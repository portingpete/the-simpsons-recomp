#include "engine_rasters.h"
#include "engine_cpu_calls.h"
#include "engine_driver.h"
#include "guest_chain.h"
#include "guest_read_memo.h"
#include "renderer/native_backend.h"
#include <cstdio>
#include <atomic>
#include <cstring>
#include <unordered_map>
#include <optional>
// Keep this last: shared header bodies retain their normal definitions.
#include "aot_inline_memory.h"

namespace Simpsons {
namespace {
std::atomic<uint32_t> nextTexture{0x00800001};
uint32_t checkedWord(const uint8_t* bytes) {
    uint32_t v{};std::memcpy(&v,bytes,sizeof(v));
    return __builtin_bswap32(v);
}
bool privateSize(uint32_t width,uint32_t height) {
    return (width==height && (width==16 || width==256 || width==1024)) ||
        (height==720 && (width==1280 || width==640));
}
bool fullSizeWorkingView(uint32_t width,uint32_t height) {
    return width==1280 && height==720;
}
void snapshotNeed(bool ok,const char* why){if(!ok)throw Failure(std::string("Native crossfade snapshot: ")+why);}
uint32_t textureIdentity(Runtime& runtime) {
    uint32_t value=nextTexture.load();
    while(value<0x00900000) if(nextTexture.compare_exchange_weak(value,value+1)) {
        if(runtime.pageAccess[value>>12].load()) throw Failure("Native texture identity overlaps guest memory");
        return value;
    }
    throw Failure("Native texture identity space exhausted");
}
}
struct EngineRasters::State {
    Runtime& runtime;
    Graphics::NativeBackend& backend;
    const uint32_t thread,engine,pool,colorId,depthId;
    std::shared_ptr<Graphics::RenderTarget> color;
    std::shared_ptr<Graphics::DepthTarget> depth;
    struct Record {
        uint32_t extension,type,node,total,width,height,textureId{};
        std::shared_ptr<Graphics::Texture> texture;
        uint32_t surfaceId{};
        std::shared_ptr<Graphics::RenderTarget> ownedColor;
        std::shared_ptr<Graphics::DepthTarget> ownedDepth;
        uint32_t snapshotOwner{};
        bool snapshotFinished{},snapshotReady{};
        uint32_t videoOwner{},videoFrame{},videoIndex{},videoStack{};
        uint32_t videoStaging{},videoPitch{},videoContext{},videoPresenter{};
        bool videoPublished{},videoLocked{},videoInitialized{},videoHasLock{};
    };
    std::unordered_map<uint32_t,Record> records;
    struct Snapshot {uint32_t owner,stack,width,height,raster;uint32_t videoFrame{},videoIndex{};};
    std::optional<Snapshot> snapshot;
    struct PlaneAccess {uint32_t raster,stack,caller;bool unlock;uint32_t presenter{};};
    std::optional<PlaneAccess> planeAccess;
    // Exact memos of the two read-only guest walks every camera validation repeats: the raster
    // plugin registry chain and the live raster list. Each is valid only while no page it read
    // has been written (see guest_read_memo.h); their results are re-derived from the cached walk.
    mutable GuestReadMemo extensionMemo,listMemo;
    mutable uint32_t extensionOffset{},extensionTotal{};
    mutable std::vector<std::pair<uint32_t,uint32_t>> listEntries;
    // Built with listEntries: raster -> (node of its first entry, number of entries), so a memo
    // hit answers the per-node matching below in O(1) instead of rescanning every entry.
    struct ListOccurrence {uint32_t first{},count{};};
    mutable std::unordered_map<uint32_t,ListOccurrence> listIndex;
    // Bumped on entry to every mutating EngineRasters operation, so no record can have changed
    // while it is unchanged.
    uint64_t recordsVersion=1;
    // Exact memo of cameraSurfaces() for a color/depth root pair: valid while the armed raster
    // allocations are unwritten (and their permissions unchanged), the registry and list memos
    // hold, the records version, the plugin offset/total globals and the shared working targets
    // are the same. requireOwner() and its globals are checked on every call regardless.
    struct SurfacesKey {
        uint32_t color{},colorType{},depth{},offset{},total{};
        uint64_t records{},extensionProof{},listProof{};
        const void* colorTarget{};const void* depthTarget{};
        uint32_t colorWidth{},colorHeight{},depthWidth{},depthHeight{};
        bool operator==(const SurfacesKey&) const=default;
    };
    mutable GuestReadMemo surfacesMemo;
    mutable SurfacesKey surfacesKey{};
    mutable std::array<NativeCameraSurfaceRole,2> surfacesResult{};
    // Set only while cameraSurfaces() proves a new memo: validateMetadata arms each raster
    // allocation it validates before reading it.
    mutable GuestReadMemo* recordingSurfaces=nullptr;
    State(Runtime& rt,Graphics::NativeBackend& graphics,uint32_t c,uint32_t d,std::shared_ptr<Graphics::RenderTarget> ct,std::shared_ptr<Graphics::DepthTarget> dt):
        runtime(rt),backend(graphics),thread(GetCurrentThreadId()),engine(PPCLoadU32(rt.base,0x82D0CA68)),
        pool(PPCLoadU32(rt.base,0x82D0D020)),colorId(c),depthId(d),color(std::move(ct)),depth(std::move(dt)) {
        extensionMemo.setWatch(rt.writeWatch());listMemo.setWatch(rt.writeWatch());surfacesMemo.setWatch(rt.writeWatch());
        surfacesMemo.setName("camera-surfaces");
        if(!pool || !color || !depth || color->format!=Graphics::TargetFormat::RGB10A2 ||
           color->width!=depth->pixelWidth() || color->height!=depth->pixelHeight())
            throw Failure("Native camera raster owner lacks matching original target roles/pool");
    }
    void requireOwner(uint8_t* base) const {
        if(active!=&runtime || base!=runtime.base || thread!=GetCurrentThreadId())
            throw Failure("Native camera raster accessed outside its runtime/thread owner");
        runtime.checkRunning();
        if(PPC_LOAD_U32(0x82D0CA68)!=engine || PPC_LOAD_U32(0x82D0D020)!=pool ||
           PPC_LOAD_U32(0x82D0CB00)!=colorId || PPC_LOAD_U32(0x82D0CAFC)!=depthId ||
           !PPC_LOAD_U32(engine+0x138) || !PPC_LOAD_U32(engine+0x13C))
            throw Failure("Native camera raster driver/pool/target owner changed");
    }
    uint32_t extension(uint8_t* base,uint32_t raster) const {
        const uint32_t offset=PPC_LOAD_U32(0x82E3DC94),total=PPC_LOAD_U32(0x82CD1E28);
        if(!raster || (raster&3) || offset<0x34 || (offset&3) || uint64_t(offset)+0x20>total ||
           uint64_t(raster)+total>0x100000000ull)
            throw Failure("Invalid native camera raster plugin/allocation bounds");
        runtime.probe(raster,total,true);
        // The registry chain does not depend on the raster: reuse its last successful walk while
        // none of the pages it read has been written and the inputs read above are unchanged.
        if(extensionMemo.valid() && extensionOffset==offset && extensionTotal==total) {
            if(GuestReadMemo::verify()) {
                // Diagnostic (SIMPSONS_WATCH_VERIFY=1): a fresh walk must reach the same answer.
                extensionMemo.invalidate();
                try {if(extension(base,raster)!=raster+offset)throw Failure("registry offset changed");}
                catch(const Failure& error) {
                    std::fprintf(stderr,"[READ MEMO MISMATCH] plugin registry cached valid but a fresh walk failed: %s\n",error.what());
                    std::fflush(stderr);std::abort();
                }
            }
            return raster+offset;
        }
        extensionMemo.begin();
        extensionMemo.arm(runtime.probe(0x82CD1E38,4,false),4);
        extensionMemo.arm(runtime.probe(0x82CD1E3C,4,false),4);
        uint32_t at=PPC_LOAD_U32(0x82CD1E38),previous=0;GuestChainWalk<256> walk;bool found=false;
        while(at) {
            if(!walk.visit(at)) throw Failure("Invalid raster plugin registry chain");
            // Validate this entire node once; volatile reads retain live guest
            // values without rechecking the same mapping for every field.
            const auto* node=runtime.probe(at,0x3C,false);
            extensionMemo.arm(node,0x3C);
            if(checkedWord(node+0x34)!=previous || checkedWord(node+0x38)!=0x82CD1E28)
                throw Failure("Original raster plugin reverse chain/registry ownership is inconsistent");
            if(checkedWord(node+8)==0x40C) {
                if(found || checkedWord(node)!=offset || checkedWord(node+4)!=0x20 ||
                   checkedWord(node+0x20)!=0x823F52B8 || checkedWord(node+0x24)!=0x823F52C0)
                    throw Failure("Unverified native raster plugin registration");
                found=true;
            }
            previous=at;at=checkedWord(node+0x30);
        }
        if(!found || previous!=PPC_LOAD_U32(0x82CD1E3C)) throw Failure("Original raster plugin 040C or registry tail is invalid");
        extensionOffset=offset;extensionTotal=total;extensionMemo.commit();
        return raster+offset;
    }
    struct ListMatch {uint32_t node{};bool duplicate{};};
    template<size_t N> std::array<ListMatch,N> listNodes(uint8_t* base,const std::array<uint32_t,N>& rasters) const {
        std::array<ListMatch,N> found{};
        const auto match=[&](uint32_t raster,uint32_t at) {
            for(size_t i=0;i<N;++i)if(raster==rasters[i]) {
                if(found[i].node) {
                    // The second role is checked after its metadata, preserving
                    // the original color-first failure order without another walk.
                    if(i==0)throw Failure("Duplicate original raster list entry");
                    found[i].duplicate=true;
                } else found[i].node=at;
            }
        };
        // Reuse the last successful walk of the live raster list while none of the pages it read
        // has been written; the lookup below is the original per-node matching over its entries.
        if(listMemo.valid()) {
            if(GuestReadMemo::verify()) {
                // Diagnostic (SIMPSONS_WATCH_VERIFY=1): the cached walk must equal a fresh one.
                std::vector<std::pair<uint32_t,uint32_t>> fresh;uint32_t cursor=PPC_LOAD_U32(0x82D0D01C);GuestChainWalk<65536> check;
                while(cursor) {
                    if(!check.visit(cursor))break;
                    const auto* node=runtime.probe(cursor,8,true);fresh.emplace_back(checkedWord(node),cursor);cursor=checkedWord(node+4);
                }
                if(fresh!=listEntries) {
                    std::fprintf(stderr,"[READ MEMO MISMATCH] raster list cached=%zu fresh=%zu\n",listEntries.size(),fresh.size());
                    for(size_t i=0;i<std::max(fresh.size(),listEntries.size())&&i<8;++i)
                        std::fprintf(stderr,"[READ MEMO MISMATCH]  #%zu cached=%08X/%08X fresh=%08X/%08X\n",i,
                            i<listEntries.size()?listEntries[i].first:0,i<listEntries.size()?listEntries[i].second:0,
                            i<fresh.size()?fresh[i].first:0,i<fresh.size()?fresh[i].second:0);
                    std::fflush(stderr);std::abort();
                }
            }
            // Same answer as matching every cached entry in list order: a requested raster's
            // first entry is its node, and a second entry is the duplicate (fatal for color).
            for(size_t i=0;i<N;++i) {
                const auto it=listIndex.find(rasters[i]);
                if(it==listIndex.end())continue;
                if(it->second.count>1 && i==0)throw Failure("Duplicate original raster list entry");
                found[i].node=it->second.first;found[i].duplicate=it->second.count>1;
            }
            return found;
        }
        listMemo.begin();listEntries.clear();listIndex.clear();
        listMemo.arm(runtime.probe(0x82D0D01C,4,true),4);
        uint32_t at=PPC_LOAD_U32(0x82D0D01C);GuestChainWalk<65536> walk;
        while(at) {
            if(!walk.visit(at)) throw Failure("Invalid original raster list chain");
            const auto* node=runtime.probe(at,8,true);
            listMemo.arm(node,8);
            const auto raster=checkedWord(node);
            listEntries.emplace_back(raster,at);
            match(raster,at);
            at=checkedWord(node+4);
        }
        for(const auto& [raster,node]:listEntries) {
            auto& occurrence=listIndex[raster];
            if(!occurrence.count++)occurrence.first=node;
        }
        listMemo.commit();
        return found;
    }
    uint32_t listNode(uint8_t* base,uint32_t raster) const {
        return listNodes(base,std::array{raster})[0].node;
    }
    void validateMetadata(uint8_t* base,uint32_t r,const Record& record) const {
        const uint32_t x=extension(base,r),type=record.type;
        if(record.videoOwner) {
            const bool finished=record.snapshotFinished;
            snapshotNeed(!type&&x==record.extension&&PPC_LOAD_U32(0x82CD1E28)==record.total&&PPC_LOAD_U32(r)==r&&
                PPC_LOAD_U32(r+0xC)==record.width&&PPC_LOAD_U32(r+0x10)==record.height&&PPC_LOAD_U32(r+0x14)==8&&
                !PPC_LOAD_U32(r+8)&&!PPC_LOAD_U32(r+0x1C)&&!PPC_LOAD_U8(r+0x20)&&
                PPC_LOAD_U8(r+0x21)==(finished?0:0x80)&&PPC_LOAD_U8(r+0x23)==(finished?0:4)&&
                PPC_LOAD_U32(x)==(finished?record.textureId:0)&&!PPC_LOAD_U32(x+4)&&PPC_LOAD_U32(x+8)==(record.videoHasLock?0u:0xFFu)&&
                !PPC_LOAD_U32(x+0xC)&&PPC_LOAD_U32(x+0x18)==(finished?0x28000002u:0),"movie plane raster metadata changed");
            snapshotNeed(PPC_LOAD_U32(r+4)==(record.videoLocked?record.videoStaging:0)&&
                PPC_LOAD_U8(r+0x22)==(record.videoLocked?4:0),"movie plane lock fields changed");
            if(record.videoHasLock) {
                snapshotNeed(record.videoPitch>=record.width&&PPC_LOAD_U32(r+0x18)==(record.videoLocked?record.videoPitch:0)&&
                    PPC_LOAD_U32(r+0x28)==record.width&&PPC_LOAD_U32(r+0x2C)==record.height&&
                    PPC_LOAD_U32(x+0x10)==record.videoPitch&&PPC_LOAD_U32(x+0x14)==record.videoStaging,
                    "movie plane stable pointer/pitch changed");
                runtime.pointer(record.videoStaging,record.videoPitch*record.height,false);
            }
            runtime.pointer(record.videoOwner,8,false);runtime.pointer(record.videoFrame,0x54,false);
            snapshotNeed(PPC_LOAD_U32(record.videoOwner)==0x8215D4F0&&PPC_LOAD_U32(record.videoOwner+4)&&
                PPC_LOAD_U32(record.videoFrame)==0x37047734&&PPC_LOAD_U32(record.videoFrame+0x38)==3,
                "movie plane original provider/frame owner changed");
            if(finished) {
                snapshotNeed(record.texture&&record.textureId&&!runtime.pageAccess[record.textureId>>12].load()&&
                    record.texture->format==Graphics::TextureFormat::R8&&record.texture->width==record.width&&
                    record.texture->height==record.height&&record.texture->levelCount()==1,"movie plane native storage differs");
                backend.validateTextureStorage(record.texture);
            }
            if(record.videoPublished) {
                const uint32_t frame=record.videoFrame,index=record.videoIndex;
                runtime.pointer(record.videoContext,12,false);
                snapshotNeed(PPC_LOAD_U32(frame+0x44+4*index)==record.videoContext&&
                    PPC_LOAD_U32(record.videoContext)==r&&PPC_LOAD_U32(record.videoContext+4)==record.videoStaging&&
                    PPC_LOAD_U32(record.videoContext+8)==record.videoPitch&&PPC_LOAD_U32(frame+4+4*index)==record.videoStaging&&
                    PPC_LOAD_U32(frame+0x10+4*index)==record.width*record.height&&
                    PPC_LOAD_U32(frame+0x1C+4*index)==record.videoPitch&&!PPC_LOAD_U32(frame+0x30)&&
                    PPC_LOAD_U32(frame+0x28)==record.width*(index?2:1)&&
                    PPC_LOAD_U32(frame+0x2C)==record.height*(index?2:1),"movie plane original publication changed");
            }
            return;
        }
        if(record.snapshotOwner) {
            const bool finished=record.snapshotFinished;const uint32_t owner=record.snapshotOwner;
            snapshotNeed(!type&&x==record.extension&&PPC_LOAD_U32(0x82CD1E28)==record.total&&PPC_LOAD_U32(r)==r&&
                PPC_LOAD_U32(r+0xC)==record.width&&PPC_LOAD_U32(r+0x10)==record.height&&PPC_LOAD_U32(r+0x14)==32&&
                !PPC_LOAD_U32(r+4)&&!PPC_LOAD_U32(r+8)&&!PPC_LOAD_U32(r+0x1C)&&PPC_LOAD_U32(r+0x20)==(finished?0u:0x00800005u)&&
                PPC_LOAD_U32(x)==(finished?record.textureId:0)&&!PPC_LOAD_U32(x+4)&&PPC_LOAD_U32(x+8)==0xFF&&!PPC_LOAD_U32(x+0xC)&&
                PPC_LOAD_U32(x+0x18)==(finished?0x18280186u:0),"original raster metadata changed");
            runtime.pointer(owner,24,false);
            snapshotNeed(PPC_LOAD_U32(0x82D09850)==owner&&PPC_LOAD_U32(owner)==0x8214E49C&&PPC_LOAD_U32(owner+8)==0x8214E498&&
                (!record.snapshotReady||PPC_LOAD_U32(owner+0xC)==r),"original crossfade owner association changed");
            if(finished) {
                snapshotNeed(record.texture&&record.textureId&&!runtime.pageAccess[record.textureId>>12].load()&&
                    record.texture->format==Graphics::TextureFormat::RGBA8&&record.texture->width==record.width&&
                    record.texture->height==record.height&&record.texture->levelCount()==1,"native texture ownership differs");
                backend.validateTexture(record.texture);
            }
            return;
        }
        const bool loading=type==4,uploaded=bool(record.texture);
        const bool privateSurface=bool(record.ownedColor)||bool(record.ownedDepth);
        const uint32_t format=loading?(uploaded?0x1A200154:0):((type==2 || type==5)?0x182801B6:0x1A220197);
        if(x!=record.extension || PPC_LOAD_U32(0x82CD1E28)!=record.total)
            throw Failure("Native camera raster allocation/extension changed outside supported ownership");
        // Validate the whole retained allocation, then read its live fields.
        // No callbacks or original execution intervene between these reads.
        const auto* bytes=runtime.probe(r,record.total,true);
        if(recordingSurfaces)recordingSurfaces->arm(bytes,record.total);
        const auto* plugin=bytes+(x-r);
        const auto word=[&](uint32_t offset){return checkedWord(bytes+offset);};
        const auto byte=[&](uint32_t offset){return *reinterpret_cast<const volatile uint8_t*>(bytes+offset);};
        if(word(0)!=r || word(0xC)!=record.width ||
           word(0x10)!=record.height || word(0x14)!=(loading?0x10:0x20) ||
           byte(0x20)!=type || byte(0x21)!=((type==2 || (loading&&!uploaded))?0x80:0) ||
           byte(0x23)!=(loading?3:((type==2 || type==5)?0xB:9)) ||
           checkedWord(plugin)!=(loading?record.textureId:(privateSurface?record.surfaceId:(type==1?depthId:0))) ||
           checkedWord(plugin+4) || checkedWord(plugin+8)!=(uploaded?0x01000100:(type==5?0x010000FF:0xFF)) || checkedWord(plugin+0xC) ||
           checkedWord(plugin+0x18)!=format)
            throw Failure("Native camera raster metadata/borrowed role changed outside supported ownership");
        if(privateSurface && (!record.surfaceId || (type==5)!=bool(record.ownedColor) ||
           (type==1)!=bool(record.ownedDepth) || !privateSize(record.width,record.height)))
            throw Failure("Native private raster surface ownership differs");
        if(record.ownedColor && fullSizeWorkingView(record.width,record.height) &&
           (record.ownedColor!=color || record.surfaceId==colorId || color->width!=1280 || color->height!=720))
            throw Failure("Native full-size color view lost its shared working storage or logical identity");
        if(record.ownedDepth && fullSizeWorkingView(record.width,record.height) &&
           (record.ownedDepth!=depth || record.surfaceId==depthId || depth->pixelWidth()!=1280 || depth->pixelHeight()!=720))
            throw Failure("Native full-size depth view lost its shared working storage or logical identity");
        if(uploaded && (word(4) || word(0x18) || (byte(0x22)&6) ||
            word(0x28)!=record.width || word(0x2C)!=record.height ||
            checkedWord(plugin+0x10) || checkedWord(plugin+0x14)))
            throw Failure("Native immutable texture raster has invalid post-unlock metadata");
    }
    const Record& rootMetadata(uint8_t* base,uint32_t r,uint32_t type) const {
        const auto it=records.find(r);
        if((type!=1 && type!=2 && type!=5) || it==records.end() || it->second.type!=type)
            throw Failure("Unknown, stale or unsupported native camera raster role");
        validateMetadata(base,r,it->second);
        if(PPC_LOAD_U16(r+0x1C) || PPC_LOAD_U16(r+0x1E) || !it->second.node)
            throw Failure("Native camera requires a registered root raster with zero offsets");
        return it->second;
    }
    NativeCameraSurface surface(const Record& record) const {
        return {record.surfaceId?record.surfaceId:(record.type==1?depthId:colorId),record.ownedColor,record.ownedDepth};
    }
    NativeCameraSurfaceRole role(const Record& record) const {
        return {record.surfaceId?record.surfaceId:(record.type==1?depthId:colorId),bool(record.ownedColor),bool(record.ownedDepth)};
    }
};
EngineRasters::EngineRasters(Runtime& rt,Graphics::NativeBackend& backend,uint32_t c,uint32_t d,
    std::shared_ptr<Graphics::RenderTarget> color,std::shared_ptr<Graphics::DepthTarget> depth):
    state(std::make_unique<State>(rt,backend,c,d,std::move(color),std::move(depth))) {}
EngineRasters::~EngineRasters()=default;
size_t EngineRasters::liveCount() const {state->requireOwner(state->runtime.base);return state->records.size();}
void EngineRasters::requireReleased() const {
    state->requireOwner(state->runtime.base);
    if(!state->records.empty()||state->snapshot||state->planeAccess) throw Failure("Native driver still owns live camera raster associations");
}
void EngineRasters::create(const PPCContext& incoming,uint8_t* base,uint32_t r,uint32_t flags) {
    ++state->recordsVersion; // invalidates the camera-surface memo
    auto& s=*state;s.requireOwner(base);
    const bool loading=flags==0x384;
    const bool snapshot=flags==0x580||flags==0x480;
    if(flags!=1 && flags!=2 && flags!=5 && !loading && !snapshot) {
        char message[160];int n=std::snprintf(message,sizeof(message),"Unimplemented native camera raster create flags=0x%08X, raster=0x%08X",flags,r);
        if(n<0||size_t(n)>=sizeof(message)) throw Failure("Unimplemented native camera raster create: diagnostic truncated");
        throw Failure(message);
    }
    if(snapshot) {
        snapshotNeed(s.snapshot&&!s.snapshot->raster&&bool(s.snapshot->videoFrame)==(flags==0x480)&&currentContext==&incoming&&uint32_t(incoming.lr)==0x824081C0&&
            incoming.r1.u32==s.snapshot->stack-0x120&&incoming.r26.u32==flags&&!incoming.r27.u32&&
            incoming.r29.u32==s.snapshot->width&&incoming.r28.u32==s.snapshot->height&&incoming.r31.u32==r,
            "raster allocation has no matching original helper frame");
    }
    const uint32_t x=s.extension(base,r);
    const uint32_t oldShared=PPC_LOAD_U32(0x82CD1D88),oldHead=PPC_LOAD_U32(0x82D0D01C);
    const bool privateDepth=flags==1 && !oldShared,privateSurface=flags==5 || privateDepth;
    // Shadows use1024; reflection helper8273BC58 uses16/256 square cameras.
    // Viewport helper827142D8 uses1280x720 and640x720 from table82CD12CC.
    // Only these independently executed original profiles are admitted.
    const uint32_t width=snapshot?s.snapshot->width:(privateSurface?PPC_LOAD_U32(r+0xC):(loading?256:s.color->width));
    const uint32_t height=snapshot?s.snapshot->height:(privateSurface?PPC_LOAD_U32(r+0x10):(loading?256:s.color->height));
    if(privateSurface && !privateSize(width,height)) {
        char message[220];std::snprintf(message,sizeof(message),
            "Unimplemented native private camera raster size=%ux%u flags=0x%08X raster=0x%08X caller=0x%08X stack=0x%08X",
            width,PPC_LOAD_U32(r+0x10),flags,r,uint32_t(incoming.lr),incoming.r1.u32);
        throw Failure(message);
    }
    if(PPC_LOAD_U32(r)!=r || PPC_LOAD_U32(r+0xC)!=width || PPC_LOAD_U32(r+0x10)!=height ||
       !PPC_LOAD_U32(r+0xC) || !PPC_LOAD_U32(r+0x10) || PPC_LOAD_U16(r+0x1C) || PPC_LOAD_U16(r+0x1E))
        throw Failure("Unimplemented native camera raster dimensions, offsets or child ownership");
    if(PPC_LOAD_U32(0x82E3DCE8)!=0x182801B6 || PPC_LOAD_U32(0x82E3DD08)!=0x1A220197)
        throw Failure("Unverified native camera raster presentation format");
    if(loading && (PPC_LOAD_U32(r+0x14)!=16 || PPC_LOAD_U32(0x82062B48)!=0x1828014F || PPC_LOAD_U32(0x82062B4C)!=0x10010000))
        throw Failure("Unverified original loading raster CPU format profile");
    if(s.records.contains(r) || s.listNode(base,r)) throw Failure("Native camera raster already has an owner/list node");
    if(flags==5 && (PPC_LOAD_U32(0x82062B88)!=0x182801B6 || PPC_LOAD_U32(0x82062B8C)!=0x20010000))
        throw Failure("Unverified original off-screen camera raster format profile");
    if(snapshot) snapshotNeed(!PPC_LOAD_U32(r+0x14)&&
        PPC_LOAD_U32(flags==0x480?0x82062B50:0x82062B58)==(flags==0x480?0x28000102u:0x18280186u)&&
        PPC_LOAD_U32(flags==0x480?0x82062B54:0x82062B5C)==(flags==0x480?0x08000000u:0x20010000u),"original snapshot/plane format table/input differs");
    s.runtime.pointer(0x82CD1D88,4,true);
    // Snapshot only fields this bounded path writes. Preserve untouched plugin
    // bytes and the original format helper's independent scratch-memory effects.
    struct Saved {uint8_t* at;std::vector<uint8_t> bytes;};
    std::vector<Saved> saved;
    auto save=[&](uint32_t at,uint32_t n) {auto* p=s.runtime.pointer(at,n,true);saved.push_back({p,{p,p+n}});};
    save(r+4,8);save(r+0x14,4);save(r+0x20,2);save(r+0x23,1);save(x,16);save(x+0x18,4);
    if(flags==2) {save(r+0x18,4);save(r+0x28,8);}
    EngineCpuCalls cpu(incoming,base);
    auto [it,inserted]=s.records.emplace(r,State::Record{x,flags&7,0,PPC_LOAD_U32(0x82CD1E28),width,height});
    if(!inserted) throw Failure("Duplicate native camera raster transaction");
    if(snapshot) {
        if(s.snapshot->videoFrame) {
            it->second.videoOwner=s.snapshot->owner;it->second.videoFrame=s.snapshot->videoFrame;
            it->second.videoIndex=s.snapshot->videoIndex;it->second.videoStack=s.snapshot->stack;
        } else it->second.snapshotOwner=s.snapshot->owner;
    }
    try {
        if(privateSurface) {
            // Original default and full-size type-5 headers describe the same
            // single-sample 182801B6 surface: pitch1280, tiles[0,720).
            // Retain separate logical IDs/list nodes while sharing working pixels.
            // Front/color-copy textures are separate resolve destinations.
            if(flags==5) it->second.ownedColor=fullSizeWorkingView(width,height)?s.color:
                s.backend.createTarget(width,height,Graphics::TargetFormat::RGB10A2,height==720?Graphics::TargetScale::Scene:Graphics::TargetScale::Fixed);
            // The matching 1A220197 depth views both use tiles[720,1440).
            else it->second.ownedDepth=fullSizeWorkingView(width,height)?s.depth:
                s.backend.createDepthTarget(width,height,height==720?Graphics::TargetScale::Scene:Graphics::TargetScale::Fixed);
            it->second.surfaceId=s.runtime.engineDriver->allocateTargetIdentity();
        }
        PPC_STORE_U32(r+4,0);PPC_STORE_U32(r+8,0);PPC_STORE_U8(r+0x20,uint8_t(flags&7));PPC_STORE_U8(r+0x21,uint8_t(flags&0xF8));
        PPC_STORE_U32(x,0);PPC_STORE_U32(x+4,0);PPC_STORE_U32(x+8,0xFF);PPC_STORE_U32(x+0xC,0);PPC_STORE_U32(x+0x18,0);
        cpu.registers().lr=0x823F70D8;
        if(cpu.invoke(0x823F6E68,r,flags)!=1) throw Failure("Original camera raster CPU format normalization failed");
        if(flags==2) {
            PPC_STORE_U32(r+0x18,0);PPC_STORE_U32(r+4,0);
            PPC_STORE_U32(r+0x28,PPC_LOAD_U32(r+0xC));PPC_STORE_U32(r+0x2C,PPC_LOAD_U32(r+0x10));
            PPC_STORE_U32(r+0x14,0x20);PPC_STORE_U8(r+0x21,0x80);PPC_STORE_U32(x+0x18,0x182801B6);
        } else if(privateSurface) {
            // Replace the SDK surface/EDRAM placement with native backing.
            // Preserve the original CPU format fields and list insertion below.
            PPC_STORE_U32(x+0x18,flags==5?0x182801B6:0x1A220197);
            if(flags==5) PPC_STORE_U8(x+8,1);
            else {
                // Keep the original pure CPU placement calculation. Its result
                // is console surface metadata, never a native address/allocation.
                cpu.registers().lr=0x823F6234;
                const uint32_t placement=height==720?(width==1280?0x2D0:0x168):(width==16?1:(width==256?0x40:0x340));
                if(cpu.invoke(0x823ED930,width,height,0x18280186,0)!=placement)
                    throw Failure("Original private depth CPU placement profile differs");
            }
            PPC_STORE_U32(x,it->second.surfaceId);
        } else if(flags==1) {
            PPC_STORE_U32(0x82CD1D88,0);PPC_STORE_U32(x+0x18,0x1A220197);PPC_STORE_U32(x,s.depthId);
        }
        s.validateMetadata(base,r,it->second);
        if(!loading && !snapshot) {
            cpu.registers().lr=0x823F726C;cpu.invoke(0x823F5DA8,r);
            const uint32_t node=s.listNode(base,r);
            if(!node || PPC_LOAD_U32(0x82D0D01C)!=node || PPC_LOAD_U32(node+4)!=oldHead)
                throw Failure("Original camera raster list insertion did not preserve ownership");
            it->second.node=node;
        }
        if(snapshot)s.snapshot->raster=r;
        fprintf(stderr,"[NATIVE RASTER] created original root=%08X type=%u extent=%ux%u extension=%X node=%08X native_surface=%08X ownership=%s\n",
            r,flags&7,width,height,x-r,it->second.node,(loading||snapshot)?0:(privateSurface?it->second.surfaceId:(flags==1?s.depthId:s.colorId)),
            snapshot?(flags==0x480?"pending movie plane":"pending crossfade snapshot"):(loading?"pending texture":
                (privateSurface?(it->second.ownedColor==s.color?"shared default color view":
                    (it->second.ownedDepth==s.depth?"shared default depth view":"owned off-screen")):"borrowed driver role")));
    } catch(...) {
        auto failure=std::current_exception();
        try {
            if(s.listNode(base,r)) {cpu.registers().lr=flags==1?0x823F636C:(flags==5?0x823F640C:0x823F6430);cpu.invoke(0x823F5E10,r);}
            if(PPC_LOAD_U32(0x82D0D01C)!=oldHead) throw Failure("Raster rollback did not restore the original list head");
            for(auto& row:saved) std::copy(row.bytes.begin(),row.bytes.end(),row.at);
            if(flags==1) PPC_STORE_U32(0x82CD1D88,oldShared);
            s.records.erase(it);
        } catch(const std::exception& error) {
            fprintf(stderr,"[NATIVE RASTER] transaction rollback incomplete: %s; terminal cleanup required\n",error.what());
        }
        std::rethrow_exception(failure);
    }
}
void EngineRasters::preflightDestroy(uint8_t* base,uint32_t r) const {
    auto& s=*state;s.requireOwner(base);const auto it=s.records.find(r);
    if(it==s.records.end()) throw Failure("Unknown or stale native camera raster destruction");
    s.validateMetadata(base,r,it->second);
    for(uint32_t stage=0;stage<8;++stage) if(PPC_LOAD_U32(0x82D0E3F8+stage*0x18)==r)
        throw Failure("Unimplemented native camera raster texture-stage unbind");
    if(it->second.surfaceId) {
        for(uint32_t slot=0;slot<4;++slot) if(PPC_LOAD_U32(0x82D0CF5C+4*slot)==it->second.surfaceId)
            throw Failure("Native private raster surface is still a color attachment");
        if(PPC_LOAD_U32(0x82D0CF58)==it->second.surfaceId)
            throw Failure("Native private raster surface is still a depth attachment");
    }
    const uint32_t node=s.listNode(base,r);
    if(it->second.snapshotOwner) snapshotNeed(it->second.snapshotFinished&&it->second.snapshotReady&&!state->snapshot,"snapshot construction is incomplete");
    if(it->second.videoOwner) snapshotNeed(it->second.videoPublished&&!it->second.videoLocked&&!state->snapshot,"movie plane is locked or incompletely published");
    if((it->second.type!=4 && !it->second.snapshotOwner && !it->second.videoOwner && !it->second.node) || node!=it->second.node)
        throw Failure("Native raster has an invalid original list association");
}
void EngineRasters::destroy(const PPCContext& incoming,uint8_t* base,uint32_t r) {
    ++state->recordsVersion; // invalidates the camera-surface memo
    preflightDestroy(base,r);auto& s=*state;const auto it=s.records.find(r);
    if(it->second.type!=4 && !it->second.snapshotOwner && !it->second.videoOwner) {
        EngineCpuCalls cpu(incoming,base);cpu.registers().lr=it->second.type==1?0x823F636C:(it->second.type==5?0x823F640C:0x823F6430);
        cpu.invoke(0x823F5E10,r);
        if(s.listNode(base,r)) throw Failure("Original camera raster list removal failed");
    }
    // Private views lose their owner; shared working storage, driver backing and
    // the shared-depth flag remain unchanged. The
    // original wrapper performs the final raster allocation free after return.
    if(it->second.videoStaging) {
        EngineCpuCalls cpu(incoming,base);cpu.registers().lr=0x82441128;
        cpu.invoke(0x8238EB00,it->second.videoStaging,0xB1800000);
    }
    s.records.erase(it);
    fprintf(stderr,"[NATIVE RASTER] released original association=%08X and any owned surface; borrowed driver backing retained\n",r);
}
void EngineRasters::requireCameraRoot(uint8_t* base,uint32_t r,uint32_t type) const {
    auto& s=*state;s.requireOwner(base);
    const auto& record=s.rootMetadata(base,r,type);
    if(s.listNode(base,r)!=record.node)
        throw Failure("Native camera requires a registered root raster with zero offsets");
}
uint32_t EngineRasters::cameraSurfaceIdentity(uint8_t* base,uint32_t r,uint32_t type) const {
    return cameraSurface(base,r,type).identity;
}
NativeCameraSurface EngineRasters::cameraSurface(uint8_t* base,uint32_t r,uint32_t type) const {
    // Resolve metadata, the complete root list and native ownership together.
    // The returned references belong to this validation call; no guest snapshot
    // or permission result is cached across original CPU execution.
    auto& s=*state;s.requireOwner(base);const auto& record=s.rootMetadata(base,r,type);
    if(s.listNode(base,r)!=record.node)
        throw Failure("Native camera requires a registered root raster with zero offsets");
    return s.surface(record);
}
std::array<NativeCameraSurfaceRole,2> EngineRasters::cameraSurfaces(uint8_t* base,uint32_t colorRaster,uint32_t colorType,uint32_t depthRaster) const {
    if(!depthRaster) {
        const auto surface=cameraSurface(base,colorRaster,colorType);
        return {NativeCameraSurfaceRole{surface.identity,bool(surface.color),bool(surface.depth)},NativeCameraSurfaceRole{}};
    }
    auto& s=*state;s.requireOwner(base);
    State::SurfacesKey key{colorRaster,colorType,depthRaster,PPC_LOAD_U32(0x82E3DC94),PPC_LOAD_U32(0x82CD1E28),s.recordsVersion,
        s.extensionMemo.generation(),s.listMemo.generation(),
        s.color.get(),s.depth.get(),s.color->width,s.color->height,s.depth->pixelWidth(),s.depth->pixelHeight()};
    if(s.surfacesMemo.valid() && s.surfacesKey==key && s.extensionMemo.valid() && s.extensionOffset==key.offset &&
       s.extensionTotal==key.total && s.listMemo.valid()) {
        if(!GuestReadMemo::verify())return s.surfacesResult;
        // Diagnostic (SIMPSONS_WATCH_VERIFY=1): fall through; a fresh proof must reach the same answer.
        const auto cached=s.surfacesResult;s.surfacesMemo.invalidate();
        const auto fresh=cameraSurfaces(base,colorRaster,colorType,depthRaster);
        if(fresh[0].identity!=cached[0].identity||fresh[0].color!=cached[0].color||fresh[0].depth!=cached[0].depth||
           fresh[1].identity!=cached[1].identity||fresh[1].color!=cached[1].color||fresh[1].depth!=cached[1].depth) {
            std::fputs("[READ MEMO MISMATCH] camera surfaces cached result differs from a fresh proof\n",stderr);
            std::fflush(stderr);std::abort();
        }
        return fresh;
    }
    struct Recording {
        const State& s;
        explicit Recording(const State& owner):s(owner){s.surfacesMemo.begin();s.recordingSurfaces=&s.surfacesMemo;}
        ~Recording(){s.recordingSurfaces=nullptr;}
    } recording{s};
    const auto& color=s.rootMetadata(base,colorRaster,colorType);
    const auto matches=s.listNodes(base,std::array{colorRaster,depthRaster});
    if(matches[0].node!=color.node)
        throw Failure("Native camera requires a registered root raster with zero offsets");
    const auto& depth=s.rootMetadata(base,depthRaster,1);
    if(matches[1].duplicate)throw Failure("Duplicate original raster list entry");
    if(matches[1].node!=depth.node)
        throw Failure("Native camera requires a registered root raster with zero offsets");
    s.surfacesResult={s.role(color),s.role(depth)};
    // The inner memos may have been re-proven during this proof: key on their current proofs.
    key.extensionProof=s.extensionMemo.generation();key.listProof=s.listMemo.generation();
    s.surfacesKey=key;s.surfacesMemo.commit();
    return s.surfacesResult;
}
void EngineRasters::attachTexture(uint8_t* base,uint32_t r,std::shared_ptr<Graphics::Texture> texture) {
    ++state->recordsVersion; // invalidates the camera-surface memo
    auto& s=*state;s.requireOwner(base);const auto it=s.records.find(r);
    if(it==s.records.end() || it->second.type!=4 || it->second.texture || it->second.textureId || !texture ||
       texture->format!=Graphics::TextureFormat::BC3 || texture->width!=it->second.width || texture->height!=it->second.height)
        throw Failure("Invalid native loading texture attachment");
    s.validateMetadata(base,r,it->second);
    if(it->second.node || s.listNode(base,r)) throw Failure("Loading texture unexpectedly acquired a camera list node");
    const uint32_t x=it->second.extension,id=textureIdentity(s.runtime);
    // All guest destinations were preflighted by metadata validation. No guest
    // pointer to a temporary console lock/surface is represented or retained.
    it->second.texture=std::move(texture);it->second.textureId=id;
    PPC_STORE_U32(x,id);PPC_STORE_U32(x+0x18,0x1A200154);PPC_STORE_U32(x+8,0x01000100);
    PPC_STORE_U32(x+0xC,0);PPC_STORE_U32(x+0x10,0);PPC_STORE_U32(x+0x14,0);
    PPC_STORE_U8(r+0x21,0);PPC_STORE_U8(r+0x23,3);PPC_STORE_U8(r+0x22,PPC_LOAD_U8(r+0x22)&~6u);
    PPC_STORE_U32(r+4,0);PPC_STORE_U32(r+0x18,0);
    PPC_STORE_U32(r+0x28,it->second.width);PPC_STORE_U32(r+0x2C,it->second.height);
    fprintf(stderr,"[NATIVE TEXTURE] attached original raster=%08X identity=%08X BC3 %ux%u; original post-unlock fields retained\n",r,id,it->second.width,it->second.height);
}
std::shared_ptr<Graphics::Texture> EngineRasters::texture(uint8_t* base,uint32_t r) const {
    auto& s=*state;s.requireOwner(base);const auto it=s.records.find(r);
    if(it==s.records.end() || !it->second.texture) throw Failure("Unknown, stale or unallocated native texture raster");
    if(it->second.snapshotOwner)snapshotNeed(it->second.snapshotReady,"crossfade parent has not published its raster");
    if(it->second.videoOwner)snapshotNeed(it->second.videoPublished&&!it->second.videoLocked&&it->second.videoInitialized,"movie plane has no unlocked uploaded image");
    s.validateMetadata(base,r,it->second);return it->second.texture;
}
uint32_t EngineRasters::rasterByIdentity(uint8_t* base,uint32_t id) const {
    auto& s=*state;s.requireOwner(base);
    if(!id) return 0;
    for(const auto& [r,record]:s.records) if(record.textureId==id) return r;
    return 0;
}
uint32_t EngineRasters::rasterByPlugin(uint8_t* base,uint32_t identity) const {
    auto& s=*state;s.requireOwner(base);
    if(!identity) return 0;
    for(const auto& [r,record]:s.records) {
        (void)record;
        try {
            s.runtime.pointer(r+0x34,8,false);
            if(PPC_LOAD_U32(r+0x34)==identity) return r;
        } catch(...) {}
    }
    return 0;
}
std::shared_ptr<Graphics::RenderTarget> EngineRasters::ownedColor(uint32_t id) const {
    auto& s=*state;s.requireOwner(s.runtime.base);
    for(const auto& [r,record]:s.records) if(record.surfaceId==id && record.ownedColor) {
        s.validateMetadata(s.runtime.base,r,record);return record.ownedColor;
    }
    return {};
}
std::shared_ptr<Graphics::DepthTarget> EngineRasters::ownedDepth(uint32_t id) const {
    auto& s=*state;s.requireOwner(s.runtime.base);
    for(const auto& [r,record]:s.records) if(record.surfaceId==id && record.ownedDepth) {
        s.validateMetadata(s.runtime.base,r,record);return record.ownedDepth;
    }
    return {};
}
void EngineRasters::beginSnapshot(PPCContext& c,uint8_t* base) {
    ++state->recordsVersion; // invalidates the camera-surface memo
    auto& s=*state;s.requireOwner(base);
    snapshotNeed(currentContext==&c&&c.r1.u32>=0x300&&!(c.r1.u32&15),"wrong helper context/stack");
    if(uint32_t(c.lr)==0x8282E9B8) {
        snapshotNeed(!s.snapshot,"nested movie plane construction");
        const uint32_t frame=c.r20.u32,provider=c.r23.u32,index=c.r30.u32;
        s.runtime.pointer(frame,0x54,true);s.runtime.pointer(provider,8,false);
        snapshotNeed(index<3&&!c.r19.u32&&c.r5.u32==0x28000002&&c.r6.u32==0x400&&
            ((c.r25.u32==1280&&c.r24.u32==720)||(c.r25.u32==640&&c.r24.u32==480))&&
            c.r3.u32==(c.r25.u32>>(index!=0))&&c.r4.u32==(c.r24.u32>>(index!=0))&&
            c.r31.u32==frame+0x10+4*index&&c.r28.u32==c.r1.u32+0x50+4*index&&
            c.r21.u32==0x82E40000&&c.r22.u32==0x8215D49C&&PPC_LOAD_U32(provider)==0x8215D4F0&&
            PPC_LOAD_U32(provider+4)&&PPC_LOAD_U32(frame)==0x37047734&&PPC_LOAD_U32(frame+0x38)==3,
            "unqualified original movie plane request");
        for(const auto& [r,record]:s.records) {
            (void)r;
            snapshotNeed(record.videoFrame!=frame||record.videoIndex!=index,"duplicate live movie plane");
        }
        s.runtime.pointer(c.r1.u32-0x200,0x200,true);
        s.snapshot=State::Snapshot{provider,c.r1.u32,c.r3.u32,c.r4.u32,0,frame,index};
        return;
    }
    // This original allocator is shared. Diagnose an unqualified caller before
    // applying the crossfade-only singleton contract to an unrelated request.
    if(uint32_t(c.lr)!=0x827020AC) {
        char message[240];std::snprintf(message,sizeof(message),
            "Unimplemented native raster factory caller=%08X dimensions=%ux%u format=%08X flags=%08X owner=%08X output=%08X plane=%u",
            uint32_t(c.lr),c.r3.u32,c.r4.u32,c.r5.u32,c.r6.u32,c.r23.u32,c.r20.u32,c.r30.u32);
        dumpGuestStack(c);throw Failure(message);
    }
    snapshotNeed(!s.snapshot,"nested snapshot construction");
    for(const auto& [r,record]:s.records) if(record.snapshotOwner) {
        std::fprintf(stderr,"[NATIVE CROSSFADE] duplicate request owner=%08X previous_owner=%08X raster=%08X previous_owner_raster=%08X finished=%u ready=%u caller=%08X stack=%08X\n",
            PPC_LOAD_U32(0x82D09850),record.snapshotOwner,r,PPC_LOAD_U32(record.snapshotOwner+0xC),
            unsigned(record.snapshotFinished),unsigned(record.snapshotReady),uint32_t(c.lr),c.r1.u32);
        dumpGuestStack(c);
        snapshotNeed(false,"duplicate live crossfade raster");
    }
    const uint32_t owner=PPC_LOAD_U32(0x82D09850);s.runtime.pointer(owner,24,false);
    snapshotNeed(uint32_t(c.lr)==0x827020AC&&c.r31.u32==owner&&c.r3.u32==1280&&c.r4.u32==720&&
        c.r3.u32==PPC_LOAD_U32(0x82E3DCE0)&&c.r4.u32==PPC_LOAD_U32(0x82E3DCE4)&&c.r5.u32==0x18280186&&c.r6.u32==0x500,
        "unsupported crossfade caller/dimensions/format/flags");
    snapshotNeed(PPC_LOAD_U32(owner)==0x8214E49C&&PPC_LOAD_U32(owner+4)==1&&PPC_LOAD_U32(owner+8)==0x8214E498&&
        !PPC_LOAD_U32(owner+0x10)&&!PPC_LOAD_U8(owner+0x14)&&!PPC_LOAD_U8(owner+0x15)&&!PPC_LOAD_U8(owner+0x16),"original crossfade constructor fields differ");
    s.runtime.pointer(c.r1.u32-0x200,0x200,true);
    s.snapshot=State::Snapshot{owner,c.r1.u32,c.r3.u32,c.r4.u32,0};
}
void EngineRasters::createSnapshotTexture(PPCContext& c,uint8_t* base) {
    ++state->recordsVersion; // invalidates the camera-surface memo
    auto& s=*state;s.requireOwner(base);
    snapshotNeed(s.snapshot&&s.snapshot->raster&&currentContext==&c,"texture factory has no pending original raster");
    const auto& p=*s.snapshot;auto it=s.records.find(p.raster);snapshotNeed(it!=s.records.end(),"pending raster ownership disappeared");auto& record=it->second;
    s.validateMetadata(base,p.raster,record);
    const bool video=p.videoFrame!=0;const uint32_t format=video?0x28000002:0x18280186;
    snapshotNeed(uint32_t(c.lr)==0x823F7354&&c.r1.u32==p.stack-0x90&&c.r31.u32==p.raster&&c.r29.u32==record.extension&&
        c.r30.u32==format&&c.r27.u32==(video?0x400u:0x500u)&&c.r28.u32==1&&!c.r26.u32&&
        c.r3.u32==p.width&&c.r4.u32==p.height&&c.r5.u32==1&&c.r6.u32==1&&!c.r7.u32&&c.r8.u32==format&&c.r9.u32==1&&c.r10.u32==3&&
        !record.texture&&!record.textureId&&!record.snapshotFinished,"unsupported original snapshot factory profile");
    auto texture=s.backend.createWritableTexture(p.width,p.height,video?Graphics::TextureFormat::R8:Graphics::TextureFormat::RGBA8);const uint32_t id=textureIdentity(s.runtime);
    if(video) {
        // Preserve the original primary pixel allocation, including its pool,
        // alignment/cache policy and TLS effects. The game already committed
        // that pool; allocating extra platform pages would count it twice.
        snapshotNeed(PPC_LOAD_U32(0x82D57244)==0x82D5724C&&PPC_LOAD_U32(0x82D5724C)==0x820B60B8&&
            PPC_LOAD_U32(0x820B60D8)==0x8268E138&&PPC_LOAD_U32(0x820B60DC)==0x8268E6E8,
            "movie plane original allocator registration differs");
        const uint32_t pitch=(p.width+255)&~255u;
        const uint64_t bytes64=uint64_t(pitch)*uint64_t(p.height);
        if(bytes64==0 || bytes64>UINT32_MAX) throw Failure("Movie plane pitch*height overflows 32 bits");
        EngineCpuCalls cpu(c,base);cpu.registers().lr=0x82440628;
        const uint32_t storage=cpu.invoke(0x8238E880,uint32_t(bytes64),0xBC800000);
        snapshotNeed(storage!=0,"movie plane original CPU pool allocation failed");
        try {
            snapshotNeed(!(storage&4095),"movie plane original allocation is not page aligned");
            s.runtime.pointer(storage,pitch*p.height,true);
        } catch(...) {
            cpu.registers().lr=0x82441128;cpu.invoke(0x8238EB00,storage,0xB1800000);throw;
        }
        record.videoStaging=storage;record.videoPitch=pitch;
    }
    record.texture=std::move(texture);record.textureId=id;c.r3.u64=id;
    // The original helper's next store publishes extension+0. Its following
    // instructions clear the no-allocation bit and store the packed format.
    std::fprintf(stderr,"[NATIVE %s] allocated owner=%08X raster=%08X id=%08X %s %ux%u one level; original initial pixels unspecified\n",video?"MOVIE PLANE":"CROSSFADE",p.owner,p.raster,id,video?"R8":"RGBA8",p.width,p.height);
}
void EngineRasters::finishSnapshot(PPCContext& c,uint8_t* base) {
    ++state->recordsVersion; // invalidates the camera-surface memo
    auto& s=*state;s.requireOwner(base);snapshotNeed(s.snapshot&&s.snapshot->raster&&currentContext==&c,"helper completion has no pending snapshot");
    const auto& p=*s.snapshot;auto& record=s.records.at(p.raster);
    snapshotNeed(c.r1.u32==p.stack-0x90&&c.r31.u32==p.raster&&c.r29.u32==record.extension&&c.r30.u32==(p.videoFrame?0x28000002u:0x18280186u)&&
        uint32_t(c.lr)==0x823F7354&&record.texture&&!record.snapshotFinished&&!record.snapshotReady&&!record.node&&!s.listNode(base,p.raster),"original snapshot helper completion differs");
    record.snapshotFinished=true;try{s.validateMetadata(base,p.raster,record);}catch(...){record.snapshotFinished=false;throw;}
    if(p.videoFrame)s.snapshot.reset();
}
void EngineRasters::commitCrossfade(PPCContext& c,uint8_t* base) {
    ++state->recordsVersion; // invalidates the camera-surface memo
    auto& s=*state;s.requireOwner(base);snapshotNeed(s.snapshot&&s.snapshot->raster&&currentContext==&c,"parent completion has no pending snapshot");
    const auto p=*s.snapshot;auto& record=s.records.at(p.raster);
    snapshotNeed(c.r1.u32==p.stack&&c.r31.u32==p.owner&&uint32_t(c.lr)==0x827020DC&&record.snapshotFinished&&!record.snapshotReady&&
        PPC_LOAD_U32(p.owner+0xC)==p.raster,"original crossfade parent publication differs");
    record.snapshotReady=true;try{s.validateMetadata(base,p.raster,record);}catch(...){record.snapshotReady=false;throw;}
    s.snapshot.reset();
    std::fprintf(stderr,"[NATIVE CROSSFADE] original constructor complete owner=%08X raster=%08X id=%08X; CPU message registrations retained; snapshot capture/composition unqualified\n",p.owner,p.raster,record.textureId);
}
void EngineRasters::commitMovieFrame(PPCContext& c,uint8_t* base) {
    ++state->recordsVersion; // invalidates the camera-surface memo
    auto& s=*state;s.requireOwner(base);
    snapshotNeed(currentContext==&c&&!s.snapshot&&c.r30.u32==3&&c.r31.u32==c.r20.u32+0x1C&&
        c.r28.u32==c.r1.u32+0x5C,"movie frame original completion ABI differs");
    std::array<std::pair<uint32_t,State::Record*>,3> planes{};
    for(auto& [r,record]:s.records) if(record.videoFrame==c.r20.u32) {
        snapshotNeed(record.videoIndex<3&&!planes[record.videoIndex].second,"movie frame has duplicate plane associations");
        planes[record.videoIndex]={r,&record};
    }
    for(uint32_t i=0;i<3;++i) {
        auto [r,record]=planes[i];snapshotNeed(record!=nullptr,"movie frame is missing a plane");
        snapshotNeed(record->videoOwner==c.r23.u32&&record->videoStack==c.r1.u32&&!record->videoPublished&&
            record->snapshotFinished&&record->videoLocked&&record->videoStaging&&record->videoPitch,
            "movie frame plane is not completely locked/allocated");
        s.validateMetadata(base,r,*record);
        const uint32_t frame=record->videoFrame,context=PPC_LOAD_U32(frame+0x44+4*i);
        s.runtime.pointer(context,12,false);
        snapshotNeed(PPC_LOAD_U32(context)==r&&PPC_LOAD_U32(context+4)==record->videoStaging&&PPC_LOAD_U32(context+8)==record->videoPitch&&
            PPC_LOAD_U32(frame+4+4*i)==record->videoStaging&&PPC_LOAD_U32(frame+0x10+4*i)==record->width*record->height&&
            PPC_LOAD_U32(frame+0x1C+4*i)==record->videoPitch&&PPC_LOAD_U32(frame+0x28)==c.r25.u32&&
            PPC_LOAD_U32(frame+0x2C)==c.r24.u32&&!PPC_LOAD_U32(frame+0x30),"movie frame original pointer/pitch publication differs");
    }
    snapshotNeed(planes[1].second->videoPitch==planes[2].second->videoPitch,"movie decoder requires equal chroma pitches");
    for(auto [r,record]:planes) {
        (void)r;record->videoContext=PPC_LOAD_U32(record->videoFrame+0x44+4*record->videoIndex);record->videoPublished=true;
    }
    std::fprintf(stderr,"[NATIVE MOVIE FRAME] original frame=%08X provider=%08X planes=3 dimensions=%ux%u; stable CPU storage, decode/presentation not yet verified\n",
        c.r20.u32,c.r23.u32,c.r25.u32,c.r24.u32);
}
NativeMovieFrame EngineRasters::movieFrame(uint8_t* base,uint32_t presenter) const {
    auto& s=*state;s.requireOwner(base);s.runtime.pointer(presenter,0x44,false);
    snapshotNeed(!s.snapshot&&!s.planeAccess&&PPC_LOAD_U32(presenter)==0x8215D4B0,
        "movie draw presenter or active construction differs");
    NativeMovieFrame frame;frame.descriptor=PPC_LOAD_U32(presenter+0x14);
    s.runtime.pointer(frame.descriptor,0x54,false);
    snapshotNeed(PPC_LOAD_U32(frame.descriptor)==0x37047734&&!PPC_LOAD_U32(frame.descriptor+0x30)&&
        PPC_LOAD_U32(frame.descriptor+0x38)==3,"movie draw descriptor format/count differs");
    frame.width=PPC_LOAD_U32(frame.descriptor+0x28);frame.height=PPC_LOAD_U32(frame.descriptor+0x2C);
    for(uint32_t i=0;i<3;++i) {
        const auto raster=PPC_LOAD_U32(presenter+8+4*i);const auto it=s.records.find(raster);
        snapshotNeed(it!=s.records.end(),"movie draw raster has no native owner");
        const auto& record=it->second;s.validateMetadata(base,raster,record);
        snapshotNeed(record.videoFrame==frame.descriptor&&record.videoIndex==i&&record.videoPresenter==presenter&&
            record.videoPublished&&!record.videoLocked&&record.videoInitialized&&record.texture&&
            record.width==frame.width/(i?2:1)&&record.height==frame.height/(i?2:1),"movie draw plane is stale, locked or not uploaded");
        if(!i)frame.provider=record.videoOwner;
        snapshotNeed(record.videoOwner==frame.provider,"movie draw mixes allocation providers");
        frame.rasters[i]=raster;frame.pitches[i]=record.videoPitch;frame.textures[i]=record.texture;
    }
    snapshotNeed(frame.pitches[1]==frame.pitches[2],"movie draw chroma pitches differ");
    return frame;
}
void EngineRasters::preflightMovieLock(PPCContext& c,uint8_t* base) {
    ++state->recordsVersion; // invalidates the camera-surface memo
    auto& s=*state;s.requireOwner(base);
    snapshotNeed(currentContext==&c&&!s.snapshot&&!s.planeAccess&&c.r1.u32>=0x200&&!(c.r1.u32&15)&&
        !c.r4.u32&&c.r5.u32==5,"invalid movie plane lock context/level/mode");
    const auto it=s.records.find(c.r3.u32);
    snapshotNeed(it!=s.records.end()&&it->second.videoOwner,"raster lock is not an owned movie plane");
    const auto& record=it->second;s.validateMetadata(base,it->first,record);
    snapshotNeed(record.snapshotFinished&&!record.videoLocked,"movie plane is incomplete or already locked");
    const uint32_t caller=uint32_t(c.lr),i=record.videoIndex;
    if(caller==0x8282E9C8) {
        snapshotNeed(!record.videoPublished&&record.videoStaging&&!record.videoHasLock&&c.r1.u32==record.videoStack&&
            c.r23.u32==record.videoOwner&&c.r20.u32==record.videoFrame&&c.r30.u32==i&&
            c.r29.u32==it->first,"initial movie lock original caller differs");
    } else {
        s.runtime.pointer(c.r31.u32,0x44,false);
        snapshotNeed(caller==0x8282ECE8&&record.videoPublished&&record.videoStaging&&c.r31.u32==record.videoPresenter&&
            PPC_LOAD_U32(c.r31.u32)==0x8215D4B0&&PPC_LOAD_U32(c.r31.u32+0x14)==record.videoFrame&&c.r29.u32==i&&c.r28.u32==0x44+4*i,
            "movie plane relock original caller differs");
    }
    s.runtime.pointer(c.r1.u32-0x60,0x60,true);
    s.planeAccess=State::PlaneAccess{it->first,c.r1.u32,caller,false};
}
void EngineRasters::preflightMovieUnlock(PPCContext& c,uint8_t* base) {
    ++state->recordsVersion; // invalidates the camera-surface memo
    auto& s=*state;s.requireOwner(base);
    snapshotNeed(currentContext==&c&&!s.snapshot&&!s.planeAccess&&c.r1.u32>=0x200&&!(c.r1.u32&15),
        "invalid movie plane unlock context");
    const auto it=s.records.find(c.r3.u32);
    snapshotNeed(it!=s.records.end()&&it->second.videoOwner,"raster unlock is not an owned movie plane");
    const auto& record=it->second;s.validateMetadata(base,it->first,record);
    snapshotNeed(record.videoPublished&&record.videoLocked&&record.videoStaging,"movie plane is unpublished or not locked");
    const uint32_t caller=uint32_t(c.lr),i=record.videoIndex;
    uint32_t presenter=0;
    if(caller==0x8282EA90) {
        snapshotNeed(c.r26.u32==record.videoFrame&&c.r27.u32==record.videoOwner&&c.r29.u32==record.videoContext&&
            c.r31.u32==record.videoFrame+0x44+4*i&&c.r30.u32==3-i,"movie plane destructor unlock caller differs");
    } else {
        s.runtime.pointer(c.r31.u32,0x44,false);
        snapshotNeed(caller==0x8282ED40&&c.r30.u32==record.videoFrame&&PPC_LOAD_U32(c.r31.u32)==0x8215D4B0&&
            (!record.videoPresenter||record.videoPresenter==c.r31.u32)&&PPC_LOAD_U32(c.r31.u32+0x14)==record.videoFrame&&c.r29.u32==i&&
            c.r28.u32==record.videoFrame+0x44+4*i,"movie frame presentation unlock caller differs");
        presenter=c.r31.u32;
    }
    s.runtime.pointer(c.r1.u32-0x60,0x60,true);
    s.planeAccess=State::PlaneAccess{it->first,c.r1.u32,caller,true,presenter};
}
void EngineRasters::lockMovieRaster(PPCContext& c,uint8_t* base) {
    ++state->recordsVersion; // invalidates the camera-surface memo
    auto& s=*state;s.requireOwner(base);
    snapshotNeed(currentContext==&c&&s.planeAccess&&!s.planeAccess->unlock,"movie lock callback has no original wrapper scope");
    const auto request=*s.planeAccess;
    struct End {State& state;~End(){state.planeAccess.reset();}} end{s};
    auto& record=s.records.at(request.raster);const uint32_t r=request.raster,x=record.extension;
    s.validateMetadata(base,r,record);
    snapshotNeed(c.r1.u32==request.stack-0x60&&c.r3.u32==c.r1.u32+0x50&&c.r4.u32==r&&c.r5.u32==5&&
        uint32_t(c.lr)==request.caller&&c.r11.u32==0x823F53D8&&c.ctr.u32==0x823F53D8&&
        PPC_LOAD_U32(s.engine+0x84)==0x823F53D8&&!record.videoLocked,"movie lock callback original ABI/table differs");
    s.runtime.pointer(r,record.total,true);s.runtime.pointer(c.r3.u32,4,true);
    // Original8243E588..E5D8 aligns linear one-byte plane rows to256 bytes.
    // The decoder receives this pitch explicitly; both chroma planes match. This
    // stable allocation belongs to the raster, not to an individual lock.
    const uint32_t pitch=(record.width+255)&~255u;
    const uint32_t storage=record.videoStaging;
    snapshotNeed(storage&&record.videoPitch==pitch,"movie plane original allocation/pitch changed");
    s.runtime.pointer(storage,pitch*record.height,true);
    // X+C deliberately stays zero: no console SDK surface is fabricated.
    // Retain the original root/mode5/level0 CPU publication field set.
    PPC_STORE_U32(x+0x10,pitch);PPC_STORE_U32(x+0x14,storage);
    PPC_STORE_U8(r+0x22,4);PPC_STORE_U32(r+4,storage);
    PPC_STORE_U32(r+0x2C,record.height);PPC_STORE_U32(r+0x28,record.width);
    PPC_STORE_U32(r+0xC,record.width);PPC_STORE_U32(r+0x10,record.height);
    PPC_STORE_U32(r+0x18,pitch);PPC_STORE_U8(x+0xB,0);PPC_STORE_U32(c.r3.u32,storage);
    record.videoHasLock=true;record.videoLocked=true;
    c.r3.u64=1;c.lr=0x82408238;
}
void EngineRasters::unlockMovieRaster(PPCContext& c,uint8_t* base) {
    ++state->recordsVersion; // invalidates the camera-surface memo
    auto& s=*state;s.requireOwner(base);
    snapshotNeed(currentContext==&c&&s.planeAccess&&s.planeAccess->unlock,"movie unlock callback has no original wrapper scope");
    const auto request=*s.planeAccess;
    struct End {State& state;~End(){state.planeAccess.reset();}} end{s};
    auto& record=s.records.at(request.raster);const uint32_t r=request.raster;
    s.validateMetadata(base,r,record);
    snapshotNeed(c.r1.u32==request.stack-0x60&&!c.r3.u32&&c.r4.u32==r&&!c.r5.u32&&c.r31.u32==r&&
        uint32_t(c.lr)==request.caller&&c.r11.u32==0x823F5588&&c.ctr.u32==0x823F5588&&
        PPC_LOAD_U32(s.engine+0x88)==0x823F5588&&record.videoLocked,"movie unlock callback original ABI/table differs");
    s.runtime.pointer(r,record.total,true);
    const auto* source=s.runtime.pointer(record.videoStaging,record.videoPitch*record.height,false);
    std::vector<uint8_t> pixels(size_t(record.width)*record.height);
    for(uint32_t y=0;y<record.height;++y)
        std::copy_n(source+size_t(y)*record.videoPitch,record.width,pixels.data()+size_t(y)*record.width);
    s.backend.writeTexture(record.texture,pixels);
    // Upload owns the logical rows before publishing unlocked state. Keep the
    // allocation and cached X+10/+14 pointer/pitch alive for original reuse.
    PPC_STORE_U32(r+0x18,0);PPC_STORE_U32(r+4,0);
    PPC_STORE_U32(r+0xC,record.width);PPC_STORE_U32(r+0x10,record.height);PPC_STORE_U8(r+0x22,0);
    if(request.presenter)record.videoPresenter=request.presenter;
    record.videoLocked=false;record.videoInitialized=true;c.r3.u64=1;c.lr=0x82407BDC;
}
}
