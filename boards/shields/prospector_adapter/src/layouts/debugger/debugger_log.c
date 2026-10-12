#include "debugger_log.h"

#include <string.h>

#include <fonts.h>

#include "heroicons.h"

#define LOG_TEXT_MAX_LENGTH 64
#define LOG_ROW_HEIGHT 30
#define LOG_TOP_MARGIN 15
#define LOG_ICON_SIZE 24
#define LOG_ICON_X 17
#define LOG_TEXT_X 45
#define LOG_TEXT_WIDTH 162

struct debugger_log_entry {
    enum zmk_debugger_log_type type;
    char text[LOG_TEXT_MAX_LENGTH];
};

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);
static struct debugger_log_entry entries[ZMK_DEBUGGER_LOG_MAX_ENTRIES];
static uint8_t entry_count;

static enum zmk_heroicon icon_for_type(enum zmk_debugger_log_type type) {
    switch (type) {
    case ZMK_DEBUGGER_LOG_WARNING:
        return ZMK_HEROICON_WARNING;
    case ZMK_DEBUGGER_LOG_ERROR:
        return ZMK_HEROICON_ERROR;
    case ZMK_DEBUGGER_LOG_CONNECT:
        return ZMK_HEROICON_CONNECT;
    case ZMK_DEBUGGER_LOG_DISCONNECT:
        return ZMK_HEROICON_DISCONNECT;
    case ZMK_DEBUGGER_LOG_MESSAGE:
    default:
        return ZMK_HEROICON_MESSAGE;
    }
}

static lv_color_t color_for_type(enum zmk_debugger_log_type type) {
    switch (type) {
    case ZMK_DEBUGGER_LOG_WARNING:
        return lv_color_hex(0xFFFF00);
    case ZMK_DEBUGGER_LOG_ERROR:
    case ZMK_DEBUGGER_LOG_DISCONNECT:
        return lv_color_hex(0xFF0000);
    case ZMK_DEBUGGER_LOG_CONNECT:
        return lv_color_white();
    case ZMK_DEBUGGER_LOG_MESSAGE:
    default:
        return lv_color_white();
    }
}

static void refresh_widget(struct zmk_widget_debugger_log *widget) {
    for (uint8_t i = 0; i < ZMK_DEBUGGER_LOG_MAX_ENTRIES; i++) {
        if (i >= entry_count) {
            lv_obj_add_flag(widget->rows[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        zmk_heroicon_set(widget->icons[i], icon_for_type(entries[i].type),
                         color_for_type(entries[i].type));
        lv_label_set_text(widget->labels[i], entries[i].text);
        lv_obj_remove_flag(widget->rows[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_all_widgets(void) {
    struct zmk_widget_debugger_log *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        refresh_widget(widget);
    }
}

// text:max 14 chars
void zmk_debugger_log_add(enum zmk_debugger_log_type type, const char *text) {
    if (text == NULL) {
        return;
    }

    uint8_t index;
    if (entry_count < ZMK_DEBUGGER_LOG_MAX_ENTRIES) {
        index = entry_count++;
    } else {
        memmove(&entries[0], &entries[1],
                sizeof(entries[0]) * (ZMK_DEBUGGER_LOG_MAX_ENTRIES - 1));
        index = ZMK_DEBUGGER_LOG_MAX_ENTRIES - 1;
    }

    entries[index].type = type;
    strncpy(entries[index].text, text, sizeof(entries[index].text) - 1);
    entries[index].text[sizeof(entries[index].text) - 1] = '\0';

    refresh_all_widgets();
}

int zmk_widget_debugger_log_init(struct zmk_widget_debugger_log *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(widget->obj, 0, LV_PART_MAIN);
    lv_obj_remove_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = 0; i < ZMK_DEBUGGER_LOG_MAX_ENTRIES; i++) {
        widget->rows[i] = lv_obj_create(widget->obj);
        lv_obj_set_size(widget->rows[i], 212, LOG_ROW_HEIGHT);
        lv_obj_set_pos(widget->rows[i], 0, LOG_TOP_MARGIN + i * LOG_ROW_HEIGHT);
        lv_obj_set_style_bg_opa(widget->rows[i], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(widget->rows[i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(widget->rows[i], 0, LV_PART_MAIN);
        lv_obj_remove_flag(widget->rows[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(widget->rows[i], LV_OBJ_FLAG_HIDDEN);

        widget->icons[i] = lv_image_create(widget->rows[i]);
        lv_obj_set_size(widget->icons[i], LOG_ICON_SIZE, LOG_ICON_SIZE);
        lv_obj_align(widget->icons[i], LV_ALIGN_LEFT_MID, LOG_ICON_X, 0);

        widget->labels[i] = lv_label_create(widget->rows[i]);
        lv_obj_set_size(widget->labels[i], LOG_TEXT_WIDTH, LV_SIZE_CONTENT);
        lv_obj_align(widget->labels[i], LV_ALIGN_LEFT_MID, LOG_TEXT_X, 0);
        lv_label_set_long_mode(widget->labels[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(widget->labels[i], &FG_Medium_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(widget->labels[i], lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_align(widget->labels[i], LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    }

    sys_slist_append(&widgets, &widget->node);
    refresh_widget(widget);
    return 0;
}

lv_obj_t *zmk_widget_debugger_log_obj(struct zmk_widget_debugger_log *widget) {
    return widget->obj;
}
