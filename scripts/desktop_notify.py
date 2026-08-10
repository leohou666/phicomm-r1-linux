"""Best-effort desktop notifications for scripts commonly run through sudo."""

from __future__ import annotations

import os
import pwd
import shutil
import subprocess
import sys
from pathlib import Path


def notify(summary: str, body: str, *, urgency: str = "normal") -> bool:
    """Send a freedesktop notification without affecting the caller's result."""
    notify_send = shutil.which("notify-send")
    if not notify_send:
        print("Desktop notification skipped: notify-send is not installed.", file=sys.stderr)
        return False

    command = [
        notify_send,
        "--app-name=Phicomm R1",
        f"--urgency={urgency}",
        "--expire-time=10000",
        summary,
        body,
    ]
    env = os.environ.copy()

    # sudo normally leaves the root process outside the graphical user's
    # D-Bus session.  Re-enter the invoking user's runtime directory and bus.
    sudo_user = env.get("SUDO_USER")
    if os.geteuid() == 0 and sudo_user and sudo_user != "root":
        try:
            user = pwd.getpwnam(sudo_user)
        except KeyError:
            print(
                f"Desktop notification skipped: unknown SUDO_USER {sudo_user!r}.",
                file=sys.stderr,
            )
            return False
        runtime_dir = Path("/run/user") / str(user.pw_uid)
        bus = runtime_dir / "bus"
        runuser = shutil.which("runuser")
        if not runuser or not bus.exists():
            print(
                f"Desktop notification skipped: no D-Bus session bus at {bus}.",
                file=sys.stderr,
            )
            return False
        command = [
            runuser,
            "-u",
            sudo_user,
            "--",
            "env",
            f"XDG_RUNTIME_DIR={runtime_dir}",
            f"DBUS_SESSION_BUS_ADDRESS=unix:path={bus}",
            *command,
        ]

    try:
        result = subprocess.run(
            command,
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            timeout=5,
            env=env,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        print(f"Desktop notification failed: {exc}", file=sys.stderr)
        return False
    if result.returncode:
        detail = result.stderr.strip() or f"status {result.returncode}"
        print(f"Desktop notification failed: {detail}", file=sys.stderr)
        return False
    return True
