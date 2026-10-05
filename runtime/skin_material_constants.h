#pragma once
#include "renderer/native_backend.h"
#include "skin_profile.h"
#include <bit>
#include <span>
#include <stdexcept>

namespace Simpsons {
struct SkinMaterialRow {uint32_t leaf,word,reg;};
inline std::span<const SkinMaterialRow> skinPixelMaterialRows(uint32_t source,bool alpha=false) {
    const auto profile=skinProfile(source);
    static constexpr SkinMaterialRow alphaRows[]={{14,80,49}};
    static constexpr SkinMaterialRow glossOpaque[]={{13,76,50},{14,80,49},{83,1124,47},{86,1160,46},{87,1164,45},{88,1168,44}};
    static constexpr SkinMaterialRow flipbookOpaque[]={{14,80,49},{87,1164,46}};
    static constexpr SkinMaterialRow dualUvRows[]={{14,80,49},{24,120,42},{93,1200,48}};
    if(profile.gloss)return alpha?std::span<const SkinMaterialRow>(alphaRows):std::span<const SkinMaterialRow>(glossOpaque);
    if(profile.flipbook)return alpha?std::span<const SkinMaterialRow>(alphaRows):std::span<const SkinMaterialRow>(flipbookOpaque);
    if(profile.dualUv)return dualUvRows;
    // Original textured skin contexts5C20/6350 consume AlphaTestEnable in
    // both passes. Its two shadow rows occur only in the opaque context.
    static constexpr SkinMaterialRow texturedAlpha[]={{14,80,49},{16,88,48}};
    static constexpr SkinMaterialRow texturedOpaque[]={{14,80,49},{16,88,48},{84,1140,47},{85,1144,46}};
    if(profile.textured)
        return alpha?std::span<const SkinMaterialRow>(texturedAlpha):std::span<const SkinMaterialRow>(texturedOpaque);
    if(alpha)return alphaRows;
    static constexpr SkinMaterialRow opaque[]={{14,80,49},{16,88,39},{83,1136,47},{84,1140,32},{85,1144,33}};
    static constexpr SkinMaterialRow dual[]={{14,80,49},{86,1172,47},{87,1176,46}};
    return profile.dual?std::span<const SkinMaterialRow>(dual):std::span<const SkinMaterialRow>(opaque);
}
inline std::span<const SkinMaterialRow> skinVertexMaterialRows(uint32_t source,bool alpha=false) {
    const auto profile=skinProfile(source);(void)alpha;
    static constexpr SkinMaterialRow flipbookRows[]={{19,100,47}};
    static constexpr SkinMaterialRow dualUvRows[]={{19,100,47},{20,104,46},{21,108,45},{22,112,44},{23,116,43}};
    if(profile.flipbook)return flipbookRows;
    if(profile.dualUv)return dualUvRows;
    return {};
}
// Selected material rows supplement the original staged vertex bank. Managed
// ticker, morph weights, world matrices and bones remain with their own setters.
inline void projectSkinVertexMaterial(std::span<const uint32_t> words,
    std::span<const uint8_t,128> filtered,Graphics::SkinVertexConstants& accumulated,uint32_t source,bool alpha=false) {
    const auto profile=skinProfile(source);
    if(words.size()!=profile.words)throw std::invalid_argument("Skin private vertex bank differs from its source profile");
    for(const auto& row:skinVertexMaterialRows(source,alpha)) {
        if(filtered[row.leaf/8]&(0x80>>(row.leaf&7)))
            for(size_t lane=0;lane<4;++lane)
                accumulated[row.reg][lane]=std::bit_cast<float>(words[row.word+lane]);
    }
}
// A zero matrix lane is data, not an unset marker. Only the palette range is
// material-owned; earlier staging rows and the bank tail stay inherited.
inline void applySkinBonePalette(Graphics::SkinVertexConstants& target,
    const Graphics::SkinVertexConstants& palette) {
    for(size_t row=52;row<52+3*64;++row)target[row]=palette[row];
}
// The original 826FDBE0 setter has already transposed each source matrix into
// four private vectors. The constant upload copies the first THREE vectors;
// transposing again here drops translations and rotates articulated parts.
inline void projectSkinBoneMatrices(std::span<const uint32_t> words,
    std::span<const uint8_t,128> modified,Graphics::SkinVertexConstants& palette,uint32_t source) {
    const auto profile=skinProfile(source);
    if(words.size()!=profile.words)throw std::invalid_argument("Skin private bone bank differs from its source profile");
    for(uint32_t bone=0;bone<64;++bone) {
        const auto leaf=profile.boneLeaf+bone;if(!(modified[leaf/8]&(0x80>>(leaf&7))))continue;
        const auto base=profile.boneWord+16*bone;
        for(uint32_t row=0;row<3;++row)for(uint32_t lane=0;lane<4;++lane)
            palette[52+3*bone+row][lane]=std::bit_cast<float>(words[base+4*row+lane]);
    }
}
// Private leaves 14->c49(word80), 16->c39(88), 83->c47(1136),
// 84->c32(1140), 85->c33(1144). Leaf15->c40 stays inherited like rigid.
inline void projectSkinMaterial(std::span<const uint32_t> words,
    std::span<const uint8_t,128> filtered,Graphics::SkinPixelConstants& accumulated,uint32_t source=0x82006348,bool alpha=false) {
    const auto profile=skinProfile(source);
    if(words.size()!=profile.words)throw std::invalid_argument("Skin private bank differs from its source profile");
    for(const auto& row:skinPixelMaterialRows(source,alpha)) {
        if(filtered[row.leaf/8]&(0x80>>(row.leaf&7)))
            for(size_t lane=0;lane<4;++lane)
                accumulated[row.reg][lane]=std::bit_cast<float>(words[row.word+lane]);
    }
}
}
