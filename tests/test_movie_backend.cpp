#include "renderer/native_backend.h"
#include "renderer/device_availability.h"
#include <d3d11sdklayers.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <thread>

using namespace Simpsons::Graphics;
namespace {
size_t checks{};
int maximumSpatialError{};
void need(bool value,const char* why) {++checks;if(!value) throw Error(why);}
void checked(HRESULT hr,const char* why) {need(SUCCEEDED(hr),why);}
float bits(uint32_t value) {return std::bit_cast<float>(value);}
uint32_t pixel(const std::vector<uint8_t>& bytes,size_t index) {
    uint32_t result{};std::memcpy(&result,bytes.data()+index*4,4);return result;
}
// Independent CPU transcription of analyze_movie_shader.evaluate_samples:
// record82152B68, size21C, SHA256
// 48d052096755186f10dc040a2e0718544f2dea5692f59b6e2ca5a9435351deee.
// /fp:strict keeps the decoded ADD/DP2ADD/DP3 products and sums separate.
std::array<float,4> oracle(float y,float cr,float cb) {
    y+=bits(0xBD800000);cr+=bits(0xBF000000);cb+=bits(0xBF000000);
    const float ry=y*bits(0x3F950A81),rc=cr*bits(0x3FCC4A9D);
    const float gc=cr*bits(0xBF501EAC),gb=cb*bits(0xBEC89507),gy=y*bits(0x3F950A81);
    const float by=y*bits(0x3F950A81),bc=cb*bits(0x40011A54);
    const float r=ry+rc,gcgb=gc+gb,b=by+bc;
    return {r+0.0f,gcgb+gy,b+0.0f,0.0f};
}
uint32_t pack(const std::array<float,4>& color) {
    uint32_t result{};
    for(uint32_t c=0;c<4;++c)
        result|=uint32_t(std::nearbyint(std::clamp(color[c],0.0f,1.0f)*float(c==3?3:1023)))<<(c*10);
    return result;
}
void pinnedOracle() {
    struct Fixture {std::array<uint8_t,3> bytes;std::array<uint32_t,4> output;};
    // Literal independent numeric_fixtures from the original record inspector.
    const Fixture fixtures[]={
        {{16,128,128},{0x3B5FCC97,0xBB081BEA,0x3B8AF67C,0}},
        {{235,128,128},{0x3F806FE2,0x3F7F77DC,0x3F808AF2,0}},
        {{0,0,0},{0xBF5EEBED,0x3F079348,0xBF8A6AFC,0}},
        {{255,255,255},{0x3FF1DF28,0x3EFA7E34,0x40066A16,0}},
        {{93,211,37},{0x3F5FDF7F,0x3E661B40,0xBEBA62F4,0}}};
    for(const auto& f:fixtures) {
        const auto value=oracle(float(f.bytes[0])/255,float(f.bytes[1])/255,float(f.bytes[2])/255);
        for(size_t c=0;c<4;++c) need(std::bit_cast<uint32_t>(value[c])==f.output[c],"CPU oracle differs from original inspector fixture");
    }
    need(pack(oracle(16.0f/255,128.0f/255,128.0f/255))==0x00400003,"First native movie pixel was forced to black/opaque");
}
MovieDraw rectangle() {
    MovieDraw draw{};draw.vertices={ScreenVertex{-1,-1,0,0},ScreenVertex{1,-1,1,0},
        ScreenVertex{-1,1,0,1},ScreenVertex{1,1,1,1}};
    draw.retainedDepthWrite=true;draw.retainedDepthCompare=6;
    for(auto& s:draw.samplers) {
        s.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        s.MaxAnisotropy=1;s.ComparisonFunc=D3D11_COMPARISON_NEVER;s.MaxLOD=13;
    }
    return draw;
}
using Planes=std::array<std::vector<uint8_t>,3>;
void upload(NativeBackend& backend,MovieDraw& draw,uint32_t width,uint32_t height,const Planes& planes,bool writable=false) {
    for(size_t i=0;i<3;++i) {
        const uint32_t w=i?width/2:width,h=i?height/2:height;
        if(writable) {draw.textures[i]=backend.createWritableTexture(w,h,TextureFormat::R8);backend.writeTexture(draw.textures[i],planes[i]);}
        else draw.textures[i]=backend.createTexture(w,h,TextureFormat::R8,planes[i]);
    }
}
Planes uniformPlanes(uint32_t w,uint32_t h,std::array<uint8_t,3> codes) {
    return {std::vector<uint8_t>(size_t(w)*h,codes[0]),std::vector<uint8_t>(size_t(w/2)*(h/2),codes[1]),
        std::vector<uint8_t>(size_t(w/2)*(h/2),codes[2])};
}
float sample(const std::vector<uint8_t>& plane,uint32_t width,uint32_t height,float u,float v,bool wrap) {
    // D3D11 functional spec 3.2.4 / 7.18: texel-space coordinates snap
    // to fixed point before filter weights. The declared native reference
    // uses 8 fractional bits and nearest-even, not unsnapped float bilinear.
    const float px=std::nearbyint((u*float(width)-0.5f)*256.0f)/256.0f;
    const float py=std::nearbyint((v*float(height)-0.5f)*256.0f)/256.0f;
    const int x=int(std::floor(px)),y=int(std::floor(py));const float fx=px-float(x),fy=py-float(y);
    auto at=[&](int sx,int sy) {
        auto address=[&](int value,int extent){return wrap?(value%extent+extent)%extent:std::clamp(value,0,extent-1);};
        return float(plane[size_t(address(sy,int(height)))*width+size_t(address(sx,int(width)))])/255.0f;
    };
    const float lo=at(x,y)*(1-fx)+at(x+1,y)*fx,hi=at(x,y+1)*(1-fx)+at(x+1,y+1)*fx;
    return lo*(1-fy)+hi*fy;
}
void pixels(NativeBackend& backend,const std::shared_ptr<RenderTarget>& target,const MovieDraw& draw,
            uint32_t planeWidth,uint32_t planeHeight,const Planes& planes,bool constantPlanes) {
    const auto bytes=backend.readbackTarget(target);
    for(uint32_t y=0;y<target->height;++y) for(uint32_t x=0;x<target->width;++x) {
        const float tx=(float(x)+0.5f)/float(target->width),ty=1-(float(y)+0.5f)/float(target->height);
        const float u=draw.vertices[0].u+(draw.vertices[1].u-draw.vertices[0].u)*tx;
        const float v=draw.vertices[0].v+(draw.vertices[2].v-draw.vertices[0].v)*ty;
        std::array<float,3> s{};
        for(size_t i=0;i<3;++i) s[i]=sample(planes[i],i?planeWidth/2:planeWidth,i?planeHeight/2:planeHeight,u,v,
            draw.samplers[i].AddressU==D3D11_TEXTURE_ADDRESS_WRAP);
        if(!constantPlanes)for(float value:s)
            need(std::abs(value*255.0f-std::nearbyint(value*255.0f))<0.0001f,"Spatial fixture filtered value is not byte-representable");
        const auto expected=pack(oracle(s[0],s[1],s[2])),actual=pixel(bytes,size_t(y)*target->width+x);
        for(uint32_t c=0;c<4;++c) {
            const uint32_t mask=c==3?3u:1023u;const int difference=std::abs(int((expected>>(10*c))&mask)-int((actual>>(10*c))&mask));
            if(!constantPlanes&&c<3)maximumSpatialError=std::max(maximumSpatialError,difference);
            if(difference!=0) {
                std::fprintf(stderr,"movie pixel %u,%u channel%u got %08X expected %08X\n",x,y,c,actual,expected);
                throw Error("Movie full rectangle/filter/channel/order pixels differ");
            }
            ++checks;
        }
    }
}
// Actual D3D state snapshot, holding every queried reference. Rejections compare
// the complete snapshot; success excludes only the original movie's changes.
struct State {
    std::vector<uint8_t> bytes;
    std::vector<ComPtr<IUnknown>> owners;
    template<class T> void value(const T& v) {
        const auto* first=reinterpret_cast<const uint8_t*>(&v);bytes.insert(bytes.end(),first,first+sizeof(v));
    }
    template<class T> void object(T* p) {
        value(reinterpret_cast<uintptr_t>(p));owners.emplace_back();owners.back().Attach(p);
    }
    explicit State(ID3D11DeviceContext* c,D3D_FEATURE_LEVEL level,bool preservedOnly=false) {
        for(auto getter:{&ID3D11DeviceContext::VSGetShaderResources,&ID3D11DeviceContext::HSGetShaderResources,
            &ID3D11DeviceContext::DSGetShaderResources,&ID3D11DeviceContext::GSGetShaderResources,
            &ID3D11DeviceContext::PSGetShaderResources,&ID3D11DeviceContext::CSGetShaderResources}) {
            std::array<ID3D11ShaderResourceView*,128> a{};const UINT first=preservedOnly&&getter==&ID3D11DeviceContext::PSGetShaderResources?3u:0u;
            (c->*getter)(first,UINT(a.size())-first,a.data());for(UINT i=0;i<UINT(a.size())-first;++i)object(a[i]);
        }
        for(auto getter:{&ID3D11DeviceContext::VSGetSamplers,&ID3D11DeviceContext::HSGetSamplers,&ID3D11DeviceContext::DSGetSamplers,
            &ID3D11DeviceContext::GSGetSamplers,&ID3D11DeviceContext::PSGetSamplers,&ID3D11DeviceContext::CSGetSamplers}) {
            std::array<ID3D11SamplerState*,16> a{};const UINT first=preservedOnly&&getter==&ID3D11DeviceContext::PSGetSamplers?3u:0u;
            (c->*getter)(first,UINT(a.size())-first,a.data());for(UINT i=0;i<UINT(a.size())-first;++i)object(a[i]);
        }
        for(auto getter:{&ID3D11DeviceContext::VSGetConstantBuffers,&ID3D11DeviceContext::HSGetConstantBuffers,
            &ID3D11DeviceContext::DSGetConstantBuffers,&ID3D11DeviceContext::GSGetConstantBuffers,
            &ID3D11DeviceContext::PSGetConstantBuffers,&ID3D11DeviceContext::CSGetConstantBuffers}) {
            std::array<ID3D11Buffer*,14> a{};(c->*getter)(0,UINT(a.size()),a.data());for(auto* p:a)object(p);
        }
        std::array<ID3D11Buffer*,32> streams{};std::array<UINT,32> strides{},offsets{};
        c->IAGetVertexBuffers(0,32,streams.data(),strides.data(),offsets.data());for(auto* p:streams)object(p);value(strides);value(offsets);
        ID3D11Buffer* index{};DXGI_FORMAT format{};UINT offset{};c->IAGetIndexBuffer(&index,&format,&offset);object(index);value(format);value(offset);
        std::array<ID3D11RenderTargetView*,8> targets{};ID3D11DepthStencilView* depth{};
        c->OMGetRenderTargets(8,targets.data(),&depth);for(auto* p:targets)object(p);object(depth);
        std::array<D3D11_VIEWPORT,16> viewports{};UINT count=16;c->RSGetViewports(&count,viewports.data());value(count);value(viewports);
        std::array<D3D11_RECT,16> scissors{};count=16;c->RSGetScissorRects(&count,scissors.data());value(count);value(scissors);
        ID3D11Predicate* predicate{};BOOL condition{};c->GetPredication(&predicate,&condition);object(predicate);value(condition);
        std::array<ID3D11Buffer*,4> so{};c->SOGetTargets(4,so.data());for(auto* p:so)object(p);
        std::array<ID3D11UnorderedAccessView*,64> uavs{};count=level>=D3D_FEATURE_LEVEL_11_1?64:8;
        c->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,count,uavs.data());for(auto* p:uavs)object(p);
        uavs.fill(nullptr);c->CSGetUnorderedAccessViews(0,count,uavs.data());for(auto* p:uavs)object(p);
        if(!preservedOnly) {
            ID3D11VertexShader* vs{};c->VSGetShader(&vs,nullptr,nullptr);object(vs);
            ID3D11PixelShader* ps{};c->PSGetShader(&ps,nullptr,nullptr);object(ps);
            ID3D11GeometryShader* gs{};c->GSGetShader(&gs,nullptr,nullptr);object(gs);
            ID3D11HullShader* hs{};c->HSGetShader(&hs,nullptr,nullptr);object(hs);
            ID3D11DomainShader* ds{};c->DSGetShader(&ds,nullptr,nullptr);object(ds);
            ID3D11InputLayout* layout{};c->IAGetInputLayout(&layout);object(layout);
            D3D11_PRIMITIVE_TOPOLOGY topology{};c->IAGetPrimitiveTopology(&topology);value(topology);
            ID3D11RasterizerState* raster{};c->RSGetState(&raster);object(raster);
            ID3D11BlendState* blend{};std::array<float,4> factors{};UINT mask{};c->OMGetBlendState(&blend,factors.data(),&mask);object(blend);value(factors);value(mask);
            ID3D11DepthStencilState* dsState{};UINT ref{};c->OMGetDepthStencilState(&dsState,&ref);object(dsState);value(ref);
        }
        ID3D11ComputeShader* cs{};c->CSGetShader(&cs,nullptr,nullptr);object(cs);
    }
};
ComPtr<ID3D11Buffer> buffer(ID3D11Device* device,UINT flags,UINT size,const void* data=nullptr) {
    D3D11_BUFFER_DESC d{};d.ByteWidth=size;d.BindFlags=flags;d.Usage=D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA initial{data,0,0};ComPtr<ID3D11Buffer> result;
    checked(device->CreateBuffer(&d,data?&initial:nullptr,&result),"Fixture buffer allocation failed");return result;
}
ComPtr<ID3D11ShaderResourceView> rawPlane(ID3D11Device* device,uint8_t code) {
    D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
    d.Format=DXGI_FORMAT_R8_UNORM;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{&code,1,1};ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;
    checked(device->CreateTexture2D(&d,&initial,&texture),"Fixture plane allocation failed");
    checked(device->CreateShaderResourceView(texture.Get(),nullptr,&view),"Fixture plane SRV failed");return view;
}
void debugMessages(ID3D11Device* device,bool clearOnly=false) {
    ComPtr<ID3D11InfoQueue> messages;if(FAILED(device->QueryInterface(IID_PPV_ARGS(&messages))))return;
    bool failed=false;
    if(!clearOnly)for(UINT64 i=0;i<messages->GetNumStoredMessagesAllowedByRetrievalFilter();++i) {
        SIZE_T size=0;checked(messages->GetMessage(i,nullptr,&size),"D3D debug message size failed");std::vector<uint8_t> storage(size);
        auto* message=reinterpret_cast<D3D11_MESSAGE*>(storage.data());checked(messages->GetMessage(i,message,&size),"D3D debug message read failed");
        if(message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR) {failed=true;std::fprintf(stderr,"D3D: %s\n",message->pDescription);}
    }
    messages->ClearStoredMessages();need(!failed,"Movie produced a D3D debug-layer error");
}
}
namespace Simpsons::Graphics {
struct NativeMovieProbe {
    static ID3D11Device* device(NativeBackend& b) {return b.device.Get();}
    static ID3D11DeviceContext* context(NativeBackend& b) {return b.context.Get();}
    static const void* pipeline(NativeBackend& b) {return b.moviePipeline.get();}
    static void debug(NativeBackend& b,bool hardware) {
        // Optional SDK debug layer, installed on the qualification host. Device
        // replacement occurs before any fixture or backend resource exists.
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        const auto hr=D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_DEBUG,levels,2,D3D11_SDK_VERSION,&device,&level,&context);
        if(SUCCEEDED(hr)){b.availability=std::make_shared<DeviceAvailability>(device.Get());b.device=device;b.context=context;b.featureLevel=level;std::fprintf(stderr,"[MOVIE TEST] D3D11 debug layer enabled\n");}
    }
};
}
namespace {
void postState(ID3D11DeviceContext* c,const MovieDraw& draw) {
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11InputLayout> layout;
    c->VSGetShader(&vs,nullptr,nullptr);c->PSGetShader(&ps,nullptr,nullptr);c->IAGetInputLayout(&layout);
    need(vs&&ps&&layout,"Movie did not retain VS/PS/declaration");
    D3D11_PRIMITIVE_TOPOLOGY topology{};c->IAGetPrimitiveTopology(&topology);
    need(topology==D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,"Movie topology is not a triangle strip");
    for(UINT i=0;i<3;++i) {
        ComPtr<ID3D11ShaderResourceView> texture;c->PSGetShaderResources(i,1,&texture);need(!texture,"Movie retained a plane binding");
        ComPtr<ID3D11SamplerState> sampler;c->PSGetSamplers(i,1,&sampler);need(bool(sampler),"Movie lost a retained sampler");
        // D3D normalizes inactive fields (e.g. non-anisotropic MaxAnisotropy
        // reads back as zero). Compare the actual canonical descriptor.
        ComPtr<ID3D11Device> device;c->GetDevice(&device);ComPtr<ID3D11SamplerState> expectedSampler;
        checked(device->CreateSamplerState(&draw.samplers[i],&expectedSampler),"Expected retained sampler creation failed");
        D3D11_SAMPLER_DESC desc{},expected{};sampler->GetDesc(&desc);expectedSampler->GetDesc(&expected);
        need(!std::memcmp(&desc,&expected,sizeof(desc)),"Movie retained sampler differs from explicit descriptor");
    }
    ComPtr<ID3D11DepthStencilState> depth;UINT ref{};c->OMGetDepthStencilState(&depth,&ref);need(bool(depth),"Missing movie depth state");
    D3D11_DEPTH_STENCIL_DESC dd{};depth->GetDesc(&dd);
    // Disabled depth also canonicalizes the inactive compare to LESS on these
    // devices. Query the expected native object; engine requests stay intact.
    D3D11_DEPTH_STENCIL_DESC requested{};requested.DepthWriteMask=draw.retainedDepthWrite?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;
    requested.DepthFunc=D3D11_COMPARISON_FUNC(draw.retainedDepthCompare+1);
    requested.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};requested.BackFace=requested.FrontFace;
    ComPtr<ID3D11Device> device;c->GetDevice(&device);ComPtr<ID3D11DepthStencilState> expectedDepth;
    checked(device->CreateDepthStencilState(&requested,&expectedDepth),"Expected disabled depth creation failed");
    D3D11_DEPTH_STENCIL_DESC canonical{};expectedDepth->GetDesc(&canonical);
    need(!dd.DepthEnable&&!dd.StencilEnable&&ref==0&&!std::memcmp(&dd,&canonical,sizeof(dd)),"Movie disabled/retained depth state differs");
    ComPtr<ID3D11RasterizerState> raster;c->RSGetState(&raster);need(bool(raster),"Missing movie rasterizer");
    D3D11_RASTERIZER_DESC rd{};raster->GetDesc(&rd);
    need(rd.FillMode==D3D11_FILL_SOLID&&rd.CullMode==D3D11_CULL_NONE&&!rd.ScissorEnable&&!rd.DepthBias&&rd.DepthClipEnable&&
         !rd.SlopeScaledDepthBias&&!rd.MultisampleEnable&&!rd.AntialiasedLineEnable,"Movie raster profile differs");
    ComPtr<ID3D11BlendState> blend;std::array<float,4> factors{};UINT mask{};c->OMGetBlendState(&blend,factors.data(),&mask);
    need(bool(blend),"Missing movie blend state");D3D11_BLEND_DESC bd{};blend->GetDesc(&bd);
    need(!bd.AlphaToCoverageEnable&&!bd.RenderTarget[0].BlendEnable&&bd.RenderTarget[0].RenderTargetWriteMask==15&&mask==0xFFFFFFFF,
         "Movie replace/full-mask state differs");
}
void movieReplacementContracts(bool hardware) {
    NativeBackend backend(!hardware),foreign(!hardware);
    auto* c=NativeMovieProbe::context(backend);
    auto target=backend.createTarget(32,32,TargetFormat::RGB10A2);auto depth=backend.createDepthTarget(32,32);
    backend.bindTargets({target,nullptr,nullptr,nullptr},depth);backend.setViewport({0,0,32,32,0,1});
    backend.clearTarget(target,{1,0,1,1});backend.clearDepthTarget(depth,.5f,0x6D);
    auto draw=rectangle();upload(backend,draw,8,10,uniformPlanes(8,10,{93,211,37}));
    const auto reject=[&](auto&& action,const char* prefix) {
        const State before(c,backend.level());const auto movies=backend.movieDrawCount(),screens=backend.screenDrawCount();
        bool rejected=false;
        try {action();}catch(const Error& error) {
            rejected=true;
            if(std::string(error.what()).find(prefix)!=0) {
                std::fprintf(stderr,"Movie receipt expected '%s', actual '%s'\n",prefix,error.what());
                need(false,"Movie receipt rejected for the wrong reason");
            }
        }
        if(!rejected)std::fprintf(stderr,"Movie receipt unexpectedly accepted negative '%s'\n",prefix);
        need(rejected,"Invalid movie receipt was accepted");
        need(State(c,backend.level()).bytes==before.bytes&&backend.movieDrawCount()==movies&&backend.screenDrawCount()==screens,
             "Movie receipt rejection changed actual native state or counts");
    };
    reject([&]{backend.completedMovieReplacement(0);},"Native movie replacement has no single completed original frame");
    reject([&]{backend.requireScreenReplacement({});},"Native screen replacement receipt is stale or foreign");
    auto before=backend.movieDrawCount();backend.drawMovie(target,depth,draw);
    auto receipt=backend.completedMovieReplacement(before);backend.requireScreenReplacement(receipt);
    reject([&]{foreign.requireScreenReplacement(receipt);},"Native screen replacement receipt is stale or foreign");
    reject([&]{backend.completedMovieReplacement(before+1);},"Native movie replacement has no single completed original frame");
    reject([&]{backend.completedMovieReplacement(UINT64_MAX);},"Native movie replacement has no single completed original frame");
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;ComPtr<ID3D11InputLayout> layout;
    c->VSGetShader(&vertex,nullptr,nullptr);c->PSGetShader(&pixel,nullptr,nullptr);c->IAGetInputLayout(&layout);
    c->VSSetShader(nullptr,nullptr,0);
    reject([&]{backend.requireScreenReplacement(receipt);},"Actual native movie replacement bindings differ");
    c->VSSetShader(vertex.Get(),nullptr,0);c->PSSetShader(nullptr,nullptr,0);
    reject([&]{backend.requireScreenReplacement(receipt);},"Actual native movie replacement bindings differ");
    c->PSSetShader(pixel.Get(),nullptr,0);c->IASetInputLayout(nullptr);
    reject([&]{backend.requireScreenReplacement(receipt);},"Actual native movie replacement layout differs");
    c->IASetInputLayout(layout.Get());backend.requireScreenReplacement(receipt);
    // Invalid frame preparation cannot consume a previously completed receipt.
    auto bad=draw;bad.samplers[2].MipLODBias=1;
    reject([&]{backend.drawMovie(target,depth,bad);},"Native movie sampler filter/LOD/comparison is unsupported");
    backend.requireScreenReplacement(receipt);
    const auto batch=backend.finishSpriteBatch(false);backend.requireScreenBatchRetirement(batch);
    backend.requireScreenReplacement(receipt); // Only the genuine batch may retire the layout.
    before=backend.movieDrawCount();backend.drawMovie(target,depth,draw);
    reject([&]{backend.requireScreenReplacement(receipt);},"Native screen replacement receipt is stale or foreign");
    receipt=backend.completedMovieReplacement(before);backend.requireScreenReplacement(receipt);
    // A second completed draw cannot be attributed to the preceding frame.
    reject([&]{backend.completedMovieReplacement(before-1);},"Native movie replacement has no single completed original frame");
    ScreenDraw screen{};screen.vertices=draw.vertices;screen.color={0,0,0,1};screen.colorWriteMask=15;
    backend.drawScreen(target,screen);
    // Restoring raw pointers cannot resurrect the old epoch after a real bind.
    c->VSSetShader(vertex.Get(),nullptr,0);c->PSSetShader(pixel.Get(),nullptr,0);c->IASetInputLayout(layout.Get());
    reject([&]{backend.requireScreenReplacement(receipt);},"Native screen replacement receipt is stale or foreign");
    reject([&]{backend.completedMovieReplacement(before);},"Native movie replacement has no single completed original frame");
    backend.bindTargets({target,nullptr,nullptr,nullptr},depth);backend.setViewport({0,0,32,32,0,1});
    before=backend.movieDrawCount();backend.drawMovie(target,depth,draw);receipt=backend.completedMovieReplacement(before);
    backend.resetEngineBindings(target,depth);
    c->VSSetShader(vertex.Get(),nullptr,0);c->PSSetShader(pixel.Get(),nullptr,0);c->IASetInputLayout(layout.Get());
    reject([&]{backend.requireScreenReplacement(receipt);},"Native screen replacement receipt is stale or foreign");
    before=backend.movieDrawCount();backend.drawMovie(target,depth,draw);receipt=backend.completedMovieReplacement(before);
    backend.retireScreenReplacement(receipt);
    reject([&]{backend.requireScreenReplacement(receipt);},"Native screen replacement receipt is stale or foreign");
    reject([&]{backend.completedMovieReplacement(before);},"Native movie replacement has no single completed original frame");
    backend.waitIdle();std::printf("PASS movie replacement: completed frame, retained original pair/layout, foreign/stale/corrupt negatives on %s\n",hardware?"hardware":"WARP");
}
int run(bool hardware) {
    pinnedOracle();NativeBackend backend(!hardware);NativeMovieProbe::debug(backend,hardware);auto* device=NativeMovieProbe::device(backend);auto* c=NativeMovieProbe::context(backend);
    // Ten/five plane rows and 32 output rows give dyadic texel-space sample
    // positions for full V and the original .1/.9 crop after D3D snapping.
    // This avoids comparing two legal adapter rounding choices near a 1/256
    // boundary. It still tests both strip triangles and every output pixel.
    constexpr uint32_t width=32,height=32;
    auto target=backend.createTarget(width,height,TargetFormat::RGB10A2);auto depth=backend.createDepthTarget(width,height);
    backend.bindTargets({target,nullptr,nullptr,nullptr},depth);backend.setViewport({0,0,float(width),float(height),0,1});
    backend.clearTarget(target,{0.25f,0.5f,0.75f,1});backend.clearDepthTarget(depth,0.5f,0xA5);
    const auto originalDepth=backend.readbackDepthTarget(depth);
    const std::array<D3D11_RECT,2> scissors={D3D11_RECT{3,5,9,11},D3D11_RECT{1,2,17,19}};c->RSSetScissorRects(2,scissors.data());
    c->SetPredication(nullptr,TRUE);
    // Deliberately non-movie stream layout/offset plus other slots. A following
    // raw Draw later consumes this exact restored stream with no stream setter.
    std::array<uint8_t,160> streamBytes{};const auto vertices=rectangle().vertices;
    for(size_t i=0;i<4;++i)std::memcpy(streamBytes.data()+16+32*i,&vertices[i],sizeof(ScreenVertex));
    auto stream=buffer(device,D3D11_BIND_VERTEX_BUFFER,UINT(streamBytes.size()),streamBytes.data());
    auto* streamPtr=stream.Get();UINT stride=32,offset=16;c->IASetVertexBuffers(0,1,&streamPtr,&stride,&offset);
    c->IASetVertexBuffers(1,1,&streamPtr,&stride,&offset);c->IASetVertexBuffers(31,1,&streamPtr,&stride,&offset);
    auto index=buffer(device,D3D11_BIND_INDEX_BUFFER,64);c->IASetIndexBuffer(index.Get(),DXGI_FORMAT_R16_UINT,6);
    std::array<uint32_t,16> hostileConstants{};hostileConstants.fill(0x7FC00000);
    auto constants=buffer(device,D3D11_BIND_CONSTANT_BUFFER,64,hostileConstants.data());auto* cb=constants.Get();
    for(auto setter:{&ID3D11DeviceContext::VSSetConstantBuffers,&ID3D11DeviceContext::HSSetConstantBuffers,&ID3D11DeviceContext::DSSetConstantBuffers,
        &ID3D11DeviceContext::GSSetConstantBuffers,&ID3D11DeviceContext::PSSetConstantBuffers,&ID3D11DeviceContext::CSSetConstantBuffers}) {
        (c->*setter)(0,1,&cb);(c->*setter)(13,1,&cb);
    }
    auto sentinel=rawPlane(device,173);auto* srv=sentinel.Get();
    for(auto setter:{&ID3D11DeviceContext::VSSetShaderResources,&ID3D11DeviceContext::HSSetShaderResources,&ID3D11DeviceContext::DSSetShaderResources,
        &ID3D11DeviceContext::GSSetShaderResources,&ID3D11DeviceContext::PSSetShaderResources,&ID3D11DeviceContext::CSSetShaderResources}) {
        (c->*setter)(0,1,&srv);(c->*setter)(9,1,&srv);(c->*setter)(127,1,&srv);
    }
    auto draw=rectangle();ComPtr<ID3D11SamplerState> sentinelSampler;auto sentinelDesc=draw.samplers[0];sentinelDesc.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    checked(device->CreateSamplerState(&sentinelDesc,&sentinelSampler),"Fixture sampler creation failed");auto* ss=sentinelSampler.Get();
    for(auto setter:{&ID3D11DeviceContext::VSSetSamplers,&ID3D11DeviceContext::HSSetSamplers,&ID3D11DeviceContext::DSSetSamplers,
        &ID3D11DeviceContext::GSSetSamplers,&ID3D11DeviceContext::PSSetSamplers,&ID3D11DeviceContext::CSSetSamplers}) {
        (c->*setter)(0,1,&ss);(c->*setter)(15,1,&ss);
    }
    auto planes=uniformPlanes(8,10,{16,128,128});upload(backend,draw,8,10,planes);
    auto reject=[&](auto&& action) {
        const State before(c,backend.level());const auto count=backend.movieDrawCount();const auto* pipeline=NativeMovieProbe::pipeline(backend);
        ComPtr<ID3D11Predicate> predicate;BOOL condition{};c->GetPredication(&predicate,&condition);
        if(predicate)c->SetPredication(nullptr,FALSE);
        const auto beforePixels=backend.readbackTarget(target);const auto beforeDepth=backend.readbackDepthTarget(depth);
        if(predicate)c->SetPredication(predicate.Get(),condition);
        bool rejected=false;try{action();}catch(const Error&){rejected=true;}
        need(rejected,"Invalid movie call was accepted");
        need(State(c,backend.level()).bytes==before.bytes,"Rejected movie changed actual D3D state");
        need(backend.movieDrawCount()==count&&NativeMovieProbe::pipeline(backend)==pipeline,"Rejected movie changed draw count/pipeline ownership");
        if(predicate)c->SetPredication(nullptr,FALSE);
        need(backend.readbackTarget(target)==beforePixels,"Rejected movie changed target pixels");
        need(backend.readbackDepthTarget(depth)==beforeDepth,"Rejected movie changed depth/stencil pixels");
        if(predicate)c->SetPredication(predicate.Get(),condition);
    };
    // Cold rejection proves validation happens before pipeline publication.
    auto invalid=draw;invalid.samplers[2].MipLODBias=1;reject([&]{backend.drawMovie(target,depth,invalid);});
    need(!NativeMovieProbe::pipeline(backend),"Cold validation published movie pipeline");
    auto queue=[&] {
        const State before(c,backend.level(),true);const auto count=backend.movieDrawCount();
        debugMessages(device,true);
        backend.drawMovie(target,depth,draw);need(backend.movieDrawCount()==count+1,"Movie did not count exactly one actual Draw");
        debugMessages(device);
        need(State(c,backend.level(),true).bytes==before.bytes,"Movie changed preserved bindings/stream/viewport/scissor");postState(c,draw);
        backend.requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);
    };
    auto submit=[&] {queue();need(backend.readbackDepthTarget(depth)==originalDepth,"Disabled movie depth/stencil modified storage");};
    D3D11_QUERY_DESC qd{D3D11_QUERY_PIPELINE_STATISTICS,0};ComPtr<ID3D11Query> stats;
    checked(device->CreateQuery(&qd,&stats),"Pipeline statistics query creation failed");c->Begin(stats.Get());submit();c->End(stats.Get());backend.waitIdle();
    D3D11_QUERY_DATA_PIPELINE_STATISTICS stat{};checked(c->GetData(stats.Get(),&stat,sizeof(stat),0),"Movie pipeline statistics failed");
    need(stat.IAVertices==4&&stat.IAPrimitives==2&&stat.VSInvocations==4&&stat.PSInvocations>=width*height,
         "Movie did not submit a real four-vertex full rectangle");
    pixels(backend,target,draw,8,10,planes,true);const auto* cached=NativeMovieProbe::pipeline(backend);
    for(const auto codes:{std::array<uint8_t,3>{235,128,128},{0,0,0},{255,255,255},{93,211,37},{93,37,211}}) {
        planes=uniformPlanes(8,10,codes);upload(backend,draw,8,10,planes);submit();pixels(backend,target,draw,8,10,planes,true);
    }
    planes=uniformPlanes(8,10,{0,0,0});
    // Make filtered values representable in R8 as well as filter coordinates:
    // each plane is an asymmetric separable sum. Luma deltas are multiples32,
    // chroma deltas multiples64, cancelling every fixture weight denominator
    // including the .125/.875 U crop. Thus minimum-format filter precision
    // cannot introduce the arbitrary between-byte rounding of a random plane.
    const std::array<uint8_t,8> lumaX={0,32,96,64,32,96,0,64};
    const std::array<uint8_t,10> lumaY={0,32,64,96,32,0,96,64,32,0};
    const std::array<uint8_t,4> crX={0,64,0,64},cbX={64,64,0,0};
    const std::array<uint8_t,5> crY={64,0,64,0,64},cbY={0,64,64,0,64};
    for(size_t i=0;i<3;++i) {
        const uint32_t w=i?4:8,h=i?5:10;
        for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)
            planes[i][size_t(y)*w+x]=uint8_t(i==0?16+lumaX[x]+lumaY[y]:(i==1?24+crX[x]+crY[y]:88+cbX[x]+cbY[y]));
    }
    for(uint32_t profile=0;profile<3;++profile) {
        draw=rectangle();if(profile==1){draw.vertices[0].u=draw.vertices[2].u=0.125f;draw.vertices[1].u=draw.vertices[3].u=0.875f;}
        if(profile==2){draw.vertices[0].v=draw.vertices[1].v=bits(0x3DCCCCCD);draw.vertices[2].v=draw.vertices[3].v=bits(0x3F666666);}
        upload(backend,draw,8,10,planes,true);
        for(size_t i=0;i<3;++i)need(backend.readback(draw.textures[i])==planes[i],"Asymmetric plane upload differs");
        submit();
        pixels(backend,target,draw,8,10,planes,false);
    }
    // Each stage has an independent sampler; exercise wrap against clamp at
    // upsampled edge texels and linear-mip retention with only one mip.
    draw=rectangle();upload(backend,draw,8,10,planes);
    for(size_t stage=0;stage<3;++stage) {
        auto& s=draw.samplers[stage];s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.BorderColor[0]=float(stage)/3;submit();pixels(backend,target,draw,8,10,planes,false);
    }
    // Queued input lifetimes and changing writable planes across submissions.
    draw=rectangle();upload(backend,draw,8,10,planes,true);auto savedDraw=draw;const Planes savedPlanes=planes;
    queue();std::array<std::weak_ptr<Texture>,3> weak={draw.textures[0],draw.textures[1],draw.textures[2]};
    draw.vertices={};draw.textures={};savedDraw.textures={};planes={};
    for(const auto& p:weak)need(p.expired(),"Movie backend retained a caller plane owner after submission");
    pixels(backend,target,savedDraw,8,10,savedPlanes,false);
    draw=rectangle();planes=uniformPlanes(8,10,{16,128,128});upload(backend,draw,8,10,planes,true);queue();
    auto previous=backend.createTarget(width,height,TargetFormat::RGB10A2);backend.copyFront(target,previous);
    planes=uniformPlanes(8,10,{93,211,37});for(size_t i=0;i<3;++i)backend.writeTexture(draw.textures[i],planes[i]);queue();
    pixels(backend,target,draw,8,10,planes,true);pixels(backend,previous,draw,8,10,uniformPlanes(8,10,{16,128,128}),true);
    need(NativeMovieProbe::pipeline(backend)==cached,"Movie pipeline was not reused");

    // Retained PS must return float (including negative and >1 values), and a
    // later stream cache hit must consume the restored IA buffer/stride/offset.
    auto floatTarget=backend.createTarget(width,height,TargetFormat::RGBA32Float);
    backend.bindTargets({floatTarget,nullptr,nullptr,nullptr},depth);
    std::array<ComPtr<ID3D11ShaderResourceView>,3> raw={rawPlane(device,93),rawPlane(device,211),rawPlane(device,37)};
    std::array<ID3D11ShaderResourceView*,3> rawViews={raw[0].Get(),raw[1].Get(),raw[2].Get()};c->PSSetShaderResources(0,3,rawViews.data());
    debugMessages(device,true);c->Draw(4,0);debugMessages(device);
    const auto floatPixels=backend.readbackTarget(floatTarget);const auto expectedFloat=oracle(93.0f/255,211.0f/255,37.0f/255);
    for(size_t i=0;i<floatPixels.size()/16;++i)for(size_t component=0;component<4;++component) {
        float actual{};std::memcpy(&actual,floatPixels.data()+i*16+component*4,4);
        need(std::isfinite(actual)&&std::abs(actual-expectedFloat[component])<0.000001f,"Retained movie PS/stream/constant independence failed");
        if(component==3)need(std::bit_cast<uint32_t>(actual)==0,"Retained movie alpha is not positive zero");
    }
    rawViews.fill(nullptr);c->PSSetShaderResources(0,3,rawViews.data());backend.bindTargets({target,nullptr,nullptr,nullptr},depth);
    for(uint32_t compare=0;compare<8;++compare) {draw.retainedDepthCompare=compare;draw.retainedDepthWrite=(compare&1)!=0;submit();}
    auto bad=[&](auto&& change) {auto d=draw;change(d);reject([&]{backend.drawMovie(target,depth,d);});};
    bad([](auto& d){d.retainedDepthCompare=8;});bad([](auto& d){d.vertices[3].x=0;});bad([](auto& d){d.vertices[2].v=0.5f;});
    bad([](auto& d){std::swap(d.vertices[1],d.vertices[2]);});
    for(size_t i=0;i<4;++i)for(uint32_t field=0;field<4;++field)bad([&](auto& d){
        auto& v=d.vertices[i];if(field==0)v.x=NAN;else if(field==1)v.y=INFINITY;else if(field==2)v.u=NAN;else v.v=INFINITY;});
    for(size_t i=0;i<3;++i) {
        bad([&](auto& d){d.textures[i].reset();});bad([&](auto& d){d.samplers[i].Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;});
        bad([&](auto& d){d.samplers[i].AddressU=D3D11_TEXTURE_ADDRESS_MIRROR;});bad([&](auto& d){d.samplers[i].AddressV=D3D11_TEXTURE_ADDRESS_BORDER;});
        bad([&](auto& d){d.samplers[i].AddressW=D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;});bad([&](auto& d){d.samplers[i].MinLOD=1;});
        bad([&](auto& d){d.samplers[i].MaxLOD=-1;});bad([&](auto& d){d.samplers[i].MaxLOD=NAN;});bad([&](auto& d){d.samplers[i].MipLODBias=INFINITY;});
        bad([&](auto& d){d.samplers[i].MaxAnisotropy=2;});bad([&](auto& d){d.samplers[i].ComparisonFunc=D3D11_COMPARISON_ALWAYS;});
        bad([&](auto& d){d.samplers[i].BorderColor[3]=NAN;});
    }
    bad([](auto& d){d.textures[2]=d.textures[1];});
    const auto widthBefore=draw.textures[1]->width;draw.textures[1]->width=99;reject([&]{backend.drawMovie(target,depth,draw);});draw.textures[1]->width=widthBefore;
    auto wrongPlane=backend.createTexture(2,2,TextureFormat::R8,std::array<uint8_t,4>{1,2,3,4});bad([&](auto& d){d.textures[1]=wrongPlane;});
    auto rgba=backend.createTexture(1,1,TextureFormat::RGBA8,std::array<uint8_t,4>{1,2,3,4});
    for(size_t i=0;i<3;++i)bad([&](auto& d){d.textures[i]=rgba;});
    const std::array<uint8_t,8> mip0{};const std::array<uint8_t,4> mip1{};const std::array<std::span<const uint8_t>,2> mipLevels={mip0,mip1};
    auto mipped=backend.createTextureMipChain(2,1,TextureFormat::RGBA8,mipLevels);bad([&](auto& d){d.textures[0]=mipped;});
    reject([&]{backend.drawMovie({},depth,draw);});reject([&]{backend.drawMovie(target,{},draw);});
    reject([&]{backend.drawMovie(std::make_shared<RenderTarget>(),depth,draw);});reject([&]{backend.drawMovie(target,std::make_shared<DepthTarget>(),draw);});
    auto otherTarget=backend.createTarget(width,height,TargetFormat::RGB10A2);auto otherDepth=backend.createDepthTarget(width,height);
    auto smallDepth=backend.createDepthTarget(width/2,height/2);
    reject([&]{backend.drawMovie(otherTarget,depth,draw);});reject([&]{backend.drawMovie(target,otherDepth,draw);});reject([&]{backend.drawMovie(target,smallDepth,draw);});
    auto wrongTarget=backend.createTarget(width,height,TargetFormat::RGBA8);wrongTarget->format=TargetFormat::RGB10A2;
    reject([&]{backend.drawMovie(wrongTarget,depth,draw);});wrongTarget->format=TargetFormat::RGBA8;
    backend.clearTarget(wrongTarget,{1,0.5f,0.25f,1});const auto wrongTargetPixels=backend.readbackTarget(wrongTarget);
    backend.bindTargets({wrongTarget,nullptr,nullptr,nullptr},depth);
    reject([&]{backend.drawMovie(wrongTarget,depth,draw);});wrongTarget->format=TargetFormat::RGB10A2;
    reject([&]{backend.drawMovie(wrongTarget,depth,draw);});wrongTarget->format=TargetFormat::RGBA8;
    need(backend.readbackTarget(wrongTarget)==wrongTargetPixels,"Rejected selected nonpacked target changed storage");
    backend.bindTargets({target,nullptr,nullptr,nullptr},depth);
    target->width=width+1;reject([&]{backend.drawMovie(target,depth,draw);});target->width=width;
    NativeBackend foreign(!hardware);auto foreignTarget=foreign.createTarget(width,height,TargetFormat::RGB10A2);auto foreignDepth=foreign.createDepthTarget(width,height);
    auto foreignPlane=foreign.createTexture(4,5,TextureFormat::R8,std::vector<uint8_t>(20,128));
    reject([&]{backend.drawMovie(foreignTarget,depth,draw);});reject([&]{backend.drawMovie(target,foreignDepth,draw);});
    for(size_t i=0;i<3;++i)bad([&](auto& d){d.textures[i]=foreignPlane;});
    backend.bindTargets({target,otherTarget,nullptr,nullptr},depth);reject([&]{backend.drawMovie(target,depth,draw);});
    backend.bindTargets({target,nullptr,nullptr,nullptr},{});reject([&]{backend.drawMovie(target,{},draw);});backend.bindTargets({target,nullptr,nullptr,nullptr},depth);
    const auto vp=*backend.viewport();
    for(UINT field=0;field<6;++field) {
        auto changed=vp;if(field==0)changed.TopLeftX=1;else if(field==1)changed.TopLeftY=1;else if(field==2)changed.Width-=1;
        else if(field==3)changed.Height-=1;else if(field==4)changed.MinDepth=0.25f;else changed.MaxDepth=0.75f;
        backend.setViewport(changed);reject([&]{backend.drawMovie(target,depth,draw);});
    }
    c->RSSetViewports(0,nullptr);reject([&]{backend.drawMovie(target,depth,draw);});
    const std::array<D3D11_VIEWPORT,2> vps={vp,vp};c->RSSetViewports(2,vps.data());reject([&]{backend.drawMovie(target,depth,draw);});backend.setViewport(vp);
    D3D11_QUERY_DESC pd{D3D11_QUERY_OCCLUSION_PREDICATE,0};ComPtr<ID3D11Predicate> predicate;
    checked(device->CreatePredicate(&pd,&predicate),"Predicate fixture allocation failed");c->SetPredication(predicate.Get(),TRUE);
    reject([&]{backend.drawMovie(target,depth,draw);});c->SetPredication(nullptr,TRUE);
    auto so=buffer(device,D3D11_BIND_STREAM_OUTPUT,64);auto* soPtr=so.Get();UINT zero=0;
    c->SOSetTargets(1,&soPtr,&zero);reject([&]{backend.drawMovie(target,depth,draw);});c->SOSetTargets(0,nullptr,nullptr);
    D3D11_TEXTURE2D_DESC ud{};ud.Width=ud.Height=ud.MipLevels=ud.ArraySize=ud.SampleDesc.Count=1;ud.Format=DXGI_FORMAT_R32_UINT;
    ud.Usage=D3D11_USAGE_DEFAULT;ud.BindFlags=D3D11_BIND_UNORDERED_ACCESS;ComPtr<ID3D11Texture2D> ut;ComPtr<ID3D11UnorderedAccessView> uav;
    checked(device->CreateTexture2D(&ud,nullptr,&ut),"UAV fixture allocation failed");checked(device->CreateUnorderedAccessView(ut.Get(),nullptr,&uav),"UAV fixture view failed");
    for(UINT slot:{1u,backend.level()>=D3D_FEATURE_LEVEL_11_1?63u:7u}) {
        auto* p=uav.Get();c->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,slot,1,&p,nullptr);
        reject([&]{backend.drawMovie(target,depth,draw);});p=nullptr;
        c->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,slot,1,&p,nullptr);
    }
    auto* uavPtr=uav.Get();c->CSSetUnorderedAccessViews(0,1,&uavPtr,nullptr);submit();uavPtr=nullptr;c->CSSetUnorderedAccessViews(0,1,&uavPtr,nullptr);
    reject([&]{bool failed=false;std::thread t([&]{try{backend.drawMovie(target,depth,draw);}catch(const Error&){failed=true;}});t.join();if(failed)throw Error("Expected owner rejection");});
    // Also preserve an absent actual stream0 exactly.
    streamPtr=nullptr;stride=offset=0;c->IASetVertexBuffers(0,1,&streamPtr,&stride,&offset);submit();
    // Live boot dimensions exercise the complete 1280x720 output and 640x360
    // chroma resources, including the lower/right triangle and alpha zero.
    auto liveTarget=backend.createTarget(1280,720,TargetFormat::RGB10A2);auto liveDepth=backend.createDepthTarget(1280,720);
    backend.bindTargets({liveTarget,nullptr,nullptr,nullptr},liveDepth);backend.setViewport({0,0,1280,720,0,1});
    backend.clearTarget(liveTarget,{1,0,1,1});backend.clearDepthTarget(liveDepth,0.5f,0x6D);
    auto live=rectangle();const auto livePlanes=uniformPlanes(1280,720,{16,128,128});upload(backend,live,1280,720,livePlanes);
    debugMessages(device,true);backend.drawMovie(liveTarget,liveDepth,live);debugMessages(device);pixels(backend,liveTarget,live,1280,720,livePlanes,true);
    need(!backend.screenDrawCount()&&!backend.im2dDrawCount()&&!backend.presentationCount(),"Movie changed another backend counter");
    movieReplacementContracts(hardware);
    backend.waitIdle();std::printf("PASS: %zu movie backend checks on %s; maximum spatial RGB error=%d codes; native GPU/packing policy, console pixel parity unverified\n",checks,hardware?"hardware":"WARP",maximumSpatialError);return 0;
}
}
int main(int argc,char** argv) {
    try {need(argc==1||(argc==2&&std::string(argv[1])=="--hardware"),"Expected optional --hardware");return run(argc==2);}
    catch(const std::exception& error){std::fprintf(stderr,"FAIL after %zu checks: %s\n",checks,error.what());return 1;}
}
