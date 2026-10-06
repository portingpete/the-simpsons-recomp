#include "native_backend.h"
#include "material_resources.h"
#include "runtime/stall_profiler.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "VSFlat.h"
#include "VSImmediate.h"
#include "PSImmediate.h"
#include "PSImmediateDual.h"
#include "VSImmediateProjected.h"
#include "PSImmediateProjected.h"
#include "PSImmediateProjectedDual.h"
#include "VSRadial.h"
#include "PSRadial.h"
#include "PSRadialQuery.h"
#include "VSTextured.h"
#include "PSFlat.h"
#include "PSTextured.h"
#include "PSOriginalFlat.h"
#include "PSOriginalTextured.h"
#include "VSFourTap.h"
#include "PSFourTap.h"
#include "PSMovie.h"
#include "VSEdge.h"
#include "PSEdge.h"
#include "VSAA.h"
#include "PSAA.h"
#include "VSEdgeAA.h"
#include "PSEdgeAA.h"
#include "VSShadowDepth.h"
#include "PSShadowDepthAlpha.h"
#include "VSZPrepass.h"
#include "VSMono.h"
#include "PSMono.h"
#include "VSRigid.h"
#include "PSRigid.h"
#include "VSRigidTextured.h"
#include "VSVfxRigid.h"
#include "PSVfxRigid.h"
#include "PSRigidTextured.h"
#include "VSRigidDualTextured.h"
#include "PSRigidDualTextured.h"
#include "VSRigidDualTexturedUV.h"
#include "PSRigidDualTexturedUV.h"
#include "VSRigidDualTexturedUVAlpha.h"
#include "PSRigidDualTexturedUVAlpha.h"
#include "VSRigidUV.h"
#include "PSRigidUV.h"
#include "VSRigidUVAlpha.h"
#include "PSRigidUVAlpha.h"
#include "VSFlipbook.h"
#include "PSFlipbook.h"
#include "VSFlipbookAlpha.h"
#include "PSFlipbookAlpha.h"
#include "VSRigidGloss.h"
#include "PSRigidGloss.h"
#include "VSRigidMultitone.h"
#include "PSRigidMultitone.h"
#include "VSRigidNormalmap.h"
#include "PSRigidNormalmap.h"
#include "VSRigidFamilyAlpha.h"
#include "PSRigidGlossAlpha.h"
#include "PSRigidMultitoneAlpha.h"
#include "PSRigidNormalmapAlpha.h"
#include "VSSkinGloss.h"
#include "PSSkinGloss.h"
#include "VSSkinGlossAlpha.h"
#include "PSSkinGlossAlpha.h"
#include "VSSkinFlipbook.h"
#include "PSSkinFlipbook.h"
#include "VSSkinFlipbookAlpha.h"
#include "PSSkinFlipbookAlpha.h"
#include "VSSkinDualUV.h"
#include "PSSkinDualUV.h"
#include "VSSkinDualUVAlpha.h"
#include "PSSkinDualUVAlpha.h"
#include "VSSkin.h"
#include "PSSkin.h"
#include "PSSkinAlpha.h"
#include "VSSkinDual.h"
#include "PSSkinDual.h"
#include "VSSkinDualAlpha.h"
#include "PSSkinDualAlpha.h"
#include "VSSkinTextured.h"
#include "VSSkinTexturedAlpha.h"
#include "PSSkinTextured.h"
#include "PSSkinTexturedAlpha.h"
#include "VSSky.h"
#include "PSSky.h"
#include "VSSkyOpaque.h"
#include "PSSkyOpaque.h"
#include "VSChocolate.h"
#include "PSChocolate.h"
#include "VSChocolateOpaque.h"
#include "PSChocolateOpaque.h"
#include "VSProjtex.h"
#include "PSProjtex.h"
#include "VSProjtexAlpha.h"
#include "PSProjtexAlpha.h"
#include "VSRigidAlpha.h"
#include "PSRigidAlpha.h"
#include "VS168F8.h"
#include "PS168F8.h"
#include "VSCoronaQuery.h"
#include "PSCoronaQuery.h"
#include "PSCoronaQueryPacked.h"
#include "PSCoronaSprite.h"
#include "PSCoronaSpritePacked.h"

namespace Simpsons::Graphics {
namespace {
void check(HRESULT result,const char* operation) {
    if(FAILED(result)) {
        char message[160];snprintf(message,sizeof(message),"Native screen %s failed: 0x%08lX",operation,ULONG(result));
        throw Error(message);
    }
}
void sameDevice(ID3D11Resource* resource,ID3D11Device* device) {
    if(!resource) throw Error("Missing native screen resource");
    ComPtr<ID3D11Device> source;resource->GetDevice(&source);
    if(source.Get()!=device) throw Error("Native screen resource belongs to another graphics device");
}
void finiteColor(const std::array<float,4>& color) {
    for(float value:color) if(!std::isfinite(value)) throw Error("Nonfinite native screen color is unverified");
}
bool sameTextureDescriptor(const D3D11_TEXTURE2D_DESC& a,const D3D11_TEXTURE2D_DESC& b) {
    return a.Width==b.Width && a.Height==b.Height && a.MipLevels==b.MipLevels && a.ArraySize==b.ArraySize &&
        a.Format==b.Format && a.SampleDesc.Count==b.SampleDesc.Count && a.SampleDesc.Quality==b.SampleDesc.Quality &&
        a.Usage==b.Usage && a.BindFlags==b.BindFlags && a.CPUAccessFlags==b.CPUAccessFlags && a.MiscFlags==b.MiscFlags;
}
struct Constants {std::array<float,4> color;float alphaReference;uint32_t alphaTest;uint32_t blendSelector;float padding{};};
static_assert(sizeof(Constants)==32 && sizeof(ScreenVertex)==16);
}
struct ScreenPipeline {
    ComPtr<ID3D11VertexShader> flatVS,texturedVS;
    ComPtr<ID3D11PixelShader> flatPS,texturedPS;
    ComPtr<ID3D11PixelShader> originalFlatPS,originalTexturedPS;
    ComPtr<ID3D11VertexShader> coronaVS;
    ComPtr<ID3D11PixelShader> coronaPS,coronaSpritePS,coronaSpritePackedPS;
    ComPtr<ID3D11InputLayout> coronaLayout;
    ComPtr<ID3D11InputLayout> flatLayout,texturedLayout;
    ComPtr<ID3D11Buffer> vertices,constants,postConstants;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11RasterizerState> rasterizer;
    ScreenPipeline(ID3D11Device* device) {
        check(device->CreateVertexShader(kVSFlat,sizeof(kVSFlat),nullptr,&flatVS),"flat vertex shader creation");
        check(device->CreateVertexShader(kVSTextured,sizeof(kVSTextured),nullptr,&texturedVS),"textured vertex shader creation");
        check(device->CreatePixelShader(kPSFlat,sizeof(kPSFlat),nullptr,&flatPS),"flat pixel shader creation");
        check(device->CreatePixelShader(kPSTextured,sizeof(kPSTextured),nullptr,&texturedPS),"textured pixel shader creation");
        check(device->CreatePixelShader(kPSOriginalFlat,sizeof(kPSOriginalFlat),nullptr,&originalFlatPS),"original flat blend shader creation");
        check(device->CreatePixelShader(kPSOriginalTextured,sizeof(kPSOriginalTextured),nullptr,&originalTexturedPS),"original textured blend shader creation");
        check(device->CreateVertexShader(kVSCoronaQuery,sizeof(kVSCoronaQuery),nullptr,&coronaVS),"corona vertex shader");
        check(device->CreatePixelShader(kPSCoronaQueryPacked,sizeof(kPSCoronaQueryPacked),nullptr,&coronaPS),"corona query shader");
        check(device->CreatePixelShader(kPSCoronaSprite,sizeof(kPSCoronaSprite),nullptr,&coronaSpritePS),"corona sprite shader");
        check(device->CreatePixelShader(kPSCoronaSpritePacked,sizeof(kPSCoronaSpritePacked),nullptr,&coronaSpritePackedPS),"corona sprite blend shader");
        const D3D11_INPUT_ELEMENT_DESC coronaElements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32G32B32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0}};
        check(device->CreateInputLayout(coronaElements,3,kVSCoronaQuery,sizeof(kVSCoronaQuery),&coronaLayout),"corona input layout");
        D3D11_INPUT_ELEMENT_DESC elements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}};
        check(device->CreateInputLayout(elements,1,kVSFlat,sizeof(kVSFlat),&flatLayout),"flat input layout creation");
        check(device->CreateInputLayout(elements,2,kVSTextured,sizeof(kVSTextured),&texturedLayout),"textured input layout creation");
        D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof(ScreenVertex)*4;
        buffer.Usage=D3D11_USAGE_DYNAMIC;buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        buffer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        check(device->CreateBuffer(&buffer,nullptr,&vertices),"vertex snapshot allocation");
        buffer.ByteWidth=sizeof(Constants);buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&buffer,nullptr,&constants),"constant snapshot allocation");
        check(device->CreateBuffer(&buffer,nullptr,&postConstants),"post-state constant snapshot allocation");
        D3D11_DEPTH_STENCIL_DESC depthDesc{};
        depthDesc.DepthEnable=FALSE;depthDesc.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
        depthDesc.DepthFunc=D3D11_COMPARISON_ALWAYS;depthDesc.StencilEnable=FALSE;
        depthDesc.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};
        depthDesc.BackFace=depthDesc.FrontFace;
        check(device->CreateDepthStencilState(&depthDesc,&depth),"depth/stencil state creation");
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        check(device->CreateRasterizerState(&raster,&rasterizer),"rasterizer state creation");
    }
};
namespace {
class NativeShaderArtifact final : public CompiledMaterial {
public:
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    // Device proof, the same contract as the recording payload proofs: one GetDevice check of
    // the exact installed shader objects; these strong references pin those objects, so while
    // the same objects are installed a later validation compares identities instead of
    // repeating the AddRef/Release round trip per draw. Any replacement forces the full check.
    mutable ComPtr<ID3D11VertexShader> provenVertex;
    mutable ComPtr<ID3D11PixelShader> provenPixel;
    mutable ComPtr<ID3D11Device> provenDevice;
    explicit NativeShaderArtifact(const MaterialIdentity& original):CompiledMaterial(original.originalAddress,original.stage) {}
};
// True iff every installed stage object of the artifact belongs to `device`.
bool artifactOnDevice(const NativeShaderArtifact& artifact,ID3D11Device* device) {
    if(device && artifact.provenDevice.Get()==device && artifact.provenVertex.Get()==artifact.vertex.Get() &&
       artifact.provenPixel.Get()==artifact.pixel.Get())
        return true;
    if(artifact.vertex) {ComPtr<ID3D11Device> owner;artifact.vertex->GetDevice(&owner);if(owner.Get()!=device)return false;}
    if(artifact.pixel) {ComPtr<ID3D11Device> owner;artifact.pixel->GetDevice(&owner);if(owner.Get()!=device)return false;}
    // Publish the proof only after the full checks succeeded.
    artifact.provenVertex=artifact.vertex;artifact.provenPixel=artifact.pixel;artifact.provenDevice=device;
    return true;
}
}
NativeScreenInputReceipt NativeBackend::bindDirectSpriteInputs(const std::shared_ptr<Texture>& texture,const D3D11_SAMPLER_DESC& sampler) {
    validateSubmissionContext();validateTexture(texture);
    if(!screenPipeline)screenPipeline=std::make_shared<ScreenPipeline>(device.Get());
    ComPtr<ID3D11SamplerState> nativeSampler;
    check(device->CreateSamplerState(&sampler,&nativeSampler),"direct sprite sampler creation");
    if(screenInputBinds==UINT64_MAX)throw Error("Native screen input binding serial exhausted");
    NativeScreenInputReceipt receipt;UINT classes=0;context->PSGetShader(&receipt.retainedPixel,nullptr,&classes);
    if(classes)throw Error("Original sprite inputs cannot retain dynamic pixel shader classes");
    invalidateScreenReplacement();
    // Original8276AFCC..B05C precedes the query-ready early-out. The pixel
    // shader is deliberately retained until the original conditional selects it.
    bindEngineTexture(0,texture);
    context->VSSetShader(screenPipeline->texturedVS.Get(),nullptr,0);
    context->IASetInputLayout(screenPipeline->texturedLayout.Get());
    auto* bound=nativeSampler.Get();context->PSSetSamplers(0,1,&bound);
    completedScreenInputBind=++screenInputBinds;
    receipt.owner=this;receipt.bind=completedScreenInputBind;receipt.epoch=screenShaderEpoch;receipt.draw=screenDraws;
    requireScreenInputReplacement(receipt);return receipt;
}
void NativeBackend::bindEngineVertexStorage(const std::shared_ptr<Buffer>& buffer,uint32_t offset,uint32_t stride){
    validateSubmissionContext();
    if(!buffer||buffer->kind!=BufferKind::Vertex||!stride||offset>buffer->size)throw Error("Invalid native engine vertex storage binding");
    sameDevice(buffer->buffer.Get(),device.Get());auto* resource=buffer->buffer.Get();
    context->IASetVertexBuffers(0,1,&resource,&stride,&offset);
}
std::unique_ptr<CompiledMaterial> NativeBackend::createMaterialArtifact(const MaterialRecord& original) {
    requireOwner();
    const auto& identity=original.identity();
    auto result=std::make_unique<NativeShaderArtifact>(identity);
    switch(identity.originalAddress) {
    case 0x821538E8:check(device->CreateVertexShader(kVSRadial,sizeof(kVSRadial),nullptr,&result->vertex),"original radial vertex artifact");break;
    case 0x82153B88:check(device->CreatePixelShader(kPSRadial,sizeof(kPSRadial),nullptr,&result->pixel),"original radial pixel artifact");break;
    case 0x82153C80:check(device->CreatePixelShader(kPSRadialQuery,sizeof(kPSRadialQuery),nullptr,&result->pixel),"original radial query artifact");break;
    case 0x821511D8:check(device->CreateVertexShader(kVSImmediate,sizeof(kVSImmediate),nullptr,&result->vertex),"original immediate vertex artifact");break;
    case 0x821509D8:check(device->CreatePixelShader(kPSImmediate,sizeof(kPSImmediate),nullptr,&result->pixel),"original immediate pixel artifact");break;
    case 0x82150B18:check(device->CreatePixelShader(kPSImmediateDual,sizeof(kPSImmediateDual),nullptr,&result->pixel),"original dual immediate pixel artifact");break;
    case 0x821513B8:check(device->CreateVertexShader(kVSImmediateProjected,sizeof(kVSImmediateProjected),nullptr,&result->vertex),"original projected immediate vertex artifact");break;
    case 0x82150C98:check(device->CreatePixelShader(kPSImmediateProjected,sizeof(kPSImmediateProjected),nullptr,&result->pixel),"original projected immediate pixel artifact");break;
    case 0x82150F18:check(device->CreatePixelShader(kPSImmediateProjectedDual,sizeof(kPSImmediateProjectedDual),nullptr,&result->pixel),"original projected dual immediate pixel artifact");break;
    case 0x821524C8:check(device->CreatePixelShader(kPSFlat,sizeof(kPSFlat),nullptr,&result->pixel),"original flat pixel artifact creation");break;
    case 0x821525E8:check(device->CreateVertexShader(kVSFlat,sizeof(kVSFlat),nullptr,&result->vertex),"original flat vertex artifact creation");break;
    case 0x82152708:check(device->CreatePixelShader(kPSTextured,sizeof(kPSTextured),nullptr,&result->pixel),"original textured pixel artifact creation");break;
    case 0x82152880:check(device->CreateVertexShader(kVSTextured,sizeof(kVSTextured),nullptr,&result->vertex),"original textured vertex artifact creation");break;
    case 0x82153278:check(device->CreateVertexShader(kVSCoronaQuery,sizeof(kVSCoronaQuery),nullptr,&result->vertex),"original corona vertex artifact");break;
    case 0x82153460:check(device->CreatePixelShader(kPSCoronaQuery,sizeof(kPSCoronaQuery),nullptr,&result->pixel),"original corona query artifact");break;
    case 0x821536F0:check(device->CreatePixelShader(kPSCoronaSprite,sizeof(kPSCoronaSprite),nullptr,&result->pixel),"original corona sprite artifact");break;
    case 0x820B8F08:check(device->CreateVertexShader(kVSFourTap,sizeof(kVSFourTap),nullptr,&result->vertex),"original four-tap vertex artifact creation");break;
    case 0x820B90D4:check(device->CreatePixelShader(kPSFourTap,sizeof(kPSFourTap),nullptr,&result->pixel),"original four-tap pixel artifact creation");break;
    case 0x82152B68:check(device->CreatePixelShader(kPSMovie,sizeof(kPSMovie),nullptr,&result->pixel),"original movie pixel artifact creation");break;
    case 0x8202E6F0:check(device->CreateVertexShader(kVSEdge,sizeof(kVSEdge),nullptr,&result->vertex),"original edge vertex artifact creation");break;
    case 0x8202E840:check(device->CreatePixelShader(kPSEdge,sizeof(kPSEdge),nullptr,&result->pixel),"original edge pixel artifact creation");break;
    case 0x820301A0:check(device->CreateVertexShader(kVSAA,sizeof(kVSAA),nullptr,&result->vertex),"original AA vertex artifact creation");break;
    case 0x820302EC:check(device->CreatePixelShader(kPSAA,sizeof(kPSAA),nullptr,&result->pixel),"original AA pixel artifact creation");break;
    case 0x820347B0:check(device->CreateVertexShader(kVSEdgeAA,sizeof(kVSEdgeAA),nullptr,&result->vertex),"original edgeAA vertex artifact creation");break;
    case 0x82034900:check(device->CreatePixelShader(kPSEdgeAA,sizeof(kPSEdgeAA),nullptr,&result->pixel),"original edgeAA pixel artifact creation");break;
    case 0x820C1E6C: // Identical executable and literal bytes, independently pinned.
    case 0x820C2FA0:check(device->CreateVertexShader(kVSShadowDepth,sizeof(kVSShadowDepth),nullptr,&result->vertex),"original shadow depth vertex artifact creation");break;
    case 0x820CA530:check(device->CreatePixelShader(kPSShadowDepthAlpha,sizeof(kPSShadowDepthAlpha),nullptr,&result->pixel),"original shadow alpha pixel artifact creation");break;
    case 0x82120C04:
    case 0x82121BE8:check(device->CreateVertexShader(kVSMono,sizeof(kVSMono),nullptr,&result->vertex),"original mono vertex artifact");break;
    case 0x82122BD4:
    case 0x82122D38:check(device->CreatePixelShader(kPSMono,sizeof(kPSMono),nullptr,&result->pixel),"original mono pixel artifact");break;
    case 0x8214A8A4:
    case 0x8214B9B8:check(device->CreateVertexShader(kVSZPrepass,sizeof(kVSZPrepass),nullptr,&result->vertex),"original Z-prepass vertex artifact creation");break;
    case 0x8200D3AC:check(device->CreateVertexShader(kVSRigid,sizeof(kVSRigid),nullptr,&result->vertex),"original rigid vertex artifact creation");break;
    case 0x8200D9E8:check(device->CreatePixelShader(kPSRigid,sizeof(kPSRigid),nullptr,&result->pixel),"original rigid pixel artifact creation");break;
    case 0x8201700C:check(device->CreateVertexShader(kVSRigidTextured,sizeof(kVSRigidTextured),nullptr,&result->vertex),"original textured rigid vertex artifact creation");break;
    case 0x82017658:check(device->CreatePixelShader(kPSRigidTextured,sizeof(kPSRigidTextured),nullptr,&result->pixel),"original textured rigid pixel artifact creation");break;
    case 0x8202B4CC:check(device->CreateVertexShader(kVSRigidDualTextured,sizeof(kVSRigidDualTextured),nullptr,&result->vertex),"original dualtextured rigid vertex artifact creation");break;
    case 0x8202BB44:check(device->CreatePixelShader(kPSRigidDualTextured,sizeof(kPSRigidDualTextured),nullptr,&result->pixel),"original dualtextured rigid pixel artifact creation");break;
    case 0x82046D9C:check(device->CreateVertexShader(kVSRigidDualTexturedUV,sizeof(kVSRigidDualTexturedUV),nullptr,&result->vertex),"original dualtextured UV vertex artifact");break;
    case 0x820477A8:check(device->CreatePixelShader(kPSRigidDualTexturedUV,sizeof(kPSRigidDualTexturedUV),nullptr,&result->pixel),"original dualtextured UV pixel artifact");break;
    case 0x82047318:check(device->CreateVertexShader(kVSRigidDualTexturedUVAlpha,sizeof(kVSRigidDualTexturedUVAlpha),nullptr,&result->vertex),"original dualtextured UV alpha vertex artifact");break;
    case 0x82047F44:check(device->CreatePixelShader(kPSRigidDualTexturedUVAlpha,sizeof(kPSRigidDualTexturedUVAlpha),nullptr,&result->pixel),"original dualtextured UV alpha pixel artifact");break;
    case 0x8204364C:check(device->CreateVertexShader(kVSRigidUV,sizeof(kVSRigidUV),nullptr,&result->vertex),"original single UV vertex artifact");break;
    case 0x82044068:check(device->CreatePixelShader(kPSRigidUV,sizeof(kPSRigidUV),nullptr,&result->pixel),"original single UV pixel artifact");break;
    case 0x82043BC0:check(device->CreateVertexShader(kVSRigidUVAlpha,sizeof(kVSRigidUVAlpha),nullptr,&result->vertex),"original single UV alpha vertex artifact");break;
    case 0x82044850:check(device->CreatePixelShader(kPSRigidUVAlpha,sizeof(kPSRigidUVAlpha),nullptr,&result->pixel),"original single UV alpha pixel artifact");break;
    case 0x820398BC:check(device->CreateVertexShader(kVSFlipbook,sizeof(kVSFlipbook),nullptr,&result->vertex),"original flipbook vertex artifact");break;
    case 0x8203A24C:check(device->CreatePixelShader(kPSFlipbook,sizeof(kPSFlipbook),nullptr,&result->pixel),"original flipbook pixel artifact");break;
    case 0x82039D70:check(device->CreateVertexShader(kVSFlipbookAlpha,sizeof(kVSFlipbookAlpha),nullptr,&result->vertex),"original flipbook alpha vertex artifact");break;
    case 0x8203A644:check(device->CreatePixelShader(kPSFlipbookAlpha,sizeof(kPSFlipbookAlpha),nullptr,&result->pixel),"original flipbook alpha pixel artifact");break;
    // The scheduled executable slots of the dual-textured alpha shaders are
    // byte-for-byte identical to the proven 168F8 alpha pair. Keep their
    // original addresses distinct while reusing the verified HLSL artifacts.
    case 0x8202B884:check(device->CreateVertexShader(kVS168F8,sizeof(kVS168F8),nullptr,&result->vertex),"original dualtextured alpha vertex artifact creation");break;
    case 0x8202C3BC:check(device->CreatePixelShader(kPS168F8,sizeof(kPS168F8),nullptr,&result->pixel),"original dualtextured alpha pixel artifact creation");break;
    case 0x8201A07C:check(device->CreateVertexShader(kVSRigidGloss,sizeof(kVSRigidGloss),nullptr,&result->vertex),"original gloss rigid vertex artifact creation");break;
    case 0x8201A6EC:check(device->CreatePixelShader(kPSRigidGloss,sizeof(kPSRigidGloss),nullptr,&result->pixel),"original gloss rigid pixel artifact creation");break;
    case 0x82054F2C:check(device->CreateVertexShader(kVSRigidMultitone,sizeof(kVSRigidMultitone),nullptr,&result->vertex),"original multitone rigid vertex artifact creation");break;
    case 0x82055710:check(device->CreatePixelShader(kPSRigidMultitone,sizeof(kPSRigidMultitone),nullptr,&result->pixel),"original multitone rigid pixel artifact creation");break;
    case 0x8205855C:check(device->CreateVertexShader(kVSRigidNormalmap,sizeof(kVSRigidNormalmap),nullptr,&result->vertex),"original normalmap rigid vertex artifact creation");break;
    case 0x82058CBC:check(device->CreatePixelShader(kPSRigidNormalmap,sizeof(kPSRigidNormalmap),nullptr,&result->pixel),"original normalmap rigid pixel artifact creation");break;
    // Gloss alpha's entire executable and fetch linkage match VS168F8.
    case 0x8201A430:check(device->CreateVertexShader(kVS168F8,sizeof(kVS168F8),nullptr,&result->vertex),"original gloss alpha vertex artifact");break;
    case 0x82055440:
    case 0x820589EC:check(device->CreateVertexShader(kVSRigidFamilyAlpha,sizeof(kVSRigidFamilyAlpha),nullptr,&result->vertex),"original family alpha vertex artifact");break;
    case 0x8201B0BC:check(device->CreatePixelShader(kPSRigidGlossAlpha,sizeof(kPSRigidGlossAlpha),nullptr,&result->pixel),"original gloss alpha pixel artifact");break;
    case 0x820560B8:check(device->CreatePixelShader(kPSRigidMultitoneAlpha,sizeof(kPSRigidMultitoneAlpha),nullptr,&result->pixel),"original multitone alpha pixel artifact");break;
    case 0x82059880:check(device->CreatePixelShader(kPSRigidNormalmapAlpha,sizeof(kPSRigidNormalmapAlpha),nullptr,&result->pixel),"original normalmap alpha pixel artifact");break;
    case 0x82007C1C:
    case 0x82008E20:check(device->CreateVertexShader(kVSSkin,sizeof(kVSSkin),nullptr,&result->vertex),"original skin vertex artifact creation");break;
    case 0x8200A02C:check(device->CreatePixelShader(kPSSkin,sizeof(kPSSkin),nullptr,&result->pixel),"original skin pixel artifact creation");break;
    case 0x8200A4A4:check(device->CreatePixelShader(kPSSkinAlpha,sizeof(kPSSkinAlpha),nullptr,&result->pixel),"original skin alpha pixel artifact creation");break;
    case 0x8201E6DC:check(device->CreateVertexShader(kVSSkinDual,sizeof(kVSSkinDual),nullptr,&result->vertex),"original dual skin vertex artifact creation");break;
    case 0x82020B9C:check(device->CreatePixelShader(kPSSkinDual,sizeof(kPSSkinDual),nullptr,&result->pixel),"original dual skin pixel artifact creation");break;
    case 0x8201F984:check(device->CreateVertexShader(kVSSkinDualAlpha,sizeof(kVSSkinDualAlpha),nullptr,&result->vertex),"original dual skin alpha vertex artifact creation");break;
    case 0x82021344:check(device->CreatePixelShader(kPSSkinDualAlpha,sizeof(kPSSkinDualAlpha),nullptr,&result->pixel),"original dual skin alpha pixel artifact creation");break;
    case 0x8202579C:check(device->CreateVertexShader(kVSSkinGloss,sizeof(kVSSkinGloss),nullptr,&result->vertex),"original VSSkinGloss artifact creation");break;
    case 0x82027C18:check(device->CreatePixelShader(kPSSkinGloss,sizeof(kPSSkinGloss),nullptr,&result->pixel),"original PSSkinGloss artifact creation");break;
    case 0x820269E8:check(device->CreateVertexShader(kVSSkinGlossAlpha,sizeof(kVSSkinGlossAlpha),nullptr,&result->vertex),"original VSSkinGlossAlpha artifact creation");break;
    case 0x82028258:check(device->CreatePixelShader(kPSSkinGlossAlpha,sizeof(kPSSkinGlossAlpha),nullptr,&result->pixel),"original PSSkinGlossAlpha artifact creation");break;
    case 0x8203D93C:check(device->CreateVertexShader(kVSSkinFlipbook,sizeof(kVSSkinFlipbook),nullptr,&result->vertex),"original VSSkinFlipbook artifact creation");break;
    case 0x82040118:check(device->CreatePixelShader(kPSSkinFlipbook,sizeof(kPSSkinFlipbook),nullptr,&result->pixel),"original PSSkinFlipbook artifact creation");break;
    case 0x8203ED38:check(device->CreateVertexShader(kVSSkinFlipbookAlpha,sizeof(kVSSkinFlipbookAlpha),nullptr,&result->vertex),"original VSSkinFlipbookAlpha artifact creation");break;
    case 0x82040510:check(device->CreatePixelShader(kPSSkinFlipbookAlpha,sizeof(kPSSkinFlipbookAlpha),nullptr,&result->pixel),"original PSSkinFlipbookAlpha artifact creation");break;
    case 0x8204BA4C:check(device->CreateVertexShader(kVSSkinDualUV,sizeof(kVSSkinDualUV),nullptr,&result->vertex),"original VSSkinDualUV artifact creation");break;
    case 0x8204E2DC:check(device->CreatePixelShader(kPSSkinDualUV,sizeof(kPSSkinDualUV),nullptr,&result->pixel),"original PSSkinDualUV artifact creation");break;
    case 0x8204CE90:check(device->CreateVertexShader(kVSSkinDualUVAlpha,sizeof(kVSSkinDualUVAlpha),nullptr,&result->vertex),"original VSSkinDualUVAlpha artifact creation");break;
    case 0x8204E75C:check(device->CreatePixelShader(kPSSkinDualUVAlpha,sizeof(kPSSkinDualUVAlpha),nullptr,&result->pixel),"original PSSkinDualUVAlpha artifact creation");break;
    case 0x8201146C:check(device->CreateVertexShader(kVSSkinTextured,sizeof(kVSSkinTextured),nullptr,&result->vertex),"original textured skin vertex artifact creation");break;
    case 0x820126EC:check(device->CreateVertexShader(kVSSkinTexturedAlpha,sizeof(kVSSkinTexturedAlpha),nullptr,&result->vertex),"original textured skin alpha vertex artifact creation");break;
    case 0x82013900:check(device->CreatePixelShader(kPSSkinTextured,sizeof(kPSSkinTextured),nullptr,&result->pixel),"original textured skin pixel artifact creation");break;
    case 0x82013F8C:check(device->CreatePixelShader(kPSSkinTexturedAlpha,sizeof(kPSSkinTexturedAlpha),nullptr,&result->pixel),"original textured skin alpha pixel artifact creation");break;
    case 0x82036F08:check(device->CreateVertexShader(kVSSky,sizeof(kVSSky),nullptr,&result->vertex),"original sky vertex artifact creation");break;
    case 0x820374E8:check(device->CreatePixelShader(kPSSky,sizeof(kPSSky),nullptr,&result->pixel),"original sky pixel artifact creation");break;
    case 0x82036C2C:check(device->CreateVertexShader(kVSSkyOpaque,sizeof(kVSSkyOpaque),nullptr,&result->vertex),"original opaque sky vertex artifact creation");break;
    case 0x820371EC:check(device->CreatePixelShader(kPSSkyOpaque,sizeof(kPSSkyOpaque),nullptr,&result->pixel),"original opaque sky pixel artifact creation");break;
    case 0x8205E0B8:check(device->CreateVertexShader(kVSChocolate,sizeof(kVSChocolate),nullptr,&result->vertex),"original chocolate vertex artifact creation");break;
    case 0x8205EED4:check(device->CreatePixelShader(kPSChocolate,sizeof(kPSChocolate),nullptr,&result->pixel),"original chocolate pixel artifact creation");break;
    case 0x8205DABC:check(device->CreateVertexShader(kVSChocolateOpaque,sizeof(kVSChocolateOpaque),nullptr,&result->vertex),"original chocolate opaque vertex artifact");break;
    case 0x8205E5E0:check(device->CreatePixelShader(kPSChocolateOpaque,sizeof(kPSChocolateOpaque),nullptr,&result->pixel),"original chocolate opaque pixel artifact");break;
    case 0x82051EEC:check(device->CreateVertexShader(kVSProjtex,sizeof(kVSProjtex),nullptr,&result->vertex),"original projtex vertex artifact");break;
    case 0x82052610:check(device->CreatePixelShader(kPSProjtex,sizeof(kPSProjtex),nullptr,&result->pixel),"original projtex pixel artifact");break;
    case 0x82052358:check(device->CreateVertexShader(kVSProjtexAlpha,sizeof(kVSProjtexAlpha),nullptr,&result->vertex),"original projtex alpha vertex artifact");break;
    case 0x82052DF4:check(device->CreatePixelShader(kPSProjtexAlpha,sizeof(kPSProjtexAlpha),nullptr,&result->pixel),"original projtex alpha pixel artifact");break;
    case 0x8200D734:check(device->CreateVertexShader(kVSRigidAlpha,sizeof(kVSRigidAlpha),nullptr,&result->vertex),"original rigid alpha vertex artifact");break;
    case 0x8200E1BC:check(device->CreatePixelShader(kPSRigidAlpha,sizeof(kPSRigidAlpha),nullptr,&result->pixel),"original rigid alpha pixel artifact");break;
    case 0x8205BE70:check(device->CreateVertexShader(kVSVfxRigid,sizeof(kVSVfxRigid),nullptr,&result->vertex),"original VFX rigid vertex artifact");break;
    case 0x8205C100:check(device->CreatePixelShader(kPSVfxRigid,sizeof(kPSVfxRigid),nullptr,&result->pixel),"original VFX rigid pixel artifact");break;
    case 0x8201739C:check(device->CreateVertexShader(kVS168F8,sizeof(kVS168F8),nullptr,&result->vertex),"original 168F8 vertex artifact creation");break;
    case 0x82017E4C:check(device->CreatePixelShader(kPS168F8,sizeof(kPS168F8),nullptr,&result->pixel),"original 168F8 pixel artifact creation");break;
    default: {
        char reason[140];snprintf(reason,sizeof(reason),"Native shader translation is not implemented for original material 0x%08X; bind rejected",identity.originalAddress);
        throw UnsupportedMaterial(reason);
    }
    }
    if((identity.stage==MaterialStage::Vertex)!=bool(result->vertex) || (identity.stage==MaterialStage::Pixel)!=bool(result->pixel))
        throw MaterialError("Original material stage disagrees with native shader artifact");
    return result;
}
void NativeBackend::validateEdgeShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) const {
    validateSubmissionContext();
    const auto* vs=dynamic_cast<const NativeShaderArtifact*>(&vertex);
    const auto* ps=dynamic_cast<const NativeShaderArtifact*>(&pixel);
    const bool pair=(vertex.originalAddress()==0x8202E6F0 && pixel.originalAddress()==0x8202E840) ||
                    (vertex.originalAddress()==0x820301A0 && pixel.originalAddress()==0x820302EC) ||
                    (vertex.originalAddress()==0x820347B0 && pixel.originalAddress()==0x82034900);
    if(!pair ||
       vertex.stage()!=MaterialStage::Vertex || pixel.stage()!=MaterialStage::Pixel ||
       !vs || !ps || !vs->vertex || !ps->pixel || vs->pixel || ps->vertex)
        throw Error("Native edge shader pair lacks its exact compiled owners");
    if(!artifactOnDevice(*vs,device.Get()) || !artifactOnDevice(*ps,device.Get()))throw Error("Native edge shader belongs to another device");
}
void NativeBackend::bindEdgeShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) {
    validateEdgeShaders(vertex,pixel);invalidateScreenReplacement();
    const auto& vs=static_cast<const NativeShaderArtifact&>(vertex);const auto& ps=static_cast<const NativeShaderArtifact&>(pixel);
    context->VSSetShader(vs.vertex.Get(),nullptr,0);context->PSSetShader(ps.pixel.Get(),nullptr,0);
    requireEdgeShaders(vertex,pixel);
}
void NativeBackend::requireEdgeShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) const {
    validateEdgeShaders(vertex,pixel);ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    context->VSGetShader(&vs,nullptr,nullptr);context->PSGetShader(&ps,nullptr,nullptr);
    if(vs.Get()!=static_cast<const NativeShaderArtifact&>(vertex).vertex.Get() ||
       ps.Get()!=static_cast<const NativeShaderArtifact&>(pixel).pixel.Get())
        throw Error("Actual native edge shader bindings differ from effect ownership");
}
void NativeBackend::validateShadowDepthShader(const CompiledMaterial& vertex) const {
    validateSubmissionContext();const auto* vs=dynamic_cast<const NativeShaderArtifact*>(&vertex);
    if(vertex.originalAddress()!=0x820C2FA0||vertex.stage()!=MaterialStage::Vertex||!vs||!vs->vertex||vs->pixel)
        throw Error("Native shadow depth shader lacks its exact compiled owner");
    if(!artifactOnDevice(*vs,device.Get()))throw Error("Native shadow depth shader belongs to another device");
}
void NativeBackend::bindShadowDepthShader(const CompiledMaterial& vertex) {
    validateShadowDepthShader(vertex);invalidateScreenReplacement();
    context->VSSetShader(static_cast<const NativeShaderArtifact&>(vertex).vertex.Get(),nullptr,0);
    context->PSSetShader(nullptr,nullptr,0); // Original pass explicitly has no pixel shader.
    requireShadowDepthShader(vertex);
}
void NativeBackend::requireShadowDepthShader(const CompiledMaterial& vertex) const {
    validateShadowDepthShader(vertex);ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    context->VSGetShader(&vs,nullptr,nullptr);context->PSGetShader(&ps,nullptr,nullptr);
    if(vs.Get()!=static_cast<const NativeShaderArtifact&>(vertex).vertex.Get()||ps)
        throw Error("Actual native shadow depth shader binding differs from its owner");
}
void NativeBackend::validateShadowAlphaShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) const {
    validateSubmissionContext();
    const auto* vs=dynamic_cast<const NativeShaderArtifact*>(&vertex);
    const auto* ps=dynamic_cast<const NativeShaderArtifact*>(&pixel);
    if(vertex.originalAddress()!=0x820C1E6C || pixel.originalAddress()!=0x820CA530 ||
       vertex.stage()!=MaterialStage::Vertex || pixel.stage()!=MaterialStage::Pixel ||
       !vs || !ps || !vs->vertex || !ps->pixel || vs->pixel || ps->vertex)
        throw Error("Native shadow alpha shader pair lacks its exact compiled owners");
    if(!artifactOnDevice(*vs,device.Get()) || !artifactOnDevice(*ps,device.Get()))throw Error("Native shadow alpha shader belongs to another device");
}
void NativeBackend::bindShadowAlphaShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) {
    validateShadowAlphaShaders(vertex,pixel);invalidateScreenReplacement();
    context->VSSetShader(static_cast<const NativeShaderArtifact&>(vertex).vertex.Get(),nullptr,0);
    context->PSSetShader(static_cast<const NativeShaderArtifact&>(pixel).pixel.Get(),nullptr,0);
    requireShadowAlphaShaders(vertex,pixel);
}
void NativeBackend::requireShadowAlphaShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) const {
    validateShadowAlphaShaders(vertex,pixel);ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    context->VSGetShader(&vs,nullptr,nullptr);context->PSGetShader(&ps,nullptr,nullptr);
    if(vs.Get()!=static_cast<const NativeShaderArtifact&>(vertex).vertex.Get() ||
       ps.Get()!=static_cast<const NativeShaderArtifact&>(pixel).pixel.Get())
        throw Error("Actual native shadow alpha shader bindings differ from effect ownership");
}
uint64_t NativeBackend::publishConstants(ConstantBankId id,const void* bytes,UINT size,ComPtr<ID3D11Buffer>& published) {
    requireOwner();
    if(id>=ConstantBankCount || !bytes || !size || (size&15)) throw Error("Native constant bank publish is malformed");
    auto& bank=constantBanks[id];
    if(bank.buffer && bank.device.Get()==device.Get() && bank.bytes==size && bank.contentsValid &&
       bank.contents.size()==size && !std::memcmp(bank.contents.data(),bytes,size)) {
        published=bank.buffer;
        return ++bank.generation; // Identical data still invalidates every older commit.
    }
    if(!bank.buffer || bank.device.Get()!=device.Get() || bank.bytes!=size) {
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=size;desc.Usage=D3D11_USAGE_DYNAMIC;
        desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        ComPtr<ID3D11Buffer> created;
        StallProfiler::Scope allocationProfile(StallProfiler::Section::Rendering,"D3D11.CreateBuffer.constantBank",nullptr,reinterpret_cast<uintptr_t>(device.Get()));
        check(device->CreateBuffer(&desc,nullptr,&created),"constant bank allocation");
        allocationProfile.finish();
        bank.buffer=std::move(created);bank.device=device;bank.bytes=size;
    }
    bank.contentsValid=false; // Failed uploads must never become a later exact-byte hit.
    bank.contents.resize(size); // Any allocation happens before Map exposes driver memory.
    // WRITE_DISCARD renames the storage: queued draws keep their bound bytes.
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_map
    D3D11_MAPPED_SUBRESOURCE mapped{};
    static constexpr const char* mapNames[]={"D3D11.Map.constants.ZPrepass","D3D11.Map.constants.Mono",
        "D3D11.Map.constants.ShadowDepth","D3D11.Map.constants.RigidVertex","D3D11.Map.constants.RigidPixel",
        "D3D11.Map.constants.SkinVertex","D3D11.Map.constants.SkinPixel","D3D11.Map.constants.SkyVertex","D3D11.Map.constants.SkyPixel"};
    static_assert(std::size(mapNames)==ConstantBankCount);
    StallProfiler::Scope mapProfile(StallProfiler::Section::Wait,mapNames[id],nullptr,reinterpret_cast<uintptr_t>(bank.buffer.Get()));
    check(context->Map(bank.buffer.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"constant bank map");
    mapProfile.finish();
    if(!mapped.pData) {context->Unmap(bank.buffer.Get(),0);throw Error("Native constant bank mapped no storage");}
    std::memcpy(mapped.pData,bytes,size);context->Unmap(bank.buffer.Get(),0);
    std::memcpy(bank.contents.data(),bytes,size);bank.contentsValid=true;
    published=bank.buffer;
    return ++bank.generation;
}
const D3D11_BUFFER_DESC* NativeBackend::provenBufferDescriptor(ID3D11Buffer* buffer,BufferDescriptorProof& proof) const {
    if(!buffer)return nullptr;
    if(proof.device && proof.device==device.Get() && proof.buffer.Get()==buffer)return &proof.desc;
    ComPtr<ID3D11Device> owner;buffer->GetDevice(&owner);
    if(owner.Get()!=device.Get())return nullptr;
    D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);
    proof.buffer=buffer;proof.device=device.Get();proof.desc=desc;
    return &proof.desc;
}
std::optional<D3D11_BUFFER_DESC> NativeBackend::booleanBankDescriptor(ID3D11Buffer* buffer) const {
    const D3D11_BUFFER_DESC* desc=nullptr;
    BufferDescriptorProof unproven;
    if(buffer && buffer==zeroBooleanBank.Get())desc=provenBufferDescriptor(buffer,zeroBooleanProof);
    else if(buffer && buffer==monoSkinBooleanBank.Get())desc=provenBufferDescriptor(buffer,monoSkinBooleanProof);
    else desc=provenBufferDescriptor(buffer,unproven);
    if(!desc)return std::nullopt;
    return *desc;
}
void NativeBackend::requireConstantBank(ConstantBankId id,ID3D11Buffer* buffer,uint64_t generation,UINT size,const char* what) const {
    if(id>=ConstantBankCount) throw Error(what);
    const auto& bank=constantBanks[id];
    if(!buffer || buffer!=bank.buffer.Get() || !generation || generation!=bank.generation || bank.bytes!=size) throw Error(what);
    const auto* proven=provenBufferDescriptor(buffer,bank.proof);
    if(!proven) throw Error("Native screen resource belongs to another graphics device");
    const D3D11_BUFFER_DESC desc=*proven;
    if(desc.ByteWidth!=size||desc.Usage!=D3D11_USAGE_DYNAMIC||desc.BindFlags!=D3D11_BIND_CONSTANT_BUFFER||
       desc.CPUAccessFlags!=D3D11_CPU_ACCESS_WRITE||desc.MiscFlags||desc.StructureByteStride) throw Error(what);
}
ComPtr<ID3D11Buffer> NativeBackend::zeroBooleans() {
    requireOwner();
    if(!zeroBooleanBank || zeroBooleanDevice.Get()!=device.Get()) {
        const std::array<uint32_t,4> zero{};
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(zero);desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        const D3D11_SUBRESOURCE_DATA data{zero.data(),0,0};
        ComPtr<ID3D11Buffer> created;check(device->CreateBuffer(&desc,&data,&created),"zero Boolean bank upload");
        zeroBooleanBank=std::move(created);zeroBooleanDevice=device;
    }
    return zeroBooleanBank;
}
class NativeShadowDepthCommit {
    friend class NativeBackend;
    ComPtr<ID3D11Buffer> constants;
    uint64_t generation{};
};
std::shared_ptr<NativeShadowDepthCommit> NativeBackend::commitShadowDepth(const CompiledMaterial& vertex,const ShadowDepthConstants& values) {
    requireShadowDepthShader(vertex);
    for(const auto& row:values)for(const auto value:row)
        if(!std::isfinite(value))throw Error("Nonfinite native character shadow constant");
    auto result=std::make_shared<NativeShadowDepthCommit>();
    result->generation=publishConstants(ShadowDepthBank,values.data(),sizeof(values),result->constants);
    context->VSSetConstantBuffers(0,1,result->constants.GetAddressOf());requireShadowDepthCommit(result);return result;
}
void NativeBackend::requireShadowDepthCommit(const std::shared_ptr<NativeShadowDepthCommit>& commit) const {
    validateSubmissionContext();
    if(!commit||!commit->constants)throw Error("Native character shadow has no committed constants");
    requireConstantBank(ShadowDepthBank,commit->constants.Get(),commit->generation,sizeof(ShadowDepthConstants),
        "Actual native character shadow constants differ from their owner's current bank commit");
    ComPtr<ID3D11Buffer> bound;
    context->VSGetConstantBuffers(0,1,&bound);
    if(bound.Get()!=commit->constants.Get())throw Error("Actual native character shadow constants differ from their owner");
}
ShadowDepthConstants NativeBackend::readbackShadowDepthConstants(const std::shared_ptr<NativeShadowDepthCommit>& commit) {
    requireShadowDepthCommit(commit);D3D11_BUFFER_DESC desc{};commit->constants->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;check(device->CreateBuffer(&desc,nullptr,&staging),"character shadow constant readback allocation");
    context->CopyResource(staging.Get(),commit->constants.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"character shadow constant readback map");
    ShadowDepthConstants values{};std::memcpy(values.data(),mapped.pData,sizeof(values));context->Unmap(staging.Get(),0);return values;
}
void NativeBackend::validateZPrepassShader(const CompiledMaterial& vertex) const {
    validateSubmissionContext();const auto* vs=dynamic_cast<const NativeShaderArtifact*>(&vertex);
    if((vertex.originalAddress()!=0x8214A8A4&&vertex.originalAddress()!=0x8214B9B8)||
       vertex.stage()!=MaterialStage::Vertex||!vs||!vs->vertex||vs->pixel)
        throw Error("Native Z-prepass shader lacks its exact compiled owner");
    if(!artifactOnDevice(*vs,device.Get()))throw Error("Native Z-prepass shader belongs to another device");
}
void NativeBackend::bindZPrepassShader(const CompiledMaterial& vertex) {
    validateZPrepassShader(vertex);flushIm2D();invalidateScreenReplacement();
    context->VSSetShader(static_cast<const NativeShaderArtifact&>(vertex).vertex.Get(),nullptr,0);
    context->PSSetShader(nullptr,nullptr,0);
    requireZPrepassShader(vertex);
}
void NativeBackend::requireZPrepassShader(const CompiledMaterial& vertex) const {
    validateZPrepassShader(vertex);ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    context->VSGetShader(&vs,nullptr,nullptr);context->PSGetShader(&ps,nullptr,nullptr);
    if(vs.Get()!=static_cast<const NativeShaderArtifact&>(vertex).vertex.Get()||ps)
        throw Error("Actual native Z-prepass shader binding differs from its owner");
}
class NativeZPrepassCommit {
    friend class NativeBackend;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11Buffer> constants,booleans;
    uint64_t generation{};
};
std::shared_ptr<NativeZPrepassCommit> NativeBackend::commitZPrepass(
    const CompiledMaterial& vertex,const ZPrepassConstants& values,const ZPrepassBooleans& booleans) {
    requireZPrepassShader(vertex);
    if(booleans!=ZPrepassBooleans{})throw Error("Native static Z-prepass requires Boolean bank zero");
    for(const auto& row:values)for(float value:row)
        if(!std::isfinite(value))throw Error("Nonfinite native Z-prepass constant");
    auto result=std::make_shared<NativeZPrepassCommit>();
    result->vertex=static_cast<const NativeShaderArtifact&>(vertex).vertex;
    result->booleans=zeroBooleans();
    result->generation=publishConstants(ZPrepassBank,values.data(),sizeof(values),result->constants);
    // Bind both banks only after the fully validated upload succeeded.
    flushIm2D();requireZPrepassShader(vertex);
    ComPtr<ID3D11Buffer> previous0,previous1;
    context->VSGetConstantBuffers(0,1,&previous0);context->VSGetConstantBuffers(1,1,&previous1);
    ID3D11Buffer* buffers[]={result->constants.Get(),result->booleans.Get()};
    context->VSSetConstantBuffers(0,2,buffers);
    try {requireZPrepassCommit(result);}
    catch(...) {
        ID3D11Buffer* previous[]={previous0.Get(),previous1.Get()};context->VSSetConstantBuffers(0,2,previous);throw;
    }
    return result;
}
void NativeBackend::requireZPrepassCommit(const std::shared_ptr<NativeZPrepassCommit>& commit) const {
    validateSubmissionContext();
    if(!commit||!commit->vertex||!commit->constants||!commit->booleans)
        throw Error("Native Z-prepass has no committed constants");
    requireConstantBank(ZPrepassBank,commit->constants.Get(),commit->generation,sizeof(ZPrepassConstants),
        "Actual native Z-prepass constants differ from their owner's current bank commit");
    const auto validate=[&](ID3D11Buffer* buffer,UINT bytes) {
        if(!buffer) throw Error("Missing native screen resource");
        const auto proven=booleanBankDescriptor(buffer);
        if(!proven) throw Error("Native screen resource belongs to another graphics device");
        const D3D11_BUFFER_DESC desc=*proven;
        if(desc.ByteWidth!=bytes||desc.Usage!=D3D11_USAGE_IMMUTABLE||desc.BindFlags!=D3D11_BIND_CONSTANT_BUFFER||
           desc.CPUAccessFlags||desc.MiscFlags||desc.StructureByteStride)
            throw Error("Native Z-prepass constant backing differs from its immutable owner");
    };
    validate(commit->booleans.Get(),sizeof(ZPrepassBooleans));
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11Buffer> b0,b1;
    context->VSGetShader(&vs,nullptr,nullptr);context->PSGetShader(&ps,nullptr,nullptr);
    context->VSGetConstantBuffers(0,1,&b0);context->VSGetConstantBuffers(1,1,&b1);
    if(vs.Get()!=commit->vertex.Get()||ps)throw Error("Native Z-prepass commit shader identity differs");
    if(b0.Get()!=commit->constants.Get()||b1.Get()!=commit->booleans.Get())
        throw Error("Actual native Z-prepass constants differ from their owner");
}
void NativeBackend::validateMonoShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) const {
    validateSubmissionContext();const auto* vs=dynamic_cast<const NativeShaderArtifact*>(&vertex);
    if((vertex.originalAddress()!=0x82120C04&&vertex.originalAddress()!=0x82121BE8)||
       vertex.stage()!=MaterialStage::Vertex||!vs||!vs->vertex||vs->pixel)
        throw Error("Native mono shader lacks its exact compiled owner");
    const auto* ps=dynamic_cast<const NativeShaderArtifact*>(&pixel);
    if((pixel.originalAddress()!=0x82122BD4&&pixel.originalAddress()!=0x82122D38)||
       pixel.stage()!=MaterialStage::Pixel||!ps||!ps->pixel||ps->vertex)
        throw Error("Native mono pixel shader lacks its exact compiled owner");
    if(!artifactOnDevice(*ps,device.Get()))throw Error("Native mono pixel shader belongs to another device");
    if(!artifactOnDevice(*vs,device.Get()))throw Error("Native mono shader belongs to another device");
}
void NativeBackend::bindMonoShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) {
    validateMonoShaders(vertex,pixel);flushIm2D();invalidateScreenReplacement();
    context->VSSetShader(static_cast<const NativeShaderArtifact&>(vertex).vertex.Get(),nullptr,0);
    context->PSSetShader(static_cast<const NativeShaderArtifact&>(pixel).pixel.Get(),nullptr,0);
    requireMonoShaders(vertex,pixel);
}
void NativeBackend::monoShaderObjects(const CompiledMaterial& vertex,const CompiledMaterial& pixel,
    ComPtr<ID3D11VertexShader>& vs,ComPtr<ID3D11PixelShader>& ps) const {
    validateMonoShaders(vertex,pixel);
    vs=static_cast<const NativeShaderArtifact&>(vertex).vertex;
    ps=static_cast<const NativeShaderArtifact&>(pixel).pixel;
}
void NativeBackend::requireMonoShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) const {
    validateMonoShaders(vertex,pixel);ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    context->VSGetShader(&vs,nullptr,nullptr);context->PSGetShader(&ps,nullptr,nullptr);
    if(vs.Get()!=static_cast<const NativeShaderArtifact&>(vertex).vertex.Get()||ps.Get()!=static_cast<const NativeShaderArtifact&>(pixel).pixel.Get())
        throw Error("Actual native mono shader binding differs from its owner");
}
class NativeMonoCommit {
    friend class NativeBackend;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants,booleans;
    uint64_t generation{};
    bool skinned{};
};
std::shared_ptr<NativeMonoCommit> NativeBackend::commitMono(
    const CompiledMaterial& vertex,const CompiledMaterial& pixel,const MonoConstants& values,const MonoBooleans& booleans) {
    requireMonoShaders(vertex,pixel);
    if(booleans[0]>1||booleans[1]||booleans[2]||booleans[3])
        throw Error("Native mono requires a canonical skin Boolean and zero unused Boolean lanes");
    for(const auto& row:values)for(float value:row)
        if(!std::isfinite(value))throw Error("Nonfinite native mono constant");
    auto result=std::make_shared<NativeMonoCommit>();
    result->pixel=static_cast<const NativeShaderArtifact&>(pixel).pixel;
    result->vertex=static_cast<const NativeShaderArtifact&>(vertex).vertex;
    result->skinned=booleans[0]!=0;
    if(result->skinned) {
        if(!monoSkinBooleanBank||monoSkinBooleanDevice.Get()!=device.Get()) {
            D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(booleans);desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            const D3D11_SUBRESOURCE_DATA data{booleans.data(),0,0};
            ComPtr<ID3D11Buffer> created;check(device->CreateBuffer(&desc,&data,&created),"mono skin Boolean bank upload");
            monoSkinBooleanBank=std::move(created);monoSkinBooleanDevice=device;
        }
        result->booleans=monoSkinBooleanBank;
    } else result->booleans=zeroBooleans();
    result->generation=publishConstants(MonoBank,values.data(),sizeof(values),result->constants);
    // Bind both banks only after the fully validated upload succeeded.
    flushIm2D();requireMonoShaders(vertex,pixel);
    ComPtr<ID3D11Buffer> previous0,previous1;
    context->VSGetConstantBuffers(0,1,&previous0);context->VSGetConstantBuffers(1,1,&previous1);
    ID3D11Buffer* buffers[]={result->constants.Get(),result->booleans.Get()};
    context->VSSetConstantBuffers(0,2,buffers);
    try {requireMonoCommit(result);}
    catch(...) {
        ID3D11Buffer* previous[]={previous0.Get(),previous1.Get()};context->VSSetConstantBuffers(0,2,previous);throw;
    }
    return result;
}
void NativeBackend::requireMonoCommit(const std::shared_ptr<NativeMonoCommit>& commit,std::optional<bool> skinned) const {
    validateSubmissionContext();
    if(!commit||!commit->vertex||!commit->pixel||!commit->constants||!commit->booleans)
        throw Error("Native mono has no committed constants");
    if(skinned&&commit->skinned!=*skinned)throw Error("Mono mesh skinning profile differs from its committed Boolean");
    requireConstantBank(MonoBank,commit->constants.Get(),commit->generation,sizeof(MonoConstants),
        "Actual native mono constants differ from their owner's current bank commit");
    const auto validate=[&](ID3D11Buffer* buffer,UINT bytes) {
        if(!buffer) throw Error("Missing native screen resource");
        const auto proven=booleanBankDescriptor(buffer);
        if(!proven) throw Error("Native screen resource belongs to another graphics device");
        const D3D11_BUFFER_DESC desc=*proven;
        if(desc.ByteWidth!=bytes||desc.Usage!=D3D11_USAGE_IMMUTABLE||desc.BindFlags!=D3D11_BIND_CONSTANT_BUFFER||
           desc.CPUAccessFlags||desc.MiscFlags||desc.StructureByteStride)
            throw Error("Native mono constant backing differs from its immutable owner");
    };
    validate(commit->booleans.Get(),sizeof(MonoBooleans));
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11Buffer> b0,b1;
    context->VSGetShader(&vs,nullptr,nullptr);context->PSGetShader(&ps,nullptr,nullptr);
    context->VSGetConstantBuffers(0,1,&b0);context->VSGetConstantBuffers(1,1,&b1);
    if(vs.Get()!=commit->vertex.Get()||ps.Get()!=commit->pixel.Get())throw Error("Native mono commit shader identity differs");
    if(b0.Get()!=commit->constants.Get()||b1.Get()!=commit->booleans.Get())
        throw Error("Actual native mono constants differ from their owner");
}
namespace {
template<class T>T readZPrepassBuffer(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Buffer* source) {
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(T);desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;check(device->CreateBuffer(&desc,nullptr,&staging),"Z-prepass constant readback allocation");
    context->CopyResource(staging.Get(),source);D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Z-prepass constant readback map");
    T values{};std::memcpy(values.data(),mapped.pData,sizeof(values));context->Unmap(staging.Get(),0);return values;
}
}
ZPrepassConstants NativeBackend::readbackZPrepassConstants(const std::shared_ptr<NativeZPrepassCommit>& commit) {
    requireZPrepassCommit(commit);return readZPrepassBuffer<ZPrepassConstants>(device.Get(),context.Get(),commit->constants.Get());
}
ZPrepassBooleans NativeBackend::readbackZPrepassBooleans(const std::shared_ptr<NativeZPrepassCommit>& commit) {
    requireZPrepassCommit(commit);return readZPrepassBuffer<ZPrepassBooleans>(device.Get(),context.Get(),commit->booleans.Get());
}
MonoConstants NativeBackend::readbackMonoConstants(const std::shared_ptr<NativeMonoCommit>& commit) {
    requireMonoCommit(commit);return readZPrepassBuffer<MonoConstants>(device.Get(),context.Get(),commit->constants.Get());
}
MonoBooleans NativeBackend::readbackMonoBooleans(const std::shared_ptr<NativeMonoCommit>& commit) {
    requireMonoCommit(commit);return readZPrepassBuffer<MonoBooleans>(device.Get(),context.Get(),commit->booleans.Get());
}
void NativeBackend::validateRigidShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) const {
    validateSubmissionContext();
    const auto* vs=dynamic_cast<const NativeShaderArtifact*>(&vertex);
    const auto* ps=dynamic_cast<const NativeShaderArtifact*>(&pixel);
    const bool originalPair=vertex.originalAddress()==0x8200D3AC&&pixel.originalAddress()==0x8200D9E8;
    const bool texturedPair=vertex.originalAddress()==0x8201700C&&pixel.originalAddress()==0x82017658;
    const bool dualPair=vertex.originalAddress()==0x8202B4CC&&pixel.originalAddress()==0x8202BB44;
    const bool dualAlphaPair=vertex.originalAddress()==0x8202B884&&pixel.originalAddress()==0x8202C3BC;
    const bool dualUVPair=vertex.originalAddress()==0x82046D9C&&pixel.originalAddress()==0x820477A8;
    const bool dualUVAlphaPair=vertex.originalAddress()==0x82047318&&pixel.originalAddress()==0x82047F44;
    const bool singleUVPair=vertex.originalAddress()==0x8204364C&&pixel.originalAddress()==0x82044068;
    const bool singleUVAlphaPair=vertex.originalAddress()==0x82043BC0&&pixel.originalAddress()==0x82044850;
    const bool flipbookPair=vertex.originalAddress()==0x820398BC&&pixel.originalAddress()==0x8203A24C;
    const bool flipbookAlphaPair=vertex.originalAddress()==0x82039D70&&pixel.originalAddress()==0x8203A644;
    const bool glossPair=vertex.originalAddress()==0x8201A07C&&pixel.originalAddress()==0x8201A6EC;
    const bool multitonePair=vertex.originalAddress()==0x82054F2C&&pixel.originalAddress()==0x82055710;
    const bool normalmapPair=vertex.originalAddress()==0x8205855C&&pixel.originalAddress()==0x82058CBC;
    const bool familyAlphaPair=(vertex.originalAddress()==0x8201A430&&pixel.originalAddress()==0x8201B0BC)||
        (vertex.originalAddress()==0x82055440&&pixel.originalAddress()==0x820560B8)||
        (vertex.originalAddress()==0x820589EC&&pixel.originalAddress()==0x82059880);
    const bool skinPair=(vertex.originalAddress()==0x82007C1C&&pixel.originalAddress()==0x8200A02C)||
        (vertex.originalAddress()==0x82008E20&&pixel.originalAddress()==0x8200A4A4)||
        (vertex.originalAddress()==0x8201E6DC&&pixel.originalAddress()==0x82020B9C)||
        (vertex.originalAddress()==0x8201F984&&pixel.originalAddress()==0x82021344) ||
        (vertex.originalAddress()==0x8201146C&&pixel.originalAddress()==0x82013900) ||
        (vertex.originalAddress()==0x820126EC&&pixel.originalAddress()==0x82013F8C) ||
        (vertex.originalAddress()==0x8202579C&&pixel.originalAddress()==0x82027C18) ||
        (vertex.originalAddress()==0x820269E8&&pixel.originalAddress()==0x82028258) ||
        (vertex.originalAddress()==0x8203D93C&&pixel.originalAddress()==0x82040118) ||
        (vertex.originalAddress()==0x8203ED38&&pixel.originalAddress()==0x82040510) ||
        (vertex.originalAddress()==0x8204BA4C&&pixel.originalAddress()==0x8204E2DC) ||
        (vertex.originalAddress()==0x8204CE90&&pixel.originalAddress()==0x8204E75C);
    const bool skyPair=(vertex.originalAddress()==0x82036F08&&pixel.originalAddress()==0x820374E8)||
        (vertex.originalAddress()==0x82036C2C&&pixel.originalAddress()==0x820371EC);
    const bool chocolatePair=(vertex.originalAddress()==0x8205E0B8&&pixel.originalAddress()==0x8205EED4)||
        (vertex.originalAddress()==0x8205DABC&&pixel.originalAddress()==0x8205E5E0)||
        (vertex.originalAddress()==0x82051EEC&&pixel.originalAddress()==0x82052610)||
        (vertex.originalAddress()==0x82052358&&pixel.originalAddress()==0x82052DF4);
    const bool a168Pair=vertex.originalAddress()==0x8201739C&&pixel.originalAddress()==0x82017E4C;
    const bool rigidAlphaPair=vertex.originalAddress()==0x8200D734&&pixel.originalAddress()==0x8200E1BC;
    const bool vfxPair=vertex.originalAddress()==0x8205BE70&&pixel.originalAddress()==0x8205C100;
    if((!flipbookPair&&!flipbookAlphaPair&&!singleUVPair&&!singleUVAlphaPair&&!familyAlphaPair&&!dualUVPair&&!dualUVAlphaPair&&!vfxPair&&!rigidAlphaPair&&!originalPair&&!texturedPair&&!dualPair&&!dualAlphaPair&&!glossPair&&!multitonePair&&!normalmapPair&&!skinPair&&!skyPair&&!chocolatePair&&!a168Pair)||
       vertex.stage()!=MaterialStage::Vertex||pixel.stage()!=MaterialStage::Pixel||
       !vs||!ps||!vs->vertex||!ps->pixel||vs->pixel||ps->vertex)
        throw Error("Native rigid shader pair lacks its exact compiled owners");
    if(!artifactOnDevice(*vs,device.Get())||!artifactOnDevice(*ps,device.Get()))throw Error("Native rigid shader belongs to another device");
}
void NativeBackend::bindRigidShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) {
    validateRigidShaders(vertex,pixel);flushIm2D();invalidateScreenReplacement();
    context->VSSetShader(static_cast<const NativeShaderArtifact&>(vertex).vertex.Get(),nullptr,0);
    context->PSSetShader(static_cast<const NativeShaderArtifact&>(pixel).pixel.Get(),nullptr,0);
    requireRigidShaders(vertex,pixel);
}
void NativeBackend::rigidShaderObjects(const CompiledMaterial& vertex,const CompiledMaterial& pixel,
    ComPtr<ID3D11VertexShader>& vs,ComPtr<ID3D11PixelShader>& ps) const {
    validateRigidShaders(vertex,pixel);
    vs=static_cast<const NativeShaderArtifact&>(vertex).vertex;
    ps=static_cast<const NativeShaderArtifact&>(pixel).pixel;
}
void NativeBackend::requireRigidShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) const {
    validateRigidShaders(vertex,pixel);ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    context->VSGetShader(&vs,nullptr,nullptr);context->PSGetShader(&ps,nullptr,nullptr);
    if(vs.Get()!=static_cast<const NativeShaderArtifact&>(vertex).vertex.Get()||
       ps.Get()!=static_cast<const NativeShaderArtifact&>(pixel).pixel.Get()) {
        std::fprintf(stderr,"[NATIVE RIGID SHADER OWNER] original=%08X/%08X expected=%p/%p actual=%p/%p epoch=%llu screen_draws=%llu\n",
            vertex.originalAddress(),pixel.originalAddress(),
            static_cast<void*>(static_cast<const NativeShaderArtifact&>(vertex).vertex.Get()),
            static_cast<void*>(static_cast<const NativeShaderArtifact&>(pixel).pixel.Get()),
            static_cast<void*>(vs.Get()),static_cast<void*>(ps.Get()),
            static_cast<unsigned long long>(screenShaderEpoch),static_cast<unsigned long long>(screenDraws));
        std::fflush(stderr);
        throw Error("Actual native rigid shader bindings differ from their owners");
    }
}
class NativeRigidCommit {
    friend class NativeBackend;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> vertexConstants,pixelConstants;
    uint64_t vertexGeneration{},pixelGeneration{};
};
class NativeSkinCommit {
    friend class NativeBackend;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> vertexConstants,pixelConstants;
    uint64_t vertexGeneration{},pixelGeneration{};
};
class NativeSkyCommit {
    friend class NativeBackend;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> vertexConstants,pixelConstants;
    uint64_t vertexGeneration{},pixelGeneration{};
};
void NativeBackend::validateSkinShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) const {
    validateSubmissionContext();
    const auto* vs=dynamic_cast<const NativeShaderArtifact*>(&vertex);
    const auto* ps=dynamic_cast<const NativeShaderArtifact*>(&pixel);
    if(!((vertex.originalAddress()==0x82007C1C&&pixel.originalAddress()==0x8200A02C)||
         (vertex.originalAddress()==0x82008E20&&pixel.originalAddress()==0x8200A4A4)||
         (vertex.originalAddress()==0x8201E6DC&&pixel.originalAddress()==0x82020B9C)||
         (vertex.originalAddress()==0x8201F984&&pixel.originalAddress()==0x82021344) ||
        (vertex.originalAddress()==0x8201146C&&pixel.originalAddress()==0x82013900) ||
        (vertex.originalAddress()==0x820126EC&&pixel.originalAddress()==0x82013F8C) ||
        (vertex.originalAddress()==0x8202579C&&pixel.originalAddress()==0x82027C18) ||
        (vertex.originalAddress()==0x820269E8&&pixel.originalAddress()==0x82028258) ||
        (vertex.originalAddress()==0x8203D93C&&pixel.originalAddress()==0x82040118) ||
        (vertex.originalAddress()==0x8203ED38&&pixel.originalAddress()==0x82040510) ||
        (vertex.originalAddress()==0x8204BA4C&&pixel.originalAddress()==0x8204E2DC) ||
        (vertex.originalAddress()==0x8204CE90&&pixel.originalAddress()==0x8204E75C))||
       vertex.stage()!=MaterialStage::Vertex||pixel.stage()!=MaterialStage::Pixel||
       !vs||!ps||!vs->vertex||!ps->pixel||vs->pixel||ps->vertex)
        throw Error("Native skin shader pair lacks its exact compiled owners");
    if(!artifactOnDevice(*vs,device.Get())||!artifactOnDevice(*ps,device.Get()))throw Error("Native skin shader belongs to another device");
}
void NativeBackend::bindSkinShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) {
    validateSkinShaders(vertex,pixel);flushIm2D();invalidateScreenReplacement();
    context->VSSetShader(static_cast<const NativeShaderArtifact&>(vertex).vertex.Get(),nullptr,0);
    context->PSSetShader(static_cast<const NativeShaderArtifact&>(pixel).pixel.Get(),nullptr,0);
    requireSkinShaders(vertex,pixel);
}
void NativeBackend::requireSkinShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) const {
    validateSkinShaders(vertex,pixel);ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    context->VSGetShader(&vs,nullptr,nullptr);context->PSGetShader(&ps,nullptr,nullptr);
    if(vs.Get()!=static_cast<const NativeShaderArtifact&>(vertex).vertex.Get()||
       ps.Get()!=static_cast<const NativeShaderArtifact&>(pixel).pixel.Get())
        throw Error("Actual native skin shader bindings differ from their owners");
}
void NativeBackend::skinShaderObjects(const CompiledMaterial& vertex,const CompiledMaterial& pixel,
    ComPtr<ID3D11VertexShader>& vs,ComPtr<ID3D11PixelShader>& ps) const {
    validateSkinShaders(vertex,pixel);
    vs=static_cast<const NativeShaderArtifact&>(vertex).vertex;
    ps=static_cast<const NativeShaderArtifact&>(pixel).pixel;
}
std::shared_ptr<NativeSkinCommit> NativeBackend::commitSkin(const CompiledMaterial& vertex,const CompiledMaterial& pixel,
    const SkinVertexConstants& vertices,const SkinPixelConstants& pixels) {
    requireSkinShaders(vertex,pixel);
    for(const auto& row:vertices)for(float value:row)
        if(!std::isfinite(value))throw Error("Nonfinite native skin vertex constant");
    for(const auto& row:pixels)for(float value:row)
        if(!std::isfinite(value))throw Error("Nonfinite native skin pixel constant");
    auto result=std::make_shared<NativeSkinCommit>();
    result->vertex=static_cast<const NativeShaderArtifact&>(vertex).vertex;
    result->pixel=static_cast<const NativeShaderArtifact&>(pixel).pixel;
    result->vertexGeneration=publishConstants(SkinVertexBank,vertices.data(),sizeof(vertices),result->vertexConstants);
    result->pixelGeneration=publishConstants(SkinPixelBank,pixels.data(),sizeof(pixels),result->pixelConstants);
    flushIm2D();requireSkinShaders(vertex,pixel);
    ComPtr<ID3D11Buffer> oldVS,oldPS;context->VSGetConstantBuffers(0,1,&oldVS);context->PSGetConstantBuffers(0,1,&oldPS);
    context->VSSetConstantBuffers(0,1,result->vertexConstants.GetAddressOf());
    context->PSSetConstantBuffers(0,1,result->pixelConstants.GetAddressOf());
    try {requireSkinCommit(result);}
    catch(...) {
        auto* v=oldVS.Get();auto* p=oldPS.Get();context->VSSetConstantBuffers(0,1,&v);context->PSSetConstantBuffers(0,1,&p);throw;
    }
    return result;
}
void NativeBackend::requireSkinCommit(const std::shared_ptr<NativeSkinCommit>& commit) const {
    validateSubmissionContext();
    if(!commit||!commit->vertex||!commit->pixel||!commit->vertexConstants||!commit->pixelConstants)
        throw Error("Native skin material has no committed constants");
    requireConstantBank(SkinVertexBank,commit->vertexConstants.Get(),commit->vertexGeneration,sizeof(SkinVertexConstants),
        "Actual native skin vertex constants differ from their owner's current bank commit");
    requireConstantBank(SkinPixelBank,commit->pixelConstants.Get(),commit->pixelGeneration,sizeof(SkinPixelConstants),
        "Actual native skin pixel constants differ from their owner's current bank commit");
}
SkinVertexConstants NativeBackend::readbackSkinVertexConstants(const std::shared_ptr<NativeSkinCommit>& commit) {
    requireSkinCommit(commit);return readZPrepassBuffer<SkinVertexConstants>(device.Get(),context.Get(),commit->vertexConstants.Get());
}
SkinPixelConstants NativeBackend::readbackSkinPixelConstants(const std::shared_ptr<NativeSkinCommit>& commit) {
    requireSkinCommit(commit);return readZPrepassBuffer<SkinPixelConstants>(device.Get(),context.Get(),commit->pixelConstants.Get());
}
std::shared_ptr<NativeRigidCommit> NativeBackend::commitRigid(const CompiledMaterial& vertex,const CompiledMaterial& pixel,
    const RigidVertexConstants& vertices,const RigidPixelConstants& pixels) {
    requireRigidShaders(vertex,pixel);
    for(const auto& row:vertices)for(float value:row)
        if(!std::isfinite(value))throw Error("Nonfinite native rigid vertex constant");
    for(const auto& row:pixels)for(float value:row)
        if(!std::isfinite(value))throw Error("Nonfinite native rigid pixel constant");
    auto result=std::make_shared<NativeRigidCommit>();
    result->vertex=static_cast<const NativeShaderArtifact&>(vertex).vertex;
    result->pixel=static_cast<const NativeShaderArtifact&>(pixel).pixel;
    result->vertexGeneration=publishConstants(RigidVertexBank,vertices.data(),sizeof(vertices),result->vertexConstants);
    result->pixelGeneration=publishConstants(RigidPixelBank,pixels.data(),sizeof(pixels),result->pixelConstants);
    flushIm2D();requireRigidShaders(vertex,pixel);
    ComPtr<ID3D11Buffer> oldVS,oldPS;context->VSGetConstantBuffers(0,1,&oldVS);context->PSGetConstantBuffers(0,1,&oldPS);
    context->VSSetConstantBuffers(0,1,result->vertexConstants.GetAddressOf());
    context->PSSetConstantBuffers(0,1,result->pixelConstants.GetAddressOf());
    try {requireRigidCommit(result);}
    catch(...) {
        auto* v=oldVS.Get();auto* p=oldPS.Get();context->VSSetConstantBuffers(0,1,&v);context->PSSetConstantBuffers(0,1,&p);throw;
    }
    return result;
}
void NativeBackend::requireRigidCommit(const std::shared_ptr<NativeRigidCommit>& commit) const {
    validateSubmissionContext();
    if(!commit||!commit->vertex||!commit->pixel||!commit->vertexConstants||!commit->pixelConstants)
        throw Error("Native rigid material has no committed constants");
    requireConstantBank(RigidVertexBank,commit->vertexConstants.Get(),commit->vertexGeneration,sizeof(RigidVertexConstants),
        "Actual native rigid vertex constants differ from their owner's current bank commit");
    requireConstantBank(RigidPixelBank,commit->pixelConstants.Get(),commit->pixelGeneration,sizeof(RigidPixelConstants),
        "Actual native rigid pixel constants differ from their owner's current bank commit");
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11Buffer> vb,pb;
    context->VSGetShader(&vs,nullptr,nullptr);context->PSGetShader(&ps,nullptr,nullptr);
    context->VSGetConstantBuffers(0,1,&vb);context->PSGetConstantBuffers(0,1,&pb);
    if(vs.Get()!=commit->vertex.Get()||ps.Get()!=commit->pixel.Get())throw Error("Native rigid commit shader identity differs");
    if(vb.Get()!=commit->vertexConstants.Get()||pb.Get()!=commit->pixelConstants.Get())
        throw Error("Actual native rigid constants differ from their owner");
}
RigidVertexConstants NativeBackend::readbackRigidVertexConstants(const std::shared_ptr<NativeRigidCommit>& commit) {
    requireRigidCommit(commit);return readZPrepassBuffer<RigidVertexConstants>(device.Get(),context.Get(),commit->vertexConstants.Get());
}
RigidPixelConstants NativeBackend::readbackRigidPixelConstants(const std::shared_ptr<NativeRigidCommit>& commit) {
    requireRigidCommit(commit);return readZPrepassBuffer<RigidPixelConstants>(device.Get(),context.Get(),commit->pixelConstants.Get());
}
std::shared_ptr<NativeSkyCommit> NativeBackend::commitSky(const CompiledMaterial& vertex,const CompiledMaterial& pixel,
    const SkyVertexConstants& vertices,const SkyPixelConstants& pixels) {
    if(!((vertex.originalAddress()==0x82036C2C&&pixel.originalAddress()==0x820371EC)||
         (vertex.originalAddress()==0x82036F08&&pixel.originalAddress()==0x820374E8)))
        throw Error("Native sky commit requires an exact matching original sky shader pair");
    requireRigidShaders(vertex,pixel);
    for(const auto& row:vertices)for(float value:row)
        if(!std::isfinite(value))throw Error("Nonfinite native sky vertex constant");
    for(const auto& row:pixels)for(float value:row)
        if(!std::isfinite(value))throw Error("Nonfinite native sky pixel constant");
    for(const auto& row:vertices)for(float value:row)
        if(!std::isfinite(value))throw Error("Nonfinite native sky vertex constant");
    for(const auto& row:pixels)for(float value:row)
        if(!std::isfinite(value))throw Error("Nonfinite native sky pixel constant");
    auto result=std::make_shared<NativeSkyCommit>();
    result->vertex=static_cast<const NativeShaderArtifact&>(vertex).vertex;
    result->pixel=static_cast<const NativeShaderArtifact&>(pixel).pixel;
    result->vertexGeneration=publishConstants(SkyVertexBank,vertices.data(),sizeof(vertices),result->vertexConstants);
    result->pixelGeneration=publishConstants(SkyPixelBank,pixels.data(),sizeof(pixels),result->pixelConstants);
    flushIm2D();requireRigidShaders(vertex,pixel);
    ComPtr<ID3D11Buffer> oldVS,oldPS;context->VSGetConstantBuffers(0,1,&oldVS);context->PSGetConstantBuffers(0,1,&oldPS);
    context->VSSetConstantBuffers(0,1,result->vertexConstants.GetAddressOf());
    context->PSSetConstantBuffers(0,1,result->pixelConstants.GetAddressOf());
    try {requireSkyCommit(result);}
    catch(...) {
        auto* v=oldVS.Get();auto* p=oldPS.Get();context->VSSetConstantBuffers(0,1,&v);context->PSSetConstantBuffers(0,1,&p);throw;
    }
    return result;
}
void NativeBackend::requireSkyCommit(const std::shared_ptr<NativeSkyCommit>& commit) const {
    validateSubmissionContext();
    if(!commit||!commit->vertex||!commit->pixel||!commit->vertexConstants||!commit->pixelConstants)
        throw Error("Native sky material has no committed constants");
    requireConstantBank(SkyVertexBank,commit->vertexConstants.Get(),commit->vertexGeneration,sizeof(SkyVertexConstants),
        "Actual native sky vertex constants differ from their owner's current bank commit");
    requireConstantBank(SkyPixelBank,commit->pixelConstants.Get(),commit->pixelGeneration,sizeof(SkyPixelConstants),
        "Actual native sky pixel constants differ from their owner's current bank commit");
}
class NativeEdgeCommit {
    friend class NativeBackend;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    std::shared_ptr<RenderTarget> source;
    uint32_t pixelAddress{};
    EdgeAAInputs edgeAA;
    std::array<ComPtr<ID3D11SamplerState>,4> additionalSamplers;
    const NativeBackend* owner{};
    uint64_t generation{};
};
ComPtr<ID3D11Buffer> NativeBackend::edgeConstantBuffer(const void* bytes,UINT size) {
    requireOwner();
    if(!device || !context || !bytes || (size!=sizeof(EdgeConstants) && size!=sizeof(EdgeAAConstants)))
        throw Error("Invalid native edge constant cache input");
    if(edgeConstantDevice.Get()!=device.Get()) {
        edgeConstantDevice=device;
        for(auto& entry:edgeConstants) {entry.buffer.Reset();entry.bytes=0;}
        edgeConstantNext=0;
    }
    for(const auto& entry:edgeConstants)
        if(entry.buffer && entry.bytes==size && !std::memcmp(entry.contents.data(),bytes,size))return entry.buffer;
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=size;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    const D3D11_SUBRESOURCE_DATA data{bytes,0,0};
    ComPtr<ID3D11Buffer> created;
    StallProfiler::Scope allocationProfile(StallProfiler::Section::Rendering,"D3D11.CreateBuffer.edgeConstants",nullptr,reinterpret_cast<uintptr_t>(device.Get()));
    check(device->CreateBuffer(&desc,&data,&created),"edge constant upload");
    allocationProfile.finish();
    // Cached storage is immutable. Eviction drops this reference only; commits
    // and queued D3D commands retain their exact data until they finish.
    auto& slot=edgeConstants[edgeConstantNext];edgeConstantNext=(edgeConstantNext+1)%edgeConstants.size();
    slot.bytes=size;std::memcpy(slot.contents.data(),bytes,size);slot.buffer=created;
    return created;
}
struct EdgePipeline {
    static constexpr std::array<ScreenVertex,4> canonicalVertices={ScreenVertex{-1,1,0,0},ScreenVertex{1,1,1,0},
        ScreenVertex{-1,-1,0,1},ScreenVertex{1,-1,1,1}};
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vertices;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11RasterizerState> raster;
    explicit EdgePipeline(ID3D11Device* device) {
        const D3D11_INPUT_ELEMENT_DESC elements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}};
        check(device->CreateInputLayout(elements,2,kVSEdge,sizeof(kVSEdge),&layout),"edge input layout creation");
        D3D11_BUFFER_DESC verticesDesc{};verticesDesc.ByteWidth=sizeof(canonicalVertices);verticesDesc.Usage=D3D11_USAGE_IMMUTABLE;
        verticesDesc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        const D3D11_SUBRESOURCE_DATA verticesData{canonicalVertices.data(),0,0};
        check(device->CreateBuffer(&verticesDesc,&verticesData,&vertices),"edge original vertex upload");
        D3D11_BLEND_DESC b{};auto& color=b.RenderTarget[0];
        color.SrcBlend=D3D11_BLEND_ONE;color.DestBlend=D3D11_BLEND_ZERO;color.BlendOp=D3D11_BLEND_OP_ADD;
        color.SrcBlendAlpha=D3D11_BLEND_ONE;color.DestBlendAlpha=D3D11_BLEND_ZERO;color.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        color.RenderTargetWriteMask=15;
        check(device->CreateBlendState(&b,&blend),"edge replace blend creation");
        D3D11_DEPTH_STENCIL_DESC d{};d.DepthFunc=D3D11_COMPARISON_ALWAYS;
        d.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};d.BackFace=d.FrontFace;
        check(device->CreateDepthStencilState(&d,&depth),"edge disabled depth creation");
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
        check(device->CreateRasterizerState(&r,&raster),"edge raster creation");
    }
};
std::shared_ptr<NativeEdgeCommit> NativeBackend::commitEdge(const std::shared_ptr<RenderTarget>& source,
        const EdgeConstants& values,const D3D11_SAMPLER_DESC& sampler,bool antiAlias) {
    flushIm2D();
    validateSubmissionContext();validateFrontTarget(source);
    for(const auto& row:values.kernel)finiteColor(row);
    finiteColor(values.dimensions);
    if(values.dimensions[0]!=float(source->width) || values.dimensions[1]!=float(source->height) ||
       (!antiAlias && (values.dimensions[2]<0 || values.dimensions[2]>2.5f || values.dimensions[2]*2!=std::floor(values.dimensions[2]*2))))
        throw Error("Native edge dimensions/line width exceed the qualified profile");
    if(sampler.Filter!=D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR || sampler.AddressU!=D3D11_TEXTURE_ADDRESS_WRAP ||
       sampler.AddressV!=D3D11_TEXTURE_ADDRESS_WRAP || sampler.AddressW!=D3D11_TEXTURE_ADDRESS_WRAP ||
       sampler.MipLODBias || sampler.MinLOD || sampler.MaxLOD!=13 || sampler.MaxAnisotropy!=1 ||
       sampler.ComparisonFunc!=D3D11_COMPARISON_NEVER)
        throw Error("Native edge requires its point/wrap, single-level sampler profile");
    // D3D11 would silently null a conflicting SRV. Reject before any bindings.
    std::array<ID3D11RenderTargetView*,8> outputs{};context->OMGetRenderTargets(8,outputs.data(),nullptr);
    bool alias=false;
    for(auto* output:outputs)if(output){ComPtr<ID3D11Resource> resource;output->GetResource(&resource);
        alias|=resource.Get()==source->texture.Get();output->Release();}
    if(alias)throw Error("Native edge source aliases an actual output");
    auto result=std::make_shared<NativeEdgeCommit>();result->source=source;
    result->pixelAddress=antiAlias?0x820302EC:0x8202E840;
    static_assert(sizeof(EdgeConstants)==144);
    auto effective=values;
    // Sub-resolution scene copies cannot resolve a half-texel ink kernel if
    // its offsets are still divided by the larger logical camera extent:
    // point sampling then selects the center again and drops the outlines.
    // Retain the authored coverage above 100%, and the authored texel span
    // below it. The guest constants were qualified before this adaptation.
    effective.dimensions[0]=float(std::min(source->width,source->pixelWidth()));
    effective.dimensions[1]=float(std::min(source->height,source->pixelHeight()));
    // Keep the original export/compositor while replacing neighbor smoothing.
    if(antiAlias && antialiasingMode==Antialiasing::FXAA)
        for(auto& tap:effective.kernel)tap[0]=tap[1]=0;
    result->constants=edgeConstantBuffer(&effective,sizeof(effective));
    result->sampler=sceneSamplerState(sampler);
    result->owner=this;result->generation=++edgeCommitGeneration;
    context->PSSetConstantBuffers(0,1,result->constants.GetAddressOf());
    context->PSSetSamplers(0,1,result->sampler.GetAddressOf());
    context->PSSetShaderResources(0,1,source->sampledView.GetAddressOf());
    requireEdgeCommit(result);return result;
}
void NativeBackend::validateEdgeAAInputs(const EdgeAAInputs& inputs) const {
    validateSubmissionContext();
    for(const auto& color:{inputs.color,inputs.base,inputs.line})validateFrontTarget(color);
    validateDepthCopyTarget(inputs.depth);validateTexture(inputs.palette);
    if(inputs.color==inputs.base || inputs.color==inputs.line || inputs.base==inputs.line ||
       inputs.color->width!=inputs.base->width || inputs.color->height!=inputs.base->height ||
       inputs.color->width!=inputs.line->width || inputs.color->height!=inputs.line->height ||
       inputs.color->width!=inputs.depth->pixelWidth() || inputs.color->height!=inputs.depth->pixelHeight() ||
       inputs.palette->format!=TextureFormat::RGBA8 || inputs.palette->levelCount()!=1 ||
       inputs.palette->width!=64 || inputs.palette->height!=64)
        throw Error("Native edgeAA requires three distinct matching color copies, matching depth and a 64x64 RGBA8 palette");
    std::array<ID3D11RenderTargetView*,8> outputs{};ComPtr<ID3D11DepthStencilView> depth;
    context->OMGetRenderTargets(8,outputs.data(),&depth);bool alias=false;
    for(auto* output:outputs)if(output){ComPtr<ID3D11Resource> resource;output->GetResource(&resource);
        for(const auto& color:{inputs.color,inputs.base,inputs.line})alias|=resource.Get()==color->texture.Get();output->Release();}
    if(depth){ComPtr<ID3D11Resource> resource;depth->GetResource(&resource);alias|=resource.Get()==inputs.depth->texture.Get();}
    if(alias)throw Error("Native edgeAA input aliases an actual color or depth attachment");
}
std::shared_ptr<NativeEdgeCommit> NativeBackend::commitEdgeAA(const EdgeAAInputs& inputs,
        const EdgeAAConstants& values,const std::array<D3D11_SAMPLER_DESC,5>& samplers) {
    flushIm2D();
    validateEdgeAAInputs(inputs);
    for(const auto& value:values.c20_27)finiteColor(value);
    for(const auto& value:values.c48_50)finiteColor(value);
    if(values.c20_27[3][0]!=float(inputs.color->width) || values.c20_27[4][0]!=float(inputs.color->height) ||
       values.c48_50[0][0]!=10 || values.c48_50[1][0]<0 || values.c48_50[1][0]>2.5f)
        throw Error("Native edgeAA requires the owned dimensions, original ten-sample count and bounded blur width");
    for(const auto& sampler:samplers)if(sampler.Filter!=D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR ||
       sampler.AddressU!=D3D11_TEXTURE_ADDRESS_WRAP || sampler.AddressV!=D3D11_TEXTURE_ADDRESS_WRAP ||
       sampler.AddressW!=D3D11_TEXTURE_ADDRESS_WRAP || sampler.MipLODBias || sampler.MinLOD || sampler.MaxLOD!=13 ||
       sampler.MaxAnisotropy!=1 || sampler.ComparisonFunc!=D3D11_COMPARISON_NEVER)
        throw Error("Native edgeAA requires five original point/wrap samplers");
    auto result=std::make_shared<NativeEdgeCommit>();result->pixelAddress=0x82034900;result->source=inputs.color;result->edgeAA=inputs;
    static_assert(sizeof(EdgeAAConstants)==176);
    auto effective=values;
    // Match the ink detector/AA sampling span when the scene is downscaled.
    // Palette, cel-shading, depth-fade and all guest-owned values stay intact.
    effective.c20_27[3][0]=float(std::min(inputs.color->width,inputs.color->pixelWidth()));
    effective.c20_27[4][0]=float(std::min(inputs.color->height,inputs.color->pixelHeight()));
    // This shader also decodes cel shading/shadows and composites ink outlines.
    // Zero radius samples the center in its ten-tap loops, preserving that work.
    if(antialiasingMode==Antialiasing::FXAA)effective.c48_50[1][0]=0;
    result->constants=edgeConstantBuffer(&effective,sizeof(effective));
    std::array<ID3D11SamplerState*,5> nativeSamplers{};
    for(size_t i=0;i<5;++i){auto& owner=i?result->additionalSamplers[i-1]:result->sampler;
        owner=sceneSamplerState(samplers[i]);nativeSamplers[i]=owner.Get();}
    result->owner=this;result->generation=++edgeCommitGeneration;
    const std::array<ID3D11ShaderResourceView*,5> views={inputs.color->sampledView.Get(),inputs.depth->depthView.Get(),
        inputs.palette->view.Get(),inputs.base->sampledView.Get(),inputs.line->sampledView.Get()};
    context->PSSetConstantBuffers(0,1,result->constants.GetAddressOf());context->PSSetSamplers(0,5,nativeSamplers.data());
    context->PSSetShaderResources(0,5,views.data());requireEdgeCommit(result);return result;
}
void NativeBackend::requireEdgeCommit(const std::shared_ptr<NativeEdgeCommit>& commit) const {
    validateSubmissionContext();
    if(!commit || !commit->constants || !commit->sampler)throw Error("Native edge has no committed parameters");
    if(commit->owner!=this || commit->generation!=edgeCommitGeneration)
        throw Error("Native edge commit is stale or belongs to another backend");
    validateFrontTarget(commit->source);sameDevice(commit->constants.Get(),device.Get());
    ComPtr<ID3D11Buffer> constants;ComPtr<ID3D11SamplerState> sampler;ComPtr<ID3D11ShaderResourceView> source;
    context->PSGetConstantBuffers(0,1,&constants);context->PSGetSamplers(0,1,&sampler);context->PSGetShaderResources(0,1,&source);
    if(constants.Get()!=commit->constants.Get() || sampler.Get()!=commit->sampler.Get() || source.Get()!=commit->source->sampledView.Get())
        throw Error("Actual native edge constants/sampler/source differ from the committed owners");
    if(commit->pixelAddress==0x82034900){
        validateEdgeAAInputs(commit->edgeAA);const auto& inputs=commit->edgeAA;
        const std::array<ID3D11ShaderResourceView*,4> expected={inputs.depth->depthView.Get(),inputs.palette->view.Get(),
            inputs.base->sampledView.Get(),inputs.line->sampledView.Get()};
        for(UINT i=0;i<4;++i){ComPtr<ID3D11ShaderResourceView> actual;ComPtr<ID3D11SamplerState> actualSampler;
            context->PSGetShaderResources(i+1,1,&actual);context->PSGetSamplers(i+1,1,&actualSampler);
            if(!commit->additionalSamplers[i] || actual.Get()!=expected[i] || actualSampler.Get()!=commit->additionalSamplers[i].Get())
                throw Error("Actual edgeAA texture/sampler slot differs from its committed owner");}
    }
}
void NativeBackend::bindEdgeDeclaration() {
    validateSubmissionContext();
    if(!edgePipeline)edgePipeline=std::make_shared<EdgePipeline>(device.Get());
    context->IASetInputLayout(edgePipeline->layout.Get());
}
void NativeBackend::drawEdge(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,
        const std::shared_ptr<NativeEdgeCommit>& commit,const std::array<ScreenVertex,3>& input,
        const CompiledMaterial& vertex,const CompiledMaterial& pixel) {
    requireNoOutputUavs();
    flushIm2D();
    requireEdgeShaders(vertex,pixel);requireEdgeCommit(commit);validateFrontTarget(target);
    if(commit->pixelAddress!=pixel.originalAddress())throw Error("Native post-effect commit belongs to another shader profile");
    requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);
    if(!depth || target->width!=commit->source->width || target->height!=commit->source->height || target==commit->source)
        throw Error("Native edge requires distinct equal-sized source/output and retained depth ownership");
    ComPtr<ID3D11InputLayout> layout;context->IAGetInputLayout(&layout);
    if(!edgePipeline || layout.Get()!=edgePipeline->layout.Get())throw Error("Native edge original declaration was not bound");
    const auto vp=viewport();
    if(!vp || vp->TopLeftX || vp->TopLeftY || vp->Width!=float(target->width) || vp->Height!=float(target->height) || vp->MinDepth!=0 || vp->MaxDepth!=1)
        throw Error("Native edge requires its whole-target native viewport");
    ComPtr<ID3D11Predicate> predicate;BOOL predicateValue{};context->GetPredication(&predicate,&predicateValue);
    ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;
    context->GSGetShader(&gs,nullptr,nullptr);context->HSGetShader(&hs,nullptr,nullptr);context->DSGetShader(&ds,nullptr,nullptr);
    std::array<ID3D11Buffer*,4> streams{};context->SOGetTargets(4,streams.data());bool streamOutput=false;
    for(auto* stream:streams)if(stream){streamOutput=true;stream->Release();}
    if(predicate || gs || hs || ds || streamOutput)throw Error("Native edge has unqualified native predication/additional stages");
    // The original rectangle list provides exactly three clip-space corners.
    // Restrict this consumer to the proven whole-camera normalized UV rectangle.
    constexpr std::array<ScreenVertex,3> expected={ScreenVertex{-1,1,0,0},ScreenVertex{1,1,1,0},ScreenVertex{-1,-1,0,1}};
    for(size_t i=0;i<input.size();++i)if(input[i].x!=expected[i].x || input[i].y!=expected[i].y || input[i].u!=expected[i].u || input[i].v!=expected[i].v)
        throw Error("Native edge original rectangle differs from the qualified geometry");
    const std::array<ScreenVertex,4> vertices={input[0],input[1],input[2],ScreenVertex{input[1].x,input[2].y,input[1].u,input[2].v}};
    auto buffer=edgePipeline->vertices;
    if(std::memcmp(vertices.data(),EdgePipeline::canonicalVertices.data(),sizeof(vertices))) {
        // The qualified float comparisons accept signed-zero UVs. Preserve
        // those bytes too, falling back to the original immutable upload.
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(vertices);desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        const D3D11_SUBRESOURCE_DATA data{vertices.data(),0,0};
        StallProfiler::Scope allocationProfile(StallProfiler::Section::Rendering,"D3D11.CreateBuffer.edgeVertices",nullptr,reinterpret_cast<uintptr_t>(device.Get()));
        check(device->CreateBuffer(&desc,&data,buffer.ReleaseAndGetAddressOf()),"edge original vertex upload");
    }
    const UINT stride=sizeof(ScreenVertex),offset=0;
    context->IASetVertexBuffers(0,1,buffer.GetAddressOf(),&stride,&offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->RSSetState(edgePipeline->raster.Get());
    context->OMSetBlendState(edgePipeline->blend.Get(),nullptr,UINT32_MAX);
    context->OMSetDepthStencilState(edgePipeline->depth.Get(),0);
    // Edge zero/one or AA five-sample means use native UNORM quantization with
    // blending disabled. AA rounding is covered by independent packed tests;
    // console precision parity is not implied by that native policy.
    // D3D11 owns the immutable vertices and bound resources through submission.
    StallProfiler::Scope drawProfile(StallProfiler::Section::Rendering,"D3D11.Draw.edge",nullptr,reinterpret_cast<uintptr_t>(context.Get()));
    context->Draw(4,0);
    drawProfile.finish();
    check(device->GetDeviceRemovedReason(),"edge draw submission");
    if(commit->pixelAddress==0x82034900)++edgeAADraws;
    else if(commit->pixelAddress==0x820302EC)++aaDraws;else ++edgeDraws;
}
std::shared_ptr<RenderTarget> NativeBackend::createTarget(uint32_t width,uint32_t height,TargetFormat format,TargetScale scale) {
    requireOwner();
    if(!width || !height || width>16384 || height>16384) throw Error("Native target dimensions exceed D3D11 bounds");
    DXGI_FORMAT nativeFormat;uint32_t bytes;
    switch(format) {
    case TargetFormat::RGBA8:nativeFormat=DXGI_FORMAT_R8G8B8A8_UNORM;bytes=4;break;
    case TargetFormat::RGB10A2:nativeFormat=DXGI_FORMAT_R10G10B10A2_UNORM;bytes=4;break;
    case TargetFormat::RGBA16Float:nativeFormat=DXGI_FORMAT_R16G16B16A16_FLOAT;bytes=8;break;
    case TargetFormat::RGBA32Float:nativeFormat=DXGI_FORMAT_R32G32B32A32_FLOAT;bytes=16;break;
    default:throw Error("Unsupported native target format");
    }
    const auto extent=renderExtent(width,height,scale);renderingAllocated=true;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=extent[0];desc.Height=extent[1];
    desc.MipLevels=1;desc.ArraySize=1;desc.Format=nativeFormat;desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    auto result=std::make_shared<RenderTarget>();result->width=width;result->height=height;
    result->physicalWidth=desc.Width;result->physicalHeight=desc.Height;
    result->format=format;result->rowBytes=desc.Width*bytes;
    check(device->CreateTexture2D(&desc,nullptr,&result->texture),"target allocation");
    tagRenderExtent(result->texture.Get(),width,height);
    check(device->CreateRenderTargetView(result->texture.Get(),nullptr,&result->view),"target view creation");
    check(device->CreateShaderResourceView(result->texture.Get(),nullptr,&result->sampledView),"target sampled view creation");
    return result;
}
void NativeBackend::clearTarget(const std::shared_ptr<RenderTarget>& target,const std::array<float,4>& color) {
    flushIm2D();
    requireOwner();
    if(!target) throw Error("Missing native screen target");
    sameDevice(target->texture.Get(),device.Get());finiteColor(color);
    context->ClearRenderTargetView(target->view.Get(),color.data());
}
std::vector<uint8_t> NativeBackend::readbackTarget(const std::shared_ptr<RenderTarget>& target) {
    flushIm2D();
    requireOwner();
    if(!target) throw Error("Missing native screen target");
    sameDevice(target->texture.Get(),device.Get());
    D3D11_TEXTURE2D_DESC desc{};target->texture->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&desc,nullptr,&staging),"target readback allocation");
    context->CopyResource(staging.Get(),target->texture.Get());
    std::vector<uint8_t> result(size_t(target->rowBytes)*target->pixelHeight());
    D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"target readback map");
    for(uint32_t y=0;y<target->pixelHeight();++y)
        memcpy(result.data()+size_t(y)*target->rowBytes,static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch,target->rowBytes);
    context->Unmap(staging.Get(),0);
    return result;
}
void NativeBackend::drawCoronaQueries(const std::shared_ptr<RenderTarget>& scene,const std::shared_ptr<DepthTarget>& sceneDepth,
    const std::shared_ptr<DepthTarget>& sampledDepth,const std::shared_ptr<RenderTarget>& backup,
    const std::shared_ptr<RenderTarget>& query,std::span<const CoronaVertex> vertices,uint32_t depthCompare) {
    requireNoOutputUavs();
    validateSubmissionContext();validateFrontTarget(scene);validateFrontTarget(backup);validateFrontTarget(query);
    validateDepthCopyTarget(sceneDepth);validateDepthCopyTarget(sampledDepth);
    if(vertices.empty()||vertices.size()>512||depthCompare>7||scene->width<64||scene->height<8||
       scene->format!=TargetFormat::RGB10A2||backup->format!=TargetFormat::RGB10A2||query->format!=TargetFormat::RGB10A2||
       backup->width!=64||backup->height!=64||query->width!=64||query->height!=8||
       sampledDepth->width!=scene->width||sampledDepth->height!=scene->height||scene==backup||scene==query||backup==query)
        throw Error("Corona query extent/format/ownership differs");
    if(coronaQueryDraws==UINT64_MAX)throw Error("Native corona query draw counter exhausted");
    for(const auto& v:vertices){for(float f:v.position)if(!std::isfinite(f))throw Error("Invalid query position");
        for(float f:v.region)if(!std::isfinite(f))throw Error("Invalid query region");
        for(float f:v.inverseSizeDepth)if(!std::isfinite(f))throw Error("Invalid query inverse/depth");}
    flushIm2D();requireSelectedTargets({scene,nullptr,nullptr,nullptr},sceneDepth);
    ComPtr<ID3D11Predicate> predicate;BOOL predicateValue{};context->GetPredication(&predicate,&predicateValue);
    if(predicate)throw Error("Corona queries cannot execute under predication");
    if(!screenPipeline)screenPipeline=std::make_shared<ScreenPipeline>(device.Get());auto& p=*screenPipeline;
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=UINT(vertices.size_bytes());bd.Usage=D3D11_USAGE_IMMUTABLE;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    const D3D11_SUBRESOURCE_DATA initial{vertices.data(),0,0};ComPtr<ID3D11Buffer> vb;
    check(device->CreateBuffer(&bd,&initial,&vb),"query vertex snapshot");
    const Constants post{{1,1,1,1},0,0,3,0};bd.ByteWidth=sizeof(post);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    const D3D11_SUBRESOURCE_DATA postData{&post,0,0};ComPtr<ID3D11Buffer> pc;
    check(device->CreateBuffer(&bd,&postData,&pc),"query post constants");
    D3D11_TEXTURE2D_DESC td{};query->texture->GetDesc(&td);td.Format=DXGI_FORMAT_R10G10B10A2_UINT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> packed;ComPtr<ID3D11RenderTargetView> output;
    check(device->CreateTexture2D(&td,nullptr,&packed),"query packed target");
    check(device->CreateRenderTargetView(packed.Get(),nullptr,&output),"query packed view");
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=D3D11_TEXTURE_ADDRESS_MIRROR;
    sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxAnisotropy=1;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;
    ComPtr<ID3D11SamplerState> sampler;check(device->CreateSamplerState(&sd,&sampler),"query point sampler");
    D3D11_BLEND_DESC blend{};blend.RenderTarget[0].RenderTargetWriteMask=15;ComPtr<ID3D11BlendState> replace;
    check(device->CreateBlendState(&blend,&replace),"query replace state");
    D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=TRUE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;dd.DepthFunc=D3D11_COMPARISON_FUNC(depthCompare+1);
    dd.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};dd.BackFace=dd.FrontFace;
    ComPtr<ID3D11DepthStencilState> postDepth;check(device->CreateDepthStencilState(&dd,&postDepth),"query post depth");
    UINT viewportCount=16;std::array<D3D11_VIEWPORT,16> savedViewports{};context->RSGetViewports(&viewportCount,savedViewports.data());
    ComPtr<ID3D11ShaderResourceView> savedQuery;context->PSGetShaderResources(1,1,&savedQuery);
    // Original8276AAA0..AAE4 leaves the restored Screen_Xenon program
    // selected. Its one completed query is separate from screen geometry.
    invalidateScreenReplacement();
    ID3D11ShaderResourceView* nulls[2]{};context->PSSetShaderResources(0,2,nulls);context->OMSetRenderTargets(0,nullptr,nullptr);
    // The original saves the upper 64x8 scene tile, draws queries into that
    // tile, resolves it, then restores it. Native distinct resources preserve
    // identical scene pixels and both copy destinations without touching EDRAM.
    const D3D11_BOX tile{0,0,0,64,8,1};
    context->CopySubresourceRegion(backup->texture.Get(),0,0,0,0,scene->texture.Get(),0,&tile);
    context->CopySubresourceRegion(packed.Get(),0,0,0,0,scene->texture.Get(),0,&tile);
    auto* rt=output.Get();context->OMSetRenderTargets(1,&rt,nullptr);
    context->OMSetBlendState(replace.Get(),nullptr,0xFFFFFFFF);context->OMSetDepthStencilState(p.depth.Get(),0);
    // The fixed 64x8 query tile occupies physical pixels even when the scene
    // is larger. Its normalized scene-depth samples still cover the camera.
    context->RSSetState(p.rasterizer.Get());const D3D11_VIEWPORT vp{0,0,float(scene->width),float(scene->height),0,1};context->RSSetViewports(1,&vp);
    UINT stride=sizeof(CoronaVertex),offset=0;auto* buffer=vb.Get();context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
    context->IASetInputLayout(p.coronaLayout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    context->VSSetShader(p.coronaVS.Get(),nullptr,0);context->PSSetShader(p.coronaPS.Get(),nullptr,0);
    context->GSSetShader(nullptr,nullptr,0);context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);
    auto* sample=sampler.Get();context->PSSetSamplers(0,1,&sample);auto* view=sampledDepth->depthView.Get();context->PSSetShaderResources(0,1,&view);
    context->Draw(UINT(vertices.size()),0);++coronaQueryDraws;
    context->OMSetRenderTargets(0,nullptr,nullptr);context->CopyResource(query->texture.Get(),packed.Get());
    context->PSSetShaderResources(0,2,nulls);rt=scene->view.Get();context->OMSetRenderTargets(1,&rt,sceneDepth->view.Get());
    context->RSSetViewports(viewportCount,savedViewports.data());context->OMSetDepthStencilState(postDepth.Get(),0);
    context->VSSetShader(p.texturedVS.Get(),nullptr,0);context->PSSetShader(p.texturedPS.Get(),nullptr,0);
    context->IASetInputLayout(p.texturedLayout.Get());buffer=nullptr;stride=0;context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
    buffer=pc.Get();context->PSSetConstantBuffers(0,1,&buffer);
    view=backup->sampledView.Get();context->PSSetShaderResources(0,1,&view);view=savedQuery.Get();context->PSSetShaderResources(1,1,&view);
    completedOriginalCoronaQueryDraw=coronaQueryDraws;
}
void NativeBackend::drawScreen(const std::shared_ptr<RenderTarget>& target,const ScreenDraw& draw) {
    drawScreenImpl(target,draw,{},false,false,7,false);
}
void NativeBackend::invalidateScreenReplacement() {
    requireOwner();
    if(screenShaderEpoch==UINT64_MAX)throw Error("Native screen shader epoch exhausted");
    ++screenShaderEpoch;completedOriginalScreenDraw=0;completedOriginalModulatedPostFilterDraw=0;completedScreenInputBind=0;
    completedOriginalCoronaQueryDraw=0;completedOriginalMovieDraw=0;
    completedScreenBatchRetirement=0;
    completedBindingReset=0;
}
NativeScreenReplacementReceipt NativeBackend::completedScreenReplacement(uint64_t before,
    const CompiledMaterial& vertex,const CompiledMaterial& pixel) const {
    requireOwner();
    if(before==UINT64_MAX||screenDraws!=before+1||completedOriginalScreenDraw!=screenDraws)
        throw Error("Native screen replacement has no single completed original draw");
    const auto* vs=dynamic_cast<const NativeShaderArtifact*>(&vertex);
    const auto* ps=dynamic_cast<const NativeShaderArtifact*>(&pixel);
    if(vertex.stage()!=MaterialStage::Vertex||pixel.stage()!=MaterialStage::Pixel||!vs||!ps||!vs->vertex||!ps->pixel)
        throw Error("Native screen replacement lacks original compiled shader owners");
    if(!artifactOnDevice(*vs,device.Get())||!artifactOnDevice(*ps,device.Get()))throw Error("Native screen replacement shader owner is foreign");
    NativeScreenReplacementReceipt receipt;receipt.owner=this;receipt.draw=screenDraws;receipt.epoch=screenShaderEpoch;
    receipt.vertex=vertex.originalAddress();receipt.pixel=pixel.originalAddress();
    context->VSGetShader(&receipt.retainedVertex,nullptr,nullptr);context->PSGetShader(&receipt.retainedPixel,nullptr,nullptr);
    requireScreenReplacement(receipt);return receipt;
}
NativeScreenReplacementReceipt NativeBackend::completedCoronaQueryReplacement(uint64_t before) const {
    requireOwner();
    if(before==UINT64_MAX||coronaQueryDraws!=before+1||completedOriginalCoronaQueryDraw!=coronaQueryDraws||!screenPipeline)
        throw Error("Native corona replacement has no single completed original query");
    NativeScreenReplacementReceipt receipt;receipt.owner=this;receipt.draw=screenDraws;receipt.epoch=screenShaderEpoch;
    receipt.coronaQuery=coronaQueryDraws;receipt.vertex=0x82152880;receipt.pixel=0x82152708;
    receipt.retainedVertex=screenPipeline->texturedVS;receipt.retainedPixel=screenPipeline->texturedPS;
    receipt.restoredQueryLayout=screenPipeline->texturedLayout;
    requireScreenReplacement(receipt);return receipt;
}
void NativeBackend::requireCompletedModulatedPostFilter(uint64_t before) const {
    requireOwner();
    if(before==UINT64_MAX||postFilterDraws!=before+1||completedOriginalModulatedPostFilterDraw!=postFilterDraws)
        throw Error("Native screen replacement lacks one completed original modulated draw");
}
NativeScreenReplacementReceipt NativeBackend::completedRestoredScreenReplacement(uint64_t before,
    const NativeScreenReplacementReceipt& previous) const {
    requireCompletedModulatedPostFilter(before);
    if(previous.restoredPostFilter>before)
        throw Error("Native restored screen replacement has an unrelated post-filter history");
    auto receipt=previous;receipt.restoredPostFilter=postFilterDraws;
    requireScreenReplacement(receipt);return receipt;
}
void NativeBackend::requireCompletedDistortion(uint64_t before) const {
    requireOwner();
    if(before>UINT64_MAX-5||postFilterDraws!=before+5||completedOriginalDistortionDraw!=postFilterDraws)
        throw Error("Native screen replacement lacks the five completed original distortion draws");
}
NativeScreenReplacementReceipt NativeBackend::completedDistortionScreenReplacement(uint64_t before,
    const NativeScreenReplacementReceipt& previous) const {
    requireCompletedDistortion(before);
    if(previous.restoredPostFilter>before)
        throw Error("Native distortion screen replacement has an unrelated post-filter history");
    auto receipt=previous;receipt.restoredPostFilter=postFilterDraws;
    requireScreenReplacement(receipt);return receipt;
}
void NativeBackend::requireScreenReplacement(const NativeScreenReplacementReceipt& receipt) const {
    requireOwner();
    const bool flat=receipt.vertex==0x821525E8&&receipt.pixel==0x821524C8;
    const bool textured=receipt.vertex==0x82152880&&receipt.pixel==0x82152708;
    const bool corona=receipt.vertex==0x82152880&&receipt.pixel==0x821536F0;
    const bool movie=receipt.vertex==0x82152880&&receipt.pixel==0x82152B68;
    const bool completed=receipt.movie?(movie&&!receipt.coronaQuery&&receipt.movie==movieDraws&&
        receipt.movie==completedOriginalMovieDraw&&receipt.movieQueryCount==coronaQueryDraws):receipt.coronaQuery?(textured&&receipt.coronaQuery==coronaQueryDraws&&
        receipt.coronaQuery==completedOriginalCoronaQueryDraw):(receipt.draw&&receipt.draw==completedOriginalScreenDraw);
    if(receipt.owner!=this||!completed||receipt.draw!=screenDraws||!receipt.retainedVertex||!receipt.retainedPixel||
       receipt.epoch!=screenShaderEpoch||(!movie&&!screenPipeline)||(!flat&&!textured&&!corona&&!movie)||
       receipt.restoredPostFilter>postFilterDraws)
        throw Error("Native screen replacement receipt is stale or foreign");
    if(movie){requireMovieReplacement(receipt);return;}
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;UINT vertexClasses=0,pixelClasses=0;
    context->VSGetShader(&vertex,nullptr,&vertexClasses);context->PSGetShader(&pixel,nullptr,&pixelClasses);
    if(vertexClasses||pixelClasses||vertex.Get()!=receipt.retainedVertex.Get()||pixel.Get()!=receipt.retainedPixel.Get()||
       vertex.Get()!=(flat?screenPipeline->flatVS.Get():screenPipeline->texturedVS.Get())||
       pixel.Get()!=(flat?screenPipeline->flatPS.Get():(corona?screenPipeline->coronaSpritePS.Get():screenPipeline->texturedPS.Get())))
        throw Error("Actual native screen replacement bindings differ from their completed draw");
    if(receipt.coronaQuery) {
        ComPtr<ID3D11InputLayout> layout;context->IAGetInputLayout(&layout);
        if(!receipt.restoredQueryLayout||receipt.restoredQueryLayout.Get()!=screenPipeline->texturedLayout.Get()||
           (layout.Get()!=receipt.restoredQueryLayout.Get()&&(!completedScreenBatchRetirement||layout)))
            throw Error("Actual native corona replacement layout differs from its completed restore");
    }
}
void NativeBackend::retireScreenReplacement(const NativeScreenReplacementReceipt& receipt) {
    requireScreenReplacement(receipt);invalidateScreenReplacement();
}
void NativeBackend::requireScreenInputReplacement(const NativeScreenInputReceipt& receipt) const {
    requireOwner();
    if(receipt.owner!=this||!receipt.bind||receipt.bind!=completedScreenInputBind||receipt.bind!=screenInputBinds||
       receipt.epoch!=screenShaderEpoch||receipt.draw!=screenDraws||!screenPipeline)
        throw Error("Native sprite input replacement receipt is stale or foreign");
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;UINT vertexClasses=0,pixelClasses=0;
    context->VSGetShader(&vertex,nullptr,&vertexClasses);context->PSGetShader(&pixel,nullptr,&pixelClasses);
    if(vertexClasses||pixelClasses||vertex.Get()!=screenPipeline->texturedVS.Get()||pixel.Get()!=receipt.retainedPixel.Get())
        throw Error("Actual native sprite input replacement differs from its original binding transaction");
}
void NativeBackend::retireScreenInputReplacement(const NativeScreenInputReceipt& receipt) {
    requireScreenInputReplacement(receipt);invalidateScreenReplacement();
}
void NativeBackend::drawOriginalScreen(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,
                                      const ScreenDraw& draw,bool depthWrite,uint32_t depthCompare,bool postDepthEnable) {
    drawScreenImpl(target,draw,depth,true,depthWrite,depthCompare,postDepthEnable);
}
void NativeBackend::drawScreenImpl(const std::shared_ptr<RenderTarget>& target,const ScreenDraw& draw,
                                  const std::shared_ptr<DepthTarget>& depth,bool original,bool depthWrite,uint32_t depthCompare,bool postDepthEnable) {
    requireNoOutputUavs();
    flushIm2D();
    requireOwner();
    if(!target) throw Error("Missing native screen target");
    sameDevice(target->texture.Get(),device.Get());
    if(original) {
        validateFrontTarget(target);
        const bool alphaClear=draw.colorWriteMask==8 && !draw.texture && !draw.coronaQuery &&
            draw.blendSelector==3 && !draw.alphaTest && draw.color==std::array<float,4>{};
        if(target->format!=TargetFormat::RGB10A2 || !depth || depthCompare>7 || (draw.colorWriteMask!=15 && !alphaClear))
            throw Error("Original screen requires RGB10A2, depth ownership and a qualified color mask");
        requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);
        ComPtr<ID3D11Predicate> predicate;BOOL value{};context->GetPredication(&predicate,&value);
        if(predicate)throw Error("Original screen cannot execute under native predication");
    }
    if(draw.blendSelector>3 || (draw.colorWriteMask&~15)) throw Error("Unsupported screen blend selector or color mask");
    if(draw.coronaQuery){
        validateFrontTarget(draw.coronaQuery);
        if(!original||!draw.texture||draw.blendSelector||draw.alphaTest||draw.coronaQuery->width!=64||draw.coronaQuery->height!=8||
           draw.coronaQuery->format!=TargetFormat::RGB10A2||!std::isfinite(draw.coronaUV[0])||!std::isfinite(draw.coronaUV[1]))
            throw Error("Unqualified corona sprite resources/state");
    }
    finiteColor(draw.color);
    if(!std::isfinite(draw.alphaReference)) throw Error("Nonfinite screen alpha reference");
    for(const auto& vertex:draw.vertices)
        if(!std::isfinite(vertex.x)||!std::isfinite(vertex.y)||!std::isfinite(vertex.u)||!std::isfinite(vertex.v))
            throw Error("Nonfinite screen vertex is unverified");
    ComPtr<ID3D11SamplerState> sampler;
    if(draw.texture) {
        validateTexture(draw.texture);
        check(device->CreateSamplerState(&draw.sampler,&sampler),"explicit sampler state creation");
    }
    D3D11_BLEND_DESC blend{};
    auto& color=blend.RenderTarget[0];color.BlendEnable=!original;
    color.SrcBlend=draw.blendSelector==3?D3D11_BLEND_ONE:D3D11_BLEND_SRC_ALPHA;
    color.DestBlend=draw.blendSelector==1?D3D11_BLEND_INV_SRC_ALPHA:(draw.blendSelector==3?D3D11_BLEND_ZERO:D3D11_BLEND_ONE);
    color.BlendOp=draw.blendSelector==2?D3D11_BLEND_OP_REV_SUBTRACT:D3D11_BLEND_OP_ADD;
    color.SrcBlendAlpha=D3D11_BLEND_ONE;color.DestBlendAlpha=D3D11_BLEND_ZERO;color.BlendOpAlpha=D3D11_BLEND_OP_ADD;
    color.RenderTargetWriteMask=draw.colorWriteMask;
    ComPtr<ID3D11BlendState> blendState;check(device->CreateBlendState(&blend,&blendState),"blend state creation");
    ComPtr<ID3D11DepthStencilState> postDepth;
    ComPtr<ID3D11Texture2D> packedOutput;
    ComPtr<ID3D11RenderTargetView> packedView;
    std::array<D3D11_VIEWPORT,16> savedViewports{};UINT savedViewportCount=16;
    ComPtr<ID3D11ShaderResourceView> savedTexture1;
    ComPtr<ID3D11ShaderResourceView> savedTexture2;
    ComPtr<ID3D11Buffer> coronaConstants;
    ComPtr<ID3D11SamplerState> querySampler;
    if(draw.coronaQuery){
        const std::array<float,8> words={draw.color[0],draw.color[1],draw.color[2],draw.color[3],draw.coronaUV[0],draw.coronaUV[1],0,0};
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(words);bd.Usage=D3D11_USAGE_IMMUTABLE;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        const D3D11_SUBRESOURCE_DATA initial{words.data(),0,0};check(device->CreateBuffer(&bd,&initial,&coronaConstants),"corona sprite constants");
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=D3D11_TEXTURE_ADDRESS_MIRROR;
        sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxAnisotropy=1;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;
        check(device->CreateSamplerState(&sd,&querySampler),"corona query sampler");
        context->PSGetShaderResources(2,1,&savedTexture2);
    }
    if(original) {
        D3D11_DEPTH_STENCIL_DESC post{};post.DepthEnable=postDepthEnable;
        post.DepthWriteMask=depthWrite?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;
        post.DepthFunc=D3D11_COMPARISON_FUNC(depthCompare+1);
        post.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};post.BackFace=post.FrontFace;
        check(device->CreateDepthStencilState(&post,&postDepth),"original post-draw depth state creation");
        // Integer output prevents adapter-dependent UNORM conversion from
        // rounding the recovered blend result a second time. Same packed family;
        // CopyResource transfers the bits without changing the engine target.
        // Use actual storage dimensions (including SSAA/internal scale), never
        // logical guest/front dimensions. The exact device and full normalized
        // descriptor are the cache key; allocation/view failures publish nothing.
        D3D11_TEXTURE2D_DESC packed=*attachmentDescriptor(*target);
        packed.Format=DXGI_FORMAT_R10G10B10A2_UINT;packed.BindFlags=D3D11_BIND_RENDER_TARGET;
        if(screenScratch.device.Get()!=device.Get() || !screenScratch.texture || !screenScratch.view ||
           !sameTextureDescriptor(screenScratch.desc,packed)) {
            ScreenScratchTexture next;next.device=device;next.desc=packed;
            {
                StallProfiler::Scope profile(StallProfiler::Section::Rendering,"D3D11.CreateTexture2D.screenScratch",nullptr,reinterpret_cast<uintptr_t>(device.Get()));
                check(device->CreateTexture2D(&packed,nullptr,&next.texture),"packed integer screen allocation");
            }
            {
                StallProfiler::Scope profile(StallProfiler::Section::Rendering,"D3D11.CreateRenderTargetView.screenScratch",nullptr,reinterpret_cast<uintptr_t>(next.texture.Get()));
                check(device->CreateRenderTargetView(next.texture.Get(),nullptr,&next.view),"packed integer screen view creation");
            }
            screenScratch=std::move(next);
        }
        packedOutput=screenScratch.texture;packedView=screenScratch.view;
        // Scratch has only RT binding capability, remains private, and exact
        // selected-target validation above rules out a retained OM scratch view.
        if(packedOutput.Get()==target->texture.Get() || (draw.texture&&packedOutput.Get()==draw.texture->texture.Get()) ||
           (draw.coronaQuery&&packedOutput.Get()==draw.coronaQuery->texture.Get()))
            throw Error("Native screen scratch aliases an engine resource");
        context->RSGetViewports(&savedViewportCount,savedViewports.data());
        context->PSGetShaderResources(1,1,&savedTexture1);
    }
    if(!screenPipeline) screenPipeline=std::make_shared<ScreenPipeline>(device.Get());
    auto& pipeline=*screenPipeline;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(pipeline.vertices.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"vertex snapshot map");
    if(original&&!draw.texture) {
        std::array<float,8> positions{};for(size_t i=0;i<4;++i){positions[2*i]=draw.vertices[i].x;positions[2*i+1]=draw.vertices[i].y;}
        memcpy(mapped.pData,positions.data(),sizeof(positions));
    } else memcpy(mapped.pData,draw.vertices.data(),sizeof(draw.vertices));
    context->Unmap(pipeline.vertices.Get(),0);
    Constants constants{draw.color,draw.alphaReference,uint32_t(draw.alphaTest),draw.blendSelector,0};
    check(context->Map(pipeline.constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"constant snapshot map");
    memcpy(mapped.pData,&constants,sizeof(constants));context->Unmap(pipeline.constants.Get(),0);
    if(original) {
        auto post=constants;post.alphaTest=0;
        check(context->Map(pipeline.postConstants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"post-state constant map");
        memcpy(mapped.pData,&post,sizeof(post));context->Unmap(pipeline.postConstants.Get(),0);
    }
    // All fallible preparation precedes submission or effective pipeline changes.
    invalidateScreenReplacement();
    if(original) {
        context->OMSetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,D3D11_KEEP_UNORDERED_ACCESS_VIEWS,nullptr,nullptr);
        {
            StallProfiler::Scope profile(StallProfiler::Section::Rendering,"D3D11.CopyResource.screenSnapshot",nullptr,reinterpret_cast<uintptr_t>(packedOutput.Get()));
            context->CopyResource(packedOutput.Get(),target->texture.Get());
        }
        auto* packed=packedView.Get();
        context->OMSetRenderTargetsAndUnorderedAccessViews(1,&packed,depth->view.Get(),0,D3D11_KEEP_UNORDERED_ACCESS_VIEWS,nullptr,nullptr);
    }
    ID3D11RenderTargetView* output=target->view.Get();if(!original)context->OMSetRenderTargets(1,&output,nullptr);
    context->OMSetBlendState(blendState.Get(),nullptr,0xFFFFFFFF);
    context->OMSetDepthStencilState(pipeline.depth.Get(),0);
    const D3D11_VIEWPORT logical{0,0,float(target->width),float(target->height),0,1};
    const auto viewport=draw.preserveAspect?contentViewport(target,logical):renderViewport(target,logical);context->RSSetViewports(1,&viewport);
    context->RSSetState(pipeline.rasterizer.Get());
    ID3D11Buffer* vertexBuffer=pipeline.vertices.Get();UINT stride=original&&!draw.texture?8:sizeof(ScreenVertex),offset=0;
    context->IASetVertexBuffers(0,1,&vertexBuffer,&stride,&offset);
    context->IASetInputLayout(draw.texture?pipeline.texturedLayout.Get():pipeline.flatLayout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->VSSetShader(draw.texture?pipeline.texturedVS.Get():pipeline.flatVS.Get(),nullptr,0);
    context->PSSetShader(original?(draw.texture?pipeline.originalTexturedPS.Get():pipeline.originalFlatPS.Get()):
        (draw.texture?pipeline.texturedPS.Get():pipeline.flatPS.Get()),nullptr,0);
    if(draw.coronaQuery){context->PSSetShader(pipeline.coronaSpritePackedPS.Get(),nullptr,0);
        auto* cb=coronaConstants.Get();context->PSSetConstantBuffers(1,1,&cb);
        auto* sampleQuery=querySampler.Get();context->PSSetSamplers(1,1,&sampleQuery);
        auto* query=draw.coronaQuery->sampledView.Get();context->PSSetShaderResources(1,1,&query);}
    context->GSSetShader(nullptr,nullptr,0);context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);
    ID3D11Buffer* constantBuffer=pipeline.constants.Get();context->PSSetConstantBuffers(0,1,&constantBuffer);
    ID3D11ShaderResourceView* view=draw.texture?draw.texture->view.Get():nullptr;
    ID3D11SamplerState* sample=sampler.Get();
    if(draw.texture||!original){context->PSSetShaderResources(0,1,&view);context->PSSetSamplers(0,1,&sample);}
    if(original){auto* sampled=draw.blendSelector!=3?target->sampledView.Get():nullptr;context->PSSetShaderResources(draw.coronaQuery?2:1,1,&sampled);}
    context->Draw(4,0);
    ++screenDraws; // A submitted draw is never represented as an untouched failure.
    completedOriginalModulatedPostFilterDraw=0;completedScreenInputBind=0;
    if(original) {
        auto* restored=draw.coronaQuery?savedTexture2.Get():savedTexture1.Get();context->PSSetShaderResources(draw.coronaQuery?2:1,1,&restored);
        // Snapshot already contains all untouched pixels. Copy back exact packed
        // codes, then retain the original attachments for following engine work.
        context->OMSetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,D3D11_KEEP_UNORDERED_ACCESS_VIEWS,nullptr,nullptr);
        {
            StallProfiler::Scope profile(StallProfiler::Section::Rendering,"D3D11.CopyResource.screenCommit",nullptr,reinterpret_cast<uintptr_t>(packedOutput.Get()));
            context->CopyResource(target->texture.Get(),packedOutput.Get());
        }
        context->OMSetRenderTargetsAndUnorderedAccessViews(1,&output,depth->view.Get(),0,D3D11_KEEP_UNORDERED_ACCESS_VIEWS,nullptr,nullptr);
        context->RSSetViewports(savedViewportCount,savedViewports.data());
        context->OMSetDepthStencilState(postDepth.Get(),0);
        ID3D11Buffer* unbound=nullptr;UINT zero=0;context->IASetVertexBuffers(0,1,&unbound,&zero,&zero);
        // The original retains c0 and shaders while disabling its post-shader
        // alpha test. Upload the real post-state before a later engine draw.
        ID3D11Buffer* postBuffer=pipeline.postConstants.Get();context->PSSetConstantBuffers(0,1,&postBuffer);
        context->PSSetShader(draw.coronaQuery?pipeline.coronaSpritePS.Get():(draw.texture?pipeline.texturedPS.Get():pipeline.flatPS.Get()),nullptr,0);
        completedOriginalScreenDraw=screenDraws;
        return;
    }
    // Release context references to per-submission resources. D3D11 retains any
    // in-flight GPU use; no guest owner or queued borrowed pointer is required.
    view=nullptr;sample=nullptr;context->PSSetShaderResources(0,1,&view);context->PSSetSamplers(0,1,&sample);
    context->OMSetRenderTargets(0,nullptr,nullptr);
}
}
