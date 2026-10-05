#pragma once

namespace {
void originalFallbackBlendEvidence(uint8_t* base) {
    constexpr std::array<uint32_t,18> setup={0x2B1E0000,0x419A0060,0x38A00000,0x38800001,
        0x38600044,0x4BF77789,0x38A00001,0x38800001,0x38600006,0x4BF77779,0x38A00001,
        0x38800006,0x38600009,0x4BF77769,0x38A00001,0x38800007,0x3860000A,0x4BF77759};
    constexpr std::array<uint32_t,6> rigid={0x38C00000,0x891F000C,0x80E30010,0x80BF0018,
        0x809F0004,0x4BFC0F75};
    constexpr std::array<uint32_t,14> cleanup={0x2B1E0000,0x419A0030,0x38A00000,0x38800000,
        0x38600044,0x4BF776A5,0x578B063E,0x2B0B0000,0x419A0014,0x38A00001,0x38800001,
        0x38600003,0x4BF77689,0x38600000};
    for(uint32_t i=0;i<setup.size();++i)need(PPC_LOAD_U32(0x827401CC+4*i)==setup[i],
        "Original shared alpha blend setup changed");
    for(uint32_t i=0;i<rigid.size();++i)need(PPC_LOAD_U32(0x82740298+4*i)==rigid[i],
        "Original zero-bone draw branch changed");
    for(uint32_t i=0;i<cleanup.size();++i)need(PPC_LOAD_U32(0x827402B0+4*i)==cleanup[i],
        "Original shared alpha blend cleanup changed");
}
void originalInheritedBlend(const EngineDriver& driver,const char* message) {
    using S=Graphics::ScalarState;
    const auto& state=driver.effectiveState();
    need(state.scalar(S::BlendEnable)==1&&state.effectiveBlend(0)==0x07060706&&
         state.scalar(S::ExpandedBlend0)==0,message);
}
}
