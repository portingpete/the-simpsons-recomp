#pragma once
#include "renderer/material_resources.h"
#include <memory>

namespace Simpsons {
namespace Graphics {class NativeBackend;}
// Native CPU shader resources own the exact immutable original records. Native
// compilation is deferred until needed; creation never certifies a draw/bind.
class EngineMaterials {
public:
    explicit EngineMaterials(Graphics::NativeBackend&);
    ~EngineMaterials();
    EngineMaterials(const EngineMaterials&)=delete;
    EngineMaterials& operator=(const EngineMaterials&)=delete;
    uint32_t create(uint8_t* base,uint32_t source,uint32_t output,Graphics::MaterialStage);
    uint32_t release(uint8_t* base,uint32_t id,Graphics::MaterialStage);
    const Graphics::CompiledMaterial& prepare(uint32_t id,Graphics::MaterialStage);
    Graphics::MaterialCapability capability(uint32_t id) const;
    void requireOwned(uint32_t id,Graphics::MaterialStage) const;
    size_t liveCount() const;
    void requireReleased() const;
private:
    struct Owner;
    std::unique_ptr<Owner> owner;
};
}
