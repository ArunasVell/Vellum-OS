# Vellum OS: notes for contributors and agents

- `components/vellum_core` must stay portable: no ESP-IDF, FreeRTOS or SDL includes. Device access goes through `vellum/hal.h`.
- Device-specific code lives only in a board component (`components/board_<device>`). Adding a device should not change core code.
- C++20, no exceptions, no RTTI. Allocate up front; avoid heap churn in the page-turn path.
- Framebuffers are 1-bit, stored in the panel's physical orientation (MSB = leftmost pixel, 1 = white). Drawing uses logical coordinates.
- Before pushing: `cmake -S sim -B build/sim && cmake --build build/sim && ctest --test-dir build/sim`, and `idf.py build` for firmware changes.
- Format with `clang-format` (`.clang-format` in the repo root).
