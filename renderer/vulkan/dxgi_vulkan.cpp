#include "dxgi_vulkan.h"
#include "renderer/native_backend.h"
#include "runtime/stall_profiler.h"
#include <algorithm>
#include <cmath>
#include <array>
#include <cstdio>
#include <cstring>

namespace Simpsons::Graphics::Vulkan {
namespace {
constexpr uint64_t fiveSeconds=5000000000ull;
AdapterInfo adapterInfo(VulkanDevice& vulkan) {
    AdapterInfo info;
    const auto& properties=vulkan.properties();
    const char* name=properties.deviceName;
    const int length=MultiByteToWideChar(CP_UTF8,0,name,-1,nullptr,0);
    info.name.resize(length>0?size_t(length-1):0);
    if(length>1) MultiByteToWideChar(CP_UTF8,0,name,-1,info.name.data(),length);
    info.vendor=properties.vendorID;info.deviceId=properties.deviceID;
    if(vulkan.luidValid()) info.luid=vulkan.adapterLuid();
    VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(vulkan.physical(),&memory);
    for(uint32_t i=0;i<memory.memoryHeapCount;++i) {
        if(memory.memoryHeaps[i].flags&VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) info.dedicatedMemory=std::max(info.dedicatedMemory,uint64_t(memory.memoryHeaps[i].size));
        else info.sharedMemory=std::max(info.sharedMemory,uint64_t(memory.memoryHeaps[i].size));
    }
    return info;
}
template<class Desc> void fillAdapterDesc(const AdapterInfo& info,Desc& desc) {
    desc={};
    wcsncpy_s(desc.Description,info.name.c_str(),_TRUNCATE);
    desc.VendorId=info.vendor;desc.DeviceId=info.deviceId;desc.AdapterLuid=info.luid;
    desc.DedicatedVideoMemory=SIZE_T(info.dedicatedMemory);desc.SharedSystemMemory=SIZE_T(info.sharedMemory);
}
D3D11Device* vulkanDevice(IUnknown* object) {
    if(!object) return nullptr;
    void* result=nullptr;
    if(FAILED(object->QueryInterface(__uuidof(D3D11Device),&result))) return nullptr;
    static_cast<D3D11Device*>(result)->Release();
    return static_cast<D3D11Device*>(result);
}
}

// ---------------------------------------------------------------- factory/adapter/device
HRESULT DxgiFactory::QueryInterface(REFIID riid,void** object) {
    if(!object) return E_POINTER;
    *object=nullptr;
    if(riid==__uuidof(IUnknown) || riid==__uuidof(IDXGIObject) || riid==__uuidof(IDXGIFactory) || riid==__uuidof(IDXGIFactory1) ||
       riid==__uuidof(IDXGIFactory2) || riid==__uuidof(IDXGIFactory3) || riid==__uuidof(IDXGIFactory4) || riid==__uuidof(IDXGIFactory5)) {
        *object=static_cast<IDXGIFactory5*>(this);AddRef();return S_OK;
    }
    return E_NOINTERFACE;
}
HRESULT DxgiFactory::GetParent(REFIID,void** parent) {if(parent) *parent=nullptr;return E_NOINTERFACE;}
HRESULT DxgiFactory::EnumAdapters(UINT index,IDXGIAdapter** adapter) {
    if(!adapter) return E_INVALIDARG;
    if(index) return DXGI_ERROR_NOT_FOUND;
    *adapter=new DxgiAdapter(adapter_);return S_OK;
}
HRESULT DxgiFactory::EnumAdapters1(UINT index,IDXGIAdapter1** adapter) {
    if(!adapter) return E_INVALIDARG;
    if(index) return DXGI_ERROR_NOT_FOUND;
    *adapter=new DxgiAdapter(adapter_);return S_OK;
}
HRESULT DxgiFactory::EnumAdapterByLuid(LUID luid,REFIID riid,void** adapter) {
    if(!adapter) return E_INVALIDARG;
    if(luid.LowPart!=adapter_.luid.LowPart || luid.HighPart!=adapter_.luid.HighPart) return DXGI_ERROR_NOT_FOUND;
    ComPtr<DxgiAdapter> created;created.Attach(new DxgiAdapter(adapter_));
    return created->QueryInterface(riid,adapter);
}
HRESULT DxgiFactory::MakeWindowAssociation(HWND window,UINT) {association_=window;return S_OK;}
HRESULT DxgiFactory::GetWindowAssociation(HWND* window) {if(!window) return E_INVALIDARG;*window=association_;return S_OK;}
HRESULT DxgiFactory::CreateSwapChain(IUnknown*,DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**) {
    reportUnsupported("legacy IDXGIFactory::CreateSwapChain");return E_NOTIMPL;
}
HRESULT DxgiFactory::CreateSoftwareAdapter(HMODULE,IDXGIAdapter**) {return E_NOTIMPL;}
HRESULT DxgiFactory::CreateSwapChainForHwnd(IUnknown* device,HWND window,const DXGI_SWAP_CHAIN_DESC1* desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,IDXGIOutput* output,IDXGISwapChain1** swapChain) {
    if(!device || !window || !desc || !swapChain) return E_INVALIDARG;
    *swapChain=nullptr;
    auto* vulkan=vulkanDevice(device);
    if(!vulkan) {std::fprintf(stderr,"[NATIVE VULKAN] swap chain device is not the Vulkan device\n");return E_INVALIDARG;}
    if((fullscreen && !fullscreen->Windowed) || output) {reportUnsupported("exclusive fullscreen swap chains");return E_NOTIMPL;}
    if(vulkan->swapChain) return DXGI_ERROR_INVALID_CALL;
    try {*swapChain=new DxgiSwapChain(vulkan,window,*desc);return S_OK;}
    catch(const std::exception& error) {
        std::fprintf(stderr,"[NATIVE VULKAN] swap chain creation failed: %s\n",error.what());
        return E_FAIL;
    }
}
HRESULT DxgiFactory::CheckFeatureSupport(DXGI_FEATURE feature,void* data,UINT size) {
    if(feature==DXGI_FEATURE_PRESENT_ALLOW_TEARING && data && size==sizeof(BOOL)) {
        // Interval-0 presents with ALLOW_TEARING use VK_PRESENT_MODE_IMMEDIATE.
        *static_cast<BOOL*>(data)=TRUE;return S_OK;
    }
    return E_INVALIDARG;
}
HRESULT DxgiAdapter::QueryInterface(REFIID riid,void** object) {
    if(!object) return E_POINTER;
    *object=nullptr;
    if(riid==__uuidof(IUnknown) || riid==__uuidof(IDXGIObject) || riid==__uuidof(IDXGIAdapter) || riid==__uuidof(IDXGIAdapter1)) {
        *object=static_cast<IDXGIAdapter1*>(this);AddRef();return S_OK;
    }
    return E_NOINTERFACE;
}
HRESULT DxgiAdapter::GetParent(REFIID riid,void** parent) {
    if(!parent) return E_INVALIDARG;
    ComPtr<DxgiFactory> factory;factory.Attach(new DxgiFactory(info_));
    return factory->QueryInterface(riid,parent);
}
HRESULT DxgiAdapter::GetDesc(DXGI_ADAPTER_DESC* desc) {if(!desc) return E_INVALIDARG;fillAdapterDesc(info_,*desc);return S_OK;}
HRESULT DxgiAdapter::GetDesc1(DXGI_ADAPTER_DESC1* desc) {if(!desc) return E_INVALIDARG;fillAdapterDesc(info_,*desc);desc->Flags=0;return S_OK;}
HRESULT DxgiAdapter::CheckInterfaceSupport(REFGUID,LARGE_INTEGER*) {return DXGI_ERROR_UNSUPPORTED;}
DxgiDevice::DxgiDevice(D3D11Device& device):device_(device) {
    adapter_.Attach(new DxgiAdapter(adapterInfo(device.vulkan())));
}
HRESULT DxgiDevice::SetMaximumFrameLatency(UINT latency) {
    if(latency>DXGI_MAX_SWAP_CHAIN_BUFFERS) return E_INVALIDARG;
    device_.setMaximumFrameLatency(latency?latency:3);
    return S_OK;
}
HRESULT DxgiDevice::GetMaximumFrameLatency(UINT* latency) {
    if(!latency) return E_INVALIDARG;
    *latency=device_.maximumFrameLatency();
    return S_OK;
}

// ---------------------------------------------------------------- swap chain
DxgiSwapChain::DxgiSwapChain(D3D11Device* device,HWND window,const DXGI_SWAP_CHAIN_DESC1& desc)
    :deviceReference_(device),device_(device),window_(window),desc_(desc) {
    if(desc_.SwapEffect!=DXGI_SWAP_EFFECT_FLIP_DISCARD && desc_.SwapEffect!=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL)
        throw Error("Native Vulkan presentation implements flip-model swap chains only");
    if(desc_.SampleDesc.Count!=1 || desc_.Stereo || desc_.BufferCount<2 || desc_.BufferCount>DXGI_MAX_SWAP_CHAIN_BUFFERS)
        throw Error("Native Vulkan swap chain descriptor is unsupported");
    if(desc_.Flags&~UINT(DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)) throw Error("Native Vulkan swap chain flags are unsupported");
    if(desc_.Format!=DXGI_FORMAT_R10G10B10A2_UNORM && desc_.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && desc_.Format!=DXGI_FORMAT_B8G8R8A8_UNORM)
        throw Error("Native Vulkan swap chain format is unsupported");
    if(!desc_.Width || !desc_.Height) {
        RECT client{};GetClientRect(window_,&client);
        if(!desc_.Width) desc_.Width=std::max<LONG>(client.right-client.left,1);
        if(!desc_.Height) desc_.Height=std::max<LONG>(client.bottom-client.top,1);
    }
    auto& vulkan=device_->vulkan();
    VkWin32SurfaceCreateInfoKHR surface{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    surface.hinstance=GetModuleHandleW(nullptr);surface.hwnd=window_;
    check(vkCreateWin32SurfaceKHR(vulkan.instance(),&surface,nullptr,&surface_),"window surface creation");
    VkBool32 supported=VK_FALSE;
    check(vkGetPhysicalDeviceSurfaceSupportKHR(vulkan.physical(),vulkan.queueFamily(),surface_,&supported),"surface support query");
    if(!supported) {vkDestroySurfaceKHR(vulkan.instance(),surface_,nullptr);throw Error("Native Vulkan queue cannot present to this window");}
    uint32_t count=0;vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan.physical(),surface_,&count,nullptr);
    modes_.resize(count);vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan.physical(),surface_,&count,modes_.data());
    count=0;vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan.physical(),surface_,&count,nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan.physical(),surface_,&count,formats.data());
    // Same packed format as the D3D11 swap chain when the surface offers it;
    // otherwise the presentation blit converts (and says so).
    const VkFormat wanted=desc_.Format==DXGI_FORMAT_R10G10B10A2_UNORM?VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        desc_.Format==DXGI_FORMAT_R8G8B8A8_UNORM?VK_FORMAT_R8G8B8A8_UNORM:VK_FORMAT_B8G8R8A8_UNORM;
    for(const VkFormat candidate:{wanted,VK_FORMAT_A2R10G10B10_UNORM_PACK32,VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM}) {
        for(const auto& format:formats) if(format.format==candidate && format.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {format_=candidate;break;}
        if(format_!=VK_FORMAT_UNDEFINED) break;
    }
    if(format_==VK_FORMAT_UNDEFINED) {vkDestroySurfaceKHR(vulkan.instance(),surface_,nullptr);throw Error("Native Vulkan surface offers no SDR presentation format");}
    if(format_!=wanted) std::fprintf(stderr,"[NATIVE VULKAN] surface lacks the swap chain format; presenting through VkFormat %d\n",int(format_));
    // Display timestamps (VK_EXT_present_timing) replace DXGI frame statistics.
    if(vulkan.capabilities().presentTiming && vkGetPhysicalDeviceSurfaceCapabilities2KHR) {
        VkPresentTimingSurfaceCapabilitiesEXT timing{VK_STRUCTURE_TYPE_PRESENT_TIMING_SURFACE_CAPABILITIES_EXT};
        VkSurfaceCapabilitiesPresentId2KHR ids{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_ID_2_KHR};ids.pNext=&timing;
        VkSurfaceCapabilities2KHR capabilities{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR};capabilities.pNext=&ids;
        VkPhysicalDeviceSurfaceInfo2KHR info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR};info.surface=surface_;
        if(vkGetPhysicalDeviceSurfaceCapabilities2KHR(vulkan.physical(),&info,&capabilities)==VK_SUCCESS &&
           ids.presentId2Supported && timing.presentTimingSupported) {
            stage_=(timing.presentStageQueries&VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT)?VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT:
                (timing.presentStageQueries&VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT)?VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT:0;
            setTimingQueueSize_=reinterpret_cast<PFN_vkSetSwapchainPresentTimingQueueSizeEXT>(vkGetDeviceProcAddr(vulkan.device(),"vkSetSwapchainPresentTimingQueueSizeEXT"));
            timeDomains_=reinterpret_cast<PFN_vkGetSwapchainTimeDomainPropertiesEXT>(vkGetDeviceProcAddr(vulkan.device(),"vkGetSwapchainTimeDomainPropertiesEXT"));
            pastTiming_=reinterpret_cast<PFN_vkGetPastPresentationTimingEXT>(vkGetDeviceProcAddr(vulkan.device(),"vkGetPastPresentationTimingEXT"));
            calibrate_=reinterpret_cast<PFN_vkGetCalibratedTimestampsKHR>(vkGetDeviceProcAddr(vulkan.device(),"vkGetCalibratedTimestampsKHR"));
            timing_=stage_ && setTimingQueueSize_ && timeDomains_ && pastTiming_;
        }
    }
    if(!timing_) std::fprintf(stderr,"[NATIVE VULKAN] display timing unavailable; frame statistics report disjoint\n");
    createBackbuffer();
    device_->swapChain=this;
}
DxgiSwapChain::~DxgiSwapChain() {
    auto& vulkan=device_->vulkan();
    if(device_->swapChain==this) device_->swapChain=nullptr;
    vkQueueWaitIdle(vulkan.queue());
    destroySwapchain();
    for(auto& acquire:acquires_) vkDestroySemaphore(vulkan.device(),acquire.semaphore,nullptr);
    if(surface_) vkDestroySurfaceKHR(vulkan.instance(),surface_,nullptr);
}
HRESULT DxgiSwapChain::QueryInterface(REFIID riid,void** object) {
    if(!object) return E_POINTER;
    *object=nullptr;
    if(riid==__uuidof(IUnknown) || riid==__uuidof(IDXGIObject) || riid==__uuidof(IDXGIDeviceSubObject) || riid==__uuidof(IDXGISwapChain) ||
       riid==__uuidof(IDXGISwapChain1) || riid==__uuidof(IDXGISwapChain2) || riid==__uuidof(IDXGISwapChain3)) {
        *object=static_cast<IDXGISwapChain3*>(this);AddRef();return S_OK;
    }
    return E_NOINTERFACE;
}
HRESULT DxgiSwapChain::GetParent(REFIID riid,void** parent) {
    if(!parent) return E_INVALIDARG;
    ComPtr<DxgiFactory> factory;factory.Attach(new DxgiFactory(device_->dxgi().adapter().info()));
    return factory->QueryInterface(riid,parent);
}
HRESULT DxgiSwapChain::GetDevice(REFIID riid,void** device) {return deviceReference_->QueryInterface(riid,device);}
void DxgiSwapChain::createBackbuffer() {
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width=desc_.Width;texture.Height=desc_.Height;texture.MipLevels=1;texture.ArraySize=1;texture.Format=desc_.Format;
    texture.SampleDesc={1,0};texture.Usage=D3D11_USAGE_DEFAULT;
    texture.BindFlags=D3D11_BIND_RENDER_TARGET|((desc_.BufferUsage&DXGI_USAGE_SHADER_INPUT)?D3D11_BIND_SHADER_RESOURCE:0);
    backbuffer_.Attach(new D3D11Texture2D(device_,texture,nullptr));
}
HRESULT DxgiSwapChain::GetBuffer(UINT buffer,REFIID riid,void** surface) {
    if(!surface) return E_INVALIDARG;
    if(buffer) return DXGI_ERROR_INVALID_CALL; // Flip-discard exposes buffer 0 only.
    return backbuffer_->QueryInterface(riid,surface);
}
HRESULT DxgiSwapChain::SetFullscreenState(BOOL fullscreen,IDXGIOutput*) {
    return fullscreen?DXGI_ERROR_NOT_CURRENTLY_AVAILABLE:S_OK;
}
HRESULT DxgiSwapChain::GetFullscreenState(BOOL* fullscreen,IDXGIOutput** target) {
    if(fullscreen) *fullscreen=FALSE;
    if(target) *target=nullptr;
    return S_OK;
}
HRESULT DxgiSwapChain::GetDesc(DXGI_SWAP_CHAIN_DESC* desc) {
    if(!desc) return E_INVALIDARG;
    *desc={};
    desc->BufferDesc.Width=desc_.Width;desc->BufferDesc.Height=desc_.Height;desc->BufferDesc.Format=desc_.Format;
    desc->BufferDesc.Scaling=DXGI_MODE_SCALING_UNSPECIFIED;desc->SampleDesc=desc_.SampleDesc;desc->BufferUsage=desc_.BufferUsage;
    desc->BufferCount=desc_.BufferCount;desc->OutputWindow=window_;desc->Windowed=TRUE;desc->SwapEffect=desc_.SwapEffect;desc->Flags=desc_.Flags;
    return S_OK;
}
HRESULT DxgiSwapChain::GetDesc1(DXGI_SWAP_CHAIN_DESC1* desc) {if(!desc) return E_INVALIDARG;*desc=desc_;return S_OK;}
HRESULT DxgiSwapChain::GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* desc) {
    if(!desc) return E_INVALIDARG;
    *desc={};desc->Windowed=TRUE;
    return S_OK;
}
HRESULT DxgiSwapChain::GetHwnd(HWND* window) {if(!window) return E_INVALIDARG;*window=window_;return S_OK;}
HRESULT DxgiSwapChain::ResizeBuffers(UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags) {
    // DXGI requires every outside reference to the buffers to be released.
    const ULONG references=backbuffer_->AddRef();backbuffer_->Release();
    if(references>2) return DXGI_ERROR_INVALID_CALL;
    if(flags&~UINT(DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)) return E_INVALIDARG;
    if(count) desc_.BufferCount=count;
    if(format!=DXGI_FORMAT_UNKNOWN) desc_.Format=format;
    RECT client{};GetClientRect(window_,&client);
    desc_.Width=width?width:std::max<LONG>(client.right-client.left,1);
    desc_.Height=height?height:std::max<LONG>(client.bottom-client.top,1);
    desc_.Flags=flags;
    device_->immediate().endRendering();
    backbuffer_.Reset();
    try {createBackbuffer();}
    catch(const std::exception& error) {std::fprintf(stderr,"[NATIVE VULKAN] swap chain resize failed: %s\n",error.what());return E_FAIL;}
    return S_OK;
}
HRESULT DxgiSwapChain::CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE colorSpace,UINT* support) {
    if(!support) return E_INVALIDARG;
    *support=colorSpace==DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709?DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT:0;
    return S_OK;
}
HRESULT DxgiSwapChain::SetColorSpace1(DXGI_COLOR_SPACE_TYPE colorSpace) {
    // VK_COLOR_SPACE_SRGB_NONLINEAR_KHR is the same full-range G22/P709 SDR space.
    if(colorSpace!=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) return E_INVALIDARG;
    colorSpace_=colorSpace;
    return S_OK;
}
VkPresentModeKHR DxgiSwapChain::presentMode(UINT interval,UINT flags) const {
    auto offered=[&](VkPresentModeKHR mode){return std::find(modes_.begin(),modes_.end(),mode)!=modes_.end();};
    if(interval) return VK_PRESENT_MODE_FIFO_KHR;
    if((flags&DXGI_PRESENT_ALLOW_TEARING) && offered(VK_PRESENT_MODE_IMMEDIATE_KHR)) return VK_PRESENT_MODE_IMMEDIATE_KHR;
    // Flip-model interval 0 without tearing replaces queued frames at vblank.
    if(offered(VK_PRESENT_MODE_MAILBOX_KHR)) return VK_PRESENT_MODE_MAILBOX_KHR;
    if(offered(VK_PRESENT_MODE_IMMEDIATE_KHR)) return VK_PRESENT_MODE_IMMEDIATE_KHR;
    return VK_PRESENT_MODE_FIFO_KHR;
}
void DxgiSwapChain::destroySwapchain() {
    auto& vulkan=device_->vulkan();
    for(auto semaphore:rendered_) vkDestroySemaphore(vulkan.device(),semaphore,nullptr);
    rendered_.clear();images_.clear();
    if(swapchain_) vkDestroySwapchainKHR(vulkan.device(),swapchain_,nullptr);
    swapchain_=VK_NULL_HANDLE;
}
bool DxgiSwapChain::recreate(VkPresentModeKHR mode) {
    auto& vulkan=device_->vulkan();
    VkSurfaceCapabilitiesKHR capabilities{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan.physical(),surface_,&capabilities),"surface capability query");
    VkExtent2D extent=capabilities.currentExtent;
    if(extent.width==0xFFFFFFFFu) extent={desc_.Width,desc_.Height};
    if(!extent.width || !extent.height) return false;
    // Rare (resize, VSync toggle): retire the old chain with the queue idle.
    if(swapchain_) vkQueueWaitIdle(vulkan.queue());
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface=surface_;
    info.minImageCount=std::max(capabilities.minImageCount,3u);
    if(capabilities.maxImageCount) info.minImageCount=std::min(info.minImageCount,capabilities.maxImageCount);
    info.imageFormat=format_;info.imageColorSpace=surfaceColorSpace_;info.imageExtent=extent;info.imageArrayLayers=1;
    info.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;info.preTransform=capabilities.currentTransform;
    info.compositeAlpha=(capabilities.supportedCompositeAlpha&VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)?VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR:
        VkCompositeAlphaFlagBitsKHR(capabilities.supportedCompositeAlpha&-int32_t(capabilities.supportedCompositeAlpha));
    info.presentMode=mode;info.clipped=VK_TRUE;info.oldSwapchain=swapchain_;
    const bool timed=timing_ && timingWanted_;
    if(timed) info.flags|=VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR|VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT;
    VkSwapchainKHR created=VK_NULL_HANDLE;
    check(vkCreateSwapchainKHR(vulkan.device(),&info,nullptr,&created),"swap chain creation");
    destroySwapchain();
    swapchain_=created;extent_=extent;mode_=mode;outdated_=false;
    uint32_t count=0;vkGetSwapchainImagesKHR(vulkan.device(),swapchain_,&count,nullptr);
    images_.resize(count);vkGetSwapchainImagesKHR(vulkan.device(),swapchain_,&count,images_.data());
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    rendered_.resize(count);
    for(auto& rendered:rendered_) check(vkCreateSemaphore(vulkan.device(),&semaphore,nullptr,&rendered),"present semaphore creation");
    while(acquires_.size()<count+1) {
        Acquire acquire;check(vkCreateSemaphore(vulkan.device(),&semaphore,nullptr,&acquire.semaphore),"acquire semaphore creation");
        acquires_.push_back(acquire);
    }
    statisticsValid_=false;
    timingActive_=false;
    if(timed) {
        timeDomain_=0;bool found=false;
        timingOutstanding_=0;timingFullSince_=0;
        if(setTimingQueueSize_(vulkan.device(),swapchain_,timingQueueSize_)==VK_SUCCESS) {
            VkSwapchainTimeDomainPropertiesEXT domains{VK_STRUCTURE_TYPE_SWAPCHAIN_TIME_DOMAIN_PROPERTIES_EXT};
            uint64_t counter=0;
            if(timeDomains_(vulkan.device(),swapchain_,&domains,&counter)>=0 && domains.timeDomainCount) {
                std::vector<VkTimeDomainKHR> kinds(domains.timeDomainCount);std::vector<uint64_t> ids(domains.timeDomainCount);
                domains.pTimeDomains=kinds.data();domains.pTimeDomainIds=ids.data();
                if(timeDomains_(vulkan.device(),swapchain_,&domains,&counter)>=0) {
                    // Prefer QueryPerformanceCounter itself, then a calibrated local domain.
                    for(const VkTimeDomainKHR wanted:{VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR,VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT,VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT}) {
                        for(uint32_t i=0;i<domains.timeDomainCount && !found;++i) if(kinds[i]==wanted) {
                            timeDomain_=ids[i];timeKind_=wanted;
                            found=wanted==VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR || calibrate();
                        }
                        if(found) break;
                    }
                }
            }
        }
        if(!found) {
            std::fprintf(stderr,"[NATIVE VULKAN] swap chain offers no QueryPerformanceCounter-convertible time domain; frame statistics disabled\n");
            timing_=false;
        }
        timingActive_=found;
    }
    std::fprintf(stderr,"[NATIVE VULKAN] swap chain %ux%u images=%u mode=%d format=%d timing=%u time_domain=%d\n",
        extent.width,extent.height,count,int(mode),int(format_),unsigned(timingActive_),timingActive_?int(timeKind_):-1);
    return true;
}
HRESULT DxgiSwapChain::Present1(UINT interval,UINT flags,const DXGI_PRESENT_PARAMETERS*) {return Present(interval,flags);}
HRESULT DxgiSwapChain::Present(UINT interval,UINT flags) {
    if(interval>4) return DXGI_ERROR_INVALID_CALL;
    if(flags&DXGI_PRESENT_TEST) return S_OK;
    auto& vulkan=device_->vulkan();
    auto& context=device_->immediate();
    if(vulkan.lost()) return DXGI_ERROR_DEVICE_REMOVED;
    RECT client{};GetClientRect(window_,&client);
    if(IsIconic(window_) || client.right<=client.left || client.bottom<=client.top) return DXGI_STATUS_OCCLUDED;
    try {
        const VkPresentModeKHR mode=presentMode(interval,flags);
        VkSurfaceCapabilitiesKHR capabilities{};
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan.physical(),surface_,&capabilities),"surface capability query");
        const bool resized=capabilities.currentExtent.width!=0xFFFFFFFFu &&
            (capabilities.currentExtent.width!=extent_.width || capabilities.currentExtent.height!=extent_.height);
        // Requests for frames that never reached the display stay queued;
        // GetFrameStatistics drains the rest.
        if(timingOutstanding_>=timingQueueSize_/2) collectTimings();
        bool timingStuck=false;
        if(timingActive_ && timingOutstanding_>=timingQueueSize_) {
            LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
            if(!timingFullSince_) timingFullSince_=now.QuadPart;
            timingStuck=now.QuadPart-timingFullSince_>int64_t(timingBackoff_)*frequency.QuadPart;
            if(timingStuck) timingBackoff_=std::min(timingBackoff_*2,64u);
        } else timingFullSince_=0;
        if(!swapchain_ || outdated_ || mode!=mode_ || resized || timingStuck)
            if(!recreate(mode)) return DXGI_STATUS_OCCLUDED;
        uint32_t index=0;
        Acquire* acquire=nullptr;
        for(int attempt=0;;++attempt) {
            acquire=&acquires_[nextAcquire_];nextAcquire_=(nextAcquire_+1)%acquires_.size();
            // The semaphore is reusable once the batch that waited on it is done.
            if(acquire->serial && !vulkan.wait(acquire->serial,fiveSeconds)) throw Error("Native Vulkan acquire semaphore never retired");
            VkResult result;
            {
                StallProfiler::Scope profile(StallProfiler::Section::Present,"vkAcquireNextImageKHR",nullptr,0);
                result=vkAcquireNextImageKHR(vulkan.device(),swapchain_,fiveSeconds,acquire->semaphore,VK_NULL_HANDLE,&index);
            }
            if(result==VK_ERROR_OUT_OF_DATE_KHR && attempt==0) {acquire->serial=0;if(!recreate(mode)) return DXGI_STATUS_OCCLUDED;continue;}
            if(result==VK_SUBOPTIMAL_KHR) outdated_=true;
            else check(result,"swap chain image acquisition");
            break;
        }
        // Backbuffer (always GENERAL) -> swap image, stretched like DXGI_SCALING_STRETCH.
        context.endRendering();context.barrier();
        const VkCommandBuffer command=vulkan.commands();
        const VkImage image=images_[index];
        VkImageMemoryBarrier2 toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        toTransfer.srcStageMask=VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;toTransfer.dstStageMask=VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        toTransfer.dstAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;toTransfer.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;toTransfer.srcQueueFamilyIndex=toTransfer.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image=image;toTransfer.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dependency.imageMemoryBarrierCount=1;dependency.pImageMemoryBarriers=&toTransfer;
        vkCmdPipelineBarrier2(command,&dependency);
        const auto& source=backbuffer_->desc();
        if(source.Width==extent_.width && source.Height==extent_.height && backbuffer_->format().format==format_) {
            VkImageCopy region{};
            region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.extent={extent_.width,extent_.height,1};
            vkCmdCopyImage(command,backbuffer_->image(),VK_IMAGE_LAYOUT_GENERAL,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
        } else {
            VkImageBlit region{};
            region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
            region.srcOffsets[1]={int32_t(source.Width),int32_t(source.Height),1};
            region.dstOffsets[1]={int32_t(extent_.width),int32_t(extent_.height),1};
            vkCmdBlitImage(command,backbuffer_->image(),VK_IMAGE_LAYOUT_GENERAL,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region,VK_FILTER_LINEAR);
        }
        VkImageMemoryBarrier2 toPresent=toTransfer;
        toPresent.srcStageMask=VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;toPresent.srcAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toPresent.dstStageMask=VK_PIPELINE_STAGE_2_NONE;toPresent.dstAccessMask=0;
        toPresent.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;toPresent.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        dependency.pImageMemoryBarriers=&toPresent;
        vkCmdPipelineBarrier2(command,&dependency);
        const VulkanDevice::Wait wait{acquire->semaphore,VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT};
        const VkSemaphore rendered=rendered_[index];
        context.prepareSubmit();
        const uint64_t serial=vulkan.submit({&wait,1},{&rendered,1});
        context.afterSubmit();
        acquire->serial=serial;
        const uint64_t id=presents_+1;
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount=1;present.pWaitSemaphores=&rendered;
        present.swapchainCount=1;present.pSwapchains=&swapchain_;present.pImageIndices=&index;
        VkPresentId2KHR ids{VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR};ids.swapchainCount=1;ids.pPresentIds=&id;
        VkPresentTimingInfoEXT timingInfo{VK_STRUCTURE_TYPE_PRESENT_TIMING_INFO_EXT};
        timingInfo.timeDomainId=timeDomain_;timingInfo.presentStageQueries=stage_;
        VkPresentTimingsInfoEXT timings{VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT};timings.swapchainCount=1;timings.pTimingInfos=&timingInfo;
        const bool requestTiming=timingActive_ && timingOutstanding_<timingQueueSize_ &&
            statisticsAskedAt_!=~0ull && presents_-statisticsAskedAt_<64;
        if(timingActive_) present.pNext=&ids;
        if(requestTiming) ids.pNext=&timings;
        VkResult result;
        {
            StallProfiler::Scope profile(StallProfiler::Section::Present,"vkQueuePresentKHR",nullptr,0);
            result=vkQueuePresentKHR(vulkan.queue(),&present);
        }
        if(result==VK_ERROR_OUT_OF_DATE_KHR || result==VK_SUBOPTIMAL_KHR) outdated_=true;
        // Not expected with the outstanding count; a fresh chain resets the queue.
        else if(result==VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT) {outdated_=true;timingOutstanding_=timingQueueSize_;}
        else if(result==VK_ERROR_DEVICE_LOST) {vulkan.markLost();return DXGI_ERROR_DEVICE_REMOVED;}
        else check(result,"presentation");
        if(requestTiming && result>=0) ++timingOutstanding_;
        presents_=id;
        // IDXGIDevice1::SetMaximumFrameLatency: Present returns once at most
        // that many presented frames are still queued on the GPU.
        frameSerials_.push_back(serial);
        const uint32_t latency=std::max(1u,device_->maximumFrameLatency());
        while(!frameSerials_.empty() && vulkan.complete(frameSerials_.front())) frameSerials_.pop_front();
        while(frameSerials_.size()>latency) {
            StallProfiler::Scope profile(StallProfiler::Section::Present,"Vulkan.frameLatency",nullptr,latency);
            if(!vulkan.wait(frameSerials_.front(),fiveSeconds)) throw Error("Native Vulkan frame latency wait exceeded 5000 ms");
            frameSerials_.pop_front();
        }
    } catch(const std::exception& error) {
        std::fprintf(stderr,"[NATIVE VULKAN] present failed: %s\n",error.what());
        return vulkan.lost()?DXGI_ERROR_DEVICE_REMOVED:E_FAIL;
    }
    return S_OK;
}
// Pairs the swap chain's local clock with QueryPerformanceCounter.
bool DxgiSwapChain::calibrate() {
    if(!calibrate_) return false;
    VkSwapchainCalibratedTimestampInfoEXT local{VK_STRUCTURE_TYPE_SWAPCHAIN_CALIBRATED_TIMESTAMP_INFO_EXT};
    local.swapchain=swapchain_;local.presentStage=stage_;local.timeDomainId=timeDomain_;
    std::array<VkCalibratedTimestampInfoKHR,2> infos{};
    infos[0]={VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,&local,timeKind_};
    infos[1]={VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,nullptr,VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR};
    std::array<uint64_t,2> stamps{};uint64_t deviation=0;
    if(calibrate_(device_->vulkan().device(),2,infos.data(),stamps.data(),&deviation)!=VK_SUCCESS || !stamps[0] || !stamps[1]) return false;
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
    calibratedTime_=stamps[0];calibratedQpc_=stamps[1];qpcPerNanosecond_=double(frequency.QuadPart)/1e9;
    return true;
}
void DxgiSwapChain::collectTimings() {
    if(!timingActive_ || !swapchain_) return;
    auto& vulkan=device_->vulkan();
    // Clock drift between calibrations stays far below a frame; refresh each second.
    if(timeKind_!=VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR) {
        LARGE_INTEGER now{};QueryPerformanceCounter(&now);
        if(uint64_t(now.QuadPart)-calibratedQpc_>uint64_t(qpcPerNanosecond_*1e9)) calibrate();
    }
    constexpr uint32_t capacity=16;
    std::array<VkPastPresentationTimingEXT,capacity> timings{};
    std::array<std::array<VkPresentStageTimeEXT,4>,capacity> stages{};
    for(uint32_t i=0;i<capacity;++i) {
        timings[i].sType=VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_EXT;
        timings[i].presentStageCount=uint32_t(stages[i].size());timings[i].pPresentStages=stages[i].data();
    }
    VkPastPresentationTimingInfoEXT info{VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_INFO_EXT};
    info.flags=VK_PAST_PRESENTATION_TIMING_ALLOW_OUT_OF_ORDER_RESULTS_BIT_EXT;info.swapchain=swapchain_;
    for(int batch=0;batch<4;++batch) {
        VkPastPresentationTimingPropertiesEXT properties{VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_PROPERTIES_EXT};
        properties.presentationTimingCount=capacity;properties.pPresentationTimings=timings.data();
        for(uint32_t i=0;i<capacity;++i) timings[i].presentStageCount=uint32_t(stages[i].size());
        const VkResult result=pastTiming_(vulkan.device(),&info,&properties);
        if(result<0) return;
        for(uint32_t i=0;i<properties.presentationTimingCount && i<capacity;++i) {
            const auto& timing=timings[i];
            // Without partial results, each returned entry has left the queue.
            if(timingOutstanding_) --timingOutstanding_;
            timingBackoff_=2;
            if(!timing.reportComplete || timing.timeDomain!=timeKind_ || timing.timeDomainId!=timeDomain_) continue;
            for(uint32_t s=0;s<timing.presentStageCount;++s) {
                const auto& stage=timing.pPresentStages[s];
                if(!(stage.stage&stage_) || !stage.time) continue;
                // DXGI semantics: the latest frame that reached the display, by
                // its Present count, at that QueryPerformanceCounter time.
                if(!statisticsValid_ || timing.presentId>=statistics_.PresentCount) {
                    statistics_.PresentCount=UINT(timing.presentId);
                    statistics_.SyncQPCTime.QuadPart=timeKind_==VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR?LONGLONG(stage.time):
                        LONGLONG(calibratedQpc_)+LONGLONG(std::llround(double(int64_t(stage.time-calibratedTime_))*qpcPerNanosecond_));
                    statisticsValid_=true;
                }
            }
        }
        if(result!=VK_INCOMPLETE) break;
    }
}
HRESULT DxgiSwapChain::GetFrameStatistics(DXGI_FRAME_STATISTICS* stats) {
    if(!stats) return E_INVALIDARG;
    statisticsAskedAt_=presents_;
    if(timing_ && !timingWanted_) {timingWanted_=true;outdated_=true;}
    collectTimings();
    if(!statisticsValid_) return DXGI_ERROR_FRAME_STATISTICS_DISJOINT;
    *stats=statistics_;
    return S_OK;
}
HRESULT DxgiSwapChain::GetLastPresentCount(UINT* count) {
    if(!count) return E_INVALIDARG;
    *count=UINT(presents_);
    return S_OK;
}
}
