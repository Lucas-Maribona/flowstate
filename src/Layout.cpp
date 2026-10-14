#include "Layout.hpp"

#include <algorithm>

namespace flowstate {

std::vector<Rect> tile(int width, int height, std::size_t windows, int gap)
{
    if (windows == 0 || width <= 0 || height <= 0)
        return {};
    if (windows > static_cast<std::size_t>(width) * height)
        return {};

    std::vector<Rect> result;
    result.reserve(windows);
    if (windows == 1) {
        const int g = std::clamp(gap, 0, std::min((width - 1) / 2, (height - 1) / 2));
        result.push_back({g, g, width - 2 * g, height - 2 * g});
        return result;
    }

    const auto stack = windows - 1;
    if (width >= 2 && stack <= static_cast<std::size_t>(height)) {
        const int rows = static_cast<int>(stack);
        const int g = std::clamp(gap, 0,
            std::min((width - 2) / 3, (height - rows) / (rows + 1)));
        const int usableWidth = width - 2 * g;
        const int usableHeight = height - 2 * g;
        const int masterWidth = (usableWidth - g) / 2;
        const int stackX = g + masterWidth + g;
        const int stackWidth = width - g - stackX;
        const int stackHeight = usableHeight - static_cast<int>(stack - 1) * g;
        result.push_back({g, g, masterWidth, usableHeight});
        for (std::size_t i = 0; i < stack; ++i) {
            const int y0 = g + static_cast<int>(i * stackHeight / stack + i * g);
            const int y1 = g + static_cast<int>((i + 1) * stackHeight / stack + i * g);
            result.push_back({stackX, y0, stackWidth, y1 - y0});
        }
        return result;
    }

    // A stack cannot fit in one column. Use just enough grid columns to keep
    // every rectangle disjoint and at least one pixel wide and high.
    const auto columns = (windows - 1) / static_cast<std::size_t>(height) + 1;
    const auto rows = (windows - 1) / columns + 1;
    const int cols = static_cast<int>(columns);
    const int lines = static_cast<int>(rows);
    const int g = std::clamp(gap, 0,
        std::min((width - cols) / (cols + 1), (height - lines) / (lines + 1)));
    const int contentWidth = width - (cols + 1) * g;
    const int contentHeight = height - (lines + 1) * g;
    for (std::size_t i = 0; i < windows; ++i) {
        const int col = static_cast<int>(i % columns);
        const int row = static_cast<int>(i / columns);
        const int x0 = g + col * g + col * contentWidth / cols;
        const int x1 = g + col * g + (col + 1) * contentWidth / cols;
        const int y0 = g + row * g + row * contentHeight / lines;
        const int y1 = g + row * g + (row + 1) * contentHeight / lines;
        result.push_back({x0, y0, x1 - x0, y1 - y0});
    }
    return result;
}

} // namespace flowstate
