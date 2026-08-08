#!/usr/bin/env python3
"""Try to interrupt a zero-delay U-Boot by continuously feeding its UART.

This script only reads and writes the serial port. It does not issue any
U-Boot command and does not write board storage.
"""

from __future__ import annotations

import argparse
import pathlib
import sys
import threading

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: python3 -m pip install pyserial")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Continuously send spaces while capturing serial output, to test "
            "whether U-Boot accepts a key with bootdelay=0."
        )
    )
    parser.add_argument("device", help="serial device, for example /dev/ttyUSB0")
    parser.add_argument(
        "--baud", type=int, default=1_500_000, help="baud rate (default: 1500000)"
    )
    parser.add_argument(
        "--log",
        type=pathlib.Path,
        default=pathlib.Path("backup/uboot-interrupt.log"),
        help="raw serial log path (default: backup/uboot-interrupt.log)",
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=0.01,
        help="seconds between transmitted spaces (default: 0.01)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.interval <= 0:
        sys.exit("--interval must be greater than zero")

    args.log.parent.mkdir(parents=True, exist_ok=True)
    stop_sending = threading.Event()
    stop_all = threading.Event()

    try:
        port = serial.Serial(
            args.device,
            args.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1,
            write_timeout=0.5,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
            exclusive=True,
        )
    except (OSError, serial.SerialException) as exc:
        sys.exit(f"cannot open {args.device}: {exc}")

    # Do not use Enter here: an unconsumed Enter activates the kernel FIQ
    # debugger. A space is harmless if it reaches the Android shell instead.
    def sender() -> None:
        while not stop_sending.is_set():
            try:
                port.write(b" ")
                port.flush()
            except (OSError, serial.SerialException):
                stop_all.set()
                return
            stop_sending.wait(args.interval)

    sender_thread = threading.Thread(target=sender, name="uart-key-sender", daemon=True)

    print(f"Opened {args.device} at {args.baud} 8N1, flow control off.")
    print("Now power-cycle or reset the board. Sending spaces until U-Boot stops or the kernel starts.")
    print(f"Raw output is being saved to {args.log}. Press Ctrl-C to quit.\n")

    sender_thread.start()
    recent = bytearray()
    result: str | None = None

    try:
        with args.log.open("wb") as log_file:
            while not stop_all.is_set():
                data = port.read(port.in_waiting or 1)
                if not data:
                    continue

                log_file.write(data)
                log_file.flush()
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()

                recent.extend(data)
                if len(recent) > 4096:
                    del recent[:-4096]

                normalized = bytes(recent).replace(b"\x00", b"")
                if b"Starting kernel" in normalized:
                    stop_sending.set()
                    result = "kernel"
                    break

                # Common prompts are `=> ` or a board name followed by `# `.
                if b"=> " in normalized or b"# " in normalized:
                    stop_sending.set()
                    result = "uboot"
                    break
    except KeyboardInterrupt:
        result = "cancelled"
    finally:
        stop_sending.set()
        stop_all.set()
        sender_thread.join(timeout=1)
        port.close()

    print("\n")
    if result == "uboot":
        print("A U-Boot prompt was detected. Reopen it with picocom before entering commands.")
        return 0
    if result == "kernel":
        print(
            "U-Boot still started the kernel. It likely lacks zero-bootdelay key checking; "
            "repeated UART input cannot interrupt this build."
        )
        return 1
    print("Stopped by user; inspect the saved log for the last boot stage.")
    return 130


if __name__ == "__main__":
    raise SystemExit(main())
