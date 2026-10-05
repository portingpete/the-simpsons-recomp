#pragma once
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include "rigid_profile.h"
#include "skin_profile.h"

namespace Simpsons {
// Source identity comes from the current packet's typed owner, not from the
// previously selected shader. The caller supplies checked guest reads and the
// live host registry; this header has no dependency on guest memory or D3D11.
struct RigidPacketRecord {
    uint32_t identity{},source{},manager{},wrapper{},context{};
};
struct RigidPacketOwner {
    uint32_t packet{},typed{},vtable{},manager{},wrapper{},identity{},context{},source{},packetContext{};
};
template<class ReadWord,class FindRecord>
RigidPacketOwner resolveRigidPacketOwner(uint32_t packet,ReadWord read,FindRecord find) {
    RigidPacketOwner result{};result.packet=packet;
    RigidPacketRecord live{};bool haveLive=false;
    const char* step="packet.typed";
    auto field=[&](uint32_t address,uint32_t offset) {
        if(!address||uint64_t(address)+offset+4>0x100000000ull)
            throw std::invalid_argument("null or overflowing guest field");
        return read(address+offset);
    };
    try {
        result.typed=field(packet,0x18);
        step="packet.context";result.packetContext=field(packet,0x14);
        step="typed.vtable";result.vtable=field(result.typed,0);
        step="typed.manager";result.manager=field(result.typed,0x10);
        step="typed.wrapper";result.wrapper=field(result.typed,0x18);
        step="typed.identity";result.identity=field(result.typed,0x1C);
        step="record.lookup";live=find(result.identity);haveLive=true;
        step="record.association";
        const bool skin=isSkinSource(live.source);
        // Rigid-family packets carry their live creation context at +0x14.
        // Skin packets occur both with that context and with zero; zero falls
        // back to the live effect record's creation context.
        if(!result.identity||!live.source||!result.manager||!result.wrapper||!live.context||
           live.identity!=result.identity||live.manager!=result.manager||live.wrapper!=result.wrapper||
           (skin ? (result.packetContext&&result.packetContext!=live.context) : live.context!=result.packetContext))
            throw std::invalid_argument("packet/typed owner does not match the live effect record");
        result.context=live.context;
        step="typed.vtable-profile";
        if(result.vtable!=(skin?0x82061714u:isVfxRigidSource(live.source)?0x82061758u:0x820616C0u))
            throw std::invalid_argument("effect source does not match the rigid/skin typed vtable");
        result.source=live.source;
        // The wrapper's mutable fields are still checked by beginRigid and
        // requireRigidSelection before GPU work. No unreadable wrapper is
        // replaced, fabricated, or inferred from the previous selection here.
        return result;
    } catch(const std::exception& error) {
        char message[768];
        std::snprintf(message,sizeof(message),
            "Rigid packet owner step=%s packet=%08X typed=%08X vtable=%08X manager=%08X wrapper=%08X identity=%08X packet_context=%08X context=%08X live=%u/%08X/%08X/%08X/%08X/%08X: %s",
            step,result.packet,result.typed,result.vtable,result.manager,result.wrapper,result.identity,result.packetContext,result.context,
            haveLive?1u:0u,live.source,live.identity,live.manager,live.wrapper,live.context,error.what());
        throw std::runtime_error(message);
    }
}
inline bool rigidFallbackArgumentsQualified(uint32_t source,uint32_t r4,uint32_t r5) {
    if(isVfxRigidSource(source))return r4<=1&&r5<=1;
    // CCB8 uses the same original dispatcher/fallback as the other qualified
    // rigid sources. r4 selects its distinct opaque/alpha pass; metadata bit1
    // supplies r5, which 82740120 overwrites from packet.object before use.
    // Both original Boolean values are therefore valid for either pass.
    if(source==0x8200CCB8u)return r4<=1&&r5<=1;
    // Original82740680 derives caller flags (r23/r21) from Boolean metadata
    // selectors and passes them at82740B1C/82740B18. The pass uses r4; incoming
    // r5 is overwritten by packet.object at82740120 before its first use.
    // Ordinary dual-textured packets occur with both alpha (1,0) and (1,1);
    // the incoming r5 flag is not the selected begin technique.
    // Downstream mesh/material/commit/draw sites re-validate everything.
    const bool alphaContent=(source==0x82036448u||source==0x8205D2D8u||source==0x82051808u||source==0x820168F8u||source==0x8202AD78u||source==0x82039208u||source==0x82042F58u||source==0x820465E8u||isRigidFamilyAlphaSource(source));
    // Both Boolean flags are valid for the qualified opaque/alpha passes,
    // including gloss, multitone and normalmap's separate alpha shaders.
    const bool alphaEntry=alphaContent;
    return (!r4||(alphaContent&&r4==1))&&(!r5||(alphaEntry&&r5==1));
}
}
