#include "runtime/engine_audio_output.h"
#include "runtime/engine_audio.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/threads.h"
#include <cstdio>
#include <chrono>

namespace {
using namespace Simpsons;
size_t checks=0;
void need(bool value,const char* message){++checks;if(!value) throw Failure(message);}
struct StartupObserved{};
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==2,"Original image path required");Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);
        const auto entry=original;bool source=false,startup=false;
        rt.audioBoundaryObserver=[&](uint32_t pc,PPCContext&,uint8_t*) {
            const auto view=rt.engineAudioOutput->view();
            need(view.nativeEngine && view.muted && view.configured && view.identity,"Missing real native source at diagnostic boundary");
            if(pc==0x82345920) {need(!source && !view.active && !view.workerId,"Source observation followed worker construction");source=true;return;}
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
        std::printf("PASS original Dac lifecycle: %zu checks; owner=%08X identity=%08X, real original worker/DSP/callbacks/downstream completion, graph-driven release and OS join; ALL MUTED; no gameplay claim\n",checks,owner,identity);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original Dac lifecycle: %s\n",error.what());return 1;}
}
