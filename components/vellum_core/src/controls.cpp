#include "vellum/controls.h"

#include <algorithm>

namespace vellum {
namespace {

using A = ButtonAction;

constexpr A kReading[] = {A::None,        A::PrevPage,   A::NextPage, A::PrevChapter, A::NextChapter, A::Contents,
                          A::Home,        A::Library,    A::Bookmark, A::LightToggle, A::LightScreen, A::AutoTurn,
                          A::FullRefresh, A::Screenshot, A::Sleep,    A::QuickPanel};
// The global ones that make sense anywhere come after the menu's own.
constexpr A kMenus[] = {A::None,     A::Up,          A::Down,        A::Select,     A::Back,  A::PageUp,
                        A::PageDown, A::LightToggle, A::FullRefresh, A::Screenshot, A::Sleep, A::QuickPanel};

bool has(const ButtonAction (&set)[kButtonInputs], std::initializer_list<A> any) {
  return std::any_of(std::begin(set), std::end(set),
                     [&](A a) { return std::find(any.begin(), any.end(), a) != any.end(); });
}

}  // namespace

std::span<const ButtonAction> button_actions(bool reading) {
  if (reading) return kReading;
  return kMenus;
}

bool buttons_safe(const ButtonAction (&set)[kButtonInputs], bool reading) {
  const std::span<const A> offered = button_actions(reading);
  for (A a : set) {
    if (std::find(offered.begin(), offered.end(), a) == offered.end()) return false;
  }
  if (reading) return has(set, {A::Contents, A::Home, A::Library});
  return has(set, {A::Select}) && has(set, {A::Back}) && has(set, {A::Up, A::Down});
}

ButtonAction next_button_action(const ButtonAction (&set)[kButtonInputs], bool reading, int input) {
  const std::span<const A> offered = button_actions(reading);
  const auto at = std::find(offered.begin(), offered.end(), set[input]);
  const int start = at == offered.end() ? 0 : static_cast<int>(at - offered.begin());
  const int n = static_cast<int>(offered.size());
  for (int k = 1; k < n; ++k) {
    ButtonAction trial[kButtonInputs];
    std::copy(std::begin(set), std::end(set), trial);
    trial[input] = offered[(start + k) % n];
    if (buttons_safe(trial, reading)) return trial[input];
  }
  return set[input];
}

int button_input(Key key, bool held) {
  switch (key) {
    case Key::Prev:
      return static_cast<int>(held ? ButtonInput::PrevHeld : ButtonInput::Prev);
    case Key::Next:
      return static_cast<int>(held ? ButtonInput::NextHeld : ButtonInput::Next);
    case Key::Home:
      return static_cast<int>(held ? ButtonInput::HomeHeld : ButtonInput::Home);
    case Key::Power:
      break;
  }
  return held ? -1 : static_cast<int>(ButtonInput::Power);
}

}  // namespace vellum
