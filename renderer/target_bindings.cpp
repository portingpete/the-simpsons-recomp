#include "native_backend.h"

namespace Simpsons::Graphics {
void NativeBackend::requireSelectedTargets(const std::array<std::shared_ptr<RenderTarget>,4>& colors,const std::shared_ptr<DepthTarget>& depth) const {
    validateSubmissionContext();
    for(const auto& color:colors)if(color&&(!color->texture||!color->view))throw Error("Native selected color has no backing");
    if(depth&&(!depth->texture||!depth->view))throw Error("Native selected depth has no backing");
    std::array<ID3D11RenderTargetView*,8> actual{};ComPtr<ID3D11DepthStencilView> actualDepth;
    context->OMGetRenderTargets(8,actual.data(),&actualDepth);
    bool matched=actualDepth.Get()==(depth?depth->view.Get():nullptr);
    for(size_t i=0;i<actual.size();++i){matched=matched&&actual[i]==(i<4&&colors[i]?colors[i]->view.Get():nullptr);if(actual[i])actual[i]->Release();}
    if(!matched)throw Error("Actual native target selection differs from the original engine owner");
    requireOwner();
}
void NativeBackend::requireNoOutputUavs() const {
    validateSubmissionContext();
    std::array<ID3D11UnorderedAccessView*,64> outputs{};
    const UINT count=device->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?64u:8u;
    context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,count,outputs.data());
    bool present=false;
    for(auto* output:outputs)if(output){present=true;output->Release();}
    if(present)throw Error("Native draw has unsupported retained output UAVs");
}
void NativeBackend::bindTargets(const std::array<std::shared_ptr<RenderTarget>,4>& colors,const std::shared_ptr<DepthTarget>& depth) {
    requireOwner();
    std::array<ID3D11RenderTargetView*,4> views{};
    uint32_t width=0,height=0;
    auto validate=[&](ID3D11Texture2D* texture,TextureDescriptorProof& proof) {
        if(!texture) throw Error("Missing native target backing");
        const auto* proven=provenDescriptor(texture,proof);
        if(!proven) throw Error("Native target binding belongs to another graphics device");
        const D3D11_TEXTURE2D_DESC desc=*proven;
        if(desc.SampleDesc.Count!=1 || desc.SampleDesc.Quality || desc.ArraySize!=1 || desc.MipLevels!=1)
            throw Error("Native target binding exceeds the single-sample engine profile");
        if(width && (width!=desc.Width || height!=desc.Height)) throw Error("Native target attachment dimensions differ");
        width=desc.Width;height=desc.Height;
    };
    for(size_t i=0;i<colors.size();++i) if(colors[i]) {
        validate(colors[i]->texture.Get(),colors[i]->descriptorProof);
        if(!colors[i]->view) throw Error("Missing native color target view");
        for(size_t j=0;j<i;++j) if(colors[j] && colors[j]->texture.Get()==colors[i]->texture.Get())
            throw Error("A native color resource cannot occupy multiple target slots");
        views[i]=colors[i]->view.Get();
    }
    if(depth) {validate(depth->texture.Get(),depth->descriptorProof);if(!depth->view) throw Error("Missing native depth target view");}
    context->OMSetRenderTargets(UINT(views.size()),views.data(),depth?depth->view.Get():nullptr);
    // D3D11 target selection has no HRESULT. Verify the actual retained context
    // references so a rejected/hazard-nulled binding never reports success.
    std::array<ID3D11RenderTargetView*,4> actual{};
    ID3D11DepthStencilView* actualDepth{};
    context->OMGetRenderTargets(UINT(actual.size()),actual.data(),&actualDepth);
    bool matched=actualDepth==(depth?depth->view.Get():nullptr);
    for(size_t i=0;i<actual.size();++i) {
        matched=matched && actual[i]==views[i];
        if(actual[i]) actual[i]->Release();
    }
    if(actualDepth) actualDepth->Release();
    if(!matched) throw Error("D3D11 did not retain the requested engine target bindings");
    requireOwner();
}
void NativeBackend::clearBindings() {
    requireOwner();invalidateScreenReplacement();context->ClearState();requireOwner();
}
}
