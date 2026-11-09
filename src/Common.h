#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// A decoded image in top-down, 8-bit RGBA order.
struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;  // width * height * 4

    bool Empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
    void Clear() {
        width = 0;
        height = 0;
        pixels.clear();
    }
};
