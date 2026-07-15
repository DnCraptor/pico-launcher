#include "file_viewer.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "graphics.h"
#include "pico/stdlib.h"
#include "ui/ui_footer.h"
#include "ui/ui_input.h"
#include "ui/ui_widgets.h"

#ifdef HID
#include "ps2.h"
#endif

#define VIEWER_FIRST_ROW 1u
#define VIEWER_STATUS_ROW (TEXTMODE_ROWS - 2u)
#define VIEWER_VISIBLE_ROWS (TEXTMODE_ROWS - 3u)
#define VIEWER_TEXT_WIDTH (TEXTMODE_COLS - 2u)

static bool read_byte(FIL *file, FSIZE_t offset, uint8_t *value) {
    UINT read_count = 0;
    if (f_lseek(file, offset) != FR_OK)
        return false;
    if (f_read(file, value, 1, &read_count) != FR_OK)
        return false;
    return read_count == 1;
}

static FSIZE_t next_line_offset(FIL *file, FSIZE_t offset, FSIZE_t size) {
    if (f_lseek(file, offset) != FR_OK)
        return size;

    uint8_t ch;
    UINT read_count;
    while (offset < size) {
        read_count = 0;
        if (f_read(file, &ch, 1, &read_count) != FR_OK || read_count != 1)
            return size;
        ++offset;
        if (ch == '\n')
            break;
    }
    return offset;
}

static FSIZE_t previous_line_offset(FIL *file, FSIZE_t offset) {
    uint8_t ch;
    if (offset == 0)
        return 0;

    --offset;
    if (offset > 0 && read_byte(file, offset, &ch) && ch == '\n')
        --offset;

    while (offset > 0) {
        if (!read_byte(file, offset - 1u, &ch))
            return 0;
        if (ch == '\n')
            break;
        --offset;
    }
    return offset;
}

static FSIZE_t move_lines_down(FIL *file, FSIZE_t offset, FSIZE_t size, uint32_t count) {
    while (count-- && offset < size)
        offset = next_line_offset(file, offset, size);
    return offset;
}

static FSIZE_t move_lines_up(FIL *file, FSIZE_t offset, uint32_t count) {
    while (count-- && offset > 0)
        offset = previous_line_offset(file, offset);
    return offset;
}

static FSIZE_t last_page_offset(FIL *file, FSIZE_t size) {
    FSIZE_t offset = size;
    return move_lines_up(file, offset, VIEWER_VISIBLE_ROWS);
}

static void draw_line(FIL *file, FSIZE_t *offset, FSIZE_t size, uint32_t row) {
    char line[TEXTMODE_COLS - 1u];
    uint32_t column = 0;
    uint8_t ch = 0;
    UINT read_count = 0;

    memset(line, ' ', VIEWER_TEXT_WIDTH);
    line[VIEWER_TEXT_WIDTH] = '\0';

    if (f_lseek(file, *offset) == FR_OK) {
        while (*offset < size) {
            read_count = 0;
            if (f_read(file, &ch, 1, &read_count) != FR_OK || read_count != 1)
                break;
            ++*offset;
            if (ch == '\n')
                break;
            if (ch == '\r')
                continue;
            if (ch == '\t') {
                const uint32_t spaces = 4u - (column & 3u);
                for (uint32_t i = 0; i < spaces && column < VIEWER_TEXT_WIDTH; ++i)
                    line[column++] = ' ';
                continue;
            }
            if (column < VIEWER_TEXT_WIDTH)
                line[column++] = ch >= 32u ? (char)ch : '.';
        }
    }

    draw_text(line, 1, row, 11, 1);
}

static void draw_viewer(FIL *file, const char *path, FSIZE_t top_offset, FSIZE_t size) {
    ui_draw_box(path, 0, 0, TEXTMODE_COLS, TEXTMODE_ROWS - 1u);

    FSIZE_t offset = top_offset;
    for (uint32_t row = 0; row < VIEWER_VISIBLE_ROWS; ++row)
        draw_line(file, &offset, size, VIEWER_FIRST_ROW + row);

    char status[TEXTMODE_COLS + 1u];
    const uint32_t percent = size ? (uint32_t)((top_offset * 100u) / size) : 100u;
    snprintf(status, sizeof(status), "Offset: %lu/%lu  %lu%%",
             (unsigned long)top_offset, (unsigned long)size, (unsigned long)percent);
    ui_status_draw(status, 11, 1);

    static const ui_footer_item_t footer[] = {
        {"F10/Esc", "Exit"},
    };
    ui_footer_draw(footer, sizeof(footer) / sizeof(footer[0]), NULL, 0);
}

static void viewer_input_task(void) {
#ifdef HID
    keyboard_task();
#endif
}

bool file_viewer_run(const char *path) {
    if (!path || !*path)
        return false;

    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK) {
        ui_message_box("Viewer", "Unable to open file", path, 42, 12, 1);
        sleep_ms(800);
        return false;
    }

    const FSIZE_t size = f_size(&file);
    FSIZE_t top_offset = 0;
    bool redraw = true;
    bool running = true;

    ui_input_flush();
    while (running) {
        viewer_input_task();

        if (redraw) {
            draw_viewer(&file, path, top_offset, size);
            redraw = false;
        }

        ui_key_event_t event;
        while (ui_input_poll(&event)) {
            if (event.type != UI_KEY_PRESS)
                continue;

            switch (event.scancode) {
                case 0x0001: // Esc
                case 0x0044: // F10
                    running = false;
                    break;
                case 0x0048: // Up, normalized
                case 0xE048: // Up
                    top_offset = move_lines_up(&file, top_offset, 1);
                    redraw = true;
                    break;
                case 0x0050: // Down, normalized
                case 0xE050: // Down
                    if (top_offset < size)
                        top_offset = move_lines_down(&file, top_offset, size, 1);
                    redraw = true;
                    break;
                case 0x0049: // Page Up, normalized
                case 0xE049: // Page Up
                    top_offset = move_lines_up(&file, top_offset, VIEWER_VISIBLE_ROWS);
                    redraw = true;
                    break;
                case 0x0051: // Page Down, normalized
                case 0xE051: // Page Down
                    top_offset = move_lines_down(&file, top_offset, size, VIEWER_VISIBLE_ROWS);
                    redraw = true;
                    break;
                case 0x0047: // Home, normalized
                case 0xE047: // Home
                    top_offset = 0;
                    redraw = true;
                    break;
                case 0x004F: // End, normalized
                case 0xE04F: // End
                    top_offset = last_page_offset(&file, size);
                    redraw = true;
                    break;
                default:
                    break;
            }
        }

        sleep_ms(1);
    }

    f_close(&file);
    ui_input_flush();
    return true;
}
