#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

// Original material records that NativeBackend::createMaterialArtifact turns
// into real native shader objects. Tests pin this exact population instead of
// deriving it from the compiler; add an entry only with its qualified artifact.
inline constexpr std::array<uint32_t,108> nativeTranslatedMaterials={
    0x821524C8,0x821525E8,0x82152708,0x82152880,             // Screen flat/textured
    0x820B8F08,0x820B90D4,0x82152B68,                        // Four-tap, movie PS
    0x8202E6F0,0x8202E840,0x820301A0,0x820302EC,0x820347B0,0x82034900, // Edge, AA, edgeAA
    0x820C2FA0,0x820C1E6C,0x820CA530,                        // Shadow depth/alpha
    0x82120C04,0x82121BE8,0x82122BD4,0x82122D38,             // Mono
    0x8214A8A4,0x8214B9B8,                                   // Z-prepass
    0x8200D3AC,0x8200D9E8,0x8201700C,0x82017658,0x8202B4CC,0x8202BB44,
    0x8202B884,0x8202C3BC,0x8201A07C,0x8201A6EC,0x82054F2C,0x82055710,
    0x8205855C,0x82058CBC,                                   // Rigid families
    0x8201A430,0x8201B0BC,0x82055440,0x820560B8,0x820589EC,0x82059880, // Gloss/multitone/normalmap alpha
    0x82046D9C,0x820477A8,0x82047318,0x82047F44,             // Animated dual UV opaque/alpha
    0x8204364C,0x82044068,0x82043BC0,0x82044850,             // Animated single UV opaque/alpha
    0x820398BC,0x8203A24C,0x82039D70,0x8203A644,             // Flipbook opaque/alpha
    0x82007C1C,0x8200A02C,0x82008E20,0x8200A4A4,0x8201E6DC,0x82020B9C, // Skin
    0x8201F984,0x82021344,                                   // Dual skin alpha
    0x8201146C,0x82013900,0x820126EC,0x82013F8C,              // Textured skin opaque/alpha
    0x82036F08,0x820374E8,0x8205E0B8,0x8205EED4,0x8200D734,0x8200E1BC,
    0x82036C2C,0x820371EC,                                   // Sky opaque exact records
    0x8205BE70,0x8205C100,0x8201739C,0x82017E4C,             // Sky, chocolate, alpha, VFX, 168F8
    0x8205DABC,0x8205E5E0,0x82051EEC,0x82052610,0x82052358,0x82052DF4, // Chocolate opaque/projected rigid
    0x8202579C,0x82027C18,0x820269E8,0x82028258,             // Gloss skin
    0x8203D93C,0x82040118,0x8203ED38,0x82040510,             // Flipbook skin
    0x8204BA4C,0x8204E2DC,0x8204CE90,0x8204E75C,             // Animated dual UV skin
    0x82153278,0x82153460,0x821536F0,                        // Corona
    0x821511D8,0x821509D8,0x82150B18,                        // Immediate
    0x821513B8,0x82150C98,0x82150F18,                        // Projected immediate
    0x821538E8,0x82153B88,0x82153C80,                        // Radial
};
static_assert([] {
    for(size_t i=0;i<nativeTranslatedMaterials.size();++i) {
        if(!nativeTranslatedMaterials[i]) return false; // Catches a short initializer.
        for(size_t j=0;j<i;++j) if(nativeTranslatedMaterials[i]==nativeTranslatedMaterials[j]) return false;
    }
    return true;
}(),"Translated material list must be complete and unique");
inline bool nativeTranslatedMaterial(uint32_t address) {
    return std::find(nativeTranslatedMaterials.begin(),nativeTranslatedMaterials.end(),address)!=nativeTranslatedMaterials.end();
}
