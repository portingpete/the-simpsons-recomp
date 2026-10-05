// Real D3D11 shader execution against independent geometric/material formulae.
// No runtime shader compilation or instruction interpreter.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "renderer/skin_mesh.h"
#include "renderer/skin_input.h"
#include "runtime/skin_material_constants.h"
#include "VSSkin.h"
#include "PSSkin.h"
#include "VSSkinPixelProbe.h"
#include "GSSkinProbe.h"
#include "VSSkinDual.h"
#include "PSSkinDual.h"
#include "GSSkinDualProbe.h"
#include "VSSkinDualPixelProbe.h"

using Microsoft::WRL::ComPtr;
using Simpsons::Graphics::SkinVertex;
using Vec=std::array<float,4>;
using DVec=std::array<double,4>;
using VC=std::array<Vec,256>;
using PC=std::array<Vec,64>;
using Probe=std::array<Vec,6>;
struct Vertex {SkinVertex skin;};
static_assert(sizeof(Vertex)==160);
size_t checks{},vertexDraws{},pixelDraws{};
void need(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
void hr(HRESULT h,const char* why){if(FAILED(h)){char b[180];sprintf_s(b,"%s HRESULT %08lX",why,ULONG(h));throw std::runtime_error(b);}}
void nearValue(float actual,double expected,const char* label,size_t n,size_t lane) {
    ++checks;
    if(!std::isfinite(actual)||std::abs(actual-expected)>0.000008+0.000008*std::abs(expected)) {
        char b[220];sprintf_s(b,"%s sample%zu lane%zu actual %.9g expected %.12g",label,n,lane,actual,expected);
        throw std::runtime_error(b);
    }
}
double sat(double v){return std::clamp(v,0.0,1.0);}
double frac(double v){return v-std::floor(v);}
double mix(double a,double b,double t){return a+(b-a)*t;}
DVec transform(const VC& c,size_t start,const DVec& p) {
    DVec out{};for(size_t r=0;r<4;++r)for(size_t l=0;l<4;++l)out[r]+=double(c[start+r][l])*p[l];return out;
}
std::vector<double> vertexExpected(const Vertex& vertex,const VC& c,bool dual) {
    const auto& v=vertex.skin;
    DVec p{v.position[0],v.position[1],v.position[2],1},skinned{0,0,0,1},normal{};
    if(c[39][3]>.5f) {
        const std::array<std::array<float,3>,6> morphs{v.morph1,v.morph2,v.morph3,v.morph4,v.morph5,v.morph6};
        for(size_t i=0;i<6;++i)for(size_t lane=0;lane<3;++lane)p[lane]+=morphs[i][lane]*double(c[38+i/4][i%4]);
    }
    for(size_t bone=0;bone<4;++bone)for(size_t r=0;r<3;++r) {
        const auto& row=c[52+3*size_t(v.indices[bone])+r];
        for(size_t lane=0;lane<4;++lane)skinned[r]+=double(v.weights[bone])*row[lane]*p[lane];
        for(size_t lane=0;lane<3;++lane)normal[r]+=double(v.weights[bone])*row[lane]*v.normal[lane];
    }
    double length=0;for(size_t i=0;i<3;++i)length+=normal[i]*normal[i];length=std::sqrt(length);
    for(size_t i=0;i<3;++i)normal[i]=length==0?0:normal[i]/length;
    const auto clip=transform(c,0,skinned),world=transform(c,12,skinned),n=transform(c,12,normal);
    std::vector<double> out(clip.begin(),clip.end());out.insert(out.end(),v.uv.begin(),v.uv.end());
    if(dual)out.insert(out.end(),vertex.skin.uv1.begin(),vertex.skin.uv1.end());
    out.insert(out.end(),n.begin(),n.begin()+3);out.insert(out.end(),world.begin(),world.end());
    if(dual){const auto shadow=transform(c,26,world);out.insert(out.end(),shadow.begin(),shadow.end());}
    out.insert(out.end(),v.color.begin(),v.color.end());return out;
}
const std::array<Vec,4> baseTexels{{{.125f,.25f,.75f,-9},{.5f,.625f,.25f,5},
                                  {.875f,.375f,.5f,1},{1.25f,-.25f,1,0}}};
const std::array<Vec,9> shadowGrid{{{.2f,.4f,.6f,7},{.9f,.1f,.3f,7},{.3f,.8f,.2f,7},
    {.6f,.3f,.8f,7},{.8f,.7f,.2f,7},{.1f,.2f,.4f,7},{.7f,.5f,.1f,7},{.4f,.8f,.5f,7},{.9f,.2f,.6f,7}}};
DVec baseExpected(const Vec& uv) {
    const double x=2*frac(uv[0])-.5,y=2*frac(uv[1])-.5;const int ix=int(std::floor(x)),iy=int(std::floor(y));
    const auto sample=[&](int dx,int dy,size_t l){return double(baseTexels[size_t(((iy+dy)%2+2)%2)*2+size_t(((ix+dx)%2+2)%2)][l]);};
    DVec result{};for(size_t l=0;l<4;++l)result[l]=mix(mix(sample(0,0,l),sample(1,0,l),frac(x)),mix(sample(0,1,l),sample(1,1,l),frac(x)),frac(y));
    return result;
}
double shadowExpected(const Vec& projection) {
    const double u=.5+.5*projection[0]/projection[3],v=.5-.5*projection[1]/projection[3];
    const double threshold=1-sat(double(projection[2])/projection[3]);
    const auto test=[&](int x,int y) {
        const int col=int(std::floor(u*1024))+x-511,row=int(std::floor(v*1024))+y-511;
        const double sample=col>=0&&col<3&&row>=0&&row<3?shadowGrid[size_t(row*3+col)][size_t(x+1)]:.75;
        return threshold>=sample?1.0:0.0;
    };
    const double left=test(-1,0)+mix(test(-1,-1),test(-1,1),frac(v*1024));
    const double center=test(0,0)+mix(test(0,-1),test(0,1),frac(v*1024));
    const double right=test(1,0)+mix(test(1,-1),test(1,1),frac(v*1024));
    return .5*(mix(left,right,frac(u*1024))+center);
}
DVec pixelExpected(const Probe& input,const PC& c) {
    const auto base=baseExpected(input[0]);double nl=0,vl=0;
    std::array<double,3> view{};
    for(size_t i=0;i<3;++i){nl+=double(input[2][i])*input[2][i];view[i]=double(c[4][i])-input[3][i];vl+=view[i]*view[i];}
    nl=std::sqrt(nl);vl=std::sqrt(vl);double light=0;
    for(size_t i=0;i<3;++i) {
        const double n=nl==0?0:input[2][i]/nl;
        const double direction=c[49][3]*double(c[36][i])+(1-double(c[49][3]))*(vl==0?0:view[i]/vl)+(i==0?c[40][2]:0);
        light+=n*direction;
    }
    const double q=(.25*sat(light)+c[40][1])/(1+double(c[40][1]));
    const double rim=.25*c[46][0]*(double(.1f)>=std::min(q*q,.25)?1:0)*(1-std::floor(input[5][0]));
    const double id=c[49][2]!=0?c[49][2]:c[40][0];
    const double visibility=c[47][0]!=0?sat((1-double(c[30][1]))*(c[31][0]>0?shadowExpected(input[4]):1)+c[30][1]):1;
    return {(id+32*std::floor(32*frac(2*input[1][1]))+std::floor(64*frac(input[1][0])))*double(1.0f/2046.0f),
        (std::floor(32*std::clamp(base[0],0.0,double(.96f)))+32*std::floor(32*std::clamp(base[1],0.0,double(.96f))))*double(1.0f/1023.0f),
        .5*(input[5][2]>=.9f?1:0)+rim+double(.2f)*base[2],
        double(.7f)+double(.3f)*(1-std::floor(visibility))};
}
// Controlled opaque-material domain: view and authored light face +Z, normals
// face +/-Z. Palette packing and both shading bands are independent of the
// dual-textured shader and must reach the actual render target in every lane.
DVec opaquePixelExpected(const Probe& input,const PC& c) {
    const bool lit=input[1][2]>0;
    const bool shadowSide=c[32][2]==0||!lit;
    const double lightBand=(.2*(lit?1:0)+c[40][1])/(1+double(c[40][1]));
    const double rim=(shadowSide?(lightBand*lightBand<=.125?.125:0):(c[33][2]>=.04?.0625:0))*c[47][0]*
        (1-std::floor(input[3][0]))*(c[39][3]>=.5f?0:1);
    const double id=c[49][2]!=0?c[49][2]:c[40][0];
    return {id*double(1.0f/1023.0f),
        (std::floor(64*frac(input[0][0]))+32*std::floor(32*frac(2*input[0][1])))*double(1.0f/1023.0f),
        .5*(input[3][2]>=.9f?1:0)+.25*(frac(input[0][1])>=.5?1:0)+rim,
        double(.01f)};
}
struct Fixture {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Buffer> vc,pc,probe;
    ComPtr<ID3D11VertexShader> pixelVS,opaquePixelVS;ComPtr<ID3D11PixelShader> ps,opaquePS;
    ComPtr<ID3D11Texture2D> target,read,shadow,base;
    ComPtr<ID3D11RenderTargetView> rtv;ComPtr<ID3D11ShaderResourceView> shadowView,baseView;
    ComPtr<ID3D11SamplerState> shadowSampler,baseSampler;
    ComPtr<ID3D11RasterizerState> raster;ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11Buffer> buffer(UINT size,UINT binds,const void* data=nullptr,D3D11_USAGE usage=D3D11_USAGE_DEFAULT) {
        D3D11_BUFFER_DESC d{};d.ByteWidth=size;d.BindFlags=binds;d.Usage=usage;
        if(usage==D3D11_USAGE_STAGING)d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        D3D11_SUBRESOURCE_DATA init{data,0,0};ComPtr<ID3D11Buffer> out;
        hr(device->CreateBuffer(&d,data?&init:nullptr,&out),"buffer");return out;
    }
    explicit Fixture(bool hardware) {
        D3D_FEATURE_LEVEL level{};const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
        hr(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,
            levels,1,D3D11_SDK_VERSION,&device,&level,&context),"device");
        need(level==D3D_FEATURE_LEVEL_11_0,"SM5 required");
        vc=buffer(sizeof(VC),D3D11_BIND_CONSTANT_BUFFER);pc=buffer(sizeof(PC),D3D11_BIND_CONSTANT_BUFFER);probe=buffer(sizeof(Probe),D3D11_BIND_CONSTANT_BUFFER);
        hr(device->CreateVertexShader(kVSSkinDualPixelProbe,sizeof(kVSSkinDualPixelProbe),nullptr,&pixelVS),"pixel fixture VS");
        hr(device->CreatePixelShader(kPSSkinDual,sizeof(kPSSkinDual),nullptr,&ps),"dual PS");
        hr(device->CreateVertexShader(kVSSkinPixelProbe,sizeof(kVSSkinPixelProbe),nullptr,&opaquePixelVS),"opaque pixel fixture VS");
        hr(device->CreatePixelShader(kPSSkin,sizeof(kPSSkin),nullptr,&opaquePS),"opaque PS");
        D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=2;td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;
        td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        hr(device->CreateTexture2D(&td,nullptr,&target),"target");hr(device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"RTV");
        td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        hr(device->CreateTexture2D(&td,nullptr,&read),"target staging");
        td.Usage=D3D11_USAGE_DEFAULT;td.CPUAccessFlags=0;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{baseTexels.data(),2*sizeof(Vec),0};
        hr(device->CreateTexture2D(&td,&data,&base),"base texture");hr(device->CreateShaderResourceView(base.Get(),nullptr,&baseView),"base view");
        td.Width=td.Height=1024;std::vector<Vec> pixels(1024*1024,Vec{.75f,.75f,.75f,7});
        for(size_t y=0;y<3;++y)for(size_t x=0;x<3;++x)pixels[(511+y)*1024+511+x]=shadowGrid[y*3+x];
        data={pixels.data(),1024*sizeof(Vec),0};hr(device->CreateTexture2D(&td,&data,&shadow),"shadow texture");
        hr(device->CreateShaderResourceView(shadow.Get(),nullptr,&shadowView),"shadow view");
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxAnisotropy=1;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;
        hr(device->CreateSamplerState(&sd,&shadowSampler),"point clamp base-level shadow sampler");
        sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        hr(device->CreateSamplerState(&sd,&baseSampler),"linear wrap base sampler");
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        hr(device->CreateRasterizerState(&rd,&raster),"raster");
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthFunc=D3D11_COMPARISON_ALWAYS;hr(device->CreateDepthStencilState(&dd,&depth),"depth disabled");
    }
    void vertices(const std::vector<Vertex>& input,const VC& constants,bool dual) {
        ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11InputLayout> layout;
        const void* code=dual?static_cast<const void*>(kVSSkinDual):kVSSkin;
        const SIZE_T bytes=dual?sizeof(kVSSkinDual):sizeof(kVSSkin);
        hr(device->CreateVertexShader(code,bytes,nullptr,&vs),"skin VS");
        std::vector<D3D11_INPUT_ELEMENT_DESC> elems(Simpsons::Graphics::SkinInputElements.begin(),Simpsons::Graphics::SkinInputElements.end());
        if(dual)elems.push_back({"TEXCOORD",12,DXGI_FORMAT_R32G32_FLOAT,0,152,D3D11_INPUT_PER_VERTEX_DATA,0});
        hr(device->CreateInputLayout(elems.data(),UINT(elems.size()),code,bytes,&layout),"production skin layout");
        std::vector<D3D11_SO_DECLARATION_ENTRY> outputs{{0,"SV_Position",0,0,4,0},{0,"TEXCOORD",0,0,2,0}};
        if(dual)outputs.push_back({0,"TEXCOORD",1,0,2,0});
        outputs.push_back({0,"TEXCOORD",dual?2u:1u,0,3,0});outputs.push_back({0,"TEXCOORD",dual?3u:2u,0,4,0});
        if(dual)outputs.push_back({0,"TEXCOORD",4,0,4,0});outputs.push_back({0,"TEXCOORD",dual?5u:3u,0,4,0});
        const UINT lanes=dual?23:17,stride=lanes*sizeof(float);
        hr(device->CreateGeometryShaderWithStreamOutput(dual?static_cast<const void*>(kGSSkinDualProbe):kGSSkinProbe,
            dual?sizeof(kGSSkinDualProbe):sizeof(kGSSkinProbe),outputs.data(),UINT(outputs.size()),&stride,1,
            D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&gs),"skin output observer");
        auto vb=buffer(UINT(input.size()*sizeof(Vertex)),D3D11_BIND_VERTEX_BUFFER,input.data(),D3D11_USAGE_IMMUTABLE);
        auto out=buffer(UINT(input.size()*stride),D3D11_BIND_STREAM_OUTPUT);
        auto staging=buffer(UINT(input.size()*stride),0,nullptr,D3D11_USAGE_STAGING);
        context->OMSetRenderTargets(0,nullptr,nullptr);context->UpdateSubresource(vc.Get(),0,nullptr,constants.data(),0,0);
        context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        auto* v=vb.Get();const UINT inputStride=sizeof(Vertex),zero=0;context->IASetVertexBuffers(0,1,&v,&inputStride,&zero);
        context->VSSetShader(vs.Get(),nullptr,0);context->GSSetShader(gs.Get(),nullptr,0);context->PSSetShader(nullptr,nullptr,0);
        auto* b=vc.Get();context->VSSetConstantBuffers(0,1,&b);b=out.Get();context->SOSetTargets(1,&b,&zero);
        context->Draw(UINT(input.size()),0);context->SOSetTargets(0,nullptr,nullptr);context->CopyResource(staging.Get(),out.Get());
        D3D11_MAPPED_SUBRESOURCE map{};hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"vertex readback");
        std::vector<float> actual(input.size()*lanes);std::memcpy(actual.data(),map.pData,actual.size()*sizeof(float));context->Unmap(staging.Get(),0);
        for(size_t i=0;i<input.size();++i){const auto expected=vertexExpected(input[i],constants,dual);need(expected.size()==lanes,"oracle layout");
            for(size_t l=0;l<lanes;++l)nearValue(actual[i*lanes+l],expected[l],dual?"dual VS":"skin VS",i,l);}
        ++vertexDraws;
    }
    void pixel(const Probe& input,const PC& constants,size_t sample,bool dual=true) {
        auto* targetView=rtv.Get();context->OMSetRenderTargets(1,&targetView,nullptr);context->ClearRenderTargetView(targetView,Vec{-99,-99,-99,-99}.data());
        context->UpdateSubresource(pc.Get(),0,nullptr,constants.data(),0,0);context->UpdateSubresource(probe.Get(),0,nullptr,input.data(),0,0);
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(dual?pixelVS.Get():opaquePixelVS.Get(),nullptr,0);context->GSSetShader(nullptr,nullptr,0);
        context->PSSetShader(dual?ps.Get():opaquePS.Get(),nullptr,0);
        auto* b=probe.Get();context->VSSetConstantBuffers(2,1,&b);b=pc.Get();context->PSSetConstantBuffers(0,1,&b);
        ID3D11ShaderResourceView* views[]{shadowView.Get(),baseView.Get()};context->PSSetShaderResources(0,2,views);
        ID3D11SamplerState* samplers[]{shadowSampler.Get(),baseSampler.Get()};context->PSSetSamplers(0,2,samplers);
        const D3D11_VIEWPORT vp{0,0,2,2,0,1};context->RSSetViewports(1,&vp);context->RSSetState(raster.Get());context->OMSetDepthStencilState(depth.Get(),0);
        context->OMSetBlendState(nullptr,nullptr,UINT_MAX);context->Draw(3,0);context->CopyResource(read.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE map{};hr(context->Map(read.Get(),0,D3D11_MAP_READ,0,&map),"pixel readback");
        const auto expected=dual?pixelExpected(input,constants):opaquePixelExpected(input,constants);std::array<Vec,4> actual{};
        for(size_t y=0;y<2;++y)std::memcpy(actual.data()+y*2,static_cast<const char*>(map.pData)+y*map.RowPitch,2*sizeof(Vec));
        context->Unmap(read.Get(),0);
        for(const auto& color:actual)for(size_t l=0;l<4;++l)nearValue(color[l],expected[l],dual?"dual PS":"opaque PS",sample,l);
        ++pixelDraws;
    }
};
int main(int argc,char** argv)try {
    need(argc==1||(argc==2&&std::string(argv[1])=="--hardware"),"Unexpected arguments");Fixture gpu(argc==2);
    VC c{};c[0]={.4f,.1f,.05f,.2f};c[1]={-.1f,.6f,0,-.15f};c[2]={0,.15f,.3f,.1f};c[3]={.01f,.02f,0,1};
    c[12]={.9f,.2f,.1f,-.3f};c[13]={-.2f,.8f,.1f,.4f};c[14]={.1f,-.1f,1.1f,.5f};c[15]={0,0,0,1};
    c[26]={.1f,.2f,0,.4f};c[27]={-.2f,.1f,.05f,.3f};c[28]={.01f,.02f,.15f,.2f};c[29]={0,0,.01f,1};
    c[38]={.5f,-.125f,.25f,.75f};c[39]={-.2f,.1f,0,0};
    for(size_t b=0;b<64;++b){const float f=float(b)/64;
        c[52+3*b]={1+.3f*f,.02f*f,-.05f*f,.4f*f};c[53+3*b]={-.04f*f,.8f+.2f*f,.06f*f,-.7f*f};c[54+3*b]={.03f*f,-.09f*f,1.1f-.3f*f,.5f*f};}
    for(uint32_t source:{0x82006348u,0x8201CD48u}) {
        const auto profile=Simpsons::skinProfile(source);
        std::vector<uint32_t> words(profile.words,0x7FC12345);
        std::array<uint8_t,128> modified{};VC projected=c;
        for(size_t b=0;b<64;++b) {
            const size_t leaf=profile.boneLeaf+b;modified[leaf/8]|=uint8_t(0x80>>(leaf&7));
            for(size_t row=0;row<3;++row)for(size_t lane=0;lane<4;++lane)
                words[profile.boneWord+16*b+4*row+lane]=std::bit_cast<uint32_t>(c[52+3*b+row][lane]);
            for(size_t row=0;row<3;++row)projected[52+3*b+row]={99,98,97,96};
        }
        // Private fourth vectors are deliberately NaN: they are not uploaded.
        // Distinct rotations/translations expose a second transpose immediately.
        Simpsons::projectSkinBoneMatrices(words,modified,projected,source);
        need(projected==c,"Skin upload transposed private bones twice or read the fourth vector");
        modified.fill(0);const auto retained=projected;
        words.assign(profile.words,0x7FC12345);
        Simpsons::projectSkinBoneMatrices(words,modified,projected,source);
        need(projected==retained,"Unmodified skin bones lost their retained palette");
        c=projected;
    }
    VC inherited=c;for(size_t r=52;r<56;++r)inherited[r]={9,8,7,6};
    Simpsons::applySkinBonePalette(inherited,c);
    need(inherited==c,"Zero bone lanes must overwrite stale staging values");
    VC emptyPalette{};emptyPalette[0]={7,8,9,10};emptyPalette[255]={11,12,13,14};
    Simpsons::applySkinBonePalette(inherited,emptyPalette);
    need(inherited[0]==c[0]&&inherited[255]==c[255]&&inherited[52]==Vec{}&&inherited[243]==Vec{},"Palette owns exactly its 192 register rows");
    std::vector<Vertex> vertices(128);
    for(size_t i=0;i<vertices.size();++i){auto& v=vertices[i].skin;const float f=float(i);
        v.position={.01f*f-.5f,.07f*float(i%9)-.2f,.03f*float(i%11)};v.normal=i%13?std::array<float,3>{.2f+.01f*f,-.3f,.8f}:std::array<float,3>{};
        v.uv={-.7f+.013f*f,1.3f-.021f*f};vertices[i].skin.uv1={2.1f-.027f*f,-.8f+.019f*f};
        v.indices={float(i%64),float((i+17)%64),float((i+33)%64),float((i+63)%64)};
        v.weights={.125f,.25f,.375f,.25f};if(i%3==0){v.weights={0,0,0,0};v.weights[i%4]=1;}
        v.color={.03f*float(i%19),.04f*float(i%17),.05f*float(i%23),.2f+.06f*float(i%11)};
        v.morph1={.1f,-.2f,.3f};v.morph2={-.4f,.5f,.6f};v.morph3={.7f,.8f,-.9f};
        v.morph4={-.2f,-.3f,.4f};v.morph5={.3f,-.5f,.7f};v.morph6={.1f,.15f,-.25f};}
    for(float flag:{0.0f,.5f,.5001f,1.0f}){c[39][3]=flag;gpu.vertices(vertices,c,false);gpu.vertices(vertices,c,true);}
    for(size_t i=0;i<192;++i) {
        PC p{};p[4]={2,-1,3,1};p[36]={.3f,-.5f,.8f,0};p[49]={0,0,i%3?7.0f:0,float(i%5)/4};
        p[40]={13,float(i%7)*.2f-.2f,float(i%3)*.1f,0};p[46][0]=float(i%3)*.5f;p[47][0]=i%4?1.0f:0;
        p[30][1]=float(i%5)*.2f;p[31][0]=i%5?1.0f:0;
        Probe input{};input[0]={float(i%9)*.125f-.5f,float(i%7)*.125f-.25f,0,0};input[1]={float(i%11)*.0625f-.125f,float(i%13)*.0625f,0,0};
        input[2]=i%17?Vec{float(i%5)*.3f-.5f,.7f,-.3f,0}:Vec{};input[3]=i%19?Vec{-.7f,.4f,.9f,1}:p[4];
        input[4]={float(i%4)*.5f/1024,-float(i%3)*.5f/1024,float(i%7)*.25f,2};
        input[5]={i%2?1.0f:0,0,i%3?.95f:.8f,1};gpu.pixel(input,p,i);
    }
    for(size_t i=0;i<256;++i) {
        PC p{};p[4]={0,0,3,1};p[32]={0,0,1,0};p[40][0]=13;
        p[49][2]=i%3?7.0f:0;p[33][2]=i%2?.1f:.01f;
        p[47][0]=float(i%3);p[39][3]=i%5?0.0f:1.0f;
        // Original Homer commits carry a zero fakeLightDir. Xenos scalar
        // multiplication preserves zero through normalization's reciprocal
        // square root of zero; IEEE 0*infinity would poison the shadow test.
        if(i>=128){p[32]={};p[40][1]=.34f;}
        Probe input{};input[0]={float(i%9)*.125f-.5f,float(i%7)*.125f-.25f,0,0};
        input[1]={0,0,i%4?2.0f:-2.0f,0};input[2]={0,0,0,1};
        input[3]={i%7?0.0f:1.0f,0,i%3?.95f:.8f,1};
        gpu.pixel(input,p,i,false);
    }
    std::printf("PASS skin shader %s: %zu checks, %zu vertex draws (1024 vertices), %zu pixel draws\n",argc==2?"hardware":"WARP",checks,vertexDraws,pixelDraws);
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL skin shaders after %zu checks: %s\n",checks,e.what());return 1;}
