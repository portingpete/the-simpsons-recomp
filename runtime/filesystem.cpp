#include "filesystem.h"
#include "native_saves.h"
#include <winternl.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <set>
#include <utility>

namespace {
std::set<uint32_t>& streamTraceHandles() { static std::set<uint32_t> handles; return handles; }
constexpr uint32_t success=0, invalidInfo=0xc0000003, lengthMismatch=0xc0000004;
constexpr uint32_t accessViolation=0xc0000005, invalidHandle=0xc0000008;
constexpr uint32_t invalidParameter=0xc000000d, accessDenied=0xc0000022;
constexpr uint32_t badName=0xc0000033, notSupported=0xc00000bb;
constexpr uint32_t isDirectory=0xc00000ba, notDirectory=0xc0000103;
constexpr uint32_t nameTooLong=0xc0000106, reparseDenied=0xc000050b;
constexpr uint32_t syncNonalert=0x20, directoryOption=1, nonDirectoryOption=0x40;
constexpr uint32_t noBuffering=FILE_NO_INTERMEDIATE_BUFFERING;
static_assert(noBuffering==8); // Original frontend opens request 0x68.
constexpr uint32_t freeSpaceQuery=0x00800000; // FILE_OPEN_FOR_FREE_SPACE_QUERY: capture opening user for quota queries.
struct Status { uint32_t value; };
[[noreturn]] void fail(uint32_t status) { throw Status{status}; }
[[noreturn]] void unsupported(const char* feature, uint32_t value) {
    fprintf(stderr,"[FS] unsupported %s=0x%08X\n",feature,value);
    fail(notSupported);
}
template<class T> T native(const char* name) {
    HMODULE mod=GetModuleHandleW(L"ntdll.dll");
    if(!mod) throw Simpsons::Failure("ntdll.dll module handle unavailable");
    auto proc=GetProcAddress(mod,name);
    if(!proc) throw Simpsons::Failure(std::string("Required native file service unavailable: ")+name);
    T fn{};std::memcpy(&fn,&proc,sizeof(fn));
    return fn;
}
using CreateFn=NTSTATUS(NTAPI*)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,
    PLARGE_INTEGER,ULONG,ULONG,ULONG,ULONG,PVOID,ULONG);
using ReadFn=NTSTATUS(NTAPI*)(HANDLE,HANDLE,PVOID,PVOID,PIO_STATUS_BLOCK,PVOID,ULONG,PLARGE_INTEGER,PULONG);
using QueryFn=NTSTATUS(NTAPI*)(HANDLE,PIO_STATUS_BLOCK,PVOID,ULONG,FILE_INFORMATION_CLASS);
// FS_INFORMATION_CLASS is a 32-bit enum, absent from the Windows user-mode SDK header.
using QueryVolumeFn=NTSTATUS(NTAPI*)(HANDLE,PIO_STATUS_BLOCK,PVOID,ULONG,ULONG);
using SetFn=NTSTATUS(NTAPI*)(HANDLE,PIO_STATUS_BLOCK,PVOID,ULONG,FILE_INFORMATION_CLASS);
uint32_t winError() {
    switch(DWORD error=GetLastError()) {
    case ERROR_FILE_NOT_FOUND: return 0xc0000034;
    case ERROR_PATH_NOT_FOUND: return 0xc000003a;
    case ERROR_ACCESS_DENIED: return accessDenied;
    case ERROR_SHARING_VIOLATION: return 0xc0000043;
    case ERROR_INVALID_HANDLE: return invalidHandle;
    case ERROR_DIRECTORY: return notDirectory;
    case ERROR_INVALID_NAME: return badName;
    default: fprintf(stderr,"[FS] native Win32 failure=%lu\n",error); return 0xc0000001;
    }
}
void check(NTSTATUS status) { if(status<0) fail(uint32_t(status)); }
struct NativeHandle {
    HANDLE value{};
    ~NativeHandle() { if(value && value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
    HANDLE release() { return std::exchange(value,nullptr); }
};
using Object=std::shared_ptr<Simpsons::KernelHandle>;
// Parent directory handles deny deletion/renaming for the lifetime of the file.
// shared_ptr retains this derived destructor even though KernelHandle is not polymorphic.
struct PinnedFile final : Simpsons::KernelHandle {
    std::vector<Object> parents;
    PinnedFile(HANDLE h,std::vector<Object> p):KernelHandle(h,Type::File),parents(std::move(p)) {}
};
std::mutex fileMutex; // Serializes synchronous position/query/read operations.
std::unordered_map<Simpsons::KernelHandle*,std::weak_ptr<Simpsons::KernelHandle>> owned;

uint8_t* guest(uint8_t* base,uint64_t address,uint32_t size,bool write) {
    if(!address || address>UINT32_MAX || uint64_t(size)>0x100000000ull-address) fail(accessViolation);
    try { return PPCGuestPointer(base,uint32_t(address),size,write); }
    catch(const Simpsons::Failure&) {
        // PPCGuestPointer also checks runtime/window shutdown. Preserve that
        // exception so guest error handling cannot turn shutdown into retries.
        if(Simpsons::active && Simpsons::active->stopping.load(std::memory_order_acquire)) throw;
        fail(accessViolation);
    }
}
uint16_t be16(const uint8_t* p) { return uint16_t((uint16_t(p[0])<<8)|p[1]); }
uint32_t be32(const uint8_t* p) { return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3]; }
uint64_t be64(const uint8_t* p) { return (uint64_t(be32(p))<<32)|be32(p+4); }
void put32(uint8_t* p,uint32_t value) { for(unsigned i=0;i<4;++i) p[i]=uint8_t(value>>(24-i*8)); }
void put64(uint8_t* p,uint64_t value) { put32(p,uint32_t(value>>32)); put32(p+4,uint32_t(value)); }
struct IoResult {
    PPCContext& ctx;
    uint8_t* block{};
    void finish(uint32_t status,uint32_t information=0) {
        if(block) {put32(block,status);put32(block+4,information);}
        ctx.r3.u64=status;
    }
};
template<class F> void dispatch(PPCContext& ctx,uint8_t* base,uint32_t iosb,F action) {
    struct HostState {uint32_t fp=PPCFPSCRRegister::getcsr();DWORD error=GetLastError();
        HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}} host;
    if(!Simpsons::active||base!=Simpsons::active->base)throw Simpsons::Failure("Invalid native filesystem runtime");
    Simpsons::active->checkRunning();
    IoResult io{ctx};
    try {
        if(iosb) io.block=guest(base,iosb,8,true);
        std::lock_guard lock(fileMutex);
        action(io);
    } catch(const Status& e) { io.finish(e.value); }
    catch(const Simpsons::Platform::NativeSaveError& e) {
        uint32_t status=0;
        switch(e.code){
        case ERROR_ACCESS_DENIED:status=accessDenied;break;case ERROR_SHARING_VIOLATION:status=0xc0000043;break;
        case ERROR_FILE_NOT_FOUND:status=0xc0000034;break;case ERROR_PATH_NOT_FOUND:status=0xc000003a;break;
        case ERROR_FILE_EXISTS:case ERROR_ALREADY_EXISTS:status=0xc0000035;break;
        case ERROR_INVALID_PARAMETER:case ERROR_INVALID_DATA:status=invalidParameter;break;
        case ERROR_INVALID_HANDLE:status=invalidHandle;break;case ERROR_DISK_FULL:status=0xc000007f;break;
        default:throw Simpsons::Failure("Unqualified native save filesystem error: "+std::to_string(e.code)+" "+e.what());
        }
        io.finish(status);
    }
}
bool equalPath(const std::wstring& a,const std::wstring& b) {
    return CompareStringOrdinal(a.data(),int(a.size()),b.data(),int(b.size()),TRUE)==CSTR_EQUAL;
}
std::wstring finalPath(HANDLE handle) {
    DWORD length=GetFinalPathNameByHandleW(handle,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!length) fail(winError());
    std::wstring path(length,L'\0');
    DWORD actual=GetFinalPathNameByHandleW(handle,path.data(),length,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!actual || actual>=length) fail(actual?accessDenied:winError());
    path.resize(actual);
    while(path.size()>7 && path.back()==L'\\') path.pop_back();
    return path;
}
bool directory(HANDLE handle) {
    FILE_ATTRIBUTE_TAG_INFO attrs{};
    if(!GetFileInformationByHandleEx(handle,FileAttributeTagInfo,&attrs,sizeof(attrs))) fail(winError());
    if(attrs.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) fail(reparseDenied);
    if(GetFileType(handle)!=FILE_TYPE_DISK) fail(accessDenied);
    return (attrs.FileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
}
Object rootObject(Simpsons::Runtime& rt,std::wstring& path,const std::filesystem::path& overrideRoot={}) {
    // gameRoot is established/canonicalized by the parent at initialization.
    // Check the opened object against that exact path, never a freshly resolved
    // replacement target if a parent directory has been switched to a junction.
    if(!rt.gameRoot.is_absolute()) fail(accessDenied);
    std::wstring expected=(overrideRoot.empty()?rt.gameRoot:overrideRoot).lexically_normal().native();
    if(expected.size()<3 || expected[1]!=L':' || expected[2]!=L'\\') fail(accessDenied);
    expected=L"\\\\?\\"+expected;
    while(expected.size()>7 && expected.back()==L'\\') expected.pop_back();
    NativeHandle root{CreateFileW(expected.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|SYNCHRONIZE,
        FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
    if(root.value==INVALID_HANDLE_VALUE) fail(winError());
    if(!directory(root.value)) fail(notDirectory);
    path=finalPath(root.value);
    if(!equalPath(path,expected)) fail(accessDenied);
    auto object=std::make_shared<Simpsons::KernelHandle>(root.value,Simpsons::KernelHandle::Type::File);
    root.release(); return object;
}
Object fileObject(Simpsons::Runtime& rt,uint32_t id) {
    auto object=rt.getHandle(id);
    if(!object || object->type!=Simpsons::KernelHandle::Type::File) fail(invalidHandle);
    auto found=owned.find(object.get());
    if(found==owned.end() || found->second.lock()!=object) fail(invalidHandle);
    return object;
}
bool underRoot(const std::wstring& root,const std::wstring& path) {
    return equalPath(root,path) || (path.size()>root.size() && path[root.size()]==L'\\' &&
        equalPath(root,path.substr(0,root.size())));
}
// Observer-only provenance of what the guest ACTUALLY opened and read. It never
// authorizes, retries or changes I/O; failures to observe are contained. The
// declared (guest-supplied) alias is kept distinct from the normalized path of
// the opened native object, and every read reports the real offset and length.
// Immutable facts of one open, shared with the recent-read ring so a read can outlive its handle.
struct OpenInfo {
    uint64_t openId{},extent{};
    std::string declared,relative,root;
};
struct OpenProvenance {
    uint64_t openId{},extent{},fileIndex{},reads{};
    uint32_t volume{},access{},share{},options{};
    std::string declared,relative,root;
    std::shared_ptr<const OpenInfo> info;
};
// Bounded ring of the most recent audited reads and where each deposited its bytes.
struct ReadRecord {
    uint64_t sequence{},openId{},readOrdinal{},fileOffset{};
    uint32_t destination{},completed{};
    std::shared_ptr<const OpenInfo> info;
};
std::mutex recentReadMutex; // Leaf lock: never held while taking another.
std::array<ReadRecord,512> recentReads;
uint64_t recentReadCount{};
std::unordered_map<Simpsons::KernelHandle*,OpenProvenance> provenance; // Guarded by fileMutex.
uint64_t nextOpenId{};
std::pair<std::string,const char*> auditRelative(Simpsons::Runtime& rt,const std::wstring& opened) {
    const std::pair<const std::filesystem::path*,const char*> roots[]={{&rt.gameRoot,"game"},{&rt.nativeFrontendRoot,"native-frontend"}};
    for(const auto& [rootPath,kind]:roots) {
        if(rootPath->empty()) continue;
        const std::wstring root=L"\\\\?\\"+rootPath->lexically_normal().native();
        if(!underRoot(root,opened) || opened.size()<=root.size()+1) continue;
        std::string relative;
        for(wchar_t ch:opened.substr(root.size()+1)) {
            if(ch==L'\\') relative+='/';
            else if(ch>=L'A' && ch<=L'Z') relative+=char(ch-L'A'+'a');
            else relative+=ch<128?char(ch):'?';
        }
        return {relative,kind};
    }
    return {"(outside-known-roots)","unknown"};
}
void recordOpen(Simpsons::Runtime& rt,Simpsons::KernelHandle& object,const std::string& declared,uint32_t guestHandle,
                uint32_t access,uint32_t share,uint32_t options,uint32_t caller) noexcept {
    if(!rt.resourceAudit.active()) return;
    try {
        if(directory(object.native)) return;
        OpenProvenance p;p.openId=++nextOpenId;p.access=access;p.share=share;p.options=options;p.declared=declared;
        const auto relative=auditRelative(rt,finalPath(object.native));
        p.relative=relative.first;p.root=relative.second;
        BY_HANDLE_FILE_INFORMATION info{};
        if(GetFileInformationByHandle(object.native,&info)) {
            p.volume=info.dwVolumeSerialNumber;p.fileIndex=(uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow;
            p.extent=(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
        }
        char parameters[640],ownership[160],instance[960];
        std::snprintf(parameters,sizeof(parameters),"root=%s declared=%.400s access=%08X share=%X options=%08X",
            p.root.c_str(),p.declared.c_str(),access,share,options);
        std::snprintf(ownership,sizeof(ownership),"admission=native-open qualified=under-known-root reparse=none");
        std::snprintf(instance,sizeof(instance),"handle=%08X open_id=%llu extent=%llu volume=%08X file_index=%016llX",
            guestHandle,static_cast<unsigned long long>(p.openId),static_cast<unsigned long long>(p.extent),p.volume,
            static_cast<unsigned long long>(p.fileIndex));
        const std::string asset="file:"+p.relative;
        p.info=std::make_shared<const OpenInfo>(OpenInfo{p.openId,p.extent,p.declared,p.relative,p.root});
        provenance[&object]=std::move(p);
        rt.resourceAudit.lifecycle("file_open",asset,caller,parameters,ownership,0,instance);
    } catch(...) {std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] file open capture failed\n");}
}
void recordRead(Simpsons::Runtime& rt,Simpsons::KernelHandle& object,uint32_t guestHandle,uint32_t caller,bool explicitOffset,
                uint64_t offset,uint32_t requested,uint32_t completed,uint32_t status,uint32_t destination) noexcept {
    try {
        const auto found=provenance.find(&object);
        if(found==provenance.end()) return;
        auto& p=found->second;++p.reads;
        {
            std::lock_guard ring(recentReadMutex);
            recentReads[recentReadCount%recentReads.size()]=ReadRecord{recentReadCount+1,p.openId,p.reads,offset,destination,completed,p.info};
            ++recentReadCount;
        }
        char parameters[640],ownership[160],instance[960];
        std::snprintf(parameters,sizeof(parameters),"root=%s status=%08X offset_mode=%s",p.root.c_str(),status,explicitOffset?"explicit":"position");
        std::snprintf(ownership,sizeof(ownership),"admission=native-read handle=owned-open");
        std::snprintf(instance,sizeof(instance),
            "handle=%08X open_id=%llu read_ordinal=%llu offset=%llu requested=%u completed=%u extent=%llu declared=%.400s",
            guestHandle,static_cast<unsigned long long>(p.openId),static_cast<unsigned long long>(p.reads),
            static_cast<unsigned long long>(offset),requested,completed,static_cast<unsigned long long>(p.extent),p.declared.c_str());
        rt.resourceAudit.observe("file_read","file:"+p.relative,caller,parameters,ownership,0,instance);
    } catch(...) {std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] file read capture failed\n");}
}
void observeLevelStream(Simpsons::Runtime& rt,const Simpsons::KernelHandle& object,const std::string& requested) {
    if(requested.size()<4) return;
    std::string lower=requested;
    for(char& ch:lower) if(ch>='A' && ch<='Z') ch=char(ch-'A'+'a');
    if(!lower.ends_with(".str")) return;
    // Resolve the opened file rather than trusting a request relative to an
    // arbitrary directory handle. Only the gameflow's top-level map streams
    // can change the active-level gate; nested resource streams cannot.
    try {
        const std::wstring root=L"\\\\?\\"+rt.gameRoot.lexically_normal().native();
        const auto opened=finalPath(object.native);
        if(!underRoot(root,opened) || opened.size()<=root.size()+1) return;
        std::wstring relative=opened.substr(root.size()+1);
        for(wchar_t& ch:relative) if(ch>=L'A' && ch<=L'Z') ch=wchar_t(ch-L'A'+L'a');
        constexpr std::array<const wchar_t*,18> maps={
            L"spr_hub",L"loc",L"brt",L"eighty_bites",L"tree_hugger",L"mob_rules",
            L"cheater",L"dayofthedolphins",L"colossaldonut",L"bargainbin",
            L"bigsuperhappy",L"dayspringfieldstoodstill",L"gamehub",
            L"grand_theft_scratchy",L"medal_of_homer",L"meetthyplayer",
            L"neverquest",L"rhymes"};
        for(const wchar_t* stem:maps) {
            const std::wstring name=std::wstring(stem)+L"\\"+stem+L".str";
            if(relative!=name) continue;
            const bool loc=relative==L"loc\\loc.str";
            const std::wstring mission(stem);
            rt.resourceAudit.mission(std::string(mission.begin(),mission.end()));
            rt.landOfChocolateStreamSeen.store(loc,std::memory_order_release);
            if(rt.autoDefeatLocEnemies)std::fprintf(stderr,"[AUTO DEFEAT LOC] map=%ls active=%u\n",stem,unsigned(loc));
            return;
        }
    } catch(const Status&) {
        // An optional level observation must not fail a successful asset open.
    }
}
struct Name { uint32_t root,attributes; std::string path; };
Name readName(uint8_t* base,uint32_t address) {
    auto* attrs=guest(base,address,12,false);
    Name result{be32(attrs),be32(attrs+8),{}};
    auto* ansi=guest(base,be32(attrs+4),8,false);
    uint16_t length=be16(ansi),maximum=be16(ansi+2);
    if(!length || length>maximum) fail(badName);
    if(length>4096) fail(nameTooLong);
    auto* bytes=guest(base,be32(ansi+4),length,false);
    result.path.assign(reinterpret_cast<const char*>(bytes),length);
    for(char& ch:result.path) {
        if(uint8_t(ch)<32 || uint8_t(ch)>126) fail(badName);
        if(ch=='/') ch='\\';
    }
    if(result.attributes&~0x40u) unsupported("object attributes",result.attributes);
    return result;
}
std::vector<std::wstring> components(Name& name) {
    std::string lower=name.path;
    for(char& c:lower) if(c>='A' && c<='Z') c=char(c+'a'-'A');
    size_t prefix=0;
    for(const char* alias:{"game:","d:","\\device\\harddisk0\\partition1"}) {
        std::string a=alias;
        if(lower.starts_with(a) && (lower.size()==a.size() || lower[a.size()]=='\\')) {prefix=a.size();break;}
    }
    if(prefix) {
        if(name.root!=0 && name.root!=0xfffffffdu) fail(badName);
        name.path.erase(0,prefix);
        if(!name.path.empty()) name.path.erase(0,1);
    } else if(name.path.find(':')!=std::string::npos || name.path.front()=='\\') {
        fprintf(stderr,"[FS] unsupported asset alias: %.240s\n",name.path.c_str()); fail(notSupported);
    }
    std::vector<std::wstring> result;
    size_t start=0;
    while(start<name.path.size()) {
        size_t end=name.path.find('\\',start);
        if(end==std::string::npos) end=name.path.size();
        auto part=name.path.substr(start,end-start);
        if(part.empty() || part=="." || part==".." || part.back()=='.' || part.back()==' ' ||
            part.find_first_of(":*?\"<>|")!=std::string::npos) fail(badName);
        result.emplace_back(part.begin(),part.end()); start=end+1;
    }
    return result;
}
Object openAsset(Simpsons::Runtime& rt,Name name,uint32_t access,uint32_t share,uint32_t options) {
    constexpr uint32_t allowedAccess=0xa01200a9u; // GENERIC_READ/EXECUTE, SYNCHRONIZE, READ_CONTROL, read/EA/execute/attributes.
    if(access&~allowedAccess) fail(accessDenied);
    if((share&~7u) || !(access&(SYNCHRONIZE|GENERIC_READ|GENERIC_EXECUTE))) fail(invalidParameter);
    if((options&0x1000u)) fail(accessDenied); // FILE_DELETE_ON_CLOSE
    if((options&0x30u)!=syncNonalert) unsupported("asynchronous/alertable open options",options);
    constexpr uint32_t supportedOptions=directoryOption|syncNonalert|nonDirectoryOption|4u|0x800u|freeSpaceQuery|noBuffering;
    if(options&~supportedOptions) unsupported("create options",options);
    if((options&(directoryOption|nonDirectoryOption))==(directoryOption|nonDirectoryOption)) fail(invalidParameter);
    bool trailingSeparator=!name.path.empty() && name.path.back()=='\\';
    auto parts=components(name);
    std::wstring rootPath;
    const bool videoFrontend=(name.root==0||name.root==0xfffffffdu)&&parts.size()==2&&!rt.nativeFrontendRoot.empty()&&
        ((equalPath(parts[0],L"frontend")&&equalPath(parts[1],L"frontend.str"))||
         (equalPath(parts[0],L"simpsons_chars")&&equalPath(parts[1],L"simpsons_chars_global.str")));
    if(videoFrontend)std::fprintf(stderr,"[NATIVE VIDEO ASSET] loaded native Options package for %s\n",name.path.c_str());
    auto root=rootObject(rt,rootPath,videoFrontend?rt.nativeFrontendRoot:std::filesystem::path{});
    std::vector<Object> pins{root};
    Object current=root;
    if(name.root!=0 && name.root!=0xfffffffdu) {
        current=fileObject(rt,name.root);
        if(!directory(current->native)) fail(notDirectory);
        if(!underRoot(rootPath,finalPath(current->native))) fail(accessDenied);
        pins.push_back(current);
    }
    static auto create=native<CreateFn>("NtCreateFile");
    // Empty name opens the verified root itself relative to its held handle.
    if(parts.empty()) parts.emplace_back();
    for(size_t i=0;i<parts.size();++i) {
        bool last=i+1==parts.size();
        UNICODE_STRING text{};
        if(parts[i].size()>32767) throw Simpsons::Failure("Native save path component too long");
        text.Buffer=parts[i].data(); text.Length=USHORT(parts[i].size()*sizeof(wchar_t)); text.MaximumLength=text.Length;
        OBJECT_ATTRIBUTES attrs{};
        attrs.Length=sizeof(attrs); attrs.RootDirectory=current->native; attrs.ObjectName=&text; attrs.Attributes=0x40;
        IO_STATUS_BLOCK ios{}; NativeHandle next;
        // READ_ATTRIBUTES is internal validation access, never write access.
        ULONG wanted=last?(access|FILE_READ_ATTRIBUTES):FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|SYNCHRONIZE;
        ULONG nativeOptions=0x200000u|syncNonalert; // OPEN_REPARSE_POINT: never follow the next component.
        // Preserve FREE_SPACE_QUERY on the actual returned object, including an
        // empty-name root open. NT captures the opening user's quota context.
        if(last) nativeOptions|=options&~(directoryOption|nonDirectoryOption);
        // NO_INTERMEDIATE_BUFFERING reaches NT unchanged. Native reads enforce
        // this volume/device's offset, length and buffer-alignment contract;
        // no cached reopen, hidden buffer or fabricated completion is involved.
        check(create(&next.value,wanted,&attrs,&ios,nullptr,0,
            last?(share&FILE_SHARE_READ):(FILE_SHARE_READ|FILE_SHARE_WRITE),1,nativeOptions,nullptr,0));
        bool isDir=directory(next.value); // Check reparse tag BEFORE using as a parent or returning it.
        if(!underRoot(rootPath,finalPath(next.value))) fail(accessDenied);
        if((!last || (options&directoryOption) || (last&&trailingSeparator)) && !isDir) fail(notDirectory);
        if(last && (options&nonDirectoryOption) && isDir) fail(isDirectory);
        if(last) {
            auto object=std::make_shared<PinnedFile>(next.value,std::move(pins));
            next.release(); return object;
        }
        current=std::make_shared<Simpsons::KernelHandle>(next.value,Simpsons::KernelHandle::Type::File);
        next.release(); pins.push_back(current);
    }
    fail(badName);
}
void open(PPCContext& ctx,uint8_t* base,bool createCall) {
    uint32_t output=ctx.r3.u32,access=ctx.r4.u32,attrs=ctx.r5.u32,iosb=ctx.r6.u32;
    dispatch(ctx,base,iosb,[&](IoResult& io) {
        if(!output || !attrs) fail(invalidParameter);
        auto* out=guest(base,output,4,true);
        Name name=readName(base,attrs);
        uint32_t share=createCall?ctx.r9.u32:ctx.r7.u32;
        uint32_t options=createCall?be32(guest(base,uint64_t(ctx.r1.u32)+0x54,4,false)):ctx.r8.u32;
        std::string lower=name.path;for(char& c:lower)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
        const bool savePath=lower.starts_with("rmcsave:\\");
        if(createCall) {
            if(ctx.r7.u32) guest(base,ctx.r7.u32,8,false); // AllocationSize is ignored only for FILE_OPEN.
            if(!savePath&&ctx.r10.u32!=1) fail(accessDenied); // Original assets remain read-only.
            // FileAttributes and AllocationSize do not modify an existing FILE_OPEN.
        }
        const std::string requested=name.path;
        Object object;
        uint32_t information=1;
        try {
            if(savePath){
                if(name.root&&name.root!=0xfffffffdu)fail(badName);
                if(createCall&&ctx.r7.u32&&be64(guest(base,ctx.r7.u32,8,false)))unsupported("save allocation-size hint",ctx.r7.u32);
                auto store=Simpsons::active->nativeSaveSource(false);auto session=store?store->find("rmcsave"):nullptr;
                if(!session)fail(0xc000003a);
                const auto relative=name.path.substr(9);
                if(relative.empty()&&createCall&&ctx.r10.u32!=1)fail(accessDenied);
                auto file=relative.empty()&&(options&directoryOption)?session->openDirectory(access,share,options):
                    session->openFile(relative,access,share,createCall?ctx.r10.u32:1,options);
                information=file->disposition();object=std::make_shared<Simpsons::KernelHandle>(file->handle(),Simpsons::KernelHandle::Type::File);object->saveFile=std::move(file);
            }else object=openAsset(*Simpsons::active,std::move(name),access,share,options);
        }
        catch(const Status& status) {
            fprintf(stderr,"[FS] open failed status=%08X access=%08X share=%X options=%08X caller=%08X path=%.240s\n",
                status.value,access,share,options,uint32_t(ctx.lr),requested.c_str());
            throw;
        }
        catch(const Simpsons::Platform::NativeSaveError& error) {
            fprintf(stderr,"[FS] save open failed error=%u access=%08X share=%X options=%08X caller=%08X path=%.240s: %s\n",
                error.code,access,share,options,uint32_t(ctx.lr),requested.c_str(),error.what());throw;
        }
        if(!savePath) observeLevelStream(*Simpsons::active,*object,requested);
        std::erase_if(owned,[](const auto& entry){return entry.second.expired();});
        std::erase_if(provenance,[](const auto& entry){return !owned.contains(entry.first);});
        owned[object.get()]=object;
        auto* const observedOpen=savePath?nullptr:object.get();
        uint32_t id=Simpsons::active->addHandle(std::move(object));
        put32(out,id); io.finish(success,information);
        if(observedOpen) recordOpen(*Simpsons::active,*observedOpen,requested,id,access,share,options,uint32_t(ctx.lr));
        // Temporary stream-trace: record handles for the square texture's
        // stream so its reads (offset/length) are logged in NtReadFile.
        {
            std::string probe=requested;for(char& c:probe)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
            if(probe.find("e172a05c.str")!=std::string::npos) {
                streamTraceHandles().insert(id);
                fprintf(stderr,"[NATIVE STREAM OPEN] handle=%08X path=%.160s\n",id,requested.c_str());
            }
        }
        if(savePath)std::fprintf(stderr,"[NATIVE SAVE] file open handle=%08X access=%08X options=%08X path=%s; actual native payload\n",id,access,options,requested.c_str());
    });
}
struct StandardInfo { LARGE_INTEGER allocation,eof; ULONG links; BOOLEAN pending,directory; USHORT reserved; };
struct NetworkInfo { LARGE_INTEGER times[4],allocation,eof; ULONG attributes,pad; };
struct alignas(8) VolumeSizeInfo { LARGE_INTEGER total,available; ULONG sectorsPerUnit,bytesPerSector; };
static_assert(sizeof(StandardInfo)==24 && sizeof(NetworkInfo)==56);
static_assert(sizeof(VolumeSizeInfo)==24 && offsetof(VolumeSizeInfo,available)==8 &&
    offsetof(VolumeSizeInfo,sectorsPerUnit)==16 && offsetof(VolumeSizeInfo,bytesPerSector)==20);
}

namespace Simpsons {
uint64_t fileReadSequence() {std::lock_guard lock(recentReadMutex);return recentReadCount;}
FileReadSource recentFileReadCovering(uint32_t address,uint32_t length,uint64_t maxSequence) {
    FileReadSource result;
    if(!length||uint64_t(address)+length>0x100000000ull) return result;
    const uint64_t begin=address,end=begin+length;
    struct Piece {uint64_t begin,end;const ReadRecord* read;};
    std::vector<Piece> pieces;
    std::vector<std::pair<uint64_t,uint64_t>> uncovered{{begin,end}};
    const ReadRecord* newest=nullptr;
    std::lock_guard lock(recentReadMutex);
    const uint64_t filled=std::min<uint64_t>(recentReadCount,recentReads.size());
    for(uint64_t back=0;back<filled&&!uncovered.empty();++back) {
        const auto& read=recentReads[(recentReadCount-1-back)%recentReads.size()];
        const uint64_t readBegin=read.destination,readEnd=readBegin+read.completed;
        if(read.sequence>maxSequence||!(readBegin<end&&begin<readEnd)) continue;
        if(!newest) newest=&read;
        std::vector<std::pair<uint64_t,uint64_t>> remaining;
        for(const auto& [from,to]:uncovered) {
            const uint64_t lo=std::max(from,readBegin),hi=std::min(to,readEnd);
            if(lo>=hi){remaining.emplace_back(from,to);continue;}
            pieces.push_back({lo,hi,&read});
            if(from<lo) remaining.emplace_back(from,lo);
            if(hi<to) remaining.emplace_back(hi,to);
        }
        uncovered=std::move(remaining);
    }
    if(!newest) return result;
    const auto fill=[&](const ReadRecord& read,uint64_t at) {
        result.openId=read.openId;result.readOrdinal=read.readOrdinal;result.sequence=read.sequence;
        result.fileOffset=read.fileOffset==UINT64_MAX?UINT64_MAX:read.fileOffset+(at>read.destination?at-read.destination:0);
        result.destination=read.destination;result.completed=read.completed;
        if(read.info){result.fileExtent=read.info->extent;result.relative=read.info->relative;result.declared=read.info->declared;result.root=read.info->root;}
    };
    std::sort(pieces.begin(),pieces.end(),[](const Piece& a,const Piece& b){return a.begin<b.begin;});
    for(size_t i=0;i<pieces.size()&&i<result.pieces.size();++i) {
        const auto& piece=pieces[i];
        result.pieces[i]={uint32_t(piece.begin),uint32_t(piece.end),piece.read->fileOffset==UINT64_MAX?UINT64_MAX:piece.read->fileOffset+(piece.begin-piece.read->destination),
                          piece.read->readOrdinal,piece.read->openId,piece.read->sequence};
    }
    if(!uncovered.empty()) {result.status=FileReadSource::Status::Ambiguous;result.readCount=uint32_t(pieces.size());fill(*newest,begin);return result;}
    result.readCount=uint32_t(pieces.size());
    if(pieces.size()==1) {result.status=FileReadSource::Status::Covered;fill(*pieces[0].read,begin);return result;}
    // Several last-writer reads: one linear file range only if every piece is the same open at the matching offset.
    const auto& first=*pieces[0].read;bool linear=first.fileOffset!=UINT64_MAX;
    const uint64_t base=linear?first.fileOffset+(begin>first.destination?begin-first.destination:0):0;
    for(const auto& piece:pieces) {
        if(!linear||piece.read->openId!=first.openId||piece.read->fileOffset==UINT64_MAX||
           piece.read->fileOffset+(piece.begin-piece.read->destination)!=base+(piece.begin-begin)) {linear=false;break;}
    }
    if(!linear) {result.status=FileReadSource::Status::Ambiguous;fill(*newest,begin);return result;}
    result.status=FileReadSource::Status::Spanning;fill(first,begin);return result;
}
}
PPC_FUNC(__imp__NtCreateFile) { open(ctx,base,true); }
PPC_FUNC(__imp__NtOpenFile) { open(ctx,base,false); }
PPC_FUNC(__imp__NtReadFile) {
    uint32_t handle=ctx.r3.u32,iosb=ctx.r7.u32;
    dispatch(ctx,base,iosb,[&](IoResult& io) {
        if(ctx.r4.u32 || ctx.r5.u32 || ctx.r6.u32) unsupported("event/APC read",ctx.r4.u32|ctx.r5.u32|ctx.r6.u32);
        auto object=fileObject(*Simpsons::active,handle);
        if(directory(object->native)) fail(isDirectory);
        uint8_t empty{};
        void* output=ctx.r9.u32?guest(base,ctx.r8.u32,ctx.r9.u32,true):&empty;
        LARGE_INTEGER offset{}; LARGE_INTEGER* offsetPtr=nullptr;
        if(ctx.r10.u32) {
            uint64_t value=be64(guest(base,ctx.r10.u32,8,false));
            if(value==UINT64_MAX || value==UINT64_MAX-1) value=UINT64_MAX-1;
            else if(value>INT64_MAX) fail(invalidParameter);
            offset.QuadPart=int64_t(value); offsetPtr=&offset;
        }
        static auto read=native<ReadFn>("NtReadFile");
        IO_STATUS_BLOCK result{};
        // Observer only: the real offset of this read (explicit, or the synchronous
        // file position before it) for the audit. Never consulted by the read itself.
        uint64_t observedOffset=UINT64_MAX;
        const bool observedExplicit=offsetPtr&&offset.QuadPart!=int64_t(UINT64_MAX-1);
        const bool observed=Simpsons::active->resourceAudit.active()&&provenance.contains(object.get());
        if(observed) {
            if(observedExplicit) observedOffset=uint64_t(offset.QuadPart);
            else {
                static auto position=native<QueryFn>("NtQueryInformationFile");
                LARGE_INTEGER current{};IO_STATUS_BLOCK probe{};
                if(position(object->native,&probe,&current,sizeof(current),FILE_INFORMATION_CLASS(14))>=0) observedOffset=uint64_t(current.QuadPart);
            }
        }
        NTSTATUS status=read(object->native,nullptr,nullptr,nullptr,&result,output,ctx.r9.u32,offsetPtr,nullptr);
        if(observed && status>=0 && result.Information)
            recordRead(*Simpsons::active,*object,handle,uint32_t(ctx.lr),observedExplicit,observedOffset,ctx.r9.u32,uint32_t(result.Information),uint32_t(status),ctx.r8.u32);
        if(streamTraceHandles().count(handle))
            fprintf(stderr,"[NATIVE STREAM READ] handle=%08X offset=%lld bytes=%u completed=%u status=%08X caller=%08X\n",
                handle,static_cast<long long>(offsetPtr?offset.QuadPart:-1),ctx.r9.u32,uint32_t(result.Information),
                uint32_t(status),uint32_t(ctx.lr));
        if(uint32_t(status)==0x103) throw Simpsons::Failure("Synchronous asset read unexpectedly returned pending");
        io.finish(uint32_t(status),uint32_t(result.Information));
        if(object->saveFile)std::fprintf(stderr,"[NATIVE SAVE] file read handle=%08X bytes=%u offset=%lld explicit=%u completed=%u status=%08X caller=%08X; actual original payload\n",handle,ctx.r9.u32,static_cast<long long>(offset.QuadPart),unsigned(offsetPtr!=nullptr),uint32_t(result.Information),uint32_t(status),uint32_t(ctx.lr));
    });
}
PPC_FUNC(__imp__NtQueryInformationFile) {
    uint32_t handle=ctx.r3.u32,iosb=ctx.r4.u32,output=ctx.r5.u32,length=ctx.r6.u32,info=ctx.r7.u32;
    dispatch(ctx,base,iosb,[&](IoResult& io) {
        uint32_t size=info==5?24:info==14?8:info==34?56:0;
        if(!size) {fprintf(stderr,"[FS] unsupported query class=%u\n",info);fail(invalidInfo);}
        if(length<size) fail(lengthMismatch);
        auto* out=guest(base,output,size,true);
        auto object=fileObject(*Simpsons::active,handle);
        static auto query=native<QueryFn>("NtQueryInformationFile");
        IO_STATUS_BLOCK result{};
        if(info==5) {
            StandardInfo data{}; check(query(object->native,&result,&data,sizeof(data),FILE_INFORMATION_CLASS(5)));
            put64(out,uint64_t(data.allocation.QuadPart));put64(out+8,uint64_t(data.eof.QuadPart));
            put32(out+16,data.links);out[20]=data.pending;out[21]=data.directory;out[22]=out[23]=0;
        } else if(info==14) {
            if(directory(object->native)) fail(isDirectory);
            LARGE_INTEGER position{}; check(query(object->native,&result,&position,8,FILE_INFORMATION_CLASS(14)));
            put64(out,uint64_t(position.QuadPart));
        } else {
            NetworkInfo data{}; check(query(object->native,&result,&data,sizeof(data),FILE_INFORMATION_CLASS(34)));
            for(unsigned i=0;i<4;++i) put64(out+i*8,uint64_t(data.times[i].QuadPart));
            put64(out+32,uint64_t(data.allocation.QuadPart));put64(out+40,uint64_t(data.eof.QuadPart));
            put32(out+48,data.attributes);put32(out+52,0);
        }
        io.finish(success,size);
    });
}
PPC_FUNC(__imp__NtQueryDirectoryFile) {
    // Xbox has no information-class or single-entry arguments: its original
    // FindFirst/FindNext consumer uses one ANSI FILE_DIRECTORY_INFORMATION.
    // Argument nine (RestartScan) is on the original stack at +0x54.
    dispatch(ctx,base,ctx.r7.u32,[&](IoResult& io) {
        if(ctx.r4.u32||ctx.r5.u32||ctx.r6.u32)unsupported("event/APC directory query",ctx.r4.u32|ctx.r5.u32|ctx.r6.u32);
        const uint32_t length=ctx.r9.u32;
        if(length<72)fail(lengthMismatch);
        auto* out=guest(base,ctx.r8.u32,length,true);
        auto object=fileObject(*Simpsons::active,ctx.r3.u32);
        if(!object->saveFile)fail(accessDenied); // Only mounted native save generations are qualified.
        if(!directory(object->native))fail(notDirectory);
        const auto restart=be32(guest(base,uint64_t(ctx.r1.u32)+0x54,4,false));
        if(restart>1)fail(invalidParameter);
        std::wstring pattern;UNICODE_STRING name{};UNICODE_STRING* filter=nullptr;
        if(ctx.r10.u32){
            const auto* ansi=guest(base,ctx.r10.u32,8,false);const uint16_t count=be16(ansi);
            if(count>be16(ansi+2)||count>42)fail(badName);
            if(count){const auto* bytes=guest(base,be32(ansi+4),count,false);
                for(uint32_t i=0;i<count;++i){const auto ch=bytes[i];if(ch<32||ch>126||std::string_view("\\/:\"<>|").find(char(ch))!=std::string_view::npos)fail(badName);pattern+=wchar_t(ch);}
                if(pattern==L"."||pattern==L"..")fail(badName);
                name.Buffer=pattern.data();name.Length=USHORT(pattern.size()*2);name.MaximumLength=name.Length;filter=&name;
            }
        }
        using DirectoryFn=NTSTATUS(NTAPI*)(HANDLE,HANDLE,PVOID,PVOID,PIO_STATUS_BLOCK,PVOID,ULONG,FILE_INFORMATION_CLASS,BOOLEAN,PUNICODE_STRING,BOOLEAN);
        static const auto query=native<DirectoryFn>("NtQueryDirectoryFile");
        struct alignas(8) Entry {ULONG next,index;LARGE_INTEGER times[4],eof,allocation;ULONG attributes,nameLength;wchar_t name[256];};
        static_assert(offsetof(Entry,name)==64);
        // Matching buffer capacity preserves NT's real overflow and cursor
        // behavior while allowing its UTF-16 names to become guest ASCII.
        const ULONG capacity=64+2*std::min(length-64,255u);
        Entry entry{};IO_STATUS_BLOCK result{};NTSTATUS status{};bool first=true;
        do{
            entry={};result={};status=query(object->native,nullptr,nullptr,nullptr,&result,&entry,capacity,FILE_INFORMATION_CLASS(1),TRUE,first?filter:nullptr,BOOLEAN(first&&restart));first=false;
            if(uint32_t(status)==0x103)throw Simpsons::Failure("Synchronous save directory query unexpectedly returned pending");
            if(status<0&&uint32_t(status)!=0x80000005){io.finish(uint32_t(status));return;}
            if(result.Information<64||result.Information>capacity||(entry.nameLength&1)||entry.next)throw Simpsons::Failure("Incomplete native directory result");
        }while((entry.nameLength==2&&entry.name[0]==L'.')||(entry.nameLength==4&&entry.name[0]==L'.'&&entry.name[1]==L'.'));
        if(entry.attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))fail(accessDenied);
        const uint32_t actual=entry.nameLength/2,copied=std::min(actual,uint32_t((result.Information-64)/2));
        if(actual>42||copied>length-64)fail(badName);
        for(uint32_t i=0;i<copied;++i)if(entry.name[i]<32||entry.name[i]>126)fail(badName);
        put32(out,0);put32(out+4,entry.index);
        for(unsigned i=0;i<4;++i)put64(out+8+i*8,uint64_t(entry.times[i].QuadPart));
        put64(out+40,uint64_t(entry.eof.QuadPart));put64(out+48,uint64_t(entry.allocation.QuadPart));
        put32(out+56,entry.attributes);put32(out+60,actual);
        for(uint32_t i=0;i<copied;++i)out[64+i]=uint8_t(entry.name[i]);
        io.finish(uint32_t(status),64+copied);
        std::fprintf(stderr,"[NATIVE SAVE] directory entry bytes=%llu name=%.*ls status=%08X; actual native file metadata\n",static_cast<unsigned long long>(entry.eof.QuadPart),int(copied),entry.name,uint32_t(status));
    });
}
PPC_FUNC(__imp__NtQueryVolumeInformationFile) {
    uint32_t handle=ctx.r3.u32,iosb=ctx.r4.u32,output=ctx.r5.u32,length=ctx.r6.u32,info=ctx.r7.u32;
    dispatch(ctx,base,iosb,[&](IoResult& io) {
        if(info!=3) {fprintf(stderr,"[FS] unsupported volume query class=%u\n",info);fail(invalidInfo);}
        if(length<24) fail(lengthMismatch);
        auto* out=guest(base,output,24,true);
        auto object=fileObject(*Simpsons::active,handle);
        static auto query=native<QueryVolumeFn>("NtQueryVolumeInformationFile");
        IO_STATUS_BLOCK result{}; VolumeSizeInfo data{};
        NTSTATUS status=query(object->native,&result,&data,sizeof(data),3); // FileFsSizeInformation
        if(uint32_t(status)==0x103) throw Simpsons::Failure("Synchronous volume query unexpectedly returned pending");
        check(status);
        if(status!=0 || result.Status!=0 || result.Information!=sizeof(data))
            throw Simpsons::Failure("Native volume query did not return a complete FileFsSizeInformation result");
        // Counts are allocation units on this handle's actual host volume, with
        // NT's caller/quota semantics. Do not synthesize console disk geometry.
        put64(out,uint64_t(data.total.QuadPart));put64(out+8,uint64_t(data.available.QuadPart));
        put32(out+16,data.sectorsPerUnit);put32(out+20,data.bytesPerSector);
        io.finish(success,24);
    });
}
PPC_FUNC(__imp__NtSetInformationFile) {
    uint32_t handle=ctx.r3.u32,iosb=ctx.r4.u32,input=ctx.r5.u32,length=ctx.r6.u32,info=ctx.r7.u32;
    dispatch(ctx,base,iosb,[&](IoResult& io) {
        if(info!=14&&info!=19&&info!=20) {fprintf(stderr,"[FS] denied mutating/unsupported set class=%u\n",info);fail(accessDenied);}
        if(info!=14){auto candidate=Simpsons::active->getHandle(handle);if(!candidate||!candidate->saveFile||!candidate->saveFile->writable())fail(accessDenied);}
        if(length<8) fail(lengthMismatch);
        uint64_t position=be64(guest(base,input,8,false));
        if(position>INT64_MAX) fail(invalidParameter);
        auto object=fileObject(*Simpsons::active,handle);
        if(info!=14&&(!object->saveFile||!object->saveFile->writable()))fail(accessDenied);
        if(directory(object->native)) fail(isDirectory);
        LARGE_INTEGER value{};value.QuadPart=int64_t(position);
        static auto set=native<SetFn>("NtSetInformationFile");
        IO_STATUS_BLOCK result{};check(set(object->native,&result,&value,8,FILE_INFORMATION_CLASS(info)));
        io.finish(success,8);
    });
}
PPC_FUNC(__imp__NtWriteFile) {
    dispatch(ctx,base,ctx.r7.u32,[&](IoResult& io) {
        const uint32_t handle=ctx.r3.u32;auto candidate=Simpsons::active->getHandle(handle);
        if(!candidate||!candidate->saveFile||!candidate->saveFile->writable())fail(accessDenied);
        if(ctx.r4.u32||ctx.r5.u32||ctx.r6.u32)unsupported("event/APC save write",ctx.r4.u32|ctx.r5.u32|ctx.r6.u32);
        auto object=fileObject(*Simpsons::active,handle);
        if(!object->saveFile||!object->saveFile->writable()){fprintf(stderr,"[FS] denied asset write\n");fail(accessDenied);}
        uint8_t empty{};void* bytes=ctx.r9.u32?guest(base,ctx.r8.u32,ctx.r9.u32,false):&empty;
        LARGE_INTEGER offset{};LARGE_INTEGER* offsetPtr=nullptr;
        if(ctx.r10.u32){const auto value=be64(guest(base,ctx.r10.u32,8,false));if(value>INT64_MAX&&value<UINT64_MAX-1)fail(invalidParameter);offset.QuadPart=int64_t(value);offsetPtr=&offset;}
        static const auto write=native<ReadFn>("NtWriteFile");IO_STATUS_BLOCK result{};
        const auto status=write(object->native,nullptr,nullptr,nullptr,&result,bytes,ctx.r9.u32,offsetPtr,nullptr);
        if(uint32_t(status)==0x103)throw Simpsons::Failure("Synchronous native save write unexpectedly returned pending");
        io.finish(uint32_t(status),uint32_t(result.Information));
        std::fprintf(stderr,"[NATIVE SAVE] file write handle=%08X bytes=%u completed=%u status=%08X; actual original payload\n",handle,ctx.r9.u32,uint32_t(result.Information),uint32_t(status));
    });
}
PPC_FUNC(__imp__NtFlushBuffersFile){dispatch(ctx,base,ctx.r4.u32,[&](IoResult& io){auto object=fileObject(*Simpsons::active,ctx.r3.u32);if(!object->saveFile)fail(accessDenied);object->saveFile->flush();io.finish(success,0);});}
PPC_FUNC(__imp__NtDeleteFile) {fprintf(stderr,"[FS] denied asset deletion\n");ctx.r3.u64=accessDenied;}
