#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <bit>
#include "renderer/im2d_vertices.h"
#include "VSIm2D.h"
#include "PSIm2DFlat.h"
#include "PSIm2DTextured.h"
#include "GSIm2DProbe.h"
using Microsoft::WRL::ComPtr;
namespace {
size_t checks{};
void need(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void check(HRESULT h,const char* why){if(FAILED(h)){char message[180];sprintf_s(message,"%s: %08lX",why,ULONG(h));throw std::runtime_error(message);}}
using Vertex=Simpsons::Graphics::Im2DVertex;
static_assert(sizeof(Vertex)==40);
struct Fixture {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> flat,texture;
    ComPtr<ID3D11GeometryShader> probe;ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> constants;
    Fixture(bool hardware) {
        D3D_FEATURE_LEVEL level{};
        check(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"device");
        std::printf("Im2D shader arithmetic %s feature_level=%X\n",hardware?"hardware":"WARP",unsigned(level));
        check(device->CreateVertexShader(kVSIm2D,sizeof(kVSIm2D),nullptr,&vs),"original-branch vertex shader");
        check(device->CreatePixelShader(kPSIm2DFlat,sizeof(kPSIm2DFlat),nullptr,&flat),"original flat expression");
        check(device->CreatePixelShader(kPSIm2DTextured,sizeof(kPSIm2DTextured),nullptr,&texture),"original textured expression");
        D3D11_INPUT_ELEMENT_DESC input[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0}};
        check(device->CreateInputLayout(input,3,kVSIm2D,sizeof(kVSIm2D),&layout),"decoded input layout");
        D3D11_SO_DECLARATION_ENTRY so[]={{0,"SV_Position",0,0,4,0},{0,"COLOR",0,0,4,0},{0,"TEXCOORD",0,0,2,0}};
        UINT stride=sizeof(Vertex);
        check(device->CreateGeometryShaderWithStreamOutput(kGSIm2DProbe,sizeof(kGSIm2DProbe),so,3,&stride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&probe),"stream output observer");
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=16;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&cb,nullptr,&constants),"scale constants");
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        ComPtr<ID3D11RasterizerState> rs;check(device->CreateRasterizerState(&raster,&rs),"rasterizer");context->RSSetState(rs.Get());
        D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=FALSE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
        depth.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};depth.BackFace=depth.FrontFace;
        ComPtr<ID3D11DepthStencilState> ds;check(device->CreateDepthStencilState(&depth,&ds),"depth disabled");context->OMSetDepthStencilState(ds.Get(),0);
        context->IASetInputLayout(layout.Get());context->VSSetShader(vs.Get(),nullptr,0);
        auto* constant=constants.Get();context->VSSetConstantBuffers(0,1,&constant);
    }
    void scale(uint32_t width,uint32_t height) {
        const std::array<float,4> values={2.0f/float(width),2.0f/float(height),0,0};
        context->UpdateSubresource(constants.Get(),0,nullptr,values.data(),0,0);
    }
    ComPtr<ID3D11Buffer> vertices(const std::vector<Vertex>& source) {
        D3D11_BUFFER_DESC b{};b.ByteWidth=UINT(source.size()*sizeof(Vertex));b.BindFlags=D3D11_BIND_VERTEX_BUFFER;b.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=source.data();ComPtr<ID3D11Buffer> result;
        check(device->CreateBuffer(&b,&data,&result),"vertex input");auto* ptr=result.Get();UINT stride=sizeof(Vertex),offset=0;
        context->IASetVertexBuffers(0,1,&ptr,&stride,&offset);return result;
    }
    std::vector<Vertex> observe(const std::vector<Vertex>& input,uint32_t width,uint32_t height) {
        scale(width,height);auto held=vertices(input);
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(input.size()*sizeof(Vertex));desc.BindFlags=D3D11_BIND_STREAM_OUTPUT;
        ComPtr<ID3D11Buffer> output,readback;check(device->CreateBuffer(&desc,nullptr,&output),"stream output allocation");
        desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check(device->CreateBuffer(&desc,nullptr,&readback),"stream readback allocation");
        auto* target=output.Get();UINT offset=0;context->SOSetTargets(1,&target,&offset);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);context->GSSetShader(probe.Get(),nullptr,0);context->PSSetShader(nullptr,nullptr,0);
        context->Draw(UINT(input.size()),0);context->SOSetTargets(0,nullptr,nullptr);context->GSSetShader(nullptr,nullptr,0);
        context->CopyResource(readback.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"stream readback");
        std::vector<Vertex> values(input.size());memcpy(values.data(),mapped.pData,values.size()*sizeof(Vertex));context->Unmap(readback.Get(),0);return values;
    }
    std::vector<std::array<float,4>> pixels(const std::vector<Vertex>& input,uint32_t width,uint32_t height,bool textured) {
        scale(width,height);auto held=vertices(input);
        D3D11_TEXTURE2D_DESC t{};t.Width=width;t.Height=height;t.MipLevels=1;t.ArraySize=1;t.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
        t.SampleDesc.Count=1;t.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> output,readback;check(device->CreateTexture2D(&t,nullptr,&output),"float target");
        t.BindFlags=0;t.Usage=D3D11_USAGE_STAGING;t.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&t,nullptr,&readback),"float target readback");
        ComPtr<ID3D11RenderTargetView> view;check(device->CreateRenderTargetView(output.Get(),nullptr,&view),"float target view");
        const float clear[]={-99,-99,-99,-99};context->ClearRenderTargetView(view.Get(),clear);auto* target=view.Get();context->OMSetRenderTargets(1,&target,nullptr);
        D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&viewport);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);context->PSSetShader(textured?texture.Get():flat.Get(),nullptr,0);
        context->Draw(UINT(input.size()),0);context->OMSetRenderTargets(0,nullptr,nullptr);context->CopyResource(readback.Get(),output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"float pixels");
        std::vector<std::array<float,4>> result(size_t(width)*height);
        for(uint32_t y=0;y<height;++y)memcpy(result.data()+size_t(y)*width,static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch,size_t(width)*16);
        context->Unmap(readback.Get(),0);return result;
    }
    void sampleTexture() {
        D3D11_TEXTURE2D_DESC t{};t.Width=4;t.Height=2;t.MipLevels=1;t.ArraySize=1;t.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        t.SampleDesc.Count=1;t.BindFlags=D3D11_BIND_SHADER_RESOURCE;t.Usage=D3D11_USAGE_IMMUTABLE;
        std::array<uint8_t,32> data{};for(uint32_t i=0;i<8;++i){data[i*4]=uint8_t(i*29);data[i*4+1]=uint8_t(255-i*17);data[i*4+2]=uint8_t(i*11);data[i*4+3]=uint8_t(31+i*32);}
        D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=data.data();initial.SysMemPitch=16;
        ComPtr<ID3D11Texture2D> image;check(device->CreateTexture2D(&t,&initial,&image),"original-expression sample fixture");
        ComPtr<ID3D11ShaderResourceView> view;check(device->CreateShaderResourceView(image.Get(),nullptr,&view),"sample view");auto* srv=view.Get();context->PSSetShaderResources(0,1,&srv);
        D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
        ComPtr<ID3D11SamplerState> state;check(device->CreateSamplerState(&sampler,&state),"sample state");auto* p=state.Get();context->PSSetSamplers(0,1,&p);
    }
};
}
int main(int argc,char** argv)try {
    Fixture f(argc>1 && std::string(argv[1])=="--hardware");
    std::vector<Vertex> input;
    for(uint32_t i=0;i<64;++i)input.push_back({{float(i)*21.125f-10,float(i)*11.5f+0.25f,float(i)/63,float(int(i)-32)},
        {float(i)/31-0.5f,float(i%9)-3,float(i%3)*0.75f,0.25f}, {float(i)/63,1-float(i)/63}});
    for(auto dimensions:std::array<std::array<uint32_t,2>,3>{{{1280,720},{640,720},{17,13}}}) {
        auto output=f.observe(input,dimensions[0],dimensions[1]);
        for(size_t i=0;i<input.size();++i) {
            const auto& v=input[i];const auto& o=output[i];
            const float sx=2.0f/float(dimensions[0]),sy=2.0f/float(dimensions[1]);
            const float x=((v.position[0]-0.5f)*sx)-1.0f,y=-(((v.position[1]-0.5f)*sy)-1.0f);
            need(o.position[0]==x && o.position[1]==y,"Original screen-position arithmetic differs");
            need(o.position[2]==v.position[2] && o.position[3]==1,"Original Im2D Z or constant W differs");
            for(uint32_t c=0;c<4;++c)need(o.color[c]==std::min(1.0f,v.color[c]),"Original per-vertex min1 color differs");
            need(o.uv==v.uv,"Original passthrough UV differs");
        }
    }
    constexpr uint32_t width=16,height=8;
    std::vector<Vertex> quad={{{0.5f,0.5f,0.25f,0},{2,-0.25f,0.75f,0.5f},{0,0}},
        {{width+0.5f,0.5f,0.25f,20},{0,0.75f,0.75f,0.5f},{1,0}},
        {{0.5f,height+0.5f,0.25f,-4},{2,-0.25f,0.75f,0.5f},{0,1}},
        {{width+0.5f,height+0.5f,0.25f,0.125f},{0,0.75f,0.75f,0.5f},{1,1}}};
    f.sampleTexture();
    for(bool textured:{false,true}) {
        const auto pixels=f.pixels(quad,width,height,textured);
        for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
            const float u=(float(x)+0.5f)/width;
            std::array<float,4> expected={1-u,-0.25f+u,0.75f,0.5f};
            if(textured) {
                const uint32_t i=(y/4)*4+x/4;
                const std::array<float,4> sample={float(i*29)/255,float(255-i*17)/255,float(i*11)/255,float(31+i*32)/255};
                for(uint32_t c=0;c<4;++c)expected[c]*=sample[c];
            }
            for(uint32_t c=0;c<4;++c)need(std::abs(pixels[y*width+x][c]-expected[c])<=0.000002f,
                "Original pixel expression, affine interpolation, min-before-interpolation or texture modulation differs");
        }
    }
    // Original declaration and 28-byte source feed the same native shaders.
    // Each channel is distinct, so alpha/channel reversals change actual pixels.
    using namespace Simpsons::Graphics;
    DeclarationRegistry declarations;
    const std::array<uint8_t,48> originalDeclaration={
        0,0,0,0,0,0x1A,0x23,0xA6,0,0,0,0,
        0,0,0,16,0,0x18,0x28,0x86,0,10,0,0,
        0,0,0,20,0,0x2C,0x23,0xA5,0,5,0,0,
        0,0xFF,0,0,0xFF,0xFF,0xFF,0xFF,0,0,0,0};
    const auto declaration=declarations.record(declarations.create(originalDeclaration));
    std::array<uint8_t,112> originalVertices{};
    const auto put=[](uint8_t* p,float value) {
        const auto bits=std::bit_cast<uint32_t>(value);
        p[0]=uint8_t(bits>>24);p[1]=uint8_t(bits>>16);p[2]=uint8_t(bits>>8);p[3]=uint8_t(bits);
    };
    for(size_t i=0;i<quad.size();++i) {
        auto* p=originalVertices.data()+i*28;
        for(size_t c=0;c<4;++c)put(p+c*4,quad[i].position[c]);
        p[16]=73;p[17]=211;p[18]=29;p[19]=151;
        put(p+20,quad[i].uv[0]);put(p+24,quad[i].uv[1]);
    }
    const auto owned=decodeIm2DVertices(*declaration,originalVertices,true);
    originalVertices.fill(0); // GPU input must own the converted source.
    for(bool textured:{false,true}) {
        const auto pixels=f.pixels(owned,width,height,textured);
        for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
            std::array<float,4> expected={211.0f/255,29.0f/255,151.0f/255,73.0f/255};
            if(textured) {
                const uint32_t i=(y/4)*4+x/4;
                const std::array<float,4> sample={float(i*29)/255,float(255-i*17)/255,float(i*11)/255,float(31+i*32)/255};
                for(size_t c=0;c<4;++c)expected[c]*=sample[c];
            }
            for(size_t c=0;c<4;++c)need(std::abs(pixels[y*width+x][c]-expected[c])<=0.000002f,
                "Original packed color or owned native vertex input differs in rendered pixels");
        }
    }
    std::printf("PASS: %zu Im2D shader arithmetic checks; original packed-color conversion and GPU source/transform qualification, game draw integration unverified\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL after %zu checks: %s\n",checks,e.what());return 1;}
