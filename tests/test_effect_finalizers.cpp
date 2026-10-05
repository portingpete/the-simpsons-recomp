// Actual derived finalizers, independent original-byte publication destinations,
// complete private/shared value changes and both real cleanup orders, muted.
#include "effect_catalog_lifecycle_helpers.h"
#include <fstream>
#include <map>

namespace {
struct Publication {uint32_t destination,value;};
struct Row {uint32_t index,vtable,finalizer;std::vector<Publication> publications;};
std::vector<Row> fixture(const char* path) {
    std::ifstream f(path);uint32_t n=0;f>>std::hex>>n;need(f.good() && n==48,"Finalizer fixture header differs");
    std::vector<Row> result;
    for(uint32_t i=0;i<n;++i) {
        Row r{};uint32_t count=0;f>>r.index>>r.vtable>>r.finalizer>>count;
        need(f.good() && r.index<49 && r.index!=2 && count<=32,"Invalid original finalizer row");
        for(uint32_t j=0;j<count;++j) {Publication p{};f>>p.destination>>p.value;r.publications.push_back(p);}
        need(f.good(),"Truncated finalizer fixture");result.push_back(r);
    }
    return result;
}
uint32_t tableRow(uint32_t row) {return row<25?table+16*row:0x82CD1448+16*(row-25);}
void put(std::vector<uint8_t>& bytes,size_t at,uint32_t value) {
    need(at<=bytes.size() && bytes.size()-at>=4,"Expected publication outside buffer");
    for(uint32_t i=0;i<4;++i) bytes[at+i]=uint8_t(value>>(24-8*i));
}
void runFinalizer(Runtime& rt,EngineCpuCalls& cpu,uint32_t t,uint32_t id,const Row& row) {
    auto* base=rt.base;auto& effects=rt.engineDriver->effects();const auto v=effects.view(id);
    const uint32_t pool=PPC_LOAD_U32(root),q=rt.effectPoolBacking;
    auto poolBefore=snapshot(rt,pool,512),values=snapshot(rt,q,0x2A4);
    auto privateValues=v.defaultVectorWords;auto modified=effects.privateModifiedMask(id);
    const auto original=snapshot(rt,v.source,uint32_t(effects.originalBytes(id).size()));
    const auto beforeCount=effects.typedReflectionCount(id);const auto beforeAbi=abi(cpu.registers());
    if(row.index==4) {
        // Distinct finite components expose swaps and accidental value resets.
        PPC_STORE_U32(t+0xC8,0x3E800000);PPC_STORE_U32(t+0xCC,0x3F400000);PPC_STORE_U32(t+0xD0,0x3F000000);
        privateValues[277*4]=PPC_LOAD_U32(t+0xF0);privateValues[278*4]=PPC_LOAD_U32(t+0xF8);
        modified[79/8]|=0x80u>>(79%8);modified[80/8]|=0x80u>>(80%8);
        const uint32_t offset=PPC_LOAD_U32(pool+0x108)-q;
        for(uint32_t leaf:{6u,7u,8u,9u,10u}) poolBefore[leaf/8]|=uint8_t(0x80u>>(leaf%8));
        put(values,offset+15*16,PPC_LOAD_U32(t+0xF0));put(values,offset+16*16,PPC_LOAD_U32(t+0xF4));
        put(values,offset+17*16,PPC_LOAD_U32(t+0xFC));put(values,offset+18*16,0x3F400000);
        put(values,offset+18*16+4,0x3E800000);put(values,offset+18*16+8,0x3F000000);
        put(values,offset+18*16+12,0);put(values,offset+19*16,0);
    }
    need(PPC_LOAD_U32(t)==row.vtable && PPC_LOAD_U32(row.vtable+12)==row.finalizer,"Finalizer virtual dispatch differs");
    // Original slot+C returns incidental r3; it has no specified result value.
    cpu.invoke(PPC_LOAD_U32(row.vtable+12),t);
    need(abi(cpu.registers())==beforeAbi,"Derived finalizer changed original nonvolatile ABI");
    for(const auto& p:row.publications) {
        const auto at=p.destination<0x10000?t+p.destination:p.destination;
        if(PPC_LOAD_U32(at)!=p.value) std::fprintf(stderr,"row=%u finalizer=%08X destination=%08X actual=%08X expected=%08X\n",row.index,row.finalizer,at,PPC_LOAD_U32(at),p.value);
        need(PPC_LOAD_U32(at)==p.value,"Original derived query publication differs");
    }
    need(effects.typedReflectionCount(id)==beforeCount+(row.index!=0),"Derived finalizer common call count differs");
    const auto mask=snapshot(rt,pool+0x80,128);std::copy(mask.begin(),mask.end(),poolBefore.begin()+0x80);
    same(rt,pool,poolBefore,"Derived finalizer changed pool outside known mask/modified bits");
    same(rt,q,values,"Original shadow shared sampler/vector/scalar writes differ");
    same(rt,v.source,original,"Derived finalizer changed original immutable FX");
    need(effects.view(id).defaultVectorWords==privateValues,"Native private sampler defaults differ");
    need(effects.privateModifiedMask(id)==modified,"Private parameter-modification bits differ");
    if(row.index==4) {
        need(PPC_LOAD_U32(t+0xD4)==0 && PPC_LOAD_U32(t+0xC8)==0x3E800000 &&
             PPC_LOAD_U32(t+0xCC)==0x3F400000 && PPC_LOAD_U32(t+0xD0)==0x3F000000,"Shadow CPU scalar/vector field values differ");
        auto call=cpu.registers();call.lastFunction=0x823C7CA0;call.lr=0x82706BC8;call.r31.u64=t;call.r30.u64=v.wrapper;
        call.r29.u64=0x82061428;call.r28.u64=0x148009E;
        const uint32_t borrowed=PPC_LOAD_U32(t+0xF8);PPC_STORE_U32(t+0xF8,0xBAD0);
        rejects([&]{effects.initializeShadowSamplers(call,base);},"Accepted stale borrowed depth sampler");PPC_STORE_U32(t+0xF8,borrowed);
        PPC_STORE_U32(t+0x69C,0);rejects([&]{effects.initializeShadowSamplers(call,base);},"Accepted changed shadow parameter handle");PPC_STORE_U32(t+0x69C,0x14C00A0);
        need(effects.view(id).defaultVectorWords==privateValues && effects.privateModifiedMask(id)==modified,
             "Rejected shadow sampler initialization changed native values");
        same(rt,q,values,"Rejected shadow sampler initialization changed pool values");
    }
}
void cycle(Runtime& rt,EngineCpuCalls& cpu,uint32_t cycle,const std::vector<Row>& rows) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();
    const auto baseline=driver.rasterCount();const auto context=PPC_LOAD_U32(0x82D5DA74);
    const auto opts=cpu.registers().r1.u32+0x60;PPC_STORE_U32(opts,2);PPC_STORE_U32(opts+4,16);PPC_STORE_U32(opts+8,0);
    stage="original registration";const auto manager=cpu.invoke(0x8269BF70,0x260,opts);
    need(manager && cpu.invoke(0x826B6F60,manager,context)==manager,"Original manager creation failed");
    need(cpu.invoke(0x827019E8,table,25)==0 && cpu.invoke(0x827019E8,0x82CD1448,24)==0 && effects.count()==49,"All49 registration failed");
    stage="complete original derived finalizers";
    for(const auto& row:rows) {
        const uint32_t r=tableRow(row.index),id=PPC_LOAD_U32(PPC_LOAD_U32(r+8)+16);
        const auto t=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(r+4));need(t!=0,"Typed finalizer owner missing");
        for(uint32_t round=0;round<2;++round) runFinalizer(rt,cpu,t,id,row);
        queries(rt,cpu,id,PPC_LOAD_U32(r)+12);cache(rt,cpu,id,PPC_LOAD_U32(r)+12);
    }
    stage="complete original manager walk";
    const uint32_t sharedPool=PPC_LOAD_U32(root);
    auto managerBytes=snapshot(rt,manager,0x260);
    const auto sharedRoot=snapshot(rt,sharedPool,512),sharedValues=snapshot(rt,rt.effectPoolBacking,0x2A4);
    // Original manager publishes the first four genuine shared leaf handles.
    for(uint32_t i=0;i<4;++i) put(managerBytes,0x21C+4*i,((i+1)<<18)|(i<<1)|1);
    std::map<uint32_t,const Row*> byOwner;
    std::map<uint32_t,uint32_t> globalPublications;
    for(const auto& row:rows) {
        const uint32_t t=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(tableRow(row.index)+4));
        need(byOwner.emplace(t,&row).second,"Typed manager owner alias");
    }
    uint32_t node=PPC_LOAD_U32(manager+0x34),walked=0;
    need(node!=0,"Original manager list missing");node=PPC_LOAD_U32(PPC_LOAD_U32(node+8));
    std::map<uint32_t,bool> visited;
    while(node) {
        need(walked<48 && visited.emplace(node,true).second,"Original manager list repeats/exceeds48 nodes");
        const uint32_t t=PPC_LOAD_U32(node+4);need(byOwner.contains(t),"Manager list contains unqualified typed owner");
        for(const auto& p:byOwner.at(t)->publications) if(p.destination>=0x10000) globalPublications[p.destination]=p.value;
        node=PPC_LOAD_U32(PPC_LOAD_U32(node+8));++walked;
    }
    need(walked==48,"Original manager omitted a typed finalizer");
    const auto savedAbi=abi(cpu.registers());cpu.invoke(0x826B7218,manager);
    need(abi(cpu.registers())==savedAbi,"Manager finalizer changed ABI");
    same(rt,manager,managerBytes,"Manager changed fields outside four pool-handle publications");
    same(rt,sharedPool,sharedRoot,"Repeated manager reflection changed established shared masks/ownership");
    same(rt,rt.effectPoolBacking,sharedValues,"Repeated manager finalization changed established shared values");
    for(const auto& [at,value]:globalPublications) need(PPC_LOAD_U32(at)==value,"Manager global publication order differs");
    for(const auto& [t,row]:byOwner) {
        const uint32_t id=PPC_LOAD_U32(PPC_LOAD_U32(tableRow(row->index)+8)+16);
        need(effects.typedReflectionCount(id)==(row->index?3u:0u),"Manager did not invoke each common finalizer exactly once");
        for(const auto& p:row->publications) if(p.destination<0x10000)
            need(PPC_LOAD_U32(t+p.destination)==p.value,"Manager typed publication differs");
    }
    need(!PPC_LOAD_U32(0x82D0CAF8) && effects.count()==49,"Finalizers created SDK device/lost owners");
    stage="original finalized cleanup";const auto pool=PPC_LOAD_U32(root);
    const auto poolBefore=snapshot(rt,pool,512),values=snapshot(rt,rt.effectPoolBacking,0x2A4);
    for(bool first:{cycle==0,cycle!=0}) cpu.invoke(0x82701118,first?table:0x82CD1448,first?25:24);
    need(!effects.count() && !driver.shadowTextures().count() && !driver.quadDeclarations().count(),"Finalized cleanup lost native resources");
    same(rt,pool,poolBefore,"Cleanup changed shared root/masks");same(rt,rt.effectPoolBacking,values,"Cleanup changed shared parameter values");
    effects.requirePoolReleased(pool);need(driver.rasterCount()==baseline+4,"Known shadows normal raster cleanup differs");
    need(cpu.invoke(0x826B7600,manager,1)==manager && !PPC_LOAD_U32(0x82D08BFC),"Manager deletion failed");
    std::printf("Finalizer cycle=%u:48 original typed finalizers twice;196 publications per round;complete sampler/value/mask changes;both ownership orders\n",cycle);
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==3,"Original image and finalizer fixture required");const auto rows=fixture(argv[2]);
        Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);const auto entry=original;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;
        try{runOriginal(original,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original startup boundary absent");
        EngineCpuCalls cpu(entry,rt.base);for(uint32_t i=0;i<2;++i) cycle(rt,cpu,i,rows);
        std::printf("PASS original effect finalizers:%zu checks;192 direct and96 manager-dispatched finalizer calls;ALL MUTED;application guarded\n",checks);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL finalizers:stage=%s checks=%zu %s\n",stage,checks,e.what());return 1;}
}
