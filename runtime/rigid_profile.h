#pragma once
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace Simpsons {
inline bool isVfxRigidSource(uint32_t source) {return source==0x8205B848;}
inline bool isRigidFamilyAlphaSource(uint32_t source) {
    return source==0x82019988||source==0x820547E8||source==0x82057E08;
}
inline bool isRigidFamilyAlphaPixel(uint32_t pixel) {
    return pixel==0x8201B0BC||pixel==0x820560B8||pixel==0x82059880;
}
struct RigidProfile {
    uint32_t vertex,pixel,context,samplers,words,shadowLeaf,rimLeaf,shadowWord,rimWord;
    bool textured;
    // Gloss-only material inputs and their pinned register targets. The
    // sentinel 0xFFFFFFFF means "this material has no such private leaf";
    // rigid/textured/dual read shadow c47/rim c46, while gloss reads scale
    // c47/shadow c46/rim c45 and additionally the specular exponent c50.
    // Normalmap reads shadow c47/rim c46/scale c45 plus exponent c50 (leaf13)
    // and an extra c44 (leaf23, g_NormalMapLight); custom c49 (leaf14) is
    // always projected when dirty.
    uint32_t scaleLeaf,scaleWord,exponentLeaf,exponentWord;
    uint32_t shadowReg,rimReg,scaleReg,exponentReg;
    bool gloss;
    bool multitone=false; // Noise leaves occupy the three material rows instead of shadow/rim/scale.
    bool normalmap=false;
    uint32_t extraLeaf=0xFFFFFFFFu,extraWord=0xFFFFFFFFu,extraReg=0xFFFFFFFFu;
    bool uv=false;
    bool singleUv=false; // Single animated UV, with two opaque shadow stages.
    bool flipbook=false; // Atlas animation, one base texture and no shadow stages.
    bool chocolate=false; // Opaque2 material stages after two depths; alpha3 material stages.
    bool projtex=false; // Opaque base2/projected3; alpha base0 only.
};
inline bool isRigidSource(uint32_t source) {
    return source==0x8200CCB8||source==0x820168F8||source==0x8202AD78||source==0x82039208||source==0x82042F58||source==0x820465E8||source==0x82019988||source==0x820547E8||source==0x82057E08||source==0x8205D2D8||source==0x82051808;
}
// Source membership never selects alpha; the pinned original low-byte
// selector chooses typed+A8 or typed+AC from the actual begin request.
inline bool isAlphaFamilySource(uint32_t) {return false;}
// Each qualified ordinary rigid source has separate opaque and alpha passes.
// Membership admits the technique; only the original begin request selects it.
inline bool isAlphaBeginSource(uint32_t source) {
    return source==0x82036448||source==0x8205D2D8||isRigidSource(source);
}
inline RigidProfile rigidProfile(uint32_t source) {
    if(isVfxRigidSource(source))return {0x8205BE70,0x8205C100,0x1510,6,104,
        0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,true,
        0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,
        0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,false};
    if(source==0x8200CCB8)return {0x8200D3AC,0x8200D9E8,0x2620,12,136,18,19,120,124,false,
        0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,47,46,0xFFFFFFFFu,0xFFFFFFFFu,false};
    if(source==0x820168F8)return {0x8201700C,0x82017658,0x27B0,18,140,19,20,124,128,true,
        0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,47,46,0xFFFFFFFFu,0xFFFFFFFFu,false};
    if(source==0x8202AD78)return {0x8202B4CC,0x8202BB44,0x2920,18,156,20,21,140,144,true,
        0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,47,46,0xFFFFFFFFu,0xFFFFFFFFu,false};
    // Retail flipbook: animation c47, reflected ticker c22, base t0.
    // Alpha retains its distinct world/eye program; neither pass samples depth.
    if(source==0x82039208)return {0x820398BC,0x8203A24C,0x2560,6,120,
        0xFFFFFFFFu,20,0xFFFFFFFFu,116,true,
        0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,
        0xFFFFFFFFu,46,0xFFFFFFFFu,0xFFFFFFFFu,false,false,false,
        0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,false,false,true};
    // Retail single UV: t0 world depth, t1 character depth, t2 base.
    // Its animation vectors and shadow/rim rows have their own register maps.
    if(source==0x82042F58)return {0x8204364C,0x82044068,0x2D90,18,132,
        20,21,104,108,true,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,
        44,43,0xFFFFFFFFu,0xFFFFFFFFu,false,false,false,
        0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,false,true};
    // Animated dual UV: two material textures and only character shadow t0.
    // Its five VS material vectors and texture flags have separate pass maps.
    if(source==0x820465E8)return {0x82046D9C,0x820477A8,0x30F0,18,172,
        0xFFFFFFFFu,27,0xFFFFFFFFu,168,false,
        22,112,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,48,42,0xFFFFFFFFu,
        false,false,false,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,true};
    if(source==0x82019988)return {0x8201A07C,0x8201A6EC,0x2B00,18,136,20,21,128,132,true,
        19,124,13,76,46,45,47,50,true};
    if(source==0x820547E8)return {0x82054F2C,0x82055710,0x2D40,24,152,20,21,140,144,true,
        22,148,0xFFFFFFFFu,0xFFFFFFFFu,47,46,45,0xFFFFFFFFu,false,true};
    if(source==0x82057E08)return {0x8205855C,0x82058CBC,0x3140,24,156,20,21,140,144,true,
        22,148,13,76,47,46,45,50,false,false,true,23,152,44};
    // The original selector chooses opaque or alpha from its request. Both
    // sky passes retain the same four samplers and depth-write0 policy.
    if(source==0x82036448)return {0x82036C2C,0x820371EC,0x2480,24,188,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,false,
        0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,false,false,false};
    if(source==0x8205D2D8) {
        RigidProfile p{0x8205DABC,0x8205E5E0,0x3780,24,184,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,false,
            0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,false};
        p.chocolate=true;return p;
    }
    if(source==0x82051808) {
        RigidProfile p{0x82051EEC,0x82052610,0x2740,24,132,20,0xFFFFFFFFu,128,0xFFFFFFFFu,true,
            0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,47,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,false};
        p.projtex=true;return p;
    }
    char message[192];
    std::snprintf(message,sizeof(message),
        "Unqualified rigid source profile: source=%08X; no match among 8200CCB8,820168F8,8202AD78,82039208,82042F58,820465E8,82019988,820547E8,82057E08,82036448,8205D2D8",
        static_cast<unsigned>(source));
    throw std::invalid_argument(message);
}
// The pass beginRigid qualifies for alpha-family sources. Sky/chocolate
// select the pass their profile already describes; textured rigid 0x820168F8
// selects a distinct 6-sampler rigidalpha pass (VS8201739C/PS82017E4C,
// ctx 0x2AB0, reach-game-217 packet 82D6EAF0). Dual-textured 0x8202AD78 has
// its own 6-sampler alpha pair (VS8202B884/PS8202C3BC, ctx 0x2C30). Gloss,
// multitone and normalmap also retain distinct original alpha records below.
// Source profiles keep front shaders and effect-level bank/material facts.
struct RigidAlphaPass {uint32_t vertex,pixel,context,samplers;};
inline RigidAlphaPass rigidAlphaPass(uint32_t source) {
    if(source==0x8200CCB8)return {0x8200D734,0x8200E1BC,0x2910,6};
    if(source==0x820168F8)return {0x8201739C,0x82017E4C,0x2AB0,6};
    if(source==0x8202AD78)return {0x8202B884,0x8202C3BC,0x2C30,6};
    if(source==0x82039208)return {0x82039D70,0x8203A644,0x2840,6};
    if(source==0x82042F58)return {0x82043BC0,0x82044850,0x30A0,6};
    if(source==0x820465E8)return {0x82047318,0x82047F44,0x3440,12};
    if(source==0x82019988)return {0x8201A430,0x8201B0BC,0x2DF0,6};
    if(source==0x820547E8)return {0x82055440,0x820560B8,0x3040,6};
    if(source==0x82057E08)return {0x820589EC,0x82059880,0x3450,6};
    if(source==0x82036448)return {0x82036F08,0x820374E8,0x27B0,24};
    if(source==0x8205D2D8)return {0x8205E0B8,0x8205EED4,0x3AD0,18};
    if(source==0x82051808)return {0x82052358,0x82052DF4,0x2A20,6};
    const auto profile=rigidProfile(source);
    return {profile.vertex,profile.pixel,profile.context,profile.samplers};
}
}
