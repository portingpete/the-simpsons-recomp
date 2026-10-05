// Standalone real GPU probes. Main supplies offline FXC headers from
// rigid_shader.hlsl: VSRigid, PSRigid, GSRigidProbe, VSRigidPixelProbe.
// Link d3d11 + dxgi. No runtime compiler or original instruction interpreter.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include "VSRigid.h"
#include "PSRigid.h"
#include "GSRigidProbe.h"
#include "VSRigidPixelProbe.h"
#include "VSRigidTextured.h"
#include "PSRigidTextured.h"
#include "PSRigidNormalProbe.h"
#include "PSRigidDualTextured.h"
#include "VSRigidDualPixelProbe.h"
#include "VSRigidDualTextured.h"
#include "GSRigidDualProbe.h"
#include "PSRigidGloss.h"
#include "VSRigidGloss.h"
#include "GSRigidGlossProbe.h"
#include "VSRigidGlossPixelProbe.h"
#include "VSRigidMultitone.h"
#include "GSRigidMultitoneProbe.h"
#include "VSRigidNormalmap.h"
#include "GSRigidNormalmapProbe.h"
#include "PSRigidMultitone.h"
#include "VSRigidMultitonePixelProbe.h"
using Microsoft::WRL::ComPtr;
namespace {
using Vec=std::array<float,4>;
using VConstants=std::array<Vec,30>;
using PConstants=std::array<Vec,50>;
using Probe=std::array<Vec,5>;
using Grid=std::array<Vec,9>;
struct Vertex {std::array<float,3> position,normal;Vec color;std::array<float,2> uv;};
struct Output {Vec position;std::array<float,2> uv;Vec characterShadow,worldShadow;std::array<float,3> normal;Vec color;};
static_assert(sizeof(Vertex)==48 && sizeof(Output)==84 && sizeof(VConstants)==480 && sizeof(PConstants)==800);
size_t checks{},vsDraws{},psDraws{},verticesObserved{};
void need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void hr(HRESULT value,const char* why){if(FAILED(value)){char text[200];sprintf_s(text,"%s: %08lX",why,ULONG(value));throw std::runtime_error(text);}}
void nearValue(float actual,double expected,const char* name,size_t index,size_t lane) {
    ++checks;
    if(!std::isfinite(actual)||std::abs(actual-expected)>0.000003+0.000003*std::abs(expected)) {
        char text[240];sprintf_s(text,"%s sample%zu lane%zu: actual %.9g expected %.12g",name,index,lane,actual,expected);
        throw std::runtime_error(text);
    }
}
double sat(double v){return std::clamp(v,0.0,1.0);}
double frac(double v){return v-std::floor(v);}
double mix(double a,double b,double t){return a+(b-a)*t;}
// Independent geometric oracle: conventional row matrices, not shader registers.
std::array<double,21> vertexExpected(const Vertex& input,const VConstants& c) {
    std::array<double,4> p={input.position[0],input.position[1],input.position[2],1};
    const auto multiply=[&](size_t first,const std::array<double,4>& value) {
        std::array<double,4> result{};
        for(size_t row=0;row<4;++row)for(size_t lane=0;lane<4;++lane)result[row]+=double(c[first+row][lane])*value[lane];
        return result;
    };
    const auto clip=multiply(0,p),world=multiply(12,p),character=multiply(26,world),light=multiply(22,world);
    std::array<double,21> out{};
    std::copy(clip.begin(),clip.end(),out.begin());out[4]=input.uv[0];out[5]=input.uv[1];
    std::copy(character.begin(),character.end(),out.begin()+6);std::copy(light.begin(),light.end(),out.begin()+10);
    for(size_t row=0;row<3;++row)for(size_t lane=0;lane<3;++lane)out[14+row]+=double(input.normal[lane])*c[12+row][lane];
    std::copy(input.color.begin(),input.color.end(),out.begin()+17);return out;
}
// Independent spatial filter: three column sums, vertically interpolated edge
// rows, then horizontal interpolation. Texture channel depends on the column.
// This is NOT a depth-comparison sampler: compare occurs after ordinary sampling.
double shadowFilter(const Vec& projection,const Grid& grid) {
    const double u=.5+.5*projection[0]/projection[3],v=.5-.5*projection[1]/projection[3];
    const double threshold=1-sat(double(projection[2])/projection[3]);
    const auto compare=[&](int x,int y) {
        const int column=int(std::floor(u*1024))+x-511,row=int(std::floor(v*1024))+y-511;
        const float sample=column>=0&&column<3&&row>=0&&row<3?grid[size_t(row*3+column)][size_t(x+1)]:.75f;
        return threshold>=sample?1.0:0.0;
    };
    const double fy=frac(v*1024),fx=frac(u*1024);
    const double left=compare(-1,0)+mix(compare(-1,-1),compare(-1,1),fy);
    const double center=compare(0,0)+mix(compare(0,-1),compare(0,1),fy);
    const double right=compare(1,0)+mix(compare(1,-1),compare(1,1),fy);
    return .5*(mix(left,right,fx)+center);
}
std::array<double,4> pixelExpected(const Probe& input,const PConstants& c,const std::array<Grid,2>& grids,const Vec* base=nullptr,bool dual=false) {
    const double u=frac(input[0][0]),v=frac(input[0][1]);
    const double length=std::sqrt(double(input[3][0])*input[3][0]+double(input[3][1])*input[3][1]+double(input[3][2])*input[3][2]);
    std::array<double,3> normal{};double light=0;
    // The original zero vector remains zero after legacy normalization; it
    // has no direction. Other finite vectors use the geometric unit direction.
    for(size_t lane=0;lane<3;++lane){normal[lane]=length==0?0:input[3][lane]/length;light+=normal[lane]*c[36][lane];}
    light=sat(light);
    const double upper=2*v>=1?1:0;
    const double blue=input[4][2]>=.9f?1:0;
    const double id=c[49][2]!=0?c[49][2]:c[40][0];
    double visibility=1;
    if(c[47][0]!=0) {
        const double character=c[31][0]>0?shadowFilter(input[1],grids[1]):1;
        const double world=c[31][0]>0?shadowFilter(input[2],grids[0]):1;
        const double charGate=sat(std::floor(1-sat(normal[1]))+sat((1-c[30][1])*character+c[30][1]));
        const double worldGate=sat(std::floor(-light)+1+world);
        visibility=std::min(charGate,worldGate);
    }
    if(dual) {
        need(base!=nullptr,"Dual shader oracle requires a base texture sample");
        const double detailU=frac(input[0][2]),detailV=frac(input[0][3]);
        return {(id+32*std::floor(32*frac(2*detailV))+std::floor(64*detailU))*double(1.0f/2046.0f),
            (32*std::floor(32*std::clamp(double((*base)[1]),0.0,double(.96f)))+
             std::floor(32*std::clamp(double((*base)[0]),0.0,double(.96f))))*double(1.0f/1023.0f),
            double(.2f)*(*base)[2]+.5*blue+.25*c[46][0]*(.125>=light?1:0),
            double(.3f)*(1-std::floor(visibility))+double(.7f)};
    }
    if(base)return {id*double(1.0f/1023.0f),
        (std::floor(32*std::clamp(double((*base)[1]),0.0,double(.96f)))*32+
         std::floor(32*std::clamp(double((*base)[0]),0.0,double(.96f))))*double(1.0f/1023.0f),
        double(.2f)*(*base)[2]+.5*blue+.25*c[46][0]*(.125>=light?1:0),
        double(.3f)*(1-std::floor(visibility))+double(.7f)};

    return {id*double(1.0f/1023.0f),(std::floor(32*(2*v-upper))*32+std::floor(64*u))*double(1.0f/1023.0f),
        .5*blue+.25*upper+.125*c[46][0]*(.125>=light?1:0),double(.39f)*(1-std::floor(visibility))+double(.01f)};
}
struct Fixture {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vs,pixelProbe;ComPtr<ID3D11PixelShader> ps,normalProbe;ComPtr<ID3D11GeometryShader> gs;
    ComPtr<ID3D11InputLayout> layout;ComPtr<ID3D11Buffer> vc,pc,inputs;
    ComPtr<ID3D11Texture2D> target,staging;ComPtr<ID3D11RenderTargetView> rtv;
    std::array<ComPtr<ID3D11Texture2D>,2> textures;std::array<ComPtr<ID3D11ShaderResourceView>,2> views;
    ComPtr<ID3D11SamplerState> sampler;ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depth;ComPtr<ID3D11BlendState> blend;
    bool textured{},dual{};
    ComPtr<ID3D11Texture2D> baseTexture;
    ComPtr<ID3D11ShaderResourceView> baseView;
    std::array<Vec,4> baseTexels{{{.125f,.25f,.75f,-9},{.5f,.625f,.25f,5},
                                 {.875f,.375f,.5f,1},{1.25f,-.25f,1,0}}};
    explicit Fixture(bool hardware,bool useTexture=false,bool useDual=false):textured(useTexture||useDual),dual(useDual) {
        const D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_11_0};D3D_FEATURE_LEVEL level{};
        hr(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,requested,1,D3D11_SDK_VERSION,&device,&level,&context),"device");
        need(level==D3D_FEATURE_LEVEL_11_0,"Shader model5 device required");
        std::printf("Rigid shader probes: %s\n",hardware?"hardware":"WARP");
        hr(device->CreateVertexShader(textured?kVSRigidTextured:kVSRigid,textured?sizeof(kVSRigidTextured):sizeof(kVSRigid),nullptr,&vs),"original rigid VS");
        hr(device->CreatePixelShader(dual?kPSRigidDualTextured:(textured?kPSRigidTextured:kPSRigid),
            dual?sizeof(kPSRigidDualTextured):(textured?sizeof(kPSRigidTextured):sizeof(kPSRigid)),nullptr,&ps),"original rigid PS");
        hr(device->CreatePixelShader(kPSRigidNormalProbe,sizeof(kPSRigidNormalProbe),nullptr,&normalProbe),"normalization observer");
        hr(device->CreateVertexShader(dual?kVSRigidDualPixelProbe:kVSRigidPixelProbe,
            dual?sizeof(kVSRigidDualPixelProbe):sizeof(kVSRigidPixelProbe),nullptr,&pixelProbe),"pixel input fixture");
        const D3D11_INPUT_ELEMENT_DESC elements[]={
            {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",3,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0}};
        hr(device->CreateInputLayout(elements,4,kVSRigid,sizeof(kVSRigid),&layout),"rigid input payload");
        const D3D11_SO_DECLARATION_ENTRY outputs[]={{0,"SV_Position",0,0,4,0},{0,"TEXCOORD",0,0,2,0},
            {0,"TEXCOORD",1,0,4,0},{0,"TEXCOORD",2,0,4,0},{0,"TEXCOORD",3,0,3,0},{0,"TEXCOORD",4,0,4,0}};
        const UINT outputStride=sizeof(Output);
        hr(device->CreateGeometryShaderWithStreamOutput(kGSRigidProbe,sizeof(kGSRigidProbe),outputs,6,&outputStride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&gs),"VS output observer");
        D3D11_BUFFER_DESC bd{};bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.ByteWidth=sizeof(VConstants);
        hr(device->CreateBuffer(&bd,nullptr,&vc),"VS bank");bd.ByteWidth=sizeof(PConstants);
        hr(device->CreateBuffer(&bd,nullptr,&pc),"PS bank");bd.ByteWidth=sizeof(Probe);
        hr(device->CreateBuffer(&bd,nullptr,&inputs),"pixel fixture inputs");
        D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=2;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;
        td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        hr(device->CreateTexture2D(&td,nullptr,&target),"float target");hr(device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"RTV");
        td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        hr(device->CreateTexture2D(&td,nullptr,&staging),"color readback");
        // Exact original literal dimensions; channel values are intentionally
        // independent. This proves shader sampling, not D24FS8 view conversion.
        td.Width=td.Height=1024;td.Usage=D3D11_USAGE_DEFAULT;td.CPUAccessFlags=0;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const std::vector<Vec> initial(1024*1024,Vec{.75f,.75f,.75f,-19});
        const D3D11_SUBRESOURCE_DATA data{initial.data(),1024*sizeof(Vec),0};
        for(size_t slot=0;slot<2;++slot){hr(device->CreateTexture2D(&td,&data,&textures[slot]),"shadow probe texture");hr(device->CreateShaderResourceView(textures[slot].Get(),nullptr,&views[slot]),"shadow probe view");}
        td.Width=td.Height=2;
        const D3D11_SUBRESOURCE_DATA baseData{baseTexels.data(),2*sizeof(Vec),0};
        hr(device->CreateTexture2D(&td,&baseData,&baseTexture),"independent rigid base texture");
        hr(device->CreateShaderResourceView(baseTexture.Get(),nullptr,&baseView),"rigid base view");
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxAnisotropy=1;sd.MaxLOD=D3D11_FLOAT32_MAX;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;
        hr(device->CreateSamplerState(&sd,&sampler),"ordinary normalized point sampler");
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        hr(device->CreateRasterizerState(&rd,&raster),"probe rasterizer");
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=FALSE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;dd.DepthFunc=D3D11_COMPARISON_ALWAYS;
        hr(device->CreateDepthStencilState(&dd,&depth),"probe depth state");
        D3D11_BLEND_DESC bl{};bl.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        hr(device->CreateBlendState(&bl,&blend),"all original output lanes");
    }
    void vertices(const std::vector<Vertex>& input,const VConstants& c) {
        context->OMSetRenderTargets(0,nullptr,nullptr);context->UpdateSubresource(vc.Get(),0,nullptr,c.data(),0,0);
        D3D11_BUFFER_DESC d{};d.ByteWidth=UINT(input.size()*sizeof(Vertex));d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        const D3D11_SUBRESOURCE_DATA data{input.data(),0,0};ComPtr<ID3D11Buffer> vb,out,read;
        hr(device->CreateBuffer(&d,&data,&vb),"VS vertices");d.ByteWidth=UINT(input.size()*sizeof(Output));d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_STREAM_OUTPUT;
        hr(device->CreateBuffer(&d,nullptr,&out),"VS outputs");d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        hr(device->CreateBuffer(&d,nullptr,&read),"VS staging");
        ComPtr<ID3D11Query> query;const D3D11_QUERY_DESC q{D3D11_QUERY_SO_STATISTICS,0};hr(device->CreateQuery(&q,&query),"SO query");
        context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        auto* buffer=vb.Get();const UINT stride=sizeof(Vertex),zero=0;context->IASetVertexBuffers(0,1,&buffer,&stride,&zero);
        context->VSSetShader(vs.Get(),nullptr,0);context->GSSetShader(gs.Get(),nullptr,0);context->PSSetShader(nullptr,nullptr,0);
        auto* bank=vc.Get();context->VSSetConstantBuffers(0,1,&bank);auto* output=out.Get();context->SOSetTargets(1,&output,&zero);
        context->Begin(query.Get());context->Draw(UINT(input.size()),0);context->End(query.Get());context->SOSetTargets(0,nullptr,nullptr);
        context->CopyResource(read.Get(),out.Get());D3D11_MAPPED_SUBRESOURCE mapped{};hr(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped),"VS actual outputs");
        std::vector<std::array<float,21>> actual(input.size());std::memcpy(actual.data(),mapped.pData,input.size()*sizeof(Output));context->Unmap(read.Get(),0);
        D3D11_QUERY_DATA_SO_STATISTICS stats{};need(context->GetData(query.Get(),&stats,sizeof(stats),0)==S_OK&&stats.NumPrimitivesWritten==input.size()&&stats.PrimitivesStorageNeeded==input.size(),"Original VS output count differs");
        for(size_t i=0;i<input.size();++i){const auto want=vertexExpected(input[i],c);for(size_t lane=0;lane<21;++lane)nearValue(actual[i][lane],want[lane],"VS",i,lane);}
        ++vsDraws;verticesObserved+=input.size();
    }
    void dualVertices(const std::vector<Vertex>& input,const VConstants& c) {
        struct DualVertex {Vertex base;std::array<float,2> uv1;};
        static_assert(sizeof(DualVertex)==56);
        std::vector<DualVertex> vertices(input.size());
        for(size_t i=0;i<input.size();++i)vertices[i]={input[i],{float(i)*.1875f-2,float(i%7)*.3125f}};
        ComPtr<ID3D11VertexShader> shader;ComPtr<ID3D11GeometryShader> observer;
        ComPtr<ID3D11InputLayout> inputsLayout;
        hr(device->CreateVertexShader(kVSRigidDualTextured,sizeof(kVSRigidDualTextured),nullptr,&shader),"dual VS");
        const D3D11_INPUT_ELEMENT_DESC elements[]={
            {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",3,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",4,DXGI_FORMAT_R32G32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0}};
        hr(device->CreateInputLayout(elements,5,kVSRigidDualTextured,sizeof(kVSRigidDualTextured),&inputsLayout),"dual layout");
        const D3D11_SO_DECLARATION_ENTRY outputs[]={{0,"SV_Position",0,0,4,0},{0,"TEXCOORD",0,0,2,0},
            {0,"TEXCOORD",1,0,2,0},{0,"TEXCOORD",2,0,4,0},{0,"TEXCOORD",3,0,4,0},
            {0,"TEXCOORD",4,0,3,0},{0,"TEXCOORD",5,0,4,0}};
        const UINT outputStride=23*sizeof(float);
        hr(device->CreateGeometryShaderWithStreamOutput(kGSRigidDualProbe,sizeof(kGSRigidDualProbe),outputs,7,
            &outputStride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&observer),"dual VS observer");
        context->OMSetRenderTargets(0,nullptr,nullptr);context->UpdateSubresource(vc.Get(),0,nullptr,c.data(),0,0);
        D3D11_BUFFER_DESC d{};d.ByteWidth=UINT(vertices.size()*sizeof(DualVertex));d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        const D3D11_SUBRESOURCE_DATA data{vertices.data(),0,0};ComPtr<ID3D11Buffer> vb,out,read;
        hr(device->CreateBuffer(&d,&data,&vb),"dual vertices");d.ByteWidth=UINT(vertices.size()*outputStride);d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_STREAM_OUTPUT;
        hr(device->CreateBuffer(&d,nullptr,&out),"dual outputs");d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        hr(device->CreateBuffer(&d,nullptr,&read),"dual staging");
        context->IASetInputLayout(inputsLayout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        auto* buffer=vb.Get();const UINT stride=sizeof(DualVertex),zero=0;context->IASetVertexBuffers(0,1,&buffer,&stride,&zero);
        context->VSSetShader(shader.Get(),nullptr,0);context->GSSetShader(observer.Get(),nullptr,0);context->PSSetShader(nullptr,nullptr,0);
        auto* bank=vc.Get();context->VSSetConstantBuffers(0,1,&bank);auto* output=out.Get();context->SOSetTargets(1,&output,&zero);
        context->Draw(UINT(vertices.size()),0);context->SOSetTargets(0,nullptr,nullptr);context->CopyResource(read.Get(),out.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};hr(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped),"dual VS readback");
        std::vector<std::array<float,23>> actual(vertices.size());std::memcpy(actual.data(),mapped.pData,vertices.size()*outputStride);context->Unmap(read.Get(),0);
        for(size_t i=0;i<vertices.size();++i) {
            const auto base=vertexExpected(input[i],c);std::array<double,23> want{};
            std::copy_n(base.begin(),6,want.begin());std::copy(base.begin()+6,base.end(),want.begin()+8);
            want[6]=vertices[i].uv1[0];want[7]=vertices[i].uv1[1];
            for(size_t lane=0;lane<23;++lane)nearValue(actual[i][lane],want[lane],"dual VS",i,lane);
        }
        ++vsDraws;verticesObserved+=vertices.size();
    }
#include "header/test_multitone_vertices.h"
#include "header/test_multitone_pixels.h"
#include "header/test_normalmap_vertices.h"
    void glossVertices(const std::vector<Vertex>& input,const VConstants& c) {
        struct Input {Vertex base;std::array<float,2> uv1;};
        std::vector<Input> vertices(input.size());
        for(size_t i=0;i<input.size();++i)vertices[i]={input[i],{float(i)*.1875f-2,float(i%7)*.3125f}};
        ComPtr<ID3D11VertexShader> shader;ComPtr<ID3D11GeometryShader> observer;ComPtr<ID3D11InputLayout> layout;
        hr(device->CreateVertexShader(kVSRigidGloss,sizeof(kVSRigidGloss),nullptr,&shader),"gloss VS");
        const D3D11_INPUT_ELEMENT_DESC elements[]={
            {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",3,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",4,DXGI_FORMAT_R32G32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0}};
        // Use the SAME shared input layout signature as the native mesh path.
        hr(device->CreateInputLayout(elements,5,kVSRigidDualTextured,sizeof(kVSRigidDualTextured),&layout),"gloss shared mesh layout");
        const D3D11_SO_DECLARATION_ENTRY outputs[]={{0,"SV_Position",0,0,4,0},{0,"TEXCOORD",0,0,4,0},
            {0,"TEXCOORD",1,0,4,0},{0,"TEXCOORD",2,0,4,0},{0,"TEXCOORD",3,0,4,0},
            {0,"TEXCOORD",4,0,3,0},{0,"TEXCOORD",5,0,4,0}};
        const UINT outputStride=27*sizeof(float);
        hr(device->CreateGeometryShaderWithStreamOutput(kGSRigidGlossProbe,sizeof(kGSRigidGlossProbe),outputs,7,
            &outputStride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&observer),"gloss VS observer");
        context->OMSetRenderTargets(0,nullptr,nullptr);context->UpdateSubresource(vc.Get(),0,nullptr,c.data(),0,0);
        D3D11_BUFFER_DESC d{};d.ByteWidth=UINT(vertices.size()*sizeof(Input));d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        const D3D11_SUBRESOURCE_DATA data{vertices.data(),0,0};ComPtr<ID3D11Buffer> vb,out,read;
        hr(device->CreateBuffer(&d,&data,&vb),"gloss vertices");d.ByteWidth=UINT(vertices.size()*outputStride);d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_STREAM_OUTPUT;
        hr(device->CreateBuffer(&d,nullptr,&out),"gloss outputs");d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        hr(device->CreateBuffer(&d,nullptr,&read),"gloss staging");
        context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        auto* buffer=vb.Get();const UINT stride=sizeof(Input),zero=0;context->IASetVertexBuffers(0,1,&buffer,&stride,&zero);
        context->VSSetShader(shader.Get(),nullptr,0);context->GSSetShader(observer.Get(),nullptr,0);context->PSSetShader(nullptr,nullptr,0);
        auto* bank=vc.Get();context->VSSetConstantBuffers(0,1,&bank);auto* output=out.Get();context->SOSetTargets(1,&output,&zero);
        context->Draw(UINT(vertices.size()),0);context->SOSetTargets(0,nullptr,nullptr);context->CopyResource(read.Get(),out.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};hr(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped),"gloss VS readback");
        std::vector<std::array<float,27>> actual(vertices.size());std::memcpy(actual.data(),mapped.pData,vertices.size()*outputStride);context->Unmap(read.Get(),0);
        for(size_t i=0;i<vertices.size();++i) {
            const auto base=vertexExpected(input[i],c);std::array<double,27> want{};
            std::copy_n(base.begin(),6,want.begin());want[6]=vertices[i].uv1[0];want[7]=vertices[i].uv1[1];
            for(size_t row=0;row<4;++row) {
                want[8+row]=c[12+row][3];
                for(size_t lane=0;lane<3;++lane)want[8+row]+=double(c[12+row][lane])*input[i].position[lane];
            }
            std::copy(base.begin()+6,base.end(),want.begin()+12);
            for(size_t lane=0;lane<27;++lane)nearValue(actual[i][lane],want[lane],"gloss VS",i,lane);
        }
        ++vsDraws;verticesObserved+=vertices.size();
    }
    void glossPixels() {
        ComPtr<ID3D11VertexShader> probe;ComPtr<ID3D11PixelShader> shader;
        hr(device->CreateVertexShader(kVSRigidGlossPixelProbe,sizeof(kVSRigidGlossPixelProbe),nullptr,&probe),"gloss pixel fixture");
        hr(device->CreatePixelShader(kPSRigidGloss,sizeof(kPSRigidGloss),nullptr,&shader),"original gloss PS");
        ComPtr<ID3D11Buffer> constants,parameters;
        D3D11_BUFFER_DESC bd{};bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.ByteWidth=51*sizeof(Vec);
        hr(device->CreateBuffer(&bd,nullptr,&constants),"gloss c50 bank");bd.ByteWidth=6*sizeof(Vec);
        hr(device->CreateBuffer(&bd,nullptr,&parameters),"gloss interpolators");
        auto* color=rtv.Get();context->OMSetRenderTargets(1,&color,nullptr);
        context->OMSetDepthStencilState(depth.Get(),0);context->OMSetBlendState(blend.Get(),nullptr,0xFFFFFFFF);
        const D3D11_VIEWPORT vp{0,0,2,2,0,1};context->RSSetViewports(1,&vp);context->RSSetState(raster.Get());
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(probe.Get(),nullptr,0);context->GSSetShader(nullptr,nullptr,0);context->PSSetShader(shader.Get(),nullptr,0);
        auto* cb=parameters.Get();context->VSSetConstantBuffers(2,1,&cb);cb=constants.Get();context->PSSetConstantBuffers(0,1,&cb);
        ID3D11ShaderResourceView* resources[]={nullptr,nullptr,baseView.Get()};context->PSSetShaderResources(0,3,resources);
        ID3D11SamplerState* states[]={nullptr,nullptr,sampler.Get()};context->PSSetSamplers(0,3,states);
        // Independent geometric oracle: camera along +X, half-vector +X;
        // reflection dot view is max(2*Nx*Nx-1,0). UV1 packs the object code,
        // sampled UV0 supplies RGB and the specular mask in alpha.
        // Keep the original boundary case as well as an off-boundary normal.
        // Color arithmetic rounds to float32 before the integer floor packing.
        for(const Vec normal:{Vec{1,0,0,0},Vec{0,1,0,0},Vec{.8660254f,.5f,0,0},Vec{.8f,.6f,0,0}})
        for(float exponent:{0.f,1.f,4.f})for(float scale:{0.f,.25f,1.f})
        for(float alpha:{0.f,.5f,1.f})for(float v:{.25f,.75f})
        for(unsigned shadowCase=0;shadowCase<6;++shadowCase) {
            const bool enabled=shadowCase!=0,receiver=shadowCase>=2;
            const bool characterLit=shadowCase<2||(shadowCase&1)==0;
            const bool worldLit=shadowCase<4;
            // Disabled/nonreceiver cases deliberately leave both shadow maps
            // unbound. Other cases independently darken character and world.
            ID3D11ShaderResourceView* shadows[]={receiver?views[0].Get():nullptr,receiver?views[1].Get():nullptr};
            ID3D11SamplerState* shadowStates[]={receiver?sampler.Get():nullptr,receiver?sampler.Get():nullptr};
            context->PSSetShaderResources(0,2,shadows);context->PSSetSamplers(0,2,shadowStates);
            const D3D11_BOX box{511,511,0,514,514,1};
            for(size_t bank=0;bank<2;++bank) {
                const float value=(bank==0?worldLit:characterLit)?.25f:.75f;
                Grid grid;grid.fill(Vec{value,value,value,-9});
                context->UpdateSubresource(textures[bank].Get(),0,&box,grid.data(),3*sizeof(Vec),0);
            }
            std::array<Vec,51> c{};c[4]={1,0,0,0};c[36]={0,1,0,0};c[40]={37,0,1,0};
            c[31][0]=receiver?1.f:0.f;c[46][0]=enabled?1.f:0.f;
            c[45][0]=1;c[47][0]=scale;c[50][0]=exponent;
            const std::array<Vec,6> p={Vec{.25f,.25f,.1875f,v},Vec{},Vec{0,0,.5f,1},Vec{0,0,.5f,1},normal,Vec{0,0,.9f,1}};
            const Vec texel={.125f,.25f,.375f,alpha};std::array<Vec,4> texels;texels.fill(texel);
            context->UpdateSubresource(constants.Get(),0,nullptr,c.data(),0,0);context->UpdateSubresource(parameters.Get(),0,nullptr,p.data(),0,0);
            context->UpdateSubresource(baseTexture.Get(),0,nullptr,texels.data(),2*sizeof(Vec),0);
            const Vec sentinel={-17,-19,-23,-29};context->ClearRenderTargetView(rtv.Get(),sentinel.data());
            context->Draw(3,0);++psDraws;context->CopyResource(staging.Get(),target.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"gloss readback");
            std::array<Vec,4> actual{};for(size_t y=0;y<2;++y)std::memcpy(actual.data()+2*y,static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch,2*sizeof(Vec));
            context->Unmap(staging.Get(),0);
            const double length=std::hypot(double(normal[0]),double(normal[1]));
            const double nx=normal[0]/length,ny=normal[1]/length;
            const double specular=scale*std::pow(std::max(2*nx*nx-1,0.0),exponent);
            const float red=std::min(float(sat(texel[0]+alpha*specular)),.96f);
            const float green=std::min(float(sat(texel[1]+alpha*specular)),.96f);
            const double blue=sat(texel[2]+alpha*specular);
            const std::array<double,4> want={
                (37+32*std::floor(32*(2*v-(v>=.5f?1:0)))+12)*double(1.0f/2046),
                (32*std::floor(32*green)+std::floor(32*red))*double(1.0f/1023),
                blue*double(.2f)+.5+(ny<=.125?.25:0),
                enabled&&ny>0&&(!characterLit||!worldLit)?1.0:double(.7f)};
            for(size_t i=0;i<4;++i)for(size_t lane=0;lane<4;++lane)
                nearValue(actual[i][lane],want[lane],"gloss packing/specular",i,lane);
        }
    }
    void pixels(const Probe& input,const PConstants& c,const std::array<Grid,2>& grids,const char* name,bool bind=true,bool observeNormal=false) {
        context->UpdateSubresource(pc.Get(),0,nullptr,c.data(),0,0);context->UpdateSubresource(inputs.Get(),0,nullptr,input.data(),0,0);
        const D3D11_BOX box{511,511,0,514,514,1};
        for(size_t slot=0;slot<2;++slot)context->UpdateSubresource(textures[slot].Get(),0,&box,grids[slot].data(),3*sizeof(Vec),0);
        const Vec sentinel={-17,-19,-23,-29};context->ClearRenderTargetView(rtv.Get(),sentinel.data());
        auto* color=rtv.Get();context->OMSetRenderTargets(1,&color,nullptr);context->OMSetDepthStencilState(depth.Get(),0);context->OMSetBlendState(blend.Get(),nullptr,0xFFFFFFFF);
        const D3D11_VIEWPORT vp{0,0,2,2,0,1};context->RSSetViewports(1,&vp);context->RSSetState(raster.Get());
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(pixelProbe.Get(),nullptr,0);context->GSSetShader(nullptr,nullptr,0);context->PSSetShader(observeNormal?normalProbe.Get():ps.Get(),nullptr,0);
        auto* cb=inputs.Get();context->VSSetConstantBuffers(1,1,&cb);cb=pc.Get();context->PSSetConstantBuffers(0,1,&cb);
        ID3D11ShaderResourceView* resources[]={bind?views[0].Get():nullptr,bind?views[1].Get():nullptr};context->PSSetShaderResources(0,2,resources);
        ID3D11SamplerState* states[]={bind?sampler.Get():nullptr,bind?sampler.Get():nullptr};context->PSSetSamplers(0,2,states);
        context->UpdateSubresource(baseTexture.Get(),0,nullptr,baseTexels.data(),2*sizeof(Vec),0);
        auto* baseResource=textured?baseView.Get():nullptr;context->PSSetShaderResources(2,1,&baseResource);
        auto* baseState=textured?sampler.Get():nullptr;context->PSSetSamplers(2,1,&baseState);
        context->Draw(3,0);++psDraws;context->CopyResource(staging.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"PS actual pixels");
        std::array<Vec,4> actual{};for(size_t y=0;y<2;++y)std::memcpy(actual.data()+2*y,static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch,2*sizeof(Vec));context->Unmap(staging.Get(),0);
        const auto column=size_t(std::clamp(std::floor(double(input[0][0])*2),0.0,1.0));
        const auto row=size_t(std::clamp(std::floor(double(input[0][1])*2),0.0,1.0));
        auto want=pixelExpected(input,c,grids,textured?&baseTexels[2*row+column]:nullptr,dual);
        if(observeNormal) {
            const double length=std::hypot(double(input[3][0]),double(input[3][1]),double(input[3][2]));
            for(size_t lane=0;lane<4;++lane)want[lane]=length==0?0:input[3][std::min(lane,size_t(2))]/length;
        }
        for(size_t i=0;i<4;++i)for(size_t lane=0;lane<4;++lane) {
            nearValue(actual[i][lane],want[lane],name,i,lane);
            if(observeNormal&&want[lane]==0)need(actual[i][lane]==0&&!std::signbit(actual[i][lane]),"Legacy normalization did not produce positive zero");
        }
    }
};
}
int main(int argc,char** argv)try {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    need(argc==1||(argc==2&&std::string(argv[1])=="--hardware"),"Optional --hardware expected");
    {
        Fixture gloss(argc==2,true);gloss.glossPixels();gloss.multitoneVertices();gloss.multitonePixels();gloss.normalmapVertices();
    }
    {
        Fixture dual(argc==2,true,true);
        PConstants c{};c[31][0]=1;c[36][1]=1;c[40][0]=37;c[46][0]=1;
        const Probe p={Vec{.3125f,.625f,.1875f,.8125f},Vec{0,0,.5f,1},
            Vec{0,0,.5f,1},Vec{0,2,0,0},Vec{0,0,.9f,1}};
        std::array<Grid,2> grids{};
        dual.pixels(p,c,grids,"dual fixed UV0/UV1 sample",false);
    }
    for(bool textured:{false,true}) {
    Fixture f(argc==2,textured);
    const float nan=std::numeric_limits<float>::quiet_NaN();VConstants vc{};for(auto& row:vc)row.fill(nan);
    for(size_t base:{0u,12u,22u,26u})for(size_t row=0;row<4;++row){vc[base+row]={};vc[base+row][row]=1;}
    std::vector<Vertex> input(32);
    for(size_t i=0;i<input.size();++i)input[i]={{float(int(i)-16)*.125f,float(i%5)-2,float(i%3)*.25f},{.25f+float(i%3),-2,.5f},
        {float(i)*.125f,-.25f,float(i%4)*.5f,1.75f},{float(i)*.25f-3,float(i%7)*.125f}};
    input[0].normal={0,0,0};input[1].normal={-0.f,0,-0.f};
    f.vertices(input,vc);f.glossVertices(input,vc);
    for(size_t base:{0u,12u,22u,26u})for(size_t row=0;row<4;++row)for(size_t lane=0;lane<4;++lane)
        vc[base+row][lane]=(row==lane?1.25f:0)+float(int((base+3*row+lane)%9)-4)*.0625f;
    f.vertices(input,vc);f.glossVertices(input,vc);
    // Isolate every matrix bank; nonaffine W and nonunit normals catch hidden
    // normalization, matrix transpose and missing world-to-shadow composition.
    for(size_t base:{0u,12u,22u,26u}){auto one=vc;for(size_t row=0;row<4;++row)one[base+row][3]+=float(row+1)*.5f;f.vertices(input,one);f.glossVertices(input,one);}
    PConstants pc{};for(auto& row:pc)row.fill(nan);
    pc[30][1]=0;pc[31][0]=1;pc[36]={0,1,0,nan};pc[40][0]=37;pc[46][0]=1;pc[47][0]=0;pc[49][2]=0;
    const float center=512.5f/1024;
    Probe probe={Vec{.3125f,.625f,0,0},Vec{(center-.5f)*2,(.5f-center)*2,.5f,1},Vec{(center-.5f)*2,(.5f-center)*2,.5f,1},Vec{0,2,0,0},Vec{-5,8,.9f,11}};
    std::array<Grid,2> grids{};for(auto& grid:grids)grid.fill(Vec{.25f,.5f,.75f,-9});
    // Observe the normalization result directly so later min/max/saturate
    // cannot accidentally hide an infinity-times-zero NaN.
    for(const Vec normal:{Vec{0,0,0,0},Vec{-0.f,0,-0.f,0},Vec{0,2,0,0},Vec{-3,0,4,0},Vec{1,-2,3,0}}) {
        auto one=probe;one[3]=normal;f.pixels(one,pc,grids,"normalization result",false,true);
    }
    for(float zero:{0.f,-0.f})for(float enabled:{0.f,1.f})for(float receiver:{-1.f,1.f})
        for(float rim:{0.f,1.f})for(float custom:{0.f,511.f}) {
            auto one=probe;one[3]={zero,zero,zero,0};auto c=pc;
            c[47][0]=enabled;c[31][0]=receiver;c[46][0]=rim;c[49][2]=custom;
            f.pixels(one,c,grids,"zero-normal final packing",enabled!=0&&receiver>0);
        }
    f.pixels(probe,pc,grids,"shadow disabled",false);
    // Equality is SETGTE (including blue, V-half and the rim threshold).
    for(float blue:{std::nextafter(.9f,0.f),.9f,std::nextafter(.9f,1.f)})for(float v:{std::nextafter(.5f,0.f),.5f,std::nextafter(.5f,1.f)}) {
        auto one=probe;one[4][2]=blue;one[0][1]=v;f.pixels(one,pc,grids,"packing equality",false);
    }
    for(float light:{std::nextafter(.125f,0.f),.125f,std::nextafter(.125f,1.f)})for(float custom:{0.f,-7.f,511.f}) {
        auto c=pc;c[36][1]=light;c[49][2]=custom;c[46][0]=2;f.pixels(probe,c,grids,"rim and custom object id",false);
    }
    for(float u:{-2.125f,0.f,.999f,3.625f}){auto one=probe;one[0][0]=u;one[0][1]=-1.375f;f.pixels(one,pc,grids,"UV wrap arithmetic",false);}
    if(textured)for(size_t channel=0;channel<4;++channel)for(float value:{-.5f,0.f,.125f,.5f,.96f,1.5f}) {
        const auto saved=f.baseTexels;
        for(auto& texel:f.baseTexels)texel[channel]=value;
        for(float vertical:{-.25f,.25f,.75f,1.25f})for(float horizontal:{-.25f,.25f,.75f,1.25f}) {
            auto one=probe;one[0]={horizontal,vertical,0,0};
            f.pixels(one,pc,grids,"base texture channels, clipping and coordinates",false);
        }
        f.baseTexels=saved;
    }
    pc[47][0]=1;
    for(float receiver:{-2.f,-0.f,0.f}){auto c=pc;c[31][0]=receiver;f.pixels(probe,c,grids,"nonreceiver unbound",false);}
    for(float enabled:{-2.f,1.f})for(float amount:{0.f,.25f,1.f})for(float normalY:{-1.f,0.f,1.f}) {
        auto c=pc;c[47][0]=enabled;c[30][1]=amount;auto one=probe;one[3]={1,normalY,0,0};f.pixels(one,c,grids,"shadow gates");
    }
    // Each of nine taps in each bank independently changes the FINAL alpha.
    // At half-texel fractions the weights (in eighths) are {1,2,1,2,4,2,1,2,1}.
    // Pick a subset totaling 8 minus this tap's weight: off -> shadow .4,
    // on -> lit .01. Only the original selected R/G/B lane changes.
    const int weights[]={1,2,1,2,4,2,1,2,1};
    for(size_t bank=0;bank<2;++bank)for(size_t tap=0;tap<9;++tap) {
        unsigned subset=0;bool found=false;
        for(unsigned mask=0;mask<512&&!found;++mask)if(!(mask&(1u<<tap))) {
            int sum=0;for(size_t i=0;i<9;++i)if(mask&(1u<<i))sum+=weights[i];
            if(sum==8-weights[tap]){subset=mask;found=true;}
        }
        need(found,"Meaningful tap isolation subset missing");
        for(unsigned on=0;on<2;++on) {
            auto isolated=grids;for(auto& texel:isolated[1-bank])texel={.25f,.25f,.25f,17};
            for(size_t i=0;i<9;++i){isolated[bank][i]={.9375f,.8125f,.6875f,-11};isolated[bank][i][i%3]=((subset&(1u<<i))||(i==tap&&on))?.25f:.75f;}
            const auto want=pixelExpected(probe,pc,isolated);nearValue(float(want[3]),on?.01:.4,"independent tap outcome",bank*9+tap,3);
            char name[100];sprintf_s(name,"shadow bank%zu tap%zu selected channel%zu on%u",bank,tap,tap%3,on);
            f.pixels(probe,pc,isolated,name);
        }
    }
    // Depth equality must be lit: all channels equal 1 - projected depth.
    for(auto& grid:grids)grid.fill(Vec{.5f,.5f,.5f,-99});f.pixels(probe,pc,grids,"depth equality");
    // Distinct projected coordinates and W exercise reciprocal, axis sign,
    // fixed 1024 fractional weights and independent bank ownership.
    for(float fraction:{.125f,.375f,.75f}) {
        auto one=probe;one[1][0]=((512+fraction)/1024-.5f)*4;one[1][1]=(.5f-(512+.75f)/1024)*4;one[1][2]=1;one[1][3]=2;
        for(size_t bank=0;bank<2;++bank)for(size_t i=0;i<9;++i)grids[bank][i]={float((i+bank)%3)*.375f,float((i+2*bank+1)%3)*.375f,float((2*i+bank)%3)*.375f,23};
        f.pixels(one,pc,grids,"projection and fractional filter");
    }
    }
    std::printf("PASS rigid shaders: %zu checks; %zu actual VS draws/%zu vertices, %zu actual PS draws; both opaque variants, all exports, base channels, stage-local constants, branches, equality and 18 isolated RGB taps\n",checks,vsDraws,verticesObserved,psDraws);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL rigid shaders after %zu checks: %s\n",checks,e.what());return 1;}
