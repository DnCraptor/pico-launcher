/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "tusb.h"
#include "bsp/board_api.h"
#include "usb.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum  {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;
static bool usb_stack_initialized = false;

void led_blinking_task(void);
void cdc_task(void);

/*------------- MAIN -------------*/
inline void init_pico_usb_drive() {
    set_tud_msc_ejected(false);

    if (!usb_stack_initialized) {
        board_init();
        // Initialize the device stack only once. Re-entering USB-drive mode
        // later is a logical reconnect, not a second initialization.
        tud_init(BOARD_TUD_RHPORT);
        if (board_init_after_tusb) {
            board_init_after_tusb();
        }
        usb_stack_initialized = true;
    } else {
        // Re-enable the D+/D- pull-up so the host enumerates us again.
        tud_connect();
    }
}

inline void pico_usb_drive_heartbeat() {
    tud_task(); // tinyusb device task
    led_blinking_task();
    cdc_task();
}
 
void pico_usb_drive_finish_eject() {
    // START STOP UNIT is acknowledged asynchronously by TinyUSB. Do not
    // disconnect from inside tud_msc_start_stop_cb(), and do not stop calling
    // tud_task() immediately: the host must first receive the successful CSW.
    const uint32_t status_deadline = board_millis() + 250u;
    while ((int32_t)(board_millis() - status_deadline) < 0) {
        pico_usb_drive_heartbeat();
        sleep_ms(1);
    }

    // A safe-eject command unloads the medium, but it does not electrically
    // detach the USB device. Disable the pull-up so the PC observes a real
    // disconnect and a later tud_connect() can enumerate the drive again.
    tud_disconnect();

    const uint32_t disconnect_deadline = board_millis() + 20u;
    while ((int32_t)(board_millis() - disconnect_deadline) < 0) {
        tud_task();
        sleep_ms(1);
    }

    board_led_write(false);
    blink_interval_ms = BLINK_NOT_MOUNTED;
}

void in_flash_drive() {
  init_pico_usb_drive();
  while(!tud_msc_ejected()) {
    pico_usb_drive_heartbeat();
    //if_swap_drives();
  }
  for (int i = 0; i < 10; ++i) { // sevaral hb till end of cycle, TODO: care eject
    pico_usb_drive_heartbeat();
    sleep_ms(50);
  }
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+
// Invoked when device is mounted
void tud_mount_cb(void) {
  blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void) {
  blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en) {
  (void) remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void) {
  blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

// Invoked to determine max LUN
uint8_t tud_msc_get_maxlun_cb(void) {
  // TinyUSB expects the highest LUN index, not the LUN count.
  // This firmware exports one disk only, so the only valid LUN is 0.
  return 0;
}

//--------------------------------------------------------------------+
// USB CDC
//--------------------------------------------------------------------+
void cdc_task(void)
{
  // connected() check for DTR bit
  // Most but not all terminal client set this when making connection
  // if ( tud_cdc_connected() )
  {
    // connected and there are data available
    if ( tud_cdc_available() )
    {
      // read data
      char buf[64];
      uint32_t count = tud_cdc_read(buf, sizeof(buf));
      (void) count;

      // Echo back
      // Note: Skip echo by commenting out write() and write_flush()
      // for throughput test e.g
      //    $ dd if=/dev/zero of=/dev/ttyACM0 count=10000
      tud_cdc_write(buf, count);
      tud_cdc_write_flush();
    }
  }
}

// Invoked when cdc when line state changed e.g connected/disconnected
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
  (void) itf;
  (void) rts;

  // TODO set some indicator
  if ( dtr )
  {
    // Terminal connected
  }else
  {
    // Terminal disconnected
  }
}

// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf)
{
  (void) itf;
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
  static uint32_t start_ms = 0;
  static bool led_state = false;

  // Blink every interval ms
  if ( board_millis() - start_ms < blink_interval_ms) return; // not enough time
  start_ms += blink_interval_ms;

  board_led_write(led_state);
  led_state = 1 - led_state; // toggle
}
