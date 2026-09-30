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

    def notify_waiting(self, task: str, id: str = "n1") -> None:
        """WAITING = 「需要你,但不是 Yes/No」。契約 §4.5 把 NEED YOU 和 TIMEOUT 都
        對映到這個狀態;單色螢幕只顯示表頭 WAITING + task + 固定 'waiting for you'
        (不吃 line 欄位),所以兩者在裝置上長得一樣,差別只在 task 文字。"""
        self.log(f"[Bridge] → 裝置顯示 WAITING + 震動(block):{task}")
        self._screen("WAITING", id=id, task=task)
        self._haptic("block")

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


# --- Mock AI: the scenario scripts -----------------------------------------
#
# Each scenario is one full narrative (has a beginning and an end), covering a
# branch of the design's four core events (permission / stuck / interrupted /
# done). With auto_user (loopback) a simulated user auto-presses; on a real
# device you press the buttons when prompted.

def _make_auto(bridge: MacBridge, auto_user: bool):
    """Return an auto(key, screen, id, delay) that schedules a simulated button
    press — but only in loopback (headless). On a real device it's a no-op, so
    the same scenario code drives both."""
    tp = bridge.transport

    def auto(key: str, screen: str = "", id: str = "", delay: float = 0.6) -> None:
        if auto_user and isinstance(tp, LoopbackTransport):
            threading.Timer(delay, lambda: tp.inject_press(key, screen, id)).start()

    return auto


def scenario_happy(bridge: MacBridge, auto_user: bool = False) -> int:
    """① 一切順利:開始任務 → 要權限 → Yes → 做完 → Open(契約 §0 Phase-1 收貨標準)。"""
    auto = _make_auto(bridge, auto_user)
    bridge.log("=== 劇本①  一切順利(allow → done → open) ===")

    bridge.show_task("Website Redesign", "Building...")
    time.sleep(1.0)

    if not auto_user:
        bridge.log(">>> 請在裝置上按 Yes(D2)批准 <<<")
    auto("YES", "PERMISSION", "p1")
    decision = bridge.request_permission(
        "p1", "Website Redesign", "Run 248 tests?", timeout=45)
    bridge.log(f"[MockAI] 權限結果:{decision}")
    if decision != "allow":
        bridge.log("[MockAI] 未獲批准,流程中止。")
        return 0

    bridge.show_task("Website Redesign", "Running tests...")
    time.sleep(1.2)

    bridge.notify_done("Website Redesign", "12 tests passed")
    if not auto_user:
        bridge.log(">>> 請按 Open(D10)開啟結果(或任一鍵確認)<<<")
    auto("OPEN", "DONE", "d1")
    key = bridge.wait_ack(timeout=45)
    if key == "OPEN":
        bridge.open_result("Website Redesign")
    elif key is not None:
        bridge.log(f"[MockAI] 收到確認鍵 {key}(非 Open,視為知道了)")

    bridge.log("=== 劇本① 完成 ===")
    return 0


def scenario_deny(bridge: MacBridge, auto_user: bool = False) -> int:
    """② 拒絕:你按 No → AI 停手,不執行那個動作。"""
    auto = _make_auto(bridge, auto_user)
    bridge.log("=== 劇本②  拒絕(按 No → deny → 停手) ===")

    bridge.show_task("Database Migration", "Ready to run")
    time.sleep(1.0)

    if not auto_user:
        bridge.log(">>> 請在裝置上按 No(D9)拒絕 <<<")
    auto("NO", "PERMISSION", "p2")
    decision = bridge.request_permission(
        "p2", "Database Migration", "Drop old tables?", timeout=45)
    bridge.log(f"[MockAI] 權限結果:{decision}")
    if decision == "deny":
        bridge.log("[MockAI] 你拒絕了 → 不執行,任務擱置。")

    bridge.log("=== 劇本② 完成 ===")
    return 0


def scenario_timeout(bridge: MacBridge, auto_user: bool = False) -> int:
    """③ 逾時:一小段時間沒人按 → AI 收回,改用 Mac 上的一般提示。"""
    bridge.log("=== 劇本③  逾時(沒人按 → timeout → 交還一般提示) ===")

    bridge.show_task("Deploy to staging", "Awaiting approval")
    time.sleep(1.0)

    if not auto_user:
        bridge.log(">>> 這個劇本請「不要按」,等它自己逾時(約 6 秒)<<<")
    # 刻意不排任何按鍵;短逾時方便觀察(正式用會是幾十秒)。
    decision = bridge.request_permission(
        "p3", "Deploy to staging", "Deploy now?", timeout=6)
    bridge.log(f"[MockAI] 權限結果:{decision}")
    if decision == "timeout":
        bridge.log("[MockAI] 沒等到人 → 收回這筆,改用 Mac 上的一般提示。")

    bridge.log("=== 劇本③ 完成 ===")
    return 0


def scenario_stopped(bridge: MacBridge, auto_user: bool = False) -> int:
    """④ 中斷:任務跑到一半被中斷(不是失敗)→ STOPPED + 長震,你按任一鍵知道了。"""
    auto = _make_auto(bridge, auto_user)
    bridge.log("=== 劇本④  中斷(STOPPED + 長震 → 任一鍵知道了) ===")

    bridge.show_task("Build iOS app", "Compiling...")
    time.sleep(1.2)

    bridge.notify_error("Build iOS app", "interrupted")
    if not auto_user:
        bridge.log(">>> 請按任一鍵(Yes/No/Open)表示知道了 <<<")
    auto("OPEN", "STOPPED", "e1")
    key = bridge.wait_ack(timeout=45)
    if key is not None:
        bridge.log(f"[MockAI] 收到 {key} → 知道了。")

    bridge.log("=== 劇本④ 完成 ===")
    return 0


def scenario_multitask(bridge: MacBridge, auto_user: bool = False) -> int:
    """⑤ 多任務排隊:兩筆權限請求,host 一次只推一件,你逐一批准。"""
    auto = _make_auto(bridge, auto_user)
    bridge.log("=== 劇本⑤  多任務排隊(佇列一次推一件,逐一批准) ===")

    pending = [
        ("q1", "Website Redesign", "Run 248 tests?"),
        ("q2", "API Service", "Restart server?"),
    ]
    total = len(pending)
    for i, (pid, task, question) in enumerate(pending, 1):
        bridge.log(f"[MockAI] 佇列剩 {total - i + 1} 件,推第 {i}/{total} 件")
        if not auto_user:
            bridge.log(f">>> 第 {i} 件:請按 Yes(D2)批准 <<<")
        auto("YES", "PERMISSION", pid)
        decision = bridge.request_permission(pid, task, question, timeout=45)
        bridge.log(f"[MockAI] 第 {i} 件結果:{decision}")
        if decision != "allow":
            bridge.log("[MockAI] 未批准,停止清佇列。")
            return 0
        time.sleep(0.6)

    bridge.notify_done("All tasks", "queue cleared")
    bridge.log("=== 劇本⑤ 完成 ===")
    return 0


def scenario_needyou(bridge: MacBridge, auto_user: bool = False) -> int:
    """⑥ 需要你:AI 卡住需要你,但不是 Yes/No 權限 → WAITING(NEED YOU)畫面 +
    震動,要你回 Mac 看一眼,按任一鍵表示你來了。"""
    auto = _make_auto(bridge, auto_user)
    bridge.log("=== 劇本⑥  需要你(WAITING/NEED YOU + 震動 → 回 Mac 看) ===")

    bridge.show_task("Refactor auth module", "Working...")
    time.sleep(1.0)

    bridge.notify_waiting("Refactor auth module")
    if not auto_user:
        bridge.log(">>> 這是叫你回 Mac 看細節,按任一鍵表示你來了 <<<")
    auto("VOICE", "WAITING", "n1")
    key = bridge.wait_ack(timeout=45)
    if key is not None:
        bridge.log(f"[MockAI] 收到 {key} → 你來了,細節在 Mac 上處理。")

    bridge.log("=== 劇本⑥ 完成 ===")
    return 0


def scenario_longrun(bridge: MacBridge, auto_user: bool = False) -> int:
    """⑦ 跑太久:任務跑很久 → WAITING(TIMEOUT 畫面)提醒你決定要不要繼續等。
    註:單色螢幕的 TIMEOUT 就是 WAITING,和⑥長得一樣,差別只在 task 文字。"""
    auto = _make_auto(bridge, auto_user)
    bridge.log("=== 劇本⑦  跑太久(TIMEOUT 畫面 = WAITING → 你決定) ===")

    bridge.show_task("Train model", "Running...")
    time.sleep(1.2)

    # task 文字帶出「太久」的語意(WAITING 只顯示 task,不顯示 line)。
    bridge.notify_waiting("Train model - 12 min", id="t1")
    if not auto_user:
        bridge.log(">>> 跑太久了,按任一鍵表示你知道了/回 Mac 決定 <<<")
    auto("OPEN", "WAITING", "t1")
    key = bridge.wait_ack(timeout=45)
    if key is not None:
        bridge.log(f"[MockAI] 收到 {key} → 你來決定要不要繼續等。")

    bridge.log("=== 劇本⑦ 完成 ===")
    return 0


# --- local-triggered scenarios (device button / voice starts them) ---------
# Unlike the AI-driven scenarios above, these begin with YOU pressing a button
# on the device; the host echoes the resulting screen (host stays source of
# truth). The 4 local screens map to §4.5: SESSIONS / LISTENING / PROCESSING /
# OPENING. These need the matching firmware screens in status_screen.c.

def scenario_sessions(bridge: MacBridge, auto_user: bool = False) -> int:
    """⑧ 任務清單:待機時按 Open → SESSIONS 瀏覽正在跑的任務。"""
    auto = _make_auto(bridge, auto_user)
    bridge.log("=== 劇本⑧  任務清單(按 Open → SESSIONS) ===")

    bridge._screen("READY", clock=time.strftime("%H:%M"))
    if not auto_user:
        bridge.log(">>> 請按 Open(D10)進任務清單 <<<")
    auto("OPEN", "READY")
    if bridge.wait_ack(keys=("OPEN",), timeout=45) != "OPEN":
        bridge.log("[MockAI] 沒進清單。")
        return 0

    bridge.log("[Bridge] → 裝置顯示 SESSIONS(任務清單)")
    bridge._screen("SESSIONS", task="3 tasks running")
    time.sleep(2.5)

    bridge.log("=== 劇本⑧ 完成 ===")
    return 0


def scenario_voice(bridge: MacBridge, auto_user: bool = False) -> int:
    """⑨⑩ 語音:按 Voice → LISTENING(聆聽)→ PROCESSING(理解)→ 回待機。"""
    auto = _make_auto(bridge, auto_user)
    bridge.log("=== 劇本⑨⑩  語音(Voice → LISTENING → PROCESSING) ===")

    bridge._screen("READY", clock=time.strftime("%H:%M"))
    if not auto_user:
        bridge.log(">>> 請按 Voice(D0)開始說話 <<<")
    auto("VOICE", "READY")
    if bridge.wait_ack(keys=("VOICE",), timeout=45) != "VOICE":
        bridge.log("[MockAI] 沒觸發語音。")
        return 0

    bridge.log("[Bridge] → 裝置顯示 LISTENING(聆聽中)")
    bridge._screen("LISTENING")
    time.sleep(2.0)  # 假裝在收音
    bridge.log("[Bridge] → 裝置顯示 PROCESSING(理解中)")
    bridge._screen("PROCESSING")
    time.sleep(2.0)  # 假裝在想
    bridge._screen("READY", clock=time.strftime("%H:%M"))
    bridge.log("[MockAI] 語音處理完,回待機(真 AI 這裡會執行語音指令)。")

    bridge.log("=== 劇本⑨⑩ 完成 ===")
    return 0


def scenario_opening(bridge: MacBridge, auto_user: bool = False) -> int:
    """⑪ 開啟結果:DONE 後按 Open → OPENING(開啟中)→ 開檔。"""
    auto = _make_auto(bridge, auto_user)
    bridge.log("=== 劇本⑪  開啟結果(按 Open → OPENING) ===")

    bridge.notify_done("Website Redesign", "12 tests passed")
    if not auto_user:
        bridge.log(">>> 請按 Open(D10)開啟結果 <<<")
    auto("OPEN", "DONE", "d1")
    if bridge.wait_ack(keys=("OPEN",), timeout=45) != "OPEN":
        bridge.log("[MockAI] 沒開啟。")
        return 0

    bridge.log("[Bridge] → 裝置顯示 OPENING(開啟中)")
    bridge._screen("OPENING", task="Website Redesign")
    time.sleep(1.5)
    bridge.open_result("Website Redesign")

    bridge.log("=== 劇本⑪ 完成 ===")
    return 0


# The full arranged set, in display order. Name -> (function, one-line 白話).
SCENARIOS = [
    ("happy",     scenario_happy,     "一切順利:allow → done → open"),
    ("deny",      scenario_deny,      "拒絕:按 No → AI 停手"),
    ("timeout",   scenario_timeout,   "逾時:沒人按 → 交還一般提示"),
    ("stopped",   scenario_stopped,   "中斷:STOPPED + 長震 → 知道了"),
    ("multitask", scenario_multitask, "多任務排隊:逐一批准"),
    ("needyou",   scenario_needyou,   "需要你:WAITING/NEED YOU → 回 Mac 看"),
    ("longrun",   scenario_longrun,   "跑太久:TIMEOUT 畫面(=WAITING)→ 你決定"),
    ("sessions",  scenario_sessions,  "任務清單:按 Open → SESSIONS"),
    ("voice",     scenario_voice,     "語音:Voice → LISTENING → PROCESSING"),
    ("opening",   scenario_opening,   "開啟結果:按 Open → OPENING"),
]
SCENARIO_MAP = {name: fn for name, fn, _ in SCENARIOS}

# Backward-compatible alias (the old happy-path entry point).
run_demo = scenario_happy


def run_all_scenarios(bridge: MacBridge, auto_user: bool = False,
                      gap: float = 2.0) -> int:
    """Run every arranged scenario in order, with a READY screen between each so
    the two runs don't blur together."""
    n = len(SCENARIOS)
    bridge.log(f"=== 全劇本連跑:共 {n} 個 ===")
    for i, (name, fn, desc) in enumerate(SCENARIOS, 1):
        bridge.log("")
        bridge.log(f"########## [{i}/{n}] {name} — {desc} ##########")
        fn(bridge, auto_user)
        bridge._screen("READY", clock=time.strftime("%H:%M"))  # 分隔:回待機
        time.sleep(gap)
    bridge.log("")
    bridge.log("=== 全劇本跑完 ===")
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
                    help="run the happy-path scenario (default action)")
    ap.add_argument("--scenario", choices=list(SCENARIO_MAP),
                    help="run one named scenario: " + ", ".join(SCENARIO_MAP))
    ap.add_argument("--all", action="store_true",
                    help="run every arranged scenario in order")
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
        if args.all:
            return run_all_scenarios(bridge, auto_user=args.loopback)
        if args.scenario:
            return SCENARIO_MAP[args.scenario](bridge, auto_user=args.loopback)
        return scenario_happy(bridge, auto_user=args.loopback)
    finally:
        transport.close()
        if logf:
            logf.close()


if __name__ == "__main__":
    raise SystemExit(main())
