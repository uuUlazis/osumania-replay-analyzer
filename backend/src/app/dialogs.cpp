#include "app/dialogs.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <shlwapi.h>

#include <vector>

namespace mra {

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                                   nullptr, 0, nullptr, nullptr);
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), size,
                        nullptr, nullptr);
    return out;
}

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(),
                                   nullptr, 0);
    std::wstring out(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), size);
    return out;
}

static std::optional<std::string> runDialog(bool pickFolder,
                                            const std::wstring& title,
                                            const std::vector<DialogFilter>& filters,
                                            const std::wstring& initialDir = L"") {
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) return std::nullopt;

    std::optional<std::string> result;
    DWORD options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (pickFolder) options |= FOS_PICKFOLDERS;
    dialog->SetOptions(options);
    dialog->SetTitle(title.c_str());

    std::vector<COMDLG_FILTERSPEC> specs;
    std::vector<std::wstring> names, patterns;
    // COMDLG_FILTERSPEC holds raw pointers, so the backing strings must never
    // reallocate: without reserve(), push_back growth invalidates the c_str()
    // captured for earlier entries and the filter matches nothing.
    names.reserve(filters.size());
    patterns.reserve(filters.size());
    for (const DialogFilter& f : filters) {
        names.push_back(f.name);
        patterns.push_back(f.pattern);
        specs.push_back({names.back().c_str(), patterns.back().c_str()});
    }
    if (!specs.empty()) dialog->SetFileTypes((UINT)specs.size(), specs.data());

    // 上次位置: non-empty initialDir forces the dialog to open there (the
    // caller passes the path remembered in last_state). Parse failure is
    // silently ignored - the dialog then opens at the shell default.
    if (!initialDir.empty()) {
        IShellItem* start = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initialDir.c_str(), nullptr,
                                                  IID_PPV_ARGS(&start))) &&
            start) {
            dialog->SetFolder(start);
            start->Release();
        }
    }

    if (SUCCEEDED(dialog->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                result = wideToUtf8(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

std::optional<std::string> openFileDialog(const std::wstring& title,
                                          const std::vector<DialogFilter>& filters) {
    return runDialog(false, title, filters);
}

std::optional<std::string> openFolderDialog(const std::wstring& title,
                                            const std::wstring& initialDir) {
    return runDialog(true, title, {}, initialDir);
}

}  // namespace mra
