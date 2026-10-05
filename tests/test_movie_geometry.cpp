// Standalone C++20 CPU test: this file + renderer/movie_geometry.cpp only.
// argv[1] is the immutable, VA-mapped analysis/simpsons.pe. No Runtime/device.
#include "renderer/movie_geometry.h"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void need(bool ok,const char* why) {++checks;if(!ok)throw std::runtime_error(why);}
template<class F> void rejects(F&& action) {
    bool rejected=false;try {action();}catch(const MovieGeometryError&) {rejected=true;}
    need(rejected,"Unqualified movie geometry accepted");
}

constexpr uint32_t imageBase=0x82000000,codeStart=0x8282E548,codeEnd=0x8282E6C8;
// ALL words from width fetch through End, including non-taken stores/branches.
// Primary image pins, independently checked against original184 disassembly.
constexpr std::array<uint32_t,96> codePins={
    0x817E0008,0x816B000C,0x2F0B0280,0x39600001,0x41990008,0x39600000,
    0x556B063E,0x807FCAF8,0x38C00010,0x2B0B0000,0x38A00003,0x38800008,
    0x409A00A0,0x4BC1DED5,0x897E0041,0x2B0B0000,0x3D60821E,0xC00BD110,
    0x3D60821E,0xD0030000,0xD0030004,0xD0030014,0xD0030020,0x419A0044,
    0xC18BD0D8,0x3D608200,0xD1830008,0xD1830028,0xC16B1894,0x3D608200,
    0xD163000C,0xD163001C,0xC1AB0BB0,0x3D608200,0xD1A30010,0xD1A30018,
    0xD1A30024,0xC00B36E8,0xD003002C,0x480000DC,0xC1ABD0D8,0x3D608200,
    0xD1A30008,0xD1A3000C,0xD1A3001C,0xD1A30028,0xC18B0BB0,0xD1830010,
    0xD1830018,0xD1830024,0xD183002C,0x480000AC,0x897E0041,0x2B0B0000,
    0x419A0044,0x4BC1DE2D,0x3D60821E,0xC00BD110,0x3D60821E,0xC1ABD0D8,
    0x3D608200,0xD1A30008,0xD1A3000C,0xD1A3001C,0xD1A30028,0xC18B0BB0,
    0xD1830010,0xD1830018,0xD1830024,0xD183002C,0x48000050,0x4BC1DDED,
    0x3D60821E,0xC00BD110,0x3D608207,0xC18BA014,0x3D60821E,0xD1830008,
    0xD1830028,0xC16BD0D8,0x3D608200,0xD163000C,0xD163001C,0xC1AB0BB0,
    0x3D608216,0xD1A30010,0xD1A30024,0xD1A3002C,0xC14BD498,0xD1430018,
    0xD0030020,0xD0030014,0xD0030004,0xD0030000,0x807FCAF8,0x4BC1E22D,
};
constexpr std::array<std::array<uint32_t,2>,7> dataPins={{
    {0x821DD110,0xBF800000},{0x821DD0D8,0x00000000},{0x82000BB0,0x3F800000},
    {0x82001894,0x3DCCCCCD},{0x820036E8,0x3F666666},{0x8206A014,0x3E000000},
    {0x8215D498,0x3F600000},
}};
uint32_t word(const std::vector<uint8_t>& image,uint32_t address) {
    need(address>=imageBase && uint64_t(address-imageBase)+4<=image.size(),"Original read out of range");
    const auto* p=image.data()+(address-imageBase);
    return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
}
void pinOriginal(const std::vector<uint8_t>& image) {
    need(image.size()==15466496,"Original mapped image size differs");
    for(size_t i=0;i<codePins.size();++i)
        need(word(image,codeStart+uint32_t(i)*4)==codePins[i],"Original geometry instruction differs");
    for(const auto& pin:dataPins)need(word(image,pin[0])==pin[1],"Original float literal differs");
    constexpr std::array<uint32_t,9> declaration={
        0x00000000,0x002C23A5,0x00000000,0x00000008,0x002C23A5,0x00050000,
        0x00FF0000,0xFFFFFFFF,0x00000000};
    for(size_t i=0;i<declaration.size();++i)
        need(word(image,0x82151724+uint32_t(i)*4)==declaration[i],"Original XY/UV declaration differs");
    need(word(image,0x8282E3F8)==0x38800001 && word(image,0x8282E404)==0x4BC0D315,
         "Movie half-pixel1 request differs");
}

struct Store {uint32_t pc,offset,source,bits;};
struct Trace {
    std::array<uint8_t,48> bytes{};
    std::vector<Store> stores;
    uint32_t beginPc{};
};
int32_t signed16(uint32_t value) {
    return (value&0x8000)?int32_t(value&0xFFFF)-0x10000:int32_t(value&0xFFFF);
}
// Deliberately independent of buildMovieGeometry and its branch/profile table.
// Decode the pinned ORIGINAL instruction stream; execute only integer control
// flow, literal lfs and stfs. SDK Begin is an allocation-only oracle boundary.
// It validates primitive/count/stride and poisons volatile GPR/FPR contents;
// it does not emulate a device, shader, command buffer or original rasterizer.
Trace traceOriginal(const std::vector<uint8_t>& image,int32_t width,uint8_t flag) {
    constexpr uint32_t presenter=0x10000,luma=0x20000,buffer=0x30000;
    std::array<uint32_t,32> r{},f{},source{};
    std::array<bool,32> cr{},loaded{};
    std::array<bool,12> written{};
    r[30]=presenter;r[31]=0x82D10000;
    Trace trace;trace.bytes.fill(0xCD);
    auto memory=[&](uint32_t address) {
        if(address==presenter+8)return luma;
        if(address==luma+12)return std::bit_cast<uint32_t>(width);
        if(address==0x82D0CAF8)return uint32_t{0}; // Device identity not consumed.
        throw std::runtime_error("Unexpected original lwz address");
    };
    uint32_t pc=codeStart,steps=0;
    while(pc!=0x8282E6C0) {
        need(++steps<128 && pc>=codeStart && pc<codeEnd && !(pc&3),"Original trace escaped bounded code");
        const auto w=word(image,pc),op=w>>26,rt=(w>>21)&31,ra=(w>>16)&31;
        const auto address=(ra?r[ra]:0)+uint32_t(signed16(w));
        auto next=pc+4;
        if(op==14)r[rt]=address; // addi (li)
        else if(op==15)r[rt]=(ra?r[ra]:0)+(uint32_t(signed16(w))<<16); // addis (lis)
        else if(op==32)r[rt]=memory(address);
        else if(op==34) {
            need(address==presenter+0x41,"Unexpected original lbz address");r[rt]=flag;
        }else if(op==10 || op==11) {
            const auto field=((w>>23)&7)*4;
            const int64_t a=op==11?int64_t(std::bit_cast<int32_t>(r[ra])):int64_t(r[ra]);
            const int64_t b=op==11?int64_t(signed16(w)):int64_t(w&0xFFFF);
            cr[field]=a<b;cr[field+1]=a>b;cr[field+2]=a==b;cr[field+3]=false;
        }else if(op==21) {
            need(w==0x556B063E,"Unexpected original rotate/mask");r[ra]=r[rt]&0xFF;
        }else if(op==16) {
            const auto bo=(w>>21)&31,bi=(w>>16)&31;
            need((bo==4 || bo==12) && !(w&3),"Unexpected conditional branch form");
            if(cr[bi]==(bo==12))next=pc+uint32_t(signed16(w&0xFFFC));
        }else if(op==18) {
            need(!(w&2),"Unexpected absolute branch");
            const auto delta=w&0x03FFFFFC;
            const auto target=pc+((delta&0x02000000)?delta|0xFC000000:delta);
            if(w&1) {
                need(target==0x8244C450 && !trace.beginPc,"Unexpected/multiple SDK calls");
                need(r[4]==8 && r[5]==3 && r[6]==16,"Original primitive8/count3/stride16 differs");
                trace.beginPc=pc;
                std::fill_n(r.begin(),13,0xDEADBEEF);loaded.fill(false);r[3]=buffer;
            }else next=target;
        }else if(op==48) {
            need(std::any_of(dataPins.begin(),dataPins.end(),[&](const auto& p){return p[0]==address;}),
                 "Original lfs did not read a pinned literal");
            f[rt]=word(image,address);source[rt]=address;loaded[rt]=true;
        }else if(op==52) {
            need(trace.beginPc && loaded[rt] && address>=buffer && address<buffer+48 && !(address&3),
                 "Uninitialized float or out-of-range original stfs");
            const auto offset=address-buffer;
            need(!written[offset/4],"Original vertex word overwritten");written[offset/4]=true;
            for(uint32_t j=0;j<4;++j)trace.bytes[offset+j]=uint8_t(f[rt]>>(24-8*j));
            trace.stores.push_back({pc,offset,source[rt],f[rt]});
        }else throw std::runtime_error("Unexpected opcode in original geometry trace");
        pc=next;
    }
    need(trace.stores.size()==12 && std::all_of(written.begin(),written.end(),[](bool b){return b;}),
         "Original trace did not write all 48 bytes exactly once");
    return trace;
}

std::array<uint32_t,4> bits(const MovieVertex& v) {
    return {std::bit_cast<uint32_t>(v.x),std::bit_cast<uint32_t>(v.y),
            std::bit_cast<uint32_t>(v.u),std::bit_cast<uint32_t>(v.v)};
}
std::array<uint8_t,48> bigEndianBytes(const std::array<MovieVertex,3>& vertices) {
    std::array<uint8_t,48> result{};size_t at=0;
    for(const auto& v:vertices)for(auto w:bits(v))for(int shift:{24,16,8,0})result[at++]=uint8_t(w>>shift);
    return result;
}
void compare(const std::vector<uint8_t>& image,int32_t width,uint8_t flag) {
    const auto traced=traceOriginal(image,width,flag);
    const auto geometry=buildMovieGeometry(width,flag);
    need(bigEndianBytes(geometry.originalVertices)==traced.bytes,"Native original 48 bytes differ from stfs execution");
    std::array<uint32_t,12> hostWords{};
    for(const auto& s:traced.stores)hostWords[s.offset/4]=s.bits;
    need(std::memcmp(geometry.originalVertices.data(),hostWords.data(),48)==0,"Native XYUV layout/bit patterns differ");
    need(std::memcmp(geometry.originalVertices.data(),geometry.nativeVertices.data(),48)==0,
         "Expansion changed original first three vertices");
    const std::array<uint32_t,4> fourth={hostWords[4],hostWords[9],hostWords[6],hostWords[11]};
    need(bits(geometry.nativeVertices[3])==fourth,"Fourth corner lost original XY/UV endpoint bits");
    const auto expanded=expandMovieRectangle(geometry.originalVertices);
    need(std::memcmp(expanded.data(),geometry.nativeVertices.data(),64)==0,"Direct expansion and builder disagree");
}

void fourBranchStoreOrder(const std::vector<uint8_t>& image) {
    // Hand-transcribed PCs from original184, not implementation-generated data.
    constexpr std::array<std::array<uint32_t,12>,4> stores={{
        {0x8282E594,0x8282E598,0x8282E59C,0x8282E5A0,0x8282E5F0,0x8282E5F4,
         0x8282E5F8,0x8282E5FC,0x8282E604,0x8282E608,0x8282E60C,0x8282E610},
        {0x8282E594,0x8282E598,0x8282E59C,0x8282E5A0,0x8282E5B0,0x8282E5B4,
         0x8282E5C0,0x8282E5C4,0x8282E5D0,0x8282E5D4,0x8282E5D8,0x8282E5E0},
        {0x8282E67C,0x8282E680,0x8282E68C,0x8282E690,0x8282E69C,0x8282E6A0,
         0x8282E6A4,0x8282E6AC,0x8282E6B0,0x8282E6B4,0x8282E6B8,0x8282E6BC},
        {0x8282E63C,0x8282E640,0x8282E644,0x8282E648,0x8282E650,0x8282E654,
         0x8282E658,0x8282E65C,0x8282E6B0,0x8282E6B4,0x8282E6B8,0x8282E6BC},
    }};
    constexpr std::array<uint32_t,4> begin={0x8282E57C,0x8282E57C,0x8282E664,0x8282E624};
    for(size_t i=0;i<4;++i) {
        const auto traced=traceOriginal(image,i<2?640:1280,uint8_t(i%2));
        need(traced.beginPc==begin[i],"Original branch selected wrong Begin");
        for(size_t j=0;j<12;++j)need(traced.stores[j].pc==stores[i][j],"Original store assignment/order differs");
    }
}

// Test BOTH triangles against the independent full-rectangle interpolation
// plane. Double arithmetic is only a mathematical test, not a GPU precision
// claim. Samples avoid all edges and the shared diagonal.
void fullRectangleCoverage() {
    constexpr int triangles[2][3]={{0,1,2},{2,1,3}};
    for(int32_t width:{640,1280})for(uint8_t flag:{uint8_t{0},uint8_t{1}}) {
        const auto q=buildMovieGeometry(width,flag).nativeVertices;
        // The longest edge is 1--2: squared lengths 8,4,4 select strip0123.
        const auto a=bits(q[0]),b=bits(q[1]),c=bits(q[2]),d=bits(q[3]);
        for(size_t component=0;component<4;++component) {
            // Reference emits subtraction then addition, not (b+c)-a.
            // All intermediates here are exact binary32, including A-A=0.
            const float delta=std::bit_cast<float>(b[component])-std::bit_cast<float>(a[component]);
            const float completed=delta+std::bit_cast<float>(c[component]);
            need(std::bit_cast<uint32_t>(completed)==d[component],"Endpoint copy differs from reference primitive8 arithmetic");
        }
        for(int iy=0;iy<7;++iy)for(int ix=0;ix<9;++ix) {
            const double x=-1+2*(ix+0.375)/9,y=-1+2*(iy+0.625)/7;
            int covered=0;
            for(const auto& tri:triangles) {
                const auto &a=q[tri[0]],&b=q[tri[1]],&c=q[tri[2]];
                const double determinant=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);
                need(determinant==4,"Native strip winding/area differs");
                const double wa=((b.y-c.y)*(x-c.x)+(c.x-b.x)*(y-c.y))/determinant;
                const double wb=((c.y-a.y)*(x-c.x)+(a.x-c.x)*(y-c.y))/determinant;
                const double wc=1-wa-wb;
                if(wa<0 || wb<0 || wc<0)continue;
                ++covered;
                const double u=wa*a.u+wb*b.u+wc*c.u,v=wa*a.v+wb*b.v+wc*c.v;
                const double eu=q[0].u+(x+1)*0.5*(double(q[1].u)-q[0].u);
                const double ev=q[0].v+(y+1)*0.5*(double(q[2].v)-q[0].v);
                need(u-eu<1e-14 && eu-u<1e-14 && v-ev<1e-14 && ev-v<1e-14,
                     "Completed rectangle changed original affine UV interpolation");
            }
            need(covered==1,"Native rectangle has a missing or overlapping triangle");
        }
    }
    // Observed boot150 width1280, presenter+41=1. Preserve the literal Y/V
    // relationship; do not substitute the ordinary screen path's corner order.
    const auto live=buildMovieGeometry(1280,1);
    constexpr std::array<uint32_t,16> expected={
        0xBF800000,0xBF800000,0,0, 0x3F800000,0xBF800000,0x3F800000,0,
        0xBF800000,0x3F800000,0,0x3F800000, 0x3F800000,0x3F800000,0x3F800000,0x3F800000};
    need(std::memcmp(live.nativeVertices.data(),expected.data(),64)==0,"Boot150 full UV rectangle differs");
}

void validation() {
    const auto good=buildMovieGeometry(640,0).originalVertices;
    std::array<MovieVertex,4> tooMany{};
    for(size_t count:{size_t{0},size_t{1},size_t{2},size_t{4}})
        rejects([&]{expandMovieRectangle(std::span<const MovieVertex>(tooMany.data(),count));});
    // Flip every individual bit of all three supported profiles, including
    // signed-zero, one-ULP endpoints, non-rectangular XY and changed UV edges.
    for(const auto& profile:std::array{buildMovieGeometry(640,0),buildMovieGeometry(640,1),buildMovieGeometry(1280,0)}) {
        const auto words=std::bit_cast<std::array<uint32_t,12>>(profile.originalVertices);
        for(size_t i=0;i<12;++i)for(uint32_t bit=0;bit<32;++bit) {
            auto changed=words;changed[i]^=uint32_t{1}<<bit;
            const auto bad=std::bit_cast<std::array<MovieVertex,3>>(changed),before=bad;
            rejects([&]{expandMovieRectangle(bad);});
            need(std::memcmp(bad.data(),before.data(),48)==0,"Rejected expansion modified source");
        }
    }
    for(uint32_t nonfinite:{0x7F800000u,0xFF800000u,0x7FC00000u,0x7FA12345u})for(size_t i=0;i<12;++i) {
        auto words=std::bit_cast<std::array<uint32_t,12>>(good);words[i]=nonfinite;
        const auto bad=std::bit_cast<std::array<MovieVertex,3>>(words);
        rejects([&]{expandMovieRectangle(bad);});
    }
    auto flipped=good;std::swap(flipped[0],flipped[2]);rejects([&]{expandMovieRectangle(flipped);});
    auto bothCrops=buildMovieGeometry(1280,0).originalVertices;
    const auto vCrop=buildMovieGeometry(640,1).originalVertices;
    for(size_t i=0;i<3;++i)bothCrops[i].v=vCrop[i].v;
    rejects([&]{expandMovieRectangle(bothCrops);});
    auto mutableInput=good;const auto before=mutableInput;
    const auto owned=expandMovieRectangle(mutableInput);
    need(std::memcmp(mutableInput.data(),before.data(),48)==0,"Successful expansion modified input");
    mutableInput={};need(owned[0].x==-1 && owned[3].u==1,"Expansion retained input lifetime");
}

void roundingIndependence() {
    struct SavedEnvironment {
        std::fenv_t saved;
        SavedEnvironment() {need(!std::fegetenv(&saved),"Cannot capture FP environment");}
        ~SavedEnvironment() {std::fesetenv(&saved);}
    } saved;
    for(int32_t width:{640,1280})for(uint8_t flag:{uint8_t{0},uint8_t{1}}) {
        const auto expected=buildMovieGeometry(width,flag);
        for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}) {
            need(!std::fesetround(mode),"Cannot set test rounding mode");
            need(!std::feclearexcept(FE_ALL_EXCEPT),"Cannot clear FP exceptions");
            const auto result=buildMovieGeometry(width,flag);
            need(std::fegetround()==mode && !std::fetestexcept(FE_ALL_EXCEPT),"Geometry performed FP arithmetic/changed environment");
            need(std::memcmp(result.originalVertices.data(),expected.originalVertices.data(),48)==0 &&
                 std::memcmp(result.nativeVertices.data(),expected.nativeVertices.data(),64)==0,
                 "Literal geometry inherited caller rounding mode");
        }
    }
}
}

int main(int argc,char** argv)try {
    need(argc==2,"Usage: MovieGeometryTests <immutable analysis/simpsons.pe>");
    std::ifstream stream(argv[1],std::ios::binary);need(bool(stream),"Cannot open original image read-only");
    const std::vector<uint8_t> image((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());
    pinOriginal(image);fourBranchStoreOrder(image);
    for(int32_t width:{INT32_MIN,-1,0,1,639,640,641,1280,INT32_MAX})
        for(uint32_t flag=0;flag<256;++flag)compare(image,width,uint8_t(flag));
    fullRectangleCoverage();validation();roundingIndependence();
    std::printf("PASS: %zu checks; original code/data pins, 2304 independent PPC store traces, exact 48-byte input, complete native rectangle and validation\n",checks);
    return 0;
}catch(const std::exception& e) {
    std::fprintf(stderr,"FAIL after %zu checks: %s\n",checks,e.what());return 1;
}
