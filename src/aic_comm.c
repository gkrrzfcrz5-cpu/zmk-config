/*
 * Copyright (c) 2026 The AI Companion Contributors
 * SPDX-License-Identifier: MIT
 *
 * Phase 5 host-communication module: the device side of the USB CDC-ACM data
 * channel (docs/phase5-interface-contract.md).
 *
 * Data flow, and why it is split across three contexts:
 *   1. UART RX ISR (aic_uart_isr): drains the CDC FIFO byte-by-byte into a line
 *      buffer. On a newline it hands the completed line to a k_msgq and pokes a
 *      work item. ISR does the minimum — no parsing, no LVGL, no I2C.
 *   2. Display work queue (aic_process_work): drains the msgq, parses each JSON
 *      line, and dispatches. LVGL is single-threaded and is serviced on THIS
 *      queue (zmk_display_work_q(), app/src/display/main.c), so updating the
 *      screen from here is race-free — that is the whole reason we marshal to it
 *      instead of touching LVGL from the ISR.
 *   3. A low-priority thread (aic_comm_main): one-time UART setup, then watches
 *      the CDC DTR line to send `hello` when the host opens the port and to
 *      restore the local standby screen when it closes/unplugs.
 *
 * The framing + message set is a minimal, controlled subset of JSON-lines, so we
 * parse with a small depth-aware top-level field extractor rather than pulling
 * in a full JSON library: every message is one line, one flat object, UTF-8.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <zmk/display.h>

#include "aic_screen.h"
#include "aic_haptic.h"
#include "aic_input.h"

LOG_MODULE_REGISTER(aic_comm, CONFIG_ZMK_LOG_LEVEL);

#define AIC_COMM_UART_NODE DT_CHOSEN(aic_comm_uart)
static const struct device *const uart_dev = DEVICE_DT_GET(AIC_COMM_UART_NODE);

/* One received line. AIC_LINE_MAX leaves room for a trailing NUL. */
#define AIC_LINE_MAX 256
struct aic_line {
    char buf[AIC_LINE_MAX];
    uint16_t len;
};

/* ISR -> display-queue hand-off. Depth 4 is plenty; the host sends at human
 * cadence, and each line is processed within a display tick. */
K_MSGQ_DEFINE(s_rx_q, sizeof(struct aic_line), 4, 4);

/* --- tiny top-level JSON field extractor ----------------------------------- */

/*
 * Return a pointer to the first char of the VALUE of top-level object key `key`
 * in NUL-terminated `s`, or NULL. Depth- and string-aware: only keys at depth 1
 * (directly inside the outer object) match, so a key with the same name nested
 * inside e.g. "list":[{...}] is never picked up, and a string VALUE equal to the
 * key name is not mistaken for the key (a key is the string immediately followed
 * by ':').
 */
static const char *json_value(const char *s, const char *key)
{
    size_t keylen = strlen(key);
    int depth = 0;
    bool in_str = false;
    bool esc = false;
    const char *tok = NULL; /* start of the current string token's content */

    for (const char *p = s; *p; p++) {
        char c = *p;
        if (in_str) {
            if (esc) {
                esc = false;
            } else if (c == '\\') {
                esc = true;
            } else if (c == '"') {
                in_str = false;
                if (depth == 1 && (size_t)(p - tok) == keylen &&
                    strncmp(tok, key, keylen) == 0) {
                    const char *q = p + 1;
                    while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') {
                        q++;
                    }
                    if (*q == ':') {
                        q++;
                        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') {
                            q++;
                        }
                        return q;
                    }
                }
            }
            continue;
        }
        switch (c) {
        case '"':
            in_str = true;
            tok = p + 1;
            break;
        case '{':
        case '[':
            depth++;
            break;
        case '}':
        case ']':
            depth--;
            break;
        default:
            break;
        }
    }
    return NULL;
}

/* Copy the string value of top-level `key` into out[out_sz] (always
 * NUL-terminated). Returns true if the key was found with a string value.
 * Handles the JSON escapes we emit; raw UTF-8 bytes pass through unchanged. */
static bool json_str(const char *s, const char *key, char *out, size_t out_sz)
{
    const char *v = json_value(s, key);
    if (v == NULL || *v != '"') {
        if (out_sz) {
            out[0] = '\0';
        }
        return false;
    }
    v++; /* past opening quote */
    size_t i = 0;
    bool esc = false;
    while (*v && i + 1 < out_sz) {
        char c = *v++;
        if (esc) {
            switch (c) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            /* \", \\, \/ and any other: keep the literal char */
            default: break;
            }
            out[i++] = c;
            esc = false;
        } else if (c == '\\') {
            esc = true;
        } else if (c == '"') {
            break;
        } else {
            out[i++] = c;
        }
    }
    out[i] = '\0';
    return true;
}

/* Parse the integer value of top-level `key` into *out. Returns true on a
 * plain (optionally negative) integer value; ignores fractions. */
static bool json_int(const char *s, const char *key, long *out)
{
    const char *v = json_value(s, key);
    if (v == NULL) {
        return false;
    }
    bool neg = false;
    if (*v == '-') {
        neg = true;
        v++;
    }
    if (*v < '0' || *v > '9') {
        return false;
    }
    long n = 0;
    while (*v >= '0' && *v <= '9') {
        n = n * 10 + (*v - '0');
        v++;
    }
    *out = neg ? -n : n;
    return true;
}

/* --- transmit -------------------------------------------------------------- */

/* Serialise transmits: hello (comm thread), pong (display queue) and input
 * (behavior thread) can each call aic_send_line from a different context; the
 * mutex keeps every line's bytes contiguous on the wire. All callers are thread/
 * work contexts (never an ISR), so blocking on the lock is safe. */
K_MUTEX_DEFINE(s_tx_mutex);

static void aic_send_line(const char *line)
{
    if (!device_is_ready(uart_dev)) {
        return;
    }
    k_mutex_lock(&s_tx_mutex, K_FOREVER);
    for (const char *p = line; *p; p++) {
        uart_poll_out(uart_dev, (unsigned char)*p);
    }
    uart_poll_out(uart_dev, '\n');
    k_mutex_unlock(&s_tx_mutex);
}

/* --- message dispatch (display work queue context) ------------------------- */

static void aic_fire_cue(const char *cue)
{
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_HAPTIC)
    /* Map the host's semantic cue to a DRV2605L effect. Distinct feels for the
     * three cues are tuned/verified on the real motor in a later step; the
     * mapping is the seam. See docs/phase5-interface-contract.md §4.3. */
    uint8_t effect = AIC_HAPTIC_BUZZ1;
    if (strcmp(cue, "block") == 0) {
        effect = AIC_HAPTIC_DOUBLE_CLICK; /* permission / stuck: double tap */
    } else if (strcmp(cue, "stopped") == 0) {
        effect = AIC_HAPTIC_BUZZ1;        /* interrupted: one buzz */
    } else if (strcmp(cue, "done") == 0) {
        effect = AIC_HAPTIC_STRONG_CLICK; /* finished: one firm click (Sharp
                                           * Tick was too faint on the ERM) */
    }
    aic_haptic_play_effect(effect);
#else
    ARG_UNUSED(cue);
#endif
}

/* Reused only on the single display-queue thread, so file-static keeps the work
 * handler's stack small (the display/system work queue stack is modest). */
static struct aic_screen_model s_work_model;

/* The screen the device is currently showing, snapshotted from each `screen`
 * message so a button press can tag its `input` with the right state + id.
 * Written on the display queue, read on the behavior thread — a torn read is
 * harmless (short strings; the host binds the response to the correct item via
 * `id`, which is exactly what `id` is for when the screen changed under a late
 * press). */
static char s_cur_screen[AIC_STATE_LEN];
static char s_cur_id[AIC_ID_LEN];

static void aic_handle_message(const char *json)
{
    char t[16];
    if (!json_str(json, "t", t, sizeof(t))) {
        return; /* not a recognisable message */
    }

    if (strcmp(t, "screen") == 0) {
        memset(&s_work_model, 0, sizeof(s_work_model));
        json_str(json, "state", s_work_model.state, sizeof(s_work_model.state));
        json_str(json, "task", s_work_model.task, sizeof(s_work_model.task));
        json_str(json, "line", s_work_model.line, sizeof(s_work_model.line));
        json_str(json, "question", s_work_model.question, sizeof(s_work_model.question));
        json_str(json, "clock", s_work_model.clock, sizeof(s_work_model.clock));
        json_str(json, "id", s_work_model.id, sizeof(s_work_model.id));
        /* Remember what we are now showing, for button `input` tagging. */
        strncpy(s_cur_screen, s_work_model.state, sizeof(s_cur_screen) - 1);
        s_cur_screen[sizeof(s_cur_screen) - 1] = '\0';
        strncpy(s_cur_id, s_work_model.id, sizeof(s_cur_id) - 1);
        s_cur_id[sizeof(s_cur_id) - 1] = '\0';
        aic_screen_render(&s_work_model);
    } else if (strcmp(t, "haptic") == 0) {
        char cue[16];
        if (json_str(json, "cue", cue, sizeof(cue))) {
            aic_fire_cue(cue);
        } else {
            /* Dev/tuning aid: {"t":"haptic","v":1,"id":N} plays raw DRV2605L
             * ROM effect N (1..123) so the real motor's feel can be swept live
             * without a reflash per candidate. Not part of the host contract's
             * semantic-cue path (§4.3); host production traffic uses "cue". */
            long id;
            if (json_int(json, "id", &id) && id >= 1 && id <= 123) {
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_HAPTIC)
                aic_haptic_play_effect((uint8_t)id);
#endif
            }
        }
    } else if (strcmp(t, "ping") == 0) {
        aic_send_line("{\"t\":\"pong\",\"v\":1}");
    }
    /* "tasks": browse-mode cache — deferred. Unknown `t`: ignore (§3). */
}

/* --- button input (behavior thread context) -------------------------------- */

/*
 * Called from the &aic_input keymap behavior (src/behavior_input.c) when a
 * physical button is pressed. Report WHICH button plus the current screen state
 * + id; the host decides what the press means for that screen (contract §5.1).
 */
void aic_comm_send_input(uint32_t key)
{
    const char *k;
    switch (key) {
    case AIC_KEY_VOICE: k = "VOICE"; break;
    case AIC_KEY_YES:   k = "YES";   break;
    case AIC_KEY_NO:    k = "NO";    break;
    case AIC_KEY_OPEN:  k = "OPEN";  break;
    default:            return;
    }

    char line[160];
    if (s_cur_id[0] != '\0') {
        snprintf(line, sizeof(line),
                 "{\"t\":\"input\",\"v\":1,\"src\":\"button\",\"key\":\"%s\","
                 "\"screen\":\"%s\",\"id\":\"%s\"}",
                 k, s_cur_screen, s_cur_id);
    } else {
        snprintf(line, sizeof(line),
                 "{\"t\":\"input\",\"v\":1,\"src\":\"button\",\"key\":\"%s\","
                 "\"screen\":\"%s\"}",
                 k, s_cur_screen);
    }
    aic_send_line(line);
}

static struct aic_line s_work_line; /* display-queue only */

static void aic_process_work_cb(struct k_work *work)
{
    ARG_UNUSED(work);
    while (k_msgq_get(&s_rx_q, &s_work_line, K_NO_WAIT) == 0) {
        s_work_line.buf[s_work_line.len] = '\0'; /* len < AIC_LINE_MAX */
        aic_handle_message(s_work_line.buf);
    }
}
K_WORK_DEFINE(s_process_work, aic_process_work_cb);

/* Restore the local standby screen (host disconnected). Marshalled to the
 * display queue because it touches LVGL. */
static void aic_standby_work_cb(struct k_work *work)
{
    ARG_UNUSED(work);
    struct aic_screen_model standby;
    memset(&standby, 0, sizeof(standby));
    aic_screen_render(&standby);
    /* No live screen context once the host is gone. */
    s_cur_screen[0] = '\0';
    s_cur_id[0] = '\0';
}
K_WORK_DEFINE(s_standby_work, aic_standby_work_cb);

/* --- UART RX ISR ----------------------------------------------------------- */

static struct aic_line s_scratch; /* ISR-only accumulator */
static bool s_overflow;           /* dropping an overlong line until next newline */

static void aic_rx_byte(uint8_t c)
{
    if (c == '\n' || c == '\r') {
        if (!s_overflow && s_scratch.len > 0) {
            /* k_msgq_put copies by value; safe to reuse s_scratch after. */
            s_scratch.buf[s_scratch.len] = '\0';
            if (k_msgq_put(&s_rx_q, &s_scratch, K_NO_WAIT) == 0) {
                k_work_submit_to_queue(zmk_display_work_q(), &s_process_work);
            }
        }
        s_scratch.len = 0;
        s_overflow = false;
        return;
    }
    if (s_overflow) {
        return;
    }
    if (s_scratch.len < AIC_LINE_MAX - 1) {
        s_scratch.buf[s_scratch.len++] = (char)c;
    } else {
        s_overflow = true; /* wait for a newline to resync */
    }
}

static void aic_uart_isr(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);
    if (!uart_irq_update(dev)) {
        return;
    }
    while (uart_irq_rx_ready(dev)) {
        uint8_t byte;
        if (uart_fifo_read(dev, &byte, 1) != 1) {
            break;
        }
        aic_rx_byte(byte);
    }
}

/* --- setup + DTR-driven hello/standby thread ------------------------------- */

#define AIC_HELLO \
    "{\"t\":\"hello\",\"v\":1,\"fw\":\"0.1.0\",\"proto\":1," \
    "\"caps\":[\"screen\",\"haptic\",\"tasks\",\"input\"]}"

static void aic_comm_main(void)
{
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("CDC UART device not ready");
        return;
    }

    uart_irq_rx_disable(uart_dev);
    uart_irq_tx_disable(uart_dev);

    int ret = uart_irq_callback_user_data_set(uart_dev, aic_uart_isr, NULL);
    if (ret < 0) {
        LOG_ERR("failed to set UART callback: %d", ret);
        return;
    }
    uart_irq_rx_enable(uart_dev);
    LOG_INF("host comm channel ready on %s", uart_dev->name);

    bool was_up = false;
    for (;;) {
        uint32_t dtr = 0;
        (void)uart_line_ctrl_get(uart_dev, UART_LINE_CTRL_DTR, &dtr);

        if (dtr && !was_up) {
            was_up = true;
            /* Give the host a moment to start reading before we greet it. */
            k_msleep(100);
            aic_send_line(AIC_HELLO);
        } else if (!dtr && was_up) {
            was_up = false;
            /* Port closed / cable pulled: fall back to local standby. */
            k_work_submit_to_queue(zmk_display_work_q(), &s_standby_work);
        }
        k_msleep(200);
    }
}

K_THREAD_DEFINE(aic_comm_thread, 1024, aic_comm_main, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
