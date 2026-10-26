#include "paths.h"

#include <windows.h>

namespace mra {

std::filesystem::path pathFromUtf8(const std::string& utf8) {
    if (utf8.empty()) return {};
    int wideLen = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                        static_cast<int>(utf8.size()),
                                        nullptr, 0);
    if (wideLen <= 0) return std::filesystem::path(utf8);
    std::wstring wide(static_cast<size_t>(wideLen), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                          static_cast<int>(utf8.size()), wide.data(),
                          wideLen);
    return std::filesystem::path(wide);
}

std::string pathToUtf8(const std::filesystem::path& p) {
    const std::wstring& wide = p.native();
    if (wide.empty()) return {};
    int utf8Len = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                        static_cast<int>(wide.size()),
                                        nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return {};
    std::string utf8(static_cast<size_t>(utf8Len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                          static_cast<int>(wide.size()), utf8.data(),
                          utf8Len, nullptr, nullptr);
    return utf8;
}

} // namespace mra
