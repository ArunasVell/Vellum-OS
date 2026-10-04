#pragma once

#include "vellum/hal.h"

namespace vellum {

// Main loop. Returns only when the input source reports Quit (simulator);
// on hardware it never returns.
void run(Board& board);

}  // namespace vellum
