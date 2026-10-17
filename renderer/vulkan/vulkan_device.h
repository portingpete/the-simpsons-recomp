#pragma once
#include "vulkan_api.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <new>
#include <span>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

namespace Simpsons::Graphics::Vulkan {
// Host-visible sub-range of a ring or dedicated buffer. Valid for commands
// recorded into the batch that allocated it.
struct HostSlice {
    VkBuffer buffer=VK_NULL_HANDLE;
    VkDeviceSize offset=0,size=0;
    uint8_t* data=nullptr;
};
struct HostBuffer {
    VkBuffer buffer=VK_NULL_HANDLE;
    VmaAllocation allocation=nullptr;
    uint8_t* data=nullptr;
    VkDeviceSize size=0;
    bool coherent=true;
};
// Fixed-size host-visible slot (power-of-two class) inside a shared page.
// Renamed D3D11 buffers own their slots: contents stay valid for as long as
// the buffer uses them, independent of batch recycling.
struct HostSlot {
    VkBuffer buffer=VK_NULL_HANDLE;
    VkDeviceSize offset=0,size=0;
    uint8_t* data=nullptr;
    uint32_t sizeClass=0;
    VmaAllocation dedicated=nullptr;
};
struct DeviceCapabilities {
    bool stencilExport=false,customBorderColor=false,presentTiming=false,presentId2=false,
        swapchainMaintenance=false,maintenance5=false,fillModeNonSolid=false,mirrorClampToEdge=false,
        pipelineStatistics=false,preciseOcclusion=false;
    uint32_t maxPushDescriptors=0;
    VkSampleCountFlags colorSamples=0,depthSamples=0,stencilSamples=0,integerSamples=0;
};

// Runs recorded command-buffer calls in order on a worker thread, so the
// driver's command encoding (most of the Vulkan backend's game-thread cost)
// overlaps the game thread, as NVIDIA's D3D11 driver does on its own thread.
// The owner thread resolves every argument: a call captures plain values
// (handles, offsets, copied descriptor data) and later runs with the batch's
// command buffer. SIMPSONS_VULKAN_RECORD_THREAD=0 runs each call immediately.
class CommandRecorder {
public:
    CommandRecorder();
    ~CommandRecorder();
    CommandRecorder(const CommandRecorder&)=delete;
    CommandRecorder& operator=(const CommandRecorder&)=delete;
    bool threaded() const {return threaded_;}
    // Owner thread, with every recorded call finished: later calls record
    // into this command buffer.
    void target(VkCommandBuffer command) {target_=command;}
    // call(VkCommandBuffer)
    template<class F> void record(F&& call) {
        using Call=std::decay_t<F>;
        static_assert(std::is_trivially_copyable_v<Call> && std::is_trivially_destructible_v<Call>,"recorded calls capture plain values");
        if(!threaded_) {call(target_);return;}
        new(reserve(callBytes<Call>(),&invoke<Call>)) Call(std::forward<F>(call));
        publish();
    }
    // call(VkCommandBuffer,const T*) with a copy of data.
    template<class T,class F> void record(std::span<const T> data,F&& call) {
        using Call=std::decay_t<F>;
        static_assert(std::is_trivially_copyable_v<Call> && std::is_trivially_destructible_v<Call>,"recorded calls capture plain values");
        static_assert(std::is_trivially_copyable_v<T> && alignof(T)<=16,"recorded data is plain");
        if(!threaded_) {call(target_,data.data());return;}
        auto* payload=static_cast<std::byte*>(reserve(callBytes<Call>()+data.size_bytes(),&invokeWith<Call,T>));
        new(payload) Call(std::forward<F>(call));
        if(!data.empty()) std::memcpy(payload+callBytes<Call>(),data.data(),data.size_bytes());
        publish();
    }
    // Owner thread: returns once every recorded call has run.
    void finish();
private:
    struct Chunk;
    struct Header {void (*invoke)(void* payload,VkCommandBuffer command);size_t bytes;};
    static_assert(sizeof(Header)==16);
    template<class Call> static constexpr size_t callBytes() {return (sizeof(Call)+15)&~size_t(15);}
    template<class Call> static void invoke(void* payload,VkCommandBuffer command) {(*static_cast<Call*>(payload))(command);}
    template<class Call,class T> static void invokeWith(void* payload,VkCommandBuffer command) {
        (*static_cast<Call*>(payload))(command,reinterpret_cast<const T*>(static_cast<std::byte*>(payload)+callBytes<Call>()));
    }
    void* reserve(size_t bytes,void (*call)(void*,VkCommandBuffer));
    void publish();
    void run();
    Chunk* takeChunk();
    void giveChunk(Chunk* chunk);
    const bool threaded_;
    VkCommandBuffer target_=VK_NULL_HANDLE;
    Chunk* writeChunk_=nullptr;size_t writeOffset_=0;uint64_t produced_=0; // Owner thread.
    Chunk* readChunk_=nullptr;size_t readOffset_=0;bool stopped_=false;    // Worker thread.
    alignas(64) std::atomic<uint64_t> published_{0};
    alignas(64) std::atomic<bool> sleeping_{false};
    alignas(64) std::atomic<uint64_t> consumed_{0};
    std::atomic<bool> draining_{false};
    std::mutex chunkMutex_;
    std::vector<Chunk*> spare_;
    std::thread worker_;
};

// One logical device and its single universal queue. Command recording,
// submission and ring allocation belong to the owner thread (the D3D11
// immediate context). Resource creation, initialization uploads and deferred
// destruction are thread-safe.
class VulkanDevice {
public:
    VulkanDevice();
    ~VulkanDevice();
    VulkanDevice(const VulkanDevice&)=delete;
    VulkanDevice& operator=(const VulkanDevice&)=delete;

    VkInstance instance() const {return instance_;}
    VkPhysicalDevice physical() const {return physical_;}
    VkDevice device() const {return device_;}
    VkQueue queue() const {return queue_;}
    uint32_t queueFamily() const {return family_;}
    VmaAllocator allocator() const {return allocator_;}
    const VkPhysicalDeviceProperties& properties() const {return properties_;}
    const DeviceCapabilities& capabilities() const {return capabilities_;}
    const std::string& description() const {return description_;}
    LUID adapterLuid() const {return luid_;}
    bool luidValid() const {return luidValid_;}

    // Records a call into the current batch, begun on first use (see
    // CommandRecorder). Owner thread.
    template<class F> void record(F&& call) {begin();recorder_.record(std::forward<F>(call));}
    template<class T,class F> void record(std::span<const T> data,F&& call) {begin();recorder_.record(data,std::forward<F>(call));}
    bool recording() const {return current_.command!=VK_NULL_HANDLE;}
    // Serial signalled by the batch being recorded (or the next one).
    uint64_t recordingSerial() const {return submitted_.load(std::memory_order_acquire)+1;}
    uint64_t submittedSerial() const {return submitted_.load(std::memory_order_acquire);}
    uint64_t completedSerial();
    bool complete(uint64_t serial) {return serial<=completed_ || serial<=completedSerial();}
    // False on timeout. Device loss throws.
    bool wait(uint64_t serial,uint64_t timeoutNanoseconds);
    struct Wait {VkSemaphore semaphore;VkPipelineStageFlags2 stages;};
    // Submits pending initialization commands and the current batch (even an
    // empty one) and signals recordingSerial(). Returns that serial.
    uint64_t submit(std::span<const Wait> waits={},std::span<const VkSemaphore> signals={});
    // Thread-safe. Runs once all GPU work recorded so far has completed.
    void retire(std::function<void()> destroy);
    void collect();
    // Thread-safe initialization commands for new resources. They execute
    // ahead of the next submitted batch, followed by a full memory barrier.
    void recordInit(const std::function<void(VkCommandBuffer)>& record,std::function<void()> afterCompletion={});
    // Owner-thread host rings.
    HostSlice allocateDynamic(VkDeviceSize size,VkDeviceSize alignment);
    HostSlice allocateUpload(VkDeviceSize size,VkDeviceSize alignment);
    // Thread-safe host slots. freeSlot returns immediately: the caller proves
    // the GPU no longer reads the slot (or uses retire()).
    HostSlot allocateSlot(VkDeviceSize size);
    void freeSlot(const HostSlot& slot);
    // Thread-safe. Pool for one small attachment image (render or depth
    // target): drivers prefer dedicated memory for those, and transient
    // targets created per frame then paid vkAllocateMemory/vkFreeMemory.
    VmaPool smallImagePool(const VkImageCreateInfo& image);
    // Thread-safe dedicated host buffers (staging resources, init uploads).
    HostBuffer createHostBuffer(VkDeviceSize size,VkBufferUsageFlags usage,bool readback);
    void destroyHostBuffer(const HostBuffer& buffer);
    void invalidate(const HostBuffer& buffer);
    void flush(const HostBuffer& buffer);
    bool lost() const {return lost_.load(std::memory_order_acquire);}
    // Signals every registered device-removed event (ID3D11Device4 contract).
    void markLost();
    DWORD registerLostEvent(HANDLE event);
    void unregisterLostEvent(DWORD cookie);
    VkResult status() const {return lost()?VK_ERROR_DEVICE_LOST:VK_SUCCESS;}
private:
    struct Batch {VkCommandPool pool=VK_NULL_HANDLE;VkCommandBuffer command=VK_NULL_HANDLE;uint64_t serial=0;};
    struct InitBatch {VkCommandPool pool=VK_NULL_HANDLE;VkCommandBuffer command=VK_NULL_HANDLE;std::vector<std::function<void()>> after;};
    struct RingChunk {VkBuffer buffer=VK_NULL_HANDLE;VmaAllocation allocation=nullptr;uint8_t* data=nullptr;VkDeviceSize size=0;uint64_t serial=0;};
    struct Ring {
        Ring(const char* label,VkBufferUsageFlags flags,VkDeviceSize chunk):name(label),usage(flags),chunkSize(chunk){}
        const char* name;VkBufferUsageFlags usage;VkDeviceSize chunkSize;
        RingChunk current;VkDeviceSize cursor=0;std::deque<RingChunk> retired;
    };
    HostSlice allocate(Ring& ring,VkDeviceSize size,VkDeviceSize alignment);
    RingChunk createChunk(const Ring& ring,VkDeviceSize size);
    Batch acquireBatch();
    void begin() {if(!current_.command) beginBatch();}
    void beginBatch();
    void createInstance();
    void selectPhysicalDevice();
    void createDevice();
    VkInstance instance_=VK_NULL_HANDLE;
    VkPhysicalDevice physical_=VK_NULL_HANDLE;
    VkDevice device_=VK_NULL_HANDLE;
    VkQueue queue_=VK_NULL_HANDLE;
    uint32_t family_=0;
    VmaAllocator allocator_=nullptr;
    VkPhysicalDeviceProperties properties_{};
    DeviceCapabilities capabilities_{};
    std::string description_;
    LUID luid_{};bool luidValid_=false;
    VkSemaphore timeline_=VK_NULL_HANDLE;
    std::atomic<uint64_t> submitted_{0};
    uint64_t completed_=0;
    Batch current_;
    std::deque<Batch> inFlight_;
    std::vector<Batch> freeBatches_;
    std::mutex initMutex_;
    InitBatch init_;
    std::deque<std::pair<uint64_t,InitBatch>> initInFlight_;
    std::vector<InitBatch> freeInit_;
    std::mutex retireMutex_;
    std::deque<std::pair<uint64_t,std::function<void()>>> retirements_;
    Ring dynamic_{"dynamic",VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,8u<<20};
    Ring upload_{"upload",VK_BUFFER_USAGE_TRANSFER_SRC_BIT,16u<<20};
    struct SlotPage {VkBuffer buffer=VK_NULL_HANDLE;VmaAllocation allocation=nullptr;uint8_t* data=nullptr;};
    static constexpr uint32_t slotClasses=13; // 256 B .. 1 MiB
    std::mutex slotMutex_;
    std::array<std::vector<HostSlot>,slotClasses> freeSlots_;
    std::vector<SlotPage> slotPages_;
    std::atomic<bool> lost_{false};
    std::mutex imagePoolMutex_;
    std::map<std::tuple<VkFormat,VkImageUsageFlags,VkImageCreateFlags>,uint32_t> imagePoolTypes_;
    std::array<VmaPool,VK_MAX_MEMORY_TYPES> imagePools_{};
    std::mutex lostMutex_;
    std::vector<std::pair<DWORD,HANDLE>> lostEvents_;
    DWORD nextLostCookie_=1;
    CommandRecorder recorder_;
};
}
