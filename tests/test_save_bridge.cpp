#include "runtime/runtime.h"
#include "runtime/native_saves.h"
#include "runtime/native_local_players.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/filesystem.h"
#include <array>
#include <cstdio>
#include <cstring>

PPC_FUNC(__imp__XamContentGetCreator);
PPC_FUNC(__imp__NtClose);
namespace {
using namespace Simpsons;size_t checks{};
void need(bool ok,const char* why){++checks;if(!ok)throw Failure(why);}
struct Temp {std::filesystem::path path;Temp(){wchar_t buffer[MAX_PATH]{};need(GetTempPathW(MAX_PATH,buffer)!=0,"Temporary root unavailable");const auto parent=std::filesystem::canonical(buffer);
    path=parent/("SimpsonsSaveBridge-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));need(path.parent_path()==parent&&std::filesystem::create_directory(path),"Unique contained temporary root failed");}
    ~Temp(){std::error_code e;std::filesystem::remove_all(path,e);}};
constexpr uint32_t data=0x50000,alias=0x50200,disposition=0x50240,license=0x50244,creator=0x50248,identity=0x50250;
constexpr uint32_t handleOut=0x50300,ios=0x50310,attrs=0x50400,ansi=0x50420,path=0x50440,buffer=0x50600,offset=0x50700;
uint32_t create(EngineCpuCalls& cpu,uint32_t mode,uint32_t slot=0){auto& c=cpu.registers();c.r8.u64=license;c.r9.u64=0;return cpu.invoke(0x824327D0,slot,alias,data,mode,disposition);}
PPCContext context(const PPCContext& entry){PPCContext c{};c.r1.u64=entry.r1.u64;return c;}
uint32_t open(Runtime& rt,const PPCContext& entry,bool writable){auto* base=rt.base;const std::string name="rmcsave:\\payload.bin";
    PPC_STORE_U32(attrs,0xFFFFFFFD);PPC_STORE_U32(attrs+4,ansi);PPC_STORE_U32(attrs+8,0x40);
    PPC_STORE_U16(ansi,uint16_t(name.size()));PPC_STORE_U16(ansi+2,uint16_t(name.size()+1));PPC_STORE_U32(ansi+4,path);std::memcpy(rt.pointer(path,unsigned(name.size()+1),true),name.c_str(),name.size()+1);
    auto c=context(entry);c.r3.u64=handleOut;c.r4.u64=GENERIC_READ|SYNCHRONIZE|(writable?GENERIC_WRITE:0);c.r5.u64=attrs;c.r6.u64=ios;
    c.r7.u64=0;c.r8.u64=FILE_ATTRIBUTE_NORMAL;c.r9.u64=writable?0:FILE_SHARE_READ;c.r10.u64=writable?3:1;PPC_STORE_U32(c.r1.u32+0x54,0x60);
    __imp__NtCreateFile(c,base);need(c.r3.u32==0&&PPC_LOAD_U32(ios)==0&&PPC_LOAD_U32(ios+4)==(writable?2u:1u),"Native save file open/disposition failed");return PPC_LOAD_U32(handleOut);
}
void close(Runtime& rt,const PPCContext& entry,uint32_t handle){auto c=context(entry);c.r3.u64=handle;__imp__NtClose(c,rt.base);need(c.r3.u32==0,"Original save file close failed");}
}
int main(int argc,char** argv){try{
    need(argc==2,"Original image required");Temp temp;Runtime rt;rt.configureLocalPlayers(temp.path/"profiles");rt.contentRoot=temp.path/"content";std::filesystem::create_directory(rt.contentRoot);rt.load(argv[1]);
    PPCContext entry{};rt.initialize(entry);auto* base=rt.base;rt.map(0x50000,0x2000,true,"Native save bridge fixture");
    const auto players=rt.localPlayerSource();const auto player=players->create("Save bridge");rt.activateLocalPlayer(0,player.id);
    const auto profilePath=temp.path/"profiles"/(player.id+".profile");const auto profileBefore=readFile(profilePath);const auto handles=rt.handles.size();
    std::memset(rt.pointer(data,0x134,true),0,0x134);PPC_STORE_U32(data,1);PPC_STORE_U32(data+4,1);
    const std::wstring display=L"Original bridge fixture";for(uint32_t i=0;i<display.size();++i)PPC_STORE_U16(data+8+i*2,uint16_t(display[i]));
    std::memcpy(rt.pointer(data+0x108,6,true),"SlotA",6);std::memcpy(rt.pointer(alias,8,true),"rmcsave",8);
    EngineCpuCalls cpu(entry,base);const auto stack=cpu.registers().r1.u64;
    need(create(cpu,2)==0&&PPC_LOAD_U32(disposition)==1&&!PPC_LOAD_U32(license),"Original create wrapper failed");
    need(rt.nativeSaveSource()->find("rmcsave")!=nullptr&&Platform::scanNativeSaves(rt.contentRoot,player.id,0x45410809).records.empty(),"Create did not mount an unpublished native session");
    const auto savedFP=PPCFPSCRRegister::getcsr();for(uint32_t fp:{0x1F80u,0x3FC0u,0x5F80u,0x9FC0u,0xE07Fu}){
        PPCContext c{};std::memset(&c,0xA5,sizeof(c));c.r1.u64=entry.r1.u64;c.r3.u64=0;c.r4.u64=data;c.r5.u64=creator;c.r6.u64=identity;c.r7.u64=0;
        PPCContext expected;std::memcpy(&expected,&c,sizeof(c));expected.r3.u64=0;PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(0x13579BDF);__imp__XamContentGetCreator(c,base);
        const auto actualFP=PPCFPSCRRegister::getcsr();const auto error=GetLastError();PPCFPSCRRegister::restoreHostCSR(savedFP);
        need(!std::memcmp(&c,&expected,sizeof(c))&&actualFP==fp&&error==0x13579BDF,"Creator query changed CPU/host state");
        need(PPC_LOAD_U32(creator)==1&&PPC_LOAD_U64(identity)==players->identity(0),"Creator is not actual native profile identity");
    }
    need(cpu.invoke(0x82432550,0,data,creator,0,0)==0&&PPC_LOAD_U32(creator)==1,"Original creator wrapper failed");
    const auto file=open(rt,entry,true);need(rt.getHandle(file)->saveFile!=nullptr,"Writable save file lacks native owner");
    const std::array<uint8_t,13> payload{0,1,2,3,0xFE,0xFF,0x41,0,0x19,0x72,0xAB,0xCD,0xEF};std::memcpy(rt.pointer(buffer,unsigned(payload.size()),true),payload.data(),payload.size());PPC_STORE_U64(offset,0);
    auto c=context(entry);c.r3.u64=file;c.r7.u64=ios;c.r8.u64=buffer;c.r9.u64=payload.size();c.r10.u64=offset;__imp__NtWriteFile(c,base);
    need(c.r3.u32==0&&PPC_LOAD_U32(ios+4)==payload.size(),"Actual native save write failed");
    c=context(entry);c.r3.u64=file;c.r4.u64=ios;__imp__NtFlushBuffersFile(c,base);need(c.r3.u32==0,"Actual native file flush failed");
    need(cpu.invoke(0x82432530,alias,0)==ERROR_SHARING_VIOLATION,"Container close published a still-open writable file");close(rt,entry,file);
    need(cpu.invoke(0x82432538,alias,0)==0,"Original content flush wrapper failed");need(cpu.invoke(0x82432530,alias,0)==0,"Original content close wrapper failed");
    need(!rt.nativeSaveSource()->find("rmcsave")&&Platform::scanNativeSaves(rt.contentRoot,player.id,0x45410809).records.size()==1,"Completed save was not published/released");
    need(create(cpu,3)==0&&PPC_LOAD_U32(disposition)==2,"Original reopen wrapper failed");const auto reader=open(rt,entry,false);std::memset(rt.pointer(buffer,unsigned(payload.size()),true),0xA5,payload.size());
    c=context(entry);c.r3.u64=reader;c.r7.u64=ios;c.r8.u64=buffer;c.r9.u64=payload.size();c.r10.u64=offset;__imp__NtReadFile(c,base);
    need(c.r3.u32==0&&PPC_LOAD_U32(ios+4)==payload.size()&&!std::memcmp(rt.pointer(buffer,unsigned(payload.size()),false),payload.data(),payload.size()),"Reopened original save bytes differ");
    c=context(entry);c.r3.u64=reader;c.r7.u64=ios;__imp__NtWriteFile(c,base);need(c.r3.u32==0xC0000022,"Read-only native save accepted write");close(rt,entry,reader);
    // Execute the original FindFirstFile directory-open/query consumer. This
    // supplies the save loader's file length before it validates the header.
    constexpr uint32_t directoryInfo=0x50800;
    const auto lookupStatus=cpu.invoke(0x82434388,attrs,directoryInfo,512,handleOut);
    std::fprintf(stderr,"Original save directory lookup status=%08X\n",lookupStatus);
    need(lookupStatus==0,"Original save directory lookup failed");
    const auto search=PPC_LOAD_U32(handleOut);
    need(PPC_LOAD_U64(directoryInfo+0x28)==payload.size()&&PPC_LOAD_U32(directoryInfo+0x3C)==11&&
        !std::memcmp(rt.pointer(directoryInfo+0x40,11,false),"payload.bin",11),"Original lookup did not return actual payload name/size");
    need(cpu.invoke(0x82434500,search,directoryInfo,512)==0x80000006,"Original FindNext did not report end of actual directory");
    need(cpu.invoke(0x82432530,alias,0)==ERROR_SHARING_VIOLATION,"Container closed while original search still owned its directory");
    for(uint32_t fp:{0x1F80u,0x3FC0u,0x5F80u,0x9FC0u,0xE07Fu}){
        std::memset(&c,0xA5,sizeof(c));c.r1.u64=entry.r1.u64;c.r3.u64=search;c.r4.u64=0;c.r5.u64=0;c.r6.u64=0;
        c.r7.u64=ios;c.r8.u64=directoryInfo;c.r9.u64=512;c.r10.u64=0;PPC_STORE_U32(c.r1.u32+0x54,1);
        PPCContext expected;std::memcpy(&expected,&c,sizeof(c));expected.r3.u64=0;
        std::memset(rt.pointer(directoryInfo,512,true),0xA5,512);
        PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(0x13579BDF);__imp__NtQueryDirectoryFile(c,base);
        const auto actualFP=PPCFPSCRRegister::getcsr();const auto error=GetLastError();PPCFPSCRRegister::restoreHostCSR(savedFP);
        need(!std::memcmp(&c,&expected,sizeof(c))&&actualFP==fp&&error==0x13579BDF,"Directory query changed CPU/host state");
        need(PPC_LOAD_U32(ios)==0&&PPC_LOAD_U32(ios+4)==75&&PPC_LOAD_U64(directoryInfo+40)==payload.size()&&
            PPC_LOAD_U8(directoryInfo+75)==0xA5,"Restarted directory query lost real size or exceeded its result");
    }
    c=context(entry);c.r3.u64=search;c.r7.u64=ios;c.r8.u64=directoryInfo;c.r9.u64=71;c.r10.u64=0;
    __imp__NtQueryDirectoryFile(c,base);need(c.r3.u32==0xC0000004&&PPC_LOAD_U32(ios+4)==0,"Short directory output was accepted");
    c.r3.u64=search;c.r9.u64=512;c.r8.u64=0;__imp__NtQueryDirectoryFile(c,base);need(c.r3.u32==0xC0000005,"Absent directory output was accepted");
    c.r3.u64=0xDEADBEEC;c.r8.u64=directoryInfo;__imp__NtQueryDirectoryFile(c,base);need(c.r3.u32==0xC0000008,"Unowned directory handle was accepted");
    c=context(entry);c.r3.u64=search;c.r7.u64=ios;__imp__NtWriteFile(c,base);need(c.r3.u32==0xC0000022,"Read-only save directory accepted write");
    close(rt,entry,search);
    const std::string absent="rmcsave:\\missing.bin";
    PPC_STORE_U16(ansi,uint16_t(absent.size()));PPC_STORE_U16(ansi+2,uint16_t(absent.size()));
    std::memcpy(rt.pointer(path,unsigned(absent.size()),true),absent.data(),absent.size());
    const auto beforeMissing=rt.handles.size();
    need(cpu.invoke(0x82434388,attrs,directoryInfo,512,handleOut)==0xC000000F&&rt.handles.size()==beforeMissing,"Original missing-file lookup lost native status or leaked directory");
    need(cpu.invoke(0x82432530,alias,0)==0,"Read session close failed");
    need(create(cpu,2,3)==ERROR_NO_SUCH_USER&&!rt.nativeSaveSource()->find("rmcsave"),"Signed-out slot created native save");
    PPC_STORE_U32(data,0);need(create(cpu,2)==ERROR_INVALID_PARAMETER&&!rt.nativeSaveSource()->find("rmcsave"),"Original zero-device validation bypassed");
    need(rt.handles.size()==handles&&cpu.registers().r1.u64==stack&&readFile(profilePath)==profileBefore,"Save bridge leaked handles/stack or changed profile");
    std::printf("PASS original native save bridge:%zu checks; original create/creator/flush/close wrappers, native file payload persistence and ownership\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL original native save bridge:%s\n",e.what());return 1;}}
