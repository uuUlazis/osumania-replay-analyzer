#include "util/debug_log.h"

#include <chrono>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

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

// 每次启动一个新日志文件 (文件名按启动时间), 便于按会话查阅。
// 保留最近 10 个 (用户定案), 更早的在启动时清理。
std::filesystem::path logDir() {
    std::filesystem::path d = exeDir() / "debug";
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    return d;
}

std::filesystem::path logPath() {
    static std::filesystem::path launchFile;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char name[64];
        std::snprintf(name, sizeof(name), "debug_%04d%02d%02d_%02d%02d%02d.log",
                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                      tm.tm_hour, tm.tm_min, tm.tm_sec);
        launchFile = logDir() / name;
    }
    return launchFile;
}

// 保留最近 10 个 debug_*.log, 删除更老的 (启动时调用一次)
void pruneOldLogs() {
    std::error_code ec;
    std::filesystem::path dir = logDir();
    std::vector<std::filesystem::path> logs;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("debug_", 0) == 0 &&
            name.size() > 4 && name.compare(name.size() - 4, 4, ".log") == 0)
            logs.push_back(entry.path());
    }
    if (logs.size() <= 10) return;
    std::sort(logs.begin(), logs.end());  // 文件名含时间戳, 字典序 = 时间序
    for (size_t i = 0; i + 10 < logs.size(); ++i)
        std::filesystem::remove(logs[i], ec);
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
    pruneOldLogs();
    const std::filesystem::path p = logPath();
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
