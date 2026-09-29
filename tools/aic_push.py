#!/usr/bin/env python3
"""AI Companion — device communication test tool (host side).

Push `screen` / `haptic` / `tasks` / `ping` messages to the device over the USB
CDC-ACM serial port, and print everything the device sends back (`hello`,
`pong`, `input`). This is a MANUAL test bench for the device firmware and a
reference implementation for the Mac/Optimus-side engineer — the real host will
send the same JSON-lines automatically from Optimus hooks. See
docs/phase5-interface-contract.md for the full protocol.

Requires pyserial:  pip install pyserial   (or: uv pip install pyserial)

Usage:
    # interactive (auto-detects /dev/tty.usbmodem*):
    python3 tools/aic_push.py

    # one-shot: send a single message then exit
    python3 tools/aic_push.py --send '{"t":"screen","v":1,"state":"READY","clock":"10:24"}'

    # explicit port
    python3 tools/aic_push.py --port /dev/tty.usbmodem1101

Interactive commands (type `help` to see them again):
    ready [HH:MM]              -> screen READY (clock standby)
    task <name> | <line>       -> screen TASK (primary running task)
    perm <id> <task> | <q>     -> screen PERMISSION (Deny/Allow)
    wait <id> <task>           -> screen WAITING (stuck, answer on Mac)
    stop <id> <task>           -> screen STOPPED (interrupted)
    done <id> <task>           -> screen DONE
    buzz <block|stopped|done>  -> haptic cue
    ping                       -> liveness check (device replies pong)
    { ... }                    -> send a raw JSON line verbatim
    quit / Ctrl-D              -> exit

The `|` splits the two text parts where shown; e.g.
    task Website Redesign | building...
    perm p1 Website Redesign | Run the test suite?
"""
from __future__ import annotations

import argparse
import glob
import json
import sys
import threading
import time

try:
    import serial  # pyserial
except ImportError:
    sys.exit("pyserial not installed. Run:  pip install pyserial")

BAUD = 115200  # nominal; baud is a formality for USB CDC-ACM


def find_port() -> str | None:
    """Return the first /dev/tty.usbmodem* (macOS) or /dev/ttyACM* (Linux)."""
    for pattern in ("/dev/tty.usbmodem*", "/dev/ttyACM*"):
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0]
    return None


def reader_thread(ser: serial.Serial) -> None:
    """Print every line the device sends, pretty-labelled by message type."""
    buf = b""
    while True:
        try:
            chunk = ser.read(256)
        except serial.SerialException:
            print("\n[port closed]")
            return
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", errors="replace").strip()
            if not text:
                continue
            try:
                msg = json.loads(text)
                t = msg.get("t", "?")
                print(f"\n<= [{t}] {text}")
            except json.JSONDecodeError:
                print(f"\n<= (non-json) {text}")
            print("aic> ", end="", flush=True)


def send(ser: serial.Serial, obj: dict) -> None:
    line = json.dumps(obj, ensure_ascii=False)
    ser.write((line + "\n").encode("utf-8"))
    ser.flush()
    print(f"=> {line}")


def build_message(cmd: str) -> dict | None:
    """Translate an interactive command line into a protocol message dict."""
    cmd = cmd.strip()
    if not cmd:
        return None
    if cmd.startswith("{"):
        return json.loads(cmd)  # raw JSON passthrough

    parts = cmd.split(maxsplit=1)
    verb = parts[0].lower()
    rest = parts[1] if len(parts) > 1 else ""

    def two(text: str) -> tuple[str, str]:
        """Split '<a> | <b>' into (a, b); if no '|', the whole thing is a."""
        if "|" in text:
            a, b = text.split("|", 1)
            return a.strip(), b.strip()
        return text.strip(), ""

    if verb == "ready":
        return {"t": "screen", "v": 1, "state": "READY", "clock": rest.strip() or "10:24"}
    if verb == "task":
        name, line = two(rest)
        return {"t": "screen", "v": 1, "state": "TASK", "task": name, "line": line}
    if verb == "perm":
        # perm <id> <task> | <question>
        head, question = two(rest)
        hid, _, task = head.partition(" ")
        return {"t": "screen", "v": 1, "state": "PERMISSION",
                "id": hid.strip(), "task": task.strip(), "question": question}
    if verb in ("wait", "stop", "done"):
        state = {"wait": "WAITING", "stop": "STOPPED", "done": "DONE"}[verb]
        hid, _, task = rest.partition(" ")
        return {"t": "screen", "v": 1, "state": state,
                "id": hid.strip(), "task": task.strip()}
    if verb == "buzz":
        return {"t": "haptic", "v": 1, "cue": rest.strip() or "block"}
    if verb == "ping":
        return {"t": "ping", "v": 1}

    print(f"unknown command: {verb!r} (type 'help')")
    return None


HELP = __doc__.split("Interactive commands", 1)[1]


def main() -> int:
    ap = argparse.ArgumentParser(description="AI Companion device test tool")
    ap.add_argument("--port", help="serial port (default: auto-detect usbmodem)")
    ap.add_argument("--send", help="send one JSON message and exit")
    args = ap.parse_args()

    port = args.port or find_port()
    if not port:
        return "No /dev/tty.usbmodem* found. Is the device plugged in? Use --port."

    try:
        ser = serial.Serial(port, BAUD, timeout=0.2)
    except serial.SerialException as e:
        return f"Could not open {port}: {e}"
    print(f"[opened {port} @ {BAUD}]")

    if args.send:
        send(ser, json.loads(args.send))
        time.sleep(0.3)
        return 0

    threading.Thread(target=reader_thread, args=(ser,), daemon=True).start()
    print("Type 'help' for commands, 'quit' to exit.")
    try:
        while True:
            try:
                cmd = input("aic> ")
            except EOFError:
                break
            if cmd.strip() in ("quit", "exit"):
                break
            if cmd.strip() == "help":
                print("Interactive commands" + HELP)
                continue
            try:
                obj = build_message(cmd)
            except json.JSONDecodeError as e:
                print(f"bad JSON: {e}")
                continue
            if obj is not None:
                send(ser, obj)
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
    print("\n[closed]")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
