#pragma once
#include "dlss.h"
#include "shadow_mesh.h"
#include "temporal_history.h"
#include <d3d11_1.h>

namespace Simpsons::Graphics {
class TemporalScenePipeline;
class TemporalGeometryDraw {
    friend class TemporalScenePipeline;
    std::weak_ptr<TemporalScenePipeline> scene;
    TemporalGeometryIdentity identity;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> previous,controls,previousBooleans;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11RenderTargetView> color,motion,bias;
    ComPtr<ID3D11DepthStencilView> depth;
    bool conservative{},prepared{};
    UINT sampleMask{};
public:
    // Distinct retained recording items own distinct frozen transform/bone/morph buffers.
    void prepare(ID3D11DeviceContext*,std::span<const TemporalRegister>,bool skinned=false);
    void packedOutput(ID3D11RenderTargetView*,bool conservative);
    void bind(ID3D11DeviceContext*);
};
class ScopedTemporalGeometryDraw {
    struct State;
    std::unique_ptr<State> state;
public:
    ScopedTemporalGeometryDraw(ID3D11DeviceContext*,const std::shared_ptr<TemporalGeometryDraw>&);
    ~ScopedTemporalGeometryDraw();
};
class TemporalScenePipeline : public std::enable_shared_from_this<TemporalScenePipeline> {
    friend class TemporalGeometryDraw;
    friend struct NativeUiCompositeProbe;
    // Fixture-only read seam: immutable ownership of the reused 16-byte control.
    ComPtr<ID3D11Buffer> uiBackgroundJitterConstants() const;
    struct State;
    std::unique_ptr<State> state;
public:
    TemporalScenePipeline(ID3D11Device*,ID3D11DeviceContext*,const DlssExtent&);
    ~TemporalScenePipeline();
    void registerWorldTargets(ID3D11Texture2D*,ID3D11DepthStencilView*);
    void begin(ID3D11Texture2D*,ID3D11RenderTargetView*,ID3D11DepthStencilView*,uint64_t camera,uint64_t cameraGeneration,bool reset);
    bool matches(ID3D11Texture2D*,ID3D11DepthStencilView*) const;
    bool active() const;
    bool hasGeometry() const;
    bool reconstructed() const;
    void markReconstructed();
    std::shared_ptr<TemporalGeometryDraw> draw(ID3D11Texture2D*,ID3D11RenderTargetView*,ID3D11DepthStencilView*,
        const ShadowMeshDraw&,uint32_t shader,uint32_t kind,bool recorded);
    DlssFrame frame() const;
    ID3D11Texture2D* motion() const;
    ID3D11Texture2D* biasCurrent() const;
    void copyOutput(ID3D11ShaderResourceView* reconstructed,ID3D11ShaderResourceView* original,ID3D11RenderTargetView*,uint32_t width,uint32_t height,std::array<float,2> sourceJitter={});
    void copyUiBackground(ID3D11ShaderResourceView*,ID3D11RenderTargetView*,uint32_t width,uint32_t height,std::array<float,2> sourceJitter={});
    void finishFrame();
    void reset();
    void rejectUntrackedWorldDraw(const char* kind);
};
}
