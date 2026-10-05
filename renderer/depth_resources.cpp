#include "native_backend.h"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Simpsons::Graphics {
namespace {
void check(HRESULT result,const char* operation) {
    if(FAILED(result)) {
        char message[160];snprintf(message,sizeof(message),"Native depth %s failed: 0x%08lX",operation,ULONG(result));
        throw Error(message);
    }
}
void sameDevice(ID3D11Resource* resource,ID3D11Device* device) {
    if(!resource) throw Error("Missing native depth resource");
    ComPtr<ID3D11Device> source;resource->GetDevice(&source);
    if(source.Get()!=device) throw Error("Native depth resource belongs to another graphics device");
}
bool exact20e4(float value) {
    if(!std::isfinite(value)||value<0||value>1) return false;
    if(value<std::ldexp(1.0f,-14)) {
        double units=std::ldexp(double(value),34);
        return units==std::floor(units);
    }
    return (std::bit_cast<uint32_t>(value)&7)==0;
}
}
std::shared_ptr<DepthTarget> NativeBackend::createDepthTarget(uint32_t width,uint32_t height,TargetScale scale) {
    requireOwner();
    if(!width||!height||width>16384||height>16384) throw Error("Native depth dimensions exceed D3D11 bounds");
    auto result=std::make_shared<DepthTarget>();result->width=width;result->height=height;
    const auto extent=renderExtent(width,height,scale);renderingAllocated=true;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=extent[0];desc.Height=extent[1];
    result->physicalWidth=desc.Width;result->physicalHeight=desc.Height;
    desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_R32G8X24_TYPELESS;
    desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
    check(device->CreateTexture2D(&desc,nullptr,&result->texture),"allocation");
    tagRenderExtent(result->texture.Get(),width,height);
    D3D11_DEPTH_STENCIL_VIEW_DESC depthDesc{};
    depthDesc.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;depthDesc.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    check(device->CreateDepthStencilView(result->texture.Get(),&depthDesc,&result->view),"DSV creation");
    D3D11_SHADER_RESOURCE_VIEW_DESC sample{};
    sample.Format=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;sample.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
    sample.Texture2D.MipLevels=1;
    check(device->CreateShaderResourceView(result->texture.Get(),&sample,&result->depthView),"depth SRV creation");
    sample.Format=DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
    check(device->CreateShaderResourceView(result->texture.Get(),&sample,&result->stencilView),"stencil SRV creation");
    return result;
}
void NativeBackend::clearDepthTarget(const std::shared_ptr<DepthTarget>& target,float depth,uint8_t stencil,
                          bool clearDepth,bool clearStencil) {
    flushIm2D();
    requireOwner();
    if(!target) throw Error("Missing native depth target");
    sameDevice(target->texture.Get(),device.Get());
    if(!clearDepth && !clearStencil) throw Error("Native depth/stencil clear has no selected components");
    if(clearDepth && !exact20e4(depth)) throw Error("Native depth clear needs an exactly representable 20e4 value; original rounding is unverified");
    if(!std::isfinite(depth)) throw Error("Native depth clear value is non-finite");
    if(depth==0.0f) depth=0.0f; // Normalize -0.0: original format has no signed zero.
    context->ClearDepthStencilView(target->view.Get(),(clearDepth?D3D11_CLEAR_DEPTH:0)|(clearStencil?D3D11_CLEAR_STENCIL:0),
                                  clearDepth?depth:0,stencil);
}
std::vector<uint8_t> NativeBackend::readbackDepthTarget(const std::shared_ptr<DepthTarget>& target) {
    flushIm2D();
    requireOwner();
    if(!target) throw Error("Missing native depth target");
    sameDevice(target->texture.Get(),device.Get());
    D3D11_TEXTURE2D_DESC desc{};target->texture->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&desc,nullptr,&staging),"staging allocation");
    context->CopyResource(staging.Get(),target->texture.Get());
    size_t rowBytes=size_t(target->storageWidth())*8;
    std::vector<uint8_t> result(rowBytes*target->storageHeight());
    D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"staging map");
    if(!mapped.pData) {context->Unmap(staging.Get(),0);throw Error("Staging map is empty");}
    if(mapped.RowPitch<rowBytes) {context->Unmap(staging.Get(),0);throw Error("Staging row pitch is truncated");}
    for(uint32_t row=0;row<target->storageHeight();++row)
        memcpy(result.data()+rowBytes*row,static_cast<const uint8_t*>(mapped.pData)+size_t(mapped.RowPitch)*row,rowBytes);
    context->Unmap(staging.Get(),0);return result;
}
}
