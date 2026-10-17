# First day with Vellum on the X4 Pro

Vellum has been built and tested on a computer, but not yet on a real X4 Pro. This list walks through everything once, in about half an hour. Each test says what to do, what "good" looks like, and what to send back if it isn't.

Before you start:

- Install Vellum with the `vellum-install-<version>.html` page from the latest release (steps 1 and 2 on that page). Keep the backup file from step 1.
- Put a memory card in the reader with two or three EPUB books in a `books` folder.
- Keep the installer page open on the computer and the cable plugged in for the first few tests. At the bottom of the page, press **Show log** and pick the reader. Whatever the reader prints appears there. If a test fails, that text is the most useful thing to send (see [Sending a log](#sending-a-log)).
- Take photos when something looks wrong. A photo of the screen says more than a description.

The buttons in this list: the two **page buttons** on the side (one goes back a page, one goes forward), **Power**, and the **Home pad** (the touch spot below the screen).

## 1. Screen

**Do:** Wait after installing. The reader restarts by itself, or press Power if the screen stays the same for half a minute.

**Good:** The Vellum home screen appears, upright when you hold the reader the usual way, with the clock and battery at the top. Text is sharp, nothing is cut off at the edges, and no faint copy of the old screen ("ghosting") remains.

**If not, send:** a photo; whether the picture is upside down, mirrored, sideways or shifted; and the log lines that start with `display controller` and `full refresh`.

## 2. Page turns look clean

**Do:** Open a book. Turn 20 pages forward with the page button, then a few back.

**Good:** Each turn is quick (well under a second) with no black flash. About every 12 pages the whole screen flashes once to clean itself; that's normal. Text stays crisp; no grey smudges build up.

**If not, send:** a photo of a smudged page, and the `fast refresh` / `full refresh` lines from the log (they show how long each turn took).

## 3. Buttons

**Do:** Home screen → **Settings** → **Device check**. Press each page button, Power, and the Home pad once.

**Good:** Each of the four boxes (Up, Down, Home, Power) turns black while pressed and gets a tick afterwards. Holding a button keeps only its own box black. Pressing Home twice leaves the screen.

**If not, send:** which box lit up for which physical button (for example "the forward page button lights up Up"), or which one does nothing.

## 4. Touch lands where you touch

**Do:** Stay in **Device check**. Touch each of the four corners of the screen in turn, then the centre. Then drag a finger slowly across the screen.

**Good:** A cross appears exactly under your finger every time, and follows the drag.

**If not, send:** where the cross appeared when you touched the top-left corner (for example "bottom-left" or "top-right"), and the same for the top-right corner. Also send the log lines that start with `touch raw`. This tells us which of the three touch settings (`kTouchSwapXY`, `kTouchFlipX`, `kTouchFlipY`) to change; it's a one-line fix.

## 5. Taps and swipes in a book

**Do:** In a book: tap the right third of the page, then the left third, then the middle. Then swipe left, and swipe right. Then swipe down from the very top edge of the screen.

**Good:** Right third turns forward, left third goes back, the middle turns forward too. Swipe left turns forward, swipe right goes back. The swipe from the top opens the quick panel (light, Night, Contents, Home, Settings, Sleep); Home or a tap under it closes it.

**If not, send:** which taps or swipes did the wrong thing.

## 6. Battery

**Do:** Look at the battery number at the top of the screen. Then **Settings → Device check** shows it as a percentage. Unplug the cable and look again after a minute. Also open **Settings → Sleep & battery → Battery**.

**Good:** A sensible percentage (not 0 % or stuck at 100 %). With the cable in it says "Charging"; unplugged it says "On battery". The Battery screen opens without errors (its history fills in over the following days).

**If not, send:** what it shows plugged and unplugged, and what the stock software showed before, if you remember.

If the reader goes straight to a **Battery empty** screen although the battery is not empty, the gauge is reading 2 % or less. Plug the cable in: the reader works normally while charging. Send what Device check shows for the battery, plugged and unplugged.

## 7. Clock

**Do:** Look at the time at the top. If it says the clock is not set, do test 10 (Add books), which sets the clock from your phone, then come back.

**Good:** After the phone has set it, the time is right and stays right after the reader sleeps and wakes (test 11), and the next day. **Settings → General → Clock** switches between 24-hour and 12-hour.

**If not, send:** what time it showed and the real time, and whether it was before or after sleeping.

## 8. Front light

**Do:** **Settings → Display & light → Front light**. Use the page buttons to change Brightness, press Power to go to Warmth, and change that too. Tapping the sliders also works.

**Good:** Brightness goes from off to bright in even steps. Warmth set to **Cool** looks white-blue, **Warm** looks orange, and in between it mixes smoothly. No flicker or buzzing.

**If not, send:** whether Cool and Warm look swapped, whether some steps do nothing, and if there is flicker.

## 9. Memory card and books

**Do:** Home → **All books**. Open each of your books. In one, turn to page 30, press Home, then open the same book again.

**Good:** Your books are listed with their titles and authors. Each opens within a few seconds with readable text (accents and Lithuanian letters too). The book reopens on the page you left.

**If not, send:** the name of the book that misbehaves (and the EPUB file, if you can), what you saw, and the log from opening it.

## 10. Wi-Fi: Add books and the phone page

**Do:** Home → **Add books**. On your phone, scan the first QR code to join the reader's Wi-Fi, then the second one to open the page. Send one EPUB from the phone. Press Home on the reader when done.

**Good:** The phone joins the `Vellum-…` network, the page opens, the book uploads with a progress bar and appears on the reader's screen under "Received", and then in All books. Opening the phone page also sets the reader's clock.

**If not, send:** which step failed (joining the Wi-Fi, opening the page, or uploading), any message on the reader or phone, and the log.

## 11. Sleep and wake

**Do:** In a book, hold Power for a moment (under a second) and let go. Wait a minute, then press Power once. Then leave the reader alone for longer than **Settings → Sleep & battery → Sleep after** (10 minutes unless changed).

**Good:** Holding Power shows the book's cover across the whole screen; a short press brings you back to the same page within a few seconds. After the idle time it goes to sleep by itself. Overnight asleep the battery should drop only a little (a few percent at most).

**If not, send:** whether it didn't sleep, didn't wake, or woke to a different place; the battery percentage before and after a night. (The log disconnects while the reader sleeps; the installer page reconnects by itself when it wakes.)

## 12. Device check: what the reader knows about itself

**Do:** **Settings → Device check** and look at the "This device" list.

**Good:** Model, Version, Screen (the screen's controller), Battery, Clock, Memory card, Front light and Wi-Fi all show a value, none says "Missing". The last row, **Free memory**, shows two numbers: the reader's own memory (should be 40 KB or more) and the extra memory chip (should be 2 MB or more).

**If not, send:** a photo of that list.

## 13. Fonts

**Do:** **Settings → Reading → Book font**: pick each of the fonts in turn and go back to a book after each. Then **Settings → Display & light → Menu font**, and do the same.

**Good:** Each font change takes a few seconds at most, and the book and menus redraw in the new font, with nothing overlapping or cut off.

**If not, send:** which font was slow (roughly how many seconds) or looked wrong, with a photo.

## 14. Update from a file

**Do:** On the computer, download `vellum-<version>.bin` from a release (any version, even the same one). On the reader, **Settings → Updates → Update from a file**, follow the screen to open the page on the phone or computer, and send that `.bin` file.

**Good:** The reader shows "Updating Vellum" with a percentage, restarts, and comes back to Vellum. **Settings → Device check** shows the version you sent.

**If not, send:** the message the reader showed, and the log from the moment you sent the file until it restarted.

## 15. Start-up and memory in the log

**Do:** With the log open (see [Sending a log](#sending-a-log)), press **Restart reader** on the installer page and wait for the home screen. Then hold Power to sleep, wait ten seconds, and press Power to wake. Then open **Add books**, send one book from the phone, and press Home.

**Good:** After each start the log has two lines beginning with `boot:`: the version, the screen's controller, the card, why it started (`power on`, `deep sleep`…), when the first screen appeared ("first screen at … ms"), and the free memory. Waking from sleep should show the first screen within about two seconds. No lines say `internal RAM low` or `app stack nearly full`, before or after Add books.

**If not, send:** the whole log from the restart to the end.

## 16. Battery use (only with a USB power meter)

**Do:** If you have a USB power meter, put it between the charger and the reader, charge to 100 %, and note the current: in a book with the front light off and nothing touched for a minute; then asleep (hold Power).

**Good:** Asleep, almost nothing (well under 1 mA once fully charged). Awake and idle, much less than while turning pages.

**If not, send:** the numbers for each state, and whether the front light was on.

## If the reader restarts by itself

After a crash the reader starts on the home screen in the built-in Literata font (your settings are kept; picking a font in Settings uses it again), and saves a record of the crash inside the reader. Send the log from the restart: it says `the last run crashed`. If you can run the developer tools, `idf.py coredump-info` (or `esptool.py read_flash 0xFF0000 0x10000 coredump.bin`, then send the file) shows exactly where it happened.

## Sending a log

The reader writes what it is doing to the USB cable. To capture it:

1. Plug the reader into the computer and open the `vellum-install-<version>.html` page in Chrome or Edge.
2. At the bottom, press **Show log** and pick the reader in the list.
3. Do the thing that goes wrong. To see the reader start from the beginning, press **Restart reader**.
4. Press **Copy** and paste the text into a message, or **Save as file** and attach it.

Say in a sentence what you did and what you expected. Lines starting with `E (` are errors, `W (` warnings; but please send everything, not just those.

If the reader no longer starts at all, it can always be fixed with the installer page: follow "If the reader will not connect", then run step 2 again, or step 3 to go back to the original software.
