#pragma once

#include <cstddef>
#include <vector>

namespace flowstate {

struct Rect {
    int x, y, width, height;
    bool operator==(const Rect &) const = default;
};

// One master tile on the left, remaining tiles stacked on the right.
// Cramped outputs use a grid. Returns no tiles if there are fewer pixels than
// windows, because positive, non-overlapping rectangles are then impossible.
// The requested gap is reduced when needed to keep every tile positive.
std::vector<Rect> tile(int width, int height, std::size_t windows, int gap);

} // namespace flowstate
