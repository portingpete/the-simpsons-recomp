// Both real registration tables, live pool reuse and both declaration cleanup orders.
#include "effect_catalog_lifecycle_helpers.h"
#include <set>

namespace {
constexpr uint32_t secondTable=0x82CD1448;
uint32_t rowAddress(uint32_t row) {return row<25?table+16*row:secondTable+16*(row-25);}

void allTables(Runtime& rt,EngineCpuCalls& cpu,uint32_t cycle,std::array<uint32_t,49>& previous,
               uint32_t& previousPost,uint32_t& editedWord) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();auto& decls=driver.quadDeclarations();
    const auto rasterBaseline=driver.rasterCount();const auto beforeAbi=abi(cpu.registers());
    need(!effects.count() && !decls.count() && !PPC_LOAD_U32(0x82D099A0),"Previous FX/global declaration survived");
    const uint32_t pool=PPC_LOAD_U32(root),oldBacking=rt.effectPoolBacking,context=PPC_LOAD_U32(0x82D5DA74);
    const auto firstRows=snapshot(rt,table,400),secondRows=snapshot(rt,secondTable,384);
    std::array<std::vector<uint8_t>,49> sources;
    for(uint32_t row=0;row<49;++row) {
        const auto r=rowAddress(row),f=PPC_LOAD_U32(r);sources[row]=snapshot(rt,f,PPC_LOAD_U32(f+4)+12);
        need(!PPC_LOAD_U32(r+8),"Registration table already has a wrapper");
    }
    stage="construct manager and register first25";
    const uint32_t options=cpu.registers().r1.u32+0x60;
    PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    const auto manager=cpu.invoke(0x8269BF70,0x260,options);
    need(manager && cpu.invoke(0x826B6F60,manager,context)==manager,"Original manager construction failed");
    need(cpu.invoke(0x827019E8,table,25)==0 && effects.count()==25,"First registration table failed");
    const auto backing=rt.effectPoolBacking,values=PPC_LOAD_U32(pool+0x108);
    need(backing && PPC_LOAD_U32(pool+0x180)==backing && PPC_LOAD_U32(pool+0x184)==0x2A4 &&
         PPC_LOAD_U32(pool+0x188)==1,"Original populated pool shape/refcount differs");
    if(cycle) need(backing==oldBacking && PPC_LOAD_U32(values+64)==editedWord,"First table reset surviving shared values");
    editedWord=cycle?0x3F400000:0x3E800000;PPC_STORE_U32(values+64,editedWord);
    const auto poolBytes=snapshot(rt,pool,0x200),backingBytes=snapshot(rt,backing,0x2A4);
    const auto quad=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(table+3*16+4));
    need(quad && decls.count()==2,"First table's quad declarations missing");
    const uint32_t quadPosition=PPC_LOAD_U32(quad+0xA8),quadTextured=PPC_LOAD_U32(quad+0xAC);
    const auto quadRecord=decls.record(quadTextured);
    stage="register second24 against populated pool";
    need(cpu.invoke(0x827019E8,secondTable,24)==0 && effects.count()==49,"Second registration table failed");
    same(rt,pool,poolBytes,"Second table changed existing pool metadata/refcount");
    same(rt,backing,backingBytes,"Second table changed shared storage/defaults");
    need(abi(cpu.registers())==beforeAbi,"Combined registration changed nonvolatile ABI");
    const uint32_t post=PPC_LOAD_U32(0x82D099A0);
    need(post && post!=previousPost && post!=quadPosition && post!=quadTextured && !rt.pageAccess[post>>12].load() &&
         decls.owns(post) && decls.count()==3,"Cached post declaration lacks separate fresh native ownership");
    const auto postRecord=decls.record(post);
    need(postRecord==quadRecord && postRecord->bytes().size()==36 && postRecord->elements().size()==2 &&
         postRecord->minimumStreamBytes()==16 && postRecord->bytes().data()!=rt.pointer(0x82061674,36,false) &&
         !std::memcmp(postRecord->bytes().data(),rt.pointer(0x82061674,36,false),36),
         "Post declaration failed immutable byte dedup/ownership/extent");
    stage="verify all49 original metadata queries and caches";
    std::array<uint32_t,49> ids{};std::set<uint32_t> unique;size_t shaders=0,compiled=0,typed=0;
    for(uint32_t row=0;row<49;++row) {
        const uint32_t r=rowAddress(row),f=PPC_LOAD_U32(r),w=PPC_LOAD_U32(r+8),name=PPC_LOAD_U32(r+4);
        need(w!=0,"Original row has no wrapper");ids[row]=PPC_LOAD_U32(w+16);
        need(ids[row] && ids[row]!=previous[row] && !rt.pageAccess[ids[row]>>12].load() && unique.insert(ids[row]).second,
             "Native FX identity reused, aliased or mapped");
        const auto view=effects.view(ids[row]);const auto owned=effects.originalBytes(ids[row]);
        need(view.source==f && view.manager==manager && view.pool==pool && view.wrapper==w,
             "Effect owner lost original wrapper/source/pool association");
        need(owned.size()==sources[row].size() && owned.data()!=rt.pointer(f,uint32_t(owned.size()),false) &&
             std::equal(owned.begin(),owned.end(),sources[row].begin()),"Original effect envelope changed or borrowed");
        shaders+=effects.shaderCount(ids[row]);compiled+=effects.compiledShaderCount(ids[row]);
        const auto t=cpu.invoke(0x826B7088,manager,name);typed+=(t!=0);
        need((t!=0)==(PPC_LOAD_U32(r+12)!=0),"Original typed callback presence differs");
        cache(rt,cpu,ids[row],f+12);queries(rt,cpu,ids[row],f+12);
        if(row!=0 && row!=3)
            need(PPC_LOAD_U32(effects.sharedParameterStorage(ids[row],0x80003))==editedWord,
                 "FX association lost the live edited shared value");
    }
    need(shaders==215 && compiled==2 && typed==48,"Combined original inventory/capability counts differ");
    need(driver.shadowTextures().count()==3 && driver.rasterCount()==rasterBaseline+4,"Combined registration changed shadow ownership");
    need(!effects.parameter(ids[27],"FakeLighting") && !effects.parameter(ids[27],"FakeLightingThreshold"),
         "Missing original edge parameters were fabricated");
    uint32_t name=PPC_LOAD_U32(pool+0x11C);
    for(uint32_t i=0;i<11;++i) {
        const auto text=stringAt(rt,name);const auto handle=((i+1)<<18)|(i<<1)|1;
        need(cpu.invoke(0x826B2528,pool,name)==handle,"Original pool lookup differs");
        for(uint32_t row=25;row<49;++row)
            need(cpu.invoke(0x823C7B20,ids[row],name)==handle,"Second table did not reuse the complete live pool namespace");
        name+=uint32_t(text.size())+1;
    }
    stage="reject malformed cached declaration operations";
    auto bad=cpu.registers();bad.r3.u64=0x82061674;bad.r30.u64=0x82D10000;bad.lr=0x823CA354;
    bad.r31.u64=0;
    rejects([&]{decls.createPost(bad,base);},"Cached declaration accepted missing original owner");
    bad=cpu.registers();bad.r3.u64=post;bad.r30.u64=0;bad.r31.u64=0x82D10000;bad.lr=0x823CA3E0;
    rejects([&]{decls.release(bad,base);},"Cached declaration accepted wrong destruction owner");
    need(PPC_LOAD_U32(0x82D099A0)==post && decls.count()==3 && decls.record(post)==postRecord,
         "Rejected operation damaged existing declaration owners");
    auto cleanup=[&](bool first) {
        const uint32_t begin=first?0:25,end=first?25:49;
        cpu.invoke(0x82701118,first?table:secondTable,first?25:24);
        for(uint32_t row=begin;row<end;++row) {
            need(!PPC_LOAD_U32(rowAddress(row)+8),"Original cleanup did not clear table wrapper");
            need(!cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(rowAddress(row)+4)),"Original typed registration survived cleanup");
            rejects([&]{effects.view(ids[row]);},"Retired FX ID survived cleanup");
        }
    };
    stage=cycle?"second24 cleanup before first25":"first25 cleanup before second24";
    if(!cycle) {
        cleanup(true);
        need(effects.count()==24 && decls.count()==1 && PPC_LOAD_U32(0x82D099A0)==post && decls.record(post)==postRecord,
             "First-table cleanup released the second table's declaration reference");
        rejects([&]{decls.record(quadPosition);},"Retired quad position ID survived");
        rejects([&]{decls.record(quadTextured);},"Retired quad UV ID survived");
        cleanup(false);
    } else {
        cleanup(false);
        need(effects.count()==25 && decls.count()==2 && !PPC_LOAD_U32(0x82D099A0) && decls.record(quadTextured)==quadRecord,
             "Second-table cleanup released the quad's distinct declaration reference");
        rejects([&]{decls.record(post);},"Retired post declaration ID survived");
        cleanup(true);
    }
    need(!effects.count() && !decls.count() && !driver.shadowTextures().count() && !PPC_LOAD_U32(0x82D099A0),
         "Original combined cleanup retained FX/declaration/texture owners");
    effects.requireReleased();effects.requirePoolReleased(pool);decls.requireReleased();driver.shadowTextures().requireReleased();
    for(uint32_t id:{post,quadPosition,quadTextured}) rejects([&]{decls.record(id);},"Stale native declaration remained queryable");
    need(postRecord->bytes().size()==36 && !std::memcmp(postRecord->bytes().data(),rt.pointer(0x82061674,36,false),36),
         "Logical cleanup damaged retained immutable CPU cache snapshot");
    same(rt,table,firstRows,"First table not restored");same(rt,secondTable,secondRows,"Second table not restored");
    for(uint32_t row=0;row<49;++row) same(rt,PPC_LOAD_U32(rowAddress(row)),sources[row],"Original FX source mutated");
    same(rt,pool,poolBytes,"Combined cleanup changed genuine pool header/refcount");
    same(rt,backing,backingBytes,"Combined cleanup changed surviving shared values");
    // The known normal shadows destructor gap remains visible in both orders.
    need(driver.rasterCount()==rasterBaseline+4,"Normal shadows raster cleanup behavior changed without qualification");
    need(cpu.invoke(0x826B7600,manager,1)==manager && !PPC_LOAD_U32(0x82D08BFC),"Original manager cleanup failed");
    need(!PPC_LOAD_U32(0x82D0CAF8) && PPC_LOAD_U32(0x82D5DA74)==context,"Combined FX lifecycle created a console device");
    need(abi(cpu.registers())==beforeAbi,"Combined cleanup changed original nonvolatile ABI");
    previous=ids;previousPost=post;
    std::printf("Combined catalog cycle=%u:49 FX/48 typed/215 shaders(2 compiled); shared pool preserved; both declaration references released; normal shadows retains4 camera rasters\n",cycle);
}
}

int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==2,"Original image required");Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);
        try {
            const auto entry=original;bool observed=false;rt.graphicsStartupObserver=observeGraphics;
            try {runOriginal(original,rt.base);}catch(const Observed&){observed=true;}
            rt.graphicsStartupObserver={};need(observed,"Original post-audio startup boundary not reached");
            EngineCpuCalls cpu(entry,rt.base);std::array<uint32_t,49> previous{};uint32_t previousPost=0,editedWord=0;
            for(uint32_t i=0;i<2;++i) allTables(rt,cpu,i,previous,previousPost,editedWord);
            std::printf("PASS combined effect catalog lifecycle: %zu checks; both real registration tables and cleanup orders; ALL MUTED; no finalization, draw or full camera teardown claim\n",checks);
        }catch(const std::exception& e){std::fprintf(stderr,"Combined fixture failed before Runtime teardown: stage=%s checks=%zu error=%s\n",stage,checks,e.what());throw;}
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL combined effect lifecycle: %s\n",e.what());return 1;}
}
