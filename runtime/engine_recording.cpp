#include "engine_recording.h"
#include "runtime.h"
#include "engine_driver.h"
#include "engine_effects.h"
#include "renderer/native_backend.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
// Keep this last: shared header bodies retain their normal definitions.
#include "aot_inline_memory.h"

namespace Simpsons {
namespace {
constexpr uint32_t singleton=0x82D09784,poolHead=0x82DFE10C,event=0x82D61DD0;
std::atomic<uint32_t> nextIdentity{0x00600001};
uint32_t reserveIdentity(Runtime& rt) {
    uint32_t id=nextIdentity.load();
    while(id<0x00700000) if(nextIdentity.compare_exchange_weak(id,id+1)) {
        if(rt.pageAccess[id>>12].load()) throw Failure("Recording identity overlaps guest memory");
        return id;
    }
    throw Failure("Native recording identity space exhausted");
}
bool overlaps(uint32_t a,uint32_t n,uint32_t b,uint32_t m) {
    return uint64_t(a)<uint64_t(b)+m && uint64_t(b)<uint64_t(a)+n;
}
}

struct EngineRecordingOwners::State {
    Runtime& runtime;
    Graphics::NativeBackend& backend;
    const DWORD thread;
    const uint32_t engine;
    enum class Phase {Prepared,Committed,Destroying};
    struct EventNode {
        uint32_t address,receiver;uint16_t refs,priority;
        bool operator==(const EventNode&) const=default;
    };
    struct EventView {
        uint32_t root;uint16_t registrations,descriptorRefs;uint32_t type;uint16_t flags;
        std::vector<EventNode> nodes;
        bool operator==(const EventView&) const=default;
    };
    struct Record {
        uint32_t owner,id,sp;
        PPCContext* cpu;
        Phase phase;
        std::array<uint32_t,2> aliases;
        uint32_t untouched60;
        std::vector<uint32_t> pools;
        EventView registration;
        std::shared_ptr<Graphics::NativeRecordingContext> native;
    };
    std::optional<Record> record;
    struct Session {
        PPCContext* cpu{};
        uint32_t sp{},typed{},object{},metadata{},previous{},bucket{},head{},cursor{},payloadId{},vertexId{},pixelId{},cpuRecord{};
        NativeCameraBinding camera;
        std::array<uint32_t,4> scissor{};
        Graphics::NativeRecordingMask mask{};
        std::shared_ptr<Graphics::RenderTarget> color;
        std::shared_ptr<Graphics::DepthTarget> depth;
        std::shared_ptr<Graphics::NativeRecordingPayload> payload;
        bool stateActive{};
        bool finishing{};
        uint32_t finishCursor{},queryBytes{};
        uint32_t previousPayload{},freeRecord{},oldHead{};
    };
    std::optional<Session> session;
    struct Payload {
        uint32_t id{},cpuRecord{},typed{},object{},metadata{},previous{},head{},bytes{};
        NativeCameraBinding camera;
        std::shared_ptr<Graphics::NativeRecordingPayload> native;
        bool completed{};
    };
    std::unordered_map<uint32_t,Payload> payloads;
    // Immutable completed-payload receipts (sealed/draws/bytes never change
    // after completion; executions is unchecked). Cached to avoid 90 GPU
    // GetData queries per graph() validation (28 validations per frame).
    // IDs are never reused (reserveIdentity increments); erased on payload
    // deletion to bound growth. Device removal throws in owned() before any
    // cache lookup, so stale cached receipts cannot hide removal.
    mutable std::unordered_map<uint32_t,Graphics::NativeRecordingReceipt> receiptCache;
    struct PayloadIndex {
        bool valid=false;
        uint32_t poolBlock=0,first=0,last=0;
        size_t count=0;
        std::array<const Payload*,2000> slots{};
        std::array<size_t,2000> batch{};
        std::vector<std::shared_ptr<Graphics::NativeRecordingPayload>> batchPayloads;
        std::vector<uint32_t> batchIds;
        std::vector<Graphics::NativeRecordingReceipt> batchReceipts;
    };
    mutable PayloadIndex payloadIndex;
    void invalidatePayloadIndex() { payloadIndex.valid=false;payloadIndex.count=0;payloadIndex.slots.fill(nullptr);payloadIndex.batch.fill(size_t(-1));payloadIndex.batchPayloads.clear();payloadIndex.batchIds.clear();payloadIndex.batchReceipts.clear(); }
    struct Replay {PPCContext* cpu{};uint32_t sp{},packet{},payloadId{};};
    std::optional<Replay> replay;
    struct PluginDeletion {PPCContext* cpu;uint32_t sp,object,head;std::vector<uint32_t> nodes;};
    std::optional<PluginDeletion> pluginDeletion;
    struct Deletion {
        PPCContext* cpu;uint32_t sp,node,payload,freeHead;
        std::array<uint32_t,13> words;
        std::array<uint32_t,2> aliases;
        bool released{};
    };
    std::optional<Deletion> deletion;
    struct Node {uint32_t address,payload,head,metadata,next;};
    std::vector<Node> nodes; // Original allocation order; no CPU storage is owned here.
    std::vector<uint32_t> freeOrder,lru;
    uint32_t poolBlock{},successCount{},capturedPayloads{};
    uint64_t lruTouchCount{}; // Non-const observe path only; decides periodic full graph validation.
    static constexpr std::array<uint32_t,9> historyOffsets={0x54,0x58,0x5C,0x60,0x64,0x6C,0x70,0x74,0x78};
    std::array<uint32_t,9> history{};
    struct Observation {PPCContext* cpu;uint32_t sp,lr,packet,payload,node;};
    std::optional<Observation> touch,touched,reset,quota;
    struct FreePlan {
        bool valid=false;
        uint32_t poolBlock=0,first=0,last=0;
        size_t freeCount=0;
        std::vector<size_t> offsets;
        std::vector<uint32_t> expectedRaw;
        std::array<uint8_t,2000> baseClaimed{};
    };
    mutable FreePlan freePlan;
    // Bounded private host-topology proofs: live slot/offset/backlink per
    // nodes-vector order plus distinct head/root pairs. No guest pointers,
    // no guest values, no completed/pending status. Correctness derives from
    // explicit invalidation at the only State.nodes/freeOrder mutation sites:
    // initializeGraph, observeBegin 826F4F48 (push+free erase), deletion
    // 827374A4 (next-link edit+node erase+free insert), all via
    // invalidateFreePlan() BEFORE mutation. Payload completion and LRU touch
    // do not change node topology and deliberately do not invalidate here.
    struct TopologyPlan {
        bool valid=false;
        uint32_t poolBlock=0,first=0,last=0;
        size_t nodeCount=0,freeCount=0;
        std::vector<uint32_t> slots;
        std::vector<size_t> offsets;
        std::vector<uint32_t> backlinks;
        std::vector<uint32_t> distinctHeads;
        std::vector<uint32_t> distinctNodes;
    };
    mutable TopologyPlan topologyPlan;
    void invalidateTopologyPlan() { topologyPlan.valid=false;topologyPlan.nodeCount=0;topologyPlan.freeCount=0;topologyPlan.slots.clear();topologyPlan.offsets.clear();topologyPlan.backlinks.clear();topologyPlan.distinctHeads.clear();topologyPlan.distinctNodes.clear(); }
    void invalidateFreePlan() { freePlan.valid=false;freePlan.freeCount=0;freePlan.offsets.clear();freePlan.expectedRaw.clear();freePlan.baseClaimed.fill(0);invalidateTopologyPlan(); }
    State(Runtime& rt,Graphics::NativeBackend& graphics):runtime(rt),backend(graphics),
        thread(GetCurrentThreadId()),engine(PPCLoadU32(rt.base,0x82D0CA68)) {}
    void caller(uint8_t* base) const {
        if(active!=&runtime || base!=runtime.base || GetCurrentThreadId()!=thread)
            throw Failure("Native recording owner has the wrong runtime/thread");
        runtime.checkRunning();
    }
    void engineOwner(uint8_t* base) const {
        caller(base);
        if(!engine || PPC_LOAD_U32(0x82D0CA68)!=engine || PPC_LOAD_U32(0x82D0CAF8))
            throw Failure("Native recording engine owner or console-device invariant changed");
        backend.validateSubmissionContext();
    }
    void frame(PPCContext& ctx,uint8_t* base) const {
        engineOwner(base);
        if(currentContext!=&ctx || ctx.r1.u32<0x100 || (ctx.r1.u32&15))
            throw Failure("Invalid original recording callback context/stack");
        runtime.pointer(ctx.r1.u32-0x100,0x100,true);
    }
    uint8_t* region(uint32_t a,uint32_t n,bool write=false) const {
        if(!a || (a&3) || uint64_t(a)+n>0x100000000ull)
            throw Failure("Invalid original recording object bounds");
        return runtime.pointer(a,n,write);
    }
    std::array<uint32_t,2> aliases(uint8_t* base) const {
        return {PPC_LOAD_U32(0x82D6D890),PPC_LOAD_U32(0x82D63028)};
    }
    std::vector<uint32_t> pools(uint8_t* base) const {
        std::vector<uint32_t> result;std::unordered_set<uint32_t> seen;
        for(uint32_t p=PPC_LOAD_U32(poolHead);p;p=PPC_LOAD_U32(p+4)) {
            if(result.size()>=65536 || !seen.insert(p).second)
                throw Failure("Invalid original CPU pool registry chain");
            region(p,0x38,true);result.push_back(p);
        }
        return result;
    }
    EventView registration(uint8_t* base) const {
        EventView out{PPC_LOAD_U32(event),PPC_LOAD_U16(event+4),PPC_LOAD_U16(event+6),0,0,{}};
        if(!out.root) return out;
        region(out.root,0x20,true);
        out.type=PPC_LOAD_U32(out.root+0x10);out.flags=PPC_LOAD_U16(out.root+0x1E);
        uint32_t previous=out.root;std::unordered_set<uint32_t> seen{out.root};
        for(uint32_t at=PPC_LOAD_U32(out.root);at;at=PPC_LOAD_U32(at)) {
            if(out.nodes.size()>=65536 || !seen.insert(at).second)
                throw Failure("Invalid recording CPU registration chain");
            region(at,0x10,true);
            if(PPC_LOAD_U32(at+4)!=previous || !PPC_LOAD_U16(at+0xC))
                throw Failure("Invalid recording CPU registration links/refcount");
            out.nodes.push_back({at,PPC_LOAD_U32(at+8),PPC_LOAD_U16(at+0xC),PPC_LOAD_U16(at+0xE)});
            previous=at;
        }
        return out;
    }
    static size_t receivers(const EventView& view,uint32_t receiver) {
        return size_t(std::count_if(view.nodes.begin(),view.nodes.end(),[&](const auto& n){return n.receiver==receiver;}));
    }
    void validateRegistration(const Record& r,uint8_t* base) const {
        const auto now=registration(base);
        if(now.root!=r.registration.root || receivers(now,r.owner+4)!=(now.root?1u:0u))
            throw Failure("Original recording callback registration is missing or duplicated");
        for(const auto& n:now.nodes) if(n.receiver==r.owner+4 && (n.refs!=1 || n.priority!=0x8000))
            throw Failure("Unsupported recording callback reference/priority state");
    }
    void emptyFields(uint32_t o,uint8_t* base) const {
        for(uint32_t off:{0x44u,0x48u,0x4Cu,0x50u,0x54u,0x58u,0x5Cu,0x64u,0x6Cu,0x70u,0x74u,0x78u})
            if(PPC_LOAD_U32(o+off)) throw Failure("Recording owner has live or unsupported record/state fields");
    }
    void pool(uint32_t o,uint8_t* base) const {
        const uint32_t p=o+0xC;
        if(PPC_LOAD_U32(p)!=0x820B8558 || PPC_LOAD_U32(p+8)!=0x820B8570)
            throw Failure("Original recording pool type/name changed");
        constexpr std::array<uint32_t,9> expected={2000,0,2000,2000,1,4,0,0x34,0x34};
        for(uint32_t i=0;i<9;++i) {
            const uint32_t offset=0xC+4*i,actual=PPC_LOAD_U32(p+offset);
            if(actual!=expected[i]) {
                char message[192];std::snprintf(message,sizeof(message),
                    "Recording pool metadata mismatch: pool=%08X offset=%02X expected=%08X actual=%08X",p,offset,expected[i],actual);
                throw Failure(message);
            }
        }
        const uint32_t block=PPC_LOAD_U32(p+0x30);
        constexpr uint32_t bytes=2000*0x34+4+0x13;
        region(block,bytes,true);
        if(overlaps(block,bytes,o,0x7C) || PPC_LOAD_U32(block) || PPC_LOAD_U32(block+4))
            throw Failure("Invalid recording pool block ownership/links");
        const uint32_t first=(block+4+0x13)&~3u,last=first+1999*0x34;
        if(PPC_LOAD_U32(block+8)!=first || PPC_LOAD_U32(block+0xC)!=last)
            throw Failure("Original recording pool block item bounds changed");
        std::array<bool,2000> visited{};uint32_t at=PPC_LOAD_U32(p+0x34),count=0;
        while(at) {
            if(at<first || at>last || (at-first)%0x34 || count==2000 || visited[(at-first)/0x34])
                throw Failure("Invalid original recording pool free list");
            visited[(at-first)/0x34]=true;++count;at=PPC_LOAD_U32(at);
        }
        if(count!=2000) throw Failure("Original recording pool retains borrowed records");
    }
    Record& find(uint32_t o,uint32_t id) {
        if(!record || record->owner!=o || record->id!=id || record->phase!=Phase::Committed)
            throw Failure("Missing, stale or incomplete native recording owner");
        return *record;
    }
    void owned(const Record& r,uint8_t* base) const {
        region(r.owner,0x7C,true);
        if(PPC_LOAD_U32(singleton)!=r.owner || PPC_LOAD_U32(r.owner)!=0x820B856C ||
           PPC_LOAD_U32(r.owner+4)!=0x820B8564 || PPC_LOAD_U32(r.owner+8)!=1 || PPC_LOAD_U32(r.owner+0x68)!=r.id)
            throw Failure("Original recording owner identity/vtable changed");
        if(runtime.pageAccess[r.id>>12].load()) throw Failure("Native recording identity became mapped");
        backend.validateRecordingContext(r.native);
    }
    void saveHistory(uint8_t* base) {
        for(size_t i=0;i<history.size();++i)history[i]=PPC_LOAD_U32(record->owner+historyOffsets[i]);
    }
    void idleHistory(uint8_t* base) const {
        for(size_t i=0;i<history.size();++i)
            if(PPC_LOAD_U32(record->owner+historyOffsets[i])!=history[i])
                throw Failure("Idle recording manager history changed outside original begin/finish");
    }
    void retiredGraph(uint8_t* base) const {
        if(session||replay||touch||touched||reset||quota||pluginDeletion||deletion)
            throw Failure("Recording destruction overlaps an active original operation");
        if(!payloads.empty()||!receiptCache.empty()||!nodes.empty()||!lru.empty()||freeOrder.size()!=2000)
            throw Failure("Recording destruction retains cached payload or CPU record ownership");
        // Original successful finish826F501C increments owner+50. Cache
        // deletion826F4BE8 clears only owned bytes/LRU accounting; neither
        // it nor the manager destructor826F3908 clears last-record history.
        // Validate the original empty pool/accounting partition and exact
        // saved history instead of treating dead values as live ownership.
        graph(base); // Includes owner+50 == successCount and zero LRU/bytes.
        idleHistory(base);
    }
    void initializeGraph(uint8_t* base) {
        if(!payloads.empty()||session||replay||touch||touched||reset||quota||pluginDeletion||deletion)
            throw Failure("Recording construction overlaps retained payload/operation ownership");
        invalidatePayloadIndex();
        invalidateFreePlan();
        nodes.clear();lru.clear();freeOrder.clear();successCount=0;
        nodes.reserve(2000);lru.reserve(2000);freeOrder.reserve(2000);payloads.reserve(2000);
        poolBlock=PPC_LOAD_U32(record->owner+0x3C);
        for(uint32_t at=PPC_LOAD_U32(record->owner+0x40);at;at=PPC_LOAD_U32(at))freeOrder.push_back(at);
        saveHistory(base);
    }
    // Check the entire one-block partition and both independent CPU graphs.
    // Free-slot payload words are deliberately ignored: only their links live.
    // Performance: pool slots are read through one freshly checked span with
    // volatile endian-aware loads confined to that span; all lookup tables are
    // bounded stack storage (direct slot indices plus open-addressing tables).
    // Every original check still executes each invocation. Invalid, misaligned
    // or foreign addresses reject before any span indexing.
    void graph(uint8_t* base) const {
        if(!record||record->phase!=Phase::Committed)throw Failure("Recording graph lacks a committed manager");
        owned(*record,base);
        const uint32_t o=record->owner,p=o+0xC;
        constexpr std::array<uint32_t,7> constants={2000,2000,1,4,0,0x34,0x34};
        if(PPC_LOAD_U32(p)!=0x820B8558||PPC_LOAD_U32(p+8)!=0x820B8570||
           nodes.size()+freeOrder.size()!=2000||PPC_LOAD_U32(p+0xC)!=freeOrder.size()||PPC_LOAD_U32(p+0x10)!=nodes.size())
            throw Failure("Recording pool partition counts/type differ");
        for(size_t i=0;i<constants.size();++i)if(PPC_LOAD_U32(p+0x14+4*uint32_t(i))!=constants[i])
            throw Failure("Recording pool growth/stride policy changed");
        if(PPC_LOAD_U32(p+0x30)!=poolBlock)throw Failure("Recording pool block ownership changed");
        constexpr uint32_t poolBytes=2000*0x34+4+0x13;
        uint8_t* const poolSpan=region(poolBlock,poolBytes,true);
        const uint32_t first=(poolBlock+4+0x13)&~3u,last=first+1999*0x34;
        if(PPC_LOAD_U32(poolBlock)||PPC_LOAD_U32(poolBlock+4)||PPC_LOAD_U32(poolBlock+8)!=first||PPC_LOAD_U32(poolBlock+12)!=last)
            throw Failure("Recording pool block bounds/links changed");
        if(!freePlan.valid||freePlan.poolBlock!=poolBlock||freePlan.first!=first||freePlan.last!=last||freePlan.freeCount!=freeOrder.size()) {
            FreePlan staged;
            staged.poolBlock=poolBlock;staged.first=first;staged.last=last;
            staged.freeCount=freeOrder.size();
            staged.offsets.resize(freeOrder.size());
            staged.expectedRaw.resize(freeOrder.size());
            std::array<uint8_t,2000> seen{};
            for(size_t i=0;i<freeOrder.size();++i) {
                const uint32_t addr=freeOrder[i];
                if(addr<first||addr>last||(addr-first)%0x34)
                    throw Failure("Recording pool has a foreign, duplicate or overlapping slot");
                const size_t slot=(addr-first)/0x34;
                if(seen[slot])throw Failure("Recording pool has a foreign, duplicate or overlapping slot");
                seen[slot]=1;
                staged.baseClaimed[slot]=1;
                staged.offsets[i]=size_t(addr-poolBlock);
                const uint32_t next=(i+1<freeOrder.size())?freeOrder[i+1]:0;
                staged.expectedRaw[i]=__builtin_bswap32(next);
            }
            staged.valid=true;
            freePlan=std::move(staged);
        }
        if(PPC_LOAD_U32(p+0x34)!=(freeOrder.empty()?0:freeOrder.front()))
            throw Failure("Original recording free-list order changed");
        {
            const size_t linkCount=freePlan.offsets.size();
            const size_t* linkOff=freePlan.offsets.data();
            const uint32_t* linkRaw=freePlan.expectedRaw.data();
            size_t linkIdx=0;
            const size_t bulkEnd=(linkCount>1)?((linkCount-1)/4)*4:0;
            for(;linkIdx<bulkEnd;linkIdx+=4) {
                const uint32_t r0=*(volatile uint32_t*)(poolSpan+linkOff[linkIdx]);
                const uint32_t r1=*(volatile uint32_t*)(poolSpan+linkOff[linkIdx+1]);
                const uint32_t r2=*(volatile uint32_t*)(poolSpan+linkOff[linkIdx+2]);
                const uint32_t r3=*(volatile uint32_t*)(poolSpan+linkOff[linkIdx+3]);
                if(r0!=linkRaw[linkIdx]||r1!=linkRaw[linkIdx+1]||r2!=linkRaw[linkIdx+2]||r3!=linkRaw[linkIdx+3])
                    throw Failure("Original recording free-list order changed");
            }
            for(;linkIdx<linkCount;++linkIdx) {
                if(*(volatile uint32_t*)(poolSpan+linkOff[linkIdx])!=linkRaw[linkIdx])
                    throw Failure(linkIdx+1==linkCount?"Original recording free list has an unowned suffix":"Original recording free-list order changed");
            }
        }
        if(!topologyPlan.valid||topologyPlan.poolBlock!=poolBlock||topologyPlan.first!=first||topologyPlan.last!=last||topologyPlan.nodeCount!=nodes.size()||topologyPlan.freeCount!=freeOrder.size()) {
            TopologyPlan staged;
            staged.poolBlock=poolBlock;staged.first=first;staged.last=last;
            staged.nodeCount=nodes.size();staged.freeCount=freeOrder.size();
            staged.slots.reserve(nodes.size());staged.offsets.reserve(nodes.size());staged.backlinks.reserve(nodes.size());
            staged.distinctHeads.reserve(nodes.size());staged.distinctNodes.reserve(nodes.size());
            std::array<uint8_t,2000> claimed=freePlan.baseClaimed;
            const auto claim=[&](uint32_t at) {
                if(at<first||at>last||(at-first)%0x34||claimed[(at-first)/0x34])
                    throw Failure("Recording pool has a foreign, duplicate or overlapping slot");
                claimed[(at-first)/0x34]=1;
            };
            const auto pow2Cap=[](size_t need)->size_t {
                size_t cap=2;
                while(cap<need&&cap<4096)cap<<=1;
                return cap>4096?4096:cap;
            };
            const size_t headCap=pow2Cap(nodes.size()*2);
            const size_t headMask=headCap-1;
            struct HeadEntry{uint32_t head;uint32_t node;uint32_t distinct;bool used;};
            std::array<HeadEntry,4096> headTable;
            for(size_t i=0;i<headCap;++i)headTable[i].used=false;
            std::array<uint32_t,2000> distinctHeads;
            std::array<uint32_t,2000> distinctNodes;
            size_t distinctCount=0;
            std::array<uint64_t,4096> keyTable;
            std::array<uint8_t,4096> keyUsed;
            for(size_t i=0;i<headCap;++i)keyUsed[i]=0;
            std::array<uint32_t,2000> backlinkVal;
            std::array<uint8_t,2000> backlinkSet;
            for(size_t i=0;i<backlinkSet.size();++i)backlinkSet[i]=0;
            std::array<uint32_t,2000> liveSlots;
            std::array<size_t,2000> liveOffsets;
            for(size_t ni=0;ni<nodes.size();++ni) {
                const auto& n=nodes[ni];
                claim(n.address);
                if(n.head<0xB4)throw Failure("Recording object-chain ownership/key differs");
                uint32_t hx=n.head*2654435761u;hx^=hx>>16;
                const size_t hi=hx&headMask;
                size_t foundIdx=headCap;uint32_t prev=0;bool found=false;
                for(size_t k=0;k<headCap;++k) {
                    const size_t j=(hi+k)&headMask;
                    if(!headTable[j].used){foundIdx=j;break;}
                    if(headTable[j].head==n.head){found=true;prev=headTable[j].node;foundIdx=j;break;}
                }
                if(foundIdx>=headCap)throw Failure("Recording object-chain ownership/key differs");
                if(n.next!=prev)throw Failure("Recording object-chain ownership/key differs");
                const uint64_t key=(uint64_t(n.head)<<32)|n.metadata;
                uint32_t kx=uint32_t(key)*2654435761u^uint32_t(key>>32)*2246822519u;kx^=kx>>16;
                const size_t ki=kx&headMask;
                bool dup=false;size_t kinsert=headCap;
                for(size_t k=0;k<headCap;++k) {
                    const size_t j=(ki+k)&headMask;
                    if(!keyUsed[j]){kinsert=j;break;}
                    if(keyTable[j]==key){dup=true;break;}
                }
                if(dup||kinsert>=headCap)throw Failure("Recording object-chain ownership/key differs");
                keyTable[kinsert]=key;keyUsed[kinsert]=1;
                const size_t selfIdx=(n.address-first)/0x34;
                if(n.next) {
                    if(n.next<first||n.next>last||(n.next-first)%0x34)
                        throw Failure("Recording object-chain ownership/key differs");
                    const size_t nextIdx=(n.next-first)/0x34;
                    backlinkVal[nextIdx]=n.address+0xC;backlinkSet[nextIdx]=1;
                }
                backlinkVal[selfIdx]=n.head;backlinkSet[selfIdx]=1;
                liveSlots[ni]=uint32_t(selfIdx);
                liveOffsets[ni]=size_t(n.address-poolBlock);
                if(found) {
                    headTable[foundIdx].node=n.address;
                    distinctNodes[headTable[foundIdx].distinct]=n.address;
                } else {
                    headTable[foundIdx].head=n.head;headTable[foundIdx].node=n.address;
                    headTable[foundIdx].distinct=uint32_t(distinctCount);headTable[foundIdx].used=true;
                    distinctHeads[distinctCount]=n.head;distinctNodes[distinctCount]=n.address;++distinctCount;
                }
            }
            for(size_t ni=0;ni<nodes.size();++ni) {
                if(!backlinkSet[liveSlots[ni]])throw Failure("Original recording node links/status/bytes/patch fields differ");
                staged.slots.push_back(liveSlots[ni]);
                staged.offsets.push_back(liveOffsets[ni]);
                staged.backlinks.push_back(backlinkVal[liveSlots[ni]]);
            }
            for(size_t i=0;i<distinctCount;++i) {
                staged.distinctHeads.push_back(distinctHeads[i]);
                staged.distinctNodes.push_back(distinctNodes[i]);
            }
            staged.valid=true;
            topologyPlan=std::move(staged);
        }
        for(size_t i=0;i<topologyPlan.distinctHeads.size();++i) {
            const uint32_t head=topologyPlan.distinctHeads[i],node=topologyPlan.distinctNodes[i];
            uint8_t* const span=region(head-0xB4,0xC0,true);
            const uint32_t root=__builtin_bswap32(*(volatile uint32_t*)(span+0xB4));
            const uint32_t before=__builtin_bswap32(*(volatile uint32_t*)(span+0xB0));
            const uint32_t after0=__builtin_bswap32(*(volatile uint32_t*)(span+0xB8));
            const uint32_t after1=__builtin_bswap32(*(volatile uint32_t*)(span+0xBC));
            if(root!=node||before||after0||after1)
                throw Failure("Original recording cache root/bucket scope differs");
        }
        if(lru.size()>2000)throw Failure("Native recording LRU receipt contains a duplicate");
        std::array<uint16_t,2000> lruPos;
        lruPos.fill(UINT16_MAX);
        for(size_t i=0;i<lru.size();++i) {
            const uint32_t addr=lru[i];
            if(addr<first||addr>last||(addr-first)%0x34)
                throw Failure("Native recording LRU receipt contains a duplicate");
            const size_t slot=(addr-first)/0x34;
            if(lruPos[slot]!=UINT16_MAX)
                throw Failure("Native recording LRU receipt contains a duplicate");
            lruPos[slot]=uint16_t(i);
        }
        if(!payloadIndex.valid||payloadIndex.poolBlock!=poolBlock||payloadIndex.first!=first||payloadIndex.last!=last||payloadIndex.count!=payloads.size()) {
            PayloadIndex staged;
            staged.poolBlock=poolBlock;staged.first=first;staged.last=last;staged.count=payloads.size();
            staged.slots.fill(nullptr);
            staged.batch.fill(size_t(-1));
            staged.batchPayloads.reserve(payloads.size());
            staged.batchIds.reserve(payloads.size());
            staged.batchReceipts.reserve(payloads.size());
            for(const auto& kv:payloads) {
                const Payload& value=kv.second;
                if(value.cpuRecord<first||value.cpuRecord>last||(value.cpuRecord-first)%0x34)
                    throw Failure("Recording pool has a foreign, duplicate or overlapping slot");
                const size_t slot=(value.cpuRecord-first)/0x34;
                if(staged.slots[slot])throw Failure("Recording pool has a foreign, duplicate or overlapping slot");
                staged.slots[slot]=&value;
                staged.batch[slot]=staged.batchPayloads.size();
                staged.batchPayloads.push_back(value.native);
                staged.batchIds.push_back(kv.first);
                auto cached=receiptCache.find(kv.first);
                if(cached!=receiptCache.end()) {
                    staged.batchReceipts.push_back(cached->second);
                } else {
                    const auto receipt=backend.recordingPayloadReceipt(value.native);
                    receiptCache.emplace(kv.first,receipt);
                    staged.batchReceipts.push_back(receipt);
                }
            }
            staged.valid=true;
            payloadIndex=std::move(staged);
        } else {
            payloadIndex.batchReceipts.resize(payloadIndex.batchPayloads.size());
            for(size_t i=0;i<payloadIndex.batchPayloads.size();++i) {
                const uint32_t id=payloadIndex.batchIds[i];
                auto cached=receiptCache.find(id);
                if(cached!=receiptCache.end()) {
                    payloadIndex.batchReceipts[i]=cached->second;
                } else {
                    const auto receipt=backend.recordingPayloadReceipt(payloadIndex.batchPayloads[i]);
                    receiptCache.emplace(id,receipt);
                    payloadIndex.batchReceipts[i]=receipt;
                }
            }
        }
        uint64_t bytes=0;size_t completed=0,pending=0;
        for(size_t ni=0;ni<nodes.size();++ni) {
            const auto& n=nodes[ni];
            const size_t selfIdx=topologyPlan.slots[ni];
            const uint32_t backlink=topologyPlan.backlinks[ni];
            uint32_t exp0=0,exp1=0,exp5=0,exp11=0;
            const Payload* slotPayload=payloadIndex.slots[selfIdx];
            if(slotPayload&&slotPayload->id!=n.payload)
                throw Failure("Completed recording CPU/native payload ownership differs");
            const Payload* payload=(slotPayload&&slotPayload->completed)?slotPayload:nullptr;
            if(payload) {
                const auto& value=*payload;
                const size_t batchSlot=payloadIndex.batch[selfIdx];
                if(batchSlot>=payloadIndex.batchReceipts.size())
                    throw Failure("Completed recording CPU/native payload ownership differs");
                const auto receipt=payloadIndex.batchReceipts[batchSlot];
                const uint16_t posRaw=lruPos[selfIdx];const bool hasPos=(posRaw!=UINT16_MAX);
                if(value.cpuRecord!=n.address||value.head!=n.head||value.object+0xB4!=n.head||value.metadata!=n.metadata||
                   value.id!=n.payload||!hasPos||receipt.state!=Graphics::NativeRecordingPayloadState::Sealed||
                   !receipt.recordedDraws||receipt.ownedDataBytes!=value.bytes||runtime.pageAccess[n.payload>>12].load())
                    throw Failure("Completed recording CPU/native payload ownership differs");
                const size_t i=size_t(posRaw);exp0=i+1<lru.size()?lru[i+1]:0;exp1=i?lru[i-1]:0;
                exp5=value.bytes;exp11=2;bytes+=value.bytes;++completed;
            } else {
                const bool hasPos=(lruPos[selfIdx]!=UINT16_MAX);
                if(!session||session->cpuRecord!=n.address||session->payloadId!=n.payload||hasPos)
                    throw Failure("Borrowed recording slot has no active or completed payload");
                ++pending;
            }
            const uint32_t expected[13]={exp0,exp1,backlink,n.next,0,exp5,0,n.metadata,0,0,n.payload,exp11,0};
            const size_t baseOff=topologyPlan.offsets[ni];
            for(size_t i=0;i<13;++i) {
                const uint32_t raw=*(volatile uint32_t*)(poolSpan+baseOff+4*i);
                if(__builtin_bswap32(raw)!=expected[i])
                    throw Failure("Original recording node links/status/bytes/patch fields differ");
            }
        }
        if(completed!=lru.size()||completed+pending!=nodes.size()||pending>1||
           payloads.size()!=completed+(session&&payloads.contains(session->payloadId)&&!payloads.at(session->payloadId).completed?1:0)||
           bytes>UINT32_MAX||PPC_LOAD_U32(o+0x4C)!=bytes||PPC_LOAD_U32(o+0x50)!=successCount||
           PPC_LOAD_U32(o+0x44)!=(lru.empty()?0:lru.front())||PPC_LOAD_U32(o+0x48)!=(lru.empty()?0:lru.back()))
            throw Failure("Original recording LRU/accounting differs from all owned completed payloads");
    }
    // Fast path for observeLruTouch: only the LRU order changed (one node moved
    // to front); node topology, free list, payload set and pool block are
    // unchanged since the last full graph() validation. Re-validates the cheap
    // global counts, the new LRU order, the touched node plus its old/new
    // neighbours (whose exp0/exp1 LRU linkage changed), the touched payload's
    // single GPU receipt, distinct cache roots, and final accounting. Skips the
    // 1910-entry free-list memory re-scan, the full 90-node pool re-scan, and
    // the 90-payload batch receipt query. Callers run a full graph() every
    // 120th LRU touch (and whenever cached plans are invalid) so unrelated
    // corruption is still caught with bounded delay. Every check below uses the
    // same exact expected values and failure messages as the corresponding full
    // graph() section.
    void graphLruTouchFast(uint8_t* base,uint32_t touchedAddr,uint32_t oldFront,uint32_t oldPrev,uint32_t oldNext) const {
        if(!record||record->phase!=Phase::Committed)throw Failure("Recording graph lacks a committed manager");
        owned(*record,base);
        const uint32_t o=record->owner,p=o+0xC;
        constexpr std::array<uint32_t,7> constants={2000,2000,1,4,0,0x34,0x34};
        if(PPC_LOAD_U32(p)!=0x820B8558||PPC_LOAD_U32(p+8)!=0x820B8570||
           nodes.size()+freeOrder.size()!=2000||PPC_LOAD_U32(p+0xC)!=freeOrder.size()||PPC_LOAD_U32(p+0x10)!=nodes.size())
            throw Failure("Recording pool partition counts/type differ");
        for(size_t i=0;i<constants.size();++i)if(PPC_LOAD_U32(p+0x14+4*uint32_t(i))!=constants[i])
            throw Failure("Recording pool growth/stride policy changed");
        if(PPC_LOAD_U32(p+0x30)!=poolBlock)throw Failure("Recording pool block ownership changed");
        constexpr uint32_t poolBytes=2000*0x34+4+0x13;
        region(poolBlock,poolBytes,true);
        const uint32_t first=(poolBlock+4+0x13)&~3u,last=first+1999*0x34;
        if(PPC_LOAD_U32(poolBlock)||PPC_LOAD_U32(poolBlock+4)||PPC_LOAD_U32(poolBlock+8)!=first||PPC_LOAD_U32(poolBlock+12)!=last)
            throw Failure("Recording pool block bounds/links changed");
        if(!freePlan.valid||freePlan.poolBlock!=poolBlock||freePlan.first!=first||freePlan.last!=last||freePlan.freeCount!=freeOrder.size())
            throw Failure("Recording pool has a foreign, duplicate or overlapping slot");
        if(!topologyPlan.valid||topologyPlan.poolBlock!=poolBlock||topologyPlan.first!=first||topologyPlan.last!=last||
           topologyPlan.nodeCount!=nodes.size()||topologyPlan.freeCount!=freeOrder.size()||
           topologyPlan.slots.size()!=nodes.size()||topologyPlan.offsets.size()!=nodes.size()||topologyPlan.backlinks.size()!=nodes.size())
            throw Failure("Recording object-chain ownership/key differs");
        if(!payloadIndex.valid||payloadIndex.poolBlock!=poolBlock||payloadIndex.first!=first||payloadIndex.last!=last||payloadIndex.count!=payloads.size())
            throw Failure("Completed recording CPU/native payload ownership differs");
        if(PPC_LOAD_U32(p+0x34)!=(freeOrder.empty()?0:freeOrder.front()))
            throw Failure("Original recording free-list order changed");
        for(size_t i=0;i<topologyPlan.distinctHeads.size();++i) {
            const uint32_t head=topologyPlan.distinctHeads[i],node=topologyPlan.distinctNodes[i];
            uint8_t* const span=region(head-0xB4,0xC0,true);
            const uint32_t root=__builtin_bswap32(*(volatile uint32_t*)(span+0xB4));
            const uint32_t before=__builtin_bswap32(*(volatile uint32_t*)(span+0xB0));
            const uint32_t after0=__builtin_bswap32(*(volatile uint32_t*)(span+0xB8));
            const uint32_t after1=__builtin_bswap32(*(volatile uint32_t*)(span+0xBC));
            if(root!=node||before||after0||after1)
                throw Failure("Original recording cache root/bucket scope differs");
        }
        if(lru.empty()||lru.size()>2000)throw Failure("Native recording LRU receipt contains a duplicate");
        if(lru.front()!=touchedAddr)throw Failure("Original recording LRU/accounting differs from all owned completed payloads");
        std::array<uint16_t,2000> lruPos;
        lruPos.fill(UINT16_MAX);
        for(size_t i=0;i<lru.size();++i) {
            const uint32_t addr=lru[i];
            if(addr<first||addr>last||(addr-first)%0x34)
                throw Failure("Native recording LRU receipt contains a duplicate");
            const size_t slot=(addr-first)/0x34;
            if(lruPos[slot]!=UINT16_MAX)
                throw Failure("Native recording LRU receipt contains a duplicate");
            lruPos[slot]=uint16_t(i);
        }
        uint8_t* const poolSpan=runtime.pointer(poolBlock,poolBytes,true);
        auto nodeIndexByAddress=[&](uint32_t addr)->size_t {
            for(size_t ni=0;ni<nodes.size();++ni) if(nodes[ni].address==addr) return ni;
            throw Failure("Original LRU touch lacks a completed owned node");
        };
        const size_t touchedNi=nodeIndexByAddress(touchedAddr);
        const auto& touchedNode=nodes[touchedNi];
        const auto touchedPayloadIt=payloads.find(touchedNode.payload);
        if(touchedPayloadIt==payloads.end()||!touchedPayloadIt->second.completed)
            throw Failure("Completed recording CPU/native payload ownership differs");
        const auto& touchedPayload=touchedPayloadIt->second;
        {
            auto cached=receiptCache.find(touchedPayload.id);
            const Graphics::NativeRecordingReceipt receipt=(cached!=receiptCache.end())?cached->second:
                receiptCache.emplace(touchedPayload.id,backend.recordingPayloadReceipt(touchedPayload.native)).first->second;
            const size_t selfSlot=(touchedAddr-first)/0x34;
            const uint16_t posRaw=lruPos[selfSlot];
            if(touchedPayload.cpuRecord!=touchedAddr||touchedPayload.cpuRecord!=touchedNode.address||
               touchedPayload.head!=touchedNode.head||touchedPayload.object+0xB4!=touchedNode.head||
               touchedPayload.metadata!=touchedNode.metadata||touchedPayload.id!=touchedNode.payload||
               posRaw==UINT16_MAX||receipt.state!=Graphics::NativeRecordingPayloadState::Sealed||
               !receipt.recordedDraws||receipt.ownedDataBytes!=touchedPayload.bytes||runtime.pageAccess[touchedNode.payload>>12].load())
                throw Failure("Completed recording CPU/native payload ownership differs");
        }
        uint32_t neighbours[4]={touchedAddr,oldFront,oldPrev,oldNext};
        size_t neighbourCount=0;
        {
            uint32_t distinct[4]={};
            for(uint32_t candidate:neighbours) {
                if(!candidate) continue;
                bool seen=false;
                for(size_t k=0;k<neighbourCount;++k) if(distinct[k]==candidate) {seen=true;break;}
                if(!seen) distinct[neighbourCount++]=candidate;
            }
            for(size_t k=0;k<neighbourCount;++k) neighbours[k]=distinct[k];
        }
        for(size_t k=0;k<neighbourCount;++k) {
            const uint32_t addr=neighbours[k];
            const size_t ni=nodeIndexByAddress(addr);
            const auto& n=nodes[ni];
            const uint32_t backlink=topologyPlan.backlinks[ni];
            const auto payloadIt=payloads.find(n.payload);
            if(payloadIt==payloads.end()||!payloadIt->second.completed)
                throw Failure("Completed recording CPU/native payload ownership differs");
            const auto& value=payloadIt->second;
            const uint16_t posRaw=lruPos[(addr-first)/0x34];
            if(value.cpuRecord!=n.address||value.head!=n.head||value.object+0xB4!=n.head||value.metadata!=n.metadata||
               value.id!=n.payload||posRaw==UINT16_MAX||runtime.pageAccess[n.payload>>12].load())
                throw Failure("Completed recording CPU/native payload ownership differs");
            if(addr==touchedAddr) {
                if(value.bytes!=touchedPayload.bytes)throw Failure("Completed recording CPU/native payload ownership differs");
            }
            const size_t i=size_t(posRaw);
            const uint32_t exp0=i+1<lru.size()?lru[i+1]:0,exp1=i?lru[i-1]:0;
            const uint32_t expected[13]={exp0,exp1,backlink,n.next,0,value.bytes,0,n.metadata,0,0,n.payload,2,0};
            const size_t baseOff=topologyPlan.offsets[ni];
            for(size_t f=0;f<13;++f) {
                const uint32_t raw=*(volatile uint32_t*)(poolSpan+baseOff+4*f);
                if(__builtin_bswap32(raw)!=expected[f])
                    throw Failure("Original recording node links/status/bytes/patch fields differ");
            }
        }
        {
            uint64_t totalBytes=0;size_t totalCompleted=0,pending=0;
            for(const auto& n:nodes) {
                const size_t selfIdx=(n.address-first)/0x34;
                const uint16_t posRaw=(selfIdx<2000)?lruPos[selfIdx]:UINT16_MAX;
                const auto it=payloads.find(n.payload);
                const bool isCompleted=(it!=payloads.end()&&it->second.completed&&posRaw!=UINT16_MAX);
                if(isCompleted) {totalBytes+=it->second.bytes;++totalCompleted;}
                else ++pending;
            }
            if(totalCompleted!=lru.size()||totalCompleted+pending!=nodes.size()||pending>1||
               payloads.size()!=totalCompleted+(session&&payloads.contains(session->payloadId)&&!payloads.at(session->payloadId).completed?1:0)||
               totalBytes>UINT32_MAX||PPC_LOAD_U32(o+0x4C)!=uint32_t(totalBytes)||PPC_LOAD_U32(o+0x50)!=successCount||
               PPC_LOAD_U32(o+0x44)!=(lru.empty()?0:lru.front())||PPC_LOAD_U32(o+0x48)!=(lru.empty()?0:lru.back()))
                throw Failure("Original recording LRU/accounting differs from all owned completed payloads");
        }
    }
};

EngineRecordingOwners::EngineRecordingOwners(Runtime& rt,Graphics::NativeBackend& backend):state(std::make_unique<State>(rt,backend)) {
    state->engineOwner(rt.base);
}
EngineRecordingOwners::~EngineRecordingOwners() {
    if(state->session&&state->session->payload) {
        if(state->session->stateActive&&state->runtime.engineDriver) {
            try {state->runtime.engineDriver->endRecordingState(state->record->id);}
            catch(const std::exception& e){std::fprintf(stderr,"[NATIVE RECORDING] terminal state restore: %s\n",e.what());}
        }
        if(!state->payloads.contains(state->session->payloadId)) {
            try {state->backend.releaseRecordingPayload(state->session->payload);}
            catch(const std::exception& e){std::fprintf(stderr,"[NATIVE RECORDING] terminal payload release: %s\n",e.what());}
        }
        state->session.reset();
    }
    if(!state->payloads.empty()) {
        try {state->backend.waitIdle();}
        catch(const std::exception& e){std::fprintf(stderr,"[NATIVE RECORDING] terminal GPU retirement: %s\n",e.what());}
        for(const auto& [id,payload]:state->payloads) {
            try {state->backend.releaseRecordingPayload(payload.native);}
            catch(const std::exception& e){std::fprintf(stderr,"[NATIVE RECORDING] terminal cached payload %08X release: %s\n",id,e.what());}
        }
        state->invalidatePayloadIndex();
        state->payloads.clear();
    }
    if(state->record) {
        std::fprintf(stderr,"[NATIVE RECORDING] incomplete original manager cleanup: owner=%08X identity=%08X phase=%u; releasing host ownership only\n",
            state->record->owner,state->record->id,unsigned(state->record->phase));
        try {state->backend.releaseRecordingContext(state->record->native);}
        catch(const std::exception& e) {std::fprintf(stderr,"[NATIVE RECORDING] terminal explicit host release: %s\n",e.what());}
        state->record.reset();
    }
}
size_t EngineRecordingOwners::count() const {state->caller(state->runtime.base);return state->record?1:0;}
void EngineRecordingOwners::requireEmpty() const {
    if(count()) throw Failure("Original recording manager still owns a native deferred context");
}
uint32_t EngineRecordingOwners::identity(uint32_t owner) const {
    state->caller(state->runtime.base);
    if(!state->record || state->record->owner!=owner || state->record->phase!=State::Phase::Committed)
        throw Failure("Missing or incomplete native recording owner");
    return state->record->id;
}
void EngineRecordingOwners::validateOwner(uint32_t o,uint32_t id) const {
    state->engineOwner(state->runtime.base);state->owned(state->find(o,id),state->runtime.base);
}
void EngineRecordingOwners::validateFrame(uint8_t* base) const {
    auto& s=*state;
    if(!s.record||s.record->phase!=State::Phase::Committed)return;
    s.graph(base);
}
void EngineRecordingOwners::observeIdleContextPublication(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.frame(c,base);
    if(!s.record)return;
    s.region(c.r1.u32,0xF0);
    const auto flags=PPC_LOAD_U32(0x82D6CCA8),caller=PPC_LOAD_U32(c.r1.u32+0xE8);
    const bool ordinary=(flags==0||flags==2||flags==0x40)&&caller==0x8273B4E0;
    const bool vfx=flags==0x20&&caller==0x8273B864;
    if(c.lastFunction!=0x826FF6D8||uint32_t(c.lr)!=0x827406A0||c.r1.u32>UINT32_MAX-0xF0||
       PPC_LOAD_U32(c.r1.u32)!=c.r1.u32+0xF0||(!ordinary&&!vfx)||
       s.record->phase!=State::Phase::Committed||s.session||s.replay||s.touch||s.touched||s.reset||s.quota||s.pluginDeletion||s.deletion)
        throw Failure("Unqualified original idle recording context publication");
    constexpr std::array<uint32_t,9> setter={0x2B030000,0x409A0010,0x3C608000,0x60634005,0x4E800020,
        0x3D6082D6,0x906B3028,0x38600000,0x4E800020};
    for(uint32_t i=0;i<setter.size();++i)if(PPC_LOAD_U32(0x826FF6D8+4*i)!=setter[i])
        throw Failure("Original idle recording context setter changed");
    auto& r=*s.record;s.owned(r,base);s.idleHistory(base);
    const auto aliases=s.aliases(base);
    if(!c.r3.u32||aliases!=std::array<uint32_t,2>{c.r3.u32,c.r3.u32}||aliases[0]!=r.aliases[0]||c.r3.u32==r.id)
        throw Failure("Original idle recording publication lost its application context owner");
    s.runtime.engineDriver->requireContext(c.r3.u32);
    // Observe the completed original store, not an intended future write.
    // The dispatcher publishes its live device context independently of a
    // recording session, so the constructor's mesh alias may legitimately
    // advance from null. Only this qualified original producer updates the
    // host expectation; neither alias nor any guest history field is changed.
    r.aliases=aliases;
}
std::weak_ptr<Graphics::NativeRecordingContext> EngineRecordingOwners::nativeContext(uint32_t o,uint32_t id) const {
    validateOwner(o,id);return state->find(o,id).native;
}
void EngineRecordingOwners::createBegin(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.frame(ctx,base);
    const uint32_t o=ctx.r3.u32;
    if(ctx.lr!=0x823B748C || s.record || PPC_LOAD_U32(singleton))
        throw Failure("Recording construction lacks its original allocator/caller or empty singleton");
    s.region(o,0x7C,true);
    if(overlaps(o,0x7C,ctx.r1.u32-0x100,0x100) || overlaps(o,0x7C,0x82000000,0xEC0000))
        throw Failure("Recording owner overlaps original image/ABI storage");
    if(PPC_LOAD_U32(0x826F4988)!=0x7D8802A6 || PPC_LOAD_U32(0x826F4AD0)!=0x4BD5DA71)
        throw Failure("Original recording constructor hook bytes changed");
    auto pools=s.pools(base);auto registration=s.registration(base);
    if(State::receivers(registration,o+4) || (registration.root && registration.registrations==0xFFFF))
        throw Failure("Recording receiver was already registered or registration count is full");
    for(uint32_t p:pools) if(overlaps(o,0x7C,p,0x38)) throw Failure("Recording owner overlaps an existing CPU pool");
    if(!s.runtime.engineDriver) throw Failure("Recording construction has no live native driver");
    const auto aliases=s.aliases(base);s.runtime.engineDriver->requireContext(aliases[0]);
    const uint32_t id=reserveIdentity(s.runtime),untouched=PPC_LOAD_U32(o+0x60);
    auto native=s.backend.createRecordingContext(); // Before the original body publishes or allocates CPU pools.
    s.record.emplace(State::Record{o,id,ctx.r1.u32,&ctx,State::Phase::Prepared,aliases,untouched,
        std::move(pools),std::move(registration),std::move(native)});
}
void EngineRecordingOwners::createCommit(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.frame(ctx,base);
    if(!s.record || s.record->phase!=State::Phase::Prepared) throw Failure("Unscoped recording creation commit");
    auto& r=*s.record;const uint32_t o=r.owner;
    if(r.cpu!=&ctx || ctx.r1.u32!=r.sp-0x80 || ctx.lr!=0x826F4A90 || ctx.r31.u32!=o ||
       ctx.r3.u32 || ctx.r4.u32!=2 || ctx.r5.u32 || ctx.r6.u32 || ctx.r7.u32 || ctx.r8.u32!=o+0x68 ||
       PPC_LOAD_U32(ctx.r1.u32)!=r.sp || PPC_LOAD_U32(r.sp-8)!=0x823B748C) {
        char message[640];std::snprintf(message,sizeof(message),
            "Recording create commit ABI: contextMatch=%u SP=%08X/%08X LR=%llX/826F4A90 r31=%08X/%08X "
            "r3..8=%08X,%08X,%08X,%08X,%08X,%08X expected=0,2,0,0,0,%08X backchain=%08X/%08X savedLR=%08X/823B748C",
            unsigned(r.cpu==&ctx),ctx.r1.u32,r.sp-0x80,static_cast<unsigned long long>(ctx.lr),ctx.r31.u32,o,
            ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,ctx.r7.u32,ctx.r8.u32,o+0x68,
            PPC_LOAD_U32(ctx.r1.u32),r.sp,PPC_LOAD_U32(r.sp-8));
        throw Failure(message);
    }
    s.region(o,0x7C,true);
    if(PPC_LOAD_U32(singleton)!=o || PPC_LOAD_U32(o)!=0x820B856C || PPC_LOAD_U32(o+4)!=0x820B8564 ||
       PPC_LOAD_U32(o+8)!=1 || PPC_LOAD_U32(o+0x68) || PPC_LOAD_U32(o+0x60)!=r.untouched60 || s.aliases(base)!=r.aliases)
        throw Failure("Original recording constructor publication or untouched bytes changed");
    s.emptyFields(o,base);s.pool(o,base);
    auto pools=s.pools(base);
    if(pools.empty() || pools.front()!=o+0xC || std::vector<uint32_t>(pools.begin()+1,pools.end())!=r.pools)
        throw Failure("Original recording constructor failed to retain the CPU pool registry");
    s.validateRegistration(r,base);
    const auto registration=s.registration(base);
    if(registration.root && (registration.registrations!=uint16_t(r.registration.registrations+1) ||
       registration.nodes.size()!=r.registration.nodes.size()+1))
        throw Failure("Original recording registration did not acquire exactly one receiver");
    for(const auto& old:r.registration.nodes) {
        const auto at=std::find_if(registration.nodes.begin(),registration.nodes.end(),[&](const auto& n){return n.address==old.address;});
        if(at==registration.nodes.end() || at->receiver!=old.receiver || at->refs!=old.refs || at->priority!=old.priority)
            throw Failure("Original recording construction changed another CPU receiver");
    }
    s.backend.validateRecordingContext(r.native);
    if(s.runtime.pageAccess[r.id>>12].load()) throw Failure("Prepared recording identity became mapped");
    PPC_STORE_U32(o+0x68,r.id);r.phase=State::Phase::Committed;
    s.initializeGraph(base);
    ctx.lr=0x826F4AD4;ctx.r3.u64=0; // Actual native ownership succeeded; no original recording is claimed.
    std::fprintf(stderr,"[NATIVE RECORDING] created empty deferred-context owner=%08X identity=%08X CPU-pool=%08X available=%u; no payload recorded\n",
        o,r.id,o+0xC,PPC_LOAD_U32(o+0x18));
}
void EngineRecordingOwners::requireActiveContext(uint32_t identity) const {
    auto& s=*state;auto* base=s.runtime.base;s.engineOwner(base);
    if(!s.record||s.record->id!=identity||!s.session||s.session->cursor<7||s.session->cursor>15||!s.session->payload)
        throw Failure("Missing or stale active native recording context");
    s.owned(*s.record,base);
    if(s.aliases(base)!=std::array<uint32_t,2>{identity,identity}||
       s.backend.recordingPayloadReceipt(s.session->payload).state!=Graphics::NativeRecordingPayloadState::Recording)
        throw Failure("Native recording context aliases/phase differ");
}
std::shared_ptr<Graphics::NativeRecordingPayload> EngineRecordingOwners::activePayload(uint32_t contextIdentity) const {
    requireActiveContext(contextIdentity);auto& s=*state;auto* base=s.runtime.base;const auto& run=*s.session;
    if(run.cursor!=15||!run.cpuRecord||PPC_LOAD_U32(s.record->owner+0x54)!=run.payloadId||
       PPC_LOAD_U32(run.head)!=run.cpuRecord||PPC_LOAD_U32(run.cpuRecord+8)!=run.head||
       PPC_LOAD_U32(run.cpuRecord+0x28)!=run.payloadId||PPC_LOAD_U32(run.cpuRecord+0x1C)!=run.metadata)
         throw Failure("Native recording payload lost its completed original CPU publication");
    s.runtime.engineDriver->requireRecordingStateSeed(contextIdentity);return run.payload;
}
void EngineRecordingOwners::beginRecording(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.frame(c,base);
    if(c.lastFunction!=0x826F4D08||uint32_t(c.lr)!=0x827404EC||!s.record||s.session||s.replay||s.touch||s.touched||s.reset||s.quota||s.pluginDeletion||s.deletion||c.r3.u32!=s.record->owner||c.r7.u32!=1)
        throw Failure("Unqualified original recording begin entry");
    auto& r=s.find(c.r3.u32,s.record->id);s.idleHistory(base);s.validateRegistration(r,base);
    auto& driver=*s.runtime.engineDriver;driver.requireContext(c.r5.u32);
    if(s.aliases(base)!=std::array<uint32_t,2>{c.r5.u32,c.r5.u32}||
       !PPC_LOAD_U32(0x82CED94C)||!PPC_LOAD_U32(0x82CED950)||PPC_LOAD_U32(0x82D6D850)!=0xB0)
        throw Failure("Original recording begin limits, aliases or cache plugin differ");
    s.region(c.r4.u32,0xB0);s.region(c.r6.u32,0xC0,true);s.region(c.r8.u32,0x2C);
    driver.effects().requireRigidSelection(c.r4.u32);
    if(PPC_LOAD_U32(c.r8.u32+0x24)||PPC_LOAD_U32(c.r6.u32+0xB0)||PPC_LOAD_U32(c.r6.u32+0xB8)||PPC_LOAD_U32(c.r6.u32+0xBC))
        throw Failure("Native recording requires static geometry and only bucket1 ownership");
    uint32_t oldHead=0;
    for(const auto& node:s.nodes)if(node.head==c.r6.u32+0xB4) {
        if(node.metadata==c.r8.u32)throw Failure("Fresh recording cannot overwrite a completed cache key; retain original replay");
        oldHead=node.address;
    }
    if(PPC_LOAD_U32(c.r6.u32+0xB4)!=oldHead)throw Failure("Fresh recording encountered an unowned object cache");
    // The original quota branch precedes allocation and eviction. Observe its
    // real zero return and let the unchanged caller select its fallback.
    if(s.successCount>=PPC_LOAD_U32(0x82CED94C)) {
        s.quota=State::Observation{&c,c.r1.u32,uint32_t(c.lr),0,0,0};return;
    }
    if(PPC_LOAD_U32(r.owner+0x4C)>=PPC_LOAD_U32(0x82CED950)||s.freeOrder.empty())
        throw Failure("Native recording eviction or CPU pool growth is not qualified");
    State::Session next{};next.cpu=&c;next.sp=c.r1.u32;next.typed=c.r4.u32;next.object=c.r6.u32;
    next.metadata=c.r8.u32;next.previous=c.r5.u32;next.bucket=1;next.head=next.object+0xB4;
    next.previousPayload=PPC_LOAD_U32(r.owner+0x54);next.freeRecord=s.freeOrder.front();next.oldHead=oldHead;
    s.runtime.pointer(next.sp-0xC0,0xC0,true);
    std::memcpy(next.mask.data(),s.runtime.pointer(next.typed+0x50,40,false),40);
    // Multitone inherits VS c32..43 as well as c0..31. The original typed
    // reflection owns this material-specific mask; one rigid profile is not
    // a valid mask for every shader. Native replay already applies each bit.
    const auto expected=driver.effects().rigidRecordingMask(next.typed);
    if(next.mask!=expected) {
        std::fprintf(stderr,"[NATIVE RECORDING MASK] typed=%08X effect=%08X object=%08X metadata=%08X technique=%08X pass=%08X mask=",
            next.typed,PPC_LOAD_U32(next.typed+0x1C),next.object,next.metadata,
            PPC_LOAD_U32(next.typed+0x48),PPC_LOAD_U32(next.typed+0x4C));
        for(auto byte:next.mask)std::fprintf(stderr,"%02X",unsigned(byte));
        std::fprintf(stderr,"\n");
        throw Failure("Original rigid recording inheritance mask differs");
    }
    next.camera=driver.cameraBinding();
    if(next.camera.viewport!=std::array<uint32_t,6>{0,0,1280,720,0x3F800000,0})
        throw Failure("Original rigid recording camera differs");
    bool alphaOne=false;next.color=driver.color(next.camera.colorIdentity,alphaOne);next.depth=driver.depth(next.camera.depthIdentity);
    if(alphaOne)throw Failure("Original rigid recording selected an alpha-one front target");
    s.backend.requireSelectedTargets({next.color,nullptr,nullptr,nullptr},next.depth);
    const auto scissor=s.backend.scissor();if(!scissor)throw Failure("Original recording has no native scissor snapshot");next.scissor=*scissor;
    next.pixelId=reserveIdentity(s.runtime);next.vertexId=reserveIdentity(s.runtime);
    s.session=std::move(next);
    // Keep the original limit checks, alias stores, state getters and CPU pool
    // allocation. Payload construction is at its actual later SDK allocation.
}
void EngineRecordingOwners::beginOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.frame(c,base);
    if(!s.record||!s.session)throw Failure("Unscoped original recording begin operation");
    auto& r=*s.record;auto& run=*s.session;const auto o=r.owner;
    constexpr uint32_t sites[]={0x826F4DE4,0x826F4DF0,0x826F4E04,0x826F4E14,0x826F4E20,0x826F4E2C,
        0x826F4E94,0x826F4ED4,0x826F4EEC,0x826F4EF8,0x826F4F04,0x826F4F10,0x826F4F1C,0x826F4F28,0x826F4F48};
    if(run.cpu!=&c||c.r1.u32!=run.sp-0xC0||PPC_LOAD_U32(c.r1.u32)!=run.sp||
       PPC_LOAD_U32(run.sp-8)!=0x827404EC||c.r31.u32!=o||run.cursor>=std::size(sites)||sites[run.cursor]!=site)
        throw Failure("Original recording begin operation frame/order differs");
    s.owned(r,base);
    const auto application=PPC_LOAD_U32(0x82D6D890),mesh=PPC_LOAD_U32(0x82D63028);
    if(application!=r.id||mesh!=(run.cursor<7?run.previous:r.id)||PPC_LOAD_U32(o+0x64)!=run.previous||
       PPC_LOAD_U32(o+0x58)!=run.object||PPC_LOAD_U32(o+0x5C)!=run.metadata||PPC_LOAD_U32(o+0x60)!=run.bucket)
        throw Failure("Original recording begin publication differs");
    if(run.cursor>=7&&(PPC_LOAD_U32(o+0x6C)!=run.camera.colorIdentity||PPC_LOAD_U32(o+0x70)!=run.camera.depthIdentity||
       PPC_LOAD_U32(o+0x74)!=run.pixelId||PPC_LOAD_U32(o+0x78)!=run.vertexId))
        throw Failure("Original recording retained shader/target snapshot identity differs");
    if(run.cursor<6&&c.r3.u32!=run.previous)throw Failure("Original recording snapshot context differs");
    if(run.cursor>=7&&run.cursor<14&&c.r3.u32!=r.id)throw Failure("Original recording destination context differs");
    switch(site) {
    case 0x826F4DE4:
        if(c.r4.u32)throw Failure("Original recording color snapshot slot differs");
        c.r3.u64=run.camera.colorIdentity;break;
    case 0x826F4DF0:c.r3.u64=run.camera.depthIdentity;break;
    case 0x826F4E04:case 0x826F4E14: {
        const auto field=site==0x826F4E04?0x74u:0x78u;
        if(c.r4.u32!=o+field)throw Failure("Original recording shader snapshot output differs");
        s.runtime.engineDriver->effects().requireRigidSelection(run.typed);
        PPC_STORE_U32(c.r4.u32,field==0x74?run.pixelId:run.vertexId);c.r3.u64=0;break;
    }
    case 0x826F4E20:
        if(c.r4.u32!=c.r1.u32+0x50)throw Failure("Original recording scissor output differs");
        for(uint32_t i=0;i<4;++i)PPC_STORE_U32(c.r4.u32+4*i,run.scissor[i]);c.r3.u64=0;break;
    case 0x826F4E2C:
        if(c.r4.u32!=c.r1.u32+0x60)throw Failure("Original recording viewport output differs");
        // The engine camera owns the original reversed-depth viewport. Native
        // raster draws separately implement that range in the depth adapter.
        for(uint32_t i=0;i<6;++i)PPC_STORE_U32(c.r4.u32+4*i,run.camera.viewport[i]);c.r3.u64=0;break;
    case 0x826F4E94:
        if(c.r3.u32!=0x3000||c.r4.u32||run.payload||PPC_LOAD_U32(o+0x54)!=run.previousPayload)throw Failure("Original recording payload allocation differs");
        // Still no pool borrow or CPU record publication. Validation deferred to present validateFrame.
        run.payload=s.backend.allocateRecordingPayload(r.native,c.r3.u32);run.payloadId=reserveIdentity(s.runtime);
        c.r3.u64=run.payloadId;break;
    case 0x826F4ED4:
        if(c.r4.u32!=run.payloadId||PPC_LOAD_U32(o+0x54)!=run.payloadId||c.r5.u32!=4||c.r6.u32!=run.typed+0x50||c.r7.u64||c.r8.u64||c.r9.u64)
            throw Failure("Original recording start arguments differ");
        s.runtime.engineDriver->effects().requireRigidSelection(run.typed);
        s.backend.beginRecordingPayload(run.payload,4,run.mask,run.mask);
        s.runtime.engineDriver->beginRecordingState(r.id);run.stateActive=true;c.r3.u64=0;break;
    case 0x826F4EEC:
        if(c.r4.u32||c.r5.u32!=run.camera.colorIdentity||PPC_LOAD_U32(o+0x6C)!=c.r5.u32)throw Failure("Original recording color seed differs");
        c.r3.u64=0;break;
    case 0x826F4EF8:
        if(c.r4.u32!=run.camera.depthIdentity||PPC_LOAD_U32(o+0x70)!=c.r4.u32)throw Failure("Original recording depth seed differs");
        c.r3.u64=0;break;
    case 0x826F4F04:
        if(c.r4.u32!=c.r1.u32+0x60)throw Failure("Original recording viewport seed pointer differs");
        for(uint32_t i=0;i<6;++i)if(PPC_LOAD_U32(c.r4.u32+4*i)!=run.camera.viewport[i])throw Failure("Original recording viewport seed changed");
        c.r3.u64=0;break;
    case 0x826F4F10:
        if(c.r4.u32!=c.r1.u32+0x50)throw Failure("Original recording scissor seed pointer differs");
        for(uint32_t i=0;i<4;++i)if(PPC_LOAD_U32(c.r4.u32+4*i)!=run.scissor[i])throw Failure("Original recording scissor seed changed");
        c.r3.u64=0;break;
    case 0x826F4F1C:case 0x826F4F28:
        if(c.r4.u32!=(site==0x826F4F1C?run.vertexId:run.pixelId))throw Failure("Original recording shader seed differs");
        c.r3.u64=0;break;
    case 0x826F4F48: {
        s.runtime.engineDriver->requireRecordingStateSeed(r.id);
        const auto cpuRecord=PPC_LOAD_U32(run.head);s.region(cpuRecord,0x34,true);
        if(cpuRecord!=run.freeRecord||s.freeOrder.empty()||s.freeOrder.front()!=cpuRecord||
           PPC_LOAD_U32(cpuRecord+8)!=run.head||PPC_LOAD_U32(cpuRecord+0xC)!=run.oldHead||PPC_LOAD_U32(cpuRecord+0x1C)!=run.metadata||
           PPC_LOAD_U32(cpuRecord+0x28)!=run.payloadId)
            throw Failure("Original recording CPU cache publication differs");
        for(uint32_t offset:{0u,4u,0x10u,0x14u,0x18u,0x20u,0x24u,0x2Cu,0x30u})
            if(PPC_LOAD_U32(cpuRecord+offset))throw Failure("Original initial CPU record has unexpected completed/patch state");
        const auto receipt=s.backend.recordingPayloadReceipt(run.payload);
        if(receipt.state!=Graphics::NativeRecordingPayloadState::Recording||receipt.recordedDraws||receipt.ownedDataBytes)
            throw Failure("Native recording begin unexpectedly submitted draw work");
        run.cpuRecord=cpuRecord;
        s.invalidateFreePlan();
        s.nodes.push_back({cpuRecord,run.payloadId,run.head,run.metadata,run.oldHead});
        s.freeOrder.erase(s.freeOrder.begin());
        std::fprintf(stderr,"[NATIVE RECORDING BEGIN] owner=%08X context=%08X payload=%08X CPU-record=%08X bucket=%u; original CPU allocation/aliases retained, native deferred seed ready, zero recorded draws\n",
            o,r.id,run.payloadId,cpuRecord,run.bucket);break;
    }
    default:throw Failure("Unknown original recording begin operation");
    }
    ++run.cursor;
    // The six native seed bindings were captured and set together at the
    // actual begin operation. These consecutive original seed calls must match
    // that owned snapshot exactly; no draw or other native mutation intervenes.
    if(site!=0x826F4F48)c.lr=site+4;
}
void EngineRecordingOwners::finishRecording(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.frame(c,base);
    if(c.lastFunction!=0x826F4F58||uint32_t(c.lr)!=0x82740660||!s.record||!s.session||
       c.r3.u32!=s.record->owner||c.r1.u32!=s.session->sp||s.session->finishing||s.replay)
        throw Failure("Unqualified original recording finish entry");
    auto& run=*s.session;const auto payload=activePayload(s.record->id);
    s.runtime.engineDriver->effects().requireRigidRecordingComplete(run.typed);
    const auto receipt=s.backend.recordingPayloadReceipt(payload);
    if(!receipt.recordedDraws||receipt.executedDraws||receipt.executions||!receipt.ownedDataBytes||
       receipt.ownedDataBytes>receipt.ownedDataCapacityBytes||receipt.ownedDataBytes>UINT32_MAX)
        throw Failure("Native recording finish requires an unexecuted nonempty payload");
    s.region(run.cpuRecord,0x34,true);
    for(uint32_t offset:{0u,4u,0x10u,0x14u,0x18u,0x20u,0x24u,0x2Cu,0x30u})
        if(PPC_LOAD_U32(run.cpuRecord+offset))throw Failure("Initial recording finish has unsupported CPU result/patch state");
    const auto o=s.record->owner;
    if(s.successCount==UINT32_MAX||uint64_t(PPC_LOAD_U32(o+0x4C))+receipt.ownedDataBytes>UINT32_MAX)
        throw Failure("Original recording finish accounting would overflow");
    s.runtime.pointer(c.r1.u32-0x80,0x80,true);
    run.finishing=true;run.finishCursor=0;run.queryBytes=uint32_t(receipt.ownedDataBytes);
    // Keep original query, CPU record lookup, status branch, accounting/LRU
    // stores and both original context-alias restoration helpers.
}
void EngineRecordingOwners::finishOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.frame(c,base);
    if(!s.record||!s.session||!s.session->finishing)throw Failure("Unscoped original recording finish operation");
    auto& run=*s.session;const auto o=s.record->owner,context=s.record->id,r=run.cpuRecord;
    constexpr uint32_t sites[]={0x826F4F88,0x826F4FC0,0x826F50A8,0x826F50B0,0x826F50C4};
    if(run.cpu!=&c||c.r1.u32!=run.sp-0x80||PPC_LOAD_U32(c.r1.u32)!=run.sp||
       PPC_LOAD_U32(run.sp-8)!=0x82740660||c.r30.u32!=o||run.finishCursor>=std::size(sites)||sites[run.finishCursor]!=site)
        throw Failure("Original recording finish frame/order differs");
    s.owned(*s.record,base);
    if(PPC_LOAD_U32(o+0x54)!=run.payloadId||PPC_LOAD_U32(o+0x58)!=run.object||PPC_LOAD_U32(o+0x5C)!=run.metadata||
       PPC_LOAD_U32(o+0x60)!=run.bucket||PPC_LOAD_U32(o+0x64)!=run.previous||PPC_LOAD_U32(run.head)!=r||
       PPC_LOAD_U32(r+8)!=run.head||PPC_LOAD_U32(r+0x28)!=run.payloadId||
       PPC_LOAD_U32(o+0x6C)!=run.camera.colorIdentity||PPC_LOAD_U32(o+0x70)!=run.camera.depthIdentity||
       PPC_LOAD_U32(o+0x74)!=run.pixelId||PPC_LOAD_U32(o+0x78)!=run.vertexId)
        throw Failure("Original recording finish lost its CPU owner/publication");
    const auto aliases=s.aliases(base);
    if(aliases!=(site==0x826F50C4?std::array<uint32_t,2>{run.previous,run.previous}:std::array<uint32_t,2>{context,context}))
        throw Failure("Original recording finish aliases differ");
    if(site==0x826F4F88) {
        const auto receipt=s.backend.recordingPayloadReceipt(activePayload(context));
        if(c.r3.u32!=context||c.r4.u32!=c.r1.u32+0x50||c.r5.u32!=c.r1.u32+0x54||receipt.ownedDataBytes!=run.queryBytes)
            throw Failure("Original recording size query ABI or native receipt changed");
        // Native accounting: the first output is owned immutable draw-data
        // bytes, the second is zero (no separately serialized second stream).
        // These are not estimates of opaque D3D11 driver or original SDK bytes.
        PPC_STORE_U32(c.r4.u32,run.queryBytes);PPC_STORE_U32(c.r5.u32,0);c.r3.u64=0;
    } else if(site==0x826F4FC0) {
        if(c.r3.u32!=context||c.r31.u32!=r||c.r29.u32!=run.metadata||c.r28.u32||
           PPC_LOAD_U32(c.r1.u32+0x50)!=run.queryBytes||PPC_LOAD_U32(c.r1.u32+0x54))
            throw Failure("Original recording finish selected a different CPU record or size result");
        s.invalidatePayloadIndex();
        const auto [at,inserted]=s.payloads.emplace(run.payloadId,State::Payload{run.payloadId,r,run.typed,run.object,
            run.metadata,run.previous,run.head,run.queryBytes,run.camera,run.payload});
        if(!inserted)throw Failure("Native recording payload already published");
        try {s.backend.finishRecordingPayload(run.payload);}
        catch(...) {s.invalidatePayloadIndex();s.payloads.erase(at);s.receiptCache.erase(run.payloadId);throw;}
        if(s.backend.recordingPayloadReceipt(run.payload).state!=Graphics::NativeRecordingPayloadState::Sealed)
            throw Failure("Native finish did not create a sealed command list");
        c.r3.u64=0; // Only an actual successful FinishCommandList permits the original success branch.
    } else {
        const auto receipt=s.backend.recordingPayloadReceipt(run.payload);
        if(c.r31.u32!=r||c.r28.u32||receipt.state!=Graphics::NativeRecordingPayloadState::Sealed||receipt.executions||
           PPC_LOAD_U32(r+0x14)!=run.queryBytes||PPC_LOAD_U32(r+0x10)||PPC_LOAD_U32(r+0x2C)!=2)
            throw Failure("Original recording finish did not publish its checked CPU accounting/LRU result");
        if(site==0x826F50A8) {
            auto& value=s.payloads.at(run.payloadId);
            if(value.completed)throw Failure("Original recording success was observed twice");
            value.completed=true;++s.successCount;s.lru.insert(s.lru.begin(),r);
        }
        if(site==0x826F50A8) {
            if(!run.color||c.r3.u32!=run.camera.colorIdentity||PPC_LOAD_U32(o+0x6C)!=c.r3.u32)
                throw Failure("Original recording color snapshot release differs");
            run.color.reset();c.r3.u64=0; // Release this native snapshot lease; payload seed/draw owners retain their leases.
        } else if(site==0x826F50B0) {
            if(run.color||!run.depth||c.r3.u32!=run.camera.depthIdentity||PPC_LOAD_U32(o+0x70)!=c.r3.u32)
                throw Failure("Original recording depth snapshot release differs");
            run.depth.reset();c.r3.u64=0;
        } else {
            if(run.color||run.depth||!run.stateActive)throw Failure("Original recording restore precedes snapshot releases");
            s.runtime.engineDriver->requireContext(run.previous);
            s.runtime.engineDriver->endRecordingState(context);run.stateActive=false;
            std::fprintf(stderr,"[NATIVE RECORDING FINISH] payload=%08X CPU-record=%08X draws=%llu owned-bytes=%u; real command list sealed, original accounting/LRU and context restoration completed, zero executions\n",
                run.payloadId,r,static_cast<unsigned long long>(receipt.recordedDraws),run.queryBytes);
        }
    }
    ++run.finishCursor;if(site!=0x826F50C4)c.lr=site+4;
}
void EngineRecordingOwners::requireFinishedRecording(uint32_t typed,uint32_t previous) const {
    auto& s=*state;auto* base=s.runtime.base;s.engineOwner(base);
    if(!s.record||!s.session||s.session->typed!=typed||s.session->previous!=previous||!s.session->finishing||
       s.session->finishCursor!=5||s.session->stateActive||s.session->color||s.session->depth||
       s.aliases(base)!=std::array<uint32_t,2>{previous,previous})
        throw Failure("Native recording has not completed original finish/context restoration");
    const auto& run=*s.session;const auto found=s.payloads.find(run.payloadId);
    if(found==s.payloads.end()||found->second.native!=run.payload||
       s.backend.recordingPayloadReceipt(run.payload).state!=Graphics::NativeRecordingPayloadState::Sealed||
       PPC_LOAD_U32(run.cpuRecord+0x28)!=run.payloadId||PPC_LOAD_U32(run.cpuRecord+0x2C)!=2)
         throw Failure("Finished native recording lost its CPU payload result");
    s.runtime.engineDriver->requireContext(previous);
}
void EngineRecordingOwners::completeEffectRestore(uint32_t typed,uint32_t previous) {
    requireFinishedRecording(typed,previous);state->saveHistory(state->runtime.base);state->session.reset();
}
void EngineRecordingOwners::requireReplay(uint32_t packet,uint32_t payloadId) const {
    auto& s=*state;auto* base=s.runtime.base;s.engineOwner(base);
    const auto at=s.payloads.find(payloadId);
    if(s.session||!s.record||s.reset||s.quota||s.pluginDeletion||s.deletion||at==s.payloads.end()||!at->second.completed)throw Failure("Native replay has no completed idle payload owner");
    s.idleHistory(base);const auto& p=at->second;s.region(packet,0x24);
    if(PPC_LOAD_U32(packet)!=p.metadata||PPC_LOAD_U32(packet+4)!=p.object||PPC_LOAD_U32(packet+8)!=p.camera.camera||
       PPC_LOAD_U32(packet+0x10)!=1||PPC_LOAD_U8(packet+0xC)!=1||PPC_LOAD_U32(packet+0x14)!=p.previous||
       PPC_LOAD_U32(packet+0x18)!=p.typed||s.aliases(base)!=std::array<uint32_t,2>{p.previous,p.previous})
        throw Failure("Native replay packet/context differs from its original recording owner");
    s.region(p.cpuRecord,0x34);
    // graph() resolves this exact owned node even when a newer metadata key
    // has become the head of its object's cache chain.
    if(PPC_LOAD_U32(p.cpuRecord+0x1C)!=p.metadata||
       PPC_LOAD_U32(p.cpuRecord+0x28)!=p.id||PPC_LOAD_U32(p.cpuRecord+0x14)!=p.bytes||PPC_LOAD_U32(p.cpuRecord+0x10)||
       PPC_LOAD_U32(p.cpuRecord+0x2C)!=2||PPC_LOAD_U32(p.cpuRecord+0x20)||PPC_LOAD_U32(p.cpuRecord+0x24)||PPC_LOAD_U32(p.cpuRecord+0x30))
        throw Failure("Native replay CPU record fields differ");
    const auto receipt=s.backend.recordingPayloadReceipt(p.native);
    if(receipt.state!=Graphics::NativeRecordingPayloadState::Sealed||!receipt.recordedDraws||receipt.ownedDataBytes!=p.bytes)
        throw Failure("Native replay payload is not a sealed nonempty recording");
    s.runtime.engineDriver->requireContext(p.previous);
    const auto camera=s.runtime.engineDriver->cameraBinding();
    if(camera.camera!=p.camera.camera||camera.colorIdentity!=p.camera.colorIdentity||camera.depthIdentity!=p.camera.depthIdentity||camera.viewport!=p.camera.viewport)
        throw Failure("Native replay target/camera owner differs");
}
void EngineRecordingOwners::beginReplay(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.frame(c,base);
    if(c.lastFunction!=0x827402F0||uint32_t(c.lr)!=0x82740AFC||c.r5.u32||s.replay||s.touch||!s.touched||
       s.touched->cpu!=&c||s.touched->sp!=c.r1.u32||s.touched->packet!=c.r3.u32||s.touched->payload!=c.r4.u32)
        throw Failure("Unqualified original recording replay entry");
    requireReplay(c.r3.u32,c.r4.u32);s.runtime.pointer(c.r1.u32-0x70,0x70,true);
    s.replay=State::Replay{&c,c.r1.u32,c.r3.u32,c.r4.u32};
    s.touched.reset();
}
void EngineRecordingOwners::observeLruTouch(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.frame(c,base);
    if(!s.record||s.session||s.replay||s.reset||s.quota||c.r3.u32!=s.record->owner||
       uint32_t(c.lr)!=0x82740AEC||PPC_LOAD_U32(c.r1.u32)!=c.r1.u32+0xF0||
       PPC_LOAD_U32(c.r1.u32+0xE8)!=0x8273B4E0)
        throw Failure("Unqualified original recording LRU touch frame");
    if(site==0x826F4C70) {
        if(c.lastFunction!=0x826F4C70||s.touch||s.touched)throw Failure("Overlapping original recording LRU touch");
        requireReplay(c.r31.u32,c.r26.u32);
        const auto& payload=s.payloads.at(c.r26.u32);
        if(c.r4.u32!=payload.cpuRecord)throw Failure("Original LRU touch selected another CPU record");
        s.touch=State::Observation{&c,c.r1.u32,uint32_t(c.lr),c.r31.u32,c.r26.u32,c.r4.u32};
        return;
    }
    if((site!=0x826F4CF4&&site!=0x826F4D00)||!s.touch||s.touched||s.touch->cpu!=&c||
       s.touch->sp!=c.r1.u32||s.touch->lr!=uint32_t(c.lr)||s.touch->packet!=c.r31.u32||
       s.touch->payload!=c.r26.u32||s.touch->node!=c.r4.u32)
        throw Failure("Original LRU touch return has no matching entry");
    // The singleton takes 4D00; all larger lists take 4CF4, including a
    // head no-op. The original helper has already performed every link store.
    if((s.lru.size()==1)!=(site==0x826F4D00))throw Failure("Original LRU touch took an unexpected return branch");
    const auto at=std::find(s.lru.begin(),s.lru.end(),s.touch->node);
    if(at==s.lru.end())throw Failure("Original LRU touch lacks a completed owned node");
    // Performance: LRU touches occur once per replay (~28 per frame). Full
    // graph validation (1910 free slots + 90 nodes + receipts, ~50us) is
    // deferred to once-per-frame present validation (validateFrame), saving
    // ~2.8ms/frame. Only the LRU order changed here (one node moved to front);
    // node topology, free list, payload set and pool block are unchanged.
    // Targeted safety checks above (touch frame, LRU membership) plus cheap
    // idleHistory below preserve fail-stop safety; rendering uses payloads
    // (not LRU/free/head/accounting which are validation-only). A corrupted
    // frame throws in present validation before display (no glitch shown).
    s.lru.erase(at);s.lru.insert(s.lru.begin(),s.touch->node);
    s.idleHistory(base);
    s.touched=s.touch;s.touch.reset(); // Consumed only by the immediate original replay call.
}
void EngineRecordingOwners::observeCounterReset(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.frame(c,base);
    if(!s.record||s.session||s.replay||s.touch||s.touched||s.quota||c.r3.u32!=s.record->owner+4)
        throw Failure("Unqualified original recording event reset receiver/lifetime");
    if(site==0x826F3988) {
        if(c.lastFunction!=0x826F3988||s.reset)throw Failure("Overlapping original recording counter callback");
        s.idleHistory(base);
        const auto registration=s.registration(base);
        if(registration.root!=s.record->registration.root||State::receivers(registration,c.r3.u32)!=(registration.root?1u:0u))
            throw Failure("Original counter callback lost its registered receiver");
        // A null descriptor takes the original conditional return at 3994.
        // Dispatch may temporarily retain its event node, so do not impose the
        // idle registration reference count inside the callback itself.
        if(registration.root)s.reset=State::Observation{&c,c.r1.u32,uint32_t(c.lr),0,0,registration.root};
        return;
    }
    if(site!=0x826F39A0||!s.reset||s.reset->cpu!=&c||s.reset->sp!=c.r1.u32||s.reset->lr!=uint32_t(c.lr)||
       PPC_LOAD_U32(event)!=s.reset->node||PPC_LOAD_U32(s.record->owner+0x50))
         throw Failure("Original recording counter reset return differs");
    s.successCount=0;s.idleHistory(base);s.reset.reset();
}
void EngineRecordingOwners::observeQuotaReturn(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.frame(c,base);
    if(!s.record||!s.quota||s.session||s.replay||s.touch||s.touched||s.reset||s.quota->cpu!=&c||
       s.quota->sp!=c.r1.u32||PPC_LOAD_U32(c.r1.u32-8)!=s.quota->lr||c.r31.u32!=s.record->owner||c.r3.u32||
       s.successCount<PPC_LOAD_U32(0x82CED94C))
         throw Failure("Original recording quota return differs");
    s.idleHistory(base);
    const auto aliases=s.aliases(base);
    s.runtime.engineDriver->requireContext(aliases[0]);
    if(aliases[0]!=aliases[1])throw Failure("Original quota return changed a recording context alias");
    s.quota.reset(); // Original restore helper and caller's failure branch still run.
}
void EngineRecordingOwners::executeReplay(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.frame(c,base);
    if(!s.replay||s.replay->cpu!=&c||c.r1.u32!=s.replay->sp-0x70||PPC_LOAD_U32(c.r1.u32)!=s.replay->sp||
       PPC_LOAD_U32(s.replay->sp-8)!=0x82740AFC||c.r31.u32!=s.replay->packet||c.r30.u32!=s.replay->payloadId||
       c.r4.u32!=s.replay->payloadId||c.r5.u32)
        throw Failure("Original recording execute frame/arguments differ");
    const auto found=s.payloads.find(s.replay->payloadId);
    if(found==s.payloads.end()||!found->second.completed)throw Failure("Native replay has no completed idle payload owner");
    auto& p=found->second;
    if(c.r3.u32!=p.previous)throw Failure("Original recording execute context differs");
    s.runtime.engineDriver->effects().prepareRigidReplay(p.typed,s.replay->packet,s.replay->payloadId);
    const bool firstGlobal=!s.runtime.frameCaptureDirectory.empty()&&!s.backend.recordingExecutedDrawCount();
    const bool postEdgeAACapture=!s.runtime.frameCaptureDirectory.empty()&&
        std::filesystem::exists(s.runtime.frameCaptureDirectory/"after-edgeaa.request");
    const bool capture=!s.runtime.frameCaptureDirectory.empty()&&
        ((!s.backend.recordingPayloadReceipt(p.native).executions&&
          (s.capturedPayloads<std::min(PPC_LOAD_U32(0x82CED94C),16u)))||postEdgeAACapture);
    std::shared_ptr<Graphics::RenderTarget> target;std::vector<uint8_t> before;
    if(capture) {
        bool alphaOne=false;target=s.runtime.engineDriver->color(p.camera.colorIdentity,alphaOne);
        if(alphaOne||target->format!=Graphics::TargetFormat::RGB10A2)throw Failure("First rigid replay capture target format differs");
        before=s.backend.readbackTarget(target);
    }
    // ExecuteCommandList(TRUE) restores actual immediate bindings. Native
    // fixed-state caches still describe those unchanged bindings; per-draw
    // inherited values are prepared from the original current CPU staging.
    s.backend.executeRecordingPayload(p.native);
    const auto receipt=s.backend.recordingPayloadReceipt(p.native);
    if(capture) {
        const auto after=s.backend.readbackTarget(target);
        if(after.size()!=before.size()||after.size()!=size_t(target->pixelWidth())*target->pixelHeight()*4)
            throw Failure("First rigid replay capture extent differs");
        size_t changed=0;for(size_t i=0;i<after.size();i+=4)changed+=std::memcmp(before.data()+i,after.data()+i,4)!=0;
        const auto write=[&](const char* name,const uint8_t* bytes,size_t size) {
            std::ofstream out(s.runtime.frameCaptureDirectory/name,std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes),std::streamsize(size));
            if(!out)throw Failure("First rigid replay diagnostic write failed");
        };
        char prefix[40];
        if(postEdgeAACapture)std::snprintf(prefix,sizeof(prefix),"recording-post-edgeaa");
        else if(firstGlobal)std::snprintf(prefix,sizeof(prefix),"recording-first");
        else std::snprintf(prefix,sizeof(prefix),"recording-%08X",p.id);
        const std::string stem=prefix;
        write((stem+"-before.rgb10a2").c_str(),before.data(),before.size());
        write((stem+"-after.rgb10a2").c_str(),after.data(),after.size());
        write((stem+"-staging-vs.bin").c_str(),s.runtime.pointer(0x82D6C0D0,56*16,false),56*16);
        write((stem+"-staging-ps.bin").c_str(),s.runtime.pointer(0x82D6C450,56*16,false),56*16);
        std::ofstream out(s.runtime.frameCaptureDirectory/(stem+".json"));
        out<<"{\"width\":"<<target->pixelWidth()<<",\"height\":"<<target->pixelHeight()<<",\"changed_pixels\":"<<changed
           <<",\"recorded_draws\":"<<receipt.recordedDraws<<",\"executed_draws\":"<<receipt.executedDraws
           <<",\"payload\":"<<p.id<<",\"cpu_record\":"<<p.cpuRecord<<",\"object\":"<<p.object<<",\"metadata\":"<<p.metadata
           <<",\"executions\":"<<receipt.executions
           <<",\"capture_source\":\"private_scene_target_readback\",\"front_copy_completed\":false,\"gameplay_verified\":false}\n";
        if(!out)throw Failure("First rigid replay diagnostic metadata write failed");
        if(postEdgeAACapture)std::filesystem::remove(s.runtime.frameCaptureDirectory/"after-edgeaa.request");
        ++s.capturedPayloads;
        std::fprintf(stderr,"[NATIVE RECORDING COLOR EVIDENCE] payload=%08X changed_pixels=%zu extent=%ux%u; private scene target only, no presented gameplay claim\n",
            p.id,changed,target->pixelWidth(),target->pixelHeight());
    }
    if(receipt.executions<=4||(receipt.executions%512)==0)
        std::fprintf(stderr,"[NATIVE RECORDING EXECUTE] payload=%08X CPU-record=%08X executions=%llu executed-draws=%llu; actual native command list submitted with current original staged constants\n",
            p.id,p.cpuRecord,static_cast<unsigned long long>(receipt.executions),static_cast<unsigned long long>(receipt.executedDraws));
    c.r3.u64=0;c.lr=0x8274034C;s.replay.reset();
}
void EngineRecordingOwners::pluginDestroyBegin(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.frame(c,base);
    if(c.lastFunction!=0x827374B0||uint32_t(c.lr)!=0x823FB3DC||c.r4.u32!=0xB0||
       PPC_LOAD_U32(0x82D6D850)!=0xB0||c.r5.u32!=0x10||!s.record||
       s.pluginDeletion||s.deletion||s.session||s.replay||s.touch||s.touched||s.reset||s.quota)
         throw Failure("Unqualified original nonempty recording plugin destruction");
    s.region(c.r3.u32,0xC0,true);s.idleHistory(base);
    const uint32_t object=c.r3.u32,head=object+0xB4;
    if(PPC_LOAD_U32(object+0xB0)||!PPC_LOAD_U32(head)||PPC_LOAD_U32(object+0xB8)||PPC_LOAD_U32(object+0xBC))
        throw Failure("Recording plugin destruction has unsupported cache buckets");
    State::PluginDeletion next{&c,c.r1.u32,object,head,{}};
    for(uint32_t node=PPC_LOAD_U32(head);node;node=PPC_LOAD_U32(node+0xC)) {
        const auto it=std::find_if(s.nodes.begin(),s.nodes.end(),[&](const auto& n){return n.address==node;});
        if(it==s.nodes.end()||it->head!=head||next.nodes.size()>=2000)
            throw Failure("Recording plugin destruction contains an unowned CPU record");
        const auto& p=s.payloads.at(it->payload);
        if(!p.completed||p.object!=object||p.head!=head)throw Failure("Recording plugin payload object differs");
        s.runtime.engineDriver->effects().requireCachedRecordRetirement(p.id,p.typed,p.object,p.metadata,p.native);
        next.nodes.push_back(node);
    }
    s.pluginDeletion=std::move(next); // No guest register, list or resource has changed.
}
void EngineRecordingOwners::pluginDestroyComplete(PPCContext& c,uint8_t* base) {
    auto& s=*state;
    if(!s.pluginDeletion)return; // Preserve previously supported empty CPU callbacks.
    s.frame(c,base);const auto& p=*s.pluginDeletion;
    if(p.cpu!=&c||c.r1.u32!=p.sp-0x70||PPC_LOAD_U32(c.r1.u32)!=p.sp||
       c.r29.u32!=p.object||c.r30.u32||c.r31.u32!=p.object+0xC0||!p.nodes.empty()||s.deletion)
        throw Failure("Original recording plugin loop did not complete its scoped removals");
    for(uint32_t off=0xB0;off<0xC0;off+=4)if(PPC_LOAD_U32(p.object+off))
         throw Failure("Original recording plugin retained a cache head");
    s.idleHistory(base);s.pluginDeletion.reset();
}
void EngineRecordingOwners::deleteRecord(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.frame(c,base);
    if(site==0x82737400) {
        if(c.lastFunction!=site||uint32_t(c.lr)!=0x827374D8||!s.pluginDeletion||s.deletion||
           s.pluginDeletion->cpu!=&c||s.pluginDeletion->nodes.empty()||
           c.r1.u32!=s.pluginDeletion->sp-0x70||c.r29.u32!=s.pluginDeletion->object||
           c.r30.u32!=3||c.r31.u32!=s.pluginDeletion->head||c.r3.u32!=s.pluginDeletion->nodes.front())
             throw Failure("Unqualified original cached record deletion entry");
        s.idleHistory(base);
        State::Deletion d{&c,c.r1.u32,c.r3.u32,PPC_LOAD_U32(c.r3.u32+0x28),
            s.freeOrder.empty()?0:s.freeOrder.front(),{},s.aliases(base)};
        for(uint32_t i=0;i<13;++i)d.words[i]=PPC_LOAD_U32(d.node+4*i);
        const auto& p=s.payloads.at(d.payload);
        s.runtime.engineDriver->effects().requireCachedRecordRetirement(p.id,p.typed,p.object,p.metadata,p.native);
        s.deletion=d;return;
    }
    if(!s.deletion||!s.pluginDeletion||s.deletion->cpu!=&c||
       c.r1.u32!=s.deletion->sp-0x70||PPC_LOAD_U32(c.r1.u32)!=s.deletion->sp||
       PPC_LOAD_U32(s.deletion->sp-8)!=0x827374D8||c.r31.u32!=s.deletion->node||
       s.aliases(base)!=s.deletion->aliases)
        throw Failure("Unscoped original cached record deletion operation");
    auto& d=*s.deletion;const auto& p=s.payloads.at(d.payload);
    if(site==0x8273743C) {
        if(d.released||c.r3.u32!=d.payload||c.r29.u32||c.r30.u32)
             throw Failure("Original cached record release arguments differ");
        s.idleHistory(base);
        auto& effects=s.runtime.engineDriver->effects();
        effects.requireCachedRecordRetirement(p.id,p.typed,p.object,p.metadata,p.native);
        // A sealed native list may still own submitted GPU work. Complete that
        // work before invalidating per-payload replay data or releasing it.
        s.backend.waitIdle();
        effects.retireCachedRecord(p.id,p.typed,p.object,p.metadata,p.native);
        s.backend.releaseRecordingPayload(p.native);d.released=true;
        c.r3.u64=0;c.lr=0x82737440;return; // Replace only the resource-release BL.
    }
    if(site!=0x827374A4||!d.released||c.r29.u32!=1||c.r30.u32!=0x82D10000||
       c.r3.u32!=s.record->owner||c.r4.u32!=d.node||uint32_t(c.lr)!=0x827374A4)
        throw Failure("Original cached record deletion did not reach its pool-return epilogue");
    auto expected=d.words;
    expected[0]=d.freeHead;expected[1]=expected[2]=expected[3]=expected[10]=0;
    for(uint32_t i=0;i<13;++i)if(PPC_LOAD_U32(d.node+4*i)!=expected[i])
        throw Failure("Original cached record unlink/free changed unexpected node fields");
    if(PPC_LOAD_U32(s.pluginDeletion->head)!=d.words[3])
        throw Failure("Original cached record deletion did not advance its object head");
    s.idleHistory(base);
    s.invalidateFreePlan();
    const uint32_t node=d.node,id=d.payload,bytes=p.bytes;
    for(auto& n:s.nodes)if(n.next==node)n.next=d.words[3];
    std::erase_if(s.nodes,[&](const auto& n){return n.address==node;});
    std::erase(s.lru,node);s.freeOrder.insert(s.freeOrder.begin(),node);s.invalidatePayloadIndex();s.payloads.erase(id);s.receiptCache.erase(id);
    s.pluginDeletion->nodes.erase(s.pluginDeletion->nodes.begin());s.deletion.reset();
    // Validation deferred to present validateFrame.
    std::fprintf(stderr,"[NATIVE RECORD RETIRED] payload=%08X CPU-record=%08X bytes=%u retained=%zu free=%zu; original object/LRU unlink, accounting and pool return verified\n",
        id,node,bytes,s.payloads.size(),s.freeOrder.size());
}

void EngineRecordingOwners::destroyBegin(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.frame(ctx,base);
    if(!s.record || s.record->phase!=State::Phase::Committed || s.pluginDeletion || s.deletion || ctx.r3.u32!=s.record->owner || ctx.lr!=0x826F4B00)
        throw Failure("Recording destruction lacks the original deleting-wrapper scope");
    auto& r=*s.record;s.owned(r,base);s.retiredGraph(base);s.pool(r.owner,base);s.validateRegistration(r,base);
    const auto aliases=s.aliases(base);
    if(aliases!=r.aliases || aliases[0]==r.id || aliases[1]==r.id) {
        char message[224];std::snprintf(message,sizeof(message),
            "Recording idle context aliases changed: 82D6D890=%08X expected=%08X 82D63028=%08X expected=%08X recording=%08X",
            aliases[0],r.aliases[0],aliases[1],r.aliases[1],r.id);
        throw Failure(message);
    }
    if(PPC_LOAD_U32(0x826F3908)!=0x7D8802A6 || PPC_LOAD_U32(0x826F396C)!=0x916A9784)
        throw Failure("Original recording destructor hook bytes changed");
    auto pools=s.pools(base);
    if(std::count(pools.begin(),pools.end(),r.owner+0xC)!=1) throw Failure("Recording pool is absent from its registry");
    auto registration=s.registration(base);
    r.pools=std::move(pools);r.registration=std::move(registration);
    r.cpu=&ctx;r.sp=ctx.r1.u32;r.phase=State::Phase::Destroying;
}
void EngineRecordingOwners::destroyCommit(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.frame(ctx,base);
    if(!s.record || s.record->phase!=State::Phase::Destroying) throw Failure("Unscoped recording destruction commit");
    auto& r=*s.record;const uint32_t o=r.owner;
    if(r.cpu!=&ctx || ctx.r1.u32!=r.sp-0x70 || ctx.lr!=0x826F3958 || ctx.r31.u32!=o ||
       ctx.r30.u32!=o+4 || ctx.r11.u32 || ctx.r10.u32!=0x82D10000 ||
       PPC_LOAD_U32(ctx.r1.u32)!=r.sp || PPC_LOAD_U32(r.sp-8)!=0x826F4B00) {
        char message[512];std::snprintf(message,sizeof(message),
            "Recording destroy commit ABI: contextMatch=%u SP=%08X/%08X LR=%llX/826F3958 r31=%08X/%08X r30=%08X/%08X "
            "r11=%08X/0 r10=%08X/82D10000 backchain=%08X/%08X savedLR=%08X/826F4B00",
            unsigned(r.cpu==&ctx),ctx.r1.u32,r.sp-0x70,static_cast<unsigned long long>(ctx.lr),ctx.r31.u32,o,
            ctx.r30.u32,o+4,ctx.r11.u32,ctx.r10.u32,PPC_LOAD_U32(ctx.r1.u32),r.sp,PPC_LOAD_U32(r.sp-8));
        throw Failure(message);
    }
    s.region(o,0x7C,true);
    if(PPC_LOAD_U32(singleton)!=o || PPC_LOAD_U32(o)!=0x820B8560 || PPC_LOAD_U32(o+4)!=0x82001660 ||
       PPC_LOAD_U32(o+0xC)!=0x8215093C || PPC_LOAD_U32(o+0x68)!=r.id || s.aliases(base)!=r.aliases)
        throw Failure("Original recording destructor did not complete its CPU helper effects");
    auto expected=r.pools;expected.erase(std::find(expected.begin(),expected.end(),o+0xC));
    if(s.pools(base)!=expected) throw Failure("Original recording destructor did not unlink exactly its CPU pool");
    const auto registration=s.registration(base);
    // 82690790 broadcasts iMsgOnDeleteEntity and removes only the two
    // deletion-service subscriptions. It does not remove iMsgPreRender.
    // Preserve and verify that separate CPU event registration, not just its
    // root/count. Its global event-pool lifetime is not this native COM lifetime.
    if(registration!=r.registration) {
        uint32_t node=0;uint16_t refs=0,priority=0;
        for(const auto& n:registration.nodes) if(n.receiver==o+4) {node=n.address;refs=n.refs;priority=n.priority;break;}
        char message[768];std::snprintf(message,sizeof(message),
            "Original recording registration after CPU destructor: root=%08X before=%08X descriptorCount=%u before=%u "
            "descriptorRefs=%u before=%u type=%08X before=%08X flags=%04X before=%04X "
            "nodes=%zu before=%zu receiver=%08X matches=%zu node=%08X refs=%u priority=%04X receiverVtable=%08X deleteEvent=%08X; expected unchanged iMsgPreRender",
            registration.root,r.registration.root,registration.registrations,r.registration.registrations,
            registration.descriptorRefs,r.registration.descriptorRefs,registration.type,r.registration.type,registration.flags,r.registration.flags,
            registration.nodes.size(),r.registration.nodes.size(),o+4,State::receivers(registration,o+4),node,refs,priority,
            PPC_LOAD_U32(o+4),PPC_LOAD_U32(0x82D5728C));
        throw Failure(message);
    }
    // Original pool blocks may already be freed: never dereference saved block/free-list pointers here.
    s.backend.releaseRecordingContext(r.native);
    PPC_STORE_U32(o+0x68,0); // Explicit native identity adaptation, before the original singleton store/free.
    s.record.reset(); // No PPC registers changed; original 826F396C still executes.
}
}

// Main adds midasm_hook entries with registers=["ctx","base"], without a
// jump or return override. Pins below are 12 original bytes from simpsons.pe.
namespace {
Simpsons::EngineRecordingOwners& recordingObservationOwner(uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)
        throw Simpsons::Failure("Invalid recording observation runtime");
    return Simpsons::active->engineDriver->recordingOwners();
}
}
// 826F4C70: 81640004394000002f0b0000
void SimpsonsNativeRecordLru_826F4C70(PPCContext& c,uint8_t* base) {
    recordingObservationOwner(base).observeLruTouch(c,base,0x826F4C70);
}
// 826F4CF4: 4e8000209083004890830044
void SimpsonsNativeRecordLru_826F4CF4(PPCContext& c,uint8_t* base) {
    recordingObservationOwner(base).observeLruTouch(c,base,0x826F4CF4);
}
// 826F4D00: 4e800020000000007d8802a6
void SimpsonsNativeRecordLru_826F4D00(PPCContext& c,uint8_t* base) {
    recordingObservationOwner(base).observeLruTouch(c,base,0x826F4D00);
}
// 826F3988: 3d6082d6816b1dd02b0b0000
void SimpsonsNativeRecordCounter_826F3988(PPCContext& c,uint8_t* base) {
    recordingObservationOwner(base).observeCounterReset(c,base,0x826F3988);
}
// 826F39A0: 4e800020000000003863fffc
void SimpsonsNativeRecordCounter_826F39A0(PPCContext& c,uint8_t* base) {
    recordingObservationOwner(base).observeCounterReset(c,base,0x826F39A0);
}
// 826F4D50: 483476b838a0000038800000
void SimpsonsNativeRecordQuota_826F4D50(PPCContext& c,uint8_t* base) {
    recordingObservationOwner(base).observeQuotaReturn(c,base);
}
void SimpsonsNativeRecordDeleteBegin(PPCContext& c,uint8_t* base) {
    recordingObservationOwner(base).deleteRecord(c,base,0x82737400);
}
void SimpsonsNativeRecordDeleteRelease(PPCContext& c,uint8_t* base) {
    recordingObservationOwner(base).deleteRecord(c,base,0x8273743C);
}
void SimpsonsNativeRecordDeleteComplete(PPCContext& c,uint8_t* base) {
    recordingObservationOwner(base).deleteRecord(c,base,0x827374A4);
}
// 826FF6F4: 386000004e80002000000000. Original82D63028 store has completed.
void SimpsonsNativeRecordIdleContextPublished(PPCContext& c,uint8_t* base) {
    // Other original setters, including exact recording begin/finish phases,
    // retain their existing independent validation and cannot advance idle
    // aliases through this observation.
    if(uint32_t(c.lr)!=0x827406A0)return;
    recordingObservationOwner(base).observeIdleContextPublication(c,base);
}
