#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ui_footer_item_t {
    const char *key;
    const char *label;
} ui_footer_item_t;

void ui_footer_draw(const ui_footer_item_t *left_items,
                    size_t left_count,
                    const ui_footer_item_t *right_items,
                    size_t right_count);

#ifdef __cplusplus
}
#endif
