#include "runtime/stall_profiler.h"
#include <windows.h>
#include <cstdio>
#include <type_traits>

int main() {
    using namespace Simpsons::StallProfiler;
    static_assert(!enabled);
    static_assert(std::is_empty_v<Scope> && std::is_empty_v<ThreadScope>);
    static_assert(std::is_trivially_destructible_v<Scope>);
    SetLastError(0x12345678);
    {
        [[maybe_unused]] ThreadScope thread(nullptr,true);
        Scope call(Section::Rendering,"compiled-out-call",nullptr,123,456);
        beginWait(nullptr,"compiled-out-wait",789,111);
        endWait();
        call.finish();
        frameBoundary();
    }
    if (GetLastError()!=0x12345678) return 1;
    std::puts("Stall profiler compiled out: empty scopes, no runtime linkage, state preserved");
    return 0;
}
