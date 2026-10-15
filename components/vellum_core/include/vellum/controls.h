// What the buttons do: an action for each way of pressing them, one set
// while reading and one in menus (Settings › Buttons).
#pragma once

#include <cstdint>
#include <span>

#include "vellum/hal.h"

namespace vellum {

// A way of pressing a button. A press is held when it lasts kHoldKeyMs.
// Holding Power is not among them: it always puts the reader to sleep.
enum class ButtonInput : uint8_t { Prev, Next, Power, Home, PrevHeld, NextHeld, HomeHeld };
constexpr int kButtonInputs = 7;
constexpr uint32_t kHoldKeyMs = 600;

enum class ButtonAction : uint8_t {
  None,
  // While reading
  PrevPage,
  NextPage,
  PrevChapter,
  NextChapter,
  Contents,
  Home,
  Library,
  Bookmark,     // adds or removes the page's bookmark
  LightToggle,  // front light off, or back on as it was
  LightScreen,  // opens the front light screen
  AutoTurn,     // starts or stops turning pages by itself
  FullRefresh,  // redraws the screen with a flash, clearing ghosting
  Screenshot,   // saves the screen to screenshots/ on the card
  Sleep,
  // In menus
  Up,
  Down,
  Select,  // opens or changes the highlighted item
  Back,    // back or home, as the Home button did
  PageUp,
  PageDown,
};
constexpr int kButtonActions = 21;

struct ButtonMap {
  ButtonAction reader[kButtonInputs];
  ButtonAction menu[kButtonInputs];

  bool operator==(const ButtonMap&) const = default;
};

// The side buttons turn pages and move the highlight, Power opens the
// contents or the highlighted item, Home goes back or home. In menus,
// holding a side button goes back too, so the buttons alone always lead out
// (the Home pad needs the touch controller; while reading, the contents
// lead home). Page turns stay on the press, not waiting to see if it's held.
constexpr ButtonMap kDefaultButtons = {
    {ButtonAction::PrevPage, ButtonAction::NextPage, ButtonAction::Contents, ButtonAction::Home, ButtonAction::None,
     ButtonAction::None, ButtonAction::None},
    {ButtonAction::Up, ButtonAction::Down, ButtonAction::Select, ButtonAction::Back, ButtonAction::Back,
     ButtonAction::Back, ButtonAction::None},
};

// The defaults before holding a side button went back in menus: a saved
// set equal to one of these was never chosen, and loads as the new default.
constexpr ButtonMap kFormerDefaultButtons = {
    {ButtonAction::PrevPage, ButtonAction::NextPage, ButtonAction::Contents, ButtonAction::Home, ButtonAction::None,
     ButtonAction::None, ButtonAction::None},
    {ButtonAction::Up, ButtonAction::Down, ButtonAction::Select, ButtonAction::Back, ButtonAction::None,
     ButtonAction::None, ButtonAction::None},
};

// The actions offered while reading, or in menus, in the order Settings
// cycles through them.
std::span<const ButtonAction> button_actions(bool reading);

// A set leaves a way out: while reading, some press opens the contents,
// the home screen or the library; in menus, some press selects, some goes
// back, and some moves the highlight. (Touch works as before either way.)
bool buttons_safe(const ButtonAction (&set)[kButtonInputs], bool reading);

// The action after input's current one, skipping those that are not
// offered or would leave the set unsafe; the current one when none would do.
ButtonAction next_button_action(const ButtonAction (&set)[kButtonInputs], bool reading, int input);

// The press of `key` as an input, held or not. Power held is not one: -1.
int button_input(Key key, bool held);

}  // namespace vellum
