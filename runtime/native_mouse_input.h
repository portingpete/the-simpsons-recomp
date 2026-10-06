#pragma once
#include <cstdint>

namespace Simpsons::Platform {
// Unscaled relative Win32 raw-input counts: right is +x, down is +y.
struct NativeMouseMotion {
    bool active=false;
    int32_t x=0,y=0;
    bool operator==(const NativeMouseMotion&) const=default;
};
}
