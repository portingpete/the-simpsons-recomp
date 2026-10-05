#include "runtime/runtime.h"
#include "runtime/native_local_players.h"
#include "runtime/engine_cpu_calls.h"
#include "ppc_recomp_shared.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>

PPC_FUNC(__imp__XMsgStartIORequest);
namespace {
namespace fs=std::filesystem;
size_t checks=0;
void need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
struct Temp {
    fs::path parent=fs::canonical(fs::temp_directory_path());
    fs::path root=parent/("SimpsonsAchievementBridge-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    Temp(){need(fs::create_directory(root),"Fixture directory already exists");}
    ~Temp(){if(root.is_absolute() && root.parent_path()==parent && root.filename().wstring().starts_with(L"SimpsonsAchievementBridge-")){
        std::error_code error;fs::remove_all(root,error);}}
};
constexpr uint32_t request=0x11000,records=0x11100,ov=0x12008,owner=0x14000;
size_t countFiles(const fs::path& root){return fs::exists(root)?size_t(std::distance(fs::directory_iterator(root),fs::directory_iterator())):0;}
std::string read(const fs::path& path){std::ifstream in(path,std::ios::binary);need(bool(in),"Cannot read actual achievement");return {std::istreambuf_iterator<char>(in),{}};}
void write(const fs::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<bytes;out.close();need(bool(out),"Cannot write owned corruption fixture");}
struct Fixture {
    Temp temp;Simpsons::Runtime rt;PPCContext entry{};
    std::shared_ptr<Simpsons::Platform::NativeLocalPlayers> players;
    Simpsons::Platform::LocalProfile profile;
    uint32_t eventId{};std::shared_ptr<Simpsons::KernelHandle> event;
    fs::path store=temp.root/"profiles.achievements";
    explicit Fixture(const char* image){
        rt.configureLocalPlayers(temp.root/"profiles");rt.load(image);rt.initialize(entry);rt.map(0x10000,0x10000,true,"achievement bridge fixture");
        rt.map(0x40000,0x1000,false,"achievement readonly output fixture");
        players=rt.localPlayerSource();profile=players->create("Earned");rt.activateLocalPlayer(0,profile.id);
        Simpsons::EngineCpuCalls cpu(entry,rt.base);cpu.invoke(0x827B4E78,owner);
        auto* base=rt.base;eventId=PPC_LOAD_U32(owner+4);event=rt.getHandle(eventId);
        need(event && event->type==Simpsons::KernelHandle::Type::Event,"Original owner did not create an event");
    }
    PPCContext prepare(uint32_t id=7){
        auto* base=rt.base;need(ResetEvent(event->native)!=FALSE,"Fixture event reset failed");
        std::memset(rt.pointer(ov-8,0x2C,true),0xA7,0x2C);
        PPC_STORE_U32(ov,997);PPC_STORE_U32(ov+0xC,eventId);PPC_STORE_U32(ov+0x10,0);PPC_STORE_U32(ov+0x14,0x12345678);
        PPC_STORE_U32(request,1);PPC_STORE_U32(request+4,records);PPC_STORE_U32(records,0);PPC_STORE_U32(records+4,id);
        PPC_STORE_U32(rt.threadAddress+0x160,0xDEADBEEF);
        PPCContext ctx{};std::memset(&ctx,0xA5,sizeof(ctx));ctx.r3.u32=0xFB;ctx.r4.u32=0xB0008;ctx.r5.u32=ov;
        ctx.r6.u32=request;ctx.r7.u32=8;ctx.r13.u32=entry.r13.u32;return ctx;
    }
    void rejected(PPCContext ctx){
        auto* base=rt.base;PPCContext expected;std::memcpy(&expected,&ctx,sizeof(ctx));
        std::array<uint8_t,0x2C> bytes;std::memcpy(bytes.data(),rt.pointer(ov-8,unsigned(bytes.size()),false),bytes.size());
        const auto files=countFiles(store),handles=rt.handles.size();
        std::array<uint8_t,4> error;std::memcpy(error.data(),rt.pointer(rt.threadAddress+0x160,4,false),4);
        bool caught=false;try{__imp__XMsgStartIORequest(ctx,base);}catch(const std::exception&){caught=true;}
        need(caught && !std::memcmp(&ctx,&expected,sizeof(ctx)),"Rejected achievement changed CPU context or returned success");
        need(!std::memcmp(bytes.data(),rt.pointer(ov-8,unsigned(bytes.size()),false),bytes.size()) &&
             !std::memcmp(error.data(),rt.pointer(rt.threadAddress+0x160,4,false),4) && countFiles(store)==files && rt.handles.size()==handles,
             "Rejected request changed completion, durable files or native handles");
        need(WaitForSingleObject(event->native,0)==WAIT_TIMEOUT,"Rejected achievement signalled completion");
    }
};
void importContracts(Fixture& f){
    auto* base=f.rt.base;const auto saved=PPCFPSCRRegister::getcsr();
    struct Restore{uint32_t fp;~Restore(){PPCFPSCRRegister::restoreHostCSR(fp);}} restore{saved};
    for(uint32_t fp:{0x1F80u,0x3FC0u,0x5F80u,0x9FC0u,0xE07Fu}){
        auto ctx=f.prepare();PPCContext expected;std::memcpy(&expected,&ctx,sizeof(ctx));expected.r3.u64=997;
        PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(0x456789);__imp__XMsgStartIORequest(ctx,base);
        const auto actualFP=PPCFPSCRRegister::getcsr();const auto error=GetLastError();PPCFPSCRRegister::restoreHostCSR(saved);
        need(!std::memcmp(&ctx,&expected,sizeof(ctx)) && actualFP==fp && error==0x456789,"Achievement import changed CPU/host controls");
        need(PPC_LOAD_U32(ov)==0 && PPC_LOAD_U32(ov+4)==0 && PPC_LOAD_U32(ov+0x18)==0 &&
             PPC_LOAD_U32(ov+0xC)==f.eventId && PPC_LOAD_U32(ov+0x10)==0 && PPC_LOAD_U32(ov+0x14)==0x12345678 &&
             PPC_LOAD_U64(ov-8)==0xA7A7A7A7A7A7A7A7ull && PPC_LOAD_U64(ov+0x1C)==0xA7A7A7A7A7A7A7A7ull,
             "Achievement completion ABI or extent differs");
        need(f.rt.getHandle(PPC_LOAD_U32(ov+8))==f.rt.mainThreadHandle && PPC_LOAD_U32(f.rt.threadAddress+0x160)==0,
             "Completion did not identify the actual calling thread or clear its last error");
        need(WaitForSingleObject(f.event->native,0)==WAIT_OBJECT_0 && WaitForSingleObject(f.event->native,0)==WAIT_TIMEOUT,
             "Successful durable request did not signal its actual auto-reset event");
        need(f.players->hasAchievement(f.profile.id,0x45410809,7) && countFiles(f.store)==1,"Repeated completion lacks durable idempotent unlock");
    }
    const auto file=f.store/(f.profile.id+"-45410809-00000007.achievement");const auto original=read(file);
    auto ctx=f.prepare();ctx.r3.u32=0xFF;f.rejected(ctx);
    ctx=f.prepare();ctx.r4.u32=0xB0007;f.rejected(ctx);
    ctx=f.prepare();ctx.r7.u32=4;f.rejected(ctx);
    ctx=f.prepare();ctx.r5.u32=0x40000;f.rejected(ctx);
    ctx=f.prepare();PPC_STORE_U32(ov+0x10,0x82000000);f.rejected(ctx);
    ctx=f.prepare();PPC_STORE_U32(ov+0xC,PPC_LOAD_U32(ov+8));f.rejected(ctx);
    ctx=f.prepare();PPC_STORE_U32(request,26);f.rejected(ctx);
    ctx=f.prepare();PPC_STORE_U32(request+4,0x50000);f.rejected(ctx);
    ctx=f.prepare();PPC_STORE_U32(records,1);f.rejected(ctx);
    ctx=f.prepare();PPC_STORE_U32(request+4,ov);f.rejected(ctx);
    ctx=f.prepare();PPC_STORE_U32(f.entry.r13.u32+0x150,1);f.rejected(ctx);PPC_STORE_U32(f.entry.r13.u32+0x150,0);
    write(file,"corrupt");ctx=f.prepare();f.rejected(ctx);need(read(file)=="corrupt","Rejected request repaired a corrupt achievement");write(file,original);
    need(f.players->hasAchievement(f.profile.id,0x45410809,7),"Corruption fixture restore failed");
}
void originalRoundTrip(Fixture& f){
    auto* base=f.rt.base;PPC_STORE_U32(records,0);PPC_STORE_U32(records+4,8);PPC_STORE_U32(records+8,0);PPC_STORE_U32(records+12,9);
    need(ResetEvent(f.event->native)!=FALSE,"Original round-trip event reset failed");
    std::exception_ptr error;
    std::jthread observer([&]{try{
        if(WaitForSingleObject(f.event->native,2000)!=WAIT_OBJECT_0)throw std::runtime_error("Durable write did not signal within bound");
        Simpsons::Platform::NativeLocalPlayers reopened(f.temp.root/"profiles");
        if(!reopened.hasAchievement(f.profile.id,0x45410809,8) || !reopened.hasAchievement(f.profile.id,0x45410809,9))
            throw std::runtime_error("Event was signalled before all achievement records could be reopened");
    }catch(...){error=std::current_exception();}});
    Simpsons::EngineCpuCalls cpu(f.entry,base);const auto stack=cpu.registers().r1.u64;
    need(cpu.invoke(0x824316A8,2,records,owner+8)==997,"Original achievement writer return differs");
    observer.join();if(error)std::rethrow_exception(error);
    need(cpu.invoke(0x827B4DF8,owner,0)==0,"Original completion poll did not observe successful persistence");
    const auto completedId=PPC_LOAD_U32(owner+0x10);
    need(f.rt.getHandle(completedId)==f.rt.mainThreadHandle,"Original request lost actual thread identity");
    cpu.invoke(0x827B4EE0,owner);
    need(!f.rt.getHandle(f.eventId) && PPC_LOAD_U32(owner+4)==0 && cpu.registers().r1.u64==stack,"Original achievement owner did not release event/frame");
}
}
int main(int argc,char** argv){try{
    need(argc==2,"Expected original image path");Fixture fixture(argv[1]);importContracts(fixture);originalRoundTrip(fixture);
    // The retained event lease lets this final rejection test inspect its real
    // unsignalled object even after the original owner removed the public handle.
    auto ctx=fixture.prepare();fixture.rt.requestStop("achievement bridge fixture complete");fixture.rejected(ctx);
    std::printf("PASS native achievement bridge: %zu checks; real durable profiles/records, original writer/poll/teardown, event-after-persistence and rejection\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL native achievement bridge: %s\n",error.what());return 1;}}
