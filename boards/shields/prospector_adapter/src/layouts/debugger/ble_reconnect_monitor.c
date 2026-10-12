#include <zephyr/kernel.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#include <stdio.h>
#include <string.h>

#include <zephyr/bluetooth/hci.h>
#include <zmk/ble.h>
#include <zmk/event_manager.h>
#include <zmk/events/split_central_status_changed.h>

#include "ble_reconnect_monitor.h"
#include "debugger_log.h"

#define RECONNECT_WINDOW_MS (10U * 60U * 1000U)
#define RECONNECT_HISTORY_SIZE 5U

struct reconnect_history {
    uint32_t timestamps[RECONNECT_HISTORY_SIZE];
    uint8_t count;
    bool log_pending;
};

static struct reconnect_history histories[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];
static struct k_spinlock histories_lock;

static bool is_expected_disconnect(uint8_t reason) {
    switch (reason) {
    case BT_HCI_ERR_REMOTE_USER_TERM_CONN:
    case BT_HCI_ERR_REMOTE_POWER_OFF:
    case BT_HCI_ERR_LOCALHOST_TERM_CONN:
        return true;
    default:
        return false;
    }
}

static uint8_t prune_history(struct reconnect_history *history, uint32_t now) {
    uint8_t retained = 0U;

    for (uint8_t i = 0U; i < history->count; i++) {
        if ((uint32_t)(now - history->timestamps[i]) <= RECONNECT_WINDOW_MS) {
            history->timestamps[retained++] = history->timestamps[i];
        }
    }

    history->count = retained;
    return retained;
}

static void record_unexpected_disconnect(uint8_t source, uint32_t now) {
    k_spinlock_key_t key = k_spin_lock(&histories_lock);
    struct reconnect_history *history = &histories[source];

    prune_history(history, now);
    if (history->count == RECONNECT_HISTORY_SIZE) {
        memmove(&history->timestamps[0], &history->timestamps[1],
                sizeof(history->timestamps[0]) * (RECONNECT_HISTORY_SIZE - 1U));
        history->count--;
    }

    history->timestamps[history->count++] = now;
    history->log_pending = history->count >= 2U;
    k_spin_unlock(&histories_lock, key);
}

static void add_reconnect_log(uint8_t source, uint8_t count) {
    char text[20];
    enum zmk_debugger_log_type type;

    if (count >= 5U) {
        type = ZMK_DEBUGGER_LOG_ERROR;
        snprintf(text, sizeof(text), "P%d Failure 5+", source + 1);
    } else if (count >= 3U) {
        type = ZMK_DEBUGGER_LOG_WARNING;
        snprintf(text, sizeof(text), "P%d Unstable %d", source + 1, count);
    } else {
        type = ZMK_DEBUGGER_LOG_MESSAGE;
        snprintf(text, sizeof(text), "P%d Reconnect", source + 1);
    }

    zmk_debugger_log_add(type, text);
}

bool zmk_debugger_reconnect_monitor_log_if_pending(uint8_t source) {
    if (source >= ZMK_SPLIT_BLE_PERIPHERAL_COUNT) {
        return false;
    }

    uint8_t count = 0U;
    k_spinlock_key_t key = k_spin_lock(&histories_lock);
    struct reconnect_history *history = &histories[source];

    prune_history(history, (uint32_t)k_uptime_get());
    if (history->log_pending && history->count >= 2U) {
        count = history->count;
    }
    history->log_pending = false;
    k_spin_unlock(&histories_lock, key);

    if (count < 2U) {
        return false;
    }

    add_reconnect_log(source, count);
    return true;
}

static int reconnect_monitor_listener_cb(const zmk_event_t *eh) {
    const struct zmk_split_central_status_changed *event =
        as_zmk_split_central_status_changed(eh);
    if (event == NULL || event->slot >= ZMK_SPLIT_BLE_PERIPHERAL_COUNT || event->connected ||
        is_expected_disconnect(event->reason)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    record_unexpected_disconnect(event->slot, (uint32_t)k_uptime_get());

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(reconnect_monitor, reconnect_monitor_listener_cb);
ZMK_SUBSCRIPTION(reconnect_monitor, zmk_split_central_status_changed);

#endif /* CONFIG_ZMK_SPLIT && CONFIG_ZMK_SPLIT_ROLE_CENTRAL */
