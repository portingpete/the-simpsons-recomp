#pragma once
#include "native_backend.h"
#include "smaa_pipeline.h"
#include <d3d11_1.h>

namespace Simpsons::Graphics {
// Internal presentation-filter cache. SMAA remains absent until selected;
// ordinary filters do not construct its shader or lookup resources.
struct AntialiasingPipeline {
    ComPtr<ID3D11DeviceContext1> context;
    ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> fxaa,sharpen;
    std::unique_ptr<detail::SMAAPipeline> smaa;
    std::array<ComPtr<ID3D11Buffer>,3> fxaaQuality;
    std::array<ComPtr<ID3D11Buffer>,101> sharpeningStrength;
    std::shared_ptr<RenderTarget> sharpened;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11DepthStencilState> depth;
    AntialiasingPipeline(ID3D11Device*,ID3D11DeviceContext*);
};
}
