#pragma once
#include <memory>
#include <cstdint>
struct PPCContext;
namespace Simpsons {
class Runtime;namespace Graphics{class NativeBackend;}
class EngineParticles {
public:
    EngineParticles(Runtime&,Graphics::NativeBackend&);
    ~EngineParticles();
    void begin(PPCContext&,unsigned char*);
    void finish(PPCContext&,unsigned char*);
    // Successful native submissions after the complete original CPU upload.
    uint64_t drawCount() const;
private:
    struct State;std::unique_ptr<State> state;
};
}
