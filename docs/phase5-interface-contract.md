# AI Companion — 裝置⇄Mac 介面協定(Interface Contract)

**狀態:v0.2 HANDOFF DRAFT(2026-09-29)。** 這份文件是給 Mac / Optimus 端工程師的**對接規格**,也是裝置韌體端的實作依據。所有行為已與需求方逐條確認;JSON 欄位名 / 列舉值以本文件為準。

驗證層級:**DESIGN(尚未實作)**。以下欄位、列舉、時序為協定約定,尚未是已出貨的 API。

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

| `cue` | 對應事件 | 震法 |
|-------|----------|------|
| `block` | 要權限 / 卡住(擋進度) | 連兩下(嗒嗒) |
| `stopped` | 中斷 / 停了 | 一記長震(嗡———) |
| `done` | 做完 | 輕輕一下(嗒) |

- **重複震由 host 負責**:擋進度類(`block`)若使用者沒回,host 每約 **30 秒**再送一次 `haptic`,**最多再送 2~3 次就停**。`stopped` / `done` 只送一次。裝置只管「收到就震一次」。
- 強度:裝置端調到「拿在手上/放手邊感覺得到」即可(軟體參數,之後可調)。

### 4.4 `ping` — 存活檢查(可選)

```json
{"t":"ping","v":1}
```
裝置回 `pong`(§5.3)。

---

## 5. Device → Host 訊息

### 5.1 `input` — 使用者按了實體按鈕

```json
{"t":"input","v":1,"src":"button","key":"YES","screen":"PERMISSION","id":"perm-123"}
```

- `src` = `"button"`。
- `key` ∈ `VOICE` | `YES` | `NO` | `OPEN`(送的是**實體按下哪顆鈕**;意義由 host 依 `screen` 判讀)。
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

## 9. 給 host 端工程師的待確認事項

1. Optimus 實際開放哪些 hook 事件,能不能對應到:agent 開始跑 / 要權限 / 卡住等你 / 突然中斷 / 做完?(需求方主要用 Optimus。)
2. 權限的 `id`:Optimus 那邊有沒有一個能唯一標識「這一次權限請求」的識別碼,能塞進 `PERMISSION.id`、並在收到 `input` 時用它去批准正確的請求?
3. `PERMISSION.question` 想放多長?裝置畫面空間有限,建議一句、必要時截斷。
4. 「最近有動靜」怎麼判定(最後一次輸出的時間戳?),`TASK.line` 那一句話從哪來?
5. 語音:Voice 鈕送到 host 後,Optimus 這端要怎麼把它接到「開始/停止語音輸入」?

---

*相關文件:`docs/architecture.md`(系統總覽)、`docs/progress.md`(Phase 1–4 已完成的硬體/韌體驗證)、「12 Core Screen States」UI 目錄(mono 原型已於 2026-09-28 真機驗證,彩色版待硬體到貨)。*
