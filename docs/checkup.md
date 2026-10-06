# Pre-device checkup

A review of the whole firmware before it first runs on an X4 Pro (ESP32-S3,
512 KB internal SRAM, 8 MB PSRAM, 16 MB flash), looking for what would show
up on the first boot or in daily use. Priorities: not crashing, speed,
battery life. Done in October 2026 against `main` at a98fb39.

Everything here was checked on a computer: the firmware build and its memory
map, the host simulator and tests, and some purpose-built measurements
(described under [How it was checked](#how-it-was-checked)). Times on the
device are estimates from host instruction counts; the device will tell the
real ones (see [Only the hardware can answer](#only-the-hardware-can-answer)).

## The short version

Fixed in this round, most important first:

1. **A PNG book cover overflowed the app's stack.** The app runs on the main
   task, which had 8 KB of stack. Decoding a PNG cover needs about 9 KB
   (`stbi__parse_png_file` alone has a 7.8 KB frame). The first library scan
   of a book with a PNG cover would have crashed, and kept crashing on every
   start until the book was removed. The main task now has 16 KB.
2. **Wi-Fi would likely not have started.** Allocations up to 16 KB preferred
   internal RAM, and the app makes many of them (rendered fonts, cover and
   book buffers): 67 KB with the default fonts, about 200 KB with a rendered
   menu and book font, against roughly 90 KB of internal heap. Internal RAM
   would have been full before "Add books" or an update started Wi-Fi, which
   needs internal RAM for its buffers and tasks. Now only blocks under 4 KB
   prefer internal RAM (the app's: under 1 KB in all).
3. **The CPU never slowed down.** Power management was enabled in the config
   but never configured, so the reader ran at 240 MHz all the time it was
   awake. It now runs at 240 MHz while it works and 80 MHz while it waits
   for a button or a touch.
4. **A broken book or font could crash the reader on every start.** After a
   crash or a watchdog reset the reader now starts on the home screen with
   Literata instead of reopening the book with the chosen fonts.
5. **Text could be laid out for the wrong font.** With a book font other
   than Literata, changing Text size or Book font left the open book's pages
   laid out for the old font (lines running off the page or gaps).
6. **The end of a long chapter was unreachable.** Chapters were cut at 1024
   pages; a book converted as one long file, at a large text size, lost its
   end. Now 8192 pages.
7. **An empty battery was not handled.** At 2 % and not charging the reader
   now sleeps with a "Battery empty" screen instead of running until the
   voltage collapses; under 20 % it skips the night's update check.
8. **An upload from a phone that walked away never ended.** Its handler kept
   waiting forever (and stopping the server waits for the handler). It now
   gives up after 45 s of silence.
9. **Speed:** page layout (done for a whole chapter when it opens) takes a
   third fewer instructions; waking to the home screen no longer renders a
   custom book font (about half a second); waking skips the PSRAM test and,
   from deep sleep, the app image check.
10. **Logs:** a boot summary line, warnings when internal RAM or the app's
    stack run low, a core dump in flash after a crash, and free memory in
    Settings › Device check.

The configuration changes are in `sdkconfig.defaults`. An existing
`sdkconfig` keeps its old values: delete it (and the build folder) before
the next firmware build.

Not fixed, most important first (details below): no light sleep while awake;
a custom menu font is rendered again on every wake (about half a second);
a chapter is laid out whole when it opens (seconds for a very long one);
corrupt font files can crash `stb_truetype`.

## How it was checked

- **Memory map:** `idf.py size` and `size-components`, and the largest
  symbols in internal RAM (`nm` on the ELF).
- **Stack:** every core and board source compiled with the firmware's own
  compiler and flags plus `-fstack-usage`, then the frames added up along
  the deep call chains.
- **Heap:** a host program that counts every allocation by size while the
  app starts with 20 books (first start and warm start), opens a book and
  turns 50 pages. On the device, an allocation's size decides whether it
  prefers internal RAM (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`).
- **Speed:** instruction counts with `callgrind` for layout, font
  rendering and start-up. As a rough guide, the S3 at 240 MHz manages
  150–200 million of these host instructions a second.
- **Corrupt input:** 1500 rounds each of mutated EPUBs (with cover decoding),
  dictionaries, fonts and the card's text files (settings, library,
  progress, bookmarks, highlights, stats, battery log, update state),
  built with AddressSanitizer and UBSan. The full test suite also ran
  under both.
- **Reading the code** for sleep, wake, Wi-Fi, updates and the watchdog.

## Memory

### Internal RAM

341,760 bytes of the S3's SRAM are usable as data RAM (DIRAM). With this
round's changes:

| Use | Bytes | Notes |
| --- | ---: | --- |
| Code that must run from RAM (IRAM in DIRAM) | 83,531 | Wi-Fi and interrupt code |
| Static data (`.data` + `.bss`) | 93,411 | the `App` object is 28,284 of it (was 36,468) |
| **Free for heap and stacks at start** | **164,818** | |
| Framebuffer | 48,000 | must be internal and DMA-capable |
| Display SPI chunk | 4,000 | DMA |
| Main task stack (the app) | 16,384 | was 8,192 |
| System task stacks | about 12,000 | idle ×2, IPC ×2, timer, events |
| Reserved for DMA and internal-only use | 32,768 | `SPIRAM_MALLOC_RESERVE_INTERNAL` |
| The app's small allocations | under 1,000 | was 67 KB (default fonts) to 200 KB (rendered fonts) |
| **Left for Wi-Fi, TLS, the web server** | **about 50,000 + the 32 KB reserve** | estimate |

Wi-Fi with buffers in PSRAM (`SPIRAM_TRY_ALLOCATE_WIFI_LWIP`) still needs
roughly 40–60 KB internal: its task, lwIP's task, the web server's 8 KB
stack and DMA descriptors. It should now fit; the boot summary and Device
check show the real figure, and the reader logs a warning when internal RAM
drops under 24 KB.

### PSRAM

Everything the app allocates up front, measured on the host after starting
with 20 books (pointers are half the size on the device, so these are
upper bounds):

| Use | Size | When |
| --- | ---: | --- |
| EPUB: 2 MB chapter buffer, paragraph, manifest and contents tables | about 2.7 MB | always |
| Covers: thumbnail work buffers and the two thumbnails shown | about 1.5 MB | always (1.1 MB before the full-screen sleep cover) |
| Library entries, cache, bookmarks and file buffer | about 0.6 MB | always |
| Library: folders still to read during a scan (64 paths) | 10 KB | always (added with folders) |
| Highlights and their file buffer | about 0.5 MB | always |
| Battery log and reading stats | about 0.25 MB | always |
| Reader page table | 64 KB | always |
| Text, FB2 and web-page books: chapter, paragraph and contents tables | about 0.8 MB | always (added with more book types) |
| **Total at start** | **about 6.2 MB** | |
| Free book store: one list page, a book's details, the way back, your own catalogues and their search | about 61 KB | from the first visit to the store |
| Phone page: the font list for its Settings section | about 1.2 KB | while the phone page is open |
| Rendered fonts | 20–60 KB per size | with a font other than Literata |
| Dictionary buffers | about 150 KB | after the first look-up |
| A PNG cover being decoded | width × height × channels | while making its thumbnail |
| Folder browser: one folder's listing (256 items) | about 34 KB | from the first visit to Folders |
| Picture viewer: a BMP row | 20 KB | from the first BMP shown |
| Picture viewer: a PNG file, then its pixels | the file (up to 1 MB), then width × height | while decoding it (JPEGs are read as they decode; the picture itself goes into the large cover buffer) |
| Wi-Fi and TLS buffers | 100–200 KB | while Wi-Fi is on |

About 1.6 MB stay free. A very large colour PNG cover (roughly a million
pixels or more in RGB) does not fit and keeps the drawn cover; nothing
crashes. The 2 MB chapter buffer is the one to shrink if PSRAM ever runs
short.

### Stacks

Deepest chains found (bytes, frames from `-fstack-usage`, on the device):

| Chain | Stack | Room |
| --- | ---: | --- |
| Library scan → cover → `stbi_load_from_memory` → `stbi__parse_png_file` | about 9,100 | was over 8,192: overflow. Now 16,384 |
| Home screen → `draw_home` → `draw_book_row` → `draw_picture` | about 6,500 | fine |
| Reader → `typeset_page` → `hyphenate` | about 3,000 | fine |
| Dictionary look-up → `find` → `read_entry` | about 2,500 | fine |
| Font rendering → `find_kerning` / `RasterFont::build` → `stb_truetype` | about 5,000 | fine |
| Web server: `post_upload` (8 KB task) | about 3,500 | fine |

`CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY` is on, so an overflow stops
with a clear message instead of corrupting memory. The boot summary prints
how much of the app's stack was never used, and a warning appears when
less than 2 KB was left at the deepest.

## Speed

Every wake from deep sleep restarts the firmware, so start-up time is wake
time.

| Work | Host instructions | Device estimate | Change |
| --- | ---: | ---: | --- |
| App start, Literata fonts, to the home screen | 7.4 M | 40–50 ms | — |
| …to the reader (opens and lays out the chapter) | 12.5 M | 60–80 ms | — |
| Menu font other than Literata (6 sizes) | 79 M | 0.4–0.5 s | not fixed |
| Book font other than Literata (3 sizes) | 78 M | 0.4–0.5 s | now only when a book opens |
| PSRAM test at start (8 MB) | — | tens to hundreds of ms | skipped now |
| App image check from deep sleep (2.5 MB SHA-256) | — | about 100 ms | skipped now |
| First library scan, 20 new books with covers | 45 ms on the host | 1–2 s | once; cached after |
| Laying out a page | 529 k → 349 k | 2.6–3.5 → 1.7–2.3 ms | hyphenation lookups, `strlen` per line, ASCII glyphs |

A chapter is laid out whole when it opens: about 0.3 s for 150 pages, but
several seconds for a chapter of a thousand pages or more. Turning a page
within a chapter sets only that page, and makes no allocations (measured:
none in 50 turns).

On the board, the steps before the first screen that wait: the SD card's
power cycle (80 ms off, 120 ms on), the touch controller's reset (80 ms),
and the first battery reading, which can take up to 4 s if the fuel gauge
has to restart (only after it lost power).

## Battery

| State | What is off | Done here |
| --- | --- | --- |
| Asleep (deep sleep) | CPU, RAM, radio; front light, touch and SD card power switched off and held | the touch controller also gets its sleep command first, in case its switch leaks |
| Awake, waiting | front light at its setting; touch scanning (it carries the Home pad) | CPU at 80 MHz instead of 240 |
| Awake, working | — | 240 MHz only while working |
| Wi-Fi on | — | held at 240 MHz while transfers run |

Not done: **automatic light sleep while waiting.** It would cut the waiting
current from tens of mA to a few, but needs, all verified on the device:
the buttons and the touch interrupt as light-sleep wake sources
(`gpio_wakeup_enable`, which turns their edge interrupts into level ones),
the front light's PWM on a clock that runs in light sleep (or a power lock
while the light is on), and accepting that the USB log drops in and out.
The configuration is ready (`CONFIG_PM_ENABLE`, tickless idle); the board's
`init_power_management()` is where it would be switched on.

The nightly update wake only happens when automatic updates are on, a home
Wi-Fi is saved and the clock is set; now also only with 20 % or more (or
charging) at wake time. Each one costs a few seconds of Wi-Fi.

## Robustness

| Case | Behaviour |
| --- | --- |
| Corrupt EPUB, dictionary, settings or other card files | 1500 mutated rounds each, under AddressSanitizer: no crash, no memory error |
| Corrupt JPEG cover | integer overflow inside `tjpgd`'s IDCT (UBSan): harmless wrap, picture garbage at worst |
| Corrupt font file | `stb_truetype` reads past the file (it does not check offsets). On the device that can crash; the reader then restarts at home with Literata (fixed above). Uploads arrive whole, so only a truly broken file does this |
| Crash or watchdog reset | starts at home with Literata; core dump saved; log says so |
| Watchdog during long work | the task watchdog only warns (no reset): index building for a big dictionary, a first library scan or font rendering print "task watchdog" lines, which are harmless |
| Missing card | built-in books only; Add books says there is no card |
| Full card | uploads refused before writing (256 KB kept free); other writes fail quietly and keep the old file |
| Clock not set | no battery history, no night wake; reading stats use the steady timer |
| Empty battery | sleeps with "Battery empty" at 2 % (fixed above) |
| Brownout reset | logged as such in the boot summary |
| Phone leaves during an upload | upload dropped after 45 s (fixed above) |

## Logs

Once the first screen is up the reader logs two lines like:

```
I (1234) x4pro: boot: Vellum 0.1.60, panel SSD1677, card mounted, start: deep sleep, first screen at 1830 ms
I (1234) x4pro: boot: free internal 104 KB (largest block 72 KB), PSRAM 4210 KB, app stack 7100 B left
```

and later, only when it happens:

```
W (…) x4pro: internal RAM low: 20 KB free
W (…) x4pro: app stack nearly full: 1800 B were left at the deepest
W (…) x4pro: the last run crashed (crash): starting at home with the built-in font; `idf.py coredump-info` shows where
```

**Settings › Device check** has a new last row, **Free memory**: internal RAM
in KB, then PSRAM in MB.

## Not fixed: recommendations

In order of value:

1. **Light sleep while awake** (see Battery). The largest battery gain left;
   needs the hardware to verify wake-up from every button and touch.
2. **Cache rendered fonts on the card.** A custom menu font costs about half
   a second on every wake. Writing each rendered size to the card (20–60 KB
   each) and reading it back would take a few tens of ms. The cache must be
   checked on load (glyph offsets within the bitmap) so a damaged file
   cannot crash drawing.
3. **Lay chapters out as they are read.** Long chapters take seconds to open;
   laying out ahead of the reader (and counting the rest in the background)
   would make every chapter open in a page's time.
4. **Check font files before using them.** A pass over the `loca`/`glyf`
   (and CFF) offsets for the characters used would stop `stb_truetype` from
   reading outside a broken file.
5. `Font::kern` is a quarter of layout time (a binary search over ~10,000
   pairs per letter pair); an index by left glyph would halve it.
6. The phone page's highlight list uses an 8 KB static buffer in internal
   RAM (`Highlights::write_json`); it could move to PSRAM.
7. The SD card's first power-on could reuse the time the slot was already
   off during start-up (80 ms per wake).

## Only the hardware can answer

These are in [first-day-tests.md](first-day-tests.md) where useful:

- Real free internal RAM with Wi-Fi on (boot summary, Device check, and the
  log during Add books and an update).
- Real start and wake times (the boot summary's "first screen at").
- Current asleep and awake, with a USB power meter: does the touch
  controller or the SD card draw anything in deep sleep, and how much does
  80 MHz save while waiting?
- Whether the fuel gauge reads sensibly near empty (the reader now acts on
  2 %); whether a brownout happens before that during a full refresh.
- Whether the front light flickers with the CPU changing speed (it should
  not: 80 MHz keeps its clock).
- How long the first battery reading takes after the gauge loses power.
