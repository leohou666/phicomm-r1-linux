#!/usr/bin/env python3
"""RAM-only verification of the R1 eMMC-reading SPL.

This helper owns UART2 before invoking only ``rkdeveloptool db``.  The loader
contains the already verified DDR blob and an MMC-enabled SPL, but no FIT.
The A4 probe prints the first raw MMC read.  The expected result is one block
read from mainline-MMC LBA 0x6000 with the factory trust ``TOS     `` header,
followed by a raw-image rejection.  This corresponds to Rockchip loader/write
LBA 0x4000 because the two verified address views differ by 0x2000 sectors.
Nothing is written to eMMC.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
import time
from pathlib import Path

import serial

from desktop_notify import notify


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOADER = ROOT / "build/artifacts/r1-emmc-mmc-spl-ram-test-a4-loader.bin"
DEFAULT_LOG = ROOT / "build/artifacts/r1-emmc-mmc-spl-ram-test-a4.log"
EXPECTED_LOADER_SHA256 = "429bb70e19c39d2d92de75c92895c0b417dacfb31438052a79b43b22cbc3d65d"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="RAM-only db + serial capture for the eMMC MMC-SPL candidate."
    )
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--loader", type=Path, default=DEFAULT_LOADER)
    parser.add_argument("--log", type=Path, default=DEFAULT_LOG)
    parser.add_argument("--monitor-seconds", type=float, default=12.0)
    parser.add_argument(
        "--rkdeveloptool",
        type=Path,
        default=ROOT / "rkdeveloptool/rkdeveloptool",
    )
    parser.add_argument("--no-notify", action="store_true")
    parser.add_argument(
        "--skip-default-hash-check",
        action="store_true",
        help="allow a rebuilt or modified default diagnostic loader",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if os.geteuid() != 0:
        raise SystemExit("run with sudo so MaskROM USB and the serial port are accessible")
    if args.monitor_seconds <= 0:
        raise SystemExit("--monitor-seconds must be positive")

    loader = args.loader.resolve()
    tool = args.rkdeveloptool.resolve()
    log_path = args.log.resolve()
    if not loader.is_file():
        raise SystemExit(f"loader does not exist: {loader}")
    if not tool.is_file() or not os.access(tool, os.X_OK):
        raise SystemExit(f"rkdeveloptool is not executable: {tool}")
    if loader == DEFAULT_LOADER.resolve() and not args.skip_default_hash_check:
        actual = sha256(loader)
        if actual != EXPECTED_LOADER_SHA256:
            raise SystemExit(
                f"refusing unexpected default loader SHA-256 {actual}; "
                f"expected {EXPECTED_LOADER_SHA256}"
            )

    log_path.parent.mkdir(parents=True, exist_ok=True)
    board_output = bytearray()
    command = [str(tool), "db", str(loader)]
    print(f"Loader: {loader}")
    print(f"Raw board log: {log_path}")
    print("Safety: this invokes only 'rkdeveloptool db'; it does not write eMMC.")
    print("Expected: MMC1 reads mainline LBA 0x6000 as 'TOS     ', then rejects the non-FIT image.")

    try:
        with serial.Serial(
            args.port,
            1_500_000,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.05,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
            exclusive=True,
        ) as uart:
            uart.reset_input_buffer()
            proc = subprocess.Popen(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            command_deadline = time.monotonic() + 20.0
            monitor_deadline: float | None = None
            while True:
                chunk = uart.read(4096)
                if chunk:
                    board_output.extend(chunk)
                    sys.stdout.buffer.write(chunk)
                    sys.stdout.buffer.flush()

                if proc.poll() is None:
                    if time.monotonic() >= command_deadline:
                        proc.terminate()
                        raise SystemExit("rkdeveloptool db timed out after 20 seconds")
                elif monitor_deadline is None:
                    monitor_deadline = time.monotonic() + args.monitor_seconds

                if monitor_deadline is not None and time.monotonic() >= monitor_deadline:
                    break

            host_output, _ = proc.communicate(timeout=1)
            if host_output:
                print("\n--- rkdeveloptool db ---")
                sys.stdout.buffer.write(host_output)
                sys.stdout.buffer.flush()
            status = proc.returncode
    except serial.SerialException as exc:
        raise SystemExit(f"cannot own {args.port}: {exc}") from exc
    finally:
        log_path.write_bytes(board_output)

    saw_mmc = b"Trying to boot from MMC1" in board_output
    saw_trust_read = (
        b"R1MMC sector=6000 count=1 got=1 hdr=544f532020202020"
        in board_output
    )
    saw_expected_reject = b"R1MMC raw load rejected:" in board_output
    print(
        f"\ndb_status={status} saw_mmc={int(saw_mmc)} "
        f"saw_trust_read={int(saw_trust_read)} "
        f"saw_expected_reject={int(saw_expected_reject)}"
    )
    passed = status == 0 and saw_mmc and saw_trust_read and saw_expected_reject
    if not args.no_notify:
        notify(
            "R1 MMC SPL test passed" if passed else "R1 MMC SPL test needs review",
            "Serial is free; inspect the saved log before any eMMC write.",
            urgency="normal" if passed else "critical",
        )
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
