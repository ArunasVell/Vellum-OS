#include "vellum/library.h"

#include <algorithm>

namespace vellum {

Library::Library(std::span<const Book* const> books) {
  count_ = static_cast<int>(std::min<size_t>(books.size(), kMaxBooks));
  std::copy_n(books.begin(), count_, books_);
}

void Library::touch(int i) { states_[i].last_opened = ++clock_; }

int Library::current() const {
  int best = 0;
  for (int i = 1; i < count_; ++i) {
    if (states_[i].last_opened > states_[best].last_opened) best = i;
  }
  return best;
}

int Library::by_recent(int* out, int max) const {
  int n = std::min(max, count_);
  int order[kMaxBooks];
  for (int i = 0; i < count_; ++i) order[i] = i;
  // Stable, so unopened books (all 0) keep library order.
  std::stable_sort(order, order + count_,
                   [&](int a, int b) { return states_[a].last_opened > states_[b].last_opened; });
  std::copy_n(order, n, out);
  return n;
}

}  // namespace vellum
