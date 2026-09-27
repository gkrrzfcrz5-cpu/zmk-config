# Progress Log

Date format: YYYY-MM-DD. Newest phase last.

## Recorded development environment (Phase 0, 2026-09-27)

| Tool | Version | Notes |
|------|---------|-------|
| macOS | 26.6 (25G5028f) | Apple M2, arm64 |
| git | 2.50.1 (Apple Git-155) | global user: `lin_mickey <lin_mickey@apple.com>` |
| Python | 3.12.9 | |
| uv | 0.11.16 | |
| Homebrew | 7.0.6 | |
| node / npm | v26.5.0 / 11.17.0 | |
| Docker | 29.7.2 | daemon not running (not needed for cloud build) |
| gh | — | not installed |
| west/cmake/ninja/dtc/arm-gcc | — | not installed (cloud build strategy) |

Toolchain strategy: **GitHub Actions cloud build** — no local Zephyr SDK, per
project preference. Local machine only needs git (SSH) to push.

Network constraint: HTTPS to `github.com` is blocked by the sandbox; **git uses
SSH** (`git@github.com`, key `~/.ssh/id_ed25519`, authenticated as
`gkrrzfcrz5-cpu`). `api/codeload/raw.githubusercontent/zmk.dev` are reachable.

## Phase 0 — Inspect environment ✅ DONE (2026-09-27)
- Full environment inspected and recorded (see table above + `test-results.md`).
- Confirmed clean start: no pre-existing ZMK/Zephyr workspace to preserve.

## Phase 1 — Set up ZMK  ⏳ IN PROGRESS (2026-09-27)
Completed:
- Verified authoritative sources by cloning:
  - `zmkfirmware/unified-zmk-config-template` (pins **ZMK v0.3**).
  - `zmkfirmware/zmk` **@ v0.3.0** (commit `edf5c08`, Zephyr `v3.5.0+zmk-fixes`).
- Scaffolded `zmk-config` from the official template (verbatim template files +
  authored `build.yaml`, `README.md`, `docs/`).
- **Version-compat finding:** plan's `xiao_ble//zmk` (Zephyr 3.6+ syntax) does
  NOT exist on v0.3. Verified correct id is **`seeeduino_xiao_ble`**. Using it.

Blocked / needs user:
- Create the GitHub repo `github.com/gkrrzfcrz5-cpu/zmk-config` (empty, public),
  then first push over SSH to trigger the Actions build.

Next executable task:
- Push repo -> confirm GitHub Actions build succeeds -> download UF2 (Phase 1
  verification gate).

## Phase 2 — XIAO bring-up  ⏳ NOT STARTED
## Phase 3 — ai_companion shield  ⏳ NOT STARTED
## Phase 4 — Display + haptic  ⏳ NOT STARTED
## Phase 5 — MacBook communication  ⏳ NOT STARTED
