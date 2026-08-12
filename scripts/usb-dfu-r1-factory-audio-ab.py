#!/usr/bin/env python3
"""Download the hash-pinned factory-driver audio A/B FIT to U-Boot RAM."""

from __future__ import annotations

import hashlib
import subprocess
import sys
from pathlib import Path

from desktop_notify import notify


ROOT = Path(__file__).resolve().parents[1]
FIT = ROOT / "build/artifacts/r1-linux-factory-3.10-ak7755-route-a21r5.itb"
FIT_SHA256 = "334f75358c9f109ba3115c268d11cd6b7d638f86cb90af8f61b8694f4438003c"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as image:
        for chunk in iter(lambda: image.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    if not FIT.is_file():
        raise SystemExit(f"A21r5 FIT does not exist; build it first: {FIT}")

    actual = sha256(FIT)
    if actual != FIT_SHA256:
        raise SystemExit(
            f"refusing unexpected A21r5 FIT: SHA-256 {actual}, expected {FIT_SHA256}"
        )

    print("Experiment: factory Linux 3.10 plus recovered Android AK7755 media route")
    print(f"Hash-pinned FIT: {FIT}")
    print("Safety: DFU RAM only; storage, wireless, and unrelated cards disabled.")
    result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts/usb-dfu-r1-linux.py"),
            "--fit",
            str(FIT),
            "--no-notify",
        ],
        check=False,
    )
    if result.returncode:
        notify(
            "R1 factory audio A/B DFU failed",
            f"dfu-util exited with status {result.returncode}.",
            urgency="critical",
        )
        return result.returncode

    notify(
        "R1 factory audio A/B downloaded",
        "DFU detached; U-Boot should verify and boot the RAM-only A21r5 FIT.",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
