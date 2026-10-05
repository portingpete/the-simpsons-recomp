#include "engine_resources.h"
#include "runtime.h"
#include "renderer/declaration_resources.h"
#include <atomic>
#include <unordered_map>

namespace {
struct ScratchOwner {
    std::shared_ptr<Simpsons::Graphics::Buffer> backing,index;
    uint32_t indexToken{};
    Simpsons::Graphics::DeclarationRegistry declarations;
    std::unordered_map<uint32_t,Simpsons::Graphics::DeclarationId> declarationTokens;
};
thread_local ScratchOwner* currentScratch{};
// Separate native engine identities below the first mapped guest kernel area.
// Never reused or truncated from 64-bit owner IDs; exhaustion is explicit.
std::atomic<uint32_t> nextIndex{0x00C00001},nextDeclaration{0x00B00001};
uint32_t token(std::atomic<uint32_t>& next,uint32_t limit) {
    uint32_t value=next.load();
    while(value<limit) if(next.compare_exchange_weak(value,value+1)) return value;
    throw Simpsons::Failure("Native engine resource identity space exhausted");
}
ScratchOwner& requireScope(uint8_t* base) {
    if(!currentScratch || !Simpsons::active || base!=Simpsons::active->base)
        throw Simpsons::Failure("Native scratch resource service reached outside its verified owner scope");
    return *currentScratch;
}
}
namespace Simpsons {
struct EngineScratchResources::Owner : ScratchOwner {};
EngineScratchResources::EngineScratchResources(std::shared_ptr<Graphics::Buffer> index):owner(std::make_unique<Owner>()) {
    if(currentScratch) throw Failure("Nested native scratch resource owner is unsupported");
    if(!index || index->type()!=Graphics::BufferKind::Index16 || index->byteSize()!=0x4E20)
        throw Failure("Native scratch owner lacks its original index resource");
    owner->backing=std::move(index);currentScratch=owner.get();
}
EngineScratchResources::~EngineScratchResources() {currentScratch=nullptr;}
void EngineScratchResources::validateCreated(uint32_t index,uint32_t declaration) const {
    if(!owner->index || index!=owner->indexToken || !owner->declarationTokens.contains(declaration))
        throw Failure("Original scratch fields do not identify their native resources");
    auto id=owner->declarationTokens.at(declaration);
    auto record=owner->declarations.record(id);
    if(record->elements().size()!=3 || record->minimumStreamBytes()!=28)
        throw Failure("Original scratch declaration differs from the verified layout");
}
void EngineScratchResources::requireReleased() const {
    if(owner->index || !owner->declarationTokens.empty() || owner->declarations.liveCount())
        throw Failure("Original scratch cleanup left native resource ownership live");
}
void EngineScratchResources::requireDeclaration(uint32_t id) const {
    if(currentScratch!=owner.get()) throw Failure("Native declaration query outside its owner thread");
    const auto found=owner->declarationTokens.find(id);
    if(found==owner->declarationTokens.end() || !owner->declarations.contains(found->second))
        throw Failure("Unknown or stale native declaration binding");
}
bool EngineScratchResources::ownsIndex(uint32_t id) const {
    if(currentScratch!=owner.get()) throw Failure("Native index query outside its owner thread");
    return owner->index && id==owner->indexToken;
}
std::shared_ptr<const Graphics::DeclarationRecord> EngineScratchResources::declaration(uint32_t id) const {
    requireDeclaration(id);
    return owner->declarations.record(owner->declarationTokens.at(id));
}
}

// These two callsite hooks replace native allocation/release, not the CPU owner
// function. Their original following stores, branches and declarations execute.
void SimpsonsNativeScratchIndexCreate(PPCContext& ctx,uint8_t* base) {
    auto& owner=requireScope(base);
    if(ctx.r3.u32!=0x4E20 || ctx.r4.u32!=0x28 || ctx.r5.u32!=1 || ctx.r6.u32 || owner.index)
        throw Simpsons::Failure("Unsupported scratch index allocation contract");
    owner.indexToken=token(nextIndex,0x00D00000);owner.index=owner.backing;
    ctx.r3.u32=owner.indexToken;ctx.lr=0x82409B10;
}
void SimpsonsNativeScratchIndexRelease(PPCContext& ctx,uint8_t* base) {
    auto& owner=requireScope(base);
    if(!owner.index || ctx.r3.u32!=owner.indexToken)
        throw Simpsons::Failure("Invalid native scratch index release");
    owner.index.reset();owner.indexToken=0;
    ctx.r3.u32=0;ctx.lr=0x82408E94;
}
void SimpsonsNativeDeclarationCreate(PPCContext& ctx,uint8_t* base) {
    auto& owner=requireScope(base);
    uint32_t source=ctx.r3.u32,output=ctx.r4.u32;
    auto* destination=Simpsons::active->pointer(output,4,true);
    std::vector<uint8_t> bytes;
    for(size_t count=0;count<=Simpsons::Graphics::DeclarationRegistry::MaxElements;++count) {
        uint64_t at=uint64_t(source)+bytes.size();
        if(at+12>0x100000000ull) throw Simpsons::Failure("Original declaration crosses guest address bounds");
        auto* row=Simpsons::active->pointer(uint32_t(at),12,false);
        bytes.insert(bytes.end(),row,row+12);
        if(PPC_LOAD_U32(uint32_t(at)+4)==0xFFFFFFFF) break;
    }
    auto id=owner.declarations.create(bytes);
    uint32_t result=0;
    for(const auto& [candidate,existing]:owner.declarationTokens) if(existing==id) {result=candidate;break;}
    if(!result) {
        try {
            result=token(nextDeclaration,0x00C00000);
            owner.declarationTokens.emplace(result,id);
        } catch(...) {owner.declarations.release(id);throw;}
    }
    // Output was checked before any allocation; no further throwing operation
    // follows publication. Use byte stores so an unaligned guest word is valid.
    for(unsigned i=0;i<4;++i) destination[i]=uint8_t(result>>(24-8*i));
    ctx.r3.u32=1;
}
void SimpsonsNativeDeclarationRelease(PPCContext& ctx,uint8_t* base) {
    auto& owner=requireScope(base);
    auto found=owner.declarationTokens.find(ctx.r3.u32);
    if(found==owner.declarationTokens.end()) throw Simpsons::Failure("Invalid or stale native engine declaration release");
    auto id=found->second;owner.declarations.release(id);
    if(!owner.declarations.contains(id)) owner.declarationTokens.erase(found);
}
