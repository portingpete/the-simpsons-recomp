#include "smaa_pipeline.h"
#include <cstdio>
#include "VSSMAAEdge.h"
#include "PSSMAAEdge.h"
#include "VSSMAAWeight.h"
#include "PSSMAAWeight.h"
#include "VSSMAANeighborhood.h"
#include "PSSMAANeighborhood.h"
#include "vendor/smaa/AreaTex.h"
#include "vendor/smaa/SearchTex.h"

namespace Simpsons::Graphics::detail {
namespace {
void check(HRESULT result,const char* operation) {
    if(FAILED(result)) {
        char message[180];std::snprintf(message,sizeof(message),"Native SMAA %s failed: 0x%08lX",operation,ULONG(result));
        throw Error(message);
    }
}
void lookup(ID3D11Device* device,UINT width,UINT height,DXGI_FORMAT format,const void* bytes,UINT pitch,
            ComPtr<ID3D11Texture2D>& texture,ComPtr<ID3D11ShaderResourceView>& view) {
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;
    desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=format;
    desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data{bytes,pitch,pitch*height};
    check(device->CreateTexture2D(&desc,&data,&texture),"immutable lookup texture");
    check(device->CreateShaderResourceView(texture.Get(),nullptr,&view),"lookup view");
}
ComPtr<ID3D11Texture2D> actualTexture(ID3D11View* view,ID3D11Device* owner,UINT width,UINT height) {
    if(!view)throw Error("Native SMAA lacks a source or output view");
    ComPtr<ID3D11Device> device;view->GetDevice(&device);
    if(device.Get()!=owner)throw Error("Native SMAA view belongs to a different device");
    ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture;check(resource.As(&texture),"front texture interface");
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    if(desc.Width!=width||desc.Height!=height||desc.Format!=DXGI_FORMAT_R10G10B10A2_UNORM||
       desc.MipLevels!=1||desc.ArraySize!=1||desc.SampleDesc.Count!=1||desc.SampleDesc.Quality!=0)
        throw Error("Native SMAA front descriptor differs from its prepared single-sample extent");
    return texture;
}
}
SMAAPipeline::SMAAPipeline(ID3D11Device* owner):device(owner) {
    if(!owner)throw Error("Native SMAA lacks an owning device");
    static_assert(sizeof(areaTexBytes)==AREATEX_SIZE&&sizeof(searchTexBytes)==SEARCHTEX_SIZE);
    lookup(owner,AREATEX_WIDTH,AREATEX_HEIGHT,DXGI_FORMAT_R8G8_UNORM,areaTexBytes,AREATEX_PITCH,areaTexture,areaView);
    lookup(owner,SEARCHTEX_WIDTH,SEARCHTEX_HEIGHT,DXGI_FORMAT_R8_UNORM,searchTexBytes,SEARCHTEX_PITCH,searchTexture,searchView);
    check(owner->CreateVertexShader(kVSSMAAEdge,sizeof(kVSSMAAEdge),nullptr,&edgeVertex),"edge vertex shader");
    check(owner->CreatePixelShader(kPSSMAAEdge,sizeof(kPSSMAAEdge),nullptr,&edgePixel),"edge pixel shader");
    check(owner->CreateVertexShader(kVSSMAAWeight,sizeof(kVSSMAAWeight),nullptr,&weightVertex),"weight vertex shader");
    check(owner->CreatePixelShader(kPSSMAAWeight,sizeof(kPSSMAAWeight),nullptr,&weightPixel),"weight pixel shader");
    check(owner->CreateVertexShader(kVSSMAANeighborhood,sizeof(kVSSMAANeighborhood),nullptr,&neighborhoodVertex),"neighborhood vertex shader");
    check(owner->CreatePixelShader(kPSSMAANeighborhood,sizeof(kPSSMAANeighborhood),nullptr,&neighborhoodPixel),"neighborhood pixel shader");
    D3D11_SAMPLER_DESC sampler{};sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxLOD=D3D11_FLOAT32_MAX;
    sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    check(owner->CreateSamplerState(&sampler,&linearSampler),"linear clamp sampler");
    sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    check(owner->CreateSamplerState(&sampler,&pointSampler),"point clamp sampler");
}
void SMAAPipeline::prepare(UINT newWidth,UINT newHeight) {
    if(!newWidth||!newHeight||newWidth>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION||newHeight>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        throw Error("Native SMAA extent is invalid");
    if(width==newWidth&&height==newHeight)return;
    const auto surface=[&](DXGI_FORMAT format) {
        Surface result;D3D11_TEXTURE2D_DESC desc{};desc.Width=newWidth;desc.Height=newHeight;
        desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=format;
        desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        check(device->CreateTexture2D(&desc,nullptr,&result.texture),"intermediate texture");
        check(device->CreateRenderTargetView(result.texture.Get(),nullptr,&result.target),"intermediate target");
        check(device->CreateShaderResourceView(result.texture.Get(),nullptr,&result.sampled),"intermediate view");
        return result;
    };
    auto newEdges=surface(DXGI_FORMAT_R8G8_UNORM),newWeights=surface(DXGI_FORMAT_R8G8B8A8_UNORM);
    const std::array<float,4> values{1.0f/float(newWidth),1.0f/float(newHeight),float(newWidth),float(newHeight)};
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(values);desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    const D3D11_SUBRESOURCE_DATA data{values.data(),0,0};ComPtr<ID3D11Buffer> newMetrics;
    check(device->CreateBuffer(&desc,&data,&newMetrics),"immutable pixel metrics");
    edges=std::move(newEdges);weights=std::move(newWeights);metrics=std::move(newMetrics);width=newWidth;height=newHeight;
}
void SMAAPipeline::draw(ID3D11DeviceContext* context,ID3D11ShaderResourceView* source,ID3D11RenderTargetView* output) {
    if(!context||!metrics)throw Error("Native SMAA lacks a context or prepared extent");
    ComPtr<ID3D11Device> owner;context->GetDevice(&owner);
    if(owner.Get()!=device.Get()||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)
        throw Error("Native SMAA context differs from its owning immediate device");
    const auto input=actualTexture(source,device.Get(),width,height),target=actualTexture(output,device.Get(),width,height);
    if(input.Get()==target.Get())throw Error("Native SMAA source aliases its output");
    D3D11_SHADER_RESOURCE_VIEW_DESC sampled{};source->GetDesc(&sampled);
    D3D11_RENDER_TARGET_VIEW_DESC rendered{};output->GetDesc(&rendered);
    if(sampled.Format!=DXGI_FORMAT_R10G10B10A2_UNORM||sampled.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||
       sampled.Texture2D.MostDetailedMip!=0||sampled.Texture2D.MipLevels!=1||
       rendered.Format!=DXGI_FORMAT_R10G10B10A2_UNORM||rendered.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D||rendered.Texture2D.MipSlice!=0)
        throw Error("Native SMAA front view descriptor is invalid");
    const std::array<ID3D11ShaderResourceView*,3> empty{};
    context->PSSetShaderResources(0,UINT(empty.size()),empty.data());context->OMSetRenderTargets(0,nullptr,nullptr);
    // Discarded non-edge pixels and all four blend channels must begin at zero
    // each frame, including when these targets are reused after a different scene.
    const float clear[4]{};context->ClearRenderTargetView(edges.target.Get(),clear);context->ClearRenderTargetView(weights.target.Get(),clear);
    const std::array<ID3D11SamplerState*,2> samplers{linearSampler.Get(),pointSampler.Get()};
    context->PSSetSamplers(0,UINT(samplers.size()),samplers.data());
    context->VSSetConstantBuffers(0,1,metrics.GetAddressOf());context->PSSetConstantBuffers(0,1,metrics.GetAddressOf());
    const auto pass=[&](ID3D11VertexShader* vertex,ID3D11PixelShader* pixel,ID3D11RenderTargetView* destination,
                        const std::array<ID3D11ShaderResourceView*,3>& inputs) {
        context->PSSetShaderResources(0,UINT(empty.size()),empty.data());
        context->OMSetRenderTargets(1,&destination,nullptr);
        context->VSSetShader(vertex,nullptr,0);context->PSSetShader(pixel,nullptr,0);
        context->PSSetShaderResources(0,UINT(inputs.size()),inputs.data());context->Draw(3,0);
    };
    pass(edgeVertex.Get(),edgePixel.Get(),edges.target.Get(),{source,nullptr,nullptr});
    pass(weightVertex.Get(),weightPixel.Get(),weights.target.Get(),{edges.sampled.Get(),areaView.Get(),searchView.Get()});
    pass(neighborhoodVertex.Get(),neighborhoodPixel.Get(),output,{source,weights.sampled.Get(),nullptr});
    context->PSSetShaderResources(0,UINT(empty.size()),empty.data());context->OMSetRenderTargets(0,nullptr,nullptr);
}
}
