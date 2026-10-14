#pragma once

#include <cstddef>
#include <vector>

namespace flowstate {

struct Rect {
    int x, y, width, height;
    bool operator==(const Rect &) const = default;
};

// One master tile on the left, remaining tiles stacked on the right.
// The requested gap is reduced when an output is too small to fit it.
std::vector<Rect> tile(int width, int height, std::size_t windows, int gap);

} // namespace flowstate
