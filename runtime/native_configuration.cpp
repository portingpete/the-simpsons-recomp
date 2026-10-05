#include "runtime.h"
#include <array>
#include <cstring>
#include <utility>

namespace Simpsons::Platform {
// Native PC policy at original XGetLanguage wrapper 82432D10. Original
// 822587E0's table names all twelve IDs; this is not a console config store.
// Regional UI variants share the original primary-language ID. Chinese uses
// the five qualified Windows regional variants to distinguish the original
// T.Chinese (8) and S.Chinese (10). Other/unqualified identifiers use English.
uint32_t originalLanguageForWindowsUi(uint16_t language) noexcept {
    switch(PRIMARYLANGID(language)) {
    case LANG_ENGLISH: return 1;
    case LANG_JAPANESE: return 2;
    case LANG_GERMAN: return 3;
    case LANG_FRENCH: return 4;
    case LANG_SPANISH: return 5;
    case LANG_ITALIAN: return 6;
    case LANG_KOREAN: return 7;
    case LANG_CHINESE:
        switch(SUBLANGID(language)) {
        case SUBLANG_CHINESE_TRADITIONAL:
        case SUBLANG_CHINESE_HONGKONG:
        case SUBLANG_CHINESE_MACAU: return 8;
        case SUBLANG_CHINESE_SIMPLIFIED:
        case SUBLANG_CHINESE_SINGAPORE: return 10;
        default: return 1;
        }
    case LANG_PORTUGUESE: return 9;
    case LANG_POLISH: return 11;
    case LANG_RUSSIAN: return 12;
    default: return 1;
    }
}
}

// Plain reference is required by generated midasm declarations (PPC_FUNC's
// __restrict reference has different MSVC C++ decoration). No arguments or
// guest memory effects; only r3 receives a zero-extended original language ID.
// Missing translations still follow original 8282AEE8/8282C750 asset fallback.
void SimpsonsNativeGetLanguage(PPCContext& ctx,uint8_t* base) {
    (void)base;
    struct HostFloatingPoint {
        const uint32_t previous=PPCFPSCRRegister::getcsr();
        HostFloatingPoint(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
        ~HostFloatingPoint(){PPCFPSCRRegister::restoreHostCSR(previous);}
    } floatingPoint;
    ctx.r3.u64=Simpsons::Platform::originalLanguageForWindowsUi(GetUserDefaultUILanguage());
}

// Native availability policy at the two optional platform music-control
// wrappers82B794B8/82B79590. The PC port has no integrated external-music
// controller. A real error is returned; no playback ownership, notification,
// desktop-media state or successful completion is invented. The original
// game wrappers82808E50/828099A8 retain their low-byte intent stores and tail
// calls. Their native Dac0/game-audio paths remain separate.
// These wrappers return Win32-style errors (82B790E8 converts HRESULT values),
// so ERROR_NOT_SUPPORTED is50, not HRESULT80070032. Leave the generic console
// version and message imports guarded for any unrelated consumer.
void SimpsonsNativeExternalMusicUnavailable(PPCContext& ctx,uint8_t* base) {
    (void)base;
    ctx.r3.u64=ERROR_NOT_SUPPORTED;
}

// Original GetTimeZoneInformation platform wrapper82432A10. The original
// timestamp/bias consumers remain AOT; the PC supplies its actual Windows
// time-zone rules and current standard/daylight classification.
void SimpsonsNativeGetTimeZoneInformation(PPCContext& ctx,uint8_t* base) {
    struct HostState {uint32_t fp=PPCFPSCRRegister::getcsr();DWORD error=GetLastError();
        HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}} host;
    auto* rt=Simpsons::active;
    if(!rt||base!=rt->base)throw Simpsons::Failure("Invalid native time-zone runtime");rt->checkRunning();
    if(!ctx.r3.u32||(ctx.r3.u32&3))throw Simpsons::Failure("Invalid original time-zone output");
    auto* output=PPCGuestPointer(base,ctx.r3.u32,172,true);
    TIME_ZONE_INFORMATION zone{};const DWORD state=GetTimeZoneInformation(&zone);
    if(state==TIME_ZONE_ID_INVALID)throw Simpsons::Failure("Native time-zone query failed: "+std::to_string(GetLastError()));
    if(state>TIME_ZONE_ID_DAYLIGHT)throw Simpsons::Failure("Unknown native time-zone classification");
    static_assert(sizeof(zone)==172&&offsetof(TIME_ZONE_INFORMATION,StandardBias)==84&&offsetof(TIME_ZONE_INFORMATION,DaylightBias)==168);
    // Both layouts have the same field offsets. Only the three LONG fields
    // are 32-bit; names and SYSTEMTIME members are all 16-bit UTF-16/integers.
    std::array<uint8_t,172> bytes{};std::memcpy(bytes.data(),&zone,bytes.size());
    for(size_t i=0;i<bytes.size();){
        if(i==0||i==84||i==168){std::swap(bytes[i],bytes[i+3]);std::swap(bytes[i+1],bytes[i+2]);i+=4;}
        else{std::swap(bytes[i],bytes[i+1]);i+=2;}
    }
    rt->checkRunning();std::memcpy(output,bytes.data(),bytes.size());ctx.r3.u64=state;
}
