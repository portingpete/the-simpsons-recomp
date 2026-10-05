// One first initialization of the genuine original CPU FX pool, with original
// serialized row1 data. No FX object/identity, registration or manual free.
#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include "runtime/engine_effects.h"
#include "runtime/engine_audio_output.h"
#include "runtime/threads.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace Simpsons;
constexpr uint32_t blob=0x820D5730,body=blob+12,rootSlot=0x82D6D2F8,table=0x82CEFD20;
constexpr std::array<const char*,11> names={
    "g_ViewProjection","g_WorldEyePosition","g_UTransform","g_VTransform",
    "kWorldToViewPortTfmLight","kWorldToViewPortTfmCharLight","kShadowDepthSampler",
    "kShadowCharDepthSampler","kShadowEdgeSampler","kShadowAmt","kIsShadowReceiver"};
size_t checks{};
const char* stage="startup";
void need(bool value,const char* message) {
    ++checks;
    if(!value) {std::fprintf(stderr,"CHECK %zu stage=%s: %s\n",checks,stage,message);throw Failure(message);}
}
std::vector<uint8_t> bytes(Runtime& rt,uint32_t address,uint32_t size) {
    const auto* p=rt.pointer(address,size,false);return {p,p+size};
}
void same(Runtime& rt,uint32_t address,const std::vector<uint8_t>& expected,const char* message) {
    need(!std::memcmp(rt.pointer(address,uint32_t(expected.size()),false),expected.data(),expected.size()),message);
}
void put(std::vector<uint8_t>& v,uint32_t offset,uint32_t word) {
    for(uint32_t i=0;i<4;++i) v.at(offset+i)=uint8_t(word>>(24-8*i));
}
struct SavedAbi {uint64_t sp,lr;std::array<uint64_t,18> gpr;};
SavedAbi abi(const PPCContext& c) {
    return {c.r1.u64,c.lr,{c.r14.u64,c.r15.u64,c.r16.u64,c.r17.u64,c.r18.u64,c.r19.u64,
        c.r20.u64,c.r21.u64,c.r22.u64,c.r23.u64,c.r24.u64,c.r25.u64,c.r26.u64,c.r27.u64,
        c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64}};
}
void sameAbi(const PPCContext& c,const SavedAbi& before) {
    const auto after=abi(c);
    need(after.sp==before.sp && after.lr==before.lr && after.gpr==before.gpr,
         "Original CPU call changed SP/LR/nonvolatile GPRs");
}
uint32_t align(uint32_t value,uint32_t boundary) {return (value+boundary-1)&~(boundary-1);}
struct StartupObserved {};

void probe(Runtime& rt,EngineCpuCalls& cpu) {
    auto* base=rt.base;
    stage="genuine empty pool preflight";
    const uint32_t pool=PPC_LOAD_U32(rootSlot),context=PPC_LOAD_U32(0x82D5DA74);
    const uint32_t ownerThread=rt.effectPoolThread,population=PPC_LOAD_U32(0x82E2D968);
    need(pool && pool==rt.effectPoolRoot && !(pool&0x7F),"Missing genuine aligned original pool");
    need(ownerThread==GetCurrentThreadId() && population==1,"Pool provenance/thread/population differs");
    need(rt.engineDriver && rt.engineDriver->started(),"Missing actual native driver");
    rt.engineDriver->requireContext(context);
    auto& effects=rt.engineDriver->effects();
    need(!effects.count() && !PPC_LOAD_U32(0x82D08BFC) && !PPC_LOAD_U32(0x82D0CAF8),
         "Fixture already owns FX/manager/console SDK device");
    const auto before=bytes(rt,pool,0x200),tableBefore=bytes(rt,table,25*16);
    for(uint32_t i=0;i<0x100;++i)
        need(before[i]==(i<0x80?0:0xFF),"Original empty bookkeeping differs");
    for(uint32_t i=0x100;i<0x128;i+=4) need(!PPC_LOAD_U32(pool+i),"Refuse initializer on nonempty metadata");
    need(!PPC_LOAD_U32(pool+0x180) && !PPC_LOAD_U32(pool+0x184) && PPC_LOAD_U32(pool+0x188)==1,
         "Refuse initializer on owned backing or non-root reference count");
    // Unknown constructor padding is snapshotted only; no fabricated initial value.
    const uint32_t bodySize=PPC_LOAD_U32(blob+4),blobSize=bodySize+12;
    need(PPC_LOAD_U32(blob)==0xA3D70141 && bodySize==0x47A4 && PPC_LOAD_U32(table+16)==blob,
         "Wrong original littextured source/profile");
    const auto sourceBefore=bytes(rt,blob,blobSize);
    auto inSource=[&](uint32_t address,uint32_t size) {
        need(address>=body && uint64_t(address)+size<=uint64_t(body)+bodySize,
             "Serialized cell/data escaped original body");
    };
    auto cell=[&](uint32_t offset,uint32_t size) {
        const uint32_t relative=PPC_LOAD_U32(body+offset);
        need(relative!=0xFFFFFFFF && relative<=bodySize-4,"Invalid serialized cell relative offset");
        const uint32_t pointerCell=body+relative;
        inSource(pointerCell,4);
        const uint32_t dataRelative=PPC_LOAD_U32(pointerCell);
        need(dataRelative!=0xFFFFFFFF && dataRelative<=bodySize,"Invalid serialized data relative offset");
        const uint32_t result=body+dataRelative;inSource(result,size);return result;
    };
    const uint32_t slots=PPC_LOAD_U32(body+0x11C),top=PPC_LOAD_U32(body+0x114);
    const uint32_t leaves=PPC_LOAD_U32(body+0x134),vectorsBytes=PPC_LOAD_U32(body+0x13C);
    const uint32_t namesBytes=PPC_LOAD_U32(body+0x294),dirtyWords=PPC_LOAD_U32(body+0x124);
    need(slots==12 && top==11 && leaves==11 && dirtyWords==1,"Shared original descriptor/count profile differs");
    need(vectorsBytes==320 && namesBytes==209,"Original default/name block sizes differ");
    const uint32_t descSource=cell(0x10C,slots*8),valueSource=cell(0x12C,vectorsBytes);
    const uint32_t nameSource=cell(0x290,namesBytes),pointerSource=cell(0x29C,slots*4);
    const uint32_t backingBytes=align(align(slots*8,16)+vectorsBytes+namesBytes,4)+slots*4;
    need(backingBytes==0x2A4,"Actual original allocation arithmetic differs");
    const auto descriptors=bytes(rt,descSource,slots*8),values=bytes(rt,valueSource,vectorsBytes);
    const auto nameBlock=bytes(rt,nameSource,namesBytes);
    // Live witness for the selected original CPU allocator dispatch, not an override.
    const uint32_t allocator=PPC_LOAD_U32(0x82D57244);
    need(allocator==0x82D5724C && PPC_LOAD_U32(allocator)==0x820B60B8 &&
         PPC_LOAD_U32(PPC_LOAD_U32(allocator))==0x8268DDA0,"Unexpected original allocator dispatch");
    std::printf("SOURCE F=%08X descriptors=%08X defaults=%08X names=%08X pointers=%08X; slots=%u leaves=%u top=%u namesBytes=%u backingBytes=%X\n",
                body,descSource,valueSource,nameSource,pointerSource,slots,leaves,top,namesBytes,backingBytes);

    stage="one original first initialization";
    const auto initAbi=abi(cpu.registers());
    // r3 is the ORIGINAL serialized body, r4 is the genuine empty original pool.
    // Exactly once: this entry replaces backing and must never be used as merge.
    need(cpu.invoke(0x82C181E8,body,pool)==0,"Original pool initializer HRESULT failed");
    sameAbi(cpu.registers(),initAbi);
    const uint32_t backing=PPC_LOAD_U32(pool+0x180),size=PPC_LOAD_U32(pool+0x184);
    need(backing && !(backing&15) && size==backingBytes,"Original aligned backing/size differs");
    rt.pointer(backing,size,false);
    const uint32_t raw=PPC_LOAD_U32(backing-4);
    need(raw && raw<backing && backing-raw<=16 && backing==((raw+16)&~15u),
         "Original aligned allocator raw backpointer differs");
    rt.pointer(raw,backingBytes+16,false);
    need(uint64_t(backing)+size<=pool || uint64_t(pool)+0x200<=backing,"Backing aliases pool object");
    need(uint64_t(backing)+size<=blob || uint64_t(blob)+blobSize<=backing,"Backing aliases source");
    const uint32_t desc=backing,storage=align(backing+slots*8,16),ownedNames=storage+vectorsBytes;
    const uint32_t pointers=align(ownedNames+namesBytes,4);
    need(pointers+slots*4==backing+size,"Backing partitions exceed allocation");
    auto expected=before;
    std::fill(expected.begin(),expected.begin()+16,0xFF);
    const std::array<uint32_t,10> header={desc,slots,storage,leaves,vectorsBytes,dirtyWords,top,ownedNames,namesBytes,pointers};
    for(uint32_t i=0;i<header.size();++i) put(expected,0x100+4*i,header[i]);
    put(expected,0x180,backing);put(expected,0x184,size);
    same(rt,pool,expected,"Pool footprint/count/unknown padding differs from original stores");
    same(rt,desc,descriptors,"Owned descriptors are not exact original 96-byte copy");
    same(rt,storage,values,"Owned default storage is not exact original 320-byte copy");
    same(rt,ownedNames,nameBlock,"Owned names are not exact original 209-byte copy");
    for(uint32_t i=0;i<slots;++i) {
        const uint32_t relative=PPC_LOAD_U32(pointerSource+4*i);
        const uint32_t sourceName=relative==0xFFFFFFFF?0:body+relative;
        const uint32_t relocated=ownedNames+sourceName-nameSource;
        need(PPC_LOAD_U32(pointers+4*i)==relocated,"Original per-descriptor relocation differs");
        if(i) need(sourceName>=nameSource && sourceName<nameSource+namesBytes,
                   "Leaf name is outside copied shared names");
        else need(relative==0xFFFFFFFF,"Descriptor-zero sentinel differs");
    }
    // The unused descriptor-zero name becomes ownedNames-nameSource modulo 2^32.
    // Do not dereference it or impose invented null semantics. Three alignment
    // bytes between names and pointer table are also intentionally not asserted.
    stage="original name API and immutable readback";
    const auto backingBeforeQueries=bytes(rt,backing,size);
    uint32_t cursor=0,descriptorIndex=1,leafOrdinal=0;
    for(uint32_t i=0;i<top;++i) {
        const uint32_t length=uint32_t(std::strlen(names[i]))+1;
        need(cursor+length<=namesBytes && !std::memcmp(nameBlock.data()+cursor,names[i],length),
             "Actual original top-level name differs");
        need(PPC_LOAD_U32(pointers+4*descriptorIndex)==ownedNames+cursor,"Owned name/descriptor association differs");
        const uint32_t word=PPC_LOAD_U32(desc+8*descriptorIndex);
        need(!(word&3),"This row's shared descriptor is not a leaf");
        const uint32_t expectedHandle=(descriptorIndex<<18)|(leafOrdinal<<1)|1;
        const auto lookupAbi=abi(cpu.registers());
        const uint32_t handle=cpu.invoke(0x826B2528,pool,nameSource+cursor);
        need(handle==expectedHandle,"Actual original API returned a different shared handle");
        sameAbi(cpu.registers(),lookupAbi);
        need(cpu.invoke(0x826B2528,pool,ownedNames+cursor)==handle,"Owned name pointer lookup differs");
        sameAbi(cpu.registers(),lookupAbi);
        const uint32_t firstVector=PPC_LOAD_U16(desc+8*descriptorIndex+6);
        // Raw first 16-byte storage slot only. The +4 field is not assumed to
        // be a vector count; all 320 storage bytes were compared above.
        need(firstVector*16+16<=vectorsBytes,"Shared descriptor first storage slot differs");
        std::printf("HANDLE %s=%08X descriptor=%u leaf=%u valueOffset=%X words=%08X,%08X,%08X,%08X\n",
                    names[i],handle,descriptorIndex,leafOrdinal,firstVector*16,
                    PPC_LOAD_U32(storage+firstVector*16),PPC_LOAD_U32(storage+firstVector*16+4),
                    PPC_LOAD_U32(storage+firstVector*16+8),PPC_LOAD_U32(storage+firstVector*16+12));
        cursor+=length;++descriptorIndex;++leafOrdinal;
    }
    need(cursor==208 && cursor+1==namesBytes && !nameBlock[cursor],"Shared names/extra trailing zero differs");
    need(descriptorIndex==slots && leafOrdinal==leaves,"Shared traversal did not consume exact slots/leaves");
    // Original image pointers only: private row0 g_Weights and wrapper name
    // fourtapblend must not be accepted as shared row1 pool parameters.
    const uint32_t firstBody=0x820B8AAC;
    // Private +288 is direct-relative; shared +290 has an extra cell.
    const uint32_t privateNames=firstBody+PPC_LOAD_U32(firstBody+0x288);
    constexpr char privateName[]="g_Weights";
    need(!std::memcmp(rt.pointer(privateNames,sizeof(privateName),false),privateName,sizeof(privateName)),
         "Original private-name cell assumption differs");
    const auto missingAbi=abi(cpu.registers());
    need(!cpu.invoke(0x826B2528,pool,privateNames),"Shared API accepted private g_Weights");
    need(!cpu.invoke(0x826B2528,pool,PPC_LOAD_U32(table+4)),"Shared API accepted fourtapblend wrapper name");
    sameAbi(cpu.registers(),missingAbi);
    same(rt,backing,backingBeforeQueries,"Original name API modified backing/readback values");
    same(rt,pool,expected,"Original name API changed metadata/padding/refcount");
    same(rt,blob,sourceBefore,"Initializer/name API modified original serialized source");
    same(rt,table,tableBefore,"Fixture changed production registration table");
    need(PPC_LOAD_U32(rootSlot)==pool && rt.effectPoolRoot==pool && rt.effectPoolThread==ownerThread &&
         PPC_LOAD_U32(pool+0x188)==1 && PPC_LOAD_U32(0x82E2D968)==population,
         "Original CPU initialization lost root provenance/count/population");
    need(currentContext==&cpu.registers() && PPC_LOAD_U32(0x82D5DA74)==context && !effects.count() &&
         !PPC_LOAD_U32(0x82D08BFC) && !PPC_LOAD_U32(0x82D0CAF8),
         "CPU initialization changed native context or constructed FX/SDK objects");
    rt.engineDriver->requireContext(context);
    std::printf("POOL P=%08X raw=%08X aligned=%08X size=%X desc=%08X storage=%08X names=%08X pointers=%08X count=1 population=1 nativeContext=%08X\n",
                pool,raw,backing,size,desc,storage,ownedNames,pointers,context);
    // Keep backing owned by the real root through terminal Runtime teardown.
    // No CF final release/CRT cleanup, normal gameplay, or leak proof is claimed.
}
}

int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==2,"Original image path required");Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);
        try {
            const auto entry=original;bool sourceObserved=false,startupObserved=false;
            rt.audioBoundaryObserver=[&](uint32_t pc,PPCContext&,uint8_t* base) {
                need(bool(rt.engineAudioOutput),"Missing actual audio output");
                const auto view=rt.engineAudioOutput->view();
                need(view.nativeEngine && view.muted && view.configured && view.identity,"Missing actual muted native Dac source");
                if(pc==0x82345920) {
                    need(!sourceObserved && !view.active && !view.workerId,"Source observed after worker construction");
                    sourceObserved=true;return;
                }
                need(pc==0x828166FC && sourceObserved && view.active && view.workerId && view.event,
                     "Missing actual established post-audio boundary");
                need(!PPC_LOAD_U32(0x82D08BFC),"Startup already published FX manager");
                std::shared_ptr<KernelHandle> worker;
                {std::lock_guard lock(rt.threadMutex);
                    for(const auto& thread:rt.threads) if(thread->id==view.workerId) worker=thread->object;}
                need(worker && GetThreadId(worker->native)==view.workerId && view.workerId!=GetCurrentThreadId() &&
                     WaitForSingleObject(worker->native,0)==WAIT_TIMEOUT,"Dac worker is not an actual live OS thread");
                throw StartupObserved{};
            };
            try {runOriginal(original,rt.base);} catch(const StartupObserved&) {startupObserved=true;}
            rt.audioBoundaryObserver={};
            need(startupObserved,"Original startup missed the established boundary");
            // Fresh saved entry context; never resume the exception-unwound startup.
            EngineCpuCalls cpu(entry,rt.base);probe(rt,cpu);
            std::printf("PASS effect pool CPU: %zu checks; one original serialized-body initialization; 11 shared handles; ALL MUTED; no FX record/device/shader creation; terminal teardown only\n",checks);
            std::fflush(stdout);
        } catch(const std::exception& error) {
            std::fprintf(stderr,"Pool CPU fixture failed before Runtime teardown: stage=%s checks=%zu error=%s\n",stage,checks,error.what());throw;
        }
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL effect pool CPU: %s\n",error.what());return 1;}
}
