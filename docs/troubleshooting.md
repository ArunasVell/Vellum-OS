# Troubleshooting

Something not working? Find what you see below and follow the steps. Each one is a real problem someone has run into with Vellum on the XTEINK X4 Pro. If yours is not here, see [How to report a problem](#how-to-report-a-problem) at the end.

To find which version of Vellum you have: **Settings › General › Updates** shows it at the top ("Vellum 0.1.N on this reader"). **Settings › General › Device check** shows it too, in the Version row.

- [Installing](#installing)
  - [The installer page cannot find the reader](#the-installer-page-cannot-find-the-reader)
  - [The browser has no button to connect](#the-browser-has-no-button-to-connect)
  - [I want to update, but I have no computer](#i-want-to-update-but-i-have-no-computer)
  - [Check now says Vellum is up to date, but I heard about a new version](#check-now-says-vellum-is-up-to-date-but-i-heard-about-a-new-version)
- [Screen and touch](#screen-and-touch)
  - [The screen or a book is upside down](#the-screen-or-a-book-is-upside-down)
  - [Landscape does not work in the menus](#landscape-does-not-work-in-the-menus)
  - [Touch and the Home pad do nothing](#touch-and-the-home-pad-do-nothing)
- [Memory card and books](#memory-card-and-books)
  - [The memory card shows Missing](#the-memory-card-shows-missing)
  - [A book is stuck on Preparing a book…](#a-book-is-stuck-on-preparing-a-book)
  - [A book will not open](#a-book-will-not-open)
- [If nothing else helps](#if-nothing-else-helps)
- [How to report a problem](#how-to-report-a-problem)

## Installing

### The installer page cannot find the reader

**What you see:** you press a button on the installer page (`vellum-install-0.1.N.html`), but the reader is not in the list, or the page says it could not connect.

**What to do:** put the reader in **download mode**. It is needed the first time, while the original software is still on the reader, and any time the reader does not start.

1. Keep the USB cable connected to the computer.
2. Turn the reader off completely.
3. Press and hold the **left page button** (the one that goes back a page).
4. While still holding it, press **Power**. Keep holding the page button for 3 more seconds, then let go.
5. The screen does not change. That is normal: the reader is waiting for the computer.
6. Press the step's button on the page again and pick the reader in the list (often called "USB JTAG/serial debug unit").

Also try another USB cable: some cables only charge and carry no data.

**Before installing for the first time**, always do step 1 on the page, **Save the original software**. It saves a file named `x4pro-original-<date>.bin` in your Downloads folder. Keep it safe: it is your only way back to the original software.

### The browser has no button to connect

**What you see:** the installer page opens, but connecting does nothing or the page says your browser is not supported.

**What to do:** open the page in **Google Chrome** or **Microsoft Edge** on a computer (Windows, Mac, Linux or a Chromebook). Safari and Firefox cannot talk to the reader over a cable, and neither can phones or tablets. Double-clicking the downloaded file is fine; it works without internet.

### I want to update, but I have no computer

You do not need the cable to update a reader that already runs Vellum. A phone or a tablet is enough.

1. On the phone or tablet, open the [latest release](https://github.com/ArunasVell/Vellum-OS/releases/latest) and download the file `vellum-0.1.N.bin` (not the `install` or `usb` files).
2. On the reader: **Settings › General › Updates › Update from a file**.
3. The reader shows a Wi-Fi network and QR codes. Scan the first to join the reader's network, then the second to open its page.
4. On that page, choose the `.bin` file you downloaded and send it.
5. The reader shows the progress, restarts and comes back with the new version. Your books, settings and reading places are kept.

If home Wi-Fi is set up, **Settings › General › Updates › Check now** downloads and installs the new version by itself, with no file at all.

### Check now says Vellum is up to date, but I heard about a new version

New versions are not published every time something changes. A version only appears once it is released, and until then the reader rightly says **Vellum is up to date**. A change that is "done" on GitHub can take a day or more to reach a release.

What else the Updates screen can say:

- **Set up home Wi-Fi to look for updates.** Use **Settings › General › Updates › Home Wi-Fi** first (your phone does the typing).
- **Could not join the home Wi-Fi.** Check the network name and password, and that the reader is in range.
- **Could not reach the update server.** The Wi-Fi works but the internet does not. Try again later.

## Screen and touch

### The screen or a book is upside down

**What you see:** the home screen, menus or books are upside down.

**What to do:**

1. Go to **Settings › Display & light › Screen**.
2. Choose **Portrait**.

This setting is saved on the memory card. If you once chose **Upside down** (or tried it while testing), it comes back every time that card is in the reader, even after an update. With a different card, or no card, the reader starts in Portrait, so a screen that turns over when you put a card in is this setting.

### Landscape does not work in the menus

**What you see:** you chose **Landscape, keys left** or **Landscape, keys right** in **Settings › Display & light › Screen**, but the home screen and menus stay upright.

**This is on purpose.** Only books turn on their side, when you open one; menus stay upright so their buttons stay where your fingers expect them. The Screen setting says so under the list: "Books turn on their side when you open one. Menus stay upright." Open a book to see landscape.

### Touch and the Home pad do nothing

**What you see:** taps and swipes do nothing, and the Home pad below the screen does not take you home. The side buttons and Power still work.

**What to do:**

1. Check your version (**Settings › General › Updates**). This was fixed in **version 0.1.96**: on older versions the touch screen did not start properly on the X4 Pro. If yours is older, update (see [I want to update, but I have no computer](#i-want-to-update-but-i-have-no-computer)).
2. If it still happens on a newer version, open **Settings › General › Device check**. Touch the screen in the box labelled **TOUCH**. At the bottom of that box is a short line about the touch chip (it looks something like `5D · irq yes · 120 ok 0 err`).
3. Take a photo of the whole Device check screen and send it (see [How to report a problem](#how-to-report-a-problem)).

If touch works but lands in the wrong place, Device check helps there too: a cross appears where the reader thinks you touched. Touch each corner and note where the cross appears.

## Memory card and books

### The memory card shows Missing

**What you see:** **Settings › General › Device check** says **Memory card: Missing**, or the library is empty although the card has books.

**What to do:** the reader only looks for the card when it **starts**, not when you put it in later.

1. Take the card out and push it back in firmly until it clicks.
2. Restart the reader: hold Power to put it to sleep, then press Power to wake it. If that does not help, turn it fully off and on again.
3. Put the card in **before** switching the reader on in the future.

If it still says Missing, try the card in a computer. It must be a microSD card formatted as **FAT32**, with books in a folder called `books`.

### A book is stuck on Preparing a book…

**What you see:** you open a book (often a large PDF) and the screen says **Preparing a book…** for a very long time.

Books that are not EPUB (PDF, Kindle books, Word and others) are converted once, the first time you open them. That is normally a few seconds to a minute. On **version 0.1.142 and older** some large PDFs took far too long and there was no way out.

**What to do on a newer version:** tap the screen or press a button to stop. The screen shows "Page N of M" while it works, so you can see it is moving. The book stays in the library; open it again when you have time to wait, since it only converts once.

**What to do on version 0.1.142 or older:**

1. Pull the memory card out. The reader goes back to the home screen.
2. On a computer, move that file off the card (or out of the `books` folder).
3. Put the card back and restart the reader.
4. Update to a newer version, then copy the book back.

### A book will not open

Vellum tells you why with a short message:

- **This book is locked**: the book has DRM (copy protection). Kindle books bought from Amazon, and PDFs with a password, cannot be opened. Books without DRM, from Project Gutenberg, Standard Ebooks or shops that sell DRM-free books, work.
- **Only pictures of pages, no text**: the PDF is a scan (photos of paper pages). Vellum reflows the text of a PDF to fit the screen, so it needs real text inside. Look for an EPUB of the same book instead.
- **This book could not be read**: the file is damaged or in a form Vellum does not know. Try copying it to the card again, or find another copy.

Newer Kindle files (KFX) are not supported at all. EPUB always works best.

## If nothing else helps

The reader can always be brought back with the installer page on a computer, even if it no longer starts:

1. Put the reader in download mode (see [The installer page cannot find the reader](#the-installer-page-cannot-find-the-reader)).
2. Run **step 2, Install Vellum** again. Your books stay on the card.
3. Or run **step 3, Go back to the original** and choose the `x4pro-original-<date>.bin` file you saved before installing.

## How to report a problem

Please tell us what went wrong: it is how Vellum gets better. Open a [new issue on GitHub](https://github.com/ArunasVell/Vellum-OS/issues/new) (you need a free GitHub account) and include:

1. **The version number**, from **Settings › General › Updates** (or the Version row in Device check).
2. **What you did, what you expected, and what happened**, in a few sentences.
3. **A photo of Settings › General › Device check.** It shows the screen, battery, clock, memory card, front light, Wi-Fi, free memory and the touch line, which answers most of our questions at once.
4. **A photo of the screen** when something looks wrong.
5. **The log, if you can.** With the reader connected to a computer, open the installer page in Chrome or Edge, press **Show log** at the bottom, do the thing that goes wrong, then **Copy** and paste the text into the issue. [first-day-tests.md](first-day-tests.md#sending-a-log) explains this step by step.

If the reader restarted by itself, say so: it keeps a record of the crash, and the log after the restart says `the last run crashed`.
