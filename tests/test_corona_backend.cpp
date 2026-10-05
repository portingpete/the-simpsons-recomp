#include "renderer/native_backend.h"
#include "renderer/im2d_draw.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace Simpsons::Graphics;
void need(bool ok,const char* why){if(!ok)throw Error(why);}
uint32_t pixel(const std::vector<uint8_t>& data,size_t i){uint32_t v;std::memcpy(&v,data.data()+4*i,4);return v;}
int main(int argc,char** argv){try{
    NativeBackend b(!(argc==2&&std::strcmp(argv[1],"--hardware")==0));
    auto scene=b.createTarget(128,72,TargetFormat::RGB10A2),backup=b.createTarget(64,64,TargetFormat::RGB10A2),query=b.createTarget(64,8,TargetFormat::RGB10A2);
    auto depth=b.createDepthTarget(128,72),sampled=b.createDepthTarget(128,72);
    b.bindTargets({scene,nullptr,nullptr,nullptr},depth);b.setViewport({0,0,128,72,0,1});
    b.clearTarget(scene,{.25f,.5f,.75f,1});b.clearTarget(backup,{1,0,0,1});b.clearDepthTarget(depth,.25f,83);
    const auto sceneBefore=b.readbackTarget(scene),depthBefore=b.readbackDepthTarget(depth),backupBefore=b.readbackTarget(backup);
    std::array<CoronaVertex,3> vertices={{{{.5f/128,.5f/72},{64,36,16,8},{1.0f/128,1.0f/72,.5f}},
        {{31.5f/128,3.5f/72},{64,36,16,8},{1.0f/128,1.0f/72,.5f}},
        {{63.5f/128,7.5f/72},{64,36,16,8},{1.0f/128,1.0f/72,.5f}}}};
    auto draw=[&]{b.drawCoronaQueries(scene,depth,sampled,backup,query,vertices,6);
        b.requireSelectedTargets({scene,nullptr,nullptr,nullptr},depth);
        need(b.readbackTarget(scene)==sceneBefore&&b.readbackDepthTarget(depth)==depthBefore,"Corona pass changed scene/depth pixels");};
    b.clearDepthTarget(sampled,0,0);draw();auto q=b.readbackTarget(query);
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<64;++x){const bool covered=(x==0&&y==0)||(x==31&&y==3)||(x==63&&y==7);
        need(pixel(q,y*64+x)==(covered?0xFFFFFFFF:pixel(sceneBefore,y*128+x)),"Corona point position, visible result or untouched tile differs");}
    const auto saved=b.readbackTarget(backup);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)
        need(pixel(saved,y*64+x)==(y<8?pixel(sceneBefore,y*128+x):pixel(backupBefore,y*64+x)),"Corona backup changed outside the saved tile");
    for(float z:{.5f,.75f}){b.clearDepthTarget(sampled,z,0);draw();q=b.readbackTarget(query);
        for(unsigned at:{0u,3*64+31u,7*64+63u})need(pixel(q,at)==0xC0000000,"Corona hidden/equal-depth result differs");}
    // Produce a real depth discontinuity with the qualified flat native draw.
    b.clearDepthTarget(sampled,0,0);Im2DDraw occluder{};occluder.primitiveType=4;occluder.rasterWidth=128;occluder.rasterHeight=72;
    occluder.blendWord=0x10001;occluder.pixelCenterHalf=true;occluder.depthTest=occluder.depthWrite=true;occluder.depthCompare=7;
    for(auto xy:std::array<std::array<float,2>,4>{{{64.5f,.5f},{128.5f,.5f},{64.5f,72.5f},{128.5f,72.5f}}})
        occluder.vertices.push_back({{xy[0],xy[1],.75f,1},{1,1,1,1},{0,0}});
    b.bindTargets({scene,nullptr,nullptr,nullptr},sampled);b.drawIm2D(scene,sampled,occluder);
    b.bindTargets({scene,nullptr,nullptr,nullptr},depth);draw();q=b.readbackTarget(query);
    // Independent weighted-area formula, using known left/right scene depth.
    double total=0,hidden=0;
    for(int y=-8;y<=8;++y)for(int x=-8;x<=8;++x){const double weight=std::max(0.0,1.0-std::sqrt(double(x*x+y*y))/8.0);
        total+=weight;if(x>=0)hidden+=weight;}
    const auto expected=unsigned(std::nearbyint((1-hidden/total)*1023));
    for(unsigned at:{0u,3*64+31u,7*64+63u}){const auto actual=pixel(q,at)&1023;
        if(std::abs(int(actual)-int(expected))>1)std::fprintf(stderr,"partial actual=%u expected=%u\n",actual,expected);
        need(std::abs(int(actual)-int(expected))<=1,"Corona weighted partial visibility differs");}
    // The consumer applies visibility to RGBA before SRC_ALPHA/ONE blending.
    ScreenDraw sprite{};sprite.vertices={{{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,-1,1,1}}};sprite.color={1,1,1,1};sprite.colorWriteMask=15;
    const uint8_t white[]={255,255,255,255};sprite.texture=b.createTexture(1,1,TextureFormat::RGBA8,white);
    sprite.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sprite.sampler.AddressU=sprite.sampler.AddressV=sprite.sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    sprite.sampler.MaxAnisotropy=1;sprite.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sprite.coronaQuery=query;sprite.coronaUV={.5f/64,.5f/8};
    b.clearTarget(scene,{0,0,0,0});b.drawOriginalScreen(scene,depth,sprite,false,6,false);
    const float visibility=float(pixel(q,0)&1023)/1023;const auto code=unsigned(std::nearbyint(visibility*visibility*1023));
    const auto result=b.readbackTarget(scene);for(unsigned i=0;i<128*72;++i)need((pixel(result,i)&1023)==code,"Corona sprite modulation/blend differs");
    need(b.readbackDepthTarget(depth)==depthBefore,"Corona sprite changed scene depth");
    std::puts("PASS corona visible/hidden/equality/weighted partial coverage, tile preservation and sprite modulation");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
