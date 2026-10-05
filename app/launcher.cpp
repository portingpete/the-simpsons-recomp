#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <winioctl.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
constexpr wchar_t kTitle[] = L"The Simpsons Game";

struct Failure { std::wstring message; };

enum class LaunchMode { Normal, FirstMissionCompletion, BartmanBegins };
enum class LauncherAction { DispatchGame, SelfTest };

struct LauncherOptions {
    LaunchMode mode = LaunchMode::Normal;
    LauncherAction action = LauncherAction::DispatchGame;
};

LauncherOptions parseOptions(const std::vector<std::wstring>& arguments) {
    if (arguments.size() == 1) return {};
    if (arguments.size() == 2 && arguments[1] == L"--self-test") return {LaunchMode::Normal, LauncherAction::SelfTest};
    if (arguments.size() == 2 && arguments[1] == L"--first-mission-completion")
        return {LaunchMode::FirstMissionCompletion, LauncherAction::DispatchGame};
    if (arguments.size() == 2 && arguments[1] == L"--bartman-begins")
        return {LaunchMode::BartmanBegins, LauncherAction::DispatchGame};
    throw Failure{L"Usage: SimpsonsLauncher.exe [--first-mission-completion | --bartman-begins | --self-test]"};
}

class Handle {
public:
    explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) reset(std::exchange(other.value_, nullptr));
        return *this;
    }
    explicit operator bool() const noexcept {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }
    HANDLE get() const noexcept { return value_; }
    void reset(HANDLE value = nullptr) noexcept {
        if (*this) CloseHandle(value_);
        value_ = value;
    }
private:
    HANDLE value_;
};

std::wstring windowsError(std::wstring_view action, DWORD code = GetLastError()) {
    std::array<wchar_t, 1024> buffer{};
    const DWORD count = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr);
    std::wstring result(action);
    result += L" (Windows error " + std::to_wstring(code) + L").";
    if (count != 0) {
        result += L"\n";
        result.append(buffer.data(), count);
    }
    return result;
}

[[noreturn]] void failWindows(std::wstring_view action, DWORD code = GetLastError()) {
    throw Failure{windowsError(action, code)};
}

fs::path modulePath() {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (count == 0) failWindows(L"Could not locate this launcher");
        if (count < buffer.size()) return fs::path(std::wstring(buffer.data(), count));
        if (buffer.size() >= 32768) throw Failure{L"The launcher path is too long."};
        buffer.resize(buffer.size() * 2);
    }
}

// Keep ordinary paths in commands, diagnostics and identity comparisons. Only
// filesystem calls receive the extended spelling, so deep private save trees
// do not depend on the process manifest or Windows' legacy MAX_PATH setting.
fs::path absolutePath(const fs::path& path) {
    auto spelling = path.native();
    std::replace(spelling.begin(), spelling.end(), L'/', L'\\');
    if (spelling.size() >= 8 && CompareStringOrdinal(spelling.data(), 8, L"\\\\?\\UNC\\", 8, TRUE) == CSTR_EQUAL)
        spelling = L"\\\\" + spelling.substr(8);
    else if (spelling.starts_with(L"\\\\?\\")) spelling.erase(0, 4);
    auto absolute = fs::absolute(fs::path(spelling)).lexically_normal();
    absolute.make_preferred();
    return absolute;
}

std::wstring win32Path(const fs::path& path) {
    const auto spelling = absolutePath(path).native();
    if (spelling.starts_with(L"\\\\")) return L"\\\\?\\UNC\\" + spelling.substr(2);
    return L"\\\\?\\" + spelling;
}

fs::path canonicalPath(const fs::path& path, std::error_code& error) {
    const auto resolved = fs::canonical(fs::path(win32Path(path)), error);
    return error ? fs::path{} : absolutePath(resolved);
}

bool regularFile(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(fs::path(win32Path(path)), error);
}

struct GamePaths {
    fs::path root;
    fs::path executable;
    fs::path image;
    fs::path profileStore;
    fs::path contentStore;
    fs::path logs;
};

constexpr wchar_t kProfileId[] = L"575cf79a-3815-45f7-a6f7-e8d709d16298";
constexpr wchar_t kLocalProfile[] = L"0:575cf79a-3815-45f7-a6f7-e8d709d16298";

GamePaths pathsAt(const fs::path& root) {
    return {root, root / L"build" / L"native" / L"SimpsonsNative.exe",
        root / L"analysis" / L"simpsons.pe", root / L"build" / L"mainmenu-profile-204",
        root / L"build" / L"mainmenu-content-204", root / L"build" / L"launcher-logs"};
}

// Only the launcher's own ancestors are searched; the caller's working directory
// and PATH cannot select a different copy of the game.
std::optional<GamePaths> findRoot(const fs::path& executable) {
    if (!executable.is_absolute()) return std::nullopt;
    for (fs::path candidate = executable.parent_path(); !candidate.empty();) {
        auto paths = pathsAt(candidate);
        if (regularFile(paths.executable) && regularFile(paths.image)) return paths;
        const auto parent = candidate.parent_path();
        if (parent == candidate) break;
        candidate = parent;
    }
    return std::nullopt;
}

// Microsoft CRT quoting: backslashes before quotes and before the closing quote
// must be doubled. Always quote, including empty arguments. No shell is involved.
std::wstring quoteArgument(std::wstring_view argument) {
    if (argument.find(L'\0') != std::wstring_view::npos)
        throw Failure{L"A launch argument contains an invalid character."};
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t character : argument) {
        if (character == L'\\') {
            ++slashes;
            continue;
        }
        result.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += character;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result += L'"';
    return result;
}

std::wstring gameCommand(const GamePaths& paths, LaunchMode mode = LaunchMode::Normal) {
    auto command = quoteArgument(paths.executable.native()) +
        L" --image " + quoteArgument(paths.image.native()) +
        L" --profile-store " + quoteArgument(paths.profileStore.native()) +
        L" --content-store " + quoteArgument(paths.contentStore.native()) +
        L" --local-profile " + quoteArgument(kLocalProfile);
    if (mode == LaunchMode::FirstMissionCompletion) command += L" --first-mission-completion";
    else if (mode == LaunchMode::BartmanBegins) command += L" --bartman-begins";
    if (command.size() >= 32767) throw Failure{L"The game folder path is too long to launch."};
    return command;
}

std::vector<std::wstring> splitCommand(const std::wstring& command) {
    int count = 0;
    auto** arguments = CommandLineToArgvW(command.c_str(), &count);
    if (arguments == nullptr) failWindows(L"Could not read the launch arguments");
    std::vector<std::wstring> result;
    try {
        for (int index = 0; index < count; ++index) result.emplace_back(arguments[index]);
    } catch (...) {
        LocalFree(arguments);
        throw;
    }
    LocalFree(arguments);
    return result;
}

void ensureDirectory(const fs::path& directory) {
    std::error_code error;
    fs::create_directories(fs::path(win32Path(directory)), error);
    if (error) throw Failure{L"Could not create this folder:\n" + directory.native() +
        L"\nCheck that the game folder is writable. Error " + std::to_wstring(error.value()) + L"."};
}

std::wstring padded(unsigned int value, size_t width) {
    auto text = std::to_wstring(value);
    if (text.size() < width) text.insert(0, width - text.size(), L'0');
    return text;
}

std::wstring logStem(const SYSTEMTIME& time) {
    return L"Simpsons-" + padded(time.wYear, 4) + padded(time.wMonth, 2) + padded(time.wDay, 2) +
        L"-" + padded(time.wHour, 2) + padded(time.wMinute, 2) + padded(time.wSecond, 2) +
        L"-" + padded(time.wMilliseconds, 3) + L"Z-" + std::to_wstring(GetCurrentProcessId());
}

struct LogFile { fs::path path; Handle handle; };

LogFile createLog(const fs::path& directory, const SYSTEMTIME& time) {
    ensureDirectory(directory);
    const auto stem = logStem(time);
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    for (unsigned int sequence = 0; sequence < 10000; ++sequence) {
        auto path = directory / (stem + L"-" + padded(sequence, 4) + L".log");
        Handle file(CreateFileW(win32Path(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file) return {std::move(path), std::move(file)};
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            failWindows(L"Could not create a game log in:\n" + directory.native(), error);
    }
    throw Failure{L"Could not choose a new game log name. Please try again."};
}

void writeText(HANDLE file, std::wstring_view text) {
    if (text.empty()) return;
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size == 0) failWindows(L"Could not prepare the game log");
    std::string bytes(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
            bytes.data(), size, nullptr, nullptr) == 0) failWindows(L"Could not prepare the game log");
    DWORD offset = 0;
    while (offset < static_cast<DWORD>(bytes.size())) {
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, static_cast<DWORD>(bytes.size()) - offset, &written, nullptr))
            failWindows(L"Could not write the game log");
        if (written == 0) throw Failure{L"Could not write the game log. Check the available disk space."};
        offset += written;
    }
}

DWORD sourceAttributes(const fs::path& path) {
    const DWORD attributes = GetFileAttributesW(win32Path(path).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) failWindows(L"Could not read the source profile/save:\n" + path.native());
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        throw Failure{L"Private launch copies cannot read a source reparse point:\n" + path.native()};
    return attributes;
}

void rejectReparseAncestors(const fs::path& path) {
    const auto absolute = absolutePath(path);
    fs::path component = absolute.root_path();
    sourceAttributes(component);
    for (const auto& part : absolute.relative_path()) {
        component /= part;
        sourceAttributes(component);
    }
}

BY_HANDLE_FILE_INFORMATION sourceInformation(HANDLE handle, const fs::path& path, bool directory) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)) failWindows(L"Could not inspect the source profile/save:\n" + path.native());
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        throw Failure{L"Private launch copies cannot read a source reparse point:\n" + path.native()};
    if (((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory || GetFileType(handle) != FILE_TYPE_DISK)
        throw Failure{L"The source profile/save has an unexpected file type:\n" + path.native()};
    return info;
}

void copySourceFile(const fs::path& source, const fs::path& destination) {
    // Opening the entry itself prevents a substituted file symlink from being
    // followed. The source handle also refuses concurrent writes and renames.
    Handle input(CreateFileW(win32Path(source).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!input) failWindows(L"Could not open the source profile/save:\n" + source.native());
    const auto info = sourceInformation(input.get(), source, false);
    Handle output(CreateFileW(win32Path(destination).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!output) failWindows(L"Could not create the private profile/save:\n" + destination.native());
    std::array<char, 65536> buffer{};
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(input.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
            failWindows(L"Could not read the source profile/save:\n" + source.native());
        if (read == 0) break;
        DWORD offset = 0;
        while (offset < read) {
            DWORD written = 0;
            if (!WriteFile(output.get(), buffer.data() + offset, read - offset, &written, nullptr))
                failWindows(L"Could not write the private profile/save:\n" + destination.native());
            if (written == 0) throw Failure{L"Could not complete the private copy. Check the available disk space."};
            offset += written;
        }
    }
    if (!SetFileTime(output.get(), &info.ftCreationTime, &info.ftLastAccessTime, &info.ftLastWriteTime))
        failWindows(L"Could not preserve the private copy's file times:\n" + destination.native());
}

void copySourceTree(const fs::path& source, const fs::path& destination) {
    // Keep each parent open without delete sharing while its entries are copied.
    Handle directory(CreateFileW(win32Path(source).c_str(), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!directory) failWindows(L"Could not open the source profile/save folder:\n" + source.native());
    sourceInformation(directory.get(), source, true);
    if (!CreateDirectoryW(win32Path(destination).c_str(), nullptr))
        failWindows(L"Could not create the private profile/save folder:\n" + destination.native());
    std::error_code error;
    fs::directory_iterator entries(fs::path(win32Path(source)), error), end;
    while (!error && entries != end) {
        const auto entry = source / entries->path().filename();
        const auto copied = destination / entry.filename();
        const DWORD attributes = sourceAttributes(entry);
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) copySourceTree(entry, copied);
        else copySourceFile(entry, copied);
        entries.increment(error);
    }
    if (error) throw Failure{L"Could not enumerate the source profile/save folder:\n" + source.native() +
        L"\nError " + std::to_wstring(error.value()) + L"."};
}

struct PrivateRun { fs::path directory; GamePaths paths; };

PrivateRun createPrivateRun(const GamePaths& source, const SYSTEMTIME& time,
        LaunchMode mode = LaunchMode::FirstMissionCompletion) {
    if (mode != LaunchMode::FirstMissionCompletion && mode != LaunchMode::BartmanBegins)
        throw Failure{L"A private launch requires a supported mission mode."};
    const bool bartman = mode == LaunchMode::BartmanBegins;
    const auto parent = source.root / L"build" / (bartman ? L"bartman-begins-runs" : L"mission-completion-runs");
    rejectReparseAncestors(parent.parent_path());
    ensureDirectory(parent);
    rejectReparseAncestors(parent);
    for (unsigned int sequence = 0; sequence < 10000; ++sequence) {
        const auto directory = parent / (std::wstring(bartman ? L"BartmanBegins-" : L"Completion-") +
            logStem(time).substr(9) + L"-" + padded(sequence, 4));
        if (CreateDirectoryW(win32Path(directory).c_str(), nullptr)) {
            auto paths = source;
            paths.profileStore = directory / L"profile";
            paths.contentStore = directory / L"content";
            paths.logs = directory / L"logs";
            return {directory, std::move(paths)};
        }
        const DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) failWindows(L"Could not create a private mission run", error);
    }
    throw Failure{L"Could not choose a new private mission run folder. Please try again."};
}

void copyPrivateStores(const GamePaths& source, const PrivateRun& run) {
    rejectReparseAncestors(source.profileStore);
    rejectReparseAncestors(source.contentStore);
    copySourceTree(source.profileStore, run.paths.profileStore);
    copySourceTree(source.contentStore, run.paths.contentStore);
    auto video = source.profileStore;
    video += L".video.cfg";
    const DWORD attributes = GetFileAttributesW(win32Path(video).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND) failWindows(L"Could not read the source video settings:\n" + video.native(), error);
    } else {
        sourceAttributes(video);
        auto copiedVideo = run.paths.profileStore;
        copiedVideo += L".video.cfg";
        copySourceFile(video, copiedVideo);
    }
}

fs::path latestLog(const fs::path& directory) {
    fs::path latest;
    std::error_code error;
    fs::directory_iterator iterator(fs::path(win32Path(directory)), error);
    const fs::directory_iterator end;
    while (!error && iterator != end) {
        const auto path = directory / iterator->path().filename();
        const auto name = path.filename().native();
        if (name.starts_with(L"Simpsons-") && path.extension() == L".log" && regularFile(path) &&
            (latest.empty() || name > latest.filename().native())) latest = path;
        iterator.increment(error);
    }
    return latest;
}

class AttributeList {
public:
    AttributeList() {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (bytes == 0) failWindows(L"Could not prepare the game process");
        list_ = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, bytes));
        if (list_ == nullptr) throw Failure{L"There is not enough memory to start the game."};
        if (!InitializeProcThreadAttributeList(list_, 1, 0, &bytes)) {
            const DWORD error = GetLastError();
            HeapFree(GetProcessHeap(), 0, list_);
            list_ = nullptr;
            failWindows(L"Could not prepare the game process", error);
        }
    }
    ~AttributeList() {
        DeleteProcThreadAttributeList(list_);
        HeapFree(GetProcessHeap(), 0, list_);
    }
    AttributeList(const AttributeList&) = delete;
    AttributeList& operator=(const AttributeList&) = delete;
    LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept { return list_; }
private:
    LPPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
};

Handle startGame(const GamePaths& paths, LogFile& log, LaunchMode mode) {
    auto command = gameCommand(paths, mode); // CreateProcessW requires writable storage.
    writeText(log.handle.get(), L"The Simpsons Game - native development build\r\n"
        L"Working folder: " + paths.root.native() + L"\r\nCommand: " + command + L"\r\n\r\n");
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!input) failWindows(L"Could not prepare the game's input");
    AttributeList attributes;
    std::array<HANDLE, 2> inherited{input.get(), log.handle.get()};
    if (!UpdateProcThreadAttribute(attributes.get(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited.data(), sizeof(inherited), nullptr, nullptr)) failWindows(L"Could not prepare the game's log output");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.get();
    startup.StartupInfo.hStdOutput = log.handle.get();
    startup.StartupInfo.hStdError = log.handle.get();
    startup.lpAttributeList = attributes.get();
    PROCESS_INFORMATION process{};
    // No job object, timeout, hidden-window hint, or shutdown linkage. Suppress
    // only the console: the game's native window retains its normal visibility.
    if (!CreateProcessW(paths.executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, paths.root.c_str(),
            &startup.StartupInfo, &process)) {
        const auto failure = windowsError(L"The game could not start");
        writeText(log.handle.get(), failure + L"\r\n");
        throw Failure{failure};
    }
    Handle child(process.hProcess);
    Handle thread(process.hThread);
    return child;
}

struct PreparedLaunch { GamePaths paths; LogFile log; };

PreparedLaunch prepareLaunch(const GamePaths& source, LaunchMode mode, const SYSTEMTIME& time) {
    if (!regularFile(source.executable) || !regularFile(source.image))
        throw Failure{L"A required game file is missing. Complete the native build and try again.\n\n"
            L"Required files:\n" + source.executable.native() + L"\n" + source.image.native()};
    const auto profile = source.profileStore / (std::wstring(kProfileId) + L".profile");
    const auto save = source.contentStore / L"save-index" / kProfileId / L"45410809" / L"SIMPSONS_SLOT1.save";
    if (!regularFile(profile) || !regularFile(save))
        throw Failure{L"The recorded Player profile or save is missing. Restore the main-menu profile/content data before launching.\n\n"
            L"Required files:\n" + profile.native() + L"\n" + save.native()};
    auto launchPaths = source;
    std::optional<PrivateRun> privateRun;
    if (mode == LaunchMode::FirstMissionCompletion || mode == LaunchMode::BartmanBegins) {
        privateRun = createPrivateRun(source, time, mode);
        launchPaths = privateRun->paths;
    }
    auto log = createLog(launchPaths.logs, time);
    if (privateRun) {
        const auto label = mode == LaunchMode::BartmanBegins ? L"Bartman Begins direct launch." : L"First-mission completion launch.";
        writeText(log.handle.get(), std::wstring(label) + L" Original stores remain unchanged.\r\n"
            L"Private run: " + privateRun->directory.native() + L"\r\n"
            L"Source profile: " + source.profileStore.native() + L"\r\n"
            L"Source content: " + source.contentStore.native() + L"\r\n");
        try {
            copyPrivateStores(source, *privateRun);
        } catch (const Failure& failure) {
            writeText(log.handle.get(), L"Private copy failed:\r\n" + failure.message + L"\r\n");
            throw Failure{failure.message + L"\n\nThe partial private copy and log were kept in:\n" + privateRun->directory.native()};
        } catch (...) {
            writeText(log.handle.get(), L"Private copy failed unexpectedly. Partial files were retained.\r\n");
            throw Failure{L"Could not copy the profile and save. The partial private copy and log were kept in:\n" + privateRun->directory.native()};
        }
    }
    return {std::move(launchPaths), std::move(log)};
}

int dispatchGame(const GamePaths& source, LaunchMode mode) {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    auto launch = prepareLaunch(source, mode, time);
    try {
        auto child = startGame(launch.paths, launch.log, mode);
        // Close only the parent's process/log handles. The game keeps running
        // with its inherited log and NUL input; no launcher window is created.
        child.reset();
        return 0;
    } catch (const Failure& failure) {
        throw Failure{failure.message + L"\n\nGame log:\n" + launch.log.path.native()};
    }
}

void require(bool condition, const wchar_t* message) {
    if (!condition) throw Failure{std::wstring(L"Self-test failed: ") + message};
}

// A newly created, exclusively owned directory is the only tree tests can remove.
// No real game file is copied, opened for writing, or executed.
class TestDirectory {
public:
    explicit TestDirectory(const fs::path& parent) {
        ensureDirectory(parent);
        std::error_code error;
        parent_ = canonicalPath(parent, error);
        if (error) throw Failure{L"Could not resolve the self-test parent folder."};
        if (!parent_.is_absolute()) throw Failure{L"The self-test parent must be an absolute folder."};
        for (unsigned int sequence = 0; sequence < 1000; ++sequence) {
            auto candidate = parent_ / (L"launcher-self-test-" + std::to_wstring(GetCurrentProcessId()) +
                L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(sequence));
            if (CreateDirectoryW(win32Path(candidate).c_str(), nullptr)) {
                path = std::move(candidate);
                return;
            }
            if (GetLastError() != ERROR_ALREADY_EXISTS) failWindows(L"Could not create the self-test folder");
        }
        throw Failure{L"Could not allocate a self-test folder."};
    }
    ~TestDirectory() {
        if (!path.empty()) {
            std::error_code error;
            const auto resolvedParent = canonicalPath(parent_, error);
            if (error || resolvedParent != parent_) return;
            const auto resolvedTarget = canonicalPath(path, error);
            // Re-resolve before recursive removal. A moved/replaced parent or a
            // redirected test directory must never expand our cleanup boundary.
            if (error || !resolvedTarget.is_absolute() || resolvedTarget != path ||
                resolvedTarget == parent_ || resolvedTarget.parent_path() != parent_) return;
            // A failed junction fixture can leave its link behind. Scan the
            // root, ancestors and descendants without following reparse points;
            // preserve the fixture when any link or scan error remains.
            try {
                for (auto ancestor = path; !ancestor.empty();) {
                    sourceAttributes(ancestor);
                    const auto next = ancestor.parent_path();
                    if (next == ancestor) break;
                    ancestor = next;
                }
                fs::recursive_directory_iterator entry(fs::path(win32Path(path)), fs::directory_options::none, error), end;
                if (error) return;
                while (entry != end) {
                    sourceAttributes(entry->path());
                    entry.increment(error);
                    if (error) return;
                }
            } catch (...) {return;}
            fs::remove_all(fs::path(win32Path(resolvedTarget)), error);
        }
    }
    TestDirectory(const TestDirectory&) = delete;
    TestDirectory& operator=(const TestDirectory&) = delete;
    fs::path path;
private:
    fs::path parent_;
};

void fixtureFile(const fs::path& path, std::wstring_view contents = {}) {
    ensureDirectory(path.parent_path());
    Handle file(CreateFileW(win32Path(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) failWindows(L"Could not create a self-test file");
    writeText(file.get(), contents);
}

std::string fixtureContents(const fs::path& path) {
    std::ifstream file(fs::path(win32Path(path)), std::ios::binary);
    if (!file) throw Failure{L"Could not read a self-test file."};
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void fixtureJunction(const fs::path& path, const fs::path& target) {
    if (!CreateDirectoryW(win32Path(path).c_str(), nullptr)) failWindows(L"Could not create the self-test junction folder");
    Handle directory(CreateFileW(win32Path(path).c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!directory) failWindows(L"Could not open the self-test junction folder");
    std::error_code error;
    const auto print = canonicalPath(target, error).native();
    if (error) throw Failure{L"Could not resolve the self-test junction target."};
    const auto substitute = L"\\??\\" + win32Path(fs::path(print)).substr(4);
    struct JunctionData {
        DWORD tag;
        WORD length, reserved;
        WORD substituteOffset, substituteLength, printOffset, printLength;
        wchar_t paths[4096];
    } data{};
    if (substitute.size() + print.size() + 2 > std::size(data.paths))
        throw Failure{L"The self-test junction path is too long."};
    data.tag = IO_REPARSE_TAG_MOUNT_POINT;
    data.substituteLength = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    data.printOffset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t));
    data.printLength = static_cast<WORD>(print.size() * sizeof(wchar_t));
    data.length = static_cast<WORD>(8 + (substitute.size() + print.size() + 2) * sizeof(wchar_t));
    std::copy(substitute.begin(), substitute.end(), data.paths);
    std::copy(print.begin(), print.end(), data.paths + substitute.size() + 1);
    DWORD returned = 0;
    if (!DeviceIoControl(directory.get(), FSCTL_SET_REPARSE_POINT, &data, DWORD(data.length) + 8,
            nullptr, 0, &returned, nullptr)) failWindows(L"Could not create the self-test junction");
}

int selfTest() {
    const auto ownExecutable = modulePath();
    require(win32Path(L"C:\\fixture\\folder\\..\\file") == L"\\\\?\\C:\\fixture\\file", L"extended drive path differs");
    require(win32Path(L"\\\\server\\share\\folder\\..\\file") == L"\\\\?\\UNC\\server\\share\\file", L"extended UNC path differs");
    require(win32Path(L"\\\\?\\C:\\fixture\\file") == L"\\\\?\\C:\\fixture\\file", L"extended drive path was prefixed twice");
    require(win32Path(L"\\\\?\\unc\\server\\share\\file") == L"\\\\?\\UNC\\server\\share\\file", L"extended UNC path was prefixed twice");
    // This mode also works before the game/image is built. All temporary files
    // live beside the launcher, under a uniquely owned self-test directory.
    TestDirectory fixture(ownExecutable.parent_path());
    const auto root = fixture.path / L"Game space \u00e9\u6e2c\u8a66 & (test)!";
    const auto paths = pathsAt(root);
    const auto launcher = paths.executable.parent_path() / L"SimpsonsLauncher.exe";
    fixtureFile(launcher);
    fixtureFile(paths.executable);
    // A complete real workspace may be an ancestor. It must never be modified.
    const auto before = findRoot(launcher);
    require(!before || before->root != root, L"missing image accepted");
    ensureDirectory(paths.image);
    const auto directoryImage = findRoot(launcher);
    require(!directoryImage || directoryImage->root != root, L"directory accepted as image");
    fs::remove(fs::path(win32Path(paths.image)));
    fixtureFile(paths.image);
    const auto found = findRoot(launcher);
    require(found && found->root == root, L"nearest complete root not found");
    require(found->executable == paths.executable && found->image == paths.image &&
        found->profileStore == paths.profileStore && found->contentStore == paths.contentStore && found->logs == paths.logs,
        L"discovered paths changed");
    require(!findRoot(L"relative\\SimpsonsLauncher.exe"), L"relative path accepted");
    const auto command = splitCommand(gameCommand(paths));
    require(command == std::vector<std::wstring>{paths.executable.native(), L"--image", paths.image.native(),
        L"--profile-store", paths.profileStore.native(), L"--content-store", paths.contentStore.native(), L"--local-profile", kLocalProfile},
        L"Unicode game command did not round-trip");
    const auto defaultOption = parseOptions({L"launcher.exe"});
    require(defaultOption.mode == LaunchMode::Normal && defaultOption.action == LauncherAction::DispatchGame,
        L"ordinary launcher no longer dispatches directly");
    const auto completionOption = parseOptions({L"launcher.exe", L"--first-mission-completion"});
    require(completionOption.mode == LaunchMode::FirstMissionCompletion && completionOption.action == LauncherAction::DispatchGame,
        L"completion launch option not selected");
    const auto bartmanOption = parseOptions({L"launcher.exe", L"--bartman-begins"});
    require(bartmanOption.mode == LaunchMode::BartmanBegins && bartmanOption.action == LauncherAction::DispatchGame,
        L"Bartman Begins launch option not selected for direct dispatch");
    require(parseOptions({L"launcher.exe", L"--self-test"}).action == LauncherAction::SelfTest, L"self-test option not selected");
    for (const auto& rejected : std::vector<std::vector<std::wstring>>{
        {}, {L"launcher.exe", L"--unknown"},
        {L"launcher.exe", L"--first-mission-completion", L"--first-mission-completion"},
        {L"launcher.exe", L"--first-mission-completion", L"--self-test"},
        {L"launcher.exe", L"--self-test", L"--first-mission-completion"},
        {L"launcher.exe", L"--bartman-begins", L"--bartman-begins"},
        {L"launcher.exe", L"--bartman-begins", L"--first-mission-completion"},
        {L"launcher.exe", L"--first-mission-completion", L"--bartman-begins"},
        {L"launcher.exe", L"--bartman-begins", L"--self-test"},
        {L"launcher.exe", L"--self-test", L"--bartman-begins"}}) {
        bool refused = false;
        try { parseOptions(rejected); } catch (const Failure&) { refused = true; }
        require(refused, L"unknown, duplicate or combined launcher option accepted");
    }
    auto completionCommand = command;
    completionCommand.push_back(L"--first-mission-completion");
    require(splitCommand(gameCommand(paths, LaunchMode::FirstMissionCompletion)) == completionCommand,
        L"completion game command did not round-trip");
    auto bartmanCommand = command;
    bartmanCommand.push_back(L"--bartman-begins");
    require(splitCommand(gameCommand(paths, LaunchMode::BartmanBegins)) == bartmanCommand,
        L"Bartman Begins Unicode game command did not round-trip or selected completion");
    const std::vector<std::wstring> cases{L"", L"plain", L"two words", L"tab\there", L"a\"b",
        L"C:\\space dir\\", L"\\\\server\\share\\", L"a\\\\\"b", L"\"", L"\\",
        L"\u00e9\u6e2c\u8a66 & %PATH% ! (x)"};
    for (const auto& argument : cases) {
        const auto parsed = splitCommand(L"test.exe " + quoteArgument(argument));
        require(parsed.size() == 2 && parsed[1] == argument, L"argument quoting did not round-trip");
    }
    SYSTEMTIME time{};
    GetSystemTime(&time);
    auto first = createLog(paths.logs, time);
    writeText(first.handle.get(), L"preserve this log\r\n");
    auto second = createLog(paths.logs, time); // Force a same-millisecond collision.
    require(first.path != second.path, L"log name collision");
    LARGE_INTEGER bytes{};
    require(GetFileSizeEx(first.handle.get(), &bytes) && bytes.QuadPart == 19, L"existing log overwritten");
    require(first.path.parent_path() == paths.logs && second.path.parent_path() == paths.logs, L"log escaped its folder");
    require(latestLog(paths.logs) == second.path, L"latest log selection failed");
    DWORD flags = 0;
    require(GetHandleInformation(second.handle.get(), &flags) && (flags & HANDLE_FLAG_INHERIT) != 0,
        L"log handle not inheritable");
    const auto sourceProfile = paths.profileStore / (std::wstring(kProfileId) + L".profile");
    const auto sourceSave = paths.contentStore / L"save-index" / kProfileId / L"45410809" / L"SIMPSONS_SLOT1.save";
    auto sourceVideo = paths.profileStore;
    sourceVideo += L".video.cfg";
    fixtureFile(sourceProfile, L"profile source bytes");
    fixtureFile(paths.profileStore / L"preferences" / L"nested.cfg", L"nested profile bytes");
    fixtureFile(sourceSave, L"save source bytes");
    fixtureFile(sourceVideo, L"video source bytes");
    const auto longRelative = fs::path(L"long-path-coverage") / std::wstring(MAX_PATH - 80, L'x') / L"save-copy.bin";
    const auto longSource = paths.contentStore / longRelative;
    require(longSource.native().size() > static_cast<size_t>(MAX_PATH), L"source fixture no longer exceeds MAX_PATH");
    fixtureFile(longSource, L"long source save bytes");
    {
        auto missingProfile = paths;
        missingProfile.profileStore = root / L"build" / L"missing-direct-profile";
        missingProfile.logs = root / L"build" / L"missing-direct-logs";
        bool rejectedMissingProfile = false;
        try { prepareLaunch(missingProfile, LaunchMode::Normal, time); } catch (const Failure& failure) {
            rejectedMissingProfile = failure.message.find(L"profile or save is missing") != std::wstring::npos;
        }
        require(rejectedMissingProfile && !fs::exists(fs::path(win32Path(missingProfile.logs))),
            L"direct launch accepted missing profile or created its log before validation");
        bool bartmanRejectedMissingProfile = false;
        try { prepareLaunch(missingProfile, LaunchMode::BartmanBegins, time); } catch (const Failure& failure) {
            bartmanRejectedMissingProfile = failure.message.find(L"profile or save is missing") != std::wstring::npos;
        }
        require(bartmanRejectedMissingProfile && !fs::exists(fs::path(win32Path(root / L"build" / L"bartman-begins-runs"))),
            L"Bartman Begins accepted missing profile or created a private run before validation");
        auto normalLaunch = prepareLaunch(paths, LaunchMode::Normal, time);
        require(normalLaunch.paths.profileStore == paths.profileStore && normalLaunch.paths.contentStore == paths.contentStore &&
            normalLaunch.paths.logs == paths.logs && regularFile(normalLaunch.log.path), L"direct normal launch preparation changed stores or logs");
        require(splitCommand(gameCommand(normalLaunch.paths)) == command, L"direct normal launch command changed");
        auto completionLaunch = prepareLaunch(paths, LaunchMode::FirstMissionCompletion, time);
        require(completionLaunch.paths.profileStore != paths.profileStore && completionLaunch.paths.contentStore != paths.contentStore &&
            regularFile(completionLaunch.log.path), L"direct completion launch preparation did not isolate stores or create its log");
        require(fixtureContents(completionLaunch.paths.profileStore / sourceProfile.filename()) == "profile source bytes" &&
            fixtureContents(completionLaunch.paths.contentStore / sourceSave.lexically_relative(paths.contentStore)) == "save source bytes",
            L"direct completion launch preparation did not copy the selected profile/save");
        const auto completionLog = fixtureContents(completionLaunch.log.path);
        require(completionLog.find("First-mission completion launch") != std::string::npos,
            L"direct completion log is missing its launch mode");
        auto bartmanLaunch = prepareLaunch(paths, LaunchMode::BartmanBegins, time);
        const auto bartmanDirectory = bartmanLaunch.paths.profileStore.parent_path();
        require(bartmanDirectory.parent_path() == root / L"build" / L"bartman-begins-runs" &&
            bartmanDirectory.filename().native().starts_with(L"BartmanBegins-") &&
            bartmanDirectory != completionLaunch.paths.profileStore.parent_path() &&
            bartmanLaunch.paths.logs == bartmanDirectory / L"logs" && regularFile(bartmanLaunch.log.path),
            L"Bartman Begins run or log escaped its distinct private folder");
        const auto bartmanProfile = bartmanLaunch.paths.profileStore / sourceProfile.filename();
        const auto bartmanSave = bartmanLaunch.paths.contentStore / sourceSave.lexically_relative(paths.contentStore);
        auto bartmanVideo = bartmanLaunch.paths.profileStore;
        bartmanVideo += L".video.cfg";
        require(fixtureContents(bartmanProfile) == "profile source bytes" && fixtureContents(bartmanSave) == "save source bytes" &&
            fixtureContents(bartmanVideo) == "video source bytes" &&
            fixtureContents(bartmanLaunch.paths.profileStore / L"preferences" / L"nested.cfg") == "nested profile bytes" &&
            fixtureContents(bartmanLaunch.paths.contentStore / longRelative) == "long source save bytes",
            L"Bartman Begins did not copy exact profile, content, nested, video and long-path data");
        const auto bartmanPrivateCommand = splitCommand(gameCommand(bartmanLaunch.paths, LaunchMode::BartmanBegins));
        require(bartmanPrivateCommand[4] == bartmanLaunch.paths.profileStore.native() &&
            bartmanPrivateCommand[6] == bartmanLaunch.paths.contentStore.native() &&
            bartmanPrivateCommand.back() == L"--bartman-begins" && bartmanPrivateCommand.size() == bartmanCommand.size(),
            L"Bartman Begins command used source stores or an extra startup mode");
        const auto bartmanLog = fixtureContents(bartmanLaunch.log.path);
        require(bartmanLog.find("Bartman Begins direct launch") != std::string::npos &&
            bartmanLog.find("First-mission completion launch") == std::string::npos,
            L"Bartman Begins log did not retain its distinct launch mode");
        for (const auto& privateFile : std::array<fs::path, 3>{bartmanProfile, bartmanSave, bartmanVideo}) {
            Handle change(CreateFileW(win32Path(privateFile).c_str(), GENERIC_WRITE, 0, nullptr,
                TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            require(bool(change), L"Bartman Begins private store could not be written");
            writeText(change.get(), L"Bartman Begins private bytes");
        }
        require(fixtureContents(sourceProfile) == "profile source bytes" && fixtureContents(sourceSave) == "save source bytes" &&
            fixtureContents(sourceVideo) == "video source bytes" &&
            fixtureContents(completionLaunch.paths.contentStore / sourceSave.lexically_relative(paths.contentStore)) == "save source bytes",
            L"Bartman Begins private writes changed a source or completion store");
        const auto collisionRun = createPrivateRun(paths, time, LaunchMode::BartmanBegins);
        require(collisionRun.directory != bartmanDirectory && collisionRun.directory.parent_path() == bartmanDirectory.parent_path(),
            L"Bartman Begins same-timestamp run collided or escaped its folder");
    }
    const auto firstRun = createPrivateRun(paths, time);
    const auto secondRun = createPrivateRun(paths, time);
    require(firstRun.directory != secondRun.directory, L"completion run collision");
    require(firstRun.directory.parent_path() == root / L"build" / L"mission-completion-runs",
        L"completion run escaped its folder");
    copyPrivateStores(paths, firstRun);
    const auto privateProfile = firstRun.paths.profileStore / sourceProfile.filename();
    const auto privateSave = firstRun.paths.contentStore / sourceSave.lexically_relative(paths.contentStore);
    const auto longPrivateSave = firstRun.paths.contentStore / longRelative;
    require(longPrivateSave.native().size() > static_cast<size_t>(MAX_PATH), L"private fixture no longer exceeds MAX_PATH");
    auto privateVideo = firstRun.paths.profileStore;
    privateVideo += L".video.cfg";
    require(fixtureContents(privateProfile) == "profile source bytes" && fixtureContents(privateSave) == "save source bytes" &&
        fixtureContents(privateVideo) == "video source bytes", L"private copy bytes changed");
    require(regularFile(longPrivateSave) && fixtureContents(longPrivateSave) == "long source save bytes",
        L"long private save could not be copied or read");
    require(fixtureContents(firstRun.paths.profileStore / L"preferences" / L"nested.cfg") == "nested profile bytes",
        L"nested profile data not copied");
    const auto privateCommand = splitCommand(gameCommand(firstRun.paths, LaunchMode::FirstMissionCompletion));
    require(privateCommand[4] == firstRun.paths.profileStore.native() && privateCommand[6] == firstRun.paths.contentStore.native() &&
        privateCommand.back() == L"--first-mission-completion", L"completion command used original stores");
    require(privateCommand[4] != paths.profileStore.native() && privateCommand[6] != paths.contentStore.native(),
        L"completion stores not isolated");
    Handle altered(CreateFileW(win32Path(privateSave).c_str(), GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    require(bool(altered), L"private save could not be changed");
    writeText(altered.get(), L"private autosave bytes");
    altered.reset();
    require(fixtureContents(sourceSave) == "save source bytes" && fixtureContents(sourceProfile) == "profile source bytes" &&
        fixtureContents(sourceVideo) == "video source bytes", L"private write changed an original store");
    Handle longAltered(CreateFileW(win32Path(longPrivateSave).c_str(), GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    require(bool(longAltered), L"long private save could not be changed");
    writeText(longAltered.get(), L"long private autosave bytes");
    longAltered.reset();
    require(fixtureContents(longPrivateSave) == "long private autosave bytes" && fixtureContents(longSource) == "long source save bytes",
        L"long private write changed the original save");
    require(DeleteFileW(win32Path(sourceVideo).c_str()) != FALSE, L"optional video fixture could not be removed");
    copyPrivateStores(paths, secondRun);
    auto missingVideo = secondRun.paths.profileStore;
    missingVideo += L".video.cfg";
    require(!regularFile(missingVideo), L"absent video preferences were invented");
    const auto bartmanWithoutVideo = createPrivateRun(paths, time, LaunchMode::BartmanBegins);
    copyPrivateStores(paths, bartmanWithoutVideo);
    auto bartmanMissingVideo = bartmanWithoutVideo.paths.profileStore;
    bartmanMissingVideo += L".video.cfg";
    require(!regularFile(bartmanMissingVideo), L"Bartman Begins invented absent video preferences");
    const auto junction = paths.profileStore / L"redirected";
    fixtureJunction(junction, paths.contentStore);
    const auto refusedRun = createPrivateRun(paths, time);
    bool refused = false;
    try { copyPrivateStores(paths, refusedRun); } catch (const Failure& failure) {
        refused = failure.message.find(L"reparse point") != std::wstring::npos;
    }
    const auto bartmanRefusedRun = createPrivateRun(paths, time, LaunchMode::BartmanBegins);
    bool bartmanRefused = false;
    try { copyPrivateStores(paths, bartmanRefusedRun); } catch (const Failure& failure) {
        bartmanRefused = failure.message.find(L"reparse point") != std::wstring::npos;
    }
    auto redirectedSource = paths;
    redirectedSource.profileStore = junction;
    const auto refusedRootRun = createPrivateRun(paths, time);
    bool refusedRoot = false;
    try { copyPrivateStores(redirectedSource, refusedRootRun); } catch (const Failure& failure) {
        refusedRoot = failure.message.find(L"reparse point") != std::wstring::npos;
    }
    require(RemoveDirectoryW(win32Path(junction).c_str()) != FALSE, L"self-test junction could not be removed");
    require(refused && fs::is_directory(fs::path(win32Path(refusedRun.directory))) && fs::is_directory(fs::path(win32Path(refusedRun.paths.profileStore))),
        L"source reparse point accepted or partial copy removed");
    require(refusedRoot && !fs::exists(fs::path(win32Path(refusedRootRun.paths.profileStore))), L"source root junction accepted");
    require(bartmanRefused && fs::is_directory(fs::path(win32Path(bartmanRefusedRun.directory))) &&
        !regularFile(bartmanRefusedRun.paths.profileStore / L"redirected" / L"save-index" / kProfileId / L"45410809" / L"SIMPSONS_SLOT1.save"),
        L"Bartman Begins followed a source junction or removed its partial copy");
    require(!regularFile(refusedRun.paths.profileStore / L"redirected" / L"save-index" / kProfileId / L"45410809" / L"SIMPSONS_SLOT1.save"),
        L"source junction was followed");
    require(fixtureContents(sourceSave) == "save source bytes", L"reparse refusal changed the original save");
    return 0;
}

void testOutput(std::wstring_view message) noexcept {
    try {
        OutputDebugStringW(std::wstring(message).c_str());
        const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        if (output != nullptr && output != INVALID_HANDLE_VALUE) writeText(output, message);
    } catch (...) { /* Test exit status remains available without a console. */ }
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    bool testing = false;
    try {
        const auto arguments = splitCommand(GetCommandLineW());
        testing = arguments.size() >= 2 && arguments[1] == L"--self-test";
        const auto options = parseOptions(arguments);
        if (options.action == LauncherAction::SelfTest) {
            const int result = selfTest();
            testOutput(L"Launcher self-test passed. No game was started.\r\n");
            return result;
        }
        const auto paths = findRoot(modulePath());
        if (!paths) throw Failure{L"Game files are missing. Keep SimpsonsLauncher.exe in build\\native and complete the native build.\n\nSee docs\\launcher.md for setup help."};
        return dispatchGame(*paths, options.mode);
    } catch (const Failure& failure) {
        if (testing) testOutput(failure.message + L"\r\n");
        else MessageBoxW(nullptr, failure.message.c_str(), kTitle, MB_OK | MB_ICONERROR);
    } catch (...) {
        if (testing) testOutput(L"Launcher self-test failed unexpectedly.\r\n");
        else MessageBoxW(nullptr, L"The game could not start. Please check the native build and try again.", kTitle, MB_OK | MB_ICONERROR);
    }
    return 1;
}
