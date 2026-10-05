#include "native_backend.h"

namespace Simpsons::Graphics {
namespace {
// Get* returns owned COM references. Keep them through validation/mutation and
// release on every exit, including a rejected preflight. No guest refs exist here.
template<class T,size_t N> struct References {
    std::array<T*,N> values{};
    References()=default;
    References(const References&)=delete;
    ~References() {for(auto* value:values) if(value) value->Release();}
};
struct ShaderReads {
    References<ID3D11ShaderResourceView,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> stages[6];
    explicit ShaderReads(ID3D11DeviceContext* context) {
        constexpr UINT count=D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
        context->PSGetShaderResources(0,count,stages[0].values.data());
        context->VSGetShaderResources(0,count,stages[1].values.data());
        context->GSGetShaderResources(0,count,stages[2].values.data());
        context->HSGetShaderResources(0,count,stages[3].values.data());
        context->DSGetShaderResources(0,count,stages[4].values.data());
        context->CSGetShaderResources(0,count,stages[5].values.data());
    }
};
DXGI_FORMAT colorFormat(TargetFormat format) {
    switch(format) {
    case TargetFormat::RGBA8:return DXGI_FORMAT_R8G8B8A8_UNORM;
    case TargetFormat::RGB10A2:return DXGI_FORMAT_R10G10B10A2_UNORM;
    case TargetFormat::RGBA16Float:return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case TargetFormat::RGBA32Float:return DXGI_FORMAT_R32G32B32A32_FLOAT;
    default:throw Error("Native binding reset has an unsupported color format");
    }
}
void validateTarget(ID3D11Texture2D* texture,ID3D11View* view,ID3D11Device* device,
                    uint32_t width,uint32_t height,DXGI_FORMAT format,UINT bindFlags) {
    if(!texture || !view) throw Error("Native binding reset requires backed color and depth targets");
    ComPtr<ID3D11Device> source;texture->GetDevice(&source);
    ComPtr<ID3D11Device> viewDevice;view->GetDevice(&viewDevice);
    ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
    if(source.Get()!=device || viewDevice.Get()!=device || resource.Get()!=texture)
        throw Error("Native binding reset target/view belongs to another resource or device");
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    // One complete subresource; no UAV or stream-output alias can exist for it.
    if(!width || !height || desc.Width!=width || desc.Height!=height || desc.Format!=format ||
       desc.MipLevels!=1 || desc.ArraySize!=1 || desc.SampleDesc.Count!=1 || desc.SampleDesc.Quality ||
       desc.Usage!=D3D11_USAGE_DEFAULT || desc.BindFlags!=bindFlags || desc.CPUAccessFlags || desc.MiscFlags)
        throw Error("Native binding reset target metadata exceeds the owned single-sample profile");
}
}

void NativeBackend::clearEngineTexture(uint32_t stage) {
    validateSubmissionContext();
    if(stage>=8) throw Error("Native engine texture clear stage is outside zero through seven");
    // Binding-time unbind only: the logical texture/raster owner and every
    // other context binding survive. D3D11 retains outstanding submitted use.
    // This null setter cannot create an input/output resource conflict.
    ID3D11ShaderResourceView* none{};
    context->PSSetShaderResources(stage,1,&none);
    ComPtr<ID3D11ShaderResourceView> actual;
    context->PSGetShaderResources(stage,1,&actual);
    requireOwner();
    if(actual) throw Error("D3D11 did not clear the requested engine texture stage");
}

void NativeBackend::requireEngineTexture(uint32_t stage,const std::shared_ptr<Texture>& texture) const {
    validateSubmissionContext();
    if(stage>=8) throw Error("Native engine texture stage is outside zero through seven");
    if(texture) validateTexture(texture);
    ComPtr<ID3D11ShaderResourceView> actual;
    context->PSGetShaderResources(stage,1,&actual);
    if(actual.Get()!=(texture?texture->view.Get():nullptr))
        throw Error("Actual native engine texture binding differs from its original cache");
}

void NativeBackend::bindEngineTexture(uint32_t stage,const std::shared_ptr<Texture>& texture) {
    validateSubmissionContext();
    if(stage>=8) throw Error("Native engine texture stage is outside zero through seven");
    // Storage validation proves BindFlags==SHADER_RESOURCE exclusively, and
    // that this exact view covers the owned texture's declared mip interval.
    // Thus the setter cannot implicitly remove an output binding.
    validateTexture(texture);
    auto* view=texture->view.Get();
    context->PSSetShaderResources(stage,1,&view);
    requireEngineTexture(stage,texture);
}
void NativeBackend::bindSpriteQueryTexture(const std::shared_ptr<RenderTarget>& texture) {
    validateSubmissionContext();
    if(!texture||texture->width!=64||texture->height!=8||texture->format!=TargetFormat::RGB10A2||!texture->sampledView)
        throw Error("Sprite query requires the original64x8 color texture");
    validateTarget(texture->texture.Get(),texture->view.Get(),device.Get(),64,8,DXGI_FORMAT_R10G10B10A2_UNORM,
                   D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
    References<ID3D11RenderTargetView,8> outputs;context->OMGetRenderTargets(8,outputs.values.data(),nullptr);
    for(auto* view:outputs.values)if(view){ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
        if(resource.Get()==texture->texture.Get())throw Error("Sprite query texture still bound as output");}
    D3D11_SAMPLER_DESC desc{};desc.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    desc.AddressU=desc.AddressV=D3D11_TEXTURE_ADDRESS_MIRROR;desc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.MaxAnisotropy=1;desc.ComparisonFunc=D3D11_COMPARISON_NEVER;
    ComPtr<ID3D11SamplerState> sampler;
    if(FAILED(device->CreateSamplerState(&desc,&sampler)))throw Error("Sprite query sampler creation failed");
    auto* resource=texture->sampledView.Get();auto* bound=sampler.Get();
    context->PSSetShaderResources(1,1,&resource);context->PSSetSamplers(1,1,&bound);
}
NativeScreenBatchReceipt NativeBackend::finishSpriteBatch(bool clearTextures){
    validateSubmissionContext();
    if(screenBatchRetirements==UINT64_MAX)throw Error("Native screen batch retirement serial exhausted");
    NativeScreenBatchReceipt receipt;UINT vertexClasses=0,pixelClasses=0;
    context->VSGetShader(&receipt.retainedVertex,nullptr,&vertexClasses);
    context->PSGetShader(&receipt.retainedPixel,nullptr,&pixelClasses);
    if(vertexClasses||pixelClasses)throw Error("Original sprite batch cannot retain dynamic shader classes");
    if(clearTextures){clearEngineTexture(0);clearEngineTexture(1);}
    context->IASetInputLayout(nullptr);
    completedScreenBatchRetirement=++screenBatchRetirements;
    receipt.owner=this;receipt.batch=completedScreenBatchRetirement;receipt.epoch=screenShaderEpoch;receipt.draw=screenDraws;
    requireScreenBatchRetirement(receipt);return receipt;
}
void NativeBackend::requireScreenBatchRetirement(const NativeScreenBatchReceipt& receipt) const {
    requireOwner();
    if(receipt.owner!=this||!receipt.batch||receipt.batch!=completedScreenBatchRetirement||
       receipt.batch!=screenBatchRetirements||receipt.epoch!=screenShaderEpoch||receipt.draw!=screenDraws)
        throw Error("Native screen batch retirement receipt is stale or foreign");
    ComPtr<ID3D11InputLayout> layout;ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;
    UINT vertexClasses=0,pixelClasses=0;context->IAGetInputLayout(&layout);
    context->VSGetShader(&vertex,nullptr,&vertexClasses);context->PSGetShader(&pixel,nullptr,&pixelClasses);
    if(layout||vertexClasses||pixelClasses||vertex.Get()!=receipt.retainedVertex.Get()||pixel.Get()!=receipt.retainedPixel.Get())
        throw Error("Actual native sprite batch retirement differs from its completed input cleanup");
}

NativeBindingResetReceipt NativeBackend::resetEngineBindings(const std::shared_ptr<RenderTarget>& defaultColor,
                                        const std::shared_ptr<DepthTarget>& defaultDepth,bool resetViewportAndScissor) {
    validateSubmissionContext(); // Owner, device availability and immediate context.
    if(bindingResetSerial==UINT64_MAX)throw Error("Native binding reset serial exhausted");
    if(!defaultColor || !defaultDepth)
        throw Error("Native binding reset requires backed color and depth targets");
    const auto format=colorFormat(defaultColor->format);
    validateTarget(defaultColor->texture.Get(),defaultColor->view.Get(),device.Get(),
                   defaultColor->pixelWidth(),defaultColor->pixelHeight(),format,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
    validateTarget(defaultDepth->texture.Get(),defaultDepth->view.Get(),device.Get(),
                   defaultDepth->storageWidth(),defaultDepth->storageHeight(),DXGI_FORMAT_R32G8X24_TYPELESS,
                   D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE);
    if(defaultColor->width!=defaultDepth->width || defaultColor->height!=defaultDepth->height ||
       defaultColor->pixelWidth()!=defaultDepth->storageWidth() || defaultColor->pixelHeight()!=defaultDepth->storageHeight())
        throw Error("Native binding reset attachment dimensions differ");
    D3D11_RENDER_TARGET_VIEW_DESC colorView{};defaultColor->view->GetDesc(&colorView);
    D3D11_DEPTH_STENCIL_VIEW_DESC depthView{};defaultDepth->view->GetDesc(&depthView);
    if(colorView.Format!=format || colorView.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D || colorView.Texture2D.MipSlice ||
       depthView.Format!=DXGI_FORMAT_D32_FLOAT_S8X24_UINT || depthView.ViewDimension!=D3D11_DSV_DIMENSION_TEXTURE2D ||
       depthView.Texture2D.MipSlice || depthView.Flags)
        throw Error("Native binding reset target view exceeds the owned attachment profile");

    const ShaderReads before(context.Get());
    for(size_t stage=0;stage<6;++stage) for(size_t slot=0;slot<before.stages[stage].values.size();++slot) {
        if(stage==0 && slot<8) continue; // These exact engine texture slots are cleared.
        auto* view=before.stages[stage].values[slot];
        if(!view) continue;
        ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
        if(resource.Get()==defaultColor->texture.Get() || resource.Get()==defaultDepth->texture.Get())
            throw Error("Native binding reset would unbind a preserved shader resource");
    }
    References<ID3D11RenderTargetView,8> oldColors;
    ComPtr<ID3D11DepthStencilView> oldDepth;
    context->OMGetRenderTargets(8,oldColors.values.data(),&oldDepth);
    for(size_t slot=4;slot<oldColors.values.size();++slot) if(oldColors.values[slot])
        throw Error("Native binding reset does not own render-target slots above three");
    ComPtr<ID3D11UnorderedAccessView> uavZero;
    context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,1,&uavZero);
    if(uavZero) throw Error("Native binding reset would unbind a preserved output UAV");

    // Every predictable rejection precedes the first setter. D3D11 retains
    // submitted GPU uses after unbinding; neither Flush nor a guest refcount
    // decrement is an equivalent lifetime operation. This does not wait for GPU
    // completion or publish any guest cache/state. See original 823EFDA0 contract.
    flushIm2D();invalidateScreenReplacement();
    std::array<ID3D11ShaderResourceView*,8> noTextures{};
    std::array<ID3D11Buffer*,4> noVertices{};
    std::array<UINT,4> zero{};
    context->PSSetShaderResources(0,8,noTextures.data());
    context->IASetVertexBuffers(0,4,noVertices.data(),zero.data(),zero.data());
    context->IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);
    context->PSSetShader(nullptr,nullptr,0);
    context->VSSetShader(nullptr,nullptr,0);
    context->IASetInputLayout(nullptr);
    ID3D11RenderTargetView* color=defaultColor->view.Get();
    // NumRTVs=1 also nulls slots 1..7 (4..7 were checked empty). KEEP preserves
    // unrelated OM UAVs, including their hidden counters. Slot zero was checked.
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargetsandunorderedaccessviews
    context->OMSetRenderTargetsAndUnorderedAccessViews(1,&color,defaultDepth->view.Get(),
                                                     0,D3D11_KEEP_UNORDERED_ACCESS_VIEWS,nullptr,nullptr);
    if(resetViewportAndScissor) {
        // Original color-zero rebind8243D198 copies scissor82069FBC and
        // viewport82069FA4;8243CE80 clips the latter to target dimensions.
        // 8243C430 applies the viewport as effective clipping (and intersects
        // the requested rectangle when scissor is enabled). Both give full size.
        const auto viewport=renderViewport(defaultColor,{0,0,float(defaultColor->width),float(defaultColor->height),0,1});
        const auto scissor=renderScissor(defaultColor,{0,0,LONG(defaultColor->width),LONG(defaultColor->height)});
        context->RSSetViewports(1,&viewport);context->RSSetScissorRects(1,&scissor);
    }

    // These setters return no HRESULT. Verify real context state, not a mirror.
    const ShaderReads after(context.Get());
    bool matched=true;
    for(size_t stage=0;stage<6;++stage) for(size_t slot=0;slot<before.stages[stage].values.size();++slot)
        matched=matched && after.stages[stage].values[slot]==
            ((stage==0 && slot<8)?nullptr:before.stages[stage].values[slot]);
    References<ID3D11Buffer,4> vertices;
    std::array<UINT,4> strides{},offsets{};
    context->IAGetVertexBuffers(0,4,vertices.values.data(),strides.data(),offsets.data());
    for(size_t slot=0;slot<4;++slot)
        matched=matched && !vertices.values[slot] && !strides[slot] && !offsets[slot];
    ComPtr<ID3D11Buffer> index;DXGI_FORMAT indexFormat{};UINT indexOffset{};
    context->IAGetIndexBuffer(&index,&indexFormat,&indexOffset);
    ComPtr<ID3D11PixelShader> ps;context->PSGetShader(&ps,nullptr,nullptr);
    ComPtr<ID3D11VertexShader> vs;context->VSGetShader(&vs,nullptr,nullptr);
    ComPtr<ID3D11InputLayout> layout;context->IAGetInputLayout(&layout);
    matched=matched && !index && indexFormat==DXGI_FORMAT_UNKNOWN && !indexOffset && !ps && !vs && !layout;
    References<ID3D11RenderTargetView,8> colors;
    ComPtr<ID3D11DepthStencilView> depth;context->OMGetRenderTargets(8,colors.values.data(),&depth);
    matched=matched && colors.values[0]==defaultColor->view.Get() && depth.Get()==defaultDepth->view.Get();
    for(size_t slot=1;slot<colors.values.size();++slot) matched=matched && !colors.values[slot];
    if(resetViewportAndScissor) {
        const auto vp=viewport();D3D11_RECT rect{};UINT count=1;context->RSGetScissorRects(&count,&rect);
        matched=matched&&vp&&vp->TopLeftX==0&&vp->TopLeftY==0&&vp->Width==float(defaultColor->width)&&
            vp->Height==float(defaultColor->height)&&vp->MinDepth==0&&vp->MaxDepth==1&&
            count==1&&rect.left==0&&rect.top==0&&rect.right==LONG(defaultColor->pixelWidth())&&rect.bottom==LONG(defaultColor->pixelHeight());
    }
    requireOwner();
    if(!matched) throw Error("D3D11 did not retain the narrow engine binding reset");
    // Rasterizer/blend/depth state, samplers, all constant buffers, other shaders,
    // topology and VB slots >=4 survive. Viewport/scissor change only on request.
    // A device-loss/postcondition failure is explicit; GPU work is not rolled back.
    NativeBindingResetReceipt receipt;receipt.owner=this;receipt.device=device;
    receipt.reset=completedBindingReset=++bindingResetSerial;
    receipt.epoch=screenShaderEpoch;requireBindingReset(receipt);return receipt;
}
void NativeBackend::requireBindingReset(const NativeBindingResetReceipt& receipt) const {
    requireOwner();
    if(receipt.owner!=this||receipt.device.Get()!=device.Get()||!receipt.reset||receipt.reset!=completedBindingReset||
       receipt.reset!=bindingResetSerial||receipt.epoch!=screenShaderEpoch)
        throw Error("Native binding reset receipt is stale or foreign");
    if(pendingIm2D||pendingIm2DBuffer)
        throw Error("Native binding reset receipt has unfinished Im2D work");
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;UINT vertexClasses=0,pixelClasses=0;
    context->VSGetShader(&vertex,nullptr,&vertexClasses);context->PSGetShader(&pixel,nullptr,&pixelClasses);
    if(vertex||pixel||vertexClasses||pixelClasses)
        throw Error("Actual native shaders differ from the completed binding reset");
}
void NativeBackend::retireBindingReset(const NativeBindingResetReceipt& receipt) {
    requireBindingReset(receipt);invalidateScreenReplacement();
}
}
