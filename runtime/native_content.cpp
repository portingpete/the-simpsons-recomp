#include "native_content.h"
#include "native_saves.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <optional>
#include <string>
#include <string_view>

namespace Simpsons::Platform {
namespace {
struct File {
    HANDLE handle;
    explicit File(HANDLE h):handle(h){}
    ~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
};
using Lease=std::shared_ptr<File>;
[[noreturn]] void bad(const std::string& why){throw std::runtime_error("Native content store: "+why);}
[[noreturn]] void windowsError(const char* operation){bad(std::string(operation)+" failed (Windows "+std::to_string(GetLastError())+")");}
void need(bool ok,const char* why){if(!ok)bad(why);}
uint32_t be32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
void put32(uint8_t* p,uint32_t v){for(uint32_t i=0;i<4;++i)p[i]=uint8_t(v>>(24-i*8));}
bool equal(const std::wstring& a,const std::wstring& b){return CompareStringOrdinal(a.data(),int(a.size()),b.data(),int(b.size()),TRUE)==CSTR_EQUAL;}
std::wstring finalPath(HANDLE h){
    const auto n=GetFinalPathNameByHandleW(h,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);if(!n)windowsError("Query content path");
    std::wstring s(n,L'\0');const auto used=GetFinalPathNameByHandleW(h,s.data(),n,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!used||used>=n)windowsError("Read content path");s.resize(used);while(s.size()>7&&s.back()==L'\\')s.pop_back();return s;
}
Lease open(const std::filesystem::path& p,bool directory,bool missing){
    auto name=std::filesystem::path(p).make_preferred().native();need(p.is_absolute()&&name.size()>=3&&name[1]==L':',"Content path must be an absolute drive path");
    name=L"\\\\?\\"+name;while(name.size()>7&&name.back()==L'\\')name.pop_back();
    HANDLE h=CreateFileW(name.c_str(),(directory?FILE_LIST_DIRECTORY:GENERIC_READ)|FILE_READ_ATTRIBUTES|SYNCHRONIZE,
        FILE_SHARE_READ|(directory?FILE_SHARE_WRITE:0),nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr);
    if(h==INVALID_HANDLE_VALUE){const auto e=GetLastError();if(missing&&(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND))return {};windowsError("Open content");}
    Lease lease;try{lease=std::make_shared<File>(h);}catch(...){CloseHandle(h);throw;}
    FILE_ATTRIBUTE_TAG_INFO info{};if(!GetFileInformationByHandleEx(h,FileAttributeTagInfo,&info,sizeof(info)))windowsError("Inspect content object");
    need(!(info.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),"Content reparse point is unsupported");
    need(bool(info.FileAttributes&FILE_ATTRIBUTE_DIRECTORY)==directory&&GetFileType(h)==FILE_TYPE_DISK,"Content object type differs");
    need(equal(finalPath(h),name),"Content path changed through an alias");return lease;
}
bool directories(const std::filesystem::path& input,ContentSnapshot& result){
    const auto path=std::filesystem::absolute(input).lexically_normal();
    auto part=path.root_path();auto root=open(part,true,false);result.leases.push_back(root);
    for(const auto& component:path.relative_path()){
        need(component!=L"."&&component!=L"..","Content path contains traversal");part/=component;
        auto next=open(part,true,true);if(!next)return false;result.leases.push_back(next);
    }
    return true;
}
std::string filename(const std::wstring& name){
    need(!name.empty()&&name.size()<=42&&name!=L"."&&name!=L".."&&name.back()!=L'.'&&name.back()!=L' ',"Content filename exceeds original ABI bounds");
    std::string out;for(auto c:name){need(c>=32&&c<127&&std::wstring(L"\\/:*?\"<>|").find(c)==std::wstring::npos,"Unsupported content filename");out.push_back(char(c));}return out;
}
ContentSnapshot::Record metadata(const Lease& file,const std::string& name,uint32_t title,uint32_t language,uint32_t device){
    // XContentHeader344 + packed XContentMetadata93D6, independently checked
    // against the reference declarations. Read bounded metadata, not payload.
    std::array<uint8_t,0x971A> header{};LARGE_INTEGER size{};
    if(!GetFileSizeEx(file->handle,&size))windowsError("Get package size");
    need(size.QuadPart>=static_cast<LONGLONG>(header.size()),"Truncated content header");
    DWORD got{};if(!ReadFile(file->handle,header.data(),DWORD(header.size()),&got,nullptr))windowsError("Read package header");
    need(got==header.size(),"Short package header read");const auto* b=header.data();
    need(be32(b)==0x434F4E20||be32(b)==0x4C495645||be32(b)==0x50495253,"Unsupported content package magic");
    const auto headerSize=be32(b+0x340),version=be32(b+0x348);
    need(headerSize>=header.size()&&uint64_t(headerSize)<=uint64_t(size.QuadPart),"Content header extent exceeds its file");
    need((version==1||version==2)&&be32(b+0x344)==2&&be32(b+0x360)==title,"Content metadata version/type/title differs from its installed location");
    need(be32(b+0x3A9)==0&&b[0x379]==0x24,"Unsupported content volume metadata");
    // Marketplace data lives under the common owner; do not manufacture an XUID.
    for(uint32_t i=0;i<8;++i)need(!b[0x371+i],"Profile-owned marketplace package is unqualified");
    const uint32_t lang=language-1;
    uint32_t display=lang<9?0x411+lang*256:(version>=2?0x541A+(lang-9)*256:0x411);
    if(!b[display]&&!b[display+1])display=0x411;
    ContentSnapshot::Record record{};put32(record.data(),device);put32(record.data()+4,2);
    // XCONTENT_DATA always has128 UTF16 code units. The enumerator's string
    // setter copies at most127, zeros the terminator and all following bytes.
    for(uint32_t i=0;i<127;++i){const auto h=b[display+2*i],l=b[display+2*i+1];if(!h&&!l)break;record[8+2*i]=h;record[9+2*i]=l;}
    std::copy(name.begin(),name.end(),record.begin()+0x108);return record;
}
void scan(const std::filesystem::path& root,uint32_t title,uint32_t language,uint32_t device,ContentSnapshot& result){
    wchar_t titleName[9]{};swprintf_s(titleName,L"%08X",title);
    const auto path=std::filesystem::absolute(root/L"0000000000000000"/titleName/L"00000002").lexically_normal();
    if(!directories(path,result)){
        std::fprintf(stderr,"[CONTENT STORE] device=%u path=%ls missing; zero installed marketplace entries\n",device,path.c_str());return;
    }
    WIN32_FIND_DATAW data{};HANDLE find=FindFirstFileW((path/L"*").c_str(),&data);
    if(find==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND)return;windowsError("Enumerate content directory");}
    struct Find {HANDLE h;~Find(){FindClose(h);}} closer{find};
    std::vector<std::wstring> names;
    do{
        const std::wstring name=data.cFileName;if(name==L"."||name==L"..")continue;
        need(names.size()<4096,"Content catalog exceeds bounded package count");
        need(!(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)),"Extracted-directory/reparse content package is not yet supported");
        filename(name);names.push_back(name);
    }while(FindNextFileW(find,&data));
    if(GetLastError()!=ERROR_NO_MORE_FILES)windowsError("Continue content enumeration");
    std::sort(names.begin(),names.end());
    for(const auto& name:names){auto file=open(path/name,false,false);auto record=metadata(file,filename(name),title,language,device);
        result.records.push_back(record);result.leases.push_back(std::move(file));}
    std::fprintf(stderr,"[CONTENT STORE] device=%u path=%ls packages=%zu; original metadata only, no mount/license claim\n",device,path.c_str(),names.size());
}
std::vector<std::wstring> saveEntries(const std::filesystem::path& path,bool directory){
    WIN32_FIND_DATAW data{};HANDLE find=FindFirstFileW((path/L"*").c_str(),&data);
    if(find==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND)return {};windowsError("Enumerate native saves");}
    struct Find {HANDLE h;~Find(){FindClose(h);}} closer{find};std::vector<std::wstring> names;
    do{
        const std::wstring name=data.cFileName;if(name==L"."||name==L"..")continue;
        need(names.size()<4096,"Native save directory exceeds entry bound");
        need(!(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&bool(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==directory,
            "Native save entry has unsupported object type");
        filename(name);names.push_back(name);
    }while(FindNextFileW(find,&data));
    if(GetLastError()!=ERROR_NO_MORE_FILES)windowsError("Continue native save enumeration");
    std::sort(names.begin(),names.end());return names;
}
Lease saveFile(const std::filesystem::path& path){
    auto file=open(path,false,false);BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file->handle,&info))windowsError("Inspect native save file");
    need(info.nNumberOfLinks==1,"Native save file has multiple hard links");return file;
}
ContentSnapshot::Record saveMetadata(const std::filesystem::path& path,const std::string& name,ContentSnapshot& result){
    // A native directory catalog, not an STFS container or a fabricated save.
    // metadata.txt is published last by a future save writer. Enumeration
    // requires actual data files, but does not claim their game payload is valid.
    auto file=saveFile(path/L"metadata.txt");LARGE_INTEGER size{};
    if(!GetFileSizeEx(file->handle,&size))windowsError("Get native save metadata size");
    need(size.QuadPart>0&&size.QuadPart<=1024,"Native save metadata size is invalid");
    std::string bytes(size_t(size.QuadPart),'\0');DWORD got{};
    if(!ReadFile(file->handle,bytes.data(),DWORD(bytes.size()),&got,nullptr))windowsError("Read native save metadata");
    need(got==bytes.size(),"Short native save metadata read");
    constexpr std::string_view magic="SIMPSONS-NATIVE-SAVE 1\n";
    need(bytes.starts_with(magic)&&bytes.back()=='\n',"Native save metadata framing/version differs");
    const std::string display=bytes.substr(magic.size(),bytes.size()-magic.size()-1);
    need(!display.empty(),"Native save display name is absent");
    for(unsigned char c:display)need(c>=32&&c!=127,"Native save display name contains a control character");
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,display.data(),int(display.size()),nullptr,0);
    need(count>0&&count<=127,"Native save display name is invalid UTF8 or exceeds original extent");
    std::wstring wide(size_t(count),L'\0');
    need(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,display.data(),int(display.size()),wide.data(),count)==count,"Native save display conversion failed");
    ContentSnapshot::Record record{};put32(record.data(),1);put32(record.data()+4,1);
    for(size_t i=0;i<wide.size();++i){record[8+2*i]=uint8_t(wide[i]>>8);record[9+2*i]=uint8_t(wide[i]);}
    std::copy(name.begin(),name.end(),record.begin()+0x108);result.leases.push_back(std::move(file));
    need(directories(path/L"data",result),"Published native save has no data directory");
    const auto files=saveEntries(path/L"data",false);need(!files.empty(),"Published native save has no data files");
    for(const auto& entry:files)result.leases.push_back(saveFile(path/L"data"/entry));
    return record;
}
}
ContentSnapshot scanContent(const std::filesystem::path& installed,const std::filesystem::path& disc,uint32_t title,uint32_t language,uint32_t device){
    need(!installed.empty()&&!disc.empty()&&title&&language>=1&&language<=12&&device<=2,"Invalid native content catalog configuration");
    ContentSnapshot result;if(device!=2)scan(installed,title,language,1,result);if(device!=1)scan(disc,title,language,2,result);return result;
}
ContentSnapshot scanNativeSaves(const std::filesystem::path& installed,const std::string& profile,uint32_t title,uint32_t device){
    need(!installed.empty()&&title&&device<=1,"Invalid native save catalog configuration");
    need(profile.size()==36,"Native save owner must be a full profile GUID");
    for(size_t i=0;i<profile.size();++i){const char c=profile[i];
        need(i==8||i==13||i==18||i==23?c=='-':(c>='0'&&c<='9')||(c>='a'&&c<='f'),"Invalid native save owner GUID");}
    need(profile[14]=='4'&&(profile[19]=='8'||profile[19]=='9'||profile[19]=='a'||profile[19]=='b'),"Unqualified native save owner GUID");
    wchar_t titleName[9]{};swprintf_s(titleName,L"%08X",title);
    const auto path=std::filesystem::absolute(installed/L"saves"/profile/titleName).lexically_normal();
    ContentSnapshot result;
    if(directories(path,result))for(const auto& name:saveEntries(path,true)){
        need(directories(path/name,result),"Native save disappeared during enumeration");
        result.records.push_back(saveMetadata(path/name,filename(name),result));
    }
    auto indexed=scanIndexedNativeSaves(installed,profile,title);
    auto recordName=[](const ContentSnapshot::Record& r){
        const char* ptr=reinterpret_cast<const char*>(r.data()+0x108);
        size_t len=strnlen(ptr,0x134-0x108);
        return std::string(ptr,len);
    };
    for(auto& record:indexed.records){
        const std::string name=recordName(record);
        auto found=std::find_if(result.records.begin(),result.records.end(),[&](const auto& old){return name==recordName(old);});
        if(found==result.records.end())result.records.push_back(std::move(record));else *found=std::move(record);
    }
    need(result.records.size()<=4096,"Combined native save catalog exceeds entry bound");
    std::sort(result.records.begin(),result.records.end(),[&](const auto& a,const auto& b){return recordName(a)<recordName(b);});
    result.leases.insert(result.leases.end(),std::make_move_iterator(indexed.leases.begin()),std::make_move_iterator(indexed.leases.end()));
    std::fprintf(stderr,"[CONTENT STORE] profile=%s path=%ls native saves=%zu; real directory snapshot\n",profile.c_str(),path.c_str(),result.records.size());
    return result;
}
namespace {
std::optional<NativeStorage> queryStorage(const std::filesystem::path& installed,bool allowMissing){
    need(!installed.empty(),"Native storage location is absent");
    const auto path=std::filesystem::absolute(installed).lexically_normal();
    need(!path.relative_path().empty()&&GetDriveTypeW(path.root_path().c_str())==DRIVE_FIXED,"Native storage must be a folder on a fixed local volume");
    ContentSnapshot ownership;
    if(!directories(path,ownership)){
        need(allowMissing,"Configured native storage folder does not exist");
        return std::nullopt;
    }
    ULARGE_INTEGER available{},total{},free{};
    if(!GetDiskFreeSpaceExW(path.c_str(),&available,&total,&free))windowsError("Query native storage capacity");
    std::array<wchar_t,261> label{};
    const auto folder=std::static_pointer_cast<File>(ownership.leases.back());
    if(!GetVolumeInformationByHandleW(folder->handle,label.data(),DWORD(label.size()),nullptr,nullptr,nullptr,nullptr,0))
        windowsError("Query native storage volume label");
    return NativeStorage{path,available.QuadPart,total.QuadPart,free.QuadPart,label.data(),std::move(ownership.leases)};
}
}
NativeStorage queryNativeStorage(const std::filesystem::path& installed){
    return std::move(*queryStorage(installed,false));
}
bool nativeStorageAvailable(const std::filesystem::path& installed){
    return queryStorage(installed,true).has_value();
}
NativeStorage inspectNativeStorage(const std::filesystem::path& installed){
    auto result=queryNativeStorage(installed);const auto& path=result.path;
    // A capacity query alone proves neither write access nor durable commits.
    // Random CREATE_NEW ownership cannot replace any existing file. Delete on
    // close applies only to this opened object, including exception unwinding.
    std::array<unsigned char,16> random{};
    need(BCryptGenRandom(nullptr,random.data(),ULONG(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"Native storage probe identity allocation failed");
    constexpr wchar_t digits[]=L"0123456789abcdef";std::wstring name=L".simpsons-storage-probe-";
    for(const auto c:random){name.push_back(digits[c>>4]);name.push_back(digits[c&15]);}
    const auto expected=std::wstring(L"\\\\?\\")+(path/name).make_preferred().native();
    HANDLE raw=CreateFileW(expected.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
    if(raw==INVALID_HANDLE_VALUE)windowsError("Create native storage write probe");
    File probe(raw);FILE_ATTRIBUTE_TAG_INFO info{};
    if(!GetFileInformationByHandleEx(raw,FileAttributeTagInfo,&info,sizeof(info)))windowsError("Inspect native storage write probe");
    need(!(info.FileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&GetFileType(raw)==FILE_TYPE_DISK&&equal(finalPath(raw),expected),"Native storage write probe ownership differs");
    constexpr char bytes[]="SIMPSONS NATIVE STORAGE PROBE\n";DWORD written{};
    if(!WriteFile(raw,bytes,DWORD(sizeof(bytes)-1),&written,nullptr))windowsError("Write native storage probe");
    need(written==sizeof(bytes)-1,"Short native storage probe write");
    if(!FlushFileBuffers(raw))windowsError("Flush native storage probe");
    return result;
}
}
