#include "runtime/zprepass_vertices.h"
#include "runtime/static_mesh_source_cache.h"
#include "runtime/runtime.h"
#include "common/byte_range_equal.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <emmintrin.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <tuple>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Only this fixture executable replaces allocation. Preparation is outside the
// observed interval; actual production insertion allocations are counted and
// failed one at a time, including the list/map/vector allocations behind STL.
struct OwnedSourceAllocationObservation {
    const std::vector<uint8_t>* inputs[3]{};
    const uint8_t* pointers[3]{};
    size_t calls{},fail{SIZE_MAX};
    bool injected{},afterConsume{};
    void (*onAllocation)(size_t,void*) noexcept=nullptr;
    void* argument{};
};
static thread_local OwnedSourceAllocationObservation* ownedSourceAllocationObservation=nullptr;
void* operator new(std::size_t size) {
    if(auto* observation=ownedSourceAllocationObservation) {
        for(size_t plane=0;plane<3;++plane)
            if(observation->inputs[plane]->data()!=observation->pointers[plane])observation->afterConsume=true;
        const size_t ordinal=observation->calls++;
        if(observation->onAllocation)observation->onAllocation(ordinal,observation->argument);
        if(ordinal==observation->fail) {
            observation->injected=true;
            throw std::bad_alloc();
        }
    }
    if(void* value=std::malloc(size?size:1))return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {return ::operator new(size);}
void operator delete(void* value) noexcept {std::free(value);}
void operator delete[](void* value) noexcept {std::free(value);}
void operator delete(void* value,std::size_t) noexcept {std::free(value);}
void operator delete[](void* value,std::size_t) noexcept {std::free(value);}

namespace Simpsons {
// Const observations only. No mutation or allocation is hidden in this probe.
struct StaticMeshSourceCacheTestProbe {
    struct Entry {
        bool found{};
        std::array<std::span<const uint8_t>,3> planes{};
        std::array<size_t,3> capacities{};
        size_t charge{};
    };
    template<class Mesh>static Entry entry(const StaticMeshSourceCache<Mesh>& cache,const Mesh* mesh) noexcept {
        for(const auto& candidate:cache.lru_)if(candidate.mesh.get()==mesh)
            return {true,{candidate.vertices,candidate.indices,candidate.declaration},
                {candidate.vertices.capacity(),candidate.indices.capacity(),candidate.declaration.capacity()},candidate.charge};
        return {};
    }
    template<class Mesh>static size_t hints(const StaticMeshSourceCache<Mesh>& cache) noexcept {return cache.hints_.size();}
    template<class Mesh>static size_t hints(const StaticMeshSourceCache<Mesh>& cache,const Mesh* mesh) noexcept {
        size_t result{};
        for(const auto& [key,hint]:cache.hints_) {
            (void)key;
            if(hint.entry&&hint.entry->mesh.get()==mesh)++result;
        }
        return result;
    }
};
}

namespace {
using Bytes=std::vector<uint8_t>;
using Row=std::array<uint32_t,3>;
using Simpsons::decodeStaticZPrepassVertices;
size_t checks{};
void need(bool value,const char* why) {++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& action,const char* why) {
    bool rejected=false;try {action();}catch(const Simpsons::Failure&){rejected=true;}need(rejected,why);
}
void put(Bytes& bytes,size_t at,uint32_t value) {
    need(at<=bytes.size()&&bytes.size()-at>=4,"Invalid fixture word write");
    bytes[at]=uint8_t(value>>24);bytes[at+1]=uint8_t(value>>16);bytes[at+2]=uint8_t(value>>8);bytes[at+3]=uint8_t(value);
}
uint32_t word(const Bytes& bytes,size_t at) {
    need(at<=bytes.size()&&bytes.size()-at>=4,"Truncated capture word");
    return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];
}
Bytes declaration() {
    // Six exact live013 rows; only position0 feeds the Boolean-false program.
    constexpr std::array<Row,6> rows={{{0,0x002A23B9,0},{12,0x002A2187,0x00030000},
        {16,0x00182886,0x000A0000},{20,0x002C23A5,0x00050000},{28,0x002C23A5,0x00050100},
        {0x00FF0000,UINT32_MAX,0}}};
    Bytes result(rows.size()*12);for(size_t i=0;i<rows.size();++i)for(size_t lane=0;lane<3;++lane)put(result,12*i+4*lane,rows[i][lane]);
    return result;
}
Bytes vertices() {
    Bytes result(72);
    for(size_t at=0;at<result.size();at+=4)put(result,at,0x7FC12345);
    constexpr std::array<uint32_t,6> positions={0x3E000001,0xC1480000,0x447A0000,0x80000000,0x00000001,0x7F7FFFFF};
    for(size_t i=0;i<2;++i)for(size_t lane=0;lane<3;++lane)put(result,36*i+4*lane,positions[3*i+lane]);
    return result;
}
void inert(const Simpsons::Graphics::ZPrepassVertex& vertex) {
    for(float v:vertex.weights)need(std::bit_cast<uint32_t>(v)==0,"Invented unused weight");
    for(float v:vertex.indices)need(std::bit_cast<uint32_t>(v)==0,"Invented unused bone index");
    for(const auto& delta:vertex.morph)for(float v:delta)need(std::bit_cast<uint32_t>(v)==0,"Invented unused morph input");
}
void positions(const std::vector<Simpsons::Graphics::ZPrepassVertex>& actual,const Bytes& source,uint32_t stride,uint32_t offset) {
    need(actual.size()==source.size()/stride,"Decoded vertex count differs");
    for(size_t i=0;i<actual.size();++i) {
        for(size_t lane=0;lane<3;++lane)
            need(std::bit_cast<uint32_t>(actual[i].position[lane])==word(source,i*stride+offset+4*lane),"Position bits or lane order changed");
        inert(actual[i]);
    }
}
void consumedAndDeadInputs() {
    auto d=declaration(),v=vertices();const auto originalD=d,originalV=v;
    const auto out=decodeStaticZPrepassVertices(v,d,36);positions(out,v,36,0);
    need(d==originalD&&v==originalV,"Decoder modified its borrowed inputs");
    // Every optional row can be absent, including UV0. No UV finiteness check.
    for(size_t row=1;row<5;++row) {
        auto sparse=d;sparse.erase(sparse.begin()+12*row,sparse.begin()+12*(row+1));
        positions(decodeStaticZPrepassVertices(v,sparse,36),v,36,0);
    }
    Bytes positionOnly(d.begin(),d.begin()+12);positionOnly.insert(positionOnly.end(),d.end()-12,d.end());
    Bytes packed;packed.insert(packed.end(),v.begin(),v.begin()+12);packed.insert(packed.end(),v.begin()+36,v.begin()+48);
    positions(decodeStaticZPrepassVertices(packed,positionOnly,12),packed,12,0);
    std::fill(v.begin(),v.end(),0);std::fill(d.begin(),d.end(),0);positions(out,originalV,36,0);
    // Follow declared position offset and row ordering; retain padding untouched.
    auto moved=declaration();for(size_t row=0;row<5;++row)put(moved,12*row,word(moved,12*row)+4);
    Bytes padded(80,0xFF);std::copy_n(originalV.begin(),36,padded.begin()+4);std::copy_n(originalV.begin()+36,36,padded.begin()+44);
    std::array<uint8_t,12> first{};std::copy_n(moved.begin(),12,first.begin());
    std::copy_n(moved.begin()+48,12,moved.begin());std::copy(first.begin(),first.end(),moved.begin()+48);
    positions(decodeStaticZPrepassVertices(padded,moved,40),padded,40,4);
}
void declarationGuards() {
    const auto d=declaration(),v=vertices();
    for(size_t row=0;row<5;++row) {
        for(const auto& change:std::array<std::array<uint32_t,2>,5>{{
                {0,1},{0,36},{0,0x00010000},{4,0},{8,0x01000000}}}) {
            auto bad=d;put(bad,row*12+change[0],change[1]);
            rejects([&]{decodeStaticZPrepassVertices(v,bad,36);},"Malformed consumed/dead row accepted");
        }
        auto duplicate=d;duplicate.insert(duplicate.end()-12,d.begin()+row*12,d.begin()+(row+1)*12);
        rejects([&]{decodeStaticZPrepassVertices(v,duplicate,36);},"Duplicate semantic accepted");
    }
    auto missing=d;missing.erase(missing.begin(),missing.begin()+12);
    rejects([&]{decodeStaticZPrepassVertices(v,missing,36);},"Missing position0 accepted");
    for(uint32_t semantic:{0x00000100u,0x00050200u,0x000A0100u,0x00010000u,0x00020000u}) {
        auto bad=d;put(bad,8,semantic);rejects([&]{decodeStaticZPrepassVertices(v,bad,36);},"Unqualified semantic accepted");
    }
    for(const auto& change:std::array<std::array<uint32_t,2>,4>{{
            {60,0x00FF0004},{64,0},{68,0x01000000},{68,1}}}) {
        auto bad=d;put(bad,change[0],change[1]);rejects([&]{decodeStaticZPrepassVertices(v,bad,36);},"Malformed terminator accepted");
    }
    auto early=d;std::copy_n(d.end()-12,12,early.begin()+12);
    rejects([&]{decodeStaticZPrepassVertices(v,early,36);},"Early terminator accepted");
}
void tangentProfile() {
    // Exact live016 seven-row declaration; first/last captured position words.
    constexpr std::array<Row,7> rows={{{0,0x002A23B9,0},{12,0x002A2187,0x00030000},
        {16,0x002A2187,0x00060000},{20,0x00182886,0x000A0000},
        {24,0x002C23A5,0x00050000},{32,0x002C23A5,0x00050100},{0x00FF0000,UINT32_MAX,0}}};
    Bytes d(rows.size()*12),v(80);
    for(size_t row=0;row<rows.size();++row)for(size_t lane=0;lane<3;++lane)put(d,12*row+4*lane,rows[row][lane]);
    for(size_t at=0;at<v.size();at+=4)put(v,at,0x7FC12345);
    constexpr std::array<uint32_t,6> p={0x41B1013B,0x408A1062,0xC1EC0B0F,0x41DD199A,0x408A1D7E,0xC20912F2};
    for(size_t i=0;i<2;++i)for(size_t lane=0;lane<3;++lane)put(v,40*i+4*lane,p[3*i+lane]);
    positions(decodeStaticZPrepassVertices(v,d,40),v,40,0);
    // NaN/Inf/arbitrary packed tangent bits cannot change any native attribute.
    for(uint32_t poison:{0x7F800000u,0xFF800000u,0xFFFFFFFFu,0x80000000u}) {
        auto changed=v;put(changed,16,poison);put(changed,56,poison);
        positions(decodeStaticZPrepassVertices(changed,d,40),v,40,0);
        need(changed[16]==uint8_t(poison>>24),"Decoder modified tangent payload");
    }
    for(const auto& change:std::array<std::array<uint32_t,2>,7>{{
            {24,0x00010010},{24,17},{24,40},{28,0x002A23B9},{28,0x002C23A5},{32,0x01060000},{32,0x00060100}}}) {
        auto bad=d;put(bad,change[0],change[1]);
        rejects([&]{decodeStaticZPrepassVertices(v,bad,40);},"Unqualified tangent format/stream/method/index/bounds accepted");
    }
    auto duplicate=d;duplicate.insert(duplicate.end()-12,d.begin()+24,d.begin()+36);
    rejects([&]{decodeStaticZPrepassVertices(v,duplicate,40);},"Duplicate packed tangent accepted");
}
void finiteAndExtents() {
    const auto d=declaration(),v=vertices();
    for(size_t lane=0;lane<3;++lane)for(uint32_t invalid:{0x7FC12345u,0x7F800000u,0xFF800000u}) {
        auto bad=v;put(bad,4*lane,invalid);rejects([&]{decodeStaticZPrepassVertices(bad,d,36);},"Nonfinite position accepted");
    }
    for(uint32_t stride:{0u,8u,35u,1024u})
        rejects([&]{decodeStaticZPrepassVertices(v,d,stride);},"Invalid stride accepted");
    for(size_t size:std::array<size_t,4>{0,1,35,73}) {
        auto bad=v;bad.resize(size);rejects([&]{decodeStaticZPrepassVertices(bad,d,36);},"Empty or partial vertex accepted");
    }
    for(size_t size:std::array<size_t,5>{0,12,60,71,66*12}) {
        auto bad=d;bad.resize(size);rejects([&]{decodeStaticZPrepassVertices(v,bad,36);},"Invalid declaration extent accepted");
    }
    Bytes largeOwner(size_t(65536)*36);
    for(size_t at=0;at<largeOwner.size();at+=36)std::copy_n(v.begin(),36,largeOwner.begin()+at);
    const auto ownerBefore=largeOwner;
    const auto large=decodeStaticZPrepassVertices(largeOwner,d,36);
    need(large.size()==65536&&large.front().position==large.back().position,
         "Valid original65536 Z-prepass owner rejected or decoded incorrectly");
    need(largeOwner==ownerBefore,"Large Z-prepass decoder changed its owner");
}
// Decoder-only ownership/bit controls; no cache, GPU or timing changes.
void staticPositionCase(std::span<const uint8_t> bytes,std::span<const uint8_t> decl,
    uint32_t stride,size_t count,uint32_t offset,const char* failure=nullptr) {
    struct RestoreCSR {uint32_t saved=_mm_getcsr();~RestoreCSR(){_mm_setcsr(saved);}} restore;
    const Bytes beforeBytes(bytes.begin(),bytes.end()),beforeDeclaration(decl.begin(),decl.end());
    std::array<uint32_t,36> states{};size_t next{};
    for(uint32_t round=0;round<4;++round)for(uint32_t flush=0;flush<4;++flush)for(uint32_t sticky:{0u,0x3Fu})
        states[next++]=0x1F80u|(round<<13)|((flush&1u)?0x40u:0u)|((flush&2u)?0x8000u:0u)|sticky;
    for(uint32_t round=0;round<4;++round)states[next++]=round<<13;
    need(next==states.size(),"Static position host-state inventory differs");
    for(uint32_t control:states) {
        _mm_setcsr(control);bool rejected{};
        try {
            const auto actual=decodeStaticZPrepassVertices(bytes,decl,stride);
            const auto afterCSR=_mm_getcsr();
            need(afterCSR==control,"Static position decode changed full MXCSR");
            need(!failure&&actual.size()==count,"Static position success/count differs");
            for(size_t vertex=0;vertex<actual.size();++vertex) {
                for(size_t lane=0;lane<3;++lane) {
                    const size_t at=vertex*stride+offset+4*lane;
                    const uint32_t gold=uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|
                        uint32_t(bytes[at+2])<<8|bytes[at+3];
                    need(std::bit_cast<uint32_t>(actual[vertex].position[lane])==gold,
                         "Static position ordered BE bits differ");
                }
                inert(actual[vertex]);
            }
        } catch(const Simpsons::Failure& error) {
            const auto afterCSR=_mm_getcsr();rejected=true;
            need(afterCSR==control,"Rejected static position decode changed full MXCSR");
            need(failure&&std::strcmp(error.what(),failure)==0,"Static position exact Failure message differs");
        }
        need(rejected==(failure!=nullptr),"Static position expected failure was absent");
        need(std::equal(bytes.begin(),bytes.end(),beforeBytes.begin())&&
             std::equal(decl.begin(),decl.end(),beforeDeclaration.begin()),"Static position decode changed borrowed inputs");
    }
}
void staticPositionOwnedBounds() {
    const auto d=declaration(),full=vertices();
    constexpr std::array<uint32_t,12> bits={0u,0x80000000u,1u,0x80000001u,0x007FFFFFu,0x807FFFFFu,
        0x00800000u,0x80800000u,0x7F7FFFFFu,0xFF7FFFFFu,0x3E000001u,0xC1480000u};
    for(size_t rotation=0;rotation<bits.size();++rotation) {
        Bytes source(72,0xFF); // NaN poison in every dead original attribute.
        for(size_t vertex=0;vertex<2;++vertex)for(size_t lane=0;lane<3;++lane)
            put(source,36*vertex+4*lane,bits[(rotation+3*vertex+lane)%bits.size()]);
        staticPositionCase(source,d,36,2,0);
    }
    // Two owned XYZ records end at48. Trailing padding and the next partial
    // record's NaNs do not create a third addressable position.
    for(size_t padding=0;padding<=32;padding+=4) {
        Bytes source(48+padding,0xFF);
        std::copy_n(full.begin(),12,source.begin());std::copy_n(full.begin()+36,12,source.begin()+36);
        staticPositionCase(source,d,36,2,0);
    }
    Bytes third(84,0xFF);
    std::copy_n(full.begin(),12,third.begin());std::copy_n(full.begin()+36,12,third.begin()+36);
    staticPositionCase(third,d,36,0,0,"Nonfinite static Z-prepass position");
    auto moved=d;for(size_t row=0;row<5;++row)put(moved,12*row,word(moved,12*row)+4);
    Bytes movedSource(56,0xFF);
    std::copy_n(full.begin(),12,movedSource.begin()+4);std::copy_n(full.begin()+36,12,movedSource.begin()+44);
    staticPositionCase(movedSource,moved,40,2,4);
    auto incomplete=movedSource;incomplete.resize(52);put(incomplete,44,0x7FC12345u);
    staticPositionCase(incomplete,moved,40,1,4);
    Bytes positionOnly(d.begin(),d.begin()+12);positionOnly.insert(positionOnly.end(),d.end()-12,d.end());
    Bytes shortOwner(8,0xFF);
    staticPositionCase(shortOwner,positionOnly,12,0,0,"Static Z-prepass vertex owner lacks its first consumed record");
    for(size_t vertex=0;vertex<2;++vertex)for(size_t lane=0;lane<3;++lane)
        for(uint32_t invalid:{0x7FC12345u,0x7F800000u,0xFF800000u}) {
            auto bad=movedSource;put(bad,4+40*vertex+4*lane,invalid);
            staticPositionCase(bad,moved,40,0,4,"Nonfinite static Z-prepass position");
        }
    auto poisoned=full;put(poisoned,0,0x7FC12345u);
    auto badTerminator=d;put(badTerminator,badTerminator.size()-4,1);
    staticPositionCase(poisoned,badTerminator,36,0,0,"Static Z-prepass declaration terminator differs");
    staticPositionCase(poisoned,badTerminator,0,0,0,"Static Z-prepass vertex stride or extent is invalid");
    staticPositionCase(d,d,36,2,0); // Source/declaration overlap is read-only.
    for(size_t residue=1;residue<=3;++residue) {
        Bytes unaligned(movedSource.size()+8,0xA5),unalignedDeclaration(moved.size()+8,0x5A);
        std::copy(movedSource.begin(),movedSource.end(),unaligned.begin()+residue);
        std::copy(moved.begin(),moved.end(),unalignedDeclaration.begin()+4-residue);
        const auto beforeV=unaligned,beforeD=unalignedDeclaration;
        staticPositionCase({unaligned.data()+residue,movedSource.size()},
            {unalignedDeclaration.data()+4-residue,moved.size()},40,2,4);
        need(unaligned==beforeV&&unalignedDeclaration==beforeD,"Unaligned static input sentinels changed");
    }
}
void staticPositionProtectedEnd() {
#ifdef _WIN32
    // The prior private static_positions_controls.h guard pattern, scoped to
    // these decoder cases. Source/declaration are read-only up to NOACCESS.
    struct Guard {
        uint8_t* base{};size_t page{},size{};
        explicit Guard(std::span<const uint8_t> bytes):size(bytes.size()) {
            SYSTEM_INFO info{};GetSystemInfo(&info);page=info.dwPageSize;
            need(page>=4096&&size<=page,"Static position guard extent differs");
            base=static_cast<uint8_t*>(VirtualAlloc(nullptr,2*page,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS));
            need(base!=nullptr,"Static position guard allocation failed");DWORD previous{};
            if(!VirtualProtect(base,page,PAGE_READWRITE,&previous)) {
                if(!VirtualFree(base,0,MEM_RELEASE))std::abort();
                base=nullptr;throw std::runtime_error("Static position guard writable admission failed");
            }
            std::memcpy(base+page-size,bytes.data(),size);
            if(!VirtualProtect(base,page,PAGE_READONLY,&previous)) {
                if(!VirtualFree(base,0,MEM_RELEASE))std::abort();
                base=nullptr;throw std::runtime_error("Static position guard read-only admission failed");
            }
        }
        Guard(const Guard&)=delete;Guard& operator=(const Guard&)=delete;
        ~Guard(){if(base&&!VirtualFree(base,0,MEM_RELEASE))std::abort();}
        std::span<const uint8_t> view()const noexcept{return {base+page-size,size};}
    };
    const auto d=declaration(),full=vertices();
    Bytes positionOnly(d.begin(),d.begin()+12);positionOnly.insert(positionOnly.end(),d.end()-12,d.end());
    Bytes one(full.begin(),full.begin()+12);Guard exactOne(one),exactDeclaration(positionOnly);
    staticPositionCase(exactOne.view(),exactDeclaration.view(),12,1,0);
    Bytes endOwned(full.begin(),full.begin()+48);Guard exactTwo(endOwned),exactFullDeclaration(d);
    staticPositionCase(exactTwo.view(),exactFullDeclaration.view(),36,2,0);
    for(size_t vertex=0;vertex<2;++vertex)for(size_t lane=0;lane<3;++lane)
        for(uint32_t invalid:{0x7FC12345u,0x7F800000u,0xFF800000u}) {
            auto bad=endOwned;put(bad,36*vertex+4*lane,invalid);Guard exactBad(bad);
            staticPositionCase(exactBad.view(),exactFullDeclaration.view(),36,0,0,"Nonfinite static Z-prepass position");
        }
    Bytes eight(full.begin(),full.begin()+8);Guard shortOwner(eight);
    staticPositionCase(shortOwner.view(),exactDeclaration.view(),12,0,0,
        "Static Z-prepass vertex owner lacks its first consumed record");
    staticPositionCase({exactOne.base+exactOne.page,0},exactDeclaration.view(),12,0,0,
        "Static Z-prepass vertex stride or extent is invalid");
#endif
}

// Exact raw-source cache coverage on the production cache implementation.
struct FakeMesh {
    uint32_t tag;
    size_t* destroyed{};
    ~FakeMesh(){if(destroyed)++*destroyed;}
};
using SourceCache=Simpsons::StaticMeshSourceCache<FakeMesh>;
Bytes indices() {
    Bytes result(12);
    for(size_t i=0;i<result.size();++i)result[i]=uint8_t(i);
    return result;
}
uint64_t sourceKey(const Bytes& v,const Bytes& i,const Bytes& d,uint32_t stride) {
    return SourceCache::contentKey(v,i,d,stride);
}
std::shared_ptr<FakeMesh> sourceMesh(uint32_t tag) {return std::make_shared<FakeMesh>(FakeMesh{tag,nullptr});}
void sourceCacheExactHitSharesMesh() {
    SourceCache cache;
    const auto v=vertices(),i=indices(),d=declaration();
    constexpr uint32_t stride=36;
    const auto key=sourceKey(v,i,d,stride);
    need(!cache.find(v,i,d,stride,key),"Empty source cache reported a hit");
    const auto mesh=sourceMesh(7);
    cache.insert(v,i,d,stride,key,mesh);
    need(cache.size()==1,"Valid source was not retained");
    // Identical bytes in different vectors hit by content, never by address,
    // and share the inserted mesh.
    Bytes v2=v,i2=i,d2=d;
    const auto hit=cache.find(v2,i2,d2,stride,sourceKey(v2,i2,d2,stride));
    need(bool(hit),"Exact source bytes must hit");
    need(hit.get()==mesh.get(),"Exact source bytes must share the cached mesh");
}
void sourceCachePlaneAndStrideMiss() {
    SourceCache cache;
    const auto v=vertices(),i=indices(),d=declaration();
    constexpr uint32_t stride=36;
    cache.insert(v,i,d,stride,sourceKey(v,i,d,stride),sourceMesh(1));
    need(cache.size()==1,"Valid source was not retained");
    auto mutated=v;mutated[0]^=0xFF;
    need(!cache.find(mutated,i,d,stride,sourceKey(mutated,i,d,stride)),"Vertex mutation must miss");
    auto mutatedI=i;mutatedI[0]^=0xFF;
    need(!cache.find(v,mutatedI,d,stride,sourceKey(v,mutatedI,d,stride)),"Index mutation must miss");
    auto mutatedD=d;mutatedD[0]^=0xFF;
    need(!cache.find(v,i,mutatedD,stride,sourceKey(v,i,mutatedD,stride)),"Declaration mutation must miss");
    need(!cache.find(v,i,d,stride+4,sourceKey(v,i,d,stride+4)),"Stride change must miss");
    auto truncated=v;truncated.pop_back();
    need(!cache.find(truncated,i,d,stride,sourceKey(truncated,i,d,stride)),"Extent change must miss");
    need(cache.size()==1,"Misses must not insert");
}
void sourceCacheSnapshotsSurviveCallerMutation() {
    SourceCache cache;
    auto v=vertices(),i=indices(),d=declaration();
    constexpr uint32_t stride=36;
    const auto key=sourceKey(v,i,d,stride);
    const auto mesh=sourceMesh(3);
    cache.insert(v,i,d,stride,key,mesh);
    const auto pristineV=v,pristineI=i,pristineD=d;
    v[8]^=0x01;i[2]^=0x01;d[4]^=0x01;
    // Snapshots own their bytes: pristine content still hits after the caller
    // mutates its buffers, while the mutated bytes miss.
    const auto hit=cache.find(pristineV,pristineI,pristineD,stride,sourceKey(pristineV,pristineI,pristineD,stride));
    need(hit&&hit.get()==mesh.get(),"Snapshot must survive caller mutation");
    need(!cache.find(v,i,d,stride,sourceKey(v,i,d,stride)),"Mutated caller bytes must miss");
}
void sourceCacheForcedHashRejects() {
    SourceCache cache;
    const auto v=vertices(),i=indices(),d=declaration();
    constexpr uint32_t stride=36;
    cache.insert(v,i,d,stride,sourceKey(v,i,d,stride),sourceMesh(5));
    // A forced identical reject key with any differing byte, extent or stride
    // still misses: there is no hash-only hit.
    const auto forced=sourceKey(v,i,d,stride);
    auto otherV=v;otherV.back()^=0xFF;
    need(!cache.find(otherV,i,d,stride,forced),"Forced hash must not mask vertex bytes");
    auto otherI=i;otherI.back()^=0xFF;
    need(!cache.find(v,otherI,d,stride,forced),"Forced hash must not mask index bytes");
    auto otherD=d;otherD.back()^=0xFF;
    need(!cache.find(v,i,otherD,stride,forced),"Forced hash must not mask declaration bytes");
    need(!cache.find(v,i,d,stride+4,forced),"Forced hash must not mask stride");
    auto longer=v;longer.push_back(0);
    need(!cache.find(longer,i,d,stride,forced),"Forced hash must not mask extent");
}
void sourceCacheEviction() {
    const auto i=indices(),d=declaration();
    constexpr uint32_t stride=36;
    SourceCache cache(2,1024u*1024u);
    const auto entry=[&](uint32_t bits,uint32_t tag) {
        auto v=vertices();put(v,0,bits);
        const auto key=sourceKey(v,i,d,stride);
        cache.insert(v,i,d,stride,key,sourceMesh(tag));
        return std::make_tuple(v,key);
    };
    const auto [a,aKey]=entry(0x3F800000u,11);
    const auto [b,bKey]=entry(0x40000000u,12);
    need(cache.size()==2,"Small cache must hold two entries");
    need(bool(cache.find(a,i,d,stride,aKey)),"Inserted entry must be resident");
    // Promote A, then insert C: least-recently-used B is evicted, A stays.
    const auto [c,cKey]=entry(0x40400000u,13);
    need(cache.size()==2,"Entry cap must evict");
    need(bool(cache.find(a,i,d,stride,aKey)),"Promoted entry must survive eviction");
    need(!cache.find(b,i,d,stride,bKey),"LRU entry must be evicted");
    need(bool(cache.find(c,i,d,stride,cKey)),"Newest entry must be resident");
    // Byte budget: an entry that alone exceeds it is not retained.
    SourceCache tiny(512,32);
    tiny.insert(vertices(),indices(),declaration(),stride,
        sourceKey(vertices(),indices(),declaration(),stride),sourceMesh(14));
    need(tiny.size()==0&&tiny.residentBytes()==0,"Oversized entry must not be retained");
    // Residency accounting matches the retained snapshot bytes.
    need(cache.residentBytes()==2*(vertices().size()+i.size()+d.size()),"Residency accounting differs");
}
// Production byte-range equality coverage on the actual shared helper.
// The scalar oracle below is a plain byte loop, not a mirror of the SSE2
// implementation; agreement proves the helper compares every byte exactly.
bool scalarBytesEqual(const uint8_t* a,const uint8_t* b,size_t n) {
    if(n==0)return true;
    for(size_t i=0;i<n;++i)if(a[i]!=b[i])return false;
    return true;
}
void byteRangeEqualMatchesScalar() {
    // Absolute-index patterns mostly disagree for offA!=offB (useful unequal
    // coverage): agreement with the scalar oracle on both outcomes.
    constexpr size_t kMaxLength=257,kMaxOffset=15;
    Bytes backingA(kMaxOffset+kMaxLength+1),backingB(kMaxOffset+kMaxLength+1);
    for(size_t k=0;k<backingA.size();++k) {
        backingA[k]=static_cast<uint8_t>((k*31+7)&0xFF);backingB[k]=backingA[k];
    }
    for(size_t len=0;len<=kMaxLength;++len)
        for(size_t offA=0;offA<=kMaxOffset;++offA)
            for(size_t offB=0;offB<=kMaxOffset;++offB) {
                const uint8_t* pA=backingA.data()+offA;
                const uint8_t* pB=backingB.data()+offB;
                need(Simpsons::ByteRangesEqual(pA,pB,len)==scalarBytesEqual(pA,pB,len),
                    "Helper disagrees with scalar reference");
            }
    // Equal relative-index content at every independent offset pair, so the
    // vector equal path at all alignments is covered; then a mid-range
    // mismatch exercised against the scalar reference, then restore.
    for(size_t len=0;len<=kMaxLength;++len)
        for(size_t offA=0;offA<=kMaxOffset;++offA)
            for(size_t offB=0;offB<=kMaxOffset;++offB) {
                for(size_t i=0;i<len;++i) {
                    const uint8_t v=static_cast<uint8_t>((i*131+17)&0xFF);
                    backingA[offA+i]=v;backingB[offB+i]=v;
                }
                const uint8_t* pA=backingA.data()+offA;
                const uint8_t* pB=backingB.data()+offB;
                need(Simpsons::ByteRangesEqual(pA,pB,len),"Helper must match equal content at independent offsets");
                need(scalarBytesEqual(pA,pB,len),"Scalar oracle must match equal content");
                if(len>size_t(0)) {
                    backingA[offA+len/size_t(2)]^=0xFF;
                    need(!Simpsons::ByteRangesEqual(pA,pB,len),"Helper missed mismatch vs scalar reference");
                    need(!scalarBytesEqual(pA,pB,len),"Scalar oracle missed mismatch");
                    backingA[offA+len/size_t(2)]^=0xFF;
                    need(Simpsons::ByteRangesEqual(pA,pB,len),"Helper differs after restore");
                }
            }
}
void byteRangeEqualEveryByteMismatch() {
    // A mismatch at every byte position of representative block/tail/boundary
    // lengths must report unequal, and restoring the byte must report equal.
    // Fixed misaligned independent offsets (3,11) exercise unaligned loads.
    constexpr size_t kMaxLength=257;
    Bytes backingA(16+kMaxLength+1),backingB(16+kMaxLength+1);
    for(size_t k=0;k<backingA.size();++k) {
        backingA[k]=static_cast<uint8_t>((k*31+7)&0xFF);backingB[k]=backingA[k];
    }
    const size_t lengths[]={1,2,3,7,8,9,15,16,17,24,31,32,33,47,48,49,55,56,57,
        63,64,65,71,72,73,80,95,96,97,111,112,113,127,128,129,143,144,159,160,
        191,192,193,207,208,223,224,239,240,241,255,256,257};
    const size_t offsets[][2]={{3,11},{11,3}};
    for(size_t len:lengths)for(const auto& pair:offsets) {
        // Equal relative-index content per offset pair/length: the spans must
        // compare equal before any per-byte mutation.
        uint8_t* pA=backingA.data()+pair[0];
        uint8_t* pB=backingB.data()+pair[1];
        for(size_t i=0;i<len;++i) {
            const uint8_t v=static_cast<uint8_t>((i*31+7)&0xFF);
            pA[i]=v;pB[i]=v;
        }
        need(Simpsons::ByteRangesEqual(pA,pB,len),"Helper must match identical ranges");
        for(size_t pos=0;pos<len;++pos) {
            pA[pos]^=0xFF;
            need(!Simpsons::ByteRangesEqual(pA,pB,len),"Helper missed differing byte");
            pA[pos]^=0xFF;
            need(Simpsons::ByteRangesEqual(pA,pB,len),"Helper differs after restore");
        }
    }
}
void byteRangeEqualIgnoresTrailingBytes() {
    // Bytes immediately past the compared size must not be observed: equal
    // prefixes with differing trailers compare equal (no same-page overread),
    // while a differing prefix byte still compares unequal.
    const size_t lengths[]={1,2,7,8,9,15,16,17,31,32,33,63,64,65,127,128,129,255,256,257};
    for(size_t len:lengths) {
        Bytes a(len+32),b(len+32);
        for(size_t k=0;k<len;++k) {
            a[k]=static_cast<uint8_t>((k*17+3)&0xFF);b[k]=a[k];
        }
        for(size_t k=0;k<32;++k) {
            a[len+k]=static_cast<uint8_t>(k&0xFF);
            b[len+k]=static_cast<uint8_t>((~k)&0xFF);
        }
        need(Simpsons::ByteRangesEqual(a.data(),b.data(),len),"Helper must ignore bytes past size");
        need(Simpsons::ByteRangesEqual(b.data(),a.data(),len),"Helper must ignore bytes past size (swapped)");
        b[len-1]^=0xFF;
        need(!Simpsons::ByteRangesEqual(a.data(),b.data(),len),"Helper missed prefix difference");
        b[len-1]^=0xFF;
        need(Simpsons::ByteRangesEqual(a.data(),b.data(),len),"Helper differs after restore");
    }
}
void byteRangeEqualGuardPages() {
#ifdef _WIN32
    // Real guard-page-end allocations: each probe range ends exactly at a
    // NOACCESS page, so any read past either range faults. Equal ranges must
    // match and mismatches must miss in both operand orders without faulting.
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const size_t page=info.dwPageSize;
    need(page>=size_t(4096),"Unexpected system page size");
    const size_t usableBytes=2*page;
    uint8_t* base=static_cast<uint8_t*>(
        VirtualAlloc(nullptr,usableBytes+page,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    need(base!=nullptr,"Guard-page allocation failed");
    DWORD previous=0;
    need(VirtualProtect(base+usableBytes,page,PAGE_NOACCESS,&previous)!=0,
        "Guard-page protection failed");
    const size_t lengths[]={1,15,16,17,63,64,65,255,256,257,1024,4096};
    const size_t edges[]={15,16,31,32,63,64,127,128,255,256};
    for(size_t len:lengths) {
        need(len<=usableBytes,"Guard fixture smaller than probe length");
        uint8_t* guarded=base+usableBytes-len;
        for(size_t i=0;i<len;++i)guarded[i]=static_cast<uint8_t>((i*131+17)&0xFF);
        Bytes heap(len);
        for(size_t i=0;i<len;++i)heap[i]=guarded[i];
        need(Simpsons::ByteRangesEqual(guarded,heap.data(),len),"Guard-backed equal bytes must match");
        need(Simpsons::ByteRangesEqual(heap.data(),guarded,len),"Guard-backed equal bytes must match (swapped)");
        std::vector<size_t> positions;
        positions.push_back(size_t(0));
        if(len>size_t(1))positions.push_back(len-1);
        for(size_t edge:edges)
            if(edge<len&&edge!=size_t(0)&&edge!=len-1)positions.push_back(edge);
        for(size_t pos:positions) {
            guarded[pos]^=0xFF;
            need(!Simpsons::ByteRangesEqual(guarded,heap.data(),len),"Guard-backed difference must miss");
            need(!Simpsons::ByteRangesEqual(heap.data(),guarded,len),"Guard-backed difference must miss (swapped)");
            guarded[pos]^=0xFF;
            need(Simpsons::ByteRangesEqual(guarded,heap.data(),len),"Guard-backed bytes must match after restore");
        }
    }
    // Empty ranges permit null pointers and never dereference, even when a
    // pointer sits at the start of the NOACCESS page itself, both orders.
    need(Simpsons::ByteRangesEqual(nullptr,nullptr,0),"Empty range must accept null pointers");
    need(Simpsons::ByteRangesEqual(base,nullptr,0),"Empty range must not dereference");
    need(Simpsons::ByteRangesEqual(nullptr,base,0),"Empty range must not dereference (swapped)");
    need(Simpsons::ByteRangesEqual(base+usableBytes,nullptr,0),"Empty range at guard page must not dereference");
    need(Simpsons::ByteRangesEqual(nullptr,base+usableBytes,0),"Empty range at guard page must not dereference (swapped)");
    need(Simpsons::ByteRangesEqual(base+usableBytes,base+usableBytes,0),"Empty range at guard page must accept");
    need(VirtualFree(base,0,MEM_RELEASE)!=0,"Guard-page release failed");
#endif
}
void sourceLookupEmptyAndOneEntry() {
    // Empty cache: lookup misses but still returns the current content key;
    // residency stays bounded.
    SourceCache cache;
    const auto v=vertices(),i=indices(),d=declaration();
    constexpr uint32_t stride=36;
    auto empty=cache.lookup(v,i,d,stride);
    need(!empty.mesh,"Empty lookup must miss");
    need(empty.key==sourceKey(v,i,d,stride),"Empty lookup must return the current content key");
    need(cache.size()==0&&cache.residentBytes()==0,"Empty lookup must not insert");
    // One entry: first lookup misses, insert, second lookup hits with object
    // identity and the correct key. The single-node promote guard stays safe:
    // repeated hits keep working and misses stay misses.
    const auto mesh=sourceMesh(21);
    need(!cache.lookup(v,i,d,stride).mesh,"One-entry cache must miss before insert");
    cache.insert(v,i,d,stride,sourceKey(v,i,d,stride),mesh);
    need(cache.size()==1,"One-entry insert must be retained");
    auto hit=cache.lookup(v,i,d,stride);
    need(bool(hit.mesh),"One-entry lookup must hit");
    need(hit.mesh.get()==mesh.get(),"One-entry lookup must share the cached mesh");
    need(hit.key==sourceKey(v,i,d,stride),"One-entry hit must return the entry key");
    auto hitAgain=cache.lookup(v,i,d,stride);
    need(hitAgain.mesh.get()==mesh.get()&&hitAgain.key==hit.key,"Repeated one-entry hits must stay identical");
    auto mutated=v;mutated[0]^=0xFF;
    auto miss=cache.lookup(mutated,i,d,stride);
    need(!miss.mesh,"One-entry mutated lookup must miss");
    need(miss.key==sourceKey(mutated,i,d,stride),"One-entry miss must return the current content key");
    need(cache.size()==1,"Misses must not insert");
    auto restored=cache.lookup(v,i,d,stride);
    need(restored.mesh.get()==mesh.get(),"Entry must still hit after a miss");
    // Zero-entry bound: inserts are dropped, lookups always miss but bounded.
    SourceCache zero(0,1024u*1024u);
    auto zeroMiss=zero.lookup(v,i,d,stride);
    need(!zeroMiss.mesh,"Zero-entry lookup must miss");
    zero.insert(v,i,d,stride,zeroMiss.key,sourceMesh(22));
    need(zero.size()==0&&zero.residentBytes()==0,"Zero-entry insert must not be retained");
}
void sourceLookupRepeatedCycleIdenticalExtents() {
    // Several distinct meshes with identical extents: repeated complete LRU
    // cycles hit with object identity and correct keys, and LRU eviction is
    // preserved (promoted entries survive, least-recently-used is evicted).
    constexpr uint32_t stride=36;
    const auto baseV=vertices(),baseI=indices(),baseD=declaration();
    constexpr size_t kVariants=4;
    std::array<Bytes,kVariants> vs,idx,decl;
    std::array<std::shared_ptr<FakeMesh>,kVariants> meshes;
    std::array<uint64_t,kVariants> keys;
    for(size_t k=0;k<kVariants;++k) {
        vs[k]=baseV;idx[k]=baseI;decl[k]=baseD;
        put(vs[k],0,0x3F800000u+uint32_t(k));
        put(vs[k],36,0x40000000u+uint32_t(k));
        idx[k][k%idx[k].size()]^=uint8_t(0x10+k);
        decl[k][k%decl[k].size()]^=uint8_t(0x20+k);
        need(vs[k].size()==baseV.size()&&idx[k].size()==baseI.size()&&decl[k].size()==baseD.size(),
            "Cycle variants must keep identical extents");
        meshes[k]=sourceMesh(uint32_t(30+k));
        keys[k]=sourceKey(vs[k],idx[k],decl[k],stride);
    }
    SourceCache cache;
    for(size_t k=0;k<kVariants;++k) {
        auto probe=cache.lookup(vs[k],idx[k],decl[k],stride);
        need(!probe.mesh,"Cycle variant must miss before insert");
        need(probe.key==keys[k],"Miss must return the current content key");
        cache.insert(vs[k],idx[k],decl[k],stride,probe.key,meshes[k]);
    }
    need(cache.size()==kVariants,"All cycle variants must be retained");
    // Two full cycles in insertion order: every hit shares the mesh object
    // and returns the entry key.
    for(size_t round=0;round<2;++round)
        for(size_t k=0;k<kVariants;++k) {
            auto hit=cache.lookup(vs[k],idx[k],decl[k],stride);
            need(bool(hit.mesh),"Repeated cycle must hit every variant");
            need(hit.mesh.get()==meshes[k].get(),"Repeated cycle must share the cached mesh object");
            need(hit.key==keys[k],"Repeated cycle hit must return the entry key");
            need(hit.key==sourceKey(vs[k],idx[k],decl[k],stride),"Repeated cycle key must match content key");
        }
    need(cache.size()==kVariants,"Cycles must not insert");
    need(cache.residentBytes()==kVariants*(baseV.size()+baseI.size()+baseD.size()),
        "Residency accounting differs after cycles");
    // LRU eviction preserved under the new path: promote A, then inserting D
    // evicts least-recently-used B while A/C/D stay resident.
    SourceCache evictCache(3,1024u*1024u);
    for(size_t k=0;k<3;++k) {
        auto probe=evictCache.lookup(vs[k],idx[k],decl[k],stride);
        need(!probe.mesh,"Small cache variant must miss before insert");
        evictCache.insert(vs[k],idx[k],decl[k],stride,probe.key,meshes[k]);
    }
    auto promoteA=evictCache.lookup(vs[0],idx[0],decl[0],stride);
    need(promoteA.mesh.get()==meshes[0].get(),"Promoted entry must hit");
    auto missD=evictCache.lookup(vs[3],idx[3],decl[3],stride);
    need(!missD.mesh,"New variant must miss before insert");
    evictCache.insert(vs[3],idx[3],decl[3],stride,missD.key,meshes[3]);
    need(evictCache.size()==3,"Entry cap must evict");
    need(bool(evictCache.lookup(vs[0],idx[0],decl[0],stride).mesh),"Promoted entry must survive eviction");
    need(bool(evictCache.lookup(vs[3],idx[3],decl[3],stride).mesh),"Newest entry must be resident");
    need(!evictCache.lookup(vs[1],idx[1],decl[1],stride).mesh,"LRU entry must be evicted");
    need(bool(evictCache.lookup(vs[2],idx[2],decl[2],stride).mesh),"Other entry must stay resident");
}
void sourceLookupPlaneMutationsMiss() {
    // Same extents but a change at the beginning/middle/tail of any plane or
    // stride must miss; the base content still hits afterwards.
    SourceCache cache;
    const auto v=vertices(),i=indices(),d=declaration();
    constexpr uint32_t stride=36;
    const auto mesh=sourceMesh(41);
    cache.insert(v,i,d,stride,sourceKey(v,i,d,stride),mesh);
    const std::array<size_t,3> vPos={0,v.size()/2,v.size()-1};
    const std::array<size_t,3> iPos={0,i.size()/2,i.size()-1};
    const std::array<size_t,3> dPos={0,d.size()/2,d.size()-1};
    for(size_t pos:vPos) {
        auto mutated=v;mutated[pos]^=0xFF;
        need(mutated.size()==v.size(),"Vertex mutation must keep extents");
        auto miss=cache.lookup(mutated,i,d,stride);
        need(!miss.mesh,"Vertex change must miss despite same extents");
        need(miss.key==sourceKey(mutated,i,d,stride),"Vertex miss must return the current content key");
    }
    for(size_t pos:iPos) {
        auto mutated=i;mutated[pos]^=0xFF;
        auto miss=cache.lookup(v,mutated,d,stride);
        need(!miss.mesh,"Index change must miss despite same extents");
        need(miss.key==sourceKey(v,mutated,d,stride),"Index miss must return the current content key");
    }
    for(size_t pos:dPos) {
        auto mutated=d;mutated[pos]^=0xFF;
        auto miss=cache.lookup(v,i,mutated,stride);
        need(!miss.mesh,"Declaration change must miss despite same extents");
        need(miss.key==sourceKey(v,i,mutated,stride),"Declaration miss must return the current content key");
    }
    auto strideMiss=cache.lookup(v,i,d,stride+4);
    need(!strideMiss.mesh,"Stride change must miss despite same extents");
    need(strideMiss.key==sourceKey(v,i,d,stride+4),"Stride miss must return the current content key");
    need(cache.size()==1,"Mutation misses must not insert");
    auto hit=cache.lookup(v,i,d,stride);
    need(hit.mesh.get()==mesh.get()&&hit.key==sourceKey(v,i,d,stride),
        "Base content must still hit with identity and key");
}
void sourceLookupSnapshotsSurviveCallerMutation() {
    // Snapshots own their bytes on the lookup path: mutating caller buffers
    // after insert misses, restoring them hits again with identity.
    SourceCache cache;
    auto v=vertices(),i=indices(),d=declaration();
    constexpr uint32_t stride=36;
    const auto mesh=sourceMesh(43);
    auto miss=cache.lookup(v,i,d,stride);
    need(!miss.mesh,"Snapshot fixture must miss before insert");
    cache.insert(v,i,d,stride,miss.key,mesh);
    const auto pristineV=v,pristineI=i,pristineD=d;
    const auto pristineKey=sourceKey(pristineV,pristineI,pristineD,stride);
    v[8]^=0x01;i[2]^=0x01;d[4]^=0x01;
    auto mutated=cache.lookup(v,i,d,stride);
    need(!mutated.mesh,"Mutated caller bytes must miss");
    need(mutated.key==sourceKey(v,i,d,stride),"Mutated miss must return the current content key");
    auto hit=cache.lookup(pristineV,pristineI,pristineD,stride);
    need(hit.mesh.get()==mesh.get(),"Pristine content must hit after caller mutation");
    need(hit.key==pristineKey,"Pristine hit must return the entry key");
    v=pristineV;i=pristineI;d=pristineD;
    auto restored=cache.lookup(v,i,d,stride);
    need(restored.mesh.get()==mesh.get()&&restored.key==pristineKey,
        "Restored caller bytes must hit with identity and key");
}
void sourceLookupOversizeAndEvictionBounded() {
    // Oversize entries are not retained and entry caps stay bounded on the
    // lookup path; residency matches retained snapshots.
    const auto v=vertices(),i=indices(),d=declaration();
    constexpr uint32_t stride=36;
    SourceCache tiny(512,32);
    auto tinyMiss=tiny.lookup(v,i,d,stride);
    need(!tinyMiss.mesh,"Oversize fixture must miss");
    tiny.insert(v,i,d,stride,tinyMiss.key,sourceMesh(51));
    need(tiny.size()==0&&tiny.residentBytes()==0,"Oversize entry must not be retained");
    need(!tiny.lookup(v,i,d,stride).mesh,"Oversize content must keep missing");
    SourceCache capped(2,1024u*1024u);
    const auto entry=[&](uint32_t bits,uint32_t tag) {
        auto vv=vertices();put(vv,0,bits);
        auto probe=capped.lookup(vv,i,d,stride);
        if(!probe.mesh)capped.insert(vv,i,d,stride,probe.key,sourceMesh(tag));
        return vv;
    };
    const auto a=entry(0x3F800000u,52);
    const auto b=entry(0x40000000u,53);
    need(capped.size()==2,"Capped cache must hold two entries");
    need(bool(capped.lookup(a,i,d,stride).mesh),"Inserted entry must be resident");
    const auto c=entry(0x40400000u,54);
    need(capped.size()==2,"Entry cap must evict");
    need(bool(capped.lookup(a,i,d,stride).mesh),"Promoted entry must survive eviction");
    need(!capped.lookup(b,i,d,stride).mesh,"LRU entry must be evicted");
    need(bool(capped.lookup(c,i,d,stride).mesh),"Newest entry must be resident");
    need(capped.residentBytes()==2*(v.size()+i.size()+d.size()),"Residency accounting differs");
}
void sourceCacheForcedHashBoundaries() {
    // Strengthened forced-collision coverage: differences at 16-byte block
    // boundaries, 64-byte unroll boundaries and scalar-tail positions must
    // still miss under a forced identical reject key.
    SourceCache cache;
    const auto v=vertices(),i=indices(),d=declaration();
    constexpr uint32_t stride=36;
    cache.insert(v,i,d,stride,sourceKey(v,i,d,stride),sourceMesh(5));
    const auto forced=sourceKey(v,i,d,stride);
    for(size_t pos:{size_t(0),size_t(15),size_t(16),size_t(17),size_t(31),size_t(32),
            size_t(33),size_t(63),size_t(64),size_t(65),size_t(71)}) {
        need(pos<v.size(),"Vertex boundary fixture out of range");
        auto other=v;other[pos]^=0xFF;
        need(!cache.find(other,i,d,stride,forced),"Forced hash must not mask vertex boundary bytes");
    }
    for(size_t pos:{size_t(0),size_t(11)}) {
        need(pos<i.size(),"Index boundary fixture out of range");
        auto other=i;other[pos]^=0xFF;
        need(!cache.find(v,other,d,stride,forced),"Forced hash must not mask index boundary bytes");
    }
    for(size_t pos:{size_t(0),size_t(15),size_t(16),size_t(17),size_t(63),size_t(64),size_t(71)}) {
        need(pos<d.size(),"Declaration boundary fixture out of range");
        auto other=d;other[pos]^=0xFF;
        need(!cache.find(v,i,other,stride,forced),"Forced hash must not mask declaration boundary bytes");
    }
}
void sourceWatchObservationsPreserveResults() {
    struct WatchedSource {
        std::array<uint8_t,3*4096> bytes{};
        std::array<std::atomic<uint8_t>,3> armed{};
        std::array<std::atomic<uint32_t>,3> version{};
        ~WatchedSource() {
            for(uint32_t page=0;page<3;++page)
                if(armed[page].exchange(0))Simpsons::guestWatchBlockAdjust(page,-1);
        }
        void stored(uint32_t page) {
            if(armed[page].exchange(0))Simpsons::guestWatchBlockAdjust(page,-1);
            version[page].fetch_add(1);
        }
    } source;
    const auto v=vertices(),i=indices(),d=declaration();
    std::copy(v.begin(),v.end(),source.bytes.begin());
    std::copy(i.begin(),i.end(),source.bytes.begin()+4096);
    std::copy(d.begin(),d.end(),source.bytes.begin()+8192);
    const std::span<const uint8_t> vv(source.bytes.data(),v.size());
    const std::span<const uint8_t> ii(source.bytes.data()+4096,i.size());
    const std::span<const uint8_t> dd(source.bytes.data()+8192,d.size());
    SourceCache cache(2,1024*1024);
    cache.setWriteWatch({source.bytes.data(),source.armed.data(),source.version.data()},true);
    const auto original=sourceMesh(70);
    auto first=cache.lookup(vv,ii,dd,36);
    need(!first.mesh&&first.pending.valid,"Initial watched source must miss and arm before capture");
    cache.insert(vv,ii,dd,36,first.key,original,&first.pending);
    need(cache.lookup(vv,ii,dd,36).mesh==original,"Unchanged watched source lost its exact mesh");
    need(cache.watchHits()==1&&cache.statistics().fullLookups==1,"Watch observations confused fast/full lookups");
    source.stored(0);source.bytes[0]^=1;
    auto changed=cache.lookup(vv,ii,dd,36);
    need(!changed.mesh&&cache.statistics().dirtyBindings==1,"Changed watched bytes reused stale mesh");
    const auto replacement=sourceMesh(71);
    cache.insert(vv,ii,dd,36,changed.key,replacement,&changed.pending);
    need(cache.lookup(vv,ii,dd,36).mesh==replacement,"Changed watched bytes did not bind replacement");
    source.stored(1); // Same bytes still require full equality after a store.
    need(cache.lookup(vv,ii,dd,36).mesh==replacement,"Same-byte store changed the exact result");
    need(cache.statistics().dirtyBindings==2&&cache.statistics().exactHits==1,
         "Same-byte invalidation must count as a full exact hit");
    for(uint64_t n=0;n<2*Simpsons::kStaticMeshSourceCacheRevalidateLookups&&
            !cache.statistics().revalidations;++n)
        need(cache.lookup(vv,ii,dd,36).mesh==replacement,"Bounded revalidation changed exact mesh identity");
    const auto& stats=cache.statistics();
    need(stats.revalidations==1&&stats.exactHits==2&&stats.misses==2,
         "Expired watched binding did not perform its full comparison");
    need(stats.lookups==cache.watchHits()+stats.fullLookups&&stats.fullLookups==stats.exactHits+stats.misses,
         "Source lookup observations do not partition actual results");
    need(stats.insertions==2&&!stats.evictions&&stats.peakEntries==2&&stats.peakBytes==cache.residentBytes(),
         "Source observations changed bounded residency");
    const auto beforeClear=stats.lookups;
    cache.clear();
    need(!cache.size()&&!cache.residentBytes()&&cache.statistics().lookups==beforeClear,
         "Clearing residency must preserve lifetime observations");
    need(!cache.lookup(vv,ii,dd,36).mesh,"Cleared source retained a stale watched binding");
    SourceCache tiny(2,32);
    tiny.insert(v,i,d,36,sourceKey(v,i,d,36),original);
    need(tiny.statistics().oversizedSkips==1&&!tiny.statistics().insertions&&!tiny.residentBytes(),
         "Oversized source observation changed rejection or residency");
}

struct OwnedSources {
    std::array<Bytes,3> planes{vertices(),indices(),declaration()};
    explicit OwnedSources(uint32_t bits=0x3E000001u) {
        put(planes[0],0,bits);
        // Logical bytes fit small budgets; spare retained capacity must still
        // be charged. Observe actual capacities instead of assuming growth.
        for(size_t plane=0;plane<3;++plane)planes[plane].reserve(planes[plane].size()+97+31*plane);
    }
    size_t logical() const noexcept {return planes[0].size()+planes[1].size()+planes[2].size();}
    size_t charge() const noexcept {return planes[0].capacity()+planes[1].capacity()+planes[2].capacity();}
    uint64_t key(uint64_t stride=36) const noexcept {
        return SourceCache::contentKey(planes[0],planes[1],planes[2],stride);
    }
};
struct OwnedSourceSnapshot {
    std::array<Bytes,3> bytes;
    std::array<const uint8_t*,3> pointers;
    std::array<size_t,3> capacities;
    explicit OwnedSourceSnapshot(const OwnedSources& sources):bytes(sources.planes) {
        for(size_t plane=0;plane<3;++plane) {
            pointers[plane]=sources.planes[plane].data();
            capacities[plane]=sources.planes[plane].capacity();
        }
    }
    size_t logical() const noexcept {return bytes[0].size()+bytes[1].size()+bytes[2].size();}
    size_t charge() const noexcept {return capacities[0]+capacities[1]+capacities[2];}
};
void untouchedOwnedSources(const OwnedSources& sources,const OwnedSourceSnapshot& before) {
    for(size_t plane=0;plane<3;++plane)
        need(sources.planes[plane]==before.bytes[plane]&&sources.planes[plane].data()==before.pointers[plane]&&
             sources.planes[plane].capacity()==before.capacities[plane],"Rejected owned source changed bytes, pointer or capacity");
}
void transferredOwnedSources(const SourceCache& cache,const OwnedSources& sources,
                             const OwnedSourceSnapshot& before,const FakeMesh* mesh) {
    const auto entry=Simpsons::StaticMeshSourceCacheTestProbe::entry(cache,mesh);
    need(entry.found&&entry.charge==before.charge(),"Owned entry missing or charged by logical size");
    for(size_t plane=0;plane<3;++plane) {
        need(sources.planes[plane].empty(),"Owned insertion did not consume every input");
        need(entry.planes[plane].data()==before.pointers[plane]&&entry.capacities[plane]==before.capacities[plane],
             "Owned insertion copied or discarded a source allocation");
        need(entry.planes[plane].size()==before.bytes[plane].size()&&
             std::equal(entry.planes[plane].begin(),entry.planes[plane].end(),before.bytes[plane].begin()),
             "Owned insertion changed a source plane's extent or bytes");
    }
}
bool ownedInsert(SourceCache& cache,OwnedSources& sources,uint64_t key,const std::shared_ptr<FakeMesh>& mesh,
                 const SourceCache::PendingArm* pending=nullptr,uint64_t stride=36) {
    return cache.insertOwned(sources.planes[0],sources.planes[1],sources.planes[2],stride,key,mesh,pending);
}
void ownedStatistics(const SourceCache& cache,uint64_t insertions,uint64_t logical,uint64_t capacity) {
    const auto& stats=cache.statistics();
    need(stats.ownedInsertions==insertions&&stats.ownedLogicalBytes==logical&&stats.ownedCapacityBytes==capacity,
         "Owned observations must count only successful commits and their original logical/capacity bytes");
}
struct ObserveOwnedAllocations {
    OwnedSourceAllocationObservation observation;
    explicit ObserveOwnedAllocations(const OwnedSources& sources,size_t fail=SIZE_MAX) {
        need(!ownedSourceAllocationObservation,"Nested owned allocation observation");
        observation.fail=fail;
        for(size_t plane=0;plane<3;++plane) {
            observation.inputs[plane]=&sources.planes[plane];
            observation.pointers[plane]=sources.planes[plane].data();
        }
        ownedSourceAllocationObservation=&observation;
    }
    ~ObserveOwnedAllocations(){ownedSourceAllocationObservation=nullptr;}
};
struct OwnedWatchedSource {
    std::array<uint8_t,6*4096> bytes{};
    std::array<std::atomic<uint8_t>,6> armed{};
    std::array<std::atomic<uint32_t>,6> version{};
    ~OwnedWatchedSource() {
        for(uint32_t page=0;page<6;++page)
            if(armed[page].exchange(0))Simpsons::guestWatchBlockAdjust(page,-1);
    }
    void stored(uint32_t page) {
        need(page<6,"Owned watch fixture page outside allocation");
        if(armed[page].exchange(0))Simpsons::guestWatchBlockAdjust(page,-1);
        version[page].fetch_add(1);
    }
    void seed(size_t slot,const OwnedSources& sources) {
        need(slot<2,"Owned watch fixture slot outside allocation");
        for(size_t plane=0;plane<3;++plane) {
            need(sources.planes[plane].size()<=4096,"Owned watch plane crosses fixture slot");
            std::copy(sources.planes[plane].begin(),sources.planes[plane].end(),bytes.begin()+(3*slot+plane)*4096);
        }
    }
    std::array<std::span<const uint8_t>,3> ranges(size_t slot=0) const {
        need(slot<2,"Owned watch fixture range outside allocation");
        return {std::span<const uint8_t>(bytes.data()+3*slot*4096,72),
                std::span<const uint8_t>(bytes.data()+(3*slot+1)*4096,12),
                std::span<const uint8_t>(bytes.data()+(3*slot+2)*4096,72)};
    }
    OwnedSources capture(size_t slot=0) const {
        OwnedSources result;
        const auto spans=ranges(slot);
        for(size_t plane=0;plane<3;++plane)result.planes[plane].assign(spans[plane].begin(),spans[plane].end());
        return result;
    }
    SourceCache::LookupResult lookup(SourceCache& cache,size_t slot=0) const {
        const auto spans=ranges(slot);
        return cache.lookup(spans[0],spans[1],spans[2],36);
    }
    void attach(SourceCache& cache) {cache.setWriteWatch({bytes.data(),armed.data(),version.data()},true);}
};
void unchangedPending(const SourceCache::PendingArm& pending,const SourceCache::PendingArm& before,
                      const uint32_t* versions,size_t capacity) {
    need(pending.valid==before.valid&&pending.stride==before.stride&&pending.versions==before.versions&&
         pending.versions.data()==versions&&pending.versions.capacity()==capacity,"Owned insertion changed its borrowed pending arm");
    for(size_t plane=0;plane<3;++plane)
        need(pending.plane[plane]==before.plane[plane]&&pending.size[plane]==before.size[plane]&&
             pending.first[plane]==before.first[plane]&&pending.last[plane]==before.last[plane],
             "Owned insertion changed pending arm planes or page extents");
}
void ownedSourceTransferAndSkips() {
    OwnedSources sources;
    const OwnedSourceSnapshot before(sources);
    const auto key=sources.key();const auto mesh=sourceMesh(81);
    SourceCache exact(2,before.charge());
    need(ownedInsert(exact,sources,key,mesh),"Exact retained-capacity budget rejected owned insertion");
    transferredOwnedSources(exact,sources,before,mesh.get());
    need(exact.residentBytes()==before.charge()&&exact.size()==1,"Owned capacity residency differs");
    need(exact.find(before.bytes[0],before.bytes[1],before.bytes[2],36,key)==mesh,"Transferred source cannot hit exact original bytes");
    ownedStatistics(exact,1,before.logical(),before.charge());
    for(unsigned reason=0;reason<4;++reason) {
        OwnedSources rejected;
        const OwnedSourceSnapshot snapshot(rejected);
        SourceCache cache(reason==1?0:2,reason==2?0:(reason==3?snapshot.charge()-1:SIZE_MAX));
        const auto retained=reason==0?std::shared_ptr<FakeMesh>{}:mesh;
        need(!ownedInsert(cache,rejected,rejected.key(),retained),"Null/disabled/oversized owned insertion was retained");
        untouchedOwnedSources(rejected,snapshot);
        need(!cache.size()&&!cache.residentBytes(),"Owned skip changed cache residency");
        ownedStatistics(cache,0,0,0);
        need(cache.statistics().oversizedSkips==uint64_t(reason==3),"Disabled or oversized owned skip observation differs");
    }
    // Object aliasing is rejected even when all three logical planes are
    // legal; distinct vector objects with equal bytes remain legal.
    for(size_t pair=0;pair<3;++pair) {
        OwnedSources aliased;
        const OwnedSourceSnapshot snapshot(aliased);
        SourceCache cache;
        Bytes* a=&aliased.planes[0];Bytes* b=&aliased.planes[1];Bytes* c=&aliased.planes[2];
        if(pair==0)b=a;else if(pair==1)c=a;else c=b;
        need(!cache.insertOwned(*a,*b,*c,36,aliased.key(),mesh),"Repeated vector object accepted as distinct owned planes");
        untouchedOwnedSources(aliased,snapshot);
        need(!cache.size()&&!cache.residentBytes(),"Aliased owned skip published an entry");
        ownedStatistics(cache,0,0,0);
    }
    OwnedSources equal;
    equal.planes[1]=equal.planes[0];equal.planes[2]=equal.planes[0];
    const OwnedSourceSnapshot equalBefore(equal);
    SourceCache equalCache;
    need(ownedInsert(equalCache,equal,equal.key(),mesh),"Distinct equal-byte owned vectors were mistaken for aliases");
    transferredOwnedSources(equalCache,equal,equalBefore,mesh.get());
}
void ownedSourceCollisionAndExtents() {
    OwnedSources sources;
    const OwnedSourceSnapshot before(sources);
    const auto key=sources.key();const auto mesh=sourceMesh(82);
    SourceCache cache;
    need(ownedInsert(cache,sources,key,mesh),"Owned collision fixture did not insert");
    for(size_t plane=0;plane<3;++plane) {
        for(size_t position:{size_t(0),before.bytes[plane].size()/2,before.bytes[plane].size()-1}) {
            auto changed=before.bytes;changed[plane][position]^=0xFF;
            need(!cache.find(changed[0],changed[1],changed[2],36,key),"Owned forced key accepted changed plane bytes");
        }
        auto changed=before.bytes;changed[plane].pop_back();
        need(!cache.find(changed[0],changed[1],changed[2],36,key),"Owned forced key accepted truncated plane");
        changed=before.bytes;changed[plane].push_back(0);
        need(!cache.find(changed[0],changed[1],changed[2],36,key),"Owned forced key accepted enlarged plane");
    }
    need(!cache.find(before.bytes[0],before.bytes[1],before.bytes[2],40,key),"Owned forced key accepted different stride");
    need(cache.find(before.bytes[0],before.bytes[1],before.bytes[2],36,key)==mesh&&cache.size()==1,
         "Owned misses changed exact original mesh or residency");
    ownedStatistics(cache,1,before.logical(),before.charge());
}
void ownedSourceMixedBudgetAndLifetime() {
    const auto v=vertices(),i=indices(),d=declaration();
    const size_t legacyCharge=v.size()+i.size()+d.size();
    OwnedSources owned(0x40000000);
    const OwnedSourceSnapshot before(owned);
    size_t destroyed{};
    SourceCache cache(3,legacyCharge+before.charge());
    const auto legacy=sourceMesh(83);
    cache.insert(v,i,d,36,sourceKey(v,i,d,36),legacy);
    auto ownedMesh=sourceMesh(84);std::weak_ptr<FakeMesh> weak=ownedMesh;
    ownedMesh->destroyed=&destroyed;
    const auto ownedKey=owned.key();
    need(ownedInsert(cache,owned,ownedKey,ownedMesh),"Mixed cache did not accept exact capacity boundary");
    transferredOwnedSources(cache,owned,before,ownedMesh.get());
    const auto legacyEntry=Simpsons::StaticMeshSourceCacheTestProbe::entry(cache,legacy.get());
    need(legacyEntry.found&&legacyEntry.charge==legacyCharge&&cache.residentBytes()==cache.maxBytes(),
         "Mixed legacy size and owned capacity accounting differs");
    // Retain a real returned mesh owner, then remove the setup owner so it
    // cannot hide premature eviction/destruction of the shared mesh.
    auto live=cache.find(before.bytes[0],before.bytes[1],before.bytes[2],36,ownedKey);
    ownedMesh.reset();
    need(live&&weak.use_count()==2,"Owned cache did not retain exactly its mesh plus the live caller");
    need(cache.find(v,i,d,36,sourceKey(v,i,d,36))==legacy,"Mixed legacy promotion failed");
    auto fresh=v;put(fresh,0,0x40400000);
    const auto newest=sourceMesh(85);
    cache.insert(fresh,i,d,36,sourceKey(fresh,i,d,36),newest);
    need(cache.size()==2&&cache.residentBytes()==2*legacyCharge,"Mixed byte eviction subtracted logical bytes from owned capacity");
    need(!cache.find(before.bytes[0],before.bytes[1],before.bytes[2],36,ownedKey),"Owned LRU survived capacity eviction");
    need(cache.find(v,i,d,36,sourceKey(v,i,d,36))==legacy&&
         cache.find(fresh,i,d,36,sourceKey(fresh,i,d,36))==newest,"Mixed byte eviction removed a retained legacy mesh");
    need(!weak.expired()&&!destroyed&&live->tag==84&&weak.use_count()==1,"Eviction invalidated the live mesh caller");
    ownedStatistics(cache,1,before.logical(),before.charge());
    const auto insertions=cache.statistics().insertions;
    cache.clear();
    need(!cache.size()&&!cache.residentBytes()&&cache.statistics().insertions==insertions&&!destroyed,
         "Mixed clear changed observations or a live evicted owner");
    ownedStatistics(cache,1,before.logical(),before.charge());
    live.reset();need(weak.expired()&&destroyed==1,"Live evicted mesh did not retire exactly on its final owner");
}
void ownedSourcePendingAndRevalidation() {
    OwnedWatchedSource source;
    OwnedSources initial;
    source.seed(0,initial);
    const auto guestBefore=source.bytes;
    SourceCache cache(2,SIZE_MAX);source.attach(cache);
    auto miss=source.lookup(cache);
    need(!miss.mesh&&miss.pending.valid,"Owned watched miss did not arm original guest source before capture");
    auto captured=source.capture();const OwnedSourceSnapshot before(captured);
    const auto pendingBefore=miss.pending;
    const auto* versions=miss.pending.versions.data();const auto versionCapacity=miss.pending.versions.capacity();
    const auto mesh=sourceMesh(86);
    need(ownedInsert(cache,captured,miss.key,mesh,&miss.pending),"Owned watched insertion did not commit");
    transferredOwnedSources(cache,captured,before,mesh.get());
    unchangedPending(miss.pending,pendingBefore,versions,versionCapacity);
    need(source.bytes==guestBefore&&Simpsons::StaticMeshSourceCacheTestProbe::hints(cache,mesh.get())==1,
         "Owned capture changed guest bytes or retained a host-vector binding instead of original guest spans");
    const auto fullBefore=cache.statistics().fullLookups;
    need(source.lookup(cache).mesh==mesh&&cache.watchHits()==1&&cache.statistics().fullLookups==fullBefore,
         "Original unchanged guest spans did not use the owned watched binding");
    for(uint64_t tick=0;tick<2*Simpsons::kStaticMeshSourceCacheRevalidateLookups&&!cache.statistics().revalidations;++tick)
        need(source.lookup(cache).mesh==mesh,"Owned periodic revalidation changed exact mesh identity");
    need(cache.statistics().revalidations==1&&cache.statistics().fullLookups==fullBefore+1&&
         cache.statistics().exactHits==1,"Owned binding failed to repeat bounded full-byte equality");
    transferredOwnedSources(cache,captured,before,mesh.get());
    const auto observations=cache.statistics();
    const auto bindings=cache.watchBindings();
    cache.clear();
    need(!cache.size()&&!cache.residentBytes()&&!Simpsons::StaticMeshSourceCacheTestProbe::hints(cache),
         "Owned clear retained source bytes or dangling hints");
    ownedStatistics(cache,1,before.logical(),before.charge());
    need(cache.statistics().insertions==observations.insertions&&cache.watchBindings()==bindings,
         "Owned clear reset cumulative insertion/binding observations");
    need(!source.lookup(cache).mesh,"Owned clear retained a stale fast-hit mesh");
}
void ownedSourceInterveningMutationAndInvalidArm() {
    for(size_t changedPlane=0;changedPlane<3;++changedPlane) {
        OwnedWatchedSource source;OwnedSources initial;source.seed(0,initial);
        SourceCache cache;source.attach(cache);
        auto miss=source.lookup(cache);auto captured=source.capture();const OwnedSourceSnapshot before(captured);
        need(!miss.mesh&&miss.pending.valid,"Intervening mutation fixture did not arm before capture");
        const auto pendingBefore=miss.pending;
        const auto* versions=miss.pending.versions.data();const auto capacity=miss.pending.versions.capacity();
        source.stored(uint32_t(changedPlane));source.bytes[changedPlane*4096]^=1;
        const auto guestAfterMutation=source.bytes;
        const auto mesh=sourceMesh(uint32_t(90+changedPlane));
        need(ownedInsert(cache,captured,miss.key,mesh,&miss.pending),"Stale pending arm incorrectly prevented retaining validated owned bytes");
        transferredOwnedSources(cache,captured,before,mesh.get());
        unchangedPending(miss.pending,pendingBefore,versions,capacity);
        need(!cache.watchBindings()&&!Simpsons::StaticMeshSourceCacheTestProbe::hints(cache)&&source.bytes==guestAfterMutation,
             "Intervening source mutation was silently rebound or changed by insertion");
        need(!source.lookup(cache).mesh&&!cache.watchHits(),"Changed guest plane reused a stale owned snapshot");
        need(cache.find(before.bytes[0],before.bytes[1],before.bytes[2],36,miss.key)==mesh,
             "Stale binding rejection lost the immutable captured bytes");
        source.stored(uint32_t(changedPlane));source.bytes[changedPlane*4096]^=1;
        need(source.lookup(cache).mesh==mesh&&cache.watchBindings()==1,"Restored source did not bind after new full equality");
        const auto full=cache.statistics().fullLookups;
        need(source.lookup(cache).mesh==mesh&&cache.statistics().fullLookups==full&&cache.watchHits()==1,
             "Restored source did not use its newly proven original-span binding");
    }
    for(unsigned invalid=0;invalid<7;++invalid) {
        OwnedWatchedSource source;OwnedSources initial;source.seed(0,initial);
        SourceCache cache;source.attach(cache);
        auto miss=source.lookup(cache);auto captured=source.capture();const OwnedSourceSnapshot before(captured);
        need(miss.pending.valid,"Malformed pending fixture lacked an original arm");
        auto malformed=miss.pending;
        if(invalid==0)malformed.valid=false;
        else if(invalid==1)++malformed.stride;
        else if(invalid==2)++malformed.size[0];
        else if(invalid==3)++malformed.size[1];
        else if(invalid==4)++malformed.size[2];
        else if(invalid==5)++malformed.first[0];
        else malformed.versions.pop_back();
        const auto malformedBefore=malformed;
        const auto* versions=malformed.versions.data();const auto capacity=malformed.versions.capacity();
        const auto mesh=sourceMesh(uint32_t(94+invalid));
        need(ownedInsert(cache,captured,miss.key,mesh,&malformed),"Invalid hint must not reject valid owned source retention");
        transferredOwnedSources(cache,captured,before,mesh.get());
        unchangedPending(malformed,malformedBefore,versions,capacity);
        need(!cache.watchBindings()&&!Simpsons::StaticMeshSourceCacheTestProbe::hints(cache),
             "Mismatched stride/extent/page/version pending arm published a binding");
        const auto full=cache.statistics().fullLookups;
        need(source.lookup(cache).mesh==mesh&&cache.statistics().fullLookups==full+1&&!cache.watchHits(),
             "Rejected pending arm bypassed the required original guest byte comparison");
        ownedStatistics(cache,1,before.logical(),before.charge());
    }
}
struct OwnedInsertionFixture {
    unsigned mode;
    size_t oldDestroyed{};
    OwnedWatchedSource source;
    SourceCache cache{1,SIZE_MAX};
    OwnedSources captured;
    SourceCache::LookupResult miss;
    std::shared_ptr<FakeMesh> mesh{sourceMesh(104)};
    std::weak_ptr<FakeMesh> old;
    FakeMesh* oldPointer{};
    size_t oldCharge{};
    size_t slot{};
    explicit OwnedInsertionFixture(unsigned value):mode(value) {
        need(mode<4,"Unknown owned allocation fixture mode");
        if(mode)source.attach(cache);
        OwnedSources fresh(0x40000000u);
        if(mode>=2) {
            OwnedSources original(0x3F800000u);source.seed(0,original);
            auto oldMiss=source.lookup(cache);
            auto previous=sourceMesh(103);previous->destroyed=&oldDestroyed;
            oldPointer=previous.get();old=previous;oldCharge=original.logical();
            cache.insert(original.planes[0],original.planes[1],original.planes[2],36,oldMiss.key,previous,&oldMiss.pending);
            previous.reset();
            need(!old.expired()&&cache.watchBindings()==1,"Allocation fixture failed to retain original watched mesh");
            if(mode==2)slot=1;
            else for(uint32_t page=0;page<3;++page)source.stored(page);
        }
        source.seed(slot,fresh);
        miss=source.lookup(cache,slot);
        need(!miss.mesh&&miss.pending.valid==bool(mode),"Owned allocation fixture did not reach a new cache miss");
        captured=source.capture(slot);
    }
    bool commit() {return ownedInsert(cache,captured,miss.key,mesh,mode?&miss.pending:nullptr);}
    void committed(const OwnedSourceSnapshot& before) {
        transferredOwnedSources(cache,captured,before,mesh.get());
        need(cache.size()==1&&cache.residentBytes()==before.charge(),"Owned commit did not apply exact entry/capacity limits");
        ownedStatistics(cache,1,before.logical(),before.charge());
        if(mode>=2)need(old.expired()&&oldDestroyed==1,"Successful owned replacement did not retire the old cache-only mesh");
        if(mode) {
            const auto full=cache.statistics().fullLookups;
            const auto hits=cache.watchHits();
            need(Simpsons::StaticMeshSourceCacheTestProbe::hints(cache)==1&&
                 Simpsons::StaticMeshSourceCacheTestProbe::hints(cache,mesh.get())==1,
                 "Owned replacement published dangling or duplicate original-span hints");
            need(source.lookup(cache,slot).mesh==mesh&&cache.statistics().fullLookups==full&&cache.watchHits()==hits+1,
                 "New original-span hint was erased while evicting the old entry");
        }
    }
    void failed(const OwnedSourceSnapshot& before,const SourceCache::Statistics& statistics,uint64_t bindings) {
        untouchedOwnedSources(captured,before);
        need(cache.size()==size_t(mode>=2)&&cache.residentBytes()==oldCharge&&cache.statistics().insertions==statistics.insertions&&
             cache.statistics().evictions==statistics.evictions&&cache.watchBindings()==bindings,
             "Failed owned preparation evicted or published an entry/binding");
        ownedStatistics(cache,0,0,0);
        need(Simpsons::StaticMeshSourceCacheTestProbe::hints(cache)==size_t(mode>=2),
             "Failed pre-cap owned preparation changed old hint residency");
        if(mode>=2) {
            need(!old.expired()&&!oldDestroyed&&Simpsons::StaticMeshSourceCacheTestProbe::entry(cache,oldPointer).found,
                 "Failed owned preparation destroyed the old cache-only mesh");
            if(mode==2) {
                const auto full=cache.statistics().fullLookups;
                const auto hits=cache.watchHits();
                need(source.lookup(cache,0).mesh.get()==oldPointer&&cache.statistics().fullLookups==full&&cache.watchHits()==hits+1,
                     "Failed owned preparation damaged an unchanged old fast-hit binding");
            }
        }
    }
};
void ownedSourceEveryAllocationBeforeTransfer() {
    for(unsigned mode=0;mode<4;++mode) {
        size_t points{};
        {
            OwnedInsertionFixture fixture(mode);
            const OwnedSourceSnapshot before(fixture.captured);
            bool committed{},lateAllocation{};
            {
                ObserveOwnedAllocations audit(fixture.captured);
                committed=fixture.commit();points=audit.observation.calls;lateAllocation=audit.observation.afterConsume;
            }
            need(committed&&points>0&&!lateAllocation,"Successful owned insertion allocated after consuming any input plane");
            if(mode==1)need(points>=4,"Watched allocation audit missed list, versions, hint keys or map preparation");
            fixture.committed(before);
        }
        for(size_t point=0;point<points;++point) {
            OwnedInsertionFixture fixture(mode);
            const OwnedSourceSnapshot before(fixture.captured);
            const auto statistics=fixture.cache.statistics();const auto bindings=fixture.cache.watchBindings();
            const auto pending=fixture.miss.pending;
            const auto* versions=fixture.miss.pending.versions.data();const auto capacity=fixture.miss.pending.versions.capacity();
            bool rejected{},injected{},lateAllocation{};
            {
                ObserveOwnedAllocations audit(fixture.captured,point);
                try {(void)fixture.commit();}catch(const std::bad_alloc&){rejected=true;}
                injected=audit.observation.injected;lateAllocation=audit.observation.afterConsume;
            }
            need(rejected&&injected&&!lateAllocation,"Owned actual-allocation failure was absent or occurred after input consumption");
            unchangedPending(fixture.miss.pending,pending,versions,capacity);
            fixture.failed(before,statistics,bindings);
            need(fixture.commit(),"Owned insertion did not recover after a preparation allocation failure");
            fixture.committed(before);
        }
    }
}
void ownedSourceMutationDuringFinalAllocation() {
    size_t points{};
    {
        OwnedInsertionFixture fixture(1);
        ObserveOwnedAllocations audit(fixture.captured);
        const bool committed=fixture.commit();
        points=audit.observation.calls;
        need(committed&&points>=4&&!audit.observation.afterConsume,"Final allocation mutation audit lacked staged watched insertion");
    }
    OwnedInsertionFixture fixture(1);
    const OwnedSourceSnapshot before(fixture.captured);
    struct Mutation {
        OwnedWatchedSource* source;
        size_t ordinal;
        bool fired{};
        static void allocation(size_t at,void* argument) noexcept {
            auto& self=*static_cast<Mutation*>(argument);
            if(at!=self.ordinal)return;
            self.fired=true;
            if(self.source->armed[0].exchange(0))Simpsons::guestWatchBlockAdjust(0,-1);
            self.source->version[0].fetch_add(1);
            self.source->bytes[0]^=1;
        }
    } mutation{&fixture.source,points-1};
    bool committed{},lateAllocation{};
    {
        ObserveOwnedAllocations audit(fixture.captured);
        audit.observation.onAllocation=Mutation::allocation;audit.observation.argument=&mutation;
        committed=fixture.commit();lateAllocation=audit.observation.afterConsume;
    }
    need(committed&&mutation.fired&&!lateAllocation,"Final allocation mutation did not occur inside real pre-consume preparation");
    transferredOwnedSources(fixture.cache,fixture.captured,before,fixture.mesh.get());
    need(!fixture.cache.watchBindings()&&!Simpsons::StaticMeshSourceCacheTestProbe::hints(fixture.cache),
         "Source mutation during final map allocation survived pending version recheck");
    need(!fixture.source.lookup(fixture.cache).mesh&&!fixture.cache.watchHits(),
         "Changed guest bytes used the new default hint placeholder");
    need(fixture.cache.find(before.bytes[0],before.bytes[1],before.bytes[2],36,fixture.miss.key)==fixture.mesh,
         "Final version recheck lost valid immutable captured source bytes");
    ownedStatistics(fixture.cache,1,before.logical(),before.charge());
}
Bytes read(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary);need(bool(in),"Optional capture could not be opened");
    Bytes bytes{std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};need(!in.bad(),"Capture read failed");return bytes;
}
void captured(const std::filesystem::path& directory) {
    const std::string prefix=std::filesystem::exists(directory/"rejected-zprepass-elements.bin")?"rejected-zprepass-":"scene-";
    const auto d=read(directory/(prefix+"elements.bin")),v=read(directory/(prefix+"vertices.bin")),g=read(directory/(prefix+"geometry.bin"));
    need(g.size()==120&&word(g,0)==v.size()&&word(g,8)*12==d.size(),"Captured geometry extent differs");
    uint32_t offset=UINT32_MAX;
    for(size_t at=0;at+12<d.size();at+=12)if(word(d,at+8)==0&&word(d,at+4)==0x002A23B9)offset=word(d,at)&0xFFFF;
    need(offset!=UINT32_MAX,"Capture lacks position0");const auto stride=word(g,4);
    positions(decodeStaticZPrepassVertices(v,d,stride),v,stride,offset);
}
}
int main(int argc,char** argv)try {
    need(argc<=2,"Usage: ZPrepassVerticesTests [capture-directory]");
    consumedAndDeadInputs();declarationGuards();tangentProfile();finiteAndExtents();
    const auto positionCallerCSR=_mm_getcsr();
    staticPositionOwnedBounds();staticPositionProtectedEnd();
    need(_mm_getcsr()==positionCallerCSR,"Static position controls did not restore caller MXCSR");
    sourceCacheExactHitSharesMesh();sourceCachePlaneAndStrideMiss();sourceCacheSnapshotsSurviveCallerMutation();
    sourceCacheForcedHashRejects();sourceCacheForcedHashBoundaries();sourceCacheEviction();
    sourceLookupEmptyAndOneEntry();sourceLookupRepeatedCycleIdenticalExtents();sourceLookupPlaneMutationsMiss();
    sourceLookupSnapshotsSurviveCallerMutation();sourceLookupOversizeAndEvictionBounded();
    sourceWatchObservationsPreserveResults();
    ownedSourceTransferAndSkips();ownedSourceCollisionAndExtents();ownedSourceMixedBudgetAndLifetime();
    ownedSourcePendingAndRevalidation();ownedSourceInterveningMutationAndInvalidArm();
    ownedSourceEveryAllocationBeforeTransfer();ownedSourceMutationDuringFinalAllocation();
    byteRangeEqualMatchesScalar();byteRangeEqualEveryByteMismatch();byteRangeEqualIgnoresTrailingBytes();
    byteRangeEqualGuardPages();if(argc==2)captured(argv[1]);
    std::printf("PASS static Z-prepass vertices: %zu checks; exact position bits, eight zero inputs, dead payloads, declaration guards, exact-source cache, transactional owned raw sources and SSE2 byte-range equality\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL static Z-prepass vertices after %zu checks: %s\n",checks,e.what());return 1;}
