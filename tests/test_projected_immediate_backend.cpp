#include "renderer/immediate_draw.h"
#include "renderer/device_availability.h"
#include <d3d11sdklayers.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace Simpsons::Graphics {
struct NativeIm2DProbe {
    static ID3D11DeviceContext* context(NativeBackend& b){return b.context.Get();}
    static ID3D11Device* device(NativeBackend& b){return b.device.Get();}
    static void debug(NativeBackend& b,bool hardware){
        ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL level;
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        if(SUCCEEDED(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,
            D3D11_CREATE_DEVICE_DEBUG,levels,2,D3D11_SDK_VERSION,&d,&level,&c))){
            b.device=d;b.context=c;b.featureLevel=level;b.availability=std::make_shared<DeviceAvailability>(d.Get());
        }
    }
};
}
using namespace Simpsons::Graphics;
namespace {
size_t checks=0,cases=0;
void need(bool value,const char* why){++checks;if(!value)throw Error(why);}
uint32_t word(const std::vector<uint8_t>& bytes,size_t at){uint32_t value;std::memcpy(&value,bytes.data()+at*4,4);return value;}
float fraction(float x){return x-std::floor(x);}
unsigned mirror(float x){float q=std::fmod(x,2.0f);if(q<0)q+=2;if(q>1)q=2-q;return std::min(1023u,unsigned(std::floor(q*1024)));}
float visibility(unsigned mask,float u,float v,float z){
    const float threshold=1-std::clamp(z,0.0f,1.0f);
    auto tap=[&](float x,float y){const unsigned bit=(mirror(v+y/1024)>=512?2u:0u)+(mirror(u+x/1024)>=512?1u:0u);
        return threshold>=((mask&(1u<<bit))?.75f:.25f)?1.0f:0.0f;};
    const float fx=fraction(u*1024-.5f),fy=fraction(v*1024-.5f);
    const float left=tap(-.5f,-.5f)*(1-fy)+tap(-.5f,.5f)*fy;
    const float right=tap(.5f,-.5f)*(1-fy)+tap(.5f,.5f)*fy;
    return left*(1-fx)+right*fx;
}
void quad(ImmediateDraw& d,float x0,float y0,float x1,float y1,float z){
    const ImmediateVertex a{{x0,y0,z},.75f,{.2f,.3f,0,0}},b{{x0,y1,z},.75f,{.2f,.3f,0,0}},
        c{{x1,y0,z},.75f,{.2f,.3f,0,0}},e{{x1,y1,z},.75f,{.2f,.3f,0,0}};
    d.vertices.insert(d.vertices.end(),{a,b,c,c,b,e});
}
}
int main(int argc,char** argv){try{
    const bool hardware=argc==2&&!std::strcmp(argv[1],"--hardware");need(argc==1||hardware,"Usage: ProjectedImmediateBackendTests [--hardware]");
    NativeBackend b(!hardware);NativeIm2DProbe::debug(b,hardware);auto* context=NativeIm2DProbe::context(b);
    auto color=b.createTarget(16,16,TargetFormat::RGB10A2),shadowColor=b.createTarget(1024,1024,TargetFormat::RGB10A2);
    auto depth=b.createDepthTarget(16,16),shadow=b.createDepthTarget(1024,1024);
    ImmediateDraw d{};d.viewport={0,0,16,16,0,0x3F800000};d.viewportEnable=d.halfPixelOffset=1;
    d.depthEnable=d.depthWrite=1;d.depthCompare=7;d.colorMask=15;d.multisampleMask=0xFFFFFFFF;
    d.blendWord=0x10001;d.blendEnable=d.alphaTest=0;d.primitive=4;
    const std::array<uint8_t,4> texel={80,160,48,192};d.texture=b.createTexture(1,1,TextureFormat::RGBA8,texel);
    d.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;d.sampler.AddressU=d.sampler.AddressV=d.sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    d.sampler.MaxAnisotropy=1;d.sampler.MaxLOD=13;d.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
    for(unsigned i=0;i<4;++i)d.constants[i][i]=1;d.constants[4]={.3f,.4f,.5f,.6f};
    quad(d,-.5f,.5f,.5f,-.5f,.5f);d.projected=true;d.projectionDepth=shadow;
    d.projectionSampler=d.sampler;d.projectionSampler.AddressU=d.projectionSampler.AddressV=D3D11_TEXTURE_ADDRESS_MIRROR;
    // A real native depth attachment supplies all four original channels through
    // the explicitly qualified RRRR adapter. No synthetic RGBA shadow texture.
    auto fill=d;fill.projected=false;fill.projectionDepth.reset();fill.viewport={0,0,1024,1024,0,0x3F800000};fill.colorMask=0;
    auto shadowPattern=[&](unsigned mask){
        fill.vertices.clear();for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)
            quad(fill,float(x)-1,1-float(y),float(x),-float(y),(mask&(1u<<(2*y+x)))?.75f:.25f);
        b.bindTargets({shadowColor,nullptr,nullptr,nullptr},shadow);b.setViewport({0,0,1024,1024,0,1});
        b.clearTarget(shadowColor,{0,0,0,0});b.clearDepthTarget(shadow,0,0xA5);b.drawImmediate(shadowColor,shadow,fill);
        b.bindTargets({color,nullptr,nullptr,nullptr},depth);b.setViewport({0,0,16,16,0,1});
    };
    const std::array<std::array<float,2>,5> coords={{{511.7f/1024,511.8f/1024},{512.2f/1024,512.3f/1024},
        {511.5f/1024,511.5f/1024},{-511.7f/1024,511.8f/1024},{1+511.7f/1024,1+511.8f/1024}}};
    const auto originalVertices=d.vertices;
    for(auto& vertex:d.vertices){vertex.uv[2]=std::numeric_limits<float>::quiet_NaN();vertex.uv[3]=std::numeric_limits<float>::infinity();}
    const auto poisonedVertices=d.vertices;
    for(unsigned mask=0;mask<16;++mask){shadowPattern(mask);
        for(auto uv:coords)for(float z:{-.25f,.25f,.5f,.9f,1.25f})for(float ambient:{0.0f,.4f,1.0f}){
            d.projectionConstants={};d.projectionConstants[3]={2*uv[0]-1,1-2*uv[1],z,1};d.projectionConstants[4][0]=ambient;
            b.clearTarget(color,{0,0,0,0});b.clearDepthTarget(depth,0,0x5A);b.drawImmediate(color,depth,d);
            const auto output=b.readbackTarget(color);const float light=ambient+(1-ambient)*visibility(mask,uv[0],uv[1],z);
            std::array<unsigned,4> expected{};for(unsigned lane=0;lane<4;++lane)
                expected[lane]=unsigned(std::nearbyint(float(texel[lane])/255*d.constants[4][lane]*(lane<3?light:.75f)*(lane<3?1023:3)));
            for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){const auto value=word(output,16*y+x);
                if(x<4||x>=12||y<4||y>=12){need(value==0,"Projected immediate coverage differs");continue;}
                for(unsigned lane=0;lane<4;++lane){const auto actual=(value>>(lane*10))&(lane<3?1023u:3u);
                    if(std::abs(int(actual)-int(expected[lane]))>(lane<3?1:0)){std::fprintf(stderr,"projected mask=%u uv=%g,%g z=%g ambient=%g lane=%u actual=%u expected=%u\n",mask,uv[0],uv[1],z,ambient,lane,actual,expected[lane]);throw Error("Projected immediate shadow arithmetic differs");}++checks;}
            }
            ++cases;
        }
    }
    need(!std::memcmp(d.vertices.data(),poisonedVertices.data(),d.vertices.size()*sizeof(ImmediateVertex)),"Projected draw mutated unused original UV lanes");
    // A nontrivial homogeneous matrix makes projection vary over the geometry.
    // W is two, XY scales differ, and translated coordinates cross tap boundaries.
    shadowPattern(5);const auto shadowBefore=b.readbackDepthTarget(shadow);
    d.projectionConstants={};d.projectionConstants[0][0]=1.0f/256;d.projectionConstants[1][1]=1.0f/128;
    d.projectionConstants[3]={-1.0f/1024,1.0f/2048,1,2};d.projectionConstants[4][0]=.25f;
    b.clearTarget(color,{0,0,0,0});b.drawImmediate(color,depth,d);const auto transformed=b.readbackTarget(color);
    for(unsigned y=4;y<12;++y)for(unsigned x=4;x<12;++x){
        const float px=(float(x)+.5f)/8-1,py=1-(float(y)+.5f)/8;
        const float u=(px/256-1.0f/1024)*.25f+.5f,v=-(py/128+1.0f/2048)*.25f+.5f;
        const float light=.25f+.75f*visibility(5,u,v,.5f);const auto value=word(transformed,16*y+x);
        for(unsigned lane=0;lane<3;++lane)need(std::abs(int((value>>(10*lane))&1023)-int(std::nearbyint(float(texel[lane])/255*d.constants[4][lane]*light*1023)))<=1,
            "Projected immediate homogeneous transform or interpolation differs");
    }
    // Negative c25 skips the matrix and emits zero projection, producing full
    // visibility for normalized native depth. Unused matrix lanes may be poison.
    const auto saved=d;d.projectionConstants[4][0]=-1;
    for(unsigned row=0;row<4;++row)for(auto& value:d.projectionConstants[row])value=std::numeric_limits<float>::quiet_NaN();
    auto flat=d;flat.projected=false;flat.projectionDepth.reset();
    b.clearTarget(color,{0,0,0,0});b.drawImmediate(color,depth,flat);const auto flatOutput=b.readbackTarget(color);
    b.clearTarget(color,{0,0,0,0});b.drawImmediate(color,depth,d);need(b.readbackTarget(color)==flatOutput,"Projected negative predicate changed flat output");
    d=saved;
    // Projected dual keeps the second UV pair and multiplies its RGBA after
    // the complete shadow/base-color calculation, including alpha.
    b.clearTarget(color,{0,0,0,0});b.drawImmediate(color,depth,d);const auto singleOutput=b.readbackTarget(color);
    const std::array<uint8_t,16> secondPixels={255,0,0,255,0,255,0,128,0,0,255,64,255,255,255,0};
    auto dual=d;dual.texture2=b.createTexture(2,2,TextureFormat::RGBA8,secondPixels);
    for(unsigned selection=0;selection<4;++selection){
        for(auto& vertex:dual.vertices){vertex.uv[2]=(selection&1)?.75f:.25f;vertex.uv[3]=(selection&2)?.75f:.25f;}
        b.clearTarget(color,{0,0,0,0});b.drawImmediate(color,depth,dual);const auto dualOutput=b.readbackTarget(color);
        for(unsigned y=4;y<12;++y)for(unsigned x=4;x<12;++x){const auto value=word(dualOutput,16*y+x),single=word(singleOutput,16*y+x);
            for(unsigned lane=0;lane<3;++lane)need(((value>>(10*lane))&1023)==(secondPixels[4*selection+lane]?((single>>(10*lane))&1023):0),
                "Projected dual texture stage, UV or RGB modulation differs");
            const auto alpha=unsigned(std::nearbyint(float(texel[3])/255*d.constants[4][3]*.75f*float(secondPixels[4*selection+3])/255*3));
            need((value>>30)==alpha,"Projected dual alpha modulation differs");
        }
    }
    // The original decal texture helper can clear stage zero. Keep the exact
    // projected shader and shadow input; its sampled base RGBA is then zero.
    auto nullBase=d;nullBase.nullTexture=true;nullBase.texture.reset();nullBase.sampler={};
    nullBase.alphaTest=0;nullBase.blendEnable=0;nullBase.depthWrite=0;
    b.clearTarget(color,{1,1,1,1});const auto nullDepth=b.readbackDepthTarget(depth);
    b.drawImmediate(color,depth,nullBase);const auto nullOutput=b.readbackTarget(color);
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){
        const bool inside=x>=4&&x<12&&y>=4&&y<12;
        need(word(nullOutput,16*y+x)==(inside?0u:UINT32_MAX),"Projected null base sample retained color or alpha");
    }
    need(b.readbackDepthTarget(depth)==nullDepth,"Projected null base sample changed disabled depth writes");
    nullBase.alphaTest=1;nullBase.alphaReference=1;b.clearTarget(color,{1,1,1,1});
    const auto rejectedNull=b.readbackTarget(color);b.drawImmediate(color,depth,nullBase);
    need(b.readbackTarget(color)==rejectedNull,"Projected null base sample bypassed original alpha rejection");
    // Every invalid profile must fail before submission or attachment changes.
    auto rejects=[&](const ImmediateDraw& bad){const auto count=b.immediateDrawCount();
        const auto before=b.readbackTarget(color),beforeDepth=b.readbackDepthTarget(depth);bool caught=false;
        try{b.drawImmediate(color,depth,bad);}catch(const Error&){caught=true;}
        need(caught&&b.immediateDrawCount()==count&&b.readbackTarget(color)==before&&b.readbackDepthTarget(depth)==beforeDepth,
             "Invalid projected immediate mutated native output");};
    auto bad=d;bad.projectionDepth.reset();rejects(bad);bad=d;bad.projectionDepth=depth;rejects(bad);
    bad=d;bad.projectionDepth=b.createDepthTarget(64,64);rejects(bad);bad=dual;bad.vertices[0].uv[2]=std::numeric_limits<float>::quiet_NaN();rejects(bad);
    bad=d;bad.radial=true;rejects(bad);bad=d;bad.nullTexture=true;rejects(bad);
    bad=d;bad.projected=false;rejects(bad);bad=d;bad.projectionConstants[4][0]=std::numeric_limits<float>::infinity();rejects(bad);
    bad=d;bad.projectionConstants[3][3]=0;rejects(bad);bad=d;bad.projectionConstants[1][2]=std::numeric_limits<float>::quiet_NaN();rejects(bad);
    bad=d;bad.projectionSampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;rejects(bad);
    // A caller's b3 buffer is restored even though projected VS consumes it.
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> retained;need(SUCCEEDED(NativeIm2DProbe::device(b)->CreateBuffer(&bd,nullptr,&retained)),"Projection restore buffer creation failed");
    auto* cb=retained.Get();context->VSSetConstantBuffers(3,1,&cb);b.drawImmediate(color,depth,d);
    ComPtr<ID3D11Buffer> restored;context->VSGetConstantBuffers(3,1,&restored);need(restored.Get()==retained.Get(),"Projected immediate leaked b3 constants");
    auto retainedDesc=d.sampler;retainedDesc.AddressU=D3D11_TEXTURE_ADDRESS_CLAMP;ComPtr<ID3D11SamplerState> retainedSampler;
    need(SUCCEEDED(NativeIm2DProbe::device(b)->CreateSamplerState(&retainedDesc,&retainedSampler)),"Projected retained sampler creation failed");
    auto* sm=retainedSampler.Get();context->PSSetSamplers(2,1,&sm);b.drawImmediate(color,depth,d);
    ComPtr<ID3D11SamplerState> restoredSampler;context->PSGetSamplers(2,1,&restoredSampler);
    need(restoredSampler.Get()==retainedSampler.Get(),"Projected immediate leaked its shadow sampler");
    b.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);
    const auto finalDepth=b.readbackDepthTarget(depth);for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){const auto at=y*16+x;
        need(word(finalDepth,at*2)==((x>=4&&x<12&&y>=4&&y<12)?0x3F000000u:0u)&&finalDepth[at*8+4]==0x5A,"Projected immediate depth or stencil differs");}
    const auto shadowAfter=b.readbackDepthTarget(shadow);
    for(size_t at=0;at<shadowBefore.size();at+=8)
        need(!std::memcmp(shadowBefore.data()+at,shadowAfter.data()+at,5),"Projected shadow sampling changed original depth or stencil");
    d.vertices=originalVertices;
    ComPtr<ID3D11InfoQueue> queue;NativeIm2DProbe::device(b)->QueryInterface(IID_PPV_ARGS(&queue));
    if(queue)for(UINT64 i=0;i<queue->GetNumStoredMessages();++i){SIZE_T size=0;queue->GetMessage(i,nullptr,&size);std::vector<uint8_t> bytes(size);auto* message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());queue->GetMessage(i,message,&size);
        if(message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"D3D11: %s\n",message->pDescription);throw Error("Projected immediate D3D11 validation error");}}
    std::printf("PASS projected immediate %s: %zu cases, %zu checks\n",hardware?"hardware":"WARP",cases,checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"Projected immediate failure: %s\n",error.what());return 1;}}
