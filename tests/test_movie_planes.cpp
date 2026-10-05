#include "effect_catalog_lifecycle_helpers.h"
#include "renderer/engine_state.h"
#include <exception>
#include <limits>
#include <set>
#include <thread>

namespace {
constexpr uint32_t frameBytes=0x54,guardBytes=16;
constexpr uint32_t publicLock=0x82408208,publicUnlock=0x82407BA8,publicDestroy=0x82407DC0;

// Expected values below come from the primary analysis/simpsons.pe words, not
// native implementation metadata. Addresses and all structure offsets are hex.
void originalContract(Runtime& rt) {
    auto* base=rt.base;
    const std::array words={
        std::array<uint32_t,2>{0x8282EB24,0x396BD4B0}, // separate presenter
        std::array<uint32_t,2>{0x8282ED6C,0x396BD4F0}, // allocation provider
        std::array<uint32_t,2>{0x8282E910,0x90830004}, // original allocator setter
        std::array<uint32_t,2>{0x8274B1E8,0x909F0004}, // adapter's backing heap
        std::array<uint32_t,2>{0x8274B1FC,0x816B0008}, // retain original backing heap
        std::array<uint32_t,2>{0x8274B258,0x816B000C}, // paired release
        std::array<uint32_t,2>{0x8243E598,0x39000100}, // linear pitch alignment
        std::array<uint32_t,2>{0x8243E5C8,0x7D2B5878},
        std::array<uint32_t,2>{0x8243E5D8,0x917F0000},
        std::array<uint32_t,2>{0x8238EAB0,0x816B0020}, // original pooled allocation dispatch
        std::array<uint32_t,2>{0x8238EB74,0x816B0024}, // original paired free dispatch
        std::array<uint32_t,2>{0x8268E18C,0x38A01000}, // minimum pool alignment
        std::array<uint32_t,2>{0x8268E704,0x817E0010}, // pooled-pointer classification
        std::array<uint32_t,2>{0x8268E710,0x815E0014},
        std::array<uint32_t,2>{0x823738C8,0x91430030},
        std::array<uint32_t,2>{0x823738CC,0x3D403704},
        std::array<uint32_t,2>{0x823738D4,0x614A7734},
        std::array<uint32_t,2>{0x82373918,0x99630051}, // last initialized byte, not sizeof
        std::array<uint32_t,2>{0x8282E9B4,0x4BBC88C5},
        std::array<uint32_t,2>{0x8282E9C4,0x4BBD9845},
        std::array<uint32_t,2>{0x8282E9E8,0x834A0010},
        std::array<uint32_t,2>{0x8282EA04,0x93A30000},
        std::array<uint32_t,2>{0x8282EA08,0x93630004},
        std::array<uint32_t,2>{0x8282EA10,0x93430008},
        std::array<uint32_t,2>{0x8282EA1C,0x937FFFF4},
        std::array<uint32_t,2>{0x8282EA24,0x907F0034},
        std::array<uint32_t,2>{0x8282EA28,0x935F000C},
        std::array<uint32_t,2>{0x8282EA2C,0x917F0000},
        std::array<uint32_t,2>{0x8282EA44,0x382100D0}, // commit before the real epilogue
        std::array<uint32_t,2>{0x82408234,0x4E800421},
        std::array<uint32_t,2>{0x8240823C,0x80610050}, // public result is the output slot
        std::array<uint32_t,2>{0x82407BD8,0x4E800421},
        std::array<uint32_t,2>{0x82407BDC,0x7FE3FB78}, // public unlock returns raster
        std::array<uint32_t,2>{0x823F5524,0x997F0022},
        std::array<uint32_t,2>{0x823F5530,0x917F0004},
        std::array<uint32_t,2>{0x823F5538,0x915F002C},
        std::array<uint32_t,2>{0x823F553C,0x917F0028},
        std::array<uint32_t,2>{0x823F5570,0x917F0018},
        std::array<uint32_t,2>{0x823F5574,0x9B5E000B},
        std::array<uint32_t,2>{0x823F557C,0x91770000},
        std::array<uint32_t,2>{0x823F5610,0x917F0018},
        std::array<uint32_t,2>{0x823F5614,0x917F0004},
        std::array<uint32_t,2>{0x823F5680,0x716B00F9},
        std::array<uint32_t,2>{0x8282ECE4,0x4BBD9525},
        std::array<uint32_t,2>{0x8282ED3C,0x4BBD8E6D},
        std::array<uint32_t,2>{0x8282EA8C,0x4BBD911D},
        std::array<uint32_t,2>{0x8282EA94,0x4BBD932D},
        std::array<uint32_t,2>{0x8282EAA8,0x816B000C},
        std::array<uint32_t,2>{0x8282EABC,0x939F0000},
        std::array<uint32_t,2>{0x8282EAC8,0x938BFFF4},
        std::array<uint32_t,2>{0x8282EACC,0x938B0000},
        std::array<uint32_t,2>{0x8282EAD8,0x939A0028},
        std::array<uint32_t,2>{0x8282EADC,0x939A002C},
        std::array<uint32_t,2>{0x8282EAE0,0x917A0030}
    };
    for(const auto& row:words) need(PPC_LOAD_U32(row[0])==row[1],"Primary movie-plane instruction differs");
    const auto engine=PPC_LOAD_U32(0x82D0CA68);
    need(PPC_LOAD_U32(engine+0x84)==0x823F53D8&&PPC_LOAD_U32(engine+0x88)==0x823F5588,
        "Original public raster callback registration differs");
}

void put(std::vector<uint8_t>& bytes,uint32_t offset,uint32_t value) {
    need(uint64_t(offset)+4<=bytes.size(),"Fixture expected-word bounds differ");
    for(uint32_t i=0;i<4;++i) bytes[offset+i]=uint8_t(value>>(24-8*i));
}
struct ThreadAbi {
    SavedAbi saved;
    uint64_t r2,r13;
    bool operator==(const ThreadAbi&) const=default;
};
ThreadAbi threadAbi(const PPCContext& c) {return {abi(c),c.r2.u64,c.r13.u64};}

struct Frame {
    uint32_t allocation{},address{},ordinal{};
    std::vector<uint8_t> initialized,published;
};
Frame newFrame(Runtime& rt,const PPCContext& entry,uint32_t ordinal,uint8_t poison) {
    EngineCpuCalls cpu(entry,rt.base);
    Frame f;f.ordinal=ordinal;f.allocation=cpu.invoke(0x8269BD70,frameBytes+2*guardBytes);
    need(f.allocation!=0,"Original descriptor allocation failed");f.address=f.allocation+guardBytes;
    std::memset(rt.pointer(f.allocation,frameBytes+2*guardBytes,true),poison,frameBytes+2*guardBytes);
    auto expected=snapshot(rt,f.allocation,frameBytes+2*guardBytes);
    for(uint32_t at:{4u,8u,0xCu,0x10u,0x14u,0x18u,0x28u,0x2Cu,0x34u,0x38u,0x3Cu,0x44u,0x48u,0x4Cu})
        put(expected,guardBytes+at,0);
    put(expected,guardBytes,0x37047734);put(expected,guardBytes+0x30,4);
    for(uint32_t at:{0x40u,0x50u,0x51u}) expected[guardBytes+at]=0;
    const auto before=threadAbi(cpu.registers());cpu.invoke(0x823738C0,f.address);
    need(threadAbi(cpu.registers())==before,"Original descriptor initializer changed thread/nonvolatile ABI");
    same(rt,f.allocation,expected,"Original initializer changed pitch, spare bytes or descriptor guards");
    f.initialized=std::move(expected);return f;
}

struct Plane {
    uint32_t frame{},index{},raster{},context{},data{},pitch{},width{},height{},identity{};
    uint32_t spanBytes{};
    bool observedTexture=false;
    std::vector<uint8_t> rasterLocked,contextBytes,expected,staging;
    std::weak_ptr<Graphics::Texture> weak;
};

Plane planeAt(Runtime& rt,const Frame& f,uint32_t index,uint32_t width,uint32_t height) {
    auto* base=rt.base;Plane p;p.frame=f.address;p.index=index;
    p.width=width>>(index!=0);p.height=height>>(index!=0);
    p.context=PPC_LOAD_U32(f.address+0x44+4*index);
    need(p.context&&!(p.context&3),"Original allocator did not publish a RasterContext");
    rt.pointer(p.context,12,false);p.raster=PPC_LOAD_U32(p.context);
    p.data=PPC_LOAD_U32(f.address+4+4*index);p.pitch=PPC_LOAD_U32(f.address+0x1C+4*index);
    const auto x=p.raster+PPC_LOAD_U32(0x82E3DC94);p.identity=PPC_LOAD_U32(x);
    need(p.data&&p.pitch>=p.width&&uint64_t(p.pitch)*p.height<=std::numeric_limits<uint32_t>::max(),
        "Native movie pointer/pitched span is invalid");
    // Primary linear R8 layout8243E588..E5D8: 256-byte row alignment.
    const uint32_t originalPitch=p.width==1280?1280:(p.width==640?768:512);
    need(p.pitch==originalPitch,"Native CPU pitch differs from the original linear R8 profile");
    need(PPC_LOAD_U32(p.context+4)==p.data&&PPC_LOAD_U32(p.context+8)==p.pitch&&
        PPC_LOAD_U32(f.address+0x10+4*index)==p.width*p.height,"Original descriptor/context publication differs");
    need(PPC_LOAD_U32(p.raster)==p.raster&&PPC_LOAD_U32(p.raster+0xC)==p.width&&
        PPC_LOAD_U32(p.raster+0x10)==p.height&&PPC_LOAD_U32(p.raster+0x14)==8&&
        !PPC_LOAD_U32(p.raster+8)&&!PPC_LOAD_U32(p.raster+0x1C)&&
        !PPC_LOAD_U8(p.raster+0x20)&&!PPC_LOAD_U8(p.raster+0x21)&&!PPC_LOAD_U8(p.raster+0x23),
        "Original plane root, depth, offsets or format bytes differ");
    need(PPC_LOAD_U8(p.raster+0x22)==4&&PPC_LOAD_U32(p.raster+4)==p.data&&
        PPC_LOAD_U32(p.raster+0x18)==p.pitch&&PPC_LOAD_U32(p.raster+0x28)==p.width&&
        PPC_LOAD_U32(p.raster+0x2C)==p.height&&PPC_LOAD_U32(x+0x10)==p.pitch&&
        PPC_LOAD_U32(x+0x14)==p.data&&!PPC_LOAD_U32(x+8),"Initial mode5/level0 CPU lock fields differ");
    // X+C is explicitly unavailable in the native representation; no SDK
    // acquired/released surface address is fabricated for an R8 texture.
    need(p.identity&&!rt.pageAccess[p.identity>>12].load()&&!PPC_LOAD_U32(x+4)&&
        !PPC_LOAD_U32(x+0xC)&&PPC_LOAD_U32(x+0x18)==0x28000002,"Native plane extension/identity differs");
    // Original8238E880(size,BC800000) obtains a page-aligned block from the
    // existing application pool. The containing physical allocation is shared
    // with unrelated owners; neither its base nor its extent belongs to us.
    p.spanBytes=p.pitch*p.height;
    const auto heap=PPC_LOAD_U32(0x82D57244);
    need(heap==0x82D5724C&&PPC_LOAD_U32(heap)==0x820B60B8&&
        PPC_LOAD_U32(0x820B60D8)==0x8268E138&&PPC_LOAD_U32(0x820B60DC)==0x8268E6E8,
        "Original pooled allocation/free registration differs");
    const auto pool=PPC_LOAD_U32(heap+0x10),poolBytes=PPC_LOAD_U32(heap+0x14);
    need(pool&&poolBytes&&!(p.data&4095)&&p.data>=pool&&
        uint64_t(p.data)+p.spanBytes<=uint64_t(pool)+poolBytes,"Plane CPU block is outside the original pool or unaligned");
    rt.pointer(p.data,p.spanBytes,true);
    p.contextBytes=snapshot(rt,p.context,12);
    p.rasterLocked=snapshot(rt,p.raster,PPC_LOAD_U32(0x82CD1E28));
    rejects([&]{rt.engineDriver->textureRaster(p.raster);},"Locked plane exposed a sampleable texture");
    return p;
}

// The decoder consumes the explicit stride. Include higher x/y bits to expose
// 256-pixel tiling errors as well as row/plane swaps.
uint8_t sample(uint32_t x,uint32_t y,uint32_t frame,uint32_t plane,uint32_t version) {
    return uint8_t(13*x+29*y+47*(x>>8)+71*(y>>8)+53*frame+83*plane+97*version+(x*y>>5));
}
void writePlane(Runtime& rt,Plane& p,uint32_t frame,uint32_t version) {
    auto* base=rt.base;need(PPC_LOAD_U32(p.raster+4)==p.data,"CPU writer was given an unlocked plane");
    // Only pitch*H bytes belong to this request. Padding within those rows is
    // guarded; surrounding mapped pool bytes and any allocator slack are not
    // ours to read/write. Descriptor guards are separate original allocations.
    auto* data=rt.pointer(p.data,p.spanBytes,true);
    const auto padding=uint8_t(0xA5+7*frame+11*p.index+version);
    std::memset(data,padding,p.spanBytes);p.expected.resize(size_t(p.width)*p.height);
    for(uint32_t y=0;y<p.height;++y) for(uint32_t x=0;x<p.width;++x)
        data[size_t(y)*p.pitch+x]=p.expected[size_t(y)*p.width+x]=sample(x,y,frame,p.index,version);
    p.staging=snapshot(rt,p.data,p.spanBytes);
}

void caller(PPCContext& c,uint32_t presenter,const Plane& p,bool lock) {
    c.r3.u64=p.raster;c.r4.u64=0;c.r5.u64=lock?5:0;
    c.r31.u64=presenter;c.r30.u64=p.frame;c.r29.u64=p.index;
    c.r28.u64=lock?0x44+4*p.index:p.frame+0x44+4*p.index;
    c.lr=lock?0x8282ECE8:0x8282ED40;
}
void cpuFields(Runtime& rt,const Plane& p,bool locked) {
    auto expected=p.rasterLocked;
    if(!locked) {put(expected,4,0);put(expected,0x18,0);expected[0x22]&=0xF9;}
    same(rt,p.raster,expected,"Public lock/unlock changed the wrong raster/plugin bytes");
    same(rt,p.context,p.contextBytes,"Public lock/unlock refreshed cached RasterContext fields");
    same(rt,p.data,p.staging,"Public lock/unlock changed CPU samples or row padding");
}
void access(Runtime& rt,const PPCContext& entry,uint32_t presenter,const Frame& f,Plane& p,bool lock) {
    auto* base=rt.base;
    // Reproduce the caller profile at 8282ECE4/8282ED3C. The full 8282EC58
    // parent is exercised separately by --integration. This storage-only path
    // sets its current-frame field as fixture input, not parent execution.
    PPC_STORE_U32(presenter+0x14,f.address);
    const auto owner=snapshot(rt,presenter,0x44);EngineCpuCalls cpu(entry,base);
    caller(cpu.registers(),presenter,p,lock);const auto before=threadAbi(cpu.registers());
    need(cpu.invoke(lock?publicLock:publicUnlock)==(lock?p.data:p.raster),"Original public wrapper return/output-slot ABI differs");
    need(threadAbi(cpu.registers())==before,"Original public wrapper changed thread/nonvolatile ABI");
    same(rt,presenter,owner,"Public wrapper changed the original presenter");
    same(rt,f.allocation,f.published,"Public wrapper changed descriptor data, pitch or guards");cpuFields(rt,p,lock);
    if(lock) rejects([&]{rt.engineDriver->textureRaster(p.raster);},"Relocked plane exposed a sampleable texture");
}

void readback(Runtime& rt,Plane& p) {
    auto* base=rt.base;need(!PPC_LOAD_U32(p.raster+4),"Fixture readback attempted while locked");
    auto& d=*rt.engineDriver;
    {
        const auto texture=d.textureRaster(p.raster);
        need(texture->format==Graphics::TextureFormat::R8&&texture->width==p.width&&
            texture->height==p.height&&texture->levelCount()==1,"Native movie texture profile differs");
        if(p.observedTexture) need(!p.weak.expired()&&p.weak.lock()==texture,"Unlock/relock replaced the native plane resource");
        p.weak=texture;p.observedTexture=true;
    }
    const auto actual=d.readbackTextureRaster(p.raster);
    need(actual.size()==p.expected.size(),"Native R8 readback included padding or a wrong extent");
    const auto mismatch=std::mismatch(actual.begin(),actual.end(),p.expected.begin());
    if(mismatch.first!=actual.end()) {
        const auto at=size_t(mismatch.first-actual.begin());
        std::fprintf(stderr,"R8 mismatch frame=%08X plane=%u x=%zu y=%zu pitch=%u actual=%02X expected=%02X\n",
            p.frame,p.index,at%p.width,at/p.width,p.pitch,unsigned(*mismatch.first),unsigned(*mismatch.second));
    }
    need(actual==p.expected,"Native upload/readback differs from CPU-written logical samples");cpuFields(rt,p,false);
}

template<class F> void rejectedPublic(Runtime& rt,const PPCContext& entry,uint32_t presenter,const Plane& p,
    bool lock,F&& change,const char* message) {
    EngineCpuCalls cpu(entry,rt.base);caller(cpu.registers(),presenter,p,lock);change(cpu.registers());
    const auto before=threadAbi(cpu.registers());
    rejects([&]{cpu.invoke(lock?publicLock:publicUnlock);},message);
    need(cpu.registers().lastFunction==(lock?publicLock:publicUnlock),"Rejected access entered the original wrapper body");
    need(threadAbi(cpu.registers())==before,"Rejected preflight changed original ABI");
}

void rejections(Runtime& rt,const PPCContext& entry,uint32_t presenter,uint32_t provider,const Frame& f,Plane& p,uint32_t crossfade) {
    auto* base=rt.base;auto& d=*rt.engineDriver;PPC_STORE_U32(presenter+0x14,f.address);
    const auto owner=snapshot(rt,presenter,0x44),allocationOwner=snapshot(rt,provider,8);
    const auto count=d.rasterCount(),physical=rt.physicalAllocations.size();
    const auto unchanged=[](PPCContext&){};
    for(uint32_t mode:{0u,1u,2u,3u,4u,7u,0x105u})
        rejectedPublic(rt,entry,presenter,p,true,[&](PPCContext& c){c.r5.u64=mode;},"Non-mode5 movie lock accepted");
    for(uint32_t level:{1u,2u,255u,256u})
        rejectedPublic(rt,entry,presenter,p,true,[&](PPCContext& c){c.r4.u64=level;},"Nonzero movie mip accepted");
    for(uint32_t mutation=0;mutation<5;++mutation)
        rejectedPublic(rt,entry,presenter,p,true,[&](PPCContext& c){
            switch(mutation) {
                case 0:c.lr+=4;break;
                case 1:c.lr=0x8282E9C8;break; // initial caller cannot relock a published frame
                case 2:++c.r29.u64;break;
                case 3:c.r28.u64+=4;break;
                case 4:c.r31.u64=0;break;
            }
        },"Wrong original movie relock caller accepted");
    rejectedPublic(rt,entry,presenter,p,false,unchanged,"Double movie unlock accepted");
    for(uint32_t raster:{0u,0xDEADu,crossfade}) for(bool lock:{false,true})
        rejectedPublic(rt,entry,presenter,p,lock,[&](PPCContext& c){c.r3.u64=raster;},"Unknown/crossfade raster accepted movie access");
    // Keep every field mutation isolated. A valid-looking child/root mismatch
    // must not turn an owned movie raster into a supported subraster.
    for(const auto field:std::array<std::array<uint32_t,2>,8>{{
        {p.raster,p.raster+4},{p.raster+0x1C,1},{p.raster+0x14,32},
        {f.address+4+4*p.index,p.data+4},{f.address+0x1C+4*p.index,p.pitch+1},
        {f.address+0x10+4*p.index,p.width*p.height+1},{p.context+4,p.data+4},{provider+4,0}}}) {
        const auto old=PPC_LOAD_U32(field[0]);PPC_STORE_U32(field[0],field[1]);
        rejects([&]{d.textureRaster(p.raster);},"Changed movie ownership/CPU metadata accepted");
        rejectedPublic(rt,entry,presenter,p,true,unchanged,"Changed movie ownership accepted relock");
        PPC_STORE_U32(field[0],old);
    }
    PPC_STORE_U32(presenter+0x14,0);
    rejectedPublic(rt,entry,presenter,p,true,unchanged,"Relock accepted a different current frame");PPC_STORE_U32(presenter+0x14,f.address);
    for(uint32_t slot=0;slot<8;++slot) {
        const auto at=0x82D0E3F8+slot*0x18,old=PPC_LOAD_U32(at);PPC_STORE_U32(at,p.raster);
        EngineCpuCalls cpu(entry,base);const auto before=threadAbi(cpu.registers());
        rejects([&]{cpu.invoke(publicDestroy,p.raster);},"Bound movie raster destruction accepted");
        need(cpu.registers().lastFunction==publicDestroy&&threadAbi(cpu.registers())==before,
            "Bound destruction reached plugin/free callbacks or changed ABI");PPC_STORE_U32(at,old);
    }
    bool foreignTexture=false,foreignLock=false;std::exception_ptr error;
    std::thread foreign([&]{
        try {
            try {d.textureRaster(p.raster);}catch(const Failure&){foreignTexture=true;}
            EngineCpuCalls cpu(entry,base);caller(cpu.registers(),presenter,p,true);
            try {cpu.invoke(publicLock);}catch(const Failure&){foreignLock=true;}
        }catch(...){error=std::current_exception();}
    });
    foreign.join();if(error)std::rethrow_exception(error);
    need(foreignTexture&&foreignLock,"Foreign thread acquired native movie ownership");
    need(d.rasterCount()==count&&rt.physicalAllocations.size()==physical&&!p.weak.expired(),"Rejected operation changed native ownership");
    same(rt,presenter,owner,"Rejected operation changed presenter fields");
    same(rt,provider,allocationOwner,"Rejected operation changed allocation provider fields");
    same(rt,f.allocation,f.published,"Rejected operation changed descriptor/context publication");readback(rt,p);
}

void lockedRejections(Runtime& rt,const PPCContext& entry,uint32_t presenter,const Frame& f,Plane& p) {
    auto* base=rt.base;PPC_STORE_U32(presenter+0x14,f.address);
    rejectedPublic(rt,entry,presenter,p,true,[](PPCContext&){},"Double movie lock accepted");
    for(uint32_t mutation=0;mutation<4;++mutation)
        rejectedPublic(rt,entry,presenter,p,false,[&](PPCContext& c){
            switch(mutation) {case 0:c.lr+=4;break;case 1:c.r30.u64=0;break;
                case 2:++c.r29.u64;break;case 3:c.r28.u64+=4;break;}
        },"Wrong presentation unlock caller accepted");
    {
        EngineCpuCalls cpu(entry,base);rejects([&]{cpu.invoke(publicDestroy,p.raster);},"Locked movie raster destroyed without original unlock");
        need(cpu.registers().lastFunction==publicDestroy,"Locked destruction reached original plugin callbacks");
    }
    same(rt,f.allocation,f.published,"Rejected locked operation changed descriptor guards");cpuFields(rt,p,true);
}

void nonMovie(Runtime& rt,const PPCContext& entry,uint32_t presenter) {
    auto* base=rt.base;auto& d=*rt.engineDriver;const auto baseline=d.rasterCount();
    EngineCpuCalls cpu(entry,base);const auto r=cpu.invoke(0x82408130,1280,720,0,2);
    need(r&&d.rasterCount()==baseline+1,"Original non-movie camera raster setup failed");
    const auto saved=snapshot(rt,r,PPC_LOAD_U32(0x82CD1E28));Plane p;p.raster=r;
    for(bool lock:{false,true})rejectedPublic(rt,entry,presenter,p,lock,[](PPCContext&){},"Non-movie camera raster accepted movie access");
    same(rt,r,saved,"Rejected movie access changed camera raster bytes");
    need(cpu.invoke(publicDestroy,r)==1&&d.rasterCount()==baseline,"Original camera raster cleanup failed");
}

// Provider creation/retirement below uses the original movie allocator adapter.
struct Provider {uint32_t address{},allocator{},presenter{},heap{};};
Provider newProvider(Runtime& rt,const PPCContext& entry) {
    auto* base=rt.base;EngineCpuCalls cpu(entry,base);Provider p;
    // Original8282DB18 creates this 8-byte adapter, retaining its backing heap.
    // Borrow the already initialized application heap through its real getter.
    p.heap=cpu.invoke(0x8268E7F0);need(p.heap!=0,"Original application heap absent");
    p.allocator=cpu.invoke(0x8269BD70,8);need(p.allocator!=0,"Original movie allocator storage failed");
    auto before=threadAbi(cpu.registers());
    need(cpu.invoke(0x8274B1C8,p.allocator,p.heap)==p.allocator,"Original movie allocator constructor failed");
    need(threadAbi(cpu.registers())==before,"Original movie allocator constructor ABI changed");
    need(PPC_LOAD_U32(p.allocator)==0x8215094C&&PPC_LOAD_U32(p.allocator+4)==p.heap&&
        PPC_LOAD_U32(0x82150950)==0x8274B140&&PPC_LOAD_U32(0x82150958)==0x8274B1A0,
        "Movie context allocator is not the original allocate/free adapter");
    p.address=cpu.invoke(0x8269BD70,8);need(p.address!=0,"Original provider allocation failed");
    before=threadAbi(cpu.registers());need(cpu.invoke(0x8282ED68,p.address)==p.address,"Original provider constructor failed");
    cpu.invoke(0x8282E910,p.address,p.allocator); // the provider's original vtable+8 setter
    need(threadAbi(cpu.registers())==before,"Original provider construction/setter ABI changed");
    need(PPC_LOAD_U32(p.address)==0x8215D4F0&&PPC_LOAD_U32(p.address+4)==p.allocator&&
        PPC_LOAD_U32(0x8215D4F4)==0x8282E940&&PPC_LOAD_U32(0x8215D500)==0x8282EA50,
        "Original allocation provider class, allocator or virtual methods differ");
    p.presenter=cpu.invoke(0x8269BD70,0x44);need(p.presenter!=0,"Original presenter allocation failed");
    before=threadAbi(cpu.registers());need(cpu.invoke(0x8282EB00,p.presenter)==p.presenter,"Original presenter constructor failed");
    need(threadAbi(cpu.registers())==before&&p.presenter!=p.address&&PPC_LOAD_U32(p.presenter)==0x8215D4B0&&
        !PPC_LOAD_U32(p.presenter+0x14),"Presenter was confused with allocation provider or changed ABI");
    return p;
}
void releaseProvider(Runtime& rt,const PPCContext& entry,const Provider& p) {
    EngineCpuCalls cpu(entry,rt.base);const auto before=threadAbi(cpu.registers());
    need(cpu.invoke(0x8282EC08,p.presenter,1)==p.presenter,"Original presenter deleting destructor failed");
    need(cpu.invoke(0x8282ED78,p.address,1)==p.address,"Original provider deleting destructor failed");
    need(cpu.invoke(0x8274B220,p.allocator,1)==p.allocator,"Original movie allocator deleting destructor failed");
    need(threadAbi(cpu.registers())==before,"Original provider/presenter/allocator teardown ABI changed");
    need(cpu.invoke(0x8268E7F0)==p.heap,"Movie adapter teardown changed the application heap identity");
}

void profile(Runtime& rt,const PPCContext& entry,uint32_t width,uint32_t height,uint32_t crossfade,
    std::set<uint32_t>& identities) {
    auto* base=rt.base;auto& d=*rt.engineDriver;const auto baseline=d.rasterCount();
    const auto provider=newProvider(rt,entry);nonMovie(rt,entry,provider.presenter);
    const auto physicalBaseline=rt.physicalAllocations.size();
    const auto resetList=snapshot(rt,0x82D0D01C,4),attachments=snapshot(rt,0x82D0CF58,20);
    const auto crossfadeOwner=PPC_LOAD_U32(0x82D09850);
    const auto fadeOwner=snapshot(rt,crossfadeOwner,24),fadeRaster=snapshot(rt,crossfade,PPC_LOAD_U32(0x82CD1E28));
    const auto fadeTexture=d.textureRaster(crossfade);
    std::array<Frame,3> frames;std::array<std::array<Plane,3>,3> planes;
    std::set<uint32_t> rasters,contexts,pointers;bool padded=false;
    for(uint32_t j=0;j<3;++j) {
        auto& f=frames[j];f=newFrame(rt,entry,j,std::array<uint8_t,3>{0xCD,0xFF,0x5A}[j]);
        EngineCpuCalls cpu(entry,base);const auto before=threadAbi(cpu.registers());
        cpu.registers().r8.u64=3;
        cpu.invoke(0x8282E940,provider.address,f.address,width,height,0); // no specified r3 result
        need(threadAbi(cpu.registers())==before,"Original three-plane allocator changed thread/nonvolatile ABI");
        need(d.rasterCount()==baseline+3*(j+1),"Original frame allocation did not add exactly three rasters");
        auto expected=f.initialized;put(expected,guardBytes+0x28,width);put(expected,guardBytes+0x2C,height);
        put(expected,guardBytes+0x30,0);put(expected,guardBytes+0x38,3);
        for(uint32_t i=0;i<3;++i) {
            auto& p=planes[j][i];p=planeAt(rt,f,i,width,height);padded|=p.pitch>p.width;
            need(rasters.insert(p.raster).second&&contexts.insert(p.context).second&&pointers.insert(p.data).second&&
                identities.insert(p.identity).second,"Movie frames/planes alias a live owner or reuse a native identity");
            put(expected,guardBytes+4+4*i,p.data);put(expected,guardBytes+0x10+4*i,p.width*p.height);
            put(expected,guardBytes+0x1C+4*i,p.pitch);put(expected,guardBytes+0x44+4*i,p.context);
        }
        need(planes[j][1].pitch==planes[j][2].pitch,"Original decoder shares one pitch for both chroma planes");
        same(rt,f.allocation,expected,"Original plane allocation changed unrelated descriptor bytes/guards");f.published=std::move(expected);
    }
    need(d.rasterCount()==baseline+9&&rasters.size()==9&&pointers.size()==9,
        "Three frames did not retain nine simultaneous native planes/distinct CPU blocks");
    need(rt.physicalAllocations.size()==physicalBaseline,"Movie CPU blocks acquired extra physical allocations outside the original pool");
    need(padded,"Movie fixtures did not exercise any padded rows");
    for(uint32_t j=0;j<3;++j)for(auto& p:planes[j])for(uint32_t k=0;k<3;++k)for(const auto& q:planes[k])if(p.data!=q.data)
        need(uint64_t(p.data)+p.spanBytes<=q.data||
            uint64_t(q.data)+q.spanBytes<=p.data,"Different movie planes overlap CPU byte spans");
    for(uint32_t version=0;version<3;++version) {
        stage="CPU writes and original public unlock/upload";
        for(uint32_t j=0;j<3;++j)for(auto& p:planes[j])writePlane(rt,p,j,version);
        lockedRejections(rt,entry,provider.presenter,frames[0],planes[0][0]);
        // Upload all nine before checking any image: later uploads must not
        // overwrite an earlier plane's native backing or cached CPU pointer.
        for(uint32_t j=0;j<3;++j)for(auto& p:planes[j])access(rt,entry,provider.presenter,frames[j],p,false);
        for(auto& frame:planes)for(auto& p:frame)readback(rt,p);
        if(!version)rejections(rt,entry,provider.presenter,provider.address,frames[0],planes[0][0],crossfade);
        need(d.rasterCount()==baseline+9,"Upload cycle changed native plane ownership count");
        need(rt.physicalAllocations.size()==physicalBaseline,"Upload/relock cycle changed the existing physical pool count");
        same(rt,crossfadeOwner,fadeOwner,"Movie access changed crossfade ownership");same(rt,crossfade,fadeRaster,"Movie access changed crossfade raster");
        need(d.textureRaster(crossfade)==fadeTexture,"Movie access replaced crossfade native backing");
        if(version!=2) {
            stage="original public relock and stable cached bytes";
            for(uint32_t j=0;j<3;++j)for(auto& p:planes[j]) {
                need(!p.weak.expired(),"Unlocked movie backing lost its native owner");
                access(rt,entry,provider.presenter,frames[j],p,true);
                need(!p.weak.expired(),"Relock discarded native texture ownership");
            }
        }
    }
    // Exercise all-unlocked, all-locked and mixed descriptors through the real
    // 8282EA50 method. It owns RasterContext free and public raster destruction.
    for(auto& p:planes[0])access(rt,entry,provider.presenter,frames[0],p,true);
    for(uint32_t i:{0u,2u})access(rt,entry,provider.presenter,frames[2],planes[2][i],true);
    stage="original frame retirement";uint32_t retired=0;std::array<bool,3> retiredFrames{};
    for(uint32_t j:{1u,0u,2u}) {
        auto& f=frames[j];auto expected=f.published;
        for(uint32_t i=0;i<3;++i)for(uint32_t offset:{4u,0x10u,0x44u})put(expected,guardBytes+offset+4*i,0);
        put(expected,guardBytes+0x28,0);put(expected,guardBytes+0x2C,0);put(expected,guardBytes+0x30,4);
        {
            EngineCpuCalls cpu(entry,base);const auto before=threadAbi(cpu.registers());cpu.invoke(0x8282EA50,provider.address,f.address);
            need(threadAbi(cpu.registers())==before,"Original deleting frame method changed thread/nonvolatile ABI");
        }
        ++retired;same(rt,f.allocation,expected,"Original deleting frame changed retained pitch/count/magic/spare bytes/guards");
        need(d.rasterCount()==baseline+9-3*retired,"Original frame retirement retained or over-released native raster owners");
        need(rt.physicalAllocations.size()==physicalBaseline,"Original frame retirement changed the shared physical pool count");
        retiredFrames[j]=true;
        for(auto& p:planes[j]) {
            need(p.weak.expired(),"Original frame retirement retained native R8 backing");
            // Pooled free8238EB00(pointer,B1800000) may leave these addresses
            // mapped and reusable. Do not dereference retired CPU/context/raster
            // bytes or infer pool-block release from physical-allocation counts.
            rejects([&]{d.textureRaster(p.raster);},"Retired native plane texture accepted");
            rejects([&]{d.readbackTextureRaster(p.raster);},"Retired native plane readback accepted");
            for(bool lock:{false,true})rejectedPublic(rt,entry,provider.presenter,p,lock,[](PPCContext&){},"Stale movie raster accepted public access");
            EngineCpuCalls cpu(entry,base);rejects([&]{cpu.invoke(publicDestroy,p.raster);},"Stale movie raster accepted destruction");
        }
        for(uint32_t k=0;k<3;++k)if(!retiredFrames[k]) {
            same(rt,frames[k].allocation,frames[k].published,"Frame retirement damaged another live descriptor/guards");
            for(const auto& p:planes[k]) {
                need(!p.weak.expired(),"Frame retirement released another live native plane");
                cpuFields(rt,p,k==0||(k==2&&p.index!=1));
            }
        }
        same(rt,crossfadeOwner,fadeOwner,"Movie retirement changed crossfade owner");same(rt,crossfade,fadeRaster,"Movie retirement changed crossfade raster");
        need(d.textureRaster(crossfade)==fadeTexture,"Movie retirement released crossfade native backing");
        EngineCpuCalls cpu(entry,base);cpu.invoke(0x8269BEB0,f.allocation);
    }
    same(rt,0x82D0D01C,resetList,"Movie lifecycle changed unrelated raster reset list");
    same(rt,0x82D0CF58,attachments,"Movie lifecycle changed driver attachments");releaseProvider(rt,entry,provider);
    need(d.rasterCount()==baseline,"Movie profile retained native raster owners");
    need(rt.physicalAllocations.size()==physicalBaseline,"Movie profile changed the shared physical pool count");
    // Native raster counts and weak R8 references prove native resource release.
    // Unchanged physical pools alone do not prove absence of original heap leaks.
    std::printf("Movie plane profile %ux%u: three frames/nine distinct R8 planes, three uploads per plane, locked/unlocked/mixed retirement\n",width,height);
}

void lifecycle(Runtime& rt,const PPCContext& entry) {
    auto* base=rt.base;auto& d=*rt.engineDriver;const auto baseline=d.rasterCount();
    const auto messages=snapshot(rt,0x82D6C064,16),list=snapshot(rt,0x82D0D01C,4);
    uint32_t owner{};std::weak_ptr<Graphics::Texture> weak;
    {
        EngineCpuCalls cpu(entry,base);owner=cpu.invoke(0x8269BD70,24);need(owner!=0,"Original crossfade allocation failed");
        const auto before=threadAbi(cpu.registers());need(cpu.invoke(0x82702028,owner)==owner,"Original live crossfade constructor failed");
        need(threadAbi(cpu.registers())==before,"Original crossfade constructor ABI changed");
    }
    const auto raster=PPC_LOAD_U32(owner+0xC);need(PPC_LOAD_U32(0x82D09850)==owner&&d.rasterCount()==baseline+1,"Original crossfade was not retained");
    weak=d.textureRaster(raster);std::set<uint32_t> identities{PPC_LOAD_U32(raster+PPC_LOAD_U32(0x82E3DC94))};
    stage="1280x720 original plane allocation";profile(rt,entry,1280,720,raster,identities);
    stage="640x480 original plane allocation";profile(rt,entry,640,480,raster,identities);
    need(identities.size()==19&&!weak.expired(),"Movie profiles reused native identity or released crossfade early");
    {
        EngineCpuCalls cpu(entry,base);const auto before=threadAbi(cpu.registers());
        need(cpu.invoke(0x82702348,owner,1)==owner,"Original crossfade deleting destructor failed");
        need(threadAbi(cpu.registers())==before,"Original crossfade deleting destructor ABI changed");
    }
    need(weak.expired()&&!PPC_LOAD_U32(0x82D09850)&&d.rasterCount()==baseline,"Final crossfade retirement retained native ownership");
    same(rt,0x82D6C064,messages,"Paired lifecycle changed original message ownership");same(rt,0x82D0D01C,list,"Paired lifecycle changed reset-list ownership");
}

struct MovieAbi {
    ThreadAbi thread;
    std::array<uint64_t,18> fpr;
    std::array<uint8_t,12> condition;
    std::array<uint8_t,12*16> vectors;
    bool operator==(const MovieAbi&) const=default;
};
MovieAbi movieAbi(const PPCContext& c) {
    MovieAbi result{threadAbi(c),{c.f14.u64,c.f15.u64,c.f16.u64,c.f17.u64,c.f18.u64,c.f19.u64,
        c.f20.u64,c.f21.u64,c.f22.u64,c.f23.u64,c.f24.u64,c.f25.u64,c.f26.u64,c.f27.u64,
        c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64},
        {c.cr2.lt,c.cr2.gt,c.cr2.eq,c.cr2.so,c.cr3.lt,c.cr3.gt,c.cr3.eq,c.cr3.so,
         c.cr4.lt,c.cr4.gt,c.cr4.eq,c.cr4.so},{}};
    const std::array<const PPCVRegister*,12> registers={&c.v20,&c.v21,&c.v22,&c.v23,&c.v24,&c.v25,
        &c.v26,&c.v27,&c.v28,&c.v29,&c.v30,&c.v31};
    for(size_t i=0;i<registers.size();++i)std::memcpy(result.vectors.data()+16*i,registers[i]->u8,16);
    return result;
}
void seedMovieAbi(PPCContext& c) {
    const std::array<PPCRegister*,18> gpr={&c.r14,&c.r15,&c.r16,&c.r17,&c.r18,&c.r19,&c.r20,&c.r21,&c.r22,
        &c.r23,&c.r24,&c.r25,&c.r26,&c.r27,&c.r28,&c.r29,&c.r30,&c.r31};
    const std::array<PPCRegister*,18> fpr={&c.f14,&c.f15,&c.f16,&c.f17,&c.f18,&c.f19,&c.f20,&c.f21,&c.f22,
        &c.f23,&c.f24,&c.f25,&c.f26,&c.f27,&c.f28,&c.f29,&c.f30,&c.f31};
    for(size_t i=0;i<gpr.size();++i) {gpr[i]->u64=0x1234567800000000ull+i;fpr[i]->u64=0x3FF0000000000000ull+i;}
    c.cr2={1,0,0,{1}};c.cr3={0,1,0,{0}};c.cr4={0,0,1,{1}};
    const std::array<PPCVRegister*,12> vectors={&c.v20,&c.v21,&c.v22,&c.v23,&c.v24,&c.v25,
        &c.v26,&c.v27,&c.v28,&c.v29,&c.v30,&c.v31};
    for(size_t i=0;i<vectors.size();++i)for(size_t j=0;j<16;++j)vectors[i]->u8[j]=uint8_t(17*i+3*j);
    c.lr=0x12345678;
}

void sameMovieState(const Graphics::EngineState& actual,const Graphics::EngineState& before,bool success) {
    need(actual.initialized()==before.initialized(),"Movie changed effective state readiness");
    for(const auto& field:Graphics::scalarStateEvidence()) {
        // Independent original8282E404/E410/E41C/E428 setter requests.
        const auto expected=success&&field.id==0x144?1u:
            success&&(field.id==0x38||field.id==0x28||field.id==0x60)?0u:before.scalar(field.id);
        need(actual.scalar(field.id)==expected,"Movie changed an unrelated/inherited scalar or failed its original write");
    }
    for(uint32_t i=0;i<16;++i)for(const auto& field:Graphics::samplerStateEvidence()) {
        const auto expected=success&&i<3&&(field.id==0x10||field.id==0x14)?1u:before.sampler(i,field.id);
        need(actual.sampler(i,field.id)==expected,"Movie changed an unrelated sampler field/stage or lost linear filtering");
    }
    for(uint32_t i=0;i<4;++i)
        need(actual.effectiveBlend(i)==(success&&!i?0x10001u:before.effectiveBlend(i)),"Movie packed blend publication differs");
}
bool sameCamera(const NativeCameraBinding& a,const NativeCameraBinding& b) {
    return a.camera==b.camera&&a.colorRaster==b.colorRaster&&a.depthRaster==b.depthRaster&&
        a.colorIdentity==b.colorIdentity&&a.depthIdentity==b.depthIdentity&&a.viewport==b.viewport;
}
struct MovieDrawSnapshot {
    Graphics::EngineState state;
    NativeCameraBinding camera;
    std::array<uint64_t,7> counters;
    size_t rasters,physical;
    std::array<uint32_t,3> shaderCaches;
    std::vector<std::pair<uint32_t,std::vector<uint8_t>>> memory;
    std::vector<uint8_t> color,depth;
};
std::array<uint64_t,7> movieCounters(EngineDriver& d) {
    return {d.movieDrawCount(),d.screenDrawCount(),d.im2DDrawCount(),d.bindingResetCount(),
        d.cameraClearCount(),d.cameraCopyCount(),d.presentationCount()};
}
MovieDrawSnapshot drawSnapshot(Runtime& rt,const Provider& provider,const Frame& frame,const std::array<Plane,3>& planes,
    bool pixels) {
    auto* base=rt.base;auto& d=*rt.engineDriver;
    MovieDrawSnapshot result{d.effectiveState(),d.cameraBinding(),movieCounters(d),d.rasterCount(),
        rt.physicalAllocations.size(),{PPC_LOAD_U32(0x82CD1A68),PPC_LOAD_U32(0x82CD1A6C),PPC_LOAD_U32(0x82CD1A70)},{},{},{}};
    auto capture=[&](uint32_t address,uint32_t size){result.memory.emplace_back(address,snapshot(rt,address,size));};
    // Original movie never touches these application/RW/cache/stream fields.
    capture(0x82D5DB78,0x41D4);capture(0x82D0D170,0x2FBC);capture(0x82E3D160,0xB24);
    capture(0x82D501E0,0x140);capture(0x82D0CAB0,0x40);capture(0x82D0CF58,20);
    capture(0x82D10114,8);capture(0x82CD1A64,4);
    capture(PPC_LOAD_U32(0x82D0CA68),4);capture(0x82E3DD60,4);capture(0x82D0CB1C,4);
    capture(provider.presenter,0x44);capture(provider.address,8);capture(provider.allocator,8);
    capture(frame.allocation,frameBytes+2*guardBytes);
    for(const auto& p:planes) {capture(p.raster,PPC_LOAD_U32(0x82CD1E28));capture(p.context,12);}
    if(pixels) {result.color=d.readbackColor(result.camera.colorIdentity);result.depth=d.readbackDepth(result.camera.depthIdentity);}
    return result;
}
void sameDrawSnapshot(Runtime& rt,const MovieDrawSnapshot& before,bool success) {
    auto* base=rt.base;auto& d=*rt.engineDriver;sameMovieState(d.effectiveState(),before.state,success);
    need(sameCamera(d.cameraBinding(),before.camera),"Movie changed selected camera/attachments/logical viewport");
    auto expected=before.counters;if(success)++expected[0];
    need(movieCounters(d)==expected,"Movie changed another draw/clear/copy/present counter or submitted a rejected draw");
    need(d.rasterCount()==before.rasters&&rt.physicalAllocations.size()==before.physical,"Movie changed raster/pool ownership");
    for(const auto& [address,bytes]:before.memory)same(rt,address,bytes,"Movie changed unrelated CPU state or plane ownership metadata");
    const std::array<uint32_t,3> expectedCaches=success?
        std::array<uint32_t,3>{PPC_LOAD_U32(0x82DFEB34),PPC_LOAD_U32(0x82CF2340),PPC_LOAD_U32(0x82CF2328)}:before.shaderCaches;
    need(std::array<uint32_t,3>{PPC_LOAD_U32(0x82CD1A68),PPC_LOAD_U32(0x82CD1A6C),PPC_LOAD_U32(0x82CD1A70)}==expectedCaches,
        "Movie declaration/VS/PS cache publication differs");
    if(!before.depth.empty())need(d.readbackDepth(before.camera.depthIdentity)==before.depth,"Movie changed depth/stencil storage");
    if(!success&&!before.color.empty())need(d.readbackColor(before.camera.colorIdentity)==before.color,"Rejected movie changed color storage");
}

void movieDrawCaller(PPCContext& c,uint32_t presenter,uint32_t descriptor,uint8_t frameByte) {
    c.r3.u64=presenter;c.r31.u64=presenter;c.r4.u64=frameByte;c.r27.u64=frameByte;
    c.r30.u64=descriptor;c.r28.u64=descriptor+0x50;c.r29.u64=3;c.lr=0x8282ED60;
}
template<class F> void rejectedMovieDraw(Runtime& rt,const PPCContext& entry,const Provider& provider,
    const Frame& frame,const std::array<Plane,3>& planes,F&& mutation,const char* expectedReason) {
    auto* base=rt.base;EngineCpuCalls cpu(entry,base);
    movieDrawCaller(cpu.registers(),provider.presenter,frame.address,PPC_LOAD_U8(frame.address+0x51));
    // Negative inputs isolate the downstream guard under the recovered frame
    // shape. This injected header never proves a positive original draw; all
    // positive/recovery coverage executes the full 8282EC58 producer instead.
    const auto negativeStack=cpu.registers().r1.u32,priorBackchain=PPC_LOAD_U32(negativeStack);
    PPC_STORE_U32(negativeStack,negativeStack+0x80);
    mutation(cpu.registers());const auto before=drawSnapshot(rt,provider,frame,planes,true);
    const auto savedAbi=movieAbi(cpu.registers());bool rejected=false;
    try {cpu.invoke(0x8282E3E8);}catch(const std::exception& e) {
        PPC_STORE_U32(negativeStack,priorBackchain);
        rejected=true;need(std::string(e.what()).find(expectedReason)!=std::string::npos,"Movie rejected at an unintended earlier gate");
    }
    PPC_STORE_U32(negativeStack,priorBackchain);
    need(rejected,"Unqualified movie draw accepted");
    need(movieAbi(cpu.registers())==savedAbi,"Rejected movie draw changed original nonvolatile/return ABI");
    sameDrawSnapshot(rt,before,false);
}

void integrationUniform(Runtime& rt,Plane& plane,uint8_t value) {
    auto* data=rt.pointer(plane.data,plane.spanBytes,true);
    std::memset(data,0xD5,plane.spanBytes);plane.expected.assign(size_t(plane.width)*plane.height,value);
    for(uint32_t y=0;y<plane.height;++y)std::memset(data+size_t(y)*plane.pitch,value,plane.width);
    plane.staging=snapshot(rt,plane.data,plane.spanBytes);
}
void integrationPixels(Runtime& rt,uint32_t colorIdentity,uint32_t expected) {
    const auto pixels=rt.engineDriver->readbackColor(colorIdentity);
    need(pixels.size()==1280*720*4,"Movie color readback size differs");
    for(size_t at=0;at<pixels.size();at+=4) {
        uint32_t actual{};std::memcpy(&actual,pixels.data()+at,4);
        if(actual!=expected) {std::fprintf(stderr,"Movie integration pixel%zu got%08X expected%08X\n",at/4,actual,expected);
            need(false,"Movie did not cover the complete target with the original plane order/alpha");}
    }
    ++checks;
}

void movieInputState(Runtime& rt) {
    auto* base=rt.base;auto& d=*rt.engineDriver;
    d.directScalar(base,0x144,0);d.directScalar(base,0x38,2);d.directScalar(base,0x28,1);d.directScalar(base,0x60,1);
    // Real public effective-state requests, not injected readiness. Resolve
    // the original application's selectors from its immutable SDK ID table.
    for(uint32_t id:{0x10u,0x14u}) {
        uint32_t selector=0;
        for(uint32_t i=1;i<=20;++i)if(PPC_LOAD_U32(0x821506E0+4*i)==id)selector=i;
        need(selector!=0,"Original min/mag application selector absent");
        for(uint32_t i=0;i<16;++i)d.applicationSampler(base,0x82D5DB78,i,selector,0,true);
    }
    // Distinct REAL original flat owners expose premature draw-cache writes,
    // even after an earlier successful movie draw has selected the movie trio.
    // These are CPU-cache fixture inputs; no native shader object is invented.
    need(PPC_LOAD_U32(0x82DFEB30)&&PPC_LOAD_U32(0x82CF231C)&&PPC_LOAD_U32(0x82CF2310),
        "Original flat shader/declaration owners absent for cache sentinels");
    PPC_STORE_U32(0x82CD1A68,PPC_LOAD_U32(0x82DFEB30));
    PPC_STORE_U32(0x82CD1A6C,PPC_LOAD_U32(0x82CF231C));PPC_STORE_U32(0x82CD1A70,PPC_LOAD_U32(0x82CF2310));
}

void originalMovieDrawPins(Runtime& rt) {
    auto* base=rt.base;
    constexpr std::array<std::array<uint32_t,2>,22> pins={{
        {0x8282EC58,0x7D8802A6},{0x8282EC5C,0x4820D769},{0x8282EC60,0x9421FF80},
        {0x8282EC80,0x8B7E0051},{0x8282EC88,0x917F0008},{0x8282ECA0,0x917F000C},
        {0x8282ECAC,0x917F0010},{0x8282ECE4,0x4BBD9525},{0x8282ED18,0x4E800421},
        {0x8282ED1C,0x93DF0014},{0x8282ED3C,0x4BBD8E6D},{0x8282ED54,0x7F64DB78},
        {0x8282ED58,0x7FE3FB78},{0x8282ED5C,0x4BFFF68D},{0x8282ED60,0x38210080},
        {0x8282ED64,0x4820D6B0},{0x8282E3F8,0x38800001},{0x8282E404,0x4BC0D315},
        {0x8282E450,0x908B1A6C},{0x8282E468,0x908B1A70},{0x8282E540,0x908B1A68},
        {0x82152B7C,0x00000118}, // Original movie PS metadata differs from ordinary screen PS.
    }};
    for(const auto& pin:pins)need(PPC_LOAD_U32(pin[0])==pin[1],"Original movie presenter/draw code/data pin differs");
    need(PPC_LOAD_U32(0x82CF2344)==0x82152880&&PPC_LOAD_U32(0x82CF232C)==0x82152B68,
        "Original movie shared shader source-list identities differ");
}

void integrationRejections(Runtime& rt,const PPCContext& entry,const Provider& provider,
    const Frame& frame,std::array<Plane,3>& planes,const Frame& other,const std::array<Plane,3>& otherPlanes) {
    auto* base=rt.base;auto& d=*rt.engineDriver;const auto noChange=[](PPCContext&){};
    movieInputState(rt);
    stage="movie draw ABI rejection";
    for(uint32_t change=0;change<8;++change)
        rejectedMovieDraw(rt,entry,provider,frame,planes,[&](PPCContext& c){
            switch(change) {
                case 0:c.lr+=4;break;case 1:c.r1.u64-=8;break;case 2:c.r3.u64=provider.address;break;
                case 3:c.r4.u64^=1;break;case 4:c.r27.u64^=1;break;case 5:c.r29.u64=2;break;
                case 6:c.r28.u64+=4;break;case 7:c.r31.u64=provider.address;break;
            }
        },"presenter-call ABI");
    rejectedMovieDraw(rt,entry,provider,frame,planes,[&](PPCContext& c){PPC_STORE_U32(c.r1.u32,c.r1.u32+0x100);},
        "full original presenter frame");
    // A consistent wrong caller descriptor gets past the initial register gate.
    rejectedMovieDraw(rt,entry,provider,frame,planes,[&](PPCContext& c){c.r30.u64=other.address;c.r28.u64=other.address+0x50;},
        "committed descriptor");
    rejectedMovieDraw(rt,entry,provider,frame,planes,[&](PPCContext& c){c.r4.u64=c.r27.u64=uint8_t(PPC_LOAD_U8(frame.address+0x51)^1);},
        "committed descriptor");

    stage="movie frame snapshot rejection";
    // All changes are temporary fixture-memory mutations. No readiness or
    // replacement shader object is fabricated; capture AFTER each mutation so
    // even the rejected input bytes themselves must remain untouched by draw.
    auto changedWord=[&](uint32_t at,uint32_t value,const char* reason) {
        const auto old=PPC_LOAD_U32(at);PPC_STORE_U32(at,value);
        try {
            rejectedMovieDraw(rt,entry,provider,frame,planes,noChange,reason);
            need(PPC_LOAD_U32(at)==value,"Rejected movie draw changed the injected metadata/shader word");
        }
        catch(...) {PPC_STORE_U32(at,old);throw;}
        PPC_STORE_U32(at,old);
    };
    changedWord(provider.presenter,0x8215D4F0,"presenter or active construction");
    changedWord(provider.presenter+0x14,other.address,"plane is stale");
    changedWord(frame.address,0,"descriptor format/count");
    changedWord(frame.address+0x30,1,"descriptor format/count");
    changedWord(frame.address+0x38,2,"descriptor format/count");
    changedWord(frame.address+0x28,1281,"publication changed");
    changedWord(frame.address+0x2C,721,"publication changed");
    for(uint32_t i=0;i<3;++i) {
        changedWord(provider.presenter+8+4*i,0,"raster has no native owner");
        changedWord(provider.presenter+8+4*i,planes[(i+1)%3].raster,"plane is stale");
        changedWord(provider.presenter+8+4*i,otherPlanes[i].raster,"plane is stale");
        changedWord(frame.address+0x44+4*i,otherPlanes[i].context,"publication changed");
        changedWord(frame.address+0x1C+4*i,planes[i].pitch+4,"publication changed");
        changedWord(planes[i].context+4,planes[i].data+4,"publication changed");
        changedWord(planes[i].raster+PPC_LOAD_U32(0x82E3DC94),otherPlanes[i].identity,"raster metadata changed");
    }
    // Real relock through the original wrapper: native ownership still exists,
    // but a CPU-locked plane cannot participate in a new draw snapshot.
    for(auto& p:planes) {
        access(rt,entry,provider.presenter,frame,p,true);
        rejectedMovieDraw(rt,entry,provider,frame,planes,noChange,"plane is stale");
        access(rt,entry,provider.presenter,frame,p,false);readback(rt,p);
    }
    stage="movie draw resource/state rejection";
    changedWord(0x82E3DD60,0,"active full-size original camera");
    changedWord(0x82DFEB34,0,"declaration lacks its native creation owner");
    // Corrupt then restore the original CPU-created objects, never synthesize
    // a header/code allocation. These validations precede draw-side mutation.
    for(const auto slot:std::array<std::array<uint32_t,3>,2>{{{0x82CF2340,0x368,0x20},{0x82CF2328,0x28,0x18}}}) {
        const auto object=PPC_LOAD_U32(slot[0]),code=PPC_LOAD_U32(object+slot[2]);
        changedWord(object+slot[1],PPC_LOAD_U32(object+slot[1])^1,"header/code bytes differ");
        changedWord(code,PPC_LOAD_U32(code)^1,"header/code bytes differ");
    }
    for(const auto field:std::array<std::array<uint32_t,2>,4>{{{0x6C,1},{0xD4,7},{0x158,0x40000000},{0x134,1}}}) {
        const auto old=d.effectiveState().scalar(field[0]);d.directScalar(base,field[0],field[1]);
        try {rejectedMovieDraw(rt,entry,provider,frame,planes,noChange,"Native movie");}
        catch(...) {d.directScalar(base,field[0],old);throw;}
        d.directScalar(base,field[0],old);
    }
    // Foreign-thread rejection must precede even the snapshot lookup. Do not
    // construct EngineCpuCalls there (it would write a fixture stack frame).
    EngineCpuCalls cpu(entry,base);movieDrawCaller(cpu.registers(),provider.presenter,frame.address,PPC_LOAD_U8(frame.address+0x51));
    const auto before=drawSnapshot(rt,provider,frame,planes,true);auto foreign=cpu.registers();const auto foreignAbi=movieAbi(foreign);
    bool rejected=false;std::exception_ptr error;
    std::thread worker([&]{try {d.drawMovie(foreign,base);}catch(const Failure&){rejected=true;}catch(...){error=std::current_exception();}});
    worker.join();if(error)std::rethrow_exception(error);
    need(rejected&&movieAbi(foreign)==foreignAbi,"Foreign movie draw did not reject before ABI mutation");sameDrawSnapshot(rt,before,false);
}

void movieIntegration(Runtime& rt,const PPCContext& entry) {
    auto* base=rt.base;auto& d=*rt.engineDriver;originalMovieDrawPins(rt);
    need(d.submissionConfigured()&&PPC_LOAD_U32(0x82DFEB34)&&PPC_LOAD_U32(0x82CF2340)&&
        PPC_LOAD_U32(0x82CF2328)&&PPC_LOAD_U32(0x82DFE354)&&PPC_LOAD_U32(0x82DFE358),
        "Original pre-FX startup lacks real shared shader/declaration/submission owners");
    const auto baseline=d.rasterCount();const auto provider=newProvider(rt,entry);
    need(!PPC_LOAD_U32(provider.presenter+0x14)&&!PPC_LOAD_U32(provider.presenter+0x18),
        "Original fresh presenter has unexpected current frame/return callback");
    EngineCpuCalls setup(entry,base);const auto camera=PPC_LOAD_U32(0x82E07248);
    need(setup.invoke(0x823F1A18,camera)==camera,"Original movie camera begin failed");
    // Same original pipeline scope as test_original_screen_bridge; storage for
    // its stack object comes from the initialized original general allocator.
    const auto scope=setup.invoke(0x8269BD70,0x40);need(scope!=0,"Original pipeline-scope allocation failed");
    setup.invoke(0x826B09A0,scope,0);
    d.directScalar(base,0x158,0x3F800000);d.directScalar(base,0x15C,0x3F800000);
    // Establish the same actual viewport as the preceding Im2D draw in boot152.
    // Ordinary screen quads temporarily install their viewport then restore it,
    // so they cannot initialize an absent native viewport at this early fixture.
    need(setup.invoke(0x824025A8,1,0)==1,"Original Im2D null raster selection failed");setup.invoke(0x82400040);
    const auto vertices=setup.invoke(0x8269BD70,112);need(vertices!=0,"Original Im2D input allocation failed");
    for(uint32_t i=0;i<4;++i) {
        const auto row=vertices+28*i;
        PPC_STORE_U32(row,std::bit_cast<uint32_t>(i>=2?24.5f:8.5f));
        PPC_STORE_U32(row+4,std::bit_cast<uint32_t>(i&1?24.5f:8.5f));
        PPC_STORE_U32(row+8,0);PPC_STORE_U32(row+12,0x3F800000);PPC_STORE_U32(row+16,0xFF000000);
        PPC_STORE_U32(row+20,0);PPC_STORE_U32(row+24,0);
    }
    d.directScalar(base,0x134,1);
    for(auto [id,value]:std::array<std::pair<uint32_t,uint32_t>,10>{{{0x28,0},{0x30,0},{0x2C,7},
        {0x38,2},{0x3C,1},{0x60,1},{0x64,0},{0x68,4},{0x48,6},{0x4C,7}}})setup.invoke(0x82400170,id,value);
    const auto im2dBefore=d.im2DDrawCount();need(setup.invoke(0x82409308,4,vertices,4)==1,"Original Im2D setup draw failed");
    need(d.im2DDrawCount()==im2dBefore+1,"Original Im2D did not establish its native viewport");
    d.directScalar(base,0x134,0);setup.invoke(0x8269BEB0,vertices);
    std::array<Frame,4> frames;std::array<std::array<Plane,3>,4> planes;
    for(uint32_t j=0;j<4;++j) {
        stage="original movie integration allocation";const uint32_t width=j<2?1280:640,height=j<2?720:480;
        auto& f=frames[j];f=newFrame(rt,entry,j,0xCD);EngineCpuCalls cpu(entry,base);cpu.registers().r8.u64=3;
        cpu.invoke(0x8282E940,provider.address,f.address,width,height,0);
        auto expected=f.initialized;put(expected,guardBytes+0x28,width);put(expected,guardBytes+0x2C,height);
        put(expected,guardBytes+0x30,0);put(expected,guardBytes+0x38,3);
        for(uint32_t i=0;i<3;++i) {
            auto& p=planes[j][i];p=planeAt(rt,f,i,width,height);
            put(expected,guardBytes+4+4*i,p.data);put(expected,guardBytes+0x10+4*i,p.width*p.height);
            put(expected,guardBytes+0x1C+4*i,p.pitch);put(expected,guardBytes+0x44+4*i,p.context);
            // First frame reproduces the reported decoded boot150 constant
            // image. Later frames distinguish index1 Cb=37 from index2 Cr=211.
            integrationUniform(rt,p,j?std::array<uint8_t,3>{93,37,211}[i]:std::array<uint8_t,3>{16,128,128}[i]);
        }
        // Original frame+51 and presenter+41 are separate inputs. Use a
        // nonboolean descriptor byte to exercise r4/r27 transport independently.
        PPC_STORE_U8(f.address+0x51,uint8_t(0xA0+j));expected[guardBytes+0x51]=uint8_t(0xA0+j);
        same(rt,f.allocation,expected,"Integration allocator changed original descriptor bytes/guards");f.published=std::move(expected);
    }
    need(d.rasterCount()==baseline+12,"Integration frames did not retain twelve independent plane owners");
    for(uint32_t j=0;j<4;++j) {
        stage="complete original movie presenter and return ABI";auto& f=frames[j];
        // j0 is boot150 width1280/flag1; remaining calls cover the other three branches.
        PPC_STORE_U8(provider.presenter+0x41,uint8_t(j%2?0:1));
        movieInputState(rt);
        d.directScalar(base,0x30,j&1); // Inactive depth-write request must survive the draw.
        auto before=drawSnapshot(rt,provider,f,planes[j],true);
        // Expected ORIGINAL parent publications, separate from draw mutations.
        for(auto& [address,bytes]:before.memory) {
            if(address==provider.presenter) {put(bytes,0x14,f.address);for(uint32_t i=0;i<3;++i)put(bytes,8+4*i,planes[j][i].raster);}
            for(const auto& p:planes[j])if(address==p.raster) {put(bytes,4,0);put(bytes,0x18,0);bytes[0x22]&=0xF9;}
        }
        {
            EngineCpuCalls cpu(entry,base);seedMovieAbi(cpu.registers());const auto saved=movieAbi(cpu.registers());
            cpu.invoke(0x8282EC58,provider.presenter,f.address);
            need(movieAbi(cpu.registers())==saved,"Original movie presenter did not restore caller SP/LR/nonvolatile registers");
        }
        sameDrawSnapshot(rt,before,true);
        need(PPC_LOAD_U32(provider.presenter+0x14)==f.address,"Original presenter did not commit incoming descriptor");
        for(auto& p:planes[j])readback(rt,p);
        if(j)for(const auto& p:planes[j-1])cpuFields(rt,p,true);
        // Pinned native-model packed outputs from original shader inspector
        // fixtures: Y16/Cr128/Cb128 and Y93/Cr211/Cb37, alpha zero in both.
        integrationPixels(rt,before.camera.colorIdentity,j?0x00039B7F:0x00400003);
    }
    // The final descriptor is fully unlocked; other genuine frames remain live
    // and relocked. Mutation tests cannot pass merely through missing startup.
    integrationRejections(rt,entry,provider,frames[3],planes[3],frames[0],planes[0]);
    // Recovery uses the sole original draw producer, including its real 0x80
    // frame and descriptor relock/unlock loop, rather than inventing a leaf ABI.
    {
        stage="movie draw recovery after rejection";EngineCpuCalls cpu(entry,base);
        const auto saved=movieAbi(cpu.registers());const auto before=drawSnapshot(rt,provider,frames[3],planes[3],true);
        cpu.invoke(0x8282EC58,provider.presenter,frames[3].address);
        need(movieAbi(cpu.registers())==saved,"Movie presenter recovery changed original caller ABI");
        sameDrawSnapshot(rt,before,true);integrationPixels(rt,before.camera.colorIdentity,0x00039B7F);
    }
    stage="original movie integration retirement";
    for(uint32_t j=0;j<4;++j) {
        setup.invoke(0x8282EA50,provider.address,frames[j].address);
        for(const auto& p:planes[j])need(p.weak.expired(),"Movie draw retained a retired native plane owner");
        setup.invoke(0x8269BEB0,frames[j].allocation);
    }
    // Current descriptor no longer belongs to the presenter after fixture
    // retirement. No retired pooled memory is read by subsequent assertions.
    PPC_STORE_U32(provider.presenter+0x14,0);releaseProvider(rt,entry,provider);
    need(d.rasterCount()==baseline,"Movie integration leaked raster owners");
    setup.invoke(0x826B09F0,scope);setup.invoke(0x8269BEB0,scope);
    need(setup.invoke(0x823F1A08,camera)==camera,"Original movie camera end failed");
}
}

int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(exceptionFilter);
    try {
        need(argc==2||(argc==3&&std::string(argv[2])=="--integration"),"Usage: MoviePlaneTests <original image> [--integration]");
        Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;
        try {runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original pre-FX startup boundary absent");originalContract(rt);
        if(argc==3) {
            movieIntegration(rt,entry);
            std::printf("PASS original movie integration: %zu checks; original presenter uploads/draw/return ABI, five native draws, four geometry branches, unequal chroma order, state/cache and rejection recovery; no codec/scanout/console parity claim; ALL MUTED\n",checks);
            return 0;
        }
        lifecycle(rt,entry);
        std::printf("PASS original movie-plane native lifetime: %zu checks; two profiles, 18 plane lifetimes, three upload cycles, stable cached CPU bytes, original wrappers/retirement and crossfade coexistence; no codec, movie presentation or gameplay claim; ALL MUTED\n",checks);
        return 0;
    }catch(const std::exception& e) {
        std::fprintf(stderr,"FAIL movie-plane lifetime: %zu checks stage=%s %s\n",checks,stage,e.what());return 1;
    }
}
