#include "ui_disk_guard.h"

#include <stdbool.h>

#include "pico/stdlib.h"
#include "ui_input.h"
#include "ui_widgets.h"

static unsigned int guard_depth;

static void disk_led_set(bool active) {
#ifdef PICO_DEFAULT_LED_PIN
#if PICO_DEFAULT_LED_PIN_INVERTED
    gpio_put(PICO_DEFAULT_LED_PIN, active ? 0 : 1);
#else
    gpio_put(PICO_DEFAULT_LED_PIN, active ? 1 : 0);
#endif
#else
    (void)active;
#endif
}

void ui_disk_guard_begin(const char *operation) {
    if (guard_depth++ != 0u)
        return;

    ui_input_set_blocked(true);
    disk_led_set(true);
    ui_message_box("In progress",
                   operation ? operation : "Disk operation...",
                   "Please wait", 36u, 15u, 1u);
}

void ui_disk_guard_end(void) {
    if (guard_depth == 0u)
        return;

    --guard_depth;
    if (guard_depth != 0u)
        return;

    disk_led_set(false);
    ui_input_set_blocked(false);
    ui_input_flush();
}
