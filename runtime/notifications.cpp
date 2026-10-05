#include "runtime.h"
#include "native_notifications.h"
#include <cstdio>

namespace Simpsons {
std::shared_ptr<Platform::NativeNotifications> Runtime::notificationSource() {
    if(active!=this) throw Failure("Native notification source belongs to another runtime");
    checkRunning();std::lock_guard lock(notificationMutex);
    if(!notifications) notifications=std::make_shared<Platform::NativeNotifications>();
    return notifications;
}
}

PPC_FUNC(__imp__XamNotifyCreateListener) {
    if(!Simpsons::active || base!=Simpsons::active->base) throw Simpsons::Failure("Invalid notification-listener runtime");
    auto& rt=*Simpsons::active;
    // Qualified original subscriptions only: system (82860F10)
    // and audio category 5 (8280A178). The latter's 8280A780 poll requests
    // 0x0A000003; an empty poll preserves the original owner's +4C byte.
    // Registration creates no playback/controller state or initial event.
    // Keep combined masks and other categories/versions explicitly gated.
    const uint64_t mask=ctx.r3.u64;
    if((mask!=1 && mask!=0x20) || ctx.r4.u32!=2) {
        char message[192];int n=std::snprintf(message,sizeof(message),"Unsupported native notification subscription: mask=%016llX version=%u caller=%08X",
            static_cast<unsigned long long>(ctx.r3.u64),ctx.r4.u32,uint32_t(ctx.lr));
        if(n<0||size_t(n)>=sizeof(message)) throw Simpsons::Failure("Unsupported native notification subscription: diagnostic truncated");
        throw Simpsons::Failure(message);
    }
    auto listener=rt.notificationSource()->create(ctx.r3.u64,ctx.r4.u32);
    HANDLE wait=listener->duplicateWaitHandle();
    std::shared_ptr<Simpsons::KernelHandle> object;
    try {object=std::make_shared<Simpsons::KernelHandle>(wait,Simpsons::KernelHandle::Type::Notification);}
    catch(...) {CloseHandle(wait);throw;}
    object->notification=std::move(listener);
    const uint32_t handle=rt.addHandle(std::move(object));
    ctx.r3.u64=handle;
    std::fprintf(stderr,"[NOTIFICATION] native %s listener=%08X mask=%016llX version=2; waitable queue, no synthetic startup events\n",
        mask==1?"system":"audio-category-5",handle,static_cast<unsigned long long>(mask));
}

PPC_FUNC(__imp__XNotifyGetNext) {
    if(!Simpsons::active || base!=Simpsons::active->base) throw Simpsons::Failure("Invalid notification-poll runtime");
    auto& rt=*Simpsons::active;rt.checkRunning();
    const uint32_t handle=ctx.r3.u32,match=ctx.r4.u32,idOut=ctx.r5.u32,paramOut=ctx.r6.u32;
    auto object=rt.getHandle(handle);
    if(!object || object->type!=Simpsons::KernelHandle::Type::Notification || !object->notification)
        throw Simpsons::Failure("Notification poll does not reference an owned native listener");
    if(!idOut) throw Simpsons::Failure("Native notification poll requires an original ID output");
    rt.pointer(idOut,4,true);if(paramOut) rt.pointer(paramOut,4,true);
    // All output checks precede consumption. Empty results initialize outputs
    // to zero and report false; pending data is owned independently of the
    // guest stack. Closing the handle cannot invalidate this in-flight lease.
    const auto next=object->notification->read(match);
    PPC_STORE_U32(idOut,next?next->id:0);
    if(paramOut) PPC_STORE_U32(paramOut,next?next->parameter:0);
    ctx.r3.u64=next?1:0;
}
