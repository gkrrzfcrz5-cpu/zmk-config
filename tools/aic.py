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

    # -- receive (device -> host, e.g. button presses) -----------------------
    def read_json_line(self, deadline: float) -> dict | None:
        """Read one newline-delimited JSON object the device sent, or None once
        `deadline` (a time.monotonic() value) passes. Non-JSON / blank lines and
        the device's own hello are skipped."""
        buf = bytearray()
        while time.monotonic() < deadline:
            chunk = self.ser.read(64)  # Serial timeout is 0.2s, so this polls
            if not chunk:
                continue
            buf.extend(chunk)
            while b"\n" in buf:
                raw, _, rest = buf.partition(b"\n")
                del buf[:]
                buf.extend(rest)
                s = raw.decode("utf-8", "replace").strip()
                if not s:
                    continue
                try:
                    return json.loads(s)
                except json.JSONDecodeError:
                    continue
        return None

    def wait_for_button(self, keys=("YES", "NO"), timeout: float = 45.0):
        """Block until the device reports a button press whose key is in `keys`,
        or `timeout` seconds elapse. Returns the key string, or None on timeout."""
        deadline = time.monotonic() + timeout
        while True:
            obj = self.read_json_line(deadline)
            if obj is None:
                return None
            if obj.get("t") == "input" and obj.get("key") in keys:
                return obj["key"]

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


def _pretooluse_decision(decision: str, reason: str) -> None:
    """Emit a PreToolUse hook decision. This is the ONLY thing allowed on stdout
    on the gate path (every diagnostic goes to stderr) — the agent reads stdout
    as the decision."""
    print(json.dumps({
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": decision,       # allow | deny | ask
            "permissionDecisionReason": reason,
        }
    }, ensure_ascii=False))


# PermissionRequest hooks use a DIFFERENT stdout shape than PreToolUse: a nested
# decision object {"behavior":"allow"|"deny","message"?}. Empty stdout = abstain
# = fall through to the agent's normal prompt (never a silent allow/deny).
PERMREQ_LOG = "/tmp/aic-permreq.log"


def _permreq_decision(behavior: str, message: str | None = None) -> None:
    decision = {"behavior": behavior}
    if message is not None:
        decision["message"] = message
    print(json.dumps({
        "hookSpecificOutput": {
            "hookEventName": "PermissionRequest",
            "decision": decision,
        }
    }, ensure_ascii=False))


def _permreq_log(msg: str) -> None:
    """Append a timestamped line to the observe log (best-effort, never raises)."""
    try:
        with open(PERMREQ_LOG, "a", encoding="utf-8") as fh:
            fh.write(f"{time.strftime('%H:%M:%S')} {msg}\n")
    except OSError:
        pass


def _tool_detail(payload: dict) -> tuple[str, str]:
    """(tool_name, human one-liner) from a PreToolUse/PermissionRequest payload."""
    tool = payload.get("tool_name", "tool")
    ti = payload.get("tool_input") or {}
    detail = (ti.get("command") or ti.get("description") or ti.get("file_path")
              or json.dumps(ti, ensure_ascii=False))
    return tool, detail


def cmd_permreq(args) -> int:
    """PermissionRequest hook: let the device answer the prompts the agent would
    genuinely raise (unlike PreToolUse's gate, this event fires only when a
    prompt is actually needed). `--observe` logs when it fires but touches
    nothing (fall through to the normal prompt) — used to confirm the firing
    behaviour before going live. Fail-soft: no device / no answer / timeout all
    fall through (empty stdout, exit 0), never a silent decision."""
    try:
        payload = json.loads(sys.stdin.read())
    except (json.JSONDecodeError, ValueError):
        payload = {}
    tool, detail = _tool_detail(payload)

    # permreq: no marker by default (the event itself is the filter).
    marker = args.marker or ""
    if marker and marker not in f"{tool} {detail}":
        _permreq_log(f"FIRED tool={tool} — passthrough (no marker)")
        return 0

    if args.observe:
        _permreq_log(f"PermissionRequest  tool={tool} detail={detail[:120]!r} (observe)")
        return 0  # log only; fall through to the normal prompt

    _permreq_log(f"FIRED tool={tool} detail={detail[:120]!r} (live)")
    try:
        dev = AICDevice(port=args.port).open()
    except (DeviceNotFound, RuntimeError) as e:
        _permreq_log(f"  device unavailable, passthrough: {e}")
        print(f"[aic] permreq: device unavailable, passing through: {e}",
              file=sys.stderr)
        return 0

    try:
        question = detail if len(detail) <= 60 else detail[:57] + "..."
        dev.permission(id="permreq", task=f"{tool} 要執行", question=question)
        if not args.no_buzz:
            dev.buzz("block")  # 要權限 = 嗒—嗒
        key = dev.wait_for_button(keys=("YES", "NO"), timeout=args.timeout)
    finally:
        dev.close()

    if key == "YES":
        _permreq_log("  -> allow (YES)")
        _permreq_decision("allow")
    elif key == "NO":
        _permreq_log("  -> deny (NO)")
        _permreq_decision("deny", "使用者在 AI Companion 裝置上按了 ✗ 拒絕")
    else:
        # No press: abstain (empty stdout) -> the agent's normal prompt.
        _permreq_log("  -> timeout, passthrough")
        print("[aic] permreq: no button within timeout, passing through",
              file=sys.stderr)
    return 0


def cmd_notify(args) -> int:
    """Notification hook: fires when the agent needs the user's attention —
    historically (a) when it raises a tool-permission prompt and (b) when the
    prompt has been idle ~60s waiting for input. It is ONE-DIRECTIONAL: it can
    only notify, never answer, so the device just buzzes + shows a heads-up and
    the user replies on the Mac.

    `--observe` logs the message + payload keys without touching the device, to
    confirm the trigger set and the exact `message` strings for THIS Optimus
    build before we wire real behaviour on them (PermissionRequest was assumed
    to work and turned out dead — don't repeat that). Always exit 0,
    fire-and-forget: a Notification hook must never block the session."""
    try:
        payload = json.loads(sys.stdin.read())
    except (json.JSONDecodeError, ValueError):
        payload = {}
    msg = payload.get("message", "")
    keys = ",".join(sorted(payload.keys()))

    if args.observe:
        _permreq_log(f"Notification  msg={msg[:140]!r} keys=[{keys}] (observe)")
        return 0

    # live: classify by message text (strings confirmed via --observe first).
    # "permission / 權限 / approve / use" => a blocking ask; else treat as idle.
    low = msg.lower()
    is_perm = any(w in low for w in ("permission", "approve", "allow", "使用", "權限"))
    try:
        dev = AICDevice(port=args.port).open()
    except (DeviceNotFound, RuntimeError) as e:
        _permreq_log(f"Notification live: device unavailable: {e}")
        print(f"[aic] notify: device unavailable: {e}", file=sys.stderr)
        return 0
    try:
        if is_perm:
            dev.permission(id="notify", task="Optimus 要你了",
                           question=msg[:57] + "..." if len(msg) > 60 else msg)
            if not args.no_buzz:
                dev.buzz("block")   # 要權限 = 嗡—嗡
        else:
            dev.waiting(task="Optimus 在等你")
            if not args.no_buzz:
                dev.buzz("block")
        time.sleep(args.hold if args.hold > 0 else 1.5)
    finally:
        dev.close()
    return 0


def cmd_gate(args) -> int:
    """PreToolUse hook: let the physical device approve/deny a tool call.

    Reads the hook payload on stdin. To stay out of the way during normal work it
    only engages the device when the tool call carries the marker string (default
    "AIC-GATE"); every other call passes straight through (exit 0, no stdout =
    fall through to the normal prompt). Fail-soft throughout: no device / no
    answer -> normal prompt, never block the agent.

    Flow: show a PERMISSION screen + "block" buzz, wait for a YES/NO button, map
    YES->allow and NO->deny; timeout -> ask (normal prompt).
    """
    try:
        payload = json.load(sys.stdin)
    except (json.JSONDecodeError, ValueError):
        return 0  # not a hook payload -> passthrough

    tool, detail = _tool_detail(payload)
    if args.observe:
        _permreq_log(f"PreToolUse  tool={tool} detail={detail[:120]!r} (observe)")
        return 0  # log only; do not gate
    # gate: marker required by default so it stays dormant during normal work.
    marker = "AIC-GATE" if args.marker is None else args.marker
    if marker and marker not in f"{tool} {detail}":
        return 0  # not our test call -> instant passthrough, device untouched

    try:
        dev = AICDevice(port=args.port).open()
    except (DeviceNotFound, RuntimeError) as e:
        print(f"[aic] gate: device unavailable, passing through: {e}",
              file=sys.stderr)
        return 0  # device gone -> let the agent prompt normally

    try:
        question = detail if len(detail) <= 60 else detail[:57] + "..."
        dev.permission(id="gate", task=f"{tool} 要執行", question=question)
        if not args.no_buzz:
            dev.buzz("block")  # 要權限 = 嗒—嗒
        key = dev.wait_for_button(keys=("YES", "NO"), timeout=args.timeout)
    finally:
        dev.close()

    if key == "YES":
        _pretooluse_decision("allow", "使用者在 AI Companion 裝置上按了 ✓ 批准")
    elif key == "NO":
        _pretooluse_decision("deny", "使用者在 AI Companion 裝置上按了 ✗ 拒絕")
    else:
        print("[aic] gate: no button within timeout, passing through",
              file=sys.stderr)
        _pretooluse_decision("ask", "裝置逾時未回應,改用一般提示")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="AI Companion device one-shot push")
    ap.add_argument("event", choices=["done", "waiting", "stopped", "ready",
                                       "task", "buzz", "ping", "gate", "permreq",
                                       "notify"],
                    help="what to push ('gate' = PreToolUse hook, "
                         "'permreq' = PermissionRequest hook, "
                         "'notify' = Notification hook)")
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
    ap.add_argument("--marker", default=None,
                    help="gate/permreq: only engage the device when the tool "
                         "call contains this string (gate defaults to AIC-GATE; "
                         "permreq defaults to none)")
    ap.add_argument("--observe", action="store_true",
                    help="permreq: log when the hook fires but don't touch the "
                         "device (fall through to the normal prompt)")
    ap.add_argument("--timeout", type=float, default=45.0,
                    help="gate/permreq: seconds to wait for a button press")
    args = ap.parse_args(argv)

    if args.event == "gate":
        return cmd_gate(args)
    if args.event == "permreq":
        return cmd_permreq(args)
    if args.event == "notify":
        return cmd_notify(args)

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
