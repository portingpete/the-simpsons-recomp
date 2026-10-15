#include "vulkan_device.h"
#include "renderer/native_backend.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Simpsons::Graphics::Vulkan {
const char* resultName(VkResult result) noexcept {
    switch(result) {
    case VK_SUCCESS:return "VK_SUCCESS";
    case VK_NOT_READY:return "VK_NOT_READY";
    case VK_TIMEOUT:return "VK_TIMEOUT";
    case VK_INCOMPLETE:return "VK_INCOMPLETE";
    case VK_SUBOPTIMAL_KHR:return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY:return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_SURFACE_LOST_KHR:return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
    default:return "VkResult";
    }
}
void fail(VkResult result,const char* operation) {
    char message[256];
    std::snprintf(message,sizeof(message),"Native Vulkan %s failed: %s (%d)",operation,resultName(result),int(result));
    throw Error(message);
}
namespace {
bool hasExtension(const std::vector<VkExtensionProperties>& list,const char* name) {
    for(const auto& extension:list) if(!std::strcmp(extension.extensionName,name)) return true;
    return false;
}
std::vector<VkExtensionProperties> deviceExtensions(VkPhysicalDevice physical) {
    uint32_t count=0;vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr);
    std::vector<VkExtensionProperties> list(count);
    vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,list.data());list.resize(count);
    return list;
}
// Required for the D3D11 contract mapped by this device; checked before
// selection so an unsuitable adapter is skipped rather than half-created.
constexpr const char* requiredDeviceExtensions[]={
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME};
const char* robustnessExtension(const std::vector<VkExtensionProperties>& list) {
    if(hasExtension(list,VK_KHR_ROBUSTNESS_2_EXTENSION_NAME)) return VK_KHR_ROBUSTNESS_2_EXTENSION_NAME;
    if(hasExtension(list,VK_EXT_ROBUSTNESS_2_EXTENSION_NAME)) return VK_EXT_ROBUSTNESS_2_EXTENSION_NAME;
    return nullptr;
}
VkDeviceSize alignUp(VkDeviceSize value,VkDeviceSize alignment) {
    return alignment>1?(value+alignment-1)/alignment*alignment:value;
}
}

VulkanDevice::VulkanDevice() {
    createInstance();
    selectPhysicalDevice();
    createDevice();
}
VulkanDevice::~VulkanDevice() {
    if(device_) {
        if(current_.command) {
            try {submit();} catch(...) {}
        }
        vkDeviceWaitIdle(device_);
        completed_=~0ull;
        {
            std::lock_guard lock(initMutex_);
            if(init_.command) {vkEndCommandBuffer(init_.command);for(auto& after:init_.after) after();vkDestroyCommandPool(device_,init_.pool,nullptr);}
            for(auto& [serial,batch]:initInFlight_) {for(auto& after:batch.after) after();vkDestroyCommandPool(device_,batch.pool,nullptr);}
            for(auto& batch:freeInit_) vkDestroyCommandPool(device_,batch.pool,nullptr);
        }
        {
            std::unique_lock lock(retireMutex_);
            while(!retirements_.empty()) {
                auto pending=std::move(retirements_);retirements_.clear();
                lock.unlock();
                for(auto& [serial,destroy]:pending) destroy();
                lock.lock();
            }
        }
        auto destroyRing=[&](Ring& ring) {
            if(ring.current.buffer) vmaDestroyBuffer(allocator_,ring.current.buffer,ring.current.allocation);
            for(auto& chunk:ring.retired) vmaDestroyBuffer(allocator_,chunk.buffer,chunk.allocation);
        };
        destroyRing(dynamic_);destroyRing(upload_);
        for(auto& page:slotPages_) vmaDestroyBuffer(allocator_,page.buffer,page.allocation);
        for(auto& batch:inFlight_) vkDestroyCommandPool(device_,batch.pool,nullptr);
        for(auto& batch:freeBatches_) vkDestroyCommandPool(device_,batch.pool,nullptr);
        if(current_.pool) vkDestroyCommandPool(device_,current_.pool,nullptr);
        if(timeline_) vkDestroySemaphore(device_,timeline_,nullptr);
        if(allocator_) vmaDestroyAllocator(allocator_);
        vkDestroyDevice(device_,nullptr);
    }
}

// One process-wide instance. Entry points are loaded through it (volk instance
// mode), so device commands go through loader trampolines that dispatch on the
// device handle: several VulkanDevice objects (tests, cross-device checks) can
// coexist without sharing one device's function pointers.
void VulkanDevice::createInstance() {
    static std::mutex mutex;
    static VkInstance shared=VK_NULL_HANDLE;
    std::lock_guard lock(mutex);
    if(shared) {instance_=shared;return;}
    if(volkInitialize()!=VK_SUCCESS) throw Error("Native Vulkan loader vulkan-1.dll is unavailable");
    const uint32_t version=volkGetInstanceVersion();
    if(version<VK_API_VERSION_1_3) throw Error("Native Vulkan renderer requires a Vulkan 1.3 loader");
    uint32_t count=0;vkEnumerateInstanceExtensionProperties(nullptr,&count,nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateInstanceExtensionProperties(nullptr,&count,available.data());available.resize(count);
    std::vector<const char*> extensions={VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    for(const char* name:{VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_WIN32_SURFACE_EXTENSION_NAME})
        if(!hasExtension(available,name)) throw Error("Native Vulkan loader lacks Win32 surface support");
    for(const char* optional:{VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME})
        if(hasExtension(available,optional)) extensions.push_back(optional);
    std::vector<const char*> layers;
    if(const char* validation=std::getenv("SIMPSONS_VULKAN_VALIDATION");validation && *validation=='1') {
        uint32_t layerCount=0;vkEnumerateInstanceLayerProperties(&layerCount,nullptr);
        std::vector<VkLayerProperties> list(layerCount);vkEnumerateInstanceLayerProperties(&layerCount,list.data());
        bool found=false;
        for(const auto& layer:list) found|=!std::strcmp(layer.layerName,"VK_LAYER_KHRONOS_validation");
        if(found) layers.push_back("VK_LAYER_KHRONOS_validation");
        else std::fprintf(stderr,"[NATIVE VULKAN] SIMPSONS_VULKAN_VALIDATION=1 but VK_LAYER_KHRONOS_validation is not installed\n");
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName="The Simpsons Game native port";app.pEngineName="SimpsonsNative";app.apiVersion=VK_API_VERSION_1_3;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo=&app;
    info.enabledExtensionCount=uint32_t(extensions.size());info.ppEnabledExtensionNames=extensions.data();
    info.enabledLayerCount=uint32_t(layers.size());info.ppEnabledLayerNames=layers.data();
    check(vkCreateInstance(&info,nullptr,&instance_),"instance creation");
    volkLoadInstance(instance_);
    shared=instance_; // Lives for the process; never destroyed by a device.
}

void VulkanDevice::selectPhysicalDevice() {
    uint32_t count=0;check(vkEnumeratePhysicalDevices(instance_,&count,nullptr),"adapter enumeration");
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(instance_,&count,devices.data()),"adapter enumeration");
    const char* requested=std::getenv("SIMPSONS_VULKAN_DEVICE");
    int best=-1;VkPhysicalDevice chosen=VK_NULL_HANDLE;
    for(auto physical:devices) {
        VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical,&properties);
        const auto extensions=deviceExtensions(physical);
        bool suitable=properties.apiVersion>=VK_API_VERSION_1_3 && robustnessExtension(extensions);
        for(const char* name:requiredDeviceExtensions) suitable=suitable && hasExtension(extensions,name);
        if(requested && *requested) {
            std::string name=properties.deviceName,want=requested;
            std::transform(name.begin(),name.end(),name.begin(),::tolower);
            std::transform(want.begin(),want.end(),want.begin(),::tolower);
            if(name.find(want)==std::string::npos) continue;
        }
        if(!suitable) {
            std::fprintf(stderr,"[NATIVE VULKAN] skipping %s: Vulkan 1.3 with push descriptors, robustness2 and depth-clip control is required\n",properties.deviceName);
            continue;
        }
        const int score=properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU?3:
            properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU?2:1;
        if(score>best) {best=score;chosen=physical;}
    }
    if(!chosen) throw Error(requested&&*requested?"Native Vulkan adapter named by SIMPSONS_VULKAN_DEVICE is unavailable or unsuitable":
        "Native Vulkan renderer found no suitable Vulkan 1.3 adapter");
    physical_=chosen;
    vkGetPhysicalDeviceProperties(physical_,&properties_);
}

void VulkanDevice::createDevice() {
    const auto extensions=deviceExtensions(physical_);
    uint32_t familyCount=0;vkGetPhysicalDeviceQueueFamilyProperties(physical_,&familyCount,nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_,&familyCount,families.data());
    bool foundFamily=false;
    for(uint32_t i=0;i<familyCount && !foundFamily;++i) {
        const auto flags=families[i].queueFlags;
        if((flags&VK_QUEUE_GRAPHICS_BIT) && (flags&VK_QUEUE_COMPUTE_BIT) && families[i].timestampValidBits) {
            family_=i;foundFamily=true;
            const BOOL present=vkGetPhysicalDeviceWin32PresentationSupportKHR(physical_,i);
            if(!present) std::fprintf(stderr,"[NATIVE VULKAN] queue family %u lacks Win32 presentation; headless use only\n",i);
        }
    }
    if(!foundFamily) throw Error("Native Vulkan adapter has no universal queue with timestamps");

    // Supported features first; enable the required subset explicitly.
    VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceVulkan11Features supported11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features supported12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features supported13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceRobustness2FeaturesKHR supportedRobustness{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_KHR};
    VkPhysicalDeviceDepthClipEnableFeaturesEXT supportedClip{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT};
    VkPhysicalDeviceCustomBorderColorFeaturesEXT supportedBorder{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT};
    VkPhysicalDevicePresentId2FeaturesKHR supportedPresentId{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR};
    VkPhysicalDevicePresentTimingFeaturesEXT supportedTiming{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT};
    VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR supportedSwapchain{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR};
    VkPhysicalDeviceMaintenance5FeaturesKHR supportedMaintenance5{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR};
    const char* robustness=robustnessExtension(extensions);
    const bool borderExtension=hasExtension(extensions,VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME);
    const bool presentIdExtension=hasExtension(extensions,VK_KHR_PRESENT_ID_2_EXTENSION_NAME);
    const bool timingExtension=presentIdExtension && hasExtension(extensions,VK_EXT_PRESENT_TIMING_EXTENSION_NAME) &&
        hasExtension(extensions,VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
    const char* swapchainMaintenance=hasExtension(extensions,VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)?VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME:
        hasExtension(extensions,VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)?VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME:nullptr;
    const bool maintenance5Extension=hasExtension(extensions,VK_KHR_MAINTENANCE_5_EXTENSION_NAME);
    supported.pNext=&supported11;supported11.pNext=&supported12;supported12.pNext=&supported13;
    supported13.pNext=&supportedRobustness;supportedRobustness.pNext=&supportedClip;
    void** tail=&supportedClip.pNext;
    if(borderExtension){*tail=&supportedBorder;tail=&supportedBorder.pNext;}
    if(presentIdExtension){*tail=&supportedPresentId;tail=&supportedPresentId.pNext;}
    if(timingExtension){*tail=&supportedTiming;tail=&supportedTiming.pNext;}
    if(swapchainMaintenance){*tail=&supportedSwapchain;tail=&supportedSwapchain.pNext;}
    if(maintenance5Extension){*tail=&supportedMaintenance5;tail=&supportedMaintenance5.pNext;}
    vkGetPhysicalDeviceFeatures2(physical_,&supported);
    const auto& core=supported.features;
    auto need=[&](VkBool32 value,const char* feature) {
        if(!value) {
            char message[200];std::snprintf(message,sizeof(message),"Native Vulkan adapter %s lacks required feature %s",properties_.deviceName,feature);
            throw Error(message);
        }
    };
    need(core.depthClamp,"depthClamp");need(core.independentBlend,"independentBlend");
    need(core.sampleRateShading,"sampleRateShading");need(core.textureCompressionBC,"textureCompressionBC");
    need(core.robustBufferAccess,"robustBufferAccess");
    need(supported12.timelineSemaphore,"timelineSemaphore");need(supported12.hostQueryReset,"hostQueryReset");
    need(supported12.scalarBlockLayout,"scalarBlockLayout");
    need(supported13.dynamicRendering,"dynamicRendering");need(supported13.synchronization2,"synchronization2");
    need(supportedRobustness.robustBufferAccess2,"robustBufferAccess2");need(supportedRobustness.robustImageAccess2,"robustImageAccess2");
    need(supportedRobustness.nullDescriptor,"nullDescriptor");need(supportedClip.depthClipEnable,"depthClipEnable");

    VkPhysicalDeviceFeatures2 enabled{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceVulkan11Features enabled11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features enabled12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features enabled13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceRobustness2FeaturesKHR enabledRobustness{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_KHR};
    VkPhysicalDeviceDepthClipEnableFeaturesEXT enabledClip{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT};
    VkPhysicalDeviceCustomBorderColorFeaturesEXT enabledBorder{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT};
    VkPhysicalDevicePresentId2FeaturesKHR enabledPresentId{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR};
    VkPhysicalDevicePresentTimingFeaturesEXT enabledTiming{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT};
    VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR enabledSwapchain{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR};
    VkPhysicalDeviceMaintenance5FeaturesKHR enabledMaintenance5{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR};
    auto& features=enabled.features;
    features.depthClamp=VK_TRUE;features.independentBlend=VK_TRUE;features.sampleRateShading=VK_TRUE;
    features.textureCompressionBC=VK_TRUE;features.robustBufferAccess=VK_TRUE;
    features.samplerAnisotropy=core.samplerAnisotropy;features.fillModeNonSolid=core.fillModeNonSolid;
    features.dualSrcBlend=core.dualSrcBlend;features.shaderImageGatherExtended=core.shaderImageGatherExtended;
    features.imageCubeArray=core.imageCubeArray;features.shaderClipDistance=core.shaderClipDistance;
    features.shaderCullDistance=core.shaderCullDistance;features.depthBiasClamp=core.depthBiasClamp;
    features.alphaToOne=VK_FALSE;features.fragmentStoresAndAtomics=VK_FALSE;
    features.pipelineStatisticsQuery=core.pipelineStatisticsQuery;features.occlusionQueryPrecise=core.occlusionQueryPrecise;
    features.geometryShader=VK_FALSE;
    enabled11.shaderDrawParameters=supported11.shaderDrawParameters;
    enabled12.timelineSemaphore=VK_TRUE;enabled12.hostQueryReset=VK_TRUE;enabled12.scalarBlockLayout=VK_TRUE;
    enabled12.samplerMirrorClampToEdge=supported12.samplerMirrorClampToEdge;
    enabled12.uniformBufferStandardLayout=supported12.uniformBufferStandardLayout;
    enabled13.dynamicRendering=VK_TRUE;enabled13.synchronization2=VK_TRUE;enabled13.maintenance4=supported13.maintenance4;
    enabledRobustness.robustBufferAccess2=VK_TRUE;enabledRobustness.robustImageAccess2=VK_TRUE;enabledRobustness.nullDescriptor=VK_TRUE;
    enabledClip.depthClipEnable=VK_TRUE;
    enabled.pNext=&enabled11;enabled11.pNext=&enabled12;enabled12.pNext=&enabled13;
    enabled13.pNext=&enabledRobustness;enabledRobustness.pNext=&enabledClip;
    tail=&enabledClip.pNext;
    std::vector<const char*> names(std::begin(requiredDeviceExtensions),std::end(requiredDeviceExtensions));
    names.push_back(robustness);
    if(borderExtension && supportedBorder.customBorderColors && supportedBorder.customBorderColorWithoutFormat) {
        enabledBorder.customBorderColors=VK_TRUE;enabledBorder.customBorderColorWithoutFormat=VK_TRUE;
        *tail=&enabledBorder;tail=&enabledBorder.pNext;names.push_back(VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME);
        capabilities_.customBorderColor=true;
    }
    if(hasExtension(extensions,VK_EXT_SHADER_STENCIL_EXPORT_EXTENSION_NAME)) {
        names.push_back(VK_EXT_SHADER_STENCIL_EXPORT_EXTENSION_NAME);capabilities_.stencilExport=true;
    }
    if(presentIdExtension && supportedPresentId.presentId2) {
        enabledPresentId.presentId2=VK_TRUE;*tail=&enabledPresentId;tail=&enabledPresentId.pNext;
        names.push_back(VK_KHR_PRESENT_ID_2_EXTENSION_NAME);capabilities_.presentId2=true;
        if(timingExtension && supportedTiming.presentTiming) {
            enabledTiming.presentTiming=VK_TRUE;*tail=&enabledTiming;tail=&enabledTiming.pNext;
            names.push_back(VK_EXT_PRESENT_TIMING_EXTENSION_NAME);names.push_back(VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
            capabilities_.presentTiming=true;
        }
    }
    if(swapchainMaintenance && supportedSwapchain.swapchainMaintenance1) {
        enabledSwapchain.swapchainMaintenance1=VK_TRUE;*tail=&enabledSwapchain;tail=&enabledSwapchain.pNext;
        names.push_back(swapchainMaintenance);capabilities_.swapchainMaintenance=true;
    }
    if(maintenance5Extension && supportedMaintenance5.maintenance5) {
        enabledMaintenance5.maintenance5=VK_TRUE;*tail=&enabledMaintenance5;tail=&enabledMaintenance5.pNext;
        names.push_back(VK_KHR_MAINTENANCE_5_EXTENSION_NAME);capabilities_.maintenance5=true;
    }
    capabilities_.fillModeNonSolid=core.fillModeNonSolid;
    capabilities_.pipelineStatistics=core.pipelineStatisticsQuery;capabilities_.preciseOcclusion=core.occlusionQueryPrecise;
    capabilities_.mirrorClampToEdge=supported12.samplerMirrorClampToEdge;

    const float priority=1.0f;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex=family_;queue.queueCount=1;queue.pQueuePriorities=&priority;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext=&enabled;info.queueCreateInfoCount=1;info.pQueueCreateInfos=&queue;
    info.enabledExtensionCount=uint32_t(names.size());info.ppEnabledExtensionNames=names.data();
    check(vkCreateDevice(physical_,&info,nullptr,&device_),"device creation");
    vkGetDeviceQueue(device_,family_,0,&queue_);

    VkPhysicalDevicePushDescriptorPropertiesKHR push{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR};
    VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    push.pNext=&ids;ids.pNext=&driver;
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};properties.pNext=&push;
    vkGetPhysicalDeviceProperties2(physical_,&properties);
    capabilities_.maxPushDescriptors=push.maxPushDescriptors;
    if(ids.deviceLUIDValid) {std::memcpy(&luid_,ids.deviceLUID,sizeof(luid_));luidValid_=true;}
    const auto& limits=properties_.limits;
    capabilities_.colorSamples=limits.framebufferColorSampleCounts;capabilities_.depthSamples=limits.framebufferDepthSampleCounts;
    capabilities_.stencilSamples=limits.framebufferStencilSampleCounts;capabilities_.integerSamples=limits.sampledImageIntegerSampleCounts;
    char text[512];
    std::snprintf(text,sizeof(text),"%s api=%u.%u.%u driver=%s %s",properties_.deviceName,VK_API_VERSION_MAJOR(properties_.apiVersion),
        VK_API_VERSION_MINOR(properties_.apiVersion),VK_API_VERSION_PATCH(properties_.apiVersion),driver.driverName,driver.driverInfo);
    description_=text;

    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr=vkGetInstanceProcAddr;functions.vkGetDeviceProcAddr=vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo allocator{};
    allocator.physicalDevice=physical_;allocator.device=device_;allocator.instance=instance_;
    allocator.vulkanApiVersion=VK_API_VERSION_1_3;allocator.pVulkanFunctions=&functions;
    check(vmaCreateAllocator(&allocator,&allocator_),"memory allocator creation");

    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE;type.initialValue=0;
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};semaphore.pNext=&type;
    check(vkCreateSemaphore(device_,&semaphore,nullptr,&timeline_),"submission timeline creation");
}

VulkanDevice::Batch VulkanDevice::acquireBatch() {
    if(!freeBatches_.empty()) {Batch batch=freeBatches_.back();freeBatches_.pop_back();return batch;}
    if(!inFlight_.empty() && complete(inFlight_.front().serial)) {
        Batch batch=inFlight_.front();inFlight_.pop_front();
        check(vkResetCommandPool(device_,batch.pool,0),"command pool reset");
        return batch;
    }
    Batch batch;
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;pool.queueFamilyIndex=family_;
    check(vkCreateCommandPool(device_,&pool,nullptr,&batch.pool),"command pool creation");
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool=batch.pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
    check(vkAllocateCommandBuffers(device_,&allocate,&batch.command),"command buffer allocation");
    return batch;
}
VkCommandBuffer VulkanDevice::commands() {
    if(!current_.command) {
        if(lost()) fail(VK_ERROR_DEVICE_LOST,"command recording");
        const Batch batch=acquireBatch();
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(batch.command,&begin),"command buffer begin");
        current_=batch;
    }
    return current_.command;
}
uint64_t VulkanDevice::completedSerial() {
    uint64_t value=0;
    const VkResult result=vkGetSemaphoreCounterValue(device_,timeline_,&value);
    if(result==VK_ERROR_DEVICE_LOST) markLost();
    check(result,"timeline query");
    if(value>completed_) completed_=value;
    return completed_;
}
bool VulkanDevice::wait(uint64_t serial,uint64_t timeoutNanoseconds) {
    if(complete(serial)) return true;
    // Callers submit through the context, which first ends dynamic rendering.
    if(serial>submittedSerial()) throw Error("Native Vulkan wait for a batch that was never submitted");
    VkSemaphoreWaitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    info.semaphoreCount=1;info.pSemaphores=&timeline_;info.pValues=&serial;
    const VkResult result=vkWaitSemaphores(device_,&info,timeoutNanoseconds);
    if(result==VK_TIMEOUT) return false;
    if(result==VK_ERROR_DEVICE_LOST) markLost();
    check(result,"GPU completion wait");
    completedSerial();
    return true;
}
uint64_t VulkanDevice::submit(std::span<const Wait> waits,std::span<const VkSemaphore> signals) {
    if(lost()) fail(VK_ERROR_DEVICE_LOST,"submission");
    const uint64_t serial=recordingSerial();
    InitBatch initSubmit;
    {
        std::lock_guard lock(initMutex_);
        if(init_.command) {
            // Initialization uploads and layout transitions precede every later
            // command on this queue.
            VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
            barrier.srcStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;barrier.srcAccessMask=VK_ACCESS_2_MEMORY_WRITE_BIT;
            barrier.dstStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;barrier.dstAccessMask=VK_ACCESS_2_MEMORY_READ_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dependency.memoryBarrierCount=1;dependency.pMemoryBarriers=&barrier;
            vkCmdPipelineBarrier2(init_.command,&dependency);
            check(vkEndCommandBuffer(init_.command),"initialization command end");
            initSubmit=std::move(init_);init_={};
        }
    }
    if(current_.command) check(vkEndCommandBuffer(current_.command),"command buffer end");
    VkCommandBufferSubmitInfo buffers[2]{};uint32_t bufferCount=0;
    if(initSubmit.command) {buffers[bufferCount].sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;buffers[bufferCount++].commandBuffer=initSubmit.command;}
    if(current_.command) {buffers[bufferCount].sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;buffers[bufferCount++].commandBuffer=current_.command;}
    std::vector<VkSemaphoreSubmitInfo> waitInfos;waitInfos.reserve(waits.size());
    for(const auto& wait:waits) {
        VkSemaphoreSubmitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};info.semaphore=wait.semaphore;info.stageMask=wait.stages;
        waitInfos.push_back(info);
    }
    std::vector<VkSemaphoreSubmitInfo> signalInfos;signalInfos.reserve(signals.size()+1);
    {
        VkSemaphoreSubmitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        info.semaphore=timeline_;info.value=serial;info.stageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;signalInfos.push_back(info);
    }
    for(auto signal:signals) {
        VkSemaphoreSubmitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        info.semaphore=signal;info.stageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;signalInfos.push_back(info);
    }
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount=uint32_t(waitInfos.size());submit.pWaitSemaphoreInfos=waitInfos.data();
    submit.commandBufferInfoCount=bufferCount;submit.pCommandBufferInfos=buffers;
    submit.signalSemaphoreInfoCount=uint32_t(signalInfos.size());submit.pSignalSemaphoreInfos=signalInfos.data();
    const VkResult result=vkQueueSubmit2(queue_,1,&submit,VK_NULL_HANDLE);
    if(result==VK_ERROR_DEVICE_LOST) markLost();
    check(result,"queue submission");
    submitted_.store(serial,std::memory_order_release);
    if(current_.command) {current_.serial=serial;inFlight_.push_back(current_);current_={};}
    if(initSubmit.command) {std::lock_guard lock(initMutex_);initInFlight_.emplace_back(serial,std::move(initSubmit));}
    collect();
    return serial;
}
void VulkanDevice::retire(std::function<void()> destroy) {
    std::lock_guard lock(retireMutex_);
    retirements_.emplace_back(recordingSerial(),std::move(destroy));
}
void VulkanDevice::collect() {
    const uint64_t done=completedSerial();
    std::vector<std::function<void()>> ready;
    {
        std::lock_guard lock(retireMutex_);
        // Release threads can append slightly out of serial order; scan all.
        for(auto it=retirements_.begin();it!=retirements_.end();) {
            if(it->first<=done) {ready.push_back(std::move(it->second));it=retirements_.erase(it);}
            else ++it;
        }
    }
    for(auto& destroy:ready) destroy();
    std::vector<InitBatch> finished;
    {
        std::lock_guard lock(initMutex_);
        while(!initInFlight_.empty() && initInFlight_.front().first<=done) {
            finished.push_back(std::move(initInFlight_.front().second));initInFlight_.pop_front();
        }
    }
    for(auto& batch:finished) {
        for(auto& after:batch.after) after();
        batch.after.clear();
        check(vkResetCommandPool(device_,batch.pool,0),"initialization pool reset");
        std::lock_guard lock(initMutex_);freeInit_.push_back(std::move(batch));
    }
}
void VulkanDevice::recordInit(const std::function<void(VkCommandBuffer)>& record,std::function<void()> afterCompletion) {
    std::lock_guard lock(initMutex_);
    if(!init_.command) {
        if(!freeInit_.empty()) {init_=std::move(freeInit_.back());freeInit_.pop_back();}
        else {
            VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pool.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;pool.queueFamilyIndex=family_;
            check(vkCreateCommandPool(device_,&pool,nullptr,&init_.pool),"initialization pool creation");
            VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocate.commandPool=init_.pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
            check(vkAllocateCommandBuffers(device_,&allocate,&init_.command),"initialization command allocation");
        }
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(init_.command,&begin),"initialization command begin");
    }
    record(init_.command);
    if(afterCompletion) init_.after.push_back(std::move(afterCompletion));
}
VulkanDevice::RingChunk VulkanDevice::createChunk(const Ring& ring,VkDeviceSize size) {
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=size;buffer.usage=ring.usage;
    VmaAllocationCreateInfo allocation{};allocation.usage=VMA_MEMORY_USAGE_AUTO;
    allocation.flags=VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT|VMA_ALLOCATION_CREATE_MAPPED_BIT;
    allocation.requiredFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    RingChunk chunk;VmaAllocationInfo info{};
    check(vmaCreateBuffer(allocator_,&buffer,&allocation,&chunk.buffer,&chunk.allocation,&info),ring.name);
    chunk.data=static_cast<uint8_t*>(info.pMappedData);chunk.size=size;
    if(!chunk.data) fail(VK_ERROR_MEMORY_MAP_FAILED,ring.name);
    return chunk;
}
HostSlice VulkanDevice::allocate(Ring& ring,VkDeviceSize size,VkDeviceSize alignment) {
    if(!size) size=1;
    VkDeviceSize offset=alignUp(ring.cursor,std::max<VkDeviceSize>(alignment,16));
    if(!ring.current.buffer || offset+size>ring.current.size) {
        if(ring.current.buffer) {ring.current.serial=recordingSerial();ring.retired.push_back(ring.current);ring.current={};}
        while(!ring.retired.empty() && complete(ring.retired.front().serial) && ring.retired.front().size!=ring.chunkSize) {
            const auto chunk=ring.retired.front();ring.retired.pop_front();
            retire([allocator=allocator_,chunk]{vmaDestroyBuffer(allocator,chunk.buffer,chunk.allocation);});
        }
        if(size<=ring.chunkSize && !ring.retired.empty() && complete(ring.retired.front().serial)) {
            ring.current=ring.retired.front();ring.retired.pop_front();
        } else ring.current=createChunk(ring,std::max(ring.chunkSize,size));
        offset=0;
    }
    ring.cursor=offset+size;
    ring.current.serial=recordingSerial();
    return {ring.current.buffer,offset,size,ring.current.data+offset};
}
HostSlice VulkanDevice::allocateDynamic(VkDeviceSize size,VkDeviceSize alignment) {return allocate(dynamic_,size,alignment);}
HostSlice VulkanDevice::allocateUpload(VkDeviceSize size,VkDeviceSize alignment) {return allocate(upload_,size,alignment);}
void VulkanDevice::markLost() {
    lost_.store(true,std::memory_order_release);
    std::lock_guard lock(lostMutex_);
    for(const auto& [cookie,event]:lostEvents_) SetEvent(event);
}
DWORD VulkanDevice::registerLostEvent(HANDLE event) {
    std::lock_guard lock(lostMutex_);
    const DWORD cookie=nextLostCookie_++;
    lostEvents_.emplace_back(cookie,event);
    if(lost()) SetEvent(event);
    return cookie;
}
void VulkanDevice::unregisterLostEvent(DWORD cookie) {
    std::lock_guard lock(lostMutex_);
    std::erase_if(lostEvents_,[cookie](const auto& entry){return entry.first==cookie;});
}
HostSlot VulkanDevice::allocateSlot(VkDeviceSize size) {
    constexpr VkBufferUsageFlags usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT|
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    uint32_t sizeClass=0;
    while(sizeClass<slotClasses && (VkDeviceSize(256)<<sizeClass)<size) ++sizeClass;
    if(sizeClass==slotClasses) {
        // Larger than 1 MiB: one dedicated allocation per slot.
        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=size;buffer.usage=usage;
        VmaAllocationCreateInfo allocation{};allocation.usage=VMA_MEMORY_USAGE_AUTO;
        allocation.flags=VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT|VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocation.requiredFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        HostSlot slot;VmaAllocationInfo info{};
        check(vmaCreateBuffer(allocator_,&buffer,&allocation,&slot.buffer,&slot.dedicated,&info),"large buffer slot");
        slot.data=static_cast<uint8_t*>(info.pMappedData);slot.size=size;slot.sizeClass=slotClasses;
        return slot;
    }
    std::lock_guard lock(slotMutex_);
    auto& free=freeSlots_[sizeClass];
    if(free.empty()) {
        const VkDeviceSize slotSize=VkDeviceSize(256)<<sizeClass;
        const VkDeviceSize pageSize=std::max<VkDeviceSize>(slotSize*16,256u<<10);
        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=pageSize;buffer.usage=usage;
        VmaAllocationCreateInfo allocation{};allocation.usage=VMA_MEMORY_USAGE_AUTO;
        allocation.flags=VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT|VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocation.requiredFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        SlotPage page;VmaAllocationInfo info{};
        check(vmaCreateBuffer(allocator_,&buffer,&allocation,&page.buffer,&page.allocation,&info),"buffer slot page");
        page.data=static_cast<uint8_t*>(info.pMappedData);
        slotPages_.push_back(page);
        for(VkDeviceSize offset=pageSize;offset>=slotSize;offset-=slotSize)
            free.push_back({page.buffer,offset-slotSize,slotSize,page.data+offset-slotSize,sizeClass,nullptr});
    }
    HostSlot slot=free.back();free.pop_back();
    return slot;
}
void VulkanDevice::freeSlot(const HostSlot& slot) {
    if(!slot.buffer) return;
    if(slot.dedicated) {vmaDestroyBuffer(allocator_,slot.buffer,slot.dedicated);return;}
    std::lock_guard lock(slotMutex_);
    freeSlots_[slot.sizeClass].push_back(slot);
}
HostBuffer VulkanDevice::createHostBuffer(VkDeviceSize size,VkBufferUsageFlags usage,bool readback) {
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=std::max<VkDeviceSize>(size,4);buffer.usage=usage;
    VmaAllocationCreateInfo allocation{};allocation.usage=VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocation.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|(readback?VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT:VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    allocation.requiredFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    if(readback) allocation.preferredFlags=VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    HostBuffer result;VmaAllocationInfo info{};
    check(vmaCreateBuffer(allocator_,&buffer,&allocation,&result.buffer,&result.allocation,&info),"host buffer creation");
    VkMemoryPropertyFlags flags=0;vmaGetAllocationMemoryProperties(allocator_,result.allocation,&flags);
    result.data=static_cast<uint8_t*>(info.pMappedData);result.size=size;result.coherent=(flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
    if(!result.data) {vmaDestroyBuffer(allocator_,result.buffer,result.allocation);fail(VK_ERROR_MEMORY_MAP_FAILED,"host buffer mapping");}
    return result;
}
void VulkanDevice::destroyHostBuffer(const HostBuffer& buffer) {
    if(buffer.buffer) retire([allocator=allocator_,buffer]{vmaDestroyBuffer(allocator,buffer.buffer,buffer.allocation);});
}
void VulkanDevice::invalidate(const HostBuffer& buffer) {
    if(!buffer.coherent) check(vmaInvalidateAllocation(allocator_,buffer.allocation,0,VK_WHOLE_SIZE),"host buffer invalidation");
}
void VulkanDevice::flush(const HostBuffer& buffer) {
    if(!buffer.coherent) check(vmaFlushAllocation(allocator_,buffer.allocation,0,VK_WHOLE_SIZE),"host buffer flush");
}
}
