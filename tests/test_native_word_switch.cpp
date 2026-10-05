#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace {
size_t checks=0;
void need(bool value,const char* reason) {++checks;if(!value) throw std::runtime_error(reason);}
}
int main(int argc,char** argv) {
    try {
        need(argc==2,"Expected original image path");
        Simpsons::Runtime rt;rt.load(argv[1]);
        rt.map(0x10000,0x10000,true,"Original word-switch fixture");
        auto* base=rt.base;
        // Original load/add/word-bound/table-address/CTR sequence. The input
        // remains a full GPR after ADDI; only dispatch uses its bounded word.
        const std::array<uint32_t,10> words={0x807f0000,0x39630001,0x2b0b007f,0x41990368,
            0x3d808285,0x398cbd34,0x5560103a,0x7c0c002e,0x7c0903a6,0x4e800420};
        for(size_t i=0;i<words.size();++i)
            need(PPC_LOAD_U32(0x8284bd0c+uint32_t(i*4))==words[i],"Original word-switch instruction pin changed");
        need(PPC_LOAD_U32(0x8284bd34)==0x8284c520,"Original case-zero table target changed");
        need(PPC_LOAD_U32(0x8284c520)==0x3860011f && PPC_LOAD_U32(0x8284c524)==0x382100d0 &&
             PPC_LOAD_U32(0x8284c528)==0x481efeec,"Original case-zero return pin changed");
        constexpr uint32_t object=0x10000,output=0x11000;
        std::array<uint8_t,64> input{},sentinel{};
        input.fill(0xa5);sentinel.fill(0x5a);
        std::memcpy(rt.pointer(object,unsigned(input.size()),true),input.data(),input.size());
        PPC_STORE_U32(object,0xffffffff);
        std::memcpy(input.data(),rt.pointer(object,unsigned(input.size()),false),input.size());
        std::memcpy(rt.pointer(output,unsigned(sentinel.size()),true),sentinel.data(),sentinel.size());
        PPCContext incoming{};incoming.r1.u64=0x20000;incoming.lr=0x12345678;
        Simpsons::EngineCpuCalls cpu(incoming,base);
        const auto stack=cpu.registers().r1.u64;
        cpu.registers().r27.u64=0x1122334455667788ull;cpu.registers().r31.u64=0x8877665544332211ull;
        need(cpu.invoke(0x8284bcf8,object,output)==287,"Actual original wrapped-word case did not return its original token");
        need(cpu.registers().r11.u64==0x100000000ull,"Switch fix incorrectly truncated original GPR arithmetic");
        need(cpu.registers().ctr.u64==0x8284c520,"Original table load/CTR target did not execute");
        need(cpu.registers().r1.u64==stack && cpu.registers().r27.u64==0x1122334455667788ull &&
             cpu.registers().r31.u64==0x8877665544332211ull,"Original switch did not restore its frame/nonvolatiles");
        need(!std::memcmp(rt.pointer(object,unsigned(input.size()),false),input.data(),input.size()) &&
             !std::memcmp(rt.pointer(output,unsigned(sentinel.size()),false),sentinel.data(),sentinel.size()),
             "Original terminal case changed caller input/output");
        std::printf("PASS original word switch: %zu checks; actual LWZ/ADDI/CMPLWI/table/CTR/return, full GPR preserved\n",checks);
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL original word switch: %s\n",error.what());return 1;}
}
