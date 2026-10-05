// Standalone real D3D11 shader qualification. Headers are produced OFFLINE by
// FXC from renderer/fourtapblend.hlsl; no runtime compiler or engine hooks.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
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
#include <string>
#include <vector>
#include "VSFourTap.h"
#include "PSFourTap.h"

namespace {
using Microsoft::WRL::ComPtr;
using Color=std::array<float,4>;
size_t checks=0,components=0,draws=0;
void need(bool value,const char* why) {++checks;if(!value) throw std::runtime_error(why);}
void hr(HRESULT result,const char* what) {
    if(FAILED(result)) {char text[180];std::snprintf(text,sizeof(text),"%s: HRESULT %08X",what,unsigned(result));throw std::runtime_error(text);}
}
void close(float value,float expected,const char* label) {
    ++components;
    if(!std::isfinite(value) || std::abs(value-expected)>0.000004f) {
        char text[180];std::snprintf(text,sizeof(text),"%s: got %.9g expected %.9g",label,value,expected);throw std::runtime_error(text);
    }
}
struct UV {float u,v;};
struct Transform {float ux,uy,uc,vx,vy,vc;UV at(float u,float v) const {return {ux*u+uy*v+uc,vx*u+vy*v+vc};}};
struct Vertex {Color position;std::array<UV,4> uv;};
static_assert(sizeof(Vertex)==48);
struct Constants {std::array<Color,4> weights;};
static_assert(sizeof(Constants)==64);
struct Texture {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    std::array<std::vector<Color>,4> mip;
};
struct Readback {std::vector<Color> color;std::vector<float> depth;};
constexpr Color untouched={-13,-11,-7,-5};
constexpr std::array<Transform,4> ordinary={Transform{1,0,0,0,1,0},{-1,0,1,0,1,0},
                                         {0,1,0,1,0,0},{0,-1,1,1,0,0}};

class Fixture {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vertices,constants;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depthState;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11Texture2D> target,depth,staging,depthStaging;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11Query> finished;
    void finish() {
        context->End(finished.Get());context->Flush();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        for(;;) {
            BOOL complete=FALSE;const HRESULT status=context->GetData(finished.Get(),&complete,sizeof(complete),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            hr(status,"GPU completion query");hr(device->GetDeviceRemovedReason(),"Device removed");
            if(status==S_OK && complete) return;
            need(std::chrono::steady_clock::now()<deadline,"GPU completion exceeded five seconds");Sleep(1);
        }
    }
public:
    explicit Fixture(bool hardware) {
        D3D_FEATURE_LEVEL level{};const D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_11_0};
        hr(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,
            requested,1,D3D11_SDK_VERSION,&device,&level,&context),"Create actual D3D11 device");
        need(level==D3D_FEATURE_LEVEL_11_0,"Unexpected feature level");
        ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC description{};
        hr(device.As(&dxgi),"Query DXGI device");hr(dxgi->GetAdapter(&adapter),"Get actual adapter");
        hr(adapter->GetDesc(&description),"Get adapter description");
        std::printf("Four-tap device: %s, %ls, feature 11.0\n",hardware?"hardware":"WARP",description.Description);
        hr(device->CreateVertexShader(kVSFourTap,sizeof(kVSFourTap),nullptr,&vs),"Create qualified VS");
        hr(device->CreatePixelShader(kPSFourTap,sizeof(kPSFourTap),nullptr,&ps),"Create qualified PS");
        const D3D11_INPUT_ELEMENT_DESC elements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",2,DXGI_FORMAT_R32G32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",3,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0}};
        hr(device->CreateInputLayout(elements,5,kVSFourTap,sizeof(kVSFourTap),&layout),"Create test-owned decoded input layout");
        D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof(Vertex)*4;buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        hr(device->CreateBuffer(&buffer,nullptr,&vertices),"Create vertex buffer");
        buffer.ByteWidth=sizeof(Constants);buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        hr(device->CreateBuffer(&buffer,nullptr,&constants),"Create exact four-register constant buffer");
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        hr(device->CreateRasterizerState(&rd,&raster),"Create explicit fixture rasterizer");
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=TRUE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;dd.DepthFunc=D3D11_COMPARISON_LESS;
        hr(device->CreateDepthStencilState(&dd,&depthState),"Create depth state");
        D3D11_BLEND_DESC bd{};bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        hr(device->CreateBlendState(&bd,&blend),"Create replacement blend state");
        D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=8;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;
        td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        hr(device->CreateTexture2D(&td,nullptr,&target),"Create float target");
        hr(device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"Create RTV");
        td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        hr(device->CreateTexture2D(&td,nullptr,&staging),"Create color readback");
        td.Format=DXGI_FORMAT_D32_FLOAT;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;td.Usage=D3D11_USAGE_DEFAULT;td.CPUAccessFlags=0;
        hr(device->CreateTexture2D(&td,nullptr,&depth),"Create real depth target");
        hr(device->CreateDepthStencilView(depth.Get(),nullptr,&dsv),"Create DSV");
        td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        hr(device->CreateTexture2D(&td,nullptr,&depthStaging),"Create depth readback");
        const D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};hr(device->CreateQuery(&query,&finished),"Create GPU completion event");
    }
    Texture texture(bool constantMips=false,bool roundingProbe=false) {
        Texture result;
        std::array<D3D11_SUBRESOURCE_DATA,4> initial{};
        for(unsigned level=0;level<4;++level) {
            const unsigned size=8u>>level;auto& values=result.mip[level];values.resize(size*size);
            for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) {
                values[y*size+x]=constantMips?Color{float(level+1)/4,float(4-level)/8,float(level)-0.5f,float(level+2)/16}:
                    Color{float(x+1)/16+float(y)/128+float(level)/2,float(y+2)/32-float(x)/64,
                          float((x*3+y*5)%17)/16-0.5f,float((x+y*2)%13)/16};
            }
            if(roundingProbe) {
                std::fill(values.begin(),values.end(),Color{});
                if(level==0) {values[2].fill(1+std::ldexp(1.0f,-23));values[3].fill(-1);}
            }
            initial[level]={values.data(),size*UINT(sizeof(Color)),0};
        }
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=8;desc.MipLevels=4;desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        hr(device->CreateTexture2D(&desc,initial.data(),&result.texture),"Create asymmetric immutable mip texture");
        hr(device->CreateShaderResourceView(result.texture.Get(),nullptr,&result.view),"Create full mip SRV");return result;
    }
    Readback draw(const Texture& texture,const std::array<float,4>& weights,const std::array<Transform,4>& transforms,
                  bool linear=false,bool wrap=true,int forcedMip=-1,bool halfExtent=false) {
        std::array<Vertex,4> input{};
        for(unsigned i=0;i<4;++i) {
            const float u=float(i&1),v=float(i>>1);
            input[i].position={u*2-1,1-v*2,halfExtent?0.5f:0.25f,halfExtent?2.0f:1.0f};
            for(unsigned tap=0;tap<4;++tap) input[i].uv[tap]=transforms[tap].at(u,v);
        }
        Constants values{};
        for(unsigned i=0;i<4;++i) values.weights[i]={weights[i],float(i+3)*19,-float(i+1)*13,0.375f+float(i)};
        context->UpdateSubresource(vertices.Get(),0,nullptr,input.data(),0,0);
        context->UpdateSubresource(constants.Get(),0,nullptr,&values,0,0);
        D3D11_SAMPLER_DESC sd{};sd.Filter=linear?D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT:D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU=sd.AddressV=sd.AddressW=wrap?D3D11_TEXTURE_ADDRESS_WRAP:D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxAnisotropy=1;sd.ComparisonFunc=D3D11_COMPARISON_ALWAYS;
        sd.MinLOD=forcedMip<0?0:float(forcedMip);sd.MaxLOD=forcedMip<0?D3D11_FLOAT32_MAX:float(forcedMip);
        ComPtr<ID3D11SamplerState> sampler;hr(device->CreateSamplerState(&sd,&sampler),"Create explicit external sampler");
        context->ClearRenderTargetView(rtv.Get(),untouched.data());context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,1,0);
        ID3D11RenderTargetView* color=rtv.Get();context->OMSetRenderTargets(1,&color,dsv.Get());
        context->OMSetDepthStencilState(depthState.Get(),0);context->OMSetBlendState(blend.Get(),nullptr,0xFFFFFFFF);
        const D3D11_VIEWPORT viewport{0,0,8,8,0,1};context->RSSetViewports(1,&viewport);context->RSSetState(raster.Get());
        context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ID3D11Buffer* vb=vertices.Get();const UINT stride=sizeof(Vertex),offset=0;context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
        ID3D11Buffer* cb=constants.Get();context->PSSetConstantBuffers(0,1,&cb);
        ID3D11ShaderResourceView* srv=texture.view.Get();context->PSSetShaderResources(0,1,&srv);
        ID3D11SamplerState* state=sampler.Get();context->PSSetSamplers(0,1,&state);
        context->Draw(4,0);++draws;
        context->CopyResource(staging.Get(),target.Get());context->CopyResource(depthStaging.Get(),depth.Get());finish();
        Readback result;result.color.resize(64);result.depth.resize(64);D3D11_MAPPED_SUBRESOURCE mapped{};
        hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Read actual RGBA32F pixels");
        for(unsigned y=0;y<8;++y) std::memcpy(result.color.data()+8*y,static_cast<uint8_t*>(mapped.pData)+y*mapped.RowPitch,8*sizeof(Color));
        context->Unmap(staging.Get(),0);
        hr(context->Map(depthStaging.Get(),0,D3D11_MAP_READ,0,&mapped),"Read actual D32 depth");
        for(unsigned y=0;y<8;++y) std::memcpy(result.depth.data()+8*y,static_cast<uint8_t*>(mapped.pData)+y*mapped.RowPitch,8*sizeof(float));
        context->Unmap(depthStaging.Get(),0);return result;
    }
};

Color sample(const Texture& texture,UV uv,unsigned level,bool linear,bool wrap) {
    const int size=8>>level;
    auto texel=[&](int x,int y) {
        auto address=[&](int a) {return wrap?(a%size+size)%size:std::clamp(a,0,size-1);};
        return texture.mip[level][size_t(address(y)*size+address(x))];
    };
    if(!linear) return texel(int(std::floor(uv.u*float(size))),int(std::floor(uv.v*float(size))));
    const float x=uv.u*float(size)-0.5f,y=uv.v*float(size)-0.5f;
    const int ix=int(std::floor(x)),iy=int(std::floor(y));const float fx=x-float(ix),fy=y-float(iy);
    Color result{};const auto a=texel(ix,iy),b=texel(ix+1,iy),c=texel(ix,iy+1),d=texel(ix+1,iy+1);
    for(unsigned lane=0;lane<4;++lane) result[lane]=(a[lane]*(1-fx)+b[lane]*fx)*(1-fy)+(c[lane]*(1-fx)+d[lane]*fx)*fy;
    return result;
}
void verify(Fixture& fixture,const Texture& texture,const std::array<float,4>& weights,
            const std::array<Transform,4>& transforms,const std::array<unsigned,4>& levels={},
            bool linear=false,bool wrap=true,int forcedMip=-1,bool halfExtent=false) {
    const auto pixels=fixture.draw(texture,weights,transforms,linear,wrap,forcedMip,halfExtent);
    for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
        const bool covered=!halfExtent || (x>=2 && x<6 && y>=2 && y<6);
        Color expected=untouched;
        if(covered) {
            const float u=(float(x)+0.5f-(halfExtent?2.0f:0.0f))/(halfExtent?4.0f:8.0f);
            const float v=(float(y)+0.5f-(halfExtent?2.0f:0.0f))/(halfExtent?4.0f:8.0f);
            std::array<Color,4> taps;
            for(unsigned i=0;i<4;++i) taps[i]=sample(texture,transforms[i].at(u,v),levels[i],linear,wrap);
            for(unsigned c=0;c<4;++c) {
                expected[c]=taps[3][c]*weights[3];
                for(int tap=2;tap>=0;--tap) expected[c]=std::fma(taps[size_t(tap)][c],weights[size_t(tap)],expected[c]);
            }
        }
        for(unsigned c=0;c<4;++c) close(pixels.color[y*8+x][c],expected[c],"Four-tap sample/weight/accumulation result");
        close(pixels.depth[y*8+x],covered?0.25f:1.0f,"Fetched position Z/W and coverage");
    }
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    try {
        need(argc==1 || (argc==2 && std::string(argv[1])=="--hardware"),"Use optional --hardware only");
        Fixture fixture(argc==2);const auto texture=fixture.texture();
        // Basis weights catch every fetch/export permutation, X-only constant
        // access, component order, and alpha preservation independently.
        for(unsigned i=0;i<4;++i) {std::array<float,4> weights{};weights[i]=1;verify(fixture,texture,weights,ordinary);}
        const std::array<float,4> weights={0.125f,0.25f,-0.5f,1.125f};
        verify(fixture,texture,weights,ordinary);
        verify(fixture,texture,{2,3,-1,4},ordinary); // No weight normalization or shader saturation.
        auto shifted=ordinary;
        for(unsigned i=0;i<4;++i) {shifted[i].uc+=(float(i)-1)/4;shifted[i].vc+=(2-float(i))/8;}
        verify(fixture,texture,weights,shifted,{},false,true);
        verify(fixture,texture,weights,shifted,{},false,false);
        auto fractional=ordinary;
        for(auto& tap:fractional) {tap.uc+=1.0f/32;tap.vc-=1.0f/32;}
        verify(fixture,texture,weights,fractional,{},true,true);
        verify(fixture,texture,weights,fractional,{},true,false);
        // Each implicit sample has a different power-of-two derivative/LOD.
        // Mips have distinct constant RGBA, so address/filter rounding cannot
        // hide an incorrectly shared LOD or hardcoded SampleLevel(0).
        const auto mips=fixture.texture(true);
        const std::array<Transform,4> scales={Transform{4,0,0,0,4,0},{2,0,0,0,2,0},
                                            {1,0,0,0,1,0},{8,0,0,0,8,0}};
        verify(fixture,mips,weights,scales,{2,1,0,3});
        verify(fixture,mips,weights,scales,{1,1,1,1},false,true,1);
        // Force W=2 with unscaled X/Y: only the middle 4x4 pixels are covered.
        // Fetched Z=.5 must become depth .25. A screen-style Z=0/W=1 rewrite fails.
        verify(fixture,mips,weights,ordinary,{0,0,0,0},false,true,0,true);
        // Numeric qualification probe: precise native mad can be fused or
        // unfused. Report which occurs, rather than invent console bit identity.
        const auto rounding=fixture.texture(false,true);
        std::array<Transform,4> fixed{};
        for(unsigned i=0;i<4;++i) fixed[i]={0,0,(float(i)+0.5f)/8,0,0,0.5f/8};
        const auto numeric=fixture.draw(rounding,{0,0,1-std::ldexp(1.0f,-23),1},fixed);
        const float fused=-std::ldexp(1.0f,-46),observed=numeric.color[0][0];
        need(observed==fused || observed==0,"Native mad precision probe has an unqualified outcome");
        for(const auto& color:numeric.color) for(float lane:color)
            need(std::bit_cast<uint32_t>(lane)==std::bit_cast<uint32_t>(observed),"Native mad rounding was not consistent across lanes/pixels");
        std::printf("Native precise-mad cancellation probe: %s, bits=%08X; fused reference=%08X\n",
                    observed==fused?"fused":"unfused",std::bit_cast<uint32_t>(observed),std::bit_cast<uint32_t>(fused));
        std::printf("PASS four-tap shaders: %zu draws, %zu RGBA/depth comparisons, %zu checks; asymmetric taps/weights, wrap/clamp, linear/point, independent computed LOD, Z/W. Headless fixture only; no original engine scene or presentation.\n",draws,components,checks);
        return 0;
    }catch(const std::exception& e) {std::fprintf(stderr,"FAIL four-tap shaders: %s\n",e.what());return 1;}
}
