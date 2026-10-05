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
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
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
// Exact raw-source cache coverage on the production cache implementation.
struct FakeMesh {uint32_t tag;};
using SourceCache=Simpsons::StaticMeshSourceCache<FakeMesh>;
Bytes indices() {
    Bytes result(12);
    for(size_t i=0;i<result.size();++i)result[i]=uint8_t(i);
    return result;
}
uint64_t sourceKey(const Bytes& v,const Bytes& i,const Bytes& d,uint32_t stride) {
    return SourceCache::contentKey(v,i,d,stride);
}
std::shared_ptr<FakeMesh> sourceMesh(uint32_t tag) {return std::make_shared<FakeMesh>(FakeMesh{tag});}
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
    sourceCacheExactHitSharesMesh();sourceCachePlaneAndStrideMiss();sourceCacheSnapshotsSurviveCallerMutation();
    sourceCacheForcedHashRejects();sourceCacheForcedHashBoundaries();sourceCacheEviction();
    sourceLookupEmptyAndOneEntry();sourceLookupRepeatedCycleIdenticalExtents();sourceLookupPlaneMutationsMiss();
    sourceLookupSnapshotsSurviveCallerMutation();sourceLookupOversizeAndEvictionBounded();
    byteRangeEqualMatchesScalar();byteRangeEqualEveryByteMismatch();byteRangeEqualIgnoresTrailingBytes();
    byteRangeEqualGuardPages();if(argc==2)captured(argv[1]);
    std::printf("PASS static Z-prepass vertices: %zu checks; exact position bits, eight zero inputs, dead payloads, declaration guards, exact-source cache and SSE2 byte-range equality\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL static Z-prepass vertices after %zu checks: %s\n",checks,e.what());return 1;}
