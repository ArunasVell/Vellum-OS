/* Vellum: only miniz's inflater (tinfl) is vendored, from miniz 3.0.2
 * (https://github.com/richgel999/miniz, MIT licence in LICENSE). Its symbols
 * get a vellum_ prefix so they cannot clash with the copy in the ESP32 ROM. */
#pragma once

#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_MALLOC

#define tinfl_decompress vellum_tinfl_decompress
#define tinfl_decompress_mem_to_heap vellum_tinfl_decompress_mem_to_heap
#define tinfl_decompress_mem_to_mem vellum_tinfl_decompress_mem_to_mem
#define tinfl_decompress_mem_to_callback vellum_tinfl_decompress_mem_to_callback
#define tinfl_decompressor_alloc vellum_tinfl_decompressor_alloc
#define tinfl_decompressor_free vellum_tinfl_decompressor_free

#include "miniz_common.h"
#include "miniz_tinfl.h"
