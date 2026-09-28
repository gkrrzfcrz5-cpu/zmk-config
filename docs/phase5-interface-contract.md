# AI Companion — Device ⇄ Host Interface Contract

**Status: DRAFT v0.1 — for review by the Mac/AI-side team. Nothing here is
implemented yet.** This document proposes the wire protocol between the physical
AI Companion device and the host application (the Mac process that runs / manages
the AI coding sessions). It is written to be shared with the host-side team and
to drive the open decisions listed at the end.

Verification level of this document: **DESIGN PROPOSAL** (not CODE-REVIEW /
BUILD / FLASH / HW-VERIFIED). Field names, enums, and framing below are
proposals, not shipped APIs.

---

## 1. Purpose & roles

The device is a **physical control + status surface** so the user does not have
to watch the Mac screen while AI agents run. It never runs AI models.

| Party | Responsibility |
|-------|----------------|
| **Host** (Mac app / agent bridge) | Owns all session state. Decides what the device should show. Pushes screen updates + haptic cues. Receives the user's physical responses and acts on them. |
| **Device** (XIAO nRF52840) | Renders the current screen. Fires haptics on request. Reports button/encoder input. Holds **no** authoritative state — it is a thin client that shows what the host last sent. |

Design rule: **the host is the source of truth.** The device only mirrors the
last screen it was told to show and forwards raw input. This keeps the device
firmware simple and lets the host evolve without reflashing.

## 2. Transport

The message layer (§3–§5) is **transport-agnostic**: the same newline-delimited
messages ride over whichever link we choose. Two candidates:

| Option | Pros | Cons | Verdict |
|--------|------|------|---------|
| **BLE GATT custom service** (recommended target) | Wireless (matches the "companion" use); device already pairs to the Mac over BLE today; GATT naturally models "host writes / device notifies". | ZMK does not expose easy custom GATT — likely needs the display/data path moved to custom Zephyr code (see [[architecture]] open question). Small MTU (~23–244 B) ⇒ keep messages small / support fragmentation. | **Target for the shipped product.** |
| **USB CDC-ACM (serial)** (recommended for bring-up) | Dead simple bidirectional stream; trivial to drive from any host language (`/dev/tty.usbmodem*`); no pairing. | Needs a cable; still needs custom Zephyr CDC handling. | **Use first to prototype the protocol**, then port the identical message layer to BLE. |

Recommendation: **prototype the protocol over USB CDC, ship over BLE GATT.**
Because the message layer is identical, the transport can be swapped without
changing host or device application logic.

### 2.1 BLE GATT sketch (if/when we go BLE)

One custom primary service with three characteristics:

| Characteristic | Properties | Direction | Carries |
|----------------|-----------|-----------|---------|
| `rx` (host→device) | Write / Write-No-Response | Host → Device | `screen`, `haptic`, `config`, `ping` messages |
| `tx` (device→host) | Notify | Device → Host | `input`, `hello`, `pong` messages |
| `info` | Read | — | Static: firmware version, protocol version, capabilities |

UUIDs: **TBD** (allocate a private 128-bit base). Fragmentation: if a message
exceeds the negotiated MTU, split on a byte boundary and reassemble at the
newline delimiter (§3).

## 3. Framing & encoding

- **Encoding:** UTF-8 **JSON, one message per line** (newline `\n` delimited,
  a.k.a. JSON-lines). Chosen for readability during bring-up.
- **Why JSON now:** screens are tiny (a few short strings), so size is not a
  concern at this stage. If BLE throughput ever bites, we can swap to CBOR with
  the same field names — deferred until measured.
- **Every message** is a JSON object with:
  - `t` — message type (string, required). One of §4/§5.
  - `v` — protocol version (integer, required). This document = **`1`**.
  - type-specific fields.
- **Unknown `t` or unknown fields:** the receiver **must ignore** them (forward
  compatibility). Unknown enum values (e.g. a new screen state) → device shows a
  safe fallback (§4.1, `UNKNOWN`).

## 4. Host → Device messages

### 4.1 `screen` — set the current screen

```json
{"t":"screen","v":1,"state":"WORKING","data":{ ... }}
```

`state` is one of the 12 core states (matches the UI catalog). `data` fields are
per-state and all **optional** (device substitutes blanks/placeholders if
missing). Proposed fields:

| `state` | `data` fields | Meaning |
|---------|---------------|---------|
| `HOME` | `running` (int), `need_you` (int), `done` (int), `clock` (str "HH:MM") | Dashboard counts |
| `SESSIONS` | `rows`: array of `{name, status}` where status ∈ `RUNNING`/`IDLE`/`NEED`/`DONE` | Session list |
| `WORKING` | `name` (str), `activity` (str, e.g. "BUILDING..."), `pct` (int 0–100), `elapsed` (str), `hint` (str) | Active session progress |
| `NEED_YOU` | `title` (str), `detail` (str), `count` (int) | Attention required |
| `PERMISSIONS` | `rows`: array of `{label, age}` | Permission queue |
| `PERMISSION` | `id` (str), `question` (str), `detail` (str) | Single decision; buttons YES/NO answer it |
| `DONE` | `title` (str), `detail` (str), `footer` (str) | Success |
| `ATTENTION` | `title` (str), `detail` (str) | Error / failure |
| `TIMEOUT` | `title` (str), `detail` (str), `elapsed` (str) | Long-running warning |
| `LISTENING` | — | Voice capture active |
| `PROCESSING` | `detail` (str) | Interpreting request |
| `OPENING` | `title` (str), `detail` (str) | Opening a result |
| `UNKNOWN` | — | Fallback if the device gets a state it doesn't recognise |

The device renders the given state immediately and holds it until the next
`screen` message (no auto-cycle in production — the 5s auto-cycle is a
prototype-only demo with no host attached).

### 4.2 `haptic` — fire a haptic cue

```json
{"t":"haptic","v":1,"effect":"need_you"}
```

`effect` ∈ (proposed, maps to DRV2605L effect IDs — see [[haptic-plan]]):
`need_you` | `permission` | `done` | `error` | `timeout` | `tick`. The device
maps each to a concrete effect; the host does not need to know effect numbers.

### 4.3 `config` — runtime settings (optional, v1 minimal)

```json
{"t":"config","v":1,"blank_on_idle":false,"brightness":100}
```

All fields optional. Deferred: most config can wait past v1.

### 4.4 `ping` — liveness

```json
{"t":"ping","v":1}
```
Device replies with `pong` (§5.3).

## 5. Device → Host messages

### 5.1 `input` — a physical control was used

```json
{"t":"input","v":1,"src":"button","key":"YES","screen":"PERMISSION","ctx":"perm-123"}
```

- `src` ∈ `button` | `encoder`.
- For `button`: `key` ∈ `VOICE` | `YES` | `NO` | `OPEN`.
- For `encoder`: `delta` (signed int, detents; + = clockwise) instead of `key`.
- `screen` — the state the device was showing when the input happened (so the
  host can disambiguate what the press *meant*).
- `ctx` — echoes the `id` of the current screen if it had one (e.g. the
  `PERMISSION.id`), so a YES/NO is bound to the exact decision it answered.

**Button semantics are host-interpreted**, but the intended mapping is:

| Button | On `PERMISSION` | On `LISTENING`/general | On a result |
|--------|-----------------|------------------------|-------------|
| `VOICE` | — | start/stop voice capture | — |
| `YES` | allow the decision | confirm | — |
| `NO` | deny the decision | dismiss | — |
| `OPEN` | — | — | open the result on the Mac |

Encoder: scroll / move selection in list screens (host decides).

### 5.2 `hello` — sent on connect / reconnect

```json
{"t":"hello","v":1,"fw":"0.1.0","proto":1,"caps":["screen","haptic","encoder"]}
```
Lets the host learn the device's firmware + protocol version + capabilities and
re-push the current screen after a reconnect.

### 5.3 `pong` — reply to `ping`

```json
{"t":"pong","v":1}
```

## 6. Connection lifecycle

1. Link comes up (BLE connect or USB enumerate).
2. Device sends `hello`.
3. Host reads `info` (BLE) / notes `hello` and **pushes the current `screen`**
   (device starts blank/`UNKNOWN` until told what to show).
4. Steady state: host pushes `screen`/`haptic` as session state changes; device
   pushes `input` as the user acts.
5. **Liveness:** host may `ping` every N seconds; if the device stops responding,
   the host treats the device as disconnected. If the *device* sees no host
   traffic for a timeout, it shows a neutral "disconnected" screen (proposed:
   reuse `UNKNOWN` or a dedicated state — **open question §8**).
6. On reconnect, go to step 2.

## 7. Versioning

- `v` / `proto` integer bumps on any breaking change.
- Receivers ignore unknown fields and unknown message types → additive changes
  are non-breaking.
- Host and device advertise their `proto` in `hello`/`info`; the host should
  degrade gracefully if the device speaks an older version.

## 8. Open questions / decisions needed from the host team

1. **Transport for the shipped product:** BLE GATT (wireless, harder in ZMK) vs
   USB CDC (simple, tethered)? Recommendation: prototype CDC → ship BLE.
2. **Firmware architecture:** driving arbitrary host data into a display is not
   something ZMK does natively. Do we (a) extend ZMK with a custom GATT/CDC data
   path, or (b) move the display+comms to a plain Zephyr app and keep ZMK only
   for HID input? This decision does **not** change this wire contract.
3. **Host stack:** what runs on the Mac — a standalone app, or a bridge inside
   the existing agent tooling? Affects who owns session→screen mapping.
4. **Disconnected UX:** what should the device show when the host is silent?
5. **Security/pairing (BLE):** is just-works pairing acceptable, or do we need
   bonding/encryption for the data channel?
6. **Multiple sessions:** `SESSIONS`/`PERMISSIONS` lists — max rows the device
   should render, and how the host picks which to show.
7. **Field freeze:** confirm the per-state `data` fields in §4.1 match what the
   host can actually produce, then freeze v1.

---

*Cross-refs: [[architecture]] (system overview + the HID-vs-GATT open question),
[[haptic-plan]] (DRV2605L effects), the "12 Core Screen States" UI catalog
(mono prototype HW-verified 2026-09-28).*
