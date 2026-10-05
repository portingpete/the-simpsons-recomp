#pragma once
#include "renderer/declaration_resources.h"
#include <cstdint>
#include <memory>
struct PPCContext;
namespace Simpsons {
class Runtime;
// Original quad's two immutable CPU declarations and the second FX family's
// global cached declaration. No native input layout,
// shader binding, vertex conversion or draw is implied by this ownership.
class EngineQuadDeclarations {
public:
    EngineQuadDeclarations(Runtime&,uint32_t context);
    ~EngineQuadDeclarations();
    void create(PPCContext&,uint8_t*);
    void createPost(PPCContext&,uint8_t*);
    void release(PPCContext&,uint8_t*);
    bool owns(uint32_t id) const;
    size_t count() const;
    std::shared_ptr<const Graphics::DeclarationRecord> record(uint32_t id) const;
    void requireReleased() const;
private:
    struct State;
    std::unique_ptr<State> state;
};
}
void SimpsonsNativeQuadDeclarationCreate(PPCContext&,uint8_t*);
void SimpsonsNativePostDeclarationCreate(PPCContext&,uint8_t*);
bool SimpsonsNativeGraphicsResourceRelease(PPCContext&,uint8_t*);
bool SimpsonsOriginalMeshDeclarationDestroy(PPCContext&,uint8_t*);
