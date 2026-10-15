#include "dlss.h"
#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers.h"
#include "runtime/stall_profiler.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Simpsons::Graphics {
namespace {
// This native renderer's own persistent project GUID, not an NVIDIA application ID.
// Project IDs are supported for custom engines by NVIDIA's public NGX API.
constexpr const char* projectId="f719b9c8-30f3-49ee-b9f7-92869239d531";
constexpr const char* engineVersion="SimpsonsNative-1";
void require(bool valid,const char* message) {if(!valid)throw Error(message);}
void check(HRESULT status,const char* operation) {
    if(FAILED(status)) {
        char text[200];std::snprintf(text,sizeof(text),"NVIDIA DLSS %s failed: 0x%08lX",operation,ULONG(status));
        throw Error(text);
    }
}
bool ngxFailed(NVSDK_NGX_Result value) noexcept {return (uint32_t(value)&0xFFF00000u)==uint32_t(NVSDK_NGX_Result_Fail);}
void ngxCheck(NVSDK_NGX_Result result,const char* operation) {
    if(ngxFailed(result)) {
        char text[200];std::snprintf(text,sizeof(text),"NVIDIA DLSS %s failed: NGX 0x%08X",operation,unsigned(result));
        throw Error(text);
    }
}
NVSDK_NGX_PerfQuality_Value quality(Antialiasing mode) {
    switch(mode) {
    case Antialiasing::DLAA:return NVSDK_NGX_PerfQuality_Value_DLAA;
    case Antialiasing::DLSSQuality:return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case Antialiasing::DLSSBalanced:return NVSDK_NGX_PerfQuality_Value_Balanced;
    case Antialiasing::DLSSPerformance:return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case Antialiasing::DLSSUltraPerformance:return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    default:throw Error("NVIDIA DLSS requires a temporal antialiasing mode");
    }
}
// Whole D3D11 state transaction, including UAVs, all shader stages, class instances,
// ranged constant bindings and predication. Engine owners retain their physical state.
// https://learn.microsoft.com/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-swapdevicecontextstate
struct ScopedContext {
    ID3D11DeviceContext1* context;
    ComPtr<ID3DDeviceContextState> previous;
    ScopedContext(ID3D11DeviceContext1* c,ID3DDeviceContextState* isolated):context(c) {
        context->SwapDeviceContextState(isolated,&previous);
    }
    ~ScopedContext() {context->SwapDeviceContextState(previous.Get(),nullptr);}
};
D3D11_TEXTURE2D_DESC descriptor(ID3D11Texture2D* texture,ID3D11Device* expected) {
    require(texture,"NVIDIA DLSS input texture is missing");
    ComPtr<ID3D11Device> actual;texture->GetDevice(&actual);
    require(actual.Get()==expected,"NVIDIA DLSS texture belongs to another graphics device");
    D3D11_TEXTURE2D_DESC value{};texture->GetDesc(&value);
    require(value.MipLevels==1&&value.ArraySize==1&&value.SampleDesc.Count==1&&!value.SampleDesc.Quality&&
        value.Usage==D3D11_USAGE_DEFAULT&&!value.CPUAccessFlags&&!value.MiscFlags&&
        (value.BindFlags&D3D11_BIND_SHADER_RESOURCE),"NVIDIA DLSS requires single-sample GPU-owned sampled inputs");
    return value;
}
}

struct NvidiaDlss::State {
    DWORD owner=GetCurrentThreadId();
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11DeviceContext1> context1;
    ComPtr<ID3DDeviceContextState> isolated;
    DlssCapability capability;
    DlssExtent extent;
    Antialiasing mode=Antialiasing::Original;
    NVSDK_NGX_Parameter* parameters{};
    NVSDK_NGX_Handle* feature{};
    ComPtr<ID3D11Texture2D> output;
    ComPtr<ID3D11ShaderResourceView> outputView;
    ComPtr<ID3D11UnorderedAccessView> outputUav;
    bool initialized{},creationAttempted{},terminal{},depthInverted=true,gpuIdle=true;
    uint64_t evaluationCount{};
    void requireOwner() const {
        require(GetCurrentThreadId()==owner,"NVIDIA DLSS used from another thread");
        require(device&&context&&context->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE,"NVIDIA DLSS needs its owning immediate context");
        check(device->GetDeviceRemovedReason(),"device availability");
    }
    void dispose() noexcept {
        if(feature){NVSDK_NGX_D3D11_ReleaseFeature(feature);feature=nullptr;}
        if(parameters){NVSDK_NGX_D3D11_DestroyParameters(parameters);parameters=nullptr;}
        if(initialized){NVSDK_NGX_D3D11_Shutdown1(device.Get());initialized=false;}
        outputUav.Reset();outputView.Reset();output.Reset();
    }
};

NvidiaDlss::NvidiaDlss(ID3D11Device* device,ID3D11DeviceContext* context,const std::filesystem::path& directory)
    :state(std::make_unique<State>()) {
    auto& s=*state;s.device=device;s.context=context;s.requireOwner();
    ComPtr<ID3D11Device> contextDevice;context->GetDevice(&contextDevice);
    require(contextDevice.Get()==device,"NVIDIA DLSS context belongs to another graphics device");
    ComPtr<IDXGIDevice> dxgi;check(s.device.As(&dxgi),"DXGI device query");
    ComPtr<IDXGIAdapter> adapter;check(dxgi->GetAdapter(&adapter),"actual adapter query");
    DXGI_ADAPTER_DESC desc{};check(adapter->GetDesc(&desc),"actual adapter descriptor");
    s.capability.vendorId=desc.VendorId;s.capability.deviceId=desc.DeviceId;s.capability.adapterName=desc.Description;
    if(desc.VendorId!=0x10DE) {s.capability.reason="The active D3D11 adapter is not an NVIDIA GPU";return;}
    const auto dataDirectory=directory/L"ngx-logs";
    std::error_code directoryError;std::filesystem::create_directories(dataDirectory,directoryError);
    if(directoryError){s.capability.reason="NGX log directory cannot be created";return;}
    const auto featureDirectory=directory.wstring();
    const wchar_t* paths[]={featureDirectory.c_str()};
    NVSDK_NGX_FeatureCommonInfo common{};
    common.PathListInfo={paths,1};common.LoggingInfo.MinimumLoggingLevel=NVSDK_NGX_LOGGING_LEVEL_ON;
    NVSDK_NGX_FeatureDiscoveryInfo discovery{};
    discovery.SDKVersion=NVSDK_NGX_Version_API;discovery.FeatureID=NVSDK_NGX_Feature_SuperSampling;
    discovery.Identifier.IdentifierType=NVSDK_NGX_Application_Identifier_Type_Project_Id;
    discovery.Identifier.v.ProjectDesc={projectId,NVSDK_NGX_ENGINE_TYPE_CUSTOM,engineVersion};
    discovery.ApplicationDataPath=dataDirectory.c_str();discovery.FeatureInfo=&common;
    NVSDK_NGX_FeatureRequirement requirements{};
    auto result=NVSDK_NGX_D3D11_GetFeatureRequirements(adapter.Get(),&discovery,&requirements);
    s.capability.ngxResult=uint32_t(result);
    if(ngxFailed(result)){s.capability.reason="NGX feature requirements query failed";return;}
    s.capability.supportReasons=uint32_t(requirements.FeatureSupported);
    if(requirements.FeatureSupported!=NVSDK_NGX_FeatureSupportResult_Supported) {
        s.capability.reason="NGX reports DLSS unavailable on the active adapter, driver or operating system";return;
    }
    ComPtr<ID3D11Device1> device1;
    if(FAILED(s.device.As(&device1))||FAILED(s.context.As(&s.context1))) {
        s.capability.reason="D3D11.1 context-state isolation is unavailable";return;
    }
    const D3D_FEATURE_LEVEL levels[]={device->GetFeatureLevel()};
    D3D_FEATURE_LEVEL selected{};
    check(device1->CreateDeviceContextState(0,levels,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&selected,&s.isolated),
        "isolated context-state creation");
    result=NVSDK_NGX_D3D11_Init_with_ProjectID(projectId,NVSDK_NGX_ENGINE_TYPE_CUSTOM,engineVersion,
        dataDirectory.c_str(),device,&common);
    s.capability.ngxResult=uint32_t(result);
    if(ngxFailed(result)){s.capability.reason="NGX initialization failed";return;}
    s.initialized=true;
    struct InitializationGuard {
        State& owner;bool completed=false;
        ~InitializationGuard(){if(!completed)owner.dispose();}
    } initialization{s};
    result=NVSDK_NGX_D3D11_GetCapabilityParameters(&s.parameters);
    s.capability.ngxResult=uint32_t(result);
    if(ngxFailed(result)){s.capability.reason="NGX capability parameters query failed";s.dispose();return;}
    int available{},needsDriver{};
    const auto availableResult=s.parameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available,&available);
    s.parameters->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,&needsDriver);
    s.parameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor,&s.capability.minimumDriverMajor);
    s.parameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor,&s.capability.minimumDriverMinor);
    if(ngxFailed(availableResult)||!available||needsDriver) {
        s.capability.reason=needsDriver?"NGX requires a newer NVIDIA driver":"NGX does not expose SuperSampling on this device";
        s.dispose();return;
    }
    // Explicitly select the 4.5 reconstruction models. L targets Ultra Performance;
    // M supplies 4.5 SR for Quality/Balanced/Performance. DLAA keeps recommended K.
    s.parameters->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,unsigned(NVSDK_NGX_DLSS_Hint_Render_Preset_K));
    s.parameters->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,unsigned(NVSDK_NGX_DLSS_Hint_Render_Preset_M));
    s.parameters->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,unsigned(NVSDK_NGX_DLSS_Hint_Render_Preset_M));
    s.parameters->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,unsigned(NVSDK_NGX_DLSS_Hint_Render_Preset_M));
    s.parameters->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance,unsigned(NVSDK_NGX_DLSS_Hint_Render_Preset_L));
    s.capability.available=true;s.capability.reason="NGX SuperSampling available";
    std::fprintf(stderr,"[NATIVE NGX] adapter_vendor=%04X adapter_device=%04X requirements=0x%X capability=available sdk=v310.9.1 SR_preset=M ultra_preset=L DLAA_preset=K\n",
        desc.VendorId,desc.DeviceId,s.capability.supportReasons);
    initialization.completed=true;
}
NvidiaDlss::~NvidiaDlss() {
    if(!state)return;
    auto& s=*state;
    // Backend shutdown must prove GPU idle first. A removed device has no live GPU work.
    if(s.gpuIdle||FAILED(s.device->GetDeviceRemovedReason()))s.dispose();
    else {std::fprintf(stderr,"[NATIVE NGX] GPU idle was not proven before teardown; complete NGX/device/resource state retained\n");(void)state.release();}
}
const DlssCapability& NvidiaDlss::capability() const {return state->capability;}
DlssExtent NvidiaDlss::optimalExtent(Antialiasing mode,uint32_t w,uint32_t h) {
    auto& s=*state;s.requireOwner();require(s.capability.available&&s.initialized&&s.parameters&&!s.terminal,"NVIDIA DLSS is unavailable or shut down");
    require(w&&h&&w<=16384&&h<=16384,"NVIDIA DLSS output dimensions are invalid");
    unsigned rw{},rh{},maxw{},maxh{},minw{},minh{};float ignoredSharpness{};
    ngxCheck(NGX_DLSS_GET_OPTIMAL_SETTINGS(s.parameters,w,h,quality(mode),&rw,&rh,&maxw,&maxh,&minw,&minh,&ignoredSharpness),"optimal resolution query");
    require(rw&&rh&&rw<=w&&rh<=h,"NGX returned invalid optimal render dimensions");
    if(mode==Antialiasing::DLAA)require(rw==w&&rh==h,"NGX DLAA must use the full native output resolution");
    return {rw,rh,w,h};
}
void NvidiaDlss::create(Antialiasing mode,const DlssExtent& extent,bool inverted) {
    auto& s=*state;s.requireOwner();require(s.capability.available&&s.initialized&&s.parameters&&!s.terminal,"NVIDIA DLSS is unavailable or shut down");
    require(!s.creationAttempted&&!s.feature,"NVIDIA DLSS creation was already attempted; resources are frozen until GPU-idle shutdown");
    require(extent.renderWidth&&extent.renderHeight&&extent.outputWidth&&extent.outputHeight&&
        extent.renderWidth<=extent.outputWidth&&extent.renderHeight<=extent.outputHeight&&
        extent.outputWidth<=16384&&extent.outputHeight<=16384,"NVIDIA DLSS dimensions are invalid");
    if(mode==Antialiasing::DLAA)require(extent.renderWidth==extent.outputWidth&&extent.renderHeight==extent.outputHeight,
        "NVIDIA DLAA requires native render resolution");
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=extent.outputWidth;desc.Height=extent.outputHeight;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    check(s.device->CreateTexture2D(&desc,nullptr,&s.output),"RGBA16F output allocation");
    check(s.device->CreateShaderResourceView(s.output.Get(),nullptr,&s.outputView),"output SRV creation");
    check(s.device->CreateUnorderedAccessView(s.output.Get(),nullptr,&s.outputUav),"output UAV creation");
    NVSDK_NGX_DLSS_Create_Params create{};
    create.Feature={extent.renderWidth,extent.renderHeight,extent.outputWidth,extent.outputHeight,quality(mode)};
    // Engine color is already perceptually encoded packed SDR. There is no pre-exposure.
    // The SDK computes exposure from actual submitted color for M/L, with no CPU readback.
    create.InFeatureCreateFlags=NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_AutoExposure|
        (inverted?NVSDK_NGX_DLSS_Feature_Flags_DepthInverted:0);
    {
        ScopedContext isolated(s.context1.Get(),s.isolated.Get());
        StallProfiler::Scope profile(StallProfiler::Section::Rendering,"NGX.D3D11.CreateDLSS",nullptr,reinterpret_cast<uintptr_t>(s.device.Get()));
        s.creationAttempted=true;s.gpuIdle=false;
        const auto result=NGX_D3D11_CREATE_DLSS_EXT(s.context.Get(),&s.feature,s.parameters,&create);
        if(ngxFailed(result)){s.capability.available=false;s.capability.reason="NGX feature creation failed; GPU-idle shutdown required";}
        ngxCheck(result,"feature creation");
    }
    s.extent=extent;s.mode=mode;s.depthInverted=inverted;
}
ID3D11Texture2D* NvidiaDlss::evaluate(ID3D11Texture2D* color,ID3D11Texture2D* depth,ID3D11Texture2D* motion,
    ID3D11Texture2D* bias,const DlssFrame& frame) {
    auto& s=*state;s.requireOwner();
    require(s.initialized&&s.parameters&&!s.terminal&&s.feature&&s.output,"NVIDIA DLSS feature is absent or shut down");
    require(frame.geometryDraws&&frame.motionDraws==frame.geometryDraws,"NVIDIA DLSS rejects incomplete scene motion coverage");
    require(frame.depthInverted==s.depthInverted,"NVIDIA DLSS scene depth direction differs from feature flags");
    require(std::isfinite(frame.elapsedMilliseconds)&&frame.elapsedMilliseconds>0,
        "NVIDIA DLSS requires finite measured frame time");
    for(float value:frame.jitter)require(std::isfinite(value)&&value>=-0.5f&&value<=0.5f,
        "NVIDIA DLSS jitter must stay within half a render pixel");
    const auto c=descriptor(color,s.device.Get()),d=descriptor(depth,s.device.Get()),m=descriptor(motion,s.device.Get());
    for(const auto* desc:{&c,&d,&m})require(desc->Width==s.extent.renderWidth&&desc->Height==s.extent.renderHeight,
        "NVIDIA DLSS color, depth and motion must match the queried render extent");
    require(c.Format==DXGI_FORMAT_R10G10B10A2_UNORM||c.Format==DXGI_FORMAT_R8G8B8A8_UNORM||c.Format==DXGI_FORMAT_R16G16B16A16_FLOAT||c.Format==DXGI_FORMAT_R32G32B32A32_FLOAT,"NVIDIA DLSS scene color format is unqualified");
    require(m.Format==DXGI_FORMAT_R16G16_FLOAT||m.Format==DXGI_FORMAT_R32G32_FLOAT,"NVIDIA DLSS motion must use RG16F/RG32F");
    require(d.Format==DXGI_FORMAT_R32G8X24_TYPELESS||d.Format==DXGI_FORMAT_R32_TYPELESS||
        d.Format==DXGI_FORMAT_R32_FLOAT||d.Format==DXGI_FORMAT_D32_FLOAT||d.Format==DXGI_FORMAT_R24G8_TYPELESS,
        "NVIDIA DLSS depth format is unqualified");
    if(bias){const auto b=descriptor(bias,s.device.Get());require(b.Width==c.Width&&b.Height==c.Height&&
        (b.Format==DXGI_FORMAT_R8_UNORM||b.Format==DXGI_FORMAT_R16_FLOAT),"NVIDIA DLSS current-color bias mask is unqualified");}
    require(color!=depth&&color!=motion&&depth!=motion&&color!=s.output.Get()&&depth!=s.output.Get()&&motion!=s.output.Get()&&(!bias||(bias!=color&&bias!=depth&&bias!=motion&&bias!=s.output.Get())),"NVIDIA DLSS inputs must not alias");
    NVSDK_NGX_D3D11_DLSS_Eval_Params eval{};
    eval.Feature.pInColor=color;eval.Feature.pInOutput=s.output.Get();eval.pInDepth=depth;eval.pInMotionVectors=motion;
    eval.InJitterOffsetX=frame.jitter[0];eval.InJitterOffsetY=frame.jitter[1];
    eval.InRenderSubrectDimensions={s.extent.renderWidth,s.extent.renderHeight};
    eval.InReset=frame.reset?1:0;eval.InMVScaleX=eval.InMVScaleY=1.0f;
    // K/M/L do not support this optional input (pinned NGX guide, section 3.15).
    // Retain the renderer bias target for diagnostics; actual motion remains required.
    eval.pInBiasCurrentColorMask=nullptr;eval.InPreExposure=eval.InExposureScale=1.0f;
    eval.InFrameTimeDeltaInMsec=frame.elapsedMilliseconds;
    {
        ScopedContext isolated(s.context1.Get(),s.isolated.Get());
        StallProfiler::Scope profile(StallProfiler::Section::Rendering,"NGX.D3D11.EvaluateDLSS",nullptr,reinterpret_cast<uintptr_t>(s.feature));
        s.gpuIdle=false; // Submission can occur even when NGX subsequently returns a failure.
        const auto result=NGX_D3D11_EVALUATE_DLSS_EXT(s.context.Get(),s.feature,s.parameters,&eval);
        if(ngxFailed(result)){s.terminal=true;s.capability.available=false;s.capability.reason="NGX evaluation failed; GPU-idle shutdown required";}
        ngxCheck(result,"feature evaluation");
    }
    ++s.evaluationCount;
    if(s.evaluationCount<=4||frame.reset)std::fprintf(stderr,
        "[NATIVE NGX EVALUATE] frame=%llu mode=%u render=%ux%u output=%ux%u jitter=(%.6f,%.6f) dt_ms=%.3f reset=%u geometry=%llu motion=%llu success=1\n",
        static_cast<unsigned long long>(s.evaluationCount),unsigned(s.mode),c.Width,c.Height,s.extent.outputWidth,s.extent.outputHeight,
        frame.jitter[0],frame.jitter[1],frame.elapsedMilliseconds,frame.reset?1u:0u,
        static_cast<unsigned long long>(frame.geometryDraws),static_cast<unsigned long long>(frame.motionDraws));
    return s.output.Get();
}
ID3D11ShaderResourceView* NvidiaDlss::outputView() const {return state->outputView.Get();}
const DlssExtent& NvidiaDlss::extent() const {return state->extent;}
uint64_t NvidiaDlss::evaluations() const {return state->evaluationCount;}
void NvidiaDlss::shutdownAfterGpuIdle() {state->requireOwner();state->gpuIdle=true;state->dispose();state->terminal=true;state->capability.available=false;state->capability.reason="NGX shut down after GPU idle";}
}
