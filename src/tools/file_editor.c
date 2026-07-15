#include "file_editor.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff.h"
#include "graphics.h"
#include "pico/stdlib.h"
#include "ui/ui_disk_guard.h"
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
    uint32_t capacity;
    uint32_t *line_offsets;
    uint32_t line_count;
    bool modified;
} editor_document_t;

typedef struct editor_state {
    uint32_t cursor_line;
    uint32_t cursor_column;
    uint32_t top_line;
    uint32_t left_column;
    bool overwrite_mode;
} editor_state_t;

static void editor_input_task(void) {
#ifdef HID
    keyboard_task();
#endif
}

static bool document_rebuild_lines(editor_document_t *document);

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
    document->capacity = document->size + 1u;
    document->data = (char *)malloc(document->capacity);
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

    if (!document_rebuild_lines(document)) {
        document_free(document);
        return false;
    }
    return true;
}

static bool document_rebuild_lines(editor_document_t *document) {
    uint32_t line_count = 1u;
    for (uint32_t i = 0; i < document->size; ++i) {
        if (document->data[i] == '\n')
            ++line_count;
    }

    uint32_t *offsets = (uint32_t *)realloc(
        document->line_offsets, (size_t)line_count * sizeof(document->line_offsets[0]));
    if (!offsets)
        return false;

    document->line_offsets = offsets;
    document->line_offsets[0] = 0;
    uint32_t line = 1u;
    for (uint32_t i = 0; i < document->size; ++i) {
        if (document->data[i] == '\n' && line < line_count)
            document->line_offsets[line++] = i + 1u;
    }
    document->line_count = line_count;
    return true;
}

static bool document_reserve(editor_document_t *document, uint32_t required_size) {
    if (required_size + 1u <= document->capacity)
        return true;

    uint32_t capacity = document->capacity ? document->capacity : 64u;
    while (capacity < required_size + 1u) {
        const uint32_t grown = capacity + capacity / 2u + 64u;
        if (grown <= capacity) {
            capacity = required_size + 1u;
            break;
        }
        capacity = grown;
    }

    char *data = (char *)realloc(document->data, capacity);
    if (!data)
        return false;
    document->data = data;
    document->capacity = capacity;
    return true;
}

static bool document_replace_range(editor_document_t *document, uint32_t offset,
                                   uint32_t remove_count, const char *insert,
                                   uint32_t insert_count) {
    if (offset > document->size || remove_count > document->size - offset)
        return false;

    const uint32_t new_size = document->size - remove_count + insert_count;
    if (!document_reserve(document, new_size))
        return false;

    memmove(document->data + offset + insert_count,
            document->data + offset + remove_count,
            document->size - offset - remove_count);
    if (insert_count)
        memcpy(document->data + offset, insert, insert_count);

    document->size = new_size;
    document->data[new_size] = '\0';
    if (!document_rebuild_lines(document))
        return false;
    document->modified = true;
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

static uint32_t line_offset_at_visual_column(const editor_document_t *document,
                                             uint32_t line, uint32_t target_column) {
    const uint32_t start = document->line_offsets[line];
    const uint32_t end = line_end_offset(document, line);
    uint32_t visual_column = 0;

    for (uint32_t offset = start; offset < end; ++offset) {
        const uint8_t ch = (uint8_t)document->data[offset];
        const uint32_t width = ch == '\t'
            ? EDITOR_TAB_SIZE - (visual_column % EDITOR_TAB_SIZE)
            : 1u;
        if (target_column < visual_column + width)
            return offset;
        visual_column += width;
    }
    return end;
}

static uint32_t line_visual_column_at_offset(const editor_document_t *document,
                                             uint32_t line, uint32_t target_offset) {
    const uint32_t start = document->line_offsets[line];
    const uint32_t end = line_end_offset(document, line);
    uint32_t visual_column = 0;
    if (target_offset > end)
        target_offset = end;

    for (uint32_t offset = start; offset < target_offset; ++offset) {
        if (document->data[offset] == '\t')
            visual_column += EDITOR_TAB_SIZE - (visual_column % EDITOR_TAB_SIZE);
        else
            ++visual_column;
    }
    return visual_column;
}

static bool editor_insert_character(editor_document_t *document, editor_state_t *state,
                                    char character) {
    const uint32_t offset = line_offset_at_visual_column(
        document, state->cursor_line, state->cursor_column);
    const uint32_t end = line_end_offset(document, state->cursor_line);
    uint32_t remove_count = 0;

    if (state->overwrite_mode && offset < end)
        remove_count = 1u;

    if (!document_replace_range(document, offset, remove_count, &character, 1u))
        return false;
    ++state->cursor_column;
    return true;
}

static bool editor_insert_newline(editor_document_t *document, editor_state_t *state) {
    const uint32_t offset = line_offset_at_visual_column(
        document, state->cursor_line, state->cursor_column);
    const char newline = '\n';
    if (!document_replace_range(document, offset, 0, &newline, 1u))
        return false;
    ++state->cursor_line;
    state->cursor_column = 0;
    return true;
}

static bool editor_backspace(editor_document_t *document, editor_state_t *state) {
    const uint32_t line = state->cursor_line;
    const uint32_t start = document->line_offsets[line];
    const uint32_t offset = line_offset_at_visual_column(document, line, state->cursor_column);

    if (offset > start) {
        const uint32_t remove_offset = offset - 1u;
        const uint32_t new_column = line_visual_column_at_offset(document, line, remove_offset);
        if (!document_replace_range(document, remove_offset, 1u, NULL, 0))
            return false;
        state->cursor_column = new_column;
        return true;
    }

    if (line == 0)
        return true;

    const uint32_t previous_line = line - 1u;
    const uint32_t previous_end = line_end_offset(document, previous_line);
    const uint32_t previous_column = line_visual_length(document, previous_line);
    const uint32_t remove_count = start - previous_end;
    if (!document_replace_range(document, previous_end, remove_count, NULL, 0))
        return false;
    state->cursor_line = previous_line;
    state->cursor_column = previous_column;
    return true;
}

static bool editor_delete(editor_document_t *document, editor_state_t *state) {
    const uint32_t line = state->cursor_line;
    const uint32_t offset = line_offset_at_visual_column(document, line, state->cursor_column);
    const uint32_t end = line_end_offset(document, line);

    if (offset < end)
        return document_replace_range(document, offset, 1u, NULL, 0);

    if (line + 1u >= document->line_count)
        return true;

    const uint32_t next_start = document->line_offsets[line + 1u];
    return document_replace_range(document, end, next_start - end, NULL, 0);
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
        draw_text(cursor, cursor_x, cursor_y, 0, 7);
    }

    char status[TEXTMODE_COLS + 1u];
    snprintf(status, sizeof(status), "Line: %lu/%lu  Col: %lu%s",
             (unsigned long)(state->cursor_line + 1u),
             (unsigned long)document->line_count,
             (unsigned long)(state->cursor_column + 1u),
             document->modified ? "  Modified" : "");
    ui_status_draw(status, 11, 1);
    draw_text(state->overwrite_mode ? "OVR" : "INS",
              TEXTMODE_COLS - 4u, EDITOR_STATUS_ROW, 15, 1);

    static const ui_footer_item_t footer[] = {
        {"F2", "Save "},
        {"F10/Esc", "Exit"},
    };
    ui_footer_draw(footer, sizeof(footer) / sizeof(footer[0]), NULL, 0);
}


static bool editor_confirm(const char *title, const char *message,
                           const char *yes_label, const char *no_label) {
    const uint32_t width = TEXTMODE_COLS < 53u ? TEXTMODE_COLS - 4u : 49u;
    const uint32_t x = (TEXTMODE_COLS - width) / 2u;
    const uint32_t y = (TEXTMODE_ROWS - 5u) / 2u;

    ui_draw_box(title, x, y, width, 5u);
    draw_text(message, x + 2u, y + 2u, 15, 1);

    ui_footer_item_t footer[] = {
        {"Enter/Y", yes_label},
        {"Esc/N", no_label},
    };
    ui_footer_draw(footer, 2u, NULL, 0);

    ui_input_flush();
    for (;;) {
        editor_input_task();
        ui_key_event_t event;
        while (ui_input_poll(&event)) {
            if (event.type != UI_KEY_PRESS)
                continue;
            if (event.scancode == 0x001C || event.scancode == 0xE01C ||
                event.scancode == 0x0015)
                return true;
            if (event.scancode == 0x0001 || event.scancode == 0x0031)
                return false;
        }
        sleep_ms(1);
    }
}

static bool editor_make_aux_path(const char *path, const char *suffix,
                                 char *output, size_t output_size) {
    const int written = snprintf(output, output_size, "%s%s", path, suffix);
    return written > 0 && (size_t)written < output_size;
}

static bool document_save(editor_document_t *document, const char *path) {
    char temporary[320];
    char backup[320];
    if (!editor_make_aux_path(path, ".tmp", temporary, sizeof(temporary)) ||
        !editor_make_aux_path(path, ".bak", backup, sizeof(backup)))
        return false;

    f_unlink(temporary);
    f_unlink(backup);

    FIL file;
    if (f_open(&file, temporary, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
        return false;

    uint32_t offset = 0;
    bool success = true;
    while (offset < document->size) {
        UINT written = 0;
        const UINT chunk = (UINT)(document->size - offset);
        if (f_write(&file, document->data + offset, chunk, &written) != FR_OK ||
            written == 0) {
            success = false;
            break;
        }
        offset += written;
    }
    if (success && f_sync(&file) != FR_OK)
        success = false;
    if (f_close(&file) != FR_OK)
        success = false;

    if (!success) {
        f_unlink(temporary);
        return false;
    }

    if (f_rename(path, backup) != FR_OK) {
        f_unlink(temporary);
        return false;
    }
    if (f_rename(temporary, path) != FR_OK) {
        f_rename(backup, path);
        f_unlink(temporary);
        return false;
    }

    f_unlink(backup);
    document->modified = false;
    return true;
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

            if (event.scancode == 0x003C) {
                if (!document.modified ||
                    editor_confirm("Save", "Write changes to file?", "Save ", "Cancel")) {
                    if (document.modified) {
                        ui_disk_guard_begin("Saving file...");
                        const bool saved = document_save(&document, path);
                        ui_disk_guard_end();
                        if (!saved) {
                            ui_message_box("Save error", "Unable to save file", path, 44, 12, 1);
                            sleep_ms(1000);
                        }
                    }
                }
                redraw = true;
                ui_input_flush();
                break;
            }

            if (event.scancode == 0x0001 || event.scancode == 0x0044) {
                if (!document.modified ||
                    editor_confirm("Unsaved changes",
                                   "Changes will be lost. Exit?",
                                   "Discard ", "Cancel")) {
                    running = false;
                }
                redraw = true;
                ui_input_flush();
                break;
            }

            if (event.scancode == 0x0052 || event.scancode == 0xE052) {
                state.overwrite_mode = !state.overwrite_mode;
            } else if (event.scancode == 0x001C || event.scancode == 0xE01C) {
                if (!editor_insert_newline(&document, &state))
                    running = false;
            } else if (event.scancode == 0x000E) {
                if (!editor_backspace(&document, &state))
                    running = false;
            } else if (event.scancode == 0x0053 || event.scancode == 0xE053) {
                if (!editor_delete(&document, &state))
                    running = false;
            } else if (event.scancode == 0x000F) {
                const char tab = '\t';
                const uint32_t next_tab = EDITOR_TAB_SIZE -
                    (state.cursor_column % EDITOR_TAB_SIZE);
                if (!editor_insert_character(&document, &state, tab))
                    running = false;
                else
                    state.cursor_column += next_tab - 1u;
            } else if (event.character >= 32 && !(event.modifiers & UI_MOD_CTRL)) {
                if (!editor_insert_character(&document, &state, event.character))
                    running = false;
            } else if ((event.modifiers & UI_MOD_CTRL) &&
                       scancode_is(event.scancode, 0x0047, 0xE047)) {
                state.cursor_line = 0;
                state.cursor_column = 0;
            } else if ((event.modifiers & UI_MOD_CTRL) &&
                       scancode_is(event.scancode, 0x004F, 0xE04F)) {
                state.cursor_line = document.line_count - 1u;
                state.cursor_column = line_visual_length(&document, state.cursor_line);
            } else if (scancode_is(event.scancode, 0x0048, 0xE048)) {
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
