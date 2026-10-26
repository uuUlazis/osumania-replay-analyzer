#pragma once
// Native Windows file/folder pickers (IFileOpenDialog). All user file access
// flows through these pickers - the frontend never touches the filesystem.
#include <optional>
#include <string>
#include <vector>

namespace mra {

struct DialogFilter {
    const wchar_t* name;      // e.g. L"osu! replay"
    const wchar_t* pattern;   // e.g. L"*.osr"
};

// Returns nullopt when the user cancels or COM fails.
std::optional<std::string> openFileDialog(const std::wstring& title,
                                          const std::vector<DialogFilter>& filters);
// initialDir non-empty -> the dialog opens at that folder (上次位置).
std::optional<std::string> openFolderDialog(const std::wstring& title,
                                            const std::wstring& initialDir = L"");

// Converts a UTF-16 Win32 path to UTF-8 std::string.
std::string wideToUtf8(const std::wstring& w);
std::wstring utf8ToWide(const std::string& s);

}  // namespace mra
