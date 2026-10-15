#pragma once
#include "native_backend.h"

namespace Simpsons::Graphics::detail {
// Internal SMAA 1x resources. The backend calls draw only inside its isolated
// presentation context state; this class never owns engine or guest state.
class SMAAPipeline {
    friend struct SMAAPipelineProbe;
    struct Surface {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> target;
        ComPtr<ID3D11ShaderResourceView> sampled;
    };
    const ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Texture2D> areaTexture,searchTexture;
    ComPtr<ID3D11ShaderResourceView> areaView,searchView;
    ComPtr<ID3D11VertexShader> edgeVertex,weightVertex,neighborhoodVertex;
    ComPtr<ID3D11PixelShader> edgePixel,weightPixel,neighborhoodPixel;
    ComPtr<ID3D11SamplerState> linearSampler,pointSampler;
    Surface edges,weights;
    ComPtr<ID3D11Buffer> metrics;
    UINT width{},height{};
public:
    explicit SMAAPipeline(ID3D11Device* owner);
    void prepare(UINT newWidth,UINT newHeight);
    void draw(ID3D11DeviceContext*,ID3D11ShaderResourceView* source,ID3D11RenderTargetView* output);
};
}
