// Standalone ABI/host-filesystem contract test: link filesystem.cpp, not the
// parent runtime/game library. See docs/filesystem.md for target/compile commands.
#include "runtime/filesystem.h"
#include "runtime/native_window.h"
#include "runtime/threads.h"
#include <winioctl.h>
#include <winternl.h>
#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>

#ifndef SIMPSONS_FILESYSTEM_STANDALONE
#error "Build this test with SIMPSONS_FILESYSTEM_STANDALONE; do not link SimpsonsRuntime"
#endif

namespace {
std::vector<uint8_t> ram(0x10000);
bool protectBuffer=false;
uint32_t stopOnGuestAddress=0; // Deterministic stop during a later pointer check.
void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
constexpr uint32_t output=0x1200,ios=0x1210,attrs=0x1000,ansi=0x1020,pathBytes=0x1100;
constexpr uint32_t buffer=0x2000,infoBuffer=0x1500,offsetPtr=0x1400,stack=0x1800;
constexpr uint32_t readAccess=0x100001,syncFile=0x60;
constexpr uint32_t freeSpaceDirectory=0x00800021;
constexpr uint32_t denied=0xc0000022,invalid=0xc000000d,accessFault=0xc0000005,unsupported=0xc00000bb;
}

// Small checked-memory/handle-table harness using the ACTUAL Runtime/PPCContext
// declarations. All path, native handle and I/O implementation is production code.
namespace Simpsons {
Runtime* active{};
thread_local PPCContext* currentContext{};
NativeWindow::~NativeWindow()=default;
GuestThread::~GuestThread()=default;
Runtime::Runtime() {base=ram.data();active=this;}
Runtime::~Runtime() {handles.clear();active=nullptr;}
std::shared_ptr<Platform::NativeSaveStore> Runtime::nativeSaveSource(bool) {return {};}
void Runtime::checkRunning() {
    if(stopping.load(std::memory_order_acquire)) {
        std::lock_guard lock(stopMutex);
        throw Failure(stopReason);
    }
}
uint8_t* Runtime::pointer(uint32_t address,unsigned width,bool write) {
    if(address<0x1000 || uint64_t(address)+width>ram.size() ||
        (write && protectBuffer && uint64_t(address)+width>buffer && address<buffer+0x1000))
        throw Failure("test guest memory access denied");
    return base+address;
}
uint32_t Runtime::addHandle(std::shared_ptr<KernelHandle> handle) {
    std::lock_guard lock(handleMutex);uint32_t id=nextHandle;nextHandle+=4;handles.emplace(id,std::move(handle));return id;
}
std::shared_ptr<KernelHandle> Runtime::getHandle(uint32_t id) {
    std::lock_guard lock(handleMutex);auto found=handles.find(id);return found==handles.end()?nullptr:found->second;
}
uint32_t Runtime::closeHandle(uint32_t id) {
    std::lock_guard lock(handleMutex);return handles.erase(id)?0:0xc0000008;
}
}
uint8_t* PPCGuestPointer(uint8_t* base,uint32_t address,unsigned width,bool write) {
    require(base==Simpsons::active->base,"wrong guest base");
    if(stopOnGuestAddress && address==stopOnGuestAddress) {
        std::lock_guard lock(Simpsons::active->stopMutex);
        Simpsons::active->stopReason="test pointer-check shutdown";
        Simpsons::active->stopping.store(true,std::memory_order_release);
    }
    Simpsons::active->checkRunning();
    return Simpsons::active->pointer(address,width,write);
}
[[noreturn]] void PPCRecompFailure(const PPCContext&,uint32_t,const char* reason) {throw Simpsons::Failure(reason);}

namespace {
void store32(uint32_t address,uint32_t value) {uint8_t* base=ram.data();PPC_STORE_U32(address,value);}
void store64(uint32_t address,uint64_t value) {uint8_t* base=ram.data();PPC_STORE_U64(address,value);}
uint32_t load32(uint32_t address) {uint8_t* base=ram.data();return PPC_LOAD_U32(address);}
uint64_t load64(uint32_t address) {uint8_t* base=ram.data();return PPC_LOAD_U64(address);}
uint32_t callerLr=0x82345678; // The guest import caller recorded by provenance receipts.
PPCContext context() {PPCContext ctx{};ctx.r1.u32=stack;ctx.lr=callerLr;return ctx;}
// Minimal readers for the compact JSON receipt rows written by ResourceAudit.
std::string jsonString(const std::string& row,const char* key) {
    const std::string marker=std::string("\"")+key+"\":\"";const auto at=row.find(marker);require(at!=std::string::npos,"receipt field absent");
    std::string result;
    for(size_t i=at+marker.size();i<row.size();++i) {
        if(row[i]=='"') return result;
        if(row[i]=='\\') {require(++i<row.size(),"receipt escape truncated");result+=row[i];}
        else result+=row[i];
    }
    throw std::runtime_error("receipt string unterminated");
}
std::map<std::string,std::string> wordsOf(const std::string& text) {
    std::map<std::string,std::string> result;size_t start=0;
    while(start<text.size()) {
        auto end=text.find(' ',start);if(end==std::string::npos) end=text.size();
        const auto word=text.substr(start,end-start);const auto eq=word.find('=');
        if(eq!=std::string::npos) result[word.substr(0,eq)]=word.substr(eq+1);
        start=end+1;
    }
    return result;
}
void name(const std::string& path,uint32_t root=0xfffffffdu) {
    require(path.size()<240,"test path too long");
    store32(attrs,root);store32(attrs+4,ansi);store32(attrs+8,0x40);
    uint8_t* base=ram.data();PPC_STORE_U16(ansi,uint16_t(path.size()));PPC_STORE_U16(ansi+2,uint16_t(path.size()+1));
    store32(ansi+4,pathBytes);memcpy(ram.data()+pathBytes,path.data(),path.size());
    ram[pathBytes+path.size()]=0xCC; // Counted string, deliberately NOT NUL terminated.
}
uint32_t open(const std::string& path,uint32_t options=syncFile,uint32_t access=readAccess,
              uint32_t root=0xfffffffdu,bool create=false,uint32_t disposition=1,uint32_t share=3) {
    name(path,root);store32(output,0xdeadbeef);store32(ios,0xdeadbeef);store32(ios+4,0xdeadbeef);
    auto ctx=context();ctx.r3.u32=output;ctx.r4.u32=access;ctx.r5.u32=attrs;ctx.r6.u32=ios;
    if(create) {
        ctx.r7.u32=0;ctx.r8.u32=0x80;ctx.r9.u32=share;ctx.r10.u32=disposition;
        store32(stack+0x54,options);ctx.r11.u32=0;__imp__NtCreateFile(ctx,ram.data());
    } else {ctx.r7.u32=share;ctx.r8.u32=options;__imp__NtOpenFile(ctx,ram.data());}
    require(load32(ios)==ctx.r3.u32,"open IOSB status differs from return");
    if(ctx.r3.u32) {
        require(load32(output)==0xdeadbeef && load32(ios+4)==0,"failed open modified handle/reported bytes");
        return ctx.r3.u32;
    }
    require(load32(output)!=0xdeadbeef && load32(ios+4)==1,"open did not report FILE_OPENED");
    return load32(output);
}
uint32_t query(uint32_t handle,uint32_t type,uint32_t length) {
    memset(ram.data()+infoBuffer,0xA5,64);
    auto ctx=context();ctx.r3.u32=handle;ctx.r4.u32=ios;ctx.r5.u32=infoBuffer;ctx.r6.u32=length;ctx.r7.u32=type;
    __imp__NtQueryInformationFile(ctx,ram.data());require(load32(ios)==ctx.r3.u32,"query IOSB mismatch");return ctx.r3.u32;
}
uint32_t queryVolume(uint32_t handle,uint32_t type=3,uint32_t length=24,uint32_t destination=infoBuffer) {
    memset(ram.data()+infoBuffer,0xA5,64);
    auto ctx=context();ctx.r3.u32=handle;ctx.r4.u32=ios;ctx.r5.u32=destination;ctx.r6.u32=length;ctx.r7.u32=type;
    __imp__NtQueryVolumeInformationFile(ctx,ram.data());
    require(load32(ios)==ctx.r3.u32,"volume query IOSB mismatch");return ctx.r3.u32;
}
struct alignas(8) NativeVolumeSize {
    LARGE_INTEGER total,available;
    ULONG sectorsPerUnit,bytesPerSector;
};
static_assert(sizeof(NativeVolumeSize)==24);
NativeVolumeSize nativeVolume(HANDLE file) {
    using Query=NTSTATUS(NTAPI*)(HANDLE,PIO_STATUS_BLOCK,PVOID,ULONG,ULONG);
    auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryVolumeInformationFile"));
    require(query!=nullptr,"native volume query unavailable");
    NativeVolumeSize result{};IO_STATUS_BLOCK io{};
    require(query(file,&io,&result,sizeof(result),3)==0 && io.Status==0 && io.Information==24,
            "independent native class-3 query failed");
    return result;
}
ULONG nativeWordInformation(HANDLE file,ULONG kind) {
    using Query=NTSTATUS(NTAPI*)(HANDLE,PIO_STATUS_BLOCK,PVOID,ULONG,ULONG);
    auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationFile"));
    require(query!=nullptr,"native file query unavailable");
    ULONG value=0;IO_STATUS_BLOCK io{};
    require(query(file,&io,&value,sizeof(value),kind)==0 && io.Status==0 && io.Information==sizeof(value),
        "independent native mode/alignment query failed");
    return value;
}
void verifyVolume(Simpsons::Runtime& rt,uint32_t handle) {
    // Free space may change independently of this process. Compare all four
    // fields only when the immediate surrounding native samples agree.
    for(unsigned attempt=0;attempt<32;++attempt) {
        auto before=nativeVolume(rt.getHandle(handle)->native);
        require(queryVolume(handle,3,64)==0 && load32(ios+4)==24,"volume query completion mismatch");
        auto after=nativeVolume(rt.getHandle(handle)->native);
        require(ram[infoBuffer+24]==0xA5 && ram[infoBuffer+63]==0xA5,"volume result exceeded 24 bytes");
        if(before.total.QuadPart!=after.total.QuadPart || before.available.QuadPart!=after.available.QuadPart ||
           before.sectorsPerUnit!=after.sectorsPerUnit || before.bytesPerSector!=after.bytesPerSector) continue;
        require(load64(infoBuffer)==uint64_t(after.total.QuadPart) &&
                load64(infoBuffer+8)==uint64_t(after.available.QuadPart) &&
                load32(infoBuffer+16)==after.sectorsPerUnit && load32(infoBuffer+20)==after.bytesPerSector,
                "guest allocation units/geometry differ from actual native volume");
        require(after.total.QuadPart>0 && after.available.QuadPart>=0 && after.sectorsPerUnit && after.bytesPerSector,
                "invalid native volume geometry");
        // Mirror the original wrapper's mullw + mulld conversion; no fixed
        // 512-byte sector or fabricated console allocation unit is involved.
        uint32_t unitBytes=load32(infoBuffer+16)*load32(infoBuffer+20);
        require(uint64_t(unitBytes)==uint64_t(after.sectorsPerUnit)*after.bytesPerSector,
                "native allocation unit too large for original wrapper");
        require(load64(infoBuffer)*unitBytes==uint64_t(after.total.QuadPart)*unitBytes &&
                load64(infoBuffer+8)*unitBytes==uint64_t(after.available.QuadPart)*unitBytes,
                "original wrapper byte conversion mismatch");
        return;
    }
    throw std::runtime_error("native free space never stabilized across volume query samples");
}
uint64_t position(uint32_t handle) {require(query(handle,14,8)==0,"position query failed");return load64(infoBuffer);}
uint32_t seek(uint32_t handle,uint64_t offset) {
    store64(infoBuffer,offset);auto ctx=context();ctx.r3.u32=handle;ctx.r4.u32=ios;
    ctx.r5.u32=infoBuffer;ctx.r6.u32=8;ctx.r7.u32=14;__imp__NtSetInformationFile(ctx,ram.data());
    require(load32(ios)==ctx.r3.u32,"seek IOSB mismatch");return ctx.r3.u32;
}
uint32_t read(uint32_t handle,uint32_t count,bool explicitOffset=false,uint64_t offset=0,
              uint32_t destination=buffer,uint32_t event=0,uint32_t apc=0,uint32_t apcContext=0) {
    auto ctx=context();ctx.r3.u32=handle;ctx.r4.u32=event;ctx.r5.u32=apc;ctx.r6.u32=apcContext;
    ctx.r7.u32=ios;ctx.r8.u32=destination;ctx.r9.u32=count;ctx.r10.u32=explicitOffset?offsetPtr:0;
    store64(offsetPtr,offset);__imp__NtReadFile(ctx,ram.data());
    require(load32(ios)==ctx.r3.u32,"read IOSB mismatch");return ctx.r3.u32;
}
void writeHost(const std::filesystem::path& path,const std::string& contents) {
    std::ofstream file(path,std::ios::binary);file.write(contents.data(),std::streamsize(contents.size()));
    require(bool(file),"unable to create temporary fixture");
}
std::string readHost(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void junction(const std::filesystem::path& link,const std::filesystem::path& target) {
    // Real NTFS directory reparse point; no symlink/developer-mode privilege needed.
    std::filesystem::create_directory(link);
    std::wstring substitute=L"\\??\\"+target.native(),print=target.native();
    const size_t subBytes=substitute.size()*2,printBytes=print.size()*2;
    std::vector<uint8_t> data(16+subBytes+2+printBytes+2);
    auto put16=[&](size_t p,uint16_t v){memcpy(data.data()+p,&v,2);};
    DWORD tag=IO_REPARSE_TAG_MOUNT_POINT;memcpy(data.data(),&tag,4);
    put16(4,uint16_t(data.size()-8));put16(8,0);put16(10,uint16_t(subBytes));
    put16(12,uint16_t(subBytes+2));put16(14,uint16_t(printBytes));
    memcpy(data.data()+16,substitute.data(),subBytes);memcpy(data.data()+18+subBytes,print.data(),printBytes);
    HANDLE file=CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    require(file!=INVALID_HANDLE_VALUE,"cannot open fixture junction");
    DWORD bytes;BOOL ok=DeviceIoControl(file,FSCTL_SET_REPARSE_POINT,data.data(),DWORD(data.size()),nullptr,0,&bytes,nullptr);
    CloseHandle(file);require(ok,"cannot create fixture junction");
}
struct TemporaryTree {
    std::filesystem::path temp,root;
    std::vector<std::filesystem::path> junctions;
    TemporaryTree() {
        wchar_t path[MAX_PATH+1];require(GetTempPathW(MAX_PATH,path)!=0,"no temporary directory");
        temp=std::filesystem::canonical(path);
        root=temp/(L"simpsons-filesystem-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        require(std::filesystem::create_directory(root),"temporary fixture already exists");
    }
    ~TemporaryTree() {
        // Delete only our freshly created absolute temp child. Unlink junctions
        // explicitly first; never recurse through a reparse point.
        try {
            require(root.is_absolute() && root.parent_path()==temp &&
                root.filename().wstring().starts_with(L"simpsons-filesystem-"),"unsafe cleanup target");
            for(const auto& path:junctions) {
                require(path.parent_path().parent_path()==root,"unsafe junction cleanup target");
                if(GetFileAttributesW(path.c_str())&FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(path.c_str());
                else DeleteFileW(path.c_str());
            }
            for(const auto& entry:std::filesystem::recursive_directory_iterator(root))
                require(!(GetFileAttributesW(entry.path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT),"reparse point remains before cleanup");
            std::filesystem::remove_all(root);
        } catch(...) {fprintf(stderr,"Temporary filesystem fixtures retained for inspection\n");}
    }
};
}

int main() {
    try {
        TemporaryTree tree;
        std::filesystem::create_directories(tree.root/"game"/"Data");
        std::filesystem::create_directories(tree.root/"outside");
        const std::string contents("A\0BCdefgh",9);
        const auto asset=tree.root/"game"/"Data"/"MiXeD.bin";
        writeHost(asset,contents);writeHost(tree.root/"outside"/"secret.bin","DO NOT READ");
        const auto lastWrite=std::filesystem::last_write_time(asset);
        Simpsons::Runtime rt;rt.gameRoot=std::filesystem::canonical(tree.root/"game");
        // Observer-only provenance of what the guest actually opened and read. Placed first so the
        // fixture's open_id is 1; everything later in this test also runs with the audit active.
        const auto auditPath=tree.root/"filesystem-audit.jsonl";
        rt.resourceAudit.configure(auditPath);rt.resourceAudit.mission("fs-provenance");rt.resourceAudit.action("snu-read");
        writeHost(tree.root/"game"/"Data"/"Audio.SNU","ABCDEFGHIJKLMNOP");
        {
            // Declared alias (drive prefix, forward slashes, different case) differs from the opened object's path.
            const auto audio=open("d:/Data/AUDIO.snu");require(audio<0x80000000,"provenance fixture open failed");
            BY_HANDLE_FILE_INFORMATION identity{};
            require(GetFileInformationByHandle(rt.getHandle(audio)->native,&identity),"independent native identity query failed");
            require(read(audio,4)==0 && load32(ios+4)==4,"provenance read 1 failed");
            require(seek(audio,10)==0 && read(audio,100)==0 && load32(ios+4)==6,"provenance partial read failed");
            rt.resourceAudit.failure("probe-position");
            require(read(audio,1)==0xc0000011,"provenance EOF read failed");
            require(read(audio,3,true,5)==0 && load32(ios+4)==3,"provenance explicit read failed");
            rt.resourceAudit.failure("probe-explicit");
            require(read(audio,1,true,UINT64_MAX-2)==invalid,"negative explicit offset accepted");
            require(read(audio,0,false,0,0)==0 && load32(ios+4)==0,"zero-length read failed");
            rt.resourceAudit.failure("probe-final");
            require(rt.closeHandle(audio)==0 && read(audio,1)==0xc0000008,"closed provenance handle accepted");
            rt.resourceAudit.failure("probe-after-close");
            std::vector<std::string> rows;{std::ifstream input(auditPath);std::string line;while(std::getline(input,line)) rows.push_back(line);}
            // 0 open | 1 first position-mode read | 2 probe(position) | 3 explicit read | 4,5,6 probes. EOF, rejected,
            // zero-length and post-close reads emit nothing; the repeated position-mode read is deduplicated.
            require(rows.size()==7,"provenance receipts missing, repeated or unexpected");
            const auto open0=rows[0];char hex[32];
            require(jsonString(open0,"event")=="lifecycle" && jsonString(open0,"kind")=="file_open" && jsonString(open0,"asset")=="file:data/audio.snu" &&
                    jsonString(open0,"mission")=="fs-provenance" && jsonString(open0,"last_action")=="snu-read" &&
                    open0.find("\"caller\":"+std::to_string(callerLr)+",")!=std::string::npos,"open receipt lost its asset/caller/mission/action");
            auto openParameters=wordsOf(jsonString(open0,"parameters")),openInstance=wordsOf(jsonString(open0,"instance")),openOwnership=wordsOf(jsonString(open0,"ownership"));
            std::snprintf(hex,sizeof(hex),"%08X",audio);const std::string guestHandle=hex;
            require(openParameters["root"]=="game" && openParameters["declared"]=="d:\\Data\\AUDIO.snu" && openParameters["access"]=="00100001" &&
                    openParameters["share"]=="3" && openParameters["options"]=="00000060","open receipt lost the declared alias or open arguments");
            require(openOwnership["admission"]=="native-open" && openOwnership["reparse"]=="none","open receipt claimed no verified native open");
            std::snprintf(hex,sizeof(hex),"%08lX",identity.dwVolumeSerialNumber);const std::string volume=hex;
            char index[32];std::snprintf(index,sizeof(index),"%016llX",static_cast<unsigned long long>((uint64_t(identity.nFileIndexHigh)<<32)|identity.nFileIndexLow));
            require(openInstance["handle"]==guestHandle && openInstance["open_id"]=="1" && openInstance["extent"]=="16" &&
                    openInstance["volume"]==volume && openInstance["file_index"]==index && identity.nFileSizeLow==16,
                    "open receipt differs from the independent native object identity");
            const auto group=jsonString(open0,"group");
            require(group.find("open_id")==std::string::npos && group.find(guestHandle)==std::string::npos && group.find("file_index")==std::string::npos,
                    "open instance identity leaked into the stable group");
            const auto expectRead=[&](const std::string& row,const char* event,const char* mode,uint32_t ordinal,uint64_t offset,uint32_t requested,uint32_t completed) {
                require(jsonString(row,"event")==event && jsonString(row,"asset")=="file:data/audio.snu" && jsonString(row,"mission")=="fs-provenance" &&
                        row.find("\"caller\":"+std::to_string(callerLr)+",")!=std::string::npos,"read receipt lost its asset/caller/mission");
                auto p=wordsOf(jsonString(row,"parameters")),i=wordsOf(jsonString(row,"instance")),o=wordsOf(jsonString(row,"ownership"));
                require(p["root"]=="game" && p["status"]=="00000000" && p["offset_mode"]==mode && o["admission"]=="native-read","read receipt lost its stable fields");
                require(i["handle"]==guestHandle && i["open_id"]=="1" && i["read_ordinal"]==std::to_string(ordinal) && i["offset"]==std::to_string(offset) &&
                        i["requested"]==std::to_string(requested) && i["completed"]==std::to_string(completed) && i["extent"]=="16" &&
                        i["declared"]=="d:\\Data\\AUDIO.snu","read receipt differs from the actual offset/length/ordinal");
                const auto g=jsonString(row,"group");
                for(const char* leaked:{"read_ordinal","offset=","requested","completed",guestHandle.c_str(),"open_id"})
                    require(g.find(leaked)==std::string::npos,"read instance data leaked into the stable group");
            };
            expectRead(rows[1],"encounter","position",1,0,4,4);
            // A deduplicated read still refreshes the latest snapshot that a later failure reports.
            expectRead(rows[2],"failure","position",2,10,100,6);
            require(jsonString(rows[2],"reason")=="probe-position" && jsonString(rows[2],"kind")=="file_read","failure did not bind to the latest read receipt");
            expectRead(rows[3],"encounter","explicit",3,5,3,3);
            for(unsigned index:{4u,5u,6u}) {
                expectRead(rows[index],"failure","explicit",3,5,3,3);
                require(jsonString(rows[index],"instance")==jsonString(rows[3],"instance"),"a failed, zero-length or post-close read replaced the latest receipt");
            }
            require(jsonString(rows[4],"reason")=="probe-explicit" && jsonString(rows[5],"reason")=="probe-final" && jsonString(rows[6],"reason")=="probe-after-close",
                    "probe failure reasons differ");
            // Which read deposited bytes at a guest range? Newest overlapping read decides: Covered when it covers the
            // whole range, Ambiguous when it only overlaps it (ring reuse), None otherwise. Failed reads are never recorded.
            {
                using Simpsons::FileReadSource;using Relation=FileReadSource::Status;
                const auto second=open("Data/Audio.SNU");require(second<0x80000000,"ring fixture open failed");
                const auto covering=[&](uint32_t address,uint32_t length){return Simpsons::recentFileReadCovering(address,length);};
                require(read(second,8,true,0,0x3000)==0 && load32(ios+4)==8,"ring read 1 failed");
                auto hit=covering(0x3000,8);
                require(hit.status==Relation::Covered&&hit.openId==2&&hit.readOrdinal==1&&hit.fileOffset==0&&hit.fileExtent==16&&
                        hit.destination==0x3000&&hit.completed==8&&hit.relative=="data/audio.snu"&&hit.declared=="Data\\Audio.SNU"&&hit.root=="game",
                        "covering read lost its file, offset or destination");
                hit=covering(0x3004,2);require(hit.status==Relation::Covered&&hit.fileOffset==4,"sub-range file offset is not read offset plus delta");
                require(covering(0x3000,9).status==Relation::Ambiguous,"a range extending past the read was reported covered");
                require(covering(0x3008,4).status==Relation::None&&covering(0x2FFF,1).status==Relation::None&&covering(0x2FFF,2).status==Relation::Ambiguous,
                        "adjacent or preceding ranges overlapped the read");
                require(covering(0x3000,0).status==Relation::None&&covering(0xFFFFFFFFu,2).status==Relation::None,"degenerate or wrapping ranges matched");
                require(read(second,8,true,8,0x3000)==0,"ring read 2 failed");
                hit=covering(0x3000,8);require(hit.status==Relation::Covered&&hit.fileOffset==8&&hit.readOrdinal==2,"an older covering read beat the newest");
                require(read(second,2,true,14,0x3004)==0,"ring read 3 failed");
                hit=covering(0x3000,8);require(hit.status==Relation::Ambiguous&&hit.readOrdinal==3&&hit.fileOffset==14,
                        "a newer partial overwrite did not make the relation ambiguous");
                hit=covering(0x3004,2);require(hit.status==Relation::Covered&&hit.readOrdinal==3,"the newest read did not cover its own range");
                require(read(second,4,true,100,0x5000)==0xc0000011 && covering(0x5000,4).status==Relation::None,"a failed (EOF) read was recorded");
                // A block spanning two consecutive chunks resolves to one linear file range; non-linear or gapped history does not.
                require(read(second,8,true,0,0x3800)==0 && read(second,8,true,8,0x3808)==0,"spanning fixture reads failed");
                hit=covering(0x3804,8);
                require(hit.status==Relation::Spanning&&hit.readCount==2&&hit.fileOffset==4&&hit.relative=="data/audio.snu",
                        "a claim spanning two consecutive reads did not resolve to one file offset");
                require(hit.pieces[0].begin==0x3804&&hit.pieces[0].end==0x3808&&hit.pieces[0].fileOffset==4&&hit.pieces[1].begin==0x3808&&hit.pieces[1].end==0x380C&&
                        hit.pieces[1].fileOffset==8&&hit.pieces[0].openId==2&&hit.pieces[1].openId==2&&hit.pieces[1].readOrdinal==hit.pieces[0].readOrdinal+1&&
                        hit.pieces[1].sequence==hit.pieces[0].sequence+1&&hit.pieces[2].end==0,"span pieces lost their ranges, offsets, open or order");
                hit=covering(0x3800,16);require(hit.status==Relation::Spanning&&hit.fileOffset==0&&hit.readCount==2,"exact two-read span differs");
                require(covering(0x3804,16).status==Relation::Ambiguous,"a span with unrecorded bytes was reported resolved");
                require(read(second,8,true,0,0x3900)==0 && read(second,8,true,0,0x3908)==0,"non-linear fixture reads failed");
                hit=covering(0x3904,8);require(hit.status==Relation::Ambiguous&&hit.readCount==2,"adjacent reads of non-adjacent file ranges were merged");
                require(hit.pieces[0].fileOffset==4&&hit.pieces[1].fileOffset==0&&hit.pieces[0].openId==hit.pieces[1].openId,"non-linear pieces lost their file offsets");
                // A read completing after the snapshot (another thread, after the copy) must not be taken as the source.
                {
                    require(read(second,8,true,0,0x3A00)==0,"snapshot fixture read 1 failed");
                    const auto snapshot=Simpsons::fileReadSequence();
                    require(read(second,4,true,8,0x3A02)==0,"snapshot fixture read 2 failed");
                    require(Simpsons::recentFileReadCovering(0x3A00,8).status==Relation::Ambiguous,"the unbounded query ignored the newer overlapping read");
                    hit=Simpsons::recentFileReadCovering(0x3A00,8,snapshot);
                    require(hit.status==Relation::Covered&&hit.fileOffset==0&&hit.readCount==1,"a read after the snapshot changed the attribution");
                    require(Simpsons::recentFileReadCovering(0x3A02,4,snapshot).status==Relation::Covered&&
                            Simpsons::recentFileReadCovering(0x3A02,4).fileOffset==8,"snapshot bound or newest-read lookup differs");
                }
                // Adjacent reads whose file offsets are linear but belong to different opens are not one stream.
                {
                    const auto third=open("Data/Audio.SNU");require(third<0x80000000,"second ring fixture open failed");
                    require(read(second,8,true,0,0x7100)==0&&read(third,8,true,8,0x7108)==0,"cross-open fixture reads failed");
                    hit=covering(0x7100,16);
                    require(hit.status==Relation::Ambiguous&&hit.readCount==2&&hit.pieces[0].openId!=hit.pieces[1].openId,"reads of different opens were merged into one span");
                    require(rt.closeHandle(third)==0,"second ring fixture close failed");
                }
                // More last-writer reads than retained pieces: the true count is kept, the first eight are listed.
                for(unsigned i=0;i<10;++i)require(read(second,1,true,i,0x7000+i)==0,"piece-limit fixture read failed");
                hit=covering(0x7000,10);
                require(hit.status==Relation::Spanning&&hit.readCount==10&&hit.fileOffset==0&&hit.pieces.size()==8&&hit.pieces[7].begin==0x7007&&hit.pieces[7].end==0x7008&&
                        hit.pieces[7].fileOffset==7,"a span of ten reads lost its count or its retained pieces");
                for(unsigned i=0;i<512;++i)require(read(second,1,true,0,0x6000+i)==0,"ring eviction read failed");
                require(covering(0x3000,8).status==Relation::None,"an evicted read still matched");
                require(covering(0x6000,1).status==Relation::Covered&&covering(0x61FF,1).status==Relation::Covered,"recent reads were lost");
                require(rt.closeHandle(second)==0,"ring fixture close failed");
            }
        }
        uint32_t handle=open("GaMe:/dAtA/mIxEd.BIN",syncFile,readAccess,0xfffffffdu,true);
        require(handle<0x80000000,"create FILE_OPEN failed");
        require(query(handle,5,24)==0 && load64(infoBuffer+8)==contents.size() && !ram[infoBuffer+21],"standard size/directory mismatch");
        require(load32(ios+4)==24 && ram[infoBuffer+24]==0xA5,"standard output extent mismatch");
        require(query(handle,34,56)==0 && load64(infoBuffer+40)==contents.size() && ram[infoBuffer+56]==0xA5,"network size extent mismatch");
        memset(ram.data()+buffer,0xCC,64);
        require(read(handle,3)==0 && load32(ios+4)==3 && memcmp(ram.data()+buffer,contents.data(),3)==0 && ram[buffer+3]==0xCC,"initial read mismatch");
        require(position(handle)==3,"read did not advance position");
        require(read(handle,20)==0 && load32(ios+4)==6 && memcmp(ram.data()+buffer,contents.data()+3,6)==0,"partial EOF read mismatch");
        require(read(handle,1)==0xc0000011 && load32(ios+4)==0,"EOF was not reported");
        require(read(handle,2,true,1)==0 && load32(ios+4)==2 && position(handle)==3,"atomic explicit seek/read mismatch");
        require(read(handle,1,true,UINT64_MAX)==0 && position(handle)==4,"Xbox current-position sentinel mismatch");
        require(read(handle,1,true,UINT64_MAX-1)==0 && position(handle)==5,"NT current-position sentinel mismatch");
        require(read(handle,1,true,UINT64_MAX-2)==invalid && position(handle)==5,"negative offset accepted");
        require(seek(handle,0x100000003ull)==0 && position(handle)==0x100000003ull,"64-bit position truncated");
        require(read(handle,1)==0xc0000011 && query(handle,5,24)==0 && load64(infoBuffer+8)==9,"seek grew original");
        require(seek(handle,0)==0 && load32(ios+4)==8,"seek completion mismatch");
        DWORD attempted=0;
        require(!WriteFile(rt.getHandle(handle)->native,"BAD",3,&attempted,nullptr),"native handle unexpectedly grants write access");
        require(read(handle,0,false,0,0)==0 && load32(ios+4)==0 && position(handle)==0,"zero read mismatch");
        require(query(handle,5,23)==0xc0000004 && ram[infoBuffer]==0xA5,"short query buffer modified");
        require(query(handle,99,64)==0xc0000003,"unknown query faked success");
        require(read(handle,16,false,0,0xfff8)==accessFault && position(handle)==0,"cross-boundary buffer accepted");
        require(read(handle,16,false,0,0xfffffff8)==accessFault,"guest wrap accepted");
        protectBuffer=true;require(read(handle,1)==accessFault,"read-only guest buffer accepted");protectBuffer=false;
        // Stop both before IOSB validation and after it, during buffer checking.
        // Neither path may translate shutdown into a guest access violation,
        // overwrite a validated IOSB, or consume native file data.
        for(bool duringBuffer:{false,true}) {
            store32(ios,0xdeadbeef);store32(ios+4,0x12345678);ram[buffer]=0xCC;
            auto cancelled=context();cancelled.r3.u32=handle;cancelled.r7.u32=ios;
            cancelled.r8.u32=buffer;cancelled.r9.u32=1;
            rt.stopReason="test pointer-check shutdown";
            stopOnGuestAddress=duringBuffer?buffer:0;
            rt.stopping.store(!duringBuffer,std::memory_order_release);
            bool propagated=false;
            try {__imp__NtReadFile(cancelled,ram.data());}
            catch(const Simpsons::Failure& failure) {
                propagated=std::string(failure.what())=="test pointer-check shutdown";
            }
            stopOnGuestAddress=0;rt.stopping.store(false,std::memory_order_release);
            require(propagated && cancelled.r3.u32==handle,"shutdown failure swallowed or replaced");
            require(load32(ios)==0xdeadbeef && load32(ios+4)==0x12345678 && ram[buffer]==0xCC,
                    "cancelled read changed guest outputs");
            require(position(handle)==0,"cancelled read consumed native file data");
        }
        for(unsigned kind=0;kind<3;++kind)
            require(read(handle,1,false,0,buffer,kind==0,kind==1,kind==2)==unsupported && position(handle)==0,"async/APC read accepted");
        auto ctx=context();ctx.r3.u32=handle;ctx.r7.u32=0xfffffffcu;ctx.r8.u32=buffer;ctx.r9.u32=1;
        __imp__NtReadFile(ctx,ram.data());require(ctx.r3.u32==accessFault && position(handle)==0,"invalid IOSB consumed file data");
        require(!MoveFileExW((rt.gameRoot/"Data").c_str(),(rt.gameRoot/"Moved").c_str(),0),"ancestor pin allowed rename");
        for(const char* alias:{"d:/Data/MiXeD.bin","\\Device\\Harddisk0\\Partition1\\Data\\MiXeD.bin","Data/MiXeD.bin"}) {
            auto id=open(alias);require(id<0x80000000,"verified alias/relative open failed");rt.closeHandle(id);
        }
        auto attributesOnly=open("Data/MiXeD.bin",syncFile,0x100080);
        require(attributesOnly<0x80000000 && read(attributesOnly,1)==denied,"attribute-only handle gained read-data access");
        rt.closeHandle(attributesOnly);
        // The original frontend loader uses 0x68: synchronous, non-directory,
        // unbuffered. Verify actual NT mode and direct guest-memory I/O through
        // both guest entry points, using the real volume/device alignment.
        const auto sector=nativeVolume(rt.getHandle(handle)->native).bytesPerSector;
        require(sector>=512 && sector<=4096 && !(sector&(sector-1)),"unqualified fixture sector size");
        std::string directBytes(3*sector+37,'\0');
        for(size_t i=0;i<directBytes.size();++i)directBytes[i]=char((i*29+i/sector)%251);
        const auto directAsset=rt.gameRoot/"Data"/"direct.bin";
        writeHost(directAsset,directBytes);const auto directLastWrite=std::filesystem::last_write_time(directAsset);
        for(bool createCall:{false,true}) {
            const auto direct=open("D:/Data/direct.bin",0x68,0x80100080,0xfffffffdu,createCall,1,1);
            require(direct<0x80000000,"original unbuffered readonly open failed");
            HANDLE native=rt.getHandle(direct)->native;
            require((nativeWordInformation(native,16)&0x28)==0x28,"native unbuffered/synchronous mode was stripped");
            const auto mask=nativeWordInformation(native,17);
            require(mask<=4095 && !(mask&(mask+1)),"unqualified fixture device alignment");
            const uintptr_t address=(uintptr_t(ram.data()+0x4000)+mask)&~uintptr_t(mask);
            const auto aligned=uint32_t(address-uintptr_t(ram.data()));
            require(uint64_t(aligned)+4*sector+16<=ram.size(),"unbuffered fixture exceeds guest RAM");
            memset(ram.data()+aligned,0xCC,4*sector+16);
            require(read(direct,2*sector,true,sector,aligned)==0 && load32(ios+4)==2*sector &&
                position(direct)==3*sector && !memcmp(ram.data()+aligned,directBytes.data()+sector,2*sector),
                "unbuffered aligned explicit read differs from original fixture bytes");
            require(seek(direct,0)==0 && read(direct,sector,false,0,aligned)==0 &&
                position(direct)==sector && !memcmp(ram.data()+aligned,directBytes.data(),sector),
                "unbuffered synchronous current-position read differs");
            require(read(direct,sector-1,true,0,aligned)==invalid && load32(ios+4)==0 && position(direct)==sector,
                "unbuffered length alignment error lost or consumed data");
            require(read(direct,sector,true,1,aligned)==invalid && load32(ios+4)==0 && position(direct)==sector,
                "unbuffered offset alignment error lost or consumed data");
            if(mask)require(read(direct,sector,true,0,aligned+1)==invalid && load32(ios+4)==0 && position(direct)==sector,
                "unbuffered device buffer alignment error lost or consumed data");
            require(seek(direct,1)==invalid && position(direct)==sector,"unaligned unbuffered seek accepted");
            memset(ram.data()+aligned,0xCC,4*sector+16);
            require(read(direct,4*sector,true,0,aligned)==0 && load32(ios+4)==directBytes.size() &&
                position(direct)==directBytes.size() && !memcmp(ram.data()+aligned,directBytes.data(),directBytes.size()),
                "unbuffered partial EOF read count/bytes/position differ");
            for(unsigned i=0;i<16;++i)require(ram[aligned+4*sector+i]==0xCC,"unbuffered read exceeded requested extent");
            require(read(direct,sector,true,4*sector,aligned)==0xc0000011 && load32(ios+4)==0,
                "unbuffered EOF replaced by successful data");
            DWORD written=0;require(!WriteFile(native,"BAD",3,&written,nullptr),"unbuffered handle gained write access");
            require(rt.closeHandle(direct)==0,"unbuffered handle close failed");
            require(readHost(directAsset)==directBytes && std::filesystem::last_write_time(directAsset)==directLastWrite,
                "unbuffered reads modified the original fixture");
        }
        puts("PASS native unbuffered assets: both open ABIs, retained mode, actual aligned data, native alignment failures, partial EOF and readonly handles");
        auto dir=open("game:/Data",0x21);require(dir<0x80000000,"directory open failed");
        auto relative=open("mixed.bin",syncFile,readAccess,dir);require(relative<0x80000000,"root-handle relative open failed");
        require(query(dir,5,24)==0 && ram[infoBuffer+21]==1,"directory query mismatch");
        require(read(dir,1)==0xc00000ba,"directory read accepted");
        require(open("Data",syncFile)==0xc00000ba && open("Data/Mixed.bin",0x21)==0xc0000103,"directory/file mismatch accepted");
        require(open("Data/Mixed.bin/",syncFile)==0xc0000103,"file trailing separator accepted");
        require(open("x",syncFile,readAccess,handle)==0xc0000103,"file used as relative directory");
        require(open("game:/Data/Mixed.bin",syncFile,readAccess,dir)==0xc0000033,"absolute alias with relative root accepted");
        auto rootHandle=open("game:/",0x21);require(rootHandle<0x80000000,"root directory open failed");rt.closeHandle(rootHandle);
        for(const char* path:{"game:/","D:","\\Device\\Harddisk0\\Partition1","gAmE:/dAtA"}) {
            auto volume=open(path,freeSpaceDirectory);
            require(volume<0x80000000,"original 0x00800021 volume-query open failed");
            verifyVolume(rt,volume);
            require(query(volume,5,24)==0 && ram[infoBuffer+21],"free-space flag lost directory type");
            require(read(volume,1)==0xc00000ba,"free-space directory allowed byte reads");
            rt.closeHandle(volume);
            require(queryVolume(volume)==0xc0000008 && load32(ios+4)==0 && ram[infoBuffer]==0xA5,
                    "closed volume handle accepted or wrote result");
        }
        auto volume=open("Data",freeSpaceDirectory,readAccess,0xfffffffdu,true);
        require(volume<0x80000000,"NtCreateFile FILE_OPEN free-space option failed");
        verifyVolume(rt,volume);
        require(queryVolume(volume,3,23)==0xc0000004 && ram[infoBuffer]==0xA5 && load32(ios+4)==0,
                "short volume result accepted/modified");
        for(uint32_t type:{0u,1u,2u,4u,5u,7u,UINT32_MAX})
            require(queryVolume(volume,type,64)==0xc0000003 && ram[infoBuffer]==0xA5 && load32(ios+4)==0,
                    "unsupported volume class faked success");
        for(uint32_t address:{0u,0xfff0u,0xfffffff0u})
            require(queryVolume(volume,3,24,address)==accessFault && load32(ios+4)==0,"invalid volume output accepted");
        protectBuffer=true;require(queryVolume(volume,3,24,buffer)==accessFault,"protected volume output accepted");protectBuffer=false;
        ram[0xffe7]=0xCC;
        require(queryVolume(volume,3,UINT32_MAX,0xffe8)==0 && ram[0xffe7]==0xCC && load32(ios+4)==24,
                "volume query did not validate/write exactly 24 bytes at guest boundary");
        ctx=context();ctx.r3.u32=volume;ctx.r4.u32=0xfffffffcu;ctx.r5.u32=infoBuffer;ctx.r6.u32=24;ctx.r7.u32=3;
        memset(ram.data()+infoBuffer,0xA5,64);__imp__NtQueryVolumeInformationFile(ctx,ram.data());
        require(ctx.r3.u32==accessFault && ram[infoBuffer]==0xA5,"invalid volume IOSB wrote result");
        ctx=context();ctx.r3.u32=volume;ctx.r4.u32=ios;ctx.r5.u32=buffer;ctx.r6.u32=24;ctx.r7.u32=3;
        store32(ios,0xdeadbeef);store32(ios+4,0x12345678);ram[buffer]=0xCC;
        stopOnGuestAddress=buffer;bool volumeCancelled=false;
        try {__imp__NtQueryVolumeInformationFile(ctx,ram.data());}
        catch(const Simpsons::Failure& failure) {volumeCancelled=std::string(failure.what())=="test pointer-check shutdown";}
        stopOnGuestAddress=0;rt.stopping.store(false,std::memory_order_release);
        require(volumeCancelled && ctx.r3.u32==volume && load32(ios)==0xdeadbeef && load32(ios+4)==0x12345678 && ram[buffer]==0xCC,
                "volume query swallowed cancellation or modified guest outputs");
        // Reach a real native failure through a service-owned object, without
        // creating a foreign handle or relying on a possibly reused closed ID.
        require(CloseHandle(rt.getHandle(volume)->native)!=0,"native failure fixture close failed");
        rt.getHandle(volume)->native=nullptr;
        require(queryVolume(volume)==0xc0000008 && load32(ios+4)==0 && ram[infoBuffer]==0xA5,
                "native volume failure was replaced or copied incomplete data");
        rt.closeHandle(volume);
        auto volumeFile=open("Data/MiXeD.bin",syncFile|0x00800000);
        require(volumeFile<0x80000000,"free-space file handle open failed");
        verifyVolume(rt,volumeFile);require(position(volumeFile)==0,"volume query moved file position");rt.closeHandle(volumeFile);
        verifyVolume(rt,dir);verifyVolume(rt,handle); // Native class 3 also accepts ordinary file/directory handles.
        require(open("Data/MiXeD.bin",freeSpaceDirectory)==0xc0000103,"free-space option bypassed directory check");
        require(open("Data",freeSpaceDirectory|0x1000)==denied,"free-space flag allowed delete-on-close");
        require(open("Data",freeSpaceDirectory,readAccess|2)==denied,"free-space flag allowed write access");
        require(open("Data",freeSpaceDirectory,readAccess,0xfffffffdu,true,3)==denied,"free-space flag allowed open-if");
        require(open("Data",freeSpaceDirectory|0x04000000)==unsupported,"unknown options accepted with free-space flag");
        require(open("game:/../outside",freeSpaceDirectory)>=0x80000000,"free-space open escaped root");
        require(open("\\Device\\Harddisk0\\Partition0",freeSpaceDirectory)==unsupported,"unverified volume alias accepted");
        for(const char* path:{"game:/../outside/secret.bin","game:/Data/../../outside/secret.bin","Data/./MiXeD.bin",
                              "Data/MiXeD.bin:stream","Data/MiXeD.bin.","Data/MiXeD.bin ","Data/*","Data//MiXeD.bin"})
            require(open(path)>=0x80000000,"unsafe path accepted");
        for(const char* path:{"save:/x","C:/Windows/win.ini","\\??\\game:\\Data\\MiXeD.bin",
                              "\\Device\\Harddisk0\\Partition10\\secret.bin","\\\\server\\share\\secret"})
            require(open(path)==unsupported,"unsupported alias accepted");
        require(open("Data/MISSING.bin")>=0x80000000,"missing file faked success");
        require(open("Data/MiXeD.bin",0x40)==unsupported && open("Data/MiXeD.bin",0x50)==unsupported,"async/alertable open accepted");
        require(open("Data/MiXeD.bin",syncFile|0x1000)==denied,"delete-on-close accepted");
        for(uint32_t access:{0x40000000u,0x100003u,0x100005u,0x110001u,0x100101u,0x02000000u})
            require(open("Data/MiXeD.bin",syncFile,access)==denied,"write/delete/maximum access accepted");
        for(uint32_t disposition:{0u,2u,3u,4u,5u})
            require(open("Data/MiXeD.bin",syncFile,readAccess,0xfffffffdu,true,disposition)==denied,"mutating disposition accepted");
        require(open("New.bin",syncFile,readAccess,0xfffffffdu,true,2)==denied && !std::filesystem::exists(rt.gameRoot/"New.bin"),"created an original asset");
        ctx=context();ctx.r3.u32=handle;ctx.r7.u32=ios;__imp__NtWriteFile(ctx,ram.data());require(ctx.r3.u32==denied && load32(ios+4)==0,"write accepted");
        ctx=context();__imp__NtDeleteFile(ctx,ram.data());require(ctx.r3.u32==denied,"delete accepted");
        for(uint32_t type:{4u,10u,11u,13u,19u,20u,30u}) {
            ctx=context();ctx.r3.u32=handle;ctx.r4.u32=ios;ctx.r5.u32=infoBuffer;ctx.r6.u32=64;ctx.r7.u32=type;
            __imp__NtSetInformationFile(ctx,ram.data());require(ctx.r3.u32==denied,"mutation via information class accepted");
        }
        name("game:/Data/MiXeD.bin");store32(ansi+4,0xfffffff0);
        ctx=context();ctx.r3.u32=output;ctx.r4.u32=readAccess;ctx.r5.u32=attrs;ctx.r6.u32=ios;ctx.r7.u32=3;ctx.r8.u32=syncFile;
        __imp__NtOpenFile(ctx,ram.data());require(ctx.r3.u32==accessFault,"wrapped counted string accepted");
        name(std::string("Data/Mi\0XeD.bin",15));ctx.r3.u32=output;
        __imp__NtOpenFile(ctx,ram.data());require(ctx.r3.u32==0xc0000033,"embedded NUL accepted");
        name("Data/MiXeD.bin");store32(ansi,0x00200001);ctx.r3.u32=output;
        __imp__NtOpenFile(ctx,ram.data());require(ctx.r3.u32==0xc0000033,"invalid counted-string lengths accepted");
        name("Data/MiXeD.bin");store32(ansi,0x10011001);ctx.r3.u32=output;
        __imp__NtOpenFile(ctx,ram.data());require(ctx.r3.u32==0xc0000106,"unbounded counted string accepted");
        name("Data/MiXeD.bin");ctx.r3.u32=buffer;protectBuffer=true;
        size_t beforeHandles=rt.handles.size();__imp__NtOpenFile(ctx,ram.data());protectBuffer=false;
        require(ctx.r3.u32==accessFault && rt.handles.size()==beforeHandles,"invalid handle output created a native file handle");
        name("Data/MiXeD.bin");ctx=context();ctx.r1.u32=0xffffffd0;ctx.r3.u32=output;ctx.r4.u32=readAccess;ctx.r5.u32=attrs;ctx.r6.u32=ios;
        __imp__NtCreateFile(ctx,ram.data());require(ctx.r3.u32==accessFault,"wrapped ninth argument accepted");
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        auto wrong=rt.addHandle(std::make_shared<Simpsons::KernelHandle>(event,Simpsons::KernelHandle::Type::Event));
        require(queryVolume(wrong)==0xc0000008,"non-file handle accepted for volume query");
        require(read(wrong,1)==0xc0000008,"non-file handle accepted");rt.closeHandle(wrong);
        HANDLE foreign=CreateFileW((tree.root/"outside"/"secret.bin").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        auto foreignId=rt.addHandle(std::make_shared<Simpsons::KernelHandle>(foreign,Simpsons::KernelHandle::Type::File));
        require(queryVolume(foreignId)==0xc0000008,"foreign file accepted for volume query");
        require(read(foreignId,1)==0xc0000008,"foreign native file handle accepted");rt.closeHandle(foreignId);
        for(const auto& target:{tree.root/"outside",rt.gameRoot/"Data"}) {
            auto link=rt.gameRoot/(tree.junctions.empty()?"Escape":"Inside");
            tree.junctions.push_back(link);junction(link,target);
            require(open(tree.junctions.size()==1?"Escape/secret.bin":"Inside/MiXeD.bin")>=0x80000000,"reparse component followed");
            require(open(link.filename().string(),0x21)>=0x80000000,"reparse directory handle returned");
            require(open(link.filename().string(),freeSpaceDirectory)>=0x80000000,"free-space flag followed reparse directory");
        }
        const auto realRoot=rt.gameRoot;
        rt.gameRoot=realRoot/"Escape";
        require(open("game:/secret.bin")>=0x80000000,"reparse root followed");
        std::filesystem::create_directory(tree.root/"outside"/"Child");
        writeHost(tree.root/"outside"/"Child"/"secret.bin","OUTSIDE CHILD");
        rt.gameRoot=realRoot/"Escape"/"Child";
        require(open("game:/secret.bin")>=0x80000000,"intermediate reparse in configured root followed");
        rt.gameRoot=realRoot;
        auto symlink=rt.gameRoot/"file-link.bin";
        if(CreateSymbolicLinkW(symlink.c_str(),(tree.root/"outside"/"secret.bin").c_str(),2)) {
            tree.junctions.push_back(symlink);
            require(open("game:/file-link.bin")>=0x80000000,"file symlink followed");
        } else {
            require(GetLastError()==ERROR_PRIVILEGE_NOT_HELD || GetLastError()==ERROR_INVALID_PARAMETER,
                    "file symlink fixture failed unexpectedly");
            puts("SKIP: file-symlink creation privilege unavailable (directory junction checks remain mandatory)");
        }
        require(seek(handle,0)==0,"concurrent read setup failed");
        std::vector<uint8_t> observed;std::mutex observedMutex;std::atomic<bool> workerFailed=false;
        auto worker=[&](uint32_t address) {
            for(;;) {
                auto call=context();call.r3.u32=handle;call.r7.u32=address;call.r8.u32=address+16;call.r9.u32=1;
                __imp__NtReadFile(call,ram.data());
                if(call.r3.u32==0xc0000011) break;
                if(call.r3.u32!=0 || load32(address+4)!=1) {workerFailed=true;break;}
                std::lock_guard lock(observedMutex);observed.push_back(ram[address+16]);
            }
        };
        std::thread first(worker,0x3000),second(worker,0x4000);first.join();second.join();
        std::vector<uint8_t> expected(contents.begin(),contents.end());
        std::sort(observed.begin(),observed.end());std::sort(expected.begin(),expected.end());
        require(!workerFailed && observed==expected && position(handle)==9,"concurrent synchronous reads duplicated/skipped bytes");
        rt.closeHandle(relative);rt.closeHandle(dir);require(rt.closeHandle(handle)==0,"close failed");
        require(read(handle,1)==0xc0000008 && rt.closeHandle(handle)==0xc0000008,"closed/stale handle accepted");
        require(MoveFileExW((rt.gameRoot/"Data").c_str(),(rt.gameRoot/"Moved").c_str(),0),"closed files retained ancestor pins");
        require(MoveFileExW((rt.gameRoot/"Moved").c_str(),(rt.gameRoot/"Data").c_str(),0),"fixture rename restore failed");
        require(readHost(asset)==contents && std::filesystem::last_write_time(asset)==lastWrite,"original fixture changed");
        require(readHost(tree.root/"outside"/"secret.bin")=="DO NOT READ","outside fixture changed");
        puts("PASS: asset ABI, read/EOF/position/size, native volume space, checked memory, shutdown propagation, aliases, readonly policy, root pins and reparse rejection");
        return 0;
    } catch(const std::exception& error) {fprintf(stderr,"FAIL: %s (Win32=%lu)\n",error.what(),GetLastError());return 1;}
}
