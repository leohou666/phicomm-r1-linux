#!/usr/bin/env python3
"""Wait for one Rockchip MaskROM device, then run the RAM-only R1 boot flow.

The resulting boot helper performs only ``rkdeveloptool db`` and paced UART
YMODEM delivery.  It never invokes a storage write/erase command.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TOOL = ROOT / "rkdeveloptool" / "rkdeveloptool"
DEFAULT_BOOT = ROOT / "scripts" / "boot-r1-optee-uboot.py"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Repeatedly wait for RK3229 MaskROM, then run the RAM-only OP-TEE/U-Boot flow."
    )
    parser.add_argument("--port", default="/dev/ttyUSB0", help="USB-TTL serial device")
    parser.add_argument("--poll-seconds", type=float, default=0.5, help="USB poll interval")
    parser.add_argument("--rkdeveloptool", type=Path, default=DEFAULT_TOOL)
    parser.add_argument(
        "--monitor-seconds",
        type=float,
        default=0.0,
        help="seconds to keep the serial bridge after YMODEM completes (default: 0)",
    )
    return parser.parse_args()


def find_maskrom(tool: Path) -> list[str]:
    result = subprocess.run([str(tool), "ld"], text=True, capture_output=True, check=False)
    return [
        line
        for line in result.stdout.splitlines()
        if "Vid=0x2207" in line and "Pid=0x320b" in line and "Maskrom" in line
    ]


def main() -> int:
    args = parse_args()
    if os.geteuid() != 0:
        raise SystemExit("run with sudo so MaskROM USB and the serial port are accessible")
    if args.poll_seconds <= 0 or args.monitor_seconds < 0:
        raise SystemExit("poll and monitor durations must be non-negative (poll must be positive)")
    tool = args.rkdeveloptool.resolve()
    if not tool.is_file() or not os.access(tool, os.X_OK):
        raise SystemExit(f"rkdeveloptool is not executable: {tool}")

    last_state = ""
    armed = True
    print("Watching for exactly one Rockchip MaskROM device (2207:320b).")
    print("Each newly detected session runs only RAM 'db' plus UART YMODEM; it never writes eMMC.")
    while True:
        devices = find_maskrom(tool)
        if not armed:
            if not devices:
                print("Previous USB session disappeared; watcher is armed again.")
                armed = True
                last_state = ""
            time.sleep(args.poll_seconds)
            continue
        if len(devices) == 1:
            print(f"MaskROM detected: {devices[0]}")
            command = [
                sys.executable,
                str(DEFAULT_BOOT),
                "--port",
                args.port,
                "--rkdeveloptool",
                str(tool),
                "--monitor-seconds",
                str(args.monitor_seconds),
            ]
            status = subprocess.run(command, check=False).returncode
            print(f"RAM-only boot attempt exited with status {status}; releasing serial and waiting for USB re-enumeration.")
            # rkdeveloptool labels the RAM usbplug stage as Maskrom too.  Do
            # not retrigger until this USB session has disappeared.
            armed = False
            last_state = ""
            continue
        state = "none" if not devices else f"{len(devices)} matching devices"
        if state != last_state:
            print(f"Waiting: {state}.")
            last_state = state
        time.sleep(args.poll_seconds)


if __name__ == "__main__":
    raise SystemExit(main())
