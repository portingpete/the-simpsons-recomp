#pragma once
#include <array>
#include <string_view>
namespace Simpsons {
inline constexpr std::array<std::string_view,18> auditStages={
    "spr_hub","loc","brt","eighty_bites","tree_hugger","mob_rules",
    "cheater","dayofthedolphins","colossaldonut","bargainbin",
    "bigsuperhappy","dayspringfieldstoodstill","gamehub","grand_theft_scratchy",
    "medal_of_homer","meetthyplayer","neverquest","rhymes"};
inline bool isAuditStage(std::string_view stage) {
    for(const auto known:auditStages)if(stage==known)return true;
    return false;
}
}
