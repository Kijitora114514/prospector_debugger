#include <lvgl.h>

#include "layer_roller.h"
#include "battery_bar.h"
#include "debugger_log.h"

static struct zmk_widget_layer_roller layer_roller_widget;
static struct zmk_widget_battery_bar battery_bar_widget;
static struct zmk_widget_debugger_log debugger_log_widget;

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    zmk_widget_debugger_log_init(&debugger_log_widget, screen);
    lv_obj_set_size(zmk_widget_debugger_log_obj(&debugger_log_widget), 212, 240);
    lv_obj_align(zmk_widget_debugger_log_obj(&debugger_log_widget), LV_ALIGN_LEFT_MID, 0, 0);

    zmk_widget_battery_bar_init(&battery_bar_widget, screen);
    lv_obj_set_size(zmk_widget_battery_bar_obj(&battery_bar_widget), 64, 176);
    lv_obj_align(zmk_widget_battery_bar_obj(&battery_bar_widget), LV_ALIGN_TOP_RIGHT, -5, 0);

    zmk_widget_layer_roller_init(&layer_roller_widget, screen);
    lv_obj_set_size(zmk_widget_layer_roller_obj(&layer_roller_widget), 64, 58);
    lv_obj_align(zmk_widget_layer_roller_obj(&layer_roller_widget), LV_ALIGN_BOTTOM_RIGHT, -5, 0);

    zmk_debugger_log_add(ZMK_DEBUGGER_LOG_MESSAGE, "Good Morning.");

    return screen;
}

