#include "d3d11_vulkan.h"
#include "d3d11_vulkan_pipeline.h"
#include "renderer/native_backend.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Simpsons::Graphics::Vulkan {
uint64_t nextObjectId() noexcept {
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1,std::memory_order_relaxed);
}
void reportUnsupported(const char* what) noexcept {
    std::fprintf(stderr,"[NATIVE VULKAN] unsupported D3D11 use: %s\n",what);
}

HRESULT PrivateData::get(REFGUID guid,UINT* size,void* data) {
    if(!size) return E_INVALIDARG;
    std::lock_guard lock(mutex_);
    for(const auto& entry:entries_) {
        if(entry.guid!=guid) continue;
        if(entry.object) {
            if(!data) {*size=sizeof(IUnknown*);return S_OK;}
            if(*size<sizeof(IUnknown*)) {*size=0;return DXGI_ERROR_MORE_DATA;}
            IUnknown* object=entry.object.Get();object->AddRef();
            std::memcpy(data,&object,sizeof(object));*size=sizeof(object);return S_OK;
        }
        if(!data) {*size=UINT(entry.bytes.size());return S_OK;}
        if(*size<entry.bytes.size()) {*size=0;return DXGI_ERROR_MORE_DATA;}
        std::memcpy(data,entry.bytes.data(),entry.bytes.size());*size=UINT(entry.bytes.size());return S_OK;
    }
    *size=0;
    return DXGI_ERROR_NOT_FOUND;
}
HRESULT PrivateData::set(REFGUID guid,UINT size,const void* data) {
    std::lock_guard lock(mutex_);
    std::erase_if(entries_,[&](const Entry& entry){return entry.guid==guid;});
    if(!data || !size) return S_OK;
    Entry entry{guid,std::vector<uint8_t>(static_cast<const uint8_t*>(data),static_cast<const uint8_t*>(data)+size),{}};
    entries_.push_back(std::move(entry));
    return S_OK;
}
HRESULT PrivateData::setInterface(REFGUID guid,const IUnknown* object) {
    std::lock_guard lock(mutex_);
    std::erase_if(entries_,[&](const Entry& entry){return entry.guid==guid;});
    if(object) entries_.push_back({guid,{},const_cast<IUnknown*>(object)});
    return S_OK;
}

// ---------------------------------------------------------------- buffers
bool D3D11Buffer::supports(REFIID riid) const {
    return riid==__uuidof(ID3D11Resource) || riid==__uuidof(ID3D11Buffer) || riid==__uuidof(D3D11Buffer);
}
D3D11Buffer::D3D11Buffer(D3D11Device* device,const D3D11_BUFFER_DESC& desc,const D3D11_SUBRESOURCE_DATA* initial)
    :DeviceChild(device),desc_(desc) {
    auto& vulkan=device->vulkan();
    // UAV-bindable (structured/raw) and stream-output buffers are retained
    // state for D3D11 binding contracts; no shader stage here writes them.
    if(desc.BindFlags&D3D11_BIND_SHADER_RESOURCE) throw Error("Native Vulkan buffers support vertex, index, constant, stream-output and UAV binding only");
    if(desc.MiscFlags&~UINT(D3D11_RESOURCE_MISC_BUFFER_STRUCTURED|D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS))
        throw Error("Native Vulkan buffers map only structured/raw misc flags");
    if(desc.Usage==D3D11_USAGE_STAGING) {
        storage_=Storage::Staging;
        host_=vulkan.createHostBuffer(desc.ByteWidth,VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            (desc.CPUAccessFlags&D3D11_CPU_ACCESS_READ)!=0);
        if(initial && initial->pSysMem) std::memcpy(host_.data,initial->pSysMem,desc.ByteWidth);
        else std::memset(host_.data,0,desc.ByteWidth);
        vulkan.flush(host_);
        return;
    }
    if(desc.Usage==D3D11_USAGE_DYNAMIC || (desc.BindFlags&D3D11_BIND_CONSTANT_BUFFER)) {
        // Host storage renamed on DISCARD/whole updates (D3D11 orphaning):
        // already-recorded draws keep reading the slot they were recorded with.
        storage_=Storage::Renamed;
        slot_=vulkan.allocateSlot(desc.ByteWidth);
        if(initial && initial->pSysMem) std::memcpy(slot_.data,initial->pSysMem,desc.ByteWidth);
        else std::memset(slot_.data,0,desc.ByteWidth);
        return;
    }
    storage_=Storage::Device;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size=desc.ByteWidth;
    info.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|
        ((desc.BindFlags&(D3D11_BIND_VERTEX_BUFFER|D3D11_BIND_STREAM_OUTPUT))?VK_BUFFER_USAGE_VERTEX_BUFFER_BIT:0)|
        ((desc.BindFlags&D3D11_BIND_INDEX_BUFFER)?VK_BUFFER_USAGE_INDEX_BUFFER_BIT:0)|
        ((desc.BindFlags&D3D11_BIND_UNORDERED_ACCESS)?VK_BUFFER_USAGE_STORAGE_BUFFER_BIT:0);
    VmaAllocationCreateInfo allocation{};allocation.usage=VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    check(vmaCreateBuffer(vulkan.allocator(),&info,&allocation,&buffer_,&allocation_,nullptr),"buffer creation");
    if(initial && initial->pSysMem) {
        const HostBuffer staging=vulkan.createHostBuffer(desc.ByteWidth,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,false);
        std::memcpy(staging.data,initial->pSysMem,desc.ByteWidth);vulkan.flush(staging);
        const VkBuffer target=buffer_;const VkDeviceSize size=desc.ByteWidth;
        vulkan.recordInit([&](VkCommandBuffer command) {
            const VkBufferCopy region{0,0,size};vkCmdCopyBuffer(command,staging.buffer,target,1,&region);
        },[&vulkan,staging]{vulkan.destroyHostBuffer(staging);});
    } else {
        const VkBuffer target=buffer_;
        vulkan.recordInit([&](VkCommandBuffer command){vkCmdFillBuffer(command,target,0,VK_WHOLE_SIZE,0);});
    }
}
D3D11Buffer::~D3D11Buffer() {
    auto& vulkan=device_->vulkan();
    if(buffer_) vulkan.retire([allocator=vulkan.allocator(),buffer=buffer_,allocation=allocation_]{vmaDestroyBuffer(allocator,buffer,allocation);});
    if(slot_.buffer) vulkan.retire([&vulkan,slot=slot_]{vulkan.freeSlot(slot);});
    for(const auto& [serial,slot]:spare_) vulkan.retire([&vulkan,slot=slot]{vulkan.freeSlot(slot);});
    if(host_.buffer) vulkan.destroyHostBuffer(host_);
}
D3D11Buffer::Range D3D11Buffer::range() const {
    switch(storage_) {
    case Storage::Device:return {buffer_,0};
    case Storage::Renamed:return {slot_.buffer,slot_.offset};
    default:return {host_.buffer,0};
    }
}
uint8_t* D3D11Buffer::hostData() const {
    return storage_==Storage::Renamed?slot_.data:storage_==Storage::Staging?host_.data:nullptr;
}
void D3D11Buffer::rename(uint64_t retireAfter) {
    auto& vulkan=device_->vulkan();
    spare_.emplace_back(retireAfter,slot_);
    if(spare_.size()>1 && vulkan.complete(spare_.front().first)) {slot_=spare_.front().second;spare_.pop_front();}
    else slot_=vulkan.allocateSlot(desc_.ByteWidth);
    cpuCurrent_=true;
}
void D3D11Buffer::renamePreserving(uint64_t retireAfter) {
    if(!cpuCurrent_) throw Error("Native Vulkan partial update of a GPU-written renamed buffer is unsupported");
    const uint8_t* previous=slot_.data;
    rename(retireAfter);
    std::memcpy(slot_.data,previous,desc_.ByteWidth);
}

// ---------------------------------------------------------------- textures
bool D3D11Texture2D::supports(REFIID riid) const {
    return riid==__uuidof(ID3D11Resource) || riid==__uuidof(ID3D11Texture2D) || riid==__uuidof(D3D11Texture2D);
}
D3D11Texture2D::D3D11Texture2D(D3D11Device* device,const D3D11_TEXTURE2D_DESC& desc,const D3D11_SUBRESOURCE_DATA* initial)
    :DeviceChild(device),desc_(desc) {
    if(!desc_.Width || !desc_.Height || desc_.Width>16384 || desc_.Height>16384 || !desc_.ArraySize || desc_.ArraySize>2048)
        throw Error("Native Vulkan texture extent is invalid");
    if(!desc_.MipLevels) {
        uint32_t levels=1;
        for(uint32_t extent=std::max(desc_.Width,desc_.Height);extent>1;extent>>=1) ++levels;
        desc_.MipLevels=levels;
    }
    if(desc_.SampleDesc.Count!=1 || desc_.SampleDesc.Quality)
        throw Error("Native Vulkan textures are single-sample only");
    if(desc_.MiscFlags&~UINT(D3D11_RESOURCE_MISC_TEXTURECUBE)) throw Error("Native Vulkan textures map only the TEXTURECUBE misc flag");
    if((desc_.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE) && desc_.ArraySize%6) throw Error("Native Vulkan cube textures need six faces per cube");
    constexpr UINT unsupportedBinds=D3D11_BIND_STREAM_OUTPUT|D3D11_BIND_VERTEX_BUFFER|
        D3D11_BIND_INDEX_BUFFER|D3D11_BIND_CONSTANT_BUFFER|D3D11_BIND_DECODER|D3D11_BIND_VIDEO_ENCODER;
    if(desc_.BindFlags&unsupportedBinds) throw Error("Native Vulkan textures support shader-resource, render-target, depth and UAV binding only");
    format_=resourceFormat(desc_.Format,desc_.BindFlags);
    if(isStaging()) {createStaging();upload(initial);return;}
    createImage();
    upload(initial);
}
void D3D11Texture2D::createStaging() {
    if(desc_.SampleDesc.Count!=1 || desc_.BindFlags) throw Error("Native Vulkan staging textures must be single-sample and unbound");
    const bool combined=format_.depth() && format_.stencil();
    VkDeviceSize offset=0;size_t shadowBytes=0;
    for(uint32_t layer=0;layer<desc_.ArraySize;++layer) for(uint32_t mip=0;mip<desc_.MipLevels;++mip) {
        Layout layout;layout.width=mipWidth(mip);layout.height=mipHeight(mip);
        const uint32_t blocksWide=(layout.width+format_.blockWidth-1)/format_.blockWidth;
        const uint32_t blocksHigh=(layout.height+format_.blockHeight-1)/format_.blockHeight;
        layout.rowPitch=blocksWide*format_.blockBytes;layout.depthPitch=layout.rowPitch*blocksHigh;
        if(combined) {
            // Vulkan copies the aspects as separate planes: 4-byte depth then
            // 1-byte stencil. Map interleaves them into the D3D texel layout.
            layout.offset=offset;offset+=VkDeviceSize(layout.width)*layout.height*4;
            layout.stencilOffset=offset;offset=(offset+VkDeviceSize(layout.width)*layout.height+15)&~VkDeviceSize(15);
            shadowBytes+=layout.depthPitch;
        } else {
            layout.offset=offset;offset=(offset+layout.depthPitch+15)&~VkDeviceSize(15);
        }
        layouts_.push_back(layout);
    }
    // layouts_ index = mip + layer*mips (D3D11CalcSubresource order).
    host_=device_->vulkan().createHostBuffer(offset,VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        (desc_.CPUAccessFlags&D3D11_CPU_ACCESS_READ)!=0);
    std::memset(host_.data,0,size_t(offset));
    if(combined) shadow_.assign(shadowBytes,0);
}
void D3D11Texture2D::createImage() {
    auto& vulkan=device_->vulkan();
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType=VK_IMAGE_TYPE_2D;info.format=format_.format;
    info.extent={desc_.Width,desc_.Height,1};info.mipLevels=desc_.MipLevels;info.arrayLayers=desc_.ArraySize;
    info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;info.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if(desc_.BindFlags&D3D11_BIND_SHADER_RESOURCE) info.usage|=VK_IMAGE_USAGE_SAMPLED_BIT;
    if(desc_.BindFlags&D3D11_BIND_RENDER_TARGET) info.usage|=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if(desc_.BindFlags&D3D11_BIND_DEPTH_STENCIL) info.usage|=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if(desc_.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE) info.flags|=VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    if(format_.typeless && !format_.depth() && !format_.stencil()) info.flags|=VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    VmaAllocationCreateInfo allocation{};allocation.usage=VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if(info.usage&(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
        allocation.flags|=VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    check(vmaCreateImage(vulkan.allocator(),&info,&allocation,&image_,&allocation_,nullptr),"texture creation");
}
void D3D11Texture2D::upload(const D3D11_SUBRESOURCE_DATA* initial) {
    auto& vulkan=device_->vulkan();
    const bool combined=format_.depth() && format_.stencil();
    if(isStaging()) {
        if(!initial) return;
        if(combined) throw Error("Native Vulkan depth/stencil staging initial data is unsupported");
        for(uint32_t sub=0;sub<subresources();++sub) {
            const auto& layout=layouts_[sub];const auto& source=initial[sub];
            const uint32_t rows=layout.depthPitch/layout.rowPitch;
            for(uint32_t row=0;row<rows;++row)
                std::memcpy(host_.data+layout.offset+VkDeviceSize(row)*layout.rowPitch,
                    static_cast<const uint8_t*>(source.pSysMem)+size_t(row)*source.SysMemPitch,layout.rowPitch);
        }
        vulkan.flush(host_);
        return;
    }
    const VkImage image=image_;
    const VkImageSubresourceRange all{format_.aspects,0,desc_.MipLevels,0,desc_.ArraySize};
    auto toGeneral=[image,all](VkCommandBuffer command) {
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask=VK_PIPELINE_STAGE_2_NONE;barrier.dstStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.dstAccessMask=VK_ACCESS_2_MEMORY_READ_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.image=image;barrier.subresourceRange=all;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dependency.imageMemoryBarrierCount=1;dependency.pImageMemoryBarriers=&barrier;
        vkCmdPipelineBarrier2(command,&dependency);
    };
    if(!initial) {
        // Every image lives in GENERAL. New storage starts zeroed like D3D11
        // allocations; compressed formats cannot be cleared and always upload.
        const bool clearable=!format_.compressed;
        const bool depthStencil=format_.depth()||format_.stencil();
        vulkan.recordInit([&](VkCommandBuffer command) {
            toGeneral(command);
            if(!clearable) return;
            VkMemoryBarrier2 order{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
            order.srcStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;order.dstStageMask=VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
            order.dstAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dependency.memoryBarrierCount=1;dependency.pMemoryBarriers=&order;
            vkCmdPipelineBarrier2(command,&dependency);
            if(depthStencil) {
                const VkClearDepthStencilValue zero{0.0f,0};vkCmdClearDepthStencilImage(command,image,VK_IMAGE_LAYOUT_GENERAL,&zero,1,&all);
            } else {
                const VkClearColorValue zero{};vkCmdClearColorImage(command,image,VK_IMAGE_LAYOUT_GENERAL,&zero,1,&all);
            }
        });
        return;
    }
    if(format_.depth()||format_.stencil()) {uploadDepthStencil(initial,toGeneral);return;}
    VkDeviceSize total=0;
    std::vector<VkBufferImageCopy> regions;
    std::vector<std::tuple<VkDeviceSize,uint32_t,uint32_t>> rows; // offset, row bytes, row count
    for(uint32_t layer=0;layer<desc_.ArraySize;++layer) for(uint32_t mip=0;mip<desc_.MipLevels;++mip) {
        const uint32_t width=mipWidth(mip),height=mipHeight(mip);
        const uint32_t blocksWide=(width+format_.blockWidth-1)/format_.blockWidth,blocksHigh=(height+format_.blockHeight-1)/format_.blockHeight;
        VkBufferImageCopy region{};
        region.bufferOffset=total;region.imageSubresource={format_.aspects,mip,layer,1};region.imageExtent={width,height,1};
        regions.push_back(region);
        rows.emplace_back(total,blocksWide*format_.blockBytes,blocksHigh);
        total=(total+VkDeviceSize(blocksWide)*format_.blockBytes*blocksHigh+15)&~VkDeviceSize(15);
    }
    const HostBuffer staging=vulkan.createHostBuffer(total,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,false);
    for(size_t sub=0;sub<rows.size();++sub) {
        const auto [offset,rowBytes,count]=rows[sub];
        const auto& source=initial[sub];
        if(!source.pSysMem) {vulkan.destroyHostBuffer(staging);throw Error("Native Vulkan texture initial data lacks a subresource");}
        for(uint32_t row=0;row<count;++row)
            std::memcpy(staging.data+offset+VkDeviceSize(row)*rowBytes,static_cast<const uint8_t*>(source.pSysMem)+size_t(row)*source.SysMemPitch,rowBytes);
    }
    vulkan.flush(staging);
    vulkan.recordInit([&](VkCommandBuffer command) {
        toGeneral(command);
        VkMemoryBarrier2 order{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        order.srcStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;order.dstStageMask=VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        order.dstAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dependency.memoryBarrierCount=1;dependency.pMemoryBarriers=&order;
        vkCmdPipelineBarrier2(command,&dependency);
        vkCmdCopyBufferToImage(command,staging.buffer,image,VK_IMAGE_LAYOUT_GENERAL,uint32_t(regions.size()),regions.data());
    },[&vulkan,staging]{vulkan.destroyHostBuffer(staging);});
}
// D3D packs depth and stencil per texel (D32S8: float then stencil byte in 8
// bytes; D24S8: 24-bit depth below the stencil byte); Vulkan copies each aspect
// as its own plane (4-byte depth words, 1-byte stencil).
void D3D11Texture2D::uploadDepthStencil(const D3D11_SUBRESOURCE_DATA* initial,const std::function<void(VkCommandBuffer)>& toGeneral) {
    auto& vulkan=device_->vulkan();
    const uint32_t texelBytes=format_.blockBytes;
    const uint32_t depthBytes=format_.format==VK_FORMAT_D16_UNORM?2:4;
    VkDeviceSize total=0;
    std::vector<VkBufferImageCopy> regions;
    std::vector<std::pair<VkDeviceSize,VkDeviceSize>> planes;
    for(uint32_t layer=0;layer<desc_.ArraySize;++layer) for(uint32_t mip=0;mip<desc_.MipLevels;++mip) {
        const uint32_t width=mipWidth(mip),height=mipHeight(mip);
        const VkDeviceSize depthOffset=total;total=(total+VkDeviceSize(width)*height*depthBytes+15)&~VkDeviceSize(15);
        const VkDeviceSize stencilOffset=total;
        if(format_.depth()) regions.push_back({depthOffset,0,0,{VK_IMAGE_ASPECT_DEPTH_BIT,mip,layer,1},{0,0,0},{width,height,1}});
        if(format_.stencil()) {
            regions.push_back({stencilOffset,0,0,{VK_IMAGE_ASPECT_STENCIL_BIT,mip,layer,1},{0,0,0},{width,height,1}});
            total=(total+VkDeviceSize(width)*height+15)&~VkDeviceSize(15);
        }
        planes.emplace_back(depthOffset,stencilOffset);
    }
    const HostBuffer staging=vulkan.createHostBuffer(total,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,false);
    size_t sub=0;
    for(uint32_t layer=0;layer<desc_.ArraySize;++layer) for(uint32_t mip=0;mip<desc_.MipLevels;++mip,++sub) {
        const uint32_t width=mipWidth(mip),height=mipHeight(mip);
        const auto& source=initial[sub];
        if(!source.pSysMem) {vulkan.destroyHostBuffer(staging);throw Error("Native Vulkan depth initial data lacks a subresource");}
        uint8_t* depth=staging.data+planes[sub].first;uint8_t* stencil=staging.data+planes[sub].second;
        for(uint32_t y=0;y<height;++y) for(uint32_t x=0;x<width;++x) {
            const uint8_t* texel=static_cast<const uint8_t*>(source.pSysMem)+size_t(y)*source.SysMemPitch+size_t(x)*texelBytes;
            const size_t index=size_t(y)*width+x;
            if(texelBytes==8) {std::memcpy(depth+index*4,texel,4);stencil[index]=texel[4];}
            else if(format_.stencil()) {uint32_t word=0;std::memcpy(&word,texel,4);const uint32_t d=word&0xFFFFFFu;std::memcpy(depth+index*4,&d,4);stencil[index]=uint8_t(word>>24);}
            else std::memcpy(depth+index*depthBytes,texel,depthBytes);
        }
    }
    vulkan.flush(staging);
    const VkImage image=image_;
    vulkan.recordInit([&](VkCommandBuffer command) {
        toGeneral(command);
        VkMemoryBarrier2 order{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        order.srcStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;order.dstStageMask=VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        order.dstAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dependency.memoryBarrierCount=1;dependency.pMemoryBarriers=&order;
        vkCmdPipelineBarrier2(command,&dependency);
        vkCmdCopyBufferToImage(command,staging.buffer,image,VK_IMAGE_LAYOUT_GENERAL,uint32_t(regions.size()),regions.data());
    },[&vulkan,staging]{vulkan.destroyHostBuffer(staging);});
}
D3D11Texture2D::~D3D11Texture2D() {
    auto& vulkan=device_->vulkan();
    if(image_) vulkan.retire([allocator=vulkan.allocator(),image=image_,allocation=allocation_]{vmaDestroyImage(allocator,image,allocation);});
    if(host_.buffer) vulkan.destroyHostBuffer(host_);
}

// ---------------------------------------------------------------- views
namespace {
template<class T> T* unwrapResource(ID3D11Resource* resource) {
    if(!resource) return nullptr;
    void* object=nullptr;
    if(FAILED(resource->QueryInterface(__uuidof(T),&object))) return nullptr;
    static_cast<T*>(object)->Release();
    return static_cast<T*>(object);
}
VkImageView createView(VulkanDevice& vulkan,VkImage image,VkImageViewType type,const ViewFormat& format,const ViewRange& range) {
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image=image;info.viewType=type;info.format=format.format;info.components=format.swizzle;
    info.subresourceRange={format.aspect,range.mip,range.mips,range.layer,range.layers};
    VkImageView view=VK_NULL_HANDLE;
    check(vkCreateImageView(vulkan.device(),&info,nullptr,&view),"image view creation");
    return view;
}
void retireView(VulkanDevice& vulkan,VkImageView view) {
    if(view) vulkan.retire([device=vulkan.device(),view]{vkDestroyImageView(device,view,nullptr);});
}
}

bool D3D11ShaderResourceView::supports(REFIID riid) const {
    return riid==__uuidof(ID3D11View) || riid==__uuidof(ID3D11ShaderResourceView) || riid==__uuidof(D3D11ShaderResourceView);
}
D3D11ShaderResourceView::D3D11ShaderResourceView(D3D11Device* device,ID3D11Resource* resource,const D3D11_SHADER_RESOURCE_VIEW_DESC* desc)
    :DeviceChild(device),resource_(resource) {
    texture_=unwrapResource<D3D11Texture2D>(resource);
    if(!texture_ || texture_->parent()!=device) throw Error("Native Vulkan shader-resource views require a 2D texture of this device");
    const auto& td=texture_->desc();
    if(!(td.BindFlags&D3D11_BIND_SHADER_RESOURCE)) throw Error("Native Vulkan shader-resource view of an unbound texture");
    if(desc) desc_=*desc;
    else {
        desc_.Format=td.Format;
        const bool cube=(td.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE)!=0;
        if(cube) {desc_.ViewDimension=td.ArraySize>6?D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:D3D11_SRV_DIMENSION_TEXTURECUBE;}
        else desc_.ViewDimension=td.ArraySize>1?D3D11_SRV_DIMENSION_TEXTURE2DARRAY:D3D11_SRV_DIMENSION_TEXTURE2D;
        desc_.Texture2DArray={0,td.MipLevels,0,td.ArraySize};
        if(desc_.ViewDimension==D3D11_SRV_DIMENSION_TEXTURECUBE) desc_.TextureCube={0,td.MipLevels};
        if(desc_.ViewDimension==D3D11_SRV_DIMENSION_TEXTURECUBEARRAY) desc_.TextureCubeArray={0,td.MipLevels,0,td.ArraySize/6};
    }
    const DXGI_FORMAT viewFormatCode=desc_.Format==DXGI_FORMAT_UNKNOWN?td.Format:desc_.Format;
    if(!sameFamily(viewFormatCode,td.Format)) throw Error("Native Vulkan view format is outside its resource family");
    const ViewFormat format=viewFormat(viewFormatCode,texture_->format());
    auto mips=[&](UINT first,UINT count) {
        if(first>=td.MipLevels) throw Error("Native Vulkan view mip range is invalid");
        range_.mip=first;range_.mips=count==UINT(-1)?td.MipLevels-first:count;
        if(!range_.mips || range_.mip+range_.mips>td.MipLevels) throw Error("Native Vulkan view mip range is invalid");
    };
    VkImageViewType type=VK_IMAGE_VIEW_TYPE_2D;
    switch(desc_.ViewDimension) {
    case D3D11_SRV_DIMENSION_TEXTURE2D:mips(desc_.Texture2D.MostDetailedMip,desc_.Texture2D.MipLevels);break;
    case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
        mips(desc_.Texture2DArray.MostDetailedMip,desc_.Texture2DArray.MipLevels);
        range_.layer=desc_.Texture2DArray.FirstArraySlice;range_.layers=desc_.Texture2DArray.ArraySize;type=VK_IMAGE_VIEW_TYPE_2D_ARRAY;break;
    case D3D11_SRV_DIMENSION_TEXTURECUBE:
        mips(desc_.TextureCube.MostDetailedMip,desc_.TextureCube.MipLevels);range_.layers=6;type=VK_IMAGE_VIEW_TYPE_CUBE;break;
    case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
        mips(desc_.TextureCubeArray.MostDetailedMip,desc_.TextureCubeArray.MipLevels);
        range_.layer=desc_.TextureCubeArray.First2DArrayFace;range_.layers=desc_.TextureCubeArray.NumCubes*6;type=VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;break;
    default:throw Error("Native Vulkan shader-resource view dimension is unsupported");
    }
    if(!range_.layers || range_.layer+range_.layers>td.ArraySize) throw Error("Native Vulkan view array range is invalid");
    view_=createView(device->vulkan(),texture_->image(),type,format,range_);
}
D3D11ShaderResourceView::~D3D11ShaderResourceView() {retireView(device_->vulkan(),view_);}

bool D3D11RenderTargetView::supports(REFIID riid) const {
    return riid==__uuidof(ID3D11View) || riid==__uuidof(ID3D11RenderTargetView) || riid==__uuidof(D3D11RenderTargetView);
}
D3D11RenderTargetView::D3D11RenderTargetView(D3D11Device* device,ID3D11Resource* resource,const D3D11_RENDER_TARGET_VIEW_DESC* desc)
    :DeviceChild(device),resource_(resource) {
    texture_=unwrapResource<D3D11Texture2D>(resource);
    if(!texture_ || texture_->parent()!=device) throw Error("Native Vulkan render-target views require a 2D texture of this device");
    const auto& td=texture_->desc();
    if(!(td.BindFlags&D3D11_BIND_RENDER_TARGET)) throw Error("Native Vulkan render-target view of a texture without RENDER_TARGET binding");
    if(desc) desc_=*desc;
    else {
        desc_.Format=td.Format;
        desc_.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    }
    const DXGI_FORMAT code=desc_.Format==DXGI_FORMAT_UNKNOWN?td.Format:desc_.Format;
    if(!sameFamily(code,td.Format)) throw Error("Native Vulkan view format is outside its resource family");
    format_=Vulkan::viewFormat(code,texture_->format());
    switch(desc_.ViewDimension) {
    case D3D11_RTV_DIMENSION_TEXTURE2D:range_.mip=desc_.Texture2D.MipSlice;break;
    case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
        range_.mip=desc_.Texture2DArray.MipSlice;range_.layer=desc_.Texture2DArray.FirstArraySlice;
        if(desc_.Texture2DArray.ArraySize!=1) throw Error("Native Vulkan layered render targets are unsupported");
        break;
    default:throw Error("Native Vulkan render-target view dimension is unsupported");
    }
    if(range_.mip>=td.MipLevels || range_.layer>=td.ArraySize) throw Error("Native Vulkan render-target subresource is invalid");
    extent_={texture_->mipWidth(range_.mip),texture_->mipHeight(range_.mip)};
    view_=createView(device->vulkan(),texture_->image(),VK_IMAGE_VIEW_TYPE_2D,format_,range_);
}
D3D11RenderTargetView::~D3D11RenderTargetView() {retireView(device_->vulkan(),view_);}

bool D3D11DepthStencilView::supports(REFIID riid) const {
    return riid==__uuidof(ID3D11View) || riid==__uuidof(ID3D11DepthStencilView) || riid==__uuidof(D3D11DepthStencilView);
}
D3D11DepthStencilView::D3D11DepthStencilView(D3D11Device* device,ID3D11Resource* resource,const D3D11_DEPTH_STENCIL_VIEW_DESC* desc)
    :DeviceChild(device),resource_(resource) {
    texture_=unwrapResource<D3D11Texture2D>(resource);
    if(!texture_ || texture_->parent()!=device) throw Error("Native Vulkan depth-stencil views require a 2D texture of this device");
    const auto& td=texture_->desc();
    if(!(td.BindFlags&D3D11_BIND_DEPTH_STENCIL)) throw Error("Native Vulkan depth-stencil view of a texture without DEPTH_STENCIL binding");
    if(desc) desc_=*desc;
    else {
        desc_.Format=td.Format;
        desc_.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    }
    if(desc_.Flags) throw Error("Native Vulkan read-only depth-stencil views are unsupported");
    const DXGI_FORMAT code=desc_.Format==DXGI_FORMAT_UNKNOWN?td.Format:desc_.Format;
    if(!sameFamily(code,td.Format)) throw Error("Native Vulkan view format is outside its resource family");
    ViewFormat format=viewFormat(code,texture_->format());
    if(!(format.aspect&(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT))) throw Error("Native Vulkan depth-stencil view format has no depth");
    aspects_=format.aspect;
    switch(desc_.ViewDimension) {
    case D3D11_DSV_DIMENSION_TEXTURE2D:range_.mip=desc_.Texture2D.MipSlice;break;
    case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
        range_.mip=desc_.Texture2DArray.MipSlice;range_.layer=desc_.Texture2DArray.FirstArraySlice;
        if(desc_.Texture2DArray.ArraySize!=1) throw Error("Native Vulkan layered depth targets are unsupported");
        break;
    default:throw Error("Native Vulkan depth-stencil view dimension is unsupported");
    }
    if(range_.mip>=td.MipLevels || range_.layer>=td.ArraySize) throw Error("Native Vulkan depth-stencil subresource is invalid");
    extent_={texture_->mipWidth(range_.mip),texture_->mipHeight(range_.mip)};
    format.swizzle={};
    view_=createView(device->vulkan(),texture_->image(),VK_IMAGE_VIEW_TYPE_2D,format,range_);
}
D3D11DepthStencilView::~D3D11DepthStencilView() {retireView(device_->vulkan(),view_);}

bool D3D11UnorderedAccessView::supports(REFIID riid) const {
    return riid==__uuidof(ID3D11View) || riid==__uuidof(ID3D11UnorderedAccessView) || riid==__uuidof(D3D11UnorderedAccessView);
}
D3D11UnorderedAccessView::D3D11UnorderedAccessView(D3D11Device* device,ID3D11Resource* resource,const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc)
    :DeviceChild(device),resource_(resource) {
    auto* buffer=unwrapResource<D3D11Buffer>(resource);
    auto* texture=unwrapResource<D3D11Texture2D>(resource);
    if((!buffer && !texture) || (buffer?buffer->parent():texture->parent())!=device)
        throw Error("Native Vulkan unordered access views require a resource of this device");
    const UINT binds=buffer?buffer->desc().BindFlags:texture->desc().BindFlags;
    if(!(binds&D3D11_BIND_UNORDERED_ACCESS)) throw Error("Native Vulkan UAV of a resource without UNORDERED_ACCESS binding");
    if(desc) desc_=*desc;
    else if(texture) {desc_.Format=texture->desc().Format;desc_.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;}
    else {
        const auto& bd=buffer->desc();
        if(!(bd.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) || !bd.StructureByteStride)
            throw Error("Native Vulkan default buffer UAV needs a structured buffer");
        desc_.Format=DXGI_FORMAT_UNKNOWN;desc_.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;desc_.Buffer.NumElements=bd.ByteWidth/bd.StructureByteStride;
    }
}

// ---------------------------------------------------------------- states
namespace {
VkSamplerAddressMode addressMode(D3D11_TEXTURE_ADDRESS_MODE mode,bool mirrorOnce) {
    switch(mode) {
    case D3D11_TEXTURE_ADDRESS_WRAP:return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case D3D11_TEXTURE_ADDRESS_MIRROR:return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case D3D11_TEXTURE_ADDRESS_CLAMP:return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case D3D11_TEXTURE_ADDRESS_BORDER:return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case D3D11_TEXTURE_ADDRESS_MIRROR_ONCE:
        if(!mirrorOnce) throw Error("Native Vulkan adapter lacks MIRROR_ONCE sampling");
        return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    default:throw Error("Native Vulkan sampler address mode is invalid");
    }
}
}
VkCompareOp compareOp(D3D11_COMPARISON_FUNC function) {
    switch(function) {
    case D3D11_COMPARISON_NEVER:return VK_COMPARE_OP_NEVER;
    case D3D11_COMPARISON_LESS:return VK_COMPARE_OP_LESS;
    case D3D11_COMPARISON_EQUAL:return VK_COMPARE_OP_EQUAL;
    case D3D11_COMPARISON_LESS_EQUAL:return VK_COMPARE_OP_LESS_OR_EQUAL;
    case D3D11_COMPARISON_GREATER:return VK_COMPARE_OP_GREATER;
    case D3D11_COMPARISON_NOT_EQUAL:return VK_COMPARE_OP_NOT_EQUAL;
    case D3D11_COMPARISON_GREATER_EQUAL:return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case D3D11_COMPARISON_ALWAYS:return VK_COMPARE_OP_ALWAYS;
    default:throw Error("Native Vulkan comparison function is invalid");
    }
}
bool D3D11SamplerState::supports(REFIID riid) const {
    return riid==__uuidof(ID3D11SamplerState) || riid==__uuidof(D3D11SamplerState);
}
D3D11SamplerState::D3D11SamplerState(D3D11Device* device,const D3D11_SAMPLER_DESC& desc):DeviceChild(device),desc_(desc) {
    auto& vulkan=device->vulkan();
    const uint32_t filter=desc.Filter;
    if(filter&0x100) throw Error("Native Vulkan minimum/maximum filter reduction is unsupported");
    const bool anisotropic=(filter&0x40)!=0,comparison=(filter&0x80)!=0;
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    // D3D11_FILTER: bit0 mip linear, bit2 mag linear, bit4 min linear.
    info.minFilter=(anisotropic||(filter&0x10))?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
    info.magFilter=(anisotropic||(filter&0x04))?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
    info.mipmapMode=(anisotropic||(filter&0x01))?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST;
    const bool mirrorOnce=vulkan.capabilities().mirrorClampToEdge;
    info.addressModeU=addressMode(desc.AddressU,mirrorOnce);info.addressModeV=addressMode(desc.AddressV,mirrorOnce);
    info.addressModeW=addressMode(desc.AddressW,mirrorOnce);
    const float maxBias=vulkan.properties().limits.maxSamplerLodBias;
    info.mipLodBias=std::clamp(desc.MipLODBias,-maxBias,maxBias);
    if(anisotropic) {
        info.anisotropyEnable=VK_TRUE;
        info.maxAnisotropy=std::clamp(float(desc.MaxAnisotropy),1.0f,vulkan.properties().limits.maxSamplerAnisotropy);
    }
    info.compareEnable=comparison;info.compareOp=comparison?compareOp(desc.ComparisonFunc):VK_COMPARE_OP_NEVER;
    info.minLod=std::isfinite(desc.MinLOD)?std::max(desc.MinLOD,0.0f):0.0f;
    info.maxLod=(!std::isfinite(desc.MaxLOD) || desc.MaxLOD>=VK_LOD_CLAMP_NONE)?VK_LOD_CLAMP_NONE:std::max(desc.MaxLOD,info.minLod);
    VkSamplerCustomBorderColorCreateInfoEXT custom{VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT};
    const bool border=desc.AddressU==D3D11_TEXTURE_ADDRESS_BORDER||desc.AddressV==D3D11_TEXTURE_ADDRESS_BORDER||desc.AddressW==D3D11_TEXTURE_ADDRESS_BORDER;
    if(border) {
        const auto* c=desc.BorderColor;
        if(c[0]==0&&c[1]==0&&c[2]==0&&c[3]==0) info.borderColor=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        else if(c[0]==0&&c[1]==0&&c[2]==0&&c[3]==1) info.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        else if(c[0]==1&&c[1]==1&&c[2]==1&&c[3]==1) info.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        else if(vulkan.capabilities().customBorderColor) {
            std::memcpy(custom.customBorderColor.float32,c,sizeof(float)*4);custom.format=VK_FORMAT_UNDEFINED;
            info.borderColor=VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;info.pNext=&custom;
        } else throw Error("Native Vulkan adapter lacks custom sampler border colors");
    }
    check(vkCreateSampler(vulkan.device(),&info,nullptr,&sampler_),"sampler creation");
}
D3D11SamplerState::~D3D11SamplerState() {
    device_->forgetSampler(this);
    auto& vulkan=device_->vulkan();
    vulkan.retire([device=vulkan.device(),sampler=sampler_]{vkDestroySampler(device,sampler,nullptr);});
}
bool D3D11BlendState::supports(REFIID riid) const {return riid==__uuidof(ID3D11BlendState) || riid==__uuidof(D3D11BlendState);}
D3D11BlendState::~D3D11BlendState() {device_->forgetBlend(this);}
bool D3D11DepthStencilState::supports(REFIID riid) const {return riid==__uuidof(ID3D11DepthStencilState) || riid==__uuidof(D3D11DepthStencilState);}
D3D11DepthStencilState::~D3D11DepthStencilState() {device_->forgetDepthStencil(this);}
bool D3D11RasterizerState::supports(REFIID riid) const {return riid==__uuidof(ID3D11RasterizerState) || riid==__uuidof(D3D11RasterizerState);}
D3D11RasterizerState::~D3D11RasterizerState() {device_->forgetRasterizer(this);}
bool D3D11InputLayout::supports(REFIID riid) const {return riid==__uuidof(ID3D11InputLayout) || riid==__uuidof(D3D11InputLayout);}

// ---------------------------------------------------------------- shaders
namespace {
VkShaderModule createModule(VulkanDevice& vulkan,const std::vector<uint32_t>& words) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize=words.size()*sizeof(uint32_t);info.pCode=words.data();
    VkShaderModule module=VK_NULL_HANDLE;
    check(vkCreateShaderModule(vulkan.device(),&info,nullptr,&module),"shader module creation");
    return module;
}
void retireModule(VulkanDevice& vulkan,VkShaderModule module) {
    if(module) vulkan.retire([device=vulkan.device(),module]{vkDestroyShaderModule(device,module,nullptr);});
}
}
bool D3D11VertexShader::supports(REFIID riid) const {return riid==__uuidof(ID3D11VertexShader) || riid==__uuidof(D3D11VertexShader);}
D3D11VertexShader::D3D11VertexShader(D3D11Device* device,std::shared_ptr<const SpirvModule> spirv)
    :DeviceChild(device),spirv_(std::move(spirv)) {
    module_=createModule(device->vulkan(),spirv_->withBindingOffset(0));
}
D3D11VertexShader::~D3D11VertexShader() {retireModule(device_->vulkan(),module_);}
bool D3D11PixelShader::supports(REFIID riid) const {return riid==__uuidof(ID3D11PixelShader) || riid==__uuidof(D3D11PixelShader);}
D3D11PixelShader::D3D11PixelShader(D3D11Device* device,std::shared_ptr<const SpirvModule> spirv)
    :DeviceChild(device),spirv_(std::move(spirv)) {
    if(spirv_->writesStencil() && !device->vulkan().capabilities().stencilExport)
        throw Error("Native Vulkan adapter lacks shader stencil export for "+spirv_->name());
    module_=createModule(device->vulkan(),spirv_->withBindingOffset(pixelBindingBase));
}
D3D11PixelShader::~D3D11PixelShader() {
    auto& vulkan=device_->vulkan();
    retireModule(vulkan,module_);
    for(auto& [id,module]:linked_) retireModule(vulkan,module);
}
VkShaderModule D3D11PixelShader::moduleFor(const D3D11VertexShader& vertex) {
    const auto& outputs=vertex.spirv().outputs();
    std::vector<std::pair<const SpirvVariable*,uint32_t>> remap;
    uint32_t used=0;
    for(const auto& output:outputs) for(uint32_t i=0;i<output.locations;++i) used|=1u<<(output.location+i);
    bool identical=true;
    for(const auto& input:spirv_->inputs()) {
        const SpirvVariable* match=nullptr;
        for(const auto& output:outputs) if(output.semantic==input.semantic && output.index==input.index) {match=&output;break;}
        uint32_t location;
        if(match) location=match->location;
        else {
            // D3D would reject the link; read an unwritten location instead of
            // aliasing another semantic.
            location=31;while(location && (used&(1u<<location))) --location;
        }
        if(location!=input.location) identical=false;
        remap.emplace_back(&input,location);
    }
    if(identical) return module_;
    std::lock_guard lock(mutex_);
    if(auto found=linked_.find(vertex.id());found!=linked_.end()) return found->second;
    const VkShaderModule module=createModule(device_->vulkan(),spirv_->withInputLocations(remap,pixelBindingBase));
    linked_.emplace(vertex.id(),module);
    return module;
}

// ---------------------------------------------------------------- queries and lists
bool D3D11Query::supports(REFIID riid) const {
    return riid==__uuidof(ID3D11Asynchronous) || riid==__uuidof(ID3D11Query) || riid==__uuidof(D3D11Query) ||
        (riid==__uuidof(ID3D11Predicate) && desc_.Query==D3D11_QUERY_OCCLUSION_PREDICATE);
}
UINT D3D11Query::GetDataSize() {
    switch(desc_.Query) {
    case D3D11_QUERY_EVENT:case D3D11_QUERY_OCCLUSION_PREDICATE:return sizeof(BOOL);
    case D3D11_QUERY_TIMESTAMP:case D3D11_QUERY_OCCLUSION:return sizeof(UINT64);
    case D3D11_QUERY_TIMESTAMP_DISJOINT:return sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT);
    case D3D11_QUERY_PIPELINE_STATISTICS:return sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS);
    default:return 0;
    }
}
void D3D11Query::releaseSlots() {
    if(slot!=~0u) {device_->releaseQuerySlot(QueryPool::Timestamp,slot);slot=~0u;}
    for(uint32_t segment:segments) device_->releaseQuerySlot(pool(),segment);
    segments.clear();
}
D3D11Query::~D3D11Query() {releaseSlots();}
bool D3D11CommandList::supports(REFIID riid) const {return riid==__uuidof(ID3D11CommandList) || riid==__uuidof(D3D11CommandList);}
bool D3D11DeviceContextState::supports(REFIID riid) const {return riid==__uuidof(ID3DDeviceContextState) || riid==__uuidof(D3D11DeviceContextState);}
}
