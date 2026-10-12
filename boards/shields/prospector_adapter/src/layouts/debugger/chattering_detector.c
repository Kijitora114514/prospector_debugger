#include <zephyr/kernel.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#include <stdio.h>
#include <string.h>

#include <drivers/behavior.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/split_central_status_changed.h>
#include <zmk/keymap.h>
#include <zmk/keys.h>
#include <zmk/matrix.h>

#include "debugger_log.h"

#define CHATTERING_INTERVAL_MS 30U
#define ACTIVITY_WINDOW_MS 1000U
#define PRESS_WARNING_COUNT 10U
#define CHATTERING_WARNING_COUNT 3U
#define HOLDING_WARNING_MS 30000U
#define KEY_NAME_MAX_LENGTH 5U

#define KEY_STATE_HAS_PRESS BIT(0)
#define KEY_STATE_RELEASED BIT(1)
#define KEY_STATE_PRESSED BIT(2)
#define KEY_STATE_HOLDING_REPORTED BIT(3)
#define KEY_STATE_PRESSED_AT_DISCONNECT BIT(4)

enum key_monitor_log_kind {
    KEY_MONITOR_LOG_CHATTERING,
    KEY_MONITOR_LOG_HOLDING,
    KEY_MONITOR_LOG_HOLDING_DOWN,
};

struct key_monitor_log_entry {
    uint32_t position;
    uint8_t kind;
};

struct key_activity_state {
    uint32_t last_press_ms;
    uint32_t last_release_ms;
    uint32_t window_start_ms;
    uint8_t press_count;
    uint8_t chatter_count;
    uint8_t flags;
};

static struct key_activity_state
    key_states[ZMK_SPLIT_BLE_PERIPHERAL_COUNT][ZMK_KEYMAP_LEN];
static struct k_spinlock key_states_lock;

K_MSGQ_DEFINE(key_monitor_log_msgq, sizeof(struct key_monitor_log_entry),
              ZMK_DEBUGGER_LOG_MAX_ENTRIES, 4);

static bool behavior_is_transparent(const char *behavior_dev) {
#define RETURN_TRUE_IF_MATCH(node_id)                                                              \
    if (strcmp(behavior_dev, DEVICE_DT_NAME(node_id)) == 0) {                                      \
        return true;                                                                               \
    }

    DT_FOREACH_STATUS_OKAY(zmk_behavior_transparent, RETURN_TRUE_IF_MATCH)

#undef RETURN_TRUE_IF_MATCH
    return false;
}

static bool keycode_from_binding(const struct zmk_behavior_binding *binding, uint32_t *keycode) {
    if (binding == NULL || binding->behavior_dev == NULL) {
        return false;
    }

#define RETURN_PARAM1_IF_MATCH(node_id)                                                            \
    if (strcmp(binding->behavior_dev, DEVICE_DT_NAME(node_id)) == 0) {                             \
        *keycode = binding->param1;                                                                \
        return true;                                                                               \
    }

    DT_FOREACH_STATUS_OKAY(zmk_behavior_key_press, RETURN_PARAM1_IF_MATCH)
    DT_FOREACH_STATUS_OKAY(zmk_behavior_key_toggle, RETURN_PARAM1_IF_MATCH)
    DT_FOREACH_STATUS_OKAY(zmk_behavior_sticky_key, RETURN_PARAM1_IF_MATCH)

#undef RETURN_PARAM1_IF_MATCH

#define RETURN_PARAM2_IF_MATCH(node_id)                                                            \
    if (strcmp(binding->behavior_dev, DEVICE_DT_NAME(node_id)) == 0) {                             \
        *keycode = binding->param2;                                                                \
        return true;                                                                               \
    }

    DT_FOREACH_STATUS_OKAY(zmk_behavior_hold_tap, RETURN_PARAM2_IF_MATCH)

#undef RETURN_PARAM2_IF_MATCH
    return false;
}

static const struct zmk_behavior_binding *binding_for_position(uint32_t position) {
    int highest_layer = zmk_keymap_highest_layer_active();

    for (int layer_index = highest_layer; layer_index >= 0; layer_index--) {
        zmk_keymap_layer_id_t layer_id = zmk_keymap_layer_index_to_id(layer_index);
        if (layer_id == ZMK_KEYMAP_LAYER_ID_INVAL || !zmk_keymap_layer_active(layer_id)) {
            continue;
        }

        const struct zmk_behavior_binding *binding =
            zmk_keymap_get_layer_binding_at_idx(layer_id, position);
        if (binding == NULL || binding->behavior_dev == NULL) {
            return NULL;
        }

        if (behavior_is_transparent(binding->behavior_dev)) {
            continue;
        }

        return binding;
    }

    return NULL;
}

static bool keycode_for_position(uint32_t position, uint32_t *keycode) {
    return keycode_from_binding(binding_for_position(position), keycode);
}

static bool behavior_is_hold_exempt(const char *behavior_dev) {
    if (behavior_dev == NULL) {
        return false;
    }

#define RETURN_TRUE_IF_HOLD_EXEMPT(node_id)                                                        \
    if (strcmp(behavior_dev, DEVICE_DT_NAME(node_id)) == 0) {                                      \
        return true;                                                                               \
    }

    DT_FOREACH_STATUS_OKAY(zmk_behavior_hold_tap, RETURN_TRUE_IF_HOLD_EXEMPT)
    DT_FOREACH_STATUS_OKAY(zmk_behavior_momentary_layer, RETURN_TRUE_IF_HOLD_EXEMPT)
    DT_FOREACH_STATUS_OKAY(zmk_behavior_toggle_layer, RETURN_TRUE_IF_HOLD_EXEMPT)
    DT_FOREACH_STATUS_OKAY(zmk_behavior_to_layer, RETURN_TRUE_IF_HOLD_EXEMPT)

#undef RETURN_TRUE_IF_HOLD_EXEMPT
    return false;
}

static bool position_is_hold_exempt(uint32_t position) {
    const struct zmk_behavior_binding *binding = binding_for_position(position);
    if (binding == NULL) {
        return false;
    }
    if (behavior_is_hold_exempt(binding->behavior_dev)) {
        return true;
    }

    uint32_t encoded;
    if (!keycode_from_binding(binding, &encoded)) {
        return false;
    }

    uint16_t usage_page = ZMK_HID_USAGE_PAGE(encoded);
    uint16_t keycode = ZMK_HID_USAGE_ID(encoded);
    if (usage_page == 0U) {
        usage_page = HID_USAGE_KEY;
    }

    return usage_page == HID_USAGE_KEY && keycode >= HID_USAGE_KEY_KEYBOARD_LEFTCONTROL &&
           keycode <= HID_USAGE_KEY_KEYBOARD_RIGHT_GUI;
}

static void format_key_name(uint32_t position, char *name, size_t name_size) {
    uint32_t encoded;
    if (!keycode_for_position(position, &encoded)) {
        snprintf(name, name_size, "K%u", (unsigned int)position);
        return;
    }

    uint16_t usage_page = ZMK_HID_USAGE_PAGE(encoded);
    uint16_t keycode = ZMK_HID_USAGE_ID(encoded);
    bool shifted = (SELECT_MODS(encoded) & (MOD_LSFT | MOD_RSFT)) != 0U;
    if (usage_page == 0U) {
        usage_page = HID_USAGE_KEY;
    }

    if (usage_page != HID_USAGE_KEY) {
        snprintf(name, name_size, "K%u", (unsigned int)position);
        return;
    }

    if (keycode >= HID_USAGE_KEY_KEYBOARD_A && keycode <= HID_USAGE_KEY_KEYBOARD_Z) {
        snprintf(name, name_size, "%c", 'A' + keycode - HID_USAGE_KEY_KEYBOARD_A);
        return;
    }

    if (keycode >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION &&
        keycode <= HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS) {
        uint8_t number = keycode - HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION;
        const char shifted_numbers[] = "!@#$%^&*(";
        snprintf(name, name_size, "%c", shifted ? shifted_numbers[number] : '1' + number);
        return;
    }

    if (keycode == HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) {
        snprintf(name, name_size, "%c", shifted ? ')' : '0');
        return;
    }

    if (keycode >= HID_USAGE_KEY_KEYBOARD_F1 && keycode <= HID_USAGE_KEY_KEYBOARD_F12) {
        snprintf(name, name_size, "F%d", keycode - HID_USAGE_KEY_KEYBOARD_F1 + 1);
        return;
    }

    const char *short_name = NULL;
    switch (keycode) {
    case HID_USAGE_KEY_KEYBOARD_RETURN_ENTER:
        short_name = "ENT";
        break;
    case HID_USAGE_KEY_KEYBOARD_ESCAPE:
        short_name = "ESC";
        break;
    case HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE:
        short_name = "BSP";
        break;
    case HID_USAGE_KEY_KEYBOARD_TAB:
        short_name = "TAB";
        break;
    case HID_USAGE_KEY_KEYBOARD_SPACEBAR:
        short_name = "SPC";
        break;
    case HID_USAGE_KEY_KEYBOARD_DELETE_FORWARD:
        short_name = "DEL";
        break;
    case HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE:
        short_name = shifted ? "_" : "-";
        break;
    case HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS:
        short_name = shifted ? "+" : "=";
        break;
    case HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE:
        short_name = shifted ? "{" : "[";
        break;
    case HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE:
        short_name = shifted ? "}" : "]";
        break;
    case HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE:
        short_name = shifted ? "|" : "\\";
        break;
    case HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON:
        short_name = shifted ? ":" : ";";
        break;
    case HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE:
        short_name = shifted ? "\"" : "'";
        break;
    case HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE:
        short_name = shifted ? "~" : "`";
        break;
    case HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN:
        short_name = shifted ? "<" : ",";
        break;
    case HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN:
        short_name = shifted ? ">" : ".";
        break;
    case HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK:
        short_name = shifted ? "?" : "/";
        break;
    case HID_USAGE_KEY_KEYBOARD_CAPS_LOCK:
        short_name = "CAP";
        break;
    case HID_USAGE_KEY_KEYBOARD_LEFTCONTROL:
    case HID_USAGE_KEY_KEYBOARD_RIGHTCONTROL:
        short_name = "CTL";
        break;
    case HID_USAGE_KEY_KEYBOARD_LEFTSHIFT:
    case HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT:
        short_name = "SFT";
        break;
    case HID_USAGE_KEY_KEYBOARD_LEFTALT:
    case HID_USAGE_KEY_KEYBOARD_RIGHTALT:
        short_name = "ALT";
        break;
    case HID_USAGE_KEY_KEYBOARD_LEFT_GUI:
    case HID_USAGE_KEY_KEYBOARD_RIGHT_GUI:
        short_name = "GUI";
        break;
    case HID_USAGE_KEY_KEYBOARD_LEFTARROW:
        short_name = "LFT";
        break;
    case HID_USAGE_KEY_KEYBOARD_RIGHTARROW:
        short_name = "RGT";
        break;
    case HID_USAGE_KEY_KEYBOARD_UPARROW:
        short_name = "UP";
        break;
    case HID_USAGE_KEY_KEYBOARD_DOWNARROW:
        short_name = "DWN";
        break;
    default:
        break;
    }

    if (short_name != NULL) {
        snprintf(name, name_size, "%s", short_name);
    } else {
        snprintf(name, name_size, "K%u", (unsigned int)position);
    }
}

static void add_key_monitor_log(uint32_t position, enum key_monitor_log_kind kind) {
    enum zmk_debugger_log_type type;
    const char *suffix;

    switch (kind) {
    case KEY_MONITOR_LOG_HOLDING:
        type = ZMK_DEBUGGER_LOG_WARNING;
        suffix = "Holding";
        break;
    case KEY_MONITOR_LOG_HOLDING_DOWN:
        type = ZMK_DEBUGGER_LOG_ERROR;
        suffix = "Holding Down";
        break;
    case KEY_MONITOR_LOG_CHATTERING:
    default:
        type = ZMK_DEBUGGER_LOG_WARNING;
        suffix = "Chattering";
        break;
    }

    char key_name[KEY_NAME_MAX_LENGTH];
    char text[20];
    format_key_name(position, key_name, sizeof(key_name));
    snprintf(text, sizeof(text), "%s %s", key_name, suffix);
    zmk_debugger_log_add(type, text);
}

static void key_monitor_log_work_cb(struct k_work *work) {
    ARG_UNUSED(work);

    struct key_monitor_log_entry entry;

    while (k_msgq_get(&key_monitor_log_msgq, &entry, K_NO_WAIT) == 0) {
        add_key_monitor_log(entry.position, entry.kind);
    }
}

K_WORK_DEFINE(key_monitor_log_work, key_monitor_log_work_cb);

static void queue_key_monitor_log(uint32_t position, enum key_monitor_log_kind kind) {
    if (!zmk_display_is_initialized()) {
        return;
    }

    struct key_monitor_log_entry entry = {
        .position = position,
        .kind = kind,
    };
    if (k_msgq_put(&key_monitor_log_msgq, &entry, K_NO_WAIT) != 0) {
        return;
    }

    k_work_submit_to_queue(zmk_display_work_q(), &key_monitor_log_work);
}

static void holding_check_work_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(holding_check_work, holding_check_work_cb);

static void schedule_holding_check(void) {
    if (zmk_display_is_initialized()) {
        k_work_schedule_for_queue(zmk_display_work_q(), &holding_check_work,
                                  K_MSEC(HOLDING_WARNING_MS));
    }
}

static void holding_check_work_cb(struct k_work *work) {
    ARG_UNUSED(work);

    uint32_t now = (uint32_t)k_uptime_get();
    uint32_t next_delay = UINT32_MAX;

    for (uint8_t source = 0U; source < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; source++) {
        for (uint32_t position = 0U; position < ZMK_KEYMAP_LEN; position++) {
            bool evaluate = false;
            k_spinlock_key_t key = k_spin_lock(&key_states_lock);
            struct key_activity_state *state = &key_states[source][position];

            if ((state->flags & (KEY_STATE_PRESSED | KEY_STATE_HOLDING_REPORTED)) ==
                KEY_STATE_PRESSED) {
                uint32_t held_ms = now - state->last_press_ms;
                if (held_ms >= HOLDING_WARNING_MS) {
                    state->flags |= KEY_STATE_HOLDING_REPORTED;
                    evaluate = true;
                } else {
                    next_delay = MIN(next_delay, HOLDING_WARNING_MS - held_ms);
                }
            }

            k_spin_unlock(&key_states_lock, key);

            if (evaluate && !position_is_hold_exempt(position)) {
                queue_key_monitor_log(position, KEY_MONITOR_LOG_HOLDING);
            }
        }
    }

    if (next_delay != UINT32_MAX) {
        k_work_reschedule_for_queue(zmk_display_work_q(), &holding_check_work,
                                    K_MSEC(MAX(next_delay, 1U)));
    }
}

static void update_activity_window(struct key_activity_state *state, uint32_t now) {
    if (state->press_count == 0U ||
        (uint32_t)(now - state->window_start_ms) > ACTIVITY_WINDOW_MS) {
        state->window_start_ms = now;
        state->press_count = 0U;
        state->chatter_count = 0U;
    }
}

static bool count_press(struct key_activity_state *state) {
    if (state->press_count < PRESS_WARNING_COUNT) {
        state->press_count++;
    }

    if (state->press_count >= PRESS_WARNING_COUNT) {
        state->press_count = 0U;
        return true;
    }

    return false;
}

static bool count_rapid_repress(struct key_activity_state *state) {
    if (state->chatter_count < CHATTERING_WARNING_COUNT) {
        state->chatter_count++;
    }

    if (state->chatter_count >= CHATTERING_WARNING_COUNT) {
        state->chatter_count = 0U;
        return true;
    }

    return false;
}

static bool handle_press(uint8_t source, uint32_t position, uint32_t now) {
    struct key_activity_state *state = &key_states[source][position];

    /* Ignore duplicate press states; only physical state transitions count. */
    if ((state->flags & KEY_STATE_PRESSED) != 0U) {
        return false;
    }

    bool rapid_repress =
        (state->flags & (KEY_STATE_HAS_PRESS | KEY_STATE_RELEASED)) ==
            (KEY_STATE_HAS_PRESS | KEY_STATE_RELEASED) &&
        (uint32_t)(now - state->last_press_ms) <= CHATTERING_INTERVAL_MS &&
        (uint32_t)(now - state->last_release_ms) <= CHATTERING_INTERVAL_MS;

    update_activity_window(state, now);
    bool warn = count_press(state);

    if (rapid_repress) {
        warn |= count_rapid_repress(state);
    }

    state->last_press_ms = now;
    state->flags |= KEY_STATE_HAS_PRESS | KEY_STATE_PRESSED;
    state->flags &= ~(KEY_STATE_RELEASED | KEY_STATE_HOLDING_REPORTED);

    return warn;
}

static void handle_release(uint8_t source, uint32_t position, uint32_t now) {
    struct key_activity_state *state = &key_states[source][position];

    /* Ignore duplicate or unmatched releases. */
    if ((state->flags & KEY_STATE_PRESSED) == 0U) {
        return;
    }

    state->last_release_ms = now;
    state->flags &= ~(KEY_STATE_PRESSED | KEY_STATE_HOLDING_REPORTED);
    state->flags |= KEY_STATE_RELEASED;
}

static void handle_connection_event(const struct zmk_split_central_status_changed *event) {
    if (event->slot >= ZMK_SPLIT_BLE_PERIPHERAL_COUNT) {
        return;
    }

    for (uint32_t position = 0U; position < ZMK_KEYMAP_LEN; position++) {
        bool holding_down = false;
        k_spinlock_key_t key = k_spin_lock(&key_states_lock);
        struct key_activity_state *state = &key_states[event->slot][position];

        if (!event->connected) {
            if ((state->flags & KEY_STATE_PRESSED) != 0U) {
                state->flags |= KEY_STATE_PRESSED_AT_DISCONNECT;
            }
        } else if ((state->flags & KEY_STATE_PRESSED_AT_DISCONNECT) != 0U) {
            holding_down = (state->flags & KEY_STATE_PRESSED) != 0U;
            state->flags &= ~KEY_STATE_PRESSED_AT_DISCONNECT;
        }

        k_spin_unlock(&key_states_lock, key);

        if (holding_down) {
            queue_key_monitor_log(position, KEY_MONITOR_LOG_HOLDING_DOWN);
        }
    }
}

static int chattering_detector_listener_cb(const zmk_event_t *eh) {
    const struct zmk_split_central_status_changed *connection_event =
        as_zmk_split_central_status_changed(eh);
    if (connection_event != NULL) {
        handle_connection_event(connection_event);
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_position_state_changed *event = as_zmk_position_state_changed(eh);

    /* Only remote physical positions are observed. Sensor, input-split, macro, and local events
     * use other event sources and do not reach this detector.
     */
    if (event == NULL || event->source == ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL ||
        event->source >= ZMK_SPLIT_BLE_PERIPHERAL_COUNT || event->position >= ZMK_KEYMAP_LEN) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    uint32_t now = (uint32_t)event->timestamp;
    bool chattering = false;
    k_spinlock_key_t key = k_spin_lock(&key_states_lock);
    if (event->state) {
        chattering = handle_press(event->source, event->position, now);
    } else {
        handle_release(event->source, event->position, now);
    }
    k_spin_unlock(&key_states_lock, key);

    if (chattering) {
        queue_key_monitor_log(event->position, KEY_MONITOR_LOG_CHATTERING);
    }
    if (event->state) {
        schedule_holding_check();
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(chattering_detector, chattering_detector_listener_cb);
ZMK_SUBSCRIPTION(chattering_detector, zmk_position_state_changed);
ZMK_SUBSCRIPTION(chattering_detector, zmk_split_central_status_changed);

#endif /* CONFIG_ZMK_SPLIT && CONFIG_ZMK_SPLIT_ROLE_CENTRAL */
