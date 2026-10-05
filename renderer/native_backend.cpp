#include "native_backend.h"
#include "device_availability.h"
#include "presentation_window.h"
#include "d3d_call_stats.h"
#include <dxgi1_5.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <bit>

namespace Simpsons::Graphics {
namespace {
void check(HRESULT result,const char* operation) {
    if(FAILED(result)) {
        char message[160];snprintf(message,sizeof(message),"Native D3D11 %s failed: 0x%08lX",operation,ULONG(result));
        throw Error(message);
    }
}
struct Layout {DXGI_FORMAT format;uint32_t rowBytes,rows;};
Layout layout(uint32_t width,uint32_t height,TextureFormat format,bool mip=false) {
    if(!width || !height || width>16384 || height>16384) throw Error("Native texture dimensions exceed D3D11 bounds");
    if(format==TextureFormat::RGBA8) return {DXGI_FORMAT_R8G8B8A8_UNORM,width*4,height};
    if(format==TextureFormat::BGRX8) return {DXGI_FORMAT_B8G8R8X8_UNORM,width*4,height};
    // Native scalar red UNORM; no channel replication or color conversion.
    // https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format
    if(format==TextureFormat::R8) return {DXGI_FORMAT_R8_UNORM,width,height};
    if(!mip && ((width&3) || (height&3))) throw Error("Unverified compressed texture edge layout");
    switch(format) {
    case TextureFormat::BC1:return {DXGI_FORMAT_BC1_UNORM,((width+3)/4)*8,(height+3)/4};
    case TextureFormat::BC2:return {DXGI_FORMAT_BC2_UNORM,((width+3)/4)*16,(height+3)/4};
    case TextureFormat::BC3:return {DXGI_FORMAT_BC3_UNORM,((width+3)/4)*16,(height+3)/4};
    default:throw Error("Unsupported native texture format");
    }
}
}
NativeBackend::NativeBackend(bool software,bool vsync):owner(GetCurrentThreadId()),vsyncEnabled(vsync) {
    D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
    D3D_DRIVER_TYPE driver=software?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_HARDWARE;
    HRESULT result=D3D11CreateDevice(nullptr,driver,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,2,D3D11_SDK_VERSION,&device,&featureLevel,&context);
    if(result==E_INVALIDARG)
        result=D3D11CreateDevice(nullptr,driver,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels+1,1,D3D11_SDK_VERSION,&device,&featureLevel,&context);
    check(result,"device creation");
    if(d3dCallStatsRequested())installD3DCallStats(context.Get(),device.Get());
    availability=std::make_shared<DeviceAvailability>(device.Get());
    fprintf(stderr,"[NATIVE GRAPHICS] D3D11 %s feature_level=0x%X; no console command processor\n",software?"WARP":"hardware",unsigned(featureLevel));
}
void NativeBackend::requireOwner() const {
    if(GetCurrentThreadId()!=owner) throw Error("Native graphics immediate context used from a different thread");
    availability->require();
}
void NativeBackend::validateSubmissionContext() const {
    requireOwner();
    if(!context || !device)
        throw Error("Native engine requires its actual D3D11 immediate submission context");
    // Fast path: identical strong-identity pair already proven. Strong refs
    // pin both objects, so pointer equality cannot be a recycled ABA value.
    // Device-availability/owner checks above and below are always retained,
    // and test-probe replacement of context/device forces a pointer change.
    if(validatedSubmissionContext.Get()==context.Get() && validatedSubmissionDevice.Get()==device.Get() &&
       validatedSubmissionContext && validatedSubmissionDevice) {
        requireOwner();
        return;
    }
    if(!context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)
        throw Error("Native engine requires its actual D3D11 immediate submission context");
    ComPtr<ID3D11Device> actual;context->GetDevice(&actual);
    if(!device || actual.Get()!=device.Get()) throw Error("Native submission context belongs to another device");
    requireOwner();
    // Publish proof only after the full checks succeed.
    validatedSubmissionContext=context;
    validatedSubmissionDevice=device;
}
void NativeBackend::attachWindow(HWND window,uint32_t width,uint32_t height) {
    validateSubmissionContext();
    if(swapChain || !window || !IsWindow(window) || !width || !height || width>16384 || height>16384)
        throw Error("Invalid or repeated native presentation target attachment");
    if(!presentationWindowMatches(window,width,height))
        throw Error("Native presentation window ownership or client extent differs");
    UINT support{};
    check(device->CheckFormatSupport(DXGI_FORMAT_R10G10B10A2_UNORM,&support),"RGB10A2 presentation support query");
    constexpr UINT required=D3D11_FORMAT_SUPPORT_TEXTURE2D|D3D11_FORMAT_SUPPORT_RENDER_TARGET|D3D11_FORMAT_SUPPORT_DISPLAY;
    if((support&required)!=required) throw Error("Native RGB10A2 presentation format is unavailable; no 8-bit fallback");
    ComPtr<IDXGIDevice> dxgiDevice;check(device.As(&dxgiDevice),"DXGI interface query");
    ComPtr<IDXGIAdapter> adapter;check(dxgiDevice->GetAdapter(&adapter),"adapter query");
    ComPtr<IDXGIFactory2> factory;check(adapter->GetParent(IID_PPV_ARGS(&factory)),"factory query");
    DXGI_SWAP_CHAIN_DESC1 desc{};
    const auto extent=renderExtent(width,height,TargetScale::Scene);
    const uint32_t samples=antialiasingMode==Antialiasing::SSAA4x?2:1;
    const auto canvas=presentationExtent(extent[0]/samples,extent[1]/samples,width,height,width,height);
    desc.Width=canvas[0];desc.Height=canvas[1];desc.Format=DXGI_FORMAT_R10G10B10A2_UNORM;
    desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling=DXGI_SCALING_STRETCH;desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    bool useTearing=false;
    {
        ComPtr<IDXGIFactory5> factory5;
        if(SUCCEEDED(factory.As(&factory5))) {
            BOOL allowTearing=FALSE;
            const HRESULT supportResult=factory5->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING,&allowTearing,sizeof(allowTearing));
            useTearing=SUCCEEDED(supportResult) && allowTearing==TRUE;
        }
        if(useTearing)desc.Flags=DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }
    ComPtr<IDXGISwapChain1> created;
    check(factory->CreateSwapChainForHwnd(device.Get(),window,&desc,nullptr,nullptr,&created),"RGB10A2 swap chain creation (no format fallback)");
    // Explicit native SDR interpretation, not a recovered Xenos gamma policy.
    // G22/P709 denotes the sRGB transfer curve, not a shader-side pow(2.2).
    // Packed resource copies remain untouched; Windows still owns composition,
    // display conversion and scanout. This does not establish their equivalence.
    // https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiswapchain3-setcolorspace1
    ComPtr<IDXGISwapChain3> colorChain;check(created.As(&colorChain),"explicit SDR color-space interface");
    UINT colorSupport{};
    check(colorChain->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,&colorSupport),"SDR color-space support query");
    if(!(colorSupport&DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
        throw Error("Native full-range SDR G22/P709 presentation is unavailable");
    if(colorChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)!=S_OK)
        throw Error("Native full-range SDR G22/P709 color-space selection failed");
    check(factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER),"window association");
    ComPtr<ID3D11Texture2D> target;check(created->GetBuffer(0,IID_PPV_ARGS(&target)),"backbuffer query");
    ComPtr<ID3D11RenderTargetView> view;check(device->CreateRenderTargetView(target.Get(),nullptr,&view),"backbuffer view creation");
    swapChain=std::move(created);backbuffer=std::move(view);
    presentationSourceWidth=extent[0]/samples;presentationSourceHeight=extent[1]/samples;
    presentationLogicalWidth=width;presentationLogicalHeight=height;
    tearingEnabled=useTearing;
    if(!vsyncEnabled && !tearingEnabled)
        fprintf(stderr,"[NATIVE PRESENT] DXGI tearing unavailable; Vsync off via Present(0,0)\n");
}
std::shared_ptr<Texture> NativeBackend::createTexture(uint32_t width,uint32_t height,TextureFormat format,std::span<const uint8_t> bytes) {
    if(format==TextureFormat::RGBA8 || format==TextureFormat::BGRX8) {
        const std::array levels{bytes};return createTextureMipChain(width,height,format,levels);
    }
    requireOwner();
    auto storage=layout(width,height,format);
    if(uint64_t(storage.rowBytes)*storage.rows!=bytes.size()) throw Error("Native texture input byte count mismatch");
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=width;desc.Height=height;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=storage.format;desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA source{bytes.data(),storage.rowBytes,UINT(bytes.size())};
    auto result=std::make_shared<Texture>();
    result->width=width;result->height=height;result->format=format;
    result->rowBytes=storage.rowBytes;result->rows=storage.rows;
    check(device->CreateTexture2D(&desc,&source,&result->texture),"texture upload");
    check(device->CreateShaderResourceView(result->texture.Get(),nullptr,&result->view),"texture view creation");
    return result;
}
std::shared_ptr<Texture> NativeBackend::createTextureMipChain(uint32_t width,uint32_t height,TextureFormat format,
    std::span<const std::span<const uint8_t>> levels) {
    validateSubmissionContext();
    if(format!=TextureFormat::RGBA8 && format!=TextureFormat::BGRX8 &&
       format!=TextureFormat::BC1 && format!=TextureFormat::BC2 && format!=TextureFormat::BC3)
        throw Error("Native mip chains support only RGBA8/BGRX8/BC1/BC2/BC3");
    const auto storage=layout(width,height,format);
    const auto maximum=std::bit_width(std::max(width,height));
    if(levels.empty() || levels.size()>maximum) throw Error("Invalid native texture mip level count");
    std::vector<D3D11_SUBRESOURCE_DATA> sources;sources.reserve(levels.size());
    for(uint32_t i=0;i<levels.size();++i) {
        const auto mip=layout(std::max(1u,width>>i),std::max(1u,height>>i),format,i!=0);
        if(uint64_t(mip.rowBytes)*mip.rows!=levels[i].size())throw Error("Native texture input byte count mismatch for mip level");
        sources.push_back({levels[i].data(),mip.rowBytes,UINT(levels[i].size())});
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=width;desc.Height=height;desc.MipLevels=UINT(levels.size());desc.ArraySize=1;
    desc.Format=storage.format;desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    auto result=std::make_shared<Texture>();result->width=width;result->height=height;
    result->format=format;result->rowBytes=storage.rowBytes;result->rows=storage.rows;result->mipLevels=desc.MipLevels;
    check(device->CreateTexture2D(&desc,sources.data(),&result->texture),"explicit mip chain upload");
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=desc.Format;view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
    view.Texture2D.MipLevels=desc.MipLevels;
    check(device->CreateShaderResourceView(result->texture.Get(),&view,&result->view),"explicit mip chain view creation");
    validateTexture(result);return result;
}
std::shared_ptr<Texture> NativeBackend::createWritableTexture(uint32_t width,uint32_t height,TextureFormat format) {
    requireOwner();
    if(format!=TextureFormat::RGBA8 && format!=TextureFormat::R8) throw Error("Native writable textures support only RGBA8/R8");
    const auto storage=layout(width,height,format);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=width;desc.Height=height;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=storage.format;desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    auto result=std::make_shared<Texture>();
    result->width=width;result->height=height;result->format=format;
    result->rowBytes=storage.rowBytes;result->rows=storage.rows;result->writable=true;
    // No initial data or clear: original CPU writers supply the first pixels.
    check(device->CreateTexture2D(&desc,nullptr,&result->texture),"writable texture allocation");
    check(device->CreateShaderResourceView(result->texture.Get(),nullptr,&result->view),"writable texture view creation");
    return result;
}
void NativeBackend::writeTexture(const std::shared_ptr<Texture>& texture,std::span<const uint8_t> bytes) {
    validateTextureStorage(texture);
    if(!texture->writable) throw Error("Native texture upload requires writable storage");
    if(uint64_t(texture->rowBytes)*texture->rows!=bytes.size()) throw Error("Native texture upload byte count mismatch");
    // The immediate-context API snapshots source data before returning, including
    // when it must stage a second GPU copy. No guest pitch/padding is accepted.
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-updatesubresource
    context->UpdateSubresource(texture->texture.Get(),0,nullptr,bytes.data(),texture->rowBytes,0);
    requireOwner();
}
void NativeBackend::validateTexture(const std::shared_ptr<Texture>& texture) const {
    validateTextureStorage(texture);
    if(texture->format==TextureFormat::R8) throw Error("Native R8 storage is unsupported by existing pipelines");
}
void NativeBackend::validateTextureStorage(const std::shared_ptr<Texture>& texture) const {
    requireOwner();
    if(!texture || !texture->texture || !texture->view) throw Error("Missing native texture resource/view");
    const auto& proof=texture->storageProof;
    if(proof.device && proof.device==device.Get() && proof.texture.Get()==texture->texture.Get() && proof.view.Get()==texture->view.Get() &&
       proof.width==texture->width && proof.height==texture->height && proof.rowBytes==texture->rowBytes && proof.rows==texture->rows &&
       proof.mipLevels==texture->mipLevels && proof.format==texture->format && proof.writable==texture->writable)
        return;
    ComPtr<ID3D11Device> sourceDevice;texture->texture->GetDevice(&sourceDevice);
    if(sourceDevice.Get()!=device.Get()) throw Error("Native resource belongs to another graphics device");
    const auto storage=layout(texture->width,texture->height,texture->format);
    if(texture->writable && texture->format!=TextureFormat::RGBA8 && texture->format!=TextureFormat::R8)
        throw Error("Native writable textures support only RGBA8/R8");
    D3D11_TEXTURE2D_DESC actual{};texture->texture->GetDesc(&actual);
    if(actual.Width!=texture->width || actual.Height!=texture->height || actual.Format!=storage.format ||
       !texture->mipLevels || texture->mipLevels>std::bit_width(std::max(texture->width,texture->height)) ||
       ((texture->writable || texture->format==TextureFormat::R8) && texture->mipLevels!=1) ||
       actual.MipLevels!=texture->mipLevels || actual.ArraySize!=1 || actual.SampleDesc.Count!=1 || actual.SampleDesc.Quality ||
       actual.Usage!=(texture->writable?D3D11_USAGE_DEFAULT:D3D11_USAGE_IMMUTABLE) ||
       actual.BindFlags!=D3D11_BIND_SHADER_RESOURCE || actual.CPUAccessFlags || actual.MiscFlags ||
       texture->rowBytes!=storage.rowBytes || texture->rows!=storage.rows)
        throw Error("Native texture metadata does not match its backing");
    ComPtr<ID3D11Resource> viewed;texture->view->GetResource(&viewed);
    if(viewed.Get()!=texture->texture.Get()) throw Error("Native texture view belongs to another resource");
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};texture->view->GetDesc(&view);
    if(view.Format!=storage.format || view.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D ||
       view.Texture2D.MostDetailedMip || view.Texture2D.MipLevels!=texture->mipLevels)
        throw Error("Native texture view does not match its backing");
    // Publish the proof only after the full check succeeded.
    texture->storageProof={texture->texture,texture->view,device.Get(),texture->width,texture->height,
        texture->rowBytes,texture->rows,texture->mipLevels,texture->format,texture->writable};
}
std::vector<uint8_t> NativeBackend::readback(const std::shared_ptr<Texture>& texture) {
    return readbackMip(texture,0);
}
std::vector<uint8_t> NativeBackend::readbackMip(const std::shared_ptr<Texture>& texture,uint32_t level) {
    flushIm2D();
    validateTextureStorage(texture);
    if(level>=texture->mipLevels)throw Error("Native texture readback mip level is out of range");
    const auto storage=layout(std::max(1u,texture->width>>level),std::max(1u,texture->height>>level),texture->format,level!=0);
    D3D11_TEXTURE2D_DESC desc{};texture->texture->GetDesc(&desc);
    // Preserve the source extent and mip count. A 1x1/2x2 BC subresource is
    // valid within its original chain, but not as a standalone BC texture.
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&desc,nullptr,&staging),"readback allocation");
    context->CopySubresourceRegion(staging.Get(),level,0,0,0,texture->texture.Get(),level,nullptr);
    std::vector<uint8_t> result(size_t(storage.rowBytes)*storage.rows);
    D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),level,D3D11_MAP_READ,0,&mapped),"readback map");
    if(!mapped.pData) {context->Unmap(staging.Get(),level);throw Error("Native mip readback mapped null data");}
    if(mapped.RowPitch<storage.rowBytes) {context->Unmap(staging.Get(),level);throw Error("Native mip readback pitch is shorter than a logical row");}
    for(uint32_t row=0;row<storage.rows;++row)
        memcpy(result.data()+size_t(row)*storage.rowBytes,static_cast<const uint8_t*>(mapped.pData)+size_t(row)*mapped.RowPitch,storage.rowBytes);
    context->Unmap(staging.Get(),level);
    return result;
}
std::shared_ptr<Buffer> NativeBackend::createBuffer(uint32_t size,BufferKind kind) {
    requireOwner();
    if(!size || size>0x4000000) throw Error("Native buffer size is outside the bounded engine resource profile");
    UINT binding;
    switch(kind) {
    case BufferKind::Vertex:binding=D3D11_BIND_VERTEX_BUFFER;break;
    case BufferKind::Index16:
        if(size&1) throw Error("Native 16-bit index buffer has a partial element");
        binding=D3D11_BIND_INDEX_BUFFER;break;
    default:throw Error("Unsupported native buffer kind");
    }
    auto result=std::make_shared<Buffer>();result->size=size;result->kind=kind;
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=size;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=binding;
    check(device->CreateBuffer(&desc,nullptr,&result->buffer),"engine buffer allocation");
    return result;
}
void NativeBackend::validateBufferWrite(const std::shared_ptr<Buffer>& buffer,uint32_t offset,std::span<const uint8_t> bytes) const {
    requireOwner();
    if(!buffer || !buffer->buffer) throw Error("Missing native buffer upload resource");
    if(!provenBufferDescriptor(buffer->buffer.Get(),buffer->deviceProof)) throw Error("Native buffer belongs to another graphics device");
    if(uint64_t(offset)+bytes.size()>buffer->size) throw Error("Native buffer upload exceeds its resource");
}
void NativeBackend::writeBuffer(const std::shared_ptr<Buffer>& buffer,uint32_t offset,std::span<const uint8_t> bytes) {
    validateBufferWrite(buffer,offset,bytes);
    flushIm2DBufferWrites();
    uploadBuffer(buffer->buffer.Get(),offset,bytes);
}
void NativeBackend::queueIm2DBufferWrite(const std::shared_ptr<Buffer>& buffer,uint32_t offset,std::span<const uint8_t> bytes) {
    validateBufferWrite(buffer,offset,bytes);
    constexpr size_t capacity=0x40000;
    if(buffer->kind!=BufferKind::Vertex || buffer->size>capacity)
        throw Error("Queued Im2D upload requires bounded original vertex storage");
    if(bytes.empty())return;
    const bool compatible=pendingIm2DBuffer.Get()==buffer->buffer.Get() &&
        uint64_t(pendingIm2DBufferOffset)+pendingIm2DBufferBytes.size()==offset &&
        pendingIm2DBufferBytes.size()+bytes.size()<=capacity;
    if(!compatible)flushIm2DBufferWrites();
    // Allocation and byte ownership precede publication. Keep the real COM
    // identity, so replacing the caller's Buffer wrapper cannot redirect a write.
    pendingIm2DBufferBytes.reserve(capacity);
    pendingIm2DBufferBytes.insert(pendingIm2DBufferBytes.end(),bytes.begin(),bytes.end());
    if(!pendingIm2DBuffer) {
        pendingIm2DBuffer=buffer->buffer;pendingIm2DBufferOffset=offset;
    }
}
void NativeBackend::flushIm2DBufferWrites() {
    if(!pendingIm2DBuffer)return;
    requireOwner();
    uploadBuffer(pendingIm2DBuffer.Get(),pendingIm2DBufferOffset,pendingIm2DBufferBytes);
    pendingIm2DBuffer.Reset();pendingIm2DBufferBytes.clear();
}
void NativeBackend::uploadBuffer(ID3D11Buffer* buffer,uint32_t offset,std::span<const uint8_t> bytes) {
    if(bytes.empty()) return;
    constexpr UINT capacity=1024*1024;
    if(bytes.size()>capacity) {
        D3D11_BOX range{offset,0,0,UINT(offset+bytes.size()),1,1};
        context->UpdateSubresource(buffer,0,&range,bytes.data(),0,0);++bufferUploads;
        return;
    }
    if(!uploadRing) {
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=capacity;desc.Usage=D3D11_USAGE_DYNAMIC;
        desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        check(device->CreateBuffer(&desc,nullptr,&uploadRing),"native buffer upload ring allocation");
    }
    UINT start=(uploadCursor+15u)&~15u;
    if(uint64_t(start)+bytes.size()>capacity)start=0;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(uploadRing.Get(),0,start?D3D11_MAP_WRITE_NO_OVERWRITE:D3D11_MAP_WRITE_DISCARD,0,&mapped),"native buffer upload ring map");
    if(!mapped.pData) throw Error("Native buffer upload mapping is empty");
    if(bytes.empty()) throw Error("Native buffer upload is empty");
    memcpy(static_cast<uint8_t*>(mapped.pData)+start,bytes.data(),bytes.size());
    context->Unmap(uploadRing.Get(),0);
    // Only fresh ring bytes are written. DISCARD on wrap retains all queued
    // source generations; the GPU copies the exact range into the original
    // DEFAULT buffer in order. Its untouched bytes and native identity survive.
    // No guest fence assumption, CPU pointer retention or skipped upload.
    D3D11_BOX range{start,0,0,UINT(start+bytes.size()),1,1};
    context->CopySubresourceRegion(buffer,0,offset,0,0,uploadRing.Get(),0,&range);++bufferUploads;
    uploadCursor=UINT(start+bytes.size());
}
std::vector<uint8_t> NativeBackend::readbackBuffer(const std::shared_ptr<Buffer>& buffer) {
    flushIm2D();
    requireOwner();
    if(!buffer || !buffer->buffer) throw Error("Missing native buffer readback resource");
    ComPtr<ID3D11Device> source;buffer->buffer->GetDevice(&source);
    if(source.Get()!=device.Get()) throw Error("Native buffer belongs to another graphics device");
    D3D11_BUFFER_DESC desc{};buffer->buffer->GetDesc(&desc);
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;check(device->CreateBuffer(&desc,nullptr,&staging),"buffer readback allocation");
    context->CopyResource(staging.Get(),buffer->buffer.Get());
    std::vector<uint8_t> result(buffer->size);
    D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"buffer readback map");
    if(!mapped.pData) {context->Unmap(staging.Get(),0);throw Error("Buffer readback mapping is empty");}
    if(result.empty()) {context->Unmap(staging.Get(),0);throw Error("Buffer readback size is empty");}
    memcpy(result.data(),mapped.pData,result.size());context->Unmap(staging.Get(),0);
    return result;
}
void NativeBackend::clear(const std::array<float,4>& color) {
    flushIm2D();
    requireOwner();
    if(!backbuffer) throw Error("Native clear lacks a presentation target");
    for(float value:color) if(!std::isfinite(value)) throw Error("Nonfinite native clear color");
    context->ClearRenderTargetView(backbuffer.Get(),color.data());
}
bool NativeBackend::present() {
    flushIm2D();
    requireOwner();
    if(!swapChain) throw Error("Native present lacks a presentation target");
    const UINT interval=vsyncEnabled?1u:0u;
    const UINT flags=tearingEnabled&&!vsyncEnabled?DXGI_PRESENT_ALLOW_TEARING:0u;
    HRESULT result=swapChain->Present(interval,flags);
    check(result,"presentation");
    requireOwner();
    if(result==DXGI_STATUS_OCCLUDED) return false;
    if(result!=S_OK) throw Error("Native presentation returned an unsupported display status");
    ++presentations;
    return true;
}
void NativeBackend::resetSceneStateCacheForDevice() {
    sceneCacheDevice=device;
    for(auto& entry:sceneDepthStates) entry.Reset();
    for(auto& entry:sceneRasterStates) entry.Reset();
    for(auto& entry:sceneBlends) { entry.used=false; entry.state.Reset(); }
    sceneBlendNext=0;
    for(auto& entry:sceneSamplers) { entry.used=false; entry.state.Reset(); }
    sceneSamplerNext=0;
    for(auto& entry:sceneDepthConstants) { entry.used=false; entry.buffer.Reset(); }
    sceneDepthConstantNext=0;
}
ComPtr<ID3D11DepthStencilState> NativeBackend::sceneDepthState(uint32_t enable,uint32_t write,uint32_t compare) {
    requireOwner();
    if(!device || !context) throw Error("Native scene state cache requires its D3D device");
    if(enable>1 || write>1 || compare>7) throw Error("Native scene depth state is not canonical");
    if(sceneCacheDevice.Get()!=device.Get()) resetSceneStateCacheForDevice();
    const size_t index=(enable?16u:0u)|(write?8u:0u)|compare;
    if(!sceneDepthStates[index]) {
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable=enable;
        dd.DepthWriteMask=write?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;
        dd.DepthFunc=D3D11_COMPARISON_FUNC(compare+1);
        dd.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};
        dd.BackFace=dd.FrontFace;
        check(device->CreateDepthStencilState(&dd,&sceneDepthStates[index]),"scene depth state creation");
    }
    return sceneDepthStates[index];
}
ComPtr<ID3D11RasterizerState> NativeBackend::sceneRasterState(uint32_t cull,uint32_t scissorEnable) {
    requireOwner();
    if(!device || !context) throw Error("Native scene state cache requires its D3D device");
    if(!((cull==0||cull==2||cull==6)) || scissorEnable>1) throw Error("Native scene raster state is unqualified");
    if(sceneCacheDevice.Get()!=device.Get()) resetSceneStateCacheForDevice();
    const size_t index=(scissorEnable?3u:0u)+(cull==0?0u:(cull==2?1u:2u));
    if(!sceneRasterStates[index]) {
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_SOLID;
        rd.CullMode=cull==0?D3D11_CULL_NONE:D3D11_CULL_BACK;
        rd.FrontCounterClockwise=cull==2; rd.ScissorEnable=scissorEnable; rd.DepthClipEnable=TRUE;
        check(device->CreateRasterizerState(&rd,&sceneRasterStates[index]),"scene raster state creation");
    }
    return sceneRasterStates[index];
}
ComPtr<ID3D11BlendState> NativeBackend::sceneBlendState(uint32_t enable,uint32_t word,uint32_t mask) {
    requireOwner();
    if(!device || !context) throw Error("Native scene state cache requires its D3D device");
    if(mask>15 || enable>1) throw Error("Native scene blend state is unqualified");
    if(sceneCacheDevice.Get()!=device.Get()) resetSceneStateCacheForDevice();
    for(auto& entry:sceneBlends) {
        if(entry.used && entry.enable==enable && entry.word==word && entry.mask==mask) return entry.state;
    }
    D3D11_BLEND_DESC bd{}; auto& color=bd.RenderTarget[0]; color.RenderTargetWriteMask=UINT8(mask);
    color.BlendEnable=enable;
    if(word==0x07060706) { color.SrcBlend=color.SrcBlendAlpha=D3D11_BLEND_SRC_ALPHA; color.DestBlend=color.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA; }
    else { color.SrcBlend=color.SrcBlendAlpha=D3D11_BLEND_ONE; color.DestBlend=color.DestBlendAlpha=D3D11_BLEND_ZERO; }
    color.BlendOp=color.BlendOpAlpha=D3D11_BLEND_OP_ADD;
    ComPtr<ID3D11BlendState> created;
    check(device->CreateBlendState(&bd,&created),"scene blend state creation");
    auto& slot=sceneBlends[sceneBlendNext]; sceneBlendNext=(sceneBlendNext+1)%sceneBlends.size();
    slot.used=true; slot.enable=enable; slot.word=word; slot.mask=mask; slot.state=created;
    return created;
}
ComPtr<ID3D11SamplerState> NativeBackend::sceneSamplerState(const D3D11_SAMPLER_DESC& desc) {
    requireOwner();
    if(!device || !context) throw Error("Native scene state cache requires its D3D device");
    if(sceneCacheDevice.Get()!=device.Get()) resetSceneStateCacheForDevice();
    for(auto& entry:sceneSamplers) {
        if(entry.used && std::memcmp(&entry.desc,&desc,sizeof(desc))==0) return entry.state;
    }
    ComPtr<ID3D11SamplerState> created;
    check(device->CreateSamplerState(&desc,&created),"scene sampler creation");
    auto& slot=sceneSamplers[sceneSamplerNext]; sceneSamplerNext=(sceneSamplerNext+1)%sceneSamplers.size();
    slot.used=true; slot.desc=desc; slot.state=created;
    return created;
}
ComPtr<ID3D11Buffer> NativeBackend::sceneDepthConstantBuffer(uint32_t reverse,uint32_t constantBits,uint32_t slopeBits) {
    requireOwner();
    if(!device || !context) throw Error("Native scene state cache requires its D3D device");
    if(sceneCacheDevice.Get()!=device.Get()) resetSceneStateCacheForDevice();
    for(auto& entry:sceneDepthConstants) {
        if(entry.used && entry.reverse==reverse && entry.constantBits==constantBits && entry.slopeBits==slopeBits) return entry.buffer;
    }
    struct Values { uint32_t reverse,constantBits,slopeBits,reserved; };
    const Values values{reverse,constantBits,slopeBits,0};
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth=sizeof(values); bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER; bd.Usage=D3D11_USAGE_IMMUTABLE;
    const D3D11_SUBRESOURCE_DATA initial{&values,0,0};
    ComPtr<ID3D11Buffer> created;
    check(device->CreateBuffer(&bd,&initial,&created),"scene depth constant creation");
    auto& slot=sceneDepthConstants[sceneDepthConstantNext]; sceneDepthConstantNext=(sceneDepthConstantNext+1)%sceneDepthConstants.size();
    slot.used=true; slot.reverse=reverse; slot.constantBits=constantBits; slot.slopeBits=slopeBits; slot.buffer=created;
    return created;
}
}
