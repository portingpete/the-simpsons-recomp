#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace Simpsons {

// Per-present observation of the original RwFrame LTM attached to the
// controllable 43-bone RpAtomic reached in Land of Chocolate. It never treats
// an input receipt, a local bone pose, or a stale render as a world position.
class PlayerTelemetry {
public:
    void observeCharacterFrame(uint32_t object,uint32_t boneCount,
                               const std::array<float,16>& worldMatrix,
                               bool frameValidated) noexcept;
    // Called on EVERY completed presentation, even when no frame is captured.
    // Consumes the observations so a missing character cannot reuse old data.
    std::string takeJson();
    // Same consumption without any allocation/formatting, for frames that will
    // not capture. Keeps per-frame overhead steady when no capture is taken.
    void discard() noexcept {seen_=ambiguous_=invalid_=false;object_=0;position_={};}
private:
    bool seen_{},ambiguous_{},invalid_{};
    uint32_t object_{};
    std::array<float,3> position_{};
};

}
