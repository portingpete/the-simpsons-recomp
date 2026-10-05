#pragma once
#include <windows.h>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>

namespace Simpsons::Platform {
struct NotificationError : std::runtime_error {using std::runtime_error::runtime_error;};
struct Notification {uint32_t id{},parameter{};bool operator==(const Notification&) const=default;};
class NativeNotifications;
class NotificationListener {
    friend class NativeNotifications;
    NotificationListener(uint64_t mask,uint32_t version);
    void enqueue(Notification);
    const uint64_t mask;
    const uint32_t version;
    HANDLE event{};
    std::mutex mutex;
    std::deque<Notification> pending;
public:
    ~NotificationListener();
    NotificationListener(const NotificationListener&)=delete;
    NotificationListener& operator=(const NotificationListener&)=delete;
    // The caller owns and must close this duplicate. It refers to the same
    // manual-reset event, with lifetime independent of this borrowed C++ object.
    HANDLE duplicateWaitHandle() const;
    std::optional<Notification> read(uint32_t match=0);
    size_t pendingCount();
};
// Host values and references only: no guest pointers, console SDK objects or
// invented initial notifications. Native platform producers publish real
// transitions; creating a listener never asserts UI/profile/input availability.
class NativeNotifications {
    std::mutex mutex;
    std::vector<std::weak_ptr<NotificationListener>> listeners;
public:
    std::shared_ptr<NotificationListener> create(uint64_t mask,uint32_t maxVersion);
    void publish(Notification);
};
}
