// An on-screen keyboard for short text: a search, a shelf's name.
//
// It takes the bottom kHeight pixels of the screen: a line with the text
// typed so far (and Cancel), then five rows of keys. The pages: English
// letters with digits on top; the same with the language's accented letters
// on top (Lithuanian ą č ę ė į š ų ū ž, German ä ö ü ß...); digits with
// punctuation; and for Ukrainian its own letters. Shift makes the next letter
// a capital.
//
// Fingers tap keys; without touch, the side buttons move a cursor over the
// keys and Power types the one under it. The keyboard draws itself and
// keeps its own text; the screen that opened it decides what Done and
// Cancel mean, and redraws what the text changes.
#pragma once

#include <cstdint>
#include <string_view>

#include "vellum/framebuffer.h"
#include "vellum/hal.h"
#include "vellum/strings.h"

namespace vellum {

class Keyboard {
 public:
  static constexpr int kMaxBytes = 64;  // longest text, UTF-8
  static constexpr int kHeight = 372;   // logical px, at the bottom of the screen

  // English letters; the same with a row of the language's accented
  // letters for the digits; symbols; the Ukrainian letters.
  enum class Page : uint8_t { English, Accents, Symbols, Ukrainian };

  // What an input did, so the screen can redraw only what changed.
  enum class Result : uint8_t {
    None,    // nothing (a tap outside the keys)
    Redraw,  // the keyboard looks different (cursor, page, shift)
    Typed,   // the text changed
    Done,    // OK
    Cancel,  // Cancel
  };

  struct Options {
    std::string_view prompt = {};              // shown in the empty text line
    std::string_view ok = "OK";                // the key that finishes
    std::string_view cancel = "";              // the button in the text line; "" leaves it out
    Page page = Page::English;                 // the letters to start with
    Language language = Language::Lithuanian;  // whose accents (or Ukrainian letters) the language key gives
    bool capitalize = false;                   // shift is on while the text is empty (names)
    int max_bytes = kMaxBytes;                 // at most kMaxBytes
  };

  // Shows the keyboard with `text` already typed.
  void open(std::string_view text, const Options& options);
  void close() { open_ = false; }
  bool is_open() const { return open_; }

  std::string_view text() const { return {text_, static_cast<size_t>(length_)}; }
  Page page() const { return page_; }
  bool shifted() const { return shift_; }

  // Where it draws: the bottom of the screen.
  Rect area(const Framebuffer& fb) const { return {0, fb.height() - kHeight, fb.width(), kHeight}; }
  // Draws the text line and the keys over area(); only that part changes.
  void draw(Framebuffer& fb) const;

  // A side button: Prev and Next move the cursor over the keys, Power
  // presses the key under it. Home is the screen's (usually Cancel).
  Result key(Key k);
  // A finger at logical (x, y): presses the key there. None outside area().
  Result tap(const Framebuffer& fb, int x, int y);

  // What the keys do, for tests and for hosts with a real keyboard.
  // type() adds UTF-8 text (false when it does not fit); backspace()
  // removes the last character.
  bool type(std::string_view s);
  void backspace();
  void set_page(Page p);

  // The keys on the current page, in cursor order, and where each is drawn.
  // Tests find a key by its label with find().
  int key_count() const;
  Rect key_rect(const Framebuffer& fb, int i) const;
  // The key whose label (as typed, for letters) is `label`, or -1.
  int find(std::string_view label) const;
  int cursor() const { return cursor_; }

  struct KeyDef;  // one key of a page's layout (keyboard.cpp)

 private:
  const KeyDef& def(int i) const;
  // The letters the symbols key returns to, and the language key's other page.
  Page letters() const { return last_letters_; }
  Page other_letters() const { return options_.language == Language::Ukrainian ? Page::Ukrainian : Page::Accents; }
  int row_of(int i) const;
  Result press(int i);
  void draw_key(Framebuffer& fb, int i) const;
  void draw_line(Framebuffer& fb) const;

  char text_[kMaxBytes + 1] = {};
  int length_ = 0;
  Options options_;
  Page page_ = Page::English;
  Page last_letters_ = Page::English;  // the letters shown last, for the symbols key
  bool shift_ = false;
  bool open_ = false;
  int cursor_ = 0;
  bool show_cursor_ = false;  // shown once a side button moves it
};

}  // namespace vellum
