#include "runtime.h"
#include "engine_driver.h"
#include "renderer/engine_state.h"
#include "renderer/native_backend.h"

namespace {
struct OverlayHostState {
    DWORD error=GetLastError();
    uint32_t fp=PPCFPSCRRegister::getcsr();
    OverlayHostState() {PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~OverlayHostState() {PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
};

void overlayExpanded(PPCContext& ctx,uint8_t* base,uint32_t request,uint32_t resume) {
    using namespace Simpsons;
    using namespace Simpsons::Graphics;
    OverlayHostState host;
    if(!active || !base || base!=active->base || currentContext!=&ctx || !active->engineDriver)
        throw Failure("Overlay expansion has no current native runtime/context/driver");
    // 827F58D8 saves the camera in r30. The two calls load the zero SDK-device
    // slot through r28; r28/r31 retain the original sign-extended LIS base.
    // Check the complete Boolean argument, not a truncated/canonicalized value.
    if(ctx.r3.u64!=0 || ctx.r4.u64!=request || ctx.r28.u32!=0x82D10000 ||
       ctx.r31.u32!=0x82D10000 || !ctx.r30.u32 || !ctx.r1.u32 || (ctx.r1.u32&15))
        throw Failure("Original overlay expansion callsite ABI changed");

    auto& driver=*active->engineDriver;
    // This first owner query checks the runtime, calling thread, live engine
    // and zero SDK-device global before any indirect original-memory access.
    const auto& effective=driver.effectiveState();
    driver.requireContext(PPC_LOAD_U32(0x82D5DA74));
    driver.requireContext(PPC_LOAD_U32(0x82D6D890));
    if(!driver.submissionConfigured() || effective.scalar(ScalarState::ExpandedBlend0)>1)
        throw Failure("Overlay expansion has no qualified native submission/state");
    active->pointer(ctx.r1.u32,0xD0,false); // Original parent's 208-byte frame.
    const auto binding=driver.cameraBinding();
    const auto engine=PPC_LOAD_U32(0x82D0CA68);
    if(binding.camera!=ctx.r30.u32 || PPC_LOAD_U32(engine)!=binding.camera ||
       PPC_LOAD_U32(0x82E3DD60)!=binding.camera || PPC_LOAD_U32(0x82D0CB1C)!=1 ||
       PPC_LOAD_U32(0x82D0CF5C)!=binding.colorIdentity ||
       PPC_LOAD_U32(0x82D0CF58)!=binding.depthIdentity)
        throw Failure("Overlay expansion requires the active original camera attachments");
    bool sampledAlphaOne{};
    const auto target=driver.color(binding.colorIdentity,sampledAlphaOne);
    const auto depth=driver.depth(binding.depthIdentity);
    if(!target || !depth || target->format!=TargetFormat::RGB10A2 || !target->width || !target->height)
        throw Failure("Overlay expansion requires live owned RGB10A2 storage");

    // Original 8243B3B0 has no calls or pixel writes. For formats 2/10 it
    // retains the same packed bits and changes only their blend interpretation.
    // Keep the request in the real effective-state owner; subsequent qualified
    // draws use explicit float equations and per-write RGB10A2 packing. This
    // follows OriginalScreen's provisional policy, not proven console precision.
    // directScalar also validates the original setter table before committing.
    // Do not touch pending/applied CPU caches or create a console device object.
    driver.directScalar(base,0x134,request);
    driver.setUiDrawing(request!=0);
    ctx.lr=resume; // The skipped BL still supplies its original continuation.
}
}

// These global C++ symbols replace only the two BL instructions, not the
// surrounding original parent, its state setters, draw, or epilogue.
void SimpsonsNativeOverlayExpandedEnter(PPCContext& ctx,uint8_t* base) {
    // 827F5AF0: 4BC458C1, li r4,1 at 827F5AE8.
    overlayExpanded(ctx,base,1,0x827F5AF4);
}
void SimpsonsNativeOverlayExpandedExit(PPCContext& ctx,uint8_t* base) {
    // 827F5C08: 4BC457A9, li r4,0 at 827F5C00.
    overlayExpanded(ctx,base,0,0x827F5C0C);
}
