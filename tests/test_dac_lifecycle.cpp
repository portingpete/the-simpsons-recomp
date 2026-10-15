#include "runtime/engine_audio_output.h"
#include "runtime/engine_audio.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/threads.h"
#include <cstdio>
#include <chrono>
#include <cstring>
#include <string_view>
#include <thread>
#include <objbase.h>

namespace {
using namespace Simpsons;
size_t checks=0;
void need(bool value,const char* message){++checks;if(!value) throw Failure(message);}
struct StartupObserved{};
struct Apartment {
    APTTYPE type{};
    APTTYPEQUALIFIER qualifier{};
    HRESULT status=CoGetApartmentType(&type,&qualifier);
    bool unowned() const {
        // A host worker may keep the process MTA alive. Its implicit presence
        // does not represent a CoInitializeEx reference on this main thread.
        return status==CO_E_NOTINITIALIZED ||
            (SUCCEEDED(status) && type==APTTYPE_MTA && qualifier==APTTYPEQUALIFIER_IMPLICIT_MTA);
    }
    bool operator==(const Apartment& other) const {
        return (unowned() && other.unowned()) ||
            (status==other.status && (FAILED(status) || (type==other.type && qualifier==other.qualifier)));
    }
};
struct ApartmentLease {
    bool held=false;
    void close(){if(held){CoUninitialize();held=false;}}
    ~ApartmentLease(){close();}
};
struct GuestSnapshot {
    struct Mapping {Region region;std::vector<uint8_t> bytes;};
    std::vector<Mapping> mappings;
    std::array<uint8_t,sizeof(PPCContext)> registers{};
    const uint32_t csr=PPCFPSCRRegister::getcsr();
    size_t threads,handles,objects,allocations,physical,tls;
    uint32_t nextHandle;
    explicit GuestSnapshot(Runtime& rt,const PPCContext& ctx):threads(rt.threads.size()),handles(rt.handles.size()),
        objects(rt.objectReferences.size()),allocations(rt.allocations.size()),physical(rt.physicalAllocations.size()),
        tls(rt.tlsBases.size()),nextHandle(rt.nextHandle) {
        std::memcpy(registers.data(),&ctx,sizeof(ctx));
        for(const auto& region:rt.regions) {
            need(region.size<=UINT32_MAX,"Prewarm fixture mapping exceeds checked width");
            for(uint64_t page=region.address>>12;page<(uint64_t(region.address)+region.size)>>12;++page)
                need(rt.pageAccess[uint32_t(page)].load()&1,"Prewarm fixture mapping is not readable");
            // Inspect mapped host backing, including deliberately unbound import
            // slots. This snapshot never executes a guest read of an SDK import.
            const auto* bytes=rt.base+region.address;
            mappings.push_back({region,{bytes,bytes+region.size}});
        }
    }
    void unchanged(Runtime& rt,const PPCContext& ctx) const {
        need(!std::memcmp(registers.data(),&ctx,sizeof(ctx)) && csr==PPCFPSCRRegister::getcsr(),
             "Endpoint preparation changed original registers or floating-point control");
        need(active==&rt && currentContext==&ctx && !rt.stopping && WaitForSingleObject(rt.stopEvent,0)==WAIT_TIMEOUT,
             "Endpoint preparation changed runtime execution or cancellation ownership");
        need(rt.regions.size()==mappings.size(),"Endpoint preparation changed guest mappings");
        for(size_t i=0;i<mappings.size();++i) {
            const auto& saved=mappings[i];const auto& actual=rt.regions[i];
            need(actual.address==saved.region.address && actual.size==saved.region.size && actual.write==saved.region.write &&
                 actual.name==saved.region.name && actual.use==saved.region.use,"Endpoint preparation changed guest mapping metadata");
            need(!std::memcmp(saved.bytes.data(),rt.base+actual.address,saved.bytes.size()),
                 "Endpoint preparation changed mapped guest memory or original control targets");
        }
        need(rt.threads.size()==threads && rt.handles.size()==handles && rt.objectReferences.size()==objects &&
             rt.allocations.size()==allocations && rt.physicalAllocations.size()==physical && rt.tlsBases.size()==tls &&
             rt.nextHandle==nextHandle,"Endpoint preparation published guest thread/handle/allocation ownership");
        need(!rt.window && !rt.engineDriver && !rt.engineAudio && !rt.engineAudioReader,
             "Endpoint preparation began graphics or original audio construction");
    }
};
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==2 || (argc==3 && (std::string_view(argv[2])=="--prewarm" ||
             std::string_view(argv[2])=="--prewarm-cancel")),"Original image path and optional prewarm mode required");
        const bool prewarm=argc==3,cancel=prewarm && std::string_view(argv[2])=="--prewarm-cancel";
        ApartmentLease outerApartment;
        Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);
        const Apartment initialApartment;
        if(cancel) {
            need(initialApartment.unowned(),"Cancellation fixture needs no initial main-thread COM reference");
            need(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"Cancellation fixture outer MTA failed");
            outerApartment.held=true;
        }
        const Apartment beforePreparation;
        if(cancel) need(SUCCEEDED(beforePreparation.status) && beforePreparation.type==APTTYPE_MTA &&
             beforePreparation.qualifier==APTTYPEQUALIFIER_NONE,"Cancellation fixture outer MTA did not acquire direct main-thread ownership");
        uint64_t preparedGeneration=0;
        if(prewarm) {
            const GuestSnapshot guest(rt,original);
            rt.engineAudioOutput=std::make_shared<EngineAudioOutput>(rt);
            preparedGeneration=rt.engineAudioOutput->prepareEndpoint();
            need(preparedGeneration && rt.engineAudioOutput->prepareEndpoint()==preparedGeneration,
                 "Repeated endpoint preparation allocated another backend");
            guest.unchanged(rt,original);
            bool unowned=false;try{rt.engineAudioOutput->view();}catch(const Failure&){unowned=true;}
            need(unowned,"Prepared endpoint fabricated an original Dac0 owner/view");
            bool rejected=false;
            std::thread wrongThread([&]{try{rt.engineAudioOutput->prepareEndpoint();}catch(const Failure&){rejected=true;}});
            wrongThread.join();need(rejected,"Endpoint preparation accepted a foreign native thread");
            guest.unchanged(rt,original);
            need(SUCCEEDED(Apartment{}.status) && Apartment{}.type==APTTYPE_MTA,"Prepared graph lacks its main-thread MTA");
            if(cancel) {
                rt.requestStop("Prewarm cancellation fixture");
                bool stopped=false;try{rt.engineAudioOutput->prepareEndpoint();}catch(const Failure&){stopped=true;}
                need(stopped,"Endpoint preparation ignored runtime cancellation");
                rt.engineAudioOutput.reset();
                need(Apartment{}==beforePreparation,"Prepared endpoint teardown unbalanced the preexisting MTA");
                outerApartment.close();
                need(Apartment{}==initialApartment,"Cancelled endpoint leaked COM apartment ownership");
                std::printf("PASS original Dac prewarm cancellation: %zu checks; no guest owner/source/PCM, repeated preparation, wrong-thread rejection, balanced nested MTA; ALL MUTED\n",checks);
                return 0;
            }
        }
        const auto entry=original;bool source=false,startup=false;
        rt.audioBoundaryObserver=[&](uint32_t pc,PPCContext&,uint8_t*) {
            const auto view=rt.engineAudioOutput->view();
            need(view.nativeEngine && view.muted && view.configured && view.identity,"Missing real native source at diagnostic boundary");
            if(prewarm) need(view.generation==preparedGeneration,"Original acquisition did not consume the prepared native endpoint");
            if(pc==0x82345920) {
                need(!source && !view.active && !view.workerId,"Source observation followed worker construction");
                if(prewarm) {
                    bool lateRejected=false;try{rt.engineAudioOutput->prepareEndpoint();}catch(const Failure&){lateRejected=true;}
                    need(lateRejected,"Endpoint preparation accepted an already-owned original source");
                }
                source=true;return;
            }
            need(pc==0x828166FC && source && view.active && view.workerId && view.event,"Startup did not release with a real source/worker/event");
            throw StartupObserved{};
        };
        try {runOriginal(original,rt.base);}catch(const StartupObserved&){startup=true;}
        rt.audioBoundaryObserver={};
        need(startup,"Actual original startup did not reach the post-Q4C-release boundary");
        auto view=rt.engineAudioOutput->view();const uint32_t root=view.root,owner=view.owner,identity=view.identity;
        std::shared_ptr<KernelHandle> worker;
        {
            std::lock_guard lock(rt.threadMutex);
            for(const auto& thread:rt.threads) if(thread->id==view.workerId) worker=thread->object;
        }
        need(worker && GetThreadId(worker->native)==view.workerId && view.workerId!=GetCurrentThreadId(),"Missing real original Dac0 native thread handle");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
        do {
            rt.checkRunning();view=rt.engineAudioOutput->view();
            if(view.submitted>=2 && view.consumed>=2 && view.backendRetired && view.passCallbacks) break;
            need(std::chrono::steady_clock::now()<deadline,"Original Dac0 worker did not complete real CPU/DSP/downstream work");Sleep(10);
        }while(true);
        need(!view.released && !view.workerReturned && !view.joined && WaitForSingleObject(worker->native,0)==WAIT_TIMEOUT,
             "Dac0 worker stopped before normal graph teardown");
        need(PPCLoadU32(rt.base,owner+0x40)==identity && PPCLoadU32(rt.base,0x82E31BCC)==root &&
             !PPCLoadU32(rt.base,0x82E2D9F0) && !PPCLoadU32(rt.base,0x82E2D9F4),
             "Native Dac0 worker populated SDK globals or lost CPU ownership");
        // Invoke the actual outer root destructor: it queues graph destruction,
        // releases Q4C, serializes with the Dac worker via Q48, then the native
        // hook joins the real OS thread before the original root is freed.
        EngineCpuCalls cpu(entry,rt.base);cpu.invoke(0x82338FA0,root);
        DWORD result=~0u;
        need(WaitForSingleObject(worker->native,0)==WAIT_OBJECT_0 && GetExitCodeThread(worker->native,&result) && result==0,
             "Original Dac0 worker did not exit normally and join");
        need(!PPCLoadU32(rt.base,0x82E31BCC) && rt.engineAudio && !rt.engineAudio->ready(),
             "Original root teardown did not free its root/stop the EXm0 factory");
        bool retired=false;try{rt.engineAudioOutput->view();}catch(const Failure&){retired=true;}
        need(retired,"Native Dac0 identity remained live after original root free");
        need(!outerApartment.held && Apartment{}==initialApartment,"Original Dac0 retirement leaked or removed prior COM ownership");
        std::printf("PASS original Dac lifecycle%s: %zu checks; owner=%08X identity=%08X, real original worker/DSP/callbacks/downstream completion, graph-driven release and OS join; ALL MUTED; no gameplay claim\n",prewarm?" with prewarm":"",checks,owner,identity);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original Dac lifecycle: %s\n",error.what());return 1;}
}
