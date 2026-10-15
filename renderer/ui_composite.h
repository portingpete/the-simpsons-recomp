#pragma once
#include "native_backend.h"
namespace Simpsons::Graphics {
// Resource routing and GPU submission lifetime, not a guest SDK fence or a
// claim that scene color/motion/depth are temporally paired. The runtime owner
// must qualify the reconstruction boundary before requesting this handoff.
class NativeUiCompositeBundle {
    friend class NativeBackend;
    friend struct NativeUiCompositeProbe;
    struct Resources {
        std::shared_ptr<RenderTarget> sceneColor,outputColor;
        std::shared_ptr<DepthTarget> sceneDepth,outputDepth;
        uint32_t logicalWidth{},logicalHeight{},sourceWidth{},sourceHeight{},outputWidth{},outputHeight{};
        std::array<float,2> sourceJitter{};
        std::shared_ptr<RenderTarget> sceneColorCopy,outputColorCopy;
        std::shared_ptr<DepthTarget> sceneDepthCopy,outputDepthCopy;
        uint64_t copyGeneration{};
    };
    std::shared_ptr<Resources> resources;
    std::shared_ptr<NativeCopySubmission> completion;
};
}
