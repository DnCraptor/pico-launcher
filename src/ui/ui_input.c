#include "ui_input.h"

#include <stddef.h>
#include <string.h>

#include "hardware/sync.h"

#define UI_INPUT_QUEUE_SIZE 64u
#define UI_INPUT_QUEUE_MASK (UI_INPUT_QUEUE_SIZE - 1u)

_Static_assert((UI_INPUT_QUEUE_SIZE & UI_INPUT_QUEUE_MASK) == 0,
               "UI_INPUT_QUEUE_SIZE must be a power of two");

static ui_key_event_t event_queue[UI_INPUT_QUEUE_SIZE];
static volatile uint8_t queue_head;
static volatile uint8_t queue_tail;
static uint8_t key_state[64];
static uint8_t modifiers;

static uint16_t normalize_scancode(uint32_t raw_scancode) {
    const uint16_t code = (uint16_t)(raw_scancode & 0xFFFFu);
    return (uint16_t)(code & ~0x0080u);
}

static uint16_t key_state_index(uint16_t scancode) {
    return (uint16_t)((scancode & 0xFFu) | ((scancode & 0xFF00u) ? 0x100u : 0u));
}

static void set_key_state(uint16_t scancode, bool down) {
    const uint16_t code = key_state_index(scancode);
    const uint8_t mask = (uint8_t)(1u << (code & 7u));
    if (down)
        key_state[code >> 3u] |= mask;
    else
        key_state[code >> 3u] &= (uint8_t)~mask;
}

static bool get_key_state(uint16_t scancode) {
    const uint16_t code = key_state_index(scancode);
    return (key_state[code >> 3u] & (uint8_t)(1u << (code & 7u))) != 0;
}

static void update_modifiers(uint16_t scancode, bool pressed, bool first_press) {
    switch (scancode) {
        case 0x002A:
        case 0x0036:
            if (pressed) modifiers |= UI_MOD_SHIFT;
            else if (!get_key_state(0x002A) && !get_key_state(0x0036)) modifiers &= (uint8_t)~UI_MOD_SHIFT;
            break;
        case 0x001D:
        case 0xE01D:
            if (pressed) modifiers |= UI_MOD_CTRL;
            else if (!get_key_state(0x001D) && !get_key_state(0xE01D)) modifiers &= (uint8_t)~UI_MOD_CTRL;
            break;
        case 0x0038:
        case 0xE038:
            if (pressed) modifiers |= UI_MOD_ALT;
            else if (!get_key_state(0x0038) && !get_key_state(0xE038)) modifiers &= (uint8_t)~UI_MOD_ALT;
            break;
        case 0x003A:
            if (pressed && first_press) modifiers ^= UI_MOD_CAPS;
            break;
        default:
            break;
    }
}

static char scancode_to_char(uint16_t scancode, uint8_t mods) {
    static const char plain[128] = {
        [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',[0x07]='6',[0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',
        [0x0C]='-',[0x0D]='=',[0x0E]='\b',[0x0F]='\t',
        [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',[0x15]='y',[0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',
        [0x1A]='[',[0x1B]=']',[0x1C]='\n',
        [0x1E]='a',[0x1F]='s',[0x20]='d',[0x21]='f',[0x22]='g',[0x23]='h',[0x24]='j',[0x25]='k',[0x26]='l',
        [0x27]=';',[0x28]='\'',[0x29]='`',[0x2B]='\\',
        [0x2C]='z',[0x2D]='x',[0x2E]='c',[0x2F]='v',[0x30]='b',[0x31]='n',[0x32]='m',
        [0x33]=',',[0x34]='.',[0x35]='/',[0x39]=' ',
    };
    static const char shifted[128] = {
        [0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',[0x07]='^',[0x08]='&',[0x09]='*',[0x0A]='(',[0x0B]=')',
        [0x0C]='_',[0x0D]='+',[0x0E]='\b',[0x0F]='\t',
        [0x1A]='{',[0x1B]='}',[0x1C]='\n',[0x27]=':',[0x28]='"',[0x29]='~',[0x2B]='|',
        [0x33]='<',[0x34]='>',[0x35]='?',[0x39]=' ',
    };

    if ((scancode & 0xFF00u) != 0 || scancode >= 128u)
        return 0;

    const uint8_t code = (uint8_t)scancode;
    const bool shift = (mods & UI_MOD_SHIFT) != 0;
    char c = shift && shifted[code] ? shifted[code] : plain[code];
    if (c >= 'a' && c <= 'z') {
        const bool upper = shift ^ ((mods & UI_MOD_CAPS) != 0);
        if (upper) c = (char)(c - 'a' + 'A');
    }
    if (mods & UI_MOD_CTRL) {
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 1);
        else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 1);
        else if (c >= '@' && c <= '_') c = (char)(c & 0x1F);
    }
    return c;
}

static void enqueue_event(const ui_key_event_t *event) {
    uint8_t next = (uint8_t)((queue_head + 1u) & UI_INPUT_QUEUE_MASK);
    if (next == queue_tail)
        queue_tail = (uint8_t)((queue_tail + 1u) & UI_INPUT_QUEUE_MASK);
    event_queue[queue_head] = *event;
    queue_head = next;
}

void ui_input_handle_scancode(uint32_t raw_scancode) {
    if (!raw_scancode)
        return;

    const bool pressed = ((uint8_t)raw_scancode & 0x80u) == 0;
    const uint16_t scancode = normalize_scancode(raw_scancode);
    const bool was_down = get_key_state(scancode);

    set_key_state(scancode, pressed);
    update_modifiers(scancode, pressed, pressed && !was_down);

    const ui_key_event_t event = {
        .type = pressed ? UI_KEY_PRESS : UI_KEY_RELEASE,
        .scancode = scancode,
        .modifiers = modifiers,
        .character = pressed ? scancode_to_char(scancode, modifiers) : 0,
    };
    enqueue_event(&event);
}

bool ui_input_poll(ui_key_event_t *event) {
    if (!event)
        return false;
    const uint32_t irq_state = save_and_disable_interrupts();
    if (queue_tail == queue_head) {
        restore_interrupts(irq_state);
        return false;
    }
    *event = event_queue[queue_tail];
    queue_tail = (uint8_t)((queue_tail + 1u) & UI_INPUT_QUEUE_MASK);
    restore_interrupts(irq_state);
    return true;
}

void ui_input_flush(void) {
    const uint32_t irq_state = save_and_disable_interrupts();
    queue_tail = queue_head;
    restore_interrupts(irq_state);
}

bool ui_input_key_down(uint16_t scancode) {
    const uint32_t irq_state = save_and_disable_interrupts();
    const bool down = get_key_state(scancode);
    restore_interrupts(irq_state);
    return down;
}

uint8_t ui_input_modifiers(void) {
    const uint32_t irq_state = save_and_disable_interrupts();
    const uint8_t result = modifiers;
    restore_interrupts(irq_state);
    return result;
}
