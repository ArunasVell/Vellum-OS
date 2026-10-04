# Vellum OS

Fast, battery-friendly, beautifully simple firmware for e-ink readers. Starting with the XTEINK X4 Pro, built to run on more devices.

> **Status: early skeleton.** The desktop simulator runs a placeholder reader. On the X4 Pro the firmware has drivers for the screen (all three controller batches), battery gauge, buttons and frontlight, not yet tested on a device. Touch and the SD card are not written yet, and there is no text rendering, so do not flash it expecting a working reader.

## Goals

- **Speed**: the next page is ready before you press the button.
- **Battery life**: the chip sleeps between page turns and unused parts are powered off.
- **Reading experience**: good typography, careful refresh handling, no clutter.
- **Looks**: one consistent, calm design language.
- **Portability**: a new device is a new board profile, not a rewrite.

## Layout

```
components/vellum_core/   Portable code: framebuffer, display queue, app. No ESP-IDF or SDL here.
components/board_x4pro/   XTEINK X4 Pro board profile and drivers (ESP-IDF).
main/                     Firmware entry point.
sim/                      Desktop simulator (SDL2), the same core code running on a PC.
tests/                    Host unit tests for the core.
```

Only the board profile knows which device it runs on. Everything else talks to the hardware API in `components/vellum_core/include/vellum/hal.h`.

## Desktop simulator

Needs CMake, a C++20 compiler and SDL2 (`sudo apt install libsdl2-dev` or `brew install sdl2`).

```sh
cmake -S sim -B build/sim
cmake --build build/sim
./build/sim/vellum-sim
```

Right arrow or Space turns the page, Left goes back, H forces a full refresh, Esc quits. A full refresh briefly flashes the window, like e-ink does.

Run the tests with `ctest --test-dir build/sim`. Take a screenshot without a window:

```sh
./build/sim/vellum-sim --headless --keys nnn --screenshot page.png
```

## Firmware

Needs [ESP-IDF v6.1](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/get-started/).

```sh
idf.py set-target esp32s3
idf.py build
```

### Before flashing a real device

1. Only use a unit bought from xteink.com. Some reseller units are USB-locked, and flashing unsupported firmware on those can brick them.
2. Back up the full stock flash first, and keep the file safe:

   ```sh
   python -m esptool --chip esp32s3 -b 921600 read-flash 0 0x1000000 x4pro-stock-backup.bin
   ```

3. Then `idf.py -p PORT flash monitor`. To go back, write the backup with `write-flash 0 x4pro-stock-backup.bin`.

## Credits

The X4 Pro pin map, hardware notes, screen drivers (SSD1677, UC8179, UC8279 and the boot-time detection between them) and the CW2017 battery gauge driver are ported from the [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) (MIT, see [NOTICE](NOTICE)). [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) (MIT) is the reference open firmware for these devices.

## License

MIT, see [LICENSE](LICENSE).
