# Vellum OS

Fast, battery-friendly, beautifully simple firmware for e-ink readers. Starting with the XTEINK X4 Pro, built to run on more devices.

> **Status: alpha.** Vellum installs and runs on the XTEINK X4 Pro. The screen, touch screen, Home pad, SD card and updates are confirmed working on a device; the rest is still being tested day to day (see [docs/first-day-tests.md](docs/first-day-tests.md)). Expect rough edges, and save the original software before installing (the installer does this for you).

## What it does

- **Reading**: justified, hyphenated text (English and Lithuanian) in Literata, Atkinson Hyperlegible Next, EB Garamond or your own font; text size, spacing, margins and alignment; pictures, links and footnotes; night mode, focus mode and go-to page.
- **Books**: EPUB, Kindle books without DRM (MOBI, AZW3), PDF with text in it, Word, RTF, Markdown, FB2, plain text, web pages and comics (CBZ).
- **Finding your place**: chapters, bookmarks, highlights, a dictionary, reading stats, and the page you were on in every book.
- **Library**: cover grid or list, shelves, folders, search and recently added.
- **Getting books**: send them from a phone or computer over Wi-Fi, download free books from Project Gutenberg, Standard Ebooks or your own catalogue, or send a book from one Vellum reader to another.
- **Sync**: reading position with KOReader sync, and every setting can be changed from a phone page.
- **Sleep and battery**: the book's cover, your own pictures or stats on the sleep screen; a battery log with a 7-day chart and time left.
- **Controls**: remap any button, swipe and tap gestures, a quick panel, screen rotation and front light.
- **Setup and updates**: a short first-time setup (each step can be skipped), menus in English, Lithuanian, German, French, Spanish, Italian, Polish, Latvian, Estonian or Ukrainian, and updates installed from a phone or a file.

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

Put books in a `books` folder on the card (books in the top folder are found too). Vellum reads EPUB, Kindle books without DRM (`.mobi`, `.azw3`, `.azw`, `.prc`, `.pdb`), PDF with text in it (reflowed: its words are set again as paragraphs, so pictures, columns and page design are left behind; scanned or password-protected PDFs cannot be read), Word (`.docx`), RTF, Markdown, FictionBook (`.fb2`, `.fb2.zip`), plain text, single web pages and comics (`.cbz`, a page per screen). Books that are not EPUB, FB2, text or web pages are converted once, the first time you open them, into `vellum/converted/` (safe to delete; they are converted again). The screen shows the page it has got to, and a tap stops it. Vellum keeps its own files in a `vellum` folder: `settings.txt`, `progress.txt` (the page you are on in each book), `state.txt` (whether to reopen the book after sleep) and `library.txt` (a list of the books, so the library opens without re-reading every file). All of them are plain text and safe to delete; Vellum rebuilds them.

On a card Vellum has never used (no `settings.txt` and no `state.txt`), a short setup comes before the home screen: language, clock, menu text size, home Wi-Fi and how to add books, each with Skip. Finishing it saves `setup_done=1` in `settings.txt`; Settings › Setup again runs it once more. With `--card`, an empty folder shows it.

### Adding books over Wi-Fi

**Add books** on the home screen starts a Wi-Fi network named `Vellum-XXXX` with a new password each time, and shows it with two QR codes. Join the network (scan the first code), then open `http://192.168.4.1` (the second code) in any browser and choose EPUB files; they are saved to the card's `books` folder. Press Home on the reader to finish: Wi-Fi turns off and the library picks up the new books.

The upload page is `components/vellum_core/web/upload.html`; after editing it run `python3 tools/embed_page.py`. Its three requests (`GET /`, `GET /books`, `POST /upload?name=`) are described in `vellum/upload_page.h`, so other boards can serve the same page.

### In the browser

The same core code also compiles to WebAssembly, so the simulator runs as one self-contained web page (works on phones too). Needs [Emscripten](https://emscripten.org/docs/getting_started/):

```sh
sim/web/build.sh   # writes build/web/vellum-simulator.html
```

Drop EPUB files on the page to add them to the library. The page keeps its card in the browser (IndexedDB), so books, settings and progress are still there next time.

## Install on your X4 Pro

**New to Vellum? Follow [docs/install.md](docs/install.md)**, a step-by-step guide that needs no technical knowledge. What changed in each version is in [CHANGELOG.md](CHANGELOG.md). Stuck? See [docs/troubleshooting.md](docs/troubleshooting.md).

Every release has a file `vellum-install-<version>.html`. Download it, open it in **Chrome or Edge** on a computer (double-clicking the downloaded file is fine; it needs no internet), connect the reader with its USB cable and follow the three steps on the page: save the original software, install Vellum, and (whenever you like) go back to the original. The page is in English and Lithuanian, explains how to put the reader in download mode if it will not connect, and has a **Show log** panel that shows what the reader prints, ready to copy into a bug report.

Then go through [docs/first-day-tests.md](docs/first-day-tests.md), the checklist for a reader's first day with Vellum.

The page is made by `tools/make_installer.py` from a firmware build (`python3 tools/make_installer.py build`), which puts the firmware images and [esptool-js](https://github.com/espressif/esptool-js) (Apache 2.0, pinned and checksum-checked) inside the one file; CI attaches it to each release. Web Serial needs a secure page, and Chrome counts a file opened from disk as one. Installing over other software also wipes the small settings area (NVS), where the original software keeps its own settings; reinstalling over Vellum keeps it (home Wi-Fi, paired readers).

Merging into `main` only builds and tests. A release is published by hand: **Actions › CI › Run workflow** on `main`. Its notes list the changes merged since the previous release.

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
