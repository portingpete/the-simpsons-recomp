#include "renderer/native_backend.h"
#include "renderer/ui_composite.h"
#include "renderer/scene_feedback.h"
#include "renderer/temporal_scene.h"
#include <algorithm>
#include <d3d11_3.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
#include <thread>
#include "VSUiCompositeFixture.h"
#include "PSUiCompositeFixture.h"
#include "PSUiCompositeFixtureDepthOnly.h"
#include "PSUiCompositeFixtureStencilBit.h"
#include "VSUiCompositeJitterFixture.h"
#include "PSUiCompositeJitterFixtureDepthOnly.h"
#include "PSUiCompositeJitterFixtureStencilBit.h"
#include "PSUiCompositeJitterLateWorld.h"
using namespace Simpsons::Graphics;
uint64_t uiCompositeChecks{};
void require(bool value,const char* reason){++uiCompositeChecks;if(!value)throw Error(reason);}
template<class F>void rejects(F action){try{action();}catch(const Error&){return;}throw Error("Invalid UI composite operation was accepted");}
#include "header/test_engine_binding_reset.h"

namespace Simpsons::Graphics {
struct NativeUiCompositeProbe {
    struct Snapshot {
        EngineBindingResetProbe::Snapshot pipeline;
        std::array<uintptr_t,D3D11_SO_BUFFER_SLOT_COUNT> stream{};
        uintptr_t predicate{};BOOL predicateValue{};
        explicit Snapshot(ID3D11DeviceContext* context):pipeline(context) {
            std::array<ID3D11Buffer*,D3D11_SO_BUFFER_SLOT_COUNT> buffers{};context->SOGetTargets(UINT(buffers.size()),buffers.data());
            for(size_t at=0;at<buffers.size();++at){stream[at]=reinterpret_cast<uintptr_t>(buffers[at]);if(buffers[at])buffers[at]->Release();}
            ComPtr<ID3D11Predicate> value;context->GetPredication(&value,&predicateValue);predicate=reinterpret_cast<uintptr_t>(value.Get());
        }
        bool operator==(const Snapshot& other) const {
            return pipeline==other.pipeline&&stream==other.stream&&predicate==other.predicate&&predicateValue==other.predicateValue;
        }
    };
    static void check(HRESULT result,const char* reason){require(SUCCEEDED(result),reason);}
    static uint32_t word(const std::vector<uint8_t>& data,size_t at){uint32_t result{};std::memcpy(&result,data.data()+at,4);return result;}
    static ComPtr<ID3D11Buffer> constant(ID3D11Device* device,const std::array<uint32_t,4>& value,UINT flags=D3D11_BIND_CONSTANT_BUFFER) {
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(value);desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=flags;
        const D3D11_SUBRESOURCE_DATA data{value.data(),0,0};ComPtr<ID3D11Buffer> result;
        check(device->CreateBuffer(&desc,&data,&result),"UI fixture constant/buffer creation failed");return result;
    }
    static void logicalColor(NativeBackend& backend,const std::shared_ptr<RenderTarget>& owner,uint32_t width,uint32_t height) {
        owner->width=width;owner->height=height;backend.tagRenderExtent(owner->texture.Get(),width,height);
    }
    static bool formats(NativeBackend& backend) {
        for(auto format:{DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,DXGI_FORMAT_X32_TYPELESS_G8X24_UINT}) {
            UINT support{};if(FAILED(backend.device->CheckFormatSupport(format,&support))||!(support&D3D11_FORMAT_SUPPORT_SHADER_LOAD))return false;
        }
        UINT support{};return SUCCEEDED(backend.device->CheckFormatSupport(DXGI_FORMAT_D32_FLOAT_S8X24_UINT,&support))&&(support&D3D11_FORMAT_SUPPORT_DEPTH_STENCIL);
    }
    static void extentContracts(bool software) {
        NativeBackend temporal(software,false);
        temporal.configureRendering(3440,1440,1,Antialiasing::DLAA,125);
        require(temporal.outputExtent()==std::array<uint32_t,2>{4300,1800},"UI output extent lost configured scale");
        temporal.configureSceneExtent(860,360);
        require(temporal.outputExtent()==std::array<uint32_t,2>{4300,1800}&&temporal.sceneExtent()==std::array<uint32_t,2>{860,360},"SDK render extent collapsed full output extent");
        require(temporal.renderExtent(1280,720,TargetScale::Scene)==std::array<uint32_t,2>{860,360}&&
                temporal.renderExtent(1280,720,TargetScale::Output)==std::array<uint32_t,2>{4300,1800},"Scene/output target roles do not retain independent extents");
        rejects([&]{temporal.configureSceneExtent(0,360);});
        rejects([&]{temporal.renderExtent(1280,720,static_cast<TargetScale>(99));});
        temporal.createTarget(1,1,TargetFormat::RGB10A2);
        rejects([&]{temporal.configureSceneExtent(640,360);});
        NativeBackend spatial(software,false);rejects([&]{spatial.configureSceneExtent(640,360);});
    }
    static void sourcePattern(NativeBackend& backend,const std::shared_ptr<RenderTarget>& color,const std::shared_ptr<DepthTarget>& depth) {
        // Always populate the independent source with ordinary depth + bit
        // writes, so this fixture does not require the optional fast feature.
        ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel,stencil;
        check(backend.device->CreateVertexShader(kVSUiCompositeFixture,sizeof(kVSUiCompositeFixture),nullptr,&vertex),"UI fixture VS creation failed");
        check(backend.device->CreatePixelShader(kPSUiCompositeFixtureDepthOnly,sizeof(kPSUiCompositeFixtureDepthOnly),nullptr,&pixel),"UI fixture depth PS creation failed");
        check(backend.device->CreatePixelShader(kPSUiCompositeFixtureStencilBit,sizeof(kPSUiCompositeFixtureStencilBit),nullptr,&stencil),"UI fixture stencil PS creation failed");
        backend.context->ClearState();backend.bindTargets({color,nullptr,nullptr,nullptr},depth);
        backend.clearTarget(color,{0.25f,0.5f,0.75f,1});backend.clearDepthTarget(depth,1,0);
        auto raster=backend.sceneRasterState(0,0);backend.context->RSSetState(raster.Get());
        const D3D11_VIEWPORT viewport{0,0,float(depth->storageWidth()),float(depth->storageHeight()),0,1};backend.context->RSSetViewports(1,&viewport);
        D3D11_DEPTH_STENCIL_DESC d{};d.DepthEnable=TRUE;d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;d.DepthFunc=D3D11_COMPARISON_ALWAYS;
        ComPtr<ID3D11DepthStencilState> state;check(backend.device->CreateDepthStencilState(&d,&state),"UI fixture depth state creation failed");
        backend.context->OMSetDepthStencilState(state.Get(),0);backend.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        backend.context->VSSetShader(vertex.Get(),nullptr,0);backend.context->PSSetShader(pixel.Get(),nullptr,0);backend.context->Draw(3,0);
        d.DepthEnable=FALSE;d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;d.StencilEnable=TRUE;d.StencilReadMask=255;
        d.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_REPLACE,D3D11_COMPARISON_ALWAYS};d.BackFace=d.FrontFace;
        backend.context->OMSetRenderTargets(0,nullptr,depth->view.Get());backend.context->PSSetShader(stencil.Get(),nullptr,0);
        for(uint32_t bit=0;bit<8;++bit) {
            d.StencilWriteMask=UINT8(1u<<bit);check(backend.device->CreateDepthStencilState(&d,&state),"UI fixture stencil-bit state creation failed");
            auto mask=constant(backend.device.Get(),{1u<<bit,0,0,0});
            backend.context->OMSetDepthStencilState(state.Get(),1u<<bit);backend.context->PSSetConstantBuffers(1,1,mask.GetAddressOf());backend.context->Draw(3,0);
        }
        backend.bindTargets({color,nullptr,nullptr,nullptr},depth);
        // Retain unrelated high-slot bindings in all stages. Isolated transfer
        // must preserve them as well as shaders, OM, IA, raster, view/scissor.
        auto sentinel=constant(backend.device.Get(),{0x11223344,0x55667788,0x99AABBCC,0xDDEEFF00});
        backend.context->PSSetConstantBuffers(13,1,sentinel.GetAddressOf());backend.context->VSSetConstantBuffers(13,1,sentinel.GetAddressOf());
        backend.context->GSSetConstantBuffers(13,1,sentinel.GetAddressOf());backend.context->HSSetConstantBuffers(13,1,sentinel.GetAddressOf());
        backend.context->DSSetConstantBuffers(13,1,sentinel.GetAddressOf());backend.context->CSSetConstantBuffers(13,1,sentinel.GetAddressOf());
        const D3D11_RECT scissor{1,1,LONG(depth->storageWidth()),LONG(depth->storageHeight())};backend.context->RSSetScissorRects(1,&scissor);
    }
    static void gold(const std::vector<uint8_t>& data,uint32_t sourceWidth,uint32_t sourceHeight,uint32_t outputWidth,uint32_t outputHeight) {
        require(data.size()==size_t(outputWidth)*outputHeight*8,"UI depth readback size differs from actual output extent");
        for(uint32_t y=0;y<outputHeight;++y)for(uint32_t x=0;x<outputWidth;++x) {
            // Independent integer center mapping; no shader/compiler helper.
            const uint32_t sx=uint32_t((uint64_t(2*x+1)*sourceWidth)/(2*outputWidth));
            const uint32_t sy=uint32_t((uint64_t(2*y+1)*sourceHeight)/(2*outputHeight));
            require(sx<sourceWidth&&sy<sourceHeight,"UI fixture independent mapping escaped source extent");
            const float depth=float(((sx+3*sy)&15)+1)/32;const uint32_t stencil=(sx*17+sy*29+0x55)&255;
            const size_t at=(size_t(y)*outputWidth+x)*8;float actual{};std::memcpy(&actual,data.data()+at,4);
            require(actual==depth&&(word(data,at+4)&255)==stencil,"UI transfer depth/stencil pair differs from independent nearest gold");
        }
    }
    static bool sameDepth(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b) {
        if(a.size()!=b.size())return false;
        for(size_t at=0;at<a.size();at+=8)if(word(a,at)!=word(b,at)||(word(a,at+4)&255)!=(word(b,at+4)&255))return false;
        return true; // Upper24 stencil DWORD bits are unused in D32/S8/X24.
    }
    template<class F>static void unchanged(NativeBackend& backend,F action) {
        const Snapshot before(backend.context.Get());rejects(action);
        require(Snapshot(backend.context.Get())==before&&!backend.uiComposite,"Rejected UI promotion changed context or published a route");
    }
    static void deviceReplacement(bool software) {
        NativeBackend backend(software,false),foreign(software,false);
        auto color=backend.createTarget(7,5,TargetFormat::RGB10A2);auto depth=backend.createDepthTarget(7,5);
        auto output=backend.createTarget(11,7,TargetFormat::RGB10A2);logicalColor(backend,output,7,5);sourcePattern(backend,color,depth);
        auto first=backend.promoteUiComposite(color,depth,output);backend.waitCopy(first->completion);backend.clearUiComposite();
        const auto originalPipeline=backend.uiCompositePipeline;
        auto foreignColor=foreign.createTarget(7,5,TargetFormat::RGB10A2);auto foreignDepth=foreign.createDepthTarget(7,5);
        auto foreignOutput=foreign.createTarget(11,7,TargetFormat::RGB10A2);logicalColor(foreign,foreignOutput,7,5);sourcePattern(foreign,foreignColor,foreignDepth);
        struct Restore {
            NativeBackend& backend;ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
            std::shared_ptr<DeviceAvailability> availability;D3D_FEATURE_LEVEL level;
            void apply(){backend.device=device;backend.context=context;backend.availability=availability;backend.featureLevel=level;}
            ~Restore(){apply();}
        } restore{backend,backend.device,backend.context,backend.availability,backend.featureLevel};
        require(backend.device.Get()!=foreign.device.Get(),"UI device-change fixture did not create distinct actual devices");
        backend.device=foreign.device;backend.context=foreign.context;backend.availability=foreign.availability;backend.featureLevel=foreign.featureLevel;
        auto replacement=backend.promoteUiComposite(foreignColor,foreignDepth,foreignOutput);const auto replacementPipeline=backend.uiCompositePipeline;
        require(replacementPipeline!=originalPipeline,"UI pipeline reused the old actual-device cache");
        gold(backend.readbackDepthTarget(backend.routeDepth(foreignDepth)),7,5,11,7);
        backend.waitCopy(replacement->completion);backend.clearUiComposite();restore.apply();
        backend.bindTargets({color,nullptr,nullptr,nullptr},depth);
        auto restored=backend.promoteUiComposite(color,depth,output);
        require(backend.uiCompositePipeline!=replacementPipeline&&backend.uiCompositePipeline!=originalPipeline,"UI pipeline reused a stale cache after actual device restoration");
        gold(backend.readbackDepthTarget(backend.routeDepth(depth)),7,5,11,7);
        backend.waitCopy(restored->completion);backend.clearUiComposite();backend.waitIdle();foreign.waitIdle();
        std::printf("Verified UI pipeline cache replaces on actual device/context change and restoration (%s)\n",software?"WARP":"hardware");
    }
    static void caseContracts(bool software,uint32_t sourceWidth,uint32_t sourceHeight,uint32_t outputWidth,uint32_t outputHeight) {
        NativeBackend backend(software,false),foreign(software,false);
        auto color=backend.createTarget(sourceWidth,sourceHeight,TargetFormat::RGB10A2);
        auto depth=backend.createDepthTarget(sourceWidth,sourceHeight);
        auto output=backend.createTarget(outputWidth,outputHeight,TargetFormat::RGB10A2);logicalColor(backend,output,sourceWidth,sourceHeight);
        auto privateColor=backend.createTarget(3,3,TargetFormat::RGB10A2);auto privateDepth=backend.createDepthTarget(3,3);
        auto foreignOutput=foreign.createTarget(outputWidth,outputHeight,TargetFormat::RGB10A2);logicalColor(foreign,foreignOutput,sourceWidth,sourceHeight);
        sourcePattern(backend,color,depth);backend.clearTarget(output,{0.75f,0.25f,0.5f,1});
        const auto sourceBytes=backend.readbackDepthTarget(depth);gold(sourceBytes,sourceWidth,sourceHeight,sourceWidth,sourceHeight);
        const auto colorBytes=backend.readbackTarget(color),outputBytes=backend.readbackTarget(output);
        const Snapshot before(backend.context.Get());
        unchanged(backend,[&]{backend.promoteUiComposite(color,depth,color);});
        unchanged(backend,[&]{backend.promoteUiComposite(color,depth,foreignOutput);});
        output->width=sourceWidth+1;unchanged(backend,[&]{backend.promoteUiComposite(color,depth,output);});logicalColor(backend,output,sourceWidth,sourceHeight);
        unchanged(backend,[&]{backend.promoteUiComposite(color,privateDepth,output);});
        backend.context->PSSetShaderResources(9,1,output->sampledView.GetAddressOf());
        unchanged(backend,[&]{backend.promoteUiComposite(color,depth,output);});
        ID3D11ShaderResourceView* nullView{};backend.context->PSSetShaderResources(9,1,&nullView);
        D3D11_QUERY_DESC predicateDesc{D3D11_QUERY_OCCLUSION_PREDICATE,0};ComPtr<ID3D11Predicate> predicate;
        check(backend.device->CreatePredicate(&predicateDesc,&predicate),"UI fixture predicate creation failed");backend.context->SetPredication(predicate.Get(),TRUE);
        unchanged(backend,[&]{backend.promoteUiComposite(color,depth,output);});backend.context->SetPredication(nullptr,FALSE);
        require(Snapshot(backend.context.Get())==before,"UI rejection fixture did not restore its own state");
        bool denied=false;std::thread wrongOwner([&]{try{backend.promoteUiComposite(color,depth,output);}catch(const Error&){denied=true;}});wrongOwner.join();
        require(denied,"UI promotion bypassed owner-thread guard");
        auto bundle=backend.promoteUiComposite(color,depth,output);auto uiDepth=backend.routeDepth(depth);
        require(bundle&&bundle->completion&&uiDepth&&uiDepth.get()!=depth.get()&&uiDepth->texture.Get()!=depth->texture.Get(),"UI promotion reused the original world depth owner");
        require(uiDepth->storageWidth()==outputWidth&&uiDepth->storageHeight()==outputHeight&&uiDepth->pixelWidth()==sourceWidth&&uiDepth->pixelHeight()==sourceHeight,
                "UI output depth lost actual extent or logical camera bounds");
        backend.requireSelectedTargets({output,nullptr,nullptr,nullptr},uiDepth);
        std::shared_ptr<RenderTarget> colorAlias=color;std::shared_ptr<DepthTarget> depthAlias=depth;
        require(backend.routeColor(colorAlias)==output&&backend.routeDepth(depthAlias)==uiDepth&&backend.routeColor(privateColor)==privateColor&&backend.routeDepth(privateDepth)==privateDepth&&
                backend.routeColor(output)==output&&!backend.routeColor({})&&!backend.routeDepth({}),"UI routing changed private owners or missed exact shared original aliases");
        rejects([&]{backend.promoteUiComposite(color,depth,output);});
        output->width=sourceWidth+1;const Snapshot routed(backend.context.Get());
        rejects([&]{backend.routeColor(color);});require(Snapshot(backend.context.Get())==routed,"Stale UI owner metadata rejection changed context");logicalColor(backend,output,sourceWidth,sourceHeight);
        backend.bindTargets({privateColor,nullptr,nullptr,nullptr},privateDepth);const Snapshot privateSelection(backend.context.Get());
        rejects([&]{backend.clearUiComposite();});require(Snapshot(backend.context.Get())==privateSelection&&backend.uiComposite,"UI close silently repaired unrelated selected owners");
        backend.bindTargets({output,nullptr,nullptr,nullptr},uiDepth);
        gold(backend.readbackDepthTarget(uiDepth),sourceWidth,sourceHeight,outputWidth,outputHeight);
        require(sameDepth(backend.readbackDepthTarget(depth),sourceBytes)&&backend.readbackTarget(color)==colorBytes&&backend.readbackTarget(output)==outputBytes,"UI depth transfer mutated world depth or color owners");
        backend.waitCopy(bundle->completion);require(backend.copyComplete(bundle->completion),"UI promotion has no real completed GPU event");
        backend.clearUiComposite();require(Snapshot(backend.context.Get())==before,"UI isolated transfer/owned close did not restore complete native context");
        require(backend.routeColor(color)==color&&backend.routeDepth(depth)==depth,"UI routes survived owned close");
        backend.clearDepthTarget(uiDepth,1,255); // Poison every bit before forced fallback.
        const auto* pipelineIdentity=backend.uiCompositePipeline.get();
        auto forced=backend.promoteUiCompositeImpl(color,depth,output,true);
        require(backend.routeDepth(depth)==uiDepth&&backend.uiCompositePipeline.get()==pipelineIdentity,"UI immutable pipeline/output-depth cache did not reuse the qualified owner");
        gold(backend.readbackDepthTarget(uiDepth),sourceWidth,sourceHeight,outputWidth,outputHeight);
        backend.waitCopy(forced->completion);require(backend.copyComplete(forced->completion),"Forced UI fallback has no real completed event");
        backend.clearUiComposite();require(Snapshot(backend.context.Get())==before,"Forced UI stencil fallback changed complete native context");
        auto resizedOutput=backend.createTarget(outputWidth+2,outputHeight+2,TargetFormat::RGB10A2);logicalColor(backend,resizedOutput,sourceWidth,sourceHeight);
        auto resizedBundle=backend.promoteUiComposite(color,depth,resizedOutput);auto resizedDepth=backend.routeDepth(depth);
        require(backend.uiCompositePipeline.get()==pipelineIdentity&&resizedDepth!=uiDepth&&resizedDepth->texture.Get()!=uiDepth->texture.Get(),"UI extent change failed to replace depth while retaining immutable device pipeline");
        require(resizedDepth->storageWidth()==outputWidth+2&&resizedDepth->storageHeight()==outputHeight+2,"UI replaced cached depth has incorrect actual extent");
        gold(backend.readbackDepthTarget(resizedDepth),sourceWidth,sourceHeight,outputWidth+2,outputHeight+2);
        backend.waitCopy(resizedBundle->completion);backend.clearUiComposite();resizedBundle.reset();
        require(Snapshot(backend.context.Get())==before,"UI extent replacement changed complete context");
        auto token=forced->completion;
        std::weak_ptr<RenderTarget> weakColor=color,weakOutput=output;std::weak_ptr<DepthTarget> weakDepth=depth;
        bundle.reset();forced.reset();colorAlias.reset();depthAlias.reset();color.reset();output.reset();depth.reset();
        require(!weakColor.expired()&&!weakOutput.expired()&&!weakDepth.expired(),"Actual UI completion token dropped original/output owners");
        token.reset();require(weakColor.expired()&&weakOutput.expired()&&weakDepth.expired(),"UI completion resource lease contains an ownership cycle");
        backend.waitIdle();
        D3D11_FEATURE_DATA_D3D11_OPTIONS2 options{};
        const bool shaderRef=SUCCEEDED(backend.device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2,&options,sizeof(options)))&&options.PSSpecifiedStencilRefSupported;
        std::printf("Verified UI composite %ux%u->%ux%u: %s normal + forced eight-bit fallback, exact paired nearest depth/stencil, full context, owner routes, rejection, cache and GPU leases (%s)\n",
            sourceWidth,sourceHeight,outputWidth,outputHeight,(sourceWidth==outputWidth&&sourceHeight==outputHeight)?"exact resource copy":shaderRef?"shader-stencil-ref":"eight-bit fallback",software?"WARP":"hardware");
    }
    static uint32_t floatWord(float value) {uint32_t result{};std::memcpy(&result,&value,4);return result;}
    static std::array<uint32_t,8> transferBytes(NativeBackend& backend,const ComPtr<ID3D11Buffer>& buffer) {
        require(buffer.Get()!=nullptr,"UI jitter fixture has no actual cached transfer constant");
        D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);
        require(desc.ByteWidth==32&&desc.Usage==D3D11_USAGE_DYNAMIC&&desc.BindFlags==D3D11_BIND_CONSTANT_BUFFER&&
                desc.CPUAccessFlags==D3D11_CPU_ACCESS_WRITE&&!desc.MiscFlags&&!desc.StructureByteStride,
                "UI transfer constant is not the qualified cached 32-byte dynamic buffer");
        ComPtr<ID3D11Device> owner;buffer->GetDevice(&owner);require(owner.Get()==backend.device.Get(),"UI transfer constant belongs to a stale actual device");
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> staging;check(backend.device->CreateBuffer(&desc,nullptr,&staging),"UI constant readback allocation failed");
        backend.context->CopyResource(staging.Get(),buffer.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(backend.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"UI constant readback map failed");
        std::array<uint32_t,8> result{};std::memcpy(result.data(),mapped.pData,sizeof(result));backend.context->Unmap(staging.Get(),0);return result;
    }
    static void transferGold(NativeBackend& backend,const ComPtr<ID3D11Buffer>& buffer,uint32_t sw,uint32_t sh,uint32_t ow,uint32_t oh,std::array<float,2> jitter) {
        require(transferBytes(backend,buffer)==std::array<uint32_t,8>{sw,sh,ow,oh,floatWord(jitter[0]),floatWord(jitter[1]),0,0},
                "UI cached transfer constant has stale extent/jitter data or undefined padding");
    }
    static void jitterSource(NativeBackend& backend,const std::shared_ptr<RenderTarget>& color,const std::shared_ptr<DepthTarget>& depth,std::array<float,2> jitter) {
        ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel,stencil;
        check(backend.device->CreateVertexShader(kVSUiCompositeJitterFixture,sizeof(kVSUiCompositeJitterFixture),nullptr,&vertex),"UI jitter fixture VS creation failed");
        check(backend.device->CreatePixelShader(kPSUiCompositeJitterFixtureDepthOnly,sizeof(kPSUiCompositeJitterFixtureDepthOnly),nullptr,&pixel),"UI jitter depth PS creation failed");
        check(backend.device->CreatePixelShader(kPSUiCompositeJitterFixtureStencilBit,sizeof(kPSUiCompositeJitterFixtureStencilBit),nullptr,&stencil),"UI jitter stencil PS creation failed");
        backend.context->ClearState();backend.bindTargets({color,nullptr,nullptr,nullptr},depth);
        backend.clearTarget(color,{0.25f,0.5f,0.75f,1});backend.clearDepthTarget(depth,1,0);
        auto raster=backend.sceneRasterState(0,0);backend.context->RSSetState(raster.Get());
        const D3D11_VIEWPORT viewport{0,0,float(depth->storageWidth()),float(depth->storageHeight()),0,1};backend.context->RSSetViewports(1,&viewport);
        auto source=constant(backend.device.Get(),{depth->storageWidth(),depth->storageHeight(),floatWord(jitter[0]),floatWord(jitter[1])});
        backend.context->PSSetConstantBuffers(0,1,source.GetAddressOf());
        D3D11_DEPTH_STENCIL_DESC d{};d.DepthEnable=TRUE;d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;d.DepthFunc=D3D11_COMPARISON_ALWAYS;
        ComPtr<ID3D11DepthStencilState> state;check(backend.device->CreateDepthStencilState(&d,&state),"UI jitter source depth state failed");
        backend.context->OMSetDepthStencilState(state.Get(),0);backend.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        backend.context->VSSetShader(vertex.Get(),nullptr,0);backend.context->PSSetShader(pixel.Get(),nullptr,0);backend.context->Draw(3,0);
        d.DepthEnable=FALSE;d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;d.StencilEnable=TRUE;d.StencilReadMask=255;
        d.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_REPLACE,D3D11_COMPARISON_ALWAYS};d.BackFace=d.FrontFace;
        backend.context->OMSetRenderTargets(0,nullptr,depth->view.Get());backend.context->PSSetShader(stencil.Get(),nullptr,0);
        for(uint32_t bit=0;bit<8;++bit) {
            d.StencilWriteMask=UINT8(1u<<bit);check(backend.device->CreateDepthStencilState(&d,&state),"UI jitter source stencil state failed");
            auto mask=constant(backend.device.Get(),{1u<<bit,0,0,0});backend.context->OMSetDepthStencilState(state.Get(),1u<<bit);
            backend.context->PSSetConstantBuffers(1,1,mask.GetAddressOf());backend.context->Draw(3,0);
        }
        backend.bindTargets({color,nullptr,nullptr,nullptr},depth);
        auto sentinel=constant(backend.device.Get(),{0x11223344,0x55667788,0x99AABBCC,0xDDEEFF00});
        backend.context->PSSetConstantBuffers(13,1,sentinel.GetAddressOf());backend.context->VSSetConstantBuffers(13,1,sentinel.GetAddressOf());
        backend.context->GSSetConstantBuffers(13,1,sentinel.GetAddressOf());backend.context->HSSetConstantBuffers(13,1,sentinel.GetAddressOf());
        backend.context->DSSetConstantBuffers(13,1,sentinel.GetAddressOf());backend.context->CSSetConstantBuffers(13,1,sentinel.GetAddressOf());
        auto stream=constant(backend.device.Get(),{11,22,33,44},D3D11_BIND_STREAM_OUTPUT);const UINT offset=0;
        backend.context->SOSetTargets(1,stream.GetAddressOf(),&offset);
        const D3D11_RECT scissor{1,1,LONG(depth->storageWidth()),LONG(depth->storageHeight())};backend.context->RSSetScissorRects(1,&scissor);
    }
    struct SourceGold {float depth;uint32_t stencil;};
    static int64_t eighth(float value) {
        const int64_t result=std::llround(double(value)*8);
        require(double(result)==double(value)*8,"UI jitter gold requires its independent exact eighth-pixel sequence");return result;
    }
    static SourceGold sourceGold(uint32_t x,uint32_t y,uint32_t width,uint32_t height,std::array<float,2> sourceJitter) {
        // Integer silhouette classification, independent of HLSL arithmetic.
        const int64_t px=8*int64_t(x)+4-eighth(sourceJitter[0]),py=8*int64_t(y)+4-eighth(sourceJitter[1]);
        if(px<4||py<4||px>=8*int64_t(width)-4||py>=8*int64_t(height)-4||px+py>=4*int64_t(width+height))return {1,0};
        return {float(((x+3*y)&15)+1)/32,(x*17+y*29+0x55)&255};
    }
    static uint32_t jitterCoordinate(uint32_t pixel,uint32_t source,uint32_t output,float jitter) {
        // Signed rational floor: the fixture neither calls production helpers
        // nor reproduces floating shader floor/division behavior for its gold.
        const int64_t numerator=(2*int64_t(pixel)+1)*source*4+eighth(jitter)*output,denominator=8*int64_t(output);
        int64_t result=numerator/denominator;if(numerator<0&&numerator%denominator)--result;
        if(result<0)return 0;if(result>=source)return source-1;return uint32_t(result);
    }
    static SourceGold outputGold(uint32_t x,uint32_t y,uint32_t sw,uint32_t sh,uint32_t ow,uint32_t oh,std::array<float,2> sourceJitter,std::array<float,2> transferJitter) {
        return sourceGold(jitterCoordinate(x,sw,ow,transferJitter[0]),jitterCoordinate(y,sh,oh,transferJitter[1]),sw,sh,sourceJitter);
    }
    static uint64_t jitterGold(const std::vector<uint8_t>& data,uint32_t sw,uint32_t sh,uint32_t ow,uint32_t oh,std::array<float,2> sourceJitter,std::array<float,2> transferJitter) {
        require(data.size()==size_t(ow)*oh*8,"UI jitter readback does not match actual output extent");
        uint32_t ones=0,zeros=0;uint64_t differs=0;
        for(uint32_t y=0;y<oh;++y)for(uint32_t x=0;x<ow;++x) {
            const auto expected=outputGold(x,y,sw,sh,ow,oh,sourceJitter,transferJitter);
            const auto zero=outputGold(x,y,sw,sh,ow,oh,sourceJitter,{0,0});
            const size_t at=(size_t(y)*ow+x)*8;float actual{};std::memcpy(&actual,data.data()+at,4);
            require(actual==expected.depth&&(word(data,at+4)&255)==expected.stencil,"UI jitter depth/stencil differs from independent signed-rational nearest gold");
            ones|=expected.stencil;zeros|=(~expected.stencil)&255;
            differs+=expected.depth!=zero.depth||expected.stencil!=zero.stencil;
        }
        require(ones==255&&zeros==255,"UI jitter silhouette did not exercise both values of every stencil bit");return differs;
    }
    static void lateWorld(NativeBackend& backend,const std::shared_ptr<RenderTarget>& output,const std::shared_ptr<DepthTarget>& depth,
                          uint32_t sw,uint32_t sh,std::array<float,2> sourceJitter,std::array<float,2> transferJitter) {
        const Snapshot before(backend.context.Get());ComPtr<ID3D11Device1> device;ComPtr<ID3D11DeviceContext1> context;
        check(backend.device.As(&device),"UI late-world state device interface failed");check(backend.context.As(&context),"UI late-world state context interface failed");
        ComPtr<ID3DDeviceContextState> isolated;const auto level=backend.device->GetFeatureLevel();
        const UINT flags=(backend.device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
        check(device->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&isolated),"UI late-world context-state creation failed");
        {
            struct Restore {ID3D11DeviceContext1* context;ComPtr<ID3DDeviceContextState> previous;~Restore(){context->ClearState();context->SwapDeviceContextState(previous.Get(),nullptr);}} restore{context.Get(),{}};
            context->SwapDeviceContextState(isolated.Get(),&restore.previous);context->ClearState();
            ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;
            check(backend.device->CreateVertexShader(kVSUiCompositeJitterFixture,sizeof(kVSUiCompositeJitterFixture),nullptr,&vertex),"UI late-world VS failed");
            check(backend.device->CreatePixelShader(kPSUiCompositeJitterLateWorld,sizeof(kPSUiCompositeJitterLateWorld),nullptr,&pixel),"UI late-world PS failed");
            context->OMSetRenderTargets(1,output->view.GetAddressOf(),depth->view.Get());
            D3D11_DEPTH_STENCIL_DESC d{};d.DepthEnable=TRUE;d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;d.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
            ComPtr<ID3D11DepthStencilState> state;check(backend.device->CreateDepthStencilState(&d,&state),"UI late-world compare state failed");context->OMSetDepthStencilState(state.Get(),0);
            D3D11_BLEND_DESC b{};b.RenderTarget[0].RenderTargetWriteMask=15;ComPtr<ID3D11BlendState> blend;
            check(backend.device->CreateBlendState(&b,&blend),"UI late-world color state failed");context->OMSetBlendState(blend.Get(),nullptr,~0u);
            auto raster=backend.sceneRasterState(0,0);context->RSSetState(raster.Get());
            const D3D11_VIEWPORT viewport{0,0,float(output->pixelWidth()),float(output->pixelHeight()),0,1};context->RSSetViewports(1,&viewport);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);context->Draw(3,0);
        }
        require(Snapshot(backend.context.Get())==before,"UI late-world fixture failed to restore its own complete pipeline/SO/predication state");
        const auto pixels=backend.readbackTarget(output);const uint32_t ow=output->pixelWidth(),oh=output->pixelHeight();
        require(pixels.size()==size_t(ow)*oh*4,"UI late-world packed readback has incorrect physical size");
        uint32_t passed=0,failed=0,equal=0;
        for(uint32_t y=0;y<oh;++y)for(uint32_t x=0;x<ow;++x) {
            const float incoming=y%7==6?0.75f:0.25f;
            const float stored=outputGold(x,y,sw,sh,ow,oh,sourceJitter,transferJitter).depth;
            const bool accepts=incoming<=stored;const uint32_t expected=0xC0000000u|(accepts?(1023u<<10):1023u);
            require(word(pixels,(size_t(y)*ow+x)*4)==expected,"Actual late-world LESS_EQUAL depth draw disagrees with independent mapped silhouette gold");
            passed+=accepts;failed+=!accepts;equal+=incoming==stored;
        }
        require(passed&&failed&&equal,"UI late-world fixture lacks passing, occluded or equality-depth coverage");
    }
    static void jitterContracts(bool software,Antialiasing mode,uint32_t sw,uint32_t sh,uint32_t ow,uint32_t oh) {
        NativeBackend backend(software,false);backend.configureRendering(1280,720,1,mode);
        auto color=backend.createTarget(sw,sh,TargetFormat::RGB10A2);auto depth=backend.createDepthTarget(sw,sh);
        auto output=backend.createTarget(ow,oh,TargetFormat::RGB10A2);logicalColor(backend,output,sw,sh);
        const std::array<std::array<float,2>,8> sequence{{{0,0},{0.5f,-0.5f},{-0.5f,0.5f},{0.25f,-0.25f},{-0.25f,0.25f},{0.5f,0.5f},{-0.5f,-0.5f},{0.125f,-0.375f}}};
        ComPtr<ID3D11Buffer> cached;std::shared_ptr<DepthTarget> cachedDepth;uint64_t changedPixels=0;
        for(const auto jitter:sequence) {
            jitterSource(backend,color,depth,jitter);
            const auto sourceBytes=backend.readbackDepthTarget(depth),colorBytes=backend.readbackTarget(color);
            jitterGold(sourceBytes,sw,sh,sw,sh,jitter,{0,0});const Snapshot before(backend.context.Get());
            for(bool force:{false,true}) {
                backend.clearTarget(output,{1,0,0,1});const auto outputBytes=backend.readbackTarget(output);
                if(force)backend.clearDepthTarget(cachedDepth,1,255);
                auto bundle=force?backend.promoteUiCompositeImpl(color,depth,output,true,jitter):backend.promoteUiComposite(color,depth,output,jitter);
                require(bundle->resources->sourceJitter==jitter,"UI completion lease did not capture the submitted source jitter");
                const auto actual=backend.uiCompositeTransferConstants();const auto selected=backend.routeDepth(depth);
                if(!cached){cached=actual;cachedDepth=selected;}
                require(actual.Get()==cached.Get()&&selected==cachedDepth,"Changing jitter on the same owners replaced the cached transfer buffer or output depth");
                transferGold(backend,actual,sw,sh,ow,oh,jitter);
                const auto transferred=backend.readbackDepthTarget(selected);changedPixels+=jitterGold(transferred,sw,sh,ow,oh,jitter,jitter);
                require(sameDepth(backend.readbackDepthTarget(depth),sourceBytes)&&backend.readbackTarget(color)==colorBytes&&backend.readbackTarget(output)==outputBytes,
                        "UI jitter transfer changed source depth, source color or reconstructed color");
                lateWorld(backend,output,selected,sw,sh,jitter,jitter);
                require(sameDepth(backend.readbackDepthTarget(selected),transferred)&&sameDepth(backend.readbackDepthTarget(depth),sourceBytes)&&backend.readbackTarget(color)==colorBytes,
                        "UI late-world compare wrote transferred/source depth or world color");
                backend.waitCopy(bundle->completion);require(backend.copyComplete(bundle->completion),"UI jitter transfer did not publish a real completed event");
                backend.clearUiComposite();require(Snapshot(backend.context.Get())==before,"UI jitter transfer/close changed full native pipeline/SO/predication state");
            }
        }
        require(changedPixels>0,"UI nonzero jitter tests cannot detect an unchanged mapping or an equal-size CopyResource shortcut");
        const auto sourceJitter=sequence.back();const std::array<float,2> firstJitter{0.5f,-0.5f},secondJitter{-0.5f,0.5f};
        const auto sourceBytes=backend.readbackDepthTarget(depth);const Snapshot queuedBefore(backend.context.Get());
        auto savedFirst=backend.createDepthTarget(ow,oh);
        auto first=backend.promoteUiComposite(color,depth,output,firstJitter);backend.context->CopyResource(savedFirst->texture.Get(),backend.routeDepth(depth)->texture.Get());
        require(backend.uiCompositeTransferConstants().Get()==cached.Get(),"Queued UI transfer changed constant identity");backend.clearUiComposite();
        auto second=backend.promoteUiCompositeImpl(color,depth,output,true,secondJitter);
        require(first->resources->sourceJitter==firstJitter&&second->resources->sourceJitter==secondJitter,"Queued UI jitter captures alias mutable pipeline state");
        require(backend.uiCompositeTransferConstants().Get()==cached.Get()&&backend.routeDepth(depth)==cachedDepth,"Queued changed jitter replaced cached native owners");
        // No readback or CPU wait occurs between the two promotions. The first
        // GPU snapshot must retain its earlier WRITE_DISCARD constant contents.
        jitterGold(backend.readbackDepthTarget(savedFirst),sw,sh,ow,oh,sourceJitter,firstJitter);
        jitterGold(backend.readbackDepthTarget(cachedDepth),sw,sh,ow,oh,sourceJitter,secondJitter);
        transferGold(backend,cached,sw,sh,ow,oh,secondJitter);backend.waitCopy(first->completion);backend.waitCopy(second->completion);backend.clearUiComposite();
        require(Snapshot(backend.context.Get())==queuedBefore&&sameDepth(backend.readbackDepthTarget(depth),sourceBytes),"Queued varying-jitter transfers changed source/context");
        const auto oldBytes=transferBytes(backend,cached);const auto rejectionColor=backend.readbackTarget(color),rejectionOutput=backend.readbackTarget(output);const float tooFar=std::nextafter(0.5f,1.0f);
        for(const auto invalid:std::array<std::array<float,2>,8>{{{tooFar,0},{-tooFar,0},{0,tooFar},{0,-tooFar},
                {std::numeric_limits<float>::infinity(),0},{0,-std::numeric_limits<float>::infinity()},
                {std::numeric_limits<float>::quiet_NaN(),0},{0,std::numeric_limits<float>::quiet_NaN()}}}) {
            unchanged(backend,[&]{backend.promoteUiComposite(color,depth,output,invalid);});
            unchanged(backend,[&]{backend.promoteUiCompositeImpl(color,depth,output,true,invalid);});
            require(backend.uiCompositeTransferConstants().Get()==cached.Get()&&transferBytes(backend,cached)==oldBytes,
                    "Rejected UI jitter mutated cached constant identity or data");
        }
        require(sameDepth(backend.readbackDepthTarget(depth),sourceBytes)&&backend.readbackTarget(color)==rejectionColor&&backend.readbackTarget(output)==rejectionOutput,
                "Rejected UI jitter changed source depth, source color or reconstructed color");
        bool denied=false;std::thread wrongOwner([&]{try{backend.uiCompositeTransferConstants();}catch(const Error&){denied=true;}});wrongOwner.join();
        require(denied,"UI fixture cached-constant accessor bypassed the owner-thread guard");
        auto resizedOutput=backend.createTarget(ow+2,oh+2,TargetFormat::RGB10A2);logicalColor(backend,resizedOutput,sw,sh);
        auto resized=backend.promoteUiComposite(color,depth,resizedOutput,firstJitter);const auto resizedDepth=backend.routeDepth(depth);
        require(backend.uiCompositeTransferConstants().Get()==cached.Get()&&resizedDepth!=cachedDepth,"UI extent change replaced dynamic constants or retained incorrectly sized depth");
        transferGold(backend,cached,sw,sh,ow+2,oh+2,firstJitter);jitterGold(backend.readbackDepthTarget(resizedDepth),sw,sh,ow+2,oh+2,sourceJitter,firstJitter);
        backend.waitCopy(resized->completion);backend.clearUiComposite();require(Snapshot(backend.context.Get())==queuedBefore,"UI jitter extent replacement changed full native state");backend.waitIdle();
        std::printf("Verified UI jitter %s %ux%u->%ux%u: 8 phase pairs, normal/forced all-bit fallback, actual late-world depth draw, signed clamps, ordered constant renames and cached32B identity (%s)\n",
            mode==Antialiasing::DLAA?"DLAA":"DLSSQuality",sw,sh,ow,oh,software?"WARP":"hardware");
    }
    static void jitterDeviceReplacement(bool software) {
        NativeBackend backend(software,false),foreign(software,false);const std::array<float,2> jitter{0.5f,-0.5f};
        auto color=backend.createTarget(12,8,TargetFormat::RGB10A2);auto depth=backend.createDepthTarget(12,8);
        auto output=backend.createTarget(18,12,TargetFormat::RGB10A2);logicalColor(backend,output,12,8);jitterSource(backend,color,depth,jitter);
        auto first=backend.promoteUiComposite(color,depth,output,jitter);const auto originalConstant=backend.uiCompositeTransferConstants();
        backend.waitCopy(first->completion);backend.clearUiComposite();
        auto foreignColor=foreign.createTarget(12,8,TargetFormat::RGB10A2);auto foreignDepth=foreign.createDepthTarget(12,8);
        auto foreignOutput=foreign.createTarget(18,12,TargetFormat::RGB10A2);logicalColor(foreign,foreignOutput,12,8);jitterSource(foreign,foreignColor,foreignDepth,jitter);
        struct Restore {NativeBackend& backend;ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;std::shared_ptr<DeviceAvailability> availability;D3D_FEATURE_LEVEL level;
            void apply(){backend.device=device;backend.context=context;backend.availability=availability;backend.featureLevel=level;}~Restore(){apply();}
        } restore{backend,backend.device,backend.context,backend.availability,backend.featureLevel};
        require(backend.device.Get()!=foreign.device.Get(),"UI jitter cache fixture did not obtain a distinct actual device");
        backend.device=foreign.device;backend.context=foreign.context;backend.availability=foreign.availability;backend.featureLevel=foreign.featureLevel;
        rejects([&]{backend.uiCompositeTransferConstants();});
        auto replacement=backend.promoteUiComposite(foreignColor,foreignDepth,foreignOutput,jitter);const auto replacementConstant=backend.uiCompositeTransferConstants();
        require(replacementConstant.Get()!=originalConstant.Get(),"UI dynamic constants reused a foreign actual-device/context cache");
        transferGold(backend,replacementConstant,12,8,18,12,jitter);jitterGold(backend.readbackDepthTarget(backend.routeDepth(foreignDepth)),12,8,18,12,jitter,jitter);
        backend.waitCopy(replacement->completion);backend.clearUiComposite();restore.apply();rejects([&]{backend.uiCompositeTransferConstants();});
        backend.bindTargets({color,nullptr,nullptr,nullptr},depth);auto restored=backend.promoteUiComposite(color,depth,output,jitter);const auto restoredConstant=backend.uiCompositeTransferConstants();
        require(restoredConstant.Get()!=originalConstant.Get()&&restoredConstant.Get()!=replacementConstant.Get(),"UI dynamic constant cache revived a stale actual-device buffer");
        transferGold(backend,restoredConstant,12,8,18,12,jitter);jitterGold(backend.readbackDepthTarget(backend.routeDepth(depth)),12,8,18,12,jitter,jitter);
        backend.waitCopy(restored->completion);backend.clearUiComposite();backend.waitIdle();foreign.waitIdle();
    }
    static int runJitter(bool software) {
        jitterContracts(software,Antialiasing::DLAA,12,8,12,8);jitterContracts(software,Antialiasing::DLSSQuality,12,8,18,12);jitterDeviceReplacement(software);
        std::printf("Verified UI jitter contract: checks=%llu actual_cases=2 device_cache=1 phase_pairs=8 completion=PASS driver=%s\n",static_cast<unsigned long long>(uiCompositeChecks),software?"WARP":"hardware");return 0;
    }
    struct CopyRoleReceipt {
        uint64_t generation{};
        std::weak_ptr<RenderTarget> color;
        std::weak_ptr<DepthTarget> depth;
    };
    static void requireCopyRoleReceipt(NativeBackend& backend,const std::shared_ptr<RenderTarget>& originalColor,
                                       const std::shared_ptr<DepthTarget>& originalDepth,const CopyRoleReceipt& receipt) {
        // Fixture-only submission publication. This independently checks both
        // epoch and actual owner; it is not an EngineDriver implementation.
        if(!receipt.generation||receipt.generation!=backend.uiCompositeCopyGeneration()||
           receipt.color.lock()!=backend.routeColor(originalColor)||receipt.depth.lock()!=backend.routeDepth(originalDepth))
            throw Error("Fixture copied-role receipt has stale phase generation or actual backing");
    }
    static CopyRoleReceipt copyRolePixels(NativeBackend& backend,const std::shared_ptr<RenderTarget>& sceneColor,
                                          const std::shared_ptr<DepthTarget>& sceneDepth,const std::shared_ptr<RenderTarget>& originalColor,
                                          const std::shared_ptr<DepthTarget>& originalDepth,uint32_t sw,uint32_t sh,bool outputPhase) {
        const auto sourceColor=backend.routeColor(sceneColor);const auto sourceDepth=backend.routeDepth(sceneDepth);
        const auto color=backend.routeColor(originalColor);const auto depth=backend.routeDepth(originalDepth);
        const Snapshot before(backend.context.Get());const auto generation=backend.uiCompositeCopyGeneration();
        auto colorEvent=backend.copyFront(sourceColor,color);auto depthEvent=backend.copyDepth(sourceDepth,depth);
        CopyRoleReceipt receipt{generation,color,depth};requireCopyRoleReceipt(backend,originalColor,originalDepth,receipt);
        require(backend.uiCompositeCopyGeneration()==generation&&Snapshot(backend.context.Get())==before,"Shared role resource copies changed phase epoch or complete state");
        const auto sourceBytes=backend.readbackTarget(sourceColor);const auto copied=backend.readbackTarget(color);
        require(copied==sourceBytes,"Shared role color copy differs from actual selected source pixels");
        require(sameDepth(backend.readbackDepthTarget(depth),backend.readbackDepthTarget(sourceDepth)),"Shared role copy changed a selected depth bit or stencil value");
        gold(backend.readbackDepthTarget(depth),sw,sh,depth->storageWidth(),depth->storageHeight());
        if(outputPhase)for(size_t at=0;at<copied.size();at+=4)require(word(copied,at)==0xC00003FFu,"Output-phase copied color differs from independent packed red gold");
        backend.waitCopy(colorEvent);backend.waitCopy(depthEvent);
        require(backend.copyComplete(colorEvent)&&backend.copyComplete(depthEvent),"Shared copy role has no actual completed GPU events");return receipt;
    }
    static void copyRoleContracts(bool software,Antialiasing mode) {
        constexpr uint32_t lw=64,lh=36;
        NativeBackend backend(software,false),foreign(software,false);backend.configureRendering(1280,720,1,mode);
        if(mode==Antialiasing::DLSSQuality)backend.configureSceneExtent(640,360);
        auto color=backend.createTarget(lw,lh,TargetFormat::RGB10A2,TargetScale::Scene);auto depth=backend.createDepthTarget(lw,lh,TargetScale::Scene);
        auto output=backend.createTarget(lw,lh,TargetFormat::RGB10A2,TargetScale::Output);
        auto colorCopy=backend.createTarget(lw,lh,TargetFormat::RGB10A2,TargetScale::Scene);auto depthCopy=backend.createDepthTarget(lw,lh,TargetScale::Scene);
        auto unrelatedColor=backend.createTarget(lw,lh,TargetFormat::RGB10A2,TargetScale::Scene);auto unrelatedDepth=backend.createDepthTarget(lw,lh,TargetScale::Scene);
        auto shadow=backend.createDepthTarget(8,8);const uint32_t sw=color->pixelWidth(),sh=color->pixelHeight();
        sourcePattern(backend,color,depth);backend.clearTarget(output,{1,0,0,1});backend.clearTarget(colorCopy,{0,1,1,1});backend.clearDepthTarget(depthCopy,1,0xA5);
        const auto worldColor=backend.readbackTarget(color),worldDepth=backend.readbackDepthTarget(depth);
        require(!backend.uiCompositeCopyGeneration(),"Unregistered temporal shared copies published an epoch");
        const Snapshot originalState(backend.context.Get());backend.registerUiCompositeCopyRoles(colorCopy,depthCopy);
        const auto registered=backend.uiCompositeCopyGeneration();require(registered&&Snapshot(backend.context.Get())==originalState,"Copy-role registration lacks an epoch or changed native bindings");
        require(backend.routeColor(colorCopy)==colorCopy&&backend.routeDepth(depthCopy)==depthCopy,"Inactive copy registration routed Scene copies to Output");
        backend.registerUiCompositeCopyRoles(colorCopy,depthCopy);require(backend.uiCompositeCopyGeneration()==registered,"Exact repeat registration incremented the shared-copy epoch");
        auto rejectedRegistration=[&](auto action) {
            const Snapshot before(backend.context.Get());const auto epoch=backend.uiCompositeCopyGeneration();const auto pending=backend.pendingCopies.size();
            const auto oldColor=backend.routeColor(colorCopy);const auto oldDepth=backend.routeDepth(depthCopy);
            const auto oldColorBytes=backend.readbackTarget(oldColor),oldDepthBytes=backend.readbackDepthTarget(oldDepth);
            rejects(action);require(backend.uiCompositeCopyGeneration()==epoch&&backend.pendingCopies.size()==pending&&Snapshot(backend.context.Get())==before&&
                backend.routeColor(colorCopy)==oldColor&&backend.routeDepth(depthCopy)==oldDepth&&backend.readbackTarget(oldColor)==oldColorBytes&&sameDepth(backend.readbackDepthTarget(oldDepth),oldDepthBytes),
                "Rejected shared-copy registration changed epoch, work, route, state or original pixels");
        };
        rejectedRegistration([&]{backend.registerUiCompositeCopyRoles({},depthCopy);});rejectedRegistration([&]{backend.registerUiCompositeCopyRoles(colorCopy,{});});
        auto foreignColor=foreign.createTarget(lw,lh,TargetFormat::RGB10A2);auto foreignDepth=foreign.createDepthTarget(lw,lh);
        rejectedRegistration([&]{backend.registerUiCompositeCopyRoles(foreignColor,depthCopy);});rejectedRegistration([&]{backend.registerUiCompositeCopyRoles(colorCopy,foreignDepth);});
        auto badColor=backend.createTarget(lw+2,lh,TargetFormat::RGB10A2,TargetScale::Scene);auto badDepth=backend.createDepthTarget(lw,lh+2,TargetScale::Scene);
        rejectedRegistration([&]{backend.registerUiCompositeCopyRoles(badColor,depthCopy);});rejectedRegistration([&]{backend.registerUiCompositeCopyRoles(colorCopy,badDepth);});
        if(mode==Antialiasing::DLSSQuality) {
            auto wrongScaleColor=backend.createTarget(lw,lh,TargetFormat::RGB10A2,TargetScale::Output);auto wrongScaleDepth=backend.createDepthTarget(lw,lh,TargetScale::Output);
            rejectedRegistration([&]{backend.registerUiCompositeCopyRoles(wrongScaleColor,depthCopy);});rejectedRegistration([&]{backend.registerUiCompositeCopyRoles(colorCopy,wrongScaleDepth);});
        }
        auto aliasDepth=backend.createDepthTarget(lw,lh,TargetScale::Scene);aliasDepth->texture=colorCopy->texture;
        rejectedRegistration([&]{backend.registerUiCompositeCopyRoles(colorCopy,aliasDepth);});
        {
            const auto savedMode=backend.antialiasingMode;
            struct RestoreMode {NativeBackend& backend;Antialiasing mode;~RestoreMode(){backend.antialiasingMode=mode;}} restore{backend,savedMode};
            backend.antialiasingMode=Antialiasing::SMAA;
            require(!backend.uiCompositeCopyGeneration(),"Non-temporal mode published a cached temporal copy-role epoch");
        }
        bool denied=false;std::thread wrongOwner([&]{try{backend.registerUiCompositeCopyRoles(colorCopy,depthCopy);}catch(const Error&){denied=true;}});wrongOwner.join();
        require(denied&&backend.uiCompositeCopyGeneration()==registered&&Snapshot(backend.context.Get())==originalState,"Copy-role registration bypassed owner thread or mutated rejected state");
        const auto sceneReceipt=copyRolePixels(backend,color,depth,colorCopy,depthCopy,sw,sh,false);
        const auto preservedSceneColor=backend.readbackTarget(colorCopy),preservedSceneDepth=backend.readbackDepthTarget(depthCopy);
        auto first=backend.promoteUiComposite(color,depth,output);const auto promoted=backend.uiCompositeCopyGeneration();
        auto outputColorCopy=backend.routeColor(colorCopy);auto outputDepthCopy=backend.routeDepth(depthCopy);
        require(promoted==registered+1&&outputColorCopy!=colorCopy&&outputDepthCopy!=depthCopy&&
                outputColorCopy->pixelWidth()==output->pixelWidth()&&outputColorCopy->pixelHeight()==output->pixelHeight()&&
                outputDepthCopy->storageWidth()==output->pixelWidth()&&outputDepthCopy->storageHeight()==output->pixelHeight()&&
                outputColorCopy->width==lw&&outputColorCopy->height==lh&&outputDepthCopy->width==lw&&outputDepthCopy->height==lh,
                "Promoted shared copies lost Output extent/logical bounds or phase epoch");
        require(first->resources->sceneColorCopy==colorCopy&&first->resources->sceneDepthCopy==depthCopy&&
                first->resources->outputColorCopy==outputColorCopy&&first->resources->outputDepthCopy==outputDepthCopy&&first->resources->copyGeneration==promoted&&
                first->resources->sceneColor==color&&first->resources->sceneDepth==depth&&first->resources->outputColor==output&&first->resources->outputDepth==backend.routeDepth(depth),
                "UI completion did not capture all eight actual strong owners and copy generation");
        const Snapshot staleState(backend.context.Get());outputColorCopy->width=lw+1;
        rejects([&]{backend.uiCompositeCopyGeneration();});rejects([&]{backend.routeColor(colorCopy);});
        require(Snapshot(backend.context.Get())==staleState,"Stale Output copy metadata rejection changed full native state");outputColorCopy->width=lw;
        require(backend.uiCompositeCopyGeneration()==promoted,"Stale metadata rejection incremented the shared-copy epoch");
        require(backend.routeColor(unrelatedColor)==unrelatedColor&&backend.routeDepth(unrelatedDepth)==unrelatedDepth&&backend.routeDepth(shadow)==shadow,
                "Registered CF routing changed unrelated SceneCopies/private/shadow owners");
        rejects([&]{requireCopyRoleReceipt(backend,colorCopy,depthCopy,sceneReceipt);});
        CopyRoleReceipt wrongBacking{promoted,colorCopy,depthCopy};rejects([&]{requireCopyRoleReceipt(backend,colorCopy,depthCopy,wrongBacking);});
        const auto outputReceipt=copyRolePixels(backend,color,depth,colorCopy,depthCopy,sw,sh,true);
        rejectedRegistration([&]{backend.registerUiCompositeCopyRoles(colorCopy,depthCopy);});
        rejectedRegistration([&]{backend.registerUiCompositeCopyRoles(unrelatedColor,unrelatedDepth);});
        require(backend.readbackTarget(colorCopy)==preservedSceneColor&&sameDepth(backend.readbackDepthTarget(depthCopy),preservedSceneDepth)&&
                backend.readbackTarget(color)==worldColor&&sameDepth(backend.readbackDepthTarget(depth),worldDepth),"Output shared copies changed retained Scene copies or original main attachments");
        backend.waitCopy(first->completion);auto lease=first->completion;backend.clearUiComposite();const auto cleared=backend.uiCompositeCopyGeneration();
        require(cleared==promoted+1&&backend.routeColor(colorCopy)==colorCopy&&backend.routeDepth(depthCopy)==depthCopy&&Snapshot(backend.context.Get())==originalState,
                "Owned clear failed to restore Scene copy routes/full state or advance epoch");
        rejects([&]{requireCopyRoleReceipt(backend,colorCopy,depthCopy,outputReceipt);});rejects([&]{requireCopyRoleReceipt(backend,colorCopy,depthCopy,sceneReceipt);});
        backend.clearUiComposite();backend.registerUiCompositeCopyRoles(colorCopy,depthCopy);require(backend.uiCompositeCopyGeneration()==cleared,"Inactive no-op clear or repeat registration changed epoch");
        auto second=backend.promoteUiCompositeImpl(color,depth,output,true);require(backend.uiCompositeCopyGeneration()==cleared+1&&
            backend.routeColor(colorCopy)==outputColorCopy&&backend.routeDepth(depthCopy)==outputDepthCopy,"Next frame replaced persistent Output copy cache or retained old generation");
        rejects([&]{requireCopyRoleReceipt(backend,colorCopy,depthCopy,outputReceipt);});copyRolePixels(backend,color,depth,colorCopy,depthCopy,sw,sh,true);
        backend.waitCopy(second->completion);backend.clearUiComposite();const auto beforeReplacement=backend.uiCompositeCopyGeneration();
        auto newColorCopy=backend.createTarget(lw,lh,TargetFormat::RGB10A2,TargetScale::Scene);auto newDepthCopy=backend.createDepthTarget(lw,lh,TargetScale::Scene);
        backend.registerUiCompositeCopyRoles(newColorCopy,newDepthCopy);require(backend.uiCompositeCopyGeneration()==beforeReplacement+1&&
            backend.routeColor(colorCopy)==colorCopy&&backend.routeDepth(depthCopy)==depthCopy,"New registration failed transactional generation/publication or retained old active routes");
        std::weak_ptr<RenderTarget> oldSceneColor=colorCopy,oldOutputColor=outputColorCopy;std::weak_ptr<DepthTarget> oldSceneDepth=depthCopy,oldOutputDepth=outputDepthCopy;
        first.reset();second.reset();colorCopy.reset();depthCopy.reset();outputColorCopy.reset();outputDepthCopy.reset();wrongBacking={};
        backend.waitIdle();require(!oldSceneColor.expired()&&!oldSceneDepth.expired()&&!oldOutputColor.expired()&&!oldOutputDepth.expired(),"Replacement registry dropped old four copy-role owners still leased by completion");
        lease.reset();require(oldSceneColor.expired()&&oldSceneDepth.expired()&&oldOutputColor.expired()&&oldOutputDepth.expired(),"Completed copy-role resource lease retained an ownership cycle");
        auto replacement=backend.promoteUiComposite(color,depth,output);require(backend.routeColor(newColorCopy)!=newColorCopy&&backend.routeDepth(newDepthCopy)!=newDepthCopy,
            "Replacement registered copies did not receive dedicated Output owners");copyRolePixels(backend,color,depth,newColorCopy,newDepthCopy,sw,sh,true);
        backend.waitCopy(replacement->completion);backend.clearUiComposite();
        // Actual aliases with main scene inputs are knowable only at promotion,
        // not from the registration argument's guest role label.
        backend.registerUiCompositeCopyRoles(color,depth);backend.waitIdle();const auto aliasEpoch=backend.uiCompositeCopyGeneration();const Snapshot aliasState(backend.context.Get());
        const auto aliasPending=backend.pendingCopies.size();unchanged(backend,[&]{backend.promoteUiComposite(color,depth,output);});
        unchanged(backend,[&]{backend.promoteUiCompositeImpl(color,depth,output,true);});
        require(backend.uiCompositeCopyGeneration()==aliasEpoch&&backend.pendingCopies.size()==aliasPending&&Snapshot(backend.context.Get())==aliasState&&
                backend.readbackTarget(color)==worldColor&&sameDepth(backend.readbackDepthTarget(depth),worldDepth),"Rejected main/copy-role alias changed epoch, work, bindings or scene pixels");
        backend.waitIdle();std::printf("Verified shared copy roles %s: Scene/Output actual copies, exact pixels, epoch/backing receipts, persistent cache, all-eight leases and transactional rejects (%s)\n",
            mode==Antialiasing::DLAA?"DLAA":"DLSSQuality",software?"WARP":"hardware");
    }
    static void copyRoleDeviceReplacement(bool software) {
        constexpr uint32_t lw=16,lh=12;NativeBackend backend(software,false),foreign(software,false);
        for(auto* configured:{&backend,&foreign}){configured->configureRendering(1280,720,1,Antialiasing::DLSSQuality);configured->configureSceneExtent(640,360);}
        auto color=backend.createTarget(lw,lh,TargetFormat::RGB10A2,TargetScale::Scene);auto depth=backend.createDepthTarget(lw,lh,TargetScale::Scene);
        auto output=backend.createTarget(lw,lh,TargetFormat::RGB10A2,TargetScale::Output);
        auto colorCopy=backend.createTarget(lw,lh,TargetFormat::RGB10A2,TargetScale::Scene);auto depthCopy=backend.createDepthTarget(lw,lh,TargetScale::Scene);
        sourcePattern(backend,color,depth);backend.clearTarget(output,{1,0,0,1});backend.registerUiCompositeCopyRoles(colorCopy,depthCopy);
        auto first=backend.promoteUiComposite(color,depth,output);const auto firstColor=backend.routeColor(colorCopy);const auto firstDepth=backend.routeDepth(depthCopy);
        copyRolePixels(backend,color,depth,colorCopy,depthCopy,8,6,true);backend.waitCopy(first->completion);backend.clearUiComposite();backend.waitIdle();
        auto foreignColor=foreign.createTarget(lw,lh,TargetFormat::RGB10A2,TargetScale::Scene);auto foreignDepth=foreign.createDepthTarget(lw,lh,TargetScale::Scene);
        auto foreignOutput=foreign.createTarget(lw,lh,TargetFormat::RGB10A2,TargetScale::Output);
        auto foreignColorCopy=foreign.createTarget(lw,lh,TargetFormat::RGB10A2,TargetScale::Scene);auto foreignDepthCopy=foreign.createDepthTarget(lw,lh,TargetScale::Scene);
        sourcePattern(foreign,foreignColor,foreignDepth);foreign.clearTarget(foreignOutput,{1,0,0,1});
        struct Restore {NativeBackend& backend;ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;std::shared_ptr<DeviceAvailability> availability;D3D_FEATURE_LEVEL level;
            void apply(){backend.device=device;backend.context=context;backend.availability=availability;backend.featureLevel=level;}~Restore(){apply();}
        } restore{backend,backend.device,backend.context,backend.availability,backend.featureLevel};
        require(backend.device.Get()!=foreign.device.Get(),"Copy-role device fixture has no distinct actual device");
        backend.device=foreign.device;backend.context=foreign.context;backend.availability=foreign.availability;backend.featureLevel=foreign.featureLevel;
        rejects([&]{backend.uiCompositeCopyGeneration();});backend.registerUiCompositeCopyRoles(foreignColorCopy,foreignDepthCopy);
        auto replacement=backend.promoteUiComposite(foreignColor,foreignDepth,foreignOutput);
        const auto replacementColor=backend.routeColor(foreignColorCopy);const auto replacementDepth=backend.routeDepth(foreignDepthCopy);
        require(replacementColor!=firstColor&&replacementDepth!=firstDepth,"Registered Output copy cache reused a stale actual-device owner");
        ComPtr<ID3D11Device> actualColor,actualDepth;replacementColor->texture->GetDevice(&actualColor);replacementDepth->texture->GetDevice(&actualDepth);
        require(actualColor.Get()==foreign.device.Get()&&actualDepth.Get()==foreign.device.Get(),"Replacement copy cache lacks actual foreign device ownership");
        copyRolePixels(backend,foreignColor,foreignDepth,foreignColorCopy,foreignDepthCopy,8,6,true);
        backend.waitCopy(replacement->completion);backend.clearUiComposite();backend.waitIdle();restore.apply();rejects([&]{backend.uiCompositeCopyGeneration();});
        backend.registerUiCompositeCopyRoles(colorCopy,depthCopy);backend.bindTargets({color,nullptr,nullptr,nullptr},depth);
        auto restored=backend.promoteUiComposite(color,depth,output);const auto restoredColor=backend.routeColor(colorCopy);const auto restoredDepth=backend.routeDepth(depthCopy);
        require(restoredColor!=firstColor&&restoredColor!=replacementColor&&restoredDepth!=firstDepth&&restoredDepth!=replacementDepth,
                "Restored device reused stale registered Output copy resources");
        restoredColor->texture->GetDevice(&actualColor);restoredDepth->texture->GetDevice(&actualDepth);
        require(actualColor.Get()==restore.device.Get()&&actualDepth.Get()==restore.device.Get(),"Restored Output copy cache has the wrong actual device");
        copyRolePixels(backend,color,depth,colorCopy,depthCopy,8,6,true);backend.waitCopy(restored->completion);backend.clearUiComposite();backend.waitIdle();foreign.waitIdle();
    }
    static int runCopyRoles(bool software) {
        NativeBackend spatial(software,false);auto color=spatial.createTarget(8,8,TargetFormat::RGB10A2);auto depth=spatial.createDepthTarget(8,8);
        require(!spatial.uiCompositeCopyGeneration(),"Spatial rendering published a temporal shared-copy epoch");rejects([&]{spatial.registerUiCompositeCopyRoles(color,depth);});
        require(!spatial.uiCompositeCopyGeneration(),"Rejected spatial registration published a shared-copy epoch");
        copyRoleContracts(software,Antialiasing::DLAA);copyRoleContracts(software,Antialiasing::DLSSQuality);copyRoleDeviceReplacement(software);
        std::printf("Verified UI shared-copy contract: checks=%llu actual_cases=2 device_cache=1 completion=PASS driver=%s\n",static_cast<unsigned long long>(uiCompositeChecks),software?"WARP":"hardware");return 0;
    }
    static int run(bool software,bool spatialOnly=false) {
        NativeBackend capability(software,false);
        if(!formats(capability)){std::printf("UI composite required typed depth/stencil format profile unsupported (%s); fixture skipped explicitly\n",software?"WARP":"hardware");return 77;}
        if(!spatialOnly)extentContracts(software);
        deviceReplacement(software);
        caseContracts(software,7,5,11,7);caseContracts(software,11,7,7,5);caseContracts(software,7,5,7,5);
        std::printf("Verified UI composite contract: checks=%llu actual_cases=3 completion=PASS driver=%s\n",static_cast<unsigned long long>(uiCompositeChecks),software?"WARP":"hardware");
        return 0;
    }
    // Feedback cases are offscreen renderer qualification. They do not create
    // an NGX scene or claim queued display/original-game phase acceptance.
    struct FeedbackImage {
        uint32_t width{},height{};
        std::vector<uint8_t> color,depth;
    };
    static uint32_t feedbackCode(uint32_t x,uint32_t y,uint32_t salt) {
        return ((x*67+y*29+salt*31+17)&1023)|(((x*43+y*71+salt*13+91)&1023)<<10)|
               (((x*19+y*53+salt*37+207)&1023)<<20)|(((x+2*y+salt)&3)<<30);
    }
    static void feedbackRawPattern(NativeBackend& backend,const std::shared_ptr<RenderTarget>& color,
                                   const std::shared_ptr<DepthTarget>& depth,uint32_t salt) {
        sourcePattern(backend,color,depth); // Actual depth draw and all eight stencil-bit draws.
        ComPtr<ID3D11DeviceContext1> context;check(backend.context.As(&context),"Feedback pattern requires actual ClearView interface");
        for(uint32_t y=0;y<color->physicalHeight;++y)for(uint32_t x=0;x<color->physicalWidth;++x) {
            const uint32_t code=feedbackCode(x,y,salt);
            const float values[4]={float(code&1023)/1023,float((code>>10)&1023)/1023,
                float((code>>20)&1023)/1023,float(code>>30)/3};
            const D3D11_RECT rectangle{LONG(x),LONG(y),LONG(x+1),LONG(y+1)};
            context->ClearView(color->view.Get(),values,&rectangle,1);
        }
        auto stream=constant(backend.device.Get(),{71,83,97,109},D3D11_BIND_STREAM_OUTPUT);const UINT offset=0;
        backend.context->SOSetTargets(1,stream.GetAddressOf(),&offset);
    }
    static FeedbackImage feedbackRead(NativeBackend& backend,const std::shared_ptr<RenderTarget>& color,
                                     const std::shared_ptr<DepthTarget>& depth) {
        require(color->physicalWidth==depth->physicalWidth&&color->physicalHeight==depth->physicalHeight,"Feedback read has unpaired actual extent");
        return {color->physicalWidth,color->physicalHeight,backend.readbackTarget(color),backend.readbackDepthTarget(depth)};
    }
    static void feedbackRawGold(const FeedbackImage& image,uint32_t salt) {
        require(image.color.size()==size_t(image.width)*image.height*4&&image.depth.size()==size_t(image.width)*image.height*8,"Feedback raw image extent is invalid");
        uint32_t ones=0,zeros=0,alphas=0;
        for(uint32_t y=0;y<image.height;++y)for(uint32_t x=0;x<image.width;++x) {
            const size_t at=size_t(y)*image.width+x;const uint32_t stencil=(x*17+y*29+0x55)&255;
            require(word(image.color,at*4)==feedbackCode(x,y,salt)&&word(image.depth,at*8)==floatWord(float(((x+3*y)&15)+1)/32)&&
                    (word(image.depth,at*8+4)&255)==stencil,"GPU raw feedback population differs from independent packed/depth/stencil pattern");
            ones|=stencil;zeros|=(~stencil)&255;alphas|=1u<<(feedbackCode(x,y,salt)>>30);
        }
        require(ones==255&&zeros==255&&alphas==15,"Feedback raw pattern omitted a stencil bit value or alpha code");
    }
    static FeedbackImage feedbackOverwriteRaw(NativeBackend& backend,const std::shared_ptr<RenderTarget>& color,
                                              const std::shared_ptr<DepthTarget>& depth,uint32_t salt) {
        feedbackRawPattern(backend,color,depth,salt);backend.clearDepthTarget(depth,1,0xA5);const auto image=feedbackRead(backend,color,depth);
        for(uint32_t y=0;y<image.height;++y)for(uint32_t x=0;x<image.width;++x) {
            const size_t at=size_t(y)*image.width+x;
            require(word(image.color,at*4)==feedbackCode(x,y,salt)&&word(image.depth,at*8)==floatWord(1)&&
                (word(image.depth,at*8+4)&255)==0xA5,"Actual live Output overwrite did not poison color/alpha/depth/stencil independently");
        }
        return image;
    }
    static std::array<uint32_t,2> feedbackCandidates(uint32_t pixel,uint32_t source,uint32_t destination,float jitter,bool bounded) {
        if(!bounded) {
            // Signed rational transport, independent of production float operations.
            const int64_t numerator=(8*int64_t(pixel)+4-eighth(jitter))*source,denominator=8*int64_t(destination);
            int64_t index=numerator/denominator;if(numerator<0&&numerator%denominator)--index;
            const auto clamped=uint32_t(std::clamp(index,int64_t(0),int64_t(source-1)));return {clamped,clamped};
        }
        // Only near-tie inputs use a conservative three-operation binary32
        // rounding interval. Alpha/D/S must match ONE common candidate pair.
        const long double coordinate=(static_cast<long double>(pixel)+0.5L-static_cast<long double>(jitter))*source/destination;
        const long double error=8*std::numeric_limits<float>::epsilon()*std::max(1.0L,std::abs(coordinate));
        const int64_t low=int64_t(std::floor(coordinate-error)),high=int64_t(std::floor(coordinate+error));
        require(high-low<=1,"Feedback bounded nearest oracle permits too many candidate texels");
        return {uint32_t(std::clamp(low,int64_t(0),int64_t(source-1))),uint32_t(std::clamp(high,int64_t(0),int64_t(source-1)))};
    }
    struct FeedbackRgb {std::array<long double,3> value{},bound{};};
    static FeedbackRgb feedbackBilinear(const FeedbackImage& source,uint32_t x,uint32_t y,uint32_t width,uint32_t height,std::array<float,2> jitter) {
        // Decode input RGB10 codes and interpolate centers in real arithmetic.
        // Hardware filter weights have at least eight fractional bits; bound
        // their quantization by the local horizontal/vertical code gradients.
        const long double px=std::clamp((static_cast<long double>(x)+0.5L-jitter[0])*source.width/width-0.5L,0.0L,static_cast<long double>(source.width-1));
        const long double py=std::clamp((static_cast<long double>(y)+0.5L-jitter[1])*source.height/height-0.5L,0.0L,static_cast<long double>(source.height-1));
        const uint32_t x0=uint32_t(std::floor(px)),y0=uint32_t(std::floor(py)),x1=std::min(x0+1,source.width-1),y1=std::min(y0+1,source.height-1);
        const long double fx=px-x0,fy=py-y0;FeedbackRgb result;
        const std::array<uint32_t,4> codes{word(source.color,(size_t(y0)*source.width+x0)*4),word(source.color,(size_t(y0)*source.width+x1)*4),
            word(source.color,(size_t(y1)*source.width+x0)*4),word(source.color,(size_t(y1)*source.width+x1)*4)};
        for(uint32_t channel=0;channel<3;++channel) {
            const long double a=(codes[0]>>(10*channel))&1023,b=(codes[1]>>(10*channel))&1023,
                c=(codes[2]>>(10*channel))&1023,d=(codes[3]>>(10*channel))&1023;
            result.value[channel]=(a*(1-fx)+b*fx)*(1-fy)+(c*(1-fx)+d*fx)*fy;
            result.bound[channel]=1.05L+(std::max(std::abs(b-a),std::abs(d-c))+std::max(std::abs(c-a),std::abs(d-b)))/256;
        }
        return result;
    }
    static void feedbackGold(const FeedbackImage& source,const FeedbackImage& before,const FeedbackImage& actual,
                             std::array<float,2> jitter,uint32_t mask,bool bounded=false) {
        require(before.width==actual.width&&before.height==actual.height&&actual.color.size()==size_t(actual.width)*actual.height*4&&
                actual.depth.size()==size_t(actual.width)*actual.height*8,"Feedback destination readback extent is invalid");
        const bool exact=jitter==std::array<float,2>{}&&source.width==actual.width&&source.height==actual.height;
        for(uint32_t y=0;y<actual.height;++y)for(uint32_t x=0;x<actual.width;++x) {
            const size_t at=size_t(y)*actual.width+x;const uint32_t color=word(actual.color,at*4),depth=word(actual.depth,at*8),stencil=word(actual.depth,at*8+4)&255;
            if(!(mask&1))require(color==word(before.color,at*4),"Selective feedback overwrote unselected packed color");
            if(!(mask&2))require(depth==word(before.depth,at*8),"Selective feedback overwrote unselected depth bits");
            if(!(mask&4))require(stencil==(word(before.depth,at*8+4)&255),"Selective feedback overwrote unselected stencil bits");
            if(exact) {
                if(mask&1)require(color==word(source.color,at*4),"Equal zero-grid feedback changed an exact packed color byte");
                if(mask&2)require(depth==word(source.depth,at*8),"Equal zero-grid feedback changed exact depth bits");
                if(mask&4)require(stencil==(word(source.depth,at*8+4)&255),"Equal zero-grid feedback changed an exact stencil bit");
                continue;
            }
            if(mask&1) {
                const auto rgb=feedbackBilinear(source,x,y,actual.width,actual.height,jitter);
                for(uint32_t channel=0;channel<3;++channel)require(std::abs(static_cast<long double>((color>>(10*channel))&1023)-rgb.value[channel])<=rgb.bound[channel],
                    "Feedback RGB differs from independent decoded bilinear/filter-precision bounds");
            }
            const auto xs=feedbackCandidates(x,source.width,actual.width,jitter[0],bounded),ys=feedbackCandidates(y,source.height,actual.height,jitter[1],bounded);
            bool paired=false;
            for(uint32_t sy:ys)for(uint32_t sx:xs) {
                const size_t sourceAt=size_t(sy)*source.width+sx;
                paired=paired||((!(mask&1)||(color>>30)==(word(source.color,sourceAt*4)>>30))&&
                    (!(mask&2)||depth==word(source.depth,sourceAt*8))&&(!(mask&4)||stencil==(word(source.depth,sourceAt*8+4)&255)));
            }
            require(paired,"Feedback alpha/depth/stencil do not share one independently allowed nearest source texel");
        }
    }
    template<class F>static void feedbackRejected(NativeBackend& backend,const std::shared_ptr<RenderTarget>& color,
                                                const std::shared_ptr<DepthTarget>& depth,F action) {
        const auto pixels=feedbackRead(backend,color,depth);backend.waitIdle();const Snapshot state(backend.context.Get());
        const auto origins=backend.temporalSceneOrigins();const auto pending=backend.pendingCopies.size();
        rejects(action);const auto after=backend.temporalSceneOrigins();
        require(Snapshot(backend.context.Get())==state&&backend.pendingCopies.size()==pending&&after.color==origins.color&&after.depth==origins.depth&&
            after.components==origins.components,"Rejected feedback changed complete context, work or component receipts");
        const auto final=feedbackRead(backend,color,depth);require(final.color==pixels.color&&sameDepth(final.depth,pixels.depth),"Rejected feedback mutated Scene pixels");
    }
    static void feedbackDirect(bool software,uint32_t sceneWidth,uint32_t sceneHeight,uint32_t outputWidth,uint32_t outputHeight) {
        NativeBackend backend(software,false),foreign(software,false);
        auto source=backend.createTarget(outputWidth,outputHeight,TargetFormat::RGB10A2);auto sourceDepth=backend.createDepthTarget(outputWidth,outputHeight);
        logicalColor(backend,source,sceneWidth,sceneHeight);sourceDepth->width=sceneWidth;sourceDepth->height=sceneHeight;backend.tagRenderExtent(sourceDepth->texture.Get(),sceneWidth,sceneHeight);
        auto color=backend.createTarget(sceneWidth,sceneHeight,TargetFormat::RGB10A2);auto depth=backend.createDepthTarget(sceneWidth,sceneHeight);
        feedbackRawPattern(backend,source,sourceDepth,3);const auto input=feedbackRead(backend,source,sourceDepth);feedbackRawGold(input,3);
        ComPtr<ID3D11Buffer> cached;
        const std::array<std::array<float,2>,5> jitters{{{0,0},{0.5f,-0.5f},{-0.5f,0.5f},{0.125f,-0.375f},{std::nextafter(-0.5f,0.0f),std::nextafter(0.5f,0.0f)}}};
        for(size_t phase=0;phase<jitters.size();++phase)for(uint32_t mask=1;mask<=7;++mask) {
            backend.clearTarget(color,{1,0,1,0});backend.clearDepthTarget(depth,1,255);const auto before=feedbackRead(backend,color,depth);
            const Snapshot state(backend.context.Get());auto normal=backend.submitSceneFeedback(source,sourceDepth,color,depth,jitters[phase],mask);
            require(normal&&Snapshot(backend.context.Get())==state,"Direct feedback lost completion or full context/SO state");
            const auto constant=backend.sceneFeedbackTransferConstants();if(cached)require(cached.Get()==constant.Get(),"Feedback allocated a new transfer buffer for another jitter/mask");else cached=constant;
            const bool exact=jitters[phase]==std::array<float,2>{}&&sceneWidth==outputWidth&&sceneHeight==outputHeight;
            const bool shaderWork=((mask&1)&&!exact)||((mask&6)&&!(exact&&(mask&6)==6));
            // Exact copies intentionally do not Map unused shader constants.
            if(shaderWork)transferGold(backend,constant,outputWidth,outputHeight,sceneWidth,sceneHeight,jitters[phase]);
            const auto normalPixels=feedbackRead(backend,color,depth);feedbackGold(input,before,normalPixels,jitters[phase],mask,phase==4);
            backend.waitCopy(normal);require(backend.copyComplete(normal),"Feedback direct transfer has no real completed GPU event");
            backend.clearTarget(color,{1,0,1,0});backend.clearDepthTarget(depth,1,255);
            auto forced=backend.submitSceneFeedback(source,sourceDepth,color,depth,jitters[phase],mask,true);
            require(forced&&Snapshot(backend.context.Get())==state,"Forced feedback stencil fallback changed full state");
            const auto forcedPixels=feedbackRead(backend,color,depth);feedbackGold(input,before,forcedPixels,jitters[phase],mask,phase==4);
            require(forcedPixels.color==normalPixels.color&&sameDepth(forcedPixels.depth,normalPixels.depth),"Feedback normal/forced fallback produced different actual paired pixels");
            backend.waitCopy(forced);require(backend.copyComplete(forced),"Forced feedback stencil fallback has no completed event");
            const auto preserved=feedbackRead(backend,source,sourceDepth);require(preserved.color==input.color&&sameDepth(preserved.depth,input.depth),"Feedback mutated its raw color/depth/stencil source");
        }
        auto denied=[&](auto action){const auto before=feedbackRead(backend,color,depth);backend.waitIdle();const Snapshot state(backend.context.Get());const auto pending=backend.pendingCopies.size();rejects(action);
            require(Snapshot(backend.context.Get())==state&&backend.pendingCopies.size()==pending,"Invalid private feedback submitted commands or changed full state");
            const auto actual=feedbackRead(backend,color,depth);const auto original=feedbackRead(backend,source,sourceDepth);
            require(original.color==input.color&&sameDepth(original.depth,input.depth)&&actual.color==before.color&&sameDepth(actual.depth,before.depth),"Private feedback rejection corrupted source or destination pixels");};
        denied([&]{backend.submitSceneFeedback(source,sourceDepth,color,depth,{std::numeric_limits<float>::quiet_NaN(),0},7);});
        denied([&]{backend.submitSceneFeedback(source,sourceDepth,color,depth,{0,std::numeric_limits<float>::infinity()},7);});
        denied([&]{backend.submitSceneFeedback(source,sourceDepth,color,depth,{0.5001f,0},7);});
        denied([&]{backend.submitSceneFeedback(source,sourceDepth,color,depth,{},0);});denied([&]{backend.submitSceneFeedback(source,sourceDepth,color,depth,{},8);});
        denied([&]{backend.submitSceneFeedback(source,sourceDepth,source,sourceDepth,{},7);});
        auto foreignColor=foreign.createTarget(sceneWidth,sceneHeight,TargetFormat::RGB10A2);auto foreignDepth=foreign.createDepthTarget(sceneWidth,sceneHeight);
        denied([&]{backend.submitSceneFeedback(source,sourceDepth,foreignColor,foreignDepth,{},7);});
        backend.context->PSSetShaderResources(11,1,color->sampledView.GetAddressOf());
        denied([&]{backend.submitSceneFeedback(source,sourceDepth,color,depth,{},7);});ID3D11ShaderResourceView* nullView{};backend.context->PSSetShaderResources(11,1,&nullView);
        D3D11_QUERY_DESC predicateDesc{D3D11_QUERY_OCCLUSION_PREDICATE,0};ComPtr<ID3D11Predicate> predicate;
        check(backend.device->CreatePredicate(&predicateDesc,&predicate),"Feedback predicate fixture creation failed");backend.waitIdle();backend.context->SetPredication(predicate.Get(),TRUE);
        const Snapshot predicated(backend.context.Get());rejects([&]{backend.submitSceneFeedback(source,sourceDepth,color,depth,{},7);});
        require(Snapshot(backend.context.Get())==predicated,"Feedback rejection failed to preserve real predication");backend.context->SetPredication(nullptr,FALSE);
        bool deniedThread=false;std::thread wrongOwner([&]{try{backend.submitSceneFeedback(source,sourceDepth,color,depth,{},7);}catch(const Error&){deniedThread=true;}});wrongOwner.join();
        require(deniedThread,"Feedback direct transfer bypassed owner-thread guard");
        backend.context->ClearState();auto extra=std::make_shared<uint32_t>(0xA55A);std::weak_ptr<uint32_t> weakExtra=extra;
        auto lease=backend.submitSceneFeedback(source,sourceDepth,color,depth,{0.125f,-0.375f},7,true,extra);backend.waitCopy(lease);backend.waitIdle();
        std::weak_ptr<RenderTarget> weakSource=source,weakColor=color;std::weak_ptr<DepthTarget> weakSourceDepth=sourceDepth,weakDepth=depth;
        source.reset();sourceDepth.reset();color.reset();depth.reset();extra.reset();
        require(!weakSource.expired()&&!weakSourceDepth.expired()&&!weakColor.expired()&&!weakDepth.expired()&&!weakExtra.expired(),"Feedback completion dropped one of four actual owners or extra snapshot lease");
        lease.reset();require(weakSource.expired()&&weakSourceDepth.expired()&&weakColor.expired()&&weakDepth.expired()&&weakExtra.expired(),"Feedback completion/pipeline has an ownership cycle");
        foreign.waitIdle();std::printf("Verified direct Scene feedback %ux%u<-raw %ux%u: 5 jitter phases, all7 component masks, exact zero bytes, bounded coupled FP ties, bilinear RGB, normal/forced stencil, state and leases (%s)\n",
            sceneWidth,sceneHeight,outputWidth,outputHeight,software?"WARP":"hardware");
    }
    struct FeedbackFrame {
        std::shared_ptr<NativeSceneFeedbackFrame> capture;
        std::shared_ptr<NativeCopySubmission> front,commit;
        std::shared_ptr<DepthTarget> rawDepth;
        FeedbackImage raw;
    };
    static FeedbackFrame feedbackCaptureFrame(NativeBackend& backend,const std::shared_ptr<RenderTarget>& scene,
        const std::shared_ptr<DepthTarget>& depth,const std::shared_ptr<RenderTarget>& output,const std::shared_ptr<RenderTarget>& front,
        uint64_t serial,uint32_t salt,std::array<float,2> currentJitter) {
        backend.bindTargets({scene,nullptr,nullptr,nullptr},depth);backend.clearTarget(scene,{0,0,0,0});backend.clearDepthTarget(depth,0,255);
        const auto clearOrigins=backend.temporalSceneOrigins();
        for(const auto& component:clearOrigins.components)require(component.kind==TemporalRasterOriginKind::Uniform,"Actual full clear did not establish a Uniform Scene origin");
        auto promoted=backend.promoteUiComposite(scene,depth,output,currentJitter);auto rawDepth=backend.routeDepth(depth);
        feedbackRawPattern(backend,output,rawDepth,salt);const Snapshot state(backend.context.Get());
        // No readback or intervening copy is allowed between this actual raw
        // front submission, capture and commit. Capture is not display proof.
        auto rawFront=backend.copyFront(output,front);auto capture=backend.captureSceneFeedback(serial,rawFront);
        auto committed=backend.commitSceneFeedback(capture);
        require(capture&&committed&&Snapshot(backend.context.Get())==state&&backend.uiComposite,"Feedback capture/commit changed active route or complete state");
        const auto origins=backend.temporalSceneOrigins();
        for(size_t at=0;at<3;++at)require(origins.components[at].kind==TemporalRasterOriginKind::Grid&&origins.components[at].jitter==std::array<float,2>{}&&
            origins.components[at].revision==clearOrigins.components[at].revision+1,"Committed feedback did not publish exactly one Grid0 component revision");
        const auto raw=feedbackRead(backend,output,rawDepth);feedbackRawGold(raw,salt);
        require(capture->capturedColor&&capture->capturedDepth&&capture->snapshotCompletion&&capture->snapshotGeneration&&
            capture->capturedColor->texture.Get()!=output->texture.Get()&&capture->capturedDepth->texture.Get()!=rawDepth->texture.Get(),
            "Feedback capture did not retain an independent generation-stamped canonical pair/completion");
        const auto canonical=feedbackRead(backend,capture->capturedColor,capture->capturedDepth);
        require(canonical.color==raw.color&&sameDepth(canonical.depth,raw.depth),"Canonical capture changed a raw RGB10/alpha/depth/stencil code");
        require(backend.readbackTarget(front)==raw.color,"Captured raw front copy differs from the actual packed Output source");
        const auto scenePixels=feedbackRead(backend,scene,depth);feedbackGold(raw,scenePixels,scenePixels,{},7);
        backend.waitCopy(rawFront);backend.waitCopy(capture->snapshotCompletion);backend.waitCopy(committed);backend.waitCopy(promoted->completion);
        require(backend.copyComplete(rawFront)&&backend.copyComplete(capture->snapshotCompletion)&&backend.copyComplete(committed),"Captured/snapshotted/committed raw feedback has no completed GPU events");
        return {capture,rawFront,committed,rawDepth,raw};
    }
    static void feedbackFrames(bool software,Antialiasing mode,uint32_t configuredSceneWidth,uint32_t configuredSceneHeight) {
        constexpr uint32_t lw=12,lh=8;NativeBackend backend(software,false),foreign(software,false);
        backend.configureRendering(1280,720,1,mode);backend.configureSceneExtent(configuredSceneWidth,configuredSceneHeight);
        auto scene=backend.createTarget(lw,lh,TargetFormat::RGB10A2,TargetScale::Scene);auto depth=backend.createDepthTarget(lw,lh,TargetScale::Scene);
        auto output=backend.createTarget(lw,lh,TargetFormat::RGB10A2,TargetScale::Output);auto front=backend.createTarget(lw,lh,TargetFormat::RGB10A2,TargetScale::Output);
        backend.bindTargets({scene,nullptr,nullptr,nullptr},depth);backend.registerTemporalWorldTargets(scene,depth);
        const auto unknown=backend.temporalSceneOrigins();for(const auto& component:unknown.components)require(component.kind==TemporalRasterOriginKind::Unknown,"Registration invented a known Scene origin");
        feedbackRejected(backend,scene,depth,[&]{backend.alignSceneFeedback(scene,depth,{0.125f,-0.375f},7);});
        const std::array<uint32_t,8> preserve{7,6,5,4,3,2,1,0};
        const std::array<std::array<float,2>,3> current{{{0.125f,-0.375f},{-0.5f,0.5f},{0.375f,0.125f}}};
        const std::array<std::array<float,2>,3> next{{{-0.5f,0.5f},{0.375f,-0.125f},{0.5f,-0.5f}}};
        uint64_t serial=1;ComPtr<ID3D11Buffer> constant;std::array<ComPtr<ID3D11Texture2D>,2> canonicalCache;
        uint64_t previousSnapshotGeneration=0;
        for(uint32_t selector=0;selector<8;++selector)for(uint32_t frame=0;frame<3;++frame) {
            auto captured=feedbackCaptureFrame(backend,scene,depth,output,front,serial++,selector*3+frame+1,current[frame]);
            const auto canonical=backend.sceneFeedbackCapturedResources();
            require(canonical[0].Get()==captured.capture->capturedColor->texture.Get()&&canonical[1].Get()==captured.capture->capturedDepth->texture.Get()&&
                captured.capture->snapshotGeneration>previousSnapshotGeneration,"Capture accessor lost actual cached pair or monotonic content generation");
            if(canonicalCache[0])require(canonical[0].Get()==canonicalCache[0].Get()&&canonical[1].Get()==canonicalCache[1].Get(),"Another frame replaced same-size canonical snapshot cache");else canonicalCache=canonical;
            previousSnapshotGeneration=captured.capture->snapshotGeneration;
            feedbackRejected(backend,scene,depth,[&]{backend.captureSceneFeedback(serial,captured.front);});
            feedbackRejected(backend,scene,depth,[&]{backend.commitSceneFeedback(captured.capture);});
            backend.clearUiComposite();
            // Overwrite the original live Output after commit. Transport must
            // still read this frame's immutable canonical captured contents.
            const auto overwritten=feedbackOverwriteRaw(backend,output,captured.rawDepth,101+selector*3+frame);
            require(overwritten.color!=captured.raw.color&&!sameDepth(overwritten.depth,captured.raw.depth),"Capture overwrite fixture did not change actual live Output color and depth/stencil pixels");
            backend.bindTargets({scene,nullptr,nullptr,nullptr},depth);const Snapshot state(backend.context.Get());const auto before=feedbackRead(backend,scene,depth);
            const auto origins=backend.temporalSceneOrigins();auto aligned=backend.alignSceneFeedback(scene,depth,next[frame],preserve[selector]);
            require(Snapshot(backend.context.Get())==state&&!backend.uiComposite,"Next Scene alignment changed full context or reopened UI route");
            const auto after=backend.temporalSceneOrigins();
            for(size_t at=0;at<3;++at)if(preserve[selector]&(1u<<at))require(after.components[at].kind==TemporalRasterOriginKind::Grid&&
                after.components[at].jitter==next[frame]&&after.components[at].revision==origins.components[at].revision+1,"Alignment lost the explicit actual-next-jitter/component revision");
                else require(after.components[at]==origins.components[at],"Alignment changed an unselected component receipt");
            if(preserve[selector]) {
                require(aligned!=nullptr,"Nonempty next-frame feedback omitted real GPU submission");
                feedbackGold(captured.raw,before,feedbackRead(backend,scene,depth),next[frame],preserve[selector]);
                const auto cached=backend.sceneFeedbackTransferConstants();if(constant)require(constant.Get()==cached.Get(),"Multi-frame feedback replaced the cached CB on a jitter/selector change");else constant=cached;
                transferGold(backend,cached,output->physicalWidth,output->physicalHeight,scene->physicalWidth,scene->physicalHeight,next[frame]);
                backend.waitCopy(aligned);require(backend.copyComplete(aligned),"Next-frame feedback did not complete a real GPU event");
                feedbackRejected(backend,scene,depth,[&]{backend.alignSceneFeedback(scene,depth,next[frame],preserve[selector]);});
            }else require(!aligned&&after.components==origins.components,"Full-clear selector submitted preservation work or changed origins");
            // Original clear components execute after alignment; explicitly
            // supplied jitter for selectors without a real begin is only a
            // renderer case, not Runtime temporal-eligibility certification.
            const auto retained=feedbackRead(backend,scene,depth);
            if(selector&1)backend.clearTarget(scene,{1,0,1,0});
            if(selector&6)backend.clearDepthTarget(depth,0,0xA5,bool(selector&2),bool(selector&4));
            const auto final=feedbackRead(backend,scene,depth);
            for(size_t pixel=0;pixel<size_t(scene->physicalWidth)*scene->physicalHeight;++pixel) {
                require(word(final.color,pixel*4)==((selector&1)?0x3FF003FFu:word(retained.color,pixel*4)),"Original selector color clear/retention differs from exact packed gold");
                require(word(final.depth,pixel*8)==((selector&2)?floatWord(0):word(retained.depth,pixel*8)),"Original selector depth clear/retention differs from exact bits");
                require((word(final.depth,pixel*8+4)&255)==((selector&4)?0xA5u:(word(retained.depth,pixel*8+4)&255)),"Original selector stencil clear/retention differs from all-bit gold");
            }
            const auto rawAfter=feedbackRead(backend,output,captured.rawDepth);require(rawAfter.color==overwritten.color&&sameDepth(rawAfter.depth,overwritten.depth),"Alignment/original clears changed the overwritten live Output source");
            const auto canonicalAfter=feedbackRead(backend,captured.capture->capturedColor,captured.capture->capturedDepth);
            require(canonicalAfter.color==captured.raw.color&&sameDepth(canonicalAfter.depth,captured.raw.depth),"Live Output overwrite or alignment mutated the captured canonical contents");
        }
        // An intervening known full component clear invalidates canonical
        // replay for ALL selected fields: snapshot one paired Grid0 Scene.
        std::array<ComPtr<ID3D11Texture2D>,2> scratch;
        for(uint32_t changed:{1u,2u,4u}) {
            auto captured=feedbackCaptureFrame(backend,scene,depth,output,front,serial++,31+changed,{0.125f,-0.375f});backend.clearUiComposite();
            if(changed&1)backend.clearTarget(scene,{1,0,1,0});if(changed&6)backend.clearDepthTarget(depth,0,0xA5,bool(changed&2),bool(changed&4));
            const auto sceneSource=feedbackRead(backend,scene,depth);const auto origins=backend.temporalSceneOrigins();const Snapshot state(backend.context.Get());
            auto aligned=backend.alignSceneFeedback(scene,depth,{0.5f,-0.5f},7);
            require(aligned&&Snapshot(backend.context.Get())==state,"Intervening component clear snapshot lost completion or full state");
            feedbackGold(sceneSource,sceneSource,feedbackRead(backend,scene,depth),{0.5f,-0.5f},7);
            const auto actual=backend.sceneFeedbackScratchResources();require(actual[0]&&actual[1]&&actual[0].Get()!=scene->texture.Get()&&actual[1].Get()!=depth->texture.Get(),"Intervening clear did not allocate independent paired scratch resources");
            if(scratch[0])require(actual[0].Get()==scratch[0].Get()&&actual[1].Get()==scratch[1].Get(),"Same-size component clear replaced the paired scratch cache");else scratch=actual;
            const auto after=backend.temporalSceneOrigins();for(size_t at=0;at<3;++at)require(after.components[at].revision==origins.components[at].revision+1,"Snapshot alignment published multiple/missing component revisions");
            backend.waitCopy(aligned);const auto raw=feedbackRead(backend,output,captured.rawDepth);require(raw.color==captured.raw.color&&sameDepth(raw.depth,captured.raw.depth),"Snapshot fallback mutated canonical raw owners");
        }
        // Two uncommitted captures share unchanged Scene origins/active owners,
        // but reuse the cached canonical pair. Only the newer generation owns
        // its pixels; a fresh serial alone cannot legitimize the old token.
        backend.bindTargets({scene,nullptr,nullptr,nullptr},depth);backend.clearTarget(scene,{0,0,0,0});backend.clearDepthTarget(depth,0,255);
        auto generationUi=backend.promoteUiComposite(scene,depth,output,{0.125f,-0.375f});auto generationDepth=backend.routeDepth(depth);
        feedbackRawPattern(backend,output,generationDepth,70);auto firstFront=backend.copyFront(output,front);auto firstCapture=backend.captureSceneFeedback(serial++,firstFront);
        const auto generationOrigins=backend.temporalSceneOrigins();
        feedbackRawPattern(backend,output,generationDepth,71);auto secondFront=backend.copyFront(output,front);auto secondCapture=backend.captureSceneFeedback(serial++,secondFront);
        require(secondCapture->snapshotGeneration>firstCapture->snapshotGeneration&&secondCapture->capturedColor==firstCapture->capturedColor&&
            secondCapture->capturedDepth==firstCapture->capturedDepth&&backend.temporalSceneOrigins().components==generationOrigins.components,
            "Generation replacement fixture changed owners/origins rather than only canonical contents");
        feedbackRejected(backend,scene,depth,[&]{backend.commitSceneFeedback(firstCapture);});
        auto generationCommit=backend.commitSceneFeedback(secondCapture);const auto generationRaw=feedbackRead(backend,output,generationDepth);feedbackRawGold(generationRaw,71);
        backend.clearUiComposite();feedbackOverwriteRaw(backend,output,generationDepth,91);backend.bindTargets({scene,nullptr,nullptr,nullptr},depth);
        const auto generationBefore=feedbackRead(backend,scene,depth);auto generationAligned=backend.alignSceneFeedback(scene,depth,{-0.5f,0.5f},7);
        feedbackGold(generationRaw,generationBefore,feedbackRead(backend,scene,depth),{-0.5f,0.5f},7);
        backend.waitCopy(generationAligned);backend.waitCopy(generationCommit);backend.waitCopy(generationUi->completion);
        // Replacing canonical contents without committing them must invalidate
        // lastFrame replay even when EVERY Scene baseline revision is unchanged.
        auto oldBaseline=feedbackCaptureFrame(backend,scene,depth,output,front,serial++,81,{});
        const auto unchangedBaseline=backend.temporalSceneOrigins();
        feedbackRawPattern(backend,output,oldBaseline.rawDepth,83);auto replacementFront=backend.copyFront(output,front);
        auto uncommittedReplacement=backend.captureSceneFeedback(serial++,replacementFront);
        require(uncommittedReplacement->snapshotGeneration>oldBaseline.capture->snapshotGeneration&&
            backend.temporalSceneOrigins().components==unchangedBaseline.components,"Uncommitted replacement did not isolate snapshot generation from Scene receipts");
        backend.clearUiComposite();const auto currentScene=feedbackRead(backend,scene,depth);
        auto replacedAlignment=backend.alignSceneFeedback(scene,depth,{0.5f,-0.5f},7);
        feedbackGold(currentScene,currentScene,feedbackRead(backend,scene,depth),{0.5f,-0.5f},7);
        const auto replacementScratch=backend.sceneFeedbackScratchResources();
        require(replacementScratch[0].Get()==scratch[0].Get()&&replacementScratch[1].Get()==scratch[1].Get(),
            "Invalidated canonical generation did not reuse paired current-Scene snapshot cache");
        backend.waitCopy(replacedAlignment);
        // Same wrapper and descriptors do not prove the captured COM backing.
        // A real replacement after capture must reject before any Scene work.
        backend.bindTargets({scene,nullptr,nullptr,nullptr},depth);backend.clearTarget(scene,{0,0,0,0});backend.clearDepthTarget(depth,0,255);
        auto identityUi=backend.promoteUiComposite(scene,depth,output,{});auto identityDepth=backend.routeDepth(depth);
        feedbackRawPattern(backend,output,identityDepth,97);auto identityFront=backend.copyFront(output,front);
        auto identityCapture=backend.captureSceneFeedback(serial++,identityFront);
        auto replacementDepth=backend.createDepthTarget(identityDepth->physicalWidth,identityDepth->physicalHeight);
        replacementDepth->width=identityDepth->width;replacementDepth->height=identityDepth->height;
        backend.tagRenderExtent(replacementDepth->texture.Get(),replacementDepth->width,replacementDepth->height);
        backend.clearDepthTarget(replacementDepth,1,0x3C);
        D3D11_TEXTURE2D_DESC oldTexture{},newTexture{};identityDepth->texture->GetDesc(&oldTexture);replacementDepth->texture->GetDesc(&newTexture);
        D3D11_DEPTH_STENCIL_VIEW_DESC oldDsv{},newDsv{};identityDepth->view->GetDesc(&oldDsv);replacementDepth->view->GetDesc(&newDsv);
        D3D11_SHADER_RESOURCE_VIEW_DESC oldDepthSrv{},newDepthSrv{},oldStencilSrv{},newStencilSrv{};
        identityDepth->depthView->GetDesc(&oldDepthSrv);replacementDepth->depthView->GetDesc(&newDepthSrv);
        identityDepth->stencilView->GetDesc(&oldStencilSrv);replacementDepth->stencilView->GetDesc(&newStencilSrv);
        require(std::memcmp(&oldTexture,&newTexture,sizeof(oldTexture))==0&&oldDsv.Format==newDsv.Format&&oldDsv.ViewDimension==newDsv.ViewDimension&&
            oldDsv.Flags==newDsv.Flags&&oldDsv.Texture2D.MipSlice==newDsv.Texture2D.MipSlice&&
            oldDepthSrv.Format==newDepthSrv.Format&&oldDepthSrv.ViewDimension==newDepthSrv.ViewDimension&&oldDepthSrv.Texture2D.MostDetailedMip==newDepthSrv.Texture2D.MostDetailedMip&&
            oldDepthSrv.Texture2D.MipLevels==newDepthSrv.Texture2D.MipLevels&&oldStencilSrv.Format==newStencilSrv.Format&&oldStencilSrv.ViewDimension==newStencilSrv.ViewDimension&&
            oldStencilSrv.Texture2D.MostDetailedMip==newStencilSrv.Texture2D.MostDetailedMip&&oldStencilSrv.Texture2D.MipLevels==newStencilSrv.Texture2D.MipLevels,
            "Raw-depth identity regression does not use equal actual texture/DSV/SRV descriptors");
        {
            struct RestoreDepth {
                std::shared_ptr<DepthTarget> target;DepthTarget original;
                void apply(){*target=original;}
                ~RestoreDepth(){apply();}
            } restore{identityDepth,*identityDepth};
            require(identityDepth->texture.Get()!=replacementDepth->texture.Get()&&identityDepth->view.Get()!=replacementDepth->view.Get()&&
                identityDepth->depthView.Get()!=replacementDepth->depthView.Get()&&identityDepth->stencilView.Get()!=replacementDepth->stencilView.Get(),
                "Raw-depth replacement fixture reused an original COM resource/view");
            identityDepth->texture=replacementDepth->texture;identityDepth->view=replacementDepth->view;
            identityDepth->depthView=replacementDepth->depthView;identityDepth->stencilView=replacementDepth->stencilView;
            backend.bindTargets({output,nullptr,nullptr,nullptr},identityDepth);
            require(identityCapture->rawDepth==identityDepth&&backend.routeDepth(depth)==identityDepth&&backend.uiComposite->resources->outputDepth==identityDepth,
                "Raw-depth identity regression replaced the wrapper/route instead of only actual COM backing");
            ComPtr<ID3D11DepthStencilView> actual;backend.context->OMGetRenderTargets(0,nullptr,&actual);
            require(actual.Get()==replacementDepth->view.Get(),"Raw-depth identity regression did not bind the replacement actual DSV");
            feedbackRejected(backend,scene,depth,[&]{backend.commitSceneFeedback(identityCapture);});
            restore.apply();backend.bindTargets({output,nullptr,nullptr,nullptr},identityDepth);
            require(identityDepth->texture.Get()==restore.original.texture.Get()&&identityDepth->view.Get()==restore.original.view.Get()&&
                identityDepth->depthView.Get()==restore.original.depthView.Get()&&identityDepth->stencilView.Get()==restore.original.stencilView.Get(),
                "Raw-depth identity fixture failed to restore exact captured fields");
        }
        auto restoredIdentityCommit=backend.commitSceneFeedback(identityCapture);
        const auto restoredRaw=feedbackRead(backend,output,identityDepth);feedbackRawGold(restoredRaw,97);
        const auto restoredScene=feedbackRead(backend,scene,depth);feedbackGold(restoredRaw,restoredScene,restoredScene,{},7);
        backend.waitCopy(restoredIdentityCommit);backend.waitCopy(identityUi->completion);backend.clearUiComposite();
        auto captured=feedbackCaptureFrame(backend,scene,depth,output,front,serial++,47,{0.125f,-0.375f});
        feedbackRejected(backend,scene,depth,[&]{backend.alignSceneFeedback(scene,depth,{},7);});backend.clearUiComposite();
        auto foreignColor=foreign.createTarget(lw,lh,TargetFormat::RGB10A2);auto foreignDepth=foreign.createDepthTarget(lw,lh);
        feedbackRejected(backend,scene,depth,[&]{backend.alignSceneFeedback(foreignColor,foreignDepth,{},7);});
        feedbackRejected(backend,scene,depth,[&]{backend.alignSceneFeedback(scene,depth,{0.5001f,0},7);});
        feedbackRejected(backend,scene,depth,[&]{backend.alignSceneFeedback(scene,depth,{0,std::numeric_limits<float>::quiet_NaN()},7);});
        feedbackRejected(backend,scene,depth,[&]{backend.alignSceneFeedback(scene,depth,{},8);});
        feedbackRejected(backend,scene,depth,[&]{backend.commitSceneFeedback(captured.capture);});
        // A generic incoming copy proves Unknown rather than manufacturing an
        // explicit origin in the test. Copies OUT do not poison the receipts.
        auto poisonColor=backend.createTarget(scene->physicalWidth,scene->physicalHeight,TargetFormat::RGB10A2);
        auto poisonDepth=backend.createDepthTarget(scene->physicalWidth,scene->physicalHeight);
        logicalColor(backend,poisonColor,lw,lh);poisonDepth->width=lw;poisonDepth->height=lh;backend.tagRenderExtent(poisonDepth->texture.Get(),lw,lh);
        backend.clearTarget(poisonColor,{0,1,1,1});backend.clearDepthTarget(poisonDepth,0,0x3C);backend.bindTargets({poisonColor,nullptr,nullptr,nullptr},poisonDepth);
        auto poisonedColor=backend.copyFront(poisonColor,scene);auto poisonedDepth=backend.copyDepth(poisonDepth,depth);
        const auto poisoned=backend.temporalSceneOrigins();for(const auto& component:poisoned.components)require(component.kind==TemporalRasterOriginKind::Unknown,"Generic incoming Scene copy invented a qualified raster grid");
        feedbackRejected(backend,scene,depth,[&]{backend.alignSceneFeedback(scene,depth,{0.5f,-0.5f},7);});
        backend.waitCopy(poisonedColor);backend.waitCopy(poisonedDepth);backend.bindTargets({scene,nullptr,nullptr,nullptr},depth);backend.waitIdle();foreign.waitIdle();
        std::printf("Verified Scene feedback frames %s Scene=%ux%u raw=%ux%u: all8 selectors x3 actual explicit jitter frames, capture/commit events, component receipts, intervening clears/all-selected scratch, cached CB, generic copy poison and transactional rejects (%s)\n",
            mode==Antialiasing::DLAA?"DLAA":"DLSSQuality",scene->physicalWidth,scene->physicalHeight,output->physicalWidth,output->physicalHeight,software?"WARP":"hardware");
    }
    static void feedbackCaptureLifetime(bool software) {
        auto backend=std::make_unique<NativeBackend>(software,false);backend->configureRendering(1280,720,1,Antialiasing::DLAA);
        auto scene=backend->createTarget(12,8,TargetFormat::RGB10A2);auto depth=backend->createDepthTarget(12,8);
        auto output=backend->createTarget(12,8,TargetFormat::RGB10A2);auto front=backend->createTarget(12,8,TargetFormat::RGB10A2);
        backend->registerTemporalWorldTargets(scene,depth);
        auto captured=feedbackCaptureFrame(*backend,scene,depth,output,front,1,19,{});backend->clearUiComposite();backend->waitIdle();
        std::weak_ptr<RenderTarget> weakScene=scene,weakOutput=output,weakFront=front,weakCanonical=captured.capture->capturedColor;
        std::weak_ptr<DepthTarget> weakDepth=depth,weakRawDepth=captured.rawDepth,weakCanonicalDepth=captured.capture->capturedDepth;
        auto frame=captured.capture;captured={};scene.reset();depth.reset();output.reset();front.reset();backend.reset();
        require(!weakScene.expired()&&!weakDepth.expired()&&!weakOutput.expired()&&!weakFront.expired()&&!weakRawDepth.expired()&&
            !weakCanonical.expired()&&!weakCanonicalDepth.expired(),"Completed capture lost registered/raw/canonical/front owners after backend destruction");
        frame.reset();require(weakScene.expired()&&weakDepth.expired()&&weakOutput.expired()&&weakFront.expired()&&weakRawDepth.expired()&&
            weakCanonical.expired()&&weakCanonicalDepth.expired(),"Scene feedback capture/cache/completion retains an ownership cycle");
    }
    static int runFeedback(bool software) {
        const auto initialChecks=uiCompositeChecks;
        feedbackDirect(software,12,8,12,8);feedbackDirect(software,7,5,12,8);
        feedbackFrames(software,Antialiasing::DLAA,1280,720);feedbackFrames(software,Antialiasing::DLSSQuality,768,432);
        feedbackCaptureLifetime(software);
        std::printf("SCENE_FEEDBACK completion=PASS local_checks=%llu direct_extents=2 frame_extents=2 selectors=8 frames_per_selector=3 canonical_overwrite=PASS generation_replacement=PASS raw_depth_identity=PASS component_origin=PASS full_state=PASS cache=PASS lifetime=PASS offscreen_renderer_only=1 driver=%s\n",
            static_cast<unsigned long long>(uiCompositeChecks-initialChecks),software?"WARP":"hardware");return 0;
    }
    static std::array<uint32_t,4> backgroundJitterBytes(NativeBackend& backend,const ComPtr<ID3D11Buffer>& buffer) {
        require(buffer.Get()!=nullptr,"Empty background lost its actual cached jitter constant");D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);
        require(desc.ByteWidth==16&&desc.Usage==D3D11_USAGE_DEFAULT&&desc.BindFlags==D3D11_BIND_CONSTANT_BUFFER&&!desc.CPUAccessFlags&&
            !desc.MiscFlags&&!desc.StructureByteStride,"Empty background jitter is not the reused 16-byte DEFAULT constant");
        ComPtr<ID3D11Device> owner;buffer->GetDevice(&owner);require(owner.Get()==backend.device.Get(),"Empty background jitter buffer belongs to another actual device");
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Buffer> staging;
        check(backend.device->CreateBuffer(&desc,nullptr,&staging),"Empty background jitter staging creation failed");backend.context->CopyResource(staging.Get(),buffer.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};check(backend.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Empty background jitter staging map failed");
        std::array<uint32_t,4> result{};std::memcpy(result.data(),mapped.pData,sizeof(result));backend.context->Unmap(staging.Get(),0);return result;
    }
    static std::array<uint32_t,2> backgroundCandidates(uint32_t pixel,uint32_t source,uint32_t output,float jitter) {
        if(double(jitter)*8==std::round(double(jitter)*8)) {const auto selected=jitterCoordinate(pixel,source,output,jitter);return {selected,selected};}
        const long double coordinate=(static_cast<long double>(pixel)+0.5L)*source/output+static_cast<long double>(jitter);
        const long double error=8*std::numeric_limits<float>::epsilon()*std::max(1.0L,std::abs(coordinate));
        const int64_t low=int64_t(std::floor(coordinate-error)),high=int64_t(std::floor(coordinate+error));require(high-low<=1,"Empty-background nearest interval escaped its bounded FP contract");
        return {uint32_t(std::clamp(low,int64_t(0),int64_t(source-1))),uint32_t(std::clamp(high,int64_t(0),int64_t(source-1)))};
    }
    static void backgroundGold(const FeedbackImage& source,const FeedbackImage& output,std::array<float,2> sourceJitter) {
        require(output.color.size()==size_t(output.width)*output.height*4&&output.depth.size()==size_t(output.width)*output.height*8,
            "Empty-background output has incorrect actual paired extent");
        for(uint32_t y=0;y<output.height;++y)for(uint32_t x=0;x<output.width;++x) {
            const size_t at=size_t(y)*output.width+x;const uint32_t color=word(output.color,at*4),depth=word(output.depth,at*8),stencil=word(output.depth,at*8+4)&255;
            // Forward source-grid transport: source jitter is in SOURCE render
            // pixels. RGB coordinate = OutputCenter*Scene/Output+jSource-0.5.
            const long double px=std::clamp((static_cast<long double>(x)+0.5L)*source.width/output.width+sourceJitter[0]-0.5L,0.0L,static_cast<long double>(source.width-1));
            const long double py=std::clamp((static_cast<long double>(y)+0.5L)*source.height/output.height+sourceJitter[1]-0.5L,0.0L,static_cast<long double>(source.height-1));
            const uint32_t x0=uint32_t(std::floor(px)),x1=std::min(x0+1,source.width-1),y0=uint32_t(std::floor(py)),y1=std::min(y0+1,source.height-1);
            const long double fx=px-x0,fy=py-y0;
            const std::array<uint32_t,4> codes{word(source.color,(size_t(y0)*source.width+x0)*4),word(source.color,(size_t(y0)*source.width+x1)*4),
                word(source.color,(size_t(y1)*source.width+x0)*4),word(source.color,(size_t(y1)*source.width+x1)*4)};
            for(uint32_t channel=0;channel<3;++channel) {
                const long double a=(codes[0]>>(10*channel))&1023,b=(codes[1]>>(10*channel))&1023,c=(codes[2]>>(10*channel))&1023,d=(codes[3]>>(10*channel))&1023;
                const long double expected=(a*(1-fx)+b*fx)*(1-fy)+(c*(1-fx)+d*fx)*fy;
                const long double bound=1.05L+(std::max(std::abs(b-a),std::abs(d-c))+std::max(std::abs(c-a),std::abs(d-b)))/256;
                require(std::abs(static_cast<long double>((color>>(10*channel))&1023)-expected)<=bound,"Empty-background RGB uses an incorrect source-grid jitter/extent or bilinear value");
            }
            const auto xs=backgroundCandidates(x,source.width,output.width,sourceJitter[0]),ys=backgroundCandidates(y,source.height,output.height,sourceJitter[1]);bool paired=false;
            for(uint32_t sy:ys)for(uint32_t sx:xs) {const size_t sourceAt=size_t(sy)*source.width+sx;
                paired=paired||((color>>30)==(word(source.color,sourceAt*4)>>30)&&depth==word(source.depth,sourceAt*8)&&stencil==(word(source.depth,sourceAt*8+4)&255));}
            require(paired,"Empty-background packed alpha/depth/stencil do not share the common actual source-grid texel");
            if(sourceJitter==std::array<float,2>{}&&source.width==output.width&&source.height==output.height)
                require(color==word(source.color,at*4),"Default-zero equal-size empty background changed an exact packed color byte");
        }
    }
    static void emptyBackgroundGrid(bool software,Antialiasing mode,uint32_t configuredWidth,uint32_t configuredHeight) {
        constexpr uint32_t lw=12,lh=8;NativeBackend backend(software,false);backend.configureRendering(1280,720,1,mode);backend.configureSceneExtent(configuredWidth,configuredHeight);
        auto scene=backend.createTarget(lw,lh,TargetFormat::RGB10A2,TargetScale::Scene);auto depth=backend.createDepthTarget(lw,lh,TargetScale::Scene);
        auto output=backend.createTarget(lw,lh,TargetFormat::RGB10A2,TargetScale::Output);auto front=backend.createTarget(lw,lh,TargetFormat::RGB10A2,TargetScale::Output);
        backend.temporalScene=std::make_shared<TemporalScenePipeline>(backend.device.Get(),backend.context.Get(),DlssExtent{scene->physicalWidth,scene->physicalHeight,output->physicalWidth,output->physicalHeight});
        backend.temporalOutput=output;backend.registerTemporalWorldTargets(scene,depth);const auto pipeline=backend.temporalScene;
        const auto jitterBuffer=pipeline->uiBackgroundJitterConstants();uint64_t serial=1;
        const std::array<uint32_t,4> selectors{2,3,6,2};
        for(size_t which=0;which<selectors.size();++which) {
            const uint32_t selector=selectors[which];backend.bindTargets({scene,nullptr,nullptr,nullptr},depth);
            backend.beginTemporalScene(scene,depth,0xC001,1);const auto actualEmptyFrame=pipeline->frame();
            require(!actualEmptyFrame.geometryDraws&&!actualEmptyFrame.motionDraws&&!backend.temporalSceneHasGeometry(),"Empty background fabricated geometry/motion coverage");
            auto captured=feedbackCaptureFrame(backend,scene,depth,output,front,serial++,51+uint32_t(which),actualEmptyFrame.jitter);backend.clearUiComposite();
            const auto actualSource=which==3?std::array<float,2>{-0.375f,0.5f}:actualEmptyFrame.jitter;
            require(actualSource!=std::array<float,2>{},"Empty background fixture has no actual nonzero source grid");
            auto aligned=backend.alignSceneFeedback(scene,depth,actualSource,selector==2?5u:selector==3?4u:1u);
            if(selector&1)backend.clearTarget(scene,{1,0,1,0});backend.clearDepthTarget(depth,0,0xA5,bool(selector&2),bool(selector&4));
            const auto source=feedbackRead(backend,scene,depth);const auto origins=backend.temporalSceneOrigins();const Snapshot before(backend.context.Get());
            const auto sourceJitter=backend.temporalSceneOriginGrid(scene,depth);
            require(sourceJitter==actualSource&&Snapshot(backend.context.Get())==before&&backend.temporalSceneOrigins().components==origins.components,
                "Common source-grid query changed state/origins or guessed the current empty-frame jitter");
            if(which==3)require(sourceJitter!=actualEmptyFrame.jitter,"Two-axis source-grid case did not distinguish the unused planned frame sample");
            auto background=backend.prepareTemporalUiBackground(scene,depth,sourceJitter);
            require(background==output&&!backend.temporalSceneActive()&&backend.temporalScene==pipeline&&Snapshot(backend.context.Get())==before,
                "Actual empty-background copy/reset changed full context or replaced owned pipeline/output");
            require(pipeline->uiBackgroundJitterConstants().Get()==jitterBuffer.Get()&&backgroundJitterBytes(backend,jitterBuffer)==
                std::array<uint32_t,4>{floatWord(sourceJitter[0]),floatWord(sourceJitter[1]),0,0},"Empty-background cached b1 jitter identity/data is stale");
            auto promoted=backend.promoteUiComposite(scene,depth,background,sourceJitter);auto selectedDepth=backend.routeDepth(depth);
            backgroundGold(source,feedbackRead(backend,background,selectedDepth),sourceJitter);
            const auto unchanged=feedbackRead(backend,scene,depth);require(unchanged.color==source.color&&sameDepth(unchanged.depth,source.depth),"Empty background/promotion changed actual source Scene pixels");
            backend.waitCopy(aligned);backend.waitCopy(promoted->completion);backend.clearUiComposite();require(Snapshot(backend.context.Get())==before,"Empty-background owned close changed complete native context");
        }
        // Preserve the omitted/default-zero call and Uniform fold while the
        // real empty pipeline carries a different planned frame sample.
        backend.bindTargets({scene,nullptr,nullptr,nullptr},depth);backend.clearTarget(scene,{1,0,1,1});backend.clearDepthTarget(depth,0,0xA5);
        backend.beginTemporalScene(scene,depth,0xC001,1);require(backend.temporalSceneOriginGrid(scene,depth)==std::array<float,2>{},"All-Uniform common grid did not fold to zero");
        const auto zeroSource=feedbackRead(backend,scene,depth);const Snapshot zeroState(backend.context.Get());auto zeroOutput=backend.prepareTemporalUiBackground(scene,depth);
        auto zeroPromoted=backend.promoteUiComposite(scene,depth,zeroOutput);backgroundGold(zeroSource,feedbackRead(backend,zeroOutput,backend.routeDepth(depth)),{});
        require(backgroundJitterBytes(backend,jitterBuffer)==std::array<uint32_t,4>{},"Default-zero background retained previous nonzero b1 jitter data");
        backend.waitCopy(zeroPromoted->completion);backend.clearUiComposite();require(Snapshot(backend.context.Get())==zeroState,"Default-zero background changed full context");
        backend.clearTarget(scene,{1,0,1,1});backend.clearDepthTarget(depth,0,0xA5);
        auto colorGrid=backend.alignSceneFeedback(scene,depth,{0.125f,-0.375f},1);auto depthGrid=backend.alignSceneFeedback(scene,depth,{-0.375f,0.125f},2);
        feedbackRejected(backend,scene,depth,[&]{backend.temporalSceneOriginGrid(scene,depth);});backend.waitCopy(colorGrid);backend.waitCopy(depthGrid);
        auto poison=backend.createTarget(scene->physicalWidth,scene->physicalHeight,TargetFormat::RGB10A2);logicalColor(backend,poison,lw,lh);backend.clearTarget(poison,{0,1,0,1});
        backend.bindTargets({poison,nullptr,nullptr,nullptr},{});auto copied=backend.copyFront(poison,scene);
        require(backend.temporalSceneOrigins().components[0].kind==TemporalRasterOriginKind::Unknown,"Actual incoming copy did not establish Unknown for empty source-grid rejection");
        feedbackRejected(backend,scene,depth,[&]{backend.temporalSceneOriginGrid(scene,depth);});backend.waitCopy(copied);backend.bindTargets({scene,nullptr,nullptr,nullptr},depth);backend.waitIdle();
    }
    static int runEmptyBackgroundGrid(bool software) {
        const auto initial=uiCompositeChecks;emptyBackgroundGrid(software,Antialiasing::DLAA,1280,720);emptyBackgroundGrid(software,Antialiasing::DLSSQuality,768,432);
        std::printf("EMPTY_BACKGROUND_GRID completion=PASS local_checks=%llu actual_pipeline=1 selectors=2,3,6 source_grid=PASS RGB_alpha_DS=PASS full_state=PASS source_immutable=PASS cached_b1=PASS zero_default=PASS mixed_unknown=rejected ngx_evaluation=0 driver=%s\n",
            static_cast<unsigned long long>(uiCompositeChecks-initial),software?"WARP":"hardware");return 0;
    }
};
}
int main(int argc,char** argv) {
    try {
        const bool hardware=argc>=2&&std::strcmp(argv[1],"--hardware")==0;
        const bool spatialOnly=argc>=2&&std::strcmp(argv[argc-1],"--spatial-only")==0;
        require(argc==1||(argc==2&&(hardware||spatialOnly))||(argc==3&&hardware&&spatialOnly),
                "Usage: UICompositeTests [--hardware] [--spatial-only]");
        const int baseline=NativeUiCompositeProbe::run(!hardware,spatialOnly);if(baseline||spatialOnly)return baseline;
        const int jitter=NativeUiCompositeProbe::runJitter(argc==1);if(jitter)return jitter;
        const int copies=NativeUiCompositeProbe::runCopyRoles(argc==1);if(copies)return copies;
        const int feedback=NativeUiCompositeProbe::runFeedback(argc==1);if(feedback)return feedback;
        return NativeUiCompositeProbe::runEmptyBackgroundGrid(argc==1);
    }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
