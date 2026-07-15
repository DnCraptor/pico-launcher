#include <cstdlib>
#include <cstring>
#include <pico.h>
#include <hardware/vreg.h>
#include <hardware/clocks.h>
#include <hardware/flash.h>
#include <hardware/watchdog.h>
#include <pico/bootrom.h>
#include <pico/multicore.h>
#include <pico/stdlib.h>

#include "graphics.h"
#include "ui/ui_footer.h"
#include "ui/ui_widgets.h"
#include "tools/file_viewer.h"

extern "C" {
#include "ui/ui_input.h"
#include "ps2.h"
#include "usb.h"
}

#include "ff.h"
#include "nespad.h"

static FATFS fs;
semaphore vga_start_semaphore;
#define DISP_WIDTH (320)
#define DISP_HEIGHT (240)

#define ZERO_BLOCK_OFFSET ((16ul << 20) - (256ul << 10) - (4ul << 10))
#define ZERO_BLOCK_ADDRESS (XIP_BASE + ZERO_BLOCK_OFFSET)

typedef struct UF2_Block_t {
    // 32 byte header
    uint32_t magicStart0;
    uint32_t magicStart1;
    uint32_t flags;
    uint32_t targetAddr;
    uint32_t payloadSize;
    uint32_t blockNo;
    uint32_t numBlocks;
    uint32_t fileSize; // or familyID;
    uint8_t data[476];
    uint32_t magicEnd;
} UF2_Block_t;


uint16_t SCREEN[TEXTMODE_ROWS][TEXTMODE_COLS];


static uint32_t input;

extern "C" {
bool __time_critical_func(handleScancode)(const uint32_t ps2scancode) {
    if (!ps2scancode)
        return true;

    ui_input_handle_scancode(ps2scancode);
    // ps2kbd reports XT set-1 make/break codes. Extended keys keep the E0
    // prefix in the upper byte (E050/E0D0), while the launcher uses only the
    // normalized low-byte code (50). Keep input as the current key state:
    // set it on make and clear it only on the matching break.
    const uint8_t raw_scancode = (uint8_t)ps2scancode;
    const uint8_t scancode = raw_scancode & 0x7F;

    // Keep the launcher's existing held-key model while the viewer/editor use
    // the event queue. Clear only the release of the currently active key.
    if (raw_scancode & 0x80) {
        if (input == scancode)
            input = 0;
    } else {
        input = scancode;
    }

    return true;
}
}

void __time_critical_func(render_core)() {
    multicore_lockout_victim_init();
    graphics_init();

    const auto buffer = (uint8_t *)SCREEN;

    graphics_set_bgcolor(0x000000);
    graphics_set_offset(0, 0);
    graphics_set_mode(TEXTMODE_DEFAULT);
    graphics_set_buffer(buffer, TEXTMODE_COLS, TEXTMODE_ROWS);
    graphics_set_textbuffer(buffer);
    clrScr(1);

    sem_acquire_blocking(&vga_start_semaphore);
    // 60 FPS loop
#define frame_tick (16666)
    uint64_t tick = time_us_64();
#ifdef TFT
    uint64_t last_renderer_tick = tick;
#endif
    uint64_t last_input_tick = tick;
    while (true) {
#ifdef TFT
        if (tick >= last_renderer_tick + frame_tick) {
            refresh_lcd();
            last_renderer_tick = tick;
        }
#endif
        // Every 5th frame
        if (tick >= last_input_tick + frame_tick * 5) {
            nespad_read();
            last_input_tick = tick;
        }
        tick = time_us_64();

        tight_loop_contents();
    }

    __unreachable();
}

inline static uint32_t __not_in_flash_func(read_flash_block)(FIL * f, uint8_t * buffer, uint32_t expected_flash_target_offset) {
    UINT bytes_read = 0;
    UF2_Block_t uf2_block{};
    uint32_t data_sector_index = 0;
    for(; data_sector_index < FLASH_SECTOR_SIZE; data_sector_index += 256) {
        f_read(f, &uf2_block, sizeof(uf2_block), &bytes_read);
        if (!bytes_read) break;
        if (uf2_block.targetAddr == XIP_BASE + 0xFFFF00) { // ignore such block
            f_read(f, &uf2_block, sizeof(uf2_block), &bytes_read);
            if (!bytes_read) break;
        }
        if (expected_flash_target_offset != uf2_block.targetAddr - XIP_BASE) {
            f_lseek(f, f_tell(f) - sizeof(uf2_block)); // we will reread this block, it doesnt belong to this continues block
            expected_flash_target_offset = uf2_block.targetAddr - XIP_BASE;
            break;
        }
        memcpy(buffer + data_sector_index, uf2_block.data, 256);
        expected_flash_target_offset += 256;
        #ifdef PICO_DEFAULT_LED_PIN
        gpio_put(PICO_DEFAULT_LED_PIN, (expected_flash_target_offset >> 13) & 1);
        #endif
    }
    return expected_flash_target_offset;
}

static inline int __not_in_flash_func() memcmp32(const uint32_t* p1, const uint32_t* p2, size_t len) {
    len >>= 2;
    while(len--) {
        if (*p1++ != *p2++) return 1;
    }
    return 0;
}

static inline bool isExecutableOld(const char pathname[256]) {
    const char* extension = strrchr(pathname, '.');
    if (extension == nullptr) {
        return false;
    }
    extension++; // Move past the '.' character
    if (strcmp(extension, DEP_EXT) == 0) {
        return true;
    }
    return false;
}

static bool __not_in_flash_func(flash_file)(const char pathname[256]) {
    FIL file;
    #ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    #endif
    if (FR_OK == f_open(&file, pathname, FA_READ)) {
        bool e_old = isExecutableOld(pathname);
        uint32_t flash_target_offset = e_old ? (64ul << 10) : 0;

        multicore_lockout_start_blocking();
        uint32_t ints = save_and_disable_interrupts();
        bool toff = false;
        while(true) {
            uint8_t buffer[FLASH_SECTOR_SIZE] __aligned(4);
            uint32_t next_flash_target_offset = read_flash_block(&file, buffer, flash_target_offset);
            if (next_flash_target_offset == flash_target_offset) {
                break;
            }
            if (flash_target_offset == 0) {
                uint32_t *v = (uint32_t*)buffer;
                uint32_t nm = v[1023];
                v[1023] = 0x3836d91a; // MAGIC 2
                // save original block
                if (memcmp32((uint32_t*)buffer, (uint32_t*)ZERO_BLOCK_ADDRESS, FLASH_SECTOR_SIZE)) {
                    flash_range_erase(ZERO_BLOCK_OFFSET, FLASH_SECTOR_SIZE);
                    flash_range_program(ZERO_BLOCK_OFFSET, buffer, FLASH_SECTOR_SIZE);
                }
                /// patch 0x10000004 by my entry point
                v[1] = *(uint32_t*)(XIP_BASE + 4); // my Reset (any MSP, ISRx, ets. is or for me)
                v[1023] = nm; // recover original value
            }
            if (memcmp32((uint32_t*)buffer, (uint32_t*)(flash_target_offset + XIP_BASE), FLASH_SECTOR_SIZE)) {
                flash_range_erase(flash_target_offset, FLASH_SECTOR_SIZE);
                flash_range_program(flash_target_offset, buffer, FLASH_SECTOR_SIZE);
            }
            if (e_old && flash_target_offset == (64ul << 10)) {
                uint32_t *v = (uint32_t*)buffer;
                v[1023] = 0x3836d91b; // MAGIC 5
                if (memcmp32((uint32_t*)buffer, (uint32_t*)ZERO_BLOCK_ADDRESS, FLASH_SECTOR_SIZE)) {
                    flash_range_erase(ZERO_BLOCK_OFFSET, FLASH_SECTOR_SIZE);
                    flash_range_program(ZERO_BLOCK_OFFSET, buffer, FLASH_SECTOR_SIZE);
                }
            }

            flash_target_offset = next_flash_target_offset;
        }
        restore_interrupts(ints);
        multicore_lockout_end_blocking();
        #ifdef PICO_DEFAULT_LED_PIN
        gpio_put(PICO_DEFAULT_LED_PIN, false);
        #endif
        f_close(&file);

        if (f_open(&file, "/.firmware", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
            UINT br;
            f_write(&file, (char*)pathname, strlen(pathname), &br);
            f_close(&file);
        }

        *(uint32_t*)(0x20000000 + (512 << 10) - 8) = 0x383da910; // magic3
        watchdog_enable(100, true);
        while(1);
        __unreachable();
    }
    return true;
}

bool __not_in_flash_func(load_firmware)(const char pathname[256]) {
    draw_text("[ Loading... ]", 1, 0, 10, 1);
    sleep_ms(500);
    if (flash_file(pathname)) {
        draw_text("[ Unexpected target offset... ]", 1, 0, 10, 1);
        sleep_ms(5000);
        while(1);
    }
    return true;
}

typedef struct __attribute__((__packed__)) {
    bool is_directory;
    bool is_executable;
    size_t size;
    char filename[79];
} file_item_t;

constexpr int max_files = 2500;
static file_item_t fileItems[max_files];

static bool build_path(char *destination, size_t destination_size,
                       const char *basepath, const char *filename) {
    if (!destination || destination_size == 0 || !basepath || !filename)
        return false;

    const int length = snprintf(destination, destination_size, "%s\\%s", basepath, filename);
    return length >= 0 && (size_t)length < destination_size;
}

static void draw_filebrowser_chrome(const char *basepath, char *title, size_t title_size) {
    snprintf(title, title_size, "SD:\\%s", basepath);
    ui_draw_box(title, 0, 0, TEXTMODE_COLS, TEXTMODE_ROWS - 1);

    static const ui_footer_item_t footer_left[] = {
        {"Enter/START", "Run "},
        {"F3", "View "},
        {"F4", "Edit "},
        {"F8", "Del "},
    };
#ifndef HID
    static const ui_footer_item_t footer_right[] = {
        {"F10/A", "USB"},
    };
    ui_footer_draw(footer_left, sizeof(footer_left) / sizeof(footer_left[0]),
                   footer_right, sizeof(footer_right) / sizeof(footer_right[0]));
#else
    ui_footer_draw(footer_left, sizeof(footer_left) / sizeof(footer_left[0]), NULL, 0);
#endif
}

int compareFileItems(const void* a, const void* b) {
    const auto* itemA = (file_item_t *)a;
    const auto* itemB = (file_item_t *)b;
    // Directories come first
    if (itemA->is_directory && !itemB->is_directory)
        return -1;
    if (!itemA->is_directory && itemB->is_directory)
        return 1;
    // Sort files alphabetically
    return strcmp(itemA->filename, itemB->filename);
}

static inline bool isExecutable(const char pathname[256]) {
    const char* extension = strrchr(pathname, '.');
    if (extension == nullptr) {
        return false;
    }
    extension++; // Move past the '.' character
    if (strcmp(extension, "uf2") == 0) {
        return true;
    }
    if (strcmp(extension, "UF2") == 0) {
        return true;
    }
    if (strcmp(extension, DEP_EXT) == 0) {
        return true;
    }
    return isExecutableOld(pathname);
}

void __not_in_flash_func(filebrowser)() {
    bool debounce = true;
    static char basepath[256] = "";
    static char tmp[TEXTMODE_COLS + 1];
    constexpr int per_page = TEXTMODE_ROWS - 3;

    static DIR dir;
    static FILINFO fileInfo;

    if (FR_OK != f_mount(&fs, "SD", 1)) {
        draw_text("SD Card not inserted or SD Card error!", 0, 2, 12, 0);
        draw_text("Insert some FAT32 card and reboot...", 0, 3, 12, 0);
        while (true);
    }

    while (true) {
        memset(fileItems, 0, sizeof(file_item_t) * max_files);
        int total_files = 0;

        draw_filebrowser_chrome(basepath, tmp, sizeof(tmp));
        memset(tmp, ' ', TEXTMODE_COLS);

        if (FR_OK != f_opendir(&dir, basepath)) {
            draw_text("Failed to open directory", 1, 2, 4, 0);
            while (true);
        }

        if (strlen(basepath) > 0) {
            strcpy(fileItems[total_files].filename, "..\0");
            fileItems[total_files].is_directory = true;
            fileItems[total_files].size = 0;
            total_files++;
        }

        while (f_readdir(&dir, &fileInfo) == FR_OK &&
               fileInfo.fname[0] != '\0' &&
               total_files < max_files
        ) {
            // Set the file item properties
            fileItems[total_files].is_directory = fileInfo.fattrib & AM_DIR;
            fileItems[total_files].size = fileInfo.fsize;
            fileItems[total_files].is_executable = isExecutable(fileInfo.fname);
            strncpy(fileItems[total_files].filename, fileInfo.fname, sizeof(fileItems[0].filename) - 1);
            fileItems[total_files].filename[sizeof(fileItems[0].filename) - 1] = 0;
            total_files++;
        }
        f_closedir(&dir);

        qsort(fileItems, total_files, sizeof(file_item_t), compareFileItems);

        if (total_files > max_files) {
            draw_text(" Too many files!! ", TEXTMODE_COLS - 17, 0, 12, 3);
        }

        int offset = 0;
        int current_item = 0;

        while (true) {
#ifdef HID
            keyboard_task();
#endif
            sleep_ms(1);
#ifdef HID
            for (int i = 0; i < 99; ++i) {
                keyboard_task();
                sleep_ms(1);
            }
#else
            sleep_ms(99);
#endif

            bool view_requested = false;
            ui_key_event_t ui_event;
            while (ui_input_poll(&ui_event)) {
                if (ui_event.type == UI_KEY_PRESS && ui_event.scancode == 0x003D)
                    view_requested = true;
            }

            if (view_requested && total_files > 0) {
                const auto file_at_cursor = fileItems[offset + current_item];
                if (!file_at_cursor.is_directory &&
                    build_path(tmp, sizeof(tmp), basepath, file_at_cursor.filename)) {
                    input = 0;
                    file_viewer_run(tmp);
                    input = 0;
                    draw_filebrowser_chrome(basepath, tmp, sizeof(tmp));
                }
            }

            if (!debounce) {
                debounce = !(nespad_state & DPAD_START) && input != 0x1C;
            }

            // ESCAPE
            if (nespad_state & DPAD_B || input == 0x01) {
                return;
            }

#ifndef HID
            // F10 / gamepad A: expose the SD card as a USB MSC device.
            if (nespad_state & DPAD_A || input == 0x44) {
                ui_message_box("SD Cardreader mode",
                               "Mounting SD Card. Use safe eject",
                               "to continue...", 40, 13, 1);

                sleep_ms(500);

                init_pico_usb_drive();

                while (!tud_msc_ejected()) {
                    pico_usb_drive_heartbeat();
                }

                int post_cycles = 1000;
                while (--post_cycles) {
                    sleep_ms(1);
                    pico_usb_drive_heartbeat();
                }
                debounce = true;
                break;
            }
#endif

            if (nespad_state & DPAD_DOWN || input == 0x50) {
                if (offset + (current_item + 1) < total_files) {
                    if (current_item + 1 < per_page) {
                        current_item++;
                    }
                    else {
                        offset++;
                    }
                }
            }

            if (nespad_state & DPAD_UP || input == 0x48) {
                if (current_item > 0) {
                    current_item--;
                }
                else if (offset > 0) {
                    offset--;
                }
            }

            int per_page2 = per_page >> 1;

            // PageDown
            if (input == 0x51) {

                int max_visible = total_files - offset;              // сколько реально видно
                int max_cursor  = max_visible > per_page ? per_page : max_visible;

                if (current_item + per_page2 < max_cursor) {
                    // Курсор помещается в пределах текущего окна
                    current_item += per_page2;
                } else {
                    // Нужно прокрутить окно
                    int new_offset = offset + per_page2;

                    if (new_offset + per_page > total_files)
                        new_offset = total_files > per_page ? total_files - per_page : 0;

                    offset = new_offset;

                    // удерживаем курсор внутри страницы
                    if (current_item >= total_files - offset)
                        current_item = total_files - offset - 1;
                }
            }
            // PageUp
            if (input == 0x49) {

                if (current_item >= per_page2) {
                    current_item -= per_page2;
                } else {
                    int shift = per_page2 - current_item;

                    if (offset >= shift) {
                        offset -= shift;
                    } else {
                        offset = 0;
                    }

                    current_item = 0;
                }
            }
            // Home
            if (input == 0x47) {
                offset = 0;
                current_item = 0;
            }
            // End
            if (input == 0x4F) {
                if (total_files > 0) {
                    if (total_files > per_page) {
                        offset = total_files - per_page;
                        current_item = per_page - 1;
                    } else {
                        offset = 0;
                        current_item = total_files - 1;
                    }
                }
            }

            if (nespad_state & DPAD_RIGHT || input == 0x4D || input == 0x7A) {
                offset += per_page;
                if (offset + (current_item + 1) > total_files) {
                    offset = total_files - (current_item + 1);
                }
            }

            if (nespad_state & DPAD_LEFT || input == 0x4B) {
                if (offset > per_page) {
                    offset -= per_page;
                }
                else {
                    offset = 0;
                    current_item = 0;
                }
            }

            if (debounce && ((nespad_state & DPAD_START) != 0 || input == 0x1C)) {
                auto file_at_cursor = fileItems[offset + current_item];

                if (file_at_cursor.is_directory) {
                    if (strcmp(file_at_cursor.filename, "..") == 0) {
                        const char* lastBackslash = strrchr(basepath, '\\');
                        if (lastBackslash != nullptr) {
                            const size_t length = lastBackslash - basepath;
                            basepath[length] = '\0';
                        }
                    }
                    else {
                        sprintf(basepath, "%s\\%s", basepath, file_at_cursor.filename);
                    }
                    debounce = false;
                    break;
                }

                if (!build_path(tmp, sizeof(tmp), basepath, file_at_cursor.filename)) {
                    ui_message_box("File", "Path is too long", file_at_cursor.filename, 42, 12, 1);
                    sleep_ms(800);
                    draw_filebrowser_chrome(basepath, tmp, sizeof(tmp));
                } else if (file_at_cursor.is_executable) {
                    load_firmware(tmp);
                } else {
                    input = 0;
                    file_viewer_run(tmp);
                    input = 0;
                    debounce = false;
                    draw_filebrowser_chrome(basepath, tmp, sizeof(tmp));
                }
            }

            for (int i = 0; i < per_page; i++) {
                const auto item = fileItems[offset + i];
                uint8_t color = 11;
                uint8_t bg_color = 1;

                if (i == current_item) {
                    color = 0;
                    bg_color = 3;
                    snprintf(tmp, TEXTMODE_COLS, "Size: %iKb, File %lu of %i",
                             item.size / 1024, offset + i + 1, total_files);
                    ui_status_draw(tmp, 11, 1);
                }

                const auto len = strlen(item.filename);
                color = item.is_directory ? 15 : color;
                color = item.is_executable ? 10 : color;
                //color = strstr((char *)rom_filename, item.filename) != nullptr ? 13 : color;
                memset(tmp, ' ', TEXTMODE_COLS - 2);
                tmp[TEXTMODE_COLS - 2] = '\0';
                memcpy(&tmp, item.filename, len < TEXTMODE_COLS - 2 ? len : TEXTMODE_COLS - 2);
                draw_text(tmp, 1, i + 1, color, bg_color);
            }
        }
    }
}

__attribute__((constructor))
static void before_main(void) {
    if ( *(uint32_t*)(0x20000000 + (512 << 10) - 4) != 0x17F00FFF &&    // magic (enter to UI)
         *(uint32_t*)(0x20000000 + (512 << 10) - 8) == 0x383da910       // magic 3 (enter to target)
    ) {
        *(uint32_t*)(0x20000000 + (512 << 10) - 8) = 0; // cleanup magic 3 (expected to be set each time, if required)
        if (((uint32_t*)ZERO_BLOCK_ADDRESS)[1023] == 0x3836d91b) {  // magic 5
            asm volatile (
                "mov r0, %[start]\n"
                "ldr r1, =%[vtable]\n"
                "str r0, [r1]\n"
                "ldmia r0, {r0, r1}\n"
                "msr msp, r0\n"
                "bx r1\n"
                :: [start] "r" (XIP_BASE + (64ul << 10)), [vtable] "X" (PPB_BASE + M33_VTOR_OFFSET)
            );
        } else {
            // VTOR deliberately stays at 0x10000000 to preserve app IRQ vectors;
            // we only override Reset via manual jump using archived block
            asm volatile (
                "ldr r0, =%[zb_addr]\n"
                "ldmia r0, {r0, r1}\n"
                "msr msp, r0\n"
                "bx r1\n"
                :: [zb_addr] "X" (ZERO_BLOCK_ADDRESS)
            );
        }
        __unreachable();
    }
}

const uint8_t erase_blok[4096]
    __aligned(4096)
    __attribute__((section(".erase_block"), used)) = { 0 };

int main() {
    set_sys_clock_khz(252 * KHZ, 0);

    keyboard_init();
    //keyboard_send(0xFF);
    nespad_begin(clock_get_hz(clk_sys) / 1000, NES_GPIO_CLK, NES_GPIO_DATA, NES_GPIO_LAT);

    // external magic to force UI
	bool magic = *(uint32_t*)(0x20000000 + (512 << 10) - 4) == 0x17F00FFF;
    if (magic) {
        *(uint32_t*)(0x20000000 + (512 << 10) - 4) = 0;
    } else {
        // not yet flashed (internal magics)
        magic = ((uint32_t*)ZERO_BLOCK_ADDRESS)[1023] != 0x3836d91a // MAGIC 2
                && ((uint32_t*)ZERO_BLOCK_ADDRESS)[1023] != 0x3836d91b; // MAGIC 5
    }
    
    if (FR_OK == f_mount(&fs, "SD", 1)) {
        static FIL f;
        if (f_open(&f, "/.firmware", FA_READ) == FR_OK) {
            f_close(&f);
        } else {
            magic = true; // we have no SD card marker (may be removed to force pass into UI)
        }
        f_mount(0, "SD", 1); // unmount
    } // else no-sdcard mode

    for (int i = 20; i--;) {
        nespad_read();
#ifdef HID
        for (int j = 0; j < 50; ++j) {
            keyboard_task();
            sleep_ms(1);
        }
#else
        sleep_ms(50);
#endif

        // F12 Boot to USB FIRMWARE UPDATE mode
        if ((nespad_state & DPAD_START) && !(nespad_state & DPAD_SELECT) || input == 0x58) {
            reset_usb_boot(0, 0);
        }

        // Any other key/button - run launcher
        if (magic || (nespad_state && !(nespad_state & DPAD_START)) || (input && input != 0x58)) {
            sem_init(&vga_start_semaphore, 0, 1);
            multicore_launch_core1(render_core);
            sem_release(&vga_start_semaphore);
            sleep_ms(250);
            filebrowser();
        }
    }

    *(uint32_t*)(0x20000000 + (512 << 10) - 8) = 0x383da910; // magic 3
    watchdog_enable(100, true);
    while(1);
    __unreachable();
}
