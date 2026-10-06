#include "renderer/native_storage_screen.h"
#include "renderer/native_backend.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>

using namespace Simpsons::Graphics;
namespace {
void require(bool condition,const char* reason) {if(!condition)throw Error(reason);}
struct Window {
    HWND handle{};
    explicit Window(unsigned width,unsigned height) {
        WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);
        type.lpszClassName=L"SimpsonsStorageRendererProbe";
        require(RegisterClassW(&type)!=0 || GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"Storage renderer probe class failed");
        handle=CreateWindowExW(WS_EX_TOOLWINDOW,type.lpszClassName,L"Storage renderer probe",WS_POPUP,
            -32000,-32000,int(width),int(height),nullptr,nullptr,type.hInstance,nullptr);
        require(handle!=nullptr,"Storage renderer probe window failed");
        require(!IsWindowVisible(handle),"Storage renderer probe opened a visible window");
    }
    ~Window() {if(handle)DestroyWindow(handle);}
    void resize(unsigned width,unsigned height) {
        require(SetWindowPos(handle,nullptr,0,0,int(width),int(height),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,
            "Storage renderer probe resize failed");
    }
};
size_t pixels(const std::vector<uint8_t>& image,unsigned width,int left,int top,int right,int bottom,
              const std::function<bool(unsigned,unsigned,unsigned)>& matches) {
    size_t count=0;
    for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
        const auto at=(size_t(y)*width+size_t(x))*4;
        if(matches(image[at],image[at+1],image[at+2]))++count;
    }
    return count;
}
auto light=[](unsigned r,unsigned g,unsigned b){return r>180 && g>180 && b>180;};
auto dark=[](unsigned r,unsigned g,unsigned b){return r<20 && g<20 && b<20;};
auto gold=[](unsigned r,unsigned g,unsigned b){return r>200 && g>160 && b<60;};
void bitmap(const std::filesystem::path& path,const std::vector<uint8_t>& rgba,unsigned width,unsigned height) {
    if(path.empty())return;
    BITMAPFILEHEADER file{};file.bfType=0x4D42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
    file.bfSize=file.bfOffBits+DWORD(rgba.size());
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=LONG(width);info.biHeight=-LONG(height);
    info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;info.biSizeImage=DWORD(rgba.size());
    auto bgra=rgba;for(size_t at=0;at<bgra.size();at+=4)std::swap(bgra[at],bgra[at+2]);
    std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(&file),sizeof(file));
    output.write(reinterpret_cast<const char*>(&info),sizeof(info));output.write(reinterpret_cast<const char*>(bgra.data()),std::streamsize(bgra.size()));
    require(bool(output),"Storage renderer inspection bitmap failed");
}
void run(const std::filesystem::path& assets,const std::filesystem::path& capture) {
    Window window(1280,720);StorageScreenBackground background; background.width=1280;background.height=720;
    background.rgba.resize(size_t(1280)*720*4);
    for(size_t at=0;at<background.rgba.size();at+=4) {
        background.rgba[at]=40;background.rgba[at+1]=80;background.rgba[at+2]=170;background.rgba[at+3]=255;
    }
    NativeStorageScreen screen(window.handle,1280,720,std::move(background),assets);
    NativeStorageScreenState state;state.player=L"Player";
    state.folder=L"K:\\The Simpsons Game\\My saved games\\"+std::wstring(200,L'W')+L"\\profiles";
    state.availableBytes=419ull*1024*1024*1024;state.requiredBytes=64*1024;state.selectedRow=0;
    screen.render(state);const auto normal=screen.readbackRGBA();
    require(normal.size()==size_t(1280)*720*4,"Storage renderer readback is not a full RGBA frame");
    require(pixels(normal,1280,350,105,950,160,light)>1000 && pixels(normal,1280,350,105,950,160,dark)>500,
        "Actual retail title font or black outline is absent");
    require(pixels(normal,1280,460,165,820,179,gold)>400,"Storage title lacks its yellow menu underline");
    require(pixels(normal,1280,220,215,1060,254,gold)>1000,"Root/saves location statement is absent");
    require(pixels(normal,1280,220,320,1060,346,light)==0 && pixels(normal,1280,220,382,1060,421,light)==0,
        "Storage screen still exposes a folder label or absolute path");
    require(pixels(normal,1280,350,349,950,383,light)>800,"Actual storage capacity is absent");
    require(normal[(size_t(200)*1280+40)*4]==40 && normal[(size_t(200)*1280+40)*4+1]==80 &&
        normal[(size_t(200)*1280+40)*4+2]==170,"Transparent menu overlay lost the actual supplied background");
    require(screen.hitTest(640,485)==0 && screen.hitTest(640,535)==1 && screen.hitTest(40,485)==-1,
        "Storage row hit testing missed the visible choices or included outside artwork");
    screen.render(state);require(screen.readbackRGBA()==normal,"Unchanged storage render altered its pixels");
    state.folder=L"C:\\Completely different absolute path\\"+std::wstring(500,L'X');
    screen.render(state);require(screen.readbackRGBA()==normal,"Private absolute folder path affects the fixed root/saves screen");
    bitmap(capture,normal,screen.width(),screen.height());
    state.availableBytes=0;state.selectedRow=1;screen.render(state);const auto insufficient=screen.readbackRGBA();
    require(pixels(insufficient,1280,420,467,860,505,light)<pixels(normal,1280,420,467,860,505,light)/3,
        "Insufficient-capacity folder remains styled as an available selection");
    require(pixels(insufficient,1280,400,448,880,470,gold)>150,"Insufficient capacity has no readable reason");
    require(pixels(insufficient,1280,390,522,890,558,light)>1000,"Continue without saving cannot be distinguished as selected");
    window.resize(2560,1080);screen.resize(2560,1080);screen.render(state);const auto wide=screen.readbackRGBA();
    require(screen.width()==2560 && screen.height()==1080 && wide.size()==size_t(2560)*1080*4,
        "Ultrawide resize did not rebuild the owned renderer target and overlay");
    require(screen.hitTest(1280,728)==0 && screen.hitTest(1280,803)==1 && screen.hitTest(200,728)==-1,
        "Ultrawide row input ignored the centered 16:9 menu or accepted a side margin");
    require(wide[(size_t(300)*2560+40)*4]==0 && wide[(size_t(300)*2560+40)*4+1]==0 &&
        wide[(size_t(300)*2560+400)*4]==40 && wide[(size_t(300)*2560+400)*4+1]==80,
        "Ultrawide renderer stretched the captured 16:9 frame or lost its black side margins");
    std::printf("PASS Native Save Storage renderer: retail fonts/outline, root/saves statement, hidden absolute path, capacity states, background, readback, ultrawide resize/input\n");
}
}
int main(int argc,char** argv) {
    try {require(argc>=2,"Usage: NativeStorageScreenTests <native-assets/storage-screen> [inspection.bmp]");
        run(std::filesystem::path(argv[1]),argc>=3?std::filesystem::path(argv[2]):std::filesystem::path{});return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL Native Save Storage renderer: %s\n",error.what());return 1;}
}
