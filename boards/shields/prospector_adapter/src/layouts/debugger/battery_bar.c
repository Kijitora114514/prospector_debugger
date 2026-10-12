#include "battery_bar.h"

#include <zmk/display.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_central_status_changed.h>
#include <zmk/event_manager.h>

#include <fonts.h>

#include "heroicons.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct battery_update_state {
    uint8_t source;
    uint8_t level;
};

struct connection_update_state {
    uint8_t source;
    bool connected;
};

static bool connected[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];
static bool battery_known[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];
static uint8_t battery_levels[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];

static void set_battery_display(lv_obj_t *widget_obj, uint8_t source) {
    if (source >= ZMK_SPLIT_BLE_PERIPHERAL_COUNT) {
        return;
    }

    lv_obj_t *container = lv_obj_get_child(widget_obj, source);
    if (container == NULL) {
        return;
    }

    lv_obj_t *image = lv_obj_get_child(container, 0);
    lv_obj_t *label = lv_obj_get_child(container, 1);
    if (image == NULL || label == NULL) {
        return;
    }

    if (!connected[source]) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
        zmk_heroicon_set(image, ZMK_HEROICON_DISCONNECT, lv_color_hex(0xFF0000));
    } else if (!battery_known[source]) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
        zmk_heroicon_set(image, ZMK_HEROICON_CONNECT, lv_color_white());
    } else if (battery_levels[source] <= 10) {
        lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(label, "%d%%", battery_levels[source]);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFF00), LV_PART_MAIN);
    } else if (battery_levels[source] <= 80) {
        lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(label, "%d%%", battery_levels[source]);
        lv_obj_set_style_text_color(label, lv_color_hex(0x00FF00), LV_PART_MAIN);
    } else {
        lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(label, "%d%%", battery_levels[source]);
        lv_obj_set_style_text_color(label, lv_color_hex(0x0000FF), LV_PART_MAIN);
    }
}

static void update_battery_displays(uint8_t source) {
    struct zmk_widget_battery_bar *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        if (widget->initialized) {
            set_battery_display(widget->obj, source);
        }
    }
}

void battery_bar_battery_update_cb(struct battery_update_state state) {
    if (state.source >= ZMK_SPLIT_BLE_PERIPHERAL_COUNT) {
        return;
    }
    battery_levels[state.source] = state.level;
    battery_known[state.source] = true;
    update_battery_displays(state.source);
}

static struct battery_update_state battery_bar_get_battery_state(const zmk_event_t *eh) {
    if (eh == NULL) {
        return (struct battery_update_state){.source = 0, .level = 0};
    }

    const struct zmk_peripheral_battery_state_changed *bat_ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (bat_ev == NULL) {
        return (struct battery_update_state){.source = 0, .level = 0};
    }

    return (struct battery_update_state){
        .source = bat_ev->source,
        .level = bat_ev->state_of_charge,
    };
}

void battery_bar_connection_update_cb(struct connection_update_state state) {
    if (state.source >= ZMK_SPLIT_BLE_PERIPHERAL_COUNT) {
        return;
    }
    connected[state.source] = state.connected;
    update_battery_displays(state.source);
}

static struct connection_update_state battery_bar_get_connection_state(const zmk_event_t *eh) {
    if (eh == NULL) {
        return (struct connection_update_state){.source = 0, .connected = false};
    }

    const struct zmk_split_central_status_changed *conn_ev =
        as_zmk_split_central_status_changed(eh);
    if (conn_ev == NULL) {
        return (struct connection_update_state){.source = 0, .connected = false};
    }

    return (struct connection_update_state){
        .source = conn_ev->slot,
        .connected = conn_ev->connected,
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_bar_battery, struct battery_update_state,
                            battery_bar_battery_update_cb, battery_bar_get_battery_state);
ZMK_SUBSCRIPTION(widget_battery_bar_battery, zmk_peripheral_battery_state_changed);

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_bar_connection, struct connection_update_state,
                            battery_bar_connection_update_cb, battery_bar_get_connection_state);
ZMK_SUBSCRIPTION(widget_battery_bar_connection, zmk_split_central_status_changed);

int zmk_widget_battery_bar_init(struct zmk_widget_battery_bar *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_flex_flow(widget->obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(widget->obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(widget->obj, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(widget->obj, 8, LV_PART_MAIN);
    lv_obj_remove_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
        lv_obj_t *container = lv_obj_create(widget->obj);
        lv_obj_set_size(container, 48, 36);
        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);

        lv_obj_t *image = lv_image_create(container);
        lv_obj_set_size(image, 36, 36);
        lv_obj_set_style_bg_opa(image, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_pad_all(image, 0, LV_PART_MAIN);
        lv_obj_center(image);

        lv_obj_t *label = lv_label_create(container);
        lv_obj_set_style_text_font(label, &FG_Medium_20, LV_PART_MAIN);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_center(label);

        set_battery_display(widget->obj, i);
    }

    sys_slist_append(&widgets, &widget->node);
    widget->initialized = true;
    widget_battery_bar_connection_init();
    widget_battery_bar_battery_init();

    return 0;
}

lv_obj_t *zmk_widget_battery_bar_obj(struct zmk_widget_battery_bar *widget) { return widget->obj; }

void zmk_widget_battery_bar_set_compact(bool compact) {
    ARG_UNUSED(compact);
}
