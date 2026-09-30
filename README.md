# pico-launcher - Bootloader Firmware Flasher / Launcher for MURMULATOR Devboard
Bootloader firmware flasher / launcher for MURMULATOR devboard

# How to Use
1) Copy all your Pico firmwares to the SD card.
2) Reboot PICO by holding down SELECT on the Gamepad or F11 on the keyboard and select default firmware.

Additionally, you can:
- Hold F12 or START button while booting to enter firmware update mode.
- Use F10 or the A button in the launcher to enter SD card-reader mode.

# Compiling
To compile it yourself:

1. Copy the file ``boot2\exit_from_boot2.S`` from the repository to your Pico SDK at ``pico-sdk\src\rp2_common\boot_stage2\asminclude\boot2_helpers\exit_from_boot2.S``
2. Build using the "MinSizeRel" build type.

## Olimex RP2040-PICO-PC with Pico 2 (PCp2)

Configure in a separate build directory:

```sh
cmake -S . -B build-PCp2 -DPICO_PC=ON -DHID=ON -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build-PCp2 --parallel
```

The Pico SDK must be available through `PICO_SDK_PATH`.
Output: `bin/MinSizeRel/PCp2-uf2-launcher-HDMI-HID.uf2`.
The launcher recognizes `.PCp2` firmware files (case-sensitive), plus `.uf2`/`.UF2`.
`PICO_PC` takes precedence over the default `M2` option and cannot be combined with `ZERO2`.
Only the onboard HDMI/DVI output is supported by this configuration.
USB HID defaults to ON for a fresh PCp2 build. USB drive mode is unavailable in HID builds.
For an external PS/2 keyboard and USB drive mode, explicitly configure `-DHID=OFF`.

Pin mapping follows [murmulator-os2](https://github.com/DnCraptor/murmulator-os2/blob/main/CMakeLists.txt):

| Signal | GPIO |
| --- | --- |
| HDMI/DVI clock pair | 12, 13 |
| HDMI/DVI data pairs | 14–19 |
| SD MISO / SCK / MOSI / CS | 4 / 6 / 7 / 22 |
| External PS/2 clock / data | 0 / 1 |
| External NES clock / latch / data 1 / data 2 (UEXT1) | 5 / 9 / 20 / 21 |

The existing launcher flash layout is unchanged: code and its saved boot block
are linked at the end of the 16 MiB XIP window. `FLASH_SIZE` does not relocate
these regions in the supplied source tree.
