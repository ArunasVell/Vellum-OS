# Speed and battery (October 2026)

What was measured, what changed, and what only the X4 Pro itself can confirm.

## Where a page turn's time goes

Measured in the simulator with a 720-page test book (instruction counts
under `valgrind --tool=callgrind`, which track the chip's speed better than
a desktop's clock):

| Step | Desktop | Share of the CPU work |
| --- | --- | --- |
| Lay out the next page | under 0.01 ms (pages are laid out when a chapter opens) | — |
| Draw the page into memory | 0.2 ms | almost all, 70 % of it putting glyphs on the page |
| Open a chapter | 3 ms for 60 pages, 18 ms for 400 | once per chapter |

On the ESP32-S3 the drawing is a few milliseconds. The panel is the rest: on
the X4 Pro a fast refresh is two 48 KB transfers at 10 MHz (about 40 ms
each) plus the waveform itself (a few hundred ms), and the controller's
high-voltage supply was switched off after every refresh (the SSD1677 driver
also waited a fixed 200 ms for that) and on again before the next.

## What changed

1. **Glyphs are drawn about a third faster.** Each glyph row's start is
   mapped once and the rest is stepping; a mask byte's four pixels are looked
   up at once (`Framebuffer::draw_mask`). Every screen with text gains, not
   only books. Checked against pixel-by-pixel drawing in all four rotations
   (`test_draw_mask_matches_set_pixel`).
2. **The panel stays powered for 1.5 s after a refresh.** A second page turn
   (or moving through a menu) within that time skips switching the supply
   off and on. It goes off when the time passes with nothing new to show,
   before sleep and before a restart. Switching it off no longer waits a
   fixed 200 ms: the controller's BUSY line says when it is done.
3. **The CPU slows to 80 MHz while the panel refreshes.** The refresh is
   the panel's work; the chip only waits for BUSY.
4. **Light sleep while waiting.** Between presses the chip naps (CPU,
   RAM and clocks paused, a few mA instead of tens). Just before each nap
   the three buttons and the touch line are set to wake it; just after, the
   app is told and reads them. Every 2 s it also looks at the buttons, so a
   press could never be lost for long. It stays awake while the front light
   is on (its PWM stops in light sleep), while the panel refreshes, while
   Wi-Fi is on, and while a computer is connected over USB (the log cannot
   work through light sleep).
5. **Presses made during a refresh are no longer lost.** A button edge is
   queued with the level it had and when; it used to be read again after the
   refresh, by which time a quick press was already let go, so a second
   press only counted once the screen was done. Presses already waiting
   when one is handled are drawn first and shown together with one refresh
   (`InputSource::pending`), so a run of quick presses through a menu or the
   keyboard catches up at once.
6. **Holding Power sleeps after 0.7 s** instead of a full second.
7. **Settings › Device check shows the last page turn's time** next to the
   screen size ("800 × 480 · page 420 ms"): from the button to the new page
   on screen.

Not changed: the waveforms themselves, the SPI speed (10 MHz, as validated
by FreeInk), and how often the full refresh runs (Settings).

## Check on the X4 Pro

1. **Buttons and touch after a pause.** Open a book and leave it untouched
   for a minute. Then press the page button once: the page turns at once.
   Leave it again, then tap the right side of the page: it turns. Leave it
   again, then press the Home pad: home appears. Do the same with Power
   (opens Contents) and holding Power (sleep).
2. **Quick presses.** In Settings or the keyboard, press the page button
   three times fast: the selection moves three places, without waiting for
   each refresh. Holding Power puts the reader to sleep a little sooner.
3. **Page turn time.** Turn about ten pages, then open **Settings › Device
   check**: the Screen row shows the last turn's time. Turn ten pages quickly
   (one every half second) and check again: it should be lower than for a
   single slow turn.
4. **Front light.** Turn the light on, wait a minute without touching
   anything: it must not flicker or go out.
5. **Battery.** Read as usual for a day with the light off. **Settings ›
   Sleep & battery › Battery** shows the drain while reading; compare it with
   a day on the previous version. With a USB power meter between a charger
   and the reader (not a computer): a book open, light off, untouched for a
   minute should now draw a few mA rather than tens.

If a press is ever slow to be noticed (about 2 s), that is the safety check
catching a missed wake-up: please say which button or touch it was.
