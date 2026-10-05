#include "engine_materials.h"
#include "runtime.h"
#include "renderer/native_material_compiler.h"
#include <atomic>
#include <cstdio>
#include <fstream>
#include "engine_particles.h"
#include "engine_driver.h"
#include <unordered_map>

namespace {
thread_local Simpsons::EngineMaterials* materialScope{};
std::atomic<uint32_t> nextMaterial{0x00E00001};
uint32_t allocateToken() {
    uint32_t id=nextMaterial.load();
    while(id<0x00F00000) if(nextMaterial.compare_exchange_weak(id,id+1)) {
        if(Simpsons::active->pageAccess[id>>12].load()) throw Simpsons::Failure("Native material token overlaps mapped guest memory");
        return id;
    }
    throw Simpsons::Failure("Native material identity space exhausted");
}
Simpsons::EngineMaterials& scope(uint8_t* base) {
    if(!materialScope || !Simpsons::active || base!=Simpsons::active->base)
        throw Simpsons::Failure("Native material service reached outside its owner scope");
    return *materialScope;
}
}
namespace Simpsons {
struct EngineMaterials::Owner {
    Graphics::MaterialRegistry records;
    Graphics::NativeMaterialCompiler compiler;
    std::unordered_map<uint32_t,Graphics::MaterialId> tokens;
    explicit Owner(Graphics::NativeBackend& backend):compiler(backend) {}
    Graphics::MaterialId find(uint32_t token) const {
        auto found=tokens.find(token);
        if(found==tokens.end()) {
            // Name the token and the live population so an intermittent stale reference can be attributed from the log alone.
            char message[160];std::snprintf(message,sizeof(message),"Unknown or stale native material identity %08X (%zu live material tokens, next %08X)",token,tokens.size(),nextMaterial.load());
            throw Failure(message);
        }
        return found->second;
    }
};
EngineMaterials::EngineMaterials(Graphics::NativeBackend& backend):owner(std::make_unique<Owner>(backend)) {
    if(materialScope) throw Failure("Nested native material owner");
    materialScope=this;
}
EngineMaterials::~EngineMaterials() {materialScope=nullptr;}
uint32_t EngineMaterials::create(uint8_t* base,uint32_t source,uint32_t output,Graphics::MaterialStage stage) {
    if(&scope(base)!=this) throw Failure("Wrong native material owner");
    // Original SDK creation invokes these optional callbacks before allocation;
    // they can reject the record. Their nonzero behavior is not yet ported.
    const uint32_t callback=stage==Graphics::MaterialStage::Vertex?0x82D51548:0x82D51544;
    if(PPC_LOAD_U32(callback)) throw Failure("Native material creation requires unsupported original shader callback");
    auto* destination=active->pointer(output,4,true);
    const Graphics::MaterialIdentity* identity=nullptr;
    for(const auto& candidate:Graphics::originalMaterialIdentities()) if(candidate.originalAddress==source) {identity=&candidate;break;}
    if(!identity || identity->stage!=stage) throw Failure("Unsupported original material record or stage");
    // Vertex creation also invalidates its original engine binding cache.
    uint8_t* vertexCache=stage==Graphics::MaterialStage::Vertex?active->pointer(0x82CD1A6C,4,true):nullptr;
    auto* bytes=active->pointer(source,identity->recordBytes,false);
    const auto record=owner->records.create(source,{bytes,identity->recordBytes});
    uint32_t token;
    try {token=allocateToken();owner->tokens.emplace(token,record);}
    catch(...) {owner->records.release(record);throw;}
    for(unsigned i=0;i<4;++i) destination[i]=uint8_t(token>>(24-8*i));
    if(vertexCache) for(unsigned i=0;i<4;++i) vertexCache[i]=0xFF;
    return token;
}
uint32_t EngineMaterials::release(uint8_t* base,uint32_t token,Graphics::MaterialStage stage) {
    if(&scope(base)!=this) throw Failure("Wrong native material owner");
    const auto record=owner->find(token);
    if(owner->records.record(record).identity().stage!=stage) throw Failure("Native material release stage mismatch");
    const uint32_t remaining=owner->records.referenceCount(record)-1;
    owner->records.release(record);
    if(!remaining) owner->tokens.erase(token);
    return remaining;
}
const Graphics::CompiledMaterial& EngineMaterials::prepare(uint32_t token,Graphics::MaterialStage stage) {
    if(materialScope!=this) throw Failure("Native material preparation outside owner thread");
    const auto record=owner->find(token);
    if(owner->records.record(record).identity().stage!=stage) throw Failure("Native material bind stage mismatch");
    return owner->records.prepareForBind(record,owner->compiler);
}
Graphics::MaterialCapability EngineMaterials::capability(uint32_t token) const {return owner->records.capability(owner->find(token));}
void EngineMaterials::requireOwned(uint32_t token,Graphics::MaterialStage stage) const {
    if(materialScope!=this || owner->records.record(owner->find(token)).identity().stage!=stage)
        throw Failure("Native material identity has the wrong owner or stage");
}
size_t EngineMaterials::liveCount() const {return owner->records.liveCount();}
void EngineMaterials::requireReleased() const {
    if(!owner->tokens.empty() || owner->records.liveCount()) throw Failure("Original material cleanup left native ownership live");
}
}
void SimpsonsNativePixelMaterialCreate(PPCContext& ctx,uint8_t* base) {
    scope(base).create(base,ctx.r3.u32,ctx.r4.u32,Simpsons::Graphics::MaterialStage::Pixel);ctx.r3.u32=1;
}
void SimpsonsNativeVertexMaterialCreate(PPCContext& ctx,uint8_t* base) {
    scope(base).create(base,ctx.r3.u32,ctx.r4.u32,Simpsons::Graphics::MaterialStage::Vertex);ctx.r3.u32=1;
}
void SimpsonsNativePixelMaterialRelease(PPCContext& ctx,uint8_t* base) {
    ctx.r3.u32=scope(base).release(base,ctx.r3.u32,Simpsons::Graphics::MaterialStage::Pixel);
}
void SimpsonsNativeVertexMaterialRelease(PPCContext& ctx,uint8_t* base) {
    ctx.r3.u32=scope(base).release(base,ctx.r3.u32,Simpsons::Graphics::MaterialStage::Vertex);
}
// These SDK entries are explicit guards, not native binding implementations.
// A native engine draw boundary must resolve its resources/state before this
// layer; native opaque IDs must never be dereferenced as console objects.
// Null-device calls (r3==0) pass through: the console device is null forever
// under the native driver, and the body executes on coherent zero-page
// scratch (reads see zeros/writes persist, no GPU side effects), taking the
// original null-tolerant paths. Audited per call below; a live device still
// rejects loudly.
void SimpsonsRejectConsoleMaterialBinding(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base) throw Simpsons::Failure("Invalid graphics binding runtime");
    if(ctx.r3.u32==0) {
        std::fprintf(stderr,"[NATIVE NULL DEVICE CALL] caller=%08X\n",uint32_t(ctx.lr));
        return;
    }
    char message[160];
    std::snprintf(message,sizeof(message),"Unimplemented native engine material/declaration binding; console SDK call from 0x%08X rejected",uint32_t(ctx.lr));
    throw Simpsons::Failure(message);
}

// The original particle engine bypasses the ordinary RenderWare draw bridge.
// Runs278/279 spun in console DrawVertices. Capture the first reached profile,
// then enter the qualified native owner before any console device writes.
void SimpsonsNativeParticlePreflight(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base)
        throw Simpsons::Failure("Invalid particle preflight runtime");
    auto& runtime=*Simpsons::active;
    const uint32_t emitter=ctx.r30.u32;
    runtime.pointer(emitter,0x134,false);
    const uint32_t count=PPC_LOAD_U16(emitter+0xF4),capacity=PPC_LOAD_U16(emitter+0xC8);
    const uint32_t definition=PPC_LOAD_U32(emitter+0x118),parameters=PPC_LOAD_U32(emitter+0x11C);
    if(!runtime.frameCaptureDirectory.empty() && !std::filesystem::exists(runtime.frameCaptureDirectory/"particle-frontier")) {
        std::fprintf(stderr,"[NATIVE PARTICLE FRONTIER] site=82772D98 emitter=%08X count=%u capacity=%u definition=%08X parameters=%08X stream=%08X\n",
            emitter,count,capacity,definition,parameters,PPC_LOAD_U32(0x82DFEB20));
        const auto directory=runtime.frameCaptureDirectory/"particle-frontier";
        std::filesystem::create_directories(directory);
        std::ofstream index(directory/"spans.json");
        if(!index)throw Simpsons::Failure("Cannot create particle evidence manifest");
        index<<"{\"emitter\":"<<emitter<<",\"count\":"<<count<<",\"capacity\":"<<capacity<<",\"spans\":[";
        bool first=true;
        auto capture=[&](const std::string& name,uint32_t address,uint32_t size) {
            const auto* bytes=runtime.pointer(address,size,false);
            std::ofstream output(directory/(name+".bin"),std::ios::binary);
            output.write(reinterpret_cast<const char*>(bytes),size);
            if(!output)throw Simpsons::Failure("Cannot write particle evidence span");
            if(!first)index<<',';first=false;
            index<<"{\"name\":\""<<name<<"\",\"address\":"<<address<<",\"bytes\":"<<size<<'}';
        };
        capture("emitter",emitter,0x134);
        capture("definition",definition,0x108);
        capture("parameters",parameters,0x90);
        capture("frame",0x82DFEA20,0x1A0);
        capture("shader-registry",0x82CF2590,0xAC);
        capture("color",ctx.r1.u32+0xA0,0x20);
        const uint32_t texture=PPC_LOAD_U32(emitter+0xD0);
        capture("texture",texture,0x58);
        const uint32_t raster=PPC_LOAD_U32(texture),plugin=PPC_LOAD_U32(0x82E3DC94);
        capture("raster",raster,0x34);
        if(plugin>=0x34 && plugin<0x1000 && uint64_t(raster)+plugin+0x20<=0x100000000ull) {
            const uint32_t header=PPC_LOAD_U32(raster+plugin);
            capture("raster-plugin",raster+plugin,0x20);
            if(header>=0x10000)capture("texture-header",header,0x40);
        }
        // Original particle storage: eight timestamps followed by eight
        // 64-byte records per bucket. Capture only bounded live CPU storage.
        if(capacity && capacity<=4096 && count<=capacity) {
            const uint32_t buckets=(capacity+7)/8;
            capture("bucket-pointers",emitter+0x130,4*buckets);
            for(uint32_t i=0;i<buckets;++i) {
                const uint32_t bucket=PPC_LOAD_U32(emitter+0x130+4*i);
                if(bucket)capture("bucket-"+std::to_string(i),bucket,0x220);
            }
        }
        index<<"]}\n";
        if(!index)throw Simpsons::Failure("Cannot finish particle evidence manifest");
    }
    if(!runtime.engineDriver)throw Simpsons::Failure("Particle has no native driver");
    runtime.engineDriver->particles().begin(ctx,base);
}
