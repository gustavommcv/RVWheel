#include "LauncherCore.hpp"

#include <algorithm>
#include <cwctype>
#include <sstream>

namespace rvwheel::tools::launcher {

namespace {

[[nodiscard]] std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return std::towlower(ch); });
    return value;
}

void AppendUnique(std::vector<std::filesystem::path>& paths, std::filesystem::path candidate) {
    if (candidate.empty()) {
        return;
    }
    candidate = candidate.lexically_normal();
    const std::wstring key = Lower(candidate.native());
    const auto duplicate = std::find_if(paths.begin(), paths.end(), [&](const auto& existing) {
        return Lower(existing.lexically_normal().native()) == key;
    });
    if (duplicate == paths.end()) {
        paths.push_back(std::move(candidate));
    }
}

[[nodiscard]] std::wstring UnescapeVdfPath(std::wstring value) {
    std::wstring result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == L'\\' && index + 1 < value.size() && value[index + 1] == L'\\') {
            result.push_back(L'\\');
            ++index;
        } else {
            result.push_back(value[index]);
        }
    }
    return result;
}

[[nodiscard]] std::string Trim(std::string value) {
    const auto isWhitespace = [](unsigned char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; };
    while (!value.empty() && isWhitespace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && isWhitespace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

[[nodiscard]] bool IsModLine(std::string_view line, std::string_view modName) {
    const std::string trimmed = Trim(std::string(line));
    if (trimmed.size() < modName.size() || trimmed.compare(0, modName.size(), modName) != 0) {
        return false;
    }
    std::size_t cursor = modName.size();
    while (cursor < trimmed.size() && (trimmed[cursor] == ' ' || trimmed[cursor] == '\t')) {
        ++cursor;
    }
    return cursor < trimmed.size() && trimmed[cursor] == ':';
}

} // namespace

std::vector<std::filesystem::path> ParseSteamLibraryRoots(std::wstring_view vdfText,
                                                          const std::filesystem::path& defaultSteamRoot) {
    std::vector<std::filesystem::path> roots;
    AppendUnique(roots, defaultSteamRoot);

    std::size_t cursor = 0;
    while (cursor < vdfText.size()) {
        const std::size_t key = vdfText.find(L"\"path\"", cursor);
        if (key == std::wstring_view::npos) {
            break;
        }
        const std::size_t openingQuote = vdfText.find(L'\"', key + 6);
        if (openingQuote == std::wstring_view::npos) {
            break;
        }
        std::size_t closingQuote = openingQuote + 1;
        while (closingQuote < vdfText.size()) {
            if (vdfText[closingQuote] == L'\"' && vdfText[closingQuote - 1] != L'\\') {
                break;
            }
            ++closingQuote;
        }
        if (closingQuote >= vdfText.size()) {
            break;
        }
        AppendUnique(roots, std::filesystem::path(UnescapeVdfPath(
                                std::wstring(vdfText.substr(openingQuote + 1, closingQuote - openingQuote - 1)))));
        cursor = closingQuote + 1;
    }
    return roots;
}

std::string EnableUe4ssMod(std::string_view existingText, std::string_view modName) {
    const std::string newline = existingText.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    std::vector<std::string> lines;
    std::istringstream input{std::string(existingText)};
    std::string line;
    bool replaced = false;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (IsModLine(line, modName)) {
            if (!replaced) {
                lines.emplace_back(std::string(modName) + " : 1");
                replaced = true;
            }
        } else {
            lines.push_back(std::move(line));
        }
    }

    if (!replaced) {
        const auto builtInMarker = std::find_if(lines.begin(), lines.end(), [](const std::string& candidate) {
            return candidate.find("Built-in keybinds") != std::string::npos;
        });
        lines.insert(builtInMarker, std::string(modName) + " : 1");
    }

    std::ostringstream output;
    for (const auto& outputLine : lines) {
        output << outputLine << newline;
    }
    return output.str();
}

std::vector<std::filesystem::path> BridgeExecutableCandidates(const std::filesystem::path& launcherExecutable) {
    const auto launcherDirectory = launcherExecutable.parent_path();
    const auto configName = launcherDirectory.filename();
    const auto toolsBuildDirectory = launcherDirectory.parent_path().parent_path();
    return {
        launcherDirectory / L"rvwheel_device_probe.exe",
        toolsBuildDirectory / L"device_probe" / configName / L"rvwheel_device_probe.exe",
    };
}

} // namespace rvwheel::tools::launcher
