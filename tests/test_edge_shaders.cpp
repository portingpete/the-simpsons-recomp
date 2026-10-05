// Independent packed-color classification fixture for the two original edge
// shaders. Offline FXC only; no engine admission or runtime shader translation.
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
#ifdef SIMPSONS_AA_SHADER_TESTS
#include "VSAA.h"
#include "PSAA.h"
#define kVSEdge kVSAA
#define kPSEdge kPSAA
#else
#include "VSEdge.h"
#include "PSEdge.h"
#endif
namespace {
using Microsoft::WRL::ComPtr;
using Color=std::array<float,4>;
constexpr unsigned side=32;
size_t checks{},draws{};
void need(bool v,const char* text){++checks;if(!v)throw std::runtime_error(text);}
void hr(HRESULT r,const char* text){if(FAILED(r))throw std::runtime_error(text);}
struct Vertex {float x,y,u,v;};
struct Constants {std::array<Color,8> kernel;Color dimensions;};
static_assert(sizeof(Constants)==144);
uint32_t pack(uint32_t r,uint32_t g,uint32_t b,uint32_t a){return r|(g<<10)|(b<<20)|(a<<30);}
Color unpack(uint32_t p){return {float(p&1023)/1023,float((p>>10)&1023)/1023,float((p>>20)&1023)/1023,float(p>>30)/3};}
Color sample(const std::vector<uint32_t>& image,float u,float v,bool wrap){
    const auto coord=[&](float t){int n=int(std::floor(t*side));return wrap?(n%int(side)+int(side))%int(side):std::clamp(n,0,int(side)-1);};
    return unpack(image[size_t(coord(v))*side+size_t(coord(u))]);
}
// Boolean classification derived independently from the scheduled register
// operations, including the blue-category product carried between iterations.
#ifndef SIMPSONS_AA_SHADER_TESTS
float expected(const std::vector<uint32_t>& image,unsigned x,unsigned y,const Constants& c,bool wrap){
    const float u=(float(x)+0.5f)/side,v=(float(y)+0.5f)/side;
    const auto center=sample(image,u,v,wrap);
    const float blue=std::bit_cast<float>(0x3EEB851Fu),alpha=std::bit_cast<float>(0x3F19999Au);
    const float band=std::bit_cast<float>(0x3E570A3Du),epsilon=std::bit_cast<float>(0x38D1B717u);
    const auto alphaClass=[&](const Color& p){return p[3]>=alpha;};
    const auto blueClass=[&](const Color& p){return p[2]>=blue;};
    const auto category=[&](const Color& p){return p[1]+float(!alphaClass(p)&&(p[2]-(blueClass(p)?0.5f:0.0f)>=band));};
    const auto base=category(center);bool blueProduct=!blueClass(center);
    for(unsigned tap=0;tap<4;++tap){
        const auto neighbor=sample(image,u+c.kernel[tap][0]*c.dimensions[2]/c.dimensions[0],
            v+c.kernel[tap][1]*c.dimensions[2]/c.dimensions[1],wrap);
        blueProduct=blueProduct&&!blueClass(neighbor);
        if(std::abs(neighbor[0]-center[0])>=epsilon){
            if(!alphaClass(center)||blueProduct)return 1;
        }else if(!alphaClass(center)&&!alphaClass(neighbor)&&blueProduct&&std::abs(category(neighbor)-base)>=epsilon)return 1;
    }
    return 0;
}
#else
Color expected(const std::vector<uint32_t>& image,unsigned x,unsigned y,const Constants& c,bool wrap){
    const float u=(float(x)+0.5f)/side,v=(float(y)+0.5f)/side;
    const auto center=sample(image,u,v,wrap);std::array<double,3> sum{center[0],center[1],center[2]};
    for(unsigned tap=0;tap<4;++tap){
        const auto neighbor=sample(image,u+c.kernel[tap][0]/c.dimensions[0],v+c.kernel[tap][1]/c.dimensions[1],wrap);
        for(unsigned channel=0;channel<3;++channel)sum[channel]+=neighbor[channel];
    }
    return {float(sum[0]/5),float(sum[1]/5),float(sum[2]/5),1};
}
#endif
class Fixture {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vertex,constant;ComPtr<ID3D11Texture2D> output,staging,depth,depthStaging;
    ComPtr<ID3D11RenderTargetView> rtv;ComPtr<ID3D11DepthStencilView> dsv;ComPtr<ID3D11Query> event;
    void finish(){context->End(event.Get());context->Flush();const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        for(;;){BOOL done{};const auto r=context->GetData(event.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);hr(r,"GPU query failed");
            hr(device->GetDeviceRemovedReason(),"Device removed");if(r==S_OK&&done)return;need(std::chrono::steady_clock::now()<until,"GPU wait timed out");Sleep(1);}}
public:
    explicit Fixture(bool hardware){
        D3D_FEATURE_LEVEL level{};const D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_11_0};
        hr(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,requested,1,D3D11_SDK_VERSION,&device,&level,&context),"Create device");
        need(level==D3D_FEATURE_LEVEL_11_0,"Wrong feature level");
        hr(device->CreateVertexShader(kVSEdge,sizeof(kVSEdge),nullptr,&vs),"Create VSEdge");hr(device->CreatePixelShader(kPSEdge,sizeof(kPSEdge),nullptr,&ps),"Create PSEdge");
        const D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}};
        hr(device->CreateInputLayout(elements,2,kVSEdge,sizeof(kVSEdge),&layout),"Create layout");
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
    void verify(const std::vector<uint32_t>& image,const Constants& values,bool wrap){
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=side;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R10G10B10A2_UNORM;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA initial{image.data(),side*4,0};ComPtr<ID3D11Texture2D> source;ComPtr<ID3D11ShaderResourceView> srv;
        hr(device->CreateTexture2D(&desc,&initial,&source),"Create packed source");hr(device->CreateShaderResourceView(source.Get(),nullptr,&srv),"Create SRV");
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=sd.AddressW=wrap?D3D11_TEXTURE_ADDRESS_WRAP:D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxAnisotropy=1;sd.ComparisonFunc=D3D11_COMPARISON_ALWAYS;ComPtr<ID3D11SamplerState> sampler;hr(device->CreateSamplerState(&sd,&sampler),"Create sampler");
        context->UpdateSubresource(constant.Get(),0,nullptr,&values,0,0);ID3D11Buffer* cb=constant.Get();context->PSSetConstantBuffers(0,1,&cb);
        ID3D11ShaderResourceView* input=srv.Get();context->PSSetShaderResources(0,1,&input);ID3D11SamplerState* samp=sampler.Get();context->PSSetSamplers(0,1,&samp);
        const float untouched[]={-1,-2,-3,-4};context->ClearRenderTargetView(rtv.Get(),untouched);context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,0.25f,0);
        ID3D11RenderTargetView* target=rtv.Get();context->OMSetRenderTargets(1,&target,dsv.Get());context->Draw(4,0);++draws;
        context->CopyResource(staging.Get(),output.Get());context->CopyResource(depthStaging.Get(),depth.Get());finish();
        D3D11_MAPPED_SUBRESOURCE mapped{};hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Map color");
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){Color actual{};std::memcpy(actual.data(),static_cast<uint8_t*>(mapped.pData)+mapped.RowPitch*y+x*sizeof(Color),sizeof(Color));
            const auto want=expected(image,x,y,values,wrap);
#ifdef SIMPSONS_AA_SHADER_TESTS
            for(unsigned lane=0;lane<4;++lane)need(std::isfinite(actual[lane]) && std::abs(actual[lane]-want[lane])<0.00000024f,
                "AA float output differs from independent five-sample mean");
            need(actual[3]==1,"AA output alpha was sampled");}
#else
            if(actual!=Color{want,want,want,1}){char why[180];std::snprintf(why,sizeof(why),"Draw%zu pixel%u,%u: got %.9g,%.9g,%.9g,%.9g expected %.9g",draws,x,y,actual[0],actual[1],actual[2],actual[3],want);throw std::runtime_error(why);}++checks;}
#endif
        context->Unmap(staging.Get(),0);hr(context->Map(depthStaging.Get(),0,D3D11_MAP_READ,0,&mapped),"Map depth");
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){float value{};std::memcpy(&value,static_cast<uint8_t*>(mapped.pData)+mapped.RowPitch*y+x*4,4);need(value==1,"VS did not export literal Z/W one");}
        context->Unmap(depthStaging.Get(),0);
    }
};
void run(bool hardware){
    Fixture fixture(hardware);Constants c{{Color{0,1,7,9},{1,0,7,9},{0,-1,7,9},{-1,0,7,9},
        {11,13,0,0},{17,19,0,0},{23,29,0,0},{31,37,0,0}},Color{side,side,1,913}};
    std::vector<uint32_t> pixels(side*side);
    for(uint32_t a=0;a<4;++a)for(uint32_t b:{0u,214u,215u,470u,471u,726u,1023u}){
        std::fill(pixels.begin(),pixels.end(),pack(128,512,b,a));fixture.verify(pixels,c,true);
    }
    for(unsigned pattern=0;pattern<5;++pattern){
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){
            const auto r=pattern==0?128u:((x/4+y/4)%2?128u:130u);
            const auto g=pattern<2?512u:((x/8+y/8)%2?256u:512u);
            const auto b=pattern<3?0u:((x+y)%3?214u:726u);
            const auto a=pattern<4?0u:(x/4+y/4)%4;
            pixels[y*side+x]=pack(r,g,b,a);
        }
        for(bool wrap:{false,true})for(float width:{0.0f,0.5f,1.0f,1.5f,2.0f,2.5f}){c.dimensions[2]=width;fixture.verify(pixels,c,wrap);}
        c.dimensions={16,32,1,913};fixture.verify(pixels,c,true);c.dimensions={32,16,1,913};fixture.verify(pixels,c,false);c.dimensions={32,32,1,913};
        std::reverse(c.kernel.begin(),c.kernel.begin()+4);fixture.verify(pixels,c,true);std::reverse(c.kernel.begin(),c.kernel.begin()+4);
    }
#ifdef SIMPSONS_AA_SHADER_TESTS
    // Independent asymmetric color ramps, impulses, binary edges and alpha
    // variants expose channel swaps, wrong tap count and sampled-alpha bugs.
    c.dimensions={32,32,0.15f,913};
    for(unsigned pattern=0;pattern<4;++pattern){
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x)
            pixels[y*side+x]=pattern==0?pack(x*33,y*33,(x+y)*13,(x+y)%4):
                (pattern==1?pack((x==15&&y==16)?1023:0,(x+y)%2?777:2,431,(x+y)%4):
                 (pattern==2?pack(x<16?0:1023,y<16?1023:0,(x+y)%3?1023:0,0):pack(1023,13,798,(x/4)%4)));
        for(bool wrap:{false,true})fixture.verify(pixels,c,wrap);
    }
    std::printf("PASS AA %s: %zu draws, %zu float/color/depth checks; engine activation remains guarded\n",hardware?"hardware":"WARP",draws,checks);
#else
    std::printf("PASS edge %s: %zu draws, %zu exact pixel/depth checks\n",hardware?"hardware":"WARP",draws,checks);
#endif
}
}
int main(int argc,char** argv){try{const bool hardware=argc==2&&!std::strcmp(argv[1],"--hardware");if(argc!=1&&!hardware)throw std::runtime_error("Unexpected arguments");run(hardware);return 0;}
catch(const std::exception& e){std::fprintf(stderr,"FAIL edge after%zu checks: %s\n",checks,e.what());return 1;}}
