#!/usr/bin/env python3
"""AI Companion device — host-side API + CLI.

A small reusable wrapper over the USB CDC-ACM JSON channel (see
docs/phase5-interface-contract.md) so real features — agent hooks and the like —
don't have to touch raw serial. Two ways to use it:

1. As a library:

       from aic import AICDevice
       with AICDevice() as dev:
           dev.done(task="Website")     # screen DONE
           dev.buzz("done")             # haptic

2. As a one-shot CLI (what the Claude Code hook calls):

       python3 aic.py done                 # DONE screen + "done" buzz
       python3 aic.py waiting --task Build  # WAITING screen + "block" buzz
       python3 aic.py stopped               # STOPPED screen + "stopped" buzz
       python3 aic.py buzz done             # just the haptic
       python3 aic.py ping

The CLI is deliberately fail-soft: if no device is connected it prints a note to
stderr and exits 0, so wiring it into an agent hook never breaks the agent.

Only one process may hold the serial port at a time — quit the interactive
tools/aic_push.py before using this (and vice-versa).
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import sys
import time

try:
    import serial  # pyserial
except ImportError:
    serial = None

BAUD = 115200  # nominal; baud is a formality for USB CDC-ACM


def find_port() -> str | None:
    """First /dev/tty.usbmodem* (macOS) or /dev/ttyACM* (Linux), or None."""
    for pattern in ("/dev/tty.usbmodem*", "/dev/ttyACM*"):
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0]
    return None


class DeviceNotFound(RuntimeError):
    pass


class AICDevice:
    """Thin, synchronous client for the device's JSON channel.

    Open it (or use it as a context manager), call the screen/haptic helpers,
    then close it. Opening asserts DTR, which the device reads as "host present";
    closing drops DTR and the device falls back to its local standby screen. For
    a persistent link (screen stays put), keep one instance open.
    """

    def __init__(self, port: str | None = None):
        self.port = port or find_port()
        self.ser = None

    # -- lifecycle -----------------------------------------------------------
    def open(self, settle: float = 0.4) -> "AICDevice":
        if serial is None:
            raise RuntimeError("pyserial not installed (pip install pyserial)")
        if not self.port:
            raise DeviceNotFound("no /dev/tty.usbmodem* found — device plugged in?")
        self.ser = serial.Serial(self.port, BAUD, timeout=0.2)
        # Opening asserts DTR; the device debounces (~100ms) then sends hello and
        # brings up its RX/haptic path. Pushing bytes before that is ready drops
        # the earliest message (observed: first haptic swallowed). Let it settle.
        if settle > 0:
            time.sleep(settle)
        return self

    def close(self, drain: float = 0.2) -> None:
        if self.ser is not None:
            try:
                self.ser.flush()
                time.sleep(drain)  # let the device receive/process before DTR drops
            finally:
                self.ser.close()
                self.ser = None

    def __enter__(self) -> "AICDevice":
        return self.open()

    def __exit__(self, *exc) -> None:
        self.close()

    # -- transmit ------------------------------------------------------------
    def send(self, obj: dict) -> None:
        line = json.dumps(obj, ensure_ascii=False)
        self.ser.write((line + "\n").encode("utf-8"))
        self.ser.flush()

    # -- screen (host = source of truth) ------------------------------------
    def screen(self, state: str, **fields) -> None:
        msg = {"t": "screen", "v": 1, "state": state}
        msg.update({k: v for k, v in fields.items() if v is not None})
        self.send(msg)

    def ready(self, clock: str = "") -> None:
        self.screen("READY", clock=clock or time.strftime("%H:%M"))

    def task(self, name: str, line: str | None = None) -> None:
        self.screen("TASK", task=name, line=line)

    def permission(self, id: str, task: str, question: str) -> None:
        self.screen("PERMISSION", id=id, task=task, question=question)

    def waiting(self, task: str, id: str = "") -> None:
        self.screen("WAITING", id=id, task=task)

    def stopped(self, task: str, id: str = "") -> None:
        self.screen("STOPPED", id=id, task=task)

    def done(self, task: str, id: str = "") -> None:
        self.screen("DONE", id=id, task=task)

    # -- haptic (semantic cue; device maps to an effect) ---------------------
    def buzz(self, cue: str) -> None:
        self.send({"t": "haptic", "v": 1, "cue": cue})

    def ping(self) -> None:
        self.send({"t": "ping", "v": 1})


# --- CLI (used by agent hooks) --------------------------------------------

def _default_task() -> str:
    """A sensible label when a hook doesn't pass one: the project folder name."""
    return os.path.basename(os.getcwd()) or "agent"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="AI Companion device one-shot push")
    ap.add_argument("event", choices=["done", "waiting", "stopped", "ready",
                                       "task", "buzz", "ping"],
                    help="what to push")
    ap.add_argument("cue", nargs="?", help="for 'buzz': block|stopped|done")
    ap.add_argument("--task", help="task name to show (default: folder name)")
    ap.add_argument("--line", help="secondary status line (for 'task')")
    ap.add_argument("--id", default="", help="item id to bind a response to")
    ap.add_argument("--hold", type=float, default=2.0,
                    help="seconds to keep the screen up before releasing "
                         "(the link drops on exit and the device reverts to "
                         "standby); default 2.0")
    ap.add_argument("--no-buzz", action="store_true", help="screen only, no haptic")
    ap.add_argument("--port", help="serial port (default: auto-detect)")
    args = ap.parse_args(argv)

    task = args.task or _default_task()

    try:
        dev = AICDevice(port=args.port).open()
    except (DeviceNotFound, RuntimeError) as e:
        # Fail-soft: never break the calling hook because the device is absent.
        print(f"[aic] device not available: {e}", file=sys.stderr)
        return 0

    try:
        cue = None
        if args.event == "done":
            dev.done(task=task, id=args.id); cue = "done"
        elif args.event == "waiting":
            dev.waiting(task=task, id=args.id); cue = "block"
        elif args.event == "stopped":
            dev.stopped(task=task, id=args.id); cue = "stopped"
        elif args.event == "ready":
            dev.ready(); cue = None
        elif args.event == "task":
            dev.task(task, line=args.line); cue = None
        elif args.event == "buzz":
            dev.buzz(args.cue or "block")
            args.hold = 0.0
        elif args.event == "ping":
            dev.ping()
            args.hold = 0.0

        if cue and not args.no_buzz:
            dev.buzz(cue)

        if args.hold > 0:
            time.sleep(args.hold)
    finally:
        dev.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
