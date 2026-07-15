#ifndef UI_INPUT_H
#define UI_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ui_key_event_type {
    UI_KEY_PRESS = 1,
    UI_KEY_RELEASE = 2,
} ui_key_event_type_t;

enum {
    UI_MOD_SHIFT = 1u << 0,
    UI_MOD_CTRL  = 1u << 1,
    UI_MOD_ALT   = 1u << 2,
    UI_MOD_CAPS  = 1u << 3,
};

typedef struct ui_key_event {
    ui_key_event_type_t type;
    uint16_t scancode;
    uint8_t modifiers;
    char character;
} ui_key_event_t;

void ui_input_handle_scancode(uint32_t raw_scancode);
bool ui_input_poll(ui_key_event_t *event);
void ui_input_flush(void);
bool ui_input_key_down(uint16_t scancode);
uint8_t ui_input_modifiers(void);

#ifdef __cplusplus
}
#endif

#endif
