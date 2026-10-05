// Independent sampling/geometry/blend references for exact original post shaders.
// Native finite-float policy; no console filtering or blending precision claim.
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include "VSPostAutoTextured.h"
#include "PSPostAlphaToRGBA.h"
#include "PSPostTextured.h"
#include "PSPostFiltered.h"
#include "PSPostGlow.h"
#include "PSPostAlphaToRGBADraw.h"
#include "PSPostTexturedDraw.h"
#include "PSPostFilteredDraw.h"
#include "PSPostGlowDraw.h"
#include "GSPostProbe.h"
using Microsoft::WRL::ComPtr;
namespace {
using V=std::array<float,4>;
using Bank=std::array<V,10>;
size_t checks{},draws{};
void need(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void hr(HRESULT value,const char* why){if(FAILED(value)){char msg[180];sprintf_s(msg,"%s %08lX",why,ULONG(value));throw std::runtime_error(msg);}}
void close(float got,float want){++checks;if(!std::isfinite(got)||std::abs(got-want)>0.000006f){char msg[180];sprintf_s(msg,"GPU %.9g differs from independent reference %.9g",got,want);throw std::runtime_error(msg);}}
struct Vertex{float x,y;};
struct Observed{V position;std::array<float,2> uv;};
static_assert(sizeof(Vertex)==8&&sizeof(Observed)==24&&sizeof(Bank)==160);
struct Texture {
    ComPtr<ID3D11ShaderResourceView> view;
    std::array<std::vector<V>,3> mips;
};
// CPU oracle uses texture-coordinate geometry, not the shader's temporary registers.
V sample(const Texture& t,int mip,double u,double v,bool linear,int address) {
    const int w=32>>mip,h=16>>mip;
    const auto texel=[&](int x,int y){
        if(address==1){x=(x%w+w)%w;y=(y%h+h)%h;}else if(address==2){x=(x%(2*w)+2*w)%(2*w);y=(y%(2*h)+2*h)%(2*h);if(x>=w)x=2*w-1-x;if(y>=h)y=2*h-1-y;}else{x=std::clamp(x,0,w-1);y=std::clamp(y,0,h-1);}
        return t.mips[mip][size_t(y*w+x)];
    };
    if(!linear)return texel(int(std::floor(u*w)),int(std::floor(v*h)));
    const double px=u*w-0.5,py=v*h-0.5;const int x=int(std::floor(px)),y=int(std::floor(py));
    const double fx=px-x,fy=py-y;const auto a=texel(x,y),b=texel(x+1,y),c=texel(x,y+1),d=texel(x+1,y+1);V result{};
    for(int k=0;k<4;++k)result[k]=float((a[k]*(1-fx)+b[k]*fx)*(1-fy)+(c[k]*(1-fx)+d[k]*fx)*fy);
    return result;
}
V reference(const Texture& t,int shader,const Bank& bank,int mip,double u,double v,bool linear,int address) {
    const auto fetch=[&](double x,double y){return sample(t,mip,x,y,linear,address);};
    V out{};
    if(shader==0){out.fill(fetch(u,v)[3]);return out;}
    if(shader==1){out=fetch(u,v);for(int k=0;k<4;++k)out[k]*=bank[0][k];return out;}
    if(shader==2){
        const double x=bank[9][0]/2.0,y=bank[9][1]/2.0;
        const auto a=fetch(u+x,v-y),b=fetch(u-x,v-y),c=fetch(u+x,v+y),d=fetch(u-x,v+y);
        for(int k=0;k<4;++k)out[k]=((c[k]+d[k])+b[k])+a[k];
        for(float& f:out)f*=0.25f;
        return out;
    }
    const float left=fetch(u-bank[1][0],v)[0],right=fetch(u+bank[1][0],v)[0];
    const float down=fetch(u,v+bank[1][1])[0],up=fetch(u,v-bank[1][1])[0],center=fetch(u,v)[0];
    const float sum=(((left+right)+down)+up)+center;
    const float intensity=std::min(sum*0.4f,1.0f);
    for(int k=0;k<4;++k)out[k]=intensity*bank[0][k];return out;
}
uint32_t packed(V source,uint32_t destination,uint32_t word,bool enable) {
    const V d={float(destination&1023)*(1.0f/1023.0f),float(destination>>10&1023)*(1.0f/1023.0f),float(destination>>20&1023)*(1.0f/1023.0f),float(destination>>30)*(1.0f/3.0f)};
    if(enable){
        const float sf=(word&31)==11?1-d[3]:1;
        const float df=((word>>8)&31)==7?1-source[3]:((word>>8)&31)==1?1.0f:0.0f;
        for(int k=0;k<3;++k){const float a=source[k]*sf,b=d[k]*df;source[k]=((word>>5)&7)==4?b-a:a+b;}
    }
    uint32_t result=0;
    for(int k=0;k<4;++k){const float value=std::clamp(source[k],0.0f,1.0f)*(k==3?3.0f:1023.0f);const float low=std::floor(value),fraction=value-low;
        const uint32_t code=uint32_t(low)+uint32_t(fraction>0.5f||(fraction==0.5f&&(uint32_t(low)&1)));result|=code<<(k==3?30:10*k);}
    return result;
}
struct Fixture {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11InputLayout> layout;
    std::array<ComPtr<ID3D11PixelShader>,4> ps,pack;
    ComPtr<ID3D11Buffer> cb,drawCB,vb;
    ComPtr<ID3D11RasterizerState> raster;
    Fixture(bool hardware){
        D3D_FEATURE_LEVEL level{};hr(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"device");
        hr(device->CreateVertexShader(kVSPostAutoTextured,sizeof(kVSPostAutoTextured),nullptr,&vs),"original auto VS");
        const D3D11_INPUT_ELEMENT_DESC element={"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        hr(device->CreateInputLayout(&element,1,kVSPostAutoTextured,sizeof(kVSPostAutoTextured),&layout),"original float2 declaration");
        const void* bytecode[]={kPSPostAlphaToRGBA,kPSPostTextured,kPSPostFiltered,kPSPostGlow,kPSPostAlphaToRGBADraw,kPSPostTexturedDraw,kPSPostFilteredDraw,kPSPostGlowDraw};
        const size_t sizes[]={sizeof(kPSPostAlphaToRGBA),sizeof(kPSPostTextured),sizeof(kPSPostFiltered),sizeof(kPSPostGlow),sizeof(kPSPostAlphaToRGBADraw),sizeof(kPSPostTexturedDraw),sizeof(kPSPostFilteredDraw),sizeof(kPSPostGlowDraw)};
        for(int i=0;i<8;++i)hr(device->CreatePixelShader(bytecode[i],sizes[i],nullptr,i<4?&ps[i]:&pack[i-4]),"original PS or packed adapter");
        D3D11_BUFFER_DESC d{};d.ByteWidth=sizeof(Bank);d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;hr(device->CreateBuffer(&d,nullptr,&cb),"PS original c0..9");
        d.ByteWidth=16;hr(device->CreateBuffer(&d,nullptr,&drawCB),"draw constants");
        // RECTLIST expansion preserves all original corners; strip ordering differs.
        const Vertex vertices[]={{-1,1},{1,1},{-1,-1},{1,-1}};d.ByteWidth=sizeof(vertices);d.BindFlags=D3D11_BIND_VERTEX_BUFFER;d.Usage=D3D11_USAGE_IMMUTABLE;
        const D3D11_SUBRESOURCE_DATA data{vertices,0,0};hr(device->CreateBuffer(&d,&data,&vb),"four expanded corners");
        D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;hr(device->CreateRasterizerState(&rs,&raster),"raster");
    }
    Texture texture(int pattern){
        Texture t;std::array<D3D11_SUBRESOURCE_DATA,3> data{};
        for(int m=0;m<3;++m){int w=32>>m,h=16>>m;t.mips[m].resize(size_t(w*h));
            for(int y=0;y<h;++y)for(int x=0;x<w;++x){V c{};
                for(int k=0;k<4;++k){const int n=(x*(k+3)+y*(7-k)+m*11+k*5)%32;
                    c[k]=pattern==0?float(n)/64.0f:pattern==1?float(n-16)/16.0f:float(((x^y^k)&1)?(k+1):0)/4.0f;}
                t.mips[m][size_t(y*w+x)]=c;}
            data[m]={t.mips[m].data(),UINT(w*sizeof(V)),0};}
        D3D11_TEXTURE2D_DESC d{};d.Width=32;d.Height=16;d.MipLevels=3;d.ArraySize=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.Usage=D3D11_USAGE_IMMUTABLE;
        ComPtr<ID3D11Texture2D> resource;hr(device->CreateTexture2D(&d,data.data(),&resource),"texture with three distinct mip levels");hr(device->CreateShaderResourceView(resource.Get(),nullptr,&t.view),"texture view");return t;
    }
    void vertexProbe(){
        const Vertex inputs[]={{-1,1},{1,1},{-1,-1},{1,-1},{0,0},{-2,3},{0.125f,-0.375f}};
        ComPtr<ID3D11Buffer> in,out,staging;D3D11_BUFFER_DESC d{};d.ByteWidth=sizeof(inputs);d.BindFlags=D3D11_BIND_VERTEX_BUFFER;d.Usage=D3D11_USAGE_IMMUTABLE;D3D11_SUBRESOURCE_DATA data{inputs,0,0};hr(device->CreateBuffer(&d,&data,&in),"probe vertices");
        d.ByteWidth=sizeof(Observed)*UINT(std::size(inputs));d.BindFlags=D3D11_BIND_STREAM_OUTPUT;d.Usage=D3D11_USAGE_DEFAULT;hr(device->CreateBuffer(&d,nullptr,&out),"probe output");
        d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.Usage=D3D11_USAGE_STAGING;hr(device->CreateBuffer(&d,nullptr,&staging),"probe staging");
        const D3D11_SO_DECLARATION_ENTRY elements[]={{0,"SV_Position",0,0,4,0},{0,"TEXCOORD",0,0,2,0}};const UINT outStride=sizeof(Observed);
        ComPtr<ID3D11GeometryShader> gs;hr(device->CreateGeometryShaderWithStreamOutput(kGSPostProbe,sizeof(kGSPostProbe),elements,2,&outStride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&gs),"actual VS output probe");
        context->ClearState();context->VSSetShader(vs.Get(),nullptr,0);context->GSSetShader(gs.Get(),nullptr,0);context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        UINT stride=8,offset=0;auto* ib=in.Get();context->IASetVertexBuffers(0,1,&ib,&stride,&offset);auto* ob=out.Get();context->SOSetTargets(1,&ob,&offset);context->Draw(UINT(std::size(inputs)),0);++draws;context->SOSetTargets(0,nullptr,nullptr);
        context->CopyResource(staging.Get(),out.Get());D3D11_MAPPED_SUBRESOURCE map{};hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"VS map");const auto* observed=static_cast<const Observed*>(map.pData);
        for(size_t i=0;i<std::size(inputs);++i){const auto& v=inputs[i];close(observed[i].position[0],v.x);close(observed[i].position[1],v.y);close(observed[i].position[2],0);close(observed[i].position[3],1);close(observed[i].uv[0],float((double(v.x)+1)/2));close(observed[i].uv[1],float((1-double(v.y))/2));}
        context->Unmap(staging.Get(),0);context->ClearState();
    }
    void render(const Texture& texture,int shader,const Bank& bank,int lod,bool linear,int address,bool packedOutput=false,uint32_t word=0x10001,bool enable=false){
        constexpr UINT w=16,h=8;
        context->ClearState();context->RSSetState(raster.Get());context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader((packedOutput?pack:ps)[shader].Get(),nullptr,0);context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        UINT stride=8,offset=0;auto* vertices=vb.Get();context->IASetVertexBuffers(0,1,&vertices,&stride,&offset);
        context->UpdateSubresource(cb.Get(),0,nullptr,bank.data(),0,0);const std::array<uint32_t,4> state={word,uint32_t(enable),0,0};context->UpdateSubresource(drawCB.Get(),0,nullptr,state.data(),0,0);ID3D11Buffer* buffers[]={cb.Get(),drawCB.Get()};context->PSSetConstantBuffers(0,2,buffers);
        D3D11_SAMPLER_DESC sd{};sd.Filter=linear?D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT:D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=sd.AddressW=address==1?D3D11_TEXTURE_ADDRESS_WRAP:address==2?D3D11_TEXTURE_ADDRESS_MIRROR:D3D11_TEXTURE_ADDRESS_CLAMP;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;sd.MaxAnisotropy=1;sd.MinLOD=lod<0?0:float(lod);sd.MaxLOD=lod<0?2:float(lod);
        ComPtr<ID3D11SamplerState> sampler;hr(device->CreateSamplerState(&sd,&sampler),"sampler");auto* sam=sampler.Get();context->PSSetSamplers(0,1,&sam);
        D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;td.Format=packedOutput?DXGI_FORMAT_R10G10B10A2_UINT:DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target,staging,destination;ComPtr<ID3D11RenderTargetView> rtv;ComPtr<ID3D11ShaderResourceView> destinationView;
        hr(device->CreateTexture2D(&td,nullptr,&target),"output allocation");hr(device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"output view");td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;hr(device->CreateTexture2D(&td,nullptr,&staging),"output staging");
        std::array<uint32_t,w*h> codes{};for(UINT i=0;i<codes.size();++i)codes[i]=((i*11+79)%1024)|(((i*23+213)%1024)<<10)|(((i*31+557)%1024)<<20)|((i%4)<<30);
        if(packedOutput){td.BindFlags=D3D11_BIND_SHADER_RESOURCE;td.CPUAccessFlags=0;td.Usage=D3D11_USAGE_IMMUTABLE;const D3D11_SUBRESOURCE_DATA data{codes.data(),w*4,0};hr(device->CreateTexture2D(&td,&data,&destination),"destination codes");hr(device->CreateShaderResourceView(destination.Get(),nullptr,&destinationView),"destination uint view");}
        ID3D11ShaderResourceView* views[]={texture.view.Get(),destinationView.Get()};context->PSSetShaderResources(0,2,views);const D3D11_VIEWPORT vp{0,0,float(w),float(h),0,1};context->RSSetViewports(1,&vp);auto* view=rtv.Get();context->OMSetRenderTargets(1,&view,nullptr);const float clear[]={0,0,0,0};context->ClearRenderTargetView(view,clear);context->Draw(4,0);++draws;context->OMSetRenderTargets(0,nullptr,nullptr);context->CopyResource(staging.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE map{};hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"pixel readback");
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){const auto source=reference(texture,shader,bank,lod<0?1:lod,(double(x)+.5)/w,(double(y)+.5)/h,linear,address);const auto* row=static_cast<const uint8_t*>(map.pData)+y*map.RowPitch;
            if(packedOutput){uint32_t got;std::memcpy(&got,row+x*4,4);const auto want=packed(source,codes[y*w+x],word,enable);if(got!=want){char msg[200];sprintf_s(msg,"Packed stage%d word%X enabled%d (%u,%u): got%08X want%08X",shader,word,int(enable),x,y,got,want);need(false,msg);}++checks;}
            else{V got;std::memcpy(got.data(),row+x*sizeof(V),sizeof(V));for(int k=0;k<4;++k)close(got[k],source[k]);}}
        context->Unmap(staging.Get(),0);
    }
};
void run(bool hardware){
    Fixture f(hardware);f.vertexProbe();
    const float nan=std::numeric_limits<float>::quiet_NaN();Bank bank{};for(auto& row:bank)row.fill(nan);
    bank[0]={0.625f,-0.5f,1.25f,0.3125f};bank[1]={0.0625f,0.125f,nan,nan};bank[9]={0.1875f,0.25f,nan,nan};
    for(int pattern=0;pattern<3;++pattern){const auto t=f.texture(pattern);
        for(int shader=0;shader<4;++shader)for(bool linear:{false,true})for(int address:{0,1,2})for(int lod:{-1,0,1,2})f.render(t,shader,bank,lod,linear,address);
        // Zero and negative radii, independent horizontal/vertical scales.
        for(const auto xy:{std::array<float,2>{0,0},std::array<float,2>{-0.125f,0.25f},std::array<float,2>{0.1875f,0}}){auto changed=bank;changed[1][0]=changed[9][0]=xy[0];changed[1][1]=changed[9][1]=xy[1];for(int shader:{2,3})f.render(t,shader,changed,0,true,true);}
        for(int shader=0;shader<4;++shader)for(uint32_t word:{0x10001u,0x1000Bu,0x10101u,0x1010Bu,0x10181u,0x1018Bu,0x10701u,0x1070Bu})for(bool enable:{false,true})f.render(t,shader,bank,0,true,true,true,word,enable);
    }
    std::printf("PASS post shaders %s: %zu checks over %zu draws; original VS, four PS, point/linear clamp/wrap/mirror, three mips, dead NaNs, packed blend8\n",hardware?"hardware":"WARP",checks,draws);
}
}
int main(int argc,char** argv)try{need(argc==1||(argc==2&&std::string(argv[1])=="--hardware"),"Expected optional --hardware");run(argc==2);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL post shaders after %zu checks: %s\n",checks,e.what());return 1;}
