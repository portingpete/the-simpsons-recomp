// Actual MUS owner: original file load, selector-0 request, mixer use, stop and retirement.
#include "runtime/engine_audio.h"
#include "runtime/engine_audio_output.h"
#include "runtime/engine_audio_reader.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/threads.h"
#include "audio/audio_catalog.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <bcrypt.h>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
extern "C" void __imp__sub_823424D8(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8238D640(PPCContext&,uint8_t*);
extern "C" void __imp__sub_82B75498(PPCContext&,uint8_t*);
extern "C" void __imp__sub_82432A08(PPCContext&,uint8_t*);

namespace {
using namespace Simpsons;
std::atomic<size_t> checks{};
void need(bool value,const char* why){++checks;if(!value)throw Failure(why);}
struct StartupObserved{};
uint32_t be32(const uint8_t* p){return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];}
std::string hash(std::span<const uint8_t> bytes){
    need(bytes.size()<=ULONG_MAX,"MUS hash extent overflow");std::array<uint8_t,32> digest{};
    need(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),
                    digest.data(),ULONG(digest.size()))>=0,"MUS hash failed");
    std::string result;for(auto b:digest){result.push_back("0123456789abcdef"[b>>4]);result.push_back("0123456789abcdef"[b&15]);}return result;
}
struct Abi {
    uint64_t sp,lr;std::array<uint64_t,18> g{},f{};
    explicit Abi(const PPCContext& c):sp(c.r1.u64),lr(c.lr){
        const auto gs=std::array{c.r14,c.r15,c.r16,c.r17,c.r18,c.r19,c.r20,c.r21,c.r22,c.r23,c.r24,c.r25,c.r26,c.r27,c.r28,c.r29,c.r30,c.r31};
        const auto fs=std::array{c.f14,c.f15,c.f16,c.f17,c.f18,c.f19,c.f20,c.f21,c.f22,c.f23,c.f24,c.f25,c.f26,c.f27,c.f28,c.f29,c.f30,c.f31};
        for(size_t i=0;i<18;++i){g[i]=gs[i].u64;f[i]=fs[i].u64;}
    }
    void verify(const PPCContext& c)const{const Abi after(c);need(after.sp==sp&&after.lr==lr&&after.g==g&&after.f==f,"Configured MUS changed nonvolatile ABI");}
};
struct Lock {
    EngineCpuCalls& cpu;uint32_t q;
    Lock(EngineCpuCalls& c,uint32_t root):cpu(c),q(root){need(cpu.invoke(0x823392C8,q)==0,"MUS Q lock failed");}
    ~Lock(){try{need(cpu.invoke(0x823392F0,q)==1,"MUS Q unlock failed");}catch(...){active->requestStop("MUS Q unwind failed");}}
};
template<class F> void waitFor(Runtime& rt,F&& f,const char* why,unsigned seconds=8){
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
    for(;;){rt.checkRunning();if(f())return;need(std::chrono::steady_clock::now()<limit,why);Sleep(10);}
}
void pins(Runtime& rt){
    constexpr std::pair<uint32_t,uint32_t> expected[]={
        {0x82334284,0x2B040000},{0x823342CC,0x4E800421},{0x823342DC,0x997F0158},
        {0x823351A4,0x2F1C0000},{0x823351B4,0x4BFECB1D},{0x823352B4,0x4BFECEED},
        {0x8233436C,0x4E800421},{0x82334378,0x93DF0154},
        {0x82334410,0x812A000C},{0x82334420,0x81290008},{0x82334440,0x4199FFE0},
        {0x82334634,0xC809D410},{0x82334670,0x4E800421},{0x823347C0,0x38800001},
        {0x823347DC,0x4E800421},{0x82341B68,0x90EA0000},{0x82333F90,0x4E800421},
        {0x82333FC0,0x90E90000},{0x82330DEC,0x90E90000},{0x821DD410,0x3F50624D},
        {0x821DD414,0xD2F1A9FC},{0x821DD1E8,0},{0x821DD1EC,0},{0x821B5F30,0x643A5C61},
        {0x82334504,0x815F0064},{0x8233450C,0x80AA000C},{0x82334514,0x4E800421},
        {0x8232ED88,0x90830064},{0x8232ED8C,0x4E800020},
    };for(const auto& [pc,w]:expected)need(PPCLoadU32(rt.base,pc)==w,"Configured MUS source instruction changed");
}

// All wrappers forward the existing real AOT/native entry. None returns a
// replacement pointer, EOF, message or successful result. The only deliberate
// invalid invocation is a before-admission negative probe inside the real
// producer; it restores the actual CPU context before forwarding that call.
class Observation {
public:
    Runtime& rt;static Observation* current;
    std::array<uint32_t,8> addresses{0x82341B20,0x823424D8,0x8233FAF8,0x8238D640,0x82334CA0,0x8233D980,0x82342268,0x82341F78};
    std::array<PPCFunc*,8> bodies{};
    static void configure(PPCContext& c,uint8_t* base){auto& s=*current;
        if(c.r4.u32==0){
            need(c.lr==0x82334674,"MUS configuration bypassed whole original voice producer");
            const uint32_t input=c.r5.u32;
            need(PPCLoadU64(base,input)==0x3F50624DD2F1A9FCull&&PPCLoadU64(base,input+8)==std::bit_cast<uint64_t>(double(s.audioOffset)),
                 "Original ordinary MUS time/file base changed");
            need(PPCLoadU32(base,input+0x14)==s.metadata.load()+s.headerOffset,"Original MUS selected a different header record");
            need(!std::memcmp(s.rt.pointer(PPCLoadU32(base,input+0x10),128,false),"d:\\audiostreams\\menu_mus.mus",28),"Original MUS filename changed");
            s.member.store(c.r3.u32);++s.configurations;
        }else if(c.r4.u32==1&&c.r3.u32==s.member.load()){need(c.lr==0x823347E0,"MUS stop bypassed original voice method");++s.stopCommands;}
        s.bodies[0](c,base);
    }
    static void producer(PPCContext& c,uint8_t* base){auto& s=*current;
        if(!s.producerCalls.load()){
            const auto saved=c;const auto before=audioReaders(s.rt)->snapshot();bool rejected=false;
            c.lr=0x823424D4;
            try{s.rt.engineAudio->producerBegin(c,base);}catch(const Failure&){rejected=true;}
            c=saved;const auto after=audioReaders(s.rt)->snapshot();
            need(rejected&&before.groups==after.groups&&before.managers==after.managers&&before.claims==after.claims&&before.copies==after.copies&&before.operations==after.operations,
                 "Malformed original producer caller mutated ownership or was accepted");
        }
        __imp__sub_823424D8(c,base);++s.producerCalls;
    }
    static void decode(PPCContext& c,uint8_t* base){auto& s=*current;const uint32_t frames=c.r5.u32;
        if(frames)need(c.lr==0x823628E4,"MUS output did not come from the original unbuffered mixer");
        s.codec.store(c.r3.u32);s.bodies[2](c,base);s.decoded.fetch_add(frames);
    }
    static void readerFree(PPCContext& c,uint8_t* base){auto& s=*current;const uint32_t caller=uint32_t(c.lr);
        __imp__sub_8238D640(c,base);if(caller==0x82342D60)++s.cancelledReleases;else if(caller==0x82341880)++s.completedReleases;
    }
    static void allocationFree(PPCContext& c,uint8_t* base){auto& s=*current;const uint32_t address=c.r3.u32;
        s.bodies[4](c,base);if(address&&address==s.metadata.load())++s.metadataFrees;
    }
    static void readerRetire(PPCContext& c,uint8_t* base){auto& s=*current;
        const bool ours=PPCLoadU32(base,c.r3.u32+4)==s.group.load();s.bodies[5](c,base);if(ours)++s.readerRetirements;
    }
    static void stop(PPCContext& c,uint8_t* base){auto& s=*current;s.bodies[6](c,base);++s.stopExecutions;}
    static void command(PPCContext& c,uint8_t* base){auto& s=*current;const uint32_t request=c.r3.u32;
        need(PPCLoadU32(base,request+4)==s.member.load()&&PPCLoadU64(base,request+0x18)==0&&
             !PPCLoadU32(base,request+0x28),"Original ordinary MUS command acquired a seek time or optional metadata");
        s.bodies[7](c,base);++s.setupExecutions;
    }
public:
    std::atomic<uint32_t> metadata{},member{},codec{},group{};
    std::atomic<uint64_t> configurations{},producerCalls{},decoded{},cancelledReleases{},completedReleases{},metadataFrees{},readerRetirements{},stopCommands{},stopExecutions{},setupExecutions{};
    uint32_t headerOffset,audioOffset;
    Observation(Runtime& value,uint32_t header,uint32_t audio):rt(value),headerOffset(header),audioOffset(audio){
        need(!current,"Configured MUS observer is already installed");current=this;
        const std::array<PPCFunc*,8> wrappers={configure,producer,decode,readerFree,allocationFree,readerRetire,stop,command};
        for(size_t i=0;i<addresses.size();++i){bodies[i]=PPC_LOOKUP_FUNC(rt.base,addresses[i]);need(bodies[i]!=nullptr,"Original MUS observed entry is missing");PPC_LOOKUP_FUNC(rt.base,addresses[i])=wrappers[i];}
    }
    ~Observation(){rt.stopThreads();for(size_t i=0;i<addresses.size();++i)PPC_LOOKUP_FUNC(rt.base,addresses[i])=bodies[i];current=nullptr;}
};
Observation* Observation::current{};
}
PPC_FUNC(sub_823424D8){if(Observation::current)Observation::producer(ctx,base);else __imp__sub_823424D8(ctx,base);}
PPC_FUNC(sub_8238D640){if(Observation::current)Observation::readerFree(ctx,base);else __imp__sub_8238D640(ctx,base);}
PPC_FUNC(sub_82B75498){
    const auto before=ctx;__imp__sub_82B75498(ctx,base);
    if(Observation::current)std::fprintf(stderr,"[CONFIGURED MUS READ] caller=%08X handle=%08X buffer=%08X bytes=%u result=%u completed=%u ios=%08X\n",
        uint32_t(before.lr),before.r3.u32,before.r4.u32,before.r5.u32,ctx.r3.u32,
        before.r6.u32?PPCLoadU32(base,before.r6.u32):0,PPCLoadU32(base,before.r1.u32-64));
}
PPC_FUNC(sub_82432A08){
    if(Observation::current)std::fprintf(stderr,"[CONFIGURED MUS DIRTY DISC] original caller=%08X value=%08X\n",uint32_t(ctx.lr),ctx.r3.u32);
    __imp__sub_82432A08(ctx,base);
}

int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    std::setvbuf(stdout,nullptr,_IONBF,0);std::setvbuf(stderr,nullptr,_IONBF,0);
    try{
        need(argc==3,"Original image and independent configured MUS case required");size_t caseIndex=0;
        const char* end=argv[2]+std::strlen(argv[2]);const auto parsed=std::from_chars(argv[2],end,caseIndex);
        need(parsed.ec==std::errc{}&&parsed.ptr==end&&caseIndex<2,"Invalid configured MUS case");
        Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);pins(rt);const auto entry=original;
        const auto source=readFile(rt.gameRoot/"audiostreams/menu_mus.mus");
        need(source.size()==32950016&&hash(source)=="3154127815784c0de9331e1b69748ff47b2e5e70de012296fedb1aace9a31dda","Configured MUS original payload changed");
        const auto descriptorSource=readFile(rt.gameRoot.parent_path()/"build/restrictive-audit/menu_mus.msx");
        need(descriptorSource.size()==4766&&hash(descriptorSource)=="3fa1abe6b4a8eac02d44191e42900fc672b85cbfe1619e8f128d05c8c13944e9",
             "Configured MUS authored descriptor extraction changed; prepare the exact original frontend entry 30");
        constexpr uint32_t body=0x50;
        need(!std::memcmp(descriptorSource.data()+body,"PFDx\x05\x03",6)&&descriptorSource[body+13]==1,
             "Original menu MUS descriptor owner changed");
        const uint32_t descriptorTable=be32(descriptorSource.data()+body+0x2C);
        need(descriptorTable==0xDBC,"Original MUS descriptor table changed");
        const uint32_t descriptorOffset=4*be32(descriptorSource.data()+body+descriptorTable);
        need(descriptorOffset==0xDC0&&be32(descriptorSource.data()+body+descriptorOffset+8)==be32(source.data())&&
             be32(descriptorSource.data()+body+descriptorOffset+12)==0x750,
             "Original MUS descriptor does not identify this metadata bank");
        const uint32_t record=40+28*uint32_t(caseIndex),header=16*be32(source.data()+record+8),audio=128*be32(source.data()+record+12);
        const uint32_t selector=caseIndex?be32(source.data()+40+8)+1:0;
        const auto catalog=Audio::AudioCatalog::load(rt.gameRoot.parent_path()/"analysis/audio_catalog.bin");
        const auto* certificate=catalog.findHeader(std::span(source).subspan(header,8));
        need(certificate&&!certificate->loop&&certificate->channels==6&&certificate->sampleRate==48000,
             "Configured MUS record lacks its exact existing source certificate");
        const uint32_t audioBytes=be32(source.data()+record+20),frames=be32(source.data()+header+4)&0x1FFFFFFF;
        uint32_t blocks=0,samples=0,at=audio;
        while(at<audio+audioBytes){const uint32_t length=be32(source.data()+at)&0xFFFFFF;
            need(length>=8&&uint64_t(at)+length<=uint64_t(audio)+audioBytes,"Configured MUS original block extent changed");
            samples+=be32(source.data()+at+4);at+=length;++blocks;}
        need(at==audio+audioBytes&&samples==frames&&certificate->frames==frames,"Configured MUS source block/frame extent changed");
        bool started=false;uint32_t manager=0;
        rt.audioBoundaryObserver=[&](uint32_t pc,PPCContext& c,uint8_t*){
            if(pc!=0x828166FC)return;const auto view=rt.engineAudioOutput->view();manager=c.r31.u32;
            need(view.nativeEngine&&view.configured&&view.active&&view.workerId&&manager&&PPCLoadU32(rt.base,manager+0x348)==view.root,"Original configured MUS startup missing");throw StartupObserved{};
        };
        try{runOriginal(original,rt.base);}catch(const StartupObserved&){started=true;}rt.audioBoundaryObserver={};need(started,"Original MUS audio startup did not complete");
        const auto output=rt.engineAudioOutput->view();std::shared_ptr<KernelHandle> worker;
        {std::lock_guard lock(rt.threadMutex);for(const auto& t:rt.threads)if(t->id==output.workerId)worker=t->object;}
        need(worker&&GetThreadId(worker->native)==output.workerId,"Configured MUS original Dac worker missing");
        EngineCpuCalls cpu(entry,rt.base);const Abi abi(cpu.registers());const auto readers=audioReaders(rt,true);
        need(cpu.invoke(0x8280FD70,manager)==1,"Original MUS service initialization failed");abi.verify(cpu.registers());
        const uint32_t inputs=rt.allocatePhysical(0,8192,PAGE_READWRITE,0,std::numeric_limits<uint32_t>::max(),4096);need(inputs!=0,"MUS fixture inputs unavailable");
        std::memset(rt.pointer(inputs,8192,true),0,8192);std::memcpy(rt.pointer(inputs,128,true),"d:\\audiostreams\\menu_mus.mus",28);
        std::memcpy(rt.pointer(inputs+512,uint32_t(descriptorSource.size()),true),descriptorSource.data(),descriptorSource.size());
        need(cpu.invoke(0x82321CD0,inputs)==source.size(),"Original file provider could not measure the configured MUS bank");abi.verify(cpu.registers());
        Observation observe(rt,header,audio);uint32_t voice=0,metadataBytes=0;
        {Lock lock(cpu,output.root);cpu.registers().f1.f64=double(float(0x47E00)/48000.0f);
            need(cpu.invoke(0x82330540,0,inputs+256,4,0,0)==0,"Original configured stream factory failed");abi.verify(cpu.registers());
            voice=PPCLoadU32(rt.base,inputs+256);need(voice&&PPCLoadU32(rt.base,voice)==0x821DC850,"Original configured MUS voice missing");
            observe.group.store(PPCLoadU32(rt.base,voice+0x148));need(observe.group.load()!=0,"Original configured MUS has no reader group");
            need(!PPCLoadU32(rt.base,voice+0x64),"Standalone MUS factory unexpectedly supplied a descriptor");
            cpu.invoke(0x8232ED88,voice,inputs+512+body+descriptorOffset);abi.verify(cpu.registers());
            cpu.invoke(0x82334870,voice,inputs);abi.verify(cpu.registers());cpu.registers().r8.u32=0;
            need(int32_t(cpu.invoke(0x82334468,voice,0,selector,0,0))==-9999,"Original MUS did not request its descriptor-sized metadata before playback");abi.verify(cpu.registers());
            metadataBytes=PPCLoadU32(rt.base,voice+0x150);
            need(metadataBytes==PPCLoadU32(rt.base,PPCLoadU32(rt.base,voice+0x64)+0xC)&&
                 metadataBytes<=source.size()&&uint64_t(header)+8<=metadataBytes,"Original descriptor does not own the selected MUS header extent");
            observe.metadata.store(PPCLoadU32(rt.base,voice+0x14C));need(observe.metadata.load()&&PPCLoadU8(rt.base,voice+0x158)==1,"Original loader did not own its MUS metadata allocation");
        }
        waitFor(rt,[&]{Lock lock(cpu,output.root);cpu.invoke(0x82334328,voice);abi.verify(cpu.registers());return PPCLoadU32(rt.base,voice+0x154)==0xBEDFACED;},"Original MUS metadata file request never completed");
        need(!std::memcmp(rt.pointer(observe.metadata.load(),metadataBytes,false),source.data(),metadataBytes),"Original loader did not read the exact unmodified MUS metadata extent");
        {Lock lock(cpu,output.root);cpu.registers().r8.u32=0;const uint32_t result=cpu.invoke(0x82334468,voice,0,selector,0,0);abi.verify(cpu.registers());
            need(int32_t(result)>=0&&observe.configurations.load()==1&&observe.member.load(),"Original MUS table/setup path failed");}
        waitFor(rt,[&]{return observe.setupExecutions.load()==1&&observe.producerCalls.load()&&observe.decoded.load()>=1024;},"Original configured MUS did not request and decode real PCM");
        need(rt.engineAudioOutput->view().submitted>output.submitted,"Original configured MUS never submitted a real mixer block");
        const uint32_t codec=observe.codec.load();const auto codecView=rt.engineAudio->view(codec);const auto lease=rt.engineAudio->lease(codec,codecView.generation);
        if(caseIndex==1){
            waitFor(rt,[&]{return observe.completedReleases.load()>=blocks&&observe.decoded.load()+384>=frames;},"Short authored MUS did not reach actual decode/reader completion",12);
        }
        {Lock lock(cpu,output.root);need(cpu.invoke(0x82334778,voice)==0,"Original configured MUS stop failed");abi.verify(cpu.registers());}
        waitFor(rt,[&]{return observe.stopCommands.load()==1&&observe.stopExecutions.load()&&rt.engineAudio->count()==0;},"Original MUS worker did not execute stop and retire its configured codec");
        need(lease.expired(),"Configured MUS codec survived actual original stop");
        if(caseIndex==0)need(observe.cancelledReleases.load()>0,"Live MUS stop did not join/release a genuine pending compressed claim");
        {Lock lock(cpu,output.root);need(uint64_t(PPCLoadU32(rt.base,output.root+0xD0))+16<=PPCLoadU32(rt.base,output.root+0xCC),"Original MUS owner has no retirement command slots");
            cpu.invoke(0x82333EC8,voice,1);abi.verify(cpu.registers());}
        waitFor(rt,[&]{const auto s=readers->snapshot();return !s.groups&&!s.managers&&!s.claims&&!s.operations&&rt.engineAudio->count()==0;},"Configured MUS deferred reader/group retirement did not finish");
        need(observe.metadataFrees.load()==1&&observe.readerRetirements.load()==1,"Actual MUS bank release or deferred reader retirement was not observed");readers->requireStopped();
        need(cpu.invoke(0x8232E068)==1,"Original configured MUS service shutdown failed");abi.verify(cpu.registers());
        need(!PPCLoadU32(rt.base,0x82E36C90)&&!PPCLoadU32(rt.base,0x82E36C94),"Configured MUS services survived shutdown");
        cpu.invoke(0x82338FA0,output.root);DWORD result=~0u;
        need(WaitForSingleObject(worker->native,0)==WAIT_OBJECT_0&&GetExitCodeThread(worker->native,&result)&&result==0,"Configured MUS root failed to join the original Dac worker");
        need(!PPCLoadU32(rt.base,0x82E31BCC)&&!rt.engineAudio->ready(),"Configured MUS root/factory remained live");rt.freePhysical(inputs);
        std::printf("PASS configured original MUS case=%zu: %zu checks; ordinal=%zu header=%X audio=%X selector=%u blocks=%llu decoded=%llu normal-releases=%llu cancel-releases=%llu; original owned file/read/request/mixer/stop/codec free/metadata free/deferred reader retirement/service close/worker join\n",caseIndex,checks.load(),caseIndex,header,audio,selector,
            static_cast<unsigned long long>(observe.producerCalls.load()),static_cast<unsigned long long>(observe.decoded.load()),static_cast<unsigned long long>(observe.completedReleases.load()),static_cast<unsigned long long>(observe.cancelledReleases.load()));return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL configured original MUS: %s\n",e.what());return 1;}
}
