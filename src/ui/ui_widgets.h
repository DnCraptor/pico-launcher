#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_fill_row(uint32_t y, uint8_t color, uint8_t bgcolor);
void ui_clear_workspace(uint32_t top, uint32_t bottom, uint8_t color, uint8_t bgcolor);
void ui_draw_box(const char *title, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
void ui_status_draw(const char *text, uint8_t color, uint8_t bgcolor);
void ui_status_clear(void);
void ui_message_box(const char *title, const char *line1, const char *line2,
                    uint32_t width, uint8_t color, uint8_t bgcolor);

#ifdef __cplusplus
}
#endif
