# Vellum OS

Fast, battery-friendly, beautifully simple firmware for e-ink readers. Starting with the XTEINK X4 Pro, built to run on more devices.

> **Status: early skeleton.** The simulator has a home screen, a library of four built-in public-domain books plus any EPUB books on the SD card, settings (English or Lithuanian menus, 12- or 24-hour clock, text size, line and word spacing, margins, alignment, hyphenation) and the reader, with justified, hyphenated text in Literata, Atkinson Hyperlegible Next, EB Garamond or any .ttf/.otf font added from a phone (menus can use a different font from books). Settings and the page you are on in each book are saved to the card. **Add books** opens the reader's own Wi-Fi network so a phone or computer can send books from a browser. Holding Power for a second puts the reader to sleep with the book's cover on screen, as does leaving it idle (10 minutes by default); pressing Power wakes it back in the book. On the X4 Pro the firmware has drivers for the screen (all three controller batches), battery gauge, clock, buttons, touch screen, frontlight, SD card, Wi-Fi book upload and deep sleep, not yet tested on a device.

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
sim/                      Desktop and browser simulators, the same core code running on a PC.
assets/fonts/             Source fonts: tools/make_fonts.sh pre-renders Literata, tools/make_ttf_fonts.py embeds the others.
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

Right arrow or Space turns the page or moves the highlight, Left goes back, Enter opens the highlighted item (the power button on the device), H goes to the home screen, Esc quits. The mouse is the touch screen: click to tap, drag to swipe. A full refresh briefly flashes the window, like e-ink does.

Run the tests with `ctest --test-dir build/sim`. Take a screenshot without a window:

```sh
./build/sim/vellum-sim --headless --keys onnn --time 14:05 --screenshot page.png
```

Keys: `n` next, `p` previous, `o` open (power), `z` hold power (sleep), `h` home, `[X,Y]` taps a point and `[X,Y>X2,Y2]` swipes (logical portrait pixels, 480 x 800). `--time` fixes the clock so screenshots repeat.

Add `--card DIR` to use a folder as the SD card (works with or without `--headless`).

### The SD card

Put EPUB books in a `books` folder on the card (books in the top folder are found too). Vellum keeps its own files in a `vellum` folder: `settings.txt`, `progress.txt` (the page you are on in each book), `state.txt` (whether to reopen the book after sleep) and `library.txt` (a list of the books, so the library opens without re-reading every file). All three are plain text and safe to delete; Vellum rebuilds them.

### Adding books over Wi-Fi

**Add books** on the home screen starts a Wi-Fi network named `Vellum-XXXX` with a new password each time, and shows it with two QR codes. Join the network (scan the first code), then open `http://192.168.4.1` (the second code) in any browser and choose EPUB files; they are saved to the card's `books` folder. Press Home on the reader to finish: Wi-Fi turns off and the library picks up the new books.

The upload page is `components/vellum_core/web/upload.html`; after editing it run `python3 tools/embed_page.py`. Its three requests (`GET /`, `GET /books`, `POST /upload?name=`) are described in `vellum/upload_page.h`, so other boards can serve the same page.

### In the browser

The same core code also compiles to WebAssembly, so the simulator runs as one self-contained web page (works on phones too). Needs [Emscripten](https://emscripten.org/docs/getting_started/):

```sh
sim/web/build.sh   # writes build/web/vellum-simulator.html
```

Drop EPUB files on the page to add them to the library. The page keeps its card in the browser (IndexedDB), so books, settings and progress are still there next time.

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

The X4 Pro pin map, hardware notes, screen drivers (SSD1677, UC8179, UC8279 and the boot-time detection between them) and the CW2017 battery gauge driver are ported from the [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) (MIT, see [NOTICE](NOTICE)). [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) (MIT) is the reference open firmware for these devices. EPUB files are unzipped with the inflater from [miniz](https://github.com/richgel999/miniz) (MIT, in `components/vellum_core/third_party/miniz`). QR codes are drawn with [Project Nayuki's QR Code generator](https://www.nayuki.io/page/qr-code-generator-library) (MIT, in `components/vellum_core/third_party/qrcodegen`).

Literata (the Literata Project Authors), Atkinson Hyperlegible Next (the Atkinson Hyperlegible Next Project Authors) and EB Garamond (the EB Garamond Project Authors) are under the SIL Open Font License; the licences are in each folder of [assets/fonts](assets/fonts). Fonts not pre-rendered are drawn with stb_truetype by Sean Barrett (public domain or MIT). Hyphenation patterns come from the [hyph-utf8](https://www.hyphenation.org/tex) project: American English by Gerard D.C. Kuiken, Lithuanian by Vytas Statulevičius, Sigitas Tolušis and Yannis Haralambous (MIT); licences are kept in [assets/hyphenation](assets/hyphenation) and the generated tables. The built-in books are public-domain texts from Project Gutenberg: the first chapters of Lewis Carroll's *Alice's Adventures in Wonderland*, Herman Melville's *Moby-Dick* and Jane Austen's *Persuasion*, and G. K. Chesterton's story *The Absence of Mr Glass*.

## License

MIT, see [LICENSE](LICENSE).
