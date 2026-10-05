// Native language-query adaptation, original name/locale-lookup consumers,
// and explicit absence of a generic console configuration implementation.
#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "ppc_recomp_shared.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string_view>
#include <type_traits>

void SimpsonsNativeGetLanguage(PPCContext& ctx,uint8_t* base);
void SimpsonsNativeExternalMusicUnavailable(PPCContext& ctx,uint8_t* base);
void SimpsonsNativeGetTimeZoneInformation(PPCContext& ctx,uint8_t* base);
namespace Simpsons::Platform {uint32_t originalLanguageForWindowsUi(uint16_t) noexcept;}
namespace {
size_t checks=0;
void need(bool ok,const char* why) {++checks;if(!ok) throw std::runtime_error(why);}
constexpr std::array<const char*,12> names={"English","Japanese","German","French",
    "Spanish","Italian","Korean","T.Chinese","Portuguese","S.Chinese","Polish","Russian"};
struct Case {uint16_t windows;uint32_t original;};
// Explicit Microsoft LANGID examples, independent of the production switch.
constexpr std::array<Case,29> cases={{{0x0409,1},{0x0809,1},{0x040a,5},{0x080a,5},
    {0x0c0a,5},{0x0411,2},{0x0407,3},{0x0807,3},{0x0c07,3},{0x040c,4},
    {0x0c0c,4},{0x0410,6},{0x0810,6},{0x0412,7},{0x0404,8},{0x0c04,8},
    {0x1404,8},{0x0804,10},{0x1004,10},{0x0416,9},{0x0816,9},{0x0415,11},
    {0x0419,12},{0x0413,1},{0x041d,1},{0x041f,1},{0x0000,1},{0x1400,1},{0xffff,1}}};

void mappings() {
    for(auto test:cases) need(Simpsons::Platform::originalLanguageForWindowsUi(test.windows)==test.original,
                            "Windows language mapping differs from qualified original ID");
    for(uint16_t language:{0x0004,0x1804,0x7c04,0x1000,0x0c00})
        need(Simpsons::Platform::originalLanguageForWindowsUi(language)==1,
             "Unqualified Chinese/custom UI identifier did not use explicit English fallback");
    for(uint32_t id=0;id<65536;++id) {
        const uint32_t mapped=Simpsons::Platform::originalLanguageForWindowsUi(uint16_t(id));
        need(mapped>=1 && mapped<=12,"Native mapping escapes original wrapper/name-table bounds");
    }
    std::puts("PASS explicit Windows language mappings / original bounded enum / custom-locale fallback");
}

void nativeAbi() {
    struct Page {
        uint8_t* p=static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS));
        ~Page(){if(p) VirtualFree(p,0,MEM_RELEASE);}
    } guard;
    need(guard.p!=nullptr,"Language guard-page allocation failed");
    static_assert(std::is_trivially_copyable_v<PPCContext>);
    const uint32_t saved=PPCFPSCRRegister::getcsr();
    struct Restore {uint32_t value;~Restore(){PPCFPSCRRegister::restoreHostCSR(value);}} restore{saved};
    for(uint32_t controls:{0x1f80u,0x3fc0u,0x5f80u,0x9fc0u,0xe07fu}) {
        for(unsigned iteration=0;iteration<2;++iteration) {
            PPCContext ctx{},expected{};std::memset(&ctx,0xa5,sizeof(ctx));
            ctx.r1.u64=0;ctx.lr=0xfedcba988282af00ull;ctx.fpscr.csr=0xe07f;
            std::memcpy(&expected,&ctx,sizeof(ctx));
            const LANGID before=GetUserDefaultUILanguage();
            PPCFPSCRRegister::restoreHostCSR(controls);
            SimpsonsNativeGetLanguage(ctx,iteration?guard.p:nullptr);
            const uint32_t actualControls=PPCFPSCRRegister::getcsr();
            PPCFPSCRRegister::restoreHostCSR(saved);
            const LANGID after=GetUserDefaultUILanguage();
            need(before==after,"Windows UI language changed during the finite observation");
            need(ctx.r3.u64==Simpsons::Platform::originalLanguageForWindowsUi(before),
                 "Native result differs from actual Windows user UI language");
            need(actualControls==controls,"Native language query changed host FP controls/status");
            expected.r3.u64=ctx.r3.u64;
            need(!std::memcmp(&ctx,&expected,sizeof(ctx)),"Language query changed ABI outside r3");
        }
    }
    const auto language=GetUserDefaultUILanguage();
    std::printf("PASS real Windows UI LANGID=%04X original=%u / no guest access / whole-context ABI / five FP profiles\n",
                language,Simpsons::Platform::originalLanguageForWindowsUi(language));
}

void originalConsumers(const char* imagePath) {
    Simpsons::Runtime rt;rt.load(imagePath);rt.map(0x10000,0x10000,true,"Native configuration fixtures");
    auto* base=rt.base;
    const std::array<uint32_t,12> wrapper={0x7d8802a6,0x9181fff8,0x9421ffa0,0x39600000,
        0x38e10050,0x38c00004,0x38a10054,0x38800009,0x38600003,0xb1610050,0x4888fbad,0x2c030000};
    for(size_t i=0;i<wrapper.size();++i)
        need(PPC_LOAD_U32(0x82432d10+uint32_t(i)*4)==wrapper[i],"Original language/config wrapper pin");
    const std::array<uint8_t,12> nameOffsets={0,0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x90,0x80,0xa0,0xb0};
    need(!std::memcmp(rt.pointer(0x821d3c20,12,false),nameOffsets.data(),12),"Original language-name switch table changed");
    // Pin every original name pointer generated by the small table's branches.
    for(size_t id=0;id<names.size();++id) {
        const uint32_t branch=0x82258824+nameOffsets[id];
        need(PPC_LOAD_U32(branch)==0x3d60821d && PPC_LOAD_U32(branch+4)==0x396b1f4c,
             "Original language-name branch framing");
        const uint32_t addi=PPC_LOAD_U32(branch+8);
        need((addi&0xffff0000)==0x386b0000,"Original language-name address instruction");
        const uint32_t address=0x821d1f4c+int16_t(addi);
        need(!std::memcmp(rt.pointer(address,std::strlen(names[id])+1,false),names[id],std::strlen(names[id])+1),
             "Native enum disagrees with original language-name bytes");
    }
    PPCContext incoming{};incoming.r1.u64=0x20000;incoming.lr=0x11223344;
    Simpsons::EngineCpuCalls cpu(incoming,base);auto& ctx=cpu.registers();
    const uint64_t sp=ctx.r1.u64,lr=ctx.lr;
    ctx.r14.u64=0xfedcba9887654321;ctx.r31.u64=0x123456789abcdef0;
    const auto windows=GetUserDefaultUILanguage();
    const uint32_t selected=Simpsons::Platform::originalLanguageForWindowsUi(windows);
    need(cpu.invoke(0x82432d10)==selected,"Generated entry did not reach the native language hook");
    need(ctx.r3.u64==selected,"Original native language result not zero-extended");
    const uint32_t name=cpu.invoke(0x822587e0);
    need(GetUserDefaultUILanguage()==windows,"Windows UI language changed during AOT name fixture");
    need(!std::memcmp(rt.pointer(name,std::strlen(names[selected-1])+1,false),names[selected-1],std::strlen(names[selected-1])+1),
         "Retained AOT name consumer disagrees with native enum");
    need(ctx.r1.u64==sp && ctx.lr==lr && ctx.r14.u64==0xfedcba9887654321 && ctx.r31.u64==0x123456789abcdef0,
         "Original language/name consumer changed nonvolatile ABI");

    const auto tablePath=std::filesystem::path(imagePath).parent_path().parent_path()/"Simpsons Game, The (USA)"/"text"/"localetable.txt";
    std::ifstream file(tablePath,std::ios::binary);
    need(bool(file),"Original USA locale table missing");
    const std::string contents((std::istreambuf_iterator<char>(file)),{});
    const std::string expected="5\nEA205701A0X11\nntsc_en\n0\n0\nen\nen\nEnglish\n1\n(none)\nss\nen\nStringIDs\n0\n(none)\n\n";
    // The exact fixture framing is qualified by the evidence script against
    // the original parser, not an invented five-language interpretation.
    need(contents==expected,"Original USA locale table bytes changed");
    constexpr uint32_t table=0x11000,records=0x11100;
    PPC_STORE_U32(table+4,2);PPC_STORE_U32(table+8,records);
    std::memcpy(rt.pointer(0x11200,3,true),"en",3);
    std::memcpy(rt.pointer(0x11210,3,true),"ss",3);
    PPC_STORE_U32(records,0x11200);PPC_STORE_U32(records+0x14,0x11210);
    for(const char* code:{"en","ss","de","fr","es","it","ja","ko","zh",""}) {
        std::memcpy(rt.pointer(0x11300,std::strlen(code)+1,true),code,std::strlen(code)+1);
        const uint32_t index=cpu.invoke(0x8282c750,table,0x11300);
        need(index==(std::string_view(code)=="ss"?1u:0u),"Original locale search lost index-zero fallback");
    }
    // Actual host time-zone records must survive the guest byte order exactly.
    // Execute the original higher-level bias consumer as an independent check
    // on signed bias units and standard/daylight classification.
    constexpr uint32_t zoneOut=0x12000,biasOut=0x12100;
    TIME_ZONE_INFORMATION zone{};const auto zoneState=GetTimeZoneInformation(&zone);
    need(zoneState<=2,"Actual native time-zone query failed");
    const auto savedFP=PPCFPSCRRegister::getcsr();
    for(uint32_t fp:{0x1F80u,0x3FC0u,0x5F80u,0x9FC0u,0xE07Fu}){
        PPCContext query;std::memset(&query,0xA5,sizeof(query));query.r3.u64=zoneOut;
        PPCContext expectedQuery;std::memcpy(&expectedQuery,&query,sizeof(query));expectedQuery.r3.u64=zoneState;
        std::memset(rt.pointer(zoneOut-4,180,true),0xA5,180);
        PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(0x13579BDF);SimpsonsNativeGetTimeZoneInformation(query,base);
        const auto actualFP=PPCFPSCRRegister::getcsr();const auto hostError=GetLastError();PPCFPSCRRegister::restoreHostCSR(savedFP);
        need(!std::memcmp(&query,&expectedQuery,sizeof(query))&&actualFP==fp&&hostError==0x13579BDF,"Time-zone query changed CPU/host state");
        need(PPC_LOAD_U32(zoneOut)==uint32_t(zone.Bias)&&PPC_LOAD_U32(zoneOut+84)==uint32_t(zone.StandardBias)&&
            PPC_LOAD_U32(zoneOut+168)==uint32_t(zone.DaylightBias),"Native time-zone bias fields differ");
        for(uint32_t i=0;i<32;++i)need(PPC_LOAD_U16(zoneOut+4+i*2)==zone.StandardName[i]&&PPC_LOAD_U16(zoneOut+88+i*2)==zone.DaylightName[i],"Native UTF16 time-zone names differ");
        const SYSTEMTIME dates[]{zone.StandardDate,zone.DaylightDate};
        for(uint32_t i=0;i<2;++i){const auto& d=dates[i];const std::array<uint16_t,8> fields{d.wYear,d.wMonth,d.wDayOfWeek,d.wDay,d.wHour,d.wMinute,d.wSecond,d.wMilliseconds};
            for(uint32_t j=0;j<8;++j)need(PPC_LOAD_U16(zoneOut+68+i*84+j*2)==fields[j],"Native time-zone transition fields differ");}
        need(PPC_LOAD_U32(zoneOut-4)==0xA5A5A5A5&&PPC_LOAD_U32(zoneOut+172)==0xA5A5A5A5,"Time-zone query exceeded original output");
    }
    need(cpu.invoke(0x82432A10,zoneOut)==zoneState,"Original time-zone entry did not reach native service");
    cpu.invoke(0x82CB7A60,biasOut);
    const int64_t biasMinutes=int64_t(zone.Bias)+(zoneState==1?zone.StandardBias:zoneState==2?zone.DaylightBias:0);
    need(int64_t(PPC_LOAD_U64(biasOut))==biasMinutes*600000000ll,"Original timestamp consumer lost signed native time-zone bias");
    TIME_ZONE_INFORMATION zoneAfter{};
    need(GetTimeZoneInformation(&zoneAfter)==zoneState&&!std::memcmp(&zoneAfter,&zone,sizeof(zone)),"Native time zone changed during observation");
    for(uint32_t address:{0u,zoneOut+1,0x1FFFCu}){
        PPCContext query{};query.r3.u64=address;bool rejected=false;
        try{SimpsonsNativeGetTimeZoneInformation(query,base);}catch(const Simpsons::Failure&){rejected=true;}
        need(rejected,"Invalid time-zone output was accepted");
    }
    {PPCContext query{};query.r3.u64=zoneOut;bool rejected=false;
        try{SimpsonsNativeGetTimeZoneInformation(query,nullptr);}catch(const Simpsons::Failure&){rejected=true;}
        need(rejected,"Invalid time-zone runtime was accepted");}
    std::puts("PASS native time-zone rules / original signed timestamp bias / exact guest record / CPU and host state");
    constexpr uint32_t timeInput=0x12200,timeOutput=0x12220;
    const std::array<SYSTEMTIME,6> dateCases{{
        {1601,1,1,1,0,0,0,0},{1970,1,4,1,0,0,0,1},{2000,2,2,29,12,34,56,789},
        {2026,9,0,13,23,59,59,999},{2100,3,1,1,1,2,3,4},{2400,2,2,29,0,0,0,0}}};
    for(const auto& date:dateCases){
        FILETIME fileTime{};need(SystemTimeToFileTime(&date,&fileTime)!=FALSE,"Calendar fixture cannot be represented as native file time");
        const uint64_t ticks=((uint64_t(fileTime.dwHighDateTime)<<32)|fileTime.dwLowDateTime)+9999;
        PPC_STORE_U64(timeInput,ticks);
        const std::array<uint16_t,8> fields{date.wYear,date.wMonth,date.wDay,date.wHour,date.wMinute,date.wSecond,date.wMilliseconds,date.wDayOfWeek};
        for(uint32_t fp:{0x1F80u,0x3FC0u,0x5F80u,0x9FC0u,0xE07Fu}){
            PPCContext query;std::memset(&query,0xA5,sizeof(query));query.r3.u64=timeInput;query.r4.u64=timeOutput;
            PPCContext expectedQuery;std::memcpy(&expectedQuery,&query,sizeof(query));
            std::memset(rt.pointer(timeOutput-4,24,true),0xA5,24);
            PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(0x13579BDF);__imp__RtlTimeToTimeFields(query,base);
            const auto actualFP=PPCFPSCRRegister::getcsr();const auto hostError=GetLastError();PPCFPSCRRegister::restoreHostCSR(savedFP);
            need(!std::memcmp(&query,&expectedQuery,sizeof(query))&&actualFP==fp&&hostError==0x13579BDF,"Calendar conversion changed CPU/host state");
            for(uint32_t i=0;i<8;++i)need(PPC_LOAD_U16(timeOutput+i*2)==fields[i],"Native calendar result differs from independent calendar fixture");
            need(PPC_LOAD_U64(timeInput)==ticks&&PPC_LOAD_U32(timeOutput-4)==0xA5A5A5A5&&PPC_LOAD_U32(timeOutput+16)==0xA5A5A5A5,"Calendar conversion changed input or exceeded output");
        }
        need(cpu.invoke(0x824393C8,timeInput,timeOutput)==1,"Original file-time conversion failed");
        const std::array<uint16_t,8> systemFields{date.wYear,date.wMonth,date.wDayOfWeek,date.wDay,date.wHour,date.wMinute,date.wSecond,date.wMilliseconds};
        for(uint32_t i=0;i<8;++i)need(PPC_LOAD_U16(timeOutput+i*2)==systemFields[i],"Original SYSTEMTIME consumer reordered native fields incorrectly");
    }
    for(bool badInput:{false,true}){PPCContext query{};query.r3.u64=badInput?0:timeInput;query.r4.u64=badInput?timeOutput:0;bool rejected=false;
        try{__imp__RtlTimeToTimeFields(query,base);}catch(const Simpsons::Failure&){rejected=true;}need(rejected,"Invalid native calendar buffer was accepted");}
    std::puts("PASS original file-time calendar conversion / epoch and century leap boundaries / submillisecond truncation / host state");
    // Raw configuration stays an unsupported import: even this category is
    // qualified only at the no-argument wrapper, without any console store.
    rt.checkingImports=true;
    std::array<uint8_t,16> sentinels{};sentinels.fill(0xa5);
    std::memcpy(rt.pointer(0x11400,sentinels.size(),true),sentinels.data(),sentinels.size());
    ctx.r3.u64=3;ctx.r4.u64=9;ctx.r5.u64=0x11404;ctx.r6.u64=4;ctx.r7.u64=0x11400;
    bool rejected=false;
    try {__imp__ExGetXConfigSetting(ctx,base);}
    catch(const Simpsons::Failure& error) {
        need(std::string(error.what()).find("unimplemented import __imp__ExGetXConfigSetting")!=std::string::npos,
             "Unexpected raw configuration failure");rejected=true;
    }
    need(rejected,"Raw console configuration import silently became supported");
    need(!std::memcmp(rt.pointer(0x11400,sentinels.size(),false),sentinels.data(),sentinels.size()),
         "Unsupported raw configuration changed guest value/size outputs");
    // Optional external music is unavailable at its two specific platform
    // boundaries. Exercise both real game intent wrappers and their exact
    // low-byte stores, including the global-owner absent branch. No successful
    // platform ownership result, notification or host-audio state is supplied.
    constexpr uint32_t music=0x11800,global=0x82E06E30;
    const uint32_t oldGlobal=PPC_LOAD_U32(global),oldClaim=PPC_LOAD_U32(0x82E39BC8),oldRestore=PPC_LOAD_U32(0x82E39BCC);
    auto* object=rt.pointer(music,0x80,true);std::array<uint8_t,0x80> objectBefore;objectBefore.fill(0xA5);
    const auto allocationCount=rt.allocations.size(),handleCount=rt.handles.size();
    need(PPC_LOAD_U32(0x82808E54)==0x9883004D && PPC_LOAD_U32(0x828099BC)==0x986B004D &&
         PPC_LOAD_U32(0x828099C8)==0x4836FAF0 && PPC_LOAD_U32(0x828099CC)==0x4836FBC4,
         "Original music intent wrapper instruction pins changed");
    for(uint32_t request:{0u,1u,0x100u,0x101u,0xFFu,0xFFFFFFFFu}) {
        for(uint32_t entry:{0x82808E50u,0x828099A8u}) {
            std::memcpy(object,objectBefore.data(),objectBefore.size());PPC_STORE_U32(global,music);
            auto expected=objectBefore;expected[0x4D]=uint8_t(request);
            ctx.lr=0x826B957C;const auto before=ctx;
            const auto result=entry==0x82808E50?cpu.invoke(entry,music,request):cpu.invoke(entry,request);
            need(result==50 && ctx.r3.u64==50,"Unavailable external music did not return its Win32 unsupported-operation error");
            need(!std::memcmp(object,expected.data(),expected.size()),"Original music intent store changed unexpected object bytes");
            need(ctx.r1.u64==before.r1.u64 && ctx.lr==before.lr && ctx.r14.u64==before.r14.u64 && ctx.r31.u64==before.r31.u64,
                 "Native music availability damaged original tail-call/nonvolatile ABI");
            need(PPC_LOAD_U32(0x82E39BC8)==oldClaim && PPC_LOAD_U32(0x82E39BCC)==oldRestore &&
                 rt.allocations.size()==allocationCount && rt.handles.size()==handleCount,
                 "Unavailable music service changed claim/restore state or created resources");
        }
        PPC_STORE_U32(global,0);std::memcpy(object,objectBefore.data(),objectBefore.size());
        need(cpu.invoke(0x828099A8,request)==request && !std::memcmp(object,objectBefore.data(),objectBefore.size()),
             "Absent original music owner did not retain its original no-call return");
    }
    PPC_STORE_U32(global,oldGlobal);
    // The generic system-version import remains an explicit failure. Returning
    // an invented old/new console version must not select another SDK branch.
    bool versionRejected=false;
    try {__imp__XamGetSystemVersion(ctx,base);}
    catch(const Simpsons::Failure& error) {
        versionRejected=std::string(error.what()).find("unimplemented import __imp__XamGetSystemVersion")!=std::string::npos;
    }
    need(versionRejected,"Optional music adaptation accidentally supplied a generic console system version");
    const std::array<uint32_t,14> yieldWords={0x7D8802A6,0x9181FFF8,0x9421FFA0,0x4814CBA1,
        0x3D604000,0x616B0024,0x7D635850,0x7D6B0034,0x556BDFFE,0x69630001,0x38210060,
        0x8181FFF8,0x7D8803A6,0x4E800020};
    for(uint32_t i=0;i<yieldWords.size();++i)need(PPC_LOAD_U32(0x82B76B98+4*i)==yieldWords[i],
        "Original native-yield Boolean conversion changed");
    for(unsigned i=0;i<16;++i) {
        const auto before=ctx;const auto result=cpu.invoke(0x82B76B98);
        need(result<=1 && ctx.r1.u64==before.r1.u64 && ctx.lr==before.lr &&
             ctx.r14.u64==before.r14.u64 && ctx.r31.u64==before.r31.u64,
             "Original yield wrapper lost its Boolean or nonvolatile return ABI");
    }
    std::puts("PASS original music intent/tail calls / unsupported platform result / no claim or fabricated system version");
    std::puts("PASS original AOT language/name/locale lookup / USA index-zero fallback / raw config rejection");
}
}

int main(int argc,char** argv) {
    try {
        need(argc==2,"Original flat image path required");mappings();nativeAbi();originalConsumers(argv[1]);
        std::printf("NativeConfiguration PASS: %zu checks\n",checks);return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"NativeConfiguration FAIL after %zu checks: %s\n",checks,error.what());return 1;
    }
}
