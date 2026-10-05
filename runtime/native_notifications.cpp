#include "native_notifications.h"
#include <algorithm>

namespace Simpsons::Platform {
NotificationListener::NotificationListener(uint64_t categories,uint32_t maximum):mask(categories),version(maximum) {
    event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!event) throw NotificationError("Native notification wait-event creation failed");
}
NotificationListener::~NotificationListener() {if(event) CloseHandle(event);}
HANDLE NotificationListener::duplicateWaitHandle() const {
    HANDLE result=nullptr;
    if(!DuplicateHandle(GetCurrentProcess(),event,GetCurrentProcess(),&result,0,FALSE,DUPLICATE_SAME_ACCESS))
        throw NotificationError("Native notification wait-event duplication failed");
    return result;
}
void NotificationListener::enqueue(Notification notification) {
    // Notification identifiers contain local-id[15:0], version[24:16] and
    // category[30:25]. Do not use compiler-dependent host bitfield packing.
    if(!(mask&(uint64_t(1)<<((notification.id>>25)&63))) || ((notification.id>>16)&511)>version) return;
    std::lock_guard lock(mutex);
    pending.push_back(notification); // Allocate before signalling a consumer.
    if(!SetEvent(event)) {
        pending.pop_back(); // Readers cannot observe this unpublished element.
        throw NotificationError("Native notification wakeup failed");
    }
}
std::optional<Notification> NotificationListener::read(uint32_t match) {
    std::lock_guard lock(mutex);
    auto at=match?std::find_if(pending.begin(),pending.end(),[&](Notification n){return n.id==match;}):pending.begin();
    if(at==pending.end()) return std::nullopt;
    const Notification result=*at;
    // A filter miss never resets the event: unrelated pending notifications
    // remain waitable. Keep the queue intact if an OS reset fails.
    if(pending.size()==1 && !ResetEvent(event)) throw NotificationError("Native notification event reset failed");
    pending.erase(at);return result;
}
size_t NotificationListener::pendingCount() {std::lock_guard lock(mutex);return pending.size();}
std::shared_ptr<NotificationListener> NativeNotifications::create(uint64_t mask,uint32_t maxVersion) {
    if(maxVersion>511) throw NotificationError("Native notification version exceeds the identifier field");
    auto listener=std::shared_ptr<NotificationListener>(new NotificationListener(mask,maxVersion));
    std::lock_guard lock(mutex);
    std::erase_if(listeners,[](const auto& old){return old.expired();});
    listeners.push_back(listener);return listener;
}
void NativeNotifications::publish(Notification notification) {
    if(!notification.id || (notification.id&0x80000000))
        throw NotificationError("Invalid native notification identifier");
    // Serialize sources to preserve publication order at every listener. A
    // closed listener expires; a concurrent read/close retains its own lease.
    // Allocation failure during broadcast is explicit and may follow delivery
    // to earlier listeners; there is no claim of a cross-listener rollback.
    std::lock_guard lock(mutex);
    std::erase_if(listeners,[](const auto& old){return old.expired();});
    for(const auto& weak:listeners) if(auto listener=weak.lock()) listener->enqueue(notification);
}
}
