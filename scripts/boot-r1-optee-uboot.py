#!/usr/bin/env python3
"""Start the RAM-only R1 open-OP-TEE -> U-Boot chain in one command.

The script deliberately limits its Rockchip action to ``rkdeveloptool db``.
It opens the USB-TTL first, runs db only after that port is exclusively owned,
waits for the SPL YMODEM CRC request, and then sends the known-good FIT with
the measured 50-us per-byte pacing. It never writes eMMC.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shlex
import subprocess
import sys
from pathlib import Path

from desktop_notify import notify


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOADER = ROOT / "build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-rxfast-loader.bin"
GIC_PRETEE_TRACE_LOADER = ROOT / "build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-gic-pretee-trace-loader.bin"
GIC_INT55_CLEANUP_LOADER = ROOT / "build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-gic-int55-cleanup-ab-loader.bin"
DEFAULT_FIT = ROOT / "build/artifacts/r1-ymodem-fit-dtb.itb"
RK_V2_FIT = ROOT / "build/artifacts/r1-ymodem-fit-dtb-rk-v2.00.itb"
EXPECTED_SHA256 = {
    DEFAULT_LOADER: "7220f8b7508cca136d87ba33098b8bf2851d9a1343a5442568ce2a8414aeda3f",
    GIC_PRETEE_TRACE_LOADER: "05b804d7bfdbd403c187f379dd23e332e808d75e8c0dc99d85c0cccea9f8d810",
    GIC_INT55_CLEANUP_LOADER: "ff47e369966feac248510aaa7577e54484e3ecfb80a53fef0f99d818d087bd50",
    DEFAULT_FIT: "5687f549a82d3f2e0b51fe064df05a4b623ba180872c581132e6ecbe6a49cd84",
    RK_V2_FIT: "4ebc55f53e998d85da7dd6d812935dd87818c6953eb7dea996fa4b882019ab7e",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as image:
        for chunk in iter(lambda: image.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="RAM-only MaskROM db + paced YMODEM FIT boot to open-OP-TEE U-Boot."
    )
    parser.add_argument("--port", default="/dev/ttyUSB0", help="USB-TTL serial device")
    parser.add_argument("--loader", type=Path, default=DEFAULT_LOADER)
    parser.add_argument("--fit", type=Path, default=DEFAULT_FIT)
    parser.add_argument(
        "--rkdeveloptool",
        type=Path,
        default=ROOT / "rkdeveloptool/rkdeveloptool",
        help="local rkdeveloptool executable",
    )
    parser.add_argument("--tx-gap-us", type=int, default=50)
    parser.add_argument(
        "--monitor-seconds",
        type=float,
        default=0,
        help="seconds to keep the serial bridge after YMODEM completes (default: 0)",
    )
    parser.add_argument(
        "--no-notify",
        action="store_true",
        help="do not send a desktop notification after the boot transfer",
    )
    parser.add_argument(
        "--skip-default-hash-check",
        action="store_true",
        help="allow a modified default loader or FIT (normally hashes are enforced)",
    )
    return parser.parse_args()


def validate_default_artifact(path: Path, skip_check: bool) -> None:
    if not path.is_file():
        raise SystemExit(f"required artifact does not exist: {path}")
    expected = EXPECTED_SHA256.get(path.resolve())
    if expected and not skip_check:
        actual = sha256(path)
        if actual != expected:
            raise SystemExit(
                f"refusing unexpected default artifact {path}: SHA-256 {actual}, expected {expected}; "
                "rebuild it or use --skip-default-hash-check deliberately"
            )


def main() -> int:
    args = parse_args()
    if os.geteuid() != 0:
        raise SystemExit("run with sudo so both the serial port and MaskROM USB are accessible")
    if args.tx_gap_us < 0 or args.monitor_seconds < 0:
        raise SystemExit("--tx-gap-us and --monitor-seconds must not be negative")

    loader = args.loader.resolve()
    fit = args.fit.resolve()
    tool = args.rkdeveloptool.resolve()
    validate_default_artifact(loader, args.skip_default_hash_check)
    validate_default_artifact(fit, args.skip_default_hash_check)
    if not tool.is_file() or not os.access(tool, os.X_OK):
        raise SystemExit(f"rkdeveloptool is not executable: {tool}")

    db_command = [str(tool), "db", str(loader)]
    bridge_command = [
        sys.executable,
        str(ROOT / "scripts/ymodem-serial-bridge.py"),
        "--start-command",
        shlex.join(db_command),
        "--command-timeout",
        "20",
        "--tx-gap-us",
        str(args.tx_gap_us),
        "--monitor-seconds",
        str(args.monitor_seconds),
        "--no-notify",
        args.port,
        str(fit),
    ]
    print("RAM-only sequence: open serial -> rkdeveloptool db -> wait for SPL CRC C -> YMODEM FIT")
    print(f"Loader: {loader}")
    print(f"FIT:    {fit}")
    print("Safety: this invokes only 'rkdeveloptool db'; it does not write eMMC.")
    status = subprocess.run(bridge_command, check=False).returncode
    if not args.no_notify:
        if status == 0:
            notify(
                "R1 U-Boot download complete",
                f"OP-TEE/U-Boot FIT sent; {args.port} is free for the next step.",
            )
        else:
            notify(
                "R1 U-Boot download failed",
                f"RAM-only boot transfer exited with status {status}.",
                urgency="critical",
            )
    return status


if __name__ == "__main__":
    raise SystemExit(main())
