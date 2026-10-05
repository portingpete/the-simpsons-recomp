#include "effect_catalog_lifecycle_helpers.h"
#include <set>
#include <thread>

namespace {
void originalCpu(Runtime& rt,const PPCContext& entry){
    auto* base=rt.base;rt.map(0x50000,0x1000,true,"crossfade CPU qualification");EngineCpuCalls cpu(entry,base);
    constexpr uint32_t r=0x50010,header=0x50110,out=0x50200;
    for(uint32_t poison:{0u,0xCDu,0xFFu,0x5Au}){
        std::memset(rt.pointer(r-16,0x74,true),int(poison),0x74);auto expected=snapshot(rt,r-16,0x74);
        auto word=[&](uint32_t at,uint32_t v){for(uint32_t i=0;i<4;++i)expected[16+at+i]=uint8_t(v>>(24-8*i));};
        word(0x14,32);expected[16+0x20]=0;expected[16+0x21]=0x80;expected[16+0x23]=5;
        const auto before=abi(cpu.registers());need(cpu.invoke(0x823F6E68,r,0x580)==1,"Original crossfade normalization failed");
        need(abi(cpu.registers())==before,"Original normalization ABI changed");same(rt,r-16,expected,"Original normalization wrote a different field set");
        std::memset(rt.pointer(header-16,0x54,true),0xA7,0x54);std::memset(rt.pointer(header,0x34,true),int(poison),0x34);
        auto& c=cpu.registers();c.r3.u64=3;c.r4.u64=1280;c.r5.u64=720;c.r6.u64=1;c.r7.u64=1;c.r8.u64=0;c.r9.u64=0x18280186;c.r10.u64=2;
        PPC_STORE_U32(c.r1.u32+0x54,0);PPC_STORE_U32(c.r1.u32+0x5C,0);PPC_STORE_U32(c.r1.u32+0x64,0);
        PPC_STORE_U32(c.r1.u32+0x6C,header);PPC_STORE_U32(c.r1.u32+0x74,out);PPC_STORE_U32(c.r1.u32+0x7C,out+4);cpu.invoke(0x8243F928);
        need(abi(c)==before,"Original header builder ABI changed");
        const std::array<uint32_t,13> words={0x00100003,1,0,0,0,0xFFFF0000,0xFFFF0000,0x8A000002,0x86,0x0059E4FF,0xC14,0,0x200};
        // Original8243FA94..FC1C inserts fields into existing fetch words.
        // Preserve those exact uncovered bits; they are not implied zeroes.
        const std::array<uint32_t,13> preserved={0,0,0,0xFFFFFFFF,0xFFFFFFFF,0,0,0x003FFC00,0xFFFFFB00,0,0x7FF80000,0xFFFFFC3F,0xFFFFF1FF};
        std::printf("Crossfade CPU header poison=%02X:",poison);for(uint32_t i=0;i<words.size();++i)std::printf(" %08X",PPC_LOAD_U32(header+4*i));std::puts("");
        for(uint32_t i=0;i<words.size();++i)need(PPC_LOAD_U32(header+4*i)==(words[i]|(poison*0x01010101&preserved[i])),"Original crossfade texture header differs");
        need(PPC_LOAD_U32(out)==0x398000&&!PPC_LOAD_U32(out+4),"Original crossfade allocation layout differs");
        for(uint32_t i=0;i<16;++i)need(*rt.pointer(header-16+i,1,false)==0xA7&&*rt.pointer(header+0x34+i,1,false)==0xA7,"CPU header escaped bounds");
    }
}
void lifecycle(Runtime& rt,const PPCContext& entry){
    auto* base=rt.base;auto& d=*rt.engineDriver;const auto baseline=d.rasterCount();
    const auto messages=snapshot(rt,0x82D6C064,16),list=snapshot(rt,0x82D0D01C,4);
    std::set<uint32_t> identities;
    for(uint32_t poison:{0u,0xCDu,0xFFu,0x5Au}){
        uint32_t owner{},r{},id{};std::weak_ptr<Graphics::Texture> weak;
        {
            EngineCpuCalls cpu(entry,base);owner=cpu.invoke(0x8269BD70,24);need(owner!=0,"Original crossfade allocation failed");std::memset(rt.pointer(owner,24,true),int(poison),24);
            const auto before=abi(cpu.registers());need(cpu.invoke(0x82702028,owner)==owner,"Original crossfade constructor failed");need(abi(cpu.registers())==before,"Original crossfade constructor ABI changed");
        }
        need(PPC_LOAD_U32(0x82D09850)==owner&&PPC_LOAD_U32(owner)==0x8214E49C&&PPC_LOAD_U32(owner+4)==1&&PPC_LOAD_U32(owner+8)==0x8214E498&&
            !PPC_LOAD_U32(owner+0x10)&&!PPC_LOAD_U8(owner+0x14)&&!PPC_LOAD_U8(owner+0x15)&&!PPC_LOAD_U8(owner+0x16)&&PPC_LOAD_U8(owner+0x17)==poison,"Original crossfade parent fields/preserved byte differ");
        r=PPC_LOAD_U32(owner+0xC);const auto x=r+PPC_LOAD_U32(0x82E3DC94);id=PPC_LOAD_U32(x);
        need(d.rasterCount()==baseline+1&&identities.insert(id).second&&!rt.pageAccess[id>>12].load(),"Native crossfade raster count/identity differs");
        need(PPC_LOAD_U32(r)==r&&PPC_LOAD_U32(r+0xC)==1280&&PPC_LOAD_U32(r+0x10)==720&&PPC_LOAD_U32(r+0x14)==32&&!PPC_LOAD_U32(r+0x20)&&
            PPC_LOAD_U32(x+0x18)==0x18280186&&PPC_LOAD_U32(x+8)==0xFF&&!PPC_LOAD_U32(x+4)&&!PPC_LOAD_U32(x+0xC),"Original completed snapshot metadata differs");
        same(rt,0x82D0D01C,list,"Crossfade created a reset-list node absent from its original path");
        {auto texture=d.textureRaster(r);weak=texture;need(texture->width==1280&&texture->height==720&&texture->levelCount()==1&&texture->format==Graphics::TextureFormat::RGBA8,"Native snapshot storage profile differs");}
        const auto saved=snapshot(rt,r,PPC_LOAD_U32(0x82CD1E28)),ownerSaved=snapshot(rt,owner,24);
        for(const auto field:std::array<std::array<uint32_t,2>,5>{{{r+0xC,256},{r+0x20,0x00800005},{x,0},{x+0x18,0x28280186},{owner+0xC,0}}}){
            const auto old=PPC_LOAD_U32(field[0]);PPC_STORE_U32(field[0],field[1]);rejects([&]{d.textureRaster(r);},"Changed snapshot metadata accepted");PPC_STORE_U32(field[0],old);
        }
        bool foreign=false;std::thread t([&]{try{d.textureRaster(r);}catch(const Failure&){foreign=true;}});t.join();need(foreign,"Foreign thread accepted snapshot ownership");
        {EngineCpuCalls cpu(entry,base);rejects([&]{d.beginSnapshotRaster(cpu.registers(),base);},"Duplicate live snapshot accepted");rejects([&]{d.createSnapshotTexture(cpu.registers(),base);},"Factory accepted without original helper");}
        const auto stage0=PPC_LOAD_U32(0x82D0E3F8);PPC_STORE_U32(0x82D0E3F8,r);
        rejects([&]{d.preflightRasterDestroy(base,r);},"Bound snapshot destruction accepted");PPC_STORE_U32(0x82D0E3F8,stage0);
        same(rt,r,saved,"Rejected snapshot operations changed raster fields");same(rt,owner,ownerSaved,"Rejected operations changed parent fields");
        need(!weak.expired(),"Snapshot backing expired before its paired destructor");
        {
            EngineCpuCalls cpu(entry,base);const auto before=abi(cpu.registers());need(cpu.invoke(0x82702348,owner,1)==owner,"Original deleting crossfade destructor failed");need(abi(cpu.registers())==before,"Original crossfade destructor ABI changed");
        }
        need(!PPC_LOAD_U32(0x82D09850)&&d.rasterCount()==baseline&&weak.expired(),"Paired crossfade destruction retained native backing/singleton");
        same(rt,0x82D6C064,messages,"Original paired descriptor release did not restore baseline");same(rt,0x82D0D01C,list,"Paired snapshot cleanup changed unrelated reset list");
        rejects([&]{d.textureRaster(r);},"Retired snapshot raster accepted");
    }
}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(exceptionFilter);
    try{
        need(argc==2,"Original image required");Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original pre-FX startup boundary absent");originalCpu(rt,entry);lifecycle(rt,entry);
        std::printf("PASS original crossfade snapshot:%zu checks; four original constructor/deleting-destructor lifetimes,CPU field preservation/header layout,owned native storage,stale/bound/foreign rejection; no initial pixel/capture/composition claim;ALL MUTED\n",checks);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL crossfade:%zu checks %s\n",checks,e.what());return 1;}
}
