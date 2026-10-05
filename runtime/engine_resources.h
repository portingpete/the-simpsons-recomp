#pragma once
#include "renderer/native_backend.h"
#include <memory>

namespace Simpsons {
namespace Graphics {class DeclarationRecord;}
// First-start bridge for the actual original scratch owner. This scope retains
// real native backing and immutable declarations. Original CPU constructors and
// cleanup still run; native IDs are opaque and never SDK object pointers.
class EngineScratchResources {
public:
    explicit EngineScratchResources(std::shared_ptr<Graphics::Buffer> index);
    ~EngineScratchResources();
    EngineScratchResources(const EngineScratchResources&)=delete;
    EngineScratchResources& operator=(const EngineScratchResources&)=delete;
    void validateCreated(uint32_t index,uint32_t declaration) const;
    void requireReleased() const;
    void requireDeclaration(uint32_t id) const;
    std::shared_ptr<const Graphics::DeclarationRecord> declaration(uint32_t id) const;
    bool ownsIndex(uint32_t id) const;
private:
    struct Owner;
    std::unique_ptr<Owner> owner;
};
}
