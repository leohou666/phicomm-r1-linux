#!/usr/bin/env python3
"""Download the hash-pinned R1 Linux rescue FIT through U-Boot DFU RAM.

The target must already be at the U-Boot ``dfu 0 ram 0`` command.  This
script only selects the ``linux-fit`` RAM alternate, downloads one FIT, and
sends DFU detach so the target can continue with the following ``bootm``.
It never invokes a storage-backed DFU alternate.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
from pathlib import Path

from desktop_notify import notify


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FIT = (
    ROOT
    / "build/artifacts/r1-linux-mainline-6.18-ak7755-bluealsa-a25r4.itb"
)
DEFAULT_FIT_SHA256 = "236cfed1aa1b880102e7363a762986061dd20487be49b8b5dfe64f145fb72da4"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as image:
        for chunk in iter(lambda: image.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Send one hash-pinned Linux FIT to the R1 U-Boot DFU RAM alternate."
    )
    parser.add_argument("--fit", type=Path, default=DEFAULT_FIT)
    parser.add_argument(
        "--skip-default-hash-check",
        action="store_true",
        help="allow a modified default FIT deliberately",
    )
    parser.add_argument(
        "--expect-sha256",
        metavar="HEX",
        help="refuse to download unless the selected FIT has this SHA-256",
    )
    parser.add_argument("--no-notify", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    fit = args.fit.resolve()
    if not fit.is_file():
        raise SystemExit(f"Linux FIT does not exist: {fit}")

    actual = sha256(fit)
    if args.expect_sha256:
        expected = args.expect_sha256.lower()
        if len(expected) != 64 or any(c not in "0123456789abcdef" for c in expected):
            raise SystemExit("--expect-sha256 must be exactly 64 hexadecimal digits")
        if actual != expected:
            raise SystemExit(
                f"refusing unexpected FIT: SHA-256 {actual}, expected {expected}"
            )

    if fit == DEFAULT_FIT.resolve() and not args.skip_default_hash_check:
        if actual != DEFAULT_FIT_SHA256:
            raise SystemExit(
                f"refusing unexpected default FIT: SHA-256 {actual}, "
                f"expected {DEFAULT_FIT_SHA256}"
            )

    dfu_util = shutil.which("dfu-util")
    if not dfu_util:
        raise SystemExit("dfu-util is missing; on Fedora run: sudo dnf install dfu-util")

    print(f"Linux FIT: {fit}")
    print(f"SHA-256: {actual}")
    print("Target: U-Boot DFU alternate 'linux-fit' at RAM 0x6a800000")
    print("Safety: this script has no MMC/storage alternate and does not flash eMMC.")

    download = [
        dfu_util,
        "-d",
        "2207:320b",
        "-a",
        "linux-fit",
        "-D",
        str(fit),
    ]
    detach = [dfu_util, "-d", "2207:320b", "-a", "linux-fit", "-e"]

    try:
        subprocess.run(download, check=True)
        subprocess.run(detach, check=True)
    except subprocess.CalledProcessError as exc:
        if not args.no_notify:
            notify(
                "R1 USB DFU failed",
                f"dfu-util exited with status {exc.returncode}.",
                urgency="critical",
            )
        return exc.returncode

    if not args.no_notify:
        notify(
            "R1 Linux FIT downloaded",
            "DFU detached; U-Boot should now verify and boot the FIT.",
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
