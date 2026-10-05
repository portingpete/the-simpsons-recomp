#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include <array>
#include <cstdio>
#include <cstring>

PPC_FUNC(__imp__XamGetExecutionId);
PPC_FUNC(__imp__RtlImageXexHeaderField);
namespace {
using namespace Simpsons;
size_t checks{};
void need(bool ok,const char* why){++checks;if(!ok)throw Failure(why);}
uint32_t be32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
}
int main(int argc,char** argv){try{
    need(argc==2,"Original image path required");Runtime rt;rt.load(argv[1]);PPCContext entry{};rt.initialize(entry);auto* base=rt.base;
    const uint32_t kernelVersion=PPC_LOAD_U32(0x82000664);
    need(kernelVersion>=0x01000000 && kernelVersion+8<=0x01040000,"XboxKrnlVersion import does not point into owned bootstrap memory");
    need(PPC_LOAD_U16(kernelVersion)==2 && PPC_LOAD_U16(kernelVersion+2)==0 &&
         PPC_LOAD_U16(kernelVersion+4)==5766 && PPC_LOAD_U16(kernelVersion+6)==0,
         "XboxKrnlVersion does not match the verified XEX xboxkrnl 0x20168600 requirement");
    rt.map(0x50000,0x1000,true,"execution identity output fixture");rt.map(0x60000,0x1000,false,"execution identity readonly fixture");
    constexpr uint32_t out=0x50100;
    const auto source=readFile(std::filesystem::path(argv[1]).parent_path()/"simpsons.unencrypted.xex");
    const uint32_t size=be32(source.data()+8),count=be32(source.data()+20);uint32_t offset=0,field=0;
    for(uint32_t i=0;i<count;++i)if(be32(source.data()+24+i*8)==0x40006){field=24+i*8;offset=be32(source.data()+field+4);break;}
    need(offset&&offset+24<=size,"Original execution optional header is absent");
    constexpr std::array<uint8_t,24> original={0x05,0x63,0x10,0x42,0,0,0,1,0,0,0,1,0x45,0x41,0x08,0x09,0,0,1,1,0,0,0,0};
    need(!std::memcmp(source.data()+offset,original.data(),original.size()),"Original execution metadata identity changed");
    std::vector<uint8_t> header(rt.pointer(rt.headerAddress,size,false),rt.pointer(rt.headerAddress,size,false)+size);
    const auto saved=PPCFPSCRRegister::getcsr();const auto handles=rt.handles.size(),regions=rt.regions.size();
    for(uint32_t fp:{0x1F80u,0x3FC0u,0x5F80u,0x9FC0u,0xE07Fu}){
        std::memset(rt.pointer(out-8,20,true),0xA7,20);PPCContext c{};std::memset(&c,0xA5,sizeof(c));c.r3.u64=out;
        PPCContext expected;std::memcpy(&expected,&c,sizeof(c));expected.r3.u64=0;
        PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(0xABCDEF01);__imp__XamGetExecutionId(c,base);
        const auto actualFP=PPCFPSCRRegister::getcsr();const auto error=GetLastError();PPCFPSCRRegister::restoreHostCSR(saved);
        need(!std::memcmp(&c,&expected,sizeof(c))&&actualFP==fp&&error==0xABCDEF01,"Execution query changed CPU/host state");
        need(PPC_LOAD_U32(out)==rt.headerAddress+offset&&!std::memcmp(rt.pointer(PPC_LOAD_U32(out),24,false),original.data(),24),"Execution query did not return actual loaded original bytes");
        need(PPC_LOAD_U64(out-8)==0xA7A7A7A7A7A7A7A7ull&&PPC_LOAD_U64(out+4)==0xA7A7A7A7A7A7A7A7ull,"Execution pointer output escaped four bytes");
        need(!std::memcmp(header.data(),rt.pointer(rt.headerAddress,size,false),size)&&rt.handles.size()==handles&&rt.regions.size()==regions,"Execution query changed header/ownership or fabricated an allocation");
    }
    PPCContext common{};common.r3.u64=rt.headerAddress;common.r4.u64=0x40006;__imp__RtlImageXexHeaderField(common,base);
    need(common.r3.u32==rt.headerAddress+offset,"Existing original-header lookup disagrees with execution query");
    EngineCpuCalls cpu(entry,base);const auto sp=cpu.registers().r1.u64;
    for(uint32_t title:{0u,0x45410809u,0x4541FFFFu})need(cpu.invoke(0x82433BF0,title)==0&&cpu.registers().r1.u64==sp,"Original publisher/title validation rejected its own execution identity");
    need(cpu.invoke(0x82433BF0,0x45420001)==ERROR_FUNCTION_FAILED&&cpu.registers().r1.u64==sp,"Original foreign-publisher rejection was bypassed");
    const auto rejected=[&](uint32_t destination){
        PPCContext c=entry;c.r3.u64=destination;PPC_STORE_U32(out,0xBEEFBEEF);bool failed=false;
        try{__imp__XamGetExecutionId(c,base);}catch(const Failure&){failed=true;}
        need(failed&&PPC_LOAD_U32(out)==0xBEEFBEEF,"Rejected identity request published an output");
        std::memcpy(rt.pointer(rt.headerAddress,size,true),header.data(),size);
    };
    rejected(0);rejected(out+1);rejected(0x60000);rejected(rt.headerAddress+offset);
    PPC_STORE_U32(rt.headerAddress,0);rejected(out);
    PPC_STORE_U32(rt.headerAddress+8,23);rejected(out);
    PPC_STORE_U32(rt.headerAddress+8,0x10001);rejected(out);
    PPC_STORE_U32(rt.headerAddress+20,0xFFFFFFFF);rejected(out);
    PPC_STORE_U32(rt.headerAddress+field,0x40007);rejected(out);
    PPC_STORE_U32(rt.headerAddress+field+4,size-4);rejected(out);
    need(!std::memcmp(header.data(),rt.pointer(rt.headerAddress,size,false),size),"Identity rejection fixture did not restore header");
    rt.requestStop("Execution identity fixture complete");PPCContext c=entry;c.r3.u64=out;bool cancelled=false;
    try{__imp__XamGetExecutionId(c,base);}catch(const Failure&){cancelled=true;}need(cancelled,"Cancelled runtime returned an execution identity");
    std::printf("PASS native execution identity:%zu checks; bound XboxKrnlVersion 2.0.5766.0, actual loaded XEX metadata, original publisher validation, no allocation, CPU/host state and bounded rejection\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL native execution identity:%s\n",error.what());return 1;}}
