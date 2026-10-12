#pragma once

#include <lvgl.h>

enum zmk_heroicon {
    ZMK_HEROICON_CONNECT,
    ZMK_HEROICON_DISCONNECT,
};

/**
 * Set an image to one of the shared Heroicons and recolor it as a solid icon.
 * The source image's alpha channel is preserved by LVGL.
 */
void zmk_heroicon_set(lv_obj_t *image, enum zmk_heroicon icon, lv_color_t color);

