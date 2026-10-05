#include "runtime/runtime.h"
#include "runtime/native_content.h"
#include "runtime/native_local_players.h"
#include "runtime/engine_cpu_calls.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <set>
#include <thread>

PPC_EXTERN_FUNC(__imp__XamContentCreateEnumerator);
PPC_EXTERN_FUNC(__imp__XamContentGetDeviceData);
PPC_EXTERN_FUNC(__imp__XamContentGetDeviceState);
PPC_EXTERN_FUNC(__imp__XamContentGetDeviceName);
PPC_EXTERN_FUNC(__imp__XamEnumerate);
PPC_EXTERN_FUNC(__imp__NtClose);
namespace {
using namespace Simpsons;
size_t checks{};
void need(bool b,const char* s){++checks;if(!b)throw Failure(s);}
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}need(caught,"Invalid content operation was accepted");}
void word(std::vector<uint8_t>& b,size_t p,uint32_t v){for(uint32_t i=0;i<4;++i)b[p+i]=uint8_t(v>>(24-8*i));}
std::vector<uint8_t> package(const std::u16string& name,uint32_t magic=0x50495253){
    std::vector<uint8_t> b(0xA000,0);word(b,0,magic);word(b,0x340,0x971A);word(b,0x344,2);word(b,0x348,2);word(b,0x360,0x45410809);b[0x379]=0x24;
    for(size_t i=0;i<name.size()&&i<128;++i){b[0x411+i*2]=uint8_t(name[i]>>8);b[0x412+i*2]=uint8_t(name[i]);}
    return b;
}
void write(const std::filesystem::path& p,const std::vector<uint8_t>& b){std::ofstream f(p,std::ios::binary|std::ios::trunc);need(bool(f),"Create fixture package failed");f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));need(bool(f),"Write fixture package failed");}
struct Fixture {
    std::filesystem::path root,installed,disc,folder;
    Fixture(){
        wchar_t temp[MAX_PATH]{};need(GetTempPathW(MAX_PATH,temp)!=0,"Get fixture directory failed");
        root=std::filesystem::path(temp)/(L"SimpsonsContentTest-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        need(std::filesystem::create_directory(root),"Create unique fixture directory failed");
        installed=root/L"installed";disc=root/L"game";folder=installed/L"0000000000000000"/L"45410809"/L"00000002";
        std::filesystem::create_directory(disc);
    }
    ~Fixture(){
        // Only this exact unique directory, below the native temporary root.
        // Never delete an input content store or an original game directory.
        std::error_code e;if(root.is_absolute()&&root.filename().wstring().starts_with(L"SimpsonsContentTest-"))std::filesystem::remove_all(root,e);
    }
};
uint32_t load(const Platform::ContentSnapshot::Record& r,size_t p){return uint32_t(r[p])<<24|uint32_t(r[p+1])<<16|uint32_t(r[p+2])<<8|r[p+3];}
void catalog(){
    Fixture f;
    {auto s=Platform::scanContent(f.installed,f.disc/"Content",0x45410809,1);need(s.records.empty(),"Missing content locations fabricated records");}
    need(!std::filesystem::exists(f.installed),"Read-only scan created a content directory");
    std::filesystem::create_directories(f.folder);
    const auto a=package(u"Original \u03A9 name"),b=package(u"Second",0x4C495645);write(f.folder/"B",b);write(f.folder/"A",a);
    {
        auto s=Platform::scanContent(f.installed,f.disc/"Content",0x45410809,1);
        need(s.records.size()==2&&load(s.records[0],0)==1&&load(s.records[0],4)==2,"Native catalog count/device/type differs");
        need(s.records[0][0x108]=='A'&&s.records[1][0x108]=='B',"Native package snapshot order differs");
        need(!std::memcmp(s.records[0].data()+8,a.data()+0x411,2*15),"Original UTF16 display name differs");
        for(size_t i=0x109;i<0x134;++i)need(s.records[0][i]==0,"Short filename did not retain zero padding");
        HANDLE replace=CreateFileW((f.folder/"A").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        need(replace==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_SHARING_VIOLATION,"Snapshot did not retain immutable package ownership");
        need(!MoveFileW((f.folder/"A").c_str(),(f.folder/"Moved").c_str())&&GetLastError()==ERROR_SHARING_VIOLATION,"Snapshot package was renamed while owned");
    }
    const auto original=a;
    for(const auto field:std::array<std::array<uint32_t,2>,7>{{{0,0},{0x340,0x10000},{0x344,1},{0x348,3},{0x360,0x12345678},{0x3A9,1},{0x371,1}}}){
        auto bad=original;word(bad,field[0],field[1]);write(f.folder/"A",bad);
        rejects([&]{Platform::scanContent(f.installed,f.disc/"Content",0x45410809,1);});
    }
    auto bad=original;bad.resize(0x500);write(f.folder/"A",bad);rejects([&]{Platform::scanContent(f.installed,f.disc/"Content",0x45410809,1);});
    write(f.folder/"A",original);
    std::filesystem::create_directory(f.folder/"extracted");rejects([&]{Platform::scanContent(f.installed,f.disc/"Content",0x45410809,1);});std::filesystem::remove(f.folder/"extracted");
    const auto discFolder=f.disc/L"Content"/L"0000000000000000"/L"45410809"/L"00000002";std::filesystem::create_directories(discFolder);
    write(discFolder/"OnDisc",package(u"On-disc",0x434F4E20));
    {auto s=Platform::scanContent(f.installed,f.disc/"Content",0x45410809,1);need(s.records.size()==3&&load(s.records.back(),0)==2,"Disc content was omitted or misidentified");}
    {auto s=Platform::scanContent(f.installed,f.disc/"Content",0x45410809,1,2);need(s.records.size()==1&&load(s.records[0],0)==2,"Native device filter differs");}
    auto localized=package(u"English");localized[0x541A]=0;localized[0x541B]='P';write(f.folder/"A",localized);
    {auto s=Platform::scanContent(f.installed,f.disc/"Content",0x45410809,10,1);need(s.records[0][8]==0&&s.records[0][9]=='P',"Version2 display language offset differs");}
    {auto s=Platform::scanContent(f.installed,f.disc/"Content",0x45410809,2,1);need(s.records[0][9]=='E',"Absent localization did not use original English metadata");}
}
void saveFixture(const std::filesystem::path& path,const std::string& display){
    std::filesystem::create_directories(path/L"data");
    const auto metadata=std::string("SIMPSONS-NATIVE-SAVE 1\n")+display+"\n";
    write(path/L"metadata.txt",std::vector<uint8_t>(metadata.begin(),metadata.end()));
    write(path/L"data"/L"fixture.bin",{1,2,3,4}); // Test bytes, never campaign progress.
}
void saveCatalog(){
    Fixture f;const std::string owner="11111111-2222-4333-8444-555555555555",other="11111111-2222-4333-8444-666666666666";
    auto scan=[&](const std::string& id=std::string("11111111-2222-4333-8444-555555555555"),uint32_t title=0x45410809,uint32_t device=0){return Platform::scanNativeSaves(f.installed,id,title,device);};
    {auto s=scan();need(s.records.empty()&&!std::filesystem::exists(f.installed),"Empty save scan manufactured storage");}
    const auto folder=f.installed/L"saves"/owner/L"45410809";
    saveFixture(folder/L"B","Second");saveFixture(folder/L"A","Native \xCE\xA9");
    saveFixture(f.installed/L"saves"/other/L"45410809"/L"Other","Other profile");
    saveFixture(f.installed/L"saves"/owner/L"12345678"/L"Title","Other title");
    {
        auto s=scan();need(s.records.size()==2&&load(s.records[0],0)==1&&load(s.records[0],4)==1,"Native save device/type/count differs");
        need(s.records[0][0x108]=='A'&&s.records[1][0x108]=='B'&&s.records[0][22]==3&&s.records[0][23]==0xA9,"Native save order/UTF16 name differs");
        for(const auto& path:{folder/L"A"/L"metadata.txt",folder/L"A"/L"data"/L"fixture.bin"}){
            HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
            need(h==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_SHARING_VIOLATION,"Save snapshot did not pin actual metadata/data");
        }
        need(!MoveFileW((folder/L"A").c_str(),(folder/L"Moved").c_str()),"Save snapshot directory moved while owned");
    }
    need(scan(owner,0x45410809,1).records.size()==2&&scan(other).records.size()==1&&scan(owner,0x12345678).records.size()==1,"Native save owner/title/device filtering differs");
    rejects([&]{scan("../../outside");});rejects([&]{scan(owner,0x45410809,2);});
    for(const auto& bad:std::vector<std::string>{"", "bad\n", "SIMPSONS-NATIVE-SAVE 2\nName\n", "SIMPSONS-NATIVE-SAVE 1\n\xFF\n", "SIMPSONS-NATIVE-SAVE 1\nName\nTrailing\n",std::string("SIMPSONS-NATIVE-SAVE 1\n")+std::string(128,'A')+"\n"}){
        write(folder/L"A"/L"metadata.txt",std::vector<uint8_t>(bad.begin(),bad.end()));rejects([&]{scan();});
    }
    saveFixture(folder/L"A","Restored");
    std::filesystem::remove(folder/L"A"/L"data"/L"fixture.bin");rejects([&]{scan();});
    saveFixture(folder/L"A","Restored");
    const auto alias=f.root/L"alias.bin";
    need(CreateHardLinkW(alias.c_str(),(folder/L"A"/L"data"/L"fixture.bin").c_str(),nullptr)!=FALSE,"Create save hard-link fixture failed");
    rejects([&]{scan();});std::filesystem::remove(alias);
    write(folder/L"unexpected",{1});rejects([&]{scan();});std::filesystem::remove(folder/L"unexpected");
    need(scan().records.size()==2,"Rejected catalog poisoned a later real snapshot");
}
void storageEligibility(){
    Fixture f;
    rejects([&]{Platform::inspectNativeStorage(f.installed);});
    need(!std::filesystem::exists(f.installed),"Storage inspection manufactured a configured folder");
    std::filesystem::create_directory(f.installed);
    write(f.installed/L"keep.bin",{9,8,7});
    {
        auto storage=Platform::inspectNativeStorage(f.installed);
        ULARGE_INTEGER available{},total{},free{};
        need(GetDiskFreeSpaceExW(f.installed.c_str(),&available,&total,&free)!=FALSE,"Fixture volume query failed");
        need(storage.path==std::filesystem::absolute(f.installed).lexically_normal()&&storage.totalBytes==total.QuadPart&&storage.availableBytes<=storage.totalBytes&&storage.freeBytes<=storage.totalBytes,
            "Native storage capacity/ownership differs from its actual volume");
        need(storage.fits(0)&&storage.fits(storage.availableBytes)&&!storage.fits(UINT64_MAX),"Native requested-byte qualification truncated a64-bit size");
        size_t count=0;for(const auto& entry:std::filesystem::directory_iterator(f.installed)){
            ++count;need(entry.path().filename()==L"keep.bin","Storage eligibility retained a probe or created save data");}
        need(count==1&&readFile(f.installed/L"keep.bin")==std::vector<uint8_t>({9,8,7}),"Storage eligibility changed an existing file");
        need(!MoveFileW(f.installed.c_str(),(f.root/L"moved").c_str()),"Storage eligibility did not retain its actual directory owner");
    }
    need(MoveFileW(f.installed.c_str(),(f.root/L"moved").c_str())!=FALSE,"Storage ownership leaked directory leases after release");
    rejects([&]{Platform::inspectNativeStorage(f.root/L"moved"/L"keep.bin");});
    rejects([&]{Platform::inspectNativeStorage(f.root.root_path());});
}
struct Abi {uint64_t sp,lr;std::array<uint64_t,18> gpr;};
Abi abi(const PPCContext& c){return {c.r1.u64,c.lr,{c.r14.u64,c.r15.u64,c.r16.u64,c.r17.u64,c.r18.u64,c.r19.u64,c.r20.u64,c.r21.u64,c.r22.u64,c.r23.u64,c.r24.u64,c.r25.u64,c.r26.u64,c.r27.u64,c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64}};}
void unchanged(const PPCContext& c,const Abi& a){const auto b=abi(c);need(a.sp==b.sp&&a.lr==b.lr&&a.gpr==b.gpr,"Original content wrapper changed nonvolatile ABI");}
void bridge(const char* image){
    Fixture f;Runtime rt;rt.load(image);PPCContext entry{};rt.initialize(entry);auto* base=rt.base;
    rt.gameRoot=f.disc;rt.contentRoot=f.installed;rt.map(0x50000,0x4000,true,"bounded content ABI fixture");
    EngineCpuCalls cpu(entry,base);auto& c=cpu.registers();constexpr uint32_t sizeOut=0x50000,handleOut=0x50004,buffer=0x50100,countOut=0x50008,ov=0x51000;
    auto create=[&](uint32_t page){c.r3.u64=0xFE;c.r4.u64=0;c.r5.u64=2;c.r6.u64=0;c.r7.u64=page;c.r8.u64=sizeOut;c.r9.u64=handleOut;
        const auto a=abi(c);cpu.invoke(0x82432560);unchanged(c,a);need(c.r3.u64==0&&PPC_LOAD_U32(sizeOut)==page*0x134,"Original create wrapper/result size differs");return PPC_LOAD_U32(handleOut);};
    auto enumerate=[&](uint32_t h,uint32_t length,uint32_t count,uint32_t o){const auto a=abi(c);
        // Original XEnumerate wrapper rearranges5 arguments and inserts flags0.
        cpu.invoke(0x82432C90,h,buffer,length,count,o);unchanged(c,a);return c.r3.u32;};
    auto close=[&](uint32_t h){c.r3.u64=h;__imp__NtClose(c,base);need(c.r3.u64==0&&!rt.getHandle(h),"Native enumerator NtClose failed");};
    const auto initial=rt.handles.size();uint32_t h=create(1);need(rt.handles.size()==initial+1,"Enumerator has no real runtime handle");
    std::memset(rt.pointer(buffer,0x400,true),0xA5,0x400);
    need(enumerate(h,0x134,countOut,0)==ERROR_NO_MORE_FILES&&!PPC_LOAD_U32(countOut),"Synchronous empty enumeration differs");
    for(uint32_t i=0;i<0x400;++i)need(*rt.pointer(buffer+i,1,false)==0xA5,"Empty enumeration changed record bytes");
    close(h);need(enumerate(h,0x134,countOut,0)==ERROR_INVALID_HANDLE,"Stale enumeration handle accepted");
    std::filesystem::create_directories(f.folder);write(f.folder/"A",package(u"Actual fixture entry"));write(f.folder/"B",package(u"B"));write(f.folder/"C",package(u"C"));
    h=create(2);auto weak=std::weak_ptr<Platform::ContentEnumeration>(rt.getHandle(h)->content);
    need(enumerate(h,0x134,countOut,0)==ERROR_INSUFFICIENT_BUFFER&&!PPC_LOAD_U32(countOut),"Short page did not fail without consuming a record");
    need(enumerate(h,2*0x134,countOut,0)==0&&PPC_LOAD_U32(countOut)==2,"Populated original enumerate wrapper failed");
    need(PPC_LOAD_U32(buffer)==1&&PPC_LOAD_U32(buffer+4)==2&&PPC_LOAD_U8(buffer+0x108)=='A'&&PPC_LOAD_U8(buffer+0x134+0x108)=='B',"Original content records differ");
    auto event=std::make_shared<KernelHandle>(CreateEventW(nullptr,TRUE,TRUE,nullptr),KernelHandle::Type::Event);need(event->native!=nullptr,"Fixture event allocation failed");
    const uint32_t eventId=rt.addHandle(event);
    std::memset(rt.pointer(ov-16,0x3C,true),0x5A,0x3C);std::memset(rt.pointer(ov,0x1C,true),0,0x1C);PPC_STORE_U32(ov+0xC,eventId);PPC_STORE_U32(ov+0x14,0x12345678);
    need(enumerate(h,2*0x134,0,ov)==ERROR_IO_PENDING,"Overlapped original wrapper did not return IO_PENDING");
    need(!PPC_LOAD_U32(ov)&&PPC_LOAD_U32(ov+4)==1&&PPC_LOAD_U32(ov+8)==0xFFFFFFFE&&!PPC_LOAD_U32(ov+0x18)&&PPC_LOAD_U32(ov+0x14)==0x12345678,
        "Completed overlapped fields/context preservation differ");
    need(WaitForSingleObject(event->native,0)==WAIT_OBJECT_0&&PPC_LOAD_U8(buffer+0x108)=='C',"Completed content event/last record differs");
    for(uint32_t i=0;i<16;++i)need(*rt.pointer(ov-16+i,1,false)==0x5A&&*rt.pointer(ov+0x1C+i,1,false)==0x5A,"Completion escaped original overlapped bounds");
    need(cpu.invoke(0x82433670,ov,countOut,0)==0&&PPC_LOAD_U32(countOut)==1&&cpu.invoke(0x82432C68,ov)==0,"Original completed-result consumers disagree");
    need(enumerate(h,2*0x134,0,ov)==ERROR_IO_PENDING&&PPC_LOAD_U32(ov)==ERROR_FUNCTION_FAILED&&PPC_LOAD_U32(ov+0x18)==0x80070012&&!PPC_LOAD_U32(ov+4),"Empty overlapped result/status/length differs");
    need(cpu.invoke(0x82433670,ov,countOut,0)==ERROR_FUNCTION_FAILED&&!PPC_LOAD_U32(countOut)&&cpu.invoke(0x82432C68,ov)==0x80070012,"Original end-of-enumeration consumers disagree");
    auto preserved=std::vector<uint8_t>(rt.pointer(ov,0x1C,false),rt.pointer(ov,0x1C,false)+0x1C);
    PPC_STORE_U32(ov+0x10,0x827AFB78);rejects([&]{enumerate(h,2*0x134,0,ov);});PPC_STORE_U32(ov+0x10,0);
    need(!std::memcmp(rt.pointer(ov,0x1C,false),preserved.data(),preserved.size()),"Rejected APC enumeration changed completion state");
    rejects([&]{enumerate(h,2*0x134,countOut,ov);});
    c.r3.u64=h;c.r4.u64=0;c.r5.u64=ov;c.r6.u64=2*0x134;c.r7.u64=0;c.r8.u64=ov;rejects([&]{__imp__XamEnumerate(c,base);});
    close(h);need(weak.expired(),"Closing enumeration retained its native snapshot leases");close(eventId);
    need(rt.handles.size()==initial,"Content enumeration leaked runtime handles");
    for(uint32_t badPage:{0u,4097u,0xFFFFFFFFu})rejects([&]{create(badPage);});
    h=create(1);need(enumerate(h,0x134,countOut,0)==0&&PPC_LOAD_U8(buffer+0x108)=='A',"Fresh enumeration inherited old cursor");close(h);
}
struct Observed {};
void ownerLifecycle(const char* image){
    Fixture f;Runtime rt;rt.load(image);rt.contentRoot=f.installed;PPCContext startup{};rt.initialize(startup);const auto entry=startup;auto* base=rt.base;
    bool observed=false;rt.audioBoundaryObserver=[](uint32_t pc,PPCContext&,uint8_t*){if(pc==0x828166FC)throw Observed{};};
    try{runOriginal(startup,base);}catch(const Observed&){observed=true;}rt.audioBoundaryObserver={};
    need(observed,"Original startup missed the established pre-FX boundary");
    const auto owner=PPC_LOAD_U32(0x82D08B98);
    need(owner&&PPC_LOAD_U32(owner)==0x8215A088&&!PPC_LOAD_U32(owner+0x184)&&!PPC_LOAD_U32(owner+0xC)&&!PPC_LOAD_U8(owner+0x1D5),
        "Actual original content owner is absent or already enumerating");
    const auto baseline=rt.handles.size();
    EngineCpuCalls cpu(entry,base);const auto before=abi(cpu.registers());cpu.invoke(0x827AFCD8,owner,0xFE);unchanged(cpu.registers(),before);
    const auto h=PPC_LOAD_U32(owner+0x184);auto object=rt.getHandle(h);need(object&&object->content,"Original owner did not retain its real enumerator");
    auto weak=std::weak_ptr<Platform::ContentEnumeration>(object->content);object.reset();
    need(PPC_LOAD_U32(owner+0xC)==3&&PPC_LOAD_U32(owner+0x180)==4&&!PPC_LOAD_U8(owner+0x1D5)&&
        PPC_LOAD_U32(owner+0x10)==ERROR_FUNCTION_FAILED&&PPC_LOAD_U32(owner+0x28)==0x80070012&&rt.handles.size()==baseline+1,
        "Original owner enumeration state or empty completion differs");
    cpu.invoke(0x827AFCD8,owner,0xFE);unchanged(cpu.registers(),before);
    need(!PPC_LOAD_U32(owner+0x184)&&!PPC_LOAD_U32(owner+0x180)&&!PPC_LOAD_U32(owner+0xC)&&PPC_LOAD_U8(owner+0x1D5)==1,
        "Original owner poll did not mark scan complete and clear its state");
    need(!rt.getHandle(h)&&weak.expired()&&rt.handles.size()==baseline,"Original owner cleanup retained its enumeration handle/snapshot");
    std::printf("Original content owner %08X: factory/request/poll/CloseHandle completed; native snapshot released\n",owner);
}
void saveOwner(const char* image){
    Fixture f;Runtime rt;rt.load(image);PPCContext entry{};rt.initialize(entry);auto* base=rt.base;
    rt.gameRoot=f.disc;rt.contentRoot=f.installed;rt.configureLocalPlayers(f.root/L"profiles");
    const auto players=rt.localPlayerSource();const auto profile=players->create("Player");players->activate(0,profile.id);
    rt.map(0x50000,0x4000,true,"original save owner fixture");
    EngineCpuCalls cpu(entry,base);constexpr uint32_t owner=0x50000,out=0x51000;
    // Execute the original constructor, including its actual native events.
    need(cpu.invoke(0x8285CC78,owner)==owner&&!PPC_LOAD_U32(owner+0x360),"Original save owner constructor/device sentinel differs");
    PPC_STORE_U32(owner+8,0);const auto initial=rt.handles.size();
    const auto before=abi(cpu.registers());
    need(cpu.invoke(0x8285C418,owner,out)==12&&PPC_LOAD_U32(owner+0x32C)==0xFFFFFFFF&&rt.handles.size()==initial,
        "Original empty save request did not consume and close its actual snapshot");unchanged(cpu.registers(),before);
    const auto folder=f.installed/L"saves"/profile.id/L"45410809";
    saveFixture(folder/L"SlotA","Native test save");
    need(cpu.invoke(0x8285C418,owner,out)==11&&rt.handles.size()==initial+1,"Original save request missed populated native catalog");
    const auto h=PPC_LOAD_U32(owner+0x32C);auto weak=std::weak_ptr<Platform::ContentEnumeration>(rt.getHandle(h)->content);
    need(PPC_LOAD_U32(out)==owner+0x1F0&&PPC_LOAD_U32(out+8)==owner+0x2F0&&PPC_LOAD_U8(owner+0x2F0)=='S',"Original save consumer published different record pointers/name");
    players->signOut(0);
    need(cpu.invoke(0x8285C530,owner)==13&&rt.getHandle(h)!=nullptr,"Sign-out did not preserve original inactive-player handling");
    players->activate(0,profile.id);
    need(cpu.invoke(0x8285C530,owner)==12&&PPC_LOAD_U32(owner+0x32C)==0xFFFFFFFF&&weak.expired()&&rt.handles.size()==initial,
        "Original end-of-save scan did not close its snapshot");
    players->signOut(0);
    auto& c=cpu.registers();c.r3.u64=0;c.r4.u64=0;c.r5.u64=1;c.r6.u64=0;c.r7.u64=1;c.r8.u64=out;c.r9.u64=out+4;
    PPC_STORE_U32(out,0xABABABAB);PPC_STORE_U32(out+4,0xCDCDCDCD);
    __imp__XamContentCreateEnumerator(c,base);
    need(c.r3.u32==uint32_t(HRESULT_FROM_WIN32(ERROR_NO_SUCH_USER))&&PPC_LOAD_U32(out)==0xABABABAB&&PPC_LOAD_U32(out+4)==0xCDCDCDCD&&rt.handles.size()==initial,
        "Inactive save factory fabricated a player/handle or changed outputs");
    for(const uint32_t offset:{0x320u,0x324u})need(cpu.invoke(0x82432CC8,PPC_LOAD_U32(owner+offset))==1,"Original save fixture event cleanup failed");
    std::printf("Original save owner: native constructor, empty/populated request, sign-out, next record and paired close passed\n");
}
void deviceState(const char* image){
    Fixture f;std::filesystem::create_directory(f.installed);
    Runtime rt;rt.load(image);PPCContext entry{};rt.initialize(entry);auto* base=rt.base;
    rt.gameRoot=f.disc;rt.contentRoot=f.installed;rt.map(0x50000,0x1000,true,"native storage state fixture");
    constexpr uint32_t out=0x50100;const auto initial=rt.handles.size();
    const auto saved=PPCFPSCRRegister::getcsr();
    for(bool present:{true,false}){
        if(!present)need(std::filesystem::remove(f.installed),"Remove owned empty storage fixture failed");
        for(uint32_t fp:{0x1F80u,0x3FC0u,0x5F80u,0x9FC0u,0xE07Fu})for(uint32_t id:{0u,1u,2u,0xFFFFFFFFu}){
            PPCContext query{};std::memset(&query,0xA5,sizeof(query));query.r3.u64=id;query.r4.u64=0;
            PPCContext expected;std::memcpy(&expected,&query,sizeof(query));
            expected.r3.u64=id==1&&present?ERROR_SUCCESS:ERROR_DEVICE_NOT_CONNECTED;
            std::memset(rt.pointer(out,128,true),0xC7,128);
            PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(0xDEAD9135);
            __imp__XamContentGetDeviceState(query,base);
            const auto actualFP=PPCFPSCRRegister::getcsr();const auto error=GetLastError();
            PPCFPSCRRegister::restoreHostCSR(saved);
            need(actualFP==fp&&error==0xDEAD9135&&!std::memcmp(&query,&expected,sizeof(query)),"Storage state changed CPU/host state or result");
            for(unsigned i=0;i<128;++i)need(PPC_LOAD_U8(out+i)==0xC7,"Storage status wrote guest bytes");
        }
        EngineCpuCalls cpu(entry,base);PPC_STORE_U32(out,1);
        need(cpu.invoke(0x82432570,1,0)==(present?0u:ERROR_DEVICE_NOT_CONNECTED),"Original storage-state tail ABI differs");
        need(cpu.invoke(0x8285B518,0,out)==(present?0u:2u),"Original save-load availability branch differs");
        PPC_STORE_U32(out,0);need(cpu.invoke(0x8285B518,0,out)==2,"Original absent-device branch differs");
    }
    for(uint32_t overlap:{1u,out,0xFFFFFFFFu}){
        PPCContext query{};query.r3.u64=1;query.r4.u64=overlap;const auto before=query.r3.u64;
        rejects([&]{__imp__XamContentGetDeviceState(query,base);});need(query.r3.u64==before,"Unqualified async query published a result");
    }
    PPCContext query{};query.r3.u64=1;
    rejects([&]{__imp__XamContentGetDeviceState(query,base+1);});
    rt.stopping=true;rejects([&]{__imp__XamContentGetDeviceState(query,base);});rt.stopping=false;
    need(rt.handles.size()==initial&&!std::filesystem::exists(f.installed),"Storage status created files or leaked guest handles");
    std::printf("Storage state: fresh present/missing folder, foreign IDs, original load branch, CPU/FP/error preservation and async rejection passed\n");
}
void deviceData(const char* image){
    Fixture f;std::filesystem::create_directory(f.installed);Runtime rt;rt.load(image);PPCContext entry{};rt.initialize(entry);auto* base=rt.base;
    rt.gameRoot=f.disc;rt.contentRoot=f.installed;rt.map(0x50000,0x4000,true,"native storage metadata fixture");rt.map(0x60000,0x1000,false,"readonly native storage metadata fixture");
    constexpr uint32_t out=0x50100,device=0x50200,blocks=0x50300;const auto initial=rt.handles.size();
    const auto saved=PPCFPSCRRegister::getcsr();
    for(uint32_t fp:{0x1F80u,0x3FC0u,0x5F80u,0x9FC0u,0xE07Fu}){
        std::memset(rt.pointer(out-16,0x70,true),0xA7,0x70);
        PPCContext c{};std::memset(&c,0xA5,sizeof(c));c.r3.u64=1;c.r4.u64=out;
        PPCContext expected;std::memcpy(&expected,&c,sizeof(c));expected.r3.u64=0;
        PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(0xDEAD1234);__imp__XamContentGetDeviceData(c,base);
        const auto actualFP=PPCFPSCRRegister::getcsr();const auto error=GetLastError();PPCFPSCRRegister::restoreHostCSR(saved);
        need(actualFP==fp&&error==0xDEAD1234&&!std::memcmp(&c,&expected,sizeof(c)),"Storage metadata changed CPU/host state");
        ULARGE_INTEGER available{},total{},free{};std::array<wchar_t,261> label{};
        need(GetDiskFreeSpaceExW(f.installed.c_str(),&available,&total,&free)&&GetVolumeInformationW(f.installed.root_path().c_str(),label.data(),DWORD(label.size()),nullptr,nullptr,nullptr,nullptr,0),"Independent native volume query failed");
        need(PPC_LOAD_U32(out)==1&&PPC_LOAD_U32(out+4)==1&&PPC_LOAD_U64(out+8)==total.QuadPart&&PPC_LOAD_U64(out+16)<=total.QuadPart,
            "Storage metadata differs from actual native volume totals");
        const std::wstring name=label[0]?std::wstring(label.data()):f.installed.root_path().native();
        const auto units=std::min<size_t>(27,name.size());for(size_t i=0;i<units;++i)need(PPC_LOAD_U16(out+24+uint32_t(i)*2)==uint16_t(name[i]),"Native volume label UTF16 differs");
        need(!PPC_LOAD_U16(out+24+uint32_t(units)*2),"Native volume name lacks terminator");
        for(uint32_t i=0;i<16;++i)need(PPC_LOAD_U8(out-16+i)==0xA7&&PPC_LOAD_U8(out+0x50+i)==0xA7,"Storage metadata escaped80-byte ABI");
    }
    EngineCpuCalls cpu(entry,base);auto& c=cpu.registers();PPC_STORE_U32(device,1);
    const auto original=[&](bool optional){c.r3.u64=0;c.r4.u64=device;c.r5.u64=optional?0:blocks;c.r6.u64=optional?0:blocks+8;c.r7.u64=blocks+16;c.r8.u64=blocks+24;
        const auto before=abi(c);const uint32_t sp=c.r1.u32;
        need(cpu.invoke(0x8285CDF8)==0,"Original native storage capacity consumer failed");unchanged(c,before);
        // Read the exact80-byte result in this original call's released stack
        // frame. The original itself converts its queried byte counts to4KiB
        // blocks; comparing separate OS queries would race unrelated disk I/O.
        const auto actualAvailable=PPC_LOAD_U64(sp-0x70)>>12,actualTotal=PPC_LOAD_U64(sp-0x78)>>12;
        need(PPC_LOAD_U64(blocks+16)==actualAvailable&&PPC_LOAD_U64(blocks+24)==actualTotal,"Original native capacity conversion differs");
        if(!optional)need(PPC_LOAD_U64(blocks)==actualAvailable&&PPC_LOAD_U64(blocks+8)==actualAvailable,"Original optional capacity outputs differ");
    };
    original(false);original(true);
    std::array<wchar_t,261> volume{};
    need(GetVolumeInformationW(f.installed.root_path().c_str(),volume.data(),DWORD(volume.size()),nullptr,nullptr,nullptr,nullptr,0)!=FALSE,"Independent volume label unavailable");
    const std::wstring name=volume[0]?std::wstring(volume.data()):f.installed.root_path().native();
    constexpr uint32_t nameOut=0x50402; // UTF16 needs two-byte, not four-byte alignment.
    const auto required=uint32_t(name.size()+1);
    for(uint32_t fp:{0x1F80u,0x3FC0u,0x5F80u,0x9FC0u,0xE07Fu})for(uint32_t capacity:{0u,required-1,required,required+8}){
        std::memset(rt.pointer(nameOut-2,128,true),0xA7,128);PPCContext query{};std::memset(&query,0xA5,sizeof(query));
        query.r3.u64=1;query.r4.u64=nameOut;query.r5.u64=capacity;PPCContext expected;std::memcpy(&expected,&query,sizeof(query));
        expected.r3.u64=capacity<required?ERROR_INSUFFICIENT_BUFFER:0;
        PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(0xDEAD4321);__imp__XamContentGetDeviceName(query,base);
        const auto actualFP=PPCFPSCRRegister::getcsr();const auto error=GetLastError();PPCFPSCRRegister::restoreHostCSR(saved);
        need(actualFP==fp&&error==0xDEAD4321&&!std::memcmp(&query,&expected,sizeof(query)),"Storage name changed CPU/host state");
        for(uint32_t i=0;i<64;++i){const uint16_t expectedUnit=capacity>=required&&i>0&&i<=required?(i==required?0:uint16_t(name[i-1])):0xA7A7;
            need(PPC_LOAD_U16(nameOut-2+i*2)==expectedUnit,"Storage name endian/terminator/untouched-tail extent differs");}
    }
    need(cpu.invoke(0x82432588,1,nameOut,required)==0&&cpu.invoke(0x82C9E1B0,nameOut)==name.size(),"Original storage name wrapper/UTF16 consumer failed");
    const auto rejectName=[&](uint32_t id,uint32_t destination,uint32_t capacity){PPCContext query{};query.r3.u64=id;query.r4.u64=destination;query.r5.u64=capacity;
        rejects([&]{__imp__XamContentGetDeviceName(query,base);});};
    rejectName(0,nameOut,28);rejectName(2,nameOut,28);rejectName(1,0,28);rejectName(1,nameOut+1,28);rejectName(1,0x60000,required);rejectName(1,nameOut,32769);
    const auto reject=[&](uint32_t id,uint32_t output){PPCContext ctx{};ctx.r3.u64=id;ctx.r4.u64=output;
        std::array<uint8_t,0x50> bytes{};std::memcpy(bytes.data(),rt.pointer(out,0x50,false),bytes.size());
        rejects([&]{__imp__XamContentGetDeviceData(ctx,base);});need(!std::memcmp(bytes.data(),rt.pointer(out,0x50,false),bytes.size()),"Rejected storage query changed output");};
    reject(0,out);reject(2,out);reject(0xFFFFFFFF,out);reject(1,0);reject(1,out+1);reject(1,0x60000);
    need(rt.handles.size()==initial&&std::filesystem::is_empty(f.installed),"Storage query retained handles or created files");
    std::printf("Original native storage data:actual volume metadata,80-byte ABI,64-bit counts and original4KiB conversion passed\n");
}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try{need(argc==2,"Original image required");catalog();saveCatalog();storageEligibility();bridge(argv[1]);ownerLifecycle(argv[1]);saveOwner(argv[1]);deviceData(argv[1]);deviceState(argv[1]);std::printf("PASS native content enumeration:%zu checks; real file snapshots,storage eligibility/metadata,original marketplace/save consumers,populated/empty paging,overlapped events,malformed input and original owner release;metadata only,ALL MUTED\n",checks);return 0;}
    catch(const std::exception& e){std::fprintf(stderr,"FAIL content enumeration:%zu checks:%s\n",checks,e.what());return 1;}
}
