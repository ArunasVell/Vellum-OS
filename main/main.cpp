#include "board_x4pro.h"
#include "vellum/app.h"

extern "C" void app_main() { vellum::run(board_x4pro_init()); }
