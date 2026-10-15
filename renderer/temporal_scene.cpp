#include "temporal_scene.h"
#include "skin_input.h"
#include "VSTemporalOutput.h"
#include "PSTemporalOutput.h"
#include "VSTemporalUiBackground.h"
#include "PSTemporalUiBackground.h"
#include "VSTemporal_8200D3AC.h"
#include "PSTemporal_8200D3AC.h"
#include "VSTemporal_8201700C.h"
#include "PSTemporal_8201700C.h"
#include "VSTemporal_8202B4CC.h"
#include "PSTemporal_8202B4CC.h"
#include "VSTemporal_82046D9C.h"
#include "PSTemporal_82046D9C.h"
#include "VSTemporal_82047318.h"
#include "PSTemporal_82047318.h"
#include "VSTemporal_8204364C.h"
#include "PSTemporal_8204364C.h"
#include "VSTemporal_82043BC0.h"
#include "PSTemporal_82043BC0.h"
#include "VSTemporal_8202B884.h"
#include "PSTemporal_8202B884.h"
#include "VSTemporal_8201739C.h"
#include "PSTemporal_8201739C.h"
#include "VSTemporal_8201A07C.h"
#include "PSTemporal_8201A07C.h"
#include "VSTemporal_82054F2C.h"
#include "PSTemporal_82054F2C.h"
#include "VSTemporal_8205855C.h"
#include "PSTemporal_8205855C.h"
#include "VSTemporal_8201A430.h"
#include "PSTemporal_8201A430.h"
#include "VSTemporal_820589EC.h"
#include "PSTemporal_820589EC.h"
#include "VSTemporal_82055440.h"
#include "PSTemporal_82055440.h"
#include "VSTemporal_82051EEC.h"
#include "PSTemporal_82051EEC.h"
#include "VSTemporal_82052358.h"
#include "PSTemporal_82052358.h"
#include "VSTemporal_8200D734.h"
#include "PSTemporal_8200D734.h"
#include "VSTemporal_8205BE70.h"
#include "PSTemporal_8205BE70.h"
#include "VSTemporal_8205E0B8.h"
#include "PSTemporal_8205E0B8.h"
#include "VSTemporal_8205DABC.h"
#include "PSTemporal_8205DABC.h"
#include "VSTemporal_820398BC.h"
#include "PSTemporal_820398BC.h"
#include "VSTemporal_82039D70.h"
#include "PSTemporal_82039D70.h"
#include "VSTemporal_82007C1C.h"
#include "PSTemporal_82007C1C.h"
#include "VSTemporal_82008E20.h"
#include "PSTemporal_82008E20.h"
#include "VSTemporal_8201E6DC.h"
#include "PSTemporal_8201E6DC.h"
#include "VSTemporal_8201F984.h"
#include "PSTemporal_8201F984.h"
#include "VSTemporal_8201146C.h"
#include "PSTemporal_8201146C.h"
#include "VSTemporal_820126EC.h"
#include "PSTemporal_820126EC.h"
#include "VSTemporal_8202579C.h"
#include "PSTemporal_8202579C.h"
#include "VSTemporal_820269E8.h"
#include "PSTemporal_820269E8.h"
#include "VSTemporal_8203D93C.h"
#include "PSTemporal_8203D93C.h"
#include "VSTemporal_8203ED38.h"
#include "PSTemporal_8203ED38.h"
#include "VSTemporal_8204BA4C.h"
#include "PSTemporal_8204BA4C.h"
#include "VSTemporal_8204CE90.h"
#include "PSTemporal_8204CE90.h"
#include "VSTemporal_82120C04.h"
#include "PSTemporal_82120C04.h"
#include "VSTemporal_82121BE8.h"
#include "PSTemporal_82121BE8.h"
#include "VSTemporal_82036F08.h"
#include "PSTemporal_82036F08.h"
#include "VSTemporal_82036C2C.h"
#include "PSTemporal_82036C2C.h"
#include "VSTemporal_82156548.h"
#include "PSTemporal_82156548.h"
#include "VSTemporal_821511D8.h"
#include "PSTemporal_821511D8.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <unordered_map>
namespace Simpsons::Graphics {
namespace {
void require(bool yes,const char* why){if(!yes)throw Error(why);}
void check(HRESULT hr,const char* why){if(FAILED(hr))throw Error(why);}
struct Control {std::array<float,2> jitter,size;uint32_t history,conservative;std::array<uint32_t,2> reserved{};};
static_assert(sizeof(Control)==32);
struct Code {const void* vertex;size_t vertexBytes;const void* pixel;size_t pixelBytes;};
Code shaderCode(uint32_t shader) {
    switch(shader){case 0x8200D3AC:return {kVSTemporal_8200D3AC,sizeof(kVSTemporal_8200D3AC),kPSTemporal_8200D3AC,sizeof(kPSTemporal_8200D3AC)};
case 0x8201700C:return {kVSTemporal_8201700C,sizeof(kVSTemporal_8201700C),kPSTemporal_8201700C,sizeof(kPSTemporal_8201700C)};
case 0x8202B4CC:return {kVSTemporal_8202B4CC,sizeof(kVSTemporal_8202B4CC),kPSTemporal_8202B4CC,sizeof(kPSTemporal_8202B4CC)};
case 0x82046D9C:return {kVSTemporal_82046D9C,sizeof(kVSTemporal_82046D9C),kPSTemporal_82046D9C,sizeof(kPSTemporal_82046D9C)};
case 0x82047318:return {kVSTemporal_82047318,sizeof(kVSTemporal_82047318),kPSTemporal_82047318,sizeof(kPSTemporal_82047318)};
case 0x8204364C:return {kVSTemporal_8204364C,sizeof(kVSTemporal_8204364C),kPSTemporal_8204364C,sizeof(kPSTemporal_8204364C)};
case 0x82043BC0:return {kVSTemporal_82043BC0,sizeof(kVSTemporal_82043BC0),kPSTemporal_82043BC0,sizeof(kPSTemporal_82043BC0)};
case 0x8202B884:return {kVSTemporal_8202B884,sizeof(kVSTemporal_8202B884),kPSTemporal_8202B884,sizeof(kPSTemporal_8202B884)};
case 0x8201739C:return {kVSTemporal_8201739C,sizeof(kVSTemporal_8201739C),kPSTemporal_8201739C,sizeof(kPSTemporal_8201739C)};
case 0x8201A07C:return {kVSTemporal_8201A07C,sizeof(kVSTemporal_8201A07C),kPSTemporal_8201A07C,sizeof(kPSTemporal_8201A07C)};
case 0x82054F2C:return {kVSTemporal_82054F2C,sizeof(kVSTemporal_82054F2C),kPSTemporal_82054F2C,sizeof(kPSTemporal_82054F2C)};
case 0x8205855C:return {kVSTemporal_8205855C,sizeof(kVSTemporal_8205855C),kPSTemporal_8205855C,sizeof(kPSTemporal_8205855C)};
case 0x8201A430:return {kVSTemporal_8201A430,sizeof(kVSTemporal_8201A430),kPSTemporal_8201A430,sizeof(kPSTemporal_8201A430)};
case 0x820589EC:return {kVSTemporal_820589EC,sizeof(kVSTemporal_820589EC),kPSTemporal_820589EC,sizeof(kPSTemporal_820589EC)};
case 0x82055440:return {kVSTemporal_82055440,sizeof(kVSTemporal_82055440),kPSTemporal_82055440,sizeof(kPSTemporal_82055440)};
case 0x82051EEC:return {kVSTemporal_82051EEC,sizeof(kVSTemporal_82051EEC),kPSTemporal_82051EEC,sizeof(kPSTemporal_82051EEC)};
case 0x82052358:return {kVSTemporal_82052358,sizeof(kVSTemporal_82052358),kPSTemporal_82052358,sizeof(kPSTemporal_82052358)};
case 0x8200D734:return {kVSTemporal_8200D734,sizeof(kVSTemporal_8200D734),kPSTemporal_8200D734,sizeof(kPSTemporal_8200D734)};
case 0x8205BE70:return {kVSTemporal_8205BE70,sizeof(kVSTemporal_8205BE70),kPSTemporal_8205BE70,sizeof(kPSTemporal_8205BE70)};
case 0x8205E0B8:return {kVSTemporal_8205E0B8,sizeof(kVSTemporal_8205E0B8),kPSTemporal_8205E0B8,sizeof(kPSTemporal_8205E0B8)};
case 0x8205DABC:return {kVSTemporal_8205DABC,sizeof(kVSTemporal_8205DABC),kPSTemporal_8205DABC,sizeof(kPSTemporal_8205DABC)};
case 0x820398BC:return {kVSTemporal_820398BC,sizeof(kVSTemporal_820398BC),kPSTemporal_820398BC,sizeof(kPSTemporal_820398BC)};
case 0x82039D70:return {kVSTemporal_82039D70,sizeof(kVSTemporal_82039D70),kPSTemporal_82039D70,sizeof(kPSTemporal_82039D70)};
case 0x82007C1C:return {kVSTemporal_82007C1C,sizeof(kVSTemporal_82007C1C),kPSTemporal_82007C1C,sizeof(kPSTemporal_82007C1C)};
case 0x82008E20:return {kVSTemporal_82008E20,sizeof(kVSTemporal_82008E20),kPSTemporal_82008E20,sizeof(kPSTemporal_82008E20)};
case 0x8201E6DC:return {kVSTemporal_8201E6DC,sizeof(kVSTemporal_8201E6DC),kPSTemporal_8201E6DC,sizeof(kPSTemporal_8201E6DC)};
case 0x8201F984:return {kVSTemporal_8201F984,sizeof(kVSTemporal_8201F984),kPSTemporal_8201F984,sizeof(kPSTemporal_8201F984)};
case 0x8201146C:return {kVSTemporal_8201146C,sizeof(kVSTemporal_8201146C),kPSTemporal_8201146C,sizeof(kPSTemporal_8201146C)};
case 0x820126EC:return {kVSTemporal_820126EC,sizeof(kVSTemporal_820126EC),kPSTemporal_820126EC,sizeof(kPSTemporal_820126EC)};
case 0x8202579C:return {kVSTemporal_8202579C,sizeof(kVSTemporal_8202579C),kPSTemporal_8202579C,sizeof(kPSTemporal_8202579C)};
case 0x820269E8:return {kVSTemporal_820269E8,sizeof(kVSTemporal_820269E8),kPSTemporal_820269E8,sizeof(kPSTemporal_820269E8)};
case 0x8203D93C:return {kVSTemporal_8203D93C,sizeof(kVSTemporal_8203D93C),kPSTemporal_8203D93C,sizeof(kPSTemporal_8203D93C)};
case 0x8203ED38:return {kVSTemporal_8203ED38,sizeof(kVSTemporal_8203ED38),kPSTemporal_8203ED38,sizeof(kPSTemporal_8203ED38)};
case 0x8204BA4C:return {kVSTemporal_8204BA4C,sizeof(kVSTemporal_8204BA4C),kPSTemporal_8204BA4C,sizeof(kPSTemporal_8204BA4C)};
case 0x8204CE90:return {kVSTemporal_8204CE90,sizeof(kVSTemporal_8204CE90),kPSTemporal_8204CE90,sizeof(kPSTemporal_8204CE90)};
case 0x82120C04:return {kVSTemporal_82120C04,sizeof(kVSTemporal_82120C04),kPSTemporal_82120C04,sizeof(kPSTemporal_82120C04)};
case 0x82121BE8:return {kVSTemporal_82121BE8,sizeof(kVSTemporal_82121BE8),kPSTemporal_82121BE8,sizeof(kPSTemporal_82121BE8)};
case 0x82036F08:return {kVSTemporal_82036F08,sizeof(kVSTemporal_82036F08),kPSTemporal_82036F08,sizeof(kPSTemporal_82036F08)};
case 0x82036C2C:return {kVSTemporal_82036C2C,sizeof(kVSTemporal_82036C2C),kPSTemporal_82036C2C,sizeof(kPSTemporal_82036C2C)};
case 0x82156548:return {kVSTemporal_82156548,sizeof(kVSTemporal_82156548),kPSTemporal_82156548,sizeof(kPSTemporal_82156548)};
case 0x821511D8:return {kVSTemporal_821511D8,sizeof(kVSTemporal_821511D8),kPSTemporal_821511D8,sizeof(kPSTemporal_821511D8)};
    default:throw Error("Temporal geometry shader has no exact color/coverage adapter");}
}
ComPtr<ID3D11Buffer> constant(ID3D11Device* device,UINT bytes) {
    D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> result;check(device->CreateBuffer(&d,nullptr,&result),"Temporal constant allocation failed");return result;
}
ComPtr<ID3D11InputLayout> inputLayout(ID3D11Device* device,uint32_t kind,const Code& code) {
    const D3D11_INPUT_ELEMENT_DESC rigid[]={
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",3,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",4,DXGI_FORMAT_R32G32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",5,DXGI_FORMAT_R32G32B32_FLOAT,0,56,D3D11_INPUT_PER_VERTEX_DATA,0}};
    const D3D11_INPUT_ELEMENT_DESC mono[]={
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,28,D3D11_INPUT_PER_VERTEX_DATA,0}};
    const D3D11_INPUT_ELEMENT_DESC sky[]={
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",2,DXGI_FORMAT_R32G32_FLOAT,0,20,D3D11_INPUT_PER_VERTEX_DATA,0}};
    const D3D11_INPUT_ELEMENT_DESC immediate[]={
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
    const D3D11_INPUT_ELEMENT_DESC ball[]={
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0}};
    std::vector<D3D11_INPUT_ELEMENT_DESC> skin(SkinInputElements.begin(),SkinInputElements.end());
    skin.push_back({"TEXCOORD",12,DXGI_FORMAT_R32G32_FLOAT,0,152,D3D11_INPUT_PER_VERTEX_DATA,0});
    require(kind<6,"Temporal declaration kind is invalid");
    const auto* elements=kind==0?rigid:kind==1?skin.data():kind==2?mono:kind==3?sky:kind==4?ball:immediate;
    const UINT count=kind==0?UINT(std::size(rigid)):kind==1?UINT(skin.size()):kind==2?UINT(std::size(mono)):kind==3?UINT(std::size(sky)):kind==4?UINT(std::size(ball)):UINT(std::size(immediate));
    ComPtr<ID3D11InputLayout> result;check(device->CreateInputLayout(elements,count,code.vertex,code.vertexBytes,&result),"Temporal original declaration creation failed");return result;
}
struct SwapContext {
    ID3D11DeviceContext1* context;ComPtr<ID3DDeviceContextState> old;
    SwapContext(ID3D11DeviceContext1* c,ID3DDeviceContextState* clean):context(c){context->SwapDeviceContextState(clean,&old);}
    ~SwapContext(){context->SwapDeviceContextState(old.Get(),nullptr);}
};
}
struct TemporalScenePipeline::State {
    struct Attachment {ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11RenderTargetView> target;ComPtr<ID3D11ShaderResourceView> sampled;};
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<ID3D11DeviceContext1> context1;ComPtr<ID3DDeviceContextState> isolated;
    DlssExtent extent;Attachment motion,bias;
    ComPtr<ID3D11Texture2D> color;ComPtr<ID3D11RenderTargetView> colorView;ComPtr<ID3D11DepthStencilView> depth;
    ComPtr<ID3D11Texture2D> worldColor;ComPtr<ID3D11DepthStencilView> worldDepth;
    ComPtr<ID3D11VertexShader> outputVertex;ComPtr<ID3D11PixelShader> outputPixel;
    ComPtr<ID3D11VertexShader> uiVertex;ComPtr<ID3D11PixelShader> uiPixel;ComPtr<ID3D11SamplerState> uiSampler;
    ComPtr<ID3D11Buffer> uiExtent,outputJitter;
    ComPtr<ID3D11RasterizerState> outputRaster;ComPtr<ID3D11DepthStencilState> outputDepth;ComPtr<ID3D11BlendState> outputBlend;
    TemporalGeometryHistory history;
    std::unordered_map<TemporalGeometryIdentity,std::shared_ptr<TemporalGeometryDraw>,TemporalGeometryIdentityHash> draws;
    struct Adapter {ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;ComPtr<ID3D11InputLayout> layout;};
    std::unordered_map<uint32_t,Adapter> adapters;
    struct Blend {D3D11_BLEND_DESC original{};ComPtr<ID3D11BlendState> state;};std::vector<Blend> blends;
    LARGE_INTEGER frequency{},lastFrame{},started{};
    uint64_t camera{},cameraGeneration{},geometryDraws{},motionDraws{};
    DWORD owner=GetCurrentThreadId();std::array<float,2> jitter{};float milliseconds{};
    bool active{},reset=true,qualified=true,reconstructed{};
    std::unordered_map<TemporalGeometryIdentity,uint64_t,TemporalGeometryIdentityHash> lastUsed;
    void requireOwner() const {require(GetCurrentThreadId()==owner,"Temporal scene used from another thread");check(device->GetDeviceRemovedReason(),"Temporal graphics device removed");}
    Attachment attachment(DXGI_FORMAT format) {
        UINT support{};check(device->CheckFormatSupport(format,&support),"Temporal format query failed");
        constexpr UINT needed=D3D11_FORMAT_SUPPORT_RENDER_TARGET|D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
        require((support&needed)==needed,"Temporal format cannot render and sample on this device");
        D3D11_TEXTURE2D_DESC d{};d.Width=extent.renderWidth;d.Height=extent.renderHeight;d.MipLevels=d.ArraySize=1;
        d.Format=format;d.SampleDesc={1,0};d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        Attachment a;check(device->CreateTexture2D(&d,nullptr,&a.texture),"Temporal target creation failed");
        check(device->CreateRenderTargetView(a.texture.Get(),nullptr,&a.target),"Temporal render view failed");
        check(device->CreateShaderResourceView(a.texture.Get(),nullptr,&a.sampled),"Temporal sampled view failed");return a;
    }
    ComPtr<ID3D11BlendState> fusedBlend(const ShadowMeshDraw& d) {
        D3D11_BLEND_DESC desc{};desc.IndependentBlendEnable=TRUE;
        auto factor=[](uint32_t v){switch(v){case 0:return D3D11_BLEND_ZERO;case 1:return D3D11_BLEND_ONE;case 6:return D3D11_BLEND_SRC_ALPHA;case 7:return D3D11_BLEND_INV_SRC_ALPHA;default:throw Error("Temporal original blend factor is unqualified");}};
        auto& c=desc.RenderTarget[0];c.BlendEnable=d.temporalBlendEnable;c.RenderTargetWriteMask=UINT8(d.colorMask);
        c.SrcBlend=factor(d.temporalBlendWord&255);c.DestBlend=factor((d.temporalBlendWord>>8)&255);
        c.SrcBlendAlpha=factor((d.temporalBlendWord>>16)&255);c.DestBlendAlpha=factor((d.temporalBlendWord>>24)&255);
        c.BlendOp=c.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        for(UINT i=1;i<3;++i){auto& m=desc.RenderTarget[i];m.RenderTargetWriteMask=i==1?3:1;m.SrcBlend=m.SrcBlendAlpha=D3D11_BLEND_ONE;m.DestBlend=m.DestBlendAlpha=D3D11_BLEND_ZERO;m.BlendOp=m.BlendOpAlpha=D3D11_BLEND_OP_ADD;}
        for(const auto& entry:blends)if(!std::memcmp(&entry.original,&desc,sizeof(desc)))return entry.state;
        Blend entry;entry.original=desc;check(device->CreateBlendState(&desc,&entry.state),"Temporal independent blend failed");
        blends.push_back(entry);return entry.state;
    }
};
TemporalScenePipeline::TemporalScenePipeline(ID3D11Device* device,ID3D11DeviceContext* context,const DlssExtent& extent):state(std::make_unique<State>()) {
    auto& s=*state;s.device=device;s.context=context;s.extent=extent;s.requireOwner();
    require(QueryPerformanceFrequency(&s.frequency)&&QueryPerformanceCounter(&s.lastFrame),"Temporal frame clock unavailable");
    check(s.context.As(&s.context1),"Temporal D3D11.1 context unavailable");
    ComPtr<ID3D11Device1> d1;check(s.device.As(&d1),"Temporal D3D11.1 device unavailable");
    const auto level=device->GetFeatureLevel();D3D_FEATURE_LEVEL actual{};
    check(d1->CreateDeviceContextState(0,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&actual,&s.isolated),"Temporal isolated context failed");
    s.motion=s.attachment(DXGI_FORMAT_R32G32_FLOAT);s.bias=s.attachment(DXGI_FORMAT_R8_UNORM);
    check(device->CreateVertexShader(kVSTemporalOutput,sizeof(kVSTemporalOutput),nullptr,&s.outputVertex),"Temporal output VS failed");
    check(device->CreatePixelShader(kPSTemporalOutput,sizeof(kPSTemporalOutput),nullptr,&s.outputPixel),"Temporal output PS failed");
    check(device->CreateVertexShader(kVSTemporalUiBackground,sizeof(kVSTemporalUiBackground),nullptr,&s.uiVertex),"Temporal UI background VS failed");
    check(device->CreatePixelShader(kPSTemporalUiBackground,sizeof(kPSTemporalUiBackground),nullptr,&s.uiPixel),"Temporal UI background PS failed");
    D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxLOD=D3D11_FLOAT32_MAX;
    check(device->CreateSamplerState(&sampler,&s.uiSampler),"Temporal UI background sampler failed");
    s.uiExtent=constant(device,16);s.outputJitter=constant(device,16);
    D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
    check(device->CreateRasterizerState(&r,&s.outputRaster),"Temporal output raster failed");
    D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
    check(device->CreateDepthStencilState(&depth,&s.outputDepth),"Temporal output depth failed");
    D3D11_BLEND_DESC blend{};auto& b=blend.RenderTarget[0];b.RenderTargetWriteMask=15;b.SrcBlend=b.SrcBlendAlpha=D3D11_BLEND_ONE;b.DestBlend=b.DestBlendAlpha=D3D11_BLEND_ZERO;b.BlendOp=b.BlendOpAlpha=D3D11_BLEND_OP_ADD;
    check(device->CreateBlendState(&blend,&s.outputBlend),"Temporal output blend failed");
}
TemporalScenePipeline::~TemporalScenePipeline()=default;
void TemporalScenePipeline::registerWorldTargets(ID3D11Texture2D* color,ID3D11DepthStencilView* depth) {
    auto& s=*state;s.requireOwner();require(color&&depth&&!s.active,"Temporal world registration requires immutable startup owners");
    require(!s.worldColor||(s.worldColor.Get()==color&&s.worldDepth.Get()==depth),"Temporal world target registration cannot replace a live original owner");
    ComPtr<ID3D11Device> colorDevice,depthDevice;color->GetDevice(&colorDevice);depth->GetDevice(&depthDevice);
    require(colorDevice.Get()==s.device.Get()&&depthDevice.Get()==s.device.Get(),"Temporal registered world belongs to another device");
    D3D11_TEXTURE2D_DESC desc{};color->GetDesc(&desc);
    require(desc.Width==s.extent.renderWidth&&desc.Height==s.extent.renderHeight&&desc.SampleDesc.Count==1&&!desc.SampleDesc.Quality,"Temporal registered world color extent/sample mismatch");
    ComPtr<ID3D11Resource> resource;depth->GetResource(&resource);ComPtr<ID3D11Texture2D> texture;check(resource.As(&texture),"Temporal registered depth is not a texture");
    texture->GetDesc(&desc);require(desc.Width==s.extent.renderWidth&&desc.Height==s.extent.renderHeight&&desc.SampleDesc.Count==1&&!desc.SampleDesc.Quality,"Temporal registered world depth extent/sample mismatch");
    s.worldColor=color;s.worldDepth=depth;
}
void TemporalScenePipeline::begin(ID3D11Texture2D* color,ID3D11RenderTargetView* view,ID3D11DepthStencilView* depth,uint64_t camera,uint64_t generation,bool reset) {
    auto& s=*state;s.requireOwner();require(color&&view&&depth&&camera&&generation,"Temporal scene requires actual camera lifetime/color/depth");
    require(s.worldColor.Get()==color&&s.worldDepth.Get()==depth,"Temporal scene began on unregistered original world owners");
    ComPtr<ID3D11Device> colorDevice,viewDevice,depthDevice;color->GetDevice(&colorDevice);view->GetDevice(&viewDevice);depth->GetDevice(&depthDevice);
    require(colorDevice.Get()==s.device.Get()&&viewDevice.Get()==s.device.Get()&&depthDevice.Get()==s.device.Get(),"Temporal scene attachment belongs to another device");
    ComPtr<ID3D11Resource> colorResource;view->GetResource(&colorResource);require(colorResource.Get()==color,"Temporal scene color view is not its original owner");
    D3D11_TEXTURE2D_DESC desc{};color->GetDesc(&desc);
    require(desc.Width==s.extent.renderWidth&&desc.Height==s.extent.renderHeight&&desc.SampleDesc.Count==1&&!desc.SampleDesc.Quality,"Temporal scene color extent/samples differ");
    ComPtr<ID3D11Resource> resource;depth->GetResource(&resource);ComPtr<ID3D11Texture2D> texture;check(resource.As(&texture),"Temporal depth is not a texture");
    texture->GetDesc(&desc);require(desc.Width==s.extent.renderWidth&&desc.Height==s.extent.renderHeight&&desc.SampleDesc.Count==1&&!desc.SampleDesc.Quality,"Temporal scene depth extent/samples differ");
    if(s.active&&s.color.Get()==color&&s.depth.Get()==depth) {
        require(s.camera==camera&&s.cameraGeneration==generation,"A different camera lifetime cleared the active temporal scene");
        require(!s.geometryDraws&&!s.reconstructed,"Main scene was cleared again after temporal geometry submission");
        return;
    }
    require(!s.active,"A second temporal camera began before reconstruction");
    if(reset||s.camera!=camera||s.cameraGeneration!=generation){s.history.reset();s.reset=true;}
    s.camera=camera;s.cameraGeneration=generation;s.color=color;s.colorView=view;s.depth=depth;s.active=true;s.qualified=true;s.reconstructed=false;s.geometryDraws=s.motionDraws=0;
    require(QueryPerformanceCounter(&s.started),"Temporal frame clock failed");
    s.milliseconds=float(double(s.started.QuadPart-s.lastFrame.QuadPart)*1000.0/double(s.frequency.QuadPart));
    if(s.milliseconds>1000){s.history.reset();s.reset=true;}
    s.jitter=temporalJitter(s.history.frameNumber()-1,s.extent.renderWidth,s.extent.renderHeight,s.extent.outputWidth,s.extent.outputHeight);
    const float zero[4]={0,0,0,0},one[4]={1,1,1,1};
    s.context->ClearRenderTargetView(s.motion.target.Get(),zero);s.context->ClearRenderTargetView(s.bias.target.Get(),one);
}
bool TemporalScenePipeline::matches(ID3D11Texture2D* color,ID3D11DepthStencilView* depth) const {
    const auto& s=*state;s.requireOwner();return s.active&&s.color.Get()==color&&s.depth.Get()==depth;
}
bool TemporalScenePipeline::active() const {state->requireOwner();return state->active;}
bool TemporalScenePipeline::hasGeometry() const {state->requireOwner();return state->active&&state->geometryDraws!=0;}
bool TemporalScenePipeline::reconstructed() const {state->requireOwner();return state->active&&state->reconstructed;}
void TemporalScenePipeline::markReconstructed() {
    auto& s=*state;s.requireOwner();require(s.active&&s.qualified&&!s.reconstructed,"Temporal reconstruction completion has no unique active scene");s.reconstructed=true;
}
std::shared_ptr<TemporalGeometryDraw> TemporalScenePipeline::draw(ID3D11Texture2D* color,ID3D11RenderTargetView* view,ID3D11DepthStencilView* depth,const ShadowMeshDraw& d,uint32_t shader,uint32_t kind,bool recorded) {
    auto& s=*state;s.requireOwner();
    const bool originalWorld=recorded?(s.worldColor.Get()==color&&s.worldDepth.Get()==depth):matches(color,depth);
    if(!originalWorld||!(d.colorMask&7))return {};
    require(!s.active||!s.reconstructed,"World geometry was submitted after temporal reconstruction");
    require(d.temporalObject&&d.temporalGeneration&&d.temporalGeometry&&d.temporalSubmesh,"Temporal draw lacks stable instance lifetime");
    require((kind==5)==(shader==0x821511D8),"Temporal Immediate shader/declaration identity differs");
    TemporalGeometryIdentity id{d.temporalObject,d.temporalGeneration,d.temporalGeometry,d.temporalSubmesh,shader};
    if(!recorded)s.lastUsed[id]=s.history.frameNumber();
    if(!recorded){const auto old=s.draws.find(id);if(old!=s.draws.end()){
        old->second->blend=s.fusedBlend(d);old->second->conservative=d.temporalBlendEnable||!d.depthWrite;
        old->second->sampleMask=d.multisampleMask;old->second->color=view;old->second->depth=depth;return old->second;
    }}
    auto adapter=s.adapters.find(shader);
    if(adapter==s.adapters.end()){
        const auto code=shaderCode(shader);State::Adapter a;
        check(s.device->CreateVertexShader(code.vertex,code.vertexBytes,nullptr,&a.vertex),"Temporal fused VS failed");
        check(s.device->CreatePixelShader(code.pixel,code.pixelBytes,nullptr,&a.pixel),"Temporal fused PS failed");
        a.layout=inputLayout(s.device.Get(),kind,code);adapter=s.adapters.emplace(shader,std::move(a)).first;
    }
    auto value=std::make_shared<TemporalGeometryDraw>();value->scene=shared_from_this();value->identity=id;
    value->vertex=adapter->second.vertex;value->pixel=adapter->second.pixel;value->layout=adapter->second.layout;
    value->previous=constant(s.device.Get(),256*16);value->controls=constant(s.device.Get(),sizeof(Control));value->previousBooleans=constant(s.device.Get(),16);
    value->blend=s.fusedBlend(d);value->conservative=d.temporalBlendEnable||!d.depthWrite;value->sampleMask=d.multisampleMask;
    value->color=view;value->depth=depth;value->motion=s.motion.target;value->bias=s.bias.target;
    if(!recorded)s.draws.emplace(id,value);return value;
}
void TemporalGeometryDraw::prepare(ID3D11DeviceContext* context,std::span<const TemporalRegister> current,bool skinned) {
    const auto owner=scene.lock();require(bool(owner),"Temporal draw outlived its backend");auto& s=*owner->state;s.requireOwner();
    require(context==s.context.Get()&&s.active&&!s.reconstructed,"Temporal preparation requires unreconstructed owning world scene");
    const auto history=s.history.capture(identity,current);
    std::array<TemporalRegister,256> bank{};std::copy(history.previous.begin(),history.previous.end(),bank.begin());
    const Control data{s.jitter,{float(s.extent.renderWidth),float(s.extent.renderHeight)},uint32_t(history.valid),uint32_t(conservative)};
    const std::array<uint32_t,4> booleans={uint32_t(skinned),0,0,0};
    context->UpdateSubresource(previous.Get(),0,nullptr,bank.data(),0,0);context->UpdateSubresource(controls.Get(),0,nullptr,&data,0,0);
    context->UpdateSubresource(previousBooleans.Get(),0,nullptr,booleans.data(),0,0);prepared=true;++s.geometryDraws;++s.motionDraws;
}
void TemporalGeometryDraw::packedOutput(ID3D11RenderTargetView* output,bool uncertain) {
    const auto owner=scene.lock();require(bool(owner),"Packed temporal output outlived its backend");auto& s=*owner->state;s.requireOwner();
    require(output&&s.active&&!s.reconstructed,"Packed temporal output requires active world color");
    ComPtr<ID3D11Device> device;output->GetDevice(&device);require(device.Get()==s.device.Get(),"Packed temporal color belongs to another device");
    D3D11_RENDER_TARGET_VIEW_DESC view{};output->GetDesc(&view);
    require(view.Format==DXGI_FORMAT_R10G10B10A2_UINT&&view.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2D,"Packed temporal color lost original UINT output type");
    ComPtr<ID3D11Resource> resource;output->GetResource(&resource);ComPtr<ID3D11Texture2D> texture;check(resource.As(&texture),"Packed temporal color is not a texture");
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    require(desc.Width==s.extent.renderWidth&&desc.Height==s.extent.renderHeight&&desc.SampleDesc.Count==1&&!desc.SampleDesc.Quality,"Packed temporal MRT extent/sample mismatch");
    color=output;conservative=uncertain;
}
void TemporalGeometryDraw::bind(ID3D11DeviceContext* context) {
    const auto owner=scene.lock();require(bool(owner),"Temporal draw outlived its backend");auto& s=*owner->state;s.requireOwner();
    require(prepared||context->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED,"Temporal draw not prepared");
    auto* cb=previous.Get();context->VSSetConstantBuffers(10,1,&cb);cb=controls.Get();context->VSSetConstantBuffers(11,1,&cb);context->PSSetConstantBuffers(11,1,&cb);cb=previousBooleans.Get();context->VSSetConstantBuffers(12,1,&cb);
    context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);context->IASetInputLayout(layout.Get());
    ID3D11RenderTargetView* mrt[]={color.Get(),motion.Get(),bias.Get()};context->OMSetRenderTargets(3,mrt,depth.Get());context->OMSetBlendState(blend.Get(),nullptr,sampleMask);
}
struct ScopedTemporalGeometryDraw::State {
    ID3D11DeviceContext* context;ComPtr<ID3D11DeviceContext1> context1;
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;ComPtr<ID3D11InputLayout> layout;
    std::array<ComPtr<ID3D11Buffer>,3> vs;ComPtr<ID3D11Buffer> ps;
    std::array<UINT,3> first{},count{};UINT psFirst{},psCount{};
    std::array<ComPtr<ID3D11RenderTargetView>,8> targets;ComPtr<ID3D11DepthStencilView> depth;
    ComPtr<ID3D11BlendState> blend;FLOAT factors[4]{};UINT mask{};
    explicit State(ID3D11DeviceContext* c):context(c) {
        check(c->QueryInterface(IID_PPV_ARGS(&context1)),"Temporal state restoration needs D3D11.1");
        context->VSGetShader(&vertex,nullptr,nullptr);context->PSGetShader(&pixel,nullptr,nullptr);context->IAGetInputLayout(&layout);
        const UINT slots[]={10,11,12};for(UINT i=0;i<3;++i)context1->VSGetConstantBuffers1(slots[i],1,&vs[i],&first[i],&count[i]);
        context1->PSGetConstantBuffers1(11,1,&ps,&psFirst,&psCount);
        std::array<ID3D11RenderTargetView*,8> raw{};context->OMGetRenderTargets(8,raw.data(),&depth);for(size_t i=0;i<raw.size();++i)targets[i].Attach(raw[i]);
        context->OMGetBlendState(&blend,factors,&mask);
    }
    ~State() {
        context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);context->IASetInputLayout(layout.Get());
        const UINT slots[]={10,11,12};for(UINT i=0;i<3;++i){auto* cb=vs[i].Get();context1->VSSetConstantBuffers1(slots[i],1,&cb,&first[i],&count[i]);}
        auto* cb=ps.Get();context1->PSSetConstantBuffers1(11,1,&cb,&psFirst,&psCount);
        std::array<ID3D11RenderTargetView*,8> raw{};for(size_t i=0;i<raw.size();++i)raw[i]=targets[i].Get();
        context->OMSetRenderTargets(8,raw.data(),depth.Get());context->OMSetBlendState(blend.Get(),factors,mask);
    }
};
ScopedTemporalGeometryDraw::ScopedTemporalGeometryDraw(ID3D11DeviceContext* context,const std::shared_ptr<TemporalGeometryDraw>& draw) {
    if(draw){state=std::make_unique<State>(context);draw->bind(context);}
}
ScopedTemporalGeometryDraw::~ScopedTemporalGeometryDraw()=default;
DlssFrame TemporalScenePipeline::frame() const {
    const auto& s=*state;s.requireOwner();require(s.active&&s.qualified&&!s.reconstructed,"Temporal scene coverage unqualified or already reconstructed");float dt=s.milliseconds;
    if(!(dt>0)){LARGE_INTEGER now{};require(QueryPerformanceCounter(&now),"Temporal evaluation clock failed");dt=float(double(now.QuadPart-s.started.QuadPart)*1000.0/double(s.frequency.QuadPart));}
    return {s.jitter,dt,s.reset,true,s.geometryDraws,s.motionDraws};
}
ID3D11Texture2D* TemporalScenePipeline::motion() const{return state->motion.texture.Get();}
ID3D11Texture2D* TemporalScenePipeline::biasCurrent() const{return state->bias.texture.Get();}
void TemporalScenePipeline::copyOutput(ID3D11ShaderResourceView* input,ID3D11ShaderResourceView* original,ID3D11RenderTargetView* output,uint32_t width,uint32_t height,std::array<float,2> sourceJitter) {
    auto& s=*state;s.requireOwner();
    for(float component:sourceJitter)require(std::isfinite(component)&&std::abs(component)<=0.5f,"Temporal packed-alpha source jitter is invalid");
    const std::array<float,4> control{sourceJitter[0],sourceJitter[1],0,0};
    s.context->UpdateSubresource(s.outputJitter.Get(),0,nullptr,control.data(),0,0);
    SwapContext restore(s.context1.Get(),s.isolated.Get());auto* c=s.context.Get();
    c->OMSetRenderTargets(1,&output,nullptr);c->OMSetBlendState(s.outputBlend.Get(),nullptr,0xffffffff);c->OMSetDepthStencilState(s.outputDepth.Get(),0);
    c->RSSetState(s.outputRaster.Get());const D3D11_VIEWPORT viewport={0,0,float(width),float(height),0,1};c->RSSetViewports(1,&viewport);
    c->IASetInputLayout(nullptr);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(s.outputVertex.Get(),nullptr,0);c->PSSetShader(s.outputPixel.Get(),nullptr,0);
    ID3D11ShaderResourceView* inputs[]={input,original};c->PSSetShaderResources(0,2,inputs);
    auto* controlBuffer=s.outputJitter.Get();c->PSSetConstantBuffers(1,1,&controlBuffer);c->Draw(3,0);
}
ComPtr<ID3D11Buffer> TemporalScenePipeline::uiBackgroundJitterConstants() const {
    auto& s=*state;s.requireOwner();return s.outputJitter;
}
void TemporalScenePipeline::copyUiBackground(ID3D11ShaderResourceView* input,ID3D11RenderTargetView* output,uint32_t width,uint32_t height,std::array<float,2> sourceJitter) {
    auto& s=*state;s.requireOwner();require(!hasGeometry(),"UI-only spatial background cannot replace temporal world reconstruction");
    for(float value:sourceJitter)require(std::isfinite(value)&&std::abs(value)<=.5f,"UI background source jitter is not a finite render-pixel sample");
    const std::array<uint32_t,4> extent={width,height,0,0};s.context->UpdateSubresource(s.uiExtent.Get(),0,nullptr,extent.data(),0,0);
    // Reuse the actual device-owned b1 allocation on the serialized queue.
    const std::array<float,4> controls={sourceJitter[0],sourceJitter[1],0,0};
    s.context->UpdateSubresource(s.outputJitter.Get(),0,nullptr,controls.data(),0,0);
    SwapContext restore(s.context1.Get(),s.isolated.Get());auto* c=s.context.Get();
    c->OMSetRenderTargets(1,&output,nullptr);c->OMSetBlendState(s.outputBlend.Get(),nullptr,0xffffffff);c->OMSetDepthStencilState(s.outputDepth.Get(),0);
    c->RSSetState(s.outputRaster.Get());const D3D11_VIEWPORT viewport={0,0,float(width),float(height),0,1};c->RSSetViewports(1,&viewport);
    c->IASetInputLayout(nullptr);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(s.uiVertex.Get(),nullptr,0);c->PSSetShader(s.uiPixel.Get(),nullptr,0);c->PSSetShaderResources(0,1,&input);
    auto* dimensions=s.uiExtent.Get();c->PSSetConstantBuffers(0,1,&dimensions);
    auto* controlBuffer=s.outputJitter.Get();c->PSSetConstantBuffers(1,1,&controlBuffer);
    auto* sampler=s.uiSampler.Get();c->PSSetSamplers(0,1,&sampler);c->Draw(3,0);
}
void TemporalScenePipeline::finishFrame() {
    auto& s=*state;s.requireOwner();
    if(s.active)s.lastFrame=s.started;
    else require(QueryPerformanceCounter(&s.lastFrame),"UI-only temporal frame clock failed");
    if(s.active&&s.reconstructed){s.history.finishFrame();s.reset=false;}else{s.history.reset();s.reset=true;}
    std::erase_if(s.draws,[&s](const auto& pair){const auto use=s.lastUsed.find(pair.first);return use==s.lastUsed.end()||use->second+2<s.history.frameNumber();});
    std::erase_if(s.lastUsed,[&s](const auto& pair){return pair.second+2<s.history.frameNumber();});
    s.active=false;s.reconstructed=false;s.color.Reset();s.colorView.Reset();s.depth.Reset();s.geometryDraws=s.motionDraws=0;
}
void TemporalScenePipeline::reset() {auto& s=*state;s.requireOwner();s.history.reset();s.reset=true;s.active=false;s.color.Reset();s.colorView.Reset();s.depth.Reset();}
void TemporalScenePipeline::rejectUntrackedWorldDraw(const char* kind) {
    auto& s=*state;if(s.active){s.qualified=false;char why[180];std::snprintf(why,sizeof(why),"Temporal scene contains an untracked world %s draw",kind);throw Error(why);}
}
}

