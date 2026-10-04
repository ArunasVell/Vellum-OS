#include "vellum/book.h"

namespace vellum {

std::span<const Book* const> builtin_books() {
  static constexpr const Book* kBooks[] = {&books::alice, &books::moby_dick, &books::persuasion, &books::father_brown};
  return kBooks;
}

}  // namespace vellum
