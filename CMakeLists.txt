# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 13_3_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.22)

option(ZERO2 "Waveshare RP2350 Pi Zero" ON)

if (ZERO2)
    set(PICO_BOARD_HEADER_DIRS ${CMAKE_SOURCE_DIR}/src/boards)
    set(PICO_BOARD waveshare_rp2350_pizero)
    set(PICO_PLATFORM rp2350-arm-s)
endif()

include(pico_sdk_import.cmake)

project(launcher C CXX ASM)
pico_sdk_init()

option(M2 "Murmulator 2.0" OFF)
option(VGA "Enable VGA" OFF)
option(TFT "Enable TFT display" OFF)
option(ILI9341 "Enable TFT ILI9341 display" OFF)
option(HDMI "Enable HDMI display" ON)
option(TV "Enable TV composite output" OFF)
option(SOFTTV "Enable TV soft composite output" OFF)
option(HID "Enable USB HID host (USB drive mode is unavailable)" ON)

option(FLASH_SIZE "Target Flash Size" 4096)

if(NOT FLASH_SIZE)
set(FLASH_SIZE 4096)
endif ()

if(NOT LAUNCHER_VERSION)
set(LAUNCHER_VERSION 4)
endif ()

if (ZERO2)
    SET(BUILD_NAME "z0p2-uf2-${PROJECT_NAME}")
elseif (M2)
    SET(BUILD_NAME "m2p2-uf2-${PROJECT_NAME}")
else()
    SET(BUILD_NAME "m1p2-uf2-${PROJECT_NAME}")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

if (HID)
    add_subdirectory(drivers/ps2kbd)
else()
    add_subdirectory(drivers/ps2)
endif()
add_subdirectory(drivers/fatfs)
add_subdirectory(drivers/sdcard)
add_subdirectory(drivers/nespad)

add_subdirectory(drivers/vga-nextgen)
add_subdirectory(drivers/st7789)
add_subdirectory(drivers/hdmi)
add_subdirectory(drivers/tv)
add_subdirectory(drivers/tv-software)
add_subdirectory(drivers/graphics)

# INCLUDE FILES THAT SHOULD BE COMPILED:
if (HID)
    file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c" "drivers/usbhost/*.c" "drivers/usbhost/*.cpp")
else()
    file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c" "drivers/usb/*.c")
endif()

message(STATUS "Add source files:")
foreach (SRC_FILE IN LISTS SRC)
    message(STATUS "${SRC_FILE}")
endforeach ()
message(STATUS "")

add_executable(${PROJECT_NAME} ${SRC})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if (HID)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/drivers/usbhost)
endif()

configure_file(memmap.ld.in memmap.ld @ONLY)
pico_set_linker_script(${PROJECT_NAME} ${CMAKE_CURRENT_BINARY_DIR}/memmap.ld)

pico_set_program_name(${PROJECT_NAME} "RetroPico Launcher")
pico_set_program_version(${PROJECT_NAME} "test")

target_link_libraries(${PROJECT_NAME} PRIVATE
        sdcard
        fatfs
        nespad

        graphics

        pico_runtime
        pico_stdlib
        hardware_pio

        pico_multicore
        hardware_flash

        tinyusb_board
)
if (HID)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HID=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE ps2kbd tinyusb_host)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE ps2 tinyusb_device)
endif()

#family_configure_device_example(${PROJECT_NAME} noos)
target_link_options(${PROJECT_NAME} PRIVATE -Xlinker --print-memory-usage)
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
if (HID)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/drivers/usbhost
        ${CMAKE_CURRENT_SOURCE_DIR}/drivers/usb
    )
else()
    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/drivers/usb)
endif()

if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
target_compile_options(${PROJECT_NAME} PUBLIC
        -Wno-error=suggest-attribute=format
        -Wno-error=cast-qual
        -Wno-error=unused-parameter
        -Wno-error=conversion
        -Wno-error=format=
        -Wno-error=sign-compare
        -Wno-error=missing-field-initializers
        -Wno-error=switch
        -Wno-error=implicit-fallthrough=
        -Wno-error=stringop-truncation
        -Wno-error=restrict
        -w
)
endif ()

if (ZERO2)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        ZERO2=1
        PICO_FLASH_SIZE_BYTES=16777216

        # VGA 8 pins starts from pin:
        VGA_BASE_PIN=32

        # HDMI 8 pins starts from pin:
        HDMI_BASE_PIN=32

        # TFT
        TFT_CS_PIN=12
        TFT_RST_PIN=14
        TFT_LED_PIN=15
        TFT_DC_PIN=16
        TFT_DATA_PIN=18
        TFT_CLK_PIN=19

        # SDCARD
        SDCARD_SPI_BUS=spi1
        SDCARD_PIN_SPI0_SCK=30
        SDCARD_PIN_SPI0_MOSI=31
        SDCARD_PIN_SPI0_MISO=40
        SDCARD_PIN_SPI0_CS=43

        # PS2 keyboard
        KBD_CLOCK_PIN=2
        KBD_DATA_PIN=3

        # NES Gamepad
        NES_GPIO_CLK=4
        NES_GPIO_LAT=5
        NES_GPIO_DATA=7

        DEP_EXT="z0p2"
    )
elseif (M2)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        PICO_FLASH_SIZE_BYTES=16777216

        # VGA 8 pins starts from pin:
        VGA_BASE_PIN=12

        # HDMI 8 pins starts from pin:
        HDMI_BASE_PIN=12

        # TFT
        TFT_CS_PIN=12
        TFT_RST_PIN=14
        TFT_LED_PIN=15
        TFT_DC_PIN=16
        TFT_DATA_PIN=18
        TFT_CLK_PIN=19

        # SDCARD
        SDCARD_PIN_SPI0_CS=5
        SDCARD_PIN_SPI0_SCK=6
        SDCARD_PIN_SPI0_MOSI=7
        SDCARD_PIN_SPI0_MISO=4

        # PS2 keyboard
        KBD_CLOCK_PIN=2
        KBD_DATA_PIN=3

        # NES Gamepad
        NES_GPIO_CLK=20
        NES_GPIO_LAT=21
        NES_GPIO_DATA=26

        DEP_EXT="m2p2"
    )
else ()
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        PICO_FLASH_SIZE_BYTES=16777216

        # VGA 8 pins starts from pin:
        VGA_BASE_PIN=6

        # HDMI 8 pins starts from pin:
        HDMI_BASE_PIN=6

        # TFT
        TFT_CS_PIN=6
        TFT_RST_PIN=8
        TFT_LED_PIN=9
        TFT_DC_PIN=10
        TFT_DATA_PIN=12
        TFT_CLK_PIN=13

        # SDCARD
        SDCARD_PIN_SPI0_SCK=2
        SDCARD_PIN_SPI0_MOSI=3
        SDCARD_PIN_SPI0_MISO=4
        SDCARD_PIN_SPI0_CS=5

        # PS2 keyboard
        KBD_CLOCK_PIN=0
        KBD_DATA_PIN=1

        # NES Gamepad
        NES_GPIO_CLK=14
        NES_GPIO_LAT=15
        NES_GPIO_DATA=16

        DEP_EXT="m1p2"
    )
endif ()

target_compile_definitions(${PROJECT_NAME} PRIVATE FLASH_SIZE=${FLASH_SIZE})

IF (TFT)
    target_link_libraries(${PROJECT_NAME} PRIVATE st7789)
    target_compile_definitions(${PROJECT_NAME} PRIVATE TFT)
    SET(BUILD_NAME "${BUILD_NAME}-TFT")
    IF (ILI9341)
        SET(BUILD_NAME "${BUILD_NAME}-ILI9341")
        target_compile_definitions(${PROJECT_NAME} PRIVATE ILI9341)
    ELSE ()
        SET(BUILD_NAME "${BUILD_NAME}-ST7789")
    ENDIF ()
ELSEIF (HDMI)
    target_link_libraries(${PROJECT_NAME} PRIVATE hdmi)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HDMI)
    SET(BUILD_NAME "${BUILD_NAME}-HDMI")
ELSEIF (TV)
    target_compile_definitions(${PROJECT_NAME} PRIVATE TV)
    target_link_libraries(${PROJECT_NAME} PRIVATE tv)
    SET(BUILD_NAME "${BUILD_NAME}-TV")
ELSEIF(TV)
    target_compile_definitions(${PROJECT_NAME} PRIVATE TV)
    target_link_libraries(${PROJECT_NAME} PRIVATE tv)
    SET(BUILD_NAME "${BUILD_NAME}-TV")
ELSEIF(SOFTTV)
	target_compile_definitions(${PROJECT_NAME} PRIVATE SOFTTV)
	target_link_libraries(${PROJECT_NAME} PRIVATE tv-software)
	SET(BUILD_NAME "${BUILD_NAME}-TV-SOFT")
ELSE()
    target_compile_definitions(${PROJECT_NAME} PRIVATE VGA)
    target_link_libraries(${PROJECT_NAME} PRIVATE vga-nextgen)
    SET(BUILD_NAME "${BUILD_NAME}-VGA")
ENDIF()

if (HID)
    SET(BUILD_NAME "${BUILD_NAME}-HID")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${BUILD_NAME}")

pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 0)

#pico_set_binary_type(${PROJECT_NAME} no_flash)
#pico_set_binary_type(${PROJECT_NAME} copy_to_ram)

pico_add_extra_outputs(${PROJECT_NAME})
