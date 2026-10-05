#include "native_saves.h"
#include <bcrypt.h>
#include <winternl.h>
#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace Simpsons::Platform {
namespace {
[[noreturn]] void fail(uint32_t code,const char* why){throw NativeSaveError(code,why);}
void need(bool value,const char* why){if(!value)fail(ERROR_INVALID_DATA,why);}
[[noreturn]] void osFail(const char* why){fail(GetLastError(),why);}
struct Handle {
    HANDLE h=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE value):h(value){}
    ~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
};
using Lease=std::shared_ptr<Handle>;
std::wstring finalName(HANDLE h){
    const DWORD n=GetFinalPathNameByHandleW(h,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);if(!n)osFail("Save path size query");
    std::wstring out(n,L'\0');const auto used=GetFinalPathNameByHandleW(h,out.data(),n,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!used||used>=n)osFail("Save path query");out.resize(used);while(out.size()>7&&out.back()==L'\\')out.pop_back();return out;
}
std::wstring extended(const std::filesystem::path& p){auto out=L"\\\\?\\"+p.lexically_normal().native();while(out.size()>7&&out.back()==L'\\')out.pop_back();return out;}
bool equal(const std::wstring& a,const std::wstring& b){return CompareStringOrdinal(a.data(),int(a.size()),b.data(),int(b.size()),TRUE)==CSTR_EQUAL;}
void kind(HANDLE h,bool dir){FILE_ATTRIBUTE_TAG_INFO tag{};if(!GetFileInformationByHandleEx(h,FileAttributeTagInfo,&tag,sizeof(tag)))osFail("Save object attributes");
    need(GetFileType(h)==FILE_TYPE_DISK&&!(tag.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&bool(tag.FileAttributes&FILE_ATTRIBUTE_DIRECTORY)==dir,"Invalid save object kind");
    if(!dir){BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(h,&info))osFail("Save file identity");need(info.nNumberOfLinks==1,"Save file has hard-link aliases");}}
Lease openNative(const std::filesystem::path& path,bool dir,DWORD access=GENERIC_READ,DWORD sharing=FILE_SHARE_READ){
    HANDLE h=CreateFileW(extended(path).c_str(),access|(dir?FILE_LIST_DIRECTORY:0),sharing,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|(dir?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr);
    if(h==INVALID_HANDLE_VALUE)osFail("Open native save object");Lease file;try{file=std::make_shared<Handle>(h);}catch(...){CloseHandle(h);throw;}
    kind(h,dir);need(equal(finalName(h),extended(path)),"Save object path changed");return file;
}
void component(const std::string& value,size_t bound=42){
    need(!value.empty()&&value.size()<=bound&&value!="."&&value!=".."&&value.back()!='.'&&value.back()!=' ',"Invalid native save component");
    for(unsigned char c:value)need(c>=32&&c<127&&std::string_view("\\/:*?\"<>|").find(char(c))==std::string_view::npos,"Invalid native save component character");
    std::string stem=value.substr(0,value.find('.'));for(char& c:stem)if(c>='a'&&c<='z')c=char(c-'a'+'A');
    need(stem!="CON"&&stem!="PRN"&&stem!="AUX"&&stem!="NUL"&&!(stem.size()==4&&(stem.starts_with("COM")||stem.starts_with("LPT"))&&stem[3]>='0'&&stem[3]<='9'),"Reserved native save component");
}
void owner(const std::string& id){need(id.size()==36,"Save owner must be a full native GUID");
    for(size_t i=0;i<id.size();++i)need(i==8||i==13||i==18||i==23?id[i]=='-':(id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f'),"Invalid save owner GUID");
    need(id[14]=='4'&&(id[19]=='8'||id[19]=='9'||id[19]=='a'||id[19]=='b'),"Invalid save owner GUID version");}
std::string titleName(uint32_t title){need(title!=0,"Save title missing");char out[9]{};if(sprintf_s(out,"%08X",title)!=8||out[8]!='\0') fail(ERROR_INVALID_DATA,"Save title formatting failed");return out;}
std::string utf8(const std::wstring& value){need(!value.empty()&&value.size()<=127,"Save display name extent differs");
    for(wchar_t c:value)need(c>=32&&c!=127,"Save display name contains control characters");
    int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);need(size>0,"Invalid UTF16 save display name");
    std::string result(size,'\0');need(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),result.data(),size,nullptr,nullptr)==size,"Save display conversion failed");return result;}
std::wstring utf16(const std::string& value){need(value.size()<=size_t(INT_MAX),"Saved display name too large");int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);need(size>0&&size<=127,"Invalid saved display name");
    std::wstring out(size,L'\0');need(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),size)==size,"Saved display conversion failed");(void)utf8(out);return out;}
void validate(const NativeSaveInfo& info){owner(info.profile);(void)titleName(info.title);component(info.name);(void)utf8(info.display);}
std::string hex(const unsigned char* bytes,size_t count){constexpr char digits[]="0123456789abcdef";std::string value;value.reserve(count*2);for(size_t i=0;i<count;++i){value+=digits[bytes[i]>>4];value+=digits[bytes[i]&15];}return value;}
std::string generation(){std::array<unsigned char,16> bytes{};need(BCryptGenRandom(nullptr,bytes.data(),ULONG(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"Native save generation RNG failed");return hex(bytes.data(),bytes.size());}
void validGeneration(const std::string& value){need(value.size()==32&&std::all_of(value.begin(),value.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');}),"Invalid save generation");}
std::string hash(const std::string& bytes){std::array<unsigned char,32> out{};if(bytes.size()>ULONG_MAX) fail(ERROR_INVALID_DATA,"Save hash input too large");need(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,bytes.empty()?nullptr:reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),ULONG(bytes.size()),out.data(),ULONG(out.size()))>=0,"Native save hash failed");return hex(out.data(),out.size());}
struct Directory {
    NativeStorage root;
    std::vector<Lease> pins;
    std::filesystem::path path;
    Directory(const std::filesystem::path& base,const std::filesystem::path& relative,bool create):root(queryNativeStorage(base)),path(root.path){
        need(!relative.is_absolute(),"Save relative root was absolute");
        for(const auto& part:relative){component(part.string(),64);path/=part;
            if(create&&!CreateDirectoryW(extended(path).c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)osFail("Create native save directory");
            pins.push_back(openNative(path,true,FILE_READ_ATTRIBUTES|SYNCHRONIZE,FILE_SHARE_READ|FILE_SHARE_WRITE));}
    }
    HANDLE handle() const{need(!pins.empty(),"Save directory has no native handle");return pins.back()->h;}
};
std::string read(const Lease& file){LARGE_INTEGER size{};if(!GetFileSizeEx(file->h,&size))osFail("Native save index size");need(size.QuadPart>0&&size.QuadPart<=2048,"Native save index length is invalid");
    std::string bytes(size_t(size.QuadPart),'\0');DWORD got{};if(!ReadFile(file->h,bytes.data(),DWORD(bytes.size()),&got,nullptr))osFail("Native save index read");need(got==bytes.size(),"Short native save index read");return bytes;}
std::string manifest(const NativeSaveInfo& info,const std::string& gen){validate(info);validGeneration(gen);
    const auto prefix="SIMPSONS-NATIVE-SAVE 2\n"+info.profile+"\n"+titleName(info.title)+"\n"+info.name+"\n"+utf8(info.display)+"\n"+gen+"\n";
    return prefix+"SHA256:"+hash(prefix)+"\n";}
struct Indexed {NativeSaveInfo info;std::string gen;std::shared_ptr<Directory> index,data;Lease record;};
Indexed indexed(const std::filesystem::path& root,const std::string& profile,uint32_t title,const std::string& name){
    owner(profile);component(name);Indexed result;result.index=std::make_shared<Directory>(root,std::filesystem::path("save-index")/profile/titleName(title),false);
    result.record=openNative(result.index->path/(name+".save"),false);
    const auto bytes=read(result.record);std::vector<std::string> lines;size_t at=0;
    while(at<bytes.size()){const auto end=bytes.find('\n',at);need(end!=std::string::npos,"Unterminated native save index");lines.push_back(bytes.substr(at,end-at));at=end+1;}
    need(lines.size()==7&&lines[0]=="SIMPSONS-NATIVE-SAVE 2"&&lines[1]==profile&&lines[2]==titleName(title)&&lines[3]==name,"Native save index identity differs");
    result.info={profile,title,name,utf16(lines[4])};result.gen=lines[5];need(bytes==manifest(result.info,result.gen),"Native save index checksum differs");
    try{result.data=std::make_shared<Directory>(root,std::filesystem::path("save-data")/profile/titleName(title)/name/result.gen,false);}
    catch(const NativeSaveError& e){if(e.code==ERROR_FILE_NOT_FOUND||e.code==ERROR_PATH_NOT_FOUND)fail(ERROR_INVALID_DATA,"Published save generation is missing");throw;}
    return result;
}
Indexed lookup(const std::filesystem::path& root,const std::string& profile,uint32_t title,const std::string& name){
    try{return indexed(root,profile,title,name);}catch(const NativeSaveError& e){if(e.code!=ERROR_FILE_NOT_FOUND&&e.code!=ERROR_PATH_NOT_FOUND)throw;}
    // Existing native v1 directory saves stay readable. Their first write is
    // copied into a new generation and published through the v2 index.
    Indexed result;result.index=std::make_shared<Directory>(root,std::filesystem::path("saves")/profile/titleName(title)/name,false);
    try{
        result.record=openNative(result.index->path/"metadata.txt",false);const auto bytes=read(result.record);
        constexpr std::string_view prefix="SIMPSONS-NATIVE-SAVE 1\n";
        need(bytes.starts_with(prefix)&&bytes.back()=='\n',"Legacy native save framing differs");
        result.info={profile,title,name,utf16(bytes.substr(prefix.size(),bytes.size()-prefix.size()-1))};validate(result.info);
        result.data=std::make_shared<Directory>(root,std::filesystem::path("saves")/profile/titleName(title)/name/"data",false);
    }catch(const NativeSaveError& e){if(e.code==ERROR_FILE_NOT_FOUND||e.code==ERROR_PATH_NOT_FOUND)fail(ERROR_INVALID_DATA,"Legacy native save is incomplete");throw;}
    return result;
}
std::vector<std::string> files(const Directory& dir){WIN32_FIND_DATAW data{};HANDLE search=FindFirstFileW((dir.path/L"*").c_str(),&data);
    if(search==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND)return {};osFail("Enumerate native save data");}
    struct Find{HANDLE h;~Find(){FindClose(h);}} cleanup{search};std::vector<std::string> result;
    do{std::wstring wide=data.cFileName;if(wide==L"."||wide==L"..")continue;
        need(result.size()<4096&&!(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)),"Unexpected native save data entry");
        std::string name;for(auto c:wide){need(c<128,"Non-ASCII native save data name");name+=char(c);}component(name);result.push_back(std::move(name));
    }while(FindNextFileW(search,&data));if(GetLastError()!=ERROR_NO_MORE_FILES)osFail("Continue native save data enumeration");
    std::sort(result.begin(),result.end());return result;
}
bool missing(const NativeSaveError& e){return e.code==ERROR_FILE_NOT_FOUND||e.code==ERROR_PATH_NOT_FOUND;}
std::string aliasName(std::string alias){component(alias,32);for(char& c:alias)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return alias;}
void writeAll(HANDLE file,const void* data,DWORD size){if(size==0) return;if(!data) fail(ERROR_INVALID_DATA,"Absent save write buffer");DWORD count{};if(!WriteFile(file,data,size,&count,nullptr))osFail("Write native save data");need(count==size,"Short native save write");}
}
struct NativeSaveFile::State {Lease file;std::shared_ptr<Directory> directory;bool writable{};uint32_t disposition{};};
NativeSaveFile::NativeSaveFile(std::unique_ptr<State> value):state(std::move(value)){}
NativeSaveFile::~NativeSaveFile()=default;
HANDLE NativeSaveFile::handle() const{return state->file->h;}
bool NativeSaveFile::writable() const{return state->writable;}
uint32_t NativeSaveFile::disposition() const{return state->disposition;}
void NativeSaveFile::flush(){if(state->writable&&!FlushFileBuffers(handle()))osFail("Flush native save data file");}
struct NativeSaveSession::State {
    std::filesystem::path root;NativeSaveInfo info;std::string gen;std::shared_ptr<Directory> data,lockDirectory;Lease identityLock;
    std::mutex mutex;std::vector<std::weak_ptr<NativeSaveFile>> files;bool changed{},closed{};
    State(std::filesystem::path r,NativeSaveInfo i):root(std::move(r)),info(std::move(i)){}
    void noFiles(){std::erase_if(files,[](const auto& f){return f.expired();});if(!files.empty())fail(ERROR_SHARING_VIOLATION,"Save session still owns open data files");}
    void fresh(bool copy){
        noFiles();const auto old=data;gen=generation();const auto relative=std::filesystem::path("save-data")/info.profile/titleName(info.title)/info.name/gen;
        // RNG collisions must not reopen another generation.
        Directory parent(root,relative.parent_path(),true);
        if(!CreateDirectoryW(extended(parent.path/gen).c_str(),nullptr))osFail("Create exclusive native save generation");
        auto next=std::make_shared<Directory>(root,relative,false);
        if(copy&&old)for(const auto& name:Platform::files(*old)){
            const auto source=openNative(old->path/name,false);HANDLE raw=CreateFileW(extended(next->path/name).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr);
            if(raw==INVALID_HANDLE_VALUE)osFail("Clone native save file");Handle target(raw);kind(raw,false);need(equal(finalName(raw),extended(next->path/name)),"Save clone escaped generation");
            std::array<uint8_t,65536> block{};for(;;){DWORD got{};if(!ReadFile(source->h,block.data(),DWORD(block.size()),&got,nullptr))osFail("Read previous native save generation");if(!got)break;writeAll(raw,block.data(),got);}
            if(!FlushFileBuffers(raw))osFail("Flush cloned native save data");
        }
        data=std::move(next);changed=true;
    }
    void publish(){
        noFiles();need(data!=nullptr,"Native save session has no data directory");
        const auto names=Platform::files(*data);need(!names.empty(),"An empty save container cannot be published");
        for(const auto& name:names){auto file=openNative(data->path/name,false,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ);if(!FlushFileBuffers(file->h))osFail("Flush completed native save payload");}
        Directory index(root,std::filesystem::path("save-index")/info.profile/titleName(info.title),true);
        Directory stage(root,std::filesystem::path("save-staging"),true);
        const auto pending=stage.path/(generation()+".tmp"),destination=index.path/(info.name+".save");
        const auto bytes=manifest(info,gen);HANDLE raw=CreateFileW(extended(pending).c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr);
        if(raw==INVALID_HANDLE_VALUE)osFail("Create native save index transaction");Handle file(raw);kind(raw,false);need(equal(finalName(raw),extended(pending)),"Save index transaction escaped root");
        bool committed=false;struct Rollback{HANDLE h;bool& committed;~Rollback(){if(!committed){FILE_DISPOSITION_INFO info{TRUE};SetFileInformationByHandle(h,FileDispositionInfo,&info,sizeof(info));}}} rollback{raw,committed};
        writeAll(raw,bytes.data(),DWORD(bytes.size()));if(!FlushFileBuffers(raw))osFail("Flush native save index transaction");
        const auto target=extended(destination);
        // The Win32 rename wrapper also consumes a terminated path, even
        // though FileNameLength itself excludes the terminator.
        if(target.size()>MAX_PATH*4 || target.size()*sizeof(wchar_t)>DWORD(-1)) fail(ERROR_INVALID_DATA,"Save destination too long");
        std::vector<uint8_t> rename(offsetof(FILE_RENAME_INFO,FileName)+(target.size()+1)*sizeof(wchar_t));
        auto* infoOut=reinterpret_cast<FILE_RENAME_INFO*>(rename.data());infoOut->ReplaceIfExists=TRUE;infoOut->RootDirectory=nullptr;infoOut->FileNameLength=DWORD(target.size()*sizeof(wchar_t));
        std::memcpy(infoOut->FileName,target.data(),infoOut->FileNameLength);
        if(!SetFileInformationByHandle(raw,FileRenameInfo,rename.data(),DWORD(rename.size())))osFail("Publish native save index atomically");committed=true;
        need(equal(finalName(raw),target),"Published save index escaped its native destination");
        if(!FlushFileBuffers(raw))osFail("Flush published native save index");changed=false;
    }
};
NativeSaveSession::NativeSaveSession(std::shared_ptr<State> value):state(std::move(value)){}
NativeSaveSession::~NativeSaveSession()=default;
const NativeSaveInfo& NativeSaveSession::info() const{return state->info;}
std::shared_ptr<NativeSaveFile> NativeSaveSession::openFile(const std::string& name,uint32_t access,uint32_t share,uint32_t disposition,uint32_t options){
    component(name);std::lock_guard lock(state->mutex);if(state->closed)fail(ERROR_INVALID_HANDLE,"Closed native save session");
    constexpr uint32_t allowed=GENERIC_READ|GENERIC_WRITE|SYNCHRONIZE|READ_CONTROL|FILE_READ_DATA|FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_READ_ATTRIBUTES|FILE_WRITE_ATTRIBUTES|FILE_READ_EA|FILE_WRITE_EA;
    need(!(access&~allowed)&&!(share&~7u)&&disposition<=5&&(options&0x30)==0x20&&!(options&~(0x20u|0x40u|4u|8u|0x800u)),"Unqualified native save file open");
    const bool write=(access&(GENERIC_WRITE|FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_ATTRIBUTES|FILE_WRITE_EA))!=0;
    if(disposition!=1&&!write)fail(ERROR_ACCESS_DENIED,"Save mutation requires write access");
    if((write||disposition!=1)&&!state->changed)state->fresh(true);
    if(write)state->changed=true;
    static const auto create=reinterpret_cast<NTSTATUS(NTAPI*)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,PLARGE_INTEGER,ULONG,ULONG,ULONG,ULONG,PVOID,ULONG)>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtCreateFile"));
    need(create!=nullptr,"Native save NT file service missing");
    std::wstring wide(name.begin(),name.end());UNICODE_STRING text{};text.Buffer=wide.data();text.Length=USHORT(wide.size()*2);text.MaximumLength=text.Length;
    OBJECT_ATTRIBUTES attrs{};attrs.Length=sizeof(attrs);attrs.RootDirectory=state->data->handle();attrs.ObjectName=&text;attrs.Attributes=0x40;
    IO_STATUS_BLOCK ios{};HANDLE raw=INVALID_HANDLE_VALUE;const auto status=create(&raw,access|FILE_READ_ATTRIBUTES,&attrs,&ios,nullptr,FILE_ATTRIBUTE_NORMAL,share&FILE_SHARE_READ,disposition,options|0x200000,nullptr,0);
    if(status<0){static const auto convert=reinterpret_cast<ULONG(NTAPI*)(NTSTATUS)>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlNtStatusToDosError"));need(convert!=nullptr,"NT save status conversion unavailable");fail(convert(status),"Native save file open failed");}
    need(status==0,"Synchronous native save open was pending");Lease nativeFile;try{nativeFile=std::make_shared<Handle>(raw);}catch(...){CloseHandle(raw);throw;}
    kind(raw,false);need(equal(finalName(raw),extended(state->data->path/name)),"Native save file escaped generation");
    auto value=std::make_unique<NativeSaveFile::State>();value->file=std::move(nativeFile);value->directory=state->data;value->writable=write;value->disposition=uint32_t(ios.Information);
    auto result=std::shared_ptr<NativeSaveFile>(new NativeSaveFile(std::move(value)));state->files.push_back(result);return result;
}
std::shared_ptr<NativeSaveFile> NativeSaveSession::openDirectory(uint32_t access,uint32_t share,uint32_t options){
    std::lock_guard lock(state->mutex);if(state->closed)fail(ERROR_INVALID_HANDLE,"Closed native save session");
    constexpr uint32_t allowed=GENERIC_READ|SYNCHRONIZE|READ_CONTROL|FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|FILE_READ_EA;
    if(access&~allowed)fail(ERROR_ACCESS_DENIED,"Save directory is read-only");
    need(!(share&~3u)&&(access&SYNCHRONIZE)&&(options&0x31u)==0x21u&&!(options&~0x4021u),"Unqualified native save directory open");
    // Reopen the pinned generation itself, relative to its held handle. Each
    // search owns an independent NT cursor and keeps the session from closing
    // or switching generations until that search handle is released.
    static const auto create=reinterpret_cast<NTSTATUS(NTAPI*)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,PLARGE_INTEGER,ULONG,ULONG,ULONG,ULONG,PVOID,ULONG)>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtCreateFile"));
    need(create!=nullptr,"Native save NT directory service missing");
    wchar_t empty{};UNICODE_STRING text{};text.Buffer=&empty;
    OBJECT_ATTRIBUTES attrs{};attrs.Length=sizeof(attrs);attrs.RootDirectory=state->data->handle();attrs.ObjectName=&text;attrs.Attributes=0x40;
    IO_STATUS_BLOCK ios{};HANDLE raw=INVALID_HANDLE_VALUE;
    const auto status=create(&raw,access|FILE_READ_ATTRIBUTES,&attrs,&ios,nullptr,0,share,1,options|0x200000u,nullptr,0);
    if(status<0){static const auto convert=reinterpret_cast<ULONG(NTAPI*)(NTSTATUS)>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlNtStatusToDosError"));need(convert!=nullptr,"NT directory status conversion unavailable");fail(convert(status),"Native save directory open failed");}
    need(status==0,"Synchronous native save directory open was pending");Lease file;try{file=std::make_shared<Handle>(raw);}catch(...){CloseHandle(raw);throw;}
    kind(raw,true);need(equal(finalName(raw),extended(state->data->path)),"Native save directory escaped generation");
    auto value=std::make_unique<NativeSaveFile::State>();value->file=std::move(file);value->directory=state->data;value->disposition=uint32_t(ios.Information);
    auto result=std::shared_ptr<NativeSaveFile>(new NativeSaveFile(std::move(value)));state->files.push_back(result);return result;
}
void NativeSaveSession::flush(){std::lock_guard lock(state->mutex);if(state->closed)fail(ERROR_INVALID_HANDLE,"Closed native save session");
    for(auto& weak:state->files)if(auto file=weak.lock())file->flush();
    state->noFiles();if(state->changed)state->publish();}
struct NativeSaveStore::State {std::filesystem::path root;mutable std::mutex mutex;std::unordered_map<std::string,std::shared_ptr<NativeSaveSession>> sessions;explicit State(std::filesystem::path r):root(std::move(r)){(void)queryNativeStorage(root);}};
NativeSaveStore::NativeSaveStore(std::filesystem::path root):state(std::make_unique<State>(std::filesystem::absolute(root).lexically_normal())){}
NativeSaveStore::~NativeSaveStore()=default;
uint32_t NativeSaveStore::open(const std::string& alias,const NativeSaveInfo& info,uint32_t mode){
    validate(info);const auto key=aliasName(alias);need(mode>=1&&mode<=5,"Unsupported native save disposition");std::lock_guard lock(state->mutex);
    if(state->sessions.contains(key))fail(ERROR_SHARING_VIOLATION,"Native save alias already owns a session");
    auto value=std::make_shared<NativeSaveSession::State>(state->root,info);
    value->lockDirectory=std::make_shared<Directory>(state->root,std::filesystem::path("save-locks")/info.profile/titleName(info.title),true);
    const auto lockPath=value->lockDirectory->path/(info.name+".lock");
    HANDLE lockFile=CreateFileW(extended(lockPath).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(lockFile==INVALID_HANDLE_VALUE)osFail("Lock native save identity");
    try{value->identityLock=std::make_shared<Handle>(lockFile);}catch(...){CloseHandle(lockFile);throw;}
    kind(lockFile,false);need(equal(finalName(lockFile),extended(lockPath)),"Native save identity lock escaped root");
    LARGE_INTEGER lockSize{};if(!GetFileSizeEx(lockFile,&lockSize))osFail("Inspect native save identity lock");need(lockSize.QuadPart==0,"Unexpected data in native save identity lock");
    std::unique_ptr<Indexed> old;try{old=std::make_unique<Indexed>(lookup(state->root,info.profile,info.title,info.name));}catch(const NativeSaveError& e){if(!missing(e))throw;}
    if(mode==1&&old)fail(ERROR_ALREADY_EXISTS,"Native save already exists");if((mode==3||mode==5)&&!old)fail(ERROR_PATH_NOT_FOUND,"Native save does not exist");
    const bool fresh=mode==1||mode==2||mode==5||!old;
    if(fresh)value->fresh(false);else{value->data=old->data;value->gen=old->gen;value->info.display=old->info.display;}
    state->sessions.emplace(key,std::shared_ptr<NativeSaveSession>(new NativeSaveSession(std::move(value))));return fresh?1:2;
}
std::shared_ptr<NativeSaveSession> NativeSaveStore::find(const std::string& alias) const{const auto key=aliasName(alias);std::lock_guard lock(state->mutex);auto found=state->sessions.find(key);return found==state->sessions.end()?nullptr:found->second;}
bool NativeSaveStore::exists(const NativeSaveInfo& info) const{validate(info);std::lock_guard lock(state->mutex);
    for(const auto& [alias,session]:state->sessions){(void)alias;const auto& current=session->info();if(current.profile==info.profile&&current.title==info.title&&current.name==info.name)return true;}
    try{(void)lookup(state->root,info.profile,info.title,info.name);return true;}catch(const NativeSaveError& e){if(missing(e))return false;throw;}}
void NativeSaveStore::flush(const std::string& alias){auto session=find(alias);if(!session)fail(ERROR_INVALID_HANDLE,"Native save alias is not open");session->flush();}
void NativeSaveStore::close(const std::string& alias){const auto key=aliasName(alias);std::lock_guard lock(state->mutex);auto found=state->sessions.find(key);if(found==state->sessions.end())fail(ERROR_INVALID_HANDLE,"Native save alias is not open");
    const auto held=found->second;auto& current=*held->state;std::lock_guard sessionLock(current.mutex);current.noFiles();if(current.changed)current.publish();current.closed=true;
    current.identityLock.reset();current.lockDirectory.reset();state->sessions.erase(found);}
ContentSnapshot scanIndexedNativeSaves(const std::filesystem::path& root,const std::string& profile,uint32_t title){
    owner(profile);ContentSnapshot result;std::shared_ptr<Directory> index;
    if(GetFileAttributesW(root.c_str())==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)return result;fail(error,"Inspect indexed native save root");}
    try{index=std::make_shared<Directory>(root,std::filesystem::path("save-index")/profile/titleName(title),false);}catch(const NativeSaveError& e){if(missing(e))return result;throw;}
    WIN32_FIND_DATAW data{};HANDLE search=FindFirstFileW((index->path/L"*").c_str(),&data);if(search==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND)return result;osFail("Enumerate native save index");}
    struct Find{HANDLE h;~Find(){FindClose(h);}} cleanup{search};std::vector<std::string> names;
    do{std::wstring wide=data.cFileName;if(wide==L"."||wide==L"..")continue;need(names.size()<4096&&!(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&wide.ends_with(L".save"),"Unexpected native save index entry");
        std::string name;for(auto c:wide.substr(0,wide.size()-5)){need(c<128,"Non-ASCII save index name");name+=char(c);}component(name);names.push_back(std::move(name));
    }while(FindNextFileW(search,&data));if(GetLastError()!=ERROR_NO_MORE_FILES)osFail("Continue native save index enumeration");std::sort(names.begin(),names.end());
    for(const auto& name:names){auto record=indexed(root,profile,title,name);const auto payloads=files(*record.data);need(!payloads.empty(),"Published native save has no payload files");
        ContentSnapshot::Record dataRecord{};dataRecord[3]=1;dataRecord[7]=1;
        for(size_t i=0;i<record.info.display.size();++i){dataRecord[8+i*2]=uint8_t(record.info.display[i]>>8);dataRecord[9+i*2]=uint8_t(record.info.display[i]);}
        need(name.size()<=0x2C,"Native save name exceeds content record");
        std::memcpy(dataRecord.data()+0x108,name.data(),name.size());
        dataRecord[0x108+name.size()]=0;
        result.records.push_back(dataRecord);
        result.leases.push_back(record.index);result.leases.push_back(record.data);result.leases.push_back(record.record);
        for(const auto& file:payloads)result.leases.push_back(openNative(record.data->path/file,false));
    }
    return result;
}
}
