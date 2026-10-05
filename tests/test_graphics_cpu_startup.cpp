// Retain and exercise the original first graphics object's CPU lifetime.
// The existing post-audio startup observation supplies the real initialized
// heap and native driver. This is an isolated lifetime, not the whole parent.
#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace {
using namespace Simpsons;
size_t checks{};
void need(bool value,const char* message) {++checks;if(!value) throw Failure(message);}
struct StartupObserved {};
void observe(PPCContext& ctx,uint8_t* base) {
    (void)ctx;
    need(active && active->engineDriver && active->engineDriver->started(),"Missing actual native driver");
    active->engineDriver->requireContext(PPC_LOAD_U32(0x82D5DA74));
    need(!PPC_LOAD_U32(0x82D08BFC),"Graphics object was published before its constructor");
    throw StartupObserved{};
}
void checkObject(Runtime& rt,uint32_t object,uint32_t identity) {
    auto* base=rt.base;
    need(PPC_LOAD_U32(0x82D08BFC)==object && PPC_LOAD_U32(object)==0x820B71DC,
         "Original singleton/vtable publication differs");
    need(PPC_LOAD_U32(object+0x14)==identity && PPC_LOAD_U32(object+0x18)==PPC_LOAD_U32(0x82D6D2F8),
         "Original borrowed context/global value differs");
    need(!PPC_LOAD_U32(object+4) && !PPC_LOAD_U32(object+0xC) && PPC_LOAD_U8(object+0x10)==1,
         "Original basic defaults differ");
    for(uint32_t offset:{0x1Cu,0x34u}) {
        const uint32_t array=object+offset,node=PPC_LOAD_U32(array),items=PPC_LOAD_U32(node+8);
        need(node && items && PPC_LOAD_U32(array+0x14),"Original array allocation failed");
        rt.pointer(node,12,false);rt.pointer(items,28,false);rt.pointer(PPC_LOAD_U32(array+0x14),24,false);
        need(!PPC_LOAD_U32(array+4) && PPC_LOAD_U32(array+8)==6 &&
             PPC_LOAD_U32(array+0xC)==0x3E800000 && !PPC_LOAD_U32(array+0x10),
             "Original six-entry container defaults differ");
        for(uint32_t i=0;i<6;++i) need(!PPC_LOAD_U32(items+4*i),"Original container entry is not empty");
    }
    for(uint32_t matrix=0;matrix<6;++matrix) for(uint32_t element=0;element<16;++element) {
        const auto actual=PPC_LOAD_U32(object+0x50+matrix*0x40+element*4);
        const uint32_t expected=element%5==0?0x3F800000u:0;
        // Cofactor-based inverse math retains signed zero. Verify numerical
        // identity without rewriting or claiming an original-console bit oracle.
        const bool equal=expected?actual==expected:(actual&0x7FFFFFFFu)==0;
        if(!equal) {
            std::fprintf(stderr,"Matrix mismatch object=%08X matrix=%u element=%u actual=%08X expected=%08X\n",
                         object,matrix,element,actual,expected);
        }
        need(equal,"Original matrix constructor/multiply/inverse did not retain identity");
    }
    for(uint32_t offset:{0x1D0u,0x1D4u,0x1D8u})
        need(!(PPC_LOAD_U32(object+offset)&0x7FFFFFFFu),"Original inverse translation differs");
    need(PPC_LOAD_U32(object+0x1DC)==1 && PPC_LOAD_U32(object+0x1E0)==1,"Original matrix revisions differ");
    const std::array<uint32_t,11> defaults={0,0xBF800000,0,0x3F800000,0x3F800000,0x3F800000,
                                         0x3F800000,0x3DCCCCCD,0x3DCCCCCD,0x3DCCCCCD,0x3F800000};
    for(uint32_t i=0;i<defaults.size();++i)
        need(PPC_LOAD_U32(object+0x1F0+4*i)==defaults[i],"Original graphics scalar default differs");
    for(uint32_t offset:{0x21Cu,0x220u,0x224u,0x228u}) need(!PPC_LOAD_U32(object+offset),"Original trailing default differs");
    need(PPC_LOAD_U32(object+8)==0xA5A5A5A5 && PPC_LOAD_U8(object+0x11)==0xA5,
         "Constructor changed an uninitialized field");
    for(uint32_t offset=0x22C;offset<0x260;++offset)
        need(PPC_LOAD_U8(object+offset)==0xA5,"Constructor changed untouched trailing storage");
}
void secondaryLifetime(Runtime& rt,EngineCpuCalls& cpu,uint32_t manager,uint32_t identity,uint32_t options) {
    auto* base=rt.base;
    const uint32_t object=cpu.invoke(0x8269BF70,0x70,options);
    need(object && !(object&15),"Original secondary object allocation failed");
    std::memset(rt.pointer(object,0x70,true),0xA5,0x70);
    need(cpu.invoke(0x8271BD10,object,manager)==object,"Original secondary constructor result differs");
    need(PPC_LOAD_U32(object)==identity && PPC_LOAD_U32(object+0x60)==manager,
         "Secondary constructor lost original borrowed ownership");
    need(PPC_LOAD_U8(object+0x1C)==PPC_LOAD_U8(0x82D5DA70) && !PPC_LOAD_U8(object+0x1D) &&
         PPC_LOAD_U32(object+0x14)==0x3F800000 && !PPC_LOAD_U32(object+0x18),
         "Secondary CPU defaults differ");
    for(uint32_t offset:{4u,8u,0x24u,0x28u,0x2Cu,0x30u,0x34u,0x38u,0x3Cu,0x40u,0x44u,0x48u,0x4Cu,0x64u,0x68u})
        need(!PPC_LOAD_U32(object+offset),"Secondary resource/default field is not empty");
    const uint32_t child=PPC_LOAD_U32(object+0x20);
    need(child && PPC_LOAD_U32(child)==identity,"Secondary constructor did not allocate its real CPU child");
    for(uint32_t offset=4;offset<0x58;++offset)
        need(PPC_LOAD_U8(child+offset)==(offset==0x54?1u:0u),"Original child defaults/SDK-branch flag differ");
    need(PPC_LOAD_U32(object+0xC)==0xA5A5A5A5 && PPC_LOAD_U32(object+0x6C)==0xA5A5A5A5,
         "Secondary constructor changed uninitialized storage");
    cpu.invoke(0x8271CD00,object);
    // Child is freed; inspect only still-live O and the first manager.
    need(!PPC_LOAD_U32(object+0x20) && PPC_LOAD_U32(0x82D08BFC)==manager && PPC_LOAD_U32(manager+0x14)==identity,
         "Original idle cleanup changed parent/borrowed ownership");
    cpu.invoke(0x8269BEB0,object);
}
void emptyEffectWrapper(Runtime& rt,EngineCpuCalls& cpu,uint32_t manager) {
    auto* base=rt.base;
    const uint32_t literal=PPC_LOAD_U32(0x82CEFD24);
    constexpr char expected[]="fourtapblend";
    need(!std::memcmp(rt.pointer(literal,sizeof(expected),false),expected,sizeof(expected)),"Original first effect literal changed");
    const uint32_t wrapper=cpu.invoke(0x8269BD70,0x30);
    need(wrapper!=0,"Original FX wrapper allocation failed");
    std::memset(rt.pointer(wrapper,0x30,true),0xA5,0x30);
    // r5 is unused by this original constructor; r6 is the retained M owner.
    need(cpu.invoke(0x826B3880,wrapper,literal,0,manager)==wrapper,"Original FX wrapper constructor result differs");
    const uint32_t name=PPC_LOAD_U32(wrapper+4);
    need(PPC_LOAD_U32(wrapper)==0x820B7140 && PPC_LOAD_U32(wrapper+0xC)==manager &&
         PPC_LOAD_U32(wrapper+0x14)==wrapper,"Original FX wrapper vtable/ownership differs");
    need(name && name!=literal && PPC_LOAD_U16(wrapper+8)==sizeof(expected)-1 &&
         PPC_LOAD_U16(wrapper+10)>=sizeof(expected)-1 &&
         !std::memcmp(rt.pointer(name,sizeof(expected),false),expected,sizeof(expected)),
         "Original FX wrapper did not own an exact independent name copy");
    for(uint32_t offset:{0x10u,0x1Cu,0x20u,0x2Cu})
        need(!PPC_LOAD_U32(wrapper+offset),"Original empty effect/cache fields differ");
    for(uint32_t offset:{0x18u,0x24u,0x28u})
        need(PPC_LOAD_U32(wrapper+offset)==0xA5A5A5A5,"FX wrapper constructor changed untouched helper storage");
    need(cpu.invoke(0x826B38F0,wrapper)==name && !cpu.invoke(0x826B3908,wrapper),
         "Original wrapper name/effect accessors differ");
    need(cpu.invoke(0x826B4AC8,wrapper,1)==wrapper,"Original empty FX deleting destructor result differs");
    // Wrapper and owned name are freed; inspect only the live manager/table.
    need(PPC_LOAD_U32(0x82D08BFC)==manager && !PPC_LOAD_U32(0x82CEFD28),
         "Unregistered empty FX lifecycle changed the parent/registration table");
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==2,"Original image required");Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);
        const auto entry=original;bool observed=false;
        rt.audioBoundaryObserver=[](uint32_t pc,PPCContext& ctx,uint8_t* base) {
            if(pc==0x828166FC) observe(ctx,base);
            else need(pc==0x82345920,"Unexpected original startup observation");
        };
        try{runOriginal(original,rt.base);}catch(const StartupObserved&){observed=true;}
        rt.audioBoundaryObserver={};
        need(observed,"Actual original startup did not reach the existing post-audio observation");
        EngineCpuCalls cpu(entry,rt.base);auto* base=rt.base;
        const uint32_t identity=PPC_LOAD_U32(0x82D5DA74),options=cpu.registers().r1.u32+0x60;
        const auto savedFlag=PPC_LOAD_U8(0x82D5DB74);const auto savedEffect=PPC_LOAD_U32(0x82E2D2D8);
        for(uint32_t cycle=0;cycle<2;++cycle) {
            PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
            const uint32_t object=cpu.invoke(0x8269BF70,0x260,options);
            need(object && !(object&15),"Actual original graphics heap allocation failed");
            std::memset(rt.pointer(object,0x260,true),0xA5,0x260);
            // Fixture inputs exercise both original optional global-store branches.
            PPC_STORE_U8(0x82D5DB74,uint8_t(cycle));PPC_STORE_U32(0x82E2D2D8,0x12345678);
            const auto sp=cpu.registers().r1.u64;
            cpu.registers().r30.u64=0x123456789ABCDEF0ull;cpu.registers().r31.u64=0xFEDCBA9876543210ull;
            cpu.registers().f31.u64=0x40123456789ABCDEull;cpu.registers().lr=0x12345678;
            need(cpu.invoke(0x826B6F60,object,identity)==object,"Original constructor returned another object");
            need(cpu.registers().r1.u64==sp && cpu.registers().r30.u64==0x123456789ABCDEF0ull &&
                 cpu.registers().r31.u64==0xFEDCBA9876543210ull && cpu.registers().f31.u64==0x40123456789ABCDEull &&
                 cpu.registers().lr==0x12345678,"Original constructor damaged save/restore ABI");
            checkObject(rt,object,identity);
            need(PPC_LOAD_U32(0x82E2D2D8)==(cycle?1u:0x12345678u),"Original optional CPU side effect differs");
            secondaryLifetime(rt,cpu,object,identity,options);
            emptyEffectWrapper(rt,cpu,object);
            need(cpu.invoke(0x826B7600,object,1)==object,"Original deleting destructor returned another value");
            // Do not inspect freed object or array storage.
            need(!PPC_LOAD_U32(0x82D08BFC),"Original destructor did not clear singleton");
            need(PPC_LOAD_U32(0x82D5DA74)==identity && !PPC_LOAD_U32(0x82D0CAF8),
                 "CPU lifetime changed native context or constructed a console device");
            rt.engineDriver->requireContext(identity);
        }
        PPC_STORE_U8(0x82D5DB74,savedFlag);PPC_STORE_U32(0x82E2D2D8,savedEffect);
        std::printf("PASS original graphics CPU startup: %zu checks; two actual heap/array/matrix/constructor/destructor lifetimes; ALL MUTED; no resource or draw claim\n",checks);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL graphics CPU startup: %s\n",error.what());return 1;}
}
