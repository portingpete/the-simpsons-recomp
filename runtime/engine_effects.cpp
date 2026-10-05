#include "engine_effects.h"
#include "guest_read_memo.h"
#include "callsite_counts.h"
#include "engine_driver.h"
#include "engine_recording.h"
#include "engine_cpu_calls.h"
#include "engine_audio.h"
#include "engine_shadow_textures.h"
#include "engine_reflection_textures.h"
#include "engine_builtin_textures.h"
#include "engine_scene_copies.h"
#include "engine_itxd_textures.h"
#include "engine_quad_declarations.h"
#include "character_mesh.h"
#include "static_mesh_source_cache.h"
#include "static_shadow_mesh.h"
#include "zprepass_vertices.h"
#include "rigid_vertices.h"
#include "rigid_material_constants.h"
#include "rigid_packet_owner.h"
#include "skin_vertices.h"
#include "sky_vertices.h"
#include "skin_profile.h"
#include "common/geometry_extent.h"
#include "skin_material_constants.h"
#include "renderer/effect_resources.h"
#include "renderer/effect_reflection.h"
#include "renderer/material_texture_usage.h"
#include "renderer/native_material_compiler.h"
#include "renderer/engine_state.h"
#include "renderer/zprepass_mesh.h"
#include "renderer/mono_mesh.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
// Keep this last: shared header bodies retain their normal definitions.
#include "aot_inline_memory.h"

namespace {
using namespace Simpsons;
using Graphics::EffectRecord;
constexpr uint32_t sharedSource=0x820D5730;
std::atomic<uint32_t> nextIdentity{0x00500001};
struct HostState {
    const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
    HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
};
void need(bool value,const char* message) {if(!value) throw Failure(message);}
uint32_t rigidSubmeshExtent(Runtime& runtime,uint32_t entries,uint32_t count) {
    // Original empty loops bind and close geometry without consuming rows.
    if(!count)return 0;
    // Original827400F8 transports the full DWORD count to82701220, which
    // iterates36-byte rows. Check its complete logical owner before reading
    // any row; mapped allocation padding does not extend that owner.
    const uint64_t bytes=36ull*count;
    need(bytes<=UINT32_MAX&&uint64_t(entries)+bytes<=UINT32_MAX,
         "Original rigid submesh byte span overflows");
    const auto owner=runtime.engineAudio?runtime.engineAudio->allocationSpan(entries):std::nullopt;
    if(owner)need(bytes<=uint64_t(owner->address)+owner->extent-entries,
                  "Original rigid submesh rows exceed their observed allocation owner");
    // Existing static/unobserved views retain mapped-range qualification.
    runtime.pointer(entries,uint32_t(bytes),false);return uint32_t(bytes);
}
// Raw rigid captures are diagnostics. This budget bounds their host copy and
// is not an original row-count limit: a larger valid table still renders.
constexpr uint32_t rigidCaptureBudgetBytes=64u*1024*1024;
// Exact stable-group fields of one rigid row receipt (entry kind, source, table shape and the
// consumed row scalars). Cached per audit context so repeated rows cost one hash lookup.
using RigidRowKey=std::array<uint32_t,14>;
struct RigidRowKeyHash {
    size_t operator()(const RigidRowKey& key) const noexcept {
        uint64_t hash=1469598103934665603ull;
        for(const auto word:key){hash^=word;hash*=1099511628211ull;}
        return size_t(hash);
    }
};
enum class RigidCaptureStatus {Complete,SkippedBudget,Failed};
struct RigidCaptureIdentity {
    const char* phase{};uint32_t source{},caller{},typed{},metadata{},object{},geometry{},payload{};
    uint32_t count{},entries{},bytes{},ownerBase{},ownerExtent{};uint64_t ownerGeneration{};uint64_t scene{};
};
struct RigidCaptureOutcome {
    RigidCaptureStatus status{RigidCaptureStatus::Failed};
    const char* step{"start"};const char* reasonCode{"none"};
    char reason[256]{};
};
// Runs capture work that must never decide a primary outcome. Every allocation,
// path, I/O and report failure is contained here (including inside a catch of
// the original upload error, which the caller then rethrows bare). The caller's
// host CSR/LastError are restored; no PPC/TLS state is swapped or mutated.
template<class Work,class Report>
RigidCaptureStatus containedRigidCapture(Work&& work,Report&& report) noexcept {
    const auto savedCSR=PPCFPSCRRegister::getcsr();const DWORD savedError=GetLastError();
    struct Restore {
        uint32_t csr;DWORD error;
        ~Restore() noexcept {PPCFPSCRRegister::restoreHostCSR(csr);SetLastError(error);}
    } restore{savedCSR,savedError};
    RigidCaptureOutcome outcome;
    try {
        outcome.status=work(outcome);
    }catch(const std::exception& error) {
        outcome.status=RigidCaptureStatus::Failed;outcome.reasonCode="exception";
        std::snprintf(outcome.reason,sizeof(outcome.reason),"%.255s",error.what()?error.what():"diagnostic exception");
    }catch(...) {
        outcome.status=RigidCaptureStatus::Failed;outcome.reasonCode="exception";
        std::snprintf(outcome.reason,sizeof(outcome.reason),"non-standard diagnostic exception");
    }
    try {
        report(outcome);
    }catch(...) {
        // The audit sink failed too; keep a bounded identity-free receipt.
        std::fprintf(stderr,"[RIGID CAPTURE DIAGNOSTIC] receipt unavailable step=%s\n",outcome.step);
    }
    return outcome.status;
}
void skinSubmeshExtent(Runtime& runtime,uint32_t entries,uint32_t count) {
    // Original82701638 consumes the full unsigned metadata word and36-byte
    // rows from the relocated8282F618 pool. Qualify the complete borrowed
    // span before multiplication/address narrowing or reading any row.
    const uint64_t bytes=36ull*count;
    need(bytes<=UINT32_MAX&&uint64_t(entries)+bytes<=UINT32_MAX,
         "Original skin submesh byte span overflows");
    const auto owner=runtime.engineAudio?runtime.engineAudio->allocationSpan(entries):std::nullopt;
    if(owner)need(bytes<=uint64_t(owner->address)+owner->extent-entries,
                  "Original skin submesh rows exceed their observed allocation owner");
    // Static/unobserved input retains the existing mapped-range qualification.
    // A mapped allocation's readable padding is not its observed logical end.
    runtime.pointer(entries,uint32_t(bytes),false);
}
// Original826FE710 selects a shader palette from byte(start,count) ranges.
// Its one-range result aliases82D64080; multiple ranges use64-matrix scratch.
std::optional<EngineAudioOwners::AllocationSpan> boneGroupOwner(Runtime& runtime,uint32_t table,uint32_t groups) {
    const auto owner=runtime.engineAudio?runtime.engineAudio->allocationSpan(table):std::nullopt;
    if(owner)need(2ull*groups<=uint64_t(owner->address)+owner->extent-table,
                 "Original matrix group table exceeds its observed allocation owner");
    return owner;
}
uint32_t boneGroupCount(Runtime& runtime,uint32_t bones,uint32_t groups,uint32_t table) {
    auto* base=runtime.base;
    // Original826FE710 iterates a signed-positive word count. Range rows do
    // not consume shader slots when their length is zero; only their selected
    // matrix total is bounded by the reflected64-matrix shader palette.
    need(bones>64&&bones<=255&&groups>=1&&groups<=0x7FFFFFFFu,
         "Original submesh matrix group extent is unqualified");
    (void)boneGroupOwner(runtime,table,groups);
    runtime.pointer(table,2*groups,false);uint32_t count=0;
    for(uint32_t i=0;i<groups;++i) {
        const uint32_t first=PPC_LOAD_U8(table+2*i),length=PPC_LOAD_U8(table+2*i+1);
        need(first<=bones&&length<=bones-first&&length<=64-count,
             "Original submesh matrix range exceeds its composed/shader palette");
        count+=length;
    }
    need(count>=1&&count<=64,"Original submesh matrix palette is empty or oversized");return count;
}
uint32_t boneGroupSource(Runtime& runtime,uint32_t bones,uint32_t groups,uint32_t table,uint32_t count) {
    auto* base=runtime.base;need(count==boneGroupCount(runtime,bones,groups,table),"Original selected matrix group count differs");
    const uint32_t address=groups==1?0x82D64080+64*PPC_LOAD_U8(table):0x82D63070;
    const auto* selected=runtime.pointer(address,64*count,false);uint32_t out=0;
    for(uint32_t i=0;i<groups;++i) {
        const uint32_t first=PPC_LOAD_U8(table+2*i),length=PPC_LOAD_U8(table+2*i+1);
        if(length)need(!std::memcmp(selected+64*out,runtime.pointer(0x82D64080+64*first,64*length,false),64*length),
                       "Original matrix group copy differs from authored ranges");
        out+=length;
    }
    return address;
}
struct BoneGroupSnapshot {
    uint32_t entry{};
    std::array<uint32_t,9> words{};
    std::vector<uint8_t> ranges;
    std::optional<EngineAudioOwners::AllocationSpan> owner;
};
void validateBoneGroupSnapshot(Runtime& runtime,uint32_t entry,const std::array<uint32_t,9>& words,
                               const std::vector<uint8_t>& ranges,
                               const std::optional<EngineAudioOwners::AllocationSpan>& owner) {
    // Revalidate an observed generation before touching any borrowed bytes.
    // Static/mapped input with no observed heap owner keeps its mapped-range
    // qualification; discovering an owner later is a lifetime change too.
    const auto currentOwner=runtime.engineAudio?runtime.engineAudio->allocationSpan(words[8]):std::nullopt;
    need(currentOwner==owner,"Original matrix group allocation owner/generation changed during traversal");
    auto* base=runtime.base;runtime.pointer(entry,36,false);
    for(uint32_t i=0;i<9;++i)need(PPC_LOAD_U32(entry+4*i)==words[i],"Original grouped matrix submesh changed during traversal");
    need(!ranges.empty()&&ranges.size()==2ull*words[7]&&
         !std::memcmp(runtime.pointer(words[8],uint32_t(ranges.size()),false),ranges.data(),ranges.size()),
         "Original grouped matrix ranges changed during traversal");
}
void validateBoneGroupSnapshot(Runtime& runtime,const std::vector<BoneGroupSnapshot>& rows,uint32_t entry) {
    const auto found=std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.entry==entry;});
    need(found!=rows.end(),"Original grouped matrix submesh has no captured owner");
    validateBoneGroupSnapshot(runtime,entry,found->words,found->ranges,found->owner);
}
std::vector<BoneGroupSnapshot> boneGroupRows(Runtime& runtime,uint32_t bones,uint32_t metadata,uint32_t object,bool materialSkip) {
    if(bones<=64)return {};
    auto* base=runtime.base;runtime.pointer(metadata,0x2C,false);
    const uint32_t count=PPC_LOAD_U32(metadata+0x10),rows=PPC_LOAD_U32(metadata+0x14);
    need(count<=65535,"Original grouped submesh count is unqualified");
    if(count)runtime.pointer(rows,36*count,false);
    uint32_t offsets=0,materials=0;
    if(materialSkip) {
        runtime.pointer(object,0x1C,false);const auto data=PPC_LOAD_U32(object+0x18);
        runtime.pointer(data,0x28,false);offsets=PPC_LOAD_U32(data+0x24);materials=PPC_LOAD_U32(0x82D6D814);
    }
    std::vector<BoneGroupSnapshot> selected;for(uint32_t i=0;i<count;++i) {
        const uint32_t row=rows+36*i;
        if(materialSkip) {
            const auto index=PPC_LOAD_U32(row);need(index<65536,"Original grouped material index is unqualified");
            runtime.pointer(offsets+4*index,4,false);const auto offset=PPC_LOAD_U32(offsets+4*index);
            need(uint64_t(materials)+offset+2<=UINT32_MAX,"Original grouped material address overflows");
            runtime.pointer(materials+offset,2,false);if(PPC_LOAD_U16(materials+offset)&0x20)continue;
        }
        BoneGroupSnapshot captured;captured.entry=row;
        for(uint32_t j=0;j<9;++j)captured.words[j]=PPC_LOAD_U32(row+4*j);
        captured.owner=boneGroupOwner(runtime,captured.words[8],captured.words[7]);
        boneGroupCount(runtime,bones,captured.words[7],captured.words[8]);
        const auto* ranges=runtime.pointer(captured.words[8],2*captured.words[7],false);
        captured.ranges.assign(ranges,ranges+2*captured.words[7]);
        validateBoneGroupSnapshot(runtime,captured.entry,captured.words,captured.ranges,captured.owner);
        selected.push_back(std::move(captured));
    }
    return selected;
}
// Routine successful draw/replay sampling: first few + periodic. Callers must
// keep all semantic work and checked guest reads outside the logging condition;
// only the fprintf itself is gated. Failure/rejection/first-capture evidence
// paths remain unconditional.
inline bool sampleHotLog(uint32_t& counter) noexcept {
    const uint32_t n=counter++;
    return n<4 || (n%512)==0;
}
struct PostProfile {
    uint32_t source,vtable,vertex,pixel,technique,scalars,samplers,words,beginCaller,endCaller,commitCaller,frame,typedBias;
    const char* name;
};
const PostProfile& postProfile(uint32_t source) {
    static constexpr PostProfile profiles[]={
        {0x8202DF98,0x820614E4,0x8202E6F0,0x8202E840,0x82D09928,5,6,164,0x823CA5B4,0x823CA5DC,0x823C9544,0xB0,0,"EDGE"},
        {0x8202FA78,0x82061430,0x820301A0,0x820302EC,0x82D098FC,4,6,152,0x823CA5B4,0x823CA5DC,0x823C85E0,0xA0,0,"AA"},
        {0x82034008,0x82061598,0x820347B0,0x82034900,0x82D09958,4,30,188,0x823CA1BC,0x823CA1E0,0x823CA180,0xC0,0x18,"EDGEAA"}};
    for(const auto& profile:profiles)if(profile.source==source)return profile;
    throw Failure("Unported native post-effect source");
}
uint32_t word(std::span<const uint8_t> b,size_t at) {
    need(at<=b.size() && b.size()-at>=4,"Truncated native FX metadata word");
    return uint32_t(b[at])<<24|uint32_t(b[at+1])<<16|uint32_t(b[at+2])<<8|b[at+3];
}
void store(uint8_t* p,uint32_t value) noexcept {
    p[0]=uint8_t(value>>24);p[1]=uint8_t(value>>16);p[2]=uint8_t(value>>8);p[3]=uint8_t(value);
}
uint64_t doubleWord(std::span<const uint8_t> b,size_t at) {return uint64_t(word(b,at))<<32|word(b,at+4);}
void storeDouble(uint8_t* p,uint64_t value) noexcept {store(p,uint32_t(value>>32));store(p+4,uint32_t(value));}
uint32_t typedVtable(uint32_t callback) {
    // Original constructors pinned by both completed catalog/finalizer proofs.
    switch(callback) {
    case 0x8273B040:return 0x8215034C;
    case 0x826B6B68:return 0x820B7170;
    case 0x82707058:return 0x8214E518;
    case 0x8273AE18:return 0x8215032C;
    case 0x8273A430:return 0x8215020C;
    case 0x8273ABF8:return 0x8215030C;
    case 0x8273AA18:return 0x821502EC;
    case 0x8273A4A8:return 0x8215022C;
    case 0x823C7FB8:return 0x82061430;
    case 0x823C8698:return 0x82061494;
    case 0x823C89A8:return 0x820614BC;
    case 0x823C8D90:return 0x820614E4;
    case 0x823C96A0:return 0x82061598;
    case 0x823CA6B0:return 0x820616C0;
    case 0x823CA960:return 0x82061714;
    case 0x823CABE8:return 0x82061758;
    default:throw Failure("Unqualified native typed FX callback");
    }
}
std::string guestName(Runtime& rt,uint32_t at) {
    need(at,"Null native FX query name");std::string result;
    for(uint32_t i=0;i<256;++i) {
        need(at<=UINT32_MAX-i,"Native FX query name overflow");
        const uint8_t c=*rt.pointer(at+i,1,false);
        if(!c) return result;
        result+=char(c);
    }
    throw Failure("Unterminated native FX query name");
}
const Graphics::EffectIdentity& profile(uint32_t source) {
    for(const auto& row:Graphics::originalEffectIdentities()) if(row.originalAddress==source) return row;
    char why[160];std::snprintf(why,sizeof(why),"Unqualified original native FX source identity=%08X; additional metadata profile required",source);
    throw Failure(why);
}
std::unique_ptr<EffectRecord> copyEffect(Runtime& rt,uint32_t source) {
    const auto& row=profile(source);
    return std::make_unique<EffectRecord>(source,std::span<const uint8_t>(rt.pointer(source,row.recordBytes,false),row.recordBytes));
}
void poolRoot(Runtime& rt,uint32_t pool) {
    auto* base=rt.base;
    need(pool && !(pool&127),"Native FX requires the original aligned CPU pool");
    rt.pointer(pool-4,0x204,false);
    const uint32_t raw=PPC_LOAD_U32(pool-4);
    need(raw && raw<=pool-4 && pool-raw<=128,"Original FX pool allocation backpointer differs");
    rt.pointer(raw,0x280,false);
    need(PPC_LOAD_U32(pool+0x188)>0,"Original FX pool has no live CPU root reference");
}
void emptyPool(Runtime& rt,uint32_t pool) {
    auto* base=rt.base;poolRoot(rt,pool);
    for(uint32_t at=0;at<0x100;at+=4)
        need(PPC_LOAD_U32(pool+at)==(at<0x80?0:0xFFFFFFFFu),"Original FX pool bookkeeping is not empty");
    for(uint32_t at=0x100;at<=0x124;at+=4)
        need(!PPC_LOAD_U32(pool+at),"Original FX pool has unqualified shared metadata");
    need(!PPC_LOAD_U32(pool+0x180) && !PPC_LOAD_U32(pool+0x184),"Original FX pool has an unqualified allocation owner");
}
// `tables` (optional) is the exact memo of the expensive part below: the whole-allocation
// permission checks and the three table comparisons. While it holds (no store to the compared
// tables' pages, no permission change anywhere, same backing), those are skipped; every scalar
// provenance word is still re-read and checked on every call.
void populatedPool(Runtime& rt,uint32_t pool,const EffectRecord& schema,GuestReadMemo* tables=nullptr) {
    auto* base=rt.base;poolRoot(rt,pool);
    const auto body=schema.body();
    const uint32_t q=rt.effectPoolBacking,descriptors=word(body,0x11C),defaultBytes=word(body,0x13C);
    const uint32_t nameBytes=word(body,0x294),defaultAt=(8*descriptors+15)&~15u;
    const uint32_t namesAt=defaultAt+defaultBytes,namePointersAt=(namesAt+nameBytes+3)&~3u;
    const uint32_t total=namePointersAt+4*descriptors;
    need(q && !(q&15) && PPC_LOAD_U32(pool+0x180)==q && PPC_LOAD_U32(pool+0x184)==total,
         "Original FX shared allocation provenance changed");
    const bool proven=tables && tables->valid();
    // A new proof takes the permission epoch BEFORE the permission checks it will stand for.
    if(tables && !proven){tables->begin();tables->trackPermissions();}
    if(!proven)rt.pointer(q-4,total+4,false);
    const uint32_t raw=PPC_LOAD_U32(q-4);
    need(raw && raw<=q-4 && q-raw<=16,"Original FX shared allocation backpointer differs");
    if(!proven)rt.pointer(raw,total+16,false);
    const std::array<uint32_t,10> expected={q,descriptors,q+defaultAt,word(body,0x134),defaultBytes,
        word(body,0x124),word(body,0x114),q+namesAt,nameBytes,q+namePointersAt};
    for(uint32_t i=0;i<expected.size();++i)
        need(PPC_LOAD_U32(pool+0x100+4*i)==expected[i],"Original FX shared metadata layout changed");
    if(proven)return;
    if(tables) {
        // Arm the compared tables before reading them; the defaults between them are not compared.
        tables->arm(rt.probe(q,8*descriptors,false),8*descriptors);
        tables->arm(rt.probe(q+namesAt,nameBytes,false),nameBytes);
        tables->arm(rt.probe(q+namePointersAt,4*descriptors,false),4*descriptors);
    }
    const uint32_t sourceDescriptors=word(body,word(body,0x10C)),sourceNames=word(body,word(body,0x290));
    const uint32_t sourceNamePointers=word(body,word(body,0x29C));
    need(sourceDescriptors<=body.size() && 8*descriptors<=body.size()-sourceDescriptors &&
         !std::memcmp(rt.pointer(q,8*descriptors,false),body.data()+sourceDescriptors,8*descriptors),
         "Original FX shared descriptors changed");
    need(sourceNames<=body.size() && nameBytes<=body.size()-sourceNames &&
         !std::memcmp(rt.pointer(q+namesAt,nameBytes,false),body.data()+sourceNames,nameBytes),
         "Original FX shared names changed");
    for(uint32_t i=0;i<descriptors;++i) {
        const uint32_t p=word(body,sourceNamePointers+4*i);
        // The real initializer rebases even sentinel0; it is not a null pointer.
        const uint32_t originalName=p==UINT32_MAX?0:sharedSource+12+p;
        const uint32_t rebased=originalName-(sharedSource+12+sourceNames)+q+namesAt;
        need(PPC_LOAD_U32(q+namePointersAt+4*i)==rebased,"Original FX shared descriptor name mapping changed");
    }
    if(tables)tables->commit();
    // The default words and dirty bookkeeping are mutable original CPU data.
    // Never hash them against startup defaults or reset them on later attaches.
}
EngineEffects& effects(uint8_t* base) {
    need(active && base==active->base && active->engineDriver,"Native FX has no active driver");
    return active->engineDriver->effects();
}
}
namespace Simpsons {
struct EngineEffects::State {
    struct Record {
        View view;
        std::unique_ptr<EffectRecord> metadata;
        Graphics::MaterialRegistry shaderRecords;
        std::vector<Graphics::MaterialId> shaders;
        std::array<uint8_t,128> privateMask;
        std::array<uint8_t,128> privateModified;
        uint32_t typedReflections=0;
        uint32_t contextIdentity{};
        std::shared_ptr<Graphics::NativeRecordingContext> recordingContext;
        Graphics::RigidVertexConstants rigidMaterialVS{};
        Graphics::RigidPixelConstants rigidMaterialPS{};
        Graphics::SkinVertexConstants skinMaterialVS{};
        std::shared_ptr<Graphics::Texture> skinBaseTexture,skinSecondTexture;
        uint32_t skinSecondHeader{};
        uint32_t skinBaseHeader{};
        Graphics::SkinPixelConstants skinMaterialPS{};
        bool skinMaterialInitialized{};
        std::shared_ptr<Graphics::Texture> rigidBaseTexture,rigidNoiseTexture;
        std::array<std::shared_ptr<Graphics::Texture>,2> rigidUvTexture{};
        std::array<uint32_t,2> rigidUvTextureHeader{};
        uint32_t rigidNoiseHeader{};
        uint32_t rigidBaseHeader{};
        std::shared_ptr<Graphics::Texture> chocolateTexture[3]{};
        uint32_t chocolateTextureHeader[3]{};
        // Sky stages 0-2 come from material texture rows. Stage 3 is normally
        // the live full-resolution scene-copy target retained by the original
        // texture bind; the palette row is only a fallback when no staged
        // scene-copy owner exists.
        std::shared_ptr<Graphics::Texture> skyTexture[4]{};
        uint32_t skyTextureHeader[4]{};
        std::shared_ptr<Graphics::RenderTarget> skyLineTarget;
        uint32_t skyLineTargetId{};
        Graphics::SkyVertexConstants skyMaterialVS{};
        Graphics::SkyPixelConstants skyMaterialPS{};
        bool skyMaterialInitialized{};
        bool rigidMaterialInitialized{},rigidAlphaEyeInitialized{};
        struct ParameterStorage {
            Runtime& runtime;uint32_t address{};uint32_t wordCount{};
            ParameterStorage(Runtime& rt,std::span<const uint32_t> words):runtime(rt),wordCount(uint32_t(words.size())) {
                address=rt.allocatePhysical(0,uint32_t(words.size()*4),PAGE_READWRITE,0,UINT32_MAX,4096);
                need(address,"Native FX private parameter allocation failed");
                try {auto* bytes=rt.pointer(address,uint32_t(words.size()*4),true);
                    for(size_t i=0;i<words.size();++i)store(bytes+4*i,words[i]);}
                catch(...){rt.freePhysical(address);address=0;throw;}
            }
            uint32_t words() const noexcept {return wordCount;}
            void fillPoison(uint32_t slot) {
                // Fresh poison contents on every TEMP lease, same pattern as the
                // original per-draw allocation. Caller guarantees wordCount
                // matches the slot's size bound; no truncation or growth here.
                auto* bytes=runtime.pointer(address,wordCount*4,true);
                for(uint32_t j=0;j<wordCount;++j)store(bytes+4*j,0xA5000000u|(slot<<16)|j);
            }
            ~ParameterStorage(){if(address)try{runtime.freePhysical(address);}catch(const std::exception& e){
                std::fprintf(stderr,"[NATIVE EFFECT] terminal private storage release failed: %s\n",e.what());}}
        };
        std::unique_ptr<ParameterStorage> parameters;
        // Bounded per-effect lazy caches derived only from immutable owned
        // metadata. Reflection has four variants keyed on notSkinned AND
        // whether the fixed shared schema names were supplied (never conflate
        // empty pool names with supplied names or skin mode). Pass bindings
        // key exact technique+handle, at most 64 successes; failures never
        // publish. Record lifetime owns both caches.
        mutable std::array<std::unique_ptr<Graphics::EffectReflection>,4> reflectionCache;
        mutable std::vector<std::pair<std::pair<uint32_t,uint32_t>,Graphics::EffectBinding>> passBindingCache;
        static constexpr size_t kPassBindingBound=64;
        Record(Runtime& rt,uint32_t wrapper,uint32_t manager,uint32_t source,uint32_t pool,
               Graphics::NativeMaterialCompiler& compiler):
            view{0,wrapper,manager,source,0,pool,0,Phase::Created,{},{},{}},metadata(copyEffect(rt,source)) {
            view.cacheBytes=metadata->cacheBytes();
            // Original creation copies the source prefix, including its private
            // mutable mask. Keep the live mask separate from immutable metadata.
            std::copy_n(metadata->body().data()+0x80,privateMask.size(),privateMask.begin());
            std::copy_n(metadata->body().data(),privateModified.size(),privateModified.begin());
            for(const auto& technique:metadata->techniques()) {
                for(const auto& row:technique.scalars) view.scalars.push_back({row.sdkId,row.value});
                for(const auto& row:technique.samplers) view.samplers.push_back({row.stage,row.sdkId,row.value});
            }
            const auto defaults=metadata->privateDefaults();
            for(size_t i=0;i<defaults.size();i+=4) view.defaultVectorWords.push_back(word(defaults,i));
            for(const auto& shader:metadata->shaders()) {
                const auto id=shaderRecords.create(shader.originalAddress,metadata->body().subspan(shader.bodyOffset,shader.recordBytes));
                shaders.push_back(id);
                // Only these two FX shaders have qualified offline native code.
                // All others own immutable bytes, with explicit Uncompiled capability.
                if(shader.originalAddress==0x820B8F08 || shader.originalAddress==0x820B90D4)
                    shaderRecords.prepareForBind(id,compiler);
            }
        }
    };
    Runtime& runtime;
    Graphics::NativeBackend& backend;
    Graphics::NativeMaterialCompiler compiler;
    const DWORD thread=GetCurrentThreadId();
    std::unordered_map<uint32_t,std::unique_ptr<Record>> records;
    std::unique_ptr<EffectRecord> poolSchema;
    // Bounded owner-scoped reuse for TEMPORARY upload buffers only (mono/rigid/
    // skin replay staging). Persistent per-effect Record::parameters remain
    // independent and are never pooled. Idle buffers are owned here and never
    // referenced by any active run (exclusive lease: acquire removes from idle,
    // release returns only after the run's original closure). Fixed mono/rigid
    // slots use 224 words; skin slots use vs*4/ps*4 words and grow safely via
    // fresh allocation when no exact-size idle buffer exists. No GPU retains
    // guest buffer addresses; run.copies snapshots remain the source of truth.
    static constexpr size_t kTempUploadIdleBound=12;
    std::vector<std::unique_ptr<Record::ParameterStorage>> tempUploadIdle;
    void recycleTempUpload(std::array<std::unique_ptr<Record::ParameterStorage>,4>& buffers) {
        // Idle capacity is reserved at owner construction; push_back below must
        // not allocate/throw halfway through closure. If reserve somehow failed
        // at construction, this still bounds but may throw on reallocation.
        for(auto& b:buffers) {
            if(!b) continue;
            if(tempUploadIdle.size()<kTempUploadIdleBound) tempUploadIdle.push_back(std::move(b));
            // Else b destroys here, freeing its physical commit (bounded).
        }
    }
    std::array<std::unique_ptr<Record::ParameterStorage>,4> acquireTempUpload(const std::array<uint32_t,4>& wordCounts) {
        std::array<std::unique_ptr<Record::ParameterStorage>,4> out{};
        try {
            for(uint32_t i=0;i<4;++i) {
                auto it=tempUploadIdle.end();
                for(auto cur=tempUploadIdle.begin();cur!=tempUploadIdle.end();++cur) {
                    if((*cur)->words()==wordCounts[i]) {it=cur;break;}
                }
                if(it!=tempUploadIdle.end()) {
                    auto buf=std::move(*it);tempUploadIdle.erase(it);
                    buf->fillPoison(i);
                    out[i]=std::move(buf);
                } else {
                    std::vector<uint32_t> poison(wordCounts[i]);
                    for(uint32_t j=0;j<wordCounts[i];++j)poison[j]=0xA5000000u|(i<<16)|j;
                    out[i]=std::make_unique<Record::ParameterStorage>(runtime,poison);
                }
            }
        } catch(...) {
            for(auto& b:out) if(b) {
                if(tempUploadIdle.size()<kTempUploadIdleBound) tempUploadIdle.push_back(std::move(b));
            }
            throw;
        }
        return out;
    }
    uint32_t activeEdgeId{},edgeTyped{},edgeCamera{};
    struct ScreenReplacement {
        uint32_t effect{},typed{},manager{},wrapper{},technique{},cache{};
        uint32_t declaration{},vertexCache{},pixelCache{};
        Graphics::NativeScreenReplacementReceipt receipt;
        Graphics::NativeScreenInputReceipt inputs;
        Graphics::NativeScreenBatchReceipt batch;
        Graphics::NativeBindingResetReceipt reset;
        bool inputOnly{},pendingDraw{},inputsRetired{},bindingReset{};
    } screenReplacement,resetPreflight;
    void requireRigidPhysical(const char* boundary) {
        try {backend.requireRigidShaders(*rigidVertex,*rigidPixel);}
        catch(const Graphics::Error&) {
            auto* base=runtime.base;const auto* c=currentContext;
            std::fprintf(stderr,"[NATIVE RIGID SHADER BOUNDARY] boundary=%s effect=%08X typed=%08X original=%08X/%08X guest=%08X/%08X/%08X receipt=%08X reset=%u input_only=%u pending=%u batch_retired=%u fn=%08X lr=%08X sp=%08X\n",
                boundary,rigidId,rigidTyped,rigidVertex->originalAddress(),rigidPixel->originalAddress(),
                PPC_LOAD_U32(0x82CD1A68),PPC_LOAD_U32(0x82CD1A6C),PPC_LOAD_U32(0x82CD1A70),
                screenReplacement.effect,unsigned(screenReplacement.bindingReset),unsigned(screenReplacement.inputOnly),
                unsigned(screenReplacement.pendingDraw),unsigned(screenReplacement.inputsRetired),
                c?c->lastFunction:0,c?uint32_t(c->lr):0,c?c->r1.u32:0);
            std::fflush(stderr);throw;
        }
    }
    const PPCContext* resetPreflightCpu{};
    uint32_t resetPreflightFrame{};
    struct RetainedSelection {uint32_t id{},typed{},technique{},cache{};};
    RetainedSelection retainedSelection() {
        const auto count=unsigned(bool(rigidId))+unsigned(bool(skinId))+unsigned(bool(monoId))+
            unsigned(bool(zprepassId))+unsigned(bool(activeShadowId))+unsigned(bool(activeEdgeId));
        need(count<=1,"Original binding reset has overlapping logical effect owners");
        uint32_t id{},typed{},technique{};
        if(rigidId){id=rigidId;typed=rigidTyped;technique=alphaRigid()?0x0007FFFCu:0x0003FFFCu;}
        else if(skinId){id=skinId;typed=skinTyped;technique=alphaSkin()?0x0007FFFCu:0x0003FFFCu;}
        else if(monoId){id=monoId;typed=monoTyped;technique=monoTechnique;}
        else if(zprepassId){id=zprepassId;typed=zprepassTyped;technique=0x0003FFFC;}
        else if(activeShadowId){id=activeShadowId;typed=shadowTyped;technique=shadowTechnique;}
        else if(activeEdgeId){id=activeEdgeId;typed=edgeTyped;technique=0x0003FFFC;}
        if(!id)return {};
        const auto cache=find(id).view.cache+((activeShadowId||monoId)&&technique==0x0007FFFC?24u:0u);
        return {id,typed,technique,cache};
    }
    void requireResetLogicalOwner(const RetainedSelection& selection) {
        const auto& record=find(selection.id);const auto& v=record.view;auto* base=runtime.base;
        need(v.phase==Phase::Reflected&&record.typedReflections==1&&
             v.manager==PPC_LOAD_U32(0x82D08BFC)&&PPC_LOAD_U32(v.wrapper)==0x820B7140&&
             PPC_LOAD_U32(v.wrapper+0xC)==v.manager&&PPC_LOAD_U32(v.wrapper+0x10)==v.identity&&
             PPC_LOAD_U32(v.wrapper+0x14)==v.wrapper&&PPC_LOAD_U32(v.wrapper+0x18)==v.identity,
             "Original binding reset lost its reflected wrapper owner");
        if(monoId) {
            need(v.source==0x8211F480&&monoVertex&&monoPixel&&monoPacket&&
                 monoTyped==PPC_LOAD_U32(monoPacket+((monoImmediatePath||monoRecordingPath)?0x18u:0x1Cu))&&PPC_LOAD_U32(monoTyped)==0x8215020C&&
                 (!monoMesh.native||monoMesh.phase==4),"Original reset mono association or completed mesh changed");
        } else if(zprepassId) {
            need(v.source==0x821490E0&&zprepassVertex&&zprepassTyped==PPC_LOAD_U32(0x82D6D8A0)&&
                 PPC_LOAD_U32(zprepassTyped)==0x8215022C&&(!zprepassMesh.native||zprepassMesh.phase==4),
                 "Original reset zprepass association or completed mesh changed");
        } else if(activeShadowId) {
            const bool alpha=shadowTechnique==0x0003FFFC;
            need(v.source==0x820C0550&&shadowVertex&&bool(shadowPixel)==alpha&&
                 (shadowTechnique==0x0003FFFC||shadowTechnique==0x0007FFFC)&&
                 PPC_LOAD_U32(shadowTyped)==0x8214E518&&PPC_LOAD_U32(shadowTyped+0x10)==v.manager&&
                 PPC_LOAD_U32(shadowTyped+(alpha?0x5E0:0x5DC))==shadowTechnique&&
                 (!characterMesh.geometry||characterMesh.complete)&&!shadowPalettePrelude.cpu,
                 "Original reset character-shadow association changed");
        } else if(activeEdgeId) {
            need(edgeVertex&&edgePixel&&!edgeVertices&&!edgeDeclaration&&
                 PPC_LOAD_U32(edgeTyped)==postProfile(v.source).vtable&&PPC_LOAD_U32(edgeTyped+0x10)==v.manager,
                 "Original reset post-effect association or completed rectangle changed");
            edgeCache(record);
        } else if(rigidId) {
            need(rigidVertex&&rigidPixel&&(!rigidImmediate.active||!rigidImmediate.activated),
                 "Original reset rigid owner or immediate lifetime changed");
        } else {
            need(skinId&&skinVertex&&skinPixel&&(!skinImmediate.active||!skinImmediate.activated),
                 "Original reset skin owner or immediate lifetime changed");
        }
        validatePool(v.pool);
    }
    void requireScreenReplacement(uint8_t* base,bool allowPending=false) {
        const auto& replacement=screenReplacement;
        if(runtime.resourceAudit.active())try {
            const auto declaration=PPC_LOAD_U32(0x82CD1A68),vertex=PPC_LOAD_U32(0x82CD1A6C),pixel=PPC_LOAD_U32(0x82CD1A70);
            const auto known=records.find(replacement.effect);
            char asset[40],parameters[224],instance[240];
            std::snprintf(asset,sizeof(asset),"source:%08X",known==records.end()?0:known->second->view.source);
            std::snprintf(parameters,sizeof(parameters),"input_only=%u pending=%u batch_retired=%u binding_reset=%u technique=%08X declaration_match=%u vertex_match=%u pixel_match=%u",
                unsigned(replacement.inputOnly),unsigned(replacement.pendingDraw),unsigned(replacement.inputsRetired),unsigned(replacement.bindingReset),replacement.technique,
                unsigned(declaration==replacement.declaration),unsigned(vertex==replacement.vertexCache),unsigned(pixel==replacement.pixelCache));
            std::snprintf(instance,sizeof(instance),"effect=%08X typed=%08X manager=%08X wrapper=%08X cache=%08X expected=%08X/%08X/%08X actual=%08X/%08X/%08X function=%08X",
                replacement.effect,replacement.typed,replacement.manager,replacement.wrapper,replacement.cache,replacement.declaration,replacement.vertexCache,replacement.pixelCache,
                declaration,vertex,pixel,currentContext?currentContext->lastFunction:0);
            runtime.resourceAudit.observe("screen_replacement",asset,currentContext?uint32_t(currentContext->lr):0,parameters,
                known==records.end()?"unknown-owner":"registered-owner",runtime.nativeDepthCopyCount.load(),instance);
        }catch(...){std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] screen replacement capture failed\n");}
        need(allowPending||!replacement.pendingDraw,"Original sprite input transaction has unfinished geometry");
        const auto selection=retainedSelection();const auto id=selection.id,typed=selection.typed;
        need(id&&replacement.effect==id&&replacement.typed==typed&&
             (replacement.bindingReset||(!activeEdgeId&&!activeShadowId&&!monoId&&!zprepassId)),
             "Original screen replacement lost its logical effect owner");
        const auto& record=find(id);const auto& v=record.view;
        const auto selected=selection.technique;
        need(!record.recordingContext&&replacement.manager==v.manager&&replacement.wrapper==v.wrapper&&
             replacement.technique==selected&&replacement.cache==selection.cache&&
             PPC_LOAD_U32(typed+0x18)==v.wrapper&&PPC_LOAD_U32(typed+0x1C)==id&&
             PPC_LOAD_U32(v.manager+4)==v.wrapper&&PPC_LOAD_U32(v.manager+8)==id&&
             PPC_LOAD_U32(v.manager+0xC)==selected&&PPC_LOAD_U32(v.wrapper+0x2C)==selection.cache,
             "Original screen replacement effect/cache association changed");
        if(replacement.bindingReset)requireResetLogicalOwner(selection);
        const auto declaration=PPC_LOAD_U32(0x82CD1A68),vertex=PPC_LOAD_U32(0x82CD1A6C),pixel=PPC_LOAD_U32(0x82CD1A70);
        if(declaration!=replacement.declaration||vertex!=replacement.vertexCache||pixel!=replacement.pixelCache) {
            std::fprintf(stderr,"[NATIVE SCREEN REPLACEMENT CACHE] effect=%08X typed=%08X input_only=%u pending=%u batch_retired=%u binding_reset=%u expected=%08X/%08X/%08X actual=%08X/%08X/%08X fn=%08X lr=%08X\n",
                id,typed,unsigned(replacement.inputOnly),unsigned(replacement.pendingDraw),unsigned(replacement.inputsRetired),unsigned(replacement.bindingReset),
                replacement.declaration,replacement.vertexCache,replacement.pixelCache,declaration,vertex,pixel,
                currentContext?currentContext->lastFunction:0,currentContext?uint32_t(currentContext->lr):0);
            std::fflush(stderr);
        }
        need(declaration==replacement.declaration&&vertex==replacement.vertexCache&&pixel==replacement.pixelCache,
             "Original screen replacement shader/declaration cache changed");
        if(replacement.bindingReset) {backend.flushIm2D();backend.requireBindingReset(replacement.reset);}
        else if(replacement.inputOnly)backend.requireScreenInputReplacement(replacement.inputs);
        else backend.requireScreenReplacement(replacement.receipt);
        if(replacement.inputsRetired)backend.requireScreenBatchRetirement(replacement.batch);
    }
    uint32_t activeShadowId{},shadowTyped{},shadowCamera{},shadowTechnique{};
    const Graphics::CompiledMaterial* shadowVertex{};
    const Graphics::CompiledMaterial* shadowPixel{};
    std::shared_ptr<Graphics::NativeShadowDepthCommit> shadowCommit;
    uint32_t monoId{},monoTyped{},monoCamera{},monoPacket{},monoFrame{},monoTechnique{};
    bool monoWorld{},monoBoolean{},monoMaterial{},monoImmediatePath{},monoRecordingPath{};
    const Graphics::CompiledMaterial* monoVertex{};
    const Graphics::CompiledMaterial* monoPixel{};
    std::shared_ptr<Graphics::NativeMonoCommit> monoCommit;
    uint32_t zprepassId{},zprepassTyped{},zprepassCamera{};
    const Graphics::CompiledMaterial* zprepassVertex{};
    std::shared_ptr<Graphics::NativeZPrepassCommit> zprepassCommit;
    uint32_t rigidId{},rigidTyped{},rigidCamera{};
    bool alphaRigid() const {
        if(!rigidPixel)return false;
        const auto p=rigidPixel->originalAddress();
        return p==0x8200E1BC||p==0x82017E4C||p==0x8202C3BC||p==0x8203A644||p==0x82052DF4||p==0x82044850||p==0x82047F44||p==0x820374E8||p==0x8205EED4||isRigidFamilyAlphaPixel(p);
    }
    const Graphics::CompiledMaterial* rigidVertex{};
    const Graphics::CompiledMaterial* rigidPixel{};
    struct RigidTextureTransfer {
        PPCContext* cpu{};
        uint32_t sp{},manager{},context{},shadowOwner{},cursor{},phase{};
        bool active{},complete{},immediate{};
        struct Row {uint32_t handle{},usage{},stage{},texture{};};
        std::array<Row,3> rows{};
        std::array<std::shared_ptr<Graphics::DepthTarget>,2> depths{};
    } rigidTextures;
    struct RigidMeshRun {
        PPCContext* cpu{};
        uint32_t entrySP{},frame{},packet{},metadata{},object{},geometry{},context{},phase{};
        uint32_t cache{},declaration{},cursor{},payloadId{};
        bool active{},complete{},immediate{},auxiliaryCleared{},submeshUpdated{};
        struct Submesh {uint32_t entry{},material{},materialHeader{},materialOffset{};std::array<uint32_t,9> words{};uint16_t flags{};};
        std::vector<Submesh> draws;
        std::vector<uint8_t> header;
        std::shared_ptr<Graphics::NativeRigidMesh> native;
        std::shared_ptr<Graphics::NativeRecordingPayload> payload;
        std::shared_ptr<Graphics::NativeRigidReplayConstants> replayValues;
    } rigidMesh;
    // The original cache can replay an older node after another build reuses
    // the same packet/stack storage. Retain qualification by payload identity,
    // never by the address of that transient packet or the latest mesh session.
    struct RigidPayload {
        uint32_t id{},effect{},typed{},context{},metadata{},object{},camera{},shadowOwner{},draws{};
        uint64_t bytes{};
        const Graphics::CompiledMaterial* vertex{};
        const Graphics::CompiledMaterial* pixel{};
        std::shared_ptr<Graphics::NativeRecordingPayload> native;
        std::shared_ptr<Graphics::NativeRigidReplayConstants> replayValues;
        std::shared_ptr<Graphics::NativeSkyReplayConstants> skyReplayValues;
        std::shared_ptr<Graphics::NativeMonoReplayConstants> monoReplayValues;
    };
    std::unordered_map<uint32_t,RigidPayload> rigidPayloads;
    uint32_t rigidCapturePayload{};
    // Attempted latches consume one diagnostic attempt (even when skipped or
    // failed); the completed latch is set only after every artifact closed.
    bool rigidCaptureDraw{},rigidCaptureDrawAttempted{};
    struct RigidMaterialRun {
        uint32_t material{},frame{},cursor{},pendingSlot{},pendingSite{};
        bool active{},filtered{},committed{};
        struct Row {uint32_t offset{},type{},handle{},value{};std::array<uint32_t,6> binding{};};
        std::vector<Row> rows;
    } rigidMaterial;
    struct RigidReplayRun {
        PPCContext* cpu{};
        uint32_t packet{},payload{},typed{},context{},entrySP{},uploadSP{},phase{};
        bool shared{},callbacks{},upload{},complete{},prepared{};
        uint64_t executions{},executedDraws{};
        std::array<std::unique_ptr<Record::ParameterStorage>,4> buffers;
        std::array<std::array<uint8_t,896>,2> copies{};
    } rigidReplay,monoReplay;
    // The original quota fallback has no recording payload. Its staging and
    // draw owners cannot be mistaken for a cached replay or retained by it.
    struct RigidImmediateRun {
        PPCContext* cpu{};
        uint32_t entrySP{},frame{},packet{},typed{},context{};
        bool active{},activated{},submeshActive{},alpha{};
        uint64_t drawsBefore{};
        RigidReplayRun staging;
        std::shared_ptr<Graphics::NativeRigidCommit> commit;
    } rigidImmediate;
    struct MonoImmediateRun {
        PPCContext* cpu{};
        uint32_t entrySP{},frame{},packet{},typed{},context{},material{};
        bool active{},submeshActive{},submeshUpdated{},materialActive{},filtered{},materialCommitted{},alpha{};
        uint64_t drawsBefore{};
    } monoImmediate;
    RigidTextureTransfer monoTextures;
    struct MonoRecordingRun {
        PPCContext* cpu{};
        uint32_t packet{},typed{},sp{},context{},previous{},manager{},payloadId{},material{};
        bool active{},complete{},materialActive{},filtered{},materialCommitted{};
        Graphics::MonoConstants constants{};
        Graphics::MonoBooleans booleans{};
        std::shared_ptr<Graphics::NativeRecordingPayload> payload;
        std::shared_ptr<Graphics::NativeMonoReplayConstants> live;
    } monoRecording;
    uint32_t skinId{},skinTyped{},skinCamera{};
    const Graphics::CompiledMaterial* skinVertex{};
    const Graphics::CompiledMaterial* skinPixel{};
    bool alphaSkin() const {
        if(!skinPixel)return false;
        const auto p=skinPixel->originalAddress();
        return p==0x8200A4A4||p==0x82021344||p==0x82013F8C||p==0x82028258||p==0x82040510||p==0x8204E75C;
    }
    std::shared_ptr<Graphics::NativeSkinCommit> skinCommit;
    struct SkinTextureTransfer {
        PPCContext* cpu{};
        uint32_t sp{},context{},shadowOwner{};
        bool active{},complete{};
        uint32_t cursor{},phase{};
        struct Row {uint32_t handle{},usage{},stage{},texture{};};
        std::array<Row,3> rows{};
        std::shared_ptr<Graphics::DepthTarget> characterShadow;
    } skinTextures;
    struct SkinMeshRun {
        PPCContext* cpu{};
        uint32_t entrySP{},frame{},packet{},metadata{},object{},geometry{},context{},phase{};
        uint32_t cache{},declaration{},cursor{},payloadId{};
        bool active{},complete{},immediate{},auxiliaryCleared{},submeshUpdated{};
        struct Submesh {uint32_t entry{},material{},materialHeader{},materialOffset{};std::array<uint32_t,9> words{};uint16_t flags{};std::vector<uint8_t> boneRanges;std::optional<EngineAudioOwners::AllocationSpan> boneOwner;};
        std::vector<Submesh> draws;
        std::vector<uint8_t> header;
        std::array<std::vector<uint8_t>,6> morphStreams;
        uint32_t morphBoundMask{},morphClearMask{};
        std::shared_ptr<Graphics::NativeSkinMesh> native;
        std::shared_ptr<Graphics::NativeRecordingPayload> payload;
        std::shared_ptr<Graphics::NativeSkinReplayConstants> replayValues;
    } skinMesh;
    struct SkyMeshRun {
        PPCContext* cpu{};
        uint32_t entrySP{},frame{},packet{},metadata{},object{},geometry{},context{},phase{};
        uint32_t cache{},declaration{},cursor{},payloadId{};
        uint64_t drawsBefore{};
        bool active{},complete{},immediate{},auxiliaryCleared{},submeshUpdated{};
        struct Submesh {uint32_t entry{},material{},materialHeader{},materialOffset{};std::array<uint32_t,9> words{};uint16_t flags{};};
        std::vector<Submesh> draws;
        std::vector<uint8_t> header;
        std::shared_ptr<Graphics::NativeSkyMesh> native;
        std::shared_ptr<Graphics::NativeRecordingPayload> payload;
        std::shared_ptr<Graphics::NativeSkyReplayConstants> replayValues;
    } skyMesh;
    // Sky staging binds textures by small id during replay uploads; the
    // material commit resolves what it can from material rows and falls back
    // to these per-stage ids at draw. Reset per packet at sky begin.
    std::array<uint32_t,4> skyStagedTextures{};
    struct SkyMaterialRun {
        uint32_t material{},frame{},cursor{},pendingSlot{},pendingSite{};
        bool active{},filtered{},committed{};
        struct Row {uint32_t offset{},type{},handle{},value{};std::array<uint32_t,6> binding{};};
        std::vector<Row> rows;
        std::shared_ptr<Graphics::NativeSkyCommit> commit;
    } skyMaterial;
    struct SkinPayload {
        uint32_t id{},effect{},typed{},context{},metadata{},object{},camera{},draws{};
        uint64_t bytes{};
        const Graphics::CompiledMaterial* vertex{};
        const Graphics::CompiledMaterial* pixel{};
        std::shared_ptr<Graphics::NativeRecordingPayload> native;
        std::shared_ptr<Graphics::NativeSkinReplayConstants> replayValues;
    };
    std::unordered_map<uint32_t,SkinPayload> skinPayloads;
    struct SkinMaterialRun {
        uint32_t material{},frame{},cursor{},pendingSlot{},pendingSite{};
        bool active{},filtered{},committed{};
        struct Row {uint32_t offset{},type{},handle{},value{};std::array<uint32_t,6> binding{};};
        std::vector<Row> rows;
    } skinMaterial;
    struct SkinReplayRun {
        PPCContext* cpu{};
        uint32_t packet{},payload{},typed{},context{},entrySP{},uploadSP{},phase{};
        uint32_t vsRegisters{},psRegisters{};
        bool shared{},callbacks{},upload{},complete{},prepared{};
        uint64_t executions{},executedDraws{};
        std::array<std::unique_ptr<Record::ParameterStorage>,4> buffers;
        std::array<std::vector<uint8_t>,2> copies{};
    } skinReplay;
    struct SkinImmediateRun {
        PPCContext* cpu{};
        uint32_t entrySP{},frame{},packet{},typed{},context{};
        bool active{},activated{},submeshActive{},alpha{};
        uint64_t drawsBefore{};
        SkinReplayRun staging;
        std::shared_ptr<Graphics::NativeSkinCommit> commit;
    } skinImmediate;
    struct ZPrepassMesh {
        uint32_t metadata{},object{},geometry{},frame{},phase{},cache{},declaration{};
        std::vector<uint8_t> header;
        std::shared_ptr<Graphics::NativeZPrepassMesh> native;
    } zprepassMesh;
    // Exact raw-source cache: identical current raw vertex/index/declaration
    // bytes plus stride reuse the native mesh, skipping the snapshot, decoder
    // and upload pure work. State-owned (never inside the resettable
    // ZPrepassMesh above) so inspectZPrepassMesh entry resets cannot drop it.
    // Keyed by content only, never by guest address; snapshots own their
    // bytes and retain no guest pointer.
    StaticMeshSourceCache<Graphics::NativeZPrepassMesh> zprepassSourceCache;
    // Guest-write-watched exact source caches for the rigid and skin mesh streams (same contract as zprepassSourceCache).
    StaticMeshSourceCache<Graphics::NativeRigidMesh> rigidSourceCache;
    // One per original skin vertex shader: the shader address is an upload input the exact key must not conflate.
    std::unordered_map<uint32_t,std::unique_ptr<StaticMeshSourceCache<Graphics::NativeSkinMesh>>> skinSourceCaches;
    StaticMeshSourceCache<Graphics::NativeSkinMesh>& skinSourceCache(uint32_t vertexAddress) {
        auto& slot=skinSourceCaches[vertexAddress];
        if(!slot) {
            const char* verify=std::getenv("SIMPSONS_WATCH_VERIFY");
            slot=std::make_unique<StaticMeshSourceCache<Graphics::NativeSkinMesh>>(128,16u*1024u*1024u);
            slot->setWriteWatch(runtime.writeWatch(),verify&&*verify&&*verify!='0');
        }
        return *slot;
    }
    struct MonoMesh {
        uint32_t metadata{},object{},geometry{},frame{},phase{},cache{},declaration{};
        uint32_t bones{},auxiliaryMask{},cleanupMask{},paletteEntry{},paletteCount{},expectedGroups{};
        bool boneUpload{},skinBoolean{},skinCommit{};
        uint32_t cursor{};
        std::vector<RigidMeshRun::Submesh> draws;
        std::vector<uint8_t> header;
        std::vector<BoneGroupSnapshot> boneGroups;
        std::shared_ptr<Graphics::NativeMonoMesh> native;
    } monoMesh;
    struct CharacterMesh {
        uint32_t geometry{},metadata{},object{},frame{},declaration{},cache{},phase{},paletteEntry{},paletteCount{};
        uint32_t submeshCount{},submeshes{};
        bool staticGeometry{},paletteCommitted{},complete{};
        std::vector<uint8_t> header,vertices,indices,elements;
        std::vector<BoneGroupSnapshot> boneGroups;
        std::shared_ptr<Graphics::NativeShadowMesh> native;
    } characterMesh;
    struct ShadowPalettePrelude {
        PPCContext* cpu{};
        uint32_t metadata{},object{},frame{};
        std::vector<BoneGroupSnapshot> boneGroups;
    } shadowPalettePrelude;
    const Graphics::CompiledMaterial* edgeVertex{};
    const Graphics::CompiledMaterial* edgePixel{};
    std::shared_ptr<Graphics::NativeEdgeCommit> edgeCommit;
    std::shared_ptr<Graphics::RenderTarget> edgeSource;
    Graphics::EdgeAAInputs edgeAAInputs;
    std::array<uint32_t,5> edgeAAIds{};
    uint32_t edgeSourceId{},edgeDeclaration{},edgeDrawSp{};
    std::unique_ptr<Record::ParameterStorage> edgeVertices;
    // Owner-scoped idle reuse for the transient 12-word edge rectangle only.
    // At most one idle physical page; never pools persistent Record::parameters.
    // Exclusive lease: idle->active on allocation, active->idle only after a
    // successful backend.drawEdge; failed draws keep active for terminal RAII.
    std::unique_ptr<Record::ParameterStorage> idleEdgeVertices;
    // One-shot initial shadow-depth diagnostic per owner; latched only after
    // both raw/meta outputs close successfully. Front capture.request protocol
    // is owned by the presentation path and left unchanged here.
    bool shadowDepthCaptured{};
    // Explicit first-rigid-mesh capture: payloadId==0 (immediate) must not
    // re-trigger. Latch mesh once with geometry/payload correlation so the
    // first draw metadata corresponds to the first mesh.
    bool rigidFirstMeshDone{},rigidFirstMeshAttempted{};
    // Row receipts already emitted in the current audit mission/action context.
    std::unordered_set<RigidRowKey,RigidRowKeyHash> rigidRowKeys;
    uint64_t rigidRowEpoch{};
    uint32_t rigidFirstMeshGeometry{},rigidFirstMeshPayload{};
    State(Runtime& rt,Graphics::NativeBackend& device):runtime(rt),backend(device),compiler(device) {
        tempUploadIdle.reserve(kTempUploadIdleBound);
        const char* verify=std::getenv("SIMPSONS_WATCH_VERIFY");
        zprepassSourceCache.setWriteWatch(rt.writeWatch(),verify&&*verify&&*verify!='0');
        rigidSourceCache.setWriteWatch(rt.writeWatch(),verify&&*verify&&*verify!='0');
    }
    void require(uint8_t* base) const {
        need(active==&runtime && base==runtime.base && GetCurrentThreadId()==thread,"Native FX accessed outside its runtime/thread");
        runtime.checkRunning();
        need(runtime.engineDriver!=nullptr,"Native FX driver no longer exists");
        runtime.engineDriver->requireContext(PPCLoadU32(base,0x82D5DA74));
    }
    Record& find(uint32_t token) const {
        auto at=records.find(token);need(at!=records.end(),"Unknown or stale native FX identity");return *at->second;
    }
    uint32_t privateWord(uint32_t token,uint32_t index) const {
        require(runtime.base);const auto& record=find(token);
        need(index<record.view.defaultVectorWords.size(),"Native FX private parameter word exceeds owned storage");
        if(!record.parameters)return record.view.defaultVectorWords[index];
        const uint32_t owned=record.parameters->words();
        need(record.view.defaultVectorWords.size()==owned,"Native FX private parameter extent differs");
        need(uint64_t(owned)*4<=UINT32_MAX,"Native FX private parameter extent overflow");
        // Match view()'s validation of the complete live allocation. Mesh
        // inspection only consumes the skinning Boolean, so no View/vector
        // snapshots or reads of the other ~1100 private words are required.
        uint8_t* const span=runtime.pointer(record.parameters->address,owned*4,false);
        return __builtin_bswap32(*(volatile uint32_t*)(span+4*index));
    }
    RigidPacketOwner packetOwner(uint32_t packet) const {
        auto* base=runtime.base;
        try {
            return resolveRigidPacketOwner(packet,
                [&](uint32_t address){runtime.pointer(address,4,false);return PPC_LOAD_U32(address);},
                [&](uint32_t identity){
                    const auto& record=find(identity);const auto& v=record.view;
                    need(v.phase==Phase::Reflected&&record.typedReflections==1,
                         "Rigid packet identity has no live reflected owner");
                    return RigidPacketRecord{v.identity,v.source,v.manager,v.wrapper,record.contextIdentity};
                });
        } catch(const std::exception& error) {
            std::fprintf(stderr,"[NATIVE PACKET OWNER] %s\n",error.what());
            throw Failure(error.what());
        }
    }
    const RigidPayload& requireRigidPayload(uint32_t packet,uint32_t identity) const {
        auto* base=runtime.base;runtime.engineDriver->recordingOwners().requireReplay(packet,identity);
        const auto at=rigidPayloads.find(identity);need(at!=rigidPayloads.end(),"Rigid replay has no retained effect payload");
        const auto& payload=at->second;const auto& record=find(payload.effect);const auto& v=record.view;
        need(payload.id==identity&&payload.native&&
             (v.source==0x8211F480?(payload.monoReplayValues&&!payload.replayValues&&!payload.skyReplayValues):
              v.source==0x82036448?(payload.skyReplayValues&&!payload.replayValues&&!payload.monoReplayValues):
              (payload.replayValues&&!payload.skyReplayValues&&!payload.monoReplayValues))&&
             payload.vertex&&payload.pixel&&(isRigidSource(v.source)||v.source==0x82036448||v.source==0x8211F480)&&
             v.phase==Phase::Reflected&&record.typedReflections==1&&
             !record.recordingContext&&record.contextIdentity==payload.context&&
             PPC_LOAD_U32(packet)==payload.metadata&&PPC_LOAD_U32(packet+4)==payload.object&&
             PPC_LOAD_U32(packet+8)==payload.camera&&PPC_LOAD_U32(packet+0x18)==payload.typed&&
             PPC_LOAD_U32(packet+0x14)==payload.context&&PPC_LOAD_U32(packet+0x20)==payload.shadowOwner&&
             PPC_LOAD_U32(payload.typed)==(v.source==0x8211F480?0x8215020Cu:0x820616C0u)&&PPC_LOAD_U32(payload.typed+0x1C)==payload.effect&&
             PPC_LOAD_U32(payload.typed+0x10)==v.manager&&PPC_LOAD_U32(payload.typed+0x18)==v.wrapper&&
             PPC_LOAD_U32(v.wrapper+0x10)==payload.effect&&PPC_LOAD_U32(v.wrapper+0xC)==v.manager,
             "Rigid replay differs from its retained effect/object/metadata ownership");
        const auto receipt=backend.recordingPayloadReceipt(payload.native);
        need(receipt.state==Graphics::NativeRecordingPayloadState::Sealed&&receipt.recordedDraws==payload.draws&&
             receipt.ownedDataBytes==payload.bytes,"Rigid replay native payload receipt changed");
        if(payload.monoReplayValues)backend.validateMonoShaders(*payload.vertex,*payload.pixel);
        else backend.validateRigidShaders(*payload.vertex,*payload.pixel);
        return payload;
    }
    void requireRigidReplayFinished() const {
        if(!rigidReplay.cpu)return;
        const auto at=rigidPayloads.find(rigidReplay.payload);
        need(at!=rigidPayloads.end()&&rigidReplay.complete&&rigidReplay.prepared,
             "A previous rigid replay has not completed its staging");
        const auto receipt=backend.recordingPayloadReceipt(at->second.native);
        need(receipt.state==Graphics::NativeRecordingPayloadState::Sealed&&rigidReplay.executions<UINT64_MAX&&
             rigidReplay.executedDraws<=UINT64_MAX-at->second.draws&&
             receipt.executions==rigidReplay.executions+1&&receipt.executedDraws==rigidReplay.executedDraws+at->second.draws,
             "A previous rigid replay has not actually executed its retained payload");
    }
    Record& edgeOwner(PPCContext& c,uint8_t* base,uint32_t wrapper,uint32_t typed=0) {
        require(base);need(currentContext==&c,"Native edge setter has no current original frame");
        if(!typed)typed=c.r31.u32;
        runtime.pointer(typed,0xA8,false);runtime.pointer(wrapper,0x30,false);
        auto& record=find(PPC_LOAD_U32(wrapper+0x10));const auto& v=record.view;
        if(v.source!=0x8202DF98 && v.source!=0x8202FA78 && v.source!=0x82034008) {
            char message[220];std::snprintf(message,sizeof(message),
                "Unported native effect activation/setter: source=%08X id=%08X typed=%08X caller=%08X entry=%08X r3=%08X r4=%08X r5=%08X",
                v.source,v.identity,c.r31.u32,uint32_t(c.lr),c.lastFunction,c.r3.u32,c.r4.u32,c.r5.u32);throw Failure(message);
        }
        need(v.phase==Phase::Reflected && record.typedReflections==1 &&
             PPC_LOAD_U32(typed)==postProfile(v.source).vtable && PPC_LOAD_U32(typed+0x18)==wrapper &&
             v.wrapper==wrapper && PPC_LOAD_U32(wrapper)==0x820B7140 &&
             v.manager==PPC_LOAD_U32(0x82D08BFC) && PPC_LOAD_U32(wrapper+0xC)==v.manager &&
             PPC_LOAD_U32(wrapper+0x1C)==v.cache && PPC_LOAD_U32(wrapper+0x20)==v.cacheBytes,
             "Native edge setter requires the exact reflected edge owner");
        validatePool(v.pool);
        return record;
    }
    uint32_t edgeSlot(Record& record,uint32_t handle) {
        const auto entries=record.metadata->parameters(false);
        const auto at=std::find_if(entries.begin(),entries.end(),[&](const auto& p){return p.handle==handle;});
        need(at!=entries.end() && !(at->descriptorWords[0]&3),"Native edge parameter handle is not a private leaf");
        const uint32_t offset=16*(at->descriptorWords[1]&0xFFFF),bytes=uint32_t(record.view.defaultVectorWords.size()*4);
        need(offset<bytes && bytes-offset>=16,"Native edge parameter exceeds owned storage");
        if(!record.parameters)record.parameters=std::make_unique<Record::ParameterStorage>(runtime,record.view.defaultVectorWords);
        const uint32_t leaf=(handle>>1)&0x1FFFF;
        need(leaf<1024,"Native edge dirty bit exceeds owner");
        record.privateModified[leaf/8]|=uint8_t(0x80>>(leaf&7));
        return record.parameters->address+offset;
    }
    void edgeCache(const Record& record) {
        auto* base=runtime.base;const auto& v=record.view;const auto& pass=record.metadata->techniques().front();
        const auto c=v.cache,w=v.wrapper;
        const auto& profile=postProfile(v.source);const uint32_t scalarCount=profile.scalars;
        need(record.metadata->techniques().size()==1 && pass.handle==0x0003FFFC && pass.passHandle==0x0003FFFE &&
             pass.vertexShaderAddress==profile.vertex && pass.pixelShaderAddress==profile.pixel &&
             pass.scalars.size()==scalarCount && pass.samplers.size()==profile.samplers,"Native post-effect pass metadata differs");
        need(PPC_LOAD_U32(w+0x14)==w && PPC_LOAD_U32(w+0x18)==v.identity && PPC_LOAD_U32(w+0x24)==c &&
             PPC_LOAD_U32(w+0x28)==1 && PPC_LOAD_U32(w+0x2C)==(activeEdgeId?c:0),"Native edge CPU cache owner differs");
        runtime.pointer(c,v.cacheBytes,false);
        const std::array<uint32_t,6> header={pass.handle,0,c+24,c+24+12*scalarCount,scalarCount,profile.samplers};
        for(uint32_t i=0;i<header.size();++i)need(PPC_LOAD_U32(c+4*i)==header[i],"Native edge cache header changed");
        uint32_t at=c+24;
        for(const auto& row:pass.scalars){
            need(PPC_LOAD_U32(at)==PPC_LOAD_U32(0x82E06F80+row.sdkId) && PPC_LOAD_U32(at+4)==row.value,
                 "Native edge application scalar cache changed");at+=12;
        }
        for(const auto& row:pass.samplers){
            need(PPC_LOAD_U32(at)==row.stage && PPC_LOAD_U32(at+4)==PPC_LOAD_U32(0x82E07118+row.sdkId) &&
                 PPC_LOAD_U32(at+8)==row.value,"Native edge application sampler cache changed");at+=16;
        }
        need(at==c+v.cacheBytes,"Native edge cache extent differs");
    }
    Record& activeRecord() {
        require(runtime.base);need(activeEdgeId && edgeVertex && edgePixel,"Native edge has no active compiled pass");
        auto& record=find(activeEdgeId);const auto& v=record.view;auto* base=runtime.base;
        runtime.pointer(edgeTyped,0xA8,false);runtime.pointer(v.wrapper,0x30,false);
        need(PPC_LOAD_U32(edgeTyped)==postProfile(v.source).vtable &&
             PPC_LOAD_U32(edgeTyped+0x10)==v.manager && PPC_LOAD_U32(edgeTyped+0x18)==v.wrapper &&
             PPC_LOAD_U32(edgeTyped+0x1C)==v.identity && v.manager==PPC_LOAD_U32(0x82D08BFC) &&
             PPC_LOAD_U32(v.wrapper+0xC)==v.manager && PPC_LOAD_U32(v.wrapper+0x10)==v.identity &&
             PPC_LOAD_U32(v.manager+4)==v.wrapper && PPC_LOAD_U32(v.manager+8)==v.identity &&
             PPC_LOAD_U32(v.manager+0xC)==0x0003FFFC,"Native edge active association changed");
        const auto camera=runtime.engineDriver->cameraBinding();
        need(camera.camera==edgeCamera && PPC_LOAD_U32(0x82E3DD60)==edgeCamera &&
             PPC_LOAD_U32(0x82D0CB1C)==1 && camera.viewport==std::array<uint32_t,6>{0,0,1280,720,0x3F800000,0},
             "Native edge active camera changed");
        edgeCache(record);validatePool(v.pool);backend.requireEdgeShaders(*edgeVertex,*edgePixel);return record;
    }
    Record& activeShadowRecord(PPCContext& c,uint8_t* base,uint32_t typed=0,bool alpha=false) {
        require(base);
        need(currentContext==&c&&activeShadowId&&!activeEdgeId&&shadowVertex&&(typed?typed:c.r31.u32)==shadowTyped&&
             shadowTechnique==(alpha?0x0003FFFCu:0x0007FFFCu)&&bool(shadowPixel)==alpha,
             "Character shadow parameter has no active original owner");
        auto& record=find(activeShadowId);const auto& v=record.view;
        runtime.pointer(shadowTyped,0x6C0,false);runtime.pointer(v.wrapper,0x30,false);runtime.pointer(v.manager,0x18,false);
        need(v.source==0x820C0550&&v.phase==Phase::Reflected&&record.typedReflections==1&&
             PPC_LOAD_U32(shadowTyped)==0x8214E518&&PPC_LOAD_U32(shadowTyped+0x10)==v.manager&&
             PPC_LOAD_U32(shadowTyped+0x18)==v.wrapper&&PPC_LOAD_U32(shadowTyped+0x1C)==v.identity&&
             PPC_LOAD_U32(shadowTyped+(alpha?0x5E0:0x5DC))==shadowTechnique&&v.manager==PPC_LOAD_U32(0x82D08BFC)&&
             PPC_LOAD_U32(v.wrapper)==0x820B7140&&PPC_LOAD_U32(v.wrapper+0xC)==v.manager&&
             PPC_LOAD_U32(v.wrapper+0x10)==v.identity&&PPC_LOAD_U32(v.wrapper+0x14)==v.wrapper&&
             PPC_LOAD_U32(v.wrapper+0x18)==v.identity&&PPC_LOAD_U32(v.wrapper+0x1C)==v.cache&&
             PPC_LOAD_U32(v.wrapper+0x20)==v.cacheBytes&&PPC_LOAD_U32(v.wrapper+0x24)==v.cache&&
             PPC_LOAD_U32(v.wrapper+0x2C)==v.cache+(alpha?0:24)&&PPC_LOAD_U32(v.manager+4)==v.wrapper&&
             PPC_LOAD_U32(v.manager+8)==v.identity&&PPC_LOAD_U32(v.manager+0xC)==shadowTechnique,
             "Character shadow parameter lost its reflected effect association");
        const auto camera=runtime.engineDriver->cameraBinding();
        need(camera.camera==shadowCamera&&PPC_LOAD_U32(0x82E3DD60)==shadowCamera&&
             (shadowCamera==PPC_LOAD_U32(shadowTyped+0x5B4)||shadowCamera==PPC_LOAD_U32(shadowTyped+0x5B8))&&
             runtime.engineDriver->shadowTextures().ownsCamera(shadowCamera)&&
             camera.viewport==std::array<uint32_t,6>{0,0,1024,1024,0x3F800000,0},
             "Character shadow parameter lost its active camera");
        validatePool(v.pool);
        if(alpha)backend.requireShadowAlphaShaders(*shadowVertex,*shadowPixel);
        else backend.requireShadowDepthShader(*shadowVertex);
        return record;
    }
    Record& activeZPrepass(PPCContext& c,uint8_t* base) {
        require(base);
        if(!(currentContext==&c&&zprepassId&&zprepassVertex&&!activeEdgeId&&!activeShadowId)) {
            std::fprintf(stderr,"[NATIVE ZPREPASS OWNER] zprepassId=%08X vertex=%p edge=%08X shadow=%08X lastFunction=%08X lr=%08X\n",
                zprepassId,(const void*)zprepassVertex,activeEdgeId,activeShadowId,c.lastFunction,uint32_t(c.lr));
            try {
                uint32_t frame=c.r1.u32;
                for(unsigned depth=0;depth<8;++depth) {
                    runtime.pointer(frame,16,false);
                    const uint32_t parent=PPC_LOAD_U32(frame);
                    if(parent<=frame||(parent&15)||parent-frame<16||parent-frame>0x10000)break;
                    runtime.pointer(parent-8,4,false);
                    std::fprintf(stderr,"[NATIVE ZPREPASS OWNER] frame%u sp=%08X caller_sp=%08X saved_lr=%08X\n",
                        depth,frame,parent,PPC_LOAD_U32(parent-8));
                    frame=parent;
                }
            } catch(...) {}
        }
        need(currentContext==&c&&zprepassId&&zprepassVertex&&!activeEdgeId&&!activeShadowId,
             "Zprepass has no active native effect owner");
        auto& record=find(zprepassId);const auto& v=record.view;
        runtime.pointer(zprepassTyped,0xC8,false);runtime.pointer(v.wrapper,0x30,false);runtime.pointer(v.manager,0x18,false);
        need(v.source==0x821490E0&&v.phase==Phase::Reflected&&record.typedReflections==1&&
             zprepassTyped==PPC_LOAD_U32(0x82D6D8A0)&&PPC_LOAD_U32(zprepassTyped)==0x8215022C&&
             PPC_LOAD_U32(zprepassTyped+0x18)==v.wrapper&&PPC_LOAD_U32(zprepassTyped+0x1C)==v.identity&&
             PPC_LOAD_U32(v.manager+4)==v.wrapper&&PPC_LOAD_U32(v.manager+8)==v.identity&&
             PPC_LOAD_U32(v.manager+0xC)==0x0003FFFC&&PPC_LOAD_U32(v.wrapper+0x2C)==v.cache,
             "Original zprepass association changed");
        const auto camera=runtime.engineDriver->cameraBinding();
        need(camera.camera==zprepassCamera&&PPC_LOAD_U32(0x82E3DD60)==camera.camera&&
             camera.viewport==std::array<uint32_t,6>{0,0,1280,720,0x3F800000,0},
             "Original zprepass camera changed");
        validatePool(v.pool);backend.requireZPrepassShader(*zprepassVertex);return record;
    }
    Record& activeMono(PPCContext& c,uint8_t* base) {
        require(base);
        need(currentContext==&c&&monoId&&monoVertex&&monoPixel&&!activeEdgeId&&!activeShadowId&&!zprepassId&&!rigidId&&!skinId,
             "Mono has no active native effect owner");
        auto& record=find(monoId);const auto& v=record.view;
        runtime.pointer(monoTyped,0xC0,false);runtime.pointer(v.wrapper,0x30,false);runtime.pointer(v.manager,0x18,false);
        need(v.source==0x8211F480&&v.phase==Phase::Reflected&&record.typedReflections==1&&
             monoTyped==PPC_LOAD_U32(monoPacket+((monoImmediatePath||monoRecordingPath)?0x18u:0x1Cu))&&PPC_LOAD_U32(monoTyped)==0x8215020C&&
             PPC_LOAD_U32(monoTyped+0x18)==v.wrapper&&PPC_LOAD_U32(monoTyped+0x1C)==v.identity&&
             PPC_LOAD_U32(v.manager+4)==v.wrapper&&PPC_LOAD_U32(v.manager+8)==v.identity&&
             PPC_LOAD_U32(v.manager+0xC)==monoTechnique&&PPC_LOAD_U32(v.wrapper+0x2C)==v.cache+(monoTechnique==0x0007FFFC?24u:0u),
             "Original mono association changed");
        const auto camera=runtime.engineDriver->cameraBinding();
        need(camera.camera==monoCamera&&PPC_LOAD_U32(0x82E3DD60)==camera.camera&&
             camera.viewport==std::array<uint32_t,6>{0,0,1280,720,0x3F800000,0},
             "Original mono camera changed");
        validatePool(v.pool);
        if(record.recordingContext)backend.validateMonoShaders(*monoVertex,*monoPixel);
        else backend.requireMonoShaders(*monoVertex,*monoPixel);
        return record;
    }
    D3D11_SAMPLER_DESC edgeState(uint32_t stage=0) const {
        const auto& effective=runtime.engineDriver->effectiveState();
        (void)effective.requireOriginalEdgeState();
        constexpr std::array<uint32_t,20> expected={0,0,0,0,0,0,2,0,0,1,1,1,0,13,0,0,0,0,0,1};
        for(uint32_t i=0;i<expected.size();++i)
            need(effective.sampler(stage,4*i)==expected[i],"Native edge effective point/wrap sampler differs");
        D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxLOD=13;return sampler;
    }
    const EffectRecord& schema() {
        if(!poolSchema) poolSchema=copyEffect(runtime,sharedSource);
        return *poolSchema;
    }
    const Graphics::EffectReflection& cachedReflection(const Record& record,bool notSkinned,bool hasSharedNames) {
        // Four variants keyed on notSkinned AND whether the fixed shared schema
        // names are supplied. Names are built from immutable State metadata only
        // on miss, never read from guest. Full decode publishes after success.
        const size_t slot=(notSkinned?2u:0u)|(hasSharedNames?1u:0u);
        auto& cached=record.reflectionCache[slot];
        if(cached) return *cached;
        std::vector<std::string_view> names;
        if(hasSharedNames) for(const auto& p:schema().parameters(true)) names.push_back(p.name);
        auto fresh=std::make_unique<Graphics::EffectReflection>(*record.metadata,notSkinned,names);
        cached=std::move(fresh);
        return *cached;
    }
    Graphics::EffectBinding cachedPassBinding(const Record& record,uint32_t technique,uint32_t handle) {
        // Exact technique+handle key, derived only from owned metadata. At most
        // 64 successful values per Record; failed decodes never publish and
        // unknown pass/handle still runs the full decoder/rejects. Returned by
        // value so vector reallocation stays safe. If full, decode uncached.
        for(const auto& e:record.passBindingCache)
            if(e.first.first==technique && e.first.second==handle) return e.second;
        Graphics::EffectBinding fresh=Graphics::effectPassBinding(*record.metadata,technique,handle);
        if(record.passBindingCache.size()<Record::kPassBindingBound)
            record.passBindingCache.emplace_back(std::make_pair(technique,handle),fresh);
        return fresh;
    }
    uint32_t monoSharedUsage(const Record& record,const Graphics::EffectTechnique& pass,uint32_t handle) const {
        // Original826F3258 never looks up a local descriptor: the shared
        // pool's leaf may exceed this FX's local shared descriptor count.
        const auto body=record.metadata->body();const auto leaf=(handle>>1)&0x1FFFFu;
        uint32_t usage=0;
        for(uint32_t lane=0;lane<8;++lane) {
            const auto bitmap=word(body,pass.contextOffset+32*(handle&1)+4*lane);
            const uint64_t offset=uint64_t(bitmap)+8*(leaf/64);
            need(offset<=body.size()&&8<=body.size()-offset,"Mono shared usage bitmap exceeds its serialized owner");
            const uint64_t bits=uint64_t(word(body,size_t(offset)))<<32|word(body,size_t(offset)+4);
            usage|=uint32_t((bits>>(63-leaf%64))&1)<<lane;
        }
        return usage;
    }
    // Exact memo of the populated pool's table comparisons (populatedPool), keyed by the pool
    // and backing it was proven for.
    mutable GuestReadMemo poolTables;
    uint32_t poolTablesPool{},poolTablesBacking{};
    void validatePool(uint32_t pool) {
        validatePoolCalls.note(__builtin_return_address(0));
        need(pool==runtime.effectPoolRoot && runtime.effectPoolThread==thread &&
             pool==PPCLoadU32(runtime.base,0x82D6D2F8),"Native FX lost its original CPU pool provenance");
        if(runtime.effectPoolBacking) {
            if(poolTablesPool!=pool || poolTablesBacking!=runtime.effectPoolBacking)poolTables.invalidate();
            if(GuestReadMemo::verify() && poolTables.valid()) {
                // Diagnostic (SIMPSONS_WATCH_VERIFY=1): the full check must agree with the memo.
                try {populatedPool(runtime,pool,schema());}
                catch(const std::exception& error) {
                    std::fprintf(stderr,"[READ MEMO MISMATCH] FX pool tables cached valid but a fresh check failed: %s\n",error.what());
                    std::fflush(stderr);std::abort();
                }
            }
            poolTables.setWatch(runtime.writeWatch());poolTables.setName("fx-pool-tables");
            populatedPool(runtime,pool,schema(),&poolTables);
            poolTablesPool=pool;poolTablesBacking=runtime.effectPoolBacking;
        }
        else emptyPool(runtime,pool);
    }
    void attachShared(PPCContext& ctx,Record& record) {
        const auto local=record.metadata->parameters(true);
        if(local.empty()) return;
        const uint32_t pool=record.view.pool;
        if(!runtime.effectPoolBacking) {
            need(record.view.source==sharedSource,"First shared FX must supply the original eleven-name profile");
            // Retain the original CPU-only aligned allocation/copy/rebase routine.
            // It consumes serialized F, not a relocated or synthetic SDK object.
            EngineCpuCalls cpu(ctx,runtime.base);
            const auto result=cpu.invoke(0x82C181E8,sharedSource+12,pool);
            need(result==0,"Original FX shared metadata allocation failed");
            runtime.effectPoolBacking=PPCLoadU32(runtime.base,pool+0x180);
            populatedPool(runtime,pool,schema());
            const auto defaults=schema().sharedDefaults();
            need(!std::memcmp(runtime.pointer(PPCLoadU32(runtime.base,pool+0x108),uint32_t(defaults.size()),false),
                              defaults.data(),defaults.size()),"Original FX shared initial defaults differ");
        }
        const auto common=schema().parameters(true);
        need(local.size()<=common.size(),"Unqualified FX shared name profile");
        for(size_t i=0;i<local.size();++i)
            need(local[i].name==common[i].name && local[i].handle==common[i].handle &&
                 local[i].descriptorWords==common[i].descriptorWords,"FX shared association needs an unqualified merge");
        // These profiles reuse existing pool indices. Never recopy defaults or
        // borrow the unchanged serialized pointer cells as live shared storage.
    }
    void requireSkinOpaqueContext(uint32_t identity) const {
        // r3 is the owned opaque native scene context, not guest memory.
        // Once the exact owned mesh context, driver readiness/ownership and
        // the context-ID namespace are proven, fresh acquire-ordered
        // pageAccess loads proving zero for every page of the 4-byte range
        // establish unmapped without throwing/catching an exception.
        need(identity==skinMesh.context,"Skin opaque context differs from its owned mesh context");
        runtime.engineDriver->requireContext(identity);
        need(identity>=0x00900001 && identity<0x00A00000,"Skin opaque context is outside the owned context-ID namespace");
        const uint64_t first=uint64_t(identity)>>12,last=(uint64_t(identity)+3)>>12;
        for(uint64_t page=first;page<=last;++page)
            need(runtime.pageAccess[page].load(std::memory_order_acquire)==0,"Skin opaque context overlaps guest memory");
    }
    uint32_t allocateIdentity() {
        uint32_t id=nextIdentity.load();
        while(id<0x00600000) if(nextIdentity.compare_exchange_weak(id,id+1)) {
            need(!runtime.pageAccess[id>>12].load(),"Native FX identity overlaps guest memory");return id;
        }
        throw Failure("Native FX identity space exhausted");
    }
};
EngineEffects::EngineEffects(Runtime& rt,Graphics::NativeBackend& backend):state(std::make_unique<State>(rt,backend)) {}
EngineEffects::~EngineEffects() {
    if(!state->records.empty()) std::fprintf(stderr,"[NATIVE EFFECT] terminal release of %zu native effects; original wrapper/cache cleanup was not completed\n",state->records.size());
}
void EngineEffects::create(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.require(base);
    const uint32_t wrapper=ctx.r31.u32,manager=PPC_LOAD_U32(0x82D08BFC);
    need(ctx.lastFunction==0x826B4B88 && uint32_t(ctx.lr)==0x82701A94,
         "Native FX requires the original engine registration caller");
    (void)profile(ctx.r4.u32);
    auto* object=s.runtime.pointer(wrapper,0x30,true);
    need(manager && PPC_LOAD_U32(wrapper)==0x820B7140 && PPC_LOAD_U32(wrapper+12)==manager &&
         ctx.r6.u32==wrapper+16 && ctx.r3.u32==PPC_LOAD_U32(manager+20) &&
         ctx.r5.u32==PPC_LOAD_U32(manager+24),"Native FX wrapper/context/shared-pool ABI differs");
    s.runtime.engineDriver->requireContext(ctx.r3.u32);s.validatePool(ctx.r5.u32);
    need(!PPC_LOAD_U32(0x82D51548) && !PPC_LOAD_U32(0x82D51544),"Native FX cannot omit original optional shader observers");
    need(!PPC_LOAD_U32(wrapper+16) && !PPC_LOAD_U32(wrapper+28) && !PPC_LOAD_U32(wrapper+32) &&
         PPC_LOAD_U32(wrapper+20)==wrapper,"Native FX wrapper is not freshly constructed");
    for(const auto& [id,record]:s.records) { (void)id;need(record->view.wrapper!=wrapper,"Native FX wrapper already owns an effect"); }
    need(s.records.size()<64,"Native FX ownership bound exceeded");
    auto record=std::make_unique<State::Record>(s.runtime,wrapper,manager,ctx.r4.u32,ctx.r5.u32,s.compiler);
    record->contextIdentity=ctx.r3.u32;
    s.attachShared(ctx,*record);
    const uint32_t token=s.allocateIdentity();record->view.identity=token;
    s.records.emplace(token,std::move(record));store(object+16,token);
    ctx.r3.u64=0;ctx.lr=0x826B4BB0;
}
void EngineEffects::reflect(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.require(base);const uint32_t helper=ctx.r3.u32;
    need(helper>=20 && uint32_t(ctx.lr)==0x826B4BB8,"Native FX reflection requires its original creation call");
    const uint32_t wrapper=helper-20;auto* object=s.runtime.pointer(wrapper,0x30,true);
    auto& record=s.find(PPC_LOAD_U32(wrapper+16));auto& v=record.view;
    need(v.wrapper==wrapper && v.manager==PPC_LOAD_U32(0x82D08BFC) &&
         v.phase==Phase::Created && PPC_LOAD_U32(helper)==wrapper && !PPC_LOAD_U32(wrapper+28) &&
         !PPC_LOAD_U32(wrapper+32),"Native FX reflection owner/initial cache differs");
    try {
        std::vector<uint32_t> scalarSelectors,samplerSelectors;
        for(const auto row:v.scalars) {
            need(!(row.sdkId&3) && row.sdkId<=324,"Unqualified FX scalar selector");
            const auto selector=PPC_LOAD_U32(0x82E06F80+row.sdkId);
            need(selector<0x57,"Unported original scalar remapping");scalarSelectors.push_back(selector);
        }
        for(const auto row:v.samplers) {
            need(row.stage<16 && !(row.sdkId&3) && row.sdkId<=44,"Unqualified FX sampler selector");
            const auto selector=PPC_LOAD_U32(0x82E07118+row.sdkId);
            need(selector>=1 && selector<=20,"Unported original sampler remapping");samplerSelectors.push_back(selector);
        }
        EngineCpuCalls cpu(ctx,base);const uint32_t cache=cpu.invoke(0x8269BE40,v.cacheBytes);
        need(cache!=0,"Original FX state-cache allocation failed");
        auto* rows=s.runtime.pointer(cache,v.cacheBytes,true);
        const auto& techniques=record.metadata->techniques();
        uint32_t cursor=uint32_t(24*techniques.size());size_t scalarIndex=0,samplerIndex=0;
        for(size_t i=0;i<techniques.size();++i) {
            const auto& technique=techniques[i];const uint32_t scalarCount=uint32_t(technique.scalars.size());
            const uint32_t samplerCount=uint32_t(technique.samplers.size()),samplerAt=cursor+12*scalarCount;
            auto* header=rows+24*i;
            store(header,technique.handle);store(header+4,0);store(header+8,cache+cursor);
            store(header+12,cache+samplerAt);store(header+16,scalarCount);store(header+20,samplerCount);
            for(const auto row:technique.scalars) {
                store(rows+cursor,scalarSelectors[scalarIndex++]);store(rows+cursor+4,row.value);cursor+=12;
            }
            for(const auto row:technique.samplers) {
                store(rows+cursor,row.stage);store(rows+cursor+4,samplerSelectors[samplerIndex++]);
                store(rows+cursor+8,row.value);cursor+=16;
            }
            // Saved-previous-value words are left untouched. State blocks are
            // interleaved per technique, exactly like original 826B4828.
        }
        need(cursor==v.cacheBytes,"Native FX cache extent differs");
        store(object+24,v.identity);store(object+28,cache);store(object+32,v.cacheBytes);
        store(object+36,cache);store(object+40,uint32_t(techniques.size()));
        v.cache=cache;v.phase=Phase::Reflected;
        const auto& name=record.metadata->identity().name;
        std::fprintf(stderr,"[NATIVE EFFECT] %.*s metadata/reflection owned wrapper=%08X identity=%08X cache=%08X bytes=%X shaders=%zu compiled=%zu; application guarded\n",
                     int(name.size()),name.data(),wrapper,v.identity,cache,v.cacheBytes,record.shaders.size(),compiledShaderCount(v.identity));
    }catch(...) {v.phase=Phase::Failed;throw;}
}
void EngineEffects::reflectTyped(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.require(base);const uint32_t t=ctx.r3.u32;
    need(ctx.lastFunction==0x826B5168 && (uint32_t(ctx.lr)==0x826B7544 || uint32_t(ctx.lr)==0x826B75C8),
         "Native typed reflection requires the original named/technique gateway");
    s.runtime.pointer(t,0xA8,true);
    auto& record=s.find(PPC_LOAD_U32(t+0x1C));const auto& v=record.view;
    need(v.phase==Phase::Reflected && PPC_LOAD_U32(t+0x18)==v.wrapper && PPC_LOAD_U32(t+0x10)==v.manager &&
         v.manager==PPC_LOAD_U32(0x82D08BFC) && PPC_LOAD_U32(v.wrapper+0x10)==v.identity,
         "Native typed reflection lost its wrapper/manager identity");
    s.validatePool(v.pool);
    const auto& identity=record.metadata->identity();const uint32_t row=identity.row;
    need(row!=0 && row!=2,"This FX does not use the common reflection gateway");
    const uint32_t table=row<25?0x82CEFD20+16*row:0x82CD1448+16*(row-25);
    need(PPC_LOAD_U32(table)==v.source && PPC_LOAD_U32(table+8)==v.wrapper,
         "Native typed reflection lost its registration row");
    const uint32_t callback=PPC_LOAD_U32(table+12),vt=typedVtable(callback);
    const bool skinned=callback==0x8273AE18 || callback==0x823CA960;
    const uint32_t predicate=skinned?0x823CA888:0x823C7F28;
    need(PPC_LOAD_U32(t)==vt && PPC_LOAD_U32(vt+16)==predicate &&
         PPC_LOAD_U32(predicate)==(skinned?0x38600001u:0x38600000u) && PPC_LOAD_U32(predicate+4)==0x4E800020,
         "Native typed reflection feature predicate/type differs");
    need((row==3)==(uint32_t(ctx.lr)==0x826B75C8),"Wrong original typed reflection gateway variant");
    need(guestName(s.runtime,PPC_LOAD_U32(t+8))==identity.name,"Native typed reflection name/source differs");
    need(record.typedReflections!=UINT32_MAX,"Native typed reflection counter exhausted");

    struct Region {
        uint32_t address,size;uint8_t* target;std::vector<uint8_t> bytes;
    };
    auto region=[&](uint32_t address,uint32_t size) {
        need(address && !(address&3),"Native typed reflection output alignment");
        auto* p=s.runtime.pointer(address,size,true);return Region{address,size,p,{p,p+size}};
    };
    std::array<Region,4> outputs{region(t,0xA8),region(PPC_LOAD_U32(t+0x28),1536),
                                region(PPC_LOAD_U32(t+0x30),672),region(PPC_LOAD_U32(t+0x38),480)};
    auto separate=[](uint32_t a,uint32_t an,uint32_t b,uint32_t bn) {return uint64_t(a)+an<=b || uint64_t(b)+bn<=a;};
    need(outputs[2].address>=4 && PPC_LOAD_U32(outputs[2].address-4)==24,"Native typed classification allocation header differs");
    for(size_t i=0;i<outputs.size();++i) {
        const auto& a=outputs[i];
        for(size_t j=0;j<i;++j) need(separate(a.address,a.size,outputs[j].address,outputs[j].size),"Aliased typed reflection outputs");
        if(i!=2) need(separate(a.address,a.size,outputs[2].address-4,4),"Typed reflection output aliases the classification allocation header");
        need(separate(a.address,a.size,v.wrapper,0x30) && separate(a.address,a.size,v.cache,v.cacheBytes) &&
             separate(a.address,a.size,v.pool,0x200) && separate(a.address,a.size,v.source,identity.recordBytes) &&
             (!s.runtime.effectPoolBacking || separate(a.address,a.size,s.runtime.effectPoolBacking,0x2A4)),
             "Typed reflection output aliases its FX/pool owner");
    }
    const bool hasSharedReflectionNames=bool(s.runtime.effectPoolBacking);
    const Graphics::EffectReflection& decoded=s.cachedReflection(record,!skinned,hasSharedReflectionNames);
    // Native replacement of the transient SDK adapters uses this live Record's
    // existing pool lease. Their non-escaping GetPool pointers/extra SDK retains
    // are not guest resources here. No pool reference escapes in decoded values.
    // All following original calls are qualified CPU lookup/constant-return code.
    {
        EngineCpuCalls cpu(ctx,base);
        need(cpu.invoke(0x826B7088,v.manager,PPC_LOAD_U32(t+8))==t,"Typed reflection object is not registered with its manager");
        for(size_t i=0;i<decoded.passes.size();++i)
            need(cpu.invoke(predicate,t)==uint32_t(skinned),"Original typed predicate result differs");
    }
    auto privateMask=record.privateMask;
    auto* sharedTarget=s.runtime.pointer(v.pool+0x80,128,true);
    std::array<uint8_t,128> sharedMask;std::copy_n(sharedTarget,sharedMask.size(),sharedMask.begin());
    auto binding=[&](std::vector<uint8_t>& bytes,size_t at,const Graphics::EffectBinding& b) {
        need(at<=bytes.size() && bytes.size()-at>=24,"Typed reflection binding output extent");
        store(bytes.data()+at,b.handle);store(bytes.data()+at+4,b.usage);
        for(size_t lane=0;lane<2;++lane) {
            if(b.lanes[lane]) {
                store(bytes.data()+at+8+4*lane,b.lanes[lane]->start);
                store(bytes.data()+at+16+4*lane,b.lanes[lane]->count);
            }
            if(b.arrayElements) store(bytes.data()+at+16+4*lane,*b.arrayElements);
        }
        // Inactive incidental range fields keep the caller's existing bytes.
        // They are not invented zeroes or serialized neighbors of SDK vectors.
    };
    auto& object=outputs[0].bytes;
    store(object.data()+0x24,uint32_t(decoded.parameters.size()));
    store(object.data()+0x2C,decoded.reflectionCubeIndex);
    store(object.data()+0x34,uint32_t(decoded.classified.size()));
    store(object.data()+0x44,decoded.lightFlags);
    for(size_t i=0;i<decoded.parameters.size();++i) binding(outputs[1].bytes,24*i,decoded.parameters[i].binding);
    for(size_t i=0;i<decoded.classified.size();++i)
        for(size_t w=0;w<6;++w) store(outputs[2].bytes.data()+28*i+4*w,decoded.classified[i].words[w]);
    if(!decoded.lights.empty()) {
        for(size_t i=0;i<decoded.lights.size();++i)
            for(size_t member=0;member<5;++member) binding(outputs[3].bytes,120*i+24*member,decoded.lights[i].members[member]);
        store(object.data()+0x3C,uint32_t(decoded.lights.size()));store(object.data()+0x40,decoded.activeLights);
    }
    object[0x20]=1;
    for(size_t i=0;i<decoded.passes.size();++i) {
        const auto& p=decoded.passes[i];const size_t at=0x48+48*i;
        store(object.data()+at,p.techniqueHandle);store(object.data()+at+4,p.passHandle);
        for(size_t lane=0;lane<2;++lane) {
            const size_t offset=at+8+8*lane;
            storeDouble(object.data()+offset,doubleWord(object,offset)|p.masks[lane]);
        }
        for(const auto& c:p.clears) {
            need(c.namespaceIndex<2,"Unqualified dirty-mask namespace");
            const uint32_t leaves=c.namespaceIndex?word(s.schema().body(),0x134):word(record.metadata->body(),0x130);
            need(leaves<=1024 && c.firstLeaf<=leaves && c.leafCount<=leaves-c.firstLeaf,"Dirty-mask subtree exceeds qualified leaves");
            auto& mask=c.namespaceIndex?sharedMask:privateMask;
            for(uint32_t leaf=c.firstLeaf;leaf<c.firstLeaf+c.leafCount;++leaf) mask[leaf/8]&=uint8_t(~(0x80u>>(leaf%8)));
        }
    }
    // All decoding, original callbacks and output preflights completed. The
    // owner thread publishes only already-validated CPU buffers and mask bytes.
    for(const auto& output:outputs) std::memcpy(output.target,output.bytes.data(),output.size);
    std::copy(sharedMask.begin(),sharedMask.end(),sharedTarget);record.privateMask=privateMask;
    ++record.typedReflections;ctx.r3.u64=0;
    std::fprintf(stderr,"[NATIVE REFLECTION] %.*s typed=%08X parameters=%zu classes=%zu lightChildren=%zu passes=%zu calls=%u; CPU publication only, application guarded\n",
                 int(identity.name.size()),identity.name.data(),t,decoded.parameters.size(),decoded.classified.size(),decoded.lights.size(),decoded.passes.size(),record.typedReflections);
}
void EngineEffects::initializeShadowSamplers(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.require(base);const uint32_t t=ctx.r31.u32;
    // lastFunction records the most recent entered callee, not a call stack.
    need(ctx.lastFunction==0x823C7CA0 && uint32_t(ctx.lr)==0x82706BC8 && ctx.r29.u32==0x82061428,
         "Shadow sampler initialization requires its original finalizer/LUT");
    s.runtime.pointer(t,0x6C0,false);
    auto& record=s.find(PPC_LOAD_U32(t+0x1C));auto& v=record.view;
    need(v.source==0x820C0550 && v.phase==Phase::Reflected && record.typedReflections &&
         PPC_LOAD_U32(t)==0x8214E518 && PPC_LOAD_U32(t+0x18)==v.wrapper && ctx.r30.u32==v.wrapper &&
         PPC_LOAD_U32(v.wrapper+0x10)==v.identity && PPC_LOAD_U32(t+0x10)==v.manager &&
         PPC_LOAD_U32(0x82D08BFC)==v.manager && PPC_LOAD_U32(0x82CEFD68)==v.wrapper,
         "Shadow sampler initialization lost its typed FX ownership");
    s.validatePool(v.pool);
    need(ctx.r28.u32==0x0148009E && PPC_LOAD_U32(t+0x690)==0x0148009E &&
         PPC_LOAD_U32(t+0x69C)==0x014C00A0,
         "Original shadow sampler query handles differ");
    constexpr std::array<uint8_t,8> lut={128,64,32,16,8,4,2,1};
    need(!std::memcmp(s.runtime.pointer(0x82061428,8,false),lut.data(),lut.size()),"Shadow bit-mask LUT changed");
    auto& driver=*s.runtime.engineDriver;
    for(const uint32_t field:{0xF0u,0xF4u,0xFCu}) {
        const auto resource=driver.shadowTextures().view(PPC_LOAD_U32(t+field));
        need(resource.owner==t && resource.field==field && !resource.staging &&
             resource.phase==(field==0xFC?EngineShadowTextures::Phase::Uploaded:EngineShadowTextures::Phase::Allocated),
             "Shadow finalizer sampler texture ownership/phase differs");
    }
    const uint32_t borrowed=PPC_LOAD_U32(t+0xF8);
    need(borrowed && borrowed==PPC_LOAD_U32(0x82D0CF84) && bool(driver.depth(borrowed)),
         "Shadow first-depth sampler lost its borrowed native driver role");
    struct Write {size_t wordIndex;uint32_t leaf,value;};
    std::array<Write,2> writes{};
    const auto parameters=record.metadata->parameters(false);
    for(size_t i=0;i<writes.size();++i) {
        const uint32_t handle=i?0x014C00A0:0x0148009E,slot=i?278:277;
        const auto p=std::find_if(parameters.begin(),parameters.end(),[&](const auto& p){return p.handle==handle;});
        need(p!=parameters.end() && p->name==(i?"kFirstDepthSampler":"kShadowBackDepthSampler") &&
             p->descriptorWords[0]==0xC && p->descriptorWords[1]==slot &&
             size_t(slot)*4<v.defaultVectorWords.size(),"Unqualified private shadow sampler descriptor/storage");
        writes[i]={size_t(slot)*4,(handle>>1)&0x1FFFF,PPC_LOAD_U32(t+(i?0xF8:0xF0))};
        need(writes[i].leaf<1024,"Private shadow sampler modification bit exceeds mask");
    }
    // The original inline stores borrow texture identities without AddRef. The
    // typed shadows/driver owners still own these resources. Native effect values
    // carry the same identity, never a synthetic console SDK object pointer.
    for(const auto& w:writes) {
        v.defaultVectorWords[w.wordIndex]=w.value;
        record.privateModified[w.leaf/8]|=lut[w.leaf%8];
    }
    // Continue at original82706CDC. r29's LUT and r31's typed owner are live;
    // r27 is dead, r28/r30 are overwritten before use. The original pool getter
    // ignores r3, and all later volatile inputs are produced by original code.
    std::fprintf(stderr,"[NATIVE EFFECT] shadows private sampler defaults initialized typed=%08X; original shared-pool tail retained\n",t);
}
void EngineEffects::release(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.require(base);const uint32_t id=ctx.r3.u32;auto& v=s.find(id).view;
    need(s.activeEdgeId!=id&&s.activeShadowId!=id&&s.zprepassId!=id&&s.rigidId!=id&&s.monoId!=id,"Cannot release an active native effect");
    for(const auto& [payloadId,payload]:s.rigidPayloads)
        need(payload.effect!=id,"Cannot release an effect still owned by a retained rigid payload");
    need(ctx.lastFunction==0x826B3910 && ctx.r31.u32==v.wrapper && PPC_LOAD_U32(v.wrapper+16)==id,
         "Native FX release requires the original wrapper destructor");
    if(v.phase==Phase::Reflected)
        need(PPC_LOAD_U32(v.wrapper+28)==v.cache && PPC_LOAD_U32(v.wrapper+32)==v.cacheBytes,
             "Native FX cache ownership changed before original release");
    s.validatePool(v.pool);s.records.erase(id);ctx.r3.u64=0;ctx.lr=0x826B3940;
    // Original tail clears W+10 and frees CPU cache/name/object.
}
void EngineEffects::setShadowWorld(PPCContext& c,uint8_t* base) {
    if(uint32_t(c.lr)==0x8273A94C){monoOperation(c,base,0x82704600);return;}
    auto& s=*state;s.require(base);const uint32_t t=c.r31.u32,sp=c.r1.u32;
    if(uint32_t(c.lr)==0x8270C270) {
        auto& record=s.activeZPrepass(c,base);const auto& v=record.view;
        need(c.lastFunction==0x82704600&&c.r30.u32==s.zprepassTyped&&t==s.zprepassCamera&&
             sp>=0x200&&!(sp&15)&&PPC_LOAD_U32(sp)==sp+0x1C0&&c.r5.u32==sp+0x90&&
             c.r3.u32==v.identity&&c.r4.u32==0x00040001&&PPC_LOAD_U32(s.zprepassTyped+0xC0)==c.r4.u32,
             "Original zprepass combined-matrix helper frame differs");
        const auto parameters=s.schema().parameters(true);
        const auto p=std::find_if(parameters.begin(),parameters.end(),[](const auto& p){return p.handle==0x00040001;});
        need(p!=parameters.end()&&p->name=="g_ViewProjection"&&p->descriptorWords==std::array<uint32_t,2>{0x004007B0,0x00100000},
             "Original zprepass shared matrix descriptor differs");
        const auto destination=sharedParameterStorage(v.identity,c.r4.u32);
        std::array<uint32_t,16> input{};for(uint32_t i=0;i<16;++i)input[i]=PPC_LOAD_U32(c.r5.u32+4*i);
        for(uint32_t i=0;i<4;++i)need(PPC_LOAD_U32(0x82000EC0+4*i)==UINT32_MAX,"Original zprepass transpose mask differs");
        for(uint32_t r=0;r<4;++r)for(uint32_t col=0;col<4;++col)PPC_STORE_U32(destination+16*r+4*col,input[4*col+r]);
        auto* dirty=s.runtime.pointer(v.pool,128,true);dirty[0]|=0x80;
        return;
    }
    need(currentContext==&c && c.lastFunction==0x82704600 && uint32_t(c.lr)==0x82705690 &&
         sp && !(sp&15) && uint64_t(sp)+0xA0<=0x100000000ull && PPC_LOAD_U32(sp)==sp+0xA0 && c.r5.u32==sp+0x50,
         "Native shadow world matrix requires original827055E0 frame");
    s.runtime.pointer(t,0x6C0,false);auto& record=s.find(c.r3.u32);const auto& v=record.view;
    need(v.source==0x820C0550 && v.phase==Phase::Reflected && record.typedReflections==1 &&
         PPC_LOAD_U32(t)==0x8214E518 && PPC_LOAD_U32(t+0x18)==v.wrapper && PPC_LOAD_U32(t+0x1C)==v.identity &&
         PPC_LOAD_U32(t+0x10)==v.manager && v.manager==PPC_LOAD_U32(0x82D08BFC) &&
         PPC_LOAD_U32(v.wrapper)==0x820B7140 && PPC_LOAD_U32(v.wrapper+0xC)==v.manager && PPC_LOAD_U32(v.wrapper+0x10)==v.identity,
         "Native shadow world matrix lost its original reflected typed owner");
    need(c.r4.u32==0x000C0004 && PPC_LOAD_U32(t+0x660)==c.r4.u32,"Original shadow g_World handle changed");
    const auto parameters=record.metadata->parameters(false);
    const auto p=std::find_if(parameters.begin(),parameters.end(),[](const auto& p){return p.handle==0x000C0004;});
    need(p!=parameters.end() && p->name=="g_World" && p->descriptorWords==std::array<uint32_t,2>{0x004007B0,0x00100005},
         "Native shadow world matrix descriptor/extent changed");
    s.validatePool(v.pool);
    need(s.runtime.engineDriver->shadowTextures().ownsCamera(PPC_LOAD_U32(t+0x5B4)) &&
         s.runtime.engineDriver->shadowTextures().ownsCamera(PPC_LOAD_U32(t+0x5B8)),"Shadow world matrix lacks constructor-owned cameras");
    // Original827055E0 assembles four rows from the real frame matrix, with
    // homogeneous lanes0,0,0,1. Original82704600's float-matrix vsel mask is
    // all ones, so its vmrghw/vmrglw sequence is an exact bitwise4x4 transpose.
    std::array<uint32_t,16> input{},output{};
    for(uint32_t i=0;i<16;++i)input[i]=PPC_LOAD_U32(c.r5.u32+4*i);
    need(input[3]==0 && input[7]==0 && input[11]==0 && input[15]==0x3F800000,"Original frame matrix homogeneous lanes changed");
    for(uint32_t i=0;i<4;++i)need(PPC_LOAD_U32(0x82000EC0+4*i)==0xFFFFFFFF,"Original four-column matrix selection mask changed");
    need(PPC_LOAD_U8(0x8206142A)==0x20,"Original g_World modification bit changed");
    for(uint32_t r=0;r<4;++r)for(uint32_t col=0;col<4;++col)output[r*4+col]=input[col*4+r];
    // This is owned parameter data, not an SDK header. Keep all other private
    // values and shared pool data intact. No shader activation/upload occurs.
    const auto destination=s.edgeSlot(record,c.r4.u32);s.runtime.pointer(destination,64,true);
    for(uint32_t i=0;i<16;++i)PPC_STORE_U32(destination+4*i,output[i]);
    static thread_local uint32_t logged{};if(logged++<4)std::fprintf(stderr,
        "[NATIVE SHADOW WORLD] typed=%08X id=%08X g_World=000C0004; original frame assembly, exact matrix transpose, native private values only\n",t,v.identity);
}
uint32_t EngineEffects::technique(uint32_t id,std::string_view name) const {
    state->require(state->runtime.base);const auto& record=state->find(id);
    need(record.view.phase==Phase::Reflected,"Native FX is not ready for metadata queries");
    for(const auto& technique:record.metadata->techniques()) if(name==technique.name) return technique.handle;
    return 0;
}
uint32_t EngineEffects::parameter(uint32_t id,std::string_view name) const {
    state->require(state->runtime.base);const auto& record=state->find(id);
    need(record.view.phase==Phase::Reflected,"Native FX is not ready for metadata queries");
    for(const auto& p:record.metadata->parameters(false)) if(name==p.name) return p.handle;
    if(!record.metadata->parameters(true).empty()) {
        state->validatePool(record.view.pool);
        // Original attach copies P+118 into F+114, so shared FX sees the complete
        // current eleven-name pool, including when its serialized profile has four.
        for(const auto& p:state->schema().parameters(true)) if(name==p.name) return p.handle;
    }
    return 0;
}
void EngineEffects::query(PPCContext& ctx,uint8_t* base,bool isTechnique) {
    auto& s=*state;s.require(base);const uint32_t id=ctx.r3.u32;
    need(ctx.lastFunction==(isTechnique?0x823C7CA0u:0x823C7B20u),"Unqualified native FX query entry");
    const auto& record=s.find(id);
    need(record.view.manager==PPC_LOAD_U32(0x82D08BFC) &&
         PPC_LOAD_U32(record.view.wrapper+16)==id,"Native FX query lost its engine owner");
    const auto name=guestName(s.runtime,ctx.r4.u32);
    ctx.r3.u64=isTechnique?technique(id,name):parameter(id,name);
    // Entry replacement: caller BL and LR effects are retained by original code.
}
void EngineEffects::observeProducerEntry(const PPCContext& c,uint8_t* base,const char* boundary) const noexcept {
    HostState preserve;
    auto& s=*state;if(!s.runtime.resourceAudit.active())return;
    try {
        const bool threadMatch=GetCurrentThreadId()==s.thread;
        const bool runtimeMatch=active==&s.runtime&&base==s.runtime.base;
        const bool readable=threadMatch&&runtimeMatch;
        uint32_t unavailable=0;
        auto rd=[&](uint64_t address,unsigned bit) noexcept -> uint32_t {
            if(readable&&address&&address<=UINT32_MAX-3)try {
                const auto* bytes=s.runtime.pointer(uint32_t(address),4,false);
                return (uint32_t(bytes[0])<<24)|(uint32_t(bytes[1])<<16)|(uint32_t(bytes[2])<<8)|bytes[3];
            }catch(...){}
            unavailable|=uint32_t(1)<<bit;return 0;
        };
        auto field=[&](uint32_t parent,uint32_t offset,unsigned bit) noexcept -> uint32_t {
            return rd(parent?uint64_t(parent)+offset:0,bit);
        };
        const auto caller=uint32_t(c.lr),flags=rd(0x82D6CCA8,0);
        uint32_t packet=c.r31.u32,typed=0;
        if(caller==0x8273A93C) {packet=readable?s.monoPacket:0;typed=c.r30.u32;}
        else if(caller==0x827406C8)typed=rd(0x82D6D8A0,3);
        else if(caller==0x8270614C||caller==0x8270715C||caller==0x827071FC||caller==0x827076DC) {
            typed=c.r31.u32;packet=0;
        } else typed=field(packet,0x18,3);
        const auto wrapperId=field(c.r4.u32,0x10,1),managerId=field(c.r3.u32,8,2),typedId=field(typed,0x1C,4);
        const auto vtable=rd(typed,5),a8=field(typed,0xA8,6),ac=field(typed,0xAC,7);
        const auto metadata=rd(packet,8),geometry=field(metadata,0xC,9);
        const auto stride=field(geometry,4,10),elements=field(geometry,8,11);
        const auto packetCamera=field(packet,8,12),publishedCamera=rd(0x82E3DD60,13);
        const auto submeshCount=field(metadata,0x10,14),submeshes=field(metadata,0x14,15),bones=field(metadata,0x24,16);
        const auto collection=field(metadata,0x34,17);
        const bool meshEntry=boundary&&(std::strcmp(boundary,"skin_mesh_entry")==0||std::strcmp(boundary,"rigid_mesh_entry")==0);
        const auto requestedTechnique=meshEntry?0u:c.r5.u32;
        std::optional<EngineAudioOwners::AllocationSpan> rowOwner;
        const char* rowOwnerState="unreadable";
        if(readable&&!(unavailable&0x8000))try {
            rowOwner=s.runtime.engineAudio?s.runtime.engineAudio->allocationSpan(submeshes):std::nullopt;
            rowOwnerState=rowOwner?"observed-live":"unobserved";
        }catch(...){rowOwnerState="closing-or-unavailable";}
        const State::Record* candidate=nullptr;uint32_t candidateCount=0;
        // The registry belongs to the renderer thread. Never inspect it from
        // a rejected foreign thread merely to improve a diagnostic.
        if(readable)for(const auto id:std::array{wrapperId,typedId}) {
            const auto at=s.records.find(id);
            if(at!=s.records.end()&&at->second&&at->second.get()!=candidate) {
                candidate=at->second.get();++candidateCount;
            }
        }
        if(candidateCount!=1)candidate=nullptr;
        uint32_t source=0,phase=0,reflections=0,vertex=0,pixel=0,pass=0;bool cachedSelection=false;
        if(candidate) {
            source=candidate->view.source;phase=uint32_t(candidate->view.phase);reflections=candidate->typedReflections;
            if(candidate->metadata)for(const auto& selection:candidate->metadata->techniques())if(selection.handle==requestedTechnique) {
                vertex=selection.vertexShaderAddress;pixel=selection.pixelShaderAddress;pass=selection.passHandle;cachedSelection=true;break;
            }
        }
        char asset[40],parameters[1024],ownership[256],instance[1200];
        std::snprintf(asset,sizeof(asset),"source:%08X",source);
        std::snprintf(parameters,sizeof(parameters),
            "boundary=%s source=%08X source_state=%s requested_technique=%08X argument5_role=%s selection_state=%s candidate_vs=%08X candidate_ps=%08X candidate_pass=%08X flags=%08X flags_low8=%u vtable=%08X technique_A8=%08X technique_AC=%08X stride=%u elements=%u camera_matches_global=%u submesh_count=%u bones=%u submesh_owner=%s unreadable_mask=%08X",
            boundary?boundary:"unknown",source,candidate?"cached-candidate":"unresolved",requestedTechnique,meshEntry?"typed-mesh-owner":"technique",
            cachedSelection?"cached-candidate-before-validation":"unresolved",vertex,pixel,pass,flags,flags&255,vtable,a8,ac,stride,elements,
            unsigned(packetCamera&&packetCamera==publishedCamera&&!(unavailable&0x3000)),submeshCount,bones,rowOwnerState,unavailable);
        std::snprintf(ownership,sizeof(ownership),"runtime_match=%u thread_match=%u cpu_match=%u cached_candidates=%u phase=%u typed_reflections=%u admission=unvalidated",
            unsigned(runtimeMatch),unsigned(threadMatch),unsigned(currentContext==&c),candidateCount,phase,reflections);
        std::snprintf(instance,sizeof(instance),
            "r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X r29=%08X r30=%08X r31=%08X r31_low8=%u stack=%08X function=%08X packet=%08X typed=%08X wrapper_identity=%08X manager_identity=%08X typed_identity=%08X metadata=%08X geometry=%08X packet_camera=%08X published_camera=%08X submeshes=%08X collection=%08X submesh_owner_base=%08X submesh_owner_extent=%u submesh_owner_generation=%llu",
            c.r3.u32,c.r4.u32,c.r5.u32,c.r6.u32,c.r7.u32,c.r29.u32,c.r30.u32,c.r31.u32,c.r31.u32&255,c.r1.u32,c.lastFunction,
            packet,typed,wrapperId,managerId,typedId,metadata,geometry,packetCamera,publishedCamera,submeshes,collection,
            rowOwner?rowOwner->address:0,rowOwner?rowOwner->extent:0,static_cast<unsigned long long>(rowOwner?rowOwner->generation:0));
        s.runtime.resourceAudit.observe("effect_producer_entry",asset,caller,parameters,ownership,s.runtime.nativeDepthCopyCount.load(),instance);
    }catch(...){std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] effect producer entry capture failed\n");}
}
void EngineEffects::beginShadowDepth(PPCContext& c,uint8_t* base) {
    observeProducerEntry(c,base,"shadow_begin");
    auto& s=*state;s.require(base);const auto t=c.r31.u32,w=c.r4.u32,m=c.r3.u32;
    const bool alpha=c.r5.u32==0x0003FFFC;
    const bool staticGeometry=uint32_t(c.lr)==0x827076DC;
    if(c.lastFunction==0x826B5FC0&&uint32_t(c.lr)!=0x8270614C&&uint32_t(c.lr)!=0x8270715C&&!staticGeometry){
        std::fprintf(stderr,"[NATIVE SHADOW NEXT CALLER] caller=%08X technique=%08X typed=%08X wrapper=%08X manager=%08X r29=%08X r30=%08X\n",
            uint32_t(c.lr),c.r5.u32,t,w,m,c.r29.u32,c.r30.u32);
        if(uint32_t(c.lr)==0x827071FC||uint32_t(c.lr)==0x827076DC){
            const auto meta=c.r29.u32;s.runtime.pointer(meta,0x2C,false);const auto geometry=PPC_LOAD_U32(meta+0xC);
            s.runtime.pointer(geometry,0x78,false);
            std::fprintf(stderr,"[NATIVE STATIC SHADOW METADATA]");
            for(uint32_t i=0;i<0x2C;i+=4)std::fprintf(stderr," %08X",PPC_LOAD_U32(meta+i));std::fprintf(stderr,"\n");
            std::fprintf(stderr,"[NATIVE STATIC SHADOW GEOMETRY]");
            for(uint32_t i=0;i<0x78;i+=4)std::fprintf(stderr," %08X",PPC_LOAD_U32(geometry+i));std::fprintf(stderr,"\n");
            if(!s.runtime.frameCaptureDirectory.empty()){
                const auto capture=[&](const char* name,uint32_t address,uint32_t bytes){
                    need(bytes<=16*1024*1024,"Static shadow diagnostic extent is too large");
                    const auto* data=s.runtime.pointer(address,bytes,false);
                    std::ofstream out(s.runtime.frameCaptureDirectory/name,std::ios::binary);
                    out.write(reinterpret_cast<const char*>(data),bytes);need(bool(out),"Static shadow diagnostic capture failed");
                };
                capture("static-shadow-metadata.bin",meta,0x2C);capture("static-shadow-geometry.bin",geometry,0x78);
                capture("static-shadow-vertices.bin",PPC_LOAD_U32(geometry+0x10),PPC_LOAD_U32(geometry));
                capture("static-shadow-indices.bin",PPC_LOAD_U32(geometry+0x1C),PPC_LOAD_U32(geometry+0x14));
                capture("static-shadow-elements.bin",PPC_LOAD_U32(geometry+0xC),PPC_LOAD_U32(geometry+8)*12);
                capture("static-shadow-declaration-cache.bin",PPC_LOAD_U32(geometry+0x30),12);
                const auto object=c.r30.u32;capture("static-shadow-object.bin",object,0x24);
                capture("static-shadow-frame.bin",PPC_LOAD_U32(object+4),0xA4);
                const auto objectData=PPC_LOAD_U32(object+0x18);capture("static-shadow-object-data.bin",objectData,0x28);
                const auto count=PPC_LOAD_U32(meta+0x10),submeshes=PPC_LOAD_U32(meta+0x14);
                need(count<=4096,"Static shadow submesh diagnostic extent is too large");
                capture("static-shadow-submeshes.bin",submeshes,36*count);
                const auto offsets=PPC_LOAD_U32(objectData+0x24),materials=PPC_LOAD_U32(0x82D6D814);
                std::ofstream rows(s.runtime.frameCaptureDirectory/"static-shadow-materials.json");rows<<"[";
                for(uint32_t i=0;i<count;++i){
                    const auto index=PPC_LOAD_U32(submeshes+36*i);need(index<65536,"Static shadow material index diagnostic is too large");
                    s.runtime.pointer(offsets+4*index,4,false);const auto offset=PPC_LOAD_U32(offsets+4*index),material=materials+offset;
                    s.runtime.pointer(material,4,false);
                    if(i)rows<<",";rows<<"{\"index\":"<<index<<",\"offset\":"<<offset<<",\"address\":"<<material
                        <<",\"flags\":"<<PPC_LOAD_U16(material)<<",\"reference\":"<<uint32_t(PPC_LOAD_U8(material+2))<<"}";
                }
                rows<<"]\n";need(bool(rows),"Static shadow material diagnostic write failed");
                const auto values=parameterValues(PPC_LOAD_U32(w+0x10));
                need(values.size()>67,"Static shadow private flag diagnostic is absent");
                std::fprintf(stderr,"[NATIVE STATIC SHADOW SKIN FLAG] %08X %08X %08X %08X\n",values[64],values[65],values[66],values[67]);
            }
        }
    }
    need(currentContext==&c&&c.lastFunction==0x826B5FC0&&uint32_t(c.lr)==(alpha?0x8270614Cu:(staticGeometry?0x827076DCu:0x8270715Cu))&&
         c.r5.u32==(alpha?0x0003FFFCu:0x0007FFFCu),
         "Unqualified original character shadow activation");
    s.runtime.pointer(t,0x6C0,false);s.runtime.pointer(w,0x30,false);s.runtime.pointer(m,0x18,false);
    // Alpha draw/resource/constant semantics are a separate capability. An
    // empty original queue still executes its actual shader/state/setter path.
    if(alpha)need(!PPC_LOAD_U32(t+0xAC),"Character shadow alpha queue is not yet qualified");
    auto& record=s.find(PPC_LOAD_U32(w+0x10));const auto& v=record.view;
    State::ShadowPalettePrelude palettePrelude;
    if(!alpha&&!staticGeometry&&c.r29.u32) {
        // The genuine82707138 parent retains metadata inr29 before activation.
        // Bound its composed destination and every group before the original
        // matrix composition/copies run. The interrupted null-owner fixture
        // still stops at its existing skinning-parameter boundary.
        s.runtime.pointer(c.r29.u32,0x2C,false);const auto bones=PPC_LOAD_U32(c.r29.u32+0x24);
        need(bones<=255,"Original character composed palette exceeds bounded storage");
        palettePrelude.cpu=&c;palettePrelude.metadata=c.r29.u32;palettePrelude.object=c.r27.u32;palettePrelude.frame=c.r1.u32;
        palettePrelude.boneGroups=boneGroupRows(s.runtime,bones,c.r29.u32,c.r27.u32,false);
    }
    if(staticGeometry){
        s.runtime.pointer(c.r29.u32,0x2C,false);
        need(!PPC_LOAD_U32(c.r29.u32+0x24)&&!c.r28.u64&&c.r1.u32>=0x200&&!(c.r1.u32&15)&&
             PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0x80,"Original static shadow activation branch/frame differs");
        const auto values=parameterValues(v.identity);
        need(values.size()==1116&&!values[64],"Original static shadow activation retained a skinning flag");
    }
    need(v.source==0x820C0550&&v.phase==Phase::Reflected&&record.typedReflections==1&&
         PPC_LOAD_U32(t)==0x8214E518&&PPC_LOAD_U32(t+0x10)==m&&PPC_LOAD_U32(t+0x18)==w&&PPC_LOAD_U32(t+0x1C)==v.identity&&
         PPC_LOAD_U32(t+(alpha?0x5E0:0x5DC))==c.r5.u32&&v.wrapper==w&&v.manager==m&&m==PPC_LOAD_U32(0x82D08BFC)&&
         PPC_LOAD_U32(w)==0x820B7140&&PPC_LOAD_U32(w+0xC)==m&&PPC_LOAD_U32(w+0x14)==w&&PPC_LOAD_U32(w+0x18)==v.identity&&
         PPC_LOAD_U32(w+0x1C)==v.cache&&PPC_LOAD_U32(w+0x20)==v.cacheBytes&&PPC_LOAD_U32(w+0x24)==v.cache,
         "Character shadow activation lost original reflected ownership");
    s.validatePool(v.pool);
    const auto camera=s.runtime.engineDriver->cameraBinding();
    need(s.runtime.engineDriver->shadowTextures().ownsCamera(camera.camera)&&
         (camera.camera==PPC_LOAD_U32(t+0x5B4)||camera.camera==PPC_LOAD_U32(t+0x5B8))&&
         camera.camera==PPC_LOAD_U32(0x82E3DD60)&&camera.viewport==std::array<uint32_t,6>{0,0,1024,1024,0x3F800000,0},
         "Character shadow activation requires its actual constructor-owned camera");
    const auto techniques=record.metadata->techniques();
    need(techniques.size()>1&&PPC_LOAD_U32(w+0x28)==techniques.size(),"Character shadow cache technique count differs");
    const auto& pass=techniques[alpha?0:1];
    need(pass.name==(alpha?"RenderShadowDepthAlpha":"RenderShadowDepth")&&pass.handle==c.r5.u32&&pass.passHandle==c.r5.u32+2&&
         pass.vertexShaderAddress==(alpha?0x820C1E6Cu:0x820C2FA0u)&&pass.pixelShaderAddress==(alpha?0x820CA530u:0u)&&
         pass.scalars.size()==3&&pass.samplers.size()==(alpha?6u:0u),
         "Original character shadow pass metadata differs");
    constexpr std::array<uint32_t,3> ids{0x28,0x30,0x60};const std::array<uint32_t,3> values{1,1,alpha?1u:0u};
    s.runtime.pointer(v.cache,v.cacheBytes,false);
    const uint32_t header=v.cache+(alpha?0:24);
    const uint32_t rows=v.cache+uint32_t(24*techniques.size()+(alpha?0:12*techniques[0].scalars.size()+16*techniques[0].samplers.size()));
    need(uint64_t(rows)+36+16*pass.samplers.size()<=uint64_t(v.cache)+v.cacheBytes&&PPC_LOAD_U32(header)==pass.handle&&!PPC_LOAD_U32(header+4)&&
         PPC_LOAD_U32(header+8)==rows&&PPC_LOAD_U32(header+12)==rows+36&&PPC_LOAD_U32(header+16)==3&&PPC_LOAD_U32(header+20)==pass.samplers.size(),
         "Original character shadow pass cache changed");
    auto prospective=s.runtime.engineDriver->effectiveState();
    for(size_t i=0;i<3;++i){
        need(pass.scalars[i].sdkId==ids[i]&&pass.scalars[i].value==values[i]&&
             PPC_LOAD_U32(rows+uint32_t(12*i))==PPC_LOAD_U32(0x82E06F80+ids[i])&&PPC_LOAD_U32(rows+uint32_t(12*i)+4)==values[i],
             "Original character shadow literal states changed");
        prospective.setScalar(ids[i],values[i]);
        const auto fields=Graphics::scalarStateEvidence();
        const auto field=std::find_if(fields.begin(),fields.end(),[&](const auto& f){return f.id==ids[i];});
        need(field!=fields.end()&&PPC_LOAD_U32(0x82CD28B8+3*ids[i]+4)==field->setterAddress,"Original character shadow scalar dispatch changed");
    }
    constexpr std::array<uint32_t,6> samplerIds{0,4,8,0x10,0x14,0x18},samplerValues{0,0,0,0,0,2};
    for(size_t i=0;i<pass.samplers.size();++i){
        const auto& row=pass.samplers[i];const auto at=rows+36+uint32_t(16*i);
        need(row.stage==0&&row.sdkId==samplerIds[i]&&row.value==samplerValues[i]&&
             !PPC_LOAD_U32(at)&&PPC_LOAD_U32(at+4)==PPC_LOAD_U32(0x82E07118+row.sdkId)&&PPC_LOAD_U32(at+8)==row.value,
             "Original character shadow sampler cache changed");
        prospective.setSampler(row.stage,row.sdkId,row.value);
        const auto fields=Graphics::samplerStateEvidence();
        const auto field=std::find_if(fields.begin(),fields.end(),[&](const auto& f){return f.id==row.sdkId;});
        need(field!=fields.end()&&PPC_LOAD_U32(0x82CD2D78+3*row.sdkId+4)==field->setterAddress,"Original character shadow sampler dispatch changed");
    }
    need(!s.activeEdgeId,"Post-effect remains active before character shadow activation");
    // These authored bytes are read-only throughout82707138/82706378. Retain
    // their identity before original composition and range selection run.
    s.shadowPalettePrelude=std::move(palettePrelude);
    if(s.activeShadowId){
        s.activeShadowRecord(c,base,0,s.shadowTechnique==0x0003FFFC);
        need(s.activeShadowId==v.identity,"Character shadow activation selected another effect");
        if(s.shadowTechnique==pass.handle)return;
    }
    else need(!PPC_LOAD_U32(m+4)&&!PPC_LOAD_U32(m+0xC)&&!PPC_LOAD_U32(w+0x2C),"Another original effect remains selected");
    need(word(record.metadata->body(),0x120)==2&&word(record.metadata->body(),0x124)==1&&PPC_LOAD_U32(v.pool+0x114)==1,
         "Original character shadow dirty extent changed");
    const Graphics::CompiledMaterial* vertex=nullptr;const Graphics::CompiledMaterial* pixel=nullptr;
    const auto shaders=record.metadata->shaders();need(shaders.size()==record.shaders.size(),"Shadow shader registry changed");
    for(size_t i=0;i<shaders.size();++i){
        if(shaders[i].originalAddress==pass.vertexShaderAddress){
            need(!vertex,"Duplicate character shadow vertex shader");vertex=&record.shaderRecords.prepareForBind(record.shaders[i],s.compiler);
        }else if(pass.pixelShaderAddress&&shaders[i].originalAddress==pass.pixelShaderAddress){
            need(!pixel,"Duplicate character shadow pixel shader");pixel=&record.shaderRecords.prepareForBind(record.shaders[i],s.compiler);
        }
    }
    need(vertex&&bool(pixel)==alpha,"Character shadow shader pair is incomplete");
    if(alpha)s.backend.validateShadowAlphaShaders(*vertex,*pixel);else s.backend.validateShadowDepthShader(*vertex);
    if(s.activeShadowId){EngineCpuCalls cpu(c,base);cpu.registers().lr=0x826B5FF8;cpu.invoke(0x826B4628,m);}
    // Preserve original manager stores and execute its real CPU state-cache
    // save/apply routine. Only the SDK program identity is native-owned.
    PPC_STORE_U32(m+4,w);PPC_STORE_U32(m+8,v.identity);PPC_STORE_U32(m+0xC,pass.handle);
    {EngineCpuCalls cpu(c,base);cpu.registers().lr=0x826B604C;cpu.invoke(0x826B35D8,w+0x14,pass.handle,0);}
    need(PPC_LOAD_U32(w+0x2C)==header,"Original character shadow cache did not select its pass");
    for(const auto& row:pass.scalars)s.runtime.engineDriver->directScalar(base,row.sdkId,row.value);
    for(const auto& row:pass.samplers)s.runtime.engineDriver->directSampler(base,row.stage,row.sdkId,row.value);
    record.privateModified.fill(0);std::fill_n(record.privateModified.begin(),16,uint8_t(0xFF));
    // Original826B1FF0..2050 skips shared reseeding when the global is
    // nonzero and F+2B8 has its live pool (stored by82C1D3BC). The live pool
    // is already provenance-checked above; immutable source F+2B8 is zero.
    if(!PPC_LOAD_U32(0x82D00F80)){
        auto* dirty=s.runtime.pointer(v.pool,128,true);std::memset(dirty,0,128);std::memset(dirty,0xFF,16);
    }
    if(alpha)s.backend.bindShadowAlphaShaders(*vertex,*pixel);else s.backend.bindShadowDepthShader(*vertex);
    s.screenReplacement={};
    s.activeShadowId=v.identity;s.shadowTyped=t;s.shadowCamera=camera.camera;s.shadowVertex=vertex;s.shadowPixel=pixel;s.shadowTechnique=pass.handle;
    {static thread_local uint32_t shadowBeginAlphaSample{},shadowBeginDepthSample{};
    if(sampleHotLog(alpha?shadowBeginAlphaSample:shadowBeginDepthSample))
        std::fprintf(stderr,"[NATIVE SHADOW %s BEGIN] id=%08X typed=%08X camera=%08X; original state cache applied, actual VS%08X PS%08X bound; constants and draw separate\n",alpha?"ALPHA":"DEPTH",v.identity,t,camera.camera,pass.vertexShaderAddress,pass.pixelShaderAddress);}
}
// Mono keeps the original world assembly, callback VMX arithmetic and both
// staging-bank copies. Only FX storage/SDK upload endpoints are native owners.
void EngineEffects::monoOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.require(base);auto& run=s.monoReplay;
    if(site==0x8273A878) {
        const bool immediate=uint32_t(c.lr)==0x82740134;
        const bool recording=uint32_t(c.lr)==0x8274047C;
        need(currentContext==&c&&c.lastFunction==site&&(immediate||recording||uint32_t(c.lr)==0x82740B64)&&
             c.r1.u32>=0x300&&!(c.r1.u32&15)&&PPC_LOAD_U32(c.r1.u32)==c.r1.u32+(immediate?0x80u:recording?0xD0u:0xF0u)&&
             (immediate?(s.monoImmediate.active&&s.monoImmediate.cpu==&c&&c.r1.u32==s.monoImmediate.frame&&
                         c.r7.u32==uint32_t(s.monoImmediate.alpha)&&!PPC_LOAD_U32(0x82D6CCA8)):
              recording?(!c.r7.u32&&!PPC_LOAD_U32(0x82D6CCA8)&&c.r26.u32==0&&c.r25.u32<=1&&c.r30.u32==1):
                        (!c.r7.u32&&PPC_LOAD_U32(0x82D6CCA8)==2)),"Unqualified mono world callback entry");
        const auto packet=c.r31.u32;s.runtime.pointer(packet,0x24,false);s.runtime.pointer(c.r3.u32,0xC0,false);
        s.runtime.pointer(c.r4.u32,0x2C,false);
        need(c.r3.u32==PPC_LOAD_U32(packet+((immediate||recording)?0x18u:0x1Cu))&&c.r4.u32==PPC_LOAD_U32(packet)&&
             c.r5.u32==PPC_LOAD_U32(packet+4)&&c.r6.u32==PPC_LOAD_U32(packet+8)&&
             PPC_LOAD_U32(c.r3.u32)==0x8215020C&&PPC_LOAD_U32(c.r4.u32+0x24)<=255&&
             (!s.monoMesh.native||s.monoMesh.phase==4),"Mono world callback lost its packet or bounded bone palette");
        if(s.monoId){s.activeMono(c,base);need(s.monoTyped==c.r3.u32,"Another mono owner remains active");}
        s.monoPacket=packet;s.monoTyped=c.r3.u32;s.monoFrame=c.r1.u32;s.monoImmediatePath=immediate;s.monoRecordingPath=recording;
        if(recording){s.monoRecording={};s.monoTextures={};}
        s.monoWorld=s.monoBoolean=s.monoMaterial=false;s.monoCommit.reset();s.monoMesh={};s.recycleTempUpload(run.buffers);run={};return;
    }
    auto& record=s.activeMono(c,base);const auto& v=record.view;
    if(site==0x82704600) {
        need(c.lastFunction==site&&uint32_t(c.lr)==0x8273A94C&&c.r1.u32==s.monoFrame-0xB0&&
             PPC_LOAD_U32(c.r1.u32)==s.monoFrame&&c.r30.u32==s.monoTyped&&c.r3.u32==v.identity&&
             c.r4.u32==0x000C0004&&PPC_LOAD_U32(s.monoTyped+0xB4)==c.r4.u32&&
             c.r5.u32==c.r1.u32+0x50&&!s.monoWorld,"Original mono world setter differs");
        const auto parameters=record.metadata->parameters(false);
        const auto p=std::find_if(parameters.begin(),parameters.end(),[](const auto& p){return p.handle==0x000C0004;});
        need(p!=parameters.end()&&p->name=="g_World"&&p->descriptorWords==std::array<uint32_t,2>{0x004007B0,0x00100005},
             "Mono world descriptor differs");
        std::array<uint32_t,16> input{};for(uint32_t i=0;i<16;++i)input[i]=PPC_LOAD_U32(c.r5.u32+4*i);
        need(!input[3]&&!input[7]&&!input[11]&&input[15]==0x3F800000,"Mono world homogeneous lanes differ");
        for(uint32_t i=0;i<4;++i)need(PPC_LOAD_U32(0x82000EC0+4*i)==UINT32_MAX,"Mono world transpose mask differs");
        const auto destination=s.edgeSlot(record,c.r4.u32);
        for(uint32_t r=0;r<4;++r)for(uint32_t col=0;col<4;++col)PPC_STORE_U32(destination+16*r+4*col,input[4*col+r]);
        s.monoWorld=true;return;
    }
    if(site==0x823C8EB0) {
        const bool skin=uint32_t(c.lr)==0x827003B4||uint32_t(c.lr)==0x82700454;
        auto& mesh=s.monoMesh;
        if(skin)need((uint32_t(c.lr)==0x82700454)==(mesh.bones>64)&&
                     (mesh.bones<=64||mesh.paletteEntry==c.r31.u32),"Original mono skin Boolean has another palette caller");
        need(c.lastFunction==site&&c.r30.u32==s.monoTyped&&c.r3.u32==v.wrapper&&c.r4.u32==0x00300014&&
             PPC_LOAD_U32(s.monoTyped+0xB8)==c.r4.u32&&s.monoWorld&&
             (skin?(mesh.bones&&mesh.phase==3&&mesh.boneUpload&&!mesh.skinBoolean&&c.r5.u32==1&&
                    c.r1.u32==mesh.frame&&PPC_LOAD_U32(mesh.frame)==s.monoFrame&&c.r28.u32==mesh.metadata):
                   (uint32_t(c.lr)==0x82740B78&&c.r1.u32==s.monoFrame&&c.r31.u32==s.monoPacket&&!c.r5.u32&&!s.monoBoolean)),
             "Original mono skinning Boolean differs");
        const auto parameters=record.metadata->parameters(false);
        const auto p=std::find_if(parameters.begin(),parameters.end(),[](const auto& p){return p.handle==0x00300014;});
        need(p!=parameters.end()&&p->name=="kIsSkinned"&&p->descriptorWords==std::array<uint32_t,2>{8,0x00010010},
             "Mono Boolean descriptor differs");
        PPC_STORE_U32(s.edgeSlot(record,c.r4.u32),skin?0x3F800000u:0u);
        s.monoBoolean=true;if(skin)mesh.skinBoolean=true;return;
    }
    const auto commit=[&](bool staged) {
        const auto values=parameterValues(v.identity);
        need(values.size()==1092&&(values[64]==0||values[64]==0x3F800000u)&&record.parameters,"Mono storage/Boolean differs");
        const auto body=record.metadata->body();const auto passes=record.metadata->techniques();
        const auto selected=std::find_if(passes.begin(),passes.end(),[&](const auto& pass){return pass.handle==s.monoTechnique;});
        need(selected!=passes.end(),"Selected mono constant pass is absent");const auto& pass=*selected;
        need(pass.contextOffset==(s.monoTechnique==0x0007FFFC?0x4BF0u:0x45E0u)&&word(body,0x130)==75&&word(body,0x134)==4&&
             word(body,0x138)==4368&&word(body,0x13C)==112,"Mono constant metadata differs");
        for(uint32_t space=0;space<2;++space) {
            const auto rows=word(body,pass.contextOffset+0x40+4*space),leaves=space?4u:75u;
            for(uint32_t leaf=0;leaf<leaves;++leaf) {
                uint32_t handle=0,mapping=0;
                if(space&&leaf==0){handle=0x00040001;mapping=0xC00;}
                else if(!space&&leaf==10)handle=0x00300014;
                else if(!space&&leaf>=11){handle=((leaf+3)<<18)|(leaf<<1);mapping=0x800+52+3*(leaf-11);}
                need(word(body,rows+16*leaf)==handle&&word(body,rows+16*leaf+4)==mapping&&
                     !word(body,rows+16*leaf+8)&&!word(body,rows+16*leaf+12),"Mono register mapping differs");
            }
        }
        Graphics::MonoConstants constants{};Graphics::MonoBooleans booleans{};
        booleans[0]=values[64]?1u:0u;
        const auto shared=sharedParameterStorage(v.identity,0x00040001);
        for(uint32_t row=0;row<4;++row)for(uint32_t lane=0;lane<4;++lane)
            constants[row][lane]=std::bit_cast<float>(staged?word(run.copies[0],16*row+4*lane):PPC_LOAD_U32(shared+16*row+4*lane));
        for(uint32_t bone=0;bone<64;++bone)for(uint32_t row=0;row<3;++row)for(uint32_t lane=0;lane<4;++lane)
            constants[52+3*bone+row][lane]=std::bit_cast<float>(values[68+16*bone+4*row+lane]);
        if(record.recordingContext) {
            need(s.monoRecordingPath&&s.monoRecording.active&&!booleans[0],"Mono recording commit lost its static lease");
            s.monoRecording.constants=constants;s.monoRecording.booleans=booleans;
        } else s.monoCommit=s.backend.commitMono(*s.monoVertex,*s.monoPixel,constants,booleans);
        static thread_local uint32_t logged{};if(logged++<16) {
            std::fprintf(stderr,"[NATIVE MONO COMMIT] id=%08X typed=%08X packet=%08X staged=%u phase=%u boolean=%08X c0..3=",
                v.identity,s.monoTyped,s.monoPacket,unsigned(staged),run.phase,booleans[0]);
            for(uint32_t row=0;row<4;++row)for(uint32_t lane=0;lane<4;++lane)
                std::fprintf(stderr,"%08X%s",std::bit_cast<uint32_t>(constants[row][lane]),lane==3?";":"/");
            std::fprintf(stderr,"\n");
        }
        record.privateModified.fill(0);std::memset(s.runtime.pointer(v.pool,128,true),0,128);
    };
    if(site==0x82C1DBA0) {
        if(s.monoRecordingPath&&s.monoRecording.materialActive) {
            auto& recording=s.monoRecording;const auto& mesh=s.monoMesh;
            need(c.lastFunction==site&&uint32_t(c.lr)==0x826B5764&&recording.filtered&&!recording.materialCommitted&&
                 c.r1.u32==mesh.frame-0xC0&&PPC_LOAD_U32(c.r1.u32)==mesh.frame&&c.r30.u32==s.monoTyped&&
                 c.r27.u32==recording.material&&c.r3.u32==v.identity,"Original deferred mono material commit frame differs");
            commit(false);recording.materialActive=false;recording.materialCommitted=true;c.r3.u64=0;return;
        }
        if(s.monoImmediatePath&&s.monoImmediate.materialActive) {
            auto& immediate=s.monoImmediate;const auto& mesh=s.monoMesh;
            need(c.lastFunction==site&&uint32_t(c.lr)==0x826B560C&&immediate.filtered&&!immediate.materialCommitted&&
                 c.r1.u32==mesh.frame-0xC0&&PPC_LOAD_U32(c.r1.u32)==mesh.frame&&
                 c.r30.u32==s.monoTyped&&c.r28.u32==immediate.material&&c.r3.u32==v.identity&&run.complete,
                 "Original immediate mono material commit lost its original frame");
            // Original8270A7A0 writes the callback's combined matrix directly
            // to82D6C0D0, and826B3270 uploads that bank before the submesh.
            // This qualified empty material has no matrix setter. Its original
            // 826B2F20->82C1DBA0 dirty-mask walk preserves those already uploaded
            // constants; the zero shared-pool default is not a later write.
            commit(true);immediate.materialActive=false;immediate.materialCommitted=true;c.r3.u64=0;return;
        }
        if(uint32_t(c.lr)==0x827003BC||uint32_t(c.lr)==0x8270045C) {
            auto& mesh=s.monoMesh;
            need((uint32_t(c.lr)==0x8270045C)==(mesh.bones>64)&&
                 (mesh.bones<=64||mesh.paletteEntry==c.r31.u32),"Original mono commit has another palette caller");
            need(c.lastFunction==site&&mesh.bones&&mesh.phase==3&&mesh.boneUpload&&mesh.skinBoolean&&!mesh.skinCommit&&
                 c.r1.u32==mesh.frame&&PPC_LOAD_U32(mesh.frame)==s.monoFrame&&c.r28.u32==mesh.metadata&&
                 c.r30.u32==s.monoTyped&&c.r3.u32==v.identity&&s.monoMaterial&&run.complete&&run.phase==4,
                 "Original skinned mono commit lost its palette or completed callbacks");
            commit(true);mesh.skinCommit=true;c.r3.u64=0;return;
        }
        need(c.lastFunction==site&&uint32_t(c.lr)==0x82740B80&&c.r1.u32==s.monoFrame&&c.r31.u32==s.monoPacket&&
             c.r3.u32==v.identity&&s.monoWorld&&s.monoBoolean&&!s.monoMaterial,"Original mono material commit differs");
        commit(false);s.monoMaterial=true;c.r3.u64=0;return;
    }
    if(site==0x826B5770) {
        need(c.lastFunction==site&&uint32_t(c.lr)==(s.monoImmediatePath?0x82740198u:0x82740B94u)&&c.r1.u32==s.monoFrame&&c.r31.u32==s.monoPacket&&
             c.r3.u32==s.monoTyped&&c.r4.u32==PPC_LOAD_U32(s.monoPacket)&&c.r5.u32==PPC_LOAD_U32(s.monoPacket+4)&&
             c.r6.u32==s.monoCamera&&s.monoWorld&&(s.monoImmediatePath||s.monoMaterial)&&!run.callbacks,"Original mono callback loop entry differs");
        if(s.monoImmediatePath) {
            need(!PPC_LOAD_U32(PPC_LOAD_U32(s.monoPacket)+0x24)&&!s.privateWord(v.identity,64),
                 "Immediate mono static producer has a skinned Boolean or palette");
            s.monoBoolean=true;
        }
        need(PPC_LOAD_U32(s.monoTyped+0x34)==1,"Mono classification count is unqualified");
        const auto rows=PPC_LOAD_U32(s.monoTyped+0x30),bindings=PPC_LOAD_U32(s.monoTyped+0x28);
        s.runtime.pointer(rows,28,false);s.runtime.pointer(bindings,24,false);
        constexpr std::array<uint32_t,7> expected{0,1,5,1,0,0,0};
        for(uint32_t i=0;i<7;++i)need(PPC_LOAD_U32(rows+4*i)==expected[i],"Mono classification row differs");
        need(PPC_LOAD_U32(bindings)==0x00040001&&PPC_LOAD_U32(0x82CF0190)==0x8270A7A0,
             "Mono combined matrix callback/handle differs");
        s.runtime.pointer(0x82D6C0D0,896,true);s.runtime.pointer(0x82D6C450,896,true);
        run.cpu=&c;run.entrySP=c.r1.u32;run.typed=s.monoTyped;run.packet=s.monoPacket;
        run.context=PPC_LOAD_U32(0x82D6D890);s.runtime.engineDriver->requireContext(run.context);
        run.callbacks=true;return;
    }
    if(s.monoRecordingPath&&(site==0x826B5618||site==0x826B2F20)) {
        auto& recording=s.monoRecording;auto& mesh=s.monoMesh;
        need(recording.active&&recording.cpu==&c&&mesh.phase==3&&mesh.cursor<mesh.draws.size()&&record.recordingContext,
             "Deferred mono material has no original recording submesh");
        const auto& row=mesh.draws[mesh.cursor];
        if(site==0x826B5618) {
            need(c.lastFunction==site&&uint32_t(c.lr)==0x82701528&&c.r1.u32==mesh.frame&&c.r17.u32==s.monoTyped&&
                 c.r31.u32==row.entry&&c.r3.u32==s.monoTyped&&c.r4.u32==row.material&&!recording.materialActive&&!recording.materialCommitted,
                 "Original deferred mono material entry differs");
            need(!((PPC_LOAD_U32(row.material+0xC)>>10)&31),"Deferred mono material parameter rows are not yet qualified");
            recording.material=row.material;recording.materialActive=true;recording.filtered=false;return;
        }
        need(c.lastFunction==site&&uint32_t(c.lr)==0x826B5764&&c.r1.u32==mesh.frame-0xC0&&
             PPC_LOAD_U32(c.r1.u32)==mesh.frame&&c.r30.u32==s.monoTyped&&c.r27.u32==recording.material&&
             c.r3.u32==v.identity&&recording.materialActive&&!recording.filtered,"Original deferred mono dirty filter differs");
        filterRigidDirty(record.privateModified,record.privateMask);
        auto* dirty=s.runtime.pointer(v.pool,128,true);for(uint32_t i=0;i<128;++i)dirty[i]&=PPC_LOAD_U8(v.pool+0x80+i);
        recording.filtered=true;return;
    }
    if(s.monoImmediatePath&&(site==0x826B54D0||site==0x826B2F20)) {
        auto& immediate=s.monoImmediate;auto& mesh=s.monoMesh;
        need(immediate.active&&immediate.cpu==&c&&mesh.phase==3&&mesh.cursor<mesh.draws.size(),
             "Immediate mono material has no active original submesh");
        const auto& submesh=mesh.draws[mesh.cursor];
        if(site==0x826B54D0) {
            need(c.lastFunction==site&&uint32_t(c.lr)==0x8270131C&&c.r1.u32==mesh.frame&&
                 immediate.submeshUpdated&&!immediate.materialActive&&c.r3.u32==s.monoTyped&&c.r4.u32==submesh.material,
                 "Immediate mono material entry differs");
            // Mono has no material sampler/vector leaves in this qualified
            // static path. The original empty collection still runs its full
            // material prologue, dirty filter, commit and epilogue.
            need(!((PPC_LOAD_U32(submesh.material+0xC)>>10)&31),"Immediate mono material parameter rows are not yet qualified");
            immediate.material=submesh.material;immediate.materialActive=true;immediate.filtered=false;immediate.materialCommitted=false;return;
        }
        need(c.lastFunction==site&&uint32_t(c.lr)==0x826B560C&&c.r1.u32==mesh.frame-0xC0&&
             PPC_LOAD_U32(c.r1.u32)==mesh.frame&&c.r30.u32==s.monoTyped&&c.r28.u32==immediate.material&&
             c.r3.u32==v.identity&&immediate.materialActive&&!immediate.filtered,
             "Immediate mono dirty filter differs");
        filterRigidDirty(record.privateModified,record.privateMask);
        auto* dirty=s.runtime.pointer(v.pool,128,true);
        for(uint32_t i=0;i<128;++i)dirty[i]&=PPC_LOAD_U8(v.pool+0x80+i);
        immediate.filtered=true;return;
    }
    need(run.cpu==&c&&run.callbacks&&(s.monoImmediatePath||s.monoMaterial),"Mono upload has no callback owner");
    if(site==0x826B3270) {
        need(c.lastFunction==site&&uint32_t(c.lr)==0x826B589C&&run.callbacks&&!run.upload&&
             c.r1.u32==run.entrySP-0xD0&&PPC_LOAD_U32(c.r1.u32)==run.entrySP&&
             PPC_LOAD_U32(c.r1.u32+0xC8)==(s.monoImmediatePath?0x82740198u:0x82740B94u)&&c.r30.u32==run.typed&&
             !c.r3.u32&&c.r4.u32==56&&!c.r5.u32&&c.r6.u32==56,
             "Original mono replay must upload both complete 56-register banks");
        // Actual owned CPU upload destinations. Original memcpy and VMX copy
        // execute into both banks, with neither an FX-shaped object nor an SDK
        // command packet. They are checked before any native replay publication.
        // TEMPORARY buffers are owner-scoped pooled (224 words each): fresh
        // poison on every lease, exact size bound, exclusive until run closure.
        run.buffers=s.acquireTempUpload({224,224,224,224});
        run.uploadSP=c.r1.u32-0x90;run.upload=true;return;
    }
    need(run.upload&&!run.complete&&c.r1.u32==run.uploadSP&&PPC_LOAD_U32(run.uploadSP)==run.entrySP-0xD0&&
         PPC_LOAD_U32(run.uploadSP+0x88)==0x826B589C,"Original mono stage-upload frame differs");
    if(site==0x826B32E4||site==0x826B3330) {
        const uint32_t stage=site==0x826B32E4?0:1;
        need(run.phase==2*stage&&c.r3.u32==run.context&&c.r4.u32==stage&&!c.r5.u32&&c.r6.u32==0x82D6C7D0&&
             c.r7.u32==0x82D6C7E0&&c.r8.u32==56,"Original mono stage-upload SDK arguments/order differ");
        const auto source=stage?0x82D6C450u:0x82D6C0D0u;
        std::memcpy(run.copies[stage].data(),s.runtime.pointer(source,896,false),896);
        PPC_STORE_U32(c.r6.u32,run.buffers[2*stage]->address);PPC_STORE_U32(c.r7.u32,run.buffers[2*stage+1]->address);
        ++run.phase;c.r3.u64=0;c.lr=site+4;return;
    }
    need(site==0x826B3314||site==0x826B3364,"Unknown mono replay staging boundary");
    const uint32_t stage=site==0x826B3314?0:1;
    need(run.phase==2*stage+1&&PPC_LOAD_U32(0x82D6C7D0)==run.buffers[2*stage]->address&&
         PPC_LOAD_U32(0x82D6C7E0)==run.buffers[2*stage+1]->address,"Original mono upload destination publication changed");
    const auto source=stage?0x82D6C450u:0x82D6C0D0u;
    need(!std::memcmp(s.runtime.pointer(source,896,false),run.copies[stage].data(),896),"Mono staging changed during original upload");
    for(uint32_t i=0;i<2;++i)need(!std::memcmp(s.runtime.pointer(run.buffers[2*stage+i]->address,896,false),run.copies[stage].data(),896),
        "Original mono stage upload did not copy all 56 registers");
    ++run.phase;if(stage==1){run.complete=true;commit(true);if(s.monoImmediatePath)s.monoMaterial=true;}
    // Observation only: original context getters and both epilogues still run.
}
void EngineEffects::beginMono(PPCContext& c,uint8_t* base) {
    observeProducerEntry(c,base,"mono_begin");
    auto& s=*state;s.require(base);const auto t=c.r30.u32,w=c.r4.u32,m=c.r3.u32;
    const bool alpha=(c.r31.u32&255)!=0;const auto technique=alpha?0x0007FFFCu:0x0003FFFCu;
    need(currentContext==&c&&c.lastFunction==0x826B5FC0&&uint32_t(c.lr)==0x8273A93C&&c.r5.u32==technique&&
         c.r1.u32>=0x200&&!(c.r1.u32&15)&&PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0xB0,
         "Unqualified original mono activation");
    s.runtime.pointer(t,0xC0,false);s.runtime.pointer(w,0x30,false);s.runtime.pointer(m,0x18,false);s.runtime.pointer(s.monoPacket,0x24,false);
    need(s.monoPacket&&s.monoFrame==PPC_LOAD_U32(c.r1.u32)&&s.monoTyped==t&&
         (s.monoImmediatePath?c.r31.u32==uint32_t(s.monoImmediate.alpha):!c.r31.u32),"Mono activation lost its original world callback");
    auto& record=s.find(PPC_LOAD_U32(w+0x10));const auto& v=record.view;
    need(v.source==0x8211F480&&v.phase==Phase::Reflected&&record.typedReflections==1&&
         PPC_LOAD_U32(t)==0x8215020C&&PPC_LOAD_U32(t+0x10)==m&&PPC_LOAD_U32(t+0x18)==w&&PPC_LOAD_U32(t+0x1C)==v.identity&&
         PPC_LOAD_U32(t+(alpha?0xA8u:0xACu))==c.r5.u32&&v.wrapper==w&&v.manager==m&&m==PPC_LOAD_U32(0x82D08BFC)&&
         PPC_LOAD_U32(w)==0x820B7140&&PPC_LOAD_U32(w+0xC)==m&&PPC_LOAD_U32(w+0x14)==w&&PPC_LOAD_U32(w+0x18)==v.identity&&
         PPC_LOAD_U32(w+0x1C)==v.cache&&PPC_LOAD_U32(w+0x20)==v.cacheBytes&&PPC_LOAD_U32(w+0x24)==v.cache,
         "Original mono reflected owner differs");
    const auto meta=PPC_LOAD_U32(s.monoPacket);s.runtime.pointer(meta,0x2C,false);
    need(PPC_LOAD_U32(meta+0x24)<=255,"Mono geometry exceeds bounded composed palette storage");
    const auto camera=s.runtime.engineDriver->cameraBinding();
    need(camera.camera==PPC_LOAD_U32(s.monoPacket+8)&&camera.camera==PPC_LOAD_U32(0x82E3DD60)&&
         camera.viewport==std::array<uint32_t,6>{0,0,1280,720,0x3F800000,0},"Original mono camera differs");
    s.validatePool(v.pool);
    const auto passes=record.metadata->techniques();need(passes.size()==2&&PPC_LOAD_U32(w+0x28)==2,"Original mono technique count differs");
    const auto& pass=passes[alpha?1u:0u];
    if(s.runtime.resourceAudit.active())try {
        char asset[32],parameters[240],instance[192];
        std::snprintf(asset,sizeof(asset),"source:%08X",v.source);
        std::snprintf(parameters,sizeof(parameters),"source=%08X vs=%08X ps=%08X technique=%08X pass=%08X context=%08X flag=%08X publicImmediate=%u",
            v.source,pass.vertexShaderAddress,pass.pixelShaderAddress,pass.handle,pass.passHandle,pass.contextOffset,c.r31.u32,unsigned(s.monoImmediatePath));
        std::snprintf(instance,sizeof(instance),"effect=%08X typed=%08X wrapper=%08X manager=%08X packet=%08X cache=%08X frame=%08X",
            v.identity,t,w,m,s.monoPacket,v.cache,c.r1.u32);
        s.runtime.resourceAudit.observe("effect_pass",asset,uint32_t(c.lr),parameters,"reflected-selection-pending",s.runtime.nativeDepthCopyCount.load(),instance);
    }catch(...){std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] mono pass capture failed\n");}
    need(pass.name==(alpha?"TechniqueAlpha":"TechniqueOpaque")&&pass.handle==technique&&pass.passHandle==technique+2&&
         pass.vertexShaderAddress==(alpha?0x82121BE8u:0x82120C04u)&&pass.pixelShaderAddress==(alpha?0x82122D38u:0x82122BD4u)&&pass.scalars.size()==1&&pass.samplers.empty()&&
         pass.scalars[0].sdkId==0x38&&pass.scalars[0].value==2,"Original mono pass metadata differs");
    s.runtime.pointer(v.cache,v.cacheBytes,false);const auto selectedCache=v.cache+(alpha?24u:0u),rows=v.cache+48+(alpha?12u:0u);
    const std::array<uint32_t,6> header{pass.handle,0,rows,rows+12,1,0};
    for(uint32_t i=0;i<header.size();++i)need(PPC_LOAD_U32(selectedCache+4*i)==header[i],"Original mono cache header differs");
    need(PPC_LOAD_U32(rows)==PPC_LOAD_U32(0x82E06F80+0x38)&&PPC_LOAD_U32(rows+4)==2,"Original mono cull cache differs");
    need(!s.activeEdgeId&&!s.activeShadowId,"Another native effect remains active before mono");
    if(s.monoId){
        s.activeMono(c,base);need(s.monoId==v.identity,"Another mono effect selected");
        if(s.monoTechnique==technique)return;
        const auto packet=s.monoPacket,frame=s.monoFrame;const auto immediate=s.monoImmediate;const bool publicPath=s.monoImmediatePath;
        EngineCpuCalls cpu(c,base);cpu.registers().lr=0x826B5FF8;cpu.invoke(0x826B4628,m);
        s.monoPacket=packet;s.monoFrame=frame;s.monoTyped=t;s.monoImmediate=immediate;s.monoImmediatePath=publicPath;
    }
    need(!PPC_LOAD_U32(w+0x2C),"Mono cache is already selected");
    const Graphics::CompiledMaterial* vertex=nullptr;const Graphics::CompiledMaterial* pixel=nullptr;const auto shaders=record.metadata->shaders();
    for(size_t i=0;i<shaders.size();++i){
        if(shaders[i].originalAddress==pass.vertexShaderAddress)vertex=&record.shaderRecords.prepareForBind(record.shaders[i],s.compiler);
        if(shaders[i].originalAddress==pass.pixelShaderAddress)pixel=&record.shaderRecords.prepareForBind(record.shaders[i],s.compiler);
    }
    need(vertex&&pixel,"Mono shader pair is missing");s.backend.validateMonoShaders(*vertex,*pixel);
    need(word(record.metadata->body(),0x120)==2&&word(record.metadata->body(),0x124)==1,
         "Original mono dirty extent differs");
    if(s.zprepassId||s.rigidId||s.skinId){EngineCpuCalls cpu(c,base);cpu.registers().lr=0x826B5FF8;cpu.invoke(0x826B4628,m);}
    need(!PPC_LOAD_U32(m+4)&&!PPC_LOAD_U32(m+0xC),"Another original effect remains selected before mono");
    PPC_STORE_U32(m+4,w);PPC_STORE_U32(m+8,v.identity);PPC_STORE_U32(m+0xC,pass.handle);
    {EngineCpuCalls cpu(c,base);cpu.registers().lr=0x826B604C;cpu.invoke(0x826B35D8,w+0x14,pass.handle,0);}
    need(PPC_LOAD_U32(w+0x2C)==selectedCache,"Original mono cache did not select the original pass");
    s.runtime.engineDriver->directScalar(base,0x38,2);
    record.privateModified.fill(0);std::fill_n(record.privateModified.begin(),16,uint8_t(0xFF));
    if(!PPC_LOAD_U32(0x82D00F80)){auto* dirty=s.runtime.pointer(v.pool,128,true);std::memset(dirty,0,128);std::memset(dirty,0xFF,16);}
    s.backend.bindMonoShaders(*vertex,*pixel);s.screenReplacement={};s.monoPixel=pixel;s.monoId=v.identity;s.monoTyped=t;s.monoCamera=camera.camera;s.monoVertex=vertex;s.monoTechnique=technique;
    static thread_local uint32_t monoBeginSample{};
    if(sampleHotLog(monoBeginSample))
        std::fprintf(stderr,"[NATIVE MONO BEGIN] id=%08X typed=%08X camera=%08X; original cull cache and native VS%08X/PS%08X bound\n",
            v.identity,t,camera.camera,pass.vertexShaderAddress,pass.pixelShaderAddress);
}
void EngineEffects::beginZPrepass(PPCContext& c,uint8_t* base) {
    observeProducerEntry(c,base,"zprepass_begin");
    auto& s=*state;s.require(base);const auto t=PPC_LOAD_U32(0x82D6D8A0),w=c.r4.u32,m=c.r3.u32;
    need(currentContext==&c&&c.lastFunction==0x826B5FC0&&uint32_t(c.lr)==0x827406C8&&c.r5.u32==0x0003FFFC&&
         c.r1.u32>=0x200&&!(c.r1.u32&15)&&PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0xF0,
         "Unqualified original zprepass activation");
    s.runtime.pointer(t,0xC8,false);s.runtime.pointer(w,0x30,false);s.runtime.pointer(m,0x18,false);s.runtime.pointer(c.r31.u32,0x24,false);
    auto& record=s.find(PPC_LOAD_U32(w+0x10));const auto& v=record.view;
    need(v.source==0x821490E0&&v.phase==Phase::Reflected&&record.typedReflections==1&&
         PPC_LOAD_U32(t)==0x8215022C&&PPC_LOAD_U32(t+0x10)==m&&PPC_LOAD_U32(t+0x18)==w&&PPC_LOAD_U32(t+0x1C)==v.identity&&
         PPC_LOAD_U32(t+0xAC)==c.r5.u32&&v.wrapper==w&&v.manager==m&&m==PPC_LOAD_U32(0x82D08BFC)&&
         PPC_LOAD_U32(w)==0x820B7140&&PPC_LOAD_U32(w+0xC)==m&&PPC_LOAD_U32(w+0x14)==w&&PPC_LOAD_U32(w+0x18)==v.identity&&
         PPC_LOAD_U32(w+0x1C)==v.cache&&PPC_LOAD_U32(w+0x20)==v.cacheBytes&&PPC_LOAD_U32(w+0x24)==v.cache,
         "Original zprepass reflected owner differs");
    const auto meta=PPC_LOAD_U32(c.r31.u32);s.runtime.pointer(meta,0x2C,false);
    need(!PPC_LOAD_U32(meta+0x24),"Skinned zprepass geometry is not yet qualified");
    const auto camera=s.runtime.engineDriver->cameraBinding();
    need(camera.camera==PPC_LOAD_U32(c.r31.u32+8)&&camera.camera==PPC_LOAD_U32(0x82E3DD60)&&
         camera.viewport==std::array<uint32_t,6>{0,0,1280,720,0x3F800000,0},"Original zprepass camera differs");
    s.validatePool(v.pool);
    const auto passes=record.metadata->techniques();need(passes.size()==2&&PPC_LOAD_U32(w+0x28)==2,"Original zprepass technique count differs");
    const auto& pass=passes.front();
    need(pass.name=="TechniqueOpaque"&&pass.handle==0x0003FFFC&&pass.passHandle==0x0003FFFE&&
         pass.vertexShaderAddress==0x8214A8A4&&!pass.pixelShaderAddress&&pass.scalars.size()==1&&pass.samplers.empty()&&
         pass.scalars[0].sdkId==0x38&&pass.scalars[0].value==2,"Original zprepass pass metadata differs");
    s.runtime.pointer(v.cache,v.cacheBytes,false);const auto rows=v.cache+48;
    const std::array<uint32_t,6> header{pass.handle,0,rows,rows+12,1,0};
    for(uint32_t i=0;i<header.size();++i)need(PPC_LOAD_U32(v.cache+4*i)==header[i],"Original zprepass cache header differs");
    need(PPC_LOAD_U32(rows)==PPC_LOAD_U32(0x82E06F80+0x38)&&PPC_LOAD_U32(rows+4)==2,"Original zprepass cull cache differs");
    need(!s.activeEdgeId&&!s.activeShadowId,"Another native effect remains active before zprepass");
    if(s.zprepassId){s.activeZPrepass(c,base);need(s.zprepassId==v.identity,"Another zprepass effect selected");return;}
    need(!PPC_LOAD_U32(m+4)&&!PPC_LOAD_U32(m+0xC)&&!PPC_LOAD_U32(w+0x2C),"Another original effect remains selected before zprepass");
    const Graphics::CompiledMaterial* vertex=nullptr;const auto shaders=record.metadata->shaders();
    for(size_t i=0;i<shaders.size();++i)if(shaders[i].originalAddress==pass.vertexShaderAddress)
        vertex=&record.shaderRecords.prepareForBind(record.shaders[i],s.compiler);
    need(vertex,"Zprepass vertex shader is missing");s.backend.validateZPrepassShader(*vertex);
    need(word(record.metadata->body(),0x120)==2&&word(record.metadata->body(),0x124)==1,
         "Original zprepass dirty extent differs");
    PPC_STORE_U32(m+4,w);PPC_STORE_U32(m+8,v.identity);PPC_STORE_U32(m+0xC,pass.handle);
    {EngineCpuCalls cpu(c,base);cpu.registers().lr=0x826B604C;cpu.invoke(0x826B35D8,w+0x14,pass.handle,0);}
    need(PPC_LOAD_U32(w+0x2C)==v.cache,"Original zprepass cache did not select opaque pass");
    s.runtime.engineDriver->directScalar(base,0x38,2);
    record.privateModified.fill(0);std::fill_n(record.privateModified.begin(),16,uint8_t(0xFF));
    if(!PPC_LOAD_U32(0x82D00F80)){auto* dirty=s.runtime.pointer(v.pool,128,true);std::memset(dirty,0,128);std::memset(dirty,0xFF,16);}
    s.backend.bindZPrepassShader(*vertex);s.screenReplacement={};s.zprepassId=v.identity;s.zprepassTyped=t;s.zprepassCamera=camera.camera;s.zprepassVertex=vertex;
    {static thread_local uint32_t zprepassBeginSample{};
    if(sampleHotLog(zprepassBeginSample))
        std::fprintf(stderr,"[NATIVE ZPREPASS BEGIN] id=%08X typed=%08X camera=%08X; original cull cache and native VS8214A8A4/null PS bound\n",v.identity,t,camera.camera);}
}
void EngineEffects::commitZPrepass(PPCContext& c,uint8_t* base) {
    auto& s=*state;auto& record=s.activeZPrepass(c,base);const auto& v=record.view;
    need(c.lastFunction==0x826B3980&&uint32_t(c.lr)==0x82740820&&c.r3.u32==v.wrapper&&c.r30.u32==s.zprepassTyped&&
         c.r1.u32>=0x200&&!(c.r1.u32&15)&&PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0xF0&&record.parameters,
         "Original static zprepass commit frame differs");
    const auto values=parameterValues(v.identity);need(values.size()==1100&&!values[64],"Static zprepass has a skinned Boolean or different storage");
    const auto body=record.metadata->body();const auto& pass=record.metadata->techniques().front();
    need(pass.contextOffset==0x45C0&&word(body,0x130)==77&&word(body,0x134)==4,
         "Original zprepass commit context differs");
    for(uint32_t space=0;space<2;++space) {
        const auto rows=word(body,pass.contextOffset+0x40+4*space),leaves=space?4u:77u;
        for(uint32_t leaf=0;leaf<leaves;++leaf) {
            uint32_t handle=0,mapping=0;
            if(space&&leaf==0){handle=0x00040001;mapping=0xC00;}
            else if(!space&&leaf==10)handle=0x00300014;
            else if(!space&&(leaf==11||leaf==12)){handle=((leaf+3)<<18)|(leaf<<1);mapping=36+leaf-11;}
            else if(!space&&leaf>=13){handle=((leaf+4)<<18)|(leaf<<1);mapping=0x800+52+3*(leaf-13);}
            need(word(body,rows+16*leaf)==handle&&word(body,rows+16*leaf+4)==mapping&&
                 !word(body,rows+16*leaf+8)&&!word(body,rows+16*leaf+12),"Original zprepass register mapping differs");
        }
    }
    Graphics::ZPrepassConstants constants{};Graphics::ZPrepassBooleans booleans{};
    const auto shared=sharedParameterStorage(v.identity,0x00040001);
    for(uint32_t row=0;row<4;++row)for(uint32_t lane=0;lane<4;++lane)constants[row][lane]=std::bit_cast<float>(PPC_LOAD_U32(shared+16*row+4*lane));
    for(uint32_t row=0;row<2;++row)for(uint32_t lane=0;lane<4;++lane)constants[36+row][lane]=std::bit_cast<float>(values[68+4*row+lane]);
    for(uint32_t bone=0;bone<64;++bone)for(uint32_t row=0;row<3;++row)for(uint32_t lane=0;lane<4;++lane)
        constants[52+3*bone+row][lane]=std::bit_cast<float>(values[76+16*bone+4*row+lane]);
    s.zprepassCommit=s.backend.commitZPrepass(*s.zprepassVertex,constants,booleans);
    record.privateModified.fill(0);std::memset(s.runtime.pointer(v.pool,128,true),0,128);
}
void EngineEffects::monoImmediateOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.require(base);auto& run=s.monoImmediate;
    need(currentContext==&c,"Immediate mono has no original CPU frame");
    if(site==0x827400F8) {
        need(c.lastFunction==site&&uint32_t(c.lr)==0x82740B28&&c.r4.u32<=1&&c.r5.u32<=1&&c.r6.u32<=1&&
             !run.active&&c.r1.u32>=0x400&&!(c.r1.u32&15)&&PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0xF0&&
             !PPC_LOAD_U32(0x82D6CCA8),"Unqualified original immediate mono entry");
        const auto packet=c.r3.u32;s.runtime.pointer(packet,0x24,false);
        const auto typed=PPC_LOAD_U32(packet+0x18),metadata=PPC_LOAD_U32(packet);
        s.runtime.pointer(typed,0xC0,false);s.runtime.pointer(metadata,0x38,false);
        auto& record=s.find(PPC_LOAD_U32(typed+0x1C));const auto& v=record.view;
        const auto context=PPC_LOAD_U32(0x82D6D890);s.runtime.engineDriver->requireContext(context);
        need(PPC_LOAD_U32(typed)==0x8215020C&&v.source==0x8211F480&&v.phase==Phase::Reflected&&
             record.typedReflections==1&&PPC_LOAD_U32(typed+0x18)==v.wrapper&&PPC_LOAD_U32(typed+0x10)==v.manager&&
             v.manager==PPC_LOAD_U32(0x82D08BFC)&&record.contextIdentity==context&&!record.recordingContext&&
             (!PPC_LOAD_U32(packet+0x14)||PPC_LOAD_U32(packet+0x14)==context)&&
             PPC_LOAD_U32(typed+0xA8)==0x0007FFFC&&PPC_LOAD_U32(typed+0xAC)==0x0003FFFC&&
             !PPC_LOAD_U32(metadata+0x24),"Original immediate mono owner or static palette differs");
        need(!s.rigidImmediate.active&&!s.skinImmediate.active,"Immediate mono overlaps another packet lifetime");
        run={};run.cpu=&c;run.entrySP=c.r1.u32;run.frame=c.r1.u32-0x80;run.packet=packet;run.typed=typed;
        run.context=context;run.alpha=c.r4.u32!=0;run.active=true;run.drawsBefore=s.backend.monoMeshDrawCount();s.monoTextures={};return;
    }
    s.activeMono(c,base);auto& mesh=s.monoMesh;
    need(run.active&&run.cpu==&c&&s.monoImmediatePath&&s.monoPacket==run.packet&&s.monoTyped==run.typed,
         "Immediate mono continuation lost its packet owner");
    if(site==0x82700498) {
        need(c.lastFunction==site&&uint32_t(c.lr)==0x82701310&&c.r1.u32==mesh.frame&&mesh.phase==3&&
             mesh.cursor<mesh.draws.size()&&!run.submeshActive&&!run.submeshUpdated&&c.r3.u32==run.typed&&
             c.r4.u32==mesh.metadata&&c.r5.u32==mesh.object,"Immediate mono submesh callback entry differs");
        const auto& row=mesh.draws[mesh.cursor];
        need(c.r6.u32==row.words[1]&&c.r7.u32==row.words[0],"Immediate mono material selector differs");
        const auto bits=PPC_LOAD_U32(row.material+0xC);
        need(!((bits>>20)&7),"Immediate mono submesh callback rows are not yet qualified");
        run.submeshActive=true;return;
    }
    if(site==0x827005DC) {
        need(run.submeshActive&&mesh.cursor<mesh.draws.size()&&c.r1.u32==mesh.frame-0xE0&&
             PPC_LOAD_U32(c.r1.u32)==mesh.frame&&PPC_LOAD_U32(c.r1.u32+0xD8)==0x82701310&&
             c.r28.u32==run.typed&&c.r25.u32==mesh.draws[mesh.cursor].material,
             "Immediate mono submesh callback return differs");
        run.submeshActive=false;run.submeshUpdated=true;return;
    }
    need(site==0x827402E4&&c.r1.u32==run.frame&&PPC_LOAD_U32(run.frame)==run.entrySP&&
         PPC_LOAD_U32(run.frame+0x78)==0x82740B28&&c.r31.u32==run.packet&&c.r30.u32==uint32_t(run.alpha)&&
         mesh.phase==4&&mesh.cursor==mesh.draws.size()&&!run.submeshActive&&!run.materialActive&&
         s.monoReplay.complete&&s.monoReplay.phase==4&&s.monoTextures.complete&&!s.monoTextures.active&&
         s.backend.monoMeshDrawCount()==run.drawsBefore+mesh.cursor,
         "Immediate mono ended before its original uploads, draws and stream cleanup");
    run.active=false;
}
void EngineEffects::monoImmediateTextureOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;auto& record=s.activeMono(c,base);auto& run=s.monoTextures;const auto& immediate=s.monoImmediate;
    const auto passes=record.metadata->techniques();const auto selected=std::find_if(passes.begin(),passes.end(),
        [&](const auto& pass){return pass.handle==s.monoTechnique;});need(selected!=passes.end(),"Immediate mono texture pass is absent");
    const auto& pass=*selected;
    if(site==0x8273FF80) {
        need(c.lastFunction==site&&uint32_t(c.lr)==0x827401C8&&immediate.active&&immediate.cpu==&c&&
             c.r1.u32==immediate.frame&&c.r3.u32==immediate.packet&&c.r4.u32==s.monoId&&
             c.r5.u32==s.monoTechnique&&!c.r6.u32&&!run.active&&!run.complete&&s.monoReplay.complete,
             "Unqualified immediate mono shared texture walk");
        run.cpu=&c;run.sp=c.r1.u32;run.context=immediate.context;run.shadowOwner=PPC_LOAD_U32(immediate.packet+0x20);return;
    }
    need(currentContext==&c&&run.cpu==&c&&c.r1.u32==run.sp-0xC0&&PPC_LOAD_U32(c.r1.u32)==run.sp&&
         PPC_LOAD_U32(run.sp-8)==0x827401C8&&c.r29.u32==s.monoId,"Immediate mono shared texture frame differs");
    if(site==0x8273FFA4) {
        need(c.r3.u32==immediate.packet&&c.r4.u32==s.monoId&&c.r5.u32==s.monoTechnique&&!run.active&&!run.complete,
             "Immediate mono texture lookup ABI differs");
        constexpr uint32_t handles[]={0x001C000D,0x0020000F,0x00240011};
        constexpr uint32_t handleFields[]={0x698,0x694,0x6A0},textureFields[]={0xF0,0xF4,0xFC};
        s.runtime.pointer(run.shadowOwner,0x6A4,false);const auto shadowId=PPC_LOAD_U32(run.shadowOwner+0x1C);
        const auto& shadow=s.find(shadowId);
        need(PPC_LOAD_U32(run.shadowOwner)==0x8214E518&&shadow.view.source==0x820C0550&&
             shadow.view.manager==record.view.manager&&shadow.view.pool==record.view.pool,
             "Immediate mono lost the original shared shadows owner");
        for(uint32_t i=0;i<3;++i) {
            need(PPC_LOAD_U32(run.shadowOwner+handleFields[i])==handles[i],"Immediate mono shadow handle association differs");
            // Original826F3258 is a usage query, not a local descriptor
            // lookup. These real shadow-owner handles refer to shared pool
            // leaves absent from mono's four local shared descriptors. Its
            // selected-pass bitmap still owns their zero bits. Preserve the
            // exact namespace/leaf arithmetic and bound every bitmap read;
            // general reflection descriptor admission stays unchanged.
            need(!s.monoSharedUsage(record,pass,handles[i]),"Mono has an unexpected shared shadow texture usage");
            const auto texture=PPC_LOAD_U32(run.shadowOwner+textureFields[i]);
            const auto view=s.runtime.engineDriver->shadowTextures().view(texture);
            need(view.owner==run.shadowOwner&&view.field==textureFields[i],"Immediate mono shadow texture has another live owner");
            // No sampling occurs for these zero-usage rows. Their actual live
            // texture ownership is required; an upload is not a prerequisite.
            run.rows[i]={handles[i],0,i,texture};
        }
        need(pass.passHandle==s.monoTechnique+2,"Immediate mono pass encoding differs");
        c.r10.s64=int64_t(20*(pass.passHandle>>18));c.r9.u64=0;c.r7.u64=0x66666667;c.r28.u64=0;c.r23.u64=0x82D10000;
        run.active=true;run.phase=1;run.cursor=0;return;
    }
    if(site==0x827400F0) {
        need(run.active&&run.phase==1&&run.cursor==3&&c.r28.u32==12,"Immediate mono shared texture walk is incomplete");
        run.active=false;run.complete=true;return;
    }
    need(site==0x82740044&&run.active&&run.phase==1&&run.cursor<3&&c.r28.u32==4*run.cursor&&
         c.r3.u32==s.monoId&&c.r4.u32==pass.passHandle&&c.r27.u32==pass.passHandle&&
         c.r5.u32==run.rows[run.cursor].handle&&c.r31.u32==c.r5.u32,
         "Immediate mono reached an unexpected sampler-map or texture-bind boundary");
    for(uint32_t i=0;i<3;++i)need(PPC_LOAD_U32(c.r1.u32+0x50+4*i)==run.rows[i].handle&&
        PPC_LOAD_U32(c.r1.u32+0x60+4*i)==run.rows[i].texture,"Immediate mono shared texture snapshot changed");
    c.r3.u64=0;c.lr=site+4;++run.cursor;
}
void EngineEffects::monoImmediateMeshOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.activeMono(c,base);auto& mesh=s.monoMesh;auto& run=s.monoImmediate;
    need(run.active&&run.cpu==&c&&s.monoImmediatePath&&s.monoTextures.complete&&!s.monoTextures.active&&
         s.monoReplay.complete&&s.monoReplay.phase==4,"Immediate mono mesh has no completed original uploads");
    if(site==0x82701220) {
        need(c.lastFunction==site&&uint32_t(c.lr)==0x827402B0&&c.r1.u32==run.frame&&
             c.r31.u32==run.packet&&c.r5.u32==s.monoTyped&&!c.r6.u32&&
             c.r3.u32==PPC_LOAD_U32(run.packet)&&c.r4.u32==PPC_LOAD_U32(run.packet+4)&&
             c.r7.u32==PPC_LOAD_U32(c.r3.u32+0x10)&&c.r8.u32<=1,
             "Immediate mono original static mesh producer differs");
        const auto meta=c.r3.u32,object=c.r4.u32,geometry=PPC_LOAD_U32(meta+0xC);
        s.runtime.pointer(meta,0x38,false);s.runtime.pointer(object,0x40,false);s.runtime.pointer(geometry,0x78,false);
        need(!PPC_LOAD_U32(meta+0x24)&&!s.privateWord(s.monoId,64),"Immediate mono static mesh has a skin palette");
        const auto objectGeometry=PPC_LOAD_U32(object+0x18),dynamicOffset=PPC_LOAD_U32(0x82CF05EC),morphOffset=PPC_LOAD_U32(0x82D6CAB0);
        need(uint64_t(object)+dynamicOffset+4<=UINT32_MAX&&uint64_t(objectGeometry)+morphOffset+4<=UINT32_MAX,
             "Immediate mono geometry extension overflows");
        s.runtime.pointer(object+dynamicOffset,4,false);s.runtime.pointer(objectGeometry,0x28,false);s.runtime.pointer(objectGeometry+morphOffset,4,false);
        need(!(PPC_LOAD_U32(object+dynamicOffset)&&(PPC_LOAD_U32(objectGeometry+8)&0x04000000))&&
             !PPC_LOAD_U32(objectGeometry+morphOffset)&&!PPC_LOAD_U32(geometry+0x34),
             "Immediate mono dynamic, morph or external stream is not yet qualified");
        mesh={};mesh.metadata=meta;mesh.object=object;mesh.geometry=geometry;mesh.frame=c.r1.u32-0xF0;
        const auto count=PPC_LOAD_U32(meta+0x10),entries=PPC_LOAD_U32(meta+0x14),collection=PPC_LOAD_U32(meta+0x34);
        need(count<=UINT32_MAX/36,"Immediate mono submesh snapshot extent overflows");s.runtime.pointer(entries,36*count,false);
        s.runtime.pointer(collection,12,false);const auto materials=PPC_LOAD_U32(collection);
        need(materials<=(UINT32_MAX-12)/4,"Immediate mono material collection extent overflows");s.runtime.pointer(collection,12+4*materials,false);
        const auto offsets=PPC_LOAD_U32(objectGeometry+0x24),headers=PPC_LOAD_U32(0x82D6D814);
        for(uint32_t i=0;i<count;++i) {
            State::RigidMeshRun::Submesh row;row.entry=entries+36*i;
            for(uint32_t lane=0;lane<9;++lane)row.words[lane]=PPC_LOAD_U32(row.entry+4*lane);
            need(uint64_t(offsets)+4ull*row.words[0]+4<=UINT32_MAX,"Immediate mono material offset overflows");
            s.runtime.pointer(offsets+4*row.words[0],4,false);row.materialOffset=PPC_LOAD_U32(offsets+4*row.words[0]);
            need(uint64_t(headers)+row.materialOffset+4<=UINT32_MAX,"Immediate mono material header overflows");
            row.materialHeader=headers+row.materialOffset;s.runtime.pointer(row.materialHeader,4,false);row.flags=PPC_LOAD_U16(row.materialHeader);
            if(row.flags&0x20)continue;
            need(row.words[1]<materials,"Immediate mono material selector is out of range");
            row.material=PPC_LOAD_U32(collection+12+4*row.words[1]);s.runtime.pointer(row.material,0x18,false);mesh.draws.push_back(row);
        }
        return;
    }
    need(c.r1.u32==mesh.frame&&PPC_LOAD_U32(mesh.frame)==run.frame&&c.r19.u32==s.monoTyped&&
         c.r22.u32==mesh.object&&c.r25.u32==mesh.metadata,"Immediate mono submesh frame differs");
    if(site==0x827013B0) {
        need(mesh.cursor<mesh.draws.size()&&c.r31.u32==mesh.draws[mesh.cursor].entry&&run.submeshUpdated&&
             run.materialCommitted&&!run.materialActive,"Immediate mono draw has no original completed material");
        const auto& row=mesh.draws[mesh.cursor];for(uint32_t lane=0;lane<9;++lane)
            need(PPC_LOAD_U32(row.entry+4*lane)==row.words[lane],"Immediate mono submesh changed after qualification");
        monoMeshOperation(c,base,site);++mesh.cursor;run.submeshUpdated=false;run.materialCommitted=false;return;
    }
    if(site==0x8270142C) {
        need(mesh.cursor==mesh.draws.size()&&!run.materialActive&&!run.submeshActive,"Immediate mono stream cleanup preceded its draws");
        monoMeshOperation(c,base,site);return;
    }
    need(site==0x82701438&&mesh.phase==4&&mesh.cursor==mesh.draws.size()&&c.r3.u32==mesh.object,
         "Immediate mono mesh did not execute its original cleanup and return");
}
void EngineEffects::inspectMonoMesh(PPCContext& c,uint8_t* base) {
    auto& s=*state;auto& record=s.activeMono(c,base);auto& mesh=s.monoMesh;
    const bool skin=c.lastFunction==0x82700318&&uint32_t(c.lr)==0x82740BB8;
    need((skin?(c.r5.u32==s.monoTyped&&!c.r6.u32):
                (c.lastFunction==0x826FF4C8&&uint32_t(c.lr)==0x82740BD8&&!c.r5.u32))&&
         c.r1.u32>=0x200&&!(c.r1.u32&15)&&(!mesh.native||mesh.phase==4)&&
         c.r1.u32==s.monoFrame&&c.r31.u32==s.monoPacket,"Unqualified mono mesh entry");
    need(s.monoWorld&&s.monoBoolean&&s.monoMaterial&&s.monoReplay.complete&&s.monoReplay.phase==4,
         "Mono mesh requires completed original callbacks and constant uploads");
    s.backend.requireMonoCommit(s.monoCommit);s.runtime.pointer(c.r3.u32,0x2C,false);s.runtime.pointer(c.r4.u32,0x28,false);
    const auto bones=PPC_LOAD_U32(c.r3.u32+0x24);
    need(c.r3.u32==PPC_LOAD_U32(s.monoPacket)&&c.r4.u32==PPC_LOAD_U32(s.monoPacket+4)&&
         (skin?(bones&&bones<=255&&c.r7.u32==PPC_LOAD_U32(c.r3.u32+0x10)):
               (!bones&&c.r6.u32==PPC_LOAD_U32(c.r3.u32+0x10)))&&
         !s.privateWord(record.view.identity,64),"Mono mesh palette, Boolean or range differs");
    const auto geometry=PPC_LOAD_U32(c.r3.u32+0xC);s.runtime.pointer(geometry,0x78,false);
    mesh={};mesh.metadata=c.r3.u32;mesh.object=c.r4.u32;mesh.geometry=geometry;
    mesh.frame=c.r1.u32-(skin?0xB0u:0x90u);mesh.bones=bones;
    if(skin){mesh.boneGroups=boneGroupRows(s.runtime,bones,mesh.metadata,mesh.object,true);mesh.expectedGroups=uint32_t(mesh.boneGroups.size());}
    static thread_local uint32_t logs{};if(logs++<4) {
        std::fprintf(stderr,"[NATIVE MONO MESH] metadata=%08X object=%08X geometry=%08X submeshes=%u\n",mesh.metadata,mesh.object,geometry,c.r6.u32);
        std::fprintf(stderr,"[NATIVE MONO STATE]");const auto& effective=s.runtime.engineDriver->effectiveState();
        for(const auto& field:Graphics::scalarStateEvidence())std::fprintf(stderr," %X=%08X",field.id,effective.scalar(field.id));std::fprintf(stderr,"\n");
    }
}
void EngineEffects::monoRecordingOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;auto& record=s.activeMono(c,base);auto& run=s.monoRecording;auto& mesh=s.monoMesh;
    auto& driver=*s.runtime.engineDriver;auto& textures=s.monoTextures;
    need(s.monoRecordingPath&&s.monoTechnique==0x0003FFFC&&record.recordingContext&&currentContext==&c,
         "Mono deferred path has no original opaque context lease");
    driver.recordingOwners().requireActiveContext(record.contextIdentity);
    const auto& pass=record.metadata->techniques().front();
    if(site==0x826F39E0) {
        need(c.lastFunction==site&&uint32_t(c.lr)==0x82740534&&c.r1.u32==s.monoFrame&&c.r31.u32==s.monoPacket&&
             c.r3.u32==PPC_LOAD_U32(0x82D09784)&&c.r4.u32==s.monoId&&c.r5.u32==pass.handle&&!c.r6.u32&&!textures.active,
             "Unqualified original deferred mono texture entry");
        textures={};textures.cpu=&c;textures.sp=c.r1.u32;textures.manager=c.r3.u32;textures.context=record.contextIdentity;
        textures.shadowOwner=PPC_LOAD_U32(s.monoPacket+0x20);textures.active=true;return;
    }
    if(site==0x826F3A10||site==0x826F3ABC||site==0x826F3B6C) {
        need(textures.active&&textures.cpu==&c&&textures.context==record.contextIdentity&&
             c.r1.u32==textures.sp-0xC0&&PPC_LOAD_U32(c.r1.u32)==textures.sp&&PPC_LOAD_U32(textures.sp-8)==0x82740534&&
             c.r23.u32==textures.manager&&c.r30.u32==s.monoId,"Original deferred mono texture frame differs");
        if(site==0x826F3A10) {
            need(!textures.phase&&!textures.cursor&&c.r3.u32==textures.shadowOwner&&c.r31.u32==pass.handle&&!c.r29.u32,
                 "Original deferred mono named shadow lookup differs");
            constexpr uint32_t handles[]={0x001C000D,0x0020000F,0x00240011};
            constexpr uint32_t fields[]={0x698,0x694,0x6A0},owners[]={0xF0,0xF4,0xFC};
            s.runtime.pointer(textures.shadowOwner,0x6A4,false);const auto& shadow=s.find(PPC_LOAD_U32(textures.shadowOwner+0x1C));
            need(PPC_LOAD_U32(textures.shadowOwner)==0x8214E518&&shadow.view.source==0x820C0550&&
                 shadow.view.manager==record.view.manager&&shadow.view.pool==record.view.pool,"Deferred mono shadow owner differs");
            for(uint32_t i=0;i<3;++i) {
                need(PPC_LOAD_U32(textures.shadowOwner+fields[i])==handles[i]&&!s.monoSharedUsage(record,pass,handles[i]),
                     "Deferred mono shared usage or handle differs");
                const auto texture=PPC_LOAD_U32(textures.shadowOwner+owners[i]);const auto view=driver.shadowTextures().view(texture);
                need(view.owner==textures.shadowOwner&&view.field==owners[i],"Deferred mono shadow texture has another owner");
                textures.rows[i]={handles[i],0,i,texture};
            }
            c.r11.s64=int64_t(20*(pass.passHandle>>18));c.r8.u64=0x66666667;c.r28.u64=0;textures.phase=1;return;
        }
        if(site==0x826F3B6C) {
            need(textures.phase==1&&textures.cursor==3&&c.r28.u32==12,"Deferred mono shared texture loop is incomplete");
            textures.active=false;textures.complete=true;return;
        }
        need(textures.phase==1&&textures.cursor<3&&c.r28.u32==4*textures.cursor&&c.r3.u32==s.monoId&&
             c.r4.u32==pass.passHandle&&c.r27.u32==pass.passHandle&&c.r5.u32==textures.rows[textures.cursor].handle&&
             c.r31.u32==c.r5.u32,"Deferred mono texture usage query differs");
        for(uint32_t i=0;i<3;++i)need(PPC_LOAD_U32(c.r1.u32+0x50+4*i)==textures.rows[i].handle&&
            PPC_LOAD_U32(c.r1.u32+0x60+4*i)==textures.rows[i].texture,"Deferred mono texture snapshot changed");
        c.r3.u64=0;c.lr=site+4;++textures.cursor;return;
    }
    need(textures.complete&&!textures.active,"Deferred mono mesh preceded original texture closure");
    if(site==0x82701448) {
        need(c.lastFunction==site&&uint32_t(c.lr)==0x82740624&&c.r1.u32==s.monoFrame&&c.r31.u32==s.monoPacket&&
             c.r5.u32==s.monoTyped&&!c.r6.u32&&c.r7.u32==PPC_LOAD_U32(c.r3.u32+0x10)&&
             c.r8.u32==textures.manager&&c.r9.u32==PPC_LOAD_U8(s.monoPacket+0xC)&&!run.active,
             "Unqualified original deferred mono mesh entry");
        const auto metadata=c.r3.u32,object=c.r4.u32;const auto geometry=PPC_LOAD_U32(metadata+0xC);
        need(metadata==PPC_LOAD_U32(s.monoPacket)&&object==PPC_LOAD_U32(s.monoPacket+4)&&!PPC_LOAD_U32(metadata+0x24),
             "Deferred mono static packet differs");
        s.runtime.pointer(metadata,0x38,false);s.runtime.pointer(object,0xC0,false);s.runtime.pointer(geometry,0x78,false);
        const auto objectGeometry=PPC_LOAD_U32(object+0x18),dynamic=PPC_LOAD_U32(0x82CF05EC),morph=PPC_LOAD_U32(0x82D6CAB0);
        need(uint64_t(object)+dynamic+4<=UINT32_MAX&&uint64_t(objectGeometry)+morph+4<=UINT32_MAX,
             "Deferred mono geometry extension overflows");
        s.runtime.pointer(object+dynamic,4,false);s.runtime.pointer(objectGeometry,0x28,false);s.runtime.pointer(objectGeometry+morph,4,false);
        need(!(PPC_LOAD_U32(object+dynamic)&&(PPC_LOAD_U32(objectGeometry+8)&0x04000000))&&
             !PPC_LOAD_U32(objectGeometry+morph)&&!PPC_LOAD_U32(geometry+0x34),"Deferred mono external or morph streams are unported");
        run={};run.cpu=&c;run.packet=s.monoPacket;run.typed=s.monoTyped;run.sp=c.r1.u32;run.context=record.contextIdentity;
        run.previous=PPC_LOAD_U32(run.packet+0x14);run.manager=textures.manager;run.payload=driver.recordingOwners().activePayload(run.context);
        run.payloadId=PPC_LOAD_U32(run.manager+0x54);need(run.payloadId&&!s.rigidPayloads.contains(run.payloadId),"Mono recording payload is not fresh");
        mesh={};mesh.metadata=metadata;mesh.object=object;mesh.geometry=geometry;mesh.frame=run.sp-0xE0;
        const auto count=PPC_LOAD_U32(metadata+0x10),entries=PPC_LOAD_U32(metadata+0x14),collection=PPC_LOAD_U32(metadata+0x34);
        need(count<=UINT32_MAX/36,"Deferred mono submesh snapshot overflows");s.runtime.pointer(entries,36*count,false);
        s.runtime.pointer(collection,12,false);const auto materials=PPC_LOAD_U32(collection);
        need(materials<=(UINT32_MAX-12)/4,"Deferred mono material collection overflows");s.runtime.pointer(collection,12+4*materials,false);
        const auto offsets=PPC_LOAD_U32(objectGeometry+0x24),headers=PPC_LOAD_U32(0x82D6D814);
        for(uint32_t i=0;i<count;++i) {
            State::RigidMeshRun::Submesh row;row.entry=entries+36*i;
            for(uint32_t j=0;j<9;++j)row.words[j]=PPC_LOAD_U32(row.entry+4*j);
            need(uint64_t(offsets)+4ull*row.words[0]+4<=UINT32_MAX,"Deferred mono material offset overflows");
            s.runtime.pointer(offsets+4*row.words[0],4,false);row.materialOffset=PPC_LOAD_U32(offsets+4*row.words[0]);
            need(uint64_t(headers)+row.materialOffset+4<=UINT32_MAX,"Deferred mono material header overflows");
            row.materialHeader=headers+row.materialOffset;s.runtime.pointer(row.materialHeader,4,false);row.flags=PPC_LOAD_U16(row.materialHeader);
            if(row.flags&0x20)continue;
            if((row.flags&4)&&PPC_LOAD_U32(0x82D5DA78))need(!PPC_LOAD_U8(PPC_LOAD_U32(0x82D5DA78)+0x1C),
                "Deferred mono special sample-mask branch is unported");
            need(row.words[1]<materials,"Deferred mono material selector is out of range");
            row.material=PPC_LOAD_U32(collection+12+4*row.words[1]);s.runtime.pointer(row.material,0x18,false);mesh.draws.push_back(row);
        }
        run.live=s.backend.createMonoReplayConstants();run.active=true;return;
    }
    need(run.active&&run.cpu==&c&&run.context==record.contextIdentity&&run.packet==s.monoPacket,
         "Deferred mono mesh lost its original lease");
    if(site==0x82701628) {
        need(c.r1.u32==mesh.frame&&PPC_LOAD_U32(mesh.frame)==run.sp&&mesh.phase==3&&mesh.cursor==mesh.draws.size()&&
             !run.materialActive&&!run.materialCommitted,"Deferred mono loop ended before its original material/draw closure");
        mesh.phase=4;run.active=false;run.complete=true;return;
    }
    if(site==0x827015C0) {
        need(mesh.cursor<mesh.draws.size()&&run.materialCommitted&&!run.materialActive&&c.r31.u32==mesh.draws[mesh.cursor].entry,
             "Deferred mono draw preceded its original material commit");
        const auto& row=mesh.draws[mesh.cursor];
        for(uint32_t i=0;i<9;++i)need(PPC_LOAD_U32(row.entry+4*i)==row.words[i],"Deferred mono submesh changed before draw");
        need(PPC_LOAD_U16(row.materialHeader)==row.flags,"Deferred mono material header changed before draw");
        monoMeshOperation(c,base,site);++mesh.cursor;run.materialCommitted=false;return;
    }
    monoMeshOperation(c,base,site);
}
void EngineEffects::monoMeshOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.activeMono(c,base);auto& mesh=s.monoMesh;auto& driver=*s.runtime.engineDriver;
    const bool recording=s.monoRecording.active;
    if(!recording)s.backend.requireMonoCommit(s.monoCommit);
    need(mesh.geometry&&PPC_LOAD_U32(mesh.metadata+0xC)==mesh.geometry,"Mono mesh lost its CPU geometry");
    const bool binding=site==0x826FF340||site==0x826FF3B4||site==0x826FF498||site==0x826FF4A4;
    if(binding) {
        need(c.r1.u32==mesh.frame-0xA0&&PPC_LOAD_U32(c.r1.u32)==mesh.frame&&c.r27.u32==mesh.geometry&&
             c.r3.u32==PPC_LOAD_U32(0x82D63028),"Mono binding lost its original frame/context");
        if(recording)driver.recordingOwners().requireActiveContext(c.r3.u32);else driver.requireContext(c.r3.u32);
    } else need(c.r1.u32==mesh.frame&&PPC_LOAD_U32(c.r1.u32)==mesh.frame+(recording?0xE0u:s.monoImmediatePath?0xF0u:mesh.bones?0xB0u:0x90u)&&
                (recording?c.r23.u32:s.monoImmediatePath?c.r25.u32:c.r28.u32)==mesh.metadata&&
                (recording?c.r20.u32:s.monoImmediatePath?c.r22.u32:mesh.bones?c.r24.u32:c.r27.u32)==mesh.object&&
                c.r3.u32==PPC_LOAD_U32(recording||s.monoImmediatePath?0x82D63028u:0x82D0CAF8u)&&
                (recording||s.monoImmediatePath||!c.r3.u32),
                "Mono draw/cleanup frame or legacy device differs");
    const auto geometry=mesh.geometry;
    const auto capture=[&](uint32_t address,uint32_t count,size_t maxBytes=16*1024*1024){
        need(count&&count<=maxBytes,"Mono snapshot extent is invalid");
        const auto* bytes=s.runtime.pointer(address,count,false);return std::vector<uint8_t>(bytes,bytes+count);
    };
    if(site==0x826FF340||site==0x826FF3B4) {
        need(!mesh.phase&&!c.r4.u32&&c.r5.u32==geometry+0x38&&!c.r6.u32&&c.r7.u32==PPC_LOAD_U32(geometry+4)&&c.r8.u32==1,
             "Original static mono stream arguments differ");
        const auto count=PPC_LOAD_U32(geometry+8);need(count>=2&&count<=65,"Static mono declaration extent differs");
        need(validOriginalVertexExtent(PPC_LOAD_U32(geometry),c.r7.u32),"Original mono vertex byte owner or stride is invalid");
        auto header=capture(geometry,0x78);const auto vertices=capture(PPC_LOAD_U32(geometry+0x10),PPC_LOAD_U32(geometry),originalGeometryMaxVertexBytes);
        const auto indices=capture(PPC_LOAD_U32(geometry+0x1C),PPC_LOAD_U32(geometry+0x14),originalGeometryMaxIndexBytes);
        const auto elements=capture(PPC_LOAD_U32(geometry+0xC),12*count);
        std::vector<Graphics::MonoVertex> decoded;
        try {
            if(mesh.bones) {
                const auto source=decodeCharacterVertices(vertices,elements,c.r7.u32,std::min(mesh.bones,64u));
                decoded.resize(source.size());
                for(size_t i=0;i<source.size();++i) {
                    decoded[i].position=source[i].position;decoded[i].weights=source[i].weights;decoded[i].indices=source[i].indices;
                }
            } else {
                const auto positions=decodeStaticZPrepassVertices(vertices,elements,c.r7.u32);
                decoded.resize(positions.size());for(size_t i=0;i<positions.size();++i)decoded[i].position=positions[i].position;
            }
        }
        catch(const Failure& error) {
            std::fprintf(stderr,"[NATIVE MONO DECODE REJECTED] geometry=%08X stride=%u draws=%llu reason=%s elements=",
                geometry,c.r7.u32,static_cast<unsigned long long>(s.backend.monoMeshDrawCount()),error.what());
            for(const auto byte:elements)std::fprintf(stderr,"%02X",byte);std::fprintf(stderr,"\n");
            if(!s.runtime.frameCaptureDirectory.empty()) {
                const auto write=[&](const char* name,std::span<const uint8_t> bytes){
                    std::ofstream out(s.runtime.frameCaptureDirectory/name,std::ios::binary);
                    out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
                    need(bool(out),"Rejected mono diagnostic capture failed");
                };
                write("rejected-mono-elements.bin",elements);write("rejected-mono-vertices.bin",vertices);
                write("rejected-mono-indices.bin",indices);write("rejected-mono-geometry.bin",header);
                const auto camera=driver.cameraBinding();const auto depth=driver.depth(camera.depthIdentity);
                const auto pixels=s.backend.readbackDepthTarget(depth);write("mono-depth.f32s8",pixels);
                size_t occupied=0;for(size_t at=0;at<pixels.size();at+=8){float z;std::memcpy(&z,pixels.data()+at,4);occupied+=z!=0;}
                std::ofstream meta(s.runtime.frameCaptureDirectory/"mono-depth.json");
                meta<<"{\"width\":"<<depth->pixelWidth()<<",\"height\":"<<depth->pixelHeight()<<",\"nonzero_depth_pixels\":"<<occupied
                    <<",\"mono_draws\":"<<s.backend.monoMeshDrawCount()<<",\"capture_source\":\"private_main_camera_depth_readback\"}\n";
            }
            throw;
        }
        // Mono has no morph inputs. Skin weights/indices are retained for the
        // later original Boolean1 and palette commit; static payloads stay zero.
        const auto decodedIndices=decodeCharacterIndices(indices);
        need(PPC_LOAD_U32(geometry+0x18)==1&&(PPC_LOAD_U32(geometry+0x50)&~3u)==PPC_LOAD_U32(geometry+0x10)&&
             (PPC_LOAD_U32(geometry+0x54)&3)==2&&(PPC_LOAD_U32(geometry+0x58)&0xE0000003)==0x20000002&&
             PPC_LOAD_U32(geometry+0x70)==PPC_LOAD_U32(geometry+0x1C)&&PPC_LOAD_U32(geometry+0x74)==indices.size(),
             "Original static mono source buffer headers differ");
        const auto cache=PPC_LOAD_U32(geometry+0x30);s.runtime.pointer(cache,12,false);
        const auto declaration=PPC_LOAD_U32(cache+4);need(declaration&&c.r23.u32==declaration,"Original mono declaration is missing");
        auto native=s.backend.uploadMonoMesh(decoded,decodedIndices,mesh.bones!=0);
        if(recording)s.backend.bindMonoMeshVertices(s.monoRecording.payload,native);else s.backend.bindMonoMeshVertices(native);
        mesh.header=std::move(header);mesh.cache=cache;mesh.declaration=declaration;mesh.native=std::move(native);mesh.phase=1;
    } else {
        need(mesh.native&&mesh.header.size()==0x78&&!std::memcmp(s.runtime.pointer(geometry,0x78,false),mesh.header.data(),0x78)&&
             PPC_LOAD_U32(mesh.cache+4)==mesh.declaration,"Original mono geometry/header changed before draw");
        if(site==0x826FF498) {
            need(mesh.phase==1&&c.r4.u32==mesh.declaration,"Original mono declaration arguments differ");
            if(recording)s.backend.bindMonoMeshDeclaration(s.monoRecording.payload,mesh.native);else s.backend.bindMonoMeshDeclaration(mesh.native);mesh.phase=2;
        } else if(site==0x826FF4A4) {
            need(mesh.phase==2&&c.r4.u32==geometry+0x58,"Original mono index arguments differ");
            if(recording)s.backend.bindMonoMeshIndices(s.monoRecording.payload,mesh.native);else s.backend.bindMonoMeshIndices(mesh.native);mesh.phase=3;
        } else if(site==(s.monoImmediatePath?0x8270142Cu:0x826FF5B0u)) {
            need(mesh.phase==3&&c.r4.u32==1&&!c.r5.u32&&!c.r6.u32&&!c.r7.u32&&c.r8.u32==1,
                 "Original mono stream cleanup differs");
            s.backend.clearMonoAuxiliaryStream();mesh.phase=4;
        } else {
            need((recording?site==0x827015C0:s.monoImmediatePath?site==0x827013B0:mesh.bones?site==0x82700470:site==0x826FF584)&&mesh.phase==3&&
                 (!mesh.bones||mesh.skinCommit),"Original mono draw is out of order");
            const auto count=PPC_LOAD_U32(mesh.metadata+0x10),submeshes=PPC_LOAD_U32(mesh.metadata+0x14);
            const auto offset=recording?c.r24.u32:s.monoImmediatePath?c.r21.u32:mesh.bones?c.r29.u32:c.r31.u32;
            const auto entry=recording||s.monoImmediatePath||mesh.bones?c.r31.u32:c.r11.u32;
            if(mesh.bones>64){
                validateBoneGroupSnapshot(s.runtime,mesh.boneGroups,entry);
                need(mesh.paletteEntry==entry&&mesh.paletteCount>=1&&mesh.paletteCount<=64,
                     "Grouped mono draw lost its committed submesh palette");
            }
            need(count<=UINT32_MAX/36&&offset%36==0&&offset/36<count&&entry==submeshes+offset&&
                 c.r4.u32==PPC_LOAD_U32(entry+12)&&c.r5.u32==PPC_LOAD_U32(entry+16)&&
                 c.r6.u32==PPC_LOAD_U32(entry+20)&&c.r7.u32==PPC_LOAD_U32(entry+24),
                 "Original static mono submesh arguments differ");
            const auto& effective=driver.effectiveState();const auto camera=driver.cameraBinding();
            using S=Graphics::ScalarState;Graphics::MonoMeshDraw draw{};
            draw.primitiveType=c.r4.u32;draw.baseVertex=c.r5.s32;draw.startIndex=c.r6.u32;draw.indexCount=c.r7.u32;draw.viewport=camera.viewport;
            if(const auto scissor=s.backend.scissor())draw.scissor=*scissor;
            draw.depthEnable=effective.scalar(S::DepthEnable);draw.depthWrite=effective.scalar(S::DepthWrite);draw.depthCompare=effective.scalar(S::DepthCompare);
            draw.cull=effective.scalar(S::Cull);draw.fill=effective.scalar(S::Fill);draw.colorMask=effective.scalar(S::ColorMask0);
            draw.stencilEnable=effective.scalar(S::StencilEnable);draw.alphaTest=effective.scalar(S::AlphaTest);
            draw.scissorEnable=effective.scalar(S::ScissorEnable);draw.halfPixelOffset=effective.scalar(S::HalfPixelOffset);
            draw.primitiveReset=effective.scalar(S::PrimitiveResetEnable);draw.primitiveResetIndex=effective.scalar(S::PrimitiveResetIndex);
            draw.viewportEnable=effective.scalar(S::ViewportEnable);draw.clipPlaneEnable=effective.scalar(S::ClipPlaneEnable);
            draw.multisampleAntialias=effective.scalar(S::MultisampleAntialias);draw.multisampleMask=effective.scalar(S::MultisampleMask);
            draw.alphaToMask=effective.scalar(S::AlphaToMask);draw.depthBiasBits=effective.scalar(S::DepthBias);draw.slopeBiasBits=effective.scalar(S::SlopeBias);
            draw.depthPolicy=Graphics::ShadowMeshDepthPolicy::Reference20e4Rne;
            draw.blendEnable=effective.scalar(S::BlendEnable);draw.blendWord=effective.effectiveBlend(0);draw.expandedBlend=effective.scalar(S::ExpandedBlend0);
            need(!effective.scalar(S::TessellationMode),"Static mono tessellation is unqualified");
            bool alphaOne=false;const auto color=driver.color(camera.colorIdentity,alphaOne);need(!alphaOne,"Mono front target has an alpha-one owner");
            if(recording)s.backend.recordMonoMesh(s.monoRecording.payload,color,driver.depth(camera.depthIdentity),mesh.native,*s.monoVertex,*s.monoPixel,
                s.monoRecording.constants,s.monoRecording.booleans,s.monoRecording.live,draw);
            else s.backend.drawMonoMesh(color,driver.depth(camera.depthIdentity),mesh.native,*s.monoVertex,*s.monoPixel,s.monoCommit,draw);
            static thread_local uint32_t monoSkinDrawSample{};
            if(s.backend.monoMeshDrawCount()<=8||(mesh.bones&&sampleHotLog(monoSkinDrawSample)))
                std::fprintf(stderr,"[NATIVE MONO DRAW] geometry=%08X vertices=%u indices=%u count=%llu bones=%u\n",
                    geometry,mesh.native->vertexCount(),draw.indexCount,static_cast<unsigned long long>(s.backend.monoMeshDrawCount()),mesh.bones);
        }
    }
    c.lr=site+4;
}
uint64_t EngineEffects::monoMeshDrawCount() const {state->require(state->runtime.base);return state->backend.monoMeshDrawCount();}
void EngineEffects::monoSkinOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    if(site==0x82700318) {
        // Also called by the original depth-prepass branch; its independent
        // entry guard remains responsible for that path.
        if(uint32_t(c.lr)==0x82740BB8)inspectMonoMesh(c,base);
        return;
    }
    auto& s=*state;auto& record=s.activeMono(c,base);auto& mesh=s.monoMesh;
    const auto& v=record.view;
    need(mesh.bones&&mesh.bones<=255&&PPC_LOAD_U32(mesh.metadata+0x24)==mesh.bones&&
         PPC_LOAD_U32(mesh.metadata+0xC)==mesh.geometry&&PPC_LOAD_U32(mesh.frame)==s.monoFrame&&
         PPC_LOAD_U32(mesh.frame+0xA8)==0x82740BB8,
         "Skinned mono lost its original mesh frame or bone palette");
    if(site==0x827003A0||site==0x82700440) {
        const bool grouped=site==0x82700440;uint32_t count=mesh.bones;
        need(mesh.phase==3&&c.r1.u32==mesh.frame&&c.r28.u32==mesh.metadata&&
             c.r24.u32==mesh.object&&c.r30.u32==s.monoTyped&&c.r3.u32==v.identity&&
             c.r4.u32==0x00340016&&PPC_LOAD_U32(s.monoTyped+0xBC)==c.r4.u32&&
             c.r25.u32==0x82D64080,
             "Original mono bone-array arguments differ");
        if(grouped) {
            const auto rows=PPC_LOAD_U32(mesh.metadata+0x14),submeshes=PPC_LOAD_U32(mesh.metadata+0x10);
            need(mesh.bones>64&&c.r29.u32%36==0&&c.r29.u32/36<submeshes&&c.r31.u32==rows+c.r29.u32,
                 "Original grouped mono palette has no current submesh");
            validateBoneGroupSnapshot(s.runtime,mesh.boneGroups,c.r31.u32);
            const auto groups=PPC_LOAD_U32(c.r31.u32+28),table=PPC_LOAD_U32(c.r31.u32+32);
            count=boneGroupCount(s.runtime,mesh.bones,groups,table);
            need(c.r6.u32==count&&PPC_LOAD_U32(mesh.frame+0x50)==count&&
                 c.r5.u32==boneGroupSource(s.runtime,mesh.bones,groups,table,count),
                 "Original grouped mono palette source/count differs");
        } else need(mesh.bones<=64&&!mesh.boneUpload&&c.r5.u32==0x82D64080&&c.r6.u32==count,
                    "Original mono whole palette source/count differs");
        const auto parameters=record.metadata->parameters(false);
        const auto p=std::find_if(parameters.begin(),parameters.end(),[](const auto& p){return p.handle==0x00340016;});
        need(p!=parameters.end()&&p->name=="kBoneMatrices"&&
             p->descriptorWords==std::array<uint32_t,2>{0x00000102,0x03000041},
             "Original mono bone-array descriptor differs");
        const auto body=record.metadata->body();const auto descriptors=word(body,0x108);
        for(uint32_t bone=0;bone<64;++bone)
            need(word(body,descriptors+8*(14+bone))==0x000005B0&&
                 word(body,descriptors+8*(14+bone)+4)==0x000C0011+4*bone,
                 "Original mono child matrix descriptor differs");
        // 826FD060 uses the same descriptor-selected transpose as 826FDBE0,
        // in groups of eight matrices and then a scalar tail. Child 5B0
        // selects the all-one mask at 82000EC0. 826FE7C8 already computed
        // the actual inverse-bind/pose product in the original CPU code.
        for(uint32_t i=0;i<4;++i)need(PPC_LOAD_U32(0x82000EC0+4*i)==UINT32_MAX,"Mono bone transpose mask changed");
        need(v.defaultVectorWords.size()==1092&&record.parameters,"Mono bone storage extent differs");
        const auto* source=s.runtime.pointer(c.r5.u32,64*count,false);
        auto* destination=s.runtime.pointer(record.parameters->address+68*4,64*count,true);
        for(uint32_t bone=0;bone<count;++bone)for(uint32_t row=0;row<4;++row)for(uint32_t lane=0;lane<4;++lane)
            std::memcpy(destination+64*bone+16*row+4*lane,source+64*bone+16*lane+4*row,4);
        for(uint32_t bone=0;bone<count;++bone){const auto leaf=11+bone;record.privateModified[leaf/8]|=uint8_t(0x80>>(leaf&7));}
        mesh.boneUpload=true;mesh.paletteCount=count;mesh.paletteEntry=grouped?c.r31.u32:0;
        mesh.skinBoolean=false;mesh.skinCommit=false;c.lr=site+4;return;
    }
    if(site==0x82700470){monoMeshOperation(c,base,site);return;}
    if(site==0x8270048C) {
        need(c.r1.u32==mesh.frame&&c.r28.u32==mesh.metadata&&c.r24.u32==mesh.object&&
             c.r30.u32==s.monoTyped&&mesh.phase==3&&(mesh.skinCommit||(mesh.bones>64&&!mesh.expectedGroups)),
             "Original skinned mono loop did not complete its native bindings and palette");
        s.backend.clearMonoAuxiliaryStream();mesh.phase=4;return;
    }
    const bool cleanup=site==0x826FE6CC;
    need((cleanup?(c.r1.u32==mesh.frame-0x80&&PPC_LOAD_U32(c.r1.u32)==mesh.frame&&
                   PPC_LOAD_U32(c.r1.u32+0x78)==0x8270048C&&mesh.phase==3&&(mesh.skinCommit||(mesh.bones>64&&!mesh.expectedGroups))):
                  (c.r1.u32==mesh.frame-0xA0&&PPC_LOAD_U32(c.r1.u32)==mesh.frame&&
                   c.r27.u32==mesh.geometry&&mesh.phase==1))&&
         c.r3.u32==PPC_LOAD_U32(0x82D63028)&&c.r4.u32>=1&&c.r4.u32<=6&&!c.r6.u32&&
         c.r8.u64==skinMorphStreamDirtyMask(c.r4.u32),
         "Original mono unused stream lost its source frame or fetch-group mask");
    s.runtime.engineDriver->requireContext(c.r3.u32);
    auto& mask=cleanup?mesh.cleanupMask:mesh.auxiliaryMask;
    const uint32_t bit=1u<<(c.r4.u32-1);
    need(mask==bit-1,"Original mono auxiliary streams changed order");
    if(site==0x826FF434) {
        need(c.r4.u32==c.r30.u32&&c.r4.u32<=c.r28.u32&&c.r28.u32<=6&&c.r7.u32==12,
             "Original mono unused morph stream selection differs");
        s.runtime.pointer(c.r24.u32,12,false);
        const auto table=PPC_LOAD_U32(c.r24.u32+8);
        need(!(c.r25.u32&3)&&!(c.r10.u32&3)&&uint64_t(table)+c.r25.u32+4<=UINT32_MAX,
             "Original mono morph table offset overflows");
        s.runtime.pointer(table+c.r25.u32,4,false);const auto row=PPC_LOAD_U32(table+c.r25.u32);
        need(uint64_t(row)+c.r10.u32+4<=UINT32_MAX,"Original mono morph source offset overflows");
        s.runtime.pointer(row+c.r10.u32,4,false);
        need(PPC_LOAD_U32(row+c.r10.u32)==c.r5.u32,"Original mono morph source changed");
        s.runtime.pointer(c.r5.u32,32,false);
        const auto address=PPC_LOAD_U32(c.r5.u32+0x18),size=PPC_LOAD_U32(c.r5.u32+0x1C);
        const auto stride=PPC_LOAD_U32(mesh.geometry+4),vertexBytes=PPC_LOAD_U32(mesh.geometry);
        need(validOriginalVertexExtent(vertexBytes,stride)&&(address&3)==3&&(size&0xF0000003u)==0x10000002u,
             "Original mono morph source format or base owner differs");
        const auto morphBytes=size&0x0FFFFFFCu;
        need(morphBytes<=originalGeometryMaxVertexBytes,"Original mono morph owner has unproved upper resource bits");
        if(morphBytes)s.runtime.pointer(address&~3u,morphBytes,false);
        // VS82120C04 has exactly position/weights/indices FETCH inputs. No
        // morph stream or coefficient feeds either branch of this shader.
    } else need((site==0x826FF47C||cleanup)&&!c.r5.u32&&!c.r7.u32&&
                c.r4.u32==(cleanup?c.r31.u32:c.r30.u32),"Original mono auxiliary stream clear differs");
    mask|=bit;c.lr=site+4;
}
void EngineEffects::inspectZPrepassMesh(PPCContext& c,uint8_t* base) {
    if(state->monoId){inspectMonoMesh(c,base);return;}
    auto& s=*state;auto& record=s.activeZPrepass(c,base);auto& mesh=s.zprepassMesh;
    need(c.lastFunction==0x826FF4C8&&uint32_t(c.lr)==0x82740834&&!c.r5.u32&&c.r1.u32>=0x200&&!(c.r1.u32&15)&&
         (!mesh.native||mesh.phase==4),"Unqualified static zprepass mesh entry");
    s.backend.requireZPrepassCommit(s.zprepassCommit);s.runtime.pointer(c.r3.u32,0x2C,false);s.runtime.pointer(c.r4.u32,0x28,false);
    need(!PPC_LOAD_U32(c.r3.u32+0x24)&&c.r6.u32==PPC_LOAD_U32(c.r3.u32+0x10)&&
         !s.privateWord(record.view.identity,64),"Static zprepass mesh has a bone palette/Boolean or different range");
    const auto geometry=PPC_LOAD_U32(c.r3.u32+0xC);s.runtime.pointer(geometry,0x78,false);
    mesh={};mesh.metadata=c.r3.u32;mesh.object=c.r4.u32;mesh.geometry=geometry;mesh.frame=c.r1.u32-0x90;
    static thread_local uint32_t logs{};if(logs++<4) {
        std::fprintf(stderr,"[NATIVE ZPREPASS MESH] metadata=%08X object=%08X geometry=%08X submeshes=%u\n",mesh.metadata,mesh.object,geometry,c.r6.u32);
        std::fprintf(stderr,"[NATIVE ZPREPASS STATE]");const auto& effective=s.runtime.engineDriver->effectiveState();
        for(const auto& field:Graphics::scalarStateEvidence())std::fprintf(stderr," %X=%08X",field.id,effective.scalar(field.id));std::fprintf(stderr,"\n");
    }
}
void EngineEffects::zprepassMeshOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    if(state->monoId){monoMeshOperation(c,base,site);return;}
    if(state->rigidId&&(site==0x826FF340||site==0x826FF498||site==0x826FF4A4)) {
        rigidMeshOperation(c,base,site);return;
    }
    if(state->skinId&&(site==0x826FF340||site==0x826FF498||site==0x826FF4A4)) {
        skinMeshOperation(c,base,site);return;
    }
    auto& s=*state;s.activeZPrepass(c,base);auto& mesh=s.zprepassMesh;auto& driver=*s.runtime.engineDriver;
    s.backend.requireZPrepassCommit(s.zprepassCommit);
    need(mesh.geometry&&PPC_LOAD_U32(mesh.metadata+0xC)==mesh.geometry,"Zprepass mesh lost its CPU geometry");
    const bool binding=site==0x826FF340||site==0x826FF498||site==0x826FF4A4;
    if(binding) {
        need(c.r1.u32==mesh.frame-0xA0&&PPC_LOAD_U32(c.r1.u32)==mesh.frame&&c.r27.u32==mesh.geometry&&
             c.r3.u32==PPC_LOAD_U32(0x82D63028),"Zprepass binding lost its original frame/context");
        driver.requireContext(c.r3.u32);
    } else need(c.r1.u32==mesh.frame&&PPC_LOAD_U32(c.r1.u32)==mesh.frame+0x90&&
                c.r28.u32==mesh.metadata&&c.r27.u32==mesh.object&&c.r3.u32==PPC_LOAD_U32(0x82D0CAF8)&&!c.r3.u32,
                "Zprepass draw/cleanup frame or legacy device differs");
    const auto geometry=mesh.geometry;
    const auto capture=[&](uint32_t address,uint32_t count,size_t maxBytes=16*1024*1024){
        need(count&&count<=maxBytes,"Zprepass snapshot extent is invalid");
        const auto* bytes=s.runtime.pointer(address,count,false);return std::vector<uint8_t>(bytes,bytes+count);
    };
    if(site==0x826FF340) {
        need(!mesh.phase&&!c.r4.u32&&c.r5.u32==geometry+0x38&&!c.r6.u32&&c.r7.u32==PPC_LOAD_U32(geometry+4)&&c.r8.u32==1,
             "Original static zprepass stream arguments differ");
        const auto count=PPC_LOAD_U32(geometry+8);need(count>=2&&count<=65,"Static zprepass declaration extent differs");
        auto header=capture(geometry,0x78);
        // Fresh checked guest spans (range-validated borrows, no copy yet).
        // Identical validation to capture(): empty or oversized extents reject.
        const auto borrow=[&](uint32_t address,uint32_t size,size_t maxBytes=16*1024*1024){
            need(size&&size<=maxBytes,"Zprepass snapshot extent is invalid");
            const auto* bytes=s.runtime.pointer(address,size,false);return std::span<const uint8_t>(bytes,size);
        };
        const auto stride=c.r7.u32;
        need(validOriginalVertexExtent(PPC_LOAD_U32(geometry),stride),"Original zprepass vertex byte owner or stride is invalid");
        const auto vertexView=borrow(PPC_LOAD_U32(geometry+0x10),PPC_LOAD_U32(geometry),originalGeometryMaxVertexBytes);
        const auto indexView=borrow(PPC_LOAD_U32(geometry+0x1C),PPC_LOAD_U32(geometry+0x14),originalGeometryMaxIndexBytes);
        const auto elementView=borrow(PPC_LOAD_U32(geometry+0xC),12*count);
        // Source buffer header guards and declaration owner checks stay
        // unconditional and run before any hit use, so decl/index/stride
        // changes invalidate and invalid data still reaches a rejection.
        need(PPC_LOAD_U32(geometry+0x18)==1&&(PPC_LOAD_U32(geometry+0x50)&~3u)==PPC_LOAD_U32(geometry+0x10)&&
             (PPC_LOAD_U32(geometry+0x54)&3)==2&&(PPC_LOAD_U32(geometry+0x58)&0xE0000003)==0x20000002&&
             PPC_LOAD_U32(geometry+0x70)==PPC_LOAD_U32(geometry+0x1C)&&PPC_LOAD_U32(geometry+0x74)==indexView.size(),
             "Original static zprepass source buffer headers differ");
        const auto cacheAddress=PPC_LOAD_U32(geometry+0x30);s.runtime.pointer(cacheAddress,12,false);
        const auto declarationAddress=PPC_LOAD_U32(cacheAddress+4);
        need(declarationAddress&&c.r23.u32==declarationAddress,"Original zprepass declaration is missing");
        // Exact-content lookup: bounded back-entry exact check before XXH3,
        // then XXH3 reject key plus full length+memcmp of every plane and
        // stride. No guest address in the content key; no hash-only hit.
        // A repeat lookup of the same guest ranges skips the comparison only
        // while an exact write barrier proves none of their pages was written
        // since the last full comparison (re-verified on a bounded schedule).
        const auto sourceLookup=s.zprepassSourceCache.lookup(vertexView,indexView,elementView,stride);
        if(auto hit=sourceLookup.mesh) {
            // Hit: skip snapshot copies, decoder and upload entirely.
            // bindZPrepassMeshVertices still validates the native owner/device
            // on the shared mesh.
            s.backend.bindZPrepassMeshVertices(hit);
            mesh.header=std::move(header);mesh.cache=cacheAddress;mesh.declaration=declarationAddress;
            mesh.native=std::move(hit);mesh.phase=1;
            c.lr=site+4;return;
        }
        const auto vertices=capture(PPC_LOAD_U32(geometry+0x10),PPC_LOAD_U32(geometry),originalGeometryMaxVertexBytes);
        const auto indices=capture(PPC_LOAD_U32(geometry+0x1C),PPC_LOAD_U32(geometry+0x14),originalGeometryMaxIndexBytes);
        const auto elements=capture(PPC_LOAD_U32(geometry+0xC),12*count);
        std::vector<Graphics::ZPrepassVertex> decoded;
        try {decoded=decodeStaticZPrepassVertices(vertices,elements,c.r7.u32);}
        catch(const Failure& error) {
            std::fprintf(stderr,"[NATIVE ZPREPASS DECODE REJECTED] geometry=%08X stride=%u draws=%llu reason=%s elements=",
                geometry,c.r7.u32,static_cast<unsigned long long>(s.backend.zprepassMeshDrawCount()),error.what());
            for(const auto byte:elements)std::fprintf(stderr,"%02X",byte);std::fprintf(stderr,"\n");
            if(!s.runtime.frameCaptureDirectory.empty()) {
                const auto write=[&](const char* name,std::span<const uint8_t> bytes){
                    std::ofstream out(s.runtime.frameCaptureDirectory/name,std::ios::binary);
                    out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
                    need(bool(out),"Rejected zprepass diagnostic capture failed");
                };
                write("rejected-zprepass-elements.bin",elements);write("rejected-zprepass-vertices.bin",vertices);
                write("rejected-zprepass-indices.bin",indices);write("rejected-zprepass-geometry.bin",header);
                const auto camera=driver.cameraBinding();const auto depth=driver.depth(camera.depthIdentity);
                const auto pixels=s.backend.readbackDepthTarget(depth);write("zprepass-depth.f32s8",pixels);
                size_t occupied=0;for(size_t at=0;at<pixels.size();at+=8){float z;std::memcpy(&z,pixels.data()+at,4);occupied+=z!=0;}
                std::ofstream meta(s.runtime.frameCaptureDirectory/"zprepass-depth.json");
                meta<<"{\"width\":"<<depth->pixelWidth()<<",\"height\":"<<depth->pixelHeight()<<",\"nonzero_depth_pixels\":"<<occupied
                    <<",\"zprepass_draws\":"<<s.backend.zprepassMeshDrawCount()<<",\"capture_source\":\"private_main_camera_depth_readback\"}\n";
            }
            throw;
        }
        // Absent skin/morph fields are dead only for the original Boolean0
        // branch, which is checked at entry and actual constant commit.
        const auto decodedIndices=decodeCharacterIndices(indices);
        need(PPC_LOAD_U32(geometry+0x18)==1&&(PPC_LOAD_U32(geometry+0x50)&~3u)==PPC_LOAD_U32(geometry+0x10)&&
             (PPC_LOAD_U32(geometry+0x54)&3)==2&&(PPC_LOAD_U32(geometry+0x58)&0xE0000003)==0x20000002&&
             PPC_LOAD_U32(geometry+0x70)==PPC_LOAD_U32(geometry+0x1C)&&PPC_LOAD_U32(geometry+0x74)==indices.size(),
             "Original static zprepass source buffer headers differ");
        const auto cache=PPC_LOAD_U32(geometry+0x30);s.runtime.pointer(cache,12,false);
        const auto declaration=PPC_LOAD_U32(cache+4);need(declaration&&c.r23.u32==declaration,"Original zprepass declaration is missing");
        auto native=s.backend.uploadZPrepassMesh(decoded,decodedIndices);s.backend.bindZPrepassMeshVertices(native);
        // Insert only after the full decoder/index/renderer validation above
        // succeeded; rejections throw before reaching here and cache nothing.
        s.zprepassSourceCache.insert(vertices,indices,elements,stride,sourceLookup.key,native,&sourceLookup.pending);
        mesh.header=std::move(header);mesh.cache=cache;mesh.declaration=declaration;mesh.native=std::move(native);mesh.phase=1;
    } else {
        need(mesh.native&&mesh.header.size()==0x78&&!std::memcmp(s.runtime.pointer(geometry,0x78,false),mesh.header.data(),0x78)&&
             PPC_LOAD_U32(mesh.cache+4)==mesh.declaration,"Original zprepass geometry/header changed before draw");
        if(site==0x826FF498) {
            need(mesh.phase==1&&c.r4.u32==mesh.declaration,"Original zprepass declaration arguments differ");
            s.backend.bindZPrepassMeshDeclaration(mesh.native);mesh.phase=2;
        } else if(site==0x826FF4A4) {
            need(mesh.phase==2&&c.r4.u32==geometry+0x58,"Original zprepass index arguments differ");
            s.backend.bindZPrepassMeshIndices(mesh.native);mesh.phase=3;
        } else if(site==0x826FF5B0) {
            need(mesh.phase==3&&c.r4.u32==1&&!c.r5.u32&&!c.r6.u32&&!c.r7.u32&&c.r8.u32==1,
                 "Original zprepass stream cleanup differs");
            s.backend.clearZPrepassAuxiliaryStream();mesh.phase=4;
        } else {
            need(site==0x826FF584&&mesh.phase==3,"Original zprepass draw is out of order");
            const auto count=PPC_LOAD_U32(mesh.metadata+0x10),submeshes=PPC_LOAD_U32(mesh.metadata+0x14);
            need(count<=65535&&c.r31.u32%36==0&&c.r31.u32/36<count&&c.r11.u32==submeshes+c.r31.u32&&
                 c.r4.u32==PPC_LOAD_U32(c.r11.u32+12)&&c.r5.u32==PPC_LOAD_U32(c.r11.u32+16)&&
                 c.r6.u32==PPC_LOAD_U32(c.r11.u32+20)&&c.r7.u32==PPC_LOAD_U32(c.r11.u32+24),
                 "Original static zprepass submesh arguments differ");
            const auto& effective=driver.effectiveState();const auto camera=driver.cameraBinding();
            using S=Graphics::ScalarState;Graphics::ShadowMeshDraw draw{};
            draw.primitiveType=c.r4.u32;draw.baseVertex=c.r5.s32;draw.startIndex=c.r6.u32;draw.indexCount=c.r7.u32;draw.viewport=camera.viewport;
            if(const auto scissor=s.backend.scissor())draw.scissor=*scissor;
            draw.depthEnable=effective.scalar(S::DepthEnable);draw.depthWrite=effective.scalar(S::DepthWrite);draw.depthCompare=effective.scalar(S::DepthCompare);
            draw.cull=effective.scalar(S::Cull);draw.fill=effective.scalar(S::Fill);draw.colorMask=effective.scalar(S::ColorMask0);
            draw.stencilEnable=effective.scalar(S::StencilEnable);draw.alphaTest=effective.scalar(S::AlphaTest);
            draw.scissorEnable=effective.scalar(S::ScissorEnable);draw.halfPixelOffset=effective.scalar(S::HalfPixelOffset);
            draw.primitiveReset=effective.scalar(S::PrimitiveResetEnable);draw.primitiveResetIndex=effective.scalar(S::PrimitiveResetIndex);
            draw.viewportEnable=effective.scalar(S::ViewportEnable);draw.clipPlaneEnable=effective.scalar(S::ClipPlaneEnable);
            draw.multisampleAntialias=effective.scalar(S::MultisampleAntialias);draw.multisampleMask=effective.scalar(S::MultisampleMask);
            draw.alphaToMask=effective.scalar(S::AlphaToMask);draw.depthBiasBits=effective.scalar(S::DepthBias);draw.slopeBiasBits=effective.scalar(S::SlopeBias);
            draw.depthPolicy=Graphics::ShadowMeshDepthPolicy::Reference20e4Rne;
            need(!effective.scalar(S::TessellationMode),"Static zprepass tessellation is unqualified");
            bool alphaOne=false;s.backend.drawZPrepassMesh(driver.color(camera.colorIdentity,alphaOne),driver.depth(camera.depthIdentity),
                mesh.native,*s.zprepassVertex,s.zprepassCommit,draw);
            if(s.backend.zprepassMeshDrawCount()<=8)std::fprintf(stderr,"[NATIVE ZPREPASS DRAW] geometry=%08X vertices=%u indices=%u count=%llu\n",
                geometry,mesh.native->vertexCount(),draw.indexCount,static_cast<unsigned long long>(s.backend.zprepassMeshDrawCount()));
        }
    }
    c.lr=site+4;
}
uint64_t EngineEffects::zprepassDrawCount() const {state->require(state->runtime.base);return state->backend.zprepassMeshDrawCount();}
std::array<uint32_t,976> EngineEffects::readbackZPrepassConstants() {
    auto& s=*state;s.require(s.runtime.base);need(s.zprepassId&&s.zprepassVertex,"No active zprepass constant owner");
    s.backend.requireZPrepassShader(*s.zprepassVertex);
    return std::bit_cast<std::array<uint32_t,976>>(s.backend.readbackZPrepassConstants(s.zprepassCommit));
}
std::array<uint32_t,4> EngineEffects::readbackZPrepassBooleans() {
    auto& s=*state;s.require(s.runtime.base);need(s.zprepassId&&s.zprepassVertex,"No active zprepass Boolean owner");
    s.backend.requireZPrepassShader(*s.zprepassVertex);return s.backend.readbackZPrepassBooleans(s.zprepassCommit);
}
void EngineEffects::rigidImmediateOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.require(base);auto& run=s.rigidImmediate;auto& driver=*s.runtime.engineDriver;
    if(site==0x827400F8) {
        s.runtime.pointer(c.r3.u32,0x24,false);const auto typed=PPC_LOAD_U32(c.r3.u32+0x18);
        s.runtime.pointer(typed,0x20,false);
        if(PPC_LOAD_U32(typed)==0x8215020C){monoImmediateOperation(c,base,site);return;}
    } else if(s.monoImmediate.active){monoImmediateOperation(c,base,site);return;}
    if(site==0x827005DC&&!run.active&&!s.skinImmediate.active)return; // Observation in a shared original helper.
    if(site!=0x827400F8&&s.skinImmediate.active){skinImmediateOperation(c,base,site);return;}
    if(site!=0x827400F8&&s.rigidId&&s.find(s.rigidId).view.source==0x82036448){skyImmediateOperation(c,base,site);return;}
    need(currentContext==&c,"Rigid immediate work has no original CPU frame");
    if(site==0x827400F8) {
        // Chocolate fallback entries carry r4==r5==1 (r5 is dead, overwritten
        // from the packet; r4 flows into the original packet loop while the
        // native staging below keys off packet/typed/context only).
        const auto fbOwner=s.packetOwner(c.r3.u32);
        if(fbOwner.vtable==0x82061714&&isSkinSource(fbOwner.source)) {
            skinImmediateOperation(c,base,site);
            return;
        }
        const bool fallbackArguments=rigidFallbackArgumentsQualified(fbOwner.source,c.r4.u32,c.r5.u32);
        if(fbOwner.source==0x8200CCB8&&c.r4.u32==1) {
            s.runtime.pointer(fbOwner.typed,0xB0,false);
            need(PPC_LOAD_U32(fbOwner.typed+0xA8)==0x0003FFFC&&PPC_LOAD_U32(fbOwner.typed+0xAC)==0x0007FFFC,
                 "Rigid alpha typed technique association differs");
            // Checked reads stay unconditional (same words verified above);
            // only the diagnostic print is sampled.
            const uint32_t alphaOpaque=PPC_LOAD_U32(fbOwner.typed+0xA8),alphaAlpha=PPC_LOAD_U32(fbOwner.typed+0xAC);
            static thread_local uint32_t alphaEntrySample{};
            if(sampleHotLog(alphaEntrySample))
                std::fprintf(stderr,"[NATIVE RIGID ALPHA ENTRY] typed=%08X opaque=%08X alpha=%08X packet=%08X\n",
                    fbOwner.typed,alphaOpaque,alphaAlpha,fbOwner.packet);
        }
        if(c.r4.u32||c.r5.u32) {
            static thread_local uint32_t fallbackOwnerSample{};
            if(sampleHotLog(fallbackOwnerSample))
                std::fprintf(stderr,"[NATIVE FALLBACK OWNER] packet=%08X typed=%08X identity=%08X source=%08X wrapper=%08X context=%08X vtable=%08X selected=%08X\n",
                    fbOwner.packet,fbOwner.typed,fbOwner.identity,fbOwner.source,fbOwner.wrapper,fbOwner.context,fbOwner.vtable,s.rigidId);
        }
        if(!(c.lastFunction==site&&uint32_t(c.lr)==0x82740B28&&!run.active&&!s.rigidMesh.active&&
             !s.rigidTextures.active&&!s.rigidMaterial.active&&!s.skinMesh.active&&!s.skinMaterial.active&&
             !s.skyMesh.active&&!s.skyMaterial.active&&
             !s.skinImmediate.active&&fallbackArguments&&c.r6.u32<=1&&
             c.r1.u32>=0x400&&!(c.r1.u32&15))) {
            auto peek=[&](uint32_t a)->uint32_t { try { s.runtime.pointer(a,4,false); } catch(...) { return 0xDEADDEADu; } return PPC_LOAD_U32(a); };
            const auto packet=c.r3.u32;
            std::fprintf(stderr,"[NATIVE FALLBACK ENTRY] fn=%08X lr=%08X runA=%u rmesh=%u rtex=%u rmat=%u skmesh=%u skmat=%u skyM=%u skyMat=%u skinImm=%u r4=%08X r5=%08X r6=%08X r1=%08X packet=%08X meta=%08X typed=%08X source=%08X identity=%08X\n",
                c.lastFunction,uint32_t(c.lr),run.active,s.rigidMesh.active,s.rigidTextures.active,s.rigidMaterial.active,
                s.skinMesh.active,s.skinMaterial.active,s.skyMesh.active,s.skyMaterial.active,
                s.skinImmediate.active,c.r4.u32,c.r5.u32,c.r6.u32,c.r1.u32,packet,peek(packet),fbOwner.typed,fbOwner.source,fbOwner.identity);
        }
        need(c.lastFunction==site&&uint32_t(c.lr)==0x82740B28&&!run.active&&!s.rigidMesh.active&&
             !s.rigidTextures.active&&!s.rigidMaterial.active&&!s.skinMesh.active&&!s.skinMaterial.active&&
             !s.skyMesh.active&&!s.skyMaterial.active&&
             !s.skinImmediate.active&&fallbackArguments&&c.r6.u32<=1&&
             c.r1.u32>=0x400&&!(c.r1.u32&15),"Unqualified original opaque rigid fallback entry");
        {
            static uint32_t lastDevice=0xDEADDEADu;
            const auto device=PPC_LOAD_U32(0x82D0CAF8);
            if(device!=lastDevice) {
                std::fprintf(stderr,"[NATIVE DEVICE GLOBAL] 0x82D0CAF8=%08X packet=%08X\n",device,c.r3.u32);
                lastDevice=device;
            }
        }
        const auto packet=c.r3.u32;s.runtime.pointer(packet,0x24,false);
        const auto meta=PPC_LOAD_U32(packet),object=PPC_LOAD_U32(packet+4),typed=PPC_LOAD_U32(packet+0x18);
        s.runtime.pointer(meta,0x38,false);s.runtime.pointer(object,0x40,false);s.runtime.pointer(typed,0xB0,false);
        const bool vfx=isVfxRigidSource(fbOwner.source);
        need(PPC_LOAD_U32(typed)==(vfx?0x82061758u:0x820616C0u)&&!PPC_LOAD_U32(meta+0x24)&&
             PPC_LOAD_U32(0x82D6CCA8)==(vfx?0x20u:0u),
             "Rigid immediate fallback requires the original static opaque packet");
        const auto objectGeometry=PPC_LOAD_U32(object+0x18),morphOffset=PPC_LOAD_U32(0x82D6CAB0);
        need(uint64_t(objectGeometry)+morphOffset+4<=UINT32_MAX,"Rigid immediate morph offset overflows");
        s.runtime.pointer(objectGeometry+morphOffset,4,false);
        need(!PPC_LOAD_U32(objectGeometry+morphOffset),"Rigid immediate morph branch is not qualified");
        const auto context=PPC_LOAD_U32(packet+0x14);driver.requireContext(context);
        const auto& record=s.find(PPC_LOAD_U32(typed+0x1C));
        need(!record.recordingContext&&record.contextIdentity==context,"Rigid immediate fallback overlaps a recording lease");
        s.requireRigidReplayFinished();
        s.recycleTempUpload(s.rigidReplay.buffers);s.rigidReplay={};s.rigidTextures={};s.rigidMesh={};s.rigidMaterial={};s.recycleTempUpload(run.staging.buffers);run={};
        s.skyMesh={};s.skyMaterial={};s.skyStagedTextures={};
        run.cpu=&c;run.entrySP=c.r1.u32;run.frame=c.r1.u32-0x80;run.packet=packet;run.typed=typed;run.context=context;
        run.drawsBefore=s.backend.rigidMeshDrawCount();run.active=true;run.alpha=c.r4.u32==1;
        run.staging.cpu=&c;run.staging.packet=packet;run.staging.typed=typed;run.staging.context=context;run.staging.entrySP=run.frame;
        return; // Original quota result, dispatcher branch and prologue remain.
    }
    need(run.active&&run.cpu==&c&&run.activated,"Rigid immediate continuation has no activated lifetime");
    requireRigidSelection(run.typed);driver.requireContext(run.context);
    need(!s.find(s.rigidId).recordingContext,"Rigid immediate continuation acquired a recording lease");
    auto& mesh=s.rigidMesh;
    if(site==0x82700498) {
        need(c.lastFunction==site&&uint32_t(c.lr)==0x82701310&&c.r1.u32==mesh.frame&&mesh.active&&mesh.immediate&&
             mesh.cursor<mesh.draws.size()&&!run.submeshActive&&!mesh.submeshUpdated&&c.r3.u32==run.typed&&
             c.r4.u32==mesh.metadata&&c.r5.u32==mesh.object,"Rigid immediate submesh update entry differs");
        const auto& row=mesh.draws[mesh.cursor];
        need(c.r6.u32==row.words[1]&&c.r7.u32==row.words[0],"Rigid immediate submesh material selection differs");
        const auto bits=PPC_LOAD_U32(row.material+0xC),first=(bits>>15)&31,count=(bits>>20)&7;
        const auto rows=PPC_LOAD_U32(row.material+0x14);
        if(count)s.runtime.pointer(rows+12*first,12*count,false);
        for(uint32_t i=0;i<count;++i) {
            const auto type=(PPC_LOAD_U32(rows+12*(first+i))>>25)&63;
            // The original table's no-op categories still execute their real
            // binding lookup, packet copy and callback. Other categories need
            // their own staging/upload qualification before direct drawing.
            if((type!=9&&type!=22&&type!=23&&type!=24&&type!=32&&type!=39)||
               PPC_LOAD_U32(0x82CF0188+8*type)!=0x8270A480) {
                char why[200];std::snprintf(why,sizeof(why),
                    "Unqualified rigid immediate submesh callback: material=%08X row=%u type=%u count=%u",row.material,first+i,type,count);
                throw Failure(why);
            }
        }
        run.submeshActive=true;return;
    }
    if(site==0x827005DC) {
        need(run.submeshActive&&c.r1.u32==mesh.frame-0xE0&&PPC_LOAD_U32(c.r1.u32)==mesh.frame&&
             PPC_LOAD_U32(c.r1.u32+0xD8)==0x82701310&&c.r28.u32==run.typed&&
             c.r25.u32==mesh.draws[mesh.cursor].material,"Rigid immediate submesh update did not return through its original frame");
        run.submeshActive=false;mesh.submeshUpdated=true;return;
    }
    need(site==0x827402E4&&c.r1.u32==run.frame&&PPC_LOAD_U32(run.frame)==run.entrySP&&
         PPC_LOAD_U32(run.frame+0x78)==0x82740B28&&c.r31.u32==run.packet&&
         c.r30.u32==uint32_t(run.alpha)&&
         mesh.immediate&&mesh.complete&&!mesh.active&&mesh.auxiliaryCleared&&mesh.cursor==mesh.draws.size()&&
         !run.submeshActive&&!s.rigidMaterial.active&&!s.rigidMaterial.committed&&
         run.staging.complete&&run.staging.phase==4&&s.rigidTextures.complete&&!s.rigidTextures.active,
         "Rigid immediate fallback ended before original staging, draws and cleanup completed");
    need(s.backend.rigidMeshDrawCount()==run.drawsBefore+mesh.cursor,"Rigid immediate native draw receipt differs");
    {static thread_local uint32_t rigidImmediateCompleteSample{};
    if(sampleHotLog(rigidImmediateCompleteSample))
        std::fprintf(stderr,"[NATIVE RIGID IMMEDIATE COMPLETE] packet=%08X geometry=%08X draws=%u; original fallback and stream cleanup completed\n",
            run.packet,mesh.geometry,mesh.cursor);}
    run.active=false;run.commit.reset();
}
void EngineEffects::skyMeshOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    // Sky shares the original rigid loops but owns its mesh and replay constants.
    auto& s=*state;s.require(base);
    requireRigidSelection(s.rigidTyped);
    auto& record=s.find(s.rigidId);auto& mesh=s.skyMesh;
    const bool immediate=s.rigidImmediate.active;
    need(currentContext==&c&&s.rigidTextures.complete&&!s.rigidTextures.active&&s.rigidTextures.immediate==immediate&&
         record.contextIdentity==s.rigidTextures.context,"Sky mesh has no completed texture owner");
    std::shared_ptr<Graphics::NativeRecordingPayload> payload;
    if(immediate)need(!record.recordingContext&&s.rigidImmediate.cpu==&c&&s.rigidImmediate.activated&&
         s.rigidImmediate.staging.complete,"Sky immediate mesh has no completed original staging");
    else {need(bool(record.recordingContext),"Sky deferred mesh has no recording lease");
        payload=s.runtime.engineDriver->recordingOwners().activePayload(record.contextIdentity);}
    if(site==(immediate?0x82701220u:0x82701448u)) {
        need(c.lastFunction==site&&uint32_t(c.lr)==(immediate?0x827402B0u:0x82740624u)&&!mesh.active&&
             c.r1.u32==s.rigidTextures.sp&&PPC_LOAD_U32(c.r1.u32)==c.r1.u32+(immediate?0x80u:0xD0u)&&c.r5.u32==s.rigidTyped&&
             !c.r6.u32&&c.r7.u32==PPC_LOAD_U32(c.r3.u32+0x10)&&(immediate?c.r8.u32:c.r9.u32)==PPC_LOAD_U8(c.r31.u32+0xC),
             "Unqualified original sky mesh-loop entry");
        const auto packet=c.r31.u32,meta=c.r3.u32,object=c.r4.u32;
        s.runtime.pointer(meta,0x38,false);s.runtime.pointer(object,0x40,false);
        need(PPC_LOAD_U32(packet)==meta&&PPC_LOAD_U32(packet+4)==object&&PPC_LOAD_U32(packet+0x18)==s.rigidTyped&&
             !PPC_LOAD_U32(meta+0x24)&&!PPC_LOAD_U32(0x82D6CCA8),"Original sky mesh packet/static qualification differs");
        const auto geometry=PPC_LOAD_U32(meta+0xC),objectGeometry=PPC_LOAD_U32(object+0x18);
        s.runtime.pointer(geometry,0x78,false);s.runtime.pointer(objectGeometry,0x28,false);
        const auto dynamicOffset=PPC_LOAD_U32(0x82CF05EC),morphOffset=PPC_LOAD_U32(0x82D6CAB0);
        need(uint64_t(object)+dynamicOffset<=UINT32_MAX&&uint64_t(objectGeometry)+morphOffset<=UINT32_MAX,
             "Sky geometry extension offset overflows");
        s.runtime.pointer(object+dynamicOffset,4,false);s.runtime.pointer(objectGeometry+morphOffset,4,false);
        need(!(PPC_LOAD_U32(object+dynamicOffset)&&(PPC_LOAD_U32(objectGeometry+8)&0x04000000))&&
             !PPC_LOAD_U32(objectGeometry+morphOffset)&&!PPC_LOAD_U32(geometry+0x34),
             "Sky dynamic, morph or external stream is not qualified");
        State::SkyMeshRun next;next.cpu=&c;next.entrySP=c.r1.u32;next.frame=c.r1.u32-(immediate?0xF0u:0xE0u);next.immediate=immediate;
        next.packet=packet;next.metadata=meta;next.object=object;next.geometry=geometry;next.context=record.contextIdentity;next.payload=payload;
        if(!immediate) {
            next.payloadId=PPC_LOAD_U32(s.rigidTextures.manager+0x54);
            need(next.payloadId&&!s.rigidPayloads.contains(next.payloadId),"Sky recording payload is not fresh");
            for(const auto& [id,retained]:s.rigidPayloads)
                need(retained.native!=payload,"Sky recording reuses a retained native payload");
            next.replayValues=s.backend.createSkyReplayConstants();
        }
        const auto count=PPC_LOAD_U32(meta+0x10),entries=PPC_LOAD_U32(meta+0x14),collection=PPC_LOAD_U32(meta+0x34);
        need(count<=65535,"Sky submesh extent is unqualified");s.runtime.pointer(entries,36*count,false);s.runtime.pointer(collection,12,false);
        const auto materialCount=PPC_LOAD_U32(collection),offsets=PPC_LOAD_U32(objectGeometry+0x24),headers=PPC_LOAD_U32(0x82D6D814);
        need(materialCount<=65535,"Sky material collection extent is unqualified");s.runtime.pointer(collection,12+4*materialCount,false);
        for(uint32_t i=0;i<count;++i) {
            State::SkyMeshRun::Submesh row;row.entry=entries+36*i;
            for(uint32_t j=0;j<9;++j)row.words[j]=PPC_LOAD_U32(row.entry+4*j);
            need(uint64_t(offsets)+4ull*row.words[0]+4<=UINT32_MAX,"Sky material offset table overflows");
            s.runtime.pointer(offsets+4*row.words[0],4,false);row.materialOffset=PPC_LOAD_U32(offsets+4*row.words[0]);
            need(uint64_t(headers)+row.materialOffset+4<=UINT32_MAX,"Sky material header overflows");
            row.materialHeader=headers+row.materialOffset;s.runtime.pointer(row.materialHeader,4,false);
            row.flags=PPC_LOAD_U16(row.materialHeader);
            if(row.flags&0x20)continue;
            const auto special=PPC_LOAD_U32(0x82D5DA78);
            if((row.flags&4)&&special){s.runtime.pointer(special+0x1C,1,false);
                need(!PPC_LOAD_U8(special+0x1C),"Sky special sample-mask branch is not qualified");}
            need(row.words[1]<materialCount,"Sky submesh material index is out of range");
            row.material=PPC_LOAD_U32(collection+12+4*row.words[1]);s.runtime.pointer(row.material,0x18,false);
            next.draws.push_back(row);
        }
        // Sky reflection owns TimeTicker (leaf 19) and the sampler/uv
        // registers (g_LineSampler leaf 24, g_texCoords leaf 25); these are
        // the only mask exclusions in all 128 mask bytes.
        for(const auto leaf:{19u,24u,25u})
            need(!(record.privateMask[leaf/8]&(0x80>>(leaf&7))),"Sky inherited private mask remains material-eligible");
        next.drawsBefore=s.backend.skyMeshDrawCount();
        next.active=true;mesh=std::move(next);return;
    }
    need(mesh.active&&mesh.immediate==immediate&&mesh.payload==payload&&mesh.cpu==&c&&mesh.context==record.contextIdentity&&
         PPC_LOAD_U32(mesh.metadata+0xC)==mesh.geometry,"Sky mesh lifetime changed");
    const auto geometry=mesh.geometry;
    const bool binding=site==0x826FF340||site==0x826FF498||site==0x826FF4A4;
    if(!binding) {
        need(c.r1.u32==mesh.frame&&PPC_LOAD_U32(mesh.frame)==mesh.entrySP&&
             PPC_LOAD_U32(mesh.frame+(immediate?0xE8u:0xD8u))==(immediate?0x827402B0u:0x82740624u)&&
             (immediate?c.r19.u32:c.r17.u32)==s.rigidTyped&&(immediate?c.r22.u32:c.r20.u32)==mesh.object&&
             (immediate?c.r25.u32:c.r23.u32)==mesh.metadata,"Original sky submesh frame differs");
        // Post-binding sites share rigid's immediate loop (material commit,
        // draw, auxiliary cleanup, loop end). Material/draw state comes from
        // the sky runs; frame shapes match the shared loop.
        auto& driver=*s.runtime.engineDriver;auto& run=s.skyMaterial;
        if(immediate&&site==0x8270142C) {
            need(mesh.phase==3&&mesh.cursor==mesh.draws.size()&&!mesh.auxiliaryCleared&&
                 !run.active&&!run.committed&&c.r3.u32==mesh.context&&c.r4.u32==1&&
                 !c.r5.u32&&!c.r6.u32&&!c.r7.u32&&c.r8.u32==1,"Sky immediate auxiliary-stream cleanup differs");
            s.backend.clearZPrepassAuxiliaryStream();mesh.auxiliaryCleared=true;c.lr=site+4;return;
        }
        if(site==(immediate?0x82701438u:0x82701628u)) {
            need(mesh.phase==3&&mesh.cursor==mesh.draws.size()&&!run.active&&!run.committed&&(!immediate||mesh.auxiliaryCleared),
                 "Original sky loop ended before its material/draw closure");
            if(immediate) {const auto objectGeometry=PPC_LOAD_U32(mesh.object+0x18),offset=PPC_LOAD_U32(0x82D6CAB0);
            need(uint64_t(objectGeometry)+offset+4<=UINT32_MAX,"Sky cleanup morph offset overflows");
            s.runtime.pointer(objectGeometry+offset,4,false);
            need(!PPC_LOAD_U32(objectGeometry+offset),"Sky original morph cleanup branch changed");}
            mesh.active=false;mesh.complete=true;return;
        }
        need(site==(immediate?0x827013B0u:0x827015C0u)&&mesh.phase==3&&mesh.cursor<mesh.draws.size()&&
             run.committed&&!run.active&&(!immediate||mesh.submeshUpdated),
             "Sky draw has no completed material commit");
        const auto& row=mesh.draws[mesh.cursor];
        {static thread_local uint32_t skyDrawSample{};
        if(sampleHotLog(skyDrawSample))
            std::fprintf(stderr,"[NATIVE SKY DRAW] submesh=%08X material=%08X flags=%04X words3..6=%08X %08X %08X %08X\n",
                row.entry,row.material,row.flags,row.words[3],row.words[4],row.words[5],row.words[6]);}
        need(c.r31.u32==row.entry&&c.r3.u32==mesh.context&&c.r4.u32==row.words[3]&&c.r5.u32==row.words[4]&&
             c.r6.u32==row.words[5]&&c.r7.u32==row.words[6]&&run.material==row.material&&
             PPC_LOAD_U16(row.materialHeader)==row.flags,"Original sky draw arguments/material differ");
        for(uint32_t j=0;j<9;++j)need(PPC_LOAD_U32(row.entry+4*j)==row.words[j],"Sky submesh changed before draw");
        const auto& effective=driver.effectiveState();const auto camera=driver.cameraBinding();
        need(camera.camera==s.rigidCamera,"Sky draw camera owner changed");
        using S=Graphics::ScalarState;Graphics::SkyMeshDraw draw{};
        draw.primitiveType=c.r4.u32;draw.baseVertex=c.r5.s32;draw.startIndex=c.r6.u32;draw.indexCount=c.r7.u32;draw.viewport=camera.viewport;
        if(const auto scissor=s.backend.scissor())draw.scissor=*scissor;
        draw.depthEnable=effective.scalar(S::DepthEnable);draw.depthWrite=effective.scalar(S::DepthWrite);draw.depthCompare=effective.scalar(S::DepthCompare);
        draw.cull=effective.scalar(S::Cull);draw.fill=effective.scalar(S::Fill);draw.colorMask=effective.scalar(S::ColorMask0);
        draw.stencilEnable=effective.scalar(S::StencilEnable);draw.alphaTest=effective.scalar(S::AlphaTest);
        draw.scissorEnable=effective.scalar(S::ScissorEnable);draw.halfPixelOffset=effective.scalar(S::HalfPixelOffset);
        draw.primitiveReset=effective.scalar(S::PrimitiveResetEnable);draw.primitiveResetIndex=effective.scalar(S::PrimitiveResetIndex);
        draw.viewportEnable=effective.scalar(S::ViewportEnable);draw.clipPlaneEnable=effective.scalar(S::ClipPlaneEnable);
        draw.multisampleAntialias=effective.scalar(S::MultisampleAntialias);draw.multisampleMask=effective.scalar(S::MultisampleMask);
        draw.alphaToMask=effective.scalar(S::AlphaToMask);draw.depthBiasBits=effective.scalar(S::DepthBias);draw.slopeBiasBits=effective.scalar(S::SlopeBias);
        draw.depthPolicy=Graphics::ShadowMeshDepthPolicy::Reference20e4Rne;
        draw.blendEnable=effective.scalar(S::BlendEnable);draw.blendWord=effective.effectiveBlend(0);draw.expandedBlend=effective.scalar(S::ExpandedBlend0);
        need(!effective.scalar(S::TessellationMode),"Sky tessellation is unqualified");
        // Sampler states come from the 24 metadata rows applied at begin;
        // validate the six pinned values per stage, then build the descriptors
        // stages 0-2 sample tiled layers linear/wrap, stage 3 the line layer
        // point/clamp.
        constexpr uint32_t sdkIds[]={0,4,8,0x10,0x14,0x18};
        constexpr uint32_t baseValues[]={0,0,0,1,1,1},lineValues[]={2,2,2,0,0,2};
        for(uint32_t stage=0;stage<4;++stage)for(uint32_t i=0;i<6;++i)
            need(effective.sampler(stage,sdkIds[i])==(stage<3?baseValues[i]:lineValues[i]),
                 "Sky inherited sampler state is unqualified");
        for(uint32_t stage=0;stage<4;++stage) {
            auto& desc=draw.samplers[stage];
            desc.Filter=stage<3?D3D11_FILTER_MIN_MAG_MIP_LINEAR:D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
            desc.AddressU=desc.AddressV=desc.AddressW=stage<3?D3D11_TEXTURE_ADDRESS_WRAP:D3D11_TEXTURE_ADDRESS_CLAMP;
            desc.MaxAnisotropy=1;desc.ComparisonFunc=D3D11_COMPARISON_NEVER;desc.MinLOD=0;desc.MaxLOD=13;
        }
        for(uint32_t stage=0;stage<3;++stage) {
            need(bool(record.skyTexture[stage]),"Sky draw is missing a committed layer texture");
            draw.textures[stage]=record.skyTexture[stage];
        }
        if(record.skyLineTarget) {
            need(record.skyLineTargetId,"Sky draw scene-copy line layer is missing its retained identity");
            draw.lineTarget=record.skyLineTarget;
        } else {
            need(bool(record.skyTexture[3]),"Sky draw is missing a committed line texture");
            draw.textures[3]=record.skyTexture[3];
        }
        bool alphaOne=false;const auto color=driver.color(camera.colorIdentity,alphaOne);need(!alphaOne,"Sky color owner requires unsupported alpha synthesis");
        if(immediate)s.backend.drawSkyMesh(color,driver.depth(camera.depthIdentity),mesh.native,*s.rigidVertex,*s.rigidPixel,
            run.commit,draw);
        else s.backend.recordSkyMesh(payload,color,driver.depth(camera.depthIdentity),mesh.native,*s.rigidVertex,*s.rigidPixel,
            record.skyMaterialVS,record.skyMaterialPS,mesh.replayValues,draw);
        ++mesh.cursor;run.committed=false;mesh.submeshUpdated=false;
        {static thread_local uint32_t skyDrawCompleteSample{};
        if(sampleHotLog(skyDrawCompleteSample))
            std::fprintf(stderr,"[NATIVE SKY %s DRAW] geometry=%08X submesh=%08X indices=%u completed=%u/%zu depth=%u write=%u compare=%u viewport_z=%08X/%08X bias=%08X/%08X\n",
                immediate?"IMMEDIATE":"RECORD",geometry,row.entry,draw.indexCount,mesh.cursor,mesh.draws.size(),
                draw.depthEnable,draw.depthWrite,draw.depthCompare,draw.viewport[4],draw.viewport[5],draw.depthBiasBits,draw.slopeBiasBits);}
        c.lr=site+4;return;
    }
    need(c.r1.u32==mesh.frame-0xA0&&PPC_LOAD_U32(c.r1.u32)==mesh.frame&&c.r27.u32==mesh.geometry&&
         c.r3.u32==mesh.context,"Sky deferred binding frame/context differs");
    if(site==0x826FF340) {
        need(!mesh.phase&&!c.r4.u32&&c.r5.u32==geometry+0x38&&!c.r6.u32&&c.r7.u32==PPC_LOAD_U32(geometry+4)&&c.r8.u32==1,
             "Original sky vertex-stream arguments differ");
        const auto capture=[&](uint32_t address,uint32_t bytes,size_t maxBytes=64*1024*1024){need(bytes&&bytes<=maxBytes,"Sky source byte extent is unqualified");
            const auto* p=s.runtime.pointer(address,bytes,false);return std::vector<uint8_t>(p,p+bytes);};
        const auto count=PPC_LOAD_U32(geometry+8);need(count>=2&&count<=65,"Sky declaration table extent is unqualified");
        auto header=capture(geometry,0x78);
        const auto vertices=capture(PPC_LOAD_U32(geometry+0x10),PPC_LOAD_U32(geometry));
        const auto indices=capture(PPC_LOAD_U32(geometry+0x1C),PPC_LOAD_U32(geometry+0x14),originalGeometryMaxIndexBytes);
        const auto elements=capture(PPC_LOAD_U32(geometry+0xC),12*count);
        const auto decoded=decodeSkyVertices(vertices,elements,c.r7.u32);
        const auto decodedIndices=decodeCharacterIndices(indices);
        const auto cache=PPC_LOAD_U32(geometry+0x30);s.runtime.pointer(cache,12,false);
        const auto declaration=PPC_LOAD_U32(cache+4);
        need(PPC_LOAD_U32(geometry+0x18)==1&&(PPC_LOAD_U32(geometry+0x50)&~3u)==PPC_LOAD_U32(geometry+0x10)&&
             (PPC_LOAD_U32(geometry+0x54)&3)==2&&(PPC_LOAD_U32(geometry+0x58)&0xE0000003)==0x20000002&&
             PPC_LOAD_U32(geometry+0x70)==PPC_LOAD_U32(geometry+0x1C)&&PPC_LOAD_U32(geometry+0x74)==indices.size()&&
             declaration&&c.r23.u32==declaration,"Original sky source/declaration headers differ");
        {static thread_local uint32_t skyVerticesSample{};
        if(sampleHotLog(skyVerticesSample))
            std::fprintf(stderr,"[NATIVE SKY VERTICES] geometry=%08X verts=%zu stride=%u\n",geometry,decoded.size(),c.r7.u32);}
        auto native=s.backend.uploadSkyMesh(decoded,decodedIndices);
        if(immediate)s.backend.bindSkyMeshVertices(native);else s.backend.bindSkyMeshVertices(payload,native);
        mesh.header=std::move(header);mesh.cache=cache;mesh.declaration=declaration;mesh.native=std::move(native);mesh.phase=1;
    } else if(site==0x826FF498) {
        need(mesh.phase==1&&c.r4.u32==mesh.declaration,"Sky declaration binding is out of order");
        if(immediate)s.backend.bindSkyMeshDeclaration(mesh.native);else s.backend.bindSkyMeshDeclaration(payload,mesh.native);mesh.phase=2;
    } else {
        need(site==0x826FF4A4&&mesh.phase==2&&c.r4.u32==geometry+0x58,"Sky index binding is out of order");
        if(immediate)s.backend.bindSkyMeshIndices(mesh.native);else s.backend.bindSkyMeshIndices(payload,mesh.native);mesh.phase=3;
    }
    c.lr=site+4;
}
void EngineEffects::skyImmediateOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    // Sky shares rigid's immediate loop and fallback but carries its own mesh
    // state. Submesh update shapes are probed here before pinning.
    auto& s=*state;s.require(base);auto& run=s.rigidImmediate;auto& mesh=s.skyMesh;
    need(currentContext==&c,"Sky immediate work has no original CPU frame");
    need(run.active&&run.cpu==&c&&run.activated,"Sky immediate continuation has no activated lifetime");
    requireRigidSelection(run.typed);
    need(!s.find(s.rigidId).recordingContext,"Sky immediate continuation acquired a recording lease");
    if(site==0x82700498) {
        if(!(c.lastFunction==site&&uint32_t(c.lr)==0x82701310&&c.r1.u32==mesh.frame&&mesh.active&&mesh.immediate&&
             mesh.cursor<mesh.draws.size()&&!run.submeshActive&&!mesh.submeshUpdated&&c.r3.u32==run.typed&&
             c.r4.u32==mesh.metadata&&c.r5.u32==mesh.object))
            std::fprintf(stderr,"[NATIVE SKY SUBMESH] fn=%08X lr=%08X r1=%08X frame=%08X active=%u imm=%u cursor=%u/%zu subA=%u subU=%u r3=%08X typed=%08X r4=%08X meta=%08X r5=%08X obj=%08X r6=%08X r7=%08X\n",
                c.lastFunction,uint32_t(c.lr),c.r1.u32,mesh.frame,mesh.active,mesh.immediate,
                mesh.cursor,mesh.draws.size(),run.submeshActive,mesh.submeshUpdated,c.r3.u32,run.typed,
                c.r4.u32,mesh.metadata,c.r5.u32,mesh.object,c.r6.u32,c.r7.u32);
        need(c.lastFunction==site&&uint32_t(c.lr)==0x82701310&&c.r1.u32==mesh.frame&&mesh.active&&mesh.immediate&&
             mesh.cursor<mesh.draws.size()&&!run.submeshActive&&!mesh.submeshUpdated&&c.r3.u32==run.typed&&
             c.r4.u32==mesh.metadata&&c.r5.u32==mesh.object,"Sky immediate submesh update entry differs");
        const auto& row=mesh.draws[mesh.cursor];
        need(c.r6.u32==row.words[1]&&c.r7.u32==row.words[0],"Sky immediate submesh material selection differs");
        run.submeshActive=true;return;
    }
    if(site==0x827005DC) {
        if(!(run.submeshActive&&c.r1.u32==mesh.frame-0xE0&&PPC_LOAD_U32(c.r1.u32)==mesh.frame&&
             PPC_LOAD_U32(c.r1.u32+0xD8)==0x82701310&&c.r28.u32==run.typed&&
             c.r25.u32==mesh.draws[mesh.cursor].material))
            std::fprintf(stderr,"[NATIVE SKY SUBMESH RETURN] active=%u r1=%08X frame=%08X star=%08X starD8=%08X r28=%08X typed=%08X r25=%08X material=%08X\n",
                run.submeshActive,c.r1.u32,mesh.frame,PPC_LOAD_U32(c.r1.u32),PPC_LOAD_U32(c.r1.u32+0xD8),
                c.r28.u32,run.typed,c.r25.u32,mesh.draws[mesh.cursor].material);
        need(run.submeshActive&&c.r1.u32==mesh.frame-0xE0&&PPC_LOAD_U32(c.r1.u32)==mesh.frame&&
             PPC_LOAD_U32(c.r1.u32+0xD8)==0x82701310&&c.r28.u32==run.typed&&
             c.r25.u32==mesh.draws[mesh.cursor].material,"Sky immediate submesh update did not return through its original frame");
        run.submeshActive=false;mesh.submeshUpdated=true;return;
    }
    if(site==0x827402E4) {
        auto& material=s.skyMaterial;
        need(c.r1.u32==run.frame&&PPC_LOAD_U32(run.frame)==run.entrySP&&
             PPC_LOAD_U32(run.frame+0x78)==0x82740B28&&c.r31.u32==run.packet&&c.r30.u32==uint32_t(run.alpha)&&
             mesh.immediate&&mesh.complete&&!mesh.active&&mesh.auxiliaryCleared&&mesh.cursor==mesh.draws.size()&&
             !run.submeshActive&&!material.active&&!material.committed&&
             run.staging.complete&&run.staging.phase==4&&s.rigidTextures.complete&&!s.rigidTextures.active,
             "Sky immediate fallback ended before original staging, draws and cleanup completed");
        need(s.backend.skyMeshDrawCount()==mesh.drawsBefore+mesh.cursor,"Sky immediate native draw receipt differs");
        {static thread_local uint32_t skyImmediateCompleteSample{};
        if(sampleHotLog(skyImmediateCompleteSample))
            std::fprintf(stderr,"[NATIVE SKY IMMEDIATE COMPLETE] packet=%08X geometry=%08X draws=%u; original fallback and stream cleanup completed\n",
                run.packet,mesh.geometry,mesh.cursor);}
        run.active=false;material.commit.reset();return;
    }
    std::fprintf(stderr,"[NATIVE SKY SITE] site=%08X lr=%08X\n",site,uint32_t(c.lr));
    throw Failure("Sky immediate site is not yet qualified");
}
void EngineEffects::rigidMeshOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    if(site==0x82701220||site==0x82701448)observeProducerEntry(c,base,"rigid_mesh_entry");
    auto& s=*state;s.require(base);
    if(s.monoImmediate.active){monoImmediateMeshOperation(c,base,site);return;}
    if(s.monoRecordingPath&&s.monoId){monoRecordingOperation(c,base,site);return;}
    if(s.skinImmediate.active){skinMeshOperation(c,base,site);return;}
    if(s.rigidId&&s.find(s.rigidId).view.source==0x82036448){skyMeshOperation(c,base,site);return;}
    requireRigidSelection(s.rigidTyped);
    auto& driver=*s.runtime.engineDriver;auto& record=s.find(s.rigidId);auto& mesh=s.rigidMesh;
    const bool immediate=s.rigidImmediate.active;
    need(currentContext==&c&&s.rigidTextures.complete&&!s.rigidTextures.active&&s.rigidTextures.immediate==immediate&&
         record.contextIdentity==s.rigidTextures.context,"Rigid mesh has no completed texture owner");
    std::shared_ptr<Graphics::NativeRecordingPayload> payload;
    if(immediate)need(!record.recordingContext&&s.rigidImmediate.cpu==&c&&s.rigidImmediate.activated&&
        s.rigidImmediate.staging.complete,"Rigid immediate mesh has no completed original staging");
    else {need(bool(record.recordingContext),"Rigid deferred mesh has no recording lease");
        payload=driver.recordingOwners().activePayload(record.contextIdentity);}
    if(site==(immediate?0x82701220u:0x82701448u)) {
        // The entry builds a fresh run and overwrites the mesh below, so a
        // stale completion flag from an earlier consumed loop is discarded;
        // only a live loop (!active) overlaps. (Recording entries have no
        // interleaving fallback-entry reset; reach-game-241 site 82701448.)
        if(!(c.lastFunction==site&&uint32_t(c.lr)==(immediate?0x827402B0u:0x82740624u)&&!mesh.active&&
             c.r1.u32==s.rigidTextures.sp&&PPC_LOAD_U32(c.r1.u32)==c.r1.u32+(immediate?0x80u:0xD0u)&&c.r5.u32==s.rigidTyped&&
             !c.r6.u32&&c.r7.u32==PPC_LOAD_U32(c.r3.u32+0x10)&&(immediate?c.r8.u32:c.r9.u32)==PPC_LOAD_U8(c.r31.u32+0xC))) {
            auto peek=[&](uint32_t a)->uint32_t { try { s.runtime.pointer(a,4,false); } catch(...) { return 0xDEADDEADu; } return PPC_LOAD_U32(a); };
            std::fprintf(stderr,"[NATIVE RIGID MESHENTRY] site=%08X fn=%08X lr=%08X meshA=%u meshC=%u r1=%08X sp=%08X star=%08X want=%08X r5=%08X typed=%08X r6=%08X r7=%08X c3p10=%08X r89=%08X pktC=%02X imm=%u\n",
                site,c.lastFunction,uint32_t(c.lr),mesh.active,mesh.complete,c.r1.u32,s.rigidTextures.sp,
                peek(c.r1.u32),c.r1.u32+(immediate?0x80u:0xD0u),c.r5.u32,s.rigidTyped,c.r6.u32,c.r7.u32,
                peek(c.r3.u32+0x10),immediate?c.r8.u32:c.r9.u32,peek(c.r31.u32+0xC)&0xFF,immediate);
        }
        need(c.lastFunction==site&&uint32_t(c.lr)==(immediate?0x827402B0u:0x82740624u)&&!mesh.active&&
             c.r1.u32==s.rigidTextures.sp&&PPC_LOAD_U32(c.r1.u32)==c.r1.u32+(immediate?0x80u:0xD0u)&&c.r5.u32==s.rigidTyped&&
             !c.r6.u32&&c.r7.u32==PPC_LOAD_U32(c.r3.u32+0x10)&&(immediate?c.r8.u32:c.r9.u32)==PPC_LOAD_U8(c.r31.u32+0xC),
             "Unqualified original rigid mesh-loop entry");
        const auto packet=c.r31.u32,meta=c.r3.u32,object=c.r4.u32;
        s.runtime.pointer(meta,0x38,false);s.runtime.pointer(object,0x40,false);
        need(PPC_LOAD_U32(packet)==meta&&PPC_LOAD_U32(packet+4)==object&&PPC_LOAD_U32(packet+0x18)==s.rigidTyped&&
             !PPC_LOAD_U32(meta+0x24)&&PPC_LOAD_U32(0x82D6CCA8)==(isVfxRigidSource(record.view.source)?0x20u:0u),
             "Original rigid mesh packet/static qualification differs");
        const auto geometry=PPC_LOAD_U32(meta+0xC),objectGeometry=PPC_LOAD_U32(object+0x18);
        s.runtime.pointer(geometry,0x78,false);s.runtime.pointer(objectGeometry,0x28,false);
        const auto dynamicOffset=PPC_LOAD_U32(0x82CF05EC),morphOffset=PPC_LOAD_U32(0x82D6CAB0);
        need(uint64_t(object)+dynamicOffset<=UINT32_MAX&&uint64_t(objectGeometry)+morphOffset<=UINT32_MAX,
             "Rigid geometry extension offset overflows");
        s.runtime.pointer(object+dynamicOffset,4,false);s.runtime.pointer(objectGeometry+morphOffset,4,false);
        need(!(PPC_LOAD_U32(object+dynamicOffset)&&(PPC_LOAD_U32(objectGeometry+8)&0x04000000))&&
             !PPC_LOAD_U32(objectGeometry+morphOffset)&&!PPC_LOAD_U32(geometry+0x34),
             "Rigid dynamic, morph or external stream is not qualified");
        State::RigidMeshRun next;next.cpu=&c;next.entrySP=c.r1.u32;next.frame=c.r1.u32-(immediate?0xF0u:0xE0u);next.immediate=immediate;
        next.packet=packet;next.metadata=meta;next.object=object;next.geometry=geometry;next.context=record.contextIdentity;next.payload=payload;
        // activePayload checked the completed original begin and O+54 publication.
        if(!immediate) {
            next.payloadId=PPC_LOAD_U32(s.rigidTextures.manager+0x54);
            need(next.payloadId&&!s.rigidPayloads.contains(next.payloadId),"Rigid recording payload is not fresh");
            for(const auto& [id,retained]:s.rigidPayloads)
                need(retained.native!=payload,"Rigid recording reuses a retained native payload");
        }
        const auto count=PPC_LOAD_U32(meta+0x10),entries=PPC_LOAD_U32(meta+0x14),collection=PPC_LOAD_U32(meta+0x34);
        rigidSubmeshExtent(s.runtime,entries,count);
        if(count) {
            s.runtime.pointer(collection,12,false);
            const auto materialCount=PPC_LOAD_U32(collection),offsets=PPC_LOAD_U32(objectGeometry+0x24),headers=PPC_LOAD_U32(0x82D6D814);
            need(materialCount<=65535,"Rigid material collection extent is unqualified");s.runtime.pointer(collection,12+4*materialCount,false);
            // Native row prevalidation receipt (observer only). It copies the raw nine
            // words into the audit before any offset, header or collection read, and
            // validates nothing. Rows sharing every consumed field share one stable
            // group, so it fires only when that group changes: the row checks below
            // depend only on those fields, hence a failing row always differs from its
            // predecessor and its snapshot is the latest attempted row.
            const bool auditRows=s.runtime.resourceAudit.active();
            std::optional<EngineAudioOwners::AllocationSpan> rowOwner;
            if(auditRows)try{rowOwner=s.runtime.engineAudio?s.runtime.engineAudio->allocationSpan(entries):std::nullopt;}catch(...){}
            const auto bones=PPC_LOAD_U32(meta+0x24),variant=immediate?c.r8.u32:c.r9.u32;
            const auto observeRow=[&](uint32_t ordinal,const State::RigidMeshRun::Submesh& row) noexcept {
                HostState preserve;
                try {
                    char asset[40],parameters[640],ownership[256],instance[768];
                    std::snprintf(asset,sizeof(asset),"source:%08X",record.view.source);
                    std::snprintf(parameters,sizeof(parameters),
                        "boundary=rigid_row_prevalidation source=%08X requested_technique=00000000 submesh_count=%u bones=%u collection_count=%u variant=%u vfx=%u rw_selector=%08X compiled_index=%u primitive=%u base_vertex=%d start_index=%u index_count=%u authored_group_count=%u header_state=not-read-before-admission",
                        record.view.source,count,bones,materialCount,variant,unsigned(isVfxRigidSource(record.view.source)),
                        row.words[0],row.words[1],row.words[3],int32_t(row.words[4]),row.words[5],row.words[6],row.words[7]);
                    std::snprintf(ownership,sizeof(ownership),
                        "admission=unvalidated runtime_match=%u thread_match=%u cpu_match=%u submesh_owner=%s consumed=w0,w1,w3,w4,w5,w6 authored=w7 unclassified=w2,w8",
                        unsigned(active==&s.runtime),unsigned(GetCurrentThreadId()==s.thread),unsigned(currentContext==&c),
                        rowOwner?"observed-live":"unobserved");
                    std::snprintf(instance,sizeof(instance),
                        "row_ordinal=%u row_address=%08X row_offset=%llu words=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X metadata=%08X table=%08X collection=%08X r3=%08X r4=%08X r5=%08X r7=%08X submesh_owner_base=%08X submesh_owner_extent=%u submesh_owner_generation=%llu",
                        ordinal,row.entry,36ull*ordinal,row.words[0],row.words[1],row.words[2],row.words[3],row.words[4],row.words[5],row.words[6],row.words[7],row.words[8],
                        meta,entries,collection,c.r3.u32,c.r4.u32,c.r5.u32,c.r7.u32,
                        rowOwner?rowOwner->address:0,rowOwner?uint32_t(rowOwner->extent):0u,static_cast<unsigned long long>(rowOwner?rowOwner->generation:0));
                    s.runtime.resourceAudit.observe("effect_producer_row",asset,immediate?0x827402B0u:0x82740624u,parameters,ownership,
                        s.runtime.nativeDepthCopyCount.load(),instance);
                }catch(...){std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] rigid row prevalidation capture failed\n");}
            };
            // The cache is invalidated whenever the audit mission/action changes, since both are
            // part of every stable group. A failing row whose key was already cached is still
            // published by the handler below, so its snapshot is always the latest attempted row.
            if(auditRows&&s.rigidRowEpoch!=s.runtime.resourceAudit.epoch()){s.rigidRowKeys.clear();s.rigidRowEpoch=s.runtime.resourceAudit.epoch();}
            std::optional<RigidRowKey> previousKey;
            State::RigidMeshRun::Submesh attempted;uint32_t attemptedOrdinal=0;bool attemptedPublished=true;
            try {
            for(uint32_t i=0;i<count;++i) {
                State::RigidMeshRun::Submesh row;row.entry=entries+36*i;
                for(uint32_t j=0;j<9;++j)row.words[j]=PPC_LOAD_U32(row.entry+4*j);
                if(auditRows) {
                    attempted=row;attemptedOrdinal=i;attemptedPublished=false;
                    const RigidRowKey key{immediate,record.view.source,count,bones,materialCount,variant,unsigned(bool(rowOwner)),
                        row.words[0],row.words[1],row.words[3],row.words[4],row.words[5],row.words[6],row.words[7]};
                    if((!previousKey||*previousKey!=key)&&s.rigidRowKeys.insert(key).second){observeRow(i,row);attemptedPublished=true;}
                    previousKey=key;
                }
                need(uint64_t(offsets)+4ull*row.words[0]+4<=UINT32_MAX,"Rigid material offset table overflows");
                s.runtime.pointer(offsets+4*row.words[0],4,false);row.materialOffset=PPC_LOAD_U32(offsets+4*row.words[0]);
                need(uint64_t(headers)+row.materialOffset+4<=UINT32_MAX,"Rigid material header overflows");
                row.materialHeader=headers+row.materialOffset;s.runtime.pointer(row.materialHeader,4,false);
                row.flags=PPC_LOAD_U16(row.materialHeader);
                if(row.flags&0x20)continue; // The actual original branch still performs this skip.
                const auto special=PPC_LOAD_U32(0x82D5DA78);
                if((row.flags&4)&&special){s.runtime.pointer(special+0x1C,1,false);
                    need(!PPC_LOAD_U8(special+0x1C),"Rigid special sample-mask branch is not qualified");}
                need(row.words[1]<materialCount,"Rigid submesh material index is out of range");
                row.material=PPC_LOAD_U32(collection+12+4*row.words[1]);s.runtime.pointer(row.material,0x18,false);
                next.draws.push_back(row);
            }
            } catch(...) {
                if(auditRows&&!attemptedPublished)observeRow(attemptedOrdinal,attempted);
                throw;
            }
        }
        // Reflection owns inherited registers. Material commits must never seed
        // these from private defaults or the shared pool in the recorded list.
        // Sky reflection owns TimeTicker (leaf 19) and the sampler/uv
        // registers (g_LineSampler leaf 24, g_texCoords leaf 25); these are
        // the only mask exclusions in all 128 mask bytes. (Sky dispatches to
        // skyMeshOperation before reaching here; chocolate flows through.)
        // Chocolate reflection owns the light/spec/object/alpha-test leaves
        // (8,13,14,15,16) plus reserved leaves 5,11,12; these are its only
        // mask exclusions. g_World (leaf 2) stays material-eligible here.
        if(isVfxRigidSource(record.view.source)) {
            // Its inherited registers come from this pass's own four original
            // callbacks. Check its reflected exclusions, not the lit-rigid set.
            const auto& reflected=s.cachedReflection(record,true,bool(s.runtime.effectPoolBacking));
            need(reflected.classified.size()==4,"VFX rigid callback classification changed");
        } else if(record.view.source==0x82036448)
            for(const auto leaf:{19u,24u,25u})
                need(!(record.privateMask[leaf/8]&(0x80>>(leaf&7))),"Sky inherited private mask remains material-eligible");
        else if(record.view.source==0x82039208)
            // Flipbook keeps animation c47 material-owned and ticker18
            // reflected. Neither its unused alpha-test nor shadow leaves
            // should be borrowed from a different selected context.
            for(const auto leaf:{2u,8u,9u,10u,11u,12u,15u,18u})
                need(!(record.privateMask[leaf/8]&(0x80>>(leaf&7))),"Flipbook inherited private mask remains material-eligible");
        else if(record.view.source==0x82042F58)
            // Real reflected callbacks own world/light/object and TimeTicker22.
            for(const auto leaf:{2u,8u,9u,10u,11u,12u,15u,22u})
                need(!(record.privateMask[leaf/8]&(0x80>>(leaf&7))),"Single UV inherited private mask remains material-eligible");
        else if(record.view.source==0x82051808)
            for(const auto leaf:{2u,8u,9u,10u,11u,12u,15u,19u})
                need(!(record.privateMask[leaf/8]&(0x80>>(leaf&7))),"Projected rigid inherited private mask remains material-eligible");
        else if(record.view.source==0x8205D2D8||record.view.source==0x820465E8)
            // Chocolate reflection owns the rigid inherited set plus
            // TimeTicker (leaf 23): mask bytes read 0xDF/0x06/0xFE, excluding
            // leaves 2,8,9,10,11,12,15,23 across all 128 mask bytes.
            for(const auto leaf:{2u,8u,9u,10u,11u,12u,15u,23u})
                need(!(record.privateMask[leaf/8]&(0x80>>(leaf&7))),"Chocolate inherited private mask remains material-eligible");
        else for(const auto leaf:{2u,8u,9u,10u,11u,12u,15u})
            need(!(record.privateMask[leaf/8]&(0x80>>(leaf&7))),"Rigid inherited private mask remains material-eligible");
        // Each sealed payload keeps its own live owner. Zero initialization is
        // never executable: prepareRigidReplay requires both original uploads.
        if(!immediate)next.replayValues=s.backend.createRigidReplayConstants({},{});
        next.active=true;mesh=std::move(next);return;
    }
    need(mesh.active&&mesh.immediate==immediate&&mesh.cpu==&c&&mesh.payload==payload&&mesh.context==record.contextIdentity&&
         PPC_LOAD_U32(mesh.metadata+0xC)==mesh.geometry,"Rigid recording mesh lifetime changed");
    const bool binding=site==0x826FF340||site==0x826FF498||site==0x826FF4A4;
    if(binding)need(c.r1.u32==mesh.frame-0xA0&&PPC_LOAD_U32(c.r1.u32)==mesh.frame&&c.r27.u32==mesh.geometry&&
                   c.r3.u32==mesh.context,"Rigid deferred binding frame/context differs");
    else need(c.r1.u32==mesh.frame&&PPC_LOAD_U32(mesh.frame)==mesh.entrySP&&
              PPC_LOAD_U32(mesh.frame+(immediate?0xE8u:0xD8u))==(immediate?0x827402B0u:0x82740624u)&&
              (immediate?c.r19.u32:c.r17.u32)==s.rigidTyped&&(immediate?c.r22.u32:c.r20.u32)==mesh.object&&
              (immediate?c.r25.u32:c.r23.u32)==mesh.metadata,
              "Original rigid submesh frame differs");
    const auto geometry=mesh.geometry;
    // Non-attributing receipt for one capture attempt. It is called only inside
    // containedRigidCapture, so any formatting/string/audit failure is contained.
    const auto reportCapture=[&](const RigidCaptureIdentity& id,const RigidCaptureOutcome& o,const char* ownership) {
        const char* status=o.status==RigidCaptureStatus::Complete?"complete":
            o.status==RigidCaptureStatus::SkippedBudget?"skipped":"failed";
        if(o.status!=RigidCaptureStatus::Complete) // Never silent, with or without an audit file.
            std::fprintf(stderr,"[RIGID CAPTURE DIAGNOSTIC] phase=%s status=%s reason_code=%s step=%s source=%08X caller=%08X count=%u bytes=%u budget=%u: %s\n",
                id.phase,status,o.reasonCode,o.step,id.source,id.caller,id.count,id.bytes,rigidCaptureBudgetBytes,o.reason);
        if(!s.runtime.resourceAudit.active())return;
        char asset[40],parameters[256],instance[512];
        std::snprintf(asset,sizeof(asset),"source:%08X",id.source);
        // Row fields describe only raw table copies; draw metadata reads none.
        if(std::strcmp(id.phase,"first_draw")==0)
            std::snprintf(parameters,sizeof(parameters),"phase=%s status=%s reason_code=%s capture_step=%s",
                id.phase,status,o.reasonCode,o.step);
        else
            std::snprintf(parameters,sizeof(parameters),"phase=%s status=%s reason_code=%s capture_step=%s submesh_count=%u submesh_bytes=%u budget_bytes=%u",
                id.phase,status,o.reasonCode,o.step,id.count,id.bytes,rigidCaptureBudgetBytes);
        std::snprintf(instance,sizeof(instance),
            "typed=%08X metadata=%08X object=%08X geometry=%08X payload=%08X submeshes=%08X submesh_owner_base=%08X submesh_owner_extent=%u submesh_owner_generation=%llu",
            id.typed,id.metadata,id.object,id.geometry,id.payload,id.entries,id.ownerBase,id.ownerExtent,
            static_cast<unsigned long long>(id.ownerGeneration));
        std::string detail(instance);detail+=" capture_directory=";detail+=s.runtime.frameCaptureDirectory.string();
        s.runtime.resourceAudit.diagnostic("rigid_capture",asset,id.caller,parameters,ownership,id.scene,detail,o.reason);
    };
    if(site==0x826FF340) {
        need(!mesh.phase&&!c.r4.u32&&c.r5.u32==geometry+0x38&&!c.r6.u32&&c.r7.u32==PPC_LOAD_U32(geometry+4)&&c.r8.u32==1,
             "Original rigid vertex-stream arguments differ");
        const auto capture=[&](uint32_t address,uint32_t bytes,size_t maxBytes=64*1024*1024){need(bytes&&bytes<=maxBytes,"Rigid source byte extent is unqualified");
            const auto* p=s.runtime.pointer(address,bytes,false);return std::vector<uint8_t>(p,p+bytes);};
        const auto count=PPC_LOAD_U32(geometry+8);need(count>=2&&count<=65,"Rigid declaration extent is unqualified");
        auto header=capture(geometry,0x78);
        // Exact-content lookup keyed by the three guest source planes plus every
        // input the decoder consumes. A repeat of the same guest ranges skips the
        // comparison only while the exact write barrier proves no page of them was
        // stored to (bounded re-verification); a hit skips copy, decode and upload.
        // The first raw capture needs the copies, so it always takes the full path.
        const bool rigidSourceCached=s.runtime.frameCaptureDirectory.empty()||s.rigidFirstMeshDone;
        const bool rigidVariantChocolate=record.view.source==0x8205D2D8;
        const bool rigidVariantTangent=(rigidProfile(record.view.source).normalmap&&!s.alphaRigid())||rigidVariantChocolate;
        const bool rigidVariantUv1=record.view.source==0x8202AD78||(record.view.source==0x820465E8&&!s.alphaRigid())||(rigidProfile(record.view.source).gloss&&!s.alphaRigid())||
            rigidProfile(record.view.source).multitone||rigidProfile(record.view.source).normalmap||rigidVariantChocolate;
        const uint32_t rigidVariant=c.r7.u32|(rigidVariantUv1?0x10000u:0u)|(rigidVariantTangent?0x20000u:0u)|(isVfxRigidSource(record.view.source)?0x40000u:0u);
        StaticMeshSourceCache<Graphics::NativeRigidMesh>::LookupResult rigidSourceLookup;
        if(rigidSourceCached&&c.r7.u32<0x10000) {
            const auto borrow=[&](uint32_t address,uint32_t bytes,size_t maxBytes){need(bytes&&bytes<=maxBytes,"Rigid source byte extent is unqualified");
                return std::span<const uint8_t>(s.runtime.pointer(address,bytes,false),bytes);};
            const auto vertexView=borrow(PPC_LOAD_U32(geometry+0x10),PPC_LOAD_U32(geometry),64*1024*1024);
            const auto indexView=borrow(PPC_LOAD_U32(geometry+0x1C),PPC_LOAD_U32(geometry+0x14),originalGeometryMaxIndexBytes);
            const auto elementView=borrow(PPC_LOAD_U32(geometry+0xC),12*count,64*1024*1024);
            rigidSourceLookup=s.rigidSourceCache.lookup(vertexView,indexView,elementView,rigidVariant);
            if(auto hit=rigidSourceLookup.mesh) {
                const auto cache=PPC_LOAD_U32(geometry+0x30);s.runtime.pointer(cache,12,false);
                const auto declaration=PPC_LOAD_U32(cache+4);
                need(PPC_LOAD_U32(geometry+0x18)==1&&(PPC_LOAD_U32(geometry+0x50)&~3u)==PPC_LOAD_U32(geometry+0x10)&&
                     (PPC_LOAD_U32(geometry+0x54)&3)==2&&(PPC_LOAD_U32(geometry+0x58)&0xE0000003)==0x20000002&&
                     PPC_LOAD_U32(geometry+0x70)==PPC_LOAD_U32(geometry+0x1C)&&PPC_LOAD_U32(geometry+0x74)==indexView.size()&&
                     declaration&&c.r23.u32==declaration,"Original rigid source/declaration headers differ");
                if(immediate)s.backend.bindRigidMeshVertices(hit);else s.backend.bindRigidMeshVertices(payload,hit);
                mesh.header=std::move(header);mesh.cache=cache;mesh.declaration=declaration;mesh.native=std::move(hit);mesh.phase=1;
                c.lr=site+4;return;
            }
        }
        const auto vertices=capture(PPC_LOAD_U32(geometry+0x10),PPC_LOAD_U32(geometry));
        const auto indices=capture(PPC_LOAD_U32(geometry+0x1C),PPC_LOAD_U32(geometry+0x14),originalGeometryMaxIndexBytes);
        const auto elements=capture(PPC_LOAD_U32(geometry+0xC),12*count);
        if(record.view.source==0x8205D2D8) {
            // Chocolate vertex declaration (pink/black frontier): which
            // streams exist and in what order (fetch-to-semantic wiring).
            {static thread_local uint32_t chocolateVerticesSample{};
            if(sampleHotLog(chocolateVerticesSample)) {
                std::fprintf(stderr,"[NATIVE CHOCOLATE VERTICES] geometry=%08X stride=%u count=%u vbytes=%u\n",
                    geometry,c.r7.u32,count,uint32_t(vertices.size()));
                for(uint32_t i=0;i<count;++i)
                    std::fprintf(stderr,"[NATIVE CHOCOLATE ELEMENT] %08X %08X %08X\n",
                        (uint32_t(elements[12*i])<<24)|(uint32_t(elements[12*i+1])<<16)|(uint32_t(elements[12*i+2])<<8)|elements[12*i+3],
                        (uint32_t(elements[12*i+4])<<24)|(uint32_t(elements[12*i+5])<<16)|(uint32_t(elements[12*i+6])<<8)|elements[12*i+7],
                        (uint32_t(elements[12*i+8])<<24)|(uint32_t(elements[12*i+9])<<16)|(uint32_t(elements[12*i+10])<<8)|elements[12*i+11]);
            }}
        }
        if(record.view.source==0x82036448) {
            std::fprintf(stderr,"[NATIVE SKY VERTICES] geometry=%08X stride=%u count=%u vbytes=%u ibytes=%u\n",
                geometry,c.r7.u32,count,uint32_t(vertices.size()),uint32_t(indices.size()));
            for(uint32_t i=0;i<count;++i)
                std::fprintf(stderr,"[NATIVE SKY ELEMENT] %08X %08X %08X\n",
                    (uint32_t(elements[12*i])<<24)|(uint32_t(elements[12*i+1])<<16)|(uint32_t(elements[12*i+2])<<8)|elements[12*i+3],
                    (uint32_t(elements[12*i+4])<<24)|(uint32_t(elements[12*i+5])<<16)|(uint32_t(elements[12*i+6])<<8)|elements[12*i+7],
                    (uint32_t(elements[12*i+8])<<24)|(uint32_t(elements[12*i+9])<<16)|(uint32_t(elements[12*i+10])<<8)|elements[12*i+11]);
            throw Failure("Sky vertex declaration probe captured; decoder required");
        }
        const bool chocolate=record.view.source==0x8205D2D8;
        const bool tangentInput=(rigidProfile(record.view.source).normalmap&&!s.alphaRigid())||chocolate;
        const bool uv1Input=record.view.source==0x8202AD78||(record.view.source==0x820465E8&&!s.alphaRigid())||(rigidProfile(record.view.source).gloss&&!s.alphaRigid())||
            rigidProfile(record.view.source).multitone||rigidProfile(record.view.source).normalmap||chocolate;
        const auto decoded=decodeRigidVertices(vertices,elements,c.r7.u32,uv1Input,tangentInput,!isVfxRigidSource(record.view.source));const auto cache=PPC_LOAD_U32(geometry+0x30);
        const auto decodedIndices=decodeCharacterIndices(indices);s.runtime.pointer(cache,12,false);
        const auto declaration=PPC_LOAD_U32(cache+4);
        need(PPC_LOAD_U32(geometry+0x18)==1&&(PPC_LOAD_U32(geometry+0x50)&~3u)==PPC_LOAD_U32(geometry+0x10)&&
             (PPC_LOAD_U32(geometry+0x54)&3)==2&&(PPC_LOAD_U32(geometry+0x58)&0xE0000003)==0x20000002&&
             PPC_LOAD_U32(geometry+0x70)==PPC_LOAD_U32(geometry+0x1C)&&PPC_LOAD_U32(geometry+0x74)==indices.size()&&
             declaration&&c.r23.u32==declaration,"Original rigid source/declaration headers differ");
        // Raw diagnostic copy of the original inputs. It never decides a primary
        // outcome (see containedRigidCapture): over-budget tables are skipped and
        // I/O or allocation failures are reported, never thrown.
        RigidCaptureIdentity captureIdentity;
        captureIdentity.source=record.view.source;captureIdentity.caller=immediate?0x827402B0u:0x82740624u;
        captureIdentity.typed=s.rigidTyped;captureIdentity.metadata=mesh.metadata;captureIdentity.object=mesh.object;
        captureIdentity.geometry=geometry;captureIdentity.payload=mesh.payloadId;captureIdentity.scene=s.runtime.nativeDepthCopyCount.load();
        const auto captureGroup=[&](const char* stem,bool omitEmptyRows,RigidCaptureIdentity& id,RigidCaptureOutcome& o)->RigidCaptureStatus {
            o.step="extent";
            const auto entries=PPC_LOAD_U32(mesh.metadata+0x14),rows=PPC_LOAD_U32(mesh.metadata+0x10);
            id.entries=entries;id.count=rows;
            // Reuse mesh entry's overflow, logical-owner and mapped-span checks for
            // this copy. Zero consumes no row pointer, including when rejected.
            const auto bytes=rigidSubmeshExtent(s.runtime,entries,rows);id.bytes=bytes;
            if(const auto owner=s.runtime.engineAudio?s.runtime.engineAudio->allocationSpan(entries):std::nullopt) {
                id.ownerBase=owner->address;id.ownerExtent=uint32_t(owner->extent);id.ownerGeneration=owner->generation;
            }
            if(bytes>rigidCaptureBudgetBytes) {
                o.reasonCode="budget_exceeded";
                std::snprintf(o.reason,sizeof(o.reason),"Rigid submesh capture of %u bytes exceeds the %u byte diagnostic budget; rendering is unaffected",
                    bytes,rigidCaptureBudgetBytes);
                return RigidCaptureStatus::SkippedBudget;
            }
            o.step="read_rows";
            std::vector<uint8_t> submeshes;
            if(bytes){const auto* p=s.runtime.pointer(entries,bytes,false);submeshes.assign(p,p+bytes);}
            const auto write=[&](const char* step,const char* suffix,const std::vector<uint8_t>& data) {
                o.step=step;
                std::ofstream out(s.runtime.frameCaptureDirectory/(std::string(stem)+suffix),std::ios::binary);
                out.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));
                out.close();need(bool(out),"Cannot write rigid capture file");
            };
            // Preserve the entire original table, including rows the original
            // material flag branch skips. No native interleaving or repacking.
            write("write_geometry","-geometry.bin",header);write("write_vertices","-vertices.bin",vertices);
            write("write_indices","-indices.bin",indices);write("write_elements","-elements.bin",elements);
            if(bytes||!omitEmptyRows)write("write_submeshes","-submeshes.bin",submeshes);
            o.step="written";return RigidCaptureStatus::Complete;
        };
        std::shared_ptr<Graphics::NativeRigidMesh> native;
        try {native=s.backend.uploadRigidMesh(decoded,decodedIndices);}
        catch(const Graphics::Error& error) {
            // Preserve rejected original inputs before any binding. Diagnostics
            // never repair attributes or change the original failure/result: the
            // contained call cannot throw, so the bare rethrow below is primary.
            if(!s.runtime.frameCaptureDirectory.empty()) {
                auto id=captureIdentity;id.phase="rejected_upload";
                containedRigidCapture([&](RigidCaptureOutcome& o)->RigidCaptureStatus {
                    const auto status=captureGroup("rigid-rejected",true,id,o);
                    if(status!=RigidCaptureStatus::Complete)return status;
                    o.step="write_metadata";
                    std::ofstream out(s.runtime.frameCaptureDirectory/"rigid-rejected.json");
                    out<<"{\"geometry\":"<<geometry<<",\"metadata\":"<<mesh.metadata<<",\"object\":"<<mesh.object
                       <<",\"payload\":"<<mesh.payloadId<<",\"stride\":"<<c.r7.u32<<",\"vertex_count\":"<<decoded.size()
                       <<",\"selected_submesh_entries\":[";
                    for(size_t i=0;i<mesh.draws.size();++i){if(i)out<<',';out<<mesh.draws[i].entry;}
                    out<<"],\"capture_source\":\"original_rigid_upload_rejection\",\"gameplay_verified\":false}\n";
                    out.close();need(bool(out),"Cannot write rejected original rigid metadata");
                    std::fprintf(stderr,"[NATIVE RIGID REJECTED INPUT] geometry=%08X vertices=%zu stride=%u payload=%08X: %s\n",
                        geometry,decoded.size(),c.r7.u32,mesh.payloadId,error.what());
                    return RigidCaptureStatus::Complete;
                },[&](const RigidCaptureOutcome& o){reportCapture(id,o,"diagnostic_only=1 primary_upload_error=preserved");});
            }
            throw;
        }
        if(immediate)s.backend.bindRigidMeshVertices(native);else s.backend.bindRigidMeshVertices(payload,native);
        // Insert only after the full decoder/index/renderer validation above succeeded.
        if(rigidSourceCached&&c.r7.u32<0x10000)s.rigidSourceCache.insert(vertices,indices,elements,rigidVariant,rigidSourceLookup.key,native,&rigidSourceLookup.pending);
        if(!s.runtime.frameCaptureDirectory.empty()&&!s.rigidFirstMeshDone&&!s.rigidFirstMeshAttempted&&!mesh.draws.empty()) {
            // One raw attempt per fresh latch, even if skipped or failed: a later
            // smaller table must not silently replace the first original input.
            s.rigidFirstMeshAttempted=true;
            auto id=captureIdentity;id.phase="first_raw";
            if(containedRigidCapture([&](RigidCaptureOutcome& o){return captureGroup("rigid-first",false,id,o);},
                    [&](const RigidCaptureOutcome& o){reportCapture(id,o,"diagnostic_only=1 primary_outcome=unchanged");})==RigidCaptureStatus::Complete) {
                s.rigidFirstMeshDone=true;s.rigidFirstMeshGeometry=geometry;s.rigidFirstMeshPayload=mesh.payloadId;
                s.rigidCapturePayload=mesh.payloadId;
            }
        }
        mesh.header=std::move(header);mesh.cache=cache;mesh.declaration=declaration;mesh.native=std::move(native);mesh.phase=1;
    } else {
        need(mesh.native&&mesh.header.size()==0x78&&!std::memcmp(s.runtime.pointer(geometry,0x78,false),mesh.header.data(),0x78)&&
             PPC_LOAD_U32(mesh.cache+4)==mesh.declaration,"Original rigid geometry/header changed");
        if(site==0x826FF498) {
            need(mesh.phase==1&&c.r4.u32==mesh.declaration,"Rigid declaration binding is out of order");
            const bool chocolate=record.view.source==0x8205D2D8;
            const bool tangent=rigidProfile(record.view.source).normalmap&&!s.alphaRigid();
            const bool vfx=isVfxRigidSource(record.view.source);
            if(immediate){if(vfx)s.backend.bindVfxRigidMeshDeclaration(mesh.native);else if(chocolate)s.backend.bindChocolateMeshDeclaration(mesh.native,s.alphaRigid());else if(tangent)s.backend.bindRigidNormalMeshDeclaration(mesh.native);else s.backend.bindRigidMeshDeclaration(mesh.native);}
            else{if(vfx)s.backend.bindVfxRigidMeshDeclaration(payload,mesh.native);else if(chocolate)s.backend.bindChocolateMeshDeclaration(payload,mesh.native,s.alphaRigid());else if(tangent)s.backend.bindRigidNormalMeshDeclaration(payload,mesh.native);else s.backend.bindRigidMeshDeclaration(payload,mesh.native);}
            mesh.phase=2;
        } else if(site==0x826FF4A4) {
            need(mesh.phase==2&&c.r4.u32==geometry+0x58,"Rigid index binding is out of order");
            if(immediate)s.backend.bindRigidMeshIndices(mesh.native);else s.backend.bindRigidMeshIndices(payload,mesh.native);mesh.phase=3;
        } else if(site==0x8270142C) {
            need(immediate&&mesh.phase==3&&mesh.cursor==mesh.draws.size()&&!mesh.auxiliaryCleared&&
                 !s.rigidMaterial.active&&!s.rigidMaterial.committed&&c.r3.u32==mesh.context&&c.r4.u32==1&&
                 !c.r5.u32&&!c.r6.u32&&!c.r7.u32&&c.r8.u32==1,"Rigid immediate auxiliary-stream cleanup differs");
            s.backend.clearZPrepassAuxiliaryStream();mesh.auxiliaryCleared=true;
        } else if(site==(immediate?0x82701438u:0x82701628u)) {
            need(mesh.phase==3&&mesh.cursor==mesh.draws.size()&&!s.rigidMaterial.active&&(!immediate||mesh.auxiliaryCleared),
                 "Original rigid loop ended before its material/draw closure");
            if(immediate) {
                const auto objectGeometry=PPC_LOAD_U32(mesh.object+0x18),offset=PPC_LOAD_U32(0x82D6CAB0);
                need(uint64_t(objectGeometry)+offset+4<=UINT32_MAX,"Rigid cleanup morph offset overflows");
                s.runtime.pointer(objectGeometry+offset,4,false);
                need(!PPC_LOAD_U32(objectGeometry+offset),"Rigid original morph cleanup branch changed");
            }
            mesh.active=false;mesh.complete=true;return;
        } else {
            need(site==(immediate?0x827013B0u:0x827015C0u)&&mesh.phase==3&&mesh.cursor<mesh.draws.size()&&
                 s.rigidMaterial.committed&&!s.rigidMaterial.active&&(!immediate||mesh.submeshUpdated),
                 "Rigid deferred draw has no completed material commit");
            const auto& row=mesh.draws[mesh.cursor];
            need(c.r31.u32==row.entry&&c.r3.u32==mesh.context&&c.r4.u32==row.words[3]&&c.r5.u32==row.words[4]&&
                 c.r6.u32==row.words[5]&&c.r7.u32==row.words[6]&&s.rigidMaterial.material==row.material&&
                 PPC_LOAD_U16(row.materialHeader)==row.flags,"Original rigid draw arguments/material differ");
            for(uint32_t j=0;j<9;++j)need(PPC_LOAD_U32(row.entry+4*j)==row.words[j],"Rigid submesh changed before draw");
            const auto& effective=driver.effectiveState();const auto camera=driver.cameraBinding();
            need(camera.camera==s.rigidCamera,"Rigid draw camera owner changed");
            using S=Graphics::ScalarState;Graphics::RigidMeshDraw draw{};
            draw.primitiveType=c.r4.u32;draw.baseVertex=c.r5.s32;draw.startIndex=c.r6.u32;draw.indexCount=c.r7.u32;draw.viewport=camera.viewport;
            if(const auto scissor=s.backend.scissor())draw.scissor=*scissor;
            draw.depthEnable=effective.scalar(S::DepthEnable);draw.depthWrite=effective.scalar(S::DepthWrite);draw.depthCompare=effective.scalar(S::DepthCompare);
            draw.cull=effective.scalar(S::Cull);draw.fill=effective.scalar(S::Fill);draw.colorMask=effective.scalar(S::ColorMask0);
            draw.stencilEnable=effective.scalar(S::StencilEnable);draw.alphaTest=effective.scalar(S::AlphaTest);
            draw.scissorEnable=effective.scalar(S::ScissorEnable);draw.halfPixelOffset=effective.scalar(S::HalfPixelOffset);
            draw.primitiveReset=effective.scalar(S::PrimitiveResetEnable);draw.primitiveResetIndex=effective.scalar(S::PrimitiveResetIndex);
            draw.viewportEnable=effective.scalar(S::ViewportEnable);draw.clipPlaneEnable=effective.scalar(S::ClipPlaneEnable);
            draw.multisampleAntialias=effective.scalar(S::MultisampleAntialias);draw.multisampleMask=effective.scalar(S::MultisampleMask);
            draw.alphaToMask=effective.scalar(S::AlphaToMask);draw.depthBiasBits=effective.scalar(S::DepthBias);draw.slopeBiasBits=effective.scalar(S::SlopeBias);
            draw.depthPolicy=Graphics::ShadowMeshDepthPolicy::Reference20e4Rne;
            draw.blendEnable=effective.scalar(S::BlendEnable);draw.blendWord=effective.effectiveBlend(0);draw.expandedBlend=effective.scalar(S::ExpandedBlend0);
            need(!effective.scalar(S::TessellationMode),"Rigid tessellation is unqualified");
            const bool rigidAlpha=s.rigidPixel->originalAddress()==0x8200E1BC;
            const bool a168Alpha=s.rigidPixel->originalAddress()==0x82017E4C;
            // Dual-alpha's scheduled VS/PS instructions are identical to the
            // proven 168F8 alpha pair, including its single stage-0 base fetch.
            const bool dualAlpha=s.rigidPixel->originalAddress()==0x8202C3BC;
            const bool familyAlpha=isRigidFamilyAlphaPixel(s.rigidPixel->originalAddress());
            const bool uv=record.view.source==0x820465E8;
            const bool uvAlpha=uv&&s.alphaRigid();
            const bool singleUvAlpha=record.view.source==0x82042F58&&s.alphaRigid();
            const bool flipbook=record.view.source==0x82039208;
            const bool chocolate=record.view.source==0x8205D2D8;
            const bool chocolateAlpha=chocolate&&s.alphaRigid();
            const bool projtex=rigidProfile(record.view.source).projtex,projtexAlpha=projtex&&s.alphaRigid();
            const uint32_t chocolateCount=chocolateAlpha?3u:2u,chocolateStage=chocolateAlpha?0u:2u;
            const bool vfx=isVfxRigidSource(record.view.source);
            const bool noShadowSamples=vfx||rigidAlpha||a168Alpha||dualAlpha||familyAlpha||chocolateAlpha||uvAlpha||singleUvAlpha||flipbook||projtexAlpha;
            draw.shadowSamplePolicy=noShadowSamples?Graphics::RigidShadowSamplePolicy::NotUsed:Graphics::RigidShadowSamplePolicy::ReferenceD24FS8DepthRRRR;
            if(uv&&!uvAlpha)draw.shadows[0]=s.rigidTextures.depths[1];
            else if(!noShadowSamples)draw.shadows=s.rigidTextures.depths;
            constexpr std::array<uint32_t,20> sampler{2,2,2,0,0,0,2,0,0,1,1,1,0,13,0,0,0,0,0,1};
            constexpr std::array<uint32_t,20> chocolateSampler{0,0,0,0,1,1,1,0,0,1,1,1,0,13,0,0,0,0,0,1};
            // 168F8 alpha draws (reach-game-224 packet 82D6E01C): stage 0 is
            // the effect-owned base-pattern table (== chocolate); stages 1-2
            // inherit earlier sampler lanes. SDK0/4/8 are U/V/W addressing;
            // SDK10/14/18 are mag/min/mip filters, as pinned in engine_state.
            constexpr std::array<uint32_t,20> a168Sampler0{0,0,0,0,1,1,1,0,0,1,1,1,0,13,0,0,0,0,0,1};
            // PS82017E4C fetches only stage0. Its selected pass owns six
            // sampler words for that stage; later stages are inherited and
            // neither consumed nor bound by the alpha draw. Validate every
            // consumed word without imposing a profile on unused history.
            if(!vfx&&!rigidAlpha&&!chocolateAlpha&&!projtexAlpha&&!dualAlpha&&!familyAlpha&&!uv&&!singleUvAlpha&&!flipbook)for(uint32_t stage=0;stage<(a168Alpha?1u:2u);++stage) {
                for(uint32_t i=0;i<sampler.size();++i) {
                    const uint32_t expected=a168Alpha?a168Sampler0[i]:sampler[i];
                    if(effective.sampler(stage,4*i)!=expected) {
                        std::fprintf(stderr,"[NATIVE RIGID SAMPLER MISMATCH] source=%08X typed=%08X packet=%08X geometry=%08X material=%08X stage=%u sdk=%08X actual=%08X expected=%08X VS=%08X PS=%08X\n",
                            record.view.source,s.rigidTyped,mesh.packet,mesh.geometry,row.material,stage,4*i,effective.sampler(stage,4*i),expected,
                            s.rigidVertex->originalAddress(),s.rigidPixel->originalAddress());
                        for(uint32_t slot=0;slot<3;++slot) {
                            std::fprintf(stderr,"[NATIVE RIGID SAMPLER WORDS] stage=%u",slot);
                            for(uint32_t lane=0;lane<20;++lane)std::fprintf(stderr," %08X",effective.sampler(slot,4*lane));
                            std::fprintf(stderr,"\n");
                        }
                    }
                    need(effective.sampler(stage,4*i)==expected,"Rigid inherited sampler state is unqualified");
                }
                auto& desc=draw.samplers[stage];
                if(a168Alpha&&stage==0) {
                    desc.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                    desc.AddressU=desc.AddressV=desc.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
                } else {
                    // Original sampler state is mag=point, min=point, mip=2.
                    // On Xenos mip filter 2 is BaseMap, so shadow fetches must
                    // stay on level 0 rather than trilinearly filtering mips.
                    desc.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
                    desc.AddressU=desc.AddressV=desc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
                }
                desc.MaxAnisotropy=1;
                desc.ComparisonFunc=D3D11_COMPARISON_NEVER;desc.MinLOD=0;desc.MaxLOD=(a168Alpha&&stage==0)?13.f:0.f;
            }
            if(chocolate)for(uint32_t stage=0;stage<chocolateCount;++stage) {
                for(uint32_t i=0;i<chocolateSampler.size();++i)
                    need(effective.sampler(chocolateStage+stage,4*i)==chocolateSampler[i],"Chocolate material sampler state is unqualified");
                auto& desc=draw.materialSamplers[stage];
                desc.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                desc.AddressU=desc.AddressV=desc.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
                desc.MaxAnisotropy=1;desc.ComparisonFunc=D3D11_COMPARISON_NEVER;desc.MinLOD=0;desc.MaxLOD=13;
            }
            if(rigidAlpha) {
                need(record.rigidBaseTexture&&record.rigidBaseHeader,"Rigid alpha base texture is missing");
                for(uint32_t index=0;index<20;++index)need(effective.sampler(0,4*index)==a168Sampler0[index],
                     "Rigid alpha base sampler state differs");
                draw.baseTexture=record.rigidBaseTexture;
                draw.baseSampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                draw.baseSampler.AddressU=draw.baseSampler.AddressV=draw.baseSampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
                draw.baseSampler.MaxAnisotropy=1;draw.baseSampler.ComparisonFunc=D3D11_COMPARISON_NEVER;draw.baseSampler.MaxLOD=13;
            }
            if(chocolate) {
                for(uint32_t stage=0;stage<chocolateCount;++stage) {
                    need(record.chocolateTexture[stage]&&record.chocolateTextureHeader[stage],"Chocolate draw has no material texture owner");
                    draw.materialTextures[stage]=record.chocolateTexture[stage];
                }
            }
            if(uv) {
                if(!uvAlpha) {
                    for(uint32_t index=0;index<sampler.size();++index)
                        need(effective.sampler(0,4*index)==sampler[index],"UV character-shadow sampler differs");
                    auto& desc=draw.samplers[0];desc.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
                    desc.AddressU=desc.AddressV=desc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
                    desc.MaxAnisotropy=1;desc.ComparisonFunc=D3D11_COMPARISON_NEVER;desc.MaxLOD=0;
                }
                for(uint32_t texture=0;texture<2;++texture) {
                    const auto stage=texture+(uvAlpha?0u:1u);
                    for(uint32_t index=0;index<a168Sampler0.size();++index)
                        need(effective.sampler(stage,4*index)==a168Sampler0[index],"UV material sampler differs");
                    need(record.rigidUvTexture[texture]&&record.rigidUvTextureHeader[texture],"UV draw has no committed material texture");
                    draw.uvTextures[texture]=record.rigidUvTexture[texture];
                    auto& desc=draw.uvSamplers[texture];desc.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                    desc.AddressU=desc.AddressV=desc.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
                    desc.MaxAnisotropy=1;desc.ComparisonFunc=D3D11_COMPARISON_NEVER;desc.MaxLOD=13;
                }
            }
            if(rigidProfile(record.view.source).textured) {
                need(record.rigidBaseTexture&&record.rigidBaseHeader,"Textured rigid draw has no material texture owner");
                if(vfx||a168Alpha||dualAlpha||familyAlpha||singleUvAlpha||flipbook||projtexAlpha) {
                    if(vfx||dualAlpha||familyAlpha||singleUvAlpha||flipbook||projtexAlpha)for(uint32_t index=0;index<20;++index)
                        need(effective.sampler(0,4*index)==a168Sampler0[index],"Dual alpha base sampler state differs");
                    draw.baseTexture=record.rigidBaseTexture;
                    draw.baseSampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                    draw.baseSampler.AddressU=draw.baseSampler.AddressV=draw.baseSampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
                    draw.baseSampler.MaxAnisotropy=1;draw.baseSampler.ComparisonFunc=D3D11_COMPARISON_NEVER;draw.baseSampler.MaxLOD=13;
                } else {
                    constexpr std::array<uint32_t,20> baseSampler{0,0,0,0,1,1,1,0,0,1,1,1,0,13,0,0,0,0,0,1};
                    for(uint32_t index=0;index<baseSampler.size();++index)need(effective.sampler(2,4*index)==baseSampler[index],
                        "Textured rigid inherited base sampler state is unqualified");
                    draw.baseTexture=record.rigidBaseTexture;
                    draw.baseSampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                    draw.baseSampler.AddressU=draw.baseSampler.AddressV=draw.baseSampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
                    draw.baseSampler.MaxAnisotropy=1;draw.baseSampler.ComparisonFunc=D3D11_COMPARISON_NEVER;draw.baseSampler.MaxLOD=13;
                }
            }
            if(!familyAlpha&&(rigidProfile(record.view.source).multitone||rigidProfile(record.view.source).normalmap)) {
                need(record.rigidNoiseTexture&&record.rigidNoiseHeader,rigidProfile(record.view.source).normalmap?"Normalmap draw has no normal texture owner":"Multitone draw has no noise texture owner");
                for(uint32_t index=0;index<20;++index)need(effective.sampler(3,4*index)==effective.sampler(2,4*index),
                    "Multitone noise sampler differs from qualified linear/wrap state");
                draw.noiseTexture=record.rigidNoiseTexture;draw.noiseSampler=draw.baseSampler;
            }
            if(projtex&&!projtexAlpha) {
                need(record.rigidNoiseTexture&&record.rigidNoiseHeader,"Projected rigid draw has no projection texture owner");
                for(uint32_t index=0;index<20;++index)need(effective.sampler(3,4*index)==(index<3?2u:chocolateSampler[index]),
                    "Projected texture sampler differs from original linear/clamp state");
                draw.noiseTexture=record.rigidNoiseTexture;draw.noiseSampler=draw.baseSampler;
                draw.noiseSampler.AddressU=draw.noiseSampler.AddressV=draw.noiseSampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
            }
            bool alphaOne=false;const auto color=driver.color(camera.colorIdentity,alphaOne);need(!alphaOne,"Rigid color owner requires unsupported alpha synthesis");
            if(immediate)s.backend.drawRigidMesh(color,driver.depth(camera.depthIdentity),mesh.native,*s.rigidVertex,*s.rigidPixel,
                s.rigidImmediate.commit,draw);
            else s.backend.recordRigidMesh(payload,color,driver.depth(camera.depthIdentity),mesh.native,*s.rigidVertex,*s.rigidPixel,
                record.rigidMaterialVS,record.rigidMaterialPS,mesh.replayValues,draw);
            if(!s.runtime.frameCaptureDirectory.empty()&&s.rigidFirstMeshDone&&
               s.rigidFirstMeshGeometry==geometry&&s.rigidFirstMeshPayload==mesh.payloadId&&
               s.rigidCapturePayload==mesh.payloadId&&!s.rigidCaptureDraw&&!s.rigidCaptureDrawAttempted) {
                // One contained metadata attempt per completed raw capture. The
                // actual draw above is primary; no failure here retries or throws.
                s.rigidCaptureDrawAttempted=true;
                RigidCaptureIdentity id;
                id.phase="first_draw";id.source=record.view.source;id.caller=immediate?0x827402B0u:0x82740624u;
                id.typed=s.rigidTyped;id.metadata=mesh.metadata;id.object=mesh.object;id.geometry=geometry;
                id.payload=mesh.payloadId;id.entries=row.entry;id.scene=s.runtime.nativeDepthCopyCount.load();
                if(containedRigidCapture([&](RigidCaptureOutcome& o)->RigidCaptureStatus {
                o.step="write_draw";
                std::ofstream out(s.runtime.frameCaptureDirectory/"rigid-first-draw.json");
                out<<"{\n  \"source\": \"original first rigid mesh and effective recording state\"";
                const auto scalar=[&](const char* name,uint64_t value){out<<",\n  \""<<name<<"\": "<<value;};
                const auto words=[&](const char* name,const auto& values){out<<",\n  \""<<name<<"\": [";
                    for(size_t i=0;i<values.size();++i){if(i)out<<", ";out<<values[i];}out<<"]";};
                scalar("payload",mesh.payloadId);scalar("effect",s.rigidId);scalar("typed",s.rigidTyped);
                scalar("metadata",mesh.metadata);scalar("object",mesh.object);scalar("geometry",geometry);
                scalar("submesh",row.entry);scalar("material",row.material);scalar("materialFlags",row.flags);
                scalar("camera",camera.camera);scalar("colorIdentity",camera.colorIdentity);scalar("depthIdentity",camera.depthIdentity);
                scalar("vertexCount",mesh.native->vertexCount());scalar("stride",PPC_LOAD_U32(geometry+4));
                scalar("primitiveType",draw.primitiveType);scalar("indexCount",draw.indexCount);scalar("startIndex",draw.startIndex);
                out<<",\n  \"baseVertex\": "<<draw.baseVertex;
                words("viewportWords",draw.viewport);words("scissor",draw.scissor);words("submeshWords",row.words);
                scalar("depthEnable",draw.depthEnable);scalar("depthWrite",draw.depthWrite);scalar("depthCompare",draw.depthCompare);
                scalar("cull",draw.cull);scalar("fill",draw.fill);scalar("colorMask",draw.colorMask);
                scalar("stencilEnable",draw.stencilEnable);scalar("alphaTest",draw.alphaTest);
                scalar("scissorEnable",draw.scissorEnable);scalar("halfPixelOffset",draw.halfPixelOffset);
                scalar("primitiveReset",draw.primitiveReset);scalar("primitiveResetIndex",draw.primitiveResetIndex);
                scalar("viewportEnable",draw.viewportEnable);scalar("clipPlaneEnable",draw.clipPlaneEnable);
                scalar("multisampleAntialias",draw.multisampleAntialias);scalar("multisampleMask",draw.multisampleMask);
                scalar("alphaToMask",draw.alphaToMask);scalar("depthBiasBits",draw.depthBiasBits);scalar("slopeBiasBits",draw.slopeBiasBits);
                scalar("blendEnable",draw.blendEnable);scalar("blendWord",draw.blendWord);scalar("expandedBlend",draw.expandedBlend);
                scalar("worldShadow",s.rigidTextures.rows[0].texture);scalar("characterShadow",s.rigidTextures.rows[1].texture);
                std::array<uint32_t,20> samplerWords{};
                for(uint32_t stage=0;stage<2;++stage){for(uint32_t i=0;i<samplerWords.size();++i)samplerWords[i]=effective.sampler(stage,4*i);
                    words(stage?"sampler1Words":"sampler0Words",samplerWords);}
                for(uint32_t reg:{46u,47u,49u}) {
                    std::array<uint32_t,4> bits{};for(uint32_t i=0;i<4;++i)bits[i]=std::bit_cast<uint32_t>(record.rigidMaterialPS[reg][i]);
                    words(reg==46?"materialPS46Bits":reg==47?"materialPS47Bits":"materialPS49Bits",bits);
                }
                out<<"\n}\n";out.close();need(bool(out),"Cannot write original first rigid draw-state capture");
                o.step="written";return RigidCaptureStatus::Complete;
                },[&](const RigidCaptureOutcome& o){reportCapture(id,o,"diagnostic_only=1 primary_outcome=unchanged");})==RigidCaptureStatus::Complete)
                    s.rigidCaptureDraw=true;
            }
            ++mesh.cursor;s.rigidMaterial.committed=false;mesh.submeshUpdated=false;
            if(vfx) {
                static thread_local uint32_t vfxDrawSample{};
                if(sampleHotLog(vfxDrawSample))std::fprintf(stderr,"[NATIVE VFX RIGID DRAW] geometry=%08X indices=%u; original texture, tint, UV, opacity and depth committed\n",geometry,draw.indexCount);
            }
            {static thread_local uint32_t rigidDrawSample{};
            if(sampleHotLog(rigidDrawSample))
                std::fprintf(stderr,"[NATIVE RIGID %s DRAW] geometry=%08X submesh=%08X indices=%u completed=%u/%zu\n",
                    immediate?"IMMEDIATE":"RECORD",geometry,row.entry,draw.indexCount,mesh.cursor,mesh.draws.size());}
        }
    }
    c.lr=site+4;
}
void EngineEffects::skyMaterialOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    // The original material loop owns row callbacks and dirty filtering.
    auto& s=*state;s.require(base);
    requireRigidSelection(s.rigidTyped);auto& mesh=s.skyMesh;auto& run=s.skyMaterial;
    auto& record=s.find(s.rigidId);const auto& v=record.view;
    const bool immediate=mesh.immediate;
    need(currentContext==&c&&mesh.active&&mesh.cpu==&c&&mesh.phase==3&&mesh.cursor<mesh.draws.size()&&
         (immediate?(s.rigidImmediate.active&&s.rigidImmediate.cpu==&c&&!record.recordingContext&&mesh.submeshUpdated):
          (record.recordingContext&&s.runtime.engineDriver->recordingOwners().activePayload(mesh.context)==mesh.payload)),
         "Sky material has no active original recording mesh");
    const auto& submesh=mesh.draws[mesh.cursor];
    if(site==(immediate?0x826B54D0u:0x826B5618u)) {
        need(c.lastFunction==site&&uint32_t(c.lr)==(immediate?0x8270131Cu:0x82701528u)&&c.r1.u32==mesh.frame&&
             (immediate?c.r19.u32:c.r17.u32)==s.rigidTyped&&c.r31.u32==submesh.entry&&c.r3.u32==s.rigidTyped&&c.r4.u32==submesh.material&&!run.active&&!run.committed,
             "Unqualified original sky material-loop entry");
        State::SkyMaterialRun next;next.material=submesh.material;next.frame=mesh.frame-0xC0;
        const auto count=(PPC_LOAD_U32(next.material+0xC)>>10)&31,rows=PPC_LOAD_U32(next.material+0x14),bindings=PPC_LOAD_U32(s.rigidTyped+0x28);
        if(count)s.runtime.pointer(rows,12*count,false);
        constexpr std::array<uint32_t,14> callbacks{0,0x8270BE50,0x8270BDB0,0x8270BBC0,0x8270BBC0,0x8270BBC0,
            0x8270BC60,0x8270BA80,0x8270BA80,0x8270BA80,0x8270BD08,0x8270BB20,0x8270BB20,0x8270BB20};
        for(uint32_t i=0;i<count;++i) {
            const auto bits=PPC_LOAD_U32(rows+12*i);if(!immediate&&(bits&0x01C00000)!=0x00400000)continue;
            State::SkyMaterialRun::Row row;row.offset=12*i;row.type=bits&255;row.value=PPC_LOAD_U32(rows+12*i+8);
            need(row.type&&row.type<callbacks.size()&&PPC_LOAD_U32(0x82CEFF88+8*row.type)==callbacks[row.type],
                 "Unqualified original sky material value callback");
            const auto binding=bindings+24*((bits>>16)&63);s.runtime.pointer(binding,24,false);
            for(uint32_t j=0;j<6;++j)row.binding[j]=PPC_LOAD_U32(binding+4*j);row.handle=row.binding[0];
            const auto entries=(row.handle&1)?s.schema().parameters(true):record.metadata->parameters(false);
            const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& p){return p.handle==row.handle;});
            need(found!=entries.end()&&!(found->descriptorWords[0]&3),"Sky material binding is not an owned leaf");
            const auto offset=16*(found->descriptorWords[1]&0xFFFF);
            const auto bytes=(row.handle&1)?s.schema().sharedDefaults().size():record.metadata->privateDefaults().size();
            need(offset<=bytes&&bytes-offset>=16&&((row.handle>>1)&0x1FFFF)<1024,"Sky material leaf exceeds storage");
            const bool vector=(row.type>=3&&row.type<=5)||(row.type>=7&&row.type<=9)||row.type>=11;
            if(vector){need(!(row.value&15),"Sky vector material input is not aligned for original LVX");s.runtime.pointer(row.value,16,false);}
            else if(row.type==6||row.type==10){need(!(row.value&3),"Sky integer material input is not word aligned");s.runtime.pointer(row.value&~15u,16,false);}
            else s.runtime.pointer(row.value,row.type==1?8:4,false);
            next.rows.push_back(row);
        }
        next.active=true;run=std::move(next);return;
    }
    need((site==0x826B2F20||site==0x82C1DBA0)&&run.active&&c.r1.u32==run.frame&&
         PPC_LOAD_U32(run.frame)==mesh.frame&&PPC_LOAD_U32(run.frame+0xB8)==(immediate?0x8270131Cu:0x82701528u)&&
         uint32_t(c.lr)==(immediate?0x826B560Cu:0x826B5764u)&&c.r3.u32==v.identity&&c.r30.u32==s.rigidTyped&&
         (immediate?c.r28.u32:c.r27.u32)==run.material&&run.cursor==run.rows.size()&&!run.pendingSlot,
         "Original sky material commit frame/order differs");
    s.validatePool(v.pool);
    need(word(record.metadata->body(),0x120)==1&&word(record.metadata->body(),0x124)==1&&PPC_LOAD_U32(v.pool+0x114)==1,
         "Sky material dirty filter extent differs");
    // Sky constant commit: staging banks plus material overlays. VS regs 46
    // (g_cloudVelocityTex2And3, leaf 18) and 47 (g_cloudVelocity, leaf 17)
    // come from dirty material leaves; PS has no material-mapped registers
    // (pc23 arrives via staging). Textures resolve from material rows below.
    auto dirty=std::span<uint8_t,128>(s.runtime.pointer(v.pool,128,true),128);
    std::array<uint8_t,128> prospectiveShared;std::copy(dirty.begin(),dirty.end(),prospectiveShared.begin());
    if(site==0x826B2F20)filterRigidDirty(prospectiveShared,
        std::span<const uint8_t,128>(s.runtime.pointer(v.pool+0x80,128,false),128));
    if(PPC_LOAD_U32(0x82D00F80)&&!rigidSharedCommitHasNoWork(prospectiveShared,word(record.metadata->body(),0x124))) {
        char why[320];std::snprintf(why,sizeof(why),
            "Sky recording union shared-map has nonempty filtered work: site=%08X pool=%08X global=%08X words=%u",
            site,v.pool,PPC_LOAD_U32(0x82D00F80),word(record.metadata->body(),0x124));
        throw Failure(why);
    }
    // The sky pass consumes shared view-projection through original replay
    // callbacks. Reject an eligibility change instead of dropping an upload.
    {
        const auto handle=parameter(v.identity,"g_ViewProjection"),leaf=(handle>>1)&0x1FFFF;
        need((handle&1)&&leaf<1024&&!(PPC_LOAD_U8(v.pool+0x80+leaf/8)&(0x80>>(leaf&7))),
             "Sky inherited shared mapping remains material-eligible");
    }
    if(site==0x826B2F20) {
        need(!run.filtered,"Sky material dirty filter repeated");
        filterRigidDirty(record.privateModified,record.privateMask);
        std::copy(prospectiveShared.begin(),prospectiveShared.end(),dirty.begin());
        run.filtered=true;return;
    }
    need(run.filtered&&!run.committed&&c.lastFunction==0x82C1DBA0,"Sky SDK material commit was not filtered");
    auto words=record.view.defaultVectorWords;
    if(record.parameters)for(uint32_t i=0;i<words.size();++i)words[i]=PPC_LOAD_U32(record.parameters->address+4*i);
    need(words.size()==188,"Sky private bank extent differs");
    if(!record.skyMaterialInitialized)for(uint32_t leaf:{17u,18u})
        need(record.privateModified[leaf/8]&(0x80>>(leaf&7)),"First sky commit did not initialize material registers");
    auto vertex=record.skyMaterialVS;auto pixel=record.skyMaterialPS;
    if(immediate) {
    const auto& staging=s.rigidImmediate.staging;
    need(staging.complete&&staging.phase==4&&!staging.prepared,"Sky immediate material lost its original stage uploads");
    for(uint32_t stage=0;stage<2;++stage)
        need(!std::memcmp(s.runtime.pointer(stage?0x82D6C450:0x82D6C0D0,896,false),staging.copies[stage].data(),896),
             "Sky immediate per-submesh staging changed without a qualified original upload");
    for(uint32_t i=0;i<56;++i)for(uint32_t j=0;j<4;++j)
        vertex[i][j]=std::bit_cast<float>(word(staging.copies[0],16*i+4*j));
    for(uint32_t i=0;i<pixel.size()&&i<56;++i)for(uint32_t j=0;j<4;++j)
        pixel[i][j]=std::bit_cast<float>(word(staging.copies[1],16*i+4*j));
    }
    // Material overlay leaves from both identical sky pass maps (leaf 18 to
    // vc46, leaf 17 to vc47); both are four-lane private words like rigid's.
    if(record.privateModified[18/8]&(0x80>>(18&7))) {
        for(uint32_t lane=0;lane<4;++lane)vertex[46][lane]=std::bit_cast<float>(words[96+lane]);
        for(float lane:vertex[46])need(std::isfinite(lane),"Sky material vc46 contains nonfinite data");
    }
    if(record.privateModified[17/8]&(0x80>>(17&7))) {
        for(uint32_t lane=0;lane<4;++lane)vertex[47][lane]=std::bit_cast<float>(words[92+lane]);
        for(float lane:vertex[47])need(std::isfinite(lane),"Sky material vc47 contains nonfinite data");
    }
    // Sampler texture rows 20..22 are the three authored sky layers. The
    // original stage-3 bind is independent of those rows and, in the live
    // gameplay path, names the second 1280x720 scene copy. Do not substitute
    // leaf23's 64x64 EdgeAA palette for that screen-space line/discard input.
    auto& driver=*s.runtime.engineDriver;
    constexpr uint32_t slotLeaves[]={20u,21u,22u,24u},slotWords[]={104u,120u,136u,168u};
    constexpr uint32_t slotHandles[]={0x00600028u,0x0064002Au,0x0068002Cu,0x00700030u};
    const auto selectedTechnique=s.alphaRigid()?0x0007FFFCu:0x0003FFFCu;
    for(uint32_t slot=0;slot<4;++slot)
        need(Graphics::pixelTextureStage(s.cachedPassBinding(record,selectedTechnique,slotHandles[slot]))==slot,
             "Sky selected material texture stage differs");
    need(!Graphics::pixelTextureStage(s.cachedPassBinding(record,selectedTechnique,0x006C002Eu)),
         "Sky selected palette unexpectedly consumes a texture stage");
    for(uint32_t slot=0;slot<3;++slot) {
        const auto header=words[slotWords[slot]];
        {static thread_local uint32_t skyTextureSample{};
        if(sampleHotLog(skyTextureSample))
            std::fprintf(stderr,"[NATIVE SKY TEXTURE] slot=%u leaf=%u header=%08X staged=%08X\n",
                slot,slotLeaves[slot],header,s.skyStagedTextures[slot]);}
        try {
            auto texture=driver.materialTexture(base,header);
            record.skyTexture[slot]=std::move(texture);record.skyTextureHeader[slot]=header;
            {static thread_local uint32_t skyTextureHitSample{};
            if(sampleHotLog(skyTextureHitSample))
                std::fprintf(stderr,"[NATIVE SKY TEXTURE] slot=%u itxd-hit width=%u height=%u format=%u levels=%u\n",
                    slot,record.skyTexture[slot]->width,record.skyTexture[slot]->height,
                    static_cast<unsigned>(record.skyTexture[slot]->format),record.skyTexture[slot]->levelCount());}
        } catch(const std::exception& error) {
            std::fprintf(stderr,"[NATIVE SKY TEXTURE] slot=%u itxd-miss: %s\n",slot,error.what());
            record.skyTexture[slot].reset();record.skyTextureHeader[slot]=0;
        }
        need(bool(record.skyTexture[slot]),"Sky committed sampler texture is missing");
        need(record.skyTextureHeader[slot]==words[slotWords[slot]],"Sky committed texture changed before bind");
        if(immediate)s.backend.bindEngineTexture(slot,record.skyTexture[slot]);
    }
    record.skyLineTarget.reset();record.skyLineTargetId=0;record.skyTexture[3].reset();record.skyTextureHeader[3]=0;
    const auto stagedLine=s.skyStagedTextures[3];
    if(stagedLine&&driver.sceneCopies().owns(stagedLine)) {
        record.skyLineTarget=driver.sceneCopies().backing(stagedLine);record.skyLineTargetId=stagedLine;
        {static thread_local uint32_t skyLineSample{};
        if(sampleHotLog(skyLineSample))
            std::fprintf(stderr,"[NATIVE SKY TEXTURE] slot=3 scene-copy id=%08X width=%u height=%u\n",
                stagedLine,record.skyLineTarget->width,record.skyLineTarget->height);}
    } else if(stagedLine) {
        // Original ENGINE5 can bind a published builtin image or a copied
        // ITXD header. Resolve the exact staged identity through its class
        // owner; the private material leaf is not this callback's storage.
        record.skyTexture[3]=driver.materialTexture(base,stagedLine);
        need(bool(record.skyTexture[3]),"Sky staged line image lost its published texture owner");
        record.skyTextureHeader[3]=stagedLine;
        if(immediate)s.backend.bindEngineTexture(3,record.skyTexture[3]);
    } else {
        const uint32_t slot=3,header=words[slotWords[slot]];
        try {
            record.skyTexture[slot]=driver.materialTexture(base,header);
            record.skyTextureHeader[slot]=header;
        } catch(const std::exception& error) {
            std::fprintf(stderr,"[NATIVE SKY TEXTURE] slot=3 fallback miss: %s\n",error.what());
            record.skyTexture[slot].reset();record.skyTextureHeader[slot]=0;
        }
        need(bool(record.skyTexture[slot]),"Sky stage-3 bind has neither scene-copy nor fallback texture owner");
        if(immediate)s.backend.bindEngineTexture(slot,record.skyTexture[slot]);
    }
    // Only the two reflected material rows persist; inherited rows arrive
    // from the payload's current original replay uploads.
    record.skyMaterialVS[46]=vertex[46];record.skyMaterialVS[47]=vertex[47];record.skyMaterialInitialized=true;
    if(immediate)run.commit=s.backend.commitSky(*s.rigidVertex,*s.rigidPixel,vertex,pixel);
    record.privateModified.fill(0);std::memset(dirty.data(),0,dirty.size());
    run.active=false;run.committed=true;
    c.r3.u64=0;
}
void EngineEffects::skyParameterOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.require(base);
    auto& run=s.skyMaterial;auto& mesh=s.skyMesh;
    const bool immediate=mesh.immediate;
    need(currentContext==&c&&mesh.cpu==&c&&mesh.active&&run.active&&!run.filtered&&c.r1.u32==run.frame&&
         PPC_LOAD_U32(run.frame)==mesh.frame&&PPC_LOAD_U32(run.frame-8)==(immediate?0x826B55F4u:0x826B574Cu)&&
         c.r30.u32==s.rigidTyped&&(immediate?c.r28.u32:c.r27.u32)==run.material&&run.cursor<run.rows.size(),
         "Unqualified original sky material descriptor continuation");
    auto& record=s.find(s.rigidId);
    need(immediate?(s.rigidImmediate.active&&s.rigidImmediate.cpu==&c&&!record.recordingContext):
        (record.recordingContext&&s.runtime.engineDriver->recordingOwners().activePayload(mesh.context)==mesh.payload),
         "Sky material setter lost its native lifetime");
    const auto& row=run.rows[run.cursor];const auto packet=run.frame+0x50;
    need(c.r29.u32==row.offset&&PPC_LOAD_U32(packet)==s.rigidId&&PPC_LOAD_U32(packet+4)==row.handle&&
         PPC_LOAD_U32(packet+0x20)==row.value,"Original sky material callback packet differs");
    for(uint32_t j=0;j<6;++j)need(PPC_LOAD_U32(packet+8+4*j)==row.binding[j],"Original sky material binding snapshot differs");
    const uint32_t expected=row.type==1?0x8270BE7C:row.type==2?0x8270BDDC:row.type<=5?0x8270BBEC:
        row.type==6?0x8270BC90:row.type<=9?0x8270BAB0:row.type==10?0x8270BD38:0x8270BB50;
    const bool second=site==0x8270BCB0||site==0x8270BD58;
    if(second) {
        need(run.pendingSlot&&run.pendingSite==site,"Sky scalar conversion/store is out of order");
        c.r11.u64=run.pendingSlot;run.pendingSlot=run.pendingSite=0;++run.cursor;return;
    }
    need(site==expected&&!run.pendingSlot&&c.r11.u32==s.rigidId,"Sky value callback type differs");
    uint32_t destination;
    if(row.handle&1) {
        destination=sharedParameterStorage(s.rigidId,row.handle);const auto leaf=(row.handle>>1)&0x1FFFF;
        auto* byte=s.runtime.pointer(record.view.pool+leaf/8,1,true);*byte|=uint8_t(0x80>>(leaf&7));
    } else destination=s.edgeSlot(record,row.handle);
    if(row.type==6||row.type==10) {
        run.pendingSlot=destination;run.pendingSite=row.type==6?0x8270BCB0:0x8270BD58;
        return;
    }
    if(row.type==1){c.r10.u64=destination;c.r8.u64=0;}
    else {c.r11.u64=destination;c.r6.u64=0;}
    ++run.cursor;
}
void EngineEffects::rigidMaterialOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    if(state->monoId){monoOperation(c,base,site);return;}
    if(state->activeShadowId&&site==0x82C1DBA0){commitShadowDepth(c,base);return;}
    auto& s=*state;s.require(base);
    if(s.skinImmediate.active){skinMaterialOperation(c,base,site);return;}
    if(s.rigidId&&s.find(s.rigidId).view.source==0x82036448){skyMaterialOperation(c,base,site);return;}
    requireRigidSelection(s.rigidTyped);auto& mesh=s.rigidMesh;auto& run=s.rigidMaterial;
    auto& record=s.find(s.rigidId);const auto& v=record.view;
    const bool immediate=mesh.immediate;
    need(currentContext==&c&&mesh.active&&mesh.cpu==&c&&mesh.phase==3&&mesh.cursor<mesh.draws.size()&&
         (immediate?(s.rigidImmediate.active&&s.rigidImmediate.cpu==&c&&!record.recordingContext&&mesh.submeshUpdated):
          (record.recordingContext&&s.runtime.engineDriver->recordingOwners().activePayload(mesh.context)==mesh.payload)),
         "Rigid material has no active original recording mesh");
    const auto& submesh=mesh.draws[mesh.cursor];
    if(site==(immediate?0x826B54D0u:0x826B5618u)) {
        need(c.lastFunction==site&&uint32_t(c.lr)==(immediate?0x8270131Cu:0x82701528u)&&c.r1.u32==mesh.frame&&
             (immediate?c.r19.u32:c.r17.u32)==s.rigidTyped&&
             c.r31.u32==submesh.entry&&c.r3.u32==s.rigidTyped&&c.r4.u32==submesh.material&&!run.active&&!run.committed,
             "Unqualified original rigid material-loop entry");
        State::RigidMaterialRun next;next.material=submesh.material;next.frame=mesh.frame-0xC0;
        const auto count=(PPC_LOAD_U32(next.material+0xC)>>10)&31,rows=PPC_LOAD_U32(next.material+0x14),bindings=PPC_LOAD_U32(s.rigidTyped+0x28);
        if(count)s.runtime.pointer(rows,12*count,false);
        constexpr std::array<uint32_t,14> callbacks{0,0x8270BE50,0x8270BDB0,0x8270BBC0,0x8270BBC0,0x8270BBC0,
            0x8270BC60,0x8270BA80,0x8270BA80,0x8270BA80,0x8270BD08,0x8270BB20,0x8270BB20,0x8270BB20};
        for(uint32_t i=0;i<count;++i) {
            const auto bits=PPC_LOAD_U32(rows+12*i);if(!immediate&&(bits&0x01C00000)!=0x00400000)continue;
            State::RigidMaterialRun::Row row;row.offset=12*i;row.type=bits&255;row.value=PPC_LOAD_U32(rows+12*i+8);
            need(row.type&&row.type<callbacks.size()&&PPC_LOAD_U32(0x82CEFF88+8*row.type)==callbacks[row.type],
                 "Unqualified original rigid material value callback");
            const auto binding=bindings+24*((bits>>16)&63);s.runtime.pointer(binding,24,false);
            for(uint32_t j=0;j<6;++j)row.binding[j]=PPC_LOAD_U32(binding+4*j);row.handle=row.binding[0];
            const auto entries=(row.handle&1)?s.schema().parameters(true):record.metadata->parameters(false);
            const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& p){return p.handle==row.handle;});
            need(found!=entries.end()&&!(found->descriptorWords[0]&3),"Rigid material binding is not an owned leaf");
            const auto offset=16*(found->descriptorWords[1]&0xFFFF);
            const auto bytes=(row.handle&1)?s.schema().sharedDefaults().size():record.metadata->privateDefaults().size();
            need(offset<=bytes&&bytes-offset>=16&&((row.handle>>1)&0x1FFFF)<1024,"Rigid material leaf exceeds storage");
            const bool vector=(row.type>=3&&row.type<=5)||(row.type>=7&&row.type<=9)||row.type>=11;
            if(vector){need(!(row.value&15),"Rigid vector material input is not aligned for original LVX");s.runtime.pointer(row.value,16,false);}
            else if(row.type==6||row.type==10){need(!(row.value&3),"Rigid integer material input is not word aligned");s.runtime.pointer(row.value&~15u,16,false);}
            else s.runtime.pointer(row.value,row.type==1?8:4,false);
            next.rows.push_back(row);
        }
        next.active=true;run=std::move(next);return;
    }
    need((site==0x826B2F20||site==0x82C1DBA0)&&run.active&&c.r1.u32==run.frame&&
         PPC_LOAD_U32(run.frame)==mesh.frame&&PPC_LOAD_U32(run.frame+0xB8)==(immediate?0x8270131Cu:0x82701528u)&&
         uint32_t(c.lr)==(immediate?0x826B560Cu:0x826B5764u)&&c.r3.u32==v.identity&&c.r30.u32==s.rigidTyped&&
         (immediate?c.r28.u32:c.r27.u32)==run.material&&
         run.cursor==run.rows.size()&&!run.pendingSlot,"Original rigid material commit frame/order differs");
    s.validatePool(v.pool);
    need(word(record.metadata->body(),0x120)==1&&word(record.metadata->body(),0x124)==1&&PPC_LOAD_U32(v.pool+0x114)==1,
         "Rigid material dirty filter extent differs");
    auto dirty=std::span<uint8_t,128>(s.runtime.pointer(v.pool,128,true),128);
    std::array<uint8_t,128> prospectiveShared;std::copy(dirty.begin(),dirty.end(),prospectiveShared.begin());
    if(site==0x826B2F20)filterRigidDirty(prospectiveShared,
        std::span<const uint8_t,128>(s.runtime.pointer(v.pool+0x80,128,false),128));
    // 82C1E59C..E5D0 selects the union only when F+2B8 (our owned pool)
    // and global82D00F80 are nonzero. All eight downstream categories AND
    // that context's map with the SAME dirty qword (F+124 == 1). Prove
    // zero work BEFORE mutating either dirty bank, and recheck at SDK entry.
    // Original SDK pointer reads are not native ownership: no fake context,
    // pass-map substitution or successful shared resource bind is asserted.
    if(PPC_LOAD_U32(0x82D00F80)&&!rigidSharedCommitHasNoWork(prospectiveShared,word(record.metadata->body(),0x124))) {
        char why[320];std::snprintf(why,sizeof(why),
            "Rigid recording union shared-map has nonempty filtered work: site=%08X pool=%08X global=%08X words=%u dirty64=%016llX eligibility64=%016llX filtered64=%016llX; mapping is not yet qualified",
            site,v.pool,PPC_LOAD_U32(0x82D00F80),word(record.metadata->body(),0x124),
            static_cast<unsigned long long>(doubleWord(dirty,0)),
            static_cast<unsigned long long>(doubleWord(std::span<const uint8_t>(s.runtime.pointer(v.pool+0x80,8,false),8),0)),
            static_cast<unsigned long long>(doubleWord(prospectiveShared,0)));
        throw Failure(why);
    }
    if(isVfxRigidSource(v.source)) {
        need(immediate,"VFX rigid material requires its original immediate draw lifetime");
        if(site==0x826B2F20) {
            need(!run.filtered,"VFX rigid dirty filter repeated");
            filterRigidDirty(record.privateModified,record.privateMask);
            std::copy(prospectiveShared.begin(),prospectiveShared.end(),dirty.begin());
            run.filtered=true;return;
        }
        need(run.filtered&&!run.committed&&c.lastFunction==0x82C1DBA0,"VFX rigid material commit was not filtered");
        const auto& staging=s.rigidImmediate.staging;
        need(staging.complete&&staging.phase==4&&!staging.prepared,"VFX rigid has no completed original stage upload");
        for(uint32_t stage=0;stage<2;++stage) {
            need(!std::memcmp(s.runtime.pointer(stage?0x82D6C450:0x82D6C0D0,896,false),staging.copies[stage].data(),896),
                 "VFX rigid original constants changed after upload");
            for(uint32_t i=0;i<2;++i)need(!std::memcmp(s.runtime.pointer(staging.buffers[2*stage+i]->address,896,false),
                 staging.copies[stage].data(),896),"VFX rigid stage copy changed");
        }
        Graphics::RigidVertexConstants vertex{};Graphics::RigidPixelConstants pixel{};
        for(uint32_t i=0;i<vertex.size();++i)for(uint32_t j=0;j<4;++j)vertex[i][j]=std::bit_cast<float>(word(staging.copies[0],16*i+4*j));
        for(uint32_t i=0;i<pixel.size();++i)for(uint32_t j=0;j<4;++j)pixel[i][j]=std::bit_cast<float>(word(staging.copies[1],16*i+4*j));
        auto words=record.view.defaultVectorWords;
        if(record.parameters)for(uint32_t i=0;i<words.size();++i)words[i]=PPC_LOAD_U32(record.parameters->address+4*i);
        // Project only material-eligible dirty leaves through the exact pass
        // map. Inherited WorldViewProjection, tint and UV registers retain the
        // original callback results copied above.
        for(bool shared:{false,true})for(const auto& entry:(shared?s.schema().parameters(true):record.metadata->parameters(false))) {
            if(entry.descriptorWords[0]&3)continue;
            const auto leaf=(entry.handle>>1)&0x1FFFF;
            need(leaf<1024,"VFX rigid dirty leaf exceeds its owner");
            if(!((shared?dirty[leaf/8]:record.privateModified[leaf/8])&(0x80>>(leaf&7))))continue;
            const auto binding=s.cachedPassBinding(record,0x0003FFFC,entry.handle);
            if(!binding.usage)continue;
            if(binding.usage==0x80) {
                need(!shared&&entry.handle==0x00500020&&!binding.lanes[0]&&binding.lanes[1]&&
                     binding.lanes[1]->start==0&&binding.lanes[1]->count==1&&words.size()>88,
                     "VFX rigid texture map differs");
                record.rigidBaseHeader=words[88];
                record.rigidBaseTexture=s.runtime.engineDriver->materialTexture(base,record.rigidBaseHeader);
                continue;
            }
            need(binding.usage==2,"VFX rigid material has an unsupported binding category");
            const auto offset=4*(entry.descriptorWords[1]&0xFFFF);
            for(uint32_t stage=0;stage<2;++stage)if(binding.lanes[stage]) {
                const auto range=*binding.lanes[stage];
                need(range.start+uint64_t(range.count)<=(stage?pixel.size():vertex.size()),"VFX rigid register map exceeds native bank");
                uint32_t source=0;
                if(shared)source=sharedParameterStorage(v.identity,entry.handle);
                else need(offset+uint64_t(range.count)*4<=words.size(),"VFX rigid private register source exceeds storage");
                for(uint32_t row=0;row<range.count;++row)for(uint32_t lane=0;lane<4;++lane) {
                    const auto bits=shared?PPC_LOAD_U32(source+16*row+4*lane):words[offset+4*row+lane];
                    const auto value=std::bit_cast<float>(bits);need(std::isfinite(value),"VFX rigid material is nonfinite");
                    if(stage)pixel[range.start+row][lane]=value;else vertex[range.start+row][lane]=value;
                }
            }
        }
        need(record.rigidBaseTexture&&record.rigidBaseHeader==words[88],"VFX rigid material has no current base texture");
        s.backend.bindEngineTexture(0,record.rigidBaseTexture);
        s.rigidImmediate.commit=s.backend.commitRigid(*s.rigidVertex,*s.rigidPixel,vertex,pixel);
        record.privateModified.fill(0);std::fill(dirty.begin(),dirty.end(),uint8_t(0));
        run.active=false;run.committed=true;c.r3.u64=0;return;
    }
    // Every shared mapping consumed by pass2620 is supplied by original replay
    // callbacks or the separate original texture transfer. Reject an eligibility
    // change instead of dropping an SDK shared upload or texture rebind.
    for(const auto name:{"g_ViewProjection","kWorldToViewPortTfmLight","kWorldToViewPortTfmCharLight","kShadowAmt","kIsShadowReceiver",
                         "kShadowDepthSampler","kShadowCharDepthSampler"}) {
        const auto handle=parameter(v.identity,name),leaf=(handle>>1)&0x1FFFF;
        need((handle&1)&&leaf<1024,"Rigid inherited shared mapping has an invalid shared handle");
        const auto binding=s.cachedPassBinding(record,s.alphaRigid()?0x0007FFFC:0x0003FFFC,handle);
        if(binding.usage)need(!(PPC_LOAD_U8(v.pool+0x80+leaf/8)&(0x80>>(leaf&7))),
            "Rigid inherited shared mapping remains material-eligible");
    }
    if(site==0x826B2F20) {
        need(!run.filtered,"Rigid material dirty filter repeated");
        filterRigidDirty(record.privateModified,record.privateMask);
        std::copy(prospectiveShared.begin(),prospectiveShared.end(),dirty.begin());
        run.filtered=true;return; // Retain original 826B2FD0 tail-call.
    }
    need(run.filtered&&!run.committed&&c.lastFunction==0x82C1DBA0,"Rigid SDK material commit was not filtered");
    const auto profile=rigidProfile(v.source);
    const auto materialRegisters=rigidPixelMaterialRows(v.source,s.alphaRigid());
    const auto vertexRegisters=rigidVertexMaterialRows(v.source,s.alphaRigid());
    if(profile.singleUv||profile.flipbook||profile.projtex) {
        const auto selected=s.alphaRigid()?0x0007FFFCu:0x0003FFFCu;
        const auto map=word(record.metadata->body(),(s.alphaRigid()?rigidAlphaPass(v.source).context:profile.context)+0x40);
        for(const auto& row:materialRegisters)if(row.leaf!=0xFFFFFFFFu) {
            const auto binding=s.cachedPassBinding(record,selected,word(record.metadata->body(),map+16*row.leaf));
            need(binding.usage==2&&!binding.lanes[0]&&binding.lanes[1]&&binding.lanes[1]->start==row.reg&&binding.lanes[1]->count==1,
                 "Single UV pixel material mapping differs");
        }
        for(const auto& row:vertexRegisters)if(row.leaf!=0xFFFFFFFFu) {
            const auto binding=s.cachedPassBinding(record,selected,word(record.metadata->body(),map+16*row.leaf));
            need(binding.usage==1&&binding.lanes[0]&&!binding.lanes[1]&&binding.lanes[0]->start==row.reg&&binding.lanes[0]->count==1,
                 "Single UV vertex material mapping differs");
        }
    }
    if(s.alphaRigid()&&isRigidFamilyAlphaSource(v.source)) {
        // These are distinct alpha shaders, with their own original map table.
        // Do not borrow opaque register targets merely because the source is
        // gloss, multitone or normalmap.
        const auto map=word(record.metadata->body(),rigidAlphaPass(v.source).context+0x40);
        for(const auto& row:materialRegisters)if(row.leaf!=0xFFFFFFFFu) {
            const auto handle=word(record.metadata->body(),map+16*row.leaf);
            const auto binding=s.cachedPassBinding(record,0x0007FFFC,handle);
            need(binding.usage==2&&!binding.lanes[0]&&binding.lanes[1]&&
                 binding.lanes[1]->start==row.reg&&binding.lanes[1]->count==1,"Rigid family alpha pixel material mapping differs");
        }
        if(profile.multitone||profile.normalmap) {
            const auto binding=s.cachedPassBinding(record,0x0007FFFC,0x005C0026);
            need(binding.usage==0&&!binding.lanes[0]&&!binding.lanes[1],"Rigid family alpha unexpectedly consumes noise/normal texture");
        }
    }
    if(!record.rigidMaterialInitialized) {
        for(const auto& row:materialRegisters)if(row.leaf!=0xFFFFFFFFu)
            need(record.privateModified[row.leaf/8]&(0x80>>(row.leaf&7)),"First rigid commit did not initialize all pixel material registers");
        for(const auto& row:vertexRegisters)if(row.leaf!=0xFFFFFFFFu)
            need(record.privateModified[row.leaf/8]&(0x80>>(row.leaf&7)),"First rigid commit did not initialize all vertex material registers");
    }
    auto words=record.view.defaultVectorWords;
    if(record.parameters)for(uint32_t i=0;i<words.size();++i)words[i]=PPC_LOAD_U32(record.parameters->address+4*i);
    auto prospective=record.rigidMaterialPS;projectRigidMaterial(words,record.privateModified,prospective,v.source,s.alphaRigid());
    auto prospectiveVS=record.rigidMaterialVS;projectRigidVertexMaterial(words,record.privateModified,prospectiveVS,v.source,s.alphaRigid());
    for(const auto& row:vertexRegisters)if(row.leaf!=0xFFFFFFFFu)
        for(float lane:prospectiveVS[row.reg])need(std::isfinite(lane),"Rigid vertex material contains nonfinite data");
    const bool rigidAlpha=s.rigidPixel->originalAddress()==0x8200E1BC;
    if(rigidAlpha) {
        need(immediate,"Rigid alpha recording constants require original replay qualification");
        const auto textureHandle=parameter(v.identity,"g_BaseSampler");
        const auto textureBinding=s.cachedPassBinding(record,0x0007FFFC,textureHandle);
        need(textureHandle==0x00500020&&textureBinding.usage==0x80&&!textureBinding.lanes[0]&&
             textureBinding.lanes[1]&&textureBinding.lanes[1]->start==0&&textureBinding.lanes[1]->count==1,
             "Rigid alpha base sampler mapping differs");
        if(record.privateModified[2]&0x80) {
            const auto header=words[88];
            record.rigidBaseTexture=s.runtime.engineDriver->materialTexture(base,header);
            record.rigidBaseHeader=header;
        }
        need(record.rigidBaseTexture&&record.rigidBaseHeader==words[88],"Rigid alpha current base texture missing");
        s.backend.bindEngineTexture(0,record.rigidBaseTexture);
        const auto eye=parameter(v.identity,"g_WorldEyePosition"),leaf=(eye>>1)&0x1FFFF;
        const auto binding=s.cachedPassBinding(record,0x0007FFFC,0x00080003);
        need((eye&1)&&leaf<1024&&binding.usage==2&&!binding.lanes[0]&&binding.lanes[1]&&
             binding.lanes[1]->start==4&&binding.lanes[1]->count==1,"Rigid alpha eye mapping differs");
        if(!(PPC_LOAD_U8(v.pool+0x80+leaf/8)&(0x80>>(leaf&7)))) {
            // The shared reflection exclusion is accumulated across materials.
            // 310b has eligibility=dirty=0: original PC4 is inherited from the
            // completed stage upload (WORLD_EYE_POS callback 8270C290 wrote it from the
            // active manager+230 at that time). Retail never rewrites an excluded PC4,
            // so the register keeps that value even after the camera has moved: the
            // manager's current eye is not compared (live bargainbin: 2 of 2 sweeps
            // staged (79.9,58.2,34.5) against a moved (77.7,52.3,27.0)).
            need(!(dirty[leaf/8]&(0x80>>(leaf&7))),"Excluded rigid alpha eye remains dirty");
            const auto& staged=s.rigidImmediate.staging.copies[1];
            for(uint32_t lane=0;lane<4;++lane)prospective[4][lane]=std::bit_cast<float>(word(staged,64+4*lane));
            record.rigidAlphaEyeInitialized=true;
        } else if(dirty[leaf/8]&(0x80>>(leaf&7))) {
            const auto storage=sharedParameterStorage(v.identity,eye);s.runtime.pointer(storage,16,false);
            for(uint32_t lane=0;lane<4;++lane)prospective[4][lane]=std::bit_cast<float>(PPC_LOAD_U32(storage+4*lane));
            record.rigidAlphaEyeInitialized=true;
        }
        need(record.rigidAlphaEyeInitialized,"Rigid alpha eye constant was not initialized");
        for(float lane:prospective[4])need(std::isfinite(lane),"Rigid alpha eye constant is nonfinite");
    }
    if(profile.uv) {
        constexpr std::array<std::string_view,2> names{"g_BaseSampler","g_BaseSampler2"};
        constexpr std::array<uint32_t,2> handles{0x00740032u,0x00780034u},textureWords{136u,152u};
        const auto selected=s.alphaRigid()?0x0007FFFCu:0x0003FFFCu;
        for(uint32_t texture=0;texture<2;++texture) {
            const auto handle=parameter(v.identity,names[texture]);need(handle==handles[texture],"UV material texture handle differs");
            const auto binding=s.cachedPassBinding(record,selected,handle);
            const auto stage=texture+(s.alphaRigid()?0u:1u);
            need(binding.usage==0x80&&!binding.lanes[0]&&binding.lanes[1]&&
                 binding.lanes[1]->start==stage&&binding.lanes[1]->count==1,"UV material texture stage differs");
            const uint32_t leaf=25+texture,header=words[textureWords[texture]];
            if(record.privateModified[leaf/8]&(0x80>>(leaf&7))) {
                record.rigidUvTexture[texture]=s.runtime.engineDriver->materialTexture(base,header);
                record.rigidUvTextureHeader[texture]=header;
            }
            need(record.rigidUvTexture[texture]&&record.rigidUvTextureHeader[texture]==header,"UV current material texture missing");
            if(immediate)s.backend.bindEngineTexture(stage,record.rigidUvTexture[texture]);
        }
    }
    if(profile.textured) {
        need(words.size()==profile.words,"Textured rigid private storage differs");
        const auto baseHandle=parameter(v.identity,"g_BaseSampler");
        const uint32_t baseLeaf=profile.flipbook?19u:profile.singleUv?23u:17u,baseWord=profile.flipbook?100u:profile.singleUv?116u:92u;
        need(baseHandle==(profile.flipbook?0x005C0026u:profile.singleUv?0x006C002Eu:0x00540022u),"Textured rigid base sampler handle changed");
        const auto baseBinding=s.cachedPassBinding(record,s.alphaRigid()?0x0007FFFCu:0x0003FFFCu,baseHandle);
        need(baseBinding.usage==0x80&&!baseBinding.lanes[0]&&baseBinding.lanes[1]&&
             baseBinding.lanes[1]->start==((s.alphaRigid()||profile.flipbook)?0u:2u)&&baseBinding.lanes[1]->count==1,
             "Textured rigid selected base sampler mapping differs");
        if(record.privateModified[baseLeaf/8]&(0x80>>(baseLeaf&7))) {
            const auto header=words[baseWord];
            auto texture=s.runtime.engineDriver->materialTexture(base,header);
            record.rigidBaseTexture=std::move(texture);record.rigidBaseHeader=header;
        }
        need(record.rigidBaseTexture&&record.rigidBaseHeader==words[baseWord],"Textured rigid has no current committed base texture");
        if(immediate) {
            const auto pixel=s.rigidPixel->originalAddress();
            s.backend.bindEngineTexture((profile.flipbook||pixel==0x82017E4C||pixel==0x8202C3BC||pixel==0x82044850||pixel==0x82052DF4||isRigidFamilyAlphaPixel(pixel))?0u:2u,record.rigidBaseTexture);
        }
    }
    if(profile.chocolate) {
        need(words.size()==profile.words,"Chocolate private storage differs");
        const bool alpha=s.alphaRigid();const auto selected=alpha?0x0007FFFCu:0x0003FFFCu;
        const auto map=word(record.metadata->body(),(alpha?rigidAlphaPass(v.source).context:profile.context)+0x40);
        for(const auto& row:materialRegisters)if(row.leaf!=0xFFFFFFFFu) {
            const auto binding=s.cachedPassBinding(record,selected,word(record.metadata->body(),map+16*row.leaf));
            need(binding.usage==2&&!binding.lanes[0]&&binding.lanes[1]&&binding.lanes[1]->start==row.reg&&binding.lanes[1]->count==1,
                 "Chocolate selected pixel material mapping differs");
        }
        for(const auto& row:vertexRegisters)if(row.leaf!=0xFFFFFFFFu) {
            const auto binding=s.cachedPassBinding(record,selected,word(record.metadata->body(),map+16*row.leaf));
            need(binding.usage==1&&binding.lanes[0]&&!binding.lanes[1]&&binding.lanes[0]->start==row.reg&&binding.lanes[0]->count==1,
                 "Chocolate selected vertex material mapping differs");
        }
        constexpr std::array<std::string_view,4> names{"g_PaletteSampler","g_BaseSampler","g_BaseSampler2","g_NormalMapSampler"};
        constexpr std::array<uint32_t,4> handles{0x00700030u,0x00740032u,0x00780034u,0x007C0036u};
        const uint32_t firstStage=alpha?0u:2u,textureCount=alpha?3u:2u;
        for(uint32_t i=0;i<handles.size();++i) {
            need(parameter(v.identity,names[i])==handles[i],"Chocolate sampler handle changed");
            const auto binding=s.cachedPassBinding(record,selected,handles[i]);
            if(!i||(!alpha&&i==3))need(binding.usage==0&&!binding.lanes[0]&&!binding.lanes[1],"Chocolate unused sampler consumes a texture stage");
            else need(binding.usage==0x80&&!binding.lanes[0]&&binding.lanes[1]&&binding.lanes[1]->start==firstStage+i-1&&binding.lanes[1]->count==1,
                      "Chocolate selected material sampler mapping differs");
        }
        constexpr uint32_t textureWords[]{136u,152u,168u};
        for(uint32_t texture=0;texture<textureCount;++texture) {
            const auto header=words[textureWords[texture]],leaf=25+texture;
            if(record.privateModified[leaf/8]&(0x80>>(leaf&7))) {
                record.chocolateTexture[texture]=s.runtime.engineDriver->materialTexture(base,header);
                record.chocolateTextureHeader[texture]=header;
            }
            need(record.chocolateTexture[texture]&&record.chocolateTextureHeader[texture]==header,"Chocolate current material texture missing");
            if(immediate)s.backend.bindEngineTexture(firstStage+texture,record.chocolateTexture[texture]);
        }
    }
    if(profile.projtex) {
        need(parameter(v.identity,"g_ProjTexSampler")==0x00580024,"Projected rigid sampler handle changed");
        const auto binding=s.cachedPassBinding(record,s.alphaRigid()?0x0007FFFCu:0x0003FFFCu,0x00580024);
        if(s.alphaRigid())need(binding.usage==0&&!binding.lanes[0]&&!binding.lanes[1],"Projected alpha consumes unused projection texture");
        else {
            need(binding.usage==0x80&&!binding.lanes[0]&&binding.lanes[1]&&binding.lanes[1]->start==3&&binding.lanes[1]->count==1,
                 "Projected rigid projection stage differs");
            if(record.privateModified[18/8]&(0x80>>(18&7))) {
                record.rigidNoiseTexture=s.runtime.engineDriver->materialTexture(base,words[108]);record.rigidNoiseHeader=words[108];
            }
            need(record.rigidNoiseTexture&&record.rigidNoiseHeader==words[108],"Projected rigid current projection texture missing");
            if(immediate)s.backend.bindEngineTexture(3,record.rigidNoiseTexture);
        }
    }
    if(profile.multitone&&!s.alphaRigid()) {
        need(parameter(v.identity,"g_NoiseSampler")==0x005C0026,"Multitone noise sampler handle changed");
        if(record.privateModified[19/8]&(0x80>>(19&7))) {
            const auto header=words[124];
            auto texture=s.runtime.engineDriver->materialTexture(base,header);
            record.rigidNoiseTexture=std::move(texture);record.rigidNoiseHeader=header;
        }
        need(record.rigidNoiseTexture&&record.rigidNoiseHeader==words[124],"Multitone has no current committed noise texture");
        if(immediate)s.backend.bindEngineTexture(3,record.rigidNoiseTexture);
    }
    if(profile.normalmap&&!s.alphaRigid()) {
        need(parameter(v.identity,"g_NormalMapSampler")==0x005C0026,"Normalmap normal sampler handle changed");
        if(record.privateModified[19/8]&(0x80>>(19&7))) {
            const auto header=words[124];
            auto texture=s.runtime.engineDriver->materialTexture(base,header);
            record.rigidNoiseTexture=std::move(texture);record.rigidNoiseHeader=header;
        }
        need(record.rigidNoiseTexture&&record.rigidNoiseHeader==words[124],"Normalmap has no current committed normal texture");
        if(immediate)s.backend.bindEngineTexture(3,record.rigidNoiseTexture);
    }
    for(const auto& row:materialRegisters)if(row.leaf!=0xFFFFFFFFu)
        for(float lane:prospective[row.reg])need(std::isfinite(lane),"Rigid material register contains nonfinite data");
    if(immediate) {
        const auto& staging=s.rigidImmediate.staging;
        need(staging.complete&&staging.phase==4&&!staging.prepared,"Rigid immediate material lost its original stage uploads");
        for(uint32_t stage=0;stage<2;++stage) {
            need(!std::memcmp(s.runtime.pointer(stage?0x82D6C450:0x82D6C0D0,896,false),staging.copies[stage].data(),896),
                 "Rigid immediate per-submesh staging changed without a qualified original upload");
            for(uint32_t i=0;i<2;++i)need(!std::memcmp(s.runtime.pointer(staging.buffers[2*stage+i]->address,896,false),
                 staging.copies[stage].data(),896),"Rigid immediate original upload storage changed");
        }
        Graphics::RigidVertexConstants vertex{};Graphics::RigidPixelConstants pixel{};
        for(uint32_t i=0;i<vertex.size();++i)for(uint32_t j=0;j<4;++j)
            vertex[i][j]=std::bit_cast<float>(word(staging.copies[0],16*i+4*j));
        for(uint32_t i=0;i<pixel.size();++i)for(uint32_t j=0;j<4;++j)
            pixel[i][j]=std::bit_cast<float>(word(staging.copies[1],16*i+4*j));
        for(const auto& row:materialRegisters)if(row.leaf!=0xFFFFFFFFu)pixel[row.reg]=prospective[row.reg];
        for(const auto& row:vertexRegisters)if(row.leaf!=0xFFFFFFFFu)vertex[row.reg]=prospectiveVS[row.reg];
        if(rigidAlpha)pixel[4]=prospective[4];
        // Allocate and bind the genuine native b0 owners before dirty-bank
        // mutation. Direct DrawIndexed validates this exact committed owner.
        s.rigidImmediate.commit=s.backend.commitRigid(*s.rigidVertex,*s.rigidPixel,vertex,pixel);
    }
    record.rigidMaterialPS=prospective;record.rigidMaterialVS=prospectiveVS;record.rigidMaterialInitialized=true;
    record.privateModified.fill(0);std::fill(dirty.begin(),dirty.end(),uint8_t(0));
    run.active=false;run.committed=true;
    // Cached draws retain material snapshots; their inherited bank is supplied
    // at replay. The immediate branch has already bound its actual constants.
    c.r3.u64=0;
}
void EngineEffects::rigidParameterOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.require(base);
    if(s.skinImmediate.active){skinParameterOperation(c,base,site);return;}
    if(s.rigidId&&s.find(s.rigidId).view.source==0x82036448){skyParameterOperation(c,base,site);return;}
    auto& run=s.rigidMaterial;auto& mesh=s.rigidMesh;
    const bool immediate=mesh.immediate;
    need(currentContext==&c&&mesh.cpu==&c&&mesh.active&&run.active&&!run.filtered&&c.r1.u32==run.frame&&
         PPC_LOAD_U32(run.frame)==mesh.frame&&PPC_LOAD_U32(run.frame-8)==(immediate?0x826B55F4u:0x826B574Cu)&&
         c.r30.u32==s.rigidTyped&&(immediate?c.r28.u32:c.r27.u32)==run.material&&run.cursor<run.rows.size(),
         "Unqualified original rigid material descriptor continuation");
    auto& record=s.find(s.rigidId);
    need(immediate?(s.rigidImmediate.active&&s.rigidImmediate.cpu==&c&&!record.recordingContext):
        (record.recordingContext&&s.runtime.engineDriver->recordingOwners().activePayload(mesh.context)==mesh.payload),
        "Rigid material setter lost its native lifetime");
    const auto& row=run.rows[run.cursor];const auto packet=run.frame+0x50;
    need(c.r29.u32==row.offset&&PPC_LOAD_U32(packet)==s.rigidId&&PPC_LOAD_U32(packet+4)==row.handle&&
         PPC_LOAD_U32(packet+0x20)==row.value,"Original rigid material callback packet differs");
    for(uint32_t j=0;j<6;++j)need(PPC_LOAD_U32(packet+8+4*j)==row.binding[j],"Original rigid material binding snapshot differs");
    const uint32_t expected=row.type==1?0x8270BE7C:row.type==2?0x8270BDDC:row.type<=5?0x8270BBEC:
        row.type==6?0x8270BC90:row.type<=9?0x8270BAB0:row.type==10?0x8270BD38:0x8270BB50;
    const bool second=site==0x8270BCB0||site==0x8270BD58;
    if(second) {
        need(run.pendingSlot&&run.pendingSite==site,"Rigid scalar conversion/store is out of order");
        c.r11.u64=run.pendingSlot;run.pendingSlot=run.pendingSite=0;++run.cursor;return;
    }
    need(site==expected&&!run.pendingSlot&&c.r11.u32==s.rigidId,"Rigid value callback type differs");
    uint32_t destination;
    if(row.handle&1) {
        destination=sharedParameterStorage(s.rigidId,row.handle);const auto leaf=(row.handle>>1)&0x1FFFF;
        auto* byte=s.runtime.pointer(record.view.pool+leaf/8,1,true);*byte|=uint8_t(0x80>>(leaf&7));
    } else destination=s.edgeSlot(record,row.handle);
    if(row.type==6||row.type==10) {
        run.pendingSlot=destination;run.pendingSite=row.type==6?0x8270BCB0:0x8270BD58;
        return; // Original vcfsx/vcfux executes between the two hooks.
    }
    if(row.type==1){c.r10.u64=destination;c.r8.u64=0;} // Preserve actual loaded texture identity in r6.
    else {c.r11.u64=destination;c.r6.u64=0;}
    ++run.cursor; // The next original instruction owns the value store.
}
void EngineEffects::rigidReplayOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    // A completed recorded rigid replay keeps its run state until the next replay starts or its
    // payload retires (requireRigidReplayFinished). Only an unprepared run is staging, so only that
    // one claims these hooks; a finished one must not misroute a later recorded mono upload.
    if(state->monoId&&!(state->rigidReplay.cpu&&!state->rigidReplay.prepared)){monoOperation(c,base,site);return;}
    auto& s=*state;s.require(base);
    if(s.skinImmediate.active){skinReplayOperation(c,base,site);return;}
    const bool immediate=s.rigidImmediate.active;
    auto& run=immediate?s.rigidImmediate.staging:s.rigidReplay;
    need(currentContext==&c&&run.cpu==&c&&run.packet&&!run.prepared,
         "Rigid replay staging has no original replay association");
    const auto* cachedPayload=immediate?nullptr:&s.requireRigidPayload(run.packet,run.payload);
    const auto effect=immediate?s.rigidId:cachedPayload->effect;
    auto& record=s.find(effect);
    if(immediate)need(s.rigidImmediate.activated&&s.rigidImmediate.cpu==&c&&s.rigidImmediate.frame==run.entrySP&&
        run.typed==s.rigidTyped&&run.context==record.contextIdentity&&!record.recordingContext,
        "Rigid immediate staging lost its effect/context owner");
    else need(cachedPayload->typed==run.typed&&cachedPayload->context==run.context,"Rigid replay staging lost its effect/context owner");
    if(site==0x826B6068) {
        need(!immediate&&c.lastFunction==site&&uint32_t(c.lr)==0x82740328&&c.r1.u32==run.entrySP&&c.r31.u32==run.packet&&
             c.r3.u32==record.view.manager&&!run.shared&&!run.callbacks,"Unqualified original replay shared update");
        s.validatePool(record.view.pool);
        need(PPC_LOAD_U32(record.view.manager+0x18)==record.view.pool,"Original replay shared pool association differs");
        const auto first=PPC_LOAD_U32(record.view.manager+0x224),second=PPC_LOAD_U32(record.view.manager+0x228);
        if(first&&second){(void)sharedParameterStorage(effect,first);(void)sharedParameterStorage(effect,second);}
        run.shared=true;return; // Retain 826B6068 -> 826B4728, including dirty bits and manager copies.
    }
    if(site==0x826B5770) {
        need(c.lastFunction==site&&uint32_t(c.lr)==(immediate?0x82740198u:0x8274033Cu)&&c.r1.u32==run.entrySP&&c.r31.u32==run.packet&&
             c.r3.u32==run.typed&&c.r4.u32==PPC_LOAD_U32(run.packet)&&c.r5.u32==PPC_LOAD_U32(run.packet+4)&&
             c.r6.u32==PPC_LOAD_U32(run.packet+8)&&(immediate?!run.shared:run.shared)&&!run.callbacks,
             "Unqualified original rigid replay callback entry");
        const auto first=PPC_LOAD_U32(record.view.manager+0x224),second=PPC_LOAD_U32(record.view.manager+0x228);
        if(!immediate&&first&&second) {
            const std::array<uint32_t,2> slots{sharedParameterStorage(effect,first),sharedParameterStorage(effect,second)};
            for(uint32_t i=0;i<8;++i)need(PPC_LOAD_U32(slots[i/4]+4*(i%4))==PPC_LOAD_U32(0x820B7120+4*i)&&
                PPC_LOAD_U32(record.view.manager+0x240+4*i)==PPC_LOAD_U32(0x820B7120+4*i),"Original shared replay update did not complete");
        }
        const auto count=PPC_LOAD_U32(run.typed+0x34),rows=PPC_LOAD_U32(run.typed+0x30);
        need(count<=64,"Rigid replay classification extent is unqualified");if(count)s.runtime.pointer(rows,28*count,false);
        s.runtime.pointer(0x82D6C0D0,896,true);s.runtime.pointer(0x82D6C450,896,true);
        run.callbacks=true;return; // Original per-object callbacks compute all staging values.
    }
    if(site==0x826B3270) {
        need(c.lastFunction==site&&uint32_t(c.lr)==0x826B589C&&run.callbacks&&!run.upload&&
             c.r1.u32==run.entrySP-0xD0&&PPC_LOAD_U32(c.r1.u32)==run.entrySP&&
             PPC_LOAD_U32(c.r1.u32+0xC8)==(immediate?0x82740198u:0x8274033Cu)&&c.r30.u32==run.typed&&
             !c.r3.u32&&c.r4.u32==56&&!c.r5.u32&&c.r6.u32==56,
             "Original rigid replay must upload both complete 56-register banks");
        // Actual owned CPU upload destinations. Original memcpy and VMX copy
        // execute into both banks, with neither an FX-shaped object nor an SDK
        // command packet. They are checked before any native replay publication.
        // TEMPORARY buffers are owner-scoped pooled (224 words each): fresh
        // poison on every lease, exact size bound, exclusive until run closure.
        run.buffers=s.acquireTempUpload({224,224,224,224});
        run.uploadSP=c.r1.u32-0x90;run.upload=true;return;
    }
    need(run.upload&&!run.complete&&c.r1.u32==run.uploadSP&&PPC_LOAD_U32(run.uploadSP)==run.entrySP-0xD0&&
         PPC_LOAD_U32(run.uploadSP+0x88)==0x826B589C,"Original rigid stage-upload frame differs");
    if(site==0x826B32E4||site==0x826B3330) {
        const uint32_t stage=site==0x826B32E4?0:1;
        need(run.phase==2*stage&&c.r3.u32==run.context&&c.r4.u32==stage&&!c.r5.u32&&c.r6.u32==0x82D6C7D0&&
             c.r7.u32==0x82D6C7E0&&c.r8.u32==56,"Original rigid stage-upload SDK arguments/order differ");
        const auto source=stage?0x82D6C450u:0x82D6C0D0u;
        std::memcpy(run.copies[stage].data(),s.runtime.pointer(source,896,false),896);
        PPC_STORE_U32(c.r6.u32,run.buffers[2*stage]->address);PPC_STORE_U32(c.r7.u32,run.buffers[2*stage+1]->address);
        ++run.phase;c.r3.u64=0;c.lr=site+4;return;
    }
    need(site==0x826B3314||site==0x826B3364,"Unknown rigid replay staging boundary");
    const uint32_t stage=site==0x826B3314?0:1;
    need(run.phase==2*stage+1&&PPC_LOAD_U32(0x82D6C7D0)==run.buffers[2*stage]->address&&
         PPC_LOAD_U32(0x82D6C7E0)==run.buffers[2*stage+1]->address,"Original rigid upload destination publication changed");
    const auto source=stage?0x82D6C450u:0x82D6C0D0u;
    need(!std::memcmp(s.runtime.pointer(source,896,false),run.copies[stage].data(),896),"Rigid staging changed during original upload");
    for(uint32_t i=0;i<2;++i)need(!std::memcmp(s.runtime.pointer(run.buffers[2*stage+i]->address,896,false),run.copies[stage].data(),896),
        "Original rigid stage upload did not copy all 56 registers");
    ++run.phase;if(stage==1)run.complete=true;
    // Observation only: original context getters and both epilogues still run.
}
void EngineEffects::prepareRigidReplay(uint32_t typed,uint32_t packet,uint32_t payloadIdentity) {
    auto& s=*state;s.require(s.runtime.base);auto& run=s.rigidReplay;
    need(run.typed==typed&&run.packet==packet&&run.payload==payloadIdentity&&run.cpu==currentContext&&run.complete&&!run.prepared&&run.phase==4&&
         currentContext&&currentContext->r1.u32==run.entrySP,
         "Rigid replay requires both completed original 56-register stage uploads");
    const auto& payload=s.requireRigidPayload(run.packet,run.payload);
    const auto receipt=s.backend.recordingPayloadReceipt(payload.native);
    need(payload.typed==typed&&payload.context==run.context&&receipt.executions==run.executions&&
         receipt.executedDraws==run.executedDraws,
         "Rigid replay constant lifetime/context differs");
    for(uint32_t stage=0;stage<2;++stage) {
        need(!std::memcmp(s.runtime.pointer(stage?0x82D6C450:0x82D6C0D0,896,false),run.copies[stage].data(),896),
             "Original rigid staging changed before payload execution");
        for(uint32_t i=0;i<2;++i)need(!std::memcmp(s.runtime.pointer(run.buffers[2*stage+i]->address,896,false),run.copies[stage].data(),896),
            "Original rigid upload storage changed before payload execution");
    }
    if(payload.monoReplayValues) {
        Graphics::MonoConstants vertex{};
        for(uint32_t i=0;i<56;++i)for(uint32_t j=0;j<4;++j)vertex[i][j]=std::bit_cast<float>(word(run.copies[0],16*i+4*j));
        s.backend.updateMonoReplayConstants(payload.monoReplayValues,vertex);
    } else if(payload.skyReplayValues) {
        Graphics::SkyVertexConstants vertex{};Graphics::SkyPixelConstants pixel{};
        for(uint32_t i=0;i<56;++i)for(uint32_t j=0;j<4;++j)vertex[i][j]=std::bit_cast<float>(word(run.copies[0],16*i+4*j));
        for(uint32_t i=0;i<pixel.size();++i)for(uint32_t j=0;j<4;++j)pixel[i][j]=std::bit_cast<float>(word(run.copies[1],16*i+4*j));
        s.backend.updateSkyReplayConstants(payload.skyReplayValues,vertex,pixel);
    } else {
    Graphics::RigidVertexConstants vertex;Graphics::RigidPixelConstants pixel;
    for(uint32_t i=0;i<vertex.size();++i)for(uint32_t j=0;j<4;++j)vertex[i][j]=std::bit_cast<float>(word(run.copies[0],16*i+4*j));
    for(uint32_t i=0;i<pixel.size();++i)for(uint32_t j=0;j<4;++j)pixel[i][j]=std::bit_cast<float>(word(run.copies[1],16*i+4*j));
    // Copies actual CPU staging, including original incidental/dead lanes.
    // The backend merges the payload mask with each draw's material snapshot.
    s.backend.updateRigidReplayConstants(payload.replayValues,vertex,pixel);
    }
    run.prepared=true;
    {static thread_local uint32_t rigidReplayConstantsSample{};
    if(sampleHotLog(rigidReplayConstantsSample))
        std::fprintf(stderr,"[NATIVE RIGID REPLAY CONSTANTS] typed=%08X payload=%08X VS/PS=56+56 original registers; both CPU copies verified\n",typed,payload.id);}
}
void EngineEffects::requireCachedRecordRetirement(uint32_t id,uint32_t typed,uint32_t object,uint32_t metadata,
    const std::shared_ptr<Graphics::NativeRecordingPayload>& native) const {
    auto& s=*state;auto* base=s.runtime.base;s.require(base);
    need(!s.rigidMesh.active&&!s.skyMesh.active&&!s.skinMesh.active&&!s.rigidTextures.active&&!s.skinTextures.active&&
         !s.rigidMaterial.active&&!s.skyMaterial.active&&!s.skinMaterial.active&&
         !s.rigidImmediate.active&&!s.skinImmediate.active&&!s.monoImmediate.active&&!s.monoRecording.active&&!s.skinReplay.cpu,
         "Cached record retirement overlaps an active effect operation");
    s.requireRigidReplayFinished();
    const auto at=s.rigidPayloads.find(id);
    need(at!=s.rigidPayloads.end()&&!s.skinPayloads.contains(id),"Cached record retirement has no qualified rigid/sky effect owner");
    const auto& p=at->second;const auto& r=s.find(p.effect);const auto& v=r.view;
    s.runtime.pointer(typed,0xA8,false);s.runtime.pointer(v.wrapper,0x30,false);
    need(p.id==id&&p.typed==typed&&p.object==object&&p.metadata==metadata&&p.native==native&&
         v.phase==Phase::Reflected&&r.typedReflections==1&&!r.recordingContext&&r.contextIdentity==p.context&&
         (isRigidSource(v.source)||v.source==0x82036448||v.source==0x8211F480)&&
         (v.source==0x8211F480?(p.monoReplayValues&&!p.skyReplayValues&&!p.replayValues):
          v.source==0x82036448?(p.skyReplayValues&&!p.replayValues&&!p.monoReplayValues):(p.replayValues&&!p.skyReplayValues&&!p.monoReplayValues))&&
         p.vertex&&p.pixel&&PPC_LOAD_U32(typed)==(v.source==0x8211F480?0x8215020Cu:0x820616C0u)&&PPC_LOAD_U32(typed+0x1C)==p.effect&&
         PPC_LOAD_U32(typed+0x10)==v.manager&&PPC_LOAD_U32(typed+0x18)==v.wrapper&&
         PPC_LOAD_U32(v.wrapper+0x10)==p.effect&&PPC_LOAD_U32(v.wrapper+0xC)==v.manager&&
         s.rigidMesh.payloadId!=id&&s.skyMesh.payloadId!=id&&s.skinMesh.payloadId!=id,
         "Cached record retirement differs from retained effect/object/metadata ownership");
    const auto receipt=s.backend.recordingPayloadReceipt(native);
    need(receipt.state==Graphics::NativeRecordingPayloadState::Sealed&&receipt.recordedDraws==p.draws&&
         receipt.ownedDataBytes==p.bytes,"Cached record retirement has an incomplete native receipt");
    if(p.monoReplayValues)s.backend.validateMonoShaders(*p.vertex,*p.pixel);
    else s.backend.validateRigidShaders(*p.vertex,*p.pixel);
}
void EngineEffects::retireCachedRecord(uint32_t id,uint32_t typed,uint32_t object,uint32_t metadata,
    const std::shared_ptr<Graphics::NativeRecordingPayload>& native) {
    requireCachedRecordRetirement(id,typed,object,metadata,native);
    auto& s=*state;const auto& p=s.rigidPayloads.at(id);
    if(p.monoReplayValues)s.backend.releaseMonoReplayConstants(p.monoReplayValues);
    else if(p.replayValues)s.backend.releaseRigidReplayConstants(p.replayValues);
    else s.backend.releaseSkyReplayConstants(p.skyReplayValues);
    if(s.rigidReplay.payload==id){s.recycleTempUpload(s.rigidReplay.buffers);s.rigidReplay={};}
    s.rigidPayloads.erase(id);
}

void EngineEffects::requireRigidRecordingComplete(uint32_t typed) const {
    if(state->monoId) {
        auto& s=*state;requireRigidSelection(typed);const auto& record=s.find(s.monoId);const auto& run=s.monoRecording;const auto& mesh=s.monoMesh;
        need(record.recordingContext&&run.complete&&!run.active&&run.live&&run.payloadId&&!s.rigidPayloads.contains(run.payloadId)&&
             !run.materialActive&&!run.materialCommitted&&s.monoTextures.complete&&!s.monoTextures.active&&mesh.phase==4&&
             !mesh.draws.empty()&&mesh.cursor==mesh.draws.size()&&record.contextIdentity==run.context&&
             s.runtime.engineDriver->recordingOwners().activePayload(run.context)==run.payload,
             "Mono recording has not completed its original material/mesh loop");
        const auto receipt=s.backend.recordingPayloadReceipt(run.payload);
        need(receipt.state==Graphics::NativeRecordingPayloadState::Recording&&receipt.recordedDraws==mesh.cursor&&
             !receipt.executions&&!receipt.executedDraws,"Mono recorded draw receipt differs");return;
    }
    auto& s=*state;s.require(s.runtime.base);requireRigidSelection(typed);const auto& record=s.find(s.rigidId);const auto& mesh=s.rigidMesh;
    if(record.view.source==0x82036448) {
        const auto& mesh=s.skyMesh;
    need(record.recordingContext&&mesh.replayValues&&mesh.payloadId&&!s.rigidPayloads.contains(mesh.payloadId)&&
         record.skyMaterialInitialized&&s.rigidTextures.complete&&
         mesh.complete&&!mesh.active&&mesh.phase==3&&!mesh.draws.empty()&&mesh.cursor==mesh.draws.size()&&
         !s.skyMaterial.active&&!s.skyMaterial.committed&&record.contextIdentity==mesh.context&&
         s.runtime.engineDriver->recordingOwners().activePayload(mesh.context)==mesh.payload,
         "Sky recording has not completed its original material/mesh loop");
    const auto receipt=s.backend.recordingPayloadReceipt(mesh.payload);
    need(receipt.state==Graphics::NativeRecordingPayloadState::Recording&&receipt.recordedDraws==mesh.cursor&&
         !receipt.executions&&!receipt.executedDraws,"Sky recording native draw receipt differs");        return;
    }
    need(record.recordingContext&&mesh.replayValues&&mesh.payloadId&&!s.rigidPayloads.contains(mesh.payloadId)&&
         record.rigidMaterialInitialized&&s.rigidTextures.complete&&
         mesh.complete&&!mesh.active&&mesh.phase==3&&!mesh.draws.empty()&&mesh.cursor==mesh.draws.size()&&
         !s.rigidMaterial.active&&!s.rigidMaterial.committed&&record.contextIdentity==mesh.context&&
         s.runtime.engineDriver->recordingOwners().activePayload(mesh.context)==mesh.payload,
         "Rigid recording has not completed its original material/mesh loop");
    const auto receipt=s.backend.recordingPayloadReceipt(mesh.payload);
    need(receipt.state==Graphics::NativeRecordingPayloadState::Recording&&receipt.recordedDraws==mesh.cursor&&
         !receipt.executions&&!receipt.executedDraws,"Rigid recording native draw receipt differs");
}
void EngineEffects::associateRecordingContext(PPCContext& c,uint8_t* base) {
    need(!state->rigidImmediate.active,"Recording context association overlaps an immediate rigid lifetime");
    auto& s=*state;s.require(base);
    const auto caller=uint32_t(c.lr);
    if(s.monoId&&(caller==0x8274050C||caller==0x82740674)) {
        need(currentContext==&c&&c.lastFunction==0x82C1CFE8&&c.r3.u32==s.monoId&&s.monoRecordingPath&&
             c.r1.u32==s.monoFrame&&PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0xD0&&c.r31.u32==s.monoPacket,
             "Original mono recording context frame differs");
        requireRigidSelection(s.monoTyped);auto& record=s.find(s.monoId);auto& owners=s.runtime.engineDriver->recordingOwners();
        const auto previous=PPC_LOAD_U32(s.monoPacket+0x14);s.runtime.engineDriver->requireContext(previous);
        if(caller==0x8274050C) {
            need(c.r29.u32==s.monoId&&c.r28.u32==PPC_LOAD_U32(0x82D09784)&&record.contextIdentity==previous&&
                 !record.recordingContext&&!s.monoRecording.active&&(!s.monoMesh.native||s.monoMesh.phase==4),
                 "Mono recording context changed its previous owner");
            s.requireRigidReplayFinished();owners.requireActiveContext(c.r4.u32);s.runtime.engineDriver->requireRecordingStateSeed(c.r4.u32);
            auto lease=owners.nativeContext(c.r28.u32,c.r4.u32).lock();need(bool(lease),"Mono recording context has no owner lease");
            s.backend.validateRecordingContext(lease);s.recycleTempUpload(s.rigidReplay.buffers);s.rigidReplay={};
            record.recordingContext=std::move(lease);record.contextIdentity=c.r4.u32;c.r3.u64=0;return;
        }
        const auto& run=s.monoRecording;const auto& mesh=s.monoMesh;
        need(c.r4.u32==previous&&run.complete&&!run.active&&run.cpu==&c&&run.packet==s.monoPacket&&run.typed==s.monoTyped&&
             record.recordingContext&&record.contextIdentity==run.context&&run.previous==previous&&mesh.phase==4&&
             mesh.cursor==mesh.draws.size()&&run.live&&run.payloadId&&!s.rigidPayloads.contains(run.payloadId),
             "Mono restore has no completed recording lease");
        owners.requireFinishedRecording(s.monoTyped,previous);const auto receipt=s.backend.recordingPayloadReceipt(run.payload);
        need(receipt.state==Graphics::NativeRecordingPayloadState::Sealed&&receipt.recordedDraws==mesh.cursor&&
             !receipt.executions&&!receipt.executedDraws,"Mono restore requires its sealed unexecuted payload");
        State::RigidPayload payload;payload.id=run.payloadId;payload.effect=s.monoId;payload.typed=s.monoTyped;payload.context=previous;
        payload.metadata=mesh.metadata;payload.object=mesh.object;payload.camera=s.monoCamera;payload.shadowOwner=s.monoTextures.shadowOwner;
        payload.draws=mesh.cursor;payload.bytes=receipt.ownedDataBytes;payload.vertex=s.monoVertex;payload.pixel=s.monoPixel;
        payload.native=run.payload;payload.monoReplayValues=run.live;
        need(s.rigidPayloads.emplace(payload.id,std::move(payload)).second,"Mono retained payload identity already exists");
        record.recordingContext.reset();record.contextIdentity=previous;owners.completeEffectRestore(s.monoTyped,previous);
        s.monoRecording={};s.monoTextures={};c.r3.u64=0;return;
    }
    if(caller==0x8274031C||caller==0x8274050C)
        need(!s.skyMesh.active&&!s.skyMaterial.active&&!s.skyMaterial.committed,"Context association overlaps a sky traversal");
    if(caller==0x8274031C) {
        need(currentContext==&c&&c.lastFunction==0x82C1CFE8&&c.r1.u32>=0x200&&!(c.r1.u32&15)&&
             PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0x70,"Unqualified original rigid replay context frame");
        s.runtime.pointer(c.r31.u32,0x24,false);s.requireRigidReplayFinished();
        const auto& payload=s.requireRigidPayload(c.r31.u32,c.r30.u32);
        // A completed immediate session is not a live recording: replay runs
        // from its retained payload with its own constants, so only an ACTIVE
        // mesh/texture session overlaps. (reach-game-237: stale immediate cpu
        // from an earlier completed packet vs replay of payload 00600004.)
        if(!(c.r3.u32==payload.effect&&c.r4.u32==payload.context&&
             (!s.rigidMesh.cpu||(s.rigidMesh.complete&&!s.rigidMesh.active))&&
             (!s.rigidTextures.cpu||(s.rigidTextures.complete&&!s.rigidTextures.active))&&
             !s.rigidMaterial.active&&!s.rigidMaterial.committed))
            std::fprintf(stderr,"[NATIVE RIGID REPLAY] packet=%08X payload=%08X typed=%08X r3=%08X effect=%08X r4=%08X context=%08X meshCpu=%d meshDone=%u/%u texCpu=%d texDone=%u/%u matA=%u matC=%u\n",
                c.r31.u32,payload.id,payload.typed,c.r3.u32,payload.effect,c.r4.u32,payload.context,
                bool(s.rigidMesh.cpu),s.rigidMesh.complete,!s.rigidMesh.active,
                bool(s.rigidTextures.cpu),s.rigidTextures.complete,!s.rigidTextures.active,
                s.rigidMaterial.active,s.rigidMaterial.committed);
        need(c.r3.u32==payload.effect&&c.r4.u32==payload.context&&
             (!s.rigidMesh.cpu||(s.rigidMesh.complete&&!s.rigidMesh.active))&&
             (!s.rigidTextures.cpu||(s.rigidTextures.complete&&!s.rigidTextures.active))&&
             !s.rigidMaterial.active&&!s.rigidMaterial.committed,"Rigid replay overlaps a recording or differs from its retained owner");
        s.runtime.engineDriver->requireContext(payload.context);
        const auto receipt=s.backend.recordingPayloadReceipt(payload.native);
        s.recycleTempUpload(s.rigidReplay.buffers);s.rigidReplay={};auto& replay=s.rigidReplay;
        replay.cpu=&c;replay.packet=c.r31.u32;replay.payload=payload.id;replay.typed=payload.typed;
        replay.context=payload.context;replay.entrySP=c.r1.u32;
        replay.executions=receipt.executions;replay.executedDraws=receipt.executedDraws;
        c.r3.u64=0;return;
    }
    if(caller==0x82740674) {
        need(currentContext==&c&&c.lastFunction==0x82C1CFE8&&s.rigidId&&c.r3.u32==s.rigidId&&
             c.r1.u32>=0x200&&!(c.r1.u32&15)&&PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0xD0,
             "Unqualified original rigid context restore frame");
        s.runtime.pointer(c.r31.u32,0x24,false);auto& record=s.find(s.rigidId);
        const auto typed=PPC_LOAD_U32(c.r31.u32+0x18),previous=PPC_LOAD_U32(c.r31.u32+0x14);
        need(typed==s.rigidTyped&&c.r4.u32==previous,"Original rigid context restore association differs");
        s.runtime.engineDriver->requireContext(previous);
        auto& owners=s.runtime.engineDriver->recordingOwners();
        if(record.view.source==0x82036448) {
        const auto& mesh=s.skyMesh;
        need(record.recordingContext&&record.contextIdentity==mesh.context&&mesh.complete&&!mesh.active&&mesh.replayValues&&
             mesh.packet==c.r31.u32&&mesh.entrySP==c.r1.u32&&mesh.cursor==mesh.draws.size()&&mesh.cursor&&
             s.rigidTextures.complete&&!s.rigidTextures.active&&!s.skyMaterial.active&&!s.skyMaterial.committed&&
             PPC_LOAD_U32(c.r31.u32)==mesh.metadata&&PPC_LOAD_U32(c.r31.u32+4)==mesh.object&&
             PPC_LOAD_U32(c.r31.u32+8)==s.rigidCamera&&PPC_LOAD_U32(c.r31.u32+0x20)==s.rigidTextures.shadowOwner&&
             mesh.payloadId&&PPC_LOAD_U32(s.rigidTextures.manager+0x54)==mesh.payloadId&&!s.rigidPayloads.contains(mesh.payloadId),
             "Sky restore has no completed fresh recording lease");
        owners.requireFinishedRecording(typed,previous);
        const auto receipt=s.backend.recordingPayloadReceipt(mesh.payload);
        need(receipt.state==Graphics::NativeRecordingPayloadState::Sealed&&receipt.recordedDraws==mesh.cursor&&
             !receipt.executions&&!receipt.executedDraws,"Sky restore requires its sealed, unexecuted native draws");
        State::RigidPayload payload;
        payload.id=mesh.payloadId;payload.effect=s.rigidId;payload.typed=typed;payload.context=previous;
        payload.metadata=mesh.metadata;payload.object=mesh.object;payload.camera=s.rigidCamera;
        payload.shadowOwner=s.rigidTextures.shadowOwner;payload.draws=mesh.cursor;payload.bytes=receipt.ownedDataBytes;
        payload.vertex=s.rigidVertex;payload.pixel=s.rigidPixel;payload.native=mesh.payload;payload.skyReplayValues=mesh.replayValues;
        need(s.rigidPayloads.emplace(payload.id,std::move(payload)).second,"Rigid retained payload identity already exists");
        record.recordingContext.reset();record.contextIdentity=previous;
        owners.completeEffectRestore(typed,previous);
        // CPU FX parameters and material accumulation survive the context swap.
        // Only the completed recording's transient traversal state is discarded.
        s.rigidTextures={};s.skyMesh={};s.skyMaterial={};
        c.r3.u64=0;return;
        }
        const auto& mesh=s.rigidMesh;
        need(record.recordingContext&&record.contextIdentity==mesh.context&&mesh.complete&&!mesh.active&&mesh.replayValues&&
             mesh.packet==c.r31.u32&&mesh.entrySP==c.r1.u32&&mesh.cursor==mesh.draws.size()&&mesh.cursor&&
             s.rigidTextures.complete&&!s.rigidTextures.active&&!s.rigidMaterial.active&&!s.rigidMaterial.committed&&
             PPC_LOAD_U32(c.r31.u32)==mesh.metadata&&PPC_LOAD_U32(c.r31.u32+4)==mesh.object&&
             PPC_LOAD_U32(c.r31.u32+8)==s.rigidCamera&&PPC_LOAD_U32(c.r31.u32+0x20)==s.rigidTextures.shadowOwner&&
             mesh.payloadId&&PPC_LOAD_U32(s.rigidTextures.manager+0x54)==mesh.payloadId&&!s.rigidPayloads.contains(mesh.payloadId),
             "Rigid restore has no completed fresh recording lease");
        owners.requireFinishedRecording(typed,previous);
        const auto receipt=s.backend.recordingPayloadReceipt(mesh.payload);
        need(receipt.state==Graphics::NativeRecordingPayloadState::Sealed&&receipt.recordedDraws==mesh.cursor&&
             !receipt.executions&&!receipt.executedDraws,"Rigid restore requires its sealed, unexecuted native draws");
        State::RigidPayload payload;
        payload.id=mesh.payloadId;payload.effect=s.rigidId;payload.typed=typed;payload.context=previous;
        payload.metadata=mesh.metadata;payload.object=mesh.object;payload.camera=s.rigidCamera;
        payload.shadowOwner=s.rigidTextures.shadowOwner;payload.draws=mesh.cursor;payload.bytes=receipt.ownedDataBytes;
        payload.vertex=s.rigidVertex;payload.pixel=s.rigidPixel;payload.native=mesh.payload;payload.replayValues=mesh.replayValues;
        need(s.rigidPayloads.emplace(payload.id,std::move(payload)).second,"Rigid retained payload identity already exists");
        record.recordingContext.reset();record.contextIdentity=previous;
        owners.completeEffectRestore(typed,previous);
        // CPU FX parameters and material accumulation survive the context swap.
        // Only the completed recording's transient traversal state is discarded.
        s.rigidTextures={};s.rigidMesh={};s.rigidMaterial={};
        c.r3.u64=0;return;
    }
    need(currentContext==&c&&c.lastFunction==0x82C1CFE8&&uint32_t(c.lr)==0x8274050C&&
         s.rigidId&&c.r3.u32==s.rigidId&&c.r29.u32==s.rigidId&&c.r1.u32>=0x200&&!(c.r1.u32&15)&&
         PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0xD0,"Unqualified original FX recording-context association");
    s.runtime.pointer(c.r31.u32,0x24,false);auto& record=s.find(c.r3.u32);
    const auto typed=PPC_LOAD_U32(c.r31.u32+0x18),previous=PPC_LOAD_U32(c.r31.u32+0x14);
    requireRigidSelection(typed);
    need(record.contextIdentity==previous&&!record.recordingContext&&c.r28.u32==PPC_LOAD_U32(0x82D09784),
         "Original FX recording-context previous ownership differs");
    s.requireRigidReplayFinished();
    // A completed immediate session is not a live one: the recording begin
    // overwrites mesh/texture state wholesale, so only an ACTIVE session
    // overlaps the new lease. Material must still be fully idle.
    if(!((!s.rigidMesh.cpu||(s.rigidMesh.complete&&!s.rigidMesh.active))&&
         (!s.rigidTextures.cpu||(s.rigidTextures.complete&&!s.rigidTextures.active))&&
         !s.rigidMaterial.active&&!s.rigidMaterial.committed))
        std::fprintf(stderr,"[NATIVE RIGID RECORD] packet=%08X meshCpu=%d meshDone=%u/%u texCpu=%d texDone=%u/%u matA=%u matC=%u\n",
            c.r31.u32,bool(s.rigidMesh.cpu),s.rigidMesh.complete,!s.rigidMesh.active,
            bool(s.rigidTextures.cpu),s.rigidTextures.complete,!s.rigidTextures.active,
            s.rigidMaterial.active,s.rigidMaterial.committed);
    need((!s.rigidMesh.cpu||(s.rigidMesh.complete&&!s.rigidMesh.active))&&
         (!s.rigidTextures.cpu||(s.rigidTextures.complete&&!s.rigidTextures.active))&&
         !s.rigidMaterial.active&&!s.rigidMaterial.committed,
         "Rigid recording still owns an unfinished mesh/material/texture session");
    s.runtime.engineDriver->requireContext(previous);
    auto& owners=s.runtime.engineDriver->recordingOwners();owners.requireActiveContext(c.r4.u32);
    s.runtime.engineDriver->requireRecordingStateSeed(c.r4.u32);
    auto lease=owners.nativeContext(c.r28.u32,c.r4.u32).lock();need(bool(lease),"Native FX recording context has no owned lifetime");
    s.backend.validateRecordingContext(lease);
    // Original82C1CFE8's nonzero->nonzero branch only transfers its context
    // reference. Shader resources, parameters and dirty masks survive intact.
    // Keep that lease on the native effect; its identity is not an SDK header.
    s.recycleTempUpload(s.rigidReplay.buffers);s.rigidReplay={};record.recordingContext=std::move(lease);record.contextIdentity=c.r4.u32;c.r3.u64=0;
    std::fprintf(stderr,"[NATIVE RIGID CONTEXT] FX=%08X context=%08X; owned nonnull context lease transferred, material data retained\n",record.view.identity,record.contextIdentity);
}
void EngineEffects::rigidTextureOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.require(base);
    if(s.monoImmediate.active){monoImmediateTextureOperation(c,base,site);return;}
    if(s.monoRecordingPath&&s.monoId){monoRecordingOperation(c,base,site);return;}
    if(s.skinImmediate.active) {
        // Both opaque skin sources retain the original three-shadow lookup.
        // Only dual skin consumes a row: character depth mapped to stage0.
        auto& record=s.find(s.skinId);
        const auto selectedTechnique=s.alphaSkin()?0x0007FFFCu:0x0003FFFCu;
        const auto passes=record.metadata->techniques();
        const auto passIt=std::find_if(passes.begin(),passes.end(),[&](const auto& pass){return pass.handle==selectedTechnique;});
        need(passIt!=passes.end(),"Selected skin texture-transfer pass is missing");
        const auto& skinPass=*passIt;
        const auto profile=skinProfile(record.view.source);
        if(site!=0x8273FF80) {
            const auto& run=s.skinTextures;
            need(currentContext==&c&&run.cpu==&c&&c.r1.u32==run.sp-0xC0&&
                 PPC_LOAD_U32(c.r1.u32)==run.sp&&PPC_LOAD_U32(run.sp-8)==0x827401C8&&
                 c.r29.u32==s.skinId&&run.context==record.contextIdentity,
                 "Original skin texture-transfer frame/lifetime differs");
        }
        if(site==0x8273FF80) {
            need(currentContext==&c&&c.lastFunction==site&&uint32_t(c.lr)==0x827401C8u&&
                 c.r3.u32==s.skinImmediate.packet&&c.r4.u32==s.skinId&&c.r5.u32==selectedTechnique&&!c.r6.u32&&
                 !s.skinTextures.active&&!s.skinTextures.complete,
                 "Unqualified original skin texture-transfer entry");
            requireSkinSelection(s.skinTyped);
            need(!record.recordingContext&&s.skinImmediate.activated&&s.skinImmediate.cpu==&c&&
                 c.r1.u32==s.skinImmediate.frame&&s.skinImmediate.staging.complete,
                 "Skin immediate texture transfer has no completed original staging");
            s.runtime.engineDriver->requireContext(record.contextIdentity);
            s.runtime.pointer(c.r1.u32-0xC0,0xC0,true);s.runtime.pointer(c.r31.u32,0x24,false);
            need(PPC_LOAD_U32(c.r31.u32+0x18)==s.skinTyped,"Skin texture transfer parent packet differs");
            auto& run=s.skinTextures;run.cpu=&c;run.sp=c.r1.u32;run.context=record.contextIdentity;
            run.shadowOwner=PPC_LOAD_U32(c.r31.u32+0x20);
            return; // Retain the original named lookup/helper prologue.
        }
        if(site==0x8273FFA4) {
            // Shared shadows-lookup helper prologue and original three-row walk.
            auto& record=s.find(s.skinId);
            auto& run=s.skinTextures;
            need(c.r3.u32==s.skinImmediate.packet&&c.r4.u32==s.skinId&&c.r5.u32==selectedTechnique&&!run.active&&!run.complete,
                 "Original skin shadows lookup arguments differ");
            auto& driver=*s.runtime.engineDriver;
            s.validatePool(record.view.pool);
        const Graphics::EffectReflection& reflection=s.cachedReflection(record,true,true);
        constexpr uint32_t handles[]={0x001C000D,0x0020000F,0x00240011};
        constexpr uint32_t handleFields[]={0x698,0x694,0x6A0},textureFields[]={0xF0,0xF4,0xFC};
        constexpr std::string_view parameterNames[]={"kShadowDepthSampler","kShadowCharDepthSampler","kShadowEdgeSampler"};
        const auto shadowOwner=PPC_LOAD_U32(c.r31.u32+0x20);
        s.runtime.pointer(shadowOwner,0x6A4,false);
            need(PPC_LOAD_U32(shadowOwner)==0x8214E518&&
                 s.find(PPC_LOAD_U32(shadowOwner+0x1C)).view.source==0x820C0550,"Original named shadows owner differs");
            for(uint32_t i=0;i<3;++i) {
                const auto found=std::find_if(reflection.parameters.begin(),reflection.parameters.end(),[&](const auto& p){return p.name==parameterNames[i];});
                need(found!=reflection.parameters.end()&&found->binding.handle==handles[i]&&
                     PPC_LOAD_U32(shadowOwner+handleFields[i])==handles[i]&&parameter(s.skinId,parameterNames[i])==handles[i],
                     "Skin shared shadow parameter association differs");
                const auto binding=s.cachedPassBinding(record,selectedTechnique,handles[i]);
                const bool used=(profile.dual||profile.textured)&&!s.alphaSkin()&&i==1;
                need(binding.usage==(used?0x80u:0u)&&!binding.lanes[0]&&
                     (used?(binding.lanes[1]&&binding.lanes[1]->start==0&&binding.lanes[1]->count==1):!binding.lanes[1]),
                     "Skin shadow texture usage or sampler map differs");
                const auto id=PPC_LOAD_U32(shadowOwner+textureFields[i]);const auto view=driver.shadowTextures().view(id);
                need(view.owner==shadowOwner&&view.field==textureFields[i]&&view.phase==EngineShadowTextures::Phase::Uploaded&&
                     view.format==(i<2?0x1A220197u:0x18280086u)&&view.width==(i<2?1024u:32u)&&view.height==view.width,
                     "Skin shadow texture is not its uploaded original owner");
                run.rows[i]={handles[i],binding.usage,used?0u:i,id};
                if(used)run.characterShadow=driver.depth(id);
            }
            // The replaced opaque FX loads also initialized the divide-by20
            // operands and loop registers. The following original instructions
            // encode the selected pass from these values; stale heap pointers
            // must never become a pass handle.
            need(skinPass.passHandle==selectedTechnique+2&&!PPC_LOAD_U32(0x82D0CAF8),"Skin shadow transfer pass/context differs");
            c.r10.s64=int64_t(20*(skinPass.passHandle>>18));c.r9.u64=0;c.r7.u64=0x66666667;
            c.r28.u64=0;c.r23.u64=0x82D10000;
            run.cursor=0;run.phase=1;run.active=true;
            {static thread_local uint32_t skinTexturesLookupSample{};
            if(sampleHotLog(skinTexturesLookupSample))
                std::fprintf(stderr,"[NATIVE SKIN TEXTURES] lookup complete; source=%08X character_stage0=%u\n",record.view.source,profile.dual);}
            return;
        }
        if(site==0x82740044||site==0x82740060||site==0x827400E0||site==0x827400F0) {
            auto& run=s.skinTextures;
            {static thread_local uint32_t skinTexturesSiteSample{};
            if(sampleHotLog(skinTexturesSiteSample))
                std::fprintf(stderr,"[NATIVE SKIN TEXTURES] site=%08X lr=%08X phase=%u cursor=%u r3=%08X r4=%08X r5=%08X r27=%08X r28=%08X r31=%08X\n",
                    site,uint32_t(c.lr),run.phase,run.cursor,c.r3.u32,c.r4.u32,c.r5.u32,c.r27.u32,c.r28.u32,c.r31.u32);}
            if(site==0x827400F0) {
                need(run.active&&run.phase==1&&run.cursor==3&&c.r28.u32==12,"Original skin texture loop is incomplete");
                run.active=false;run.complete=true;
                need(!(profile.dual||profile.textured)||s.alphaSkin()||bool(run.characterShadow),"Textured skin texture walk missed its character depth");
                {static thread_local uint32_t skinTexturesLoopSample{};
                if(sampleHotLog(skinTexturesLoopSample))
                    std::fprintf(stderr,"[NATIVE SKIN TEXTURES] three-row loop complete, character_stage0=%u\n",profile.dual);}return;
            }
            need(run.active&&run.cursor<3&&c.r28.u32==4*run.cursor,"Original skin texture loop selection differs");
            const auto& row=run.rows[run.cursor];
            for(uint32_t i=0;i<3;++i)
                need(PPC_LOAD_U32(c.r1.u32+0x50+4*i)==run.rows[i].handle&&
                     PPC_LOAD_U32(c.r1.u32+0x60+4*i)==run.rows[i].texture,"Skin original texture snapshots changed");
            if(site==0x82740044) {
                need(run.phase==1&&c.r3.u32==s.skinId&&c.r4.u32==skinPass.passHandle&&c.r27.u32==skinPass.passHandle&&c.r5.u32==row.handle&&c.r31.u32==row.handle,
                     "Original skin texture usage-query ABI differs");
                c.r3.u64=row.usage;c.lr=site+4;
                if(row.usage)run.phase=2;else ++run.cursor;
            } else if(site==0x82740060) {
                need(run.phase==2&&row.usage==0x80&&c.r3.u32==row.usage&&c.r31.u32==row.handle,
                     "Original skin sampler-map query differs");
                need((profile.dual||profile.textured)&&run.cursor==1&&!PPC_LOAD_U32(0x82D0CAF8),"Skin sampler query has an unexpected used row");
                c.r3.u64=run.context;c.r4.u64=row.stage;c.r5.u64=row.texture;run.phase=3;
            } else {
                need(run.phase==3&&(profile.dual||profile.textured)&&run.cursor==1&&c.r3.u32==run.context&&c.r4.u32==0&&
                     c.r5.u32==row.texture&&c.r6.u64==0x0000000080000000ull&&run.characterShadow,
                     "Original skin texture-bind ABI differs");
                s.backend.bindRigidShadowDepth(0,run.characterShadow);
                c.r3.u64=0;c.lr=site+4;++run.cursor;run.phase=1;
            }
            return;
        }
        std::fprintf(stderr,"[NATIVE SKIN TEXTURES] site=%08X lr=%08X r1=%08X r3=%08X r4=%08X r5=%08X r6=%08X\n",
            site,uint32_t(c.lr),c.r1.u32,c.r3.u32,c.r4.u32,c.r5.u32,c.r6.u32);
        throw Failure("Skin texture-transfer site is not yet qualified");
    }
    auto& run=s.rigidTextures;auto& driver=*s.runtime.engineDriver;
    const bool immediate=s.rigidImmediate.active;
    if(site==(immediate?0x8273FF80u:0x826F39E0u)) {
        // Sky/chocolate/168F8 transfer under their alpha technique (7FFFC);
        // rigid sources transfer opaque (3FFFC).
        const bool alphaEntry=s.rigidId&&s.alphaRigid();
        // The entry resets the run wholesale below, so a stale completion flag
        // from an earlier consumed session (immediate fallback end or recording
        // restore) is discarded; only a live session (!active) overlaps.
        // Recording transfers have no interleaving fallback-entry reset, so
        // consecutive recording entries legitimately observe complete=1.
        if(!(currentContext==&c&&c.lastFunction==site&&uint32_t(c.lr)==(immediate?0x827401C8u:0x82740534u)&&s.rigidId&&
             c.r3.u32==(immediate?s.rigidImmediate.packet:PPC_LOAD_U32(0x82D09784))&&
             c.r4.u32==s.rigidId&&c.r5.u32==(alphaEntry?0x0007FFFCu:0x0003FFFCu)&&!c.r6.u32&&
             !run.active))
            std::fprintf(stderr,"[NATIVE RIGID TEXENTRY] site=%08X fn=%08X lr=%08X rigid=%08X r3=%08X packet=%08X r4=%08X r5=%08X r6=%08X runA=%u runC=%u source=%08X\n",
                site,c.lastFunction,uint32_t(c.lr),s.rigidId,c.r3.u32,immediate?s.rigidImmediate.packet:PPC_LOAD_U32(0x82D09784),
                c.r4.u32,c.r5.u32,c.r6.u32,run.active,run.complete,s.rigidId?s.find(s.rigidId).view.source:0);
        need(currentContext==&c&&c.lastFunction==site&&uint32_t(c.lr)==(immediate?0x827401C8u:0x82740534u)&&s.rigidId&&
             c.r3.u32==(immediate?s.rigidImmediate.packet:PPC_LOAD_U32(0x82D09784))&&
             c.r4.u32==s.rigidId&&c.r5.u32==(alphaEntry?0x0007FFFCu:0x0003FFFCu)&&!c.r6.u32&&
             !run.active,"Unqualified original rigid texture-transfer entry");
        requireRigidSelection(s.rigidTyped);auto& record=s.find(s.rigidId);
        if(immediate) {
            need(!record.recordingContext&&s.rigidImmediate.activated&&s.rigidImmediate.cpu==&c&&
                 c.r1.u32==s.rigidImmediate.frame&&s.rigidImmediate.staging.complete,
                 "Rigid immediate texture transfer has no completed original staging");
            driver.requireContext(record.contextIdentity);
        } else {
            need(bool(record.recordingContext),"Rigid texture transfer has no FX recording lease");
            driver.recordingOwners().requireActiveContext(record.contextIdentity);
            driver.requireRecordingStateSeed(record.contextIdentity);
        }
        s.runtime.pointer(c.r1.u32-0xC0,0xC0,true);s.runtime.pointer(c.r31.u32,0x24,false);
        need(PPC_LOAD_U32(c.r31.u32+0x18)==s.rigidTyped,"Rigid texture transfer parent packet differs");
        run={};run.cpu=&c;run.sp=c.r1.u32;run.manager=immediate?record.view.manager:c.r3.u32;
        run.context=record.contextIdentity;run.immediate=immediate;
        run.shadowOwner=PPC_LOAD_U32(c.r31.u32+0x20);run.active=true;
        return; // Retain original named lookup and helper prologue.
    }
    if(!(currentContext==&c&&run.active&&run.immediate==immediate&&run.cpu==&c&&c.r1.u32==run.sp-0xC0&&
         PPC_LOAD_U32(c.r1.u32)==run.sp&&PPC_LOAD_U32(run.sp-8)==(immediate?0x827401C8u:0x82740534u)&&
         (immediate?(c.r29.u32==s.rigidId&&(!run.phase||c.r23.u32==0x82D10000)):
          (c.r23.u32==run.manager&&c.r30.u32==s.rigidId)))) {
        auto peek=[&](uint32_t a)->uint32_t { try { s.runtime.pointer(a,4,false); } catch(...) { return 0xDEADDEADu; } return PPC_LOAD_U32(a); };
        std::fprintf(stderr,"[NATIVE RIGID TEXFRAME] site=%08X cpu=%d runA=%u imm=%u runcpu=%d r1=%08X sp=%08X star=%08X star8=%08X r29=%08X rigid=%08X phase=%u r23=%08X\n",
            site,currentContext==&c,run.active,run.immediate,run.cpu==&c,c.r1.u32,run.sp,peek(c.r1.u32),peek(run.sp-8),
            c.r29.u32,s.rigidId,run.phase,c.r23.u32);
    }
    need(currentContext==&c&&run.active&&run.immediate==immediate&&run.cpu==&c&&c.r1.u32==run.sp-0xC0&&
         PPC_LOAD_U32(c.r1.u32)==run.sp&&PPC_LOAD_U32(run.sp-8)==(immediate?0x827401C8u:0x82740534u)&&
         (immediate?(c.r29.u32==s.rigidId&&(!run.phase||c.r23.u32==0x82D10000)):
          (c.r23.u32==run.manager&&c.r30.u32==s.rigidId)),"Original rigid texture-transfer frame differs");
    requireRigidSelection(s.rigidTyped);auto& record=s.find(s.rigidId);
    need(record.contextIdentity==run.context&&bool(record.recordingContext)!=immediate,"Rigid texture transfer lost its FX context lease");
    std::shared_ptr<Graphics::NativeRecordingPayload> payload;
    if(!immediate)payload=driver.recordingOwners().activePayload(run.context);
    // Sky transfers its rigidalpha pass (selected by requested technique);
    // rigid keeps the front pass exactly as before.
    const auto& passes=record.metadata->techniques();
    size_t passIndex=0;
    if(s.alphaRigid()) {
        for(size_t i=0;i<passes.size();++i)if(passes[i].handle==0x0007FFFC)passIndex=i;
        need(passIndex==1,"Original alpha-family transfer pass is missing");
    }
    const auto& pass=passes[passIndex];
    const auto profile=rigidProfile(record.view.source);
    const auto alphaTransfer=rigidAlphaPass(record.view.source);
    if(s.alphaRigid())
        need(pass.handle==0x0007FFFC&&pass.passHandle==0x0007FFFE&&pass.contextOffset==alphaTransfer.context&&
             pass.vertexShaderAddress==alphaTransfer.vertex&&pass.pixelShaderAddress==alphaTransfer.pixel,"Rigid texture-transfer pass metadata differs");
    else
        need(pass.handle==0x0003FFFC&&pass.passHandle==0x0003FFFE&&pass.contextOffset==profile.context&&
             pass.vertexShaderAddress==profile.vertex&&pass.pixelShaderAddress==profile.pixel,"Rigid texture-transfer pass metadata differs");
    if(site==(immediate?0x8273FFA4u:0x826F3A10u)) {
        need(!run.phase&&!run.cursor&&(immediate?(c.r11.u32==run.shadowOwner&&c.r5.u32==pass.handle):
             (uint32_t(c.lr)==site&&c.r3.u32==run.shadowOwner&&c.r31.u32==pass.handle&&!c.r29.u32)),
             "Original shadows lookup or requested pass differs");
        s.runtime.pointer(run.shadowOwner,0x6A4,false);
        need(PPC_LOAD_U32(run.shadowOwner)==0x8214E518&&
             s.find(PPC_LOAD_U32(run.shadowOwner+0x1C)).view.source==0x820C0550,"Original named shadows owner differs");
        s.validatePool(record.view.pool);
        if(s.alphaRigid()||profile.flipbook||record.view.source==0x82036448||isVfxRigidSource(record.view.source)) {
            // Sky samples its own textures, never shadow depths: all three
            // shadow rows carry usage0 with no sampler lanes (skin shape).
            // Retain the rows without depths and return before the backend
            // validation and register setup, mirroring the skin lookup.
            const Graphics::EffectReflection& noShadowReflection=s.cachedReflection(record,true,true);
            constexpr uint32_t noShadowHandles[]={0x001C000D,0x0020000F,0x00240011};
            constexpr uint32_t noShadowHandleFields[]={0x698,0x694,0x6A0},noShadowTextureFields[]={0xF0,0xF4,0xFC};
            constexpr std::string_view noShadowParameterNames[]={"kShadowDepthSampler","kShadowCharDepthSampler","kShadowEdgeSampler"};
            for(uint32_t i=0;i<3;++i) {
                const auto found=std::find_if(noShadowReflection.parameters.begin(),noShadowReflection.parameters.end(),[&](const auto& p){return p.name==noShadowParameterNames[i];});
                need(found!=noShadowReflection.parameters.end()&&found->binding.handle==noShadowHandles[i]&&parameter(s.rigidId,noShadowParameterNames[i])==noShadowHandles[i]&&
                     PPC_LOAD_U32(run.shadowOwner+noShadowHandleFields[i])==noShadowHandles[i],"No-shadow rigid shared parameter association differs");
                const auto binding=s.cachedPassBinding(record,pass.handle,found->binding.handle);
                if(!(binding.usage==0u&&!binding.lanes[0]&&!binding.lanes[1]))
                    std::fprintf(stderr,"[NATIVE RIGID NO-SHADOW] row=%u usage=%08X lanes=%u %u\n",
                        i,binding.usage,!!binding.lanes[0],!!binding.lanes[1]);
                need(binding.usage==0u&&!binding.lanes[0]&&!binding.lanes[1],
                     "No-shadow rigid texture usage or sampler map differs");
                const auto id=PPC_LOAD_U32(run.shadowOwner+noShadowTextureFields[i]);const auto view=driver.shadowTextures().view(id);
                need(view.owner==run.shadowOwner&&view.field==noShadowTextureFields[i]&&view.phase==EngineShadowTextures::Phase::Uploaded&&
                     view.format==(i<2?0x1A220197u:0x18280086u)&&view.width==(i<2?1024u:32u)&&view.height==view.width,
                     "No-shadow rigid texture is not its uploaded original owner");
                run.rows[i]={noShadowHandles[i],binding.usage,i,id};
            }
            // The shared row loop still runs originally and reads these
            // registers (unlike skin, which replaces every row site), so
            // replicate the immediate setup with the sky pass handle. No
            // depths are validated or bound for the zero-usage rows.
            if(immediate) {
                need(!PPC_LOAD_U32(0x82D0CAF8),"No-shadow rigid immediate opaque SDK context global was populated");
                c.r10.s64=int64_t(20*(pass.passHandle>>18));c.r9.u64=0;c.r7.u64=0x66666667;
                c.r28.u64=0;c.r23.u64=0x82D10000;
            } else {c.r11.s64=int64_t(20*(pass.passHandle>>18));c.r8.u64=0x66666667;c.r28.u64=0;}
            run.phase=1;return;
        }
        const Graphics::EffectReflection& reflection=s.cachedReflection(record,true,true);
        constexpr uint32_t handles[]={0x001C000D,0x0020000F,0x00240011};
        constexpr uint32_t handleFields[]={0x698,0x694,0x6A0},textureFields[]={0xF0,0xF4,0xFC};
        constexpr std::string_view parameterNames[]={"kShadowDepthSampler","kShadowCharDepthSampler","kShadowEdgeSampler"};
        const auto output=driver.depth(driver.cameraBinding().depthIdentity);
        for(uint32_t i=0;i<3;++i) {
            const auto found=std::find_if(reflection.parameters.begin(),reflection.parameters.end(),[&](const auto& p){return p.name==parameterNames[i];});
            need(found!=reflection.parameters.end()&&found->binding.handle==handles[i]&&parameter(s.rigidId,parameterNames[i])==handles[i]&&
                 PPC_LOAD_U32(run.shadowOwner+handleFields[i])==handles[i],"Rigid shared shadow parameter association differs");
            const auto& binding=found->binding;
            const bool used=profile.uv?i==1:i<2;
            const uint32_t stage=profile.uv?0u:i;
            need(binding.usage==(used?0x80u:0u)&&!binding.lanes[0]&&
                 (used?(binding.lanes[1]&&binding.lanes[1]->start==stage&&binding.lanes[1]->count==1):!binding.lanes[1]),
                 "Rigid shadow texture usage or sampler map differs");
            const auto id=PPC_LOAD_U32(run.shadowOwner+textureFields[i]);const auto view=driver.shadowTextures().view(id);
            need(view.owner==run.shadowOwner&&view.field==textureFields[i]&&view.phase==EngineShadowTextures::Phase::Uploaded&&
                 view.format==(i<2?0x1A220197u:0x18280086u)&&view.width==(i<2?1024u:32u)&&view.height==view.width,
                 "Rigid shadow texture is not its uploaded original owner");
            run.rows[i]={handles[i],binding.usage,stage,id};
            if(used) {
                run.depths[i]=driver.depth(id);
            }
        }
        if(profile.uv)need(run.depths[1]&&run.depths[1]!=output,"UV character shadow aliases its scene output");
        else s.backend.validateRigidShadowDepths(run.depths,output);
        // Replace only opaque FX pointer traversal. Original stack snapshots,
        // divide-by20, pass-handle encoding and three-iteration loop remain.
        if(immediate) {
            need(!PPC_LOAD_U32(0x82D0CAF8),"Rigid immediate opaque SDK context global was populated");
            c.r10.s64=int64_t(20*(pass.passHandle>>18));c.r9.u64=0;c.r7.u64=0x66666667;
            c.r28.u64=0;c.r23.u64=0x82D10000;
        } else {c.r11.s64=int64_t(20*(pass.passHandle>>18));c.r8.u64=0x66666667;c.r28.u64=0;}
        run.phase=1;return;
    }
    if(site==(immediate?0x827400F0u:0x826F3B6Cu)) {
        need(run.phase==1&&run.cursor==3&&c.r28.u32==12,"Original rigid texture loop is incomplete");
        run.active=false;run.complete=true;
        const auto used=unsigned(run.rows[0].usage!=0)+unsigned(run.rows[1].usage!=0)+unsigned(run.rows[2].usage!=0);
        {static thread_local uint32_t rigidTexturesSample{};
        if(sampleHotLog(rigidTexturesSample))
            std::fprintf(stderr,"[NATIVE RIGID TEXTURES] context=%08X pass=%08X used_rows=%u shadow0=%08X shadow1=%08X; original three-row loop completed\n",
                         run.context,pass.handle,used,run.rows[0].texture,run.rows[1].texture);}return;
    }
    need(run.cursor<3&&c.r28.u32==4*run.cursor&&c.r27.u32==pass.passHandle,"Original rigid texture loop selection differs");
    const auto& row=run.rows[run.cursor];
    for(uint32_t i=0;i<3;++i)
        need(PPC_LOAD_U32(c.r1.u32+0x50+4*i)==run.rows[i].handle&&PPC_LOAD_U32(c.r1.u32+0x60+4*i)==run.rows[i].texture,
             "Original rigid texture snapshots changed");
    if(site==(immediate?0x82740044u:0x826F3ABCu)) {
        need(run.phase==1&&c.r3.u32==s.rigidId&&c.r4.u32==pass.passHandle&&c.r5.u32==row.handle&&c.r31.u32==row.handle,
             "Original rigid texture usage-query ABI differs");
        c.r3.u64=row.usage;c.lr=site+4;
        if(row.usage)run.phase=2;else ++run.cursor;
    } else if(site==(immediate?0x82740060u:0x826F3ADCu)) {
        need(run.phase==2&&row.usage==0x80&&(immediate?c.r3.u32:c.r10.u32)==row.usage&&
             c.r31.u32==row.handle,"Original rigid sampler-map query differs");
        if(immediate)need(!PPC_LOAD_U32(0x82D0CAF8),"Rigid immediate SDK global changed before sampler binding");
        c.r3.u64=run.context;c.r4.u64=row.stage;c.r5.u64=row.texture;run.phase=3;
        // Retain original 64-bit sampler-mask arithmetic at3B50..3B58.
    } else if(site==(immediate?0x827400E0u:0x826F3B5Cu)) {
        need(run.phase==3&&run.cursor<2&&c.r3.u32==run.context&&c.r4.u32==row.stage&&c.r5.u32==row.texture&&
             c.r6.u64==(0x8000000000000000ull>>(row.stage+32)),"Original rigid deferred texture-bind ABI differs");
        if(immediate)s.backend.bindRigidShadowDepth(row.stage,run.depths[run.cursor]);
        else s.backend.bindRigidShadowDepth(payload,row.stage,run.depths[run.cursor]);
        c.r3.u64=0;c.lr=site+4;++run.cursor;run.phase=1;
    } else throw Failure("Unknown original rigid texture-transfer boundary");
}
void EngineEffects::requireRigidSelection(uint32_t typed) const {
    auto& s=*state;s.require(s.runtime.base);auto* base=s.runtime.base;
    if(s.monoId) {
        need(currentContext&&s.monoRecordingPath&&s.monoTyped==typed&&s.monoTechnique==0x0003FFFC,
             "Recording mono selection differs from its original opaque producer");
        s.activeMono(*currentContext,base);return;
    }
    need(s.rigidId&&s.rigidTyped==typed&&s.rigidVertex&&s.rigidPixel&&!s.activeEdgeId&&!s.activeShadowId&&!s.zprepassId,
         "Missing native rigid selection");
    const auto& v=s.find(s.rigidId).view;
    // The active shader pair identifies the selected technique, including
    // both front and alpha draws from the same textured effect.
    const auto selected=s.alphaRigid()?0x0007FFFCu:0x0003FFFCu;
    need(PPC_LOAD_U32(typed+0x1C)==v.identity&&PPC_LOAD_U32(v.manager+4)==v.wrapper&&
         PPC_LOAD_U32(v.manager+8)==v.identity&&PPC_LOAD_U32(v.manager+0xC)==selected&&
         PPC_LOAD_U32(v.wrapper+0x2C)==v.cache,"Original rigid selection changed");
    // During recording the physical immediate bindings are unrelated to the
    // deferred context. Qualify immutable shader objects without touching IA/VS/PS.
    if(s.find(s.rigidId).recordingContext)s.backend.validateRigidShaders(*s.rigidVertex,*s.rigidPixel);
    else s.requireRigidPhysical("requireRigidSelection");
}
std::array<uint8_t,40> EngineEffects::rigidRecordingMask(uint32_t typed) const {
    requireRigidSelection(typed);auto& s=*state;auto* base=s.runtime.base;
    const auto& record=s.find(s.monoId?s.monoId:s.rigidId);
    const bool hasSharedMaskNames=bool(s.runtime.effectPoolBacking);
    const Graphics::EffectReflection& reflected=s.cachedReflection(record,true,hasSharedMaskNames);
    need(!reflected.passes.empty(),"Rigid recording has no reflected pass");
    // Original826F4D08 receives typed+50: the FIRST typed pass's mask, even
    // when a later alpha technique is selected. Retain that original contract.
    const auto& pass=reflected.passes.front();
    need(PPC_LOAD_U32(typed+0x48)==pass.techniqueHandle&&PPC_LOAD_U32(typed+0x4C)==pass.passHandle,
         "Rigid recording first reflected pass changed");
    std::array<uint8_t,40> mask{};
    for(size_t lane=0;lane<2;++lane)for(size_t byte=0;byte<8;++byte)
        mask[8*lane+byte]=uint8_t(pass.masks[lane]>>(56-8*byte));
    return mask;
}
void EngineEffects::beginVfxRigid(PPCContext& c,uint8_t* base) {
    observeProducerEntry(c,base,"vfx_rigid_begin");
    auto& s=*state;s.require(base);auto& run=s.rigidImmediate;
    const auto packet=c.r31.u32,w=c.r4.u32,m=c.r3.u32;
    const auto owner=s.packetOwner(packet);
    need(currentContext==&c&&c.lastFunction==0x826B5FC0&&uint32_t(c.lr)==0x823CAC78&&
         c.r5.u32==0x0003FFFC&&run.active&&!run.activated&&run.cpu==&c&&run.packet==packet&&
         c.r1.u32==run.frame-0x60&&PPC_LOAD_U32(c.r1.u32)==run.frame&&
         PPC_LOAD_U32(c.r1.u32+0x58)==0x82740134&&isVfxRigidSource(owner.source),
         "VFX rigid activation lost its original immediate frame");
    auto& record=s.find(owner.identity);const auto& v=record.view;
    s.runtime.pointer(w,0x30,false);s.runtime.pointer(m,0x1C,false);s.runtime.pointer(owner.typed,0xAC,false);
    need(owner.wrapper==w&&owner.manager==m&&m==PPC_LOAD_U32(0x82D08BFC)&&
         PPC_LOAD_U32(owner.typed+0xA8)==0x0003FFFC&&
         PPC_LOAD_U32(w)==0x820B7140&&PPC_LOAD_U32(w+0xC)==m&&
         PPC_LOAD_U32(w+0x10)==v.identity&&PPC_LOAD_U32(w+0x14)==w&&
         PPC_LOAD_U32(w+0x18)==v.identity&&PPC_LOAD_U32(w+0x1C)==v.cache&&
         PPC_LOAD_U32(w+0x20)==120&&v.cacheBytes==120&&PPC_LOAD_U32(w+0x24)==v.cache&&
         PPC_LOAD_U32(w+0x28)==1&&!record.recordingContext&&PPC_LOAD_U32(0x82D6CCA8)==0x20,
         "VFX rigid reflected owner differs");
    const auto camera=s.runtime.engineDriver->cameraBinding();
    need(camera.camera==PPC_LOAD_U32(packet+8)&&camera.camera==PPC_LOAD_U32(0x82E3DD60),
         "VFX rigid camera differs");
    s.runtime.engineDriver->requireContext(owner.context);s.validatePool(v.pool);
    const auto passes=record.metadata->techniques();need(passes.size()==1,"VFX rigid pass count changed");
    const auto& pass=passes.front();
    need(pass.handle==0x0003FFFC&&pass.passHandle==0x0003FFFE&&pass.contextOffset==0x1510&&
         pass.vertexShaderAddress==0x8205BE70&&pass.pixelShaderAddress==0x8205C100&&
         pass.scalars.empty()&&pass.samplers.size()==6,"VFX rigid original pass changed");
    constexpr uint32_t samplerIds[]={0,4,8,16,20,24};
    for(uint32_t i=0;i<6;++i)need(pass.samplers[i].stage==0&&pass.samplers[i].sdkId==samplerIds[i]&&
        pass.samplers[i].value==(i<3?0u:1u),"VFX rigid sampler rows changed");
    s.runtime.pointer(v.cache,v.cacheBytes,false);
    const std::array<uint32_t,6> header{pass.handle,0,v.cache+24,v.cache+24,0,6};
    for(uint32_t i=0;i<header.size();++i)need(PPC_LOAD_U32(v.cache+4*i)==header[i],"VFX rigid cache changed");
    if(s.rigidId==v.identity) {
        if(s.screenReplacement.effect)s.requireScreenReplacement(base);else requireRigidSelection(owner.typed);
        run.activated=true;return;
    }
    need(!s.activeEdgeId&&!s.activeShadowId,"VFX rigid overlaps an unfinished effect");
    if(s.zprepassId)s.activeZPrepass(c,base);
    else if(s.rigidId){if(s.screenReplacement.effect)s.requireScreenReplacement(base);else requireRigidSelection(s.rigidTyped);}
    else if(s.skinId){if(s.screenReplacement.effect)s.requireScreenReplacement(base);else requireSkinSelection(s.skinTyped);}
    const Graphics::CompiledMaterial* vertex=nullptr;const Graphics::CompiledMaterial* pixel=nullptr;
    const auto shaders=record.metadata->shaders();
    for(size_t i=0;i<shaders.size();++i) {
        if(shaders[i].originalAddress==pass.vertexShaderAddress)vertex=&record.shaderRecords.prepareForBind(record.shaders[i],s.compiler);
        if(shaders[i].originalAddress==pass.pixelShaderAddress)pixel=&record.shaderRecords.prepareForBind(record.shaders[i],s.compiler);
    }
    need(vertex&&pixel,"VFX rigid shader pair missing");s.backend.validateRigidShaders(*vertex,*pixel);
    {EngineCpuCalls cpu(c,base);cpu.registers().lr=0x826B5FF8;cpu.invoke(0x826B4628,m);}
    need(!PPC_LOAD_U32(m+4)&&!PPC_LOAD_U32(m+0xC)&&!PPC_LOAD_U32(w+0x2C),"VFX rigid previous pass did not end");
    PPC_STORE_U32(m+4,w);PPC_STORE_U32(m+8,v.identity);PPC_STORE_U32(m+0xC,pass.handle);
    {EngineCpuCalls cpu(c,base);cpu.registers().lr=0x826B604C;cpu.invoke(0x826B35D8,w+0x14,pass.handle,0);}
    need(PPC_LOAD_U32(w+0x2C)==v.cache,"VFX rigid cache did not select its pass");
    for(const auto& row:pass.samplers)s.runtime.engineDriver->directSampler(base,row.stage,row.sdkId,row.value);
    record.privateModified.fill(0);std::fill_n(record.privateModified.begin(),16,uint8_t(0xFF));
    if(!PPC_LOAD_U32(0x82D00F80)){auto* dirty=s.runtime.pointer(v.pool,128,true);std::memset(dirty,0,128);std::memset(dirty,0xFF,16);}
    s.backend.bindRigidShaders(*vertex,*pixel);s.screenReplacement={};s.rigidId=v.identity;s.rigidTyped=owner.typed;s.rigidCamera=camera.camera;
    s.rigidVertex=vertex;s.rigidPixel=pixel;s.skinId=s.skinTyped=s.skinCamera=0;s.skinVertex=s.skinPixel=nullptr;
    run.activated=true;
    static thread_local uint32_t sample{};
    if(sampleHotLog(sample))std::fprintf(stderr,"[NATIVE VFX RIGID BEGIN] packet=%08X typed=%08X effect=%08X; original mesh-particle shader pair bound\n",packet,owner.typed,v.identity);
}
void EngineEffects::beginRigid(PPCContext& c,uint8_t* base) {
    observeProducerEntry(c,base,"rigid_begin");
    auto& s=*state;s.require(base);const auto packet=c.r31.u32,w=c.r4.u32,m=c.r3.u32;
    const bool immediate=s.rigidImmediate.active;
    // Admit each source's observed techniques; the actual request selects the
    // front or alpha pass below. Textured rigid has both kinds of packets.
    const bool alphaBegin=isAlphaBeginSource(s.packetOwner(packet).source);
    if(!(currentContext==&c&&c.lastFunction==0x826B5FC0&&uint32_t(c.lr)==0x823CA760&&
         (c.r5.u32==0x0003FFFC||(alphaBegin&&c.r5.u32==0x0007FFFC))&&
         c.r1.u32>=0x200&&!(c.r1.u32&15)&&PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0x60&&
         PPC_LOAD_U32(c.r1.u32+0x58)==(immediate?0x82740134u:0x8274047Cu))) {
        auto peek=[&](uint32_t a)->uint32_t { try { s.runtime.pointer(a,4,false); } catch(...) { return 0xDEADDEADu; } return PPC_LOAD_U32(a); };
        std::fprintf(stderr,"[NATIVE BEGIN ENTRY] fn=%08X lr=%08X tech=%08X r1=%08X star=%08X star58=%08X imm=%u packet=%08X\n",
            c.lastFunction,uint32_t(c.lr),c.r5.u32,c.r1.u32,peek(c.r1.u32),peek(c.r1.u32+0x58),immediate,c.r31.u32);
    }
    need(currentContext==&c&&c.lastFunction==0x826B5FC0&&uint32_t(c.lr)==0x823CA760&&
         (c.r5.u32==0x0003FFFC||(alphaBegin&&c.r5.u32==0x0007FFFC))&&
         c.r1.u32>=0x200&&!(c.r1.u32&15)&&PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0x60&&
         PPC_LOAD_U32(c.r1.u32+0x58)==(immediate?0x82740134u:0x8274047Cu),"Unqualified original rigid activation");
    if(immediate)need(s.rigidImmediate.cpu==&c&&s.rigidImmediate.packet==packet&&!s.rigidImmediate.activated&&
         c.r1.u32==s.rigidImmediate.frame-0x60,"Rigid immediate activation has no original fallback frame");
    s.runtime.pointer(packet,0x24,false);const auto t=PPC_LOAD_U32(packet+0x18),meta=PPC_LOAD_U32(packet);
    s.runtime.pointer(t,0xB0,false);s.runtime.pointer(w,0x30,false);s.runtime.pointer(m,0x18,false);s.runtime.pointer(meta,0x2C,false);
    auto& record=s.find(PPC_LOAD_U32(w+0x10));const auto& v=record.view;
    const bool sky=v.source==0x82036448;
    const bool alphaFamily=isAlphaBeginSource(v.source)&&c.r5.u32==0x0007FFFC;
    if(isAlphaBeginSource(v.source)&&
       s.rigidId==v.identity&&s.alphaRigid()!=alphaFamily) {
        need(immediate&&!s.rigidImmediate.activated,"Rigid pass switch interrupted a draw");
        EngineCpuCalls cpu(c,base);cpu.registers().lr=0x826B5FF8;cpu.invoke(0x826B4628,m);
    }
    const auto profile=rigidProfile(v.source);
    // Sky tracks its selected rigidalpha cache in view.cache; a re-begin
    // while still selected sees the wrapper default in +0x1C/+0x24 instead.
    // Chocolate tracks likewise once selected.
    if(alphaFamily&&!(PPC_LOAD_U32(w+0x1C)==v.cache||PPC_LOAD_U32(w+0x2C)==v.cache))
        std::fprintf(stderr,"[NATIVE SKY OWNER] tA8=%08X w1C=%08X w24=%08X w2C=%08X vcache=%08X m4=%08X m8=%08X mC=%08X\n",
            PPC_LOAD_U32(t+0xA8),PPC_LOAD_U32(w+0x1C),PPC_LOAD_U32(w+0x24),PPC_LOAD_U32(w+0x2C),
            v.cache,PPC_LOAD_U32(m+4),PPC_LOAD_U32(m+8),PPC_LOAD_U32(m+0xC));
    need(v.phase==Phase::Reflected&&record.typedReflections==1&&
         PPC_LOAD_U32(t)==0x820616C0&&PPC_LOAD_U32(t+0x10)==m&&PPC_LOAD_U32(t+0x18)==w&&PPC_LOAD_U32(t+0x1C)==v.identity&&
         (isAlphaBeginSource(v.source)?PPC_LOAD_U32(t+0xA8)==0x0003FFFCu:PPC_LOAD_U32(t+0xA8)==c.r5.u32)&&v.wrapper==w&&v.manager==m&&m==PPC_LOAD_U32(0x82D08BFC)&&
         PPC_LOAD_U32(w)==0x820B7140&&PPC_LOAD_U32(w+0xC)==m&&PPC_LOAD_U32(w+0x14)==w&&PPC_LOAD_U32(w+0x18)==v.identity&&
         (PPC_LOAD_U32(w+0x1C)==v.cache||(alphaFamily&&PPC_LOAD_U32(w+0x2C)==v.cache))&&PPC_LOAD_U32(w+0x20)==v.cacheBytes&&
         (PPC_LOAD_U32(w+0x24)==v.cache||(alphaFamily&&PPC_LOAD_U32(w+0x2C)==v.cache)),
         "Original rigid reflected owner differs");
    need(!PPC_LOAD_U32(meta+0x24)&&!PPC_LOAD_U32(0x82D6CCA8),"Rigid activation geometry/dispatcher differs");
    const auto camera=s.runtime.engineDriver->cameraBinding();
    need(camera.camera==PPC_LOAD_U32(packet+8)&&camera.camera==PPC_LOAD_U32(0x82E3DD60)&&
         camera.viewport==std::array<uint32_t,6>{0,0,1280,720,0x3F800000,0},"Original rigid camera differs");
    s.runtime.engineDriver->requireContext(PPC_LOAD_U32(packet+0x14));s.validatePool(v.pool);
    const auto passes=record.metadata->techniques();need(passes.size()==2&&PPC_LOAD_U32(w+0x28)==2,"Original rigid technique count differs");
    // Alpha family selects its rigidalpha pass by the requested technique in
    // typed+0xAC; rigid keeps its front (opaque) pass exactly as before.
    size_t passIndex=0;
    if(alphaFamily) {
        const uint32_t requested=PPC_LOAD_U32(t+0xAC);
        need(requested==0x0007FFFC,"Original alpha-family requested pass differs");
        for(size_t i=0;i<passes.size();++i)if(passes[i].handle==requested)passIndex=i;
        need(passIndex==1,"Original alpha-family requested pass is missing");
    }
    const auto& pass=passes[passIndex];
    const auto alphaPass=rigidAlphaPass(v.source);
    if(s.runtime.resourceAudit.active())try {
        char asset[32],parameters[192];
        std::snprintf(asset,sizeof(asset),"source:%08X",v.source);
        std::snprintf(parameters,sizeof(parameters),"source=%08X vs=%08X ps=%08X technique=%08X pass=%08X context=%08X",
            v.source,pass.vertexShaderAddress,pass.pixelShaderAddress,pass.handle,pass.passHandle,pass.contextOffset);
        s.runtime.resourceAudit.observe("effect_pass",asset,uint32_t(c.lr),parameters,"reflected-selected",s.runtime.nativeDepthCopyCount.load());
    }catch(...){std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] rigid pass capture failed\n");}
    if(alphaFamily) {
        need(pass.name=="rigidalpha"&&pass.handle==0x0007FFFC&&pass.passHandle==0x0007FFFE&&pass.contextOffset==alphaPass.context&&
             pass.vertexShaderAddress==alphaPass.vertex&&pass.pixelShaderAddress==alphaPass.pixel&&pass.scalars.size()==2&&pass.samplers.size()==alphaPass.samplers&&
             pass.scalars[0].sdkId==0x28&&pass.scalars[0].value==1&&pass.scalars[1].sdkId==0x30&&pass.scalars[1].value==(sky?0u:1u),
             "Original alpha-family pass metadata differs");
    } else
        need(pass.name=="rigid"&&pass.handle==0x0003FFFC&&pass.passHandle==0x0003FFFE&&pass.contextOffset==profile.context&&
             pass.vertexShaderAddress==profile.vertex&&pass.pixelShaderAddress==profile.pixel&&pass.scalars.size()==2&&pass.samplers.size()==profile.samplers&&
             pass.scalars[0].sdkId==0x28&&pass.scalars[0].value==1&&pass.scalars[1].sdkId==0x30&&pass.scalars[1].value==(sky?0u:1u),
             "Original rigid pass metadata differs");
    const std::array<uint32_t,6> samplerIds={0,4,8,0x10,0x14,0x18},samplerValues={2,2,2,0,0,2};
    if(alphaFamily||sky) {
        // Alpha rigidalpha stages 0-2 select the base rows; sky stage 3
        // selects the sky rows (chocolate has 18 samplers, no stage 3).
        // All six sdk ids repeat per stage. 168F8 and dual-textured use the
        // same single-stage base-pattern table: stage 0, {0,0,0,1,1,1}.
        if(profile.uv) {
            need(pass.samplers.size()==12,"Dual UV alpha sampler count differs");
            for(uint32_t i=0;i<12;++i)need(pass.samplers[i].stage==i/6&&pass.samplers[i].sdkId==samplerIds[i%6]&&
                 pass.samplers[i].value==(i%6<3?0u:1u),"Dual UV alpha sampler rows differ");
        } else if(profile.chocolate) {
            need(pass.samplers.size()==18,"Chocolate alpha sampler count differs");
            for(uint32_t i=0;i<18;++i)need(pass.samplers[i].stage==i/6&&pass.samplers[i].sdkId==samplerIds[i%6]&&
                 pass.samplers[i].value==(i%6<3?0u:1u),"Chocolate alpha sampler rows differ");
        } else if(isRigidSource(v.source)) {
            need(pass.samplers.size()==6,"Single-stage rigidalpha sampler count differs");
            for(uint32_t i=0;i<6;++i)need(pass.samplers[i].stage==0&&pass.samplers[i].sdkId==samplerIds[i]&&
                 pass.samplers[i].value==(i<3?0u:1u),"Single-stage rigidalpha sampler rows differ");
        } else for(uint32_t i=0;i<profile.samplers;++i) {
            const uint32_t stage=i/6,k=i%6;
            const uint32_t value=stage<3?(k<3?0u:1u):(k<3?2u:(k<5?0u:2u));
            need(pass.samplers[i].stage==stage&&pass.samplers[i].sdkId==samplerIds[k]&&
                 pass.samplers[i].value==value,"Original alpha-family sampler rows differ");
        }
    } else if(profile.flipbook) {
        need(pass.samplers.size()==6,"Flipbook opaque sampler count differs");
        for(uint32_t i=0;i<6;++i)need(pass.samplers[i].stage==0&&pass.samplers[i].sdkId==samplerIds[i]&&
             pass.samplers[i].value==(i<3?0u:1u),"Flipbook opaque base sampler rows differ");
    } else if(profile.uv) {
        for(uint32_t i=0;i<18;++i)need(pass.samplers[i].stage==i/6&&pass.samplers[i].sdkId==samplerIds[i%6]&&
             pass.samplers[i].value==(i<6?samplerValues[i]:(i%6<3?0u:1u)),"Dual UV opaque sampler rows differ");
    } else for(uint32_t i=0;i<12;++i)need(pass.samplers[i].stage==i/6&&pass.samplers[i].sdkId==samplerIds[i%6]&&
        pass.samplers[i].value==samplerValues[i%6],"Original rigid sampler rows differ");
    // The textured base rows live in the 18+-row front/alpha tables; 168F8's
    // 6-row alpha table is checked (once probed) above instead.
    if(profile.textured&&pass.samplers.size()>=18) {
        const std::array<uint32_t,6> baseValues{0,0,0,1,1,1};
        for(uint32_t index=0;index<6;++index)need(pass.samplers[12+index].stage==2&&
            pass.samplers[12+index].sdkId==samplerIds[index]&&pass.samplers[12+index].value==baseValues[index],
            "Textured rigid base sampler rows differ");
    }
    if(!alphaFamily&&(profile.chocolate||profile.projtex)) {
        need(pass.samplers.size()==24,"Four-stage rigid sampler count differs");
        for(uint32_t i=12;i<24;++i) {
            const uint32_t stage=i/6,k=i%6;
            const uint32_t expected=profile.projtex&&stage==3&&k<3?2u:(k<3?0u:1u);
            need(pass.samplers[i].stage==stage&&pass.samplers[i].sdkId==samplerIds[k]&&pass.samplers[i].value==expected,
                 "Four-stage rigid material sampler rows differ");
        }
    }
    s.runtime.pointer(v.cache,v.cacheBytes,false);const auto rows=v.cache+48;
    if(profile.multitone&&!alphaFamily) {
        const std::array<uint32_t,6> noiseValues{0,0,0,1,1,1};
        for(uint32_t index=0;index<6;++index)need(pass.samplers[18+index].stage==3&&
            pass.samplers[18+index].sdkId==samplerIds[index]&&pass.samplers[18+index].value==noiseValues[index],
            "Multitone noise sampler rows differ");
    }
    if(profile.normalmap&&!alphaFamily) {
        const std::array<uint32_t,6> normalValues{0,0,0,1,1,1};
        for(uint32_t index=0;index<6;++index)need(pass.samplers[18+index].stage==3&&
            pass.samplers[18+index].sdkId==samplerIds[index]&&pass.samplers[18+index].value==normalValues[index],
            "Normalmap normal sampler rows differ");
    }
    // The wrapper cache still carries the default (front) technique header
    // before the alpha family selects rigidalpha; rigid selects front so its
    // header is already the pass header. A re-begin while still selected sees
    // the rigidalpha header on the tracked selected cache instead. Both
    // alpha fronts carry 24 samplers in the default header.
    if(alphaFamily) {
        const auto h0=PPC_LOAD_U32(v.cache);
        if(h0==0x0003FFFCu) {
            // The default (front) header carries the front table count: 24
            // for sky/chocolate fronts, 18 for 168F8/dual-textured fronts
            // (h5=0x12 with rows..rows+24 extent intact).
            const uint32_t frontSamplers=isRigidSource(v.source)?profile.samplers:24u;
            const std::array<uint32_t,6> fresh{0x0003FFFCu,0,rows,rows+24,2,frontSamplers};
            for(uint32_t i=0;i<fresh.size();++i)need(PPC_LOAD_U32(v.cache+4*i)==fresh[i],"Original rigid cache header differs");
        } else {
            need(h0==pass.handle&&PPC_LOAD_U32(v.cache+4)==0&&
                 PPC_LOAD_U32(v.cache+8)+24==PPC_LOAD_U32(v.cache+12)&&PPC_LOAD_U32(v.cache+16)==2&&
                 PPC_LOAD_U32(v.cache+20)==alphaPass.samplers,"Original rigid cache header differs");
            s.runtime.pointer(PPC_LOAD_U32(v.cache+8),64,false);
        }
    } else {
        const std::array<uint32_t,6> header{pass.handle,0,rows,rows+24,2,profile.samplers};
        for(uint32_t i=0;i<header.size();++i)need(PPC_LOAD_U32(v.cache+4*i)==header[i],"Original rigid cache header differs");
    }
    need(!s.activeEdgeId&&!s.activeShadowId,"Another native effect remains active before rigid");
    if(s.rigidId==v.identity) {
        need(s.rigidId==v.identity&&s.rigidTyped==t&&s.rigidCamera==camera.camera&&s.rigidVertex&&s.rigidPixel&&
             PPC_LOAD_U32(m+4)==w&&PPC_LOAD_U32(m+8)==v.identity&&PPC_LOAD_U32(m+0xC)==pass.handle&&
             PPC_LOAD_U32(w+0x2C)==v.cache,"Original rigid active pair changed");
        if(s.screenReplacement.effect)s.requireScreenReplacement(base);
        else s.requireRigidPhysical("beginRigid same selection");
        if(immediate)s.rigidImmediate.activated=true;
        return;
    }
    if(s.zprepassId) {
        s.activeZPrepass(c,base);
        need(!s.zprepassMesh.native||s.zprepassMesh.phase==4,"Rigid activation interrupted an unfinished depth mesh");
    } else if(s.rigidId){if(s.screenReplacement.effect)s.requireScreenReplacement(base);else requireRigidSelection(s.rigidTyped);}
    else if(s.skinId){if(s.screenReplacement.effect)s.requireScreenReplacement(base);else requireSkinSelection(s.skinTyped);}
    else need(!PPC_LOAD_U32(m+4)&&!PPC_LOAD_U32(m+0xC),"Another original effect remains selected before rigid");
    need(!PPC_LOAD_U32(w+0x2C),"Rigid cache was already selected before activation");
    const Graphics::CompiledMaterial* vertex=nullptr;const Graphics::CompiledMaterial* pixel=nullptr;
    const auto shaders=record.metadata->shaders();
    for(size_t i=0;i<shaders.size();++i) {
        if(shaders[i].originalAddress==pass.vertexShaderAddress)vertex=&record.shaderRecords.prepareForBind(record.shaders[i],s.compiler);
        if(shaders[i].originalAddress==pass.pixelShaderAddress)pixel=&record.shaderRecords.prepareForBind(record.shaders[i],s.compiler);
    }
    need(vertex&&pixel,"Rigid shader pair is missing");s.backend.validateRigidShaders(*vertex,*pixel);
    need(word(record.metadata->body(),0x120)==1&&word(record.metadata->body(),0x124)==1,"Original rigid dirty extent differs");
    auto prospective=s.runtime.engineDriver->effectiveState();
    for(const auto& row:pass.scalars)prospective.setScalar(row.sdkId,row.value);
    for(const auto& row:pass.samplers)prospective.setSampler(row.stage,row.sdkId,row.value);
    // Original826B5FF4 ends the previously selected pair before publishing the
    // new one. The depth prepass remains active on the live scene transition.
    {EngineCpuCalls cpu(c,base);cpu.registers().lr=0x826B5FF8;cpu.invoke(0x826B4628,m);}
    need(!s.zprepassId&&!PPC_LOAD_U32(m+4)&&!PPC_LOAD_U32(m+0xC),"Original rigid transition did not end its previous pass");
    PPC_STORE_U32(m+4,w);PPC_STORE_U32(m+8,v.identity);PPC_STORE_U32(m+0xC,pass.handle);
    {EngineCpuCalls cpu(c,base);cpu.registers().lr=0x826B604C;cpu.invoke(0x826B35D8,w+0x14,pass.handle,0);}
    if(alphaFamily) {
        // Selecting rigidalpha switches the wrapper to that technique's own
        // cache object; track it as the live cache. The selected header
        // carries the pass sampler count; rows are validated mapped below
        // with the full row extent checked at commit.
        const auto selected=PPC_LOAD_U32(w+0x2C);
        need(selected&&PPC_LOAD_U32(selected)==0x0007FFFC&&PPC_LOAD_U32(selected+4)==0&&
             PPC_LOAD_U32(selected+8)+24==PPC_LOAD_U32(selected+12)&&PPC_LOAD_U32(selected+16)==2&&
             PPC_LOAD_U32(selected+20)==alphaPass.samplers,"Original alpha-family selected cache differs");
        s.runtime.pointer(PPC_LOAD_U32(selected+8),64,false);
        record.view.cache=selected;
    }
    need(PPC_LOAD_U32(w+0x2C)==v.cache,"Original rigid cache did not select its pass");
    for(const auto& row:pass.scalars)s.runtime.engineDriver->directScalar(base,row.sdkId,row.value);
    for(const auto& row:pass.samplers)s.runtime.engineDriver->directSampler(base,row.stage,row.sdkId,row.value);
    record.privateModified.fill(0);std::fill_n(record.privateModified.begin(),16,uint8_t(0xFF));
    if(!PPC_LOAD_U32(0x82D00F80)){auto* dirty=s.runtime.pointer(v.pool,128,true);std::memset(dirty,0,128);std::memset(dirty,0xFF,16);}
    s.backend.bindRigidShaders(*vertex,*pixel);s.screenReplacement={};s.rigidId=v.identity;s.rigidTyped=t;s.rigidCamera=camera.camera;
    s.rigidVertex=vertex;s.rigidPixel=pixel;
    s.skinId=s.skinTyped=s.skinCamera=0;s.skinVertex=s.skinPixel=nullptr;
    if(immediate)s.rigidImmediate.activated=true;
}
void EngineEffects::requireSkinSelection(uint32_t typed) const {
    auto& s=*state;s.require(s.runtime.base);auto* base=s.runtime.base;
    need(s.skinId&&s.skinTyped==typed&&s.skinVertex&&s.skinPixel&&!s.activeEdgeId&&!s.activeShadowId&&!s.zprepassId&&!s.rigidId,
         "Missing native skin selection");
    const auto& v=s.find(s.skinId).view;const auto selected=s.alphaSkin()?0x0007FFFCu:0x0003FFFCu;
    need(PPC_LOAD_U32(typed+0x1C)==v.identity&&PPC_LOAD_U32(v.manager+4)==v.wrapper&&
         PPC_LOAD_U32(v.manager+8)==v.identity&&PPC_LOAD_U32(v.manager+0xC)==selected&&
         PPC_LOAD_U32(v.wrapper+0x2C)==v.cache,"Original skin selection changed");
    if(s.find(s.skinId).recordingContext)s.backend.validateSkinShaders(*s.skinVertex,*s.skinPixel);
    else s.backend.requireSkinShaders(*s.skinVertex,*s.skinPixel);
}
void EngineEffects::beginSkin(PPCContext& c,uint8_t* base) {
    observeProducerEntry(c,base,"skin_begin");
    auto& s=*state;s.require(base);const auto packet=c.r31.u32,w=c.r4.u32,m=c.r3.u32;
    const bool immediate=s.skinImmediate.active;const bool alphaFamily=c.r5.u32==0x0007FFFC;
    {static thread_local uint32_t skinBeginEntrySample{};
    if(sampleHotLog(skinBeginEntrySample))
        std::fprintf(stderr,"[NATIVE SKIN BEGIN] entry=%08X caller=%08X technique=%08X packet=%08X manager=%08X wrapper=%08X\n",
            c.lastFunction,uint32_t(c.lr),c.r5.u32,packet,m,w);}
    need(currentContext==&c&&c.lastFunction==0x826B5FC0&&uint32_t(c.lr)==0x823CAA0C&&
         (c.r5.u32==0x0003FFFC||c.r5.u32==0x0007FFFC)&&
         c.r1.u32>=0x200&&!(c.r1.u32&15)&&PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0x60,
         "Unqualified original skin activation");
    if(immediate)need(s.skinImmediate.cpu==&c&&s.skinImmediate.packet==packet&&!s.skinImmediate.activated&&
         c.r1.u32==s.skinImmediate.frame-0x60&&s.skinImmediate.alpha==alphaFamily,
         "Skin immediate activation has no original fallback frame");
    s.runtime.pointer(packet,0x24,false);const auto t=PPC_LOAD_U32(packet+0x18),meta=PPC_LOAD_U32(packet);
    s.runtime.pointer(t,0xB8,false);s.runtime.pointer(w,0x30,false);s.runtime.pointer(m,0x18,false);s.runtime.pointer(meta,0x2C,false);
    auto& record=s.find(PPC_LOAD_U32(w+0x10));const auto& v=record.view;
    const auto profile=skinProfile(v.source);
    if(s.skinId==v.identity&&s.alphaSkin()!=alphaFamily) {
        need(immediate&&!s.skinImmediate.activated,"Skin pass switch interrupted a draw");
        EngineCpuCalls cpu(c,base);cpu.registers().lr=0x826B5FF8;cpu.invoke(0x826B4628,m);
    }
    need(v.phase==Phase::Reflected&&record.typedReflections==1&&
         PPC_LOAD_U32(t)==0x82061714&&PPC_LOAD_U32(t+0x10)==m&&PPC_LOAD_U32(t+0x18)==w&&PPC_LOAD_U32(t+0x1C)==v.identity&&
         PPC_LOAD_U32(t+0x48)==0x0003FFFC&&PPC_LOAD_U32(t+0x4C)==0x0003FFFE&&
         PPC_LOAD_U32(t+0xB0)==0x0003FFFC&&PPC_LOAD_U32(t+0xB4)==0x0007FFFC&&
         v.wrapper==w&&v.manager==m&&m==PPC_LOAD_U32(0x82D08BFC)&&
         PPC_LOAD_U32(w)==0x820B7140&&PPC_LOAD_U32(w+0xC)==m&&PPC_LOAD_U32(w+0x14)==w&&PPC_LOAD_U32(w+0x18)==v.identity&&
         (PPC_LOAD_U32(w+0x1C)==v.cache||(alphaFamily&&PPC_LOAD_U32(w+0x2C)==v.cache))&&PPC_LOAD_U32(w+0x20)==v.cacheBytes&&
         (PPC_LOAD_U32(w+0x24)==v.cache||(alphaFamily&&PPC_LOAD_U32(w+0x2C)==v.cache)),
         "Original skin reflected owner differs");
    need(!PPC_LOAD_U32(0x82D6CCA8),"Skin activation dispatcher differs");
    const auto skinBones=PPC_LOAD_U32(meta+0x24);
    // The original mesh loop selects <=64 matrices per draw when its full
    // palette is larger. Keep the composed scratch span below the adjacent
    // 826FE7C8 once flag at82D68040; this admits at most255 authored entries,
    // while the actual shader palette remains64 entries.
    need(skinBones<=255,"Skin activation composed bone extent is unqualified");
    {static thread_local uint32_t skinBonesMetaSample{};
    if(sampleHotLog(skinBonesMetaSample))
        std::fprintf(stderr,"[NATIVE SKIN BONES] meta=%08X bones=%u\n",meta,skinBones);}
    const auto camera=s.runtime.engineDriver->cameraBinding();
    need(camera.camera==PPC_LOAD_U32(packet+8)&&camera.camera==PPC_LOAD_U32(0x82E3DD60)&&
         camera.viewport==std::array<uint32_t,6>{0,0,1280,720,0x3F800000,0},"Original skin camera differs");
    s.runtime.engineDriver->requireContext(PPC_LOAD_U32(packet+0x14));s.validatePool(v.pool);
    const auto passes=record.metadata->techniques();need(passes.size()==2&&PPC_LOAD_U32(w+0x28)==2,"Original skin technique count differs");
    const auto passIt=std::find_if(passes.begin(),passes.end(),[&](const auto& candidate){return candidate.handle==c.r5.u32;});
    need(passIt!=passes.end(),"Original skin requested pass is missing");const auto& pass=*passIt;
    const auto alphaPass=skinAlphaPass(v.source);
    if(s.runtime.resourceAudit.active())try {
        char asset[32],parameters[192];
        std::snprintf(asset,sizeof(asset),"source:%08X",v.source);
        std::snprintf(parameters,sizeof(parameters),"source=%08X vs=%08X ps=%08X technique=%08X pass=%08X context=%08X",
            v.source,pass.vertexShaderAddress,pass.pixelShaderAddress,pass.handle,pass.passHandle,pass.contextOffset);
        s.runtime.resourceAudit.observe("effect_pass",asset,uint32_t(c.lr),parameters,"reflected-selected",s.runtime.nativeDepthCopyCount.load());
    }catch(...){std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] skin pass capture failed\n");}
    {static thread_local uint32_t skinPassSample{};
    if(sampleHotLog(skinPassSample))
        std::fprintf(stderr,"[NATIVE SKIN PASS] name=%s handle=%08X pass=%08X ctx=%08X vs=%08X ps=%08X scalars=%zu samplers=%zu\n",
            pass.name.c_str(),pass.handle,pass.passHandle,pass.contextOffset,pass.vertexShaderAddress,pass.pixelShaderAddress,
            pass.scalars.size(),pass.samplers.size());}
    if(alphaFamily) {
        need(pass.name=="skinalpha"&&pass.handle==0x0007FFFC&&pass.passHandle==0x0007FFFE&&pass.contextOffset==alphaPass.context&&
             pass.vertexShaderAddress==alphaPass.vertex&&pass.pixelShaderAddress==alphaPass.pixel&&pass.scalars.size()==2&&pass.samplers.size()==alphaPass.samplers&&
             pass.scalars[0].sdkId==0x28&&pass.scalars[0].value==1&&pass.scalars[1].sdkId==0x30&&pass.scalars[1].value==1,
             "Original skin alpha pass metadata differs");
        constexpr std::array<uint32_t,6> samplerIds{0,4,8,0x10,0x14,0x18};
        for(uint32_t i=0;i<alphaPass.samplers;++i)need(pass.samplers[i].stage==i/6&&pass.samplers[i].sdkId==samplerIds[i%6]&&
             pass.samplers[i].value==(i%6<3?0u:1u),"Original skin alpha sampler rows differ");
    } else
        need(pass.name=="skin"&&pass.handle==0x0003FFFC&&pass.passHandle==0x0003FFFE&&pass.contextOffset==profile.context&&
             pass.vertexShaderAddress==profile.vertex&&pass.pixelShaderAddress==profile.pixel&&pass.scalars.size()==2&&pass.samplers.size()==profile.samplers&&
             pass.scalars[0].sdkId==0x28&&pass.scalars[0].value==1&&pass.scalars[1].sdkId==0x30&&pass.scalars[1].value==1,
             "Original skin pass metadata differs");
    s.runtime.pointer(v.cache,v.cacheBytes,false);const auto rows=v.cache+48;
    if(alphaFamily) {
        const auto h0=PPC_LOAD_U32(v.cache);
        if(h0==0x0003FFFCu) {
            const std::array<uint32_t,6> header{0x0003FFFCu,0,rows,rows+24,2,profile.samplers};
            for(uint32_t i=0;i<header.size();++i)need(PPC_LOAD_U32(v.cache+4*i)==header[i],"Original skin cache header differs");
        } else {
            need(h0==0x0007FFFCu&&PPC_LOAD_U32(v.cache+4)==0&&
                 PPC_LOAD_U32(v.cache+8)+24==PPC_LOAD_U32(v.cache+12)&&PPC_LOAD_U32(v.cache+16)==2&&
                 PPC_LOAD_U32(v.cache+20)==alphaPass.samplers,"Original skin alpha cache header differs");
            s.runtime.pointer(PPC_LOAD_U32(v.cache+8),64,false);
        }
    } else {
        const std::array<uint32_t,6> header{pass.handle,0,rows,rows+24,2,profile.samplers};
        for(uint32_t i=0;i<header.size();++i)need(PPC_LOAD_U32(v.cache+4*i)==header[i],"Original skin cache header differs");
    }
    need(!s.activeEdgeId&&!s.activeShadowId,"Another native effect remains active before skin");
    if(s.skinId==v.identity) {
        need(s.skinId==v.identity&&s.skinTyped==t&&s.skinCamera==camera.camera&&s.skinVertex&&s.skinPixel&&
             PPC_LOAD_U32(m+4)==w&&PPC_LOAD_U32(m+8)==v.identity&&PPC_LOAD_U32(m+0xC)==pass.handle&&
             PPC_LOAD_U32(w+0x2C)==v.cache,"Original skin active pair changed");
        if(s.screenReplacement.effect)s.requireScreenReplacement(base);
        else s.backend.requireSkinShaders(*s.skinVertex,*s.skinPixel);
        if(immediate)s.skinImmediate.activated=true;
        return;
    }
    if(s.zprepassId) {
        s.activeZPrepass(c,base);
        need(!s.zprepassMesh.native||s.zprepassMesh.phase==4,"Skin activation interrupted an unfinished depth mesh");
    } else if(s.skinId){if(s.screenReplacement.effect)s.requireScreenReplacement(base);else requireSkinSelection(s.skinTyped);}
    else if(s.rigidId){if(s.screenReplacement.effect)s.requireScreenReplacement(base);else requireRigidSelection(s.rigidTyped);}
    else need(!PPC_LOAD_U32(m+4)&&!PPC_LOAD_U32(m+0xC),"Another original effect remains selected before skin");
    need(!PPC_LOAD_U32(w+0x2C),"Skin cache was already selected before activation");
    const Graphics::CompiledMaterial* vertex=nullptr;const Graphics::CompiledMaterial* pixel=nullptr;
    const auto shaders=record.metadata->shaders();
    for(size_t i=0;i<shaders.size();++i) {
        if(shaders[i].originalAddress==pass.vertexShaderAddress)vertex=&record.shaderRecords.prepareForBind(record.shaders[i],s.compiler);
        if(shaders[i].originalAddress==pass.pixelShaderAddress)pixel=&record.shaderRecords.prepareForBind(record.shaders[i],s.compiler);
    }
    need(vertex&&pixel,"Skin shader pair is missing");s.backend.validateSkinShaders(*vertex,*pixel);
    // Skin filters the same 32-byte private window as rigid (F+120 counts
    // 16-byte vectors: 2 vectors here vs rigid's phrasing); F+124==1 keeps
    // the single-word shared-union check.
    need(word(record.metadata->body(),0x120)==2&&word(record.metadata->body(),0x124)==1,"Original skin dirty extent differs");
    auto prospective=s.runtime.engineDriver->effectiveState();
    for(const auto& row:pass.scalars)prospective.setScalar(row.sdkId,row.value);
    for(const auto& row:pass.samplers)prospective.setSampler(row.stage,row.sdkId,row.value);
    {EngineCpuCalls cpu(c,base);cpu.registers().lr=0x826B5FF8;cpu.invoke(0x826B4628,m);}
    need(!s.zprepassId&&!PPC_LOAD_U32(m+4)&&!PPC_LOAD_U32(m+0xC),"Original skin transition did not end its previous pass");
    PPC_STORE_U32(m+4,w);PPC_STORE_U32(m+8,v.identity);PPC_STORE_U32(m+0xC,pass.handle);
    {EngineCpuCalls cpu(c,base);cpu.registers().lr=0x826B604C;cpu.invoke(0x826B35D8,w+0x14,pass.handle,0);}
    if(alphaFamily) {
        const auto selected=PPC_LOAD_U32(w+0x2C);
        need(selected&&PPC_LOAD_U32(selected)==0x0007FFFC&&PPC_LOAD_U32(selected+4)==0&&
             PPC_LOAD_U32(selected+8)+24==PPC_LOAD_U32(selected+12)&&PPC_LOAD_U32(selected+16)==2&&
             PPC_LOAD_U32(selected+20)==alphaPass.samplers,"Original skin alpha selected cache differs");
        s.runtime.pointer(PPC_LOAD_U32(selected+8),64,false);record.view.cache=selected;
    }
    need(PPC_LOAD_U32(w+0x2C)==v.cache,"Original skin cache did not select its pass");
    for(const auto& row:pass.scalars)s.runtime.engineDriver->directScalar(base,row.sdkId,row.value);
    for(const auto& row:pass.samplers)s.runtime.engineDriver->directSampler(base,row.stage,row.sdkId,row.value);
    record.privateModified.fill(0);std::fill_n(record.privateModified.begin(),16,uint8_t(0xFF));
    if(!PPC_LOAD_U32(0x82D00F80)){auto* dirty=s.runtime.pointer(v.pool,128,true);std::memset(dirty,0,128);std::memset(dirty,0xFF,16);}
    s.backend.bindSkinShaders(*vertex,*pixel);s.screenReplacement={};s.skinId=v.identity;s.skinTyped=t;s.skinCamera=camera.camera;
    s.skinVertex=vertex;s.skinPixel=pixel;
    s.rigidId=s.rigidTyped=s.rigidCamera=0;s.rigidVertex=s.rigidPixel=nullptr;
    if(immediate)s.skinImmediate.activated=true;
    {static thread_local uint32_t skinBeginBoundSample{};
    if(sampleHotLog(skinBeginBoundSample))
        std::fprintf(stderr,"[NATIVE SKIN BEGIN] id=%08X typed=%08X camera=%08X; original cache and native VS%08X/PS%08X bound\n",
            v.identity,t,camera.camera,pass.vertexShaderAddress,pass.pixelShaderAddress);}
}
void EngineEffects::beginEdge(PPCContext& c,uint8_t* base) {
    observeProducerEntry(c,base,"edge_begin");
    auto& s=*state;s.require(base);
    // The original manager checks wrapper/technique before any SDK bind.
    // Preserve that proven no-op while a completed screen draw owns the
    // physical bindings; scene commit/draw still require the scene shaders.
    if(currentContext==&c&&c.lastFunction==0x826B5FC0&&s.screenReplacement.effect&&
       !s.rigidImmediate.active&&!s.skinImmediate.active&&
       c.r3.u32==s.screenReplacement.manager&&c.r4.u32==s.screenReplacement.wrapper&&
       c.r5.u32==s.screenReplacement.technique) {
        s.requireScreenReplacement(base);return;
    }
    if(c.lastFunction==0x826B5FC0&&uint32_t(c.lr)==0x823CAC78){beginVfxRigid(c,base);return;}
    if(c.lastFunction==0x826B5FC0&&uint32_t(c.lr)==0x8273A93C){beginMono(c,base);return;}
    if(c.lastFunction==0x826B5FC0&&uint32_t(c.lr)==0x823CA760){beginRigid(c,base);return;}
    if(c.lastFunction==0x826B5FC0&&uint32_t(c.lr)==0x823CAA0C){beginSkin(c,base);return;}
    if(c.lastFunction==0x826B5FC0&&uint32_t(c.lr)==0x827406C8){beginZPrepass(c,base);return;}
    if(c.lastFunction==0x826B5FC0&&uint32_t(c.lr)==0x8270614C) {
        s.runtime.pointer(c.r31.u32,0x6C0,false);
        const uint32_t alphaEntryQueue=PPC_LOAD_U32(c.r31.u32+0xA8),alphaEntryCount=PPC_LOAD_U32(c.r31.u32+0xAC);
        const uint32_t alphaEntryAlpha=PPC_LOAD_U32(c.r31.u32+0x678),alphaEntryFull=PPC_LOAD_U32(c.r31.u32+0x680);
        const unsigned long long alphaEntryDraws=static_cast<unsigned long long>(s.backend.shadowMeshDrawCount());
        {static thread_local uint32_t shadowAlphaEntrySample{};
        if(sampleHotLog(shadowAlphaEntrySample))
            std::fprintf(stderr,"[NATIVE SHADOW ALPHA ENTRY] typed=%08X technique=%08X queue=%08X count=%u character_draws=%llu alpha_handle=%08X full_handle=%08X\n",
                c.r31.u32,c.r5.u32,alphaEntryQueue,alphaEntryCount,alphaEntryDraws,alphaEntryAlpha,alphaEntryFull);}
        if(s.activeShadowId&&s.backend.shadowMeshDrawCount()&&!s.runtime.frameCaptureDirectory.empty()&&!s.shadowDepthCaptured) {
            const auto binding=s.runtime.engineDriver->cameraBinding();
            const auto target=s.runtime.engineDriver->depth(binding.depthIdentity);
            const auto bytes=s.backend.readbackDepthTarget(target);
            size_t occupied=0;for(size_t i=0;i<bytes.size();i+=8){float z{};std::memcpy(&z,bytes.data()+i,4);occupied+=z!=0;}
            const auto stem=s.runtime.frameCaptureDirectory/"character-shadow-depth";
            std::ofstream raw(stem.string()+".f32s8",std::ios::binary);raw.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
            std::ofstream meta(stem.string()+".json");meta<<"{\"width\":"<<target->pixelWidth()<<",\"height\":"<<target->pixelHeight()
                <<",\"format\":\"FLOAT32_LE_STENCIL8_PAD24\",\"shadow_mesh_draws\":"<<s.backend.shadowMeshDrawCount()
                <<",\"nonzero_depth_pixels\":"<<occupied<<",\"capture_source\":\"private_shadow_depth_renderer_readback\",\"front_copy_completed\":false}\n";
            raw.close();meta.close();
            need(bool(raw)&&bool(meta),"Character shadow depth capture failed");
            s.shadowDepthCaptured=true;
        }
    }
    if(c.lastFunction==0x826B5FC0&&(c.r5.u32==0x0007FFFC||uint32_t(c.lr)==0x8270614C)){beginShadowDepth(c,base);return;}
    if(c.lastFunction!=0x826B5FC0 || c.r5.u32!=0x0003FFFC) {
        // Keep this preflight free of guest reads and mutations. A different
        // original technique needs its own shader/draw qualification; report
        // the actual request so the next boundary can be identified offline.
        char message[240];std::snprintf(message,sizeof(message),
            "Unqualified native edge activation caller/technique: entry=%08X caller=%08X manager=%08X wrapper=%08X technique=%08X typed=%08X",
            c.lastFunction,uint32_t(c.lr),c.r3.u32,c.r4.u32,c.r5.u32,c.r31.u32);
        throw Failure(message);
    }
    auto& record=s.edgeOwner(c,base,c.r4.u32);const auto& v=record.view;
    const auto& profile=postProfile(v.source);
    need(uint32_t(c.lr)==profile.beginCaller,"Unqualified native post-effect begin caller");
    if(v.source==0x82034008) {
        constexpr std::array<uint32_t,4> loop={10,0,1,0};
        for(size_t i=0;i<loop.size();++i)
            need(word(record.metadata->body(),8992+4*i)==loop[i],"Native edgeAA integer loop context changed");
    }
    need(c.r3.u32==v.manager && PPC_LOAD_U32(c.r31.u32+0x10)==v.manager &&
         PPC_LOAD_U32(c.r31.u32+0x1C)==v.identity && PPC_LOAD_U32(profile.technique)==c.r5.u32,
         "Native edge activation lost its original typed/manager association");
    const auto camera=s.runtime.engineDriver->cameraBinding();
    need(camera.camera==c.r30.u32 && camera.camera==PPC_LOAD_U32(0x82E3DD60),"Native edge activation camera differs");
    s.edgeCache(record);
    const auto m=v.manager,w=v.wrapper;
    if(s.activeEdgeId) {
        need(s.activeEdgeId==v.identity && s.edgeTyped==c.r31.u32 && s.edgeCamera==camera.camera &&
             PPC_LOAD_U32(m+4)==w && PPC_LOAD_U32(m+8)==v.identity && PPC_LOAD_U32(m+0xC)==c.r5.u32,
             "Native effect selection changed outside its active edge owner");
        return; // Original pair-cache hit performs neither state save nor bind.
    }
    need(!PPC_LOAD_U32(m+4) && !PPC_LOAD_U32(m+0xC),"Another original effect remains active");
    need(record.shaders.size()==2,"Native edge requires exactly two original shader records");
    const Graphics::CompiledMaterial* vertex=nullptr;const Graphics::CompiledMaterial* pixel=nullptr;
    for(const auto shader:record.shaders) {
        const auto& compiled=record.shaderRecords.prepareForBind(shader,s.compiler);
        if(compiled.originalAddress()==profile.vertex)vertex=&compiled;
        else if(compiled.originalAddress()==profile.pixel)pixel=&compiled;
        else throw Failure("Unqualified shader in native edge pass");
    }
    need(vertex && pixel,"Native edge shader pair is incomplete");s.backend.validateEdgeShaders(*vertex,*pixel);
    const auto& pass=record.metadata->techniques().front();auto prospective=s.runtime.engineDriver->effectiveState();
    for(const auto& row:pass.scalars) {
        prospective.setScalar(row.sdkId,row.value);
        const auto fields=Graphics::scalarStateEvidence();
        const auto field=std::find_if(fields.begin(),fields.end(),[&](const auto& f){return f.id==row.sdkId;});
        need(field!=fields.end() && PPC_LOAD_U32(0x82CD28B8+3*row.sdkId+4)==field->setterAddress,"Native edge scalar dispatch changed");
    }
    for(const auto& row:pass.samplers) {
        prospective.setSampler(row.stage,row.sdkId,row.value);
        const auto fields=Graphics::samplerStateEvidence();
        const auto field=std::find_if(fields.begin(),fields.end(),[&](const auto& f){return f.id==row.sdkId;});
        need(field!=fields.end() && PPC_LOAD_U32(0x82CD2D78+3*row.sdkId+4)==field->setterAddress,"Native edge sampler dispatch changed");
    }
    const auto dirtyBytes=16*word(record.metadata->body(),0x120);
    need(dirtyBytes==16,"Original edge private dirty-block count differs");
    // SDK E is a native identity; it is never dereferenced. Original manager
    // stores and the actual CPU cache save/apply order are preserved here.
    s.activeEdgeId=v.identity;s.edgeTyped=c.r31.u32;s.edgeCamera=camera.camera;
    PPC_STORE_U32(m+4,w);PPC_STORE_U32(m+8,v.identity);PPC_STORE_U32(m+0xC,c.r5.u32);
    {EngineCpuCalls cpu(c,base);cpu.registers().lr=0x826B604C;cpu.invoke(0x826B35D8,w+0x14,c.r5.u32,0);}
    need(PPC_LOAD_U32(w+0x2C)==v.cache,"Original edge state cache did not select its pass");
    // Original pass also issues the same literal states directly to the SDK,
    // regardless of application-cache hits. Keep those separate from RW caches.
    for(const auto& row:pass.scalars)s.runtime.engineDriver->directScalar(base,row.sdkId,row.value);
    for(const auto& row:pass.samplers)s.runtime.engineDriver->directSampler(base,row.stage,row.sdkId,row.value);
    std::fill_n(record.privateModified.begin(),dirtyBytes,uint8_t(0xFF));
    s.backend.bindEdgeShaders(*vertex,*pixel);s.screenReplacement={};
    s.edgeVertex=vertex;s.edgePixel=pixel;
    {static thread_local uint32_t edgeBeginSample{};
    if(sampleHotLog(edgeBeginSample))
        std::fprintf(stderr,"[NATIVE %s BEGIN] id=%08X typed=%08X camera=%08X pass=0003FFFE; original state save/apply and real VS/PS bind completed; parameters/draw separate\n",profile.name,v.identity,s.edgeTyped,s.edgeCamera);}
}
void EngineEffects::preflightScreenReplacement(uint8_t* base) const {
    auto& s=*state;s.require(base);
    if(!s.rigidId&&!s.skinId)return;
    if(s.screenReplacement.effect){s.requireScreenReplacement(base);return;}
    if(s.rigidId) {
        need(!s.rigidImmediate.active||!s.rigidImmediate.activated,"Screen draw interrupted an original rigid draw lifetime");
        need(!s.find(s.rigidId).recordingContext,"Screen draw interrupted an original rigid recording");
        requireRigidSelection(s.rigidTyped);
    } else {
        need(!s.skinImmediate.active||!s.skinImmediate.activated,"Screen draw interrupted an original skin draw lifetime");
        need(!s.find(s.skinId).recordingContext,"Screen draw interrupted an original skin recording");
        requireSkinSelection(s.skinTyped);
    }
}
void EngineEffects::preflightBindingReset(uint8_t* base) const {
    auto& s=*state;s.require(base);
    s.resetPreflight={};s.resetPreflightCpu=currentContext;
    s.resetPreflightFrame=currentContext?currentContext->r1.u32:0;
    const auto selection=s.retainedSelection();if(!selection.id)return;
    if(s.screenReplacement.effect)s.requireScreenReplacement(base);
    else {
        need(!s.find(selection.id).recordingContext,"Binding reset interrupted an original effect recording");
        need(currentContext,"Original binding reset lacks its CPU owner frame");
        // Capture only a fully qualified selected owner. Camera/physical program
        // checks happen before the real reset; cleanup later uses that provenance.
        if(s.rigidId||s.skinId)preflightScreenReplacement(base);
        else if(s.monoId)s.activeMono(*currentContext,base);
        else if(s.zprepassId)s.activeZPrepass(*currentContext,base);
        else if(s.activeShadowId)s.activeShadowRecord(*currentContext,base,s.shadowTyped,s.shadowTechnique==0x0003FFFC);
        else s.activeRecord();
    }
    s.requireResetLogicalOwner(selection);
    const auto& v=s.find(selection.id).view;
    s.resetPreflight.effect=selection.id;s.resetPreflight.typed=selection.typed;
    s.resetPreflight.manager=v.manager;s.resetPreflight.wrapper=v.wrapper;
    s.resetPreflight.technique=selection.technique;s.resetPreflight.cache=selection.cache;
}
void EngineEffects::completeScreenReplacement(uint8_t* base,const Graphics::NativeScreenReplacementReceipt& receipt,
    uint32_t declaration,uint32_t vertexCache,uint32_t pixelCache) {
    auto& s=*state;s.require(base);if(!s.rigidId&&!s.skinId)return;
    const auto id=s.rigidId?s.rigidId:s.skinId,typed=s.rigidId?s.rigidTyped:s.skinTyped;
    const auto& v=s.find(id).view;
    State::ScreenReplacement replacement;replacement.effect=id;replacement.typed=typed;
    replacement.manager=v.manager;replacement.wrapper=v.wrapper;replacement.cache=v.cache;
    replacement.technique=(s.rigidId?s.alphaRigid():s.alphaSkin())?0x0007FFFCu:0x0003FFFCu;
    replacement.declaration=declaration;replacement.vertexCache=vertexCache;replacement.pixelCache=pixelCache;
    replacement.receipt=receipt;s.screenReplacement=replacement;s.requireScreenReplacement(base);
}
void EngineEffects::completeRestoredScreenReplacement(uint8_t* base,uint64_t before,
    uint32_t declaration,uint32_t vertexCache,uint32_t pixelCache) {
    auto& s=*state;s.require(base);s.backend.requireCompletedModulatedPostFilter(before);
    need(declaration==PPC_LOAD_U32(0x82DFEB30)&&vertexCache==PPC_LOAD_U32(0x82CF231C)&&
         pixelCache==PPC_LOAD_U32(0x82CF2304),"Original restored overlay cache identity differs");
    if(!s.rigidId&&!s.skinId)return;
    if(!s.screenReplacement.effect) {
        if(s.rigidId)requireRigidSelection(s.rigidTyped);else requireSkinSelection(s.skinTyped);
        return; // The scoped post-filter restored the original scene pair.
    }
    auto& replacement=s.screenReplacement;
    if(replacement.bindingReset)s.backend.requireBindingReset(replacement.reset);
    else if(replacement.inputOnly)s.backend.requireScreenInputReplacement(replacement.inputs);
    else replacement.receipt=s.backend.completedRestoredScreenReplacement(before,replacement.receipt);
    replacement.declaration=declaration;
    replacement.vertexCache=vertexCache;replacement.pixelCache=pixelCache;
    s.requireScreenReplacement(base);
}
void EngineEffects::completeScreenInputReplacement(uint8_t* base,const Graphics::NativeScreenInputReceipt& inputs,
    uint32_t declaration,uint32_t vertexCache,uint32_t pixelCache,bool pendingDraw) {
    auto& s=*state;s.require(base);s.backend.requireScreenInputReplacement(inputs);
    need(declaration==PPC_LOAD_U32(0x82DFEB34)&&vertexCache==PPC_LOAD_U32(0x82CF2340),
         "Original sprite input cache identity differs");
    if(!s.rigidId&&!s.skinId)return;
    const auto id=s.rigidId?s.rigidId:s.skinId,typed=s.rigidId?s.rigidTyped:s.skinTyped;
    const auto& v=s.find(id).view;State::ScreenReplacement replacement;
    replacement.effect=id;replacement.typed=typed;replacement.manager=v.manager;replacement.wrapper=v.wrapper;
    replacement.cache=v.cache;replacement.technique=(s.rigidId?s.alphaRigid():s.alphaSkin())?0x0007FFFCu:0x0003FFFCu;
    replacement.declaration=declaration;replacement.vertexCache=vertexCache;replacement.pixelCache=pixelCache;
    replacement.inputs=inputs;replacement.inputOnly=true;replacement.pendingDraw=pendingDraw;
    s.screenReplacement=std::move(replacement);s.requireScreenReplacement(base,true);
}
void EngineEffects::requirePendingScreenInputs(uint8_t* base,const Graphics::NativeScreenInputReceipt& inputs) const {
    auto& s=*state;s.require(base);s.backend.requireScreenInputReplacement(inputs);
    if(!s.rigidId&&!s.skinId)return;
    need(s.screenReplacement.inputOnly&&s.screenReplacement.pendingDraw,
         "Original sprite completion lacks its pending input transaction");
    s.requireScreenReplacement(base,true);
}
void EngineEffects::completeScreenBatchRetirement(uint8_t* base,const Graphics::NativeScreenBatchReceipt& batch) {
    auto& s=*state;s.require(base);s.backend.requireScreenBatchRetirement(batch);
    if(!s.rigidId&&!s.skinId)return;
    if(!s.screenReplacement.effect) {
        if(s.rigidId)requireRigidSelection(s.rigidTyped);else requireSkinSelection(s.skinTyped);
        return; // No screen displacement; the original scene shaders remain.
    }
    auto& replacement=s.screenReplacement;
    need(!replacement.pendingDraw&&!PPC_LOAD_U32(0x82CD1A68)&&
         PPC_LOAD_U32(0x82CD1A6C)==replacement.vertexCache&&PPC_LOAD_U32(0x82CD1A70)==replacement.pixelCache,
         "Original sprite batch changed retained shader caches");
    replacement.declaration=0;replacement.batch=batch;replacement.inputsRetired=true;
    s.requireScreenReplacement(base);
}
void EngineEffects::completeBindingReset(PPCContext& incoming,uint8_t* base,const Graphics::NativeBindingResetReceipt& reset) {
    auto& s=*state;s.require(base);s.backend.requireBindingReset(reset);
    need(!PPC_LOAD_U32(0x82CD1A68)&&!PPC_LOAD_U32(0x82CD1A6C)&&!PPC_LOAD_U32(0x82CD1A70),
         "Original completed binding reset did not clear shader/declaration caches");
    const auto selection=s.retainedSelection();
    need(&incoming==s.resetPreflightCpu&&incoming.r1.u32==s.resetPreflightFrame&&currentContext&&
         currentContext->r1.u32+0x100==incoming.r1.u32&&PPC_LOAD_U32(currentContext->r1.u32)==incoming.r1.u32&&
         s.resetPreflight.effect==selection.id&&s.resetPreflight.typed==selection.typed&&
         s.resetPreflight.technique==selection.technique&&s.resetPreflight.cache==selection.cache,
         "Original binding reset completed without its preflight owner frame");
    if(!selection.id){s.resetPreflight={};s.resetPreflightCpu=nullptr;s.resetPreflightFrame=0;return;}
    const auto id=selection.id,typed=selection.typed;
    const auto& v=s.find(id).view;State::ScreenReplacement replacement;
    need(s.resetPreflight.manager==v.manager&&s.resetPreflight.wrapper==v.wrapper,
         "Original binding reset changed its preflight manager/wrapper");
    replacement.effect=id;replacement.typed=typed;replacement.manager=v.manager;replacement.wrapper=v.wrapper;
    replacement.cache=selection.cache;replacement.technique=selection.technique;
    replacement.reset=reset;replacement.bindingReset=true;s.screenReplacement=std::move(replacement);
    s.requireScreenReplacement(base);
    s.resetPreflight={};s.resetPreflightCpu=nullptr;s.resetPreflightFrame=0;
}
void EngineEffects::completeFixedFunctionDeclaration(uint8_t* base,uint32_t declaration) {
    auto& s=*state;s.require(base);
    if(!s.retainedSelection().id)return;
    if(!s.screenReplacement.effect||!s.screenReplacement.bindingReset)
        throw Failure("Original fixed-function declaration lacks its completed binding reset");
    auto& replacement=s.screenReplacement;
    need(!replacement.pendingDraw&&declaration&&declaration==PPC_LOAD_U32(0x82D101D8)&&
         declaration==PPC_LOAD_U32(0x82CD1A68)&&!PPC_LOAD_U32(0x82CD1A6C)&&!PPC_LOAD_U32(0x82CD1A70),
         "Original fixed-function declaration changed shader caches or input identity");
    s.backend.flushIm2D();s.backend.requireBindingReset(replacement.reset);
    replacement.declaration=declaration;replacement.vertexCache=replacement.pixelCache=0;
    s.requireScreenReplacement(base);
}
void EngineEffects::endEdge(PPCContext& c,uint8_t* base,bool wrapper) {
    auto& s=*state;s.require(base);
    need(currentContext==&c && c.lastFunction==(wrapper?0x826B4B18u:0x826B4628u),"Unqualified native effect end frame");
    if(wrapper)s.runtime.pointer(c.r3.u32,0x30,false);
    const uint32_t m=wrapper?PPC_LOAD_U32(c.r3.u32+0xC):c.r3.u32;
    need(m && m==PPC_LOAD_U32(0x82D08BFC),"Native effect end manager differs");s.runtime.pointer(m,0x18,false);
    if(s.rigidId) {
        auto& record=s.find(s.rigidId);const auto& v=record.view;
        need(!s.rigidImmediate.active||(!s.rigidImmediate.activated),
             "Original rigid end interrupted an immediate draw lifetime");
        // Sky lives in the rigid fields but keeps its rigidalpha selection.
        // Chocolate and 168F8 likewise once selected.
        const auto selected=s.alphaRigid()?0x0007FFFCu:0x0003FFFCu;
        need(!s.activeEdgeId&&!s.activeShadowId&&!s.zprepassId&&v.manager==m&&s.rigidVertex&&s.rigidPixel&&
             (!wrapper||c.r3.u32==v.wrapper)&&PPC_LOAD_U32(m+4)==v.wrapper&&PPC_LOAD_U32(m+8)==v.identity&&
             PPC_LOAD_U32(m+0xC)==selected&&PPC_LOAD_U32(v.wrapper+0x2C)==v.cache,"Original rigid end association differs");
        if(s.screenReplacement.effect)s.requireScreenReplacement(base);
        else s.requireRigidPhysical("endEdge rigid");
        {EngineCpuCalls cpu(c,base);cpu.registers().lr=wrapper?0x826B4B5C:0x826B466C;cpu.invoke(0x826B37B8,v.wrapper+0x14);}
        need(!PPC_LOAD_U32(v.wrapper+0x2C),"Original rigid cache did not restore");
        if(s.screenReplacement.effect) {
            if(s.screenReplacement.bindingReset)s.backend.retireBindingReset(s.screenReplacement.reset);
            else if(s.screenReplacement.inputOnly)s.backend.retireScreenInputReplacement(s.screenReplacement.inputs);
            else s.backend.retireScreenReplacement(s.screenReplacement.receipt);
        }
        PPC_STORE_U32(m+4,0);PPC_STORE_U32(m+0xC,0);
        if(s.alphaRigid())record.view.cache=PPC_LOAD_U32(v.wrapper+0x1C);
        s.screenReplacement={};s.rigidId=0;s.rigidTyped=0;s.rigidCamera=0;s.rigidVertex=nullptr;s.rigidPixel=nullptr;return;
    }
    if(s.skinId) {
        auto& record=s.find(s.skinId);const auto& v=record.view;
        need(!s.skinImmediate.active||!s.skinImmediate.activated,
             "Original skin end interrupted an immediate draw lifetime");
        const auto selected=s.alphaSkin()?0x0007FFFCu:0x0003FFFCu;
        need(!s.activeEdgeId&&!s.activeShadowId&&!s.zprepassId&&!s.rigidId&&v.manager==m&&s.skinVertex&&s.skinPixel&&
             (!wrapper||c.r3.u32==v.wrapper)&&PPC_LOAD_U32(m+4)==v.wrapper&&PPC_LOAD_U32(m+8)==v.identity&&
             PPC_LOAD_U32(m+0xC)==selected&&PPC_LOAD_U32(v.wrapper+0x2C)==v.cache,"Original skin end association differs");
        if(s.screenReplacement.effect)s.requireScreenReplacement(base);
        else s.backend.requireSkinShaders(*s.skinVertex,*s.skinPixel);
        {EngineCpuCalls cpu(c,base);cpu.registers().lr=wrapper?0x826B4B5C:0x826B466C;cpu.invoke(0x826B37B8,v.wrapper+0x14);}
        need(!PPC_LOAD_U32(v.wrapper+0x2C),"Original skin cache did not restore");
        if(s.screenReplacement.effect) {
            if(s.screenReplacement.bindingReset)s.backend.retireBindingReset(s.screenReplacement.reset);
            else if(s.screenReplacement.inputOnly)s.backend.retireScreenInputReplacement(s.screenReplacement.inputs);
            else s.backend.retireScreenReplacement(s.screenReplacement.receipt);
        }
        PPC_STORE_U32(m+4,0);PPC_STORE_U32(m+0xC,0);
        if(s.alphaSkin())record.view.cache=PPC_LOAD_U32(v.wrapper+0x1C);
        s.screenReplacement={};s.skinId=0;s.skinTyped=0;s.skinCamera=0;s.skinVertex=nullptr;s.skinPixel=nullptr;return;
    }
    if(s.monoId) {
        auto& record=s.find(s.monoId);const auto& v=record.view;
        if(s.screenReplacement.bindingReset)s.requireScreenReplacement(base);else s.activeMono(c,base);
        need(v.manager==m&&(!wrapper||c.r3.u32==v.wrapper)&&(!s.monoMesh.native||s.monoMesh.phase==4),
             "Original mono ended with an unfinished mesh");
        {EngineCpuCalls cpu(c,base);cpu.registers().lr=wrapper?0x826B4B5C:0x826B466C;cpu.invoke(0x826B37B8,v.wrapper+0x14);}
        need(!PPC_LOAD_U32(v.wrapper+0x2C),"Original mono cache did not restore");
        if(s.screenReplacement.bindingReset)s.backend.retireBindingReset(s.screenReplacement.reset);
        s.screenReplacement={};
        PPC_STORE_U32(m+4,0);PPC_STORE_U32(m+0xC,0);
        s.monoId=0;s.monoTyped=0;s.monoCamera=0;s.monoVertex=nullptr;s.monoPixel=nullptr;s.monoCommit.reset();s.monoMesh={};s.recycleTempUpload(s.monoReplay.buffers);s.monoReplay={};s.monoPacket=0;s.monoFrame=0;s.monoTechnique=0;s.monoImmediate={};s.monoTextures={};s.monoRecording={};s.monoWorld=s.monoBoolean=s.monoMaterial=s.monoImmediatePath=s.monoRecordingPath=false;return;
    }
    if(s.zprepassId) {
        auto& record=s.find(s.zprepassId);const auto& v=record.view;
        if(s.screenReplacement.bindingReset)s.requireScreenReplacement(base);else s.activeZPrepass(c,base);
        need(v.manager==m&&(!wrapper||c.r3.u32==v.wrapper)&&(!s.zprepassMesh.native||s.zprepassMesh.phase==4),
             "Original zprepass ended with an unfinished mesh");
        {EngineCpuCalls cpu(c,base);cpu.registers().lr=wrapper?0x826B4B5C:0x826B466C;cpu.invoke(0x826B37B8,v.wrapper+0x14);}
        need(!PPC_LOAD_U32(v.wrapper+0x2C),"Original zprepass cache did not restore");
        if(s.screenReplacement.bindingReset)s.backend.retireBindingReset(s.screenReplacement.reset);
        s.screenReplacement={};
        PPC_STORE_U32(m+4,0);PPC_STORE_U32(m+0xC,0);
        s.zprepassId=0;s.zprepassTyped=0;s.zprepassCamera=0;s.zprepassVertex=nullptr;s.zprepassCommit.reset();s.zprepassMesh={};return;
    }
    if(s.activeShadowId){
        auto& record=s.find(s.activeShadowId);const auto& v=record.view;
        need(!s.activeEdgeId&&v.manager==m&&s.shadowVertex&&(!wrapper||c.r3.u32==v.wrapper)&&
             PPC_LOAD_U32(m+4)==v.wrapper&&PPC_LOAD_U32(m+8)==v.identity&&PPC_LOAD_U32(m+0xC)==s.shadowTechnique&&
             (s.shadowTechnique==0x0007FFFC||s.shadowTechnique==0x0003FFFC)&&
             PPC_LOAD_U32(v.wrapper+0x2C)==v.cache+(s.shadowTechnique==0x0003FFFC?0:24),"Character shadow end lost its active association");
        if(s.screenReplacement.bindingReset)s.requireScreenReplacement(base);
        else if(s.shadowTechnique==0x0003FFFC){need(s.shadowPixel,"Character shadow alpha pixel owner is absent");s.backend.requireShadowAlphaShaders(*s.shadowVertex,*s.shadowPixel);}
        else s.backend.requireShadowDepthShader(*s.shadowVertex);
        {EngineCpuCalls cpu(c,base);cpu.registers().lr=wrapper?0x826B4B5C:0x826B466C;cpu.invoke(0x826B37B8,v.wrapper+0x14);}
        need(!PPC_LOAD_U32(v.wrapper+0x2C),"Original character shadow state cache did not restore");
        if(s.screenReplacement.bindingReset)s.backend.retireBindingReset(s.screenReplacement.reset);
        s.screenReplacement={};
        PPC_STORE_U32(m+0xC,0);PPC_STORE_U32(m+4,0);
        s.characterMesh={};
        s.shadowPalettePrelude={};
        s.activeShadowId=0;s.shadowTyped=0;s.shadowCamera=0;s.shadowTechnique=0;s.shadowVertex=nullptr;s.shadowPixel=nullptr;s.shadowCommit.reset();return;
    }
    if(!PPC_LOAD_U32(m+0xC)) {need(!s.activeEdgeId,"Native edge selection disappeared before end");return;}
    need(s.activeEdgeId,"Unported original effect remains active at end");
    need(!s.edgeVertices && !s.edgeDeclaration,"Native edge rectangle remains unfinished at end");
    auto& record=s.find(s.activeEdgeId);const auto& v=record.view;s.edgeCache(record);
    need(v.manager==m && PPC_LOAD_U32(m+4)==v.wrapper && PPC_LOAD_U32(m+8)==v.identity &&
         PPC_LOAD_U32(m+0xC)==0x0003FFFC && (!wrapper ||
         (c.r3.u32==v.wrapper && uint32_t(c.lr)==postProfile(v.source).endCaller && c.r31.u32==s.edgeTyped)),
         "Native edge end lost its active association");
    if(s.screenReplacement.bindingReset)s.requireScreenReplacement(base);
    {EngineCpuCalls cpu(c,base);cpu.registers().lr=wrapper?0x826B4B5C:0x826B466C;cpu.invoke(0x826B37B8,v.wrapper+0x14);}
    need(!PPC_LOAD_U32(v.wrapper+0x2C),"Original edge state cache did not restore/end");
    if(s.screenReplacement.bindingReset)s.backend.retireBindingReset(s.screenReplacement.reset);
    s.screenReplacement={};
    // The qualified single pass has no default-state override. Original end
    // retains shaders, textures, constants, declaration and viewport rectangle.
    PPC_STORE_U32(m+0xC,0);PPC_STORE_U32(m+4,0);
    s.activeEdgeId=0;s.edgeTyped=0;s.edgeCamera=0;
    s.edgeVertex=nullptr;s.edgePixel=nullptr;s.edgeCommit.reset();s.edgeSource.reset();s.edgeSourceId=0;
    s.edgeAAInputs={};s.edgeAAIds={};
}
void EngineEffects::commitEdge(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.require(base);
    if(c.lastFunction==0x826B3980&&uint32_t(c.lr)==0x82740820){commitZPrepass(c,base);return;}
    if(s.activeShadowId){commitShadowDepth(c,base);return;}
    auto& record=s.activeRecord();const auto& v=record.view;const bool aa=v.source==0x8202FA78;
    const auto& profile=postProfile(v.source);
    need(currentContext==&c && c.lastFunction==0x826B3980 && uint32_t(c.lr)==profile.commitCaller &&
         c.r31.u32==s.edgeTyped+profile.typedBias && c.r30.u32==0x82061428 && c.r1.u32>=0x200 && !(c.r1.u32&15) &&
         PPC_LOAD_U32(c.r1.u32)==c.r1.u32+profile.frame,"Unqualified original post-effect commit frame");
    need(c.r3.u32==v.wrapper && record.parameters && !s.edgeVertices && !s.edgeDeclaration,
         "Native edge commit lacks its original private storage or has an unfinished draw");
    const auto values=parameterValues(v.identity);
    need(values.size()==profile.words,"Native post-effect parameter extent differs");
    if(v.source==0x82034008) {
        auto& driver=*s.runtime.engineDriver;auto& copies=driver.sceneCopies();
        const std::array<uint32_t,5> ids={values[240/4],values[304/4],values[560/4],values[624/4],values[688/4]};
        need(ids[3]==PPC_LOAD_U32(0x82D09894) && ids[4]==PPC_LOAD_U32(0x82D6C7F0),"Native edgeAA scene publication differs");
        Graphics::EdgeAAInputs inputs;
        inputs.color=driver.sampledColorCopy(ids[0],s.edgeCamera);
        inputs.depth=driver.sampledDepthCopy(ids[1],s.edgeCamera);
        inputs.palette=driver.itxdTextures().paletteFromHeader(base,ids[2]);
        inputs.base=copies.edgeSource(ids[3],s.edgeCamera);inputs.line=copies.aaSource(ids[4],s.edgeCamera);
        Graphics::EdgeAAConstants constants{};
        constexpr std::array<uint32_t,8> offsets={432,448,464,480,496,512,528,544};
        constexpr std::array<uint32_t,3> highOffsets={416,400,384};
        for(size_t i=0;i<offsets.size();++i)for(size_t lane=0;lane<4;++lane)
            constants.c20_27[i][lane]=std::bit_cast<float>(values[offsets[i]/4+lane]);
        for(size_t i=0;i<highOffsets.size();++i)for(size_t lane=0;lane<4;++lane)
            constants.c48_50[i][lane]=std::bit_cast<float>(values[highOffsets[i]/4+lane]);
        std::array<D3D11_SAMPLER_DESC,5> samplers{};
        for(uint32_t i=0;i<samplers.size();++i)samplers[i]=s.edgeState(i);
        auto* poolDirty=s.runtime.pointer(v.pool,128,true);
        if(!s.runtime.frameCaptureDirectory.empty()) {
            const auto request=s.runtime.frameCaptureDirectory/"edgeaa-input.request";
            if(std::filesystem::exists(request)) {
                const auto color=s.backend.readbackTarget(inputs.color);
                const auto depth=s.backend.readbackDepthTarget(inputs.depth);
                const auto basePixels=s.backend.readbackTarget(inputs.base);
                const auto line=s.backend.readbackTarget(inputs.line);
                const auto palette=s.backend.readback(inputs.palette);
                const auto write=[&](const char* name,const std::vector<uint8_t>& bytes) {
                    std::ofstream out(s.runtime.frameCaptureDirectory/name,std::ios::binary);
                    out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
                    if(!out)throw Failure("EdgeAA input diagnostic write failed");
                };
                write("edgeaa-color.rgb10a2",color);
                write("edgeaa-depth.d32s8",depth);
                write("edgeaa-base.rgb10a2",basePixels);
                write("edgeaa-line.rgb10a2",line);
                write("edgeaa-palette.rgba8",palette);
                std::ofstream meta(s.runtime.frameCaptureDirectory/"edgeaa-input.json");
                meta<<"{\"color_id\":"<<ids[0]<<",\"depth_id\":"<<ids[1]<<",\"palette_id\":"<<ids[2]
                    <<",\"base_id\":"<<ids[3]<<",\"line_id\":"<<ids[4]
                    <<",\"color_width\":"<<inputs.color->width<<",\"color_height\":"<<inputs.color->height
                    <<",\"base_width\":"<<inputs.base->width<<",\"base_height\":"<<inputs.base->height
                    <<",\"line_width\":"<<inputs.line->width<<",\"line_height\":"<<inputs.line->height
                    <<",\"palette_width\":"<<inputs.palette->width<<",\"palette_height\":"<<inputs.palette->height
                    <<",\"palette_bytes\":"<<palette.size()<<",\"c20_27_bits\":[";
                bool first=true;
                for(const auto& row:constants.c20_27)for(const auto value:row) {
                    if(!first)meta<<',';first=false;meta<<std::bit_cast<uint32_t>(value);
                }
                meta<<"],\"c48_50_bits\":[";first=true;
                for(const auto& row:constants.c48_50)for(const auto value:row) {
                    if(!first)meta<<',';first=false;meta<<std::bit_cast<uint32_t>(value);
                }
                meta<<"],\"samplers\":[";first=true;
                for(const auto& sampler:samplers) {
                    if(!first)meta<<',';first=false;
                    meta<<"{\"filter\":"<<uint32_t(sampler.Filter)
                        <<",\"address_u\":"<<uint32_t(sampler.AddressU)
                        <<",\"address_v\":"<<uint32_t(sampler.AddressV)
                        <<",\"address_w\":"<<uint32_t(sampler.AddressW)<<'}';
                }
                meta<<"]}\n";
                if(!meta)throw Failure("EdgeAA input diagnostic metadata write failed");
                std::filesystem::remove(request);
                std::fprintf(stderr,"[NATIVE EDGEAA INPUT EVIDENCE] captured color/base/line/palette immediately before commit\n");
            }
        }
        auto committed=s.backend.commitEdgeAA(inputs,constants,samplers);
        s.edgeCommit=std::move(committed);s.edgeAAInputs=std::move(inputs);s.edgeAAIds=ids;
        record.privateModified.fill(0);std::memset(poolDirty,0,128);
        {static thread_local uint32_t edgeAACommitSample{};
        if(sampleHotLog(edgeAACommitSample))
            std::fprintf(stderr,"[NATIVE EDGEAA COMMIT] id=%08X color=%08X depth=%08X palette=%08X base=%08X line=%08X width=%.0f height=%.0f samples=%.0f blur=%.9g fade=%.9g threshold=%.9g; five actual textures and original constants bound\n",
                v.identity,ids[0],ids[1],ids[2],ids[3],ids[4],constants.c20_27[3][0],constants.c20_27[4][0],constants.c48_50[0][0],constants.c48_50[1][0],constants.c20_27[0][0],constants.c20_27[2][0]);}
        return;
    }
    need(values[240/4]==PPC_LOAD_U32(aa?0x82D6C7F0u:0x82D09894u),"Native edge/AA source publication differs");
    Graphics::EdgeConstants constants{};
    for(uint32_t i=0;i<8;++i)for(uint32_t lane=0;lane<4;++lane) {
        const auto at=(aa?416u:528u)/4+4*i+lane;
        need(values[at]==v.defaultVectorWords[at],"Native edge kernel differs from its authored immutable default");
        constants.kernel[i][lane]=std::bit_cast<float>(values[at]);
    }
    constants.dimensions={std::bit_cast<float>(values[(aa?384u:432u)/4]),std::bit_cast<float>(values[(aa?400u:448u)/4]),std::bit_cast<float>(values[368/4]),0};
    auto& copies=s.runtime.engineDriver->sceneCopies();
    const auto source=aa?copies.aaSource(values[240/4],s.edgeCamera):copies.edgeSource(values[240/4],s.edgeCamera);
    auto* poolDirty=s.runtime.pointer(v.pool,128,true);const auto sampler=s.edgeState();
    auto committed=s.backend.commitEdge(source,constants,sampler,aa);
    s.edgeCommit=std::move(committed);s.edgeSource=source;s.edgeSourceId=values[240/4];
    // Original82C1ED00/0C clear E's and E+100's entire aligned cache lines.
    // E is native-owned here; E+100 is the genuine shared CPU pool P.
    record.privateModified.fill(0);std::memset(poolDirty,0,128);
    {static thread_local uint32_t edgeCommitSample{};
    if(sampleHotLog(edgeCommitSample))
        std::fprintf(stderr,"[NATIVE %s COMMIT] id=%08X source=%08X width=%.0f height=%.0f kernel_width=%.9g; real constants/sampler/SRV bound, original dirty masks cleared\n",
            aa?"AA":"EDGE",v.identity,s.edgeSourceId,constants.dimensions[0],constants.dimensions[1],constants.dimensions[2]);}
}
void EngineEffects::edgeRectangle(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.require(base);const auto source=s.activeRecord().view.source;const bool aa=source==0x8202FA78;
    need(currentContext==&c && c.r1.u32>=0x200 && !(c.r1.u32&15) &&
         PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0x80 && c.r29.u32==s.edgeCamera &&
         c.r3.u32==PPC_LOAD_U32(0x82D5DA74) && c.r30.u32==c.r3.u32,"Unqualified original edge rectangle frame");
    s.backend.requireEdgeCommit(s.edgeCommit);s.edgeState();
    auto& copies=s.runtime.engineDriver->sceneCopies();
    if(source==0x82034008) {
        auto& driver=*s.runtime.engineDriver;const auto& ids=s.edgeAAIds;const auto& inputs=s.edgeAAInputs;
        for(uint32_t i=1;i<5;++i)s.edgeState(i);
        need(driver.sampledColorCopy(ids[0],s.edgeCamera)==inputs.color && driver.sampledDepthCopy(ids[1],s.edgeCamera)==inputs.depth &&
             driver.itxdTextures().paletteFromHeader(base,ids[2])==inputs.palette &&
             copies.edgeSource(ids[3],s.edgeCamera)==inputs.base && copies.aaSource(ids[4],s.edgeCamera)==inputs.line,
             "Native edgeAA resource lease changed");
    } else need((aa?copies.aaSource(s.edgeSourceId,s.edgeCamera):copies.edgeSource(s.edgeSourceId,s.edgeCamera))==s.edgeSource,
                "Native edge scene source lease changed");
    if(site==0x823CA46C) {
        need(!s.edgeVertices && !s.edgeDeclaration && c.r4.u32==PPC_LOAD_U32(0x82D099A0),"Native edge declaration order/publication differs");
        const auto declaration=s.runtime.engineDriver->quadDeclarations().record(c.r4.u32);
        constexpr std::array<uint32_t,9> words={0,0x002C23A5,0,8,0x002C23A5,0x00050000,0x00FF0000,0xFFFFFFFF,0};
        need(declaration->bytes().size()==36 && declaration->minimumStreamBytes()==16 && declaration->elements().size()==2,
             "Native edge declaration layout differs");
        for(size_t i=0;i<words.size();++i)need(word(declaration->bytes(),4*i)==words[i],"Native edge declaration bytes differ");
        s.backend.bindEdgeDeclaration();s.edgeDeclaration=c.r4.u32;s.edgeDrawSp=c.r1.u32;c.lr=site+4;return;
    }
    need(s.edgeDeclaration && s.edgeDeclaration==PPC_LOAD_U32(0x82D099A0) && s.edgeDrawSp==c.r1.u32,
         "Native edge rectangle lost its bound declaration/frame");
    if(site==0x823CA480) {
        need(!s.edgeVertices && c.r4.u32==8 && c.r5.u32==3 && c.r6.u32==16,"Native edge rectangle allocation differs");
        if(s.idleEdgeVertices) {
            need(s.idleEdgeVertices->words()==12,"Native edge rectangle idle extent differs");
            s.edgeVertices=std::move(s.idleEdgeVertices);
            std::memset(s.runtime.pointer(s.edgeVertices->address,48,true),0,48);
        } else {
            s.edgeVertices=std::make_unique<State::Record::ParameterStorage>(s.runtime,std::array<uint32_t,12>{});
        }
        c.r3.u64=s.edgeVertices->address;c.lr=site+4;return;
    }
    need(site==0x823CA4F0 && s.edgeVertices && c.r31.u32==s.edgeVertices->address,"Native edge rectangle storage/submission differs");
    std::array<Graphics::ScreenVertex,3> vertices{};
    for(uint32_t i=0;i<3;++i){const auto p=s.edgeVertices->address+16*i;
        vertices[i]={std::bit_cast<float>(PPC_LOAD_U32(p)),std::bit_cast<float>(PPC_LOAD_U32(p+4)),
                     std::bit_cast<float>(PPC_LOAD_U32(p+8)),std::bit_cast<float>(PPC_LOAD_U32(p+12))};}
    const auto camera=s.runtime.engineDriver->cameraBinding();bool alphaOne=false;
    const auto color=s.runtime.engineDriver->color(camera.colorIdentity,alphaOne);
    const auto depth=s.runtime.engineDriver->depth(camera.depthIdentity);
    need(!s.idleEdgeVertices,"Native edge rectangle idle owner is still retained");
    s.backend.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);
    // Camera selection keeps its original reverse viewport as logical state.
    // These shaders do not export depth; edgeAA samples a separate depth copy.
    // edgeState proved depth/write and stencil off. Normalize the native viewport, as
    // other native screen consumers do, without changing the logical endpoints.
    s.backend.setViewport({0,0,float(camera.viewport[2]),float(camera.viewport[3]),0,1});
    s.backend.drawEdge(color,depth,s.edgeCommit,vertices,*s.edgeVertex,*s.edgePixel);
    if(source==0x82034008 && !s.runtime.frameCaptureDirectory.empty()) {
        const auto request=s.runtime.frameCaptureDirectory/"edgeaa-output.request";
        if(std::filesystem::exists(request)) {
            const auto pixels=s.backend.readbackTarget(color);
            std::ofstream out(s.runtime.frameCaptureDirectory/"edgeaa-output.rgb10a2",std::ios::binary);
            out.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()));
            if(!out)throw Failure("EdgeAA output diagnostic write failed");
            std::filesystem::remove(request);
            std::ofstream arm(s.runtime.frameCaptureDirectory/"after-edgeaa.request");
            arm<<"capture next rigid replay\n";
            if(!arm)throw Failure("EdgeAA post-replay diagnostic arm failed");
            std::fprintf(stderr,"[NATIVE EDGEAA OUTPUT EVIDENCE] captured output and armed next rigid replay\n");
        }
    }
    s.idleEdgeVertices=std::move(s.edgeVertices);
    s.edgeDeclaration=0;s.edgeDrawSp=0;c.lr=site+4;
    const char* drawName=postProfile(source).name;
    const unsigned long long drawCount=static_cast<unsigned long long>(source==0x82034008?s.backend.edgeAADrawCount():(aa?s.backend.aaDrawCount():s.backend.edgeDrawCount()));
    {static thread_local uint32_t edgeDrawSample{};
    if(sampleHotLog(edgeDrawSample))
        std::fprintf(stderr,"[NATIVE %s DRAW] id=%08X camera=%08X; original three vertices submitted as native rectangle, count=%llu\n",
            drawName,s.activeEdgeId,s.edgeCamera,drawCount);}
}
uint64_t EngineEffects::edgeDrawCount() const {state->require(state->runtime.base);return state->backend.edgeDrawCount();}
uint64_t EngineEffects::aaDrawCount() const {state->require(state->runtime.base);return state->backend.aaDrawCount();}
uint64_t EngineEffects::edgeAADrawCount() const {state->require(state->runtime.base);return state->backend.edgeAADrawCount();}
uint32_t EngineEffects::activeEdge() const {
    auto& s=*state;s.require(s.runtime.base);return s.activeEdgeId;
}
EngineEffects::View EngineEffects::view(uint32_t id) const {
    state->require(state->runtime.base);const auto& record=state->find(id);auto result=record.view;
    if(record.parameters) {
        const uint32_t owned=record.parameters->words();
        need(result.defaultVectorWords.size()==owned,"Native FX private parameter extent differs");
        need(uint64_t(owned)*4<=UINT32_MAX,"Native FX private parameter extent overflow");
        if(owned) {
            const uint32_t bytes=owned*4;
            uint8_t* const span=state->runtime.pointer(record.parameters->address,bytes,false);
            for(uint32_t i=0;i<owned;++i)
                result.defaultVectorWords[i]=__builtin_bswap32(*(volatile uint32_t*)(span+4*i));
        }
    }
    return result;
}
std::vector<uint32_t> EngineEffects::parameterValues(uint32_t id) const {
    state->require(state->runtime.base);const auto& record=state->find(id);auto words=record.view.defaultVectorWords;
    if(record.parameters) {
        const uint32_t owned=record.parameters->words();
        need(words.size()==owned,"Native FX private parameter extent differs");
        need(uint64_t(owned)*4<=UINT32_MAX,"Native FX private parameter extent overflow");
        if(owned) {
            const uint32_t bytes=owned*4;
            uint8_t* const span=state->runtime.pointer(record.parameters->address,bytes,false);
            for(uint32_t i=0;i<owned;++i)
                words[i]=__builtin_bswap32(*(volatile uint32_t*)(span+4*i));
        }
    }
    return words;
}
EngineEffects::ViewHeader EngineEffects::viewHeader(uint32_t id) const {
    state->require(state->runtime.base);const auto& record=state->find(id);const auto& v=record.view;
    if(record.parameters) {
        const uint32_t owned=record.parameters->words();
        need(v.defaultVectorWords.size()==owned,"Native FX private parameter extent differs");
        need(uint64_t(owned)*4<=UINT32_MAX,"Native FX private parameter extent overflow");
        if(owned)state->runtime.pointer(record.parameters->address,owned*4,false);
    }
    return {v.identity,v.wrapper,v.manager,v.source,v.cache,v.pool,v.cacheBytes,v.phase};
}
std::array<uint32_t,4> EngineEffects::auditIdentity(uint32_t id) const {
    state->require(state->runtime.base);
    const auto& v=state->find(id).view;
    return {v.source,v.cache,v.pool,uint32_t(v.phase)};
}
void EngineEffects::completeDistortionScreenReplacement(uint8_t* base,uint64_t before,
    uint32_t declaration,uint32_t vertexCache,uint32_t pixelCache) {
    auto& s=*state;s.require(base);s.backend.requireCompletedDistortion(before);
    need(declaration==PPC_LOAD_U32(0x82DFEB34)&&vertexCache==PPC_LOAD_U32(0x82CF2340)&&
         pixelCache==PPC_LOAD_U32(0x82CF25A8),"Original distortion final cache identity differs");
    if(!s.screenReplacement.effect)return;
    auto& replacement=s.screenReplacement;
    if(replacement.bindingReset)s.backend.requireBindingReset(replacement.reset);
    else if(replacement.inputOnly)s.backend.requireScreenInputReplacement(replacement.inputs);
    else replacement.receipt=s.backend.completedDistortionScreenReplacement(before,replacement.receipt);
    // The complete original phase leaves its final composite in the guest
    // shader cache. Its native draws restore the prior physical bindings.
    replacement.declaration=declaration;
    replacement.vertexCache=vertexCache;replacement.pixelCache=pixelCache;
    s.requireScreenReplacement(base);
}
void EngineEffects::edgeParameter(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.require(base);
    // lastFunction is a trace of the most recently entered callee, not a call
    // stack. The original boolean/video helpers legitimately change it here.
    need(c.r1.u32>=0x200 && !(c.r1.u32&15) && c.r30.u32==0x82061428 &&
         PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0xB0,"Unqualified original edge parameter frame");
    s.runtime.pointer(c.r31.u32,0xA8,false);const auto wrapper=PPC_LOAD_U32(c.r31.u32+0x18);
    auto& record=s.edgeOwner(c,base,wrapper);
    need(record.view.source==0x8202DF98,"Edge parameter continuation belongs to another effect");
    auto slot=[&](uint32_t global,uint32_t expected){
        need(PPC_LOAD_U32(global)==expected,"Original edge parameter publication changed");
        return s.edgeSlot(record,expected);
    };
    switch(site) {
    case 0x823C9100:
        need(c.r9.u32==PPC_LOAD_U32(0x82D0992C),"Edge line-width handle differs");
        c.r11.u64=slot(0x82D0992C,0x00340016);c.r8.u64=0;c.r3.u64=wrapper;break;
    case 0x823C91A8:
        need(c.r9.u32==PPC_LOAD_U32(0x82D09934),"Edge depth-fade handle differs");
        c.r11.u64=slot(0x82D09934,0x003C001A);c.r5.u64=0;c.r3.u64=wrapper;break;
    case 0x823C9264:
        need(c.r9.u32==PPC_LOAD_U32(0x82D09950),"Edge width handle differs");
        c.r10.u64=slot(0x82D09950,0x0044001E);c.r6.u64=0;c.r11.u64=record.view.identity;break;
    // Skip only SDK pointer/descriptor work. The actual original extsw/std,
    // lfd/fcfid/frsp remain in the original function, including rounding mode.
    case 0x823C92F4:case 0x823C92FC:case 0x823C9318:break;
    case 0x823C9364:
        c.r11.u64=slot(0x82D09954,0x00480020);c.r5.u64=0;break;
    case 0x823C93A4:
        need(c.r10.u32==PPC_LOAD_U32(0x82D0994C),"Edge palette handle differs");
        c.r11.u64=slot(0x82D0994C,0x004C0022);c.r9.u64=0;break;
    case 0x823C9448:
        c.r11.u64=slot(0x82D09944,0x002C0012);c.r9.u64=0;break;
    case 0x823C94C0:
        need(c.r10.u32==PPC_LOAD_U32(0x82D09948),"Edge depth texture handle differs");
        c.r10.u64=slot(0x82D09948,0x00300014);c.r7.u64=0;break;
    default:throw Failure("Unqualified edge parameter continuation");
    }
    // Original stfsx/stwx instructions own the actual value writes. No shader
    // constant upload, resource binding or effect activation is implied here.
}
void EngineEffects::aaParameter(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.require(base);
    need(currentContext==&c && c.r1.u32>=0x200 && !(c.r1.u32&15) && c.r30.u32==0x82061428 &&
         PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0xA0,"Unqualified original AA parameter frame");
    s.runtime.pointer(c.r31.u32,0xA8,false);const auto wrapper=PPC_LOAD_U32(c.r31.u32+0x18);
    auto& record=s.edgeOwner(c,base,wrapper);need(record.view.source==0x8202FA78,"AA parameter belongs to another effect");
    auto slot=[&](uint32_t global,uint32_t handle){
        need(PPC_LOAD_U32(global)==handle,"Original AA parameter publication changed");return s.edgeSlot(record,handle);
    };
    switch(site) {
    case 0x823C8380:
        need(c.r9.u32==PPC_LOAD_U32(0x82D09904),"AA kernel-width handle differs");
        c.r10.u64=slot(0x82D09904,0x00340016);c.r8.u64=0;c.r11.u64=record.view.identity;break;
    case 0x823C8404:break; // Skip only SDK descriptor loads; original global handle load follows.
    case 0x823C8414:
        need(c.r9.u32==PPC_LOAD_U32(0x82D09900),"AA color handle differs");c.r9.u64=0x82D70000;break;
    case 0x823C8454:
        c.r10.u64=slot(0x82D09900,0x002C0012);c.r6.u64=0;break;
    case 0x823C84C4:
        c.r10.u64=slot(0x82D09908,0x00380018);c.r6.u64=0;c.r11.u64=record.view.identity;break;
    case 0x823C8548:case 0x823C8550:case 0x823C8560:break;
    case 0x823C8568:
        need(c.r9.u32==PPC_LOAD_U32(0x82D0990C),"AA height handle differs");
        c.r11.u64=slot(0x82D0990C,0x003C001A);c.r5.u64=0;c.r3.u64=wrapper;break;
    default:throw Failure("Unqualified AA parameter continuation");
    }
}
void EngineEffects::edgeAAParameter(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.require(base);
    need(currentContext==&c && c.r1.u32>=0x200 && !(c.r1.u32&15) && c.r30.u32==0x82061428 &&
         PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0xC0 && c.r31.u32==s.edgeTyped+0x18,
         "Unqualified original edgeAA parameter frame");
    const auto wrapper=PPC_LOAD_U32(c.r31.u32);
    auto& record=s.edgeOwner(c,base,wrapper,c.r31.u32-0x18);
    need(record.view.source==0x82034008 && record.view.identity==s.activeEdgeId,"EdgeAA setter lost its active owner");
    auto slot=[&](uint32_t global,uint32_t handle,uint32_t actual){
        if(PPC_LOAD_U32(global)!=handle || actual!=handle)
            std::fprintf(stderr,"[EDGEAA PARAMETER REJECTED] site=%08X global=%08X published=%08X actual=%08X expected=%08X\n",site,global,PPC_LOAD_U32(global),actual,handle);
        need(PPC_LOAD_U32(global)==handle && actual==handle,"Original edgeAA parameter publication/handle changed");
        return s.edgeSlot(record,handle);
    };
    switch(site) {
    case 0x823C9A9C:c.r10.u64=slot(0x82D09960,0x00340016,c.r10.u32);c.r11.u64=0;break;
    case 0x823C9B30:c.r9.u64=slot(0x82D0995C,0x002C0012,c.r9.u32);c.r7.u64=0;break;
    case 0x823C9BB0:c.r7.u64=slot(0x82D09964,0x00680030,c.r10.u32);c.r11.u64=0;break;
    case 0x823C9C40:c.r9.u64=slot(0x82D0998C,0x006C0032,c.r9.u32);c.r10.u64=0;break;
    case 0x823C9CCC:c.r8.u64=slot(0x82D09968,0x00300014,c.r9.u32);c.r11.u64=0;break;
    case 0x823C9D5C:c.r9.u64=slot(0x82D09970,0x003C001A,c.r9.u32);c.r7.u64=0;break;
    case 0x823C9E00:c.r8.u64=slot(0x82D09974,0x00380018,c.r9.u32);c.r10.u64=0;break;
    case 0x823C9EC4:c.r11.u64=slot(0x82D09978,0x0040001C,c.r9.u32);break;
    case 0x823C9F24:c.r9.u64=slot(0x82D0997C,0x0044001E,c.r9.u32);c.r6.u64=0;break;
    case 0x823C9FB4:c.r8.u64=slot(0x82D09988,0x004C0022,c.r9.u32);c.r10.u64=0;break;
    case 0x823CA044:c.r11.u64=slot(0x82D09990,0x00580028,c.r9.u32);c.r7.u64=0;break;
    case 0x823C9B28:case 0x823C9DF0:case 0x823C9E80:case 0x823C9E88:case 0x823C9EB4:
    case 0x823C9F1C:case 0x823C9F9C:case 0x823CA03C:case 0x823CA0B4:break;
    default:throw Failure("Unqualified edgeAA parameter continuation");
    }
    // Only SDK descriptor/address operations are replaced. Original lfs,
    // integer/vector conversions and stfsx/stwx/stvewx still write the values.
}
void EngineEffects::edgeAAHelper(PPCContext& c,uint8_t* base,bool palette) {
    auto& s=*state;s.require(base);
    need(currentContext==&c && c.r1.u32>=0x200 && !(c.r1.u32&15) && c.r30.u32==0x82061428 &&
         PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0xC0 && c.r31.u32==s.edgeTyped+0x18 &&
         uint32_t(c.lr)==(palette?0x823C8208u:0x823C8178u),"Unqualified original edgeAA helper frame");
    const uint32_t caller=PPC_LOAD_U32(c.r1.u32-8);
    auto& record=s.edgeOwner(c,base,c.r3.u32,c.r31.u32-0x18);
    need(record.view.source==0x82034008 && record.view.identity==s.activeEdgeId,"EdgeAA helper lost its active owner");
    uint32_t global=0,handle=0;
    if(palette){need(caller==0x823CA178,"Unqualified edgeAA palette caller");global=0x82D0996C;handle=0x0064002E;}
    else switch(caller) {
    case 0x823CA0CC:global=0x82D09994;handle=0x005C002A;break;
    case 0x823CA0E0:global=0x82D09998;handle=0x0060002C;break;
    case 0x823CA0F4:global=0x82D0999C;handle=0x00480020;break;
    case 0x823CA12C:global=0x82D09980;handle=0x00500024;break;
    case 0x823CA154:global=0x82D09984;handle=0x00540026;break;
    default:throw Failure("Unqualified edgeAA scalar helper caller");
    }
    need(PPC_LOAD_U32(global)==handle && c.r4.u32==handle,"Original edgeAA helper parameter changed");
    const auto address=s.edgeSlot(record,handle);
    if(palette){c.r10.u64=address;c.r9.u64=0;}else{c.r11.u64=address;c.r6.u64=0;}
    // Both original save/restore helpers and the final value store remain AOT.
}
void EngineEffects::edgeBoolean(PPCContext& c,uint8_t* base) {
    if(uint32_t(c.lr)==0x82740B78||uint32_t(c.lr)==0x827003B4||uint32_t(c.lr)==0x82700454){monoOperation(c,base,0x823C8EB0);return;}
    auto& s=*state;s.require(base);
    if(c.lastFunction==0x823C8EB0&&uint32_t(c.lr)==0x82740818) {
        // Diagnostic probe only; actual Boolean store and all guards below remain
        // unconditional. Sample routine successful logging, keep failures.
        s.runtime.pointer(c.r3.u32,0x30,false);
        const auto probeId=PPC_LOAD_U32(c.r3.u32+0x10);
        auto& probeRecord=s.find(probeId);const auto& probeView=probeRecord.view;
        const auto probeParams=probeRecord.metadata->parameters(false);
        static thread_local uint32_t skinBooleanSample{};
        if(sampleHotLog(skinBooleanSample)) {
            std::fprintf(stderr,"[NATIVE SKIN BOOLEAN] r3=%08X r4=%08X r5=%08X\n",c.r3.u32,c.r4.u32,c.r5.u32);
            std::fprintf(stderr,"[NATIVE SKIN BOOLEAN] id=%08X source=%08X\n",probeId,probeView.source);
            for(const auto& p:probeParams)
                if(p.handle==c.r4.u32)
                    std::fprintf(stderr,"[NATIVE SKIN BOOLEAN] handle=%08X name=%s desc=%08X %08X\n",
                        p.handle,p.name.c_str(),p.descriptorWords[0],p.descriptorWords[1]);
        }
    }
    if(c.lastFunction==0x823C8EB0&&uint32_t(c.lr)==0x82740818) {
        auto& record=s.activeZPrepass(c,base);
        need(c.r3.u32==record.view.wrapper&&c.r4.u32==0x00300014&&
             PPC_LOAD_U32(s.zprepassTyped+0xB8)==c.r4.u32&&c.r30.u32==s.zprepassTyped&&!c.r5.u32,
             "Original static zprepass Boolean arguments differ");
        const auto parameters=record.metadata->parameters(false);
        const auto p=std::find_if(parameters.begin(),parameters.end(),[](const auto& p){return p.handle==0x00300014;});
        need(p!=parameters.end()&&p->name=="kIsSkinned"&&p->descriptorWords==std::array<uint32_t,2>{8,0x00010010},
             "Original zprepass Boolean descriptor differs");
        // Original823C8EB0 stores float0/1 even for this Boolean descriptor;
        // commit category4 subsequently normalizes it to a Boolean register.
        PPC_STORE_U32(s.edgeSlot(record,c.r4.u32),uint8_t(c.r5.u32)?0x3F800000:0);return;
    }
    if(c.lastFunction==0x823C8EB0&&(uint32_t(c.lr)==0x8270716C||uint32_t(c.lr)==0x827071CC||uint32_t(c.lr)==0x8270615C)) {
        const bool alpha=uint32_t(c.lr)==0x8270615C;const uint32_t handle=alpha?0x00340016:0x00300014;
        auto& record=s.activeShadowRecord(c,base,0,alpha);
        need(c.r3.u32==record.view.wrapper&&c.r4.u32==handle&&PPC_LOAD_U32(s.shadowTyped+(alpha?0x678:0x674))==c.r4.u32,
             "Original character skinning flag parameter differs");
        const auto entries=record.metadata->parameters(false);
        const auto p=std::find_if(entries.begin(),entries.end(),[&](const auto& p){return p.handle==handle;});
        need(p!=entries.end()&&p->name==(alpha?"kIsAlphaTested":"kIsSkinned")&&
             p->descriptorWords==std::array<uint32_t,2>{0x00200008,alpha?0x00010011u:0x00010010u},
             "Original character skinning flag descriptor differs");
        const auto address=s.edgeSlot(record,c.r4.u32);
        PPC_STORE_U32(address,uint8_t(c.r5.u32)?0x3F800000:0);
        return;
    }
    need(c.lastFunction==0x823C8EB0 && (uint32_t(c.lr)==0x823C9190 || uint32_t(c.lr)==0x823C9238),
         "Unqualified original edge boolean setter caller");
    auto& record=s.edgeOwner(c,base,c.r3.u32);
    need(record.view.source==0x8202DF98,"Edge boolean continuation belongs to another effect");
    const uint32_t global=uint32_t(c.lr)==0x823C9190?0x82D09930:0x82D09938;
    const uint32_t handle=uint32_t(c.lr)==0x823C9190?0x00380018:0x0040001C;
    need(c.r4.u32==handle && PPC_LOAD_U32(global)==handle,"Original edge boolean parameter differs");
    const auto address=s.edgeSlot(record,handle);
    // Original clrlwi24/cntlzw/xori, then vcfux and one stvewx: float 0 or 1
    // from the low byte. Other three lanes of the parameter remain unchanged.
    PPC_STORE_U32(address,uint8_t(c.r5.u32)?0x3F800000:0);
}
static uint32_t skinBoneGroupCount(Runtime& runtime,uint32_t bones,const std::array<uint32_t,9>& row) {
    return boneGroupCount(runtime,bones,row[7],row[8]);
}
void EngineEffects::setShadowBones(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.require(base);
    if(c.lastFunction==0x826FDBE0&&(uint32_t(c.lr)==0x827016D8||uint32_t(c.lr)==0x82701820)) {
        need(currentContext==&c&&s.skinId,"Skin bone-array setter has no active skin");
        auto& record=s.find(s.skinId);const auto& v=record.view;const auto profile=skinProfile(v.source);
        auto& mesh=s.skinMesh;auto& run=s.skinImmediate;
        requireSkinSelection(s.skinTyped);s.runtime.engineDriver->requireContext(mesh.context);
        need(run.active&&run.activated&&run.staging.complete&&run.cpu==&c&&!record.recordingContext&&
             mesh.active&&mesh.immediate&&mesh.cpu==&c&&mesh.phase==3&&c.r1.u32==mesh.frame&&
             PPC_LOAD_U32(mesh.frame)==mesh.entrySP&&mesh.packet==run.packet,
             "Original skin bone setter has no active mesh frame");
        s.runtime.pointer(mesh.metadata,0x38,false);
        need(c.r21.u32==s.skinTyped&&c.r22.u32==mesh.object&&c.r23.u32==mesh.metadata&&c.r15.u32==0x82D64080&&
             PPC_LOAD_U32(mesh.metadata+0xC)==mesh.geometry&&PPC_LOAD_U32(mesh.packet)==mesh.metadata&&
             PPC_LOAD_U32(mesh.packet+4)==mesh.object&&PPC_LOAD_U32(mesh.packet+0x18)==s.skinTyped,
             "Original skin bone owner/metadata changed");
        const uint32_t bones=PPC_LOAD_U32(mesh.metadata+0x24);
        need(bones>=1&&bones<=255,"Original skin composed bone extent differs");
        need(c.r3.u32==v.identity&&c.r4.u32==profile.boneArray&&PPC_LOAD_U32(s.skinTyped+0xA8)==c.r4.u32&&
             c.r6.u32>=1&&c.r6.u32<=64,
             "Original skin bone-array arguments differ");
        if(uint32_t(c.lr)==0x827016D8) {
            // 8274010C retains the dispatcher's Boolean r6 in r28. The skin
            // prologue does not define r28 before this call: it is unrelated
            // to the bone upload. r31/r30 are the clamped loop start/end.
            if(!(bones<=64&&c.r5.u32==0x82D64080&&c.r6.u32==bones&&!mesh.cursor&&!mesh.submeshUpdated&&
                 !s.skinMaterial.active&&!s.skinMaterial.committed&&!c.r31.u32&&c.r30.u32==PPC_LOAD_U32(mesh.metadata+0x10)))
                std::fprintf(stderr,"[NATIVE SKIN FIRST PALETTE] bones=%u source=%08X count=%u cursor=%u updated=%u material=%u/%u start=%08X end=%08X expectedEnd=%08X callerFlag=%08X\n",
                    bones,c.r5.u32,c.r6.u32,mesh.cursor,mesh.submeshUpdated,s.skinMaterial.active,s.skinMaterial.committed,
                    c.r31.u32,c.r30.u32,PPC_LOAD_U32(mesh.metadata+0x10),c.r28.u32);
            need(bones<=64&&c.r5.u32==0x82D64080&&c.r6.u32==bones&&!mesh.cursor&&!mesh.submeshUpdated&&
                 !s.skinMaterial.active&&!s.skinMaterial.committed&&!c.r31.u32&&
                 c.r30.u32==PPC_LOAD_U32(mesh.metadata+0x10),
                 "Original skin first bone palette or loop extent differs");
        } else {
            need(bones>64&&mesh.cursor<mesh.draws.size()&&mesh.submeshUpdated&&
                 !s.skinMaterial.active&&!s.skinMaterial.committed,"Original skin group palette has no current submesh");
            const auto& row=mesh.draws[mesh.cursor];
            validateBoneGroupSnapshot(s.runtime,row.entry,row.words,row.boneRanges,row.boneOwner);
            const auto count=skinBoneGroupCount(s.runtime,bones,row.words),table=row.words[8];
            const uint32_t expectedSource=row.words[7]==1?0x82D64080+64*PPC_LOAD_U8(table):0x82D63070;
            need(c.r30.u32==row.entry&&c.r28.u32==row.words[1]&&c.r29.u32==row.material&&
                 c.r5.u32==expectedSource&&c.r6.u32==count&&PPC_LOAD_U32(mesh.frame+0x50)==count,
                 "Original skin selected matrix group source/count differs");
            // 826FE748..758 returns a direct contiguous alias for one group;
            // 826FE780..7B4 copies multiple byte(start,count) ranges into the
            // 64-matrix scratch at82D63070. Validate the actual original copy
            // against its live composed source before consuming it.
            const auto* selected=s.runtime.pointer(expectedSource,64*count,false);uint32_t out=0;
            for(uint32_t i=0;i<row.words[7];++i) {
                const uint32_t first=PPC_LOAD_U8(table+2*i),length=PPC_LOAD_U8(table+2*i+1);
                if(length) {
                    const auto* composed=s.runtime.pointer(0x82D64080+64*first,64*length,false);
                    need(!std::memcmp(selected+64*out,composed,64*length),"Original skin matrix group copy differs from authored ranges");
                }
                out+=length;
            }
        }
        const auto entries=record.metadata->parameters(false);
        const auto p=std::find_if(entries.begin(),entries.end(),[&](const auto& p){return p.handle==profile.boneArray;});
        need(p!=entries.end()&&p->name=="g_BlendMatrices"&&p->descriptorWords==std::array<uint32_t,2>{0x00200102,0x03000041},
             "Original skin bone-array descriptor differs");
        const uint32_t count=c.r6.u32;
        const auto body=record.metadata->body();const auto selected=s.alphaSkin()?0x0007FFFCu:0x0003FFFCu;
        const auto passes=record.metadata->techniques();
        const auto passIt=std::find_if(passes.begin(),passes.end(),[&](const auto& candidate){return candidate.handle==selected;});
        need(passIt!=passes.end(),"Selected skin bone pass is missing");const auto& pass=*passIt;
        const auto expectedContext=s.alphaSkin()?skinAlphaPass(v.source).context:profile.context;
        need(pass.name==(s.alphaSkin()?"skinalpha":"skin")&&pass.contextOffset==expectedContext&&word(body,0x120)==2&&word(body,0x124)==1&&
             word(body,0x130)==profile.leaves&&word(body,0x134)==11,"Original skin bone constant context differs");
        const auto rows=word(body,pass.contextOffset+0x40);
        need(word(body,rows+16*profile.boneLeaf)==profile.boneArray+0x40000&&word(body,rows+16*profile.boneLeaf+4)==0x834&&
             !word(body,rows+16*profile.boneLeaf+8)&&!word(body,rows+16*profile.boneLeaf+12),"Original skin first bone row differs");
        for(uint32_t i=0;i<4;++i)need(PPC_LOAD_U32(0x82000EC0+4*i)==UINT32_MAX,"Original bone matrix transpose mask changed");
        const auto* source=s.runtime.pointer(c.r5.u32,64*count,false);
        if(count==43 && s.skinImmediate.active && !s.runtime.frameCaptureDirectory.empty()) {
            // This reached 43-bone character is the one moved by the verified
            // Land of Chocolate jump/forward controls. Its bone0 source is a
            // LOCAL pose. The attached RpAtomic child frame's LTM is the
            // original world transform (sub_823F2540); sample that instead.
            const uint32_t packet=s.skinImmediate.packet;
            const uint32_t object=packet?PPC_LOAD_U32(packet+4):0;
            std::array<float,16> world{};bool valid=false;
            try {
                if(!object||PPC_LOAD_U32(object)!=0x01000501)throw Failure("Telemetry atomic type differs");
                const uint32_t frame=PPC_LOAD_U32(object+4);
                s.runtime.pointer(frame,0xA4,false);
                const uint32_t parent=PPC_LOAD_U32(frame+0xA0);
                s.runtime.pointer(parent,0xA4,false);
                if(frame==parent||PPC_LOAD_U32(parent+0xA0)!=parent)
                    throw Failure("Telemetry atomic frame is not a child of a root frame");
                for(uint32_t j=0;j<16;++j)world[j]=std::bit_cast<float>(PPC_LOAD_U32(frame+0x50+4*j));
                valid=true;
            }catch(const std::exception&) {} // Optional observer cannot stop original gameplay.
            s.runtime.playerTelemetry.observeCharacterFrame(object,count,world,valid);
        }
        {static thread_local uint32_t skinBonesIdSample{};
        if(sampleHotLog(skinBonesIdSample))
            std::fprintf(stderr,"[NATIVE SKIN BONES] id=%08X count=%u storageWords=%zu\n",
                v.identity,count,v.defaultVectorWords.size());}
        need(v.defaultVectorWords.size()==profile.words,"Skin private matrix storage extent differs");
        if(!record.parameters)record.parameters=std::make_unique<State::Record::ParameterStorage>(s.runtime,v.defaultVectorWords);
        auto* destination=s.runtime.pointer(record.parameters->address+4*profile.boneWord,64*count,true);
        for(uint32_t bone=0;bone<count;++bone)for(uint32_t row=0;row<4;++row)for(uint32_t col=0;col<4;++col)
            std::memcpy(destination+64*bone+16*row+4*col,source+64*bone+16*col+4*row,4);
        for(uint32_t i=0;i<count;++i){const auto leaf=profile.boneLeaf+i;record.privateModified[leaf/8]|=uint8_t(0x80>>(leaf&7));}
        return;
    }
    const bool grouped=uint32_t(c.lr)==0x82706438;
    need(c.lastFunction==0x826FDBE0&&(uint32_t(c.lr)==0x827071A0||grouped),
         "Unqualified original character bone-array setter caller");
    auto& record=s.activeShadowRecord(c,base,grouped?c.r30.u32:0);const auto& v=record.view;
    need(c.r3.u32==v.identity&&c.r4.u32==0x0040001C&&PPC_LOAD_U32(s.shadowTyped+0x670)==c.r4.u32&&c.r6.u32<=64,
         "Original character bone-array arguments differ");
    s.runtime.pointer(c.r29.u32,0x2C,false);
    if(grouped) {
        auto& mesh=s.characterMesh;const auto bones=PPC_LOAD_U32(mesh.metadata+0x24);
        const auto rows=PPC_LOAD_U32(mesh.metadata+0x14),submeshes=PPC_LOAD_U32(mesh.metadata+0x10);
        need(!mesh.staticGeometry&&mesh.phase==3&&mesh.metadata==c.r29.u32&&mesh.object==c.r26.u32&&
             mesh.frame==c.r1.u32&&PPC_LOAD_U32(mesh.frame)==mesh.frame+0xC0&&
             PPC_LOAD_U32(mesh.metadata+0xC)==mesh.geometry&&c.r23.u32==0x82D64080&&
             c.r28.u32%36==0&&c.r28.u32/36<submeshes&&c.r31.u32==rows+c.r28.u32,
             "Original character group palette lost its mesh frame/current submesh");
        const auto groups=PPC_LOAD_U32(c.r31.u32+28),table=PPC_LOAD_U32(c.r31.u32+32);
        validateBoneGroupSnapshot(s.runtime,mesh.boneGroups,c.r31.u32);
        const auto count=boneGroupCount(s.runtime,bones,groups,table);
        need(c.r6.u32==count&&PPC_LOAD_U32(mesh.frame+0x50)==count&&
             c.r5.u32==boneGroupSource(s.runtime,bones,groups,table,count),
             "Original character selected matrix group source/count differs");
        mesh.paletteEntry=c.r31.u32;mesh.paletteCount=count;mesh.paletteCommitted=false;
    } else need(c.r5.u32==0x82D64080&&c.r28.u32==c.r5.u32&&c.r6.u32==c.r30.u32&&
                PPC_LOAD_U32(c.r29.u32+0x24)==c.r6.u32,"Original character whole bone palette differs");
    const auto entries=record.metadata->parameters(false);
    const auto p=std::find_if(entries.begin(),entries.end(),[](const auto& p){return p.handle==0x0040001C;});
    need(p!=entries.end()&&p->name=="kBoneMatrices"&&p->descriptorWords==std::array<uint32_t,2>{0x00200102,0x03000041},
         "Original character bone-array descriptor differs");
    // The original zero-count convention selects the entire 64-element array.
    const uint32_t count=c.r6.u32?c.r6.u32:64;
    const auto body=record.metadata->body();const auto descriptors=word(body,0x108);
    for(uint32_t i=0;i<64;++i)
        need(word(body,descriptors+8*(17+i))==0x000005B0&&word(body,descriptors+8*(17+i)+4)==0x000C0014+4*i,
             "Original character bone matrix layout differs");
    // Original826FDC74 extracts bits4..6 of the CHILD descriptor, not the
    // column count. 000005B0 yields3; (3+1)*4 selects82000EC0 (all ones).
    // Its vsel therefore selects the exact bitwise4x4 transpose.
    // The later constant upload selects three full vectors, dropping row four.
    for(uint32_t i=0;i<4;++i)need(PPC_LOAD_U32(0x82000EC0+4*i)==UINT32_MAX,"Original bone matrix transpose mask changed");
    const auto* source=s.runtime.pointer(c.r5.u32,64*count,false);
    need(v.defaultVectorWords.size()*4==4464,"Character private matrix storage extent differs");
    if(!record.parameters)record.parameters=std::make_unique<State::Record::ParameterStorage>(s.runtime,v.defaultVectorWords);
    auto* destination=s.runtime.pointer(record.parameters->address+320,64*count,true);
    for(uint32_t bone=0;bone<count;++bone)for(uint32_t row=0;row<4;++row)for(uint32_t col=0;col<4;++col)
        std::memcpy(destination+64*bone+16*row+4*col,source+64*bone+16*col+4*row,4);
    for(uint32_t i=0;i<count;++i){const auto leaf=14+i;record.privateModified[leaf/8]|=uint8_t(0x80>>(leaf&7));}
    static thread_local uint32_t logged{};if(logged++<4)std::fprintf(stderr,
        "[NATIVE SHADOW BONES] typed=%08X id=%08X count=%u source=%08X; original CPU matrices copied to owned private values; upload and draw separate\n",
        s.shadowTyped,v.identity,count,c.r5.u32);
}
void EngineEffects::commitShadowDepth(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.require(base);
    const bool staticGeometry=uint32_t(c.lr)==0x827076E4;
    const bool grouped=c.lastFunction==0x82C1DBA0&&uint32_t(c.lr)==0x82706440;
    need(grouped||(c.lastFunction==0x826B3980&&(uint32_t(c.lr)==0x827071A8||staticGeometry)),
         "Unqualified original character shadow constant commit caller");
    auto& record=s.activeShadowRecord(c,base,grouped?c.r30.u32:0);const auto& v=record.view;
    need(c.r3.u32==(grouped?v.identity:v.wrapper)&&record.parameters,"Character shadow commit lacks its owner/private values");
    if(grouped) {
        const auto& mesh=s.characterMesh;
        need(!mesh.staticGeometry&&mesh.phase==3&&mesh.frame==c.r1.u32&&
             PPC_LOAD_U32(mesh.frame)==mesh.frame+0xC0&&mesh.metadata==c.r29.u32&&mesh.object==c.r26.u32&&
             PPC_LOAD_U32(mesh.metadata+0x24)>64&&mesh.paletteEntry==c.r31.u32&&
             mesh.paletteCount>=1&&mesh.paletteCount<=64,"Grouped shadow commit has no selected original palette");
    }
    // Original82C1DE7C and82C1E5E8 use category0 masks and 16-byte per-leaf
    // mappings. All seven other categories are empty for this exact pass.
    // Mapping word1 contains the starting VS register and (vector count-1)<<10.
    const auto body=record.metadata->body();const auto pass=record.metadata->techniques()[1];
    need(pass.contextOffset==51040&&word(body,0x120)==2&&word(body,0x124)==1&&
         word(body,0x130)==81&&word(body,0x134)==11,"Character shadow constant context extent differs");
    const auto at=pass.contextOffset;
    for(uint32_t space=0;space<2;++space)for(uint32_t category=0;category<8;++category) {
        const uint32_t mask=word(body,at+space*32+4*category),bytes=space?8:16;
        for(uint32_t i=0;i<bytes;++i) {
            uint8_t expected=0;
            if(category==0)for(uint32_t bit=0;bit<8;++bit){const auto leaf=8*i+bit;
                if(space?leaf==0:(leaf==2||leaf==10||(leaf>=14&&leaf<78)))expected|=uint8_t(0x80>>bit);}
            need(mask+i<body.size()&&body[mask+i]==expected,"Character shadow constant category mask differs");
        }
    }
    for(uint32_t space=0;space<2;++space) {
        const uint32_t rows=word(body,at+0x40+4*space),leaves=space?11:81;
        for(uint32_t leaf=0;leaf<leaves;++leaf) {
            uint32_t handle=0,mapping=0;
            if(space&&leaf==0){handle=0x00040001;mapping=0xC00;}
            else if(!space&&leaf==2){handle=0x000C0004;mapping=0xC0C;}
            else if(!space&&leaf==10){handle=0x00300014;mapping=40;}
            else if(!space&&leaf>=14&&leaf<78){handle=((leaf+3)<<18)|(leaf<<1);mapping=0x800+52+3*(leaf-14);}
            need(word(body,rows+16*leaf)==handle&&word(body,rows+16*leaf+4)==mapping&&
                 !word(body,rows+16*leaf+8)&&!word(body,rows+16*leaf+12),"Character shadow constant register mapping differs");
        }
    }
    const auto shared=sharedParameterStorage(v.identity,0x00040001);
    const auto values=parameterValues(v.identity);need(values.size()==1116,"Character shadow constant values extent differs");
    if(staticGeometry){
        s.runtime.pointer(c.r29.u32,0x2C,false);
        need(!PPC_LOAD_U32(c.r29.u32+0x24)&&!c.r28.u64&&!values[64]&&c.r1.u32>=0x200&&!(c.r1.u32&15)&&
             PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0x80,"Original static shadow commit has skinning enabled or a different frame");
    }
    Graphics::ShadowDepthConstants constants{};
    for(uint32_t row=0;row<4;++row)for(uint32_t lane=0;lane<4;++lane) {
        constants[row][lane]=std::bit_cast<float>(PPC_LOAD_U32(shared+16*row+4*lane));
        constants[12+row][lane]=std::bit_cast<float>(values[20+4*row+lane]);
    }
    for(uint32_t lane=0;lane<4;++lane)constants[40][lane]=std::bit_cast<float>(values[64+lane]);
    for(uint32_t bone=0;bone<64;++bone)for(uint32_t row=0;row<3;++row)for(uint32_t lane=0;lane<4;++lane)
        constants[52+3*bone+row][lane]=std::bit_cast<float>(values[80+16*bone+4*row+lane]);
    auto* sharedDirty=s.runtime.pointer(v.pool,128,true);
    auto committed=s.backend.commitShadowDepth(*s.shadowVertex,constants);
    s.shadowCommit=std::move(committed);record.privateModified.fill(0);std::memset(sharedDirty,0,128);
    if(grouped)s.characterMesh.paletteCommitted=true;
    static thread_local uint32_t logged{};if(logged++<4)std::fprintf(stderr,
        "[NATIVE SHADOW DEPTH COMMIT] id=%08X camera=%08X; actual original view-projection,world,skin flag and64 bone slots bound to VS; no draw\n",
        v.identity,s.shadowCamera);
}
std::array<uint32_t,976> EngineEffects::readbackShadowConstants() {
    auto& s=*state;s.require(s.runtime.base);need(s.activeShadowId&&s.shadowVertex,"No active character shadow constant owner");
    s.backend.requireShadowDepthShader(*s.shadowVertex);
    const auto values=s.backend.readbackShadowDepthConstants(s.shadowCommit);
    return std::bit_cast<std::array<uint32_t,976>>(values);
}
void EngineEffects::inspectShadowMesh(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.require(base);
    const bool staticGeometry=uint32_t(c.lr)==0x827076F8;
    if(c.lastFunction!=0x82706378||(uint32_t(c.lr)!=0x827071BC&&!staticGeometry)||!s.activeShadowId)return;
    s.activeShadowRecord(c,base);s.backend.requireShadowDepthCommit(s.shadowCommit);
    need(c.r3.u32==s.shadowTyped,"Original character mesh entry lost its typed owner");
    s.runtime.pointer(c.r5.u32,0x2C,false);const auto geometry=PPC_LOAD_U32(c.r5.u32+0xC);
    s.runtime.pointer(geometry,0x78,false);
    const auto bones=PPC_LOAD_U32(c.r5.u32+0x24);
    need(bones<=255,"Original shadow composed bone extent is unqualified");
    if(!staticGeometry) {
        need(c.r6.u32==0x82D64080,"Original character mesh lost its composed palette");
        if(bones>64){
            const auto& prelude=s.shadowPalettePrelude;
            need(prelude.cpu==&c&&prelude.metadata==c.r5.u32&&prelude.object==c.r4.u32&&prelude.frame==c.r1.u32,
                 "Original grouped character mesh lost its pre-composition owner");
            for(const auto& row:prelude.boneGroups)validateBoneGroupSnapshot(s.runtime,row.entry,row.words,row.ranges,row.owner);
        }
    }
    if(staticGeometry)need(!c.r6.u64&&!PPC_LOAD_U32(c.r5.u32+0x24)&&!s.privateWord(s.activeShadowId,64),
                          "Original static shadow mesh entry has skinning data enabled");
    // Each original call owns a fresh snapshot; D3D11 retains submitted buffers
    // until its work completes. Address reuse cannot reuse a stale native mesh.
    s.characterMesh={};s.characterMesh.geometry=geometry;s.characterMesh.metadata=c.r5.u32;
    s.characterMesh.object=c.r4.u32;s.characterMesh.frame=c.r1.u32-0xC0;
    s.characterMesh.submeshCount=PPC_LOAD_U32(c.r5.u32+0x10);s.characterMesh.submeshes=PPC_LOAD_U32(c.r5.u32+0x14);
    need(s.characterMesh.submeshCount<=65535,"Original character submesh extent is unqualified");
    s.characterMesh.staticGeometry=staticGeometry;
    if(bones>64)s.characterMesh.boneGroups=std::move(s.shadowPalettePrelude.boneGroups);
    s.shadowPalettePrelude={};
    static thread_local std::array<uint32_t,2> logged{};if(logged[staticGeometry?1:0]++>=4)return;
    std::fprintf(stderr,"[NATIVE SHADOW MESH] typed=%08X object=%08X metadata=%08X geometry=%08X bones=%u submeshes=%u matrices=%08X; original mesh entry retained\n",
        s.shadowTyped,c.r4.u32,c.r5.u32,geometry,PPC_LOAD_U32(c.r5.u32+0x24),PPC_LOAD_U32(c.r5.u32+0x10),c.r6.u32);
    std::fprintf(stderr,"[NATIVE SHADOW MESH METADATA]");
    for(uint32_t i=0;i<0x2C;i+=4)std::fprintf(stderr," %08X",PPC_LOAD_U32(c.r5.u32+i));std::fprintf(stderr,"\n");
    std::fprintf(stderr,"[NATIVE SHADOW MESH GEOMETRY]");
    for(uint32_t i=0;i<0x78;i+=4)std::fprintf(stderr," %08X",PPC_LOAD_U32(geometry+i));std::fprintf(stderr,"\n");
    const auto declaration=PPC_LOAD_U32(geometry+0x30);
    std::fprintf(stderr,"[NATIVE SHADOW MESH DECLARATION]");
    for(uint32_t i=0;i<24;i+=4)std::fprintf(stderr," %08X",PPC_LOAD_U32(declaration+i));std::fprintf(stderr,"\n");
    const auto submeshes=PPC_LOAD_U32(c.r5.u32+0x14);
    std::fprintf(stderr,"[NATIVE SHADOW MESH SUBMESH]");
    if(PPC_LOAD_U32(c.r5.u32+0x10))for(uint32_t i=0;i<36;i+=4)std::fprintf(stderr," %08X",PPC_LOAD_U32(submeshes+i));std::fprintf(stderr,"\n");
    const auto& effective=s.runtime.engineDriver->effectiveState();
    std::fprintf(stderr,"[NATIVE SHADOW MESH STATE]");
    for(const auto& field:Graphics::scalarStateEvidence())std::fprintf(stderr," %X=%08X",field.id,effective.scalar(field.id));std::fprintf(stderr,"\n");
    if(!s.runtime.frameCaptureDirectory.empty()) {
        const auto capture=[&](const char* name,uint32_t address,uint32_t bytes) {
            need(bytes<=16*1024*1024,"Shadow mesh diagnostic capture too large");
            const auto* data=s.runtime.pointer(address,bytes,false);
            char prefix[40];std::snprintf(prefix,sizeof(prefix),"%08X-",geometry);
            std::ofstream out(s.runtime.frameCaptureDirectory/(std::string(prefix)+name),std::ios::binary);
            out.write(reinterpret_cast<const char*>(data),bytes);
            need(bool(out),"Shadow mesh diagnostic capture failed");
        };
        capture("shadow-mesh-vertices.bin",PPC_LOAD_U32(geometry+0x10),PPC_LOAD_U32(geometry));
        capture("shadow-mesh-indices.bin",PPC_LOAD_U32(geometry+0x1C),PPC_LOAD_U32(geometry+0x14));
        capture("shadow-mesh-elements.bin",PPC_LOAD_U32(geometry+0xC),PPC_LOAD_U32(geometry+8)*12);
    }
    // Original82706378 continues into its native binding hooks and CPU loop.
}
void EngineEffects::shadowMeshOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.require(base);auto& mesh=s.characterMesh;
    s.activeShadowRecord(c,base,c.r30.u32);s.backend.requireShadowDepthCommit(s.shadowCommit);
    auto& driver=*s.runtime.engineDriver;
    need(mesh.geometry&&mesh.metadata==c.r29.u32&&mesh.object==c.r26.u32&&mesh.frame==c.r1.u32&&
         PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0xC0&&PPC_LOAD_U32(mesh.metadata+0xC)==mesh.geometry&&
         c.r27.u32==PPC_LOAD_U32(PPC_LOAD_U32(s.shadowTyped+0x10)+0x14)&&c.r3.u32==c.r27.u32,
         "Character mesh operation lost its original frame or resource association");
    driver.requireContext(c.r3.u32);
    const auto geometry=mesh.geometry;
    auto capture=[&](uint32_t address,uint32_t count,size_t maxBytes=16*1024*1024) {
        need(count&&count<=maxBytes,"Character mesh snapshot extent is invalid");
        const auto* begin=s.runtime.pointer(address,count,false);return std::vector<uint8_t>(begin,begin+count);
    };
    if(site==0x827063B8) {
        need(!mesh.phase&&c.r31.u32==geometry&&!c.r4.u32&&c.r5.u32==geometry+0x38&&!c.r6.u32&&
             c.r7.u32==PPC_LOAD_U32(geometry+4)&&c.r8.u32==1,"Original character stream arguments differ");
        const auto count=PPC_LOAD_U32(geometry+8),bones=PPC_LOAD_U32(mesh.metadata+0x24);
        need(count>=2&&count<=65,"Original character declaration length is unsupported");
        need(validOriginalVertexExtent(PPC_LOAD_U32(geometry),c.r7.u32),"Original character vertex byte owner or stride is invalid");
        auto header=capture(geometry,0x78);
        auto vertices=capture(PPC_LOAD_U32(geometry+0x10),PPC_LOAD_U32(geometry),originalGeometryMaxVertexBytes);
        auto indices=capture(PPC_LOAD_U32(geometry+0x1C),PPC_LOAD_U32(geometry+0x14),originalGeometryMaxIndexBytes);
        auto elements=capture(PPC_LOAD_U32(geometry+0xC),12*count);
        need(!mesh.staticGeometry||!bones,"Original static shadow mesh acquired a bone palette");
        std::vector<Graphics::ShadowMeshVertex> decoded;
        try {decoded=mesh.staticGeometry?decodeStaticShadowVertices(vertices,elements,c.r7.u32):
                                        decodeCharacterVertices(vertices,elements,c.r7.u32,std::min(bones,64u));}
        catch(const Failure& error){
            std::fprintf(stderr,"[NATIVE SHADOW DECODE REJECTED] geometry=%08X metadata=%08X object=%08X static=%u bones=%u stride=%u vertices_bytes=%zu prior_draws=%llu reason=%s\n",
                geometry,mesh.metadata,mesh.object,unsigned(mesh.staticGeometry),bones,c.r7.u32,vertices.size(),
                static_cast<unsigned long long>(s.backend.shadowMeshDrawCount()),error.what());
            std::fprintf(stderr,"[NATIVE SHADOW REJECTED DECLARATION]");
            for(const auto value:elements)std::fprintf(stderr,"%02X",value);std::fprintf(stderr,"\n");
            if(!s.runtime.frameCaptureDirectory.empty()){
                const auto write=[&](const char* name,std::span<const uint8_t> bytes){
                    std::ofstream out(s.runtime.frameCaptureDirectory/name,std::ios::binary);
                    out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
                    need(bool(out),"Rejected shadow mesh diagnostic write failed");
                };
                write("rejected-shadow-vertices.bin",vertices);write("rejected-shadow-indices.bin",indices);
                write("rejected-shadow-elements.bin",elements);write("rejected-shadow-geometry.bin",header);
                write("rejected-shadow-metadata.bin",capture(mesh.metadata,0x2C));
            }
            throw;
        }
        auto decodedIndices=decodeCharacterIndices(indices);
        // Both resource headers still identify this CPU geometry. No console
        // resource/header is submitted to the backend or edited by this bridge.
        need(PPC_LOAD_U32(geometry+0x18)==1&&
             (PPC_LOAD_U32(geometry+0x50)&~3u)==PPC_LOAD_U32(geometry+0x10)&&
             (PPC_LOAD_U32(geometry+0x54)&3)==2&&
             (PPC_LOAD_U32(geometry+0x58)&0xE0000003)==0x20000002&&
             PPC_LOAD_U32(geometry+0x70)==PPC_LOAD_U32(geometry+0x1C)&&
             PPC_LOAD_U32(geometry+0x74)==indices.size(),"Character source buffer headers differ");
        const auto cache=PPC_LOAD_U32(geometry+0x30);s.runtime.pointer(cache,12,false);
        const auto declaration=PPC_LOAD_U32(cache+4);need(declaration,"Original character declaration is missing");
        auto native=s.backend.uploadShadowMesh(decoded,decodedIndices);
        s.backend.bindShadowMeshVertices(native);
        mesh.header=std::move(header);mesh.vertices=std::move(vertices);mesh.indices=std::move(indices);mesh.elements=std::move(elements);
        mesh.cache=cache;mesh.declaration=declaration;mesh.native=std::move(native);mesh.phase=1;
    } else {
        need(mesh.native&&mesh.header.size()==0x78&&
             !std::memcmp(s.runtime.pointer(geometry,0x78,false),mesh.header.data(),0x78)&&
             PPC_LOAD_U32(mesh.cache+4)==mesh.declaration,"Bound character geometry/header changed before submission");
        if(site==0x827063C8) {
            need(mesh.phase==1&&c.r31.u32==geometry&&c.r4.u32==mesh.declaration,"Original character declaration arguments differ");
            s.backend.bindShadowMeshDeclaration(mesh.native);mesh.phase=2;
        } else if(site==0x827063D4) {
            need(mesh.phase==2&&c.r31.u32==geometry&&c.r4.u32==geometry+0x58,"Original character index arguments differ");
            s.backend.bindShadowMeshIndices(mesh.native);mesh.phase=3;
        } else {
            need(site==0x82706454&&mesh.phase==3,"Character mesh draw is out of order");
            const auto count=PPC_LOAD_U32(mesh.metadata+0x10),submeshes=PPC_LOAD_U32(mesh.metadata+0x14);
            need(count<=65535&&c.r28.u32%36==0&&c.r28.u32/36<count&&c.r31.u32==submeshes+c.r28.u32&&
                 c.r4.u32==PPC_LOAD_U32(c.r31.u32+12)&&c.r5.u32==PPC_LOAD_U32(c.r31.u32+16)&&
                 c.r6.u32==PPC_LOAD_U32(c.r31.u32+20)&&c.r7.u32==PPC_LOAD_U32(c.r31.u32+24),
                 "Original character submesh draw arguments differ");
            if(PPC_LOAD_U32(mesh.metadata+0x24)>64){
                validateBoneGroupSnapshot(s.runtime,mesh.boneGroups,c.r31.u32);
                need(mesh.paletteCommitted&&mesh.paletteEntry==c.r31.u32&&mesh.paletteCount>=1&&mesh.paletteCount<=64,
                     "Grouped shadow draw lost its committed submesh palette");
            }
            const auto& effective=driver.effectiveState();const auto camera=driver.cameraBinding();
            using S=Graphics::ScalarState;Graphics::ShadowMeshDraw draw{};
            draw.primitiveType=c.r4.u32;draw.baseVertex=c.r5.s32;draw.startIndex=c.r6.u32;draw.indexCount=c.r7.u32;
            draw.viewport=camera.viewport;
            const auto scissor=s.backend.scissor();need(scissor.has_value(),"Character shadow scissor has no native rectangle");draw.scissor=*scissor;
            draw.depthEnable=effective.scalar(S::DepthEnable);draw.depthWrite=effective.scalar(S::DepthWrite);draw.depthCompare=effective.scalar(S::DepthCompare);
            draw.cull=effective.scalar(S::Cull);draw.fill=effective.scalar(S::Fill);draw.colorMask=effective.scalar(S::ColorMask0);
            draw.stencilEnable=effective.scalar(S::StencilEnable);draw.alphaTest=effective.scalar(S::AlphaTest);
            draw.scissorEnable=effective.scalar(S::ScissorEnable);draw.halfPixelOffset=effective.scalar(S::HalfPixelOffset);
            draw.primitiveReset=effective.scalar(S::PrimitiveResetEnable);draw.primitiveResetIndex=effective.scalar(S::PrimitiveResetIndex);
            draw.viewportEnable=effective.scalar(S::ViewportEnable);draw.clipPlaneEnable=effective.scalar(S::ClipPlaneEnable);
            draw.multisampleAntialias=effective.scalar(S::MultisampleAntialias);draw.multisampleMask=effective.scalar(S::MultisampleMask);
            draw.alphaToMask=effective.scalar(S::AlphaToMask);draw.depthBiasBits=effective.scalar(S::DepthBias);draw.slopeBiasBits=effective.scalar(S::SlopeBias);
            draw.depthPolicy=Graphics::ShadowMeshDepthPolicy::Reference20e4Rne;
            need(!effective.scalar(S::TessellationMode),"Character shadow tessellation is unqualified");
            bool alphaOne=false;const auto color=driver.color(camera.colorIdentity,alphaOne);const auto depth=driver.depth(camera.depthIdentity);
            s.backend.drawShadowMesh(color,depth,mesh.native,*s.shadowVertex,s.shadowCommit,draw);
            if(s.backend.shadowMeshDrawCount()<=8)std::fprintf(stderr,
                "[NATIVE SHADOW MESH DRAW] geometry=%08X vertices=%u indices=%u start=%u base=%d primitive=%u count=%llu; actual native indexed character shadow submitted\n",
                geometry,mesh.native->vertexCount(),draw.indexCount,draw.startIndex,draw.baseVertex,draw.primitiveType,
                static_cast<unsigned long long>(s.backend.shadowMeshDrawCount()));
        }
    }
    c.lr=site+4;
}
uint64_t EngineEffects::shadowMeshDrawCount() const {state->require(state->runtime.base);return state->backend.shadowMeshDrawCount();}
void EngineEffects::completeShadowMesh(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.require(base);auto& mesh=s.characterMesh;
    s.activeShadowRecord(c,base,c.r30.u32);
    // The certified mid-function hook supplies the endpoint; lastFunction is
    // the last original function entry, not this inline epilogue instruction.
    need(currentContext==&c&&site==0x82706468&&mesh.geometry&&mesh.native&&mesh.phase==3&&!mesh.complete&&
         mesh.frame==c.r1.u32&&PPC_LOAD_U32(mesh.frame)==mesh.frame+0xC0&&
         mesh.metadata==c.r29.u32&&mesh.object==c.r26.u32&&
         PPC_LOAD_U32(mesh.metadata+0xC)==mesh.geometry&&
         PPC_LOAD_U32(mesh.metadata+0x10)==mesh.submeshCount&&PPC_LOAD_U32(mesh.metadata+0x14)==mesh.submeshes,
         "Original character mesh completion lost its traversal owner");
    // Zero rows branch here before initializing the countdown/cursor. Static
    // alpha-deferred rows share this real epilogue with submitted depth rows.
    need(!mesh.submeshCount||(!c.r22.u32&&c.r28.u32==36*mesh.submeshCount),
         "Original character mesh completion has unprocessed submeshes");
    mesh.complete=true;
}
uint64_t EngineEffects::skinMeshDrawCount() const {state->require(state->runtime.base);return state->backend.skinMeshDrawCount();}
void EngineEffects::setSkinBones(PPCContext&,uint8_t*) {
    throw Failure("Skin bone-array setter is not yet qualified");
}
void EngineEffects::skinMaterialOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.require(base);requireSkinSelection(s.skinTyped);auto& mesh=s.skinMesh;auto& run=s.skinMaterial;
    auto& record=s.find(s.skinId);const auto& v=record.view;
    need(currentContext==&c&&mesh.active&&mesh.cpu==&c&&mesh.phase==3&&mesh.cursor<mesh.draws.size()&&
         s.skinImmediate.active&&s.skinImmediate.cpu==&c&&!record.recordingContext&&mesh.submeshUpdated,
         "Skin material has no active original mesh");
    const auto& submesh=mesh.draws[mesh.cursor];
    if(site==0x826B54D0) {
        {static thread_local uint32_t skinMaterialEntrySample{};
        if(sampleHotLog(skinMaterialEntrySample))
            std::fprintf(stderr,"[NATIVE SKIN MATERIAL] entry material=%08X frame=%08X lr=%08X\n",
                submesh.material,mesh.frame,uint32_t(c.lr));}
        // Skin material-loop entry (bl 826B54D0 at 82701828, lr=8270182C).
        // Unlike rigid (r19=typed, r31=submesh entry), the skin loop leaves
        // r19 at the material-system base 0x82D70000 and r31 null; r3/r4 carry
        // typed/material identically. Neither register is read after entry.
        if(!(c.lastFunction==site&&c.r1.u32==mesh.frame&&
             c.r19.u32==0x82D70000&&!c.r31.u32&&c.r3.u32==s.skinTyped&&c.r4.u32==submesh.material&&!run.active&&!run.committed))
            std::fprintf(stderr,"[NATIVE SKIN MATERIAL ENTRY] fn=%08X r1=%08X frame=%08X r19=%08X typed=%08X r31=%08X entry=%08X r3=%08X r4=%08X material=%08X runA=%u runC=%u\n",
                c.lastFunction,c.r1.u32,mesh.frame,c.r19.u32,s.skinTyped,c.r31.u32,submesh.entry,
                c.r3.u32,c.r4.u32,submesh.material,run.active,run.committed);
        need(c.lastFunction==site&&c.r1.u32==mesh.frame&&
             c.r19.u32==0x82D70000&&!c.r31.u32&&c.r3.u32==s.skinTyped&&c.r4.u32==submesh.material&&!run.active&&!run.committed,
             "Unqualified original skin material-loop entry");
        State::SkinMaterialRun next;next.material=submesh.material;next.frame=mesh.frame-0xC0;
        const auto count=(PPC_LOAD_U32(next.material+0xC)>>10)&31,rows=PPC_LOAD_U32(next.material+0x14),bindings=PPC_LOAD_U32(s.skinTyped+0x28);
        if(count)s.runtime.pointer(rows,12*count,false);
        constexpr std::array<uint32_t,14> callbacks{0,0x8270BE50,0x8270BDB0,0x8270BBC0,0x8270BBC0,0x8270BBC0,
            0x8270BC60,0x8270BA80,0x8270BA80,0x8270BA80,0x8270BD08,0x8270BB20,0x8270BB20,0x8270BB20};
        for(uint32_t i=0;i<count;++i) {
            const auto bits=PPC_LOAD_U32(rows+12*i);
            State::SkinMaterialRun::Row row;row.offset=12*i;row.type=bits&255;row.value=PPC_LOAD_U32(rows+12*i+8);
            need(row.type&&row.type<callbacks.size()&&PPC_LOAD_U32(0x82CEFF88+8*row.type)==callbacks[row.type],
                 "Unqualified original skin material value callback");
            const auto binding=bindings+24*((bits>>16)&63);s.runtime.pointer(binding,24,false);
            for(uint32_t j=0;j<6;++j)row.binding[j]=PPC_LOAD_U32(binding+4*j);row.handle=row.binding[0];
            const auto entries=(row.handle&1)?s.schema().parameters(true):record.metadata->parameters(false);
            const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& p){return p.handle==row.handle;});
            need(found!=entries.end()&&!(found->descriptorWords[0]&3),"Skin material binding is not an owned leaf");
            const auto offset=16*(found->descriptorWords[1]&0xFFFF);
            const auto bytes=(row.handle&1)?s.schema().sharedDefaults().size():record.metadata->privateDefaults().size();
            need(offset<=bytes&&bytes-offset>=16&&((row.handle>>1)&0x1FFFF)<1024,"Skin material leaf exceeds storage");
            const bool vector=(row.type>=3&&row.type<=5)||(row.type>=7&&row.type<=9)||row.type>=11;
            if(vector){need(!(row.value&15),"Skin vector material input is not aligned for original LVX");s.runtime.pointer(row.value,16,false);}
            else if(row.type==6||row.type==10){need(!(row.value&3),"Skin integer material input is not word aligned");s.runtime.pointer(row.value&~15u,16,false);}
            else s.runtime.pointer(row.value,row.type==1?8:4,false);
            next.rows.push_back(row);
        }
        next.active=true;run=std::move(next);return;
    }
    need((site==0x826B2F20||site==0x82C1DBA0)&&run.active&&c.r1.u32==run.frame&&
         PPC_LOAD_U32(run.frame)==mesh.frame&&PPC_LOAD_U32(run.frame+0xB8)==0x8270182Cu&&
         uint32_t(c.lr)==0x826B560Cu&&c.r3.u32==v.identity&&c.r30.u32==s.skinTyped&&
         c.r28.u32==run.material&&run.cursor==run.rows.size()&&!run.pendingSlot,"Original skin material commit frame/order differs");
    s.validatePool(v.pool);
    need(word(record.metadata->body(),0x120)==2&&word(record.metadata->body(),0x124)==1&&PPC_LOAD_U32(v.pool+0x114)==1,
         "Skin material dirty filter extent differs");
    auto dirty=std::span<uint8_t,128>(s.runtime.pointer(v.pool,128,true),128);
    std::array<uint8_t,128> prospectiveShared;std::copy(dirty.begin(),dirty.end(),prospectiveShared.begin());
    if(site==0x826B2F20)filterRigidDirty(prospectiveShared,
        std::span<const uint8_t,128>(s.runtime.pointer(v.pool+0x80,128,false),128));
    if(PPC_LOAD_U32(0x82D00F80)&&!rigidSharedCommitHasNoWork(prospectiveShared,word(record.metadata->body(),0x124))) {
        char why[320];std::snprintf(why,sizeof(why),
            "Skin recording union shared-map has nonempty filtered work: site=%08X pool=%08X global=%08X words=%u",
            site,v.pool,PPC_LOAD_U32(0x82D00F80),word(record.metadata->body(),0x124));
        throw Failure(why);
    }
    for(const auto name:{"g_ViewProjection","kWorldToViewPortTfmLight","kWorldToViewPortTfmCharLight","kShadowAmt","kIsShadowReceiver",
                         "kShadowDepthSampler","kShadowCharDepthSampler"}) {
        const auto handle=parameter(v.identity,name),leaf=(handle>>1)&0x1FFFF;
        need((handle&1)&&leaf<1024,"Skin inherited shared mapping has an invalid shared handle");
        const auto binding=s.cachedPassBinding(record,s.alphaSkin()?0x0007FFFCu:0x0003FFFCu,handle);
        if(binding.usage)need(!(PPC_LOAD_U8(v.pool+0x80+leaf/8)&(0x80>>(leaf&7))),
             "Skin inherited shared mapping remains material-eligible");
    }
    if(site==0x826B2F20) {
        need(!run.filtered,"Skin material dirty filter repeated");
        filterRigidDirty(record.privateModified,record.privateMask);
        std::copy(prospectiveShared.begin(),prospectiveShared.end(),dirty.begin());
        run.filtered=true;return;
    }
    need(run.filtered&&!run.committed&&c.lastFunction==0x82C1DBA0,"Skin SDK material commit was not filtered");
    const auto profile=skinProfile(v.source);
    // Each submesh material sets its own subset (this one sets custom-lines
    // and rim-enable, not blendshape/rim-vector leaves), so completeness
    // across materials accumulates in the retained registers instead of
    // arriving in one commit. Validate per commit that every private row the
    // material processed survived filtering into its register leaf.
    for(const auto& materialRow:run.rows) {
        if(materialRow.handle&1)continue;
        const auto leaf=(materialRow.handle>>1)&0x1FFFF;
        if(!(record.privateMask[leaf/8]&(0x80>>(leaf&7))))continue;
        need(record.privateModified[leaf/8]&(0x80>>(leaf&7)),"Skin commit missed a material row register");
    }
    auto words=record.view.defaultVectorWords;
    if(record.parameters)for(uint32_t i=0;i<words.size();++i)words[i]=PPC_LOAD_U32(record.parameters->address+4*i);
    auto prospective=record.skinMaterialPS;projectSkinMaterial(words,record.privateModified,prospective,v.source,s.alphaSkin());
    const bool textured=profile.dual||profile.textured;
    const bool materialOnly=profile.gloss||profile.flipbook||profile.dualUv;
    const uint32_t samplerHandle=profile.dualUv?0x018000B6u:profile.gloss?0x016400A8u:profile.flipbook?0x016800AAu:textured?0x016000A6u:0x015C00A4u;
    const uint32_t samplerLeaf=profile.dualUv?91u:profile.gloss?84u:profile.flipbook?85u:textured?83u:82u;
    const uint32_t samplerWord=profile.dualUv?1168u:profile.gloss?1128u:profile.flipbook?1132u:textured?1124u:1120u;
    const auto selected=s.alphaSkin()?0x0007FFFCu:0x0003FFFCu;
    need(words.size()==profile.words&&parameter(v.identity,profile.dualUv?"g_BaseSampler":"g_Sampler")==samplerHandle,
         "Skin base sampler/storage profile differs");
    const auto selectedBinding=s.cachedPassBinding(record,selected,samplerHandle);
    const auto textureStage=Graphics::pixelTextureStage(selectedBinding);
    need(textureStage==((s.alphaSkin()||materialOnly)?std::optional<uint32_t>(0):
         (textured?std::optional<uint32_t>(1):std::nullopt)),"Skin selected base sampler map differs");
    if(textureStage) {
        if(record.privateModified[samplerLeaf/8]&(0x80>>(samplerLeaf&7))) {
            record.skinBaseTexture=s.runtime.engineDriver->materialTexture(base,words[samplerWord]);record.skinBaseHeader=words[samplerWord];
        }
        need(record.skinBaseTexture&&record.skinBaseHeader==words[samplerWord],"Skin has no current committed base texture");
        s.backend.bindEngineTexture(*textureStage,record.skinBaseTexture);
    }
    if(profile.dualUv) {
        const auto handle=parameter(v.identity,"g_BaseSampler2");need(handle==0x018400B8u,"Dual UV skin second sampler handle differs");
        const auto binding=s.cachedPassBinding(record,selected,handle);
        need(Graphics::pixelTextureStage(binding)==std::optional<uint32_t>(1),"Dual UV skin second texture stage differs");
        if(record.privateModified[92/8]&(0x80>>(92&7))) {
            record.skinSecondTexture=s.runtime.engineDriver->materialTexture(base,words[1184]);record.skinSecondHeader=words[1184];
        }
        need(record.skinSecondTexture&&record.skinSecondHeader==words[1184],"Dual UV skin current second texture missing");
        s.backend.bindEngineTexture(1,record.skinSecondTexture);
    }
    // Every source-specific material vector is checked against the selected
    // original table before projecting dirtiness; managed ticker/world/eye
    // and all unsubmitted lanes remain in the actual original stage upload.
    const auto map=word(record.metadata->body(),(s.alphaSkin()?skinAlphaPass(v.source).context:profile.context)+0x40);
    for(uint32_t stage=0;stage<2;++stage) {
        const auto rows=stage?skinPixelMaterialRows(v.source,s.alphaSkin()):skinVertexMaterialRows(v.source,s.alphaSkin());
        for(const auto& row:rows) {
            const auto binding=s.cachedPassBinding(record,selected,word(record.metadata->body(),map+16*row.leaf));
            // The base opaque leaf16 is genuinely shared by VS39 and PS39.
            // Its VS lane remains in the original managed upload; material
            // projection here consumes only its separately selected PS lane.
            const bool baseShared=v.source==0x82006348&&!s.alphaSkin()&&stage==1&&row.leaf==16;
            need(binding.usage==(baseShared?3u:stage?2u:1u)&&bool(binding.lanes[stage])&&
                 (baseShared?(row.reg==39&&binding.lanes[0]&&binding.lanes[0]->start==39&&binding.lanes[0]->count==1):!binding.lanes[1-stage])&&
                 binding.lanes[stage]->start==row.reg&&binding.lanes[stage]->count==1,"Skin selected material register mapping differs");
        }
    }
    auto prospectiveVS=record.skinMaterialVS;projectSkinVertexMaterial(words,record.privateModified,prospectiveVS,v.source,s.alphaSkin());
    {
        uint32_t dirtyBones=0;for(uint32_t b=0;b<64;++b){const auto leaf=profile.boneLeaf+b;if(record.privateModified[leaf/8]&(0x80>>(leaf&7)))++dirtyBones;}
        {static thread_local uint32_t skinBonesDirtySample{};
        if(sampleHotLog(skinBonesDirtySample))
            std::fprintf(stderr,"[NATIVE SKIN BONES] dirtyBones=%u/64\n",dirtyBones);}
        projectSkinBoneMatrices(words,record.privateModified,prospectiveVS,v.source);
    }
    const auto materialRegisters=skinPixelMaterialRows(v.source,s.alphaSkin());
    for(const auto& row:materialRegisters)
        for(float lane:prospective[row.reg])need(std::isfinite(lane),"Skin material register contains nonfinite data");
    {
        const auto& staging=s.skinImmediate.staging;
        need(staging.complete&&staging.phase==4&&!staging.prepared,"Skin immediate material lost its original stage uploads");
        need(staging.vsRegisters==56&&staging.psRegisters==56,"Skin immediate staging bank extent changed");
        for(uint32_t stage=0;stage<2;++stage) {
            const size_t bytes=56*16;
            need(staging.copies[stage].size()==bytes&&
                 !std::memcmp(s.runtime.pointer(stage?0x82D6C450:0x82D6C0D0,uint32_t(bytes),false),staging.copies[stage].data(),bytes),
                 "Skin immediate per-submesh staging changed without a qualified original upload");
        }
        Graphics::SkinVertexConstants vertex{};Graphics::SkinPixelConstants pixel{};
        for(uint32_t i=0;i<56&&i<vertex.size();++i)for(uint32_t j=0;j<4;++j)
            vertex[i][j]=std::bit_cast<float>(word(staging.copies[0],16*i+4*j));
        for(uint32_t i=0;i<56&&i<pixel.size();++i)for(uint32_t j=0;j<4;++j)
            pixel[i][j]=std::bit_cast<float>(word(staging.copies[1],16*i+4*j));
        for(const auto& row:materialRegisters)pixel[row.reg]=prospective[row.reg];
        for(const auto& row:skinVertexMaterialRows(v.source,s.alphaSkin()))vertex[row.reg]=prospectiveVS[row.reg];
        applySkinBonePalette(vertex,prospectiveVS);
        if(vertex[39][3]>.5f)for(uint32_t i=0;i<6;++i)
            need((mesh.morphBoundMask&(1u<<i))||vertex[38+i/4][i%4]==0,
                 "Active skin morph coefficient has no selected source stream");
        s.skinImmediate.commit=s.backend.commitSkin(*s.skinVertex,*s.skinPixel,vertex,pixel);
    }
    record.skinMaterialPS=prospective;record.skinMaterialVS=prospectiveVS;record.skinMaterialInitialized=true;
    record.privateModified.fill(0);std::fill(dirty.begin(),dirty.end(),uint8_t(0));
    run.active=false;run.committed=true;
    c.r3.u64=0;
}
void EngineEffects::skinParameterOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.require(base);auto& run=s.skinMaterial;auto& mesh=s.skinMesh;
    if(!(currentContext==&c&&mesh.cpu==&c&&mesh.active&&run.active&&!run.filtered&&c.r1.u32==run.frame&&
         PPC_LOAD_U32(run.frame)==mesh.frame&&PPC_LOAD_U32(run.frame-8)==0x826B55F4u&&
         c.r30.u32==s.skinTyped&&c.r28.u32==run.material&&run.cursor<run.rows.size())) {
        auto peek=[&](uint32_t a)->uint32_t { try { s.runtime.pointer(a,4,false); } catch(...) { return 0xDEADDEADu; } return PPC_LOAD_U32(a); };
        std::fprintf(stderr,"[NATIVE SKIN MATERIAL PARAM] cpu=%d active=%u runA=%u filt=%u r1=%08X runframe=%08X star=%08X meshframe=%08X star8=%08X r30=%08X typed=%08X r28=%08X mat=%08X cursor=%u/%zu site=%08X lr=%08X\n",
            currentContext==&c,mesh.cpu==&c,run.active,run.filtered,c.r1.u32,run.frame,peek(run.frame),mesh.frame,
            peek(run.frame-8),c.r30.u32,s.skinTyped,c.r28.u32,run.material,run.cursor,run.rows.size(),site,uint32_t(c.lr));
    }
    need(currentContext==&c&&mesh.cpu==&c&&mesh.active&&run.active&&!run.filtered&&c.r1.u32==run.frame&&
         PPC_LOAD_U32(run.frame)==mesh.frame&&PPC_LOAD_U32(run.frame-8)==0x826B55F4u&&
         c.r30.u32==s.skinTyped&&c.r28.u32==run.material&&run.cursor<run.rows.size(),
         "Unqualified original skin material descriptor continuation");
    auto& record=s.find(s.skinId);
    need(s.skinImmediate.active&&s.skinImmediate.cpu==&c&&!record.recordingContext,
         "Skin material setter lost its native lifetime");
    const auto& row=run.rows[run.cursor];const auto packet=run.frame+0x50;
    need(c.r29.u32==row.offset&&PPC_LOAD_U32(packet)==s.skinId&&PPC_LOAD_U32(packet+4)==row.handle&&
         PPC_LOAD_U32(packet+0x20)==row.value,"Original skin material callback packet differs");
    for(uint32_t j=0;j<6;++j)need(PPC_LOAD_U32(packet+8+4*j)==row.binding[j],"Original skin material binding snapshot differs");
    const uint32_t expected=row.type==1?0x8270BE7C:row.type==2?0x8270BDDC:row.type<=5?0x8270BBEC:
        row.type==6?0x8270BC90:row.type<=9?0x8270BAB0:row.type==10?0x8270BD38:0x8270BB50;
    const bool second=site==0x8270BCB0||site==0x8270BD58;
    if(second) {
        need(run.pendingSlot&&run.pendingSite==site,"Skin scalar conversion/store is out of order");
        c.r11.u64=run.pendingSlot;run.pendingSlot=run.pendingSite=0;++run.cursor;return;
    }
    need(site==expected&&!run.pendingSlot&&c.r11.u32==s.skinId,"Skin value callback type differs");
    uint32_t destination;
    if(row.handle&1) {
        destination=sharedParameterStorage(s.skinId,row.handle);const auto leaf=(row.handle>>1)&0x1FFFF;
        auto* byte=s.runtime.pointer(record.view.pool+leaf/8,1,true);*byte|=uint8_t(0x80>>(leaf&7));
    } else destination=s.edgeSlot(record,row.handle);
    if(row.type==6||row.type==10) {
        run.pendingSlot=destination;run.pendingSite=row.type==6?0x8270BCB0:0x8270BD58;
        return;
    }
    if(row.type==1){c.r10.u64=destination;c.r8.u64=0;}
    else {c.r11.u64=destination;c.r6.u64=0;}
    ++run.cursor;
}
void EngineEffects::skinMeshOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    if(site==0x82701638)observeProducerEntry(c,base,"skin_mesh_entry");
    auto& s=*state;s.require(base);requireSkinSelection(s.skinTyped);
    auto& driver=*s.runtime.engineDriver;auto& record=s.find(s.skinId);auto& mesh=s.skinMesh;const auto profile=skinProfile(record.view.source);
    const bool immediate=s.skinImmediate.active;
    need(immediate&&!record.recordingContext&&s.skinImmediate.cpu==&c&&s.skinImmediate.activated&&
         s.skinImmediate.staging.complete,"Skin mesh has no completed immediate staging");
    {static thread_local uint32_t skinMeshSiteSample{};
    if(sampleHotLog(skinMeshSiteSample))
        std::fprintf(stderr,"[NATIVE SKIN MESH] site=%08X lr=%08X r1=%08X frame=%08X\n",
            site,uint32_t(c.lr),c.r1.u32,mesh.frame);}
    if(site==0x82701638) {
        // Checked read stays unconditional (same word the entry path consumes);
        // only the diagnostic print is sampled.
        const uint32_t meshStack=PPC_LOAD_U32(c.r1.u32);
        {static thread_local uint32_t skinMeshEntrySample{};
        if(sampleHotLog(skinMeshEntrySample))
            std::fprintf(stderr,"[NATIVE SKIN MESH] site=%08X lr=%08X r1=%08X stack=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X r31=%08X active=%u frame=%08X typed=%08X\n",
                site,uint32_t(c.lr),c.r1.u32,meshStack,c.r3.u32,c.r4.u32,c.r5.u32,c.r6.u32,c.r7.u32,c.r8.u32,c.r31.u32,
                s.skinImmediate.active,s.skinImmediate.frame,s.skinImmediate.typed);}
        need(c.lastFunction==site&&uint32_t(c.lr)==0x82740294&&!mesh.active&&!mesh.complete&&
             c.r5.u32==s.skinTyped&&!c.r6.u32&&c.r7.u32==PPC_LOAD_U32(c.r3.u32+0x10),
             "Unqualified original skin mesh-loop entry");
        const auto packet=c.r31.u32,meta=c.r3.u32,object=c.r4.u32;
        s.runtime.pointer(meta,0x2C,false);s.runtime.pointer(object,0x40,false);
        need(PPC_LOAD_U32(packet)==meta&&PPC_LOAD_U32(packet+4)==object&&PPC_LOAD_U32(packet+0x18)==s.skinTyped&&
             PPC_LOAD_U32(meta+0x24)>=1&&PPC_LOAD_U32(meta+0x24)<=255&&!PPC_LOAD_U32(0x82D6CCA8),
             "Original skin mesh packet/static qualification differs");
        const auto geometry=PPC_LOAD_U32(meta+0xC),objectGeometry=PPC_LOAD_U32(object+0x18);
        s.runtime.pointer(geometry,0x78,false);s.runtime.pointer(objectGeometry,0x28,false);
        State::SkinMeshRun next;next.cpu=&c;next.entrySP=c.r1.u32;next.frame=c.r1.u32-0xF0;next.immediate=true;
        next.packet=packet;next.metadata=meta;next.object=object;next.geometry=geometry;next.context=record.contextIdentity;
        const auto count=PPC_LOAD_U32(meta+0x10),entries=PPC_LOAD_U32(meta+0x14),collection=PPC_LOAD_U32(meta+0x34);
        skinSubmeshExtent(s.runtime,entries,count);s.runtime.pointer(collection,12,false);
        const auto materialCount=PPC_LOAD_U32(collection),offsets=PPC_LOAD_U32(objectGeometry+0x24),headers=PPC_LOAD_U32(0x82D6D814);
        need(materialCount<=65535,"Skin material collection extent is unqualified");s.runtime.pointer(collection,12+4*materialCount,false);
        for(uint32_t i=0;i<count;++i) {
            State::SkinMeshRun::Submesh row;row.entry=entries+36*i;
            for(uint32_t j=0;j<9;++j)row.words[j]=PPC_LOAD_U32(row.entry+4*j);
            need(uint64_t(offsets)+4ull*row.words[0]+4<=UINT32_MAX,"Skin material offset table overflows");
            s.runtime.pointer(offsets+4*row.words[0],4,false);row.materialOffset=PPC_LOAD_U32(offsets+4*row.words[0]);
            need(uint64_t(headers)+row.materialOffset+4<=UINT32_MAX,"Skin material header overflows");
            row.materialHeader=headers+row.materialOffset;s.runtime.pointer(row.materialHeader,4,false);
            row.flags=PPC_LOAD_U16(row.materialHeader);
            if(row.flags&0x20)continue;
            need(row.words[1]<materialCount,"Skin submesh material index is out of range");
            row.material=PPC_LOAD_U32(collection+12+4*row.words[1]);s.runtime.pointer(row.material,0x18,false);
            if(PPC_LOAD_U32(meta+0x24)>64){
                row.boneOwner=boneGroupOwner(s.runtime,row.words[8],row.words[7]);
                (void)skinBoneGroupCount(s.runtime,PPC_LOAD_U32(meta+0x24),row.words);
                const auto* ranges=s.runtime.pointer(row.words[8],2*row.words[7],false);
                row.boneRanges.assign(ranges,ranges+2*row.words[7]);
                validateBoneGroupSnapshot(s.runtime,row.entry,row.words,row.boneRanges,row.boneOwner);
            }
            next.draws.push_back(row);
        }
        next.active=true;mesh=std::move(next);return;
    }
    need(mesh.active&&mesh.immediate&&mesh.cpu==&c&&mesh.context==record.contextIdentity&&
         PPC_LOAD_U32(mesh.metadata+0xC)==mesh.geometry,"Skin mesh lifetime changed");
    const auto geometry=mesh.geometry;
    const auto applyMorphs=[&](std::vector<Graphics::SkinVertex>& vertices) {
        if(mesh.morphBoundMask||mesh.morphClearMask)
            need((mesh.morphBoundMask|mesh.morphClearMask)==0x3F&&!(mesh.morphBoundMask&mesh.morphClearMask),
                 "Original skin morph loop did not resolve all six streams");
        for(uint32_t i=0;i<6;++i)if(mesh.morphBoundMask&(1u<<i))decodeSkinMorphStream(vertices,i+1,mesh.morphStreams[i]);
    };
    if(site==0x826FF340) {
        need(!mesh.phase&&!c.r4.u32&&c.r5.u32==geometry+0x38&&!c.r6.u32&&c.r7.u32==PPC_LOAD_U32(geometry+4)&&c.r8.u32==1,
             "Original skin vertex-stream arguments differ");
        {static thread_local uint32_t skinVerticesSample{};
        if(sampleHotLog(skinVerticesSample))
            std::fprintf(stderr,"[NATIVE SKIN VERTICES] geometry=%08X stride=%u\n",geometry,c.r7.u32);}
        const auto capture=[&](uint32_t address,uint32_t bytes,size_t maxBytes=64*1024*1024){need(bytes&&bytes<=maxBytes,"Skin source byte extent is unqualified");
            const auto* p=s.runtime.pointer(address,bytes,false);return std::vector<uint8_t>(p,p+bytes);};
        const auto count=PPC_LOAD_U32(geometry+8);need(count==((profile.dual||profile.gloss||profile.flipbook||profile.dualUv)?14u:13u)||
             (s.alphaSkin()&&(profile.gloss||profile.flipbook)&&count==13u),"Skin declaration extent is unqualified");
        auto header=capture(geometry,0x78);
        // Exact guest-write-watched source lookup (see rigidMeshOperation). Morph
        // streams are separate guest data outside the key, so morphing meshes
        // always take the full path.
        const bool skinSourceCached=!mesh.morphBoundMask&&c.r7.u32<0x8000;
        const uint64_t skinVariant=(uint64_t(record.view.source)<<32)|(uint64_t(c.r7.u32)<<1)|uint64_t(s.alphaSkin());
        StaticMeshSourceCache<Graphics::NativeSkinMesh>::LookupResult skinSourceLookup;
        if(skinSourceCached) {
            const auto borrow=[&](uint32_t address,uint32_t bytes,size_t maxBytes){need(bytes&&bytes<=maxBytes,"Skin source byte extent is unqualified");
                return std::span<const uint8_t>(s.runtime.pointer(address,bytes,false),bytes);};
            const auto vertexView=borrow(PPC_LOAD_U32(geometry+0x10),PPC_LOAD_U32(geometry),64*1024*1024);
            const auto indexView=borrow(PPC_LOAD_U32(geometry+0x1C),PPC_LOAD_U32(geometry+0x14),originalGeometryMaxIndexBytes);
            const auto elementView=borrow(PPC_LOAD_U32(geometry+0xC),12*count,64*1024*1024);
            skinSourceLookup=s.skinSourceCache(s.skinVertex->originalAddress()).lookup(vertexView,indexView,elementView,skinVariant);
            if(auto hit=skinSourceLookup.mesh) {
                const auto cache=PPC_LOAD_U32(geometry+0x30);s.runtime.pointer(cache,12,false);
                const auto declaration=PPC_LOAD_U32(cache+4);
                need(PPC_LOAD_U32(geometry+0x18)==1&&declaration&&c.r23.u32==declaration,"Original skin source/declaration headers differ");
                s.backend.bindSkinMeshVertices(hit);
                mesh.header=std::move(header);mesh.cache=cache;mesh.declaration=declaration;mesh.native=std::move(hit);mesh.phase=1;
                c.lr=site+4;return;
            }
        }
        const auto vertices=capture(PPC_LOAD_U32(geometry+0x10),PPC_LOAD_U32(geometry));
        const auto indices=capture(PPC_LOAD_U32(geometry+0x1C),PPC_LOAD_U32(geometry+0x14),originalGeometryMaxIndexBytes);
        const auto elements=capture(PPC_LOAD_U32(geometry+0xC),12*count);
        auto decoded=decodeSkinVertices(vertices,elements,c.r7.u32,record.view.source,s.alphaSkin());applyMorphs(decoded);
        const auto decodedIndices=decodeCharacterIndices(indices);
        const auto cache=PPC_LOAD_U32(geometry+0x30);s.runtime.pointer(cache,12,false);
        const auto declaration=PPC_LOAD_U32(cache+4);
        need(PPC_LOAD_U32(geometry+0x18)==1&&declaration&&c.r23.u32==declaration,"Original skin source/declaration headers differ");
        std::shared_ptr<Graphics::NativeSkinMesh> native;
        try {native=s.backend.uploadSkinMesh(decoded,decodedIndices,s.skinVertex->originalAddress());}
        catch(const Graphics::Error& error) {
            if(!s.runtime.frameCaptureDirectory.empty()) {
                std::fprintf(stderr,"[NATIVE SKIN REJECTED INPUT] geometry=%08X: %s\n",geometry,error.what());
            }
            throw;
        }
        s.backend.bindSkinMeshVertices(native);
        if(skinSourceCached)s.skinSourceCache(s.skinVertex->originalAddress()).insert(vertices,indices,elements,skinVariant,skinSourceLookup.key,native,&skinSourceLookup.pending);
        mesh.header=std::move(header);mesh.cache=cache;mesh.declaration=declaration;mesh.native=std::move(native);mesh.phase=1;
    } else if(site==0x826FF498) {
        if(!mesh.phase) {
            // Original826FF338 skips the 826FF340 stream-commit bl when
            // geometry+52 is set (skin shape: *(r23+36)==0xFFFFFF00 path);
            // the declaration bl at 826FF498 still runs with r4/r23 holding
            // the bound descriptor. Upload the vertex stream here so the
            // bind below sees the same phase==1 the 340 site would produce.
            {static thread_local uint32_t skinVerticesDeferredSample{};
            if(sampleHotLog(skinVerticesDeferredSample))
                std::fprintf(stderr,"[NATIVE SKIN VERTICES] deferred geometry=%08X r3=%08X r4=%08X r23=%08X\n",
                    geometry,c.r3.u32,c.r4.u32,c.r23.u32);}
            const auto captureDeferred=[&](uint32_t address,uint32_t bytes,size_t maxBytes=64*1024*1024){need(bytes&&bytes<=maxBytes,"Skin source byte extent is unqualified");
                const auto* p=s.runtime.pointer(address,bytes,false);return std::vector<uint8_t>(p,p+bytes);};
            const auto deferredCount=PPC_LOAD_U32(geometry+8);need(deferredCount==((profile.dual||profile.gloss||profile.flipbook||profile.dualUv)?14u:13u)||
                 (s.alphaSkin()&&(profile.gloss||profile.flipbook)&&deferredCount==13u),"Skin declaration extent is unqualified");
            auto deferredHeader=captureDeferred(geometry,0x78);
            const uint32_t deferredStrideWord=PPC_LOAD_U32(geometry+4);
            const bool deferredSourceCached=!mesh.morphBoundMask&&deferredStrideWord<0x8000;
            const uint64_t deferredVariant=(uint64_t(record.view.source)<<32)|(uint64_t(deferredStrideWord)<<1)|uint64_t(s.alphaSkin());
            StaticMeshSourceCache<Graphics::NativeSkinMesh>::LookupResult deferredLookup;
            if(deferredSourceCached) {
                const auto borrow=[&](uint32_t address,uint32_t bytes,size_t maxBytes){need(bytes&&bytes<=maxBytes,"Skin source byte extent is unqualified");
                    return std::span<const uint8_t>(s.runtime.pointer(address,bytes,false),bytes);};
                const auto vertexView=borrow(PPC_LOAD_U32(geometry+0x10),PPC_LOAD_U32(geometry),64*1024*1024);
                const auto indexView=borrow(PPC_LOAD_U32(geometry+0x1C),PPC_LOAD_U32(geometry+0x14),originalGeometryMaxIndexBytes);
                const auto elementView=borrow(PPC_LOAD_U32(geometry+0xC),12*deferredCount,64*1024*1024);
                deferredLookup=s.skinSourceCache(s.skinVertex->originalAddress()).lookup(vertexView,indexView,elementView,deferredVariant);
                if(auto hit=deferredLookup.mesh) {
                    const auto cache=PPC_LOAD_U32(geometry+0x30);s.runtime.pointer(cache,12,false);
                    const auto declaration=PPC_LOAD_U32(cache+4);
                    need(PPC_LOAD_U32(geometry+0x18)==1&&declaration&&c.r23.u32==declaration&&c.r4.u32==declaration,
                         "Original skin deferred declaration headers differ");
                    s.backend.bindSkinMeshVertices(hit);
                    mesh.header=std::move(deferredHeader);mesh.cache=cache;mesh.declaration=declaration;mesh.native=std::move(hit);mesh.phase=1;
                }
            }
            if(!mesh.phase) {
            const auto deferredVertices=captureDeferred(PPC_LOAD_U32(geometry+0x10),PPC_LOAD_U32(geometry));
            const auto deferredIndices=captureDeferred(PPC_LOAD_U32(geometry+0x1C),PPC_LOAD_U32(geometry+0x14),originalGeometryMaxIndexBytes);
            const auto deferredElements=captureDeferred(PPC_LOAD_U32(geometry+0xC),12*deferredCount);
            auto deferredDecoded=decodeSkinVertices(deferredVertices,deferredElements,PPC_LOAD_U32(geometry+4),record.view.source,s.alphaSkin());applyMorphs(deferredDecoded);
            const auto deferredDecodedIndices=decodeCharacterIndices(deferredIndices);
            const auto deferredCache=PPC_LOAD_U32(geometry+0x30);s.runtime.pointer(deferredCache,12,false);
            const auto deferredDeclaration=PPC_LOAD_U32(deferredCache+4);
            need(PPC_LOAD_U32(geometry+0x18)==1&&deferredDeclaration&&c.r23.u32==deferredDeclaration&&c.r4.u32==deferredDeclaration,
                 "Original skin deferred declaration headers differ");
            // Checked read stays unconditional (same word consumed by decode
            // above); only the diagnostic print is sampled.
            const uint32_t deferredStride=PPC_LOAD_U32(geometry+4);
            {static thread_local uint32_t skinVerticesStrideSample{};
            if(sampleHotLog(skinVerticesStrideSample))
                std::fprintf(stderr,"[NATIVE SKIN VERTICES] geometry=%08X stride=%u\n",geometry,deferredStride);}
            auto deferredNative=s.backend.uploadSkinMesh(deferredDecoded,deferredDecodedIndices,s.skinVertex->originalAddress());
            s.backend.bindSkinMeshVertices(deferredNative);
            if(deferredSourceCached)s.skinSourceCache(s.skinVertex->originalAddress()).insert(deferredVertices,deferredIndices,deferredElements,deferredVariant,deferredLookup.key,deferredNative,&deferredLookup.pending);
            mesh.header=std::move(deferredHeader);mesh.cache=deferredCache;mesh.declaration=deferredDeclaration;
            mesh.native=std::move(deferredNative);mesh.phase=1;
            }
        }
        need(mesh.phase==1&&c.r4.u32==mesh.declaration,"Skin declaration binding is out of order");
        s.backend.bindSkinMeshDeclaration(mesh.native);mesh.phase=2;
    } else if(site==0x826FF4A4) {
        need(mesh.phase==2&&c.r4.u32==geometry+0x58,"Skin index binding is out of order");
        s.backend.bindSkinMeshIndices(mesh.native);mesh.phase=3;
    } else if(site==0x8270142C) {
        need(mesh.phase==3&&mesh.cursor==mesh.draws.size()&&!s.skinMaterial.active&&!s.skinMaterial.committed,
             "Skin immediate auxiliary-stream cleanup differs");
        mesh.auxiliaryCleared=true;
    } else if(site==0x82701438) {
        need(mesh.phase==3&&mesh.cursor==mesh.draws.size()&&!s.skinMaterial.active&&mesh.auxiliaryCleared,
             "Original skin loop ended before its material/draw closure");
        mesh.active=false;mesh.complete=true;return;
    } else {
        need(site==0x827013B0&&mesh.phase==3&&mesh.cursor<mesh.draws.size()&&
             s.skinMaterial.committed&&!s.skinMaterial.active&&mesh.submeshUpdated,
             "Skin draw has no completed material commit");
        const auto& row=mesh.draws[mesh.cursor];
        {static thread_local uint32_t skinMeshDrawSample{};
        if(sampleHotLog(skinMeshDrawSample))
            std::fprintf(stderr,"[NATIVE SKIN DRAW] submesh=%08X material=%08X flags=%04X words3..6=%08X %08X %08X %08X\n",
                row.entry,row.material,row.flags,row.words[3],row.words[4],row.words[5],row.words[6]);}
        need(c.r31.u32==row.entry&&c.r3.u32==mesh.context&&c.r4.u32==row.words[3]&&c.r5.u32==row.words[4]&&
             c.r6.u32==row.words[5]&&c.r7.u32==row.words[6]&&s.skinMaterial.material==row.material&&
             PPC_LOAD_U16(row.materialHeader)==row.flags,"Original skin draw arguments/material differ");
        for(uint32_t j=0;j<9;++j)need(PPC_LOAD_U32(row.entry+4*j)==row.words[j],"Skin submesh changed before draw");
        if(PPC_LOAD_U32(mesh.metadata+0x24)>64)validateBoneGroupSnapshot(s.runtime,row.entry,row.words,row.boneRanges,row.boneOwner);
        const auto& effective=driver.effectiveState();const auto camera=driver.cameraBinding();
        need(camera.camera==s.skinCamera,"Skin draw camera owner changed");
        using S=Graphics::ScalarState;Graphics::SkinMeshDraw draw{};
        draw.primitiveType=c.r4.u32;draw.baseVertex=c.r5.s32;draw.startIndex=c.r6.u32;draw.indexCount=c.r7.u32;draw.viewport=camera.viewport;
        if(const auto scissor=s.backend.scissor())draw.scissor=*scissor;
        draw.depthEnable=effective.scalar(S::DepthEnable);draw.depthWrite=effective.scalar(S::DepthWrite);draw.depthCompare=effective.scalar(S::DepthCompare);
        draw.cull=effective.scalar(S::Cull);draw.fill=effective.scalar(S::Fill);draw.colorMask=effective.scalar(S::ColorMask0);
        draw.stencilEnable=effective.scalar(S::StencilEnable);draw.alphaTest=effective.scalar(S::AlphaTest);
        draw.scissorEnable=effective.scalar(S::ScissorEnable);draw.halfPixelOffset=effective.scalar(S::HalfPixelOffset);
        draw.primitiveReset=effective.scalar(S::PrimitiveResetEnable);draw.primitiveResetIndex=effective.scalar(S::PrimitiveResetIndex);
        draw.viewportEnable=effective.scalar(S::ViewportEnable);draw.clipPlaneEnable=effective.scalar(S::ClipPlaneEnable);
        draw.multisampleAntialias=effective.scalar(S::MultisampleAntialias);draw.multisampleMask=effective.scalar(S::MultisampleMask);
        draw.alphaToMask=effective.scalar(S::AlphaToMask);draw.depthBiasBits=effective.scalar(S::DepthBias);draw.slopeBiasBits=effective.scalar(S::SlopeBias);
        draw.depthPolicy=Graphics::ShadowMeshDepthPolicy::Reference20e4Rne;
        draw.blendEnable=effective.scalar(S::BlendEnable);draw.blendWord=effective.effectiveBlend(0);draw.expandedBlend=effective.scalar(S::ExpandedBlend0);
        observeSkinDraw(draw,mesh.geometry,row.entry);
        prepareSkinDrawTextures(draw);
        need(!effective.scalar(S::TessellationMode),"Skin tessellation is unqualified");
        bool alphaOne=false;const auto color=driver.color(camera.colorIdentity,alphaOne);need(!alphaOne,"Skin color owner requires unsupported alpha synthesis");
        s.backend.drawSkinMesh(color,driver.depth(camera.depthIdentity),mesh.native,*s.skinVertex,*s.skinPixel,
            s.skinImmediate.commit,draw);
        ++mesh.cursor;s.skinMaterial.committed=false;mesh.submeshUpdated=false;
        {static thread_local uint32_t skinMeshImmediateDrawSample{};
        if(sampleHotLog(skinMeshImmediateDrawSample))
            std::fprintf(stderr,"[NATIVE SKIN IMMEDIATE DRAW] geometry=%08X submesh=%08X indices=%u completed=%u/%zu\n",
                geometry,row.entry,draw.indexCount,mesh.cursor,mesh.draws.size());}
    }
    c.lr=site+4;
}
void EngineEffects::skinReplayOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.require(base);
    auto& run=s.skinImmediate.staging;
    {static thread_local uint32_t skinReplayEntrySample{};
    if(sampleHotLog(skinReplayEntrySample))
        std::fprintf(stderr,"[NATIVE SKIN REPLAY] site=%08X lr=%08X r1=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X r30=%08X r31=%08X cpu=%p runCpu=%p packet=%08X\n",
            site,uint32_t(c.lr),c.r1.u32,c.r3.u32,c.r4.u32,c.r5.u32,c.r6.u32,c.r7.u32,c.r8.u32,c.r30.u32,c.r31.u32,
            (void*)currentContext,(void*)run.cpu,run.packet);}
    need(currentContext==&c&&run.cpu==&c&&run.packet&&!run.prepared,
         "Skin replay staging has no original replay association");
    auto& record=s.find(s.skinId);
    need(s.skinImmediate.activated&&s.skinImmediate.cpu==&c&&s.skinImmediate.frame==run.entrySP&&
         run.typed==s.skinTyped&&run.context==record.contextIdentity&&!record.recordingContext,
         "Skin immediate staging lost its effect/context owner");
    if(site==0x826B5770) {
        need(c.lastFunction==site&&uint32_t(c.lr)==0x82740198u&&c.r1.u32==run.entrySP&&c.r31.u32==run.packet&&
             c.r3.u32==run.typed&&c.r4.u32==PPC_LOAD_U32(run.packet)&&c.r5.u32==PPC_LOAD_U32(run.packet+4)&&
             c.r6.u32==PPC_LOAD_U32(run.packet+8)&&!run.shared&&!run.callbacks,
             "Unqualified original skin replay callback entry");
        const auto count=PPC_LOAD_U32(run.typed+0x34),rows=PPC_LOAD_U32(run.typed+0x30);
        {static thread_local uint32_t skinReplayClassSample{};
        if(sampleHotLog(skinReplayClassSample))
            std::fprintf(stderr,"[NATIVE SKIN REPLAY] typedClass count=%u rows=%08X\n",count,rows);}
        need(count<=64,"Skin replay classification extent is unqualified");if(count)s.runtime.pointer(rows,28*count,false);
        run.callbacks=true;return;
    }
    if(site==0x826B3270) {
        need(c.lastFunction==site&&uint32_t(c.lr)==0x826B589C&&run.callbacks&&!run.upload&&
             c.r1.u32==run.entrySP-0xD0&&PPC_LOAD_U32(c.r1.u32)==run.entrySP&&
             PPC_LOAD_U32(c.r1.u32+0xC8)==0x82740198u&&c.r30.u32==run.typed&&
             !c.r3.u32&&!c.r5.u32,
             "Original skin replay must upload both complete register banks");
        {static thread_local uint32_t skinReplayBanksSample{};
        if(sampleHotLog(skinReplayBanksSample))
            std::fprintf(stderr,"[NATIVE SKIN REPLAY] bankCounts r4=%u r6=%u\n",c.r4.u32,c.r6.u32);}
        need(c.r4.u32&&c.r4.u32<=256&&c.r6.u32&&c.r6.u32<=256,"Skin replay bank extent is unqualified");
        run.vsRegisters=c.r4.u32;run.psRegisters=c.r6.u32;
        run.copies[0].assign(run.vsRegisters*16,0);run.copies[1].assign(run.psRegisters*16,0);
        // TEMPORARY buffers are owner-scoped pooled with exact register extents:
        // fresh poison on every lease, grow safely when skin extents exceed idle.
        run.buffers=s.acquireTempUpload({run.vsRegisters*4,run.vsRegisters*4,run.psRegisters*4,run.psRegisters*4});
        run.uploadSP=c.r1.u32-0x90;run.upload=true;return;
    }
    need(run.upload&&!run.complete&&c.r1.u32==run.uploadSP&&PPC_LOAD_U32(run.uploadSP)==run.entrySP-0xD0&&
         PPC_LOAD_U32(run.uploadSP+0x88)==0x826B589C,"Original skin stage-upload frame differs");
    if(site==0x826B32E4||site==0x826B3330) {
        const uint32_t stage=site==0x826B32E4?0:1;
        need(run.phase==2*stage&&c.r3.u32==run.context&&c.r4.u32==stage&&!c.r5.u32&&c.r6.u32==0x82D6C7D0&&
             c.r7.u32==0x82D6C7E0&&c.r8.u32==56,"Original skin stage-upload SDK arguments/order differ");
        const auto source=stage?0x82D6C450u:0x82D6C0D0u;
        const size_t bytes=stage?run.psRegisters*16:run.vsRegisters*16;
        std::memcpy(run.copies[stage].data(),s.runtime.pointer(source,uint32_t(bytes),false),bytes);
        PPC_STORE_U32(c.r6.u32,run.buffers[2*stage]->address);PPC_STORE_U32(c.r7.u32,run.buffers[2*stage+1]->address);
        ++run.phase;c.r3.u64=0;c.lr=site+4;return;
    }
    need(site==0x826B3314||site==0x826B3364,"Unknown skin replay staging boundary");
    const uint32_t stage=site==0x826B3314?0:1;
    need(run.phase==2*stage+1&&PPC_LOAD_U32(0x82D6C7D0)==run.buffers[2*stage]->address&&
         PPC_LOAD_U32(0x82D6C7E0)==run.buffers[2*stage+1]->address,"Original skin upload destination publication changed");
    const auto source=stage?0x82D6C450u:0x82D6C0D0u;
    const size_t bytes=stage?run.psRegisters*16:run.vsRegisters*16;
    need(!std::memcmp(s.runtime.pointer(source,uint32_t(bytes),false),run.copies[stage].data(),bytes),"Skin staging changed during original upload");
    for(uint32_t i=0;i<2;++i)need(!std::memcmp(s.runtime.pointer(run.buffers[2*stage+i]->address,uint32_t(bytes),false),run.copies[stage].data(),bytes),
        "Original skin stage upload did not copy all registers");
    ++run.phase;if(stage==1)run.complete=true;
    {static thread_local uint32_t skinReplayStageSample{};
    if(sampleHotLog(skinReplayStageSample))
        std::fprintf(stderr,"[NATIVE SKIN REPLAY] stage=%u phase=%u complete=%u\n",stage,run.phase,run.complete);}
    // Observation only: original context getters and both epilogues still run.
}
void EngineEffects::skinImmediateOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.require(base);auto& run=s.skinImmediate;auto& driver=*s.runtime.engineDriver;
    if(site==0x827005DC&&!run.active)return;
    need(currentContext==&c,"Skin immediate work has no original CPU frame");
    if(site==0x827400F8) {
        const auto owner=s.packetOwner(c.r3.u32);
        need(c.lastFunction==site&&uint32_t(c.lr)==0x82740B28&&!run.active&&!s.skinMesh.active&&
             !s.skinMaterial.active&&c.r4.u32<=1&&c.r5.u32<=1&&c.r6.u32<=1&&
             owner.vtable==0x82061714&&isSkinSource(owner.source)&&
             c.r1.u32>=0x400&&!(c.r1.u32&15),"Unqualified original opaque skin fallback entry");
        const bool alpha=c.r4.u32==1;
        // Original82740120 overwrites incoming r5 with packet.object before
        // any use. Dispatcher metadata can supply either Boolean here.
        const auto packet=c.r3.u32;s.runtime.pointer(packet,0x24,false);
        const auto meta=PPC_LOAD_U32(packet),object=PPC_LOAD_U32(packet+4),typed=PPC_LOAD_U32(packet+0x18);
        s.runtime.pointer(meta,0x2C,false);s.runtime.pointer(object,0x40,false);s.runtime.pointer(typed,0xB0,false);
        // Checked reads stay unconditional (same words verified by the need
        // below); only the diagnostic print is sampled.
        const uint32_t fallbackVtable=PPC_LOAD_U32(typed),fallbackMeta24=PPC_LOAD_U32(meta+0x24),fallbackCca8=PPC_LOAD_U32(0x82D6CCA8);
        {static thread_local uint32_t skinFallbackSample{};
        if(sampleHotLog(skinFallbackSample))
            std::fprintf(stderr,"[NATIVE SKIN FALLBACK] packet=%08X meta=%08X object=%08X typed=%08X vtable=%08X meta24=%08X cca8=%08X\n",
                packet,meta,object,typed,fallbackVtable,fallbackMeta24,fallbackCca8);}
        need(PPC_LOAD_U32(typed)==0x82061714&&!PPC_LOAD_U32(0x82D6CCA8),
             "Skin immediate fallback requires the original skinned opaque packet");
        auto& record=s.find(PPC_LOAD_U32(typed+0x1C));
        // Unlike rigid, the skin immediate packet carries no context at +0x14
        // (observed zero); the creation context owns all skin draws here.
        const auto context=record.contextIdentity;driver.requireContext(context);
        need(!record.recordingContext,"Skin immediate fallback overlaps a recording lease");
        s.requireRigidReplayFinished();
        s.recycleTempUpload(s.skinReplay.buffers);s.skinReplay={};s.skinMesh={};s.skinMaterial={};s.skinTextures={};s.recycleTempUpload(run.staging.buffers);run={};
        run.cpu=&c;run.entrySP=c.r1.u32;run.frame=c.r1.u32-0x80;run.packet=packet;run.typed=typed;run.context=context;
        run.drawsBefore=s.backend.skinMeshDrawCount();run.active=true;run.alpha=alpha;
        run.staging.cpu=&c;run.staging.packet=packet;run.staging.typed=typed;run.staging.context=context;run.staging.entrySP=run.frame;
        return;
    }
    need(run.active&&run.cpu==&c&&run.activated,"Skin immediate continuation has no activated lifetime");
    requireSkinSelection(run.typed);driver.requireContext(run.context);
    need(!s.find(s.skinId).recordingContext,"Skin immediate continuation acquired a recording lease");
    auto& mesh=s.skinMesh;
    if(site==0x82700498) {
        if(uint32_t(c.lr)==0x827017DC) {
            // Skin mesh-loop submesh update (bl 82700498 at 827017D8). The
            // caller keeps r23=meta (callee-saved): r3=typed, r4=metadata,
            // r5=object exactly like rigid; r6/r7 carry the same submesh
            // material selection.
            if(!(c.lastFunction==site&&c.r1.u32==mesh.frame&&mesh.active&&mesh.immediate&&
                 mesh.cursor<mesh.draws.size()&&!run.submeshActive&&!mesh.submeshUpdated&&c.r3.u32==run.typed&&
                 c.r4.u32==mesh.metadata&&c.r5.u32==mesh.object))
                std::fprintf(stderr,"[NATIVE SKIN SUBMESH ENTRY] fn=%08X lr=%08X r1=%08X frame=%08X active=%u imm=%u cursor=%u/%zu subA=%u subU=%u r3=%08X typed=%08X r4=%08X meta=%08X r5=%08X obj=%08X\n",
                    c.lastFunction,uint32_t(c.lr),c.r1.u32,mesh.frame,mesh.active,mesh.immediate,
                    mesh.cursor,mesh.draws.size(),run.submeshActive,mesh.submeshUpdated,c.r3.u32,run.typed,
                    c.r4.u32,mesh.metadata,c.r5.u32,mesh.object);
            need(c.lastFunction==site&&c.r1.u32==mesh.frame&&mesh.active&&mesh.immediate&&
                 mesh.cursor<mesh.draws.size()&&!run.submeshActive&&!mesh.submeshUpdated&&c.r3.u32==run.typed&&
                 c.r4.u32==mesh.metadata&&c.r5.u32==mesh.object,"Skin mesh-loop submesh update entry differs");
            const auto& srow=mesh.draws[mesh.cursor];
            if(!(c.r6.u32==srow.words[1]&&c.r7.u32==srow.words[0]))
                std::fprintf(stderr,"[NATIVE SKIN SUBMESH ENTRY] r6=%08X w1=%08X r7=%08X w0=%08X\n",
                    c.r6.u32,srow.words[1],c.r7.u32,srow.words[0]);
            need(c.r6.u32==srow.words[1]&&c.r7.u32==srow.words[0],"Skin mesh-loop submesh material selection differs");
            run.submeshActive=true;return;
        }
        need(c.lastFunction==site&&uint32_t(c.lr)==0x82701310&&c.r1.u32==mesh.frame&&mesh.active&&mesh.immediate&&
             mesh.cursor<mesh.draws.size()&&!run.submeshActive&&!mesh.submeshUpdated&&c.r3.u32==run.typed&&
             c.r4.u32==mesh.metadata&&c.r5.u32==mesh.object,"Skin immediate submesh update entry differs");
        const auto& row=mesh.draws[mesh.cursor];
        need(c.r6.u32==row.words[1]&&c.r7.u32==row.words[0],"Skin immediate submesh material selection differs");
        run.submeshActive=true;return;
    }
    if(site==0x827005DC) {
        need(run.submeshActive&&c.r1.u32==mesh.frame-0xE0&&PPC_LOAD_U32(c.r1.u32)==mesh.frame&&
             PPC_LOAD_U32(c.r1.u32+0xD8)==0x827017DC&&c.r28.u32==run.typed&&
             c.r25.u32==mesh.draws[mesh.cursor].material,"Skin immediate submesh update did not return through its original frame");
        run.submeshActive=false;mesh.submeshUpdated=true;return;
    }
    if(!(site==0x827402E4&&c.r1.u32==run.frame&&PPC_LOAD_U32(run.frame)==run.entrySP&&
         PPC_LOAD_U32(run.frame+0x78)==0x82740B28&&c.r31.u32==run.packet&&c.r30.u32==uint32_t(run.alpha)&&
         mesh.immediate&&mesh.complete&&!mesh.active&&mesh.auxiliaryCleared&&mesh.cursor==mesh.draws.size()&&
         !run.submeshActive&&!s.skinMaterial.active&&!s.skinMaterial.committed&&
         run.staging.complete&&run.staging.phase==4)) {
        auto peek=[&](uint32_t a)->uint32_t { try { s.runtime.pointer(a,4,false); } catch(...) { return 0xDEADDEADu; } return PPC_LOAD_U32(a); };
        std::fprintf(stderr,"[NATIVE SKIN FALLBACK END] site=%08X r1=%08X runframe=%08X star=%08X entrySP=%08X star78=%08X r31=%08X packet=%08X r30=%08X imm=%u complete=%u active=%u aux=%u cursor=%u/%zu subA=%u matA=%u matC=%u stageC=%u stageP=%u\n",
            site,c.r1.u32,run.frame,peek(run.frame),run.entrySP,peek(run.frame+0x78),c.r31.u32,run.packet,c.r30.u32,
            mesh.immediate,mesh.complete,mesh.active,mesh.auxiliaryCleared,mesh.cursor,mesh.draws.size(),
            run.submeshActive,s.skinMaterial.active,s.skinMaterial.committed,run.staging.complete,run.staging.phase);
    }
    need(site==0x827402E4&&c.r1.u32==run.frame&&PPC_LOAD_U32(run.frame)==run.entrySP&&
         PPC_LOAD_U32(run.frame+0x78)==0x82740B28&&c.r31.u32==run.packet&&c.r30.u32==uint32_t(run.alpha)&&
         mesh.immediate&&mesh.complete&&!mesh.active&&mesh.auxiliaryCleared&&mesh.cursor==mesh.draws.size()&&
         !run.submeshActive&&!s.skinMaterial.active&&!s.skinMaterial.committed&&
         run.staging.complete&&run.staging.phase==4,
         "Skin immediate fallback ended before original staging, draws and cleanup completed");
    need(s.backend.skinMeshDrawCount()==run.drawsBefore+mesh.cursor,"Skin immediate native draw receipt differs");
    {static thread_local uint32_t skinImmediateCompleteSample{};
    if(sampleHotLog(skinImmediateCompleteSample))
        std::fprintf(stderr,"[NATIVE SKIN IMMEDIATE COMPLETE] packet=%08X geometry=%08X draws=%u; original fallback and stream cleanup completed\n",
            run.packet,mesh.geometry,mesh.cursor);}
    run.active=false;run.commit.reset();
}
void EngineEffects::requireSkinRecordingComplete(uint32_t) const {
    throw Failure("Skin recording completion is not yet qualified");
}
void EngineEffects::prepareSkinReplay(uint32_t) {
    throw Failure("Skin replay preparation is not yet qualified");
}
std::span<const uint8_t> EngineEffects::originalBytes(uint32_t id) const {state->require(state->runtime.base);return state->find(id).metadata->bytes();}
std::array<uint8_t,128> EngineEffects::privateParameterMask(uint32_t id) const {state->require(state->runtime.base);return state->find(id).privateMask;}
std::array<uint8_t,128> EngineEffects::privateModifiedMask(uint32_t id) const {state->require(state->runtime.base);return state->find(id).privateModified;}
uint32_t EngineEffects::typedReflectionCount(uint32_t id) const {state->require(state->runtime.base);return state->find(id).typedReflections;}
size_t EngineEffects::count() const {state->require(state->runtime.base);return state->records.size();}
size_t EngineEffects::shaderCount(uint32_t id) const {state->require(state->runtime.base);return state->find(id).shaders.size();}
size_t EngineEffects::compiledShaderCount(uint32_t id) const {
    state->require(state->runtime.base);const auto& record=state->find(id);size_t compiled=0;
    for(const auto shader:record.shaders) compiled+=record.shaderRecords.capability(shader)==Graphics::MaterialCapability::Compiled;
    return compiled;
}
uint32_t EngineEffects::sharedParameterStorage(uint32_t id,uint32_t handle) const {
    state->require(state->runtime.base);const auto& record=state->find(id);
    need(record.view.phase==Phase::Reflected && !record.metadata->parameters(true).empty(),"Native FX has no shared parameter association");
    state->validatePool(record.view.pool);
    for(const auto& p:state->schema().parameters(true)) if(p.handle==handle) {
        need(!(p.descriptorWords[0]&3),"Shared parameter is not a qualified leaf");
        const uint32_t offset=16*(p.descriptorWords[1]&0xFFFF);
        need(offset<state->schema().sharedDefaults().size(),"Shared parameter storage offset exceeds pool");
        return PPCLoadU32(state->runtime.base,record.view.pool+0x108)+offset;
    }
    throw Failure("Unknown shared native FX parameter handle");
}
void EngineEffects::deviceEpilogueOperation(PPCContext& c,uint8_t* base) {
    // The original helper is immediate geometry setup, not an epilogue.
    // Its four constructor-owned CPU buffers have native backing. General
    // 32-byte geometry draws remain explicitly guarded at82751E38.
    auto& s=*state;s.require(base);
    s.runtime.engineDriver->immediateGeometryState(c,base,true);
}
void EngineEffects::requireReleased() const {
    need(active==&state->runtime && GetCurrentThreadId()==state->thread,"Native FX retirement is outside its runtime/thread");
    need(state->records.empty(),"Original FX wrappers still own native effects at driver stop");
}
void EngineEffects::requirePoolReleased(uint32_t pool) const {
    need(active==&state->runtime && GetCurrentThreadId()==state->thread,"Native FX pool retirement is outside its owner thread");
    for(const auto& [id,record]:state->records) {
        (void)id;need(record->view.pool!=pool,"Original FX pool still has a native effect lifetime lease");
    }
}
}
void SimpsonsNativeEffectCreate(PPCContext& ctx,uint8_t* base) {HostState fp;effects(base).create(ctx,base);}
void SimpsonsNativeEffectReflect(PPCContext& ctx,uint8_t* base) {HostState fp;effects(base).reflect(ctx,base);}
void SimpsonsNativeEffectTypedReflection(PPCContext& ctx,uint8_t* base) {HostState fp;effects(base).reflectTyped(ctx,base);}
void SimpsonsNativeEffectShadowSamplers(PPCContext& ctx,uint8_t* base) {HostState fp;effects(base).initializeShadowSamplers(ctx,base);}
void SimpsonsNativeShadowWorld(PPCContext& ctx,uint8_t* base) {HostState fp;effects(base).setShadowWorld(ctx,base);}
void SimpsonsNativeEffectRelease(PPCContext& ctx,uint8_t* base) {HostState fp;effects(base).release(ctx,base);}
void SimpsonsNativeEffectTechnique(PPCContext& ctx,uint8_t* base) {HostState fp;effects(base).query(ctx,base,true);}
void SimpsonsNativeEffectParameter(PPCContext& ctx,uint8_t* base) {HostState fp;effects(base).query(ctx,base,false);}
void SimpsonsNativeEdgeLineWidth(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeParameter(c,b,0x823C9100);}
void SimpsonsNativeEdgeFadeControl(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeParameter(c,b,0x823C91A8);}
void SimpsonsNativeEdgeWidth(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeParameter(c,b,0x823C9264);}
void SimpsonsNativeEdgeHeightA(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeParameter(c,b,0x823C92F4);}
void SimpsonsNativeEdgeHeightB(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeParameter(c,b,0x823C92FC);}
void SimpsonsNativeEdgeHeightC(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeParameter(c,b,0x823C9318);}
void SimpsonsNativeEdgeHeightD(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeParameter(c,b,0x823C9364);}
void SimpsonsNativeEdgePalette(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeParameter(c,b,0x823C93A4);}
void SimpsonsNativeEdgeColor(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeParameter(c,b,0x823C9448);}
void SimpsonsNativeEdgeDepth(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeParameter(c,b,0x823C94C0);}
void SimpsonsNativeEdgeBoolean(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeBoolean(c,b);}
void SimpsonsNativeShadowBones(PPCContext& c,uint8_t* b){HostState fp;effects(b).setShadowBones(c,b);}
void SimpsonsNativeShadowMesh(PPCContext& c,uint8_t* b){HostState fp;effects(b).inspectShadowMesh(c,b);}
void SimpsonsNativeShadowStream(PPCContext& c,uint8_t* b){HostState fp;effects(b).shadowMeshOperation(c,b,0x827063B8);}
void SimpsonsNativeShadowDeclaration(PPCContext& c,uint8_t* b){HostState fp;effects(b).shadowMeshOperation(c,b,0x827063C8);}
void SimpsonsNativeShadowIndices(PPCContext& c,uint8_t* b){HostState fp;effects(b).shadowMeshOperation(c,b,0x827063D4);}
void SimpsonsNativeShadowDraw(PPCContext& c,uint8_t* b){HostState fp;effects(b).shadowMeshOperation(c,b,0x82706454);}
void SimpsonsNativeShadowMeshEnd(PPCContext& c,uint8_t* b){HostState fp;effects(b).completeShadowMesh(c,b,0x82706468);}
void SimpsonsNativeZPrepassMesh(PPCContext& c,uint8_t* b){HostState fp;effects(b).inspectZPrepassMesh(c,b);}
void SimpsonsNativeZPrepassStream(PPCContext& c,uint8_t* b){HostState fp;effects(b).zprepassMeshOperation(c,b,0x826FF340);}
void SimpsonsNativeZPrepassDeclaration(PPCContext& c,uint8_t* b){HostState fp;effects(b).zprepassMeshOperation(c,b,0x826FF498);}
void SimpsonsNativeZPrepassIndices(PPCContext& c,uint8_t* b){HostState fp;effects(b).zprepassMeshOperation(c,b,0x826FF4A4);}
void SimpsonsNativeZPrepassDraw(PPCContext& c,uint8_t* b){HostState fp;effects(b).zprepassMeshOperation(c,b,0x826FF584);}
void SimpsonsNativeZPrepassStreamCleanup(PPCContext& c,uint8_t* b){HostState fp;effects(b).zprepassMeshOperation(c,b,0x826FF5B0);}
void SimpsonsNativeEdgeBegin(PPCContext& c,uint8_t* b){HostState fp;effects(b).beginEdge(c,b);}
void SimpsonsNativeEffectRecordingContext(PPCContext& c,uint8_t* b){HostState fp;effects(b).associateRecordingContext(c,b);}
void SimpsonsNativeRigidTextures_826F39E0(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidTextureOperation(c,b,0x826F39E0);}
void SimpsonsNativeRigidTextures_826F3A10(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidTextureOperation(c,b,0x826F3A10);}
void SimpsonsNativeRigidTextures_826F3ABC(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidTextureOperation(c,b,0x826F3ABC);}
void SimpsonsNativeRigidTextures_826F3ADC(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidTextureOperation(c,b,0x826F3ADC);}
void SimpsonsNativeRigidTextures_826F3B5C(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidTextureOperation(c,b,0x826F3B5C);}
void SimpsonsNativeRigidTextures_826F3B6C(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidTextureOperation(c,b,0x826F3B6C);}
void SimpsonsNativeRigidMeshEntry(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidMeshOperation(c,b,0x82701448);}
void SimpsonsNativeRigidImmediateEntry(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidImmediateOperation(c,b,0x827400F8);}
void SimpsonsNativeRigidImmediateComplete(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidImmediateOperation(c,b,0x827402E4);}
void SimpsonsNativeRigidImmediateMeshEntry(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidMeshOperation(c,b,0x82701220);}
void SimpsonsNativeSkinMeshEntry(PPCContext& c,uint8_t* b){HostState fp;effects(b).skinMeshOperation(c,b,0x82701638);}
void SimpsonsNativeSkinStreamSetup(PPCContext& c,uint8_t* b){HostState fp;effects(b).skinStreamSetup(c,b);}
void SimpsonsNativeSkinSamplerSkip(PPCContext& c,uint8_t* b){HostState fp;effects(b).skinSamplerSkip(c,b);}
void SimpsonsNativeSkinMorphSkip(PPCContext& c,uint8_t* b){HostState fp;effects(b).skinMorphSkip(c,b);}
void SimpsonsNativeSkinSamplerCommitSkip(PPCContext& c,uint8_t* b){HostState fp;effects(b).skinSamplerCommitSkip(c,b);}
void SimpsonsNativeSkinMorphStreamBind(PPCContext& c,uint8_t* b){HostState fp;effects(b).skinMorphStreamBind(c,b);}
void SimpsonsNativeSkinDraw(PPCContext& c,uint8_t* b){HostState fp;effects(b).skinDraw(c,b);}
void SimpsonsNativeSkinLoopEnd(PPCContext& c,uint8_t* b){HostState fp;effects(b).skinLoopEnd(c,b);}
void SimpsonsNativeSkyTextureBind(PPCContext& c,uint8_t* b){HostState fp;effects(b).skyTextureBind(c,b);}
void SimpsonsNativeDeviceEpilogue(PPCContext& c,uint8_t* b){HostState fp;effects(b).deviceEpilogueOperation(c,b);}
void EngineEffects::observeSkinDraw(const Graphics::SkinMeshDraw& draw,uint32_t geometry,uint32_t submesh) const noexcept {
    auto& s=*state;if(!s.runtime.resourceAudit.active())return;
    try {
        const auto source=s.find(s.skinId).view.source;
        char asset[40],parameters[384],instance[128];
        std::snprintf(asset,sizeof(asset),"source:%08X",source);
        std::snprintf(parameters,sizeof(parameters),"vs=%08X ps=%08X primitive=%u baseVertex=%d startIndex=%u indexCount=%u blendEnable=%u blendWord=%08X expandedBlend=%u depth=%u/%u/%u cull=%u alphaTest=%u stencil=%u restart=%u/%08X",
            s.skinVertex?s.skinVertex->originalAddress():0,s.skinPixel?s.skinPixel->originalAddress():0,draw.primitiveType,draw.baseVertex,draw.startIndex,draw.indexCount,
            draw.blendEnable,draw.blendWord,draw.expandedBlend,draw.depthEnable,draw.depthWrite,draw.depthCompare,draw.cull,draw.alphaTest,draw.stencilEnable,draw.primitiveReset,draw.primitiveResetIndex);
        std::snprintf(instance,sizeof(instance),"effect=%08X typed=%08X geometry=%08X submesh=%08X",s.skinId,s.skinTyped,geometry,submesh);
        s.runtime.resourceAudit.observe("geometry_use",asset,currentContext?uint32_t(currentContext->lr):0,parameters,
            "FX-reflected-mesh-live",s.runtime.nativeDepthCopyCount.load(),instance);
    }catch(...){std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] skin draw capture failed\n");}
}
void EngineEffects::prepareSkinDrawTextures(Graphics::SkinMeshDraw& draw) {
    auto& s=*state;auto& record=s.find(s.skinId);const auto profile=skinProfile(record.view.source);
    const auto& effective=s.runtime.engineDriver->effectiveState();
    if(profile.gloss||profile.flipbook||profile.dualUv) {
        need(record.skinBaseTexture&&s.skinTextures.complete&&!s.skinTextures.active,"Material skin lacks completed texture transfer/base owner");
        draw.baseTexture=record.skinBaseTexture;
        if(profile.dualUv){need(bool(record.skinSecondTexture),"Dual UV skin lacks second material texture");draw.secondTexture=record.skinSecondTexture;}
        draw.shadowSamplePolicy=Graphics::RigidShadowSamplePolicy::NotUsed;
        constexpr std::array<uint32_t,20> values{0,0,0,0,1,1,1,0,0,1,1,1,0,13,0,0,0,0,0,1};
        for(uint32_t stage=0;stage<(profile.dualUv?2u:1u);++stage) {
            for(uint32_t i=0;i<values.size();++i)need(effective.sampler(stage,4*i)==values[i],"Material skin original sampler state differs");
            auto& sampler=draw.samplers[stage];sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
            sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxLOD=13;
        }
    } else if((profile.dual||profile.textured)&&!s.alphaSkin()) {
        need(s.skinTextures.complete&&!s.skinTextures.active&&s.skinTextures.characterShadow&&record.skinBaseTexture,
             "Dual skin draw lacks its completed texture transfer/material");
        constexpr std::array<uint32_t,20> shadowState{2,2,2,0,0,0,2,0,0,1,1,1,0,13,0,0,0,0,0,1};
        constexpr std::array<uint32_t,20> baseState{0,0,0,0,1,1,1,0,0,1,1,1,0,13,0,0,0,0,0,1};
        for(uint32_t stage=0;stage<2;++stage)for(uint32_t i=0;i<20;++i) {
            const auto expected=stage?baseState[i]:shadowState[i];
            if(effective.sampler(stage,4*i)!=expected)
                std::fprintf(stderr,"[NATIVE SKIN SAMPLER] stage=%u sdk=%X actual=%X expected=%X\n",stage,4*i,effective.sampler(stage,4*i),expected);
            need(effective.sampler(stage,4*i)==expected,"Dual skin original sampler state differs");
        }
        draw.characterShadow=s.skinTextures.characterShadow;draw.baseTexture=record.skinBaseTexture;
        draw.shadowSamplePolicy=Graphics::RigidShadowSamplePolicy::ReferenceD24FS8DepthRRRR;
        for(uint32_t i=0;i<2;++i) {
            auto& sampler=draw.samplers[i];sampler.Filter=i?D3D11_FILTER_MIN_MAG_MIP_LINEAR:D3D11_FILTER_MIN_MAG_MIP_POINT;
            sampler.AddressU=sampler.AddressV=sampler.AddressW=i?D3D11_TEXTURE_ADDRESS_WRAP:D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxLOD=i?13.0f:0.0f;
        }
    } else if(s.alphaSkin()) {
        need(static_cast<bool>(record.skinBaseTexture),"Base skin alpha draw lacks its committed material texture");
        constexpr std::array<uint32_t,6> samplerIds{0,4,8,0x10,0x14,0x18};
        constexpr std::array<uint32_t,6> samplerValues{0,0,0,1,1,1};
        for(uint32_t i=0;i<samplerIds.size();++i)
            need(effective.sampler(0,samplerIds[i])==samplerValues[i],"Base skin alpha original sampler state differs");
        draw.baseTexture=record.skinBaseTexture;
        auto& sampler=draw.samplers[0];sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxLOD=13.0f;
    }
}
void EngineEffects::skinDraw(PPCContext& c,uint8_t* base) {
    // Replaces the bl 8244D360 at 827018C0 (resumes at 827018C4): the shared
    // draw submitter for bound vertices plus committed constants (rigid draws
    // through the same helper from 827013B0). Its original staging-upload
    // prologue faults on the native scene-context id, so validate the
    // submesh draw arguments and submit the native skin draw instead.
    auto& s=*state;s.require(base);
    auto& driver=*s.runtime.engineDriver;auto& mesh=s.skinMesh;
    auto& record=s.find(s.skinId);
    need(s.skinImmediate.active&&!record.recordingContext&&s.skinImmediate.cpu==&c&&s.skinImmediate.activated&&
         s.skinImmediate.staging.complete,"Skin draw has no completed immediate staging");
    need(mesh.active&&mesh.immediate&&mesh.cpu==&c&&mesh.phase==3&&mesh.cursor<mesh.draws.size()&&
         s.skinMaterial.committed&&!s.skinMaterial.active&&mesh.submeshUpdated,
         "Skin draw has no completed material commit");
    requireSkinSelection(s.skinTyped);
    const auto& row=mesh.draws[mesh.cursor];
    {static thread_local uint32_t skinDrawSample{};
    if(sampleHotLog(skinDrawSample))
        std::fprintf(stderr,"[NATIVE SKIN DRAW] submesh=%08X material=%08X flags=%04X words3..6=%08X %08X %08X %08X context=%08X\n",
            row.entry,row.material,row.flags,row.words[3],row.words[4],row.words[5],row.words[6],c.r3.u32);}
    need(c.r3.u32==mesh.context&&c.r4.u32==row.words[3]&&c.r5.u32==row.words[4]&&
         c.r6.u32==row.words[5]&&c.r7.u32==row.words[6]&&s.skinMaterial.material==row.material&&
         PPC_LOAD_U16(row.materialHeader)==row.flags,"Original skin draw arguments/material differ");
    for(uint32_t j=0;j<9;++j)need(PPC_LOAD_U32(row.entry+4*j)==row.words[j],"Skin submesh changed before draw");
    const auto& effective=driver.effectiveState();const auto camera=driver.cameraBinding();
    need(camera.camera==s.skinCamera,"Skin draw camera owner changed");
    using S=Graphics::ScalarState;Graphics::SkinMeshDraw draw{};
    draw.primitiveType=c.r4.u32;draw.baseVertex=c.r5.s32;draw.startIndex=c.r6.u32;draw.indexCount=c.r7.u32;draw.viewport=camera.viewport;
    if(const auto scissor=s.backend.scissor())draw.scissor=*scissor;
    draw.depthEnable=effective.scalar(S::DepthEnable);draw.depthWrite=effective.scalar(S::DepthWrite);draw.depthCompare=effective.scalar(S::DepthCompare);
    draw.cull=effective.scalar(S::Cull);draw.fill=effective.scalar(S::Fill);draw.colorMask=effective.scalar(S::ColorMask0);
    draw.stencilEnable=effective.scalar(S::StencilEnable);draw.alphaTest=effective.scalar(S::AlphaTest);
    draw.scissorEnable=effective.scalar(S::ScissorEnable);draw.halfPixelOffset=effective.scalar(S::HalfPixelOffset);
    draw.primitiveReset=effective.scalar(S::PrimitiveResetEnable);draw.primitiveResetIndex=effective.scalar(S::PrimitiveResetIndex);
    draw.viewportEnable=effective.scalar(S::ViewportEnable);draw.clipPlaneEnable=effective.scalar(S::ClipPlaneEnable);
    draw.multisampleAntialias=effective.scalar(S::MultisampleAntialias);draw.multisampleMask=effective.scalar(S::MultisampleMask);
    draw.alphaToMask=effective.scalar(S::AlphaToMask);draw.depthBiasBits=effective.scalar(S::DepthBias);draw.slopeBiasBits=effective.scalar(S::SlopeBias);
    draw.depthPolicy=Graphics::ShadowMeshDepthPolicy::Reference20e4Rne;
    draw.blendEnable=effective.scalar(S::BlendEnable);draw.blendWord=effective.effectiveBlend(0);draw.expandedBlend=effective.scalar(S::ExpandedBlend0);
    observeSkinDraw(draw,mesh.geometry,row.entry);
    prepareSkinDrawTextures(draw);
    need(!effective.scalar(S::TessellationMode),"Skin tessellation is unqualified");
    bool alphaOne=false;const auto color=driver.color(camera.colorIdentity,alphaOne);need(!alphaOne,"Skin color owner requires unsupported alpha synthesis");
    s.backend.drawSkinMesh(color,driver.depth(camera.depthIdentity),mesh.native,*s.skinVertex,*s.skinPixel,
        s.skinImmediate.commit,draw);
    ++mesh.cursor;s.skinMaterial.committed=false;mesh.submeshUpdated=false;
    {static thread_local uint32_t skinImmediateDrawSample{};
    if(sampleHotLog(skinImmediateDrawSample))
        std::fprintf(stderr,"[NATIVE SKIN IMMEDIATE DRAW] geometry=%08X submesh=%08X indices=%u completed=%u/%zu\n",
            mesh.geometry,row.entry,draw.indexCount,mesh.cursor,mesh.draws.size());}
    c.lr=0x827018C4;
}
void EngineEffects::skinLoopEnd(PPCContext& c,uint8_t* base) {    // Skin mesh-loop epilogue (8270192C, after the 826FE668 sampler loop).
    // The loop has no auxiliary-stream site (the shared cleanup bl is
    // branched around for skin); all submeshes drew with committed materials.
    (void)c;
    auto& s=*state;s.require(base);auto& mesh=s.skinMesh;
    need(mesh.active&&mesh.immediate&&mesh.cpu==currentContext&&mesh.phase==3&&mesh.cursor==mesh.draws.size()&&
         !s.skinMaterial.active&&!s.skinMaterial.committed&&!mesh.submeshUpdated,
         "Original skin loop ended before its material/draw closure");
    mesh.auxiliaryCleared=true;mesh.active=false;mesh.complete=true;
}
void EngineEffects::skyTextureBind(PPCContext& c,uint8_t* base) {
    // Probe: per-texture bind inside the 8270AD68 transfer helper (bl
    // 824408E0 at 8270AD5C). Logs the call shape, owner resolution and the
    // caller's caller, then stops for exact bind qualification.
    auto& s=*state;s.require(base);
    auto& runtime=*Simpsons::active;
    // Semantic retain stays unconditional: the draw path consumes these ids.
    if(c.r4.u32<4) s.skyStagedTextures[c.r4.u32]=c.r5.u32;
    // Checked owner read stays unconditional (same lookup as before, including
    // its failure behavior); only the diagnostic print is sampled.
    uint32_t texbindRigidSource=0;const bool haveTexbindRigid=s.rigidId!=0;
    if(haveTexbindRigid) texbindRigidSource=s.find(s.rigidId).view.source;
    // Best-effort exploratory probes below consume no semantic results; they
    // run only on a sampled diagnostic pass. The original SDK bind resumes
    // after the call site either way.
    static thread_local uint32_t skyTexbindSample{};
    const bool texbindDiag=sampleHotLog(skyTexbindSample);
    auto peek=[&](uint32_t a)->uint32_t { try { runtime.pointer(a,4,false); } catch(...) { return 0xDEADDEADu; } return PPC_LOAD_U32(a); };
    (void)peek;
    if(texbindDiag)
        std::fprintf(stderr,"[NATIVE SKY TEXBIND] r3=%08X r4=%08X r5=%08X r6=%08X r28=%08X r29=%08X r30=%08X r31=%08X rigid=%08X\n",
            c.r3.u32,c.r4.u32,c.r5.u32,c.r6.u32,c.r28.u32,c.r29.u32,c.r30.u32,c.r31.u32,s.rigidId);
    if(texbindDiag) try {
        s.runtime.pointer(c.r27.u32-14352,32,false);
        std::fprintf(stderr,"[NATIVE SKY TEXBIND] r27=%08X table:",c.r27.u32);
        for(uint32_t i=0;i<32;i+=4)std::fprintf(stderr," %08X",PPC_LOAD_U32(c.r27.u32-14352+i));
        std::fprintf(stderr,"\n");
        const uint32_t sib=PPC_LOAD_U32(c.r27.u32-14352+4);
        try { (void)s.runtime.engineDriver->textureRaster(sib); std::fprintf(stderr,"[NATIVE SKY TEXBIND] sib-raster-hit\n"); } catch(...) {}
        if(s.runtime.engineDriver->itxdTextures().rasterByHeader(base,sib))
            std::fprintf(stderr,"[NATIVE SKY TEXBIND] sib-itxd-hit\n");
        try { bool alpha=false; (void)s.runtime.engineDriver->color(sib,alpha); std::fprintf(stderr,"[NATIVE SKY TEXBIND] sib-color-hit\n"); } catch(...) {}
        try { (void)s.runtime.engineDriver->depth(sib); std::fprintf(stderr,"[NATIVE SKY TEXBIND] sib-depth-hit\n"); } catch(...) {}
    } catch(...) {}
    if(haveTexbindRigid&&texbindDiag)std::fprintf(stderr,"[NATIVE SKY TEXBIND] source=%08X\n",texbindRigidSource);
    if(texbindDiag) try {
        uint32_t frame=c.r1.u32;
        for(unsigned depth=0;depth<4;++depth) {
            runtime.pointer(frame,16,false);
            const uint32_t parent=PPC_LOAD_U32(frame);
            if(parent<=frame||(parent&15)||parent-frame<16||parent-frame>0x10000)break;
            runtime.pointer(parent-8,4,false);
            std::fprintf(stderr,"[NATIVE SKY TEXBIND] frame%u sp=%08X caller_sp=%08X saved_lr=%08X\n",
                depth,frame,parent,PPC_LOAD_U32(parent-8));
            frame=parent;
        }
    } catch(...) {}
    auto& driver=*s.runtime.engineDriver;
    if(texbindDiag) {
        try { (void)driver.depth(c.r5.u32); std::fprintf(stderr,"[NATIVE SKY TEXBIND] depth-hit\n"); } catch(...) {}
        try { bool alpha=false; (void)driver.color(c.r5.u32,alpha); std::fprintf(stderr,"[NATIVE SKY TEXBIND] color-hit\n"); } catch(...) {}
        try { (void)driver.shadowTextures().view(c.r5.u32); std::fprintf(stderr,"[NATIVE SKY TEXBIND] shadow-hit\n"); } catch(...) {}
        try { (void)driver.sampledColorCopy(c.r5.u32,driver.cameraBinding().camera); std::fprintf(stderr,"[NATIVE SKY TEXBIND] sampledcolor-hit\n"); } catch(...) {}
        try { (void)driver.sampledDepthCopy(c.r5.u32,driver.cameraBinding().camera); std::fprintf(stderr,"[NATIVE SKY TEXBIND] sampleddepth-hit\n"); } catch(...) {}
        try { (void)driver.textureRaster(c.r5.u32); std::fprintf(stderr,"[NATIVE SKY TEXBIND] raster-hit\n"); } catch(...) {}
    }
    if(c.r4.u32<4&&texbindDiag) {
        std::fprintf(stderr,"[NATIVE SKY TEXBIND] retained stage=%u id=%08X\n",c.r4.u32,c.r5.u32);
    }
    if(texbindDiag) {
        try { (void)driver.reflectionTextures().view(c.r5.u32); std::fprintf(stderr,"[NATIVE SKY TEXBIND] reflection-hit\n"); } catch(...) {}
        try { (void)driver.reflectionTextures().cube(c.r5.u32); std::fprintf(stderr,"[NATIVE SKY TEXBIND] cube-hit\n"); } catch(...) {}
        try { (void)driver.builtinTextures().view(c.r5.u32); std::fprintf(stderr,"[NATIVE SKY TEXBIND] builtin-hit\n"); } catch(...) {}
        try { (void)driver.builtinTextures().texture(c.r5.u32); std::fprintf(stderr,"[NATIVE SKY TEXBIND] builtintex-hit\n"); } catch(...) {}
    }
    if(texbindDiag) try {
        const uint32_t raster=PPC_LOAD_U32(0x82D0E3F8+24*c.r4.u32);
        std::fprintf(stderr,"[NATIVE SKY TEXBIND] stage raster=%08X\n",raster);
        (void)driver.textureRaster(raster); std::fprintf(stderr,"[NATIVE SKY TEXBIND] stage-raster-hit\n");
    } catch(...) {}
    if(texbindDiag) try {
        uint32_t frame=c.r1.u32;
        runtime.pointer(frame,16,false);
        const uint32_t parent=PPC_LOAD_U32(frame);
        runtime.pointer(parent+80,64,false);
        std::fprintf(stderr,"[NATIVE SKY TEXBIND] staging+80:");
        for(uint32_t i=0;i<64;i+=4)std::fprintf(stderr," %08X",PPC_LOAD_U32(parent+80+i));
        std::fprintf(stderr,"\n");
    } catch(...) {}
    // Diagnostic skip: original SDK bind resumes after the call site.
}
void EngineEffects::skinMorphSkip(PPCContext& c,uint8_t* base) {
    if(state->monoId){monoSkinOperation(c,base,0x826FF47C);return;}
    // Original shared helper clears unused morph streams1..6. The native
    // interleaved upload represents each cleared stream by its zero delta.
    auto& s=*state;s.require(base);
    auto& mesh=s.skinMesh;
    need(mesh.active&&mesh.cpu==currentContext&&s.skinImmediate.active,"Skin morph skip has no active skin mesh");
    s.requireSkinOpaqueContext(c.r3.u32);
    {static thread_local uint32_t skinMorphskipSample{};
    if(sampleHotLog(skinMorphskipSample))
        std::fprintf(stderr,"[NATIVE SKIN MORPHSKIP] r3=%08X mapped=%u r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X\n",
            c.r3.u32,0,c.r4.u32,c.r5.u32,c.r6.u32,c.r7.u32,c.r8.u32);}
    need(c.r5.u32==0&&c.r6.u32==0&&c.r4.u32>=1&&c.r4.u32<=6,
         "Unqualified original skin morph-stream commit call shape");
    need(!mesh.phase&&c.r1.u32==mesh.frame-0xA0&&PPC_LOAD_U32(c.r1.u32)==mesh.frame&&c.r27.u32==mesh.geometry&&
         c.r3.u32==mesh.context&&c.r4.u32==c.r30.u32&&!c.r7.u32&&c.r8.u64==skinMorphStreamDirtyMask(c.r4.u32),
         "Original unused morph stream lost its frame or fetch-group mask");
    const uint32_t bit=1u<<(c.r4.u32-1);
    need(!((mesh.morphBoundMask|mesh.morphClearMask)&bit),"Skin morph stream cleared more than once");
    mesh.morphClearMask|=bit;
}
void EngineEffects::skinSamplerCommitSkip(PPCContext& c,uint8_t* base) {
    if(state->monoId){monoSkinOperation(c,base,0x826FE6CC);return;}
    // Replaces the bl 8243C5C0 at 826FE6CC (resumes at 826FE6D0), reached
    // from the 826FE710 matrix-select helper on skin-shaped calls. Same
    // unmapped-target situation as the sampler skip (r3 is the native
    // scene-context id, r4=1 selects the commit). The qualified native
    // material commit owns consumed sampler state; validate the original
    // call shape here and resume after the unmapped SDK target.
    auto& s=*state;s.require(base);
    auto& mesh=s.skinMesh;
    need(mesh.active&&mesh.cpu==currentContext&&s.skinImmediate.active,"Skin sampler-commit skip has no active skin mesh");
    s.requireSkinOpaqueContext(c.r3.u32);
    {static thread_local uint32_t skinCommitskipSample{};
    if(sampleHotLog(skinCommitskipSample))
        std::fprintf(stderr,"[NATIVE SKIN COMMITSKIP] r3=%08X mapped=%u r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X\n",
            c.r3.u32,0,c.r4.u32,c.r5.u32,c.r6.u32,c.r7.u32,c.r8.u32);}
    need(c.r4.u32>=1&&c.r4.u32<=6&&!c.r5.u32&&!c.r6.u32&&!c.r7.u32&&
         (c.r8.u32==1||c.r8.u32==2||c.r8.u32==4),
         "Unqualified original skin sampler-commit call shape");
}
void EngineEffects::skinMorphStreamBind(PPCContext& c,uint8_t* base) {
    if(state->monoId){monoSkinOperation(c,base,0x826FF434);return;}
    // Replace the stream-source SDK call at826FF434. Original CPU selection
    // and loop order remain intact; r8 is a 64-bit fetch-group dirty mask.
    auto& s=*state;s.require(base);
    auto& mesh=s.skinMesh;
    need(mesh.active&&mesh.cpu==currentContext&&s.skinImmediate.active,"Skin morph stream has no active mesh");
    s.requireSkinOpaqueContext(c.r3.u32);
    {static thread_local uint32_t skinWriteskipSample{};
    if(sampleHotLog(skinWriteskipSample))
        std::fprintf(stderr,"[NATIVE SKIN WRITESKIP] r3=%08X mapped=%u r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X r28=%08X\n",
            c.r3.u32,0,c.r4.u32,c.r5.u32,c.r6.u32,c.r7.u32,c.r8.u32,c.r28.u32);}
    need(c.r3.u32==mesh.context&&c.r4.u32>=1&&c.r4.u32<=c.r28.u32&&c.r28.u32<=6&&
         c.r4.u32==c.r30.u32&&!c.r6.u32&&c.r7.u32==12&&c.r8.u64==skinMorphStreamDirtyMask(c.r4.u32)&&
         !mesh.phase&&c.r1.u32==mesh.frame-0xA0&&PPC_LOAD_U32(c.r1.u32)==mesh.frame&&c.r27.u32==mesh.geometry,
         "Original skin morph stream frame, slot or dirty mask differs");
    const uint32_t bit=1u<<(c.r4.u32-1);
    need(mesh.morphBoundMask==bit-1&&!mesh.morphClearMask,"Skin morph streams are not selected in original order");
    s.runtime.pointer(c.r24.u32,12,false);
    const auto table=PPC_LOAD_U32(c.r24.u32+8);
    need(!(c.r25.u32&3)&&!(c.r10.u32&3)&&uint64_t(table)+c.r25.u32+4<=UINT32_MAX,"Skin morph table offset overflows");
    s.runtime.pointer(table+c.r25.u32,4,false);const auto row=PPC_LOAD_U32(table+c.r25.u32);
    need(uint64_t(row)+c.r10.u32+4<=UINT32_MAX,"Skin selected morph offset overflows");
    s.runtime.pointer(row+c.r10.u32,4,false);
    need(PPC_LOAD_U32(row+c.r10.u32)==c.r5.u32,"Skin morph source differs from original selection");
    s.runtime.pointer(c.r5.u32,32,false);
    const auto addressWord=PPC_LOAD_U32(c.r5.u32+0x18),sizeWord=PPC_LOAD_U32(c.r5.u32+0x1C);
    const uint32_t source=addressWord&~3u,bytes=sizeWord&0x0FFFFFFCu;
    const auto stride=PPC_LOAD_U32(mesh.geometry+4),baseBytes=PPC_LOAD_U32(mesh.geometry);
    {static thread_local uint32_t skinMorphSourceSample{};
    if(sampleHotLog(skinMorphSourceSample))
        std::fprintf(stderr,"[NATIVE SKIN MORPH SOURCE] geometry=%08X stream=%u source=%08X bytes=%u vertex_bytes=%u stride=%u address_word=%08X size_word=%08X\n",
            mesh.geometry,c.r4.u32,source,bytes,baseBytes,stride,addressWord,sizeWord);}
    // A byte owner need not end on its stride boundary. The decoder later
    // proves that every consumed base record has a complete float3 in this
    // independent morph stream; unused source tails remain inert.
    need(validOriginalVertexExtent(baseBytes,stride)&&bytes&&bytes<=originalGeometryMaxVertexBytes&&
         (addressWord&3)==3&&(sizeWord&0xF0000003u)==0x10000002u,
         "Original morph buffer format or extent differs from its skin geometry");
    const auto* data=s.runtime.pointer(source,bytes,false);
    mesh.morphStreams[c.r4.u32-1].assign(data,data+bytes);mesh.morphBoundMask|=bit;
    static uint32_t captures{};
    if(captures<6&&!s.runtime.frameCaptureDirectory.empty()) {
        std::ofstream out(s.runtime.frameCaptureDirectory/("skin-morph-stream-"+std::to_string(captures++)+".bin"),std::ios::binary);
        out.write(reinterpret_cast<const char*>(data),bytes);need(bool(out),"Skin morph snapshot write failed");
    }
}
void SimpsonsNativeSkinSamplerWatch(PPCContext& c,uint8_t* b) {
    // Observe-only: logs every sampler-commit invocation with its call-site
    // lr so further skin shapes can be given narrow bl-site skips.
    HostState fp;effects(b);
    auto& runtime=*Simpsons::active;
    uint32_t mapped=0;
    try { runtime.pointer(c.r3.u32,4,false); mapped=1; } catch(...) {}
    std::fprintf(stderr,"[NATIVE SKIN WATCH] lr=%08X r3=%08X mapped=%u r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X\n",
        uint32_t(c.lr),c.r3.u32,mapped,c.r4.u32,c.r5.u32,c.r6.u32,c.r7.u32,c.r8.u32);
}
void EngineEffects::skinSamplerSkip(PPCContext& c,uint8_t* base) {    // Replaces the bl 8243C5C0 at 826FF3B4 (resumes at 826FF3B8). Census shows
    // only skin-shaped calls reach this bl (rigid/zprepass/normalmap take
    // early exits upstream); anything else fails fast here instead of running
    // the original sampler commit.
    if(state->monoId){monoMeshOperation(c,base,0x826FF3B4);return;}
    auto& s=*state;s.require(base);
    auto& mesh=s.skinMesh;
    need(mesh.active&&mesh.cpu==currentContext&&s.skinImmediate.active,"Skin sampler skip has no active skin mesh");
    s.requireSkinOpaqueContext(c.r3.u32);
    {static thread_local uint32_t skinSkipSample{};
    if(sampleHotLog(skinSkipSample))
        std::fprintf(stderr,"[NATIVE SKIN SKIP] r3=%08X mapped=%u r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X\n",
            c.r3.u32,0,c.r4.u32,c.r5.u32,c.r6.u32,c.r7.u32,c.r8.u32);}
    // 826FF394..3B4 reads the stride from this owned geometry and binds
    // its +38 descriptor.8243C6A8/B4 stores the stride in one DWORD-count
    // byte; neither this producer nor SDK restricts it to observed48/56.
    s.runtime.pointer(mesh.geometry,0x78,false);
    const auto stride=PPC_LOAD_U32(mesh.geometry+4);
    need(c.r4.u32==0&&c.r6.u32==0&&c.r8.u32==1&&
         c.r27.u32==mesh.geometry&&c.r5.u32==mesh.geometry+0x38&&
         c.r7.u32==stride&&stride&&stride<=1020&&!(stride&3),
         "Unqualified original skin sampler-commit call shape");
    s.runtime.pointer(c.r5.u32,64,false);
}
void EngineEffects::skinStreamSetup(PPCContext& c,uint8_t* base) {
    // Replaces the bl 826FF250 inside the skin mesh loop (resumes at
    // 82701664). The shared helper would commit sampler state into the native
    // scene-context id and fault; native material commits own sampled textures and carry
    // morphs in its own vertex buffer, so replicate only the OUT registers
    // (r23 descriptor, r31 scene-context slot) with read-only guest reads.
    auto& s=*state;s.require(base);
    auto& runtime=*Simpsons::active;
    auto& mesh=s.skinMesh;
    need(mesh.active&&mesh.cpu==currentContext&&s.skinImmediate.active,"Skin stream setup has no active skin mesh");
    std::fprintf(stderr,"[NATIVE SKIN STREAMSETUP] r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X meta=%08X object=%08X\n",
        c.r3.u32,c.r4.u32,c.r5.u32,c.r6.u32,c.r7.u32,c.r8.u32,mesh.metadata,mesh.object);
    need(c.r3.u32==mesh.metadata&&c.r4.u32==mesh.object&&!c.r5.u32&&!c.r6.u32&&c.r7.u32==1&&c.r8.u32==1,
         "Unqualified original skin stream-setup call shape");
    const uint32_t geometry=PPC_LOAD_U32(mesh.metadata+0xC);
    need(geometry==mesh.geometry,"Skin stream setup lost its mesh geometry");
    auto rd=[&](uint32_t address)->uint32_t {
        runtime.pointer(address,4,false);return PPC_LOAD_U32(address);
    };
    const uint32_t table=rd(0x82CF0000u+1516);
    const uint32_t r10=table+c.r4.u32;
    const uint32_t descr=rd(geometry+48);
    uint32_t bound=0;
    {
        auto guarded=[&](uint32_t address)->uint32_t {
            if(!address) return 0;
            try { runtime.pointer(address,4,false); } catch(...) { return 0xDEADDEADu; }
            return PPC_LOAD_U32(address);
        };
        const uint32_t row=r10?guarded(r10):0;
        const uint32_t flagBase=row?guarded(c.r4.u32+24):0;
        const uint32_t flagWord=(flagBase&&flagBase!=0xDEADDEADu)?guarded(flagBase+8):0;
        const uint32_t cand4=guarded(descr+4),cand8=guarded(descr+8);
        std::fprintf(stderr,"[NATIVE SKIN STREAMSETUP] table=%08X r10=%08X row=%08X flagBase=%08X flagWord=%08X cand4=%08X cand8=%08X cand4_36=%08X cand4_52=%08X cand8_36=%08X cand8_52=%08X\n",
            table,r10,row,flagBase,flagWord,cand4,cand8,
            cand4?guarded(cand4+36):0,cand4?guarded(cand4+52):0,
            cand8?guarded(cand8+36):0,cand8?guarded(cand8+52):0);
    }
    if(r10==0) bound=rd(descr+4);
    else {
        const uint32_t row=rd(r10);
        if(row==0) bound=rd(descr+4);
        else {
            const uint32_t flagBase=rd(c.r4.u32+24),flagWord=rd(flagBase+8);
            bound=(flagWord&0x04000000u)?rd(descr+8):rd(descr+4);
        }
    }
    std::fprintf(stderr,"[NATIVE SKIN STREAMSETUP] geometry=%08X descr=%08X bound=%08X\n",geometry,descr,bound);
    need(bound,"Skin stream setup resolved a null descriptor");
    {
        // Identify the bound object: a declaration holds 13 twelve-byte rows
        // ending in the 00FF0000/FFFFFFFF terminator.
        auto peek=[&](uint32_t address)->uint32_t {
            try { runtime.pointer(address,4,false); } catch(...) { return 0xDEADDEADu; }
            return PPC_LOAD_U32(address);
        };
        const uint32_t other=rd(descr+8);
        std::fprintf(stderr,"[NATIVE SKIN STREAMSETUP] cand4 head=%08X %08X %08X tail=%08X %08X %08X cand8=%08X head=%08X %08X %08X tail=%08X %08X %08X\n",
            peek(bound),peek(bound+4),peek(bound+8),peek(bound+144),peek(bound+148),peek(bound+152),
            other,peek(other),peek(other+4),peek(other+8),peek(other+144),peek(other+148),peek(other+152));
    }
    // Mirror the callee OUT state the body after the call observes: r23 is
    // volatile and always set by the helper (bound descriptor). r31/r30 are
    // callee-saved nonvolatiles the helper preserves (entry r6=0 keeps r31
    // zero for the downstream add/clamp; entry r7=1 keeps the morph index),
    // so they are deliberately left untouched here.
    c.r23.u64=bound;
}
void SimpsonsNativeSkinStreamProbe(PPCContext& c,uint8_t* b) {
    HostState fp;effects(b);
    auto& runtime=*Simpsons::active;
    // Observe-only: the original body runs afterward for all flows.
    if(c.r3.u32) { runtime.pointer(c.r3.u32,0x40,false); }
    if(c.r4.u32) { runtime.pointer(c.r4.u32,0x40,false); }
}
void SimpsonsNativeRigidImmediateMeshDraw(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidMeshOperation(c,b,0x827013B0);}
void SimpsonsNativeRigidImmediateMeshCleanup(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidMeshOperation(c,b,0x8270142C);}
void SimpsonsNativeRigidImmediateMeshComplete(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidMeshOperation(c,b,0x82701438);}
void SimpsonsNativeRigidImmediateSubmeshEntry(PPCContext& c,uint8_t* b){
    if(uint32_t(c.lr)==0x82701310){HostState fp;effects(b).rigidImmediateOperation(c,b,0x82700498);}
    else if(uint32_t(c.lr)==0x827017DC){HostState fp;effects(b).skinImmediateOperation(c,b,0x82700498);}
}
void SimpsonsNativeRigidImmediateSubmeshComplete(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidImmediateOperation(c,b,0x827005DC);}
void SimpsonsNativeRigidImmediateMaterialEntry(PPCContext& c,uint8_t* b){
    if(uint32_t(c.lr)==0x8270131C){HostState fp;effects(b).rigidMaterialOperation(c,b,0x826B54D0);}
    else if(uint32_t(c.lr)==0x8270182C){HostState fp;effects(b).skinMaterialOperation(c,b,0x826B54D0);}
}
void SimpsonsNativeRigidImmediateTextures_8273FF80(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidTextureOperation(c,b,0x8273FF80);}
void SimpsonsNativeRigidImmediateTextures_8273FFA4(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidTextureOperation(c,b,0x8273FFA4);}
void SimpsonsNativeRigidImmediateTextures_82740044(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidTextureOperation(c,b,0x82740044);}
void SimpsonsNativeRigidImmediateTextures_82740060(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidTextureOperation(c,b,0x82740060);}
void SimpsonsNativeRigidImmediateTextures_827400E0(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidTextureOperation(c,b,0x827400E0);}
void SimpsonsNativeRigidImmediateTextures_827400F0(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidTextureOperation(c,b,0x827400F0);}
void SimpsonsNativeRigidMeshComplete(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidMeshOperation(c,b,0x82701628);}
void SimpsonsNativeRigidMeshDraw(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidMeshOperation(c,b,0x827015C0);}
void SimpsonsNativeRigidMaterialEntry(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidMaterialOperation(c,b,0x826B5618);}
void SimpsonsNativeRigidMaterialFilter(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidMaterialOperation(c,b,0x826B2F20);}
void SimpsonsNativeRigidMaterialCommit(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidMaterialOperation(c,b,0x82C1DBA0);}
void SimpsonsNativeRigidParameter_8270BAB0(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidParameterOperation(c,b,0x8270BAB0);}
void SimpsonsNativeRigidParameter_8270BB50(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidParameterOperation(c,b,0x8270BB50);}
void SimpsonsNativeRigidParameter_8270BBEC(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidParameterOperation(c,b,0x8270BBEC);}
void SimpsonsNativeRigidParameter_8270BC90(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidParameterOperation(c,b,0x8270BC90);}
void SimpsonsNativeRigidParameter_8270BCB0(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidParameterOperation(c,b,0x8270BCB0);}
void SimpsonsNativeRigidParameter_8270BD38(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidParameterOperation(c,b,0x8270BD38);}
void SimpsonsNativeRigidParameter_8270BD58(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidParameterOperation(c,b,0x8270BD58);}
void SimpsonsNativeRigidParameter_8270BDDC(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidParameterOperation(c,b,0x8270BDDC);}
void SimpsonsNativeRigidParameter_8270BE7C(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidParameterOperation(c,b,0x8270BE7C);}
void SimpsonsNativeRigidReplayShared(PPCContext& c,uint8_t* b){
    if(uint32_t(c.lr)==0x82740328){HostState fp;effects(b).rigidReplayOperation(c,b,0x826B6068);}
}
void SimpsonsNativeRigidReplayCallbacks(PPCContext& c,uint8_t* b){
    if(uint32_t(c.lr)==0x82740B94){HostState fp;effects(b).monoOperation(c,b,0x826B5770);return;}
    if(uint32_t(c.lr)==0x8274033C||uint32_t(c.lr)==0x82740198){HostState fp;effects(b).rigidReplayOperation(c,b,0x826B5770);}
}
void SimpsonsNativeRigidReplayUpload(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidReplayOperation(c,b,0x826B3270);}
void SimpsonsNativeRigidReplayStage_826B32E4(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidReplayOperation(c,b,0x826B32E4);}
void SimpsonsNativeRigidReplayStage_826B3330(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidReplayOperation(c,b,0x826B3330);}
void SimpsonsNativeRigidReplayCopied_826B3314(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidReplayOperation(c,b,0x826B3314);}
void SimpsonsNativeRigidReplayCopied_826B3364(PPCContext& c,uint8_t* b){HostState fp;effects(b).rigidReplayOperation(c,b,0x826B3364);}
void SimpsonsNativeEdgeCommit(PPCContext& c,uint8_t* b){HostState fp;effects(b).commitEdge(c,b);}
void SimpsonsNativeAAKernel(PPCContext& c,uint8_t* b){HostState fp;effects(b).aaParameter(c,b,0x823C8380);}
void SimpsonsNativeAAColorA(PPCContext& c,uint8_t* b){HostState fp;effects(b).aaParameter(c,b,0x823C8404);}
void SimpsonsNativeAAColorB(PPCContext& c,uint8_t* b){HostState fp;effects(b).aaParameter(c,b,0x823C8414);}
void SimpsonsNativeAAColorC(PPCContext& c,uint8_t* b){HostState fp;effects(b).aaParameter(c,b,0x823C8454);}
void SimpsonsNativeAAWidth(PPCContext& c,uint8_t* b){HostState fp;effects(b).aaParameter(c,b,0x823C84C4);}
void SimpsonsNativeAAHeightA(PPCContext& c,uint8_t* b){HostState fp;effects(b).aaParameter(c,b,0x823C8548);}
void SimpsonsNativeAAHeightB(PPCContext& c,uint8_t* b){HostState fp;effects(b).aaParameter(c,b,0x823C8550);}
void SimpsonsNativeAAHeightC(PPCContext& c,uint8_t* b){HostState fp;effects(b).aaParameter(c,b,0x823C8560);}
void SimpsonsNativeAAHeightD(PPCContext& c,uint8_t* b){HostState fp;effects(b).aaParameter(c,b,0x823C8568);}
void SimpsonsNativeEdgeDeclaration(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeRectangle(c,b,0x823CA46C);}
void SimpsonsNativeEdgeVertices(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeRectangle(c,b,0x823CA480);}
void SimpsonsNativeEdgeDraw(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeRectangle(c,b,0x823CA4F0);}
void SimpsonsNativeEdgeEnd(PPCContext& c,uint8_t* b){HostState fp;effects(b).endEdge(c,b,true);}
void SimpsonsNativeEffectManagerEnd(PPCContext& c,uint8_t* b){HostState fp;effects(b).endEdge(c,b,false);}
void SimpsonsNativeEffectPoolCreated(PPCContext& ctx,uint8_t* base) {
    HostState fp;need(active && active->base==base,"Invalid original FX pool publication runtime");
    if(ctx.r3.u32) return;
    need(ctx.r28.u32==0x82D6D2F8 && ctx.r31.u32==PPC_LOAD_U32(0x82D6D2F8) && !active->effectPoolRoot && !active->effectPoolBacking,
         "Unqualified original FX pool publication");
    emptyPool(*active,ctx.r31.u32);
    active->effectPoolRoot=ctx.r31.u32;active->effectPoolThread=GetCurrentThreadId();
}
void SimpsonsNativeEffectPoolRetire(PPCContext& ctx,uint8_t* base) {
    HostState fp;need(active && active->base==base,"Invalid original FX pool retirement runtime");
    uint32_t pool=ctx.r3.u32;
    if(ctx.lastFunction==0x82CC1820) pool=PPC_LOAD_U32(0x82D6D2F8);
    else if(ctx.lastFunction==0x82722568) {active->pointer(pool,4,false);pool=PPC_LOAD_U32(pool);}
    else need(ctx.lastFunction==0x82C1CF00,"Unqualified original FX pool release entry");
    if(pool && pool==active->effectPoolRoot) {
        need(active->effectPoolThread==GetCurrentThreadId(),"Original FX pool retired on another thread");
        active->pointer(pool,0x18C,false);
        need(PPC_LOAD_U32(pool+0x188)>0,"Original FX pool release has no live CPU reference");
        // A real retained-reference release does not retire or invalidate P.
        if(ctx.lastFunction==0x82C1CF00 && PPC_LOAD_U32(pool+0x188)>1) return;
        if(active->engineDriver) active->engineDriver->preflightEffectPoolRetire(base,pool);
        // Direct CRT heap free does not release populated P+180. Stop before it;
        // the genuine final CF00 release owns both aligned CPU allocations.
        need(!active->effectPoolBacking || ctx.lastFunction==0x82C1CF00,
             "Populated original FX pool requires qualified shared-allocation teardown");
        active->effectPoolRoot=0;active->effectPoolThread=0;active->effectPoolBacking=0;
    }
}

void SimpsonsNativeEdgeAA_823C9A9C(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823C9A9C);}

void SimpsonsNativeEdgeAA_823C9B28(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823C9B28);}

void SimpsonsNativeEdgeAA_823C9B30(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823C9B30);}

void SimpsonsNativeEdgeAA_823C9BB0(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823C9BB0);}

void SimpsonsNativeEdgeAA_823C9C40(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823C9C40);}

void SimpsonsNativeEdgeAA_823C9CCC(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823C9CCC);}

void SimpsonsNativeEdgeAA_823C9D5C(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823C9D5C);}

void SimpsonsNativeEdgeAA_823C9DF0(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823C9DF0);}

void SimpsonsNativeEdgeAA_823C9E00(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823C9E00);}

void SimpsonsNativeEdgeAA_823C9E80(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823C9E80);}

void SimpsonsNativeEdgeAA_823C9E88(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823C9E88);}

void SimpsonsNativeEdgeAA_823C9EB4(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823C9EB4);}

void SimpsonsNativeEdgeAA_823C9EC4(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823C9EC4);}

void SimpsonsNativeEdgeAA_823C9F1C(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823C9F1C);}

void SimpsonsNativeEdgeAA_823C9F24(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823C9F24);}

void SimpsonsNativeEdgeAA_823C9F9C(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823C9F9C);}

void SimpsonsNativeEdgeAA_823C9FB4(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823C9FB4);}

void SimpsonsNativeEdgeAA_823CA03C(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823CA03C);}

void SimpsonsNativeEdgeAA_823CA044(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823CA044);}

void SimpsonsNativeEdgeAA_823CA0B4(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAParameter(c,b,0x823CA0B4);}

void SimpsonsNativeEdgeAA_823C8178(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAHelper(c,b,false);}

void SimpsonsNativeEdgeAA_823C8208(PPCContext& c,uint8_t* b){HostState fp;effects(b).edgeAAHelper(c,b,true);}

void SimpsonsNativeMonoWorldEntry(PPCContext& c,uint8_t* b){HostState fp;effects(b).monoOperation(c,b,0x8273A878);}
void SimpsonsNativeMonoSkinEntry(PPCContext& c,uint8_t* b){HostState fp;effects(b).monoSkinOperation(c,b,0x82700318);}
void SimpsonsNativeMonoSkinBones(PPCContext& c,uint8_t* b){HostState fp;effects(b).monoSkinOperation(c,b,0x827003A0);}
void SimpsonsNativeMonoSkinGroupBones(PPCContext& c,uint8_t* b){HostState fp;effects(b).monoSkinOperation(c,b,0x82700440);}
void SimpsonsNativeMonoSkinDraw(PPCContext& c,uint8_t* b){HostState fp;effects(b).monoSkinOperation(c,b,0x82700470);}
void SimpsonsNativeMonoSkinComplete(PPCContext& c,uint8_t* b){HostState fp;effects(b).monoSkinOperation(c,b,0x8270048C);}
