// Standalone real D3D11 pixel-output test. Main supplies OFFLINE FXC headers:
// PSShadowDepthAlpha.h / kPSShadowDepthAlpha (ps_5_0) and
// VSShadowAlphaProbe.h / kVSShadowAlphaProbe (vs_5_0), both from
// renderer/shadow_alpha_shader.hlsl. Link d3d11 + dxgi; no runtime compiler.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include "PSShadowDepthAlpha.h"
#include "VSShadowAlphaProbe.h"

using Microsoft::WRL::ComPtr;
namespace {
using Color = std::array<float,4>;
using Pixels = std::array<Color,4>;
using Alpha = std::array<float,4>;
using Constants = std::array<Color,42>;
struct Vertex { Color position; std::array<float,2> uv; };
static_assert(sizeof(Vertex)==24 && sizeof(Constants)==672);
size_t checks{}, draws{};
void need(bool condition,const char* why) {
    ++checks;
    if(!condition) throw std::runtime_error(why);
}
void hr(HRESULT value,const char* why) {
    if(FAILED(value)) {
        char message[192];
        sprintf_s(message,"%s: %08lX",why,static_cast<unsigned long>(value));
        throw std::runtime_error(message);
    }
}
struct Texture {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
};
struct Fixture {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vertices, constants;
    ComPtr<ID3D11Texture2D> target, staging;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11BlendState> blend;

    explicit Fixture(bool hardware) {
        D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_11_0};
        hr(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,
            nullptr,0,requested,1,D3D11_SDK_VERSION,&device,&level,&context),"create device");
        need(level==D3D_FEATURE_LEVEL_11_0,"Shader Model 5 device required");
        ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC description{};
        hr(device.As(&dxgi),"DXGI device"); hr(dxgi->GetAdapter(&adapter),"adapter");
        hr(adapter->GetDesc(&description),"adapter description");
        std::printf("Shadow alpha PS: %s, %ls, feature 11.0\n",
                    hardware?"hardware":"WARP",description.Description);
        hr(device->CreateVertexShader(kVSShadowAlphaProbe,sizeof(kVSShadowAlphaProbe),nullptr,&vs),"probe VS");
        hr(device->CreatePixelShader(kPSShadowDepthAlpha,sizeof(kPSShadowDepthAlpha),nullptr,&ps),"original-output PS");
        const D3D11_INPUT_ELEMENT_DESC inputs[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
        hr(device->CreateInputLayout(inputs,2,kVSShadowAlphaProbe,sizeof(kVSShadowAlphaProbe),&layout),"probe layout");
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth=UINT(3*sizeof(Vertex)); buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        hr(device->CreateBuffer(&buffer,nullptr,&vertices),"fullscreen vertices");
        buffer.ByteWidth=UINT(sizeof(Constants)); buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        hr(device->CreateBuffer(&buffer,nullptr,&constants),"42 pixel constants");
        D3D11_TEXTURE2D_DESC td{};
        td.Width=td.Height=2; td.MipLevels=td.ArraySize=1; td.SampleDesc.Count=1;
        td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; td.BindFlags=D3D11_BIND_RENDER_TARGET;
        hr(device->CreateTexture2D(&td,nullptr,&target),"float color target");
        hr(device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"color view");
        td.BindFlags=0; td.Usage=D3D11_USAGE_STAGING; td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        hr(device->CreateTexture2D(&td,nullptr,&staging),"pixel readback");
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;
        hr(device->CreateRasterizerState(&rd,&raster),"fixture rasterizer");
        // Observe all four original color outputs, including alpha zero.
        // There is no DSV, alpha-to-coverage, alpha test or depth conversion.
        D3D11_DEPTH_STENCIL_DESC dd{};
        dd.DepthEnable=FALSE; dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
        dd.DepthFunc=D3D11_COMPARISON_ALWAYS;
        hr(device->CreateDepthStencilState(&dd,&depth),"disable depth/stencil");
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        hr(device->CreateBlendState(&bd,&blend),"replace all color lanes");
    }

    Texture texture(const Alpha& alpha) {
        // RGB are deliberately unrelated to alpha, exposing channel mistakes.
        const Pixels texels={Color{3,-4,7,alpha[0]},Color{5,8,-2,alpha[1]},
                             Color{-6,9,4,alpha[2]},Color{11,-3,6,alpha[3]}};
        D3D11_TEXTURE2D_DESC td{};
        td.Width=td.Height=2; td.MipLevels=td.ArraySize=1; td.SampleDesc.Count=1;
        td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; td.Usage=D3D11_USAGE_IMMUTABLE;
        td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA initial{texels.data(),UINT(2*sizeof(Color)),0};
        Texture result;
        hr(device->CreateTexture2D(&td,&initial,&result.texture),"exact 2x2 alpha texture");
        hr(device->CreateShaderResourceView(result.texture.Get(),nullptr,&result.view),"texture0 view");
        return result;
    }

    Pixels draw(const Texture* texture,float predicate,float scaleU,float scaleV,float offsetU,float offsetV,
                D3D11_TEXTURE_ADDRESS_MODE addressU,D3D11_TEXTURE_ADDRESS_MODE addressV,bool linear) {
        // Oversized triangle covers every 2x2 pixel center without a diagonal.
        const std::array<Vertex,3> input={Vertex{{-1,1,0.5f,1},{offsetU,offsetV}},
            Vertex{{3,1,0.5f,1},{offsetU+2*scaleU,offsetV}},
            Vertex{{-1,-3,0.5f,1},{offsetU,offsetV+2*scaleV}}};
        Constants bank{};
        // Poison every unused register/lane; c40.x and c41.yzw are nonzero.
        for(size_t i=0;i<bank.size();++i)
            bank[i]={float(i+3),-float(i+5),float(i+7),-float(i+11)};
        bank[41][0]=predicate;
        context->UpdateSubresource(vertices.Get(),0,nullptr,input.data(),0,0);
        context->UpdateSubresource(constants.Get(),0,nullptr,bank.data(),0,0);
        D3D11_SAMPLER_DESC sd{};
        sd.Filter=linear?D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT:D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU=addressU; sd.AddressV=addressV; sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxAnisotropy=1; sd.ComparisonFunc=D3D11_COMPARISON_NEVER; sd.MaxLOD=D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        hr(device->CreateSamplerState(&sd,&sampler),"external sampler");
        const Color untouched={-17,-19,-23,-29};
        context->ClearRenderTargetView(rtv.Get(),untouched.data());
        auto* color=rtv.Get(); context->OMSetRenderTargets(1,&color,nullptr);
        context->OMSetDepthStencilState(depth.Get(),0);
        context->OMSetBlendState(blend.Get(),nullptr,0xFFFFFFFF);
        const D3D11_VIEWPORT viewport{0,0,2,2,0,1};
        context->RSSetViewports(1,&viewport); context->RSSetState(raster.Get());
        context->IASetInputLayout(layout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        auto* vb=vertices.Get(); const UINT stride=UINT(sizeof(Vertex)),offset=0;
        context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        context->VSSetShader(vs.Get(),nullptr,0); context->PSSetShader(ps.Get(),nullptr,0);
        auto* cb=constants.Get(); context->PSSetConstantBuffers(0,1,&cb);
        auto* srv=texture?texture->view.Get():nullptr; context->PSSetShaderResources(0,1,&srv);
        auto* state=texture?sampler.Get():nullptr; context->PSSetSamplers(0,1,&state);
        context->Draw(3,0); ++draws;
        context->CopyResource(staging.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        // Blocking staging Map waits for the actual draw/copy to complete.
        hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"read actual PS pixels");
        Pixels pixels{};
        for(size_t y=0;y<2;++y)
            std::memcpy(pixels.data()+2*y,static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch,2*sizeof(Color));
        context->Unmap(staging.Get(),0);
        return pixels;
    }
};

void verify(const Pixels& pixels,const Alpha& expected,const char* name,float predicate) {
    for(size_t pixel=0;pixel<4;++pixel) for(size_t lane=0;lane<4;++lane) {
        const float got=pixels[pixel][lane];
        // All point samples and chosen bilinear averages are exact binary
        // fractions. No shader emulator, register machine or loose tolerance.
        if(!std::isfinite(got) || got!=expected[pixel]) {
            char message[256];
            sprintf_s(message,"%s predicate %.9g pixel %zu lane %zu: got %.9g expected %.9g",
                      name,predicate,pixel,lane,got,expected[pixel]);
            throw std::runtime_error(message);
        }
        ++checks;
    }
}
struct Case {
    const char* name;
    float scaleU,scaleV,offsetU,offsetV;
    D3D11_TEXTURE_ADDRESS_MODE u,v;
    bool linear;
    Alpha expected;
};
}

int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    try {
        need(argc==1 || (argc==2 && std::string(argv[1])=="--hardware"),"Use no arguments (WARP) or --hardware");
        Fixture fixture(argc==2);
        const Alpha base={0,0.25f,0.75f,1},ones={1,1,1,1};
        const auto texture=fixture.texture(base);
        constexpr auto wrap=D3D11_TEXTURE_ADDRESS_WRAP,clamp=D3D11_TEXTURE_ADDRESS_CLAMP;
        // Hand-enumerated expected pixels, row-major; no sampling emulator.
        const Case cases[]={
            {"normalized centers / wrap",1,1,0,0,wrap,wrap,false,base},
            {"normalized centers / clamp",1,1,0,0,clamp,clamp,false,base},
            {"outside both axes / wrap",1,1,1,-1,wrap,wrap,false,base},
            {"outside both axes / clamp",1,1,1,-1,clamp,clamp,false,{0.25f,0.25f,0.25f,0.25f}},
            {"clamp U, wrap V",1,1,1,-1,clamp,wrap,false,{0.25f,0.25f,1,1}},
            {"wrap U, clamp V",1,1,1,-1,wrap,clamp,false,{0,0.25f,0,0.25f}},
            {"negative U, positive V / clamp",1,1,-1,1,clamp,clamp,false,{0.75f,0.75f,0.75f,0.75f}},
            {"reversed UV",-1,-1,1,1,wrap,wrap,false,{1,0.75f,0.25f,0}},
            {"linear center",0,0,0.5f,0.5f,clamp,clamp,true,{0.5f,0.5f,0.5f,0.5f}},
            {"linear left edge / clamp",0,0,0,0.5f,clamp,clamp,true,{0.375f,0.375f,0.375f,0.375f}},
            {"linear left edge / wrap",0,0,0,0.5f,wrap,wrap,true,{0.5f,0.5f,0.5f,0.5f}},
            {"linear top edge / clamp",0,0,0.5f,0,clamp,clamp,true,{0.125f,0.125f,0.125f,0.125f}}};
        const float predicates[]={0.0f,-0.0f,1.0f,-2.5f,
            std::numeric_limits<float>::min(),-std::numeric_limits<float>::min()};
        for(float predicate:predicates) for(const auto& test:cases) {
            const auto pixels=fixture.draw(&texture,predicate,test.scaleU,test.scaleV,test.offsetU,test.offsetV,
                                           test.u,test.v,test.linear);
            verify(pixels,predicate!=0?test.expected:ones,test.name,predicate);
        }
        // The constant branch must also work with texture0/sampler0 unbound.
        for(float predicate:{0.0f,-0.0f})
            verify(fixture.draw(nullptr,predicate,1,1,0,0,wrap,wrap,false),ones,"unbound constant branch",predicate);
        // Original export has no saturation; negative / >1 alpha must survive.
        const Alpha extended={-0.5f,1.25f,0.5f,2};
        const auto unclamped=fixture.texture(extended);
        for(float predicate:{0.0f,1.0f,-1.0f})
            verify(fixture.draw(&unclamped,predicate,1,1,0,0,clamp,clamp,false),
                   predicate!=0?extended:ones,"unsaturated alpha",predicate);
        std::printf("PASS shadow alpha: %zu actual draws, %zu exact RGBA lane checks, %zu total checks\n",
                    draws,draws*16,checks);
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL shadow alpha: %s\n",error.what());
        return 1;
    }
}
