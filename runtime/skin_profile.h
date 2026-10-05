#pragma once
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace Simpsons {
struct SkinProfile {
    uint32_t vertex,pixel,context,samplers,words;
    uint32_t boneArray,boneLeaf,boneWord,leaves,stride;
    bool dual;
    // Textured skin has one UV stream like base skin, but its opaque pass
    // consumes a character shadow at t0 and a material base texture at t1.
    bool textured=false;
    bool gloss=false;
    bool flipbook=false;
    bool dualUv=false;
};
inline bool isSkinSource(uint32_t source) {
    return source==0x82006348||source==0x8201CD48||source==0x8200FB98||
        source==0x82023E58||source==0x8203C008||source==0x8204A058;
}
inline SkinProfile skinProfile(uint32_t source) {
    if(source==0x82006348)return {0x82007C1C,0x8200A02C,0x5830,0,1148,0x00580024,18,96,86,48,false};
    if(source==0x8201CD48)return {0x8201E6DC,0x82020B9C,0x5F50,12,1188,0x005C0026,19,100,90,56,true};
    if(source==0x8200FB98)return {0x8201146C,0x82013900,0x5C20,12,1148,0x005C0026,19,100,86,48,false,true};
    if(source==0x82023E58)return {0x8202579C,0x82027C18,0x5D80,6,1172,0x005C0026,19,100,89,56,false,false,true};
    if(source==0x8203C008)return {0x8203D93C,0x82040118,0x5DD0,6,1168,0x0064002A,21,108,88,56,false,false,false,true};
    if(source==0x8204A058)return {0x8204BA4C,0x8204E2DC,0x6570,12,1204,0x00780034,26,128,94,56,false,false,false,false,true};
    char message[160];
    std::snprintf(message,sizeof(message),
        "Unqualified skin source profile: source=%08X; no match among 82006348,8201CD48,8200FB98,82023E58,8203C008,8204A058",
        static_cast<unsigned>(source));
    throw std::invalid_argument(message);
}
inline bool skinConsumesUV1(uint32_t source,bool alpha=false) {
    const auto profile=skinProfile(source);
    return profile.dual||profile.dualUv||(!alpha&&(profile.gloss||profile.flipbook));
}
struct SkinAlphaPass {uint32_t vertex,pixel,context,samplers;};
inline SkinAlphaPass skinAlphaPass(uint32_t source) {
    if(source==0x82006348)return {0x82008E20,0x8200A4A4,0x5F60,6};
    if(source==0x8201CD48)return {0x8201F984,0x82021344,0x66C0,6};
    if(source==0x8200FB98)return {0x820126EC,0x82013F8C,0x6350,6};
    if(source==0x82023E58)return {0x820269E8,0x82028258,0x64E0,6};
    if(source==0x8203C008)return {0x8203ED38,0x82040510,0x6520,6};
    if(source==0x8204A058)return {0x8204CE90,0x8204E75C,0x6D20,12};
    char message[160];
    std::snprintf(message,sizeof(message),
        "Unqualified skin alpha pass: source=%08X; no match among 82006348,8201CD48,8200FB98,82023E58,8203C008,8204A058",
        static_cast<unsigned>(source));
    throw std::invalid_argument(message);
}
}
