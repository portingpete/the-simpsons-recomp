#include "renderer/native_texture_stream.h"
#include <fstream>
#include <cstdio>
#include <iterator>

using namespace Simpsons::Graphics;
void require(bool value,const char* reason) {if(!value) throw Error(reason);}
template<class F> void rejects(F action) {
    try {action();} catch(const Error&) {return;}
    throw Error("Malformed original texture struct was accepted");
}
int main(int argc,char** argv) {
    try {
        if(argc!=2) throw Error("Supply the verified derived original image");
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<uint8_t> image((std::istreambuf_iterator<char>(input)),{});
        require(image.size()==15466496,"Wrong original image extent");
        NativeBackend backend(true);
        const uint32_t offsets[]={0x15F854,0x16F8E8};
        const char* names[]={"frame2","frame1"};
        for(unsigned index=0;index<2;++index) {
            auto source=std::span<const uint8_t>(image).subspan(offsets[index],0x1005C);
            auto decoded=decodeNativeTextureStruct(source);
            require(decoded.name==names[index] && decoded.maskName.empty(),"Original texture name mismatch");
            require(decoded.width==256 && decoded.height==256 && decoded.sampler==0x1102,"Original loading metadata mismatch");
            require(decoded.blocks.size()==65536 && decoded.format==TextureFormat::BC3,"Original loading texture storage mismatch");
            for(size_t i=0;i<decoded.blocks.size();++i)
                require(decoded.blocks[i]==source[92+(i^1)],"Compressed block endian conversion mismatch");
            auto texture=backend.createTexture(decoded.width,decoded.height,decoded.format,decoded.blocks);
            require(backend.readback(texture)==decoded.blocks,"Original loading blocks changed during native GPU upload");
            std::vector<uint8_t> changed(source.begin(),source.end());
            for(size_t field:{size_t(0),size_t(76),size_t(85),size_t(87),size_t(88),size_t(90)}) {
                changed[field]^=1;rejects([&]{decodeNativeTextureStruct(changed);});changed[field]^=1;
            }
            rejects([&]{decodeNativeTextureStruct(source.first(91));});
            rejects([&]{decodeNativeTextureStruct(source.first(source.size()-1));});
            changed.push_back(0);rejects([&]{decodeNativeTextureStruct(changed);});changed.pop_back();
            for(size_t i=8;i<40;++i) changed[i]='x';rejects([&]{decodeNativeTextureStruct(changed);});
        }
        require(backend.presentationCount()==0 && backend.screenDrawCount()==0,"Texture upload counted as a frame");
        puts("Both original loading texture structs decoded and uploaded to native BC3 resources; no game frames rendered.");
        return 0;
    } catch(const std::exception& e) {fprintf(stderr,"Native texture stream failure: %s\n",e.what());return 1;}
}
