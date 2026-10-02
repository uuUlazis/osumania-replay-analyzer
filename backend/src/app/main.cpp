#include "http/api_server.h"
#include "webview/webview2_shell.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#ifndef MANIA_REPLAY_ANALYZER_VERSION
#define MANIA_REPLAY_ANALYZER_VERSION "0.1.0"
#endif

namespace {

namespace fs = std::filesystem;

struct LaunchOptions {
    bool headless = false;
    int port = 0;  // 0 = pick an ephemeral free port
};

LaunchOptions parseArgs(int argc, wchar_t** argv) {
    LaunchOptions options;
    for (int i = 1; i < argc; ++i) {
        const wchar_t* arg = argv[i];
        if (wcscmp(arg, L"--headless") == 0) {
            options.headless = true;
        } else if (wcscmp(arg, L"--port") == 0 && i + 1 < argc) {
            options.port = static_cast<int>(_wtoi(argv[++i]));
        }
    }
    return options;
}

// Directory containing the executable (dev fallback asset root lives next to
// it under frontend/dist when unpacked).
std::wstring exeDirectory() {
    wchar_t buffer[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L".";
    std::wstring dir(buffer, len);
    size_t slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : dir.substr(0, slash);
}

}  // namespace

int wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    // COM is required on the UI thread for the WebView2 loader (and native
    // file dialogs run on their own request threads with per-call init).
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int argc = 0;
    wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    LaunchOptions options =
        argv ? parseArgs(argc, argv) : LaunchOptions{};

    mra::ApiServer server;
    server.setAssetProvider(
        mra::makeDefaultAssetProvider(exeDirectory() + L"\\frontend\\dist"));

    if (!server.start(options.port)) {
        ::MessageBoxW(nullptr,
                      L"服务器启动失败：无法"
                      L"绑定本地端口。",
                      L"osu!mania Replay Analyzer", MB_OK | MB_ICONERROR);
        return 1;
    }

    const std::wstring url = L"http://127.0.0.1:" +
                             std::to_wstring(server.port()) + L"/";

    if (options.headless) {
        // Console-less keep-alive; the port is discoverable via netstat or
        // an eventual named-pipe status endpoint (phase 2).
        for (;;) ::Sleep(60000);
    }

    const bool windowClosed =
        mra::launchWebView2(instance, url);  // blocks until window closes
    if (!windowClosed) {
        // WebView2 unavailable: default browser fallback. Keep serving until
        // the process is terminated.
        mra::openInBrowser(url);
        for (;;) ::Sleep(60000);
    }

    server.stop();
    return 0;
}
