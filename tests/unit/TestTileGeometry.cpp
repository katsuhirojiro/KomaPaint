#include "canvas/TileGeometry.h"
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace komapaint;
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "FAIL line " << __LINE__ << ": " << #__VA_ARGS__ << '\n'; std::exit(1); } } while (false)

int main()
{
    TileGeometry g(600, 513);
    CHECK(g.isValid());
    CHECK(g.tileSize() == 256);
    CHECK((g.address({0, 0}) == TileAddress{{0, 0}, {0, 0}}));
    CHECK((g.address({255, 255}) == TileAddress{{0, 0}, {255, 255}}));
    CHECK((g.address({256, 256}) == TileAddress{{1, 1}, {0, 0}}));
    CHECK((g.address({599, 512}) == TileAddress{{2, 2}, {87, 0}}));
    CHECK(!g.address({-1, 0})); CHECK(!g.address({0, -1}));
    CHECK(!g.address({600, 0})); CHECK(!g.address({0, 513}));
    CHECK((g.clip({-100, -10, 10, 20}) == TileRect{0, 0, 10, 20}));
    CHECK(!g.clip({-10, 0, 0, 1})); CHECK(!g.clip({1, 1, 1, 4}));
    CHECK(!g.clip({10, 10, 9, 20})); CHECK(!g.clip({600, 0, 700, 1}));
    CHECK((g.tilesForRect({0, 0, 256, 256}) == TileRange{{0, 0}, {0, 0}}));
    CHECK((g.tilesForRect({255, 255, 257, 257}) == TileRange{{0, 0}, {1, 1}}));
    CHECK((g.tilesForRect({-5, -5, 700, 700}) == TileRange{{0, 0}, {2, 2}}));
    CHECK(!g.tilesForRect({0, 0, 0, 1}));
    CHECK((g.tileBounds({2, 2}) == TileRect{512, 512, 600, 513}));
    CHECK(!g.tileBounds({3, 0})); CHECK(!g.tileBounds({-1, 0}));
    CHECK((g.localIntersection({1, 0}, {255, 2, 260, 300}) == TileRect{0, 2, 4, 256}));
    CHECK(!g.localIntersection({1, 0}, {0, 0, 256, 256}));
    TileGeometry small(9, 5, 4);
    CHECK((small.address({8, 4}) == TileAddress{{2, 1}, {0, 0}}));
    CHECK((small.tileBounds({2, 1}) == TileRect{8, 4, 9, 5}));
    CHECK(!TileGeometry(0, 5).isValid()); CHECK(!TileGeometry(1, 1, 0).isValid());
    CHECK(!TileGeometry(-1, 5).tilesForRect({0, 0, 1, 1}));
    const auto max = std::numeric_limits<std::int64_t>::max();
    const auto min = std::numeric_limits<std::int64_t>::min();
    TileGeometry large(max, max, 256);
    CHECK((large.clip({min, min, max, max}) == TileRect{0, 0, max, max}));
    auto last = large.address({max - 1, max - 1});
    CHECK(last.has_value());
    auto bounds = large.tileBounds(last->tile);
    CHECK(bounds && bounds->right == max && bounds->bottom == max);
    CHECK(!large.tileBounds({max, max}));
    std::cout << "Tile geometry tests passed\n";
}
