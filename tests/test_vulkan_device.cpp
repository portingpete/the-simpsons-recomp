// Conformance of the native Vulkan device behind the D3D11 interface subset:
// depth/stencil clear and readback, vertex- and pixel-shader depth writes,
// color output, indexed strips with strip cuts, negative base vertices,
// dynamic-buffer renaming, deferred command lists, texture sampling, state
// clears, long batches recorded on the command-recording thread, and teardown
// of a healthy and a lost device with recorded calls that were never submitted.
// Pixels are read back and compared exactly; no window is created.
#include "renderer/vulkan/vulkan_device_factory.h"
#include "renderer/vulkan/d3d11_vulkan.h"
#include <d3d11_1.h>
#include <wrl/client.h>
#include "VSVkFixtureFullscreen.h"
#include "VSVkFixturePosition.h"
#include "PSVkFixtureColor.h"
#include "PSVkFixtureUv.h"
#include "PSVkFixtureDepth.h"
#include "PSVkFixturePositionDepth.h"
#include "PSVkFixtureSample.h"
#include "PSVkFixtureReverse.h"
#include "VSShadowDepth.h"
#include "PSShadowMeshDepth.h"
#include <array>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
size_t checks{};
void need(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
void hr(HRESULT result,const char* message) {
    if(FAILED(result)) {char text[256];std::snprintf(text,sizeof(text),"%s: 0x%08lX",message,ULONG(result));throw std::runtime_error(text);}
    ++checks;
}
struct Constants {std::array<float,4> color;float depth,pixelDepth,padding[2];};
constexpr UINT side=8;

struct Fixture {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> color,depth;
    ComPtr<ID3D11RenderTargetView> colorView;
    ComPtr<ID3D11DepthStencilView> depthView;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11VertexShader> fullscreen,positioned;
    ComPtr<ID3D11PixelShader> colorShader,uvShader,depthShader,positionDepthShader,sampleShader;
    ComPtr<ID3D11DepthStencilState> writeAlways;
    ComPtr<ID3D11RasterizerState> noCull;
    Fixture() {
        D3D_FEATURE_LEVEL level{};
        hr(Simpsons::Graphics::Vulkan::createDevice(&device,&level,&context),"Vulkan device creation");
        need(level==D3D_FEATURE_LEVEL_11_1,"Vulkan device feature level");
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=side;desc.MipLevels=desc.ArraySize=1;desc.SampleDesc={1,0};
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        hr(device->CreateTexture2D(&desc,nullptr,&color),"color target");hr(device->CreateRenderTargetView(color.Get(),nullptr,&colorView),"color view");
        desc.Format=DXGI_FORMAT_R32G8X24_TYPELESS;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
        hr(device->CreateTexture2D(&desc,nullptr,&depth),"depth target");
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};dsv.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;dsv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
        hr(device->CreateDepthStencilView(depth.Get(),&dsv,&depthView),"depth view");
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(Constants);cb.Usage=D3D11_USAGE_DYNAMIC;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        hr(device->CreateBuffer(&cb,nullptr,&constants),"constant buffer");
        hr(device->CreateVertexShader(kVSVkFixtureFullscreen,sizeof(kVSVkFixtureFullscreen),nullptr,&fullscreen),"fullscreen VS");
        hr(device->CreateVertexShader(kVSVkFixturePosition,sizeof(kVSVkFixturePosition),nullptr,&positioned),"position VS");
        hr(device->CreatePixelShader(kPSVkFixtureColor,sizeof(kPSVkFixtureColor),nullptr,&colorShader),"color PS");
        hr(device->CreatePixelShader(kPSVkFixtureUv,sizeof(kPSVkFixtureUv),nullptr,&uvShader),"uv PS");
        hr(device->CreatePixelShader(kPSVkFixtureDepth,sizeof(kPSVkFixtureDepth),nullptr,&depthShader),"depth PS");
        hr(device->CreatePixelShader(kPSVkFixturePositionDepth,sizeof(kPSVkFixturePositionDepth),nullptr,&positionDepthShader),"position depth PS");
        hr(device->CreatePixelShader(kPSVkFixtureSample,sizeof(kPSVkFixtureSample),nullptr,&sampleShader),"sample PS");
        D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_ALWAYS;
        hr(device->CreateDepthStencilState(&ds,&writeAlways),"depth state");
        D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;
        hr(device->CreateRasterizerState(&rs,&noCull),"raster state");
    }
    void upload(const Constants& value) {
        D3D11_MAPPED_SUBRESOURCE mapped{};hr(context->Map(constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"constant map");
        std::memcpy(mapped.pData,&value,sizeof(value));context->Unmap(constants.Get(),0);
    }
    void bindCommon(ID3D11DeviceContext* target,bool withColor) {
        ID3D11RenderTargetView* view=withColor?colorView.Get():nullptr;
        target->OMSetRenderTargets(withColor?1:0,&view,depthView.Get());
        const D3D11_VIEWPORT viewport{0,0,float(side),float(side),0,1};target->RSSetViewports(1,&viewport);
        target->RSSetState(noCull.Get());target->OMSetDepthStencilState(writeAlways.Get(),0);
        auto* cb=constants.Get();target->VSSetConstantBuffers(0,1,&cb);target->PSSetConstantBuffers(0,1,&cb);
    }
    std::vector<uint8_t> readColor() {
        D3D11_TEXTURE2D_DESC desc{};color->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;hr(device->CreateTexture2D(&desc,nullptr,&staging),"color staging");
        context->CopyResource(staging.Get(),color.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"color map");
        std::vector<uint8_t> out(side*side*4);
        for(UINT y=0;y<side;++y) std::memcpy(out.data()+y*side*4,static_cast<uint8_t*>(mapped.pData)+y*mapped.RowPitch,side*4);
        context->Unmap(staging.Get(),0);return out;
    }
    // D32S8 texels as D3D lays them out: float depth then the stencil byte.
    std::vector<std::pair<float,uint8_t>> readDepth() {
        D3D11_TEXTURE2D_DESC desc{};depth->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;hr(device->CreateTexture2D(&desc,nullptr,&staging),"depth staging");
        context->CopyResource(staging.Get(),depth.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"depth map");
        need(mapped.RowPitch>=side*8,"depth staging row pitch");
        std::vector<std::pair<float,uint8_t>> out;
        for(UINT y=0;y<side;++y) for(UINT x=0;x<side;++x) {
            const uint8_t* texel=static_cast<uint8_t*>(mapped.pData)+y*mapped.RowPitch+x*8;
            float value{};std::memcpy(&value,texel,4);out.emplace_back(value,texel[4]);
        }
        context->Unmap(staging.Get(),0);return out;
    }
};
void allDepth(Fixture& f,float expected,uint8_t stencil,const char* message) {
    for(const auto& [value,s]:f.readDepth()) {
        if(value!=expected || s!=stencil) std::fprintf(stderr,"depth %08X stencil %02X expected %08X/%02X\n",
            std::bit_cast<uint32_t>(value),s,std::bit_cast<uint32_t>(expected),stencil);
        need(value==expected && s==stencil,message);
    }
}
void allColor(Fixture& f,std::array<uint8_t,4> expected,const char* message) {
    const auto pixels=f.readColor();
    for(size_t i=0;i<pixels.size();i+=4) need(std::memcmp(pixels.data()+i,expected.data(),4)==0,message);
}

void depthClearReadback(Fixture& f) {
    f.context->ClearDepthStencilView(f.depthView.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,0.25f,0x5A);
    allDepth(f,0.25f,0x5A,"Depth/stencil clear readback differs");
    f.context->ClearDepthStencilView(f.depthView.Get(),D3D11_CLEAR_DEPTH,0.5f,0);
    allDepth(f,0.5f,0x5A,"Depth-only clear changed stencil");
}
void vertexDepth(Fixture& f) {
    f.context->ClearDepthStencilView(f.depthView.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,0.0f,0x11);
    const float clear[4]{0,0,0,0};f.context->ClearRenderTargetView(f.colorView.Get(),clear);
    f.upload({{1,0,0.2f,1},0.5f,0,{}});
    f.bindCommon(f.context.Get(),true);
    f.context->IASetInputLayout(nullptr);f.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    f.context->VSSetShader(f.fullscreen.Get(),nullptr,0);f.context->PSSetShader(f.colorShader.Get(),nullptr,0);
    f.context->Draw(3,0);
    allColor(f,{255,0,51,255},"Fullscreen color differs");
    allDepth(f,0.5f,0x11,"Vertex depth write differs");
}
void pixelDepth(Fixture& f) {
    f.context->ClearDepthStencilView(f.depthView.Get(),D3D11_CLEAR_DEPTH,0.0f,0);
    f.upload({{0,0,0,0},0.125f,0.75f,{}});
    f.bindCommon(f.context.Get(),false);
    f.context->VSSetShader(f.fullscreen.Get(),nullptr,0);f.context->PSSetShader(f.depthShader.Get(),nullptr,0);
    f.context->Draw(3,0);
    allDepth(f,0.75f,0x11,"Pixel SV_Depth write differs");
    f.context->ClearDepthStencilView(f.depthView.Get(),D3D11_CLEAR_DEPTH,0.0f,0);
    f.upload({{0,0,0,0},0.25f,0,{}});
    f.context->PSSetShader(f.positionDepthShader.Get(),nullptr,0);
    f.context->Draw(3,0);
    allDepth(f,0.75f,0x11,"SV_Position.z depth differs");
}
void indexedStrips(Fixture& f) {
    const float clear[4]{0,0,0,0};f.context->ClearRenderTargetView(f.colorView.Get(),clear);
    // Two strips separated by the R16 cut index; the middle columns stay clear.
    struct Vertex {float x,y,u,v;};
    const Vertex vertices[]={{-1,1,0,0},{-0.5f,1,0,0},{-1,-1,0,0},{-0.5f,-1,0,0},{0.5f,1,0,0},{1,1,0,0},{0.5f,-1,0,0},{1,-1,0,0},
        {-0.5f,1,0,0},{0.5f,1,0,0}};
    const uint16_t indices[]={0,1,2,3,0xFFFF,4,5,6,7};
    D3D11_BUFFER_DESC vb{};vb.ByteWidth=sizeof(vertices);vb.Usage=D3D11_USAGE_IMMUTABLE;vb.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vd{vertices,0,0};ComPtr<ID3D11Buffer> vertexBuffer;hr(f.device->CreateBuffer(&vb,&vd,&vertexBuffer),"strip vertices");
    D3D11_BUFFER_DESC ib{};ib.ByteWidth=sizeof(indices);ib.Usage=D3D11_USAGE_IMMUTABLE;ib.BindFlags=D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA id{indices,0,0};ComPtr<ID3D11Buffer> indexBuffer;hr(f.device->CreateBuffer(&ib,&id,&indexBuffer),"strip indices");
    const D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}};
    ComPtr<ID3D11InputLayout> layout;hr(f.device->CreateInputLayout(elements,2,kVSVkFixturePosition,sizeof(kVSVkFixturePosition),&layout),"strip layout");
    f.upload({{0,1,0,1},0.5f,0,{}});
    f.bindCommon(f.context.Get(),true);
    const UINT stride=sizeof(Vertex),offset=0;auto* raw=vertexBuffer.Get();
    f.context->IASetVertexBuffers(0,1,&raw,&stride,&offset);f.context->IASetIndexBuffer(indexBuffer.Get(),DXGI_FORMAT_R16_UINT,0);
    f.context->IASetInputLayout(layout.Get());f.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    f.context->VSSetShader(f.positioned.Get(),nullptr,0);f.context->PSSetShader(f.colorShader.Get(),nullptr,0);
    f.context->DrawIndexed(9,0,0);
    auto pixels=f.readColor();
    for(UINT y=0;y<side;++y) for(UINT x=0;x<side;++x) {
        const bool covered=x<2 || x>=6;
        need(pixels[(y*side+x)*4+1]==(covered?255:0),"Indexed strip cut coverage differs");
    }
    // A negative base vertex addresses the same attributes as D3D11.
    const uint16_t shifted[]={2,3,4,5};
    D3D11_SUBRESOURCE_DATA sd{shifted,0,0};ib.ByteWidth=sizeof(shifted);
    ComPtr<ID3D11Buffer> shiftedBuffer;hr(f.device->CreateBuffer(&ib,&sd,&shiftedBuffer),"shifted indices");
    f.context->ClearRenderTargetView(f.colorView.Get(),clear);
    f.context->IASetIndexBuffer(shiftedBuffer.Get(),DXGI_FORMAT_R16_UINT,0);
    f.context->DrawIndexed(4,0,-2);
    pixels=f.readColor();
    for(UINT y=0;y<side;++y) for(UINT x=0;x<side;++x) need(pixels[(y*side+x)*4+1]==(x<2?255:0),"Negative base vertex coverage differs");
}
void deferredReplay(Fixture& f) {
    const float clear[4]{0,0,0,0};f.context->ClearRenderTargetView(f.colorView.Get(),clear);
    ComPtr<ID3D11DeviceContext> deferred;hr(f.device->CreateDeferredContext(0,&deferred),"deferred context");
    need(deferred->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED,"Deferred context type");
    f.upload({{0,0,1,1},0.5f,0,{}});
    f.bindCommon(deferred.Get(),true);
    deferred->IASetInputLayout(nullptr);deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    deferred->VSSetShader(f.fullscreen.Get(),nullptr,0);deferred->PSSetShader(f.colorShader.Get(),nullptr,0);
    deferred->Draw(3,0);
    ComPtr<ID3D11CommandList> list;hr(deferred->FinishCommandList(FALSE,&list),"finish command list");
    ComPtr<ID3D11VertexShader> before;f.context->VSSetShader(f.positioned.Get(),nullptr,0);
    f.context->ExecuteCommandList(list.Get(),TRUE);
    f.context->VSGetShader(&before,nullptr,nullptr);
    need(before.Get()==f.positioned.Get(),"ExecuteCommandList did not restore immediate state");
    allColor(f,{0,0,255,255},"Deferred replay color differs");
    f.context->ClearRenderTargetView(f.colorView.Get(),clear);
    f.context->ExecuteCommandList(list.Get(),FALSE);
    ComPtr<ID3D11VertexShader> cleared;f.context->VSGetShader(&cleared,nullptr,nullptr);
    need(!cleared,"ExecuteCommandList(FALSE) left immediate state");
    allColor(f,{0,0,255,255},"Repeated deferred replay differs");
}
void dynamicRename(Fixture& f) {
    // Two draws with one DISCARD between them: each keeps its own constants.
    const float clear[4]{0,0,0,0};f.context->ClearRenderTargetView(f.colorView.Get(),clear);
    f.bindCommon(f.context.Get(),true);
    f.context->IASetInputLayout(nullptr);f.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    f.context->VSSetShader(f.fullscreen.Get(),nullptr,0);f.context->PSSetShader(f.colorShader.Get(),nullptr,0);
    D3D11_RECT left{0,0,4,8},right{4,0,8,8};
    ComPtr<ID3D11RasterizerState> scissor;D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;
    rs.DepthClipEnable=TRUE;rs.ScissorEnable=TRUE;hr(f.device->CreateRasterizerState(&rs,&scissor),"scissor state");
    f.context->RSSetState(scissor.Get());
    f.upload({{1,0,0,1},0.5f,0,{}});f.context->RSSetScissorRects(1,&left);f.context->Draw(3,0);
    f.upload({{0,1,0,1},0.5f,0,{}});f.context->RSSetScissorRects(1,&right);f.context->Draw(3,0);
    const auto pixels=f.readColor();
    for(UINT y=0;y<side;++y) for(UINT x=0;x<side;++x) {
        const uint8_t* p=pixels.data()+(y*side+x)*4;
        need(x<4?(p[0]==255&&p[1]==0):(p[0]==0&&p[1]==255),"Renamed constant buffer leaked between draws");
    }
}
// ID3D11DeviceContext1 constant offsets (the renderer's constant arena):
// each draw reads the 256-byte window it was bound with, NO_OVERWRITE keeps
// earlier windows intact.
void constantOffsets(Fixture& f) {
    ComPtr<ID3D11DeviceContext1> context1;hr(f.context.As(&context1),"ID3D11DeviceContext1");
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=4096;desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;ComPtr<ID3D11Buffer> arena;hr(f.device->CreateBuffer(&desc,nullptr,&arena),"constant arena");
    const float clear[4]{0,0,0,0};f.context->ClearRenderTargetView(f.colorView.Get(),clear);
    f.bindCommon(f.context.Get(),true);
    f.context->IASetInputLayout(nullptr);f.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    f.context->VSSetShader(f.fullscreen.Get(),nullptr,0);f.context->PSSetShader(f.colorShader.Get(),nullptr,0);
    ComPtr<ID3D11RasterizerState> scissor;D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;
    rs.DepthClipEnable=TRUE;rs.ScissorEnable=TRUE;hr(f.device->CreateRasterizerState(&rs,&scissor),"arena scissor state");
    f.context->RSSetState(scissor.Get());
    for(UINT window=0;window<4;++window) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr(f.context->Map(arena.Get(),0,window?D3D11_MAP_WRITE_NO_OVERWRITE:D3D11_MAP_WRITE_DISCARD,0,&mapped),"arena map");
        const Constants value{{float(window==0),float(window==1),float(window==2),1},0.5f,0,{}};
        std::memcpy(static_cast<uint8_t*>(mapped.pData)+window*256,&value,sizeof(value));
        f.context->Unmap(arena.Get(),0);
        const UINT first=window*16,count=16;auto* raw=arena.Get();
        context1->VSSetConstantBuffers1(0,1,&raw,&first,&count);context1->PSSetConstantBuffers1(0,1,&raw,&first,&count);
        const D3D11_RECT column{LONG(window*2),0,LONG(window*2+2),LONG(side)};f.context->RSSetScissorRects(1,&column);
        f.context->Draw(3,0);
    }
    const auto pixels=f.readColor();
    for(UINT y=0;y<side;++y) for(UINT x=0;x<side;++x) {
        const UINT window=x/2;const uint8_t* p=pixels.data()+(y*side+x)*4;
        need(p[0]==(window==0?255:0) && p[1]==(window==1?255:0) && p[2]==(window==2?255:0) && p[3]==255,"Constant buffer offset window differs");
    }
    UINT first[1]{},count[1]{};ComPtr<ID3D11Buffer> bound;context1->PSGetConstantBuffers1(0,1,&bound,first,count);
    need(bound.Get()==arena.Get() && first[0]==48 && count[0]==16,"Constant buffer offset readback differs");
}
void sampling(Fixture& f) {
    const uint8_t texels[]={255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=2;desc.MipLevels=desc.ArraySize=1;desc.SampleDesc={1,0};
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{texels,8,0};ComPtr<ID3D11Texture2D> texture;hr(f.device->CreateTexture2D(&desc,&data,&texture),"sample texture");
    ComPtr<ID3D11ShaderResourceView> view;hr(f.device->CreateShaderResourceView(texture.Get(),nullptr,&view),"sample view");
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD=D3D11_FLOAT32_MAX;ComPtr<ID3D11SamplerState> sampler;hr(f.device->CreateSamplerState(&sd,&sampler),"point sampler");
    f.bindCommon(f.context.Get(),true);
    f.context->IASetInputLayout(nullptr);f.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    f.context->VSSetShader(f.fullscreen.Get(),nullptr,0);f.context->PSSetShader(f.sampleShader.Get(),nullptr,0);
    auto* raw=view.Get();f.context->PSSetShaderResources(0,1,&raw);auto* s=sampler.Get();f.context->PSSetSamplers(0,1,&s);
    f.context->Draw(3,0);
    const auto pixels=f.readColor();
    for(UINT y=0;y<side;++y) for(UINT x=0;x<side;++x) {
        // uv spans the 2x2 texture across the target, top row first.
        const uint8_t* expected=texels+((y>=4?2:0)+(x>=4?1:0))*4;
        need(std::memcmp(pixels.data()+(y*side+x)*4,expected,4)==0,"Point-sampled texel orientation differs");
    }
    ID3D11ShaderResourceView* none=nullptr;f.context->PSSetShaderResources(0,1,&none);
}
// The shadow-depth adapters as NativeBackend uses them: the original-
// transcribed VS with identity banks, and the 20e4 depth PS (reverse depth).
void shadowAdapters(Fixture& f) {
    ComPtr<ID3D11VertexShader> shadowVertex;ComPtr<ID3D11PixelShader> shadowPixel;
    hr(f.device->CreateVertexShader(kVSShadowDepth,sizeof(kVSShadowDepth),nullptr,&shadowVertex),"shadow VS");
    hr(f.device->CreatePixelShader(kPSShadowMeshDepth,sizeof(kPSShadowMeshDepth),nullptr,&shadowPixel),"shadow PS");
    const std::array<uint32_t,4> depthConstants{1,0,0,0};
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=16;cb.Usage=D3D11_USAGE_IMMUTABLE;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA cd{depthConstants.data(),0,0};ComPtr<ID3D11Buffer> depthBank;hr(f.device->CreateBuffer(&cb,&cd,&depthBank),"shadow depth bank");
    // PS alone behind the fixture VS (clip z 0.5): reverse mapping gives 0.5.
    f.context->ClearDepthStencilView(f.depthView.Get(),D3D11_CLEAR_DEPTH,0.0f,0);
    f.upload({{0,0,0,0},0.5f,0,{}});
    f.bindCommon(f.context.Get(),false);
    auto* bank=depthBank.Get();f.context->PSSetConstantBuffers(1,1,&bank);
    f.context->IASetInputLayout(nullptr);f.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    f.context->VSSetShader(f.fullscreen.Get(),nullptr,0);f.context->PSSetShader(shadowPixel.Get(),nullptr,0);
    ComPtr<ID3D11PixelShader> reverse;hr(f.device->CreatePixelShader(kPSVkFixtureReverse,sizeof(kPSVkFixtureReverse),nullptr,&reverse),"reverse PS");
    f.context->PSSetShader(reverse.Get(),nullptr,0);f.context->Draw(3,0);
    allDepth(f,0.5f,0x11,"Pixel constant buffer disturbed vertex constants");
    f.context->ClearDepthStencilView(f.depthView.Get(),D3D11_CLEAR_DEPTH,0.0f,0);
    f.context->PSSetShader(shadowPixel.Get(),nullptr,0);
    f.context->Draw(3,0);
    allDepth(f,0.5f,0x11,"Shadow depth PS behind a plain VS differs");
    // VS alone in front of the SV_Position.z fixture PS.
    std::array<std::array<float,4>,244> banks{};
    for(size_t row=0;row<4;++row) banks[row][row]=banks[12+row][row]=1;
    for(size_t bone=0;bone<64;++bone) for(size_t row=0;row<3;++row) banks[52+3*bone+row][row]=1;
    cb.ByteWidth=sizeof(banks);cd.pSysMem=banks.data();ComPtr<ID3D11Buffer> vertexBank;hr(f.device->CreateBuffer(&cb,&cd,&vertexBank),"shadow vertex bank");
    struct Vertex {float position[3],uv[2],weights[4],indices[4];};
    const Vertex vertices[]={{{-1,1,0.5f},{0,1},{1,0,0,0},{}},{{-1,-1,0.5f},{0.25f,0.875f},{1,0,0,0},{}},
        {{1,1,0.5f},{0.5f,0.75f},{1,0,0,0},{}},{{1,-1,0.5f},{0.75f,0.625f},{1,0,0,0},{}}};
    D3D11_BUFFER_DESC vb{};vb.ByteWidth=sizeof(vertices);vb.Usage=D3D11_USAGE_IMMUTABLE;vb.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vd{vertices,0,0};ComPtr<ID3D11Buffer> vertexBuffer;hr(f.device->CreateBuffer(&vb,&vd,&vertexBuffer),"shadow vertices");
    const uint16_t indices[]={0,1,2,3};
    D3D11_BUFFER_DESC ib{};ib.ByteWidth=sizeof(indices);ib.Usage=D3D11_USAGE_IMMUTABLE;ib.BindFlags=D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA id{indices,0,0};ComPtr<ID3D11Buffer> indexBuffer;hr(f.device->CreateBuffer(&ib,&id,&indexBuffer),"shadow indices");
    const D3D11_INPUT_ELEMENT_DESC elements[]={
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,20,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",3,DXGI_FORMAT_R32G32B32A32_FLOAT,0,36,D3D11_INPUT_PER_VERTEX_DATA,0}};
    ComPtr<ID3D11InputLayout> layout;hr(f.device->CreateInputLayout(elements,4,kVSShadowDepth,sizeof(kVSShadowDepth),&layout),"shadow layout");
    f.context->ClearDepthStencilView(f.depthView.Get(),D3D11_CLEAR_DEPTH,0.0f,0);
    auto* vbank=vertexBank.Get();f.context->VSSetConstantBuffers(0,1,&vbank);
    const UINT stride=sizeof(Vertex),offset=0;auto* raw=vertexBuffer.Get();
    f.context->IASetVertexBuffers(0,1,&raw,&stride,&offset);f.context->IASetIndexBuffer(indexBuffer.Get(),DXGI_FORMAT_R16_UINT,0);
    f.context->IASetInputLayout(layout.Get());f.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    f.context->VSSetShader(shadowVertex.Get(),nullptr,0);f.context->PSSetShader(f.positionDepthShader.Get(),nullptr,0);
    f.context->DrawIndexed(4,0,0);
    allDepth(f,0.5f,0x11,"Shadow VS in front of a plain depth PS differs");
    f.context->PSSetShader(shadowPixel.Get(),nullptr,0);
    f.context->ClearDepthStencilView(f.depthView.Get(),D3D11_CLEAR_DEPTH,0.0f,0);
    f.context->DrawIndexed(4,0,0);
    allDepth(f,0.5f,0x11,"Shadow VS+PS adapter depth differs");
    f.bindCommon(f.context.Get(),true);
}
void hazards(Fixture& f) {
    // D3D11 unbinds a shader view when its resource becomes an output, and
    // refuses to bind a view of a resource that is still an output.
    ComPtr<ID3D11ShaderResourceView> view;hr(f.device->CreateShaderResourceView(f.color.Get(),nullptr,&view),"color SRV");
    auto* raw=view.Get();f.context->PSSetShaderResources(3,1,&raw);
    ID3D11RenderTargetView* target=f.colorView.Get();f.context->OMSetRenderTargets(1,&target,nullptr);
    ComPtr<ID3D11ShaderResourceView> bound;f.context->PSGetShaderResources(3,1,&bound);
    need(!bound,"Output binding left its shader view bound");
    f.context->PSSetShaderResources(3,1,&raw);f.context->PSGetShaderResources(3,1,&bound);
    need(!bound,"Shader view of a bound output was accepted");
    f.context->OMSetRenderTargets(0,nullptr,nullptr);
    f.context->PSSetShaderResources(3,1,&raw);f.context->PSGetShaderResources(3,1,&bound);
    need(bound.Get()==view.Get(),"Shader view did not bind after the output was released");
    ID3D11ShaderResourceView* none=nullptr;f.context->PSSetShaderResources(3,1,&none);
}
// Thousands of draws in one batch: recorded calls cross the recording
// thread's storage chunks and still run in order with their own constants.
void longBatch(Fixture& f) {
    const float clear[4]{0,0,0,0};f.context->ClearRenderTargetView(f.colorView.Get(),clear);
    f.bindCommon(f.context.Get(),true);
    f.context->IASetInputLayout(nullptr);f.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    f.context->VSSetShader(f.fullscreen.Get(),nullptr,0);f.context->PSSetShader(f.colorShader.Get(),nullptr,0);
    ComPtr<ID3D11RasterizerState> scissor;D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;
    rs.DepthClipEnable=TRUE;rs.ScissorEnable=TRUE;hr(f.device->CreateRasterizerState(&rs,&scissor),"scissor state");
    f.context->RSSetState(scissor.Get());
    constexpr UINT draws=20000;
    for(UINT i=0;i<draws;++i) {
        // Column i%8 gets red (i/8)%256 and green 32*column.
        const UINT column=i%side;
        f.upload({{float((i/side)%256)/255.0f,float(column*32)/255.0f,0,1},0.5f,0,{}});
        const D3D11_RECT rect{LONG(column),0,LONG(column+1),LONG(side)};f.context->RSSetScissorRects(1,&rect);
        f.context->Draw(3,0);
    }
    const auto pixels=f.readColor();
    for(UINT y=0;y<side;++y) for(UINT x=0;x<side;++x) {
        const uint8_t* p=pixels.data()+(y*side+x)*4;
        const UINT last=draws-side+x;
        need(p[0]==(last/side)%256 && p[1]==x*32 && p[2]==0,"Long batch draw order or constants differ");
    }
    f.context->RSSetState(f.noCull.Get());
}
// ClearState and ExecuteCommandList(FALSE) reset bindings in every slot,
// including the highest ones; restoring replay keeps the caller's.
void stateClear(Fixture& f) {
    f.context->OMSetRenderTargets(0,nullptr,nullptr);
    ComPtr<ID3D11ShaderResourceView> view;hr(f.device->CreateShaderResourceView(f.color.Get(),nullptr,&view),"color SRV");
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;sd.MaxLOD=FLT_MAX;
    ComPtr<ID3D11SamplerState> sampler;hr(f.device->CreateSamplerState(&sd,&sampler),"point sampler");
    D3D11_BUFFER_DESC vd{};vd.ByteWidth=64;vd.Usage=D3D11_USAGE_DEFAULT;vd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    ComPtr<ID3D11Buffer> vertices;hr(f.device->CreateBuffer(&vd,nullptr,&vertices),"vertex buffer");
    constexpr UINT lastView=D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT-1,lastSampler=D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT-1,
        lastBuffer=D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT-1,lastStream=D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT-1;
    auto bind=[&](ID3D11DeviceContext* c) {
        auto* srv=view.Get();c->PSSetShaderResources(lastView,1,&srv);c->VSSetShaderResources(lastView,1,&srv);
        auto* s=sampler.Get();c->PSSetSamplers(lastSampler,1,&s);c->GSSetSamplers(lastSampler,1,&s);
        auto* cb=f.constants.Get();c->VSSetConstantBuffers(lastBuffer,1,&cb);c->CSSetConstantBuffers(lastBuffer,1,&cb);
        auto* vb=vertices.Get();const UINT stride=16,offset=0;c->IASetVertexBuffers(lastStream,1,&vb,&stride,&offset);
    };
    auto bound=[&]() {
        ComPtr<ID3D11ShaderResourceView> ps,vs;ComPtr<ID3D11SamplerState> pss,gss;ComPtr<ID3D11Buffer> vcb,ccb,vb;UINT stride=0,offset=0;
        f.context->PSGetShaderResources(lastView,1,&ps);f.context->VSGetShaderResources(lastView,1,&vs);
        f.context->PSGetSamplers(lastSampler,1,&pss);f.context->GSGetSamplers(lastSampler,1,&gss);
        f.context->VSGetConstantBuffers(lastBuffer,1,&vcb);f.context->CSGetConstantBuffers(lastBuffer,1,&ccb);
        f.context->IAGetVertexBuffers(lastStream,1,&vb,&stride,&offset);
        return std::array<bool,7>{bool(ps),bool(vs),bool(pss),bool(gss),bool(vcb),bool(ccb),bool(vb)};
    };
    bind(f.context.Get());
    for(bool b:bound()) need(b,"Highest-slot binding did not take");
    f.context->ClearState();
    for(bool b:bound()) need(!b,"ClearState left a highest-slot binding");
    ComPtr<ID3D11DeviceContext> deferred;hr(f.device->CreateDeferredContext(0,&deferred),"deferred context");
    bind(deferred.Get());
    ComPtr<ID3D11CommandList> list;hr(deferred->FinishCommandList(FALSE,&list),"finish command list");
    f.context->ExecuteCommandList(list.Get(),FALSE);
    for(bool b:bound()) need(!b,"ExecuteCommandList(FALSE) left a highest-slot binding");
    bind(f.context.Get());
    f.context->ExecuteCommandList(list.Get(),TRUE);
    for(bool b:bound()) need(b,"ExecuteCommandList(TRUE) lost a highest-slot binding");
    f.context->ClearState();
}
// A healthy device released with draws recorded and an occlusion query still
// open: the last release flushes the draws and begins the query's next segment
// in a new batch, which must be submitted before the query pools are destroyed
// (SIMPSONS_VULKAN_VALIDATION=1 reports any use of a destroyed object).
void pendingTeardown() {
    Fixture f;
    f.bindCommon(f.context.Get(),true);
    f.context->IASetInputLayout(nullptr);f.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    f.context->VSSetShader(f.fullscreen.Get(),nullptr,0);f.context->PSSetShader(f.sampleShader.Get(),nullptr,0);
    D3D11_QUERY_DESC qd{D3D11_QUERY_OCCLUSION,0};ComPtr<ID3D11Query> query;hr(f.device->CreateQuery(&qd,&query),"occlusion query");
    f.context->Begin(query.Get());
    for(UINT i=0;i<20000;++i) f.context->Draw(3,0);
    // The fixture's references are the last ones: the device is destroyed here.
}
// A lost device whose last batch was recorded but never submitted: the
// destructor's submission fails, and the recording thread must still be
// drained before the command pool and device are destroyed.
void lostDeviceTeardown() {
    Fixture f;
    f.bindCommon(f.context.Get(),true);
    f.context->IASetInputLayout(nullptr);f.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    f.context->VSSetShader(f.fullscreen.Get(),nullptr,0);f.context->PSSetShader(f.colorShader.Get(),nullptr,0);
    for(UINT i=0;i<20000;++i) {
        const D3D11_RECT rect{LONG(i%side),0,LONG(i%side+1),LONG(side)};f.context->RSSetScissorRects(1,&rect);
        f.context->Draw(3,0);
    }
    void* object=nullptr;
    hr(f.device->QueryInterface(__uuidof(Simpsons::Graphics::Vulkan::D3D11Device),&object),"Vulkan device interface");
    auto* device=static_cast<Simpsons::Graphics::Vulkan::D3D11Device*>(object);
    device->vulkan().markLost();
    need(device->GetDeviceRemovedReason()!=S_OK,"Marked device does not report removal");
    device->Release();
    // The fixture's references are the last ones: the device is destroyed here.
}
}

int main() {
    try {
        Fixture fixture;
        std::fprintf(stderr,"[VULKAN DEVICE TEST] %s\n",Simpsons::Graphics::Vulkan::deviceDescription(fixture.device.Get()).c_str());
        depthClearReadback(fixture);
        vertexDepth(fixture);
        pixelDepth(fixture);
        indexedStrips(fixture);
        deferredReplay(fixture);
        dynamicRename(fixture);
        constantOffsets(fixture);
        sampling(fixture);
        shadowAdapters(fixture);
        hazards(fixture);
        longBatch(fixture);
        stateClear(fixture);
        pendingTeardown();
        lostDeviceTeardown();
        std::printf("PASS Vulkan device conformance: %zu checks\n",checks);
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL Vulkan device conformance after %zu checks: %s\n",checks,error.what());
        return 1;
    }
}
