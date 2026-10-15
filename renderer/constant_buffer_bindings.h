#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <cstdint>

namespace Simpsons::Graphics::detail {
using Microsoft::WRL::ComPtr;
enum class ConstantBufferStage {Vertex,Pixel};
enum class ConstantBufferView {Full,Ranged};
struct ConstantBufferBinding {
    ComPtr<ID3D11Buffer> buffer;
    UINT firstConstant{};
    UINT numConstants{};
    ConstantBufferView view{ConstantBufferView::Full};
};
struct ConstantBufferSlice {
    ConstantBufferBinding binding;
    UINT logicalBytes{};
    uint64_t generation{};
    bool arena{};
};
// The caller supplies its already proven immediate Context1. Capture every
// reported range verbatim, including zero-length views; capability admission
// for the arena is independent of range preservation.
inline ConstantBufferBinding captureConstantBuffer(ID3D11DeviceContext* context,ID3D11DeviceContext1* ranged,
    ConstantBufferStage stage,UINT slot) noexcept {
    ConstantBufferBinding result;
    if(ranged) {
        result.view=ConstantBufferView::Ranged;
        if(stage==ConstantBufferStage::Vertex)
            ranged->VSGetConstantBuffers1(slot,1,result.buffer.GetAddressOf(),&result.firstConstant,&result.numConstants);
        else ranged->PSGetConstantBuffers1(slot,1,result.buffer.GetAddressOf(),&result.firstConstant,&result.numConstants);
    } else if(stage==ConstantBufferStage::Vertex)context->VSGetConstantBuffers(slot,1,result.buffer.GetAddressOf());
    else context->PSGetConstantBuffers(slot,1,result.buffer.GetAddressOf());
    return result;
}
inline void bindConstantBuffer(ID3D11DeviceContext* context,ID3D11DeviceContext1* ranged,
    ConstantBufferStage stage,UINT slot,const ConstantBufferBinding& binding) noexcept {
    auto* buffer=binding.buffer.Get();
    // Full and null bindings must also restore on Context1 implementations
    // without offsetting support, where a ranged Set1 call can be dropped.
    const bool whole=binding.firstConstant==0 && binding.numConstants==4096;
    if(buffer && binding.view==ConstantBufferView::Ranged && ranged && !whole) {
        if(stage==ConstantBufferStage::Vertex)
            ranged->VSSetConstantBuffers1(slot,1,&buffer,&binding.firstConstant,&binding.numConstants);
        else ranged->PSSetConstantBuffers1(slot,1,&buffer,&binding.firstConstant,&binding.numConstants);
    } else if(stage==ConstantBufferStage::Vertex)context->VSSetConstantBuffers(slot,1,&buffer);
    else context->PSSetConstantBuffers(slot,1,&buffer);
}
inline bool constantBufferBindingMatches(const ConstantBufferBinding& actual,const ConstantBufferBinding& expected) noexcept {
    if(actual.buffer.Get()!=expected.buffer.Get())return false;
    if(actual.view==expected.view)return actual.view==ConstantBufferView::Full ||
        (actual.firstConstant==expected.firstConstant && actual.numConstants==expected.numConstants);
    // A legacy whole-buffer binding is reported by GetConstantBuffers1 as the
    // full 4096-constant shader view, independently of the physical allocation.
    const auto& ranged=actual.view==ConstantBufferView::Ranged?actual:expected;
    return ranged.firstConstant==0 && ranged.numConstants==4096;
}
}
