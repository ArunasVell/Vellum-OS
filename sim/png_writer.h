#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sim {

// Writes an 8-bit grayscale PNG (uncompressed deflate; fine for screenshots).
bool write_gray_png(const std::string& path, int width, int height, const std::vector<uint8_t>& pixels);

}  // namespace sim
