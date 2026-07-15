#include "ui_widgets.h"

#include <stdio.h>
#include <string.h>

#include "graphics.h"

static void ui_draw_clipped_text(const char *text,
                                 uint32_t x,
                                 uint32_t y,
                                 uint32_t width,
                                 uint8_t color,
                                 uint8_t bgcolor) {
    if (!text || x >= TEXTMODE_COLS || y >= TEXTMODE_ROWS || width == 0)
        return;

    if (width > TEXTMODE_COLS - x)
        width = TEXTMODE_COLS - x;

    char line[TEXTMODE_COLS + 1];
    size_t len = strlen(text);
    if (len > width)
        len = width;
    memcpy(line, text, len);
    line[len] = '\0';
    draw_text(line, x, y, color, bgcolor);
}

void ui_fill_row(uint32_t y, uint8_t color, uint8_t bgcolor) {
    if (y >= TEXTMODE_ROWS)
        return;

    char line[TEXTMODE_COLS + 1];
    memset(line, ' ', TEXTMODE_COLS);
    line[TEXTMODE_COLS] = '\0';
    draw_text(line, 0, y, color, bgcolor);
}

void ui_clear_workspace(uint32_t top, uint32_t bottom, uint8_t color, uint8_t bgcolor) {
    if (bottom > TEXTMODE_ROWS)
        bottom = TEXTMODE_ROWS;
    for (uint32_t y = top; y < bottom; ++y)
        ui_fill_row(y, color, bgcolor);
}

void ui_draw_box(const char *title, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (x >= TEXTMODE_COLS || y >= TEXTMODE_ROWS || width < 2 || height < 2)
        return;
    if (width > TEXTMODE_COLS - x)
        width = TEXTMODE_COLS - x;
    if (height > TEXTMODE_ROWS - y)
        height = TEXTMODE_ROWS - y;

    char line[TEXTMODE_COLS + 1];
    const uint32_t last = width - 1;

    memset(line, 0xCD, width);
    line[0] = (char)0xC9;
    line[last] = (char)0xBB;
    line[width] = '\0';
    draw_text(line, x, y, 11, 1);

    memset(line, ' ', width);
    line[0] = (char)0xBA;
    line[last] = (char)0xBA;
    line[width] = '\0';
    for (uint32_t row = 1; row + 1 < height; ++row)
        draw_text(line, x, y + row, 11, 1);

    memset(line, 0xCD, width);
    line[0] = (char)0xC8;
    line[last] = (char)0xBC;
    line[width] = '\0';
    draw_text(line, x, y + height - 1, 11, 1);

    if (title && *title && width > 4) {
        char caption[TEXTMODE_COLS + 1];
        snprintf(caption, sizeof(caption), " %s ", title);
        size_t caption_len = strlen(caption);
        const uint32_t inner_width = width - 2;
        if (caption_len > inner_width)
            caption_len = inner_width;
        caption[caption_len] = '\0';
        ui_draw_clipped_text(caption, x + 1 + (inner_width - caption_len) / 2,
                             y, (uint32_t)caption_len, 14, 3);
    }
}

void ui_status_draw(const char *text, uint8_t color, uint8_t bgcolor) {
    const uint32_t y = TEXTMODE_ROWS - 2;
    if (TEXTMODE_COLS <= 2)
        return;

    // Columns 0 and TEXTMODE_COLS - 1 contain the bottom frame corners.
    // Preserve them and redraw only the horizontal part between the corners.
    char line[TEXTMODE_COLS - 1];
    const size_t inner_width = TEXTMODE_COLS - 2;
    memset(line, 0xCD, inner_width);
    line[inner_width] = '\0';

    if (text && *text && inner_width > 2) {
        size_t text_len = strlen(text);
        const size_t max_text_len = inner_width - 2;
        if (text_len > max_text_len)
            text_len = max_text_len;

        line[0] = '[';
        memcpy(line + 1, text, text_len);
        line[1 + text_len] = ']';
    }

    draw_text(line, 1, y, color, bgcolor);
}

void ui_status_clear(void) {
    ui_status_draw(NULL, 11, 1);
}

void ui_message_box(const char *title, const char *line1, const char *line2,
                    uint32_t width, uint8_t color, uint8_t bgcolor) {
    const uint32_t height = line2 && *line2 ? 4 : 3;
    if (width > TEXTMODE_COLS)
        width = TEXTMODE_COLS;
    if (width < 4)
        width = 4;

    const uint32_t x = (TEXTMODE_COLS - width) / 2;
    const uint32_t y = (TEXTMODE_ROWS - height) / 2;
    ui_draw_box(title, x, y, width, height);
    if (line1)
        ui_draw_clipped_text(line1, x + 1, y + 1, width - 2, color, bgcolor);
    if (line2 && *line2)
        ui_draw_clipped_text(line2, x + 1, y + 2, width - 2, color, bgcolor);
}
