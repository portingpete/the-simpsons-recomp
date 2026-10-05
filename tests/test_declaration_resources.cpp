#include "renderer/declaration_resources.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <type_traits>

using namespace Simpsons::Graphics;
namespace {
using Bytes=std::vector<uint8_t>;
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class F> void rejects(F&& call) {
    bool rejected=false;
    try {call();} catch(const DeclarationError&) {rejected=true;}
    require(rejected,"Invalid declaration operation succeeded");
}
Bytes hex(const char* text) {
    Bytes result;
    int high=-1;
    for(;*text;++text) {
        if(*text==' ') continue;
        const int value=*text>='0' && *text<='9'?*text-'0':*text>='A' && *text<='F'?*text-'A'+10:-1;
        require(value>=0,"Bad fixture hex");
        if(high<0) high=value; else {result.push_back(uint8_t(high*16+value));high=-1;}
    }
    require(high<0,"Partial fixture byte");return result;
}
const auto flat=hex("00000000 002C23A5 00000000 00FF0000 FFFFFFFF 00000000");
const auto textured=hex("00000000 002C23A5 00000000 00000008 002C23A5 00050000 00FF0000 FFFFFFFF 00000000");
// Unknown stack +B bytes are zero only in these synthetic test inputs. The
// source-evidence test below proves which bytes are written; runtime preserves all.
const auto scratch=hex("00000000 001A23A6 00000000 00000010 00182886 000A0000 00000014 002C23A5 00050000 00FF0000 FFFFFFFF 00000000");
const auto pipeline0=hex("00000000 002A23B9 00000000 0000000C 002A23B9 00030000 00000018 00182886 000A0000 0000001C 002C23A5 00050000 00FF0000 FFFFFFFF 00000000");
const auto pipeline1=hex("00000000 002A23B9 00000000 0000000C 00182886 000A0000 00000010 002C23A5 00050000 00FF0000 FFFFFFFF 00000000");
const auto pipeline2=hex("00000000 002A23B9 00000000 0000000C 00182886 000A0000 00FF0000 FFFFFFFF 00000000");

void contracts() {
    static_assert(std::is_same_v<decltype(std::declval<const DeclarationRecord&>().bytes()),std::span<const uint8_t>>);
    static_assert(!std::is_copy_constructible_v<DeclarationRecord>);
    DeclarationRegistry registry;
    const std::array fixtures={flat,textured,scratch,pipeline0,pipeline1,pipeline2};
    const std::array<uint32_t,6> extents={8,16,28,36,24,16};
    std::array<DeclarationId,6> ids{};
    for(size_t i=0;i<fixtures.size();++i) {
        ids[i]=registry.create(fixtures[i]);
        const auto record=registry.record(ids[i]);
        require(std::ranges::equal(record->bytes(),fixtures[i]),"Record did not preserve exact original bytes");
        require(record->elements().size()==fixtures[i].size()/12-1,"Terminator included as a source element");
        require(record->minimumStreamBytes()==extents[i],"Wrong source byte extent");
        require(registry.referenceCount(ids[i])==1,"New record did not start at one logical reference");
        require(registry.create(fixtures[i])==ids[i],"Identical active bytes did not deduplicate");
        require(registry.referenceCount(ids[i])==2,"Dedup did not retain resource");
    }
    require(registry.liveCount()==6 && registry.cachedCount()==6,"Wrong unique/cache counts");
    const auto elements=registry.record(ids[2])->elements();
    require(elements[0].type==0x1A23A6 && elements[0].storageBytes==16 && elements[1].usage==10 &&
            elements[1].offset==16 && elements[2].usage==5 && elements[2].offset==20,"BE field decoding failed");

    auto mutableBytes=textured;
    const auto copyId=registry.create(mutableBytes);
    mutableBytes[3]=64;
    require(registry.record(copyId)->bytes()[3]==0,"Declaration retained borrowed caller bytes");
    auto opaque=textured;
    opaque[11]=0xA5;opaque.back()=0xFE;
    const auto opaqueId=registry.create(opaque);
    require(opaqueId!=ids[1] && registry.record(opaqueId)->elements()[0].opaque==0xA5 &&
            registry.record(opaqueId)->bytes().back()==0xFE,"Opaque bytes lost or removed from dedup key");
    auto onlyTerminator=textured;onlyTerminator.back()=7;
    require(registry.create(onlyTerminator)!=ids[1],"Terminal trailing byte ignored by dedup");
    require(registry.referenceCount(ids[1])==3,"Snapshot/input copy altered logical refcount");

    registry.release(ids[0]);
    auto kept=registry.record(ids[0]);
    std::weak_ptr<const DeclarationRecord> weak=kept;
    registry.release(ids[0]);
    require(!registry.contains(ids[0]) && registry.cachedCount()==8,"Final release did not retire active ID and keep descriptor cache");
    rejects([&]{registry.record(ids[0]);});rejects([&]{registry.retain(ids[0]);});rejects([&]{registry.release(ids[0]);});
    const auto recreated=registry.create(flat);
    require(recreated.slot==ids[0].slot && recreated.generation!=ids[0].generation,"Recreation reused a stale identity");
    require(registry.record(recreated)==kept && registry.referenceCount(recreated)==1,"Cached immutable descriptor not reused");
    DeclarationRegistry other;
    auto foreign=other.create(flat);
    require(!registry.contains(foreign) && !other.contains(recreated),"IDs crossed registry ownership");
    rejects([&]{registry.release(foreign);});rejects([&]{registry.record({});});
    registry.reset();
    require(!registry.contains(recreated) && !registry.liveCount() && !registry.cachedCount(),"Reset left live identities/cache");
    require(std::ranges::equal(kept->bytes(),flat),"Retained immutable record lost data at reset");
    kept.reset();require(weak.expired(),"Reset retained unowned descriptor storage");
    auto afterReset=registry.create(flat);
    require(afterReset!=recreated,"Reset reused a stale ID");
    std::shared_ptr<const DeclarationRecord> survives;
    {DeclarationRegistry temporary;survives=temporary.record(temporary.create(textured));}
    require(std::ranges::equal(survives->bytes(),textured),"Record snapshot did not survive registry destruction");

    // A fresh ID on every zero-to-one transition, with one immutable cache entry.
    registry.reset();
    DeclarationId previous{};
    for(unsigned i=0;i<1000;++i) {
        const auto next=registry.create(scratch);
        require(next!=previous && !registry.contains(previous),"Sequential lifetimes reused an ID");
        registry.release(next);previous=next;
    }
    require(registry.cachedCount()==1 && registry.liveCount()==0,"Sequential use grew the descriptor cache");
}

void validation() {
    rejects([]{DeclarationRegistry bad({0,1});});
    rejects([]{DeclarationRegistry bad({1,0});});
    rejects([]{DeclarationRegistry bad({},nullptr);});
    DeclarationRegistry registry({1024,2});
    auto id=registry.create(flat);
    registry.retain(id);
    rejects([&]{registry.retain(id);});rejects([&]{registry.create(flat);});
    require(registry.referenceCount(id)==2,"Reference overflow changed state");
    const auto other=registry.create(textured);
    DeclarationRegistry bounded({1,2});
    const auto boundedId=bounded.create(flat);
    rejects([&]{bounded.create(scratch);});
    require(bounded.liveCount()==1 && bounded.contains(boundedId),"Cache overflow changed existing entries");
    require(registry.contains(other),"Second declaration lost ownership");

    auto bad=[&](Bytes bytes) {
        const auto before=registry.referenceCount(id);
        rejects([&]{registry.create(bytes);});
        require(registry.referenceCount(id)==before && registry.cachedCount()==2,"Validation failure mutated registry");
    };
    bad({});bad(Bytes(flat.end()-12,flat.end()));
    for(size_t size=1;size<flat.size();++size) bad(Bytes(flat.begin(),flat.begin()+size));
    auto changed=flat;changed.push_back(0);bad(changed);
    changed=flat;changed.insert(changed.end(),flat.begin(),flat.end());bad(changed);
    changed=flat;changed[12]=0xFF;bad(changed); // FFFF stream is not the sentinel.
    changed=flat;changed[13]=0;bad(changed); // Type-only termination.
    changed=flat;changed[16]=0;bad(changed); // Stream-only termination.
    changed=flat;std::fill(changed.begin()+12,changed.end(),0);bad(changed);
    changed=flat;changed[0]=1;bad(changed); // Unverified stream.
    changed=flat;changed[8]=1;bad(changed); // Unverified method.
    changed=flat;changed[9]=2;bad(changed); // Unverified usage.
    changed=flat;changed[10]=1;bad(changed); // Unverified usage index.
    changed=flat;changed[4]=1;bad(changed); // Known low six bits, unknown full type.
    changed=flat;changed[7]=0;bad(changed);
    changed=flat;changed[3]=1;bad(changed); // Unaligned source.
    changed=flat;changed[2]=0xFF;changed[3]=0xFC;bad(changed); // Extent overflow.
    for(size_t field:{size_t(14),size_t(20),size_t(21),size_t(22)}) {changed=flat;changed[field]=1;bad(changed);}
    changed=textured;changed[21]=0;bad(changed); // Duplicate usage/index.
    bad(Bytes((DeclarationRegistry::MaxElements+2)*12));
    // Largest supported aligned source endpoint is accepted without wrapping.
    DeclarationRegistry limits;
    changed=flat;changed[2]=0xFF;changed[3]=0xF8;
    require(limits.record(limits.create(changed))->minimumStreamBytes()==0x10000,"Exact source extent bound rejected/wrapped");
}

class FailingMemory final : public std::pmr::memory_resource {
public:
    size_t calls{},outstanding{},fail=SIZE_MAX;
private:
    void* do_allocate(size_t bytes,size_t alignment) override {
        if(calls++==fail) throw std::bad_alloc();
        auto result=std::pmr::new_delete_resource()->allocate(bytes,alignment);++outstanding;return result;
    }
    void do_deallocate(void* pointer,size_t bytes,size_t alignment) override {
        --outstanding;std::pmr::new_delete_resource()->deallocate(pointer,bytes,alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {return this==&other;}
};

void allocationFailures() {
    // Probe every actual PMR allocation on both initial creation and insertion
    // beside an existing ID; no simulated resource objects or success stubs.
    for(bool existing:{false,true}) {
        size_t points=0;
        {FailingMemory memory;{DeclarationRegistry registry({},&memory);
            if(existing) registry.create(flat);
            const auto begin=memory.calls;registry.create(scratch);points=memory.calls-begin;
        } require(!memory.outstanding,"Successful registry leaked allocator storage");}
        require(points>=2,"Failure probe missed owned-data allocations");
        for(size_t point=0;point<points;++point) {
            FailingMemory memory;
            {DeclarationRegistry registry({},&memory);DeclarationId old{};
                if(existing) old=registry.create(flat);
                const auto before=memory.outstanding;
                memory.fail=memory.calls+point;
                bool failed=false;
                try {registry.create(scratch);} catch(const std::bad_alloc&) {failed=true;}
                require(failed,"Allocation failure injection did not fire");
                require(registry.cachedCount()==size_t(existing) && registry.liveCount()==size_t(existing),"Partial declaration was published");
                require(memory.outstanding==before,"Partial declaration allocation leaked");
                if(existing) require(registry.contains(old) && registry.referenceCount(old)==1,"Failed allocation damaged prior ID");
                memory.fail=SIZE_MAX;
                auto retry=registry.create(scratch);
                require(registry.contains(retry),"Allocation retry did not recover");
            }
            require(!memory.outstanding,"Allocator storage survived registry destruction");
        }
    }
}

uint32_t imageWord(const Bytes& image,uint32_t address) {
    require(address>=0x82000000 && uint64_t(address)-0x82000000+4<=image.size(),"Evidence address out of image");
    const auto* p=image.data()+(address-0x82000000);
    return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
}

// Offline test-only constant/store evaluator for three straight-line original
// declaration construction windows. No branches, calls, loads, guest execution
// or instruction fetch loop. Unknown register values cannot become evidence.
struct StoreProof {
    std::array<std::optional<uint32_t>,32> registers{};
    std::array<std::optional<uint8_t>,256> stack{};
    void read(const Bytes& image,uint32_t first,uint32_t end) {
        for(uint32_t pc=first;pc<end;pc+=4) {
            const uint32_t w=imageWord(image,pc),op=w>>26,rt=(w>>21)&31,ra=(w>>16)&31,rb=(w>>11)&31;
            const auto imm=int16_t(w&0xFFFF);
            if(op==14 || op==15) {
                auto base=ra?registers[ra]:std::optional<uint32_t>(0);
                registers[rt]=base?std::optional<uint32_t>(*base+(op==15?uint32_t(int32_t(imm))*65536u:uint32_t(int32_t(imm)))):std::nullopt;
            } else if(op==24) {
                registers[ra]=registers[rt]?std::optional<uint32_t>(*registers[rt]|(w&0xFFFF)):std::nullopt;
            } else if(op==31 && ((w>>1)&1023)==444) {
                registers[ra]=registers[rt] && registers[rb]?std::optional<uint32_t>(*registers[rt]|*registers[rb]):std::nullopt;
            } else if(op==36 || op==38 || op==44) {
                require(ra==1 && registers[rt].has_value(),"Unproven source/base in declaration store");
                const unsigned width=op==36?4:op==44?2:1;
                require(imm>=0 && unsigned(imm)+width<=stack.size(),"Declaration store outside stack evidence window");
                for(unsigned i=0;i<width;++i) stack[unsigned(imm)+i]=uint8_t(*registers[rt]>>(8*(width-1-i)));
            } else throw std::runtime_error("Unexpected instruction in fixed declaration evidence window");
        }
    }
    void matches(const Bytes& expected) const {
        for(size_t i=0;i<expected.size();++i) {
            if(i%12==11) require(!stack[0x50+i].has_value(),"Original builder unexpectedly writes trailing byte");
            else require(stack[0x50+i].has_value() && *stack[0x50+i]==expected[i],"Recovered original store differs from fixture");
        }
    }
};

void originalEvidence(const char* path) {
    std::ifstream input(path,std::ios::binary);
    require(bool(input),"Cannot open original flat image for byte proof");
    const Bytes image{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    require(image.size()==15466496,"Unexpected original image length");
    for(const auto& [address,fixture]:std::array<std::pair<uint32_t,Bytes>,3>{{{0x82151724,textured},{0x82151748,flat},
            {0x8206A310,Bytes(flat.end()-12,flat.end())}}})
        require(std::equal(fixture.begin(),fixture.end(),image.begin()+(address-0x82000000)),"Static original array differs from byte fixture");
    require(imageWord(image,0x82409AA0)==0x3BC00000 && imageWord(image,0x82416C68)==0x3BE00000,"Original zero-register seed changed");
    StoreProof scratchProof;scratchProof.registers[30]=0;
    scratchProof.read(image,0x82409B1C,0x82409BB8);scratchProof.matches(scratch);
    StoreProof pipelineProof;pipelineProof.registers[31]=0;
    pipelineProof.read(image,0x82416CB0,0x82416D6C);pipelineProof.matches(pipeline0);
    pipelineProof.read(image,0x82416D70,0x82416DDC);pipelineProof.matches(pipeline1);
    pipelineProof.read(image,0x82416DE0,0x82416E30);pipelineProof.matches(pipeline2);
    // Exact scanner, comparison/copy and reference ownership instructions.
    for(const auto [pc,word]:std::array<std::pair<uint32_t,uint32_t>,19>{{
        {0x823EF84C,0x395B0004},{0x823EF858,0x394A000C},{0x823EF864,0x2F09FFFF},
        {0x823EF86C,0x3B8B0001},{0x823EF89C,0x1D7C000C},{0x823EF8C8,0x39080004},
        {0x823EF950,0x28030000},{0x823EF954,0x907A0000},{0x823EF9B0,0x4864D3D1},
        {0x823EF9E0,0x48051CB1},{0x823EF9EC,0x48055EF5},{0x823EFA74,0x48051C95},
        {0x823EFA88,0x7D5F592E},{0x82445914,0x2B0900FF},{0x82445868,0x39400001},
        {0x82445884,0x915F0004},{0x824416B0,0x394A0001},{0x8244172C,0x394AFFFF},
        {0x82409BB8,0x4BFE5C81}}}) require(imageWord(image,pc)==word,"Original declaration ABI evidence word changed");
}
}

int main(int argc,char** argv) {
    try {
        if(argc>2) throw std::runtime_error("Usage: DeclarationResourcesTests [original-flat-image]");
        contracts();validation();allocationFailures();
        if(argc==2) originalEvidence(argv[1]);
        std::puts(argc==2?"Declaration ownership and original byte proof passed (six layouts, refcounts, dedup, stale IDs, allocation rollback).":
            "Declaration ownership passed; original image evidence was not requested.");
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"Declaration contract failure: %s\n",error.what());return 1;
    }
}
