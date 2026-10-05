#include "runtime/runtime.h"
#include "runtime/native_local_players.h"
#include "runtime/engine_cpu_calls.h"
#include <array>
#include <cstdio>
#include <cstring>

PPC_FUNC(__imp__XamUserReadProfileSettings);
namespace {
using namespace Simpsons;
size_t checks{};
void need(bool v,const char* why){++checks;if(!v)throw Failure(why);}
struct Temp {
    std::filesystem::path path;
    Temp(){wchar_t p[MAX_PATH]{};need(GetTempPathW(MAX_PATH,p)!=0,"Temporary root unavailable");
        const auto parent=std::filesystem::canonical(p);
        path=parent/("SimpsonsPreferences-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        need(path.parent_path()==parent&&std::filesystem::create_directory(path),"Unique contained fixture creation failed");}
    ~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}
};
constexpr uint32_t ids=0x50100,sizeOut=0x50200,out=0x50300;
void inputs(Runtime& rt,const PPCContext& entry){auto* base=rt.base;
    PPC_STORE_U32(ids,0x10040002);PPC_STORE_U32(ids+4,0x10040003);PPC_STORE_U32(sizeOut,0);
    PPC_STORE_U32(entry.r1.u32+0x54,0);std::memset(rt.pointer(out-8,112,true),0xA7,112);
}
PPCContext direct(const PPCContext& entry){PPCContext c{};std::memset(&c,0xA5,sizeof(c));
    c.r1.u64=entry.r1.u64;c.r3.u64=0x45410809;c.r4.u64=0;c.r5.u64=0;c.r6.u64=0;
    c.r7.u64=2;c.r8.u64=ids;c.r9.u64=sizeOut;c.r10.u64=0;return c;
}
uint32_t wrapper(EngineCpuCalls& cpu,uint32_t slot,uint32_t destination,uint32_t title=0x45410809){
    auto& c=cpu.registers();c.r8.u64=destination;c.r9.u64=0;
    return cpu.invoke(0x82C71CB8,title,slot,2,ids,sizeOut);
}
struct StartupObserved {};
}
int main(int argc,char** argv){try{
    need(argc==2,"Original image required");Temp temp;Runtime rt;rt.configureLocalPlayers(temp.path/"profiles");rt.load(argv[1]);
    PPCContext entry{};rt.initialize(entry);auto* base=rt.base;rt.map(0x50000,0x1000,true,"Profile preference fixture");
    rt.map(0x60000,0x1000,false,"Profile preference readonly fixture");
    const auto players=rt.localPlayerSource();const auto player=players->create("Preferences");rt.activateLocalPlayer(0,player.id);
    const auto profilePath=temp.path/"profiles"/(player.id+".profile");const auto beforeFile=readFile(profilePath);
    const auto savedFP=PPCFPSCRRegister::getcsr(),handles=uint32_t(rt.handles.size()),regions=uint32_t(rt.regions.size());
    for(uint32_t fp:{0x1F80u,0x3FC0u,0x5F80u,0x9FC0u,0xE07Fu})for(bool fill:{false,true}){
        inputs(rt,entry);auto c=direct(entry);if(fill){PPC_STORE_U32(sizeOut,88);c.r10.u64=out;}
        PPCContext expected;std::memcpy(&expected,&c,sizeof(c));expected.r3.u64=fill?0:ERROR_INSUFFICIENT_BUFFER;
        PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(0x13579BDF);__imp__XamUserReadProfileSettings(c,base);
        const auto actualFP=PPCFPSCRRegister::getcsr();const auto error=GetLastError();PPCFPSCRRegister::restoreHostCSR(savedFP);
        need(!std::memcmp(&c,&expected,sizeof(c))&&actualFP==fp&&error==0x13579BDF,"Preference query changed CPU/host state");
        need(PPC_LOAD_U32(sizeOut)==88,"Preference capacity negotiation differs");
        need(PPC_LOAD_U64(out-8)==0xA7A7A7A7A7A7A7A7ull&&PPC_LOAD_U64(out+88)==0xA7A7A7A7A7A7A7A7ull,"Preference output escaped its extent");
        for(uint32_t i=0;i<88;++i){uint8_t value=fill?0:0xA7;if(fill&&i>=4&&i<8)value=uint8_t((out+8)>>(24-(i-4)*8));
            need(PPC_LOAD_U8(out+i)==value,"Native v1 profile returned a fabricated setting or changed size-only output");}
        need(rt.handles.size()==handles&&rt.regions.size()==regions&&readFile(profilePath)==beforeFile,"Preference read changed native ownership/profile bytes");
    }
    inputs(rt,entry);const auto sp=entry.r1.u64-0x100;
    {EngineCpuCalls cpu(entry,base);
    need(wrapper(cpu,0,0)==ERROR_INSUFFICIENT_BUFFER&&PPC_LOAD_U32(sizeOut)==88,"Original wrapper size query failed");
    need(wrapper(cpu,0,out)==0&&PPC_LOAD_U32(out)==0&&PPC_LOAD_U32(out+4)==out+8&&cpu.registers().r1.u64==sp,"Original wrapper empty settings result failed");
    inputs(rt,entry);PPC_STORE_U32(sizeOut,88);
    need(wrapper(cpu,0,out,0x45420001)==ERROR_FUNCTION_FAILED&&PPC_LOAD_U32(out)==0xA7A7A7A7,"Original publisher gate bypassed");
    need(wrapper(cpu,3,out)==ERROR_FUNCTION_FAILED&&PPC_LOAD_U32(out)==0xA7A7A7A7,"Empty slot fabricated preferences");
    rt.signOutLocalPlayer(0);need(wrapper(cpu,0,out)==ERROR_FUNCTION_FAILED,"Signed-out profile returned preference success");rt.activateLocalPlayer(0,player.id);}
    for(uint32_t small:{1u,87u}){inputs(rt,entry);PPC_STORE_U32(sizeOut,small);auto c=direct(entry);c.r10.u64=out;__imp__XamUserReadProfileSettings(c,base);
        need(c.r3.u32==ERROR_INSUFFICIENT_BUFFER&&PPC_LOAD_U32(sizeOut)==small&&PPC_LOAD_U32(out)==0xA7A7A7A7,"Short-buffer negotiation changed outputs");}
    const auto rejects=[&](auto mutate){inputs(rt,entry);PPC_STORE_U32(sizeOut,88);auto c=direct(entry);c.r10.u64=out;mutate(c);bool failed=false;
        try{__imp__XamUserReadProfileSettings(c,base);}catch(const Failure&){failed=true;}
        need(failed&&PPC_LOAD_U32(out)==0xA7A7A7A7,"Invalid preference request published results");};
    rejects([](auto& c){c.r3.u64=0x12345678;});rejects([](auto& c){c.r4.u64=4;});rejects([](auto& c){c.r5.u64=1;});
    rejects([](auto& c){c.r6.u64=ids;});rejects([](auto& c){c.r7.u64=0;});rejects([](auto& c){c.r7.u64=3;});
    rejects([](auto& c){c.r8.u64=ids+1;});rejects([](auto& c){c.r9.u64=0;});rejects([](auto& c){c.r9.u64=0x60000;});
    rejects([](auto& c){c.r10.u64=0;});rejects([](auto& c){c.r10.u64=0x60000;});rejects([](auto& c){c.r10.u64=sizeOut;});
    rejects([&](auto& c){PPC_STORE_U32(c.r1.u32+0x54,0x50400);});rejects([&](auto&){PPC_STORE_U32(ids,0x10040015);});
    rejects([&](auto&){PPC_STORE_U32(ids+4,0x10040002);});
    // Reach the existing original startup observation to own the real original
    // allocator. No replacement allocator/callback or skipped production flow.
    bool observed=false;rt.audioBoundaryObserver=[&](uint32_t pc,PPCContext&,uint8_t*){
        if(pc==0x828166FC)throw StartupObserved{};need(pc==0x82345920,"Unexpected startup observation");};
    const PPCContext initial=entry;try{runOriginal(entry,base);}catch(const StartupObserved&){observed=true;}
    rt.audioBoundaryObserver={};need(observed,"Original allocator startup was not reached");
    entry=initial;EngineCpuCalls cpu(entry,base);
    inputs(rt,entry);constexpr uint32_t owner=0x50500,flagY=0x50600,flagV=0x50601,valueY=0x50604,valueV=0x50608;
    std::memset(rt.pointer(owner,0x50,true),0,0x50);PPC_STORE_U32(owner+0x10,0);PPC_STORE_U8(owner+0x14,1);PPC_STORE_U64(owner+0x28,0x45410809);
    PPC_STORE_U8(flagY,0xA7);PPC_STORE_U8(flagV,0xA7);PPC_STORE_U32(valueY,0x11223344);PPC_STORE_U32(valueV,0x55667788);
    cpu.registers().r8.u64=0;cpu.invoke(0x827B2CE0,owner,flagY,valueY,flagV,valueV);
    need(!PPC_LOAD_U8(flagY)&&!PPC_LOAD_U8(flagV)&&PPC_LOAD_U32(valueY)==0x11223344&&PPC_LOAD_U32(valueV)==0x55667788,
        "Original preference reader invented availability or overwrote defaults");
    constexpr uint32_t associations=0x50700,options=0x50800;
    std::memset(rt.pointer(associations,0x40,true),0,0x40);PPC_STORE_U8(associations+0x18,0);PPC_STORE_U8(associations+0x30,6);
    const auto oldAssociations=PPC_LOAD_U32(0x82D08B9C),oldOwner=PPC_LOAD_U32(0x82D08D68);
    PPC_STORE_U32(0x82D08B9C,associations);PPC_STORE_U32(0x82D08D68,owner);
    std::array<uint8_t,256> defaults;for(size_t i=0;i<defaults.size();++i)defaults[i]=uint8_t(i^0x5A);
    std::memcpy(rt.pointer(options,256,true),defaults.data(),256);cpu.invoke(0x823A1228,options,0);
    need(!std::memcmp(rt.pointer(options,256,false),defaults.data(),256)&&cpu.registers().r1.u64==sp,"Original active-player loop changed game defaults without overrides");
    PPC_STORE_U32(0x82D08B9C,oldAssociations);PPC_STORE_U32(0x82D08D68,oldOwner);
    need(readFile(profilePath)==beforeFile&&std::distance(std::filesystem::directory_iterator(temp.path),std::filesystem::directory_iterator{})==1,"Read created preference/save sidecars");
    auto cancelled=direct(entry);rt.requestStop("Native profile preferences fixture complete");bool stopped=false;
    try{__imp__XamUserReadProfileSettings(cancelled,base);}catch(const Failure&){stopped=true;}
    need(stopped,"Cancelled runtime returned profile preferences");
    std::printf("PASS native profile preferences:%zu checks; actual v1 absence, original allocation/read/free and retained controller defaults, synchronous ABI and rejection\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL native profile preferences:%s\n",e.what());return 1;}}
