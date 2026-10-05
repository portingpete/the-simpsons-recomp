#include "runtime/engine_audio.h"
#include "runtime/engine_cpu_calls.h"
#include "audio/resident_xma.h"
#include <atomic>
#include <cstdio>
#include <cstring>

namespace {
using namespace Simpsons;
size_t checks=0;
void need(bool value,const char* why) {++checks;if(!value) throw Failure(why);}
template<class F> void rejects(F&& f) {
    bool rejected=false;try {f();}catch(const std::exception&) {rejected=true;}
    need(rejected,"Unobserved or retired resident bank was accepted");
}
void verifyBank(Runtime& rt,PPCContext& entry,uint8_t* base) {
    auto& owners=*rt.engineAudio;
    const uint32_t resource=entry.r3.u32;
    const auto bank=owners.residentBank(resource);
    need(bank.resource==resource && bank.handle && PPC_LOAD_U32(resource+0x14)==bank.handle &&
         !PPC_LOAD_U32(resource+0xC) && bank.source!=bank.allocation &&
         bank.header==bank.allocation+Audio::residentAudioHeaderOffset &&
         bank.extent>=Audio::residentAudioBytes && bank.extent<Audio::residentBankBytes &&
         owners.allocationGeneration(bank.allocation,Audio::residentAudioBytes)==bank.generation,
         "Actual named loader did not retain a separately allocated audio section");
    // Borrowed named payloads belong to their containing stream allocation.
    // The actual callback clears the resource pointer in both branches, but
    // only frees a source allocation when the resource retained an allocator.
    const bool ownsSource=PPC_LOAD_U32(resource+0x18)!=0;
    bool sourceRetired=false;
    try {sourceRetired=owners.allocationGeneration(bank.source,Audio::residentBankBytes)!=bank.sourceGeneration;}
    catch(const std::exception&) {sourceRetired=true;}
    need(sourceRetired==ownsSource,
         "Original completion callback violated source-bank ownership");
    need(!std::memcmp(rt.pointer(bank.header,8,false),Audio::residentXmaHeader.data(),8),
         "Original copied audio does not contain the qualified resident header");
    rejects([&] {owners.residentBank(resource+4);});
    rejects([&] {owners.residentBank(bank.allocation);});
    rejects([&] {owners.allocationGeneration(bank.allocation,bank.extent+1);});

    EngineCpuCalls cpu(entry,base);
    const auto sp=cpu.registers().r1.u64,lr=cpu.registers().lr;
    // Real resource disposal calls the metadata/voice retirement path and the
    // real backend's paired physical free. No observer is invoked by the test.
    cpu.invoke(0x8272E718,resource);
    need(cpu.registers().r1.u64==sp && cpu.registers().lr==lr && !PPC_LOAD_U32(resource+0x14),
         "Original SBK resource retirement changed ABI or retained its bank handle");
    rejects([&] {owners.residentBank(resource);});
    bool audioRetired=false;
    try {audioRetired=owners.allocationGeneration(bank.allocation,Audio::residentAudioBytes)!=bank.generation;}
    catch(const std::exception&) {audioRetired=true;}
    need(audioRetired,"Original backend did not complete its physical audio free");

    // Exercise the same real allocator again. Reuse must not revive the bank
    // association, whether the pool returns the same address or another one.
    const auto replacement=cpu.invoke(0x8238E880,Audio::residentAudioBytes,0xA0004000);
    need(replacement && owners.allocationGeneration(replacement,Audio::residentAudioBytes)!=bank.generation,
         "Original physical allocator reused a retired generation");
    rejects([&] {owners.residentBank(resource);});
    cpu.invoke(0x8238EB00,replacement,0x80000000);
    need(cpu.registers().r1.u64==sp && cpu.registers().lr==lr,"Original physical free changed ABI");
    std::printf("Original SBK split lifecycle PASS: %zu checks; named asset, %s source, audio copy, actual retirement and stale-generation rejection\n",
                checks,ownsSource?"released owned":"retained borrowed");
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==2,"Original image path required");
        Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);
        std::atomic<bool> passed=false;
        rt.audioBoundaryObserver=[&](uint32_t pc,PPCContext& ctx,uint8_t* base) {
            if(pc!=0x8271191C) return;
            verifyBank(rt,ctx,base);
            passed.store(true);
            // This isolated fixture ends after disposing the actual resource;
            // it does not resume gameplay with a deliberately retired bank.
            rt.requestStop("Original SBK lifecycle fixture completed");
        };
        try {runOriginal(original,rt.base);}
        catch(const std::exception&) {if(!passed.load()) throw;}
        rt.stopThreads();rt.audioBoundaryObserver={};
        need(passed.load(),"Actual startup did not reach the qualified named SBK load");
        return 0;
    }catch(const std::exception& error) {
        std::fprintf(stderr,"Original SBK split lifecycle FAIL: %s\n",error.what());return 1;
    }
}
