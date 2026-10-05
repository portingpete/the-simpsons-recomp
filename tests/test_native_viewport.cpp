#include "renderer/native_backend.h"
#include <bcrypt.h>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <thread>

using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void require(bool value,const char* message) {++checks;if(!value) throw Error(message);}
bool same(const D3D11_VIEWPORT& a,const D3D11_VIEWPORT& b) {
    return a.TopLeftX==b.TopLeftX && a.TopLeftY==b.TopLeftY && a.Width==b.Width && a.Height==b.Height &&
           a.MinDepth==b.MinDepth && a.MaxDepth==b.MaxDepth;
}
bool same(const std::optional<D3D11_VIEWPORT>& a,const std::optional<D3D11_VIEWPORT>& b) {
    return a.has_value()==b.has_value() && (!a || same(*a,*b));
}
void reject(NativeBackend& backend,const D3D11_VIEWPORT& value,const char* expected=nullptr) {
    const auto before=backend.viewport();
    bool failed=false;
    try {backend.setViewport(value);} catch(const Error& error) {
        failed=true;
        if(expected) require(std::string(error.what()).find(expected)!=std::string::npos,"Failure omitted actionable viewport limitation");
    }
    require(failed,"Invalid viewport silently succeeded");
    require(same(before,backend.viewport()),"Rejected viewport changed actual D3D11 state");
}

void originalEvidence(const char* path) {
    std::ifstream input(path,std::ios::binary);require(bool(input),"Cannot read original-derived image");
    std::vector<uint8_t> image{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    require(image.size()==15466496,"Unexpected original-derived image size");
    std::array<uint8_t,32> hash{};
    require(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,image.data(),ULONG(image.size()),hash.data(),ULONG(hash.size()))>=0,
            "Original evidence SHA256 failed");
    constexpr char hex[]="0123456789abcdef";
    std::string identity;for(auto byte:hash) {identity+=hex[byte>>4];identity+=hex[byte&15];}
    require(identity=="6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0","Original evidence identity changed");
    auto word=[&](uint32_t address) {
        const auto* p=image.data()+address-0x82000000;
        return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
    };
    // Original camera r4 -> retained r31 -> target/viewport helper r3.
    // Six camera stores, four unsigned integer loads and two float loads in
    // the SDK wrapper, then original negative depth scale/offset computation.
    for(auto [pc,expected]:std::array<std::pair<uint32_t,uint32_t>,27>{{
        {0x823F00DC,0x7C9F2378},{0x823F021C,0x7FE3FB78},{0x823F0224,0x4BFFE4A5},
        {0x823EE7A0,0xA97E001C},{0x823EE7AC,0x91610050},{0x823EE7B4,0x91610054},
        {0x823EE7BC,0x91610058},{0x823EE7C4,0x9161005C},{0x823EE7D4,0xD0010060},
        {0x823EE7DC,0xD0010064},{0x823EE7E0,0x4804E919},
        {0x8243D104,0x8164000C},{0x8243D10C,0x81240008},{0x8243D110,0xC0C40014},
        {0x8243D114,0x81040004},{0x8243D118,0xC0A40010},{0x8243D11C,0x80E40000},
        {0x8243D124,0xF9210058},{0x8243D138,0xFC00069C},{0x8243D154,0xFC606818},
        {0x8243D160,0x4BFFFD21},{0x8243D060,0xED9AF828},{0x8243D064,0xD3FF291C},
        {0x8243D068,0xD19F2918},{0x82000BB0,0x3F800000},{0x821DD0D8,0x00000000},
        {0x82000FB8,0x3F000000}}})
        require(word(pc)==expected,"Original viewport ABI instruction/constant changed");
    require(std::bit_cast<float>(word(0x82000BB0))==1 && std::bit_cast<float>(word(0x821DD0D8))==0,
            "Original camera depth endpoints differ");
}

void nativeTests(bool hardware) {
    NativeBackend backend(!hardware),second(!hardware);
    const NativeBackend& query=backend;
    require(backend.level()>=D3D_FEATURE_LEVEL_11_0,"Wrong viewport feature level");
    require(!query.viewport() && !second.viewport(),"New native context unexpectedly has a viewport");
    const D3D11_VIEWPORT originalReverse{0,0,1280,720,1,0};
    reject(backend,originalReverse,"reversed depth");
    require(originalReverse.MinDepth==1 && originalReverse.MaxDepth==0,"Original endpoints were sorted");
    const std::array<D3D11_VIEWPORT,7> valid={{
        {0,0,1280,720,0,1}, {10.25f,-5.5f,320.5f,240.75f,.125f,.875f},
        {-32768,-32768,65535,65535,0,1}, {32767,32767,0,0,0,0},
        {0,0,0,1,1,1}, {0,0,1,0,.5f,.5f}, {-0.0f,-0.0f,1,1,-0.0f,1}}};
    for(const auto& value:valid) {
        const auto retained=backend.setViewport(value);
        require(same(retained,value),"Setter did not return the retained viewport");
        auto actual=query.viewport();require(actual && same(*actual,value),"Actual viewport query mismatch");
        auto copied=*actual;copied.Width=123;
        require(copied.Width==123,"Returned viewport copy could not be modified locally");
        require(same(*query.viewport(),value),"Returned viewport is not an independent value snapshot");
    }
    const D3D11_VIEWPORT stable{4.25f,8.5f,128,256,.25f,.75f};
    backend.setViewport(stable);
    require(!backend.scissor(),"Fresh native context has an unexpected scissor");
    const std::array<uint32_t,4> border={1,1,1023,1023};backend.setScissor(border);
    require(backend.scissor() && *backend.scissor()==border && same(*query.viewport(),stable),"Native shadow border rectangle or viewport retention differs");
    for(const auto& bad:std::array<std::array<uint32_t,4>,4>{{{1,1,1,1023},{1,5,1023,4},{0,0,32768,2},{0xFFFFFFFF,0,3,3}}}) {
        bool failed=false;try{backend.setScissor(bad);}catch(const Error&){failed=true;}
        require(failed && backend.scissor() && *backend.scissor()==border,"Rejected native scissor changed actual rectangle");
    }
    require(!second.scissor(),"Scissor changed another native device");
    const std::array<float D3D11_VIEWPORT::*,6> members={&D3D11_VIEWPORT::TopLeftX,&D3D11_VIEWPORT::TopLeftY,
        &D3D11_VIEWPORT::Width,&D3D11_VIEWPORT::Height,&D3D11_VIEWPORT::MinDepth,&D3D11_VIEWPORT::MaxDepth};
    for(auto member:members) for(float bad:{std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
        auto value=stable;value.*member=bad;reject(backend,value);
    }
    for(auto member:{&D3D11_VIEWPORT::Width,&D3D11_VIEWPORT::Height}) {
        auto value=stable;value.*member=-1;reject(backend,value);
        value.*member=std::numeric_limits<float>::max();reject(backend,value);
    }
    for(auto member:{&D3D11_VIEWPORT::TopLeftX,&D3D11_VIEWPORT::TopLeftY}) {
        auto value=stable;value.*member=std::nextafter(-32768.0f,-INFINITY);reject(backend,value);
        value.*member=std::nextafter(32767.0f,INFINITY);reject(backend,value);
    }
    reject(backend,{32767,0,1,1,0,1});reject(backend,{0,32767,1,1,0,1});
    // The float sum rounds down to the boundary; wider validation must still
    // reject a mathematically out-of-bounds right/bottom edge.
    reject(backend,{32766.5f,0,.5001f,1,0,1});reject(backend,{0,32766.5f,1,.5001f,0,1});
    for(auto member:{&D3D11_VIEWPORT::MinDepth,&D3D11_VIEWPORT::MaxDepth}) {
        auto value=stable;value.*member=std::nextafter(0.0f,-1.0f);reject(backend,value);
        value.*member=std::nextafter(1.0f,2.0f);reject(backend,value);
    }
    reject(backend,originalReverse,"reversed depth");reject(backend,{0,0,1,1,.75f,.25f},"reversed depth");
    auto unrelated=second.setViewport({0,0,64,32,0,1});
    require(same(*query.viewport(),stable) && same(*second.viewport(),unrelated),"Viewport ownership crossed native devices");

    // Real target selection does not normalize or clip the native viewport.
    auto target=backend.createTarget(16,16,TargetFormat::RGBA8);
    backend.bindTargets({target,nullptr,nullptr,nullptr},nullptr);
    require(same(*query.viewport(),stable),"Target selection changed native viewport state");
    backend.setViewport(stable);require(same(*query.viewport(),stable),"Native viewport was silently clamped to target bounds");
    backend.bindTargets({},nullptr);require(same(*query.viewport(),stable),"Unbinding targets changed viewport state");
    std::atomic<unsigned> rejected{};
    std::thread other([&] {
        try {backend.setViewport({0,0,1,1,0,1});} catch(const Error&) {++rejected;}
        try {(void)query.viewport();} catch(const Error&) {++rejected;}
    });other.join();require(rejected==2,"Viewport setter/query accepted a foreign owner thread");
    require(same(*query.viewport(),stable),"Foreign-thread attempt changed viewport state");
    backend.clearBindings();
    require(!backend.scissor(),"Scissor getter returned a cached rectangle after ClearState");
    require(!query.viewport(),"Query returned cached viewport after actual ClearState");
    require(same(*second.viewport(),unrelated),"ClearState changed another device");
    backend.setViewport(valid[0]);require(same(*query.viewport(),valid[0]),"Viewport could not be rebound after ClearState");
    require(!backend.screenDrawCount() && !backend.presentationCount() && !second.screenDrawCount() && !second.presentationCount(),
            "Viewport tests submitted or presented a frame");
}
}

int main(int argc,char** argv) {
    try {
        if(argc<2 || argc>3 || (argc==3 && std::string(argv[2])!="--hardware"))
            throw Error("Usage: NativeViewportTests original-flat-image [--hardware]");
        originalEvidence(argv[1]);nativeTests(argc==3);
        std::printf("PASS: native viewport %s / %zu checks; original evidence + actual D3D11 set/query; no draws\n",
                    argc==3?"hardware":"WARP",checks);
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL after %zu checks: %s\n",checks,error.what());return 1;}
}
