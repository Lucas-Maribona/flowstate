#include "Layout.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

using flowstate::Rect;
using flowstate::tile;

static void check(bool condition)
{
    if (!condition) {
        std::fputs("Layout assertion failed\n", stderr);
        std::abort();
    }
}

int main()
{
    check(tile(960, 600, 0, 12).empty());
    check((tile(960, 600, 1, 12) == std::vector<Rect>{{12, 12, 936, 576}}));
    check((tile(960, 600, 2, 12) == std::vector<Rect>{
        {12, 12, 462, 576}, {486, 12, 462, 576}}));
    check((tile(960, 600, 3, 12) == std::vector<Rect>{
        {12, 12, 462, 576}, {486, 12, 462, 282}, {486, 306, 462, 282}}));

    // Odd dimensions distribute the remainder without losing pixels.
    const auto odd = tile(961, 601, 3, 12);
    check(odd[0].x == 12 && odd[0].width == 462);
    check(odd[1].x == 486 && odd[1].width == 463);
    check(odd[2].y + odd[2].height == 589);

    const auto resized = tile(600, 400, 3, 12);
    check(resized[0] == Rect(12, 12, 282, 376));
    check(resized[1] == Rect(306, 12, 282, 182));
    check(resized[2] == Rect(306, 206, 282, 182));

    // A too-large gap is reduced; every normal-sized tile remains positive.
    const auto small = tile(30, 20, 3, 128);
    for (const auto &rect : small)
        check(rect.width > 0 && rect.height > 0);

    for (const auto &rect : tile(1, 1, 4, 12))
        check(rect.width == 1 && rect.height == 1);
}
