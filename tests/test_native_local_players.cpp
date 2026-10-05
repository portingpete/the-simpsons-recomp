#include "runtime/native_local_players.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <thread>

namespace Simpsons::Platform {
struct NativeLocalPlayersTestAccess {
    static LocalProfile create(NativeLocalPlayers& owner, const std::string& name, const std::string& id) {
        return owner.createWithId(name, id);
    }
};
}
namespace {
using Simpsons::Platform::LocalPlayerError;
using Simpsons::Platform::LocalProfile;
using Simpsons::Platform::NativeLocalPlayers;
using Simpsons::Platform::NativeLocalPlayersTestAccess;
namespace fs = std::filesystem;
std::atomic<size_t> checks{0};
void need(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const char* message) {
    bool rejected = false;
    try { action(); } catch (const LocalPlayerError&) { rejected = true; }
    need(rejected, message);
}
struct Handle {
    HANDLE value;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
void write(const fs::path& file, const std::string& bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    need(bool(out), "Could not write owned profile fixture");
}
std::string read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    need(bool(in), "Could not read owned profile fixture");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
fs::path fileOf(const fs::path& root, const LocalProfile& profile) { return root/(profile.id + ".profile"); }

struct Tree {
    fs::path parent, root;
    std::vector<fs::path> junctions;
    Tree() {
        parent = fs::canonical(fs::temp_directory_path());
        root = parent/(L"simpsons-local-players-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                       std::to_wstring(GetTickCount64()));
        need(fs::create_directory(root), "Owned fixture root already exists");
    }
    ~Tree() {
        try {
            if (!root.is_absolute() || root.parent_path() != parent ||
                !root.filename().wstring().starts_with(L"simpsons-local-players-")) return;
            for (const auto& link : junctions) {
                const auto relative = link.lexically_relative(root);
                if (relative.empty() || relative.native().starts_with(L"..")) return;
                // Remove the reparse object itself before traversing anything.
                const DWORD attrs = GetFileAttributesW(link.c_str());
                if (attrs != INVALID_FILE_ATTRIBUTES && !RemoveDirectoryW(link.c_str())) return;
            }
            // The iterator yields an entry before increment could recurse into
            // it. Reject any unexpected reparse object before that increment.
            for (const auto& entry : fs::recursive_directory_iterator(root))
                if (GetFileAttributesW(entry.path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) return;
            fs::remove_all(root);
        } catch (...) { std::fprintf(stderr, "Local-player fixtures retained for inspection\n"); }
    }
    void junction(const fs::path& link, const fs::path& target) {
        need(fs::create_directory(link), "Cannot create owned junction directory");
        junctions.push_back(link);
        const std::wstring sub = L"\\??\\" + target.native(), print = target.native();
        const size_t subBytes = sub.size()*2, printBytes = print.size()*2;
        std::vector<unsigned char> data(16 + subBytes + 2 + printBytes + 2, 0);
        const auto put16 = [&](size_t at, size_t value) {
            need(value <= 65535, "Junction fixture framing overflow");
            const auto v = static_cast<uint16_t>(value);
            std::memcpy(data.data()+at, &v, sizeof(v));
        };
        const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
        std::memcpy(data.data(), &tag, sizeof(tag));
        put16(4, data.size()-8); put16(8, 0); put16(10, subBytes);
        put16(12, subBytes+2); put16(14, printBytes);
        std::memcpy(data.data()+16, sub.data(), subBytes);
        std::memcpy(data.data()+18+subBytes, print.data(), printBytes);
        Handle directory{CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
        need(directory.value != INVALID_HANDLE_VALUE, "Cannot open actual junction fixture");
        DWORD actual = 0;
        need(DeviceIoControl(directory.value, FSCTL_SET_REPARSE_POINT, data.data(),
            static_cast<DWORD>(data.size()), nullptr, 0, &actual, nullptr) != FALSE,
            "Cannot set actual NTFS junction");
    }
};

template<class F> void concurrent(size_t count, F work) {
    std::mutex errorMutex;
    std::exception_ptr error;
    std::vector<std::jthread> threads;
    for (size_t i = 0; i < count; ++i) threads.emplace_back([&, i] {
        try { work(i); }
        catch (...) { std::lock_guard lock(errorMutex); if (!error) error = std::current_exception(); }
    });
    for (auto& thread : threads) thread.join();
    if (error) std::rethrow_exception(error);
}

void persistenceAndSlots(Tree& tree) {
    const auto root = tree.root/"persist";
    std::array<LocalProfile, 4> profiles;
    {
        NativeLocalPlayers players(root);
        need(players.list().empty(), "New store was not empty");
        for (uint32_t slot = 0; slot < 4; ++slot) {
            need(players.state(slot) == 0 && !players.profile(slot), "New native session implicitly activated a profile");
            need(!players.signOut(slot), "Empty sign-out reported a change");
        }
        const std::array<std::string, 4> names{"Alice", "Bob", "123456789012345", "A B / C"};
        for (size_t i = 0; i < profiles.size(); ++i) {
            profiles[i] = players.create(names[i]);
            need(profiles[i].id.size() == 36 && profiles[i].id[14] == '4', "Generated ID is not native GUIDv4");
            need(profiles[i].name == names[i], "Persisted profile name differs");
            need(players.load(profiles[i].id) == profiles[i], "Actual persisted reload differs");
            need(players.state(static_cast<uint32_t>(i)) == 0, "Creation implicitly activated slot");
        }
        auto all = players.list();
        need(all.size() == 4 && std::is_sorted(all.begin(), all.end(), [](const auto& a, const auto& b) {
            return a.id < b.id;
        }), "List order/count is not deterministic");
        all[0].name = "Changed copy";
        need(players.list()[0].name != "Changed copy", "List exposes mutable owner state");
        for (uint32_t slot = 0; slot < 4; ++slot) {
            need(players.activate(slot, profiles[slot].id), "First activation did not change state");
            need(!players.activate(slot, profiles[slot].id), "Idempotent activation reported a change");
            need(players.state(slot) == 1 && players.profile(slot) == profiles[slot], "Active native state/profile mismatch");
        }
        auto copy = players.profile(0);
        copy->id = "foreign"; copy->name = "Altered";
        need(players.profile(0) == profiles[0], "Profile snapshot aliases active state");
        rejects([&] { players.activate(1, profiles[0].id); }, "Duplicate active identity admitted in two slots");
        need(players.profile(1) == profiles[1], "Duplicate activation damaged prior slot");
        for (uint32_t slot : {4u, 7u, 0xffffffffu}) {
            rejects([&] { (void)players.state(slot); }, "Invalid slot state aliased a user");
            rejects([&] { (void)players.profile(slot); }, "Invalid slot profile aliased a user");
            rejects([&] { players.activate(slot, profiles[0].id); }, "Invalid activation slot accepted");
            rejects([&] { players.signOut(slot); }, "Invalid sign-out slot accepted");
        }
        // Active leases deny changes by real Windows file handles, not merely
        // through the public API; removing the lease restores normal ownership.
        const auto active = fileOf(root, profiles[0]);
        Handle writer{CreateFileW(active.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, 0, nullptr)};
        need(writer.value == INVALID_HANDLE_VALUE, "Active profile admitted a concurrent writer");
        need(!DeleteFileW(active.c_str()), "Active profile admitted deletion");
        NativeLocalPlayers independent(root);
        need(independent.state(0) == 0 && independent.load(profiles[0].id) == profiles[0],
             "A second owner inherited another session or could not read an immutable active profile");
    }
    NativeLocalPlayers reopened(root);
    need(reopened.list().size() == 4, "Profile files did not persist across owner destruction");
    for (uint32_t slot = 0; slot < 4; ++slot) {
        need(reopened.load(profiles[slot].id) == profiles[slot] && reopened.state(slot) == 0,
             "Reopen lost identity/name or restored session sign-in implicitly");
    }
    need(reopened.activate(0, profiles[0].id), "Reopened activation failed");
    need(reopened.activate(0, profiles[1].id), "Replacement by another inactive profile failed");
    need(reopened.profile(0) == profiles[1], "Replacement did not publish new profile atomically");
    need(reopened.signOut(0) && !reopened.signOut(0), "Sign-out changed-state return differs");
    need(reopened.load(profiles[1].id) == profiles[1], "Sign-out deleted durable profile");
    std::puts("PASS durable reopen / immutable snapshots / four slots / active file leases / independent sessions");
}

void identityKeys(Tree& tree) {
    const auto root=tree.root/"identity";NativeLocalPlayers players(root);
    const auto first=NativeLocalPlayersTestAccess::create(players,"First","12345678-1234-4abc-8abc-123456789abc");
    need(players.identity(0)==0,"Stored profile implicitly exposed an active identity");
    const auto before=read(fileOf(root,first));
    rejects([&]{NativeLocalPlayersTestAccess::create(players,"Alias","87654321-4321-4def-8abc-123456789abc");},
            "Different durable GUID aliased the same native equality key");
    need(players.list().size()==1 && read(fileOf(root,first))==before,"Key collision changed the durable store");
    need(players.activate(2,first.id) && players.identity(2)==0x8ABC123456789ABCull && players.identity(0)==0,
         "Native identity bytes or slot association differ");
    rejects([&]{players.identity(4);},"Invalid identity slot aliased a player");
    {NativeLocalPlayers reopened(root);need(reopened.identity(2)==0,"Independent session inherited identity");
     need(reopened.activate(0,first.id) && reopened.identity(0)==players.identity(2),"Durable identity changed on reopen or slot move");}
    // Import a valid separately created profile with the colliding key. The
    // dedicated store must reject its ambiguity before replacing an active slot.
    const auto otherRoot=tree.root/"identity-other";LocalProfile other;
    {NativeLocalPlayers source(otherRoot);other=NativeLocalPlayersTestAccess::create(source,"Other","87654321-4321-4def-8abc-123456789abc");}
    fs::copy_file(fileOf(otherRoot,other),fileOf(root,other));
    rejects([&]{players.list();},"Imported key collision was accepted");
    rejects([&]{players.activate(1,other.id);},"Imported alias activated");
    need(players.identity(2)==0x8ABC123456789ABCull && players.identity(1)==0,"Rejected key alias damaged active identity");
    need(players.signOut(2) && players.identity(2)==0 && read(fileOf(root,first))==before,"Sign-out changed durable identity bytes");
    std::puts("PASS native64-bit profile keys / durable reopen / key-collision rejection / slot lifetime");
}

void achievements(Tree& tree) {
    const auto root=tree.root/"achievement-profiles",store=tree.root/"achievement-profiles.achievements";
    NativeLocalPlayers players(root);const auto first=players.create("One"),second=players.create("Two");
    const auto profileBytes=read(fileOf(root,first));
    need(!players.hasAchievement(first.id,0x45410809,1) && !fs::exists(store),"Missing achievement query invented storage");
    rejects([&]{players.writeAchievements(0x45410809,{{0,1}});},"Inactive profile earned an achievement");
    need(!fs::exists(store),"Rejected inactive write created a store");
    players.activate(0,first.id);players.activate(2,second.id);
    rejects([&]{players.writeAchievements(0x45410809,{{0,1},{1,2}});},"Mixed active/inactive batch accepted");
    need(!fs::exists(store),"Rejected batch wrote a valid prefix");
    need(players.writeAchievements(0x45410809,{{0,1},{0,1},{2,1},{0,0xFFFFFFFF}})==3,"Unique durable unlock count differs");
    need(players.hasAchievement(first.id,0x45410809,1) && players.hasAchievement(first.id,0x45410809,0xFFFFFFFF) &&
         players.hasAchievement(second.id,0x45410809,1) && !players.hasAchievement(second.id,0x45410809,0xFFFFFFFF) &&
         !players.hasAchievement(first.id,0x4541080A,1),"Native achievements crossed profile/title/ID boundaries");
    const auto file=store/(first.id+"-45410809-00000001.achievement");const auto bytes=read(file);
    need(bytes.starts_with("SIMPSONS-LOCAL-ACHIEVEMENT 1\n"+first.id+"\n45410809\n00000001\nSHA256:"),"Durable achievement framing differs");
    need(players.writeAchievements(0x45410809,{{0,1},{2,1}})==0 && read(file)==bytes,"Repeated unlock rewrote durable bytes");
    players.signOut(0);players.activate(3,first.id);
    need(players.writeAchievements(0x45410809,{{3,1}})==0,"Achievement identity depended on transient slot");
    {NativeLocalPlayers reopened(root);need(reopened.hasAchievement(first.id,0x45410809,1) && reopened.state(3)==0,"Reopen lost achievement or restored session");
     reopened.activate(1,first.id);need(reopened.writeAchievements(0x45410809,{{1,1}})==0,"Independent owner did not validate existing achievement");}
    concurrent(6,[&](size_t index){const auto id=uint32_t(index+100);players.writeAchievements(0x45410809,{{3,id}});need(players.hasAchievement(first.id,0x45410809,id),"Concurrent durable unlock lost");});
    write(file,"damaged");
    rejects([&]{players.hasAchievement(first.id,0x45410809,1);},"Corrupt achievement reported earned");
    rejects([&]{players.writeAchievements(0x45410809,{{3,2},{3,1}});},"Corrupt existing achievement silently repaired");
    need(!players.hasAchievement(first.id,0x45410809,2) && read(file)=="damaged","Corrupt batch wrote a prefix or repaired bytes");
    write(file,bytes);
    const auto aliasDirectory=tree.root/"achievement-aliases";
    need(fs::create_directory(aliasDirectory),"Cannot create owned hard-link destination");
    const auto alias=aliasDirectory/"achievement-link";
    need(CreateHardLinkW(alias.c_str(),file.c_str(),nullptr)!=FALSE,"Cannot create actual achievement hard link fixture");
    rejects([&]{players.hasAchievement(first.id,0x45410809,1);},"Hardlinked achievement accepted");
    need(DeleteFileW(alias.c_str())!=FALSE,"Cannot remove owned hard link fixture");
    need(players.hasAchievement(first.id,0x45410809,1) && read(fileOf(root,first))==profileBytes,"Achievement writes changed immutable profile");
    const auto other=tree.root/"achievement-junction-profiles",foreign=tree.root/"achievement-junction-target";
    NativeLocalPlayers redirected(other);const auto p=redirected.create("Redirect");redirected.activate(0,p.id);
    need(fs::create_directory(foreign),"Cannot create owned junction destination");
    tree.junction(tree.root/"achievement-junction-profiles.achievements",foreign);
    rejects([&]{redirected.writeAchievements(0x45410809,{{0,1}});},"Achievement store followed a directory junction");
    need(fs::is_empty(foreign),"Rejected achievement junction wrote outside its store");
    std::puts("PASS real durable achievement publication/reopen/idempotence/isolation/corruption/containment");
}

void rejectionAndCollision(Tree& tree) {
    const auto root = tree.root/"reject";
    NativeLocalPlayers players(root);
    for (const std::string& name : {std::string(), std::string(" leading"), std::string("trailing "),
            std::string("1234567890123456"), std::string("a\nb"), std::string("a\tb"),
            std::string("a\0b", 3), std::string("a\x7f"), std::string("a\x80")})
        rejects([&] { players.create(name); }, "Invalid name accepted");
    need(players.list().empty(), "Name rejection changed the store");
    const std::string fixed = "12345678-1234-4abc-8abc-123456789abc";
    const auto first = NativeLocalPlayersTestAccess::create(players, "Collision", fixed);
    const auto before = read(fileOf(root, first));
    rejects([&] { NativeLocalPlayersTestAccess::create(players, "Overwrite", fixed); },
            "Exclusive final publication overwrote a real existing identity");
    need(read(fileOf(root, first)) == before && players.list().size() == 1,
         "Collision changed bytes or leaked pending file");
    const auto second = players.create("Collision");
    need(first.name == second.name && first.id != second.id, "Display name was misused as identity");
    need(players.activate(0, first.id), "Rejection fixture activation failed");
    for (const std::string& id : {std::string(), std::string("../escape"), std::string("C:\\escape"),
            std::string("12345678-1234-4ABC-8abc-123456789abc"),
            std::string("12345678-1234-3abc-8abc-123456789abc"),
            std::string("12345678-1234-4abc-cabc-123456789abc"),
            std::string("12345678-1234-4abc-8abc-123456789ab:"),
            std::string("deadbeef-0000-4000-8000-000000000000")}) {
        rejects([&] { players.load(id); }, "Malformed/missing profile ID loaded");
        rejects([&] { players.activate(0, id); }, "Malformed/missing profile activation succeeded");
        need(players.profile(0) == first, "Rejected activation lost existing lease");
    }
    // Actual sharing violation exercises IO failure, without pretending a
    // successful allocation/write/flush or injecting an invented status.
    {
        Handle lock{CreateFileW(fileOf(root, second).c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr)};
        need(lock.value != INVALID_HANDLE_VALUE, "Cannot create real IO contention fixture");
        rejects([&] { players.load(second.id); }, "Locked file load succeeded");
        rejects([&] { players.activate(0, second.id); }, "Locked file activation succeeded");
        rejects([&] { players.create("Blocked"); }, "Corrupt/unreadable store admitted new publication");
        need(players.profile(0) == first, "IO rejection damaged active slot");
    }
    need(players.list().size() == 2 && players.load(second.id) == second, "IO failure leaked files or damaged store");
    need(players.signOut(0), "Sign-out did not release active lease");
    need(DeleteFileW(fileOf(root, first).c_str()) != FALSE, "Released file remained pinned");
    rejects([&] { players.activate(0, first.id); }, "Deleted stale identity activated");
    std::puts("PASS malformed names/IDs / real collision no overwrite / IO contention / rollback / stale deleted identity");
}

void corruption(Tree& tree) {
    const auto root = tree.root/"corrupt";
    NativeLocalPlayers players(root);
    const auto stable = players.create("Stable"), victim = players.create("Victim");
    need(players.activate(0, stable.id), "Corruption fixture activation failed");
    const auto file = fileOf(root, victim);
    const auto good = read(file);
    std::vector<std::string> invalid{"", "bad", good.substr(0, good.size()-1), good + "trailing", std::string(257, 'x')};
    auto altered = good; altered[0] ^= 1; invalid.push_back(altered);
    altered = good; altered[good.find("Victim")] = 'X'; invalid.push_back(altered); // Valid name, wrong digest.
    altered = good; altered[good.find("Victim")] = '\0'; invalid.push_back(altered);
    altered = good; altered[good.find("Victim")] = static_cast<char>(0x80); invalid.push_back(altered);
    altered = good; altered[good.find(victim.id)] = victim.id[0] == 'a' ? 'b' : 'a'; invalid.push_back(altered);
    for (const auto& bytes : invalid) {
        write(file, bytes);
        rejects([&] { players.load(victim.id); }, "Corrupt profile was accepted");
        rejects([&] { players.list(); }, "List silently skipped/repaired corrupt profile");
        rejects([&] { players.activate(0, victim.id); }, "Corrupt activation succeeded");
        need(players.profile(0) == stable && read(file) == bytes, "Corruption rejection modified slot or source bytes");
    }
    write(file, good);
    need(players.load(victim.id) == victim, "Explicit fixture restoration did not restore valid profile");
    write(root/".pending-interrupted", "partial");
    rejects([&] { players.list(); }, "Abandoned pending record was silently ignored/repaired");
    need(read(root/".pending-interrupted") == "partial", "Listing removed unowned interrupted file");
    fs::remove(root/".pending-interrupted");
    write(root/"unknown.txt", "unexpected");
    rejects([&] { players.list(); }, "Unknown dedicated-store file ignored");
    fs::remove(root/"unknown.txt");
    std::puts("PASS bounded reads / checksum / strict framing / corrupt-file failure / no automatic repair");
}

void containment(Tree& tree) {
    const auto outside = tree.root/"outside";
    fs::create_directory(outside);
    write(outside/"sentinel", "UNCHANGED");
    const auto link = tree.root/"root-link";
    tree.junction(link, outside);
    rejects([&] { NativeLocalPlayers escaped(link/"must-not-exist"); }, "Ancestor junction escaped before creation");
    rejects([&] { NativeLocalPlayers escaped(link); }, "Root junction accepted");
    need(!fs::exists(outside/"must-not-exist") && read(outside/"sentinel") == "UNCHANGED", "Rejected root changed outside data");
    rejects([&] { NativeLocalPlayers bad(tree.root/".."/"escape"); }, "Traversal root accepted");
    rejects([&] { NativeLocalPlayers bad(tree.root/"bad."); }, "Ambiguous root accepted");
    rejects([&] { NativeLocalPlayers bad(tree.root/"NUL"); }, "Reserved root accepted");
    const auto root = tree.root/"contained";
    LocalProfile saved;
    {
        NativeLocalPlayers players(root);
        saved = players.create("Contained");
        const auto alias = outside/"alias.profile";
        need(CreateHardLinkW(alias.c_str(), fileOf(root, saved).c_str(), nullptr) != FALSE, "Actual hard-link fixture failed");
        rejects([&] { players.load(saved.id); }, "External hard-link alias accepted");
        rejects([&] { players.list(); }, "List admitted hard-linked profile");
        need(DeleteFileW(alias.c_str()) != FALSE, "Could not remove owned hard-link alias");
        need(players.load(saved.id) == saved, "Hard-link rejection modified original profile");
        const std::string other = "deadbeef-0000-4000-8000-000000000000";
        tree.junction(root/(other+".profile"), outside);
        rejects([&] { players.load(other); }, "Profile entry reparse point followed");
        rejects([&] { players.list(); }, "Profile entry reparse point ignored");
        need(!MoveFileExW(root.c_str(), (tree.root/"moved").c_str(), 0), "Live owner allowed root replacement");
        need(!MoveFileExW(tree.root.c_str(), (tree.parent/(tree.root.filename().wstring()+L"-moved")).c_str(), 0),
             "Live owner allowed ancestor replacement");
    }
    // Remove the known junction itself before moving its parent; cleanup paths
    // must never become stale aliases after this positive lifetime test.
    need(RemoveDirectoryW((root/"deadbeef-0000-4000-8000-000000000000.profile").c_str()) != FALSE,
         "Could not unlink owned profile junction");
    need(MoveFileExW(root.c_str(), (tree.root/"moved").c_str(), 0) != FALSE, "Destroyed owner retained root pin");
    need(MoveFileExW((tree.root/"moved").c_str(), root.c_str(), 0) != FALSE, "Could not restore owned root name");
    need(read(outside/"sentinel") == "UNCHANGED", "Containment test modified outside payload");
    std::puts("PASS actual NTFS reparse/hard-link rejection / path validation / ancestor pin lifetime");
}

void concurrency(Tree& tree) {
    NativeLocalPlayers players(tree.root/"concurrent");
    std::mutex resultsMutex;
    std::vector<LocalProfile> created;
    concurrent(8, [&](size_t worker) {
        for (size_t i = 0; i < 4; ++i) {
            const auto p = players.create("Worker" + std::to_string(worker));
            need(players.load(p.id) == p, "Concurrent committed load differs");
            std::lock_guard lock(resultsMutex); created.push_back(p);
        }
    });
    std::set<std::string> ids;
    for (const auto& p : created) need(ids.insert(p.id).second, "Random profile ID collision was accepted");
    need(players.list().size() == 32, "Concurrent creations lost durable profiles");
    concurrent(4, [&](size_t index) {
        const uint32_t slot = static_cast<uint32_t>(index);
        for (unsigned i = 0; i < 40; ++i) {
            need(players.activate(slot, created[index].id), "Concurrent activation failed");
            need(!players.activate(slot, created[index].id), "Concurrent idempotent activation changed state");
            need(players.state(slot) == 1 && players.profile(slot) == created[index], "Concurrent slot snapshot differs");
            need(players.signOut(slot) && !players.signOut(slot), "Concurrent sign-out return differs");
        }
    });
    std::atomic<unsigned> winners{0};
    concurrent(4, [&](size_t index) {
        try { if (players.activate(static_cast<uint32_t>(index), created[0].id)) ++winners; }
        catch (const LocalPlayerError&) { /* Another slot won; verify global outcome below. */ }
    });
    unsigned activeCount = 0;
    for (uint32_t slot = 0; slot < 4; ++slot) activeCount += players.state(slot);
    need(winners == 1 && activeCount == 1, "Concurrent duplicate-identity admission was not atomic");
    for (uint32_t slot = 0; slot < 4; ++slot) players.signOut(slot);
    std::puts("PASS 32 real concurrent publications / slot transitions / exclusive identity admission");
}
}

int main() {
    try {
        Tree tree;
        persistenceAndSlots(tree);
        identityKeys(tree);
        achievements(tree);
        rejectionAndCollision(tree);
        corruption(tree);
        containment(tree);
        concurrency(tree);
        std::printf("NativeLocalPlayerOwnership PASS %zu checks\n", checks.load());
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "NativeLocalPlayerOwnership FAIL after %zu checks: %s\n", checks.load(), error.what());
        return 1;
    }
}
