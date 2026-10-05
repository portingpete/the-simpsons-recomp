#pragma once
#include "effect_catalog_lifecycle_helpers.h"
#include "runtime/engine_audio.h"

namespace {
// Paired original FX owner cleanup after an actual scene draw. This releases
// effect IDs and their CPU caches; backend-owned immutable mesh upload caches
// remain resident until their bounded cache evicts them or the backend ends.
struct OriginalDrawCatalogCleanup {
    static constexpr uint32_t second=0x82CD1448;
    std::vector<uint8_t> firstRows,secondRows;
    uint32_t pool{},backing{};
    explicit OriginalDrawCatalogCleanup(Runtime& rt)
        :firstRows(snapshot(rt,table,400)),secondRows(snapshot(rt,second,384)) {
        auto* base=rt.base;pool=PPC_LOAD_U32(root);
    }
    void release(Runtime& rt,EngineCpuCalls& cpu,uint32_t manager) {
        auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();
        struct Owner {uint32_t id,cache,cacheBytes;};
        std::array<Owner,49> owners{};
        backing=rt.effectPoolBacking;
        const auto borrowedDepth=PPC_LOAD_U32(0x82D0CF84);
        need(effects.count()==49&&pool&&backing,"Draw cleanup lacks the complete original FX catalog/pool");
        for(uint32_t row=0;row<owners.size();++row) {
            const auto entry=row<25?table+16*row:second+16*(row-25);
            const auto wrapper=PPC_LOAD_U32(entry+8),id=PPC_LOAD_U32(wrapper+0x10);
            const auto view=effects.view(id);
            need(view.manager==manager&&view.pool==pool&&view.cache&&view.cacheBytes,
                 "Draw cleanup lost an original effect/cache owner");
            need(rt.engineAudio->allocationGeneration(view.cache,view.cacheBytes)!=0,
                 "Draw cleanup cache has no original allocator owner");
            owners[row]={id,view.cache,view.cacheBytes};
        }
        stage="paired original drawn-effect catalog cleanup";
        const auto before=abi(cpu.registers());
        // Retire the target second-table effects first, retaining the genuine
        // first-table shadow/quad parents until their own paired cleanup.
        cpu.invoke(0x82701118,second,24);
        need(effects.count()==25,"Draw cleanup retained a second-table effect");
        cpu.invoke(0x82701118,table,25);
        effects.requireReleased();effects.requirePoolReleased(pool);
        driver.quadDeclarations().requireReleased();driver.shadowTextures().requireReleased();
        need(!effects.count()&&!driver.quadDeclarations().count()&&!driver.shadowTextures().count()&&
             !PPC_LOAD_U32(0x82D099A0),"Draw cleanup retained effect/declaration/shadow texture ownership");
        for(uint32_t row=0;row<owners.size();++row) {
            const auto entry=row<25?table+16*row:second+16*(row-25);
            const auto owner=owners[row];
            need(!PPC_LOAD_U32(entry+8)&&!cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(entry+4)),
                 "Draw cleanup retained original wrapper/typed publication");
            rejects([&]{effects.view(owner.id);},"Retired drawn FX identity remained usable");
            rejects([&]{effects.originalBytes(owner.id);},"Retired drawn FX original bytes remained borrowable");
            rejects([&]{effects.compiledShaderCount(owner.id);},"Retired drawn FX shader records remained usable");
            rejects([&]{rt.engineAudio->allocationGeneration(owner.cache,owner.cacheBytes);},
                    "Retired drawn FX CPU cache retained allocator ownership");
        }
        same(rt,table,firstRows,"Draw cleanup did not restore the first original FX table");
        same(rt,second,secondRows,"Draw cleanup did not restore the second original FX table");
        need(PPC_LOAD_U32(root)==pool&&rt.effectPoolBacking==backing&&PPC_LOAD_U32(pool+0x188)==1,
             "Draw cleanup released the root-owned shared pool");
        need(PPC_LOAD_U32(0x82D0CF84)==borrowedDepth&&bool(driver.depth(borrowedDepth)),
             "Draw cleanup released the borrowed driver depth-copy owner");
        stage="original drawn-effect manager destruction";
        need(cpu.invoke(0x826B7600,manager,1)==manager&&!PPC_LOAD_U32(0x82D08BFC),
             "Original drawn-effect manager destruction failed");
        need(abi(cpu.registers())==before,"Original drawn-effect cleanup damaged nonvolatile ABI");
    }
};
}
