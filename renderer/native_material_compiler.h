#pragma once
#include "native_backend.h"
#include "material_resources.h"

namespace Simpsons::Graphics {
// Resolves four screen, two FourTapBlend, one movie, edge/AA/edgeAA pairs and the shadow-depth VS to offline-compiled
// native bytecode and creates real owned D3D11 shader objects. Other original
// materials fail before bind. There is no runtime shader compiler/translator.
class NativeMaterialCompiler final : public MaterialCompiler {
    NativeBackend& backend;
public:
    explicit NativeMaterialCompiler(NativeBackend& source):backend(source) {}
    std::unique_ptr<CompiledMaterial> compile(const MaterialRecord& original) override {
        return backend.createMaterialArtifact(original);
    }
};
}
