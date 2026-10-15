#pragma once
// DXGI objects behind a Vulkan-backed D3D11 device: enough of IDXGIDevice1,
// IDXGIAdapter1, IDXGIFactory5 and IDXGISwapChain3 for the renderer's
// flip-model presentation, frame-latency and frame-statistics contract.
#include "d3d11_vulkan.h"
#include <dxgi1_5.h>

namespace Simpsons::Graphics::Vulkan {
template<class Base>
class ComObject : public Base {
public:
    virtual ~ComObject()=default;
    ULONG STDMETHODCALLTYPE AddRef() override {return refs_.fetch_add(1,std::memory_order_relaxed)+1;}
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left=refs_.fetch_sub(1,std::memory_order_acq_rel)-1;
        if(!left) delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID name,UINT size,const void* data) override {return privateData_.set(name,size,data);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID name,const IUnknown* object) override {return privateData_.setInterface(name,object);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID name,UINT* size,void* data) override {return privateData_.get(name,size,data);}
protected:
    std::atomic<ULONG> refs_{1};
    PrivateData privateData_;
};

struct AdapterInfo {std::wstring name;LUID luid{};uint64_t dedicatedMemory=0,sharedMemory=0;uint32_t vendor=0,deviceId=0;};

class DxgiFactory final : public ComObject<IDXGIFactory5> {
public:
    explicit DxgiFactory(AdapterInfo adapter):adapter_(std::move(adapter)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void** object) override;
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid,void** parent) override;
    HRESULT STDMETHODCALLTYPE EnumAdapters(UINT Adapter,IDXGIAdapter** ppAdapter) override;
    HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND WindowHandle,UINT Flags) override;
    HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND* pWindowHandle) override;
    HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown* pDevice,DXGI_SWAP_CHAIN_DESC* pDesc,IDXGISwapChain** ppSwapChain) override;
    HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE Module,IDXGIAdapter** ppAdapter) override;
    HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT Adapter,IDXGIAdapter1** ppAdapter) override;
    BOOL STDMETHODCALLTYPE IsCurrent() override {return TRUE;}
    BOOL STDMETHODCALLTYPE IsWindowedStereoEnabled() override {return FALSE;}
    HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IUnknown* pDevice,HWND hWnd,const DXGI_SWAP_CHAIN_DESC1* pDesc,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,IDXGIOutput* pRestrictToOutput,IDXGISwapChain1** ppSwapChain) override;
    HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(IUnknown*,IUnknown*,const DXGI_SWAP_CHAIN_DESC1*,IDXGIOutput*,IDXGISwapChain1**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetSharedResourceAdapterLuid(HANDLE,LUID*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusWindow(HWND,UINT,DWORD*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusEvent(HANDLE,DWORD*) override {return E_NOTIMPL;}
    void STDMETHODCALLTYPE UnregisterStereoStatus(DWORD) override {}
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusWindow(HWND,UINT,DWORD*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusEvent(HANDLE,DWORD*) override {return E_NOTIMPL;}
    void STDMETHODCALLTYPE UnregisterOcclusionStatus(DWORD) override {}
    HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(IUnknown*,const DXGI_SWAP_CHAIN_DESC1*,IDXGIOutput*,IDXGISwapChain1**) override {return E_NOTIMPL;}
    UINT STDMETHODCALLTYPE GetCreationFlags() override {return 0;}
    HRESULT STDMETHODCALLTYPE EnumAdapterByLuid(LUID AdapterLuid,REFIID riid,void** ppvAdapter) override;
    HRESULT STDMETHODCALLTYPE EnumWarpAdapter(REFIID,void**) override {return DXGI_ERROR_NOT_FOUND;}
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(DXGI_FEATURE Feature,void* pFeatureSupportData,UINT FeatureSupportDataSize) override;
private:
    AdapterInfo adapter_;
    HWND association_=nullptr;
};

class DxgiAdapter final : public ComObject<IDXGIAdapter1> {
public:
    explicit DxgiAdapter(AdapterInfo info):info_(std::move(info)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void** object) override;
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid,void** parent) override;
    HRESULT STDMETHODCALLTYPE EnumOutputs(UINT,IDXGIOutput**) override {return DXGI_ERROR_NOT_FOUND;}
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_ADAPTER_DESC* pDesc) override;
    HRESULT STDMETHODCALLTYPE CheckInterfaceSupport(REFGUID InterfaceName,LARGE_INTEGER* pUMDVersion) override;
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_ADAPTER_DESC1* pDesc) override;
    const AdapterInfo& info() const {return info_;}
private:
    AdapterInfo info_;
};

// IDXGIDevice1 view of the D3D11 device; IUnknown forwards to the device.
class DxgiDevice final : public IDXGIDevice1 {
public:
    explicit DxgiDevice(D3D11Device& device);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void** object) override {return device_.QueryInterface(riid,object);}
    ULONG STDMETHODCALLTYPE AddRef() override {return device_.AddRef();}
    ULONG STDMETHODCALLTYPE Release() override {return device_.Release();}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID name,UINT size,const void* data) override {return device_.SetPrivateData(name,size,data);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID name,const IUnknown* object) override {return device_.SetPrivateDataInterface(name,object);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID name,UINT* size,void* data) override {return device_.GetPrivateData(name,size,data);}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid,void** parent) override {return adapter_->QueryInterface(riid,parent);}
    HRESULT STDMETHODCALLTYPE GetAdapter(IDXGIAdapter** adapter) override {
        if(!adapter) return E_INVALIDARG;*adapter=adapter_.Get();adapter_->AddRef();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateSurface(const DXGI_SURFACE_DESC*,UINT,DXGI_USAGE,const DXGI_SHARED_RESOURCE*,IDXGISurface**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryResourceResidency(IUnknown* const* resources,DXGI_RESIDENCY* status,UINT count) override {
        if(!resources || !status) return E_INVALIDARG;
        for(UINT i=0;i<count;++i) status[i]=DXGI_RESIDENCY_FULLY_RESIDENT;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetGPUThreadPriority(INT priority) override {return priority>=-7&&priority<=7?S_OK:E_INVALIDARG;}
    HRESULT STDMETHODCALLTYPE GetGPUThreadPriority(INT* priority) override {if(!priority) return E_INVALIDARG;*priority=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT latency) override;
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* latency) override;
    DxgiAdapter& adapter() {return *adapter_.Get();}
private:
    D3D11Device& device_;
    ComPtr<DxgiAdapter> adapter_;
};

class DxgiSwapChain final : public ComObject<IDXGISwapChain3> {
public:
    DxgiSwapChain(D3D11Device* device,HWND window,const DXGI_SWAP_CHAIN_DESC1& desc);
    ~DxgiSwapChain() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void** object) override;
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid,void** parent) override;
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid,void** device) override;
    HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval,UINT Flags) override;
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer,REFIID riid,void** ppSurface) override;
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen,IDXGIOutput* pTarget) override;
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* pFullscreen,IDXGIOutput** ppTarget) override;
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc) override;
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount,UINT Width,UINT Height,DXGI_FORMAT NewFormat,UINT SwapChainFlags) override;
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput**) override {return DXGI_ERROR_NOT_FOUND;}
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats) override;
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* pLastPresentCount) override;
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* pDesc) override;
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc) override;
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND* pHwnd) override;
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID,void**) override {return DXGI_ERROR_INVALID_CALL;}
    HRESULT STDMETHODCALLTYPE Present1(UINT SyncInterval,UINT PresentFlags,const DXGI_PRESENT_PARAMETERS* pPresentParameters) override;
    BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override {return FALSE;}
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** output) override {if(output) *output=nullptr;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION rotation) override {return rotation==DXGI_MODE_ROTATION_IDENTITY?S_OK:E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* rotation) override {if(!rotation) return E_INVALIDARG;*rotation=DXGI_MODE_ROTATION_IDENTITY;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetSourceSize(UINT,UINT) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT*,UINT*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT) override {return DXGI_ERROR_INVALID_CALL;}
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT*) override {return DXGI_ERROR_INVALID_CALL;}
    HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override {return nullptr;}
    HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F*) override {return E_NOTIMPL;}
    UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override {return 0;}
    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace,UINT* pColorSpaceSupport) override;
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) override;
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT,UINT,UINT,DXGI_FORMAT,UINT,const UINT*,IUnknown* const*) override {return E_NOTIMPL;}
private:
    void createBackbuffer();
    bool recreate(VkPresentModeKHR mode);
    void destroySwapchain();
    void collectTimings();
    bool calibrate();
    VkPresentModeKHR presentMode(UINT interval,UINT flags) const;
    ComPtr<ID3D11Device> deviceReference_;
    D3D11Device* const device_;
    const HWND window_;
    DXGI_SWAP_CHAIN_DESC1 desc_;
    DXGI_COLOR_SPACE_TYPE colorSpace_=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    ComPtr<D3D11Texture2D> backbuffer_;
    VkSurfaceKHR surface_=VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_=VK_NULL_HANDLE;
    VkFormat format_=VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR surfaceColorSpace_=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D extent_{};
    VkPresentModeKHR mode_=VK_PRESENT_MODE_FIFO_KHR;
    std::vector<VkPresentModeKHR> modes_;
    std::vector<VkImage> images_;
    std::vector<VkSemaphore> rendered_;
    struct Acquire {VkSemaphore semaphore=VK_NULL_HANDLE;uint64_t serial=0;};
    std::vector<Acquire> acquires_;
    size_t nextAcquire_=0;
    bool outdated_=false;
    uint64_t presents_=0;
    std::deque<uint64_t> frameSerials_;
    // VK_EXT_present_timing: display timestamps stand in for DXGI frame
    // statistics. A swap-chain-local domain (nanoseconds) is mapped onto
    // QueryPerformanceCounter ticks through calibrated timestamps.
    bool timing_=false;       // The surface supports display timing.
    // A timed swap chain makes every vkQueuePresentKHR costlier, so the chain
    // is recreated with timing only once GetFrameStatistics is first used.
    bool timingWanted_=false,timingActive_=false;
    uint64_t timeDomain_=0;
    VkTimeDomainKHR timeKind_=VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR;
    PFN_vkGetCalibratedTimestampsKHR calibrate_=nullptr;
    uint64_t calibratedTime_=0,calibratedQpc_=0;
    // Timing requests not yet returned complete. Presents beyond the queue
    // size go without a request (vkQueuePresentKHR would fail), and a queue
    // that stays full (frames never displayed) is reset by recreation.
    static constexpr uint32_t timingQueueSize_=32;
    uint32_t timingOutstanding_=0;
    int64_t timingFullSince_=0;
    uint32_t timingBackoff_=2; // Seconds before the next reset; doubles while frames stay undisplayed.
    // Display timing costs a driver query per drain, so presents request it
    // only while GetFrameStatistics is in use (the refresh-locked limiter).
    uint64_t statisticsAskedAt_=~0ull; // Present count at the last GetFrameStatistics.
    double qpcPerNanosecond_=0;
    VkPresentStageFlagsEXT stage_=0;
    PFN_vkSetSwapchainPresentTimingQueueSizeEXT setTimingQueueSize_=nullptr;
    PFN_vkGetSwapchainTimeDomainPropertiesEXT timeDomains_=nullptr;
    PFN_vkGetPastPresentationTimingEXT pastTiming_=nullptr;
    bool statisticsValid_=false;
    DXGI_FRAME_STATISTICS statistics_{};
};
}
