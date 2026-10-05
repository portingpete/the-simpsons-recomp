#include "renderer/native_backend.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace Simpsons::Graphics;
void require(bool value,const char* reason) {if(!value) throw Error(reason);}
template<class F> void rejects(F action) {
    try {action();} catch(const Error&) {return;}
    throw Error("Invalid screen operation did not fail");
}
void close(float actual,float expected) {
    if(!std::isfinite(actual)||std::abs(actual-expected)>0.00003f) {
        char message[150];snprintf(message,sizeof(message),"Screen result mismatch: got %.9g expected %.9g",actual,expected);
        throw Error(message);
    }
}
std::vector<float> pixels(NativeBackend& backend,const std::shared_ptr<RenderTarget>& target) {
    auto bytes=backend.readbackTarget(target);std::vector<float> result(bytes.size()/sizeof(float));
    memcpy(result.data(),bytes.data(),bytes.size());return result;
}
void uniform(NativeBackend& backend,const std::shared_ptr<RenderTarget>& target,const std::array<float,4>& expected) {
    auto data=pixels(backend,target);
    for(size_t i=0;i<data.size();++i) close(data[i],expected[i%4]);
}
ScreenDraw quad() {
    ScreenDraw draw{};
    draw.vertices={ScreenVertex{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,-1,1,1}};
    draw.color={0.5f,0.25f,0.75f,0.25f};draw.blendSelector=3;draw.colorWriteMask=15;
    draw.alphaReference=1.0f/255.0f;
    draw.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    draw.sampler.AddressU=draw.sampler.AddressV=draw.sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    draw.sampler.MaxAnisotropy=1;draw.sampler.ComparisonFunc=D3D11_COMPARISON_ALWAYS;
    draw.sampler.MinLOD=0;draw.sampler.MaxLOD=D3D11_FLOAT32_MAX;
    return draw;
}
int main(int argc,char** argv) {
    try {
        bool hardware=argc==2 && std::string(argv[1])=="--hardware";
        NativeBackend backend(!hardware);
        auto target=backend.createTarget(9,5,TargetFormat::RGBA32Float);
        auto draw=quad();const std::array<float,4> background{0.2f,0.4f,0.6f,0.8f};
        uint64_t draws=0;
        auto submit=[&]{backend.drawScreen(target,draw);++draws;};
        // Flat arithmetic preserves each channel, HDR values and negative RGB.
        draw.color={-0.25f,2.0f,0.625f,0.75f};submit();uniform(backend,target,draw.color);
        draw=quad();
        for(uint32_t mode=0;mode<4;++mode) {
            backend.clearTarget(target,background);draw.blendSelector=mode;submit();
            auto expected=draw.color;
            for(unsigned c=0;c<3;++c) {
                float source=draw.color[c]*draw.color[3];
                if(mode==0) expected[c]=source+background[c];
                if(mode==1) expected[c]=source+background[c]*(1-draw.color[3]);
                if(mode==2) expected[c]=background[c]-source;
            }
            uniform(backend,target,expected);
        }
        // Effective color write mask preserves destination channels independently.
        draw.colorWriteMask=9;backend.clearTarget(target,background);submit();
        uniform(backend,target,{draw.color[0],background[1],background[2],draw.color[3]});
        draw.colorWriteMask=15;draw.alphaTest=true;
        for(float alpha:{0.0f,draw.alphaReference,std::nextafter(draw.alphaReference,1.0f)}) {
            draw.color[3]=alpha;backend.clearTarget(target,background);submit();
            uniform(backend,target,alpha>draw.alphaReference?draw.color:background);
        }
        // Finite clip-space geometry, Z=0/W=1: only the left half is covered.
        target=backend.createTarget(8,4,TargetFormat::RGBA32Float);
        draw=quad();draw.vertices[1].x=draw.vertices[3].x=0;
        backend.clearTarget(target,background);submit();
        auto data=pixels(backend,target);
        for(unsigned y=0;y<4;++y) for(unsigned x=0;x<8;++x) for(unsigned c=0;c<4;++c)
            close(data[(y*8+x)*4+c],x<4?draw.color[c]:background[c]);
        // Textured XY/UV layout, corner orientation and component-wise RGBA MUL.
        target=backend.createTarget(2,2,TargetFormat::RGBA32Float);draw=quad();
        const std::vector<uint8_t> texels={255,0,0,128, 0,255,0,64, 0,0,255,255, 64,128,192,32};
        draw.texture=backend.createTexture(2,2,TextureFormat::RGBA8,texels);
        draw.color={0.5f,0.25f,0.75f,0.5f};submit();
        data=pixels(backend,target);
        for(size_t i=0;i<texels.size();++i) close(data[i],(float(texels[i])/255.0f)*draw.color[i%4]);
        // Normalized repeat at exact texel centers; no half-texel addition guessed.
        for(auto& vertex:draw.vertices) {vertex.u+=1;vertex.v-=1;}submit();
        auto repeated=pixels(backend,target);
        for(size_t i=0;i<data.size();++i) close(repeated[i],data[i]);
        // Linear filtering: one pixel at normalized texture center averages four.
        target=backend.createTarget(1,1,TargetFormat::RGBA32Float);
        for(auto& vertex:draw.vertices) {vertex.u=0.5f;vertex.v=0.5f;}submit();
        data=pixels(backend,target);
        for(unsigned c=0;c<4;++c) {
            float expected=0;for(unsigned t=0;t<4;++t) expected+=float(texels[t*4+c])/255.0f/4.0f;
            close(data[c],expected*draw.color[c]);
        }
        // The API rejects unsupported inputs before any draw reaches the context.
        uint64_t before=backend.screenDrawCount();
        draw.blendSelector=4;rejects([&]{submit();});draw.blendSelector=3;
        draw.vertices[0].x=std::numeric_limits<float>::infinity();rejects([&]{submit();});draw.vertices[0].x=-1;
        NativeBackend other(!hardware);auto otherTarget=other.createTarget(1,1,TargetFormat::RGBA32Float);
        rejects([&]{backend.drawScreen(otherTarget,draw);});rejects([&]{other.readbackTarget(target);});
        draw.texture=other.createTexture(2,2,TextureFormat::RGBA8,texels);rejects([&]{submit();});
        require(backend.screenDrawCount()==before && before==draws,"Invalid submissions changed screen draw accounting");
        require(backend.presentationCount()==0,"Headless shader checks were counted as presentations");
        puts("Native screen shaders, RGBA arithmetic, coverage, four blend equations and alpha gate passed. Test fixtures only; no original game frames.");
        return 0;
    } catch(const std::exception& e) {fprintf(stderr,"Screen shader contract failure: %s\n",e.what());return 1;}
}
