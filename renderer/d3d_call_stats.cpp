// Opt-in diagnostic (SIMPSONS_D3D_CALL_STATS=1): counts every ID3D11DeviceContext and
// ID3D11Device method call by vtable slot, so the per-frame D3D call mix can be read
// from the log instead of guessed from sampling. It patches the two vtables with
// 16-byte stubs that increment a counter and jump to the original method, so no
// signature knowledge is needed and no argument is touched. Never enabled by default.
#include "d3d_call_stats.h"
#include <windows.h>
#include <d3d11.h>
#include <algorithm>
#include <cstdio>
#include <vector>
#include "d3d_call_names.inc"

extern "C" {
uint64_t gD3DContextCounts[192];
void* gD3DContextOriginal[192];
uint64_t gD3DDeviceCounts[64];
void* gD3DDeviceOriginal[64];
void d3dContextStubs();
void d3dDeviceStubs();
}
__asm__(
    ".att_syntax\n"
    ".text\n"
    ".p2align 4\n"
    ".globl d3dContextStubs\n"
    "d3dContextStubs:\n"
    ".set slot,0\n"
    ".rept 192\n"
    "    incq gD3DContextCounts+8*slot(%rip)\n"
    "    jmpq *gD3DContextOriginal+8*slot(%rip)\n"
    "    .p2align 4\n"
    "    .set slot,slot+1\n"
    ".endr\n"
    ".p2align 4\n"
    ".globl d3dDeviceStubs\n"
    "d3dDeviceStubs:\n"
    ".set slot,0\n"
    ".rept 64\n"
    "    incq gD3DDeviceCounts+8*slot(%rip)\n"
    "    jmpq *gD3DDeviceOriginal+8*slot(%rip)\n"
    "    .p2align 4\n"
    "    .set slot,slot+1\n"
    ".endr\n"
    ".intel_syntax noprefix\n");

namespace Simpsons::Graphics {
namespace {
bool installed=false;
void patch(void* object,size_t slots,void** original,void (*stubs)()) {
    void** vtable=*reinterpret_cast<void***>(object);
    DWORD previous{};
    if(!VirtualProtect(vtable,slots*sizeof(void*),PAGE_READWRITE,&previous))return;
    for(size_t i=0;i<slots;++i) {
        original[i]=vtable[i];
        vtable[i]=reinterpret_cast<uint8_t*>(stubs)+16*i;
    }
    VirtualProtect(vtable,slots*sizeof(void*),previous,&previous);
}
}
bool d3dCallStatsRequested() {
    const char* text=std::getenv("SIMPSONS_D3D_CALL_STATS");
    return text&&*text&&*text!='0';
}
void installD3DCallStats(ID3D11DeviceContext* context,ID3D11Device* device) {
    if(installed||!context||!device)return;
    installed=true;
    patch(context,std::size(kContextMethodNames),gD3DContextOriginal,d3dContextStubs);
    patch(device,std::size(kDeviceMethodNames),gD3DDeviceOriginal,d3dDeviceStubs);
}
void dumpD3DCallStats(uint64_t frames,const char* label) {
    if(!installed||!frames)return;
    struct Row {uint64_t count;const char* name;bool device;};
    std::vector<Row> rows;uint64_t contextTotal=0,deviceTotal=0;
    for(size_t i=0;i<std::size(kContextMethodNames);++i) {contextTotal+=gD3DContextCounts[i];if(gD3DContextCounts[i])rows.push_back({gD3DContextCounts[i],kContextMethodNames[i],false});}
    for(size_t i=0;i<std::size(kDeviceMethodNames);++i) {deviceTotal+=gD3DDeviceCounts[i];if(gD3DDeviceCounts[i])rows.push_back({gD3DDeviceCounts[i],kDeviceMethodNames[i],true});}
    std::sort(rows.begin(),rows.end(),[](const Row& a,const Row& b){return a.count>b.count;});
    std::fprintf(stderr,"[D3D CALL STATS] %s frames=%llu context_calls/frame=%.0f device_calls/frame=%.0f\n",label,
        static_cast<unsigned long long>(frames),double(contextTotal)/double(frames),double(deviceTotal)/double(frames));
    for(size_t i=0;i<rows.size()&&i<40;++i)
        std::fprintf(stderr,"[D3D CALL STATS]   %s::%s %.1f/frame\n",rows[i].device?"Device":"Context",rows[i].name,double(rows[i].count)/double(frames));
    std::fflush(stderr);
    std::fill(std::begin(gD3DContextCounts),std::end(gD3DContextCounts),0);
    std::fill(std::begin(gD3DDeviceCounts),std::end(gD3DDeviceCounts),0);
}
}
