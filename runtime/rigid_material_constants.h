#pragma once
#include "renderer/native_backend.h"
#include "rigid_profile.h"
#include <bit>
#include <span>
#include <stdexcept>

namespace Simpsons {
struct RigidMaterialRow {uint32_t leaf=0xFFFFFFFFu,word=0xFFFFFFFFu,reg=0xFFFFFFFFu;};
inline std::array<RigidMaterialRow,6> rigidPixelMaterialRows(uint32_t source,bool alpha=false) {
    if(source==0x82039208)return alpha?
        std::array<RigidMaterialRow,6>{{{14,80,49},{},{},{},{},{}}}:
        std::array<RigidMaterialRow,6>{{{14,80,49},{20,116,46},{},{},{},{}}};
    if(source==0x82042F58)return alpha?
        std::array<RigidMaterialRow,6>{{{14,80,49},{16,88,48},{},{},{},{}}}:
        std::array<RigidMaterialRow,6>{{{14,80,49},{20,104,44},{21,108,43},{},{},{}}};
    // Original selected context maps; the alpha shaders do not consume the
    // front pass's shadow, noise, normal texture or rim material rows.
    if(alpha&&source==0x82019988)return {{{14,80,49},{13,76,50},{19,124,47},{},{},{}}};
    if(alpha&&source==0x820547E8)return {{{14,80,49},{},{},{},{},{}}};
    if(alpha&&source==0x82057E08)return {{{14,80,49},{13,76,50},{20,140,47},{},{},{}}};
    // Chocolate's alpha pass reads four material vectors, not the ordinary
    // palette/custom-lines registers. These are the original context3AD0 maps.
    if(source==0x8205D2D8)return alpha?
        std::array<RigidMaterialRow,6>{{{13,76,50},{20,104,44},{21,108,43},{22,112,42},{},{}}}:
        std::array<RigidMaterialRow,6>{{{14,80,49},{20,104,44},{},{},{},{}}};
    if(source==0x82051808)return alpha?
        std::array<RigidMaterialRow,6>{{{14,80,49},{},{},{},{},{}}}:
        std::array<RigidMaterialRow,6>{{{14,80,49},{20,128,47},{},{},{},{}}};
    const auto p=rigidProfile(source);
    return {{{14,80,49},{p.shadowLeaf,p.shadowWord,p.shadowReg},
        {p.rimLeaf,p.rimWord,p.rimReg},{p.scaleLeaf,p.scaleWord,p.scaleReg},
        {p.exponentLeaf,p.exponentWord,p.exponentReg},{p.extraLeaf,p.extraWord,p.extraReg}}};
}
inline std::array<RigidMaterialRow,5> rigidVertexMaterialRows(uint32_t source,bool alpha=false) {
    if(source==0x82039208)return {{{17,92,47},{},{},{},{}}};
    if(source==0x82042F58)return {{{17,92,47},{18,96,46},{19,100,45},{},{}}};
    if(alpha&&isRigidFamilyAlphaSource(source))return {};
    if(source==0x820465E8)return {{{17,92,47},{18,96,46},{19,100,45},{20,104,44},{21,108,43}}};
    if(source==0x8205D2D8)return {{{17,92,47},{18,96,46},{19,100,45}}};
    if(rigidProfile(source).multitone)return {{{21,144,46},{},{}}};
    return {};
}
// 826B2F20, with F+120/F+124 == 1, filters two 16-byte vectors.
// 82C1DBA0 then consumes the selected maps and clears all 128 dirty bytes.
inline void filterRigidDirty(std::span<uint8_t,128> dirty,std::span<const uint8_t,128> mask) {
    for(size_t i=0;i<32;++i)dirty[i]&=mask[i];
}
// Unlike the vectorized filter, each of 82C1DBA0's eight shared-category
// loops reads ONE 64-bit dirty word when F+124 == 1. Zero intersected with
// any union category map is zero; no union map contents need to be invented.
inline bool rigidSharedCommitHasNoWork(std::span<const uint8_t,128> filtered,uint32_t words) {
    if(words!=1)return false;
    for(size_t i=0;i<8;++i)if(filtered[i])return false;
    return true;
}
inline void projectRigidMaterial(std::span<const uint32_t> words,
    std::span<const uint8_t,128> filtered,Graphics::RigidPixelConstants& accumulated,uint32_t source=0x8200CCB8,bool alpha=false) {
    const auto profile=rigidProfile(source);
    if(words.size()!=profile.words)throw std::invalid_argument("Rigid private bank differs from its source profile");
    // Pass2620: the remaining material maps after typed reflection removes
    // per-object parameters. Absence of a dirty bit preserves the LAST commit.
    // Register targets are pinned per material PS: rigid/textured/dual read
    // shadow c47, rim c46, custom lines c49; gloss additionally reads its
    // specular scale c47, shadow c46, rim c45 and specular exponent c50.
    // Multitone reads noise c47/c46/c45; normalmap reads c47/c46/c45/c44 plus
    // exponent c50 (leaf13) and custom c49 (leaf14).
    for(const auto& row:rigidPixelMaterialRows(source,alpha))if(row.leaf!=0xFFFFFFFFu) {
        if(filtered[row.leaf/8]&(0x80>>(row.leaf&7)))
            for(size_t lane=0;lane<4;++lane)
                accumulated[row.reg][lane]=std::bit_cast<float>(words[row.word+lane]);
    }
}
inline void projectRigidVertexMaterial(std::span<const uint32_t> words,
    std::span<const uint8_t,128> filtered,Graphics::RigidVertexConstants& accumulated,uint32_t source,bool alpha=false) {
    const auto profile=rigidProfile(source);
    if(words.size()!=profile.words)throw std::invalid_argument("Rigid private bank differs from its source profile");
    for(const auto& row:rigidVertexMaterialRows(source,alpha))if(row.leaf!=0xFFFFFFFFu)
        if(filtered[row.leaf/8]&(0x80>>(row.leaf&7)))
            for(size_t lane=0;lane<4;++lane)accumulated[row.reg][lane]=std::bit_cast<float>(words[row.word+lane]);
}
}
