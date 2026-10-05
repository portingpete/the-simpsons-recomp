#include "runtime/engine_materials.h"
#include "header/native_translated_materials.h"
#include "runtime/engine_resources.h"
#include "runtime/engine_cpu_calls.h"
#include "renderer/native_backend.h"
#include <cstdio>
#include <functional>

namespace {
size_t checks{};
void require(bool ok,const char* message) {++checks;if(!ok) throw Simpsons::Failure(message);}
void rejects(const std::function<void()>& operation) {
    ++checks;try {operation();} catch(const std::exception&) {return;}
    throw Simpsons::Failure("Expected native resource operation to fail");
}
}
int main(int argc,char** argv) {
    using namespace Simpsons;
    using namespace Simpsons::Graphics;
    try {
        if(argc!=2) throw Failure("Original flat image path required");
        Runtime runtime;runtime.load(argv[1]);runtime.map(0x10000,0x10000,true,"resource bridge stack/output");
        runtime.map(0x30000,0x1000,false,"read-only resource output");
        auto* base=runtime.base;
        PPCContext entry{};entry.r1.u32=0x20000;
        EngineCpuCalls cpu(entry,base);
        NativeBackend backend(true);
        for(uint32_t guarded:{0x82445278u,0x82445578u,0x82445798u}) {
            PPC_STORE_U32(0x12E24,0xAABBCCDD);
            bool rejected=false;
            try {cpu.invoke(guarded,0x10000,0x00E00001);}
            catch(const Failure& error) {
                rejected=std::string(error.what()).starts_with("Unimplemented native engine material/declaration binding; console SDK call from 0x");
            }
            require(rejected,"Console material binding did not fail at its explicit native guard");
            require(cpu.registers().lastFunction==guarded,"Console material binding executed beyond its guarded entry");
            require(PPC_LOAD_U32(0x12E24)==0xAABBCCDD,"Guarded console bind changed a device field");
        }
        uint32_t oldMaterial{};
        {
            EngineMaterials materials(backend);
            for(const auto& original:originalMaterialIdentities()) {
                const bool vertex=original.stage==MaterialStage::Vertex;
                const uint32_t create=vertex?0x823EFAA8:0x823EFB78,release=vertex?0x823EFB10:0x823EFBD0;
                PPC_STORE_U32(0x10000,0x11223344);
                require(cpu.invoke(create,original.originalAddress,0x10000)==1,"Original material create ABI failed");
                const uint32_t id=PPC_LOAD_U32(0x10000);oldMaterial=id;
                require(materials.capability(id)==MaterialCapability::Uncompiled,"Native creation falsely marked shader compiled");
                require(!runtime.pageAccess[id>>12].load(),"Native material token is usable as a guest pointer");
                if(vertex) require(PPC_LOAD_U32(0x82CD1A6C)==0xFFFFFFFF,"Original vertex cache invalidation missing");
                rejects([&]{cpu.invoke(vertex?0x823EFBD0:0x823EFB10,id);});
                require(materials.liveCount()==1,"Wrong-stage release changed ownership");
                if(nativeTranslatedMaterial(original.originalAddress)) {
                    require(materials.prepare(id,original.stage).originalAddress()==original.originalAddress,"Wrong real native artifact");
                } else {
                    rejects([&]{materials.prepare(id,original.stage);});
                    require(materials.capability(id)==MaterialCapability::Unsupported,"Untranslated bind was not explicitly rejected");
                }
                require(cpu.invoke(release,id)==0,"Final material release did not return zero references");
                rejects([&]{cpu.invoke(release,id);});materials.requireReleased();
            }
            PPC_STORE_U32(0x10000,0x11223344);
            rejects([&]{cpu.invoke(0x823EFB78,0x821525E8,0x10000);}); // Vertex record at pixel service.
            rejects([&]{cpu.invoke(0x823EFB78,0x821524C8,0x30000);});
            rejects([&]{cpu.invoke(0x823EFB78,0xFFFFFFFC,0x10000);});
            const uint8_t old=PPC_LOAD_U8(0x821524C8);PPC_STORE_U8(0x821524C8,old^1);
            rejects([&]{cpu.invoke(0x823EFB78,0x821524C8,0x10000);});PPC_STORE_U8(0x821524C8,old);
            require(PPC_LOAD_U32(0x10000)==0x11223344,"Failed material creation published an output");materials.requireReleased();
            for(bool vertex:{false,true}) {
                const uint32_t callback=vertex?0x82D51548:0x82D51544;
                PPC_STORE_U32(callback,0x12345678);
                PPC_STORE_U32(0x82CD1A6C,0xAABBCCDD);
                rejects([&]{cpu.invoke(vertex?0x823EFAA8:0x823EFB78,vertex?0x821525E8:0x821524C8,0x10000);});
                require(PPC_LOAD_U32(0x10000)==0x11223344 && PPC_LOAD_U32(0x82CD1A6C)==0xAABBCCDD,
                    "Unsupported original shader callback changed output or VS cache");
                materials.requireReleased();PPC_STORE_U32(callback,0);
            }
        }
        rejects([&]{cpu.invoke(0x823EFBD0,oldMaterial);});
        {
            EngineMaterials materials(backend);
            cpu.invoke(0x823EFB78,0x821524C8,0x10000);
            const uint32_t id=PPC_LOAD_U32(0x10000);
            require(id>oldMaterial,"Material token reused across owner lifetimes");
            rejects([&]{cpu.invoke(0x823EFBD0,oldMaterial);});cpu.invoke(0x823EFBD0,id);
        }
        auto index=backend.createBuffer(0x4E20,BufferKind::Index16);
        {
            EngineScratchResources scratch(index);
            PPC_STORE_U32(0x82E3DFBC,0x10000);
            cpu.invoke(0x82409A90);
            scratch.validateCreated(PPC_LOAD_U32(0x82D101D4),PPC_LOAD_U32(0x82D101D8));
            require(index.use_count()>=3,"Scratch bridge did not retain real native buffer backing");
            cpu.invoke(0x82408E30);scratch.requireReleased();
            require(PPC_LOAD_U32(0x82D101D4)==0 && PPC_LOAD_U32(0x82D101D8)==0,"Original scratch cleanup left guest IDs");
            cpu.invoke(0x823EF838,0x82151748,0x10000);uint32_t first=PPC_LOAD_U32(0x10000);
            cpu.invoke(0x823EF838,0x82151748,0x10004);require(PPC_LOAD_U32(0x10004)==first,"Original declaration byte cache did not deduplicate");
            cpu.invoke(0x823EFA18,first);rejects([&]{scratch.requireReleased();});
            cpu.invoke(0x823EFA18,first);scratch.requireReleased();rejects([&]{cpu.invoke(0x823EFA18,first);});
            cpu.invoke(0x823EF838,0x82151748,0x10000);require(PPC_LOAD_U32(0x10000)!=first,"Stale declaration token revived");
            cpu.invoke(0x823EFA18,PPC_LOAD_U32(0x10000));
            PPC_STORE_U32(0x10000,0x55667788);
            rejects([&]{cpu.invoke(0x823EF838,0x82151748,0x30000);});
            rejects([&]{cpu.invoke(0x823EF838,0xFFFFFFFC,0x10000);});
            require(PPC_LOAD_U32(0x10000)==0x55667788,"Rejected declaration changed output");scratch.requireReleased();
        }
        require(index.use_count()==1,"Native scratch scope leaked a backing reference");
        require(backend.presentationCount()==0 && backend.screenDrawCount()==0,"Resource lifecycle counted a frame");
        std::printf("PASS: original engine resource bridge / %zu checks\n",checks);return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL after %zu checks: %s\n",checks,error.what());return 1;}
}
