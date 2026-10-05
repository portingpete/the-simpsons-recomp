#include "renderer/im2d_draw.h"
#include "renderer/im2d_layers.h"
#include "renderer/device_availability.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <d3d11sdklayers.h>

using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void need(bool value,const char* why) {++checks;if(!value) throw Error(why);}
uint32_t pixel(const std::vector<uint8_t>& bytes,size_t i) {uint32_t p{};std::memcpy(&p,bytes.data()+i*4,4);return p;}
uint32_t pack(const std::array<float,4>& color) {
    uint32_t result=0;
    for(uint32_t c=0;c<4;++c) result|=uint32_t(std::nearbyint(std::clamp(color[c],0.0f,1.0f)*float(c==3?3:1023)))<<(10*c);
    return result;
}
std::array<float,4> unpack(uint32_t p) {return {float(p&1023)/1023,float((p>>10)&1023)/1023,float((p>>20)&1023)/1023,float(p>>30)/3};}
uint32_t blended(std::array<float,4> source,uint32_t destination,uint32_t word) {
    if(word==0x01000100)return destination;
    const auto d=unpack(destination);auto result=source;
    if(word!=0x00010001) for(size_t c=0;c<(word==0x07060706?4u:3u);++c) {
        const float product=source[c]*source[3];
        if(word==0x00010106) result[c]=product+d[c];
        else if(word==0x00010186) result[c]=d[c]-product;
        else {const float inverse=1.0f-source[3];const float retained=d[c]*inverse;result[c]=product+retained;}
    }
    return pack(result);
}
void uniform(const std::vector<uint8_t>& bytes,uint32_t expected,const char* why) {
    for(size_t i=0;i<bytes.size()/4;++i) {
        if(pixel(bytes,i)!=expected) fprintf(stderr,"pixel %zu got %08X expected %08X\n",i,pixel(bytes,i),expected);
        need(pixel(bytes,i)==expected,why);
    }
}
void color(Im2DDraw& draw,std::array<float,4> value) {for(auto& v:draw.vertices) v.color=value;}
void zValue(Im2DDraw& draw,float z) {for(auto& v:draw.vertices) v.position[2]=z;}
float depth20e4(float z) {
    // Pinned reference Float32To20e4 nearest-even, recovered independently in
    // build/im2d-depth/evidence.json. Decode arithmetically into exact D32.
    if(!(z>0))return 0;
    uint32_t bits=std::bit_cast<uint32_t>(z),code{};
    if(bits>=0x3FFFFFF8u)code=0xFFFFFF;
    else {
        if(bits<0x38800000u)bits=(0x800000u|(bits&0x7FFFFFu))>>std::min(113u-(bits>>23),24u);
        else bits+=0xC8000000u;
        code=((bits+3u+((bits>>3)&1u))>>3)&0xFFFFFFu;
    }
    const uint32_t exponent=code>>20,mantissa=code&0xFFFFFu;
    return std::ldexp(float(mantissa+(exponent?0x100000u:0u)),exponent?int(exponent)-35:-34);
}
uint32_t depthBits(const std::vector<uint8_t>& bytes,size_t i) {
    uint32_t bits{};std::memcpy(&bits,bytes.data()+i*8,4);return bits;
}
void depthPixel(const std::vector<uint8_t>& bytes,size_t i,float expected,uint8_t stencil) {
    const auto bits=std::bit_cast<uint32_t>(expected);
    if(depthBits(bytes,i)!=bits)fprintf(stderr,"depth pixel %zu got %08X expected %08X\n",i,depthBits(bytes,i),bits);
    need(depthBits(bytes,i)==bits,"Native Im2D depth bits differ");
    need(bytes[i*8+4]==stencil,"Native Im2D changed stencil"); // Last 24 bits are undefined padding.
}
void uniformDepth(const std::vector<uint8_t>& bytes,float expected,uint8_t stencil) {
    for(size_t i=0;i<bytes.size()/8;++i)depthPixel(bytes,i,expected,stencil);
}
Im2DDraw quad(uint32_t width=16,uint32_t height=12) {
    Im2DDraw d{};d.primitiveType=4;d.rasterWidth=width;d.rasterHeight=height;d.blendWord=0x00010001;
    d.alphaCompare=4;d.pixelCenterHalf=true;d.colorWriteMask=15;
    d.vertices={{{0.5f,0.5f,0,1},{1,1,1,1},{0,0}},
                {{float(width)+0.5f,0.5f,0,2},{1,1,1,1},{1,0}},
                {{0.5f,float(height)+0.5f,0,3},{1,1,1,1},{0,1}},
                {{float(width)+0.5f,float(height)+0.5f,0,4},{1,1,1,1},{1,1}}};
    d.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    d.sampler.AddressU=d.sampler.AddressV=d.sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    d.sampler.MaxAnisotropy=1;d.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;return d;
}
bool alphaAccept(float a,float ref,uint32_t compare) {
    switch(compare) {case 0:return false;case 1:return a<ref;case 2:return a==ref;case 3:return a<=ref;
    case 4:return a>ref;case 5:return a!=ref;case 6:return a>=ref;default:return true;}
}
// Literal DXT3 blocks in a 12x8 image (three blocks per row). The first
// block uses ascending black/white endpoints: BC2 still has FOUR colors.
// All alpha nibbles and selector rows are asymmetric; the other blocks are
// red/green on row0 and blue/yellow/magenta on row1. This is linear native
// storage, not guest tiled/endian data. No production decoder builds the oracle.
constexpr std::array<uint8_t,96> bc2Blocks={
    0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE,0x00,0x00,0xFF,0xFF,0xE4,0xB1,0x4E,0x1B,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF8,0x00,0x00,0x00,0x00,0x00,0x00,
    0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0xE0,0x07,0x00,0x00,0x00,0x00,0x00,0x00,
    0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0x1F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xE0,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,
    0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x1F,0xF8,0x00,0x00,0x00,0x00,0x00,0x00};
std::array<float,4> bc2Texel(size_t x,size_t y) {
    constexpr std::array<uint8_t,16> gray={0,255,85,170,255,0,170,85,85,170,0,255,170,85,255,0};
    constexpr std::array<std::array<float,4>,6> solids={{{0,0,0,0},{1,0,0,0},
        {0,1,0,1.0f/3},{0,0,1,2.0f/3},{1,1,0,1},{1,0,1,1.0f/5}}};
    if(x<4&&y<4) {
        const size_t index=y*4+x;const float g=float(gray[index])/255;
        return {g,g,g,float(index)/15};
    }
    return solids[(y/4)*3+x/4];
}
// D3D11.3 sections19.5.2/19.5.7 permit RGB decode error strictly below
// 1/255 + .03*endpointRange. Our black/white range is exactly1 both before
// and after promotion: error=173/5100, reference=k/3. Propagate that open
// interval through the fixture's positive diffuse t/8 and explicit UINT
// packing. No endpoint is a rounding threshold for these k/t combinations;
// float32 shader rounding cannot reach another output bin. This interval is
// ONLY for point-sampled derived RGB, never alpha, endpoints or filtering.
// https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm
constexpr std::array<uint32_t,2> bc2InterpolantCodes(uint32_t k,uint32_t tintEighths) {
    const auto encode=[=](uint32_t numerator) {
        constexpr uint32_t denominator=5100*8;
        const uint32_t scaled=numerator*tintEighths*1023;
        const uint32_t integer=scaled/denominator,remainder=scaled%denominator;
        return integer+uint32_t(remainder>denominator/2 || (remainder==denominator/2&&(integer&1)));
    };
    return {encode(k*1700-173),encode(k*1700+173)};
}
static_assert(bc2InterpolantCodes(1,8)==std::array{306u,376u} && bc2InterpolantCodes(2,8)==std::array{647u,717u});
static_assert(bc2InterpolantCodes(1,5)==std::array{191u,235u} && bc2InterpolantCodes(2,5)==std::array{405u,448u});
static_assert(bc2InterpolantCodes(1,3)==std::array{115u,141u} && bc2InterpolantCodes(2,3)==std::array{243u,269u});
static_assert(bc2InterpolantCodes(1,1)==std::array{38u,47u} && bc2InterpolantCodes(2,1)==std::array{81u,90u});
// Four 2x2 quadrants: red/alpha0, blue/alpha1; blue/alpha1, red/alpha0.
// Binary-coordinate samples give ideal 0,1/4,3/8,1/2,3/4,1 weights. Endpoint
// decode is exact; filtered float alpha need not equal the ideal value even
// when packed color does. Keep filter color checks and alpha ties separate.
constexpr std::array<uint8_t,16> bc2FilterBlock={
    0x00,0xFF,0x00,0xFF,0xFF,0x00,0xFF,0x00,0x00,0xF8,0x1F,0x00,0x50,0x50,0x05,0x05};
// The same exact endpoint-alpha quadrants, encoded with DXT5 alpha selectors.
constexpr std::array<uint8_t,16> bc3FilterBlock={
    0x00,0xFF,0x40,0x02,0x24,0x09,0x90,0x00,0x00,0xF8,0x1F,0x00,0x50,0x50,0x05,0x05};
// An 8x8 image: white with all eight alpha codes in each interpolation mode,
// then opaque ascending black/white FOUR-color selectors and opaque magenta.
// Literal bytes and analytical expected palettes are independent of ITXD code.
constexpr std::array<uint8_t,64> bc3Blocks={
    0xFF,0x00,0x88,0xC6,0xFA,0x88,0xC6,0xFA,0xFF,0xFF,0xFF,0xFF,0xE4,0xB1,0x4E,0x1B,
    0x00,0xFF,0x77,0x39,0x05,0x77,0x39,0x05,0xFF,0xFF,0xFF,0xFF,0x1B,0x4E,0xB1,0xE4,
    0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xE4,0xB1,0x4E,0x1B,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x1F,0xF8,0x00,0x00,0x00,0x00,0x00,0x00};
std::array<float,4> bc3Texel(size_t x,size_t y){
    constexpr std::array<float,8> eight={1,0,6.0f/7,5.0f/7,4.0f/7,3.0f/7,2.0f/7,1.0f/7};
    constexpr std::array<float,8> sixReversed={1,0,4.0f/5,3.0f/5,2.0f/5,1.0f/5,1,0};
    if(y<4)return {1,1,1,(x<4?eight:sixReversed)[(y*4+x%4)%8]};
    if(x>=4)return {1,0,1,1};
    constexpr std::array<float,16> gray={0,1,1.0f/3,2.0f/3,1,0,2.0f/3,1.0f/3,
        1.0f/3,2.0f/3,0,1,2.0f/3,1.0f/3,1,0};
    const auto g=gray[(y-4)*4+x];return {g,g,g,1};
}
void debugMessages(ID3D11Device* device,bool clearOnly=false) {
    ComPtr<ID3D11InfoQueue> messages;if(FAILED(device->QueryInterface(IID_PPV_ARGS(&messages))))return;
    bool failed=false;
    if(!clearOnly)for(UINT64 i=0;i<messages->GetNumStoredMessagesAllowedByRetrievalFilter();++i) {
        SIZE_T size=0;need(SUCCEEDED(messages->GetMessage(i,nullptr,&size)),"D3D debug message size failed");
        std::vector<uint8_t> storage(size);auto* message=reinterpret_cast<D3D11_MESSAGE*>(storage.data());
        need(SUCCEEDED(messages->GetMessage(i,message,&size)),"D3D debug message read failed");
        if(message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){failed=true;fprintf(stderr,"D3D: %s\n",message->pDescription);}
    }
    messages->ClearStoredMessages();need(!failed,"Im2D produced a D3D debug-layer error");
}
}

namespace Simpsons::Graphics {
struct NativeIm2DProbe {
    static ID3D11DeviceContext* context(NativeBackend& b) {return b.context.Get();}
    static ID3D11Device* device(NativeBackend& b) {return b.device.Get();}
    static const void* pipeline(NativeBackend& b) {return b.im2dPipeline.get();}
    static std::vector<uint64_t> state(NativeBackend& b) {
        auto* c=b.context.Get();std::vector<uint64_t> out;
        auto pointer=[&](const auto& p){out.push_back(reinterpret_cast<uintptr_t>(p.Get()));};
        ComPtr<ID3D11RenderTargetView> rt;ComPtr<ID3D11DepthStencilView> ds;c->OMGetRenderTargets(1,&rt,&ds);pointer(rt);pointer(ds);
        ComPtr<ID3D11BlendState> blend;float factors[4];UINT mask;c->OMGetBlendState(&blend,factors,&mask);pointer(blend);
        for(float v:factors)out.push_back(std::bit_cast<uint32_t>(v));out.push_back(mask);
        ComPtr<ID3D11DepthStencilState> depth;UINT stencil;c->OMGetDepthStencilState(&depth,&stencil);pointer(depth);out.push_back(stencil);
        ComPtr<ID3D11RasterizerState> raster;c->RSGetState(&raster);pointer(raster);
        D3D11_VIEWPORT vp[16]{};UINT n=16;c->RSGetViewports(&n,vp);out.push_back(n);
        for(UINT i=0;i<n;++i)for(float v:{vp[i].TopLeftX,vp[i].TopLeftY,vp[i].Width,vp[i].Height,vp[i].MinDepth,vp[i].MaxDepth})out.push_back(std::bit_cast<uint32_t>(v));
        D3D11_RECT rect[16]{};n=16;c->RSGetScissorRects(&n,rect);out.push_back(n);
        for(UINT i=0;i<n;++i)for(LONG v:{rect[i].left,rect[i].top,rect[i].right,rect[i].bottom})out.push_back(uint32_t(v));
        ComPtr<ID3D11InputLayout> layout;c->IAGetInputLayout(&layout);pointer(layout);
        D3D11_PRIMITIVE_TOPOLOGY topology;c->IAGetPrimitiveTopology(&topology);out.push_back(topology);
        ComPtr<ID3D11Buffer> vb,ib;UINT stride,offset;DXGI_FORMAT format;
        c->IAGetVertexBuffers(0,1,&vb,&stride,&offset);pointer(vb);out.push_back(stride);out.push_back(offset);
        c->IAGetIndexBuffer(&ib,&format,&offset);pointer(ib);out.push_back(format);out.push_back(offset);
        ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
        c->VSGetShader(&vs,nullptr,nullptr);c->PSGetShader(&ps,nullptr,nullptr);pointer(vs);pointer(ps);
        for(UINT i=0;i<2;++i) {
            ComPtr<ID3D11Buffer> vc,pc;ComPtr<ID3D11ShaderResourceView> texture;
            c->VSGetConstantBuffers(i,1,&vc);c->PSGetConstantBuffers(i,1,&pc);c->PSGetShaderResources(i,1,&texture);
            pointer(vc);pointer(pc);pointer(texture);
        }
        ComPtr<ID3D11SamplerState> sampler;c->PSGetSamplers(0,1,&sampler);pointer(sampler);return out;
    }
    static void debug(NativeBackend& b,bool hardware) {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        const auto hr=D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_DEBUG,levels,2,D3D11_SDK_VERSION,&device,&level,&context);
        if(SUCCEEDED(hr)){b.availability=std::make_shared<DeviceAvailability>(device.Get());b.device=device;b.context=context;b.featureLevel=level;fprintf(stderr,"[IM2D TEST] D3D11 debug layer enabled\n");}
    }
    static void depthState(NativeBackend& b,const Im2DDraw& draw) {
        ComPtr<ID3D11DepthStencilState> state;UINT reference{};b.context->OMGetDepthStencilState(&state,&reference);
        need(bool(state),"Missing retained native Im2D depth state");D3D11_DEPTH_STENCIL_DESC desc{};state->GetDesc(&desc);
        // The runtime canonicalizes inactive fields (observed disabled-depth
        // descriptor ALL/LESS even when ZERO/NEVER was supplied). Check active
        // state exactly; the readback matrix verifies disabled writes stay inert.
        const bool activeFields=!draw.depthTest ||
            (desc.DepthWriteMask==(draw.depthWrite?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO) &&
             uint32_t(desc.DepthFunc)==draw.depthCompare+1);
        if(bool(desc.DepthEnable)!=draw.depthTest || !activeFields || desc.StencilEnable || reference)
            fprintf(stderr,"Native depth descriptor enable=%u write=%u compare=%u stencil=%u reference=%u; requested=%u/%u/%u\n",
                desc.DepthEnable,desc.DepthWriteMask,desc.DepthFunc,desc.StencilEnable,reference,draw.depthTest,draw.depthWrite,draw.depthCompare);
        need(bool(desc.DepthEnable)==draw.depthTest && activeFields && !desc.StencilEnable && !reference,
            "Native Im2D lost retained depth/write/compare or disabled stencil state");
    }
    static void cleanTransient(NativeBackend& b) {
        ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
        b.context->VSGetShader(&vs,nullptr,nullptr);b.context->PSGetShader(&ps,nullptr,nullptr);
        need(!vs&&!ps,"Original temporary Im2D VS/PS cleanup was omitted");
        ComPtr<ID3D11Buffer> vertex,index,vs0,vs1,ps1;UINT stride{},offset{};DXGI_FORMAT format{};
        b.context->IAGetVertexBuffers(0,1,&vertex,&stride,&offset);b.context->IAGetIndexBuffer(&index,&format,&offset);
        b.context->VSGetConstantBuffers(0,1,&vs0);b.context->VSGetConstantBuffers(1,1,&vs1);b.context->PSGetConstantBuffers(1,1,&ps1);
        need(!vertex&&!index&&!vs0&&!vs1&&!ps1,"Transient Im2D GPU buffers retained by context");
    }
};
}

void batchEquivalence(NativeBackend& b) {
    auto actual=b.createTarget(32,24,TargetFormat::RGB10A2),reference=b.createTarget(32,24,TargetFormat::RGB10A2);
    auto actualDepth=b.createDepthTarget(32,24),referenceDepth=b.createDepthTarget(32,24);
    auto compare=[&] {
        need(b.readbackTarget(actual)==b.readbackTarget(reference),"Batched packet changed packed color");
        const auto a=b.readbackDepthTarget(actualDepth),r=b.readbackDepthTarget(referenceDepth);
        for(size_t p=0;p<32*24;++p) {
            need(depthBits(a,p)==depthBits(r,p),"Batched packet changed original depth order");
            need(a[p*8+4]==r[p*8+4],"Batched packet changed stencil");
        }
    };
    auto* ctx=NativeIm2DProbe::context(b);auto* device=NativeIm2DProbe::device(b);
    D3D11_BUFFER_DESC cbd{};cbd.ByteWidth=64;cbd.Usage=D3D11_USAGE_DEFAULT;cbd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> sentinel;need(SUCCEEDED(device->CreateBuffer(&cbd,nullptr,&sentinel)),"Batch sentinel constant buffer creation failed");
    const std::array<uint8_t,4> texel={71,32,149,255};auto retainedTexture=b.createTexture(1,1,TextureFormat::RGBA8,texel);
    debugMessages(device,true);
    for(UINT variation=0;variation<64;++variation) {
        b.clearBindings();b.clearTarget(actual,{.125f,.25f,.5f,1});b.clearTarget(reference,{.125f,.25f,.5f,1});
        b.clearDepthTarget(actualDepth,.5f,73);b.clearDepthTarget(referenceDepth,.5f,73);
        auto base=quad(32,24);base.blendWord=0x01000100;base.expandedBlend=variation%2;
        base.depthCompare=variation%8;base.depthTest=(variation&8)!=0;base.depthWrite=(variation&16)!=0;
        base.alphaTest=(variation&32)!=0;base.alphaReference=.45f;base.alphaCompare=(variation/3)%8;
        base.cullBits=variation%3==0?0:variation%3==1?2:6;base.reverseDepth=(variation&2)!=0;
        base.pixelCenterHalf=(variation&4)!=0;base.colorWriteMask=variation%2?15:0;
        std::vector<Im2DDraw> packets;
        for(UINT i=0;i<24;++i) {
            auto packet=base;zValue(packet,float((i*7)%9+1)/10);color(packet,{.2f,.4f,.6f,float(i%4+1)/4});
            for(auto& v:packet.vertices){v.position[0]=v.position[0]*.6f+float(i%3*5);v.position[1]=v.position[1]*.6f+float(i%4*3);}
            if(i&1){packet.primitiveType=3;packet.vertices.resize(3);}packets.push_back(packet);
        }
        b.bindTargets({reference,nullptr,nullptr,nullptr},referenceDepth);b.setViewport({0,0,32,24,0,1});
        for(const auto& packet:packets)b.drawIm2D(reference,referenceDepth,packet);
        b.bindTargets({actual,nullptr,nullptr,nullptr},actualDepth);b.setViewport({0,0,32,24,0,1});
        b.setScissor({2,3,30,22});b.bindEngineTexture(0,retainedTexture);
        auto* cb=sentinel.Get();ctx->VSSetConstantBuffers(0,1,&cb);ctx->PSSetConstantBuffers(1,1,&cb);
        const auto state=NativeIm2DProbe::state(b);
        const auto count=b.im2dDrawCount(),calls=b.im2dNativeDrawCount();
        for(auto packet:packets) {
            b.queueIm2D(actual,actualDepth,packet);
            for(auto& v:packet.vertices){v.position.fill(std::numeric_limits<float>::quiet_NaN());v.color.fill(0);}
        }
        need(b.im2dDrawCount()==count,"Queued packets were counted as submitted before DrawIndexed");
        need(NativeIm2DProbe::state(b)==state,"Queue acceptance changed caller graphics state");
        actual->width=31; // Queued resource metadata must be an owned snapshot.
        b.flushIm2D();actual->width=32;
        need(b.im2dDrawCount()==count+packets.size(),"Batch submission lost original packet accounting");
        need(b.im2dNativeDrawCount()==calls+1,"Compatible packets did not become one actual native draw");
        need(NativeIm2DProbe::state(b)==state,"Batch submission failed to restore caller graphics state");
        compare();
    }
    b.clearBindings();b.bindTargets({actual,nullptr,nullptr,nullptr},actualDepth);b.setViewport({0,0,32,24,0,1});
    b.clearDepthTarget(actualDepth,.5f,73);auto packet=quad(32,24);packet.blendWord=0x01000100;
    packet.depthTest=packet.depthWrite=true;packet.depthCompare=7;zValue(packet,.25f);
    std::array<std::shared_ptr<Buffer>,2> rawBuffers;
    std::array<std::vector<uint8_t>,2> rawExpected;
    for(UINT i=0;i<2;++i) {
        rawBuffers[i]=b.createBuffer(0x40000,BufferKind::Vertex);
        rawExpected[i].assign(0x40000,0);b.writeBuffer(rawBuffers[i],0,rawExpected[i]);
    }
    const auto uploadsBefore=b.bufferUploadCount();
    const auto packetsBefore=b.im2dDrawCount(),callsBefore=b.im2dNativeDrawCount();
    D3D11_QUERY_DESC qd{D3D11_QUERY_PIPELINE_STATISTICS,0};ComPtr<ID3D11Query> query;
    need(SUCCEEDED(device->CreateQuery(&qd,&query)),"Batch pipeline-statistics query creation failed");ctx->Begin(query.Get());
    for(UINT i=0;i<4000;++i) {
        const UINT slot=i/2340,offset=i%2340*112;
        std::array<uint8_t,112> bytes;
        for(UINT k=0;k<bytes.size();++k)bytes[k]=uint8_t(i*17+k*29);
        std::copy(bytes.begin(),bytes.end(),rawExpected[slot].begin()+offset);
        b.queueIm2DBufferWrite(rawBuffers[slot],offset,bytes);bytes.fill(0);
        b.queueIm2D(actual,actualDepth,packet);
    }
    b.flushIm2D();ctx->End(query.Get());b.waitIdle();D3D11_QUERY_DATA_PIPELINE_STATISTICS stats{};
    need(ctx->GetData(query.Get(),&stats,sizeof(stats),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK,"Batch statistics did not retire");
    need(stats.IAPrimitives==8000 && stats.IAVertices==24000,"Batching omitted original triangles/vertices");
    need(b.im2dDrawCount()==packetsBefore+4000 && b.im2dNativeDrawCount()==callsBefore+3,"Batch capacity split changed submission accounting");
    need(b.bufferUploadCount()>uploadsBefore && b.bufferUploadCount()<=uploadsBefore+5,
        "Original raw vertex writes were omitted or remained per-packet copies");
    for(UINT i=0;i<2;++i)
        need(b.readbackBuffer(rawBuffers[i])==rawExpected[i],"Draw batching lost raw GPU vertex bytes");
    uniformDepth(b.readbackDepthTarget(actualDepth),depth20e4(.25f),73);
    // Copy must see the queued write, and a later clear must occur after it.
    zValue(packet,.75f);b.queueIm2D(actual,actualDepth,packet);auto copied=b.copyDepth(actualDepth,referenceDepth);b.waitCopy(copied);
    uniformDepth(b.readbackDepthTarget(referenceDepth),depth20e4(.75f),73);
    zValue(packet,.25f);b.queueIm2D(actual,actualDepth,packet);b.clearDepthTarget(actualDepth,.5f,73);
    uniformDepth(b.readbackDepthTarget(actualDepth),.5f,73);
    // Rejected following work must not discard an earlier accepted packet.
    const auto before=b.im2dDrawCount();b.queueIm2D(actual,actualDepth,packet);
    auto bad=packet;bad.vertices[0].position[0]=std::numeric_limits<float>::quiet_NaN();bool rejected=false;
    try {b.queueIm2D(actual,actualDepth,bad);}catch(const Error&){rejected=true;}
    need(rejected && b.im2dDrawCount()==before+1,"Batch validation lost earlier work or accepted a bad packet");
    uniformDepth(b.readbackDepthTarget(actualDepth),depth20e4(.25f),73);
    // Incompatible depth parameters form separate ordered submissions.
    const auto changed=b.im2dNativeDrawCount();b.queueIm2D(actual,actualDepth,packet);
    packet.depthCompare=1;zValue(packet,.125f);b.queueIm2D(actual,actualDepth,packet);
    const auto result=b.readbackDepthTarget(actualDepth);need(b.im2dNativeDrawCount()==changed+2,"Incompatible depth packets were merged");
    uniformDepth(result,depth20e4(.125f),73);
    // Changing the selected attachments must retain each queued packet's owner
    // and leave the newer caller selection intact after the older batch flushes.
    packet.depthCompare=7;zValue(packet,.25f);b.queueIm2D(actual,actualDepth,packet);
    b.bindTargets({reference,nullptr,nullptr,nullptr},referenceDepth);
    zValue(packet,.75f);b.queueIm2D(reference,referenceDepth,packet);b.flushIm2D();
    b.requireSelectedTargets({reference,nullptr,nullptr,nullptr},referenceDepth);
    uniformDepth(b.readbackDepthTarget(actualDepth),depth20e4(.25f),73);
    uniformDepth(b.readbackDepthTarget(referenceDepth),depth20e4(.75f),73);
    // A following color draw must test against the preceding queued depth.
    // Red would pass against the clear value, but must fail against .25.
    b.bindTargets({actual,nullptr,nullptr,nullptr},actualDepth);
    b.clearTarget(actual,{0,0,1,1});b.clearDepthTarget(actualDepth,.5f,73);
    const auto transitionPackets=b.im2dDrawCount(),transitionCalls=b.im2dNativeDrawCount();
    zValue(packet,.25f);b.queueIm2D(actual,actualDepth,packet);
    auto colored=quad(32,24);colored.depthTest=true;colored.depthWrite=false;colored.depthCompare=1;
    color(colored,{1,0,0,1});zValue(colored,.375f);b.queueIm2D(actual,actualDepth,colored);
    uniform(b.readbackTarget(actual),pack({0,0,1,1}),"Color fallback ran before queued depth");
    uniformDepth(b.readbackDepthTarget(actualDepth),depth20e4(.25f),73);
    need(b.im2dDrawCount()==transitionPackets+2 && b.im2dNativeDrawCount()==transitionCalls+2,
        "Color transition changed ordered submission accounting");
    debugMessages(device);b.clearBindings();
}

#include "header/test_im2d_layers.h"
#include "header/test_im2d_color_batches.h"
int main(int argc,char** argv) {try {
    im2dLayerStorageContracts();
    const bool hardware=argc==2 && std::strcmp(argv[1],"--hardware")==0;
    NativeBackend b(!hardware);NativeIm2DProbe::debug(b,hardware);
    auto target=b.createTarget(16,12,TargetFormat::RGB10A2);auto depth=b.createDepthTarget(16,12);
    b.bindTargets({target,nullptr,nullptr,nullptr},depth);b.setViewport({0,0,16,12,0,1});
    b.clearDepthTarget(depth,0,113);const auto originalDepth=b.readbackDepthTarget(depth);
    auto d=quad();
    auto submit=[&] {
        const auto before=b.viewport();const auto count=b.im2dDrawCount();
        debugMessages(NativeIm2DProbe::device(b),true);
        b.drawIm2D(target,depth,d);need(b.im2dDrawCount()==count+1,"Im2D packet draw counter differs");
        debugMessages(NativeIm2DProbe::device(b));NativeIm2DProbe::depthState(b,d);
        b.requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);const auto after=b.viewport();
        need(before&&after&&!std::memcmp(&*before,&*after,sizeof(*before)),"Im2D changed selected viewport");
        NativeIm2DProbe::cleanTransient(b);
    };
    b.clearTarget(target,{0,0,0,0});color(d,{175.0f/255,0,0,2.0f/255});d.blendWord=0x07060706;submit();
    uniform(b.readbackTarget(target),6,"Source alpha was quantized before scalar blending");
    const auto* cachedPipeline=NativeIm2DProbe::pipeline(b);need(cachedPipeline!=nullptr,"Missing cached Im2D pipeline");
    // Retain packed storage across expansion toggles. This low-alpha input
    // distinguishes explicit float source alpha from premature2-bit reduction.
    for(uint32_t expanded:{1u,0u,1u}) {
        const auto before=pixel(b.readbackTarget(target),0);d.expandedBlend=expanded;
        submit();uniform(b.readbackTarget(target),blended(d.vertices[0].color,before,d.blendWord),
            "Expanded mode toggle lost prior packed codes or changed the declared float equation");
        need(NativeIm2DProbe::pipeline(b)==cachedPipeline,"Expansion toggle replaced native shader ownership");
    }
    d.expandedBlend=0;
    constexpr std::array words={0x07060706u,0x00010001u,0x00010706u,0x00010106u,0x00010186u,0x01000100u};
    for(uint32_t word:words) {
        d.blendWord=0x00010001;color(d,{0.17f,0.41f,0.83f,0.72f});submit();
        for(uint32_t repetition=0;repetition<3;++repetition) {
            const auto before=pixel(b.readbackTarget(target),0);const std::array source={0.31f,0.73f,0.23f,0.37f};
            d.blendWord=word;color(d,source);submit();
            uniform(b.readbackTarget(target),blended(source,before,word),"Explicit effective blend equation or repeated packing differs");
        }
    }
    // Distinguishes the first original nonseparate blend from Screen_Xenon alpha replacement.
    b.clearTarget(target,{0,0,0,0});d.blendWord=0x07060706;color(d,{0,0,0,0.5f});submit();
    uniform(b.readbackTarget(target),0x40000000,"Nonseparate alpha must be source alpha squared");
    d.blendWord=0x00010001;d.alphaTest=true;d.alphaReference=0.5f;
    for(uint32_t compare=0;compare<8;++compare) for(float alpha:{0.25f,0.5f,0.75f}) {
        b.clearTarget(target,{0,0,0,0});d.alphaCompare=compare;color(d,{0.25f,0.75f,0.5f,alpha});submit();
        uniform(b.readbackTarget(target),alphaAccept(alpha,d.alphaReference,compare)?pack(d.vertices[0].color):0,"Original alpha comparison differs");
    }
    // Test before storage saturation, as required by the selected source expression.
    b.clearTarget(target,{0,0,0,0});d.alphaCompare=1;d.alphaReference=0;color(d,{1,0,0,-0.25f});submit();
    uniform(b.readbackTarget(target),1023,"Alpha test was applied after source saturation");d.alphaTest=false;
    for(uint8_t mask=0;mask<16;++mask) {
        d.colorWriteMask=15;color(d,{0.17f,0.41f,0.83f,0.72f});submit();const auto before=pixel(b.readbackTarget(target),0);
        const std::array source={0.83f,0.23f,0.47f,0.1f};d.colorWriteMask=mask;color(d,source);submit();
        uint32_t bits=0;for(uint32_t c=0;c<4;++c) if(mask&(1u<<c)) bits|=(c==3?3u:1023u)<<(10*c);
        uniform(b.readbackTarget(target),(before&~bits)|(pack(source)&bits),"Packed integer channel mask lost preserved codes");
    }
    d=quad();
    // Actual viewport is used for drawing, including offset and extent. Original
    // raster constants remain 16x12 independently of this smaller viewport.
    b.clearTarget(target,{0,0,0,0});b.setViewport({3,2,8,6,0.25f,0.75f});submit();auto bytes=b.readbackTarget(target);
    for(size_t y=0;y<12;++y) for(size_t x=0;x<16;++x)
        need(pixel(bytes,y*16+x)==(x>=3&&x<11&&y>=2&&y<8?0xFFFFFFFFu:0),"Im2D ignored selected viewport or overwrote uncovered pixels");
    b.setViewport({0,0,16,12,0,1});
    // Native pixel-center translation fixture: integer mode moves raster edges
    // by +0.5 pixels without removing the original shader's -0.5 operation.
    d=quad();for(auto& v:d.vertices){v.position[0]=(v.position[0]<1?0.25f:8.25f);v.position[1]=(v.position[1]<1?0.25f:6.25f);}
    for(bool half:{true,false}) {
        d.pixelCenterHalf=half;b.clearTarget(target,{0,0,0,0});submit();bytes=b.readbackTarget(target);
        for(size_t y=0;y<12;++y) for(size_t x=0;x<16;++x)
            need(pixel(bytes,y*16+x)==(x<8&&y<6?0xFFFFFFFFu:0),"Native center translation fractional edge coverage differs");
    }
    // A boundary that moves across sample centers distinguishes the modes.
    for(auto& v:d.vertices){v.position[0]+=0.5f;v.position[1]+=0.5f;}
    for(bool half:{true,false}) {
        d.pixelCenterHalf=half;b.clearTarget(target,{0,0,0,0});submit();bytes=b.readbackTarget(target);
        for(size_t y=0;y<12;++y) for(size_t x=0;x<16;++x) {
            const bool covered=half?(x<8&&y<6):(x>=1&&x<9&&y>=1&&y<7);
            need(pixel(bytes,y*16+x)==(covered?0xFFFFFFFFu:0),"Native integer/half-integer center distinction lost");
        }
    }
    // Explicit cull2/6 test of both strip winding parities. These are native
    // raster policy tests, not independently established console coverage.
    for(uint32_t cull:{2u,6u}) for(bool reversed:{false,true}) {
        d=quad();d.cullBits=cull;if(reversed){std::swap(d.vertices[0],d.vertices[1]);std::swap(d.vertices[2],d.vertices[3]);}
        b.clearTarget(target,{0,0,0,0});submit();uniform(b.readbackTarget(target),(cull==6)!=reversed?0xFFFFFFFFu:0,"Raw cull mapping or strip parity differs");
    }
    // Repeated coverage in ONE strip must see the latest destination. Two
    // nondegenerate copies of the same triangle, separated by degenerate edges.
    d=quad();d.blendWord=0x07060706;color(d,{0.5f,0.25f,0.75f,0.5f});
    const auto a=d.vertices[0],bb=d.vertices[1],c=d.vertices[2];d.vertices={a,bb,c,c,a,a,bb,c};
    b.clearTarget(target,{0,0,0,0});submit();bytes=b.readbackTarget(target);
    const uint32_t once=blended(a.color,0,d.blendWord),twice=blended(a.color,once,d.blendWord);
    need(once!=twice&&pixel(bytes,0)==twice,"Overlapping strip used stale per-packet destination");
    need(pixel(bytes,16*12-1)==0,"Overlapping strip corrupted uncovered pixels");
    // Vertex min(1,color) must occur before interpolation. Across a horizontal
    // ramp from 2 to -1 the left endpoint is 1; an ignored NaN RHW cannot leak.
    d=quad();for(auto& v:d.vertices){v.color={v.uv[0]==0?2.0f:-1.0f,0,0,1};v.position[3]=std::numeric_limits<float>::quiet_NaN();v.uv={NAN,NAN};}
    submit();bytes=b.readbackTarget(target);
    for(size_t y=0;y<12;++y) for(size_t x=0;x<16;++x) {
        const float red=1.0f-2.0f*(float(x)+0.5f)/16.0f;
        need(pixel(bytes,y*16+x)==pack({red,0,0,1}),"Vertex clamping, affine interpolation or ignored RHW differs");
    }
    // Owned packet copy and source bytes survive caller mutation/release.
    auto source=quad();auto owned=source;source.vertices.clear();
    std::array<uint8_t,4> texel={17,101,239,128};auto texture=b.createTexture(1,1,TextureFormat::RGBA8,texel);
    owned.texture=texture;texture.reset();texel.fill(0);d=std::move(owned);color(d,{0.5f,0.25f,0.75f,0.5f});
    submit();d.vertices.clear();d.texture.reset();
    uniform(b.readbackTarget(target),pack({17.0f/255*0.5f,101.0f/255*0.25f,239.0f/255*0.75f,128.0f/255*0.5f}),"Owned vertex/texture upload or sampled RGBA multiplication differs");
    d=quad();const std::array<uint8_t,8> twoTexels={0,0,0,255,255,255,255,255};
    d.texture=b.createTexture(2,1,TextureFormat::RGBA8,twoTexels);
    for(auto& v:d.vertices) v.uv={0.5f,0.5f};d.sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;submit();
    uniform(b.readbackTarget(target),pack({0.5f,0.5f,0.5f,1}),"Native linear texture sampling differs");
    d.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;submit();uniform(b.readbackTarget(target),0xFFFFFFFF,"Explicit point sampler was ignored");
    d.sampler.AddressU=D3D11_TEXTURE_ADDRESS_WRAP;for(auto& v:d.vertices)v.uv={1.125f,0.5f};submit();
    uniform(b.readbackTarget(target),0xC0000000,"Explicit wrap sampler was ignored");
    d.sampler.AddressU=D3D11_TEXTURE_ADDRESS_CLAMP;submit();uniform(b.readbackTarget(target),0xFFFFFFFF,"Explicit clamp sampler was ignored");
    d.texture=b.createTexture(1,2,TextureFormat::RGBA8,twoTexels);
    for(auto& v:d.vertices)v.uv={0.5f,1.25f};
    d.sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    d.sampler.AddressV=D3D11_TEXTURE_ADDRESS_WRAP;submit();
    uniform(b.readbackTarget(target),0xC0000000,"Explicit vertical repeat sampler was ignored");
    d.sampler.AddressV=D3D11_TEXTURE_ADDRESS_CLAMP;submit();
    uniform(b.readbackTarget(target),0xFFFFFFFF,"Explicit vertical clamp sampler was ignored");
    d.texture=b.createTexture(1,1,TextureFormat::BGRX8,std::array<uint8_t,4>{239,101,17,0});submit();
    uniform(b.readbackTarget(target),pack({17.0f/255,101.0f/255,239.0f/255,1}),"BGRX channel mapping or forced alpha differs");
    need(NativeIm2DProbe::pipeline(b)==cachedPipeline,"Pipeline was not cached across submissions");NativeIm2DProbe::cleanTransient(b);
    need(b.readbackDepthTarget(depth)==originalDepth,"Im2D changed selected depth or stencil storage");

    // Sample real BC2 storage through the original texture2D*diffuse branch.
    // A non-square block rectangle catches block pitch, row and selector order;
    // replace blending must retain colored RGB even where sampled alpha is 0.
    {
        auto uploadBytes=bc2Blocks;auto bc2=b.createTexture(12,8,TextureFormat::BC2,uploadBytes);
        const std::vector<uint8_t> compressed(bc2Blocks.begin(),bc2Blocks.end());uploadBytes.fill(0xFF);
        need(b.readback(bc2)==compressed,"BC2 immutable upload borrowed or changed caller bytes");
        need(bc2->levelCount()==1,"BC2 fixture unexpectedly has multiple levels");
        b.setViewport({0,0,12,8,0,1});
        const std::array<std::array<float,4>,2> diffuse={{{1,1,1,1},{0.625f,0.375f,0.125f,0.375f}}};
        for(size_t tint=0;tint<diffuse.size();++tint)for(bool gate:{false,true}) {
            d=quad();d.texture=bc2;color(d,diffuse[tint]);zValue(d,0.25f);
            d.alphaTest=gate;d.alphaReference=tint?0.1875f:0.5f;d.alphaCompare=4;
            d.depthTest=d.depthWrite=gate;d.depthCompare=7;d.reverseDepth=true;
            b.clearTarget(target,{0,0,0,0});b.clearDepthTarget(depth,0.5f,101);submit();
            const auto rgba=b.readbackTarget(target),z=b.readbackDepthTarget(depth);
            for(size_t y=0;y<12;++y)for(size_t x=0;x<16;++x) {
                const bool covered=x<12&&y<8;std::array<float,4> sourceColor{};
                const auto texel=covered?bc2Texel(x,y):std::array<float,4>{};
                if(covered) {sourceColor=texel;for(size_t channel=0;channel<4;++channel)sourceColor[channel]*=diffuse[tint][channel];}
                const bool pass=covered&&(!gate||sourceColor[3]>d.alphaReference);
                const uint32_t expected=pass?pack(sourceColor):0,actual=pixel(rgba,y*16+x);
                if(pass&&x<4&&y<4&&texel[0]>0&&texel[0]<1) {
                    need((actual>>30)==(expected>>30),"BC2 derived RGB changed exact packed alpha");
                    const uint32_t k=texel[0]<0.5f?1u:2u;
                    for(uint32_t channel=0;channel<3;++channel) {
                        const auto interval=bc2InterpolantCodes(k,uint32_t(diffuse[tint][channel]*8));
                        const uint32_t code=(actual>>(10*channel))&1023;
                        if(code<interval[0]||code>interval[1])fprintf(stderr,
                            "BC2 tint%zu gate%u pixel(%zu,%zu) channel%u got %u interval [%u,%u]\n",
                            tint,unsigned(gate),x,y,channel,code,interval[0],interval[1]);
                        need(code>=interval[0]&&code<=interval[1],"BC2 derived RGB exceeds the propagated D3D11.3 decode interval");
                    }
                } else {
                    if(actual!=expected)fprintf(stderr,"BC2 tint%zu gate%u pixel(%zu,%zu) got %08X expected %08X\n",
                        tint,unsigned(gate),x,y,actual,expected);
                    need(actual==expected,"BC2 block/row/alpha or texture*diffuse packed output differs");
                }
                depthPixel(z,y*16+x,gate&&pass?0.75f:0.5f,101);
            }
            if(!tint&&!gate) {
                need(pixel(rgba,4)==0x000003FFu,"BC2 alpha0 lost stored red under replace blending");
                need(pixel(rgba,0)==0&&pixel(rgba,1)==0x3FFFFFFFu,"BC2 endpoint RGB or explicit alpha changed");
                need(pixel(rgba,8)==0x400FFC00u&&pixel(rgba,4*16)==0xBFF00000u,
                    "BC2 explicit alpha or block row order differs");
            }
            need(b.readback(bc2)==compressed,"BC2 draw changed source compressed storage");
        }
        // An all-white palette must decode exactly even for selectors2/3.
        // With SRC_ALPHA blending, each explicit alpha nibble becomes a
        // distinct exact 10-bit RGB result, not merely a two-bit alpha code.
        constexpr std::array<uint8_t,16> whiteAlpha={
            0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE,0xFF,0xFF,0xFF,0xFF,0xE4,0xB1,0x4E,0x1B};
        auto alphaTexture=b.createTexture(4,4,TextureFormat::BC2,whiteAlpha);
        b.setViewport({0,0,4,4,0,1});
        for(bool gate:{false,true}) {
            d=quad();d.texture=alphaTexture;d.blendWord=0x07060706;
            d.alphaTest=gate;d.alphaReference=0.5f;d.alphaCompare=4;zValue(d,0.25f);
            d.depthTest=d.depthWrite=true;d.depthCompare=7;
            b.clearTarget(target,{0,0,0,0});b.clearDepthTarget(depth,0.75f,103);submit();
            const auto rgba=b.readbackTarget(target),z=b.readbackDepthTarget(depth);
            for(size_t y=0;y<12;++y)for(size_t x=0;x<16;++x) {
                const bool covered=x<4&&y<4;const float alpha=covered?float(y*4+x)/15:0;
                const bool pass=covered&&(!gate||alpha>0.5f);
                need(pixel(rgba,y*16+x)==(pass?blended({1,1,1,alpha},0,d.blendWord):0),
                    "BC2 exact white/explicit-alpha ramp or SRC_ALPHA blending differs");
                depthPixel(z,y*16+x,pass?0.25f:0.75f,103);
            }
        }
        need(b.readback(alphaTexture)==std::vector<uint8_t>(whiteAlpha.begin(),whiteAlpha.end()),
            "BC2 alpha-ramp draw modified source bytes");
        b.setViewport({0,0,16,12,0,1});
        struct FilterSample {D3D11_FILTER filter;float u,v;std::array<float,4> expected;};
        constexpr auto point=D3D11_FILTER_MIN_MAG_MIP_POINT,linear=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        constexpr std::array<FilterSample,8> samples={{{point,0.5f,0.5f,{1,0,0,0}},
            {point,0.375f,0.625f,{0,0,1,1}},{linear,0.375f,0.375f,{1,0,0,0}},
            {linear,0.625f,0.375f,{0,0,1,1}},{linear,0.5f,0.5f,{0.5f,0,0.5f,0.5f}},
            {linear,0.4375f,0.375f,{0.75f,0,0.25f,0.25f}},
            {linear,0.5625f,0.375f,{0.25f,0,0.75f,0.75f}},
            {linear,0.4375f,0.4375f,{0.625f,0,0.375f,0.375f}}}};
        for(auto filterFormat:{TextureFormat::BC2,TextureFormat::BC3}) {
        const auto& filterBlock=filterFormat==TextureFormat::BC2?bc2FilterBlock:bc3FilterBlock;
        auto filtered=b.createTexture(4,4,filterFormat,filterBlock);
        // D3D11.3 sections7.18.16.2/19.5.7 require at least UNORM8
        // filtering precision, not exact binary float results. A nominal
        // half blend may pass GREATER .5 while packing to the expected color.
        // Preserve exact packed-color checks. Each mixed-alpha sample must
        // pass .125 and fail .875: minimum ideal separation is .125, well
        // away from the UNORM8 filtering resolution. Case0 keeps alpha off.
        for(const auto& sample:samples)for(uint32_t gateCase=0;gateCase<3;++gateCase) {
            d=quad();d.texture=filtered;d.sampler.Filter=sample.filter;
            for(auto& v:d.vertices)v.uv={sample.u,sample.v};zValue(d,0.25f);
            d.alphaTest=gateCase!=0;d.alphaCompare=4;d.alphaReference=gateCase==1?0.125f:0.875f;
            d.depthTest=d.depthWrite=true;d.depthCompare=7;
            b.clearTarget(target,{0,0,0,0});b.clearDepthTarget(depth,0.75f,107);submit();
            const bool pass=!d.alphaTest||sample.expected[3]>d.alphaReference;
            const auto rgba=b.readbackTarget(target);
            if(pixel(rgba,0)!=(pass?pack(sample.expected):0))fprintf(stderr,
                "BC2 filter%u UV(%.4f,%.4f) gateCase%u reference%.3f expectedPass%u\n",
                unsigned(sample.filter),sample.u,sample.v,gateCase,d.alphaReference,unsigned(pass));
            uniform(rgba,pass?pack(sample.expected):0,"BC2/BC3 point/bilinear sample or separated alpha gate differs");
            uniformDepth(b.readbackDepthTarget(depth),pass?0.25f:0.75f,107);
        }
        // Exact ties belong to a point sample: selected blue texel alpha1
        // times diffuse .5 yields exactly .5. All eight comparisons still
        // distinguish equality from strict inequality and gate depth writes.
        for(uint32_t compare=0;compare<8;++compare) {
            d=quad();d.texture=filtered;for(auto& v:d.vertices)v.uv={0.375f,0.625f};
            color(d,{1,1,1,0.5f});zValue(d,0.25f);
            d.alphaTest=true;d.alphaCompare=compare;d.alphaReference=0.5f;
            d.depthTest=d.depthWrite=true;d.depthCompare=7;
            b.clearTarget(target,{0,0,0,0});b.clearDepthTarget(depth,0.75f,109);submit();
            const bool pass=alphaAccept(0.5f,0.5f,compare);
            uniform(b.readbackTarget(target),pass?0xBFF00000u:0,"BC2/BC3 exact point-alpha tie comparison differs");
            uniformDepth(b.readbackDepthTarget(depth),pass?0.25f:0.75f,109);
        }
        need(b.readback(filtered)==std::vector<uint8_t>(filterBlock.begin(),filterBlock.end()),
            "BC2/BC3 filtering modified source blocks");
        // Release every caller texture reference and the vertex packet before
        // observing output. The immediate submission must retain GPU inputs.
        d=quad();d.texture=filtered;filtered.reset();for(auto& v:d.vertices)v.uv={0.375f,0.625f};
        color(d,{0.625f,0.375f,0.125f,0.375f});submit();d.texture.reset();d.vertices.clear();
        uniform(b.readbackTarget(target),pack({0,0,0.125f,0.375f}),"Released BC2/BC3 packet/source lost queued sampling");
        need(NativeIm2DProbe::pipeline(b)==cachedPipeline,"BC2/BC3 submission replaced the shared Im2D pipeline");
        }
    }

    {
        auto upload=bc3Blocks;auto bc3=b.createTexture(8,8,TextureFormat::BC3,upload);upload.fill(0);
        const std::vector<uint8_t> compressed(bc3Blocks.begin(),bc3Blocks.end());
        need(b.readback(bc3)==compressed&&bc3->levelCount()==1,"BC3 immutable upload or level count differs");
        b.setViewport({0,0,8,8,0,1});
        const std::array<std::array<float,4>,2> tints={{{1,1,1,1},{0.625f,0.375f,0.125f,0.375f}}};
        for(const auto& tint:tints)for(uint32_t blend:{0x00010001u,0x00010706u})for(uint32_t gate=0;gate<9;++gate){
            d=quad();d.texture=bc3;color(d,tint);zValue(d,0.25f);d.blendWord=blend;
            d.alphaTest=gate!=0;d.alphaCompare=gate?gate-1:7;d.alphaReference=tint[3]*0.5f;
            d.depthTest=d.depthWrite=true;d.depthCompare=7;
            b.clearTarget(target,{0,0,0,0});b.clearDepthTarget(depth,0.75f,111);submit();
            const auto rgba=b.readbackTarget(target),z=b.readbackDepthTarget(depth);
            for(size_t y=0;y<12;++y)for(size_t x=0;x<16;++x){
                const bool covered=x<8&&y<8;const auto texel=covered?bc3Texel(x,y):std::array<float,4>{};
                const bool pass=covered&&(!d.alphaTest||alphaAccept(texel[3]*tint[3],d.alphaReference,d.alphaCompare));
                const auto actual=pixel(rgba,y*16+x);
                if(!pass)need(actual==0,"BC3 alpha rejection or uncovered storage changed");
                else {
                    // D3D11.3 19.5.2/19.5.8: each derived channel may vary by
                    // <1/255+.03*endpointRange. These endpoints span exactly1.
                    // Preserve exact 0/1; propagate the allowed interval through
                    // positive tint, blending and packing. All alpha comparisons
                    // here are separated from .5 by more than this error bound.
                    std::array<float,4> lo{},hi{};
                    for(size_t c=0;c<4;++c){
                        const float error=texel[c]>0&&texel[c]<1?173.0f/5100:0;
                        lo[c]=(texel[c]-error)*tint[c];hi[c]=(texel[c]+error)*tint[c];
                    }
                    const auto low=blended(lo,0,blend),high=blended(hi,0,blend);
                    for(uint32_t c=0;c<4;++c){
                        const uint32_t mask=c==3?3u:1023u,code=(actual>>(10*c))&mask;
                        need(code>=((low>>(10*c))&mask)&&code<=((high>>(10*c))&mask),
                            "BC3 palette/alpha mode/selector/blend exceeds D3D11.3 interval");
                    }
                }
                depthPixel(z,y*16+x,pass?0.25f:0.75f,111);
            }
        }
        need(b.readback(bc3)==compressed,"BC3 sampling changed immutable compressed bytes");
        need(NativeIm2DProbe::pipeline(b)==cachedPipeline,"BC3 replaced cached Im2D pipeline");
        b.setViewport({0,0,16,12,0,1});
    }

    // Exact decoded 20e4 storage, including values that become equal only after
    // conversion. The PS maps the original depth range before quantization.
    need(std::bit_cast<uint32_t>(depth20e4(std::bit_cast<float>(0x3EFF7CEEu)))==0x3EFF7CF0u,
        "Forward actual-Z reference conversion differs");
    need(std::bit_cast<uint32_t>(depth20e4(std::bit_cast<float>(0x3F004189u)))==0x3F004188u,
        "Reversed actual-Z reference conversion differs");
    constexpr std::array roundingPins={
        std::array{0x00000000u,0x00000000u},std::array{0x00000001u,0x00000000u},
        std::array{0x2E000000u,0x00000000u},std::array{0x2E800000u,0x2E800000u},
        std::array{0x2EC00000u,0x2F000000u},std::array{0x387FFFFFu,0x38800000u},
        std::array{0x38800000u,0x38800000u},std::array{0x3F000004u,0x3F000000u},
        std::array{0x3F00000Cu,0x3F000010u},std::array{0x3F7FFFFFu,0x3F800000u}};
    for(const auto& pin:roundingPins) {
        const float z=std::bit_cast<float>(pin[0]);
        need(std::bit_cast<uint32_t>(depth20e4(z))==pin[1],"20e4 rounding/tie/subnormal reference pin differs");
        for(bool reverse:{false,true}) {
            d=quad();d.depthTest=d.depthWrite=true;d.depthCompare=7;d.reverseDepth=reverse;zValue(d,z);
            b.clearTarget(target,{0,0,0,0});b.clearDepthTarget(depth,0.75f,139);submit();
            uniform(b.readbackTarget(target),0xFFFFFFFFu,"20e4 rounding fixture lost color coverage");
            uniformDepth(b.readbackDepthTarget(depth),reverse?depth20e4(1.0f-z):std::bit_cast<float>(pin[1]),139);
        }
    }
    for(bool reverse:{false,true})for(bool enabled:{false,true})for(bool write:{false,true})for(uint32_t compare=0;compare<8;++compare)
    for(float z:{0.0f,0.25f,std::nextafter(0.5f,0.0f),0.5f,std::nextafter(0.5f,1.0f),0.75f,1.0f}) {
        d=quad();d.reverseDepth=reverse;d.depthTest=enabled;d.depthWrite=write;d.depthCompare=compare;zValue(d,z);
        b.clearTarget(target,{0,0,0,0});b.clearDepthTarget(depth,0.5f,173);submit();
        const float converted=depth20e4(reverse?1.0f-z:z);
        const bool pass=!enabled||alphaAccept(converted,0.5f,compare);
        uniform(b.readbackTarget(target),pass?0xFFFFFFFFu:0,"Native depth compare/enable gated color incorrectly");
        uniformDepth(b.readbackDepthTarget(depth),enabled&&write&&pass?converted:0.5f,173);
    }
    // Actual154 uses reversed depth. Both candidate reference rounding rules
    // agree on 3F004188 there; the forward fixture selects the declared RNE rule.
    for(bool reverse:{false,true}) {
        d=quad();d.reverseDepth=reverse;d.depthTest=d.depthWrite=true;d.depthCompare=7;
        const float actualZ=std::bit_cast<float>(0x3EFF7CEEu);zValue(d,actualZ);color(d,{0,0,0,0});
        const float expectedZ=std::bit_cast<float>(reverse?0x3F004188u:0x3EFF7CF0u);
        b.clearTarget(target,{1,1,1,1});b.clearDepthTarget(depth,0.75f,67);submit();
        uniform(b.readbackTarget(target),0,"Actual constant black ALWAYS-write fixture color differs");
        uniformDepth(b.readbackDepthTarget(depth),expectedZ,67);
        // Native target-edge clipping, not an independent console snap proof.
        for(auto& v:d.vertices) {
            v.position[0]=v.uv[0]==0?-0.25f:16.75f;v.position[1]=v.uv[1]==0?-0.25f:12.75f;
        }
        b.clearTarget(target,{1,1,1,1});b.clearDepthTarget(depth,0.75f,67);submit();
        uniform(b.readbackTarget(target),0,"Overscanned native ALWAYS-write quad color differs");
        uniformDepth(b.readbackDepthTarget(depth),expectedZ,67);
    }

    // Alpha discard must gate depth even when the depth comparison is ALWAYS;
    // every alpha function is checked for flat and texture*diffuse branches.
    for(bool textured:{false,true})for(uint32_t compare=0;compare<8;++compare)
    for(float alpha:{0.25f,0.5f,0.75f}) {
        d=quad();d.depthTest=d.depthWrite=true;d.depthCompare=7;zValue(d,0.25f);
        d.alphaTest=true;d.alphaReference=0.5f;d.alphaCompare=compare;color(d,{1,0,0,alpha});
        if(textured)d.texture=b.createTexture(1,1,TextureFormat::RGBA8,std::array<uint8_t,4>{255,255,255,255});
        b.clearTarget(target,{0,0,0,0});b.clearDepthTarget(depth,0.75f,211);submit();
        const bool pass=alphaAccept(alpha,0.5f,compare);
        uniform(b.readbackTarget(target),pass?pack(d.vertices[0].color):0,"Alpha/depth color gating differs");
        uniformDepth(b.readbackDepthTarget(depth),pass?0.25f:0.75f,211);
    }
    // A sampled alpha can discard even with opaque vertex alpha.
    d=quad();d.depthTest=d.depthWrite=true;d.depthCompare=7;zValue(d,0.25f);
    d.alphaTest=true;d.alphaCompare=4;d.alphaReference=0.5f;
    d.texture=b.createTexture(1,1,TextureFormat::RGBA8,std::array<uint8_t,4>{255,255,255,0});
    b.clearTarget(target,{0,0,0,0});b.clearDepthTarget(depth,0.75f,71);submit();
    uniform(b.readbackTarget(target),0,"Sampled alpha discard changed color");uniformDepth(b.readbackDepthTarget(depth),0.75f,71);
    for(uint32_t cull:{0u,2u,6u})for(bool reversed:{false,true}) {
        d=quad();d.depthTest=d.depthWrite=true;d.depthCompare=7;d.cullBits=cull;zValue(d,0.25f);
        if(reversed){std::swap(d.vertices[0],d.vertices[1]);std::swap(d.vertices[2],d.vertices[3]);}
        b.clearTarget(target,{0,0,0,0});b.clearDepthTarget(depth,0.75f,91);submit();
        const bool pass=cull==0||((cull==6)!=reversed);
        uniform(b.readbackTarget(target),pass?0xFFFFFFFFu:0,"Cull/depth color gating differs");
        uniformDepth(b.readbackDepthTarget(depth),pass?0.25f:0.75f,91);
    }
    for(uint8_t mask=0;mask<16;++mask) {
        d=quad();d.depthTest=d.depthWrite=true;d.depthCompare=7;d.colorWriteMask=mask;zValue(d,0.25f);
        b.clearTarget(target,{0,0,0,0});b.clearDepthTarget(depth,0.75f,121);submit();
        uint32_t bits=0;for(uint32_t c=0;c<4;++c)if(mask&(1u<<c))bits|=(c==3?3u:1023u)<<(10*c);
        uniform(b.readbackTarget(target),bits,"Color mask/depth interaction changed color");
        uniformDepth(b.readbackDepthTarget(depth),0.25f,121); // Mask0 still writes depth.
    }
    // Boot172 reached ZERO/ADD/ONE with textured triangles and ALWAYS depth
    // writes. Keep nonuniform packed destination bytes exactly, while proving
    // real depth writes, coverage and sampled alpha rejection still execute.
    {
        d=quad();
        for(auto& v:d.vertices)v.color={0.13f+0.71f*v.uv[0],0.29f+0.37f*v.uv[1],0.81f-0.43f*v.uv[0],v.uv[0]};
        submit();const auto retained=b.readbackTarget(target);
        const auto alphaTexture=b.createTexture(2,1,TextureFormat::RGBA8,
            std::array<uint8_t,8>{19,203,71,0,211,31,149,255});
        for(bool reverse:{false,true})for(bool enabled:{false,true})for(bool write:{false,true})for(uint32_t compare=0;compare<8;++compare) {
            d=quad();d.blendWord=0x01000100;d.depthTest=enabled;d.depthWrite=write;d.depthCompare=compare;
            d.reverseDepth=reverse;zValue(d,0.25f);color(d,{0.91f,0.07f,0.43f,0.37f});
            b.clearDepthTarget(depth,0.5f,157);submit();
            need(b.readbackTarget(target)==retained,"ZERO/ONE changed packed destination color or alpha");
            const float z=reverse?0.75f:0.25f;
            uniformDepth(b.readbackDepthTarget(depth),enabled&&write&&alphaAccept(z,0.5f,compare)?z:0.5f,157);
        }
        for(uint32_t expanded:{0u,1u})for(bool reverse:{false,true})for(bool textured:{false,true})
        for(bool gate:{false,true})for(uint8_t mask=0;mask<16;++mask) {
            d=quad();d.blendWord=0x01000100;d.expandedBlend=expanded;d.colorWriteMask=mask;
            d.depthTest=d.depthWrite=true;d.depthCompare=7;d.reverseDepth=reverse;zValue(d,0.25f);
            d.alphaTest=gate;d.alphaCompare=4;d.alphaReference=0.5f;color(d,{0.71f,0.29f,0.83f,1});
            if(textured)d.texture=alphaTexture;
            for(auto& v:d.vertices)if(v.uv[0]>0)v.position[0]=8.5f;
            b.clearDepthTarget(depth,0.5f,159);submit();
            need(b.readbackTarget(target)==retained,"ZERO/ONE mask, expansion or sampled alpha changed destination bytes");
            const auto z=b.readbackDepthTarget(depth);
            for(size_t y=0;y<12;++y)for(size_t x=0;x<16;++x) {
                const bool pass=x<8&&(!gate||!textured||x>=4);
                depthPixel(z,y*16+x,pass?(reverse?0.75f:0.25f):0.5f,159);
            }
        }
        // Two overlapping primitives must update depth in submission order,
        // even though their destination color is unchanged after each write.
        for(uint32_t primitive:{3u,4u})for(bool reverse:{false,true})for(uint32_t compare:{1u,4u,7u}) {
            d=quad();d.blendWord=0x01000100;d.primitiveType=primitive;d.depthTest=d.depthWrite=true;
            d.depthCompare=compare;d.reverseDepth=reverse;auto first=d.vertices,second=first;
            for(auto& v:first)v.position[2]=0.25f;for(auto& v:second)v.position[2]=0.75f;
            if(primitive==3)d.vertices={first[0],first[1],first[2],second[0],second[1],second[2]};
            else d.vertices={first[0],first[1],first[2],first[2],second[0],second[0],second[1],second[2]};
            b.clearDepthTarget(depth,0.5f,161);submit();
            need(b.readbackTarget(target)==retained,"Overlapping ZERO/ONE triangles changed packed destination bytes");
            float expected=0.5f;
            for(float z:{0.25f,0.75f}){z=reverse?1.0f-z:z;if(alphaAccept(z,expected,compare))expected=z;}
            const auto z=b.readbackDepthTarget(depth);depthPixel(z,0,expected,161);depthPixel(z,191,0.5f,161);
        }
    }
    // Per-triangle depth updates and blend snapshots must agree in one strip.
    // Repeated triangles are separated by degenerate edges; equal Z must make
    // LESS shade once and LEQUAL shade twice when writes are enabled.
    for(bool write:{false,true})for(uint32_t compare:{1u,3u,4u,6u,7u})
    for(const auto zs:{std::array{0.25f,0.5f},std::array{0.5f,0.25f},std::array{0.5f,0.5f},
        std::array{0.5f,std::nextafter(0.5f,1.0f)}}) {
        d=quad();d.depthTest=true;d.depthWrite=write;d.depthCompare=compare;d.blendWord=0x07060706;
        color(d,{0.5f,0.25f,0.75f,0.5f});
        auto first=d.vertices;auto second=first;
        for(auto& v:first)v.position[2]=zs[0];for(auto& v:second)v.position[2]=zs[1];
        d.vertices={first[0],first[1],first[2],first[2],second[0],second[0],second[1],second[2]};
        const float clear=(compare==4||compare==6)?0.0f:1.0f;
        b.clearTarget(target,{0,0,0,0});b.clearDepthTarget(depth,clear,151);submit();
        float expectedDepth=clear;uint32_t expectedColor=0;
        for(float z:zs)if(alphaAccept(depth20e4(z),expectedDepth,compare)) {
            expectedColor=blended(first[0].color,expectedColor,d.blendWord);if(write)expectedDepth=depth20e4(z);
        }
        const auto colorBytes=b.readbackTarget(target),depthBytes=b.readbackDepthTarget(depth);
        need(pixel(colorBytes,0)==expectedColor,"Strip depth ordering/ties used stale color or depth");
        depthPixel(depthBytes,0,expectedDepth,151);need(pixel(colorBytes,191)==0,"Depth strip changed uncovered color");
        depthPixel(depthBytes,191,clear,151);
    }
    // Subviewport affects coverage, while supplied Z remains unchanged.
    d=quad();d.depthTest=d.depthWrite=true;d.depthCompare=7;zValue(d,0.25f);
    b.clearTarget(target,{0,0,0,0});b.clearDepthTarget(depth,0.75f,193);b.setViewport({3,2,8,6,0,1});submit();
    const auto partialDepth=b.readbackDepthTarget(depth);bytes=b.readbackTarget(target);
    for(size_t y=0;y<12;++y)for(size_t x=0;x<16;++x) {
        const bool covered=x>=3&&x<11&&y>=2&&y<8;
        need(pixel(bytes,y*16+x)==(covered?0xFFFFFFFFu:0),"Depth subviewport color coverage differs");
        depthPixel(partialDepth,y*16+x,covered?0.25f:0.75f,193);
    }
    b.setViewport({0,0,16,12,0,1});

    // Original primitive3: complete independent triplets. Coverage is computed
    // from these small right triangles, with no sample on a sloping edge.
    {
        const auto triangle=[](float x,float y,float z,std::array<float,4> rgba) {
            return std::array{Im2DVertex{{x+0.5f,y+0.5f,z,1},rgba,{0.375f,0.625f}},
                Im2DVertex{{x+4.5f,y+0.5f,z,1},rgba,{0.375f,0.625f}},
                Im2DVertex{{x+0.5f,y+3.5f,z,1},rgba,{0.375f,0.625f}}};
        };
        const auto covered=[](size_t x,size_t y,int originX,int originY) {
            const int px=2*int(x)+1-2*originX,py=2*int(y)+1-2*originY;
            return px>=0&&py>=0&&3*px+4*py<24;
        };
        const auto red=triangle(1,1,0.25f,{1,0,0,1});
        auto blue=triangle(10,6,0.5f,{0,0,1,1});std::swap(blue[1],blue[2]);
        for(uint32_t cull:{0u,2u,6u})for(bool flip:{false,true}) {
            d=quad();d.primitiveType=3;d.vertices.assign(red.begin(),red.end());
            d.vertices.insert(d.vertices.end(),blue.begin(),blue.end());
            if(flip){std::swap(d.vertices[1],d.vertices[2]);std::swap(d.vertices[4],d.vertices[5]);}
            d.cullBits=cull;d.depthTest=d.depthWrite=true;d.depthCompare=7;
            b.clearTarget(target,{0,0,0,0});b.clearDepthTarget(depth,0.75f,83);submit();
            const auto rgba=b.readbackTarget(target),z=b.readbackDepthTarget(depth);
            for(size_t y=0;y<12;++y)for(size_t x=0;x<16;++x) {
                const bool a=covered(x,y,1,1)&&(cull==0||((cull==6)!=flip));
                const bool bb=covered(x,y,10,6)&&(cull==0||((cull==2)!=flip));
                need(pixel(rgba,y*16+x)==(a?0xC00003FFu:bb?0xFFF00000u:0),
                    "Triangle list changed winding, joined independent triangles or lost coverage");
                depthPixel(z,y*16+x,a?0.25f:bb?0.5f:0.75f,83);
            }
        }
        // Overlap is intentional here. Each triplet must observe the preceding
        // packed/masked color and written depth; cull6 catches a parity swap.
        constexpr std::array<std::array<float,4>,2> colors={{{0.75f,0.25f,0.125f,0.25f},{0.125f,0.5f,0.75f,0.75f}}};
        for(bool write:{false,true})for(bool gate:{false,true})for(uint8_t mask:std::array<uint8_t,3>{0,5,15})
        for(uint32_t compare:{1u,3u,7u})for(const auto zs:{std::array{0.25f,0.5f},std::array{0.5f,0.25f},
            std::array{0.5f,0.5f},std::array{0.5f,std::nextafter(0.5f,1.0f)}}) {
            const auto a=triangle(1,1,zs[0],colors[0]),bb=triangle(1,1,zs[1],colors[1]);
            d=quad();d.primitiveType=3;d.vertices.assign(a.begin(),a.end());d.vertices.insert(d.vertices.end(),bb.begin(),bb.end());
            d.cullBits=6;d.blendWord=0x07060706;d.colorWriteMask=mask;
            d.alphaTest=gate;d.alphaCompare=4;d.alphaReference=0.5f;
            d.depthTest=true;d.depthWrite=write;d.depthCompare=compare;d.reverseDepth=true;
            b.clearTarget(target,{0,1,0,1});b.clearDepthTarget(depth,0.875f,87);submit();
            constexpr uint32_t background=0xC00FFC00u;
            const uint32_t maskBits=mask==15?0xFFFFFFFFu:mask==5?0x3FF003FFu:0;
            uint32_t expectedColor=background;float expectedDepth=0.875f;
            for(size_t i=0;i<2;++i) {
                const float incoming=depth20e4(1.0f-zs[i]);
                if((!gate||colors[i][3]>0.5f)&&alphaAccept(incoming,expectedDepth,compare)) {
                    expectedColor=(expectedColor&~maskBits)|(blended(colors[i],expectedColor,d.blendWord)&maskBits);
                    if(write)expectedDepth=incoming;
                }
            }
            const auto rgba=b.readbackTarget(target),z=b.readbackDepthTarget(depth);
            for(size_t y=0;y<12;++y)for(size_t x=0;x<16;++x) {
                const bool inside=covered(x,y,1,1);
                need(pixel(rgba,y*16+x)==(inside?expectedColor:background),
                    "Triangle-list overlap used stale/unmasked color, incorrect alpha or strip ordering");
                depthPixel(z,y*16+x,inside?expectedDepth:0.875f,87);
            }
        }
        // Minimum, reached actual159 count and maximum: only the final triplet
        // or pair of triplets has area. This catches dropped tails and indices
        // beyond the original four-vertex strip path without thousands of
        // overlapping color contributions. BC2 uses an exact opaque blue texel.
        auto uploadBytes=bc2FilterBlock;auto listTexture=b.createTexture(4,4,TextureFormat::BC2,uploadBytes);
        uploadBytes.fill(0);
        for(size_t count:{size_t(3),size_t(498),size_t(9360)}) {
            auto input=quad();input.primitiveType=3;
            const auto corners=input.vertices;
            const std::array tail={corners[0],corners[1],corners[2],corners[3],corners[2],corners[1]};
            input.vertices.assign(count,corners[0]);
            if(count==3)std::copy(red.begin(),red.end(),input.vertices.begin());
            else std::copy(tail.begin(),tail.end(),input.vertices.end()-6);
            for(auto& v:input.vertices){v.color={0.5f,0.25f,0.75f,0.5f};v.uv={0.375f,0.625f};}
            input.texture=listTexture;d=input;input.vertices.clear();input.texture.reset();
            need(b.readback(listTexture)==std::vector<uint8_t>(bc2FilterBlock.begin(),bc2FilterBlock.end()),
                "Triangle-list BC2 upload retained mutable caller bytes");
            b.clearTarget(target,{0,0,0,0});b.clearDepthTarget(depth,0.75f,89);submit();
            d.vertices.clear();d.texture.reset();if(count==9360)listTexture.reset();
            const auto rgba=b.readbackTarget(target),z=b.readbackDepthTarget(depth);
            for(size_t y=0;y<12;++y)for(size_t x=0;x<16;++x) {
                need(pixel(rgba,y*16+x)==(count!=3||covered(x,y,1,1)?0xAFF00000u:0),
                    "Triangle-list boundary count, tail indexing or owned sampled input differs");
                depthPixel(z,y*16+x,0.75f,89);
            }
        }
        need(NativeIm2DProbe::pipeline(b)==cachedPipeline,"Triangle-list selection replaced the shared Im2D pipeline");
    }

    // Each rejection must preserve real bytes, count, selected attachments and
    // viewport. No draw returns success by being silently skipped.
    d=quad();const auto beforeReject=b.readbackTarget(target),beforeRejectDepth=b.readbackDepthTarget(depth);
    const auto initialCount=b.im2dDrawCount();auto selectedDepth=depth;
    size_t rejectionIndex=0;
    auto reject=[&](auto&& action) {
        ++rejectionIndex;
        auto* rejectionContext=NativeIm2DProbe::context(b);
        ComPtr<ID3D11Predicate> priorPredicate;BOOL priorPredicateValue{};
        rejectionContext->GetPredication(&priorPredicate,&priorPredicateValue);
        ComPtr<ID3D11DepthStencilState> priorDepthState,afterDepthState;UINT priorReference{},afterReference{};
        ComPtr<ID3D11BlendState> priorBlend,afterBlend;FLOAT priorFactor[4]{},afterFactor[4]{};UINT priorMask{},afterMask{};
        ComPtr<ID3D11RasterizerState> priorRaster,afterRaster;
        rejectionContext->OMGetDepthStencilState(&priorDepthState,&priorReference);
        rejectionContext->OMGetBlendState(&priorBlend,priorFactor,&priorMask);rejectionContext->RSGetState(&priorRaster);
        const auto vp=b.viewport();bool rejected=false;try{action();}catch(const Error&){rejected=true;}
        need(rejected,"Unsupported native Im2D request was accepted");need(b.im2dDrawCount()==initialCount,"Rejected draw changed counter");
        ComPtr<ID3D11Predicate> afterPredicate;BOOL afterPredicateValue{};
        rejectionContext->GetPredication(&afterPredicate,&afterPredicateValue);
        need(afterPredicate.Get()==priorPredicate.Get()&&afterPredicateValue==priorPredicateValue,"Rejected draw changed predication");
        rejectionContext->OMGetDepthStencilState(&afterDepthState,&afterReference);
        rejectionContext->OMGetBlendState(&afterBlend,afterFactor,&afterMask);rejectionContext->RSGetState(&afterRaster);
        need(priorDepthState.Get()==afterDepthState.Get()&&priorReference==afterReference&&
            priorBlend.Get()==afterBlend.Get()&&priorMask==afterMask&&!std::memcmp(priorFactor,afterFactor,sizeof(priorFactor))&&
            priorRaster.Get()==afterRaster.Get(),"Rejected draw changed depth/blend/raster state");
        // CopyResource itself is predicable. Observe actual storage with the
        // predicate temporarily disabled, only after checking it was retained.
        if(priorPredicate)rejectionContext->SetPredication(nullptr,FALSE);
        const auto afterBytes=b.readbackTarget(target);
        const auto afterDepthBytes=b.readbackDepthTarget(depth);
        if(priorPredicate)rejectionContext->SetPredication(priorPredicate.Get(),priorPredicateValue);
        if(afterBytes!=beforeReject)fprintf(stderr,"rejection %zu readback got %08X expected %08X\n",rejectionIndex,pixel(afterBytes,0),pixel(beforeReject,0));
        need(afterBytes==beforeReject,"Rejected draw changed target pixels");
        need(afterDepthBytes==beforeRejectDepth,"Rejected draw changed depth/stencil bytes");
        b.requireSelectedTargets({target,nullptr,nullptr,nullptr},selectedDepth);const auto after=b.viewport();
        need(vp.has_value()==after.has_value()&&(!vp||!std::memcmp(&*vp,&*after,sizeof(*vp))),"Rejected draw changed viewport");
    };
    auto bad=[&](auto&& change) {auto invalid=d;change(invalid);reject([&]{b.drawIm2D(target,depth,invalid);});};
    // The old primitive3/four-vertex rejection remains a partial-triplet case.
    bad([](auto& p){p.primitiveType=3;});bad([](auto& p){p.vertices.resize(2);});bad([](auto& p){p.vertices.resize(9363);});
    bad([](auto& p){p.primitiveType=2;});
    for(size_t count:{size_t(0),size_t(1),size_t(2),size_t(5),size_t(7),size_t(9361),size_t(9362),size_t(9363),size_t(9366)})
        bad([&](auto& p){p.primitiveType=3;p.vertices.resize(count,p.vertices[0]);});
    bad([](auto& p){p.primitiveType=3;p.vertices.resize(9360,p.vertices[0]);p.vertices.back().position[0]=NAN;});
    bad([](auto& p){p.rasterWidth=0;});bad([](auto& p){p.rasterHeight=16385;});
    bad([](auto& p){p.depthCompare=8;});bad([](auto& p){p.depthTest=true;p.depthCompare=8;});bad([](auto& p){p.stencil=true;});
    bad([](auto& p){p.blendWord=0x00012106;});bad([](auto& p){p.cullBits=1;});bad([](auto& p){p.cullBits=10;});bad([](auto& p){p.colorWriteMask=16;});
    bad([](auto& p){p.expandedBlend=2;});
    bad([](auto& p){p.expandedBlend=1;p.vertices[0].color[0]=-0.1f;});
    bad([](auto& p){p.expandedBlend=1;p.vertices[0].color[3]=1.1f;});
    bad([](auto& p){p.alphaTest=true;p.alphaCompare=8;});bad([](auto& p){p.alphaTest=true;p.alphaReference=NAN;});bad([](auto& p){p.alphaTest=true;p.alphaReference=-1;});
    for(size_t i=0;i<3;++i)bad([&](auto& p){p.vertices[2].position[i]=NAN;});
    for(size_t i=0;i<4;++i)bad([&](auto& p){p.vertices[2].color[i]=INFINITY;});
    bad([](auto& p){p.vertices[2].position[2]=1.1f;});bad([](auto& p){p.vertices[2].position[0]=65536;});
    reject([&]{b.drawIm2D({},depth,d);});reject([&]{b.drawIm2D(target,{},d);});
    auto otherTarget=b.createTarget(16,12,TargetFormat::RGB10A2);auto otherDepth=b.createDepthTarget(16,12);
    reject([&]{b.drawIm2D(otherTarget,depth,d);});reject([&]{b.drawIm2D(target,otherDepth,d);});
    auto selectedList=d;selectedList.primitiveType=3;selectedList.vertices.resize(3);
    reject([&]{b.drawIm2D(otherTarget,depth,selectedList);});reject([&]{b.drawIm2D(target,otherDepth,selectedList);});
    // Enabled testing requires the exact selected depth owner, not merely a
    // depth target with matching dimensions, and rejects before state mutation.
    d.depthTest=d.depthWrite=true;d.depthCompare=7;
    reject([&]{b.drawIm2D(target,otherDepth,d);});
    auto wrongExtent=b.createDepthTarget(8,6);reject([&]{b.drawIm2D(target,wrongExtent,d);});
    NativeBackend foreignDepthBackend(!hardware);auto foreignDepth=foreignDepthBackend.createDepthTarget(16,12);
    reject([&]{b.drawIm2D(target,foreignDepth,d);});
    selectedDepth.reset();b.bindTargets({target,nullptr,nullptr,nullptr},{});
    reject([&]{b.drawIm2D(target,{},d);});reject([&]{b.drawIm2D(target,depth,d);});
    selectedDepth=depth;b.bindTargets({target,nullptr,nullptr,nullptr},depth);
    // Actual OM disagreement is detected even when the submitted owner is valid.
    auto* selectionContext=NativeIm2DProbe::context(b);ComPtr<ID3D11RenderTargetView> selectedColor;
    selectionContext->OMGetRenderTargets(1,&selectedColor,nullptr);auto* selectedColorPtr=selectedColor.Get();
    selectionContext->OMSetRenderTargets(1,&selectedColorPtr,nullptr);selectedDepth.reset();
    reject([&]{b.drawIm2D(target,depth,d);});
    selectedDepth=depth;b.bindTargets({target,nullptr,nullptr,nullptr},depth);
    b.setViewport({0,0,16,12,0.25f,0.75f});reject([&]{b.drawIm2D(target,depth,d);});b.setViewport({0,0,16,12,0,1});
    d=quad();
    // Public metadata cannot turn a real non-RGB10A2 allocation into a valid
    // target, nor can a matching format substitute a different OM attachment.
    auto nonPacked=b.createTarget(16,12,TargetFormat::RGBA8);b.clearTarget(nonPacked,{0,0,0,0});
    b.bindTargets({nonPacked,nullptr,nullptr,nullptr},depth);const auto nonPackedBefore=b.readbackTarget(nonPacked);
    for(bool spoof:{false,true}) {
        nonPacked->format=spoof?TargetFormat::RGB10A2:TargetFormat::RGBA8;bool rejected=false;
        try{b.drawIm2D(nonPacked,depth,d);}catch(const Error&){rejected=true;}
        need(rejected&&b.im2dDrawCount()==initialCount,"Nonpacked target or spoofed metadata was accepted");
        need(b.readbackTarget(nonPacked)==nonPackedBefore,"Rejected nonpacked draw changed storage");
        b.requireSelectedTargets({nonPacked,nullptr,nullptr,nullptr},depth);
    }
    nonPacked->format=TargetFormat::RGBA8;b.bindTargets({target,nullptr,nullptr,nullptr},depth);
    NativeBackend foreign(!hardware);d.texture=foreign.createTexture(1,1,TextureFormat::RGBA8,std::array<uint8_t,4>{255,255,255,255});
    auto foreignList=d;foreignList.primitiveType=3;foreignList.vertices.resize(3);
    reject([&]{b.drawIm2D(target,depth,foreignList);});
    reject([&]{b.drawIm2D(target,depth,d);});d.texture=b.createTexture(2,1,TextureFormat::RGBA8,twoTexels);
    bad([](auto& p){p.vertices[0].uv[1]=NAN;});bad([](auto& p){p.sampler.Filter=D3D11_FILTER_ANISOTROPIC;});
    bad([](auto& p){p.sampler.AddressU=D3D11_TEXTURE_ADDRESS_MIRROR;});bad([](auto& p){p.sampler.MipLODBias=1;});
    bad([](auto& p){p.sampler.MinLOD=1;});bad([](auto& p){p.sampler.MaxLOD=NAN;});bad([](auto& p){p.sampler.ComparisonFunc=D3D11_COMPARISON_LESS;});
    d.texture.reset();
    // Complete immutable mip chains are admitted (hardware LOD, like the
    // original); their sampling is checked positively after the rejections.
    // BC2/BC3 admission must not admit BC1,
    // foreign owners, spoofed metadata, or previously unqualified sampler state.
    {
        std::vector<uint8_t> block(8,0xFF);
        d.texture=b.createTexture(4,4,TextureFormat::BC1,block);reject([&]{b.drawIm2D(target,depth,d);});
        need(b.readback(d.texture)==block,"Rejected compressed format modified source bytes");
    }
    for(auto format:{TextureFormat::BC2,TextureFormat::BC3}) {
    const auto& compressedBlock=format==TextureFormat::BC2?bc2FilterBlock:bc3FilterBlock;
    d.texture=foreign.createTexture(4,4,format,compressedBlock);
    reject([&]{b.drawIm2D(target,depth,d);});
    need(foreign.readback(d.texture)==std::vector<uint8_t>(compressedBlock.begin(),compressedBlock.end()),
        "Rejected foreign BC2/BC3 draw changed source bytes");
    d.texture=b.createTexture(4,4,format,compressedBlock);
    bad([](auto& p){p.sampler.Filter=D3D11_FILTER_ANISOTROPIC;});
    bad([](auto& p){p.sampler.MipLODBias=1;});bad([](auto& p){p.sampler.AddressV=D3D11_TEXTURE_ADDRESS_MIRROR;});
    d.texture->format=format==TextureFormat::BC2?TextureFormat::BC3:TextureFormat::BC2;
    reject([&]{b.drawIm2D(target,depth,d);});d.texture->format=format;
    d.texture->width=8;reject([&]{b.drawIm2D(target,depth,d);});d.texture->width=4;
    need(b.readback(d.texture)==std::vector<uint8_t>(compressedBlock.begin(),compressedBlock.end()),
        "Rejected BC2/BC3 profile changed source blocks");d.texture.reset();
    }
    const auto realViewport=*b.viewport();b.setViewport({0,0,0,12,0,1});reject([&]{b.drawIm2D(target,depth,d);});b.setViewport(realViewport);
    b.setViewport({0,0,17,12,0,1});reject([&]{b.drawIm2D(target,depth,d);});b.setViewport(realViewport);
    auto* ctx=NativeIm2DProbe::context(b);auto* device=NativeIm2DProbe::device(b);
    ctx->RSSetViewports(0,nullptr);reject([&]{b.drawIm2D(target,depth,d);});b.setViewport(realViewport);
    D3D11_TEXTURE2D_DESC uavDesc{};uavDesc.Width=uavDesc.Height=uavDesc.MipLevels=uavDesc.ArraySize=uavDesc.SampleDesc.Count=1;
    uavDesc.Format=DXGI_FORMAT_R32_UINT;uavDesc.Usage=D3D11_USAGE_DEFAULT;uavDesc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    ComPtr<ID3D11Texture2D> uavTexture;ComPtr<ID3D11UnorderedAccessView> uav;
    need(SUCCEEDED(device->CreateTexture2D(&uavDesc,nullptr,&uavTexture)),"UAV fixture allocation failed");
    need(SUCCEEDED(device->CreateUnorderedAccessView(uavTexture.Get(),nullptr,&uav)),"UAV fixture view failed");
    auto* uavPtr=uav.Get();ctx->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,1,1,&uavPtr,nullptr);
    reject([&]{b.drawIm2D(target,depth,d);});uavPtr=nullptr;
    ctx->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,1,1,&uavPtr,nullptr);
    D3D11_QUERY_DESC pd{D3D11_QUERY_OCCLUSION_PREDICATE,0};ComPtr<ID3D11Predicate> predicate;
    need(SUCCEEDED(device->CreatePredicate(&pd,&predicate)),"Predicate fixture allocation failed");ctx->SetPredication(predicate.Get(),TRUE);
    reject([&]{b.drawIm2D(target,depth,d);});ctx->SetPredication(nullptr,FALSE);
    D3D11_BUFFER_DESC soDesc{};soDesc.ByteWidth=64;soDesc.Usage=D3D11_USAGE_DEFAULT;soDesc.BindFlags=D3D11_BIND_STREAM_OUTPUT;
    ComPtr<ID3D11Buffer> so;need(SUCCEEDED(device->CreateBuffer(&soDesc,nullptr,&so)),"Stream-output fixture allocation failed");
    auto* soPtr=so.Get();UINT zero=0;ctx->SOSetTargets(1,&soPtr,&zero);reject([&]{b.drawIm2D(target,depth,d);});ctx->SOSetTargets(0,nullptr,nullptr);
    bool wrongThreadRejected=false;std::thread wrongThread([&]{try{b.drawIm2D(target,depth,d);}catch(const Error&){wrongThreadRejected=true;}});wrongThread.join();
    need(wrongThreadRejected&&b.im2dDrawCount()==initialCount,"Wrong-thread ownership was accepted");
    need(b.screenDrawCount()==0&&!b.presentationCount(),"Im2D submissions changed screen/presentation counters");
    need(b.readbackDepthTarget(depth)==beforeRejectDepth,"Rejected Im2D changed depth/stencil storage");
    // Disabled depth also supports an explicitly absent attachment, never a
    // substitute for a different selected depth target.
    b.bindTargets({target,nullptr,nullptr,nullptr},{});
    for(bool write:{false,true})for(uint32_t compare=0;compare<8;++compare) {
        d=quad();d.depthWrite=write;d.depthCompare=compare;b.clearTarget(target,{0,0,0,0});
        debugMessages(NativeIm2DProbe::device(b),true);b.drawIm2D(target,{},d);debugMessages(NativeIm2DProbe::device(b));
        b.requireSelectedTargets({target,nullptr,nullptr,nullptr},{});NativeIm2DProbe::depthState(b,d);NativeIm2DProbe::cleanTransient(b);
        uniform(b.readbackTarget(target),0xFFFFFFFFu,"Inactive retained write/compare with no DSV gated color");
        need(b.readbackDepthTarget(depth)==beforeRejectDepth,"Absent attachment draw modified detached depth");
    }
    // Complete immutable mip chains draw exactly like their level 0 when
    // magnified: hardware LOD clamps to MinLOD 0 under the admitted point-mip
    // filters. Their stored levels survive the draw.
    {
        const auto matchesLevelZero=[&](const std::shared_ptr<Texture>& level0,const std::shared_ptr<Texture>& chain,const char* what) {
            auto packet=quad();packet.texture=level0;b.clearTarget(target,{0,0,0,0});b.drawIm2D(target,{},packet);
            const auto expected=b.readbackTarget(target);
            packet.texture=chain;b.clearTarget(target,{0,0,0,0});b.drawIm2D(target,{},packet);
            need(b.readbackTarget(target)==expected,what);
        };
        const std::array<uint8_t,4> mip={1,2,3,4};const std::array<std::span<const uint8_t>,2> levels={twoTexels,mip};
        matchesLevelZero(b.createTexture(2,1,TextureFormat::RGBA8,twoTexels),b.createTextureMipChain(2,1,TextureFormat::RGBA8,levels),
            "Magnified RGBA8 Im2D mip chain did not sample level 0");
        const auto level0=std::span<const uint8_t>(bc2Blocks).first(64);
        const std::array<std::span<const uint8_t>,2> compressedLevels={level0,bc2FilterBlock};
        for(auto format:{TextureFormat::BC2,TextureFormat::BC3}) {
            const auto chain=b.createTextureMipChain(8,8,format,compressedLevels);
            matchesLevelZero(b.createTexture(8,8,format,level0),chain,"Magnified BC2/BC3 Im2D mip chain did not sample level 0");
            need(b.readbackMip(chain,1)==std::vector<uint8_t>(bc2FilterBlock.begin(),bc2FilterBlock.end()),
                "Im2D mip draw changed the immutable compressed storage");
        }
    }
    // Queue consecutive same-size owners and then resize the temporary packed
    // storage without a readback/idle between draws. Each target must retain
    // its own destination channels, including after the scratch pair is reused.
    std::vector<std::shared_ptr<RenderTarget>> queuedTargets;
    std::vector<uint32_t> queuedExpected;
    for(uint32_t width:{16u,16u,8u,17u,16u}) {
        const uint32_t height=width==17?7u:12u;
        auto queued=b.createTarget(width,height,TargetFormat::RGB10A2);
        const std::array<float,4> background={0,float(queuedTargets.size()*127)/1023,1,1};
        b.clearTarget(queued,background);b.bindTargets({queued,nullptr,nullptr,nullptr},{});
        b.setViewport({0,0,float(width),float(height),0,1});
        auto packet=quad(width,height);packet.colorWriteMask=1;color(packet,{1,0,0,0});
        b.drawIm2D(queued,{},packet);
        packet.blendWord=0x01000100;packet.colorWriteMask=15;color(packet,{0,0,0,0});
        b.drawIm2D(queued,{},packet);
        queuedExpected.push_back(pack(background)|1023u);queuedTargets.push_back(queued);
    }
    for(size_t i=0;i<queuedTargets.size();++i)
        uniform(b.readbackTarget(queuedTargets[i]),queuedExpected[i],"Queued Im2D target changed across scratch reuse or resize");
    // Queue enough independent depth outputs to cross multiple vertex-ring
    // generations, without any completion/readback between packets. Small
    // packets append, large packets force DISCARD; caller vertices are reused.
    std::vector<std::shared_ptr<DepthTarget>> queuedDepths;
    for(UINT count:{6u,3000u,3000u,9000u,6u,9360u,3000u,9000u,3000u}) {
        auto queued=b.createDepthTarget(16,12);b.clearDepthTarget(queued,1,93);
        b.bindTargets({target,nullptr,nullptr,nullptr},queued);b.setViewport({0,0,16,12,0,1});
        auto packet=quad();packet.primitiveType=3;packet.blendWord=0x01000100;
        packet.depthTest=packet.depthWrite=true;packet.depthCompare=7;
        zValue(packet,float(queuedDepths.size()+1)/16);
        const auto corners=packet.vertices;
        packet.vertices={corners[0],corners[1],corners[2],corners[2],corners[1],corners[3]};
        packet.vertices.resize(count,corners[0]);b.drawIm2D(target,queued,packet);
        for(auto& vertex:packet.vertices)vertex.position[2]=1;
        queuedDepths.push_back(queued);
    }
    for(size_t i=0;i<queuedDepths.size();++i)
        uniformDepth(b.readbackDepthTarget(queuedDepths[i]),float(i+1)/16,93);
    // Compare an interleaved grid of overlapping translucent quads against
    // independent one-triangle submissions in the exact original order. The
    // combined packet may group only spatially independent fragments. Test
    // depth rejection/write, alpha rejection, masks, textures and both centers.
    auto grouped=b.createTarget(192,96,TargetFormat::RGB10A2);
    auto serial=b.createTarget(192,96,TargetFormat::RGB10A2);
    auto groupedDepth=b.createDepthTarget(192,96),serialDepth=b.createDepthTarget(192,96);
    const std::array<uint8_t,16> texels={255,64,128,255,32,255,64,64,128,32,255,192,64,128,32,255};
    auto gridTexture=b.createTexture(2,2,TextureFormat::RGBA8,texels);
    for(UINT blend:{0x07060706u,0x00010706u,0x00010106u,0x00010186u})for(UINT variation=0;variation<8;++variation) {
        auto packet=quad(192,96);packet.primitiveType=3;packet.vertices.clear();packet.blendWord=blend;
        packet.depthTest=true;packet.depthWrite=(variation&1)!=0;packet.depthCompare=variation%3==0?7:variation%3==1?1:4;
        packet.reverseDepth=(variation&4)!=0;packet.pixelCenterHalf=(variation&2)!=0;
        packet.alphaTest=(variation&1)!=0;packet.alphaReference=.4f;
        packet.colorWriteMask=(variation&2)?11:15;packet.cullBits=(variation&4)?2:0;
        if(variation&1)packet.texture=gridTexture;
        for(UINT cell=0;cell<9;++cell)for(UINT pass=0;pass<2;++pass) {
            auto square=quad(20,12);zValue(square,pass?.3f:.7f);
            color(square,{float(cell%3+1)/4,float(pass+1)/3,float(cell/3+1)/4,pass?.75f:.25f});
            for(auto& vertex:square.vertices){vertex.position[0]+=float(cell%3*56+4);vertex.position[1]+=float(cell/3*28+4);}
            for(UINT index:{0u,1u,2u,2u,1u,3u})packet.vertices.push_back(square.vertices[index]);
        }
        b.clearTarget(grouped,{.25f,.5f,.75f,1});b.clearTarget(serial,{.25f,.5f,.75f,1});
        b.clearDepthTarget(groupedDepth,.5f,71);b.clearDepthTarget(serialDepth,.5f,71);
        b.bindTargets({grouped,nullptr,nullptr,nullptr},groupedDepth);b.setViewport({0,0,192,96,0,1});
        const auto copies=b.im2dColorCopyCount(),draws=b.im2dDrawCount();
        debugMessages(NativeIm2DProbe::device(b),true);b.drawIm2D(grouped,groupedDepth,packet);
        debugMessages(NativeIm2DProbe::device(b));
        need(b.im2dDrawCount()==draws+1,"Layered rendering did not submit exactly one original packet");
        need(b.im2dColorCopyCount()-copies<=8,"Independent grid triangles retained per-triangle color copies");
        b.bindTargets({serial,nullptr,nullptr,nullptr},serialDepth);
        for(size_t first=0;first<packet.vertices.size();first+=3) {
            auto triangle=packet;triangle.vertices.assign(packet.vertices.begin()+first,packet.vertices.begin()+first+3);
            b.drawIm2D(serial,serialDepth,triangle);
        }
        need(b.readbackTarget(grouped)==b.readbackTarget(serial),"Layered color differs from original triangle order");
        const auto actual=b.readbackDepthTarget(groupedDepth),expected=b.readbackDepthTarget(serialDepth);
        for(size_t pixel=0;pixel<192*96;++pixel) {
            need(depthBits(actual,pixel)==depthBits(expected,pixel),"Layered depth differs from original triangle order");
            need(actual[pixel*8+4]==expected[pixel*8+4],"Layered rendering changed stencil");
        }
        NativeIm2DProbe::cleanTransient(b);
    }
    batchEquivalence(b);colorBatchEquivalence(b);b.waitIdle();
    printf("PASS: %zu Im2D backend checks on %s; real native GPU fixtures, console precision/coverage parity unverified\n",checks,hardware?"hardware":"WARP");return 0;
} catch(const std::exception& e) {fprintf(stderr,"FAIL after %zu checks: %s\n",checks,e.what());return 1;}}
