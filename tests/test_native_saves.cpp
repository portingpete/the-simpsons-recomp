#include "runtime/native_saves.h"
#include "runtime/native_local_players.h"
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace {
using namespace Simpsons::Platform;size_t checks{};
void need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F action,uint32_t status=0){bool failed=false;try{action();}catch(const NativeSaveError& e){failed=!status||e.code==status;if(!failed)std::fprintf(stderr,"Actual save status %u, expected %u: %s\n",e.code,status,e.what());}need(failed,"Native save request was not rejected with its actual error");}
struct Temp {std::filesystem::path root;Temp(){wchar_t buffer[MAX_PATH]{};need(GetTempPathW(MAX_PATH,buffer)!=0,"Temporary path unavailable");const auto parent=std::filesystem::canonical(buffer);
    root=parent/("SimpsonsNativeSaves-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));need(root.parent_path()==parent&&std::filesystem::create_directory(root),"Unique contained temporary root creation failed");}
    ~Temp(){std::error_code e;std::filesystem::remove_all(root,e);}};
std::string read(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);need(bool(f),"Read actual save fixture failed");return {std::istreambuf_iterator<char>(f),{}};}
void write(const std::shared_ptr<NativeSaveFile>& file,const std::string& bytes){DWORD count{};need(WriteFile(file->handle(),bytes.data(),DWORD(bytes.size()),&count,nullptr)!=FALSE&&count==bytes.size(),"Real native save write failed");file->flush();}
std::string read(const std::shared_ptr<NativeSaveFile>& file){std::array<char,128> bytes{};DWORD count{};need(ReadFile(file->handle(),bytes.data(),DWORD(bytes.size()),&count,nullptr)!=FALSE,"Read native save data handle failed");return {bytes.data(),count};}
std::string title(uint32_t v){char result[9]{};sprintf_s(result,"%08X",v);return result;}
}
int main(){try{
    Temp temp;const auto root=temp.root/"content";std::filesystem::create_directory(root);
    NativeLocalPlayers players(temp.root/"profiles");const auto player=players.create("Native save");players.activate(0,player.id);
    const NativeSaveInfo info{player.id,0x45410809,"SlotA",L"Actual native \u03A9 save"};
    NativeSaveStore store(root),other(root);need(!store.exists(info)&&scanNativeSaves(root,info.profile,info.title).records.empty(),"Empty native save store invented contents");
    need(store.open("rmcsave",info,1)==1,"Native CREATE_NEW did not create a session");auto session=store.find("RMCSAVE");need(session&&session->info().profile==player.id,"Native alias lost its actual owner");
    rejects([&]{other.open("other",info,1);},ERROR_SHARING_VIOLATION);
    rejects([&]{store.close("rmcsave");});need(scanNativeSaves(root,info.profile,info.title).records.empty(),"Empty uncommitted generation was published");
    auto file=session->openFile("payload.bin",GENERIC_READ|GENERIC_WRITE|SYNCHRONIZE,0,3,0x60);
    need(file->writable()&&file->disposition()==2,"Native data CREATE disposition differs");write(file,"original payload");
    rejects([&]{store.flush("rmcsave");},ERROR_SHARING_VIOLATION);rejects([&]{store.close("rmcsave");},ERROR_SHARING_VIOLATION);
    file.reset();store.flush("rmcsave");need(store.exists(info),"Flushed native save index was not published");store.close("rmcsave");
    need(!store.find("rmcsave"),"Closed native alias retained its session");rejects([&]{session->openFile("payload.bin",GENERIC_READ|SYNCHRONIZE,1,1,0x60);},ERROR_INVALID_HANDLE);session.reset();
    const auto index=root/"save-index"/player.id/title(info.title)/"SlotA.save";const auto firstIndex=read(index);
    need(firstIndex.starts_with("SIMPSONS-NATIVE-SAVE 2\n"+player.id+"\n45410809\nSlotA\n"),"Published index identity differs from actual profile/title/name");
    auto snapshot=scanNativeSaves(root,info.profile,info.title);need(snapshot.records.size()==1&&snapshot.records[0][3]==1&&snapshot.records[0][7]==1,"Native save enumeration missed the committed real generation");
    need(store.open("rmcsave",info,3)==2,"Native OPEN_EXISTING did not use published data");session=store.find("rmcsave");file=session->openFile("payload.bin",GENERIC_READ|SYNCHRONIZE,1,1,0x60);
    need(!file->writable()&&read(file)=="original payload","Reopened native save payload differs");file.reset();
    auto directory=session->openDirectory(FILE_LIST_DIRECTORY|SYNCHRONIZE,3,0x4021);
    FILE_ATTRIBUTE_TAG_INFO directoryAttributes{};
    need(!directory->writable()&&directory->disposition()==1&&GetFileInformationByHandleEx(directory->handle(),FileAttributeTagInfo,&directoryAttributes,sizeof(directoryAttributes))&&
        (directoryAttributes.FileAttributes&FILE_ATTRIBUTE_DIRECTORY),"Save search did not own a real read-only directory");
    rejects([&]{store.close("rmcsave");},ERROR_SHARING_VIOLATION);
    rejects([&]{session->openFile("payload.bin",GENERIC_WRITE|SYNCHRONIZE,0,1,0x60);},ERROR_SHARING_VIOLATION);
    rejects([&]{session->openDirectory(GENERIC_WRITE|SYNCHRONIZE,3,0x4021);},ERROR_ACCESS_DENIED);
    rejects([&]{session->openDirectory(FILE_LIST_DIRECTORY|SYNCHRONIZE,3,0x5021);});
    need(read(index)==firstIndex,"Directory query or rejected write changed published save");
    directory.reset();store.close("rmcsave");session.reset();need(read(index)==firstIndex,"Read-only session republished native save data");
    rejects([&]{store.open("rmcsave",info,1);},ERROR_ALREADY_EXISTS);
    need(store.open("rmcsave",info,2)==1,"Native CREATE_ALWAYS did not start replacement generation");session=store.find("rmcsave");file=session->openFile("payload.bin",GENERIC_WRITE|SYNCHRONIZE,0,3,0x60);write(file,"replacement payload");file.reset();
    rejects([&]{store.close("rmcsave");},ERROR_ACCESS_DENIED);need(read(index)==firstIndex,"Failed replacement changed the published previous save");
    snapshot={};store.close("rmcsave");session.reset();need(read(index)!=firstIndex,"Replacement generation was not atomically published");
    need(store.open("rmcsave",info,4)==2,"Native OPEN_ALWAYS ignored existing save");session=store.find("rmcsave");file=session->openFile("payload.bin",GENERIC_READ|SYNCHRONIZE,1,1,0x60);need(read(file)=="replacement payload","New indexed generation payload differs");file.reset();
    // A write to an existing session forks real data first. Until publication,
    // new readers and enumeration retain the previous complete generation.
    const auto replacement=read(index);file=session->openFile("payload.bin",GENERIC_WRITE|SYNCHRONIZE,0,1,0x60);write(file,"UP");file.reset();need(read(index)==replacement,"In-progress file write changed published generation");
    store.close("rmcsave");session.reset();need(store.open("rmcsave",info,3)==2,"Reopen updated native save failed");session=store.find("rmcsave");file=session->openFile("payload.bin",GENERIC_READ|SYNCHRONIZE,1,1,0x60);need(read(file)=="UPplacement payload","Copy-on-write save update lost untouched bytes");file.reset();store.close("rmcsave");session.reset();
    NativeSaveInfo absent=info;absent.name="Absent";rejects([&]{store.open("rmcsave",absent,3);},ERROR_PATH_NOT_FOUND);rejects([&]{store.open("rmcsave",absent,5);},ERROR_PATH_NOT_FOUND);
    auto invalid=info;invalid.name="../outside";rejects([&]{store.open("rmcsave",invalid,1);});invalid=info;invalid.profile="00000000-0000-0000-0000-000000000000";rejects([&]{store.open("rmcsave",invalid,1);});
    need(store.open("rmcsave",info,3)==2,"Negative fixture reopen failed");session=store.find("rmcsave");
    for(const std::string name:{"../outside","C:outside","sub/file","CON","trailing.",""})rejects([&]{session->openFile(name,GENERIC_WRITE|SYNCHRONIZE,0,3,0x60);});
    store.close("rmcsave");session.reset();
    // A changed checksum is corruption, never interpreted as an empty catalog.
    const auto valid=read(index);{std::ofstream f(index,std::ios::binary|std::ios::trunc);f<<"bad\n";}
    rejects([&]{scanNativeSaves(root,info.profile,info.title);});{std::ofstream f(index,std::ios::binary|std::ios::trunc);f<<valid;}
    need(scanNativeSaves(root,info.profile,info.title).records.size()==1,"Corrupt index rejection poisoned a later valid read");
    {NativeSaveStore reopened(root);need(reopened.open("rmcsave",info,3)==2,"New native store instance lost committed save");reopened.close("rmcsave");}
    NativeSaveInfo legacy=info;legacy.name="Legacy";legacy.display=L"Legacy native save";
    const auto legacyRoot=root/"saves"/player.id/title(info.title)/legacy.name;std::filesystem::create_directories(legacyRoot/"data");
    {std::ofstream f(legacyRoot/"metadata.txt",std::ios::binary);f<<"SIMPSONS-NATIVE-SAVE 1\nLegacy native save\n";}
    {std::ofstream f(legacyRoot/"data"/"payload.bin",std::ios::binary);f<<"legacy payload";}
    need(store.open("rmcsave",legacy,3)==2,"Existing v1 native directory save was omitted");session=store.find("rmcsave");
    file=session->openFile("payload.bin",GENERIC_READ|SYNCHRONIZE,1,1,0x60);need(read(file)=="legacy payload","Legacy native save payload differs");file.reset();
    file=session->openFile("payload.bin",GENERIC_WRITE|SYNCHRONIZE,0,1,0x60);write(file,"NEW");file.reset();store.close("rmcsave");session.reset();
    need(read(legacyRoot/"data"/"payload.bin")=="legacy payload","Migration overwrote previous native v1 data");
    need(scanNativeSaves(root,info.profile,info.title).records.size()==2,"Indexed migration duplicated a legacy save entry");
    need(store.open("rmcsave",legacy,3)==2,"Reopen migrated native save failed");session=store.find("rmcsave");file=session->openFile("payload.bin",GENERIC_READ|SYNCHRONIZE,1,1,0x60);
    need(read(file)=="NEWacy payload","Indexed native save did not take precedence after migration");file.reset();store.close("rmcsave");session.reset();
    need(players.load(player.id)==player,"Save operations changed the actual profile record");
    std::printf("PASS native save store:%zu checks; real create/read/update/flush/close, atomic index replacement, failed replacement preserves old data, profile ownership, leases and strict paths\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL native save store:%s\n",e.what());return 1;}}
