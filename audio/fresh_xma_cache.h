#pragma once
#include "xma_source.h"
#include <cstdint>
#include <memory>

namespace Simpsons::Audio {
// Optional reuse of actual immutable fresh-context decoder results. It owns
// neither guest data nor progress; every caller receives an independent source.
// Continuations remain ordinary XmaSource owners with their real codec state.
// A miss prepared with Limits::prepareFrames is decoded to completion by one background
// worker (off the caller's thread) and then published like any other fresh image.
class FreshXmaCache {
    struct State;
    std::unique_ptr<State> state;
public:
    struct Result {
        std::shared_ptr<XmaSource> source;
        XmaSource::Receipt receipt;
        bool reused{};
    };
    struct Snapshot {
        uint64_t hits{},misses{},bypasses{},evictions{},bytes{};
        uint32_t entries{},pinned{};
    };
    explicit FreshXmaCache(uint64_t maxBytes=64ull*1024*1024,uint32_t maxEntries=128);
    ~FreshXmaCache();
    Result prepare(NativeXmaFactory&,uint64_t instanceGeneration,std::span<const XmaFormat>,
                   const XmaSource::Segment&,XmaSource::Limits);
    Snapshot snapshot() const;
};
}
