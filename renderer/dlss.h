#pragma once
#include "native_backend.h"
#include <d3d11_1.h>
#include <filesystem>
#include <string>

namespace Simpsons::Graphics {
struct DlssCapability {
    bool available{};
    uint32_t ngxResult{},supportReasons{},vendorId{},deviceId{};
    uint32_t minimumDriverMajor{},minimumDriverMinor{};
    std::wstring adapterName;
    std::string reason;
};
struct DlssExtent {
    uint32_t renderWidth{},renderHeight{},outputWidth{},outputHeight{};
};
struct DlssFrame {
    // Render pixels, +X right / +Y down. Motion is previous-current without jitter.
    std::array<float,2> jitter{};
    float elapsedMilliseconds{};
    bool reset{},depthInverted=true;
    // Renderer-owned coverage: reject submission unless actual geometry supplied motion.
    uint64_t geometryDraws{},motionDraws{};
};
class NvidiaDlss {
public:
    NvidiaDlss(ID3D11Device*,ID3D11DeviceContext*,const std::filesystem::path& applicationDirectory);
    ~NvidiaDlss();
    NvidiaDlss(const NvidiaDlss&)=delete;
    NvidiaDlss& operator=(const NvidiaDlss&)=delete;
    const DlssCapability& capability() const;
    DlssExtent optimalExtent(Antialiasing,uint32_t outputWidth,uint32_t outputHeight);
    // Freeze before scene allocation. Persistent feature and resources avoid per-frame recreation.
    void create(Antialiasing,const DlssExtent&,bool depthInverted=true);
    ID3D11Texture2D* evaluate(ID3D11Texture2D* color,ID3D11Texture2D* depth,
        ID3D11Texture2D* motion,ID3D11Texture2D* biasCurrentColor,const DlssFrame&);
    ID3D11ShaderResourceView* outputView() const;
    const DlssExtent& extent() const;
    uint64_t evaluations() const;
    // Caller must prove its immediate GPU queue idle before disposing an evaluated feature.
    void shutdownAfterGpuIdle();
private:
    struct State;
    std::unique_ptr<State> state;
};
}
