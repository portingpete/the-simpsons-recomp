#pragma once
#include "runtime.h"
#include <span>

namespace Simpsons {
// Observes the original allocation/claim/free operations. This class never
// allocates, releases or modifies a guest reader, node or ring.
class EngineAudioReader {
    struct State;
    std::unique_ptr<State> state;
public:
    struct OwnedClaim {
        uint32_t node{},handle{},manager{},group{},token{},address{},length{},owner{};
        uint64_t groupGeneration{},managerGeneration{},epoch{},sequence{};
        std::vector<uint8_t> bytes;
    };
    struct Snapshot {size_t groups{},managers{},claims{},copies{},operations{};};
    explicit EngineAudioReader(Runtime&);
    ~EngineAudioReader();
    void observe(uint32_t pc,PPCContext&,uint8_t*);
    OwnedClaim copy(uint32_t handle,uint32_t node,uint32_t owner,uint32_t token);
    void validate(const OwnedClaim&,bool forRelease=false) const; // Identity only; never rereads ring bytes.
    void validateReleased(const OwnedClaim&) const; // Never accesses the freed node.
    void excludeOutput(std::span<uint8_t>) const; // Compare canonical backing, without reading compressed bytes.
    void unwind(PPCContext&) noexcept;
    void requireStopped() const;
    Snapshot snapshot() const;
    // Explicit authorization for the actual-AOT fixture's direct constructor
    // invocation. Production requires a qualified original call site/profile.
    void permitFixtureGroup(PPCContext&,bool);
};
std::shared_ptr<EngineAudioReader> audioReaders(Runtime&,bool create=false);
void unwindAudioReaderCall(PPCContext&) noexcept;
}
