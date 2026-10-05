#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Simpsons::Platform {
struct LocalProfile {
    std::string id;   // Canonical lowercase host GUID; never an Xbox XUID.
    std::string name; // Explicit native policy: printable ASCII, 1..15 bytes.
    bool operator==(const LocalProfile&) const = default;
};

struct LocalPlayerError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct LocalAchievement {uint32_t slot;uint32_t id;};

// Windows file-backed immutable profiles and four initially empty session slots.
// No guest writes, UI, Xbox identities, notifications, or implicit activation.
// Operations serialize within this owner; different owners have independent
// sessions. File sharing/exclusive publication also protect store operations.
class NativeLocalPlayers {
    friend struct NativeLocalPlayersTestAccess;
    struct State;
    std::unique_ptr<State> impl;
    // Same exclusive-publication path used by create; the friend fixture can
    // supply a fixed valid ID to exercise an actual destination collision.
    LocalProfile createWithId(const std::string& name, const std::string& id);
public:
    explicit NativeLocalPlayers(std::filesystem::path root);
    ~NativeLocalPlayers();
    NativeLocalPlayers(const NativeLocalPlayers&) = delete;
    NativeLocalPlayers& operator=(const NativeLocalPlayers&) = delete;
    NativeLocalPlayers(NativeLocalPlayers&&) = delete;
    NativeLocalPlayers& operator=(NativeLocalPlayers&&) = delete;

    std::vector<LocalProfile> list() const; // Sorted by ID; corrupt files reject.
    LocalProfile create(const std::string& name); // Durable before returning.
    LocalProfile load(const std::string& id) const;
    bool activate(uint32_t slot, const std::string& id);
    bool signOut(uint32_t slot);
    uint32_t state(uint32_t slot) const; // 0 empty, 1 active local profile only.
    // Stable native equality key from the durable GUID's final64 bits. Zero
    // means no active profile. Store/activation checks reject key collisions.
    // This is an offline native identity, not an Xbox account credential.
    uint64_t identity(uint32_t slot) const;
    std::optional<LocalProfile> profile(uint32_t slot) const;
    // Durable native unlocks, keyed by full profile GUID/title/achievement ID.
    // Returns newly created records; repeated unlocks validate existing bytes.
    size_t writeAchievements(uint32_t title,const std::vector<LocalAchievement>& records);
    bool hasAchievement(const std::string& profileId,uint32_t title,uint32_t id) const;
};
}
