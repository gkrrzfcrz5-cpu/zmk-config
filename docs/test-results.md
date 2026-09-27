# Test Results & Verification Evidence

> Verification levels are **not** interchangeable:
> **CODE-REVIEW** < **BUILD (compiles)** < **FLASH (on device)** < **HW-VERIFIED (observed behavior)**.
> Each claim below records which level it reached and the evidence.

## Legend
- ✅ PASS — evidence recorded
- ⛔ NOT TESTED — not yet performed
- ❌ FAIL — evidence recorded

---

## Phase 0 — Environment inspection

| Check | Result | Evidence |
|-------|--------|----------|
| OS / arch | ✅ macOS 26.6 (25G5028f), Apple M2, arm64 | `uname -a`, `sw_vers` |
| git | ✅ 2.50.1 | `git --version` |
| Python / uv | ✅ 3.12.9 / uv 0.11.16 | `python3 --version`, `uv --version` |
| Homebrew / node | ✅ brew 7.0.6 / node v26.5 | `brew --version`, `node --version` |
| gh (GitHub CLI) | ⛔ not installed | `which gh` -> not found |
| west / cmake / ninja / dtc / arm-gcc | ⛔ not installed (cloud build used instead) | `which ...` |
| Existing ZMK/Zephyr workspaces | ✅ none found (clean start) | `find ~ ... -iname 'zmk*'` |
| GitHub SSH auth (personal) | ✅ `gkrrzfcrz5-cpu` authenticated | `ssh -T git@github.com` -> "Hi gkrrzfcrz5-cpu!" |
| Network: HTTPS to github.com | ❌ blocked/timeout (SSH used instead) | `curl https://github.com` -> timeout |
| Network: api/codeload/raw/zmk.dev | ✅ reachable | `curl` 200/301 |
| XIAO board connected | ⛔ not connected (not required this phase) | no USB serial / no UF2 volume |

---

## Phase 1 — ZMK config repository

| Item | Level reached | Evidence |
|------|---------------|----------|
| Config scaffolded from official template | ✅ CODE-REVIEW | files copied verbatim from `zmkfirmware/unified-zmk-config-template` |
| Board/shield identifiers verified against v0.3 source | ✅ CODE-REVIEW | `seeeduino_xiao_ble.zmk.yml`, `tester_xiao.zmk.yml` |
| GitHub repo `zmk-config` created | ⛔ NOT DONE | pending (see progress.md) |
| GitHub Actions build succeeds | ⛔ NOT TESTED | requires repo + push |
| UF2 artifact produced | ⛔ NOT TESTED | requires successful Actions run |

---

## Phase 2 — XIAO bring-up

| Item | Level reached | Evidence |
|------|---------------|----------|
| `seeeduino_xiao_ble` + `tester_xiao` compiles | ⛔ NOT TESTED | requires Actions run |
| UF2 flashed to board | ⛔ NOT TESTED | requires hardware |
| GPIO test (D0..D10 short-to-GND types `PIN n`) | ⛔ NOT TESTED | requires hardware |
| BLE pairing to Mac | ⛔ NOT TESTED | requires hardware |

---

_No results above are assumed. Rows move to ✅/❌ only when the evidence exists._
