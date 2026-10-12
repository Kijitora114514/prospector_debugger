#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

#define ZMK_DEBUGGER_LOG_MAX_ENTRIES 8

enum zmk_debugger_log_type {
    ZMK_DEBUGGER_LOG_MESSAGE,
    ZMK_DEBUGGER_LOG_WARNING,
    ZMK_DEBUGGER_LOG_ERROR,
    ZMK_DEBUGGER_LOG_CONNECT,
    ZMK_DEBUGGER_LOG_DISCONNECT,
};

struct zmk_widget_debugger_log {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *rows[ZMK_DEBUGGER_LOG_MAX_ENTRIES];
    lv_obj_t *icons[ZMK_DEBUGGER_LOG_MAX_ENTRIES];
    lv_obj_t *labels[ZMK_DEBUGGER_LOG_MAX_ENTRIES];
};

int zmk_widget_debugger_log_init(struct zmk_widget_debugger_log *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_debugger_log_obj(struct zmk_widget_debugger_log *widget);

/**
 * Add a log entry. When the log is full, the oldest entry is removed.
 *
 * @param type Type used to select the icon and its color.
 * @param text Text copied into the log's internal storage.
 */
void zmk_debugger_log_add(enum zmk_debugger_log_type type, const char *text);
