#pragma once
#include "runtime.h"
#include "native_window.h"

namespace Simpsons {
// Shared cancellation body for the public runtime check and hot memory bridge.
inline void checkRuntimeRunning(Runtime& runtime) {
    if(runtime.window && runtime.window->closed) runtime.requestStop("Native window closed");
    if(runtime.stopping.load(std::memory_order_acquire)) {
        std::lock_guard lock(runtime.stopMutex);
        throw Failure(runtime.stopReason);
    }
}
}
