#pragma once
#include "renderer/im2d_draw.h"
#include <new>
// Include at global scope after require/rejects in test_native_graphics.cpp.
// NativeBackend declares: friend struct EngineBindingResetProbe;
// Call engineBindingResetContracts(hardware). No public context accessor is added.

namespace Simpsons::Graphics {
struct EngineBindingResetProbe {
    struct Snapshot {
        std::vector<uintptr_t> retained,changed;
        template<class T> static void pointer(std::vector<uintptr_t>& out,T* value) {
            out.push_back(reinterpret_cast<uintptr_t>(value));
            if(value) value->Release(); // Get* supplied this reference.
        }
        template<class T,size_t N,class Get> void slots(Get get,bool pixel=false) {
            std::array<T*,N> values{};get(values.data());
            for(size_t i=0;i<N;++i) pointer(pixel && i<8?changed:retained,values[i]);
        }
        template<class T,class Get> void shader(Get get,bool reset) {
            T* value{};std::array<ID3D11ClassInstance*,D3D11_SHADER_MAX_INTERFACES> instances{};UINT count=UINT(instances.size());
            get(&value,instances.data(),&count);
            auto& destination=reset?changed:retained;
            pointer(destination,value);destination.push_back(count);
            for(UINT i=0;i<count;++i) pointer(destination,instances[i]);
        }
        void bytes(const void* source,size_t size) {
            auto* data=static_cast<const uint8_t*>(source);
            for(size_t i=0;i<size;++i) retained.push_back(data[i]);
        }
        explicit Snapshot(ID3D11DeviceContext* context) {
#define RESET_TEST_STAGE(PREFIX,PIXEL,RESET_SHADER,SHADER_TYPE) \
            slots<ID3D11ShaderResourceView,128>([&](auto p){context->PREFIX##GetShaderResources(0,128,p);},PIXEL); \
            slots<ID3D11SamplerState,16>([&](auto p){context->PREFIX##GetSamplers(0,16,p);}); \
            slots<ID3D11Buffer,14>([&](auto p){context->PREFIX##GetConstantBuffers(0,14,p);}); \
            shader<SHADER_TYPE>([&](auto p,auto c,auto n){context->PREFIX##GetShader(p,c,n);},RESET_SHADER)
            RESET_TEST_STAGE(PS,true,true,ID3D11PixelShader);
            RESET_TEST_STAGE(VS,false,true,ID3D11VertexShader);
            RESET_TEST_STAGE(GS,false,false,ID3D11GeometryShader);
            RESET_TEST_STAGE(HS,false,false,ID3D11HullShader);
            RESET_TEST_STAGE(DS,false,false,ID3D11DomainShader);
            RESET_TEST_STAGE(CS,false,false,ID3D11ComputeShader);
#undef RESET_TEST_STAGE
            std::array<ID3D11Buffer*,32> vertices{};std::array<UINT,32> strides{},offsets{};
            context->IAGetVertexBuffers(0,32,vertices.data(),strides.data(),offsets.data());
            for(size_t i=0;i<32;++i) {
                auto& destination=i<4?changed:retained;
                pointer(destination,vertices[i]);destination.push_back(strides[i]);destination.push_back(offsets[i]);
            }
            ID3D11Buffer* index{};DXGI_FORMAT format{};UINT offset{};
            context->IAGetIndexBuffer(&index,&format,&offset);pointer(changed,index);
            changed.push_back(format);changed.push_back(offset);
            ID3D11InputLayout* layout{};context->IAGetInputLayout(&layout);pointer(changed,layout);
            D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);retained.push_back(topology);
            std::array<ID3D11RenderTargetView*,8> colors{};ID3D11DepthStencilView* depth{};
            context->OMGetRenderTargets(8,colors.data(),&depth);
            for(auto* color:colors) pointer(changed,color);
            pointer(changed,depth);
            slots<ID3D11UnorderedAccessView,8>([&](auto p){context->CSGetUnorderedAccessViews(0,8,p);});
            slots<ID3D11UnorderedAccessView,8>([&](auto p){context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,8,p);});
            ID3D11BlendState* blend{};float factors[4]{};UINT mask{};
            context->OMGetBlendState(&blend,factors,&mask);pointer(retained,blend);bytes(factors,sizeof(factors));retained.push_back(mask);
            ID3D11DepthStencilState* depthState{};UINT stencil{};
            context->OMGetDepthStencilState(&depthState,&stencil);pointer(retained,depthState);retained.push_back(stencil);
            ID3D11RasterizerState* raster{};context->RSGetState(&raster);pointer(retained,raster);
            std::array<D3D11_VIEWPORT,16> viewports{};UINT count=16;
            context->RSGetViewports(&count,viewports.data());retained.push_back(count);bytes(viewports.data(),count*sizeof(viewports[0]));
            std::array<D3D11_RECT,16> scissors{};count=16;
            context->RSGetScissorRects(&count,scissors.data());retained.push_back(count);bytes(scissors.data(),count*sizeof(scissors[0]));
        }
        bool operator==(const Snapshot& other) const {return retained==other.retained && changed==other.changed;}
    };
    static void checked(HRESULT result,const char* operation) {require(SUCCEEDED(result),operation);}
    template<class F> static void rejectedUnchanged(ID3D11DeviceContext* context,F action,const char* prefix) {
        const Snapshot before(context);bool precise=false;
        rejects([&] {
            try {action();std::fprintf(stderr,"Reset rejection unexpectedly accepted: expected='%s'\n",prefix);}
            catch(const Error& error) {precise=std::string(error.what()).starts_with(prefix);
                if(!precise)std::fprintf(stderr,"Reset rejection mismatch: expected='%s' actual='%s'\n",prefix,error.what());
                throw;}
        });
        require(precise,"Binding reset rejected for the wrong reason");
        require(Snapshot(context)==before,"Rejected binding reset mutated native bindings");
    }
    static void receiptContracts(bool hardware) {
        NativeBackend backend(!hardware),foreign(!hardware);auto* context=backend.context.Get();
        auto color=backend.createTarget(32,16,TargetFormat::RGB10A2);
        auto depth=backend.createDepthTarget(32,16);
        backend.bindTargets({color,nullptr,nullptr,nullptr},depth);
        backend.setViewport({0,0,32,16,0,1});backend.clearTarget(color,{0,0,0,1});
        ScreenDraw screen{};screen.vertices={{{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,-1,1,1}}};
        screen.color={0,0,0,1};screen.blendSelector=3;screen.colorWriteMask=15;
        backend.drawScreen(color,screen);
        ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;
        context->VSGetShader(&vertex,nullptr,nullptr);context->PSGetShader(&pixel,nullptr,nullptr);
        require(vertex&&pixel,"Reset receipt fixture did not create real shaders");
        auto receipt=backend.resetEngineBindings(color,depth);
        backend.requireBindingReset(receipt);
        rejectedUnchanged(context,[&]{backend.requireBindingReset({});},"Native binding reset receipt is stale or foreign");
        rejectedUnchanged(foreign.context.Get(),[&]{foreign.requireBindingReset(receipt);},"Native binding reset receipt is stale or foreign");
        context->VSSetShader(vertex.Get(),nullptr,0);
        rejectedUnchanged(context,[&]{backend.requireBindingReset(receipt);},"Actual native shaders differ");
        context->VSSetShader(nullptr,nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);
        rejectedUnchanged(context,[&]{backend.requireBindingReset(receipt);},"Actual native shaders differ");
        context->PSSetShader(nullptr,nullptr,0);backend.requireBindingReset(receipt);
        auto next=backend.resetEngineBindings(color,depth);
        rejectedUnchanged(context,[&]{backend.requireBindingReset(receipt);},"Native binding reset receipt is stale or foreign");
        backend.requireBindingReset(next);
        // A genuine subsequent native shader bind invalidates the epoch even
        // if someone manually clears the physical pair back to null afterward.
        backend.drawScreen(color,screen);context->VSSetShader(nullptr,nullptr,0);context->PSSetShader(nullptr,nullptr,0);
        rejectedUnchanged(context,[&]{backend.requireBindingReset(next);},"Native binding reset receipt is stale or foreign");
        receipt=backend.resetEngineBindings(color,depth);
        Im2DDraw ui{};ui.primitiveType=4;ui.rasterWidth=32;ui.rasterHeight=16;
        ui.blendWord=0x00010001;ui.colorWriteMask=15;ui.pixelCenterHalf=true;
        ui.vertices={{{4.5f,4.5f,0,1},{1,0,0,1},{0,0}},{{4.5f,12.5f,0,1},{1,0,0,1},{0,1}},
                     {{12.5f,4.5f,0,1},{1,0,0,1},{1,0}},{{12.5f,12.5f,0,1},{1,0,0,1},{1,1}}};
        const auto before=backend.readbackTarget(color);const auto draws=backend.im2dDrawCount();
        backend.queueIm2D(color,depth,ui);require(bool(backend.pendingIm2D),"Reset receipt fixture did not queue real UI work");
        rejectedUnchanged(context,[&]{backend.requireBindingReset(receipt);},"Native binding reset receipt has unfinished Im2D work");
        // Reset is an ordering boundary: queued pixels reach the real target
        // before a new completed reset is issued, and retirement cannot drop it.
        next=backend.resetEngineBindings(color,depth);
        require(!backend.pendingIm2D&&backend.im2dDrawCount()==draws+1&&backend.readbackTarget(color)!=before,
                "Binding reset discarded pending native UI work or its pixels");
        rejectedUnchanged(context,[&]{backend.requireBindingReset(receipt);},"Native binding reset receipt is stale or foreign");
        backend.requireBindingReset(next);backend.retireBindingReset(next);
        rejectedUnchanged(context,[&]{backend.requireBindingReset(next);},"Native binding reset receipt is stale or foreign");

        // Deliberately recreate a backend at exactly the same address with the
        // same fresh serial/epoch. The retained COM device prevents a null-pair
        // receipt from becoming valid through address reuse (ABA).
        struct Slot {
            alignas(NativeBackend) unsigned char bytes[sizeof(NativeBackend)];NativeBackend* live{};
            ~Slot(){if(live)live->~NativeBackend();}
            NativeBackend& create(bool warp){if(live){live->~NativeBackend();live=nullptr;}
                live=new(bytes) NativeBackend(warp);return *live;}
        } slot;
        NativeBindingResetReceipt destroyed;
        ID3D11Device* firstDevice{};uint64_t serial{},epoch{};
        {
            auto& old=slot.create(!hardware);auto c=old.createTarget(8,8,TargetFormat::RGB10A2);auto d=old.createDepthTarget(8,8);
            destroyed=old.resetEngineBindings(c,d);firstDevice=old.device.Get();serial=old.bindingResetSerial;epoch=old.screenShaderEpoch;
        }
        auto& replacement=slot.create(!hardware);
        auto c=replacement.createTarget(8,8,TargetFormat::RGB10A2);auto d=replacement.createDepthTarget(8,8);
        const auto fresh=replacement.resetEngineBindings(c,d);
        require(replacement.device.Get()!=firstDevice&&replacement.bindingResetSerial==serial&&replacement.screenShaderEpoch==epoch,
                "Same-address receipt fixture failed to reproduce coincident reset counters");
        replacement.requireBindingReset(fresh);
        rejectedUnchanged(replacement.context.Get(),[&]{replacement.requireBindingReset(destroyed);},"Native binding reset receipt is stale or foreign");
    }
    static void coronaReceiptContracts(bool hardware) {
        NativeBackend backend(!hardware),foreign(!hardware);auto* context=backend.context.Get();
        auto scene=backend.createTarget(128,72,TargetFormat::RGB10A2),backup=backend.createTarget(64,64,TargetFormat::RGB10A2),
            query=backend.createTarget(64,8,TargetFormat::RGB10A2);
        auto depth=backend.createDepthTarget(128,72),sampled=backend.createDepthTarget(128,72);
        backend.bindTargets({scene,nullptr,nullptr,nullptr},depth);backend.setViewport({0,0,128,72,0,1});
        backend.clearTarget(scene,{.25f,.5f,.75f,1});backend.clearDepthTarget(depth,0,37);backend.clearDepthTarget(sampled,0,0);
        const auto pixels=backend.readbackTarget(scene),depthPixels=backend.readbackDepthTarget(depth);
        const std::array<CoronaVertex,1> vertices={{{{.5f/128,.5f/72},{64,36,16,8},{1.0f/128,1.0f/72,.5f}}}};
        rejectedUnchanged(context,[&]{backend.completedCoronaQueryReplacement(backend.coronaQueryDrawCount());},
            "Native corona replacement has no single completed original query");
        auto before=backend.coronaQueryDrawCount();
        backend.drawCoronaQueries(scene,depth,sampled,backup,query,vertices,6);
        auto receipt=backend.completedCoronaQueryReplacement(before);backend.requireScreenReplacement(receipt);
        require(backend.coronaQueryDrawCount()==before+1&&backend.screenDrawCount()==0&&
            backend.readbackTarget(scene)==pixels&&backend.readbackDepthTarget(depth)==depthPixels,
            "Corona replacement fabricated a screen draw or changed scene/depth pixels");
        rejectedUnchanged(context,[&]{backend.requireScreenReplacement({});},"Native screen replacement receipt is stale or foreign");
        rejectedUnchanged(foreign.context.Get(),[&]{foreign.requireScreenReplacement(receipt);},"Native screen replacement receipt is stale or foreign");
        ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;ComPtr<ID3D11InputLayout> layout;
        context->VSGetShader(&vertex,nullptr,nullptr);context->PSGetShader(&pixel,nullptr,nullptr);context->IAGetInputLayout(&layout);
        context->VSSetShader(nullptr,nullptr,0);
        rejectedUnchanged(context,[&]{backend.requireScreenReplacement(receipt);},"Actual native screen replacement bindings differ");
        context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(nullptr,nullptr,0);
        rejectedUnchanged(context,[&]{backend.requireScreenReplacement(receipt);},"Actual native screen replacement bindings differ");
        context->PSSetShader(pixel.Get(),nullptr,0);context->IASetInputLayout(nullptr);
        rejectedUnchanged(context,[&]{backend.requireScreenReplacement(receipt);},"Actual native corona replacement layout differs");
        context->IASetInputLayout(layout.Get());backend.requireScreenReplacement(receipt);
        rejectedUnchanged(context,[&]{backend.drawCoronaQueries(scene,depth,sampled,backup,query,{},6);},"Corona query extent/format/ownership differs");
        backend.requireScreenReplacement(receipt);
        const auto batch=backend.finishSpriteBatch(false);backend.requireScreenBatchRetirement(batch);backend.requireScreenReplacement(receipt);
        before=backend.coronaQueryDrawCount();backend.drawCoronaQueries(scene,depth,sampled,backup,query,vertices,6);
        rejectedUnchanged(context,[&]{backend.requireScreenReplacement(receipt);},"Native screen replacement receipt is stale or foreign");
        receipt=backend.completedCoronaQueryReplacement(before);backend.requireScreenReplacement(receipt);
        rejectedUnchanged(context,[&]{backend.completedCoronaQueryReplacement(before-1);},"Native corona replacement has no single completed original query");
        ScreenDraw screen{};screen.vertices={{{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,-1,1,1}}};
        screen.color={0,0,0,1};screen.blendSelector=3;screen.colorWriteMask=15;backend.drawScreen(scene,screen);
        // Restoring the same COM objects manually cannot revive its old epoch.
        context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);context->IASetInputLayout(layout.Get());
        rejectedUnchanged(context,[&]{backend.requireScreenReplacement(receipt);},"Native screen replacement receipt is stale or foreign");
        rejectedUnchanged(context,[&]{backend.completedCoronaQueryReplacement(before);},"Native corona replacement has no single completed original query");
        // Generic drawScreen intentionally releases its output attachments.
        // Select the next query's real scene roles before its target guard.
        backend.bindTargets({scene,nullptr,nullptr,nullptr},depth);backend.setViewport({0,0,128,72,0,1});
        before=backend.coronaQueryDrawCount();backend.drawCoronaQueries(scene,depth,sampled,backup,query,vertices,6);
        receipt=backend.completedCoronaQueryReplacement(before);backend.resetEngineBindings(scene,depth);
        rejectedUnchanged(context,[&]{backend.requireScreenReplacement(receipt);},"Native screen replacement receipt is stale or foreign");
    }
    static void nullBindings(ID3D11DeviceContext* context,ID3D11RenderTargetView* color,ID3D11DepthStencilView* depth) {
        for(UINT slot=0;slot<8;++slot) {
            ComPtr<ID3D11ShaderResourceView> view;context->PSGetShaderResources(slot,1,&view);
            require(!view,"Binding reset left an engine PS texture bound");
        }
        for(UINT slot=0;slot<4;++slot) {
            ComPtr<ID3D11Buffer> buffer;UINT stride=1,offset=1;
            context->IAGetVertexBuffers(slot,1,&buffer,&stride,&offset);
            require(!buffer && !stride && !offset,"Binding reset left an engine vertex stream bound");
        }
        ComPtr<ID3D11Buffer> index;DXGI_FORMAT format{};UINT offset=1;
        context->IAGetIndexBuffer(&index,&format,&offset);
        require(!index && format==DXGI_FORMAT_UNKNOWN && !offset,"Binding reset left an index binding");
        ComPtr<ID3D11PixelShader> ps;context->PSGetShader(&ps,nullptr,nullptr);
        ComPtr<ID3D11VertexShader> vs;context->VSGetShader(&vs,nullptr,nullptr);
        ComPtr<ID3D11InputLayout> layout;context->IAGetInputLayout(&layout);
        require(!ps && !vs && !layout,"Binding reset left an engine shader or declaration bound");
        std::array<ID3D11RenderTargetView*,8> targets{};ComPtr<ID3D11DepthStencilView> actualDepth;
        context->OMGetRenderTargets(8,targets.data(),&actualDepth);
        bool matched=targets[0]==color && actualDepth.Get()==depth;
        for(size_t i=0;i<targets.size();++i) {
            if(i) matched=matched && !targets[i];
            if(targets[i]) targets[i]->Release();
        }
        require(matched,"Binding reset did not select only the owned default attachments");
    }
    static void run(bool hardware) {
        NativeBackend backend(!hardware),foreign(!hardware);
        auto* device=backend.device.Get();auto* context=backend.context.Get();
        constexpr uint32_t width=32,height=16;
        auto color=backend.createTarget(width,height,TargetFormat::RGB10A2);
        auto depth=backend.createDepthTarget(width,height);
        std::array<std::shared_ptr<RenderTarget>,4> alternatives{};
        for(auto& target:alternatives) target=backend.createTarget(width,height,TargetFormat::RGB10A2);
        auto alternateDepth=backend.createDepthTarget(width,height);
        backend.bindTargets({color,nullptr,nullptr,nullptr},depth);
        const Snapshot withoutViewport(context);
        backend.resetEngineBindings(color,depth);
        UINT viewportCount=0;context->RSGetViewports(&viewportCount,nullptr);
        require(viewportCount==0,"Binding reset invented a viewport before native camera mapping");
        require(Snapshot(context).retained==withoutViewport.retained,"Empty-state reset changed an unrelated binding");
        ComPtr<ID3D11RenderTargetView> colorView;ComPtr<ID3D11DepthStencilView> depthView;
        context->OMGetRenderTargets(1,&colorView,&depthView);
        ComPtr<ID3D11Resource> colorResource,depthResource;
        colorView->GetResource(&colorResource);depthView->GetResource(&depthResource);
        ComPtr<ID3D11ShaderResourceView> colorRead,depthRead;
        checked(device->CreateShaderResourceView(colorResource.Get(),nullptr,&colorRead),"Reset test color SRV creation failed");
        D3D11_SHADER_RESOURCE_VIEW_DESC depthDesc{};depthDesc.Format=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        depthDesc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;depthDesc.Texture2D.MipLevels=1;
        checked(device->CreateShaderResourceView(depthResource.Get(),&depthDesc,&depthRead),"Reset test depth SRV creation failed");

        // The existing real screen pipeline supplies nonnull VS, PS and layout.
        // This is a synthetic fixture draw, not an original game frame.
        ScreenDraw draw{};draw.vertices={{{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,-1,1,1}}};
        draw.color={0,0,0,1};draw.blendSelector=3;draw.colorWriteMask=15;
        backend.drawScreen(color,draw);
        ComPtr<ID3D11PixelShader> seededPS;context->PSGetShader(&seededPS,nullptr,nullptr);
        ComPtr<ID3D11VertexShader> seededVS;context->VSGetShader(&seededVS,nullptr,nullptr);
        ComPtr<ID3D11InputLayout> seededLayout;context->IAGetInputLayout(&seededLayout);
        require(seededPS && seededVS && seededLayout,"Reset fixture did not seed real shader/declaration bindings");
        backend.bindTargets(alternatives,alternateDepth);

        std::array<uint32_t,64> payload{};for(size_t i=0;i<payload.size();++i) payload[i]=uint32_t(0x10203040+i);
        D3D11_BUFFER_DESC bufferDesc{};bufferDesc.ByteWidth=sizeof(payload);bufferDesc.Usage=D3D11_USAGE_DEFAULT;
        bufferDesc.BindFlags=D3D11_BIND_VERTEX_BUFFER|D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{payload.data(),0,0};ComPtr<ID3D11Buffer> vertex;
        checked(device->CreateBuffer(&bufferDesc,&data,&vertex),"Reset test vertex/index allocation failed");
        std::array<ID3D11Buffer*,5> vertices{};vertices.fill(vertex.Get());
        std::array<UINT,5> strides{16,20,24,28,32},offsets{0,4,8,12,16};
        context->IASetVertexBuffers(0,5,vertices.data(),strides.data(),offsets.data());
        auto* vertexPointer=vertex.Get();context->IASetVertexBuffers(31,1,&vertexPointer,&strides[4],&offsets[4]);
        context->IASetIndexBuffer(vertex.Get(),DXGI_FORMAT_R16_UINT,8);
        bufferDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> constant;
        checked(device->CreateBuffer(&bufferDesc,&data,&constant),"Reset test constant allocation failed");
        D3D11_TEXTURE2D_DESC textureDesc{};textureDesc.Width=4;textureDesc.Height=4;textureDesc.MipLevels=1;textureDesc.ArraySize=1;
        textureDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;textureDesc.SampleDesc.Count=1;
        textureDesc.Usage=D3D11_USAGE_IMMUTABLE;textureDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA textureData{payload.data(),16,64};ComPtr<ID3D11Texture2D> texture;
        checked(device->CreateTexture2D(&textureDesc,&textureData,&texture),"Reset test sampled allocation failed");
        ComPtr<ID3D11ShaderResourceView> sampled;
        checked(device->CreateShaderResourceView(texture.Get(),nullptr,&sampled),"Reset test sampled view failed");
        D3D11_SAMPLER_DESC samplerDesc{};samplerDesc.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDesc.AddressU=samplerDesc.AddressV=samplerDesc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxAnisotropy=1;samplerDesc.ComparisonFunc=D3D11_COMPARISON_ALWAYS;samplerDesc.MaxLOD=D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        checked(device->CreateSamplerState(&samplerDesc,&sampler),"Reset test sampler creation failed");
        std::array<ID3D11ShaderResourceView*,128> reads{};reads.fill(sampled.Get());
        std::array<ID3D11SamplerState*,16> samplers{};samplers.fill(sampler.Get());
        std::array<ID3D11Buffer*,14> constants{};constants.fill(constant.Get());
#define RESET_TEST_SEED(PREFIX) \
        context->PREFIX##SetShaderResources(0,128,reads.data()); \
        context->PREFIX##SetSamplers(0,16,samplers.data()); \
        context->PREFIX##SetConstantBuffers(0,14,constants.data())
        RESET_TEST_SEED(PS);RESET_TEST_SEED(VS);RESET_TEST_SEED(GS);
        RESET_TEST_SEED(HS);RESET_TEST_SEED(DS);RESET_TEST_SEED(CS);
#undef RESET_TEST_SEED
        // Release local COM ownership: retained slots, not these variables, must
        // keep the actual GPU texture/buffers/sampler alive across the reset.
        sampled.Reset();texture.Reset();constant.Reset();vertex.Reset();sampler.Reset();
        std::array<D3D11_VIEWPORT,2> viewports{{{1.5f,2.5f,18,9,0.25f,0.75f},{0,0,8,8,0,1}}};
        std::array<D3D11_RECT,2> scissors{{{2,3,11,15},{0,1,5,6}}};
        context->RSSetViewports(2,viewports.data());context->RSSetScissorRects(2,scissors.data());
        ComPtr<ID3D11BlendState> blend;context->OMGetBlendState(&blend,nullptr,nullptr);
        float factors[4]{0.125f,0.25f,0.5f,1};context->OMSetBlendState(blend.Get(),factors,0x5A5A5A5A);
        ComPtr<ID3D11DepthStencilState> depthState;context->OMGetDepthStencilState(&depthState,nullptr);
        context->OMSetDepthStencilState(depthState.Get(),0x73);

        const auto reset=[&]{backend.resetEngineBindings(color,depth);};
        auto foreignColor=foreign.createTarget(width,height,TargetFormat::RGB10A2);
        auto foreignDepth=foreign.createDepthTarget(width,height);
        auto smallDepth=backend.createDepthTarget(width-1,height);
        rejectedUnchanged(context,[&]{backend.resetEngineBindings(nullptr,depth);},"Native binding reset requires");
        rejectedUnchanged(context,[&]{backend.resetEngineBindings(color,nullptr);},"Native binding reset requires");
        rejectedUnchanged(context,[&]{backend.resetEngineBindings(std::make_shared<RenderTarget>(),depth);},"Native binding reset requires");
        rejectedUnchanged(context,[&]{backend.resetEngineBindings(color,std::make_shared<DepthTarget>());},"Native binding reset requires");
        rejectedUnchanged(context,[&]{backend.resetEngineBindings(foreignColor,depth);},"Native binding reset target/view belongs");
        rejectedUnchanged(context,[&]{backend.resetEngineBindings(color,foreignDepth);},"Native binding reset target/view belongs");
        rejectedUnchanged(context,[&]{backend.resetEngineBindings(color,smallDepth);},"Native binding reset attachment dimensions differ");
        color->width=width+1;
        // Logical extent is separate from the retained physical texture size.
        rejectedUnchanged(context,reset,"Native binding reset attachment dimensions differ");color->width=width;
        const Snapshot beforeThread(context);std::atomic<bool> wrongThread=false;
        std::thread other([&]{try {reset();} catch(const Error& error) {
            wrongThread=std::string(error.what()).starts_with("Native graphics immediate context used from a different thread");
        }});other.join();require(wrongThread,"Binding reset accepted another owner thread");
        require(Snapshot(context)==beforeThread,"Foreign-thread reset changed actual context bindings");

        // The selector-1 null-raster boundary clears exactly one PS slot. Seed
        // every stage anew so each case has seven live low-slot neighbours as
        // well as live PS 8..127, other shader stages, buffers and attachments.
        backend.clearTarget(alternatives[0],{0.25f,0.5f,0.75f,1});
        backend.clearDepthTarget(alternateDepth,0.5f,0xA7);
        const auto clearColorBefore=backend.readbackTarget(alternatives[0]);
        const auto clearDepthBefore=backend.readbackDepthTarget(alternateDepth);
        const auto clearDraws=backend.screenDrawCount(),clearPresents=backend.presentationCount();
        for(uint32_t stage=0;stage<8;++stage) {
            context->PSSetShaderResources(0,128,reads.data());
            Snapshot expected(context);
            require(expected.changed.size()>=8 && expected.changed[stage]!=0,"Texture-clear test did not seed its selected PS slot");
            expected.changed[stage]=0; // First eight entries are precisely PS SRVs 0..7.
            backend.clearEngineTexture(stage);
            require(Snapshot(context)==expected,"Single texture clear changed another binding or failed to clear its stage");
            backend.clearEngineTexture(stage);
            require(Snapshot(context)==expected,"Repeated single texture clear changed context state");
        }
        context->PSSetShaderResources(0,128,reads.data());
        for(uint32_t stage:{8u,15u,16u,127u,128u,0xFFFFFFFFu})
            rejectedUnchanged(context,[&]{backend.clearEngineTexture(stage);},"Native engine texture clear stage is outside");
        const Snapshot beforeTextureThread(context);std::atomic<bool> textureThreadRejected=false;
        std::thread textureThread([&]{try {backend.clearEngineTexture(0);} catch(const Error& error) {
            textureThreadRejected=std::string(error.what()).starts_with("Native graphics immediate context used from a different thread");
        }});textureThread.join();
        require(textureThreadRejected,"Single texture clear accepted another owner thread");
        require(Snapshot(context)==beforeTextureThread,"Foreign-thread texture clear changed native bindings");
        require(backend.readbackTarget(alternatives[0])==clearColorBefore &&
                backend.readbackDepthTarget(alternateDepth)==clearDepthBefore,"Single texture clear changed attachment contents");
        require(backend.screenDrawCount()==clearDraws && backend.presentationCount()==clearPresents,
                "Single texture clear submitted a draw/presentation");

        const auto ownedBytes=std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(payload.data()),64);
        auto ownedRead=backend.createTexture(4,4,TextureFormat::RGBA8,ownedBytes);
        auto foreignRead=foreign.createTexture(4,4,TextureFormat::RGBA8,ownedBytes);
        for(uint32_t stage=0;stage<8;++stage) {
            context->PSSetShaderResources(0,128,reads.data());
            const Snapshot beforeBind(context);
            backend.bindEngineTexture(stage,ownedRead);
            ComPtr<ID3D11ShaderResourceView> actual;context->PSGetShaderResources(stage,1,&actual);
            require(bool(actual),"Owned texture bind left a null native view");
            Snapshot expectedBind=beforeBind;
            expectedBind.changed[stage]=reinterpret_cast<uintptr_t>(actual.Get());
            require(Snapshot(context)==expectedBind,"Owned texture bind changed a neighboring slot or another context state");
            backend.requireEngineTexture(stage,ownedRead);
            backend.bindEngineTexture(stage,ownedRead);
            require(Snapshot(context)==expectedBind,"Repeated owned texture bind was not idempotent");
            rejectedUnchanged(context,[&]{backend.requireEngineTexture(stage,nullptr);},"Actual native engine texture binding differs");
            backend.clearEngineTexture(stage);backend.requireEngineTexture(stage,nullptr);
        }
        for(uint32_t stage:{8u,15u,127u,0xFFFFFFFFu})
            rejectedUnchanged(context,[&]{backend.bindEngineTexture(stage,ownedRead);},"Native engine texture stage is outside");
        rejectedUnchanged(context,[&]{backend.bindEngineTexture(0,nullptr);},"Missing native texture resource/view");
        rejectedUnchanged(context,[&]{backend.bindEngineTexture(0,foreignRead);},"Native resource belongs to another graphics device");
        ownedRead->width=8;
        rejectedUnchanged(context,[&]{backend.bindEngineTexture(0,ownedRead);},"Native texture metadata does not match its backing");
        ownedRead->width=4;
        const Snapshot beforeBindThread(context);std::atomic<bool> bindThreadRejected=false;
        std::thread bindThread([&]{try {backend.bindEngineTexture(0,ownedRead);} catch(const Error& error) {
            bindThreadRejected=std::string(error.what()).starts_with("Native graphics immediate context used from a different thread");
        }});bindThread.join();
        require(bindThreadRejected && Snapshot(context)==beforeBindThread,"Foreign-thread owned texture bind changed graphics state");
        require(backend.readback(ownedRead)==std::vector<uint8_t>(ownedBytes.begin(),ownedBytes.end()) &&
                backend.readbackTarget(alternatives[0])==clearColorBefore &&
                backend.readbackDepthTarget(alternateDepth)==clearDepthBefore,"Owned texture binding changed resource contents");
        require(backend.screenDrawCount()==clearDraws && backend.presentationCount()==clearPresents,
                "Owned texture binding submitted a draw/presentation");

        std::array<ID3D11RenderTargetView*,5> tooManyColors{};
        context->OMGetRenderTargets(4,tooManyColors.data(),nullptr);
        tooManyColors[4]=colorView.Get();
        context->OMSetRenderTargets(5,tooManyColors.data(),nullptr);
        for(size_t i=0;i<4;++i) if(tooManyColors[i]) tooManyColors[i]->Release();
        rejectedUnchanged(context,reset,"Native binding reset does not own render-target slots above three");
        backend.bindTargets(alternatives,alternateDepth);

        // Only the first eight PS reads may conflict with the proposed outputs.
        // Every other stage and high slot must reject *before* clearing shaders,
        // buffers or earlier texture slots.
        auto hazard=[&](auto setter,UINT slot,ID3D11ShaderResourceView* view) {
            (context->*setter)(slot,1,&view);
            rejectedUnchanged(context,reset,"Native binding reset would unbind a preserved shader resource");
            auto* ordinary=reads[0];(context->*setter)(slot,1,&ordinary);
        };
        hazard(&ID3D11DeviceContext::PSSetShaderResources,8,colorRead.Get());
        hazard(&ID3D11DeviceContext::PSSetShaderResources,127,depthRead.Get());
        hazard(&ID3D11DeviceContext::VSSetShaderResources,0,colorRead.Get());
        hazard(&ID3D11DeviceContext::GSSetShaderResources,15,colorRead.Get());
        hazard(&ID3D11DeviceContext::HSSetShaderResources,15,depthRead.Get());
        hazard(&ID3D11DeviceContext::DSSetShaderResources,15,depthRead.Get());
        hazard(&ID3D11DeviceContext::CSSetShaderResources,127,colorRead.Get());

        // Real counter UAVs prove the target rebind preserves other OM/CS
        // bindings and their hidden append/counter values.
        bufferDesc.ByteWidth=64;bufferDesc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bufferDesc.StructureByteStride=4;
        std::array<ComPtr<ID3D11UnorderedAccessView>,2> uavs;
        for(auto& uav:uavs) {
            ComPtr<ID3D11Buffer> buffer;checked(device->CreateBuffer(&bufferDesc,nullptr,&buffer),"Reset test UAV buffer failed");
            D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};desc.Format=DXGI_FORMAT_UNKNOWN;desc.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;
            desc.Buffer.NumElements=16;desc.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_COUNTER;
            checked(device->CreateUnorderedAccessView(buffer.Get(),&desc,&uav),"Reset test counter UAV failed");
        }
        UINT initial=7;auto* outputUav=uavs[0].Get();
        context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,0,1,&outputUav,&initial);
        rejectedUnchanged(context,reset,"Native binding reset would unbind a preserved output UAV");
        ID3D11UnorderedAccessView* noUav{};
        context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,0,1,&noUav,nullptr);
        backend.bindTargets(alternatives,alternateDepth);
        context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,4,1,&outputUav,&initial);
        initial=9;auto* computeUav=uavs[1].Get();context->CSSetUnorderedAccessViews(0,1,&computeUav,&initial);
        auto* allowedColor=colorRead.Get();auto* allowedDepth=depthRead.Get();
        context->PSSetShaderResources(0,1,&allowedColor);context->PSSetShaderResources(7,1,&allowedDepth);
        const Snapshot before(context);const auto colorOwners=color.use_count(),depthOwners=depth.use_count();
        const auto draws=backend.screenDrawCount(),presents=backend.presentationCount();
        reset();nullBindings(context,colorView.Get(),depthView.Get());
        require(Snapshot(context).retained==before.retained,"Narrow reset changed an unaffected native binding");
        require(color.use_count()==colorOwners && depth.use_count()==depthOwners,"Binding reset changed logical resource ownership");
        require(backend.screenDrawCount()==draws && backend.presentationCount()==presents,"Binding reset submitted a draw/presentation");
        const Snapshot once(context);reset();require(Snapshot(context)==once,"Repeated narrow reset was not idempotent");

        // A real color-zero change has the original conditional viewport and
        // effective scissor consequence. Other retained slots/counters survive.
        const D3D11_VIEWPORT restoredViewport{0,0,float(width),float(height),0,1};
        const D3D11_RECT restoredClip{0,0,LONG(width),LONG(height)};
        context->RSSetViewports(1,&restoredViewport);context->RSSetScissorRects(1,&restoredClip);
        const Snapshot expectedRebind(context);
        context->RSSetViewports(2,viewports.data());context->RSSetScissorRects(2,scissors.data());
        backend.bindTargets(alternatives,alternateDepth);
        // bindTargets uses OMSetRenderTargets, which cleared the fixture's
        // output UAV. Restore it before the operation under test, keeping count.
        context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,4,1,&outputUav,nullptr);
        backend.requireSelectedTargets(alternatives,alternateDepth);
        rejectedUnchanged(context,[&]{backend.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);},"Actual native target selection differs");
        const auto defaultPixels=backend.readbackTarget(color),privatePixels=backend.readbackTarget(alternatives[0]);
        const auto privateDepthPixels=backend.readbackDepthTarget(alternateDepth);
        backend.resetEngineBindings(color,depth,true);
        const Snapshot actualRebind(context);
        if(!(actualRebind==expectedRebind)) {
            for(size_t i=0;i<actualRebind.retained.size()&&i<expectedRebind.retained.size();++i)
                if(actualRebind.retained[i]!=expectedRebind.retained[i])std::fprintf(stderr,"reset retained[%zu] actual=%llX expected=%llX\n",i,
                    static_cast<unsigned long long>(actualRebind.retained[i]),static_cast<unsigned long long>(expectedRebind.retained[i]));
            for(size_t i=0;i<actualRebind.changed.size()&&i<expectedRebind.changed.size();++i)
                if(actualRebind.changed[i]!=expectedRebind.changed[i])std::fprintf(stderr,"reset changed[%zu] actual=%llX expected=%llX\n",i,
                    static_cast<unsigned long long>(actualRebind.changed[i]),static_cast<unsigned long long>(expectedRebind.changed[i]));
        }
        require(Snapshot(context)==expectedRebind,"Changed-target reset altered unrelated state or missed viewport/effective clip");
        backend.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);
        require(backend.readbackTarget(color)==defaultPixels&&backend.readbackTarget(alternatives[0])==privatePixels&&
                backend.readbackDepthTarget(alternateDepth)==privateDepthPixels,"Target reset changed attachment pixels");
        reset();require(Snapshot(context)==expectedRebind,"Cache-hit reset failed to preserve forward viewport after target change");

        // The sampler, constants, vertex buffer and texture had no local owner.
        // Query and use the retained GPU resources, rather than checking a model.
        ComPtr<ID3D11ShaderResourceView> held;context->PSGetShaderResources(15,1,&held);
        require(bool(held),"Preserved PS resource lost its context-owned lifetime");
        ComPtr<ID3D11Resource> heldResource;held->GetResource(&heldResource);
        textureDesc.Usage=D3D11_USAGE_STAGING;textureDesc.BindFlags=0;textureDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        checked(device->CreateTexture2D(&textureDesc,nullptr,&staging),"Reset test retained texture staging failed");
        context->CopyResource(staging.Get(),heldResource.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        checked(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Reset test retained texture map failed");
        bool equal=true;for(size_t row=0;row<4;++row)
            equal=equal && !std::memcmp(static_cast<const uint8_t*>(mapped.pData)+row*mapped.RowPitch,
                                      reinterpret_cast<const uint8_t*>(payload.data())+row*16,16);
        context->Unmap(staging.Get(),0);require(equal,"Context-retained texture contents changed during reset");
        D3D11_BUFFER_DESC countDesc{};countDesc.ByteWidth=4;countDesc.Usage=D3D11_USAGE_DEFAULT;
        ComPtr<ID3D11Buffer> countBuffer,countRead;
        checked(device->CreateBuffer(&countDesc,nullptr,&countBuffer),"Reset test count buffer failed");
        countDesc.Usage=D3D11_USAGE_STAGING;countDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        checked(device->CreateBuffer(&countDesc,nullptr,&countRead),"Reset test count staging failed");
        for(size_t i=0;i<uavs.size();++i) {
            context->CopyStructureCount(countBuffer.Get(),0,uavs[i].Get());context->CopyResource(countRead.Get(),countBuffer.Get());
            checked(context->Map(countRead.Get(),0,D3D11_MAP_READ,0,&mapped),"Reset test count map failed");
            uint32_t value{};std::memcpy(&value,mapped.pData,4);context->Unmap(countRead.Get(),0);
            require(value==(i?9u:7u),"Binding reset changed a preserved UAV counter");
        }
        // Default target COM lifetime must outlast the caller's CPU wrappers.
        colorRead.Reset();depthRead.Reset();colorResource.Reset();depthResource.Reset();
        colorView.Reset();depthView.Reset();std::weak_ptr<RenderTarget> weakColor=color;
        std::weak_ptr<DepthTarget> weakDepth=depth;color.reset();depth.reset();
        require(weakColor.expired() && weakDepth.expired(),"Reset invented CPU wrapper ownership");
        context->OMGetRenderTargets(1,&colorView,&depthView);
        require(colorView && depthView,"Context did not retain native attachments after wrapper release");
        float clear[4]{0,0,0,1};context->ClearRenderTargetView(colorView.Get(),clear);
        context->ClearDepthStencilView(depthView.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);
        backend.validateSubmissionContext();backend.clearBindings();receiptContracts(hardware);coronaReceiptContracts(hardware);
    }
};
}
inline void engineBindingResetContracts(bool hardware=false) {
    Simpsons::Graphics::EngineBindingResetProbe::run(hardware);
}
