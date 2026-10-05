#include "renderer/particle_draw.h"
#include "renderer/device_availability.h"
#include <d3d11sdklayers.h>
#include <cstdio>
#include <cstring>
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
int main(int argc,char** argv){try{
    const bool hardware=argc==2&&std::strcmp(argv[1],"--hardware")==0;
    NativeBackend b(!hardware);NativeIm2DProbe::debug(b,hardware);
    auto color=b.createTarget(16,16,TargetFormat::RGB10A2);auto depth=b.createDepthTarget(16,16);
    b.bindTargets({color,nullptr,nullptr,nullptr},depth);b.setViewport({0,0,16,16,0,1});
    b.clearTarget(color,{0,0,0,0});b.clearDepthTarget(depth,0,0);
    ParticleDraw d{};d.viewport={0,0,16,16,0,0x3F800000};d.depthEnable=d.depthWrite=1;d.depthCompare=6;
    d.viewportEnable=d.halfPixelOffset=1;d.colorMask=15;d.multisampleMask=0xFFFFFFFF;d.blendWord=0x10106;
    const uint8_t texel[]={255,255,255,255};d.texture=b.createTexture(1,1,TextureFormat::RGBA8,texel);
    d.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;d.sampler.AddressU=d.sampler.AddressV=d.sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    d.sampler.MaxLOD=13;d.sampler.MaxAnisotropy=1;d.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
    for(unsigned row=0;row<4;++row){d.constants[row][row]=1;d.constants[4+row][row]=1;}
    d.constants[8]={1,1,1,1};d.constants[9][0]=1;d.constants[15]={255,255,255,255};
    d.constants[16]={1,1,1,63.75};d.constants[17]={1,1,1000,0};d.constants[19][3]=255;d.constants[25][0]=-1;
    for(unsigned i=0;i<4;++i){ParticleVertex v{};v.position={0,0,.5,0};v.size={.5,0,1};v.color={16.0f/255,16.0f/255,16.0f/255,0};v.originalVertexId=float(i);d.vertices.push_back(v);}
    auto* context=NativeIm2DProbe::context(b);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    b.drawParticles(color,depth,d);auto pixels=b.readbackTarget(color);auto depths=b.readbackDepthTarget(depth);
    unsigned covered=0;
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){
        const auto v=word(pixels,y*16+x);const bool inside=x>=4&&x<12&&y>=4&&y<12;
        if(v)++covered;
        if(inside){
            // Identity projection, zero age/rotation, half-unit size. Normalized byte16
            // is scaled2/255 in VS, then doubled by the PS's DOT2ADD.
            if((v&1023)!=1||((v>>10)&1023)!=1||((v>>20)&1023)!=1||(v>>30)!=3){
                std::fprintf(stderr,"unexpected color x=%u y=%u code=%08X\n",x,y,v);throw Error("Particle original shader color differs");}
            need(word(depths,2*(y*16+x))==0x3F000000,"Particle depth differs");
        }else need(!v,"Particle coverage exceeds expected quad");
    }
    need(covered==64,"Particle quad did not cover its expected square");
    b.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);
    D3D11_PRIMITIVE_TOPOLOGY topology;context->IAGetPrimitiveTopology(&topology);need(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"Particle topology leaked");
    b.drawParticles(color,depth,d);need((word(b.readbackTarget(color),8*16+8)&1023)==2,"Particle blend lost preceding output");
    auto type5=d;type5.vertexPolicy=ParticleVertexPolicy::Type5;
    type5.blendEnable=0;
    b.clearTarget(color,{0,0,0,0});
    const auto type5Before=b.particleDrawCount();b.drawParticles(color,depth,type5);
    need(b.particleDrawCount()==type5Before+1,"Type-5 particle vertex shader did not submit");
    b.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);
    b.clearTarget(color,{0,0,0,0});b.drawParticles(color,depth,d);b.drawParticles(color,depth,d);
    d.alphaReference=255;b.drawParticles(color,depth,d);need((word(b.readbackTarget(color),8*16+8)&1023)==2,"Particle alpha reject modified color");
    d.alphaReference=0;d.blendEnable=0;b.drawParticles(color,depth,d);
    need((word(b.readbackTarget(color),8*16+8)&1023)==1,"Disabled particle blending did not replace destination");
    b.drawParticles(color,depth,d);need((word(b.readbackTarget(color),8*16+8)&1023)==1,"Disabled particle blending accumulated destination");
    const uint8_t opaqueBlack[]={0,0,0,255};auto black=d;
    black.texture=b.createTexture(1,1,TextureFormat::RGBA8,opaqueBlack);black.blendEnable=1;
    b.drawParticles(color,depth,black);
    need((word(b.readbackTarget(color),8*16+8)&0x3FFFFFFF)==0x00100401,
         "Additive particle black texel replaced the destination with a dark card");
    black.blendEnable=0;b.drawParticles(color,depth,black);
    need((word(b.readbackTarget(color),8*16+8)&0x3FFFFFFF)==0,
         "Disabled particle blending did not reproduce the opaque black card");
    auto invalid=d;invalid.vertices.pop_back();const auto before=b.particleDrawCount();bool rejected=false;
    try{b.drawParticles(color,depth,invalid);}catch(const Error&){rejected=true;}
    need(rejected&&b.particleDrawCount()==before,"Invalid particle extent submitted");
    auto dual=d;dual.secondaryPolicy=ParticleSecondarySamplePolicy::DualTexture1LinearRepeat;dual.secondaryTexture=d.texture;
    dual.secondarySampler=d.sampler;dual.secondarySampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    b.bindEngineTexture(3,d.texture);ComPtr<ID3D11ShaderResourceView> savedTexture3;context->PSGetShaderResources(3,1,&savedTexture3);
    ComPtr<ID3D11SamplerState> savedSampler1;D3D11_SAMPLER_DESC sentinel1=d.sampler;
    need(SUCCEEDED(NativeIm2DProbe::device(b)->CreateSamplerState(&sentinel1,&savedSampler1)),"Dual particle sentinel sampler creation failed");
    auto* sampler1=savedSampler1.Get();context->PSSetSamplers(1,1,&sampler1);
    const auto dualState=[&] {
        ComPtr<ID3D11ShaderResourceView> texture;ComPtr<ID3D11SamplerState> sampler;
        context->PSGetShaderResources(3,1,&texture);context->PSGetSamplers(1,1,&sampler);
        need(texture==savedTexture3&&sampler==savedSampler1,"Dual particle stage3 resource/stage1 sampler leaked");
        b.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);
    };
    b.clearTarget(color,{0,0,0,0});b.drawParticles(color,depth,dual);dualState();
    const uint32_t dualWhite=word(b.readbackTarget(color),8*16+8);need(dualWhite,"Dual particle white second texture produced no output");
    const uint8_t zeroTexel[]={0,0,0,0};dual.secondaryTexture=b.createTexture(1,1,TextureFormat::RGBA8,zeroTexel);
    b.clearTarget(color,{0,0,0,0});b.drawParticles(color,depth,dual);dualState();
    need(word(b.readbackTarget(color),8*16+8)==0,"Dual particle shader did not consume the second texture");
    const auto invalidDual=[&](const ParticleDraw& value) {
        const auto count=b.particleDrawCount();const auto pixelsBefore=b.readbackTarget(color);bool failed=false;
        try{b.drawParticles(color,depth,value);}catch(const Error&){failed=true;}
        need(failed&&b.particleDrawCount()==count&&pixelsBefore==b.readbackTarget(color),"Invalid dual particle submission mutated output/count");
        dualState();
    };
    invalid=dual;invalid.secondaryTexture.reset();invalidDual(invalid);
    invalid=dual;invalid.secondarySampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;invalidDual(invalid);
    auto projected=d;projected.shadowPolicy=ParticleShadowSamplePolicy::ReferenceD24FS8DepthRRRR;
    projected.shadow=b.createDepthTarget(1024,1024);projected.shadowSampler=d.sampler;
    projected.shadowSampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    projected.shadowSampler.AddressU=projected.shadowSampler.AddressV=D3D11_TEXTURE_ADDRESS_MIRROR;
    projected.constants[24]={0,0,.5f,1};projected.constants[25][0]=.4f;
    for(auto& vertex:projected.vertices)vertex.color={1,1,1,0};
    // Retained unrelated stage2 input and sampler must survive both variants.
    b.bindEngineTexture(2,d.texture);ComPtr<ID3D11ShaderResourceView> savedTexture;context->PSGetShaderResources(2,1,&savedTexture);
    ComPtr<ID3D11SamplerState> savedSampler;D3D11_SAMPLER_DESC sentinel=d.sampler;
    need(SUCCEEDED(NativeIm2DProbe::device(b)->CreateSamplerState(&sentinel,&savedSampler)),"Particle sentinel sampler creation failed");
    auto* sampler2=savedSampler.Get();context->PSSetSamplers(2,1,&sampler2);
    const auto shadowState=[&] {
        ComPtr<ID3D11ShaderResourceView> texture;ComPtr<ID3D11SamplerState> sampler;
        context->PSGetShaderResources(2,1,&texture);context->PSGetSamplers(2,1,&sampler);
        need(texture==savedTexture&&sampler==savedSampler,"Particle stage2 resource/sampler leaked");
        b.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);
    };
    b.clearDepthTarget(projected.shadow,0,0);b.drawParticles(color,depth,projected);shadowState();
    const uint32_t lit=word(b.readbackTarget(color),8*16+8);
    b.clearDepthTarget(projected.shadow,1,0);b.drawParticles(color,depth,projected);shadowState();
    const uint32_t shaded=word(b.readbackTarget(color),8*16+8);
    need((lit&1023)==16&&(shaded&1023)==6&&(lit>>30)==3&&(shaded>>30)==3,
         "Full particle VS/projected PS failed native shadow depth sampling or ambient/alpha");
    need(word(b.readbackDepthTarget(depth),2*(8*16+8))==0x3F000000,"Projected particles changed geometric depth");
    const auto invalidShadow=[&](const ParticleDraw& value) {
        const auto count=b.particleDrawCount();const auto pixelsBefore=b.readbackTarget(color);bool failed=false;
        try{b.drawParticles(color,depth,value);}catch(const Error&){failed=true;}
        need(failed&&b.particleDrawCount()==count&&pixelsBefore==b.readbackTarget(color),"Invalid projected particle submission mutated output/count");
        shadowState();
    };
    invalid=projected;invalid.shadow.reset();invalidShadow(invalid);
    invalid=projected;invalid.shadow=depth;invalidShadow(invalid);
    invalid=projected;invalid.shadowPolicy=ParticleShadowSamplePolicy::None;invalidShadow(invalid);
    invalid=projected;invalid.shadowSampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;invalidShadow(invalid);
    invalid=projected;invalid.shadowSampler.AddressU=D3D11_TEXTURE_ADDRESS_CLAMP;invalidShadow(invalid);
    invalid=projected;invalid.constants[25][0]=-1;invalidShadow(invalid);
    // The original registry independently combines two VS with four PS. Use
    // nondegenerate streak geometry for type5 (position.w is its trail extent),
    // and distinct retained stage2/stage3 sentinels to prove state restoration.
    unsigned matrixCases=0;
    const uint8_t secondHalf[]={128,128,128,255};
    const auto halfTexture=b.createTexture(1,1,TextureFormat::RGBA8,secondHalf);
    const auto blackTexture=b.createTexture(1,1,TextureFormat::RGBA8,opaqueBlack);
    for(auto vertexPolicy:{ParticleVertexPolicy::Ordinary,ParticleVertexPolicy::Type5})
    for(bool withShadow:{false,true})for(bool withDual:{false,true}) {
        auto family=d;family.vertexPolicy=vertexPolicy;family.blendEnable=0;family.alphaReference=0;
        for(auto& vertex:family.vertices) {
            vertex.color={1,1,1,0};
            if(vertexPolicy==ParticleVertexPolicy::Type5){vertex.position[3]=1;vertex.velocity={.5f,.125f,0,0};}
        }
        if(vertexPolicy==ParticleVertexPolicy::Type5)family.constants[18]={1,0,0,1};
        if(withShadow){family.shadowPolicy=projected.shadowPolicy;family.shadow=projected.shadow;
            family.shadowSampler=projected.shadowSampler;family.constants[24]=projected.constants[24];
            family.constants[25]=projected.constants[25];}
        if(withDual){family.secondaryPolicy=dual.secondaryPolicy;family.secondaryTexture=d.texture;
            family.secondarySampler=dual.secondarySampler;}
        const auto submit=[&](const ParticleDraw& value){
            b.clearTarget(color,{0,0,0,0});const auto count=b.particleDrawCount();
            b.drawParticles(color,depth,value);need(b.particleDrawCount()==count+1,"Particle family did not submit");
            shadowState();dualState();context->IAGetPrimitiveTopology(&topology);
            need(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"Particle family topology leaked");
            return word(b.readbackTarget(color),8*16+8);
        };
        b.clearDepthTarget(projected.shadow,0,0);const auto white=submit(family);
        need((white&1023)==16&&((white>>10)&1023)==16&&((white>>20)&1023)==16&&(white>>30)==3,
             "Particle family failed original visible geometry/color/alpha");
        for(auto u:{D3D11_TEXTURE_ADDRESS_WRAP,D3D11_TEXTURE_ADDRESS_CLAMP})
        for(auto v:{D3D11_TEXTURE_ADDRESS_WRAP,D3D11_TEXTURE_ADDRESS_CLAMP}) {
            auto addressed=family;addressed.sampler.AddressU=u;addressed.sampler.AddressV=v;
            need(submit(addressed)==white,"Particle family base wrap/clamp addressing failed");
        }
        need(word(b.readbackDepthTarget(depth),2*(8*16+8))==0x3F000000,
             "Particle family changed original geometric depth");
        auto blackBase=family;blackBase.texture=blackTexture;
        need((submit(blackBase)&0x3FFFFFFF)==0,"Particle family failed to consume base texture");
        const auto rejected=[&](const ParticleDraw& value){
            const auto count=b.particleDrawCount();const auto pixelsBefore=b.readbackTarget(color);
            bool failed=false;try{b.drawParticles(color,depth,value);}catch(const Error&){failed=true;}
            need(failed&&count==b.particleDrawCount()&&pixelsBefore==b.readbackTarget(color),
                 "Invalid particle family texture owner/sampler mutated output");shadowState();dualState();
        };
        auto badBase=family;badBase.texture.reset();rejected(badBase);
        badBase=family;badBase.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;rejected(badBase);
        badBase=family;badBase.sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;rejected(badBase);
        badBase=family;badBase.sampler.MaxLOD=0;rejected(badBase);
        if(withDual){auto changed=family;changed.secondaryTexture=halfTexture;
            need((submit(changed)&1023)==8,"Particle family failed to consume secondary texture");
            changed=family;changed.secondaryTexture.reset();rejected(changed);
            changed=family;changed.secondarySampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;rejected(changed);
            if(withShadow){changed=family;changed.shadow.reset();rejected(changed);
                changed=family;changed.shadow=depth;rejected(changed);
                changed=family;changed.shadowSampler.AddressV=D3D11_TEXTURE_ADDRESS_WRAP;rejected(changed);}
        }
        if(withShadow){b.clearDepthTarget(projected.shadow,1,0);const auto shadedFamily=submit(family);
            need((shadedFamily&1023)==6&&(shadedFamily>>30)==3,
                 "Particle family failed projected RRRR sampling or changed alpha");}
        ++matrixCases;
    }
    need(matrixCases==8,"Particle family matrix coverage changed");
    b.drawParticles(color,depth,d);shadowState();
    need((word(b.readbackTarget(color),8*16+8)&1023)==1,"Ordinary particle path changed after projected draw");
    if(NativeIm2DProbe::device(b)->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1){
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=16;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=4;
        ComPtr<ID3D11Buffer> buffer;need(SUCCEEDED(NativeIm2DProbe::device(b)->CreateBuffer(&desc,nullptr,&buffer)),"Particle high UAV storage creation failed");
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{};view.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;view.Buffer.NumElements=4;
        ComPtr<ID3D11UnorderedAccessView> uav;need(SUCCEEDED(NativeIm2DProbe::device(b)->CreateUnorderedAccessView(buffer.Get(),&view,&uav)),"Particle high UAV view creation failed");
        for(UINT slot:{1u,8u,63u}){
            auto* raw=uav.Get();context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,slot,1,&raw,nullptr);
            const auto pixelsBefore=b.readbackTarget(color);const auto depthBytesBefore=b.readbackDepthTarget(depth);const auto countBefore=b.particleDrawCount();
            bool rejectedUav=false;try{b.drawParticles(color,depth,d);}catch(const Error&){rejectedUav=true;}
            need(rejectedUav&&b.particleDrawCount()==countBefore,"Particle high UAV draw was not rejected before submission");
            ComPtr<ID3D11UnorderedAccessView> retained;context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,slot,1,&retained);
            need(retained==uav&&b.readbackTarget(color)==pixelsBefore&&b.readbackDepthTarget(depth)==depthBytesBefore,"Particle rejected high UAV draw changed state or attachments");
            ID3D11UnorderedAccessView* empty=nullptr;context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,slot,1,&empty,nullptr);
        }
    }
    ComPtr<ID3D11InfoQueue> queue;NativeIm2DProbe::device(b)->QueryInterface(IID_PPV_ARGS(&queue));
    if(queue)for(UINT64 i=0;i<queue->GetNumStoredMessages();++i){SIZE_T n=0;queue->GetMessage(i,nullptr,&n);std::vector<uint8_t> bytes(n);auto* message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());queue->GetMessage(i,message,&n);
        if(message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"D3D11: %s\n",message->pDescription);throw Error("Particle D3D11 validation error");}}
    std::puts("PASS particle shader geometry/color/depth/overlap/alpha/state and invalid submission");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL particle backend: %s\n",e.what());return 1;}}
