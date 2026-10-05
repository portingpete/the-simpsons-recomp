#include "native_audio_output.h"
#include <windows.h>
#include <xaudio2.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <xmmintrin.h>

namespace Simpsons::Audio {
namespace {
struct FloatingPoint {
    uint32_t previous=_mm_getcsr();
    FloatingPoint(){_mm_setcsr((previous|0x1f80u)&~0xe040u);}
    ~FloatingPoint(){_mm_setcsr(previous);}
};
std::string message(const char* operation,int32_t status) {
    char text[256];std::snprintf(text,sizeof(text),"%s (HRESULT 0x%08X)",operation,uint32_t(status));return text;
}
void require(bool ok,const char* why) {if(!ok) throw OutputError(why,E_INVALIDARG);}
void checked(HRESULT status,const char* why) {if(FAILED(status)) throw OutputError(why,status);}
std::atomic<uint64_t> nextGeneration{1};
uint64_t newGeneration() {
    auto id=nextGeneration.load();
    while(id!=std::numeric_limits<uint64_t>::max())
        if(nextGeneration.compare_exchange_weak(id,id+1)) return id;
    throw OutputError("Native audio output generation exhausted",E_FAIL);
}
static_assert(std::atomic<uint32_t>::is_always_lock_free);
static_assert(std::atomic<int32_t>::is_always_lock_free);
static_assert(std::atomic<uint64_t>::is_always_lock_free);
static_assert(sizeof(float)==4 && std::endian::native==std::endian::little);
}
OutputError::OutputError(const char* operation,int32_t status):std::runtime_error(message(operation,status)),status_(status) {}

struct NativeAudioOutput::State final:IXAudio2VoiceCallback,IXAudio2EngineCallback {
    enum SlotStatus:uint32_t {Free,Pending,Consumed,Cancelled,Failed};
    struct Slot {
        std::array<float,blockSamples> pcm{};
        uint64_t sequence=0; // Public API mutex only. Callbacks never read it.
        std::atomic<uint32_t> status{Free};
    };
    const uint64_t generation=newGeneration();
    const Options options;
    const uint32_t masteringFlags;
    mutable std::mutex mutex;
    HANDLE wake{};
    IXAudio2* engine{};
    IXAudio2MasteringVoice* master{};
    IXAudio2SourceVoice* source{};
    bool registered=false,configured=false,running=false,stopped=false;
    Endpoint endpoint{};
    Routing route;
    std::unique_ptr<Slot[]> slots;
    uint64_t nextSequence=1;
    std::atomic<bool> closing{false};
    // One CAS publishes BOTH first-error HRESULT and provenance. Separate
    // atomics could report a losing callback's origin with the winning error.
    std::atomic<uint64_t> failure{0};
    explicit State(const Options& value):options(value),
        masteringFlags(value.deviceId.empty()?0u:XAUDIO2_NO_VIRTUAL_AUDIO_CLIENT) {
        require(options.capacity>=1 && options.capacity<=maxBuffers,"Output capacity must be 1..64");
        require(options.deviceId.find(L'\0')==std::wstring::npos,"Embedded null in output device ID");
        slots=std::make_unique<Slot[]>(options.capacity);
        wake=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!wake) throw OutputError("Create output wake event",HRESULT_FROM_WIN32(GetLastError()));
    }
    ~State() {close();if(wake) CloseHandle(wake);}
    int32_t error() const noexcept {return int32_t(uint32_t(failure.load(std::memory_order_acquire)));}
    void recordFailure(HRESULT hr,ErrorOrigin origin) noexcept {
        uint64_t expected=0;
        const uint64_t value=(uint64_t(origin)<<32)|uint32_t(FAILED(hr)?hr:E_UNEXPECTED);
        failure.compare_exchange_strong(expected,value,std::memory_order_release,std::memory_order_relaxed);
    }
    void signal() noexcept {
        if(wake && !SetEvent(wake)) {
            recordFailure(HRESULT_FROM_WIN32(GetLastError()),ErrorOrigin::WakeEvent);
        }
    }
    void fail(HRESULT hr,ErrorOrigin origin=ErrorOrigin::BackendCall) noexcept {
        recordFailure(hr,origin);signal();
    }
    void usable() const {
        checked(error(),"Native audio output failed");
        if(stopped) throw OutputError("Native audio output is stopped",HRESULT_FROM_WIN32(ERROR_INVALID_STATE));
    }
    void backend(HRESULT hr,const char* why) {
        if(FAILED(hr)){fail(hr);throw OutputError(why,hr);}
    }
    void open() {
        FloatingPoint fp;
        APTTYPE apartment;APTTYPEQUALIFIER qualifier;
        checked(CoGetApartmentType(&apartment,&qualifier),"Output creation requires caller COM initialization");
        require(apartment==APTTYPE_MTA,"Output creation requires caller MTA");
        checked(XAudio2Create(&engine,0,XAUDIO2_DEFAULT_PROCESSOR),"XAudio2Create");
        checked(engine->RegisterForCallbacks(this),"Register output error callback");registered=true;
        const auto masterStatus=engine->CreateMasteringVoice(&master,XAUDIO2_DEFAULT_CHANNELS,sampleRate,
            masteringFlags,options.deviceId.empty()?nullptr:options.deviceId.c_str(),nullptr,AudioCategory_GameEffects);
        if(FAILED(masterStatus))std::fprintf(stderr,
            "[NATIVE AUDIO MASTER FAILURE] channels=%u rate=%u flags=%08X device=%ls effects=null category=%u HRESULT=%08X\n",
            unsigned(XAUDIO2_DEFAULT_CHANNELS),sampleRate,masteringFlags,
            options.deviceId.empty()?L"<default>":options.deviceId.c_str(),unsigned(AudioCategory_GameEffects),unsigned(masterStatus));
        checked(masterStatus,"Create output mastering voice");
        checked(master->SetVolume(options.muted?0.0f:1.0f),"Set output master volume");
        float volume=-1;master->GetVolume(&volume);
        require(volume==(options.muted?0.0f:1.0f),"Master volume readback differs");
        XAUDIO2_VOICE_DETAILS details{};master->GetVoiceDetails(&details);DWORD mask=0;
        checked(master->GetChannelMask(&mask),"Get output endpoint channel mask");
        require(details.InputChannels>=1 && details.InputChannels<=18 && details.InputSampleRate==sampleRate &&
            uint32_t(std::popcount(mask))==details.InputChannels && !(mask&~0x3ffffu),"Unsupported output endpoint layout");
        endpoint={details.InputChannels,details.InputSampleRate,mask,options.muted};
        checked(error(),"Output device failed during creation");
        std::fprintf(stderr,"[NATIVE AUDIO ENDPOINT] flags=%08X mode=%s channels=%u mask=%08X rate=%u\n",
            masteringFlags,options.deviceId.empty()?"default-virtual":"explicit-pinned",endpoint.channels,endpoint.channelMask,endpoint.sampleRate);
    }
    // Caller owns the API mutex, or construction/destruction is exclusive.
    // Callbacks NEVER acquire that mutex, allocate, free, log or call client code.
    void close() noexcept {
        if(stopped) return;
        closing.store(true,std::memory_order_release);
        FloatingPoint fp;
        if(source){source->DestroyVoice();source=nullptr;}
        if(master){master->DestroyVoice();master=nullptr;}
        if(engine){
            if(registered){engine->UnregisterForCallbacks(this);registered=false;}
            engine->Release();engine=nullptr;
        }
        // The graph no longer accesses callback/context/PCM storage. No Flush:
        // its OnBufferEnd could otherwise mislabel discarded data as consumed.
        for(uint32_t i=0;i<options.capacity;++i) {
            uint32_t pending=Pending;
            slots[i].status.compare_exchange_strong(pending,FAILED(error())?Failed:Cancelled);
        }
        running=false;stopped=true;signal();
    }
    Slot& find(Receipt receipt) const {
        if(receipt.generation!=generation || !receipt.sequence) throw OutputError("Foreign output receipt",E_INVALIDARG);
        for(uint32_t i=0;i<options.capacity;++i)
            if(slots[i].sequence==receipt.sequence && slots[i].status.load()!=Free) return slots[i];
        throw OutputError("Retired/unknown output receipt",E_INVALIDARG);
    }
    Completion completion(const Slot& slot) const {
        const auto status=slot.status.load(std::memory_order_acquire);
        BufferStatus publicStatus;
        switch(status){case Pending:publicStatus=BufferStatus::Pending;break;case Consumed:publicStatus=BufferStatus::Consumed;break;
            case Cancelled:publicStatus=BufferStatus::Cancelled;break;case Failed:publicStatus=BufferStatus::Failed;break;
            default:throw OutputError("Invalid output receipt state",E_UNEXPECTED);}
        return {{generation,slot.sequence},publicStatus,status==Failed?error():S_OK};
    }
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) noexcept override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() noexcept override {}
    void STDMETHODCALLTYPE OnStreamEnd() noexcept override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) noexcept override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*) noexcept override {}
    void STDMETHODCALLTYPE OnBufferEnd(void* context) noexcept override {
        const uintptr_t address=reinterpret_cast<uintptr_t>(context),first=reinterpret_cast<uintptr_t>(slots.get());
        if(address<first || address-first>=sizeof(Slot)*options.capacity || (address-first)%sizeof(Slot)) {fail(E_UNEXPECTED,ErrorOrigin::CallbackContract);return;}
        auto& slot=*static_cast<Slot*>(context);
        const uint32_t terminal=FAILED(error())?Failed:(closing.load(std::memory_order_acquire)?Cancelled:Consumed);
        uint32_t expected=Pending;
        if(!slot.status.compare_exchange_strong(expected,terminal,std::memory_order_release,std::memory_order_relaxed)) fail(E_UNEXPECTED,ErrorOrigin::CallbackContract);
        signal();
    }
    void STDMETHODCALLTYPE OnVoiceError(void*,HRESULT hr) noexcept override {fail(hr,ErrorOrigin::VoiceCallback);}
    void STDMETHODCALLTYPE OnProcessingPassStart() noexcept override {}
    void STDMETHODCALLTYPE OnProcessingPassEnd() noexcept override {signal();}
    void STDMETHODCALLTYPE OnCriticalError(HRESULT hr) noexcept override {fail(hr,ErrorOrigin::EngineCallback);}
};

NativeAudioOutput::NativeAudioOutput(const Options& options):state(std::make_unique<State>(options)) {state->open();}
NativeAudioOutput::~NativeAudioOutput() {stop();}
NativeAudioOutput::Endpoint NativeAudioOutput::endpoint() const {return state->endpoint;}
uint64_t NativeAudioOutput::generation() const noexcept {return state->generation;}
void NativeAudioOutput::configure(const Routing& routing) {
    auto& s=*state;std::lock_guard lock(s.mutex);FloatingPoint fp;s.usable();
    require(!s.configured,"Output routing is immutable after configuration");
    require(std::popcount(routing.sourceChannelMask)==channels && !(routing.sourceChannelMask&~0x3ffffu),"Explicit six-channel source mask required");
    require(routing.destinationChannelMask==s.endpoint.channelMask && routing.matrix.size()==channels*s.endpoint.channels,
        "Routing must match actual endpoint mask/channel count");
    for(float value:routing.matrix) require(std::isfinite(value) && value>=-1 && value<=1,"Routing gains must be finite in [-1,1]");
    Routing owned=routing; // Allocate before native publication.
    WAVEFORMATEXTENSIBLE format{};
    format.Format={WAVE_FORMAT_EXTENSIBLE,channels,sampleRate,sampleRate*channels*4,channels*4,32,22};
    format.Samples.wValidBitsPerSample=32;format.dwChannelMask=routing.sourceChannelMask;
    format.SubFormat=KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    try {
        s.backend(s.engine->CreateSourceVoice(&s.source,&format.Format,0,1.0f,&s,nullptr,nullptr),"Create six-channel source voice");
        s.backend(s.source->SetVolume(s.options.muted?0.0f:1.0f),"Set source volume");
        float volume=-1;s.source->GetVolume(&volume);require(volume==(s.options.muted?0.0f:1.0f),"Source volume readback differs");
        s.backend(s.source->SetOutputMatrix(s.master,channels,s.endpoint.channels,owned.matrix.data()),"Set explicit output matrix");
        std::vector<float> actual(owned.matrix.size());s.source->GetOutputMatrix(s.master,channels,s.endpoint.channels,actual.data());
        require(actual==owned.matrix,"Native output matrix readback differs");
        s.route=std::move(owned);s.configured=true;s.usable();
    }catch(...){s.close();throw;}
}
NativeAudioOutput::Routing NativeAudioOutput::routing() const {
    auto& s=*state;std::lock_guard lock(s.mutex);require(s.configured,"Output routing has not been configured");return s.route;
}
void NativeAudioOutput::configureWindows51() {
    auto& s=*state;std::lock_guard lock(s.mutex);FloatingPoint fp;s.usable();
    require(!s.configured,"Output routing is immutable after configuration");
    static_assert(KSAUDIO_SPEAKER_5POINT1_SURROUND==0x60f);
    // Endpoint dimensions were bounded during open(), before any matrix access.
    require(s.endpoint.channels>=1 && s.endpoint.channels<=18 &&
        uint32_t(std::popcount(s.endpoint.channelMask))==s.endpoint.channels &&
        !(s.endpoint.channelMask&~0x3ffffu),"Unsupported Windows routing endpoint");
    Routing owned{KSAUDIO_SPEAKER_5POINT1_SURROUND,s.endpoint.channelMask,{}};
    WAVEFORMATEXTENSIBLE format{};
    format.Format={WAVE_FORMAT_EXTENSIBLE,channels,sampleRate,sampleRate*channels*4,channels*4,32,22};
    format.Samples.wValidBitsPerSample=32;format.dwChannelMask=owned.sourceChannelMask;
    format.SubFormat=KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    try {
        // Poison unwritten values: GetOutputMatrix is void, so HRESULT alone
        // cannot validate its readback. Preserve the returned coefficients as-is.
        owned.matrix.assign(channels*s.endpoint.channels,std::numeric_limits<float>::quiet_NaN());
        s.backend(s.engine->CreateSourceVoice(&s.source,&format.Format,0,1.0f,&s,nullptr,nullptr),
            "Create Windows 5.1 source voice");
        s.backend(s.source->SetVolume(s.options.muted?0.0f:1.0f),"Set source volume");
        float volume=-1;s.source->GetVolume(&volume);
        require(volume==(s.options.muted?0.0f:1.0f),"Source volume readback differs");
        XAUDIO2_VOICE_DETAILS details{};s.source->GetVoiceDetails(&details);
        require(details.InputChannels==channels && details.InputSampleRate==sampleRate,
            "Windows source voice format readback differs");
        // A null send list targets our only mastering voice. No SetOutputMatrix:
        // XAudio2 derives this route from the source mask and actual endpoint.
        s.source->GetOutputMatrix(s.master,channels,s.endpoint.channels,owned.matrix.data());
        for(float value:owned.matrix)
            require(std::isfinite(value) && value>=-XAUDIO2_MAX_VOLUME_LEVEL && value<=XAUDIO2_MAX_VOLUME_LEVEL,
                "Windows default output matrix is invalid");
        s.usable();s.route=std::move(owned);s.configured=true;
    }catch(...){s.close();throw;}
}
void NativeAudioOutput::start() {
    auto& s=*state;std::lock_guard lock(s.mutex);FloatingPoint fp;s.usable();require(s.configured,"Output routing required before start");
    if(!s.running){s.backend(s.source->Start(),"Start native output");s.running=true;}
}
NativeAudioOutput::Submission NativeAudioOutput::submit(std::span<const float> samples) {
    auto& s=*state;std::lock_guard lock(s.mutex);FloatingPoint fp;s.usable();require(s.configured,"Output routing required before submission");
    require(samples.size()==blockSamples,"Output submission requires exactly 256 six-channel frames");
    for(float value:samples) require(std::isfinite(value) && value>=-1 && value<=1,"Output samples must be finite normalized float32");
    State::Slot* slot=nullptr;
    for(uint32_t i=0;i<s.options.capacity;++i) if(s.slots[i].status.load()==State::Free){slot=&s.slots[i];break;}
    if(!slot) return {SubmitStatus::Backpressure,{}};
    require(s.nextSequence!=std::numeric_limits<uint64_t>::max(),"Output receipt sequence exhausted");
    std::copy(samples.begin(),samples.end(),slot->pcm.begin());slot->sequence=s.nextSequence++;
    slot->status.store(State::Pending,std::memory_order_release);
    XAUDIO2_BUFFER buffer{};buffer.AudioBytes=sizeof(slot->pcm);buffer.pAudioData=reinterpret_cast<const BYTE*>(slot->pcm.data());buffer.pContext=slot;
    const HRESULT hr=s.source->SubmitSourceBuffer(&buffer);
    if(FAILED(hr)) {
        s.fail(hr);s.close();slot->status.store(State::Free);slot->sequence=0;
        throw OutputError("Submit native output buffer",hr);
    }
    return {SubmitStatus::Accepted,{s.generation,slot->sequence}};
}
NativeAudioOutput::Completion NativeAudioOutput::query(Receipt receipt) const {
    auto& s=*state;std::lock_guard lock(s.mutex);return s.completion(s.find(receipt));
}
void NativeAudioOutput::retire(Receipt receipt) {
    auto& s=*state;std::lock_guard lock(s.mutex);auto& slot=s.find(receipt);
    require(slot.status.load(std::memory_order_acquire)!=State::Pending,"Cannot retire a pending output buffer");
    slot.status.store(State::Free);slot.sequence=0;
}
NativeAudioOutput::Snapshot NativeAudioOutput::poll() {
    auto& s=*state;std::lock_guard lock(s.mutex);
    if(!ResetEvent(s.wake)){s.fail(HRESULT_FROM_WIN32(GetLastError()),ErrorOrigin::WakeEvent);throw OutputError("Reset output wake event",s.error());}
    Snapshot result{s.generation,s.configured,s.running,s.stopped,S_OK,s.options.capacity,0,0,{}};
    for(uint32_t i=0;i<s.options.capacity;++i){
        const auto status=s.slots[i].status.load(std::memory_order_acquire);
        if(status==State::Free) continue;
        ++result.owned;if(status==State::Pending) ++result.pending;else result.completed.push_back(s.completion(s.slots[i]));
    }
    // A Failed slot is release-published only AFTER the first-error CAS. Read
    // the packed error last so observing that slot cannot return origin=None.
    const uint64_t failure=s.failure.load(std::memory_order_acquire);
    result.error=int32_t(uint32_t(failure));result.errorOrigin=ErrorOrigin(uint32_t(failure>>32));
    std::sort(result.completed.begin(),result.completed.end(),[](const auto& a,const auto& b){return a.receipt.sequence<b.receipt.sequence;});
    return result;
}
bool NativeAudioOutput::wait(uint32_t timeoutMs) const {
    require(timeoutMs<=60000,"Output wait must be bounded to 60000 ms");
    const DWORD result=WaitForSingleObject(state->wake,timeoutMs);
    if(result==WAIT_OBJECT_0) return true;if(result==WAIT_TIMEOUT) return false;
    throw OutputError("Wait for output notification",HRESULT_FROM_WIN32(GetLastError()));
}
void* NativeAudioOutput::wakeHandle() const noexcept {return state->wake;}
void NativeAudioOutput::stop() noexcept {auto& s=*state;std::lock_guard lock(s.mutex);s.close();}

#ifdef SIMPSONS_AUDIO_OUTPUT_TESTS
std::vector<float> NativeAudioOutputTestAccess::copied(const NativeAudioOutput& output,NativeAudioOutput::Receipt receipt) {
    auto& s=*output.state;std::lock_guard lock(s.mutex);const auto& slot=s.find(receipt);return {slot.pcm.begin(),slot.pcm.end()};
}
void NativeAudioOutputTestAccess::voiceError(NativeAudioOutput& output,int32_t hr) {output.state->OnVoiceError(nullptr,hr);}
void NativeAudioOutputTestAccess::criticalError(NativeAudioOutput& output,int32_t hr) {output.state->OnCriticalError(hr);}
uint32_t NativeAudioOutputTestAccess::masteringFlags(const NativeAudioOutput& output) {return output.state->masteringFlags;}
#endif
}
