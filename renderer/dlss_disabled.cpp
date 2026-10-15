#include "dlss.h"

namespace Simpsons::Graphics {
// Link-compatible boundary for preserved, unreachable research sources.
// Normal builds never include NGX headers, create an NGX/device feature,
// inspect an NVIDIA adapter, load its DLL, or construct provider objects.
struct NvidiaDlss::State {
    DlssCapability capability;
    DlssExtent extent;
    State(){capability.reason="DLAA/DLSS are excluded from this build";}
};
NvidiaDlss::NvidiaDlss(ID3D11Device*,ID3D11DeviceContext*,const std::filesystem::path&)
    :state(std::make_unique<State>()){}
NvidiaDlss::~NvidiaDlss()=default;
const DlssCapability& NvidiaDlss::capability() const {return state->capability;}
DlssExtent NvidiaDlss::optimalExtent(Antialiasing,uint32_t,uint32_t) {
    throw Error("DLAA/DLSS are excluded from this build");
}
void NvidiaDlss::create(Antialiasing,const DlssExtent&,bool) {
    throw Error("DLAA/DLSS are excluded from this build");
}
ID3D11Texture2D* NvidiaDlss::evaluate(ID3D11Texture2D*,ID3D11Texture2D*,ID3D11Texture2D*,ID3D11Texture2D*,const DlssFrame&) {
    throw Error("DLAA/DLSS are excluded from this build");
}
ID3D11ShaderResourceView* NvidiaDlss::outputView() const {return nullptr;}
const DlssExtent& NvidiaDlss::extent() const {return state->extent;}
uint64_t NvidiaDlss::evaluations() const {return 0;}
void NvidiaDlss::shutdownAfterGpuIdle() {}
static_assert(!temporalAntialiasing(Antialiasing::DLAA));
static_assert(!validAntialiasing(Antialiasing::DLAA));
static_assert(validAntialiasing(Antialiasing::SMAA)&&!validAntialiasing(Antialiasing(8)));
}
