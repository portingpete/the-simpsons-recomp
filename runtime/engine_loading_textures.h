#pragma once
#include <cstdint>

struct PPCContext;
namespace Simpsons {
class EngineDriver;
namespace Graphics {class NativeBackend;}

// Bounded replacement body for original engine callback 8240A278, reached from
// 823FF8F0. Success publishes one original CPU texture wrapper and returns true.
// Failure throws, leaving output unchanged. Original dictionary/extension reads
// and insertion remain with the caller. No original SDK resource call is made.
bool readLoadingTexture(const PPCContext& incoming,uint8_t* base,
    Graphics::NativeBackend& backend,EngineDriver& driver,
    uint32_t stream,uint32_t output,uint32_t chunkLength);
}
