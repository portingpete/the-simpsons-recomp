// Real registration/lookup/common reflection/cleanup with independent goldens.
// The full manager/finalizer closure has a separate integration fixture.
#include "effect_catalog_lifecycle_helpers.h"
#include <fstream>
#include <iterator>

namespace {
struct GoldenBinding {std::array<uint32_t,6> words;uint32_t elements;};
struct GoldenParameter {std::string name;GoldenBinding binding;std::array<uint32_t,6> classification;};
struct GoldenPass {uint32_t technique,pass;std::array<uint64_t,2> masks;std::vector<std::array<uint32_t,3>> clears;};
struct Golden {
    uint32_t address,bytes,predicate,cube;
    std::vector<GoldenParameter> parameters;
    std::vector<std::array<uint32_t,6>> classifications;
    std::vector<std::array<GoldenBinding,5>> lights;
    uint32_t active,flags;std::vector<GoldenPass> passes;
};
struct Reader {
    std::vector<uint8_t> bytes;size_t at=0;
    uint32_t word() {
        need(at<=bytes.size() && bytes.size()-at>=4,"Golden word outside input");
        uint32_t v=0;for(unsigned i=0;i<4;++i) v=v<<8|bytes[at++];return v;
    }
    uint64_t quad() {const auto hi=word();return uint64_t(hi)<<32|word();}
    std::string text() {
        const auto n=word();need(at<=bytes.size() && n<=bytes.size()-at,"Golden string outside input");
        std::string out(bytes.begin()+at,bytes.begin()+at+n);at+=n;return out;
    }
    std::array<uint32_t,6> six() {std::array<uint32_t,6> out;for(auto& w:out) w=word();return out;}
    GoldenBinding binding() {auto words=six();return {words,word()};}
};
std::vector<Golden> goldens(const char* path) {
    std::ifstream f(path,std::ios::binary);need(bool(f),"Reflection fixture missing");
    Reader r{{std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()}};
    need(r.word()==0x46585246 && r.word()==1 && r.word()==47,"Reflection fixture format differs");
    std::vector<Golden> result;
    for(unsigned i=0;i<47;++i) {
        Golden g{};g.address=r.word();g.bytes=r.word();g.predicate=r.word();g.cube=r.word();
        auto n=r.word();for(uint32_t j=0;j<n;++j) {auto name=r.text();auto b=r.binding();g.parameters.push_back({std::move(name),b,r.six()});}
        n=r.word();for(uint32_t j=0;j<n;++j) g.classifications.push_back(r.six());
        n=r.word();for(uint32_t j=0;j<n;++j) {std::array<GoldenBinding,5> block;for(auto& b:block) b=r.binding();g.lights.push_back(block);}
        g.active=r.word();g.flags=r.word();n=r.word();
        for(uint32_t j=0;j<n;++j) {
            GoldenPass p{};p.technique=r.word();p.pass=r.word();for(auto& mask:p.masks) mask=r.quad();
            const auto count=r.word();for(uint32_t k=0;k<count;++k) {std::array<uint32_t,3> c;for(auto& w:c) w=r.word();p.clears.push_back(c);}
            g.passes.push_back(std::move(p));
        }
        result.push_back(std::move(g));
    }
    need(r.at==r.bytes.size(),"Golden input not fully consumed");return result;
}
void put(std::vector<uint8_t>& bytes,size_t at,uint32_t value) {
    need(at<=bytes.size() && bytes.size()-at>=4,"Expected CPU word extent");
    for(unsigned i=0;i<4;++i) bytes[at+i]=uint8_t(value>>(24-8*i));
}
uint64_t read64(const std::vector<uint8_t>& bytes,size_t at) {
    need(at<=bytes.size() && bytes.size()-at>=8,"Expected CPU doubleword extent");
    uint64_t result=0;for(unsigned i=0;i<8;++i) result=result<<8|bytes[at+i];return result;
}
void bind(std::vector<uint8_t>& bytes,size_t at,const GoldenBinding& b) {
    put(bytes,at,b.words[0]);put(bytes,at+4,b.words[1]);
    for(unsigned lane=0;lane<2;++lane) {
        if(b.words[1]&(lane?0xAA:0x55)) {
            put(bytes,at+8+4*lane,b.words[2+lane]);put(bytes,at+16+4*lane,b.words[4+lane]);
        }
        if(b.elements!=UINT32_MAX) put(bytes,at+16+4*lane,b.elements);
    }
}
uint32_t rowAt(uint32_t row) {return row<25?table+16*row:0x82CD1448+16*(row-25);}
void fill(Runtime& rt,uint32_t at,uint32_t size,uint32_t seed) {
    auto* p=rt.pointer(at,size,true);for(uint32_t i=0;i<size;++i) p[i]=uint8_t(seed+17*i+(i>>3));
}
void gateway(Runtime& rt,EngineCpuCalls& cpu,uint32_t t,uint32_t id,const Golden& g,uint32_t round) {
    auto* base=rt.base;auto& effects=rt.engineDriver->effects();const auto v=effects.view(id);
    const uint32_t p=PPC_LOAD_U32(t+0x28),c=PPC_LOAD_U32(t+0x30),l=PPC_LOAD_U32(t+0x38),pool=PPC_LOAD_U32(root);
    need(PPC_LOAD_U32(c-4)==24,"Original classification prefix differs");
    fill(rt,p,1536,round*23+0x61);fill(rt,c,672,round*13+0x32);fill(rt,l,480,round*7+0x43);
    fill(rt,t+0x48,96,round*19+0x14);fill(rt,t+0x20,4,round+0x85);
    PPC_STORE_U32(t+0x3C,0x76543210);PPC_STORE_U32(t+0x40,0x13579BDF);
    auto object=snapshot(rt,t,0xA8),parameters=snapshot(rt,p,1536),classes=snapshot(rt,c,672),lights=snapshot(rt,l,480);
    auto shared=snapshot(rt,pool+0x80,128);auto privateMask=effects.privateParameterMask(id);
    const auto source=snapshot(rt,g.address,g.bytes),values=snapshot(rt,rt.effectPoolBacking,0x2A4);
    const auto wrapper=snapshot(rt,v.wrapper,0x30),cacheBytes=snapshot(rt,v.cache,v.cacheBytes);
    const auto poolBefore=snapshot(rt,pool,0x200);const auto beforeAbi=abi(cpu.registers());
    const auto beforeCount=effects.typedReflectionCount(id);
    put(object,0x18,v.wrapper);put(object,0x1C,id);object[0x20]=1;
    put(object,0x24,uint32_t(g.parameters.size()));put(object,0x2C,g.cube);put(object,0x34,uint32_t(g.classifications.size()));put(object,0x44,g.flags);
    for(size_t i=0;i<g.parameters.size();++i) bind(parameters,24*i,g.parameters[i].binding);
    for(size_t i=0;i<g.classifications.size();++i) for(size_t j=0;j<6;++j) put(classes,28*i+4*j,g.classifications[i][j]);
    if(!g.lights.empty()) {
        for(size_t i=0;i<g.lights.size();++i) for(size_t j=0;j<5;++j) bind(lights,120*i+24*j,g.lights[i][j]);
        put(object,0x3C,uint32_t(g.lights.size()));put(object,0x40,g.active);
    }
    for(size_t i=0;i<g.passes.size();++i) {
        const auto& pass=g.passes[i];const size_t at=0x48+48*i;
        put(object,at,pass.technique);put(object,at+4,pass.pass);
        for(size_t lane=0;lane<2;++lane) {
            const auto bits=read64(object,at+8+8*lane)|pass.masks[lane];
            put(object,at+8+8*lane,uint32_t(bits>>32));put(object,at+12+8*lane,uint32_t(bits));
        }
        for(const auto& clear:pass.clears) for(uint32_t leaf=clear[1];leaf<clear[1]+clear[2];++leaf) {
            if(clear[0]) shared[leaf/8]&=uint8_t(~(0x80u>>(leaf%8)));
            else privateMask[leaf/8]&=uint8_t(~(0x80u>>(leaf%8)));
        }
    }
    const bool quad=v.source==PPC_LOAD_U32(table+3*16);
    need(cpu.invoke(quad?0x826B7570:0x826B74C8,t,quad?0x820B73F4:PPC_LOAD_U32(t+8))==0,"Original reflection lookup/common return differs");
    need(abi(cpu.registers())==beforeAbi,"Common reflection changed original nonvolatile ABI");
    same(rt,t,object,"Complete typed object differs, including byte neighbors/pass tails");
    same(rt,p,parameters,"Published parameter rows/unused lanes differ");
    same(rt,c,classes,"Classification words/unknown padding differ");
    same(rt,l,lights,"Light members/unused buffer tail differ");
    same(rt,pool+0x80,shared,"Shared dirty-mask changes differ");
    need(effects.privateParameterMask(id)==privateMask,"Private dirty-mask changes differ");
    need(effects.typedReflectionCount(id)==beforeCount+1,"Reflection publication not recorded");
    same(rt,g.address,source,"Reflection changed immutable original FX bytes");
    same(rt,rt.effectPoolBacking,values,"Reflection changed shared defaults/names/descriptors");
    same(rt,v.wrapper,wrapper,"Reflection changed wrapper ownership");same(rt,v.cache,cacheBytes,"Reflection changed literal state cache");
    auto expectedPool=poolBefore;std::copy(shared.begin(),shared.end(),expectedPool.begin()+0x80);
    same(rt,pool,expectedPool,"Reflection changed pool ownership or non-mask fields");
    need(PPC_LOAD_U32(c-4)==24 && PPC_LOAD_U32(pool+0x188)==1,"Reflection changed allocation/refcount ownership");
    stage="reject invalid reflection owners before publication";
    auto direct=cpu.registers();direct.r3.u64=t;direct.lr=quad?0x826B75C8:0x826B7544;direct.lastFunction=0x826B5168;
    PPC_STORE_U32(c-4,23);rejects([&]{effects.reflectTyped(direct,base);},"Accepted wrong classification allocation header");PPC_STORE_U32(c-4,24);
    PPC_STORE_U32(t+0x28,l);rejects([&]{effects.reflectTyped(direct,base);},"Accepted overlapping reflection allocations");PPC_STORE_U32(t+0x28,p);
    same(rt,t,object,"Rejected reflection changed typed data");same(rt,p,parameters,"Rejected reflection changed parameter data");
    same(rt,c,classes,"Rejected reflection changed classification data");same(rt,l,lights,"Rejected reflection changed light data");
    same(rt,pool,expectedPool,"Rejected reflection changed pool data");
    need(effects.privateParameterMask(id)==privateMask && effects.typedReflectionCount(id)==beforeCount+1,"Rejected reflection changed native state");
    stage="original common reflection publication";
}
void cycle(Runtime& rt,EngineCpuCalls& cpu,uint32_t number,const std::vector<Golden>& rows) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();const auto baseline=driver.rasterCount();
    const auto pool=PPC_LOAD_U32(root),context=PPC_LOAD_U32(0x82D5DA74);
    const auto opts=cpu.registers().r1.u32+0x60;PPC_STORE_U32(opts,2);PPC_STORE_U32(opts+4,16);PPC_STORE_U32(opts+8,0);
    stage="register both original tables";
    auto manager=cpu.invoke(0x8269BF70,0x260,opts);need(manager && cpu.invoke(0x826B6F60,manager,context)==manager,"Manager creation failed");
    need(cpu.invoke(0x827019E8,table,25)==0 && cpu.invoke(0x827019E8,0x82CD1448,24)==0 && effects.count()==49,"Combined registration failed");
    // Deliberately nontrivial shared bits ensure reflection clears selected bits
    // without resetting unrelated dirty state. Shared numeric values also survive.
    fill(rt,pool+0x80,128,0xD5+number);PPC_STORE_U32(PPC_LOAD_U32(pool+0x108)+64,number?0x3F400000:0x3E800000);
    std::array<uint32_t,49> ids{};uint32_t count=0;
    for(uint32_t row=0;row<49;++row) {
        const uint32_t r=rowAt(row),w=PPC_LOAD_U32(r+8);ids[row]=PPC_LOAD_U32(w+16);
        auto initial=effects.privateParameterMask(ids[row]);need(std::all_of(initial.begin(),initial.end(),[](uint8_t b){return b==255;}),"Native private mask not initialized from original prefix");
        if(row==0 || row==2) continue;
        const auto found=std::find_if(rows.begin(),rows.end(),[&](const auto& g){return g.address==PPC_LOAD_U32(r);});
        need(found!=rows.end(),"Gateway has no independent golden");
        const auto t=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(r+4));need(t!=0,"Original typed object missing");
        stage="original common reflection publication";
        for(uint32_t round=0;round<2;++round) gateway(rt,cpu,t,ids[row],*found,round);
        queries(rt,cpu,ids[row],found->address+12);cache(rt,cpu,ids[row],found->address+12);
        ++count;
    }
    need(count==47,"Common gateway coverage differs");
    stage="pool leases";
    const auto beforePool=snapshot(rt,pool,0x200),beforeValues=snapshot(rt,rt.effectPoolBacking,0x2A4);
    rejects([&]{cpu.invoke(0x82C1CF00,pool);},"Pool retired while reflected FX leases remained");
    same(rt,pool,beforePool,"Rejected pool retirement changed CPU root");
    need(effects.count()==49 && !PPC_LOAD_U32(0x82D0CAF8),"Reflection created SDK device or lost FX owners");
    stage="original reflected cleanup";
    for(bool first:{number==0,number!=0}) cpu.invoke(0x82701118,first?table:0x82CD1448,first?25:24);
    need(!effects.count() && !driver.quadDeclarations().count() && !driver.shadowTextures().count(),"Original cleanup retained reflected FX resources");
    effects.requirePoolReleased(pool);same(rt,pool,beforePool,"Cleanup changed shared dirty mask/root ownership");
    same(rt,rt.effectPoolBacking,beforeValues,"Cleanup changed shared edited values");
    for(auto id:ids) rejects([&]{effects.privateParameterMask(id);},"Stale reflected native FX remains queryable");
    need(driver.rasterCount()==baseline+4,"Known normal shadows cleanup behavior changed");
    need(cpu.invoke(0x826B7600,manager,1)==manager && !PPC_LOAD_U32(0x82D08BFC),"Manager cleanup failed");
    std::printf("Reflection gateway cycle=%u:47 original lookups,94 common publications; re-entry/tails/masks/pool lifetime checked; original cleanup in %s order\n",number,number?"second24-first25":"first25-second24");
}
}

int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==3,"Original image and reflection fixture required");const auto rows=goldens(argv[2]);
        Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);
        const auto entry=original;bool observed=false;rt.graphicsStartupObserver=observeGraphics;
        try {runOriginal(original,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original post-audio boundary missing");
        EngineCpuCalls cpu(entry,rt.base);for(uint32_t i=0;i<2;++i) cycle(rt,cpu,i,rows);
        std::printf("PASS original reflection gateway: %zu checks,188 common publications; both cleanup orders; application guarded, ALL MUTED\n",checks);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL original reflection gateway: stage=%s checks=%zu %s\n",stage,checks,e.what());return 1;}
}
