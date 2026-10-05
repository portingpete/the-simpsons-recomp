#pragma once
#include "runtime.h"

namespace Simpsons {
void assignThreadAffinity(Runtime& runtime,HANDLE native,uint32_t pcr,uint32_t thread,uint32_t logicalMask);
struct GuestThread {
    Runtime* runtime{};
    PPCContext context{};
    HANDLE native{};
    // Blocks entry until creation commits; rollback cancels and joins the host
    // thread before guest mappings are removed, even after an early resume.
    HANDLE startEvent{};
    std::shared_ptr<KernelHandle> object;
    uint32_t id{},pcr{},thread{},tls{},stack{},stackSize{},startup{},entry{},argument{},flags{};
    std::atomic<bool> finished=false; // Diagnostic only: the native HANDLE proves termination.
    std::atomic<bool> cancelled=false;
    ~GuestThread();
};
}
