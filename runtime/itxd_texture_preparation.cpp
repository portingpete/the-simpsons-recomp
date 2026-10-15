#include "itxd_texture_preparation.h"
#include "renderer/itxd_blocks.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <deque>
#include <limits>
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Simpsons {
namespace {
constexpr size_t maximumBytes=64u*1024u*1024u;
constexpr size_t maximumPending=128,maximumReady=1024,bookkeepingBytes=4096;
struct Usage {size_t bytes{},count{},highBytes{},highCount{};};
struct Budget {
    std::mutex mutex;
    ITXDTexturePreparation::Limits limits;
    Usage cpu,gpu;
    explicit Budget(ITXDTexturePreparation::Limits value):limits(value) {
        limits.cpuBytes=std::min(limits.cpuBytes,maximumBytes);
        limits.gpuBytes=std::min(limits.gpuBytes,maximumBytes);
        limits.pendingJobs=std::min(limits.pendingJobs,maximumPending);
        limits.readyResources=std::min(limits.readyResources,maximumReady);
    }
};
struct Lease {
    std::shared_ptr<Budget> budget;
    size_t bytes{};
    bool gpu{};
    Lease(std::shared_ptr<Budget> value,size_t count,bool device):budget(std::move(value)),bytes(count),gpu(device){}
    ~Lease() {
        std::lock_guard lock(budget->mutex);
        auto& usage=gpu?budget->gpu:budget->cpu;
        usage.bytes-=bytes;--usage.count;
    }
};
std::shared_ptr<Lease> reserve(const std::shared_ptr<Budget>& budget,size_t bytes,bool gpu) {
    std::lock_guard lock(budget->mutex);
    auto& usage=gpu?budget->gpu:budget->cpu;
    const size_t bound=gpu?budget->limits.gpuBytes:budget->limits.cpuBytes;
    const size_t count=gpu?budget->limits.readyResources:budget->limits.pendingJobs;
    if(!bytes||usage.count>=count||bytes>bound||usage.bytes>bound-bytes)return {};
    auto lease=std::make_shared<Lease>(budget,bytes,gpu);
    usage.bytes+=bytes;++usage.count;
    usage.highBytes=std::max(usage.highBytes,usage.bytes);
    usage.highCount=std::max(usage.highCount,usage.count);
    return lease;
}
// Allocation planning only: dimensions bound CPU work, and the full native mip
// chain bounds every authored chain. Descriptor/layout admission remains solely
// in the existing pure decoder after the owned snapshot has been copied.
size_t outputBound(const ITXDTexturePreparation::Request& request) noexcept {
    using Graphics::TextureFormat;
    const bool rgba=request.format==TextureFormat::RGBA8;
    const bool bc=request.format==TextureFormat::BC1||request.format==TextureFormat::BC2||request.format==TextureFormat::BC3;
    const uint32_t maximum=rgba?1024u:2048u,minimum=rgba?32u:4u;
    if((!rgba&&!bc)||request.width<minimum||request.height<minimum||
       request.width>maximum||request.height>maximum)return 0;
    size_t full{},authored{};
    const unsigned levels=std::bit_width(std::max(request.width,request.height));
    const unsigned authoredLevels=((request.descriptor[4]>>6)&15)+1;
    for(unsigned level=0;level<levels;++level) {
        const auto width=std::max(1u,request.width>>level),height=std::max(1u,request.height>>level);
        const size_t bytes=rgba?size_t(width)*height*4:
            size_t((width+3)/4)*((height+3)/4)*(request.format==TextureFormat::BC1?8:16);
        full+=bytes;if(level<authoredLevels)authored+=bytes;
    }
    // Retain the existing CPU preparation's large-RGBA policy. Small palettes
    // and other small RGBA textures are not new preparation candidates.
    if(rgba&&authored<1024u*1024u)return 0;
    return full;
}
struct KeyHash {
    size_t operator()(const ITXDTexturePreparation::Key& key) const noexcept {
        const auto mixed=key.generation^(uint64_t(key.record)*0x9E3779B97F4A7C15ull);
        return size_t(mixed^(mixed>>32));
    }
};
}

struct ITXDTexturePreparation::Result {
    Request request;
    // All payloads die before their reservations, including failure/retirement.
    std::shared_ptr<Lease> cpuLease,gpuLease;
    std::vector<uint8_t> input;
    Levels levels;
    std::shared_ptr<Graphics::Texture> texture;
    Result* retirementNext{};
};
class ITXDTexturePreparation::Ticket {
    friend class ITXDTexturePreparation;
    enum class Phase {Queued,Running,Ready,Retired};
    Request request;
    std::weak_ptr<State> owner;
    std::atomic<bool> cancelled{};
    Phase phase=Phase::Queued;
    std::unique_ptr<Result> result;
    std::list<std::shared_ptr<Ticket>>::iterator readyPosition;
};
struct ITXDTexturePreparation::State {
    using Upload=std::function<std::shared_ptr<Graphics::Texture>(const Request&,const Levels&)>;
    std::shared_ptr<const Graphics::NativeImmutableMipFactory> factory;
    Upload testUpload;
    BeforeWork beforeDecode,beforeUpload;
    std::shared_ptr<Budget> budget;
    std::mutex mutex,stopMutex;
    std::deque<std::shared_ptr<Ticket>> queue;
    std::list<std::shared_ptr<Ticket>> ready;
    std::unordered_map<Key,std::shared_ptr<Ticket>,KeyHash> active;
    std::atomic<Result*> retirements{};
    std::atomic<uint64_t> wakeSequence{};
    std::atomic<bool> workerExited=true;
    std::thread worker;
#ifdef SIMPSONS_ITXD_TEXTURE_PREPARATION_TESTING
    std::shared_ptr<const std::function<void()>> beforeIdleWait;
#endif
    bool available{},stopping{};
    size_t running{};
    uint64_t submitted{},rejected{},completed{},failed{},taken{},evicted{};
    uint64_t rejectedProfile{},rejectedCpuBudget{},rejectedDuplicate{},rejectedUnavailable{},rejectedGpuBudget{};
    std::atomic<uint64_t> cancelled{},takeAbsent{},takeBusy{},takeMismatch{},takeQueued{},takeRunning{},takeCancelled{};

    State(std::shared_ptr<const Graphics::NativeImmutableMipFactory> value,Limits limits,
          BeforeWork decode,BeforeWork upload,Upload testing={}):factory(std::move(value)),testUpload(std::move(testing)),
        beforeDecode(std::move(decode)),beforeUpload(std::move(upload)),budget(std::make_shared<Budget>(limits)){}
    ~State(){drainRetirements();}
    void notify() noexcept {wakeSequence.fetch_add(1,std::memory_order_release);wakeSequence.notify_one();}
    void drainRetirements() noexcept {
        // Atomic handoff has one owner per detached node. Normally only worker
        // drains. After join, a late Ready is terminal teardown, not demand.
        auto* item=retirements.exchange(nullptr,std::memory_order_acquire);
        while(item){auto* next=item->retirementNext;delete item;item=next;}
    }
    void retireResult(std::unique_ptr<Result> result) noexcept {
        auto* item=result.release();if(!item)return;
        auto* previous=retirements.load(std::memory_order_relaxed);
        do {item->retirementNext=previous;}
        while(!retirements.compare_exchange_weak(previous,item,std::memory_order_release,std::memory_order_relaxed));
        notify();
        // Publish before checking exit. Worker publishes exit before its final
        // drain, so neither side can strand a retirement racing shutdown.
        if(workerExited.load(std::memory_order_acquire))drainRetirements();
    }
    void eraseLocked(const std::shared_ptr<Ticket>& job) noexcept {
        if(job->phase==Ticket::Phase::Ready)ready.erase(job->readyPosition);
        active.erase(job->request.key);job->phase=Ticket::Phase::Retired;
    }
    std::shared_ptr<Ticket> cancelledReadyLocked() noexcept {
        for(const auto& job:ready)if(job->cancelled.load(std::memory_order_acquire)) {
            auto discarded=job;eraseLocked(discarded);return discarded;
        }
        return {};
    }
    std::shared_ptr<Lease> reserveGpu(size_t bytes) {
        if(!bytes||bytes>budget->limits.gpuBytes||!budget->limits.readyResources)return {};
        for(;;) {
            auto lease=reserve(budget,bytes,true);if(lease)return lease;
            // Only completed speculative textures can be evicted. Demand-held
            // Ready receipts and the current driver operation stay charged.
            std::shared_ptr<Ticket> victim;
            {
                std::lock_guard lock(mutex);
                if(stopping||ready.empty())return {};
                victim=ready.front();eraseLocked(victim);++evicted;
            }
            victim->result.reset(); // Actual GPU/CPU destruction outside locks.
        }
    }
    void abandonAll() noexcept {
        for(;;) {
            std::shared_ptr<Ticket> job;
            {
                std::lock_guard lock(mutex);
                if(active.empty()){queue.clear();ready.clear();return;}
                job=active.begin()->second;
                if(!job->cancelled.exchange(true,std::memory_order_acq_rel))++cancelled;
                eraseLocked(job);
            }
            job->result.reset();
        }
    }
    void run() noexcept {
        for(;;) {
            // Capture BEFORE scanning either work source. Atomic wait rechecks
            // this value on registration, closing the scan-to-sleep gap without
            // requiring cancellation/Ready retirement to take the queue mutex.
            const auto observed=wakeSequence.load(std::memory_order_acquire);
            drainRetirements();
            std::shared_ptr<Ticket> job,discarded;
#ifdef SIMPSONS_ITXD_TEXTURE_PREPARATION_TESTING
            std::shared_ptr<const std::function<void()>> idleHook;
#endif
            {
                std::unique_lock lock(mutex);
                if(stopping) {
                    lock.unlock();abandonAll();
                    workerExited.store(true,std::memory_order_release);drainRetirements();return;
                }
                discarded=cancelledReadyLocked();
                if(!discarded&&!queue.empty()) {
                    job=std::move(queue.front());queue.pop_front();
                    if(job->cancelled.load(std::memory_order_acquire))eraseLocked(job);
                    else {job->phase=Ticket::Phase::Running;running=1;}
                }
#ifdef SIMPSONS_ITXD_TEXTURE_PREPARATION_TESTING
                if(!job&&!discarded)idleHook=beforeIdleWait;
#endif
            }
            if(discarded){discarded->result.reset();continue;}
            if(!job) {
                if(retirements.load(std::memory_order_acquire))continue;
#ifdef SIMPSONS_ITXD_TEXTURE_PREPARATION_TESTING
                if(idleHook)try {(*idleHook)();}catch(...) {}
#endif
                wakeSequence.wait(observed,std::memory_order_acquire);continue;
            }
            if(job->phase==Ticket::Phase::Retired){job->result.reset();continue;}
            bool success=false,gpuBudgetRejected=false;
            try {
                auto& result=*job->result;
                if(beforeDecode)beforeDecode(job->request);
                if(!job->cancelled.load(std::memory_order_acquire)) {
                    using Graphics::TextureFormat;
                    result.levels=job->request.format==TextureFormat::RGBA8?
                        Graphics::decodeITXDCandyRGBA8(job->request.descriptor,job->request.width,job->request.height,result.input):
                        Graphics::decodeITXDBCMipChain(job->request.descriptor,
                            job->request.format==TextureFormat::BC1?0x52u:job->request.format==TextureFormat::BC2?0x53u:0x54u,
                            job->request.width,job->request.height,result.input);
                    size_t gpuBytes{};for(const auto& level:result.levels)gpuBytes+=level.size();
                    result.gpuLease=reserveGpu(gpuBytes);
                    gpuBudgetRejected=!result.gpuLease;
                    if(result.gpuLease&&!job->cancelled.load(std::memory_order_acquire)) {
                        if(beforeUpload)beforeUpload(job->request);
                        if(!job->cancelled.load(std::memory_order_acquire)) {
                            if(testUpload)result.texture=testUpload(job->request,result.levels);
                            else {
                                std::vector<std::span<const uint8_t>> views;views.reserve(result.levels.size());
                                for(const auto& level:result.levels)views.emplace_back(level);
                                result.texture=factory->createMipChain(job->request.width,job->request.height,job->request.format,views);
                            }
                            success=bool(result.texture);
                        }
                    }
                }
            } catch(...) {
                // Speculation cannot change original demand error provenance.
            }
            // Keep every mip alive through both device creates. Pending CPU
            // slots are then reusable even if an untouched GPU result is ready.
            auto& result=*job->result;
            std::vector<uint8_t>().swap(result.input);Levels().swap(result.levels);result.cpuLease.reset();
            bool publish=false;
            {
                std::lock_guard lock(mutex);running=0;
                if(success&&!stopping&&!job->cancelled.load(std::memory_order_acquire)) {
                    try {
                        ready.push_back(job);job->readyPosition=std::prev(ready.end());
                        job->phase=Ticket::Phase::Ready;++completed;publish=true;
                    } catch(...) {success=false;}
                }
                if(!publish) {
                    if(!stopping&&!job->cancelled.load(std::memory_order_relaxed)) {
                        ++failed;if(gpuBudgetRejected)++rejectedGpuBudget;
                    }
                    eraseLocked(job);
                }
            }
            if(!publish)job->result.reset();
        }
    }
    void start() noexcept {
        if(!factory&&!testUpload)return;
        try {
            workerExited.store(false,std::memory_order_relaxed);
            worker=std::thread([this]{run();});available=true;
        } catch(...) {workerExited.store(true,std::memory_order_release);}
    }
};

ITXDTexturePreparation::Ready::Ready(std::shared_ptr<State> state,std::unique_ptr<Result> result) noexcept:
    state_(std::move(state)),result_(std::move(result)){}
ITXDTexturePreparation::Ready::~Ready(){if(result_)state_->retireResult(std::move(result_));}
ITXDTexturePreparation::Ready::Ready(Ready&&) noexcept = default;
const ITXDTexturePreparation::Request& ITXDTexturePreparation::Ready::request() const noexcept {return result_->request;}
const std::shared_ptr<Graphics::Texture>& ITXDTexturePreparation::Ready::texture() const noexcept {return result_->texture;}

ITXDTexturePreparation::ITXDTexturePreparation(std::shared_ptr<const Graphics::NativeImmutableMipFactory> factory):
    ITXDTexturePreparation(std::move(factory),Limits{}){}
ITXDTexturePreparation::ITXDTexturePreparation(std::shared_ptr<const Graphics::NativeImmutableMipFactory> factory,
        Limits limits,BeforeWork decode,BeforeWork upload) {
    try {state_=std::make_shared<State>(std::move(factory),limits,std::move(decode),std::move(upload));state_->start();}
    catch(...) {} // Optional setup failure retains existing CPU/synchronous path.
}
ITXDTexturePreparation::~ITXDTexturePreparation(){stop();}
ITXDTexturePreparation::Handle ITXDTexturePreparation::submit(const Request& request,std::span<const uint8_t> input) noexcept {
    if(!state_)return {};
    auto& state=*state_;
    try {
        const size_t bound=outputBound(request);
        if(!request.key.record||!request.key.generation||!bound) {
            std::lock_guard lock(state.mutex);++state.rejected;++state.rejectedProfile;return {};
        }
        if(input.size()>maximumBytes||input.size()>std::numeric_limits<size_t>::max()-bound-bookkeepingBytes) {
            std::lock_guard lock(state.mutex);++state.rejected;++state.rejectedCpuBudget;return {};
        }
        {
            std::lock_guard lock(state.mutex);
            if(state.stopping||!state.available){++state.rejected;++state.rejectedUnavailable;return {};}
            if(state.active.contains(request.key)){++state.rejected;++state.rejectedDuplicate;return {};}
        }
        auto lease=reserve(state.budget,input.size()+bound+bookkeepingBytes,false);
        if(!lease){std::lock_guard lock(state.mutex);++state.rejected;++state.rejectedCpuBudget;return {};}
        auto job=std::make_shared<Ticket>();job->request=request;job->owner=state_;
        job->result=std::make_unique<Result>();job->result->request=request;job->result->cpuLease=std::move(lease);
        job->result->input.assign(input.begin(),input.end());
        {
            std::lock_guard lock(state.mutex);
            if(state.stopping||!state.available){++state.rejected;++state.rejectedUnavailable;return {};}
            if(state.active.contains(request.key)){++state.rejected;++state.rejectedDuplicate;return {};}
            state.active.emplace(request.key,job);
            try {state.queue.push_back(job);}catch(...){state.active.erase(request.key);throw;}
            ++state.submitted;
        }
        state.notify();return job;
    } catch(...) {std::lock_guard lock(state.mutex);++state.rejected;return {};}
}
std::optional<ITXDTexturePreparation::Ready> ITXDTexturePreparation::takeReady(const Handle& handle,const Request& expected) noexcept {
    if(!state_)return {};
    auto& state=*state_;const auto job=handle.lock();
    if(!job||job->owner.lock()!=state_){++state.takeAbsent;return {};}
    std::unique_lock lock(state.mutex,std::try_to_lock);
    if(!lock){++state.takeBusy;return {};}
    if(job->request!=expected){++state.takeMismatch;return {};}
    if(state.stopping||job->cancelled.load(std::memory_order_acquire)){++state.takeCancelled;return {};}
    if(job->phase==Ticket::Phase::Queued){++state.takeQueued;return {};}
    if(job->phase==Ticket::Phase::Running){++state.takeRunning;return {};}
    if(job->phase!=Ticket::Phase::Ready||!job->result||!job->result->texture){++state.takeAbsent;return {};}
    Ready ready(state_,std::move(job->result));state.eraseLocked(job);++state.taken;
    return ready;
}
void ITXDTexturePreparation::cancel(const Handle& handle) noexcept {
    if(!state_)return;const auto job=handle.lock();
    if(!job||job->owner.lock()!=state_)return;
    if(!job->cancelled.exchange(true,std::memory_order_acq_rel))++state_->cancelled;
    state_->notify();
}
void ITXDTexturePreparation::stop() noexcept {
    if(!state_)return;auto& state=*state_;
    std::lock_guard stopLock(state.stopMutex);
    {std::lock_guard lock(state.mutex);state.stopping=true;}
    state.notify();if(state.worker.joinable())state.worker.join();
    state.workerExited.store(true,std::memory_order_release);state.drainRetirements();
}
ITXDTexturePreparation::Stats ITXDTexturePreparation::stats() const noexcept {
    if(!state_){Stats result;result.stopped=true;return result;}
    auto& state=*state_;std::lock_guard lock(state.mutex);std::lock_guard budgetLock(state.budget->mutex);
    Stats result;
    result.cpuReservedBytes=state.budget->cpu.bytes;result.pendingJobs=state.budget->cpu.count;
    result.gpuReservedBytes=state.budget->gpu.bytes;result.gpuReservedResources=state.budget->gpu.count;
    result.highWaterCpuBytes=state.budget->cpu.highBytes;result.highWaterPendingJobs=state.budget->cpu.highCount;
    result.highWaterGpuBytes=state.budget->gpu.highBytes;result.highWaterGpuResources=state.budget->gpu.highCount;
    result.queued=state.queue.size();result.running=state.running;result.ready=state.ready.size();
    result.submitted=state.submitted;result.rejected=state.rejected;result.completed=state.completed;
    result.failed=state.failed;result.taken=state.taken;result.cancelled=state.cancelled.load();result.evicted=state.evicted;
    result.takeAbsent=state.takeAbsent.load();result.takeBusy=state.takeBusy.load();result.takeMismatch=state.takeMismatch.load();
    result.takeQueued=state.takeQueued.load();result.takeRunning=state.takeRunning.load();result.takeCancelled=state.takeCancelled.load();
    result.rejectedProfile=state.rejectedProfile;result.rejectedCpuBudget=state.rejectedCpuBudget;
    result.rejectedDuplicate=state.rejectedDuplicate;result.rejectedUnavailable=state.rejectedUnavailable;
    result.rejectedGpuBudget=state.rejectedGpuBudget;
    result.available=state.available;result.stopped=state.stopping;return result;
}
#ifdef SIMPSONS_ITXD_TEXTURE_PREPARATION_TESTING
ITXDTexturePreparation::ITXDTexturePreparation(Limits limits,TestUpload upload,BeforeWork decode,BeforeWork beforeUpload) {
    try {state_=std::make_shared<State>(nullptr,limits,std::move(decode),std::move(beforeUpload),std::move(upload));state_->start();}
    catch(...) {}
}
void ITXDTexturePreparation::testingWithStateLock(const std::function<void()>& work) {
    if(!state_)return;std::lock_guard lock(state_->mutex);work();
}
void ITXDTexturePreparation::testingBeforeIdleWait(std::function<void()> work) {
    if(!state_)return;
    auto hook=work?std::make_shared<const std::function<void()>>(std::move(work)):nullptr;
    {std::lock_guard lock(state_->mutex);state_->beforeIdleWait=std::move(hook);}
    state_->notify();
}
#endif
}
