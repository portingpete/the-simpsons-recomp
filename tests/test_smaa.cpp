#include "renderer/native_backend.h"
#include "renderer/antialiasing.h"
#include "renderer/smaa_pipeline.h"
#include "renderer/vendor/smaa/AreaTex.h"
#include "renderer/vendor/smaa/SearchTex.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace Simpsons::Graphics;
namespace {
size_t checks=0;
void require(bool condition,const char* reason) {++checks;if(!condition)throw Error(reason);}
void check(HRESULT result,const char* reason) {require(SUCCEEDED(result),reason);}
template<class F> void rejects(F&& action) {
    try {action();}catch(const Error&){++checks;return;}
    throw Error("Invalid SMAA operation did not fail closed");
}
constexpr uint32_t white=0xffffffffu,black=0xc0000000u;
uint32_t word(const std::vector<uint8_t>& bytes,size_t index) {
    uint32_t result{};std::memcpy(&result,bytes.data()+index*4,4);return result;
}
std::vector<uint8_t> readTexture(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source,UINT pixelBytes) {
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&desc,nullptr,&staging),"SMAA staging allocation failed");
    context->CopyResource(staging.Get(),source);
    D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"SMAA staging read failed");
    struct Unmap {ID3D11DeviceContext* context;ID3D11Resource* resource;~Unmap(){context->Unmap(resource,0);}} unmap{context,staging.Get()};
    std::vector<uint8_t> result(size_t(desc.Width)*desc.Height*pixelBytes);
    for(UINT y=0;y<desc.Height;++y)
        std::memcpy(result.data()+size_t(y)*desc.Width*pixelBytes,static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch,size_t(desc.Width)*pixelBytes);
    return result;
}
void descriptor(ID3D11Texture2D* texture,ID3D11Device* expected,UINT width,UINT height,DXGI_FORMAT format,D3D11_USAGE usage,UINT bindings) {
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);ComPtr<ID3D11Device> device;texture->GetDevice(&device);
    require(device.Get()==expected&&desc.Width==width&&desc.Height==height&&desc.Format==format&&desc.Usage==usage&&
            desc.MipLevels==1&&desc.ArraySize==1&&desc.SampleDesc.Count==1&&desc.SampleDesc.Quality==0&&
            desc.BindFlags==bindings&&desc.CPUAccessFlags==0&&desc.MiscFlags==0,"SMAA GPU texture descriptor/device differs");
}
void viewDescriptor(ID3D11ShaderResourceView* view,ID3D11Texture2D* expected,DXGI_FORMAT format) {
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};view->GetDesc(&desc);ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
    require(resource.Get()==expected&&desc.Format==format&&desc.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D&&
            desc.Texture2D.MostDetailedMip==0&&desc.Texture2D.MipLevels==1,"SMAA lookup view resource/descriptor differs");
}
void metricBytes(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Buffer* buffer,UINT width,UINT height) {
    D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);ComPtr<ID3D11Device> owner;buffer->GetDevice(&owner);
    require(owner.Get()==device&&desc.ByteWidth==16&&desc.Usage==D3D11_USAGE_IMMUTABLE&&desc.BindFlags==D3D11_BIND_CONSTANT_BUFFER&&
            desc.CPUAccessFlags==0&&desc.MiscFlags==0&&desc.StructureByteStride==0,"SMAA metric GPU buffer descriptor/device differs");
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;check(device->CreateBuffer(&desc,nullptr,&staging),"SMAA metric staging allocation failed");
    context->CopyResource(staging.Get(),buffer);D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"SMAA metric staging read failed");
    const std::array<float,4> expected{1.0f/float(width),1.0f/float(height),float(width),float(height)};
    const bool equal=std::memcmp(mapped.pData,expected.data(),sizeof(expected))==0;context->Unmap(staging.Get(),0);
    require(equal,"SMAA actual VS/PS pixel metrics differ from the prepared extent");
}
}
// These probes are local to this fixture executable. No runtime test callback,
// context accessor, or mutable public graphics API is introduced.
namespace Simpsons::Graphics::detail {
struct SMAAPipelineProbe {
    struct Resources {
        ComPtr<ID3D11Texture2D> area,search,edges,weights;
        ComPtr<ID3D11ShaderResourceView> areaView,searchView;
        ComPtr<ID3D11Buffer> metrics;
        std::array<ComPtr<ID3D11VertexShader>,3> vertices;
        std::array<ComPtr<ID3D11PixelShader>,3> pixels;
    };
    static Resources resources(const SMAAPipeline& pipeline) {
        return {pipeline.areaTexture,pipeline.searchTexture,pipeline.edges.texture,pipeline.weights.texture,pipeline.areaView,pipeline.searchView,pipeline.metrics,
                {pipeline.edgeVertex,pipeline.weightVertex,pipeline.neighborhoodVertex},{pipeline.edgePixel,pipeline.weightPixel,pipeline.neighborhoodPixel}};
    }
};
}
namespace Simpsons::Graphics {
struct NativeAntialiasingProbe {
    static void lazyResources(bool software) {
        for(const auto mode:{Antialiasing::Original,Antialiasing::FXAA}) {
            NativeBackend backend(software,false);backend.configureRendering(1280,720,1,mode);
            require(!backend.antialiasingPipeline,"Unwarmed backend already has presentation resources");
            backend.warmPresentationFilters();
            require(backend.antialiasingPipeline&&!backend.antialiasingPipeline->smaa,"Ordinary presentation warmup eagerly constructed SMAA resources");
            auto source=backend.createTarget(31,17,TargetFormat::RGB10A2);
            backend.clearTarget(source,{0.25f,0.5f,0.75f,1});
            (void)backend.resolveAntialiasing(source);
            require(!backend.antialiasingPipeline->smaa,"Original or FXAA resolve constructed SMAA resources");
            backend.configureSharpening(50);(void)backend.resolveAntialiasing(source);
            require(!backend.antialiasingPipeline->smaa,"Ordinary sharpening constructed SMAA resources");
        }
        NativeBackend backend(software,false);backend.configureRendering(1280,720,1,Antialiasing::SMAA);
        backend.warmPresentationFilters();require(backend.antialiasingPipeline&&!backend.antialiasingPipeline->smaa,"Warmup constructed SMAA before its selected resolve");
        auto source=backend.createTarget(31,17,TargetFormat::RGB10A2);backend.clearTarget(source,{0.25f,0.5f,0.75f,1});
        auto output=backend.resolveAntialiasing(source);auto* instance=backend.antialiasingPipeline->smaa.get();
        require(instance!=nullptr&&output!=source,"Selected SMAA did not construct its owned pass resources");
        const auto initial=detail::SMAAPipelineProbe::resources(*instance);lookups(backend,initial);
        for(const auto& shader:initial.vertices)require(bool(shader),"Selected SMAA lacks an actual vertex pass shader");
        for(const auto& shader:initial.pixels)require(bool(shader),"Selected SMAA lacks an actual pixel pass shader");
        auto again=backend.resolveAntialiasing(source);const auto reused=detail::SMAAPipelineProbe::resources(*backend.antialiasingPipeline->smaa);
        require(again==output&&backend.antialiasingPipeline->smaa.get()==instance&&
                reused.area==initial.area&&reused.search==initial.search&&reused.areaView==initial.areaView&&reused.searchView==initial.searchView&&
                reused.vertices==initial.vertices&&reused.pixels==initial.pixels&&reused.edges==initial.edges&&reused.weights==initial.weights&&reused.metrics==initial.metrics,
                "Repeated SMAA resolve re-created its instance, lookup, shader or per-extent cache");
        auto resized=backend.createTarget(47,23,TargetFormat::RGB10A2);backend.clearTarget(resized,{0.5f,0.25f,0.75f,1});
        (void)backend.resolveAntialiasing(resized);const auto changed=detail::SMAAPipelineProbe::resources(*backend.antialiasingPipeline->smaa);
        require(backend.antialiasingPipeline->smaa.get()==instance&&changed.area==initial.area&&changed.search==initial.search&&
                changed.areaView==initial.areaView&&changed.searchView==initial.searchView&&changed.vertices==initial.vertices&&changed.pixels==initial.pixels,
                "SMAA resize re-created immutable instance, lookup or shader resources");
        require(changed.edges!=initial.edges&&changed.weights!=initial.weights&&changed.metrics!=initial.metrics,"SMAA resize retained old per-extent GPU resources");
        descriptor(changed.edges.Get(),backend.device.Get(),47,23,DXGI_FORMAT_R8G8_UNORM,D3D11_USAGE_DEFAULT,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
        metricBytes(backend.device.Get(),backend.context.Get(),changed.metrics.Get(),47,23);
        std::printf("Verified %s SMAA laziness for warmup/Original/FXAA/sharpen and immutable instance/GPU resource reuse across resize\n",software?"WARP":"hardware");
    }
    static void upload(NativeBackend& backend,const std::shared_ptr<RenderTarget>& target,const std::vector<uint32_t>& values) {
        require(values.size()==size_t(target->pixelWidth())*target->pixelHeight(),"SMAA input extent differs");
        backend.context->UpdateSubresource(target->texture.Get(),0,nullptr,values.data(),target->pixelWidth()*4,UINT(values.size()*4));
    }
    static D3D11_QUERY_DATA_PIPELINE_STATISTICS draw(NativeBackend& backend,detail::SMAAPipeline& pipeline,
                                                   const std::shared_ptr<RenderTarget>& source,const std::shared_ptr<RenderTarget>& output) {
        auto* context=backend.context.Get();context->ClearState();
        const D3D11_VIEWPORT viewport{0,0,float(source->pixelWidth()),float(source->pixelHeight()),0,1};context->RSSetViewports(1,&viewport);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        // Default culling would remove this fullscreen triangle. Use the same
        // no-cull, no-depth isolated pass contract as the actual backend.
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        ComPtr<ID3D11RasterizerState> state;check(backend.device->CreateRasterizerState(&raster,&state),"SMAA fixture raster allocation failed");
        context->RSSetState(state.Get());
        D3D11_DEPTH_STENCIL_DESC disabled{};disabled.DepthFunc=D3D11_COMPARISON_ALWAYS;
        ComPtr<ID3D11DepthStencilState> depth;check(backend.device->CreateDepthStencilState(&disabled,&depth),"SMAA fixture depth allocation failed");
        context->OMSetDepthStencilState(depth.Get(),0);
        D3D11_QUERY_DESC desc{D3D11_QUERY_PIPELINE_STATISTICS,0};ComPtr<ID3D11Query> query;
        check(backend.device->CreateQuery(&desc,&query),"SMAA statistics allocation failed");
        context->Begin(query.Get());pipeline.draw(context,source->sampledView.Get(),output->view.Get());context->End(query.Get());
        D3D11_QUERY_DATA_PIPELINE_STATISTICS statistics{};const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        for(;;) {
            const auto result=context->GetData(query.Get(),&statistics,sizeof(statistics),0);
            check(result,"SMAA statistics read failed");if(result==S_OK)break;
            require(std::chrono::steady_clock::now()<deadline,"SMAA statistics exceeded a bounded GPU wait");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(statistics.VSInvocations==9&&statistics.IAPrimitives==3,"SMAA did not execute three independent fullscreen GPU passes");
        require(statistics.PSInvocations>=uint64_t(source->pixelWidth())*source->pixelHeight()*3,"SMAA did not shade all three fullscreen passes");
        return statistics;
    }
    static void lookups(NativeBackend& backend,const detail::SMAAPipelineProbe::Resources& resources) {
        descriptor(resources.area.Get(),backend.device.Get(),160,560,DXGI_FORMAT_R8G8_UNORM,D3D11_USAGE_IMMUTABLE,D3D11_BIND_SHADER_RESOURCE);
        descriptor(resources.search.Get(),backend.device.Get(),64,16,DXGI_FORMAT_R8_UNORM,D3D11_USAGE_IMMUTABLE,D3D11_BIND_SHADER_RESOURCE);
        viewDescriptor(resources.areaView.Get(),resources.area.Get(),DXGI_FORMAT_R8G8_UNORM);
        viewDescriptor(resources.searchView.Get(),resources.search.Get(),DXGI_FORMAT_R8_UNORM);
        const auto area=readTexture(backend.device.Get(),backend.context.Get(),resources.area.Get(),2);
        const auto search=readTexture(backend.device.Get(),backend.context.Get(),resources.search.Get(),1);
        require(area.size()==179200&&search.size()==1024,"SMAA canonical lookup extent differs");
        require(std::memcmp(area.data(),areaTexBytes,area.size())==0&&std::memcmp(search.data(),searchTexBytes,search.size())==0,
                "SMAA actual immutable GPU lookup bytes differ from pinned canonical upstream tables");
    }
    static void run(bool software) {
        NativeBackend backend(software,false);backend.configureRendering(1280,720,1,Antialiasing::SMAA);
        detail::SMAAPipeline pipeline(backend.device.Get());
        rejects([&]{pipeline.prepare(0,32);});rejects([&]{pipeline.prepare(32,0);});
        rejects([&]{pipeline.prepare(D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION+1,32);});
        for(const auto extent:{std::array<UINT,2>{64,32},std::array<UINT,2>{65,33},std::array<UINT,2>{129,17}}) {
            const auto width=extent[0],height=extent[1];pipeline.prepare(width,height);
            const auto resources=detail::SMAAPipelineProbe::resources(pipeline);lookups(backend,resources);
            descriptor(resources.edges.Get(),backend.device.Get(),width,height,DXGI_FORMAT_R8G8_UNORM,D3D11_USAGE_DEFAULT,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
            descriptor(resources.weights.Get(),backend.device.Get(),width,height,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_USAGE_DEFAULT,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
            metricBytes(backend.device.Get(),backend.context.Get(),resources.metrics.Get(),width,height);
            pipeline.prepare(width,height);const auto reused=detail::SMAAPipelineProbe::resources(pipeline);
            require(reused.edges==resources.edges&&reused.weights==resources.weights&&reused.metrics==resources.metrics,"SMAA reused extent reallocated its cached resources");
            auto source=backend.createTarget(width,height,TargetFormat::RGB10A2),output=backend.createTarget(width,height,TargetFormat::RGB10A2);
            std::vector<uint32_t> values(size_t(width)*height);
            for(const bool horizontal:{false,true}) {
                for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x)
                    values[size_t(y)*width+x]=(horizontal?y>=height/2:x>=width/2)?white:black;
                upload(backend,source,values);draw(backend,pipeline,source,output);
                const auto edges=readTexture(backend.device.Get(),backend.context.Get(),resources.edges.Get(),2);
                // The original SMAA edge encoding is west in R, north in G.
                // This independent binary-edge oracle detects flips, swapped
                // channels, half-texel offsets and stale metrics after resize.
                for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x) {
                    const auto offset=(size_t(y)*width+x)*2;
                    require(edges[offset]==(!horizontal&&x==width/2?255:0)&&edges[offset+1]==(horizontal&&y==height/2?255:0),
                            "SMAA edge pass orientation/metrics differs from the original west/north channel encoding");
                }
            }
            for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x)values[size_t(y)*width+x]=x>=y?white:black;
            upload(backend,source,values);const auto unchanged=backend.readbackTarget(source);draw(backend,pipeline,source,output);
            const auto weights=readTexture(backend.device.Get(),backend.context.Get(),resources.weights.Get(),4),filtered=backend.readbackTarget(output);
            require(std::any_of(weights.begin(),weights.end(),[](uint8_t value){return value!=0;}),"SMAA canonical weight pass produced no blend weights on a diagonal");
            size_t softened=0;
            for(size_t i=0;i<values.size();++i) {
                const auto value=word(filtered,i),gray=value&1023u;
                require((value>>30)==3&&((value>>10)&1023u)==gray&&((value>>20)&1023u)==gray,"SMAA grayscale edge changed opaque alpha or introduced tint");
                if(gray&&gray!=1023)++softened;
            }
            require(softened>=size_t(std::min(width,height)-2)&&filtered!=unchanged,"SMAA neighborhood pass did not produce diagonal subpixel coverage");
            require(backend.readbackTarget(source)==unchanged,"SMAA modified its original single-sample input");
            // A later flat frame must clear discarded edge pixels and every
            // blend channel instead of reusing weights from the diagonal.
            const uint32_t flat=0xc0000000u|147u|(331u<<10)|(719u<<20);std::fill(values.begin(),values.end(),flat);
            upload(backend,source,values);draw(backend,pipeline,source,output);
            const auto clearedEdges=readTexture(backend.device.Get(),backend.context.Get(),resources.edges.Get(),2);
            const auto clearedWeights=readTexture(backend.device.Get(),backend.context.Get(),resources.weights.Get(),4);
            require(std::all_of(clearedEdges.begin(),clearedEdges.end(),[](uint8_t v){return v==0;})&&
                    std::all_of(clearedWeights.begin(),clearedWeights.end(),[](uint8_t v){return v==0;}),"SMAA cached intermediate targets retained a previous frame");
            require(backend.readbackTarget(output)==backend.readbackTarget(source),"SMAA changed packed flat RGB10A2 pixels");
            // Equal-luma red/green edges exercise genuine color detection;
            // merely relabeling luma FXAA cannot satisfy this pass oracle.
            for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x)values[size_t(y)*width+x]=0xc0000000u|(x>=y?1023u:304u<<10);
            upload(backend,source,values);draw(backend,pipeline,source,output);
            require(backend.readbackTarget(output)!=backend.readbackTarget(source),"SMAA missed a chroma-only jagged edge");
            rejects([&]{pipeline.draw(backend.context.Get(),nullptr,output->view.Get());});
            rejects([&]{pipeline.draw(backend.context.Get(),source->sampledView.Get(),source->view.Get());});
            auto wrong=backend.createTarget(width+1,height,TargetFormat::RGB10A2);
            rejects([&]{pipeline.draw(backend.context.Get(),source->sampledView.Get(),wrong->view.Get());});
            auto rgba=backend.createTarget(width,height,TargetFormat::RGBA8);
            rejects([&]{pipeline.draw(backend.context.Get(),rgba->sampledView.Get(),output->view.Get());});
            ComPtr<ID3D11DeviceContext> deferred;check(backend.device->CreateDeferredContext(0,&deferred),"SMAA deferred fixture allocation failed");
            rejects([&]{pipeline.draw(deferred.Get(),source->sampledView.Get(),output->view.Get());});
        }
        NativeBackend foreign(software,false);detail::SMAAPipeline other(foreign.device.Get());other.prepare(129,17);
        auto source=backend.createTarget(129,17,TargetFormat::RGB10A2),output=backend.createTarget(129,17,TargetFormat::RGB10A2);
        rejects([&]{other.draw(backend.context.Get(),source->sampledView.Get(),output->view.Get());});
        const auto a=detail::SMAAPipelineProbe::resources(pipeline),b=detail::SMAAPipelineProbe::resources(other);
        require(a.area!=b.area&&a.search!=b.search,"SMAA lookup cache was shared across different device owners");
        std::printf("Verified %s genuine SMAA1x HIGH: canonical GPU lookups, three passes, orientation, chroma, clear/reuse/resize and fail-closed device/front contracts\n",software?"WARP":"hardware");
    }
    static void lifetime(bool software) {
        detail::SMAAPipelineProbe::Resources held;ID3D11Device* identity=nullptr;
        {
            NativeBackend backend(software,false);identity=backend.device.Get();
            detail::SMAAPipeline pipeline(backend.device.Get());pipeline.prepare(47,23);held=detail::SMAAPipelineProbe::resources(pipeline);
        }
        // Acquire the surviving device from a real child only after backend
        // and pipeline retirement; a test-owned device must not mask lifetime.
        ComPtr<ID3D11Device> original;held.area->GetDevice(&original);require(original.Get()==identity,"SMAA GPU child lost its original device identity");
        ComPtr<ID3D11DeviceContext> context;original->GetImmediateContext(&context);
        const auto area=readTexture(original.Get(),context.Get(),held.area.Get(),2);
        require(area.size()==AREATEX_SIZE&&std::memcmp(area.data(),areaTexBytes,area.size())==0,"SMAA retained GPU resource lost its device/data after pipeline/backend retirement");
        descriptor(held.edges.Get(),original.Get(),47,23,DXGI_FORMAT_R8G8_UNORM,D3D11_USAGE_DEFAULT,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
    }
};
}
int main(int argc,char** argv) {
    try {
        require(argc==1||(argc==2&&std::strcmp(argv[1],"--hardware")==0),"SMAA fixture accepts only --hardware");
        const bool software=argc==1;NativeAntialiasingProbe::lazyResources(software);NativeAntialiasingProbe::run(software);NativeAntialiasingProbe::lifetime(software);
        std::printf("SMAA contracts passed %zu checks\n",checks);return 0;
    }catch(const std::exception& error) {std::fprintf(stderr,"SMAA failed after %zu checks: %s\n",checks,error.what());return 1;}
}
