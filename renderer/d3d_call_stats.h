#pragma once
#include <cstdint>
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace Simpsons::Graphics {
// Opt-in per-method D3D11 call counters (SIMPSONS_D3D_CALL_STATS=1); see d3d_call_stats.cpp.
bool d3dCallStatsRequested();
void installD3DCallStats(ID3D11DeviceContext* context,ID3D11Device* device);
// Prints the counters averaged over `frames` and resets them.
void dumpD3DCallStats(uint64_t frames,const char* label);
}
