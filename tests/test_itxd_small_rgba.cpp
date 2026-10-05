#include "renderer/itxd_blocks.h"
#include "renderer/native_backend.h"
#include "renderer/im2d_draw.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void need(bool v,const char* why){++checks;if(!v)throw Error(why);}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const Error&){failed=true;}need(failed,"Unqualified small RGBA profile accepted");}
std::vector<uint8_t> read(const char* path){
    std::ifstream input(path,std::ios::binary);need(bool(input),"Missing original small RGBA fixture");
    return {std::istreambuf_iterator<char>(input),{}};
}
void sampling(NativeBackend& backend,const std::shared_ptr<Texture>& texture,std::span<const uint8_t> expected){
    auto target=backend.createTarget(16,16,TargetFormat::RGB10A2);
    auto depth=backend.createDepthTarget(16,16);
    backend.bindTargets({target,nullptr,nullptr,nullptr},depth);
    backend.setViewport({0,0,16,16,0,1});backend.clearTarget(target,{0,0,0,0});
    Im2DDraw draw{};draw.primitiveType=4;draw.rasterWidth=draw.rasterHeight=16;
    draw.blendWord=0x00010001;draw.pixelCenterHalf=true;draw.colorWriteMask=15;draw.texture=texture;
    draw.vertices={{{.5f,.5f,0,1},{1,1,1,1},{0,0}},
                   {{16.5f,.5f,0,1},{1,1,1,1},{1,0}},
                   {{.5f,16.5f,0,1},{1,1,1,1},{0,1}},
                   {{16.5f,16.5f,0,1},{1,1,1,1},{1,1}}};
    draw.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    draw.sampler.AddressU=draw.sampler.AddressV=draw.sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    draw.sampler.MaxAnisotropy=1;draw.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
    backend.drawIm2D(target,depth,draw);
    const auto pixels=backend.readbackTarget(target);need(pixels.size()==16*16*4,"Small RGBA sampled target extent differs");
    for(size_t texel=0;texel<256;++texel){
        uint32_t actual{};std::memcpy(&actual,pixels.data()+4*texel,4);
        // Point sampling of source UNORM8, followed by the existing native
        // packed-target policy. Integer oracle has no console precision claim.
        uint32_t packed{};
        for(unsigned lane=0;lane<4;++lane){
            const uint32_t maximum=lane==3?3u:1023u;
            const uint32_t value=(uint32_t(expected[4*texel+lane])*maximum+127u)/255u;
            packed|=value<<(lane==3?30u:10u*lane);
        }
        need(actual==packed,"Small RGBA native sampler coordinate/channel differs from original pixels");
    }
}
}
int main(int argc,char** argv){try{
    need(argc==2||(argc==3&&!std::strcmp(argv[2],"--hardware")),"Expected small RGBA fixture and optional --hardware");
    const bool hardware=argc==3;
    const auto file=read(argv[1]);need(file.size()==24+4096+1024,"Original small RGBA fixture extent differs");
    std::array<uint32_t,6> d{};
    for(size_t i=0;i<6;++i)for(size_t b=0;b<4;++b)d[i]|=uint32_t(file[4*i+b])<<(8*b);
    need(d==std::array<uint32_t,6>{0x80400002,0x86,0x1E00F,0xC14,0,0x200},"Original 16x16 palette descriptor differs");
    const auto tiled=std::span(file).subspan(24,4096),expected=std::span(file).subspan(24+4096,1024);
    const auto decoded=decodeITXDCandyRGBA8(d,16,16,tiled);
    need(decoded.size()==1&&decoded[0].size()==expected.size(),"Small RGBA decoder authored level count/extent differs");
    for(size_t i=0;i<expected.size();++i)need(decoded[0][i]==expected[i],"Original 16x16 RGBA coordinate/channel differs from inverse oracle");
    // One physical texel exposes the endian/swizzle lane mapping independently.
    for(size_t lane=0;lane<4;++lane){
        auto changed=std::vector<uint8_t>(tiled.begin(),tiled.end());changed[lane]^=0x5A;
        const auto actual=decodeITXDCandyRGBA8(d,16,16,changed);
        for(size_t i=0;i<expected.size();++i)need(actual[0][i]==uint8_t(expected[i]^(i==(lane+3)%4?0x5A:0)),
             "Small RGBA physical lane mutation changed another texel/channel");
    }
    // Physical offset0x400 is outside this16x16 logical rectangle. Padding is
    // allocated storage, never another mip or a shifted logical palette image.
    auto changed=std::vector<uint8_t>(tiled.begin(),tiled.end());
    std::fill(changed.begin()+0x400,changed.begin()+0x404,0xA7);
    need(decodeITXDCandyRGBA8(d,16,16,changed)==decoded,"Small RGBA tile padding leaked into logical pixels");
    NativeBackend backend(!hardware);
    std::array<std::span<const uint8_t>,1> views{decoded[0]};
    const auto texture=backend.createTextureMipChain(16,16,TextureFormat::RGBA8,views);
    need(texture->levelCount()==1&&backend.readbackMip(texture,0)==decoded[0],"Small RGBA native upload/readback differs");
    sampling(backend,texture,expected);rejects([&]{backend.readbackMip(texture,1);});
    for(size_t field=0;field<6;++field)for(unsigned bit=0;bit<32;++bit){
        auto bad=d;bad[field]^=1u<<bit;
        rejects([&]{decodeITXDCandyRGBA8(bad,16,16,tiled);});
    }
    for(uint32_t bad:{0u,8u,15u,17u,32u,2048u,0xFFFFFFFFu}){
        rejects([&]{decodeITXDCandyRGBA8(d,bad,16,tiled);});
        rejects([&]{decodeITXDCandyRGBA8(d,16,bad,tiled);});
    }
    rejects([&]{decodeITXDCandyRGBA8(d,16,16,tiled.first(4095));});
    auto large=std::vector<uint8_t>(tiled.begin(),tiled.end());large.push_back(0);
    rejects([&]{decodeITXDCandyRGBA8(d,16,16,large);});
    rejects([&]{decodeITXDPaletteRGBA8(d,16,16,tiled);});
    need(read(argv[1])==file,"Original small RGBA fixture was modified");
    std::printf("PASS %zu original16x16 RGBA pixel/lane/padding/guard checks and %s upload/sampling\n",checks,hardware?"hardware":"WARP");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL small RGBA after %zu checks: %s\n",checks,e.what());return 1;}}
