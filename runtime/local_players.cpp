#include "runtime.h"
#include "native_local_players.h"
#include "native_notifications.h"
#include <cstring>

namespace Simpsons {
void Runtime::configureLocalPlayers(const std::filesystem::path& root) {
    if(active!=this || root.empty()) throw Failure("Invalid native local-profile configuration");
    checkRunning();
    std::lock_guard lock(localPlayerMutex);
    if(localPlayers) throw Failure("Native local-profile store is already in use");
    localProfileRoot=std::filesystem::absolute(root).lexically_normal();
}

std::shared_ptr<Platform::NativeLocalPlayers> Runtime::localPlayerSource() {
    if(active!=this) throw Failure("Native local players belong to another runtime");
    checkRunning();
    std::lock_guard lock(localPlayerMutex);
    if(!localPlayers) {
        if(localProfileRoot.empty()) {
            if(!gameRoot.is_absolute()) throw Failure("Native local-profile store requires a loaded image or explicit root");
            // Native portable storage policy, separate from the read-only game
            // data. Loading stored profiles never activates a session slot.
            localProfileRoot=gameRoot.parent_path()/"userdata"/"local-profiles";
        }
        localPlayers=std::make_shared<Platform::NativeLocalPlayers>(localProfileRoot);
    }
    return localPlayers;
}

namespace {
void publishPlayerChange(Runtime& rt,const std::shared_ptr<Platform::NativeNotifications>& source) {
    try {
        // Original 828616A8 ignores this payload and re-queries state/identity.
        // Native ID-only invalidation uses zero; no recovered slot-mask claim.
        source->publish({0xA,0});
    } catch(...) {
        // State is already committed. A failed broadcast cannot be represented
        // as a rolled-back activation, particularly with multiple listeners.
        rt.requestStop("Native profile state changed but notification delivery failed");
        throw;
    }
}
}

bool Runtime::activateLocalPlayer(uint32_t slot,const std::string& profileId) {
    auto players=localPlayerSource();
    auto source=notificationSource();
    std::lock_guard transition(localPlayerTransitionMutex);
    checkRunning();
    const bool changed=players->activate(slot,profileId);
    if(changed) publishPlayerChange(*this,source);
    return changed;
}

bool Runtime::signOutLocalPlayer(uint32_t slot) {
    auto players=localPlayerSource();
    auto source=notificationSource();
    std::lock_guard transition(localPlayerTransitionMutex);
    checkRunning();
    const bool changed=players->signOut(slot);
    if(changed) publishPlayerChange(*this,source);
    return changed;
}

uint32_t Runtime::localPlayerState(uint32_t slot) {
    auto players=localPlayerSource();
    // Original candidate7 and no-selection4 are not valid platform slots.
    // An absent slot has no signed-in profile under the native session policy.
    return slot<4?players->state(slot):0;
}
}

PPC_FUNC(__imp__XamUserGetSigninState) {
    if(!Simpsons::active || base!=Simpsons::active->base)
        throw Simpsons::Failure("Invalid local-player query runtime");
    struct HostFloatingPoint {
        const uint32_t previous=PPCFPSCRRegister::getcsr();
        HostFloatingPoint(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
        ~HostFloatingPoint(){PPCFPSCRRegister::restoreHostCSR(previous);}
    } floatingPoint;
    // 82431880 tails to this import. Only r3.u32 is an argument; the caller's
    // incidental r4 is not flags. Do not change original selection caches.
    ctx.r3.u64=Simpsons::active->localPlayerState(ctx.r3.u32);
}

PPC_FUNC(__imp__XamUserGetXUID) {
    if(!Simpsons::active || base!=Simpsons::active->base)
        throw Simpsons::Failure("Invalid local identity query runtime");
    struct HostState {
        const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
        HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
        ~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
    } host;
    auto& rt=*Simpsons::active;rt.checkRunning();
    // Original82431F08 requests all profile types, mask7. Only the reached
    // query is admitted; online-only/type-specific policy remains separate.
    if(ctx.r4.u32!=7)throw Simpsons::Failure("Unqualified local identity type mask");
    const uint32_t output=ctx.r5.u32,index=ctx.r3.u32;
    if(!output){ctx.r3.u64=uint32_t(E_INVALIDARG);return;}
    auto* destination=rt.pointer(output,8,true);
    const uint64_t identity=index<4?rt.localPlayerSource()->identity(index):0;
    // A real active file lease owns the stable native key. Empty sessions do
    // not manufacture a player. Failure outputs are zero, matching the caller
    // ABI; original827B2788 alone publishes the resulting player association.
    for(uint32_t i=0;i<8;++i)destination[i]=uint8_t(identity>>(56-8*i));
    ctx.r3.u64=index>=4?uint32_t(E_INVALIDARG):identity?0:uint32_t(HRESULT_FROM_WIN32(ERROR_NO_SUCH_USER));
}

PPC_FUNC(__imp__XamUserGetName) {
    if(!Simpsons::active || base!=Simpsons::active->base)
        throw Simpsons::Failure("Invalid local name query runtime");
    struct HostState {
        const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
        HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
        ~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
    } host;
    auto& rt=*Simpsons::active;rt.checkRunning();
    const uint32_t index=ctx.r3.u32,output=ctx.r4.u32,length=ctx.r5.u32;
    // Both original827B25E8 and827B2660 request exactly16 bytes through the
    // tail wrapper82431878. Native profiles own1..15 printable ASCII bytes.
    if(length!=16)throw Simpsons::Failure("Unqualified local name buffer length");
    if(index>=4 || !output){ctx.r3.u64=ERROR_INVALID_PARAMETER;return;}
    auto* destination=rt.pointer(output,length,true);
    const auto profile=rt.localPlayerSource()->profile(index);
    if(!profile){destination[0]=0;ctx.r3.u64=ERROR_NO_SUCH_USER;return;}
    if(profile->name.size()>=length) throw Simpsons::Failure("Local player name exceeds guest buffer");
    std::memcpy(destination,profile->name.c_str(),profile->name.size()+1);
    ctx.r3.u64=0;
}

PPC_FUNC(__imp__XamUserReadProfileSettings) {
    struct HostState {
        const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
        HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
        ~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
    } host;
    if(!Simpsons::active||base!=Simpsons::active->base)throw Simpsons::Failure("Invalid native profile preferences runtime");
    auto& rt=*Simpsons::active;rt.checkRunning();
    const uint32_t title=ctx.r3.u32,slot=ctx.r4.u32,count=ctx.r7.u32,ids=ctx.r8.u32,sizeOut=ctx.r9.u32,out=ctx.r10.u32;
    // Original82C71CB8 passes argument9 at SP+54. Only the observed synchronous
    // local-slot query for inversion/vibration is qualified here.
    if((title&&title!=0x45410809)||ctx.r5.u32||ctx.r6.u32||count<1||count>2)
        throw Simpsons::Failure("Unqualified native profile preferences request");
    rt.pointer(ctx.r1.u32,0x58,false);
    if(PPC_LOAD_U32(ctx.r1.u32+0x54))throw Simpsons::Failure("Asynchronous profile preferences are not qualified");
    if(slot>=4||!sizeOut||!ids||(sizeOut&3)||(ids&3))throw Simpsons::Failure("Invalid native profile preferences arguments");
    rt.pointer(sizeOut,4,true);rt.pointer(ids,count*4,false);
    uint32_t seen=0;
    for(uint32_t i=0;i<count;++i){
        const auto id=PPC_LOAD_U32(ids+i*4);const uint32_t bit=id==0x10040002?1:id==0x10040003?2:0;
        if(!bit||(seen&bit))throw Simpsons::Failure("Unqualified or repeated controller preference ID");seen|=bit;
    }
    const auto overlaps=[](uint32_t a,uint32_t an,uint32_t b,uint32_t bn){return uint64_t(a)<uint64_t(b)+bn&&uint64_t(b)<uint64_t(a)+an;};
    if(overlaps(sizeOut,4,ids,count*4))throw Simpsons::Failure("Profile preference size output aliases IDs");
    const uint32_t supplied=PPC_LOAD_U32(sizeOut),required=8+40*count;
    if(supplied&&!out)throw Simpsons::Failure("Profile preference result buffer is absent");
    if(out&&((out&3)||overlaps(out,required,sizeOut,4)||overlaps(out,required,ids,count*4)))
        throw Simpsons::Failure("Invalid or aliased profile preference result buffer");
    if(!out||supplied<required){if(!supplied)PPC_STORE_U32(sizeOut,required);ctx.r3.u64=ERROR_INSUFFICIENT_BUFFER;return;}
    rt.pointer(out,required,true);
    const auto profile=rt.localPlayerSource()->profile(slot);
    if(!profile){ctx.r3.u64=ERROR_FUNCTION_FAILED;return;}
    // The owned, checksummed SIMPSONS-LOCAL-PROFILE 1 schema has exactly GUID
    // and display name; it contains no inversion or vibration overrides.
    // Return that real absence, with no invented setting records/defaults.
    // Original827B2CE0 then leaves its availability flags false and original
    // 823A1228 retains the game's own controller defaults.
    std::memset(rt.pointer(out,required,true),0,required);
    PPC_STORE_U32(out+4,out+8);ctx.r3.u64=0;
    std::fprintf(stderr,"[NATIVE PREFERENCES] slot=%u profile=%s requested=%u returned=0; immutable native v1 profile has no controller overrides\n",slot,profile->id.c_str(),count);
}
