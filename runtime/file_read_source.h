#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace Simpsons {
// Observer-only: which recent successful guest file reads deposited the bytes at a guest address range.
// Reads are recorded only while a resource audit file is active. Each byte is attributed to the newest
// recorded read that wrote it (no content comparison). Covered: one read supplied the whole range.
// Spanning: several reads of the SAME open did, and their file offsets map linearly (a block spanning
// consecutive ring chunks). Ambiguous: a gap in the recorded history or a non-linear mix (ring reuse);
// the newest overlapping read is then reported. None: no recorded read overlaps.
struct FileReadSource {
    enum class Status {None,Covered,Spanning,Ambiguous} status{Status::None};
    uint64_t openId{},readOrdinal{},sequence{},fileOffset{},fileExtent{};
    uint32_t destination{},completed{},readCount{};
    // The last-writer pieces (lowest address first, at most eight; readCount is the true number) that resolved the range.
    struct Piece {uint32_t begin{},end{};uint64_t fileOffset{},readOrdinal{},openId{},sequence{};};
    std::array<Piece,8> pieces{};
    std::string relative,declared,root;
};
}
