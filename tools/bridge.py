#!/usr/bin/env python3
"""AI Companion — Mac Bridge (+ Mock AI) simulator.

The Bridge is the middle layer of the Phase-1 architecture
(docs/phase5-interface-contract.md §0):

    AI Software  <->  Mac Bridge  <->  XIAO device
    (Mock here)      (this file)       (LCD/haptic/buttons)

It owns the transport, translates high-level AI events into the contract's
`screen`/`haptic` messages, reads the device's `input` (button) messages back,
and turns them into AI actions (allow/deny/ack/open). All product logic lives
here on the Mac; the device stays a thin client.

Two seams are deliberately swappable so nothing downstream has to change later:

  * Transport  — LoopbackTransport (a headless fake device, no hardware) ->
                 UsbTransport (real device over USB CDC) -> BleTransport (later).
  * AI source  — the MockAI scenario in run_demo() -> the real AI Software.

Run the built-in 5-step demo:

    python3 tools/bridge.py --loopback   # headless, auto-presses, NO hardware
    python3 tools/bridge.py              # real device over USB (you press)

The Bridge keeps ONE transport open for its whole lifetime — the contract's
"long-lived, exclusive port owner" rule (§10). Never open/close per message.
"""
from __future__ import annotations

import argparse
import queue
import sys
import threading
import time

# tools/ is on sys.path[0] when run as a script, so this finds tools/aic.py.
from aic import AICDevice, DeviceNotFound


# --- transport abstraction -------------------------------------------------
#
# Everything above this line speaks the same three-verb contract: send(obj),
# read_input(deadline). Swapping USB <-> BLE <-> loopback is swapping this class.

class Transport:
    def open(self) -> "Transport":
        return self

    def close(self) -> None:
        pass

    def send(self, obj: dict) -> None:
        raise NotImplementedError

    def read_input(self, deadline: float) -> dict | None:
        """Return the next device->host `input` message, or None once `deadline`
        (a time.monotonic() value) passes."""
        raise NotImplementedError

    def __enter__(self) -> "Transport":
        return self.open()

    def __exit__(self, *exc) -> None:
        self.close()


class UsbTransport(Transport):
    """Real device over USB CDC. Wraps one AICDevice kept open for the daemon's
    whole lifetime (contract §10: one process per port, long-lived Bridge)."""

    def __init__(self, port: str | None = None):
        self.dev = AICDevice(port=port)

    def open(self) -> "UsbTransport":
        self.dev.open()
        return self

    def close(self) -> None:
        self.dev.close()

    def send(self, obj: dict) -> None:
        self.dev.send(obj)

    def read_input(self, deadline: float) -> dict | None:
        while time.monotonic() < deadline:
            obj = self.dev.read_json_line(deadline)
            if obj is None:
                return None
            if obj.get("t") == "input":  # skip hello/pong/anything else
                return obj
        return None


class BleTransport(Transport):
    """Placeholder for the Nordic UART Service channel (Track B spike). Same
    send/read_input surface, so run_demo() runs unchanged once this is real."""

    def open(self) -> "BleTransport":
        raise NotImplementedError(
            "BLE transport not implemented yet — see the Track B / NUS spike")


class LoopbackTransport(Transport):
    """Headless fake device: records every screen/haptic the Bridge sends and
    lets the scenario inject button presses, so the whole Bridge + Mock-AI loop
    runs with NO hardware. Swap in UsbTransport when the board is back; nothing
    else changes."""

    _FEEL = {
        "done": "嗡(短強一下)",
        "block": "嗡—嗡(兩下)",
        "stopped": "嗡———(長震)",
    }

    def __init__(self, verbose: bool = True):
        self.sent: list[dict] = []          # every message the Bridge pushed
        self._presses: queue.Queue = queue.Queue()
        self.verbose = verbose

    def send(self, obj: dict) -> None:
        self.sent.append(obj)
        if self.verbose:
            self._render(obj)

    def inject_press(self, key: str, screen: str = "", id: str = "") -> None:
        msg = {"t": "input", "v": 1, "src": "button", "key": key, "screen": screen}
        if id:
            msg["id"] = id
        self._presses.put(msg)

    def read_input(self, deadline: float) -> dict | None:
        timeout = max(0.0, deadline - time.monotonic())
        try:
            return self._presses.get(timeout=timeout)
        except queue.Empty:
            return None

    def _render(self, obj: dict) -> None:
        t = obj.get("t")
        if t == "screen":
            bits = [f"[OLED] {obj.get('state', '')}"]
            for k in ("task", "line", "question", "clock"):
                if obj.get(k):
                    bits.append(f"{k}={obj[k]}")
            print("        " + "  ".join(bits))
        elif t == "haptic":
            cue = obj.get("cue", "")
            print(f"        [馬達] {cue} → {self._FEEL.get(cue, cue)}")


# --- the Bridge ------------------------------------------------------------

class MacBridge:
    """Translate AI events <-> device messages, and log every hop."""

    def __init__(self, transport: Transport, logf=None):
        self.transport = transport
        self._logf = logf

    def log(self, msg: str) -> None:
        line = f"{time.strftime('%H:%M:%S')} {msg}"
        print(line)
        if self._logf:
            self._logf.write(line + "\n")
            self._logf.flush()

    # -- host -> device -----------------------------------------------------
    def _screen(self, state: str, **fields) -> None:
        msg = {"t": "screen", "v": 1, "state": state}
        msg.update({k: v for k, v in fields.items() if v is not None})
        self.transport.send(msg)

    def _haptic(self, cue: str) -> None:
        self.transport.send({"t": "haptic", "v": 1, "cue": cue})

    def show_task(self, task: str, line: str | None = None) -> None:
        self.log(f"[Bridge] → 裝置顯示 TASK(WORKING):{task} — {line or ''}")
        self._screen("TASK", task=task, line=line)

    def request_permission(self, id: str, task: str, question: str,
                           timeout: float = 45.0) -> str:
        """Show PERMISSION + block buzz, wait for Yes/No. Returns
        'allow' | 'deny' | 'timeout'. The caller (AI) re-checks `id` validity."""
        self.log(f"[Bridge] → 裝置顯示 PERMISSION + 震動(block):{question}")
        self._screen("PERMISSION", id=id, task=task, question=question)
        self._haptic("block")
        deadline = time.monotonic() + timeout
        while True:
            obj = self.transport.read_input(deadline)
            if obj is None:
                self.log("[Bridge] 逾時無人回應 → 交還 AI 走一般提示")
                return "timeout"
            key = obj.get("key")
            if key == "YES":
                self.log(f"[Bridge] ← 收到按鍵 YES(id={obj.get('id')})→ 換算成 allow")
                return "allow"
            if key == "NO":
                self.log(f"[Bridge] ← 收到按鍵 NO → 換算成 deny")
                return "deny"
            self.log(f"[Bridge] ← 收到 {key},PERMISSION 只認 Yes/No,忽略")

    def notify_done(self, task: str, result: str | None = None,
                    id: str = "d1") -> None:
        self.log(f"[Bridge] → 裝置顯示 DONE + 震動(done):{result or ''}")
        # DONE 的協定欄位是 id+task;result 放進 line 當附帶說明(裝置可顯示可略)。
        self._screen("DONE", id=id, task=task, line=result)
        self._haptic("done")

    def notify_error(self, task: str, detail: str | None = None,
                     id: str = "e1") -> None:
        self.log(f"[Bridge] → 裝置顯示 STOPPED + 震動(stopped):{detail or ''}")
        self._screen("STOPPED", id=id, task=task)
        self._haptic("stopped")

    def wait_ack(self, keys=("OPEN", "YES", "NO", "VOICE"),
                 timeout: float = 45.0) -> str | None:
        deadline = time.monotonic() + timeout
        while True:
            obj = self.transport.read_input(deadline)
            if obj is None:
                self.log("[Bridge] 逾時,無確認")
                return None
            key = obj.get("key")
            if key in keys:
                self.log(f"[Bridge] ← 收到確認鍵 {key}")
                return key

    def open_result(self, task: str) -> None:
        self.log(f"[MockAI] Open → 開啟「{task}」的模擬結果頁面"
                 f"(真 AI 這裡會開檔/開網頁)")


# --- Mock AI: the 5-step workflow demo -------------------------------------

def run_demo(bridge: MacBridge, auto_user: bool = False) -> int:
    """Mock AI runs one full workflow (contract §0 Phase-1 pass criterion):
    WORKING -> permission -> Yes -> DONE -> Open. With auto_user (loopback), a
    simulated user auto-presses; on a real device you press the buttons."""
    tp = bridge.transport

    def auto(key: str, screen: str, id: str = "", delay: float = 0.6) -> None:
        """Schedule a simulated button press (loopback headless only)."""
        if auto_user and isinstance(tp, LoopbackTransport):
            threading.Timer(delay, lambda: tp.inject_press(key, screen, id)).start()

    bridge.log("=== Demo:模擬一次完整 AI 工作流程 ===")

    # 1) AI 開始執行任務 → 裝置顯示 WORKING
    bridge.show_task("Website Redesign", "Building...")
    time.sleep(1.0)

    # 2) AI 要求執行測試 → 裝置震動 + 顯示 PERMISSION,等 Yes/No
    if not auto_user:
        bridge.log(">>> 請在裝置上按 Yes(D2)批准 <<<")
    auto("YES", "PERMISSION", "p1")
    decision = bridge.request_permission(
        "p1", "Website Redesign", "Run 248 tests?", timeout=45)
    bridge.log(f"[MockAI] 收到權限結果:{decision}")
    if decision != "allow":
        bridge.log("[MockAI] 未獲批准,流程中止。")
        return 0

    # 3) 批准後 AI 跑測試
    bridge.show_task("Website Redesign", "Running tests...")
    time.sleep(1.2)

    # 4) 任務完成 → 裝置顯示 DONE + 震動
    bridge.notify_done("Website Redesign", "12 tests passed")
    if not auto_user:
        bridge.log(">>> 請在裝置上按 Open(D10)開啟結果(或任一鍵確認)<<<")
    auto("OPEN", "DONE", "d1")
    key = bridge.wait_ack(timeout=45)

    # 5) 使用者按 Open → Mac 開啟對應的模擬結果頁
    if key == "OPEN":
        bridge.open_result("Website Redesign")
    elif key is not None:
        bridge.log(f"[MockAI] 收到確認鍵 {key}(非 Open,視為知道了)")

    bridge.log("=== Demo 完成:雙向通訊 / 畫面 / 按鍵 / 震動 全部走過一遍 ===")
    return 0


# --- 12-screen tour --------------------------------------------------------
#
# Walk the design's 12 Core Screen States once. Each entry maps a visual screen
# to the protocol `state` that drives it (contract §4.5); state=None marks the
# local/voice screens the host does NOT push a `screen` for — noted, not faked.
TOUR = [
    ("HOME",        "READY",      {"clock": "10:24"},                                          None,      ""),
    ("SESSIONS",    None,         {},                                                          None,      "任務清單:走 tasks 訊息、裝置端瀏覽"),
    ("WORKING",     "TASK",       {"task": "Website Redesign", "line": "Building 68%"},         None,      ""),
    ("NEED YOU",    "WAITING",    {"task": "Website Redesign", "id": "n1"},                     "block",   ""),
    ("PERMISSIONS", "PERMISSION", {"task": "3 pending", "question": "Run tests?", "id": "q1"},  "block",   "權限佇列:host 一次推一件"),
    ("PERMISSION",  "PERMISSION", {"task": "Website Redesign", "question": "Run 248 tests?", "id": "p1"}, "block", ""),
    ("DONE",        "DONE",       {"task": "Website Redesign", "line": "12 tests passed", "id": "d1"}, "done", ""),
    ("ATTENTION",   "STOPPED",    {"task": "Website Redesign", "id": "e1"},                     "stopped", "出錯:暫映到 STOPPED(語意待對齊,契約 §9)"),
    ("TIMEOUT",     "WAITING",    {"task": "Website Redesign", "id": "t1"},                     "block",   "跑太久:等你決定"),
    ("LISTENING",   None,         {},                                                          None,      "語音聆聽:Voice 鈕本地觸發"),
    ("PROCESSING",  None,         {},                                                          None,      "語音理解:同上"),
    ("OPENING",     None,         {},                                                          None,      "開啟結果:Open 鈕本地行為"),
]


def run_tour(bridge: MacBridge, hold: float = 3.0) -> int:
    """Show all 12 visual screens once: the 8 host-drivable ones render on the
    real screen (+ their haptic), the 4 local/voice ones are noted only."""
    bridge.log("=== 12 畫面巡覽(能推的 8 個上真螢幕,4 個本地/語音僅註記)===")
    for i, (name, state, fields, cue, note) in enumerate(TOUR, 1):
        tag = f"[{i:2d}/12] {name}"
        if state is None:
            bridge.log(f"{tag} —(本地/語音,host 不推 screen){'  ' + note if note else ''}")
            continue
        bridge.log(f"{tag} → {state}{'  ' + note if note else ''}")
        bridge._screen(state, **fields)
        if cue:
            bridge._haptic(cue)
        time.sleep(hold)
    bridge.log("=== 巡覽完成 ===")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="AI Companion Mac Bridge + Mock AI")
    ap.add_argument("--loopback", action="store_true",
                    help="headless fake device with auto-presses (no hardware)")
    ap.add_argument("--demo", action="store_true",
                    help="run the 5-step workflow demo (default action)")
    ap.add_argument("--tour", action="store_true",
                    help="show all 12 visual screens once (auto-advance)")
    ap.add_argument("--hold", type=float, default=3.0,
                    help="tour: seconds to hold each screen (default 3.0)")
    ap.add_argument("--port", help="serial port (real device; default: auto)")
    ap.add_argument("--log", help="also append the event log to this file")
    args = ap.parse_args(argv)

    transport: Transport = (LoopbackTransport() if args.loopback
                            else UsbTransport(port=args.port))
    logf = open(args.log, "a", encoding="utf-8") if args.log else None
    bridge = MacBridge(transport, logf=logf)

    try:
        transport.open()
    except (DeviceNotFound, RuntimeError, NotImplementedError) as e:
        print(f"[bridge] 傳輸層不可用:{e}", file=sys.stderr)
        return 1

    try:
        if args.tour:
            return run_tour(bridge, hold=args.hold)
        return run_demo(bridge, auto_user=args.loopback)
    finally:
        transport.close()
        if logf:
            logf.close()


if __name__ == "__main__":
    raise SystemExit(main())
