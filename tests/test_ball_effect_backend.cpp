#include "renderer/ball_effect.h"
#include "renderer/device_availability.h"
#include <d3d11sdklayers.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
namespace Simpsons::Graphics {
struct NativeIm2DProbe {
    static ID3D11DeviceContext* context(NativeBackend& b){return b.context.Get();}
    static ID3D11Device* device(NativeBackend& b){return b.device.Get();}
    static void debug(NativeBackend& b,bool hardware){
        ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL level;
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        if(SUCCEEDED(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_DEBUG,levels,2,D3D11_SDK_VERSION,&d,&level,&c))){
            b.device=d;b.context=c;b.featureLevel=level;b.availability=std::make_shared<DeviceAvailability>(d.Get());
        }
    }
};
}
using namespace Simpsons::Graphics;
void need(bool condition,const char* message){if(!condition)throw Error(message);}
uint32_t word(const std::vector<uint8_t>& bytes,unsigned pixel){uint32_t v;std::memcpy(&v,bytes.data()+pixel*4,4);return v;}
uint32_t gray(uint32_t value,uint32_t alpha){return value|(value<<10)|(value<<20)|(alpha<<30);}
int main(int argc,char** argv){try{
    const bool hardware=argc==2&&std::strcmp(argv[1],"--hardware")==0;
    NativeBackend b(!hardware);NativeIm2DProbe::debug(b,hardware);
    auto color=b.createTarget(16,16,TargetFormat::RGB10A2);auto depth=b.createDepthTarget(16,16);
    b.bindTargets({color,nullptr,nullptr,nullptr},depth);b.setViewport({0,0,16,16,0,1});
    b.clearTarget(color,{0,0,0,0});b.clearDepthTarget(depth,0,0x5A);
    BallEffectDraw d{};d.viewport={0,0,16,16,0,0x3F800000};d.depthEnable=d.depthWrite=1;d.depthCompare=6;
    d.viewportEnable=d.halfPixelOffset=1;d.colorMask=15;d.multisampleMask=0xFFFFFFFF;d.blendWord=0x10106;
    const uint8_t texel[]={0,255,0,255};d.texture=b.createTexture(1,1,TextureFormat::RGBA8,texel);
    d.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;d.sampler.AddressU=d.sampler.AddressV=d.sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    d.sampler.MaxLOD=13;d.sampler.MaxAnisotropy=1;d.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
    for(unsigned row=0;row<4;++row){d.constants[row][row]=1;d.constants[4+row][row]=1;}
    d.constants[8]={0,0,.5f,1.0f/3.0f};
    d.vertices={{{-.5f,-.5f,0,0},{.5f,-.5f,1,0},{.5f,.5f,1,1},{-.5f,.5f,0,1}}};
    auto* context=NativeIm2DProbe::context(b);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    const D3D11_VIEWPORT retainedViewport{1,2,12,10,.2f,.8f};context->RSSetViewports(1,&retainedViewport);
    const D3D11_RECT retainedScissor{2,3,13,14};context->RSSetScissorRects(1,&retainedScissor);
    b.bindEngineTexture(7,d.texture);
    b.drawBallEffect(color,depth,d);auto pixels=b.readbackTarget(color);auto depths=b.readbackDepthTarget(depth);
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){
        const unsigned i=y*16+x;const bool inside=x>=4&&x<12&&y>=4&&y<12;
        need(word(pixels,i)==(inside?gray(256,2):0),"Ball quad coverage, G channel, scalar alpha or shared edge differs");
        need(word(depths,2*i)==(inside?0x3F000000u:0u)&&depths[i*8+4]==0x5A,"Ball depth/stencil differs");
    }
    D3D11_PRIMITIVE_TOPOLOGY topology;context->IAGetPrimitiveTopology(&topology);need(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"Ball topology leaked");
    D3D11_VIEWPORT actualViewport{};UINT count=1;context->RSGetViewports(&count,&actualViewport);
    need(count==1&&!std::memcmp(&actualViewport,&retainedViewport,sizeof(actualViewport)),"Ball viewport leaked");
    D3D11_RECT actualScissor{};count=1;context->RSGetScissorRects(&count,&actualScissor);
    need(count==1&&!std::memcmp(&actualScissor,&retainedScissor,sizeof(actualScissor)),"Ball scissor leaked");
    b.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);b.requireEngineTexture(7,d.texture);
    b.drawBallEffect(color,depth,d);need(word(b.readbackTarget(color),136)==gray(512,2),"Ball additive blend lost preceding destination");
    d.blendWord=0x10186;b.drawBallEffect(color,depth,d);need(word(b.readbackTarget(color),136)==gray(256,2),"Ball subtractive blend differs");
    d.blendWord=0x10706;b.drawBallEffect(color,depth,d);need(word(b.readbackTarget(color),136)==gray(384,2),"Ball alpha blend differs");
    d.blendWord=0x10001;b.drawBallEffect(color,depth,d);need(word(b.readbackTarget(color),136)==gray(512,2),"Ball replace blend differs");
    d.blendWord=0x10106;d.blendEnable=0;b.drawBallEffect(color,depth,d);need(word(b.readbackTarget(color),136)==gray(512,2),"Ball disabled blend differs");
    d.alphaTest=1;d.alphaReference=255;auto before=b.readbackTarget(color);auto depthBefore=b.readbackDepthTarget(depth);
    b.drawBallEffect(color,depth,d);need(b.readbackTarget(color)==before&&b.readbackDepthTarget(depth)==depthBefore,"Ball alpha rejection wrote attachments");
    d.alphaTest=0;d.alphaReference=0;d.depthWrite=0;d.constants[8][3]=1;
    const uint8_t pattern[]={0,0,255,255,0,0,255,255,255,85,255,170,0,0,255,255};
    d.texture=b.createTexture(2,2,TextureFormat::RGBA8,pattern);
    for(auto& vertex:d.vertices){vertex[2]=.25f;vertex[3]=.75f;}
    b.clearTarget(color,{0,0,0,0});b.drawBallEffect(color,depth,d);
    need(word(b.readbackTarget(color),136)==gray(341,1),"Ball UV, sampled alpha or 1.5 scale differs");
    need(b.readbackDepthTarget(depth)==depthBefore,"Ball disabled depth write changed depth/stencil");
    // Nonidentity world-to-view, then camera-plane offset and projection.
    d.texture=b.createTexture(1,1,TextureFormat::RGBA8,texel);d.constants[8]={2,3,4,1};
    d.constants[0][0]=.25f;d.constants[1][1]=.25f;d.constants[2][2]=.125f;d.constants[3]={-.25f,-.75f,0,1};
    for(auto& vertex:d.vertices){vertex[0]*=.5f;vertex[1]*=.5f;}
    b.clearTarget(color,{0,0,0,0});b.drawBallEffect(color,depth,d);pixels=b.readbackTarget(color);
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)
        need(word(pixels,y*16+x)==((x>=8&&x<12&&y>=6&&y<10)?0xFFFFFFFFu:0),"Ball world center, camera plane offset or projection differs");
    d.scissorEnable=1;d.scissor={9,7,11,9};d.colorMask=2;b.clearTarget(color,{0,0,0,0});b.drawBallEffect(color,depth,d);pixels=b.readbackTarget(color);
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)
        need(word(pixels,y*16+x)==((x>=9&&x<11&&y>=7&&y<9)?(1023u<<10):0),"Ball scissor or color mask differs");
    d.scissorEnable=0;d.colorMask=15;d.depthWrite=1;d.constants[8][2]=2;d.viewport[4]=0x3F800000;d.viewport[5]=0;
    b.clearDepthTarget(depth,0,0x5A);b.drawBallEffect(color,depth,d);depths=b.readbackDepthTarget(depth);
    need(word(depths,2*(8*16+10))==0x3F400000u&&depths[(8*16+10)*8+4]==0x5A,"Ball reversed viewport depth differs");
    const auto drawCount=b.ballEffectDrawCount();before=b.readbackTarget(color);auto invalid=d;invalid.constants[8][0]=std::numeric_limits<float>::infinity();bool rejected=false;
    try{b.drawBallEffect(color,depth,invalid);}catch(const Error&){rejected=true;}
    need(rejected&&b.ballEffectDrawCount()==drawCount&&b.readbackTarget(color)==before,"Ball invalid constants submitted");
    invalid=d;invalid.blendWord=0xDEAD;rejected=false;try{b.drawBallEffect(color,depth,invalid);}catch(const Error&){rejected=true;}
    need(rejected&&b.ballEffectDrawCount()==drawCount,"Ball unsupported blend submitted");
    // Fullscreen continuation uses the original explicit XYUV declaration.
    DistortionDraw post{};post.quad={{{-1,1,0,0},{1,1,1,0},{-1,-1,0,1}}};
    post.viewport={0,0,16,16,0,0x3F800000};post.pixelShader=0x82152708;
    post.pixelConstants[0]={1,1,1,1};post.samplers[0]=d.sampler;
    // Restore and encode change min/mag while retaining the original linear mip mode.
    // These render target views contain exactly one mip level.
    post.samplers[0].Filter=D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    post.samplers[0].AddressU=post.samplers[0].AddressV=post.samplers[0].AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    post.samplers[1]=post.samplers[0];
    auto scene=b.createTarget(256,1,TargetFormat::RGB10A2);b.clearTarget(scene,{1,0,0,1});
    post.inputs[0]=scene;b.clearTarget(color,{0,0,0,0});b.drawDistortion(color,post);
    need(word(b.readbackTarget(color),136)==(1023u|(3u<<30)),"Ball original scene restore differs");
    auto field=b.createTarget(16,16,TargetFormat::RGB10A2);b.clearTarget(field,{.5f,.5f,0,1});
    post.pixelShader=0x82156340;post.inputs[1]=field;post.blendWord=0x10706;post.blendEnable=1;
    b.clearTarget(color,{0,0,1,1});b.drawDistortion(color,post);
    need(word(b.readbackTarget(color),136)==(1023u<<20),"Ball zero distortion strength changed scene RGB");
    b.clearTarget(field,{.5f,.5f,1,1});b.drawDistortion(color,post);
    need(word(b.readbackTarget(color),136)==(1023u|(3u<<30)),"Ball full distortion composite source/alpha differs");
    post.pixelShader=0x82156150;post.inputs[1].reset();post.blendWord=0x10001;
    b.clearTarget(scene,{0,.25f,0,1});b.drawDistortion(color,post);
    need(word(b.readbackTarget(color),136)==(512u|(512u<<10)|(3u<<30)),"Ball flat field encoding differs");
    // Upload known packed patterns through the actual selected target resource.
    // This independent fixture exposes the emboss direction and displacement channel.
    auto fillTarget=[&](const std::shared_ptr<RenderTarget>& target,const std::vector<uint32_t>& values){
        need(values.size()==size_t(target->width)*target->height,"Distortion fixture extent differs");
        b.bindTargets({target,nullptr,nullptr,nullptr},nullptr);
        ComPtr<ID3D11RenderTargetView> view;context->OMGetRenderTargets(1,&view,nullptr);
        ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
        context->UpdateSubresource(resource.Get(),0,nullptr,values.data(),target->width*4,0);
        b.bindTargets({color,nullptr,nullptr,nullptr},depth);
    };
    auto mask=b.createTarget(64,64,TargetFormat::RGB10A2);auto encoded=b.createTarget(64,64,TargetFormat::RGB10A2);
    std::vector<uint32_t> steps(64*64);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)steps[y*64+x]=(y>=32?(1023u<<10):0)|(3u<<30);
    fillTarget(mask,steps);post.inputs[0]=mask;post.viewport[2]=post.viewport[3]=64;b.drawDistortion(encoded,post);
    auto encodedPixels=b.readbackTarget(encoded);
    need(word(encodedPixels,30*64+32)==(512u|(512u<<10)|(3u<<30))&&
         word(encodedPixels,31*64+32)==0xFFFFFFFFu&&word(encodedPixels,32*64+32)==0xFFFFFFFFu&&
         word(encodedPixels,33*64+32)==(512u|(512u<<10)|(3u<<30)),"Ball nonzero emboss offset, difference direction or magnitude differs");
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)steps[y*64+x]=(y<32?(1023u<<10):0)|(3u<<30);
    fillTarget(mask,steps);b.drawDistortion(encoded,post);encodedPixels=b.readbackTarget(encoded);
    need(word(encodedPixels,31*64+32)==((1023u<<20)|(3u<<30)),"Ball negative emboss direction differs");
    std::vector<uint32_t> stripe(256);
    for(unsigned x=0;x<256;++x)stripe[x]=(x<134?1023u:(1023u<<20))|(3u<<30);
    fillTarget(scene,stripe);post.inputs[0]=scene;post.inputs[1]=field;post.viewport[2]=post.viewport[3]=16;
    post.pixelShader=0x82156340;post.blendWord=0x10706;
    b.clearTarget(field,{1,.5f,1,1});b.drawDistortion(color,post);
    need(word(b.readbackTarget(color),136)==(1023u|(3u<<30)),"Ball displacement sign, scale or red-coordinate channel differs");
    b.clearTarget(field,{.5f,1,1,1});b.drawDistortion(color,post);
    need(word(b.readbackTarget(color),136)==((1023u<<20)|(3u<<30)),"Ball green displacement incorrectly changed horizontal coordinate");
    b.clearTarget(field,{1,.5f,102.0f/1023.0f,1});b.clearTarget(color,{0,0,1,1});b.drawDistortion(color,post);
    need(word(b.readbackTarget(color),136)==(510u|(513u<<20)|(1u<<30)),"Ball partial strength alpha blend differs");
    post.inputs[1].reset();post.blendWord=0x10001;b.clearTarget(scene,{0,.25f,0,1});
    post.pixelShader=0x82155F28;post.pixelConstants[9]={1.0f/256.0f,1,0,0};b.drawDistortion(color,post);
    need(word(b.readbackTarget(color),136)==((256u<<10)|(3u<<30)),"Ball explicit UV filtered continuation differs");
    b.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);
    if(NativeIm2DProbe::device(b)->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1){
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=16;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=4;
        ComPtr<ID3D11Buffer> buffer;need(SUCCEEDED(NativeIm2DProbe::device(b)->CreateBuffer(&desc,nullptr,&buffer)),"Ball high UAV storage creation failed");
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{};view.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;view.Buffer.NumElements=4;
        ComPtr<ID3D11UnorderedAccessView> uav;need(SUCCEEDED(NativeIm2DProbe::device(b)->CreateUnorderedAccessView(buffer.Get(),&view,&uav)),"Ball high UAV view creation failed");
        for(UINT slot:{8u,63u}){
            auto* raw=uav.Get();context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,slot,1,&raw,nullptr);
            const auto pixelsBefore=b.readbackTarget(color);const auto depthBytesBefore=b.readbackDepthTarget(depth);const auto countBefore=b.ballEffectDrawCount();
            bool rejectedUav=false;try{b.drawBallEffect(color,depth,d);}catch(const Error&){rejectedUav=true;}
            need(rejectedUav&&b.ballEffectDrawCount()==countBefore,"Ball high UAV draw was not rejected before submission");
            ComPtr<ID3D11UnorderedAccessView> retained;context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,slot,1,&retained);
            need(retained==uav&&b.readbackTarget(color)==pixelsBefore&&b.readbackDepthTarget(depth)==depthBytesBefore,"Ball rejected high UAV draw changed state or attachments");
            ID3D11UnorderedAccessView* empty=nullptr;context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,slot,1,&empty,nullptr);
        }
    }
    ComPtr<ID3D11InfoQueue> queue;NativeIm2DProbe::device(b)->QueryInterface(IID_PPV_ARGS(&queue));
    if(queue)for(UINT64 i=0;i<queue->GetNumStoredMessages();++i){SIZE_T n=0;queue->GetMessage(i,nullptr,&n);std::vector<uint8_t> bytes(n);auto* message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());queue->GetMessage(i,message,&n);
        if(message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"D3D11: %s\n",message->pDescription);throw Error("Ball D3D11 validation error");}}
    std::puts("PASS ball effect quad, matrices, G/alpha sample, blends, depth/stencil, scissor, state restoration and rejection");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL ball effect backend: %s\n",e.what());return 1;}}
