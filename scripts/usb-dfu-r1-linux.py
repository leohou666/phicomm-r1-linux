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
    ROOT / "build/artifacts/r1-linux-mainline-6.18-ak7755-pcm-clock-a5.itb"
)
DEFAULT_FIT_SHA256 = "bb59d10590d9c61add007a34c55c275766d8ea199df80759df4aea79305771f1"


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
    parser.add_argument("--no-notify", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    fit = args.fit.resolve()
    if not fit.is_file():
        raise SystemExit(f"Linux FIT does not exist: {fit}")

    if fit == DEFAULT_FIT.resolve() and not args.skip_default_hash_check:
        actual = sha256(fit)
        if actual != DEFAULT_FIT_SHA256:
            raise SystemExit(
                f"refusing unexpected default FIT: SHA-256 {actual}, "
                f"expected {DEFAULT_FIT_SHA256}"
            )

    dfu_util = shutil.which("dfu-util")
    if not dfu_util:
        raise SystemExit("dfu-util is missing; on Fedora run: sudo dnf install dfu-util")

    print(f"Linux FIT: {fit}")
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
