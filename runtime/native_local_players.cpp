#include "native_local_players.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string_view>
#include <utility>

namespace Simpsons::Platform {
namespace {
constexpr std::string_view magic = "SIMPSONS-LOCAL-PROFILE 1\n";
constexpr size_t maxFileBytes = 256;
constexpr size_t maxDirectoryEntries = 4096;

[[noreturn]] void fail(const std::string& text) { throw LocalPlayerError(text); }
[[noreturn]] void winFail(const char* operation, DWORD code = GetLastError()) {
    fail(std::string(operation) + " (Win32 " + std::to_string(code) + ")");
}
void require(bool ok, const char* message) { if (!ok) fail(message); }

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) noexcept : value(h) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
            value = std::exchange(other.value, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
};
struct Search {
    HANDLE value;
    ~Search() { if (value != INVALID_HANDLE_VALUE) FindClose(value); }
};

void validateId(const std::string& id) {
    require(id.size() == 36, "Profile ID must be a canonical lowercase version-4 GUID");
    for (size_t i = 0; i < id.size(); ++i) {
        const char c = id[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            require(c == '-', "Invalid profile ID separator");
        } else {
            require((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'), "Invalid profile ID character");
        }
    }
    require(id[14] == '4' && (id[19] == '8' || id[19] == '9' || id[19] == 'a' || id[19] == 'b'),
            "Unqualified profile GUID version/variant");
}
void validateName(const std::string& name) {
    require(!name.empty() && name.size() <= 15, "Profile name must contain 1..15 ASCII bytes");
    require(name.front() != ' ' && name.back() != ' ', "Profile name has leading/trailing spaces");
    for (unsigned char c : name) require(c >= 32 && c <= 126, "Profile name is not printable ASCII");
}
void validateSlot(uint32_t slot) { require(slot < 4, "Local player slot must be 0..3"); }
uint64_t profileKey(const std::string& id) {
    // The full canonical GUID remains the filename/primary identity. Its final
    // 64 bits supply the game's bounded native equality key. The UUID variant
    // ensures this key cannot be zero; list/create/activation reject aliases.
    validateId(id);uint64_t value=0;
    for(size_t i=19;i<36;++i)if(i!=23) {
        const char c=id[i];value=(value<<4)|uint64_t(c<='9'?c-'0':c-'a'+10);
    }
    require(value!=0,"Local profile equality key is zero");return value;
}

std::string hex(const unsigned char* bytes, size_t count) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out(count * 2, '0');
    for (size_t i = 0; i < count; ++i) {
        out[i*2] = digits[bytes[i] >> 4];
        out[i*2+1] = digits[bytes[i] & 15];
    }
    return out;
}
std::string randomId() {
    std::array<unsigned char, 16> bytes{};
    const NTSTATUS result = BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                                           BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (result < 0) fail("BCryptGenRandom failed (NTSTATUS " + std::to_string(result) + ")");
    bytes[6] = static_cast<unsigned char>((bytes[6] & 15) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 63) | 0x80);
    const std::string raw = hex(bytes.data(), bytes.size());
    return raw.substr(0,8) + "-" + raw.substr(8,4) + "-" + raw.substr(12,4) + "-" +
           raw.substr(16,4) + "-" + raw.substr(20,12);
}
std::string digest(std::string_view bytes) {
    require(bytes.size() <= maxFileBytes, "Profile hash input exceeds record bound");
    std::array<unsigned char, 32> value{};
    const NTSTATUS result = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()),
        value.data(), static_cast<ULONG>(value.size()));
    if (result < 0) fail("BCryptHash failed (NTSTATUS " + std::to_string(result) + ")");
    return hex(value.data(), value.size());
}
std::string serialize(const LocalProfile& profile) {
    const std::string prefix = std::string(magic) + profile.id + "\n" + profile.name + "\n";
    return prefix + "SHA256:" + digest(prefix) + "\n";
}
LocalProfile parse(const std::string& bytes, const std::string& expectedId) {
    require(bytes.size() <= maxFileBytes && bytes.starts_with(magic), "Invalid profile framing/version");
    const size_t idEnd = bytes.find('\n', magic.size());
    require(idEnd != std::string::npos, "Truncated profile ID");
    const size_t nameEnd = bytes.find('\n', idEnd+1);
    require(nameEnd != std::string::npos, "Truncated profile name");
    LocalProfile result{bytes.substr(magic.size(), idEnd-magic.size()),
                        bytes.substr(idEnd+1, nameEnd-idEnd-1)};
    validateId(result.id);
    validateName(result.name);
    require(result.id == expectedId, "Profile identity differs from filename");
    const std::string expected = "SHA256:" + digest(std::string_view(bytes).substr(0, nameEnd+1)) + "\n";
    require(std::string_view(bytes).substr(nameEnd+1) == expected, "Profile checksum/trailing-data mismatch");
    return result;
}

bool equalPath(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}
std::wstring finalPath(HANDLE handle) {
    const DWORD count = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!count) winFail("GetFinalPathNameByHandleW size");
    require(count <= 32768, "Profile path exceeds native path bound");
    std::wstring path(count, L'\0');
    const DWORD actual = GetFinalPathNameByHandleW(handle, path.data(), count,
                                                  FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!actual || actual >= count) winFail("GetFinalPathNameByHandleW value");
    path.resize(actual);
    while (path.size() > 7 && path.back() == L'\\') path.pop_back();
    return path;
}
void objectKind(HANDLE handle, bool directory) {
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag)))
        winFail("Profile file attributes");
    require(GetFileType(handle) == FILE_TYPE_DISK, "Profile storage must be a disk object");
    require(!(tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT), "Profile storage rejects reparse points");
    require(bool(tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory, "Wrong profile storage object type");
    if (!directory) {
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info)) winFail("Profile file identity");
        require(info.nNumberOfLinks == 1, "Profile file must have exactly one hard link");
    }
}
void component(const std::wstring& name) {
    require(!name.empty() && name.size() <= 255 && name != L"." && name != L"..", "Invalid profile root component");
    require(name.back() != L'.' && name.back() != L' ', "Ambiguous profile root component");
    for (wchar_t c : name) require(c >= 32 && std::wstring_view(L"<>:\"|?*\\/").find(c) == std::wstring_view::npos,
                                  "Invalid profile root character");
    std::wstring stem = name.substr(0, name.find(L'.'));
    for (wchar_t& c : stem) if (c >= L'a' && c <= L'z') c -= L'a' - L'A';
    require(stem != L"CON" && stem != L"PRN" && stem != L"AUX" && stem != L"NUL" &&
            !(stem.size() == 4 && (stem.starts_with(L"COM") || stem.starts_with(L"LPT")) &&
              stem[3] >= L'0' && stem[3] <= L'9'), "Reserved profile root component");
}
std::filesystem::path rootPath(std::filesystem::path input) {
    require(!input.empty(), "Profile root must not be empty");
    std::wstring raw = input.native();
    require(raw.find(L'\0') == std::wstring::npos && raw.size() < 30000, "Invalid profile root string");
    std::replace(raw.begin(), raw.end(), L'/', L'\\');
    require(!raw.starts_with(L"\\\\"), "Profile root must be a local drive path, not UNC/device namespace");
    input = std::filesystem::path(raw);
    for (const auto& part : input.relative_path()) component(part.native());
    // Reject drive-relative C:foo rather than resolve it using hidden per-drive CWD.
    require(!input.has_root_name() || input.has_root_directory(), "Drive-relative profile root rejected");
    const auto absolute = std::filesystem::absolute(input).lexically_normal();
    const auto root = absolute.root_path().native();
    require(root.size() == 3 && root[1] == L':' && root[2] == L'\\' &&
            ((root[0] >= L'A' && root[0] <= L'Z') || (root[0] >= L'a' && root[0] <= L'z')),
            "Profile root must be on a local drive");
    require(!absolute.relative_path().empty(), "A volume root cannot be a profile store");
    for (const auto& part : absolute.relative_path()) component(part.native());
    require(GetDriveTypeW(root.c_str()) == DRIVE_FIXED, "Profile store requires a fixed local volume");
    std::array<wchar_t, 32> fs{};
    if (!GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, nullptr, fs.data(),
                              static_cast<DWORD>(fs.size()))) winFail("Profile volume information");
    require(std::wstring_view(fs.data()) == L"NTFS", "Profile store currently requires NTFS");
    return absolute;
}

// Owns only a newly created file. Failure cleanup uses this handle, never a
// path that could have been replaced by an unrelated file. The final name is
// visible while being written, but share-zero ownership prevents readers until
// flush and close. A crash-partial record fails validation; no automatic repair.
struct Pending {
    Handle file;
    bool committed = false;
    explicit Pending(HANDLE value) : file(value) {}
    ~Pending() {
        if (!committed && file.value != INVALID_HANDLE_VALUE) {
            FILE_DISPOSITION_INFO disposition{TRUE};
            if (!SetFileInformationByHandle(file.value, FileDispositionInfo, &disposition, sizeof(disposition)))
                std::fprintf(stderr, "[LOCAL PROFILE] failed to retire uncommitted file: Win32 %lu\n", GetLastError());
        }
    }
};
struct PinnedDirectory {
    std::vector<Handle> ancestors;
    std::wstring path;
    const std::filesystem::path absolute;
    explicit PinnedDirectory(const std::filesystem::path& requested):absolute(rootPath(requested)) {
        std::filesystem::path walk = absolute.root_path();
        const auto pin = [&]() {
            const std::wstring extended = L"\\\\?\\" + walk.native();
            Handle handle(CreateFileW(extended.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            if (handle.value == INVALID_HANDLE_VALUE) winFail("Pin profile directory");
            objectKind(handle.value, true);
            ancestors.push_back(std::move(handle));
        };
        pin();
        for (const auto& part : absolute.relative_path()) {
            walk /= part;
            const std::wstring extended = L"\\\\?\\" + walk.native();
            if (!CreateDirectoryW(extended.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
                winFail("Create profile directory");
            pin();
        }
        path = finalPath(ancestors.back().value);
    }
    void preflight() const {
        for (const auto& handle : ancestors) objectKind(handle.value, true);
        require(equalPath(finalPath(ancestors.back().value), path), "Profile root identity/path changed");
    }
};

std::string hexWord(uint32_t word) {
    const std::array<unsigned char,4> bytes={static_cast<unsigned char>(word>>24),static_cast<unsigned char>(word>>16),
        static_cast<unsigned char>(word>>8),static_cast<unsigned char>(word)};
    return hex(bytes.data(),bytes.size());
}
std::string achievementBytes(const std::string& id,uint32_t title,uint32_t achievement) {
    const std::string prefix="SIMPSONS-LOCAL-ACHIEVEMENT 1\n"+id+"\n"+hexWord(title)+"\n"+hexWord(achievement)+"\n";
    return prefix+"SHA256:"+digest(prefix)+"\n";
}
std::wstring achievementName(const std::string& id,uint32_t title,uint32_t achievement) {
    const auto name=id+"-"+hexWord(title)+"-"+hexWord(achievement)+".achievement";
    return {name.begin(),name.end()};
}
struct AchievementRecord {std::string bytes;Handle file;};
std::unique_ptr<AchievementRecord> readAchievement(const PinnedDirectory& directory,const std::wstring& name) {
    directory.preflight();component(name);const auto expected=directory.path+L"\\"+name;
    Handle file(CreateFileW(expected.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    if(file.value==INVALID_HANDLE_VALUE){const auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND)return {};winFail("Open local achievement",error);}
    objectKind(file.value,false);require(equalPath(finalPath(file.value),expected),"Achievement escaped its pinned root");
    LARGE_INTEGER size{};if(!GetFileSizeEx(file.value,&size))winFail("Achievement size");
    require(size.QuadPart>0 && size.QuadPart<=LONGLONG(maxFileBytes),"Achievement size out of bounds");
    std::string bytes(size_t(size.QuadPart),'\0');DWORD got{};
    if(!ReadFile(file.value,bytes.data(),DWORD(bytes.size()),&got,nullptr))winFail("Read local achievement");
    require(got==bytes.size(),"Short achievement read");
    return std::make_unique<AchievementRecord>(AchievementRecord{std::move(bytes),std::move(file)});
}
}

struct NativeLocalPlayers::State : PinnedDirectory {
    struct Lease { LocalProfile profile; Handle file; };
    mutable std::mutex mutex;
    std::array<std::unique_ptr<Lease>,4> slots;
    const std::filesystem::path achievementRoot;
    mutable std::unique_ptr<PinnedDirectory> achievements;
    explicit State(const std::filesystem::path& requested):PinnedDirectory(requested),
        achievementRoot(absolute.native()+L".achievements") {}
    PinnedDirectory& achievementDirectory() const {
        if(!achievements)achievements=std::make_unique<PinnedDirectory>(achievementRoot);
        return *achievements;
    }
    std::wstring filename(const std::string& id) const {
        return path + L"\\" + std::wstring(id.begin(), id.end()) + L".profile";
    }
    std::unique_ptr<Lease> read(const std::string& id) const {
        validateId(id);
        preflight();
        const auto expected = filename(id);
        Handle file(CreateFileW(expected.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (file.value == INVALID_HANDLE_VALUE) winFail("Open local profile");
        objectKind(file.value, false);
        require(equalPath(finalPath(file.value), expected), "Profile file escaped its pinned root");
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.value, &size)) winFail("Local profile size");
        require(size.QuadPart > 0 && size.QuadPart <= static_cast<LONGLONG>(maxFileBytes), "Local profile size out of bounds");
        std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
        DWORD got = 0;
        if (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr)) winFail("Read local profile");
        require(got == bytes.size(), "Short local profile read");
        return std::make_unique<Lease>(Lease{parse(bytes, id), std::move(file)});
    }
    std::vector<LocalProfile> list() const {
        preflight();
        std::vector<LocalProfile> result;
        std::set<uint64_t> keys;
        WIN32_FIND_DATAW entry{};
        Search search{FindFirstFileW((path + L"\\*").c_str(), &entry)};
        if (search.value == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND) return result;
            winFail("Enumerate local profiles");
        }
        size_t visited = 0;
        do {
            const std::wstring name(entry.cFileName);
            if (name == L"." || name == L"..") continue;
            require(++visited <= maxDirectoryEntries, "Local profile directory exceeds bounded entry count");
            require(!(entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)),
                    "Unexpected directory/reparse entry in profile store");
            require(!name.starts_with(L".pending-"), "Uncommitted profile file present; no automatic recovery");
            require(name.size() == 44 && name.ends_with(L".profile"), "Unexpected file in dedicated profile store");
            std::string id;
            for (size_t i = 0; i < 36; ++i) {
                require(name[i] <= 127, "Non-ASCII profile filename");
                id += static_cast<char>(name[i]);
            }
            auto profile=read(id)->profile;
            require(keys.insert(profileKey(profile.id)).second,"Local profile equality-key collision");
            result.push_back(std::move(profile));
        } while (FindNextFileW(search.value, &entry));
        if (GetLastError() != ERROR_NO_MORE_FILES) winFail("Continue local profile enumeration");
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        return result;
    }
    LocalProfile create(const std::string& name, const std::string& id) {
        validateName(name);
        validateId(id);
        preflight();
        const auto existing=list();
        require(existing.size() < maxDirectoryEntries, "Local profile store is full");
        const auto key=profileKey(id);
        require(std::none_of(existing.begin(),existing.end(),[&](const auto& p){return p.id!=id && profileKey(p.id)==key;}),
                "Local profile equality key already exists");
        LocalProfile result{id, name};
        const auto bytes = serialize(result);
        const std::wstring target = filename(id);
        // CREATE_NEW never opens an existing record. All allocation/serialization
        // precedes creation; the final file stays exclusively owned until the
        // exact write and real flush complete. No parent directory reopen for
        // rename is needed, so every ancestor continues to deny write/delete.
        Pending pending(CreateFileW(target.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (pending.file.value == INVALID_HANDLE_VALUE) winFail("Create exclusive local profile without overwrite");
        objectKind(pending.file.value, false);
        require(equalPath(finalPath(pending.file.value), target), "Created profile escaped its pinned root");
        DWORD written = 0;
        if (!WriteFile(pending.file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr))
            winFail("Write local profile");
        require(written == bytes.size(), "Short local profile write");
        if (!FlushFileBuffers(pending.file.value)) winFail("Flush local profile data");
        pending.committed = true;
        return result;
    }
};

NativeLocalPlayers::NativeLocalPlayers(std::filesystem::path root) : impl(std::make_unique<State>(root)) {}
NativeLocalPlayers::~NativeLocalPlayers() = default;
std::vector<LocalProfile> NativeLocalPlayers::list() const {
    std::lock_guard lock(impl->mutex);
    return impl->list();
}
LocalProfile NativeLocalPlayers::create(const std::string& name) {
    std::lock_guard lock(impl->mutex);
    // RNG/filename collisions are explicit failures, never overwrite or repair.
    return impl->create(name, randomId());
}
LocalProfile NativeLocalPlayers::createWithId(const std::string& name, const std::string& id) {
    std::lock_guard lock(impl->mutex);
    return impl->create(name, id);
}
LocalProfile NativeLocalPlayers::load(const std::string& id) const {
    std::lock_guard lock(impl->mutex);
    return impl->read(id)->profile;
}
bool NativeLocalPlayers::activate(uint32_t slot, const std::string& id) {
    validateSlot(slot);
    validateId(id);
    std::lock_guard lock(impl->mutex);
    if (impl->slots[slot] && impl->slots[slot]->profile.id == id) return false;
    for (size_t i = 0; i < impl->slots.size(); ++i)
        require(i == slot || !impl->slots[i] || impl->slots[i]->profile.id != id,
                "Local profile is already active in another slot");
    auto lease = impl->read(id); // Failure leaves the old slot and file lease intact.
    // Validate the complete bounded store before exposing a64-bit key; files
    // imported by another owner must not alias a different durable profile.
    (void)impl->list();
    impl->slots[slot] = std::move(lease);
    return true;
}
bool NativeLocalPlayers::signOut(uint32_t slot) {
    validateSlot(slot);
    std::lock_guard lock(impl->mutex);
    if (!impl->slots[slot]) return false;
    impl->slots[slot].reset();
    return true;
}
uint32_t NativeLocalPlayers::state(uint32_t slot) const {
    validateSlot(slot);
    std::lock_guard lock(impl->mutex);
    return impl->slots[slot] ? 1u : 0u;
}
uint64_t NativeLocalPlayers::identity(uint32_t slot) const {
    validateSlot(slot);std::lock_guard lock(impl->mutex);
    return impl->slots[slot]?profileKey(impl->slots[slot]->profile.id):0;
}
std::optional<LocalProfile> NativeLocalPlayers::profile(uint32_t slot) const {
    validateSlot(slot);
    std::lock_guard lock(impl->mutex);
    if (!impl->slots[slot]) return std::nullopt;
    return impl->slots[slot]->profile;
}
size_t NativeLocalPlayers::writeAchievements(uint32_t title,const std::vector<LocalAchievement>& records) {
    require(title && !records.empty() && records.size()<=25,"Achievement batch is outside the qualified bound");
    std::lock_guard lock(impl->mutex);
    struct Prepared {std::wstring name;std::string bytes;std::unique_ptr<AchievementRecord> existing;};
    std::vector<Prepared> batch;std::set<std::wstring> unique;
    // Resolve every active immutable profile before creating the achievement
    // directory or any record. No session is implicitly activated by a write.
    for(const auto& record:records) {
        validateSlot(record.slot);const auto& lease=impl->slots[record.slot];require(bool(lease),"Achievement has no active local profile");
        const auto name=achievementName(lease->profile.id,title,record.id);
        if(unique.insert(name).second)batch.push_back({name,achievementBytes(lease->profile.id,title,record.id),{}});
    }
    auto& directory=impl->achievementDirectory();
    // Validate all existing records before writes and retain their read leases.
    for(auto& record:batch) {
        record.existing=readAchievement(directory,record.name);
        require(!record.existing || record.existing->bytes==record.bytes,"Existing achievement identity/checksum differs");
    }
    size_t added=0;
    for(auto& record:batch)if(!record.existing) {
        directory.preflight();const auto target=directory.path+L"\\"+record.name;
        Pending pending(CreateFileW(target.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(pending.file.value==INVALID_HANDLE_VALUE) {
            const auto error=GetLastError();
            if(error==ERROR_FILE_EXISTS || error==ERROR_ALREADY_EXISTS) {
                auto actual=readAchievement(directory,record.name);
                require(actual && actual->bytes==record.bytes,"Concurrent achievement record differs");continue;
            }
            winFail("Create exclusive achievement",error);
        }
        objectKind(pending.file.value,false);require(equalPath(finalPath(pending.file.value),target),"Created achievement escaped its root");
        DWORD written{};
        if(!WriteFile(pending.file.value,record.bytes.data(),DWORD(record.bytes.size()),&written,nullptr))winFail("Write local achievement");
        require(written==record.bytes.size(),"Short achievement write");
        if(!FlushFileBuffers(pending.file.value))winFail("Flush local achievement");
        pending.committed=true;++added;
    }
    // A filesystem failure may have committed earlier records. It throws and
    // is never acknowledged as a complete batch; retry validates each record.
    return added;
}
bool NativeLocalPlayers::hasAchievement(const std::string& profileId,uint32_t title,uint32_t id) const {
    require(title!=0,"Achievement title is absent");std::lock_guard lock(impl->mutex);
    (void)impl->read(profileId); // An achievement never invents its profile owner.
    if(!impl->achievements) {
        const auto attributes=GetFileAttributesW(impl->achievementRoot.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES) {
            const auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND)return false;
            winFail("Inspect achievement directory",error);
        }
    }
    const auto record=readAchievement(impl->achievementDirectory(),achievementName(profileId,title,id));
    if(!record)return false;
    require(record->bytes==achievementBytes(profileId,title,id),"Achievement identity/checksum differs");return true;
}
}
