#include "audio/dac_processor.h"
#include <windows.h>
#include <objbase.h>
#include <ks.h>
#include <ksmedia.h>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <future>
#include <set>
#include <thread>
#include <xmmintrin.h>

namespace {
using Output=Simpsons::Audio::NativeAudioOutput;
using Processor=Simpsons::Audio::DacProcessor;
using Access=Simpsons::Audio::DacProcessorTestAccess;
using OutputAccess=Simpsons::Audio::NativeAudioOutputTestAccess;
using Error=Simpsons::Audio::OutputError;
using Clock=std::chrono::steady_clock;
using PCM=std::array<float,Output::blockSamples>;
unsigned checks=0,realEnds=0,exactSamples=0;
void check(bool ok,const char* why) {++checks;if(!ok) throw std::runtime_error(why);}
template<class F> void rejects(F&& operation,const char* why) {
    try {operation();}catch(const Error& e) {check(FAILED(e.status()),"Rejection HRESULT must fail");return;}
    check(false,why);
}
template<class F> void until(F&& predicate,const char* why) {
    const auto deadline=Clock::now()+std::chrono::seconds(2);
    while(!predicate()) {check(Clock::now()<deadline,why);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
struct MTA {
    MTA(){const HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(hr)) throw Error("Fixture MTA",hr);}
    ~MTA(){CoUninitialize();}
};
struct HostFP {unsigned saved=_mm_getcsr();~HostFP(){_mm_setcsr(saved);}};
PCM samples(unsigned seed=0) {
    PCM result;
    for(unsigned f=0;f<256;++f) for(unsigned c=0;c<6;++c)
        result[f*6+c]=float(int((f*13+c*97+seed)%1025)-512)/512.0f;
    return result;
}
Output::Routing fixtureRoute(const Output& output) {
    // Explicit TEST routing only: these Windows labels do not name game planes.
    const auto endpoint=output.endpoint();
    Output::Routing route{KSAUDIO_SPEAKER_5POINT1_SURROUND,endpoint.channelMask,{}};
    route.matrix.resize(6*endpoint.channels);
    for(uint32_t d=0;d<endpoint.channels;++d) for(uint32_t s=0;s<6;++s)
        route.matrix[d*6+s]=float((d*3+s*2)%7+1)/64.0f;
    return route;
}
void configure(Output& output) {output.configure(fixtureRoute(output));check(output.endpoint().muted,"Automated output must be muted");}
Processor::Receipt submit(Processor& p,const PCM& pcm) {
    const auto result=p.submit(pcm);
    check(result.status==Processor::SubmitStatus::Accepted && result.receipt.generation==p.generation() &&
          result.receipt.sequence!=0,"Real source admission");return result.receipt;
}
Processor::SourceCompletion consumed(Processor& p,Processor::Receipt receipt) {
    until([&]{return p.query(receipt).status==Processor::SourceStatus::Consumed;},"Source DSP/real acceptance deadline");
    const auto result=p.query(receipt);
    check(result.processedFrames==256 && result.downstream.sequence && !result.error,"Consumption provenance");return result;
}
void exact(const std::vector<float>& actual,const PCM& expected,const char* why) {
    check(actual.size()==expected.size(),"Exact PCM extent");
    for(size_t i=0;i<expected.size();++i) {
        check(std::bit_cast<uint32_t>(actual[i])==std::bit_cast<uint32_t>(expected[i]),why);++exactSamples;
    }
}
PCM rational(unsigned seed,int initialNumerator,int stepNumerator) {
    // Independent analytically exact oracle. Raw=N/512 and gain=(A+j*B)/512;
    // |N*(A+j*B)|<2^24, so both integer conversion and /2^18 are exact binary32.
    // No production gain helper, fused math, host rounding assumption or SDK is used.
    PCM result;
    for(unsigned f=0;f<256;++f) for(unsigned c=0;c<6;++c) {
        const int n=int((f*13+c*97+seed)%1025)-512;
        result[f*6+c]=float(n*(initialNumerator+int(f)*stepNumerator))/262144.0f;
    }
    return result;
}
void realComplete(Processor& p,std::span<const Processor::Receipt> expected) {
    std::set<uint64_t> completed;
    until([&]{
        const auto snapshot=p.poll();check(snapshot.failure.stage==Processor::FailureStage::None,"Unexpected downstream failure");
        for(const auto& d:snapshot.downstream) if(d.retired) {
            check(d.completion.status==Output::BufferStatus::Consumed && !d.completion.error,"Real natural OnBufferEnd required");
            for(auto r:expected) if(d.source==r) {completed.insert(r.sequence);break;}
        }
        return completed.size()==expected.size();
    },"Actual downstream callbacks/retirement deadline");
    realEnds+=unsigned(completed.size());
}
void joined(Processor& p) {
    auto stop=std::async(std::launch::async,[&]{const auto csr=_mm_getcsr();p.stop();return _mm_getcsr()==csr;});
    check(stop.wait_for(std::chrono::seconds(2))==std::future_status::ready,"Processor stop/join deadline");
    check(stop.get(),"Stop caller MXCSR");
    const auto snapshot=p.poll();check(snapshot.stopped && snapshot.workerExited && !snapshot.active,"Actual processing thread joined");
    check(Access::workerFPRestored(p),"Host processing thread MXCSR leaked");
}
void preflightAndPendingStop() {
    {
        Output unconfigured({.capacity=4,.muted=true,.deviceId={}});
        rejects([&]{Processor bad(unconfigured);},"Unconfigured processor accepted");
        check(!unconfigured.poll().stopped,"Constructor preflight destroyed caller backend");
    }
    {
        Output wrongCapacity({.capacity=2,.muted=true,.deviceId={}});configure(wrongCapacity);
        rejects([&]{Processor bad(wrongCapacity);},"Wrong downstream capacity accepted");
    }
    {
        Output running({.capacity=4,.muted=true,.deviceId={}});configure(running);running.start();
        rejects([&]{Processor bad(running);},"Already-running backend accepted");
    }
    {
        Output occupied({.capacity=4,.muted=true,.deviceId={}});configure(occupied);occupied.submit(samples());
        rejects([&]{Processor bad(occupied);},"Foreign backend receipt accepted");
    }
    Output output({.capacity=4,.muted=true,.deviceId={}});configure(output);Processor p(output);
    HostFP restore;_mm_setcsr(0xE07F);const auto csr=_mm_getcsr();
    auto data=samples(); // Dyadic fixture arithmetic is exact under the hostile CSR.
    rejects([&]{p.submit(std::span(data).first(1535));},"Short source admitted");
    for(uint32_t invalid:{0x7FC12345u,0x7F800001u,0x7F800000u,0x3F800001u,0xBF800001u}) {
        data.back()=std::bit_cast<float>(invalid);rejects([&]{p.submit(data);},"Invalid late raw sample admitted");
    }
    for(uint32_t invalid:{0x7FC12345u,0x7F800001u,0x7F800000u,0x3F800001u,0x80000001u})
        rejects([&]{p.setTargetGain(std::bit_cast<float>(invalid));},"Invalid gain admitted");
    p.setTargetGain(-0.0f);
    check(_mm_getcsr()==csr,"Invalid/admission/gain API leaked host MXCSR");
    check(p.poll().owned==0,"Invalid admission allocated a source slot");
    const auto original=samples(1);auto first=submit(p,original),second=submit(p,samples(2));
    check(p.submit(original).status==Processor::SubmitStatus::Backpressure,"Logical source capacity must be2");
    rejects([&]{p.retire(first);},"Queued source retired");
    rejects([&]{p.query({first.generation+1,first.sequence});},"Foreign source generation accepted");
    rejects([&]{p.query({first.generation,999});},"Unknown source receipt accepted");
    rejects([&]{p.wait(60001);},"Unbounded observer wait accepted");
    joined(p);
    for(auto receipt:{first,second}) {
        const auto q=p.query(receipt);
        check(q.status==Processor::SourceStatus::Cancelled && !q.error && !q.processedFrames && !q.downstream.sequence,
              "Unactivated stop fabricated consumption");
        p.retire(receipt);rejects([&]{p.query(receipt);},"Stale source identity reused");
    }
    rejects([&]{p.submit(original);},"Stopped processor accepts raw data");
    rejects([&]{p.activate();},"Stopped processor restarts");
    rejects([&]{p.setTargetGain(1);},"Stopped processor accepts gain");
    check(output.poll().stopped && output.poll().owned==0,"Stop did not drain backend");
    std::puts("PASS validation / capacity2 / stale ownership / unactivated cancellation / actual host join");
}
void copyActivationAndCompletion() {
    Output output({.capacity=4,.muted=true,.deviceId={}});configure(output);Processor p(output);p.setTargetGain(0.25f);
    const auto raw=samples(17);
    void* allocation=VirtualAlloc(nullptr,sizeof(raw),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    check(allocation!=nullptr,"Raw fixture allocation");std::memcpy(allocation,raw.data(),sizeof(raw));
    const auto submission=p.submit({static_cast<const float*>(allocation),raw.size()});
    check(submission.status==Processor::SubmitStatus::Accepted,"Owned raw admission");
    std::memset(allocation,0xCC,sizeof(raw));check(VirtualFree(allocation,0,MEM_RELEASE)!=0,"Caller raw storage release");
    const auto first=submission.receipt,second=submit(p,samples(19));
    exact(Access::raw(p,first),raw,"Raw PCM copy differs after caller free");
    p.setTargetGain(0.5f); // Accepted raw packets must use this later processing target.
    HostFP restore;const auto csr=_mm_getcsr();p.activate();p.activate();check(_mm_getcsr()==csr,"Activate caller MXCSR");
    const auto a=consumed(p,first),b=consumed(p,second);
    for(const auto& q:{a,b}) check(q.initialGain==0.5f && q.targetGain==0.5f && q.finalGain==0.5f,"Activation must seed current from latest target");
    exact(Access::processed(p,first),rational(17,256,0),"Activation DSP oracle");
    exact(Access::submitted(p,first),rational(17,256,0),"Actual backend copy differs from DSP");
    exact(Access::submitted(p,second),rational(19,256,0),"FIFO second native block oracle");
    const std::array receipts{first,second};realComplete(p,receipts);
    check(p.submit(raw).status==Processor::SubmitStatus::Backpressure,"Completed logical receipts must count until retire");
    rejects([&]{output.query(a.downstream);},"Worker did not retire actual ended backend receipt");
    p.retire(first);p.retire(second);
    const auto previous=p.poll();p.setTargetGain(0.125f);
    until([&]{const auto s=p.poll();return s.starvationPasses>previous.starvationPasses && s.current==0.125f;},"Real empty-demand starvation snap");
    auto idle=p.poll();check(idle.owned==0 && idle.downstream.empty() && idle.backendRetired==2,"Starvation invented an input/output packet");
    check(p.wait(1000),"Empty real processing pass must signal original-worker observer");
    check(p.poll().processingHints>idle.processingHints,"Processing wake hint sequence did not advance");
    joined(p);
    std::puts("PASS owned copy / target-after-admission / real Start and OnBufferEnd / two ownership stages / empty wake+starvation");
}
void processingTimeTargetAndStop() {
    {
        Output output({.capacity=4,.muted=true,.deviceId={}});configure(output);Processor p(output);
        Access::hold(p,Access::Gate::BeforeDsp,true);Access::hold(p,Access::Gate::BeforeSubmit,true);
        p.setTargetGain(0.25f);p.activate();const auto receipt=submit(p,samples(23));
        p.setTargetGain(0.75f);Access::hold(p,Access::Gate::BeforeDsp,false);
        until([&]{return p.query(receipt).processedFrames==256;},"DSP gate deadline");
        auto q=p.query(receipt);
        check(q.status==Processor::SourceStatus::Processing && !q.downstream.sequence,"DSP alone fabricated consumption");
        rejects([&]{p.retire(receipt);},"DSP-only source retired");
        exact(Access::processed(p,receipt),rational(23,128,1),"Processing-time gain ramp oracle");
        p.setTargetGain(0.125f); // After DSP: affects later work, never rebakes this result.
        Access::hold(p,Access::Gate::BeforeSubmit,false);q=consumed(p,receipt);
        check(q.initialGain==0.25f && q.targetGain==0.75f && q.finalGain==0.75f,"Captured processing gain provenance changed");
        exact(Access::submitted(p,receipt),rational(23,128,1),"Interleaving/actual submit changed components");
        const std::array receipts{receipt};realComplete(p,receipts);p.retire(receipt);joined(p);
    }
    {
        Output output({.capacity=4,.muted=true,.deviceId={}});configure(output);Processor p(output);
        Access::hold(p,Access::Gate::BeforeSubmit,true);p.activate();const auto r=submit(p,samples(29));
        until([&]{return p.query(r).processedFrames==256;},"DSP-only stop setup");
        joined(p);const auto q=p.query(r);
        check(q.status==Processor::SourceStatus::Cancelled && q.processedFrames==256 && !q.downstream.sequence,
              "Stop between DSP and acceptance labelled consumed");
        p.retire(r);
    }
    std::puts("PASS processing-time target / exact ramp / no re-baking / stop interrupts DSP-to-submit gap");
}
void boundedDownstreamProvenance() {
    Output output({.capacity=4,.muted=true,.deviceId={}});configure(output);Processor p(output);p.activate();
    std::array<Processor::Receipt,4> old;
    std::array<Output::Receipt,4> backend;
    for(unsigned pair=0;pair<2;++pair) {
        old[pair*2]=submit(p,samples(pair));old[pair*2+1]=submit(p,samples(pair+9));
        for(unsigned j=0;j<2;++j) {const auto i=pair*2+j;backend[i]=consumed(p,old[i]).downstream;p.retire(old[i]);}
    }
    // Do not poll: actual callbacks are still harvested/retired, but four terminal
    // provenance records must remain bounded and observable until acknowledgment.
    until([&]{
        for(auto r:backend) {try {output.query(r);return false;}catch(const Error&) {}}
        return true;
    },"Real callbacks must retire independently of observer acknowledgment");
    const auto next=submit(p,samples(31)),last=submit(p,samples(37));
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
    check(p.query(next).status==Processor::SourceStatus::Queued && !p.query(next).processedFrames &&
          p.query(last).status==Processor::SourceStatus::Queued,"Unobserved downstream evidence grew beyond capacity4");
    p.setTargetGain(0.5f);
    const auto snapshot=p.poll();
    check(snapshot.downstream.size()==4 && snapshot.backendRetired==4,"Lost downstream completion provenance");
    for(unsigned i=0;i<4;++i) {
        const auto& d=snapshot.downstream[i];
        check(d.source==old[i] && d.completion.receipt==backend[i] && d.retired &&
              d.completion.status==Output::BufferStatus::Consumed,"Separate natural playback completion identity");++realEnds;
    }
    const auto a=consumed(p,next),b=consumed(p,last);
    check(a.targetGain==0.5f && b.targetGain==0.5f,"Backpressured queued work used admission-time gain");
    exact(Access::submitted(p,next),rational(31,512,-1),"Backpressured processing ramp");
    exact(Access::submitted(p,last),rational(37,256,0),"Second processing block current persistence");
    const std::array receipts{next,last};realComplete(p,receipts);p.retire(next);p.retire(last);joined(p);
    std::puts("PASS source2/downstream4 / real retirement / bounded provenance acknowledgment / queued target timing");
}
void failureAndConcurrency() {
    {
        Output output({.capacity=4,.muted=true,.deviceId={}});configure(output);Processor p(output);
        Access::hold(p,Access::Gate::BeforeSubmit,true);p.activate();
        const auto first=submit(p,samples(41)),second=submit(p,samples(43));
        until([&]{return p.query(first).processedFrames==256;},"Failure injection DSP boundary");
        OutputAccess::voiceError(output,E_FAIL); // SAME handler; explicitly injected, not device removal.
        Access::hold(p,Access::Gate::BeforeSubmit,false);
        until([&]{return p.query(first).status==Processor::SourceStatus::Failed;},"Terminal source failure deadline");
        joined(p);const auto snapshot=p.poll();
        check(snapshot.failure.error==E_FAIL && snapshot.failure.stage==Processor::FailureStage::Submit &&
              snapshot.failure.source==first && !snapshot.failure.operation.empty() &&
              snapshot.failure.backendOrigin==Output::ErrorOrigin::VoiceCallback,"Failure lost operation/source/backend provenance");
        for(auto r:{first,second}) {
            const auto q=p.query(r);check(q.status==Processor::SourceStatus::Failed && q.error==E_FAIL && !q.downstream.sequence,
                                         "Error reported consumed/accepted");p.retire(r);
        }
    }
    {
        // Mirror the observed asynchronous engine failure with no source work.
        // This delivers the SAME critical handler, not a real device removal.
        Output output({.capacity=4,.muted=true,.deviceId={}});configure(output);Processor p(output);p.activate();
        const auto raw=static_cast<int32_t>(0x88880001u);
        OutputAccess::criticalError(output,raw);
        until([&]{return p.poll().failure.stage!=Processor::FailureStage::None;},"Idle critical error propagation deadline");
        joined(p);const auto snapshot=p.poll();
        check(snapshot.failure.error==raw && snapshot.failure.stage==Processor::FailureStage::BackendState &&
              snapshot.failure.backendOrigin==Output::ErrorOrigin::EngineCallback && !snapshot.failure.source.sequence &&
              !snapshot.failure.downstream.sequence && snapshot.sources.empty() && snapshot.downstream.empty(),
              "Idle critical error lost actual HRESULT/origin or invented a receipt");
        rejects([&]{p.submit(samples());},"Idle critical backend error was suppressed");
    }
    {
        Output output({.capacity=4,.muted=true,.deviceId={}});configure(output);Processor p(output);
        Access::hold(p,Access::Gate::BeforeDsp,true);p.activate();const auto r=submit(p,samples());
        auto a=std::async(std::launch::async,[&]{p.stop();});auto b=std::async(std::launch::async,[&]{p.stop();});
        check(a.wait_for(std::chrono::seconds(2))==std::future_status::ready &&
              b.wait_for(std::chrono::seconds(2))==std::future_status::ready,"Concurrent stop joins blocked");a.get();b.get();
        check(p.query(r).status==Processor::SourceStatus::Cancelled && p.poll().workerExited,"Concurrent stop result");p.retire(r);
    }
    uint64_t previous=0;Processor::Receipt stale;
    for(unsigned i=0;i<3;++i) {
        Output output({.capacity=4,.muted=true,.deviceId={}});configure(output);Processor p(output);
        check(p.generation()>previous,"Processor generation reused");previous=p.generation();
        if(stale.sequence) rejects([&]{p.query(stale);},"Cross-generation stale receipt accepted");
        stale=submit(p,samples(i));p.activate(); // Destructor owns stop/join with live work.
    }
    std::puts("PASS injected real handler failure / not-consumed partial DSP / concurrent stop / generations / live-work destructor");
}
}
int main() {
    try {
        MTA mta;preflightAndPendingStop();copyActivationAndCompletion();processingTimeTargetAndStop();
        boundedDownstreamProvenance();failureAndConcurrency();
        std::printf("PASS native Dac processor: %u checks, %u exact component samples, %u actual OnBufferEnd receipts; all muted; no guest/SDK voice\n",
                    checks,exactSamples,realEnds);return 0;
    }catch(const std::exception& e) {std::fprintf(stderr,"FAIL native Dac processor: %s\n",e.what());return 1;}
}
