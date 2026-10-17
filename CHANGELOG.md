# Changes

Every change added to Vellum becomes a new version on the [releases page](https://github.com/ArunasVell/Vellum-OS/releases), numbered 0.1.N. This file collects them into readable steps. To install, see [docs/install.md](docs/install.md).

## First public alpha (17 October 2026)

The first version shared with everyone. It runs on the XTEINK X4 Pro and has been used day to day on one reader. Screen, touch, the Home pad, the memory card and updates are confirmed on a real device; the rest has been tested on the simulator and is being checked on the device now.

### Reading

- Justified, hyphenated text in English and Lithuanian.
- Fonts: Literata, Atkinson Hyperlegible Next, EB Garamond, or your own font file.
- Text size, line spacing, margins and alignment.
- Pictures, links and footnotes inside books.
- Night mode (white text on black), focus mode, and go to any page.
- Chapters, bookmarks, highlights and a dictionary: hold a word to look it up (dictionaries go on the memory card, see [docs/dictionaries.md](docs/dictionaries.md)).
- Reading stats, and every book reopens on the page you left.
- A footer with the time, page number and battery.

### Books

- EPUB, Kindle books without DRM (MOBI, AZW3, PRC), PDF with text in it (its words are set again as paragraphs), Word (DOCX), RTF, Markdown, FB2, plain text, web pages and comics (CBZ).
- Formats other than EPUB, FB2, text and web pages are converted once, the first time the library sees them.

### Library and getting books

- Home screen with your latest books and three big buttons: All books, Add books, Settings.
- Cover grid or list, shelves, folders, search, recently added, and finished books moved aside.
- Add books over Wi-Fi from a phone or computer by scanning two QR codes.
- A book store with free books from Project Gutenberg and Standard Ebooks, plus your own catalogue (OPDS).
- Send a book from one Vellum reader to another nearby.
- Reading position sync with KOReader, and every setting can be changed from a phone page.

### Sleep, battery and light

- Sleep screen with the book's cover, your own pictures, reading stats, or a minimal page.
- Sleep after 5, 10 or 30 minutes, or never; hold Power to sleep, press it to wake into your book.
- Battery log with a 7-day chart, time left, and how much reading and sleeping use.
- Front light control.

### Controls and setup

- Side buttons, Power, Home pad, taps and swipes; any button can be remapped.
- A quick panel (swipe down from the top) for light, night mode, contents, settings and sleep.
- Screen rotation, automatic page turning, and a choice of how often the screen fully refreshes.
- Large or normal menu text, English or Lithuanian menus.
- A short first-time setup where every step can be skipped.
- Updates over home Wi-Fi (at night, after asking, or off) or from a file.
- Device check: buttons, touch, battery, memory card and more, all on one screen.

### Fixed after the first tests on a real X4 Pro

- The screen now shows the right way up.
- The touch screen and Home pad now start reliably, and touches land where you touch.
- Every menu and book can be left with the buttons alone.
- The sample books are no longer added on the X4 Pro.

### Known limits

- Only the XTEINK X4 Pro is supported.
- The memory card is only found at start-up: put it in before switching on.
- Scanned or password-protected PDFs and Kindle books with DRM cannot be opened.
- Not yet confirmed on a real reader: front light, battery readings over several days, the book store's live catalogues, KOReader sync, and sending books between readers.

Found something? Please [open an issue](https://github.com/ArunasVell/Vellum-OS/issues).
