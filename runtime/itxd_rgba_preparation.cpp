#include "itxd_rgba_preparation.h"
#include "renderer/itxd_blocks.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Simpsons {
namespace {
constexpr size_t bookkeepingBytes=4096;
struct Layout {size_t input{},output{};uint32_t levels{};};
std::optional<Layout> qualified(const std::array<uint32_t,6>& d,uint32_t width,uint32_t height,size_t input) {
    // Match the existing decoder's large power-of-two family. Qualification
    // never converts pixels and never admits a padded/deeper/shared-tail layout.
    if(width<32||height<32||width>1024||height>1024||
       !std::has_single_bit(width)||!std::has_single_bit(height))return {};
    const uint32_t maxLevel=(d[4]>>6)&15;
    const uint32_t packedLevel=std::min(std::bit_width(width),std::bit_width(height))-5;
    if(d[0]!=(0x80000002u|((width/32)<<22))||d[1]!=0x86||
       d[2]!=((height-1)<<13|(width-1))||d[3]!=0xC14||d[4]!=(maxLevel<<6)||
       maxLevel>packedLevel||d[5]!=(maxLevel?(width*height*4|0xA00u):0x200u))return {};
    Layout layout{0,0,maxLevel+1};
    for(uint32_t level=0;level<layout.levels;++level) {
        layout.input+=size_t(std::max(32u,width>>level))*std::max(32u,height>>level)*4;
        layout.output+=size_t(width>>level)*(height>>level)*4;
    }
    if(layout.input!=input)return {};
    return layout;
}
std::optional<Layout> qualifiedBC2Base(const std::array<uint32_t,6>& d,
        uint32_t width,uint32_t height,size_t input) {
    // Exact base-only subset of decodeITXDBCMipChain. Keep RGBA admission intact.
    if(width<4||height<4||width>2048||height>2048||
       !std::has_single_bit(width)||!std::has_single_bit(height))return {};
    if((d[0]&0x803FFFFFu)!=0x80000002u||d[1]!=0x53||
       d[2]!=((height-1)<<13|(width-1))||d[3]!=0xD10||d[4]||d[5]!=0x200)return {};
    const uint32_t pitch=((d[0]>>22)&511)*32;
    if(pitch<width||pitch%128||pitch>2048)return {};
    const size_t columns=(((pitch+3)/4)+31)&~size_t(31);
    const size_t rows=(((height+3)/4)+31)&~size_t(31);
    const size_t bytes=(columns*rows*16+4095)&~size_t(4095);
    if(input!=bytes)return {};
    return Layout{bytes,size_t((width+3)/4)*((height+3)/4)*16,1};
}
}

struct ITXDRGBAPreparation::Budget {
    std::mutex mutex;
    Limits limits;
    size_t bytes{},jobs{};
    explicit Budget(Limits value):limits(value){
        limits.jobs=std::min(limits.jobs,size_t(8));
        limits.bytes=std::min(limits.bytes,size_t(64u*1024u*1024u));
    }
};
struct ITXDRGBAPreparation::Lease {
    std::shared_ptr<Budget> budget;
    size_t bytes{};
    Lease(std::shared_ptr<Budget> value,size_t count):budget(std::move(value)),bytes(count){}
    ~Lease() {
        std::lock_guard lock(budget->mutex);
        budget->bytes-=bytes;--budget->jobs;
    }
};
class ITXDRGBAPreparation::Ticket {
    friend class ITXDRGBAPreparation;
    enum class Phase {Queued,Running,Ready,Retired};
    Key key;
    Kind kind=Kind::RGBA8;
    const State* owner{};
    std::array<uint32_t,6> descriptor;
    uint32_t width{},height{};
    Layout layout;
    uint64_t admissionOrder{};
    std::atomic<bool> cancelled{};
    Phase phase=Phase::Queued;
    // Buffers are destroyed before the reservation, including exception paths.
    std::shared_ptr<Lease> lease;
    std::vector<uint8_t> input;
    Levels output;
};
struct ITXDRGBAPreparation::State {
    struct Retired {
        std::shared_ptr<Lease> lease;
        std::vector<uint8_t> input;
        Levels output;
    };
    std::shared_ptr<Budget> budget;
    BeforeDecode beforeDecode;
    std::mutex mutex,stopMutex;
    std::condition_variable wake;
    std::deque<std::shared_ptr<Ticket>> queue;
    std::unordered_map<Ticket*,std::shared_ptr<Ticket>> active;
    std::thread worker;
    bool available{},stopping{};
    size_t running{};
    uint64_t submitted{},rejected{},completed{},failed{},taken{},cancelled{};
    State(Limits limits,BeforeDecode hook):budget(std::make_shared<Budget>(limits)),beforeDecode(std::move(hook)){}
    Retired retire(const std::shared_ptr<Ticket>& job) {
        // Move ownership only. Multi-MiB deallocation and budget release happen
        // after unlocking; the lease outlives both buffers in Retired.
        Retired result{std::move(job->lease),std::move(job->input),std::move(job->output)};
        job->phase=Ticket::Phase::Retired;
        active.erase(job.get());return result;
    }
    void reclaimBC2For(Key key,size_t charge) {
        std::array<Retired,8> retired;
        std::array<std::shared_ptr<Ticket>,8> victims;
        size_t count{};
        {
            // Match stats(): state then budget, never the reverse. Retirement
            // only moves ownership; both buffers and lease are freed afterward.
            std::lock_guard lock(mutex);
            if(stopping||!available)return;
            for(const auto& [identity,job]:active) {
                (void)identity;
                if(job->key==key)return;
            }
            std::lock_guard budgetLock(budget->mutex);
            if(!budget->limits.jobs||charge>budget->limits.bytes)return;
            const auto fits=[&](size_t jobs,size_t bytes) {
                return jobs<budget->limits.jobs&&bytes<=budget->limits.bytes-charge;
            };
            if(fits(budget->jobs,budget->bytes))return;
            for(const auto& [identity,job]:active) {
                (void)identity;
                if(job->kind==Kind::BC2Base&&job->lease&&
                   (job->phase==Ticket::Phase::Queued||job->phase==Ticket::Phase::Ready))
                    victims[count++]=job;
            }
            std::sort(victims.begin(),victims.begin()+count,[](const auto& a,const auto& b) {
                return a->admissionOrder<b->admissionOrder;
            });
            size_t jobs=budget->jobs,bytes=budget->bytes,needed{};
            while(needed<count&&!fits(jobs,bytes)) {
                bytes-=victims[needed]->lease->bytes;--jobs;++needed;
            }
            // Protected running/in-flight/taken leases may make reclamation
            // insufficient. Leave every speculative ticket intact in that case.
            if(!fits(jobs,bytes))return;
            for(size_t i=0;i<needed;++i) {
                const auto& job=victims[i];
                if(!job->cancelled.exchange(true,std::memory_order_acq_rel))++cancelled;
                queue.erase(std::remove(queue.begin(),queue.end(),job),queue.end());
                retired[i]=retire(job);
            }
        }
        // A taken Ready value never appears in active; its live reservation is
        // still included above. Retired leases remain charged until this exit.
    }
    void run() noexcept {
        for(;;) {
            std::shared_ptr<Ticket> job;
            Retired abandoned;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock,[&]{return stopping||!queue.empty();});
                if(stopping)return;
                job=std::move(queue.front());queue.pop_front();
                if(job->cancelled.load(std::memory_order_acquire)){abandoned=retire(job);continue;}
                job->phase=Ticket::Phase::Running;running=1;
            }
            Levels result;
            bool success=false;
            try {
                if(beforeDecode)beforeDecode(job->key);
                if(!job->cancelled.load(std::memory_order_acquire)) {
                    result=job->kind==Kind::RGBA8?
                        Graphics::decodeITXDCandyRGBA8(job->descriptor,job->width,job->height,job->input):
                        Graphics::decodeITXDBCMipChain(job->descriptor,0x53,job->width,job->height,job->input);
                    success=result.size()==job->layout.levels;
                    for(size_t level=0;success&&level<result.size();++level)
                        success=result[level].size()==(job->kind==Kind::RGBA8?
                            size_t(job->width>>level)*(job->height>>level)*4:job->layout.output);
                }
            } catch(...) {
                // Preparation errors are speculative. The existing synchronous
                // demand decoder retains its original error and provenance.
            }
            // Running cancellation never touches input; only this worker frees
            // it. Finish deallocation before publishing a transferable lease.
            std::vector<uint8_t>().swap(job->input);
            Retired retired;
            {
                std::lock_guard lock(mutex);
                running=0;
                if(success&&!stopping&&!job->cancelled.load(std::memory_order_acquire)) {
                    job->output=std::move(result);job->phase=Ticket::Phase::Ready;++completed;
                } else {
                    job->output=std::move(result);
                    if(!job->cancelled.load(std::memory_order_relaxed)&&!stopping)++failed;
                    retired=retire(job);
                }
            }
        }
    }
};

ITXDRGBAPreparation::ITXDRGBAPreparation():ITXDRGBAPreparation(Limits{}){}
ITXDRGBAPreparation::ITXDRGBAPreparation(Limits limits,BeforeDecode hook):
    state_(std::make_shared<State>(limits,std::move(hook))) {
    try {
        state_->worker=std::thread([state=state_.get()]{state->run();});
        state_->available=true;
    } catch(...) {state_->stopping=true;}
}
ITXDRGBAPreparation::~ITXDRGBAPreparation(){stop();}

bool ITXDRGBAPreparation::qualifiesProfile(const std::array<uint32_t,6>& descriptor,
        uint32_t width,uint32_t height,size_t inputBytes) const noexcept {
    const auto layout=qualified(descriptor,width,height,inputBytes);
    return layout&&layout->output>=state_->budget->limits.minimumOutputBytes;
}

ITXDRGBAPreparation::Handle ITXDRGBAPreparation::submit(Key key,
        const std::array<uint32_t,6>& descriptor,uint32_t width,uint32_t height,
        std::span<const uint8_t> ownedInput) noexcept {
    return submitKind(Kind::RGBA8,key,descriptor,width,height,ownedInput);
}
ITXDRGBAPreparation::Handle ITXDRGBAPreparation::submitBC2Base(Key key,
        const std::array<uint32_t,6>& descriptor,uint32_t width,uint32_t height,
        std::span<const uint8_t> ownedInput) noexcept {
    return submitKind(Kind::BC2Base,key,descriptor,width,height,ownedInput);
}
ITXDRGBAPreparation::Handle ITXDRGBAPreparation::submitKind(Kind kind,Key key,
        const std::array<uint32_t,6>& descriptor,uint32_t width,uint32_t height,
        std::span<const uint8_t> ownedInput) noexcept {
    auto& state=*state_;
    try {
        const auto layout=kind==Kind::RGBA8?qualified(descriptor,width,height,ownedInput.size()):
            qualifiedBC2Base(descriptor,width,height,ownedInput.size());
        if(!key.record||!key.generation||!layout||layout->output<state.budget->limits.minimumOutputBytes) {
            std::lock_guard lock(state.mutex);++state.rejected;return {};
        }
        const size_t charge=layout->input+layout->output+bookkeepingBytes;
        {
            std::lock_guard lock(state.mutex);
            if(state.stopping||!state.available||!state.budget->limits.jobs||charge>state.budget->limits.bytes) {
                ++state.rejected;return {};
            }
            for(const auto& [identity,pending]:state.active) {
                (void)identity;
                if(pending->key==key){++state.rejected;return {};}
            }
        }
        const auto reserve=[&]() {
            std::shared_ptr<Lease> result;
            std::lock_guard lock(state.budget->mutex);
            if(state.budget->jobs<state.budget->limits.jobs&&
               state.budget->bytes<=state.budget->limits.bytes-charge) {
                // Allocate the lease before publishing its reservation. Its
                // constructor cannot throw after shared allocation succeeds.
                result=std::make_shared<Lease>(state.budget,charge);
                state.budget->bytes+=charge;++state.budget->jobs;
            }
            return result;
        };
        auto lease=reserve();
        if(!lease) {
            state.reclaimBC2For(key,charge);
            // One bounded retry only. Another producer may take the freed
            // capacity; ordinary nonblocking rejection/fallback remains valid.
            lease=reserve();
        }
        if(!lease){std::lock_guard lock(state.mutex);++state.rejected;return {};}
        auto job=std::make_shared<Ticket>();
        job->key=key;job->kind=kind;job->owner=&state;job->descriptor=descriptor;job->width=width;job->height=height;job->layout=*layout;
        job->lease=std::move(lease);
        // Copy only CPU-owned input, outside the state lock. In-flight snapshots
        // already count toward both bounds, including concurrent producers.
        job->input.assign(ownedInput.begin(),ownedInput.end());
        {
            std::lock_guard lock(state.mutex);
            if(state.stopping||!state.available){++state.rejected;return {};}
            for(const auto& [identity,pending]:state.active) {
                (void)identity;
                if(pending->key==key){++state.rejected;return {};}
            }
            state.active.emplace(job.get(),job);
            try {state.queue.push_back(job);}catch(...){state.active.erase(job.get());throw;}
            job->admissionOrder=state.submitted;
            ++state.submitted;
        }
        state.wake.notify_one();return job;
    } catch(...) {
        std::lock_guard lock(state.mutex);++state.rejected;return {};
    }
}

std::optional<ITXDRGBAPreparation::Ready> ITXDRGBAPreparation::takeReady(const Handle& handle,Key expected) noexcept {
    return takeReadyKind(handle,expected,Kind::RGBA8,nullptr,0,0);
}
std::optional<ITXDRGBAPreparation::Ready> ITXDRGBAPreparation::takeReadyBC2Base(
        const Handle& handle,Key expected,const std::array<uint32_t,6>& descriptor,
        uint32_t width,uint32_t height) noexcept {
    return takeReadyKind(handle,expected,Kind::BC2Base,&descriptor,width,height);
}
std::optional<ITXDRGBAPreparation::Ready> ITXDRGBAPreparation::takeReadyKind(
        const Handle& handle,Key expected,Kind kind,const std::array<uint32_t,6>* descriptor,
        uint32_t width,uint32_t height) noexcept {
    const auto job=handle.lock();if(!job||job->owner!=state_.get())return {};
    auto& state=*state_;
    std::unique_lock lock(state.mutex,std::try_to_lock);
    if(!lock||state.stopping||job->kind!=kind||job->key!=expected||job->cancelled.load(std::memory_order_acquire)||
       job->phase!=Ticket::Phase::Ready)return {};
    if(descriptor&&(job->descriptor!=*descriptor||job->width!=width||job->height!=height))return {};
    Ready ready(std::move(job->lease),std::move(job->output));
    job->phase=Ticket::Phase::Retired;state.active.erase(job.get());++state.taken;
    return ready;
}
void ITXDRGBAPreparation::cancel(const Handle& handle) noexcept {
    const auto job=handle.lock();if(!job||job->owner!=state_.get())return;
    const bool wasCancelled=job->cancelled.exchange(true,std::memory_order_acq_rel);
    auto& state=*state_;
    State::Retired retired;
    {
        std::lock_guard lock(state.mutex);
        if(!wasCancelled)++state.cancelled;
        state.queue.erase(std::remove(state.queue.begin(),state.queue.end(),job),state.queue.end());
        if(job->phase==Ticket::Phase::Running)state.active.erase(job.get());
        else retired=state.retire(job);
    }
}
void ITXDRGBAPreparation::stop() noexcept {
    auto& state=*state_;
    std::lock_guard stopLock(state.stopMutex);
    std::array<State::Retired,8> retired;
    size_t count{};
    {
        std::lock_guard lock(state.mutex);
        state.stopping=true;
        for(auto& [identity,job]:state.active) {
            (void)identity;
            if(!job->cancelled.exchange(true,std::memory_order_acq_rel))++state.cancelled;
            if(job->phase!=Ticket::Phase::Running) {
                retired[count++]={std::move(job->lease),std::move(job->input),std::move(job->output)};
                job->phase=Ticket::Phase::Retired;
            }
        }
        state.active.clear();state.queue.clear();
    }
    state.wake.notify_all();
    if(state.worker.joinable())state.worker.join();
}
ITXDRGBAPreparation::Stats ITXDRGBAPreparation::stats() const noexcept {
    auto& state=*state_;
    std::lock_guard lock(state.mutex);
    std::lock_guard budgetLock(state.budget->mutex);
    Stats result;
    result.reservedBytes=state.budget->bytes;result.outstandingJobs=state.budget->jobs;
    result.queued=state.queue.size();result.running=state.running;
    for(const auto& [identity,job]:state.active){(void)identity;if(job->phase==Ticket::Phase::Ready)++result.ready;}
    result.submitted=state.submitted;result.rejected=state.rejected;result.completed=state.completed;
    result.failed=state.failed;result.taken=state.taken;result.cancelled=state.cancelled;result.stopped=state.stopping;
    return result;
}
}
