#include "renderer/native_backend.h"
#include <cstdio>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Simpsons::Graphics;
void require(bool value,const char* reason) {if(!value) throw Error(reason);}
template<class F> void rejects(F action) {
    try {action();} catch(const Error&) {return;}
    throw Error("Invalid graphics operation did not fail");
}
#include "header/test_engine_binding_reset.h"
#include "header/test_im2d_buffer_upload.h"
int main(int argc,char** argv) {
    try {
        bool hardware=argc==2 && std::string(argv[1])=="--hardware";
        NativeBackend backend(!hardware);
        std::vector<uint8_t> pixels(19*7*4);
        for(size_t i=0;i<pixels.size();++i) pixels[i]=uint8_t((i*37+i/19)%256);
        auto texture=backend.createTexture(19,7,TextureFormat::RGBA8,pixels);
        auto expected=pixels;
        std::fill(pixels.begin(),pixels.end(),0); // GPU resource must own the upload.
        auto retained=texture;texture.reset();
        require(backend.readback(retained)==expected,"RGBA native GPU roundtrip, row pitch or ownership mismatch");
        for(auto format:{TextureFormat::BC1,TextureFormat::BC2,TextureFormat::BC3}) {
            std::vector<uint8_t> blocks(3*2*(format==TextureFormat::BC1?8:16));
            for(size_t i=0;i<blocks.size();++i) blocks[i]=uint8_t(i*41+3);
            auto compressed=backend.createTexture(12,8,format,blocks);
            require(backend.readback(compressed)==blocks,"Native compressed block upload/readback mismatch");
        }
        rejects([&]{backend.createTexture(19,7,TextureFormat::RGBA8,std::span<const uint8_t>(pixels.data(),1));});
        rejects([&]{backend.createTexture(3,4,TextureFormat::BC3,{});});
        rejects([&]{backend.present();});
        std::atomic<bool> wrongThreadRejected=false;
        std::thread other([&]{try {backend.readback(retained);} catch(const Error&) {wrongThreadRejected=true;}});
        other.join();require(wrongThreadRejected,"Immediate context accepted a different owner thread");
        NativeBackend second(!hardware);
        im2dBufferUploadContracts(backend,second);
        rejects([&]{second.readback(retained);});
        // Exact startup allocation sizes and partial update preservation.
        for(auto [size,kind]:{std::pair{0x4E20u,BufferKind::Index16},
                            {0x1FFFEu,BufferKind::Index16},{0x40000u,BufferKind::Vertex}}) {
            auto buffer=backend.createBuffer(size,kind);
            std::vector<uint8_t> original(size);
            for(size_t i=0;i<original.size();++i) original[i]=uint8_t(i*17+i/13);
            backend.writeBuffer(buffer,0,original);
            require(backend.readbackBuffer(buffer)==original,"Native buffer initial upload mismatch");
            std::vector<uint8_t> patch(117);for(size_t i=0;i<patch.size();++i) patch[i]=uint8_t(i*23);
            std::copy(patch.begin(),patch.end(),original.begin()+128);
            backend.writeBuffer(buffer,128,patch);std::fill(patch.begin(),patch.end(),0);
            require(backend.readbackBuffer(buffer)==original,"Native partial buffer upload lost data or ownership");
            rejects([&]{backend.writeBuffer(buffer,size-116,patch);});
            rejects([&]{backend.writeBuffer(buffer,0xFFFFFFFF,patch);});
            rejects([&]{second.writeBuffer(buffer,0,patch);});
            rejects([&]{second.readbackBuffer(buffer);});
            require(backend.readbackBuffer(buffer)==original,"Rejected upload mutated native buffer");
            backend.writeBuffer(buffer,size,{});
        }
        rejects([&]{backend.createBuffer(0,BufferKind::Vertex);});
        // Interleave overlapping partial updates across buffers and several
        // upload-ring generations before any readback. Mutating each caller's
        // bytes after submission must not affect earlier queued copies.
        std::array<std::shared_ptr<Buffer>,4> queued;
        std::array<std::vector<uint8_t>,4> queuedExpected;
        for(size_t i=0;i<queued.size();++i) {
            queued[i]=backend.createBuffer(0x40000,i&1?BufferKind::Index16:BufferKind::Vertex);
            queuedExpected[i].assign(0x40000,uint8_t(i*37));
            backend.writeBuffer(queued[i],0,queuedExpected[i]);
        }
        for(UINT step=0;step<500;++step) {
            const UINT slot=step%4,length=8193+(step*131)%16384;
            const UINT offset=(step*12289)%(0x40000-length+1);
            std::vector<uint8_t> patch(length);
            for(UINT i=0;i<length;++i)patch[i]=uint8_t(step*17+i*23);
            std::copy(patch.begin(),patch.end(),queuedExpected[slot].begin()+offset);
            backend.writeBuffer(queued[slot],offset,patch);std::fill(patch.begin(),patch.end(),0);
        }
        for(size_t i=0;i<queued.size();++i)
            require(backend.readbackBuffer(queued[i])==queuedExpected[i],"Queued buffer copies lost ordering, partial bytes or source lifetime across ring reuse");
        // Oversized uploads retain the bounded direct upload path.
        auto large=backend.createBuffer(1024*1024+17,BufferKind::Vertex);
        std::vector<uint8_t> largeBytes(large->byteSize(),0xB7);
        backend.writeBuffer(large,0,largeBytes);
        require(backend.readbackBuffer(large)==largeBytes,"Oversized direct buffer upload changed data");
        rejects([&]{backend.createBuffer(3,BufferKind::Index16);});
        auto depth=backend.createDepthTarget(19,7);
        for(float value:{0.0f,std::ldexp(1.0f,-34),std::ldexp(1.0f,-14),0.5f,1.0f}) {
            backend.clearDepthTarget(depth,value,0xA7);
            auto data=backend.readbackDepthTarget(depth);
            require(data.size()==19*7*8,"Native depth/stencil readback extent mismatch");
            for(size_t i=0;i<data.size();i+=8) {
                float read;memcpy(&read,data.data()+i,4);
                require(read==value && data[i+4]==0xA7,"Native exact depth/stencil clear mismatch");
            }
        }
        rejects([&]{backend.clearDepthTarget(depth,0.1f,0);});
        rejects([&]{backend.clearDepthTarget(depth,std::nextafter(1.0f,0.0f),0);});
        rejects([&]{backend.clearDepthTarget(depth,std::ldexp(1.0f,-35),0);});
        rejects([&]{second.clearDepthTarget(depth,1,0);});
        rejects([&]{second.readbackDepthTarget(depth);});
        backend.clearDepthTarget(depth,1.0f,0x5A);
        backend.clearDepthTarget(depth,0.0f,0xA5,true,false);
        auto partialDepth=backend.readbackDepthTarget(depth);
        for(size_t i=0;i<partialDepth.size();i+=8) {
            float read;memcpy(&read,partialDepth.data()+i,4);
            require(read==0 && partialDepth[i+4]==0x5A,"Depth-only clear changed stencil or failed to clear depth");
        }
        backend.clearDepthTarget(depth,1.0f,0xA5,false,true);
        partialDepth=backend.readbackDepthTarget(depth);
        for(size_t i=0;i<partialDepth.size();i+=8) {
            float read;memcpy(&read,partialDepth.data()+i,4);
            require(read==0 && partialDepth[i+4]==0xA5,"Stencil-only clear changed depth or failed to clear stencil");
        }
        rejects([&]{backend.clearDepthTarget(depth,1.0f,0,false,false);});
        auto color10=backend.createTarget(19,7,TargetFormat::RGB10A2);
        backend.clearTarget(color10,{1,0,0,1});auto data=backend.readbackTarget(color10);
        for(size_t i=0;i<data.size();i+=4) {
            uint32_t word;memcpy(&word,data.data()+i,4);
            require(word==0xC00003FF,"Native 10:10:10:2 target component packing mismatch");
        }
        backend.bindTargets({color10,nullptr,nullptr,nullptr},depth);
        auto smallerTarget=backend.createTarget(2,2,TargetFormat::RGBA8);
        auto foreign=second.createTarget(19,7,TargetFormat::RGB10A2);
        rejects([&]{backend.bindTargets({color10,color10,nullptr,nullptr},depth);});
        rejects([&]{backend.bindTargets({smallerTarget,nullptr,nullptr,nullptr},depth);});
        rejects([&]{backend.bindTargets({foreign,nullptr,nullptr,nullptr},depth);});
        rejects([&]{second.bindTargets({color10,nullptr,nullptr,nullptr},depth);});
        rejects([&]{backend.bindTargets({std::make_shared<RenderTarget>(),nullptr,nullptr,nullptr},depth);});
        backend.bindTargets({color10,nullptr,nullptr,nullptr},depth);
        auto alternate=backend.createTarget(19,7,TargetFormat::RGBA16Float);
        backend.bindTargets({color10,alternate,nullptr,nullptr},depth);
        backend.bindTargets({},depth); // Depth-only selection is a real native state.
        backend.clearBindings();backend.bindTargets({},nullptr);
        engineBindingResetContracts(hardware);
        require(backend.presentationCount()==0,"Headless checks were miscounted as presentations");
        puts("Native D3D11 resource contract passed; no original game frames rendered.");
        return 0;
    } catch(const std::exception& e) {fprintf(stderr,"Graphics contract failure: %s\n",e.what());return 1;}
}
