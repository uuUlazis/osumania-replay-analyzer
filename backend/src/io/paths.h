#pragma once
#include <filesystem>
#include <string>

namespace mra {

// HTTP/JSON strings arriving at the backend are UTF-8, but on Windows
// std::filesystem's narrow-char constructor interprets them in the ANSI
// codepage (GBK on zh-CN systems), mangling any path that contains
// non-ASCII characters. Convert explicitly at every path boundary.
std::filesystem::path pathFromUtf8(const std::string& utf8);

// Inverse of pathFromUtf8 for strings we hand back to the frontend
// (e.g. relative skin resource keys).
std::string pathToUtf8(const std::filesystem::path& p);

} // namespace mra
