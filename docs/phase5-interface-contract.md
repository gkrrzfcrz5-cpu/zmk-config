# AI Companion — 裝置⇄Mac 介面協定(Interface Contract)

**狀態:v0.3 HANDOFF(2026-09-29)。** 這份文件是給 Mac / 軟體團隊的**對接規格**,也是裝置韌體端的實作依據。所有行為已與需求方逐條確認;JSON 欄位名 / 列舉值以本文件為準。

驗證層級:**訊息協定 = DESIGN;裝置端(USB CDC + 6 畫面 + 4 按鈕 + 3 震動 + hello/pong)= 已實機驗證;雙向權限迴路 = 已在 USB 上實機驗證一次(2026-09-29)。BLE 傳輸 = 尚未實作(spike 進行中)。**

---

## 0. 合作分階段(與需求方 2026-09-29 對齊)

**原則:EE 不等 PCB、不等所有硬體功能到位,就先跟軟體團隊建立一條「可雙向通訊的路徑」。** 先打通路,再逐步加功能。

### Phase 1 — 最小可聯調版本(目前目標)

**架構:** `AI 軟體 ⇄ Mac Bridge ⇄ 裝置`。Mac Bridge 負責在「AI 軟體」與「裝置」之間轉換資料(持有連線、維護佇列、把 AI 事件翻成 `screen`/`haptic`、把 `input` 翻回 AI 動作)。**AI 整合的邏輯全在軟體團隊這端**,不依賴任何外部 agent hook(見 §10 已知限制)。

**首次聯調的傳輸 = USB CDC(已驗證),BLE 隨後追。** 訊息層(§3–§5)與傳輸無關,之後換 BLE 一行邏輯都不用改。

**Phase 1 通過判準(demo 這條迴路即算核心概念成立):**
> AI 軟體要求 Permission → 裝置震動 + 顯示 PERMISSION → 使用者按 Yes → AI 軟體收到回覆。

**EE 端 Phase 1 需達到的程度(對照現況):**

| 功能 | 需達到 | 現況 |
|---|---|---|
| MCU | 開發板能跑測試韌體 | ✅ 已達成 |
| Bluetooth | Mac 能連線、雙向傳測試訊息 | ⏳ BLE spike 中(Phase 1 首調先用 USB) |
| Button | 至少一顆送事件 | ✅ 四顆都送 `input` |
| Display | 顯示 Mac 傳來的狀態 | ✅ 6 種畫面 |
| Haptic | Mac 指令觸發震動 | ✅ 三種 cue |

Phase 1 **不需要**:語音、電池管理、正式 PCB。

### Phase 1 核心 API = 3 種訊息

**分工原則:Mac 端負責所有邏輯,硬體只負責顯示、提醒、回傳按鍵。** 裝置不用懂 AI 軟體、不跑 HTTP server、不知道 AI 在跑什麼工具。第一次連調只要下面**三種訊息**就能跑完整條權限迴路:

| # | 方向 | 訊息 | 用途 |
|---|---|---|---|
| ① | Mac → 裝置 | `screen` | 叫裝置顯示某一頁(如 PERMISSION) |
| ② | Mac → 裝置 | `haptic` | 叫裝置震一下提醒 |
| ③ | 裝置 → Mac | `input` | 回報使用者按了哪顆鈕(帶 `id`) |

```
① {"t":"screen","v":1,"state":"PERMISSION","id":"perm-123","task":"Website","question":"執行 248 個測試?"}
② {"t":"haptic","v":1,"cue":"block"}
③ {"t":"input","v":1,"src":"button","key":"YES","screen":"PERMISSION","id":"perm-123"}
```

Mac 收到 ③ 後,用 `id` 確認「這筆權限仍有效」再轉交 AI 軟體執行(`id` 存在的唯一理由 = 防止使用者按下去時畫面已換頁、批准到別件)。其餘訊息(`tasks` 瀏覽清單、`ping`/`pong` 存活檢查、`hello` 握手)是**選配雜務,Phase 1 可不理**。

### Phase 2 / Phase 3 — 待與需求方展開(TBD)

需求方規劃共三階段;Phase 2、3 內容待後續逐條確認後補入(預期方向:加入更多事件/佇列/多 session、切換到 BLE 無線、往正式 PCB 收斂)。

---

## 1. 角色與分工

裝置是一個**實體的「狀態顯示 + 按鈕」面板**,讓使用者不必一直盯著 Mac 螢幕看 AI agent 在幹嘛。裝置**不跑任何 AI、不保存任何權威狀態**。

| 一方 | 負責 |
|------|------|
| **Host(Mac / Optimus 端)** | 掌握所有 session 狀態;決定裝置該顯示哪一頁、何時震動;維護「需要你處理」的佇列;收到使用者的實體回應後**真的去執行**(批准/拒絕/推下一件)。**所有邏輯在這端。** |
| **Device(XIAO nRF52840)** | 收到指令就把那頁畫出來、該震就震;使用者按鈕就回報一行訊息。**薄薄的客戶端**,只反映 host 最後叫它顯示的東西。 |

**設計原則:host 是唯一事實來源(source of truth)。** 佇列、排序、重複震動的計時,全都在 host。裝置越笨越好,才不用一直重燒韌體。

### 工作分工

| 項目 | Device(韌體端) | Host(Optimus 端) |
|---|---|---|
| USB 序列通道 | 開 CDC、一行一行收發 JSON | 打開 `/dev/tty.usbmodem*`、一行一行讀寫 JSON |
| 偵測 agent 狀態 | ❌ | ✅ 用 Optimus hook 抓 |
| 「需要你」佇列 + 排序 | ❌ | ✅ 擋進度的先、一個一個彈 |
| 決定顯示哪頁 + 內容 | ❌ | ✅ |
| 畫面渲染 | ✅ | ❌ |
| 按鈕回報 | ✅ 送 `input`(綁 `id`) | ✅ 收到後執行 |
| 震動 | ✅ 收 `haptic` cue → 翻成馬達效果 | ✅ 決定何時送、重複震的計時 |
| 旋鈕捲 Mac | ✅ 原生 HID(不走本協定) | ❌ |

---

## 2. 傳輸層(Transport)

- **v1:USB CDC-ACM 序列埠。** Mac 端開 `/dev/tty.usbmodem*`,當成一條雙向文字串流。鮑率(baud)對 USB CDC 而言僅為形式,約定填 `115200`、8N1。
- **之後:BLE。** 訊息層(§3–§5)**與傳輸無關**,同一套訊息之後原封搬到 BLE GATT,host / device 的應用邏輯不用改。
- 旋鈕「捲 Mac 視窗」走的是**原生 USB HID**(裝置本來就是 HID 鍵盤/滑鼠),**不經過本協定通道**。

---

## 3. 訊息框架與編碼

- **編碼:UTF-8 JSON,一行一個訊息**(以 `\n` 分隔,即 JSON-lines)。
- **每個訊息**都是一個 JSON 物件,必含:
  - `t` — 訊息類型(字串,必填),見 §4 / §5。
  - `v` — 協定版本(整數,必填),本文件 = **`1`**。
  - 其餘為該類型專屬欄位。
- **收到不認識的 `t`、不認識的欄位:一律忽略**(向前相容)。收到不認識的畫面 `state`:裝置顯示待機畫面(§4.1 fallback)。

---

## 4. Host → Device 訊息

### 4.1 `screen` — 設定目前顯示的畫面

```json
{"t":"screen","v":1,"state":"PERMISSION","id":"perm-123","task":"Website Redesign","question":"執行測試?"}
```

`state` 列舉與各自的資料欄位(所有畫面走「大狀態 + 一句話」的精簡風格):

| `state` | 欄位 | 螢幕呈現 | 說明 |
|---------|------|----------|------|
| `TASK` | `task`(str)、`line`(str) | 「{task} — {line}」 | **主要任務**(平常有任務在跑、沒事需要你時)。host 挑「最近有動靜」的那個;見 §6 停留規則。 |
| `READY` | `clock`(str "HH:MM") | 時鐘 + 「準備好了」 | 完全沒任務在跑時的待機畫面。 |
| `PERMISSION` | `id`(str)、`task`(str)、`question`(str) | {task} + 問題 + `✕拒絕 / ✓批准` | 要你批准權限。Yes/No 直接答。 |
| `WAITING` | `id`(str)、`task`(str) | 「{task} 在等你回覆」 | 卡住問你(agent 等你打字回)。**不放問題內容**,細節回 Mac。 |
| `STOPPED` | `id`(str)、`task`(str) | 「{task} 停了」 | 任務突然中斷/停住(注意:是「中斷」,不是 build 失敗)。 |
| `DONE` | `id`(str)、`task`(str) | 「{task} 做完了」 | 任務完成。 |

- **`id` 是關鍵**:凡是「需要你處理」的畫面(`PERMISSION` / `WAITING` / `STOPPED` / `DONE`)都必須帶 `id`。使用者的回應會把這個 `id` 原樣帶回(§5.1),host 才能把「他按的」綁到「正確的那一件」——避免使用者按下去時畫面已經換頁、結果批准到別件。
- 裝置收到就**立刻**顯示、並**保持**到下一個 `screen` 訊息(生產環境不自動輪播)。
- 缺欄位:裝置以空白 / 佔位字代替。

### 4.2 `tasks` — 目前在跑的任務清單(給「瀏覽模式」用)

```json
{"t":"tasks","v":1,"list":[{"id":"t1","name":"Website Redesign","line":"建置中"},{"id":"t2","name":"Test Suite","line":"跑測試中"}]}
```

- 當「在跑的任務集合」有變動時,host 送一次最新清單;裝置快取起來。
- 使用者在待機/主要任務畫面按 **Open** 進入**瀏覽模式**後,轉旋鈕就在這份快取清單裡翻(**純裝置端行為,不回報 host**)。
- v1 可選:若 host 暫不實作,瀏覽模式就只有目前這一個任務可看。

### 4.3 `haptic` — 觸發一次震動

```json
{"t":"haptic","v":1,"cue":"block"}
```

`cue` 為**語意名稱**,裝置自己翻成 DRV2605L 的馬達效果(**host 不碰效果編號**):

| `cue` | 對應事件 | 震法(2026-09-29 實機定案) |
|-------|----------|------|
| `done` | 做完 | 一記短強震(嗡),數得出「一下」 |
| `block` | 要權限 / 卡住(擋進度) | 兩記短強震、中間停 200ms(嗡—嗡),數得出「兩下」 |
| `stopped` | 中斷 / 停了 | 一記長震(嗡———,明顯比 done 那下長) |

- **三種 cue 靠「下數 + 長短」區分**,使用者已閉眼實測可分辨。裝置端對應的 DRV2605L ROM 效果由韌體鎖定(`done`/`block` = Strong Buzz、`stopped` = 1000ms Alert);host **只送語意 cue,不碰效果編號**。早期試過 click 類效果,在這顆 ERM 馬達上太輕(太短、轉子還沒轉起來),已棄用。
- **重複震由 host 負責**:擋進度類(`block`)若使用者沒回,host 每約 **30 秒**再送一次 `haptic`,**最多再送 2~3 次就停**。`stopped` / `done` 只送一次。裝置只管「收到就震一次」。

### 4.4 `ping` — 存活檢查(可選)

```json
{"t":"ping","v":1}
```
裝置回 `pong`(§5.3)。

### 4.5 「12 視覺畫面」↔ 6 協定 state 對映

設計稿有一份完整的 **12 Core Screen States**(視覺全集,含吉祥物表情/彩色卡片);協定把它收斂成上表的 **6 個 `state`**。對映如下,好讓軟體團隊知道每個視覺畫面實際上要送哪種 `screen`:

| 視覺畫面 | 送的協定 `state` | 說明 |
|---|---|---|
| HOME(首頁/總覽) | `READY` | 沒任務在跑的待機;統計數字由 host 決定要不要顯示 |
| SESSIONS(任務清單) | *(不送 screen)* | 走 `tasks` 訊息,裝置端瀏覽翻頁 |
| WORKING(執行中) | `TASK` | 進度/百分比塞進 `line` |
| NEED YOU(需要你) | `WAITING` | 通用求助;細節回 Mac |
| PERMISSIONS(權限佇列) | `PERMISSION` | 佇列在 host 端,裝置一次只顯示一件(§6) |
| PERMISSION(權限細節) | `PERMISSION` | 直接對應,Yes/No |
| DONE(完成) | `DONE` | 直接對應 |
| ATTENTION(出錯/build 失敗) | `STOPPED` ⚠️ | 見 §9 待確認:`STOPPED` 原意是「中斷」非 build 失敗,語意待對齊 |
| TIMEOUT(跑太久) | `WAITING` | 「跑很久了,要繼續嗎」= 等你決定 |
| LISTENING(語音聆聽) | *(不送 screen)* | Voice 鈕本地觸發,語音 UI,v1 不含 |
| PROCESSING(語音理解) | *(不送 screen)* | 同上 |
| OPENING(開啟結果) | *(不送 screen)* | Open 鈕本地行為,裝置端瀏覽 |

> 有 4 個畫面**不歸 host 管**(SESSIONS/OPENING 是裝置本地瀏覽、LISTENING/PROCESSING 是語音),對方**不用**為它們送 `screen`,以免誤以為漏做。

---

## 5. Device → Host 訊息

### 5.1 `input` — 使用者按了實體按鈕

```json
{"t":"input","v":1,"src":"button","key":"YES","screen":"PERMISSION","id":"perm-123"}
```

- `src` = `"button"`。
- `key` ∈ `VOICE` | `YES` | `NO` | `OPEN` | `ENC`(送的是**實體按下哪顆鈕**;意義由 host 依 `screen` 判讀)。`ENC` = 旋鈕的按壓(第 5 顆鍵,彩色版才有);定義為**瀏覽模式的「選取/確認不同 session」**——轉旋鈕在 session 間上下選、按旋鈕(`ENC`)確認選中那個。非瀏覽畫面 host 可忽略 `ENC`。
- `screen` = 按下當下裝置正顯示的 `state`。
- `id` = 當下畫面的 `id`(若有),把回應綁到正確那一件。

**host 依 `screen` 判讀按鈕(對應行為表)**:

| 當下 `screen` | 收到 `key` | host 該做什麼 |
|---|---|---|
| `PERMISSION` | `YES` | 批准 → 執行那個動作 → 推佇列下一件 |
| `PERMISSION` | `NO` | 拒絕 → 取消 → 推下一件 |
| `PERMISSION` | `VOICE` / `OPEN` | **忽略**(權限一定要 Yes/No,避免誤消) |
| `WAITING` / `STOPPED` / `DONE` | **任何一顆** | 當「知道了」:把這件移出佇列 → 推下一件 |
| `TASK` / `READY` | `VOICE` | 觸發 Optimus 語音輸入(開始/停止講話) |
| `TASK` / `READY` | `OPEN` | 進/出**瀏覽模式**——**純裝置端**,host 通常收不到(裝置自己處理)。host 可忽略。 |
| `TASK` / `READY` | `YES` / `NO` | 忽略(沒有待處理的事) |
| **瀏覽模式(SESSIONS 等)** | `ENC` | **選取/確認不同 session**(轉旋鈕在 session 間選、按旋鈕確認);純裝置端可自理,host 通常收不到 |

> 註:`OPEN` 在待機/主要任務畫面是裝置本地的瀏覽模式開關,裝置**不一定**會送 `input`;上表列出僅為完整性。旋鈕轉動一律不經本協定(捲 Mac = 原生 HID;瀏覽模式翻頁 = 裝置本地)。

### 5.2 `hello` — 連線 / 重連時送

```json
{"t":"hello","v":1,"fw":"0.1.0","proto":1,"caps":["screen","haptic","tasks","input"]}
```
讓 host 知道裝置的韌體版本 / 協定版本 / 能力,並在重連後**重推目前該顯示的畫面**。

### 5.3 `pong` — 回應 `ping`

```json
{"t":"pong","v":1}
```

---

## 6. 佇列與行為規則(host 端責任)

1. **四種「需要你」事件**進佇列並各自送對應 `haptic`:要權限(`PERMISSION`/`block`)、卡住(`WAITING`/`block`)、中斷(`STOPPED`/`stopped`)、做完(`DONE`/`done`)。
2. **一次只推一件**:使用者處理完(收到對應 `input`)後,host 才推下一件。
3. **排序:擋進度的先**(`PERMISSION`、`WAITING`),通知類(`DONE`、`STOPPED`)排後面。
4. **佇列空了** → 回到平常畫面:有任務在跑就推 `TASK`(挑「最近有動靜」的那個),完全沒任務就推 `READY`。
5. **主要任務防閃**:`TASK` 切換後**至少停 10 秒**才准再換(host 端節流),避免多個任務同時輸出時畫面一直跳。
6. **重複震**:`block` 未獲回應則每約 30 秒重送,最多 2~3 次。

---

## 7. 連線生命週期

1. USB 列舉成功。
2. 裝置送 `hello`。
3. host 收到 `hello` → **推目前該顯示的畫面**(裝置在收到前顯示本地待機畫面)。
4. 穩態:host 依 session 狀態推 `screen` / `tasks` / `haptic`;裝置依使用者操作送 `input`。
5. **未連線**:USB 拔掉 / Optimus 沒開 / 一段時間沒收到 host 訊息 → 裝置**回到時鐘待機畫面**(本地行為,不需 host)。插回去 / host 重連 → 回到步驟 2。

---

## 8. v1 範圍

**v1 要做:**
- USB CDC 雙向。
- host→device:`screen`(6 種 state)、`haptic`(3 種 cue)、`tasks`(可選)、`ping`。
- device→host:`input`(4 顆按鈕)、`hello`、`pong`。
- 權限 Yes/No **真的**在 Optimus 生效(雙向)。
- Voice 鈕觸發 Optimus 語音輸入。
- 佇列一個一個彈、擋進度先、重複震上限、主要任務防閃。

**v1 先不做 / 之後:**
- BLE 傳輸(訊息層不變,直接搬)。
- Open 鈕的「跳到 Mac 看細節」(v1 Open 只當瀏覽模式開關 / 通知畫面的知道了)。
- `WAITING` 顯示問題內容、在裝置上直接回答。
- `config`(亮度、待機等)。

---

## 9. 給 host / 軟體團隊的待確認事項

1. ~~Optimus 開放哪些 hook 對應到 要權限/卡住/中斷/做完?~~ **已實測(2026-09-29,見 §10):此版 Optimus 的 `Notification` 與 `PermissionRequest` hook 皆不觸發;只有 `Stop`/`TurnEnd`(做完)可靠、`PreToolUse`(每個工具都觸發、決策前)。若「AI 軟體」是軟體團隊自家產品,請在自家層接權限請求,不要依賴外部 hook。**
2. 權限的 `id`:AI 軟體端有沒有一個能唯一標識「這一次權限請求」的識別碼,能塞進 `PERMISSION.id`、並在收到 `input` 時用它去批准正確的請求?
3. `PERMISSION.question` 想放多長?裝置畫面空間有限,建議一句、必要時截斷。
4. 「最近有動靜」怎麼判定(最後一次輸出的時間戳?),`TASK.line` 那一句話從哪來?
5. 語音:Voice 鈕送到 host 後,要怎麼把它接到 AI 軟體的「開始/停止語音輸入」?
6. **ATTENTION(build 失敗/程式錯誤)語意缺口:** 目前 6 個 `state` 沒有專屬的「錯誤」狀態,設計稿的 ATTENTION 只能勉強塞進 `STOPPED`(原意是「中斷」)。要嘛連調時決定「build 失敗也算 STOPPED」,要嘛之後給 6-state 加一個 `ERROR`。請對方確認偏好。

---

## 10. 已知限制與環境事實(接手前必讀)

1. 🔴 **權限/停頓時刻沒有可靠的外部 hook。** 實測此版 Optimus:`Notification` 與 `PermissionRequest` 兩個 hook 即使畫面上明明跳出權限框,也**完全不觸發**(2026-09-29 以「偷看記錄器」雙保險確認:同一次執行 `PreToolUse` 記到 3 筆、`Notification` 0 筆)。可靠的只有 `Stop`/`TurnEnd`(回合結束=做完)與 `PreToolUse`(每個工具呼叫、在權限決策**之前**、且無「這次會不會問」的提示)。→ **要權限/卡住/中斷這三種事件,Mac Bridge/軟體團隊需從 AI 軟體「內部」取得,別依賴這些外部 hook。**
2. 🔴 **序列埠一次只能一個程式持有。** 第二個開啟者會搶走按鍵位元組,導致收不到 `input`。→ **Mac Bridge 必須是唯一、長駐、獨佔 port 的程式**;不可用「開一次關一次」的一次性寫法與 daemon 並存。
3. **DTR 握手:** 打開 port 會被裝置視為「host 來了」→ 送 `hello` 並帶起 RX/haptic;關閉 port → 裝置回本地待機。Bridge 要嘛保持連線常開,要嘛接受每次開關都要重推當前畫面(§7)。開啟後**約 0.4s 內先別送訊息**,否則第一則可能被吞(裝置去抖+起 RX 的空窗)。
4. **Auto 自動核准模式:** 需求方的 Optimus 開了 Auto(自動核准工作階段的一般要求),平常很少真的停下來問——這也是「要權限」事件在 Optimus 上既難抓又少見的原因。軟體團隊自家產品若要展示權限迴路,需能主動發出權限請求。
5. **`async` hook:** 此版 Optimus 支援 hook 設 `"async": true`(背景執行、不卡回合)——「做完」通知即用此方式,已驗證。

---

## 11. 最小往返驗收測試(Phase 1 第一步)

打通以下往返,即代表傳輸層 + 雙向路徑成立,可正式開始聯調:

1. Bridge 打開 `/dev/tty.usbmodem*`,等到裝置的 `hello`。
2. Bridge 送一行 `{"t":"screen","v":1,"state":"PERMISSION","id":"t1","task":"Demo","question":"執行測試?"}`。
3. 裝置顯示 PERMISSION 畫面;Bridge 送 `{"t":"haptic","v":1,"cue":"block"}` → 裝置震「嗡—嗡」。
4. 使用者按 **Yes(D2)** → 裝置送 `{"t":"input","v":1,"src":"button","key":"YES","screen":"PERMISSION","id":"t1"}`。
5. Bridge 收到、比對 `id`,回饋給 AI 軟體 → demo「AI 收到 Yes」。

> `tools/aic.py`(裝置端 host API + CLI)已實作 §2–§5 的 USB 收發,可直接當 Bridge 的起點或參考:`AICDevice` 類別有 `open/close/screen/permission/buzz/wait_for_button` 等;CLI `gate` 子命令就是「往返步驟 2–5」的一個現成實作。

---

*相關文件:`docs/architecture.md`(系統總覽)、`docs/progress.md`(Phase 1–4 已完成的硬體/韌體驗證)、「12 Core Screen States」UI 目錄(mono 原型已於 2026-09-28 真機驗證,彩色版待硬體到貨)。*
