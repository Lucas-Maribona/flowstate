#include "Layout.hpp"

#include <QtTest/QTest>
#include <algorithm>
#include <vector>

using flowstate::Rect;
using flowstate::tile;

class LayoutTest : public QObject {
    Q_OBJECT

private slots:
    void standardLayouts()
    {
        QCOMPARE(tile(960, 600, 0, 12), std::vector<Rect>{});
        QCOMPARE(tile(960, 600, 1, 12), (std::vector<Rect>{{12, 12, 936, 576}}));
        QCOMPARE(tile(960, 600, 2, 12), (std::vector<Rect>{
            {12, 12, 462, 576}, {486, 12, 462, 576}}));
        QCOMPARE(tile(960, 600, 3, 12), (std::vector<Rect>{
            {12, 12, 462, 576}, {486, 12, 462, 282}, {486, 306, 462, 282}}));
    }

    void resizingAndGaps()
    {
        const auto odd = tile(961, 601, 3, 12);
        QCOMPARE(odd[0].x, 12);
        QCOMPARE(odd[0].width, 462);
        QCOMPARE(odd[1].x, 486);
        QCOMPARE(odd[1].width, 463);
        QCOMPARE(odd[2].y + odd[2].height, 589);
        QCOMPARE(tile(600, 400, 3, 12), (std::vector<Rect>{
            {12, 12, 282, 376}, {306, 12, 282, 182}, {306, 206, 282, 182}}));
        for (const auto &rect : tile(30, 20, 3, 128)) {
            QVERIFY(rect.width > 0);
            QVERIFY(rect.height > 0);
        }
    }

    void smallOutput()
    {
        QCOMPARE(tile(1, 3, 3, 12), (std::vector<Rect>{
            {0, 0, 1, 1}, {0, 1, 1, 1}, {0, 2, 1, 1}}));
        QCOMPARE(tile(2, 2, 4, 12), (std::vector<Rect>{
            {0, 0, 1, 1}, {1, 0, 1, 1}, {0, 1, 1, 1}, {1, 1, 1, 1}}));
        QVERIFY(tile(1, 1, 4, 12).empty());
        QVERIFY(tile(0, 10, 2, 12).empty());
    }

    void manyWindowsNeverOverlap()
    {
        for (int width = 1; width <= 8; ++width) {
            for (int height = 1; height <= 8; ++height) {
                for (int count = 1; count <= std::min(width * height, 12); ++count) {
                    const auto rectangles = tile(width, height, count, 12);
                    QCOMPARE(rectangles.size(), static_cast<std::size_t>(count));
                    for (std::size_t i = 0; i < rectangles.size(); ++i) {
                        const auto &a = rectangles[i];
                        QVERIFY(a.x >= 0 && a.y >= 0 && a.width > 0 && a.height > 0);
                        QVERIFY(a.x + a.width <= width && a.y + a.height <= height);
                        for (std::size_t j = 0; j < i; ++j) {
                            const auto &b = rectangles[j];
                            QVERIFY(a.x + a.width <= b.x || b.x + b.width <= a.x ||
                                    a.y + a.height <= b.y || b.y + b.height <= a.y);
                        }
                    }
                }
            }
        }
    }
};

QTEST_GUILESS_MAIN(LayoutTest)
#include "layout_test.moc"
