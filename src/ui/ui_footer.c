#include "ui_footer.h"

#include <string.h>

#include "graphics.h"

static size_t footer_items_width(const ui_footer_item_t *items, size_t item_count) {
    size_t width = 0;
    for (size_t i = 0; i < item_count; ++i) {
        width += strlen(items[i].key ? items[i].key : "");
        width += strlen(items[i].label ? items[i].label : "");
    }
    return width;
}

static uint32_t footer_draw_items(const ui_footer_item_t *items,
                                  size_t item_count,
                                  uint32_t x,
                                  uint32_t x_limit) {
    for (size_t i = 0; i < item_count && x < x_limit; ++i) {
        const char *key = items[i].key ? items[i].key : "";
        const char *label = items[i].label ? items[i].label : "";
        size_t key_len = strlen(key);
        size_t label_len = strlen(label);

        if (key_len > x_limit - x)
            key_len = x_limit - x;
        if (key_len) {
            draw_text(key, x, TEXTMODE_ROWS - 1, 0, 7);
            x += (uint32_t)key_len;
        }

        if (x >= x_limit)
            break;

        if (label_len > x_limit - x)
            label_len = x_limit - x;
        if (label_len) {
            draw_text(label, x, TEXTMODE_ROWS - 1, 0, 3);
            x += (uint32_t)label_len;
        }
    }
    return x;
}

void ui_footer_draw(const ui_footer_item_t *left_items,
                    size_t left_count,
                    const ui_footer_item_t *right_items,
                    size_t right_count) {
    char blank[TEXTMODE_COLS + 1];
    memset(blank, ' ', TEXTMODE_COLS);
    blank[TEXTMODE_COLS] = '\0';
    draw_text(blank, 0, TEXTMODE_ROWS - 1, 0, 3);

    size_t right_width = footer_items_width(right_items, right_count);
    if (right_width > TEXTMODE_COLS)
        right_width = TEXTMODE_COLS;

    const uint32_t right_x = TEXTMODE_COLS - (uint32_t)right_width;
    footer_draw_items(left_items, left_count, 0, right_x);
    footer_draw_items(right_items, right_count, right_x, TEXTMODE_COLS);
}
