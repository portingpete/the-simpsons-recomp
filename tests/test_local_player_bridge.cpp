#include "runtime/runtime.h"
#include "runtime/native_local_players.h"
#include "runtime/native_notifications.h"
#include "runtime/engine_cpu_calls.h"
#include "ppc_recomp_shared.h"
#include <cstdio>
#include <cstring>
#include <array>

PPC_FUNC(__imp__XamUserGetSigninState);
PPC_FUNC(__imp__XamUserGetXUID);
PPC_FUNC(__imp__XamUserGetName);

namespace {
size_t checks=0;
void need(bool value,const char* reason) {++checks;if(!value) throw std::runtime_error(reason);}
template<class F> void rejects(F call,const char* reason) {
    bool failed=false;try {call();} catch(const std::exception&) {failed=true;}
    need(failed,reason);
}
struct Temp {
    std::filesystem::path path;
    Temp() {
        wchar_t buffer[MAX_PATH+1]{};
        const DWORD length=GetTempPathW(MAX_PATH,buffer);
        if(!length || length>MAX_PATH) throw std::runtime_error("Temporary path unavailable");
        const auto parent=std::filesystem::canonical(buffer);
        for(unsigned attempt=0;attempt<100;++attempt) {
            auto candidate=parent/("SimpsonsPlayerBridge-"+std::to_string(GetCurrentProcessId())+"-"+
                std::to_string(GetTickCount64())+"-"+std::to_string(attempt));
            if(CreateDirectoryW(candidate.c_str(),nullptr)) {path=std::filesystem::canonical(candidate);break;}
            if(GetLastError()!=ERROR_ALREADY_EXISTS) throw std::runtime_error("Temporary directory creation failed");
        }
        if(path.empty() || path.parent_path()!=parent) throw std::runtime_error("Temporary directory containment failed");
    }
    ~Temp() {if(!path.empty()) {std::error_code error;std::filesystem::remove_all(path,error);}}
};

void checkQuery(Simpsons::Runtime& rt,uint32_t index,uint32_t expectedState) {
    const uint32_t saved=PPCFPSCRRegister::getcsr();
    struct Restore {uint32_t value;~Restore(){PPCFPSCRRegister::restoreHostCSR(value);}} restore{saved};
    for(uint32_t controls:{0x1f80u,0x3fc0u,0x5f80u,0x9fc0u,0xe07fu}) {
        PPCContext ctx{},expected{};std::memset(&ctx,0xa5,sizeof(ctx));
        ctx.r3.u64=0xfedcba9800000000ull|index;
        ctx.r4.u64=0x8877665544332211ull; // Incidental register, not flags.
        std::memcpy(&expected,&ctx,sizeof(ctx));expected.r3.u64=expectedState;
        PPCFPSCRRegister::restoreHostCSR(controls);
        __imp__XamUserGetSigninState(ctx,rt.base);
        const uint32_t actual=PPCFPSCRRegister::getcsr();
        PPCFPSCRRegister::restoreHostCSR(saved);
        need(!std::memcmp(&ctx,&expected,sizeof(ctx)),"Sign-in query changed context beyond r3 or returned wrong state");
        need(actual==controls,"Sign-in query changed caller host FP state");
    }
}

void identityQuery(Simpsons::Runtime& rt,uint32_t index,uint64_t identity) {
    auto* base=rt.base;constexpr uint32_t output=0x18008;
    const uint32_t status=index>=4?uint32_t(E_INVALIDARG):identity?0:uint32_t(HRESULT_FROM_WIN32(ERROR_NO_SUCH_USER));
    const uint32_t saved=PPCFPSCRRegister::getcsr();
    struct Restore {uint32_t fp;~Restore(){PPCFPSCRRegister::restoreHostCSR(fp);}} restore{saved};
    for(uint32_t fp:{0x1f80u,0x3fc0u,0x5f80u,0x9fc0u,0xe07fu}) {
        PPCContext ctx{},expected{};std::memset(&ctx,0xa5,sizeof(ctx));ctx.r3.u64=0xFEDCBA9800000000ull|index;
        ctx.r4.u64=0x1122334400000007ull;ctx.r5.u64=0x9988776600000000ull|output;
        std::memcpy(&expected,&ctx,sizeof(ctx));expected.r3.u64=status;
        std::memset(rt.pointer(output-8,24,true),0xA7,24);
        PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(0x654321);
        __imp__XamUserGetXUID(ctx,base);const auto actualFP=PPCFPSCRRegister::getcsr();const auto error=GetLastError();
        PPCFPSCRRegister::restoreHostCSR(saved);
        need(!std::memcmp(&ctx,&expected,sizeof(ctx)) && actualFP==fp && error==0x654321,"Identity query changed context/host controls");
        need(PPC_LOAD_U64(output)==identity && PPC_LOAD_U64(output-8)==0xA7A7A7A7A7A7A7A7ull &&
             PPC_LOAD_U64(output+8)==0xA7A7A7A7A7A7A7A7ull,"Identity output endian/extent differs");
    }
    PPCContext entry{};entry.r1.u32=0x30000;Simpsons::EngineCpuCalls cpu(entry,base);
    // This actual original wrapper converts HRESULTs to Win32 status codes.
    const uint32_t converted=index>=4?ERROR_INVALID_PARAMETER:identity?0:ERROR_NO_SUCH_USER;
    need(cpu.invoke(0x82431F08,index,output)==converted && PPC_LOAD_U64(output)==identity,"Original identity wrapper status/output differs");
}

void nameQuery(Simpsons::Runtime& rt,uint32_t index,const char* name) {
    auto* base=rt.base;constexpr uint32_t output=0x18008;
    const uint32_t status=index>=4?ERROR_INVALID_PARAMETER:name?0:ERROR_NO_SUCH_USER;
    const uint32_t saved=PPCFPSCRRegister::getcsr();
    struct Restore {uint32_t fp;~Restore(){PPCFPSCRRegister::restoreHostCSR(fp);}} restore{saved};
    std::array<uint8_t,32> expectedBytes;expectedBytes.fill(0xA7);
    if(index<4){if(name)std::memcpy(expectedBytes.data()+8,name,std::strlen(name)+1);else expectedBytes[8]=0;}
    for(uint32_t fp:{0x1f80u,0x3fc0u,0x5f80u,0x9fc0u,0xe07fu}) {
        PPCContext ctx{},expected{};std::memset(&ctx,0xa5,sizeof(ctx));ctx.r3.u64=0xFEDCBA9800000000ull|index;
        ctx.r4.u64=0x1122334400000000ull|output;ctx.r5.u64=0x9988776600000010ull;
        std::memcpy(&expected,&ctx,sizeof(ctx));expected.r3.u64=status;
        std::memset(rt.pointer(output-8,32,true),0xA7,32);
        PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(0x456123);
        __imp__XamUserGetName(ctx,base);const auto actualFP=PPCFPSCRRegister::getcsr();const auto error=GetLastError();
        PPCFPSCRRegister::restoreHostCSR(saved);
        need(!std::memcmp(&ctx,&expected,sizeof(ctx)) && actualFP==fp && error==0x456123,"Name query changed context/host controls");
        need(!std::memcmp(rt.pointer(output-8,32,false),expectedBytes.data(),32),"Name query string/terminator/extent differs");
    }
    PPCContext entry{};entry.r1.u32=0x30000;Simpsons::EngineCpuCalls cpu(entry,base);
    std::memset(rt.pointer(output-8,32,true),0xA7,32);
    need(cpu.invoke(0x82431878,index,output,16)==status &&
        !std::memcmp(rt.pointer(output-8,32,false),expectedBytes.data(),32),"Original name tail wrapper result differs");
}

void originalNameRefresh(Simpsons::Runtime& rt,const std::string& name) {
    auto* base=rt.base;constexpr uint32_t object=0x19000;
    std::array<uint8_t,0x60> expected;expected.fill(0xA7);
    std::memset(rt.pointer(object,unsigned(expected.size()),true),0xA7,expected.size());
    PPC_STORE_U32(object+0x10,0);PPC_STORE_U8(object+0x14,1);
    std::memcpy(expected.data(),rt.pointer(object,unsigned(expected.size()),false),expected.size());
    std::memcpy(expected.data()+0x34,name.c_str(),name.size()+1);expected[0x44]=0;
    PPCContext entry{};entry.r1.u64=0x30000;Simpsons::EngineCpuCalls cpu(entry,base);
    const auto stack=cpu.registers().r1.u64;
    cpu.invoke(0x827B25E8,object);
    need(!std::memcmp(rt.pointer(object,unsigned(expected.size()),false),expected.data(),expected.size()),
         "Original refresh did not own the exact name publication");
    need(cpu.registers().r1.u64==stack,"Original name refresh failed to restore its frame");
}

void originalLoop(Simpsons::Runtime& rt,bool activeSlot) {
    auto* base=rt.base;
    constexpr uint32_t object=0x11000,associations=0x13000,profile=0x15000;
    std::array<uint8_t,0x100> before{};
    for(size_t i=0;i<before.size();++i) before[i]=uint8_t(i^0x5a);
    std::memcpy(rt.pointer(object,unsigned(before.size()),true),before.data(),before.size());
    std::memset(rt.pointer(associations,0x100,true),0,0x100);
    // Real original lookup 8285D7D0 observes unassociated game players. An
    // active platform slot does not independently create these associations.
    PPC_STORE_U8(associations+0x18,6);PPC_STORE_U8(associations+0x30,6);
    PPC_STORE_U32(0x82d08b9c,associations);
    PPC_STORE_U32(0x82d08d68,profile);
    PPC_STORE_U32(profile+0x10,4);PPC_STORE_U8(profile+0x14,1);
    PPCContext incoming{};incoming.r1.u64=0x30000;incoming.lr=0x11223344;
    Simpsons::EngineCpuCalls cpu(incoming,base);
    cpu.registers().r14.u64=0xfedcba9876543210ull;
    cpu.registers().r31.u64=0x8877665544332211ull;
    const uint64_t stack=cpu.registers().r1.u64;
    cpu.invoke(0x823a1228,object,0);
    need(!std::memcmp(rt.pointer(object,unsigned(before.size()),false),before.data(),before.size()),
         "Original query loop changed defaults without a game-player association");
    need(PPC_LOAD_U32(profile+0x10)==4 && PPC_LOAD_U8(profile+0x14)==1,
         "Native query rewrote original game selection sentinel");
    need(cpu.registers().r1.u64==stack && cpu.registers().r14.u64==0xfedcba9876543210ull &&
         cpu.registers().r31.u64==0x8877665544332211ull,"Original query loop did not restore ABI frame");
    need(cpu.invoke(0x82431880,0)==uint32_t(activeSlot),"Actual original tail wrapper disagrees with native registry");
}
}

int main(int argc,char** argv) {
    try {
        need(argc==2,"Expected original image path");
        Temp temp;
        Simpsons::Runtime rt;rt.configureLocalPlayers(temp.path/"profiles");rt.load(argv[1]);
        rt.map(0x10000,0x20000,true,"Native local-player bridge fixture");
        auto* base=rt.base;
        need(PPC_LOAD_U32(0x82431880)==0x48890c94,"Original sign-in tail pin changed");
        need(PPC_LOAD_U32(0x823a1254)==0x2f030000 && PPC_LOAD_U32(0x823a1258)==0x419a00f4,
             "Original no-sign-in branch pin changed");
        auto source=rt.localPlayerSource();
        auto listener=rt.notificationSource()->create(1,2);
        need(!listener->read(),"Native empty session invented startup notification");
        for(uint32_t index:{0u,1u,2u,3u,4u,6u,7u,0xffffffffu}) checkQuery(rt,index,0);
        originalLoop(rt,false);
        for(uint32_t slot:{0u,3u,4u,0xffffffffu})identityQuery(rt,slot,0);
        for(uint32_t slot:{0u,3u,4u,0xffffffffu})nameQuery(rt,slot,nullptr);
        const auto stored=source->create("Native fixture");
        for(uint32_t index=0;index<4;++index) checkQuery(rt,index,0);
        need(!listener->read(),"Creating a stored profile activated a slot or emitted a sign-in event");
        need(rt.activateLocalPlayer(0,stored.id),"Real activation reported no change");
        auto notification=listener->read();
        need(notification && notification->id==0xa && notification->parameter==0,"Committed activation lacked native invalidation");
        checkQuery(rt,0,1);checkQuery(rt,1,0);checkQuery(rt,4,0);checkQuery(rt,7,0);
        originalLoop(rt,true);
        const auto key=source->identity(0);need(key!=0,"Active native profile lacks its durable equality key");
        identityQuery(rt,0,key);identityQuery(rt,1,0);
        nameQuery(rt,0,stored.name.c_str());nameQuery(rt,1,nullptr);originalNameRefresh(rt,stored.name);
        {const auto longest=source->create("123456789012345");rt.activateLocalPlayer(3,longest.id);
         nameQuery(rt,3,longest.name.c_str());rt.signOutLocalPlayer(3);
         // These real transitions belong to this added fixture, not the checks below.
         auto first=listener->read(),second=listener->read();
         need(first && second && !listener->read(),"Name fixture transitions were not delivered");}
        {PPCContext invalid{};invalid.r3.u32=0;invalid.r4.u32=7;invalid.r5.u32=0;
         __imp__XamUserGetXUID(invalid,base);need(invalid.r3.u32==uint32_t(E_INVALIDARG),"Null identity output accepted");
         invalid.r3.u32=0;invalid.r4.u32=2;invalid.r5.u32=0x18008;PPC_STORE_U64(0x18008,0x123456789ABCDEF0ull);
         rejects([&]{__imp__XamUserGetXUID(invalid,base);},"Unqualified identity type mask accepted");
         need(invalid.r3.u32==0 && PPC_LOAD_U64(0x18008)==0x123456789ABCDEF0ull,"Rejected mask changed outputs");
         invalid.r4.u32=7;invalid.r5.u32=0x3FFF0;
         rejects([&]{__imp__XamUserGetXUID(invalid,base);},"Unmapped identity output accepted");
         need(invalid.r3.u32==0,"Rejected output changed return register");}
        {PPCContext invalid{};invalid.r3.u32=0;invalid.r4.u32=0;invalid.r5.u32=16;
         __imp__XamUserGetName(invalid,base);need(invalid.r3.u32==ERROR_INVALID_PARAMETER,"Null name output accepted");
         invalid.r3.u32=0;invalid.r4.u32=0x18008;invalid.r5.u32=15;PPC_STORE_U64(0x18008,0x123456789ABCDEF0ull);
         rejects([&]{__imp__XamUserGetName(invalid,base);},"Unqualified name length accepted");
         need(invalid.r3.u32==0 && PPC_LOAD_U64(0x18008)==0x123456789ABCDEF0ull,"Rejected name length changed output");
         invalid.r4.u32=0x3FFF0;invalid.r5.u32=16;
         rejects([&]{__imp__XamUserGetName(invalid,base);},"Unmapped name output accepted");
         need(invalid.r3.u32==0,"Rejected name output changed return register");}
        need(!rt.activateLocalPlayer(0,stored.id) && !listener->read(),"Idempotent activation emitted another event");
        rejects([&]{rt.activateLocalPlayer(1,stored.id);},"Same profile simultaneously occupied two slots");
        rejects([&]{rt.activateLocalPlayer(7,stored.id);},"Invalid activation aliased a slot");
        rejects([&]{rt.configureLocalPlayers(temp.path/"other");},"Live store configuration changed");
        need(!listener->read() && rt.localPlayerState(0)==1,"Rejected mutation changed state or notified");
        need(rt.signOutLocalPlayer(0),"Real sign-out reported no change");
        notification=listener->read();need(notification && notification->id==0xa,"Sign-out did not notify after commit");
        need(!rt.signOutLocalPlayer(0) && !listener->read(),"Empty sign-out emitted a transition");
        checkQuery(rt,0,0);
        identityQuery(rt,0,0);
        nameQuery(rt,0,nullptr);
        need(source->load(stored.id)==stored,"Sign-out deleted or changed durable profile");
        {Simpsons::Platform::NativeLocalPlayers reopened(temp.path/"profiles");
         need(reopened.load(stored.id)==stored && reopened.state(0)==0,"Reopen did not preserve profile with an empty session");}
        PPCContext ctx{};ctx.r3.u64=0x1122334400000000ull;
        rejects([&]{__imp__XamUserGetSigninState(ctx,nullptr);},"Foreign runtime base accepted");
        need(ctx.r3.u64==0x1122334400000000ull,"Rejected runtime query changed output");
        rt.requestStop("local-player bridge fixture complete");
        rejects([&]{__imp__XamUserGetSigninState(ctx,base);},"Cancelled runtime allowed a state query");
        ctx.r4.u32=7;ctx.r5.u32=0x18008;
        rejects([&]{__imp__XamUserGetXUID(ctx,base);},"Cancelled runtime allowed an identity query");
        ctx.r4.u32=0x18008;ctx.r5.u32=16;
        rejects([&]{__imp__XamUserGetName(ctx,base);},"Cancelled runtime allowed a name query");
        need(ctx.r3.u64==0x1122334400000000ull,"Cancelled state query changed output");
        std::printf("PASS native local-player bridge: %zu checks; durable profile, real transitions, original empty/active-unassociated loops; no selection/save claim\n",checks);
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL local-player bridge: %s\n",error.what());return 1;}
}
