#include "engine_audio_output.h"
#include "engine_audio.h"
#include "engine_cpu_calls.h"
#include "audio/native_audio_output.h"
#include "audio/dac_processor.h"
#include "audio/dac_pcm.h"
#include <objbase.h>
#include <array>
#include <cstdio>
#include <optional>
#include <atomic>
#include <chrono>

namespace Simpsons {
namespace {
constexpr uint32_t descriptor=0x82D069B4,vtable=0x821DCB90,extent=0x3108;
void require(bool value,const char* message){if(!value) throw Failure(message);}
std::atomic<uint32_t> nextIdentity{0x00700001};
uint32_t identity(Runtime& rt) {
    uint32_t id=nextIdentity.load();
    while(id<0x00800000) if(nextIdentity.compare_exchange_weak(id,id+1)) {
        require(!rt.pageAccess[id>>12].load(),"Native Dac0 identity overlaps guest storage");return id;
    }
    throw Failure("Native Dac0 identity space exhausted");
}
struct HostFloatingPoint {
    const uint32_t previous=PPCFPSCRRegister::getcsr();
    HostFloatingPoint(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~HostFloatingPoint(){PPCFPSCRRegister::restoreHostCSR(previous);}
};
}
struct EngineAudioOutput::State {
    Runtime& runtime;
    const DWORD thread=GetCurrentThreadId();
    mutable std::mutex mutex;
    std::condition_variable changed;
    struct Construction {
        PPCContext* cpu;
        uint32_t owner,root,sp,mixer=0;
        uint32_t id=0,event=0,workerId=0;
        std::shared_ptr<KernelHandle> worker;
        bool configured=false,active=false,released=false,workerReturned=false,joined=false;
        std::array<Audio::DacProcessor::Receipt,2> receipts{};
        PPCContext* callback=nullptr;
        PPCContext* mixerCpu=nullptr;
        uint32_t mixerSp=0;
        uint64_t hints=0,submitted=0,consumed=0,cancelled=0,passes=0,backendRetired=0;
    };
    std::optional<Construction> construction;
    std::unique_ptr<Audio::NativeAudioOutput> native;
    std::unique_ptr<Audio::DacProcessor> processor;
    bool apartment=false;
    explicit State(Runtime& rt):runtime(rt) {
        require(rt.mainThreadHandle && GetThreadId(rt.mainThreadHandle->native)==thread,
                "Dac0 construction requires the original main thread");
    }
    ~State() {
        HostFloatingPoint fp;
        // Runtime first cancels/joins original guest workers. The private host
        // processor and every downstream callback/data read must quiesce before
        // releasing its backend, the main thread's MTA or guest memory.
        processor.reset();
        native.reset();
        if(apartment) {
            if(GetCurrentThreadId()!=thread) {
                std::fprintf(stderr,"[NATIVE AUDIO FAILURE] Dac0 MTA owner destroyed on a different thread\n");
                std::terminate();
            }
            CoUninitialize();
        }
        if(construction) std::fprintf(stderr,
            "[NATIVE AUDIO] terminal Dac0 cleanup owner=%08X mixer=%08X; original graph cleanup was not completed\n",
            construction->owner,construction->mixer);
    }
    void caller(PPCContext& ctx,uint8_t* base,bool main=false) const {
        require(active==&runtime && base==runtime.base && (!main || GetCurrentThreadId()==thread) && currentContext==&ctx,
                "Invalid Dac0 runtime/thread/context");
        runtime.checkRunning();
        require(ctx.r1.u32>=0x100 && !(ctx.r1.u32&15),"Invalid Dac0 caller stack");
        runtime.pointer(ctx.r1.u32-0x100,0x100,true);
    }
    void noHardware(uint8_t* base) const {
        require(!PPC_LOAD_U32(0x82E36CB8),"Dac0 SDK-disable branch is not native output");
        require(!PPC_LOAD_U32(0x82E2D9F0) && !PPC_LOAD_U32(0x82E2D9F4),"Original Dac0 output SDK was initialized");
    }
    Construction& frame(PPCContext& ctx,uint8_t* base) {
        caller(ctx,base,true);noHardware(base);
        require(construction.has_value(),"Dac0 output has no construction ownership");
        auto& c=*construction;
        require(c.cpu==&ctx && c.owner==ctx.r31.u32 && ctx.r1.u32==c.sp-0x120,
                "Dac0 constructor frame/owner changed");
        require(PPC_LOAD_U32(c.owner+4)==c.root && PPC_LOAD_U32(0x82E31BCC)==c.root &&
                PPC_LOAD_U32(c.owner+0x10)==descriptor && PPC_LOAD_U32(c.owner)==vtable,
                "Dac0 CPU object/root identity changed");
        runtime.pointer(c.owner,extent,true);
        return c;
    }
    Construction& owner(PPCContext& ctx,uint8_t* base,bool worker=false) {
        caller(ctx,base);noHardware(base);
        require(construction.has_value(),"Dac0 operation has no original owner");auto& c=*construction;
        require(!c.released && c.configured && processor && native,"Dac0 source is not live");
        runtime.pointer(c.owner,extent,true);
        require(PPC_LOAD_U32(c.owner)==vtable && PPC_LOAD_U32(c.owner+4)==c.root &&
                PPC_LOAD_U32(c.owner+0x10)==descriptor && PPC_LOAD_U32(c.owner+0x40)==c.id &&
                PPC_LOAD_U32(0x82E31BCC)==c.root,"Dac0 native/CPU source identity changed");
        if(worker) require(c.workerId==GetCurrentThreadId() && ctx.r31.u32==c.owner,
                           "Dac0 processing call is not on its original worker");
        return c;
    }
    void event(uint8_t* base,const Construction& c) const {
        require(c.event && PPC_LOAD_U32(c.owner+0x304C)==c.event,"Dac0 CPU event identity changed");
        runtime.pointer(c.event,16,true);
        require(PPC_LOAD_U8(c.event)==1 && PPC_LOAD_U32(c.event+4)<=1 &&
                PPC_LOAD_U32(c.event+8)==c.event+8 && PPC_LOAD_U32(c.event+12)==c.event+8,
                "Dac0 event is not the original synchronization-event CPU header");
    }
    void checked(const Audio::DacProcessor::Snapshot& snapshot) const {
        const auto& failure=snapshot.failure;
        if(failure.stage!=Audio::DacProcessor::FailureStage::None) {
            char provenance[192];
            int n=std::snprintf(provenance,sizeof(provenance),
                " stage=%u error=%08X backend_origin=%u source=%llu/%llu downstream=%llu/%llu: ",
                unsigned(failure.stage),uint32_t(failure.error),
                unsigned(failure.backendOrigin),
                static_cast<unsigned long long>(failure.source.generation),
                static_cast<unsigned long long>(failure.source.sequence),
                static_cast<unsigned long long>(failure.downstream.generation),
                static_cast<unsigned long long>(failure.downstream.sequence));
            if(n<0||size_t(n)>=sizeof(provenance)) throw Failure("Native Dac0 processing failed: diagnostic truncated");
            throw Failure(std::string("Native Dac0 processing failed")+provenance+failure.operation);
        }
    }
    void callback(PPCContext& incoming,uint8_t* base,uint32_t completion,uint32_t reason) {
        EngineCpuCalls cpu(incoming,base);auto& ctx=cpu.registers();
        const uint32_t record=ctx.r1.u32+0x50;
        {
            std::lock_guard lock(mutex);auto& c=*construction;
            require(!c.callback,"Nested Dac0 native-to-original callback");
            PPC_STORE_U32(record,c.owner);PPC_STORE_U32(record+4,completion);PPC_STORE_U32(record+8,reason);
            c.callback=&ctx;
        }
        try {cpu.invoke(completion?0x823463D0:0x823463B0,record);}
        catch(...) {std::lock_guard lock(mutex);construction->callback=nullptr;throw;}
        std::lock_guard lock(mutex);construction->callback=nullptr;
    }
    Audio::DacProcessor::Snapshot drain(PPCContext& ctx,uint8_t* base,bool releasing=false) {
        Audio::DacProcessor::Snapshot snapshot;
        {
            std::lock_guard lock(mutex);
            require(construction && processor,"Missing Dac0 processing ownership");
            auto& c=*construction;
            require(c.workerId==GetCurrentThreadId(),"Dac0 callback delivery is not on its original worker");
            event(base,c);snapshot=processor->poll();checked(snapshot);
            c.backendRetired=snapshot.backendRetired;
        }
        for(const auto& complete:snapshot.sources) {
            if(complete.status==Audio::DacProcessor::SourceStatus::Queued ||
               complete.status==Audio::DacProcessor::SourceStatus::Processing) continue;
            require(complete.status!=Audio::DacProcessor::SourceStatus::Failed,"Dac0 source processing failed");
            const bool consumed=complete.status==Audio::DacProcessor::SourceStatus::Consumed;
            require(consumed || releasing,"Dac0 source cancelled outside original final release");
            uint32_t slot=2,word=0;
            {
                std::lock_guard lock(mutex);auto& c=*construction;
                for(uint32_t i=0;i<2;++i) if(c.receipts[i]==complete.receipt) slot=i;
                require(slot<2,"Dac0 processing receipt has no original slot");word=c.owner+0x3100+4*slot;
                require(PPC_LOAD_U32(word)==2,"Dac0 completion references an unsubmitted original slot");
                if(consumed) require(complete.processedFrames==256 && complete.downstream.generation && complete.downstream.sequence,
                                     "Dac0 consumed source lacks full DSP/native acceptance evidence");
            }
            callback(ctx,base,word,consumed?0:0x80004004);
            {
                std::lock_guard lock(mutex);auto& c=*construction;
                require(PPC_LOAD_U32(word)==0,"Original Dac0 completion callback did not release its slot");
                processor->retire(complete.receipt);c.receipts[slot]={};
                if(consumed) ++c.consumed;else ++c.cancelled;
            }
        }
        bool pass=false;
        {
            std::lock_guard lock(mutex);auto& c=*construction;
            if(!releasing && snapshot.processingHints!=c.hints) {c.hints=snapshot.processingHints;pass=true;}
        }
        if(pass) {callback(ctx,base,0,0);std::lock_guard lock(mutex);++construction->passes;}
        return snapshot;
    }
};
EngineAudioOutput::EngineAudioOutput(Runtime& rt):state(std::make_unique<State>(rt)) {}
EngineAudioOutput::~EngineAudioOutput()=default;
void EngineAudioOutput::begin(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.caller(ctx,base,true);s.noHardware(base);
    require(!s.construction && !s.native,"Dac0 constructor entered with a pending/live output owner");
    // The retained prologue uses stwu r1,-0x120(r1). Validate its entire
    // immediate frame before publishing ownership or executing any save.
    require(ctx.r1.u32>=0x120,"Invalid Dac0 entry frame");
    s.runtime.pointer(ctx.r1.u32-0x120,0x120,true);
    const uint32_t owner=ctx.r3.u32;
    require(owner && !(owner&15) && ctx.lr==0x8233DC44 && ctx.r5.u32==descriptor,
            "Dac0 constructor did not originate in the original graph node wrapper");
    s.runtime.pointer(owner,extent,true);
    const uint32_t root=PPC_LOAD_U32(owner+4);
    require(root && root==PPC_LOAD_U32(0x82E31BCC) && PPC_LOAD_U32(owner+0x10)==descriptor,
            "Dac0 generic CPU header was not initialized by the original wrapper");
    s.runtime.pointer(root,0x100,true);
    require(s.runtime.engineAudio && s.runtime.engineAudio->ready(),"Dac0 construction precedes native EXm0 factory acquisition");
    require(PPC_LOAD_U32(descriptor+4)==0x82345618 && PPC_LOAD_U32(descriptor+8)==0x823456D0 &&
            PPC_LOAD_U32(descriptor+0x10)==0x82353B40 && PPC_LOAD_U32(descriptor+0x28)==0x44616330 &&
            PPC_LOAD_U32(vtable)==0x82345A50 && PPC_LOAD_U32(vtable+4)==0x82345AF8 && PPC_LOAD_U32(vtable+8)==0x82346528,
            "Original Dac0 descriptor or virtual methods changed");
    // Validate against shared VM metadata in its synchronization domain.
    {
        std::lock_guard vmLock(s.runtime.vmMutex);
        for(const auto& region:s.runtime.regions) if(uint64_t(owner)<uint64_t(region.address)+region.size &&
            uint64_t(region.address)<uint64_t(owner)+extent)
            require(region.use!=MemoryUse::Image && region.use!=MemoryUse::Stack && region.use!=MemoryUse::Kernel,
                    "Dac0 CPU owner overlaps protected runtime storage");
    }
    s.construction=State::Construction{&ctx,owner,root,ctx.r1.u32};
}
void EngineAudioOutput::acquire(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);auto& c=s.frame(ctx,base);
    require(!s.native && !s.apartment && ctx.r3.u32==ctx.r1.u32+0x58 && ctx.r29.u32==c.owner+0x28 && !ctx.r30.u32,
            "Dac0 native engine acquisition callsite changed");
    c.mixer=PPC_LOAD_U32(c.owner+0x24);
    require(c.mixer && !(c.mixer&0x7F),"Dac0 CPU mixer allocation is missing or misaligned");
    s.runtime.pointer(c.mixer,0x30080,true);
    require(PPC_LOAD_U32(c.mixer+0x30008)==c.root && PPC_LOAD_U32(c.owner+0xC)==c.owner+0x28 &&
            PPC_LOAD_U32(c.owner+0x28)==0x40400000 && PPC_LOAD_U32(c.owner+0x30)==0x473B8000 &&
            PPC_LOAD_U32(c.root+0xDC)==0x473B8000 && PPC_LOAD_U8(0x82E31F9D)==6 &&
            PPC_LOAD_U8(0x82E31FA0)==1 && !PPC_LOAD_U8(0x82E31F9F),
            "Original Dac0 CPU setup differs from the verified default profile");
    if(s.runtime.graphicsStartupObserver)
        s.runtime.graphicsStartupObserver(0x823458C0,ctx,base);
    // COM initialization/cleanup are host work too, including exceptional
    // creation paths outside the backend's own floating-point scope.
    HostFloatingPoint fp;
    const HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(hr)) throw Audio::OutputError("Initialize Dac0 output MTA",hr);
    s.apartment=true;
    try {s.native=std::make_unique<Audio::NativeAudioOutput>(Audio::NativeAudioOutput::Options{.capacity=4,.muted=true,.deviceId={}});}
    catch(...) {CoUninitialize();s.apartment=false;throw;}
    const auto endpoint=s.native->endpoint();
    std::fprintf(stderr,"[NATIVE AUDIO] Dac0 engine acquired owner=%08X root=%08X mixer=%08X generation=%llu endpoint_channels=%u mask=%08X muted=%u; source not configured\n",
        c.owner,c.root,c.mixer,static_cast<unsigned long long>(s.native->generation()),endpoint.channels,endpoint.channelMask,endpoint.muted);
    // A real native engine/master graph now owns output initialization. No
    // console singleton pointer, MMIO value or SDK identity is fabricated.
    ctx.r3.u64=0;ctx.lr=0x823458C4;
}
void EngineAudioOutput::source(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);auto& c=s.frame(ctx,base);
    require(s.native && ctx.r3.u32==ctx.r1.u32+0x90 && ctx.r4.u32==c.owner+0x40 && ctx.r29.u32==ctx.r4.u32,
            "Dac0 source construction has no native engine or valid original arguments");
    const uint32_t record=ctx.r3.u32;s.runtime.pointer(record,0x5C,false);
    require(PPC_LOAD_U8(record)==0 && PPC_LOAD_U8(record+4)==6 && PPC_LOAD_U32(record+8)==48000 &&
            PPC_LOAD_U8(record+0x3B)==2 && PPC_LOAD_U32(record+0x3C)==0 && PPC_LOAD_U8(record+0x40)==1 &&
            PPC_LOAD_U32(record+0x4C)==0x823463B0 && PPC_LOAD_U32(record+0x50)==0x823463D0 &&
            PPC_LOAD_U32(record+0x54)==0 && PPC_LOAD_U32(record+0x58)==c.owner,
            "Original Dac0 source record changed");
    const auto snapshot=s.native->poll();
    require(!snapshot.error && !snapshot.configured && !snapshot.running && !snapshot.stopped && !snapshot.owned,
            "Dac0 source guard found unexpected native processing ownership");
    require(!PPC_LOAD_U32(c.owner+0x40),"Dac0 source output was already populated");
    s.native->configureWindows51();
    s.processor=std::make_unique<Audio::DacProcessor>(*s.native);
    c.id=identity(s.runtime);c.configured=true;
    // This typed identity names real native ownership. It is deliberately not
    // mapped memory and must never reach a console SDK dereference.
    PPC_STORE_U32(c.owner+0x40,c.id);ctx.r3.u64=0;ctx.lr=0x82345920;
    std::fprintf(stderr,"[NATIVE AUDIO] Dac0 native source owner=%08X identity=%08X; Windows standard 5.1 adaptation, source_slots=2 downstream_slots=4, muted\n",c.owner,c.id);
}
void EngineAudioOutput::activate(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);auto& c=s.frame(ctx,base);
    require(c.configured && !c.active && ctx.r3.u32==c.id && !ctx.r4.u32 && PPC_LOAD_U32(c.owner+0x40)==c.id,
            "Dac0 native activation arguments or state changed");
    const uint32_t event=PPC_LOAD_U32(c.owner+0x304C);
    require(!c.event || c.event==event,"Dac0 worker and constructor event disagree");c.event=event;s.event(base,c);
    require(!PPC_LOAD_U32(c.owner+0x3044) && !PPC_LOAD_U32(c.owner+0x3048),"Dac0 initial CPU ring cursors changed");
    for(uint32_t i=0;i<2;++i) {
        const uint32_t record=c.owner+0x3050+0x58*i;
        require(PPC_LOAD_U32(record)==c.owner+0x44+0x1800*i && PPC_LOAD_U32(record+4)==0x1800 &&
                PPC_LOAD_U32(record+0x54)==c.owner+0x3100+4*i && !PPC_LOAD_U32(c.owner+0x3100+4*i),
                "Original Dac0 initial packet/slot construction changed");
    }
    s.processor->activate();c.active=true;ctx.r3.u64=0;ctx.lr=0x82345A40;
    std::fprintf(stderr,"[NATIVE AUDIO] Dac0 source activated owner=%08X identity=%08X event=%08X; original worker retained\n",c.owner,c.id,c.event);
}
void EngineAudioOutput::workerBegin(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);auto& c=s.owner(ctx,base);
    require(ctx.r3.u32==c.owner && ctx.lr==0x82346308 && !c.worker && GetCurrentThreadId()!=s.thread,
            "Dac0 worker entry does not match the original trampoline");
    c.worker=s.runtime.threadObject(PPC_LOAD_U32(ctx.r13.u32+0x100));
    require(c.worker && c.worker->type==KernelHandle::Type::Thread && GetThreadId(c.worker->native)==GetCurrentThreadId(),
            "Dac0 worker has no real owned native thread");
    c.workerId=GetCurrentThreadId();const uint32_t event=PPC_LOAD_U32(c.owner+0x304C);
    require(!c.event || c.event==event,"Dac0 worker event differs from its constructor");c.event=event;s.event(base,c);
    s.changed.notify_all();
    std::fprintf(stderr,"[NATIVE AUDIO] original Dac0 worker entered owner=%08X native_thread=%u\n",c.owner,c.workerId);
}
void EngineAudioOutput::workerEnd(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.caller(ctx,base);require(s.construction.has_value(),"Missing Dac0 worker exit owner");
    auto& c=*s.construction;
    // S may already have been freed by graph destruction on this very worker.
    // Only retained native metadata and live registers are read here.
    require(c.workerId==GetCurrentThreadId() && c.released && ctx.r25.u32==c.root && !c.workerReturned,
            "Dac0 worker returned before native/original graph release");
    c.workerReturned=true;s.changed.notify_all();
}
void EngineAudioOutput::signal(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.caller(ctx,base);require(s.construction.has_value(),"Missing Dac0 callback signal owner");
    auto& c=*s.construction;
    require(c.callback==&ctx && c.workerId==GetCurrentThreadId() && ctx.r3.u32==c.event,
            "Dac0 signal did not originate in an owned original CPU callback");
    s.event(base,c);const uint32_t previous=PPC_LOAD_U32(c.event+4);PPC_STORE_U32(c.event+4,1);ctx.r3.u64=previous;
    // Both original callbacks run synchronously on the sole event waiter.
    // The private processor's native wake publishes external demand/completions;
    // no guest CPU or event header is accessed from a host audio callback.
}
void EngineAudioOutput::wait(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;
    {
        std::lock_guard lock(s.mutex);auto& c=s.owner(ctx,base,true);s.event(base,c);
        require(ctx.r3.u32==c.event && ctx.r4.u32==0xFFFFFFFF && !ctx.r5.u32,
                "Dac0 wait arguments differ from the original worker");
    }
    for(;;) {
        s.runtime.checkRunning();const auto snapshot=s.drain(ctx,base);
        require(!snapshot.stopped && !snapshot.stopping,"Dac0 worker woke after native processing stopped");
        {
            std::lock_guard lock(s.mutex);auto& c=*s.construction;s.event(base,c);
            if(PPC_LOAD_U32(c.event+4)) {PPC_STORE_U32(c.event+4,0);ctx.r3.u64=0;ctx.lr=0x82346130;return;}
        }
        HANDLE handles[]={s.processor->wakeHandle(),s.runtime.stopEvent};
        const DWORD status=WaitForMultipleObjects(2,handles,FALSE,INFINITE);
        if(status==WAIT_OBJECT_0+1) s.runtime.checkRunning();
        require(status==WAIT_OBJECT_0,"Dac0 native wake wait failed");
    }
}
void EngineAudioOutput::capacity(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;
    {std::lock_guard lock(s.mutex);auto& c=s.owner(ctx,base,true);
     require(ctx.r3.u32==c.id && ctx.r4.u32==ctx.r1.u32+0x50,"Dac0 capacity arguments changed");}
    s.drain(ctx,base);
    std::lock_guard lock(s.mutex);const auto snapshot=s.processor->poll();s.checked(snapshot);
    require(snapshot.active && !snapshot.stopped && snapshot.owned<=2,"Invalid Dac0 native capacity state");
    PPC_STORE_U8(ctx.r4.u32,snapshot.owned<2?0x20:0);ctx.r3.u64=0;ctx.lr=0x823461F8;
}
void EngineAudioOutput::submit(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);auto& c=s.owner(ctx,base,true);
    const uint32_t slot=PPC_LOAD_U32(c.owner+0x3044);
    require(slot<2 && ctx.r3.u32==c.id && !ctx.r5.u32 && ctx.r4.u32==c.owner+0x3050+0x58*slot &&
            !c.receipts[slot].generation,"Dac0 source submission arguments/cursor changed");
    require(PPC_LOAD_U8(0x82E31F9D)==6 && PPC_LOAD_U8(0x82E31F9F)==1 && PPC_LOAD_U8(0x82E31FA0)==1,
            "Dac0 source mode differs from the six-channel running profile");
    const uint32_t record=ctx.r4.u32,pcm=c.owner+0x44+0x1800*slot,completion=c.owner+0x3100+4*slot;
    require(PPC_LOAD_U32(record)==pcm && PPC_LOAD_U32(record+4)==0x1800 && PPC_LOAD_U32(record+0x54)==completion &&
            PPC_LOAD_U32(completion)==2,"Dac0 source packet differs from its original CPU-owned buffer");
    for(uint32_t offset=8;offset<0x54;offset+=4) require(!PPC_LOAD_U32(record+offset),"Unsupported Dac0 packet looping/control field");
    auto block=Audio::DacPcmBlock::copyBigEndian({s.runtime.pointer(pcm,0x1800,false),0x1800});
    const auto submitted=s.processor->submit(block.pcm);
    require(submitted.status==Audio::DacProcessor::SubmitStatus::Accepted,"Dac0 source lost its previously observed packet capacity");
    c.receipts[slot]=submitted.receipt;++c.submitted;ctx.r3.u64=0;ctx.lr=0x82346234;
}
void EngineAudioOutput::gain(PPCContext& ctx,uint8_t* base,bool enable) {
    auto& s=*state;std::lock_guard lock(s.mutex);auto& c=s.owner(ctx,base);
    require(ctx.r3.u32==c.id && ctx.f1.u64==(enable?0x3FF0000000000000ull:0ull),"Dac0 gain differs from original enable/disable policy");
    // Requested engine gain remains 0/1. Windows session/endpoint volume is the
    // explicit PC platform policy; no console category values/imports are faked.
    s.processor->setTargetGain(enable?1.0f:0.0f);ctx.r3.u64=0;if(enable) ctx.lr=0x82345EB4;
}
void EngineAudioOutput::release(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;
    {std::lock_guard lock(s.mutex);auto& c=s.owner(ctx,base,true);
     require(ctx.r3.u32==c.id && !PPC_LOAD_U8(0x82E31FA0) && !c.mixerCpu,"Dac0 source release did not follow original worker stop");}
    s.processor->stop();s.drain(ctx,base,true);
    std::lock_guard lock(s.mutex);auto& c=*s.construction;
    require(!c.receipts[0].generation && !c.receipts[1].generation,"Dac0 source release retained an original packet");
    c.active=false;c.released=true;ctx.r3.u64=0;ctx.lr=0x82345A8C;
    std::fprintf(stderr,"[NATIVE AUDIO] Dac0 source released on original worker=%u owner=%08X; submitted=%llu consumed=%llu cancelled=%llu downstream_retired=%llu\n",
        c.workerId,c.owner,static_cast<unsigned long long>(c.submitted),static_cast<unsigned long long>(c.consumed),
        static_cast<unsigned long long>(c.cancelled),static_cast<unsigned long long>(c.backendRetired));
}
void EngineAudioOutput::engineRelease(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.caller(ctx,base);s.noHardware(base);
    require(s.construction.has_value(),"Missing Dac0 final engine ownership");const auto& c=*s.construction;
    require(c.released && c.workerId==GetCurrentThreadId() && ctx.r31.u32==c.owner &&
            !PPC_LOAD_U32(c.owner+0x40) && !PPC_LOAD_U32(c.owner+0x304C),"Dac0 final engine release precedes original source/event cleanup");
    ctx.r3.u64=0;ctx.lr=0x82345AA0;
}
void EngineAudioOutput::startup(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::unique_lock lock(s.mutex);s.caller(ctx,base,true);
    require(s.construction && s.construction->active && PPC_LOAD_U32(ctx.r31.u32+0x348)==s.construction->root,
            "Dac0 startup publication does not match the original root");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!s.construction->worker) {
        require(s.changed.wait_until(lock,deadline)!=std::cv_status::timeout,"Original Dac0 worker did not enter after startup release");
        s.runtime.checkRunning();
    }
    const auto& c=*s.construction;
    std::fprintf(stderr,"[NATIVE AUDIO] original Dac0 startup released root=%08X owner=%08X worker=%u; graph and source active\n",c.root,c.owner,c.workerId);
}
void EngineAudioOutput::rootJoin(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::shared_ptr<KernelHandle> worker;
    {
        std::lock_guard lock(s.mutex);s.caller(ctx,base,true);
        if(!s.construction) return;auto& c=*s.construction;
        require(ctx.r31.u32==c.root && c.released && c.worker && !c.joined,"Original Dac0 root join precedes graph/worker release");
        worker=c.worker;
    }
    HANDLE handles[]={worker->native,s.runtime.stopEvent};
    const DWORD status=WaitForMultipleObjects(2,handles,FALSE,30000);
    if(status==WAIT_OBJECT_0+1) s.runtime.checkRunning();
    require(status==WAIT_OBJECT_0,"Original Dac0 OS worker did not join at outer root teardown");
    std::lock_guard lock(s.mutex);auto& c=*s.construction;
    require(c.workerReturned,"Original Dac0 worker exited without its verified Q48/CPU epilogue");
    c.joined=true;HostFloatingPoint fp;s.processor.reset();s.native.reset();
    if(s.apartment) {CoUninitialize();s.apartment=false;}
    std::fprintf(stderr,"[NATIVE AUDIO] original Dac0 worker joined root=%08X native_thread=%u before root storage release\n",c.root,c.workerId);
}
void EngineAudioOutput::rootFinish(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.caller(ctx,base,true);
    if(!s.construction) return;const auto& c=*s.construction;
    require(c.joined && c.root==ctx.r31.u32 && !PPC_LOAD_U32(0x82E31BCC) && !s.processor && !s.native && !s.apartment,
            "Dac0 native retirement did not precede original root free");
    std::fprintf(stderr,"[NATIVE AUDIO] original audio root freed; Dac0 native identity=%08X retired\n",c.id);
    s.construction.reset();
}
void EngineAudioOutput::mixerReady(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);auto& c=s.owner(ctx,base);
    require(c.active && c.workerId==GetCurrentThreadId() && !c.mixerCpu && ctx.r29.u32==c.mixer,
            "Original mixer initialization lacks its active worker/allocation");
    // 82354178..B4 initializes three original image-resident descriptors with
    // the three 64-KiB planes of the actual 0x30080-byte mixer allocation.
    for(uint32_t i=0;i<3;++i) {
        const uint32_t d=0x82E33AA0+0x114*i;
        s.runtime.pointer(d,0x114,false);
        require(PPC_LOAD_U32(d)==c.root && PPC_LOAD_U32(d+4)==c.mixer+0x10000*i &&
                PPC_LOAD_U16(d+0xC)==0 && PPC_LOAD_U16(d+0xE)==256 && PPC_LOAD_U8(d+0x10)==64 &&
                PPC_LOAD_U32(c.mixer+0x3000C+4*i)==d,
                "Original mixer scratch descriptor initialization changed");
    }
    c.mixerCpu=&ctx;c.mixerSp=ctx.r1.u32;
}
void EngineAudioOutput::mixerEnd(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;std::lock_guard lock(s.mutex);auto& c=s.owner(ctx,base);
    require(c.mixerCpu==&ctx && c.mixerSp==ctx.r1.u32 && c.workerId==GetCurrentThreadId() && ctx.r29.u32==c.mixer,
            "Original mixer pass returned outside its lifetime scope");
    c.mixerCpu=nullptr;c.mixerSp=0;
}
EngineAudioOutput::PcmLease EngineAudioOutput::pcm(PPCContext& ctx,uint8_t* base,uint32_t root,uint32_t d,uint32_t channels) {
    auto& s=*state;std::unique_lock lock(s.mutex);auto& c=s.owner(ctx,base);
    require(c.active && c.root==root && c.workerId==GetCurrentThreadId() && c.mixerCpu==&ctx &&
            ctx.r1.u32<c.mixerSp && channels>0 && channels<=6,
            "EXm0 output is outside the original worker mixer lifetime");
    require(d>=0x82E33AA0 && (d-0x82E33AA0)%0x114==0 && (d-0x82E33AA0)/0x114<3,
            "EXm0 output descriptor is not an original mixer scratch descriptor");
    const uint32_t slot=(d-0x82E33AA0)/0x114;
    s.runtime.pointer(d,0x114,false);
    require(PPC_LOAD_U32(d)==c.root && PPC_LOAD_U32(d+4)==c.mixer+0x10000*slot &&
            PPC_LOAD_U16(d+0xE)==256 && PPC_LOAD_U8(d+0x10)==64 &&
            PPC_LOAD_U32(c.mixer+0x30008)==root && PPC_LOAD_U32(c.owner+0x24)==c.mixer &&
            PPC_LOAD_U32(c.mixer+0x30010)==d,
            "EXm0 output scratch allocation/descriptor identity changed");
    auto* bytes=s.runtime.pointer(c.mixer+0x10000*slot,4*256*channels,true);
    return {std::move(lock),{bytes,4*256*channels}};
}
EngineAudioOutput::View EngineAudioOutput::view() const {
    auto& s=*state;std::lock_guard lock(s.mutex);require(s.construction.has_value(),"Dac0 output has no owner");
    const auto& c=*s.construction;
    const auto endpoint=s.native?s.native->endpoint():Audio::NativeAudioOutput::Endpoint{};
    return {c.owner,c.root,c.mixer,s.native?s.native->generation():0,endpoint.channels,endpoint.channelMask,bool(s.native),endpoint.muted,
        c.id,c.event,c.workerId,c.configured,c.active,c.released,c.workerReturned,c.joined,
        c.submitted,c.consumed,c.cancelled,c.passes,c.backendRetired};
}
}

namespace {
std::shared_ptr<Simpsons::EngineAudioOutput> output(uint8_t* base,bool create=false) {
    if(!Simpsons::active || base!=Simpsons::active->base) throw Simpsons::Failure("Invalid Dac0 bridge runtime");
    auto& rt=*Simpsons::active;rt.checkRunning();std::lock_guard lock(rt.audioMutex);
    if(create && !rt.engineAudioOutput) rt.engineAudioOutput=std::make_shared<Simpsons::EngineAudioOutput>(rt);
    if(!rt.engineAudioOutput) throw Simpsons::Failure("Missing native Dac0 output owner");
    return rt.engineAudioOutput;
}
}
void SimpsonsNativeAudioOutputBegin(PPCContext& ctx,uint8_t* base){output(base,true)->begin(ctx,base);}
void SimpsonsNativeAudioOutputAcquire(PPCContext& ctx,uint8_t* base){output(base)->acquire(ctx,base);}
void SimpsonsNativeAudioMixerReady(PPCContext& ctx,uint8_t* base){output(base)->mixerReady(ctx,base);}
void SimpsonsNativeAudioMixerEnd(PPCContext& ctx,uint8_t* base){output(base)->mixerEnd(ctx,base);}
void SimpsonsNativeAudioSource(PPCContext& ctx,uint8_t* base){
    output(base)->source(ctx,base);
    if(Simpsons::active->audioBoundaryObserver) Simpsons::active->audioBoundaryObserver(0x82345920,ctx,base);
}
void SimpsonsNativeAudioActivate(PPCContext& ctx,uint8_t* base){output(base)->activate(ctx,base);}
void SimpsonsNativeAudioWorkerBegin(PPCContext& ctx,uint8_t* base){output(base)->workerBegin(ctx,base);}
void SimpsonsNativeAudioWorkerEnd(PPCContext& ctx,uint8_t* base){output(base)->workerEnd(ctx,base);}
void SimpsonsNativeAudioWait(PPCContext& ctx,uint8_t* base){output(base)->wait(ctx,base);}
void SimpsonsNativeAudioCapacity(PPCContext& ctx,uint8_t* base){output(base)->capacity(ctx,base);}
void SimpsonsNativeAudioSubmit(PPCContext& ctx,uint8_t* base){output(base)->submit(ctx,base);}
void SimpsonsNativeAudioGainEnable(PPCContext& ctx,uint8_t* base){output(base)->gain(ctx,base,true);}
void SimpsonsNativeAudioGainDisable(PPCContext& ctx,uint8_t* base){output(base)->gain(ctx,base,false);}
void SimpsonsNativeAudioSignal(PPCContext& ctx,uint8_t* base){output(base)->signal(ctx,base);}
void SimpsonsNativeAudioSourceRelease(PPCContext& ctx,uint8_t* base){output(base)->release(ctx,base);}
void SimpsonsNativeAudioEngineRelease(PPCContext& ctx,uint8_t* base){output(base)->engineRelease(ctx,base);}
void SimpsonsNativeAudioStartup(PPCContext& ctx,uint8_t* base){
    output(base)->startup(ctx,base);
    if(Simpsons::active->audioBoundaryObserver) Simpsons::active->audioBoundaryObserver(0x828166FC,ctx,base);
}
void SimpsonsNativeAudioRootJoin(PPCContext& ctx,uint8_t* base){
    if(Simpsons::active && Simpsons::active->engineAudioOutput) output(base)->rootJoin(ctx,base);
}
void SimpsonsNativeAudioRootFinish(PPCContext& ctx,uint8_t* base){
    if(Simpsons::active && Simpsons::active->engineAudioOutput) output(base)->rootFinish(ctx,base);
}
