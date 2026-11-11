#include "canvas/SparseTileStore.h"
#include <cstdlib>
#include <iostream>
#include <memory>
using namespace komapaint;
static bool pressure = false;
static bool fakeMemoryCheck(std::size_t) noexcept { return !pressure; }
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "FAIL line " << __LINE__ << ": " << #__VA_ARGS__ << '\n'; std::exit(1); } } while (false)
int main()
{
    using R = SparseTileStore::WriteResult;
    constexpr std::size_t tile = 4 * 4 * 4;
    auto budget = std::make_shared<DocumentMemoryBudget>(tile * 2);
    {
        SparseTileStore first(8, 4, 4), second(8, 4, 4);
        CHECK(first.setDocumentBudget(budget)); CHECK(second.setDocumentBudget(budget));
        CHECK(first.setPixel({0, 0}, 0x80402010U) == R::Changed);
        CHECK(second.setPixel({0, 0}, 0xffabcdefU) == R::Changed);
        CHECK(budget->artworkBytes() == tile * 2); CHECK(budget->stagingBytes() == 0);
        CHECK(!budget->setLimit(tile));
        CHECK(first.setPixel({4, 0}, 0xff000000U) == R::DocumentMemoryLimit);
        CHECK(first.pixel({4, 0}) == 0U); CHECK(budget->usedBytes() == tile * 2);
        CHECK(!first.setDocumentBudget(std::make_shared<DocumentMemoryBudget>()));
        // Replacing a tile needs temporary staging; without it the entire
        // restore must fail, even though the final tile count is unchanged.
        std::vector<SparseTileStore::TileSnapshot> restore{{{0, 0}, std::vector<std::uint32_t>(16, 0xff123456U)}};
        CHECK(first.restoreTiles(restore) == R::DocumentMemoryLimit);
        CHECK(first.pixel({0, 0}) == 0x80402010U); CHECK(budget->stagingBytes() == 0);
        CHECK(budget->setLimit(tile * 3));
        CHECK(first.restoreTiles(restore) == R::Changed);
        CHECK(first.pixel({0, 0}) == 0xff123456U);
        CHECK(second.pixel({0, 0}) == 0xffabcdefU);
        CHECK(budget->usedBytes() == tile * 2); CHECK(budget->peakBytes() == tile * 3);
        // Failed validation returns staging reservation and preserves artwork.
        restore.push_back(restore.front());
        CHECK(budget->setLimit(tile * 4));
        CHECK(first.restoreTiles(restore) == R::InvalidPixel);
        CHECK(first.pixel({0, 0}) == 0xff123456U);
        CHECK(budget->usedBytes() == tile * 2); CHECK(budget->stagingBytes() == 0);
        CHECK(first.setPixel({0, 0}, 0) == R::Changed); // other pixels still nonzero
        std::vector<SparseTileStore::TileSnapshot> remove{{{0, 0}, {}}};
        CHECK(first.restoreTiles(remove) == R::Changed);
        CHECK(first.allocatedTiles() == 0); CHECK(budget->usedBytes() == tile);
        CHECK(budget->setLimit(tile * 2));
        CHECK(second.setPixel({4, 0}, 0xff000000U) == R::Changed);
        CHECK(budget->usedBytes() == tile * 2);
    }
    CHECK(budget->usedBytes() == 0); // layer/store destruction releases reservations
    {
        DocumentMemoryBudget underPressure(std::numeric_limits<std::size_t>::max(), fakeMemoryCheck);
        CHECK(underPressure.reserve(DocumentMemoryBudget::Category::Artwork, 64));
        pressure = true;
        CHECK(!underPressure.reserve(DocumentMemoryBudget::Category::History, 1));
        CHECK(!underPressure.reserve(DocumentMemoryBudget::Category::RestoreStaging, 1));
        CHECK(underPressure.reserve(DocumentMemoryBudget::Category::History, 0));
        CHECK(underPressure.usedBytes() == 64);
        pressure = false;
        CHECK(underPressure.reserve(DocumentMemoryBudget::Category::History, 1));
        underPressure.release(DocumentMemoryBudget::Category::History, 1);
        underPressure.release(DocumentMemoryBudget::Category::Artwork, 64);
        CHECK(underPressure.usedBytes() == 0);
    }
    std::cout << "Document pixel budget tests passed\n";
}
