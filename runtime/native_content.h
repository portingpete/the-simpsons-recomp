#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Simpsons::Platform {
struct ContentSnapshot {
    static constexpr uint32_t recordSize=0x134;
    using Record=std::array<uint8_t,recordSize>;
    std::vector<Record> records;
    // Native file/directory leases keep enumeration metadata stable until its
    // final owner releases it. No guest pointers or console device objects.
    std::vector<std::shared_ptr<void>> leases;
};
// Marketplace packages in standard common/title/type subdirectories. Missing
// directories are empty; inaccessible, corrupt or unsupported entries fail.
// Only package header metadata is read. No mount/license/payload claim.
ContentSnapshot scanContent(const std::filesystem::path& installedRoot,const std::filesystem::path& discRoot,
    uint32_t titleId,uint32_t language,uint32_t device=0);
// Native save directories under saves/<full profile GUID>/<title>/<name>.
// Reads published metadata and pins actual data files; never creates a save.
ContentSnapshot scanNativeSaves(const std::filesystem::path& installedRoot,const std::string& profileId,
    uint32_t titleId,uint32_t device=0);
// Owns the actual configured folder and ancestors. Query is read-only;
// inspection additionally proves write/flush/delete with an owned probe file.
struct NativeStorage {
    std::filesystem::path path;
    uint64_t availableBytes{},totalBytes{},freeBytes{};
    std::wstring volumeName;
    std::vector<std::shared_ptr<void>> leases;
    bool fits(uint64_t requestedBytes) const {return requestedBytes<=availableBytes;}
};
NativeStorage queryNativeStorage(const std::filesystem::path& installedRoot);
// Fresh read-only availability of the selected native folder. A missing folder
// is disconnected; malformed, aliased or inaccessible storage still rejects.
bool nativeStorageAvailable(const std::filesystem::path& installedRoot);
NativeStorage inspectNativeStorage(const std::filesystem::path& installedRoot);
struct ContentEnumeration {
    ContentSnapshot snapshot;
    uint32_t perPage{};
    size_t cursor{};
};
}
