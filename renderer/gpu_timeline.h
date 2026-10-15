#pragma once
#include <cstdint>
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace Simpsons::Graphics::GpuTimeline {
// Opt-in GPU attribution (SIMPSONS_GPU_PROFILE=1 together with SIMPSONS_STALL_PROFILE=1).
// Every rendering/presentation stall-profiler scope boundary on the graphics owner
// thread ends a D3D11 timestamp, so the GPU time between consecutive boundaries is
// charged to the innermost such scope that was open when its commands were issued. Diagnostic only: thousands of
// timestamps per frame add CPU cost, and time the GPU spends idle waiting for the
// CPU is charged to the scope in which it idled.
bool requested();
// Starts recording on this device's immediate context; later devices are ignored.
void attach(ID3D11Device* device,ID3D11DeviceContext* context);
// Ends the device's current frame after a presentation and resolves older frames
// without waiting; summaries go to stderr every 120 resolved frames.
void frame(ID3D11Device* device);
}
