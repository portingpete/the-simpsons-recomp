#include "auto_defeat_loc_enemies.h"

#include "engine_cpu_calls.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_set>

namespace Simpsons {
namespace {

constexpr uint32_t kRegistryRoot = 0x82D09748;
constexpr uint32_t kNpcRegistryGroup = 7;
constexpr uint32_t kNpcVtable = 0x82189B50;
constexpr uint32_t kDamageComponentVtable = 0x8218AB98;
constexpr uint32_t kTeamTypeSlot = 0x82E07C70;
constexpr uint32_t kGetComponent = 0x8269ADC0;
constexpr uint32_t kSendDamage = 0x82938660;
constexpr uint32_t kHostileTeam = 3; // TeamNPC; shared by melee enemies and the white rabbit.
constexpr uint32_t kDamageType = 1;
constexpr double kLethalDamage = 1000.0;

// These are the 24 NPCMelee SimG instance IDs in Land of Chocolate's
// Story_Mode_Design GRAPH. Original 0x826A31F0 copies each 16-byte ID into
// NPCBase +0x1C. The separate NPCNinja/white-guide ID is intentionally absent.
constexpr std::array<std::array<uint32_t, 3>, 24> kMeleeIds{{
    {0x444D61C4, 0x71E52191, 0x3637A706},
    {0x447E2581, 0xF6121A88, 0x74398A9B},
    {0x4B6418A2, 0x69E469AD, 0xF0FFF867},
    {0x42118873, 0x19B7CBBF, 0x07396E57},
    {0x46B00B87, 0x530C49BB, 0x8EFD926B},
    {0x4822ED21, 0xA79F759B, 0x5C7A30E3},
    {0x4A57ABD4, 0x302D22BD, 0x88FFC232},
    {0x4F4F39DD, 0x59FC499A, 0xBF5C1B09},
    {0x4C13B30F, 0x9D03C58C, 0x1A413B2F},
    {0x4045BB13, 0xD56B8E90, 0x937FE92B},
    {0x4D4867AA, 0x3B7369A4, 0xFBC053D7},
    {0x46BE97F0, 0xC28FA2AE, 0xDE230CE3},
    {0x464F3DF7, 0x76871F9C, 0x3DAD5ABD},
    {0x41AA4ECE, 0xCB34E8BA, 0x356A7625},
    {0x4B33D9B8, 0x2DA770BA, 0xB715B852},
    {0x419D5886, 0x17433987, 0xA1B4BF18},
    {0x4EA178F3, 0x522B85B5, 0x01D7A7AC},
    {0x42CCDDFE, 0x46EF8EA7, 0x05BB6686},
    {0x4AEE37E7, 0xC0078599, 0x0DBFBFF6},
    {0x42958526, 0x933903AE, 0x02A2FEF3},
    {0x41343EFA, 0x8B8FBD84, 0x09CB8ADE},
    {0x411BA1A0, 0x91F697B2, 0xF7944188},
    {0x4D3BA737, 0xAE088CB1, 0xA91CB9ED},
    {0x4ECA1127, 0xE0B52D96, 0x0C4F6EBB},
}};

// The registry is original guest state. A stale or partially initialized
// entry must not turn this optional feature into a host-side crash.
bool readWord(Runtime& runtime, uint32_t address, uint32_t& result) {
    if (address < 0x10000 || address > 0xFFFFFFFCu || (address & 3)) return false;
    try {
        uint32_t raw{};
        std::memcpy(&raw, runtime.pointer(address, 4, false), sizeof(raw));
        result = __builtin_bswap32(raw);
        return true;
    } catch (const Failure&) {
        return false;
    }
}

bool readField(Runtime& runtime, uint32_t object, uint32_t offset, uint32_t& result) {
    if (object > 0xFFFFFFFFu - offset) return false;
    return readWord(runtime, object + offset, result);
}

bool readFloat(Runtime& runtime, uint32_t object, uint32_t offset, float& result) {
    uint32_t bits{};
    if (!readField(runtime, object, offset, bits)) return false;
    result = std::bit_cast<float>(bits);
    return std::isfinite(result);
}

bool isMeleeEnemy(Runtime& runtime, uint32_t actor, std::array<uint32_t, 4>& id) {
    for (uint32_t i = 0; i < id.size(); ++i)
        if (!readField(runtime, actor, 0x1C + 4 * i, id[i])) return false;
    if (id[0] != 0x222D3842) return false;
    for (const auto& known : kMeleeIds)
        if (id[1] == known[0] && id[2] == known[1] && id[3] == known[2]) return true;
    return false;
}

} // namespace

void autoDefeatLocEnemiesFrame(PPCContext& ctx, uint8_t* base) {
    Runtime* runtime = active;
    if (!runtime || runtime->base != base || !runtime->autoDefeatLocEnemies ||
        !runtime->landOfChocolateStreamSeen.load(std::memory_order_acquire)) return;

    uint32_t manager{}, table{}, head{};
    if (!readWord(*runtime, kRegistryRoot, manager) || !manager ||
        !readField(*runtime, manager, 4, table) || !table ||
        !readField(*runtime, table, 28 * kNpcRegistryGroup + 8, head)) return;

    // One damage message at most per frame. Its original listener may remove
    // the NPC from this list, so do not follow any node after sending it.
    static std::unordered_set<uint32_t> loggedActors;
    for (uint32_t node = head, traversed = 0; node && traversed < 4096; ++traversed) {
        uint32_t actor{}, next{}, vtable{};
        if (!readField(*runtime, node, 0, actor) ||
            !readField(*runtime, node, 4, next)) return;
        node = next;
        if (!actor || !readField(*runtime, actor, 0, vtable) || vtable != kNpcVtable) continue;

        // Check the exact graph identity before calling any original guest
        // service. The white story rabbit is also in this NPC registry.
        std::array<uint32_t, 4> instanceId{};
        if (!isMeleeEnemy(*runtime, actor, instanceId)) continue;

        uint32_t component{}, componentVtable{}, owner{}, flags{}, mask{}, policy{};
        float health{};
        if (!readField(*runtime, actor, 0x158, component) || !component ||
            !readField(*runtime, component, 0, componentVtable) || componentVtable != kDamageComponentVtable ||
            !readField(*runtime, component, 8, owner) || owner != actor ||
            !readField(*runtime, component, 0x1C, flags) ||
            !readField(*runtime, component, 0x4C, mask) ||
            !readField(*runtime, component, 0x50, policy) ||
            !readFloat(*runtime, component, 0x10, health)) continue;

        uint32_t teamType{};
        if (!readWord(*runtime, kTeamTypeSlot, teamType) || !teamType) return;
        EngineCpuCalls cpu(ctx, base);
        const uint32_t teamComponent = cpu.invoke(kGetComponent, actor, teamType);
        uint32_t team{};
        if (!teamComponent || !readField(*runtime, teamComponent, 0x18, team)) continue;

        const bool eligible = team == kHostileTeam && health > 0.0f &&
                              !(flags & 1) && (mask & 1) && policy == 1;
        if (loggedActors.size() < 32 && loggedActors.insert(actor).second) {
            std::fprintf(stderr,
                         "[AUTO DEFEAT LOC] NPC actor=%08X id=%08X-%08X-%08X-%08X team=%u health=%g eligible=%u\n",
                         actor, instanceId[0], instanceId[1], instanceId[2], instanceId[3],
                         team, double(health), unsigned(eligible));
        }
        if (!eligible) continue;

        uint32_t transform{};
        float x{}, y{}, z{};
        if (!readField(*runtime, actor, 0x8C, transform) || !transform ||
            !readFloat(*runtime, transform, 0x40, x) ||
            !readFloat(*runtime, transform, 0x44, y) ||
            !readFloat(*runtime, transform, 0x48, z)) continue;

        // The original sender consumes the current transform's XYZ directly.
        // It copies them into DamageData before dispatching the damage event.
        cpu.registers().r3.u64 = 0;
        cpu.registers().r4.u64 = actor;
        cpu.registers().r5.u64 = transform + 0x40;
        cpu.registers().r6.u64 = 0;
        cpu.registers().r7.u64 = kDamageType;
        cpu.registers().r8.u64 = 0;
        cpu.registers().r9.u64 = 0;
        cpu.registers().f1.f64 = kLethalDamage;
        cpu.invoke(kSendDamage);
        uint32_t afterVtable{}, afterOwner{};
        float afterHealth{};
        if (readField(*runtime, component, 0, afterVtable) &&
            afterVtable == kDamageComponentVtable &&
            readField(*runtime, component, 8, afterOwner) && afterOwner == actor &&
            readFloat(*runtime, component, 0x10, afterHealth)) {
            std::fprintf(stderr,
                         "[AUTO DEFEAT LOC] damage actor=%08X amount=%g health=%g->%g\n",
                         actor, kLethalDamage, double(health), double(afterHealth));
        } else {
            std::fprintf(stderr,
                         "[AUTO DEFEAT LOC] damage actor=%08X amount=%g health=%g->removed\n",
                         actor, kLethalDamage, double(health));
        }
        return;
    }
}

} // namespace Simpsons
