#include "post_filter.h"
#include "ball_effect.h"
#include "VSTextured.h"
#include "PSBallEncodeDraw.h"
#include "PSBallCompositeDraw.h"
#include "VSPostAutoTextured.h"
#include "PSPostAlphaToRGBADraw.h"
#include "PSPostTexturedDraw.h"
#include "PSPostFilteredDraw.h"
#include "PSPostGlowDraw.h"
#include "PSLumaDraw.h"
#include "PSDofDraw.h"
#include "PSBlurDraw.h"
#include "PSBloomDraw.h"
#include "PSFogLinearDraw.h"
#include "PSFogExpDraw.h"
#include "PSFogExp2Draw.h"
#include "PSSatDraw.h"
#include "PSFlatEffectDraw.h"
#include "PSModulatedFlatDraw.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Simpsons::Graphics {
namespace {
void need(bool value,const char* message){if(!value)throw Error(message);}
void check(HRESULT hr,const char* message){if(FAILED(hr)){char text[192];
    std::snprintf(text,sizeof(text),"Native post filter %s: %08lX",message,ULONG(hr));throw Error(text);}}
ComPtr<ID3D11Buffer> upload(ID3D11Device* device,const void* bytes,UINT size,UINT bind){
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=size;desc.BindFlags=bind;desc.Usage=D3D11_USAGE_IMMUTABLE;
    D3D11_SUBRESOURCE_DATA data{bytes,0,0};ComPtr<ID3D11Buffer> result;
    check(device->CreateBuffer(&desc,&data,&result),"owned upload");return result;
}
// Keep all resource slots: binding/restoring a target must not silently erase an
// unrelated sampled view through D3D11's automatic read/write hazard handling.
struct Restore {
    ID3D11DeviceContext* c;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    std::array<ID3D11ClassInstance*,256> vsClasses{},psClasses{};
    UINT vsClassCount=256,psClassCount=256;
    ComPtr<ID3D11Buffer> vb,pc0,pc1;ComPtr<ID3D11InputLayout> layout;
    std::array<std::array<ID3D11ShaderResourceView*,128>,6> srvs{};
    ComPtr<ID3D11SamplerState> sampler,secondarySampler,depthSampler;
    ComPtr<ID3D11DepthStencilState> depth;ComPtr<ID3D11BlendState> blend;ComPtr<ID3D11RasterizerState> raster;
    std::array<ID3D11RenderTargetView*,8> targets{};ComPtr<ID3D11DepthStencilView> dsv;
    std::array<D3D11_VIEWPORT,16> viewports{};std::array<D3D11_RECT,16> scissors{};
    UINT viewportCount=16,scissorCount=16,stride{},offset{},stencil{},sampleMask{};
    FLOAT factors[4]{};D3D11_PRIMITIVE_TOPOLOGY topology{};
    explicit Restore(ID3D11DeviceContext* ctx):c(ctx){
        c->VSGetShader(&vs,vsClasses.data(),&vsClassCount);c->PSGetShader(&ps,psClasses.data(),&psClassCount);
        c->PSGetConstantBuffers(0,1,&pc0);c->PSGetConstantBuffers(1,1,&pc1);
        c->IAGetVertexBuffers(0,1,&vb,&stride,&offset);c->IAGetInputLayout(&layout);c->IAGetPrimitiveTopology(&topology);
        c->PSGetSamplers(0,1,&sampler);c->PSGetSamplers(1,1,&secondarySampler);c->PSGetSamplers(2,1,&depthSampler);
        c->VSGetShaderResources(0,128,srvs[0].data());c->PSGetShaderResources(0,128,srvs[1].data());
        c->GSGetShaderResources(0,128,srvs[2].data());c->HSGetShaderResources(0,128,srvs[3].data());
        c->DSGetShaderResources(0,128,srvs[4].data());c->CSGetShaderResources(0,128,srvs[5].data());
        c->OMGetRenderTargets(8,targets.data(),&dsv);c->OMGetDepthStencilState(&depth,&stencil);
        c->OMGetBlendState(&blend,factors,&sampleMask);c->RSGetState(&raster);
        c->RSGetViewports(&viewportCount,viewports.data());c->RSGetScissorRects(&scissorCount,scissors.data());
    }
    Restore(const Restore&)=delete;
    Restore& operator=(const Restore&)=delete;
    void clearResources(){
        std::array<ID3D11ShaderResourceView*,128> empty{};
        c->VSSetShaderResources(0,128,empty.data());c->PSSetShaderResources(0,128,empty.data());
        c->GSSetShaderResources(0,128,empty.data());c->HSSetShaderResources(0,128,empty.data());
        c->DSSetShaderResources(0,128,empty.data());c->CSSetShaderResources(0,128,empty.data());
    }
    ~Restore(){
        clearResources();c->OMSetRenderTargets(8,targets.data(),dsv.Get());
        c->VSSetShader(vs.Get(),vsClasses.data(),vsClassCount);c->PSSetShader(ps.Get(),psClasses.data(),psClassCount);
        auto* b=pc0.Get();c->PSSetConstantBuffers(0,1,&b);b=pc1.Get();c->PSSetConstantBuffers(1,1,&b);
        b=vb.Get();c->IASetVertexBuffers(0,1,&b,&stride,&offset);c->IASetInputLayout(layout.Get());c->IASetPrimitiveTopology(topology);
        auto* s=sampler.Get();c->PSSetSamplers(0,1,&s);s=secondarySampler.Get();c->PSSetSamplers(1,1,&s);s=depthSampler.Get();c->PSSetSamplers(2,1,&s);
        c->VSSetShaderResources(0,128,srvs[0].data());c->PSSetShaderResources(0,128,srvs[1].data());
        c->GSSetShaderResources(0,128,srvs[2].data());c->HSSetShaderResources(0,128,srvs[3].data());
        c->DSSetShaderResources(0,128,srvs[4].data());c->CSSetShaderResources(0,128,srvs[5].data());
        c->OMSetDepthStencilState(depth.Get(),stencil);c->OMSetBlendState(blend.Get(),factors,sampleMask);
        c->RSSetState(raster.Get());c->RSSetViewports(viewportCount,viewports.data());c->RSSetScissorRects(scissorCount,scissors.data());
        for(auto* v:targets)if(v)v->Release();
        for(const auto& stage:srvs)for(auto* v:stage)if(v)v->Release();
        for(UINT i=0;i<vsClassCount;++i)vsClasses[i]->Release();
        for(UINT i=0;i<psClassCount;++i)psClasses[i]->Release();
    }
};
void noAuxiliaryStages(ID3D11DeviceContext* context,D3D_FEATURE_LEVEL level){
    ComPtr<ID3D11Predicate> predicate;BOOL value{};context->GetPredication(&predicate,&value);
    ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;
    context->GSGetShader(&gs,nullptr,nullptr);context->HSGetShader(&hs,nullptr,nullptr);context->DSGetShader(&ds,nullptr,nullptr);
    std::array<ID3D11Buffer*,4> outputs{};context->SOGetTargets(4,outputs.data());bool stream=false;
    for(auto* output:outputs)if(output){stream=true;output->Release();}
    // FL11_1 extends OM UAV binding through slot63. Every binding must be
    // checked before OMSetRenderTargets can discard an unsupported output.
    std::array<ID3D11UnorderedAccessView*,64> unordered{};
    context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,level>=D3D_FEATURE_LEVEL_11_1?64:8,unordered.data());bool uav=false;
    for(auto* output:unordered)if(output){uav=true;output->Release();}
    need(!predicate&&!gs&&!hs&&!ds&&!stream&&!uav,"Post filter has unqualified predication/additional stages/stream output/UAVs");
}
}

struct PostFilterPipeline {
    ComPtr<ID3D11VertexShader> vertex,texturedVertex;std::array<ComPtr<ID3D11PixelShader>,16> pixels;
    ComPtr<ID3D11InputLayout> layout,texturedLayout;
    ComPtr<ID3D11Texture2D> packed,destination;
    ComPtr<ID3D11RenderTargetView> output;ComPtr<ID3D11ShaderResourceView> sampled;
    UINT width{},height{};
    explicit PostFilterPipeline(ID3D11Device* device){
        check(device->CreateVertexShader(kVSPostAutoTextured,sizeof(kVSPostAutoTextured),nullptr,&vertex),"VS821529C8");
        const D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        check(device->CreateInputLayout(&element,1,kVSPostAutoTextured,sizeof(kVSPostAutoTextured),&layout),"original float2 layout");
        check(device->CreatePixelShader(kPSPostAlphaToRGBADraw,sizeof(kPSPostAlphaToRGBADraw),nullptr,&pixels[0]),"PS821583B8");
        check(device->CreatePixelShader(kPSPostTexturedDraw,sizeof(kPSPostTexturedDraw),nullptr,&pixels[1]),"PS82152708");
        check(device->CreatePixelShader(kPSPostFilteredDraw,sizeof(kPSPostFilteredDraw),nullptr,&pixels[2]),"PS82155F28");
        check(device->CreatePixelShader(kPSPostGlowDraw,sizeof(kPSPostGlowDraw),nullptr,&pixels[3]),"PS82158118");
        check(device->CreateVertexShader(kVSTextured,sizeof(kVSTextured),nullptr,&texturedVertex),"VS82152880");
        const D3D11_INPUT_ELEMENT_DESC elements[]{element,{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}};
        check(device->CreateInputLayout(elements,2,kVSTextured,sizeof(kVSTextured),&texturedLayout),"original float4 XYUV layout");
        check(device->CreatePixelShader(kPSBallEncodeDraw,sizeof(kPSBallEncodeDraw),nullptr,&pixels[4]),"PS82156150");
        check(device->CreatePixelShader(kPSBallCompositeDraw,sizeof(kPSBallCompositeDraw),nullptr,&pixels[5]),"PS82156340");
        check(device->CreatePixelShader(kPSLumaDraw,sizeof(kPSLumaDraw),nullptr,&pixels[6]),"PS821559D8");
        check(device->CreatePixelShader(kPSDofDraw,sizeof(kPSDofDraw),nullptr,&pixels[7]),"PS821517A0");
        check(device->CreatePixelShader(kPSBlurDraw,sizeof(kPSBlurDraw),nullptr,&pixels[8]),"PS82151C50");
        check(device->CreatePixelShader(kPSBloomDraw,sizeof(kPSBloomDraw),nullptr,&pixels[9]),"PS82151DB8");
        check(device->CreatePixelShader(kPSFogLinearDraw,sizeof(kPSFogLinearDraw),nullptr,&pixels[10]),"PS82153E40");
        check(device->CreatePixelShader(kPSFogExpDraw,sizeof(kPSFogExpDraw),nullptr,&pixels[11]),"PS821540A0");
        check(device->CreatePixelShader(kPSFogExp2Draw,sizeof(kPSFogExp2Draw),nullptr,&pixels[12]),"PS82154330");
        check(device->CreatePixelShader(kPSSatDraw,sizeof(kPSSatDraw),nullptr,&pixels[13]),"PS821557C8");
        check(device->CreatePixelShader(kPSFlatEffectDraw,sizeof(kPSFlatEffectDraw),nullptr,&pixels[14]),"PS821524C8");
        check(device->CreatePixelShader(kPSModulatedFlatDraw,sizeof(kPSModulatedFlatDraw),nullptr,&pixels[15]),"PS82152318");
    }
    void surfaces(ID3D11Device* device,UINT w,UINT h){
        if(width==w&&height==h)return;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=desc.ArraySize=1;desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R10G10B10A2_TYPELESS;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> p,d;ComPtr<ID3D11RenderTargetView> r;ComPtr<ID3D11ShaderResourceView> s;
        check(device->CreateTexture2D(&desc,nullptr,&p),"packed output allocation");
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;check(device->CreateTexture2D(&desc,nullptr,&d),"destination allocation");
        D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=DXGI_FORMAT_R10G10B10A2_UINT;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        check(device->CreateRenderTargetView(p.Get(),&rd,&r),"packed RTV");
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R10G10B10A2_UINT;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
        check(device->CreateShaderResourceView(d.Get(),&sd,&s),"destination SRV");
        packed=std::move(p);destination=std::move(d);output=std::move(r);sampled=std::move(s);width=w;height=h;
    }
};

void NativeBackend::drawPostFilter(const std::shared_ptr<RenderTarget>& target,const PostFilterDraw& d,bool submit){
    validateSubmissionContext();validateFrontTarget(target);
    auto validateInput=[&](const std::shared_ptr<RenderTarget>& input){
    need(input&&input!=target&&input->texture&&input->texture!=target->texture&&input->sampledView,
         "Post filter requires a separate owned input target");
    ComPtr<ID3D11Device> sourceDevice;input->texture->GetDevice(&sourceDevice);
    D3D11_TEXTURE2D_DESC sourceDesc{};input->texture->GetDesc(&sourceDesc);
    DXGI_FORMAT format{};
    switch(input->format){
    case TargetFormat::RGB10A2:format=DXGI_FORMAT_R10G10B10A2_UNORM;break;
    case TargetFormat::RGBA8:format=DXGI_FORMAT_R8G8B8A8_UNORM;break;
    case TargetFormat::RGBA16Float:format=DXGI_FORMAT_R16G16B16A16_FLOAT;break;
    case TargetFormat::RGBA32Float:format=DXGI_FORMAT_R32G32B32A32_FLOAT;break;
    default:throw Error("Post filter source format is unqualified");
    }
    need(sourceDevice.Get()==device.Get()&&sourceDesc.Format==format&&sourceDesc.Width==input->pixelWidth()&&
         sourceDesc.Height==input->pixelHeight()&&sourceDesc.Width&&sourceDesc.Height&&sourceDesc.MipLevels==1&&sourceDesc.ArraySize==1&&
         sourceDesc.SampleDesc.Count==1&&!sourceDesc.SampleDesc.Quality&&sourceDesc.Usage==D3D11_USAGE_DEFAULT&&
         sourceDesc.BindFlags==(D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE)&&!sourceDesc.CPUAccessFlags&&!sourceDesc.MiscFlags,
         "Post filter source backing differs from its owner");
    ComPtr<ID3D11Resource> sourceResource;input->sampledView->GetResource(&sourceResource);
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};input->sampledView->GetDesc(&view);
    need(sourceResource.Get()==input->texture.Get()&&view.Format==format&&view.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D&&
         !view.Texture2D.MostDetailedMip&&view.Texture2D.MipLevels==1,"Post filter input view differs");
    };
    UINT shader{};
    switch(d.pixelShader){case 0x821583B8:shader=0;break;case 0x82152708:shader=1;break;
        case 0x82155F28:shader=2;break;case 0x82158118:shader=3;break;case 0x82156150:shader=4;break;case 0x82156340:shader=5;break;
        case 0x821559D8:shader=6;break;case 0x821517A0:shader=7;break;case 0x82151C50:shader=8;break;case 0x82151DB8:shader=9;break;
        case 0x82153E40:shader=10;break;case 0x821540A0:shader=11;break;case 0x82154330:shader=12;break;case 0x821557C8:shader=13;break;
        case 0x821524C8:shader=14;break;case 0x82152318:shader=15;break;
        default:throw Error("Unqualified post filter original shader");}
    // Screen effects: Dof/Fog read the viewport depth copy; Fog, the flat
    // letterbox and the modulated overlay read no stage-0 color.
    const bool fog=shader>=10&&shader<=12,effect=shader>=7,colorless=fog||shader==14||shader==15;
    if(colorless)need(!d.input,"Original colorless pass has a stage-0 color input");else validateInput(d.input);
    if(d.secondaryInput)validateInput(d.secondaryInput);
    need((shader==7||fog)==bool(d.depthInput),"Screen-effect depth stage differs");
    if(d.depthInput){ComPtr<ID3D11Device> depthDevice;need(d.depthInput->texture&&d.depthInput->depthView,"Screen-effect depth input has no sampled view");
        d.depthInput->texture->GetDevice(&depthDevice);need(depthDevice.Get()==device.Get(),"Screen-effect depth input belongs to another device");}
    constexpr std::array<float,6> rectangle{-1,1,1,1,-1,-1},effectRectangle{-1,-1,1,-1,-1,1};
    need((shader==6||shader==7||shader==8||fog||shader==14)==bool(d.strip),"Original strip and shader differ");
    if(d.strip&&shader==14){
        // Letterbox bars: full-width bands (-1,y0),(1,y0),(-1,y1),(1,y1) inside the target.
        const auto& v=*d.strip;
        need(!d.texturedVertices&&v[0]==-1&&v[4]==-1&&v[2]==1&&v[6]==1&&v[1]==v[3]&&v[5]==v[7]&&
             std::isfinite(v[1])&&std::isfinite(v[5])&&v[1]>=-1&&v[1]<=1&&v[5]>=-1&&v[5]<=1,"Original letterbox band corners differ");
    }else if(d.strip){
        need(!d.texturedVertices&&*d.strip==std::array<float,8>{-1,-1,1,-1,-1,1,1,1},"Original strip corners differ");
    }else if(d.texturedVertices){
        const auto& v=*d.texturedVertices;
        for(float value:v)need(std::isfinite(value),"Post XYUV vertex is nonfinite");
        need(std::array<float,6>{v[0],v[1],v[4],v[5],v[8],v[9]}==rectangle,"Original explicit-UV rectangle corners differ");
        need(shader==1||shader==2||shader==4||shader==5,"Post explicit-UV shader is unqualified");
    }else need((shader<4&&d.vertices==rectangle)||((shader==9||shader==13||shader==15)&&d.vertices==effectRectangle),"Original rectangle-list corners differ");
    need((shader==5||shader==9||shader==15)==bool(d.secondaryInput),"Distortion/Bloom/query secondary source differs");
    constexpr std::array<uint32_t,8> blends{0x10001,0x1000B,0x10101,0x1010B,0x10181,0x1018B,0x10701,0x1070B};
    constexpr std::array<uint32_t,3> effectBlends{0x10001,0x10006,0x10706};
    need(d.blendEnable<=1&&d.expandedBlend<=1&&d.colorMask<=15&&
         (effect?std::find(effectBlends.begin(),effectBlends.end(),d.blendWord)!=effectBlends.end():
                 std::find(blends.begin(),blends.end(),d.blendWord)!=blends.end()||(shader==5&&d.blendWord==0x10706)),
         "Post filter blend/color state is unqualified");
    need(!d.depthEnable&&d.depthWrite<=1&&d.depthCompare<=7&&!d.stencilEnable&&!d.alphaTest&&
         !d.cull&&!d.fill&&!d.clipPlaneEnable&&!d.alphaToMask&&!d.depthBiasBits&&!d.slopeBiasBits&&
         d.halfPixelOffset==1&&d.viewportEnable==1&&d.scissorEnable<=1&&d.multisampleAntialias<=1&&
         (d.multisampleMask==0xFFFF||d.multisampleMask==0xFFFFFFFF),"Post filter effective raster/depth state is unqualified");
    need(!d.viewport[0]&&!d.viewport[1]&&d.viewport[2]==target->width&&d.viewport[3]==target->height&&
         ((d.viewport[4]==0&&d.viewport[5]==0x3F800000)||(d.viewport[4]==0x3F800000&&d.viewport[5]==0)),
         "Post filter requires a whole-target viewport");
    if(d.scissorEnable)need(d.scissor[0]<d.scissor[2]&&d.scissor[1]<d.scissor[3]&&
         d.scissor[2]<=target->width&&d.scissor[3]<=target->height,"Post filter scissor exceeds target");
    // Qualify live constant lanes only; dead CPU lanes are retained verbatim.
    if(shader==1||shader==3)for(float value:d.pixelConstants[0])need(std::isfinite(value),"Post filter color is nonfinite");
    if(shader==2||shader==3)for(UINT lane=0;lane<2;++lane)
        need(std::isfinite(d.pixelConstants[shader==2?9:1][lane]),"Post filter tap radius is nonfinite");
    // Luma layers read every color lane and gb.xy of add/sub/lrp.
    if(shader==6)for(UINT layer=0;layer<3;++layer){
        for(float value:d.pixelConstants[2*layer])need(std::isfinite(value),"Luma layer color is nonfinite");
        for(UINT lane=0;lane<2;++lane)need(std::isfinite(d.pixelConstants[2*layer+1][lane]),"Luma layer curve is nonfinite");
    }
    // Screen effects: every live lane of c0..c2 (fog c2.x and linear c1.w are dead).
    // Original Dof computes reciprocal focus distance/range at 82754564 and
    // 827545A8. Zero requests legitimately yield infinity in c1.z/w; its
    // shader preserves the original zero-annihilating multiply. With both
    // distance and range zero, c1.z is infinite and c1.w is 0/0; the original
    // shader saturates that NaN focus to zero. Only this paired DOF case is
    // allowed; other NaNs/effects/lanes retain their finite-only contract.
    if(effect){const UINT rows=(shader==8||shader==14)?1u:(shader==9||fog)?3u:2u;
        for(UINT row=0;row<rows;++row)for(UINT lane=0;lane<4;++lane)
            if(!(fog&&row==2&&lane==0)&&!(shader==10&&row==1&&lane==3)&&!(shader==15&&row==1&&lane>=2))
                if(!std::isfinite(d.pixelConstants[row][lane])&&
                   !(shader==7&&row==1&&lane>=2&&std::isinf(d.pixelConstants[row][lane]))&&
                   !(shader==7&&row==1&&lane==3&&std::isnan(d.pixelConstants[row][lane])&&
                     std::isinf(d.pixelConstants[1][2]))) {
                    uint32_t bits{};std::memcpy(&bits,&d.pixelConstants[row][lane],sizeof(bits));
                    char reason[160];std::snprintf(reason,sizeof(reason),
                        "Screen-effect constant is nonfinite: shader=%08X c%u.%c bits=%08X",
                        d.pixelShader,row,"xyzw"[lane],bits);
                    throw Error(reason);
                }}
    auto validateSampler=[&](const D3D11_SAMPLER_DESC& sm){
    need((sm.Filter==D3D11_FILTER_MIN_MAG_MIP_POINT||sm.Filter==D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR||sm.Filter==D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT||
          sm.Filter==D3D11_FILTER_MIN_MAG_MIP_LINEAR)&&sm.AddressU>=1&&sm.AddressU<=4&&sm.AddressV>=1&&sm.AddressV<=4&&
         sm.AddressW>=1&&sm.AddressW<=4&&sm.MaxAnisotropy==1&&sm.ComparisonFunc==D3D11_COMPARISON_NEVER&&
         std::isfinite(sm.MipLODBias)&&sm.MipLODBias==0&&std::isfinite(sm.MinLOD)&&std::isfinite(sm.MaxLOD)&&sm.MinLOD<=sm.MaxLOD,
         "Post filter sampler is unqualified");
    for(float value:sm.BorderColor)need(std::isfinite(value),"Post filter sampler border is nonfinite");
    };
    if(d.input)validateSampler(d.sampler);if(d.secondaryInput)validateSampler(d.secondarySampler);if(d.depthInput)validateSampler(d.depthSampler);
    flushIm2D();noAuxiliaryStages(context.Get(),device->GetFeatureLevel());
    if(!submit)return;
    if(!postFilterPipeline)postFilterPipeline=std::make_shared<PostFilterPipeline>(device.Get());
    auto& p=*postFilterPipeline;p.surfaces(device.Get(),target->pixelWidth(),target->pixelHeight());
    // RECTLIST implies its fourth corner v1+v2-v0; strip order v0,v1,v2,v3.
    const std::array<float,8> corners{d.vertices[0],d.vertices[1],d.vertices[2],d.vertices[3],d.vertices[4],d.vertices[5],
        d.vertices[2]+d.vertices[4]-d.vertices[0],d.vertices[3]+d.vertices[5]-d.vertices[1]};
    ComPtr<ID3D11Buffer> vb;
    if(d.strip)vb=upload(device.Get(),d.strip->data(),sizeof(*d.strip),D3D11_BIND_VERTEX_BUFFER);
    else if(d.texturedVertices){const auto& v=*d.texturedVertices;
        const std::array<float,16> expanded{v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],v[10],v[11],v[4],v[9],v[6],v[11]};
        vb=upload(device.Get(),expanded.data(),sizeof(expanded),D3D11_BIND_VERTEX_BUFFER);
    }else vb=upload(device.Get(),corners.data(),sizeof(corners),D3D11_BIND_VERTEX_BUFFER);
    auto pc=upload(device.Get(),d.pixelConstants.data(),sizeof(d.pixelConstants),D3D11_BIND_CONSTANT_BUFFER);
    const std::array<uint32_t,4> words{d.blendWord,d.blendEnable,0,0};
    auto dc=upload(device.Get(),words.data(),sizeof(words),D3D11_BIND_CONSTANT_BUFFER);
    ComPtr<ID3D11SamplerState> sampler,secondarySampler,depthSampler;if(d.input)check(device->CreateSamplerState(&d.sampler,&sampler),"sampler");
    if(d.secondaryInput)check(device->CreateSamplerState(&d.secondarySampler,&secondarySampler),"secondary sampler");
    if(d.depthInput)check(device->CreateSamplerState(&d.depthSampler,&depthSampler),"depth sampler");
    D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;dd.DepthFunc=D3D11_COMPARISON_ALWAYS;
    ComPtr<ID3D11DepthStencilState> depth;check(device->CreateDepthStencilState(&dd,&depth),"disabled depth");
    D3D11_BLEND_DESC bd{};bd.RenderTarget[0].BlendEnable=FALSE;
    bd.RenderTarget[0].SrcBlend=D3D11_BLEND_ONE;bd.RenderTarget[0].DestBlend=D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;bd.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO;bd.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask=UINT8(d.colorMask);
    ComPtr<ID3D11BlendState> blend;check(device->CreateBlendState(&bd,&blend),"packed color write mask");
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;rd.ScissorEnable=d.scissorEnable;
    ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster),"raster state");
    {
        Restore restore(context.Get());restore.clearResources();context->OMSetRenderTargets(0,nullptr,nullptr);
        // Preserve masked channels/outside-scissor pixels, and sample the actual
        // pre-draw destination without source quantization before the blend.
        context->CopyResource(p.packed.Get(),target->texture.Get());context->CopyResource(p.destination.Get(),target->texture.Get());
        auto* output=p.output.Get();context->OMSetRenderTargets(1,&output,nullptr);
        auto* vertices=vb.Get();const UINT stride=d.texturedVertices?16u:8u,offset=0;context->IASetVertexBuffers(0,1,&vertices,&stride,&offset);
        context->IASetInputLayout(d.texturedVertices?p.texturedLayout.Get():p.layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context->VSSetShader(d.texturedVertices?p.texturedVertex.Get():p.vertex.Get(),nullptr,0);context->PSSetShader(p.pixels[shader].Get(),nullptr,0);
        auto* constants=pc.Get();context->PSSetConstantBuffers(0,1,&constants);constants=dc.Get();context->PSSetConstantBuffers(1,1,&constants);
        ID3D11ShaderResourceView* views[]{d.input?d.input->sampledView.Get():nullptr,p.sampled.Get()};context->PSSetShaderResources(0,2,views);
        auto* sampling=sampler.Get();context->PSSetSamplers(0,1,&sampling);
        auto* secondary=d.secondaryInput?d.secondaryInput->sampledView.Get():nullptr;context->PSSetShaderResources(2,1,&secondary);
        sampling=secondarySampler.Get();context->PSSetSamplers(1,1,&sampling);
        auto* depthView=d.depthInput?d.depthInput->depthView.Get():nullptr;context->PSSetShaderResources(3,1,&depthView);
        sampling=depthSampler.Get();context->PSSetSamplers(2,1,&sampling);
        context->OMSetDepthStencilState(depth.Get(),0);context->OMSetBlendState(blend.Get(),nullptr,d.multisampleMask);
        context->RSSetState(raster.Get());const auto viewport=renderViewport(target,{0,0,float(target->width),float(target->height),0,1});
        context->RSSetViewports(1,&viewport);
        if(d.scissorEnable){const auto rect=renderScissor(target,{LONG(d.scissor[0]),LONG(d.scissor[1]),LONG(d.scissor[2]),LONG(d.scissor[3])});context->RSSetScissorRects(1,&rect);}
        context->Draw(4,0);
        restore.clearResources();context->OMSetRenderTargets(0,nullptr,nullptr);context->CopyResource(target->texture.Get(),p.packed.Get());
    }
    check(device->GetDeviceRemovedReason(),"draw submission");++postFilterDraws;
    completedOriginalModulatedPostFilterDraw=d.pixelShader==0x82152318?postFilterDraws:0;
    completedOriginalDistortionDraw=d.pixelShader==0x82156340?postFilterDraws:0;
}
void NativeBackend::drawDistortion(const std::shared_ptr<RenderTarget>& target,const DistortionDraw& draw){
    PostFilterDraw d=draw;d.input=draw.inputs[0];d.secondaryInput=draw.inputs[1];
    d.sampler=draw.samplers[0];d.secondarySampler=draw.samplers[1];
    d.texturedVertices.emplace();
    for(size_t i=0;i<3;++i)for(size_t j=0;j<4;++j)(*d.texturedVertices)[i*4+j]=draw.quad[i][j];
    drawPostFilter(target,d);
}

}
