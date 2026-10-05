// Independent color-domain reference for the fixed edgeAA shader. This test
// does not execute guest shader instructions or use a runtime translator.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include "VSEdgeAA.h"
#include "PSEdgeAA.h"
#include "PSEdgeAASourceProbe.h"
namespace {
using Microsoft::WRL::ComPtr;
using Color=std::array<float,4>;
using DColor=std::array<double,4>;
constexpr unsigned side=32,paletteSide=64;
size_t checks{},draws{},sourceProbes{};
std::array<size_t,4> branches{};
void need(bool v,const char* text){++checks;if(!v)throw std::runtime_error(text);}
void hr(HRESULT r,const char* text){if(FAILED(r))throw std::runtime_error(text);}
struct Vertex {float x,y,u,v;};
struct Constants {std::array<Color,8> c20_27;std::array<Color,3> c48_50;};
static_assert(sizeof(Constants)==176);
struct Inputs {std::array<std::vector<uint32_t>,5> image;};
uint32_t pack(uint32_t r,uint32_t g,uint32_t b,uint32_t a){return r|(g<<10)|(b<<20)|(a<<30);}
DColor unpack(uint32_t p){return {double(float(p&1023)/1023),double(float((p>>10)&1023)/1023),
    double(float((p>>20)&1023)/1023),double(float(p>>30)/3)};}
int coordinate(double value,unsigned extent,bool wrap){int n=int(std::floor(value*extent));return wrap?(n%int(extent)+int(extent))%int(extent):std::clamp(n,0,int(extent)-1);}
DColor sample(const Inputs& inputs,unsigned stage,double u,double v,bool wrap){
    const unsigned extent=stage==2?paletteSide:side;
    const auto p=inputs.image[stage][size_t(coordinate(v,extent,wrap))*extent+size_t(coordinate(u,extent,wrap))];
    if(stage==1)return {std::bit_cast<float>(p),0,0,0};
    if(stage==2)return {double(float(p&255)/255),double(float((p>>8)&255)/255),double(float((p>>16)&255)/255),double(float(p>>24)/255)};
    return unpack(p);
}
double literal(uint32_t bits){return std::bit_cast<float>(bits);}
double clamp(double value){return std::clamp(value,0.0,1.0);}
// High-level formula independent of the shader's temporary-register ordering,
// scalar co-issues, predicate counters and loop accumulators.
DColor expected(const Inputs& inputs,unsigned x,unsigned y,const Constants& c,bool wrap){
    const double u=(double(x)+0.5)/side,v=(double(y)+0.5)/side;
    const auto base=sample(inputs,3,u,v,wrap),edge=sample(inputs,0,u,v,wrap),line=sample(inputs,4,u,v,wrap);
    const bool highAlpha=base[3]>=literal(0x3F19999A);
    const bool highBlue=base[2]>=literal(0x3EEB851F);
    double blue=base[2]-(highBlue?0.5:0),green=base[1],red=base[0];
    const bool band=blue>=literal(0x3E570A3D);blue-=band?0.25:0;
    bool rimBand=band;double rimLight=0;
    if(highAlpha){
        const double code=green*32,fract=code-std::floor(code);
        red=fract>=literal(0x3F733333)?1:fract;
        green=(code-red)/32+1.0/64;
        if(green>=literal(0x3F733333))green=1;
    } else {
        rimBand=blue>=literal(0x3DAE147B);blue-=rimBand?0.125:0;
        if(blue>=literal(0x3CB851EC)){rimLight=c.c20_27[6][0];blue-=0.0625;}
    }
    const double gCode=green*32;
    const double pu=(std::floor((gCode-std::floor(gCode))*32)+0.5)/64;
    const double pv=(std::floor(gCode)+0.5)/64;
    const auto palette=sample(inputs,2,pu,pv,wrap);
    DColor color=highAlpha?DColor{red,green,blue*5,1}:DColor{palette[0],palette[1],palette[2],1};
    const double centerDepth=sample(inputs,1,u,v,wrap)[0];
    const double cast=c.c20_27[7][0];
    const double castControl=highAlpha?(base[3]>=literal(0x3F666666)?cast:1):cast;
    const double shadow=1+clamp(c.c20_27[1][0]*centerDepth)*(base[3]>=literal(0x3D4CCCCD)?1:0)*(castControl-1);
    const double rim=1+(c.c20_27[5][0]-1)*(rimBand?1:0);
    if(edge[0]<=literal(0x3D4CCCCD)){++branches[0];}
    else {
        const double du=c.c48_50[1][0]/double(c.c20_27[3][0]),dv=c.c48_50[1][0]/double(c.c20_27[4][0]);
        double largest=centerDepth;
        for(const auto& offset:std::array<std::array<double,2>,4>{{{0,-dv},{0,dv},{-du,0},{du,0}}})
            largest=std::max(largest,sample(inputs,1,u+offset[0],v+offset[1],wrap)[0]);
        const double fade=clamp(largest*c.c20_27[0][0]);
        if(fade<=c.c20_27[2][0]){
            ++branches[1];for(unsigned lane=0;lane<4;++lane)color[lane]*=1-0.5*c.c48_50[2][0]*edge[lane]*fade;
        } else if(line[0]<1){
            ++branches[2];DColor sums{};const double n=c.c48_50[0][0];
            for(unsigned i=0;i<10;++i){
                const double offset=double(i)-(n-1)*0.5;
                const auto h=sample(inputs,4,u+du*offset,v,wrap),vert=sample(inputs,4,u,v+dv*offset,wrap);
                for(unsigned lane=0;lane<4;++lane)sums[lane]+=h[lane]+vert[lane];
            }
            for(unsigned lane=0;lane<4;++lane)color[lane]*=1-0.5*c.c48_50[2][0]*(sums[lane]/n)*fade;
        } else {
            ++branches[3];for(unsigned lane=0;lane<3;++lane)color[lane]+=fade*(1-line[lane]-color[lane]);
            color[3]=1-fade*line[3];
        }
    }
    for(auto& lane:color)lane=lane*rim*shadow+rimLight;
    return color;
}
class Fixture {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps,probe;ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vertex,constant;ComPtr<ID3D11Texture2D> output,staging,depth,depthStaging;
    ComPtr<ID3D11RenderTargetView> rtv;ComPtr<ID3D11DepthStencilView> dsv;ComPtr<ID3D11Query> event;
    void finish(){context->End(event.Get());const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(context->GetData(event.Get(),nullptr,0,0)==S_FALSE){need(std::chrono::steady_clock::now()<until,"GPU wait timed out");Sleep(1);}}
public:
    explicit Fixture(bool hardware){
        D3D_FEATURE_LEVEL level{};const D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_11_0};
        hr(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,requested,1,D3D11_SDK_VERSION,&device,&level,&context),"Create device");
        need(level==D3D_FEATURE_LEVEL_11_0,"Wrong feature level");
        hr(device->CreateVertexShader(kVSEdgeAA,sizeof(kVSEdgeAA),nullptr,&vs),"Create VSEdgeAA");hr(device->CreatePixelShader(kPSEdgeAA,sizeof(kPSEdgeAA),nullptr,&ps),"Create PSEdgeAA");
        hr(device->CreatePixelShader(kPSEdgeAASourceProbe,sizeof(kPSEdgeAASourceProbe),nullptr,&probe),"Create source probe");
        const D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}};
        hr(device->CreateInputLayout(elements,2,kVSEdgeAA,sizeof(kVSEdgeAA),&layout),"Create layout");
        const Vertex vertices[]={{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,-1,1,1}};
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(vertices);bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;const D3D11_SUBRESOURCE_DATA initial{vertices,0,0};
        hr(device->CreateBuffer(&bd,&initial,&vertex),"Create vertices");bd.ByteWidth=sizeof(Constants);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        hr(device->CreateBuffer(&bd,nullptr,&constant),"Create constants");
        D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=side;td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        hr(device->CreateTexture2D(&td,nullptr,&output),"Create target");hr(device->CreateRenderTargetView(output.Get(),nullptr,&rtv),"Create RTV");
        td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;hr(device->CreateTexture2D(&td,nullptr,&staging),"Create readback");
        td.Format=DXGI_FORMAT_D32_FLOAT;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;td.Usage=D3D11_USAGE_DEFAULT;td.CPUAccessFlags=0;
        hr(device->CreateTexture2D(&td,nullptr,&depth),"Create depth");hr(device->CreateDepthStencilView(depth.Get(),nullptr,&dsv),"Create DSV");
        td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;hr(device->CreateTexture2D(&td,nullptr,&depthStaging),"Create depth readback");
        const D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};hr(device->CreateQuery(&q,&event),"Create event");
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> raster;
        hr(device->CreateRasterizerState(&rd,&raster),"Create rasterizer");context->RSSetState(raster.Get());
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=TRUE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;dd.DepthFunc=D3D11_COMPARISON_ALWAYS;ComPtr<ID3D11DepthStencilState> z;
        hr(device->CreateDepthStencilState(&dd,&z),"Create depth state");context->OMSetDepthStencilState(z.Get(),0);
        const D3D11_VIEWPORT vp{0,0,float(side),float(side),0,1};context->RSSetViewports(1,&vp);
        context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ID3D11Buffer* vb=vertex.Get();const UINT stride=sizeof(Vertex),offset=0;context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
    }
    void verify(const Inputs& inputs,const Constants& values,bool wrap,bool verifyConversion=false){
        std::array<ComPtr<ID3D11Texture2D>,5> sources;std::array<ComPtr<ID3D11ShaderResourceView>,5> views;
        std::array<ID3D11ShaderResourceView*,5> viewPointers{};
        for(unsigned i=0;i<5;++i){
            D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=i==2?paletteSide:side;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
            desc.Format=i==1?DXGI_FORMAT_R32_FLOAT:(i==2?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_R10G10B10A2_UNORM);
            desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            const D3D11_SUBRESOURCE_DATA initial{inputs.image[i].data(),desc.Width*4,0};
            hr(device->CreateTexture2D(&desc,&initial,&sources[i]),"Create source");hr(device->CreateShaderResourceView(sources[i].Get(),nullptr,&views[i]),"Create SRV");viewPointers[i]=views[i].Get();
        }
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=wrap?D3D11_TEXTURE_ADDRESS_WRAP:D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxAnisotropy=1;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;sd.MaxLOD=13;ComPtr<ID3D11SamplerState> sampler;hr(device->CreateSamplerState(&sd,&sampler),"Create sampler");
        context->UpdateSubresource(constant.Get(),0,nullptr,&values,0,0);ID3D11Buffer* cb=constant.Get();context->PSSetConstantBuffers(0,1,&cb);
        context->PSSetShaderResources(0,5,viewPointers.data());const std::array<ID3D11SamplerState*,5> samplers={sampler.Get(),sampler.Get(),sampler.Get(),sampler.Get(),sampler.Get()};context->PSSetSamplers(0,5,samplers.data());
        const float untouched[]={-1,-2,-3,-4};context->ClearRenderTargetView(rtv.Get(),untouched);context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,0.25f,0);
        ID3D11RenderTargetView* target=rtv.Get();context->OMSetRenderTargets(1,&target,dsv.Get());
        if(verifyConversion){
            context->PSSetShader(probe.Get(),nullptr,0);context->Draw(4,0);++sourceProbes;
            context->CopyResource(staging.Get(),output.Get());finish();D3D11_MAPPED_SUBRESOURCE decodedMap{};
            hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&decodedMap),"Map raw input conversion");
            for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){Color decoded{};
                std::memcpy(decoded.data(),static_cast<uint8_t*>(decodedMap.pData)+decodedMap.RowPitch*y+x*sizeof(Color),sizeof(Color));
                const uint32_t packed=inputs.image[3][y*side+x];
                const std::array<uint32_t,4> codes={packed&1023,(packed>>10)&1023,(packed>>20)&1023,packed>>30};
                for(unsigned lane=0;lane<4;++lane){const double scaled=double(decoded[lane])*(lane==3?3:1023);
                    need(std::isfinite(scaled) && std::abs(scaled-codes[lane])<0.5,"Native UNORM approximation loses its packed code");
                    need(uint32_t(std::floor(scaled+0.5))==codes[lane],"Canonical input recovers the wrong code");}
            }
            context->Unmap(staging.Get(),0);context->PSSetShader(ps.Get(),nullptr,0);
        }
        context->Draw(4,0);++draws;
        context->CopyResource(staging.Get(),output.Get());context->CopyResource(depthStaging.Get(),depth.Get());finish();
        D3D11_MAPPED_SUBRESOURCE mapped{};hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Map color");
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){Color actual{};std::memcpy(actual.data(),static_cast<uint8_t*>(mapped.pData)+mapped.RowPitch*y+x*sizeof(Color),sizeof(Color));
            const auto want=expected(inputs,x,y,values,wrap);
            for(unsigned lane=0;lane<4;++lane){
                if(!std::isfinite(actual[lane]) || std::abs(actual[lane]-want[lane])>0.000003){char why[220];
                    std::snprintf(why,sizeof(why),"Draw%zu pixel%u,%u lane%u: got %.9g expected %.12g",draws,x,y,lane,actual[lane],want[lane]);
                    context->Unmap(staging.Get(),0);context->PSSetShader(probe.Get(),nullptr,0);context->Draw(4,0);
                    context->CopyResource(staging.Get(),output.Get());finish();hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Map probe");
                    Color decoded{};std::memcpy(decoded.data(),static_cast<uint8_t*>(mapped.pData)+mapped.RowPitch*y+x*sizeof(Color),sizeof(Color));
                    const auto original=unpack(inputs.image[3][y*side+x]);
                    std::fprintf(stderr,"BASE hardware %.12g %.12g %.12g %.12g; integer reference %.12g %.12g %.12g %.12g\n",decoded[0],decoded[1],decoded[2],decoded[3],original[0],original[1],original[2],original[3]);
                    context->Unmap(staging.Get(),0);throw std::runtime_error(why);}++checks;}
        }
        context->Unmap(staging.Get(),0);hr(context->Map(depthStaging.Get(),0,D3D11_MAP_READ,0,&mapped),"Map depth");
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){float value{};std::memcpy(&value,static_cast<uint8_t*>(mapped.pData)+mapped.RowPitch*y+x*4,4);need(value==1,"VS did not export literal Z/W one");}
        context->Unmap(depthStaging.Get(),0);
    }
};
void run(bool hardware){
    Fixture fixture(hardware);Constants c{{Color{140,0,0,1},{160,0,0,1},{0.4f,0,0,1},{side,0,0,1},
        {side,0,0,1},{0.8f,0,0,1},{0.1f,0,0,1},{0.55f,0,0,1}},std::array<Color,3>{Color{10,0,0,1},{1.5f,0,0,1},{1.0f,0,0,1}}};
    Inputs inputs;for(unsigned stage=0;stage<5;++stage)inputs.image[stage].resize(stage==2?paletteSide*paletteSide:side*side);
    for(unsigned y=0;y<paletteSide;++y)for(unsigned x=0;x<paletteSide;++x)
        inputs.image[2][y*paletteSide+x]=(x*3)|((y*3)<<8)|(((x*7+y*13)%256)<<16)|(((x+y)%256)<<24);
    // Every packed channel code, each alpha, across the palette/direct modes.
    for(unsigned alpha=0;alpha<4;++alpha){
        for(unsigned i=0;i<side*side;++i){
            inputs.image[0][i]=pack(0,0,0,3);inputs.image[1][i]=std::bit_cast<uint32_t>(0.0f);
            inputs.image[3][i]=pack((i*37)%1024,i,(i*73)%1024,alpha);inputs.image[4][i]=pack(0,0,0,3);
        }
        fixture.verify(inputs,c,true,true);
    }
    // Base packed categories and all alpha encodings, with asymmetric palette,
    // explicit/no/filter/solid outline branches and distinct channel values.
    for(unsigned alpha=0;alpha<4;++alpha)for(unsigned mode=0;mode<4;++mode){
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){const size_t i=y*side+x;
            constexpr std::array<unsigned,12> blues={0,22,24,85,87,213,215,469,471,725,727,1023};
            inputs.image[3][i]=pack((x*31+17)%1024,(y*33+x*7)%1024,blues[(x+y)%blues.size()],alpha);
            inputs.image[0][i]=pack(mode?513:0,289,873,(x+y)%4);
            inputs.image[1][i]=std::bit_cast<uint32_t>(mode<2?0.0005f:0.0125f);
            inputs.image[4][i]=pack(mode==3?1023:((x*29+y*13)%900),x*31,y*31,(x/4+y/4)%4);
        }
        for(bool wrap:{false,true})fixture.verify(inputs,c,wrap);
    }
    // Mixed predicates within each pixel quad, neighbor-only depth transitions,
    // border wrapping, different blur widths and independent color parameters.
    for(unsigned pattern=0;pattern<4;++pattern){
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){const size_t i=y*side+x;
            inputs.image[3][i]=pack((x*29+y*7)%1024,(x*3+y*31)%1024,(x*73+y*11)%1024,(x/2+y/3)%4);
            inputs.image[0][i]=pack((x+y)%3?723:0,(x*47+y*13)%1024,(x*11+y*57)%1024,(x+y)%4);
            inputs.image[1][i]=std::bit_cast<uint32_t>(pattern==0?(x==15?0.01f:0.0005f):(x+y)%4?0.0125f:0.0005f);
            inputs.image[4][i]=pack((x+y)%4?uint32_t((x*31+y*7)%900):1023,(x*5+y*31)%1024,(x*23+y*17)%1024,(x+y)%4);
        }
        c.c20_27[5][0]=0.25f+0.2f*pattern;c.c20_27[6][0]=0.025f*pattern;c.c20_27[7][0]=0.25f+0.15f*pattern;
        c.c48_50[2][0]=0.25f+0.4f*pattern;
        for(float width:{0.0f,1.0f,1.5f,2.25f})for(bool wrap:{false,true}){c.c48_50[1][0]=width;fixture.verify(inputs,c,wrap);}
    }
    for(auto count:branches)need(count>100,"A complete outline branch was not exercised");
    std::printf("PASS edgeAA %s: %zu draws, %zu input probes, %zu float/depth checks, branches %zu/%zu/%zu/%zu; runtime admission remains guarded\n",
        hardware?"hardware":"WARP",draws,sourceProbes,checks,branches[0],branches[1],branches[2],branches[3]);
}
}
int main(int argc,char** argv){try{const bool hardware=argc==2&&!std::strcmp(argv[1],"--hardware");if(argc!=1&&!hardware)throw std::runtime_error("Unexpected arguments");run(hardware);return 0;}
catch(const std::exception& e){std::fprintf(stderr,"FAIL edgeAA after%zu checks: %s\n",checks,e.what());return 1;}}
