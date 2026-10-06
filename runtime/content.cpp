#include "runtime.h"
#include "native_content.h"
#include "native_local_players.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace Simpsons::Platform {uint32_t originalLanguageForWindowsUi(uint16_t) noexcept;}
namespace {
using namespace Simpsons;
constexpr uint32_t recordSize=Platform::ContentSnapshot::recordSize;
struct HostState {
    uint32_t csr=PPCFPSCRRegister::getcsr();DWORD error=GetLastError();
    HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~HostState(){PPCFPSCRRegister::restoreHostCSR(csr);SetLastError(error);}
};
Runtime& runtime(uint8_t* base){if(!active||base!=active->base)throw Failure("Invalid native content runtime");active->checkRunning();return *active;}
void need(bool b,const char* why){if(!b)throw Failure(std::string("Native content enumeration: ")+why);}
void output(Runtime& rt,uint32_t p,uint32_t size){need(p&&!(p&3),"unaligned or absent output");rt.pointer(p,size,true);}
bool overlap(uint32_t a,uint32_t n,uint32_t b,uint32_t m){return uint64_t(a)<uint64_t(b)+m&&uint64_t(b)<uint64_t(a)+n;}
}
PPC_FUNC(__imp__XamContentGetDeviceState){
    HostState host;auto& rt=runtime(base);const auto id=ctx.r3.u32;
    need(!ctx.r4.u32,"asynchronous storage status is not qualified");
    // Only native folder1 is registered as selected save storage. Query its
    // real pinned directory/volume on every request; no dummy device state.
    if(id!=1){ctx.r3.u64=ERROR_DEVICE_NOT_CONNECTED;return;}
    need(rt.gameRoot.is_absolute(),"original image data root is absent");
    const auto root=rt.contentRoot.empty()?rt.gameRoot.parent_path()/"saves":rt.contentRoot;
    const bool available=Platform::nativeStorageAvailable(root);
    ctx.r3.u64=available?ERROR_SUCCESS:ERROR_DEVICE_NOT_CONNECTED;
    std::fprintf(stderr,"[NATIVE CONTENT] device=1 state=%u folder=%ls; fresh native storage availability\n",ctx.r3.u32,root.c_str());
}
PPC_FUNC(__imp__XamContentGetDeviceData){
    HostState host;auto& rt=runtime(base);const auto id=ctx.r3.u32,out=ctx.r4.u32;
    need(id==1,"storage details for an unqualified native device");output(rt,out,0x50);
    need(rt.gameRoot.is_absolute(),"original image data root is absent");
    const auto root=rt.contentRoot.empty()?rt.gameRoot.parent_path()/"saves":rt.contentRoot;
    const auto storage=Platform::queryNativeStorage(root);
    std::array<uint8_t,0x50> record{};
    const auto put=[&](size_t offset,uint64_t value,size_t length){for(size_t i=0;i<length;++i)record[offset+i]=uint8_t(value>>(8*(length-i-1)));};
    // Original device-data ABI, not an emulated hardware object. Class1 maps
    // the actual fixed-volume folder already qualified by the native selector.
    put(0,1,4);put(4,1,4);put(8,storage.totalBytes,8);put(16,storage.availableBytes,8);
    const auto name=storage.volumeName.empty()?storage.path.root_path().native():storage.volumeName;
    size_t count=std::min<size_t>(27,name.size());
    if(count<name.size()&&count&&name[count-1]>=0xD800&&name[count-1]<=0xDBFF&&name[count]>=0xDC00&&name[count]<=0xDFFF)--count;
    for(size_t i=0;i<count;++i)put(24+2*i,uint16_t(name[i]),2);
    std::memcpy(rt.pointer(out,unsigned(record.size()),true),record.data(),record.size());ctx.r3.u64=0;
    std::fprintf(stderr,"[NATIVE CONTENT] device=1 fixed folder=%ls total=%llu available=%llu label=%ls; native volume metadata\n",storage.path.c_str(),
        static_cast<unsigned long long>(storage.totalBytes),static_cast<unsigned long long>(storage.availableBytes),name.c_str());
}
PPC_FUNC(__imp__XamContentGetDeviceName){
    HostState host;auto& rt=runtime(base);const auto id=ctx.r3.u32,out=ctx.r4.u32,capacity=ctx.r5.u32;
    need(id==1,"storage name for an unqualified native device");
    need(out&&!(out&1)&&capacity<=32768,"invalid native storage name buffer");
    need(rt.gameRoot.is_absolute(),"original image data root is absent");
    const auto root=rt.contentRoot.empty()?rt.gameRoot.parent_path()/"saves":rt.contentRoot;
    const auto storage=Platform::queryNativeStorage(root);
    const auto name=storage.volumeName.empty()?storage.path.root_path().native():storage.volumeName;
    if(capacity<name.size()+1){ctx.r3.u64=ERROR_INSUFFICIENT_BUFFER;return;}
    rt.pointer(out,capacity*2,true);
    for(uint32_t i=0;i<name.size();++i)PPC_STORE_U16(out+i*2,uint16_t(name[i]));
    PPC_STORE_U16(out+uint32_t(name.size())*2,0);ctx.r3.u64=0;
    std::fprintf(stderr,"[NATIVE CONTENT] device=1 name=%ls capacity=%u UTF16 units; actual fixed-volume label\n",name.c_str(),capacity);
}
PPC_FUNC(__imp__XamContentCreateEnumerator){
    HostState host;auto& rt=runtime(base);
    const auto user=ctx.r3.u32,device=ctx.r4.u32,type=ctx.r5.u32,flags=ctx.r6.u32,perPage=ctx.r7.u32,sizeOut=ctx.r8.u32,handleOut=ctx.r9.u32;
    const bool marketplace=user==0xFE&&device<=2&&type==2;
    const bool savedGames=user<4&&device<=1&&type==1;
    if((!marketplace&&!savedGames)||flags){
        std::fprintf(stderr,"[NATIVE CONTENT] unsupported user=%08X device=%08X type=%08X flags=%08X\n",user,device,type,flags);
        need(false,"unsupported user/device/type/flags");
    }
    need(perPage&&perPage<=4096,"unsupported page size");output(rt,handleOut,4);if(sizeOut){output(rt,sizeOut,4);need(sizeOut!=handleOut,"aliased factory outputs");}
    if(uint64_t(perPage)*recordSize>UINT32_MAX)throw Failure("Content page exceeds 32-bit size");
    need(rt.gameRoot.is_absolute(),"original image data root is absent");
    const auto installed=rt.contentRoot.empty()?rt.gameRoot.parent_path()/"saves":rt.contentRoot;
    auto enumeration=std::make_shared<Platform::ContentEnumeration>();
    // The loaded original XEX's execution ID is45410809. Snapshot the actual
    // selected native profile; a later sign-out cannot change this ownership.
    if(savedGames){
        const auto profile=rt.localPlayerSource()->profile(user);
        if(!profile){ctx.r3.u64=uint32_t(HRESULT_FROM_WIN32(ERROR_NO_SUCH_USER));return;}
        enumeration->snapshot=Platform::scanNativeSaves(installed,profile->id,0x45410809,device);
    }else enumeration->snapshot=Platform::scanContent(installed,rt.gameRoot/"Content",0x45410809,
        Platform::originalLanguageForWindowsUi(GetUserDefaultUILanguage()),device);
    enumeration->perPage=perPage;
    HANDLE native=CreateEventW(nullptr,TRUE,FALSE,nullptr);need(native!=nullptr,"native enumeration handle allocation failed");
    std::shared_ptr<KernelHandle> object;try{object=std::make_shared<KernelHandle>(native,KernelHandle::Type::ContentEnumerator);}catch(...){CloseHandle(native);throw;}
    object->content=std::move(enumeration);const auto count=object->content->snapshot.records.size();
    const uint32_t id=rt.addHandle(std::move(object));if(sizeOut)PPC_STORE_U32(sizeOut,perPage*recordSize);PPC_STORE_U32(handleOut,id);ctx.r3.u64=0;
    std::fprintf(stderr,"[NATIVE CONTENT] create handle=%08X user=%08X device=%u type=%u entries=%zu page=%u bytes=%u; real filesystem snapshot\n",id,user,device,type,count,perPage,perPage*recordSize);
}
PPC_FUNC(__imp__XamEnumerate){
    HostState host;auto& rt=runtime(base);
    const uint32_t handle=ctx.r3.u32,flags=ctx.r4.u32,buffer=ctx.r5.u32,length=ctx.r6.u32,countOut=ctx.r7.u32,ov=ctx.r8.u32;
    need(!flags,"unsupported enumeration control");
    auto object=rt.getHandle(handle);if(!object||object->type!=KernelHandle::Type::ContentEnumerator||!object->content){ctx.r3.u64=ERROR_INVALID_HANDLE;return;}
    std::lock_guard lock(object->stateMutex);auto& state=*object->content;
    need(!(countOut&&ov),"count and overlapped outputs are mutually exclusive");
    if(countOut)output(rt,countOut,4);
    std::shared_ptr<KernelHandle> event;
    if(ov){
        output(rt,ov,0x1C);need(!PPC_LOAD_U32(ov+0x10),"completion callback needs an unimplemented guest APC bridge");
        const auto id=PPC_LOAD_U32(ov+0xC);if(id){event=rt.getHandle(id);need(event&&event->type==KernelHandle::Type::Event,"overlapped event is not a live event handle");}
    }
    need(buffer&&!(buffer&3),"absent or unaligned record buffer");rt.pointer(buffer,length,true);
    need(!overlap(buffer,length,countOut,countOut?4:0)&&!overlap(buffer,length,ov,ov?0x1C:0),"record buffer aliases completion outputs");
    uint32_t status=0,count=0;
    if(length<state.perPage*recordSize)status=ERROR_INSUFFICIENT_BUFFER;
    else {
        if(state.cursor>state.snapshot.records.size())throw Failure("Content cursor out of range");
        count=uint32_t(std::min<size_t>(state.perPage,state.snapshot.records.size()-state.cursor));if(!count)status=ERROR_NO_MORE_FILES;}
    // Enumeration operates on owned immutable metadata. Completion is inline;
    // async calls still return IO_PENDING and publish a completed XOVERLAPPED,
    // a legal immediately-completed request. No artificial delay or worker is
    // needed, and no guest pointer survives return. Reset then signal events.
    if(event) {if(!event->native)throw Failure("Overlapped event has no native handle");need(ResetEvent(event->native)!=FALSE,"reset overlapped event failed");}
    for(uint32_t i=0;i<count;++i) {
        uint64_t offset=uint64_t(i)*recordSize;
        if(offset+recordSize>length)throw Failure("Content record exceeds caller buffer");
        std::memcpy(rt.pointer(buffer+uint32_t(offset),recordSize,true),state.snapshot.records[state.cursor+i].data(),recordSize);
    }
    state.cursor+=count;
    if(ov){
        const uint32_t extended=status?0x80070000u|status:0;
        PPC_STORE_U32(ov+4,count);PPC_STORE_U32(ov+8,0xFFFFFFFE);PPC_STORE_U32(ov+0x18,extended);
        PPC_STORE_U32(ov,status?ERROR_FUNCTION_FAILED:0);
        if(event) {if(!event->native)throw Failure("Completed content event has no native handle");need(SetEvent(event->native)!=FALSE,"signal completed content event failed");}ctx.r3.u64=ERROR_IO_PENDING;
    }else{if(countOut)PPC_STORE_U32(countOut,count);ctx.r3.u64=status;}
    std::fprintf(stderr,"[NATIVE CONTENT] enumerate handle=%08X records=%u status=%u cursor=%zu/%zu completion=%s\n",
        handle,count,status,state.cursor,state.snapshot.records.size(),ov?"inline overlapped":"synchronous");
}
