#include "engine_loading_textures.h"
#include "engine_cpu_calls.h"
#include "engine_driver.h"
#include "renderer/native_texture_stream.h"
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace {
using Simpsons::Failure;
using Simpsons::Runtime;
constexpr uint32_t sourceAddress=0x8215F820,sourceSize=0x20150;
constexpr uint32_t nativeSize=0x10088,structSize=0x1005C,payloadSize=0x10000;
constexpr uint32_t payloadScratch=0x82D101E0,textureRegistry=0x82CD1DB8;
constexpr uint32_t stamp=0x1C02002D;
constexpr std::array<uint8_t,32> sourceHash={
    0x74,0xDD,0xB2,0x11,0xE9,0xEB,0xF1,0x02,0x5D,0xD3,0x9A,0xA5,0x63,0x2C,0xE2,0xCF,
    0xBA,0xBD,0xB6,0x7F,0xFF,0xA1,0xEE,0xCF,0xD6,0xBE,0x5C,0xA4,0xA5,0xD3,0x64,0xDD};
thread_local bool reading{};

[[noreturn]] void fail(const char* reason) {
    throw Failure(std::string("Native loading texture: ")+reason);
}
struct Scope {
    Scope() {if(reading) fail("nested loading stream callback is unsupported");reading=true;}
    ~Scope() {reading=false;}
};
uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
}
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
bool overlaps(uint32_t a,uint32_t an,uint32_t b,uint32_t bn) {
    return uint64_t(a)<uint64_t(b)+bn && uint64_t(b)<uint64_t(a)+an;
}
std::string reason(std::exception_ptr error) {
    try {std::rethrow_exception(error);}
    catch(const std::exception& e) {return e.what();}
    catch(...) {return "non-standard exception";}
}

// Stream +8/+18/+1C are not initialized by the observed memory-stream open.
// Preserve their bytes without guessing values or assigning pointer semantics.
struct StreamState {
    uint32_t address,start;
    std::array<uint8_t,0x24> bytes{};
    StreamState(Runtime& rt,uint32_t at):address(at),start{} {
        if(!at || (at&3)) fail("invalid original stream object alignment");
        std::memcpy(bytes.data(),rt.pointer(at,0x24,false),bytes.size());
        start=be32(bytes.data()+0xC);
        if(be32(bytes.data())!=3 || be32(bytes.data()+4)!=1 ||
           be32(bytes.data()+0x10)!=sourceSize || be32(bytes.data()+0x14)!=sourceAddress ||
           be32(bytes.data()+0x20)!=1 || (start!=0x28 && start!=0x100BC))
            fail("unsupported stream type/access/ownership/source/extent/position");
        rt.pointer(at+0xC,4,true);
    }
    void require(Runtime& rt,uint32_t position) const {
        const auto* now=rt.pointer(address,0x24,false);
        if(be32(now+0xC)!=position) fail("original stream cursor differs from the exact consumed extent");
        for(size_t i=0;i<bytes.size();++i)
            if((i<0xC || i>=0x10) && now[i]!=bytes[i])
                fail("original memory stream ownership fields changed");
    }
};

// Only the recovered EA2F constructor/read contract is certified here. Keep its
// actual allocation offset, registry node and all original callbacks intact.
struct TexturePlugins {
    uint32_t offset{},total{},node{};
    std::array<uint8_t,0x18> registryBytes{};
    std::array<uint8_t,0x3C> nodeBytes{};
    explicit TexturePlugins(Runtime& rt,uint8_t* base) {
        std::memcpy(registryBytes.data(),rt.pointer(textureRegistry,0x18,false),registryBytes.size());
        offset=PPC_LOAD_U32(0x82CF0600);total=be32(registryBytes.data());node=be32(registryBytes.data()+0x10);
        if(offset!=0x58 || total!=0x78 || be32(registryBytes.data()+4)!=0x58 ||
           !node || (node&3) || be32(registryBytes.data()+0x14)!=node)
            fail("unverified original texture plugin allocation or registry shape");
        std::memcpy(nodeBytes.data(),rt.pointer(node,0x3C,false),nodeBytes.size());
        // Registration 82737260 supplies constructor/copy; the registry replaces
        // its null destructor with original no-op callback 823FB010.
        constexpr std::array<uint32_t,15> expected={
            0x58,0x20,0xEA2F,0x82737190,0x827371F8,0x827370C0,0,0,
            0x82737160,0x823FB010,0x82737068,0,0,0,textureRegistry};
        for(size_t i=0;i<expected.size();++i)
            if(be32(nodeBytes.data()+i*4)!=expected[i])
                fail("unsupported texture plugin constructor/stream/destructor registration");
    }
    void require(Runtime& rt,uint8_t* base) const {
        if(PPC_LOAD_U32(0x82CF0600)!=offset ||
           std::memcmp(rt.pointer(textureRegistry,0x18,false),registryBytes.data(),registryBytes.size()) ||
           std::memcmp(rt.pointer(node,0x3C,false),nodeBytes.data(),nodeBytes.size()))
            fail("original texture plugin registration changed during construction");
    }
};

void chunk(const std::vector<uint8_t>& source,uint32_t offset,uint32_t type,uint32_t size) {
    if(uint64_t(offset)+12+size>source.size() || le32(source.data()+offset)!=type ||
       le32(source.data()+offset+4)!=size || le32(source.data()+offset+8)!=stamp)
        fail("embedded chunk structure or enclosing extent differs from the verified loading dictionary");
}
std::vector<uint8_t> snapshotSource(Runtime& rt) {
    const auto* p=rt.pointer(sourceAddress,sourceSize,false);
    std::vector<uint8_t> source(p,p+sourceSize);
    std::array<uint8_t,32> hash{};
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,source.data(),sourceSize,
                  hash.data(),static_cast<ULONG>(hash.size()))<0)
        fail("embedded dictionary SHA256 verification failed");
    if(hash!=sourceHash) fail("embedded original dictionary bytes or extensions changed");
    // Explicit parent/child checks expose the bounds contract beyond the hash.
    chunk(source,0,0x16,sourceSize-12);
    chunk(source,12,1,4);
    if(le32(source.data()+24)!=0x00090002) fail("unexpected embedded dictionary count/platform");
    for(uint32_t position:{0x28u,0x100BCu}) {
        chunk(source,position-12,0x15,nativeSize);
        chunk(source,position,1,structSize);
        const uint32_t ext=position+12+structSize;
        chunk(source,ext,3,20);chunk(source,ext+12,0xEA2F,8);
        if(ext+32!=position+nativeSize) fail("native texture extension escapes its enclosing chunk");
    }
    chunk(source,sourceSize-12,3,0);
    return source;
}
void requireSource(Runtime& rt,const std::vector<uint8_t>& source) {
    if(std::memcmp(rt.pointer(sourceAddress,sourceSize,false),source.data(),source.size()))
        fail("embedded dictionary changed after preflight");
}
}

namespace Simpsons {
bool readLoadingTexture(const PPCContext& incoming,uint8_t* base,
    Graphics::NativeBackend& backend,EngineDriver& driver,
    uint32_t stream,uint32_t output,uint32_t chunkLength) {
    if(!active || base!=active->base || active->engineDriver.get()!=&driver)
        fail("callback has no matching runtime/driver owner");
    Runtime& rt=*active;rt.checkRunning();
    if(!driver.started() || !driver.submissionConfigured()) fail("driver submission is not initialized");
    backend.validateSubmissionContext();
    if(uint32_t(incoming.lr)!=0x823FF8F4 || incoming.r3.u32!=stream || incoming.r4.u32!=output ||
       incoming.r5.u32!=chunkLength || uint64_t(incoming.r1.u32)+0x60!=output ||
       chunkLength!=nativeSize || incoming.r1.u32<0x100 || (incoming.r1.u32&15))
        fail("unsupported original dictionary callback ABI or enclosing chunk length");
    Scope scope;
    rt.pointer(output,4,true);
    const uint32_t originalOutput=PPC_LOAD_U32(output);
    const StreamState state(rt,stream);
    if(overlaps(stream,0x24,sourceAddress,sourceSize) || overlaps(stream,0x24,payloadScratch,payloadSize) ||
       overlaps(stream,0x24,incoming.r1.u32-0x100,0x164))
        fail("stream object aliases the original source, scratch or caller output/frame");
    const auto source=snapshotSource(rt);
    const TexturePlugins plugins(rt,base);
    // Bound the exact source profile before allocating either a guest object or
    // a GPU resource; extensions are validated but left to original AOT to read.
    const auto expected=Graphics::decodeNativeTextureStruct({source.data()+state.start+12,structSize});
    const char* expectedName=state.start==0x28?"frame2":"frame1";
    if(expected.name!=expectedName || !expected.maskName.empty() || expected.sampler!=0x1102 ||
       expected.width!=256 || expected.height!=256)
        fail("unsupported embedded loading texture identity/sampler/dimensions");
    rt.pointer(payloadScratch,payloadSize,true);

    EngineCpuCalls cpu(incoming,base);
    const PPCContext cleanFrame=cpu.registers();
    const uint32_t sp=cleanFrame.r1.u32,header=sp+0x60;
    rt.pointer(sp+0x50,0x6C,true); // size/version, 88-byte header and LE level size.
    uint32_t raster=0,texture=0;
    enum class Constructor {None,Raster,Texture};
    Constructor constructing=Constructor::None;
    auto frame=[&] {
        if(cpu.registers().r1.u32!=sp) fail("original helper did not restore its checked caller frame");
    };
    auto read=[&](uint32_t destination,uint32_t size,uint32_t returnPc,uint32_t endPosition) {
        cpu.registers().lr=returnPc;
        if(cpu.invoke(0x823F90C0,stream,destination,size)!=size) fail("original memory stream returned a short read");
        frame();state.require(rt,endPosition);
    };
    try {
        cpu.registers().lr=0x8240A29C;
        if(cpu.invoke(0x823F7F58,stream,1,sp+0x50,sp+0x54)!=1)
            fail("original struct-chunk search failed");
        frame();state.require(rt,state.start+12);
        if(PPC_LOAD_U32(sp+0x50)!=structSize || PPC_LOAD_U32(sp+0x54)!=0x37002)
            fail("original chunk helper returned an unexpected size/version");
        read(header,0x48,0x8240A2C8,state.start+12+0x48);
        read(header+0x48,0x10,0x8240A2EC,state.start+12+0x58);
        read(header+0x58,4,0x8240A6E0,state.start+12+0x5C);
        read(payloadScratch,payloadSize,0x8240A780,state.start+12+structSize);
        std::vector<uint8_t> serialized(structSize);
        std::memcpy(serialized.data(),rt.pointer(header,0x5C,false),0x5C);
        std::memcpy(serialized.data()+0x5C,rt.pointer(payloadScratch,payloadSize,false),payloadSize);
        if(!std::equal(serialized.begin(),serialized.end(),source.begin()+state.start+12))
            fail("original CPU stream reads differ from the preflight source");
        auto decoded=Graphics::decodeNativeTextureStruct(serialized);

        cpu.registers().lr=0x8240A338;constructing=Constructor::Raster;
        raster=cpu.invoke(0x82408130,decoded.width,decoded.height,decoded.depthField,0x384);
        constructing=Constructor::None;frame();
        if(!raster) fail("original raster constructor returned zero");
        auto backing=backend.createTexture(decoded.width,decoded.height,decoded.format,decoded.blocks);
        // Parent validates the actual device, stores genuine ownership and sets
        // the verified post-upload CPU fields. No SDK header/lock is recreated.
        driver.attachTextureRaster(raster,std::move(backing));
        cpu.registers().lr=0x8240A874;constructing=Constructor::Texture;
        texture=cpu.invoke(0x823FDE20,raster);
        constructing=Constructor::None;frame();
        if(!texture) fail("original texture constructor returned zero");
        plugins.require(rt,base);
        auto* object=rt.pointer(texture,plugins.total,true);
        if(be32(object)!=raster || be32(object+4) || be32(object+0x54)!=1 || be32(object+0x50)!=0x1101)
            fail("original texture constructor did not establish the recovered ownership/refcount/sampler");
        for(uint32_t offset=0;offset<0x20;offset+=4)
            if(be32(object+plugins.offset+offset)!=(offset==4?texture:0))
                fail("original EA2F texture constructor did not preserve its CPU side effects");
        PPC_STORE_U32(texture+0x50,decoded.sampler);
        cpu.registers().lr=0x8240A8B8;
        if(cpu.invoke(0x823FDEF0,texture,header+8)!=texture) fail("original texture name setter failed");
        frame();cpu.registers().lr=0x8240A8C4;
        if(cpu.invoke(0x823FDF88,texture,header+40)!=texture) fail("original mask-name setter failed");
        frame();
        if(std::memcmp(rt.pointer(texture+0x10,32,false),serialized.data()+8,32) ||
           std::memcmp(rt.pointer(texture+0x30,32,false),serialized.data()+40,32) ||
           PPC_LOAD_U32(texture)!=raster || PPC_LOAD_U32(texture+4) ||
           PPC_LOAD_U32(texture+0x54)!=1 || PPC_LOAD_U32(texture+0x50)!=0x1102)
            fail("original texture name/mask/ownership fields changed before publication");
        state.require(rt,state.start+12+structSize);plugins.require(rt,base);requireSource(rt,source);
        if(PPC_LOAD_U32(output)!=originalOutput) fail("caller output changed before texture publication");
        rt.checkRunning();PPC_STORE_U32(output,texture);
        return true; // Original dictionary now owns extension read/insertion.
    } catch(...) {
        const auto error=std::current_exception();
        // Cancellation must propagate; running more AOT while stopping is not
        // permitted. Parent/runtime terminal ownership remains authoritative.
        if(rt.stopping.load()) std::rethrow_exception(error);
        if(constructing!=Constructor::None)
            throw Failure("Native loading texture: original CPU constructor interrupted before ownership returned; terminal cleanup required: "+reason(error));
        try {
            // An exception can leave the helper's emulated ABI frame adjusted.
            // Restart cleanup from our known checked frame, never that partial
            // callee state. This invokes only statically translated CPU code.
            cpu.registers()=cleanFrame;
            if(raster) driver.preflightRasterDestroy(base,raster);
            if(texture) {
                if(PPC_LOAD_U32(texture)!=raster || PPC_LOAD_U32(texture+4) || PPC_LOAD_U32(texture+0x54)!=1)
                    fail("cannot roll back a texture whose raster/dictionary/reference owner changed");
                cpu.registers().lr=0x823FF790;
                if(cpu.invoke(0x823FDEC8,texture)!=1) fail("original texture rollback failed");
                texture=0;raster=0;
            } else if(raster) {
                cpu.registers().lr=0x8240A8D8;
                if(cpu.invoke(0x82407DC0,raster)!=1) fail("original raster rollback failed");
                raster=0;
            }
        } catch(...) {
            if(rt.stopping.load()) std::rethrow_exception(error);
            throw Failure("Native loading texture: "+reason(error)+"; original CPU rollback incomplete: "+reason(std::current_exception()));
        }
        std::rethrow_exception(error);
    }
}
}
