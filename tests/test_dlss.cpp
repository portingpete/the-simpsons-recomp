#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "renderer/rigid_mesh.h"
#include "renderer/skin_mesh.h"
#include "renderer/ball_effect.h"
#include "renderer/immediate_draw.h"
#include "renderer/dlss.h"
#include "renderer/temporal_scene.h"
#include "renderer/ui_composite.h"
#include "renderer/native_material_compiler.h"
#include "renderer/effect_reflection.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
using namespace Simpsons::Graphics;
void require(bool value,const char* why){if(!value)throw Error(why);}
template<class F>void rejects(F action){bool failed=false;try{action();}catch(const Error&){failed=true;}require(failed,"Invalid temporal operation did not reject");}
#include "header/test_engine_binding_reset.h"
namespace Simpsons::Graphics {
struct NativeUiCompositeProbe {
    static void wait(NativeBackend& backend,const std::shared_ptr<NativeUiCompositeBundle>& bundle) {
        backend.waitCopy(bundle->completion);
        require(backend.copyComplete(bundle->completion),"World grid promotion has no real completed GPU event");
    }
};
struct NativeRecordingProbe {
    static ID3D11Device* device(NativeBackend& b){return b.device.Get();}
    static ID3D11DeviceContext* context(NativeBackend& b){return b.context.Get();}
    static ID3D11Texture2D* motion(NativeBackend& b){return b.temporalScene->motion();}
    static ID3D11Texture2D* bias(NativeBackend& b){return b.temporalScene->biasCurrent();}
    static NvidiaDlss& ngx(NativeBackend& b){return *b.nvidiaDlss;}
    static std::shared_ptr<RenderTarget> composeRetainedNgx(NativeBackend& b,const std::shared_ptr<RenderTarget>& source,std::array<float,2> sourceJitter={}) {
        b.temporalScene->copyOutput(b.nvidiaDlss->outputView(),source->sampledView.Get(),b.temporalOutput->view.Get(),
            b.temporalOutput->pixelWidth(),b.temporalOutput->pixelHeight(),sourceJitter);return b.temporalOutput;
    }
    static std::array<float,2> frameJitter(NativeBackend& b){return b.temporalScene->frame().jitter;}
    static DlssFrame frame(NativeBackend& b){return b.temporalScene->frame();}
    static void uploadAlphaPattern(NativeBackend& b,const std::shared_ptr<RenderTarget>& target,const std::vector<uint32_t>& pixels) {
        b.context->UpdateSubresource(target->texture.Get(),0,nullptr,pixels.data(),target->pixelWidth()*4,0);
    }
};
}
std::array<float,2> motionCenter(NativeBackend& backend) {
    auto* texture=NativeRecordingProbe::motion(backend);D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    require(desc.Format==DXGI_FORMAT_R32G32_FLOAT&&desc.SampleDesc.Count==1,"Motion descriptor differs");
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> copy;require(SUCCEEDED(NativeRecordingProbe::device(backend)->CreateTexture2D(&desc,nullptr,&copy)),"Motion staging failed");
    auto* context=NativeRecordingProbe::context(backend);context->CopyResource(copy.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};require(SUCCEEDED(context->Map(copy.Get(),0,D3D11_MAP_READ,0,&mapped)),"Motion readback failed");
    std::array<float,2> result{};std::memcpy(result.data(),static_cast<const uint8_t*>(mapped.pData)+size_t(desc.Height/2)*mapped.RowPitch+size_t(desc.Width/2)*8,8);
    context->Unmap(copy.Get(),0);return result;
}
uint32_t centerAlpha(const std::vector<uint8_t>& pixels,uint32_t width,uint32_t height) {
    require(pixels.size()==size_t(width)*height*4,"Packed alpha fixture readback extent differs");
    uint32_t packed{};std::memcpy(&packed,pixels.data()+(size_t(height/2)*width+width/2)*4,4);return packed>>30;
}
uint32_t patternedAlpha(uint32_t x,uint32_t y) {return (x^(3*y)^(x/7)^(y/11))&3;}
void verifyPattern(const std::vector<uint8_t>& pixels,uint32_t width,uint32_t height,uint32_t sourceWidth,uint32_t sourceHeight) {
    require(pixels.size()==size_t(width)*height*4,"Nonuniform packed-alpha readback extent differs");
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
        const auto sx=uint32_t((uint64_t(2)*x+1)*sourceWidth/(uint64_t(2)*width));
        const auto sy=uint32_t((uint64_t(2)*y+1)*sourceHeight/(uint64_t(2)*height));
        uint32_t packed{};std::memcpy(&packed,pixels.data()+(size_t(y)*width+x)*4,4);
        require((packed>>30)==patternedAlpha(sx,sy),"Nonuniform packed alpha differs from independent integer nearest mapping");
    }
}
// Use real world depth and retained real NGX RGB; replace only the independently
// retained two-bit channel with a high-frequency source-grid code pattern.
uint32_t verifyWorldAlphaGrid(NativeBackend& backend,const std::shared_ptr<RenderTarget>& color,
    const std::shared_ptr<DepthTarget>& depth,std::array<float,2> jitter,const std::shared_ptr<RenderTarget>& evaluatedOutput) {
    const auto evaluatedPixels=backend.readbackTarget(evaluatedOutput);
    const auto sw=color->pixelWidth(),sh=color->pixelHeight();
    const auto raw=backend.readbackTarget(color),sourceDepth=backend.readbackDepthTarget(depth);
    require(raw.size()==size_t(sw)*sh*4&&sourceDepth.size()==size_t(sw)*sh*8,"World grid source readbacks have invalid extents");
    std::vector<uint32_t> codes(size_t(sw)*sh);
    for(uint32_t y=0;y<sh;++y)for(uint32_t x=0;x<sw;++x) {
        const auto at=size_t(y)*sw+x;std::memcpy(&codes[at],raw.data()+at*4,4);
        codes[at]=(codes[at]&0x3fffffffu)|(patternedAlpha(x,y)<<30);
    }
    NativeRecordingProbe::uploadAlphaPattern(backend,color,codes);
    const EngineBindingResetProbe::Snapshot before(NativeRecordingProbe::context(backend));
    auto output=NativeRecordingProbe::composeRetainedNgx(backend,color,jitter);backend.waitIdle();
    const auto alpha=backend.readbackTarget(output);const auto ow=output->pixelWidth(),oh=output->pixelHeight();
    require(evaluatedPixels.size()==alpha.size(),"Real NGX RGB retention changed the evaluated output extent");
    auto bundle=backend.promoteUiComposite(color,depth,output,jitter);
    const auto transferred=backend.readbackDepthTarget(backend.routeDepth(depth));
    require(alpha.size()==size_t(ow)*oh*4&&transferred.size()==size_t(ow)*oh*8,"World grid output readbacks have invalid extents");
    // Double-precision analytic centers form an independent oracle. Only an
    // IEEE float roundoff neighborhood of an integer boundary may use either
    // adjoining pixel; alpha, depth and stencil must agree on the SAME candidate.
    const double radius=4.0*0x1p-23*std::max(sw,sh);
    for(uint32_t y=0;y<oh;++y)for(uint32_t x=0;x<ow;++x) {
        const double sx=(double(x)+0.5)*sw/ow+double(jitter[0]);
        const double sy=(double(y)+0.5)*sh/oh+double(jitter[1]);
        const auto loX=std::clamp(int64_t(std::floor(sx-radius)),int64_t(0),int64_t(sw-1));
        const auto hiX=std::clamp(int64_t(std::floor(sx+radius)),int64_t(0),int64_t(sw-1));
        const auto loY=std::clamp(int64_t(std::floor(sy-radius)),int64_t(0),int64_t(sh-1));
        const auto hiY=std::clamp(int64_t(std::floor(sy+radius)),int64_t(0),int64_t(sh-1));
        const auto at=size_t(y)*ow+x;uint32_t packed{},actualDepth{},actualStencil{};
        std::memcpy(&packed,alpha.data()+at*4,4);std::memcpy(&actualDepth,transferred.data()+at*8,4);std::memcpy(&actualStencil,transferred.data()+at*8+4,4);
        uint32_t evaluatedWord{};std::memcpy(&evaluatedWord,evaluatedPixels.data()+at*4,4);
        require((packed&0x3fffffffu)==(evaluatedWord&0x3fffffffu),"Packed-alpha grid transfer modified real evaluated NGX RGB");
        bool matched=false;
        for(int64_t gy=loY;gy<=hiY;++gy)for(int64_t gx=loX;gx<=hiX;++gx) {
            const auto source=(size_t(gy)*sw+size_t(gx))*8;uint32_t expectedDepth{},expectedStencil{};
            std::memcpy(&expectedDepth,sourceDepth.data()+source,4);std::memcpy(&expectedStencil,sourceDepth.data()+source+4,4);
            matched|=(packed>>30)==patternedAlpha(uint32_t(gx),uint32_t(gy))&&actualDepth==expectedDepth&&
                (actualStencil&255u)==(expectedStencil&255u);
        }
        require(matched,"Actual NGX world alpha and promoted depth/stencil disagree with one independently mapped jitter-grid source");
    }
    NativeUiCompositeProbe::wait(backend,bundle);backend.clearUiComposite();
    require(EngineBindingResetProbe::Snapshot(NativeRecordingProbe::context(backend))==before,"World alpha/depth grid composition changed original state");
    require(backend.readbackTarget(output)==alpha,"Paired depth promotion modified composed real NGX output");
    const auto retainedColor=backend.readbackTarget(color);
    require(retainedColor.size()==codes.size()*4&&std::memcmp(retainedColor.data(),codes.data(),retainedColor.size())==0,"Packed-alpha composition modified the retained original source RGB/alpha");
    const auto retained=backend.readbackDepthTarget(depth);
    for(size_t at=0;at<retained.size();at+=8)require(std::memcmp(retained.data()+at,sourceDepth.data()+at,5)==0,"World alpha-grid fixture changed source depth/stencil");
    return (jitter[0]>0?1u:0u)|(jitter[0]<0?2u:0u)|(jitter[1]>0?4u:0u)|(jitter[1]<0?8u:0u);
}
std::vector<uint8_t> temporalPixels(NativeBackend& backend,ID3D11Texture2D* texture,uint32_t pixelBytes) {
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);const auto width=desc.Width,height=desc.Height;
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> copied;require(SUCCEEDED(NativeRecordingProbe::device(backend)->CreateTexture2D(&desc,nullptr,&copied)),"Temporal attachment staging allocation failed");
    auto* context=NativeRecordingProbe::context(backend);context->CopyResource(copied.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};require(SUCCEEDED(context->Map(copied.Get(),0,D3D11_MAP_READ,0,&mapped)),"Temporal attachment readback failed");
    std::vector<uint8_t> pixels(size_t(width)*height*pixelBytes);
    for(uint32_t y=0;y<height;++y)std::memcpy(pixels.data()+size_t(y)*width*pixelBytes,static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch,size_t(width)*pixelBytes);
    context->Unmap(copied.Get(),0);return pixels;
}
bool sameDefinedDepth(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b) {
    if(a.size()!=b.size()||a.size()%8)return false;
    for(size_t at=0;at<a.size();at+=8)if(std::memcmp(a.data()+at,b.data()+at,5))return false;
    return true; // D32_FLOAT plus low S8 byte; X24 padding has no defined value.
}
struct Phase9CopyChain {
    std::array<const CompiledMaterial*,6> shaders{};
    std::shared_ptr<RenderTarget> base,line,colorCopy;
    std::shared_ptr<DepthTarget> depthCopy;
    std::shared_ptr<Texture> palette;
    Phase9CopyChain(NativeBackend& backend,NativeMaterialCompiler& compiler,MaterialRegistry& registry,std::span<const uint8_t> image) {
        constexpr std::array<uint32_t,6> addresses={0x8202E6F0,0x8202E840,0x820301A0,0x820302EC,0x820347B0,0x82034900};
        for(size_t i=0;i<addresses.size();++i)for(const auto& record:originalMaterialIdentities())if(record.originalAddress==addresses[i]) {
            const auto id=registry.create(record.originalAddress,image.subspan(record.originalAddress-0x82000000,record.recordBytes));
            shaders[i]=&registry.prepareForBind(id,compiler);
        }
        for(auto* shader:shaders)require(bool(shader),"Phase 9 original shader profile missing");
        base=backend.createTarget(1280,720,TargetFormat::RGB10A2,TargetScale::Scene);
        line=backend.createTarget(1280,720,TargetFormat::RGB10A2,TargetScale::Scene);
        colorCopy=backend.createTarget(1280,720,TargetFormat::RGB10A2,TargetScale::Scene);
        depthCopy=backend.createDepthTarget(1280,720,TargetScale::Scene);
        std::vector<uint8_t> bytes(64*64*4);for(size_t at=0;at<bytes.size();at+=4){bytes[at]=51;bytes[at+1]=102;bytes[at+2]=204;bytes[at+3]=255;}
        palette=backend.createTexture(64,64,TextureFormat::RGBA8,bytes);
    }
    void run(NativeBackend& backend,const std::shared_ptr<RenderTarget>& color,const std::shared_ptr<DepthTarget>& depth) {
        const auto before=backend.temporalSceneOrigins();const auto jitter=backend.temporalSceneJitter();
        const auto owningFrameBefore=NativeRecordingProbe::frame(backend);
        backend.requireTemporalSceneOrigin(color,depth,jitter);
        require(before.components[0].kind==TemporalRasterOriginKind::Grid&&before.components[0].jitter==jitter,"Real world color has no actual current raster-grid receipt");
        const auto depthPixels=backend.readbackDepthTarget(depth);
        const auto motionPixels=temporalPixels(backend,NativeRecordingProbe::motion(backend),8);
        const auto biasPixels=temporalPixels(backend,NativeRecordingProbe::bias(backend),1);
        const std::array<uint64_t,5> geometry={backend.rigidMeshDrawCount(),backend.skinMeshDrawCount(),backend.ballEffectDrawCount(),
            backend.recordingDrawCount(),backend.recordingExecutedDrawCount()};
        const auto bind=[&](size_t shader) {backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.setViewport({0,0,1280,720,0,1});
            backend.bindEdgeShaders(*shaders[shader],*shaders[shader+1]);backend.bindEdgeDeclaration();};
        EdgeConstants constants{};constants.kernel[0]={0,1,0,1};constants.kernel[1]={1,0,0,1};
        constants.kernel[2]={0,-1,0,1};constants.kernel[3]={-1,0,0,1};constants.dimensions={1280,720,1,0};
        D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxAnisotropy=1;sampler.MaxLOD=13;
        const std::array<ScreenVertex,3> corners={ScreenVertex{-1,1,0,0},ScreenVertex{1,1,1,0},ScreenVertex{-1,-1,0,1}};
        backend.copyFront(color,base);bind(0);const auto edge=backend.commitEdge(base,constants,sampler);
        backend.drawEdge(color,depth,edge,corners,*shaders[0],*shaders[1]);backend.requireTemporalSceneOrigin(color,depth,jitter);
        backend.copyFront(color,line);bind(2);const auto aa=backend.commitEdge(line,constants,sampler,true);
        backend.drawEdge(color,depth,aa,corners,*shaders[2],*shaders[3]);backend.requireTemporalSceneOrigin(color,depth,jitter);
        backend.copyFront(color,colorCopy);backend.copyDepth(depth,depthCopy);bind(4);
        EdgeAAInputs inputs{colorCopy,base,line,depthCopy,palette};
        EdgeAAConstants composite{};composite.c20_27={std::array<float,4>{64,0,0,1},{160,0,0,1},{.4f,0,0,1},{1280,0,0,1},
            {720,0,0,1},{1,0,0,1},{0,0,0,1},{1,0,0,1}};
        composite.c48_50={std::array<float,4>{10,0,0,1},{1.5f,0,0,1},{1,0,0,1}};
        const std::array<D3D11_SAMPLER_DESC,5> samplers={sampler,sampler,sampler,sampler,sampler};
        const auto edgeaa=backend.commitEdgeAA(inputs,composite,samplers);
        backend.drawEdge(color,depth,edgeaa,corners,*shaders[4],*shaders[5]);backend.requireTemporalSceneOrigin(color,depth,jitter);
        const auto after=backend.temporalSceneOrigins();
        require(after.components[0].revision==before.components[0].revision+3&&after.components[0].jitter==jitter&&
            after.components[1]==before.components[1]&&after.components[2]==before.components[2],"Phase 9 chain published incorrect component origin/revisions");
        require(sameDefinedDepth(backend.readbackDepthTarget(depth),depthPixels)&&temporalPixels(backend,NativeRecordingProbe::motion(backend),8)==motionPixels&&
            temporalPixels(backend,NativeRecordingProbe::bias(backend),1)==biasPixels,"Color-only filters changed real world depth/stencil, motion or NGX bias");
        const auto owningFrameAfter=NativeRecordingProbe::frame(backend);
        require(owningFrameBefore.geometryDraws&&owningFrameBefore.motionDraws&&
            owningFrameAfter.geometryDraws==owningFrameBefore.geometryDraws&&owningFrameAfter.motionDraws==owningFrameBefore.motionDraws,
            "Phase 9 filters changed the actual owning temporal frame geometry/motion counts");
        require((std::array<uint64_t,5>{backend.rigidMeshDrawCount(),backend.skinMeshDrawCount(),backend.ballEffectDrawCount(),
            backend.recordingDrawCount(),backend.recordingExecutedDrawCount()})==geometry,"Phase 9 filters changed actual world geometry/motion draw counts");
    }
};
int main(int argc,char** argv) {
    try {
        require(argc==3,"Usage: test_dlss <analysis/simpsons.pe> <dlaa|quality|balanced|performance|ultra>");
        std::ifstream input(argv[1],std::ios::binary);std::vector<uint8_t> image{std::istreambuf_iterator<char>(input),{}};
        require(!image.empty(),"Original shader image is missing");
        Antialiasing mode=std::string(argv[2])=="dlaa"?Antialiasing::DLAA:std::string(argv[2])=="quality"?Antialiasing::DLSSQuality:
            std::string(argv[2])=="balanced"?Antialiasing::DLSSBalanced:std::string(argv[2])=="performance"?Antialiasing::DLSSPerformance:Antialiasing::DLSSUltraPerformance;
        NativeBackend backend(false);backend.configureRendering(1280,720,1,mode);
        const auto outputExtent=backend.outputExtent();
        const auto renderExtent=backend.configureTemporalRendering(mode,outputExtent[0],outputExtent[1]);
        backend.configureSceneExtent(renderExtent[0],renderExtent[1]);
        require(NativeRecordingProbe::ngx(backend).capability().available,"Real NVIDIA NGX capability is unavailable");
        NativeMaterialCompiler compiler(backend);MaterialRegistry registry;
        const CompiledMaterial *rigidVS=nullptr,*rigidPS=nullptr,*skinVS=nullptr,*skinPS=nullptr;
        for(const auto& record:originalMaterialIdentities()) {
            if(record.originalAddress!=0x8205BE70&&record.originalAddress!=0x8205C100&&record.originalAddress!=0x82007C1C&&record.originalAddress!=0x8200A02C)continue;
            const auto id=registry.create(record.originalAddress,std::span<const uint8_t>(image).subspan(record.originalAddress-0x82000000,record.recordBytes));
            const auto& shader=registry.prepareForBind(id,compiler);
            if(record.originalAddress==0x8205BE70)rigidVS=&shader;else if(record.originalAddress==0x8205C100)rigidPS=&shader;
            else if(record.originalAddress==0x82007C1C)skinVS=&shader;else skinPS=&shader;
        }
        require(rigidVS&&rigidPS&&skinVS&&skinPS,"Original temporal fixture shader records missing");
        auto color=backend.createTarget(1280,720,TargetFormat::RGB10A2,TargetScale::Scene);
        auto depth=backend.createDepthTarget(1280,720,TargetScale::Scene);
        backend.registerTemporalWorldTargets(color,depth);
        backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.setViewport({0,0,1280,720,0,1});
        auto* context=NativeRecordingProbe::context(backend);
        const std::array<uint16_t,4> indices={0,1,2,3};
        std::array<RigidVertex,4> vertices{};
        const std::array<std::array<float,2>,4> corners={{{-.6f,.6f},{-.6f,-.6f},{.6f,.6f},{.6f,-.6f}}};
        for(size_t i=0;i<4;++i){vertices[i].position={corners[i][0],corners[i][1],.5f};vertices[i].normal={0,1,0};vertices[i].color={1,1,1,1};vertices[i].uv={.5f,.5f};}
        const auto rigid=backend.uploadRigidMesh(vertices,indices);
        const std::array<uint8_t,16> white={255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255};
        auto texture=backend.createTexture(2,2,TextureFormat::RGBA8,white);
        RigidMeshDraw draw{};draw.primitiveType=6;draw.indexCount=4;draw.viewport={0,0,1280,720,0x3f800000,0};
        draw.depthEnable=draw.depthWrite=1;draw.depthCompare=7;draw.colorMask=15;draw.halfPixelOffset=1;
        draw.primitiveReset=draw.viewportEnable=draw.multisampleAntialias=1;draw.primitiveResetIndex=0xffff;draw.multisampleMask=0xffffffff;
        draw.depthPolicy=ShadowMeshDepthPolicy::Reference20e4Rne;draw.blendWord=draw.temporalBlendWord=0x00010001;draw.expandedBlend=1;
        draw.temporalObject=0x1000;draw.temporalGeneration=1;draw.temporalGeometry=0x2000;draw.temporalSubmesh=0x2100;
        draw.baseTexture=texture;draw.shadowSamplePolicy=RigidShadowSamplePolicy::NotUsed;
        draw.baseSampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;draw.baseSampler.AddressU=draw.baseSampler.AddressV=draw.baseSampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        draw.baseSampler.MaxAnisotropy=1;draw.baseSampler.MaxLOD=13;draw.baseSampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
        RigidVertexConstants vc{};RigidPixelConstants pc{};for(size_t i=0;i<4;++i)vc[i][i]=1;vc[5][0]=vc[6][1]=1;pc[16]={.15f,.6f,.9f,1};
        std::array<SkinVertex,4> skinVertices{};
        for(size_t i=0;i<4;++i){auto& v=skinVertices[i];v.position={corners[i][0],corners[i][1],1};
            v.normal={0,0,-1};v.indices={1,0,0,0};v.weights={1,0,0,0};v.uv={.0111f,.5217f};v.color={1,0,0,1};v.morph1={.2f,0,0};}
        auto skinMesh=backend.uploadSkinMesh(skinVertices,indices,0x82007C1C);
        SkinMeshDraw skinDraw{};static_cast<ShadowMeshDraw&>(skinDraw)=draw;
        skinDraw.blendWord=0x00010001;skinDraw.expandedBlend=1;skinDraw.temporalObject=0x1100;skinDraw.temporalGeometry=0x2200;skinDraw.temporalSubmesh=0x2300;
        SkinVertexConstants skinVC{};SkinPixelConstants skinPC{};
        skinVC[0][0]=skinVC[1][1]=1;skinVC[2][2]=.5f;skinVC[3][3]=1;
        for(size_t i=0;i<4;++i)skinVC[12+i][i]=1;
        for(size_t i=0;i<3;++i)skinVC[55+i][i]=1;
        skinVC[39][3]=1;skinPC[4]={17,-9,200,1};skinPC[40]={0,.34f,-.2f,1};skinPC[47]={1,0,0,1};skinPC[49][2]=7;
        auto recording=backend.createRecordingContext();auto live=backend.createRigidReplayConstants(vc,pc);
        std::shared_ptr<NativeRecordingPayload> payload;
        // Record before a frame has begun, as production recording owners may.
        // Registration identifies the immutable world owners independently of
        // the active camera, and each replay still freezes its own bank.
        backend.bindRigidShaders(*rigidVS,*rigidPS);[[maybe_unused]] const auto initialCommit=backend.commitRigid(*rigidVS,*rigidPS,vc,pc);
        backend.bindRigidMeshVertices(rigid);backend.bindVfxRigidMeshDeclaration(rigid);backend.bindRigidMeshIndices(rigid);backend.bindEngineTexture(0,texture);
        NativeRecordingMask mask{};mask[0]=0x80;payload=backend.allocateRecordingPayload(recording,0x3000);
        backend.beginRecordingPayload(payload,4,mask,mask);backend.recordRigidMesh(payload,color,depth,rigid,*rigidVS,*rigidPS,vc,pc,live,draw);
        backend.finishRecordingPayload(payload);
        Phase9CopyChain phase9(backend,compiler,registry,image);
        uint32_t worldJitterCoverage=0;
        auto frame=[&](bool skin,float expectedMotion) {
            backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.clearTarget(color,{0,0,0,1});backend.clearDepthTarget(depth,0,0);
            backend.beginTemporalScene(color,depth,0x3000,1);
            if(!skin) {
                backend.bindRigidShaders(*rigidVS,*rigidPS);[[maybe_unused]] const auto commit=backend.commitRigid(*rigidVS,*rigidPS,vc,pc);
                backend.bindRigidMeshVertices(rigid);backend.bindVfxRigidMeshDeclaration(rigid);backend.bindRigidMeshIndices(rigid);
                backend.bindEngineTexture(0,texture);
                backend.updateRigidReplayConstants(live,vc,pc);
                const EngineBindingResetProbe::Snapshot before(context);backend.executeRecordingPayload(payload);
                require(EngineBindingResetProbe::Snapshot(context)==before,"Fused temporal geometry changed original bindings");
            }
            else {
                backend.bindSkinShaders(*skinVS,*skinPS);const auto commit=backend.commitSkin(*skinVS,*skinPS,skinVC,skinPC);
                backend.bindSkinMeshVertices(skinMesh);backend.bindSkinMeshDeclaration(skinMesh);backend.bindSkinMeshIndices(skinMesh);
                const EngineBindingResetProbe::Snapshot before(context);
                backend.drawSkinMesh(color,depth,skinMesh,*skinVS,*skinPS,commit,skinDraw);
                require(EngineBindingResetProbe::Snapshot(context)==before,"Fused skinned geometry changed original bindings");
            }
            const auto motion=motionCenter(backend);
            require(std::isfinite(motion[0])&&std::abs(motion[0]-expectedMotion)<.2f&&std::abs(motion[1])<.2f,"GPU object/camera motion differs from independent projection");
            phase9.run(backend,color,depth);
            const EngineBindingResetProbe::Snapshot before(context);
            const auto worldJitter=NativeRecordingProbe::frameJitter(backend);
            auto output=backend.reconstructTemporalScene(color,depth);
            require(EngineBindingResetProbe::Snapshot(context)==before,"Real NGX evaluation changed engine bindings");
            require(output->pixelWidth()==1280&&output->pixelHeight()==720,"NGX output dimensions differ");
            backend.waitIdle();require(!backend.readbackTarget(output).empty(),"NGX GPU output was not readable after completion");
            worldJitterCoverage|=verifyWorldAlphaGrid(backend,color,depth,worldJitter,output);
            backend.finishTemporalFrame();
        };
        frame(false,0);frame(false,0); // Static geometry with different jitter must remain zero velocity.
        vc[0][3]=.1f;frame(false,-.05f*color->pixelWidth()); // Combined actual object+camera projection motion.
        frame(true,0);
        skinVC[55][3]=.1f;frame(true,-.05f*color->pixelWidth()); // Actual original bone translation.
        skinVC[38][0]=.5f;frame(true,-.05f*color->pixelWidth()); // Actual morph delta .2 * weight .5.
        skinVC[0][3]=.1f;frame(true,-.05f*color->pixelWidth()); // Actual current/previous camera projection.
        BallEffectDraw ball{};static_cast<ShadowMeshDraw&>(ball)=draw;
        ball.temporalObject=0x1200;ball.temporalGeometry=0x82156548;ball.temporalSubmesh=0x82771A18;
        ball.texture=texture;ball.sampler=draw.baseSampler;ball.blendWord=0x10001;ball.blendEnable=1;
        ball.constants[0][0]=1;ball.constants[4][0]=1;ball.constants[5][1]=1;
        ball.constants[7]={0,0,.5f,1};ball.constants[8][3]=1.0f/3.0f;
        ball.vertices={{{-.6f,-.6f,0,0},{.6f,-.6f,1,0},{.6f,.6f,1,1},{-.6f,.6f,0,1}}};
        auto ballFrame=[&](float expectedMotion) {
            backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.clearTarget(color,{0,0,0,1});backend.clearDepthTarget(depth,0,0);
            backend.beginTemporalScene(color,depth,0x3000,1);
            const EngineBindingResetProbe::Snapshot before(context);backend.drawBallEffect(color,depth,ball);
            require(EngineBindingResetProbe::Snapshot(context)==before,"Packed UINT ball MRT changed original physical bindings");
            const auto motion=motionCenter(backend);
            require(std::isfinite(motion[0])&&std::abs(motion[0]-expectedMotion)<.2f&&std::abs(motion[1])<.2f,
                "Previous ball center or original CPU quad vertices did not produce measured motion");
            const auto worldJitter=NativeRecordingProbe::frameJitter(backend);
            auto output=backend.reconstructTemporalScene(color,depth);
            require(EngineBindingResetProbe::Snapshot(context)==before,"Ball NGX evaluation changed original engine state");
            backend.waitIdle();const auto pixels=backend.readbackTarget(output);
            require(centerAlpha(pixels,output->pixelWidth(),output->pixelHeight())==2,"NGX RGB composition lost the original packed engine alpha code");
            worldJitterCoverage|=verifyWorldAlphaGrid(backend,color,depth,worldJitter,output);
            backend.finishTemporalFrame();
        };
        ballFrame(0);ball.constants[8][0]=.1f;ballFrame(-.05f*color->pixelWidth());
        for(auto& vertex:ball.vertices)vertex[0]+=.1f;ballFrame(-.05f*color->pixelWidth());
        // Renderer contract fixture only: these native identities deliberately
        // do not assert original guest factory birth. The engine observer has
        // separate genuine-factory/retirement/reuse qualification.
        ImmediateDraw beam{};static_cast<ShadowMeshDraw&>(beam)=draw;
        beam.temporalObject=0x1300;beam.temporalGeneration=1;
        beam.temporalGeometry=0x821511D8;beam.temporalSubmesh=0x82776874;
        beam.texture=texture;beam.sampler=draw.baseSampler;beam.primitive=6;
        beam.depthWrite=0;beam.blendWord=0x10706;beam.blendEnable=beam.alphaTest=1;beam.alphaReference=1;
        for(size_t i=0;i<4;++i)beam.constants[i][i]=1;
        beam.constants[4]={.25f,.5f,.75f,1};
        for(float x:{-.75f,-.25f,.25f,.75f})for(float y:{.6f,-.6f})
            beam.vertices.push_back({{x,y,.5f},1,{.5f,.5f,0,0}});
        ComPtr<ID3D11DeviceContext1> ranged;
        require(SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&ranged))),"Beam fixture requires actual ranged constant bindings");
        std::array<ComPtr<ID3D11Buffer>,4> sentinels;
        for(size_t i=0;i<sentinels.size();++i) {
            D3D11_BUFFER_DESC desc{};desc.ByteWidth=512;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            require(SUCCEEDED(NativeRecordingProbe::device(backend)->CreateBuffer(&desc,nullptr,&sentinels[i])),
                "Beam ranged sentinel allocation failed");
        }
        auto beamFrame=[&](std::array<float,2> leftExpected,std::array<float,2> rightExpected,bool first) {
            backend.bindTargets({color,nullptr,nullptr,nullptr},depth);
            backend.clearTarget(color,{0,0,0,1});backend.clearDepthTarget(depth,0,0x5A);
            backend.beginTemporalScene(color,depth,0x3000,1);
            const UINT start=16,count=16;
            for(UINT i=0;i<3;++i){auto* cb=sentinels[i].Get();ranged->VSSetConstantBuffers1(10+i,1,&cb,&start,&count);}
            auto* psSentinel=sentinels[3].Get();ranged->PSSetConstantBuffers1(11,1,&psSentinel,&start,&count);
            const auto depthBefore=backend.readbackDepthTarget(depth);
            const EngineBindingResetProbe::Snapshot before(context);
            backend.drawImmediate(color,depth,beam);
            require(EngineBindingResetProbe::Snapshot(context)==before,"Beam MRT draw changed original shader/target/viewport/sampler bindings");
            for(UINT i=0;i<4;++i) {
                ComPtr<ID3D11Buffer> actual;UINT actualStart{},actualCount{};
                if(i<3)ranged->VSGetConstantBuffers1(10+i,1,&actual,&actualStart,&actualCount);
                else ranged->PSGetConstantBuffers1(11,1,&actual,&actualStart,&actualCount);
                require(actual==sentinels[i]&&actualStart==start&&actualCount==count,
                    "Beam MRT draw did not restore the exact ranged temporal constant slice");
            }
            require(sameDefinedDepth(backend.readbackDepthTarget(depth),depthBefore),"Transparent Beam changed original depth/stencil despite depth-write zero");
            const auto frame=NativeRecordingProbe::frame(backend);
            require(frame.geometryDraws==1&&frame.motionDraws==1,"Beam counted each expanded triangle as a separate temporal submission");
            const auto motion=temporalPixels(backend,NativeRecordingProbe::motion(backend),8);
            const auto bias=temporalPixels(backend,NativeRecordingProbe::bias(backend),1);
            const auto packed=backend.readbackTarget(color);
            const uint32_t width=color->pixelWidth(),height=color->pixelHeight();
            for(size_t sample=0;sample<2;++sample) {
                const auto x=sample==0?width/4:3*width/4;const size_t pixel=size_t(height/2)*width+x;
                std::array<float,2> measured{};std::memcpy(measured.data(),motion.data()+pixel*8,8);
                const auto expected=sample==0?leftExpected:rightExpected;
                for(size_t lane=0;lane<2;++lane)require(std::isfinite(measured[lane])&&std::abs(measured[lane]-expected[lane])<.2f,
                    "Beam deformation/camera motion differs from independent two-region projection");
                require(bias[pixel]==255,"Conservative transparent Beam did not retain current-frame bias while emitting real motion");
                uint32_t code{};std::memcpy(&code,packed.data()+pixel*4,4);
                require(code==(256u|(512u<<10)|(767u<<20)|(3u<<30)),"Beam fused shading changed original ordered packed RGB10A2 output");
            }
            if(first) {
                auto changed=beam;changed.vertices[0].position[0]+=.01f;
                const auto unchangedMotion=motion,unchangedBias=bias;
                bool aliasRejected=false;
                try {backend.drawImmediate(color,depth,changed);}
                catch(const std::runtime_error& error) {
                    if(std::strcmp(error.what(),"Temporal instance/submesh identity aliases different transforms in one frame")!=0)throw;
                    aliasRejected=true;
                }
                require(aliasRejected,"Same-frame Beam deformation alias did not reject");
                require(backend.readbackTarget(color)==packed&&sameDefinedDepth(backend.readbackDepthTarget(depth),depthBefore)&&
                    temporalPixels(backend,NativeRecordingProbe::motion(backend),8)==unchangedMotion&&
                    temporalPixels(backend,NativeRecordingProbe::bias(backend),1)==unchangedBias,
                    "Same-frame Beam deformation alias changed real attachments before rejection");
                require(EngineBindingResetProbe::Snapshot(context)==before,"Rejected Beam history alias changed inherited native bindings");
            }
            const auto worldJitter=NativeRecordingProbe::frameJitter(backend);
            auto output=backend.reconstructTemporalScene(color,depth);
            require(EngineBindingResetProbe::Snapshot(context)==before,"Beam real NGX evaluation changed engine bindings");
            backend.waitIdle();require(!backend.readbackTarget(output).empty(),"Beam NGX output lacks completed readable storage");
            worldJitterCoverage|=verifyWorldAlphaGrid(backend,color,depth,worldJitter,output);
            backend.finishTemporalFrame();
        };
        beamFrame({0,0},{0,0},true);
        // Mutate the original retained CPU vector after frame completion. The
        // left and right strips deform in opposite directions under one owner.
        for(size_t i=0;i<8;++i)beam.vertices[i].position[0]+=i<4?.1f:-.1f;
        beamFrame({-.05f*color->pixelWidth(),0},{.05f*color->pixelWidth(),0},false);
        for(size_t i=0;i<8;++i){beam.vertices[i].position[0]+=i<4?.02f:-.02f;beam.vertices[i].position[1]+=i<4?.04f:-.04f;}
        beam.constants[3][0]=.06f;beam.constants[3][1]=-.04f;
        beamFrame({-.04f*color->pixelWidth(),0},{-.02f*color->pixelWidth(),-.04f*color->pixelHeight()},false);
        // Real pixel-boundary checks use distinct renderer-only births. They
        // neither fabricate nor qualify an original guest lifetime.
        for(uint32_t boundary=0;boundary<4;++boundary) {
            auto probe=beam;probe.temporalGeneration=2+boundary;
            // A copied generation would emit nonzero motion at boundary2.
            if(boundary==2)for(auto& vertex:probe.vertices)vertex.position[0]+=.04f;
            probe.depthCompare=6; // Original GreaterEqual, reversed depth.
            probe.constants[4][3]=boundary==0?0.0f:boundary==1?1.0f/255.0f:boundary==2?2.0f/255.0f:1.0f;
            backend.bindTargets({color,nullptr,nullptr,nullptr},depth);
            backend.clearTarget(color,{0,0,0,1});backend.clearDepthTarget(depth,boundary==3?1.0f:0.0f,0x5A);
            backend.beginTemporalScene(color,depth,0x3000,1);
            const auto originalColor=backend.readbackTarget(color),originalDepth=backend.readbackDepthTarget(depth);
            const auto originalMotion=temporalPixels(backend,NativeRecordingProbe::motion(backend),8);
            const auto originalBias=temporalPixels(backend,NativeRecordingProbe::bias(backend),1);
            const EngineBindingResetProbe::Snapshot before(context);
            backend.drawImmediate(color,depth,probe);
            require(EngineBindingResetProbe::Snapshot(context)==before,"Beam pixel-boundary draw changed inherited native bindings");
            require(sameDefinedDepth(backend.readbackDepthTarget(depth),originalDepth),"Beam pixel boundary changed original depth/stencil");
            const auto packed=backend.readbackTarget(color);
            const auto motion=temporalPixels(backend,NativeRecordingProbe::motion(backend),8);
            const auto bias=temporalPixels(backend,NativeRecordingProbe::bias(backend),1);
            if(boundary!=2) {
                require(packed==originalColor&&motion==originalMotion&&bias==originalBias,
                    "Alpha-discarded or depth-failed Beam pixel wrote packed color/motion/bias");
            } else {
                const size_t pixel=size_t(color->pixelHeight()/2)*color->pixelWidth()+color->pixelWidth()/4;
                uint32_t actual{};std::memcpy(&actual,packed.data()+pixel*4,4);
                require(actual==(2u|(4u<<10)|(6u<<20))&&bias[pixel]==255,
                    "Beam pixel above original alpha threshold did not shade packed color and current-frame bias");
                std::array<float,2> measured{};std::memcpy(measured.data(),motion.data()+pixel*8,8);
                require(measured[0]==0&&measured[1]==0,"New Beam renderer birth reused earlier deformation history");
            }
            const auto frame=NativeRecordingProbe::frame(backend);
            require(frame.geometryDraws==1&&frame.motionDraws==1,"Coverage-boundary Beam lost its one actual submission");
            auto output=backend.reconstructTemporalScene(color,depth);
            require(EngineBindingResetProbe::Snapshot(context)==before,"Coverage-boundary NGX evaluation changed inherited bindings");
            backend.waitIdle();require(!backend.readbackTarget(output).empty(),"Coverage-boundary NGX output did not complete");
            backend.finishTemporalFrame();
        }
        std::fprintf(stderr,"PASS temporal Beam GPU: three deformation frames, independent regions/camera, ranged restoration, same-frame alias rejection, alpha zero/equal/above boundary, failed depth and new-birth history\n");
        require(worldJitterCoverage==15u,"Real NGX world fixtures lack positive and negative X/Y jitter coverage");
        require(NativeRecordingProbe::ngx(backend).evaluations()==17,"Real NGX mesh and packed world-effect evaluations were not completed");
        for(uint32_t alpha=0;alpha<4;++alpha) {
            backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.clearTarget(color,{.25f,.5f,.75f,float(alpha)/3.0f});
            const EngineBindingResetProbe::Snapshot before(context);const auto output=backend.prepareTemporalUiBackground(color,depth);
            require(EngineBindingResetProbe::Snapshot(context)==before,"UI-only background copy changed original physical state");
            backend.waitIdle();const auto pixels=backend.readbackTarget(output);
            require(centerAlpha(pixels,output->pixelWidth(),output->pixelHeight())==alpha,"UI-only background copy lost packed alpha");
        }
        require(NativeRecordingProbe::ngx(backend).evaluations()==17,"UI-only spatial frames falsely counted as NGX world evaluations");
        const auto sourceWidth=color->pixelWidth(),sourceHeight=color->pixelHeight();
        std::vector<uint32_t> alphaPattern(size_t(sourceWidth)*sourceHeight);
        for(uint32_t y=0;y<sourceHeight;++y)for(uint32_t x=0;x<sourceWidth;++x)
            alphaPattern[size_t(y)*sourceWidth+x]=0x15555555u|(patternedAlpha(x,y)<<30);
        NativeRecordingProbe::uploadAlphaPattern(backend,color,alphaPattern);
        const EngineBindingResetProbe::Snapshot alphaState(context);
        auto composed=NativeRecordingProbe::composeRetainedNgx(backend,color);backend.waitIdle();
        verifyPattern(backend.readbackTarget(composed),composed->pixelWidth(),composed->pixelHeight(),sourceWidth,sourceHeight);
        require(EngineBindingResetProbe::Snapshot(context)==alphaState,"Packed-alpha NGX composition changed original state");
        if(sourceWidth!=1280||sourceHeight!=720)rejects([&]{backend.resolveAntialiasing(color);});
        const auto verifyPresentation=[&](const std::shared_ptr<RenderTarget>& output) {
            const auto originalPixels=backend.readbackTarget(output);
            const EngineBindingResetProbe::Snapshot before(context);
            const auto untouched=backend.resolveAntialiasing(output);
            require(untouched==output,"Completed NGX output was unnecessarily filtered at zero sharpening");
            require(EngineBindingResetProbe::Snapshot(context)==before,"Temporal presentation bypass changed engine state");
            require(backend.readbackTarget(untouched)==originalPixels,"Temporal presentation bypass changed output pixels");
            backend.configureSharpening(25);
            const EngineBindingResetProbe::Snapshot sharpenState(context);
            const auto sharpened=backend.resolveAntialiasing(output);
            require(sharpened!=output&&sharpened->pixelWidth()==1280&&sharpened->pixelHeight()==720,
                "Temporal sharpening did not produce a separate full output-sized single-sample target");
            require(EngineBindingResetProbe::Snapshot(context)==sharpenState,"Temporal sharpening changed engine state");
            backend.waitIdle();
            verifyPattern(backend.readbackTarget(sharpened),1280,720,sourceWidth,sourceHeight);
            require(backend.readbackTarget(output)==originalPixels,"Temporal sharpening modified its source");
            backend.configureSharpening(0);
        };
        verifyPresentation(composed);
        composed=backend.prepareTemporalUiBackground(color,depth);backend.waitIdle();
        verifyPattern(backend.readbackTarget(composed),composed->pixelWidth(),composed->pixelHeight(),sourceWidth,sourceHeight);
        require(EngineBindingResetProbe::Snapshot(context)==alphaState,"Nonuniform UI background copy changed original state");
        verifyPresentation(composed);
        require(NativeRecordingProbe::ngx(backend).evaluations()==17,"Standalone alpha composition falsely counted as NGX world evaluation");
        rejects([&]{NativeRecordingProbe::ngx(backend).create(mode,NativeRecordingProbe::ngx(backend).extent());});
        backend.waitIdle();backend.releaseRecordingPayload(payload);backend.releaseRecordingContext(recording);NativeRecordingProbe::ngx(backend).shutdownAfterGpuIdle();
        rejects([&]{NativeRecordingProbe::ngx(backend).optimalExtent(mode,1280,720);});
        rejects([&]{backend.resolveAntialiasing(composed);});
        std::fprintf(stderr,"[TEMPORAL GPU FIXTURE] real NGX mode=%u motion=nonzero jitter=unjittered output=1280x720 state=restored completion=proven\n",unsigned(mode));
        return 0;
    } catch(const std::exception& e){std::fprintf(stderr,"Temporal GPU fixture failed: %s\n",e.what());return 1;}
}

