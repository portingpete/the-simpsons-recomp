#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Simpsons::Graphics {

struct MaterialError : std::runtime_error {using std::runtime_error::runtime_error;};
struct UnsupportedMaterial : MaterialError {using MaterialError::MaterialError;};
enum class MaterialStage {Vertex, Pixel};
enum class MaterialCapability {Uncompiled, Compiled, Unsupported};

struct MaterialIdentity {
    uint32_t originalAddress;
    MaterialStage stage;
    uint32_t recordBytes;
    std::string_view originalName; // Empty for the sixteen stripped startup records.
    std::string_view sha256;
};

// Exact US-image identities only. Neither caller data nor a requested stage can
// extend this allowlist. Full image identity remains the caller's responsibility.
std::span<const MaterialIdentity> originalMaterialIdentities() noexcept;

struct MaterialMetadata {
    std::array<uint32_t,9> headerWords{}; // Unknown fields preserved, not interpreted.
    uint32_t headerBytes{},payloadBytes{};
    uint32_t debugOffset{},reflectionOffset{},constantMetadataOffset{};
    uint32_t codeMetadataOffset{},prefixBytes{},codeBytes{};
};

class MaterialRegistry;
class MaterialRecord {
public:
    MaterialRecord(const MaterialRecord&)=delete;
    MaterialRecord& operator=(const MaterialRecord&)=delete;
    const MaterialIdentity& identity() const noexcept {return *identity_;}
    const MaterialMetadata& metadata() const noexcept {return metadata_;}
    std::span<const uint8_t> bytes() const noexcept {return bytes_;}
    std::span<const uint8_t> header() const noexcept;
    std::span<const uint8_t> payload() const noexcept;
    std::span<const uint8_t> prefix() const noexcept;
    std::span<const uint8_t> code() const noexcept;
    // Raw tail of the header starting at the verified code metadata offset.
    // Only its first two words are decoded here. No instruction interpreter.
    std::span<const uint8_t> codeMetadata() const noexcept;
private:
    friend class MaterialRegistry;
    MaterialRecord(const MaterialIdentity&,std::span<const uint8_t>,std::pmr::memory_resource*);
    const MaterialIdentity* identity_;
    std::pmr::vector<uint8_t> bytes_;
    MaterialMetadata metadata_;
};

// Native-only identity, NOT a guest address, SDK object, COM pointer, or uint32
// token. A separate engine bridge must map guest slots to these complete IDs;
// truncating an ID discards stale-reference protection. Zero generation is invalid.
struct MaterialId {
    uint64_t generation{};
    uint32_t slot{};
    friend bool operator==(MaterialId,MaterialId)=default;
};

// Backend adapter owns its real compiled shader inside a derived object.
// The registry never constructs one, interprets bytecode, or invents host handles.
// Production compilers may return this only after successful native compilation
// and object creation for exactly the reported original VA and stage.
// Backend destructors must not mutate the registry; such reentry is rejected.
class CompiledMaterial {
public:
    virtual ~CompiledMaterial()=0;
    uint32_t originalAddress() const noexcept {return address_;}
    MaterialStage stage() const noexcept {return stage_;}
    CompiledMaterial(const CompiledMaterial&)=delete;
    CompiledMaterial& operator=(const CompiledMaterial&)=delete;
protected:
    CompiledMaterial(uint32_t originalAddress,MaterialStage stage) noexcept
        :address_(originalAddress),stage_(stage) {}
private:
    uint32_t address_;
    MaterialStage stage_;
};

class MaterialCompiler {
public:
    virtual ~MaterialCompiler()=default;
    // Do not retain a borrowed record reference or mutate/reenter the registry.
    // UnsupportedMaterial is terminal for this resource; other exceptions are
    // transient failures and leave it Uncompiled. Null is an error, not success.
    virtual std::unique_ptr<CompiledMaterial> compile(const MaterialRecord&)=0;
};

struct MaterialLimits {
    uint32_t maxLive=1024;
    uint32_t maxReferences=UINT32_MAX;
};

// Single-owner service: caller serializes access, like the native immediate
// context. Every create owns a distinct record with one reference (no dedup).
// Borrowed records/artifacts remain valid only until final release/reset/destruction.
// Retain IDs for deferred native work. Nothing here binds a device or writes guest
// memory. Original 82445278/82441708 must never consume these native resources.
class MaterialRegistry {
public:
    explicit MaterialRegistry(MaterialLimits limits={},
                              std::pmr::memory_resource* memory=std::pmr::get_default_resource());
    ~MaterialRegistry();
    MaterialRegistry(const MaterialRegistry&)=delete;
    MaterialRegistry& operator=(const MaterialRegistry&)=delete;
    MaterialRegistry(MaterialRegistry&&)=delete;
    MaterialRegistry& operator=(MaterialRegistry&&)=delete;

    // Supply the exact record span, not a whole image or a guessed unbounded
    // pointer. Metadata is decoded from an immutable owned copy and its full
    // SHA256 must equal the pinned identity before an ID is published.
    // Allocation/validation failure leaves all existing IDs/resources unchanged.
    MaterialId create(uint32_t originalAddress,std::span<const uint8_t> record);
    void retain(MaterialId);
    void release(MaterialId);
    void reset(); // Invalidates every live ID; generations are never reset/reused.
    bool contains(MaterialId) const noexcept;
    size_t liveCount() const noexcept {return live_;}
    uint32_t referenceCount(MaterialId) const;
    const MaterialRecord& record(MaterialId) const;
    MaterialCapability capability(MaterialId) const;
    std::string_view unsupportedReason(MaterialId) const;

    // Both guards throw unless a real adapter-owned artifact is available.
    // prepareForBind lazily invokes the supplied compiler at most once after
    // success or explicit Unsupported; transient failure may be retried.
    // An unsupported exception must stop the native bind/draw, not fall through
    // to the original SDK or return a success-only result to its caller.
    const CompiledMaterial& requireCompiled(MaterialId) const;
    const CompiledMaterial& prepareForBind(MaterialId,MaterialCompiler&);

private:
    struct Entry;
    struct Slot {
        uint64_t generation{};
        std::unique_ptr<Entry> entry;
        Slot();
        ~Slot();
        Slot(Slot&&) noexcept;
        Slot& operator=(Slot&&) noexcept;
    };
    Entry& get(MaterialId);
    const Entry& get(MaterialId) const;
    void requireMutable() const;
    void clearEntries() noexcept;
    MaterialLimits limits_;
    std::pmr::memory_resource* memory_; // Must outlive this registry.
    std::pmr::vector<Slot> slots_;
    size_t live_{};
    bool callbacksActive_{};
};

}
