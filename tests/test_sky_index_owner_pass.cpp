// Independently allocated original R16 owners with a small selected prefix.
// Full byte-header transport and unused ownership differ from draw count.
#define SIMPSONS_SKY_OWNER_HELPERS_ONLY
#include "test_sky_large_owner_pass.cpp"
#undef SIMPSONS_SKY_OWNER_HELPERS_ONLY

int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==3,"Original image and index case (large, odd_tail, native_capacity) required");
        const std::string_view mode=argv[2];
        need(mode=="large"||mode=="odd_tail"||mode=="native_capacity","Unknown original index owner case");
        const uint32_t bytes=mode=="large"?0x01000002u:mode=="odd_tail"?0x01000001u:0x04000002u;
        Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;
        try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original index-owner startup boundary missing");
        largeOwnerRun(rt,entry,false,bytes);
        std::printf("PASS original sky independent index owner: %zu checks; original allocator/header/declaration, eight selected draws, pixels/ABI, truncated selected words and paired original release\n",checks);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original sky index owner: %zu checks stage=%s: %s\n",checks,stage,error.what());return 1;}
}
