#include "Layout.hpp"

#include <algorithm>

namespace flowstate {

std::vector<Rect> tile(int width, int height, std::size_t windows, int gap)
{
    if (windows == 0 || width <= 0 || height <= 0)
        return {};

    const auto stack = windows - 1;
    const int columns = windows == 1 ? 1 : 2;
    const int rows = static_cast<int>(std::min<std::size_t>(stack, height));
    const int maxHorizontalGap = std::max(0, (width - columns) / (columns + 1));
    const int maxVerticalGap = rows ? std::max(0, (height - rows) / (rows + 1))
                                    : std::max(0, (height - 1) / 2);
    const int g = std::clamp(gap, 0, std::min(maxHorizontalGap, maxVerticalGap));
    const int usableWidth = width - 2 * g;
    const int usableHeight = height - 2 * g;
    if (windows == 1)
        return {{g, g, usableWidth, usableHeight}};

    // When there are fewer pixels than columns, overlap rather than send a
    // zero-sized xdg configure (where zero means "client chooses the size").
    const bool narrow = usableWidth - g < 2;
    const int masterWidth = narrow ? usableWidth : (usableWidth - g) / 2;
    const int stackX = narrow ? g : g + masterWidth + g;
    const int stackWidth = narrow ? usableWidth : width - g - stackX;
    std::vector<Rect> result;
    result.reserve(windows);
    result.push_back({g, g, masterWidth, usableHeight});

    if (stack > static_cast<std::size_t>(usableHeight)) {
        for (std::size_t i = 0; i < stack; ++i)
            result.push_back({stackX, g + static_cast<int>(
                                  std::min(i, static_cast<std::size_t>(usableHeight - 1))),
                              stackWidth, 1});
        return result;
    }

    const int stackHeight = usableHeight - static_cast<int>(stack - 1) * g;
    for (std::size_t i = 0; i < stack; ++i) {
        const int y0 = g + static_cast<int>(i * stackHeight / stack + i * g);
        const int y1 = g + static_cast<int>((i + 1) * stackHeight / stack + i * g);
        result.push_back({stackX, y0, stackWidth, y1 - y0});
    }
    return result;
}

} // namespace flowstate
