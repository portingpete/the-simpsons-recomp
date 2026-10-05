#pragma once
#include "ppc_context.h"
#include "player_telemetry.h"
#include "native_video_settings.h"
#include "native_ultrawide_camera.h"
#include "resource_audit.h"
#include "../common/guest_write_watch.h"
#include "guest_access_table.h"
#include "frame_pacer.h"
#include <windows.h>
#include <filesystem>
#include <vector>
#include <string>
#include <stdexcept>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <array>
#include <atomic>
#include <functional>

namespace Simpsons {
namespace Platform {class NativeInputRecording;class NativeInputPlayback;}
class NativeWindow;
class EngineDriver;
struct Im2DProgramCache;
class EngineAudioOwners;
class EngineAudioReader;
class EngineAudioOutput;
struct GuestThread;
namespace Platform {class NativeNotifications;class NotificationListener;class NativeLocalPlayers;class NativeControllers;class NativeCommandInput;struct ContentEnumeration;class NativeSaveStore;class NativeSaveFile;}
struct ThreadExit { uint32_t code; };
struct Failure : std::runtime_error { using std::runtime_error::runtime_error; };
enum class MemoryUse { Virtual, Image, Stack, Kernel, Host, Physical, Pool };
struct Region { uint32_t address; uint64_t size; bool write; std::string name; MemoryUse use; };
struct Allocation { uint32_t address; uint64_t size; uint32_t pageSize; std::vector<uint8_t> committed; };
struct PhysicalAllocation { uint32_t address,physical,size,protect,pageSize; std::array<std::vector<uint32_t>,3> pageProtect; };
// Original caller-supplied graphics storage has no established earlier free.
// Retain its allocation/accounting until this guest address space is torn down.
// These are reservations only: native rendering never interprets their contents.
struct GraphicsStorageReservation {
    uint32_t context,allocator,freeCallback;
    std::array<uint32_t,2> address,physical,size;
};
// These are native receipt identities, never console SDK fence counters.
// Metadata survives driver restart while CF94/CF98 still refer to it.
struct GraphicsPresentReceipt {
    uint32_t context{},source{},front{};
    bool submitted{},copyCompleted{},displayTransferred{},displayAccepted{};
};
struct KernelHandle {
    enum class Type { Semaphore, Event, Mutant, Timer, Thread, File, Notification, ContentEnumerator };
    HANDLE native{};
    uint32_t guestObject{};
    std::shared_ptr<Platform::NotificationListener> notification;
    std::shared_ptr<Platform::ContentEnumeration> content;
    std::shared_ptr<Platform::NativeSaveFile> saveFile;
    std::mutex stateMutex;
    int32_t priorityIncrement=0;
    Type type;
    KernelHandle(HANDLE h,Type t):native(h),type(t) {}
    ~KernelHandle() { if(native&&!saveFile) CloseHandle(native); }
};
struct HeldObject {std::shared_ptr<KernelHandle> object;uint32_t count=0;};
struct CriticalSection {
    std::mutex mutex;
    std::condition_variable changed;
    uint32_t owner=0,recursion=0,waiters=0;
};
class Runtime {
public:
    ResourceAudit resourceAudit;
    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    void map(uint32_t address, uint64_t size, bool write, const char* name,MemoryUse use=MemoryUse::Virtual);
    // Demand-map zero-filled RW scratch for guest accesses below 0x10000
    // (null-console-device object traffic; the device global is null forever
    // under the native driver). Coherent RAM: writes persist, reads see them;
    // only initial content (zero) and GPU side effects differ, both void.
    // Returns false when nothing was mapped (already mapped, overlapping,
    // lock busy, oversized or commit failure) so callers rethrow loudly.
    bool mapZeroPage(uint32_t address, unsigned width);
    uint8_t* pointer(uint32_t address, unsigned width, bool write);
    // Same mapping, alias and permission proof as pointer(address,width,write) for a caller that
    // only reads (or merely requires writability): a write probe does not record a guest store,
    // so it never disarms the write watch of the pages it inspects.
    uint8_t* probe(uint32_t address, unsigned width, bool write);
    // Exact guest-write detection (see common/guest_write_watch.h). Indexed by
    // canonical 4 KiB page; aperture aliases resolve to one canonical page.
    void noteGuestWrite(uint32_t canonicalAddress,uint32_t width) noexcept;
    void invalidateWatchedPages(uint32_t address,uint64_t size) noexcept;
    GuestWriteWatch writeWatch() noexcept {return {base,writeWatchArmed.get(),writeWatchVersion.get()};}
    void load(const std::filesystem::path& image);
    void initialize(PPCContext& ctx);
    void bindData(uint32_t slot, uint32_t value);
    uint32_t allocateVirtual(uint32_t& address,uint32_t& size,uint32_t flags,uint32_t protect);
    uint32_t allocatePhysical(uint32_t flags,uint32_t size,uint32_t protect,uint32_t minimum,uint32_t maximum,uint32_t alignment);
    void freePhysical(uint32_t address);
    uint32_t physicalAddress(uint32_t address);
    void protectPhysical(uint32_t address,uint32_t size,uint32_t protect);
    uint32_t queryPhysicalProtect(uint32_t address);
    uint32_t addHandle(std::shared_ptr<KernelHandle> handle);
    std::shared_ptr<KernelHandle> getHandle(uint32_t handle);
    uint32_t closeHandle(uint32_t handle);
    std::shared_ptr<KernelHandle> threadObject(uint32_t address);
    std::shared_ptr<Platform::NativeNotifications> notificationSource();
    std::shared_ptr<Platform::NativeControllers> controllerSource();
    void configureLocalPlayers(const std::filesystem::path& root);
    std::shared_ptr<Platform::NativeLocalPlayers> localPlayerSource();
    std::shared_ptr<Platform::NativeSaveStore> nativeSaveSource(bool create=true);
    bool activateLocalPlayer(uint32_t slot,const std::string& profileId);
    bool signOutLocalPlayer(uint32_t slot);
    uint32_t localPlayerState(uint32_t slot);
    // Opaque identity of the guest thread object type. It points at a page that is never
    // mapped, so the CPU rejects a guest dereference and no per-access software guard is
    // needed (the slow path still names it in the failure).
    static constexpr uint32_t threadObjectType=0x7FFF0400;
    void checkRunning();
    void requestStop(const std::string& reason);
    void stopThreads();
    uint32_t createThread(PPCContext& caller);
    void initializeThread(PPCContext& ctx,uint32_t pcr,uint32_t thread,uint32_t tls,uint32_t stack,uint32_t stackSize,uint32_t id);
    void unmap(uint32_t address);
    uint64_t committedPages() const; // Caller holds vmMutex once multiple threads run.
    std::array<uint32_t,26> memoryStatistics();
    static constexpr uint32_t memoryBudgetPages=0x20000; // Native compatibility budget: 512 MiB.
    static constexpr uint32_t timebaseFrequency=50000000;
    uint8_t* base{};
    std::vector<Region> regions;
    std::vector<Allocation> allocations;
    std::vector<PhysicalAllocation> physicalAllocations;
    std::mutex handleMutex;
    uint32_t nextHandle=0x100;
    std::unordered_map<uint32_t,std::shared_ptr<KernelHandle>> handles;
    std::unordered_map<uint32_t,HeldObject> objectReferences;
    std::shared_ptr<KernelHandle> mainThreadHandle;
    std::unique_ptr<NativeWindow> window;
    std::shared_ptr<EngineDriver> engineDriver;
    // Provenance published only after the actual original CPU FX-pool constructor.
    // Native FX leases are separate from that SDK root's reference count.
    uint32_t effectPoolRoot{},effectPoolThread{};
    // Provenance of the original CPU shared-metadata allocation. The root can
    // outlive the native driver; defaults are mutable and never recopied.
    uint32_t effectPoolBacking{};
    std::shared_ptr<EngineAudioOwners> engineAudio;
    std::shared_ptr<EngineAudioReader> engineAudioReader;
    std::shared_ptr<EngineAudioOutput> engineAudioOutput;
    // Optional synchronous diagnostic observer at verified main-thread audio
    // boundaries. Production leaves it empty. A fixture may stop by throwing;
    // returning never substitutes any original/native operation or result.
    std::function<void(uint32_t,PPCContext&,uint8_t*)> audioBoundaryObserver;
    // Graphics fixtures can stop after verified original driver/CPU audio
    // construction, before opening a physical output. Production leaves this
    // empty; returning continues normal acquisition without substituting it.
    std::function<void(uint32_t,PPCContext&,uint8_t*)> graphicsStartupObserver;
    // Synchronous diagnostic observation before built-in memory-image loading.
    // Production leaves it empty; a fixture may stop by throwing a marker.
    std::function<void(uint32_t,PPCContext&,uint8_t*)> builtinImageBoundaryObserver;
    std::mutex audioMutex;
    std::vector<GraphicsStorageReservation> graphicsStorage;
    std::unordered_map<uint32_t,GraphicsPresentReceipt> graphicsPresentReceipts;
    std::filesystem::path gameRoot;
    std::filesystem::path contentRoot; // Optional native common-content store, outside original assets.
    std::filesystem::path frameCaptureDirectory; // Opt-in raw presented-frame evidence; disabled by default.
    PlayerTelemetry playerTelemetry; // Per-present original character-pose observation.
    bool captureOnRequest=false;
    bool renderTestFirstMission=false; // Temporary opt-in original -stream loc loc.str startup.
    bool firstMissionCompletion=false; // Opt-in original Land of Chocolate completion transition.
    bool bartmanBegins=false; // Opt-in original -stream brt brt.str stage startup.
    std::string auditStage; // Original -stream startup, selected from packaged map stems.
    bool auditStageMapStarted=false;
    bool auditStagePlayIntro=false;
    // Diagnostic identity retained only from a qualified original map-ready
    // boundary, until that exact gameplay owner finishes original cleanup.
    uint32_t auditGameplayOwner{};
    uint64_t auditGameplayGeneration{},auditGameplayBoundaryOrdinal{};
    std::string auditGameplayAsset,auditGameplayParameters;
    bool bartmanBeginsMapStarted=false; // Restore ordinary movie controls at the original map-ready boundary.
    bool firstMissionCompletionTriggered=false; // One original completion dispatch per opted-in launch.
    uint32_t firstMissionCompletionOwner{}; // Armed only after original map-start completion.
    uint32_t firstMissionCompletionManager{}; // Original LOC manager observed at that boundary.
    bool autoDefeatLocEnemies=false; // Opt-in native damage for NPCs in Land of Chocolate only.
    std::atomic<bool> landOfChocolateStreamSeen{false}; // Set by a successful top-level map stream open.
    std::atomic<uint64_t> nativeDepthCopyCount{0}; // Scene gate for exact returned-input playback.
    uint32_t requestedFrameRate=0; // 0 retains the original scheduler; 60 selects one refresh; other supported rates use native presentation pacing.
    bool uncappedFrameRate=false; // Opt-in zero-interval original wait for 120+ FPS testing; game dt remains real time.
    bool vsyncEnabled=false; // Normal launches use sync interval zero; --vsync selects interval one.
    NativeVideoSettings videoSettings;
    NativeVideoSettings videoSettingsBeforeMenu;
    NativeCameraProjectionState nativeCameraProjection;
    bool videoSettingsPending=true;
    std::filesystem::path videoSettingsPath,nativeFrontendRoot;
    uint64_t nativePresentationTimebase=0; // Real time sampled at the most recent native presentation entry.
    FramePacer framePacer; // Even-cadence presentation limiter state.
    // The original gameplay clock quantized every update to at least one
    // display refresh. Keep its elapsed time independent of presentation rate.
    struct GameplayTiming {
        uint32_t owner{};
        uint64_t previousTimebase{};
        double elapsedSeconds{},totalSeconds{};
        void sample(uint32_t clockOwner,uint64_t now);
        double advance(double maxSeconds);
        void reset(uint64_t now);
    } gameplayTiming;
    std::filesystem::path frameTimingPath; // Optional CSV of actual completed presentation intervals.
    bool frameTimingFramesOnly=false; // Omit diagnostic per-packet clocks; measure real frame intervals only.
    std::shared_ptr<Im2DProgramCache> im2dProgramCache; // Validated CPU-builder inputs, scoped to this loaded image.
    std::atomic<bool> stopping=false;
    HANDLE stopEvent{};
    std::mutex stopMutex,threadMutex;
    std::mutex notificationMutex;
    std::shared_ptr<Platform::NativeNotifications> notifications;
    std::mutex controllerMutex;
    std::shared_ptr<Platform::NativeControllers> controllers;
    std::shared_ptr<Platform::NativeCommandInput> controllerCommands; // Opt-in, configured before startup.
    std::shared_ptr<Platform::NativeInputRecording> inputRecording; // F8 starts after the player reaches the level.
    std::shared_ptr<Platform::NativeInputPlayback> inputPlayback; // Optional recorded XInput poll stream.
    std::string stopReason;
    std::vector<std::unique_ptr<GuestThread>> threads;
    std::vector<uint32_t> tlsBases;
    uint32_t nextThreadArea=0x01040000,nextThreadStack=0x02100000;
    GuestPageAccessTable pageAccess;
    void* vectoredHandler{}; // guest fault handler (null-device scratch demand map)
    std::unique_ptr<std::atomic<uint8_t>[]> writeWatchArmed{new std::atomic<uint8_t>[0x100000]{}};
    std::unique_ptr<std::atomic<uint32_t>[]> writeWatchVersion{new std::atomic<uint32_t>[0x100000]{}};
    std::mutex vmMutex;
    std::mutex criticalMutex;
    std::unordered_map<uint32_t,std::shared_ptr<CriticalSection>> criticalSections;
    std::vector<uint32_t> boundImports;
    uint32_t headerAddress = 0x01010000;
    uint32_t moduleAddress = 0x01020000;
    uint32_t nextAllocation = 0x10000000;
    uint32_t pcrAddress=0x01030000, threadAddress=0x01031000, staticTlsAddress=0x01032000, dynamicTlsAddress{};
    std::vector<bool> tlsSlots;
    bool checkingImports = false;
private:
    friend struct NativeMemoryContractProbe;
    uint8_t* pointerSlow(uint32_t address,unsigned width,bool write,bool note=true);
    std::mutex localPlayerMutex,localPlayerTransitionMutex;
    std::filesystem::path localProfileRoot;
    std::shared_ptr<Platform::NativeLocalPlayers> localPlayers;
    std::mutex nativeSaveMutex;
    std::shared_ptr<Platform::NativeSaveStore> nativeSaves;
};
extern Runtime* active;
extern thread_local PPCContext* currentContext;
// Native 120 FPS presentation limiter, called at present entry before the present is
// submitted: releases the present no earlier than the previous release plus the pacing
// interval (8.333 ms, or a high percentile of recent frame times when the machine cannot
// sustain 120; see frame_pacer.h). No guest clock, scheduler request, or simulation dt
// changes; game dt stays real time.
void SimpsonsNativeFramePace(Runtime& runtime);
void unwindAudioReaderCall(PPCContext&) noexcept;
int runOriginal(PPCContext& ctx, uint8_t* base);
uint32_t runThreadEntry(PPCContext& ctx,uint8_t* base,uint32_t address);
LONG exceptionFilter(EXCEPTION_POINTERS* info);
void dumpGuestStack(const PPCContext& ctx) noexcept;
std::vector<uint8_t> readFile(const std::filesystem::path& path);
}
