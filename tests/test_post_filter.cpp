#include "renderer/post_filter.h"
#include "renderer/device_availability.h"
#include "VSPostAutoTextured.h"
#include "PSPostTexturedDraw.h"
#include <d3d11sdklayers.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace Simpsons::Graphics {
struct NativeIm2DProbe {
    static ID3D11Device* device(NativeBackend& b){return b.device.Get();}
    static ID3D11DeviceContext* context(NativeBackend& b){return b.context.Get();}
    static void debug(NativeBackend& b,bool hardware){
        ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        if(SUCCEEDED(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_DEBUG,levels,2,D3D11_SDK_VERSION,&d,&level,&c))){
            b.availability=std::make_shared<DeviceAvailability>(d.Get());b.device=d;b.context=c;b.featureLevel=level;
        }
    }
};
}
using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void need(bool ok,const char* why){++checks;if(!ok)throw Error(why);}
void hr(HRESULT result,const char* why){need(SUCCEEDED(result),why);}
using Pixel=std::array<float,4>;
uint32_t pack(Pixel p){uint32_t result=0;const uint32_t shifts[]{0,10,20,30};
    for(unsigned i=0;i<4;++i){const float scale=i==3?3.0f:1023.0f;
        result|=uint32_t(std::nearbyint(std::clamp(p[i],0.0f,1.0f)*scale))<<shifts[i];}return result;}
Pixel unpack(uint32_t p){return {float(p&1023)*(1.0f/1023),float((p>>10)&1023)*(1.0f/1023),float((p>>20)&1023)*(1.0f/1023),float(p>>30)*(1.0f/3)};}
float add(float a,float b){volatile float result=a+b;return result;}
float mul(float a,float b){volatile float result=a*b;return result;}
int address(int index,int size,bool mirror){
    if(!mirror)return std::clamp(index,0,size-1);
    index%=2*size;if(index<0)index+=2*size;return index<size?index:2*size-1-index;
}
Pixel sample(const std::vector<Pixel>& data,int size,float u,float v,bool linear,bool mirror){
    const float x=mul(u,float(size)),y=mul(v,float(size));
    auto at=[&](int xx,int yy){return data[size_t(address(yy,size,mirror)*size+address(xx,size,mirror))];};
    if(!linear)return at(int(std::floor(x)),int(std::floor(y)));
    const float sx=add(x,-.5f),sy=add(y,-.5f);const int ix=int(std::floor(sx)),iy=int(std::floor(sy));
    const float fx=sx-float(ix),fy=sy-float(iy);const auto a=at(ix,iy),b=at(ix+1,iy),c=at(ix,iy+1),d=at(ix+1,iy+1);
    Pixel result{};for(unsigned i=0;i<4;++i){
        const float top=add(mul(a[i],1-fx),mul(b[i],fx)),bottom=add(mul(c[i],1-fx),mul(d[i],fx));
        result[i]=add(mul(top,1-fy),mul(bottom,fy));}return result;
}
// Luma_Xenon_PS layer weight from the layer's gb.xy; mad is unfused here.
float lumaWeight(float luma,float spread,float doubled,float centered,float x,float y){
    const float x2=mul(x,x),x3=mul(x2,x),x4=mul(x3,x),x5=mul(x4,x),x10=mul(x5,x5);
    const float y3=mul(mul(mul(y,-.5f),y),y),keep=add(1,-x10),bend=mul(y3,keep);
    const float curve=mul(mul(add(-mul(bend,bend),.25f),-y),keep);
    const float slope=add(mul(curve,doubled),bend),level=add(luma,add(mul(spread,x),x5));
    return std::clamp(add(mul(centered,slope),level),0.0f,1.0f);
}
Pixel luma(const std::array<std::array<float,4>,10>& c,Pixel source){
    const float l=std::clamp(add(add(mul(source[2],.11f),mul(source[0],.3f)),mul(source[1],.59f)),0.0f,1.0f);
    const float spread=mul(mul(l,2),add(1,-l)),doubled=add(spread,spread),centered=add(mul(l,2),-1);
    const float addWeight=lumaWeight(l,spread,doubled,centered,c[1][0],c[1][1]);
    const float subWeight=lumaWeight(l,spread,doubled,centered,c[3][0],c[3][1]);
    const float lerpWeight=lumaWeight(l,spread,doubled,centered,c[5][0],c[5][1]);
    Pixel result{};result[3]=1;
    for(unsigned i=0;i<3;++i){
        const float added=add(mul(mul(addWeight,c[0][3]),c[0][i]),source[i]);
        const float subtracted=add(-mul(mul(c[2][3],subWeight),c[2][i]),added);
        result[i]=add(mul(add(mul(lerpWeight,c[4][i]),-subtracted),c[4][3]),subtracted);
    }
    return result;
}
Pixel shader(const PostFilterDraw& draw,const std::vector<Pixel>& data,int size,float u,float v){
    const bool linear=draw.sampler.Filter==D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT||draw.sampler.Filter==D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    const bool mirror=draw.sampler.AddressU==D3D11_TEXTURE_ADDRESS_MIRROR;
    auto tap=[&](float x,float y){return sample(data,size,x,y,linear,mirror);};
    const auto& color=draw.pixelConstants[0];Pixel result{};
    if(draw.pixelShader==0x821559D8)result=luma(draw.pixelConstants,tap(u,v));
    else if(draw.pixelShader==0x821583B8){const float alpha=tap(u,v)[3];result.fill(alpha);}
    else if(draw.pixelShader==0x82152708){result=tap(u,v);for(unsigned i=0;i<4;++i)result[i]=mul(result[i],color[i]);}
    else if(draw.pixelShader==0x82155F28){
        const float sx=mul(draw.pixelConstants[9][0],.5f),sy=mul(draw.pixelConstants[9][1],.5f);
        const auto first=tap(u+sx,v-sy),second=tap(u-sx,v-sy),third=tap(u+sx,v+sy),fourth=tap(u-sx,v+sy);
        for(unsigned i=0;i<4;++i)result[i]=mul(add(add(add(third[i],fourth[i]),second[i]),first[i]),.25f);
    }else{
        const float sx=draw.pixelConstants[1][0],sy=draw.pixelConstants[1][1];
        const float sum=add(add(add(add(tap(u-sx,v)[0],tap(u+sx,v)[0]),tap(u,v+sy)[0]),tap(u,v-sy)[0]),tap(u,v)[0]);
        const float intensity=std::min(mul(sum,.4f),1.0f);
        for(unsigned i=0;i<4;++i)result[i]=mul(intensity,color[i]);
    }return result;
}
uint32_t expected(const PostFilterDraw& d,Pixel source,uint32_t old){
    const Pixel destination=unpack(old);
    if(d.blendEnable){
        const float sf=(d.blendWord&31)==11?1-destination[3]:(d.blendWord&31)==6?source[3]:1;
        const uint32_t code=(d.blendWord>>8)&31;const float df=code==7?1-source[3]:(code==1?1.0f:0.0f);
        for(unsigned i=0;i<3;++i){const float a=mul(source[i],sf),b=mul(destination[i],df);
            source[i]=((d.blendWord>>5)&7)==4?add(b,-a):add(a,b);}
    }
    const uint32_t packed=pack(source);uint32_t result=old;
    for(unsigned i=0;i<4;++i)if(d.colorMask&(1u<<i)){
        const uint32_t mask=(i==3?3u:1023u)<<(10*i);result=(result&~mask)|(packed&mask);}
    return result;
}
ComPtr<ID3D11ShaderResourceView> view(NativeBackend& backend,const std::shared_ptr<RenderTarget>& target,
                                   const void* bytes=nullptr,UINT stride=0){
    backend.bindTargets({target,nullptr,nullptr,nullptr},nullptr);
    auto* context=NativeIm2DProbe::context(backend);ComPtr<ID3D11RenderTargetView> rtv;context->OMGetRenderTargets(1,&rtv,nullptr);
    ComPtr<ID3D11Resource> resource;rtv->GetResource(&resource);context->OMSetRenderTargets(0,nullptr,nullptr);
    if(bytes)context->UpdateSubresource(resource.Get(),0,nullptr,bytes,stride,0);
    ComPtr<ID3D11ShaderResourceView> result;hr(NativeIm2DProbe::device(backend)->CreateShaderResourceView(resource.Get(),nullptr,&result),"test source view");
    return result;
}
void checkPixels(NativeBackend& backend,const std::shared_ptr<RenderTarget>& target,const PostFilterDraw& draw,
                 const std::vector<Pixel>& input,int size,const std::vector<uint32_t>& before){
    const auto bytes=backend.readbackTarget(target);need(bytes.size()==before.size()*4,"packed output extent");
    for(UINT y=0;y<target->height;++y)for(UINT x=0;x<target->width;++x){const size_t index=size_t(y)*target->width+x;
        uint32_t actual{};std::memcpy(&actual,bytes.data()+index*4,4);uint32_t wanted=before[index];
        if(!draw.scissorEnable||(x>=draw.scissor[0]&&x<draw.scissor[2]&&y>=draw.scissor[1]&&y<draw.scissor[3]))
            wanted=expected(draw,shader(draw,input,size,(float(x)+.5f)/float(target->width),(float(y)+.5f)/float(target->height)),before[index]);
        for(unsigned channel=0;channel<4;++channel){const uint32_t mask=channel==3?3:1023;
            const int a=int((actual>>(10*channel))&mask),b=int((wanted>>(10*channel))&mask);
            if(std::abs(a-b)>(channel==3?0:1)){
                std::fprintf(stderr,"pixel mismatch ps=%08X blend=%08X (%u,%u) channel%u got%d expected%d\n",draw.pixelShader,draw.blendWord,x,y,channel,a,b);
                need(false,"post filter packed pixel differs from independent oracle");}
            ++checks;
        }
    }
}
void run(bool hardware){
    NativeBackend backend(!hardware);NativeIm2DProbe::debug(backend,hardware);
    auto* device=NativeIm2DProbe::device(backend);auto* context=NativeIm2DProbe::context(backend);
    ComPtr<ID3D11InfoQueue> debug;device->QueryInterface(IID_PPV_ARGS(&debug));if(debug)debug->ClearStoredMessages();
    constexpr int size=8;std::vector<Pixel> data(size*size);
    for(int y=0;y<size;++y)for(int x=0;x<size;++x)data[size_t(y*size+x)]={float(x-2)*.1875f,float(y)*.125f,float((x+3*y)%9)*.125f,float((x+2*y)%5)*.25f};
    auto input=backend.createTarget(size,size,TargetFormat::RGBA32Float);auto sourceView=view(backend,input,data.data(),size*sizeof(Pixel));
    PostFilterDraw d{};d.input=input;d.vertices={-1,1,1,1,-1,-1};d.pixelConstants[0]={.7f,.35f,1.3f,.61f};
    d.pixelConstants[1]={.25f,.375f,0,0};d.pixelConstants[9]={.5f,.75f,0,0};
    d.sampler.AddressU=d.sampler.AddressV=d.sampler.AddressW=D3D11_TEXTURE_ADDRESS_MIRROR;
    d.sampler.MaxAnisotropy=1;d.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;d.sampler.MaxLOD=13;
    // Values in c2..8 are dead; the CPU owner may legitimately retain NaNs.
    d.pixelConstants[5].fill(std::numeric_limits<float>::quiet_NaN());
    const uint32_t shaders[]{0x821583B8,0x82152708,0x82155F28,0x82158118};
    const uint32_t blends[]{0x10001,0x1000B,0x10101,0x1010B,0x10181,0x1018B,0x10701,0x1070B};
    for(UINT extent:{4u,8u}){
        auto target=backend.createTarget(extent,extent,TargetFormat::RGB10A2);std::vector<uint32_t> before(size_t(extent)*extent);
        for(size_t i=0;i<before.size();++i)before[i]=pack({.2f,.4f,.65f,float(i%4)/3});
        d.viewport={0,0,extent,extent,0x3F800000,0};
        for(bool mirror:{false,true})for(bool linear:{false,true})for(bool mipLinear:{false,true})for(uint32_t pixel:shaders)for(uint32_t blend:blends){
            d.pixelShader=pixel;d.blendWord=blend;d.blendEnable=1;
            d.sampler.AddressU=d.sampler.AddressV=mirror?D3D11_TEXTURE_ADDRESS_MIRROR:D3D11_TEXTURE_ADDRESS_CLAMP;
            d.sampler.Filter=linear?(mipLinear?D3D11_FILTER_MIN_MAG_MIP_LINEAR:D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT):(mipLinear?D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR:D3D11_FILTER_MIN_MAG_MIP_POINT);
            (void)view(backend,target,before.data(),extent*4);backend.drawPostFilter(target,d);checkPixels(backend,target,d,data,size,before);
        }
        // Original 82770AF0 strip over three layer banks: ordinary curves,
        // saturating curves, and zero colors cancelling retained curves.
        constexpr std::array<std::array<std::array<float,4>,6>,3> banks{{
            {{{.2f,.1f,.05f,.5f},{.3f,.2f,0,0},{.1f,.05f,.2f,.25f},{.5f,-.25f,0,0},{.9f,.3f,.1f,.4f},{.7f,.6f,0,0}}},
            {{{0,0,0,0},{2.5f,-3,0,0},{.4f,.8f,.2f,1},{-.75f,1.5f,0,0},{0,0,0,0},{-1.25f,.5f,0,0}}},
            {{{1.5f,-.5f,.25f,2},{0,0,0,0},{0,0,0,0},{1,1,0,0},{.25f,.5f,.75f,1},{.125f,-2,0,0}}}}};
        for(const auto& bank:banks)for(bool linear:{false,true})for(uint32_t blend:{0x10001u,0x10101u}){
            auto l=d;l.pixelShader=0x821559D8;l.strip=std::array<float,8>{-1,-1,1,-1,-1,1,1,1};l.blendWord=blend;l.blendEnable=1;
            for(unsigned i=0;i<6;++i)l.pixelConstants[i]=bank[i];
            // Uploaded gb.zw lanes are dead (type word and padding).
            l.pixelConstants[1][2]=l.pixelConstants[3][3]=std::numeric_limits<float>::quiet_NaN();
            l.sampler.AddressU=l.sampler.AddressV=D3D11_TEXTURE_ADDRESS_CLAMP;
            l.sampler.Filter=linear?D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT:D3D11_FILTER_MIN_MAG_MIP_POINT;
            (void)view(backend,target,before.data(),extent*4);backend.drawPostFilter(target,l);checkPixels(backend,target,l,data,size,before);
        }
        d.blendEnable=0;d.colorMask=5;d.scissorEnable=1;d.scissor={1,1,extent-1,extent-1};
        (void)view(backend,target,before.data(),extent*4);backend.drawPostFilter(target,d);checkPixels(backend,target,d,data,size,before);
        d.colorMask=15;d.scissorEnable=0;
    }
    // Screen effects through the shared adapter. Uniform scene/depth/query
    // inputs make every tap equal, so short oracles isolate stage binding,
    // corner expansion, blend words and packing; tap geometry is qualified
    // against the original microcode by NativeScreenEffectShaders*.
    {
        constexpr UINT extent=8;const Pixel color{.6f,.35f,.8f,.5f};
        std::vector<Pixel> uniform(size_t(extent)*extent,color);
        auto scene=backend.createTarget(extent,extent,TargetFormat::RGBA32Float);(void)view(backend,scene,uniform.data(),extent*sizeof(Pixel));
        auto modulate=backend.createTarget(64,8,TargetFormat::RGB10A2);std::vector<uint32_t> query(64*8,pack({.4f,.2f,.9f,1}));
        (void)view(backend,modulate,query.data(),64*4);const float queryValue=unpack(query[0])[0];
        auto target=backend.createTarget(extent,extent,TargetFormat::RGB10A2);std::vector<uint32_t> before(size_t(extent)*extent);
        for(size_t i=0;i<before.size();++i)before[i]=pack({.2f+.05f*float(i%5),.4f,.65f,float(i%4)/3});
        auto depth=backend.createDepthTarget(extent,extent);
        D3D11_SAMPLER_DESC linear=d.sampler;linear.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;linear.AddressU=linear.AddressV=D3D11_TEXTURE_ADDRESS_CLAMP;
        D3D11_SAMPLER_DESC point=linear;point.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        const std::array<float,8> strip{-1,-1,1,-1,-1,1,1,1};const std::array<float,6> rect{-1,-1,1,-1,-1,1};
        auto sat=[](float v){return std::clamp(v,0.0f,1.0f);};
        struct Case{uint32_t ps,blend;float depth;uint32_t dofEdge{};};
        const Case cases[]{{0x821517A0,0x10001,.5f},{0x821517A0,0x10001,0},{0x82151C50,0x10706,.5f},{0x82151DB8,0x10001,.5f},
            {0x82153E40,0x10706,.75f},{0x82153E40,0x10706,0},{0x821540A0,0x10706,.25f},{0x82154330,0x10706,.5f},{0x821557C8,0x10006,.5f},
            {0x821517A0,0x10001,.5f,1},{0x821517A0,0x10001,.5f,2},{0x821517A0,0x10001,.5f,3},
            {0x821517A0,0x10001,.5f,4}};
        for(const auto& item:cases){
            PostFilterDraw e{};e.viewport={0,0,extent,extent,0x3F800000,0};e.pixelShader=item.ps;e.blendEnable=1;e.blendWord=item.blend;
            e.pixelConstants[0]={.7f,.45f,1.2f,.6f};e.pixelConstants[1]={.03f,.02f,.3f,2.5f};e.pixelConstants[2]={.05f,.04f,.5f/64,.5f/8};
            const bool fog=item.ps==0x82153E40||item.ps==0x821540A0||item.ps==0x82154330;
            if(fog){e.pixelConstants[1]={2.0f,.02f,.1f,1.5f};e.pixelConstants[2]={std::numeric_limits<float>::quiet_NaN(),200,300,198.5f};}
            if(item.ps==0x82151DB8)e.pixelConstants[1]={.2f,.1f,.5f,.3f};
            if(item.ps==0x821557C8)e.pixelConstants[1]={.5f,.25f,.75f,.125f};
            if(item.dofEdge==1)e.pixelConstants[1]={.03f,.02f,std::numeric_limits<float>::infinity(),0};
            if(item.dofEdge==2)e.pixelConstants[1]={.03f,.02f,.5f,std::numeric_limits<float>::infinity()};
            if(item.dofEdge==3)e.pixelConstants[1]={.03f,.02f,.5f,-std::numeric_limits<float>::infinity()};
            if(item.dofEdge==4)e.pixelConstants[1]={.03f,.02f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()};
            if(!fog){e.input=scene;e.sampler=linear;}
            if(fog||item.ps==0x821517A0){backend.clearDepthTarget(depth,item.depth,0);e.depthInput=depth;e.depthSampler=point;}
            if(item.ps==0x82151DB8){e.secondaryInput=modulate;e.secondarySampler=point;}
            if(item.ps==0x82151DB8||item.ps==0x821557C8)e.vertices=rect;else e.strip=strip;
            const auto& c=e.pixelConstants;Pixel source{};
            if(item.ps==0x821517A0){ // Equal taps: the depth-driven radius cancels.
                for(unsigned i=0;i<3;++i)source[i]=add(mul(add(mul(color[i],c[0][i]),-color[i]),c[0][3]),color[i]);source[3]=1;}
            else if(item.ps==0x82151C50){for(unsigned i=0;i<3;++i)source[i]=mul(color[i],c[0][i]);source[3]=c[0][3];}
            else if(item.ps==0x82151DB8){const float m=std::max(queryValue,c[1][3]);
                for(unsigned i=0;i<3;++i)source[i]=add(mul(mul(mul(sat(add(color[i],-c[1][i])),m),c[0][i]),c[0][3]),color[i]);source[3]=1;}
            else if(item.ps==0x821557C8){for(unsigned i=0;i<3;++i)source[i]=mul(mul(color[i],c[0][i]),add(mul(c[1][3],8),c[1][i]));source[3]=c[0][3];}
            else if(item.depth<=0)source={0,0,0,0};
            else {const float distance=sat(mul(c[1][1],add(mul(1.0f/add(c[2][1],-mul(add(1,-item.depth),c[2][3])),c[2][2]),-c[1][0])));
                float factor=distance;
                if(item.ps==0x821540A0)factor=1-std::exp2(-mul(c[1][3],distance)*1.4426950216293335f);
                if(item.ps==0x82154330){const float density=mul(c[1][3],distance);factor=1-std::exp2(1.4426950216293335f*-mul(density,density));}
                for(unsigned i=0;i<3;++i)source[i]=c[0][i];source[3]=add(mul(factor,c[0][3]),c[1][2]);}
            (void)view(backend,target,before.data(),extent*4);
            if(item.ps==0x821517A0||item.ps==0x82151C50||item.ps==0x82151DB8) {
                const auto pixelsBefore=backend.readbackTarget(target),depthBefore=backend.readbackDepthTarget(depth);
                const auto drawsBefore=backend.postFilterDrawCount();
                backend.drawPostFilter(target,e,false);
                need(backend.postFilterDrawCount()==drawsBefore&&backend.readbackTarget(target)==pixelsBefore&&
                     backend.readbackDepthTarget(depth)==depthBefore,"Suppressed screen effect changed attachments or issued a draw");
            }
            backend.drawPostFilter(target,e);
            const auto bytes=backend.readbackTarget(target);
            for(size_t i=0;i<before.size();++i){uint32_t got{};std::memcpy(&got,bytes.data()+4*i,4);const uint32_t want=expected(e,source,before[i]);
                for(unsigned channel=0;channel<4;++channel){const uint32_t mask=channel==3?3:1023;
                    const int a=int((got>>(10*channel))&mask),b=int((want>>(10*channel))&mask);
                    if(std::abs(a-b)>(channel==3?0:1)){std::fprintf(stderr,"effect ps=%08X depth=%g pixel%zu channel%u got%d expected%d\n",item.ps,double(item.depth),i,channel,a,b);
                        need(false,"Screen-effect packed pixel differs from its uniform-input oracle");}
                    ++checks;}}
        }
        // Stage and geometry rejections: color on fog, missing depth, wrong strip/rect, dead-lane policy.
        const auto count=backend.postFilterDrawCount();
        auto rejectsEffect=[&](PostFilterDraw bad){for(bool submit:{false,true}){
            bool rejected=false;try{backend.drawPostFilter(target,bad,submit);}catch(const Error&){rejected=true;}
            need(rejected&&backend.postFilterDrawCount()==count,"invalid screen effect was not rejected before submission or suppression");}};
        PostFilterDraw fog{};fog.viewport={0,0,extent,extent,0x3F800000,0};fog.pixelShader=0x821540A0;fog.blendEnable=1;fog.blendWord=0x10706;
        fog.pixelConstants[0]={.7f,.45f,1,.6f};fog.pixelConstants[1]={2,.02f,.1f,1.5f};fog.pixelConstants[2]={0,200,300,198.5f};
        fog.depthInput=depth;fog.depthSampler=point;fog.strip=strip;
        auto bad=fog;bad.input=scene;bad.sampler=linear;rejectsEffect(bad);bad=fog;bad.depthInput.reset();rejectsEffect(bad);
        bad=fog;bad.strip.reset();bad.vertices=rect;rejectsEffect(bad);bad=fog;bad.blendWord=0x10101;rejectsEffect(bad);
        bad=fog;bad.pixelConstants[1][3]=std::numeric_limits<float>::infinity();rejectsEffect(bad);
        auto dof=fog;dof.pixelShader=0x821517A0;dof.blendWord=0x10001;dof.input=scene;dof.sampler=linear;
        dof.pixelConstants[1]={.03f,.02f,.5f,1};
        bad=dof;bad.pixelConstants[1][2]=std::numeric_limits<float>::quiet_NaN();rejectsEffect(bad);
        bad=dof;bad.pixelConstants[1][3]=std::numeric_limits<float>::quiet_NaN();rejectsEffect(bad);
        bad=dof;bad.pixelConstants[1][0]=std::numeric_limits<float>::infinity();rejectsEffect(bad);
        bad=dof;bad.pixelConstants[1]={.03f,.02f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()};
        bad.pixelConstants[0][3]=std::numeric_limits<float>::quiet_NaN();rejectsEffect(bad);
        bad=dof;bad.pixelConstants[1]={std::numeric_limits<float>::quiet_NaN(),.02f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()};rejectsEffect(bad);
        PostFilterDraw sat2{};sat2.viewport=fog.viewport;sat2.pixelShader=0x821557C8;sat2.blendEnable=1;sat2.blendWord=0x10006;sat2.input=scene;sat2.sampler=linear;
        sat2.pixelConstants[0]={.5f,.5f,.5f,1};sat2.pixelConstants[1]={.5f,.5f,.5f,.1f};
        bad=sat2;bad.vertices={-1,1,1,1,-1,-1};rejectsEffect(bad);bad=sat2;bad.vertices=rect;bad.strip=strip;rejectsEffect(bad);
        PostFilterDraw bloom=sat2;bloom.pixelShader=0x82151DB8;bloom.blendWord=0x10001;bloom.vertices=rect;bloom.pixelConstants[2]={.05f,.04f,.5f,.5f};
        bad=bloom;rejectsEffect(bad); // Bloom requires its modulate stage.
    }
    // Full actual-state restoration, including multiple render targets/viewports,
    // empty/nonempty scissors and a saved input alias at arbitrary stage slots.
    auto target=backend.createTarget(8,8,TargetFormat::RGB10A2);
    auto targetView=view(backend,target);auto retained=backend.createTarget(8,8,TargetFormat::RGB10A2);
    auto second=backend.createTarget(8,8,TargetFormat::RGB10A2);auto depth=backend.createDepthTarget(8,8);
    backend.bindTargets({retained,second,nullptr,nullptr},depth);
    std::array<ComPtr<ID3D11RenderTargetView>,2> oldRT;ComPtr<ID3D11DepthStencilView> oldDepth;
    ID3D11RenderTargetView* rawRT[2]{};context->OMGetRenderTargets(2,rawRT,&oldDepth);for(unsigned i=0;i<2;++i)oldRT[i].Attach(rawRT[i]);
    const D3D11_VIEWPORT vp[2]{{0,0,4,4,0,1},{4,4,4,4,0,1}};const D3D11_RECT rects[2]{{0,0,4,4},{4,4,8,8}};
    context->RSSetViewports(2,vp);context->RSSetScissorRects(2,rects);
    auto* saved=targetView.Get();context->PSSetShaderResources(17,1,&saved);context->VSSetShaderResources(21,1,&saved);
    saved=sourceView.Get();context->CSSetShaderResources(7,1,&saved);
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    hr(device->CreateVertexShader(kVSPostAutoTextured,sizeof(kVSPostAutoTextured),nullptr,&vs),"retained VS");
    hr(device->CreatePixelShader(kPSPostTexturedDraw,sizeof(kPSPostTexturedDraw),nullptr,&ps),"retained PS");
    context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
    D3D11_BUFFER_DESC cbDesc{};cbDesc.ByteWidth=160;cbDesc.Usage=D3D11_USAGE_DEFAULT;cbDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> cb;hr(device->CreateBuffer(&cbDesc,nullptr,&cb),"retained CB");auto* cbRaw=cb.Get();context->PSSetConstantBuffers(0,1,&cbRaw);context->PSSetConstantBuffers(1,1,&cbRaw);
    ComPtr<ID3D11SamplerState> savedSampler;hr(device->CreateSamplerState(&d.sampler,&savedSampler),"retained sampler");
    auto* samplerRaw=savedSampler.Get();context->PSSetSamplers(0,1,&samplerRaw);
    D3D11_RASTERIZER_DESC rasterDesc{};rasterDesc.FillMode=D3D11_FILL_WIREFRAME;rasterDesc.CullMode=D3D11_CULL_FRONT;rasterDesc.DepthClipEnable=TRUE;
    ComPtr<ID3D11RasterizerState> savedRaster;hr(device->CreateRasterizerState(&rasterDesc,&savedRaster),"retained raster");context->RSSetState(savedRaster.Get());
    D3D11_DEPTH_STENCIL_DESC depthDesc{};depthDesc.DepthEnable=TRUE;depthDesc.DepthFunc=D3D11_COMPARISON_LESS;depthDesc.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
    ComPtr<ID3D11DepthStencilState> savedDepth;hr(device->CreateDepthStencilState(&depthDesc,&savedDepth),"retained depth");context->OMSetDepthStencilState(savedDepth.Get(),19);
    D3D11_BLEND_DESC blendDesc{};blendDesc.RenderTarget[0].RenderTargetWriteMask=5;
    ComPtr<ID3D11BlendState> savedBlend;hr(device->CreateBlendState(&blendDesc,&savedBlend),"retained blend");
    const FLOAT factors[4]{.1f,.2f,.3f,.4f};context->OMSetBlendState(savedBlend.Get(),factors,0x1234);
    D3D11_BUFFER_DESC vbDesc{};vbDesc.ByteWidth=64;vbDesc.Usage=D3D11_USAGE_DEFAULT;vbDesc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    ComPtr<ID3D11Buffer> savedVB;hr(device->CreateBuffer(&vbDesc,nullptr,&savedVB),"retained vertices");
    auto* vbRaw=savedVB.Get();const UINT stride=16,offset=8;context->IASetVertexBuffers(0,1,&vbRaw,&stride,&offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    const D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
    ComPtr<ID3D11InputLayout> savedLayout;hr(device->CreateInputLayout(&element,1,kVSPostAutoTextured,sizeof(kVSPostAutoTextured),&savedLayout),"retained input layout");context->IASetInputLayout(savedLayout.Get());
    backend.clearDepthTarget(depth,.25f,17);const auto depthBefore=backend.readbackDepthTarget(depth);
    d.viewport={0,0,8,8,0,0x3F800000};d.pixelShader=0x82152708;
    backend.drawPostFilter(target,d);
    ID3D11RenderTargetView* currentRT[2]{};ComPtr<ID3D11DepthStencilView> currentDepth;context->OMGetRenderTargets(2,currentRT,&currentDepth);
    for(unsigned i=0;i<2;++i){need(currentRT[i]==oldRT[i].Get(),"OM color not restored");currentRT[i]->Release();}need(currentDepth==oldDepth,"DSV not restored");
    D3D11_VIEWPORT gotVP[16]{};UINT n=16;context->RSGetViewports(&n,gotVP);need(n==2&&!std::memcmp(vp,gotVP,sizeof(vp)),"multiple viewports not restored");
    D3D11_RECT gotRects[16]{};n=16;context->RSGetScissorRects(&n,gotRects);need(n==2&&!std::memcmp(rects,gotRects,sizeof(rects)),"scissors not restored");
    ComPtr<ID3D11ShaderResourceView> restored;context->PSGetShaderResources(17,1,&restored);need(restored==targetView,"PS alias not restored");
    restored.Reset();context->VSGetShaderResources(21,1,&restored);need(restored==targetView,"VS alias not restored");
    restored.Reset();context->CSGetShaderResources(7,1,&restored);need(restored==sourceView,"CS view not restored");
    ComPtr<ID3D11VertexShader> gotVS;ComPtr<ID3D11PixelShader> gotPS;context->VSGetShader(&gotVS,nullptr,nullptr);context->PSGetShader(&gotPS,nullptr,nullptr);
    need(gotVS==vs&&gotPS==ps,"shaders not restored");
    for(UINT slot=0;slot<2;++slot){ComPtr<ID3D11Buffer> got;context->PSGetConstantBuffers(slot,1,&got);need(got==cb,"PS constants not restored");}
    ComPtr<ID3D11SamplerState> gotSampler;context->PSGetSamplers(0,1,&gotSampler);need(gotSampler==savedSampler,"sampler not restored");
    ComPtr<ID3D11RasterizerState> gotRaster;context->RSGetState(&gotRaster);need(gotRaster==savedRaster,"raster not restored");
    ComPtr<ID3D11DepthStencilState> gotDepth;UINT stencil{};context->OMGetDepthStencilState(&gotDepth,&stencil);need(gotDepth==savedDepth&&stencil==19,"depth state/ref not restored");
    ComPtr<ID3D11BlendState> gotBlend;UINT mask{};FLOAT gotFactors[4]{};context->OMGetBlendState(&gotBlend,gotFactors,&mask);
    need(gotBlend==savedBlend&&mask==0x1234&&!std::memcmp(factors,gotFactors,sizeof(factors)),"blend factors/mask not restored");
    ComPtr<ID3D11Buffer> gotVB;UINT gotStride{},gotOffset{};context->IAGetVertexBuffers(0,1,&gotVB,&gotStride,&gotOffset);
    need(gotVB==savedVB&&gotStride==stride&&gotOffset==offset,"vertex stream not restored");
    ComPtr<ID3D11InputLayout> gotLayout;context->IAGetInputLayout(&gotLayout);need(gotLayout==savedLayout,"input layout not restored");
    D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);need(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"topology not restored");
    need(backend.readbackDepthTarget(depth)==depthBefore,"disabled post depth modified attachment");
    const auto count=backend.postFilterDrawCount();
    auto rejects=[&](PostFilterDraw bad){bool rejected=false;try{backend.drawPostFilter(target,bad);}catch(const Error&){rejected=true;}
        need(rejected&&backend.postFilterDrawCount()==count,"invalid draw was not rejected before submission");};
    auto bad=d;bad.pixelShader=0;rejects(bad);bad=d;bad.input=target;rejects(bad);bad=d;bad.depthEnable=1;rejects(bad);
    bad=d;bad.vertices[4]=0;rejects(bad);bad=d;bad.blendWord=0x10106;rejects(bad);bad=d;bad.alphaTest=1;rejects(bad);
    bad=d;bad.pixelConstants[0][0]=std::numeric_limits<float>::infinity();rejects(bad);
    bad=d;bad.sampler.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;rejects(bad);
    auto lumaDraw=d;lumaDraw.pixelShader=0x821559D8;lumaDraw.strip=std::array<float,8>{-1,-1,1,-1,-1,1,1,1};
    for(unsigned i=0;i<6;++i)lumaDraw.pixelConstants[i]={.25f,.5f,.75f,1};
    bad=lumaDraw;bad.strip.reset();rejects(bad);bad=d;bad.strip=lumaDraw.strip;rejects(bad);
    bad=lumaDraw;(*bad.strip)[0]=1;rejects(bad);bad=lumaDraw;bad.texturedVertices.emplace();rejects(bad);
    bad=lumaDraw;bad.pixelConstants[4][3]=std::numeric_limits<float>::quiet_NaN();rejects(bad);
    bad=lumaDraw;bad.pixelConstants[3][1]=std::numeric_limits<float>::infinity();rejects(bad);
    // FL11_1 exposes OM UAV slots8..63. Reject them before any render-target
    // change, and retain the actual resource bindings and attachment contents.
    if(device->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1){
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd.StructureByteStride=4;
        ComPtr<ID3D11Buffer> buffer;hr(device->CreateBuffer(&bd,nullptr,&buffer),"high UAV test storage");
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;ud.Buffer.NumElements=4;
        ComPtr<ID3D11UnorderedAccessView> uav;hr(device->CreateUnorderedAccessView(buffer.Get(),&ud,&uav),"high UAV test view");
        for(UINT slot:{8u,63u}){
            auto* raw=uav.Get();context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,slot,1,&raw,nullptr);
            const auto pixelsBefore=backend.readbackTarget(target);const auto depthBytesBefore=backend.readbackDepthTarget(depth);
            rejects(d);
            ComPtr<ID3D11UnorderedAccessView> retainedUav;context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,slot,1,&retainedUav);
            need(retainedUav==uav,"rejected post draw lost high UAV binding");
            need(backend.readbackTarget(target)==pixelsBefore&&backend.readbackDepthTarget(depth)==depthBytesBefore,"rejected post draw changed attachments");
            ID3D11UnorderedAccessView* empty=nullptr;context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,slot,1,&empty,nullptr);
        }
    }
    NativeBackend other(!hardware);bad=d;bad.input=other.createTarget(8,8,TargetFormat::RGB10A2);rejects(bad);
    if(debug)for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T bytes{};hr(debug->GetMessage(i,nullptr,&bytes),"debug message extent");
        std::vector<uint8_t> storage(bytes);auto* message=reinterpret_cast<D3D11_MESSAGE*>(storage.data());hr(debug->GetMessage(i,message,&bytes),"debug message");
        if(message->Severity<=D3D11_MESSAGE_SEVERITY_WARNING){std::fprintf(stderr,"D3D11: %s\n",message->pDescription);need(false,"D3D11 debug warning/error");}}
    std::printf("PASS post filter %s: %zu checks, %llu draws; four original shaders plus luma strip, eight packed blend words, rectangle coverage and binding restoration\n",
        hardware?"hardware":"WARP",checks,static_cast<unsigned long long>(backend.postFilterDrawCount()));
}
}
int main(int argc,char** argv)try{
    need(argc==1||(argc==2&&std::string(argv[1])=="--hardware"),"Optional --hardware only");run(argc==2);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL post filter: %s\n",error.what());return 1;}
