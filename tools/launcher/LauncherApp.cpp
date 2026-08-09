#include "LauncherApp.hpp"

#include "LauncherCore.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace rvwheel::tools::launcher {

namespace {

constexpr wchar_t kGameProcessName[] = L"Ride-Win64-Shipping.exe";
constexpr wchar_t kModName[] = L"RVWheel";
constexpr char kModNameNarrow[] = "RVWheel"; // Same name as kModName; mods.txt is narrow text.
constexpr wchar_t kSteamLaunchUri[] = L"steam://rungameid/3949040";
constexpr int kBridgeRateHz = 60; // Matches CliParser::kDefaultRateHz; passed explicitly so a
                                   // future change to either default cannot silently desync them.

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
    ~UniqueHandle() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE handle_ = nullptr;
};

[[nodiscard]] std::filesystem::path ExecutablePath() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path(buffer);
}

[[nodiscard]] std::filesystem::path LocalAppDataPath() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path(buffer);
}

[[nodiscard]] std::optional<std::string> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

[[nodiscard]] std::wstring DecodeText(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (length <= 0) {
        codePage = CP_ACP;
        flags = 0;
        length = MultiByteToWideChar(codePage, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
    }
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(codePage, flags, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

[[nodiscard]] std::filesystem::path SteamRootFromRegistry() {
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, &type, nullptr,
                     &bytes) != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
        return {};
    }
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, &type,
                     value.data(), &bytes) != ERROR_SUCCESS) {
        return {};
    }
    value.resize(wcsnlen_s(value.c_str(), value.size()));
    return std::filesystem::path(value);
}

[[nodiscard]] std::filesystem::path FindGameWin64Directory() {
    std::filesystem::path steamRoot = SteamRootFromRegistry();
    if (steamRoot.empty()) {
        std::wstring programFilesX86(32768, L'\0');
        const DWORD length = GetEnvironmentVariableW(L"ProgramFiles(x86)", programFilesX86.data(),
                                                     static_cast<DWORD>(programFilesX86.size()));
        if (length > 0 && length < programFilesX86.size()) {
            programFilesX86.resize(length);
            steamRoot = std::filesystem::path(programFilesX86) / L"Steam";
        }
    }

    std::wstring libraryText;
    if (!steamRoot.empty()) {
        const auto bytes = ReadBytes(steamRoot / L"steamapps" / L"libraryfolders.vdf");
        if (bytes) {
            libraryText = DecodeText(*bytes);
        }
    }

    for (const auto& library : ParseSteamLibraryRoots(libraryText, steamRoot)) {
        const auto win64 = library / L"steamapps" / L"common" / L"Ride" / L"Ride" / L"Binaries" / L"Win64";
        std::error_code error;
        if (std::filesystem::is_regular_file(win64 / kGameProcessName, error) && !error) {
            return win64;
        }
    }
    return {};
}

[[nodiscard]] bool WriteBytesAtomically(const std::filesystem::path& path, const std::string& contents) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    auto temporary = path;
    temporary += L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.close();
        if (!output) {
            return false;
        }
    }
    return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

[[nodiscard]] bool CopyDirectoryContents(const std::filesystem::path& source, const std::filesystem::path& destination) {
    std::error_code error;
    if (!std::filesystem::is_directory(source, error) || error) {
        return false;
    }
    std::filesystem::create_directories(destination, error);
    if (error) {
        return false;
    }
    for (std::filesystem::recursive_directory_iterator iterator(source, error), end; iterator != end && !error;
         iterator.increment(error)) {
        const auto relative = std::filesystem::relative(iterator->path(), source, error);
        if (error) {
            return false;
        }
        const auto target = destination / relative;
        if (iterator->is_directory(error)) {
            std::filesystem::create_directories(target, error);
        } else if (iterator->is_regular_file(error)) {
            std::filesystem::create_directories(target.parent_path(), error);
            if (!error) {
                std::filesystem::copy_file(iterator->path(), target, std::filesystem::copy_options::overwrite_existing,
                                           error);
            }
        }
        if (error) {
            return false;
        }
    }
    return !error;
}

[[nodiscard]] bool InstallOrUpdateMod(const std::filesystem::path& launcherDirectory,
                                      const std::filesystem::path& gameWin64,
                                      std::wstring& errorMessage) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(gameWin64 / L"dwmapi.dll", error) || error ||
        !std::filesystem::is_regular_file(gameWin64 / L"ue4ss" / L"UE4SS.dll", error) || error) {
        errorMessage = L"UE4SS não foi encontrado na pasta do jogo. Instale o UE4SS validado antes de usar o launcher.";
        return false;
    }

    const auto bundledMod = launcherDirectory / kModName;
    const auto installedMod = gameWin64 / L"ue4ss" / L"Mods" / kModName;
    if (!CopyDirectoryContents(bundledMod, installedMod)) {
        errorMessage = L"Não foi possível copiar o mod RVWheel para a pasta do UE4SS.";
        return false;
    }

    const auto modsList = gameWin64 / L"ue4ss" / L"Mods" / L"mods.txt";
    const std::string current = ReadBytes(modsList).value_or(std::string{});
    if (!WriteBytesAtomically(modsList, EnableUe4ssMod(current, kModNameNarrow))) {
        errorMessage = L"Não foi possível habilitar RVWheel em ue4ss/Mods/mods.txt.";
        return false;
    }
    return true;
}

[[nodiscard]] DWORD FindProcessId(const wchar_t* executableName) {
    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) {
        return 0;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) {
        return 0;
    }
    do {
        if (_wcsicmp(entry.szExeFile, executableName) == 0) {
            return entry.th32ProcessID;
        }
    } while (Process32NextW(snapshot.get(), &entry));
    return 0;
}

[[nodiscard]] UniqueHandle WaitForProcess(const wchar_t* executableName, std::chrono::seconds timeout) {
    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(timeout.count()) * 1000ULL;
    do {
        const DWORD processId = FindProcessId(executableName);
        if (processId != 0) {
            UniqueHandle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
            if (process) {
                return process;
            }
        }
        Sleep(500);
    } while (GetTickCount64() < deadline);
    return {};
}

[[nodiscard]] std::filesystem::path FindBridgeExecutable(const std::filesystem::path& launcherExecutable) {
    for (const auto& candidate : BridgeExecutableCandidates(launcherExecutable)) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            return candidate;
        }
    }
    return {};
}

[[nodiscard]] UniqueHandle StartBridge(const std::filesystem::path& launcherExecutable, std::wstring& errorMessage) {
    const auto bridge = FindBridgeExecutable(launcherExecutable);
    if (bridge.empty()) {
        errorMessage = L"rvwheel_device_probe.exe não foi encontrado ao lado do launcher.";
        return {};
    }

    const auto logDirectory = LocalAppDataPath() / L"RVWheel" / L"logs";
    std::error_code filesystemError;
    std::filesystem::create_directories(logDirectory, filesystemError);
    if (filesystemError) {
        errorMessage = L"Não foi possível criar a pasta de logs do RVWheel.";
        return {};
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    UniqueHandle output(CreateFileW((logDirectory / L"bridge.log").c_str(), FILE_APPEND_DATA,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr));
    UniqueHandle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!output || !input) {
        errorMessage = L"Não foi possível abrir os logs do bridge.";
        return {};
    }

    std::wstring commandLine = L"\"" + bridge.wstring() + L"\" --bridge --rate " +
                               std::to_wstring(kBridgeRateHz) + L" --parent-pid " +
                               std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input.get();
    startup.hStdOutput = output.get();
    startup.hStdError = output.get();
    PROCESS_INFORMATION processInfo{};
    const auto launcherDirectory = launcherExecutable.parent_path();
    if (!CreateProcessW(bridge.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr,
                        launcherDirectory.c_str(), &startup, &processInfo)) {
        errorMessage = L"Não foi possível iniciar o bridge (erro Win32 " + std::to_wstring(GetLastError()) + L").";
        return {};
    }
    CloseHandle(processInfo.hThread);
    return UniqueHandle(processInfo.hProcess);
}

void ShowError(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"RVWheel Launcher", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

} // namespace

int RunLauncher() {
    UniqueHandle singleton(CreateMutexW(nullptr, FALSE, L"Local\\RVWheelLauncher"));
    if (!singleton) {
        ShowError(L"Não foi possível inicializar o launcher.");
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    const auto launcherExecutable = ExecutablePath();
    if (launcherExecutable.empty()) {
        ShowError(L"Não foi possível localizar o próprio executável do launcher.");
        return 1;
    }
    const auto gameWin64 = FindGameWin64Directory();
    if (gameWin64.empty()) {
        ShowError(L"RV There Yet? não foi encontrado em nenhuma biblioteca Steam configurada.");
        return 1;
    }

    std::wstring errorMessage;
    if (!InstallOrUpdateMod(launcherExecutable.parent_path(), gameWin64, errorMessage)) {
        ShowError(errorMessage);
        return 1;
    }

    UniqueHandle launchedBridge;
    if (FindProcessId(kBridgeExecutableFileName) == 0) {
        launchedBridge = StartBridge(launcherExecutable, errorMessage);
        if (!launchedBridge) {
            ShowError(errorMessage);
            return 1;
        }
        if (WaitForSingleObject(launchedBridge.get(), 1500) == WAIT_OBJECT_0) {
            ShowError(L"O bridge encerrou durante a inicialização. Verifique %LOCALAPPDATA%\\RVWheel\\logs\\bridge.log.");
            return 1;
        }
    }

    UniqueHandle gameProcess;
    const DWORD existingGame = FindProcessId(kGameProcessName);
    if (existingGame != 0) {
        gameProcess = UniqueHandle(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, existingGame));
    } else {
        const auto launchResult = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", kSteamLaunchUri, nullptr,
                                                                          nullptr, SW_SHOWNORMAL));
        if (launchResult <= 32) {
            ShowError(L"O Steam recusou a abertura do jogo.");
            return 1;
        }
        gameProcess = WaitForProcess(kGameProcessName, std::chrono::seconds{120});
    }

    if (!gameProcess) {
        ShowError(L"O processo de RV There Yet? não apareceu dentro de 120 segundos.");
        return 1;
    }

    WaitForSingleObject(gameProcess.get(), INFINITE);
    // If this launcher started the bridge, its --parent-pid supervision makes
    // it publish one invalid frame and exit immediately after this return.
    return 0;
}

} // namespace rvwheel::tools::launcher
