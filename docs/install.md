# Installing Vellum on an XTEINK X4 Pro

This guide is for anyone who wants to try Vellum on their own reader. It takes about 15 minutes and needs no technical knowledge. Vellum is an **alpha**: it works day to day on the X4 Pro, but expect rough edges. You can always go back to the original software.

Vellum runs on the **XTEINK X4 Pro** only for now. Other models are planned.

## What you need

- An XTEINK X4 Pro bought from **xteink.com**. Some units sold by other shops are locked against new software, and trying to install on those can leave the reader unusable.
- A computer (Windows, Mac, Linux or a Chromebook) with **Google Chrome** or **Microsoft Edge**. Safari and Firefox cannot talk to the reader over a cable.
- The reader's USB cable.
- A memory card (microSD) with a few books, if you want to read straight away. EPUB works best; Kindle books without DRM, PDF with text, Word, FB2, plain text and comics (CBZ) work too.

## 1. Download the installer

1. Open the [latest release](https://github.com/ArunasVell/Vellum-OS/releases/latest).
2. Under **Assets**, download the file named `vellum-install-0.1.N.html` (N is the version number).

That one file is the whole installer. It works without internet once downloaded.

## 2. Open it and connect the reader

1. Find the downloaded file and open it in **Chrome or Edge** (double-click it, or drag it onto a Chrome window). The page is in English and Lithuanian.
2. Connect the reader to the computer with the cable.

## 3. Save the original software (do not skip this)

On the page, press the button in **step 1, Save the original software**. Chrome shows a list: pick the reader (often called "USB JTAG/serial debug unit") and press **Connect**.

The page reads the reader's original software and saves it as a file named `x4pro-original-<date>.bin` in your Downloads folder. This takes a few minutes. **Keep this file safe**: it is your only way back to the original software.

### If the reader will not connect

The first time, while the original software is still on the reader, it usually has to be put in **download mode**:

1. Keep the cable connected.
2. Turn the reader off completely.
3. Press and hold the **left page button** (the one that goes back a page).
4. While still holding it, press **Power**. Keep holding the page button for 3 more seconds, then let go.
5. The screen does not change. That is normal: the reader is waiting for the computer. Press the step's button on the page again.

## 4. Install Vellum

Press the button in **step 2, Install Vellum** and pick the reader again if Chrome asks. When it finishes, the reader restarts by itself (press Power if the screen stays the same for half a minute).

## 5. First start

On a memory card Vellum has not seen before, a short setup comes first: language, clock style, menu text size, home Wi-Fi and how to add books. Every step has **Skip**, and you can run it again later from **Settings › General › Setup again**.

Put the memory card in **before** switching the reader on: for now the card is only looked for at start-up.

### The buttons

- **Page buttons** (on the side): turn pages and move the highlight in menus. Holding one in a menu goes back.
- **Power**: open or choose. Hold it for a second to put the reader to sleep; press it to wake up.
- **Home pad** (the touch spot below the screen): back to the home screen.
- **Touch screen**: tap the right or middle of a page to go forward, the left to go back, or swipe. Swipe down from the top edge for the quick panel (light, night mode, contents, settings, sleep).

## Adding books

- **Memory card**: copy books into a folder called `books` on the card, using a computer.
- **Over Wi-Fi**: on the home screen choose **Add books**. The reader shows a Wi-Fi network with two QR codes. Scan the first with your phone to join the network, then the second to open the upload page, and choose your books. Press Home on the reader when you are done.
- **Free books**: the book store on the reader downloads free books from Project Gutenberg and Standard Ebooks once home Wi-Fi is set.

## Updates

New versions of Vellum are released from time to time. There are two ways to update:

- **Automatically**: set your home Wi-Fi in **Settings › General › Updates › Home Wi-Fi** (your phone does the typing). The reader then looks for a new version at 3:00 at night while it sleeps and installs it. In **Settings › General › Updates › Automatic** you can choose **Ask first** or **Off** instead, and **Check now** looks straight away.
- **From a file**: download `vellum-0.1.N.bin` from the [latest release](https://github.com/ArunasVell/Vellum-OS/releases/latest), then on the reader choose **Settings › General › Updates › Update from a file** and send the file from your phone or computer the same way as books.

When **Check now** finds a new version, a **What's new** screen lists its new features and bug fixes before you install it (the side buttons turn its pages). After any update, however it was installed, the reader shows what's new once when it first starts.

Updates keep your books, settings and reading progress.

## Going back to the original software

Open the installer page again, connect the reader, and in **step 3, Go back to the original**, choose the `x4pro-original-<date>.bin` file you saved. If the reader will not connect, use download mode as described above.

## Something wrong?

First look in [troubleshooting.md](troubleshooting.md): it covers the problems people have run into so far (reader not connecting, screen upside down, touch not working, memory card missing, books that will not open) with what to do for each.

If yours is not there, please tell us: open an [issue on GitHub](https://github.com/ArunasVell/Vellum-OS/issues) and say what you did, what you expected and what happened. A photo of the screen helps a lot. At the bottom of the installer page, **Show log** shows what the reader prints while connected by cable; copying that text into the issue is the most useful thing you can send.

If you want to test everything properly, [first-day-tests.md](first-day-tests.md) walks through each part of the reader in about half an hour.
