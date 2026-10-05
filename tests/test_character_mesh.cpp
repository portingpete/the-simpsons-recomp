#include "runtime/character_mesh.h"
#include "runtime/runtime.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
using Bytes=std::vector<uint8_t>;
using Simpsons::decodeCharacterVertices;
using Simpsons::decodeCharacterIndices;
size_t checks{};
void need(bool value,const char* why) {++checks;if(!value)throw std::runtime_error(why);}
template<class F> void rejects(F&& f,const char* why) {
    bool rejected=false;
    try {f();} catch(const Simpsons::Failure&) {rejected=true;}
    need(rejected,why);
}
void put(Bytes& bytes,size_t offset,uint32_t bits) {
    need(offset+4<=bytes.size(),"Invalid synthetic fixture write");
    bytes[offset]=uint8_t(bits>>24);bytes[offset+1]=uint8_t(bits>>16);
    bytes[offset+2]=uint8_t(bits>>8);bytes[offset+3]=uint8_t(bits);
}
template<size_t N> void bits(const std::array<float,N>& actual,const std::array<uint32_t,N>& expected) {
    for(size_t i=0;i<N;++i)need(std::bit_cast<uint32_t>(actual[i])==expected[i],"Attribute bits/lane order changed");
}

// Independent literal fixture: the observed 12 data rows and complete terminal
// row. No production layout builder and no required capture files are used.
Bytes declaration() {
    constexpr std::array<uint32_t,39> words={
        0x00000000,0x002A23B9,0x00000000, // position0
        0x0000000C,0x002A2187,0x00030000, // packed normal
        0x00000010,0x002C23A5,0x00050000, // UV0
        0x00000018,0x001A2286,0x00020000, // unsigned integer bones
        0x0000001C,0x001A23A6,0x00010000, // float weights
        0x0000002C,0x00182886,0x000A0000, // color
        0x00010000,0x002A23B9,0x00000100,
        0x00020000,0x002A23B9,0x00000200,
        0x00030000,0x002A23B9,0x00000300,
        0x00040000,0x002A23B9,0x00000400,
        0x00050000,0x002A23B9,0x00000500,
        0x00060000,0x002A23B9,0x00000600,
        0x00FF0000,0xFFFFFFFF,0x00000000};
    Bytes result(words.size()*4);
    for(size_t i=0;i<words.size();++i)put(result,i*4,words[i]);
    return result;
}
Bytes vertex() {
    Bytes result(48,0xFF); // Ignored packed normal/color are deliberately not floats.
    put(result,0,0x3FA00000);put(result,4,0xC0200000);put(result,8,0x40700000);
    put(result,16,0x80000000);put(result,20,0x40100000);
    put(result,24,0x00000004); // Raw bytes 00 00 00 04: X is bone 4.
    put(result,28,0x3F800000);put(result,32,0);put(result,36,0);put(result,40,0);
    return result;
}

// live011 geometry E6B5EFF0: 14 rows, stride56, bones3. UV1 shifts the
// consumed indices/weights by eight bytes; morph streams1..6 are unchanged.
Bytes declarationWithUv1() {
    constexpr std::array<uint32_t,42> words={
        0x00000000,0x002A23B9,0x00000000,
        0x0000000C,0x002A2187,0x00030000,
        0x00000010,0x002C23A5,0x00050000,
        0x00000018,0x002C23A5,0x00050100,
        0x00000020,0x001A2286,0x00020000,
        0x00000024,0x001A23A6,0x00010000,
        0x00000034,0x00182886,0x000A0000,
        0x00010000,0x002A23B9,0x00000100,
        0x00020000,0x002A23B9,0x00000200,
        0x00030000,0x002A23B9,0x00000300,
        0x00040000,0x002A23B9,0x00000400,
        0x00050000,0x002A23B9,0x00000500,
        0x00060000,0x002A23B9,0x00000600,
        0x00FF0000,0xFFFFFFFF,0x00000000};
    Bytes result(words.size()*4);
    for(size_t i=0;i<words.size();++i)put(result,4*i,words[i]);
    return result;
}
Bytes vertexWithUv1() {
    auto result=vertex();result.insert(result.begin()+24,8,0xCD);
    put(result,24,0x42F60000);put(result,28,0xC3790000); // Distinct, unused UV1.
    put(result,32,0x3F000201); // Lanes 1,2,0,63; last lane has zero weight.
    put(result,36,0x3E800000);put(result,40,0x3F000000);
    put(result,44,0xBE000000);put(result,48,0x80000000);
    return result;
}

void additionalUvValues() {
    auto d=declarationWithUv1(),first=vertexWithUv1(),second=vertexWithUv1();
    put(second,0,0x80000000);put(second,4,0);put(second,8,0xBF000001);
    put(second,16,0xBF800001);put(second,20,0);
    put(second,32,0x02013F00); // Lanes 0,63,1,2.
    put(second,36,0x3F800000);put(second,40,0);
    put(second,44,0x3F000000);put(second,48,0xBE000000);
    auto input=first;input.insert(input.end(),second.begin(),second.end());
    const auto original=input,originalDeclaration=d;
    const auto decoded=decodeCharacterVertices(input,d,56,3);
    need(decoded.size()==2,"UV1 profile changed stride/vertex count");
    bits(decoded[0].position,std::array<uint32_t,3>{0x3FA00000,0xC0200000,0x40700000});
    bits(decoded[0].uv,std::array<uint32_t,2>{0x80000000,0x40100000});
    bits(decoded[0].weights,std::array<uint32_t,4>{0x3E800000,0x3F000000,0xBE000000,0x80000000});
    bits(decoded[0].indices,std::array<uint32_t,4>{0x3F800000,0x40000000,0,0x427C0000});
    bits(decoded[1].position,std::array<uint32_t,3>{0x80000000,0,0xBF000001});
    bits(decoded[1].uv,std::array<uint32_t,2>{0xBF800001,0});
    bits(decoded[1].weights,std::array<uint32_t,4>{0x3F800000,0,0x3F000000,0xBE000000});
    bits(decoded[1].indices,std::array<uint32_t,4>{0,0x427C0000,0x3F800000,0x40000000});
    need(input==original&&d==originalDeclaration,"UV1 decoder changed caller-owned input");
    // Both NaN encodings and infinities are legal in the unused UV1 lanes.
    // Compare every consumed output bit, not just the selected UV0 values.
    put(input,24,0x7FC12345);put(input,28,0x7F812345);
    put(input,56+24,0x7F800000);put(input,56+28,0xFF800000);
    const auto ignoredInput=input;
    const auto ignored=decodeCharacterVertices(input,d,56,3);
    need(input==ignoredInput&&d==originalDeclaration,"Ignoring UV1 canonicalized its payload or declaration");
    std::fill(input.begin(),input.end(),0);std::fill(d.begin(),d.end(),0);
    need(ignored.size()==decoded.size(),"Ignoring UV1 changed output extent");
    for(size_t i=0;i<decoded.size();++i) {
        bits(ignored[i].position,std::bit_cast<std::array<uint32_t,3>>(decoded[i].position));
        bits(ignored[i].uv,std::bit_cast<std::array<uint32_t,2>>(decoded[i].uv));
        bits(ignored[i].weights,std::bit_cast<std::array<uint32_t,4>>(decoded[i].weights));
        bits(ignored[i].indices,std::bit_cast<std::array<uint32_t,4>>(decoded[i].indices));
    }
}

void additionalUvValidation() {
    const auto d=declarationWithUv1(),v=vertexWithUv1();
    // Row3 is UV1: retain its exact float2 type, default method, stream0,
    // aligned in-stride extent and unique usage index1.
    for(const auto& mutation:std::array<std::array<uint32_t,2>,7>{{
            {40,0x001A23A6},{44,0x01050100},{36,0x00010018},
            {36,25},{36,52},{36,60},{44,0x00050200}}}) {
        auto bad=d;put(bad,mutation[0],mutation[1]);
        rejects([&]{decodeCharacterVertices(v,bad,56,3);},"Malformed unused UV1 declaration accepted");
    }
    auto duplicate=d;duplicate.insert(duplicate.end()-12,d.begin()+36,d.begin()+48);
    rejects([&]{decodeCharacterVertices(v,duplicate,56,3);},"Duplicate unused UV1 accepted");
    auto missingUv0=d;missingUv0.erase(missingUv0.begin()+24,missingUv0.begin()+36);
    rejects([&]{decodeCharacterVertices(v,missingUv0,56,3);},"UV1 substituted for required UV0");
    // The exception for dead UV1 must not weaken consumed-field or bone guards
    // at their shifted stride56 offsets, including a zero-weight bank overflow.
    for(const auto& mutation:std::array<std::array<uint32_t,2>,4>{{
            {16,0x7FC12345},{36,0x7F800000},{32,0x3F000203},{32,0x40000201}}}) {
        auto bad=v;put(bad,mutation[0],mutation[1]);
        rejects([&]{decodeCharacterVertices(bad,d,56,3);},"UV1 exception weakened a consumed attribute or bone bound");
    }
}

void valuesAndOwnership() {
    auto d=declaration(),first=vertex(),second=vertex();
    put(second,0,0x80000000);put(second,4,0x00000000);put(second,8,0xBF000001);
    put(second,16,0xBF800000);put(second,20,0x40800000);
    put(second,24,0x04030201);
    // Sum is 2.125, with a negative lane: preserve finite original values.
    put(second,28,0x3E800000);put(second,32,0x3F000000);
    put(second,36,0x3FC00000);put(second,40,0xBE000000);
    auto bytes=first;bytes.insert(bytes.end(),second.begin(),second.end());
    const auto originalBytes=bytes,originalDeclaration=d;
    const auto decoded=decodeCharacterVertices(bytes,d,48,6);
    need(decoded.size()==2,"Vertex count differs");
    bits(decoded[0].position,std::array<uint32_t,3>{0x3FA00000,0xC0200000,0x40700000});
    bits(decoded[0].uv,std::array<uint32_t,2>{0x80000000,0x40100000});
    bits(decoded[0].weights,std::array<uint32_t,4>{0x3F800000,0,0,0});
    need(decoded[0].indices==std::array<float,4>{4,0,0,0},"Packed X=4 decoded incorrectly");
    bits(decoded[1].position,std::array<uint32_t,3>{0x80000000,0,0xBF000001});
    bits(decoded[1].uv,std::array<uint32_t,2>{0xBF800000,0x40800000});
    bits(decoded[1].weights,std::array<uint32_t,4>{0x3E800000,0x3F000000,0x3FC00000,0xBE000000});
    need(decoded[1].indices==std::array<float,4>{1,2,3,4},"Bone byte lanes reversed or normalized");
    need(bytes==originalBytes&&d==originalDeclaration,"Decoder modified caller-owned input");
    std::fill(bytes.begin(),bytes.end(),0);std::fill(d.begin(),d.end(),0);
    need(decoded[0].indices[0]==4&&decoded[1].weights[2]==1.5f,"Decoded result borrowed source storage");
}

void boneBounds() {
    const auto d=declaration();
    for(size_t lane=0;lane<4;++lane) {
        auto v=vertex();
        for(size_t j=0;j<4;++j)put(v,28+4*j,0);
        put(v,24,uint32_t(5)<<(8*lane));put(v,28+4*lane,0x3F800000);
        need(decodeCharacterVertices(v,d,48,6)[0].indices[lane]==5,"Last palette bone rejected");
        put(v,24,uint32_t(6)<<(8*lane));
        rejects([&]{decodeCharacterVertices(v,d,48,6);},"Weighted index equal to palette size accepted");
        put(v,28+4*lane,0xBE800000);
        rejects([&]{decodeCharacterVertices(v,d,48,6);},"Negative nonzero weight bypassed palette bound");
        put(v,24,uint32_t(63)<<(8*lane));
        for(uint32_t zero:std::array<uint32_t,2>{0,0x80000000}) {
            put(v,28+4*lane,zero);
            const auto out=decodeCharacterVertices(v,d,48,6);
            need(out[0].indices[lane]==63,"Zero-weight index within constant bank rejected");
            need(std::bit_cast<uint32_t>(out[0].weights[lane])==zero,"Signed zero weight changed");
        }
        put(v,28+4*lane,0x3F800000);
        need(decodeCharacterVertices(v,d,48,64)[0].indices[lane]==63,"Last bank bone rejected");
        for(uint32_t bad:std::array<uint32_t,2>{64,255}) {
            put(v,24,bad<<(8*lane));
            for(uint32_t weight:std::array<uint32_t,2>{0,0x3F800000}) {
                put(v,28+4*lane,weight);
                rejects([&]{decodeCharacterVertices(v,d,48,64);},"Index outside bank accepted, including zero weight");
            }
        }
    }
    auto v=vertex();put(v,24,0);
    need(decodeCharacterVertices(v,d,48,1).size()==1,"Single-bone palette rejected");
    for(uint32_t bad:std::array<uint32_t,2>{0,65})
        rejects([&]{decodeCharacterVertices(v,d,48,bad);},"Unsupported palette size accepted");
}

void declarationValidation() {
    const auto v=vertex(),d=declaration();
    for(size_t row:std::array<size_t,4>{0,2,3,4}) {
        auto missing=d;missing.erase(missing.begin()+row*12,missing.begin()+(row+1)*12);
        rejects([&]{decodeCharacterVertices(v,missing,48,6);},"Missing consumed semantic accepted");
        auto duplicate=d;
        duplicate.insert(duplicate.end()-12,d.begin()+row*12,d.begin()+(row+1)*12);
        rejects([&]{decodeCharacterVertices(v,duplicate,48,6);},"Duplicate consumed semantic accepted");
    }
    auto reordered=d;
    for(size_t row=0;row<12;++row)
        std::copy_n(d.begin()+row*12,12,reordered.begin()+(11-row)*12);
    need(decodeCharacterVertices(v,reordered,48,6)[0].indices[0]==4,"Semantic association depends on row order");
    auto opaque=d;
    for(size_t row=0;row<13;++row)opaque[row*12+11]=0xA5;
    need(decodeCharacterVertices(v,opaque,48,6).size()==1,"Opaque trailing bytes treated as semantics");
    // Each malformed terminal field is independently rejected.
    for(const auto& mutation:std::array<std::array<uint32_t,2>,6>{{
            {144,0xFFFF0000},{144,0x00FF0004},{148,0x002A23B9},
            {152,0x01000000},{152,0x00010000},{152,0x00000100}}}) {
        auto bad=d;put(bad,mutation[0],mutation[1]);
        rejects([&]{decodeCharacterVertices(v,bad,48,6);},"Malformed terminal record accepted");
    }
    auto early=d;std::copy_n(d.end()-12,12,early.begin());
    rejects([&]{decodeCharacterVertices(v,early,48,6);},"Early terminator accepted");
    auto noEnd=d;noEnd.resize(noEnd.size()-12);
    rejects([&]{decodeCharacterVertices(v,noEnd,48,6);},"Missing terminator accepted");
    // Wrong bone type, unaligned/out-of-stride position, method, usage index,
    // unknown usage and malformed unused/morph rows must fail explicitly.
    for(const auto& mutation:std::array<std::array<uint32_t,2>,9>{{
            {40,0x00182886},{0,1},{0,40},{8,0x01000000},{8,0x00000100},
            {8,0x000F0000},{16,0x002A23B9},{64,0x001A23A6},{80,0x00000200}}}) {
        auto bad=d;put(bad,mutation[0],mutation[1]);
        rejects([&]{decodeCharacterVertices(v,bad,48,6);},"Unqualified declaration element accepted");
    }
}

void nonfiniteAndExtents() {
    const auto d=declaration(),v=vertex();
    for(size_t offset:std::array<size_t,9>{0,4,8,16,20,28,32,36,40})
        for(uint32_t invalid:std::array<uint32_t,3>{0x7FC12345,0x7F800000,0xFF800000}) {
            auto bad=v;put(bad,offset,invalid);
            rejects([&]{decodeCharacterVertices(bad,d,48,6);},"Nonfinite consumed attribute accepted");
        }
    for(size_t size:std::array<size_t,4>{0,1,47,49}) {
        auto bad=v;bad.resize(size);
        rejects([&]{decodeCharacterVertices(bad,d,48,6);},"Truncated/partial vertex payload accepted");
    }
    for(size_t size:std::array<size_t,4>{0,12,155,157}) {
        auto bad=d;bad.resize(size);
        rejects([&]{decodeCharacterVertices(v,bad,48,6);},"Invalid declaration byte extent accepted");
    }
    for(uint32_t stride:std::array<uint32_t,4>{0,24,47,4097})
        rejects([&]{decodeCharacterVertices(v,d,stride,6);},"Invalid stride accepted");
    Bytes largeOwner(size_t(65536)*48);
    for(size_t at=0;at<largeOwner.size();at+=48)std::copy(v.begin(),v.end(),largeOwner.begin()+at);
    const auto ownerBefore=largeOwner;
    const auto large=decodeCharacterVertices(largeOwner,d,48,6);
    need(large.size()==65536&&large.front().indices[0]==4&&large.back().indices[0]==4,
         "Valid original65536 character owner rejected or decoded incorrectly");
    need(largeOwner==ownerBefore,"Large character decoder changed its owner");
    auto padded=v;padded.resize(52,0xCD);
    auto next=v;put(next,24,5);padded.insert(padded.end(),next.begin(),next.end());padded.resize(104,0xCD);
    const auto out=decodeCharacterVertices(padded,d,52,6);
    need(out.size()==2&&out[0].indices[0]==4&&out[1].indices[0]==5,"Aligned padded stride decoded incorrectly");
}

void originalStrideRegression() {
    const auto d=declaration();
    // Original8243C6A8/B4 stores (stride >> 2) in ONE byte. Accepting an
    // unaligned or overflowing stride silently disagrees with original fetch.
    // The decoder must reject both forms instead of inventing a wider stride.
    for(uint32_t stride:std::array<uint32_t,2>{49,1024}) {
        auto v=vertex();v.resize(stride);
        rejects([&]{decodeCharacterVertices(v,d,stride,6);},"Stride cannot round-trip through original DWORD stride byte");
    }
}

void indexBytes() {
    Bytes input={0,0,0,4,0x12,0x34,0,0xEC,0xFF,0xFF,0xFF,0xFE,1,0,0xFF,0xFF};
    const auto original=input;
    const auto out=decodeCharacterIndices(input);
    need(out==std::vector<uint16_t>{0,4,0x1234,236,0xFFFF,0xFFFE,256,0xFFFF},"BE16 indices/cuts changed or filtered");
    need(input==original,"Index decoder modified caller input");
    std::fill(input.begin(),input.end(),0);
    need(out[2]==0x1234&&out.back()==0xFFFF,"Index output borrowed source storage");
    for(const auto& bad:std::array<Bytes,2>{Bytes{},Bytes{0}})
        rejects([&]{decodeCharacterIndices(bad);},"Index owner without a complete R16 word accepted");
    const Bytes odd={0x12,0x34,0xFF};const auto oddBefore=odd;
    need(decodeCharacterIndices(odd)==std::vector<uint16_t>{0x1234}&&odd==oddBefore,
         "Unused odd owner byte acquired an index or changed source data");
    for(const size_t size:std::array<size_t,2>{16*1024*1024+1,16*1024*1024+2}) {
        Bytes large(size,0xFF);large[0]=0x12;large[1]=0x34;
        const auto decoded=decodeCharacterIndices(large);
        need(decoded.size()==size/2&&decoded.front()==0x1234&&decoded.back()==UINT16_MAX,
             "Valid original owner above16MiB rejected or complete words changed");
        need(large.front()==0x12&&large[1]==0x34&&large.back()==0xFF,"Large index source data changed");
    }
}

Bytes read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    need(bool(input),"Optional capture file could not be opened");
    Bytes result{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    need(!input.bad(),"Optional capture file read failed");return result;
}
void capturedPayload(const std::filesystem::path& directory) {
    const auto d=read(directory/"shadow-mesh-elements.bin");
    const auto v=read(directory/"shadow-mesh-vertices.bin");
    const auto i=read(directory/"shadow-mesh-indices.bin");
    need(d==declaration()&&v.size()==11376&&i.size()==1154,"Unexpected first-mesh capture extent/declaration");
    const auto vertices=decodeCharacterVertices(v,d,48,6);
    const auto indices=decodeCharacterIndices(i);
    need(vertices.size()==237&&indices.size()==577,"Captured counts differ");
    bits(vertices[0].position,std::array<uint32_t,3>{0xBC95182B,0x3AB78034,0x3DB4D6A1});
    bits(vertices[0].uv,std::array<uint32_t,2>{0x3E17F62B,0x3F0205BC});
    bits(vertices[0].weights,std::array<uint32_t,4>{0x3F800000,0,0,0});
    need(vertices[0].indices==std::array<float,4>{4,0,0,0},"Captured bone X differs");
    need(std::count(indices.begin(),indices.end(),uint16_t(0xFFFF))==56,"Captured restart count differs");
    need(std::all_of(indices.begin(),indices.end(),[](uint16_t index){return index==0xFFFF||index<237;}),"Captured vertex index is out of range");
    need(std::count(indices.begin(),indices.end(),uint16_t(236))>0,"Captured maximum index differs");
}
void capturedUv1Payload(const std::filesystem::path& directory) {
    const auto d=read(directory/"rejected-shadow-elements.bin");
    const auto v=read(directory/"rejected-shadow-vertices.bin");
    const auto i=read(directory/"rejected-shadow-indices.bin");
    need(d==declarationWithUv1()&&v.size()==20944&&i.size()==1890,"Unexpected live011 capture extent/declaration");
    const auto vertices=decodeCharacterVertices(v,d,56,3);
    const auto indices=decodeCharacterIndices(i);
    need(vertices.size()==374&&indices.size()==945,"Captured UV1 profile counts differ");
    bits(vertices.front().position,std::array<uint32_t,3>{0xBFDDAEE6,0x3F7985F0,0xB9D1B717});
    bits(vertices.back().position,std::array<uint32_t,3>{0x3FDC6DC6,0x3F7985F0,0xB9D1B717});
    for(const auto index:std::array<size_t,2>{0,373}) {
        bits(vertices[index].uv,std::array<uint32_t,2>{0x3EC3FE5C,0x3F0205BC});
        bits(vertices[index].weights,std::array<uint32_t,4>{0x3F19999A,0x3ECCCCCD,0,0});
        bits(vertices[index].indices,std::array<uint32_t,4>{0,0x3F800000,0,0});
    }
    need(std::count(indices.begin(),indices.end(),uint16_t(0xFFFF))==69,"Captured UV1 restart count differs");
    need(std::all_of(indices.begin(),indices.end(),[](uint16_t index){return index==0xFFFF||index<374;}),
         "Captured UV1 vertex index is out of range");
    need(std::count(indices.begin(),indices.end(),uint16_t(373))>0,"Captured UV1 maximum index differs");
}
}

int main(int argc,char** argv) {
    if(argc>2) {std::fprintf(stderr,"Usage: CharacterMeshTests [capture-directory]\n");return 2;}
    size_t failures=0;
    auto run=[&](const char* name,auto&& test) {
        try {test();std::printf("PASS %s\n",name);}
        catch(const std::exception& e) {++failures;std::fprintf(stderr,"FAIL %s: %s\n",name,e.what());}
    };
    run("values and ownership",valuesAndOwnership);
    run("additional UV1 ignored with exact consumed output",additionalUvValues);
    run("additional UV1 declaration and consumed guards",additionalUvValidation);
    run("bone bounds and signed zero",boneBounds);
    run("declaration validation",declarationValidation);
    run("finite attributes and extents",nonfiniteAndExtents);
    run("original stride regression",originalStrideRegression);
    run("BE16 indices and restart",indexBytes);
    if(argc==2)run("optional captured payload",[&]{
        const std::filesystem::path directory=argv[1];
        if(std::filesystem::exists(directory/"rejected-shadow-elements.bin"))capturedUv1Payload(directory);
        else capturedPayload(directory);
    });
    std::printf("Character mesh: %zu checks, %zu failing groups\n",checks,failures);
    return failures?1:0;
}
