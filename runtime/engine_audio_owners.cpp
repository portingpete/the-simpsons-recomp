#include "engine_audio.h"
#include "engine_audio_reader.h"
#include "engine_audio_output.h"
#include "engine_cpu_calls.h"
#include "heap_page_index.h"
#include "filesystem.h"
#include "audio_source_file_receipt.h"
#include "audio/native_xma_codec.h"
#include "audio/ea_xma_block.h"
#include "audio/audio_catalog.h"
#include "audio/amx_audio_catalog.h"
#include "audio/resident_audio_catalog.h"
#include "audio/resident_xma.h"
#include "audio/xma_float_output.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <shared_mutex>
#include <limits>
#include <bcrypt.h>

namespace Simpsons {
namespace {
constexpr uint32_t descriptor=0x82D073AC,tag=0x45586D30,vtable=0x821DCAE0;
std::atomic<uint64_t> nextGeneration{1};
uint64_t generation() {
    auto value=nextGeneration.load();
    while(value!=std::numeric_limits<uint64_t>::max())
        if(nextGeneration.compare_exchange_weak(value,value+1)) return value;
    throw Failure("Native audio generation space exhausted");
}
bool overlap(uint32_t a,uint32_t n,uint32_t b,uint32_t m) {
    return uint64_t(a)<uint64_t(b)+m && uint64_t(b)<uint64_t(a)+n;
}
bool contains(uint32_t allocation,uint32_t extent,uint32_t address,uint32_t size) {
    return address>=allocation && uint64_t(address)+size<=uint64_t(allocation)+extent;
}
void require(bool value,const char* why) {if(!value) throw Failure(why);}
// Sample routine successful audio receipts across instances. The original
// source/PCM/retirement work and every rejection stay outside this condition.
bool sampleSuccessLog(uint32_t& counter) noexcept {
    const uint32_t n=counter++;
    return n<4 || (n%512)==0;
}
// Region metadata may include inaccessible stack guards. Translate its backing
// interval without dereferencing it, using the same apertures as Runtime::pointer.
uint64_t backingAddress(uint32_t address) {
    if(address>=0xC0000000 && address<0xE0000000) return uint64_t(address)-0x20000000;
    if(address>=0xE0000000 && address<0xFFD00000) return uint64_t(address)-0x40000000+0x1000;
    return address;
}
std::string audioHash(std::span<const uint8_t> bytes) {
    if(bytes.size()>ULONG_MAX) throw Failure("Audio hash extent overflow");
    std::array<uint8_t,32> hash{};
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(bytes.data()),
                  ULONG(bytes.size()),hash.data(),ULONG(hash.size()))<0)
        throw Failure("Unable to hash owned audio source");
    std::string text; text.reserve(64);
    for(auto b:hash) {text.push_back("0123456789abcdef"[b>>4]);text.push_back("0123456789abcdef"[b&15]);}
    return text;
}
}
struct EngineAudioOwners::State {
    Runtime& runtime;
    mutable std::mutex mutex;
    // Guards `heap`, `heapIndex` and HeapAllocation::closing for allocationSpan(), which the
    // render thread calls per mesh draw and which reads nothing else: holding `mutex` there
    // made it wait behind every audio-thread observation. Writers hold `mutex` and then this
    // lock exclusively, only around the mutation itself; readers under `mutex` need nothing more.
    mutable std::shared_mutex heapMutex;
    std::unique_ptr<Audio::NativeXmaFactory> factory;
    std::shared_ptr<const Audio::AudioCatalog> catalog;
    std::shared_ptr<const Audio::AmxAudioCatalog> amxCatalog;
    std::shared_ptr<const Audio::ResidentAudioCatalog> residentCatalog;
    uint32_t reserved=0;
    uint32_t residentSourceLogs{},constructedLogs{},pcmLogs{},deliveredRetirementLogs{};
    struct HeapAllocation {
        uint32_t address,extent,flags,manager,allocate,release,caller;
        uint64_t generation;
        bool closing=false;
    };
    struct HeapCall {
        uint32_t sp,address,bytes,flags,caller,manager=0,allocate=0,release=0;
        bool called=false;
    };
    struct HeapFree {uint32_t sp,address,flags;uint64_t generation;bool called=false;};
    struct ResidentBank {
        const Audio::ResidentBankProfile* profile=nullptr;
        const Audio::ResidentAudioCatalog::Bank* catalogBank=nullptr;
        Audio::ResidentBankProfile catalogProfile{};
        uint32_t firstHeaderOffset=0;
        uint32_t allocation=0,extent=0,source=0,sourceAllocation=0,sourceExtent=0;
        uint32_t resource=0,record=0,handle=0,metadata=0;
        uint64_t generation=0,sourceGeneration=0,resourceGeneration=0,recordGeneration=0;
        bool retiring=false;
        std::vector<uint8_t> bytes;
    };
    struct AmxOwner {
        const Audio::AmxAudioCatalog::Payload* payload=nullptr;
        uint32_t base=0,allocation=0,extent=0;
        uint64_t generation=0;
    };
    std::unordered_map<uint32_t,HeapAllocation> heap;
    // Containment index over `heap` (maintained at its only two mutation sites).
    HeapPageIndex heapIndex;
    std::unordered_map<PPCContext*,std::vector<HeapCall>> heapCalls;
    std::unordered_map<PPCContext*,std::vector<HeapFree>> heapFrees;
    struct BankLoad {
        uint32_t sp,request,plugin,stream,origin,flags;
        std::shared_ptr<ResidentBank> bank;
    };
    struct BankSplit {
        uint32_t sp,callback;
        std::shared_ptr<ResidentBank> bank;
        bool allocating=false,copied=false,returned=false,published=false;
    };
    std::unordered_map<PPCContext*,BankLoad> bankLoads;
    std::unordered_map<PPCContext*,BankSplit> bankSplits;
    std::unordered_map<uint32_t,std::shared_ptr<ResidentBank>> bankResources,banks;
    struct Source {
        uint64_t key=0;
        uint32_t allocation=0,charged=0,block=0,stream=0,streamState=0,streamHandle=0;
        uint32_t voice=0,voiceIndex=0,request=0,slot=0,samples=0,flag=0,sp=0;
        uint32_t requestSlot=0,producedBefore=0,residentOffset=0;
        uint64_t generation=0,sequence=0,groupGeneration=0,memberGeneration=0,stateGeneration=0;
        bool prepared=false,preflight=false,notified=false,published=false,delivered=false,cancelled=false,releasing=false;
        EngineAudioReader::OwnedClaim claim;
        std::shared_ptr<const ResidentBank> bank;
        std::shared_ptr<const AmxOwner> amx;
        const Audio::AmxAudioCatalog::Cue* amxCue=nullptr;
        Audio::XmaSource::Receipt receipt;
        std::shared_ptr<Audio::XmaSource> decoder;
    };
    struct Instance {
        const uint64_t generation;
        const uint32_t channels;
        bool enabled=true;
        bool configured=false,terminal=false;
        uint32_t currentSlot=0,remaining=0,carry=0,selector=3;
        uint64_t nextSequence=1,quotas=0;
        // Receipt sequence never rewinds. The original streamed reader does
        // rewind to block 1 (the loop body) after its complete loop segment.
        uint32_t catalogNextBlock=1,catalogProduced=0;
        std::vector<uint32_t> catalogCandidates;
        std::shared_ptr<Audio::XmaSource> source;
        std::array<std::shared_ptr<Source>,20> queue;
        struct Layer {uint8_t channels;};
        std::vector<Layer> layers;
        struct Progress {
            PPCContext* context=nullptr;
            uint32_t sp=0,before=0,quota=0,slot=0;
            bool advancing=false;
        } progress;
        Instance(uint64_t identity,uint32_t count):generation(identity),channels(count) {
            for(uint32_t left=count;left;left-=std::min(left,2u)) layers.push_back({uint8_t(std::min(left,2u))});
        }
    };
    enum class Phase {Allocated,Constructed,Live,Failed,Destroying,Released};
    struct Record {
        uint32_t address,extent,channels,owner,allocator,freeFunction;
        uint64_t generation;
        Phase phase=Phase::Allocated;
        std::array<uint32_t,4> untouched;
        std::shared_ptr<Instance> native;
    };
    struct Creation {
        uint32_t sp,channels,owner,allocator,freeFunction,address=0;
        uint64_t generation;
        bool failed=false;
    };
    struct Destruction {uint32_t sp,address;bool failedCreation;};
    std::unordered_map<uint32_t,Record> records;
    std::unordered_map<PPCContext*,Creation> creations;
    std::unordered_map<PPCContext*,Destruction> destructions;
    // Reader claims are keyed by B; resident requests have a distinct namespace.
    std::unordered_map<uint64_t,std::shared_ptr<Source>> sources;
    std::unordered_map<PPCContext*,std::shared_ptr<Source>> producers;
    struct SourceFree {
        uint32_t sp,returnPC,account;
        std::shared_ptr<Source> source;
        bool readerEntered=false,readerReturned=false;
    };
    std::unordered_map<PPCContext*,SourceFree> frees;
    // These are observations of original allocator calls, never inferred from
    // readable RAM. G owns A/W/R; the stream constructor separately owns P.
    struct Group {
        uint32_t address,extent,root,allocator,release;
        uint64_t generation;
        bool closing=false;
    };
    struct Member {
        uint32_t address,extent,group,descriptor;
        uint64_t generation;
        uint32_t states=0,stateBytes=0,voices=0,voiceOffset=0;
        uint64_t stateGeneration=0;
        bool constructed=false,stream=false,closing=false;
        std::vector<std::array<uint8_t,8>> headers;
        std::vector<uint32_t> headerAddresses;
        std::vector<bool> headerCaptured;
        std::vector<std::shared_ptr<const ResidentBank>> residentBanks;
        std::vector<std::shared_ptr<const AmxOwner>> amxOwners;
        std::vector<const Audio::AmxAudioCatalog::Cue*> amxCues;
    };
    struct Allocation {
        uint32_t sp,root,allocator,release,bytes,out,address=0;
        bool called=false,returned=false;
    };
    struct MemberBuild {uint32_t sp,address;};
    struct StorageFree {uint32_t sp,address,allocator,release;};
    std::unordered_map<uint32_t,Group> groups;
    std::unordered_map<uint32_t,Member> members;
    std::unordered_map<PPCContext*,Allocation> groupAllocations,stateAllocations;
    std::unordered_map<PPCContext*,MemberBuild> memberBuilds;
    std::unordered_map<PPCContext*,StorageFree> groupFrees,stateFrees;
    explicit State(Runtime& value):runtime(value) {}
    void auditProducer(const PPCContext& ctx) const noexcept {
        if(!runtime.resourceAudit.active())return;
        // Cached metadata is safe even when the incoming guest owner/block is
        // malformed. Capture it before frame/member/source admission; never
        // dereference a candidate identity merely to explain its rejection.
        try {
            std::ostringstream asset,parameters,ownership,instance;
            const auto found=members.find(ctx.r3.u32);
            parameters<<"fresh="<<ctx.r6.u32<<" control="<<ctx.r7.u32;
            ownership<<"registeredMember="<<(found!=members.end());
            instance<<std::hex<<"member="<<ctx.r3.u32<<" index="<<ctx.r5.u32
                    <<" block="<<ctx.r4.u32<<" stack="<<ctx.r1.u32;
            if(found!=members.end()) {
                const auto& member=found->second;const uint32_t index=ctx.r5.u32;
                ownership<<" constructed="<<member.constructed<<" closing="<<member.closing;
                instance<<" memberGeneration="<<member.generation<<" group="<<member.group
                        <<" stateGeneration="<<member.stateGeneration;
                if(index<member.headers.size() && index<member.headerCaptured.size() && member.headerCaptured[index]) {
                    asset<<"EAAC:"<<std::hex<<std::setfill('0');
                    for(uint8_t byte:member.headers[index]) asset<<std::setw(2)<<unsigned(byte);
                    if(index<member.residentBanks.size() && member.residentBanks[index]) {
                        const auto& bank=*member.residentBanks[index];ownership<<" residentBank=1 retiring="<<bank.retiring;
                        if(bank.catalogBank) {
                            asset<<" bank="<<bank.catalogBank->path<<" name="<<bank.catalogBank->name;
                            parameters<<" archive="<<bank.catalogBank->path<<" entry="<<bank.catalogBank->entryIndex
                                      <<" payload_sha256="<<std::hex<<std::setfill('0');
                            for(uint8_t byte:bank.catalogBank->payloadHash) parameters<<std::setw(2)<<unsigned(byte);
                            parameters<<std::dec;
                        }
                        if(bank.profile && index<member.headerAddresses.size() && member.headerAddresses[index]>=bank.allocation)
                            asset<<" headerOffset="<<std::dec<<(uint64_t(member.headerAddresses[index])-bank.allocation+bank.profile->audioOffset);
                        instance<<" bankGeneration="<<bank.generation;
                    } else if(index<member.amxOwners.size() && member.amxOwners[index]) {
                        const auto& owner=*member.amxOwners[index];ownership<<" amxOwner=1";
                        if(owner.payload) {
                            asset<<" amx="<<owner.payload->name;
                            parameters<<" payload_sha256="<<std::hex<<std::setfill('0');
                            for(uint8_t byte:owner.payload->payloadHash) parameters<<std::setw(2)<<unsigned(byte);
                            parameters<<std::dec;
                        }
                        if(index<member.amxCues.size() && member.amxCues[index])
                            asset<<" headerOffset="<<std::dec<<member.amxCues[index]->headerOffset;
                        instance<<" amxGeneration="<<owner.generation;
                    } else ownership<<" encodedStream=1";
                } else asset<<"uncaptured-audio-header";
            } else asset<<"unknown-audio-owner";
            runtime.resourceAudit.observe("audio-source",asset.str(),uint32_t(ctx.lr),parameters.str(),ownership.str(),0,instance.str());
        } catch(...) {std::fputs("[RESOURCE AUDIT IO FAILURE] audio source capture failed\n",stderr);}
    }
    // Observer-only relation between an owned reader claim and the guest file read that deposited its bytes
    // (newest reads covering the claim's ring range; the relation is by address recency, never by content),
    // next to the catalog candidates it matched. The claim bytes themselves are already catalog-verified by
    // AudioCatalog::match (block SHA-256). The raw numbers are recorded so agreement is checked, never assumed here.
    void auditSourceFile(const PPCContext& ctx,const EngineAudioReader::OwnedClaim& claim,const Audio::AudioCatalog& catalog,
                         const Audio::AudioCatalog::Match& matched,uint32_t block,uint64_t readSnapshot) const noexcept {
        if(!runtime.resourceAudit.active())return;
        try {
            const auto read=recentFileReadCovering(claim.address,claim.length,readSnapshot);
            std::vector<AudioSourceCandidate> identities;
            for(const auto index:matched.candidates) {
                const auto identity=catalog.candidateIdentity(index);
                if(identity)identities.push_back({identity->sourcePath,identity->audioOffset,identity->audioBytes,identity->streamIndex});
            }
            const auto receipt=formatAudioSourceFileReceipt({claim.address,claim.token,claim.owner,claim.length},block,matched.block.bytes,read,
                                                            matched.candidates.size(),identities);
            runtime.resourceAudit.observe("audio-source-file",receipt.asset,uint32_t(ctx.lr),receipt.parameters,receipt.ownership,0,receipt.instance);
        } catch(...) {std::fputs("[RESOURCE AUDIT IO FAILURE] audio source file capture failed\n",stderr);}
    }
    const Audio::AudioCatalog& audioCatalog() {
        if(!catalog) {
            require(runtime.gameRoot.is_absolute(),"Audio catalog requires the original game root");
            catalog=std::make_shared<Audio::AudioCatalog>(Audio::AudioCatalog::load(
                runtime.gameRoot.parent_path()/"analysis"/"audio_catalog.bin"));
            std::fprintf(stderr,"[NATIVE AUDIO] loaded full stream catalog: %zu sources, %zu streams, %zu blocks\n",
                catalog->sourceCount(),catalog->streamCount(),catalog->blockCount());
        }
        return *catalog;
    }
    const Audio::ResidentAudioCatalog& residentAudioCatalog() {
        if(!residentCatalog) {
            require(runtime.gameRoot.is_absolute(),"Resident audio catalog requires the original game root");
            residentCatalog=std::make_shared<Audio::ResidentAudioCatalog>(Audio::ResidentAudioCatalog::load(
                runtime.gameRoot.parent_path()/"analysis"/"resident_audio_catalog.bin"));
            std::fprintf(stderr,"[NATIVE AUDIO] loaded resident catalog: %zu banks, %zu cues, %zu blocks\n",
                residentCatalog->bankCount(),residentCatalog->cueCount(),residentCatalog->blockCount());
        }
        return *residentCatalog;
    }
    const Audio::AmxAudioCatalog& amxAudioCatalog() {
        if(!amxCatalog) {
            require(runtime.gameRoot.is_absolute(),"AMX audio catalog requires the original game root");
            amxCatalog=std::make_shared<Audio::AmxAudioCatalog>(Audio::AmxAudioCatalog::load(
                runtime.gameRoot.parent_path()/"analysis"/"amx_audio_catalog.bin"));
            std::fprintf(stderr,"[NATIVE AUDIO] loaded AMX catalog: %zu payloads, %zu cues, %zu blocks\n",
                amxCatalog->payloadCount(),amxCatalog->cueCount(),amxCatalog->blockCount());
        }
        return *amxCatalog;
    }
    static uint64_t residentKey(uint32_t request) {return (uint64_t(1)<<32)|request;}
    const HeapAllocation& observedAllocation(uint32_t address,uint32_t bytes,bool exact=false) const {
        const HeapAllocation* found=nullptr;
        for(const auto& [at,allocation]:heap)
            if(!allocation.closing && (!exact || at==address) && contains(at,allocation.extent,address,bytes) &&
               (!found || allocation.extent<found->extent)) found=&allocation;
        if(!found) {
            std::fprintf(stderr,"[RESIDENT SBK] missing actual allocation: address=%08X bytes=%X exact=%u live=%zu\n",address,bytes,unsigned(exact),heap.size());
            throw Failure("Resident SBK lacks an observed original allocation generation");
        }
        return *found;
    }
    void bankSourceIdentity(const ResidentBank& bank) const {
        const auto& allocation=observedAllocation(bank.source,bank.profile->bytes);
        require(allocation.address==bank.sourceAllocation && allocation.extent==bank.sourceExtent &&
                allocation.generation==bank.sourceGeneration,"Resident SBK original source generation changed before copying");
    }
    void residentIdentity(const ResidentBank& bank) const {
        const auto it=heap.find(bank.allocation);
        const auto registered=banks.find(bank.allocation);
        require(it!=heap.end() && !it->second.closing && it->second.generation==bank.generation &&
                it->second.extent==bank.extent && bank.extent>=bank.profile->audioBytes &&
                registered!=banks.end() && registered->second.get()==&bank,
                "Resident SBK lost its original copied-audio allocation generation");
    }
    void amxIdentity(const AmxOwner& owner) const {
        const auto it=heap.find(owner.allocation);
        require(owner.payload && it!=heap.end() && !it->second.closing &&
                it->second.generation==owner.generation && it->second.extent==owner.extent &&
                contains(owner.allocation,owner.extent,owner.base,uint32_t(owner.payload->payloadBytes)),
                "AMX original payload allocation was retired or replaced");
    }
    std::pair<std::shared_ptr<const AmxOwner>,const Audio::AmxAudioCatalog::Cue*>
    copyAmxCue(uint32_t header) {
        const auto& catalog=amxAudioCatalog();
        const std::span<const uint8_t> bytes(runtime.pointer(header,8,false),8);
        std::pair<std::shared_ptr<const AmxOwner>,const Audio::AmxAudioCatalog::Cue*> found;
        for(size_t pi=0;pi<catalog.payloadCount();++pi) {
            const auto* payload=catalog.payload(pi);
            if(!payload || payload->payloadBytes>UINT32_MAX) continue;
            for(uint32_t ordinal=0;ordinal<payload->cueCount;++ordinal) {
                const auto* cue=catalog.cue(*payload,ordinal);
                if(!cue || cue->headerOffset>header ||
                   !std::equal(bytes.begin(),bytes.end(),cue->header.begin())) continue;
                const uint32_t base=header-uint32_t(cue->headerOffset);
                const HeapAllocation* allocation=nullptr;
                for(const auto& [at,item]:heap)
                    if(!item.closing && contains(at,item.extent,base,uint32_t(payload->payloadBytes)) &&
                       (!allocation || item.extent<allocation->extent)) allocation=&item;
                if(!allocation) continue;
                const uint8_t* sourceBytes=nullptr;
                try {sourceBytes=runtime.pointer(base,uint32_t(payload->payloadBytes),false);}
                catch(const Failure&) {continue;}
                const std::span<const uint8_t> source(sourceBytes,size_t(payload->payloadBytes));
                auto be32=[&](size_t at) {
                    return uint32_t(source[at])<<24 | uint32_t(source[at+1])<<16 |
                           uint32_t(source[at+2])<<8 | source[at+3];
                };
                const uint32_t bodyBytes=be32(4);
                if(be32(0)!=9 || be32(8)!=payload->metadataBytes || be32(12)!=bodyBytes ||
                   uint64_t(bodyBytes)+payload->metadataBytes+64!=payload->payloadBytes ||
                   !catalog.verifyAudio(*payload,source)) continue;
                require(!found.first,"Ambiguous original AMX cue allocation");
                auto owner=std::make_shared<AmxOwner>();
                owner->payload=payload;owner->base=base;owner->allocation=allocation->address;
                owner->extent=allocation->extent;owner->generation=allocation->generation;
                found={owner,cue};
            }
        }
        return found;
    }
    std::shared_ptr<const ResidentBank> copyResidentBank(uint32_t header) const {
        for(const auto& [address,bank]:banks) if(contains(address,bank->profile->audioBytes,header,8)) {
            const uint32_t offset=header-address+bank->profile->audioOffset;
            const std::span<const uint8_t> ownedHeader(runtime.pointer(header,8,false),8);
            const auto* profile=Audio::residentXmaProfile(*bank->profile,offset,ownedHeader);
            const auto* cue=bank->catalogBank && residentCatalog?
                residentCatalog->findCue(*bank->catalogBank,offset,ownedHeader):nullptr;
            if(!profile && !cue)return {}; // Unknown source stays unavailable to producer admission.
            residentIdentity(*bank);
            const auto& record=observedAllocation(bank->record,32,true);
            require(!bank->retiring && record.generation==bank->recordGeneration,
                    "Resident SBK header refers to a retiring or replaced bank record");
            require(offset<=bank->bytes.size() && 8<=bank->bytes.size()-offset &&
                    !std::memcmp(ownedHeader.data(),bank->bytes.data()+offset,8),
                    "Resident SBK copied header changed");
            return bank;
        }
        return {}; // Streamed or unqualified bank; producer admission still rejects unsupported residents.
    }
    void beginResident(PPCContext&,uint8_t*,Record&,const Member&,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
    void caller(uint8_t* base) const {
        if(active!=&runtime || base!=runtime.base) throw Failure("Invalid native audio runtime");
        runtime.checkRunning();
    }
    void frame(PPCContext& ctx,uint8_t* base) const {
        caller(base);
        if(currentContext!=&ctx || ctx.r1.u32<0x100 || (ctx.r1.u32&15))
            throw Failure("Invalid original audio callback context/stack");
        runtime.pointer(ctx.r1.u32-0x100,0x100,true);
    }
    void region(uint32_t address,uint32_t bytes,bool write=false) const {
        if(!address || (address&3) || uint64_t(address)+bytes>0x100000000ull)
            throw Failure("Invalid original audio object extent");
        runtime.pointer(address,bytes,write);
    }
    void descriptorIdentity(uint8_t* base) const {
        constexpr std::array<uint32_t,7> words={0x8233E9A8,0x8233E9C8,0x8233EC58,0x8233FAF8,0,tag,0};
        for(unsigned i=0;i<words.size();++i) if(i!=4 && PPC_LOAD_U32(descriptor+4*i)!=words[i])
            throw Failure("Original EXm0 descriptor changed");
        if(PPC_LOAD_U32(vtable)!=0x8233FAF0 || PPC_LOAD_U32(vtable+4)!=0x8233EC00)
            throw Failure("Original EXm0 virtual callbacks changed");
    }
    void noHardware(uint8_t* base) const {
        if(PPC_LOAD_U8(0x82E36B82)) throw Failure("Original XMA hardware pool was initialized");
        for(uint32_t address:{0x82E36CA8u,0x82E36CACu,0x82E36CB0u,0x82E36CB4u,
                              0x82E37314u,0x82E37318u,0x82E3731Cu,0x82E37320u})
            if(PPC_LOAD_U32(address)) throw Failure("Original XMA pool storage/registry became populated");
    }
    void available(uint8_t* base) const {
        if(!factory) throw Failure("Native EXm0 factory is not active");
        descriptorIdentity(base);noHardware(base);
    }
    std::pair<uint32_t,uint32_t> allocator(uint8_t* base) const {
        const auto q=PPC_LOAD_U32(0x82E31BCC);region(q,0x20);
        const auto owner=PPC_LOAD_U32(q+0x14);region(owner,4);
        const auto table=PPC_LOAD_U32(owner);region(table,0x10);
        const auto release=PPC_LOAD_U32(table+0xC);
        if(!PPC_LOAD_U32(table+4) || !release) throw Failure("Original audio allocator callbacks are missing");
        return {owner,release};
    }
    Record& find(uint32_t address) {
        const auto it=records.find(address);
        if(it==records.end()) throw Failure("Missing/stale original EXm0 allocation");
        return it->second;
    }
    Record& live(uint32_t address,uint8_t* base) {
        auto& r=find(address);
        if(r.phase!=Phase::Live || !r.native || r.native->terminal)
            throw Failure("Native EXm0 source operation requires a live generation");
        available(base);owned(r,base,true);return r;
    }
    uint32_t voice(uint32_t a,uint32_t index,uint8_t* base) const {
        const auto& m=streamMember(a,index,base);
        return PPC_LOAD_U32(a+m.voiceOffset+48*index+8);
    }
    const Member& streamMember(uint32_t a,uint32_t index,uint8_t* base,bool closing=false) const {
        const auto it=members.find(a);
        if(it==members.end()) throw Failure("Stream owner lacks an original containing allocation");
        const auto& m=it->second;
        const auto gi=groups.find(m.group);
        if(gi==groups.end() || (!closing && (gi->second.closing || m.closing)) ||
           !m.stream || !m.constructed || !m.states || index>=m.voices)
            throw Failure("Stream owner/voice allocation generation is unavailable");
        const auto& g=gi->second;
        region(g.address,g.extent);region(a,m.extent);region(m.states,m.stateBytes);
        if(PPC_LOAD_U32(g.address+0x3C)!=g.extent || PPC_LOAD_U32(g.address+0x10)!=g.root ||
           PPC_LOAD_U32(a+4)!=g.root || PPC_LOAD_U32(a+8)!=g.address ||
           PPC_LOAD_U32(a+0x10)!=m.descriptor || PPC_LOAD_U32(a+0x1A8)!=m.states ||
           PPC_LOAD_U32(a+0x50)!=m.states+4 || PPC_LOAD_U8(a+0x1C2)!=m.voices ||
           PPC_LOAD_U16(a+0x1BC)!=m.voiceOffset ||
           !contains(a,m.extent,a+m.voiceOffset+48*index,48) ||
           !contains(m.states,m.stateBytes,m.states+4+80*index,80))
            throw Failure("Original stream allocation identity/extent changed");
        return m;
    }
    void sourceIdentity(const Source& p,uint8_t* base,bool releasing=false) const {
        const auto& m=streamMember(p.stream,p.voiceIndex,base,releasing);
        if(m.generation!=p.memberGeneration || m.stateGeneration!=p.stateGeneration ||
           groups.at(m.group).generation!=p.groupGeneration || p.streamState!=m.states+4+80*p.voiceIndex)
            throw Failure("Original EXm0 source containing generation changed");
        if(p.bank) {
            residentIdentity(*p.bank);region(p.streamState,80);
            require(p.key==residentKey(p.request) && !p.allocation && !PPC_LOAD_U32(p.request) &&
                    PPC_LOAD_U32(p.streamState+0x28)==p.streamHandle &&
                    m.residentBanks[p.voiceIndex]==p.bank &&
                    m.headerAddresses[p.voiceIndex]+p.residentOffset==p.block &&
                    (releasing || voice(p.stream,p.voiceIndex,base)==p.voice),
                    "Resident EXm0 source association changed");
            return;
        }
        if(p.amx) {
            amxIdentity(*p.amx);region(p.streamState,80);
            require(p.amxCue && p.key==residentKey(p.request) && !p.allocation &&
                    !PPC_LOAD_U32(p.request) && PPC_LOAD_U32(p.streamState+0x28)==p.streamHandle &&
                    m.amxOwners[p.voiceIndex]==p.amx && m.amxCues[p.voiceIndex]==p.amxCue &&
                    m.headerAddresses[p.voiceIndex]+p.residentOffset==p.block &&
                    (releasing || voice(p.stream,p.voiceIndex,base)==p.voice),
                    "AMX resident EXm0 source association changed");
            return;
        }
        const auto readers=audioReaders(runtime);
        if(!readers) throw Failure("Original EXm0 source reader registry disappeared");
        readers->validate(p.claim,releasing);
        region(p.allocation,20);region(p.streamState,80);region(p.streamHandle,20);
        if(PPC_LOAD_U32(p.allocation+4)!=p.charged || PPC_LOAD_U32(p.allocation+8)!=p.block ||
           PPC_LOAD_U32(p.allocation+0xC)!=1 || PPC_LOAD_U32(p.streamState+0x28)!=p.streamHandle ||
           (!releasing && voice(p.stream,p.voiceIndex,base)!=p.voice) || PPC_LOAD_U32(p.request)!=p.allocation)
            throw Failure("Original EXm0 source allocation/voice association changed");
        // Compressed bytes are owned after the counted reader copy. The ring
        // is not a second lifetime authority and is never reread here.
    }
    void queueIdentity(const Source& p,const Record& r,uint8_t* base) const {
        const auto q=r.address+PPC_LOAD_U32(r.address+0x24)+20*p.slot;
        if(PPC_LOAD_U32(q)!=p.block+8 || PPC_LOAD_U32(q+4) || PPC_LOAD_U32(q+8) ||
           PPC_LOAD_U32(q+0xC)!=p.samples || PPC_LOAD_U8(q+0x10)!=p.flag || PPC_LOAD_U8(q+0x11))
            throw Failure("Original EXm0 queue publication differs from the prepared source");
    }
    void pcmDisjoint(std::span<uint8_t> output,const Record& r,uint32_t d,uint8_t* base) const {
        const auto first=uintptr_t(output.data()),last=first+output.size();
        auto exclude=[&](uint32_t address,uint32_t bytes) {
            const auto start=uintptr_t(runtime.pointer(address,bytes,false));
            require(last<=start || start+bytes<=first,"EXm0 PCM aliases live source/owner/descriptor storage");
        };
        exclude(r.owner,0x100);exclude(d,0x114);
        const uint32_t bank=PPC_LOAD_U32(d+4),index=(d-0x82E33AA0)/0x114;
        exclude(bank-0x10000*index+0x30000,0x80);
        for(const auto& [address,record]:records) exclude(address,record.extent);
        for(const auto& [address,g]:groups) exclude(address,g.extent);
        for(const auto& [address,m]:members) if(m.states) exclude(m.states,m.stateBytes);
        for(const auto& [address,p]:sources) {
            // Only address ranges are inspected; no compressed ring bytes or
            // potentially released B metadata are reread here.
            if(p->bank) exclude(p->bank->allocation,p->bank->extent);
            else if(p->amx) exclude(p->amx->allocation,p->amx->extent);
            else {exclude(p->allocation-4,24);exclude(p->block,p->charged);exclude(p->streamHandle,20);}
        }
        std::lock_guard vmLock(runtime.vmMutex);
        for(const auto& region:runtime.regions)
            if(region.use==MemoryUse::Image || region.use==MemoryUse::Stack || region.use==MemoryUse::Kernel || region.use==MemoryUse::Host) {
                const auto start=uintptr_t(runtime.base)+backingAddress(region.address);
                require(last<=start || start+region.size<=first,"EXm0 PCM aliases protected original CPU storage");
            }
    }
    void cancel(Record& r) {
        if(!r.native) return;
        auto& n=*r.native;n.terminal=true;
        if(n.source) n.source->close();
        n.queue.fill({});n.progress={};
        for(auto& [allocation,p]:sources) if(p->voice==r.address && p->generation==r.generation) {
            if(p->decoder) p->decoder->close();
            p->cancelled=true;
        }
        for(auto it=producers.begin();it!=producers.end();)
            if(it->second->voice==r.address && it->second->generation==r.generation) it=producers.erase(it);else ++it;
    }
    bool looksNative(uint32_t address,uint8_t* base) const {
        if(!address) return false;
        region(address,0x10);
        return PPC_LOAD_U32(address)==vtable || PPC_LOAD_U32(address+0xC)==0x8233EC58;
    }
    void owned(const Record& r,uint8_t* base,bool generic) const {
        region(r.address,r.extent,true);
        const uint32_t v=r.address,layers=(r.channels+1)/2,layerBase=(v+0x5F)&~7u;
        const auto* native=r.native.get();
        const bool configured=native && native->configured;
        if(PPC_LOAD_U32(v)!=vtable || PPC_LOAD_U32(v+4)!=r.owner || PPC_LOAD_U32(v+0xC)!=0x8233EC58 ||
           PPC_LOAD_U32(v+0x10) || PPC_LOAD_U8(v+0x2E)!=r.channels || PPC_LOAD_U32(v+0x34)!=layerBase ||
           PPC_LOAD_U32(v+0x44)!=layers || PPC_LOAD_U32(v+0x3C) ||
           PPC_LOAD_U32(v+0x48)!=(native?native->remaining:0) ||
           PPC_LOAD_U32(v+0x4C)!=(native?native->carry:0) || PPC_LOAD_U8(v+0x54) ||
           PPC_LOAD_U8(v+0x55)!=(configured?0:1))
            throw Failure("Original EXm0 instance layout/state changed");
        constexpr std::array<uint32_t,4> untouchedOffsets={0x28,0x38,0x40,0x50};
        for(unsigned i=0;i<untouchedOffsets.size();++i)
            if(PPC_LOAD_U32(v+untouchedOffsets[i])!=
               (i==1 && native && native->quotas?native->currentSlot:i==3 && configured?native->selector:r.untouched[i]))
                throw Failure("Unconfigured EXm0 field changed without a supported operation");
        for(uint32_t i=0;i<layers;++i)
            for(uint32_t at=0;at<0x18;++at) {
                const uint8_t expected=at==0xC?uint8_t(std::min(2u,r.channels-2*i)):0;
                if(PPC_LOAD_U8(layerBase+0x18*i+at)!=expected)
                    throw Failure("Native EXm0 layer acquired a hardware record or unconfigured state");
            }
        if(!generic) return;
        const uint32_t codecSize=0x58+0x18*layers,queue=(v+codecSize+7)&~7u;
        if(PPC_LOAD_U32(v+8)!=v || PPC_LOAD_U32(v+0x14)!=0x8233FAF8 || PPC_LOAD_U32(v+0x18)!=tag ||
           PPC_LOAD_U32(v+0x20)!=r.extent || PPC_LOAD_U32(v+0x24)!=queue-v ||
           (!configured && PPC_LOAD_U32(v+0x1C)) || PPC_LOAD_U16(v+0x2C) ||
           PPC_LOAD_U8(v+0x2F)>=20 || PPC_LOAD_U8(v+0x30)>=20 || PPC_LOAD_U8(v+0x31)>=20 ||
           PPC_LOAD_U8(v+0x32)!=0x14 || PPC_LOAD_U8(v+0x33))
            throw Failure("Original EXm0 generic allocation/queue state changed");
        for(uint32_t i=0;i<20;++i)
            if(PPC_LOAD_U32(queue+0x14*i+0xC) && (!native || !native->queue[i]))
                throw Failure("Unqualified EXm0 instance has queued source data");
    }
};
void EngineAudioOwners::observeBank(uint32_t pc,PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::unique_lock lock(s.mutex);s.frame(ctx,base);
    if(pc==0x82711918) {
        const uint32_t request=ctx.r4.u32;
        if(ctx.ctr.u32!=0x826F2178 || PPC_LOAD_U32(request+8)!=0x39030F7C) return;
        const uint32_t bankBytes=PPC_LOAD_U32(request+0x14);
        if(bankBytes<24 || bankBytes>16*1024*1024)return;
        const uint32_t root=PPC_LOAD_U32(0x82D09764),stream=PPC_LOAD_U32(request+0x10);
        require(!s.bankLoads.contains(&ctx) && request==ctx.r1.u32+0x60 && ctx.r3.u32==root+8 &&
                PPC_LOAD_U32(ctx.r3.u32)==0x820B84D0 && PPC_LOAD_U32(0x820B84D4)==ctx.ctr.u32 &&
                ctx.r26.u32==stream && ctx.r25.u32==bankBytes &&
                PPC_LOAD_U32(request+4)==ctx.r31.u32+0x1C,
                "Resident SBK named-loader request/dispatcher changed");
        s.region(stream,0x18);
        const uint32_t cursor=PPC_LOAD_U32(stream+0xC),length=PPC_LOAD_U32(stream+0x10);
        const uint64_t origin=uint64_t(PPC_LOAD_U32(stream+0x14))+cursor;
        require(PPC_LOAD_U32(stream)==3 && cursor<=length && bankBytes<=length-cursor &&
                origin && origin+bankBytes<=0x100000000ull,
                "Resident SBK exceeds the original named memory-stream envelope");
        const auto* bytes=s.runtime.pointer(uint32_t(origin),bankBytes,false);
        if(std::memcmp(bytes,"knbs",4)!=0)return;
        const auto& catalog=s.residentAudioCatalog();
        const auto* catalogBank=catalog.findBank(std::span<const uint8_t>(bytes,bankBytes));
        require(catalogBank,"Resident SBK payload is absent from the exact original catalog");
        const auto* firstCue=catalog.cue(*catalogBank,0);
        require(firstCue && firstCue->headerOffset>=catalogBank->audioOffset &&
                firstCue->headerOffset<=UINT32_MAX && catalogBank->payloadBytes==bankBytes &&
                catalogBank->metadataBytes<=UINT32_MAX && catalogBank->audioOffset<=UINT32_MAX &&
                catalogBank->audioBytes<=UINT32_MAX,
                "Resident SBK catalog lacks a bounded first cue or bank split");
        auto bankOwner=std::make_shared<State::ResidentBank>();
        auto& bank=*bankOwner;bank.catalogBank=catalogBank;
        bank.firstHeaderOffset=uint32_t(firstCue->headerOffset);
        bank.bytes.assign(bytes,bytes+bankBytes);
        const auto* certified=Audio::residentBankProfile(bankBytes);
        if(certified && audioHash(bank.bytes)==certified->hash) {
            bank.profile=certified;
            bank.firstHeaderOffset=certified->profiles.front().headerOffset;
            Audio::validateResidentXmaBank(*certified,bank.bytes);
        }else {
            bank.catalogProfile={catalogBank->name.c_str(),bankBytes,
                uint32_t(catalogBank->metadataBytes),uint32_t(catalogBank->audioOffset),
                uint32_t(catalogBank->audioBytes),nullptr,{},{}};
            bank.profile=&bank.catalogProfile;
            require(catalog.verifyBank(*catalogBank,bank.bytes),
                    "Resident SBK source changed after catalog identification");
        }
        require(PPC_LOAD_U32(uint32_t(origin)+8)==bank.profile->metadataBytes &&
                PPC_LOAD_U32(uint32_t(origin)+0x10)==bank.profile->audioOffset &&
                PPC_LOAD_U32(uint32_t(origin)+0xC)==bank.profile->audioBytes &&
                PPC_LOAD_U32(uint32_t(origin)+0x14)==bank.profile->audioBytes,
                "Resident SBK original metadata/audio split changed");
        s.bankLoads.emplace(&ctx,State::BankLoad{ctx.r1.u32,request,root,stream,uint32_t(origin),PPC_LOAD_U32(request+0x20),bankOwner});
        std::fprintf(stderr,"[RESIDENT SBK LOAD] named key=%08X stream=%08X cursor=%X bytes=%u flags=%X; exact original bank hash verified\n",
            ctx.r27.u32,stream,cursor,bank.profile->bytes,PPC_LOAD_U32(request+0x20));
        return;
    }
    if(pc==0x826F1DCC || pc==0x826F1E04 || pc==0x8271191C) {
        const auto it=s.bankLoads.find(&ctx);if(it==s.bankLoads.end()) return;
        auto& load=it->second;auto& bank=*load.bank;
        if(pc==0x8271191C) {
            if(ctx.r1.u32!=load.sp) return;
            require(ctx.r3.u32==bank.resource && bank.resource,"Resident SBK named loader returned a different resource");
            const bool published=s.banks.contains(bank.allocation);
            s.bankLoads.erase(it);
            lock.unlock();
            if(published && s.runtime.audioBoundaryObserver) s.runtime.audioBoundaryObserver(pc,ctx,base);
            return;
        }
        require(ctx.r1.u32==load.sp-0x90 && ctx.r31.u32==load.request && ctx.r29.u32==load.plugin,
                "Resident SBK original load scope changed");
        if(pc==0x826F1DCC) {
            require(ctx.lr==pc && !bank.source && ctx.r3.u32 &&
                    ((load.flags&1)?ctx.r3.u32==load.origin:
                     ctx.r3.u32==PPC_LOAD_U32(load.request+0x1C)),
                    "Resident SBK source return differs from original allocation/borrow branch");
            const auto& allocation=s.observedAllocation(ctx.r3.u32,bank.profile->bytes,!(load.flags&1));
            if(!(load.flags&1))
                require(PPC_LOAD_U32(load.request+0x18)==allocation.manager,
                        "Resident SBK source was returned by a different allocator");
            require(!std::memcmp(s.runtime.pointer(ctx.r3.u32,bank.profile->bytes,false),bank.bytes.data(),bank.bytes.size()),
                    "Resident SBK original source copy differs from the named payload");
            bank.source=ctx.r3.u32;bank.sourceAllocation=allocation.address;
            bank.sourceExtent=allocation.extent;bank.sourceGeneration=allocation.generation;
        }else {
            require(ctx.lr==pc && bank.source && !bank.resource && ctx.r3.u32,
                    "Resident SBK resource constructor lacks its original source");
            const auto& allocation=s.observedAllocation(ctx.r3.u32,32,true);
            require(PPC_LOAD_U32(ctx.r3.u32)==0x8214FA8C &&
                    PPC_LOAD_U32(ctx.r3.u32+0xC)==bank.source &&
                    PPC_LOAD_U32(ctx.r3.u32+0x10)==bank.profile->bytes &&
                    PPC_LOAD_U32(ctx.r3.u32+0x18)==((load.flags&1)?0:PPC_LOAD_U32(load.plugin+0x14)),
                    "Resident SBK resource constructor ownership changed");
            bank.resource=ctx.r3.u32;bank.resourceGeneration=allocation.generation;
            require(s.bankResources.emplace(bank.resource,load.bank).second,"Resident SBK resource generation already exists");
        }
        return;
    }
    if(pc==0x8272E9DC) {
        const auto it=s.bankResources.find(ctx.r31.u32);if(it==s.bankResources.end()) return;
        const auto& bank=*it->second;
        s.bankSourceIdentity(bank);
        require(s.observedAllocation(bank.resource,32,true).generation==bank.resourceGeneration &&
                PPC_LOAD_U32(bank.resource)==0x8214FA8C && PPC_LOAD_U32(bank.resource+0xC)==bank.source &&
                ctx.r3.u32==bank.source && ctx.r4.u32==bank.profile->bytes && ctx.r5.u32==ctx.r1.u32+0x50 &&
                PPC_LOAD_U32(ctx.r5.u32)==0x8272E918 && PPC_LOAD_U32(ctx.r5.u32+8)==bank.resource &&
                !bank.allocation && !s.bankSplits.contains(&ctx),
                "Resident SBK preparation lost its original resource/callback");
        require(!std::memcmp(s.runtime.pointer(bank.source,bank.profile->bytes,false),bank.bytes.data(),bank.bytes.size()),
                "Resident SBK source changed before original splitting");
        s.bankSplits.emplace(&ctx,State::BankSplit{ctx.r1.u32,ctx.r5.u32,it->second});
        return;
    }
    if(pc==0x82812A00) {
        for(auto& [address,bank]:s.banks) if(bank->record==ctx.r4.u32 && !bank->retiring) {
            require(ctx.r3.u32==PPC_LOAD_U32(0x82E06E1C) && PPC_LOAD_U32(ctx.r3.u32)==0x8215CB10 &&
                    s.observedAllocation(bank->record,32,true).generation==bank->recordGeneration &&
                    PPC_LOAD_U32(bank->record+8)==address,"Resident SBK original release record changed");
            // The original backend may defer the physical free. Existing
            // receipts keep their actual allocation until observeHeap retires it.
            bank->retiring=true;
        }
        return;
    }
    const auto it=s.bankSplits.find(&ctx);if(it==s.bankSplits.end()) return;
    auto& split=it->second;auto& bank=*split.bank;
    if(pc==0x8272E9E0) {
        require(ctx.r1.u32==split.sp && ctx.r31.u32==bank.resource && ctx.lr==pc &&
                (ctx.r3.u32?(split.published && ctx.r3.u32==bank.handle):!split.published),
                "Resident SBK preparation result differs from original bank publication");
        s.bankSplits.erase(it);return;
    }
    if(pc==0x82807C4C) {
        s.bankSourceIdentity(bank);
        const uint32_t backend=PPC_LOAD_U32(0x82E06E1C);
        require(ctx.r1.u32==split.sp-0xA0 && ctx.r30.u32==bank.source &&
                ctx.r27.u32==PPC_LOAD_U32(0x82E06E2C) && ctx.r3.u32==backend &&
                PPC_LOAD_U32(backend)==0x8215CB10 && PPC_LOAD_U32(0x8215CB20)==0x82811508 &&
                ctx.ctr.u32==0x82811508 && ctx.r4.u32==bank.source+bank.profile->audioOffset &&
                ctx.r5.u32==bank.profile->audioBytes && ctx.r6.u32==bank.profile->audioBytes &&
                ctx.r8.u32==ctx.r31.u32 && ctx.r9.u32==split.callback && !bank.record,
                "Resident SBK actual audio backend/split arguments changed");
        const auto& record=s.observedAllocation(ctx.r8.u32,32,true);
        bank.record=record.address;bank.recordGeneration=record.generation;
        bank.handle=PPC_LOAD_U32(bank.record);bank.metadata=PPC_LOAD_U32(bank.record+4);
        require(bank.handle && bank.handle==ctx.r28.u32 && bank.metadata &&
                !PPC_LOAD_U32(bank.record+8) && !PPC_LOAD_U32(bank.record+0xC) && !PPC_LOAD_U32(bank.record+0x10),
                "Resident SBK original bank record initialization changed");
        return;
    }
    if(pc==0x82807C6C) {
        require(ctx.r1.u32==split.sp-0xA0 && ctx.r31.u32==bank.record && ctx.r3.u32==bank.handle &&
                split.copied && split.returned && !split.published &&
                s.observedAllocation(bank.record,32,true).generation==bank.recordGeneration &&
                PPC_LOAD_U32(bank.record)==bank.handle && PPC_LOAD_U32(bank.record+4)==bank.metadata &&
                PPC_LOAD_U32(bank.record+8)==bank.allocation,
                "Resident SBK registration lacks a completed original audio copy");
        require(s.banks.emplace(bank.allocation,split.bank).second,"Resident SBK audio allocation is already published");
        split.published=true;
        std::fprintf(stderr,"[RESIDENT SBK] resource=%08X handle=%08X source=%08X audio=%08X extent=%X generation=%llu header=%08X; original split/copy/registration verified\n",
            bank.resource,bank.handle,bank.source,bank.allocation,bank.extent,
            static_cast<unsigned long long>(bank.generation),bank.allocation+(bank.firstHeaderOffset-bank.profile->audioOffset));
        return;
    }
    require(ctx.r1.u32==split.sp-0x120 && ctx.r30.u32==bank.record &&
            ctx.r28.u32==bank.source+bank.profile->audioOffset && ctx.r29.u32==bank.profile->audioBytes &&
            ctx.r31.u32==split.callback,"Resident SBK audio copy frame/record changed");
    if(pc==0x82811568) {
        require(ctx.r3.u32==bank.profile->audioBytes && ctx.r4.u32==0xA0004000 && !split.allocating,
                "Resident SBK original physical allocation request changed");
        split.allocating=true;return;
    }
    if(pc==0x82811580) {
        s.bankSourceIdentity(bank);
        require(split.allocating && !bank.allocation && ctx.r3.u32 && ctx.r3.u32==PPC_LOAD_U32(bank.record+8) &&
                ctx.r4.u32==bank.source+bank.profile->audioOffset && ctx.r5.u32==bank.profile->audioBytes,
                "Resident SBK original audio memcpy arguments changed");
        const auto& allocation=s.observedAllocation(ctx.r3.u32,bank.profile->audioBytes,true);
        require(allocation.caller==0x8268E1CC && !overlap(allocation.address,allocation.extent,bank.sourceAllocation,bank.sourceExtent) &&
                !overlap(allocation.address,allocation.extent,bank.record,32),
                "Resident SBK copy destination lacks the original physical allocator provenance");
        bank.allocation=allocation.address;bank.extent=allocation.extent;bank.generation=allocation.generation;
        return;
    }
    if(pc==0x82811584) {
        s.bankSourceIdentity(bank);
        require(ctx.lr==pc && bank.allocation && !split.copied &&
                s.observedAllocation(bank.allocation,bank.profile->audioBytes,true).generation==bank.generation &&
                PPC_LOAD_U32(bank.record+8)==bank.allocation &&
                PPC_LOAD_U32(bank.record+0xC)==bank.profile->audioBytes && PPC_LOAD_U32(bank.record+0x10)==bank.profile->audioBytes &&
                !std::memcmp(s.runtime.pointer(bank.allocation,bank.profile->audioBytes,false),
                             bank.bytes.data()+bank.profile->audioOffset,bank.profile->audioBytes) &&
                !std::memcmp(s.runtime.pointer(bank.source+bank.profile->audioOffset,bank.profile->audioBytes,false),
                             bank.bytes.data()+bank.profile->audioOffset,bank.profile->audioBytes),
                "Resident SBK original split audio copy differs from its immutable source");
        split.copied=true;return;
    }
    require(pc==0x828115A8 || pc==0x828115D4,"Unknown original resident bank observation");
    require(!split.returned && ctx.r3.u32==(pc==0x828115A8?1u:0u) &&
            (pc==0x828115A8?split.copied:!split.copied),"Resident SBK audio backend returned an inconsistent result");
    if(PPC_LOAD_U32(bank.resource+0x18)) {
        const auto source=s.heap.find(bank.sourceAllocation);
        require(!PPC_LOAD_U32(bank.resource+0xC) &&
                (source==s.heap.end() || source->second.generation!=bank.sourceGeneration),
                "Resident SBK original completion callback did not release the whole source bank");
    }
    split.returned=true;
}
EngineAudioOwners::ResidentBankView EngineAudioOwners::residentBank(uint32_t resource) const {
    auto& s=*state;std::lock_guard lock(s.mutex);
    const auto found=s.bankResources.find(resource);
    require(found!=s.bankResources.end(),"Resident SBK resource is absent or retired");
    const auto& bank=*found->second;
    require(s.observedAllocation(resource,32,true).generation==bank.resourceGeneration,
            "Resident SBK resource generation changed");
    require(s.copyResidentBank(bank.allocation+(bank.firstHeaderOffset-bank.profile->audioOffset))==found->second,
            "Resident bank resource no longer owns its copied audio allocation");
    return {resource,bank.handle,bank.allocation,bank.extent,bank.allocation+(bank.firstHeaderOffset-bank.profile->audioOffset),
            bank.sourceAllocation,bank.generation,bank.sourceGeneration};
}
void EngineAudioOwners::observeHeap(uint32_t pc,PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);
    if(pc==0x8268DDA0) {
        s.region(ctx.r3.u32,0x20);
        require(PPC_LOAD_U32(ctx.r3.u32)==0x820B60B8 && PPC_LOAD_U32(0x820B60B8)==pc &&
                PPC_LOAD_U32(0x820B60BC)==0x8268DF90,"Original bank allocator vtable changed");
        s.heapCalls[&ctx].push_back({ctx.r1.u32,0,ctx.r4.u32,0,uint32_t(ctx.lr),ctx.r3.u32,pc,0x8268DF90});return;
    }
    if(pc==0x8268DED0 || pc==0x8268DEF0 || pc==0x8268DF34 || pc==0x8268DF50 || pc==0x8268DF84) {
        const auto it=s.heapCalls.find(&ctx);
        require(it!=s.heapCalls.end() && !it->second.empty(),"Original allocation observation lacks its entry");
        auto& call=it->second.back();
        require(ctx.r1.u32==call.sp-0x90 && ctx.r26.u32==call.manager,
                "Original allocation scope or request changed");
        if(pc!=0x8268DF84) {
            // A failed small-pool request can fall through to the main heap.
            // Both calls remain original CPU operations and share this scope.
            const bool small=pc==0x8268DED0 || pc==0x8268DEF0;
            const uint32_t heap=small?PPC_LOAD_U32(PPC_LOAD_U32(0x82D57240)):PPC_LOAD_U32(call.manager+0x1C);
            require(ctx.r3.u32==heap && ctx.r4.u32==ctx.r31.u32 && ctx.r4.u32>=call.bytes,
                    "Original lower-heap allocation ABI changed");
            call.called=true;
            return;
        }
        require(call.called,"Original allocation return has no observed callback");
        const uint32_t address=ctx.r3.u32;
        if(address && call.bytes) {
            require(uint64_t(address)+call.bytes<=0x100000000ull && !s.heap.contains(address),
                    "Original allocator returned an overflowing or live allocation");
            const auto allocationGeneration=generation();
            std::unique_lock heapLock(s.heapMutex);
            s.heap.emplace(address,State::HeapAllocation{address,call.bytes,call.flags,call.manager,
                call.allocate,call.release,call.caller,allocationGeneration});
            s.heapIndex.add(address,call.bytes);
        }
        it->second.pop_back();if(it->second.empty()) s.heapCalls.erase(it);return;
    }
    if(pc==0x8268DF90) {
        const auto it=s.heap.find(ctx.r4.u32);if(it==s.heap.end()) return;
        auto& allocation=it->second;
        require(!allocation.closing && ctx.r3.u32==allocation.manager && allocation.release==pc &&
                PPC_LOAD_U32(ctx.r3.u32)==0x820B60B8,"Original allocation free kind/lifetime changed");
        for(const auto& [key,source]:s.sources)
            require((!source->bank || source->bank->generation!=allocation.generation) &&
                    (!source->amx || source->amx->generation!=allocation.generation),
                    "Original audio allocation free preceded resident source retirement");
        s.heapFrees[&ctx].push_back({ctx.r1.u32,allocation.address,ctx.r3.u32,allocation.generation});
        std::unique_lock heapLock(s.heapMutex);
        allocation.closing=true;return;
    }
    const auto it=s.heapFrees.find(&ctx);
    if(it==s.heapFrees.end() || it->second.empty()) return;
    auto& call=it->second.back();
    // An unrelated nested free can return through these same original joins.
    if(ctx.r1.u32!=call.sp-0x70) return;
    const auto allocation=s.heap.find(call.address);
    require(allocation!=s.heap.end() && allocation->second.generation==call.generation &&
            allocation->second.closing && ctx.r31.u32==call.address && ctx.r30.u32==call.flags,
            "Original free observation lost its allocation generation");
    if(pc==0x8268DFD4 || pc==0x8268E014) {
        require(!call.called,"Original allocation free callback observed twice");
        const auto& a=allocation->second;
        const uint32_t heap=pc==0x8268DFD4?PPC_LOAD_U32(PPC_LOAD_U32(0x82D57240)):PPC_LOAD_U32(a.manager+0x1C);
        require(ctx.r3.u32==heap && ctx.r4.u32==a.address,
                "Original allocation free callback ABI changed");
        call.called=true;return;
    }
    require(pc==0x8268E018 && call.called && (ctx.lr==0x8268DFD8 || ctx.lr==0x8268E018),
            "Original allocation retirement lacks its free callback return");
    if(const auto bank=s.banks.find(call.address);bank!=s.banks.end() && bank->second->generation==call.generation)
        s.banks.erase(bank);
    if(const auto resource=s.bankResources.find(call.address);resource!=s.bankResources.end() && resource->second->resourceGeneration==call.generation)
        s.bankResources.erase(resource);
    {
        std::unique_lock heapLock(s.heapMutex);
        s.heapIndex.remove(allocation->first,allocation->second.extent);s.heap.erase(allocation);
    }
    it->second.pop_back();if(it->second.empty()) s.heapFrees.erase(it);
}
uint64_t EngineAudioOwners::allocationGeneration(uint32_t address,uint32_t bytes) const {
    auto& s=*state;std::lock_guard lock(s.mutex);
    const auto it=s.heap.find(address);
    require(bytes && it!=s.heap.end() && !it->second.closing && bytes<=it->second.extent,
            "Original bank allocation is absent, retired or too small");
    return it->second.generation;
}
std::optional<EngineAudioOwners::AllocationSpan> EngineAudioOwners::allocationSpan(uint32_t address) const {
    auto& s=*state;std::shared_lock lock(s.heapMutex);
    // Same rule as a scan of every live allocation (the smallest containing extent), but only the
    // allocations overlapping this address's page are visited.
    const State::HeapAllocation* found=nullptr;
    s.heapIndex.stab(address,[&](const HeapPageIndex::Ref& ref) {
        if(found&&ref.extent>=found->extent) return;
        if(const auto allocation=s.heap.find(ref.start);allocation!=s.heap.end())found=&allocation->second;
    });
    if(!found)return std::nullopt;
    require(!found->closing,"Original borrowed allocation owner is closing");
    return AllocationSpan{found->address,found->extent,found->generation};
}
void EngineAudioOwners::observeStream(uint32_t pc,PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);
    auto need=[](bool value,const char* why) {if(!value) throw Failure(why);};
    auto member=[&](uint32_t a)->State::Member& {
        const auto it=s.members.find(a);need(it!=s.members.end(),"Unscoped original audio member");return it->second;
    };
    switch(pc) {
    case 0x8233C654: {
        need(!s.groupAllocations.contains(&ctx),"Nested original audio group allocation");
        const auto [allocator,release]=s.allocator(base);
        const uint32_t q=PPC_LOAD_U32(0x82E31BCC);
        need(ctx.r3.u32==q && ctx.r20.u32==q && ctx.r4.u32==ctx.r1.u32+0x50 &&
             ctx.r6.u32==ctx.r29.u32 && ctx.r6.u32>=0x4C && ctx.r6.u32<=0x1000000 &&
             ctx.r7.u32==16 && !ctx.r5.u32 && !ctx.r8.u32,"Original audio group allocation request changed");
        s.groupAllocations.emplace(&ctx,State::Allocation{ctx.r1.u32,q,allocator,release,ctx.r6.u32,ctx.r4.u32});break;
    }
    case 0x8233CD44: {
        const auto it=s.groupAllocations.find(&ctx);if(it==s.groupAllocations.end()) break;
        auto& a=it->second;
        need(!a.called && ctx.r1.u32==a.sp-0x60 && ctx.r3.u32==a.allocator &&
             ctx.r4.u32==a.bytes && ctx.r6.u32==1 && ctx.r7.u32==16 && !ctx.r8.u32 &&
             ctx.r31.u32==a.out,"Original audio group allocator callback changed");
        a.called=true;break;
    }
    case 0x8233CD48: {
        const auto it=s.groupAllocations.find(&ctx);if(it==s.groupAllocations.end()) break;
        auto& a=it->second;need(a.called && !a.returned && ctx.r1.u32==a.sp-0x60 &&
             ctx.lr==0x8233CD48,"Unmatched audio group allocation return");
        a.returned=true;a.address=ctx.r3.u32;
        if(a.address) {
            need(!(a.address&15),"Original audio group alignment changed");s.region(a.address,a.bytes,true);
            for(const auto& [v,g]:s.groups) need(!overlap(v,g.extent,a.address,a.bytes),"Live audio groups overlap");
            s.groups.emplace(a.address,State::Group{a.address,a.bytes,a.root,a.allocator,a.release,generation()});
        }break;
    }
    case 0x8233C658: {
        const auto it=s.groupAllocations.find(&ctx);need(it!=s.groupAllocations.end(),"Unscoped audio group publication");
        const auto& a=it->second;
        need(a.returned && ctx.r1.u32==a.sp && PPC_LOAD_U32(a.out)==a.address && ctx.r29.u32==a.bytes,
             "Original audio group allocation result changed");
        s.groupAllocations.erase(it);break;
    }
    case 0x8233C77C: {
        const auto gi=s.groups.find(ctx.r4.u32);need(gi!=s.groups.end(),"Audio member lacks a containing group");
        const uint32_t a=ctx.r3.u32,n=PPC_LOAD_U16(ctx.r26.u32+8);
        need(!gi->second.closing && n>=0x24 && !(a&15) && contains(gi->first,gi->second.extent,a,n) &&
             ctx.r22.u32==a+n && !s.members.contains(a) && !s.memberBuilds.contains(&ctx),
             "Original audio member extent/allocation scope changed");
        State::Member m{};m.address=a;m.extent=n;m.group=gi->first;m.descriptor=ctx.r5.u32;m.generation=generation();
        s.members.emplace(a,std::move(m));s.memberBuilds.emplace(&ctx,State::MemberBuild{ctx.r1.u32,a});break;
    }
    case 0x8233C780: {
        const auto it=s.memberBuilds.find(&ctx);need(it!=s.memberBuilds.end(),"Unscoped audio member constructor return");
        const auto b=it->second;auto& m=member(b.address);
        need(ctx.r1.u32==b.sp && (!ctx.r3.u32 || ctx.r3.u32==b.address),"Audio member constructor result changed");
        if(!ctx.r3.u32) {need(!m.states,"Failed audio member retains state allocation");s.members.erase(b.address);}
        else {need(!m.stream || m.constructed,"Incomplete stream constructor published");m.constructed=true;}
        s.memberBuilds.erase(it);break;
    }
    case 0x823418E8: {
        auto& m=member(ctx.r3.u32);
        need(!m.stream && !m.constructed && m.extent>=0x1C8 && ctx.lr==0x8233DC44 &&
             PPC_LOAD_U32(m.address+8)==m.group && PPC_LOAD_U32(m.address+0x10)==m.descriptor,
             "Original stream constructor lacks its descriptor allocation");
        m.stream=true;break;
    }
    case 0x823419AC: {
        auto& m=member(ctx.r31.u32);const uint32_t n=ctx.r28.u32;
        const auto [allocator,release]=s.allocator(base);
        need(m.stream && !m.states && n>0 && n<=255 && ctx.r3.u32==allocator &&
             ctx.r4.u32==80*n+4 && ctx.r6.u32==1 && ctx.r7.u32==16 && !ctx.r8.u32 &&
             !s.stateAllocations.contains(&ctx),"Original stream state allocation request changed");
        m.voices=n;m.voiceOffset=PPC_LOAD_U16(m.address+0x1BC);
        need(contains(m.address,m.extent,m.address+m.voiceOffset,48*n),"Stream voices exceed the descriptor allocation");
        m.headers.resize(n);m.headerAddresses.resize(n);m.headerCaptured.resize(n);
        m.residentBanks.resize(n);m.amxOwners.resize(n);m.amxCues.resize(n);
        s.stateAllocations.emplace(&ctx,State::Allocation{ctx.r1.u32,PPC_LOAD_U32(m.address+4),allocator,release,80*n+4,m.address});break;
    }
    case 0x823419B0: {
        const auto it=s.stateAllocations.find(&ctx);need(it!=s.stateAllocations.end(),"Unscoped stream state allocator return");
        const auto a=it->second;auto& m=member(a.out);
        need(ctx.r1.u32==a.sp && ctx.r31.u32==m.address && ctx.lr==0x823419B0,"Stream state allocator return changed");
        if(ctx.r3.u32) {
            need(!(ctx.r3.u32&15),"Stream state allocation alignment changed");s.region(ctx.r3.u32,a.bytes,true);
            for(const auto& [v,g]:s.groups) need(!overlap(v,g.extent,ctx.r3.u32,a.bytes),"Stream states overlap group storage");
            for(const auto& [v,other]:s.members) if(other.states)
                need(!overlap(other.states,other.stateBytes,ctx.r3.u32,a.bytes),"Stream states overlap live state storage");
            m.states=ctx.r3.u32;m.stateBytes=a.bytes;m.stateGeneration=generation();
        }
        s.stateAllocations.erase(it);break;
    }
    case 0x82341AB0: {
        auto& m=member(ctx.r31.u32);
        if(ctx.r3.u32) {m.constructed=true;s.streamMember(m.address,0,base);}
        else need(!m.states,"Failed stream constructor retains allocated states");break;
    }
    case 0x823425E8: {
        auto& m=member(ctx.r3.u32);s.streamMember(m.address,ctx.r4.u32,base);
        const auto* header=s.runtime.pointer(ctx.r5.u32,8,false);
        std::copy_n(header,8,m.headers[ctx.r4.u32].begin());m.headerAddresses[ctx.r4.u32]=ctx.r5.u32;
        m.residentBanks[ctx.r4.u32].reset();
        m.residentBanks[ctx.r4.u32]=s.copyResidentBank(ctx.r5.u32);
        m.amxOwners[ctx.r4.u32].reset();m.amxCues[ctx.r4.u32]=nullptr;
        if(!m.residentBanks[ctx.r4.u32]) {
            auto [owner,cue]=s.copyAmxCue(ctx.r5.u32);
            m.amxOwners[ctx.r4.u32]=std::move(owner);m.amxCues[ctx.r4.u32]=cue;
        }
        if(m.amxOwners[ctx.r4.u32])
            std::fprintf(stderr,"[NATIVE AUDIO AMX HEADER] stream=%08X slot=%u payload=%08X cue=%u allocation=%08X generation=%llu; exact original audio hash verified\n",
                ctx.r3.u32,ctx.r4.u32,m.amxOwners[ctx.r4.u32]->base,
                m.amxCues[ctx.r4.u32]->ordinal,m.amxOwners[ctx.r4.u32]->allocation,
                static_cast<unsigned long long>(m.amxOwners[ctx.r4.u32]->generation));
        if(!m.residentBanks[ctx.r4.u32] && !m.amxOwners[ctx.r4.u32]) {
            std::fprintf(stderr,"[NATIVE AUDIO HEADER] stream=%08X slot=%u address=%08X bytes=",ctx.r3.u32,ctx.r4.u32,ctx.r5.u32);
            for(auto byte:m.headers[ctx.r4.u32])std::fprintf(stderr,"%02X",byte);
            std::fprintf(stderr,"\n");
        }
        m.headerCaptured[ctx.r4.u32]=true;break;
    }
    case 0x82341AB8: {
        auto& m=member(ctx.r3.u32);need(m.stream && !m.closing,"Unscoped or repeated stream destruction");m.closing=true;break;
    }
    case 0x82341B04: {
        auto& m=member(ctx.r31.u32);const auto [allocator,release]=s.allocator(base);
        need(m.closing && m.states && ctx.r3.u32==allocator && ctx.r4.u32==m.states &&
             ctx.ctr.u32==release && !s.stateFrees.contains(&ctx),"Stream state free identity changed");
        for(const auto& [b,p]:s.sources)
            need(p->stream!=m.address || p->memberGeneration!=m.generation,"Stream state free retains an original source receipt");
        s.stateFrees.emplace(&ctx,State::StorageFree{ctx.r1.u32,m.address,allocator,release});break;
    }
    case 0x82341B08: {
        const auto it=s.stateFrees.find(&ctx);if(it==s.stateFrees.end()) break;
        const auto f=it->second;need(ctx.r1.u32==f.sp && ctx.lr==0x82341B08,"Unmatched stream state free return");
        auto& m=member(f.address);m.states=0;m.stateBytes=0;s.stateFrees.erase(it);break;
    }
    case 0x8233C838: {
        const auto it=s.groups.find(ctx.r3.u32);need(it!=s.groups.end() && !it->second.closing,"Unscoped group destruction");
        it->second.closing=true;break;
    }
    case 0x8233C924: {
        const auto it=s.groups.find(ctx.r4.u32);need(it!=s.groups.end(),"Group free lacks allocator provenance");
        const auto& g=it->second;
        need(g.closing && ctx.r3.u32==g.allocator && ctx.ctr.u32==g.release &&
             !s.groupFrees.contains(&ctx),"Original group free identity changed");
        for(const auto& [a,m]:s.members) if(m.group==g.address) need(!m.states,"Group free retains stream state allocation");
        for(const auto& [b,p]:s.sources)
            need(p->groupGeneration!=g.generation,"Audio group free retains an original source receipt");
        s.groupFrees.emplace(&ctx,State::StorageFree{ctx.r1.u32,g.address,g.allocator,g.release});break;
    }
    case 0x8233C928: {
        const auto it=s.groupFrees.find(&ctx);need(it!=s.groupFrees.end(),"Group free return lacks saved identity");
        const auto f=it->second;need(ctx.r1.u32==f.sp && ctx.lr==0x8233C928,"Group free return changed");
        for(auto mi=s.members.begin();mi!=s.members.end();) if(mi->second.group==f.address) mi=s.members.erase(mi);else ++mi;
        s.groups.erase(f.address);s.groupFrees.erase(it);break;
    }
    default:throw Failure("Unknown original stream storage observation");
    }
}
void EngineAudioOwners::State::beginResident(PPCContext& ctx,uint8_t* base,Record& r,const Member& m,
                                             uint32_t a,uint32_t i,uint32_t w,uint32_t p,uint32_t j) {
    auto& n=*r.native;const uint32_t v=r.address,h=PPC_LOAD_U32(p+0x28);
    const auto bank=m.residentBanks[i];
    const auto amx=m.amxOwners[i];
    const auto* amxCue=m.amxCues[i];
    const uint32_t headerOffset=bank?m.headerAddresses[i]-bank->allocation+bank->profile->audioOffset:
        amxCue?amxCue->headerOffset:0;
    const auto* profile=bank?Audio::residentXmaProfile(*bank->profile,headerOffset,m.headers[i]):nullptr;
    const auto* loop=bank?Audio::residentLoopProfile(*bank->profile,headerOffset):nullptr;
    std::array<Audio::ResidentXmaProfile,2> catalogProfiles{};
    std::array<std::string,2> catalogBlockHashes{};
    Audio::ResidentLoopProfile catalogLoop{};
    if(bank && !profile && bank->catalogBank) {
        const auto& catalog=residentAudioCatalog();
        const auto* cue=catalog.findCue(*bank->catalogBank,headerOffset,m.headers[i]);
        const uint32_t expectedBlocks=cue && cue->loop && cue->loopStartSample?2:1;
        if(cue && cue->blockCount==expectedBlocks && cue->headerOffset<=UINT32_MAX &&
           (!cue->loop || cue->loopStartSample<cue->frames)) {
            uint64_t nextOffset=cue->headerOffset+(cue->loop?12:8);
            bool qualified=true;
            uint64_t sumFrames=0;
            for(uint32_t ordinal=0;ordinal<cue->blockCount;++ordinal) {
                const auto* block=catalog.findBlock(*cue,ordinal);
                const uint64_t rawBound=block?(uint64_t(block->frames)+384+4096+511)&~uint64_t(511):0;
                if(!block || block->offset!=nextOffset || block->bytes>1024*1024 ||
                   rawBound>4194304 || block->frames==0) {qualified=false;break;}
                auto& hash=catalogBlockHashes[ordinal];
                hash.reserve(64);
                for(uint8_t byte:block->hash) {
                    hash.push_back("0123456789abcdef"[byte>>4]);
                    hash.push_back("0123456789abcdef"[byte&15]);
                }
                // The exact shipped bank/block hashes are checked again when
                // parsing the original producer's owned request bytes.
                catalogProfiles[ordinal]={cue->header,uint32_t(cue->headerOffset),block->bytes,
                    block->frames,uint32_t(rawBound),cue->channels,block->selector,
                    block->codecRate,cue->playbackRate,block->restoredFF,
                    hash.c_str(),nullptr};
                nextOffset+=block->bytes;
                sumFrames+=block->frames;
            }
            if(qualified && nextOffset==cue->endOffset && sumFrames==cue->frames &&
               (!cue->loop || (cue->blockCount==1 || catalogProfiles[0].frames==cue->loopStartSample))) {
                profile=&catalogProfiles[0];
                if(cue->loop) {
                    catalogLoop={uint32_t(cue->headerOffset),cue->frames,cue->loopStartSample,
                        std::span<const Audio::ResidentXmaProfile>(catalogProfiles).first(cue->blockCount)};
                    loop=&catalogLoop;
                }
            }
        }
    }
    if(amx && amxCue && !profile) {
        const auto& catalog=amxAudioCatalog();
        const auto* block=catalog.findBlock(*amxCue,0);
        const uint64_t rawBound=block?(uint64_t(block->frames)+384+4096+511)&~uint64_t(511):0;
        if(amx->payload && catalog.findCue(*amx->payload,headerOffset,m.headers[i])==amxCue &&
           amxCue->blockCount==1 && block && block->offset==headerOffset+(amxCue->loop?12:8) &&
           block->bytes<=1024*1024 && block->frames==amxCue->frames && rawBound<=4194304 &&
           uint64_t(block->offset)+block->bytes==amxCue->endOffset) {
            auto& hash=catalogBlockHashes[0];hash.reserve(64);
            for(uint8_t byte:block->hash) {
                hash.push_back("0123456789abcdef"[byte>>4]);
                hash.push_back("0123456789abcdef"[byte&15]);
            }
            catalogProfiles[0]={amxCue->header,headerOffset,block->bytes,block->frames,
                uint32_t(rawBound),amxCue->channels,block->selector,block->codecRate,
                amxCue->playbackRate,block->restoredFF,hash.c_str(),nullptr};
            profile=&catalogProfiles[0];
            if(amxCue->loop) {
                catalogLoop={headerOffset,amxCue->frames,amxCue->loopStartSample,
                    std::span<const Audio::ResidentXmaProfile>(catalogProfiles).first(1)};
                loop=&catalogLoop;
            }
        }
    }
    const bool first=n.nextSequence==1;
    const uint32_t headerBytes=loop?12:8,loopStart=loop?loop->loopStart:0xFFFFFFFF;
    uint32_t blockOffset=headerBytes;
    bool originalCaller=false;
    if(first) originalCaller=ctx.lr==0x82342C8C && ctx.r31.u32==a && ctx.r30.u32==p && ctx.r29.u32==i && ctx.r28.u32==w && !n.source;
    else if(loop && n.source) {
        if(loop->loopStart) {
            require(loop->blocks.size()==2,"Resident intro/loop certificate shape changed");
            blockOffset+=loop->blocks[0].blockBytes;profile=&loop->blocks[1];
        }
        if(n.nextSequence==2 && loop->loopStart)
            originalCaller=ctx.lr==0x823428C8 && ctx.r30.u32==a && ctx.r31.u32==p && ctx.r29.u32==i &&
                PPC_LOAD_U32(p+0x34)==m.headerAddresses[i]+blockOffset;
        else originalCaller=ctx.lr==0x8234298C && ctx.r30.u32==a && ctx.r31.u32==p && ctx.r28.u32==i && ctx.r29.u32==w &&
                PPC_LOAD_U32(p+0x34)==m.headerAddresses[i]+blockOffset+profile->blockBytes;
        originalCaller=originalCaller && PPC_LOAD_U32(p+0x38)==m.headerAddresses[i]+blockOffset;
    }
    const uint32_t producedBefore=first?0:loopStart;
    if(!profile || !originalCaller) std::fprintf(stderr,
        "[RESIDENT PRODUCER] unsupported caller=%08X header=%08X offset=%u block=%08X next=%llu bank=%u amx=%u profile=%u loop=%u\n",
        uint32_t(ctx.lr),m.headerAddresses[i],headerOffset,ctx.r4.u32,
        static_cast<unsigned long long>(n.nextSequence),unsigned(bool(bank)),unsigned(bool(amx)),unsigned(bool(profile)),unsigned(bool(loop)));
    require(originalCaller && j<20 && m.headerCaptured[i] && (bank || amx) && n.enabled &&
            profile && r.channels==profile->channels && ctx.r6.u32==1 && !ctx.r7.u32 &&
            PPC_LOAD_U32(a+4)==r.owner && PPC_LOAD_U8(p+0x30)==3 && !PPC_LOAD_U8(p+0x31) &&
            PPC_LOAD_U8(w+0x2B)==r.channels && PPC_LOAD_U32(w+0x10)==std::bit_cast<uint32_t>(float(profile->playbackRate)) &&
            PPC_LOAD_U32(w+0x14)==(loop?loop->totalFrames:profile->frames) && PPC_LOAD_U32(w+0x18)==loopStart &&
            !PPC_LOAD_U32(w+0x20) && !PPC_LOAD_U32(w+0x24) && !PPC_LOAD_U32(p+0x3C) &&
            !PPC_LOAD_U32(p+0x40) && !PPC_LOAD_U32(p+0x44) && PPC_LOAD_U8(p+0x4C)==1 &&
            PPC_LOAD_U32(p+0x14)==producedBefore && !PPC_LOAD_U32(p+0x18) && !PPC_LOAD_U32(p+0x24) && !PPC_LOAD_U32(p+0x2C),
            "Unqualified resident EXm0 startup, format, seek or accounting");
    if(bank) residentIdentity(*bank);else amxIdentity(*amx);
    const uint32_t block=m.headerAddresses[i]+blockOffset,request=a+0x54+16*j,slot=PPC_LOAD_U8(v+0x2F);
    require(slot<20 && contains(a,m.extent,request,16) && ctx.r4.u32==block && PPC_LOAD_U32(p+8)==m.headerAddresses[i]+headerBytes &&
            (!loop || PPC_LOAD_U32(m.headerAddresses[i]+8)==loopStart) &&
            (!bank || !bank->retiring) &&
            block==(bank?bank->allocation+headerOffset-bank->profile->audioOffset+blockOffset:
                          amx->base+headerOffset+blockOffset) &&
            !std::memcmp(runtime.pointer(m.headerAddresses[i],8,false),m.headers[i].data(),8) &&
            !PPC_LOAD_U32(request) &&
            !PPC_LOAD_U8(request+0xD) && !sources.contains(residentKey(request)) && !producers.contains(&ctx),
            "Resident EXm0 request/block association changed");
    const uint32_t queue=v+PPC_LOAD_U32(v+0x24)+20*slot;
    require(!n.queue[slot] && !PPC_LOAD_U32(queue+0xC),"Resident EXm0 codec queue slot is unavailable");
    const std::span<const uint8_t> ownedBlock=bank?
        std::span<const uint8_t>(bank->bytes).subspan(headerOffset+blockOffset,profile->blockBytes):
        std::span<const uint8_t>(runtime.pointer(block,profile->blockBytes,false),profile->blockBytes);
    auto parsed=Audio::parseResidentXmaBlock(*profile,ownedBlock,r.channels,m.headers[i]);
    region(p,80,true);region(request,16,true);region(queue,20,true);runtime.pointer(ctx.r1.u32-0x200,0x200,true);
    auto source=std::make_shared<Source>();source->key=residentKey(request);
    source->bank=bank;source->amx=amx;source->amxCue=amxCue;
    source->charged=profile->blockBytes;source->block=block;source->stream=a;source->streamState=p;
    // Resident initialization leaves P+28 untouched. Preserve its opaque value
    // for the original request store; only streamed storage owns a reader here.
    // P+4C above is the byte written at 82342800 and loaded at 82342C7C.
    source->streamHandle=h;source->voice=v;source->voiceIndex=i;source->request=request;source->slot=slot;
    source->samples=parsed.declaredFrames;source->sp=ctx.r1.u32;source->requestSlot=j;source->generation=r.generation;
    source->residentOffset=blockOffset;source->producedBefore=producedBefore;
    source->sequence=n.nextSequence;source->groupGeneration=groups.at(m.group).generation;
    source->memberGeneration=m.generation;source->stateGeneration=m.stateGeneration;
    sourceIdentity(*source,base);
    const std::array<Audio::XmaFormat,1> formats={{{profile->channels,profile->codecRate,Audio::XmaVariant::Xma2}}};
    Audio::XmaSource::Limits limits;limits.maxBufferedFramesPerLayer=profile->rawFrames;
    limits.maxDeclaredFrames=profile->frames;limits.maxCompressedBytes=profile->blockBytes-12+profile->restoredFF;
    limits.maxQuotaFrames=256;
    n.source=std::make_shared<Audio::XmaSource>(*factory,r.generation,formats,limits);n.selector=profile->codecSelector;
    source->decoder=n.source;
    Audio::XmaSource::Segment segment;segment.sequence=source->sequence;segment.slot=j;
    segment.declaredFrames=source->samples;segment.continuity=Audio::XmaSource::Continuity::FreshContext;
    segment.layers=std::move(parsed.layers);segment.layers[0].skipFrames=384;
    sources.emplace(source->key,source);
    try {
        producers.emplace(&ctx,source);n.queue[slot]=source;
        const auto accepted=n.source->prepare(segment);
        require(accepted.status==Audio::XmaSource::PrepareStatus::Accepted,"Resident EXm0 source admission has no capacity");
        source->receipt=accepted.receipt;source->prepared=true;++n.nextSequence;
    }catch(...) {cancel(r);throw;}
    if(n.nextSequence<=3 && sampleSuccessLog(residentSourceLogs)) std::fprintf(stderr,"[NATIVE AUDIO SOURCE] qualified resident V=%08X generation=%llu R=%08X frames=%u channels=%u selector=%u; original initial skip=384\n",
        v,static_cast<unsigned long long>(r.generation),request,source->samples,profile->channels,profile->codecSelector);
}
void EngineAudioOwners::producerBegin(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.auditProducer(ctx);s.frame(ctx,base);
    const uint32_t a=ctx.r3.u32,i=ctx.r5.u32;
    const auto& m=s.streamMember(a,i,base);
    const uint32_t w=a+m.voiceOffset+48*i,p=m.states+4+80*i,v=PPC_LOAD_U32(w+8);
    if(!s.records.contains(v)) return;
    auto& r=s.live(v,base);const uint32_t j=PPC_LOAD_U8(p+0x32),h=PPC_LOAD_U32(p+0x28);
    if(!PPC_LOAD_U8(p+0x31)) {s.beginResident(ctx,base,r,m,a,i,w,p,j);return;}
    if(j>=20 || ctx.lr!=0x823424D0 || ctx.r31.u32!=a || ctx.r30.u32!=p || ctx.r28.u32!=i ||
       !m.headerCaptured[i]) {
        const auto& g=s.groups.at(m.group);
        std::fprintf(stderr,"[NATIVE AUDIO PRODUCER] unsupported caller=%08X A=%08X extent=%X G=%08X/%X P=%08X W=%08X V=%08X index=%u slot=%u block=%08X r6=%08X r7=%08X header=%08X captured=%u\n",
            uint32_t(ctx.lr),a,m.extent,g.address,g.extent,p,w,v,i,j,ctx.r4.u32,ctx.r6.u32,ctx.r7.u32,
            m.headerAddresses[i],unsigned(m.headerCaptured[i]));
        std::fprintf(stderr,"[NATIVE AUDIO PRODUCER] P words:");
        for(uint32_t at=0;at<80;at+=4) std::fprintf(stderr," %08X",PPC_LOAD_U32(p+at));
        std::fprintf(stderr,"\n[NATIVE AUDIO PRODUCER] W words:");
        for(uint32_t at=0;at<48;at+=4) std::fprintf(stderr," %08X",PPC_LOAD_U32(w+at));
        std::fprintf(stderr,"\n[NATIVE AUDIO PRODUCER] header:");
        for(auto value:m.headers[i]) std::fprintf(stderr,"%02X",value);
        const auto* prefix=s.runtime.pointer(ctx.r4.u32,32,false);
        std::fprintf(stderr," block prefix:");
        for(unsigned at=0;at<32;++at) std::fprintf(stderr,"%02X",prefix[at]);
        std::fprintf(stderr,"\n");
        throw Failure("Unqualified original streamed producer caller/header");
    }
    const uint32_t request=a+0x54+16*j,b=PPC_LOAD_U32(request);
    if(!contains(a,m.extent,request,16) || ctx.r29.u32!=b || PPC_LOAD_U8(request+0xD))
        throw Failure("Original streamed source request association changed");
    const auto readers=audioReaders(s.runtime);
    if(!readers) throw Failure("Streamed producer has no reader ownership registry");
    // P+2C retains the first request token (82342194/8234221C). The
    // original preload/body paths issue more requests without replacing it;
    // 82342474 claims B by H and 823424C4 publishes that actual node here.
    // Qualify this block's own token, leaving the original cancellation ID.
    s.runtime.pointer(b,4,false);
    const uint32_t token=PPC_LOAD_U32(b);
    const uint64_t readSnapshot=fileReadSequence(); // Reads completing after the copy cannot be its source.
    auto owned=readers->copy(h,b,a,token);
    if(owned.address!=ctx.r4.u32) throw Failure("Original producer block differs from owned reader claim");
    auto& n=*r.native;
    const bool first=n.nextSequence==1;
    const uint32_t catalogBlock=n.catalogNextBlock;
    const auto& catalog=s.audioCatalog();
    const auto* stream=catalog.findHeader(m.headers[i]);
    // 82342828 starts the body after the one-sample introduction. At the
    // complete body boundary 823428F8 requests the original reader again at
    // P+0C and resets P+14 to W+18. Both paths reach this owned caller with
    // r6=1; ordinary blocks inside the body arrive with r6=0.
    const bool fresh=first || (stream && stream->loop && catalogBlock==2);
    if(!stream || first!=(catalogBlock==1) || r.channels!=stream->channels ||
       ctx.r6.u32!=unsigned(fresh) || ctx.r7.u32 ||
       PPC_LOAD_U32(a+4)!=r.owner || PPC_LOAD_U8(p+0x30)!=3 || PPC_LOAD_U8(p+0x31)!=1 ||
       PPC_LOAD_U8(w+0x2B)!=r.channels || PPC_LOAD_U32(w+0x10)!=0x473B8000 ||
       PPC_LOAD_U32(w+0x14)!=stream->frames ||
       PPC_LOAD_U32(w+0x18)!=(stream->loop?stream->loopStartSample:0xFFFFFFFFu) ||
       (stream->loop && (PPC_LOAD_U32(p+0x0C)!=stream->loopOffsetRelative ||
                         PPC_LOAD_U32(p+0x14)!=n.catalogProduced || PPC_LOAD_U8(p+0x4C)!=1)) ||
       PPC_LOAD_U32(w+0x20) || PPC_LOAD_U32(w+0x24) || PPC_LOAD_U32(p+0x3C) ||
       PPC_LOAD_U32(p+0x40) || PPC_LOAD_U32(p+0x44) ||
       s.sources.contains(b) || s.producers.contains(&ctx) || !n.enabled) {
        std::fprintf(stderr,"[STREAM STARTUP REJECTED] LR=%08X A=%08X P=%08X W=%08X V=%08X B=%08X index=%u requestSlot=%u r6=%08X r7=%08X next=%llu channels=%u enabled=%u source=%u producer=%u owned=%u hash=%s\n",
            uint32_t(ctx.lr),a,p,w,v,b,i,j,ctx.r6.u32,ctx.r7.u32,
            static_cast<unsigned long long>(n.nextSequence),r.channels,unsigned(n.enabled),
            unsigned(s.sources.contains(b)),unsigned(s.producers.contains(&ctx)),owned.length,audioHash(owned.bytes).c_str());
        std::fprintf(stderr,"[STREAM STARTUP REJECTED] P words:");
        for(uint32_t at=0;at<80;at+=4) std::fprintf(stderr," %08X",PPC_LOAD_U32(p+at));
        std::fprintf(stderr,"\n[STREAM STARTUP REJECTED] W words:");
        for(uint32_t at=0;at<48;at+=4) std::fprintf(stderr," %08X",PPC_LOAD_U32(w+at));
        std::fprintf(stderr,"\n[STREAM STARTUP REJECTED] header:");
        for(auto value:m.headers[i]) std::fprintf(stderr,"%02X",value);
        std::fprintf(stderr," block prefix:");
        for(size_t at=0;at<std::min<size_t>(32,owned.bytes.size());++at) std::fprintf(stderr,"%02X",owned.bytes[at]);
        std::fprintf(stderr,"\n");
        throw Failure("Unqualified EXm0 streamed format/start/seek or source ownership");
    }
    auto matched=catalog.match(m.headers[i],n.catalogCandidates,catalogBlock,owned.bytes);
    s.auditSourceFile(ctx,owned,catalog,matched,catalogBlock,readSnapshot);
    auto parsed=Audio::parseCatalogEaXmaBlock(owned.bytes,r.channels,matched.block);
    const uint32_t before=PPC_LOAD_U32(p+0x14),slot=PPC_LOAD_U8(v+0x2F);
    const uint32_t queue=v+PPC_LOAD_U32(v+0x24)+20*slot;
    if(uint64_t(before)+parsed.declaredFrames>PPC_LOAD_U32(w+0x14) ||
       PPC_LOAD_U32(p+0x18)<owned.length || n.queue[slot] || PPC_LOAD_U32(queue+0xC))
        throw Failure("Original EXm0 producer accounting or codec slot is unavailable");
    const bool atEnd=uint64_t(before)+parsed.declaredFrames==PPC_LOAD_U32(w+0x14);
    const bool catalogEnd=catalog.complete(matched.candidates,catalogBlock);
    if((atEnd && !catalogEnd) || (stream->loop && catalogEnd && !atEnd))
        throw Failure("Audio catalog source ended before an exact block sequence completed");
    s.region(p,80,true);s.region(request,16,true);s.region(queue,20,true);
    // The producer and enqueuer reserve 0x90+0x70 bytes before the native
    // input callback reserves its own checked original-call frame.
    s.runtime.pointer(ctx.r1.u32-0x200,0x200,true);
    for(const auto& [allocation,source]:s.sources)
        if(source->voice==v && source->generation==r.generation && source->requestSlot==j)
            throw Failure("Original request slot still owns an EXm0 receipt");
    auto source=std::make_shared<State::Source>();
    source->key=b;source->allocation=b;source->charged=owned.length;source->block=owned.address;
    source->stream=a;source->streamState=p;source->streamHandle=h;source->voice=v;source->voiceIndex=i;
    source->request=request;source->slot=slot;source->samples=parsed.declaredFrames;source->flag=fresh?0:1;
    source->sp=ctx.r1.u32;source->requestSlot=j;source->producedBefore=before;source->generation=r.generation;
    source->sequence=n.nextSequence;source->groupGeneration=s.groups.at(m.group).generation;
    source->memberGeneration=m.generation;source->stateGeneration=m.stateGeneration;source->claim=std::move(owned);
    s.sourceIdentity(*source,base);
    if(fresh) {
        std::vector<Audio::XmaFormat> formats;
        for(uint32_t channel=0;channel<r.channels;channel+=2)
            formats.push_back({std::min(2u,r.channels-channel),48000,Audio::XmaVariant::Xma2});
        Audio::XmaSource::Limits limits;limits.maxBufferedFramesPerLayer=131072;
        n.source=std::make_shared<Audio::XmaSource>(*s.factory,r.generation,formats,limits);
    }
    if(!n.source) throw Failure("Stream continuation has no original fresh XMA context");
    source->decoder=n.source;
    Audio::XmaSource::Segment segment;
    segment.sequence=source->sequence;segment.slot=j;segment.declaredFrames=source->samples;
    segment.continuity=fresh?Audio::XmaSource::Continuity::FreshContext:Audio::XmaSource::Continuity::ContinueContext;
    segment.layers=std::move(parsed.layers);
    for(auto& layer:segment.layers) layer.skipFrames=fresh?384:0;
    // Reserve host publication before codec mutation. After mutation an error
    // is terminal: the original claimed B and charged P must not be retried.
    s.sources.emplace(b,source);
    try {
        s.producers.emplace(&ctx,source);n.queue[slot]=source;
        const auto accepted=n.source->prepare(segment);
        if(accepted.status!=Audio::XmaSource::PrepareStatus::Accepted)
            throw Failure("Bounded native EXm0 source admission has no capacity");
        source->receipt=accepted.receipt;source->prepared=true;
        n.catalogCandidates=std::move(matched.candidates);
        n.catalogNextBlock=stream->loop && catalogEnd?2:catalogBlock+1;
        n.catalogProduced=stream->loop && catalogEnd?stream->loopStartSample:before+parsed.declaredFrames;
        ++n.nextSequence;
    }catch(...) {s.cancel(r);throw;}
    if(first) std::fprintf(stderr,"[NATIVE AUDIO SOURCE] qualified stream V=%08X generation=%llu B=%08X frames=%u channels=%u; NativeRawF32, explicit initial skip=384\n",
        v,static_cast<unsigned long long>(r.generation),b,source->samples,r.channels);
}
EngineAudioOwners::EngineAudioOwners(Runtime& rt):state(std::make_unique<State>(rt)) {}
EngineAudioOwners::~EngineAudioOwners() {
    if(!state->records.empty() || !state->creations.empty() || !state->destructions.empty())
        std::fprintf(stderr,"[NATIVE AUDIO] incomplete original EXm0 cleanup: records=%zu creations=%zu destructions=%zu; terminal host release only\n",
            state->records.size(),state->creations.size(),state->destructions.size());
}
void EngineAudioOwners::start(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);s.descriptorIdentity(base);s.noHardware(base);
    if(!s.factory) {
        if(!s.records.empty() || !s.creations.empty() || !s.destructions.empty() || s.reserved)
            throw Failure("Native EXm0 restart retains live ownership");
        const uint32_t q=PPC_LOAD_U32(0x82E31BCC);s.region(q,0x20,true);
        const bool installCallbacks=PPC_LOAD_U32(q+0x18)==0;
        auto factory=std::make_unique<Audio::NativeXmaFactory>();
        // This shared CPU callback installation was inside the replaced pool
        // initializer. Preserve its exact conditional branch independently of
        // native readiness and the absent private hardware allocations.
        if(installCallbacks) {PPC_STORE_U32(q+0x18,0x82339788);PPC_STORE_U32(q+0x1C,0x82339798);}
        s.factory=std::move(factory);
        std::fprintf(stderr,"[NATIVE AUDIO] EXm0 native factory acquired; instances unconfigured, input/decode guarded; descriptor=%08X caller=%08X\n",
            descriptor,uint32_t(ctx.lr));
    }
    ctx.r3.s64=int32_t(descriptor);
}
void EngineAudioOwners::stop(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);s.noHardware(base);
    if(!s.records.empty() || !s.creations.empty() || !s.destructions.empty() || s.reserved ||
       !s.sources.empty() || !s.producers.empty() || !s.frees.empty())
        throw Failure("Original EXm0 teardown retains native instances or callbacks");
    s.factory.reset();
}
void EngineAudioOwners::enable(PPCContext& ctx,uint8_t* base,bool enabled) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);s.available(base);
    if(!enabled && ctx.r4.u32) throw Failure("Native EXm0 exclusion has no original hardware-record association");
    for(auto& [address,record]:s.records) if(record.native) record.native->enabled=enabled;
}
void EngineAudioOwners::createBegin(PPCContext& ctx,uint8_t* base) {
    if(ctx.r4.u32!=descriptor) return;
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);s.available(base);
    const uint32_t channels=ctx.r5.u32;
    if(channels<1 || channels>6) throw Failure("Unsupported EXm0 channel count before original allocation/truncation");
    if(ctx.r7.u32!=PPC_LOAD_U32(0x82E31BCC)) throw Failure("EXm0 owner does not match the recovered original audio-root profile");
    if(s.creations.contains(&ctx)) throw Failure("Reentrant EXm0 construction in one guest context");
    const auto [allocator,release]=s.allocator(base);
    s.creations.emplace(&ctx,State::Creation{ctx.r1.u32,channels,ctx.r7.u32,allocator,release,0,generation()});
}
void EngineAudioOwners::createAllocated(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);
    const auto it=s.creations.find(&ctx);
    if(it==s.creations.end()) {if(ctx.r30.u32==descriptor) throw Failure("Unscoped EXm0 allocation result");return;}
    auto& t=it->second;const uint32_t v=ctx.r31.u32,codecSize=0x58+0x18*((t.channels+1)/2);
    s.available(base);
    if(s.allocator(base)!=std::pair{t.allocator,t.freeFunction}) throw Failure("Original EXm0 allocator changed during construction");
    const uint32_t bytes=((codecSize+7)&~7u)+0x190;
    if(t.address || ctx.r1.u32!=t.sp-0xB0 || ctx.lr!=0x82340534 || ctx.r30.u32!=descriptor ||
       ctx.r29.u32!=bytes || ctx.r25.u32!=codecSize || ctx.r27.u32!=t.channels || ctx.r26.u32!=t.owner ||
       !v || (v&15)) throw Failure("Original EXm0 allocation callback ABI changed");
    s.region(v,bytes,true);
    if(overlap(v,bytes,t.sp-0x200,0x200)) throw Failure("EXm0 allocation overlaps callback stack");
    {
        // Worker creation/exit can grow or erase this vector. The audio owner
        // mutex protects our transactions, not the runtime mapping registry.
        std::lock_guard vmLock(s.runtime.vmMutex);
        for(const auto& region:s.runtime.regions)
            if((region.use==MemoryUse::Image || region.use==MemoryUse::Stack || region.use==MemoryUse::Kernel) &&
               uint64_t(v)<uint64_t(region.address)+region.size && uint64_t(region.address)<uint64_t(v)+bytes)
                throw Failure("EXm0 allocation overlaps original image/ABI storage");
    }
    for(const auto& [address,record]:s.records)
        if(overlap(v,bytes,address,record.extent)) throw Failure("EXm0 allocations overlap or reuse a live generation");
    const std::array<uint32_t,4> unchanged={PPC_LOAD_U32(v+0x28),PPC_LOAD_U32(v+0x38),PPC_LOAD_U32(v+0x40),PPC_LOAD_U32(v+0x50)};
    s.records.emplace(v,State::Record{v,bytes,t.channels,t.owner,t.allocator,t.freeFunction,t.generation,State::Phase::Allocated,unchanged,{}});
    t.address=v;
}
void EngineAudioOwners::construct(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);s.available(base);
    const auto it=s.creations.find(&ctx);
    if(it==s.creations.end() || !it->second.address) throw Failure("Native EXm0 constructor lacks an original allocation transaction");
    auto& t=it->second;auto& r=s.find(t.address);const uint32_t v=r.address,layers=(r.channels+1)/2,layerBase=(v+0x5F)&~7u;
    if(r.phase!=State::Phase::Allocated || ctx.r3.u32!=v || ctx.r1.u32!=t.sp-0xB0 || ctx.lr!=0x82340574 ||
       PPC_LOAD_U32(v+4)!=r.owner || PPC_LOAD_U32(v+0xC)!=0x8233EC58 || PPC_LOAD_U32(v+0x10) || PPC_LOAD_U8(v+0x2E)!=r.channels)
        throw Failure("Original EXm0 constructor callback ABI/fields changed");
    const uint32_t available=256-s.reserved;
    std::shared_ptr<State::Instance> native;
    if(available>=layers) native=std::make_shared<State::Instance>(r.generation,r.channels);
    PPC_STORE_U32(v,vtable);PPC_STORE_U32(v+0x34,layerBase);PPC_STORE_U32(v+0x44,layers);
    PPC_STORE_U32(v+0x3C,0);PPC_STORE_U32(v+0x48,0);PPC_STORE_U32(v+0x4C,0);
    PPC_STORE_U8(v+0x54,0);PPC_STORE_U8(v+0x55,1);
    std::memset(s.runtime.pointer(layerBase,layers*0x18,true),0,layers*0x18);
    // Failed borrowing preserves channel bytes of any acquired prefix, while
    // backend identities remain private. Generic destruction still runs.
    for(uint32_t i=0;i<std::min(available,layers);++i)
        PPC_STORE_U8(layerBase+0x18*i+0xC,uint8_t(std::min(2u,r.channels-2*i)));
    if(!native) {r.phase=State::Phase::Failed;t.failed=true;ctx.r3.u64=0;return;}
    r.native=std::move(native);s.reserved+=layers;r.phase=State::Phase::Constructed;ctx.r3.u64=1;
}
void EngineAudioOwners::createEnd(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);
    const auto it=s.creations.find(&ctx);if(it==s.creations.end()) return;
    const auto t=it->second;
    if(ctx.r1.u32!=t.sp-0xB0 || ctx.r30.u32!=descriptor) throw Failure("Original EXm0 construction return ABI changed");
    if(!ctx.r3.u32) {
        if(t.address && (!t.failed || s.records.contains(t.address))) throw Failure("Failed EXm0 creation did not execute original cleanup");
    }else {
        auto& r=s.find(t.address);
        if(ctx.r3.u32!=t.address || r.phase!=State::Phase::Constructed || !r.native)
            throw Failure("Incomplete EXm0 construction was published");
        s.owned(r,base,true);r.phase=State::Phase::Live;
        if(sampleSuccessLog(s.constructedLogs)) std::fprintf(stderr,"[NATIVE AUDIO] original EXm0 instance=%08X generation=%llu channels=%u layers=%zu allocation=%X; unconfigured/decode guarded\n",
            r.address,static_cast<unsigned long long>(r.generation),r.channels,r.native->layers.size(),r.extent);
    }
    s.creations.erase(it);
}
void EngineAudioOwners::destroyBegin(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);
    const uint32_t v=ctx.r3.u32;
    if(!s.records.contains(v)) {if(s.looksNative(v,base)) throw Failure("Unowned EXm0 generic destruction");return;}
    s.available(base);auto& r=s.find(v);
    if(s.destructions.contains(&ctx)) throw Failure("Reentrant EXm0 destruction in one guest context");
    const bool failed=r.phase==State::Phase::Failed;
    if(failed) {
        const auto it=s.creations.find(&ctx);
        if(it==s.creations.end() || it->second.address!=v || !it->second.failed || ctx.lr!=0x82340630 || ctx.r1.u32!=it->second.sp-0xB0)
            throw Failure("Unscoped failed EXm0 creation cleanup");
    }else {
        if(r.phase!=State::Phase::Live || !r.native) throw Failure("Incomplete or already retiring EXm0 allocation");
        s.owned(r,base,true);
    }
    if(s.allocator(base)!=std::pair{r.allocator,r.freeFunction}) throw Failure("Original EXm0 free allocator changed");
    s.destructions.emplace(&ctx,State::Destruction{ctx.r1.u32,v,failed});
    // Serialize close against stage/publication/commit. Original B/R ownership
    // survives V destruction until the original reader really releases it.
    s.cancel(r);r.phase=State::Phase::Destroying;
}
void EngineAudioOwners::release(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);s.available(base);
    const auto it=s.destructions.find(&ctx);
    if(it==s.destructions.end()) throw Failure("EXm0 release bypassed original generic destructor");
    const auto& t=it->second;auto& r=s.find(t.address);
    if(ctx.r3.u32!=r.address || ctx.r30.u32!=r.address || ctx.r1.u32!=t.sp-0x70 || ctx.lr!=0x82340314 ||
       r.phase!=State::Phase::Destroying || PPC_LOAD_U32(r.address+0x10))
        throw Failure("Original EXm0 release callback ABI changed");
    if(r.native) {s.reserved-=uint32_t(r.native->layers.size());r.native.reset();}
    else if(!t.failedCreation) throw Failure("Native EXm0 ownership was released twice");
    r.phase=State::Phase::Released;
}
void EngineAudioOwners::destroyEnd(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);
    const auto it=s.destructions.find(&ctx);if(it==s.destructions.end()) return;
    const auto t=it->second;auto& r=s.find(t.address);
    if(ctx.r1.u32!=t.sp-0x70 || ctx.r30.u32!=t.address || ctx.lr!=0x82340360 ||
       r.phase!=State::Phase::Released || r.native)
        throw Failure("EXm0 generic free did not complete its original callback path");
    // The original allocator has freed V. Never access guest storage here.
    s.records.erase(t.address);s.destructions.erase(it);
}
void EngineAudioOwners::inputPreflight(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);
    if(!s.records.contains(ctx.r3.u32)) {
        require(!s.looksNative(ctx.r3.u32,base),"Unowned native EXm0 enqueue");return;
    }
    auto& r=s.live(ctx.r3.u32,base);const auto it=s.producers.find(&ctx);
    require(it!=s.producers.end(),"EXm0 enqueue lacks an original prepared producer");
    auto& p=*it->second;s.sourceIdentity(p,base);
    const uint32_t q=r.address+PPC_LOAD_U32(r.address+0x24)+20*p.slot;
    require(p.prepared && !p.preflight && !p.notified && !p.cancelled && p.voice==r.address && p.generation==r.generation &&
            ctx.lr==0x823425C4 && ctx.r1.u32==p.sp-0x90 && ctx.r4.u32==p.block+8 &&
            ctx.r5.u32==p.samples && ctx.r6.u32==p.flag && !ctx.r7.u32 && !ctx.r8.u32 && !ctx.r9.u32 &&
            ctx.r31.u32==p.block && ctx.r30.u32==p.request && ctx.r29.u32==p.streamState &&
            PPC_LOAD_U8(p.request+0xD)==1 && PPC_LOAD_U8(p.request+0xE)==p.voiceIndex &&
            PPC_LOAD_U32(p.request+4)==p.streamHandle && !PPC_LOAD_U32(p.request+8) &&
            PPC_LOAD_U8(r.address+0x2F)==p.slot && !PPC_LOAD_U32(q+0xC),
            "Original EXm0 enqueue ABI/publication changed");
    p.preflight=true;
}
void EngineAudioOwners::input(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::unique_lock lock(s.mutex);s.frame(ctx,base);
    auto& r=s.live(ctx.r3.u32,base);const auto it=s.producers.find(&ctx);
    require(it!=s.producers.end(),"EXm0 input lacks a prepared original source");
    const auto p=it->second;s.sourceIdentity(*p,base);s.queueIdentity(*p,r,base);
    const uint32_t v=r.address,q=v+PPC_LOAD_U32(v+0x24)+20*p->slot;
    require(p->preflight && !p->notified && !p->published && !p->cancelled && p->generation==r.generation &&
            p->voice==v && ctx.lr==0x8234E7D4 && ctx.r1.u32==p->sp-0x100 &&
            ctx.r4.u32==p->slot && ctx.r31.u32==v && ctx.r30.u32==q && ctx.r29.u32==p->slot &&
            PPC_LOAD_U8(v+0x2F)==p->slot && PPC_LOAD_U8(v+0x30)==p->slot,
            "Original EXm0 input notification was repeated or reordered");
    p->notified=true;
    // This original pure CPU pop owns V30. No owner lock survives an AOT call.
    lock.unlock();uint32_t popped=0;
    try {EngineCpuCalls cpu(ctx,base);popped=cpu.invoke(0x8233E108,v);}
    catch(...) {lock.lock();const auto ri=s.records.find(v);if(ri!=s.records.end() && ri->second.generation==p->generation) s.cancel(ri->second);throw;}
    lock.lock();s.frame(ctx,base);auto& now=s.live(v,base);
    require(now.generation==p->generation && s.producers.contains(&ctx) && s.producers.at(&ctx)==p &&
            !p->cancelled && popped==q && PPC_LOAD_U8(v+0x30)==(p->slot+1)%20,
            "Original EXm0 queue pop changed source generation or result");
    s.sourceIdentity(*p,base);s.queueIdentity(*p,now,base);
    now.native->configured=true;PPC_STORE_U32(v+0x50,now.native->selector);PPC_STORE_U8(v+0x55,0);
    ctx.r3.u64=0;
}
void EngineAudioOwners::producerEnd(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);
    const auto it=s.producers.find(&ctx);if(it==s.producers.end()) return;
    const auto p=it->second;auto& r=s.live(p->voice,base);s.sourceIdentity(*p,base);s.queueIdentity(*p,r,base);
    require(p->generation==r.generation && p->preflight && p->notified && !p->published && !p->cancelled &&
            ctx.r1.u32==p->sp-0x90 && ctx.r31.u32==p->block && ctx.r30.u32==p->request &&
            ctx.r29.u32==p->streamState && ctx.r28.u32==p->samples && ctx.r3.u32==p->block+p->charged &&
            PPC_LOAD_U8(p->request+0xC)==p->slot && PPC_LOAD_U8(p->request+0xD)==1 &&
            PPC_LOAD_U32(p->streamState+0x14)==p->producedBefore+p->samples &&
            PPC_LOAD_U8(p->voice+0x2F)==(p->slot+1)%20,"Original EXm0 producer completion changed");
    p->published=true;s.producers.erase(it);
}
void EngineAudioOwners::decode(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::shared_ptr<EngineAudioOutput> output;
    {std::lock_guard lock(s.runtime.audioMutex);output=s.runtime.engineAudioOutput;}
    std::lock_guard lock(s.mutex);s.frame(ctx,base);auto& r=s.live(ctx.r3.u32,base);auto& n=*r.native;
    require(n.configured && n.source && n.enabled,"EXm0 decode requires a configured enabled original source");
    const uint32_t frames=ctx.r5.u32,v=r.address,d=ctx.r4.u32;
    if(!frames) {ctx.r3.u64=0;return;}
    require(ctx.lr==0x823628E4 && ctx.r31.u32==v && ctx.r25.u32==d && ctx.r30.u32==frames &&
            frames<=256 && ctx.r24.u32<=256 && frames<=ctx.r24.u32 && !n.progress.context && output,
            "EXm0 decode is outside the original unbuffered mixer callback");
    const uint32_t slot=PPC_LOAD_U8(v+0x31),q=v+PPC_LOAD_U32(v+0x24)+20*slot,before=PPC_LOAD_U32(v+0x1C);
    const auto p=n.queue[slot];
    require(p && p->generation==r.generation && p->published && !p->delivered && !p->cancelled && !p->releasing,
            "EXm0 output has no published original source");
    s.sourceIdentity(*p,base);s.queueIdentity(*p,r,base);
    const auto view=p->decoder->query(p->receipt);
    require(view.status==Audio::XmaSource::SourceStatus::Prepared && view.consumedFrames==before &&
            before<p->samples && frames<=p->samples-before &&
            (n.remaining?(n.currentSlot==q && n.remaining==p->samples-before):before==0),
            "EXm0 output quota differs from original queue progress");
    // Both mutexes remain held through the final store and nonallocating
    // commit. Destruction cannot close the decoder or release mixer storage.
    auto destination=output->pcm(ctx,base,r.owner,d,r.channels);
    s.pcmDisjoint(destination.bytes,r,d,base);
    const auto readers=audioReaders(s.runtime);require(bool(readers),"EXm0 output lost reader provenance");
    readers->excludeOutput(destination.bytes);
    const auto staged=p->decoder->stage(p->receipt,frames);
    if(staged.status!=Audio::XmaSource::StageStatus::Complete || !staged.ticket) {
        s.cancel(r);throw Failure("Native EXm0 cannot complete the original all-layer quota");
    }
    require(staged.ticket->progressBefore()==before && staged.ticket->frames()==frames && staged.ticket->channels()==r.channels,
            "Native EXm0 staged quota changed its original source accounting");
    Audio::XmaFloatOutput publish(PPC_LOAD_U32(d+4),PPC_LOAD_U16(d+0xE),*staged.ticket);
    p->decoder->validateCommit(staged.ticket);
    uint32_t carry=n.carry;
    if(!before && !p->flag) carry=0;
    const uint32_t remaining=p->samples-before-frames;
    if(!remaining) {const uint32_t residue=(p->samples+(p->flag?0:384))&511;if(residue) carry=512-residue;}
    publish.write(destination.bytes);p->decoder->commit(staged.ticket);
    n.currentSlot=q;n.remaining=remaining;n.carry=carry;++n.quotas;
    PPC_STORE_U32(v+0x38,q);PPC_STORE_U32(v+0x48,remaining);PPC_STORE_U32(v+0x4C,carry);
    n.progress={&ctx,ctx.r1.u32,before,frames,slot,false};ctx.r3.u64=frames;
    if(n.quotas==1 && sampleSuccessLog(s.pcmLogs)) std::fprintf(stderr,"[NATIVE AUDIO PCM] original mixer V=%08X D=%08X frames=%u channels=%u; real all-layer PCM committed\n",v,d,frames,r.channels);
}
void EngineAudioOwners::advanceBegin(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);
    if(!s.records.contains(ctx.r3.u32)) return;
    auto& r=s.live(ctx.r3.u32,base);auto& progress=r.native->progress;
    require(progress.context==&ctx && !progress.advancing && progress.sp==ctx.r1.u32 && ctx.lr==0x823628F4 &&
            ctx.r4.u32==progress.quota && PPC_LOAD_U8(r.address+0x31)==progress.slot &&
            PPC_LOAD_U32(r.address+0x1C)==progress.before,"Original EXm0 advance lacks a completed PCM quota");
    progress.advancing=true;
}
void EngineAudioOwners::advanceEnd(PPCContext& ctx,uint8_t* base,bool completion) {
    // At the conditional return, equality still has original stores to run.
    if(!completion && ctx.cr6.eq) return;
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);
    if(!s.records.contains(ctx.r3.u32)) return;
    auto& r=s.live(ctx.r3.u32,base);auto& n=*r.native;const auto progress=n.progress;
    require(progress.context==&ctx && progress.advancing && progress.sp==ctx.r1.u32 && ctx.lr==0x823628F4,
            "Original EXm0 advance return lacks its quota scope");
    const auto p=n.queue[progress.slot];require(bool(p),"Original EXm0 advance lost its source");
    const uint32_t after=progress.before+progress.quota,v=r.address,q=v+PPC_LOAD_U32(v+0x24)+20*progress.slot;
    const auto view=p->decoder->query(p->receipt);
    require(view.consumedFrames==after && completion==(after==p->samples),"Original EXm0 advance and PCM receipt disagree");
    if(completion) {
        const uint32_t next=(progress.slot+1)%20,nextQueue=v+PPC_LOAD_U32(v+0x24)+20*next;
        require(view.status==Audio::XmaSource::SourceStatus::Delivered && !PPC_LOAD_U32(q+0xC) &&
                PPC_LOAD_U8(v+0x31)==next && PPC_LOAD_U32(v+0x1C)==PPC_LOAD_U32(nextQueue+8),
                "Original EXm0 completed queue stores changed");
        p->delivered=true;n.queue[progress.slot].reset();
    }else require(view.status==Audio::XmaSource::SourceStatus::Prepared && PPC_LOAD_U8(v+0x31)==progress.slot &&
                  PPC_LOAD_U32(v+0x1C)==after && PPC_LOAD_U32(q+0xC)==p->samples,
                  "Original EXm0 partial queue progress changed");
    n.progress={};
}
void EngineAudioOwners::sourceFreeBegin(uint32_t pc,PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);
    const bool early=pc==0x82342D28;require(early || pc==0x82341840,"Unknown EXm0 release entry");
    const uint32_t request=early?ctx.r29.u32:ctx.r31.u32-0xD,b=PPC_LOAD_U32(request);
    const auto it=s.sources.find(b?uint64_t(b):State::residentKey(request));if(it==s.sources.end()) return;const auto p=it->second;
    s.sourceIdentity(*p,base,true);
    require(!p->releasing && !s.frees.contains(&ctx) && request==p->request &&
            (early?ctx.r31.u32:ctx.r30.u32)==p->stream &&
            (early?(ctx.r28.u32==p->streamState && ctx.r30.u32==p->voiceIndex && (p->cancelled || p->delivered)):
                   (p->delivered && !p->cancelled && PPC_LOAD_U8(request+0xD)==2)),
            "Original EXm0 source release is premature or has changed identity");
    if(p->bank || p->amx) {
        const uint32_t account=PPC_LOAD_U32(p->streamState+0x18);
        require(!b && !account && !PPC_LOAD_U32(p->streamState+0x24),
                "Resident EXm0 retirement acquired reader accounting");
        s.frees.emplace(&ctx,State::SourceFree{ctx.r1.u32,early?0x82342D64u:0x82341884u,account,p});
        p->releasing=true;return;
    }
    require(early?PPC_LOAD_U32(p->streamState+0x24)!=0:PPC_LOAD_U32(request+4)==p->streamHandle,
            "Qualified EXm0 source would bypass its original reader release");
    const uint32_t account=PPC_LOAD_U32(p->streamState+0x18);
    require(account>=p->charged,"Original EXm0 source accounting underflow");
    s.frees.emplace(&ctx,State::SourceFree{ctx.r1.u32,early?0x82342D60u:0x82341880u,account,p});p->releasing=true;
}
void EngineAudioOwners::readerRelease(uint32_t pc,PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);
    const auto it=s.frees.find(&ctx);
    if(it==s.frees.end()) {
        require(pc!=0x8238D640 || !s.sources.contains(ctx.r4.u32),"EXm0 reader release bypassed original source retirement");return;
    }
    auto& f=it->second;const auto& p=*f.source;
    if(pc==0x8238D640) {
        require(!f.readerEntered && ctx.r1.u32==f.sp && ctx.lr==f.returnPC &&
                ctx.r3.u32==p.streamHandle && ctx.r4.u32==p.allocation,"EXm0 reader release callback changed");
        f.readerEntered=true;
    }else {
        require(pc==0x8238D6CC && f.readerEntered && !f.readerReturned && ctx.r1.u32==f.sp-0x70,
                "EXm0 reader release returned outside its original call");
        const auto readers=audioReaders(s.runtime);require(bool(readers),"EXm0 reader release lost its registry");
        readers->validateReleased(p.claim);f.readerReturned=true;
    }
}
void EngineAudioOwners::sourceFreeEnd(uint32_t pc,PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(ctx,base);
    const auto it=s.frees.find(&ctx);if(it==s.frees.end()) return;const auto f=it->second;const auto p=f.source;
    if(p->bank || p->amx) {
        const bool early=pc==0x82342D64;
        require(pc==f.returnPC && (early || pc==0x82341884) && ctx.r1.u32==f.sp &&
                !f.readerEntered && !f.readerReturned && p->releasing &&
                (early?ctx.r29.u32:ctx.r31.u32-0xD)==p->request &&
                (early?ctx.r31.u32:ctx.r30.u32)==p->stream,
                "Resident EXm0 retirement lacks its original null-reader join");
        s.sourceIdentity(*p,base,true);
        require(!PPC_LOAD_U8(p->request+0xD) && PPC_LOAD_U32(p->streamState+0x18)==f.account,
                "Resident EXm0 original flag clear/accounting changed");
        const auto receipt=p->decoder->query(p->receipt);
        require(receipt.status==Audio::XmaSource::SourceStatus::Delivered || receipt.status==Audio::XmaSource::SourceStatus::Cancelled ||
                receipt.status==Audio::XmaSource::SourceStatus::Failed,"Resident EXm0 retired an undelivered live receipt");
        p->decoder->retire(p->receipt);s.sources.erase(p->key);s.frees.erase(it);
        if(receipt.status!=Audio::XmaSource::SourceStatus::Delivered || sampleSuccessLog(s.deliveredRetirementLogs))
            std::fprintf(stderr,"[NATIVE AUDIO RESIDENT RETIRED] R=%08X original=%08X status=%u; no reader callback or charge\n",
                p->request,pc,unsigned(receipt.status));
        return;
    }
    require(pc==f.returnPC && ctx.lr==pc && ctx.r1.u32==f.sp && f.readerEntered && f.readerReturned && p->releasing,
            "EXm0 source retirement lacks a completed original reader release");
    // B (and on early cleanup V) may already be freed. Only retained host
    // identities and still-owned A/P/R storage are consulted from here onward.
    const auto& m=s.streamMember(p->stream,p->voiceIndex,base,true);
    require(m.generation==p->memberGeneration && m.stateGeneration==p->stateGeneration &&
            s.groups.at(m.group).generation==p->groupGeneration && PPC_LOAD_U32(p->request)==p->allocation &&
            !PPC_LOAD_U8(p->request+0xD) && PPC_LOAD_U32(p->streamState+0x18)==f.account-p->charged,
            "Original EXm0 release accounting or containing generation changed");
    const auto readers=audioReaders(s.runtime);require(bool(readers),"EXm0 source retirement lost reader provenance");
    readers->validateReleased(p->claim);
    const auto receipt=p->decoder->query(p->receipt);
    require(receipt.status==Audio::XmaSource::SourceStatus::Delivered || receipt.status==Audio::XmaSource::SourceStatus::Cancelled ||
            receipt.status==Audio::XmaSource::SourceStatus::Failed,"EXm0 reader freed an undelivered live receipt");
    p->decoder->retire(p->receipt);s.sources.erase(p->key);s.frees.erase(it);
}
size_t EngineAudioOwners::count() const {
    auto& s=*state;std::lock_guard lock(s.mutex);size_t count=0;for(const auto& [v,r]:s.records) count+=bool(r.native);return count;
}
uint32_t EngineAudioOwners::reservedLayers() const {std::lock_guard lock(state->mutex);return state->reserved;}
bool EngineAudioOwners::ready() const {std::lock_guard lock(state->mutex);return bool(state->factory);}
EngineAudioOwners::View EngineAudioOwners::view(uint32_t owner) const {
    auto& s=*state;std::lock_guard lock(s.mutex);const auto& r=s.find(owner);
    if(r.phase!=State::Phase::Live || !r.native) throw Failure("EXm0 view lacks a live native instance");
    return {r.generation,r.channels,uint32_t(r.native->layers.size()),r.native->enabled};
}
std::weak_ptr<const void> EngineAudioOwners::lease(uint32_t owner,uint64_t identity) const {
    auto& s=*state;std::lock_guard lock(s.mutex);const auto& r=s.find(owner);
    if(r.phase!=State::Phase::Live || !r.native || r.generation!=identity) throw Failure("EXm0 lease generation is stale");
    return r.native;
}
}
