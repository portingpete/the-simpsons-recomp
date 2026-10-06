#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace Simpsons::Graphics {
struct Error : std::runtime_error {using std::runtime_error::runtime_error;};
using Microsoft::WRL::ComPtr;
// R8 is scalar red-channel UNORM storage; it specifies no luminance swizzle.
enum class TextureFormat {RGBA8, BC1, BC2, BC3, BGRX8, R8};
enum class TargetFormat {RGBA8, RGB10A2, RGBA16Float, RGBA32Float};
enum class TargetScale {Fixed, Scene};
enum class Antialiasing {Original=0, FXAA=1, FXAAOriginal=2, SSAA4x=3};
enum class BufferKind {Vertex, Index16};
class NativeBackend;
class NativeCopySubmission;
class NativeRecordingContext;
class NativeRecordingPayload;
using NativeRecordingMask=std::array<uint8_t,40>;
enum class NativeRecordingPayloadState {Allocated,Recording,Sealed,Failed,Released};
struct NativeRecordingReceipt {
    NativeRecordingPayloadState state{};
    uint32_t capacityBytes{},flags{};
    // capacityBytes retains the original SDK command-buffer allocation size.
    // Native constant snapshots have a separate, explicit host storage budget.
    uint32_t ownedDataCapacityBytes{};
    NativeRecordingMask inputMask{},outputMask{};
    uint64_t ownedDataBytes{},recordedDraws{},executedDraws{},executions{};
};
using NativeRecordingPrepare=std::function<void(ID3D11DeviceContext*,const std::shared_ptr<void>&,std::span<const uint8_t>)>;
class DeviceAvailability;
struct ScreenPipeline;
struct AntialiasingPipeline;
struct Im2DPipeline;
struct Im2DBatch;
struct Im2DDraw;
class MaterialRecord;
class CompiledMaterial;
// Exact proof of a texture object's owning device and creation descriptor, both immutable for
// the object's lifetime; the strong reference pins the object (no address reuse while held).
struct TextureDescriptorProof {
    ComPtr<ID3D11Texture2D> texture;
    const ID3D11Device* device{};
    D3D11_TEXTURE2D_DESC desc{};
};
// The same proof for a buffer object (owning device and creation descriptor).
struct BufferDescriptorProof {
    ComPtr<ID3D11Buffer> buffer;
    const ID3D11Device* device{};
    D3D11_BUFFER_DESC desc{};
};
class Texture {
    friend class NativeBackend;
    friend struct NativeWritableTextureProbe;
    friend struct NativeMipTextureProbe;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    uint32_t rowBytes{},rows{};
    bool writable=false;
    uint32_t mipLevels=1;
    // Exact proof of NativeBackend::validateTextureStorage: the check reads only immutable D3D
    // properties (owning device, texture and view descriptors, the view's resource) and the
    // wrapper fields captured here. The strong references pin the validated objects (no address
    // reuse while held) and every device child keeps its device alive, so while the same
    // objects, device and fields are presented the result cannot differ. Any change forces the
    // full check again.
    struct StorageProof {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> view;
        const ID3D11Device* device{};
        uint32_t width{},height{},rowBytes{},rows{},mipLevels{};
        TextureFormat format{};
        bool writable{};
    };
    mutable StorageProof storageProof;
public:
    uint32_t width{},height{};
    TextureFormat format{};
    uint32_t levelCount() const {return mipLevels;}
};
class RenderTarget {
    friend class NativeBackend;
    friend struct NativeRenderResolutionProbe;
    friend struct NativeAntialiasingProbe;
    friend struct NativeUltrawideProbe;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> view;
    ComPtr<ID3D11ShaderResourceView> sampledView;
    uint32_t rowBytes{};
    uint32_t physicalWidth{},physicalHeight{};
    // Exact proofs (same contract as Texture::StorageProof): validateFrontTarget's full check,
    // and the owning device plus descriptor of `texture` for attachment checks. Strong references
    // pin the validated objects; any change of object, device or captured field re-checks.
    struct FrontProof {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> view;
        ComPtr<ID3D11ShaderResourceView> sampledView;
        const ID3D11Device* device{};
        uint32_t width{},height{},rowBytes{},physicalWidth{},physicalHeight{};
        TargetFormat format{};
    };
    mutable FrontProof frontProof;
    mutable TextureDescriptorProof descriptorProof;
public:
    uint32_t width{},height{};
    uint32_t pixelWidth() const {return physicalWidth;}
    uint32_t pixelHeight() const {return physicalHeight;}
    TargetFormat format{};
};
class CubeTexture {
    friend class NativeBackend;
    friend struct NativeCubeTextureProbe;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    uint32_t extent{};
public:
    uint32_t faceSize() const {return extent;}
};
class Buffer {
    friend class NativeBackend;
    friend struct NativeBufferUploadProbe;
    ComPtr<ID3D11Buffer> buffer;
    uint32_t size{};
    BufferKind kind{};
    mutable BufferDescriptorProof deviceProof; // owning-device proof for validateBufferWrite
public:
    uint32_t byteSize() const {return size;}
    BufferKind type() const {return kind;}
};
class DepthTarget {
    friend class NativeBackend;
    friend struct NativeRenderResolutionProbe;
    friend struct NativeAntialiasingProbe;
    friend struct NativeUltrawideProbe;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11DepthStencilView> view;
    ComPtr<ID3D11ShaderResourceView> depthView,stencilView;
    uint32_t width{},height{};
    uint32_t physicalWidth{},physicalHeight{};
    // Exact proof of validateDepthCopyTarget's full check (same contract as RenderTarget::FrontProof).
    struct CopyProof {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11DepthStencilView> view;
        ComPtr<ID3D11ShaderResourceView> depthView,stencilView;
        const ID3D11Device* device{};
        uint32_t width{},height{},physicalWidth{},physicalHeight{};
    };
    mutable CopyProof copyProof;
    mutable TextureDescriptorProof descriptorProof;
public:
    uint32_t pixelWidth() const {return width;}
    uint32_t pixelHeight() const {return height;}
    uint32_t storageWidth() const {return physicalWidth;}
    uint32_t storageHeight() const {return physicalHeight;}
};
struct ScreenVertex {float x,y,u,v;};
struct CoronaVertex {std::array<float,2> position;std::array<float,4> region;std::array<float,3> inverseSizeDepth;};
struct ScreenDraw {
    // Owned value snapshot: no original guest pointers or borrowed texture IDs.
    std::array<ScreenVertex,4> vertices;
    std::array<float,4> color;
    std::shared_ptr<Texture> texture;
    uint32_t blendSelector; // Original 82756480 selectors 0..3.
    uint8_t colorWriteMask;
    bool alphaTest;
    float alphaReference;
    // Explicit effective state; the backend supplies no inherited defaults.
    D3D11_SAMPLER_DESC sampler;
    std::shared_ptr<RenderTarget> coronaQuery;
    std::array<float,2> coronaUV{};
    bool preserveAspect=false; // Original Apt/HUD scope, never world sprites.
};
struct MovieDraw {
    std::array<ScreenVertex,4> vertices;
    // Already in original sampler order0/2/1; each view samples scalar red.
    std::array<std::shared_ptr<Texture>,3> textures;
    std::array<D3D11_SAMPLER_DESC,3> samplers;
    bool retainedDepthWrite{};
    uint32_t retainedDepthCompare{};
};
struct MoviePipeline;
struct EdgePipeline;
class NativeEdgeCommit;
class NativeShadowDepthCommit;
class NativeShadowMesh;
struct ShadowMeshVertex;
struct ShadowMeshDraw;
using ShadowDepthConstants=std::array<std::array<float,4>,244>;
class NativeZPrepassCommit;
class NativeZPrepassMesh;
struct ZPrepassMeshUploadCache;
struct SkinMeshUploadCache;
struct ZPrepassVertex;
using ZPrepassConstants=std::array<std::array<float,4>,244>;
using ZPrepassBooleans=std::array<uint32_t,4>;
class NativeMonoCommit;
class NativeMonoMesh;
class NativeMonoReplayConstants;
struct MonoMeshUploadCache;
struct RigidMeshUploadCache;
struct ShadowMeshUploadCache;
struct SkyMeshUploadCache;
struct MonoVertex;
using MonoConstants=std::array<std::array<float,4>,244>;
using MonoBooleans=std::array<uint32_t,4>;
static_assert(sizeof(MonoConstants)==244*16 && sizeof(MonoBooleans)==16);
struct MonoMeshDraw;
class NativeRigidCommit;
class NativeRigidMesh;
class NativeRigidReplayConstants;
struct RigidVertex;
struct RigidMeshDraw;
class NativeSkinCommit;
class NativeSkinMesh;
class NativeSkinReplayConstants;
struct SkinVertex;
struct SkinMeshDraw;
class NativeSkyMesh;
class NativeSkyReplayConstants;
struct SkyVertex;
struct SkyMeshDraw;
struct ParticleDraw;
struct ParticlePipeline;
struct ImmediateDraw;
struct ImmediatePipeline;
struct BallEffectDraw;
struct DistortionDraw;
struct BallEffectPipeline;
struct PostFilterDraw;
struct PostFilterPipeline;
class NativeSkyCommit;
// Sky VS needs vc47 for the uv transform (64 float4 = 1024 bytes); sky PS
// needs pc23 plus k252..k255 (51 float4 like rigid staging).
// Skin VS needs wide bone/world/view banks (256 float4 = 4096 bytes);
// skin PS needs k252..k255 plus material registers (64 float4 = 1024 bytes).
using SkinVertexConstants=std::array<std::array<float,4>,256>;
using SkinPixelConstants=std::array<std::array<float,4>,64>;
using SkyVertexConstants=std::array<std::array<float,4>,64>;
using SkyPixelConstants=std::array<std::array<float,4>,51>;
// Include chocolate's c47 UV velocity; smaller rigid shaders accept the larger bank.
using RigidVertexConstants=std::array<std::array<float,4>,48>;
// The original rigid replay stages 56 registers and the gloss PS reads c50;
// the native PS bank carries the full 51-register staging extent (816 bytes).
// Existing 50-register HLSL shaders accept the larger bound buffer.
using RigidPixelConstants=std::array<std::array<float,4>,51>;
struct EdgeConstants {
    std::array<std::array<float,4>,8> kernel;
    std::array<float,4> dimensions; // width, height, line width, inactive.
};
struct EdgeAAConstants {
    std::array<std::array<float,4>,8> c20_27;
    std::array<std::array<float,4>,3> c48_50;
};
struct EdgeAAInputs {
    std::shared_ptr<RenderTarget> color,base,line;
    std::shared_ptr<DepthTarget> depth;
    std::shared_ptr<Texture> palette;
};

// Issued only for one completed original screen draw, corona query, or movie frame. This is
// an opaque receipt, not permission to repair or ignore a shader binding.
class NativeScreenReplacementReceipt {
    friend class NativeBackend;
    const NativeBackend* owner{};
    uint64_t draw{},epoch{},restoredPostFilter{},coronaQuery{},movie{},movieQueryCount{};
    uint32_t vertex{},pixel{};
    ComPtr<ID3D11VertexShader> retainedVertex;
    ComPtr<ID3D11PixelShader> retainedPixel;
    ComPtr<ID3D11InputLayout> restoredQueryLayout;
    ComPtr<ID3D11InputLayout> retainedMovieLayout;
public:
    NativeScreenReplacementReceipt()=default;
};
// The original unready-sprite branch changes its vertex inputs and retains
// the previous pixel shader without drawing. Keep that binding transaction
// distinct from a receipt for completed screen geometry.
class NativeScreenInputReceipt {
    friend class NativeBackend;
    const NativeBackend* owner{};
    uint64_t bind{},epoch{},draw{};
    ComPtr<ID3D11PixelShader> retainedPixel;
public:
    NativeScreenInputReceipt()=default;
};
class NativeScreenBatchReceipt {
    friend class NativeBackend;
    const NativeBackend* owner{};
    uint64_t batch{},epoch{},draw{};
    ComPtr<ID3D11VertexShader> retainedVertex;
    ComPtr<ID3D11PixelShader> retainedPixel;
public:
    NativeScreenBatchReceipt()=default;
};
// The original mode-zero rebuild can clear actual shader bindings while its
// logical effect manager remains selected. Only a completed owned reset issues
// this receipt; guest cache zeros alone cannot establish that transition.
class NativeBindingResetReceipt {
    friend class NativeBackend;
    const NativeBackend* owner{};
    ComPtr<ID3D11Device> device;
    uint64_t reset{},epoch{};
public:
    NativeBindingResetReceipt()=default;
};

// Single-owner immediate context. Callers own shared texture references until
// their submission completes; no guest pointer is retained by this backend.
class NativeBackend {
public:
    explicit NativeBackend(bool software=false,bool vsync=true);
    NativeBackend(const NativeBackend&)=delete;
    NativeBackend& operator=(const NativeBackend&)=delete;
    // Freeze before allocating engine targets/recorded draw commands. Original
    // guest raster/camera coordinates remain logical; D3D storage and coverage
    // use the selected internal extent, including depth and post targets.
    static bool supportsRendering(uint32_t width,uint32_t height,uint32_t anisotropy,
        Antialiasing aa=Antialiasing::Original,uint32_t renderScalePercent=100) noexcept;
    void configureRendering(uint32_t width,uint32_t height,uint32_t anisotropy,
        Antialiasing aa=Antialiasing::Original,uint32_t renderScalePercent=100);
    // Isolated full-screen filter; restores the actual complete engine context.
    // Original returns its source. SSAA resolves four scene pixels per output.
    std::shared_ptr<RenderTarget> resolveAntialiasing(const std::shared_ptr<RenderTarget>& front);
    std::shared_ptr<RenderTarget> presentedFront() const {return lastPresentedFront;}
    Antialiasing antialiasing() const {return antialiasingMode;}
    D3D11_SAMPLER_DESC materialSampling(D3D11_SAMPLER_DESC desc) const;
    D3D11_VIEWPORT renderViewport(const std::shared_ptr<RenderTarget>&,D3D11_VIEWPORT) const;
    D3D11_VIEWPORT contentViewport(const std::shared_ptr<RenderTarget>&,D3D11_VIEWPORT) const;
    double renderAspect() const {return double(renderBaseWidth)/renderBaseHeight;}
    std::array<uint32_t,2> sceneExtent() const {return {internalWidth,internalHeight};}
    D3D11_RECT renderScissor(const std::shared_ptr<RenderTarget>&,D3D11_RECT) const;
    // Matching RGB10A2 swapchain, alpha ignored, explicit full-range SDR G22/P709.
    // No fallback/conversion; desktop output/gamma/scanout equivalence is unproven.
    void attachWindow(HWND window,uint32_t width,uint32_t height);
    void configureVideoPresentation(bool vsync,uint32_t width,uint32_t height);
    // Packed RGB10A2 copies only. The original resolve source must be actual
    // OM color zero. Tokens own both resources; completion is not display acceptance.
    void validateFrontCopy(const std::shared_ptr<RenderTarget>& source,const std::shared_ptr<RenderTarget>& front) const;
    std::shared_ptr<NativeCopySubmission> copyFront(const std::shared_ptr<RenderTarget>& source,const std::shared_ptr<RenderTarget>& front);
    // Exact full-subresource depth/stencil transfer. The source must be the
    // actual selected DSV. No rounding, clear, sampling, draw or rebinding.
    void validateDepthCopy(const std::shared_ptr<DepthTarget>& source,const std::shared_ptr<DepthTarget>& destination) const;
    std::shared_ptr<NativeCopySubmission> copyDepth(const std::shared_ptr<DepthTarget>& source,const std::shared_ptr<DepthTarget>& destination);
    bool copyComplete(const std::shared_ptr<NativeCopySubmission>& submission);
    void waitCopy(const std::shared_ptr<NativeCopySubmission>& submission);
    void validateFrontPresentation(const std::shared_ptr<RenderTarget>& front) const;
    // Queues the front->swapchain transfer, then queues configured DXGI Present, then waits
    // the real transfer event before returning. CopyResource is queued
    // asynchronously on the single-owner immediate context, so order preserves
    // source->front->swapchain. Return still proves actual transfer completion
    // retiring every earlier immediate-context command. Engine buffers can be
    // reused on return.
    bool presentFront(const std::shared_ptr<RenderTarget>& front);
    // Same queued front->swapchain transfer and DXGI Present, without the CPU
    // wait. The returned transfer receipt proves completion only once
    // copyComplete/waitCopy reports it, so the CPU can prepare the next frame
    // while the GPU finishes this one. accepted is false only for occlusion.
    std::shared_ptr<NativeCopySubmission> presentFrontQueued(const std::shared_ptr<RenderTarget>& front,bool& accepted);
    // Five-second bounded GPU event wait. Covers previously queued immediate-
    // context use of every engine buffer; it does not prove compositor/scanout completion.
    void waitIdle();
    // D3D11 owns submission storage. Verify the actual immediate context and
    // device association before an engine integration owner publishes readiness.
    void validateSubmissionContext() const;
    // A real deferred context has separate ownership from its recording
    // payloads. Neither capability is an SDK object in guest memory.
    // Explicit release invalidates every shared alias; later validation/release
    // rejects it. RAII destruction is safe even after this backend is gone.
    std::shared_ptr<NativeRecordingContext> createRecordingContext();
    void validateRecordingContext(const std::shared_ptr<NativeRecordingContext>& recording) const;
    void releaseRecordingContext(const std::shared_ptr<NativeRecordingContext>& recording);
    std::shared_ptr<NativeRecordingPayload> allocateRecordingPayload(const std::shared_ptr<NativeRecordingContext>&,uint32_t capacityBytes);
    void beginRecordingPayload(const std::shared_ptr<NativeRecordingPayload>&,uint32_t flags,
        const NativeRecordingMask& inputMask,const NativeRecordingMask& outputMask);
    NativeRecordingReceipt recordingPayloadReceipt(const std::shared_ptr<NativeRecordingPayload>&) const;
    void recordingPayloadReceipts(std::span<const std::shared_ptr<NativeRecordingPayload>> payloads,
        std::span<NativeRecordingReceipt> receipts) const;
    void finishRecordingPayload(const std::shared_ptr<NativeRecordingPayload>&);
    void executeRecordingPayload(const std::shared_ptr<NativeRecordingPayload>&);
    void releaseRecordingPayload(const std::shared_ptr<NativeRecordingPayload>&);
    uint64_t recordingDrawCount() const;
    uint64_t recordingExecutedDrawCount() const;
    // Immutable upload. R8 is single-level native plane storage only.
    std::shared_ptr<Texture> createTexture(uint32_t width,uint32_t height,TextureFormat format,std::span<const uint8_t> bytes);
    // Explicit RGBA8/BGRX8/BC1/BC2/BC3 levels, largest first. BGRX storage keeps the unused
    // byte; its shader view samples alpha as one. Every level is tightly packed and
    // supplied by the caller; D3D11 performs no mip generation or filtering.
    std::shared_ptr<Texture> createTextureMipChain(uint32_t width,uint32_t height,TextureFormat format,
        std::span<const std::span<const uint8_t>> levels);
    // RGBA8/R8 only: allocate single-level DEFAULT storage with undefined initial
    // pixels. Writes replace the whole image using tightly packed rows: four
    // bytes per RGBA8 pixel, one per R8 pixel. R8 specifies no movie/YUV rendering.
    std::shared_ptr<Texture> createWritableTexture(uint32_t width,uint32_t height,TextureFormat format);
    // UpdateSubresource captures caller bytes before return. No pointer is
    // retained; GPU completion is not implied. Immutable textures reject writes.
    void writeTexture(const std::shared_ptr<Texture>& texture,std::span<const uint8_t> bytes);
    // Validate ownership, layout and actual backing/view, including R8 planes.
    void validateTextureStorage(const std::shared_ptr<Texture>& texture) const;
    // Existing pipeline consumers accept only the previously supported formats;
    // R8 requires a separately qualified consumer and rejects here.
    void validateTexture(const std::shared_ptr<Texture>& texture) const;
    std::vector<uint8_t> readback(const std::shared_ptr<Texture>& texture);
    std::vector<uint8_t> readbackMip(const std::shared_ptr<Texture>& texture,uint32_t level);
    // Original reflection profiles: six RGB10A2 faces, one level, size16/256.
    // Allocation leaves pixels undefined. Storage alone does not establish
    // original face orientation, expanded filtering precision or ZYX1 sampling.
    std::shared_ptr<CubeTexture> createCubeTexture(uint32_t size);
    void validateCubeTexture(const std::shared_ptr<CubeTexture>& cube) const;
    // Full-width, tightly packed rows of one face. Unwritten rows/faces survive.
    // Immediate UpdateSubresource captures the caller's bytes before return.
    void writeCubeRows(const std::shared_ptr<CubeTexture>& cube,uint32_t face,
                       uint32_t firstRow,uint32_t rowCount,std::span<const uint8_t> bytes);
    std::vector<uint8_t> readbackCubeFace(const std::shared_ptr<CubeTexture>& cube,uint32_t face);
    std::shared_ptr<Buffer> createBuffer(uint32_t size,BufferKind kind);
    void bindEngineVertexStorage(const std::shared_ptr<Buffer>& buffer,uint32_t offset,uint32_t stride);
    void writeBuffer(const std::shared_ptr<Buffer>& buffer,uint32_t offset,std::span<const uint8_t> bytes);
    // Original Im2D raw vertex storage only. Own each byte before returning;
    // combine adjacent writes to the same actual resource up to 256 KiB.
    // Every Im2D flush (including clears/copies/readback/presentation/idle)
    // submits these bytes before the draws. General writeBuffer also drains it.
    // Im2D rendering uses a separate decoded vertex buffer, never this storage.
    void queueIm2DBufferWrite(const std::shared_ptr<Buffer>& buffer,uint32_t offset,std::span<const uint8_t> bytes);
    // Actual native CopySubresourceRegion/UpdateSubresource calls, not completion.
    uint64_t bufferUploadCount() const {return bufferUploads;}
    std::vector<uint8_t> readbackBuffer(const std::shared_ptr<Buffer>& buffer);
    std::shared_ptr<RenderTarget> createTarget(uint32_t width,uint32_t height,TargetFormat format,TargetScale scale=TargetScale::Fixed);
    std::shared_ptr<DepthTarget> createDepthTarget(uint32_t width,uint32_t height,TargetScale scale=TargetScale::Fixed);
    // Engine target selection only. Validates the complete attachment set before
    // changing the immediate context; no viewport, clear or draw is implied.
    void bindTargets(const std::array<std::shared_ptr<RenderTarget>,4>& colors,const std::shared_ptr<DepthTarget>& depth);
    void requireSelectedTargets(const std::array<std::shared_ptr<RenderTarget>,4>& colors,const std::shared_ptr<DepthTarget>& depth) const;
    void clearBindings();
    // Original engine mode-zero reset: only its eight PS textures, four vertex
    // streams, index/VS/PS/layout and four color/depth attachment bindings.
    // Other resource slots, samplers and constants survive. A changed engine
    // color-zero binding explicitly resets viewport/scissor; a cache hit does not.
    NativeBindingResetReceipt resetEngineBindings(const std::shared_ptr<RenderTarget>& color,const std::shared_ptr<DepthTarget>& depth,bool resetViewportAndScissor=false);
    void requireBindingReset(const NativeBindingResetReceipt&) const;
    void retireBindingReset(const NativeBindingResetReceipt&);
    void clearEngineTexture(uint32_t stage);
    // Exactly one original PS texture slot. Owned SRV-only backing cannot alias
    // an output; neither the sampler nor any other context state is changed.
    void bindEngineTexture(uint32_t stage,const std::shared_ptr<Texture>& texture);
    void requireEngineTexture(uint32_t stage,const std::shared_ptr<Texture>& texture) const;
    // One native viewport, read back from the actual immediate context. Empty
    // after ClearState; reversed depth needs a separate proven shader transform.
    // No guest conversion, target clipping, draw or engine readiness is implied.
    D3D11_VIEWPORT setViewport(const D3D11_VIEWPORT& viewport);
    std::optional<D3D11_VIEWPORT> viewport() const;
    // One nonnegative, nonempty pixel rectangle. This changes the retained
    // rectangle only; each draw must separately qualify its scissor enable.
    void setScissor(const std::array<uint32_t,4>& rectangle);
    std::optional<std::array<uint32_t,4>> scissor() const;
    // Only clears whose values are exactly representable in original 20e4 are
    // supported; general depth writes/rounding are not yet implemented.
    void clearDepthTarget(const std::shared_ptr<DepthTarget>& target,float depth,uint8_t stencil,
                          bool clearDepth=true,bool clearStencil=true);
    std::vector<uint8_t> readbackDepthTarget(const std::shared_ptr<DepthTarget>& target);
    std::vector<uint8_t> readbackTarget(const std::shared_ptr<RenderTarget>& target);
    void clearTarget(const std::shared_ptr<RenderTarget>& target,const std::array<float,4>& color);
    // This bounded pass has depth and stencil disabled and takes the whole target
    // as viewport. A bridge must prove that state before selecting this service.
    void drawScreen(const std::shared_ptr<RenderTarget>& target,const ScreenDraw& draw);
    NativeScreenInputReceipt bindDirectSpriteInputs(const std::shared_ptr<Texture>& texture,const D3D11_SAMPLER_DESC& sampler);
    void bindSpriteQueryTexture(const std::shared_ptr<RenderTarget>& texture);
    NativeScreenBatchReceipt finishSpriteBatch(bool clearTextures=true);
    void requireScreenBatchRetirement(const NativeScreenBatchReceipt&) const;
    // Same original shader calculations, explicit float blend arithmetic and
    // integer packing. Each draw preserves prior pixels through a temporary
    // compatible-family packed target. Retains selected attachments and viewport.
    void drawOriginalScreen(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,
                            const ScreenDraw& draw,bool depthWrite,uint32_t depthCompare,bool postDepthEnable=true);
    NativeScreenReplacementReceipt completedScreenReplacement(uint64_t before,const CompiledMaterial& vertex,
                                                              const CompiledMaterial& pixel) const;
    NativeScreenReplacementReceipt completedCoronaQueryReplacement(uint64_t before) const;
    NativeScreenReplacementReceipt completedMovieReplacement(uint64_t before) const;
    void requireCompletedModulatedPostFilter(uint64_t before) const;
    NativeScreenReplacementReceipt completedRestoredScreenReplacement(uint64_t before,
        const NativeScreenReplacementReceipt&) const;
    NativeScreenReplacementReceipt completedDistortionScreenReplacement(uint64_t before,
        const NativeScreenReplacementReceipt&) const;
    void requireCompletedDistortion(uint64_t before) const;
    void requireScreenReplacement(const NativeScreenReplacementReceipt&) const;
    void retireScreenReplacement(const NativeScreenReplacementReceipt&);
    void requireScreenInputReplacement(const NativeScreenInputReceipt&) const;
    void retireScreenInputReplacement(const NativeScreenInputReceipt&);
    uint64_t screenDrawCount() const {return screenDraws;}
    void drawMovie(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,const MovieDraw& draw);
    uint64_t movieDrawCount() const {return movieDraws;}
    // Owned original primitive4 unlit input; effective state is explicit.
    // Preserves actual selected color/depth attachments and the native viewport.
    // Rejects unsupported state before submitting any rendering commands.
    void drawIm2D(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,const Im2DDraw& draw);
    // Explicit ordered queue for validated, owned flat or immutable-textured
    // packets with identical effective state. Writable textures draw immediately.
    // No caller bindings change; resource/vertex snapshots survive caller reuse.
    // Clears, copies, readbacks, other draws and presentation flush this queue.
    void queueIm2D(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,const Im2DDraw& draw);
    void flushIm2D();
    uint64_t im2dDrawCount() const {return im2dDraws;}
    uint64_t im2dNativeDrawCount() const {return im2dNativeDraws;}
    uint64_t im2dColorCopyCount() const {return im2dColorCopies;}
    std::unique_ptr<CompiledMaterial> createMaterialArtifact(const MaterialRecord& original);
    // Exact offline edge, AA and edgeAA pairs. These bind/validate real native
    // shader objects, without a parameter upload, texture binding or draw.
    void validateEdgeShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) const;
    void bindEdgeShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel);
    void requireEdgeShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) const;
    void validateShadowDepthShader(const CompiledMaterial& vertex) const;
    void bindShadowDepthShader(const CompiledMaterial& vertex);
    void requireShadowDepthShader(const CompiledMaterial& vertex) const;
    void validateShadowAlphaShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) const;
    void bindShadowAlphaShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel);
    void requireShadowAlphaShaders(const CompiledMaterial& vertex,const CompiledMaterial& pixel) const;
    std::shared_ptr<NativeShadowDepthCommit> commitShadowDepth(const CompiledMaterial& vertex,const ShadowDepthConstants&);
    void requireShadowDepthCommit(const std::shared_ptr<NativeShadowDepthCommit>&) const;
    ShadowDepthConstants readbackShadowDepthConstants(const std::shared_ptr<NativeShadowDepthCommit>&);
    std::shared_ptr<NativeShadowMesh> uploadShadowMesh(std::span<const ShadowMeshVertex>,std::span<const uint16_t>);
    void bindShadowMeshVertices(const std::shared_ptr<NativeShadowMesh>&);
    void bindShadowMeshDeclaration(const std::shared_ptr<NativeShadowMesh>&);
    void bindShadowMeshIndices(const std::shared_ptr<NativeShadowMesh>&);
    void drawShadowMesh(const std::shared_ptr<RenderTarget>&,const std::shared_ptr<DepthTarget>&,
        const std::shared_ptr<NativeShadowMesh>&,const CompiledMaterial&,const std::shared_ptr<NativeShadowDepthCommit>&,const ShadowMeshDraw&);
    std::vector<ShadowMeshVertex> readbackShadowMeshVertices(const std::shared_ptr<NativeShadowMesh>&);
    std::vector<uint16_t> readbackShadowMeshIndices(const std::shared_ptr<NativeShadowMesh>&);
    uint64_t shadowMeshDrawCount() const {return shadowMeshDraws;}
    // Exact original Z-prepass VS/null PS. Static production profile requires
    // Boolean bank zero and zero weights/indices/morph inputs; no draw at commit.
    void validateZPrepassShader(const CompiledMaterial&) const;
    void bindZPrepassShader(const CompiledMaterial&);
    void requireZPrepassShader(const CompiledMaterial&) const;
    std::shared_ptr<NativeZPrepassCommit> commitZPrepass(const CompiledMaterial&,const ZPrepassConstants&,const ZPrepassBooleans&);
    void requireZPrepassCommit(const std::shared_ptr<NativeZPrepassCommit>&) const;
    ZPrepassConstants readbackZPrepassConstants(const std::shared_ptr<NativeZPrepassCommit>&);
    ZPrepassBooleans readbackZPrepassBooleans(const std::shared_ptr<NativeZPrepassCommit>&);
    std::shared_ptr<NativeZPrepassMesh> uploadZPrepassMesh(std::span<const ZPrepassVertex>,std::span<const uint16_t>);
    void bindZPrepassMeshVertices(const std::shared_ptr<NativeZPrepassMesh>&);
    void bindZPrepassMeshDeclaration(const std::shared_ptr<NativeZPrepassMesh>&);
    void bindZPrepassMeshIndices(const std::shared_ptr<NativeZPrepassMesh>&);
    void clearZPrepassAuxiliaryStream();
    void drawZPrepassMesh(const std::shared_ptr<RenderTarget>&,const std::shared_ptr<DepthTarget>&,
        const std::shared_ptr<NativeZPrepassMesh>&,const CompiledMaterial&,const std::shared_ptr<NativeZPrepassCommit>&,const ShadowMeshDraw&);
    std::vector<ZPrepassVertex> readbackZPrepassMeshVertices(const std::shared_ptr<NativeZPrepassMesh>&);
    std::vector<uint16_t> readbackZPrepassMeshIndices(const std::shared_ptr<NativeZPrepassMesh>&);
    void validateMonoShaders(const CompiledMaterial&,const CompiledMaterial&) const;
    void bindMonoShaders(const CompiledMaterial&,const CompiledMaterial&);
    void requireMonoShaders(const CompiledMaterial&,const CompiledMaterial&) const;
    std::shared_ptr<NativeMonoCommit> commitMono(const CompiledMaterial&,const CompiledMaterial&,const MonoConstants&,const MonoBooleans&);
    void requireMonoCommit(const std::shared_ptr<NativeMonoCommit>&,std::optional<bool> skinned=std::nullopt) const;
    MonoConstants readbackMonoConstants(const std::shared_ptr<NativeMonoCommit>&);
    MonoBooleans readbackMonoBooleans(const std::shared_ptr<NativeMonoCommit>&);
    std::shared_ptr<NativeMonoMesh> uploadMonoMesh(std::span<const MonoVertex>,std::span<const uint16_t>,bool skinned=false);
    void bindMonoMeshVertices(const std::shared_ptr<NativeMonoMesh>&);
    void bindMonoMeshDeclaration(const std::shared_ptr<NativeMonoMesh>&);
    void bindMonoMeshIndices(const std::shared_ptr<NativeMonoMesh>&);
    void bindMonoMeshVertices(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<NativeMonoMesh>&);
    void bindMonoMeshDeclaration(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<NativeMonoMesh>&);
    void bindMonoMeshIndices(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<NativeMonoMesh>&);
    std::shared_ptr<NativeMonoReplayConstants> createMonoReplayConstants();
    void updateMonoReplayConstants(const std::shared_ptr<NativeMonoReplayConstants>&,const MonoConstants&);
    void releaseMonoReplayConstants(const std::shared_ptr<NativeMonoReplayConstants>&);
    void recordMonoMesh(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<RenderTarget>&,
        const std::shared_ptr<DepthTarget>&,const std::shared_ptr<NativeMonoMesh>&,const CompiledMaterial&,const CompiledMaterial&,
        const MonoConstants&,const MonoBooleans&,const std::shared_ptr<NativeMonoReplayConstants>&,const MonoMeshDraw&);
    void clearMonoAuxiliaryStream();
    void drawMonoMesh(const std::shared_ptr<RenderTarget>&,const std::shared_ptr<DepthTarget>&,
        const std::shared_ptr<NativeMonoMesh>&,const CompiledMaterial&,const CompiledMaterial&,const std::shared_ptr<NativeMonoCommit>&,const MonoMeshDraw&);
    std::vector<MonoVertex> readbackMonoMeshVertices(const std::shared_ptr<NativeMonoMesh>&);
    std::vector<uint16_t> readbackMonoMeshIndices(const std::shared_ptr<NativeMonoMesh>&);
    // Exact opaque simpsons_rigid pair. These operations qualify shader and
    // constant storage only; texture sampling, recording and draws are separate.
    void validateRigidShaders(const CompiledMaterial&,const CompiledMaterial&) const;
    void bindRigidShaders(const CompiledMaterial&,const CompiledMaterial&);
    void requireRigidShaders(const CompiledMaterial&,const CompiledMaterial&) const;
    std::shared_ptr<NativeRigidCommit> commitRigid(const CompiledMaterial&,const CompiledMaterial&,
        const RigidVertexConstants&,const RigidPixelConstants&);
    void requireRigidCommit(const std::shared_ptr<NativeRigidCommit>&) const;
    RigidVertexConstants readbackRigidVertexConstants(const std::shared_ptr<NativeRigidCommit>&);
    RigidPixelConstants readbackRigidPixelConstants(const std::shared_ptr<NativeRigidCommit>&);
    std::shared_ptr<NativeRigidMesh> uploadRigidMesh(std::span<const RigidVertex>,std::span<const uint16_t>);
    void bindRigidMeshVertices(const std::shared_ptr<NativeRigidMesh>&);
    void bindRigidMeshDeclaration(const std::shared_ptr<NativeRigidMesh>&);
    void bindVfxRigidMeshDeclaration(const std::shared_ptr<NativeRigidMesh>&);
    // Explicit normalmap-only tangent declaration (TEXCOORD0..5, 68-byte stride).
    // Existing 5-element bindings and draws are unchanged; normalmap integration
    // binds this instead of the default declaration.
    void bindRigidNormalMeshDeclaration(const std::shared_ptr<NativeRigidMesh>&);
    void bindChocolateMeshDeclaration(const std::shared_ptr<NativeRigidMesh>&,bool alpha=true);
    void bindRigidMeshIndices(const std::shared_ptr<NativeRigidMesh>&);
    void bindRigidShadowDepth(uint32_t stage,const std::shared_ptr<DepthTarget>&);
    void drawRigidMesh(const std::shared_ptr<RenderTarget>&,const std::shared_ptr<DepthTarget>&,
        const std::shared_ptr<NativeRigidMesh>&,const CompiledMaterial&,const CompiledMaterial&,
        const std::shared_ptr<NativeRigidCommit>&,const RigidMeshDraw&);
    uint64_t rigidMeshDrawCount() const {return rigidMeshDraws;}
    void bindRigidMeshVertices(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<NativeRigidMesh>&);
    void bindRigidMeshDeclaration(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<NativeRigidMesh>&);
    void bindVfxRigidMeshDeclaration(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<NativeRigidMesh>&);
    void bindRigidNormalMeshDeclaration(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<NativeRigidMesh>&);
    void bindChocolateMeshDeclaration(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<NativeRigidMesh>&,bool alpha=true);
    void bindRigidMeshIndices(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<NativeRigidMesh>&);
    void bindRigidShadowDepth(const std::shared_ptr<NativeRecordingPayload>&,uint32_t stage,const std::shared_ptr<DepthTarget>&);
    void validateRigidShadowDepths(const std::array<std::shared_ptr<DepthTarget>,2>&,const std::shared_ptr<DepthTarget>& output) const;
    std::vector<RigidVertex> readbackRigidMeshVertices(const std::shared_ptr<NativeRigidMesh>&);
    std::vector<uint16_t> readbackRigidMeshIndices(const std::shared_ptr<NativeRigidMesh>&);
    std::shared_ptr<NativeRigidReplayConstants> createRigidReplayConstants(const RigidVertexConstants&,const RigidPixelConstants&);
    void updateRigidReplayConstants(const std::shared_ptr<NativeRigidReplayConstants>&,const RigidVertexConstants&,const RigidPixelConstants&);
    void releaseRigidReplayConstants(const std::shared_ptr<NativeRigidReplayConstants>&);
    void recordRigidMesh(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<RenderTarget>&,
        const std::shared_ptr<DepthTarget>&,const std::shared_ptr<NativeRigidMesh>&,
        const CompiledMaterial&,const CompiledMaterial&,const RigidVertexConstants&,const RigidPixelConstants&,
        const std::shared_ptr<NativeRigidReplayConstants>&,const RigidMeshDraw&);
    // Exact opaque skin and dual-textured skin pairs. Bones use the wide
    // vertex bank vc52+3*bone+row; dual skin samples character depth0/base1.
    void validateSkinShaders(const CompiledMaterial&,const CompiledMaterial&) const;
    void bindSkinShaders(const CompiledMaterial&,const CompiledMaterial&);
    void requireSkinShaders(const CompiledMaterial&,const CompiledMaterial&) const;
    void skinShaderObjects(const CompiledMaterial&,const CompiledMaterial&,
        ComPtr<ID3D11VertexShader>&,ComPtr<ID3D11PixelShader>&) const;
    std::shared_ptr<NativeSkinCommit> commitSkin(const CompiledMaterial&,const CompiledMaterial&,
        const SkinVertexConstants&,const SkinPixelConstants&);
    void requireSkinCommit(const std::shared_ptr<NativeSkinCommit>&) const;
    SkinVertexConstants readbackSkinVertexConstants(const std::shared_ptr<NativeSkinCommit>&);
    SkinPixelConstants readbackSkinPixelConstants(const std::shared_ptr<NativeSkinCommit>&);
    std::shared_ptr<NativeSkinMesh> uploadSkinMesh(std::span<const SkinVertex>,std::span<const uint16_t>,uint32_t vertexAddress=0x82007C1C);
    void bindSkinMeshVertices(const std::shared_ptr<NativeSkinMesh>&);
    void bindSkinMeshDeclaration(const std::shared_ptr<NativeSkinMesh>&);
    void bindSkinMeshIndices(const std::shared_ptr<NativeSkinMesh>&);
    std::shared_ptr<NativeSkyMesh> uploadSkyMesh(std::span<const SkyVertex>,std::span<const uint16_t>);
    void bindSkyMeshVertices(const std::shared_ptr<NativeSkyMesh>&);
    void bindSkyMeshDeclaration(const std::shared_ptr<NativeSkyMesh>&);
    void bindSkyMeshIndices(const std::shared_ptr<NativeSkyMesh>&);
    void bindSkyMeshVertices(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<NativeSkyMesh>&);
    void bindSkyMeshDeclaration(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<NativeSkyMesh>&);
    void bindSkyMeshIndices(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<NativeSkyMesh>&);
    std::shared_ptr<NativeSkyReplayConstants> createSkyReplayConstants();
    void updateSkyReplayConstants(const std::shared_ptr<NativeSkyReplayConstants>&,const SkyVertexConstants&,const SkyPixelConstants&);
    void releaseSkyReplayConstants(const std::shared_ptr<NativeSkyReplayConstants>&);
    void recordSkyMesh(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<RenderTarget>&,
        const std::shared_ptr<DepthTarget>&,const std::shared_ptr<NativeSkyMesh>&,
        const CompiledMaterial&,const CompiledMaterial&,const SkyVertexConstants&,const SkyPixelConstants&,
        const std::shared_ptr<NativeSkyReplayConstants>&,const SkyMeshDraw&);
    void drawSkyMesh(const std::shared_ptr<RenderTarget>&,const std::shared_ptr<DepthTarget>&,
        const std::shared_ptr<NativeSkyMesh>&,const CompiledMaterial&,const CompiledMaterial&,
        const std::shared_ptr<NativeSkyCommit>&,const SkyMeshDraw&);
    uint64_t skyMeshDrawCount() const {return skyMeshDraws;}
    std::shared_ptr<NativeSkyCommit> commitSky(const CompiledMaterial&,const CompiledMaterial&,
        const SkyVertexConstants&,const SkyPixelConstants&);
    void requireSkyCommit(const std::shared_ptr<NativeSkyCommit>&) const;
    void drawSkinMesh(const std::shared_ptr<RenderTarget>&,const std::shared_ptr<DepthTarget>&,
        const std::shared_ptr<NativeSkinMesh>&,const CompiledMaterial&,const CompiledMaterial&,
        const std::shared_ptr<NativeSkinCommit>&,const SkinMeshDraw&);
    uint64_t skinMeshDrawCount() const {return skinMeshDraws;}
    void drawParticles(const std::shared_ptr<RenderTarget>&,const std::shared_ptr<DepthTarget>&,const ParticleDraw&);
    uint64_t particleDrawCount() const {return particleDraws;}
    void drawImmediate(const std::shared_ptr<RenderTarget>&,const std::shared_ptr<DepthTarget>&,const ImmediateDraw&);
    uint64_t immediateDrawCount() const {return immediateDraws;}
    void drawBallEffect(const std::shared_ptr<RenderTarget>&,const std::shared_ptr<DepthTarget>&,const BallEffectDraw&);
    void drawDistortion(const std::shared_ptr<RenderTarget>&,const DistortionDraw&);
    uint64_t ballEffectDrawCount() const {return ballEffectDraws;}
    // Standalone offline-qualified post draw. Resolve/copy remains caller-owned.
    // Restores actual native bindings and retains source ownership through submission.
    // submit=false still qualifies the complete request and flushes preceding
    // draws, then preserves the destination without issuing this effect.
    void drawPostFilter(const std::shared_ptr<RenderTarget>& target,const PostFilterDraw& draw,bool submit=true);
    uint64_t postFilterDrawCount() const {return postFilterDraws;}
    void drawCoronaQueries(const std::shared_ptr<RenderTarget>& scene,const std::shared_ptr<DepthTarget>& sceneDepth,
        const std::shared_ptr<DepthTarget>& sampledDepth,const std::shared_ptr<RenderTarget>& backup,
        const std::shared_ptr<RenderTarget>& query,std::span<const CoronaVertex> vertices,uint32_t depthCompare);
    uint64_t coronaQueryDrawCount() const {return coronaQueryDraws;}
    void bindSkinMeshVertices(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<NativeSkinMesh>&);
    void bindSkinMeshDeclaration(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<NativeSkinMesh>&);
    void bindSkinMeshIndices(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<NativeSkinMesh>&);
    std::vector<SkinVertex> readbackSkinMeshVertices(const std::shared_ptr<NativeSkinMesh>&);
    std::vector<uint16_t> readbackSkinMeshIndices(const std::shared_ptr<NativeSkinMesh>&);
    std::shared_ptr<NativeSkinReplayConstants> createSkinReplayConstants(const SkinVertexConstants&,const SkinPixelConstants&);
    void updateSkinReplayConstants(const std::shared_ptr<NativeSkinReplayConstants>&,const SkinVertexConstants&,const SkinPixelConstants&);
    void releaseSkinReplayConstants(const std::shared_ptr<NativeSkinReplayConstants>&);
    void recordSkinMesh(const std::shared_ptr<NativeRecordingPayload>&,const std::shared_ptr<RenderTarget>&,
        const std::shared_ptr<DepthTarget>&,const std::shared_ptr<NativeSkinMesh>&,
        const CompiledMaterial&,const CompiledMaterial&,const SkinVertexConstants&,const SkinPixelConstants&,
        const std::shared_ptr<NativeSkinReplayConstants>&,const SkinMeshDraw&);
    uint64_t monoMeshDrawCount() const {return monoMeshDraws;}
    uint64_t zprepassMeshDrawCount() const {return zprepassMeshDraws;}
    // Immutable native parameter/resource lease. A commit does not draw.
    std::shared_ptr<NativeEdgeCommit> commitEdge(const std::shared_ptr<RenderTarget>& source,
        const EdgeConstants& constants,const D3D11_SAMPLER_DESC& sampler,bool antiAlias=false);
    std::shared_ptr<NativeEdgeCommit> commitEdgeAA(const EdgeAAInputs& inputs,
        const EdgeAAConstants& constants,const std::array<D3D11_SAMPLER_DESC,5>& samplers);
    void requireEdgeCommit(const std::shared_ptr<NativeEdgeCommit>& commit) const;
    void bindEdgeDeclaration();
    void drawEdge(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,
        const std::shared_ptr<NativeEdgeCommit>& commit,const std::array<ScreenVertex,3>& vertices,
        const CompiledMaterial& vertex,const CompiledMaterial& pixel);
    uint64_t edgeDrawCount() const {return edgeDraws;}
    uint64_t aaDrawCount() const {return aaDraws;}
    uint64_t edgeAADrawCount() const {return edgeAADraws;}
    void clear(const std::array<float,4>& color);
    bool present(); // False means occluded; it does not count as a shown frame.
    uint64_t presentationCount() const {return presentations;}
    D3D_FEATURE_LEVEL level() const {return featureLevel;}
private:
    friend struct NativeWritableTextureProbe;
    friend struct EngineBindingResetProbe;
    friend struct NativePresentationProbe;
    friend struct NativeRecordingProbe;
    friend struct NativeScreenProbe;
    friend struct NativeIm2DProbe;
    friend struct NativeRenderResolutionProbe;
    friend struct NativeAntialiasingProbe;
    friend struct NativeUltrawideProbe;
    friend struct NativeMovieProbe;
    friend struct NativeBufferUploadProbe;
    void requireOwner() const;
    void validateBufferWrite(const std::shared_ptr<Buffer>& buffer,uint32_t offset,std::span<const uint8_t> bytes) const;
    void uploadBuffer(ID3D11Buffer* buffer,uint32_t offset,std::span<const uint8_t> bytes);
    void flushIm2DBufferWrites();
    void drawIm2DImpl(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,
                     const Im2DDraw& draw,bool allowBatch);
    void drawScreenImpl(const std::shared_ptr<RenderTarget>& target,const ScreenDraw& draw,
                        const std::shared_ptr<DepthTarget>& depth,bool original,bool depthWrite,uint32_t depthCompare,bool postDepthEnable);
    void invalidateScreenReplacement();
    void requireMovieReplacement(const NativeScreenReplacementReceipt&) const;
    ID3D11DeviceContext* checkedRecordingContext(const std::shared_ptr<NativeRecordingContext>& recording) const;
    void validateRecordingPayload(const std::shared_ptr<NativeRecordingPayload>&) const;
    void validateRecordingPayloadItem(const std::shared_ptr<NativeRecordingPayload>&) const;
    void rigidShaderObjects(const CompiledMaterial&,const CompiledMaterial&,ComPtr<ID3D11VertexShader>&,ComPtr<ID3D11PixelShader>&) const;
    void monoShaderObjects(const CompiledMaterial&,const CompiledMaterial&,ComPtr<ID3D11VertexShader>&,ComPtr<ID3D11PixelShader>&) const;
    ID3D11DeviceContext* checkedRecordingPayloadContext(const std::shared_ptr<NativeRecordingPayload>&) const;
    void recordRecordingDraw(const std::shared_ptr<NativeRecordingPayload>&,std::span<const uint8_t>,
        const std::shared_ptr<void>&,NativeRecordingPrepare,const std::function<void(ID3D11DeviceContext*)>&);
    void retireRecordingPayloads();
    bool retireRecordingPayload(const std::shared_ptr<NativeRecordingPayload>&);
    // In-flight payloads in the submission order of their CURRENT completion events (a
    // re-executed payload moves to the back), so retirement can stop at the first
    // incomplete event: events on the one immediate context signal in submission order.
    std::vector<std::shared_ptr<NativeRecordingPayload>> pendingRecordingPayloads;
    // Completed execution events of this device, reused instead of creating one per
    // execution. Each was verified to belong to `device` when it was created.
    std::vector<ComPtr<ID3D11Query>> recordingEventPool;
    uint64_t recordingDraws{},recordingExecutedDraws{};
    // Draw-only preflight; target matching/reset queries may retain unrelated UAVs.
    void requireNoOutputUavs() const;
    void validateFrontTarget(const std::shared_ptr<RenderTarget>& target) const;
    void validateDepthCopyTarget(const std::shared_ptr<DepthTarget>& target) const;
    // The descriptor of a color attachment's texture, or nullptr when it belongs to another
    // device; repeated queries of the same texture object come from RenderTarget::DescriptorProof.
    const D3D11_TEXTURE2D_DESC* attachmentDescriptor(const RenderTarget& target) const;
    const D3D11_TEXTURE2D_DESC* provenDescriptor(ID3D11Texture2D* texture,TextureDescriptorProof& proof) const;
    void validateEdgeAAInputs(const EdgeAAInputs& inputs) const;
    std::shared_ptr<NativeCopySubmission> submitCopy(const std::shared_ptr<void>& sourceOwner,
        const std::shared_ptr<void>& destinationOwner,ID3D11Texture2D* source,ID3D11Texture2D* destination);
    std::shared_ptr<NativeCopySubmission> submitFrontPresentation(const std::shared_ptr<RenderTarget>& front);
    bool pollCopy(const std::shared_ptr<NativeCopySubmission>& submission);
    void retireCopies();
    // Scene immediate-draw state reuse. All validation precedes lookup; misses
    // create one immutable object and publish it. Same-device only; a device
    // change clears every entry. Owner thread only. Removes per-draw driver
    // allocations from the gameplay path for steadier frame times.
    ComPtr<ID3D11DepthStencilState> sceneDepthState(uint32_t enable,uint32_t write,uint32_t compare);
    ComPtr<ID3D11RasterizerState> sceneRasterState(uint32_t cull,uint32_t scissorEnable);
    ComPtr<ID3D11BlendState> sceneBlendState(uint32_t enable,uint32_t word,uint32_t mask);
    ComPtr<ID3D11SamplerState> sceneSamplerState(const D3D11_SAMPLER_DESC& desc);
    ComPtr<ID3D11Buffer> sceneDepthConstantBuffer(uint32_t reverse,uint32_t constantBits,uint32_t slopeBits);
    // Backend-owned DYNAMIC constant banks for per-draw material commits. Each
    // changed publish rewrites every byte through Map(WRITE_DISCARD): the driver renames
    // the storage, so already-queued draws keep exactly the bytes they were
    // bound with, without allocating a new immutable buffer per commit. Every
    // An exact byte match keeps the current storage without another Map; its
    // contents already match every new draw. Every publish advances the bank
    // generation; a commit is valid only while it
    // holds its bank's latest generation, so a stale commit still rejects.
    enum ConstantBankId : size_t {ZPrepassBank,MonoBank,ShadowDepthBank,RigidVertexBank,RigidPixelBank,
        SkinVertexBank,SkinPixelBank,SkyVertexBank,SkyPixelBank,ConstantBankCount};
    struct ConstantBank {ComPtr<ID3D11Buffer> buffer;ComPtr<ID3D11Device> device;UINT bytes{};uint64_t generation{};
        mutable BufferDescriptorProof proof;std::vector<uint8_t> contents;bool contentsValid=false;};
    std::array<ConstantBank,ConstantBankCount> constantBanks;
    // Fully validated caller bytes only; returns the new generation.
    uint64_t publishConstants(ConstantBankId bank,const void* bytes,UINT size,ComPtr<ID3D11Buffer>& published);
    void requireConstantBank(ConstantBankId bank,ID3D11Buffer* buffer,uint64_t generation,UINT size,const char* what) const;
    // Immutable EDGE/AA constants: bounded exact-byte reuse on this device.
    // Commit generations still reject stale owners even when their bytes match.
    struct EdgeConstantEntry {UINT bytes{};std::array<uint8_t,sizeof(EdgeAAConstants)> contents{};ComPtr<ID3D11Buffer> buffer;};
    ComPtr<ID3D11Device> edgeConstantDevice;
    std::array<EdgeConstantEntry,8> edgeConstants;
    size_t edgeConstantNext{};
    uint64_t edgeCommitGeneration{};
    ComPtr<ID3D11Buffer> edgeConstantBuffer(const void* bytes,UINT size);
    // Shared immutable all-zero Boolean bank (static Z-prepass/mono profiles).
    ComPtr<ID3D11Buffer> zeroBooleanBank;
    ComPtr<ID3D11Device> zeroBooleanDevice;
    ComPtr<ID3D11Buffer> zeroBooleans();
    ComPtr<ID3D11Buffer> monoSkinBooleanBank;
    ComPtr<ID3D11Device> monoSkinBooleanDevice;
    // Descriptor proofs of the two shared Boolean banks, checked on every Z-prepass/mono draw.
    mutable BufferDescriptorProof zeroBooleanProof,monoSkinBooleanProof;
    // The descriptor of a buffer object, or nullptr when it belongs to another device.
    const D3D11_BUFFER_DESC* provenBufferDescriptor(ID3D11Buffer* buffer,BufferDescriptorProof& proof) const;
    std::optional<D3D11_BUFFER_DESC> booleanBankDescriptor(ID3D11Buffer* buffer) const;
    DWORD owner{};
    bool vsyncEnabled=true;
    uint32_t internalWidth=1280,internalHeight=720,textureAnisotropy=1;
    uint32_t renderBaseWidth=1280,renderBaseHeight=720;
    struct ViewportReceipt {
        D3D11_VIEWPORT physical{},logical{};std::array<float,2> scale{};bool valid=false;
    } viewportReceipt;
    struct ScissorReceipt {
        D3D11_RECT physical{};std::array<uint32_t,4> logical{};std::array<float,2> scale{};bool valid=false;
    } scissorReceipt;
    Antialiasing antialiasingMode=Antialiasing::Original;
    std::shared_ptr<AntialiasingPipeline> antialiasingPipeline;
    std::shared_ptr<RenderTarget> antialiasingOutput,lastPresentedFront;
    std::shared_ptr<RenderTarget> presentationCanvas;
    bool renderingAllocated=false;
    std::array<uint32_t,2> renderExtent(uint32_t width,uint32_t height,TargetScale) const;
    void tagRenderExtent(ID3D11Texture2D*,uint32_t logicalWidth,uint32_t logicalHeight) const;
    std::array<float,2> selectedRenderScale() const;
    uint32_t presentationWidth=0,presentationHeight=0;
    uint32_t presentationSourceWidth=0,presentationSourceHeight=0;
    uint32_t presentationLogicalWidth=0,presentationLogicalHeight=0;
    std::array<uint32_t,2> presentationExtent(uint32_t sourceWidth,uint32_t sourceHeight,uint32_t clientWidth,uint32_t clientHeight,
        uint32_t logicalWidth,uint32_t logicalHeight) const;
    bool tearingEnabled=false;
    uint64_t presentations=0;
    uint64_t screenDraws=0;
    uint64_t screenShaderEpoch=1,completedOriginalScreenDraw=0,completedOriginalCoronaQueryDraw=0,completedOriginalMovieDraw=0;
    uint64_t screenInputBinds=0,completedScreenInputBind=0;
    uint64_t screenBatchRetirements=0,completedScreenBatchRetirement=0;
    uint64_t bindingResetSerial=0,completedBindingReset=0;
    D3D_FEATURE_LEVEL featureLevel{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    // Immutable submission-identity proof: last validated immediate context +
    // device pair. Strong COM refs pin the pair so a released pointer value
    // cannot be recycled (ABA) while cached. Bounded to one pair per backend;
    // use is gated by requireOwner, so only the backend owner thread hits it.
    // Caches identity only (type/device association), never mutable bindings.
    // Any raw-pointer change forces the full GetType/GetDevice checks again;
    // the proof is published only after those checks succeed.
    mutable ComPtr<ID3D11DeviceContext> validatedSubmissionContext;
    mutable ComPtr<ID3D11Device> validatedSubmissionDevice;
    std::shared_ptr<DeviceAvailability> availability;
    ComPtr<ID3D11Buffer> uploadRing;
    uint32_t uploadCursor{};
    ComPtr<ID3D11Buffer> pendingIm2DBuffer;
    std::vector<uint8_t> pendingIm2DBufferBytes;
    uint32_t pendingIm2DBufferOffset{};
    uint64_t bufferUploads{};
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID3D11RenderTargetView> backbuffer;
    std::shared_ptr<ScreenPipeline> screenPipeline;
    // One private integer screen output per device/complete physical descriptor.
    // The whole target is copied in before every draw; contents never identify
    // an engine target. Commands on the owner immediate context serialize reuse,
    // and D3D11 retains old storage through queued use when an extent changes.
    // Holds no caller texture/target owners and exposes no bindable scratch view.
    struct ScreenScratchTexture {
        ComPtr<ID3D11Device> device;
        D3D11_TEXTURE2D_DESC desc{};
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> view;
    } screenScratch;
    // Retain even abandoned caller tokens and post-submission failure resources
    // until a real query completes, waitIdle succeeds, or the device is torn down.
    std::vector<std::shared_ptr<NativeCopySubmission>> pendingCopies;
    std::shared_ptr<Im2DPipeline> im2dPipeline;
    std::shared_ptr<Im2DBatch> pendingIm2D;
    std::shared_ptr<Im2DBatch> spareIm2D;
    uint64_t im2dDraws=0;
    uint64_t im2dNativeDraws=0;
    uint64_t im2dColorCopies=0;
    std::shared_ptr<MoviePipeline> moviePipeline;
    uint64_t movieDraws=0;
    std::shared_ptr<EdgePipeline> edgePipeline;
    uint64_t edgeDraws=0;
    uint64_t aaDraws=0;
    uint64_t edgeAADraws=0;
    uint64_t shadowMeshDraws=0;
    uint64_t monoMeshDraws=0;
    uint64_t zprepassMeshDraws=0;
    // Backend/device-owned exact-content immutable upload caches. Each backend
    // isolates its entries (no static process-global, no cross-device hits);
    // eviction drops only the cache's strong reference. Forward-declared impls
    // live in zprepass_mesh.cpp / mono_mesh.cpp / skin_mesh.cpp /
    // rigid_mesh.cpp / shadow_mesh.cpp / sky_mesh.cpp; shared_ptr tolerates the
    // incomplete type at teardown.
    std::shared_ptr<ZPrepassMeshUploadCache> zprepassMeshCache_;
    std::shared_ptr<MonoMeshUploadCache> monoMeshCache_;
    std::shared_ptr<SkinMeshUploadCache> skinMeshCache_;
    std::shared_ptr<RigidMeshUploadCache> rigidMeshCache_;
    std::shared_ptr<ShadowMeshUploadCache> shadowMeshCache_;
    std::shared_ptr<SkyMeshUploadCache> skyMeshCache_;
    uint64_t rigidMeshDraws=0;
    uint64_t skinMeshDraws=0;
    uint64_t skyMeshDraws=0;
    std::shared_ptr<ParticlePipeline> particlePipeline;
    uint64_t particleDraws=0;
    std::shared_ptr<ImmediatePipeline> immediatePipeline;
    uint64_t immediateDraws=0;
    std::shared_ptr<BallEffectPipeline> ballEffectPipeline;
    uint64_t ballEffectDraws=0;
    std::shared_ptr<PostFilterPipeline> postFilterPipeline;
    uint64_t postFilterDraws=0,completedOriginalModulatedPostFilterDraw=0,completedOriginalDistortionDraw=0;
    uint64_t coronaQueryDraws=0;
    // Device-isolated reuse for scene immediate draws. Depths cover
    // enable/write/compare (32); rasters cover cull0/2/6 x scissor0/1 (6).
    // Blends/samplers/depth-constants use bounded LRU; misses create once.
    // Cleared when the D3D device identity changes (tests replace devices).
    ComPtr<ID3D11Device> sceneCacheDevice;
    std::array<ComPtr<ID3D11DepthStencilState>,32> sceneDepthStates;
    std::array<ComPtr<ID3D11RasterizerState>,6> sceneRasterStates;
    struct SceneBlendEntry { bool used=false; uint32_t enable=0,word=0,mask=0; ComPtr<ID3D11BlendState> state; };
    std::array<SceneBlendEntry,32> sceneBlends;
    size_t sceneBlendNext=0;
    struct SceneSamplerEntry { bool used=false; D3D11_SAMPLER_DESC desc{}; ComPtr<ID3D11SamplerState> state; };
    std::array<SceneSamplerEntry,16> sceneSamplers;
    size_t sceneSamplerNext=0;
    struct SceneDepthConstantEntry { bool used=false; uint32_t reverse=0,constantBits=0,slopeBits=0; ComPtr<ID3D11Buffer> buffer; };
    std::array<SceneDepthConstantEntry,8> sceneDepthConstants;
    size_t sceneDepthConstantNext=0;
    void resetSceneStateCacheForDevice();
};
}
