// The reader's own pictures for the sleep screen: JPEG, PNG and BMP files
// in the card's sleep/ folder, put there by hand or from the phone page.
//
// Each picture is converted once into a screen picture in vellum/sleep/
// (covers::make_screen), named after the picture's name and size, so
// sleeping only reads a few hundred kilobytes and never decodes a large
// file. A picture that cannot be decoded leaves a short marker file there
// instead, so it is not tried again on every sleep. Scanning the folder
// deletes the files of pictures that are gone or have changed.
//
// Pictures can be marked. Shuffle shows a different one at each sleep,
// from the marked pictures (all of them when none is marked); One picture
// shows the marked one (the first picture when none is). The marks and the
// picture shown last are kept by name in vellum/sleep.txt.
#pragma once

#include <cstddef>
#include <cstdint>

#include "vellum/hal.h"
#include "vellum/settings.h"

namespace vellum {

// Whether a file is a picture for the sleep screen by its name: .jpg,
// .jpeg, .png or .bmp (any case), not hidden.
bool is_picture_file(const char* name);

// Vellum's own sleep pictures, quiet Lithuanian nature scenes drawn by
// tools/make_nature_pictures.py, built into the software as PNG files.
struct BuiltinPicture {
  const char* name;  // its name in the sleep folder
  const uint8_t* data;
  uint32_t size;
};
extern const BuiltinPicture kBuiltinPictures[];
extern const int kBuiltinPictureCount;

// Puts the built-in pictures in the card's sleep folder, once per card: a
// note in kBuiltinPicturesPath remembers it, so pictures the reader deleted
// do not come back. A picture of the same name already there is kept.
// Returns how many were added.
constexpr const char* kBuiltinPicturesPath = "vellum/builtin-pictures.txt";
int add_builtin_pictures(Storage& storage);

class SleepPictures {
 public:
  static constexpr int kMax = 64;
  static constexpr const char* kFolder = "sleep";
  static constexpr const char* kCacheFolder = "vellum/sleep";
  static constexpr const char* kStatePath = "vellum/sleep.txt";

  struct Picture {
    char name[96] = {};  // in kFolder
    uint32_t size = 0;
    bool marked = false;
  };

  SleepPictures();
  ~SleepPictures();
  SleepPictures(const SleepPictures&) = delete;
  SleepPictures& operator=(const SleepPictures&) = delete;

  // Lists the pictures (sorted by name), reads the marks, and deletes
  // screen pictures nothing matches any more.
  void scan(Storage& storage);
  int count() const { return count_; }
  const Picture& at(int i) const { return pictures_[i]; }
  int marked_count() const;
  void set_mark(int i, bool on) { pictures_[i].marked = on; }
  void clear_marks();
  bool save(Storage& storage) const;

  // Where picture i's screen picture is kept.
  void cache_path(int i, char* out, size_t size) const;
  // Whether picture i was converted already (or found not to decode).
  bool ready(Storage& storage, int i) const;
  // Makes picture i's screen picture when it has none yet. Returns false
  // when the picture cannot be decoded.
  bool prepare(Storage& storage, int i);

  // The picture to show at this sleep, its screen picture made if needed;
  // -1 when there is none that decodes. `seed` varies the shuffle;
  // `remember` keeps it as the one shown last (a preview does not).
  int choose(Storage& storage, SleepOrder order, uint32_t seed, bool remember);

 private:
  Picture* pictures_;
  int count_ = 0;
  char last_[96] = {};  // the name shown last
  char* file_;          // for loading and saving the state, allocated once
};

}  // namespace vellum
