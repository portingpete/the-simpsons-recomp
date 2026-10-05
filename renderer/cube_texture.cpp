#include "native_backend.h"
#include <cstdio>
#include <cstring>

namespace Simpsons::Graphics {
namespace {
void check(HRESULT result,const char* operation) {
    if(FAILED(result)) {
        char message[160];
        std::snprintf(message,sizeof(message),"Native D3D11 cube %s failed: 0x%08lX",operation,ULONG(result));
        throw Error(message);
    }
}
bool supportedSize(uint32_t size) {return size==16 || size==256;}
}
std::shared_ptr<CubeTexture> NativeBackend::createCubeTexture(uint32_t size) {
    validateSubmissionContext();
    if(!supportedSize(size)) throw Error("Native cube size must match the original 16/256 profiles");
    UINT support{};
    check(device->CheckFormatSupport(DXGI_FORMAT_R10G10B10A2_UNORM,&support),"format query");
    constexpr UINT required=D3D11_FORMAT_SUPPORT_TEXTURE2D|D3D11_FORMAT_SUPPORT_TEXTURECUBE|
        D3D11_FORMAT_SUPPORT_SHADER_SAMPLE|D3D11_FORMAT_SUPPORT_RENDER_TARGET;
    if((support&required)!=required) throw Error("Native RGB10A2 cube format is unavailable");
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=size;desc.Height=size;desc.MipLevels=1;desc.ArraySize=6;
    desc.Format=DXGI_FORMAT_R10G10B10A2_UNORM;desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
    auto result=std::make_shared<CubeTexture>();result->extent=size;
    // In particular, do not clear the lower half of a newly allocated N16 face.
    check(device->CreateTexture2D(&desc,nullptr,&result->texture),"allocation");
    D3D11_SHADER_RESOURCE_VIEW_DESC sampled{};
    sampled.Format=desc.Format;sampled.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBE;
    sampled.TextureCube.MostDetailedMip=0;sampled.TextureCube.MipLevels=1;
    check(device->CreateShaderResourceView(result->texture.Get(),&sampled,&result->view),"sampled view creation");
    validateCubeTexture(result);
    return result;
}
void NativeBackend::validateCubeTexture(const std::shared_ptr<CubeTexture>& cube) const {
    validateSubmissionContext();
    if(!cube || !cube->texture || !cube->view) throw Error("Missing native cube texture or sampled view");
    ComPtr<ID3D11Device> actual;cube->texture->GetDevice(&actual);
    if(actual.Get()!=device.Get()) throw Error("Native cube belongs to another graphics device");
    D3D11_TEXTURE2D_DESC desc{};cube->texture->GetDesc(&desc);
    if(!supportedSize(cube->extent) || desc.Width!=cube->extent || desc.Height!=cube->extent ||
       desc.MipLevels!=1 || desc.ArraySize!=6 || desc.Format!=DXGI_FORMAT_R10G10B10A2_UNORM ||
       desc.SampleDesc.Count!=1 || desc.SampleDesc.Quality!=0 || desc.Usage!=D3D11_USAGE_DEFAULT ||
       desc.BindFlags!=(D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET) ||
       desc.CPUAccessFlags!=0 || desc.MiscFlags!=D3D11_RESOURCE_MISC_TEXTURECUBE)
        throw Error("Native cube metadata does not match its backing");
    ComPtr<ID3D11Resource> viewed,resource;
    cube->view->GetResource(&viewed);check(cube->texture.As(&resource),"resource interface query");
    D3D11_SHADER_RESOURCE_VIEW_DESC sampled{};cube->view->GetDesc(&sampled);
    if(viewed.Get()!=resource.Get() || sampled.Format!=desc.Format ||
       sampled.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURECUBE ||
       sampled.TextureCube.MostDetailedMip!=0 || sampled.TextureCube.MipLevels!=1)
        throw Error("Native cube sampled view does not match its backing");
}
void NativeBackend::writeCubeRows(const std::shared_ptr<CubeTexture>& cube,uint32_t face,
                                 uint32_t firstRow,uint32_t rowCount,std::span<const uint8_t> bytes) {
    validateCubeTexture(cube);
    if(face>=6) throw Error("Native cube face is outside 0..5");
    if(firstRow>=cube->extent || !rowCount || rowCount>cube->extent-firstRow)
        throw Error("Native cube row range is outside the face");
    const uint32_t pitch=cube->extent*4;
    if(bytes.size()!=size_t(pitch)*rowCount) throw Error("Native cube upload byte count mismatch");
    const D3D11_BOX box{0,firstRow,0,cube->extent,firstRow+rowCount,1};
    // One mip means the array slice is the subresource index. A partial box
    // preserves all other texels. The immediate API snapshots CPU bytes here.
    context->UpdateSubresource(cube->texture.Get(),face,&box,bytes.data(),pitch,0);
    requireOwner();
}
std::vector<uint8_t> NativeBackend::readbackCubeFace(const std::shared_ptr<CubeTexture>& cube,uint32_t face) {
    flushIm2D();
    validateCubeTexture(cube);
    if(face>=6) throw Error("Native cube face is outside 0..5");
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=cube->extent;desc.Height=cube->extent;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R10G10B10A2_UNORM;desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    check(device->CreateTexture2D(&desc,nullptr,&staging),"readback allocation");
    const uint32_t pitch=cube->extent*4;
    std::vector<uint8_t> result(size_t(pitch)*cube->extent);
    context->CopySubresourceRegion(staging.Get(),0,0,0,0,cube->texture.Get(),face,nullptr);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"readback map");
    if(!mapped.pData || mapped.RowPitch<pitch) {
        context->Unmap(staging.Get(),0);
        throw Error("Native cube readback returned invalid row storage");
    }
    for(uint32_t row=0;row<cube->extent;++row)
        std::memcpy(result.data()+size_t(row)*pitch,static_cast<const uint8_t*>(mapped.pData)+size_t(row)*mapped.RowPitch,pitch);
    context->Unmap(staging.Get(),0);
    requireOwner();
    return result;
}
}
