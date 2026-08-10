#!/usr/bin/env python3
"""Send one YMODEM file over a raw serial port and retain both wire directions.

This avoids terminal-emulator file-transfer layers. The YMODEM sender sees a
pseudo-terminal, while this process alone owns the physical USB-TTL port and
writes every board-to-host byte to a raw log. After the sender exits, the
serial port is released immediately by default and a desktop notification is
sent; an optional bounded observation window can still be requested.
"""

from __future__ import annotations

import argparse
import os
import pty
import selectors
import shlex
import subprocess
import sys
import time
import tty
from pathlib import Path

import serial

from desktop_notify import notify


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run sb through a raw serial bridge and retain board output."
    )
    parser.add_argument("port", help="serial device, e.g. /dev/ttyUSB0")
    parser.add_argument("image", type=Path, help="YMODEM image to send")
    parser.add_argument(
        "--baud", type=int, default=1_500_000,
        help="serial baud rate (default: 1500000)",
    )
    parser.add_argument(
        "--monitor-seconds", type=float, default=0,
        help="seconds to print board output after sb exits (default: 0)",
    )
    parser.add_argument(
        "--log", type=Path, default=Path("build/artifacts/r1-ymodem-serial.log"),
        help="raw board-to-host log path",
    )
    parser.add_argument(
        "--tx-log",
        type=Path,
        default=Path("build/artifacts/r1-ymodem-serial-tx.log"),
        help="raw host-to-board log path",
    )
    parser.add_argument(
        "--tx-gap-us",
        type=int,
        default=0,
        help="minimum gap after each transmitted byte, in microseconds",
    )
    receiver_group = parser.add_mutually_exclusive_group()
    receiver_group.add_argument(
        "--command",
        help=(
            "send this U-Boot command plus CR, then wait for its YMODEM "
            "receiver request before starting sb"
        ),
    )
    receiver_group.add_argument(
        "--start-command",
        help=(
            "run this host command after the serial port is exclusively open, "
            "then wait for the board YMODEM receiver request before starting sb; "
            "the command is parsed without a shell"
        ),
    )
    parser.add_argument(
        "--command-timeout",
        type=float,
        default=10,
        help=(
            "seconds to wait for the YMODEM request after --command or "
            "--start-command (default: 10)"
        ),
    )
    parser.add_argument(
        "--no-notify",
        action="store_true",
        help="do not send a desktop notification after the transfer",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.image.is_file():
        raise SystemExit(f"image does not exist: {args.image}")
    if args.monitor_seconds < 0:
        raise SystemExit("--monitor-seconds must not be negative")
    if args.tx_gap_us < 0:
        raise SystemExit("--tx-gap-us must not be negative")
    if args.command_timeout <= 0:
        raise SystemExit("--command-timeout must be positive")

    args.log.parent.mkdir(parents=True, exist_ok=True)
    args.tx_log.parent.mkdir(parents=True, exist_ok=True)
    master_fd, slave_fd = pty.openpty()
    tty.setraw(slave_fd)
    os.set_blocking(master_fd, False)

    sender_status: int | None = None
    try:
        with serial.Serial(
            args.port,
            args.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0,
            write_timeout=5,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
            exclusive=True,
        ) as board, args.log.open("wb") as raw_log, args.tx_log.open("wb") as tx_log:
            print(
                f"Bridge owns {args.port} at {args.baud} 8N1; "
                f"sending {args.image} with sb -k -vv.",
                file=sys.stderr,
            )
            print(f"Raw board log: {args.log}", file=sys.stderr)
            print(f"Raw host-to-board log: {args.tx_log}", file=sys.stderr)
            if args.tx_gap_us:
                print(
                    f"Pacing host-to-board bytes with {args.tx_gap_us} us gaps.",
                    file=sys.stderr,
                )

            if args.start_command:
                host_command = shlex.split(args.start_command)
                if not host_command:
                    raise RuntimeError("--start-command must not be empty")
                print(
                    f"Running host start command: {host_command!r}; waiting for YMODEM request.",
                    file=sys.stderr,
                )
                subprocess.run(host_command, check=True)

            if args.command or args.start_command:
                if args.command:
                    command = args.command.encode("ascii") + b"\r"
                    print(
                        f"Sending U-Boot command: {args.command!r}; waiting for YMODEM request.",
                        file=sys.stderr,
                    )
                    written = board.write(command)
                    if written != len(command):
                        raise RuntimeError(
                            f"short command write to {args.port}: {written}/{len(command)} bytes"
                        )
                    board.flush()
                    tx_log.write(command)
                    tx_log.flush()

                request_deadline = time.monotonic() + args.command_timeout
                received = bytearray()
                while time.monotonic() < request_deadline:
                    data = os.read(board.fileno(), 4096)
                    if not data:
                        time.sleep(0.01)
                        continue
                    raw_log.write(data)
                    raw_log.flush()
                    received.extend(data)
                    os.write(sys.stdout.fileno(), data)
                    if b"C" in received:
                        print("YMODEM receiver requested CRC mode; starting sb.", file=sys.stderr)
                        break
                else:
                    rendered = received.decode("ascii", errors="replace")
                    raise RuntimeError(
                        "timed out waiting for a YMODEM CRC request after --command; "
                        f"board said: {rendered!r}"
                    )

            sender = subprocess.Popen(
                ["sb", "-k", "-vv", str(args.image)],
                stdin=slave_fd,
                stdout=slave_fd,
            )
            selector = selectors.DefaultSelector()
            selector.register(board.fileno(), selectors.EVENT_READ, "board")
            selector.register(master_fd, selectors.EVENT_READ, "sender")

            monitor_deadline: float | None = None
            while True:
                if sender.poll() is not None and monitor_deadline is None:
                    monitor_deadline = time.monotonic() + args.monitor_seconds
                    if args.monitor_seconds:
                        message = "streaming subsequent board output"
                    else:
                        message = "releasing the serial port immediately"
                    print(f"YMODEM sender exited; {message}.", file=sys.stderr)

                if monitor_deadline is not None and time.monotonic() >= monitor_deadline:
                    break

                for key, _ in selector.select(timeout=0.1):
                    if key.data == "board":
                        data = os.read(board.fileno(), 4096)
                        if not data:
                            continue
                        raw_log.write(data)
                        raw_log.flush()
                        try:
                            os.write(master_fd, data)
                        except BlockingIOError:
                            pass
                        if monitor_deadline is not None:
                            os.write(sys.stdout.fileno(), data)
                    else:
                        data = os.read(master_fd, 4096)
                        if data:
                            if args.tx_gap_us:
                                for value in data:
                                    written = board.write(bytes((value,)))
                                    if written != 1:
                                        raise RuntimeError(
                                            f"short write to {args.port}: {written}/1 byte"
                                        )
                                    board.flush()
                                    time.sleep(args.tx_gap_us / 1_000_000)
                            else:
                                written = board.write(data)
                                if written != len(data):
                                    raise RuntimeError(
                                        f"short write to {args.port}: {written}/{len(data)} bytes"
                                    )
                                board.flush()
                            tx_log.write(data)
                            tx_log.flush()

            sender_status = sender.wait()
    except serial.SerialException as exc:
        if not args.no_notify:
            notify(
                "R1 YMODEM failed",
                f"Could not use {args.port}: {exc}",
                urgency="critical",
            )
        raise SystemExit(f"cannot use {args.port}: {exc}") from exc
    finally:
        os.close(master_fd)
        os.close(slave_fd)

    assert sender_status is not None
    if not args.no_notify:
        if sender_status == 0:
            notify(
                "R1 YMODEM download complete",
                f"{args.image.name} sent; {args.port} is free for the next command.",
            )
        else:
            notify(
                "R1 YMODEM failed",
                f"{args.image.name}: sb exited with status {sender_status}.",
                urgency="critical",
            )
    return sender_status


if __name__ == "__main__":
    raise SystemExit(main())
