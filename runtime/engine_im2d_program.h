#pragma once
#include <cstdint>
#include <array>
#include <vector>
namespace Simpsons {
class Runtime;
class EngineCpuCalls;
struct Im2DProgramCache {
    struct Entry {std::array<uint8_t,436> key{};std::array<uint8_t,168> macros{};};
    uint32_t thread{};uint64_t hits{},builds{};
    std::vector<Entry> entries;
};
// Execute the original CPU source/macro builders and admit only the two
// recovered programs. Native compiled shaders live in the graphics backend;
// no console compiler, COM object or guest shader-cache allocation is emulated.
void qualifyIm2DProgram(Runtime&,EngineCpuCalls&,uint8_t*,bool textured);
}
