#include "effect_catalog_lifecycle_helpers.h"
#include "runtime/engine_builtin_textures.h"
#include "runtime/engine_itxd_textures.h"
#include "runtime/engine_reflection_textures.h"
#include <fstream>
#include <set>
#include <thread>

namespace {
template<class F>void rejected(F&& f,const char* why){
    try{f();}catch(const Failure& e){need(std::string(e.what()).find(why)!=std::string::npos,"Built-in rejection cause differs");return;}
    need(false,"Unsupported built-in operation accepted");
}
void copiedMaterialTexture(Runtime& rt,const PPCContext& entry,const char* path) {
    // Optional stock dictionary coverage runs its genuine named memory-stream
    // loader, copy/relocation/index publication and original final group free.
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& textures=driver.itxdTextures();
    constexpr uint32_t area=0x60000,dictionary=0x80000;
    std::ifstream input(path,std::ios::binary|std::ios::ate);need(bool(input),"Original material dictionary required");
    const auto length=input.tellg();need(length>0&&length<=0x200000,"Material dictionary extent differs");
    const auto bytes=uint32_t(length);rt.map(area,0x1000,true,"material resolver test envelope");
    rt.map(dictionary,bytes,true,"original material resolver dictionary");
    input.seekg(0);input.read(reinterpret_cast<char*>(rt.pointer(dictionary,bytes,true)),bytes);
    need(bool(input),"Original material dictionary read failed");const auto original=snapshot(rt,dictionary,bytes);
    PPC_STORE_U32(area,dictionary);PPC_STORE_U32(area+4,bytes);
    EngineCpuCalls cpu(entry,base);const auto stream=cpu.invoke(0x823F9598,3,1,area);
    need(stream,"Original material memory stream creation failed");
    const auto before=textures.count();uint32_t group=0;
    {
        // End the nested call frame before invoking the outer CPU owner again.
        // EngineCpuCalls publishes exactly its own context until destruction.
        EngineCpuCalls load(entry,base);load.registers().lr=0x8271191C;
        const auto request=load.registers().r1.u32+0x60;std::memset(rt.pointer(request,0x18,true),0,0x18);
        PPC_STORE_U32(area+0x10,0x54455852);PPC_STORE_U32(request+4,area+0x10);
        PPC_STORE_U32(request+0x10,stream);PPC_STORE_U32(request+0x14,bytes);
        const auto saved=abi(load.registers());group=load.invoke(0x826F26B0,0,request);
        need(group&&abi(load.registers())==saved,"Original material dictionary parent/ABI differs");
    }
    const auto plugin=PPC_LOAD_U32(0x82CF0600);uint32_t header=0;
    for(auto node=PPC_LOAD_U32(group+4);node;node=PPC_LOAD_U32(node+4)) {
        rt.pointer(node,8,false);const auto texture=PPC_LOAD_U32(node);
        if(stringAt(rt,texture+0x10)=="simpsons_palette") {header=texture+0xCC;break;}
    }
    need(header&&textures.rasterByHeader(base,header)==header-0x54,"Published stock palette header owner differs");
    const auto texture=textures.textureFromHeader(base,header);
    need(driver.materialTexture(base,header)==texture&&texture->width==64&&texture->height==64,
         "Material resolver did not preserve the published original ITXD backing");
    const auto copied=header-0xCC;need(PPC_LOAD_U32(copied+plugin+0xC)==group,"Original material texture group differs");
    rejected([&]{driver.materialTexture(base,header+4);},"unknown or stale copied texture");
    rejected([&]{driver.materialTexture(base,copied);},"unknown or stale copied texture");
    same(rt,dictionary,original,"Material resolver changed the original dictionary");
    cpu.invoke(0x826F8168,group);
    need(textures.count()==before&&!textures.rasterByHeader(base,header),"Original material group release retained owner");
    rejected([&]{driver.materialTexture(base,header);},"unknown or stale copied texture");
    need(texture&&texture->width==64,"Original header retirement invalidated independently retained native backing");
    same(rt,dictionary,original,"Original material group release changed source bytes");
}
void run(Runtime& rt,const PPCContext& entry,std::array<std::weak_ptr<Graphics::Texture>,3>& weak,const char* dictionaryPath){
    auto* base=rt.base;auto& d=*rt.engineDriver;auto& service=d.builtinTextures();const auto context=PPC_LOAD_U32(0x82D5DA74);
    need(!service.count()&&!service.complete(),"Built-in ownership starts populated");service.requireReleased();
    {
        EngineCpuCalls cpu(entry,base);const auto opts=cpu.registers().r1.u32+0x60;
        PPC_STORE_U32(opts,2);PPC_STORE_U32(opts+4,16);PPC_STORE_U32(opts+8,0);
        const auto manager=cpu.invoke(0x8269BF70,0x260,opts);
        need(manager&&cpu.invoke(0x826B6F60,manager,context)==manager,"Original effect manager setup failed");
        need(!cpu.invoke(0x827019E8,table,25)&&!cpu.invoke(0x827019E8,0x82CD1448,24),"Original effects registration failed");cpu.invoke(0x826B7218,manager);
    }
    uint32_t observed=0;bool probing=false;PPCContext caller{};
    const std::array<uint32_t,3> outputs={0x82D63004,0x82D6300C,0x82D63008};
    rt.builtinImageBoundaryObserver=[&](uint32_t pc,PPCContext& c,uint8_t*){
        if(probing)return;
        need(pc==0x82B84838&&observed<3&&service.count()==observed&&c.r6.u32==outputs[observed],"Original image sequence differs");
        need(!service.complete(),"Original parent committed aliases before completion");
        if(observed) {
            const auto pending=PPC_LOAD_U32(outputs[0]);need(pending&&service.owns(pending),"Partial original image owner missing");
            rejected([&]{d.materialTexture(base,pending);},"not published");
            need(service.count()==observed&&!service.complete(),"Rejected unpublished material image changed lifecycle");
        }
        if(!observed){
            caller=c;const auto globals=snapshot(rt,0x82D63004,24),attachments=snapshot(rt,0x82D0CF58,20);
            probing=true;
            for(uint32_t mutation=0;mutation<6;++mutation){
                const auto saved=c;
                switch(mutation){case 0:c.lr+=4;break;case 1:c.r3.u32=0;break;case 2:c.r4.u32+=4;break;
                    case 3:--c.r5.u32;break;case 4:c.r6.u32=0x82D63010;break;case 5:c.r28.u32=0;break;}
                rejected([&]{SimpsonsNativeBuiltinImageLoad(c,base);},"unsupported loader caller/source/output profile");c=saved;
                need(!service.count(),"Rejected loading created an owner");same(rt,0x82D63004,globals,"Rejected loading changed globals");same(rt,0x82D0CF58,attachments,"Rejected loading changed attachments");
            }
            probing=false;
        }
        ++observed;
    };
    {
        EngineCpuCalls cpu(entry,base);const auto before=abi(cpu.registers());
        need(!cpu.invoke(0x826FF0F8,context,0),"Original complete image parent failed");need(abi(cpu.registers())==before,"Original image parent ABI changed");
    }
    rt.builtinImageBoundaryObserver={};need(observed==3&&service.count()==3&&service.complete(),"Original image bundle incomplete");
    const auto reflection=PPC_LOAD_U32(0x82D6301C);need(d.reflectionTextures().view(reflection).phase==EngineReflectionTextures::Phase::Ready,"Original reflection owner missing");
    const auto globals=snapshot(rt,0x82D63004,24);std::set<uint32_t> ids;
    for(uint32_t i=0;i<3;++i){
        const uint32_t id=PPC_LOAD_U32(outputs[i]),n=i==2?16:32,mips=i==2?5:6;const auto v=service.view(id);const auto texture=service.texture(id);weak[i]=texture;
        need(ids.insert(id).second&&service.owns(id)&&!rt.pageAccess[id>>12].load(),"Built-in identity aliases/addressable/unowned");
        need(d.materialTexture(base,id)==texture,"Material resolver did not return exact original builtin backing");
        need(v.output==outputs[i]&&v.size==n&&v.levels==mips&&texture->width==n&&texture->height==n&&texture->levelCount()==mips&&
            texture->format==(i==2?Graphics::TextureFormat::BGRX8:Graphics::TextureFormat::RGBA8),"Built-in native profile differs");
        for(uint32_t level=0;level<mips;++level){
            const auto pixels=service.readback(id,level);need(pixels.size()==size_t(n>>level)*(n>>level)*4,"Built-in native mip size differs");
            for(size_t j=0;j<pixels.size();j+=4){
                const uint8_t rgb=i==0?255:(i==1?0:128),alpha=i==2&&level?0:255;
                need(pixels[j]==rgb&&pixels[j+1]==rgb&&pixels[j+2]==rgb&&pixels[j+3]==alpha,"Built-in original filtered/native texel differs");
            }
        }
        bool badLevel=false;try{service.readback(id,mips);}catch(const Graphics::Error&){badLevel=true;}need(badLevel,"Nonexistent native mip accepted");
        rejected([&]{d.materialTexture(base,id+0xCC);},"unknown or stale copied texture");
        rejected([&]{d.materialTexture(base,outputs[i]);},"unknown or stale copied texture");
        auto* previous=currentContext;currentContext=&caller;caller.r3.u32=id;
        rejected([&]{SimpsonsNativeGraphicsResourceRelease(caller,base);},"Unqualified original built-in image release caller");currentContext=previous;
    }
    need(PPC_LOAD_U32(0x82D63010)==PPC_LOAD_U32(outputs[1])&&PPC_LOAD_U32(0x82D63018)==PPC_LOAD_U32(outputs[1])&&PPC_LOAD_U32(0x82D63014)==PPC_LOAD_U32(outputs[0]),"Original borrowed aliases differ");
    for(uint32_t global:{0x82D63010u,0x82D63014u,0x82D63018u})
        need(d.materialTexture(base,PPC_LOAD_U32(global))==service.texture(PPC_LOAD_U32(global)),
             "Borrowed builtin alias resolved a different material backing");
    rejected([&]{d.materialTexture(base,0);},"unknown copied texture header");
    rejected([&]{d.materialTexture(base,0x00F0FFFF);},"unknown or stale copied texture");
    for(const auto id:d.reflectionTextures().view(reflection).identities)
        rejected([&]{d.materialTexture(base,id);},"unknown or stale copied texture");
    {
        EngineCpuCalls cpu(entry,base);cpu.invoke(0x826FF248); // Real original global teardown is a no-op.
        need(service.count()==3&&service.complete(),"No-op original global cleanup released images");same(rt,0x82D63004,globals,"No-op cleanup changed globals");
    }
    rejected([&]{service.requireReleased();},"retain native resources");
    auto* previous=currentContext;currentContext=&caller;
    rejected([&]{SimpsonsNativeBuiltinImageLoad(caller,base);},"duplicate image bundle");currentContext=previous;
    for(uint32_t global:{0x82D63004u,0x82D6300Cu,0x82D63008u,0x82D63010u,0x82D63014u,0x82D63018u}){
        const auto saved=PPC_LOAD_U32(global);PPC_STORE_U32(global,0);
        rejected([&]{service.view(PPC_LOAD_U32(outputs[global==outputs[0]?1:0]));},"publication differs");PPC_STORE_U32(global,saved);
    }
    for(uint32_t global:{0x82D63004u,0x82D6300Cu,0x82D63008u,0x82D63010u,0x82D63014u,0x82D63018u}) {
        const auto saved=PPC_LOAD_U32(global);PPC_STORE_U32(global,0);
        rejected([&]{d.materialTexture(base,*ids.begin());},"publication differs");PPC_STORE_U32(global,saved);
    }
    bool foreign=false,foreignMaterial=false;std::thread t([&]{try{service.count();}catch(const Failure&){foreign=true;}
        try{d.materialTexture(base,*ids.begin());}catch(const Failure&){foreignMaterial=true;}});t.join();
    need(foreign&&foreignMaterial,"Foreign thread accepted built-in/material ownership");
    rejected([&]{service.view(0xDEAD);},"unknown or stale identity");same(rt,0x82D63004,globals,"Invalid access changed publications");
    for(const auto& w:weak)need(!w.expired(),"Live image backing expired before terminal cleanup");
    if(dictionaryPath)copiedMaterialTexture(rt,entry,dictionaryPath);
}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(exceptionFilter);
    try{
        need(argc==2||argc==3,"Original image and optional stock ITXD dictionary required");std::array<std::weak_ptr<Graphics::Texture>,3> weak;
        {
            Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
            bool observed=false;rt.graphicsStartupObserver=observeGraphics;try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
            rt.graphicsStartupObserver={};need(observed,"Original pre-FX startup boundary absent");run(rt,entry,weak,argc==3?argv[2]:nullptr);
        }
        for(const auto& w:weak)need(w.expired(),"Terminal runtime cleanup retained native image backing");
        std::printf("PASS original built-in texture lifecycle:%zu checks; real parent/three sources/17 native mip readbacks/material resolver/borrowed aliases/no-op global cleanup/rejections/terminal ownership%s;ALL MUTED\n",
            checks,argc==3?"/stock ITXD publication and original group retirement":"");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL original built-in lifecycle:%zu checks %s\n",checks,e.what());return 1;}
}
