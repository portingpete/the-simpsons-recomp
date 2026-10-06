#include "engine_driver.h"
#include "runtime.h"
#include "native_window.h"
#include "native_controllers.h"
#include "engine_resources.h"
#include "engine_cpu_calls.h"
#include "engine_im2d_program.h"
#include "frame_timing.h"
#include "stall_profiler.h"
#include "engine_state_bridge.h"
#include "engine_dynamic_buffers.h"
#include "engine_materials.h"
#include "engine_pipeline_resources.h"
#include "engine_rasters.h"
#include "engine_loading_textures.h"
#include "engine_recording.h"
#include "engine_effects.h"
#include "engine_shadow_textures.h"
#include "engine_reflection_textures.h"
#include "engine_builtin_textures.h"
#include "engine_quad_declarations.h"
#include "engine_itxd_textures.h"
#include "engine_viewport_surfaces.h"
#include "engine_scene_copies.h"
#include "world_telemetry.h"
#include "scene_depth_telemetry.h"
#include "engine_particles.h"
#include "screen_effect_sites.h"
#include "skin_profile.h"
#include "rigid_profile.h"
#include "renderer/driver_resources.h"
#include "callsite_counts.h"
#include "renderer/declaration_resources.h"
#include "renderer/im2d_draw.h"
#include "renderer/native_input_prompts.h"
#include "renderer/immediate_draw.h"
#include "renderer/ball_effect.h"
#include "renderer/movie_state.h"
#include "renderer/movie_geometry.h"
#include "renderer/post_filter.h"
#include "renderer/native_material_compiler.h"
#include <bit>
#include <cmath>
#include <fstream>
#include <atomic>
#include <cstdio>
#include <unordered_set>
#include <algorithm>
#include <condition_variable>
#include <chrono>
// Keep this last: shared header bodies retain their normal definitions.
#include "aot_inline_memory.h"

namespace {
constexpr std::array<uint32_t,6> targetFields={0x82D0CB00,0x82D0CAFC,0x82D0CF84,0x82D0CF90,0x82D0CF8C,0x82D0CF88};
// Streamed-texture data residency watch (TEXBIND UNRESOLVED frontier): the
// data pointer is sampled at bind, then re-read at each of the next few
// presents to see whether streaming fills it in.
uint32_t streamWatchData=0;int streamWatchPrints=0;
std::atomic<uint32_t> nextTarget{0x00F00001};
std::atomic<uint32_t> nextContext{0x00900001};
std::atomic<uint32_t> nextPresentReceipt{0x00700001};
uint32_t presentIdentity(Simpsons::Runtime& runtime) {
    uint32_t value=nextPresentReceipt.load();
    while(value<0x00800000) if(nextPresentReceipt.compare_exchange_weak(value,value+1)) {
        if(runtime.pageAccess[value>>12].load()) throw Simpsons::Failure("Native presentation ID overlaps guest memory");
        return value;
    }
    throw Simpsons::Failure("Native presentation receipt identity space exhausted");
}
uint32_t contextIdentity(Simpsons::Runtime& runtime) {
    uint32_t value=nextContext.load();
    while(value<0x00A00000) if(nextContext.compare_exchange_weak(value,value+1)) {
        if(runtime.pageAccess[value>>12].load()) throw Simpsons::Failure("Native context ID overlaps guest memory");
        return value;
    }
    throw Simpsons::Failure("Native context identity space exhausted");
}
uint32_t targetIdentity(Simpsons::Runtime& runtime) {
    uint32_t value=nextTarget.load();
    while(value<0x01000000) if(nextTarget.compare_exchange_weak(value,value+1)) {
        if(runtime.pageAccess[value>>12].load()) throw Simpsons::Failure("Native target ID overlaps guest memory");
        return value;
    }
    throw Simpsons::Failure("Native target identity space exhausted");
}
// Routine successful diagnostic sampling: first few + periodic. Callers must
// keep all semantic work and checked guest reads outside the logging
// condition; only the fprintf itself is gated. Failure/rejection paths remain
// unconditional.
inline bool sampleHotLog(uint32_t& counter) noexcept {
    const uint32_t n=counter++;
    return n<4 || (n%512)==0;
}
void observeRegistry(uint8_t* base) {
    constexpr uint32_t registry=0x82CD1930;
    const uint32_t engine=PPC_LOAD_U32(0x82D0CA68);
    fprintf(stderr,"[ENGINE START] engine=%08X allocation_size=%X lifecycle=%u mode=%u %ux%u format=%08X flags=%08X\n",
        engine,PPC_LOAD_U32(registry),PPC_LOAD_U32(engine+0x144),PPC_LOAD_U32(0x82D0CAF0),
        PPC_LOAD_U32(0x82E3DF84),PPC_LOAD_U32(0x82E3DF88),PPC_LOAD_U32(0x82E3DF90),PPC_LOAD_U32(0x82E3DF98));
    uint32_t entry=PPC_LOAD_U32(registry+0x10);
    std::unordered_set<uint32_t> visited;
    while(entry && visited.size()<256 && visited.insert(entry).second) {
        fprintf(stderr,"[ENGINE PLUGIN] record=%08X offset=%X size=%X id=%08X ctor=%08X dtor=%08X\n",
            entry,PPC_LOAD_U32(entry),PPC_LOAD_U32(entry+4),PPC_LOAD_U32(entry+8),PPC_LOAD_U32(entry+0x20),PPC_LOAD_U32(entry+0x24));
        entry=PPC_LOAD_U32(entry+0x30);
    }
    if(entry) throw Simpsons::Failure("Original engine plugin registry is cyclic or exceeds the bounded walk");
    fprintf(stderr,"[ENGINE START] plugin_count=%zu complete_registry=true\n",visited.size());
}
}

namespace Simpsons {
struct EngineDriver::State {
    Runtime& runtime;
    const uint32_t thread,engine,width,height;
    FrameTiming timing;
    Graphics::NativeBackend backend;
    // Original storage requests execute on their own native worker. The
    // renderer services this one-shot request at a real present boundary.
    std::atomic<bool> menuFrameRequested=false;
    std::mutex menuFrameMutex;
    std::condition_variable menuFrameReady;
    bool menuFrameWaiting=false,menuFrameCompleted=false;
    uint32_t menuFrameWidth{},menuFrameHeight{};
    std::vector<uint8_t> menuFramePixels;
    std::exception_ptr menuFrameError;
    Graphics::StartupResources resources;
    std::unique_ptr<EngineScratchResources> scratch;
    std::unique_ptr<EngineRenderState> renderState;
    std::unique_ptr<EngineMaterials> materials;
    std::unique_ptr<EngineEffects> effects;
    std::unique_ptr<EngineShadowTextures> shadowTextures;
    std::unique_ptr<EngineReflectionTextures> reflectionTextures;
    std::unique_ptr<EngineBuiltinTextures> builtinTextures;
    std::unique_ptr<EngineQuadDeclarations> quadDeclarations;
    std::unique_ptr<EngineITXDTextures> itxdTextures;
    std::unique_ptr<EngineViewportSurfaces> viewportSurfaces;
    std::unique_ptr<EngineSceneCopies> sceneCopies;
    std::unique_ptr<EngineParticles> particles;
    std::unique_ptr<EnginePipelineResources> pipeline;
    std::unique_ptr<EngineRecordingOwners> recording;
    EngineDynamicBuffers dynamic;
    std::unique_ptr<EngineRasters> rasters;
    Graphics::DeclarationRegistry screenDeclarations;
    std::array<Graphics::DeclarationId,2> screenDeclarationRecords{};
    std::array<uint32_t,2> screenDeclarationIds{};
    Graphics::MaterialRegistry screenMaterials;
    struct ScreenMaterial {uint32_t handle{},code{};Graphics::MaterialId record{};};
    std::array<ScreenMaterial,25> screenMaterialBindings{};
    struct BallEffect {
        PPCContext* cpu{};
        uint32_t stack{},owner{},camera{},staging{},matrices{};
        bool inputs{},locked{};
        Graphics::BallEffectDraw draw;
    } ballEffect;
    uint64_t ballEffectDraws{};
    struct Distortion {
        PPCContext* cpu{};
        uint32_t stack{},camera{},mainId{},targetId{},tempId{},stackCount{},helper{},helperStep{};
        uint32_t draws{},copies{},vertex{},pixel{},quad{},constants{};
        uint64_t postFilterBefore{};
        std::array<uint32_t,2> textureHeaders{};
        bool locked{},declaration{};
        std::shared_ptr<Graphics::RenderTarget> main,target,temp,lease;
        std::shared_ptr<Graphics::DepthTarget> depth;
        Graphics::DistortionDraw draw;
    } distortion;
    uint32_t distortionStaging{};
    uint64_t distortionPhases{},distortionDraws{};
    // Original 82770AF0: layer tests, normalization and resets stay AOT.
    struct Luma {
        PPCContext* cpu{};
        uint32_t stack{},camera{},step{};
        std::array<bool,3> active{};
        std::shared_ptr<Graphics::RenderTarget> main,input;
        std::shared_ptr<Graphics::DepthTarget> depth;
    } luma;
    // PS c0..5 as this pass last wrote them. An inactive layer rewrites only
    // its color register; its retained gb register is cancelled by zero color.
    std::array<std::array<float,4>,6> lumaConstants{};
    uint32_t lumaStaging{};
    uint64_t lumaDraws{};
    // Original Dof/Blur/Bloom/Fog/Sat passes (runtime/screen_effect_sites.h).
    // Their console-device stores land in a guest shadow block whose declared
    // fetch fields and PS constant lanes are checked before native use.
    struct ScreenEffect {
        PPCContext* cpu{};
        uint32_t pass{},stack{},camera{},nextOrder{},device{UINT32_MAX},pixelSource{},primitive{},vertexSlot{};
        std::array<uint32_t,2> textures{};
        bool vertexShader{},declaration{},locked{};
        std::shared_ptr<Graphics::RenderTarget> main;
        std::shared_ptr<Graphics::DepthTarget> depth;
    } screenEffect;
    uint32_t effectShadow{},effectStaging{};
    std::array<uint64_t,7> screenEffectDraws{};
    std::array<uint64_t,7> screenEffectSuppressed{};
    // Native sampler for a screen-effect stage from the effective state: the
    // original clamp fields must be applied; filters follow the SDK setters.
    D3D11_SAMPLER_DESC screenEffectSampler(const Graphics::EngineState& ef,uint32_t stage) const {
        using T=Graphics::SamplerState;
        if(ef.sampler(stage,T::AddressU)!=2||ef.sampler(stage,T::AddressV)!=2||ef.sampler(stage,T::AddressW)>2||
           ef.sampler(stage,T::Magnification)>1||ef.sampler(stage,T::Minification)!=ef.sampler(stage,T::Magnification)||
           ef.sampler(stage,T::MipFilter)>2||ef.sampler(stage,T::MaximumAnisotropy)!=1||ef.sampler(stage,T::LodBiasBits))
            throw Failure("Screen effect: inherited sampler differs");
        D3D11_SAMPLER_DESC sm{};const bool linear=ef.sampler(stage,T::Magnification)!=0,mip=ef.sampler(stage,T::MipFilter)==2;
        sm.Filter=linear?(mip?D3D11_FILTER_MIN_MAG_MIP_LINEAR:D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT):(mip?D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR:D3D11_FILTER_MIN_MAG_MIP_POINT);
        sm.AddressU=sm.AddressV=D3D11_TEXTURE_ADDRESS_CLAMP;sm.AddressW=D3D11_TEXTURE_ADDRESS_MODE(ef.sampler(stage,T::AddressW)+1);
        sm.MaxAnisotropy=1;sm.ComparisonFunc=D3D11_COMPARISON_NEVER;sm.MinLOD=float(ef.sampler(stage,T::MinimumMip));
        sm.MaxLOD=ef.sampler(stage,T::MipFilter)?float(ef.sampler(stage,T::MaximumMip)):0;return sm;
    }
    // Raster/blend state of an original screen-effect draw. CB80's packed
    // equation applies independently of the scalar blend shadow.
    void screenEffectDrawState(Graphics::PostFilterDraw& d,const Graphics::EngineState& ef) const {
        using S=Graphics::ScalarState;
        d.blendEnable=1;d.blendWord=ef.effectiveBlend(0);d.expandedBlend=ef.scalar(S::ExpandedBlend0);
        d.colorMask=ef.scalar(S::ColorMask0);d.depthEnable=ef.scalar(S::DepthEnable);d.depthWrite=ef.scalar(S::DepthWrite);d.depthCompare=ef.scalar(S::DepthCompare);
        d.stencilEnable=ef.scalar(S::StencilEnable);d.alphaTest=ef.scalar(S::AlphaTest);d.cull=ef.scalar(S::Cull);d.fill=ef.scalar(S::Fill);
        d.scissorEnable=ef.scalar(S::ScissorEnable);d.halfPixelOffset=ef.scalar(S::HalfPixelOffset);d.viewportEnable=ef.scalar(S::ViewportEnable);
        if(d.scissorEnable){const auto scissor=backend.scissor();if(!scissor)throw Failure("Screen effect: enabled scissor has no retained rectangle");d.scissor=*scissor;}
        d.clipPlaneEnable=ef.scalar(S::ClipPlaneEnable);d.multisampleAntialias=ef.scalar(S::MultisampleAntialias);d.multisampleMask=ef.scalar(S::MultisampleMask);
        d.alphaToMask=ef.scalar(S::AlphaToMask);d.depthBiasBits=ef.scalar(S::DepthBias);d.slopeBiasBits=ef.scalar(S::SlopeBias);
    }
    // A console-device block runs between endpoints: verify its declared
    // writes landed in the shadow, then publish clamp fields as sampler state.
    void settleScreenEffectDevice(uint8_t* base) {
        auto& fx=screenEffect;if(fx.device==UINT32_MAX)return;
        const auto& block=ScreenEffects::sites[fx.device];fx.device=UINT32_MAX;
        auto next=renderState->effective();bool clamps=false;
        for(uint32_t bit=0;bit<32;++bit)if(block.a>>bit&1) {
            const uint32_t stage=bit/2,value=(PPC_LOAD_U32(effectShadow+0x480+0x18*stage)>>(bit&1?13:10))&7;
            if(value!=2)throw Failure("Screen effect: console-device fetch block did not write its declared clamp field");
            next.setSampler(stage,bit&1?Graphics::SamplerState::AddressV:Graphics::SamplerState::AddressU,value);clamps=true;
        }
        const uint64_t lanes=uint64_t(block.b)|uint64_t(block.c)<<32;
        for(uint32_t lane=0;lane<64;++lane)if(lanes>>lane&1&&PPC_LOAD_U32(effectShadow+0x1780+4*lane)==0x7FA5A5A5)
            throw Failure("Screen effect: console-device constant block did not write its declared lane");
        if(clamps)renderState->publishScreenState(next);
    }
    struct PostFilter {
        PPCContext* cpu{};
        uint32_t stack{},camera{},mainId{},targetId{},flags{},mode{},stackCount{},draws{},copies{};
        uint32_t helper{},helperStep{},quad{},inputHeader{},vertex{},pixel{},constants{};
        bool locked{},declaration{},sampler{};
        std::shared_ptr<Graphics::RenderTarget> main, target, lease;
        std::shared_ptr<Graphics::DepthTarget> depth;
        Graphics::PostFilterDraw draw;
    } postFilter;
    uint32_t postStaging{};
    uint32_t immediateDeclarationId{},radialDeclarationId{},radialStaging{};
    struct Radial {uint32_t stack{},owner{},descriptor{},count{};Graphics::ImmediateDraw draw;} radial;
    struct Trail {uint32_t owner{},stack{},entry{},row{},source{},count{},mode{},texture2{},raster2{},projectionOwner{},projectionIdentity{};
        bool matrix{},projectedMatrix{};Graphics::ImmediateDraw draw;} trail;
    struct Billboard {
        PPCContext* cpu{};
        uint32_t owner{},definition{},material{},mode{},texture{},raster{},sampling{},texture2{},raster2{},stack{},entry{},row{},camera{};
        uint32_t source{},count{},total{},draws{};
        uint32_t projectionOwner{},projectionIdentity{};
        bool matrix{},color{},soft{},projectedMatrix{};
        Graphics::ImmediateDraw draw;
    } billboard;
    struct Beam {
        PPCContext* cpu{};
        uint32_t owner{},definition{},item{},points{},pointCount{},texture{},raster{},sampling{};
        uint32_t stack{},entry{},row{},camera{},source{},draws{};
        bool endpoint{},matrix{},color{};
        Graphics::ImmediateDraw draw;
    } beam;
    // Three original mode-zero consumers, each with its own audited frame,
    // state endpoints, CPU fill completion and exact reservation signature.
    struct ImmediateEffect {
        PPCContext* cpu{};
        uint32_t kind{},owner{},definition{},item{},points{},pointCount{},end{},expected{};
        uint32_t stack{},frame{},entry{},row{},camera{},texture{},raster{},sampling{},matrixOwner{};
        uint32_t source{},count{},total{},draws{},record{},segment{};
        bool matrix{},color{};
        std::vector<uint32_t> segments;
        Graphics::ImmediateDraw draw;
    } immediateEffect;
    struct Decal {
        struct Node {uint32_t address{},vtable{},next{},caller{};};
        struct Child {uint32_t node{},stack{},frame{},caller{},source{},count{},triangles{};bool mesh{},matrix{},constant{},projectedMatrix{},finished{};};
        PPCContext* cpu{};
        uint32_t owner{},definition{},mode{},stack{},entry{},row{},camera{},texture{},raster{},sampling{},total{},draws{},index{};
        uint32_t projectionOwner{},projectionIdentity{};
        bool begun{},color{};
        std::vector<Node> nodes;
        std::vector<Child> children;
        Graphics::ImmediateDraw draw;
    } decal;
    std::array<uint32_t,6> targetIds{};
    std::array<uint32_t,2> frontRoles{}; // CF90/CF8C; physical ID-to-backing never rotates.
    std::shared_ptr<Graphics::NativeCopySubmission> submittedCopy;
    // The latest presentation's queued scene->front copy and front->swapchain
    // transfer. Present no longer CPU-waits them, so the GPU finishes a frame
    // while the CPU builds the next one. Their real completion is proven by a
    // bounded event wait before the next presentation consults its history
    // receipts, before a front capture, and before the receipt is reported.
    struct PendingPresentation {
        uint32_t receipt{};
        std::shared_ptr<Graphics::NativeCopySubmission> copy,transfer;
    };
    std::optional<PendingPresentation> pendingPresentation;
    void completePendingPresentation() {
        if(!pendingPresentation) return;
        const auto pending=*pendingPresentation;
        // The transfer was queued after the scene->front copy on the same
        // immediate context; waiting it retires the copy and all earlier work.
        backend.waitCopy(pending.transfer);
        if(!backend.copyComplete(pending.copy)) {
            backend.waitCopy(pending.copy);
            if(!backend.copyComplete(pending.copy)) throw Failure("Native front copy did not actually complete");
        }
        const auto found=runtime.graphicsPresentReceipts.find(pending.receipt);
        if(found==runtime.graphicsPresentReceipts.end() || !found->second.submitted)
            throw Failure("Pending native presentation lost its submitted receipt");
        found->second.copyCompleted=true;found->second.displayTransferred=true;
        pendingPresentation.reset();
    }
    uint64_t frontCopies{},presentAttempts{};
    uint64_t presentedSceneGeometryDraws{};
    uint64_t presentedDepthCopies{};
    bool presenting{};
    bool uiDrawing{};
    uint32_t contextId{};
    std::optional<NativeCameraBinding> camera;
    uint64_t cameraClears{};
    uint64_t cameraCopies{};
    uint32_t sharedColorCopiedCamera{},sharedDepthCopiedCamera{};
    uint64_t capturedDraws{};
    uint64_t capturedMovieDraws{};
    uint64_t im2dDepthDraws{},capturedIm2DDepthDraws{};
    uint64_t im2dTexturedDraws{},capturedIm2DTexturedDraws{};
    uint32_t capturedFrames{};
    uint64_t bindingResets{};
    struct Im2DUpload {
        uint32_t source{},count{},stack{},slot{},offset{},id{},staging{},primitive{};
        bool active{},triangle{};
        std::array<uint32_t,3> indices{};
        uint32_t raster{};
    } im2d;
    struct Im2DInput {
        std::vector<uint8_t> bytes;
        std::shared_ptr<Graphics::Buffer> buffer;
        std::shared_ptr<const Graphics::DeclarationRecord> declaration;
        bool setup{};
        uint32_t addressStream{},addressStride{};
    } im2dInput;
    std::vector<uint8_t> spareIm2DInputBytes;
    std::vector<Graphics::Im2DVertex> spareIm2DVertices;
    std::shared_ptr<const Graphics::DeclarationRecord> im2dDeclaration;
    std::shared_ptr<Graphics::Buffer> im2dStream;
    uint64_t im2dUploads{};
    uint64_t im2dPackets{};
    uint32_t im2dStaging{}; // Driver-owned CPU upload memory, borrowed only while locked.
    struct DirectSprite {
        uint32_t stack{},staging{};bool active{},locked{};
        Graphics::ScreenDraw draw{};Graphics::EngineState post;
        Graphics::NativeScreenInputReceipt inputs;
    } directSprite;
    uint32_t coronaDeclarationId{};
    std::array<uint32_t,12> coronaDeclarationWords{};
    struct CoronaQueries {
        PPCContext* cpu{};
        uint32_t stack{},staging{},count{},owner{};bool active{};
        uint32_t camera{};
        uint64_t drawBefore{};
        std::array<uint32_t,4> effectAssociation{};
        std::array<uint32_t,3> bindings{};
        std::vector<uint32_t> entries;
        Graphics::EngineState post;
    } coronaQueries;
    struct ImmediateBuffer {uint32_t data{};std::array<uint32_t,8> header{};std::shared_ptr<Graphics::Buffer> buffer;};
    std::array<ImmediateBuffer,4> immediateBuffers{};
    std::shared_ptr<Graphics::Buffer> immediateBuffer(uint8_t* base,uint32_t header) const {
        if(header<0x82DFE980||header>0x82DFE9F8||(header-0x82DFE980)%40)return {};
        const auto& record=immediateBuffers[(header-0x82DFE980)/40];
        if(!record.buffer||PPC_LOAD_U32(header-4)!=record.data)throw Failure("Immediate vertex buffer lost its constructor allocation");
        runtime.pointer(record.data,0x100000,false);
        for(unsigned i=0;i<8;++i)if(PPC_LOAD_U32(header+4*i)!=record.header[i])throw Failure("Immediate vertex buffer CPU header changed");
        return record.buffer;
    }
    bool bindingReset{};
    uint8_t resetTextureStages{};
    bool submissionReady{};
    bool ready{},attempted{},targetsPublished{},bindingEntered{},cacheEntered{},rasterEntered{},scratchEntered{};

    State(Runtime& rt,uint32_t w,uint32_t h):runtime(rt),thread(GetCurrentThreadId()),
        engine(PPCLoadU32(rt.base,0x82D0CA68)),width(w),height(h),timing(rt.frameTimingPath,!rt.frameTimingFramesOnly),
        backend(false,rt.vsyncEnabled) {
        requireCaller(rt.base);
        if(!rt.window || rt.window->closed || w!=rt.window->width.load(std::memory_order_relaxed) || h!=rt.window->height.load(std::memory_order_relaxed))
            throw Failure("Native engine driver requires its existing window dimensions");
        backend.configureRendering(rt.videoSettings.renderWidth(),rt.videoSettings.renderHeight(),rt.videoSettings.anisotropy(),
            Graphics::Antialiasing(rt.videoSettings.antialiasing),rt.videoSettings.renderScale);
        const auto sceneExtent=backend.sceneExtent();
        std::fprintf(stderr,"[NATIVE RENDER SETTINGS] internal=%ux%u scene=%ux%u aa=%u anisotropy=%u render_scale=%u; frozen until restart\n",
            rt.videoSettings.renderWidth(),rt.videoSettings.renderHeight(),
            sceneExtent[0],sceneExtent[1],
            rt.videoSettings.antialiasing,rt.videoSettings.anisotropy(),rt.videoSettings.renderScale);
        resources.initialize(backend,w,h);
        scratch=std::make_unique<EngineScratchResources>(resources.resources().scratchIndex);
        renderState=std::make_unique<EngineRenderState>();
        materials=std::make_unique<EngineMaterials>(backend);
        effects=std::make_unique<EngineEffects>(rt,backend);
        pipeline=std::make_unique<EnginePipelineResources>(backend);
        backend.attachWindow(rt.window->handle(),w,h);
        contextId=contextIdentity(rt);
        shadowTextures=std::make_unique<EngineShadowTextures>(rt,backend,contextId);
        reflectionTextures=std::make_unique<EngineReflectionTextures>(rt,backend,contextId);
        builtinTextures=std::make_unique<EngineBuiltinTextures>(rt,backend,contextId);
        quadDeclarations=std::make_unique<EngineQuadDeclarations>(rt,contextId);
        itxdTextures=std::make_unique<EngineITXDTextures>(rt,backend,contextId);
        viewportSurfaces=std::make_unique<EngineViewportSurfaces>(rt,backend,contextId);
        sceneCopies=std::make_unique<EngineSceneCopies>(rt,backend,contextId);
        particles=std::make_unique<EngineParticles>(rt,backend);
        recording=std::make_unique<EngineRecordingOwners>(rt,backend);
    }
    void requireCaller(uint8_t* base) const {
        if(active!=&runtime || base!=runtime.base || thread!=GetCurrentThreadId())
            throw Failure("Native driver accessed outside its runtime/thread owner");
        runtime.checkRunning();
        if(PPC_LOAD_U32(0x82D0CA68)!=engine || !engine)
            throw Failure("Original engine owner changed while native driver is live");
        if(PPC_LOAD_U32(0x82D0CAF8)) throw Failure("Native driver cannot own a console SDK device");
    }
    void initializePresentation(EngineCpuCalls& cpu,uint8_t* base) {
        constexpr uint32_t p=0x82E3DCE0;
        memset(runtime.pointer(p,0x7C,true),0,0x7C);
        PPC_STORE_U32(p,width);PPC_STORE_U32(p+4,height);PPC_STORE_U32(p+8,0x182801B6);
        PPC_STORE_U32(p+0x28,0x1A220197);PPC_STORE_U32(p+0x34,1);PPC_STORE_U32(p+0x38,1);
        PPC_STORE_U32(p+0x3C,1);PPC_STORE_U32(p+0x40,0x28280136);
        const uint32_t video=cpu.registers().r1.u32+0x60;
        cpu.registers().lr=0x823EDF9C;cpu.invoke(0x82CC24D4,video);
        if(PPC_LOAD_U32(video+4)==576 && PPC_LOAD_U32(video+24)==3 && PPC_LOAD_U32(video+12)==0) {
            PPC_STORE_U32(p+0x68,width);PPC_STORE_U32(p+0x6C,height);
            PPC_STORE_U32(p+0x70,PPC_LOAD_U32(video));PPC_STORE_U32(p+0x74,576);
        }
        PPC_STORE_U32(0x82D0CB0C,1);
    }
    void publishTargets(uint8_t* base) {
        // Check the entire publication before changing any original role field.
        for(uint32_t field:targetFields) {
            runtime.pointer(field,4,true);
            if(PPC_LOAD_U32(field)) throw Failure("Original target role already has an owner");
        }
        for(auto& id:targetIds) id=targetIdentity(runtime);
        frontRoles={targetIds[3],targetIds[4]};
        backend.bindTargets({resources.resources().defaultColor,nullptr,nullptr,nullptr},resources.resources().defaultDepth);
        for(size_t i=0;i<targetFields.size();++i) PPC_STORE_U32(targetFields[i],targetIds[i]);
        targetsPublished=true;
    }
    // All six role fields lie on one guest page (static_assert below): one write-permission
    // probe of the span proves what six per-field probes did, and the words are read from it.
    static constexpr uint32_t targetFieldsFirst=*std::min_element(targetFields.begin(),targetFields.end());
    static constexpr uint32_t targetFieldsEnd=*std::max_element(targetFields.begin(),targetFields.end())+4;
    static_assert((targetFieldsFirst>>12)==((targetFieldsEnd-1)>>12),"Target role fields must share one guest page");
    void validateTargets(uint8_t* base) const {
        const auto* fields=runtime.probe(targetFieldsFirst,targetFieldsEnd-targetFieldsFirst,true);
        for(size_t i=0;i<targetFields.size();++i) {
            const uint32_t expected=targetsPublished?((i==3 || i==4)?frontRoles[i-3]:targetIds[i]):0;
            uint32_t value{};std::memcpy(&value,fields+(targetFields[i]-targetFieldsFirst),4);
            if(__builtin_bswap32(value)!=expected)
                throw Failure("Original target role changed outside its native owner");
        }
    }
    NativeCameraBinding validateCamera(uint8_t* base,uint32_t c) const {
        requireCaller(base);
        const bool loading=c&&c==PPC_LOAD_U32(0x82E07248);
        const bool shadow=!loading && shadowTextures && shadowTextures->ownsCamera(c);
        uint32_t slot=4;
        if(!loading&&!shadow&&c&&!(c&3)) {
            const auto manager=PPC_LOAD_U32(0x82D08B10);
            if(manager) {
                runtime.pointer(manager,0x140,false);
                if(PPC_LOAD_U32(manager)!=0x820B6DEC||PPC_LOAD_U32(manager+8)!=0x820B6DE8||PPC_LOAD_U32(manager+0x128)!=4)
                    throw Failure("Original viewport manager ownership changed before camera selection");
                for(uint32_t i=0;i<3;++i)if(PPC_LOAD_U32(manager+0x14+4*i)==c) {
                    if(slot!=4)throw Failure("Viewport camera occurs in more than one original slot");slot=i;
                }
            }
        }
        if(!ready || !submissionReady || !rasters || !c || (c&3) || (!loading&&!shadow&&slot==4)) {
            fprintf(stderr,"[NATIVE CAMERA] unqualified camera=%08X loading=%08X ready=%u submission=%u rasters=%u\n",
                c,PPC_LOAD_U32(0x82E07248),unsigned(ready),unsigned(submissionReady),unsigned(bool(rasters)));
            if(currentContext)dumpGuestStack(*currentContext);
            throw Failure("Native camera selection requires a live original loading, viewport or shadows camera");
        }
        validateTargets(base);
        runtime.pointer(c,0x8C,false);
        const uint32_t colorRaster=PPC_LOAD_U32(c+0x60),depthRaster=PPC_LOAD_U32(c+0x64);
        const auto surfaces=rasters->cameraSurfaces(base,colorRaster,loading?2:5,depthRaster);
        const auto& colorSurface=surfaces[0];const auto& depthSurface=surfaces[1];
        const uint32_t cameraWidth=shadow?1024:(loading||slot==0?1280:640),cameraHeight=shadow?1024:720;
        if(width!=1280 || height!=720 || PPC_LOAD_U32(colorRaster+0xC)!=cameraWidth || PPC_LOAD_U32(colorRaster+0x10)!=cameraHeight)
            throw Failure("Native camera target/viewport extent differs from the verified startup profile");
        uint32_t colorIdentity=targetIds[0],depthIdentity=targetIds[1];
        if(!loading) {
            if(!depthRaster||PPC_LOAD_U32(depthRaster+0xC)!=cameraWidth||PPC_LOAD_U32(depthRaster+0x10)!=cameraHeight)
                throw Failure("Original viewport camera lacks matching private depth storage");
            colorIdentity=colorSurface.identity;depthIdentity=depthSurface.identity;
            if(!colorSurface.color||!depthSurface.depth)
                throw Failure("Original viewport camera does not own its native attachment pair");
        }
        using Graphics::ScalarState;
        if((renderState->effective().scalar(ScalarState::ScissorEnable)!=0 && !shadow) ||
           renderState->effective().scalar(ScalarState::ViewportEnable)!=1)
            throw Failure("Native camera requires the original full-target viewport with scissor disabled");
        // The cached depth word and the four color words are contiguous on one page: one
        // write-permission probe of the span replaces the five per-word probes.
        const auto* bindings=runtime.probe(0x82D0CF58,0x14,true);
        const auto bindingWord=[&](uint32_t address){uint32_t value{};std::memcpy(&value,bindings+(address-0x82D0CF58),4);return __builtin_bswap32(value);};
        for(uint32_t slot=0;slot<4;++slot) {
            const uint32_t cached=bindingWord(0x82D0CF5C+4*slot);
            if(slot?cached!=0:(cached && cached!=(camera?camera->colorIdentity:targetIds[0])))
                throw Failure("Native camera encountered unowned or additional color target bindings");
        }
        const uint32_t cachedDepth=bindingWord(0x82D0CF58);
        if(cachedDepth && cachedDepth!=(camera?camera->depthIdentity:targetIds[1])) throw Failure("Native camera encountered an unowned depth binding");
        // The original type-2 branch selects default depth even when C+64=0.
        return {c,colorRaster,depthRaster,colorIdentity,depthIdentity,{0,0,cameraWidth,cameraHeight,0x3F800000,0}};
    }
    // The shared screen shader handles {object,source,next}: one table for qualification and for the binding reset, so a slot
    // that draws can never be missing from the reset (live neverquest: a projected billboard's slots 22-24 were absent from the
    // reset's own copy, and the first reset after one failed on a cached shader object address).
    static constexpr std::array<uint32_t,25> screenFields={0x82CF2310,0x82CF231C,0x82CF2334,0x82CF2340,0x82CF2328,0x82CF23EC,0x82CF23F8,0x82CF23E0,0x82CF1FBC,0x82CF1F8C,0x82CF23BC,0x82CF23C8,0x82CF23D4,0x82CF234C,0x82CF261C,0x82CF2590,0x82CF2610,0x82CF1F98,0x82CF25B4,0x82CF2584,0x82CF259C,0x82CF25A8,0x82CF1FC8,0x82CF1FA4,0x82CF1FB0};
    static constexpr bool screenVertexSlot(uint32_t slot) {return slot==1||slot==3||slot==5||slot==8||slot==10||slot==13||slot==18||slot==22;}
    const Graphics::MaterialRecord& screenRecord(uint8_t* base,uint32_t slot) {
        if(slot>=screenMaterialBindings.size())throw Failure("Screen shader slot is outside the pinned catalog");
        const auto& fields=screenFields;
        constexpr std::array<uint32_t,25> sources={0x821524C8,0x821525E8,0x82152708,0x82152880,0x82152B68,0x82153278,0x82153460,0x821536F0,0x821511D8,0x821509D8,0x821538E8,0x82153B88,0x82153C80,0x821529C8,0x821583B8,0x82155F28,0x82158118,0x82150B18,0x82156548,0x82155D60,0x82156150,0x82156340,0x821513B8,0x82150C98,0x82150F18};
        const bool vertex=screenVertexSlot(slot);const uint32_t source=sources[slot],field=fields[slot];
        auto& binding=screenMaterialBindings[slot];
        if(!binding.record.generation) {
            const Graphics::MaterialIdentity* identity=nullptr;
            for(const auto& candidate:Graphics::originalMaterialIdentities())if(candidate.originalAddress==source){identity=&candidate;break;}
            if(!identity)throw Failure("Original screen record is absent from the pinned catalog");
            binding.record=screenMaterials.create(source,{runtime.pointer(source,identity->recordBytes,false),identity->recordBytes});
        }
        const auto& original=screenMaterials.record(binding.record);const auto bytes=original.bytes();
        if(std::memcmp(runtime.pointer(source,uint32_t(bytes.size()),false),bytes.data(),bytes.size()))
            throw Failure("Original screen source record changed after native snapshot");
        const auto object=PPC_LOAD_U32(field),headerBytes=original.metadata().headerBytes,codeBytes=original.metadata().payloadBytes;
        const auto offset=vertex?0x368u:0x28u;
        if(!object || (object&15) || PPC_LOAD_U32(field+4)!=source || !PPC_LOAD_U32(0x82DFE358) || !PPC_LOAD_U32(0x82DFE354))
            throw Failure("Original screen shared shader allocation or record identity changed");
        runtime.pointer(object,offset+headerBytes,false);
        const uint32_t code=PPC_LOAD_U32(object+(vertex?0x20:0x18));
        if(PPC_LOAD_U32(object)!=(vertex?6u:7u) || PPC_LOAD_U32(object+4)!=1 || !code || (code&31) ||
           std::memcmp(runtime.pointer(object+offset,headerBytes,false),bytes.data(),headerBytes) ||
           std::memcmp(runtime.pointer(code,codeBytes,false),bytes.data()+headerBytes,codeBytes))
            throw Failure("Original screen shared shader header/code bytes differ from their source record");
        if(binding.handle && (binding.handle!=object || binding.code!=code))
            throw Failure("Original screen shader allocation changed without native retirement");
        if(!binding.handle){binding.handle=object;binding.code=code;}
        return original;
    }
    const Graphics::CompiledMaterial& screenMaterial(uint8_t* base,uint32_t slot) {
        screenRecord(base,slot);
        Graphics::NativeMaterialCompiler compiler(backend);
        return screenMaterials.prepareForBind(screenMaterialBindings[slot].record,compiler);
    }
    // Shared original PS object published at a {object,source,next} handle:
    // luma 82CF24E4 and the screen effects. The mapped image is SHA-pinned at
    // load; later writes to a source record are rejected against its first-use
    // snapshot. Native draws use their own separately qualified artifacts.
    static constexpr std::array<std::pair<uint32_t,uint32_t>,10> originalPixelHandles{{
        {0x82CF24E4,0x821559D8},{0x82CF22D4,0x821517A0},{0x82CF22E4,0x82151C50},{0x82CF22F4,0x82151DB8},
        {0x82CF2408,0x82153E40},{0x82CF2414,0x821540A0},{0x82CF2420,0x82154330},{0x82CF24B4,0x821557C8},
        {0x82CF2310,0x821524C8},{0x82CF2304,0x82152318}}};
    std::unordered_map<uint32_t,std::vector<uint8_t>> pixelRecords;
    uint32_t requireOriginalPixelShader(uint8_t* base,uint32_t object,uint32_t handle) {
        uint32_t source{};
        for(const auto& [field,record]:originalPixelHandles)if(field==handle)source=record;
        if(!source || PPC_LOAD_U32(handle+4)!=source)throw Failure("Original pixel shader handle is not a qualified screen record");
        runtime.pointer(source,12,false);
        const uint32_t headerBytes=PPC_LOAD_U32(source+4),recordBytes=headerBytes+PPC_LOAD_U32(source+8);
        if(PPC_LOAD_U32(source)!=0x102A1100 || headerBytes<0x40 || recordBytes>0x800 || headerBytes>=recordBytes)
            throw Failure("Original pixel shader record envelope changed");
        auto& snapshot=pixelRecords[source];
        if(snapshot.empty()){const auto* bytes=runtime.pointer(source,recordBytes,false);snapshot.assign(bytes,bytes+recordBytes);}
        if(snapshot.size()!=recordBytes || std::memcmp(runtime.pointer(source,recordBytes,false),snapshot.data(),recordBytes))
            throw Failure("Original pixel shader record changed after native snapshot");
        if(!object || (object&15) || object!=PPC_LOAD_U32(handle) || !PPC_LOAD_U32(0x82DFE358) || !PPC_LOAD_U32(0x82DFE354))
            throw Failure("Original shared pixel shader allocation or record identity changed");
        runtime.pointer(object,0x28+headerBytes,false);const uint32_t code=PPC_LOAD_U32(object+0x18);
        if(PPC_LOAD_U32(object)!=7 || PPC_LOAD_U32(object+4)!=1 || !code || (code&31) ||
           std::memcmp(runtime.pointer(object+0x28,headerBytes,false),snapshot.data(),headerBytes) ||
           std::memcmp(runtime.pointer(code,recordBytes-headerBytes,false),snapshot.data()+headerBytes,recordBytes-headerBytes))
            throw Failure("Original shared pixel shader header/code bytes differ from their source record");
        return source;
    }
    std::shared_ptr<Graphics::RenderTarget> cameraColor(const NativeCameraBinding& binding) const {
        auto target=binding.colorIdentity==targetIds[0]?resources.resources().defaultColor:rasters->ownedColor(binding.colorIdentity);
        if(!target)throw Failure("Native selected camera color owner expired");return target;
    }
    std::shared_ptr<Graphics::DepthTarget> cameraDepth(const NativeCameraBinding& binding) const {
        auto target=binding.depthIdentity==targetIds[1]?resources.resources().defaultDepth:rasters->ownedDepth(binding.depthIdentity);
        if(!target)throw Failure("Native selected camera depth owner expired");return target;
    }
    void bindCamera(uint8_t* base,const NativeCameraBinding& binding) {
        // Actual native attachment selection is verified by OMGetRenderTargets.
        // Reversed viewport remains logical state; a future draw must implement
        // its depth mapping. Full-resource clear is independent of host viewport.
        backend.bindTargets({cameraColor(binding),nullptr,nullptr,nullptr},cameraDepth(binding));
        if(PPC_LOAD_U32(0x82D0CF5C)!=binding.colorIdentity) PPC_STORE_U32(0x82D0CF5C,binding.colorIdentity);
        if(PPC_LOAD_U32(0x82D0CF58)!=binding.depthIdentity) PPC_STORE_U32(0x82D0CF58,binding.depthIdentity);
        camera=binding;
    }
    void releaseBindingPool(EngineCpuCalls& cpu,uint8_t* base) {
        if(!bindingEntered) return;
        if(const uint32_t pool=PPC_LOAD_U32(0x82D0CB3C)) {
            PPC_STORE_U32(pool+0x18,PPC_LOAD_U32(pool+0x18)|2);
            const uint32_t releaseEntry=PPC_LOAD_U32(engine+0x13C);
            for(uint32_t i=0;i<260;++i) {
                const uint32_t slot=0x82D0CB40+4*i,entry=PPC_LOAD_U32(slot);
                if(entry) {cpu.registers().lr=0x823EE228;cpu.invoke(releaseEntry,pool,entry);PPC_STORE_U32(slot,0);}
            }
            cpu.registers().lr=0x823EE244;cpu.invoke(0x823FB708,pool);PPC_STORE_U32(0x82D0CB3C,0);
        }
        PPC_STORE_U32(0x82D0CF50,0);
        if(const uint32_t extra=PPC_LOAD_U32(0x82D0CF54)) {
            cpu.registers().lr=0x823EE274;cpu.invoke(PPC_LOAD_U32(engine+0x10C),extra);PPC_STORE_U32(0x82D0CF54,0);
        }
        bindingEntered=false;
    }
    void cleanup(EngineCpuCalls& cpu,uint8_t* base,bool closeModes) {
        if(postFilter.cpu||ballEffect.cpu||distortion.cpu||distortion.locked||distortion.lease||distortion.temp)
            throw Failure("Effect draw scope remains active during driver stop");
        if(im2d.active || im2d.staging)throw Failure("Original Im2D upload remains locked during driver stop");
        if(recording) recording->requireEmpty();
        requireCaller(base);validateTargets(base);
        if(rasters) rasters->requireReleased();
        if(shadowTextures) shadowTextures->requireReleased();
        if(reflectionTextures) reflectionTextures->requireReleased();
        if(builtinTextures) builtinTextures->requireReleased();
        if(quadDeclarations) quadDeclarations->requireReleased();
        if(itxdTextures) itxdTextures->requireReleased();
        if(viewportSurfaces) viewportSurfaces->requireReleased();
        if(sceneCopies) sceneCopies->requireReleased();
        if(screenDeclarations.liveCount())throw Failure("Original screen declarations have not been released");
        if(coronaDeclarationId||coronaQueries.active)throw Failure("Original corona ownership remains active during driver stop");
        if(immediateDeclarationId||trail.owner||billboard.owner||beam.owner||immediateEffect.cpu||decal.cpu||radialDeclarationId||radial.owner)throw Failure("Original immediate ownership remains active during driver stop");
        for(const auto& buffer:immediateBuffers)if(buffer.buffer)throw Failure("Original immediate vertex buffers have not been released");
        // The original caller walks plugin destructors before requesting stop.
        materials->requireReleased();pipeline->requireUnowned();effects->requireReleased();
        backend.waitIdle();submittedCopy.reset(); // Retire real GPU uses before releasing/recycling host backing.
        completePendingPresentation(); // Already idle: records the proven completion without another wait.
        for(uint32_t field:{0x82D0CB28u,0x82D0CB2Cu,0x82D0CB30u,0x82D50320u,0x82D503D0u})
            if(PPC_LOAD_U32(field)) throw Failure("Driver stop reached unported console declaration/pipeline cache ownership");
        if(submissionReady) for(uint32_t alias:{0x82D5DA74u,0x82D6D890u}) {
            const uint32_t value=PPC_LOAD_U32(alias);runtime.pointer(alias,4,true);
            if(value && value!=contextId) throw Failure("Native integration alias belongs to a different owner");
        }
        ready=false;sharedColorCopiedCamera=0;sharedDepthCopiedCamera=0;PPC_STORE_U32(0x82D0CB08,0);
        if(submissionReady) {
            // Normal application shutdown clears these first. Direct driver
            // retirement also invalidates its borrowed native identities.
            PPC_STORE_U32(0x82D5DA74,0);PPC_STORE_U32(0x82D6D890,0);submissionReady=false;
        }
        if(closeModes) if(const uint32_t modes=PPC_LOAD_U32(0x82D0CB18)) {
            cpu.registers().lr=0x823EE1E4;cpu.invoke(PPC_LOAD_U32(engine+0x10C),modes);
            PPC_STORE_U32(0x82D0CB18,0);PPC_STORE_U32(0x82D0CB10,0);
        }
        releaseBindingPool(cpu,base);backend.clearBindings();
        im2dInput={};im2dDeclaration.reset();im2dStream.reset();
        if(im2dStaging){runtime.freePhysical(im2dStaging);im2dStaging=0;}
        if(directSprite.active)throw Failure("Direct sprite cleanup during CPU upload");
        if(directSprite.staging){runtime.freePhysical(directSprite.staging);directSprite.staging=0;}
        if(radialStaging){runtime.freePhysical(radialStaging);radialStaging=0;}
        if(postStaging){runtime.freePhysical(postStaging);postStaging=0;}
        if(ballEffect.staging){runtime.freePhysical(ballEffect.staging);ballEffect.staging=0;}
        if(distortionStaging){runtime.freePhysical(distortionStaging);distortionStaging=0;}
        if(lumaStaging){runtime.freePhysical(lumaStaging);lumaStaging=0;}
        if(effectShadow){runtime.freePhysical(effectShadow);effectShadow=0;}
        if(effectStaging){runtime.freePhysical(effectStaging);effectStaging=0;}
        if(coronaQueries.staging){runtime.freePhysical(coronaQueries.staging);coronaQueries.staging=0;}
        if(cacheEntered) {cpu.registers().lr=0x823EE410;cpu.invoke(0x823F47A8);cacheEntered=false;}
        if(rasterEntered) {cpu.registers().lr=0x823EE414;cpu.invoke(0x823F6A20);rasterEntered=false;}
        if(dynamic.hasOwnership()) dynamic.stop(runtime,cpu,base);
        if(scratchEntered) {
            cpu.registers().lr=0x823EE41C;cpu.invoke(0x82408E30);scratch->requireReleased();scratchEntered=false;
        }
        if(PPC_LOAD_U32(0x82D0D020) || PPC_LOAD_U32(0x82D101D4) || PPC_LOAD_U32(0x82D101D8))
            throw Failure("Original CPU cleanup retained a raster/scratch owner");
        if(targetsPublished) {
            for(uint32_t field:targetFields) PPC_STORE_U32(field,0);
            targetsPublished=false;targetIds.fill(0);
            frontRoles.fill(0);
        }
        recording.reset();rasters.reset();shadowTextures.reset();reflectionTextures.reset();quadDeclarations.reset();pipeline.reset();effects.reset();materials.reset();renderState.reset();scratch.reset();resources.reset();
        fprintf(stderr,"[NATIVE ENGINE] driver cleanup completed; original pools and native role fields released, console device=0\n");
    }
};

EngineDriver::EngineDriver(Runtime& runtime,uint32_t width,uint32_t height):state(std::make_unique<State>(runtime,width,height)) {}
EngineDriver::~EngineDriver() {
    for(auto* staging:{&state->ballEffect.staging,&state->distortionStaging,&state->lumaStaging,&state->effectShadow,&state->effectStaging})if(*staging){
        try{state->runtime.freePhysical(*staging);*staging=0;}
        catch(const std::exception& e){fprintf(stderr,"[NATIVE DISTORTION] terminal staging release failed: %s\n",e.what());}
    }
    if(state->postStaging){
        try{state->runtime.freePhysical(state->postStaging);state->postStaging=0;}
        catch(const std::exception& e){fprintf(stderr,"[NATIVE POST] terminal staging release failed: %s\n",e.what());}
    }
    if(state->radialStaging){
        try{state->runtime.freePhysical(state->radialStaging);state->radialStaging=0;}catch(...){}
    }
    if(state->coronaQueries.staging){
        try{state->runtime.freePhysical(state->coronaQueries.staging);state->coronaQueries.staging=0;}
        catch(const std::exception& e){fprintf(stderr,"[NATIVE CORONA] terminal staging release failed: %s\n",e.what());}
    }
    if(state->directSprite.staging) {
        try {state->runtime.freePhysical(state->directSprite.staging);state->directSprite.staging=0;}
        catch(const std::exception& e){fprintf(stderr,"[NATIVE SPRITE] terminal staging release failed: %s\n",e.what());}
    }
    if(state->im2dStaging) {
        try {state->runtime.freePhysical(state->im2dStaging);state->im2dStaging=0;state->im2d.staging=0;}
        catch(const std::exception& e){fprintf(stderr,"[NATIVE IM2D] terminal staging release failed: %s\n",e.what());}
    }
    if(state->im2d.active)fprintf(stderr,"[NATIVE IM2D] original vertex upload did not finish before terminal cleanup\n");
    if(state->targetsPublished) {
        try {state->backend.waitIdle();state->submittedCopy.reset();state->pendingPresentation.reset();}
        catch(const std::exception& error) {
            fprintf(stderr,"[NATIVE PRESENT] terminal GPU retirement failed: %s; no completed lease is claimed\n",error.what());
        }
    }
    if(state->ready || state->targetsPublished || state->dynamic.hasOwnership())
        fprintf(stderr,"[NATIVE ENGINE] terminal shutdown releases host driver backing; original guest cleanup was not completed\n");
}
bool EngineDriver::started() const {state->requireCaller(state->runtime.base);return state->ready;}
EngineViewportSurfaces& EngineDriver::viewportSurfaces() {state->requireCaller(state->runtime.base);return *state->viewportSurfaces;}
EngineSceneCopies& EngineDriver::sceneCopies() {state->requireCaller(state->runtime.base);return *state->sceneCopies;}
EngineEffects& EngineDriver::effects() {
    state->requireCaller(state->runtime.base);
    if(!state->effects) throw Failure("Native FX owner is unavailable after driver cleanup");
    return *state->effects;
}
void EngineDriver::preflightEffectPoolRetire(uint8_t* base,uint32_t pool) const {
    if(base!=state->runtime.base || active!=&state->runtime)
        throw Failure("Invalid native FX pool retirement runtime");
    if(state->effects) state->effects->requirePoolReleased(pool);
}
uint32_t EngineDriver::refreshContext(const PPCContext& incoming,uint8_t* base) {
    auto& s=*state;s.requireCaller(base);
    if(!s.ready) throw Failure("Native context query requires a started driver");
    // Preserve the complete original pool/cache reset. The helper invalidates
    // CPU binding caches but does not unbind actual render-target attachments.
    EngineCpuCalls cpu(incoming,base);cpu.registers().lr=0x823EE908;cpu.invoke(0x823EDD38);
    if(!PPC_LOAD_U32(0x82D0CB3C)) throw Failure("Original context query lost its binding pool");
    {static thread_local uint32_t bindingCacheSample{};
    if(sampleHotLog(bindingCacheSample))
        fprintf(stderr,"[NATIVE ENGINE] original binding cache refreshed; native context identity=%08X, console device=0\n",s.contextId);}
    return s.contextId;
}
void EngineDriver::requireContext(uint32_t identity) const {
    state->requireCaller(state->runtime.base);
    if(!state->ready || !identity || identity!=state->contextId)
        throw Failure("Unknown, stale or inactive native backend context identity");
}
void EngineDriver::requireStateContext(uint32_t identity) const {
    auto& s=*state;s.requireCaller(s.runtime.base);
    if(s.renderState->recordingContext()) {
        if(identity!=s.renderState->recordingContext())throw Failure("Native state update selected another context during recording");
        s.recording->requireActiveContext(identity);return;
    }
    requireContext(identity);
}
void EngineDriver::beginRecordingState(uint32_t identity) {
    auto& s=*state;s.requireCaller(s.runtime.base);s.recording->requireActiveContext(identity);s.renderState->beginRecording(identity);
}
void EngineDriver::requireRecordingStateSeed(uint32_t identity) const {
    auto& s=*state;s.requireCaller(s.runtime.base);s.recording->requireActiveContext(identity);s.renderState->requireRecordingSeed(identity);
}
void EngineDriver::endRecordingState(uint32_t identity) {
    auto& s=*state;s.requireCaller(s.runtime.base);s.renderState->endRecording(identity);
}
bool EngineDriver::submissionConfigured() const {state->requireCaller(state->runtime.base);return state->ready && state->submissionReady;}
EngineShadowTextures& EngineDriver::shadowTextures() {
    auto& s=*state;s.requireCaller(s.runtime.base);
    if(!s.ready || !s.submissionReady || !s.shadowTextures)
        throw Failure("Native shadow texture ownership requires the live registered driver");
    return *s.shadowTextures;
}
EngineReflectionTextures& EngineDriver::reflectionTextures() {
    auto& s=*state;s.requireCaller(s.runtime.base);
    if(!s.ready || !s.submissionReady || !s.reflectionTextures)
        throw Failure("Native reflection ownership requires the live registered driver");
    return *s.reflectionTextures;
}
EngineBuiltinTextures& EngineDriver::builtinTextures() {
    auto& s=*state;s.requireCaller(s.runtime.base);
    if(!s.ready || !s.submissionReady || !s.builtinTextures)
        throw Failure("Native built-in image ownership requires the live registered driver");
    return *s.builtinTextures;
}
EngineQuadDeclarations& EngineDriver::quadDeclarations() {
    auto& s=*state;s.requireCaller(s.runtime.base);
    if(!s.ready || !s.submissionReady || !s.quadDeclarations)
        throw Failure("Native quad declarations require the live registered driver");
    return *s.quadDeclarations;
}
EngineRecordingOwners& EngineDriver::recordingOwners() {
    auto& s=*state;s.requireCaller(s.runtime.base);
    if(!s.ready || !s.submissionReady || !s.recording)
        throw Failure("Native recording ownership requires the live registered driver");
    return *s.recording;
}
void EngineDriver::createRaster(const PPCContext& incoming,uint8_t* base,uint32_t raster,uint32_t flags) {
    auto& s=*state;s.requireCaller(base);
    if(!s.ready || !s.submissionReady || !s.rasters) throw Failure("Native raster create requires registered driver submission");
    s.rasters->create(incoming,base,raster,flags);
}
void EngineDriver::beginSnapshotRaster(PPCContext& c,uint8_t* b) {auto& s=*state;s.requireCaller(b);if(!s.ready||!s.rasters)throw Failure("Snapshot raster has no live driver");s.rasters->beginSnapshot(c,b);}
void EngineDriver::createSnapshotTexture(PPCContext& c,uint8_t* b) {auto& s=*state;s.requireCaller(b);if(!s.ready||!s.rasters)throw Failure("Snapshot texture has no live driver");s.rasters->createSnapshotTexture(c,b);}
void EngineDriver::commitMovieFrame(PPCContext& c,uint8_t* b) {auto& s=*state;s.requireCaller(b);if(!s.ready||!s.rasters)throw Failure("Movie frame has no live driver");s.rasters->commitMovieFrame(c,b);}
void EngineDriver::preflightMovieLock(PPCContext& c,uint8_t* b) {auto& s=*state;s.requireCaller(b);if(!s.ready||!s.rasters)throw Failure("Movie lock has no live driver");s.rasters->preflightMovieLock(c,b);}
void EngineDriver::lockMovieRaster(PPCContext& c,uint8_t* b) {auto& s=*state;s.requireCaller(b);if(!s.ready||!s.rasters)throw Failure("Movie lock has no live driver");s.rasters->lockMovieRaster(c,b);}
void EngineDriver::preflightMovieUnlock(PPCContext& c,uint8_t* b) {auto& s=*state;s.requireCaller(b);if(!s.ready||!s.rasters)throw Failure("Movie unlock has no live driver");s.rasters->preflightMovieUnlock(c,b);}
void EngineDriver::unlockMovieRaster(PPCContext& c,uint8_t* b) {auto& s=*state;s.requireCaller(b);if(!s.ready||!s.rasters)throw Failure("Movie unlock has no live driver");s.rasters->unlockMovieRaster(c,b);}
void EngineDriver::finishSnapshotRaster(PPCContext& c,uint8_t* b) {auto& s=*state;s.requireCaller(b);if(!s.ready||!s.rasters)throw Failure("Snapshot raster has no live driver");s.rasters->finishSnapshot(c,b);}
void EngineDriver::commitCrossfade(PPCContext& c,uint8_t* b) {auto& s=*state;s.requireCaller(b);if(!s.ready||!s.rasters)throw Failure("Crossfade has no live driver");s.rasters->commitCrossfade(c,b);}
void EngineDriver::destroyRaster(const PPCContext& incoming,uint8_t* base,uint32_t raster) {
    auto& s=*state;s.requireCaller(base);
    if(!s.ready || !s.rasters) throw Failure("Native raster destroy requires its live driver");
    preflightRasterDestroy(base,raster);
    s.rasters->destroy(incoming,base,raster);
    if(s.camera && (s.camera->colorRaster==raster || s.camera->depthRaster==raster)) s.camera.reset();
}
void EngineDriver::preflightRasterDestroy(uint8_t* base,uint32_t raster) const {
    auto& s=*state;s.requireCaller(base);
    if(!s.ready || !s.rasters) throw Failure("Native raster destruction requires its live driver");
    if(s.camera && (s.camera->colorRaster==raster || s.camera->depthRaster==raster) && PPC_LOAD_U32(0x82D0CB1C))
        throw Failure("Native camera raster is still inside its original begin/end pass");
    s.rasters->preflightDestroy(base,raster);
}
size_t EngineDriver::rasterCount() const {
    state->requireCaller(state->runtime.base);return state->rasters?state->rasters->liveCount():0;
}
void EngineDriver::attachTextureRaster(uint32_t raster,std::shared_ptr<Graphics::Texture> texture) {
    auto& s=*state;s.requireCaller(s.runtime.base);
    if(!s.ready || !s.submissionReady || !s.rasters) throw Failure("Native loading texture attachment requires its live driver");
    s.backend.validateTexture(texture);s.rasters->attachTexture(s.runtime.base,raster,std::move(texture));
}
EngineParticles& EngineDriver::particles() {state->requireCaller(state->runtime.base);return *state->particles;}
EngineITXDTextures& EngineDriver::itxdTextures() {
    auto& s=*state;
    // Original dictionary loading can run on worker threads. Its typed service
    // checks the runtime/context and serializes CPU transactions itself; only
    // lazy GPU texture consumption requires the backend's owner thread.
    if(active!=&s.runtime || !s.itxdTextures) throw Failure("Native ITXD service has no live runtime owner");
    s.runtime.checkRunning();return *s.itxdTextures;
}
std::shared_ptr<Graphics::Texture> EngineDriver::textureRaster(uint32_t raster) const {
    auto& s=*state;s.requireCaller(s.runtime.base);
    if(!s.ready || !s.rasters) throw Failure("Native texture lookup requires its live driver");
    if(s.itxdTextures && s.itxdTextures->ownsRaster(raster)) return s.itxdTextures->texture(s.runtime.base,raster);
    return s.rasters->texture(s.runtime.base,raster);
}
std::shared_ptr<Graphics::Texture> EngineDriver::materialTexture(uint8_t* base,uint32_t value) const {
    auto& s=*state;
    if(s.runtime.resourceAudit.active())try {
    std::string asset="unattributed-texture",ownership="unknown-owner";char parameters[240]="identity=unresolved",instance[160];
    std::snprintf(instance,sizeof(instance),"header=%08X",value);
    if(s.itxdTextures)if(const auto cached=s.itxdTextures->auditIdentity(value)) {
        asset=cached->name;ownership="ITXD-phase="+std::to_string(cached->phase);const auto& d=cached->descriptor;
        std::snprintf(parameters,sizeof(parameters),"payload_bytes=%u descriptor=%08X,%08X,%08X,%08X,%08X,%08X",
            cached->bytes,d[0],d[1],d[2],d[3],d[4],d[5]);
        std::snprintf(instance,sizeof(instance),"header=%08X metadata=%08X payload=%08X generation=%llu",value,cached->metadata,cached->payload,
            static_cast<unsigned long long>(cached->generation));
    }
    if(s.builtinTextures&&s.builtinTextures->owns(value)){asset="builtin-image";ownership="builtin-owned";}
    s.runtime.resourceAudit.observe("texture_binding",asset,currentContext?uint32_t(currentContext->lr):0,
        parameters,ownership,s.runtime.nativeDepthCopyCount.load(),instance);
    }catch(...){std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] texture binding capture failed\n");}
    s.requireCaller(base);
    if(!s.ready||!s.submissionReady||!s.itxdTextures)
        throw Failure("Native material texture requires its live driver");
    if(s.builtinTextures&&s.builtinTextures->owns(value)) {
        if(!s.builtinTextures->complete())throw Failure("Native material builtin image is not published");
        auto texture=s.builtinTextures->texture(value);
        s.backend.validateTexture(texture);
        return texture;
    }
    return s.itxdTextures->textureFromHeader(base,value);
}
std::vector<uint8_t> EngineDriver::readbackTextureRaster(uint32_t raster) {
    return state->backend.readback(textureRaster(raster));
}
bool EngineDriver::readNativeTexture(const PPCContext& incoming,uint8_t* base,uint32_t stream,uint32_t output,uint32_t chunkLength) {
    auto& s=*state;s.requireCaller(base);
    if(!s.ready || !s.submissionReady || !s.rasters) throw Failure("Native texture stream requires its registered driver");
    return readLoadingTexture(incoming,base,s.backend,*this,stream,output,chunkLength);
}
void EngineDriver::configureSubmission(uint8_t* base,uint32_t identity,uint32_t descriptor) {
    auto& s=*state;s.requireCaller(base);requireContext(identity);
    if(s.submissionReady || PPC_LOAD_U32(s.engine+0x144)!=3)
        throw Failure("Native submission registration requires the original completed plugin/start path exactly once");
    s.runtime.pointer(0x82D5DA74,4,true);s.runtime.pointer(0x82D6D890,4,true);
    if(PPC_LOAD_U32(0x82D5DA74) || PPC_LOAD_U32(0x82D6D890))
        throw Failure("Native submission aliases already have an owner");
    s.runtime.pointer(descriptor,24,false);
    const std::array<uint32_t,6> words={PPC_LOAD_U32(descriptor),PPC_LOAD_U32(descriptor+4),PPC_LOAD_U32(descriptor+8),
        PPC_LOAD_U32(descriptor+12),PPC_LOAD_U32(descriptor+16),PPC_LOAD_U32(descriptor+20)};
    if(words[0] || words[1]!=0x20000 || words[3]!=0x600000 || words[5] || !words[2] || !words[4] ||
       words[2]!=PPC_LOAD_U32(0x82E0759C) || words[4]!=PPC_LOAD_U32(0x82E075A0) || (words[2]&0xFFF) || (words[4]&0xFFFF))
        throw Failure("Unimplemented native submission storage descriptor or missing original allocation");
    const uint32_t allocator=PPC_LOAD_U32(0x82D57244);
    if(allocator!=0x82D5724C || PPC_LOAD_U32(allocator)!=0x820B60B8 ||
       PPC_LOAD_U32(0x820B60D8)!=0x8268E138 || PPC_LOAD_U32(0x820B60DC)!=0x8268E6E8)
        throw Failure("Unverified original submission allocator ownership");
    GraphicsStorageReservation reservation{identity,allocator,0x8268E6E8,{words[2],words[4]}, {}, {0x20000,0x600000}};
    for(size_t i=0;i<2;++i) {
        s.runtime.pointer(reservation.address[i],reservation.size[i],true);
        reservation.physical[i]=s.runtime.physicalAddress(reservation.address[i]);
        for(uint32_t offset=0;offset<reservation.size[i];offset+=0x1000)
            if(s.runtime.queryPhysicalProtect(reservation.address[i]+offset)!=0x404)
                throw Failure("Original submission allocation lacks its observed read/write/cache protection");
    }
    if(reservation.physical[0]<uint64_t(reservation.physical[1])+reservation.size[1] &&
       reservation.physical[1]<uint64_t(reservation.physical[0])+reservation.size[0])
        throw Failure("Original submission reservations overlap physical storage");
    {
        std::lock_guard lock(s.runtime.vmMutex);
        bool owned=false;
        for(const auto& allocation:s.runtime.physicalAllocations)
            if(allocation.address==words[4] && allocation.size==0x600000 && allocation.pageSize==0x10000 && allocation.protect==0x20000404)
                owned=true;
        if(!owned) throw Failure("Original large submission reservation has no matching physical allocation");
    }
    for(const auto& old:s.runtime.graphicsStorage)
        for(size_t i=0;i<2;++i) for(size_t j=0;j<2;++j)
            if(reservation.physical[i]<uint64_t(old.physical[j])+old.size[j] && old.physical[j]<uint64_t(reservation.physical[i])+reservation.size[i])
                throw Failure("Original submission allocation is already retained by a native integration lifetime");
    // D3D11CreateDevice already created the native submission machinery. Its
    // actual immediate context/device association replaces console ring setup.
    // Preserve the original memory reservations, never console packets/cursors.
    s.backend.validateSubmissionContext();
    s.runtime.graphicsStorage.push_back(reservation); // Last potentially allocating publication.
    s.submissionReady=true;
    fprintf(stderr,"[NATIVE ENGINE] native submission registered for context=%08X; original CPU reservations=%08X/20000,%08X/600000 retained until runtime teardown; no console command stream\n",
        identity,words[2],words[4]);
}
void EngineDriver::start(const PPCContext& incoming,uint8_t* base) {
    auto& s=*state;s.requireCaller(base);
    if(s.attempted) throw Failure("A native driver instance supports one checked start/stop lifetime");
    if(PPC_LOAD_U32(s.engine+0x144)!=2 || PPC_LOAD_U32(0x82D0CB08) || !PPC_LOAD_U32(0x82D0CAF4))
        throw Failure("Original engine start lifecycle mismatch");
    // Original SDK allocations could lazily initialize this general game
    // allocator. Native backing must not silently omit that CPU side effect.
    // Actual original startup has already initialized it before this boundary.
    if(!PPC_LOAD_U32(0x82D57244))
        throw Failure("Native driver requires the original general allocator initialization");
    if(!(PPC_LOAD_U32(0x82E3DFBC)&0x10000))
        throw Failure("Unimplemented native driver single-buffer capability path");
    for(uint32_t field:{0x82D0CB3Cu,0x82D0D020u,0x82D0CB28u,0x82D0CB2Cu,0x82D0CB30u,
        0x82D101D4u,0x82D101D8u,0x82D507F0u,0x82D507F4u,0x82D507F8u,0x82D507FCu})
        if(PPC_LOAD_U32(field)) throw Failure("Native driver requires unowned startup pool/resource fields");
    s.validateTargets(base);s.attempted=true;
    EngineCpuCalls cpu(incoming,base);
    try {
        s.initializePresentation(cpu,base);s.publishTargets(base);
        s.bindingEntered=true;cpu.registers().lr=0x823EE188;cpu.invoke(0x823EDD38);
        if(!PPC_LOAD_U32(0x82D0CB3C)) throw Failure("Original binding pool allocation failed");
        s.cacheEntered=true;cpu.registers().lr=0x823EE18C;cpu.invoke(0x823F4780);
        s.rasterEntered=true;cpu.registers().lr=0x823EE190;cpu.invoke(0x823F69E0);
        if(!PPC_LOAD_U32(0x82D0D020)) throw Failure("Original raster pool allocation failed");
        s.scratchEntered=true;cpu.registers().lr=0x823EE194;cpu.invoke(0x82409A90);
        s.scratch->validateCreated(PPC_LOAD_U32(0x82D101D4),PPC_LOAD_U32(0x82D101D8));
        cpu.registers().lr=0x823EE198;cpu.invoke(0x824008E0);
        if(!s.renderState->effective().initialized() || PPC_LOAD_U32(0x82D10114) || PPC_LOAD_U32(0x82D10118))
            throw Failure("Native state initialization did not commit original queues");
        s.dynamic.start(s.runtime,cpu,base,s.resources);
        if(!s.dynamic.initialized()) throw Failure("Native dynamic buffer owner did not initialize");
        s.im2dStaging=s.runtime.allocatePhysical(0,0x40000,PAGE_READWRITE,0,UINT32_MAX,4096);
        if(!s.im2dStaging)throw Failure("Native Im2D vertex staging allocation failed");
        s.rasters=std::make_unique<EngineRasters>(s.runtime,s.backend,s.targetIds[0],s.targetIds[1],
            s.resources.resources().defaultColor,s.resources.resources().defaultDepth);
        s.ready=true;PPC_STORE_U32(0x82D0CB08,1);
        fprintf(stderr,"[NATIVE ENGINE] persistent %ux%u driver started: six target roles, scratch, state and four dynamic buffers; console device=0, lifecycle=2, game frames=0\n",s.width,s.height);
        fprintf(stderr,"[NATIVE ENGINE] original plugin constructors will now execute with native material/declaration/pipeline owners\n");
    } catch(...) {
        auto failure=std::current_exception();
        try {s.cleanup(cpu,base,false);} catch(const std::exception& error) {
            fprintf(stderr,"[NATIVE ENGINE] partial-start cleanup incomplete: %s; terminal host release follows\n",error.what());
        }
        std::rethrow_exception(failure);
    }
}
void EngineDriver::stop(const PPCContext& incoming,uint8_t* base) {
    state->requireCaller(base);
    if(!state->ready) throw Failure("Native driver stop requires a completed active start");
    EngineCpuCalls cpu(incoming,base);state->cleanup(cpu,base,true);
}
uint32_t EngineDriver::applicationScalar(uint8_t* base,uint32_t application,uint32_t selector,uint32_t value,bool apply) {
    auto& s=*state;s.requireCaller(base);
    if(!s.ready) throw Failure("Native application state requires a started driver");
    // Static constructor 82CB9580 passes this literal object to 82725848.
    // Alternate allocations need their own proven lifetime/extent contract.
    if(application!=0x82D5DB78) throw Failure("Unknown native application state object");
    s.runtime.pointer(application,0x41D4,false);
    requireStateContext(PPC_LOAD_U32(0x82D6D890));
    return s.renderState->applicationScalar(base,application,selector,value,apply);
}
const Graphics::EngineState& EngineDriver::effectiveState() const {
    auto& s=*state;s.requireCaller(s.runtime.base);
    if(!s.ready) throw Failure("Native effective-state query requires a started driver");
    return s.renderState->effective();
}
uint32_t EngineDriver::applicationSampler(uint8_t* base,uint32_t application,uint32_t stage,uint32_t selector,uint32_t value,bool apply) {
    auto& s=*state;s.requireCaller(base);
    if(!s.ready) throw Failure("Native application sampler requires a started driver");
    if(application!=0x82D5DB78) throw Failure("Unknown native application sampler object");
    s.runtime.pointer(application,0x41D4,false);
    requireStateContext(PPC_LOAD_U32(0x82D6D890));
    return s.renderState->applicationSampler(base,application,stage,selector,value,apply);
}
void EngineDriver::directSpriteBatch(PPCContext& c,uint8_t* base,bool begin,bool clearTextures,uint32_t endpoint){
    auto& s=*state;s.requireCaller(base);
    if(s.directSprite.active||!s.ready||!s.submissionReady||currentContext!=&c)
        throw Failure("Direct sprite batch owner differs");
    if(c.r1.u32<0x200||(c.r1.u32&15))throw Failure("Original sprite batch stack differs");
    if(begin) {
        if(endpoint!=0x8276B750||c.lastFunction!=endpoint||!clearTextures)
            throw Failure("Original sprite batch begin endpoint differs");
    } else if(clearTextures) {
        if(endpoint!=0x8276B898||c.lastFunction!=endpoint)
            throw Failure("Original sprite batch end endpoint differs");
    } else {
        s.runtime.pointer(c.r1.u32,0x140,false);
        if(endpoint!=0x8276B6D8||PPC_LOAD_U32(c.r1.u32)!=c.r1.u32+0x140)
            throw Failure("Original sprite emitter cleanup frame differs");
        if(uint32_t(c.lr)!=endpoint) {
            // B6B4 skips AF78 when emitter+164 is zero. The unconditional
            // B598 call has already returned to B59C; no later instruction
            // on this branch writes LR. This is a separate zero-draw path.
            s.runtime.pointer(c.r31.u32,0xE0,false);
            s.runtime.pointer(c.r29.u32,48,false);
            if(uint32_t(c.lr)!=0x8276B59C||c.r10.u32||!c.cr6.eq||PPC_LOAD_U32(c.r31.u32+164)||
               c.r28.u32!=cameraBinding().camera)
                throw Failure("Original zero-texture sprite emitter cleanup differs");
        }
    }
    auto next=s.renderState->effective();using S=Graphics::ScalarState;using T=Graphics::SamplerState;
    next.setScalar(S::AlphaTest,0);next.setScalar(S::BlendEnable,begin?1:0);
    next.setScalar(S::DepthEnable,begin?0:1);next.setScalar(S::DepthWrite,begin?0:1);
    if(begin){
        next.setPackedBlend(0,0x10106);
        for(unsigned stage=0;stage<2;++stage){
            next.setSampler(stage,T::Minification,stage?0:1);next.setSampler(stage,T::Magnification,stage?0:1);
            next.setSampler(stage,T::AddressU,1);next.setSampler(stage,T::AddressV,1);
        }
        s.backend.bindSpriteQueryTexture(s.viewportSurfaces->queryTexture(PPC_LOAD_U32(0x82DFEB6C)));
    }else{
        if(s.effects)s.effects->preflightScreenReplacement(base);
        const auto batch=s.backend.finishSpriteBatch(clearTextures);PPC_STORE_U32(0x82CD1A68,0);
        if(s.effects)s.effects->completeScreenBatchRetirement(base,batch);
    }
    s.renderState->publishScreenState(next);
}
void EngineDriver::coronaDeclaration(PPCContext& c,uint8_t* base,bool release) {
    auto& s=*state;s.requireCaller(base);requireContext(PPC_LOAD_U32(0x82D5DA74));
    constexpr uint32_t field=0x82DFF288,source=0x82153E0C;
    constexpr std::array<uint32_t,12> words={0,0x002C23A5,0,8,0x001A23A6,0x00050000,
        24,0x002A23B9,0x00050100,0x00FF0000,0xFFFFFFFF,0};
    if(currentContext!=&c)throw Failure("Corona declaration lacks its original CPU frame");
    if(release){
        if(!s.coronaDeclarationId||c.r3.u32!=s.coronaDeclarationId||PPC_LOAD_U32(field)!=s.coronaDeclarationId||s.coronaQueries.active)
            throw Failure("Corona declaration retirement differs");
        s.coronaDeclarationId=0;s.coronaDeclarationWords={};c.r3.u64=0;c.lr=0x8276C06C;return;
    }
    if(c.r3.u32!=source||s.coronaDeclarationId||PPC_LOAD_U32(field))throw Failure("Corona declaration creation differs");
    for(unsigned i=0;i<words.size();++i)if(PPC_LOAD_U32(source+4*i)!=words[i])throw Failure("Original corona declaration bytes changed");
    s.coronaDeclarationId=allocateTargetIdentity();s.coronaDeclarationWords=words;c.r3.u64=s.coronaDeclarationId;c.lr=0x8276BF24;
}
void EngineDriver::beginCoronaQueries(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.requireCaller(base);auto& q=s.coronaQueries;
    if(!s.ready||!s.submissionReady||currentContext!=&c||q.active||s.directSprite.active||
       c.r23.u32!=PPC_LOAD_U32(0x82DFF28C)||!c.r23.u32||!c.r24.u32||c.r29.u32||
       PPC_LOAD_U32(c.r1.u32)!=c.r1.u32+256)throw Failure("Corona producer CPU frame differs");
    s.runtime.pointer(c.r23.u32,0x50,false);const uint32_t count=PPC_LOAD_U32(c.r23.u32+0x4C);
    if(!count||count>512||PPC_LOAD_U32(c.r23.u32+0x44)!=c.r24.u32)throw Failure("Corona producer list/count differs");
    if(!s.coronaDeclarationId||PPC_LOAD_U32(0x82DFF288)!=s.coronaDeclarationId)throw Failure("Corona producer declaration owner missing");
    for(unsigned i=0;i<12;++i)if(PPC_LOAD_U32(0x82153E0C+4*i)!=s.coronaDeclarationWords[i])throw Failure("Corona source declaration changed");
    std::vector<uint32_t> entries;uint32_t entry=c.r24.u32;
    for(uint32_t i=0;i<count;++i){s.runtime.pointer(entry,0x1C,true);
        if(!entry||std::find(entries.begin(),entries.end(),entry)!=entries.end()||PPC_LOAD_U8(entry+0x18)>=64||PPC_LOAD_U8(entry+0x19)>=8)
            throw Failure("Corona producer list contains a cycle or invalid cell");
        entries.push_back(entry);entry=PPC_LOAD_U32(entry);
    }
    if(entry)throw Failure("Corona producer list exceeds its original count");
    s.screenMaterial(base,5);s.screenMaterial(base,6);s.screenMaterial(base,3);s.screenMaterial(base,2);
    if(PPC_LOAD_U32(0x82DFEB34)!=s.screenDeclarationIds[1]||!s.screenDeclarations.contains(s.screenDeclarationRecords[1]))
        throw Failure("Corona restoration declaration owner missing");
    s.viewportSurfaces->depthTexture(PPC_LOAD_U32(0x82DFEB68));s.viewportSurfaces->queryTexture(PPC_LOAD_U32(0x82DFEB6C));
    s.viewportSurfaces->queryBackupTexture(PPC_LOAD_U32(0x82DFEB64));
    const auto camera=cameraBinding();
    if(camera.viewport[0]||camera.viewport[1]||camera.viewport[2]!=1280||camera.viewport[3]!=720||
       PPC_LOAD_U32(0x82DFEB3C)!=0x3A4CCCCD||PPC_LOAD_U32(0x82DFEB40)!=0x3AB60B61)
        throw Failure("Corona producer viewport/inverse dimensions differ");
    auto post=s.renderState->effective();using S=Graphics::ScalarState;using T=Graphics::SamplerState;
    if(post.scalar(S::StencilEnable)||post.scalar(S::ScissorEnable)||post.scalar(S::ClipPlaneEnable)||post.scalar(S::AlphaToMask)||
       post.scalar(S::Fill)||post.scalar(S::ColorMask0)!=15)throw Failure("Corona inherited state is unqualified");
    post.setScalar(S::AlphaTest,0);post.setScalar(S::HalfPixelOffset,1);post.setScalar(S::Cull,0);
    post.setScalar(S::BlendEnable,0);post.setScalar(S::DepthEnable,1);post.setScalar(S::DepthWrite,1);post.setPackedBlend(0,0x10001);
    post.setSampler(0,T::Minification,0);post.setSampler(0,T::Magnification,0);post.setSampler(0,T::AddressU,1);post.setSampler(0,T::AddressV,1);
    // The nonempty A820 branch replaces the program without ending its FX
    // manager. Qualify that retained owner before any original setup/cache
    // publication; the empty A844 branch never reaches this hook.
    if(s.effects)s.effects->preflightScreenReplacement(base);
    if(!q.staging)q.staging=s.runtime.allocatePhysical(0,0x5000,PAGE_READWRITE,0,UINT32_MAX,4096);
    if(!q.staging)throw Failure("Corona CPU vertex staging allocation failed");
    const auto manager=PPC_LOAD_U32(0x82D08BFC);if(manager)s.runtime.pointer(manager,0x10,false);
    q.effectAssociation={manager,manager?PPC_LOAD_U32(manager+4):0,manager?PPC_LOAD_U32(manager+8):0,
        manager?PPC_LOAD_U32(manager+0xC):0};
    q.bindings={PPC_LOAD_U32(0x82CD1A68),PPC_LOAD_U32(0x82CD1A6C),PPC_LOAD_U32(0x82CD1A70)};
    q.cpu=&c;q.camera=camera.camera;q.drawBefore=s.backend.coronaQueryDrawCount();
    q.stack=c.r1.u32;q.owner=c.r23.u32;q.count=count;q.entries=std::move(entries);q.post=post;q.active=true;
    c.r3.u32=q.staging;c.r30.u32=0x82DFEA20;c.r25.u32=1;
}
void EngineDriver::finishCoronaQueries(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.requireCaller(base);auto& q=s.coronaQueries;
    if(!q.active||currentContext!=&c||q.cpu!=&c||q.stack!=c.r1.u32||q.owner!=c.r23.u32||c.r24.u32||c.r3.u32!=q.staging+36*q.count)
        throw Failure("Corona CPU vertex completion differs");
    std::vector<Graphics::CoronaVertex> vertices(q.count);
    for(uint32_t i=0;i<q.count;++i){
        if(PPC_LOAD_U8(q.entries[i]+0x1A)!=1)throw Failure("Original corona CPU loop did not publish query readiness");
        std::array<float,9> words{};for(unsigned j=0;j<9;++j)words[j]=std::bit_cast<float>(PPC_LOAD_U32(q.staging+36*i+4*j));
        std::memcpy(&vertices[i],words.data(),sizeof(words));
    }
    const auto camera=cameraBinding();bool alpha=false;
    const auto manager=PPC_LOAD_U32(0x82D08BFC);
    if(camera.camera!=q.camera||manager!=q.effectAssociation[0]||
       (manager?PPC_LOAD_U32(manager+4):0)!=q.effectAssociation[1]||(manager?PPC_LOAD_U32(manager+8):0)!=q.effectAssociation[2]||
       (manager?PPC_LOAD_U32(manager+0xC):0)!=q.effectAssociation[3]||q.bindings!=std::array<uint32_t,3>{
           PPC_LOAD_U32(0x82CD1A68),PPC_LOAD_U32(0x82CD1A6C),PPC_LOAD_U32(0x82CD1A70)})
        throw Failure("Original corona traversal changed its retained camera/effect/program owner");
    if(s.effects)s.effects->preflightScreenReplacement(base);
    s.backend.drawCoronaQueries(color(camera.colorIdentity,alpha),depth(camera.depthIdentity),
        s.viewportSurfaces->depthTexture(PPC_LOAD_U32(0x82DFEB68)),s.viewportSurfaces->queryBackupTexture(PPC_LOAD_U32(0x82DFEB64)),
        s.viewportSurfaces->queryTexture(PPC_LOAD_U32(0x82DFEB6C)),vertices,q.post.scalar(Graphics::ScalarState::DepthCompare));
    s.renderState->publishScreenState(q.post);
    PPC_STORE_U32(0x82CD1A6C,PPC_LOAD_U32(0x82CF2340));PPC_STORE_U32(0x82CD1A70,PPC_LOAD_U32(0x82CF2334));
    PPC_STORE_U32(0x82CD1A68,PPC_LOAD_U32(0x82DFEB34));q.active=false;c.lr=0x8276AC04;
    const auto replacement=s.backend.completedCoronaQueryReplacement(q.drawBefore);
    if(s.effects)s.effects->completeScreenReplacement(base,replacement,PPC_LOAD_U32(0x82DFEB34),
        PPC_LOAD_U32(0x82CF2340),PPC_LOAD_U32(0x82CF2334));
    static unsigned traced=0;if(traced++<5)std::fprintf(stderr,"[NATIVE CORONA QUERY] entries=%u original_cpu_vertices=true depth_samples=17x17 scene_tile_preserved=true\n",q.count);
}
uint64_t EngineDriver::coronaQueryDrawCount() const {return state->backend.coronaQueryDrawCount();}
void EngineDriver::immediateBufferLifecycle(PPCContext& c,uint8_t* base,bool release) {
    auto& s=*state;s.requireCaller(base);const auto row=release?c.r30.u32:c.r29.u32;
    if(currentContext!=&c||c.r31.u32!=0x82DFEA20||row<0x82DFE978||row>0x82DFE9F0||(row-0x82DFE978)%40||PPC_LOAD_U32(0x82DFEB20)!=row)
        throw Failure("Immediate vertex buffer lifecycle differs from its original row");
    auto& record=s.immediateBuffers[(row-0x82DFE978)/40];
    if(release){s.immediateBuffer(base,row+8);record={};return;}
    if(record.buffer)throw Failure("Duplicate original immediate vertex buffer publication");
    const auto data=PPC_LOAD_U32(row+4);s.runtime.pointer(data,0x100000,false);
    if(!data||(data&4095))throw Failure("Original immediate vertex allocation differs");
    State::ImmediateBuffer pending;pending.data=data;
    for(unsigned i=0;i<8;++i)pending.header[i]=PPC_LOAD_U32(row+8+4*i);
    pending.buffer=s.backend.createBuffer(0x100000,Graphics::BufferKind::Vertex);record=std::move(pending);
}
void EngineDriver::bindImmediateBuffer(uint8_t* base) {
    auto& s=*state;s.requireCaller(base);const auto row=PPC_LOAD_U32(0x82DFEB20);
    auto buffer=s.immediateBuffer(base,row+8);if(!buffer)throw Failure("Unknown original immediate vertex row");
    const auto start=PPC_LOAD_U32(row+4),cursor=PPC_LOAD_U32(row);
    if(cursor<start||cursor-start>0x100000||((cursor-start)&31))throw Failure("Original immediate vertex cursor exceeds its allocation");
    // Snapshot the current CPU storage. Any future draw must upload its final
    // range at that engine boundary; the unported general draw rejects there.
    s.backend.writeBuffer(buffer,0,{s.runtime.pointer(start,0x100000,false),0x100000});
    s.backend.bindEngineVertexStorage(buffer,cursor-start,32);
    PPC_STORE_U32(0x82D0CAB0,row+8);PPC_STORE_U32(0x82D0CAB4,cursor-start);PPC_STORE_U32(0x82D0CAB8,32);
}
void EngineDriver::prepareImmediateProjection(uint8_t* base,Graphics::ImmediateDraw& draw,Graphics::EngineState& post,
                                            uint32_t& owner,uint32_t& identity) {
    auto& s=*state;using T=Graphics::SamplerState;
    owner=PPC_LOAD_U32(0x82DFEB98);s.runtime.pointer(owner,0x2A0,false);identity=PPC_LOAD_U32(owner+0xF0);
    const auto resource=s.shadowTextures->view(identity);
    if(resource.owner!=owner||resource.field!=0xF0||resource.width!=1024||resource.height!=1024)
        throw Failure("Projected immediate depth differs from its original shadow owner");
    draw.projectionDepth=s.shadowTextures->depth(identity);
    if(!draw.projectionDepth)throw Failure("Projected immediate owner has no native depth resource");
    draw.projected=true;
    post.setSampler(2,T::Minification,0);post.setSampler(2,T::Magnification,0);post.setSampler(2,T::MipFilter,0);
    post.setSampler(2,T::AddressU,1);post.setSampler(2,T::AddressV,1);
    if(post.sampler(2,T::LodBiasBits)||post.sampler(2,T::MinimumMip)||post.sampler(2,T::MaximumMip)!=13||
       post.sampler(2,T::MaximumAnisotropy)!=1)throw Failure("Projected immediate inherited sampler LOD profile differs");
    auto& sampler=draw.projectionSampler;sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU=sampler.AddressV=D3D11_TEXTURE_ADDRESS_MIRROR;
    sampler.AddressW=D3D11_TEXTURE_ADDRESS_MODE(post.sampler(2,T::AddressW)+1);
    sampler.MaxLOD=13;sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
}
void EngineDriver::immediateGeometryState(PPCContext& c,uint8_t* base,bool begin) {
    auto& s=*state;s.requireCaller(base);s.runtime.pointer(c.r3.u32,8,true);
    const uint32_t mode=begin?c.r5.u32:PPC_LOAD_U32(c.r3.u32+4);
    const bool decalMode=(mode==1||mode==5)&&s.decal.cpu==&c;
    bool projectedConsumer=(mode==4||mode==6)&&!begin&&((s.billboard.cpu==&c&&s.billboard.mode==mode)||(s.trail.owner&&s.trail.mode==mode));
    if(begin&&(mode==4||mode==6)) {
        const uint32_t parent=PPC_LOAD_U32(c.r1.u32),caller=PPC_LOAD_U32(parent-8);
        if(parent!=c.r1.u32+0x80||c.r3.u32!=parent+0x70)throw Failure("Projected immediate Begin has no original caller frame");
        if(caller==0x8275F21C) {
            s.runtime.pointer(c.r31.u32,0x120,false);const auto definition=PPC_LOAD_U32(c.r31.u32+0x118);s.runtime.pointer(definition,0x108,false);
            projectedConsumer=PPC_LOAD_U32(c.r31.u32)==0x821530EC&&PPC_LOAD_U32(parent)==parent+0x1F0&&c.r4.u32==parent+0x64&&
                mode==((PPC_LOAD_U8(definition+0x104)&4)|(PPC_LOAD_U8(definition+0x40)==1?2u:0u));
        }else if(caller==0x8277E3BC) {
            s.runtime.pointer(c.r31.u32,0xE4,false);const auto definition=PPC_LOAD_U32(c.r31.u32+0xA0);s.runtime.pointer(definition,0x8C,false);
            projectedConsumer=PPC_LOAD_U32(c.r31.u32)==0x82158BB0&&PPC_LOAD_U32(parent)==parent+0x1B0&&c.r4.u32==parent+0x50&&
                mode==(((PPC_LOAD_U32(definition+0x20)>>3)&4)|(PPC_LOAD_U32(c.r31.u32+0xA8)?2u:0u));
        }
    }
    if(currentContext!=&c||!s.ready||!s.submissionReady||(mode!=0&&mode!=2&&!decalMode&&!projectedConsumer)) {
        throw Failure("Unqualified immediate geometry setup mode");
    }
    if(begin&&(s.trail.owner||s.billboard.owner||s.beam.owner||s.immediateEffect.cpu||(s.decal.cpu&&!decalMode)))throw Failure("Immediate setup overlaps an active original immediate consumer");
    auto next=s.renderState->effective();using S=Graphics::ScalarState;
    next.setScalar(S::AlphaTest,0);next.setScalar(S::DepthEnable,1);next.setScalar(S::DepthWrite,1);
    if(begin){
        if(decalMode)decalOperation(c,base,0x82751B74);
        const uint32_t pixelSlot=(mode&4)?((mode&2)?24u:23u):(mode&2)?17u:9u;
        s.screenMaterial(base,(mode&4)?22:8);s.screenMaterial(base,pixelSlot);
        bindImmediateBuffer(base);next.setScalar(S::Cull,0);next.setPackedBlend(0,0x10706);s.backend.clearEngineTexture(0);
        PPC_STORE_U32(0x82CD1A6C,PPC_LOAD_U32((mode&4)?0x82CF1FC8:0x82CF1FBC));
        PPC_STORE_U32(0x82CD1A70,PPC_LOAD_U32((mode&4)?((mode&2)?0x82CF1FB0:0x82CF1FA4):(mode&2)?0x82CF1F98:0x82CF1F8C));
        PPC_STORE_U32(0x82CD1A68,PPC_LOAD_U32(0x82DFE350));PPC_STORE_U32(c.r3.u32+4,mode);PPC_STORE_U32(c.r3.u32,0);
        if(decalMode){next.setScalar(S::SlopeBias,0x3F000000);next.setScalar(S::DepthBias,0x38FBA882);}
    }else {
        if(s.decal.cpu){decalOperation(c,base,0x82751DA0);next.setScalar(S::DepthBias,0);next.setScalar(S::SlopeBias,0);}
        if(s.beam.owner)beamOperation(c,base,0x82751DA0);
        if(s.immediateEffect.cpu)immediateEffectOperation(c,base,0x82751DA0);
        if(s.billboard.owner) {
            const auto& b=s.billboard;
            if(b.cpu!=&c||c.r1.u32!=b.stack||c.r3.u32!=b.entry||uint32_t(c.lr)!=0x8275F818||
               PPC_LOAD_U32(b.entry+4)!=b.mode||b.count||b.matrix||b.color||b.soft||b.projectedMatrix||
               PPC_LOAD_U32(b.entry)!=b.total||b.total!=4*b.draws)
                throw Failure("Billboard cleanup precedes original particle-loop completion");
            std::fprintf(stderr,"[NATIVE BILLBOARD END] owner=%08X draws=%u vertices=%u\n",b.owner,b.draws,b.total);
            s.billboard={};
        }
        if(s.trail.owner){
            if(c.r3.u32!=s.trail.entry||s.trail.count)throw Failure("Immediate cleanup precedes original trail completion");
            s.trail={};
        }
        next.setScalar(S::BlendEnable,0);
    }
    // Qualified immediate modes are realized by their native trail/billboard
    // draws after the original CPU fill loops.
    s.renderState->publishScreenState(next);
}
void EngineDriver::immediateDeclaration(PPCContext& c,uint8_t* base,bool release) {
    auto& s=*state;s.requireCaller(base);
    constexpr uint32_t words[]={0,0x002A23B9,0,12,0x002C83A4,0xA0000,16,0x001A23A6,0x50000,0xFF0000,0xFFFFFFFF,0};
    for(unsigned i=0;i<std::size(words);++i)if(PPC_LOAD_U32(0x821516F0+4*i)!=words[i])throw Failure("Immediate source declaration changed");
    if(release){
        if(!s.immediateDeclarationId||c.r3.u32!=s.immediateDeclarationId||PPC_LOAD_U32(0x82DFE350)!=s.immediateDeclarationId||s.trail.owner||s.billboard.owner||s.beam.owner||s.immediateEffect.cpu||s.decal.cpu)
            throw Failure("Immediate declaration retirement differs");
        s.immediateDeclarationId=0;c.r3.u64=0;return;
    }
    if(c.r3.u32!=0x821516F0||s.immediateDeclarationId||PPC_LOAD_U32(0x82DFE350))throw Failure("Immediate declaration creation differs");
    s.immediateDeclarationId=allocateTargetIdentity();c.r3.u64=s.immediateDeclarationId;c.lr=0x82751880;
}
void EngineDriver::beginTrail(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.requireCaller(base);auto& t=s.trail;
    const auto owner=c.r31.u32,entry=c.r1.u32+0x70;
    if(currentContext!=&c||t.owner||s.billboard.owner||s.beam.owner||s.immediateEffect.cpu||s.decal.cpu||
       PPC_LOAD_U32(c.r1.u32)!=c.r1.u32+0x1B0||PPC_LOAD_U32(entry))
        throw Failure("Original trail setup frame differs");
    s.runtime.pointer(owner,0xE4,false);const auto definition=PPC_LOAD_U32(owner+0xA0);s.runtime.pointer(definition,0x8C,false);
    const uint32_t texture2=PPC_LOAD_U32(owner+0xA8),mode=PPC_LOAD_U32(entry+4);
    const uint32_t expectedMode=(((PPC_LOAD_U32(definition+0x20)>>3)&4)|(texture2?2u:0u));
    if(mode!=expectedMode||((mode&4)&&PPC_LOAD_U32(owner)!=0x82158BB0))
        throw Failure("Trail original texture mode differs or projected shader is unqualified");
    s.screenMaterial(base,(mode&4)?22:8);s.screenMaterial(base,(mode&4)?((mode&2)?24:23):(mode&2)?17:9);
    if(!s.immediateDeclarationId||PPC_LOAD_U32(0x82DFE350)!=s.immediateDeclarationId||PPC_LOAD_U32(0x82CD1A68)!=s.immediateDeclarationId)
        throw Failure("Trail original declaration owner differs");
    if(PPC_LOAD_U32(0x82CD1A6C)!=PPC_LOAD_U32((mode&4)?0x82CF1FC8:0x82CF1FBC)||
       PPC_LOAD_U32(0x82CD1A70)!=PPC_LOAD_U32((mode&4)?((mode&2)?0x82CF1FB0:0x82CF1FA4):(mode&2)?0x82CF1F98:0x82CF1F8C))
        throw Failure("Trail original shader association differs");
    State::Trail pending;pending.owner=owner;pending.stack=c.r1.u32;pending.entry=entry;pending.row=PPC_LOAD_U32(0x82DFEB20);
    pending.mode=mode;pending.texture2=texture2;
    if(!s.immediateBuffer(base,pending.row+8))throw Failure("Trail original vertex row is unknown");
    auto& draw=pending.draw;
    for(unsigned row=0;row<4;++row)for(unsigned lane=0;lane<4;++lane)
        draw.constants[row][lane]=std::bit_cast<float>(PPC_LOAD_U32(0x82DFEAE0+16*row+4*lane));
    for(unsigned lane=0;lane<3;++lane)draw.constants[4][lane]=std::bit_cast<float>(PPC_LOAD_U32(definition+0x44+4*lane));
    draw.constants[4][3]=float(c.f31.f64);
    const auto texture=PPC_LOAD_U32(owner+0xA4);s.runtime.pointer(texture,0x54,false);
    draw.texture=textureRaster(PPC_LOAD_U32(texture));
    if(texture2){s.runtime.pointer(texture2,0x54,false);pending.raster2=PPC_LOAD_U32(texture2);draw.texture2=textureRaster(pending.raster2);}
    const auto sampling=PPC_LOAD_U32(texture+0x50);
    draw.sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    draw.sampler.AddressU=(sampling&0xF00)==0x100?D3D11_TEXTURE_ADDRESS_WRAP:D3D11_TEXTURE_ADDRESS_CLAMP;
    draw.sampler.AddressV=(sampling&0xF000)==0x1000?D3D11_TEXTURE_ADDRESS_WRAP:D3D11_TEXTURE_ADDRESS_CLAMP;
    draw.sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;draw.sampler.MaxAnisotropy=1;
    draw.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;draw.sampler.MaxLOD=13;
    const auto blend=int8_t(PPC_LOAD_U8(definition+0x24));
    draw.blendWord=blend==0?0x10106:blend==1?0x10706:blend==2?0x10186:0x10001;
    auto post=s.renderState->effective();using S=Graphics::ScalarState;using T=Graphics::SamplerState;
    post.setScalar(S::DepthWrite,0);post.setScalar(S::AlphaTest,1);post.setScalar(S::AlphaCompare,4);post.setScalar(S::AlphaReference,1);
    post.setPackedBlend(0,draw.blendWord);
    post.setSampler(0,T::Minification,1);post.setSampler(0,T::Magnification,1);post.setSampler(0,T::MipFilter,1);
    post.setSampler(0,T::AddressU,draw.sampler.AddressU-1);post.setSampler(0,T::AddressV,draw.sampler.AddressV-1);
    if(mode&2){
        post.setSampler(1,T::Minification,1);post.setSampler(1,T::Magnification,1);post.setSampler(1,T::MipFilter,1);
        post.setSampler(1,T::AddressU,post.sampler(0,T::AddressU));post.setSampler(1,T::AddressV,post.sampler(0,T::AddressV));
    }
    if(mode&4) {
        prepareImmediateProjection(base,draw,post,pending.projectionOwner,pending.projectionIdentity);
        draw.projectionConstants[4]={std::bit_cast<float>(PPC_LOAD_U32(definition+0x74)),float(c.f19.f64),float(c.f19.f64),float(c.f19.f64)};
        // These temporary words are written by the original c25 branch before
        // the SDK upload. Preserve them even though the GPU endpoint is native.
        for(unsigned lane=0;lane<4;++lane)PPC_STORE_U32(pending.stack+0x90+4*lane,std::bit_cast<uint32_t>(draw.projectionConstants[4][lane]));
    }
    // 823CAF00 writes CB80 directly; the scalar enable shadow can remain zero.
    draw.alphaTest=1;draw.alphaReference=1;draw.blendEnable=1;
    draw.depthEnable=post.scalar(S::DepthEnable);draw.depthWrite=0;draw.depthCompare=post.scalar(S::DepthCompare);
    draw.colorMask=post.scalar(S::ColorMask0);draw.cull=post.scalar(S::Cull);draw.stencilEnable=post.scalar(S::StencilEnable);
    draw.fill=post.scalar(S::Fill);draw.scissorEnable=post.scalar(S::ScissorEnable);draw.halfPixelOffset=post.scalar(S::HalfPixelOffset);
    draw.viewportEnable=post.scalar(S::ViewportEnable);draw.clipPlaneEnable=post.scalar(S::ClipPlaneEnable);draw.alphaToMask=post.scalar(S::AlphaToMask);
    draw.multisampleMask=post.scalar(S::MultisampleMask);draw.depthBiasBits=post.scalar(S::DepthBias);draw.slopeBiasBits=post.scalar(S::SlopeBias);
    draw.viewport=cameraBinding().viewport;
    s.backend.bindEngineTexture(0,draw.texture);
    if(mode&2)s.backend.bindEngineTexture(1,draw.texture2);else s.backend.clearEngineTexture(1);
    s.renderState->publishScreenState(post);t=std::move(pending);c.lr=0x8277E4FC;
}
void EngineDriver::reserveImmediate(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.requireCaller(base);auto& t=s.trail;
    if(s.decal.cpu){decalOperation(c,base,0x82751E38);return;}
    if(uint32_t(c.lr)==0x8275F6E4&&s.billboard.owner){billboardOperation(c,base,0x82751E38);return;}
    if(uint32_t(c.lr)==0x8277B124&&s.beam.owner){beamOperation(c,base,0x82751E38);return;}
    if(uint32_t(c.lr)==0x8286D868&&s.beam.owner){beamOperation(c,base,0x82751E38);return;}
    if(s.immediateEffect.cpu){immediateEffectOperation(c,base,0x82751E38);return;}
    if(t.mode&4) {
        if(!t.matrix||!t.projectedMatrix||PPC_LOAD_U32(0x82DFEB98)!=t.projectionOwner||
           PPC_LOAD_U32(t.projectionOwner+0xF0)!=t.projectionIdentity||s.shadowTextures->depth(t.projectionIdentity)!=t.draw.projectionDepth)
            throw Failure("Projected trail lost its original matrices or shadow resource");
    }
    if(!t.owner||t.count||currentContext!=&c||uint32_t(c.lr)!=0x8277E640||c.r1.u32!=t.stack||c.r3.u32!=t.entry||
       c.r4.u32!=6||c.r5.u32<4||c.r5.u32>16||(c.r5.u32&1)||PPC_LOAD_U32(0x82DFEB20)!=t.row) {
        if(uint32_t(c.lr)==0x8275F6E4&&!s.runtime.frameCaptureDirectory.empty()) {
            // Capture the new CPU billboard consumer before permitting a
            // reservation. No vertices are synthesized or submitted here.
            const auto directory=s.runtime.frameCaptureDirectory/"immediate-billboard";
            std::filesystem::create_directories(directory);
            std::ofstream meta(directory/"state.txt");meta<<std::hex;
            const auto dump=[&](const char* name,uint32_t address,uint32_t bytes) {
                meta<<name<<" address="<<address<<" bytes="<<bytes<<'\n';
                const auto* data=s.runtime.pointer(address,bytes,false);
                std::ofstream file(directory/(std::string(name)+".bin"),std::ios::binary);
                file.write(reinterpret_cast<const char*>(data),bytes);
            };
            try {
                meta<<"sp="<<c.r1.u32<<" owner="<<c.r31.u32<<" entry="<<c.r3.u32
                    <<" particle="<<c.r28.u32<<" block="<<c.r30.u32<<" index="<<c.r29.u32<<'\n';
                dump("stack",c.r1.u32,0x1F0);dump("owner",c.r31.u32,0x150);
                dump("definition",PPC_LOAD_U32(c.r31.u32+0x118),0x120);
                dump("material",PPC_LOAD_U32(c.r31.u32+0x11C),0xA0);
                dump("view-projection",0x82DFEAE0,64);dump("particle",c.r28.u32,0x60);
                const auto texture=PPC_LOAD_U32(c.r31.u32+0xD0);
                dump("texture",texture,0x54);dump("raster",PPC_LOAD_U32(texture),0xA4);
                dump("ring",PPC_LOAD_U32(0x82DFEB20),40);
                dump("shader-tokens",0x82CD1A68,12);
                const auto& effective=s.renderState->effective();
                for(const auto& field:Graphics::scalarStateEvidence())meta<<"scalar "<<field.id<<'='<<effective.scalar(field.id)<<'\n';
                for(uint32_t stage=0;stage<2;++stage)for(const auto& field:Graphics::samplerStateEvidence())
                    meta<<"sampler "<<stage<<':'<<field.id<<'='<<effective.sampler(stage,field.id)<<'\n';
                meta<<"blend="<<effective.effectiveBlend(0)<<'\n';
            } catch(const std::exception& error) {meta<<"capture_error="<<error.what()<<'\n';}
        }
        char reason[180];int n=std::snprintf(reason,sizeof(reason),"Unimplemented general immediate geometry draw: caller=%08X primitive=%u vertices=%u",uint32_t(c.lr),c.r4.u32,c.r5.u32);
        if(n<0||size_t(n)>=sizeof(reason)) throw Failure("Unimplemented general immediate geometry draw: diagnostic truncated");
        throw Failure(reason);
    }
    s.immediateBuffer(base,t.row+8);const auto start=PPC_LOAD_U32(t.row+4),cursor=PPC_LOAD_U32(t.row),bytes=32*c.r5.u32;
    if(cursor<start||uint64_t(cursor-start)+bytes>0x100000||((cursor-start)&31))throw Failure("Trail vertex reservation exceeds its original ring allocation");
    s.runtime.pointer(cursor,bytes,true);t.source=cursor;t.count=c.r5.u32;
    PPC_STORE_U32(t.row,cursor+bytes);PPC_STORE_U32(t.entry,PPC_LOAD_U32(t.entry)+t.count);c.r3.u64=cursor;
}
void EngineDriver::finishTrailBatch(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.requireCaller(base);auto& t=s.trail;
    if(!t.owner||!t.count||currentContext!=&c||c.r1.u32!=t.stack||c.r31.u32!=t.owner||
       PPC_LOAD_U32(t.entry+4)!=t.mode||PPC_LOAD_U32(t.owner+0xA8)!=t.texture2||
       (t.texture2&&PPC_LOAD_U32(t.texture2)!=t.raster2)||c.r29.u32!=t.source+32*t.count||
       PPC_LOAD_U32(t.row)!=t.source+32*t.count||PPC_LOAD_U32(0x82DFEB20)!=t.row)throw Failure("Trail CPU vertex completion differs");
    if(t.mode&2)s.backend.requireEngineTexture(1,t.draw.texture2);
    if((t.mode&4)&&(PPC_LOAD_U32(0x82DFEB98)!=t.projectionOwner||PPC_LOAD_U32(t.projectionOwner+0xF0)!=t.projectionIdentity||
                  s.shadowTextures->depth(t.projectionIdentity)!=t.draw.projectionDepth))throw Failure("Projected trail shadow resource changed during CPU fill");
    auto buffer=s.immediateBuffer(base,t.row+8);auto& draw=t.draw;draw.vertices.resize(t.count);
    for(uint32_t i=0;i<t.count;++i){std::array<float,8> values{};for(unsigned j=0;j<8;++j)values[j]=std::bit_cast<float>(PPC_LOAD_U32(t.source+32*i+4*j));
        std::memcpy(&draw.vertices[i],values.data(),32);}
    s.backend.writeBuffer(buffer,t.source-PPC_LOAD_U32(t.row+4),{reinterpret_cast<const uint8_t*>(draw.vertices.data()),32*t.count});
    const auto camera=cameraBinding();bool alphaOne=false;
    s.backend.drawImmediate(color(camera.colorIdentity,alphaOne),depth(camera.depthIdentity),draw);
    static unsigned traced=0;if(traced++<5)std::fprintf(stderr,"[NATIVE TRAIL DRAW] owner=%08X vertices=%u original_cpu_vertices=true\n",t.owner,t.count);
    t.count=t.source=0;draw.vertices.clear();
}
void EngineDriver::billboardOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    if((site==0x82751934||site==0x82751978)&&state->decal.cpu){decalOperation(c,base,site);return;}
    if(site==0x82751934&&state->beam.owner){beamOperation(c,base,site);return;}
    if(site==0x82751934&&state->immediateEffect.cpu){immediateEffectOperation(c,base,site);return;}
    auto& s=*state;s.requireCaller(base);auto& b=s.billboard;
    const auto need=[](bool value,const char* reason){if(!value)throw Failure(reason);};
    if((site==0x82751934||site==0x82751978)&&s.trail.owner) {
        auto& t=s.trail;const bool projected=site==0x82751978;
        need(currentContext==&c&&!t.count&&c.r1.u32+0x100==t.stack&&PPC_LOAD_U32(c.r1.u32)==t.stack&&
             PPC_LOAD_U32(t.stack-8)==0x8277E518&&c.r31.u32==0x82DFEA20&&c.r29.u32==0x8215A470&&
             c.r28.u32==((t.mode>>2)&1)&&!c.r3.u32&&c.r6.u32==4&&
             c.r4.u32==(projected?21u:0u)&&c.r7.u64==(projected?0x0600000000000000ull:0x8000000000000000ull)&&
             c.r5.u32==(projected?c.r1.u32+0x90:0x82DFEAE0)&&
             (projected?((t.mode&4)&&t.matrix&&!t.projectedMatrix):!t.matrix),"Trail original transform upload differs");
        if(projected)need(PPC_LOAD_U32(0x82DFEB98)==t.projectionOwner&&PPC_LOAD_U32(t.projectionOwner+0xF0)==t.projectionIdentity&&
                          s.shadowTextures->depth(t.projectionIdentity)==t.draw.projectionDepth,"Trail projection owner changed before its original matrix upload");
        auto& constants=projected?t.draw.projectionConstants:t.draw.constants;
        for(unsigned row=0;row<4;++row)for(unsigned lane=0;lane<4;++lane)
            constants[row][lane]=std::bit_cast<float>(PPC_LOAD_U32(c.r5.u32+16*row+4*lane));
        if(projected)t.projectedMatrix=true;else t.matrix=true;
        c.r3.u64=0;c.lr=site+4;return;
    }
    need(currentContext==&c&&!PPC_LOAD_U32(0x82D0CAF8),"Billboard operation requires its native immediate context");
    using S=Graphics::ScalarState;using T=Graphics::SamplerState;
    if(site==0x8275F228) {
        const uint32_t mode=PPC_LOAD_U32(c.r3.u32+4);
        need(!b.owner&&!s.trail.owner&&!s.beam.owner&&!s.immediateEffect.cpu&&!s.decal.cpu&&c.r1.u32>=0x200&&!(c.r1.u32&15)&&
             PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0x1F0&&c.r3.u32==c.r1.u32+0x70&&
             !PPC_LOAD_U32(c.r3.u32)&&(mode==0||mode==2||mode==4||mode==6),"Billboard setup frame or mode differs");
        s.runtime.pointer(c.r31.u32,0x150,false);
        const auto definition=PPC_LOAD_U32(c.r31.u32+0x118),material=PPC_LOAD_U32(c.r31.u32+0x11C);
        s.runtime.pointer(definition,0x120,false);s.runtime.pointer(material,0xA0,false);
        const uint32_t texture=PPC_LOAD_U32(c.r31.u32+0xD0),texture2=PPC_LOAD_U32(c.r31.u32+0xD4);
        need(PPC_LOAD_U32(c.r31.u32)==0x821530EC&&(mode&4)==(PPC_LOAD_U8(definition+0x104)&4)&&
             c.r4.u32==texture&&
             (!(mode&2)?(PPC_LOAD_U8(definition+0x40)!=1&&!texture2&&!c.r5.u32):
                      (PPC_LOAD_U8(definition+0x40)==1&&texture2&&c.r5.u32==texture2)),
             "Unqualified billboard shader/texture mode");
        const uint32_t pixelSlot=(mode&4)?((mode&2)?24u:23u):(mode&2)?17u:9u;
        s.screenMaterial(base,(mode&4)?22:8);s.screenMaterial(base,pixelSlot);
        need(s.immediateDeclarationId&&PPC_LOAD_U32(0x82CD1A68)==s.immediateDeclarationId&&
             PPC_LOAD_U32(0x82DFE350)==s.immediateDeclarationId&&
             PPC_LOAD_U32(0x82CD1A6C)==PPC_LOAD_U32((mode&4)?0x82CF1FC8:0x82CF1FBC)&&
             PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32((mode&4)?((mode&2)?0x82CF1FB0:0x82CF1FA4):(mode&2)?0x82CF1F98:0x82CF1F8C),"Billboard shader/declaration association differs");
        State::Billboard next;next.cpu=&c;next.owner=c.r31.u32;next.definition=definition;next.material=material;next.mode=mode;
        next.stack=c.r1.u32;next.entry=c.r3.u32;next.row=PPC_LOAD_U32(0x82DFEB20);
        need(bool(s.immediateBuffer(base,next.row+8)),"Billboard vertex ring has no native owner");
        next.texture=c.r4.u32;s.runtime.pointer(next.texture,0x54,false);
        next.raster=PPC_LOAD_U32(next.texture);next.sampling=PPC_LOAD_U32(next.texture+0x50);
        next.draw.texture=textureRaster(next.raster);next.camera=cameraBinding().camera;
        if(mode&2) {
            next.texture2=c.r5.u32;s.runtime.pointer(next.texture2,0x54,false);
            next.raster2=PPC_LOAD_U32(next.texture2);next.draw.texture2=textureRaster(next.raster2);
        }
        auto post=s.renderState->effective();
        const auto flags=PPC_LOAD_U32(definition+0xD0),flags2=PPC_LOAD_U32(definition+0xD4);
        // Exact original scalar branches8275F22C..2F4.
        post.setScalar(S::DepthWrite,(flags>>28)&1);post.setScalar(S::DepthEnable,(~flags2)&1);
        const uint32_t blend=(flags&0x80)?0x10106:(flags&0x100)?0x10186:0x10706;
        post.setPackedBlend(0,blend);post.setScalar(S::AlphaTest,1);
        post.setScalar(S::AlphaCompare,4);post.setScalar(S::AlphaReference,PPC_LOAD_U8(definition+0x106));
        post.setSampler(0,T::Minification,1);post.setSampler(0,T::Magnification,1);
        // The inline RLWIMI at827519FC is the same field as8243BD84: mip1.
        post.setSampler(0,T::MipFilter,1);
        post.setSampler(0,T::AddressU,(next.sampling&0xF00)==0x100?0:2);
        post.setSampler(0,T::AddressV,(next.sampling&0xF000)==0x1000?0:2);
        if(mode&2) {
            post.setSampler(1,T::Minification,1);post.setSampler(1,T::Magnification,1);post.setSampler(1,T::MipFilter,1);
            post.setSampler(1,T::AddressU,post.sampler(0,T::AddressU));post.setSampler(1,T::AddressV,post.sampler(0,T::AddressV));
        }
        auto& sampler=next.draw.sampler;sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU=D3D11_TEXTURE_ADDRESS_MODE(post.sampler(0,T::AddressU)+1);
        sampler.AddressV=D3D11_TEXTURE_ADDRESS_MODE(post.sampler(0,T::AddressV)+1);
        sampler.AddressW=D3D11_TEXTURE_ADDRESS_MODE(post.sampler(0,T::AddressW)+1);
        need(!post.sampler(0,T::LodBiasBits)&&!post.sampler(0,T::MinimumMip)&&post.sampler(0,T::MaximumMip)==13&&
             post.sampler(0,T::MaximumAnisotropy)==1,"Billboard inherited sampler LOD profile differs");
        sampler.MaxLOD=13;sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
        if(mode&4)prepareImmediateProjection(base,next.draw,post,next.projectionOwner,next.projectionIdentity);
        s.backend.bindEngineTexture(0,next.draw.texture);
        if(mode&2)s.backend.bindEngineTexture(1,next.draw.texture2);else s.backend.clearEngineTexture(1);
        s.renderState->publishScreenState(post);b=std::move(next);
        c.r26.s64=int64_t(int32_t(0x82D10000));c.r30.u64=post.scalar(S::AlphaReference);c.lr=0x8275F2F8;
        std::fprintf(stderr,"[NATIVE BILLBOARD BEGIN] owner=%08X texture=%08X raster=%08X mode=%u blend=%08X depth=%u/%u alpha=%u\n",
            b.owner,b.texture,b.raster,b.mode,blend,post.scalar(S::DepthEnable),post.scalar(S::DepthWrite),post.scalar(S::AlphaReference));
        return;
    }
    need(b.owner&&b.cpu==&c&&PPC_LOAD_U32(b.stack)==b.stack+0x1F0&&PPC_LOAD_U32(0x82DFEB20)==b.row&&
         PPC_LOAD_U32(b.owner+0x118)==b.definition&&PPC_LOAD_U32(b.owner+0x11C)==b.material&&
         PPC_LOAD_U32(b.owner+0xD0)==b.texture&&PPC_LOAD_U32(b.texture)==b.raster&&
         PPC_LOAD_U32(b.texture+0x50)==b.sampling&&PPC_LOAD_U32(b.entry+4)==b.mode&&
         (!(b.mode&2)?!PPC_LOAD_U32(b.owner+0xD4):
                    (PPC_LOAD_U32(b.owner+0xD4)==b.texture2&&PPC_LOAD_U32(b.texture2)==b.raster2))&&
         cameraBinding().camera==b.camera,"Billboard CPU/resource lifetime changed");
    if(b.mode&4)need(PPC_LOAD_U32(0x82DFEB98)==b.projectionOwner&&PPC_LOAD_U32(b.projectionOwner+0xF0)==b.projectionIdentity&&
                     s.shadowTextures->depth(b.projectionIdentity)==b.draw.projectionDepth,"Billboard shadow resource lifetime changed");
    if(site==0x82751934) {
        // Preserve827518D0's prologue and original827B8328 matrix product.
        need(!b.matrix&&!b.color&&!b.count&&c.r1.u32==b.stack-0x100&&PPC_LOAD_U32(c.r1.u32)==b.stack&&
             PPC_LOAD_U32(b.stack-8)==0x8275F494&&c.r31.u32==0x82DFEA20&&c.r28.u32==((b.mode>>2)&1)&&
             !c.r3.u32&&!c.r4.u32&&c.r6.u32==4&&c.r7.u64==0x8000000000000000ull&&
             (c.r5.u32==c.r1.u32+0x50||c.r5.u32==0x82DFEAE0),"Billboard matrix upload ABI differs");
        const auto* data=s.runtime.pointer(c.r5.u32,64,false);(void)data;
        for(uint32_t row=0;row<4;++row)for(uint32_t lane=0;lane<4;++lane)
            b.draw.constants[row][lane]=std::bit_cast<float>(PPC_LOAD_U32(c.r5.u32+16*row+4*lane));
        b.matrix=true;c.r3.u64=0;c.lr=0x82751938;return;
    }
    if(site==0x82751978) {
        need((b.mode&4)&&b.matrix&&!b.projectedMatrix&&!b.color&&!b.count&&c.r1.u32==b.stack-0x100&&
             PPC_LOAD_U32(c.r1.u32)==b.stack&&PPC_LOAD_U32(b.stack-8)==0x8275F494&&c.r31.u32==0x82DFEA20&&c.r28.u32==1&&
             !c.r3.u32&&c.r4.u32==21&&c.r5.u32==c.r1.u32+0x90&&c.r6.u32==4&&c.r7.u64==0x0600000000000000ull,
             "Billboard projected matrix upload ABI differs");
        for(unsigned row=0;row<4;++row)for(unsigned lane=0;lane<4;++lane)
            b.draw.projectionConstants[row][lane]=std::bit_cast<float>(PPC_LOAD_U32(c.r5.u32+16*row+4*lane));
        b.projectedMatrix=true;c.r3.u64=0;c.lr=0x8275197C;return;
    }
    need(c.r1.u32==b.stack&&c.r31.u32==b.owner,"Billboard particle loop frame differs");
    if(site==0x8275F494) {
        need(b.matrix&&!b.color&&!b.count,"Billboard color precedes its matrix or repeats");
        for(uint32_t lane=0;lane<4;++lane)b.draw.constants[4][lane]=std::bit_cast<float>(PPC_LOAD_U32(b.stack+0x80+4*lane));
        b.color=true;c.r3.u64=b.entry;return;
    }
    if(site==0x8275F4E8) {
        need(b.matrix&&b.color&&!b.soft&&!b.count&&(!(b.mode&4)||b.projectedMatrix)&&std::isfinite(c.f1.f64),
             "Billboard extra constants have an unqualified shader consumer");
        if(b.mode&4) {
            need(float(c.f1.f64)==std::bit_cast<float>(PPC_LOAD_U32(b.definition+0xE4)),"Projected billboard c25 lost its original material value");
            b.draw.projectionConstants[4]={float(c.f1.f64),0,0,0};
        }
        // Preserve the leaf helper's original temporary stores. Only the SDK
        // constant upload is realized by the owned native draw.
        PPC_STORE_U32(b.stack-16,std::bit_cast<uint32_t>(float(c.f1.f64)));
        PPC_STORE_U32(b.stack-12,0);PPC_STORE_U32(b.stack-8,0);PPC_STORE_U32(b.stack-4,0);
        b.soft=true;c.lr=0x8275F4EC;return;
    }
    if(site==0x82751E38) {
        need(b.matrix&&b.color&&b.soft&&!b.count&&uint32_t(c.lr)==0x8275F6E4&&
             c.r3.u32==b.entry&&c.r4.u32==6&&c.r5.u32==4&&PPC_LOAD_U32(b.entry)==b.total,
             "Billboard reservation has no completed original state");
        s.immediateBuffer(base,b.row+8);const auto start=PPC_LOAD_U32(b.row+4),cursor=PPC_LOAD_U32(b.row);
        need(cursor>=start&&uint64_t(cursor-start)+128<=0x100000&&!((cursor-start)&31),"Billboard reservation exceeds its original ring");
        s.runtime.pointer(cursor,128,true);b.source=cursor;b.count=4;
        PPC_STORE_U32(b.row,cursor+128);b.total+=4;PPC_STORE_U32(b.entry,b.total);c.r3.u64=cursor;return;
    }
    need(site==0x8275F7EC,"Unknown billboard native boundary");
    if(!b.count){need(!b.matrix&&!b.color&&!b.soft,"Billboard skipped a partially prepared particle");return;}
    need(b.count==4&&c.r3.u32==b.source&&PPC_LOAD_U32(b.row)==b.source+128&&PPC_LOAD_U32(b.entry)==b.total,
         "Original billboard vertex construction changed its reservation");
    auto& draw=b.draw;draw.vertices.resize(4);
    for(uint32_t i=0;i<4;++i){std::array<float,8> values{};for(uint32_t j=0;j<8;++j)
        values[j]=std::bit_cast<float>(PPC_LOAD_U32(b.source+32*i+4*j));std::memcpy(&draw.vertices[i],values.data(),32);}
    const auto& effective=s.renderState->effective();const auto camera=cameraBinding();
    draw.viewport=camera.viewport;draw.depthEnable=effective.scalar(S::DepthEnable);draw.depthWrite=effective.scalar(S::DepthWrite);
    draw.depthCompare=effective.scalar(S::DepthCompare);draw.depthPolicy=Graphics::ShadowMeshDepthPolicy::Reference20e4Rne;
    draw.colorMask=effective.scalar(S::ColorMask0);draw.cull=effective.scalar(S::Cull);draw.stencilEnable=effective.scalar(S::StencilEnable);
    draw.fill=effective.scalar(S::Fill);draw.scissorEnable=effective.scalar(S::ScissorEnable);draw.halfPixelOffset=effective.scalar(S::HalfPixelOffset);
    draw.viewportEnable=effective.scalar(S::ViewportEnable);draw.clipPlaneEnable=effective.scalar(S::ClipPlaneEnable);draw.alphaToMask=effective.scalar(S::AlphaToMask);
    draw.multisampleMask=effective.scalar(S::MultisampleMask);draw.depthBiasBits=effective.scalar(S::DepthBias);draw.slopeBiasBits=effective.scalar(S::SlopeBias);
    draw.alphaTest=effective.scalar(S::AlphaTest);draw.alphaReference=effective.scalar(S::AlphaReference);
    // Original direct CB80 calls select the packed equation independently of A010.
    draw.blendEnable=1;draw.blendWord=effective.effectiveBlend(0);
    need(effective.scalar(S::AlphaCompare)==4&&!effective.scalar(S::TessellationMode),"Billboard alpha/tessellation profile differs");
    s.backend.requireEngineTexture(0,draw.texture);
    if(b.mode&2)s.backend.requireEngineTexture(1,draw.texture2);
    auto buffer=s.immediateBuffer(base,b.row+8);s.backend.writeBuffer(buffer,b.source-PPC_LOAD_U32(b.row+4),
        {reinterpret_cast<const uint8_t*>(draw.vertices.data()),128});
    bool alphaOne=false;const auto target=color(camera.colorIdentity,alphaOne);need(!alphaOne,"Billboard target requires unqualified alpha synthesis");
    s.backend.drawImmediate(target,depth(camera.depthIdentity),draw);++b.draws;
    if(!s.runtime.frameCaptureDirectory.empty()&&b.draws==1) {
        const auto directory=s.runtime.frameCaptureDirectory/"billboard-native";std::filesystem::create_directories(directory);
        std::ofstream vertices(directory/"vertices.bin",std::ios::binary);
        vertices.write(reinterpret_cast<const char*>(draw.vertices.data()),128);
        std::ofstream constants(directory/"constants.bin",std::ios::binary);
        constants.write(reinterpret_cast<const char*>(draw.constants.data()),sizeof(draw.constants));
    }
    {static thread_local uint32_t billboardDrawSample{};
    if(sampleHotLog(billboardDrawSample))
        std::fprintf(stderr,"[NATIVE BILLBOARD DRAW] owner=%08X particle=%08X vertices=4 total=%u original_cpu_vertices=true\n",b.owner,c.r28.u32,b.draws);}
    b.source=b.count=0;b.matrix=b.color=b.soft=b.projectedMatrix=false;draw.vertices.clear();
}
uint64_t EngineDriver::immediateDrawCount() const {return state->backend.immediateDrawCount();}
// 8277B230 transforms the source polyline to view space; 8277B040 computes
// its perpendicular widths and fills each quad. Only GPU setup and submission
// are replaced here, after those original calculations have completed.
void EngineDriver::beamOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.requireCaller(base);auto& b=s.beam;
    const auto need=[](bool value,const char* reason){if(!value)throw Failure(reason);};
    need(currentContext==&c,"Beam operation requires its original CPU context");
    using S=Graphics::ScalarState;using T=Graphics::SamplerState;
    if(site==0x8277B284) {
        // Count, hidden and opacity branches have already executed. A faded
        // item may legitimately have no points or live render resources.
        const uint32_t owner=c.r28.u32,item=c.r30.u32;
        s.runtime.pointer(owner,0xE0,false);s.runtime.pointer(item,0x90,false);
        const uint32_t pointCount=PPC_LOAD_U32(item+0x68);
        need(!b.owner&&pointCount<=25,"Beam point count exceeds its original stack array");
        const uint32_t items=PPC_LOAD_U32(owner+0xC0),itemCount=PPC_LOAD_U32(owner+0xC8);
        need(PPC_LOAD_U32(owner)==0x8215895C&&items&&itemCount&&
             uint64_t(items)+uint64_t(itemCount)*0x90<=UINT32_MAX&&item>=items&&
             (item-items)%0x90==0&&(item-items)/0x90<itemCount,
             "Beam item lost its original owner array");
        s.runtime.pointer(PPC_LOAD_U32(item+0x60),16*pointCount,false);
        return;
    }
    if(site==0x8277B328||site==0x8286D754) {
        const bool endpoint=site==0x8286D754;const uint32_t stackSize=endpoint?0x110:0x2A0,entryOffset=endpoint?0x60:0x58;
        need(!b.owner&&!s.trail.owner&&!s.billboard.owner&&!s.immediateEffect.cpu&&!s.decal.cpu&&c.r1.u32>=0x300&&!(c.r1.u32&15)&&
             PPC_LOAD_U32(c.r1.u32)==c.r1.u32+stackSize&&c.r3.u32==c.r1.u32+entryOffset&&
             !PPC_LOAD_U32(c.r3.u32)&&!PPC_LOAD_U32(c.r3.u32+4)&&!c.r5.u32,
             "Beam setup frame or original immediate mode differs");
        State::Beam next;next.cpu=&c;next.endpoint=endpoint;next.owner=endpoint?c.r31.u32:c.r28.u32;
        s.runtime.pointer(next.owner,endpoint?0x120:0xE0,false);
        next.definition=PPC_LOAD_U32(next.owner+(endpoint?0xB0:0xA4));s.runtime.pointer(next.definition,endpoint?0x20:0xA8,false);
        if(endpoint) {
            need(PPC_LOAD_U32(next.owner)==0x8217FFD8&&!(PPC_LOAD_U32(next.owner+0x10)&1)&&
                 c.r29.u32==next.owner+0x100&&c.r28.u32==next.owner+0x110,"Endpoint beam lost its original object");
            next.pointCount=2;next.points=next.owner+0x100;
        }else {
            next.item=c.r30.u32;s.runtime.pointer(next.item,0x90,false);
            const uint32_t items=PPC_LOAD_U32(next.owner+0xC0),itemCount=PPC_LOAD_U32(next.owner+0xC8);
            need(PPC_LOAD_U32(next.owner)==0x8215895C&&items&&itemCount&&
                 uint64_t(items)+uint64_t(itemCount)*0x90<=UINT32_MAX&&next.item>=items&&
                 (next.item-items)%0x90==0&&(next.item-items)/0x90<itemCount,
                 "Beam item lost its original owner array");
            next.pointCount=PPC_LOAD_U32(next.item+0x68);next.points=PPC_LOAD_U32(next.item+0x60);
            need(next.pointCount>=2&&next.pointCount<=25&&!(PPC_LOAD_U32(next.item+0x78)&1),
                 "Beam setup differs from its original point loop");
            s.runtime.pointer(next.points,16*next.pointCount,false);
        }
        next.stack=c.r1.u32;next.entry=c.r3.u32;next.row=PPC_LOAD_U32(0x82DFEB20);
        next.texture=PPC_LOAD_U32(next.owner+(endpoint?0xA0:0xA8));
        need(c.r4.u32==next.texture&&bool(s.immediateBuffer(base,next.row+8)),"Beam texture or vertex ring owner differs");
        s.screenMaterial(base,8);s.screenMaterial(base,9);
        need(s.immediateDeclarationId&&PPC_LOAD_U32(0x82DFE350)==s.immediateDeclarationId&&
             PPC_LOAD_U32(0x82CD1A68)==s.immediateDeclarationId&&
             PPC_LOAD_U32(0x82CD1A6C)==PPC_LOAD_U32(0x82CF1FBC)&&
             PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(0x82CF1F8C),"Beam shader/declaration association differs");
        if(next.texture){s.runtime.pointer(next.texture,0x54,false);
            next.raster=PPC_LOAD_U32(next.texture);next.sampling=PPC_LOAD_U32(next.texture+0x50);
            next.draw.texture=textureRaster(next.raster);}
        else next.draw.nullTexture=true;
        next.camera=cameraBinding().camera;
        if(!endpoint) {
            for(unsigned row=0;row<4;++row)for(unsigned lane=0;lane<4;++lane)
                next.draw.constants[row][lane]=std::bit_cast<float>(PPC_LOAD_U32(0x82DFEAA0+16*row+4*lane));
            for(unsigned lane=0;lane<3;++lane)
                next.draw.constants[4][lane]=std::bit_cast<float>(PPC_LOAD_U32(next.definition+0x74+4*lane));
            next.draw.constants[4][3]=float(c.f31.f64);next.matrix=next.color=true;
            // Preserve the original temporary color stores skipped with the SDK.
            for(unsigned lane=0;lane<4;++lane)PPC_STORE_U32(next.stack+0x60+4*lane,std::bit_cast<uint32_t>(next.draw.constants[4][lane]));
        }
        auto post=s.renderState->effective();
        post.setScalar(S::DepthWrite,0);post.setPackedBlend(0,0x10106);
        post.setScalar(S::AlphaTest,1);post.setScalar(S::AlphaCompare,4);post.setScalar(S::AlphaReference,endpoint?1:8);
        if(next.texture) {
            post.setSampler(0,T::Minification,1);post.setSampler(0,T::Magnification,1);post.setSampler(0,T::MipFilter,1);
            post.setSampler(0,T::AddressU,(next.sampling&0xF00)==0x100?0:2);
            post.setSampler(0,T::AddressV,(next.sampling&0xF000)==0x1000?0:2);
            auto& sampler=next.draw.sampler;sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            sampler.AddressU=D3D11_TEXTURE_ADDRESS_MODE(post.sampler(0,T::AddressU)+1);
            sampler.AddressV=D3D11_TEXTURE_ADDRESS_MODE(post.sampler(0,T::AddressV)+1);
            sampler.AddressW=D3D11_TEXTURE_ADDRESS_MODE(post.sampler(0,T::AddressW)+1);
            need(!post.sampler(0,T::LodBiasBits)&&!post.sampler(0,T::MinimumMip)&&post.sampler(0,T::MaximumMip)==13&&
                 post.sampler(0,T::MaximumAnisotropy)==1,"Beam inherited sampler LOD profile differs");
            sampler.MaxLOD=13;sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
            s.backend.bindEngineTexture(0,next.draw.texture);
        }else s.backend.clearEngineTexture(0);
        s.backend.clearEngineTexture(1);
        s.renderState->publishScreenState(post);b=std::move(next);
        if(endpoint)c.r30.s64=int64_t(int32_t(0x82D10000));
        c.lr=endpoint?0x8286D7A0:0x8277B3EC;return;
    }
    need(b.owner&&b.cpu==&c&&PPC_LOAD_U32(b.stack)==b.stack+(b.endpoint?0x110:0x2A0)&&PPC_LOAD_U32(0x82DFEB20)==b.row&&
         PPC_LOAD_U32(b.owner+(b.endpoint?0xB0:0xA4))==b.definition&&PPC_LOAD_U32(b.owner+(b.endpoint?0xA0:0xA8))==b.texture&&
         (b.endpoint||(PPC_LOAD_U32(b.item+0x60)==b.points&&PPC_LOAD_U32(b.item+0x68)==b.pointCount))&&
         (!b.texture||(PPC_LOAD_U32(b.texture)==b.raster&&PPC_LOAD_U32(b.texture+0x50)==b.sampling))&&
         !PPC_LOAD_U32(b.entry+4)&&cameraBinding().camera==b.camera,"Beam CPU/resource lifetime changed");
    if(site==0x8286D80C) {
        need(b.endpoint&&!b.color&&!b.matrix&&!b.source&&c.r1.u32==b.stack&&c.r31.u32==b.owner,
             "Endpoint beam color has no original owner frame");
        for(unsigned lane=0;lane<4;++lane)b.draw.constants[4][lane]=std::bit_cast<float>(PPC_LOAD_U32(b.stack+0x80+4*lane));
        b.color=true;c.r3.u64=b.entry;c.r4.s64=int64_t(int32_t(0x8215A470));c.r5.u64=0;return;
    }
    if(site==0x82751934) {
        need(b.endpoint&&b.color&&!b.matrix&&!b.source&&c.r1.u32+0x100==b.stack&&PPC_LOAD_U32(c.r1.u32)==b.stack&&
             PPC_LOAD_U32(b.stack-8)==0x8286D858&&c.r31.u32==0x82DFEA20&&!c.r28.u32&&
             !c.r3.u32&&!c.r4.u32&&c.r5.u32==0x82DFEAE0&&c.r6.u32==4&&c.r7.u64==0x8000000000000000ull,
             "Endpoint beam matrix upload ABI differs");
        for(unsigned row=0;row<4;++row)for(unsigned lane=0;lane<4;++lane)
            b.draw.constants[row][lane]=std::bit_cast<float>(PPC_LOAD_U32(c.r5.u32+16*row+4*lane));
        b.matrix=true;c.r3.u64=0;c.lr=0x82751938;return;
    }
    if(site==0x82751DA0) {
        need(!b.source&&c.r1.u32==b.stack&&c.r3.u32==b.entry&&
             (b.endpoint?(uint32_t(c.lr)==0x8286D9B4&&c.r31.u32==b.owner):
                         (uint32_t(c.lr)==0x8277B440&&c.r28.u32==b.owner&&c.r30.u32==b.item&&c.r31.u32==b.draws))&&
             b.matrix&&b.color&&b.draws==b.pointCount-1&&
             PPC_LOAD_U32(b.entry)==4*b.draws,"Beam cleanup precedes its original segment-loop completion");
        b={};return;
    }
    need(b.matrix&&b.color&&b.draws<b.pointCount-1&&
         (b.endpoint?(c.r1.u32==b.stack&&c.r31.u32==b.owner&&c.r29.u32==b.owner+0x100&&c.r28.u32==b.owner+0x110):
                     (c.r1.u32+0xA0==b.stack&&PPC_LOAD_U32(c.r1.u32)==b.stack&&PPC_LOAD_U32(b.stack-8)==0x8277B424&&
                      c.r31.u32==b.stack+0xC0+16*b.draws&&c.r30.u32==b.stack+0xE0+16*b.draws)),
         "Beam segment helper frame differs");
    if(site==0x82751E38) {
        need(!b.source&&uint32_t(c.lr)==(b.endpoint?0x8286D868:0x8277B124)&&c.r3.u32==b.entry&&c.r4.u32==6&&c.r5.u32==4&&
             PPC_LOAD_U32(b.entry)==4*b.draws,"Beam reservation has no completed original segment");
        s.immediateBuffer(base,b.row+8);const uint32_t start=PPC_LOAD_U32(b.row+4),cursor=PPC_LOAD_U32(b.row);
        need(cursor>=start&&uint64_t(cursor-start)+128<=0x100000&&!((cursor-start)&31),"Beam reservation exceeds its original ring");
        s.runtime.pointer(cursor,128,true);b.source=cursor;
        PPC_STORE_U32(b.row,cursor+128);PPC_STORE_U32(b.entry,4*(b.draws+1));c.r3.u64=cursor;return;
    }
    need(site==(b.endpoint?0x8286D9AC:0x8277B20C)&&b.source&&c.r3.u32==b.source&&c.r11.u32==b.source+96&&
         PPC_LOAD_U32(b.row)==b.source+128&&PPC_LOAD_U32(b.entry)==4*(b.draws+1),
         "Beam CPU vertex construction changed its reservation");
    auto& draw=b.draw;draw.vertices.resize(4);
    for(unsigned i=0;i<4;++i){std::array<uint32_t,8> words{};for(unsigned lane=0;lane<8;++lane)
        words[lane]=PPC_LOAD_U32(b.source+32*i+4*lane);std::memcpy(&draw.vertices[i],words.data(),32);}
    const auto& effective=s.renderState->effective();const auto camera=cameraBinding();
    draw.viewport=camera.viewport;draw.depthEnable=effective.scalar(S::DepthEnable);draw.depthWrite=effective.scalar(S::DepthWrite);
    draw.depthCompare=effective.scalar(S::DepthCompare);draw.depthPolicy=Graphics::ShadowMeshDepthPolicy::Reference20e4Rne;
    draw.colorMask=effective.scalar(S::ColorMask0);draw.cull=effective.scalar(S::Cull);draw.stencilEnable=effective.scalar(S::StencilEnable);
    draw.fill=effective.scalar(S::Fill);draw.scissorEnable=effective.scalar(S::ScissorEnable);draw.halfPixelOffset=effective.scalar(S::HalfPixelOffset);
    draw.viewportEnable=effective.scalar(S::ViewportEnable);draw.clipPlaneEnable=effective.scalar(S::ClipPlaneEnable);draw.alphaToMask=effective.scalar(S::AlphaToMask);
    draw.multisampleMask=effective.scalar(S::MultisampleMask);draw.depthBiasBits=effective.scalar(S::DepthBias);draw.slopeBiasBits=effective.scalar(S::SlopeBias);
    draw.alphaTest=effective.scalar(S::AlphaTest);draw.alphaReference=effective.scalar(S::AlphaReference);
    draw.blendEnable=1;draw.blendWord=effective.effectiveBlend(0);
    need(effective.scalar(S::AlphaCompare)==4&&!effective.scalar(S::TessellationMode),"Beam alpha/tessellation profile differs");
    s.backend.requireEngineTexture(0,draw.texture);
    auto buffer=s.immediateBuffer(base,b.row+8);s.backend.writeBuffer(buffer,b.source-PPC_LOAD_U32(b.row+4),
        {reinterpret_cast<const uint8_t*>(draw.vertices.data()),128});
    bool alphaOne=false;const auto target=color(camera.colorIdentity,alphaOne);need(!alphaOne,"Beam target requires unqualified alpha synthesis");
    s.backend.drawImmediate(target,depth(camera.depthIdentity),draw);++b.draws;
    {static thread_local uint32_t beamDrawSample{};if(sampleHotLog(beamDrawSample))
        std::fprintf(stderr,"[NATIVE BEAM DRAW] owner=%08X item=%08X segment=%u endpoint=%u vertices=4 original_cpu_vertices=true\n",b.owner,b.item,b.draws,b.endpoint);}
    b.source=0;draw.vertices.clear();
}
void EngineDriver::submitImmediateVertices(uint8_t* base,Graphics::ImmediateDraw& draw,uint32_t row,uint32_t source,uint32_t count) {
    auto& s=*state;using S=Graphics::ScalarState;
    draw.vertices.resize(count);
    for(uint32_t i=0;i<count;++i){std::array<uint32_t,8> words{};for(unsigned lane=0;lane<8;++lane)
        words[lane]=PPC_LOAD_U32(source+32*i+4*lane);std::memcpy(&draw.vertices[i],words.data(),32);}
    const auto& effective=s.renderState->effective();const auto camera=cameraBinding();
    draw.viewport=camera.viewport;draw.depthEnable=effective.scalar(S::DepthEnable);draw.depthWrite=effective.scalar(S::DepthWrite);
    draw.depthCompare=effective.scalar(S::DepthCompare);draw.depthPolicy=Graphics::ShadowMeshDepthPolicy::Reference20e4Rne;
    draw.colorMask=effective.scalar(S::ColorMask0);draw.cull=effective.scalar(S::Cull);draw.stencilEnable=effective.scalar(S::StencilEnable);
    draw.fill=effective.scalar(S::Fill);draw.scissorEnable=effective.scalar(S::ScissorEnable);draw.halfPixelOffset=effective.scalar(S::HalfPixelOffset);
    draw.viewportEnable=effective.scalar(S::ViewportEnable);draw.clipPlaneEnable=effective.scalar(S::ClipPlaneEnable);draw.alphaToMask=effective.scalar(S::AlphaToMask);
    draw.multisampleMask=effective.scalar(S::MultisampleMask);draw.depthBiasBits=effective.scalar(S::DepthBias);draw.slopeBiasBits=effective.scalar(S::SlopeBias);
    draw.alphaTest=effective.scalar(S::AlphaTest);draw.alphaReference=effective.scalar(S::AlphaReference);
    draw.blendEnable=1;draw.blendWord=effective.effectiveBlend(0);
    if((draw.alphaTest&&effective.scalar(S::AlphaCompare)!=4)||effective.scalar(S::TessellationMode))
        throw Failure("Immediate effect alpha/tessellation profile differs");
    s.backend.requireEngineTexture(0,draw.texture);
    auto buffer=s.immediateBuffer(base,row+8);s.backend.writeBuffer(buffer,source-PPC_LOAD_U32(row+4),
        {reinterpret_cast<const uint8_t*>(draw.vertices.data()),32*count});
    bool alphaOne=false;const auto target=color(camera.colorIdentity,alphaOne);
    if(alphaOne)throw Failure("Immediate effect target requires unqualified alpha synthesis");
    s.backend.drawImmediate(target,depth(camera.depthIdentity),draw);draw.vertices.clear();
}
void EngineDriver::immediateEffectOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.requireCaller(base);auto& e=s.immediateEffect;
    const auto need=[](bool value,const char* reason){if(!value)throw Failure(reason);};
    using S=Graphics::ScalarState;using T=Graphics::SamplerState;
    need(currentContext==&c,"Immediate effect requires its original CPU context");
    if(site==0x823CB2A0) {
        s.runtime.pointer(c.r3.u32,0x2810,false);const uint32_t end=PPC_LOAD_U32(c.r3.u32+0x280C);
        need(uint64_t(c.r3.u32)+0x280C<=UINT32_MAX&&end>=c.r3.u32+0xC&&end<=c.r3.u32+0x280C&&
             (end-c.r3.u32-0xC)%0x28==0,"Immediate box queue extent differs from its original fixed array");
        return;
    }
    const auto bindTexture=[&](State::ImmediateEffect& effect,uint32_t texture,Graphics::EngineState& post) {
        if(!texture) {
            need(effect.kind>=1&&effect.kind<=3,"Unqualified null immediate texture consumer");
            effect.texture=effect.raster=effect.sampling=0;effect.draw.texture.reset();effect.draw.nullTexture=true;
            s.backend.clearEngineTexture(0);s.backend.clearEngineTexture(1);return;
        }
        s.runtime.pointer(texture,0x54,false);effect.texture=texture;effect.raster=PPC_LOAD_U32(texture);
        effect.draw.nullTexture=false;
        effect.sampling=PPC_LOAD_U32(texture+0x50);effect.draw.texture=textureRaster(effect.raster);
        post.setSampler(0,T::Minification,1);post.setSampler(0,T::Magnification,1);post.setSampler(0,T::MipFilter,1);
        post.setSampler(0,T::AddressU,(effect.sampling&0xF00)==0x100?0:2);
        post.setSampler(0,T::AddressV,(effect.sampling&0xF000)==0x1000?0:2);
        need(!post.sampler(0,T::LodBiasBits)&&!post.sampler(0,T::MinimumMip)&&post.sampler(0,T::MaximumMip)==13&&
             post.sampler(0,T::MaximumAnisotropy)==1,"Immediate effect inherited sampler LOD profile differs");
        auto& sampler=effect.draw.sampler;sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU=D3D11_TEXTURE_ADDRESS_MODE(post.sampler(0,T::AddressU)+1);
        sampler.AddressV=D3D11_TEXTURE_ADDRESS_MODE(post.sampler(0,T::AddressV)+1);
        sampler.AddressW=D3D11_TEXTURE_ADDRESS_MODE(post.sampler(0,T::AddressW)+1);
        sampler.MaxLOD=13;sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
        s.backend.bindEngineTexture(0,effect.draw.texture);s.backend.clearEngineTexture(1);
    };
    if(site==0x823CB2D8||site==0x82776534||site==0x82779A94) {
        need(!e.cpu&&!s.beam.owner&&!s.trail.owner&&!s.billboard.owner&&!s.decal.cpu,"Immediate effect overlaps an original batch");
        State::ImmediateEffect next;next.cpu=&c;next.stack=c.r1.u32;next.row=PPC_LOAD_U32(0x82DFEB20);next.camera=cameraBinding().camera;
        next.kind=site==0x823CB2D8?1:site==0x82776534?2:3;
        next.frame=next.kind==1?0x310:next.kind==2?0x1D0:0x680;
        next.entry=next.stack+(next.kind==1?0xA0:next.kind==2?0x50:0x88);
        next.owner=next.kind==1?c.r26.u32:next.kind==2?c.r31.u32:c.r27.u32;
        need(!(next.stack&15)&&next.stack>=0x800&&PPC_LOAD_U32(next.stack)==next.stack+next.frame&&
             c.r3.u32==next.entry&&!PPC_LOAD_U32(next.entry)&&!PPC_LOAD_U32(next.entry+4)&&
             bool(s.immediateBuffer(base,next.row+8)),"Immediate effect setup frame, mode or ring differs");
        s.screenMaterial(base,8);s.screenMaterial(base,9);
        need(s.immediateDeclarationId&&PPC_LOAD_U32(0x82DFE350)==s.immediateDeclarationId&&
             PPC_LOAD_U32(0x82CD1A68)==s.immediateDeclarationId&&
             PPC_LOAD_U32(0x82CD1A6C)==PPC_LOAD_U32(0x82CF1FBC)&&
             PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(0x82CF1F8C),"Immediate effect shader/declaration association differs");
        auto post=s.renderState->effective();
        if(next.kind==1) {
            s.runtime.pointer(next.owner,0x2810,false);next.end=PPC_LOAD_U32(next.owner+0x280C);
            need(next.end>next.owner+0xC&&next.end<=next.owner+0x280C&&(next.end-next.owner-0xC)%0x28==0&&
                 c.r30.u32==next.owner+0xC,"Immediate box setup lost its original queue");
            next.expected=6*((next.end-next.owner-0xC)/0x28);post.setPackedBlend(0,0x10001);
            // BeginImmediate clears stage zero. The queue can keep its initial
            // null texture without taking the original change-texture branch.
            bindTexture(next,0,post);
            c.r28.s64=int64_t(int32_t(0x82D10000));c.lr=0x823CB2F0;
        }else if(next.kind==2) {
            s.runtime.pointer(next.owner,0xA8,false);next.definition=PPC_LOAD_U32(next.owner+0xA0);s.runtime.pointer(next.definition,0x50,false);
            need(PPC_LOAD_U32(next.owner)==0x821586A8&&c.r4.u32==PPC_LOAD_U32(next.owner+0xA4)&&!c.r5.u32,
                 "Eight-vertex effect lost its original owner or texture");
            next.expected=1;bindTexture(next,c.r4.u32,post);
            for(unsigned lane=0;lane<3;++lane)next.draw.constants[4][lane]=std::bit_cast<float>(PPC_LOAD_U32(next.definition+0x20+4*lane));
            next.draw.constants[4][3]=float(c.f20.f64);next.color=true;
            for(unsigned lane=0;lane<4;++lane)PPC_STORE_U32(next.stack+0xB0+4*lane,std::bit_cast<uint32_t>(next.draw.constants[4][lane]));
            const auto blend=int8_t(PPC_LOAD_U8(next.definition+0x12));
            post.setPackedBlend(0,blend==0?0x10106:blend==1?0x10706:blend==2?0x10186:0x10001);
            post.setScalar(S::DepthWrite,0);post.setScalar(S::AlphaTest,1);post.setScalar(S::AlphaCompare,4);post.setScalar(S::AlphaReference,1);
            c.r30.s64=int64_t(int32_t(0x82D10000));c.lr=0x827765D4;
        }else {
            s.runtime.pointer(next.owner,0x1C0,false);next.definition=PPC_LOAD_U32(next.owner+0xB4);s.runtime.pointer(next.definition,0x5C,false);
            next.item=c.r21.u32;next.points=c.r20.u32;next.pointCount=c.r22.u32;
            s.runtime.pointer(next.item,0x1C,false);
            next.end=PPC_LOAD_U32(next.owner+0x1BC);s.runtime.pointer(next.end,16,false);
            next.segment=PPC_LOAD_U32(next.stack+0x64);
            const uint32_t collectionCount=PPC_LOAD_U32(next.end+8),list=PPC_LOAD_U32(next.end+0xC),count=PPC_LOAD_U32(next.item+0xC);
            need(PPC_LOAD_U32(next.owner)==0x821588E0&&int32_t(collectionCount)>0&&next.segment%4==0&&
                 next.segment/4<collectionCount&&uint64_t(list)+next.segment+4<=UINT32_MAX,
                 "Seven-vertex effect lost its original collection owner");
            s.runtime.pointer(list+next.segment,4,false);
            need(PPC_LOAD_U32(list+next.segment)==next.item&&int32_t(count)>=0&&next.pointCount==std::min(count,30u)&&
                 next.pointCount<=30&&next.points==PPC_LOAD_U32(next.item+0x10)&&
                 c.r4.u32==PPC_LOAD_U32(next.owner+0xB8)&&!c.r5.u32,"Seven-vertex effect setup changed its original points or texture");
            if(next.pointCount)s.runtime.pointer(next.points,32*next.pointCount,false);
            next.matrixOwner=PPC_LOAD_U32(next.item+0x18);s.runtime.pointer(next.matrixOwner,0x80,false);
            for(uint32_t i=0;i+1<next.pointCount;++i)if(PPC_LOAD_U8(next.points+32*i+0x2F)==2)next.segments.push_back(i);
            next.expected=uint32_t(next.segments.size());bindTexture(next,c.r4.u32,post);c.lr=0x82779A98;
        }
        s.renderState->publishScreenState(post);e=std::move(next);return;
    }
    need(e.cpu==&c&&PPC_LOAD_U32(e.stack)==e.stack+e.frame&&PPC_LOAD_U32(0x82DFEB20)==e.row&&
         !PPC_LOAD_U32(e.entry+4)&&cameraBinding().camera==e.camera,"Immediate effect CPU/camera/ring lifetime changed");
    if(e.kind==1)need(PPC_LOAD_U32(e.owner+0x280C)==e.end,"Immediate box queue changed during its original loop");
    if(e.kind==2)need(PPC_LOAD_U32(e.owner+0xA0)==e.definition&&PPC_LOAD_U32(e.owner+0xA4)==e.texture,
                     "Eight-vertex effect resource lifetime changed");
    if(e.kind==3)need(PPC_LOAD_U32(e.owner+0xB4)==e.definition&&PPC_LOAD_U32(e.owner+0xB8)==e.texture&&
                     PPC_LOAD_U32(e.owner+0x1BC)==e.end&&PPC_LOAD_U32(e.stack+0x64)==e.segment&&
                     PPC_LOAD_U32(e.item+0x10)==e.points&&PPC_LOAD_U32(e.item+0x18)==e.matrixOwner,
                     "Seven-vertex effect resource lifetime changed");
    if(e.texture)need(PPC_LOAD_U32(e.texture)==e.raster&&PPC_LOAD_U32(e.texture+0x50)==e.sampling,"Immediate effect texture owner changed");
    if(site==0x82751934) {
        const uint32_t caller=e.kind==1?0x823CB304:e.kind==2?0x827765E8:0x82779AAC;
        need(!e.matrix&&!e.source&&c.r1.u32+0x100==e.stack&&PPC_LOAD_U32(c.r1.u32)==e.stack&&PPC_LOAD_U32(e.stack-8)==caller&&
             c.r31.u32==0x82DFEA20&&!c.r28.u32&&!c.r3.u32&&!c.r4.u32&&c.r6.u32==4&&c.r7.u64==0x8000000000000000ull&&
             (e.kind==3?(c.r29.u32==e.matrixOwner+0x40&&c.r5.u32==c.r1.u32+0x50):
                        (c.r29.u32==0x8215A470&&c.r5.u32==0x82DFEAE0)),"Immediate effect original matrix upload differs");
        for(unsigned row=0;row<4;++row)for(unsigned lane=0;lane<4;++lane)
            e.draw.constants[row][lane]=std::bit_cast<float>(PPC_LOAD_U32(c.r5.u32+16*row+4*lane));
        e.matrix=true;c.r3.u64=0;c.lr=0x82751938;return;
    }
    if(site==0x823CB348) {
        need(e.kind==1&&e.matrix&&!e.source&&c.r1.u32==e.stack&&c.r26.u32==e.owner&&c.r3.u32==e.entry&&!c.r5.u32&&
             e.draws%6==0&&c.r30.u32==e.owner+0xC+0x28*(e.draws/6)&&c.r31.u32==c.r30.u32+0x14&&
             c.r4.u32==PPC_LOAD_U32(c.r30.u32+0x1C),"Immediate box texture update differs from its original row");
        auto post=s.renderState->effective();bindTexture(e,c.r4.u32,post);s.renderState->publishScreenState(post);c.lr=0x823CB34C;return;
    }
    if(site==0x823CB370||site==0x82779AD4) {
        const bool box=site==0x823CB370;
        need(e.kind==(box?1u:3u)&&e.matrix&&!e.source&&c.r1.u32==e.stack&&
             (box?(e.draws%6==0&&c.r26.u32==e.owner&&c.r30.u32==e.owner+0xC+0x28*(e.draws/6)&&
                   c.r31.u32==c.r30.u32+0x14&&PPC_LOAD_U32(c.r30.u32+0x1C)==e.texture):
                  (!e.color&&c.r27.u32==e.owner&&c.r21.u32==e.item)),"Immediate effect color update precedes its original row or matrix");
        for(unsigned lane=0;lane<4;++lane)e.draw.constants[4][lane]=std::bit_cast<float>(PPC_LOAD_U32(e.stack+(box?0x260:0xA0)+4*lane));
        e.color=true;e.record=e.draws/6;
        if(!box){const auto flags=PPC_LOAD_U32(e.definition+0x3C);auto post=s.renderState->effective();
            post.setScalar(S::DepthWrite,0);post.setScalar(S::DepthEnable,(~(flags>>3))&1);
            post.setPackedBlend(0,(flags&0x10)?0x10706:0x10106);
            post.setScalar(S::AlphaTest,1);post.setScalar(S::AlphaCompare,4);post.setScalar(S::AlphaReference,8);
            s.renderState->publishScreenState(post);c.lr=0x82779B90;}
        return;
    }
    if(site==0x82751DA0) {
        const uint32_t caller=e.kind==1?0x823CBB30:e.kind==2?0x8277687C:0x82779CE0;
        need(c.r1.u32==e.stack&&c.r3.u32==e.entry&&uint32_t(c.lr)==caller&&!e.source&&e.matrix&&e.color&&
             e.draws==e.expected&&PPC_LOAD_U32(e.entry)==e.total&&
             (e.kind!=1||(c.r26.u32==e.owner&&c.r30.u32==e.end))&&
             (e.kind!=3||(c.r27.u32==e.owner&&c.r21.u32==e.item&&(e.pointCount<=1||!c.r28.u32))),
             "Immediate effect cleanup precedes its original CPU loop completion");
        e={};return;
    }
    constexpr uint32_t boxCallers[]={0x823CB584,0x823CB6CC,0x823CB7E4,0x823CB914,0x823CBA20,0x823CBB14};
    need(e.matrix&&e.color&&e.draws<e.expected,"Immediate effect reservation/completion exceeds its original loop");
    if(e.kind==1)need(c.r1.u32+0x70==e.stack&&PPC_LOAD_U32(c.r1.u32)==e.stack&&
                     PPC_LOAD_U32(e.stack-8)==boxCallers[e.draws%6]&&e.record==e.draws/6,
                     "Immediate box helper frame/order differs");
    else if(e.kind==2)need(c.r1.u32==e.stack&&c.r31.u32==((PPC_LOAD_U16(e.definition+0x10)>>5)&1),"Eight-vertex original CPU flag/frame differs");
    else {const auto segment=e.segments[e.draws];
        need(c.r1.u32+0x60==e.stack&&PPC_LOAD_U32(c.r1.u32)==e.stack&&PPC_LOAD_U32(e.stack-8)==0x82779C60&&
             c.r31.u32==e.owner+0xD0&&c.r28.u32==e.pointCount-1-segment&&c.r29.u32==e.points+0x2E+32*segment,
             "Seven-vertex original helper frame/segment order differs");}
    if(site==0x82751E38) {
        const uint32_t caller=e.kind==1?0x823CB238:e.kind==2?0x82776680:0x82778F7C,count=e.kind==1?4:e.kind==2?8:7;
        need(!e.source&&uint32_t(c.lr)==caller&&c.r3.u32==e.entry&&c.r4.u32==6&&c.r5.u32==count&&
             PPC_LOAD_U32(e.entry)==e.total&&(e.kind!=1||(c.r31.u32==e.stack+0x70&&c.r30.u32==e.stack+0x270)),
             "Immediate effect reservation differs from its original primitive/count/input");
        s.immediateBuffer(base,e.row+8);const uint32_t start=PPC_LOAD_U32(e.row+4),cursor=PPC_LOAD_U32(e.row),bytes=32*count;
        need(cursor>=start&&uint64_t(cursor-start)+bytes<=0x100000&&!((cursor-start)&31),"Immediate effect reservation exceeds its original ring");
        s.runtime.pointer(cursor,bytes,true);e.source=cursor;e.count=count;e.total+=count;
        PPC_STORE_U32(e.row,cursor+bytes);PPC_STORE_U32(e.entry,e.total);c.r3.u64=cursor;return;
    }
    const uint32_t finish=e.kind==1?0x823CB288:e.kind==2?0x82776874:0x827790D4;
    need(site==finish&&e.source&&PPC_LOAD_U32(e.row)==e.source+32*e.count&&PPC_LOAD_U32(e.entry)==e.total&&
         (e.kind==1?(c.r3.u32==e.source+128&&c.r31.u32==e.stack+0xA0&&c.r30.u32==e.stack+0x290):
          e.kind==2?(c.r3.u32==e.source+32&&c.r11.u32==e.source+256):c.r3.u32==e.source),
         "Immediate effect CPU fill did not complete its reserved vertices");
    submitImmediateVertices(base,e.draw,e.row,e.source,e.count);++e.draws;e.source=e.count=0;
    {static thread_local uint32_t immediateEffectSample{};if(sampleHotLog(immediateEffectSample))
        std::fprintf(stderr,"[NATIVE IMMEDIATE EFFECT] kind=%u owner=%08X draw=%u vertices=%u original_cpu_vertices=true\n",e.kind,e.owner,e.draws,e.kind==1?4:e.kind==2?8:7);}
}
// Original decal manager and its two virtual geometry producers. The native
// scope owns only graphics state/uploads; both intrusive list loops, matrix
// kernels, clipping decisions and all vertex stores execute as original code.
void EngineDriver::decalOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.requireCaller(base);auto& d=s.decal;
    const auto need=[](bool value,const char* reason){if(!value)throw Failure(reason);};
    using S=Graphics::ScalarState;using T=Graphics::SamplerState;
    need(currentContext==&c,"Decal scope requires its original CPU context");
    if(site==0x827640A0) {
        need(!d.cpu&&!s.trail.owner&&!s.billboard.owner&&!s.beam.owner&&!s.immediateEffect.cpu,
             "Decal manager overlaps an original immediate consumer");
        s.runtime.pointer(c.r3.u32,0x30,false);const uint32_t definition=PPC_LOAD_U32(c.r3.u32);
        if(!definition)return; // The original null-definition early return.
        s.runtime.pointer(definition,0x60,false);
        State::Decal next;next.cpu=&c;next.owner=c.r3.u32;next.definition=definition;
        next.mode=(PPC_LOAD_U16(definition+0x10)&0x20)?5:1;
        need(c.r1.u32>=0x400&&!(c.r1.u32&15),"Decal manager stack is unqualified");
        next.stack=c.r1.u32-0xA0;next.entry=next.stack+0x58;
        next.texture=PPC_LOAD_U32(next.owner+8);
        if(next.texture){s.runtime.pointer(next.texture,0x54,false);next.raster=PPC_LOAD_U32(next.texture);
            next.sampling=PPC_LOAD_U32(next.texture+0x50);next.draw.texture=textureRaster(next.raster);}
        else next.draw.nullTexture=true;
        for(unsigned list=0;list<2;++list) {
            const uint32_t sentinel=PPC_LOAD_U32(next.owner+0x20+12*list);
            need(sentinel==next.owner-(list?0xC:0x18),"Decal manager list sentinel differs from its original constructor");
            uint32_t node=PPC_LOAD_U32(next.owner+0x18+12*list);
            while(node!=sentinel) {
                need(next.nodes.size()<0x10000&&std::none_of(next.nodes.begin(),next.nodes.end(),[&](const auto& item){return item.address==node;}),
                     "Decal manager list contains a cycle or exceeds its addressable bound");
                s.runtime.pointer(node,0x80,false);const uint32_t table=PPC_LOAD_U32(node);
                need(table==0x82153174||table==0x82153240,"Decal manager has an unqualified original geometry callback");
                need(PPC_LOAD_U32(table+8)==(table==0x82153174?0x82764608:0x82766D98)&&PPC_LOAD_U32(node+0x10)==definition,
                     "Decal node callback or material differs from its manager");
                if(table==0x82153240)s.runtime.pointer(node,0x115C,false);
                const uint32_t following=PPC_LOAD_U32(node+0x30);
                next.nodes.push_back({node,table,following,list?0x8276421C:0x827641E0});node=following;
            }
        }
        d=std::move(next);return;
    }
    need(d.cpu==&c&&PPC_LOAD_U32(d.stack)==d.stack+0xA0&&PPC_LOAD_U32(d.owner)==d.definition&&
         PPC_LOAD_U32(d.owner+8)==d.texture&&(!d.texture||(PPC_LOAD_U32(d.texture)==d.raster&&PPC_LOAD_U32(d.texture+0x50)==d.sampling))&&
         ((PPC_LOAD_U16(d.definition+0x10)&0x20)?5u:1u)==d.mode,
         "Decal manager CPU, material or texture lifetime changed");
    if(site==0x82751B74) {
        need(!d.begun&&c.r1.u32+0x80==d.stack&&PPC_LOAD_U32(c.r1.u32)==d.stack&&PPC_LOAD_U32(d.stack-8)==0x827640E0&&
             c.r3.u32==d.entry&&c.r4.u32==d.stack+0x50&&c.r5.u32==d.mode&&c.r31.u32==d.owner,
             "Decal BeginImmediate differs from its original manager call");
        if(d.mode==5) {
            d.projectionOwner=PPC_LOAD_U32(0x82DFEB98);s.runtime.pointer(d.projectionOwner,0x2A0,false);
            d.projectionIdentity=PPC_LOAD_U32(d.projectionOwner+0xF0);
            const auto resource=s.shadowTextures->view(d.projectionIdentity);
            need(resource.owner==d.projectionOwner&&resource.field==0xF0&&resource.width==1024&&resource.height==1024,
                 "Projected decal depth differs from its original shadow owner");
            d.draw.projectionDepth=s.shadowTextures->depth(d.projectionIdentity);
            need(bool(d.draw.projectionDepth),"Projected decal owner has no native depth resource");
            d.draw.projected=true;
        }
        d.row=PPC_LOAD_U32(0x82DFEB20);d.camera=cameraBinding().camera;d.begun=true;return;
    }
    need(d.begun&&PPC_LOAD_U32(d.entry+4)==d.mode&&PPC_LOAD_U32(0x82DFEB20)==d.row&&
         cameraBinding().camera==d.camera,"Decal immediate declaration, camera or ring lifetime changed");
    if(d.mode==5)need(PPC_LOAD_U32(0x82DFEB98)==d.projectionOwner&&PPC_LOAD_U32(d.projectionOwner+0xF0)==d.projectionIdentity&&
                     s.shadowTextures->depth(d.projectionIdentity)==d.draw.projectionDepth,"Projected decal shadow resource lifetime changed");
    if(site==0x827640EC) {
        need(c.r1.u32==d.stack&&c.r31.u32==d.owner&&c.r3.u32==d.entry&&c.r4.u32==d.texture&&!c.r5.u32&&!d.color,
             "Decal manager texture setup differs");
        s.screenMaterial(base,d.mode==5?22:8);s.screenMaterial(base,d.mode==5?23:9);
        need(s.immediateDeclarationId&&PPC_LOAD_U32(0x82CD1A68)==s.immediateDeclarationId&&
             PPC_LOAD_U32(0x82CD1A6C)==PPC_LOAD_U32(d.mode==5?0x82CF1FC8:0x82CF1FBC)&&
             PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(d.mode==5?0x82CF1FA4:0x82CF1F8C),
             "Decal shader or declaration association differs");
        auto post=s.renderState->effective();
        if(d.texture) {
            post.setSampler(0,T::Minification,1);post.setSampler(0,T::Magnification,1);post.setSampler(0,T::MipFilter,1);
            post.setSampler(0,T::AddressU,(d.sampling&0xF00)==0x100?0:2);post.setSampler(0,T::AddressV,(d.sampling&0xF000)==0x1000?0:2);
            need(!post.sampler(0,T::LodBiasBits)&&!post.sampler(0,T::MinimumMip)&&post.sampler(0,T::MaximumMip)==13&&
                 post.sampler(0,T::MaximumAnisotropy)==1,"Decal inherited sampler LOD profile differs");
            auto& sampler=d.draw.sampler;sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            sampler.AddressU=D3D11_TEXTURE_ADDRESS_MODE(post.sampler(0,T::AddressU)+1);
            sampler.AddressV=D3D11_TEXTURE_ADDRESS_MODE(post.sampler(0,T::AddressV)+1);
            sampler.AddressW=D3D11_TEXTURE_ADDRESS_MODE(post.sampler(0,T::AddressW)+1);
            sampler.MaxLOD=13;sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
        }
        if(d.mode==5) {
            post.setSampler(2,T::Minification,0);post.setSampler(2,T::Magnification,0);post.setSampler(2,T::MipFilter,0);
            post.setSampler(2,T::AddressU,1);post.setSampler(2,T::AddressV,1);
            need(!post.sampler(2,T::LodBiasBits)&&!post.sampler(2,T::MinimumMip)&&post.sampler(2,T::MaximumMip)==13&&
                 post.sampler(2,T::MaximumAnisotropy)==1,"Projected decal inherited sampler LOD profile differs");
            auto& projection=d.draw.projectionSampler;projection.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
            projection.AddressU=projection.AddressV=D3D11_TEXTURE_ADDRESS_MIRROR;
            projection.AddressW=D3D11_TEXTURE_ADDRESS_MODE(post.sampler(2,T::AddressW)+1);
            projection.MaxLOD=13;projection.MaxAnisotropy=1;projection.ComparisonFunc=D3D11_COMPARISON_NEVER;
        }
        if(d.texture)s.backend.bindEngineTexture(0,d.draw.texture);else s.backend.clearEngineTexture(0);
        s.backend.clearEngineTexture(1);
        s.renderState->publishScreenState(post);c.lr=0x827640F0;return;
    }
    if(site==0x8276412C) {
        need(c.r1.u32==d.stack&&c.r31.u32==d.owner&&!d.color&&d.children.empty(),"Decal manager color setup differs");
        for(unsigned lane=0;lane<4;++lane)d.draw.constants[4][lane]=std::bit_cast<float>(PPC_LOAD_U32(d.stack+0x60+4*lane));
        const auto blend=PPC_LOAD_U8(d.definition+0x14),alpha=PPC_LOAD_U8(d.definition+0x15);
        auto post=s.renderState->effective();post.setScalar(S::DepthWrite,0);
        post.setPackedBlend(0,blend==0?0x10106:blend==1?0x10706:blend==2?0x10186:0x10001);
        post.setScalar(S::AlphaTest,alpha?1:0);if(alpha){post.setScalar(S::AlphaCompare,4);post.setScalar(S::AlphaReference,alpha);}
        s.renderState->publishScreenState(post);d.color=true;c.r28.u64=alpha;c.r29.u64=alpha?1:0;c.lr=0x827641AC;return;
    }
    need(d.color,"Decal geometry precedes its original material setup");
    if(site==0x82751DA0) {
        need(c.r1.u32==d.stack&&c.r3.u32==d.entry&&c.r31.u32==d.owner&&uint32_t(c.lr)==0x82764234&&
             d.children.empty()&&d.index==d.nodes.size()&&PPC_LOAD_U32(d.entry)==d.total,
             "Decal cleanup precedes the original linked-list completion");
        d={};return;
    }
    if(site==0x82764608||site==0x82766D98) {
        const bool mesh=site==0x82766D98;
        need(c.r4.u32==d.entry&&c.r5.u32==d.stack+0x54,"Decal child lost its original batch or rebuild counter");
        if(d.children.empty()) {
            need(d.index<d.nodes.size(),"Decal child exceeds the original list");const auto& node=d.nodes[d.index];
            need(c.r1.u32==d.stack&&c.r3.u32==node.address&&uint32_t(c.lr)==node.caller&&
                 PPC_LOAD_U32(node.address)==node.vtable&&PPC_LOAD_U32(node.address+0x30)==node.next&&
                 mesh==(node.vtable==0x82153240),"Decal child order, callback or node lifetime differs");
        }else {
            const auto& outer=d.children.back();
            need(!mesh&&d.children.size()==1&&outer.mesh&&!outer.matrix&&!outer.finished&&
                 c.r1.u32==outer.stack&&c.r3.u32==outer.node&&uint32_t(c.lr)==0x82766E08&&!PPC_LOAD_U32(outer.node+0x10F0),
                 "Decal quad fallback differs from the original mesh child");
        }
        need(PPC_LOAD_U32(c.r3.u32+0x10)==d.definition,"Decal child material lifetime changed");
        State::Decal::Child child;child.node=c.r3.u32;child.frame=mesh?0x1A0:0xA0;
        child.stack=c.r1.u32-child.frame;child.caller=uint32_t(c.lr);child.mesh=mesh;d.children.push_back(child);return;
    }
    need(!d.children.empty(),"Decal upload has no original geometry child");auto& child=d.children.back();
    need(PPC_LOAD_U32(child.stack)==child.stack+child.frame&&PPC_LOAD_U32(child.stack+child.frame-8)==child.caller&&
         PPC_LOAD_U32(child.node+0x10)==d.definition,"Decal geometry frame or material lifetime changed");
    if(site==0x82751934) {
        need(!child.matrix&&!child.source&&c.r1.u32+0x100==child.stack&&PPC_LOAD_U32(c.r1.u32)==child.stack&&
             PPC_LOAD_U32(child.stack-8)==(child.mesh?0x82766E8C:0x8276465C)&&c.r29.u32==child.node+0x40&&
             c.r31.u32==0x82DFEA20&&c.r28.u32==((d.mode>>2)&1)&&!c.r3.u32&&!c.r4.u32&&
             c.r5.u32==c.r1.u32+0x50&&c.r6.u32==4&&c.r7.u64==0x8000000000000000ull,
             "Decal transform upload differs from the original model-matrix helper");
        for(unsigned row=0;row<4;++row)for(unsigned lane=0;lane<4;++lane)
            d.draw.constants[row][lane]=std::bit_cast<float>(PPC_LOAD_U32(c.r5.u32+16*row+4*lane));
        child.matrix=true;c.r3.u64=0;c.lr=0x82751938;return;
    }
    if(site==0x82751978) {
        need(d.mode==5&&child.matrix&&!child.projectedMatrix&&!child.constant&&!child.source&&c.r1.u32+0x100==child.stack&&
             PPC_LOAD_U32(c.r1.u32)==child.stack&&PPC_LOAD_U32(child.stack-8)==(child.mesh?0x82766E8C:0x8276465C)&&
             c.r29.u32==child.node+0x40&&c.r31.u32==0x82DFEA20&&c.r28.u32==1&&!c.r3.u32&&c.r4.u32==21&&
             c.r5.u32==c.r1.u32+0x90&&c.r6.u32==4&&c.r7.u64==0x0600000000000000ull,
             "Projected decal transform differs from the original shadow matrix helper");
        for(unsigned row=0;row<4;++row)for(unsigned lane=0;lane<4;++lane)
            d.draw.projectionConstants[row][lane]=std::bit_cast<float>(PPC_LOAD_U32(c.r5.u32+16*row+4*lane));
        child.projectedMatrix=true;c.r3.u64=0;c.lr=0x8275197C;return;
    }
    if(site==0x827646A0||site==0x827646E0||site==0x82766ECC||site==0x82766F08) {
        const bool mesh=site==0x82766ECC||site==0x82766F08,projected=site==0x827646A0||site==0x82766ECC;
        need(child.mesh==mesh&&projected==(d.mode==5)&&child.matrix&&!child.constant&&!child.source&&
             c.r1.u32==child.stack&&(mesh?c.r28.u32:c.r31.u32)==child.node,"Decal c25 upload differs from its original CPU branch");
        const uint32_t constants=child.stack+(mesh?(projected?0x60:0x70):(projected?0x50:0x60));
        need(PPC_LOAD_U32(constants)==PPC_LOAD_U32(projected?d.definition+0x48:0x821DD110)&&
             !PPC_LOAD_U32(constants+4)&&!PPC_LOAD_U32(constants+8)&&!PPC_LOAD_U32(constants+12),
             "Decal c25 differs from the original material/default constant");
        need(!projected||child.projectedMatrix,"Projected decal c25 precedes its original projection matrix");
        for(unsigned lane=0;lane<4;++lane)d.draw.projectionConstants[4][lane]=std::bit_cast<float>(PPC_LOAD_U32(constants+4*lane));
        child.constant=true;
        if(mesh){c.r3.u64=c.r31.u64;c.r4.u64=child.stack+0xD0;c.r24.u64=0;}
        else {c.r3.u64=d.entry;c.r4.u64=6;c.r5.u64=4;}
        return;
    }
    if(site==0x82751E38) {
        need(child.matrix&&child.constant&&!child.source&&!child.finished&&c.r1.u32==child.stack&&c.r3.u32==d.entry&&
             uint32_t(c.lr)==(child.mesh?0x82766FDC:0x82764738)&&c.r4.u32==(child.mesh?4u:6u)&&
             PPC_LOAD_U32(d.entry)==d.total,"Decal reservation differs from its original primitive or lifetime");
        uint32_t count=4;
        if(child.mesh) {
            const uint32_t input=PPC_LOAD_U32(child.node+0x10F0);
            need(input&&input<=129&&input%3==0&&c.r28.u32==child.node&&c.r29.u32==input/3,
                 "Decal mesh exceeds its original embedded vertex array");
            child.triangles=input/3;count=0;
            for(uint32_t i=0;i<child.triangles;++i) {
                const uint32_t record=child.node+0xE4+0x60*i,visibility=PPC_LOAD_U32(record+4),actor=PPC_LOAD_U32(record);
                if(visibility){s.runtime.pointer(visibility,13,false);if(!PPC_LOAD_U8(visibility+12))continue;}
                // The original count pass requires an actor. Its fill pass
                // assumes one for every visible triangle; reject before it
                // could write past the counted reservation.
                need(actor!=0,"Decal mesh visible triangle lacks its original actor");s.runtime.pointer(actor,3,false);
                if(PPC_LOAD_U8(actor+2)&4)count+=3;
            }
            need(count&&c.r5.u32==count&&c.r9.u32==count/3,"Decal triangle reservation differs from the original visibility count");
        }else need(c.r31.u32==child.node&&c.r30.u32==d.entry&&c.r5.u32==4,"Decal quad reservation differs");
        s.immediateBuffer(base,d.row+8);const uint32_t start=PPC_LOAD_U32(d.row+4),cursor=PPC_LOAD_U32(d.row),bytes=32*count;
        need(cursor>=start&&uint64_t(cursor-start)+bytes<=0x100000&&!((cursor-start)&31),"Decal reservation exceeds its original immediate ring");
        s.runtime.pointer(cursor,bytes,true);child.source=cursor;child.count=count;d.total+=count;d.draw.primitive=child.mesh?4:6;
        PPC_STORE_U32(d.row,cursor+bytes);PPC_STORE_U32(d.entry,d.total);c.r3.u64=cursor;return;
    }
    need((child.mesh?(site==0x82766E08||site==0x82766E3C||site==0x82767210):site==0x82764810)&&c.r1.u32==child.stack,
         "Decal completion site or original child frame differs");
    if(child.source) {
        need(PPC_LOAD_U32(d.row)==child.source+32*child.count&&PPC_LOAD_U32(d.entry)==d.total&&
             (child.mesh?(c.r28.u32==child.node&&c.r30.u32==child.source+32*child.count&&!c.r27.u32&&
                           c.r31.u32==child.node+0xF8+0x60*child.triangles):
                          (c.r31.u32==child.node&&c.r3.u32==child.source&&c.r11.u32==child.source+96)),
             "Decal original CPU fill did not complete its reservation");
        submitImmediateVertices(base,d.draw,d.row,child.source,child.count);++d.draws;
        {static thread_local uint32_t sample{};if(sampleHotLog(sample))std::fprintf(stderr,
            "[NATIVE DECAL] owner=%08X node=%08X mode=%u primitive=%u vertices=%u original_cpu_vertices=true\n",
            d.owner,child.node,d.mode,d.draw.primitive,child.count);}
    }
    d.children.pop_back();if(d.children.empty())++d.index;else d.children.back().finished=true;
}
// Bounded native endpoints for 82773D40. Its branch decisions, helper stack,
// vector math and six rectangle stores remain in the original CPU program.
void EngineDriver::postFilterOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    if(state->distortion.cpu&&site>=0x82771E58&&site<=0x82771F98) {
        distortionOperation(c,base,site);return;
    }
    auto& s=*state;s.requireCaller(base);auto& p=s.postFilter;
    auto need=[](bool ok,const char* why){if(!ok)throw Failure(why);};
    need(currentContext==&c,"Post filter CPU owner differs");
    const uint32_t sp=c.r1.u32;
    auto frame=[&](uint32_t bytes,uint32_t caller) {
        need(sp>=0x300&&!(sp&15)&&PPC_LOAD_U32(sp)==sp+bytes&&PPC_LOAD_U32(sp+bytes-8)==caller,
             "Post filter original frame/caller differs");
    };
    // Original direct SDK setters write a console device shadow, not the
    // native effective-state owner. Replace only these proven post call sites.
    struct ScalarEndpoint {uint32_t site,id,value;};
    constexpr ScalarEndpoint scalarEndpoints[]={
        {0x82773D94,0xD4,15},{0x82773DA0,0xCC,0},{0x82773DAC,0xD0,0},
        {0x82773DCC,0x144,1},{0x82773DD8,0x38,0},{0x82773DE4,0x28,0},{0x82773DF0,0x60,0},
        {0x827741FC,0x144,1},{0x82774208,0x3C,0},{0x82774214,0x28,1}};
    for(const auto& endpoint:scalarEndpoints)if(site==endpoint.site) {
        frame(0x120,0x8276E1C4);
        need(!c.r3.u32&&c.r4.u32==endpoint.value&&c.r31.u32==0x82D10000&&c.r25.u32<=3&&c.r24.u32<=255&&
             s.camera&&s.camera->camera==PPC_LOAD_U32(0x82E3DD60)&&PPC_LOAD_U32(0x82D0CB1C)==1,
             "Post scalar endpoint arguments/frame differ");
        if(site<0x82773E00)need(!p.cpu,"Post initial scalar endpoint overlaps an active scope");
        else need(p.cpu==&c&&p.stack==sp&&p.camera==s.camera->camera&&p.flags==c.r25.u32&&p.mode==c.r24.u32&&
                  !p.helper&&!p.locked&&!p.lease&&p.target==p.main&&p.draws==((p.flags&1)?3u:4u)&&p.copies==p.draws,
                  "Post final scalar endpoint precedes completed original draws");
        directScalar(base,endpoint.id,c.r4.u32);c.lr=site+4;return;
    }
    if(site==0x82773E34) {
        frame(0x120,0x8276E1C4);
        need(!p.cpu&&c.r25.u32<=3&&c.r24.u32<=255&&!s.im2d.active&&!s.directSprite.active&&!s.coronaQueries.active,
             "Post filter entered with overlapping scope or unsupported arguments");
        const auto camera=cameraBinding();
        need(camera.camera==PPC_LOAD_U32(0x82E3DD60)&&camera.viewport[2]==1280&&camera.viewport[3]==720&&
             PPC_LOAD_U32(0x82D0CB1C)==1&&PPC_LOAD_U32(0x82D0CF5C)==camera.colorIdentity,
             "Post filter lost its original full-size active camera");
        State::PostFilter next;next.cpu=&c;next.stack=sp;next.camera=camera.camera;next.mainId=next.targetId=camera.colorIdentity;
        next.flags=c.r25.u32;next.mode=c.r24.u32;next.stackCount=PPC_LOAD_U32(0x82CF1F88);
        need(next.stackCount==0,"Post filter entered with a retained target stack");
        s.runtime.pointer(PPC_LOAD_U32(0x82CF1F80),8,true);
        next.main=next.target=s.cameraColor(camera);next.depth=s.cameraDepth(camera);next.draw.viewport=camera.viewport;
        s.backend.requireSelectedTargets({next.main,nullptr,nullptr,nullptr},next.depth);
        s.viewportSurfaces->copyColor(c,base,site);next.copies=1;p=std::move(next);return;
    }
    if(site==0x82774218&&!p.cpu) {
        frame(0x120,0x8276E1C4);
        need(std::bit_cast<float>(PPC_LOAD_U32(c.r23.u32+12))<std::bit_cast<float>(PPC_LOAD_U32(0x821DD354)),
             "Post filter exited without its qualified alpha early return");return;
    }
    need(p.cpu==&c&&p.camera==PPC_LOAD_U32(0x82E3DD60)&&s.camera&&s.camera->camera==p.camera&&
         s.camera->colorIdentity==p.mainId&&PPC_LOAD_U32(0x82D0CB1C)==1,"Post filter lost its camera or CPU scope");
    const bool push=site>=0x82771E58&&site<=0x82771EA4;
    const bool pop=site>=0x82771F58&&site<=0x82771F98;
    const bool quad=site==0x82773CF4||site==0x82773D24;
    if(push||pop) {
        const uint32_t caller=PPC_LOAD_U32(sp+(push?0x88:0x98));
        frame(push?0x90:0xA0,caller);
        need(sp+(push?0x90:0xA0)==p.stack&&
             (push?(caller==0x82773EBC||caller==0x82773F5C):(caller==0x82773F50||caller==0x827740D0)),
             "Post target helper caller differs");
        const bool first=caller==0x82773EBC||caller==0x82773F50;
        const uint32_t aux=PPC_LOAD_U32(first?0x82DFEB7C:0x82DFEB80);
        if(site==0x82771E58) {
            need(!p.helper&&!p.lease&&!c.r3.u32&&!c.r4.u32&&!c.r29.u32&&c.r30.u32==aux&&
                 p.target==p.main&&p.copies==(first?1u:2u)&&p.draws==(first?0u:1u)&&PPC_LOAD_U32(0x82CF1F88)==p.stackCount,
                 "Post target retain transition differs");
            s.backend.requireSelectedTargets({p.main,nullptr,nullptr,nullptr},p.depth);
            p.lease=p.main;p.helper=sp;p.helperStep=1;c.r3.u64=p.mainId;c.lr=site+4;return;
        }
        if(site==0x82771F58) {
            need(!p.helper&&p.lease==p.main&&c.r29.u32==aux&&c.r30.u32==p.mainId&&
                 p.targetId==aux&&PPC_LOAD_U32(0x82CF1F88)==p.stackCount&&
                 p.draws==(first?1u:((p.flags&1)?2u:3u))&&p.copies==(first?2u:((p.flags&1)?3u:4u)),
                 "Post target original pop differs");
            p.helper=sp;p.helperStep=1;
        }
        need(p.helper==sp&&p.lease==p.main,"Post target temporary lease differs");
        if(site==0x82771E74||site==0x82771F58) {
            need(p.helperStep==1&&!c.r3.u32&&!c.r4.u32&&c.r5.u32==(push?aux:p.mainId)&&
                 PPC_LOAD_U32(0x82D0CF5C)==p.targetId,"Post target bind arguments differ");
            s.backend.requireSelectedTargets({p.target,nullptr,nullptr,nullptr},push?p.depth:nullptr);
            const auto target=push?s.viewportSurfaces->backing(aux):p.main;
            need(target->width==(push?(first?640u:320u):1280u)&&target->height==(push?(first?360u:180u):720u),
                 "Post target extent differs");
            s.backend.bindTargets({target,nullptr,nullptr,nullptr},push?nullptr:p.depth);
            p.target=target;p.targetId=push?aux:p.mainId;PPC_STORE_U32(0x82D0CF5C,p.targetId);
            // 8243D230 -> D198 resets the default full viewport, clamped by CE80
            // to the selected attachment. The helper then reverses its depth.
            p.draw.viewport={0,0,target->width,target->height,0,0x3F800000};
            s.backend.setViewport({0,0,float(target->width),float(target->height),0,1});p.helperStep=2;
        } else if(site==0x82771E80||site==0x82771F64) {
            const uint32_t output=sp+(push?0x50:0x60);
            need(p.helperStep==2&&!c.r3.u32&&c.r4.u32==output,"Post get viewport arguments differ");
            for(unsigned i=0;i<6;++i)PPC_STORE_U32(output+4*i,p.draw.viewport[i]);p.helperStep=3;
        } else if(site==0x82771EA4||site==0x82771F88) {
            const uint32_t input=sp+(push?0x50:0x60);
            need(p.helperStep==3&&!c.r3.u32&&c.r4.u32==input,"Post set viewport arguments differ");
            const std::array<uint32_t,6> expected={0,0,p.target->width,p.target->height,0x3F800000,0};
            for(unsigned i=0;i<6;++i)need(PPC_LOAD_U32(input+4*i)==expected[i],"Post original reversed viewport changed");
            p.draw.viewport=expected;p.helperStep=4;if(push){p.helper=0;p.helperStep=0;}
        } else if(site==0x82771F98) {
            need(p.helperStep==4&&c.r3.u32==p.mainId&&p.target==p.main,"Post temporary target release differs");
            p.lease.reset();p.helper=0;p.helperStep=0;c.r3.u64=0;
        } else throw Failure("Unknown post target helper endpoint");
        c.lr=site+4;return;
    }
    if(quad) {
        const uint32_t caller=PPC_LOAD_U32(sp+0x58);frame(0x60,caller);
        const uint32_t expected=p.draws==0?0x82773F18:p.draws==1?0x82773FD4:(!(p.flags&1)&&p.draws==2)?0x82774098:0x827741F4;
        need(sp+0x60==p.stack&&caller==expected&&!p.helper&&p.sampler&&p.declaration&&p.vertex==0x821529C8,
             "Post rectangle caller/input owner differs");
        const bool final=caller==0x827741F4;
        const uint32_t ps=caller==0x82773F18?0x821583B8:caller==0x82773FD4?0x82152708:final?0x82158118:0x82155F28;
        need(p.pixel==ps&&p.copies==p.draws+1&&p.targetId==PPC_LOAD_U32(0x82D0CF5C)&&
             (final?p.target==p.main:p.targetId==PPC_LOAD_U32(p.draws?0x82DFEB80:0x82DFEB7C)),
             "Post rectangle stage/shader/target differs");
        if(site==0x82773CF4) {
            need(!p.locked&&!c.r3.u32&&c.r4.u32==8&&c.r5.u32==3&&c.r6.u32==8,"Post rectangle reservation ABI differs");
            if(!s.postStaging)s.postStaging=s.runtime.allocatePhysical(0,4096,PAGE_READWRITE,0,UINT32_MAX,4096);
            need(s.postStaging!=0,"Post rectangle staging allocation failed");
            for(unsigned i=0;i<6;++i)PPC_STORE_U32(s.postStaging+4*i,0x7FC00000);
            p.quad=sp;p.locked=true;c.r3.u64=s.postStaging;c.lr=site+4;return;
        }
        need(p.locked&&p.quad==sp&&!c.r3.u32,"Post rectangle submission has no owned reservation");
        need(p.constants==(final?3u:p.draws==1?1u:p.draws==2?0x200u:0u),"Post constant staging is incomplete");
        const uint32_t pixelSlot=ps==0x821583B8?14u:ps==0x82152708?2u:ps==0x82155F28?15u:16u;
        s.screenRecord(base,13);s.screenRecord(base,pixelSlot);
        need(PPC_LOAD_U32(0x82CD1A6C)==s.screenMaterialBindings[13].handle&&
             PPC_LOAD_U32(0x82CD1A70)==s.screenMaterialBindings[pixelSlot].handle&&
             PPC_LOAD_U32(0x82CD1A68)==s.screenDeclarationIds[0]&&PPC_LOAD_U32(0x82DFEB30)==s.screenDeclarationIds[0]&&
             s.screenDeclarations.contains(s.screenDeclarationRecords[0]),"Post rectangle lost its original shader/declaration bindings");
        auto& d=p.draw;d.pixelShader=p.pixel;
        for(unsigned i=0;i<6;++i)d.vertices[i]=std::bit_cast<float>(PPC_LOAD_U32(s.postStaging+4*i));
        need(d.vertices==std::array<float,6>{-1,1,1,1,-1,-1},"Post original rectangle stores differ");
        d.input=s.viewportSurfaces->colorTexture(p.inputHeader);
        using S=Graphics::ScalarState;using T=Graphics::SamplerState;const auto& e=s.renderState->effective();
        // CB80 writes the effective packed GPU word independently of A010's
        // scalar shadow. Execute that equation even when the retained shadow
        // is zero; 0x10001 itself is the disabled/copy equation. Do not publish
        // scalar1: its rebuild would overwrite the original packed word.
        d.blendEnable=1;d.blendWord=e.effectiveBlend(0);d.expandedBlend=e.scalar(S::ExpandedBlend0);
        d.colorMask=e.scalar(S::ColorMask0);d.depthEnable=e.scalar(S::DepthEnable);d.depthWrite=e.scalar(S::DepthWrite);d.depthCompare=e.scalar(S::DepthCompare);
        d.stencilEnable=e.scalar(S::StencilEnable);d.alphaTest=e.scalar(S::AlphaTest);d.cull=e.scalar(S::Cull);d.fill=e.scalar(S::Fill);
        d.scissorEnable=e.scalar(S::ScissorEnable);d.halfPixelOffset=e.scalar(S::HalfPixelOffset);d.viewportEnable=e.scalar(S::ViewportEnable);
        d.clipPlaneEnable=e.scalar(S::ClipPlaneEnable);d.multisampleAntialias=e.scalar(S::MultisampleAntialias);d.multisampleMask=e.scalar(S::MultisampleMask);
        d.alphaToMask=e.scalar(S::AlphaToMask);d.depthBiasBits=e.scalar(S::DepthBias);d.slopeBiasBits=e.scalar(S::SlopeBias);
        need(e.sampler(0,T::AddressU)==2&&e.sampler(0,T::AddressV)==2&&e.sampler(0,T::AddressW)<=2&&
             e.sampler(0,T::Magnification)==1&&e.sampler(0,T::Minification)==1&&e.sampler(0,T::MipFilter)<=2&&
             e.sampler(0,T::MaximumAnisotropy)==1&&!e.sampler(0,T::LodBiasBits),"Post inherited sampler differs");
        d.sampler={};d.sampler.Filter=e.sampler(0,T::MipFilter)==2?D3D11_FILTER_MIN_MAG_MIP_LINEAR:D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        d.sampler.AddressU=d.sampler.AddressV=D3D11_TEXTURE_ADDRESS_CLAMP;d.sampler.AddressW=D3D11_TEXTURE_ADDRESS_MODE(e.sampler(0,T::AddressW)+1);
        d.sampler.MaxAnisotropy=1;d.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;d.sampler.MinLOD=float(e.sampler(0,T::MinimumMip));
        d.sampler.MaxLOD=e.sampler(0,T::MipFilter)?float(e.sampler(0,T::MaximumMip)):0;
        s.backend.requireSelectedTargets({p.target,nullptr,nullptr,nullptr},final?p.depth:nullptr);
        static unsigned postStateTraces=0;
        if(postStateTraces++<16)std::fprintf(stderr,
            "[NATIVE POST STATE] caller=%08X ps=%08X target=%08X extent=%ux%u input=%08X input_extent=%ux%u "
            "blend_enable=%08X blend_shadow=%08X blend_word=%08X expanded=%08X color_mask=%08X depth_enable=%08X depth_write=%08X depth_compare=%08X "
            "stencil=%08X alpha_test=%08X cull=%08X fill=%08X scissor_enable=%08X half_pixel=%08X viewport_enable=%08X "
            "clip_plane=%08X multisample=%08X sample_mask=%08X alpha_to_mask=%08X depth_bias=%08X slope_bias=%08X "
            "viewport=%08X/%08X/%08X/%08X/%08X/%08X scissor=%u/%u/%u/%u "
            "sampler_filter=%08X uvw=%u/%u/%u aniso=%u comparison=%u lod=%.9g/%.9g/%.9g border=%.9g/%.9g/%.9g/%.9g\n",
            caller,p.pixel,p.targetId,p.target->width,p.target->height,p.inputHeader,d.input->width,d.input->height,
            d.blendEnable,e.scalar(S::BlendEnable),d.blendWord,d.expandedBlend,d.colorMask,d.depthEnable,d.depthWrite,d.depthCompare,
            d.stencilEnable,d.alphaTest,d.cull,d.fill,d.scissorEnable,d.halfPixelOffset,d.viewportEnable,
            d.clipPlaneEnable,d.multisampleAntialias,d.multisampleMask,d.alphaToMask,d.depthBiasBits,d.slopeBiasBits,
            d.viewport[0],d.viewport[1],d.viewport[2],d.viewport[3],d.viewport[4],d.viewport[5],d.scissor[0],d.scissor[1],d.scissor[2],d.scissor[3],
            unsigned(d.sampler.Filter),unsigned(d.sampler.AddressU),unsigned(d.sampler.AddressV),unsigned(d.sampler.AddressW),
            d.sampler.MaxAnisotropy,unsigned(d.sampler.ComparisonFunc),double(d.sampler.MipLODBias),double(d.sampler.MinLOD),double(d.sampler.MaxLOD),
            double(d.sampler.BorderColor[0]),double(d.sampler.BorderColor[1]),double(d.sampler.BorderColor[2]),double(d.sampler.BorderColor[3]));
        s.backend.drawPostFilter(p.target,d);++p.draws;p.locked=false;p.quad=0;p.constants=0;
        if(s.backend.postFilterDrawCount()<=16)std::fprintf(stderr,"[NATIVE POST DRAW] caller=%08X ps=%08X target=%08X input=%08X extent=%ux%u blend=%08X constants=original_cpu vertices=original_cpu count=%llu\n",
            caller,p.pixel,p.targetId,p.inputHeader,p.target->width,p.target->height,d.blendWord,static_cast<unsigned long long>(s.backend.postFilterDrawCount()));
        c.lr=site+4;return;
    }
    frame(0x120,0x8276E1C4);
    need(sp==p.stack&&c.r25.u32==p.flags&&c.r24.u32==p.mode&&!p.helper&&!p.locked,
         "Post filter original invocation changed");
    if(site==0x82773F44||site==0x82774000||site==0x827740C4) {
        const uint32_t stage=site==0x82773F44?1u:site==0x82774000?2u:3u;
        need(p.draws==stage&&p.copies==stage&&(stage!=3||!(p.flags&1)),"Post resolve order differs");
        s.viewportSurfaces->copyColor(c,base,site);++p.copies;return;
    }
    if(site==0x82773E50||site==0x82773F6C||site==0x82774014) {
        const uint32_t stage=site==0x82773E50?0u:site==0x82773F6C?1u:2u;
        const uint32_t slot=stage==0?0u:stage==1?3u:4u;
        need(p.copies==stage+1&&p.draws==stage&&!c.r3.u32&&!c.r4.u32&&c.r6.u64==0x80000000ull&&
             c.r5.u32==PPC_LOAD_U32(0x82DFEB48+slot*4),"Post texture bind arguments/order differ");
        p.draw.input=s.viewportSurfaces->colorTexture(c.r5.u32);p.inputHeader=c.r5.u32;c.lr=site+4;return;
    }
    if(site==0x82773E60||site==0x82773E70) {
        need(p.copies==1&&!p.draws&&!p.sampler&&!c.r3.u32&&!c.r4.u32&&c.r5.u32==1,
             "Post direct linear sampler endpoint differs");
        directSampler(base,0,site==0x82773E60?0x14:0x10,c.r5.u32);c.lr=site+4;return;
    }
    if(site==0x82773ECC||site==0x82774130) {
        const bool first=site==0x82773ECC;
        const uint32_t selector=(p.flags&2)?11u:1u;
        const uint32_t expected=first?0x10001u:((p.mode==0?0x10100u:p.mode==2?0x10180u:p.mode==3?0x10000u:0x10700u)|selector);
        need(!c.r3.u32&&!c.r4.u32&&c.r5.u32==expected&&p.sampler&&
             p.draws==(first?0u:((p.flags&1)?2u:3u))&&p.copies==p.draws+1&&
             (first?p.targetId==PPC_LOAD_U32(0x82DFEB7C):p.target==p.main&&!p.lease),
             "Post original packed-blend endpoint differs");
        auto next=s.renderState->effective();next.setPackedBlend(0,c.r5.u32);s.renderState->publishScreenState(next);
        c.lr=site+4;return;
    }
    if(site==0x82773E74) {
        need(!p.sampler&&p.copies==1&&!p.draws,"Post sampler inline block order differs");
        auto next=s.renderState->effective();using T=Graphics::SamplerState;
        // Hardware clamp_x/y begin at bits10/13. Original rotations11/14 of1
        // insert raw2 (CLAMP), unlike the neighboring sprite's mirror block.
        need(next.sampler(0,T::Magnification)==1&&next.sampler(0,T::Minification)==1,"Post linear setters were not applied");
        next.setSampler(0,T::AddressU,2);next.setSampler(0,T::AddressV,2);s.renderState->publishScreenState(next);
        p.sampler=true;c.r3.u64=0;return;
    }
    if(site==0x82773EE4||site==0x82773EFC||site==0x82773F80||site==0x82774034||site==0x82774144) {
        const uint32_t slot=site==0x82773EE4?13u:site==0x82773EFC?14u:site==0x82773F80?2u:site==0x82774034?15u:16u;
        const bool vertex=slot==13;const uint32_t stage=slot==13||slot==14?0u:slot==2?1u:slot==15?2u:((p.flags&1)?2u:3u);
        need(!c.r3.u32&&p.draws==stage&&p.copies==stage+1&&(slot!=15||!(p.flags&1)),"Post shader bind stage differs");
        const auto& original=s.screenRecord(base,slot);
        need(c.r4.u32==s.screenMaterialBindings[slot].handle&&PPC_LOAD_U32(vertex?0x82CD1A6C:0x82CD1A70)==c.r4.u32,
             "Post shader bind did not select its exact original shared object");
        if(vertex)p.vertex=original.identity().originalAddress;else p.pixel=original.identity().originalAddress;
        c.lr=site+4;return;
    }
    if(site==0x82773F10) {
        need(!p.declaration&&!p.draws&&!c.r3.u32&&c.r4.u32==s.screenDeclarationIds[0]&&c.r4.u32&&
             PPC_LOAD_U32(0x82DFEB30)==c.r4.u32&&PPC_LOAD_U32(0x82CD1A68)==c.r4.u32&&
             s.screenDeclarations.contains(s.screenDeclarationRecords[0]),"Post float2 declaration owner differs");
        p.declaration=true;c.lr=site+4;return;
    }
    if(site==0x82773FA8||site==0x8277406C||site==0x82774174||site==0x827741C8) {
        const bool white=site==0x82773FA8,blur=site==0x8277406C,offset=site==0x827741C8;
        const uint32_t index=blur?9u:offset?1u:0u,stackOffset=white?0x70u:blur?0x80u:offset?0x90u:0xA0u;
        need(p.draws==(white?1u:blur?2u:((p.flags&1)?2u:3u))&&p.pixel==(white?0x82152708u:blur?0x82155F28u:0x82158118u)&&
             !(p.constants&(1u<<index))&&(!offset||(p.constants&1)),"Post original constant block order differs");
        for(unsigned i=0;i<4;++i){const float v=std::bit_cast<float>(PPC_LOAD_U32(sp+stackOffset+4*i));
            need(std::isfinite(v),"Post CPU constant is not finite");p.draw.pixelConstants[index][i]=v;}
        p.constants|=1u<<index;return;
    }
    if(site==0x82774218) {
        need(p.draws==((p.flags&1)?3u:4u)&&p.copies==p.draws&&!p.lease&&p.target==p.main&&
             PPC_LOAD_U32(0x82CF1F88)==p.stackCount&&PPC_LOAD_U32(0x82D0CF5C)==p.mainId,
             "Post filter exited before target restoration or completion");
        s.backend.requireSelectedTargets({p.main,nullptr,nullptr,nullptr},p.depth);p={};return;
    }
    throw Failure("Unknown bounded post filter endpoint");
}
void EngineDriver::radialOperation(PPCContext& c,uint8_t* base,uint32_t operation) {
    auto& s=*state;auto& r=s.radial;
    if(s.runtime.resourceAudit.active())try {
        char parameters[1024],instance[384];
        if(s.renderState) {
            using S=Graphics::ScalarState;const auto& e=s.renderState->effective();
            std::snprintf(parameters,sizeof(parameters),
                "operation=%u phase_owned=%u segments=%u segments_qualified=%u count=%u query=%u stencil=%u cull=%u fill=%u clip=%u viewport_enable=%u half_pixel=%u alpha_to_mask=%u depth_enable=%u depth_write=%u depth_compare=%u color_mask=%u scissor=%u sample_mask=%08X blend_enable=%u blend=%08X alpha_test=%u alpha_ref=%u depth_bias=%08X slope_bias=%08X captured_stencil=%u captured_cull=%u captured_fill=%u captured_clip=%u captured_viewport=%u captured_half_pixel=%u captured_alpha_to_mask=%u",
                operation,unsigned(bool(r.owner)),operation==2&&c.r28.u32>=3&&c.r28.u32<=32?c.r28.u32:0u,
                unsigned(operation==2&&c.r28.u32>=3&&c.r28.u32<=32),r.count,unsigned(bool(r.draw.query)),
                e.scalar(S::StencilEnable),e.scalar(S::Cull),e.scalar(S::Fill),e.scalar(S::ClipPlaneEnable),
                e.scalar(S::ViewportEnable),e.scalar(S::HalfPixelOffset),e.scalar(S::AlphaToMask),
                e.scalar(S::DepthEnable),e.scalar(S::DepthWrite),e.scalar(S::DepthCompare),e.scalar(S::ColorMask0),
                e.scalar(S::ScissorEnable),e.scalar(S::MultisampleMask),e.scalar(S::BlendEnable),e.effectiveBlend(0),
                e.scalar(S::AlphaTest),e.scalar(S::AlphaReference),e.scalar(S::DepthBias),e.scalar(S::SlopeBias),
                r.draw.stencilEnable,r.draw.cull,r.draw.fill,r.draw.clipPlaneEnable,r.draw.viewportEnable,r.draw.halfPixelOffset,r.draw.alphaToMask);
        } else std::snprintf(parameters,sizeof(parameters),"operation=%u pipeline=unavailable",operation);
        std::snprintf(instance,sizeof(instance),
            "owner=%08X descriptor=%08X retained_owner=%08X retained_descriptor=%08X declaration=%08X staging=%08X stack=%08X function=%08X lr=%08X r28=%08X",
            c.r29.u32,c.r31.u32,r.owner,r.descriptor,s.radialDeclarationId,s.radialStaging,c.r1.u32,
            c.lastFunction,uint32_t(c.lr),c.r28.u32);
        s.runtime.resourceAudit.observe("radial_draw","declaration:82153DE8",uint32_t(c.lr),parameters,
            r.owner?"original-phase-owned-before-validation":"original-boundary-before-validation",s.runtime.nativeDepthCopyCount.load(),instance);
    }catch(...) {
        s.runtime.resourceAudit.observe("radial_draw","declaration:82153DE8",uint32_t(c.lr),"snapshot=unreadable",
            "unknown-before-validation",s.runtime.nativeDepthCopyCount.load());
    }
    s.requireCaller(base);
    constexpr uint32_t words[]={0,0x001A2086,0xA0000,4,0x002C23A5,0x50000,0xFF0000,0xFFFFFFFF,0};
    for(unsigned i=0;i<std::size(words);++i)if(PPC_LOAD_U32(0x82153DE8+4*i)!=words[i])throw Failure("Original radial declaration changed");
    if(currentContext!=&c)throw Failure("Radial operation has no current CPU frame");
    if(operation==0){
        if(c.r3.u32!=0x82153DE8||s.radialDeclarationId||PPC_LOAD_U32(0x82DFF284))throw Failure("Radial declaration creation differs");
        s.radialDeclarationId=allocateTargetIdentity();c.r3.u64=s.radialDeclarationId;c.lr=0x8276BF40;return;
    }
    if(operation==1){
        if(!s.radialDeclarationId||c.r3.u32!=s.radialDeclarationId||PPC_LOAD_U32(0x82DFF284)!=s.radialDeclarationId||r.owner)
            throw Failure("Radial declaration release differs");
        s.radialDeclarationId=0;c.r3.u64=0;c.lr=0x8276C07C;return;
    }
    if(operation==2){
        if(r.owner||s.coronaQueries.active||s.directSprite.active||PPC_LOAD_U32(c.r1.u32)!=c.r1.u32+0x110||c.r28.u32<3||c.r28.u32>32)
            throw Failure("Radial original setup frame/segments differ");
        if(!s.radialDeclarationId||PPC_LOAD_U32(0x82DFF284)!=s.radialDeclarationId)throw Failure("Radial declaration owner missing");
        s.runtime.pointer(c.r31.u32,0x30,false);s.runtime.pointer(c.r29.u32,0xB4,false);
        const bool query=PPC_LOAD_U8(c.r31.u32)!=0;s.screenMaterial(base,10);s.screenMaterial(base,query?12:11);
        State::Radial pending;pending.stack=c.r1.u32;pending.owner=c.r29.u32;pending.descriptor=c.r31.u32;pending.count=2*(c.r28.u32+1);
        auto& d=pending.draw;d.radial=true;
        if(query){const auto entry=PPC_LOAD_U32(c.r31.u32+4);s.runtime.pointer(entry,0x1C,false);
            if(PPC_LOAD_U8(entry+0x1A)!=1||PPC_LOAD_U8(entry+0x18)>=64||PPC_LOAD_U8(entry+0x19)>=8)throw Failure("Radial query readiness/cell differs");
            d.query=s.viewportSurfaces->queryTexture(PPC_LOAD_U32(0x82DFEB6C));}
        using S=Graphics::ScalarState;const auto& e=s.renderState->effective();
        d.depthEnable=e.scalar(S::DepthEnable);d.depthWrite=e.scalar(S::DepthWrite);d.depthCompare=e.scalar(S::DepthCompare);
        d.colorMask=e.scalar(S::ColorMask0);d.cull=e.scalar(S::Cull);d.stencilEnable=e.scalar(S::StencilEnable);d.fill=e.scalar(S::Fill);
        d.scissorEnable=e.scalar(S::ScissorEnable);d.halfPixelOffset=e.scalar(S::HalfPixelOffset);d.viewportEnable=e.scalar(S::ViewportEnable);
        d.clipPlaneEnable=e.scalar(S::ClipPlaneEnable);d.alphaToMask=e.scalar(S::AlphaToMask);d.multisampleMask=e.scalar(S::MultisampleMask);
        d.depthBiasBits=e.scalar(S::DepthBias);d.slopeBiasBits=e.scalar(S::SlopeBias);d.alphaTest=e.scalar(S::AlphaTest);
        d.alphaReference=e.scalar(S::AlphaReference);d.blendEnable=e.scalar(S::BlendEnable);d.blendWord=e.effectiveBlend(0);d.viewport=cameraBinding().viewport;
        if(d.alphaTest&&e.scalar(S::AlphaCompare)!=4)throw Failure("Radial alpha comparison differs");
        d.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;d.sampler.AddressU=d.sampler.AddressV=d.sampler.AddressW=D3D11_TEXTURE_ADDRESS_MIRROR;
        d.sampler.MaxAnisotropy=1;d.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
        PPC_STORE_U32(0x82CD1A6C,PPC_LOAD_U32(0x82CF23BC));PPC_STORE_U32(0x82CD1A70,PPC_LOAD_U32(query?0x82CF23D4:0x82CF23C8));
        PPC_STORE_U32(0x82CD1A68,s.radialDeclarationId);r=std::move(pending);c.r30.u32=0x82D10000;c.lr=0x8276BCB4;return;
    }
    if(!r.owner||r.stack!=c.r1.u32||r.owner!=c.r29.u32||r.descriptor!=c.r31.u32)throw Failure("Radial original CPU scope changed");
    if(operation==3){
        for(unsigned i=0;i<8;++i)r.draw.constants[2+i/4][i%4]=std::bit_cast<float>(PPC_LOAD_U32(c.r1.u32+0x80+4*i));
        // The original CPU leaves unused query coordinates uninitialized for flat sprites.
        if(!r.draw.query)r.draw.constants[3][2]=r.draw.constants[3][3]=0;
        c.lr=0x8276BD74;return;
    }
    if(operation==4){
        if(c.r4.u32!=6||c.r5.u32!=r.count||c.r6.u32!=12)throw Failure("Radial original vertex reservation differs");
        if(!s.radialStaging)s.radialStaging=s.runtime.allocatePhysical(0,4096,PAGE_READWRITE,0,UINT32_MAX,4096);
        if(!s.radialStaging)throw Failure("Radial CPU staging allocation failed");c.r3.u64=s.radialStaging;c.lr=0x8276BDAC;return;
    }
    if(operation!=5||c.r3.u32!=s.radialStaging+r.count*12)throw Failure("Radial CPU vertex completion differs");
    auto& d=r.draw;d.vertices.resize(r.count);
    for(uint32_t i=0;i<r.count;++i){const auto at=s.radialStaging+12*i,color=PPC_LOAD_U32(at);auto& v=d.vertices[i];
        v.position={std::bit_cast<float>(PPC_LOAD_U32(at+4)),std::bit_cast<float>(PPC_LOAD_U32(at+8)),0};
        for(unsigned j=0;j<4;++j)v.uv[j]=float((color>>(8*j))&255)/255.0f;
    }
    const auto camera=cameraBinding();bool alpha=false;s.backend.drawImmediate(color(camera.colorIdentity,alpha),depth(camera.depthIdentity),d);
    static unsigned traced=0;if(traced++<5)std::fprintf(stderr,"[NATIVE RADIAL DRAW] vertices=%u query=%u original_cpu_vertices=true\n",r.count,unsigned(bool(d.query)));
    r={};c.lr=0x8276BE94;
}
bool EngineDriver::beginDirectSprite(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.requireCaller(base);auto& sprite=s.directSprite;
    if(sprite.active||c.r3.u32||c.r4.u32||c.r6.u64!=0x80000000ull||!c.r5.u32)
        throw Failure("Direct sprite entry ABI differs");
    const uint32_t descriptor=c.r29.u32;
    s.runtime.pointer(descriptor,8,false);
    bool drawReady=true;uint32_t queryEntry=0;
    if(PPC_LOAD_U8(descriptor)){
        const uint32_t query=PPC_LOAD_U32(descriptor+4);
        s.runtime.pointer(query,0x1C,false);
        drawReady=PPC_LOAD_U8(query+0x1A)!=0;
        if(drawReady)queryEntry=query;
    }
    // These are exactly the existing Screen_Xenon originals, not aliases.
    if(PPC_LOAD_U32(0x82CF2344)!=0x82152880||PPC_LOAD_U32(0x82CF2338)!=0x82152708)
        throw Failure("Direct sprite Screen_Xenon shader association changed");
    const uint32_t declaration=PPC_LOAD_U32(0x82DFEB34);
    if(declaration!=s.screenDeclarationIds[1]||!s.screenDeclarations.contains(s.screenDeclarationRecords[1])||
       s.screenMaterial(base,3).originalAddress()!=0x82152880)
        throw Failure("Direct sprite vertex inputs lack their original creation owner");
    const uint32_t raster=c.r10.u32,plugin=PPC_LOAD_U32(0x82E3DC94);
    if(PPC_LOAD_U32(raster+plugin)!=c.r5.u32)throw Failure("Direct sprite raster/header owner differs");
    auto draw=Graphics::ScreenDraw{};draw.texture=textureRaster(raster);
    auto post=s.renderState->effective();using S=Graphics::ScalarState;using T=Graphics::SamplerState;
    post.setScalar(S::AlphaTest,0);post.setScalar(S::BlendEnable,1);post.setScalar(S::ExpandedBlend0,1);
    post.setScalar(S::DepthEnable,0);post.setScalar(S::DepthWrite,0);post.setScalar(S::Cull,0);post.setPackedBlend(0,0x10106);
    // 8243BA40/BB D0 receive1 (linear). Inline fields atAFF8/B018 are
    // addressU/V=1, which is the native mirror mode, not RenderWare value1.
    post.setSampler(0,T::Magnification,1);post.setSampler(0,T::Minification,1);
    post.setSampler(0,T::AddressU,1);post.setSampler(0,T::AddressV,1);
    if(post.sampler(0,T::AddressW)>2||post.sampler(0,T::MaximumAnisotropy)!=1||post.sampler(0,T::LodBiasBits))
        throw Failure("Direct sprite inherited sampler is unqualified");
    draw.sampler.Filter=post.sampler(0,T::MipFilter)==2?D3D11_FILTER_MIN_MAG_MIP_LINEAR:D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    draw.sampler.AddressU=draw.sampler.AddressV=D3D11_TEXTURE_ADDRESS_MIRROR;
    draw.sampler.AddressW=D3D11_TEXTURE_ADDRESS_MODE(post.sampler(0,T::AddressW)+1);draw.sampler.MaxAnisotropy=1;
    draw.sampler.MinLOD=float(post.sampler(0,T::MinimumMip));draw.sampler.MaxLOD=post.sampler(0,T::MipFilter)?float(post.sampler(0,T::MaximumMip)):0;
    draw.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
    if(s.effects)s.effects->preflightScreenReplacement(base);
    const auto inputs=s.backend.bindDirectSpriteInputs(draw.texture,draw.sampler);
    s.renderState->publishScreenState(post);
    PPC_STORE_U32(0x82CD1A6C,PPC_LOAD_U32(0x82CF2340));PPC_STORE_U32(0x82CD1A68,declaration);
    // OriginalB0DC goes directly to B25C, before both CPU geometry generation
    // and the B250 expanded-blend reset. It does not select a pixel shader.
    if(!drawReady) {
        if(s.effects)s.effects->completeScreenInputReplacement(base,inputs,declaration,
            PPC_LOAD_U32(0x82CF2340),PPC_LOAD_U32(0x82CD1A70),false);
        return false;
    }
    if(s.screenMaterial(base,queryEntry?7:2).originalAddress()!=(queryEntry?0x821536F0u:0x82152708u))
        throw Failure("Direct sprite pixel shader lacks its original creation owner");
    if(queryEntry){
        const uint32_t column=PPC_LOAD_U8(queryEntry+0x18),row=PPC_LOAD_U8(queryEntry+0x19);
        if(column>=64||row>=8)throw Failure("Direct sprite query coordinate differs");
        draw.coronaQuery=s.viewportSurfaces->queryTexture(PPC_LOAD_U32(0x82DFEB6C));
        draw.coronaUV={(float(column)+.5f)/64,(float(row)+.5f)/8};
        post.setSampler(1,T::Minification,0);post.setSampler(1,T::Magnification,0);post.setSampler(1,T::AddressU,1);post.setSampler(1,T::AddressV,1);
    }
    s.runtime.pointer(c.r26.u32,16,false);
    for(unsigned i=0;i<4;++i)draw.color[i]=std::bit_cast<float>(PPC_LOAD_U32(c.r26.u32+4*i));
    draw.blendSelector=0;draw.colorWriteMask=uint8_t(post.scalar(S::ColorMask0));
    if(post.scalar(S::StencilEnable)||post.scalar(S::Fill)||post.scalar(S::ScissorEnable)||post.scalar(S::ClipPlaneEnable)||
       post.scalar(S::AlphaToMask)||post.scalar(S::HalfPixelOffset)!=1)throw Failure("Direct sprite inherited draw state is unqualified");
    if(!sprite.staging)sprite.staging=s.runtime.allocatePhysical(0,4096,PAGE_READWRITE,0,UINT32_MAX,4096);
    if(!sprite.staging)throw Failure("Direct sprite CPU allocation failed");
    sprite.draw=std::move(draw);sprite.post=post;sprite.stack=c.r1.u32;sprite.active=true;sprite.inputs=inputs;
    PPC_STORE_U32(0x82CD1A6C,PPC_LOAD_U32(0x82CF2340));PPC_STORE_U32(0x82CD1A70,PPC_LOAD_U32(queryEntry?0x82CF23E0:0x82CF2334));
    PPC_STORE_U32(0x82CD1A68,PPC_LOAD_U32(0x82DFEB34));
    if(s.effects)s.effects->completeScreenInputReplacement(base,inputs,declaration,
        PPC_LOAD_U32(0x82CF2340),PPC_LOAD_U32(queryEntry?0x82CF23E0:0x82CF2334),true);
    c.f1.f64=c.f31.f64;c.f2.f64=c.f30.f64;c.f3.f64=c.f29.f64;c.f4.f64=c.f28.f64;c.f5.f64=c.f27.f64;
    return true;
}
void EngineDriver::lockDirectSprite(PPCContext& c,uint8_t* base){
    auto& s=*state;s.requireCaller(base);auto& sprite=s.directSprite;
    if(!sprite.active||sprite.locked||c.r1.u32+160!=sprite.stack||c.r3.u32||c.r4.u32!=6||c.r5.u32!=4||c.r6.u32!=16)
        throw Failure("Direct sprite CPU quad lock ABI differs");
    c.r3.u32=sprite.staging;sprite.locked=true;
}
void EngineDriver::finishDirectSprite(PPCContext& c,uint8_t* base){
    auto& s=*state;s.requireCaller(base);auto& sprite=s.directSprite;
    if(!sprite.active||!sprite.locked||c.r1.u32+160!=sprite.stack||c.r3.u32)
        throw Failure("Direct sprite CPU quad completion differs");
    for(unsigned i=0;i<4;++i){auto& v=sprite.draw.vertices[i];float* values=&v.x;
        for(unsigned j=0;j<4;++j)values[j]=std::bit_cast<float>(PPC_LOAD_U32(sprite.staging+16*i+4*j));}
    const auto camera=cameraBinding();bool alpha=false;
    sprite.draw.preserveAspect=s.uiDrawing;
    s.backend.requireScreenInputReplacement(sprite.inputs);
    if(s.effects)s.effects->requirePendingScreenInputs(base,sprite.inputs);
    const auto screenBefore=s.backend.screenDrawCount();const bool query=bool(sprite.draw.coronaQuery);
    s.backend.drawOriginalScreen(color(camera.colorIdentity,alpha),depth(camera.depthIdentity),sprite.draw,false,sprite.post.scalar(Graphics::ScalarState::DepthCompare),false);
    const auto replacement=s.backend.completedScreenReplacement(screenBefore,s.screenMaterial(base,3),s.screenMaterial(base,query?7:2));
    if(s.effects)s.effects->completeScreenReplacement(base,replacement,PPC_LOAD_U32(0x82DFEB34),
        PPC_LOAD_U32(0x82CF2340),PPC_LOAD_U32(query?0x82CF23E0:0x82CF2334));
    s.renderState->publishScreenState(sprite.post);sprite.active=sprite.locked=false;sprite.draw={};
    sprite.inputs={};
    static unsigned traced=0;if(traced++<5)std::fprintf(stderr,"[NATIVE DIRECT SPRITE] original8276ACC0 quad + exact Screen_Xenon shaders completed\n");
}
void EngineDriver::directScalar(uint8_t* base,uint32_t id,uint32_t value) {
    auto& s=*state;s.requireCaller(base);
    if(!s.ready || !s.submissionReady) throw Failure("Native direct scalar requires registered driver submission");
    requireStateContext(PPC_LOAD_U32(0x82D6D890));s.renderState->directScalar(base,id,value);
}
void EngineDriver::directSampler(uint8_t* base,uint32_t stage,uint32_t id,uint32_t value) {
    auto& s=*state;s.requireCaller(base);
    if(!s.ready || !s.submissionReady)throw Failure("Native direct sampler requires registered driver submission");
    requireStateContext(PPC_LOAD_U32(0x82D6D890));s.renderState->directSampler(base,stage,id,value);
}
void EngineDriver::renderWareSampler(PPCContext& ctx,uint8_t* base,uint32_t selector,uint32_t value,bool execute) {
    auto& s=*state;s.requireCaller(base);
    if(!s.ready || !s.submissionReady)throw Failure("Native RenderWare sampler requires registered driver submission");
    requireContext(PPC_LOAD_U32(0x82D6D890));s.renderState->renderWareSampler(ctx,base,selector,value,execute);
}
void EngineDriver::preflightResetNullTexture(uint8_t* base,uint32_t texture,uint32_t stage) const {
    auto& s=*state;s.requireCaller(base);
    if(!s.ready || !s.bindingReset || texture || stage>=8)
        throw Failure("Null texture operation is outside native mode-zero reset");
    preflightNullRaster(base,texture,stage);
}
void EngineDriver::preflightNullRaster(uint8_t* base,uint32_t texture,uint32_t stage) const {
    auto& s=*state;s.requireCaller(base);
    if(!s.ready || !s.submissionReady || stage>=8 || (s.bindingReset && texture))
        throw Failure("Unsupported native raster binding");
    requireContext(PPC_LOAD_U32(0x82D6D890));
    s.backend.validateSubmissionContext();
    const uint32_t field=0x82D0E3F8+24*stage,raster=PPC_LOAD_U32(field);
    s.runtime.pointer(field,4,true);
    if(raster) textureRaster(raster); // Validate a displaced owner before changing the cache.
    uint32_t alpha=0;
    if(texture) {
        const auto owned=textureRaster(texture);
        s.backend.validateTexture(owned);
        const uint32_t offset=PPC_LOAD_U32(0x82E3DC94);
        if((texture&3) || offset<0x34 || (offset&3) || uint64_t(texture)+offset+0x20>0x100000000ull)
            throw Failure("Native raster binding has invalid plugin bounds");
        const uint32_t x=texture+offset;s.runtime.pointer(x,0x20,false);
        if(!PPC_LOAD_U32(x) || PPC_LOAD_U32(x+4))
            throw Failure("Native raster binding requires its qualified single texture owner");
        alpha=PPC_LOAD_U8(x+8);
        if(alpha>1) throw Failure("Native raster texture alpha flag is outside the Boolean profile");
        if(raster==texture) s.backend.requireEngineTexture(stage,owned);
    }
    if(!stage && PPC_LOAD_U32(0x82D0E3DC)!=alpha) {
        if(PPC_LOAD_U32(0x82D0E3DC)>1 || PPC_LOAD_U32(0x82D0E3D8)>1 || PPC_LOAD_U32(0x82D0E4C4)>1)
            throw Failure("Native raster retained alpha flags are outside the Boolean profile");
        s.runtime.pointer(0x82D0E3DC,4,true);
        if(!PPC_LOAD_U32(0x82D0E3D8)) {
            const uint32_t count=PPC_LOAD_U32(0x82D10114);
            if(count>425) throw Failure("Native null texture dirty queue is out of bounds");
            uint32_t additions=0;
            for(uint32_t id:{0x3Cu,0x60u}) {
                const uint32_t p=0x82D0F3B0+8*id,dirty=PPC_LOAD_U32(p+4);
                uint32_t queued=0;
                for(uint32_t i=0;i<count;++i) queued+=PPC_LOAD_U32(0x82D0ED08+4*i)==id;
                if(dirty>1 || queued!=dirty) throw Failure("Native null texture dirty membership is inconsistent");
                s.runtime.pointer(p,8,true);
                const uint32_t value=id==0x3C?alpha:(alpha?PPC_LOAD_U32(0x82D0E4C4):0);
                additions+=!dirty && (id==0x3C || PPC_LOAD_U32(p)!=value);
            }
            if(count+additions>425) throw Failure("Native null texture dirty queue is full");
            if(additions) {
                s.runtime.pointer(0x82D0ED08+4*count,4*additions,true);
                s.runtime.pointer(0x82D10114,4,true);
            }
        }
    }
}
void EngineDriver::setTextureRaster(uint8_t* base,uint32_t stage,uint32_t identity,uint64_t mask,uint32_t caller) {
    auto& s=*state;s.requireCaller(base);
    // Caller is the skipped bl 0x824408e0 return address: the usual 0x82401AA0
    // site plus effect-code sites (0x827519D4 reach-game-250, 0x82773148
    // reach-game-268, and the stage-zero 0x82771B44 Ball Homer effect bind).
    // The bind substance is verified below against the published raster owner.
    const bool qualifiedCaller=caller==0x82401AA0 || caller==0x827519D4 ||
        caller==0x82773148 || (caller==0x82771B44 && stage==0);
    if(!s.ready || !s.submissionReady || s.bindingReset || !qualifiedCaller || !identity || stage>=8 ||
       mask!=(uint64_t(1)<<63)>>(stage+32))
        std::fprintf(stderr,"[NATIVE TEXRASTER] ready=%u sub=%u reset=%u caller=%08X identity=%08X stage=%u mask=%016llX want=%016llX\n",
            s.ready,s.submissionReady,s.bindingReset,caller,identity,stage,(unsigned long long)mask,
            (unsigned long long)((uint64_t(1)<<63)>>(stage+32)));
    if(!s.ready || !s.submissionReady || s.bindingReset || !qualifiedCaller || !identity || stage>=8 ||
       mask!=(uint64_t(1)<<63)>>(stage+32))
        throw Failure("Native texture callback has an invalid owner or ABI");
    uint32_t raster=PPC_LOAD_U32(0x82D0E3F8+24*stage);
    // Null stage raster with a real identity: resolve the owning raster so the
    // identical downstream checks apply. Plugin pointers (mapped) and texture
    // ids (unmapped by construction) are disjoint namespaces; at most one
    // hits. A stale non-null raster still fails loudly in texture().
    // Otherwise arm the stream watch (data residency across presents); the
    // object-graph dumps that mapped this frontier are retired.
    if(!raster) {
        raster=s.rasters->rasterByPlugin(base,identity);
        if(!raster) raster=s.rasters->rasterByIdentity(base,identity);
        // Ball Homer and the original shared immediate texture helper supply
        // embedded ITXD headers. Resolve only published copied texture owners.
        if(!raster && (caller==0x82771B44||caller==0x827519D4) && s.itxdTextures)
            raster=s.itxdTextures->rasterByHeader(base,identity);
        if(raster)
            std::fprintf(stderr,"[NATIVE TEXBIND IDENTITY] stage=%u identity=%08X raster=%08X\n",stage,identity,raster);
        else {
            // Unresolvable streamed-texture bind (square/e172a05c frontier):
            // leave the stage untouched and continue so the consumer reveals
            // itself downstream. Divergent stage state is possible; every
            // later mismatch names this skip via the stage/identity below.
            try {
                s.runtime.pointer(identity,64,false);
                streamWatchData=PPC_LOAD_U32(identity+32);streamWatchPrints=0;
                std::fprintf(stderr,"[NATIVE TEXBIND SKIPPED] stage=%u identity=%08X data=%08X; stage retains its previous binding\n",
                    stage,identity,streamWatchData);
            } catch(...) { std::fprintf(stderr,"[NATIVE TEXBIND SKIPPED] stage=%u identity=%08X unreadable\n",stage,identity); }
            return;
        }
    }
    const auto texture=textureRaster(raster);
    const uint32_t offset=PPC_LOAD_U32(0x82E3DC94);
    if(uint64_t(raster)+offset+8>0x100000000ull) throw Failure("Native texture callback plugin address overflow");
    s.runtime.pointer(raster+offset,8,false);
    if(uint64_t(raster)+offset+8>0x100000000ull) throw Failure("Native texture callback plugin address overflow");
    s.runtime.pointer(raster+offset,8,false);
    if(PPC_LOAD_U32(raster+offset)!=identity || PPC_LOAD_U32(raster+offset+4))
        throw Failure("Native texture callback differs from its published original raster owner");
    try {s.backend.bindEngineTexture(stage,texture);}
    catch(...) {s.ready=false;s.runtime.requestStop("Native raster GPU publication failed");throw;}
}
void EngineDriver::resetNullTexture(uint8_t* base,uint32_t stage,uint64_t mask,uint32_t caller) {
    auto& s=*state;s.requireCaller(base);
    if(!s.ready || !s.submissionReady || stage>=8 || mask!=(uint64_t(1)<<63)>>(stage+32))
        std::fprintf(stderr,"[NATIVE TEXTURERESET] ready=%u sub=%u stage=%u mask=%016llX caller=%08X reset=%u\n",
            s.ready,s.submissionReady,stage,(unsigned long long)mask,caller,s.bindingReset);
    if(!s.ready || !s.submissionReady || stage>=8 || mask!=(uint64_t(1)<<63)>>(stage+32))
        throw Failure("Native null texture callback has an invalid scope or ABI");
    // Null-raster stage clear outside binding reset: the usual 0x82401ABC site,
    // the 0x82751B60 effect-code site, and the Ball Homer effect's stage-zero
    // clear immediately after its draw. The raster cache must be null.
    const bool qualifiedClear=caller==0x82401ABC || caller==0x82751B60 ||
        (caller==0x82771DB4 && stage==0);
    if(qualifiedClear && !s.bindingReset) {
        if(PPC_LOAD_U32(0x82D0E3F8+24*stage)) {
            std::fprintf(stderr,"[NATIVE TEXTURERESET] null raster unpublished: stage=%u cache=%08X caller=%08X\n",
                stage,PPC_LOAD_U32(0x82D0E3F8+24*stage),caller);
            throw Failure("Original null raster cache was not published");
        }
        try {s.backend.clearEngineTexture(stage);}
        catch(...) {s.ready=false;s.runtime.requestStop("Native raster GPU publication failed");throw;}
        return;
    }
    if(!s.bindingReset ||
       (caller!=0x82401DF0 && caller!=0x82400FA0)) {
        std::fprintf(stderr,"[NATIVE TEXTURERESET] reset-path: stage=%u mask=%016llX caller=%08X reset=%u\n",
            stage,(unsigned long long)mask,caller,s.bindingReset);
        throw Failure("Native reset texture callback has an invalid scope or ABI");
    }
    // The enclosing engine reset commits all actual native unbinds together
    // after its CPU rebuild succeeds. No console descriptor is represented.
    s.resetTextureStages|=uint8_t(1u<<stage);
}
void EngineDriver::resetBindings(PPCContext& incoming,uint8_t* base) {
    auto& s=*state;auto resetTimer=s.timing.measure(FrameTiming::BindingReset);s.requireCaller(base);
    if(!s.ready || !s.submissionReady || s.bindingReset) throw Failure("Native binding reset requires a live nonnested submission owner");
    requireContext(PPC_LOAD_U32(0x82D6D890));
    if(s.effects)s.effects->preflightBindingReset(base);
    if(!s.camera) throw Failure("Native binding reset has no selected camera viewport");
    const auto camera=s.validateCamera(base,s.camera->camera);
    if(camera.colorRaster!=s.camera->colorRaster || camera.depthRaster!=s.camera->depthRaster ||
       PPC_LOAD_U32(0x82D0CF5C)!=s.camera->colorIdentity || PPC_LOAD_U32(0x82D0CF58)!=s.camera->depthIdentity)
        throw Failure("Native binding reset requires its already selected owned target roles");
    const bool targetChanged=s.camera->colorIdentity!=s.targetIds[0];
    if(targetChanged) {
        if(s.camera->colorIdentity!=camera.colorIdentity || s.camera->depthIdentity!=camera.depthIdentity)
            throw Failure("Native binding reset encountered an unqualified attachment override");
        // The full-size gameplay viewport and default targets have distinct
        // logical IDs but share the same native storage. Resetting those IDs
        // performs no format conversion or draw, even after an alpha pass.
        const bool sameBacking=s.cameraColor(*s.camera)==s.resources.resources().defaultColor&&
            s.cameraDepth(*s.camera)==s.resources.resources().defaultDepth;
        if(!sameBacking)for(uint32_t id:{0x134u,0x138u,0x13Cu,0x140u})
            if(s.renderState->effective().scalar(id))throw Failure("Expanded interpretation on a changed reset target is unqualified");
    } else if(s.camera->depthIdentity!=s.targetIds[1]) throw Failure("Native default color reset has a foreign depth attachment");
    s.backend.requireSelectedTargets({s.cameraColor(*s.camera),nullptr,nullptr,nullptr},s.cameraDepth(*s.camera));
    auto bound=[](uint32_t id) {return id && id!=0xFFFFFFFFu;};
    const uint32_t vs=PPC_LOAD_U32(0x82CD1A6C),ps=PPC_LOAD_U32(0x82CD1A70),decl=PPC_LOAD_U32(0x82CD1A68),index=PPC_LOAD_U32(0x82CD1A74);
    auto screenShader=[&](uint32_t id,bool vertex) {
        for(uint32_t i=0;i<State::screenFields.size();++i)if((vertex==State::screenVertexSlot(i))&&id==PPC_LOAD_U32(State::screenFields[i])){if(i<13||i==17)s.screenMaterial(base,i);else s.screenRecord(base,i);return true;}
        // Luma and screen-effect passes leave their PS cached when last in a frame.
        if(!vertex)for(const auto& [field,record]:State::originalPixelHandles)
            if(id==PPC_LOAD_U32(field)){s.requireOriginalPixelShader(base,id,field);return true;}
        return false;
    };
    try {
        if(bound(vs)&&!screenShader(vs,true))s.materials->requireOwned(vs,Graphics::MaterialStage::Vertex);
        if(bound(ps)&&!screenShader(ps,false))s.materials->requireOwned(ps,Graphics::MaterialStage::Pixel);
    } catch(const Failure& failure) {
        // Name the cached bindings so an intermittent stale reference is attributable from the log alone.
        // A cached shader id that is a guest address (not a native token) also names the first words behind it.
        const auto words=[&](uint32_t id)->std::string {
            if(id<0x80000000u||id==0xFFFFFFFFu)return {};
            char text[96];try {const auto* p=s.runtime.pointer(id,16,false);
                std::snprintf(text,sizeof(text)," [%02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X]",p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15]);
            } catch(...) {std::snprintf(text,sizeof(text)," [unmapped]");}
            return text;
        };
        char context[128];std::snprintf(context,sizeof(context)," (binding reset cache: vs=%08X ps=%08X decl=%08X index=%08X)",vs,ps,decl,index);
        throw Failure(std::string(failure.what())+context+" vs words"+words(vs)+" ps words"+words(ps));
    }
    if(bound(decl)) {
        bool screen=false;
        for(uint32_t i=0;i<2;++i)if(decl==s.screenDeclarationIds[i]) {
            if(PPC_LOAD_U32(0x82DFEB30+i*4)!=decl || !s.screenDeclarations.contains(s.screenDeclarationRecords[i]))
                throw Failure("Original screen declaration changed before binding reset");
            screen=true;
        }
        if(decl==s.immediateDeclarationId&&decl){
            if(PPC_LOAD_U32(0x82DFE350)!=decl)throw Failure("Immediate declaration publication changed before reset");
            screen=true;
        }
        if(decl==s.radialDeclarationId&&decl){if(PPC_LOAD_U32(0x82DFF284)!=decl)throw Failure("Radial declaration publication changed");screen=true;}
        if(!screen)s.scratch->requireDeclaration(decl);
    }
    if(bound(index) && !s.scratch->ownsIndex(index)) s.pipeline->index(index);
    for(uint32_t stream=0;stream<4;++stream) {
        const uint32_t id=PPC_LOAD_U32(0x82D0CAB0+16*stream);
        if(bound(id)&&!s.immediateBuffer(base,id))s.dynamic.buffer(id);
    }
    struct Saved {uint8_t* at;std::vector<uint8_t> bytes;};
    std::vector<Saved> saved;
    for(auto [address,length]:std::array<std::pair<uint32_t,uint32_t>,6>{{
        {0x82CD1A64,0x14},{0x82D0CAB0,0x40},{0x82D0D170,0x2FBC},
        {0x82E3D160,0xB24},{0x82D501E0,0x140},{0x82D0CF58,0x14}}}) {
        auto* p=s.runtime.pointer(address,length,true);saved.push_back({p,{p,p+length}});
    }
    EngineCpuCalls cpu(incoming,base);
    bool rebuilt=false,nativeCommit=false;
    s.bindingReset=true;s.resetTextureStages=0;
    try {
        s.renderState->preflightRebuild(cpu,base);
        for(uint32_t stage=0;stage<8;++stage) preflightResetNullTexture(base,0,stage);
        for(uint32_t field:{0x82CD1A64u,0x82CD1A68u,0x82CD1A6Cu,0x82CD1A70u,0x82CD1A74u}) PPC_STORE_U32(field,0xFFFFFFFF);
        for(uint32_t stream=0;stream<4;++stream) {
            PPC_STORE_U32(0x82D0CAB0+16*stream,0xFFFFFFFF);
            PPC_STORE_U32(0x82D0CAB4+16*stream,0);PPC_STORE_U32(0x82D0CAB8+16*stream,0);
        }
        for(uint32_t stage=0;stage<8;++stage) {
            cpu.registers().lr=0x823EFE2C;
            if(cpu.invoke(0x82401AF0,0,stage)!=1) throw Failure("Original null texture reset failed");
        }
        // These are the exact final CPU cache stores of the original null
        // index/stream/PS/declaration/VS helpers. No logical resources release.
        for(uint32_t field:{0x82CD1A68u,0x82CD1A6Cu,0x82CD1A70u,0x82CD1A74u}) PPC_STORE_U32(field,0);
        for(uint32_t stream=0;stream<4;++stream) PPC_STORE_U32(0x82D0CAB0+16*stream,0);
        // Original target cache stores precede82400D50. Include them in the
        // rollback snapshot while native attachments remain unchanged.
        PPC_STORE_U32(0x82D0CF5C,s.targetIds[0]);PPC_STORE_U32(0x82D0CF58,s.targetIds[1]);
        for(uint32_t slot=1;slot<4;++slot)PPC_STORE_U32(0x82D0CF5C+4*slot,0);
        cpu.registers().lr=0x823EFFCC;
        s.renderState->rebuild(cpu,base);
        rebuilt=true;
        if(s.resetTextureStages!=0xFF) throw Failure("Original state rebuild did not request all eight native null texture bindings");
        nativeCommit=true;
        const auto reset=s.backend.resetEngineBindings(s.resources.resources().defaultColor,s.resources.resources().defaultDepth,targetChanged);
        if(s.effects)s.effects->completeBindingReset(incoming,base,reset);
        s.im2dInput={};s.im2dDeclaration.reset();s.im2dStream.reset();
        s.camera->colorIdentity=s.targetIds[0];s.camera->depthIdentity=s.targetIds[1];
        if(targetChanged)s.camera->viewport={0,0,s.width,s.height,0,0x3F800000};
        ++s.bindingResets;incoming.r3.u64=cpu.registers().r3.u64;
        s.bindingReset=false;
        if(s.bindingResets<=8 || s.bindingResets%4096==0)fprintf(stderr,"[NATIVE ENGINE] mode-zero reset: eight texture/four stream/index/shader bindings cleared; original retained RW state rebuilt; %s; no draw\n",
            targetChanged?"default targets restored,viewport=1280x720 depth0..1,effective clip=0,0,1280,720":"viewport preserved");
    } catch(...) {
        s.bindingReset=false;s.resetTextureStages=0;
        if(rebuilt || nativeCommit) {
            s.ready=false;s.runtime.requestStop("Native binding reset failed during GPU publication");
        } else for(auto& range:saved) std::copy(range.bytes.begin(),range.bytes.end(),range.at);
        throw;
    }
}
uint64_t EngineDriver::bindingResetCount() const {state->requireCaller(state->runtime.base);return state->bindingResets;}
void EngineDriver::present(PPCContext& incoming,uint8_t* base,uint32_t raster) {
    if(CallSiteCounts::enabled()) {
        static uint64_t presents=0;
        if(++presents%600==0){cameraBindingCalls.dump(600);validatePoolCalls.dump(600);}
    }
    auto& s=*state;s.requireCaller(base);
    auto presentTimer=s.timing.measure(FrameTiming::Present);
    if(!s.ready || !s.submissionReady || s.presenting || s.bindingReset || !s.camera)
        throw Failure("Native presentation requires a live nonnested submission owner");
    requireContext(PPC_LOAD_U32(0x82D6D890));
    const auto camera=s.validateCamera(base,s.camera->camera);
    if(camera.camera!=PPC_LOAD_U32(0x82E07248) || raster!=camera.colorRaster || camera.colorRaster!=s.camera->colorRaster ||
       PPC_LOAD_U32(s.engine) || PPC_LOAD_U32(0x82E3DD60) || PPC_LOAD_U32(0x82D0CB1C) ||
       PPC_LOAD_U32(0x82D0CF5C)!=s.targetIds[0] || PPC_LOAD_U32(0x82D0CF58)!=s.targetIds[1])
        throw Failure("Native presentation requires the ended loading camera and selected default attachments");
    if(PPC_LOAD_U8(0x82D55BCE)) throw Failure("Native presentation capture mode is unimplemented");
    constexpr uint32_t p=0x82E3DCE0;
    if(PPC_LOAD_U32(p)!=s.width || PPC_LOAD_U32(p+4)!=s.height || PPC_LOAD_U32(p+8)!=0x182801B6 ||
       PPC_LOAD_U32(p+0x28)!=0x1A220197 || PPC_LOAD_U32(p+0x34)!=1 || PPC_LOAD_U32(p+0x38)!=1 ||
       PPC_LOAD_U32(p+0x3C)!=1 || PPC_LOAD_U32(p+0x40)!=0x28280136 ||
       PPC_LOAD_U32(p+0x68) || PPC_LOAD_U32(p+0x6C) || PPC_LOAD_U32(p+0x70) || PPC_LOAD_U32(p+0x74))
        throw Failure("Native presentation mode/scaling differs from the verified progressive profile");
    const uint32_t interval=s.renderState->effective().scalar(0x178);
    if(interval>1) throw Failure("Native presentation interval is unsupported");
    const uint32_t ring=PPC_LOAD_U32(0x82D0D0DC);
    if(ring>=4) throw Failure("Original presentation buffer ring is out of bounds");
    s.runtime.pointer(0x82D0D0DC,20,true);
    for(uint32_t i=0;i<4;++i) {
        const uint32_t id=PPC_LOAD_U32(0x82D0D100+4*i),size=PPC_LOAD_U32(0x82D0D0F0+4*i);
        if(s.dynamic.buffer(id)->byteSize()!=size || PPC_LOAD_U32(0x82D0D0E0+4*i)>size)
            throw Failure("Original presentation buffer cursor/owner is invalid");
    }
    const uint32_t oldFront=s.frontRoles[0],oldOther=s.frontRoles[1];
    if(oldFront==oldOther || !oldFront || !oldOther) throw Failure("Native presentation front roles alias");
    s.runtime.pointer(0x82D0CF8C,16,true);
    {
        // Prove the previous frame's queued front copy/transfer completed before
        // its history receipt is consulted. The GPU normally finished it while
        // the CPU built this frame, so this rarely blocks.
        auto copyWaitTimer=s.timing.measure(FrameTiming::PresentCopy);
        s.completePendingPresentation();
    }
    const uint32_t previous=PPC_LOAD_U32(0x82D0CF94),older=PPC_LOAD_U32(0x82D0CF98);
    for(uint32_t id:{previous,older}) if(id) {
        const auto found=s.runtime.graphicsPresentReceipts.find(id);
        if(found==s.runtime.graphicsPresentReceipts.end() || !found->second.submitted ||
           !found->second.copyCompleted || !found->second.displayTransferred)
            throw Failure("Original presentation history has no completed native owner");
    }
    bool alphaOne=false;
    const auto source=s.resources.resources().defaultColor,front=color(oldFront,alphaOne);
    if(!alphaOne) throw Failure("Native presentation destination is not an owned front");
    applyNativeVideoSettings(s.runtime);
    s.backend.configureVideoPresentation(s.runtime.vsyncEnabled,s.runtime.window->presentationWidth.load(),s.runtime.window->presentationHeight.load());
    s.backend.validateFrontCopy(source,front);
    s.backend.validateFrontPresentation(front);
    EngineCpuCalls cpu(incoming,base); // Validate callback stack before any GPU/role publication.
    const uint32_t receipt=presentIdentity(s.runtime);
    auto [position,inserted]=s.runtime.graphicsPresentReceipts.emplace(receipt,GraphicsPresentReceipt{s.contextId,s.targetIds[0],oldFront});
    if(!inserted) throw Failure("Native presentation receipt was reused");
    auto& record=position->second;
    s.presenting=true;
    // The native limiter holds this present until its even-cadence release time and
    // is not part of any timing bucket: the completed presentation interval is the
    // performance measure, so the wait is included in the frame interval itself.
    if(s.runtime.requestedFrameRate&&s.runtime.requestedFrameRate!=60)SimpsonsNativeFramePace(s.runtime);
    if(s.runtime.requestedFrameRate || s.runtime.uncappedFrameRate)s.runtime.nativePresentationTimebase=PPCQueryTimebase();
    // Once-per-frame full recording validation (deferred from 58 per-mutation
    // validations per frame). Catches any LRU/free/topology/payload/accounting
    // divergence since last present (all mutations for this frame) before any
    // front copies/presents (fail-stop, no corrupted frame displayed).
    // Per-mutation paths retain targeted safety checks + idleHistory (cheap);
    // rendering uses payloads (not validation-only LRU/free/head/accounting).
    s.runtime.engineDriver->recordingOwners().validateFrame(s.runtime.base);
    try {
        // Original CF90/CF8C rotation precedes resolve. IDs still name the same
        // immutable native backing throughout rotation and future lookups.
        s.frontRoles={oldOther,oldFront};
        PPC_STORE_U32(0x82D0CF90,oldOther);PPC_STORE_U32(0x82D0CF8C,oldFront);
        auto copyTimer=s.timing.measure(FrameTiming::PresentCopy);
        s.submittedCopy=s.backend.copyFront(source,front);
        record.submitted=true;++s.frontCopies;
        PPC_STORE_U32(0x82D0CF98,previous);PPC_STORE_U32(0x82D0CF94,receipt);
        copyTimer.finish(); // Scene->front submission is queued; its actual wait follows display submission below.
        ++s.presentAttempts;
        std::shared_ptr<Graphics::NativeCopySubmission> transfer;
        {
            auto displayTimer=s.timing.measure(FrameTiming::PresentDisplay);bool accepted=false;
            transfer=s.backend.presentFrontQueued(front,accepted);record.displayAccepted=accepted;
        }
        if(record.displayAccepted && s.runtime.window) s.runtime.window->recordPresentedFrame();
        // copyCompleted/displayTransferred stay false until the queued transfer
        // really completes (next presentation, capture, idle or receipt query).
        s.pendingPresentation=State::PendingPresentation{receipt,s.submittedCopy,transfer};
        // Native uploads snapshot guest bytes into GPU-ordered copies before
        // returning, so resetting the original CPU cursors cannot race queued GPU
        // reads. Every earlier presentation's GPU work has completed (checked above).
        cpu.registers().lr=0x823EE8B0;cpu.invoke(0x823FC5B8);
        if(PPC_LOAD_U32(0x82D0D0DC)!=(ring+1)%4) throw Failure("Original presentation ring did not advance");
        for(uint32_t i=0;i<4;++i) if(PPC_LOAD_U32(0x82D0D0E0+4*i))
            throw Failure("Original presentation cursor reset was incomplete");
        // Only the two guest history words retain metadata. GPU leases have
        // completed; older identities remain unreused and unknown consumers fail.
        if(older && older!=previous) s.runtime.graphicsPresentReceipts.erase(older);
        s.submittedCopy.reset();s.presenting=false;incoming.r3.u64=1;
        const auto movies=s.backend.movieDrawCount();
        const auto rigidDraws=s.backend.rigidMeshDrawCount(),skinDraws=s.backend.skinMeshDrawCount(),skyDraws=s.backend.skyMeshDrawCount();
        const auto sceneDraws=rigidDraws+skinDraws+skyDraws;
        if(sceneDraws<s.presentedSceneGeometryDraws)throw Failure("Native scene geometry draw receipt regressed");
        const auto frameSceneDraws=sceneDraws-s.presentedSceneGeometryDraws;
        // Snapshot every completed presentation, not just captured frames.
        // Menus/movies, depth-only draws, and previous frames cannot supply
        // the positive scene-geometry receipt for this frame.
        s.presentedSceneGeometryDraws=sceneDraws;
        // Compare the original full-size viewport's post-scene depth-copy
        // receipt on every presentation. An uncaptured intervening frame must
        // never make an older depth snapshot look fresh at the next capture.
        bool freshSceneDepthCopy=false;
        try {
            const auto depthCopies=s.viewportSurfaces->depthCopyCount();
            const auto sceneCamera=worldSceneCameraId(s.runtime);
            freshSceneDepthCopy=frameSceneDraws>0 && depthCopies>s.presentedDepthCopies &&
                sceneCamera && s.viewportSurfaces->depthCopyCamera()==sceneCamera;
            s.presentedDepthCopies=depthCopies;
        } catch(...) {
            // Optional telemetry cannot invalidate a completed presentation.
        }
        const auto draws=s.backend.screenDrawCount()+s.backend.im2dDrawCount()+movies+s.backend.edgeDrawCount()+s.backend.aaDrawCount()+s.backend.edgeAADrawCount()+s.backend.shadowMeshDrawCount()+s.backend.zprepassMeshDrawCount()+s.backend.monoMeshDrawCount()+s.backend.postFilterDrawCount()+sceneDraws;
        fprintf(stderr,"[NATIVE PRESENT] copy=%llu receipt=%08X front=%08X completed=1 display=%s ring=%u; original geometry draws=%llu; scene_geometry_draws=%llu frame_scene_geometry_draws=%llu; display gamma/pacing unverified\n",
            static_cast<unsigned long long>(s.frontCopies),receipt,oldFront,record.displayAccepted?"accepted":"occluded",(ring+1)%4,
            static_cast<unsigned long long>(draws),static_cast<unsigned long long>(sceneDraws),static_cast<unsigned long long>(frameSceneDraws));
        if(streamWatchData&&streamWatchPrints<6) {
            try {
                s.runtime.pointer(streamWatchData,32,false);
                std::fprintf(stderr,"[NATIVE STREAM WATCH @%08X]",streamWatchData);
                for(uint32_t i=0;i<8;++i) std::fprintf(stderr," %08X",PPC_LOAD_U32(streamWatchData+4*i));
                std::fprintf(stderr,"\n");
            } catch(...) { std::fprintf(stderr,"[NATIVE STREAM WATCH] unreadable\n"); }
            ++streamWatchPrints;
        }
        // Keep the first movie frames even after loading artwork fills the
        // startup allowance, then sample playback without unbounded captures.
        const bool movieSample=movies>s.capturedMovieDraws &&
            (movies<=5 || (movies<=300 && movies/30>s.capturedMovieDraws/30) ||
             (movies<=18000 && movies/300>s.capturedMovieDraws/300));
        const bool interfaceSample=s.im2dDepthDraws>s.capturedIm2DDepthDraws &&
            (s.im2dDepthDraws<=8 || (s.im2dDepthDraws<=18000 && s.im2dDepthDraws/60>s.capturedIm2DDepthDraws/60));
        const bool texturedSample=s.im2dTexturedDraws && (!s.capturedIm2DTexturedDraws ||
            (s.im2dTexturedDraws<=18000 && s.im2dTexturedDraws/60>s.capturedIm2DTexturedDraws/60));
        // Preserve later menu transitions after the startup/texture allowances
        // are consumed. At most 120 additional complete-frame readbacks.
        const bool periodicSample=s.frontCopies<=7200 && s.frontCopies%60==0;
        const bool sampled=!s.runtime.captureOnRequest && (s.capturedFrames<32 || movieSample || interfaceSample || texturedSample || periodicSample);
        // Steady fast path: no path/JSON allocation when no capture will be
        // taken. Telemetry observations are still consumed every frame via
        // discard() so stale data is never reused; formatting happens only on
        // frames that actually capture.
        const bool captureDirSet=!s.runtime.frameCaptureDirectory.empty();
        bool requested=false;
        if(captureDirSet) {
            const auto captureRequest=s.runtime.frameCaptureDirectory/"capture.request";
            requested=std::filesystem::exists(captureRequest);
        }
        const bool capturing=captureDirSet && draws>s.capturedDraws && (requested || sampled);
        std::string playerTelemetry;
        if(capturing) playerTelemetry=s.runtime.playerTelemetry.takeJson();
        else s.runtime.playerTelemetry.discard();
        if(capturing) {
            s.completePendingPresentation(); // The metadata below claims a completed front copy.
            std::filesystem::create_directories(s.runtime.frameCaptureDirectory);
            char name[80];std::snprintf(name,sizeof(name),"native-frame-%04llu",static_cast<unsigned long long>(draws));
            const auto stem=s.runtime.frameCaptureDirectory/name;
            const auto presented=s.backend.presentedFront();
            if(!presented)throw Failure("Native frame capture has no completed presentation target");
            const auto bytes=s.backend.readbackTarget(presented);
            auto sceneDepthTelemetry=sceneDepthGridJson({},s.frontCopies,false);
            if(freshSceneDepthCopy) {
                const auto started=std::chrono::steady_clock::now();
                SceneDepthGridStats gridStats;
                try {
                    const auto depthBytes=s.viewportSurfaces->readbackDepthTexture(0x82DFE840);
                    sceneDepthTelemetry=sceneDepthGridJson(depthBytes,s.frontCopies,true,&gridStats,front->pixelWidth(),front->pixelHeight());
                } catch(const std::exception& error) {
                    std::fprintf(stderr,"[NATIVE DEPTH TELEMETRY] presentation=%llu unavailable: %s\n",
                        static_cast<unsigned long long>(s.frontCopies),error.what());
                } catch(...) {
                    std::fprintf(stderr,"[NATIVE DEPTH TELEMETRY] presentation=%llu unavailable: unknown readback error\n",
                        static_cast<unsigned long long>(s.frontCopies));
                }
                const auto elapsed=std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-started).count();
                std::fprintf(stderr,"[NATIVE DEPTH TELEMETRY] presentation=%llu available=%u readback_encode_ms=%.3f raw_min=%.9g raw_max=%.9g raw_distinct=%u encoded_distinct=%u\n",
                    static_cast<unsigned long long>(s.frontCopies),
                    sceneDepthTelemetry.find("\"available\":true")!=std::string::npos,elapsed,
                    gridStats.rawMin,gridStats.rawMax,gridStats.distinctRaw,gridStats.distinctEncoded);
            }
            std::ofstream raw(stem.string()+".rgb10a2",std::ios::binary);raw.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
            if(!raw)throw Failure("Native frame capture could not write packed pixels");
            std::ofstream metadata(stem.string()+".json");
            const auto commits=s.renderState?s.renderState->commitCounts():EngineRenderState::CommitCounts{};
            metadata<<"{\"width\":"<<presented->pixelWidth()<<",\"height\":"<<presented->pixelHeight()
                <<",\"scene_width\":"<<front->pixelWidth()<<",\"scene_height\":"<<front->pixelHeight()
                <<",\"antialiasing\":"<<uint32_t(s.backend.antialiasing())<<",\"format\":\"R10G10B10A2_UNORM_LE\",\"draws\":"<<draws
                <<",\"screen_draws\":"<<s.backend.screenDrawCount()<<",\"im2d_draws\":"<<s.backend.im2dDrawCount()
                <<",\"movie_draws\":"<<movies
                <<",\"edge_draws\":"<<s.backend.edgeDrawCount()
                <<",\"aa_draws\":"<<s.backend.aaDrawCount()
                <<",\"edgeaa_draws\":"<<s.backend.edgeAADrawCount()
                <<",\"shadow_mesh_draws\":"<<s.backend.shadowMeshDrawCount()
                <<",\"zprepass_draws\":"<<s.backend.zprepassMeshDrawCount()
                <<",\"post_filter_draws\":"<<s.backend.postFilterDrawCount()
                <<",\"mono_mesh_draws\":"<<s.backend.monoMeshDrawCount()
                <<",\"rigid_mesh_draws\":"<<rigidDraws<<",\"skin_mesh_draws\":"<<skinDraws<<",\"sky_mesh_draws\":"<<skyDraws
                <<",\"scene_geometry_draws\":"<<sceneDraws<<",\"frame_scene_geometry_draws\":"<<frameSceneDraws
                <<",\"im2d_depth_draws\":"<<s.im2dDepthDraws
                <<",\"im2d_textured_draws\":"<<s.im2dTexturedDraws
                <<",\"im2d_native_draw_calls\":"<<s.backend.im2dNativeDrawCount()
                <<",\"native_buffer_upload_calls\":"<<s.backend.bufferUploadCount()
                <<",\"state_commit_attempts\":"<<commits.calls<<",\"state_empty_commits\":"<<commits.empty
                <<",\"state_scalar_entries\":"<<commits.scalarEntries<<",\"state_stage_entries\":"<<commits.stageEntries
                <<",\"presentation\":"<<s.frontCopies<<",\"front_identity\":"<<oldFront
                <<",\"telemetry\":{\"schema_version\":1,\"presentation\":"<<s.frontCopies
                <<",\"player\":"<<playerTelemetry<<",\"world\":"<<worldTelemetryJson(s.runtime,s.frontCopies)
                <<",\"scene_depth_grid\":"<<sceneDepthTelemetry<<"}"
                <<",\"capture_source\":\"completed_front_renderer_readback\",\"front_copy_completed\":true,\"display_accepted\":"
                <<(record.displayAccepted?"true":"false")<<",\"alpha_ignored_by_display\":true}\n";
            raw.close();metadata.close();
            if(!raw || !metadata)throw Failure("Native frame capture could not finish pixels/metadata");
            if(requested)std::filesystem::remove(s.runtime.frameCaptureDirectory/"capture.request");
            s.capturedDraws=draws;s.capturedMovieDraws=movies;s.capturedIm2DDepthDraws=s.im2dDepthDraws;
            s.capturedIm2DTexturedDraws=s.im2dTexturedDraws;++s.capturedFrames;
            fprintf(stderr,"[NATIVE CAPTURE] preserved completed front readback draws=%llu display=%s; no color correction or overlays\n",
                static_cast<unsigned long long>(draws),record.displayAccepted?"accepted":"occluded");
        }
        if(s.menuFrameRequested.load(std::memory_order_acquire)) {
            std::lock_guard lock(s.menuFrameMutex);
            if(s.menuFrameRequested.load(std::memory_order_relaxed)) {
                uint32_t frameWidth{},frameHeight{};
                auto pixels=readbackMenuFrame(frameWidth,frameHeight);
                s.menuFrameWidth=frameWidth;s.menuFrameHeight=frameHeight;
                s.menuFramePixels=std::move(pixels);s.menuFrameCompleted=true;
                s.menuFrameRequested.store(false,std::memory_order_release);
                s.menuFrameReady.notify_one();
            }
        }
        presentTimer.finish();s.timing.frame(s.frontCopies,record.displayAccepted);
    } catch(...) {
        if(s.menuFrameRequested.load(std::memory_order_acquire)) {
            std::lock_guard lock(s.menuFrameMutex);s.menuFrameError=std::current_exception();
            s.menuFrameCompleted=true;s.menuFrameReady.notify_one();
        }
        s.presenting=false;s.ready=false;
        s.runtime.requestStop("Native presentation failed after front-role/GPU publication");
        throw; // Keep submitted leases until terminal GPU retirement; no fabricated CPU/GPU rollback.
    }
}
uint64_t EngineDriver::presentationCount() const {state->requireCaller(state->runtime.base);return state->backend.presentationCount();}
double EngineDriver::renderAspect() const {state->requireCaller(state->runtime.base);return state->backend.renderAspect();}
void EngineDriver::setUiDrawing(bool drawing) {state->requireCaller(state->runtime.base);state->uiDrawing=drawing;}
uint64_t EngineDriver::presentationAttemptCount() const {state->requireCaller(state->runtime.base);return state->presentAttempts;}
uint64_t EngineDriver::frontCopyCount() const {state->requireCaller(state->runtime.base);return state->frontCopies;}
std::vector<uint8_t> EngineDriver::readbackMenuFrame(uint32_t& width,uint32_t& height) {
    auto& s=*state;
    if(GetCurrentThreadId()!=s.thread) {
        StallProfiler::Scope menuLock(StallProfiler::Section::Wait,"EngineDriver::readbackMenuFrame mutex",currentContext,
            uint64_t(reinterpret_cast<uintptr_t>(&s.menuFrameMutex)));
        std::unique_lock lock(s.menuFrameMutex);s.runtime.checkRunning();
        menuLock.finish();
        if(s.menuFrameWaiting)throw Failure("Native menu background already has a requester");
        s.menuFrameWaiting=true;s.menuFrameCompleted=false;s.menuFrameError=nullptr;s.menuFramePixels.clear();
        s.menuFrameRequested.store(true,std::memory_order_release);
        struct Retire {State& owner;~Retire(){owner.menuFrameWaiting=false;owner.menuFrameRequested.store(false,std::memory_order_release);}} retire{s};
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        StallProfiler::Scope menuWait(StallProfiler::Section::Wait,"EngineDriver::readbackMenuFrame completion",currentContext,
            uint64_t(reinterpret_cast<uintptr_t>(&s.menuFrameReady)));
        while(!s.menuFrameCompleted) {
            s.menuFrameReady.wait_for(lock,std::chrono::milliseconds(16));s.runtime.checkRunning();
            if(std::chrono::steady_clock::now()>=deadline&&!s.menuFrameCompleted)
                throw Failure("Native menu background request did not reach a presentation within five seconds");
        }
        menuWait.finish();
        if(s.menuFrameError)std::rethrow_exception(s.menuFrameError);
        width=s.menuFrameWidth;height=s.menuFrameHeight;return std::move(s.menuFramePixels);
    }
    s.requireCaller(s.runtime.base);
    if(!s.ready || s.presenting || !s.presentAttempts)
        throw Failure("Native menu background requires an ended presented frame");
    s.completePendingPresentation();bool alphaOne=false;
    const auto front=color(s.frontRoles[1],alphaOne);
    if(!alphaOne || front->format!=Graphics::TargetFormat::RGB10A2)
        throw Failure("Native menu background is not an owned RGB10A2 front");
    width=front->pixelWidth();height=front->pixelHeight();
    auto pixels=s.backend.readbackTarget(front);
    if(pixels.size()!=size_t(width)*height*4)throw Failure("Native menu background extent differs");
    for(size_t i=0;i<pixels.size();i+=4){
        uint32_t packed{};std::memcpy(&packed,pixels.data()+i,4);
        pixels[i]=uint8_t((packed&1023)*255/1023);
        pixels[i+1]=uint8_t(((packed>>10)&1023)*255/1023);
        pixels[i+2]=uint8_t(((packed>>20)&1023)*255/1023);pixels[i+3]=255;
    }
    return pixels;
}
bool EngineDriver::submissionCompleted(uint32_t receipt) const {
    state->requireCaller(state->runtime.base);
    const auto found=state->runtime.graphicsPresentReceipts.find(receipt);
    if(!receipt || found==state->runtime.graphicsPresentReceipts.end()) throw Failure("Unknown or retired native presentation receipt");
    // A still-queued latest presentation reports its real event state; it is
    // recorded as completed only once its transfer event actually signals.
    auto& pending=state->pendingPresentation;
    if(pending && pending->receipt==receipt && state->backend.copyComplete(pending->transfer))
        state->completePendingPresentation();
    return found->second.submitted && found->second.copyCompleted;
}
bool EngineDriver::waitSubmission(uint32_t receipt) {
    state->requireCaller(state->runtime.base);
    const auto found=state->runtime.graphicsPresentReceipts.find(receipt);
    if(!receipt || found==state->runtime.graphicsPresentReceipts.end()) throw Failure("Unknown or retired native presentation receipt");
    if(state->pendingPresentation && state->pendingPresentation->receipt==receipt) state->completePendingPresentation();
    return found->second.submitted && found->second.copyCompleted;
}
uint32_t EngineDriver::pipelineResetField(uint8_t* base,uint32_t index,bool apply) {
    auto& s=*state;s.requireCaller(base);
    if(!s.ready || !s.submissionReady) throw Failure("Native pipeline state reset requires registered driver submission");
    requireContext(PPC_LOAD_U32(0x82D6D890));
    return s.renderState->pipelineResetField(base,index,apply);
}
void EngineDriver::selectCamera(uint8_t* base,uint32_t c) {
    auto& s=*state;const auto binding=s.validateCamera(base,c);const bool changed=!s.camera||s.camera->camera!=c;s.bindCamera(base,binding);
    if(changed){static thread_local uint32_t selectedCameraSample{};
    if(sampleHotLog(selectedCameraSample))
        std::fprintf(stderr,"[NATIVE CAMERA] selected camera=%08X color=%08X depth=%08X viewport=%ux%u; original target roles, no draw\n",
            c,binding.colorIdentity,binding.depthIdentity,binding.viewport[2],binding.viewport[3]);}
}
void EngineDriver::preflightCameraPass(uint8_t* base,uint32_t c,bool begin) const {
    auto& s=*state;s.validateCamera(base,c);
    const uint32_t activeCamera=PPC_LOAD_U32(0x82E3DD60),inPass=PPC_LOAD_U32(0x82D0CB1C);
    if(inPass>1 || (activeCamera && activeCamera!=c) || (begin && PPC_LOAD_U32(s.engine)!=c))
        throw Failure("Native camera pass has inconsistent original current-camera ownership");
    // Matrix, frame synchronization, allocation and begin/end stores remain
    // original AOT code. No native success code or projection is substituted.
}
void EngineDriver::clearCamera(uint8_t* base,uint32_t c,uint32_t rgba,uint32_t selector) {
    auto& s=*state;
    const auto binding=s.validateCamera(base,c);
    if(selector>7) throw Failure("Original camera clear selector is outside 0..7");
    if(s.renderState->effective().scalar(Graphics::ScalarState::ScissorEnable))
        throw Failure("Native camera clear requires the original pre-scissor full-target request");
    constexpr std::array<uint32_t,8> masks={0,0xF,0x10,0x1F,0x20,0x2F,0x30,0x3F};
    if(PPC_LOAD_U32(0x82062AA0+4*selector)!=masks[selector]) throw Failure("Original camera clear mask table changed");
    uint32_t mask=masks[selector];
    if(!binding.depthRaster) mask&=~0x30u;
    std::array<float,4> color{};
    std::array<uint8_t,4> bytes{};
    if(selector&1) {
        const auto* p=s.runtime.pointer(rgba,4,false);
        for(uint32_t i=0;i<4;++i) {
            bytes[i]=p[i];
            if(p[i]!=0 && p[i]!=255) throw Failure("Native camera clear requires exact endpoint RGBA bytes; general RGB10A2 conversion is unverified");
            color[i]=p[i]?1.0f:0.0f;
        }
    }
    const uint32_t stencil=PPC_LOAD_U32(0x82D0CB14);
    if((mask&0x20) && stencil>255) throw Failure("Native camera clear stencil is outside the canonical byte range");
    for(uint32_t id:{0x134u,0x138u,0x13Cu,0x140u})
        if(s.renderState->effective().scalar(id)) throw Failure("Expanded target interpretation is unverified for native camera clear");
    // Every unsupported argument/ownership/region check precedes native binding
    // or pixel mutation. A device failure during submission remains terminal.
    s.bindCamera(base,binding);
    if(mask&0xF) s.backend.clearTarget(s.cameraColor(binding),color);
    if(mask&0x30) s.backend.clearDepthTarget(s.cameraDepth(binding),0.0f,uint8_t(stencil),bool(mask&0x10),bool(mask&0x20));
    ++s.cameraClears;
    {static thread_local uint32_t clearCameraSample{};
    if(sampleHotLog(clearCameraSample))
        fprintf(stderr,"[NATIVE CAMERA] clear camera=%08X selector=%u mask=%02X RGBA=%02X%02X%02X%02X depth=0 stencil=%X region=%ux%u; no draw or present\n",
            c,selector,mask,bytes[0],bytes[1],bytes[2],bytes[3],stencil,binding.viewport[2],binding.viewport[3]);}
}
NativeCameraBinding EngineDriver::cameraBinding() const {
    cameraBindingCalls.note(__builtin_return_address(0));
    auto& s=*state;s.requireCaller(s.runtime.base);
    if(!s.camera) throw Failure("Native camera binding has no live selection");
    const auto current=s.validateCamera(s.runtime.base,s.camera->camera);
    if(current.colorRaster!=s.camera->colorRaster || current.depthRaster!=s.camera->depthRaster)
        throw Failure("Native camera raster association changed after selection");
    return *s.camera;
}
uint64_t EngineDriver::cameraClearCount() const {state->requireCaller(state->runtime.base);return state->cameraClears;}
void EngineDriver::copyCameraTargets(const PPCContext& incoming,uint8_t* base,uint32_t colorDestination,uint32_t depthDestination,uint32_t c) {
    auto& s=*state;const auto binding=s.validateCamera(base,c);requireContext(PPC_LOAD_U32(0x82D5DA74));
    if(!s.camera || s.camera->camera!=c || s.camera->colorIdentity!=binding.colorIdentity || s.camera->depthIdentity!=binding.depthIdentity ||
       PPC_LOAD_U32(s.engine)!=c || PPC_LOAD_U32(0x82E3DD60)!=c || PPC_LOAD_U32(0x82D0CB1C)!=1 ||
       PPC_LOAD_U32(0x82D0CF5C)!=binding.colorIdentity || PPC_LOAD_U32(0x82D0CF58)!=binding.depthIdentity)
        throw Failure("Native viewport copy requires its original active camera and selected native attachments");
    // Keep the original camera-to-manager rectangle lookup AOT, including the
    // fallback to camera raster dimensions. No copied guest pointer outlives us.
    std::array<uint32_t,4> rect{};
    {
        EngineCpuCalls cpu(incoming,base);const auto out=cpu.registers().r1.u32+0x80;
        cpu.registers().lr=0x826B08EC;cpu.invoke(0x8269D388,out,c);
        rect={PPC_LOAD_U32(out),PPC_LOAD_U32(out+4),PPC_LOAD_U32(out+8),PPC_LOAD_U32(out+12)};
    }
    if(!colorDestination&&!depthDestination)return; // Original helper issues neither operation.
    if(rect!=std::array<uint32_t,4>{0,0,s.width,s.height} || binding.viewport[2]!=s.width || binding.viewport[3]!=s.height)
        throw Failure("Native viewport copy is qualified only for the full 1280x720 rectangle; partial transfer is unported");
    // These are separate original texture roles. A color/depth mix-up or any
    // unqualified destination fails before either native GPU operation.
    std::shared_ptr<Graphics::RenderTarget> colorTarget;
    if(colorDestination==s.targetIds[5]) colorTarget=s.resources.resources().colorCopy;
    else if(colorDestination&&s.sceneCopies->owns(colorDestination)) {
        if(depthDestination)throw Failure("Original scene color copy unexpectedly requests a depth destination");
        colorTarget=s.sceneCopies->destination(incoming,colorDestination,c);
    }
    if((colorDestination&&!colorTarget) || (depthDestination&&depthDestination!=s.targetIds[2]))
        throw Failure("Native viewport copy destination is not the original shared color/depth copy role");
    const auto colorSource=s.cameraColor(binding);const auto depthSource=s.cameraDepth(binding);
    const auto& targets=s.resources.resources();
    if(colorDestination)s.backend.validateFrontCopy(colorSource,colorTarget);
    if(depthDestination)s.backend.validateDepthCopy(depthSource,targets.depthCopy);
    // The backend retains the owned resources until its GPU event completes,
    // including when the caller discards the token or the device later fails.
    if(colorDestination){
        s.backend.copyFront(colorSource,colorTarget);++s.cameraCopies;
        if(s.sceneCopies->owns(colorDestination))s.sceneCopies->copied(colorDestination,c);
        else s.sharedColorCopiedCamera=c;
    }
    if(depthDestination){s.backend.copyDepth(depthSource,targets.depthCopy);++s.cameraCopies;s.sharedDepthCopiedCamera=c;}
    {static thread_local uint32_t viewportCopySample{};
    if(sampleHotLog(viewportCopySample))
        std::fprintf(stderr,"[NATIVE VIEWPORT COPY] camera=%08X source_color=%08X source_depth=%08X destination_color=%08X destination_depth=%08X rect=0,0,1280,720; native resource copy, no clear/draw/present\n",
            c,binding.colorIdentity,binding.depthIdentity,colorDestination,depthDestination);}
}
uint64_t EngineDriver::cameraCopyCount() const {state->requireCaller(state->runtime.base);return state->cameraCopies;}
std::shared_ptr<Graphics::RenderTarget> EngineDriver::sampledColorCopy(uint32_t id,uint32_t camera) const {
    auto& s=*state;s.requireCaller(s.runtime.base);
    if(!s.ready || id!=s.targetIds[5] || !camera || s.sharedColorCopiedCamera!=camera || cameraBinding().camera!=camera)
        throw Failure("Shared color sampling lacks its original copy/camera ownership");
    return s.resources.resources().colorCopy;
}
std::shared_ptr<Graphics::DepthTarget> EngineDriver::sampledDepthCopy(uint32_t id,uint32_t camera) const {
    auto& s=*state;s.requireCaller(s.runtime.base);
    if(!s.ready || id!=s.targetIds[2] || !camera || s.sharedDepthCopiedCamera!=camera || cameraBinding().camera!=camera)
        throw Failure("Shared depth sampling lacks its original copy/camera ownership");
    return s.resources.resources().depthCopy;
}
void EngineDriver::screenDeclaration(PPCContext& ctx,uint8_t* base,uint32_t index,bool release) {
    auto& s=*state;s.requireCaller(base);requireContext(PPC_LOAD_U32(0x82D5DA74));
    if(index>1 || ctx.r31.u32!=0x82DFEA20)throw Failure("Original screen declaration owner changed");
    const uint32_t field=0x82DFEB30+index*4;
    s.runtime.pointer(field,4,true);
    auto& id=s.screenDeclarationIds[index];auto& record=s.screenDeclarationRecords[index];
    if(release) {
        if(!id || ctx.r3.u32!=id || PPC_LOAD_U32(field)!=id || !s.screenDeclarations.contains(record))
            throw Failure("Unknown or stale original screen declaration release");
        s.screenDeclarations.release(record);id=0;record={};ctx.r3.u64=0;ctx.lr=index?0x82752300:0x827522F0;
        return; // Original following stores clear the fields.
    }
    const uint32_t source=index?0x82151724:0x82151748;
    if(ctx.r3.u32!=source || id || PPC_LOAD_U32(field))throw Failure("Original screen declaration creation differs");
    constexpr std::array<uint32_t,6> flat={0,0x002C23A5,0,0x00FF0000,0xFFFFFFFF,0};
    constexpr std::array<uint32_t,9> textured={0,0x002C23A5,0,8,0x002C23A5,0x00050000,0x00FF0000,0xFFFFFFFF,0};
    const auto words=index?std::span<const uint32_t>(textured):std::span<const uint32_t>(flat);
    const auto size=uint32_t(words.size()*4);const auto* bytes=s.runtime.pointer(source,size,false);
    for(uint32_t i=0;i<words.size();++i)if(PPC_LOAD_U32(source+i*4)!=words[i])throw Failure("Original screen declaration bytes changed");
    const auto created=s.screenDeclarations.create({bytes,size});
    try {id=allocateTargetIdentity();}catch(...){s.screenDeclarations.release(created);throw;}
    record=created;ctx.r3.u64=id;ctx.lr=index?0x827521DC:0x827521C8;
    std::fprintf(stderr,"[NATIVE SCREEN] owned declaration source=%08X id=%08X stride=%u\n",source,id,index?16:8);
}
void EngineDriver::preflightScreen(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.requireCaller(base);
    if(uint64_t(ctx.r8.u32)+16>0x100000000ull)throw Failure("Original screen color crosses guest bounds");
    s.runtime.pointer(ctx.r8.u32+12,4,false);
    const float alpha=std::bit_cast<float>(PPC_LOAD_U32(ctx.r8.u32+12));
    if(!std::isfinite(alpha))throw Failure("Nonfinite original screen alpha");
    if(alpha<std::bit_cast<float>(uint32_t{0x3C010204}))return;
    const auto binding=cameraBinding();
    if(ctx.r3.u32!=binding.camera || ctx.r3.u32!=PPC_LOAD_U32(s.engine) ||
       ctx.r3.u32!=PPC_LOAD_U32(0x82E3DD60) || PPC_LOAD_U32(0x82D0CB1C)!=1)
        throw Failure("Original screen entry camera is not the active native pass");
}
void EngineDriver::preflightIm2DUpload(PPCContext& c,uint8_t* base,bool triangle) {
    auto& s=*state;auto preflightTimer=s.timing.measure(FrameTiming::Im2DPreflight);s.requireCaller(base);requireContext(PPC_LOAD_U32(0x82D5DA74));
    if(s.im2d.active || s.im2d.staging)throw Failure("Nested or unfinished original Im2D upload");
    const auto camera=cameraBinding();
    // 824090A8 selects three vertices from r3 using r5/r6/r7. Its original
    // scalar loads, raster-offset additions and stores remain in the AOT body.
    const uint32_t source=triangle?c.r3.u32:c.r4.u32,count=triangle?3u:c.r5.u32;
    const uint32_t sourceCount=triangle?c.r4.u32:count,kind=triangle?3u:c.r3.u32;
    const std::array<uint32_t,3> indices={c.r5.u32,c.r6.u32,c.r7.u32};
    if(triangle && (!sourceCount || sourceCount>9362 ||
       std::any_of(indices.begin(),indices.end(),[&](uint32_t i){return i>=sourceCount;})))
        throw Failure("Original Im2D triangle index is outside its bounded source");
    const bool primitive=kind==4 || (kind==3 && count%3==0);
    if(!primitive || count<3 || count>9362 ||
       camera.camera!=PPC_LOAD_U32(0x82E3DD60) || camera.camera!=PPC_LOAD_U32(s.engine) ||
       PPC_LOAD_U32(0x82D0CB1C)!=1 || PPC_LOAD_U32(0x82D101C4) ||
       !(PPC_LOAD_U32(0x82E3DFBC)&0x10000) || PPC_LOAD_U32(camera.colorRaster+0x1C))
    {
        std::fprintf(stderr,"[IM2D ENTRY REJECTED] primitive=%u source=%08X vertices=%u lr=%08X sp=%08X camera=%08X active=%08X engine_camera=%08X pass=%u buffer_mode=%u capability=%08X subraster=%08X texture=%08X\n",
            c.r3.u32,c.r4.u32,c.r5.u32,uint32_t(c.lr),c.r1.u32,camera.camera,PPC_LOAD_U32(0x82E3DD60),
            PPC_LOAD_U32(s.engine),PPC_LOAD_U32(0x82D0CB1C),PPC_LOAD_U32(0x82D101C4),PPC_LOAD_U32(0x82E3DFBC),
            PPC_LOAD_U32(camera.colorRaster+0x1C),PPC_LOAD_U32(0x82D0E3F8));
        try {
            const uint32_t count=std::min(c.r5.u32,8u);
            if(uint64_t(c.r4.u32)+uint64_t(count)*28>0x100000000ull) throw Failure("Rejected Im2D source extent wraps");
            s.runtime.pointer(c.r4.u32,count*28,false);
            for(uint32_t i=0;i<count;++i) {
                std::fprintf(stderr,"[IM2D ENTRY VERTEX] index=%u words=",i);
                for(uint32_t j=0;j<7;++j)std::fprintf(stderr,"%s%08X",j?",":"",PPC_LOAD_U32(c.r4.u32+i*28+j*4));
                std::fputc('\n',stderr);
            }
        } catch(const std::exception& e) {std::fprintf(stderr,"[IM2D ENTRY REJECTED] source inspection: %s\n",e.what());}
        dumpGuestStack(c);
        throw Failure("Unqualified original Im2D primitive/count/camera/raster/buffer capability");
    }
    if(PPC_LOAD_U32(0x82062E08+4*kind)!=(kind==3?4u:6u))
        throw Failure("Original Im2D primitive mapping table changed");
    if(c.r1.u32<0x180 || (c.r1.u32&15) || uint64_t(source)+uint64_t(sourceCount)*28>0x100000000ull)
        throw Failure("Original Im2D source/stack extent is invalid");
    s.runtime.pointer(c.r1.u32-0x180,0x180,true);
    s.runtime.pointer(source,sourceCount*28,false);
    s.runtime.pointer(0x82D101C8,8,true);s.runtime.pointer(0x82D0D0DC,0x34,true);
    s.dynamic.validateOwnership(s.runtime,base);
    s.scratch->validateCreated(PPC_LOAD_U32(0x82D101D4),PPC_LOAD_U32(0x82D101D8));
    const uint32_t prior=PPC_LOAD_U32(0x82D101CC);if(prior)s.dynamic.buffer(prior);
    const uint32_t current=PPC_LOAD_U32(0x82D0D0DC),bytes=count*28;
    if(current>=4)throw Failure("Original dynamic vertex cursor is outside four native owners");
    std::array<uint32_t,4> offsets{};
    for(uint32_t i=0;i<4;++i) {
        const auto id=PPC_LOAD_U32(0x82D0D100+4*i);s.dynamic.buffer(id);
        const uint32_t cursor=PPC_LOAD_U32(0x82D0D0E0+4*i);
        if(PPC_LOAD_U32(0x82D0D0F0+4*i)!=0x40000 || cursor>0x40000)
            throw Failure("Original dynamic vertex capacity/cursor differs from its native backing");
        offsets[i]=((cursor+27)/28)*28;
    }
    uint32_t slot=current,offset=offsets[current];
    if(offset+bytes>0x40000) {
        slot=4;
        for(uint32_t i=0;i<4;++i)if(i!=current && offsets[i]+bytes<=0x40000){slot=i;break;}
        if(slot==4){slot=(current+1)%4;offset=0;}else offset=offsets[slot];
    }
    s.im2d={source,count,c.r1.u32,slot,offset,PPC_LOAD_U32(0x82D0D100+4*slot),0,kind,true,triangle,indices,camera.colorRaster};
}
void EngineDriver::lockIm2DUpload(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.requireCaller(base);auto& u=s.im2d;
    auto uploadTimer=s.timing.measure(FrameTiming::Upload);
    const uint32_t bytes=u.count*28;
    if(!u.active || u.staging || c.r1.u32!=u.stack-0x140 || c.r3.u32!=u.id || c.r4.u32 || c.r5.u32 ||
       c.r6.u32!=(u.offset?0x1000u:0u) || c.r23.u32!=0x82D101CC || c.r25.u32!=0x82D101C8 ||
       c.r26.u32!=u.stack-0x50 || c.r29.u32!=28 || c.r30.u32!=bytes || c.r31.u32!=0x82D0D0D4 ||
       c.r28.u32!=u.offset || c.r27.u32!=u.offset+bytes || PPC_LOAD_U32(0x82D0D0DC)!=u.slot ||
       PPC_LOAD_U32(0x82D0D100+4*u.slot)!=u.id || u.offset+bytes>0x40000)
        throw Failure("Original dynamic allocation/lock ABI or selected range changed");
    s.dynamic.buffer(u.id);
    // Real CPU-addressable staging, never an opaque native identity mapped as
    // an SDK object. Original memcpy writes the exact source bytes here.
    u.staging=s.im2dStaging;
    if(!u.staging)throw Failure("Native Im2D vertex staging allocation failed");
    c.r3.u64=u.staging;c.lr=u.offset?0x823FC9F0:0x823FC9CC;
}
void EngineDriver::unlockIm2DUpload(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.requireCaller(base);auto& u=s.im2d;
    auto uploadTimer=s.timing.measure(FrameTiming::Upload);
    const bool originalRegisters=u.triangle?
        (c.r29.u32==u.source && c.r28.u32==u.indices[0] && c.r27.u32==u.indices[1] &&
         c.r26.u32==u.indices[2] && c.r30.u32==u.raster):
        (c.r26.u32==u.primitive && c.r27.u32==u.source && c.r30.u32==u.count);
    if(!u.active || !u.staging || c.r1.u32!=u.stack-0xA0 || c.r3.u32!=u.id ||
       !originalRegisters || c.r31.u32!=0x82D101C4 ||
       PPC_LOAD_U32(c.r1.u32+0x50)!=u.staging+u.offset || PPC_LOAD_U32(0x82D101CC)!=u.id ||
       PPC_LOAD_U32(0x82D101C8)!=u.offset/28 || PPC_LOAD_U32(0x82D0D0DC)!=u.slot ||
       PPC_LOAD_U32(0x82D0D0E0+u.slot*4)!=u.offset+u.count*28)
        throw Failure("Original Im2D copy/unlock lost its native upload owner");
    const auto backing=s.dynamic.buffer(u.id);const uint32_t bytes=u.count*28;
    const auto* data=s.runtime.pointer(u.staging+u.offset,bytes,false);
    // Own the exact bytes before staging is reused. The real raw GPU upload and
    // later native input decode use this same snapshot, never the caller stack.
    State::Im2DInput input;
    input.bytes.swap(s.spareIm2DInputBytes);input.bytes.assign(data,data+bytes);
    input.buffer=backing;input.declaration=s.scratch->declaration(PPC_LOAD_U32(0x82D101D8));
    s.backend.queueIm2DBufferWrite(backing,u.offset,input.bytes);
    u.staging=0;u.active=false;++s.im2dUploads;
    s.spareIm2DInputBytes.swap(s.im2dInput.bytes);s.spareIm2DInputBytes.clear();
    s.im2dInput=std::move(input);
    c.r3.u64=0;c.lr=u.triangle?0x824092A8:0x82409508;
    if(s.im2dUploads<=8 || s.im2dUploads%4096==0)std::fprintf(stderr,"[NATIVE IM2D UPLOAD] original vertices=%u bytes=%u slot=%u buffer=%08X offset=%u; real native storage and owned input snapshot\n",
        u.count,bytes,u.slot,u.id,u.offset);
}
void EngineDriver::preflightIm2DSetup(PPCContext& c,uint8_t* base) {
    auto& s=*state;auto setupTimer=s.timing.measure(FrameTiming::Im2DSetup);s.requireCaller(base);auto& u=s.im2d;auto& input=s.im2dInput;
    if(u.active || u.staging || input.setup || !input.buffer || input.bytes.size()!=u.count*28 ||
       c.r1.u32!=u.stack-0xA0 || c.lr!=(u.triangle?0x824092ACu:0x8240950Cu) || c.r31.u32!=0x82D101C4 ||
       PPC_LOAD_U32(0x82D101CC)!=u.id || PPC_LOAD_U32(0x82D101C8)!=u.offset/28)
        throw Failure("Original Im2D setup lost its completed upload scope");
    // Explicit material transitions require their own shader-owner bridge.
    // Zero is the original fixed-function selection, not a fabricated shader.
    if(PPC_LOAD_U32(0x82CD1A6C) || PPC_LOAD_U32(0x82CD1A70))
        throw Failure("Original Im2D setup has unqualified explicit shader bindings");
    s.dynamic.validateOwnership(s.runtime,base);
    if(input.buffer!=s.dynamic.buffer(u.id) || input.declaration!=s.scratch->declaration(PPC_LOAD_U32(0x82D101D8)))
        throw Failure("Original Im2D input owners changed before setup");
    const uint32_t declaration=PPC_LOAD_U32(0x82CD1A68),stream=PPC_LOAD_U32(0x82D0CAB0);
    if(declaration && declaration!=PPC_LOAD_U32(0x82D101D8))
        throw Failure("Original Im2D declaration transition is unqualified");
    if(declaration && s.im2dDeclaration!=input.declaration)
        throw Failure("Original Im2D declaration cache has no matching native binding");
    if(stream && stream!=0xFFFFFFFF)s.dynamic.buffer(stream);
    if(stream==u.id && !PPC_LOAD_U32(0x82D0CAB4) && PPC_LOAD_U32(0x82D0CAB8)==28 && s.im2dStream!=input.buffer)
        throw Failure("Original Im2D stream cache has no matching native binding");
    input.setup=true; // Original CPU setup and pending-state commit follow.
}
void EngineDriver::bindIm2DDeclaration(PPCContext& c,uint8_t* base) {
    auto& s=*state;auto setupTimer=s.timing.measure(FrameTiming::Im2DSetup);s.requireCaller(base);auto& input=s.im2dInput;
    if(!input.setup || c.r1.u32!=s.im2d.stack-0x110 || c.r3.u32 ||
       c.r4.u32!=PPC_LOAD_U32(0x82D101D8) || PPC_LOAD_U32(0x82CD1A68)!=c.r4.u32 ||
       PPC_LOAD_U32(0x82CD1A64)!=0xFFFFFFFF || input.declaration!=s.scratch->declaration(c.r4.u32))
        throw Failure("Original Im2D declaration setter ABI or owner changed");
    // Like the original deferred SDK setter, retain a real declaration binding
    // for submission. No resource allocation or shader selection occurs here.
    s.im2dDeclaration=input.declaration;
    if(s.effects)s.effects->completeFixedFunctionDeclaration(base,c.r4.u32);
    c.r3.u64=0;c.lr=0x82408D0C;
}
void EngineDriver::bindIm2DStream(PPCContext& c,uint8_t* base) {
    auto& s=*state;auto setupTimer=s.timing.measure(FrameTiming::Im2DSetup);s.requireCaller(base);auto& u=s.im2d;auto& input=s.im2dInput;
    // Address-stream variant (reach-game-245/246): r5 is a directly readable
    // guest pointer (not a dynamic-buffer id) with r7=32 stride and no setup.
    // Discriminated by readability: ids (0x00D0xxxx) are unmapped. Stash the
    // pointer/stride for the draw site; the draw evidence determines the
    // layout. Nothing is submitted here, mirroring the skipped bl's r3/lr.
    bool addressStream=false;
    try { s.runtime.pointer(c.r5.u32,4,false); addressStream=true; } catch(...) {}
    if(addressStream) {
        auto peek=[&](uint32_t a)->uint32_t { try { s.runtime.pointer(a,4,false); } catch(...) { return 0xDEADDEADu; } return PPC_LOAD_U32(a); };
        std::fprintf(stderr,"[NATIVE IM2D ADDRSTREAM] r5=%08X r7=%08X r8=%016llX decl101D8=%08X declCDA68=%08X\n",
            c.r5.u32,c.r7.u32,(unsigned long long)c.r8.u64,peek(0x82D101D8),peek(0x82CD1A68));
        try {
            s.runtime.pointer(c.r5.u32,64,false);
            std::fprintf(stderr,"[NATIVE IM2D ADDRDATA]");
            for(uint32_t i=0;i<16;++i) std::fprintf(stderr," %08X",PPC_LOAD_U32(c.r5.u32+4*i));
            std::fprintf(stderr,"\n");
        } catch(...) { std::fprintf(stderr,"[NATIVE IM2D ADDRDATA] unreadable\n"); }
        input.addressStream=c.r5.u32;input.addressStride=c.r7.u32;
        c.r3.u64=0;c.lr=0x823EEC70;return;
    }
    const bool mismatch=!input.setup || c.r1.u32!=u.stack-0x180 || c.r3.u32 || c.r4.u32 ||
       c.r5.u32!=u.id || c.r6.u32 || c.r7.u32!=28 || c.r8.u64!=1 ||
       PPC_LOAD_U32(0x82D0CAB0)!=u.id || PPC_LOAD_U32(0x82D0CAB4) || PPC_LOAD_U32(0x82D0CAB8)!=28 ||
       input.buffer!=s.dynamic.buffer(u.id);
    if(mismatch) {
        auto peek=[&](uint32_t a)->uint32_t { try { s.runtime.pointer(a,4,false); } catch(...) { return 0xDEADDEADu; } return PPC_LOAD_U32(a); };
        std::fprintf(stderr,"[NATIVE IM2D STREAM] setup=%u r1=%08X stack=%08X r3=%08X r4=%08X r5=%08X id=%08X r6=%08X r7=%08X r8=%016llX cab0=%08X cab4=%08X cab8=%08X bufMatch=%d decl101D8=%08X declCDA68=%08X\n",
            input.setup,c.r1.u32,u.stack,c.r3.u32,c.r4.u32,c.r5.u32,u.id,c.r6.u32,c.r7.u32,
            (unsigned long long)c.r8.u64,peek(0x82D0CAB0),peek(0x82D0CAB4),peek(0x82D0CAB8),
            input.buffer==s.dynamic.buffer(u.id),peek(0x82D101D8),peek(0x82CD1A68));
        if(c.r5.u32>=0x10000) {
            try {
                s.runtime.pointer(c.r5.u32,64,false);
                std::fprintf(stderr,"[NATIVE IM2D STREAMDATA]");
                for(uint32_t i=0;i<16;++i) std::fprintf(stderr," %08X",PPC_LOAD_U32(c.r5.u32+4*i));
                std::fprintf(stderr,"\n");
            } catch(...) { std::fprintf(stderr,"[NATIVE IM2D STREAMDATA] unreadable\n"); }
        }
    }
    if(mismatch)
        throw Failure("Original Im2D stream setter ABI or owner changed");
    s.im2dStream=input.buffer;c.r3.u64=0;c.lr=0x823EEC70;
}
void EngineDriver::drawIm2D(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.requireCaller(base);requireContext(PPC_LOAD_U32(0x82D5DA74));
    auto im2dTimer=s.timing.measure(FrameTiming::Im2D);
    auto& u=s.im2d;auto& input=s.im2dInput;
    const bool originalRegisters=u.triangle?
        (c.r29.u32==u.source && c.r28.u32==u.indices[0] && c.r27.u32==u.indices[1] &&
         c.r26.u32==u.indices[2] && c.r30.u32==u.raster):
        (c.r26.u32==u.primitive && c.r30.u32==u.count);
    if(u.active || u.staging || !input.setup || c.r1.u32!=u.stack-0xA0 || c.r3.u32 ||
       c.r4.u32!=(u.primitive==3?4u:6u) || c.r5.u32!=u.offset/28 || c.r6.u32!=(u.primitive==3?u.count/3:u.count-2) ||
       !originalRegisters || c.r31.u32!=0x82D101C4 ||
       PPC_LOAD_U32(0x82D101C8)!=u.offset/28 || PPC_LOAD_U32(0x82D101CC)!=u.id ||
       PPC_LOAD_U32(0x82CD1A68)!=PPC_LOAD_U32(0x82D101D8) ||
       PPC_LOAD_U32(0x82D0CAB0)!=u.id || PPC_LOAD_U32(0x82D0CAB4) || PPC_LOAD_U32(0x82D0CAB8)!=28 ||
       input.buffer!=s.im2dStream || input.declaration!=s.im2dDeclaration || input.bytes.size()!=u.count*28)
        throw Failure("Original Im2D draw ABI, committed bindings or uploaded range changed");
    s.dynamic.validateOwnership(s.runtime,base);
    if(input.buffer!=s.dynamic.buffer(u.id) || input.declaration!=s.scratch->declaration(PPC_LOAD_U32(0x82D101D8)))
        throw Failure("Original Im2D draw has stale input resource owners");
    const uint32_t modeDepth=PPC_LOAD_U32(0x82D0CFFC);
    if(modeDepth>8 || (modeDepth && PPC_LOAD_U32(0x82E3DCA0+4*(modeDepth-1))))
        throw Failure("Original Im2D draw is outside the qualified mode-zero pipeline");
    const auto binding=cameraBinding();
    if(binding.camera!=PPC_LOAD_U32(s.engine) || binding.camera!=PPC_LOAD_U32(0x82E3DD60) ||
       !PPC_LOAD_U32(0x82D0CB1C) || PPC_LOAD_U32(binding.colorRaster+0x1C) ||
       binding.viewport[0] || binding.viewport[1] ||
       PPC_LOAD_U32(0x82D503D8)!=PPC_LOAD_U32(binding.colorRaster+12) ||
       PPC_LOAD_U32(0x82D503DC)!=PPC_LOAD_U32(binding.colorRaster+16))
        throw Failure("Original Im2D screen override differs from the active root camera");
    const auto color=s.cameraColor(binding);
    const auto depth=s.cameraDepth(binding);
    if(binding.viewport[2]!=color->width || binding.viewport[3]!=color->height)
        throw Failure("Original Im2D requires the full selected native viewport");
    EngineCpuCalls cpu(c,base);
    const uint32_t raster=cpu.invoke(0x82401AC8,0);
    Graphics::Im2DDraw draw{};
    draw.vertices.swap(s.spareIm2DVertices);
    struct RecycleVertices {
        std::vector<Graphics::Im2DVertex>& spare;
        std::vector<Graphics::Im2DVertex>& current;
        ~RecycleVertices(){current.clear();spare.swap(current);}
    } recycleVertices{s.spareIm2DVertices,draw.vertices};
    if(raster)draw.texture=textureRaster(raster);
    {auto programTimer=s.timing.measure(FrameTiming::Program);qualifyIm2DProgram(s.runtime,cpu,base,raster!=0);}
    const auto& effective=s.renderState->effective();
    using S=Graphics::ScalarState;
    if(s.im2dPackets==0)std::fprintf(stderr,
        "[NATIVE IM2D STATE] depth=%u write=%u expanded=%u cull=%u blend=%08X alpha=%u compare=%u reference=%u mask=%X half=%u guard=%08X/%08X\n",
        effective.scalar(S::DepthEnable),effective.scalar(S::DepthWrite),effective.scalar(S::ExpandedBlend0),effective.scalar(S::Cull),
        effective.effectiveBlend(0),effective.scalar(S::AlphaTest),effective.scalar(S::AlphaCompare),effective.scalar(S::AlphaReference),
        effective.scalar(S::ColorMask0),effective.scalar(S::HalfPixelOffset),effective.scalar(S::GuardBandX),effective.scalar(S::GuardBandY));
    // The common screen gate checks the remaining scalar/sampler policy. Only
    // these explicit differences have implementations in the Im2D backend.
    if(effective.scalar(S::StencilEnable) ||
       (effective.scalar(S::Cull)!=0 && effective.scalar(S::Cull)!=2 && effective.scalar(S::Cull)!=6)) {
        char message[320];std::snprintf(message,sizeof(message),
            "Original Im2D stencil/cull state is unqualified: depth=%u write=%u compare=%u cull=%u stencil=%u raster=%08X camera=%08X completed=%llu lr=%08X sp=%08X source=%08X vertices=%u",
            effective.scalar(S::DepthEnable),effective.scalar(S::DepthWrite),effective.scalar(S::DepthCompare),
            effective.scalar(S::Cull),effective.scalar(S::StencilEnable),raster,binding.camera,
            static_cast<unsigned long long>(s.backend.im2dDrawCount()),uint32_t(c.lr),c.r1.u32,u.source,u.count);
        std::fprintf(stderr,"[IM2D REJECTED] %s\n",message);
        for(const auto& field:Graphics::scalarStateEvidence())
            std::fprintf(stderr,"[IM2D REJECTED SCALAR] id=%03X value=%08X\n",field.id,effective.scalar(field.id));
        for(const auto& field:Graphics::samplerStateEvidence())
            std::fprintf(stderr,"[IM2D REJECTED SAMPLER0] id=%03X value=%08X\n",field.id,effective.sampler(0,field.id));
        for(uint32_t vertex=0;vertex<std::min(u.count,8u);++vertex) {
            std::fprintf(stderr,"[IM2D REJECTED VERTEX] index=%u words=",vertex);
            for(uint32_t word=0;word<7;++word) {
                const auto* bytes=input.bytes.data()+28*vertex+4*word;
                const uint32_t bits=(uint32_t(bytes[0])<<24)|(uint32_t(bytes[1])<<16)|(uint32_t(bytes[2])<<8)|bytes[3];
                std::fprintf(stderr,"%s%08X",word?",":"",bits);
            }
            std::fputc('\n',stderr);
        }
        dumpGuestStack(c);
        throw Failure(message);
    }
    // Depth, cull and blend have their own Im2D implementation. Validate the
    // shared screen subset directly, retaining actual requests in the packet.
    // Multi-level chains are fully uploaded immutable mipmaps sampled with
    // hardware LOD exactly like the original (first observed: 512 BC2 x6 at
    // 1:1 texel coverage, reach-game-234/235 raster E1ACF5A8); no level cap.
    Graphics::ScreenStateSnapshot checked;
    try {checked=effective.requireOriginalIm2DDrawState(raster!=0);}
    catch(const std::exception& e) {
        std::fprintf(stderr,"[IM2D STATE REJECTED] primitive=%u vertices=%u raster=%08X reason=%s\n",u.primitive,u.count,raster,e.what());
        for(const auto& field:Graphics::samplerStateEvidence())
            std::fprintf(stderr,"[IM2D REJECTED SAMPLER0] id=%03X value=%08X\n",field.id,effective.sampler(0,field.id));
        throw;
    }
    Graphics::decodeIm2DVertices(*input.declaration,input.bytes,raster!=0,draw.vertices);
    draw.primitiveType=u.primitive;draw.rasterWidth=PPC_LOAD_U32(0x82D503D8);draw.rasterHeight=PPC_LOAD_U32(0x82D503DC);
    draw.blendWord=effective.effectiveBlend(0);draw.alphaTest=checked.alphaTest;
    draw.expandedBlend=checked.expandedBlendRequested;
    draw.alphaReference=checked.alphaReference;draw.alphaCompare=checked.alphaCompare;
    draw.cullBits=effective.scalar(S::Cull);draw.pixelCenterHalf=effective.scalar(S::HalfPixelOffset)!=0;
    draw.colorWriteMask=uint8_t(checked.colorMask);
    draw.depthTest=effective.scalar(S::DepthEnable)!=0;draw.depthWrite=effective.scalar(S::DepthWrite)!=0;
    draw.depthCompare=effective.scalar(S::DepthCompare);
    if(binding.viewport[4]==0x3F800000 && !binding.viewport[5])draw.reverseDepth=true;
    else if(binding.viewport[4] || binding.viewport[5]!=0x3F800000)
        throw Failure("Original Im2D depth viewport is outside the verified forward/reversed full range");
    if(draw.depthTest && (!depth || PPC_LOAD_U32(0x82E3DD08)!=0x1A220197))
        throw Failure("Original Im2D enabled depth requires the qualified D24FS8 working surface");
    draw.stencil=effective.scalar(S::StencilEnable)!=0;
    draw.preserveAspect=s.uiDrawing && !Graphics::isFullCanvasUiFill(draw);
    if(raster) {
        draw.sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        draw.sampler.AddressU=checked.sampler->addressU==2?D3D11_TEXTURE_ADDRESS_CLAMP:D3D11_TEXTURE_ADDRESS_WRAP;
        draw.sampler.AddressV=checked.sampler->addressV==2?D3D11_TEXTURE_ADDRESS_CLAMP:D3D11_TEXTURE_ADDRESS_WRAP;
        draw.sampler.AddressW=checked.sampler->addressW==2?D3D11_TEXTURE_ADDRESS_CLAMP:D3D11_TEXTURE_ADDRESS_WRAP;
        if(checked.sampler->addressW!=0 && checked.sampler->addressW!=2)
            throw Failure("Original Im2D texture W addressing is unqualified");
        draw.sampler.MaxAnisotropy=1;draw.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
    }
    // Original screen-position setup changes only its flag and XY constants;
    // camera depth mapping remains live. Native viewport stays0..1 because
    // D3D11 forbids reversed endpoints. The pixel shader applies the retained
    // 1-z mapping when needed, then stores decoded20e4 via SV_Depth. Guest
    // vertices and logical viewport endpoints remain unchanged.
    // Substitute only the submitted UI draw. The original atlas remains bound
    // in the engine cache, so device changes cannot invalidate binding checks.
    if(raster && s.itxdTextures && s.runtime.controllers && s.runtime.controllers->usesKeyboardMouse()) {
        const auto* controls=s.runtime.window&&!s.runtime.window->isMenuMouse()?&s.runtime.controlSettings:nullptr;
        if(auto prompts=s.itxdTextures->inputPromptTexture(base,raster,controls)) {
            draw.texture=std::move(prompts);
            if(controls)Graphics::enlargeInputPromptGlyphs(draw,Graphics::keyboardMousePromptLayout(*controls));
            else Graphics::enlargeInputPromptGlyphs(draw);
        }
    }
    s.backend.setViewport({0,0,float(binding.viewport[2]),float(binding.viewport[3]),0,1});
    {auto drawTimer=s.timing.measure(FrameTiming::Draw);s.backend.queueIm2D(color,depth,draw);}
    ++s.im2dPackets;
    if(draw.depthTest)++s.im2dDepthDraws;
    if(draw.texture)++s.im2dTexturedDraws;
    input.setup=false;
    // The engine-level draw replaces fixed-function cache/compiler selection
    // and submission/temporary-shader cleanup. Both original bodies continue with
    // override reset, start-vertex increment and its own boolean/ABI epilogue.
    c.r3.u64=0;c.lr=u.triangle?0x824092D0:0x8240953C;
    if(s.im2dPackets<=8 || s.im2dPackets%4096==0)std::fprintf(stderr,"[NATIVE IM2D DRAW] accepted=%llu submitted=%llu vertices=%u buffer=%08X offset=%u raster=%08X blend=%08X expanded=%u cull=%u alpha=%u/%u depth=%u/%u/%u reversed=%u; original program qualified, console raster precision unverified\n",
        static_cast<unsigned long long>(s.im2dPackets),static_cast<unsigned long long>(s.backend.im2dDrawCount()),u.count,u.id,u.offset,raster,draw.blendWord,draw.expandedBlend,draw.cullBits,
        draw.alphaTest,checked.alphaReferenceInteger,draw.depthTest,draw.depthWrite,draw.depthCompare,draw.reverseDepth);
}
uint64_t EngineDriver::im2DDrawCount() const {state->requireCaller(state->runtime.base);state->backend.flushIm2D();return state->backend.im2dDrawCount();}
uint64_t EngineDriver::im2DUploadCount() const {state->requireCaller(state->runtime.base);return state->im2dUploads;}
std::vector<uint8_t> EngineDriver::readbackDynamicBuffer(uint32_t id) {
    auto& s=*state;s.requireCaller(s.runtime.base);return s.backend.readbackBuffer(s.dynamic.buffer(id));
}
void EngineDriver::drawMovie(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.requireCaller(base);requireContext(PPC_LOAD_U32(0x82D5DA74));
    if(currentContext!=&c||uint32_t(c.lr)!=0x8282ED60||c.r1.u32<0x200||(c.r1.u32&15)||c.r1.u32>UINT32_MAX-0x80||
       c.r3.u32!=c.r31.u32||c.r4.u32!=c.r27.u32||c.r29.u32!=3||c.r28.u32!=c.r30.u32+0x50)
        throw Failure("Native movie draw original presenter-call ABI differs");
    s.runtime.pointer(c.r1.u32,0x80,false);
    if(PPC_LOAD_U32(c.r1.u32)!=c.r1.u32+0x80)
        throw Failure("Native movie draw lacks the full original presenter frame");
    const auto frame=s.rasters->movieFrame(base,c.r3.u32);
    if(frame.descriptor!=c.r30.u32||PPC_LOAD_U8(frame.descriptor+0x51)!=c.r4.u32)
        throw Failure("Native movie draw differs from original committed descriptor");
    const auto binding=cameraBinding();
    if(binding.camera!=PPC_LOAD_U32(s.engine)||binding.camera!=PPC_LOAD_U32(0x82E3DD60)||
       binding.colorIdentity!=PPC_LOAD_U32(0x82D0CF5C)||binding.depthIdentity!=PPC_LOAD_U32(0x82D0CF58)||
       binding.viewport!=std::array<uint32_t,6>{0,0,1280,720,0x3F800000,0}||s.im2d.active||s.im2dInput.setup)
        throw Failure("Native movie requires the active full-size original camera outside an Im2D submission");
    const uint32_t declaration=PPC_LOAD_U32(0x82DFEB34),vs=PPC_LOAD_U32(0x82CF2340),ps=PPC_LOAD_U32(0x82CF2328);
    if(!declaration||declaration!=s.screenDeclarationIds[1]||!s.screenDeclarations.contains(s.screenDeclarationRecords[1]))
        throw Failure("Original movie declaration lacks its native creation owner");
    const auto& vertex=s.screenMaterial(base,3);const auto& pixel=s.screenMaterial(base,4);
    if(vertex.originalAddress()!=0x82152880||pixel.originalAddress()!=0x82152B68)
        throw Failure("Original movie shaders differ from the recovered VS and three-plane PS");
    for(uint32_t a:{0x82CD1A68u,0x82CD1A6Cu,0x82CD1A70u})s.runtime.pointer(a,4,true);
    const auto movie=Graphics::prepareMovieState(s.renderState->effective());
    const auto geometry=Graphics::buildMovieGeometry(std::bit_cast<int32_t>(frame.width),PPC_LOAD_U8(c.r3.u32+0x41));
    Graphics::MovieDraw draw{};
    for(size_t i=0;i<draw.vertices.size();++i) {
        const auto& v=geometry.nativeVertices[i];draw.vertices[i]={v.x,v.y,v.u,v.v};
    }
    draw.textures={frame.textures[0],frame.textures[2],frame.textures[1]};
    draw.retainedDepthWrite=movie.retainedDepthWrite;draw.retainedDepthCompare=movie.retainedDepthCompare;
    for(uint32_t i=0;i<3;++i) {
        const auto& source=movie.samplers[i];auto& sampler=draw.samplers[i];
        sampler.Filter=source.mipFilter==2?D3D11_FILTER_MIN_MAG_MIP_LINEAR:D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU=source.addressU==2?D3D11_TEXTURE_ADDRESS_CLAMP:D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.AddressV=source.addressV==2?D3D11_TEXTURE_ADDRESS_CLAMP:D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.AddressW=source.addressW==2?D3D11_TEXTURE_ADDRESS_CLAMP:D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
        sampler.MinLOD=sampler.MaxLOD=0; // Each owned plane has exactly one level.
    }
    const auto effectAssociation=[&] {
        const auto manager=PPC_LOAD_U32(0x82D08BFC);if(manager)s.runtime.pointer(manager,0x10,false);
        return std::array<uint32_t,4>{manager,manager?PPC_LOAD_U32(manager+4):0,
            manager?PPC_LOAD_U32(manager+8):0,manager?PPC_LOAD_U32(manager+0xC):0};
    };
    const auto shaderCaches=[&] {return std::array<uint32_t,3>{PPC_LOAD_U32(0x82CD1A68),
        PPC_LOAD_U32(0x82CD1A6C),PPC_LOAD_U32(0x82CD1A70)};};
    // The full original presenter keeps the logical FX manager selected. Its
    // final movie shaders replace the physical program; qualify the preceding
    // scene/screen association before that replacement, never by restoration.
    if(s.effects)s.effects->preflightScreenReplacement(base);
    const auto association=effectAssociation();const auto priorCaches=shaderCaches();
    const auto movieBefore=s.backend.movieDrawCount();
    const auto presenter=c.r3.u32,frameByte=c.r4.u32,stack=c.r1.u32;
    const auto color=s.cameraColor(binding);const auto depth=s.cameraDepth(binding);
    s.backend.drawMovie(color,depth,draw);
    const auto completedFrame=s.rasters->movieFrame(base,presenter);const auto completedCamera=cameraBinding();
    if(currentContext!=&c||c.r1.u32!=stack||PPC_LOAD_U32(stack)!=stack+0x80||uint32_t(c.lr)!=0x8282ED60||
       c.r3.u32!=presenter||c.r31.u32!=presenter||c.r4.u32!=frameByte||c.r27.u32!=frameByte||
       c.r30.u32!=frame.descriptor||c.r29.u32!=3||c.r28.u32!=frame.descriptor+0x50||
       PPC_LOAD_U8(frame.descriptor+0x51)!=frameByte||effectAssociation()!=association||shaderCaches()!=priorCaches||
       completedFrame.descriptor!=frame.descriptor||completedFrame.provider!=frame.provider||
       completedFrame.width!=frame.width||completedFrame.height!=frame.height||completedFrame.rasters!=frame.rasters||
       completedFrame.pitches!=frame.pitches||completedFrame.textures!=frame.textures||
       completedCamera.camera!=binding.camera||completedCamera.colorRaster!=binding.colorRaster||
       completedCamera.depthRaster!=binding.depthRaster||completedCamera.colorIdentity!=binding.colorIdentity||
       completedCamera.depthIdentity!=binding.depthIdentity||completedCamera.viewport!=binding.viewport)
        throw Failure("Original movie completion lost its presenter, frame, camera or retained effect owner");
    s.backend.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);
    s.renderState->publishScreenState(movie.after);
    PPC_STORE_U32(0x82CD1A68,declaration);PPC_STORE_U32(0x82CD1A6C,vs);PPC_STORE_U32(0x82CD1A70,ps);
    const auto replacement=s.backend.completedMovieReplacement(movieBefore);
    if(s.effects)s.effects->completeScreenReplacement(base,replacement,declaration,vs,ps);
    s.im2dInput={};s.im2dDeclaration.reset(); // IA stream zero and its CPU cache survive.
    // This is the original void draw boundary. Its sole checked caller ignores
    // volatile return registers and resumes its own queue/lifetime epilogue.
    const auto count=s.backend.movieDrawCount();
    if(count<=5||count%300==0)std::fprintf(stderr,
        "[NATIVE MOVIE DRAW] submitted=%llu presenter=%08X frame=%08X extent=%ux%u uv_mode=%u camera=%08X planes=%08X,%08X,%08X VS=%08X PS=%08X decl=%08X; original Y/Cr/Cb arithmetic and rectangle, console precision parity unverified\n",
        static_cast<unsigned long long>(count),c.r3.u32,frame.descriptor,frame.width,frame.height,PPC_LOAD_U8(c.r3.u32+0x41),
        binding.camera,frame.rasters[0],frame.rasters[2],frame.rasters[1],vs,ps,declaration);
    if(!s.runtime.captureOnRequest && !s.runtime.frameCaptureDirectory.empty() &&
       (count<=5 || (count<=300 && count%30==0) || (count<=18000 && count%300==0))) {
        // This target is private to the selected camera. Preserve it before
        // later original composition, separately from front/display captures.
        std::filesystem::create_directories(s.runtime.frameCaptureDirectory);
        char name[80];std::snprintf(name,sizeof(name),"native-movie-target-%04llu",static_cast<unsigned long long>(count));
        const auto stem=s.runtime.frameCaptureDirectory/name;const auto pixels=s.backend.readbackTarget(s.cameraColor(binding));
        std::ofstream raw(stem.string()+".rgb10a2",std::ios::binary);
        raw.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()));
        if(!raw)throw Failure("Native movie target capture could not write packed pixels");
        std::ofstream metadata(stem.string()+".json");
        metadata<<"{\"width\":"<<s.cameraColor(binding)->pixelWidth()<<",\"height\":"<<s.cameraColor(binding)->pixelHeight()
            <<",\"format\":\"R10G10B10A2_UNORM_LE\",\"movie_draws\":"<<count<<",\"frame_descriptor\":"<<frame.descriptor
            <<",\"camera\":"<<binding.camera<<",\"target_identity\":"<<binding.colorIdentity
            <<",\"capture_source\":\"private_movie_target_renderer_readback\",\"front_copy_completed\":false,\"display_accepted\":false,\"alpha_ignored_by_display\":true}\n";
        if(!metadata)throw Failure("Native movie target capture could not write metadata");
    }
}
uint64_t EngineDriver::movieDrawCount() const {state->requireCaller(state->runtime.base);return state->backend.movieDrawCount();}
void EngineDriver::distortionOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.requireCaller(base);auto& p=s.distortion;const uint32_t sp=c.r1.u32;
    auto need=[&](bool ok,const char* why){if(!ok){char message[320];std::snprintf(message,sizeof(message),"Distortion %08X: %s",site,why);throw Failure(message);}};
    need(currentContext==&c&&s.ready&&s.submissionReady&&sp>=0x300&&!(sp&15),"live original CPU context differs");
    auto frame=[&](uint32_t bytes,uint32_t caller){need(PPC_LOAD_U32(sp)==sp+bytes&&PPC_LOAD_U32(sp+bytes-8)==caller,"original frame/caller differs");};
    if(site==0x82750C54&&!p.cpu){c.lr=site+4;PPCGuestFloatingPointScope floatingPoint(c.fpscr);PPCSafeIndirect(c,base,0x82440698);return;}
    const bool setup=site>=0x827719C0&&site<=0x827719F8;
    if(setup)need(!s.ballEffect.cpu&&s.ballEffect.matrices==3&&PPC_LOAD_U32(sp)==sp+0x70,"sprite setup frame/matrices differ");
    if(site==0x82771B88)need(s.ballEffect.cpu==&c&&s.ballEffect.stack==sp&&PPC_LOAD_U32(sp)==sp+0xB0,"sprite blend frame differs");
    if(site==0x827724E0) {
        frame(0xA0,0x827517A8);need(!p.cpu&&!s.postFilter.cpu&&!s.ballEffect.cpu&&PPC_LOAD_U32(0x82DFF580),"phase overlaps or has an empty source list");
        if(s.effects)s.effects->preflightScreenReplacement(base);
        const auto camera=cameraBinding();need(camera.camera==c.r31.u32&&camera.viewport[2]==1280&&camera.viewport[3]==720&&PPC_LOAD_U32(0x82D0CB1C)==1,"phase camera differs");
        State::Distortion next;next.cpu=&c;next.stack=sp;next.camera=camera.camera;next.mainId=next.targetId=camera.colorIdentity;
        next.main=next.target=s.cameraColor(camera);next.depth=s.cameraDepth(camera);next.draw.viewport=camera.viewport;
        next.stackCount=PPC_LOAD_U32(0x82CF1F88);need(next.stackCount==0,"target stack retained at entry");
        s.runtime.pointer(PPC_LOAD_U32(0x82CF1F80),8,true);s.backend.requireSelectedTargets({next.main,nullptr,nullptr,nullptr},next.depth);
        next.copies=1;next.postFilterBefore=s.backend.postFilterDrawCount();p=std::move(next);c.r3.u64=c.r31.u64;return;
    }
    if(site==0x8277252C) {
        frame(0xA0,0x827517A8);
        if(p.cpu){need(p.cpu==&c&&p.stack==sp&&p.draws==5&&p.copies==5&&!p.temp&&!p.lease&&!p.helper&&!p.locked&&p.target==p.main&&PPC_LOAD_U32(0x82CF1F88)==p.stackCount,"phase ended before complete restoration");
            s.backend.requireSelectedTargets({p.main,nullptr,nullptr,nullptr},p.depth);
            if(s.effects)s.effects->completeDistortionScreenReplacement(base,p.postFilterBefore,
                PPC_LOAD_U32(0x82CD1A68),PPC_LOAD_U32(0x82CD1A6C),PPC_LOAD_U32(0x82CD1A70));
            ++s.distortionPhases;p={};}
        else need(!PPC_LOAD_U32(0x82DFF580),"nonempty phase exited without native scope");
        c.r1.u64+=0xA0;return;
    }
    if(!setup&&site!=0x82771B88)need(p.cpu==&c&&s.camera&&s.camera->camera==p.camera&&p.camera==PPC_LOAD_U32(0x82E3DD60)&&PPC_LOAD_U32(0x82D0CB1C)==1,"phase owner/camera differs");
    const bool push=site>=0x82771E58&&site<=0x82771EA4,pop=site>=0x82771F58&&site<=0x82771F98;
    if(push||pop) {
        const uint32_t caller=PPC_LOAD_U32(sp+(push?0x88:0x98));frame(push?0x90:0xA0,caller);
        need(sp+(push?0x90:0xA0)==p.stack-0x130&&(push?(caller==0x827720E8||caller==0x827722A0):(caller==0x82772284||caller==0x82772450)),"target helper parent differs");
        const bool first=caller==0x827720E8||caller==0x82772284;
        if(site==0x82771E58){need(!p.helper&&!p.lease&&p.temp&&p.target==p.main&&!c.r3.u32&&!c.r4.u32&&!c.r29.u32&&c.r30.u32==p.tempId&&PPC_LOAD_U32(0x82CF1F88)==p.stackCount&&p.draws==(first?0u:1u),"target retain transition differs");
            s.backend.requireSelectedTargets({p.main,nullptr,nullptr,nullptr},p.depth);p.lease=p.main;p.helper=sp;p.helperStep=1;c.r3.u64=p.mainId;c.lr=site+4;return;}
        if(site==0x82771F58){need(!p.helper&&p.lease==p.main&&p.target==p.temp&&c.r29.u32==p.tempId&&c.r30.u32==p.mainId&&PPC_LOAD_U32(0x82CF1F88)==p.stackCount&&p.draws==(first?1u:3u),"target pop transition differs");p.helper=sp;p.helperStep=1;}
        need(p.helper==sp&&p.lease==p.main,"target helper lost its lease");
        if(site==0x82771E74||site==0x82771F58){need(p.helperStep==1&&!c.r3.u32&&!c.r4.u32&&c.r5.u32==(push?p.tempId:p.mainId)&&PPC_LOAD_U32(0x82D0CF5C)==p.targetId,"target bind differs");
            s.backend.requireSelectedTargets({p.target,nullptr,nullptr,nullptr},push?p.depth:nullptr);
            p.target=push?p.temp:p.main;p.targetId=push?p.tempId:p.mainId;
            s.backend.bindTargets({p.target,nullptr,nullptr,nullptr},push?nullptr:p.depth);PPC_STORE_U32(0x82D0CF5C,p.targetId);
            p.draw.viewport={0,0,p.target->width,p.target->height,0,0x3F800000};s.backend.setViewport({0,0,float(p.target->width),float(p.target->height),0,1});p.helperStep=2;
        } else if(site==0x82771E80||site==0x82771F64){const uint32_t output=sp+(push?0x50:0x60);need(p.helperStep==2&&!c.r3.u32&&c.r4.u32==output,"viewport output differs");
            for(unsigned i=0;i<6;++i)PPC_STORE_U32(output+4*i,p.draw.viewport[i]);p.helperStep=3;
        } else if(site==0x82771EA4||site==0x82771F88){const uint32_t input=sp+(push?0x50:0x60);need(p.helperStep==3&&!c.r3.u32&&c.r4.u32==input,"viewport input differs");
            const std::array<uint32_t,6> expected={0,0,p.target->width,p.target->height,0x3F800000,0};for(unsigned i=0;i<6;++i)need(PPC_LOAD_U32(input+4*i)==expected[i],"original reversed viewport differs");
            p.draw.viewport=expected;p.helperStep=4;if(push){p.helper=0;p.helperStep=0;}
        } else if(site==0x82771F98){need(p.helperStep==4&&c.r3.u32==p.mainId&&p.target==p.main,"target lease release differs");p.lease.reset();p.helper=p.helperStep=0;c.r3.u64=0;
        } else need(false,"unknown target helper");
        c.lr=site+4;return;
    }
    if(!setup&&site!=0x82771B88){
        if(site==0x82750C54){frame(0xA0,0x82772294);need(sp+0xA0==p.stack-0x130,"64px surface helper parent differs");}
        else if(site>=0x82772508){frame(0xA0,0x827517A8);need(sp==p.stack&&p.draws==5&&p.copies==5&&!p.temp&&!p.lease,"final state precedes completed phase");}
        else {const uint32_t bytes=site<0x82771700?0x80:site<0x82771900?0x70:0x130;
            frame(bytes,bytes==0x80?0x82772500:bytes==0x70?0x82772508:0x827724F8);need(sp+bytes==p.stack&&!p.helper,"continuation parent frame differs");}
    }
    struct Scalar {uint32_t site,id,value;};
    constexpr Scalar scalars[]={
        {0x827719C0,0x144,1},{0x827719CC,0x38,0},{0x827719D8,0x28,1},
        {0x82772024,0x144,1},{0x82772030,0x38,0},{0x8277203C,0x28,0},{0x82772048,0x30,0},{0x82772054,0x60,0},
        {0x8277157C,0x144,1},{0x82771588,0x38,0},{0x82771594,0x28,0},{0x827715A0,0x60,0},
        {0x82771718,0x144,1},{0x82771724,0x38,0},{0x82771730,0x28,0},{0x8277173C,0x60,0},
        {0x82772510,0x3C,0},{0x8277251C,0x28,1},{0x82772528,0x30,1}};
    for(const auto& e:scalars)if(site==e.site){need(!c.r3.u32&&c.r4.u32==e.value,"direct scalar arguments differ");directScalar(base,e.id,e.value);c.lr=site+4;return;}
    struct Sampler {uint32_t site,stage,id,value;};
    constexpr Sampler samplers[]={
        {0x827719E8,0,0x14,1},{0x827719F8,0,0x10,1},{0x82772140,0,0x14,1},{0x82772150,0,0x10,1},
        {0x827723B8,0,0x14,0},{0x827723C8,0,0x10,0},{0x82771614,0,0x14,0},{0x82771624,0,0x10,0},
        {0x827717B0,0,0x14,1},{0x827717C0,0,0x10,1},{0x82771820,1,0x14,1},{0x82771830,1,0x10,1}};
    for(const auto& e:samplers)if(site==e.site){need(!c.r3.u32&&c.r4.u32==e.stage&&c.r5.u32==e.value,"direct sampler arguments differ");directSampler(base,e.stage,e.id,e.value);c.lr=site+4;return;}
    if(site==0x82771B88||site==0x82772068||site==0x827715B4||site==0x82771750){
        need(!c.r3.u32&&!c.r4.u32&&(site==0x82771B88?c.r5.u32==0x10001||c.r5.u32==0x10106||c.r5.u32==0x10186||c.r5.u32==0x10706:c.r5.u32==(site==0x82771750?0x10706u:0x10001u)),"packed blend arguments differ");
        auto next=s.renderState->effective();next.setPackedBlend(0,c.r5.u32);s.renderState->publishScreenState(next);c.lr=site+4;return;
    }
    if(site==0x82772154||site==0x827717C4||site==0x82771834){auto next=s.renderState->effective();const uint32_t stage=site==0x82772154?0:1;
        next.setSampler(stage,Graphics::SamplerState::AddressU,2);next.setSampler(stage,Graphics::SamplerState::AddressV,2);s.renderState->publishScreenState(next);
        if(site==0x827717C4){c.r29.u64=1;c.r6.u64=0x40000000;c.r4.u64=1;}return;
    }
    if(site==0x827720D8||site==0x82750C54){const uint32_t size=site==0x827720D8?256:64;
        need(!p.temp&&!p.tempId&&p.target==p.main&&!p.lease&&c.r3.u32==size&&c.r4.u32==size&&c.r5.u32==0x182801B6&&!c.r6.u32&&c.r7.u32==0x82DFF02C,"temporary surface descriptor differs");
        p.temp=s.backend.createTarget(size,size,Graphics::TargetFormat::RGB10A2);p.tempId=allocateTargetIdentity();c.r3.u64=p.tempId;c.lr=site+4;return;
    }
    if(site==0x82772284||site==0x82772450){need(p.temp&&p.tempId==c.r3.u32&&p.target==p.main&&!p.lease&&!p.helper&&p.draws==(site==0x82772284?1u:3u),"temporary surface release differs");p.temp.reset();p.tempId=0;c.r3.u64=0;c.lr=site+4;return;}
    struct Copy {uint32_t site,slot,draws,copies;};
    constexpr Copy copies[]={{0x827720AC,2,0,1},{0x82772278,6,1,2},{0x82772370,7,2,3},{0x82772444,7,3,4}};
    for(const auto& e:copies)if(site==e.site){const uint32_t header=PPC_LOAD_U32(0x82DFEB48+4*e.slot);
        need(!p.locked&&p.draws==e.draws&&p.copies==e.copies&&!c.r3.u32&&c.r4.u32==0x100&&!c.r5.u32&&c.r6.u32==header&&!c.r7.u32&&!c.r8.u32&&!c.r9.u32&&!PPC_LOAD_U32(sp+0x5C)&&!PPC_LOAD_U32(sp+0x64),"resolve arguments/order differ");
        need(!c.r10.u32,"resolve default clear pointer differs");
        s.viewportSurfaces->resolveColor(header,p.target,true);++p.copies;c.r3.u64=0;c.lr=site+4;return;}
    struct Shader {uint32_t site,slot;};
    constexpr Shader shaders[]={{0x827720FC,3},{0x82772114,15},{0x82772380,3},{0x82772394,20},{0x827715CC,3},{0x827715E4,2},{0x82771768,3},{0x82771780,21}};
    for(const auto& e:shaders)if(site==e.site){const bool vertex=e.slot==3;const auto& record=s.screenRecord(base,e.slot);
        need(!c.r3.u32&&c.r4.u32==s.screenMaterialBindings[e.slot].handle&&PPC_LOAD_U32(vertex?0x82CD1A6C:0x82CD1A70)==c.r4.u32,"shader object/cache differs");
        (vertex?p.vertex:p.pixel)=record.identity().originalAddress;c.lr=site+4;return;}
    struct Texture {uint32_t site,stage,slot;};
    constexpr Texture textures[]={{0x82772130,0,2},{0x827722B0,0,6},{0x827723A8,0,7},{0x82771604,0,0},{0x827717A0,0,0},{0x82771810,1,7}};
    for(const auto& e:textures)if(site==e.site){const uint32_t header=PPC_LOAD_U32(0x82DFEB48+4*e.slot);
        need(!c.r3.u32&&c.r4.u32==e.stage&&c.r5.u32==header&&c.r6.u64==(e.stage?0x40000000ull:0x80000000ull),"snapshot texture arguments differ");
        p.textureHeaders[e.stage]=header;p.draw.inputs[e.stage]=s.viewportSurfaces->colorTexture(header);c.lr=site+4;return;}
    if(site==0x827721A0||site==0x82771638||site==0x8277187C){need(!c.r3.u32&&c.r4.u32&&c.r4.u32==s.screenDeclarationIds[1]&&PPC_LOAD_U32(0x82DFEB34)==c.r4.u32&&PPC_LOAD_U32(0x82CD1A68)==c.r4.u32&&s.screenDeclarations.contains(s.screenDeclarationRecords[1]),"quad declaration owner differs");p.declaration=true;c.lr=site+4;return;}
    if(site==0x827721D4||site==0x827722DC||site==0x8277166C){const uint32_t offset=site==0x827721D4?0x70:site==0x827722DC?0x80:0x50,index=site==0x8277166C?0:9;
        for(unsigned i=0;i<4;++i){const float value=std::bit_cast<float>(PPC_LOAD_U32(sp+offset+4*i));need(std::isfinite(value),"CPU pixel constant nonfinite");p.draw.pixelConstants[index][i]=value;}p.constants|=1u<<index;return;}
    struct Quad {uint32_t lock,finish,pixel,input,draws;};
    constexpr Quad quads[]={{0x82772200,0x82772248,0x82155F28,2,0},{0x82772308,0x82772340,0x82155F28,6,1},{0x827723DC,0x82772414,0x82156150,7,2},{0x82771698,0x827716E0,0x82152708,0,3},{0x82771890,0x827718E0,0x82156340,0,4}};
    for(const auto& e:quads)if(site==e.lock||site==e.finish){
        need(p.vertex==0x82152880&&p.pixel==e.pixel&&p.draws==e.draws&&p.declaration&&p.textureHeaders[0]==PPC_LOAD_U32(0x82DFEB48+4*e.input)&&!c.r3.u32,"quad stage/input differs");
        need(PPC_LOAD_U32(0x82D0CF5C)==p.targetId&&PPC_LOAD_U32(0x82CD1A6C)==s.screenMaterialBindings[3].handle&&PPC_LOAD_U32(0x82CD1A68)==s.screenDeclarationIds[1],"quad target/vertex binding differs");
        if(site==e.lock){need(!p.locked&&c.r4.u32==8&&c.r5.u32==3&&c.r6.u32==16,"rectangle reservation differs");
            if(!s.distortionStaging)s.distortionStaging=s.runtime.allocatePhysical(0,4096,PAGE_READWRITE,0,UINT32_MAX,4096);need(s.distortionStaging!=0,"rectangle staging allocation failed");
            for(unsigned i=0;i<12;++i)PPC_STORE_U32(s.distortionStaging+4*i,0x7FC00000);p.locked=true;p.quad=sp;c.r3.u64=s.distortionStaging;c.lr=site+4;return;}
        need(p.locked&&p.quad==sp&&(e.draws>=2||(p.constants&(1u<<9)))&&(e.draws!=3||(p.constants&1)),"rectangle lacks complete CPU inputs");
        auto& d=p.draw;d.pixelShader=p.pixel;for(unsigned i=0;i<3;++i)for(unsigned j=0;j<4;++j){d.quad[i][j]=std::bit_cast<float>(PPC_LOAD_U32(s.distortionStaging+16*i+4*j));need(std::isfinite(d.quad[i][j]),"rectangle CPU vertex nonfinite");}
        using S=Graphics::ScalarState;using T=Graphics::SamplerState;const auto& effective=s.renderState->effective();
        d.blendEnable=1;d.blendWord=effective.effectiveBlend(0);d.expandedBlend=effective.scalar(S::ExpandedBlend0);
        d.colorMask=effective.scalar(S::ColorMask0);d.depthEnable=effective.scalar(S::DepthEnable);d.depthWrite=effective.scalar(S::DepthWrite);d.depthCompare=effective.scalar(S::DepthCompare);
        d.stencilEnable=effective.scalar(S::StencilEnable);d.alphaTest=effective.scalar(S::AlphaTest);d.cull=effective.scalar(S::Cull);d.fill=effective.scalar(S::Fill);
        d.scissorEnable=effective.scalar(S::ScissorEnable);d.halfPixelOffset=effective.scalar(S::HalfPixelOffset);d.viewportEnable=effective.scalar(S::ViewportEnable);
        if(d.scissorEnable){const auto scissor=s.backend.scissor();need(bool(scissor),"enabled scissor has no retained rectangle");d.scissor=*scissor;}
        d.clipPlaneEnable=effective.scalar(S::ClipPlaneEnable);d.multisampleAntialias=effective.scalar(S::MultisampleAntialias);d.multisampleMask=effective.scalar(S::MultisampleMask);
        d.alphaToMask=effective.scalar(S::AlphaToMask);d.depthBiasBits=effective.scalar(S::DepthBias);d.slopeBiasBits=effective.scalar(S::SlopeBias);
        for(unsigned stage=0;stage<(e.draws==4?2u:1u);++stage){need(effective.sampler(stage,T::AddressU)<=2&&effective.sampler(stage,T::AddressV)<=2&&effective.sampler(stage,T::AddressW)<=2&&effective.sampler(stage,T::Magnification)<=1&&effective.sampler(stage,T::Minification)==effective.sampler(stage,T::Magnification)&&effective.sampler(stage,T::MipFilter)<=2&&!effective.sampler(stage,T::LodBiasBits),"quad sampler unsupported");
            auto& sampler=d.samplers[stage];sampler={};const bool linear=effective.sampler(stage,T::Magnification)!=0,mip=effective.sampler(stage,T::MipFilter)==2;
            sampler.Filter=linear?(mip?D3D11_FILTER_MIN_MAG_MIP_LINEAR:D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT):(mip?D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR:D3D11_FILTER_MIN_MAG_MIP_POINT);
            sampler.AddressU=D3D11_TEXTURE_ADDRESS_MODE(effective.sampler(stage,T::AddressU)+1);sampler.AddressV=D3D11_TEXTURE_ADDRESS_MODE(effective.sampler(stage,T::AddressV)+1);sampler.AddressW=D3D11_TEXTURE_ADDRESS_MODE(effective.sampler(stage,T::AddressW)+1);
            sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MinLOD=float(effective.sampler(stage,T::MinimumMip));sampler.MaxLOD=effective.sampler(stage,T::MipFilter)?float(effective.sampler(stage,T::MaximumMip)):0;
        }
        if(e.draws==4)need(p.textureHeaders[1]==PPC_LOAD_U32(0x82DFEB64),"composite second input differs");
        s.backend.requireSelectedTargets({p.target,nullptr,nullptr,nullptr},p.target==p.main?p.depth:nullptr);
        s.backend.drawDistortion(p.target,d);++p.draws;++s.distortionDraws;p.locked=false;p.quad=p.constants=0;
        if(s.distortionDraws<=20)std::fprintf(stderr,"[NATIVE DISTORTION DRAW] stage=%u ps=%08X extent=%ux%u copies=%u original_cpu_vertices=true\n",p.draws,p.pixel,p.target->width,p.target->height,p.copies);
        c.lr=site+4;return;
    }
    need(false,"unknown bounded endpoint");
}
uint64_t EngineDriver::distortionPhaseCount() const {state->requireCaller(state->runtime.base);return state->distortionPhases;}
uint64_t EngineDriver::distortionDrawCount() const {state->requireCaller(state->runtime.base);return state->distortionDraws;}

void EngineDriver::ballEffectOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.requireCaller(base);auto& b=s.ballEffect;
    const auto need=[](bool value,const char* reason){if(!value)throw Failure(reason);};
    const uint32_t sp=c.r1.u32;
    need(currentContext==&c&&s.ready&&s.submissionReady&&sp>=0x100&&!(sp&15),
         "Ball effect requires its live original CPU context");
    if(site==0x82771998||site==0x827719B4) {
        const bool first=site==0x82771998;
        const uint32_t source=first?0x82DFEA60:0x82DFEAA0;
        need(!b.cpu&&PPC_LOAD_U32(sp)==sp+0x70&&c.r30.u32==0x82DFEA20&&c.r31.u32==0x82D10000&&
             !c.r3.u32&&c.r4.u32==(first?0u:4u)&&c.r5.u32==source&&c.r6.u32==4&&
             c.r7.u64==(uint64_t(1)<<(first?63:62))&&(first||b.matrices==1),
             "Ball effect original matrix upload differs");
        std::array<std::array<float,4>,4> matrix{};
        for(unsigned row=0;row<4;++row)for(unsigned lane=0;lane<4;++lane) {
            auto& value=matrix[row][lane];value=std::bit_cast<float>(PPC_LOAD_U32(source+16*row+4*lane));
            need(std::isfinite(value),"Ball effect matrix is nonfinite");
        }
        std::copy(matrix.begin(),matrix.end(),b.draw.constants.begin()+(first?0:4));
        b.matrices=first?1:3;c.lr=site+4;return;
    }
    need(PPC_LOAD_U32(sp)==sp+0xB0,"Ball effect original draw frame differs");
    if(site==0x82771AE0) {
        need(!b.cpu&&b.matrices==3&&!s.postFilter.cpu&&!s.im2d.active&&!s.directSprite.active&&!s.coronaQueries.active,
             "Ball effect has missing matrices or an overlapping draw");
        s.runtime.pointer(c.r31.u32,0xBC,false);
        const auto camera=cameraBinding();
        need(camera.camera==PPC_LOAD_U32(s.engine)&&camera.camera==PPC_LOAD_U32(0x82E3DD60)&&
             PPC_LOAD_U32(0x82D0CB1C)==1&&camera.colorIdentity==PPC_LOAD_U32(0x82D0CF5C)&&
             camera.depthIdentity==PPC_LOAD_U32(0x82D0CF58),"Ball effect active camera differs");
        s.screenRecord(base,18);s.screenRecord(base,19);
        need(s.screenDeclarationIds[1]&&PPC_LOAD_U32(0x82DFEB34)==s.screenDeclarationIds[1]&&
             s.screenDeclarations.contains(s.screenDeclarationRecords[1]),"Ball effect declaration owner differs");
        std::array<float,4> center{};
        for(unsigned i=0;i<4;++i){center[i]=std::bit_cast<float>(PPC_LOAD_U32(sp+0x60+4*i));
            need(std::isfinite(center[i]),"Ball effect center/alpha is nonfinite");}
        need(center[3]>=0&&center[3]<=1,"Ball effect original clamped alpha differs");
        b.draw.constants[8]=center;b.cpu=&c;b.stack=sp;b.owner=c.r31.u32;b.camera=camera.camera;
        b.inputs=b.locked=false;
        // Replace only the SDK constant-bank write. Original texture lookup,
        // packed blend selection and animated UV calculations continue at B28.
        c.r30.u64=uint64_t(int64_t(int32_t(0x82D10000)));c.r4.u64=0;c.r6.u64=0x80000000ull;
        return;
    }
    need(b.cpu==&c&&b.stack==sp&&b.owner==c.r31.u32&&b.camera==cameraBinding().camera,
         "Ball effect lost its original draw scope");
    if(site==0x82771CBC) {
        need(!b.inputs&&!b.locked,"Ball effect input binding repeated");
        s.screenRecord(base,18);s.screenRecord(base,19);
        need(PPC_LOAD_U32(0x82DFEB34)==s.screenDeclarationIds[1]&&
             s.screenDeclarations.contains(s.screenDeclarationRecords[1]),"Ball effect declaration changed");
        const uint32_t texture=PPC_LOAD_U32(b.owner+0xB8);
        s.runtime.pointer(texture,4,false);const uint32_t raster=PPC_LOAD_U32(texture);
        b.draw.texture=textureRaster(raster);s.backend.requireEngineTexture(0,b.draw.texture);
        for(uint32_t address:{0x82CD1A68u,0x82CD1A6Cu,0x82CD1A70u})s.runtime.pointer(address,4,true);
        PPC_STORE_U32(0x82CD1A6C,PPC_LOAD_U32(0x82CF25B4));
        PPC_STORE_U32(0x82CD1A70,PPC_LOAD_U32(0x82CF2584));
        PPC_STORE_U32(0x82CD1A68,s.screenDeclarationIds[1]);b.inputs=true;c.lr=0x82771D08;return;
    }
    if(site==0x82771D18) {
        need(b.inputs&&!b.locked&&!c.r3.u32&&c.r4.u32==13&&c.r5.u32==4&&c.r6.u32==16,
             "Ball effect original quad reservation differs");
        if(!b.staging)b.staging=s.runtime.allocatePhysical(0,4096,PAGE_READWRITE,0,UINT32_MAX,4096);
        need(b.staging!=0,"Ball effect CPU vertex allocation failed");
        for(unsigned i=0;i<16;++i)PPC_STORE_U32(b.staging+4*i,0x7FC00000);
        b.locked=true;c.r3.u64=b.staging;c.lr=site+4;return;
    }
    need(site==0x82771D98&&b.inputs&&b.locked&&!c.r3.u32,"Ball effect completion has no original quad reservation");
    auto& d=b.draw;
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)d.vertices[i][j]=std::bit_cast<float>(PPC_LOAD_U32(b.staging+16*i+4*j));
    const auto camera=cameraBinding();
    need(PPC_LOAD_U32(0x82CD1A6C)==PPC_LOAD_U32(0x82CF25B4)&&PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(0x82CF2584)&&
         PPC_LOAD_U32(0x82CD1A68)==s.screenDeclarationIds[1],"Ball effect shader/declaration caches changed");
    using S=Graphics::ScalarState;using T=Graphics::SamplerState;const auto& e=s.renderState->effective();
    d.viewport=camera.viewport;d.depthEnable=e.scalar(S::DepthEnable);d.depthWrite=e.scalar(S::DepthWrite);d.depthCompare=e.scalar(S::DepthCompare);
    d.colorMask=e.scalar(S::ColorMask0);d.cull=e.scalar(S::Cull);d.stencilEnable=e.scalar(S::StencilEnable);d.fill=e.scalar(S::Fill);
    d.scissorEnable=e.scalar(S::ScissorEnable);d.halfPixelOffset=e.scalar(S::HalfPixelOffset);d.viewportEnable=e.scalar(S::ViewportEnable);
    d.clipPlaneEnable=e.scalar(S::ClipPlaneEnable);d.alphaToMask=e.scalar(S::AlphaToMask);d.multisampleMask=e.scalar(S::MultisampleMask);
    d.depthBiasBits=e.scalar(S::DepthBias);d.slopeBiasBits=e.scalar(S::SlopeBias);d.alphaTest=e.scalar(S::AlphaTest);d.alphaReference=e.scalar(S::AlphaReference);
    // CB80's effective packed equation is independent of the scalar blend
    // shadow, exactly as for the original post-filter's direct draws.
    d.blendEnable=1;d.blendWord=e.effectiveBlend(0);d.multisampleAntialias=e.scalar(S::MultisampleAntialias);
    if(d.scissorEnable){const auto scissor=s.backend.scissor();need(bool(scissor),"Ball effect enabled scissor has no retained rectangle");d.scissor=*scissor;}
    need((!d.alphaTest||e.scalar(S::AlphaCompare)==4)&&!e.scalar(S::TessellationMode),"Ball effect alpha/tessellation state differs");
    need(e.sampler(0,T::AddressU)<=2&&e.sampler(0,T::AddressV)<=2&&e.sampler(0,T::AddressW)<=2&&
         e.sampler(0,T::Magnification)==1&&e.sampler(0,T::Minification)==1&&e.sampler(0,T::MipFilter)<=2&&
         e.sampler(0,T::MaximumAnisotropy)==1&&!e.sampler(0,T::LodBiasBits),"Ball effect inherited sampler differs");
    d.sampler={};d.sampler.Filter=e.sampler(0,T::MipFilter)==2?D3D11_FILTER_MIN_MAG_MIP_LINEAR:D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    d.sampler.AddressU=D3D11_TEXTURE_ADDRESS_MODE(e.sampler(0,T::AddressU)+1);
    d.sampler.AddressV=D3D11_TEXTURE_ADDRESS_MODE(e.sampler(0,T::AddressV)+1);
    d.sampler.AddressW=D3D11_TEXTURE_ADDRESS_MODE(e.sampler(0,T::AddressW)+1);
    d.sampler.MaxAnisotropy=1;d.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
    d.sampler.MinLOD=float(e.sampler(0,T::MinimumMip));d.sampler.MaxLOD=e.sampler(0,T::MipFilter)?float(e.sampler(0,T::MaximumMip)):0;
    s.backend.requireEngineTexture(0,d.texture);
    s.backend.drawBallEffect(s.cameraColor(camera),s.cameraDepth(camera),d);++s.ballEffectDraws;
    if(s.ballEffectDraws<=8)std::fprintf(stderr,"[NATIVE BALL EFFECT DRAW] owner=%08X draw=%llu blend=%08X original_cpu_vertices=true\n",
        b.owner,static_cast<unsigned long long>(s.ballEffectDraws),d.blendWord);
    d.texture.reset();b.cpu=nullptr;b.stack=b.owner=b.camera=0;b.inputs=b.locked=false;c.lr=site+4;
}
uint64_t EngineDriver::ballEffectDrawCount() const {state->requireCaller(state->runtime.base);return state->ballEffectDraws;}
// Bounded native endpoints for 82770AF0. Its layer activity tests, weight
// normalization, strip stores and final layer resets remain original CPU work.
void EngineDriver::lumaOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.requireCaller(base);auto& l=s.luma;
    auto need=[&](bool ok,const char* why){if(!ok){char message[240];std::snprintf(message,sizeof(message),"Luma %08X: %s",site,why);throw Failure(message);}};
    const uint32_t sp=c.r1.u32;
    need(currentContext==&c&&s.ready&&s.submissionReady&&sp>=0x200&&!(sp&15)&&
         PPC_LOAD_U32(sp)==sp+0xD0&&PPC_LOAD_U32(sp+0xC8)==0x827517B0&&
         c.r27.u32==1&&c.r30.u32==0x82CF24F0&&c.r31.u32==0x82D10000,"original phase-one frame differs");
    using S=Graphics::ScalarState;using T=Graphics::SamplerState;
    if(site==0x82770BF0) {
        need(!l.cpu&&!s.screenEffect.cpu&&!s.postFilter.cpu&&!s.distortion.cpu&&!s.ballEffect.cpu&&!s.im2d.active&&!s.directSprite.active&&!s.coronaQueries.active,
             "pass overlaps another native scope");
        need(!c.r3.u32&&c.r4.u32==1,"half-pixel endpoint arguments differ");
        const auto camera=cameraBinding();
        need(camera.camera==PPC_LOAD_U32(0x82E3DD60)&&PPC_LOAD_U32(0x82D0CB1C)==1&&camera.colorIdentity==PPC_LOAD_U32(0x82D0CF5C)&&
             camera.depthIdentity==PPC_LOAD_U32(0x82D0CF58)&&camera.viewport[2]==1280&&camera.viewport[3]==720,
             "pass lost its active full-size camera");
        State::Luma next;next.cpu=&c;next.stack=sp;next.camera=camera.camera;
        next.main=s.cameraColor(camera);next.depth=s.cameraDepth(camera);
        s.backend.requireSelectedTargets({next.main,nullptr,nullptr,nullptr},next.depth);
        // Original 82770870 on the unnormalized layer selects each branch below.
        const float zero=std::bit_cast<float>(PPC_LOAD_U32(0x821DD0D8)),threshold=std::bit_cast<float>(PPC_LOAD_U32(0x821DD350));
        for(uint32_t i=0;i<3;++i){const uint32_t layer=0x82CF24F0+0x30*i;
            next.active[i]=!(std::bit_cast<float>(PPC_LOAD_U32(layer+0x20))==zero||std::bit_cast<float>(PPC_LOAD_U32(layer+0xC))<threshold);}
        need(next.active[0]||next.active[1]||next.active[2],"pass entered without an active layer");
        directScalar(base,uint32_t(S::HalfPixelOffset),1);
        next.step=1;l=std::move(next);c.lr=site+4;return;
    }
    need(l.cpu==&c&&l.stack==sp&&l.camera==PPC_LOAD_U32(0x82E3DD60)&&s.camera&&s.camera->camera==l.camera&&
         PPC_LOAD_U32(0x82D0CB1C)==1,"pass lost its original CPU scope or camera");
    auto expect=[&](uint32_t step){need(l.step==step,"endpoint order differs");};
    struct Scalar {uint32_t site,step;S id;uint32_t value;};
    constexpr Scalar scalars[]={{0x82770C34,2,S::Cull,0},{0x82770C40,3,S::DepthEnable,0},{0x82770C4C,4,S::AlphaTest,0},{0x82770F30,18,S::BlendEnable,0}};
    for(const auto& e:scalars)if(site==e.site){expect(e.step);need(!c.r3.u32&&c.r4.u32==e.value,"direct scalar arguments differ");
        directScalar(base,uint32_t(e.id),e.value);++l.step;c.lr=site+4;return;}
    if(site==0x82770C28) {
        expect(1);
        need(!c.r3.u32&&!c.r4.u32&&!c.r5.u32&&c.r6.u32==0x82DFE360&&PPC_LOAD_U32(0x82DFEB48)==c.r6.u32&&!c.r7.u32&&!c.r8.u32&&
             !c.r9.u32&&!c.r10.u32&&c.f1.f64==0&&!PPC_LOAD_U32(sp+0x5C)&&!PPC_LOAD_U32(sp+0x64)&&c.r29.u32==0x82DFEA20,
             "scene resolve arguments differ");
        need(PPC_LOAD_U16(0x82DFEB38)==1280&&PPC_LOAD_U16(0x82DFEB3A)==720,"scene resolve viewport selection differs");
        s.backend.requireSelectedTargets({l.main,nullptr,nullptr,nullptr},l.depth);
        // Selector zero copies color zero; unlike 827724DC it requests no clear.
        s.viewportSurfaces->resolveColor(c.r6.u32,l.main,false);
        l.step=2;c.r3.u64=0;c.lr=site+4;return;
    }
    if(site==0x82770C60) {
        expect(5);need(!c.r3.u32&&!c.r4.u32&&c.r5.u32==0x10001,"packed blend arguments differ");
        auto next=s.renderState->effective();next.setPackedBlend(0,c.r5.u32);s.renderState->publishScreenState(next);
        ++l.step;c.lr=site+4;return;
    }
    if(site==0x82770C78) {
        expect(6);s.screenRecord(base,13);
        need(!c.r3.u32&&c.r4.u32==s.screenMaterialBindings[13].handle&&PPC_LOAD_U32(0x82CF234C)==c.r4.u32&&PPC_LOAD_U32(0x82CD1A6C)==c.r4.u32,
             "vertex shader object/cache differs");
        ++l.step;c.lr=site+4;return;
    }
    if(site==0x82770C90) {
        expect(7);
        need(!c.r3.u32&&c.r4.u32==PPC_LOAD_U32(0x82CF24E4)&&PPC_LOAD_U32(0x82CD1A70)==c.r4.u32,"pixel shader object/cache differs");
        need(s.requireOriginalPixelShader(base,c.r4.u32,0x82CF24E4)==0x821559D8,"pixel shader record differs");
        ++l.step;c.lr=site+4;return;
    }
    if(site==0x82770CA8) {
        expect(8);
        need(!c.r3.u32&&!c.r4.u32&&c.r5.u32==0x82DFE360&&PPC_LOAD_U32(0x82DFEB48)==c.r5.u32&&c.r6.u64==0x80000000ull,"texture arguments differ");
        l.input=s.viewportSurfaces->colorTexture(c.r5.u32);++l.step;c.lr=site+4;return;
    }
    if(site==0x82770CB8||site==0x82770CC8) {
        const bool minification=site==0x82770CB8;expect(minification?9:10);
        need(!c.r3.u32&&!c.r4.u32&&c.r5.u32==1,"direct sampler arguments differ");
        directSampler(base,0,minification?0x14:0x10,1);++l.step;c.lr=site+4;return;
    }
    if(site==0x82770CCC) {
        // rlwimi of 1 by 11 into 0x1C00 and by 14 into 0xE000: raw 2 (clamp) for U and V.
        expect(11);auto next=s.renderState->effective();
        need(next.sampler(0,T::Magnification)==1&&next.sampler(0,T::Minification)==1,"linear setters were not applied");
        next.setSampler(0,T::AddressU,2);next.setSampler(0,T::AddressV,2);s.renderState->publishScreenState(next);
        ++l.step;return;
    }
    if(site==0x82770D14) {
        expect(12);
        need(!c.r3.u32&&c.r4.u32&&c.r4.u32==s.screenDeclarationIds[0]&&PPC_LOAD_U32(0x82DFEB30)==c.r4.u32&&
             PPC_LOAD_U32(0x82CD1A68)==c.r4.u32&&s.screenDeclarations.contains(s.screenDeclarationRecords[0]),"float2 declaration owner differs");
        ++l.step;c.lr=site+4;return;
    }
    constexpr uint32_t uploads[]={0x82770D94,0x82770E40,0x82770EA4},defaults[]={0x82770D9C,0x82770E48,0x82770EAC};
    for(uint32_t i=0;i<3;++i)if(site==uploads[i]||site==defaults[i]) {
        expect(13+i);const bool upload=site==uploads[i];const uint32_t layer=0x82CF24F0+0x30*i;
        need(upload==l.active[i],"layer branch differs from its original activity test");
        if(upload) {
            need(!c.r3.u32&&c.r4.u32==2*i&&c.r5.u32==layer&&c.r6.u32==2&&c.r7.u64==(i<2?0x8000000000000000ull:0x4000000000000000ull),
                 "normalized layer upload arguments differ");
            std::array<std::array<float,4>,2> values{};
            for(uint32_t j=0;j<8;++j)values[j/4][j%4]=std::bit_cast<float>(PPC_LOAD_U32(layer+4*j));
            for(float value:values[0])need(std::isfinite(value),"normalized layer color is nonfinite");
            need(std::isfinite(values[1][0])&&std::isfinite(values[1][1]),"normalized layer curve is nonfinite");
            s.lumaConstants[2*i]=values[0];s.lumaConstants[2*i+1]=values[1];c.lr=site+4;
        } else {
            // Replaces the direct device-shadow store of the default color.
            need(c.r29.u32==0x82152E20&&c.r28.u64==(i?0x8000000000000000ull:0),"default layer color block differs");
            for(uint32_t j=0;j<4;++j)s.lumaConstants[2*i][j]=std::bit_cast<float>(PPC_LOAD_U32(c.r29.u32+4*j));
            // Block 82770D9C derives the c0..3 dirty bit that later layers pass on.
            if(!i)c.r28.u64=0x8000000000000000ull;
        }
        ++l.step;return;
    }
    if(site==0x82770EF4) {
        expect(16);need(!c.r3.u32&&c.r4.u32==6&&c.r5.u32==4&&c.r6.u32==8,"strip reservation differs");
        if(!s.lumaStaging)s.lumaStaging=s.runtime.allocatePhysical(0,4096,PAGE_READWRITE,0,UINT32_MAX,4096);
        need(s.lumaStaging!=0,"strip staging allocation failed");
        for(uint32_t i=0;i<8;++i)PPC_STORE_U32(s.lumaStaging+4*i,0x7FC00000);
        ++l.step;c.r3.u64=s.lumaStaging;c.lr=site+4;return;
    }
    if(site==0x82770F24) {
        expect(17);need(!c.r3.u32,"strip completion arguments differ");s.screenRecord(base,13);
        need(PPC_LOAD_U32(0x82CD1A6C)==s.screenMaterialBindings[13].handle&&PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(0x82CF24E4)&&
             PPC_LOAD_U32(0x82CD1A68)==s.screenDeclarationIds[0]&&s.screenDeclarations.contains(s.screenDeclarationRecords[0])&&
             PPC_LOAD_U32(0x82D0CF5C)==s.camera->colorIdentity&&l.input,"strip lost its original shader/declaration/target bindings");
        Graphics::PostFilterDraw d;d.pixelShader=0x821559D8;d.input=l.input;d.viewport=s.camera->viewport;
        std::array<float,8> strip{};for(uint32_t i=0;i<8;++i)strip[i]=std::bit_cast<float>(PPC_LOAD_U32(s.lumaStaging+4*i));d.strip=strip;
        std::copy(s.lumaConstants.begin(),s.lumaConstants.end(),d.pixelConstants.begin());
        const auto& e=s.renderState->effective();
        // CB80's packed equation applies independently of the scalar blend shadow.
        d.blendEnable=1;d.blendWord=e.effectiveBlend(0);d.expandedBlend=e.scalar(S::ExpandedBlend0);
        d.colorMask=e.scalar(S::ColorMask0);d.depthEnable=e.scalar(S::DepthEnable);d.depthWrite=e.scalar(S::DepthWrite);d.depthCompare=e.scalar(S::DepthCompare);
        d.stencilEnable=e.scalar(S::StencilEnable);d.alphaTest=e.scalar(S::AlphaTest);d.cull=e.scalar(S::Cull);d.fill=e.scalar(S::Fill);
        d.scissorEnable=e.scalar(S::ScissorEnable);d.halfPixelOffset=e.scalar(S::HalfPixelOffset);d.viewportEnable=e.scalar(S::ViewportEnable);
        if(d.scissorEnable){const auto scissor=s.backend.scissor();need(bool(scissor),"enabled scissor has no retained rectangle");d.scissor=*scissor;}
        d.clipPlaneEnable=e.scalar(S::ClipPlaneEnable);d.multisampleAntialias=e.scalar(S::MultisampleAntialias);d.multisampleMask=e.scalar(S::MultisampleMask);
        d.alphaToMask=e.scalar(S::AlphaToMask);d.depthBiasBits=e.scalar(S::DepthBias);d.slopeBiasBits=e.scalar(S::SlopeBias);
        need(e.sampler(0,T::AddressU)==2&&e.sampler(0,T::AddressV)==2&&e.sampler(0,T::AddressW)<=2&&
             e.sampler(0,T::Magnification)==1&&e.sampler(0,T::Minification)==1&&e.sampler(0,T::MipFilter)<=2&&
             e.sampler(0,T::MaximumAnisotropy)==1&&!e.sampler(0,T::LodBiasBits),"inherited sampler differs");
        d.sampler={};d.sampler.Filter=e.sampler(0,T::MipFilter)==2?D3D11_FILTER_MIN_MAG_MIP_LINEAR:D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        d.sampler.AddressU=d.sampler.AddressV=D3D11_TEXTURE_ADDRESS_CLAMP;d.sampler.AddressW=D3D11_TEXTURE_ADDRESS_MODE(e.sampler(0,T::AddressW)+1);
        d.sampler.MaxAnisotropy=1;d.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;d.sampler.MinLOD=float(e.sampler(0,T::MinimumMip));
        d.sampler.MaxLOD=e.sampler(0,T::MipFilter)?float(e.sampler(0,T::MaximumMip)):0;
        s.backend.requireSelectedTargets({l.main,nullptr,nullptr,nullptr},l.depth);
        s.backend.drawPostFilter(l.main,d);++s.lumaDraws;
        if(s.lumaDraws<=8||!(s.lumaDraws%512))std::fprintf(stderr,"[NATIVE LUMA DRAW] layers=%u%u%u camera=%08X count=%llu original_cpu_constants=true original_cpu_vertices=true\n",
            unsigned(l.active[0]),unsigned(l.active[1]),unsigned(l.active[2]),l.camera,static_cast<unsigned long long>(s.lumaDraws));
        ++l.step;c.lr=site+4;return;
    }
    if(site==0x82770F3C) {
        expect(19);need(!c.r3.u32&&c.r4.u32==1,"depth restore arguments differ");
        s.backend.requireSelectedTargets({l.main,nullptr,nullptr,nullptr},l.depth);
        directScalar(base,uint32_t(S::DepthEnable),1);l={};c.lr=site+4;return;
    }
    need(false,"unknown bounded endpoint");
}
uint64_t EngineDriver::lumaDrawCount() const {state->requireCaller(state->runtime.base);return state->lumaDraws;}
// Bounded native endpoints for the original Dof/Blur/Bloom/Fog/Sat passes. Their
// gating, layer normalization, constant math, vertex stores and resets stay
// original; runtime/screen_effect_sites.h is derived from their instructions.
void EngineDriver::screenEffectOperation(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.requireCaller(base);auto& fx=s.screenEffect;
    using ScreenEffects::Op;const auto& table=ScreenEffects::sites;
    auto need=[&](bool ok,const char* why){if(!ok){char message[240];std::snprintf(message,sizeof(message),"Screen effect %08X: %s",site,why);throw Failure(message);}};
    size_t index=table.size();
    for(size_t i=0;i<table.size();++i)if(table[i].site==site){index=i;break;}
    need(index<table.size(),"site is absent from the derived table");
    const auto& e=table[index];const auto& pass=ScreenEffects::passes[e.pass];const uint32_t sp=c.r1.u32;
    const bool last=index+1==table.size()||table[index+1].pass!=e.pass;
    const uint32_t savedReturn=PPC_LOAD_U32(sp+pass.frame-8);
    need(currentContext==&c&&s.ready&&s.submissionReady&&sp>=0x200&&!(sp&15)&&PPC_LOAD_U32(sp)==sp+pass.frame&&
         std::find(pass.returns.begin(),pass.returns.end(),savedReturn)!=pass.returns.end()&&savedReturn&&c.r31.u32==0x82D10000,
         "original pass frame or caller differs");
    using S=Graphics::ScalarState;using T=Graphics::SamplerState;
    if(!fx.cpu) {
        // Blur's first activation primes its history through the final resolve only.
        need(e.order==0||(e.op==Op::Resolve&&last),"pass entered at an unqualified endpoint");
        if(s.luma.cpu||s.postFilter.cpu||s.distortion.cpu||s.ballEffect.cpu||s.im2d.active||s.directSprite.active||s.coronaQueries.active)
            std::fprintf(stderr,"[NATIVE SCREEN EFFECT SCOPE] site=%08X luma=%u post=%u distortion=%u ball=%u im2d=%u sprite=%u corona=%u\n",
                site,unsigned(bool(s.luma.cpu)),unsigned(bool(s.postFilter.cpu)),unsigned(bool(s.distortion.cpu)),unsigned(bool(s.ballEffect.cpu)),
                unsigned(s.im2d.active),unsigned(s.directSprite.active),unsigned(s.coronaQueries.active));
        need(!s.luma.cpu&&!s.postFilter.cpu&&!s.distortion.cpu&&!s.ballEffect.cpu&&!s.im2d.active&&!s.directSprite.active&&!s.coronaQueries.active,
             "pass overlaps another native scope");
        const auto camera=cameraBinding();
        need(camera.camera==PPC_LOAD_U32(0x82E3DD60)&&PPC_LOAD_U32(0x82D0CB1C)==1&&camera.colorIdentity==PPC_LOAD_U32(0x82D0CF5C)&&
             camera.depthIdentity==PPC_LOAD_U32(0x82D0CF58)&&camera.viewport[2]==1280&&camera.viewport[3]==720,"pass lost its active full-size camera");
        State::ScreenEffect next;next.cpu=&c;next.pass=e.pass;next.stack=sp;next.camera=camera.camera;next.nextOrder=e.order;
        next.main=s.cameraColor(camera);next.depth=s.cameraDepth(camera);
        s.backend.requireSelectedTargets({next.main,nullptr,nullptr,nullptr},next.depth);
        if(!s.effectShadow) {
            s.effectShadow=s.runtime.allocatePhysical(0,0x2000,PAGE_READWRITE,0,UINT32_MAX,4096);
            need(s.effectShadow!=0,"console-device shadow allocation failed");std::memset(s.runtime.pointer(s.effectShadow,0x2000,true),0,0x2000);
        }
        fx=std::move(next);
    }
    need(fx.cpu==&c&&fx.pass==e.pass&&fx.stack==sp&&e.order==fx.nextOrder&&fx.camera==PPC_LOAD_U32(0x82E3DD60)&&
         s.camera&&s.camera->camera==fx.camera&&PPC_LOAD_U32(0x82D0CB1C)==1,"endpoint order or pass scope differs");
    s.settleScreenEffectDevice(base);
    switch(e.op) {
    case Op::Scalar:
        need(!c.r3.u32&&c.r4.u32==e.b,"direct scalar arguments differ");directScalar(base,e.a,e.b);c.lr=site+4;break;
    case Op::Sampler:
        need(!c.r3.u32&&c.r4.u32==e.b&&c.r5.u32==e.c,"direct sampler arguments differ");directSampler(base,e.b,e.a,e.c);c.lr=site+4;break;
    case Op::PackedBlend: {
        need(!c.r3.u32&&c.r4.u32==e.a&&c.r5.u32==e.b,"packed blend arguments differ");
        auto next=s.renderState->effective();next.setPackedBlend(e.a,e.b);s.renderState->publishScreenState(next);c.lr=site+4;break;
    }
    case Op::Resolve: {
        const uint32_t header=PPC_LOAD_U32(e.a);
        need(!c.r3.u32&&c.r4.u32==e.b&&!c.r5.u32&&c.r6.u32==header&&!c.r7.u32&&!c.r8.u32&&!c.r9.u32&&!c.r10.u32&&c.f1.f64==0&&
             !PPC_LOAD_U32(sp+0x5C)&&!PPC_LOAD_U32(sp+0x64)&&!fx.locked,"scene resolve arguments differ");
        need(PPC_LOAD_U16(0x82DFEB38)==1280&&PPC_LOAD_U16(0x82DFEB3A)==720,"scene resolve viewport selection differs");
        s.backend.requireSelectedTargets({fx.main,nullptr,nullptr,nullptr},fx.depth);
        // Selector zero copies color zero without a clear.
        s.viewportSurfaces->resolveColor(header,fx.main,false);c.r3.u64=0;c.lr=site+4;break;
    }
    case Op::VertexShader: {
        // VS821529C8 (slot13) and the letterbox's VSFlat (slot1) both pass the
        // float2 position through; the native draw uses its VS821529C8 artifact.
        need(e.a==0x82CF234C||e.a==0x82CF231C,"vertex shader handle is not a qualified screen record");
        const uint32_t slot=e.a==0x82CF234C?13u:1u;s.screenRecord(base,slot);
        need(!c.r3.u32&&c.r4.u32==PPC_LOAD_U32(e.a)&&c.r4.u32==s.screenMaterialBindings[slot].handle&&PPC_LOAD_U32(0x82CD1A6C)==c.r4.u32,
             "vertex shader object/cache differs");
        fx.vertexShader=true;fx.vertexSlot=slot;c.lr=site+4;break;
    }
    case Op::PixelShader: {
        uint32_t handle=0;for(uint32_t field:{e.a,e.b,e.c})if(field&&c.r4.u32==PPC_LOAD_U32(field))handle=field;
        need(!c.r3.u32&&handle&&PPC_LOAD_U32(0x82CD1A70)==c.r4.u32,"pixel shader object/cache differs");
        fx.pixelSource=s.requireOriginalPixelShader(base,c.r4.u32,handle);c.lr=site+4;break;
    }
    case Op::Texture:
        need(!c.r3.u32&&c.r4.u32==e.a&&e.a<2&&c.r5.u32==PPC_LOAD_U32(e.b)&&c.r5.u32&&c.r6.u64==e.c,"texture arguments differ");
        fx.textures[e.a]=c.r5.u32;c.lr=site+4;break;
    case Op::Declaration:
        need(!c.r3.u32&&c.r4.u32&&c.r4.u32==PPC_LOAD_U32(e.a)&&c.r4.u32==s.screenDeclarationIds[0]&&PPC_LOAD_U32(0x82CD1A68)==c.r4.u32&&
             s.screenDeclarations.contains(s.screenDeclarationRecords[0]),"float2 declaration owner differs");
        fx.declaration=true;c.lr=site+4;break;
    case Op::Constants: {
        need(!c.r3.u32&&c.r4.u32==e.a&&c.r6.u32==e.b&&c.r7.u64&&!e.c&&c.r5.u32==sp+e.d&&e.a+e.b<=16,"constant upload arguments differ");
        const auto* source=s.runtime.pointer(c.r5.u32,16*e.b,false);
        std::memcpy(s.runtime.pointer(s.effectShadow+0x1780+16*e.a,16*e.b,true),source,16*e.b);c.lr=site+4;break;
    }
    case Op::Begin:
        need(!c.r3.u32&&c.r4.u32==e.a&&c.r5.u32==e.b&&c.r6.u32==e.c&&!fx.locked&&fx.declaration&&fx.vertexShader&&fx.pixelSource,
             "vertex reservation differs or precedes its shader/declaration binding");
        if(!s.effectStaging)s.effectStaging=s.runtime.allocatePhysical(0,4096,PAGE_READWRITE,0,UINT32_MAX,4096);
        need(s.effectStaging!=0,"vertex staging allocation failed");
        for(uint32_t i=0;i<8;++i)PPC_STORE_U32(s.effectStaging+4*i,0x7FC00000);
        fx.locked=true;fx.primitive=e.a;c.r3.u64=s.effectStaging;c.lr=site+4;break;
    case Op::End: {
        need(!c.r3.u32&&fx.locked,"vertex completion has no reservation");
        need(fx.vertexShader&&PPC_LOAD_U32(0x82CD1A6C)==s.screenMaterialBindings[fx.vertexSlot].handle&&PPC_LOAD_U32(0x82CD1A68)==s.screenDeclarationIds[0]&&
             PPC_LOAD_U32(0x82D0CF5C)==s.camera->colorIdentity,"draw lost its shader/declaration/target bindings");
        Graphics::PostFilterDraw d;d.pixelShader=fx.pixelSource;d.viewport=s.camera->viewport;
        const auto& ef=s.renderState->effective();
        auto sampler=[&](uint32_t stage){return s.screenEffectSampler(ef,stage);};
        for(uint32_t stage=0;stage<2;++stage)if(const uint32_t header=fx.textures[stage]) {
            if(header==0x82DFE840){d.depthInput=s.viewportSurfaces->depthTexture(header);d.depthSampler=sampler(stage);continue;}
            auto color=header==0x82DFE8DC?s.viewportSurfaces->queryTexture(header):s.viewportSurfaces->colorTexture(header);
            if(!stage){d.input=color;d.sampler=sampler(0);}else{d.secondaryInput=color;d.secondarySampler=sampler(1);}
        }
        for(uint32_t reg=0;reg<10;++reg)for(uint32_t lane=0;lane<4;++lane)
            d.pixelConstants[reg][lane]=std::bit_cast<float>(PPC_LOAD_U32(s.effectShadow+0x1780+16*reg+4*lane));
        if(fx.primitive==6){std::array<float,8> strip{};for(uint32_t i=0;i<8;++i)strip[i]=std::bit_cast<float>(PPC_LOAD_U32(s.effectStaging+4*i));d.strip=strip;}
        else {need(fx.primitive==8,"primitive is neither the original strip nor rectangle list");
            for(uint32_t i=0;i<6;++i)d.vertices[i]=std::bit_cast<float>(PPC_LOAD_U32(s.effectStaging+4*i));}
        s.screenEffectDrawState(d,ef);
        s.backend.requireSelectedTargets({fx.main,nullptr,nullptr,nullptr},fx.depth);
        // Preserve original normalization, request resets, state restoration and
        // every resolve. Blur's final resolve therefore keeps history current
        // while disabled; enabling it never reuses a stale pre-toggle frame.
        const auto& settings=s.runtime.videoSettings;
        const bool submit=fx.pass==0?settings.depthOfField:fx.pass==1?settings.motionBlur:fx.pass==2?settings.bloom:
            fx.pass==3?settings.atmosphericFog:fx.pass==4?settings.colorGrading:fx.pass==5?settings.cinematicLetterbox:true;
        s.backend.drawPostFilter(fx.main,d,submit);
        static constexpr const char* names[]{"dof","blur","bloom","fog","sat","letterbox","overlay"};
        if(submit){const auto draws=++s.screenEffectDraws[fx.pass];
            if(draws<=4||!(draws%512))
                std::fprintf(stderr,"[NATIVE SCREEN EFFECT DRAW] pass=%s ps=%08X camera=%08X count=%llu original_cpu_constants=true original_cpu_vertices=true\n",
                    names[fx.pass],fx.pixelSource,fx.camera,static_cast<unsigned long long>(draws));
        }else if(const auto suppressed=++s.screenEffectSuppressed[fx.pass];suppressed<=4)
            std::fprintf(stderr,"[NATIVE SCREEN EFFECT SUPPRESSED] pass=%s ps=%08X camera=%08X count=%llu original_cpu_state=true resolves_preserved=true\n",
                names[fx.pass],fx.pixelSource,fx.camera,static_cast<unsigned long long>(suppressed));
        fx.locked=false;c.lr=site+4;break;
    }
    case Op::Device: {
        // Replaces `lwz r11,-13576(r31)`: the block that follows stores into the
        // shadow. Poison its declared fields so the next endpoint can prove them.
        for(uint32_t bit=0;bit<32;++bit)if(e.a>>bit&1) {
            const uint32_t address=s.effectShadow+0x480+0x18*(bit/2),shift=bit&1?13:10;
            PPC_STORE_U32(address,PPC_LOAD_U32(address)|7u<<shift);
        }
        const uint64_t lanes=uint64_t(e.b)|uint64_t(e.c)<<32;
        for(uint32_t lane=0;lane<64;++lane)if(lanes>>lane&1)PPC_STORE_U32(s.effectShadow+0x1780+4*lane,0x7FA5A5A5);
        fx.device=uint32_t(index);c.r11.u64=s.effectShadow;break;
    }
    }
    fx.nextOrder=e.order+1u;
    // Pass 6 is only the 82755FD0 query prefix; drawGameplayOverlay closes it.
    if(last&&e.pass!=6) {
        need(!fx.locked&&fx.device==UINT32_MAX,"pass ended with an open reservation or console-device block");
        s.backend.requireSelectedTargets({fx.main,nullptr,nullptr,nullptr},fx.depth);fx={};
    }
}
std::array<std::array<float,4>,10> EngineDriver::screenEffectConstants() const {
    auto& s=*state;s.requireCaller(s.runtime.base);auto* base=s.runtime.base;std::array<std::array<float,4>,10> result{};
    if(!s.effectShadow)throw Failure("Screen-effect shadow has not been allocated");
    for(uint32_t reg=0;reg<10;++reg)for(uint32_t lane=0;lane<4;++lane)result[reg][lane]=std::bit_cast<float>(PPC_LOAD_U32(s.effectShadow+0x1780+16*reg+4*lane));
    return result;
}
uint64_t EngineDriver::screenEffectDrawCount(uint32_t pass) const {
    state->requireCaller(state->runtime.base);if(pass>=state->screenEffectDraws.size())throw Failure("Unknown screen-effect pass");
    return state->screenEffectDraws[pass];
}
void EngineDriver::drawScreen(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.requireCaller(base);requireContext(PPC_LOAD_U32(0x82D5DA74));
    if(ctx.r29.u32!=0x82DFEA20 || ctx.r31.u32!=0x82D10000 || ctx.r3.u32 || ctx.r4.u32 || ctx.r27.u32>3)
        throw Failure("Original screen draw midpoint ABI differs");
    const auto binding=cameraBinding();
    if(binding.camera!=PPC_LOAD_U32(s.engine) || binding.camera!=PPC_LOAD_U32(0x82E3DD60) || PPC_LOAD_U32(0x82D0CB1C)!=1 ||
       binding.colorIdentity!=PPC_LOAD_U32(0x82D0CF5C) || binding.depthIdentity!=PPC_LOAD_U32(0x82D0CF58) ||
       binding.viewport[0] || binding.viewport[1] || binding.viewport[2]!=1280 || binding.viewport[3]!=720)
        throw Failure("Original screen requires the active full-size root camera attachments");
    const bool textured=ctx.r28.u32!=0;const uint32_t index=textured?1:0;
    const uint32_t declaration=PPC_LOAD_U32(0x82DFEB30+index*4);
    if(!declaration || declaration!=s.screenDeclarationIds[index] || !s.screenDeclarations.contains(s.screenDeclarationRecords[index]))
        throw Failure("Original screen declaration lacks its native creation owner");
    const uint32_t vs=PPC_LOAD_U32(textured?0x82CF2340:0x82CF231C),ps=PPC_LOAD_U32(textured?0x82CF2334:0x82CF2310);
    const auto& vertex=s.screenMaterial(base,textured?3:1);
    const auto& pixel=s.screenMaterial(base,textured?2:0);
    if(vertex.originalAddress()!=(textured?0x82152880u:0x821525E8u) || pixel.originalAddress()!=(textured?0x82152708u:0x821524C8u))
        throw Failure("Original screen shaders do not match the four recovered records");
    const uint32_t stream=PPC_LOAD_U32(0x82D0CAB0);
    if(stream && stream!=0xFFFFFFFF&&!s.immediateBuffer(base,stream))s.dynamic.buffer(stream);
    for(uint32_t a:{0x82CD1A68u,0x82CD1A6Cu,0x82CD1A70u,0x82D0CAB0u,0x82D0CAB4u,0x82D0CAB8u})s.runtime.pointer(a,4,true);
    Graphics::ScreenDraw draw{};
    s.runtime.pointer(ctx.r30.u32,16,false);
    for(uint32_t i=0;i<4;++i)draw.color[i]=std::bit_cast<float>(PPC_LOAD_U32(ctx.r30.u32+4*i));
    const float left=float(ctx.f31.f64),right=float(ctx.f30.f64),top=float(ctx.f29.f64),bottom=float(ctx.f28.f64);
    if(!std::isfinite(left)||!std::isfinite(right)||!std::isfinite(top)||!std::isfinite(bottom)||
       left< -1 || right>1 || bottom< -1 || top>1 || left>=right || bottom>=top)
        throw Failure("Original screen rectangle is nonfinite, inverted, degenerate or outside the clip volume");
    const float u0=float(ctx.f27.f64),v0=float(ctx.f26.f64),u1=float(ctx.f25.f64),v1=float(ctx.f24.f64);
    draw.vertices={{{left,top,u0,v0},{right,top,u1,v0},{left,bottom,u0,v1},{right,bottom,u1,v1}}};
    if(textured) {
        s.runtime.pointer(ctx.r28.u32,4,false);const auto raster=PPC_LOAD_U32(ctx.r28.u32);
        draw.texture=s.rasters->texture(base,raster);
        if(draw.texture->levelCount()!=1)throw Failure("Original screen texture mip profile is unqualified");
    }
    auto during=s.renderState->effective();during.applyScreenQuadState(ctx.r27.u32,textured);
    const auto checked=during.requireOriginalScreenState(textured);
    draw.blendSelector=checked.blendSelector;draw.colorWriteMask=uint8_t(checked.colorMask);
    draw.alphaTest=checked.alphaTest;draw.alphaReference=checked.alphaReference;
    if(textured) {
        draw.sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        draw.sampler.AddressU=draw.sampler.AddressV=draw.sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        if(checked.sampler->addressW==1)draw.sampler.AddressW=D3D11_TEXTURE_ADDRESS_MIRROR;
        else if(checked.sampler->addressW==2)draw.sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        draw.sampler.MaxAnisotropy=1;draw.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
        draw.sampler.MinLOD=draw.sampler.MaxLOD=0;
    }
    auto after=during;after.finishScreenQuadState(ctx.r27.u32);
    if(s.effects)s.effects->preflightScreenReplacement(base);
    const auto screenBefore=s.backend.screenDrawCount();
    s.backend.drawOriginalScreen(s.cameraColor(binding),s.cameraDepth(binding),draw,checked.retainedDepthWrite,checked.retainedDepthCompare);
    s.im2dInput={};s.im2dDeclaration.reset();s.im2dStream.reset();
    s.renderState->publishScreenState(after);
    PPC_STORE_U32(0x82CD1A68,declaration);PPC_STORE_U32(0x82CD1A6C,vs);PPC_STORE_U32(0x82CD1A70,ps);
    PPC_STORE_U32(0x82D0CAB0,0);PPC_STORE_U32(0x82D0CAB4,0);PPC_STORE_U32(0x82D0CAB8,0);
    const auto replacement=s.backend.completedScreenReplacement(screenBefore,vertex,pixel);
    if(s.effects)s.effects->completeScreenReplacement(base,replacement,declaration,vs,ps);
    if(s.backend.screenDrawCount()<=8)std::fprintf(stderr,
        "[NATIVE SCREEN] submitted original quad=%llu camera=%08X texture=%08X selector=%u clip=(%.9g,%.9g)-(%.9g,%.9g) uv=(%.9g,%.9g)-(%.9g,%.9g) RGBA=(%.9g,%.9g,%.9g,%.9g); explicit float blend, console precision parity unverified\n",
        static_cast<unsigned long long>(s.backend.screenDrawCount()),binding.camera,ctx.r28.u32,draw.blendSelector,left,top,right,bottom,u0,v0,u1,v1,
        draw.color[0],draw.color[1],draw.color[2],draw.color[3]);
}
void EngineDriver::drawGameplayOverlay(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.requireCaller(base);requireContext(PPC_LOAD_U32(0x82D5DA74));
    // Original alpha test and optional query/fallback prefix have run. Replace
    // 827560A4..82756258 before the null console device is bound/submitted.
    // Keep 8275625C's actual stack and nonvolatile-register restoring epilogue.
    if(ctx.r28.u32!=0x82DFEA20 || ctx.r31.u32!=0x82D10000 || PPC_LOAD_U32(0x82D0CAF8))
        throw Failure("Original gameplay overlay frame differs");
    // Flag bit0 survives only when 8276AEE0 found a ready corona query: the
    // bridged 82756034 prefix then bound the 64x8 query texture at stage 1 and
    // the original selects Screen_Xenon_PSModulatedFlat with c1 = its coordinates.
    const bool modulated=(ctx.r30.u32&1)!=0;auto& fx=s.screenEffect;
    if(modulated) {
        if(fx.cpu!=&ctx || fx.pass!=6 || fx.stack!=ctx.r1.u32 || fx.nextOrder!=5 || !fx.textures[1] ||
           fx.textures[1]!=PPC_LOAD_U32(0x82DFEB6C))
            throw Failure("Original modulated gameplay overlay lacks its completed query prefix");
        s.settleScreenEffectDevice(base);
    } else if(fx.cpu) throw Failure("Original flat gameplay overlay overlaps a screen-effect scope");
    const auto binding=cameraBinding();
    if(binding.camera!=PPC_LOAD_U32(s.engine) || binding.camera!=PPC_LOAD_U32(0x82E3DD60) || PPC_LOAD_U32(0x82D0CB1C)!=1 ||
       binding.colorIdentity!=PPC_LOAD_U32(0x82D0CF5C) || binding.depthIdentity!=PPC_LOAD_U32(0x82D0CF58) ||
       binding.viewport[0] || binding.viewport[1])
        throw Failure("Original gameplay overlay requires the selected root camera attachments");
    const uint32_t declaration=PPC_LOAD_U32(0x82DFEB30);
    if(!declaration || declaration!=s.screenDeclarationIds[0] || !s.screenDeclarations.contains(s.screenDeclarationRecords[0]))
        throw Failure("Original gameplay overlay declaration lacks its native owner");
    const auto& vertex=s.screenMaterial(base,1);const auto& pixel=s.screenMaterial(base,0);
    if(vertex.originalAddress()!=0x821525E8 || pixel.originalAddress()!=0x821524C8)
        throw Failure("Original gameplay overlay shader identity differs");
    const uint32_t stream=PPC_LOAD_U32(0x82D0CAB0);
    if(stream && stream!=0xFFFFFFFF&&!s.immediateBuffer(base,stream))s.dynamic.buffer(stream);
    for(uint32_t a:{0x82CD1A68u,0x82CD1A6Cu,0x82CD1A70u,0x82D0CAB0u,0x82D0CAB4u,0x82D0CAB8u})s.runtime.pointer(a,4,true);
    Graphics::ScreenDraw draw{};s.runtime.pointer(ctx.r29.u32,16,false);
    for(uint32_t i=0;i<4;++i)draw.color[i]=std::bit_cast<float>(PPC_LOAD_U32(ctx.r29.u32+4*i));
    const float low=std::bit_cast<float>(PPC_LOAD_U32(0x821DD110)),high=std::bit_cast<float>(PPC_LOAD_U32(0x82000BB0));
    if(low!=-1 || high!=1)throw Failure("Original gameplay overlay rectangle constants changed");
    // SDK primitive 8 is RECTLIST: its three corners expand to four, just as
    // in the original movie path. The fourth corner is (B-A)+C, not a triangle.
    draw.vertices={{{low,low,0,0},{high,low,0,0},{low,high,0,0},{high,high,0,0}}};
    using S=Graphics::ScalarState;
    auto during=s.renderState->effective();
    during.setScalar(S::ExpandedBlend0,1);during.setScalar(S::HalfPixelOffset,1);
    during.setScalar(S::Cull,0);during.setScalar(S::DepthEnable,0);during.setScalar(S::AlphaTest,0);
    during.setPackedBlend(0,0x00010706);
    const auto checked=during.requireOriginalScreenState(false);
    draw.blendSelector=checked.blendSelector;draw.colorWriteMask=uint8_t(checked.colorMask);
    draw.alphaTest=false;draw.alphaReference=checked.alphaReference;
    auto after=during;after.setScalar(S::BlendEnable,0);after.setScalar(S::DepthEnable,1);after.setScalar(S::ExpandedBlend0,0);
    if(s.effects)s.effects->preflightScreenReplacement(base);
    const auto screenBefore=s.backend.screenDrawCount();
    const auto postFilterBefore=s.backend.postFilterDrawCount();
    if(modulated) {
        if(s.requireOriginalPixelShader(base,PPC_LOAD_U32(0x82CF2304),0x82CF2304)!=0x82152318)
            throw Failure("Original modulated gameplay overlay shader identity differs");
        // 8276AEE0 wrote the query coordinates to SP+54/SP+50; the replaced
        // block stores c1 = (SP+54, SP+50, 0, 0).
        s.runtime.pointer(ctx.r1.u32+0x50,8,false);
        Graphics::PostFilterDraw d;d.pixelShader=0x82152318;d.viewport=binding.viewport;
        d.secondaryInput=s.viewportSurfaces->queryTexture(fx.textures[1]);d.secondarySampler=s.screenEffectSampler(s.renderState->effective(),1);
        d.vertices={low,low,high,low,low,high};d.pixelConstants[0]=draw.color;
        d.pixelConstants[1]={std::bit_cast<float>(PPC_LOAD_U32(ctx.r1.u32+0x54)),std::bit_cast<float>(PPC_LOAD_U32(ctx.r1.u32+0x50)),0,0};
        s.screenEffectDrawState(d,during);
        s.backend.requireSelectedTargets({s.cameraColor(binding),nullptr,nullptr,nullptr},s.cameraDepth(binding));
        s.backend.drawPostFilter(s.cameraColor(binding),d);fx={};++s.screenEffectDraws[6];
    } else s.backend.drawOriginalScreen(s.cameraColor(binding),s.cameraDepth(binding),draw,checked.retainedDepthWrite,checked.retainedDepthCompare);
    s.im2dInput={};s.im2dDeclaration.reset();s.im2dStream.reset();s.renderState->publishScreenState(after);
    PPC_STORE_U32(0x82CD1A68,declaration);PPC_STORE_U32(0x82CD1A6C,PPC_LOAD_U32(0x82CF231C));PPC_STORE_U32(0x82CD1A70,PPC_LOAD_U32(modulated?0x82CF2304:0x82CF2310));
    PPC_STORE_U32(0x82D0CAB0,0);PPC_STORE_U32(0x82D0CAB4,0);PPC_STORE_U32(0x82D0CAB8,0);
    if(!modulated) {
        const auto replacement=s.backend.completedScreenReplacement(screenBefore,vertex,pixel);
        if(s.effects)s.effects->completeScreenReplacement(base,replacement,declaration,PPC_LOAD_U32(0x82CF231C),PPC_LOAD_U32(0x82CF2310));
    } else {
        s.backend.requireCompletedModulatedPostFilter(postFilterBefore);
        if(s.effects)s.effects->completeRestoredScreenReplacement(base,postFilterBefore,declaration,
            PPC_LOAD_U32(0x82CF231C),PPC_LOAD_U32(0x82CF2304));
    }
    std::fprintf(stderr,"[NATIVE GAMEPLAY OVERLAY] original=82755FD0 camera=%08X RGBA=(%.9g,%.9g,%.9g,%.9g) query_modulated=%u; native rectangle submitted\n",
        binding.camera,draw.color[0],draw.color[1],draw.color[2],draw.color[3],unsigned(modulated));
}
void EngineDriver::preparePostFilter(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.requireCaller(base);requireContext(PPC_LOAD_U32(0x82D5DA74));
    const auto sp=c.r1.u32;
    if(currentContext!=&c || sp<0x100 || (sp&15) || PPC_LOAD_U32(sp)!=sp+0x70 ||
       c.r31.u32!=0x82D10000 || PPC_LOAD_U32(0x82D0CAF8))
        throw Failure("Original post alpha clear frame/device differs");
    using S=Graphics::ScalarState;
    struct Endpoint {uint32_t site;S id;uint32_t value;};
    constexpr Endpoint endpoints[]={
        {0x82773C80,S::DepthEnable,1},{0x82773C8C,S::DepthWrite,0},{0x82773C98,S::Cull,2},
        {0x82773CA8,S::DepthBias,0x37D1B717},{0x82773CB4,S::SlopeBias,0x3F000000}};
    for(const auto& endpoint:endpoints)if(site==endpoint.site) {
        if(c.r3.u32 || c.r4.u32!=endpoint.value || !(PPC_LOAD_U32(0x82D6CCA8)&2))
            throw Failure("Original post alpha clear final setter differs");
        directScalar(base,uint32_t(endpoint.id),c.r4.u32);c.lr=site+4;return;
    }
    if(site!=0x82773B44 || s.postFilter.cpu || s.im2d.active || s.directSprite.active || s.coronaQueries.active)
        throw Failure("Original post alpha clear has an invalid or overlapping draw scope");
    const auto binding=cameraBinding();
    if(binding.camera!=PPC_LOAD_U32(s.engine) || binding.camera!=PPC_LOAD_U32(0x82E3DD60) || PPC_LOAD_U32(0x82D0CB1C)!=1 ||
       binding.colorIdentity!=PPC_LOAD_U32(0x82D0CF5C) || binding.depthIdentity!=PPC_LOAD_U32(0x82D0CF58) ||
       binding.viewport[0] || binding.viewport[1] || binding.viewport[2]!=1280 || binding.viewport[3]!=720)
        throw Failure("Original post alpha clear requires the selected full-size camera");
    const auto declaration=PPC_LOAD_U32(0x82DFEB30);
    if(!declaration || declaration!=s.screenDeclarationIds[0] || !s.screenDeclarations.contains(s.screenDeclarationRecords[0]) ||
       s.screenMaterial(base,1).originalAddress()!=0x821525E8 || s.screenMaterial(base,0).originalAddress()!=0x821524C8)
        throw Failure("Original post alpha clear shader/declaration owner differs");
    const float low=std::bit_cast<float>(PPC_LOAD_U32(0x821DD110)),high=std::bit_cast<float>(PPC_LOAD_U32(0x82000BB0));
    if(low!=-1 || high!=1 || PPC_LOAD_U32(0x821DD0D8))
        throw Failure("Original post alpha clear rectangle/color constants changed");
    for(uint32_t a:{0x82CD1A68u,0x82CD1A6Cu,0x82CD1A70u})s.runtime.pointer(a,4,true);
    Graphics::ScreenDraw draw{};draw.vertices={{{low,low,0,0},{high,low,0,0},{low,high,0,0},{high,high,0,0}}};
    auto during=s.renderState->effective();
    during.setScalar(S::ColorMask0,8);during.setScalar(S::DepthEnable,0);during.setScalar(S::DepthWrite,0);
    during.setScalar(S::Cull,0);during.setScalar(S::AlphaTest,0);during.setScalar(S::BlendEnable,0);
    const auto checked=during.requireOriginalAlphaClearState();
    draw.colorWriteMask=uint8_t(checked.colorMask);draw.blendSelector=checked.blendSelector;
    // The integer screen target starts as an exact copy; its alpha-only write
    // mask leaves every RGB bit intact, then the original CPU flag store and
    // five state setters run before the unchanged register-restoring epilogue.
    if(s.effects)s.effects->preflightScreenReplacement(base);
    const auto screenBefore=s.backend.screenDrawCount();
    s.backend.drawOriginalScreen(s.cameraColor(binding),s.cameraDepth(binding),draw,false,checked.retainedDepthCompare,false);
    s.im2dInput={};s.im2dDeclaration.reset();s.im2dStream.reset();s.renderState->publishScreenState(during);
    PPC_STORE_U32(0x82CD1A68,declaration);PPC_STORE_U32(0x82CD1A6C,PPC_LOAD_U32(0x82CF231C));PPC_STORE_U32(0x82CD1A70,PPC_LOAD_U32(0x82CF2310));
    const auto replacement=s.backend.completedScreenReplacement(screenBefore,s.screenMaterial(base,1),s.screenMaterial(base,0));
    if(s.effects)s.effects->completeScreenReplacement(base,replacement,declaration,PPC_LOAD_U32(0x82CF231C),PPC_LOAD_U32(0x82CF2310));
    c.lr=0x82773C68;
    static thread_local uint32_t samples{};
    if(sampleHotLog(samples))std::fprintf(stderr,"[NATIVE POST ALPHA CLEAR] camera=%08X; original rectangle submitted, RGB/depth/stencil preserved\n",binding.camera);
}
uint64_t EngineDriver::screenDrawCount() const {state->requireCaller(state->runtime.base);return state->backend.screenDrawCount();}
std::vector<uint8_t> EngineDriver::readbackColor(uint32_t id) {
    bool sampledAlphaOne{};const auto target=color(id,sampledAlphaOne);return state->backend.readbackTarget(target);
}
std::vector<uint8_t> EngineDriver::readbackDepth(uint32_t id) {
    return state->backend.readbackDepthTarget(depth(id));
}
std::shared_ptr<Graphics::RenderTarget> EngineDriver::color(uint32_t id,bool& sampledAlphaOne) const {
    auto& s=*state;s.requireCaller(s.runtime.base);
    if(!s.ready || !id) throw Failure("Native color lookup outside a started driver");
    const auto& r=s.resources.resources();
    for(size_t i:{size_t(0),size_t(3),size_t(4),size_t(5)}) if(id==s.targetIds[i]) {
        sampledAlphaOne=i==3||i==4;
        if(i==0) return r.defaultColor;
        if(i==3) return r.frontColor0;
        if(i==4) return r.frontColor1;
        return r.colorCopy;
    }
    if(s.rasters) if(auto owned=s.rasters->ownedColor(id)) {sampledAlphaOne=false;return owned;}
    throw Failure("Unknown, stale or depth-only native color target ID");
}
std::shared_ptr<Graphics::DepthTarget> EngineDriver::depth(uint32_t id) const {
    auto& s=*state;s.requireCaller(s.runtime.base);
    if(!s.ready || !id) throw Failure("Native depth lookup outside a started driver");
    if(id==s.targetIds[1]) return s.resources.resources().defaultDepth;
    if(id==s.targetIds[2]) return s.resources.resources().depthCopy;
    if(s.rasters) if(auto owned=s.rasters->ownedDepth(id)) return owned;
    if(s.submissionReady && s.shadowTextures) if(auto owned=s.shadowTextures->depth(id)) return owned;
    throw Failure("Unknown, stale or color-only native depth target ID");
}
uint32_t EngineDriver::allocateTargetIdentity() {
    auto& s=*state;s.requireCaller(s.runtime.base);
    if(!s.ready) throw Failure("Native raster target requires a started driver");
    return targetIdentity(s.runtime);
}
}

bool SimpsonsNativeDriverRequest(PPCContext& ctx,uint8_t* base) {
    using namespace Simpsons;
    if(!active || base!=active->base) throw Failure("Native driver hook has an invalid runtime context");
    {static thread_local uint32_t request6Sample{},request10Sample{};
    const uint32_t request=ctx.r3.u32;
    const bool log=request==6?sampleHotLog(request6Sample):request==10?sampleHotLog(request10Sample):true;
    if(log) fprintf(stderr,"[ENGINE DRIVER] request=%u output=%08X input=%08X value=%08X caller=%08X\n",
        ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,uint32_t(ctx.lr));}
    switch(ctx.r3.u32) {
    case 2: {
        if(active->engineDriver) throw Failure("Native engine already owns a driver lifetime");
        observeRegistry(base);
        auto driver=std::make_shared<EngineDriver>(*active,PPC_LOAD_U32(0x82E3DF84),PPC_LOAD_U32(0x82E3DF88));
        driver->start(ctx,base);
        {std::lock_guard lifetime(active->engineDriverMutex);active->engineDriver=std::move(driver);}
        ctx.r3.u32=1;return true;
    }
    case 3:
        if(!active->engineDriver) throw Failure("Original stop requested without a native driver owner");
        active->engineDriver->stop(ctx,base);
        {std::lock_guard lifetime(active->engineDriverMutex);active->engineDriver.reset();}
        ctx.r3.u32=1;return true;
    case 8:
        ctx.r3.u32=active->engineDriver && active->engineDriver->started();return true;
    case 1:
        if(active->engineDriver) throw Failure("Original close requested before native driver stop");
        break;
    }
    return false;
}

void SimpsonsNativeGraphicsStartupPreflight(PPCContext& ctx,uint8_t* base) {
    using namespace Simpsons;
    if(!active || base!=active->base || !active->engineDriver || uint32_t(ctx.lr)!=0x828620D4)
        throw Failure("Native graphics CPU startup requires the verified original caller/driver");
    active->engineDriver->requireContext(PPC_LOAD_U32(0x82D5DA74));
    if(PPC_LOAD_U32(0x82D08BFC) || PPC_LOAD_U32(0x82D5DA78))
        throw Failure("Original graphics CPU managers already exist at startup");
    // Continue every original allocation, array/matrix constructor, publication
    // and null branch. No SDK layout or replacement CPU owner is synthesized.
}

void SimpsonsRejectUnportedGraphics(PPCContext& ctx,uint8_t* base);
namespace {
void observeSceneInput(Simpsons::Runtime& rt,PPCContext& ctx) noexcept {
    if(!rt.resourceAudit.active())return;
    try {
    // Read each field independently. An invalid address is evidence, never a
    // reason to abandon the remaining snapshot or skip the actual guard.
    uint32_t unavailable=0;
    auto rd=[&](uint64_t address) noexcept -> uint32_t {
        try {
            if(!address||address>UINT32_MAX-3){++unavailable;return 0;}
            const auto* bytes=rt.pointer(uint32_t(address),4,false);
            return (uint32_t(bytes[0])<<24)|(uint32_t(bytes[1])<<16)|(uint32_t(bytes[2])<<8)|bytes[3];
        }catch(...){++unavailable;return 0;}
    };
    auto field=[&](uint32_t parent,uint32_t offset) noexcept -> uint32_t {
        if(!parent){++unavailable;return 0;}
        return rd(uint64_t(parent)+offset);
    };
    const auto packet=ctx.r3.u32,flags=rd(0x82D6CCA8),metadata=rd(packet),geometry=field(metadata,0xC);
    const auto typed=flags==0x40?rd(0x82D6D8A0):field(packet,flags==2?0x1C:0x18);
    const auto wrapper=field(typed,0x18),identity=field(typed,0x1C);
    uint32_t source=0,cache=0,pool=0;std::string ownership="owner-unvalidated";
    if(rt.engineDriver)try {
        const auto owner=rt.engineDriver->effects().auditIdentity(identity);
        source=owner[0];cache=owner[1];pool=owner[2];
        ownership="phase="+std::to_string(owner[3]);
    }catch(...){ownership="owner-unreadable-or-unregistered";}
    const auto vtable=rd(typed),a8=field(typed,0xA8),ac=field(typed,0xAC),
        stride=field(geometry,4),elements=field(geometry,8),object=field(packet,4),alias=field(wrapper,0x18);
    char asset[32],parameters[320],instance[384];
    const auto argumentClass=[](uint32_t value){return value<=1?(value?"1":"0"):"unclassified";};
    std::snprintf(asset,sizeof(asset),"source:%08X",source);
    std::snprintf(parameters,sizeof(parameters),
        "source=%08X flags=%08X vtable=%08X technique_A8=%08X technique_AC=%08X stride=%u elements=%u r4_boolean=%s r5_boolean=%s r6_boolean=%s unreadable_fields=%u",
        source,flags,vtable,a8,ac,stride,elements,argumentClass(ctx.r4.u32),argumentClass(ctx.r5.u32),argumentClass(ctx.r6.u32),unavailable);
    std::snprintf(instance,sizeof(instance),
        "packet=%08X metadata=%08X geometry=%08X object=%08X typed=%08X wrapper=%08X identity=%08X wrapper_identity=%08X cache=%08X pool=%08X sp=%08X function=%08X dispatch_caller=%08X r4=%08X r5=%08X r6=%08X",
        packet,metadata,geometry,object,typed,wrapper,identity,alias,cache,pool,ctx.r1.u32,ctx.lastFunction,uint32_t(ctx.lr),ctx.r4.u32,ctx.r5.u32,ctx.r6.u32);
    rt.resourceAudit.observe("effect_pass",asset,uint32_t(ctx.lr),parameters,ownership,rt.nativeDepthCopyCount.load(),instance);
    }catch(...){std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] scene input capture failed\n");}
}
}
void SimpsonsNativeSceneDispatch(PPCContext& ctx,uint8_t* base) {
    using namespace Simpsons;
    if(active&&base==active->base)observeSceneInput(*active,ctx);
    if(!active||base!=active->base||currentContext!=&ctx||!active->engineDriver||ctx.lastFunction!=0x82740680)
        throw Failure("Invalid native scene dispatcher frame");
    const auto flags=PPC_LOAD_U32(0x82D6CCA8);const bool rigid=flags==0;
    if(flags==0x20&&uint32_t(ctx.lr)==0x8273B864) {
        const auto packet=ctx.r3.u32;active->pointer(packet,0x24,false);
        const auto metadata=PPC_LOAD_U32(packet),object=PPC_LOAD_U32(packet+4),typed=PPC_LOAD_U32(packet+0x18);
        active->pointer(metadata,0x2C,false);active->pointer(object,0x40,false);active->pointer(typed,0xB0,false);
        const auto wrapper=PPC_LOAD_U32(typed+0x18),identity=PPC_LOAD_U32(typed+0x1C);
        active->pointer(wrapper,0x30,false);
        const auto owner=active->engineDriver->effects().viewHeader(identity);
        active->pointer(owner.manager,0x1C,false);
        if(owner.source!=0x8205B848u||owner.phase!=EngineEffects::Phase::Reflected||
           active->engineDriver->effects().typedReflectionCount(identity)!=1||
           PPC_LOAD_U32(typed)!=0x82061758u||PPC_LOAD_U32(typed+0x10)!=owner.manager||
           PPC_LOAD_U32(typed+0x18)!=owner.wrapper||PPC_LOAD_U32(typed+0x1C)!=owner.identity||
           PPC_LOAD_U32(typed+0xA8)!=0x0003FFFCu||owner.wrapper!=wrapper||owner.identity!=identity||
           owner.manager!=PPC_LOAD_U32(0x82D08BFC)||owner.pool!=PPC_LOAD_U32(owner.manager+0x18)||
           !owner.cache||owner.cacheBytes!=120||
           PPC_LOAD_U32(wrapper)!=0x820B7140u||PPC_LOAD_U32(wrapper+0xC)!=owner.manager||
           PPC_LOAD_U32(wrapper+0x10)!=owner.identity||PPC_LOAD_U32(wrapper+0x14)!=owner.wrapper||
           PPC_LOAD_U32(wrapper+0x18)!=owner.identity||PPC_LOAD_U32(wrapper+0x1C)!=owner.cache||
           PPC_LOAD_U32(wrapper+0x20)!=owner.cacheBytes||PPC_LOAD_U32(wrapper+0x24)!=owner.cache||
           PPC_LOAD_U32(wrapper+0x28)!=1)
            throw Failure("Original VFX rigid-textured scene lost its reflected one-technique owner");
        active->engineDriver->requireContext(PPC_LOAD_U32(0x82D63028));
        // This exact caller/flag pair enters the original generic scene path.
        // Keep its cache/record/fallback logic in AOT; downstream native
        // boundaries still qualify activation and drawing independently.
        return;
    }
    if((flags==0||flags==2)&&uint32_t(ctx.lr)==0x8273B4E0) {
        const auto packet=ctx.r3.u32;active->pointer(packet,0x24,false);
        // The ordinary immediate branch and Burp have different original
        // packet slots. Inspect the typed header before selecting mono; other
        // flags-zero effects retain their independently qualified path below.
        const auto typed=PPC_LOAD_U32(packet+(flags==2?0x1C:0x18)),metadata=PPC_LOAD_U32(packet);
        active->pointer(typed,4,false);
        if(flags==2||PPC_LOAD_U32(typed)==0x8215020C) {
        active->pointer(typed,0xC0,false);active->pointer(metadata,0x2C,false);
        const auto wrapper=PPC_LOAD_U32(typed+0x18);active->pointer(wrapper,0x30,false);
        const auto owner=active->engineDriver->effects().viewHeader(PPC_LOAD_U32(wrapper+0x10));
        active->pointer(owner.manager,0x1C,false);
        if(owner.source!=0x8211F480||owner.phase!=EngineEffects::Phase::Reflected||
           active->engineDriver->effects().typedReflectionCount(owner.identity)!=1||
           owner.wrapper!=wrapper||owner.identity!=PPC_LOAD_U32(typed+0x1C)||
           PPC_LOAD_U32(typed)!=0x8215020C||owner.manager!=PPC_LOAD_U32(0x82D08BFC)||
           PPC_LOAD_U32(typed+0x10)!=owner.manager||owner.pool!=PPC_LOAD_U32(owner.manager+0x18)||
           !owner.cache||!owner.cacheBytes||PPC_LOAD_U32(wrapper)!=0x820B7140||
           PPC_LOAD_U32(wrapper+0xC)!=owner.manager||PPC_LOAD_U32(wrapper+0x10)!=owner.identity||
           PPC_LOAD_U32(wrapper+0x14)!=wrapper||PPC_LOAD_U32(wrapper+0x18)!=owner.identity||
           PPC_LOAD_U32(wrapper+0x1C)!=owner.cache||PPC_LOAD_U32(wrapper+0x20)!=owner.cacheBytes||
           PPC_LOAD_U32(wrapper+0x24)!=owner.cache||PPC_LOAD_U32(wrapper+0x28)!=2||
           PPC_LOAD_U32(typed+0xA8)!=0x0007FFFC||PPC_LOAD_U32(typed+0xAC)!=0x0003FFFC||
           !PPC_LOAD_U32(packet+0x18)) {
            std::fprintf(stderr,"[NATIVE MONO DISPATCH REJECT] packet=%08X typed=%08X source=%08X phase=%u wrapper=%08X/%08X identity=%08X/%08X vtable=%08X manager=%08X/%08X technique=%08X/%08X skin=%08X material=%08X\n",
                packet,typed,owner.source,unsigned(owner.phase),wrapper,owner.wrapper,owner.identity,PPC_LOAD_U32(typed+0x1C),
                PPC_LOAD_U32(typed),owner.manager,PPC_LOAD_U32(0x82D08BFC),PPC_LOAD_U32(typed+0xA8),PPC_LOAD_U32(typed+0xAC),
                PPC_LOAD_U32(metadata+0x24),PPC_LOAD_U32(packet+0x18));
            throw Failure("Original mono scene lost its reflected owner or geometry profile");
        }
        active->engineDriver->requireContext(PPC_LOAD_U32(0x82D6D890));
        // Preserve both original branches, world/per-object callbacks and
        // static/skinned submesh split. Native mono activation, palette upload
        // and draws qualify the selected geometry independently.
        return;
        }
    }
    if(uint32_t(ctx.lr)!=0x8273B4E0||(flags!=0x40&&!rigid))
        SimpsonsRejectUnportedGraphics(ctx,base);
    active->pointer(ctx.r3.u32,0x24,false);
    const auto typed=rigid?PPC_LOAD_U32(ctx.r3.u32+0x18):PPC_LOAD_U32(0x82D6D8A0);active->pointer(typed,rigid?0xB0:0xC8,false);
    const auto wrapper=PPC_LOAD_U32(typed+0x18);active->pointer(wrapper,0x30,false);
    const auto owner=active->engineDriver->effects().viewHeader(PPC_LOAD_U32(wrapper+0x10));
    const uint32_t vtable=PPC_LOAD_U32(typed);
    const bool skin=rigid&&vtable==0x82061714u;
    const bool sky=rigid&&!skin&&vtable==0x820616C0u&&owner.source==0x82036448u;
    const bool chocolate=rigid&&!skin&&vtable==0x820616C0u&&owner.source==0x8205D2D8u;
    if(rigid&&!skin&&!sky&&!chocolate&&!isRigidSource(owner.source))
        SimpsonsRejectUnportedGraphics(ctx,base);
    if(rigid&&skin&&!isSkinSource(owner.source))
        SimpsonsRejectUnportedGraphics(ctx,base);
    // Skin typed layout carries its opaque technique at +0x48/+0x4C (not +0xA8
    // like rigid); the pass is qualified in beginSkin against effect metadata.
    // Sky shares rigid's vtable/layout but selects technique 0x0007FFFC; its
    // pass is qualified in beginSky against effect metadata. Chocolate (second
    // alpha family) follows the same pattern; its pass is qualified likewise.
    if(sky&&(PPC_LOAD_U32(typed+0xA8)!=0x0003FFFCu||PPC_LOAD_U32(typed+0xAC)!=0x0007FFFCu)) {
        std::fprintf(stderr,"[NATIVE SKY DISPATCH] typed=%08X vtable=%08X wrapper=%08X a8=%08X ac=%08X manager=%08X\n",
            typed,vtable,PPC_LOAD_U32(typed+0x18),PPC_LOAD_U32(typed+0xA8),PPC_LOAD_U32(typed+0xAC),PPC_LOAD_U32(0x82D08BFC));
        throw Failure("Original sky scene lost its reflected owner");
    }
    if(chocolate&&(PPC_LOAD_U32(typed+0xA8)!=0x0003FFFCu||PPC_LOAD_U32(typed+0xAC)!=0x0007FFFCu)) {
        std::fprintf(stderr,"[NATIVE CHOCOLATE DISPATCH] typed=%08X vtable=%08X wrapper=%08X a8=%08X ac=%08X manager=%08X\n",
            typed,vtable,PPC_LOAD_U32(typed+0x18),PPC_LOAD_U32(typed+0xA8),PPC_LOAD_U32(typed+0xAC),PPC_LOAD_U32(0x82D08BFC));
        throw Failure("Original chocolate scene lost its reflected owner");
    }
    if((!rigid&&owner.source!=0x821490E0u)||owner.phase!=EngineEffects::Phase::Reflected||
       owner.wrapper!=wrapper||owner.identity!=PPC_LOAD_U32(typed+0x1C)||vtable!=(rigid?(skin?0x82061714u:0x820616C0u):0x8215022Cu)||
       (!skin&&!sky&&!chocolate&&PPC_LOAD_U32(typed+(rigid?0xA8:0xAC))!=0x0003FFFC)||owner.manager!=PPC_LOAD_U32(0x82D08BFC))
        throw Failure(rigid?(skin?"Original skin scene lost its reflected owner":(sky?"Original sky scene lost its reflected owner":(chocolate?"Original chocolate scene lost its reflected owner":"Original rigid scene lost its reflected owner"))):"Original depth prepass lost its reflected owner");
    active->engineDriver->requireContext(PPC_LOAD_U32(0x82D6D890));
    // The real branches, setters, submesh selection and loop remain AOT.
    // Their individual native boundaries must qualify before submitting work.
}
void SimpsonsNativeSceneContext(PPCContext& ctx,uint8_t* base) {
    using namespace Simpsons;
    if(!active||base!=active->base||currentContext!=&ctx||!active->engineDriver||ctx.lastFunction!=0x82A3C3AC)
        throw Failure("Unqualified original scene context publication");
    const auto flags=PPC_LOAD_U32(0x82D6CCA8);
    const auto caller=(ctx.r1.u32>=0x200&&!(ctx.r1.u32&15))?PPC_LOAD_U32(ctx.r1.u32+0xE8):0u;
    const bool standardPath=(flags==0x40||flags==0||flags==2)&&caller==0x8273B4E0;
    const bool vfxRigidTexturedPath=flags==0x20&&caller==0x8273B864;
    if((!standardPath&&!vfxRigidTexturedPath)||ctx.r3.u32!=PPC_LOAD_U32(0x82D0CAF8)||ctx.r3.u32||
       uint32_t(ctx.lr)!=0x82740688||ctx.r1.u32<0x200||(ctx.r1.u32&15)||
       PPC_LOAD_U32(ctx.r1.u32)!=ctx.r1.u32+0xF0)
        throw Failure("Unqualified original scene context publication");
    const auto identity=PPC_LOAD_U32(0x82D6D890);active->engineDriver->requireContext(identity);
    // Retain the actual CPU setter826FF6D8 and its store to82D63028. Its
    // argument names the native device owner instead of a console SDK object.
    ctx.r3.u64=identity;
}
void SimpsonsNativeScenePacketContext(PPCContext& ctx,uint8_t* base) {
    using namespace Simpsons;
    const auto flags=PPC_LOAD_U32(0x82D6CCA8);
    const bool stackValid=ctx.r1.u32>=0x200&&!(ctx.r1.u32&15);
    const auto backchain=stackValid?PPC_LOAD_U32(ctx.r1.u32):0u;
    const auto caller=stackValid?PPC_LOAD_U32(ctx.r1.u32+0xE8):0u;
    const bool standardPath=!flags&&caller==0x8273B4E0;
    const bool vfxRigidTexturedPath=flags==0x20&&caller==0x8273B864;
    if(!active||base!=active->base||currentContext!=&ctx||!active->engineDriver||
       (!standardPath&&!vfxRigidTexturedPath)||
       ctx.lastFunction!=0x8269D240||uint32_t(ctx.lr)!=0x82740938||
       !stackValid||backchain!=ctx.r1.u32+0xF0) {
        std::fprintf(stderr,
            "[NATIVE SCENE PACKET REJECT] flags=%08X last=%08X lr=%08X r1=%08X backchain=%08X caller=%08X "
            "r31=%08X packetBucket=%08X r25=%08X r11=%08X reflection=%08X device=%08X published=%08X\\n",
            flags,ctx.lastFunction,uint32_t(ctx.lr),ctx.r1.u32,backchain,caller,
            ctx.r31.u32,ctx.r31.u32?PPC_LOAD_U32(ctx.r31.u32+0x10):0u,ctx.r25.u32,ctx.r11.u32,
            PPC_LOAD_U32(0x82D6301C),PPC_LOAD_U32(0x82D0CAF8),PPC_LOAD_U32(0x82D63028));
        throw Failure("Unqualified original scene packet context");
    }
    active->pointer(ctx.r31.u32,0x24,false);
    if(PPC_LOAD_U32(ctx.r31.u32+0x10)!=ctx.r25.u32)throw Failure("Original scene packet bucket changed");
    uint32_t identity{};
    if(!ctx.r25.u32) {
        // Original82740948 selects the reflection owner's retained context for
        // bucket0. Qualify the exact live native owner before publishing it.
        const auto reflection=PPC_LOAD_U32(0x82D6301C);
        if(!reflection)throw Failure("Original scene packet reflection owner is absent");
        active->pointer(reflection,0x18,false);
        if(ctx.r11.u32!=PPC_LOAD_U32(reflection+0x14))
            throw Failure("Original scene packet reflection context changed");
        const auto view=active->engineDriver->reflectionTextures().view(reflection);
        if(view.phase!=EngineReflectionTextures::Phase::Ready||!view.contextRetained||view.context!=ctx.r11.u32)
            throw Failure("Original scene packet reflection owner is not ready");
        identity=ctx.r11.u32;
    } else if(ctx.r25.u32==1) {
        // Original8274095C selects the console-device global for bucket1. A
        // native run keeps that global null and substitutes the published
        // native context identity from the original context setter.
        if(ctx.r11.u32!=PPC_LOAD_U32(0x82D0CAF8)||ctx.r11.u32)
            throw Failure("Original scene packet main-device context changed");
        identity=PPC_LOAD_U32(0x82D63028);
    } else {
        throw Failure("Unsupported original scene packet bucket");
    }
    active->engineDriver->requireContext(identity);
    // Retain original82740960's packet publication using the qualified native
    // identity selected by the same bucket split as the original AOT.
    ctx.r11.u64=identity;
}
void SimpsonsNativeSceneRecordingBuild(PPCContext& ctx,uint8_t* base) {
    using namespace Simpsons;
    if(!active||base!=active->base||currentContext!=&ctx||!active->engineDriver||ctx.lastFunction!=0x82740420||
       uint32_t(ctx.lr)!=0x82740A60||PPC_LOAD_U32(0x82D6CCA8)||ctx.r4.u32||ctx.r5.u32||ctx.r6.u32>1||ctx.r7.u32!=1)
        throw Failure("Unqualified original rigid recording-build entry");
    const auto packet=ctx.r3.u32;active->pointer(packet,0x24,false);
    const auto typed=PPC_LOAD_U32(packet+0x18),meta=PPC_LOAD_U32(packet);
    active->pointer(typed,0xB0,false);active->pointer(meta,0x2C,false);
    const auto owner=active->engineDriver->effects().viewHeader(PPC_LOAD_U32(typed+0x1C));
    const bool mono=owner.source==0x8211F480;
    if(mono) {
        active->pointer(typed,0xC0,false);
        if(owner.phase!=EngineEffects::Phase::Reflected||PPC_LOAD_U32(typed)!=0x8215020C||
           PPC_LOAD_U32(typed+0xA8)!=0x0007FFFC||PPC_LOAD_U32(typed+0xAC)!=0x0003FFFC||
           PPC_LOAD_U32(typed+0x18)!=owner.wrapper||PPC_LOAD_U32(typed+0x10)!=owner.manager||
           owner.manager!=PPC_LOAD_U32(0x82D08BFC)||PPC_LOAD_U32(meta+0x24)||
           PPC_LOAD_U32(meta+8)!=(ctx.r6.u32?4u:0u)||PPC_LOAD_U32(packet+0x10)!=1||PPC_LOAD_U8(packet+0xC)!=1)
            throw Failure("Original mono recording-build packet differs");
        active->engineDriver->requireContext(PPC_LOAD_U32(packet+0x14));return;
    }
    const bool sky=owner.source==0x82036448;
    if(sky)std::fprintf(stderr,"[NATIVE SKY RECORDING] packet=%08X typed=%08X meta=%08X meta24=%08X id=%08X\n",
        packet,typed,meta,PPC_LOAD_U32(meta+0x24),owner.identity);
    const bool chocolate=owner.source==0x8205D2D8;
    if(chocolate)std::fprintf(stderr,"[NATIVE CHOCOLATE RECORDING] packet=%08X typed=%08X meta=%08X meta24=%08X id=%08X\n",
        packet,typed,meta,PPC_LOAD_U32(meta+0x24),owner.identity);
    if(ctx.r6.u32||(!sky&&!chocolate&&!isRigidSource(owner.source))||owner.phase!=EngineEffects::Phase::Reflected||PPC_LOAD_U32(typed)!=0x820616C0||
       PPC_LOAD_U32(typed+0xA8)!=0x0003FFFC||PPC_LOAD_U32(typed+0x18)!=owner.wrapper||
       PPC_LOAD_U32(meta+0x24)||PPC_LOAD_U32(packet+0x10)!=1||PPC_LOAD_U8(packet+0xC)!=1)
        throw Failure("Original rigid recording-build packet differs");
    active->engineDriver->requireContext(PPC_LOAD_U32(packet+0x14));
    // Continue original material activation, record lookup and recording begin.
    // Recording begin/finish/replay retain their own explicit support guards.
}
void SimpsonsRejectUnportedGraphics(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base) throw Simpsons::Failure("Invalid graphics guard runtime");
    if(ctx.lastFunction==0x82455570&&uint32_t(ctx.lr)==0x827724E0) {
        SimpsonsNativeViewportResolveAndClearColor(ctx,base);
        return;
    }
    if(ctx.lastFunction==0x82455570&&uint32_t(ctx.lr)==0x82773E38) {
        auto& runtime=*Simpsons::active;
        std::fprintf(stderr,"[NATIVE POST COPY FRONTIER] sp=%08X selector=%08X rectangle=%08X destination=%08X point=%08X level=%08X slice=%08X clear=%08X f1=%.9g r23=%08X r24=%08X r25=%08X\n",
            ctx.r1.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,ctx.r7.u32,ctx.r8.u32,ctx.r9.u32,ctx.r10.u32,ctx.f1.f64,ctx.r23.u32,ctx.r24.u32,ctx.r25.u32);
        if(!runtime.frameCaptureDirectory.empty()) {
            const auto capture=[&](const char* name,uint32_t address,uint32_t bytes) {
                const auto* data=runtime.pointer(address,bytes,false);
                std::ofstream out(runtime.frameCaptureDirectory/name,std::ios::binary);
                out.write(reinterpret_cast<const char*>(data),bytes);
                if(!out)throw Simpsons::Failure("Post-copy diagnostic capture failed");
            };
            capture("post-frame.bin",0x82DFEA20,0x1A0);
            capture("post-viewport-rows.bin",0x82DFEF00,300);
            capture("post-texture-headers.bin",0x82DFE360,0x618);
            capture("post-shader-registry.bin",0x82CF2590,0xAC);
            capture("post-color.bin",ctx.r23.u32,16);
            capture("post-stack.bin",ctx.r1.u32,0x120);
            const auto binding=runtime.engineDriver->cameraBinding();
            bool alphaOne=false;const auto target=runtime.engineDriver->color(binding.colorIdentity,alphaOne);
            const auto pixels=runtime.engineDriver->readbackColor(binding.colorIdentity);
            std::ofstream raw(runtime.frameCaptureDirectory/"post-input.rgb10a2",std::ios::binary);
            raw.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()));
            std::ofstream meta(runtime.frameCaptureDirectory/"post-input.json");
            meta<<"{\"width\":"<<target->pixelWidth()<<",\"height\":"<<target->pixelHeight()
                <<",\"format\":\"R10G10B10A2_UNORM_LE\",\"camera\":"<<binding.camera
                <<",\"color_identity\":"<<binding.colorIdentity<<",\"depth_identity\":"<<binding.depthIdentity
                <<",\"capture_source\":\"private_post_input_readback\",\"front_copy_completed\":false}\n";
            if(!raw||!meta)throw Simpsons::Failure("Post-copy input readback capture failed");
        }
    }
    if(ctx.lastFunction==0x82740420) {
        Simpsons::active->pointer(ctx.r3.u32,0x24,false);
        std::fprintf(stderr,"[NATIVE RECORD BUILD ENTRY] packet=%08X alpha=%u selector1=%u selector2=%u bucket=%u context=%08X shadow_draws=%llu zprepass_draws=%llu\n",
            ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,ctx.r7.u32,PPC_LOAD_U32(ctx.r3.u32+0x14),
            static_cast<unsigned long long>(Simpsons::active->engineDriver->effects().shadowMeshDrawCount()),
            static_cast<unsigned long long>(Simpsons::active->engineDriver->effects().zprepassDrawCount()));
    }
    if(ctx.lastFunction==0x82740680) {
        auto& runtime=*Simpsons::active;
        try {
            const uint32_t packet=ctx.r3.u32,flags=PPC_LOAD_U32(0x82D6CCA8);
            runtime.pointer(packet,0x24,false);
            std::fprintf(stderr,"[NATIVE SCENE DISPATCH] packet=%08X flags=%08X recording=%08X context=%08X device=%08X words=",
                packet,flags,PPC_LOAD_U32(0x82CF0BE8),PPC_LOAD_U32(0x82D63028),PPC_LOAD_U32(0x82D0CAF8));
            for(uint32_t i=0;i<0x24;i+=4)std::fprintf(stderr," %08X",PPC_LOAD_U32(packet+i));
            std::fputc('\n',stderr);
            std::fprintf(stderr,"[NATIVE SCENE COMPLETED DEPTH] shadow_draws=%llu zprepass_draws=%llu\n",
                static_cast<unsigned long long>(runtime.engineDriver->effects().shadowMeshDrawCount()),
                static_cast<unsigned long long>(runtime.engineDriver->effects().zprepassDrawCount()));
            if(!flags) {
                const auto object=PPC_LOAD_U32(packet+4),metadata=PPC_LOAD_U32(packet);
                runtime.pointer(object,0x40,false);runtime.pointer(metadata,0x2C,false);
                const auto propertyOffset=PPC_LOAD_U32(object+0x3C),property=propertyOffset?PPC_LOAD_U32(0x82D6C0C0)+propertyOffset:0;
                uint32_t propertyFlags=0;if(property){runtime.pointer(property,1,false);propertyFlags=PPC_LOAD_U8(property);}
                uint32_t bucket=0;
                if(!PPC_LOAD_U8(0x82D6D860)) {
                    // Original8269D240 is a read-only camera-list search. The
                    // cloned call keeps its volatile registers out of the game.
                    Simpsons::EngineCpuCalls cpu(ctx,base);bucket=cpu.invoke(0x8269D240,PPC_LOAD_U32(packet+8))+1;
                }
                if(bucket>=4)throw Simpsons::Failure("Scene diagnostic cache bucket exceeds four original heads");
                const auto cacheOffset=PPC_LOAD_U32(0x82D6D850),root=object+cacheOffset;
                runtime.pointer(root,16,false);uint32_t node=PPC_LOAD_U32(root+4*bucket),selected=0;
                std::unordered_set<uint32_t> visited;
                while(node) {
                    if(visited.size()>=256||!visited.insert(node).second)throw Simpsons::Failure("Scene diagnostic cache chain is too large or cyclic");
                    runtime.pointer(node,0x34,false);
                    std::fprintf(stderr,"[NATIVE SCENE CACHE NODE] address=%08X metadata=%08X next=%08X flags=%08X type=%08X payload=%08X\n",
                        node,PPC_LOAD_U32(node+0x1C),PPC_LOAD_U32(node+0xC),PPC_LOAD_U32(node+0x18),PPC_LOAD_U32(node+0x2C),PPC_LOAD_U32(node+0x28));
                    if(PPC_LOAD_U32(node+0x1C)==metadata){selected=node;break;}node=PPC_LOAD_U32(node+0xC);
                }
                std::fprintf(stderr,"[NATIVE SCENE DECISION] property=%08X property_flags=%02X camera_override=%02X bucket=%u cache_offset=%X selected=%08X payload=%08X eligible=%u\n",
                    property,propertyFlags,PPC_LOAD_U8(0x82D6D860),bucket,cacheOffset,selected,selected?PPC_LOAD_U32(selected+0x28):0,
                    !selected||(!(PPC_LOAD_U32(selected+0x18)&1)&&PPC_LOAD_U32(selected+0x2C)!=3));
            }
            const uint32_t typed=flags&0x40?PPC_LOAD_U32(0x82D6D8A0):PPC_LOAD_U32(packet+(flags&2?0x1C:0x18));
            if(typed) {
                runtime.pointer(typed,0xB0,false);
                const auto wrapper=PPC_LOAD_U32(typed+0x18);runtime.pointer(wrapper,0x30,false);
                const auto effect=runtime.engineDriver->effects().view(PPC_LOAD_U32(wrapper+0x10));
                std::fprintf(stderr,"[NATIVE SCENE EFFECT] typed=%08X vtable=%08X wrapper=%08X identity=%08X source=%08X technique_AC=%08X cache=%08X pool=%08X\n",
                    typed,PPC_LOAD_U32(typed),wrapper,effect.identity,effect.source,PPC_LOAD_U32(typed+0xAC),effect.cache,effect.pool);
            }
            if(!runtime.frameCaptureDirectory.empty()) {
                const auto capture=[&](const char* name,uint32_t address,uint32_t bytes){
                    if(bytes>16*1024*1024)throw Simpsons::Failure("Scene diagnostic extent is too large");
                    const auto* data=runtime.pointer(address,bytes,false);
                    std::ofstream out(runtime.frameCaptureDirectory/name,std::ios::binary);
                    out.write(reinterpret_cast<const char*>(data),bytes);
                    if(!out)throw Simpsons::Failure("Scene diagnostic capture failed");
                };
                const auto metadata=PPC_LOAD_U32(packet),object=PPC_LOAD_U32(packet+4);
                capture("scene-packet.bin",packet,0x24);capture("scene-metadata.bin",metadata,0x2C);
                capture("scene-object.bin",object,0x40);
                const auto geometry=PPC_LOAD_U32(metadata+0xC);capture("scene-geometry.bin",geometry,0x78);
                capture("scene-elements.bin",PPC_LOAD_U32(geometry+0xC),PPC_LOAD_U32(geometry+8)*12);
                capture("scene-vertices.bin",PPC_LOAD_U32(geometry+0x10),PPC_LOAD_U32(geometry));
                capture("scene-indices.bin",PPC_LOAD_U32(geometry+0x1C),PPC_LOAD_U32(geometry+0x14));
                if(typed)capture("scene-typed.bin",typed,0xB0);
                if(flags==2&&typed&&PPC_LOAD_U32(typed)==0x8215020C) {
                    capture("mono-typed.bin",typed,0xC0);
                    const auto count=PPC_LOAD_U32(typed+0x34),parameters=PPC_LOAD_U32(typed+0x24);
                    if(count&&count<=64)capture("mono-classifications.bin",PPC_LOAD_U32(typed+0x30),28*count);
                    if(parameters&&parameters<=256)capture("mono-parameters.bin",PPC_LOAD_U32(typed+0x28),24*parameters);
                    capture("mono-shared-matrix-before.bin",runtime.engineDriver->effects().sharedParameterStorage(PPC_LOAD_U32(typed+0x1C),0x00040001),64);
                    const auto& effective=runtime.engineDriver->effectiveState();
                    std::fprintf(stderr,"[NATIVE MONO STATE] classifications=%u parameters=%u packed_blend=%08X scalars=",
                        count,parameters,effective.effectiveBlend(0));
                    for(const auto& field:Simpsons::Graphics::scalarStateEvidence())std::fprintf(stderr," %03X:%08X",field.id,effective.scalar(field.id));
                    const auto camera=runtime.engineDriver->cameraBinding();
                    std::fprintf(stderr," camera=%08X color=%08X depth=%08X viewport=",camera.camera,camera.colorIdentity,camera.depthIdentity);
                    for(auto v:camera.viewport)std::fprintf(stderr," %08X",v);
                    std::fputc('\n',stderr);
                }
            }
        } catch(const std::exception& e) {std::fprintf(stderr,"[NATIVE SCENE DIAGNOSTIC] %s\n",e.what());}
    }
    if(ctx.lastFunction==0x826B4B88) {
        const uint32_t manager=PPC_LOAD_U32(0x82D08BFC),secondary=PPC_LOAD_U32(0x82D5DA78);
        fprintf(stderr,"[NATIVE GRAPHICS] original CPU constructors returned manager=%08X secondary=%08X child=%08X context=%08X first_effect=%08X; FX creation remains unported\n",
                manager,secondary,secondary?PPC_LOAD_U32(secondary+0x20):0,
                manager?PPC_LOAD_U32(manager+0x14):0,PPC_LOAD_U32(0x82CEFD28));
        const uint32_t wrapper=ctx.r3.u32;
        fprintf(stderr,"[NATIVE EFFECT] wrapper=%08X blob=%08X parent=%08X shared_pool=%08X name_storage=%08X name_length=%u effect=%08X cache=%08X bytes=%u; rejected before SDK creation/reflection\n",
                wrapper,ctx.r4.u32,wrapper?PPC_LOAD_U32(wrapper+0xC):0,manager?PPC_LOAD_U32(manager+0x18):0,
                wrapper?PPC_LOAD_U32(wrapper+4):0,wrapper?PPC_LOAD_U16(wrapper+8):0,
                wrapper?PPC_LOAD_U32(wrapper+0x10):0,wrapper?PPC_LOAD_U32(wrapper+0x1C):0,
                wrapper?PPC_LOAD_U32(wrapper+0x20):0);
    }
    if(ctx.lastFunction==0x82756480)
        fprintf(stderr,"[NATIVE SCREEN] pending original quad camera=%08X selector=%u texture=%08X color=%08X rect=(%.9g,%.9g)-(%.9g,%.9g)\n",
                ctx.r3.u32,ctx.r9.u32,ctx.r10.u32,ctx.r8.u32,ctx.f1.f64,ctx.f2.f64,ctx.f3.f64,ctx.f4.f64);
    if(ctx.lastFunction==0x82723D80)
        fprintf(stderr,"[NATIVE STATE] unported original application state owner=%08X selector=%08X value=%08X force=%08X\n",
            ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32);
    if(ctx.lastFunction==0x824025A8) {
        fprintf(stderr,"[NATIVE STATE] unported RenderWare selector=%u value=%08X\n",ctx.r3.u32,ctx.r4.u32);
        if(ctx.r3.u32==1 && ctx.r4.u32) {
            try {
                const uint32_t raster=ctx.r4.u32,offset=PPC_LOAD_U32(0x82E3DC94);
                Simpsons::active->pointer(raster,0x34,false);
                std::fprintf(stderr,"[NATIVE RW RASTER] raster=%08X extension_offset=%X header_words=",raster,offset);
                for(uint32_t i=0;i<13;++i)std::fprintf(stderr,"%s%08X",i?",":"",PPC_LOAD_U32(raster+4*i));
                std::fputc('\n',stderr);
                // Original826C01CC/0200 load the texture from caller r15+424,
                // then its raster. Inspect only that proved caller relation.
                if(uint32_t(ctx.lr)==0x826C0268 && uint64_t(ctx.r15.u32)+0x428<=0x100000000ull) {
                    Simpsons::active->pointer(ctx.r15.u32+0x424,4,false);
                    const uint32_t texture=PPC_LOAD_U32(ctx.r15.u32+0x424);
                    Simpsons::active->pointer(texture,0x58,false);
                    if(PPC_LOAD_U32(texture)==raster) {
                        char name[33]{};const auto* source=Simpsons::active->pointer(texture+0x10,32,false);
                        for(uint32_t i=0;i<32 && source[i];++i)name[i]=source[i]>=32 && source[i]<127?char(source[i]):'?';
                        std::fprintf(stderr,"[NATIVE RW RASTER] caller_owner=%08X texture=%08X name=%s dictionary=%08X sampler=%08X refs=%u\n",
                            ctx.r15.u32,texture,name,PPC_LOAD_U32(texture+4),PPC_LOAD_U32(texture+0x50),PPC_LOAD_U32(texture+0x54));
                    }
                }
                if(offset>=0x34 && offset<=0x1000 && uint64_t(raster)+offset+0x34<=0x100000000ull) {
                    Simpsons::active->pointer(raster+offset,0x34,false);
                    std::fprintf(stderr,"[NATIVE RW RASTER] extension_words=");
                    for(uint32_t i=0;i<13;++i)std::fprintf(stderr,"%s%08X",i?",":"",PPC_LOAD_U32(raster+offset+4*i));
                    std::fputc('\n',stderr);
                    const uint32_t resource=PPC_LOAD_U32(raster+offset);
                    if(resource) {
                        Simpsons::active->pointer(resource,0x34,false);
                        std::fprintf(stderr,"[NATIVE RW RASTER] serialized_resource=%08X words=",resource);
                        for(uint32_t i=0;i<13;++i)std::fprintf(stderr,"%s%08X",i?",":"",PPC_LOAD_U32(resource+4*i));
                        std::fputc('\n',stderr);
                    }
                }
                const auto texture=Simpsons::active->engineDriver->textureRaster(raster);
                std::fprintf(stderr,"[NATIVE RW RASTER] owned native texture=%ux%u format=%u levels=%u\n",
                    texture->width,texture->height,uint32_t(texture->format),texture->levelCount());
            } catch(const std::exception& e) {std::fprintf(stderr,"[NATIVE RW RASTER] inspection=%s\n",e.what());}
            Simpsons::dumpGuestStack(ctx);
        }
    }
    if(ctx.lastFunction==0x82409308) {
        const uint32_t ui=PPC_LOAD_U32(0x82D090F0);
        fprintf(stderr,"[NATIVE IM2D] original primitive=%u vertices=%08X count=%u camera=%08X ui=%08X raster=%08X; 28-byte original XYZRHW/color/UV vertices, draw remains guarded\n",
            ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,PPC_LOAD_U32(0x82E3DD60),ui,PPC_LOAD_U32(0x82D0E3F8));
        if(ctx.r5.u32 && ctx.r5.u32<=4)Simpsons::active->pointer(ctx.r4.u32,ctx.r5.u32*28,false);
        if(ctx.r5.u32 && ctx.r5.u32<=4)for(uint32_t i=0;i<ctx.r5.u32;++i) {
            const uint32_t p=ctx.r4.u32+i*28;Simpsons::active->pointer(p,28,false);
            fprintf(stderr,"[NATIVE IM2D] vertex%u=(%08X,%08X,%08X,%08X) color=%08X uv=(%08X,%08X)\n",i,
                PPC_LOAD_U32(p),PPC_LOAD_U32(p+4),PPC_LOAD_U32(p+8),PPC_LOAD_U32(p+12),PPC_LOAD_U32(p+16),PPC_LOAD_U32(p+20),PPC_LOAD_U32(p+24));
        }
    }
    if(ctx.lastFunction==0x826F4988)
        fprintf(stderr,"[NATIVE RECORDING] constructor rejected before publication: owner=%08X singleton=%08X pool_head=%08X application_context=%08X main_device=%08X caller=%08X\n",
                ctx.r3.u32,PPC_LOAD_U32(0x82D09784),PPC_LOAD_U32(0x82DFE10C),
                PPC_LOAD_U32(0x82D6D890),PPC_LOAD_U32(0x82D0CAF8),uint32_t(ctx.lr));
    if(ctx.lastFunction==0x82452540)
        fprintf(stderr,"[NATIVE RECORDING] SDK creation rejected before allocation: adapter=%08X type=%u r5=%08X r6=%08X r7=%08X output=%08X singleton=%08X caller=%08X\n",
                ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,ctx.r7.u32,ctx.r8.u32,
                PPC_LOAD_U32(0x82D09784),uint32_t(ctx.lr));
    if(ctx.lastFunction==0x82440578)
        fprintf(stderr,"[NATIVE TEXTURE] unported original resource request extent=%ux%u r5=%u levels=%u usage=%08X format=%08X r9=%08X dimension=%u; caller=%08X\n",
                ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,ctx.r7.u32,ctx.r8.u32,ctx.r9.u32,ctx.r10.u32,uint32_t(ctx.lr));
    if(ctx.lastFunction==0x826B3980 && uint32_t(ctx.lr)==0x823C9544) {
        try {
            auto& driver=*Simpsons::active->engineDriver;const auto id=driver.effects().activeEdge();
            const auto fx=driver.effects().view(id);const auto& values=fx.defaultVectorWords;
            std::fprintf(stderr,"[NATIVE EDGE COMMIT] guarded id=%08X wrapper=%08X typed=%08X source=%08X values=%zu; original setter completed\n",
                id,ctx.r3.u32,ctx.r31.u32,fx.source,values.size());
            if(values.size()>=164)for(uint32_t offset:{240u,304u,368u,384u,400u,416u,432u,448u,464u,528u,544u,560u,576u})
                std::fprintf(stderr,"[NATIVE EDGE PARAMETER] offset=%u words=%08X/%08X/%08X/%08X\n",offset,
                    values[offset/4],values[offset/4+1],values[offset/4+2],values[offset/4+3]);
            const auto& state=driver.effectiveState();
            for(const auto& field:Simpsons::Graphics::scalarStateEvidence())
                std::fprintf(stderr,"[NATIVE EDGE SCALAR] id=%03X value=%08X\n",field.id,state.scalar(field.id));
            for(const auto& field:Simpsons::Graphics::samplerStateEvidence())
                std::fprintf(stderr,"[NATIVE EDGE SAMPLER0] id=%03X value=%08X\n",field.id,state.sampler(0,field.id));
        }catch(const std::exception& e){std::fprintf(stderr,"[NATIVE EDGE COMMIT] diagnostic unavailable: %s\n",e.what());}
    }
    if(ctx.lastFunction==0x826B5FC0) {
        fprintf(stderr,"[NATIVE EFFECT BEGIN] rejected manager=%08X wrapper=%08X technique=%08X typed=%08X camera=%08X caller=%08X\n",
                ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r31.u32,ctx.r30.u32,uint32_t(ctx.lr));
        // Diagnostic reads never admit the operation. Registered native effect
        // lookup supplies ownership; mapped memory alone is not provenance.
        try {
            auto& rt=*Simpsons::active;rt.pointer(ctx.r4.u32,0x18,false);rt.pointer(ctx.r3.u32,0x14,false);
            const auto id=PPC_LOAD_U32(ctx.r4.u32+0x10);const auto v=rt.engineDriver->effects().view(id);
            fprintf(stderr,"[NATIVE EFFECT BEGIN] id=%08X source=%08X registered_wrapper=%08X registered_manager=%08X cache=%08X pool=%08X phase=%u manager_words=%08X/%08X/%08X/%08X/%08X wrapper_vtable=%08X\n",
                    id,v.source,v.wrapper,v.manager,v.cache,v.pool,unsigned(v.phase),PPC_LOAD_U32(ctx.r3.u32),PPC_LOAD_U32(ctx.r3.u32+4),
                    PPC_LOAD_U32(ctx.r3.u32+8),PPC_LOAD_U32(ctx.r3.u32+12),PPC_LOAD_U32(ctx.r3.u32+16),PPC_LOAD_U32(ctx.r4.u32));
        } catch(const std::exception& e) {fprintf(stderr,"[NATIVE EFFECT BEGIN] diagnostic ownership unavailable: %s\n",e.what());}
    }
    if(ctx.lastFunction==0x823F7070 && uint32_t(ctx.lr)==0x824081C0) {
        const uint32_t raster=ctx.r4.u32;
        fprintf(stderr,"[NATIVE RASTER] original create raster=%08X flags=%08X extent=%ux%u depth_field=%u plugin_offset=%X\n",
            raster,ctx.r5.u32,PPC_LOAD_U32(raster+0xC),PPC_LOAD_U32(raster+0x10),PPC_LOAD_U32(raster+0x14),PPC_LOAD_U32(0x82E3DC94));
    }
    char message[240];
    std::snprintf(message,sizeof(message),"Unimplemented native engine graphics boundary 0x%08X, caller 0x%08X; original SDK access rejected",ctx.lastFunction,uint32_t(ctx.lr));
    throw Simpsons::Failure(message);
}

void SimpsonsNativeApplicationScalarPreflight(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver)
        throw Simpsons::Failure("Native application scalar has no driver owner");
    try {Simpsons::active->engineDriver->applicationScalar(base,ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,false);}
    catch(...) {
        fprintf(stderr,"[NATIVE STATE] rejected application scalar owner=%08X selector=%X value=%08X force=%08X caller=%08X\n",
                ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,uint32_t(ctx.lr));
        throw;
    }

}
void SimpsonsNativeApplicationScalarApply(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver ||
       ctx.r4.u32>=0x57 || ctx.r30.u32!=ctx.r3.u32 || ctx.r29.u32!=ctx.r5.u32 ||
       ctx.r11.u32!=ctx.r4.u32*4 || ctx.r28.u32!=(ctx.r31.u32+0x1A5)*4 ||
       ctx.r31.u32!=PPC_LOAD_U32(0x82D6D498+4*ctx.r4.u32))
        throw Simpsons::Failure("Native application scalar callsite ABI changed");
    const uint32_t setter=Simpsons::active->engineDriver->applicationScalar(base,ctx.r30.u32,ctx.r4.u32,ctx.r29.u32,true);
    // The original code below 82723DEC performs all cache and dirty-bit writes.
    ctx.r3.u64=PPC_LOAD_U32(0x82D6D890);ctx.r4.u64=ctx.r29.u64;
    ctx.r11.u64=setter;ctx.ctr.u64=setter;ctx.lr=0x82723DEC;
}

void SimpsonsNativeApplicationSamplerPreflight(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver)
        throw Simpsons::Failure("Native application sampler has no driver owner");
    try {Simpsons::active->engineDriver->applicationSampler(base,ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,false);}
    catch(...) {
        fprintf(stderr,"[NATIVE STATE] rejected application sampler owner=%08X stage=%u selector=%X value=%08X force=%08X caller=%08X\n",
                ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,ctx.r7.u32,uint32_t(ctx.lr));
        throw;
    }
}
void SimpsonsNativeApplicationSamplerApply(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver ||
       !ctx.r5.u32 || ctx.r5.u32>20 || ctx.r4.u32>=16 || ctx.r30.u32!=ctx.r3.u32 ||
       ctx.r31.u32!=ctx.r4.u32 || ctx.r29.u32!=ctx.r6.u32 || ctx.r11.u32!=ctx.r5.u32*4 ||
       ctx.r28.u32!=PPC_LOAD_U32(0x82D6D648+4*ctx.r5.u32) ||
       ctx.r27.u32!=20*ctx.r31.u32+ctx.r28.u32 || ctx.r26.u32!=4*(0x1F7+ctx.r27.u32))
        throw Simpsons::Failure("Native application sampler callsite ABI changed");
    const uint32_t setter=Simpsons::active->engineDriver->applicationSampler(base,ctx.r30.u32,ctx.r31.u32,ctx.r5.u32,ctx.r29.u32,true);
    ctx.r3.u64=PPC_LOAD_U32(0x82D6D890);ctx.r4.u64=ctx.r31.u64;ctx.r5.u64=ctx.r29.u64;
    ctx.r11.u64=setter;ctx.ctr.u64=setter;ctx.lr=0x82723D08;
}

static void nativePipelinePixelCenter(PPCContext& ctx,uint8_t* base,uint32_t resume) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver || ctx.r3.u32 || ctx.r4.u32!=1)
        throw Simpsons::Failure("Native pipeline pixel-center callsite ABI changed");
    Simpsons::active->engineDriver->directScalar(base,0x144,1);ctx.lr=resume;
}

void SimpsonsNativeRenderWareStatePreflight(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver)
        throw Simpsons::Failure("Native RenderWare state has no driver owner");
    // Selector 14 -> table byte 0 -> 824025F8 -> 82401480. The complete
    // helper is CPU-only: capability-gated fog flag plus pending scalar 196.
    // Preserve its original nonzero normalization, cache suppression and queue.
    Simpsons::active->engineDriver->effectiveState(); // Checks runtime/thread/engine ownership.
    if(ctx.r3.u32==1) {
        if(PPC_LOAD_U8(0x82062DE8)!=0x53) throw Simpsons::Failure("Original RenderWare raster selector table changed");
        const uint32_t sp=ctx.r1.u32;
        if(sp<0xF0 || (sp&15)) throw Simpsons::Failure("Invalid original RenderWare raster stack");
        Simpsons::active->pointer(sp-0xF0,0xF0,true);
        Simpsons::active->engineDriver->preflightNullRaster(base,ctx.r4.u32,0);
        return; // Original dispatcher -> 82401940; CPU alpha/cache changes remain AOT.
    }
    const uint32_t selector=ctx.r3.u32,value=ctx.r4.u32;
    if(selector==2 || selector==9) {
        Simpsons::active->engineDriver->renderWareSampler(ctx,base,selector,value,false);
        return;
    }
    if(selector==6 || selector==7 || selector==10 || selector==11 || selector==12 || selector==20 || selector==29 || selector==30) {
        const uint8_t tableByte=selector==6?0x5A:selector==7?0x63:selector==10?0x5D:selector==11?0x60:
            selector==12?0x7B:selector==20?0x96:selector==29?0xC7:0xEA;
        if(PPC_LOAD_U8(0x82062DE8+selector-1)!=tableByte)
            throw Simpsons::Failure("Original RenderWare scalar selector table changed");
        const uint32_t frame=selector==12?0xD0:0x70,sp=ctx.r1.u32;
        if(sp<frame || (sp&15))throw Simpsons::Failure("Invalid original RenderWare scalar stack");
        Simpsons::active->pointer(sp-frame,frame,true);
        const uint32_t count=PPC_LOAD_U32(0x82D10114);
        if(count>425)throw Simpsons::Failure("Original RenderWare scalar dirty queue is invalid");
        auto queued=[&](std::initializer_list<uint32_t> ids) {
            uint32_t additions=0;
            for(uint32_t id:ids) {
                const uint32_t pending=0x82D0F3B0+8*id,dirty=PPC_LOAD_U32(pending+4);
                uint32_t entries=0;
                for(uint32_t i=0;i<count;++i)entries+=PPC_LOAD_U32(0x82D0ED08+4*i)==id;
                if(dirty>1 || entries!=dirty)throw Simpsons::Failure("Original RenderWare scalar dirty membership is inconsistent");
                Simpsons::active->pointer(pending,8,true);additions+=!dirty;
            }
            if(count+additions>425)throw Simpsons::Failure("Original RenderWare scalar dirty queue is full");
            if(additions) {
                Simpsons::active->pointer(0x82D0ED08+4*count,4*additions,true);
                Simpsons::active->pointer(0x82D10114,4,true);
            }
        };
        if(selector==7 || selector==20 || selector==29) {
            constexpr std::array<uint32_t,3> shade={0,1,2};
            constexpr std::array<uint32_t,4> cull={0,0,2,6};
            constexpr std::array<uint32_t,9> compare={0,0,1,2,3,4,5,6,7};
            auto table=[&](uint32_t address,const auto& values) {
                if(value>=values.size())throw Simpsons::Failure("Original RenderWare overlay index is out of bounds");
                for(uint32_t i=0;i<values.size();++i)
                    if(PPC_LOAD_U32(address+4*i)!=values[i])
                        throw Simpsons::Failure("Original RenderWare overlay conversion table changed");
            };
            if(selector==7)table(0x82062CA0,shade);
            else if(selector==20)table(0x82062D70,cull);
            else table(0x82062DA4,compare);
            const uint32_t cache=selector==7?0x82D0E3F4:selector==20?0x82D0E3E0:0x82D0E4C0;
            if(PPC_LOAD_U32(cache)==value)return;
            Simpsons::active->pointer(cache,4,true);
            if(selector==7)queued({0x195});
            else if(selector==20)queued({0x38});
            else {
                Simpsons::active->pointer(0x82D0E4C4,4,true);
                const uint32_t blend=PPC_LOAD_U32(0x82D0F590);
                if(blend>1)throw Simpsons::Failure("Original RenderWare overlay blend Boolean is invalid");
                if(blend && PPC_LOAD_U32(0x82D0F6B0)!=uint32_t(value!=8))queued({0x60,0x68});
                else queued({0x68});
            }
        } else if(selector==10 || selector==11) {
            constexpr std::array<uint32_t,12> blend={0,0,1,4,5,6,7,10,11,8,9,16};
            if(value>=blend.size())throw Simpsons::Failure("Original RenderWare blend index is out of bounds");
            for(uint32_t i=0;i<blend.size();++i)
                if(PPC_LOAD_U32(0x82062CBC+4*i)!=blend[i])throw Simpsons::Failure("Original RenderWare blend conversion table changed");
            const uint32_t cache=selector==10?0x82D0E4B8:0x82D0E4BC;
            if(PPC_LOAD_U32(cache)==value)return;
            Simpsons::active->pointer(cache,4,true);queued({selector==10?0x48u:0x4Cu});
        } else if(selector==6) {
            const uint32_t cached=PPC_LOAD_U32(0x82D0E3B4),other=PPC_LOAD_U32(0x82D0E3B0);
            if(cached>1 || other>1)throw Simpsons::Failure("Original RenderWare depth-test Boolean is invalid");
            if(cached==uint32_t(value!=0))return;
            Simpsons::active->pointer(0x82D0E3B4,4,true);
            if(other)queued({0x2C});else queued({0x28,0x2C});
        } else if(selector==12) {
            const uint32_t cached=PPC_LOAD_U32(0x82D0E3D8),texture=PPC_LOAD_U32(0x82D0E3DC),alpha=PPC_LOAD_U32(0x82D0E4C4);
            if(cached>1 || texture>1 || alpha>1)throw Simpsons::Failure("Original RenderWare alpha Boolean is invalid");
            if(cached==uint32_t(value!=0))return;
            Simpsons::active->pointer(0x82D0E3D8,4,true);
            if(!texture) {
                // The inline blend update always queues on a CPU flag change;
                // the called alpha helper suppresses an equal pending value.
                if(PPC_LOAD_U32(0x82D0F6B0)==(value?alpha:0))queued({0x3C});
                else queued({0x3C,0x60});
            }
        } else {
            if(value>255)throw Simpsons::Failure("Unqualified RenderWare alpha reference above255");
            if(PPC_LOAD_U32(0x82D0F6D0)!=value)queued({0x64});
        }
        // All selected dispatcher/helper paths are CPU-only. Leave their
        // original normalization, cache suppression, queue order and return
        // frame to AOT; native-effective state changes on the later commit.
        return;
    }
    if(ctx.r3.u32==8) {
        // Table byte 57 -> 82402754 -> CPU-only depth-write helper 82401E00.
        if(PPC_LOAD_U8(0x82062DE8+7)!=0x57) throw Simpsons::Failure("Original RenderWare depth-write selector table changed");
        const uint32_t cached=PPC_LOAD_U32(0x82D0E3B0),other=PPC_LOAD_U32(0x82D0E3B4),count=PPC_LOAD_U32(0x82D10114);
        if(cached>1 || other>1 || count>425) throw Simpsons::Failure("Original RenderWare depth cache or queue is invalid");
        if(cached==uint32_t(ctx.r4.u32!=0)) return;
        uint32_t additions=0;
        for(uint32_t id:{0x28u,0x30u}) {
            if(id==0x28 && other) continue;
            const uint32_t pending=0x82D0F3B0+8*id,dirty=PPC_LOAD_U32(pending+4);
            uint32_t queued=0;
            for(uint32_t i=0;i<count;++i) queued+=PPC_LOAD_U32(0x82D0ED08+4*i)==id;
            if(dirty>1 || queued!=dirty) throw Simpsons::Failure("Original RenderWare depth dirty membership is inconsistent");
            Simpsons::active->pointer(pending,8,true);
            additions+=!dirty;
        }
        if(count+additions>425) throw Simpsons::Failure("Original RenderWare depth dirty queue is full");
        Simpsons::active->pointer(0x82D0E3B0,4,true);
        if(additions) {
            Simpsons::active->pointer(0x82D0ED08+4*count,4*additions,true);
            Simpsons::active->pointer(0x82D10114,4,true);
        }
        return; // The original helper queues depth enable/write; it does not apply either.
    }
    if(ctx.r3.u32!=14) SimpsonsRejectUnportedGraphics(ctx,base);
    if(PPC_LOAD_U8(0x82062DE8+13)!=0) throw Simpsons::Failure("Original RenderWare fog selector table changed");
    const uint32_t cached=PPC_LOAD_U32(0x82D0E3E4),dirty=PPC_LOAD_U32(0x82D10064),count=PPC_LOAD_U32(0x82D10114);
    if(cached>1 || dirty>1 || count>425) throw Simpsons::Failure("Original RenderWare fog cache or dirty queue is invalid");
    uint32_t queued=0;
    for(uint32_t i=0;i<count;++i) queued+=PPC_LOAD_U32(0x82D0ED08+4*i)==0x196;
    if(queued!=dirty) throw Simpsons::Failure("Original RenderWare fog dirty membership is inconsistent");
    const bool change=ctx.r4.u32?(!cached && (PPC_LOAD_U32(0x82E3DFC4)&0x180)):bool(cached);
    if(change) {
        Simpsons::active->pointer(0x82D0E3E4,4,true);
        Simpsons::active->pointer(0x82D10060,8,true);
        if(!dirty) {
            if(count==425) throw Simpsons::Failure("Original RenderWare fog dirty queue is full");
            Simpsons::active->pointer(0x82D0ED08+4*count,4,true);
            Simpsons::active->pointer(0x82D10114,4,true);
        }
    }
    // Fall through to original dispatcher/helper; no effective native state is
    // applied until the original pipeline requests a commit.
}
void SimpsonsNativePipelinePixelCenterEnter(PPCContext& ctx,uint8_t* base) {nativePipelinePixelCenter(ctx,base,0x826B09D8);}
static void nativeRwSamplerCall(PPCContext& ctx,uint8_t* base,uint32_t selector,uint32_t resume) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver || ctx.r3.u32!=ctx.r31.u32)
        throw Simpsons::Failure("Original RenderWare sampler callsite ABI changed");
    Simpsons::active->engineDriver->renderWareSampler(ctx,base,selector,ctx.r3.u32,true);
    ctx.lr=resume;
}
void SimpsonsNativeRwAddressCall(PPCContext& ctx,uint8_t* base) {nativeRwSamplerCall(ctx,base,2,0x8240271C);}
void SimpsonsNativeRwFilterCall(PPCContext& ctx,uint8_t* base) {nativeRwSamplerCall(ctx,base,9,0x82402740);}
void SimpsonsNativePipelinePixelCenterExit(PPCContext& ctx,uint8_t* base) {nativePipelinePixelCenter(ctx,base,0x826B0A10);}
void SimpsonsNativePipelineResetPreflight(PPCContext&,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver)
        throw Simpsons::Failure("Native pipeline reset has no driver owner");
    for(uint32_t i=0;i<67;++i) Simpsons::active->engineDriver->pipelineResetField(base,i,false);
}
void SimpsonsNativeBindingReset(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver)
        throw Simpsons::Failure("Native binding reset has no driver owner");
    Simpsons::active->engineDriver->resetBindings(ctx,base);
}
void SimpsonsNativePresent(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver ||
       ctx.lr!=0x8240806C || PPC_LOAD_U32(PPC_LOAD_U32(0x82D0CA68)+0x98)!=0x823EE820)
        throw Simpsons::Failure("Native presentation callback owner or original wrapper ABI is invalid");
    Simpsons::active->engineDriver->present(ctx,base,ctx.r3.u32);
}
void SimpsonsNativeRecordingCreateBegin(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver)
        throw Simpsons::Failure("Native recording constructor has no driver owner");
    Simpsons::active->engineDriver->recordingOwners().createBegin(ctx,base);
}
void SimpsonsNativeRecordingCreateCommit(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver)
        throw Simpsons::Failure("Native recording creation has no driver owner");
    Simpsons::active->engineDriver->recordingOwners().createCommit(ctx,base);
}
void SimpsonsNativeRecordingDestroyBegin(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver)
        throw Simpsons::Failure("Native recording destructor has no driver owner");
    Simpsons::active->engineDriver->recordingOwners().destroyBegin(ctx,base);
}
void SimpsonsNativeRecordingDestroyCommit(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver)
        throw Simpsons::Failure("Native recording retirement has no driver owner");
    Simpsons::active->engineDriver->recordingOwners().destroyCommit(ctx,base);
}
void SimpsonsNativeRecordingPluginDestroyPreflight(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !ctx.r3.u32 ||
       ctx.r4.u32!=PPC_LOAD_U32(0x82D6D850))
        throw Simpsons::Failure("Native recording plugin destruction has invalid original ownership/offset");
    const uint64_t fields=uint64_t(ctx.r3.u32)+ctx.r4.u32;
    if(fields>UINT32_MAX-15) throw Simpsons::Failure("Native recording plugin heads overflow the original address space");
    Simpsons::active->pointer(uint32_t(fields),16,false);
    // Qualify nonempty ownership before the original destructor loop runs.
    for(uint32_t i=0;i<4;++i)
        if(PPC_LOAD_U32(uint32_t(fields)+4*i)) {
            std::fprintf(stderr,"[NATIVE RECORD PLUGIN DESTROY] object=%08X offset=%08X flags=%08X SP=%08X LR=%08X heads=%08X,%08X,%08X,%08X\n",
                ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r1.u32,uint32_t(ctx.lr),PPC_LOAD_U32(uint32_t(fields)),
                PPC_LOAD_U32(uint32_t(fields)+4),PPC_LOAD_U32(uint32_t(fields)+8),PPC_LOAD_U32(uint32_t(fields)+12));
            for(uint32_t slot=0;slot<4;++slot)if(const auto node=PPC_LOAD_U32(uint32_t(fields)+4*slot)) {
                Simpsons::active->pointer(node,0x34,false);
                std::fprintf(stderr,"[NATIVE RECORD PLUGIN HEAD] slot=%u node=%08X words=",slot,node);
                for(uint32_t word=0;word<13;++word)std::fprintf(stderr,"%s%08X",word?",":"",PPC_LOAD_U32(node+4*word));
                std::fprintf(stderr,"\n");
            }
            if(!Simpsons::active->engineDriver)throw Simpsons::Failure("Cached record deletion has no live native driver");
            Simpsons::active->engineDriver->recordingOwners().pluginDestroyBegin(ctx,base);
            return;
        }
    // Original empty-list callback and return ABI remain AOT.
}
void SimpsonsNativeRecordingPluginDestroyComplete(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base)throw Simpsons::Failure("Invalid recording plugin completion runtime");
    if(Simpsons::active->engineDriver&&Simpsons::active->engineDriver->submissionConfigured())
        Simpsons::active->engineDriver->recordingOwners().pluginDestroyComplete(ctx,base);
}
void SimpsonsNativeResetNullTexturePreflight(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver)
        throw Simpsons::Failure("Native null texture helper has no driver owner");
    Simpsons::active->engineDriver->preflightResetNullTexture(base,ctx.r3.u32,ctx.r4.u32);
}
void SimpsonsNativeNullRasterPreflight(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver)
        throw Simpsons::Failure("Native null raster has no driver owner");
    Simpsons::active->engineDriver->preflightNullRaster(base,ctx.r3.u32,ctx.r4.u32);
    const uint32_t sp=ctx.r1.u32;
    if(sp<0x80 || (sp&15)) throw Simpsons::Failure("Invalid original null raster stack");
    Simpsons::active->pointer(sp-0x80,0x80,true);
}
void SimpsonsNativeResetTextureSet(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver || ctx.r3.u32)
    {
        char message[224];
        std::snprintf(message,sizeof(message),"Unsupported native texture binding at 0x824408E0, caller 0x%08X: device=%08X stage=%u texture=%08X mask=%016llX",
            uint32_t(ctx.lr),ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,static_cast<unsigned long long>(ctx.r6.u64));
        throw Simpsons::Failure(message);
    }
    if(ctx.r5.u32) Simpsons::active->engineDriver->setTextureRaster(base,ctx.r4.u32,ctx.r5.u32,ctx.r6.u64,uint32_t(ctx.lr));
    else Simpsons::active->engineDriver->resetNullTexture(base,ctx.r4.u32,ctx.r6.u64,uint32_t(ctx.lr));
}
void SimpsonsNativePipelineResetApply(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver ||
       ctx.r31.u32>=67 || ctx.r30.u32!=0x82CD1B70 || ctx.r3.u32 ||
       ctx.r10.u32!=PPC_LOAD_U32(0x82CD1B70+8*ctx.r31.u32) || ctx.r4.u32!=PPC_LOAD_U32(0x82CD1B74+8*ctx.r31.u32))
        throw Simpsons::Failure("Native pipeline reset callsite ABI changed");
    const uint32_t setter=Simpsons::active->engineDriver->pipelineResetField(base,ctx.r31.u32,true);
    ctx.r11.u64=setter;ctx.ctr.u64=setter;ctx.lr=0x823F4658;
}
void SimpsonsNativeCameraPassPreflight(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver ||
       ctx.r3.u32 || ctx.r5.u32 || (ctx.lastFunction!=0x823F00C0 && ctx.lastFunction!=0x823EE7F0))
        throw Simpsons::Failure("Native camera pass callback ABI or driver owner is invalid");
    Simpsons::active->engineDriver->preflightCameraPass(base,ctx.r4.u32,ctx.lastFunction==0x823F00C0);
}
void SimpsonsNativeCameraSelect(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver)
        throw Simpsons::Failure("Native camera selection has no driver owner");
    try {Simpsons::active->engineDriver->selectCamera(base,ctx.r3.u32);}
    catch(...) {
        fprintf(stderr,"[NATIVE CAMERA] rejected selection camera=%08X loading=%08X caller=%08X function=%08X\n",
            ctx.r3.u32,PPC_LOAD_U32(0x82E07248),uint32_t(ctx.lr),ctx.lastFunction);
        Simpsons::dumpGuestStack(ctx);throw;
    }
}
void SimpsonsNativeCameraClear(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver)
        throw Simpsons::Failure("Native camera clear has no driver owner");
    try {Simpsons::active->engineDriver->clearCamera(base,ctx.r3.u32,ctx.r4.u32,ctx.r5.u32);}
    catch(...) {
        fprintf(stderr,"[NATIVE CAMERA] rejected clear camera=%08X color=%08X selector=%X caller=%08X\n",
                ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,uint32_t(ctx.lr));throw;
    }
    ctx.r3.u64=1;
}

void SimpsonsNativeContextQuery(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver)
        throw Simpsons::Failure("Native context query has no active driver");
    ctx.r3.u64=Simpsons::active->engineDriver->refreshContext(ctx,base);
}

void SimpsonsNativeSubmissionConfigure(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver ||
       ctx.r3.u32!=ctx.r30.u32 || ctx.r4.u32!=ctx.r1.u32+0x70 || ctx.r28.u32)
        throw Simpsons::Failure("Native submission callsite has an invalid original context or descriptor ABI");
    Simpsons::active->engineDriver->configureSubmission(base,ctx.r3.u32,ctx.r4.u32);
    ctx.r3.u64=0;ctx.lr=0x82875E24;
}

void SimpsonsNativeRasterCreate(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver || ctx.r3.u32)
        throw Simpsons::Failure("Invalid original native raster-create callback ABI");
    Simpsons::active->engineDriver->createRaster(ctx,base,ctx.r4.u32,ctx.r5.u32);ctx.r3.u64=1;
}
namespace {
struct SnapshotHostState {
    const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
    SnapshotHostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~SnapshotHostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
};
Simpsons::EngineDriver& snapshotDriver(uint8_t* base) {
    if(!Simpsons::active||Simpsons::active->base!=base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Snapshot has no native driver owner");
    return *Simpsons::active->engineDriver;
}
}
void SimpsonsNativeSnapshotBegin(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).beginSnapshotRaster(c,b);}
bool SimpsonsNativeDirectSpriteBegin(PPCContext& c,uint8_t* b){SnapshotHostState fp;return snapshotDriver(b).beginDirectSprite(c,b);}
void SimpsonsNativeCoronaDeclarationCreate(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).coronaDeclaration(c,b,false);}
void SimpsonsNativeCoronaDeclarationRelease(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).coronaDeclaration(c,b,true);}
void SimpsonsNativeCoronaQueriesBegin(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).beginCoronaQueries(c,b);}
void SimpsonsNativeCoronaQueriesFinish(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).finishCoronaQueries(c,b);}
void SimpsonsNativeImmediateBufferPublish(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).immediateBufferLifecycle(c,b,false);}
void SimpsonsNativeImmediateBufferRetire(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).immediateBufferLifecycle(c,b,true);}
void SimpsonsNativeImmediateGeometryEnd(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).immediateGeometryState(c,b,false);}
void SimpsonsNativeImmediateDrawFrontier(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).reserveImmediate(c,b);}
void SimpsonsNativeTrailBegin(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).beginTrail(c,b);}
void SimpsonsNativeTrailFinish(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).finishTrailBatch(c,b);}
void SimpsonsNativeRadialDeclarationCreate(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).radialOperation(c,b,0);}
void SimpsonsNativeRadialDeclarationRelease(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).radialOperation(c,b,1);}
void SimpsonsNativeRadialBegin(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).radialOperation(c,b,2);}
void SimpsonsNativeRadialConstants(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).radialOperation(c,b,3);}
void SimpsonsNativeRadialLock(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).radialOperation(c,b,4);}
void SimpsonsNativeRadialFinish(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).radialOperation(c,b,5);}
void SimpsonsNativeImmediateDeclarationCreate(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).immediateDeclaration(c,b,false);}
void SimpsonsNativeImmediateDeclarationRelease(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).immediateDeclaration(c,b,true);c.lr=0x827522E0;}
void SimpsonsNativeImmediateDeclarationReleaseLocal(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).immediateDeclaration(c,b,true);c.lr=0x827518B4;}
void SimpsonsNativeDirectSpriteBatchBegin(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).directSpriteBatch(c,b,true,true,0x8276B750);}
void SimpsonsNativeDirectSpriteBatchEnd(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).directSpriteBatch(c,b,false,true,0x8276B898);}
void SimpsonsNativeDirectSpriteEmitterEnd(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).directSpriteBatch(c,b,false,false,0x8276B6D8);c.lr=0x8276B724;}
void SimpsonsNativeDirectSpriteExpandedEnd(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).directScalar(b,uint32_t(Simpsons::Graphics::ScalarState::ExpandedBlend0),0);c.lr=0x8276B25C;}
void SimpsonsNativeDirectSpriteLock(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lockDirectSprite(c,b);}
void SimpsonsNativeDirectSpriteFinish(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).finishDirectSprite(c,b);}
void SimpsonsNativeSnapshotFinish(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).finishSnapshotRaster(c,b);}
void SimpsonsNativeCrossfadeCommit(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).commitCrossfade(c,b);}
void SimpsonsNativeMovieFrameCommit(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).commitMovieFrame(c,b);}
void SimpsonsNativeMovieLockPreflight(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).preflightMovieLock(c,b);}
void SimpsonsNativeMovieLock(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lockMovieRaster(c,b);}
void SimpsonsNativeMovieUnlockPreflight(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).preflightMovieUnlock(c,b);}
void SimpsonsNativeMovieUnlock(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).unlockMovieRaster(c,b);}
void SimpsonsNativeViewportCopy(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).copyCameraTargets(c,b,c.r3.u32,c.r4.u32,c.r5.u32);}
void SimpsonsNativeScreenPreflight(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).preflightScreen(c,b);}
void SimpsonsNativeIm2DUploadPreflight(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).preflightIm2DUpload(c,b);}
void SimpsonsNativeIm2DTrianglePreflight(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).preflightIm2DUpload(c,b,true);}
void SimpsonsNativeIm2DLock(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lockIm2DUpload(c,b);}
void SimpsonsNativeIm2DUnlock(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).unlockIm2DUpload(c,b);}
void SimpsonsNativeIm2DSetup(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).preflightIm2DSetup(c,b);}
void SimpsonsNativeIm2DDeclaration(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).bindIm2DDeclaration(c,b);}
void SimpsonsNativeIm2DStream(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).bindIm2DStream(c,b);}
void SimpsonsNativeIm2DDraw(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).drawIm2D(c,b);}
void SimpsonsNativeDistortion827724E0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827724E0);}
void SimpsonsNativeDistortion8277252C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x8277252C);}
void SimpsonsNativeDistortion827719C0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827719C0);}
void SimpsonsNativeDistortion827719CC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827719CC);}
void SimpsonsNativeDistortion827719D8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827719D8);}
void SimpsonsNativeDistortion827719E8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827719E8);}
void SimpsonsNativeDistortion827719F8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827719F8);}
void SimpsonsNativeDistortion82771B88(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771B88);}
void SimpsonsNativeDistortion82772024(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772024);}
void SimpsonsNativeDistortion82772030(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772030);}
void SimpsonsNativeDistortion8277203C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x8277203C);}
void SimpsonsNativeDistortion82772048(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772048);}
void SimpsonsNativeDistortion82772054(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772054);}
void SimpsonsNativeDistortion82772068(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772068);}
void SimpsonsNativeDistortion827720AC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827720AC);}
void SimpsonsNativeDistortion827720D8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827720D8);}
void SimpsonsNativeDistortion827720FC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827720FC);}
void SimpsonsNativeDistortion82772114(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772114);}
void SimpsonsNativeDistortion82772130(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772130);}
void SimpsonsNativeDistortion82772140(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772140);}
void SimpsonsNativeDistortion82772150(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772150);}
void SimpsonsNativeDistortion82772154(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772154);}
void SimpsonsNativeDistortion827721A0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827721A0);}
void SimpsonsNativeDistortion827721D4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827721D4);}
void SimpsonsNativeDistortion82772200(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772200);}
void SimpsonsNativeDistortion82772248(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772248);}
void SimpsonsNativeDistortion82772278(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772278);}
void SimpsonsNativeDistortion82772284(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772284);}
void SimpsonsNativeDistortion82750C54(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82750C54);}
void SimpsonsNativeDistortion827722B0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827722B0);}
void SimpsonsNativeDistortion827722DC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827722DC);}
void SimpsonsNativeDistortion82772308(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772308);}
void SimpsonsNativeDistortion82772340(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772340);}
void SimpsonsNativeDistortion82772370(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772370);}
void SimpsonsNativeDistortion82772380(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772380);}
void SimpsonsNativeDistortion82772394(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772394);}
void SimpsonsNativeDistortion827723A8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827723A8);}
void SimpsonsNativeDistortion827723B8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827723B8);}
void SimpsonsNativeDistortion827723C8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827723C8);}
void SimpsonsNativeDistortion827723DC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827723DC);}
void SimpsonsNativeDistortion82772414(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772414);}
void SimpsonsNativeDistortion82772444(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772444);}
void SimpsonsNativeDistortion82772450(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772450);}
void SimpsonsNativeDistortion8277157C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x8277157C);}
void SimpsonsNativeDistortion82771588(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771588);}
void SimpsonsNativeDistortion82771594(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771594);}
void SimpsonsNativeDistortion827715A0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827715A0);}
void SimpsonsNativeDistortion827715B4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827715B4);}
void SimpsonsNativeDistortion827715CC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827715CC);}
void SimpsonsNativeDistortion827715E4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827715E4);}
void SimpsonsNativeDistortion82771604(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771604);}
void SimpsonsNativeDistortion82771614(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771614);}
void SimpsonsNativeDistortion82771624(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771624);}
void SimpsonsNativeDistortion82771638(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771638);}
void SimpsonsNativeDistortion8277166C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x8277166C);}
void SimpsonsNativeDistortion82771698(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771698);}
void SimpsonsNativeDistortion827716E0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827716E0);}
void SimpsonsNativeDistortion82771718(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771718);}
void SimpsonsNativeDistortion82771724(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771724);}
void SimpsonsNativeDistortion82771730(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771730);}
void SimpsonsNativeDistortion8277173C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x8277173C);}
void SimpsonsNativeDistortion82771750(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771750);}
void SimpsonsNativeDistortion82771768(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771768);}
void SimpsonsNativeDistortion82771780(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771780);}
void SimpsonsNativeDistortion827717A0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827717A0);}
void SimpsonsNativeDistortion827717B0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827717B0);}
void SimpsonsNativeDistortion827717C0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827717C0);}
void SimpsonsNativeDistortion827717C4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827717C4);}
void SimpsonsNativeDistortion82771810(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771810);}
void SimpsonsNativeDistortion82771820(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771820);}
void SimpsonsNativeDistortion82771830(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771830);}
void SimpsonsNativeDistortion82771834(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771834);}
void SimpsonsNativeDistortion8277187C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x8277187C);}
void SimpsonsNativeDistortion82771890(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82771890);}
void SimpsonsNativeDistortion827718E0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x827718E0);}
void SimpsonsNativeDistortion82772510(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772510);}
void SimpsonsNativeDistortion8277251C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x8277251C);}
void SimpsonsNativeDistortion82772528(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).distortionOperation(c,b,0x82772528);}
void SimpsonsNativeBallEffect82771998(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).ballEffectOperation(c,b,0x82771998);}
void SimpsonsNativeBallEffect827719B4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).ballEffectOperation(c,b,0x827719B4);}
void SimpsonsNativeBallEffect82771AE0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).ballEffectOperation(c,b,0x82771AE0);}
void SimpsonsNativeBallEffect82771CBC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).ballEffectOperation(c,b,0x82771CBC);}
void SimpsonsNativeBallEffect82771D18(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).ballEffectOperation(c,b,0x82771D18);}
void SimpsonsNativeBallEffect82771D98(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).ballEffectOperation(c,b,0x82771D98);}
void SimpsonsNativeScreenEffect82754384(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754384);}
void SimpsonsNativeScreenEffect827543BC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827543BC);}
void SimpsonsNativeScreenEffect827543C8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827543C8);}
void SimpsonsNativeScreenEffect827543D4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827543D4);}
void SimpsonsNativeScreenEffect827543E0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827543E0);}
void SimpsonsNativeScreenEffect827543F4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827543F4);}
void SimpsonsNativeScreenEffect8275440C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8275440C);}
void SimpsonsNativeScreenEffect82754424(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754424);}
void SimpsonsNativeScreenEffect8275443C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8275443C);}
void SimpsonsNativeScreenEffect8275444C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8275444C);}
void SimpsonsNativeScreenEffect8275445C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8275445C);}
void SimpsonsNativeScreenEffect82754460(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754460);}
void SimpsonsNativeScreenEffect82754484(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754484);}
void SimpsonsNativeScreenEffect827544A8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827544A8);}
void SimpsonsNativeScreenEffect827544B8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827544B8);}
void SimpsonsNativeScreenEffect827544C8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827544C8);}
void SimpsonsNativeScreenEffect827544CC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827544CC);}
void SimpsonsNativeScreenEffect827544E8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827544E8);}
void SimpsonsNativeScreenEffect82754514(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754514);}
void SimpsonsNativeScreenEffect82754518(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754518);}
void SimpsonsNativeScreenEffect8275458C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8275458C);}
void SimpsonsNativeScreenEffect827545DC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827545DC);}
void SimpsonsNativeScreenEffect82754614(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754614);}
void SimpsonsNativeScreenEffect82754620(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754620);}
void SimpsonsNativeScreenEffect8275462C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8275462C);}
void SimpsonsNativeScreenEffect82754D80(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754D80);}
void SimpsonsNativeScreenEffect82754D8C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754D8C);}
void SimpsonsNativeScreenEffect82754D98(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754D98);}
void SimpsonsNativeScreenEffect82754DA4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754DA4);}
void SimpsonsNativeScreenEffect82754DB0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754DB0);}
void SimpsonsNativeScreenEffect82754DC4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754DC4);}
void SimpsonsNativeScreenEffect82754DDC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754DDC);}
void SimpsonsNativeScreenEffect82754DF4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754DF4);}
void SimpsonsNativeScreenEffect82754E0C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754E0C);}
void SimpsonsNativeScreenEffect82754E1C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754E1C);}
void SimpsonsNativeScreenEffect82754E2C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754E2C);}
void SimpsonsNativeScreenEffect82754E30(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754E30);}
void SimpsonsNativeScreenEffect82754E4C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754E4C);}
void SimpsonsNativeScreenEffect82754E78(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754E78);}
void SimpsonsNativeScreenEffect82754E7C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754E7C);}
void SimpsonsNativeScreenEffect82754EC4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754EC4);}
void SimpsonsNativeScreenEffect82754EF4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754EF4);}
void SimpsonsNativeScreenEffect82754F00(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754F00);}
void SimpsonsNativeScreenEffect82754F0C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754F0C);}
void SimpsonsNativeScreenEffect82754F18(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754F18);}
void SimpsonsNativeScreenEffect82754F54(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82754F54);}
void SimpsonsNativeScreenEffect827555B4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827555B4);}
void SimpsonsNativeScreenEffect827555F4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827555F4);}
void SimpsonsNativeScreenEffect82755600(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82755600);}
void SimpsonsNativeScreenEffect8275560C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8275560C);}
void SimpsonsNativeScreenEffect82755618(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82755618);}
void SimpsonsNativeScreenEffect8275562C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8275562C);}
void SimpsonsNativeScreenEffect82755644(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82755644);}
void SimpsonsNativeScreenEffect8275565C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8275565C);}
void SimpsonsNativeScreenEffect82755674(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82755674);}
void SimpsonsNativeScreenEffect82755684(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82755684);}
void SimpsonsNativeScreenEffect82755694(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82755694);}
void SimpsonsNativeScreenEffect82755698(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82755698);}
void SimpsonsNativeScreenEffect827556BC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827556BC);}
void SimpsonsNativeScreenEffect827556E0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827556E0);}
void SimpsonsNativeScreenEffect827556F0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827556F0);}
void SimpsonsNativeScreenEffect82755700(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82755700);}
void SimpsonsNativeScreenEffect82755704(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82755704);}
void SimpsonsNativeScreenEffect82755730(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82755730);}
void SimpsonsNativeScreenEffect82755750(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82755750);}
void SimpsonsNativeScreenEffect827557AC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827557AC);}
void SimpsonsNativeScreenEffect827557E0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827557E0);}
void SimpsonsNativeScreenEffect82755840(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82755840);}
void SimpsonsNativeScreenEffect82755854(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82755854);}
void SimpsonsNativeScreenEffect8275587C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8275587C);}
void SimpsonsNativeScreenEffect82755888(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82755888);}
void SimpsonsNativeScreenEffect82755894(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82755894);}
void SimpsonsNativeScreenEffect8276CA80(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CA80);}
void SimpsonsNativeScreenEffect8276CA8C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CA8C);}
void SimpsonsNativeScreenEffect8276CA98(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CA98);}
void SimpsonsNativeScreenEffect8276CAA4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CAA4);}
void SimpsonsNativeScreenEffect8276CAB0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CAB0);}
void SimpsonsNativeScreenEffect8276CAC4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CAC4);}
void SimpsonsNativeScreenEffect8276CADC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CADC);}
void SimpsonsNativeScreenEffect8276CB20(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CB20);}
void SimpsonsNativeScreenEffect8276CB40(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CB40);}
void SimpsonsNativeScreenEffect8276CB50(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CB50);}
void SimpsonsNativeScreenEffect8276CB60(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CB60);}
void SimpsonsNativeScreenEffect8276CB64(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CB64);}
void SimpsonsNativeScreenEffect8276CB80(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CB80);}
void SimpsonsNativeScreenEffect8276CBAC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CBAC);}
void SimpsonsNativeScreenEffect8276CBB0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CBB0);}
void SimpsonsNativeScreenEffect8276CC50(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CC50);}
void SimpsonsNativeScreenEffect8276CC64(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CC64);}
void SimpsonsNativeScreenEffect8276CC94(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CC94);}
void SimpsonsNativeScreenEffect8276CCA0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CCA0);}
void SimpsonsNativeScreenEffect8276CCAC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CCAC);}
void SimpsonsNativeScreenEffect8276CCB8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276CCB8);}
void SimpsonsNativeScreenEffect8276FF8C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276FF8C);}
void SimpsonsNativeScreenEffect8276FF98(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276FF98);}
void SimpsonsNativeScreenEffect8276FFA4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276FFA4);}
void SimpsonsNativeScreenEffect8276FFB0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276FFB0);}
void SimpsonsNativeScreenEffect8276FFBC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276FFBC);}
void SimpsonsNativeScreenEffect8276FFC8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276FFC8);}
void SimpsonsNativeScreenEffect8276FFDC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276FFDC);}
void SimpsonsNativeScreenEffect8276FFF4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8276FFF4);}
void SimpsonsNativeScreenEffect8277000C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8277000C);}
void SimpsonsNativeScreenEffect82770024(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82770024);}
void SimpsonsNativeScreenEffect82770034(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82770034);}
void SimpsonsNativeScreenEffect82770044(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82770044);}
void SimpsonsNativeScreenEffect82770048(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82770048);}
void SimpsonsNativeScreenEffect82770064(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82770064);}
void SimpsonsNativeScreenEffect82770098(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82770098);}
void SimpsonsNativeScreenEffect827700AC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827700AC);}
void SimpsonsNativeScreenEffect827700C0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827700C0);}
void SimpsonsNativeScreenEffect827700E8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827700E8);}
void SimpsonsNativeScreenEffect827700F4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827700F4);}
void SimpsonsNativeScreenEffect82770100(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82770100);}
void SimpsonsNativeScreenEffect8277010C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8277010C);}
void SimpsonsNativeScreenEffect827562AC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827562AC);}
void SimpsonsNativeScreenEffect827562B8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827562B8);}
void SimpsonsNativeScreenEffect827562C4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827562C4);}
void SimpsonsNativeScreenEffect827562D0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827562D0);}
void SimpsonsNativeScreenEffect827562E4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827562E4);}
void SimpsonsNativeScreenEffect827562FC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827562FC);}
void SimpsonsNativeScreenEffect82756314(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82756314);}
void SimpsonsNativeScreenEffect82756344(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82756344);}
void SimpsonsNativeScreenEffect82756348(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82756348);}
void SimpsonsNativeScreenEffect827563C4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x827563C4);}
void SimpsonsNativeScreenEffect82756400(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82756400);}
void SimpsonsNativeScreenEffect82756414(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82756414);}
void SimpsonsNativeScreenEffect82756440(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82756440);}
void SimpsonsNativeScreenEffect8275644C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x8275644C);}
void SimpsonsNativeScreenEffect82756458(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82756458);}
void SimpsonsNativeScreenEffect82756044(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82756044);}
void SimpsonsNativeScreenEffect82756054(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82756054);}
void SimpsonsNativeScreenEffect82756064(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82756064);}
void SimpsonsNativeScreenEffect82756068(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82756068);}
void SimpsonsNativeScreenEffect82756088(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x82756088);}
void SimpsonsNativeLuma82770BF0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770BF0);}
void SimpsonsNativeLuma82770C28(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770C28);}
void SimpsonsNativeLuma82770C34(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770C34);}
void SimpsonsNativeLuma82770C40(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770C40);}
void SimpsonsNativeLuma82770C4C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770C4C);}
void SimpsonsNativeLuma82770C60(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770C60);}
void SimpsonsNativeLuma82770C78(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770C78);}
void SimpsonsNativeLuma82770C90(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770C90);}
void SimpsonsNativeLuma82770CA8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770CA8);}
void SimpsonsNativeLuma82770CB8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770CB8);}
void SimpsonsNativeLuma82770CC8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770CC8);}
void SimpsonsNativeLuma82770CCC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770CCC);}
void SimpsonsNativeLuma82770D14(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770D14);}
void SimpsonsNativeLuma82770D94(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770D94);}
void SimpsonsNativeLuma82770D9C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770D9C);}
void SimpsonsNativeLuma82770E40(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770E40);}
void SimpsonsNativeLuma82770E48(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770E48);}
void SimpsonsNativeLuma82770EA4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770EA4);}
void SimpsonsNativeLuma82770EAC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770EAC);}
void SimpsonsNativeLuma82770EF4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770EF4);}
void SimpsonsNativeLuma82770F24(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770F24);}
void SimpsonsNativeLuma82770F30(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770F30);}
void SimpsonsNativeLuma82770F3C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).lumaOperation(c,b,0x82770F3C);}
void SimpsonsNativeScreenDraw(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).drawScreen(c,b);}
void SimpsonsNativeGameplayOverlay(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).drawGameplayOverlay(c,b);}
void SimpsonsNativeMovieDraw(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).drawMovie(c,b);}
void SimpsonsNativeScreenFlatCreate(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenDeclaration(c,b,0,false);}
void SimpsonsNativeScreenTextureCreate(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenDeclaration(c,b,1,false);}
void SimpsonsNativeScreenFlatRelease(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenDeclaration(c,b,0,true);}
void SimpsonsNativeScreenTextureRelease(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenDeclaration(c,b,1,true);}
void SimpsonsNativeRasterDestroy(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver || ctx.r3.u32 || ctx.r5.u32)
        throw Simpsons::Failure("Invalid original native raster-destroy callback ABI");
    Simpsons::active->engineDriver->destroyRaster(ctx,base,ctx.r4.u32);ctx.r3.u64=1;
}

void SimpsonsNativeRasterDestroyPreflight(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver)
        throw Simpsons::Failure("Native raster wrapper destruction has no driver owner");
    // Validate before the original reverse plugin-destructor traversal. On
    // success all original wrapper instructions continue without register edits.
    Simpsons::active->engineDriver->preflightRasterDestroy(base,ctx.r3.u32);
}

void SimpsonsNativeTextureStreamRead(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base || !Simpsons::active->engineDriver)
        throw Simpsons::Failure("Native texture stream has no driver owner");
    ctx.r3.u64=Simpsons::active->engineDriver->readNativeTexture(ctx,base,ctx.r3.u32,ctx.r4.u32,ctx.r5.u32)?1:0;
}

void SimpsonsNativeRecordingBegin(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording begin runtime");
    Simpsons::active->engineDriver->recordingOwners().beginRecording(ctx,base);
}
void SimpsonsNativeRecordingFinish(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording finish runtime");
    Simpsons::active->engineDriver->recordingOwners().finishRecording(ctx,base);
}
void SimpsonsNativeRecordingReplay(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording replay runtime");
    Simpsons::active->engineDriver->recordingOwners().beginReplay(ctx,base);
}
void SimpsonsNativeRecordingExecute(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording execute runtime");
    Simpsons::active->engineDriver->recordingOwners().executeReplay(ctx,base);
}
void SimpsonsNativeRecordFinish_826F4F88(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording finish operation runtime");
    Simpsons::active->engineDriver->recordingOwners().finishOperation(ctx,base,0x826F4F88);
}
void SimpsonsNativeRecordFinish_826F4FC0(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording finish operation runtime");
    Simpsons::active->engineDriver->recordingOwners().finishOperation(ctx,base,0x826F4FC0);
}
void SimpsonsNativeRecordFinish_826F50A8(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording finish operation runtime");
    Simpsons::active->engineDriver->recordingOwners().finishOperation(ctx,base,0x826F50A8);
}
void SimpsonsNativeRecordFinish_826F50B0(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording finish operation runtime");
    Simpsons::active->engineDriver->recordingOwners().finishOperation(ctx,base,0x826F50B0);
}
void SimpsonsNativeRecordFinish_826F50C4(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording finish operation runtime");
    Simpsons::active->engineDriver->recordingOwners().finishOperation(ctx,base,0x826F50C4);
}

void SimpsonsNativeRecordBegin_826F4DE4(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording begin operation runtime");
    Simpsons::active->engineDriver->recordingOwners().beginOperation(ctx,base,0x826F4DE4);
}

void SimpsonsNativeRecordBegin_826F4DF0(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording begin operation runtime");
    Simpsons::active->engineDriver->recordingOwners().beginOperation(ctx,base,0x826F4DF0);
}

void SimpsonsNativeRecordBegin_826F4E04(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording begin operation runtime");
    Simpsons::active->engineDriver->recordingOwners().beginOperation(ctx,base,0x826F4E04);
}

void SimpsonsNativeRecordBegin_826F4E14(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording begin operation runtime");
    Simpsons::active->engineDriver->recordingOwners().beginOperation(ctx,base,0x826F4E14);
}

void SimpsonsNativeRecordBegin_826F4E20(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording begin operation runtime");
    Simpsons::active->engineDriver->recordingOwners().beginOperation(ctx,base,0x826F4E20);
}

void SimpsonsNativeRecordBegin_826F4E2C(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording begin operation runtime");
    Simpsons::active->engineDriver->recordingOwners().beginOperation(ctx,base,0x826F4E2C);
}

void SimpsonsNativeRecordBegin_826F4E94(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording begin operation runtime");
    Simpsons::active->engineDriver->recordingOwners().beginOperation(ctx,base,0x826F4E94);
}

void SimpsonsNativeRecordBegin_826F4ED4(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording begin operation runtime");
    Simpsons::active->engineDriver->recordingOwners().beginOperation(ctx,base,0x826F4ED4);
}

void SimpsonsNativeRecordBegin_826F4EEC(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording begin operation runtime");
    Simpsons::active->engineDriver->recordingOwners().beginOperation(ctx,base,0x826F4EEC);
}

void SimpsonsNativeRecordBegin_826F4EF8(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording begin operation runtime");
    Simpsons::active->engineDriver->recordingOwners().beginOperation(ctx,base,0x826F4EF8);
}

void SimpsonsNativeRecordBegin_826F4F04(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording begin operation runtime");
    Simpsons::active->engineDriver->recordingOwners().beginOperation(ctx,base,0x826F4F04);
}

void SimpsonsNativeRecordBegin_826F4F10(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording begin operation runtime");
    Simpsons::active->engineDriver->recordingOwners().beginOperation(ctx,base,0x826F4F10);
}

void SimpsonsNativeRecordBegin_826F4F1C(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording begin operation runtime");
    Simpsons::active->engineDriver->recordingOwners().beginOperation(ctx,base,0x826F4F1C);
}

void SimpsonsNativeRecordBegin_826F4F28(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording begin operation runtime");
    Simpsons::active->engineDriver->recordingOwners().beginOperation(ctx,base,0x826F4F28);
}

void SimpsonsNativeRecordBegin_826F4F48(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)throw Simpsons::Failure("Invalid recording begin operation runtime");
    Simpsons::active->engineDriver->recordingOwners().beginOperation(ctx,base,0x826F4F48);
}

void SimpsonsNativeBillboardBegin(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).billboardOperation(c,b,0x8275F228);}
void SimpsonsNativeBillboardMatrix(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).billboardOperation(c,b,0x82751934);}
void SimpsonsNativeBillboardColor(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).billboardOperation(c,b,0x8275F494);}
void SimpsonsNativeBillboardSoft(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).billboardOperation(c,b,0x8275F4E8);}
void SimpsonsNativeBillboardFinish(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).billboardOperation(c,b,0x8275F7EC);}
void SimpsonsNativeBeamEntry(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).beamOperation(c,b,0x8277B284);}
void SimpsonsNativeBeamBegin(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).beamOperation(c,b,0x8277B328);}
void SimpsonsNativeBeamFinish(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).beamOperation(c,b,0x8277B20C);}
void SimpsonsNativeEndpointBeamBegin(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).beamOperation(c,b,0x8286D754);}
void SimpsonsNativeEndpointBeamColor(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).beamOperation(c,b,0x8286D80C);}
void SimpsonsNativeEndpointBeamFinish(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).beamOperation(c,b,0x8286D9AC);}
void SimpsonsNativeImmediateBoxEntry(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).immediateEffectOperation(c,b,0x823CB2A0);}
void SimpsonsNativeImmediateBoxBegin(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).immediateEffectOperation(c,b,0x823CB2D8);}
void SimpsonsNativeImmediateBoxTexture(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).immediateEffectOperation(c,b,0x823CB348);}
void SimpsonsNativeImmediateBoxColor(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).immediateEffectOperation(c,b,0x823CB370);}
void SimpsonsNativeImmediateBoxFinish(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).immediateEffectOperation(c,b,0x823CB288);}
void SimpsonsNativeImmediateEightBegin(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).immediateEffectOperation(c,b,0x82776534);}
void SimpsonsNativeImmediateEightFinish(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).immediateEffectOperation(c,b,0x82776874);}
void SimpsonsNativeImmediateSevenBegin(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).immediateEffectOperation(c,b,0x82779A94);}
void SimpsonsNativeImmediateSevenColor(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).immediateEffectOperation(c,b,0x82779AD4);}
void SimpsonsNativeImmediateSevenFinish(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).immediateEffectOperation(c,b,0x827790D4);}
void SimpsonsNativeDecalEntry(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).decalOperation(c,b,0x827640A0);}
void SimpsonsNativeDecalTexture(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).decalOperation(c,b,0x827640EC);}
void SimpsonsNativeDecalColor(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).decalOperation(c,b,0x8276412C);}
void SimpsonsNativeDecalQuadEntry(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).decalOperation(c,b,0x82764608);}
void SimpsonsNativeDecalMeshEntry(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).decalOperation(c,b,0x82766D98);}
void SimpsonsNativeDecalQuadProjectedConstant(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).decalOperation(c,b,0x827646A0);}
void SimpsonsNativeDecalQuadConstant(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).decalOperation(c,b,0x827646E0);}
void SimpsonsNativeDecalMeshProjectedConstant(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).decalOperation(c,b,0x82766ECC);}
void SimpsonsNativeDecalMeshConstant(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).decalOperation(c,b,0x82766F08);}
void SimpsonsNativeDecalQuadFinish(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).decalOperation(c,b,0x82764810);}
void SimpsonsNativeDecalMeshFinish(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).decalOperation(c,b,0x82767210);}
void SimpsonsNativeDecalMeshFallbackFinish(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).decalOperation(c,b,0x82766E08);}
void SimpsonsNativeDecalMeshDeferredFinish(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).decalOperation(c,b,0x82766E3C);}
void SimpsonsNativeImmediateProjectedMatrix(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).billboardOperation(c,b,0x82751978);}

// Exact post-filter call-site adapters; no generic SDK acceptance.
void SimpsonsNativePost82773E34(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773E34);}
void SimpsonsNativePost82773F44(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773F44);}
void SimpsonsNativePost82774000(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82774000);}
void SimpsonsNativePost827740C4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x827740C4);}
void SimpsonsNativePost82771E58(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82771E58);}
void SimpsonsNativePost82771E74(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82771E74);}
void SimpsonsNativePost82771E80(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82771E80);}
void SimpsonsNativePost82771EA4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82771EA4);}
void SimpsonsNativePost82771F58(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82771F58);}
void SimpsonsNativePost82771F64(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82771F64);}
void SimpsonsNativePost82771F88(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82771F88);}
void SimpsonsNativePost82771F98(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82771F98);}
void SimpsonsNativePost82773E50(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773E50);}
void SimpsonsNativePost82773F6C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773F6C);}
void SimpsonsNativePost82774014(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82774014);}
void SimpsonsNativePost82773EE4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773EE4);}
void SimpsonsNativePost82773EFC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773EFC);}
void SimpsonsNativePost82773F80(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773F80);}
void SimpsonsNativePost82774034(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82774034);}
void SimpsonsNativePost82774144(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82774144);}
void SimpsonsNativePost82773F10(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773F10);}
void SimpsonsNativePost82773FA8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773FA8);}
void SimpsonsNativePost8277406C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x8277406C);}
void SimpsonsNativePost82774174(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82774174);}
void SimpsonsNativePost827741C8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x827741C8);}
void SimpsonsNativePost82773E74(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773E74);}
void SimpsonsNativePost82773CF4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773CF4);}
void SimpsonsNativePost82773D24(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773D24);}
void SimpsonsNativePost82774218(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82774218);}
void SimpsonsNativePost82773D94(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773D94);}
void SimpsonsNativePost82773DA0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773DA0);}
void SimpsonsNativePost82773DAC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773DAC);}
void SimpsonsNativePost82773DCC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773DCC);}
void SimpsonsNativePost82773DD8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773DD8);}
void SimpsonsNativePost82773DE4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773DE4);}
void SimpsonsNativePost82773DF0(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773DF0);}
void SimpsonsNativePost82773E60(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773E60);}
void SimpsonsNativePost82773E70(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773E70);}
void SimpsonsNativePost82773ECC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82773ECC);}
void SimpsonsNativePost82774130(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82774130);}
void SimpsonsNativePost827741FC(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x827741FC);}
void SimpsonsNativePost82774208(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82774208);}
void SimpsonsNativePost82774214(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).postFilterOperation(c,b,0x82774214);}

void SimpsonsNativePostPrepare82773B44(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).preparePostFilter(c,b,0x82773B44);}

void SimpsonsNativePostPrepare82773C80(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).preparePostFilter(c,b,0x82773C80);}

void SimpsonsNativePostPrepare82773C8C(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).preparePostFilter(c,b,0x82773C8C);}

void SimpsonsNativePostPrepare82773C98(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).preparePostFilter(c,b,0x82773C98);}

void SimpsonsNativePostPrepare82773CA8(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).preparePostFilter(c,b,0x82773CA8);}

void SimpsonsNativePostPrepare82773CB4(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).preparePostFilter(c,b,0x82773CB4);}
