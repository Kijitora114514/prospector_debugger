#include "heroicons.h"

extern const lv_image_dsc_t connect;
extern const lv_image_dsc_t disconnect;
extern const lv_image_dsc_t message;
extern const lv_image_dsc_t warning;
extern const lv_image_dsc_t error;

static const lv_image_dsc_t *get_icon(enum zmk_heroicon icon) {
    switch (icon) {
    case ZMK_HEROICON_CONNECT:
        return &connect;
    case ZMK_HEROICON_DISCONNECT:
        return &disconnect;
    case ZMK_HEROICON_MESSAGE:
        return &message;
    case ZMK_HEROICON_WARNING:
        return &warning;
    case ZMK_HEROICON_ERROR:
        return &error;
    default:
        return NULL;
    }
}

void zmk_heroicon_set(lv_obj_t *image, enum zmk_heroicon icon, lv_color_t color) {
    const lv_image_dsc_t *source = get_icon(icon);
    if (image == NULL || source == NULL) {
        return;
    }

    lv_image_set_src(image, source);
    lv_image_set_scale(image, 256); /* 24 px source at its native size */
    lv_obj_set_style_image_recolor(image, color, LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, LV_PART_MAIN);
}

