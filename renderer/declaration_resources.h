#pragma once
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <span>
#include <stdexcept>
#include <vector>

namespace Simpsons::Graphics {

struct DeclarationError : std::runtime_error {using std::runtime_error::runtime_error;};
struct UnsupportedDeclaration : DeclarationError {using DeclarationError::DeclarationError;};

// A host identity, never an SDK/COM object, guest address or truncated token.
// A future guest bridge must preserve the complete ID in its own owner mapping.
struct DeclarationId {
    uint64_t generation{};
    uint32_t slot{};
    friend bool operator==(DeclarationId,DeclarationId)=default;
};

// Decoded fields, NOT a packed host overlay of the 12-byte BE input. The trailing
// byte is preserved even though the observed original stack builders omit it.
struct DeclarationElement {
    uint16_t stream{},offset{};
    uint32_t type{};
    uint8_t method{},usage{},usageIndex{},opaque{};
    uint32_t storageBytes{}; // Raw source extent only, not a native input layout.
};

class DeclarationRegistry;
class DeclarationRecord {
public:
    DeclarationRecord(const DeclarationRecord&)=delete;
    DeclarationRecord& operator=(const DeclarationRecord&)=delete;
    std::span<const uint8_t> bytes() const noexcept {return bytes_;}
    std::span<const DeclarationElement> elements() const noexcept {return elements_;}
    // Largest offset+storageBytes in stream 0. This does NOT establish stride.
    uint32_t minimumStreamBytes() const noexcept {return minimumStreamBytes_;}
private:
    friend class DeclarationRegistry;
    DeclarationRecord(std::span<const uint8_t>,std::pmr::memory_resource*);
    std::pmr::vector<uint8_t> bytes_; // Includes the complete terminal record.
    std::pmr::vector<DeclarationElement> elements_; // Excludes the terminator.
    uint32_t minimumStreamBytes_{};
};

struct DeclarationLimits {
    uint32_t maxCached=1024; // Native safety bound, not a recovered SDK limit.
    uint32_t maxReferences=UINT32_MAX;
};

// Immutable CPU declaration ownership only. No device, guest writes, binding,
// shader compatibility claim or engine-ready result. Caller serializes access.
// Every create/retain owns one logical reference; shared record snapshots do
// not add logical references and can outlive final release/reset/destruction.
class DeclarationRegistry {
public:
    static constexpr size_t ElementBytes=12;
    static constexpr size_t MaxElements=64; // Native bounded-reader policy.
    explicit DeclarationRegistry(DeclarationLimits limits={},
        std::pmr::memory_resource* memory=std::pmr::get_default_resource());
    ~DeclarationRegistry();
    DeclarationRegistry(const DeclarationRegistry&)=delete;
    DeclarationRegistry& operator=(const DeclarationRegistry&)=delete;
    DeclarationRegistry(DeclarationRegistry&&)=delete;
    DeclarationRegistry& operator=(DeclarationRegistry&&)=delete;

    // Exact bounded span, including one final terminal record. The supported
    // subset is documented in native-declarations.md. No unbounded guest scan.
    // Exact byte equality deduplicates, including opaque bytes and terminator.
    // Failure leaves all existing IDs, cache entries and refcounts unchanged.
    DeclarationId create(std::span<const uint8_t> originalRecords);
    void retain(DeclarationId);
    void release(DeclarationId);
    void reset() noexcept;
    bool contains(DeclarationId) const noexcept;
    uint32_t referenceCount(DeclarationId) const;
    std::shared_ptr<const DeclarationRecord> record(DeclarationId) const;
    size_t liveCount() const noexcept {return live_;}
    size_t cachedCount() const noexcept {return entries_.size();}

private:
    struct Entry {
        std::shared_ptr<const DeclarationRecord> record;
        uint64_t generation{};
        uint32_t references{};
    };
    Entry& get(DeclarationId);
    const Entry& get(DeclarationId) const;
    DeclarationLimits limits_;
    // Must outlive registry AND every returned record snapshot. Used for owned
    // data/cache allocations, allowing real allocation-failure tests.
    std::pmr::memory_resource* memory_;
    std::pmr::vector<Entry> entries_;
    size_t live_{};
};

}
