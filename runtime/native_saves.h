#pragma once
#include "native_content.h"
#include <windows.h>
#include <memory>
#include <string>
#include <stdexcept>

namespace Simpsons::Platform {
struct NativeSaveError : std::runtime_error {
    uint32_t code;
    NativeSaveError(uint32_t value,const std::string& message):std::runtime_error(message),code(value){}
};
struct NativeSaveInfo {std::string profile;uint32_t title{};std::string name;std::wstring display;};
class NativeSaveSession;
class NativeSaveFile {
    friend class NativeSaveSession;
    struct State;std::unique_ptr<State> state;
    explicit NativeSaveFile(std::unique_ptr<State> value);
public:
    ~NativeSaveFile();
    HANDLE handle() const;
    bool writable() const;
    uint32_t disposition() const;
    void flush();
};
class NativeSaveSession {
    friend class NativeSaveStore;
    struct State;std::shared_ptr<State> state;
    explicit NativeSaveSession(std::shared_ptr<State> value);
public:
    ~NativeSaveSession();
    const NativeSaveInfo& info() const;
    std::shared_ptr<NativeSaveFile> openFile(const std::string& name,uint32_t access,uint32_t share,uint32_t disposition,uint32_t options);
    std::shared_ptr<NativeSaveFile> openDirectory(uint32_t access,uint32_t share,uint32_t options);
    void flush();
};
class NativeSaveStore {
    struct State;std::unique_ptr<State> state;
public:
    explicit NativeSaveStore(std::filesystem::path root);
    ~NativeSaveStore();
    // Real native folder sessions, named only by the game's bounded root alias.
    // disposition is CREATE_NEW/ALWAYS, OPEN_EXISTING/ALWAYS, TRUNCATE_EXISTING.
    uint32_t open(const std::string& alias,const NativeSaveInfo& info,uint32_t disposition);
    std::shared_ptr<NativeSaveSession> find(const std::string& alias) const;
    bool exists(const NativeSaveInfo& info) const;
    void flush(const std::string& alias);
    void close(const std::string& alias);
};
ContentSnapshot scanIndexedNativeSaves(const std::filesystem::path& root,const std::string& profile,uint32_t title);
}
