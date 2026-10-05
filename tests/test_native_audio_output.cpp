#include "audio/native_audio_output.h"
#include <windows.h>
#include <xaudio2.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
#include <xmmintrin.h>

using Output=Simpsons::Audio::NativeAudioOutput;
using Access=Simpsons::Audio::NativeAudioOutputTestAccess;
using Error=Simpsons::Audio::OutputError;
using Clock=std::chrono::steady_clock;
namespace {
unsigned checks=0,realConsumed=0;
void check(bool ok,const char* why) {++checks;if(!ok) throw std::runtime_error(why);}
template<class F> void rejects(F&& operation,const char* why) {
    try {operation();}catch(const Error& e){check(FAILED(e.status()),"Error must contain a failure HRESULT");return;}
    check(false,why);
}
struct Mta {
    Mta(){const auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(hr)) throw Error("Fixture MTA",hr);}
    ~Mta(){CoUninitialize();}
};
struct Event {
    HANDLE value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Event(){check(value!=nullptr,"Fixture event creation");}
    ~Event(){CloseHandle(value);}
};
std::array<float,Output::blockSamples> samples(unsigned seed=0) {
    std::array<float,Output::blockSamples> result;
    for(unsigned frame=0;frame<256;++frame) for(unsigned channel=0;channel<6;++channel)
        result[6*frame+channel]=float(int((frame*13+channel*97+seed)%1025)-512)/512.0f;
    return result;
}
Output::Routing route(const Output& output) {
    const auto endpoint=output.endpoint();
    // Caller-selected TEST channel labels. This is not a mapping of game planes.
    Output::Routing result{KSAUDIO_SPEAKER_5POINT1_SURROUND,endpoint.channelMask,{}};
    result.matrix.resize(6*endpoint.channels);
    for(unsigned destination=0;destination<endpoint.channels;++destination)
        for(unsigned source=0;source<6;++source)
            result.matrix[6*destination+source]=float((destination*3+source*2)%7+1)/64.0f;
    return result;
}
void configure(Output& output) {output.configure(route(output));}
Output::Receipt submit(Output& output,std::span<const float> data) {
    auto result=output.submit(data);
    check(result.status==Output::SubmitStatus::Accepted,"Expected accepted real source submission");
    check(result.receipt.generation==output.generation() && result.receipt.sequence!=0,"Assigned receipt");
    return result.receipt;
}
void complete(Output& output,std::span<const Output::Receipt> receipts) {
    const auto deadline=Clock::now()+std::chrono::seconds(5);
    for(;;) {
        auto snapshot=output.poll();
        check(snapshot.error==0 && !snapshot.stopped,"Real output failure while awaiting completion");
        bool all=true;
        for(auto receipt:receipts) {
            auto item=output.query(receipt);
            if(item.status==Output::BufferStatus::Pending) all=false;
            else check(item.status==Output::BufferStatus::Consumed && !item.error,"Natural completion required");
        }
        if(all){realConsumed+=unsigned(receipts.size());return;}
        check(Clock::now()<deadline,"Real XAudio2 completion deadline");
        output.wait(100);
    }
}
void expectIdleWake(Output& output) {
    for(unsigned i=0;i<3;++i) {
        const auto snapshot=output.poll();
        check(!snapshot.error && snapshot.owned==0 && snapshot.pending==0,"Idle queue must remain empty");
        check(output.wait(1000),"Real processing pass must wake without queued PCM");
    }
}
void preflight() {
    rejects([]{Output output({.capacity=0,.muted=true,.deviceId={}});},"Zero capacity accepted");
    rejects([]{Output output({.capacity=65,.muted=true,.deviceId={}});},"Unbounded capacity accepted");
    rejects([]{Output output({.deviceId=std::wstring(L"bad\0id",6)});},"Embedded-null device accepted");
    // A real failing system API, not a simulated HRESULT or silent fallback.
    try {
        Output output({.deviceId=L"SimpsonsNative-test-nonexistent-endpoint-7D475C3D"});
        check(false,"Missing endpoint silently selected default");
    }catch(const Error& error) {
        check(FAILED(error.status()) && std::string(error.what()).find("Create output mastering voice")!=std::string::npos,
              "Expected real CreateMasteringVoice endpoint rejection");
        std::printf("Actual system rejection: %s\n",error.what());
    }
    std::exception_ptr workerError;
    std::thread noApartment([&]{try {
        // Another initialized MTA can make this thread an implicit MTA. Force STA
        // instead so the owner reliably rejects the unsupported apartment.
        const HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        if(FAILED(hr)) throw Error("Fixture STA",hr);
        try {rejects([]{Output output({});},"STA output creation accepted");}catch(...){CoUninitialize();throw;}
        CoUninitialize();
    }catch(...){workerError=std::current_exception();}});
    noApartment.join();if(workerError) std::rethrow_exception(workerError);
    Output output({});
    const auto endpoint=output.endpoint();
    std::printf("Actual system endpoint: inputRate=%u channels=%u mask=%08X muted=%u\n",endpoint.sampleRate,endpoint.channels,endpoint.channelMask,endpoint.muted);
    check(endpoint.muted && endpoint.sampleRate==48000,"Muted 48 kHz graph required");
    rejects([&]{output.start();},"Unconfigured start");
    rejects([&]{output.submit(samples());},"Unconfigured submission");
    rejects([&]{output.routing();},"Unconfigured routing query");
    auto correct=route(output),bad=correct;
    bad.sourceChannelMask=0;rejects([&]{output.configure(bad);},"Implicit source speaker mapping");
    bad=correct;bad.sourceChannelMask|=0x80000000u;rejects([&]{output.configure(bad);},"Unknown speaker bit");
    bad=correct;bad.destinationChannelMask^=1;rejects([&]{output.configure(bad);},"Wrong endpoint mapping");
    bad=correct;bad.matrix.pop_back();rejects([&]{output.configure(bad);},"Wrong matrix extent");
    bad=correct;bad.matrix[0]=std::numeric_limits<float>::quiet_NaN();rejects([&]{output.configure(bad);},"NaN matrix");
    bad=correct;bad.matrix[0]=1.01f;rejects([&]{output.configure(bad);},"Out of contract matrix");
    check(!output.poll().configured,"Preflight failure published source");
    output.configure(correct);
    check(output.routing().matrix==correct.matrix,"Exact asymmetric routing readback");
    rejects([&]{output.configure(correct);},"Mutable routing accepted");
    rejects([&]{output.configureWindows51();},"Windows policy replaced explicit routing");
    auto data=samples();
    rejects([&]{output.submit(std::span(data).first(data.size()-1));},"Partial block accepted");
    data[3]=std::numeric_limits<float>::infinity();rejects([&]{output.submit(data);},"Infinite PCM accepted");
    data[3]=std::numeric_limits<float>::quiet_NaN();rejects([&]{output.submit(data);},"NaN PCM accepted");
    data[3]=-1.01f;rejects([&]{output.submit(data);},"Out of domain PCM accepted");
    check(output.poll().owned==0,"Malformed PCM changed ownership");
    rejects([&]{output.wait(60001);},"Unbounded wait accepted");
    expectIdleWake(output); // Source has never started and has no buffers.
    output.start();expectIdleWake(output); // Started but still no buffers.
    Event cancel;HANDLE handles[]{cancel.value,static_cast<HANDLE>(output.wakeHandle())};
    SetEvent(cancel.value);
    check(WaitForMultipleObjects(2,handles,FALSE,1000)==WAIT_OBJECT_0,"Caller stop event must interrupt idle worker wait");
    std::puts("PASS preflight / explicit routing / zero-PCM processing wakes / caller cancellation");
}
Output::Routing systemDefaultRoute(const Output::Endpoint& expected) {
    // Independent real graph: no backend configure helper and no SetOutputMatrix.
    // No PCM is submitted. Master is explicitly muted; source remains paused.
    struct Graph {
        IXAudio2* engine{};IXAudio2MasteringVoice* master{};IXAudio2SourceVoice* source{};
        ~Graph(){if(source) source->DestroyVoice();if(master) master->DestroyVoice();if(engine) engine->Release();}
    } graph;
    auto checked=[](HRESULT hr,const char* why){if(FAILED(hr)) throw Error(why,hr);};
    checked(XAudio2Create(&graph.engine,0,XAUDIO2_DEFAULT_PROCESSOR),"Oracle XAudio2Create");
    checked(graph.engine->CreateMasteringVoice(&graph.master,XAUDIO2_DEFAULT_CHANNELS,48000,
        0,nullptr,nullptr,AudioCategory_GameEffects),"Oracle master");
    checked(graph.master->SetVolume(0),"Oracle mute");
    float volume=-1;graph.master->GetVolume(&volume);check(volume==0,"Oracle master must be muted");
    XAUDIO2_VOICE_DETAILS endpoint{};graph.master->GetVoiceDetails(&endpoint);DWORD mask=0;
    checked(graph.master->GetChannelMask(&mask),"Oracle endpoint mask");
    check(endpoint.InputChannels==expected.channels && endpoint.InputSampleRate==expected.sampleRate && mask==expected.channelMask,
        "System default endpoint changed during Windows route fixture");
    WAVEFORMATEXTENSIBLE format{};
    format.Format={WAVE_FORMAT_EXTENSIBLE,6,48000,1152000,24,32,22};
    format.Samples.wValidBitsPerSample=32;format.dwChannelMask=0x60f;format.SubFormat=KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    checked(graph.engine->CreateSourceVoice(&graph.source,&format.Format,0,1.0f,nullptr,nullptr,nullptr),"Oracle source");
    std::vector<float> actual(6*expected.channels+2,std::numeric_limits<float>::quiet_NaN());
    actual.front()=123;actual.back()=-321;
    graph.source->GetOutputMatrix(graph.master,6,expected.channels,actual.data()+1);
    check(actual.front()==123 && actual.back()==-321,"GetOutputMatrix exceeded declared dimensions");
    return {0x60f,mask,{actual.begin()+1,actual.end()-1}};
}
void windowsRouting() {
    Output output({.capacity=4,.muted=true,.deviceId={}});
    check(Access::masteringFlags(output)==0,"Default output disabled the Windows virtual audio client");
    const auto endpoint=output.endpoint();
    const auto expected=systemDefaultRoute(endpoint);
    check(endpoint.muted,"Windows adaptation fixture must be muted");
    output.configureWindows51();
    const auto actual=output.routing();
    check(actual.sourceChannelMask==0x60f && actual.destinationChannelMask==endpoint.channelMask &&
          actual.matrix.size()==6*endpoint.channels,"Windows route dimensions/masks");
    check(actual.matrix.size()==expected.matrix.size() &&
        std::memcmp(actual.matrix.data(),expected.matrix.data(),actual.matrix.size()*sizeof(float))==0,
        "Windows route differs from independent native default matrix");
    for(float coefficient:actual.matrix)
        check(std::isfinite(coefficient) && coefficient>=-XAUDIO2_MAX_VOLUME_LEVEL && coefficient<=XAUDIO2_MAX_VOLUME_LEVEL,
            "Invalid system default matrix coefficient");
    std::printf("Actual Windows 5.1 default matrix: source=0000060F destination=%08X dimensions=6x%u bits=",
        endpoint.channelMask,endpoint.channels);
    for(float coefficient:actual.matrix) std::printf("%08X ",std::bit_cast<uint32_t>(coefficient));
    std::puts("");
    auto callerCopy=output.routing();
    callerCopy.sourceChannelMask=0;callerCopy.destinationChannelMask=0;callerCopy.matrix.assign(1,-1);
    const auto retained=output.routing();
    check(retained.sourceChannelMask==actual.sourceChannelMask && retained.destinationChannelMask==actual.destinationChannelMask &&
        retained.matrix.size()==actual.matrix.size() &&
        std::memcmp(retained.matrix.data(),actual.matrix.data(),actual.matrix.size()*sizeof(float))==0,"Routing did not retain immutable owned readback");
    auto snapshot=output.poll();
    check(snapshot.configured && !snapshot.running && !snapshot.stopped && !snapshot.error && snapshot.capacity==4 && snapshot.owned==0,
        "Windows configuration must leave a paused empty four-slot source");
    rejects([&]{output.configureWindows51();},"Duplicate Windows configuration accepted");
    rejects([&]{output.configure(route(output));},"Explicit route replaced Windows configuration");
    std::array<Output::Receipt,4> receipts{};
    for(unsigned i=0;i<receipts.size();++i) {
        const auto data=samples(500+i);
        {std::vector<float> transient(data.begin(),data.end());receipts[i]=submit(output,transient);transient.assign(transient.size(),-0.125f);}
        const auto copy=Access::copied(output,receipts[i]);
        check(std::memcmp(copy.data(),data.data(),sizeof(data))==0,"Windows routed voice changed component order/borrowed caller PCM");
        check(output.query(receipts[i]).status==Output::BufferStatus::Pending,"Windows source started implicitly");
    }
    check(output.submit(samples()).status==Output::SubmitStatus::Backpressure,"Four-slot Windows source backpressure");
    output.start();complete(output,receipts);
    check(output.submit(samples()).status==Output::SubmitStatus::Backpressure,"Windows receipts require retirement");
    for(auto receipt:receipts) output.retire(receipt);
    output.stop();
    check(output.poll().owned==0 && output.poll().stopped,"Windows source did not drain/stop");
    rejects([&]{output.configureWindows51();},"Windows configuration after stop");
    check(output.routing().matrix==actual.matrix,"Retained Windows routing lost at shutdown");
    Output reopened({});reopened.configureWindows51();
    check(reopened.generation()!=output.generation(),"Windows reopen reused graph generation");
    rejects([&]{reopened.query(receipts[0]);},"Windows reopen accepted old receipt");
    auto pending=submit(reopened,samples());reopened.stop();
    check(reopened.query(pending).status==Output::BufferStatus::Cancelled,"Paused Windows pending buffer falsely consumed");
    reopened.retire(pending);
    Output stopped({});stopped.stop();
    rejects([&]{stopped.configureWindows51();},"Stopped unconfigured graph acquired Windows source");
    check(!stopped.poll().configured,"Stopped rejection published Windows route");
    Output failed({});Access::criticalError(failed,XAUDIO2_E_DEVICE_INVALIDATED);
    rejects([&]{failed.configureWindows51();},"Failed unconfigured graph acquired Windows source");
    const auto failedState=failed.poll();
    check(!failedState.configured && failedState.error==XAUDIO2_E_DEVICE_INVALIDATED &&
          failedState.errorOrigin==Output::ErrorOrigin::EngineCallback,"Windows failure rejection lost first error/origin");
    failed.stop();
    std::puts("PASS Windows default matrix / independent real readback / once-only policy / four owned completions / invalid states");
}
void explicitDevicePolicy() {
    // Obtain a real current endpoint through the official enumeration API. An
    // explicit device remains pinned; this test does not change OS defaults or
    // remove any hardware, and does not claim a physical migration event.
    struct Devices {
        IMMDeviceEnumerator* enumerator{};IMMDevice* endpoint{};LPWSTR identifier{};
        ~Devices(){if(identifier) CoTaskMemFree(identifier);if(endpoint) endpoint->Release();if(enumerator) enumerator->Release();}
    } devices;
    auto checked=[](HRESULT hr,const char* why){if(FAILED(hr)) throw Error(why,hr);};
    checked(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),
                             reinterpret_cast<void**>(&devices.enumerator)),"Enumerate actual pinned endpoint");
    checked(devices.enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&devices.endpoint),"Get actual pinned endpoint");
    checked(devices.endpoint->GetId(&devices.identifier),"Read actual pinned endpoint ID");
    check(devices.identifier && *devices.identifier,"Actual pinned endpoint ID missing");
    Output pinned({.capacity=2,.muted=true,.deviceId=devices.identifier});
    check(Access::masteringFlags(pinned)==XAUDIO2_NO_VIRTUAL_AUDIO_CLIENT,"Explicit output lost its pinned-device policy");
    pinned.configureWindows51();
    const auto data=samples(71);const std::array receipts{submit(pinned,data)};
    check(Access::copied(pinned,receipts[0])==std::vector<float>(data.begin(),data.end()),"Pinned output lost owned PCM");
    pinned.start();complete(pinned,receipts);pinned.retire(receipts[0]);pinned.stop();
    check(pinned.poll().stopped && pinned.poll().owned==0,"Pinned output did not retire its real buffer");
    std::puts("PASS actual default virtual-client flag and independent native oracle; actual explicit endpoint pinned with real owned PCM/completion; no physical migration claim");
}
void copiedAndConsumed() {
    Output output({});configure(output);
    auto expected=samples(17);
    void* allocation=VirtualAlloc(nullptr,sizeof(expected),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    check(allocation!=nullptr,"Caller PCM allocation");
    std::memcpy(allocation,expected.data(),sizeof(expected));
    auto first=submit(output,{static_cast<const float*>(allocation),expected.size()});
    std::memset(allocation,0xcc,sizeof(expected));
    check(VirtualFree(allocation,0,MEM_RELEASE)!=0,"Release caller PCM BEFORE voice starts");
    const auto copy=Access::copied(output,first);
    check(std::memcmp(copy.data(),expected.data(),sizeof(expected))==0,"Submitted pointer must refer to owned exact samples");
    std::vector<float> transient(expected.begin(),expected.end());
    auto second=submit(output,transient);transient.assign(transient.size(),-0.25f);transient.clear();transient.shrink_to_fit();
    auto denied=output.submit(expected);
    check(denied.status==Output::SubmitStatus::Backpressure && denied.receipt==Output::Receipt{},"Two-buffer admission limit");
    check(output.query(first).status==Output::BufferStatus::Pending && output.query(second).status==Output::BufferStatus::Pending,"Paused source consumed PCM");
    rejects([&]{output.retire(first);},"Pending storage released");
    output.start();output.start();
    const std::array receipts{first,second};complete(output,receipts);
    check(output.submit(expected).status==Output::SubmitStatus::Backpressure,"Completed receipts must count until retirement");
    check(output.poll().completed.size()==2,"Both callbacks observed");
    for(auto receipt:receipts){output.retire(receipt);rejects([&]{output.query(receipt);},"Retired receipt remains queryable");}
    uint64_t sequence=second.sequence;
    for(unsigned batch=0;batch<24;++batch) {
        const std::array next{submit(output,samples(batch)),submit(output,samples(batch+100))};
        check(next[0].sequence>sequence && next[1].sequence>next[0].sequence,"Receipt reused on slot recycle");
        sequence=next[1].sequence;complete(output,next);
        for(auto receipt:next) output.retire(receipt);
    }
    rejects([&]{output.query(first);},"Stale slot identity alias");
    check(output.poll().owned==0,"Completed storage not retired");
    expectIdleWake(output);
    std::puts("PASS owned exact PCM after caller VirtualFree / real completions / bounded retirement / slot generations");
}
void stopAndReopen() {
    uint64_t previousGeneration=0;
    Output::Receipt old{};
    for(unsigned lifetime=0;lifetime<8;++lifetime) {
        Output output({});configure(output);
        check(output.generation()>previousGeneration,"Graph generation reused");previousGeneration=output.generation();
        if(old.sequence) rejects([&]{output.query(old);},"Foreign generation accepted");
        const std::array pending{submit(output,samples()),submit(output,samples(1))};
        old=pending[0];
        std::thread first([&]{Mta mta;output.stop();});
        std::thread second([&]{Mta mta;output.stop();});
        first.join();second.join();
        auto snapshot=output.poll();
        check(snapshot.stopped && !snapshot.running && snapshot.pending==0 && snapshot.completed.size()==2,"Stop did not drain pending storage");
        for(auto receipt:pending){
            check(output.query(receipt).status==Output::BufferStatus::Cancelled,"Unplayed stopped PCM reported consumed");
            output.retire(receipt);
        }
        if(lifetime==0) {
            output.poll(); // Clear the terminal wake after observing stopped.
            check(!output.wait(80),"Processing callbacks continued after stop returned");
        }
        rejects([&]{output.start();},"Restart stopped graph");
        rejects([&]{output.submit(samples());},"Submit after graph destruction");
        output.stop();
    }
    // The destructor itself also owns the quiescence barrier.
    for(unsigned i=0;i<4;++i){Output output({});configure(output);submit(output,samples(i));output.start();}
    std::puts("PASS paused pending stop / concurrent idempotent shutdown / graph reopen / destruction while running");
}
void callbackErrors() {
    for(bool engineFailure:{false,true}) {
        Output output({});configure(output);
        auto receipt=submit(output,samples());
        const HRESULT expected=engineFailure?XAUDIO2_E_DEVICE_INVALIDATED:E_FAIL;
        if(engineFailure) Access::criticalError(output,expected);else Access::voiceError(output,expected);
        Access::criticalError(output,E_ABORT); // Preserve the first failure.
        const auto snapshot=output.poll();
        check(snapshot.error==expected && snapshot.pending==1 && snapshot.errorOrigin==
              (engineFailure?Output::ErrorOrigin::EngineCallback:Output::ErrorOrigin::VoiceCallback),"Callback first error/origin publication");
        check(output.query(receipt).status==Output::BufferStatus::Pending,"Error callback prematurely released PCM");
        rejects([&]{output.retire(receipt);},"Error made still-live PCM retireable");
        rejects([&]{output.start();},"Start after callback error");
        rejects([&]{output.submit(samples());},"Submission after callback error");
        output.stop();
        const auto result=output.query(receipt);
        check(result.status==Output::BufferStatus::Failed && result.error==expected,"Failed receipt lost error after actual DestroyVoice");
        output.retire(receipt);
    }
    // The observed driver HRESULT has no public SDK name. Keep it unchanged,
    // correctly attributed and fatal, rather than treating unknown codes as
    // recoverable success or conflating them with validation failures.
    Output observed({});configure(observed);const auto receipt=submit(observed,samples(93));
    const auto raw=static_cast<int32_t>(0x88880001u);
    Access::criticalError(observed,raw);Access::voiceError(observed,E_FAIL);
    const auto failure=observed.poll();
    check(failure.error==raw && failure.errorOrigin==Output::ErrorOrigin::EngineCallback && failure.pending==1,
          "Unknown critical HRESULT lost first atomic origin or live PCM ownership");
    rejects([&]{observed.start();},"Unknown critical error was suppressed");
    observed.stop();check(observed.query(receipt).status==Output::BufferStatus::Failed && observed.query(receipt).error==raw,
                          "Unknown critical error was relabeled as consumption after real drainage");
    observed.retire(receipt);
    // Competing callbacks publish one complete atomic pair. A losing callback
    // cannot overwrite the origin belonging to the winning HRESULT.
    for(unsigned i=0;i<8;++i) {
        Output race({});configure(race);std::atomic<unsigned> ready{0};std::atomic<bool> go{false};
        auto await=[&]{++ready;while(!go.load(std::memory_order_acquire)) Sleep(0);};
        std::thread voice([&]{await();Access::voiceError(race,E_FAIL);});
        std::thread engine([&]{await();Access::criticalError(race,raw);});
        while(ready.load()!=2) Sleep(0);go.store(true,std::memory_order_release);voice.join();engine.join();
        const auto first=race.poll();
        check((first.error==E_FAIL && first.errorOrigin==Output::ErrorOrigin::VoiceCallback) ||
              (first.error==raw && first.errorOrigin==Output::ErrorOrigin::EngineCallback),"Concurrent error/origin publication tore");
        race.stop();const auto stopped=race.poll();
        check(stopped.error==first.error && stopped.errorOrigin==first.errorOrigin,"Drain overwrote the first atomic error/origin");
    }
    std::puts("PASS injected voice/critical CALLBACK errors (not a claim of real device removal) / real DestroyVoice drainage");
}
void racingWorker() {
    auto output=std::make_shared<Output>(Output::Options{});configure(*output);output->start();
    Event cancel;
    std::atomic<unsigned> accepted{0},retired{0};
    std::exception_ptr producerError,workerError;
    std::thread producer([&,owned=output]{try {
        Mta mta;const auto data=samples(37);
        for(;;) {
            try {
                auto result=owned->submit(data);
                if(result.status==Output::SubmitStatus::Accepted) ++accepted;
                else if(WaitForSingleObject(cancel.value,1)==WAIT_OBJECT_0) break;
            }catch(const Error&){if(owned->poll().stopped) break;throw;}
        }
    }catch(...){producerError=std::current_exception();}});
    std::thread worker([&,owned=output]{try {
        Mta mta;HANDLE handles[]{cancel.value,static_cast<HANDLE>(owned->wakeHandle())};
        for(;;) {
            auto snapshot=owned->poll();
            if(snapshot.error) throw Error("Concurrent output callback",snapshot.error);
            for(auto completion:snapshot.completed) {
                if(completion.status!=Output::BufferStatus::Consumed && completion.status!=Output::BufferStatus::Cancelled)
                    throw std::runtime_error("Concurrent terminal status");
                owned->retire(completion.receipt);++retired;
            }
            if(snapshot.stopped) break;
            const auto result=WaitForMultipleObjects(2,handles,FALSE,100);
            if(result==WAIT_OBJECT_0) break;
            if(result!=WAIT_OBJECT_0+1 && result!=WAIT_TIMEOUT) throw std::runtime_error("Concurrent worker wait");
        }
    }catch(...){workerError=std::current_exception();}});
    const auto deadline=Clock::now()+std::chrono::seconds(5);
    while(retired.load()<24 && Clock::now()<deadline) Sleep(1);
    // Stress backend stop concurrently with submit, poll, retire and callbacks.
    output->stop();SetEvent(cancel.value);producer.join();worker.join();
    if(producerError) std::rethrow_exception(producerError);if(workerError) std::rethrow_exception(workerError);
    for(auto completion:output->poll().completed){output->retire(completion.receipt);++retired;}
    check(accepted.load()>=24,"Concurrent worker did not complete real buffers");
    check(accepted.load()==retired.load() && output->poll().owned==0,"Accepted PCM lost across stop race");
    std::printf("PASS real producer/worker/stop concurrency: accepted=%u retired=%u\n",accepted.load(),retired.load());
}
void fpRestoration() {
    const uint32_t saved=_mm_getcsr();
    struct Restore {uint32_t value;~Restore(){_mm_setcsr(value);}} restore{saved};
    const uint32_t unusual=(saved|0xbfc0u)&~0x4000u; // Masked, toward -infinity, FTZ and DAZ.
    _mm_setcsr(unusual);
    Output output({});check(_mm_getcsr()==unusual,"Creation leaked host FP state");
    auto routing=route(output);
    const uint32_t caller=_mm_getcsr();
    output.configure(routing);check(_mm_getcsr()==caller,"Configuration leaked host FP state");
    auto data=samples();const uint32_t before=_mm_getcsr();
    submit(output,data);check(_mm_getcsr()==before,"Submission leaked host FP state");
    output.stop();check(_mm_getcsr()==before,"Teardown leaked host FP state");
    Output windows({});const uint32_t beforeWindows=_mm_getcsr();
    windows.configureWindows51();check(_mm_getcsr()==beforeWindows,"Windows default routing leaked host FP state");
    windows.stop();check(_mm_getcsr()==beforeWindows,"Windows routing teardown leaked host FP state");
    std::puts("PASS native operations restore caller MXCSR");
}
}
int main() {
    try {
        Mta apartment;
        preflight();windowsRouting();explicitDevicePolicy();copiedAndConsumed();stopAndReopen();callbackErrors();racingWorker();fpRestoration();
        std::printf("PASS NativeAudioOutput: %u checks; %u natural completions in deterministic batches, plus concurrent worker completions; ALL MUTED\n",checks,realConsumed);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL after %u checks: %s\n",checks,error.what());return 1;}
}
