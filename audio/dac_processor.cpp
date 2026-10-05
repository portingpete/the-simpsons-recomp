#include "dac_processor.h"
#include "dac_gain.h"
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <xmmintrin.h>

namespace Simpsons::Audio {
namespace {
using Output=NativeAudioOutput;
std::atomic<uint64_t> generations{1};
uint64_t nextGeneration() {
    auto id=generations.load();
    while(id!=std::numeric_limits<uint64_t>::max())
        if(generations.compare_exchange_weak(id,id+1)) return id;
    throw OutputError("Dac processor generation exhausted",E_FAIL);
}
void require(bool value,const char* why) {if(!value) throw OutputError(why,E_INVALIDARG);}
bool normalized(float value) {return (std::bit_cast<uint32_t>(value)&0x7FFFFFFFu)<=0x3F800000u;}
bool gain(float value) {
    const auto bits=std::bit_cast<uint32_t>(value),absolute=bits&0x7FFFFFFFu;
    return absolute<=0x3F800000u && (!(bits&0x80000000u) || !absolute);
}
struct Event {
    HANDLE handle;
    explicit Event(bool manual):handle(CreateEventW(nullptr,manual,FALSE,nullptr)) {
        if(!handle) throw OutputError("Create Dac processor event",HRESULT_FROM_WIN32(GetLastError()));
    }
    ~Event() {CloseHandle(handle);}
};
struct FPFrame {unsigned saved=_mm_getcsr();~FPFrame(){_mm_setcsr(saved);}};
struct HostCallFP:FPFrame {HostCallFP(){_mm_setcsr(0x1F80);}};
}

struct DacProcessor::State {
    struct Source {
        bool used=false;
        SourceCompletion completion;
        std::array<float,Output::blockSamples> raw{};
#ifdef SIMPSONS_DAC_PROCESSOR_TESTS
        std::array<float,Output::blockSamples> processed{};
        std::array<float,Output::blockSamples> submitted{};
#endif
    };
    struct Downstream {bool used=false;DownstreamCompletion completion;};
    Output& backend;
    const uint64_t generation=nextGeneration(),backendGeneration;
    mutable std::mutex mutex;
    std::mutex joinMutex;
    std::condition_variable readyCV;
    Event wake{true},control{false},stopEvent{true};
    std::thread worker;
    bool ready=false,active=false,stopping=false,stopped=false,workerExited=false;
    float current=0,target=1;
    uint64_t sequence=1,processingHints=0,starvationPasses=0,backendRetired=0;
    Failure failure;
    std::array<Source,sourceCapacity> sources;
    std::array<Downstream,downstreamCapacity> downstream;
#ifdef SIMPSONS_DAC_PROCESSOR_TESTS
    bool holdDsp=false,holdSubmit=false,fpRestored=true;
#endif
    explicit State(Output& value):backend(value),backendGeneration(value.generation()) {
        const auto snapshot=backend.poll();
        require(snapshot.configured && !snapshot.running && !snapshot.stopped && !snapshot.error &&
                snapshot.owned==0 && snapshot.capacity==downstreamCapacity,
                "Dac processor requires configured stopped empty capacity4 output");
    }
    ~State() {
        try {close();}catch(...) {std::terminate();} // No client callback can destroy this on its worker.
    }
    void failLocked(FailureStage stage,int32_t error,const char* operation,
                    Receipt source={},Output::Receipt receipt={},Output::ErrorOrigin origin=Output::ErrorOrigin::None) noexcept {
        if(failure.stage==FailureStage::None) {
            failure.stage=stage;failure.error=FAILED(error)?error:E_UNEXPECTED;
            failure.source=source;failure.downstream=receipt;
            failure.backendOrigin=origin;
            try {failure.operation=operation;}catch(...) {failure.operation.clear();}
        }
    }
    void signalLocked() noexcept {
        if(!SetEvent(wake.handle)) failLocked(FailureStage::Wake,HRESULT_FROM_WIN32(GetLastError()),"Signal Dac observer wake");
    }
    void requestStopLocked() noexcept {
        stopping=true;
        if(!SetEvent(stopEvent.handle)) failLocked(FailureStage::Wake,HRESULT_FROM_WIN32(GetLastError()),"Signal Dac stop event");
        readyCV.notify_all();signalLocked();
    }
    void kickLocked() noexcept {
        if(!SetEvent(control.handle)) {
            failLocked(FailureStage::Wake,HRESULT_FROM_WIN32(GetLastError()),"Signal Dac work event");requestStopLocked();
        }
    }
    void usableLocked() const {
        if(failure.stage!=FailureStage::None) throw OutputError(failure.operation.c_str(),failure.error);
        if(stopping || stopped) throw OutputError("Dac processor stopped",HRESULT_FROM_WIN32(ERROR_INVALID_STATE));
    }
    Source& findLocked(Receipt receipt) {
        require(receipt.generation==generation && receipt.sequence,"Foreign Dac source receipt");
        for(auto& slot:sources) if(slot.used && slot.completion.receipt==receipt) return slot;
        throw OutputError("Retired/unknown Dac source receipt",E_INVALIDARG);
    }
    Source* firstQueuedLocked() {
        Source* found=nullptr;
        for(auto& slot:sources) if(slot.used && slot.completion.status==SourceStatus::Queued &&
            (!found || slot.completion.receipt.sequence<found->completion.receipt.sequence)) found=&slot;
        return found;
    }
    Downstream* freeDownstreamLocked() {
        for(auto& slot:downstream) if(!slot.used) return &slot;
        return nullptr;
    }
    bool incrementLocked(uint64_t& counter,const char* name) {
        if(counter==std::numeric_limits<uint64_t>::max()) {
            failLocked(FailureStage::BackendState,E_FAIL,name);requestStopLocked();return false;
        }
        ++counter;return true;
    }
    Output::ErrorOrigin backendErrorOrigin(int32_t error) noexcept {
        // A callback can arrive between preflight poll and submit/start. Keep
        // its actual origin when the thrown HRESULT is the published first
        // backend error; never replace the original exception or operation.
        try {const auto snapshot=backend.poll();return snapshot.error==error?snapshot.errorOrigin:Output::ErrorOrigin::None;}
        catch(...) {return Output::ErrorOrigin::None;}
    }
    void harvestLocked(const Output::Snapshot& snapshot) {
        if(snapshot.generation!=backendGeneration || !snapshot.configured || snapshot.capacity!=downstreamCapacity)
            failLocked(FailureStage::BackendState,E_UNEXPECTED,"Dac output ownership/configuration changed");
        uint32_t tracked=0;
        for(const auto& slot:downstream) if(slot.used && !slot.completion.retired) ++tracked;
        if(snapshot.owned!=tracked)
            failLocked(FailureStage::BackendState,E_UNEXPECTED,"Dac output contains foreign/missing receipts");
        for(const auto& result:snapshot.completed) {
            Downstream* found=nullptr;
            for(auto& slot:downstream) if(slot.used && !slot.completion.retired &&
                slot.completion.completion.receipt==result.receipt) {found=&slot;break;}
            if(!found) {
                failLocked(FailureStage::BackendState,E_UNEXPECTED,"Unowned Dac downstream completion",{},result.receipt);continue;
            }
            found->completion.completion=result;
            if(result.status==Output::BufferStatus::Failed ||
               (result.status==Output::BufferStatus::Cancelled && !stopping))
                failLocked(FailureStage::BackendState,result.error?result.error:E_ABORT,
                           "Dac downstream failed/cancelled",found->completion.source,result.receipt,snapshot.errorOrigin);
            try {
                backend.retire(result.receipt);found->completion.retired=true;
                incrementLocked(backendRetired,"Dac downstream retirement counter exhausted");
            }catch(const OutputError& e) {failLocked(FailureStage::Retire,e.status(),e.what(),found->completion.source,result.receipt,backendErrorOrigin(e.status()));}
            signalLocked();
        }
        if(snapshot.error) failLocked(FailureStage::BackendState,snapshot.error,"Dac backend reported failure",{},{},snapshot.errorOrigin);
        if(snapshot.stopped && !stopping) failLocked(FailureStage::BackendState,E_ABORT,"Dac backend stopped externally");
    }
    bool processLocked(std::unique_lock<std::mutex>& lock,Source& source,Downstream& output) {
#ifdef SIMPSONS_DAC_PROCESSOR_TESTS
        readyCV.wait(lock,[&]{return !holdDsp || stopping;});
#endif
        if(stopping) return false;
        auto& receipt=source.completion;
        receipt.status=SourceStatus::Processing;receipt.initialGain=current;receipt.targetGain=target;
        DacGainState dsp{256,0,256,0,current,target};
        std::array<float,Output::blockSamples> planar,interleaved;
        signalLocked();lock.unlock();
        int32_t dspError=0;std::string dspMessage;
        try {
            const auto before=_mm_getcsr();
            const auto processed=processDacGain(source.raw,planar,dsp);
            require(processed.work==DacGainWork::Processed && processed.frames==256 &&
                    dsp.sourceProgress==256 && dsp.destinationProgress==256,"Incomplete Dac DSP block");
            if(_mm_getcsr()!=before) throw OutputError("Dac DSP leaked host MXCSR",E_UNEXPECTED);
            for(uint32_t frame=0;frame<256;++frame) for(uint32_t component=0;component<6;++component)
                interleaved[frame*6+component]=planar[component*256+frame];
        }catch(const std::exception& e) {dspError=E_INVALIDARG;dspMessage=e.what();}
        lock.lock();
        if(dspError) {
            failLocked(FailureStage::Dsp,dspError,dspMessage.c_str(),receipt.receipt);requestStopLocked();return false;
        }
        receipt.processedFrames=256;receipt.finalGain=dsp.current;
#ifdef SIMPSONS_DAC_PROCESSOR_TESTS
        source.processed=interleaved;
        readyCV.wait(lock,[&]{return !holdSubmit || stopping;});
#endif
        if(stopping) return false;
        try {
            const auto before=_mm_getcsr();
            const auto submitted=backend.submit(interleaved);
            if(_mm_getcsr()!=before) throw OutputError("Dac output submission leaked host MXCSR",E_UNEXPECTED);
            if(submitted.status!=Output::SubmitStatus::Accepted) {
                // The exclusive owner checked free capacity immediately before
                // DSP. Unexpected backpressure cannot be relabelled consumption.
                failLocked(FailureStage::Submit,E_UNEXPECTED,"Dac downstream capacity changed during exclusive processing",receipt.receipt);
                requestStopLocked();return false;
            }
            receipt.downstream=submitted.receipt;
            output.used=true;
            output.completion={receipt.receipt,{submitted.receipt,Output::BufferStatus::Pending,0},false};
#ifdef SIMPSONS_DAC_PROCESSOR_TESTS
            const auto copy=NativeAudioOutputTestAccess::copied(backend,submitted.receipt);
            std::copy(copy.begin(),copy.end(),source.submitted.begin());
#endif
            harvestLocked(backend.poll());
            if(failure.stage!=FailureStage::None) {requestStopLocked();return false;}
            // The source stage commits only after full DSP plus REAL acceptance.
            current=dsp.current;receipt.status=SourceStatus::Consumed;signalLocked();return true;
        }catch(const OutputError& e) {
            failLocked(FailureStage::Submit,e.status(),e.what(),receipt.receipt,receipt.downstream,backendErrorOrigin(e.status()));
            requestStopLocked();return false;
        }
    }
    void cleanup() noexcept {
        HostCallFP host;
        std::lock_guard lock(mutex);
        stopping=true;
        backend.stop(); // Real DestroyVoice drains callbacks/PCM before retirement.
        try {harvestLocked(backend.poll());}
        catch(const OutputError& e) {failLocked(FailureStage::Stop,e.status(),e.what(),{},{},backendErrorOrigin(e.status()));}
        catch(...) {failLocked(FailureStage::Stop,E_UNEXPECTED,"Unexpected Dac output cleanup failure");}
        for(auto& slot:sources) if(slot.used &&
            (slot.completion.status==SourceStatus::Queued || slot.completion.status==SourceStatus::Processing)) {
            slot.completion.status=failure.stage==FailureStage::None?SourceStatus::Cancelled:SourceStatus::Failed;
            slot.completion.error=failure.error;
        }
        active=false;stopped=true;ready=true;readyCV.notify_all();signalLocked();
    }
    void run() noexcept {
        FPFrame restore;
#ifdef SIMPSONS_DAC_PROCESSOR_TESTS
        _mm_setcsr(0xE07F); // Unmasked exceptions, sticky flags, FTZ/DAZ, round-toward-zero.
#endif
        const HRESULT apartment=[] {
            HostCallFP host;
            return CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        }();
        {
            std::lock_guard lock(mutex);
            if(FAILED(apartment)) failLocked(FailureStage::WorkerStart,apartment,"Initialize Dac processing MTA");
            ready=true;readyCV.notify_all();
        }
        try {
            if(FAILED(apartment)) throw OutputError("Initialize Dac processing MTA",apartment);
            const HANDLE handles[]{stopEvent.handle,control.handle,static_cast<HANDLE>(backend.wakeHandle())};
            for(;;) {
                const DWORD wait=WaitForMultipleObjects(3,handles,FALSE,1000);
                if(wait==WAIT_OBJECT_0) break;
                if(wait!=WAIT_OBJECT_0+1 && wait!=WAIT_OBJECT_0+2 && wait!=WAIT_TIMEOUT)
                    throw OutputError("Wait for Dac processing demand",HRESULT_FROM_WIN32(GetLastError()));
                const bool backendHint=wait==WAIT_OBJECT_0+2 || backend.wait(0);
                auto snapshot=backend.poll();
                std::unique_lock lock(mutex);
                if(backendHint) {
                    incrementLocked(processingHints,"Dac processing hint sequence exhausted");signalLocked();
                }
                harvestLocked(snapshot);
                if(failure.stage!=FailureStage::None) requestStopLocked();
                if(stopping) break;
                if(!active) continue;
                if(backendHint && !firstQueuedLocked()) {
                    // Verified enclosing starvation policy; no invented PCM or
                    // completed source receipt. XAudio2 owns native underflow.
                    current=target;
                    incrementLocked(starvationPasses,"Dac starvation counter exhausted");
                    signalLocked();
                }
                while(!stopping) {
                    auto* source=firstQueuedLocked();auto* output=freeDownstreamLocked();
                    if(!source || !output) break;
                    // Only this worker owns backend receipts. Completion can
                    // free capacity, but no other producer may consume it.
                    snapshot=backend.poll();harvestLocked(snapshot);
                    if(failure.stage!=FailureStage::None) {requestStopLocked();break;}
                    if(snapshot.owned-snapshot.completed.size()>=downstreamCapacity) break;
                    if(!processLocked(lock,*source,*output)) break;
                }
            }
        }catch(const OutputError& e) {
            std::lock_guard lock(mutex);failLocked(FailureStage::Wait,e.status(),e.what(),{},{},backendErrorOrigin(e.status()));
        }catch(const std::exception& e) {
            std::lock_guard lock(mutex);failLocked(FailureStage::Wait,E_UNEXPECTED,e.what());
        }catch(...) {
            std::lock_guard lock(mutex);failLocked(FailureStage::Wait,E_UNEXPECTED,"Unknown Dac processing failure");
        }
        cleanup();
        if(SUCCEEDED(apartment)) {HostCallFP host;CoUninitialize();}
#ifdef SIMPSONS_DAC_PROCESSOR_TESTS
        {std::lock_guard lock(mutex);fpRestored=(_mm_getcsr()==0xE07F);}
#endif
    }
    void launch() {
        worker=std::thread([this]{run();});
        std::unique_lock lock(mutex);readyCV.wait(lock,[&]{return ready;});
        if(failure.stage!=FailureStage::None) throw OutputError(failure.operation.c_str(),failure.error);
    }
    void close() {
        std::lock_guard join(joinMutex);
        if(worker.joinable() && worker.get_id()==std::this_thread::get_id())
            throw OutputError("Dac processor cannot join itself",E_UNEXPECTED);
        {std::lock_guard lock(mutex);if(!stopped) requestStopLocked();}
        if(worker.joinable()) {
            worker.join();
            std::lock_guard lock(mutex);workerExited=true;signalLocked();
        }
    }
};

DacProcessor::DacProcessor(Output& output):state(std::make_unique<State>(output)) {state->launch();}
DacProcessor::~DacProcessor()=default;
uint64_t DacProcessor::generation() const noexcept {return state->generation;}
DacProcessor::Submission DacProcessor::submit(std::span<const float> samples) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.usableLocked();
    require(samples.size()==Output::blockSamples,"Dac source requires256 six-component frames");
    for(float sample:samples) require(normalized(sample),"Dac source requires finite normalized PCM");
    State::Source* free=nullptr;
    for(auto& slot:s.sources) if(!slot.used) {free=&slot;break;}
    if(!free) return {SubmitStatus::Backpressure,{}};
    if(s.sequence==std::numeric_limits<uint64_t>::max()) {
        s.failLocked(FailureStage::Submit,E_FAIL,"Dac source sequence exhausted");s.requestStopLocked();
        throw OutputError("Dac source sequence exhausted",E_FAIL);
    }
    std::copy(samples.begin(),samples.end(),free->raw.begin());
    free->completion={};free->completion.receipt={s.generation,s.sequence++};free->used=true;
    s.kickLocked();s.signalLocked();return {SubmitStatus::Accepted,free->completion.receipt};
}
void DacProcessor::setTargetGain(float target) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.usableLocked();
    require(gain(target),"Dac target gain must be finite [0,1]");s.target=target;s.kickLocked();
}
void DacProcessor::activate() {
    auto& s=*state;std::lock_guard lock(s.mutex);s.usableLocked();
    if(s.active) return; // Repeated activation never reseeds a running ramp.
    try {s.backend.start();}
    catch(const OutputError& e) {s.failLocked(FailureStage::BackendState,e.status(),e.what(),{},{},s.backendErrorOrigin(e.status()));s.requestStopLocked();throw;}
    s.current=s.target;s.active=true;s.kickLocked();s.signalLocked();
}
DacProcessor::SourceCompletion DacProcessor::query(Receipt receipt) const {
    auto& s=*state;std::lock_guard lock(s.mutex);return s.findLocked(receipt).completion;
}
void DacProcessor::retire(Receipt receipt) {
    auto& s=*state;std::lock_guard lock(s.mutex);auto& slot=s.findLocked(receipt);
    require(slot.completion.status!=SourceStatus::Queued && slot.completion.status!=SourceStatus::Processing,
            "Cannot retire unconsumed Dac source work");
    slot.raw.fill(0);slot.used=false;slot.completion={};s.kickLocked();
}
DacProcessor::Snapshot DacProcessor::poll() {
    auto& s=*state;std::lock_guard lock(s.mutex);
    if(!ResetEvent(s.wake.handle)) {s.failLocked(FailureStage::Wake,HRESULT_FROM_WIN32(GetLastError()),"Reset Dac observer wake");s.requestStopLocked();}
    Snapshot result;result.generation=s.generation;result.backendGeneration=s.backendGeneration;
    result.active=s.active;result.stopping=s.stopping;result.stopped=s.stopped;result.workerExited=s.workerExited;
    result.current=s.current;result.target=s.target;result.failure=s.failure;
    result.processingHints=s.processingHints;result.starvationPasses=s.starvationPasses;result.backendRetired=s.backendRetired;
    for(const auto& slot:s.sources) if(slot.used) {
        ++result.owned;result.queued+=slot.completion.status==SourceStatus::Queued;
        result.processing+=slot.completion.status==SourceStatus::Processing;result.sources.push_back(slot.completion);
    }
    for(const auto& slot:s.downstream) if(slot.used) result.downstream.push_back(slot.completion);
    std::sort(result.sources.begin(),result.sources.end(),[](const auto& a,const auto& b){return a.receipt.sequence<b.receipt.sequence;});
    std::sort(result.downstream.begin(),result.downstream.end(),[](const auto& a,const auto& b){return a.completion.receipt.sequence<b.completion.receipt.sequence;});
    bool released=false;
    for(auto& slot:s.downstream) if(slot.used && slot.completion.retired) {slot={};released=true;}
    if(released && !s.stopped) s.kickLocked();
    return result;
}
void* DacProcessor::wakeHandle() const noexcept {return state->wake.handle;}
bool DacProcessor::wait(uint32_t timeoutMs) const {
    require(timeoutMs<=60000,"Dac observer wait must be0..60000ms");
    const DWORD value=WaitForSingleObject(state->wake.handle,timeoutMs);
    if(value==WAIT_OBJECT_0) return true;if(value==WAIT_TIMEOUT) return false;
    throw OutputError("Wait for Dac observer notification",HRESULT_FROM_WIN32(GetLastError()));
}
void DacProcessor::stop() {state->close();}

#ifdef SIMPSONS_DAC_PROCESSOR_TESTS
void DacProcessorTestAccess::hold(DacProcessor& owner,Gate gate,bool hold) {
    auto& s=*owner.state;std::lock_guard lock(s.mutex);
    (gate==Gate::BeforeDsp?s.holdDsp:s.holdSubmit)=hold;s.readyCV.notify_all();s.kickLocked();
}
std::vector<float> DacProcessorTestAccess::raw(const DacProcessor& owner,DacProcessor::Receipt receipt) {
    auto& s=*owner.state;std::lock_guard lock(s.mutex);const auto& raw=s.findLocked(receipt).raw;return {raw.begin(),raw.end()};
}
std::vector<float> DacProcessorTestAccess::processed(const DacProcessor& owner,DacProcessor::Receipt receipt) {
    auto& s=*owner.state;std::lock_guard lock(s.mutex);const auto& slot=s.findLocked(receipt);
    require(slot.completion.processedFrames==256,"Dac test DSP not complete");return {slot.processed.begin(),slot.processed.end()};
}
std::vector<float> DacProcessorTestAccess::submitted(const DacProcessor& owner,DacProcessor::Receipt receipt) {
    auto& s=*owner.state;std::lock_guard lock(s.mutex);const auto& slot=s.findLocked(receipt);
    require(slot.completion.downstream.sequence!=0,"Dac test has no accepted downstream receipt");
    return {slot.submitted.begin(),slot.submitted.end()};
}
bool DacProcessorTestAccess::workerFPRestored(const DacProcessor& owner) {
    auto& s=*owner.state;std::lock_guard lock(s.mutex);require(s.workerExited,"Dac test requires joined processing worker");return s.fpRestored;
}
#endif
}
