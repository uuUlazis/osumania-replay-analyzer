#include "util/debug_log.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace mra::debuglog {
namespace {

std::mutex mu;
bool on = false;

std::filesystem::path exeDir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::filesystem::path p(buf);
        return p.parent_path();
    }
#endif
    return std::filesystem::path(".");
}

std::filesystem::path logPath() {
    std::filesystem::path d = exeDir() / "debug";
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    return d / "debug.log";
}

std::string nowStamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) % 1000;
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tm.tm_hour,
                  tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));
    return buf;
}

}  // namespace

void setEnabled(bool enable) {
    std::lock_guard<std::mutex> lock(mu);
    on = enable;
    if (!on) return;
    const std::filesystem::path p = logPath();
    // Rotate a grown log aside so one long session cannot grow it forever.
    std::error_code ec;
    if (std::filesystem::exists(p, ec) &&
        std::filesystem::file_size(p, ec) > 16u * 1024 * 1024) {
        std::filesystem::rename(p, p.string() + ".old", ec);
    }
    std::ofstream f(p, std::ios::app);
    if (f) f << "[" << nowStamp() << "] debug log enabled\n";
}

bool enabled() {
    std::lock_guard<std::mutex> lock(mu);
    return on;
}

void log(const std::string& line) {
    if (!on) return;
    std::lock_guard<std::mutex> lock(mu);
    std::ofstream f(logPath(), std::ios::app);
    if (f) f << "[" << nowStamp() << "] " << line << "\n";
}

}  // namespace mra::debuglog
