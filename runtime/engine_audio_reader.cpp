#include "engine_audio_reader.h"
#include "engine_audio.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace Simpsons {
namespace {
void require(bool ok,const char* message) {if(!ok) throw Failure(message);}
bool contains(uint32_t start,uint32_t size,uint32_t at,uint32_t bytes) {
    return at>=start && uint64_t(at)+bytes<=uint64_t(start)+size;
}
}
struct EngineAudioReader::State {
    Runtime& rt;
    mutable std::mutex mutex;
    std::condition_variable changed;
    uint64_t next=1;
    uint32_t serviceAllocator{};
    enum class Phase {Constructing,Live,Resetting,Closing,Freed,Failed};
    enum class ConstructionOrigin {StockStartup,StockStream,FixtureStartup,FixtureStream};
    struct Group;
    struct Manager {
        uint32_t address{},handle{},ring{},size{},entries{},entryCount{},allocator{},freeFunction{};
        uint64_t generation{},epoch=1;
        Phase phase=Phase::Constructing;
        std::weak_ptr<Group> group;
        size_t operations{},copies{};
        std::unordered_map<DWORD,size_t> busy;
    };
    struct Group {
        const ConstructionOrigin origin;
        const uint32_t createCaller;
        Group(ConstructionOrigin source,uint32_t caller):origin(source),createCaller(caller) {}
        uint32_t address{},size{},count{},ringSize{},ringOffset{},entryCount{},root{},allocator{},allocatorOverride{},freeFunction{},identifier{};
        uint64_t generation{};
        Phase phase=Phase::Constructing;
        std::vector<std::shared_ptr<Manager>> managers;
    };
    struct Claim {
        OwnedClaim identity;
        std::shared_ptr<Manager> manager;
        bool releasing=false;
        size_t copies{};
    };
    struct Creation {
        uint32_t sp{},allocationResult{};
        bool allocatorCalled=false;
        std::shared_ptr<Group> group;
        std::shared_ptr<Manager> pending;
    };
    struct Operation {
        uint32_t sp{},handle{};
        DWORD thread{};
        std::shared_ptr<Manager> manager;
        std::shared_ptr<Claim> claim;
    };
    struct FreeGroup {uint32_t sp{};std::shared_ptr<Group> group;};
    std::unordered_map<uint32_t,std::shared_ptr<Group>> groups;
    std::unordered_map<uint32_t,std::shared_ptr<Manager>> managers,handles;
    std::unordered_map<uint32_t,std::shared_ptr<Claim>> claims;
    std::unordered_map<PPCContext*,Creation> creating;
    std::unordered_map<PPCContext*,Operation> claiming,releasing,resetting,closing,freeing;
    std::unordered_map<PPCContext*,FreeGroup> groupFrees;
    std::unordered_set<PPCContext*> fixtures;
    explicit State(Runtime& value):rt(value) {}
    uint64_t identity() {
        require(next!=std::numeric_limits<uint64_t>::max(),"Audio reader generation space exhausted");
        return next++;
    }
    void bytes(uint32_t at,uint32_t count,bool write=false) const {
        require(at && uint64_t(at)+count<=0x100000000ull,"Invalid audio reader extent");
        rt.pointer(at,count,write);
    }
    uint32_t word(uint32_t at) const {bytes(at,4);return PPCLoadU32(rt.base,at);}
    void enter(const std::shared_ptr<Manager>& m,DWORD thread) {
        // Reserve the only potentially allocating entry before either counter.
        auto& busy=m->busy[thread];++busy;++m->operations;
    }
    void begin(std::unordered_map<PPCContext*,Operation>& scopes,PPCContext& ctx,const Operation& op) {
        const auto [it,inserted]=scopes.emplace(&ctx,op);
        require(inserted,"Duplicate audio reader operation scope");
        try {enter(op.manager,op.thread);}
        catch(...) {scopes.erase(it);op.manager->phase=Phase::Failed;throw;}
    }
    void leave(const std::shared_ptr<Manager>& m,DWORD thread) {
        --m->operations;
        auto it=m->busy.find(thread);
        if(it!=m->busy.end() && !--it->second) m->busy.erase(it);
        changed.notify_all();
    }
    void drain(std::unique_lock<std::mutex>& lock,const std::shared_ptr<Manager>& m) {
        require(!m->busy.contains(GetCurrentThreadId()),"Reentrant audio reader teardown would wait on its own operation");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(m->operations || m->copies) {
            rt.checkRunning();
            require(m->phase!=Phase::Failed,"Audio reader operation was quarantined after failure");
            require(std::chrono::steady_clock::now()<deadline,"Audio reader lifetime gate timed out; provenance quarantined");
            changed.wait_for(lock,std::chrono::milliseconds(20));
        }
    }
    std::shared_ptr<Manager> byHandle(uint32_t h) const {
        const auto it=handles.find(h);return it==handles.end()?nullptr:it->second;
    }
    void live(const std::shared_ptr<Manager>& m) const {
        auto g=m->group.lock();
        require(g && g->phase==Phase::Live && m->phase==Phase::Live,
                "Audio reader claim/copy requires a live group and manager epoch");
    }
    void managerIdentity(const Manager& m) const {
        require(serviceAllocator && serviceAllocator==m.allocator && word(0x82E36B94)==m.allocator && word(m.handle+4)==m.address &&
                word(m.address+0x64)==m.ring && word(m.address+0x6C)==m.ring+m.size &&
                word(m.address+0x38)==m.entries && word(m.address+0x3C)==m.entryCount,
                "Original audio reader manager allocation identity changed");
    }
    void auditBoundary(uint32_t pc,const PPCContext& ctx,uint8_t* base) const noexcept {
        struct AuditHostState {
            uint32_t csr=PPCFPSCRRegister::getcsr();DWORD error=GetLastError();
            ~AuditHostState(){PPCFPSCRRegister::restoreHostCSR(csr);SetLastError(error);}
        } host;
        if(!rt.resourceAudit.active())return;
        // Capture raw requests and native ownership before admission. Bounded
        // diagnostic reads do not authorize input. Addresses/generations identify
        // the instance, while stable
        // asset/parameter/state fields group repeated failures together.
        try {
            std::ostringstream parameters,ownership,instance;
            std::string kind,asset="original-reader";
            parameters<<"boundary="<<std::hex<<pc;
            if(pc==0x8233D5F8) {
                kind="audio-reader-create";
                asset=ctx.lr==0x8233062C?"original-stream-reader":
                      ctx.lr==0x82816808?"original-startup-reader":"unknown-reader-factory";
                parameters<<std::dec<<" count="<<ctx.r4.u32<<" ring="<<ctx.r5.u32
                          <<" entries="<<ctx.r6.u32<<" hasAllocatorOverride="<<bool(ctx.r8.u32)
                          <<" aux="<<ctx.r9.u32;
                ownership<<"serviceAllocator="<<bool(serviceAllocator);
                instance<<std::hex<<"id="<<ctx.r3.u32<<" root="<<ctx.r7.u32
                        <<" stack="<<ctx.r1.u32<<" allocatorOverride="<<ctx.r8.u32
                        <<std::dec<<" groups="<<groups.size();
                if(ctx.lr==0x8233062C) {
                    parameters<<" bitrate="<<ctx.r10.s64<<" duration="<<std::hexfloat
                              <<ctx.f31.f64<<std::defaultfloat;
                    instance<<std::hex<<" voice="<<ctx.r31.u32;
                } else {
                    // Only 82330540 leaves its bitrate, duration and voice in
                    // these registers. Other factory callers have raw lanes.
                    instance<<std::hex<<" rawR10="<<ctx.r10.u64<<" rawR31="<<ctx.r31.u32
                            <<" rawF31="<<std::hexfloat<<ctx.f31.f64<<std::defaultfloat;
                }
            } else if(pc==0x8233D980) {
                // 8233D950 queues {8233D980,G}; 823395B8 passes the record
                // in r3. Its inherited r4 has no reader-owner meaning.
                kind="audio-reader-release";
                parameters<<" group_argument=command+4";
                const uint32_t command=ctx.r3.u32;
                const bool contextQualified=active==&rt && base==rt.base && currentContext==&ctx;
                bool requestReadable=false,requestFunctionMatches=false,requestContextQualified=false,queueReadable=false;
                uint32_t function=0,candidateAddress=0,queueBase=0,queueUsed=0,queueCapacity=0;
                uint64_t queueEnd=0;
                std::shared_ptr<Group> candidate;
                instance<<std::hex<<"r3="<<ctx.r3.u32<<" r4="<<ctx.r4.u32
                        <<" rawR3="<<ctx.r3.u64<<" rawR4="<<ctx.r4.u64<<" command="<<command
                        <<" r31="<<ctx.r31.u32<<" r30="<<ctx.r30.u32<<" r29="<<ctx.r29.u32
                        <<" rawR31="<<ctx.r31.u64<<" rawR30="<<ctx.r30.u64<<" rawR29="<<ctx.r29.u64;
                if(contextQualified && command && uint64_t(command)+8<=0x100000000ull) {
                    try {
                        rt.pointer(command,8,false);
                        function=PPCLoadU32(base,command);
                        candidateAddress=PPCLoadU32(base,command+4);
                        requestReadable=true;
                        requestFunctionMatches=function==0x8233D980;
                        const auto found=groups.find(candidateAddress);
                        if(found!=groups.end())candidate=found->second;
                        // Preserve cache-only candidate context for a malformed
                        // call whose registers do not match the original executor.
                        // Correlation is not a concurrency pin or generation proof:
                        // genuine 823395B8 callbacks own the original Q40 lock.
                        const bool registersCorrelated=candidate && ctx.lr==0x823395BC &&
                            ctx.r31.u32==candidate->root && ctx.r30.u32==command;
                        // Never dereference G, a stale G, or inherited r4.
                        if(registersCorrelated && candidate->root &&
                           uint64_t(candidate->root)+0xD4<=0x100000000ull) {
                            const uint32_t root=candidate->root;
                            rt.pointer(root+0x20,4,false);rt.pointer(root+0xCC,8,false);
                            queueBase=PPCLoadU32(base,root+0x20);
                            queueCapacity=PPCLoadU32(base,root+0xCC);
                            queueUsed=PPCLoadU32(base,root+0xD0);
                            queueEnd=uint64_t(queueBase)+queueUsed;
                            queueReadable=true;
                            // Diagnostic envelope only: the functional observer
                            // below retains its existing unknown-G/phase rules.
                            requestContextQualified=requestFunctionMatches && ctx.lr==0x823395BC &&
                                ctx.r31.u32==root && ctx.r30.u32==command && queueBase &&
                                queueUsed<=queueCapacity && uint64_t(queueBase)+queueCapacity<=0x100000000ull &&
                                queueEnd<=0x100000000ull && uint64_t(ctx.r29.u32)==queueEnd &&
                                command>=queueBase && uint64_t(command)+8<=queueEnd;
                        }
                    } catch(...) {
                        // Retain any bounded command/cached-candidate snapshot.
                        // An unreadable queue never establishes original ownership.
                    }
                }
                ownership<<"contextQualified="<<contextQualified<<" requestReadable="<<requestReadable
                         <<" requestFunctionMatches="<<requestFunctionMatches
                         <<" requestContextQualified="<<requestContextQualified
                         <<" candidateRegisteredGroup="<<bool(candidate)
                         <<" registeredManager=0 registeredGroup="<<bool(candidate && requestContextQualified);
                if(requestReadable)instance<<" function="<<function<<" candidate_group="<<candidateAddress;
                if(candidate) {
                    // Origin is immutable and selected after constructor qualification;
                    // permitted fixtures retain their own role even with stock caller/profile bytes.
                    constexpr const char* origins[]={"stock_startup","stock_stream","fixture_startup","fixture_stream"};
                    parameters<<std::dec<<" profile_source=cached_candidate origin="<<origins[unsigned(candidate->origin)]
                              <<" member_count="<<candidate->count<<" ring_bytes="<<candidate->ringSize
                              <<" entry_capacity="<<candidate->entryCount<<" requested_entries="<<(candidate->entryCount-3)
                              <<" allocator_override="<<bool(candidate->allocatorOverride);
                    ownership<<" candidateGroupPhase="<<unsigned(candidate->phase);
                    instance<<" candidateGroupGeneration="<<candidate->generation<<" root="<<candidate->root
                            <<" reader_identifier="<<candidate->identifier<<" create_caller="<<candidate->createCaller;
                } else parameters<<" profile_source=unknown";
                if(queueReadable)instance<<" queueBase="<<queueBase<<" queueUsed="<<queueUsed
                                         <<" queueCapacity="<<queueCapacity<<" queueEnd="<<queueEnd;
                if(candidate && requestContextQualified) {
                    ownership<<" groupPhase="<<unsigned(candidate->phase);
                    instance<<" group="<<candidate->address<<" groupGeneration="<<candidate->generation;
                }
            } else {
                if(pc!=0x8238D568 && pc!=0x8238D640 && pc!=0x8238D480 &&
                   pc!=0x8238CE90 && pc!=0x8233D5E8) return;
                kind=pc==0x8238D568?"audio-reader-claim":
                     pc==0x8238D480?"audio-reader-reset":"audio-reader-release";
                instance<<std::hex<<"r3="<<ctx.r3.u32<<" r4="<<ctx.r4.u32;
                const auto manager=byHandle(ctx.r3.u32);
                const auto groupFound=groups.find(ctx.r4.u32);
                auto group=manager?manager->group.lock():
                           groupFound!=groups.end()?groupFound->second:nullptr;
                ownership<<"registeredManager="<<bool(manager)<<" registeredGroup="<<bool(group);
                if(manager) {
                    parameters<<std::dec<<" ring="<<manager->size<<" entries="<<manager->entryCount;
                    ownership<<" managerPhase="<<unsigned(manager->phase);
                    instance<<" manager="<<manager->address<<" generation="<<manager->generation<<" epoch="<<manager->epoch
                            <<" operations="<<manager->operations;
                }
                if(group) {
                    ownership<<" groupPhase="<<unsigned(group->phase);
                    instance<<" group="<<group->address<<" groupGeneration="<<group->generation;
                }
                if(pc==0x8238D640) {
                    const auto claim=claims.find(ctx.r4.u32);
                    ownership<<" claimed="<<(claim!=claims.end());
                    if(claim!=claims.end()) {
                        ownership<<" releasing="<<claim->second->releasing;
                        instance<<" token="<<claim->second->identity.token<<" owner="<<claim->second->identity.owner;
                    }
                }
            }
            rt.resourceAudit.observe(kind,asset,uint32_t(ctx.lr),parameters.str(),ownership.str(),0,instance.str());
        } catch(...) {std::fputs("[RESOURCE AUDIT IO FAILURE] reader boundary capture failed\n",stderr);}
    }
    void streamProfile(const PPCContext& ctx) const {
        // The only stock stream producer is 82330540. Its caller supplies a
        // duration and bitrate, so a ring size is not a catalog enumeration.
        // Keep the producer's actual frame, new stream owner, two rounded
        // single-precision products and original fctidz/stfiwx result together.
        constexpr std::pair<uint32_t,uint32_t> words[]={
            {0x8233054C,0x9421FF70},{0x82330554,0xFFE00890},
            {0x823305BC,0x2F1D0000},{0x823305C4,0x3BA00030},
            {0x823305C8,0x7FAA07B4},{0x823305CC,0x83BF0000},
            {0x823305D8,0x7FC6F378},{0x823305DC,0x3FC082D1},
            {0x823305E0,0xF9410050},{0x823305E4,0x39200000},
            {0x823305E8,0x39000000},{0x823305EC,0x80EB1BCC},
            {0x823305F4,0x38800001},{0x823305F8,0x807E8ACC},
            {0x82330600,0xFC00069C},{0x82330604,0xFC000018},
            {0x82330608,0xEDA007F2},{0x8233060C,0xC00BD434},
            {0x82330610,0xEC0D0032},{0x82330614,0xFC00065E},
            {0x82330618,0x7C002FAE},{0x8233061C,0x81610050},
            {0x82330620,0x216B000F},{0x82330624,0x55650036},
            {0x82330628,0x4800CFD1},{0x821DD434,0xC47A0000},
            {0x821DC940,0x82334140},{0x821DC948,0x82330330},
        };
        for(const auto& [pc,expected]:words)
            require(word(pc)==expected,"Original stream reader producer bytes changed");
        require(ctx.lr==0x8233062C && !(ctx.r1.u32&15) &&
                uint64_t(ctx.r1.u32)+0x90<=0xFFFFFFFFull && word(ctx.r1.u32)==ctx.r1.u32+0x90 &&
                ctx.r29.u32==0x821DC850 && ctx.r30.u32==0x82D10000 && ctx.r31.u32,
                "Original stream reader producer lost its frame or retained owner");
        bytes(ctx.r31.u32,0x15C);bytes(ctx.r28.u32,4,true);
        require(word(ctx.r31.u32)==ctx.r29.u32 && word(ctx.r31.u32+4)==0xFFFFFFFF &&
                !word(ctx.r31.u32+0x148) && word(ctx.r31.u32+0x154)==0xBEDFACED &&
                ctx.r3.u32==word(0x82D08ACC) && ctx.r4.u32==1 && ctx.r6.u32<=253 &&
                word(ctx.r31.u32+0x1C)==std::max(1u,ctx.r6.u32) &&
                ctx.r7.u32==word(0x82E31BCC) && word(ctx.r1.u32+0x58)==ctx.r7.u32 &&
                !ctx.r8.u32 && !ctx.r9.u32,
                "Original stream reader producer owner or group arguments changed");
        // Tokens retain an eight-bit entry index. The group adds three entries;
        // preserve that representable bound without imposing the observed four.
        require(ctx.r10.s64>0 && ctx.r10.s64<=std::numeric_limits<int32_t>::max() &&
                std::isfinite(ctx.f31.f64) && ctx.f31.f64>0,
                "Original stream reader duration or bitrate is invalid");
        const double rate=double(float(ctx.r10.s64));
        const double product=double(float(rate*ctx.f31.f64));
        const double scaled=double(float(product*-1000.0));
        require(std::isfinite(product) && std::isfinite(scaled) && product==ctx.f13.f64 &&
                scaled<=0 && scaled>=-double(std::numeric_limits<int32_t>::max()-15),
                "Original stream reader ring arithmetic exceeds its containing extent");
        const auto converted=int64_t(scaled); // Original fctidz truncates toward zero.
        const auto low=uint32_t(converted);
        const auto adjusted=15u-low;
        const auto ring=adjusted&~15u;
        require(ctx.f0.s64==converted && word(ctx.r1.u32+0x50)==low && ctx.r11.u32==adjusted &&
                ctx.r5.u32==ring && ring<=uint32_t(std::numeric_limits<int32_t>::max()) &&
                uint64_t(ring)+0x50<=0xFFFFFFFFull,
                "Original stream reader ring differs from the producer's actual calculation");
        // A positive duration whose rounded product is smaller than one byte
        // legitimately truncates to zero. The original constructor builds an
        // empty reader with threshold zero; empty claim/reset/retirement remain
        // valid. Nonempty claims still require their payload inside the ring.
    }
};
EngineAudioReader::EngineAudioReader(Runtime& rt):state(std::make_unique<State>(rt)) {}
EngineAudioReader::~EngineAudioReader() {
    if(!state->groups.empty()) std::fprintf(stderr,"[NATIVE AUDIO] terminal reader provenance retained: groups=%zu managers=%zu claims=%zu\n",
        state->groups.size(),state->managers.size(),state->claims.size());
}
std::shared_ptr<EngineAudioReader> audioReaders(Runtime& rt,bool create) {
    std::lock_guard lock(rt.audioMutex);
    if(create && !rt.engineAudioReader) rt.engineAudioReader=std::make_shared<EngineAudioReader>(rt);
    return rt.engineAudioReader;
}
void EngineAudioReader::permitFixtureGroup(PPCContext& ctx,bool enabled) {
    auto& s=*state;std::lock_guard lock(s.mutex);
    if(enabled) s.fixtures.insert(&ctx);else s.fixtures.erase(&ctx);
}
void EngineAudioReader::observe(uint32_t pc,PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::unique_lock lock(s.mutex);
    s.auditBoundary(pc,ctx,base);
    require(active==&s.rt && base==s.rt.base && currentContext==&ctx,"Invalid audio reader callback runtime/context");
    s.rt.checkRunning();
    using Phase=State::Phase;
    const auto thread=GetCurrentThreadId();
    switch(pc) {
    case 0x823226E8:
        require(!s.serviceAllocator && s.groups.empty() && s.managers.empty() && s.creating.empty() && ctx.r3.u32,
                "Original reader service allocator replaced while still active");
        s.bytes(ctx.r3.u32,4);s.bytes(s.word(ctx.r3.u32),0x10);
        require(s.word(s.word(ctx.r3.u32)+8) && s.word(s.word(ctx.r3.u32)+0xC),
                "Original reader service allocator callbacks are missing");
        s.serviceAllocator=ctx.r3.u32;break;
    case 0x823227DC:
        require(s.groups.empty() && s.managers.empty() && s.claims.empty() && s.creating.empty() &&
                s.claiming.empty() && s.releasing.empty() && s.resetting.empty() && s.closing.empty() &&
                s.freeing.empty() && s.groupFrees.empty() && s.word(0x82E36B94)==s.serviceAllocator,
                "Original reader allocator clear retains live ownership or operations");
        s.serviceAllocator=0;break;
    case 0x8233D5F8: {
        std::fprintf(stderr,"[NATIVE AUDIO] reader request caller=%08X id=%08X count=%u ring=%X entries=%u root=%08X allocator=%08X aux=%08X\n",
            uint32_t(ctx.lr),ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,ctx.r7.u32,ctx.r8.u32,ctx.r9.u32);
        require(s.serviceAllocator && s.word(0x82E36B94)==s.serviceAllocator,
                "Reader group construction preceded the original allocator installation");
        require(!s.creating.contains(&ctx),"Reentrant audio reader group construction");
        const bool fixture=s.fixtures.contains(&ctx);
        const bool startup=(ctx.lr==0x82816808 || fixture) && ctx.r3.u32==0x2EA8FB98 &&
            ctx.r4.u32==4 && ctx.r5.u32==0x64000 && ctx.r6.u32==10;
        // Direct legacy fixtures retain their three explicit constructor
        // profiles. Production streams must prove the genuine whole producer.
        const bool fixtureStream=fixture && ctx.r3.u32==s.word(0x82D08ACC) &&
            ctx.r4.u32==1 && (ctx.r5.u32==0x55280 || ctx.r5.u32==0x47E00) &&
            ctx.r6.u32==4 && !ctx.r8.u32 && !ctx.r9.u32;
        require(ctx.lr==0x82816808 || ctx.lr==0x8233062C || fixture,"Unqualified audio reader group creation caller");
        const bool stream=ctx.lr==0x8233062C && !fixture;
        if(stream) s.streamProfile(ctx);
        require((startup || stream || fixtureStream) && ctx.r7.u32==s.word(0x82E31BCC),"Unqualified original audio reader group profile");
        const auto origin=fixture?
            (startup?State::ConstructionOrigin::FixtureStartup:State::ConstructionOrigin::FixtureStream):
            (startup?State::ConstructionOrigin::StockStartup:State::ConstructionOrigin::StockStream);
        auto g=std::make_shared<State::Group>(origin,uint32_t(ctx.lr));
        g->generation=s.identity();g->count=ctx.r4.u32;g->ringSize=ctx.r5.u32;g->entryCount=ctx.r6.u32+3;
        g->root=ctx.r7.u32;g->identifier=ctx.r3.u32;
        g->allocatorOverride=ctx.r8.u32;g->allocator=ctx.r8.u32?ctx.r8.u32:s.word(g->root+0x14);
        g->freeFunction=s.word(s.word(g->allocator)+0xC);
        const uint64_t size=uint64_t(g->count)*g->ringSize+((0x20*g->count+0x3F)&~0xFu);
        // The original producer has no global 64-group quota. Its checked
        // allocator and distinct live extents bound ownership; the native map
        // grows with those real allocations rather than imposing a new cap.
        require(size<=0xFFFFFFFF && g->freeFunction,"Audio reader group allocator/extent invalid");
        g->size=uint32_t(size);g->ringOffset=(0x30+0x20*g->count+15)&~15u;
        s.creating.emplace(&ctx,State::Creation{ctx.r1.u32,0,false,g,{}});break;
    }
    case 0x8233DB3C: {
        auto it=s.creating.find(&ctx);if(it==s.creating.end()) break;
        auto& c=it->second;auto& g=*c.group;
        if(ctx.r1.u32!=c.sp-0x100) break;
        require(!c.allocatorCalled && ctx.r3.u32==g.allocator && ctx.r4.u32==g.size && ctx.r5.u32==0 &&
                ctx.r6.u32==1 && ctx.r7.u32==16 && ctx.r8.u32==0 &&
                ctx.ctr.u32==s.word(s.word(g.allocator)+4),"Original reader group allocator request changed");
        c.allocatorCalled=true;break;
    }
    case 0x8233DB40: {
        auto it=s.creating.find(&ctx);if(it==s.creating.end() || ctx.r1.u32!=it->second.sp-0x100) break;
        require(it->second.allocatorCalled && ctx.lr==pc,"Unscoped reader group allocation return");
        it->second.allocationResult=ctx.r3.u32;break;
    }
    case 0x8233D644: {
        auto& c=s.creating.at(&ctx);auto& g=*c.group;
        require(ctx.r1.u32==c.sp-0xA0 && c.allocatorCalled && s.word(ctx.r1.u32+0x50)==c.allocationResult &&
                ctx.r29.u32==g.count && ctx.r26.u32==g.ringSize && ctx.r30.u32==g.root,
                "Original reader group allocation continuation changed");
        if(!c.allocationResult) break;
        g.address=c.allocationResult;require(!(g.address&15),"Original reader group alignment changed");s.bytes(g.address,g.size);
        for(const auto& [a,other]:s.groups)
            require(uint64_t(g.address)>=uint64_t(a)+other->size || uint64_t(a)>=uint64_t(g.address)+g.size,
                    "Original reader group reused a live allocation");
        s.groups.emplace(g.address,c.group);break;
    }
    case 0x8238CD48: {
        auto it=s.creating.find(&ctx);if(it==s.creating.end()) break;
        auto& c=it->second;auto& g=*c.group;
        require(ctx.r1.u32==c.sp-0x120 && !c.pending && g.address && g.managers.size()<g.count &&
                ctx.r3.u32==s.word(0x82E36B94) && ctx.r4.u32==0x218 && ctx.r6.u32==1 &&
                ctx.ctr.u32==s.word(s.word(ctx.r3.u32)+8),"Original reader manager allocator request changed");
        c.pending=std::make_shared<State::Manager>();auto& m=*c.pending;
        m.generation=s.identity();m.group=c.group;m.allocator=ctx.r3.u32;m.freeFunction=s.word(s.word(m.allocator)+0xC);
        m.ring=g.address+g.ringOffset+uint32_t(g.managers.size())*g.ringSize;m.size=g.ringSize;m.entryCount=g.entryCount;
        require(contains(g.address,g.size,m.ring,m.size),"Original reader ring slice exceeds its containing allocation");
        break;
    }
    case 0x8238CD4C: {
        auto it=s.creating.find(&ctx);if(it==s.creating.end()) break;
        auto& c=it->second;require(c.pending && ctx.r1.u32==c.sp-0x120,"Unscoped reader manager allocation return");
        auto& m=*c.pending;m.address=ctx.r3.u32;
        if(!m.address) {m.phase=Phase::Failed;c.group->phase=Phase::Failed;throw Failure("Original reader manager allocation failed before its invalid null-handle load");}
        s.bytes(m.address,0x218);require(!s.managers.contains(m.address),"Reader manager reused live storage");
        s.managers.emplace(m.address,c.pending);break;
    }
    case 0x8238CD6C: {
        auto it=s.creating.find(&ctx);if(it==s.creating.end()) break;
        const auto& c=it->second;require(bool(c.pending),"Reader constructor lacks allocator provenance");const auto& m=*c.pending;
        require(ctx.r3.u32==m.address && ctx.r4.u32==m.entryCount && ctx.r5.u32==m.ring &&
                ctx.r6.u32==m.size && ctx.r7.u32==0,"Original reader constructor slice changed");break;
    }
    case 0x8233D6C8: {
        auto& c=s.creating.at(&ctx);require(bool(c.pending),"Reader factory returned without manager provenance");
        auto m=c.pending;m->handle=ctx.r3.u32;
        require(ctx.r1.u32==c.sp-0xA0 && m->handle && s.word(m->address+0x4C)==m->handle,
                "Original reader factory returned a different handle");
        s.bytes(m->handle,0x14);m->entries=s.word(m->address+0x38);s.bytes(m->entries,0x138*m->entryCount);
        s.managerIdentity(*m);require(!s.handles.contains(m->handle),"Reader handle reused live storage");
        s.handles.emplace(m->handle,m);c.group->managers.push_back(m);c.pending.reset();break;
    }
    case 0x8233D70C: {
        auto& c=s.creating.at(&ctx);auto& g=*c.group;
        require(ctx.r1.u32==c.sp-0xA0 && ctx.r31.u32==g.address && g.managers.size()==g.count && !c.pending &&
                s.word(g.address)==g.root && s.word(g.address+4)==g.address+0x30 &&
                s.word(g.address+0x18)==g.allocatorOverride && s.word(g.address+0x1C)==g.identifier &&
                PPC_LOAD_U8(g.address+0x20)==g.count,"Original reader group publication incomplete");
        for(size_t i=0;i<g.managers.size();++i) {
            auto& m=*g.managers[i];require(s.word(g.address+0x30+uint32_t(i)*0x20+0x14)==m.handle,"Reader group record does not own its handle");
            m.phase=Phase::Live;
        }
        g.phase=Phase::Live;
        std::fprintf(stderr,"[NATIVE AUDIO] reader group=%08X generation=%llu extent=%X managers=%zu; original allocation observed\n",
            g.address,static_cast<unsigned long long>(g.generation),g.size,g.managers.size());break;
    }
    case 0x8233D710: {
        auto it=s.creating.find(&ctx);require(it!=s.creating.end(),"Unscoped audio reader group return");
        require(!it->second.group->address || it->second.group->phase==Phase::Live,"Incomplete reader group returned");
        s.creating.erase(it);break;
    }
    case 0x8238D568: {
        auto m=s.byHandle(ctx.r3.u32);if(!m) break;s.live(m);s.managerIdentity(*m);
        require(!s.claiming.contains(&ctx),"Reentrant original reader claim");
        s.begin(s.claiming,ctx,State::Operation{ctx.r1.u32,ctx.r3.u32,thread,m,{}});break;
    }
    case 0x8238D634: {
        auto it=s.claiming.find(&ctx);if(it==s.claiming.end()) break;const auto op=it->second;auto m=op.manager;
        require(ctx.r1.u32==op.sp-0x80 && ctx.r31.u32==op.handle && ctx.r30.u32==m->address,"Original reader claim return changed");
        if(ctx.r3.u32) {
            const auto b=ctx.r3.u32;s.bytes(b-4,24);
            const auto token=s.word(b),length=s.word(b+4),payload=s.word(b+8),index=token&255u;
            require(s.word(b+0xC)==1 && s.word(b+0x10)==s.word(op.handle+8) && index<m->entryCount && length &&
                    contains(m->ring,m->size,payload,length),"Original claimed reader node lies outside its live ring/entry");
            const auto entry=m->entries+0x138*index;
            require(s.word(entry)==token && s.word(entry+4)!=0 && !s.claims.contains(b),"Original reader token is stale or node is already claimed");
            auto p=std::make_shared<State::Claim>();auto g=m->group.lock();require(bool(g),"Reader group expired during claim");
            p->manager=m;p->identity={b,op.handle,m->address,g->address,token,payload,length,s.word(entry+0x12C),
                g->generation,m->generation,m->epoch,s.identity(),{}};
            s.claims.emplace(b,p);
        }
        s.leave(m,op.thread);s.claiming.erase(it);break;
    }
    case 0x8238D640: {
        auto m=s.byHandle(ctx.r3.u32);if(!m) break;
        require(m->phase==Phase::Live || m->phase==Phase::Closing,"Original reader release entered a reset/failed manager");
        auto it=s.claims.find(ctx.r4.u32);
        require(it!=s.claims.end() && it->second->manager==m && !it->second->releasing &&
                it->second->identity.epoch==m->epoch && !s.releasing.contains(&ctx),"Duplicate/stale original reader release");
        auto p=it->second;
        require(!m->busy.contains(thread),"Reentrant reader release would wait on its own copy");
        // Reset/close must see this release before the copy drain drops the
        // mutex. An exception is then balanced by the saved operation scope.
        s.begin(s.releasing,ctx,State::Operation{ctx.r1.u32,ctx.r3.u32,thread,m,p});
        p->releasing=true;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(p->copies) {
            s.rt.checkRunning();
            require(std::chrono::steady_clock::now()<deadline,"Reader release timed out draining an owned copy");
            s.changed.wait_for(lock,std::chrono::milliseconds(20));
        }
        break;
    }
    case 0x8238D6CC: {
        auto it=s.releasing.find(&ctx);if(it==s.releasing.end()) break;const auto op=it->second;
        require(ctx.r1.u32==op.sp-0x70,"Original reader release return changed");
        // The node may have been freed by the original reclaimer. Identity only.
        s.claims.erase(op.claim->identity.node);s.leave(op.manager,op.thread);s.releasing.erase(it);break;
    }
    case 0x8238D480: {
        auto m=s.byHandle(ctx.r3.u32);if(!m) break;
        require(!s.resetting.contains(&ctx) && (m->phase==Phase::Live || m->phase==Phase::Closing),"Reentrant/stale reader reset");
        if(m->phase==Phase::Live) m->phase=Phase::Resetting;
        s.drain(lock,m);s.resetting.emplace(&ctx,State::Operation{ctx.r1.u32,ctx.r3.u32,thread,m,{}});break;
    }
    case 0x8238D54C: {
        auto it=s.resetting.find(&ctx);if(it==s.resetting.end()) break;const auto op=it->second;
        require(ctx.r1.u32==op.sp-0x70,"Original reader reset return changed");
        for(auto c=s.claims.begin();c!=s.claims.end();) if(c->second->manager==op.manager) c=s.claims.erase(c);else ++c;
        op.manager->epoch=s.identity();
        if(op.manager->phase==Phase::Resetting) op.manager->phase=Phase::Live;
        s.resetting.erase(it);s.changed.notify_all();break;
    }
    case 0x8233D980: {
        const auto gaddr=s.word(ctx.r3.u32+4);auto it=s.groups.find(gaddr);if(it==s.groups.end()) break;
        auto g=it->second;require(g->phase==Phase::Live,"Duplicate/stale original reader group retirement");g->phase=Phase::Closing;
        for(const auto& m:g->managers) s.drain(lock,m);break;
    }
    case 0x8238CE90: {
        auto m=s.byHandle(ctx.r3.u32);if(!m) break;
        require(!s.closing.contains(&ctx) && m->phase==Phase::Live,"Reader manager close is stale or reentrant");
        m->phase=Phase::Closing;s.drain(lock,m);
        s.closing.emplace(&ctx,State::Operation{ctx.r1.u32,ctx.r3.u32,thread,m,{}});break;
    }
    case 0x8238CF20: case 0x8238CA18: {
        auto m=s.managers.find(ctx.r3.u32);if(m==s.managers.end()) break;
        auto it=s.closing.find(&ctx);
        require(it!=s.closing.end() && it->second.manager==m->second && m->second->phase==Phase::Closing &&
                ctx.lr==(pc==0x8238CF20?0x8238CF08:0x8238CF38),"Reader free bypassed original close/quiescence");break;
    }
    case 0x8238CF54: {
        auto it=s.closing.find(&ctx);if(it==s.closing.end()) break;auto op=it->second;auto m=op.manager;
        require(ctx.r1.u32==op.sp-0xD0 && ctx.r4.u32==m->address && ctx.r3.u32==m->allocator &&
                ctx.ctr.u32==m->freeFunction && s.word(0x82E36B94)==m->allocator && !m->operations && !m->copies &&
                !s.freeing.contains(&ctx),"Original reader manager free changed identity/allocator");
        s.freeing.emplace(&ctx,op);break;
    }
    case 0x8238CF58: {
        auto it=s.freeing.find(&ctx);if(it==s.freeing.end()) break;auto m=it->second.manager;
        require(ctx.lr==pc && ctx.r1.u32==it->second.sp-0xD0,"Original reader manager free return changed");
        m->phase=Phase::Freed;s.handles.erase(m->handle);s.managers.erase(m->address);s.freeing.erase(it);break;
    }
    case 0x8238CF08: {
        auto it=s.closing.find(&ctx);if(it==s.closing.end()) break;
        require(it->second.manager->phase==Phase::Freed && ctx.r1.u32==it->second.sp-0x70,"Original reader close did not complete its free");
        s.closing.erase(it);break;
    }
    case 0x8233D5E8: {
        auto it=s.groups.find(ctx.r4.u32);if(it==s.groups.end()) break;auto g=it->second;
        require(g->phase==Phase::Closing && ctx.r3.u32==g->allocator && ctx.ctr.u32==g->freeFunction &&
                std::all_of(g->managers.begin(),g->managers.end(),[](const auto& m){return m->phase==Phase::Freed;}),
                "Original reader ring free precedes manager teardown or uses a different allocator");
        require(!s.groupFrees.contains(&ctx),"Reentrant reader ring free");
        s.groupFrees.emplace(&ctx,State::FreeGroup{ctx.r1.u32,g});break;
    }
    case 0x8233D5EC: {
        auto it=s.groupFrees.find(&ctx);if(it==s.groupFrees.end()) break; // Early deferred return did not free G.
        require(ctx.r1.u32==it->second.sp && ctx.lr==pc,"Original reader ring free return changed");
        auto g=it->second.group;g->phase=Phase::Freed;s.groups.erase(g->address);s.groupFrees.erase(it);break;
    }
    case 0x823227A8:
        require(s.groups.empty() && s.managers.empty() && s.claims.empty() && s.creating.empty(),
                "Original reader service stop retains live native reader provenance");break;
    default:throw Failure("Unknown original audio reader observation boundary");
    }
}
EngineAudioReader::OwnedClaim EngineAudioReader::copy(uint32_t h,uint32_t b,uint32_t owner,uint32_t token) {
    auto& s=*state;std::shared_ptr<State::Claim> p;const auto thread=GetCurrentThreadId();
    {
        std::lock_guard lock(s.mutex);s.rt.checkRunning();auto it=s.claims.find(b);
        require(it!=s.claims.end(),"EXm0 source lacks an actual registered original reader claim");p=it->second;
        s.live(p->manager);const auto& id=p->identity;s.managerIdentity(*p->manager);
        const auto entry=p->manager->entries+0x138*(token&255u);
        const bool qualified=!p->releasing && id.handle==h && id.owner==owner && id.token==token && id.epoch==p->manager->epoch &&
                s.word(b)==token && s.word(b+4)==id.length && s.word(b+8)==id.address && s.word(b+0xC)==1 &&
                s.word(b+0x10)==s.word(h+8) && s.word(entry)==token && s.word(entry+4)!=0 &&
                s.word(entry+0x128)==0x823412C0 && s.word(entry+0x12C)==owner;
        if(!qualified) {
            std::fprintf(stderr,"[NATIVE AUDIO READER COPY REJECTED] B=%08X H=%08X/%08X owner=%08X/%08X token=%08X/%08X epoch=%llu/%llu releasing=%u M=%08X E=%08X length=%08X address=%08X H8=%08X B words=%08X,%08X,%08X,%08X,%08X E words=%08X,%08X,%08X,%08X\n",
                b,h,id.handle,owner,id.owner,token,id.token,
                static_cast<unsigned long long>(p->manager->epoch),static_cast<unsigned long long>(id.epoch),
                unsigned(p->releasing),id.manager,entry,id.length,id.address,s.word(h+8),
                s.word(b),s.word(b+4),s.word(b+8),s.word(b+0xC),s.word(b+0x10),
                s.word(entry),s.word(entry+4),s.word(entry+0x128),s.word(entry+0x12C));
            throw Failure("EXm0 reader token/callback/source identity changed before owned copy");
        }
        require(id.length>=8 && id.length<=1024*1024,"EXm0 reader claim exceeds bounded compressed storage");
        // Reserve the potentially allocating thread entry before changing
        // counters, so allocation failure cannot strand a copy gate.
        auto& busy=p->manager->busy[thread];
        ++busy;++p->copies;++p->manager->copies;
    }
    const auto finish=[&] {
        std::lock_guard lock(s.mutex);--p->copies;--p->manager->copies;
        auto it=p->manager->busy.find(thread);if(it!=p->manager->busy.end() && !--it->second) p->manager->busy.erase(it);
        s.changed.notify_all();
    };
    try {
        auto result=p->identity;
        auto* bytes=s.rt.pointer(result.address,result.length,false);
        result.bytes.assign(bytes,bytes+result.length);finish();return result;
    }catch(...) {finish();throw;}
}
void EngineAudioReader::validate(const OwnedClaim& expected,bool forRelease) const {
    const auto& s=*state;std::lock_guard lock(s.mutex);s.rt.checkRunning();
    const auto it=s.claims.find(expected.node);
    require(it!=s.claims.end() && !it->second->releasing,"Audio source claim was released or replaced");
    const auto& p=*it->second;const auto& id=p.identity;
    if(forRelease) {
        const auto group=p.manager->group.lock();
        require(group && (group->phase==State::Phase::Live || group->phase==State::Phase::Closing) &&
                (p.manager->phase==State::Phase::Live || p.manager->phase==State::Phase::Closing),
                "Audio source release requires its original live or closing reader generation");
    }else s.live(p.manager);
    s.managerIdentity(*p.manager);
    require(id.node==expected.node && id.handle==expected.handle && id.manager==expected.manager &&
            id.group==expected.group && id.token==expected.token && id.address==expected.address &&
            id.length==expected.length && id.owner==expected.owner &&
            id.groupGeneration==expected.groupGeneration && id.managerGeneration==expected.managerGeneration &&
            id.epoch==expected.epoch && id.sequence==expected.sequence && id.epoch==p.manager->epoch,
            "Audio source claim generation/sequence changed");
    require(s.word(id.node)==id.token && s.word(id.node+4)==id.length &&
            s.word(id.node+8)==id.address && s.word(id.node+0xC)==1,
            "Audio source claim metadata changed");
}
void EngineAudioReader::validateReleased(const OwnedClaim& expected) const {
    const auto& s=*state;std::lock_guard lock(s.mutex);s.rt.checkRunning();
    require(!s.claims.contains(expected.node),"Original reader release did not retire the source claim");
    const auto m=s.managers.find(expected.manager);
    require(m!=s.managers.end() && m->second->generation==expected.managerGeneration &&
            m->second->epoch==expected.epoch,"Original reader release changed manager generation");
    const auto g=m->second->group.lock();
    require(g && g->address==expected.group && g->generation==expected.groupGeneration,
            "Original reader release changed containing group generation");
}
void EngineAudioReader::excludeOutput(std::span<uint8_t> output) const {
    const auto& s=*state;std::lock_guard lock(s.mutex);s.rt.checkRunning();
    require(!output.empty(),"Empty EXm0 output provenance check");
    const auto first=reinterpret_cast<uintptr_t>(output.data()),last=first+output.size();
    const auto exclude=[&](uint32_t address,uint32_t bytes) {
        const auto start=reinterpret_cast<uintptr_t>(s.rt.pointer(address,bytes,false));
        require(last<=start || start+bytes<=first,"EXm0 PCM aliases original reader storage");
    };
    for(const auto& [address,g]:s.groups) exclude(address,g->size);
    for(const auto& [address,m]:s.managers) {
        exclude(address,0x218);exclude(m->handle,0x14);
        exclude(m->entries,0x138*m->entryCount);
    }
    for(const auto& [address,p]:s.claims) {
        exclude(address-4,24);exclude(p->identity.address,p->identity.length);
    }
}
void EngineAudioReader::unwind(PPCContext& ctx) noexcept {
    auto& s=*state;
    try {
        std::lock_guard lock(s.mutex);
        for(auto* operations:{&s.claiming,&s.releasing}) {
            auto it=operations->find(&ctx);if(it==operations->end()) continue;
            auto m=it->second.manager;m->phase=State::Phase::Failed;s.leave(m,it->second.thread);operations->erase(it);
        }
        for(auto* operations:{&s.resetting,&s.closing,&s.freeing}) {
            auto it=operations->find(&ctx);if(it==operations->end()) continue;
            it->second.manager->phase=State::Phase::Failed;operations->erase(it);
        }
        auto c=s.creating.find(&ctx);if(c!=s.creating.end()) {c->second.group->phase=State::Phase::Failed;s.creating.erase(c);}
        auto f=s.groupFrees.find(&ctx);if(f!=s.groupFrees.end()) {f->second.group->phase=State::Phase::Failed;s.groupFrees.erase(f);}
        s.changed.notify_all();
    }catch(...) {std::fprintf(stderr,"[AUDIO] reader unwind suppressed secondary failure\n");} // Unwind never masks the original failure; all guest ownership stays quarantined.
}
void unwindAudioReaderCall(PPCContext& ctx) noexcept {
    try {if(active) if(auto readers=audioReaders(*active)) readers->unwind(ctx);}catch(...) {std::fprintf(stderr,"[AUDIO] reader call unwind suppressed secondary failure\n");}
}
void EngineAudioReader::requireStopped() const {
    const auto& s=*state;std::lock_guard lock(s.mutex);
    require(s.groups.empty() && s.managers.empty() && s.claims.empty() && s.creating.empty(),
            "Native audio stop retains original reader leases");
}
EngineAudioReader::Snapshot EngineAudioReader::snapshot() const {
    const auto& s=*state;std::lock_guard lock(s.mutex);Snapshot result{s.groups.size(),s.managers.size(),s.claims.size()};
    for(const auto& [a,m]:s.managers) {result.copies+=m->copies;result.operations+=m->operations;}return result;
}
}

#define READER_BOUNDARY(address) \
void SimpsonsAudioReader##address(PPCContext& ctx,uint8_t* base) { \
    if(!Simpsons::active || base!=Simpsons::active->base) throw Simpsons::Failure("Invalid audio reader runtime"); \
    if constexpr(0x##address==0x8233D5F8) \
        if(Simpsons::active->audioBoundaryObserver) Simpsons::active->audioBoundaryObserver(0x##address,ctx,base); \
    std::shared_ptr<Simpsons::EngineAudioOwners> owners; \
    if constexpr(0x##address==0x8238D640 || 0x##address==0x8238D6CC) { \
        std::lock_guard lock(Simpsons::active->audioMutex);owners=Simpsons::active->engineAudio; \
    } \
    if constexpr(0x##address==0x8238D640) if(owners) owners->readerRelease(0x##address,ctx,base); \
    if(auto r=Simpsons::audioReaders(*Simpsons::active,0x##address==0x823226E8)) r->observe(0x##address,ctx,base); \
    if constexpr(0x##address==0x8238D6CC) if(owners) owners->readerRelease(0x##address,ctx,base); \
}
READER_BOUNDARY(823226E8) READER_BOUNDARY(823227DC)
READER_BOUNDARY(8233D5F8) READER_BOUNDARY(8233DB3C) READER_BOUNDARY(8233DB40)
READER_BOUNDARY(8233D644) READER_BOUNDARY(8238CD48) READER_BOUNDARY(8238CD4C)
READER_BOUNDARY(8238CD6C) READER_BOUNDARY(8233D6C8) READER_BOUNDARY(8233D70C) READER_BOUNDARY(8233D710)
READER_BOUNDARY(8238D568) READER_BOUNDARY(8238D634) READER_BOUNDARY(8238D640) READER_BOUNDARY(8238D6CC)
READER_BOUNDARY(8238D480) READER_BOUNDARY(8238D54C) READER_BOUNDARY(8233D980)
READER_BOUNDARY(8238CE90) READER_BOUNDARY(8238CF20) READER_BOUNDARY(8238CA18)
READER_BOUNDARY(8238CF54) READER_BOUNDARY(8238CF58) READER_BOUNDARY(8238CF08)
READER_BOUNDARY(8233D5E8) READER_BOUNDARY(8233D5EC) READER_BOUNDARY(823227A8)
#undef READER_BOUNDARY
