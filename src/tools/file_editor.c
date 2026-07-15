#include "file_editor.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

#define EDITOR_FIRST_ROW 1u
#define EDITOR_STATUS_ROW (TEXTMODE_ROWS - 2u)
#define EDITOR_VISIBLE_ROWS (TEXTMODE_ROWS - 3u)
#define EDITOR_TEXT_WIDTH (TEXTMODE_COLS - 2u)
#define EDITOR_TAB_SIZE 4u

typedef struct editor_document {
    char *data;
    uint32_t size;
    uint32_t *line_offsets;
    uint32_t line_count;
} editor_document_t;

typedef struct editor_state {
    uint32_t cursor_line;
    uint32_t cursor_column;
    uint32_t top_line;
    uint32_t left_column;
} editor_state_t;

static void editor_input_task(void) {
#ifdef HID
    keyboard_task();
#endif
}

static void document_free(editor_document_t *document) {
    if (!document)
        return;
    free(document->line_offsets);
    free(document->data);
    memset(document, 0, sizeof(*document));
}

static bool document_load(editor_document_t *document, const char *path) {
    if (!document || !path)
        return false;

    memset(document, 0, sizeof(*document));

    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK)
        return false;

    const FSIZE_t file_size = f_size(&file);
    if (file_size > UINT32_MAX) {
        f_close(&file);
        return false;
    }

    document->size = (uint32_t)file_size;
    document->data = (char *)malloc((size_t)document->size + 1u);
    if (!document->data) {
        f_close(&file);
        return false;
    }

    UINT total_read = 0;
    while (total_read < document->size) {
        UINT chunk_read = 0;
        const UINT remaining = document->size - total_read;
        if (f_read(&file, document->data + total_read, remaining, &chunk_read) != FR_OK || chunk_read == 0) {
            f_close(&file);
            document_free(document);
            return false;
        }
        total_read += chunk_read;
    }
    f_close(&file);
    document->data[document->size] = '\0';

    uint32_t line_count = 1;
    for (uint32_t i = 0; i < document->size; ++i) {
        if (document->data[i] == '\n')
            ++line_count;
    }

    document->line_offsets = (uint32_t *)malloc((size_t)line_count * sizeof(document->line_offsets[0]));
    if (!document->line_offsets) {
        document_free(document);
        return false;
    }

    document->line_offsets[0] = 0;
    uint32_t line = 1;
    for (uint32_t i = 0; i < document->size; ++i) {
        if (document->data[i] == '\n' && line < line_count)
            document->line_offsets[line++] = i + 1u;
    }
    document->line_count = line_count;
    return true;
}

static uint32_t line_end_offset(const editor_document_t *document, uint32_t line) {
    uint32_t end = document->size;
    if (line + 1u < document->line_count)
        end = document->line_offsets[line + 1u];
    while (end > document->line_offsets[line] &&
           (document->data[end - 1u] == '\n' || document->data[end - 1u] == '\r'))
        --end;
    return end;
}

static uint32_t line_visual_length(const editor_document_t *document, uint32_t line) {
    const uint32_t start = document->line_offsets[line];
    const uint32_t end = line_end_offset(document, line);
    uint32_t column = 0;
    for (uint32_t offset = start; offset < end; ++offset) {
        if (document->data[offset] == '\t')
            column += EDITOR_TAB_SIZE - (column % EDITOR_TAB_SIZE);
        else
            ++column;
    }
    return column;
}

static char line_character_at(const editor_document_t *document, uint32_t line, uint32_t target_column) {
    const uint32_t start = document->line_offsets[line];
    const uint32_t end = line_end_offset(document, line);
    uint32_t visual_column = 0;

    for (uint32_t offset = start; offset < end; ++offset) {
        const uint8_t ch = (uint8_t)document->data[offset];
        const uint32_t width = ch == '\t'
            ? EDITOR_TAB_SIZE - (visual_column % EDITOR_TAB_SIZE)
            : 1u;
        if (target_column >= visual_column && target_column < visual_column + width)
            return ch == '\t' ? ' ' : (ch >= 32u ? (char)ch : '.');
        visual_column += width;
    }
    return ' ';
}

static void ensure_cursor_visible(editor_state_t *state) {
    if (state->cursor_line < state->top_line)
        state->top_line = state->cursor_line;
    else if (state->cursor_line >= state->top_line + EDITOR_VISIBLE_ROWS)
        state->top_line = state->cursor_line - EDITOR_VISIBLE_ROWS + 1u;

    if (state->cursor_column < state->left_column)
        state->left_column = state->cursor_column;
    else if (state->cursor_column >= state->left_column + EDITOR_TEXT_WIDTH)
        state->left_column = state->cursor_column - EDITOR_TEXT_WIDTH + 1u;
}

static void draw_document_line(const editor_document_t *document, uint32_t line_index,
                               uint32_t left_column, uint32_t screen_row) {
    char output[TEXTMODE_COLS - 1u];
    memset(output, ' ', EDITOR_TEXT_WIDTH);
    output[EDITOR_TEXT_WIDTH] = '\0';

    if (line_index < document->line_count) {
        const uint32_t start = document->line_offsets[line_index];
        const uint32_t end = line_end_offset(document, line_index);
        uint32_t visual_column = 0;

        for (uint32_t offset = start; offset < end; ++offset) {
            const uint8_t ch = (uint8_t)document->data[offset];
            const uint32_t width = ch == '\t'
                ? EDITOR_TAB_SIZE - (visual_column % EDITOR_TAB_SIZE)
                : 1u;

            for (uint32_t i = 0; i < width; ++i, ++visual_column) {
                if (visual_column >= left_column &&
                    visual_column < left_column + EDITOR_TEXT_WIDTH) {
                    output[visual_column - left_column] = ch == '\t'
                        ? ' '
                        : (ch >= 32u ? (char)ch : '.');
                }
            }
        }
    }

    draw_text(output, 1, screen_row, 11, 1);
}

static void draw_editor(const editor_document_t *document, const editor_state_t *state,
                        const char *path) {
    ui_draw_box(path, 0, 0, TEXTMODE_COLS, TEXTMODE_ROWS - 1u);

    for (uint32_t row = 0; row < EDITOR_VISIBLE_ROWS; ++row)
        draw_document_line(document, state->top_line + row, state->left_column,
                           EDITOR_FIRST_ROW + row);

    const uint32_t cursor_x = state->cursor_column - state->left_column + 1u;
    const uint32_t cursor_y = state->cursor_line - state->top_line + EDITOR_FIRST_ROW;
    if (cursor_x < TEXTMODE_COLS - 1u && cursor_y < EDITOR_STATUS_ROW) {
        char cursor[2] = {
            line_character_at(document, state->cursor_line, state->cursor_column),
            '\0'
        };
        draw_text(cursor, cursor_x, cursor_y, 0, 3);
    }

    char status[TEXTMODE_COLS + 1u];
    snprintf(status, sizeof(status), "Line: %lu/%lu  Col: %lu  Read only",
             (unsigned long)(state->cursor_line + 1u),
             (unsigned long)document->line_count,
             (unsigned long)(state->cursor_column + 1u));
    ui_status_draw(status, 11, 1);

    static const ui_footer_item_t footer[] = {
        {"F10/Esc", "Exit"},
    };
    ui_footer_draw(footer, sizeof(footer) / sizeof(footer[0]), NULL, 0);
}

static bool scancode_is(uint16_t scancode, uint16_t normalized, uint16_t extended) {
    return scancode == normalized || scancode == extended;
}

bool file_editor_run(const char *path) {
    if (!path || !*path)
        return false;

    editor_document_t document;
    if (!document_load(&document, path)) {
        ui_message_box("Editor", "Unable to load file", path, 42, 12, 1);
        sleep_ms(800);
        return false;
    }

    editor_state_t state = {0};
    bool running = true;
    bool redraw = true;

    ui_input_flush();
    while (running) {
        editor_input_task();

        ui_key_event_t event;
        while (ui_input_poll(&event)) {
            if (event.type != UI_KEY_PRESS)
                continue;

            if (event.scancode == 0x0001 || event.scancode == 0x0044) {
                running = false;
                break;
            }

            if (scancode_is(event.scancode, 0x0048, 0xE048)) {
                if (state.cursor_line > 0)
                    --state.cursor_line;
            } else if (scancode_is(event.scancode, 0x0050, 0xE050)) {
                if (state.cursor_line + 1u < document.line_count)
                    ++state.cursor_line;
            } else if (scancode_is(event.scancode, 0x0049, 0xE049)) {
                const uint32_t count = EDITOR_VISIBLE_ROWS;
                state.cursor_line = state.cursor_line > count ? state.cursor_line - count : 0;
            } else if (scancode_is(event.scancode, 0x0051, 0xE051)) {
                const uint32_t last_line = document.line_count - 1u;
                state.cursor_line = state.cursor_line + EDITOR_VISIBLE_ROWS < last_line
                    ? state.cursor_line + EDITOR_VISIBLE_ROWS
                    : last_line;
            } else if (scancode_is(event.scancode, 0x0047, 0xE047)) {
                state.cursor_column = 0;
            } else if (scancode_is(event.scancode, 0x004F, 0xE04F)) {
                state.cursor_column = line_visual_length(&document, state.cursor_line);
            } else if (scancode_is(event.scancode, 0x004B, 0xE04B)) {
                if (state.cursor_column > 0)
                    --state.cursor_column;
            } else if (scancode_is(event.scancode, 0x004D, 0xE04D)) {
                const uint32_t line_length = line_visual_length(&document, state.cursor_line);
                if (state.cursor_column < line_length)
                    ++state.cursor_column;
            } else {
                continue;
            }

            const uint32_t line_length = line_visual_length(&document, state.cursor_line);
            if (state.cursor_column > line_length)
                state.cursor_column = line_length;
            ensure_cursor_visible(&state);
            redraw = true;
        }

        if (redraw) {
            draw_editor(&document, &state, path);
            redraw = false;
        }
        sleep_ms(1);
    }

    document_free(&document);
    ui_input_flush();
    return true;
}
