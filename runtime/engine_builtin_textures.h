#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
struct PPCContext;
namespace Simpsons {
class Runtime;
namespace Graphics {class NativeBackend;class Texture;}
// Three process-owned images created by826FF0F8. The original parent publishes
// three additional borrowed aliases. Its global teardown826FF248 is a no-op;
// no paired release or consumer binding has yet been qualified.
class EngineBuiltinTextures {
public:
    struct View {uint32_t identity,source,output,size,format,levels;};
    EngineBuiltinTextures(Runtime&,Graphics::NativeBackend&,uint32_t context);
    ~EngineBuiltinTextures();
    void load(PPCContext&,uint8_t*);
    void commit(PPCContext&,uint8_t*);
    size_t count() const;
    bool complete() const;
    bool owns(uint32_t identity) const;
    View view(uint32_t identity) const;
    std::shared_ptr<Graphics::Texture> texture(uint32_t identity) const;
    std::vector<uint8_t> readback(uint32_t identity,uint32_t level);
    void requireReleased() const;
private:
    struct State;std::unique_ptr<State> state;
};
}
void SimpsonsNativeBuiltinImageLoad(PPCContext&,uint8_t*);
void SimpsonsNativeBuiltinImagesCommit(PPCContext&,uint8_t*);
