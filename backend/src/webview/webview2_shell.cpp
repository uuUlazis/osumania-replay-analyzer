#include "webview/webview2_shell.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <wrl/event.h>

#include <cstdio>
#include <string>
#include <vector>

#ifdef MANIA_HAS_WEBVIEW2

#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>

namespace mra {
namespace {

constexpr wchar_t kWindowClass[] = L"ManiaReplayAnalyzerWindow";
constexpr wchar_t kWindowTitle[] = L"osu!mania Replay Analyzer";

std::wstring exeDirectory() {
    wchar_t buffer[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};
    std::wstring dir(buffer, len);
    size_t slash = dir.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    return dir.substr(0, slash);
}

// All mutable runtime state (WebView2 user-data dir, log, attempt-state
// file) lives under <exe dir>\_data (用户约定: 应用数据不进用户目录, 且
// _data 不进 git)。%TEMP% 是 exe 目录不可写时的回退。
std::wstring runtimeBaseDir() {
    wchar_t exeBuf[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exeBuf, MAX_PATH) != 0) {
        std::wstring dir(exeBuf);
        const size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            dir = dir.substr(0, slash) + L"\\_data";
            ::CreateDirectoryW(dir.c_str(), nullptr);
            const DWORD attr = GetFileAttributesW(dir.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES &&
                (attr & FILE_ATTRIBUTE_DIRECTORY))
                return dir;
        }
    }
    // exe 目录不可写 (Program Files 等只读位置) → %TEMP% 回退
    wchar_t temp[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, temp) == 0) return {};
    std::wstring base = temp;
    while (!base.empty() && (base.back() == L'\\' || base.back() == L'/'))
        base.pop_back();
    std::wstring dir = base + L"\\ManiaReplayAnalyzer";
    ::CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

// Failure log goes to the runtime base dir (stable, always writable);
// silently skipped when even that is unavailable.
std::wstring webViewLogPath() {
    std::wstring dir = runtimeBaseDir();
    if (dir.empty()) return L"mra_webview.log";
    return dir + L"\\mra_webview.log";
}

void webviewLog(const wchar_t* fmt, const wchar_t* a, unsigned long hr) {
    FILE* log = nullptr;
    if (_wfopen_s(&log, webViewLogPath().c_str(), L"a") == 0 && log) {
        fwprintf(log, fmt, a, hr);
        fclose(log);
    }
}

// Remembers which fallback attempt last succeeded so the next launch can try
// it first (skips seconds of guaranteed-to-fail attempts on restricted
// machines). Lives in the runtime base dir; absence = default order.
std::wstring webViewStatePath() {
    std::wstring dir = runtimeBaseDir();
    if (dir.empty()) return L"mra_webview.state";
    return dir + L"\\mra_webview.state";
}

bool envFlag(const wchar_t* name) {
    wchar_t value[8]{};
    return GetEnvironmentVariableW(name, value, 8) == 1 &&
           value[0] == L'1';
}

struct WebViewAttempt {
    std::wstring userDataFolder;
    bool noSandbox = false;
    const wchar_t* label = L"";
};

// Ordered fallback attempts. The %LOCALAPPDATA% profile works on normal
// desktops and keeps the exe folder clean (single-exe deliverable: nothing
// may be generated beside the program); %TEMP% and --no-sandbox variants
// cover machines whose profile directory or process-spawn policy breaks the
// WebView2 runtime bootstrap (controller creation fails with 0x8000FFFF).
// The exe folder is never used: the single-exe build must not create any
// webview-profile folder next to itself; everything lives under
// %LOCALAPPDATA%\ManiaReplayAnalyzer (or %TEMP% as fallback).
// All attempts run against the SAME window: it is created visible up
// front (skin-editor style) so the app pops immediately, and the WebView2
// composition target attaches reliably to a window that was never hidden.
std::vector<WebViewAttempt> buildAttempts() {
    std::vector<WebViewAttempt> attempts;
    const bool forceNoSandbox = envFlag(L"MRA_WEBVIEW2_NOSANDBOX");
    wchar_t custom[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"MRA_WEBVIEW2_DATA", custom, MAX_PATH) > 0) {
        attempts.push_back({custom, forceNoSandbox, L"custom dir"});
        return attempts;
    }
    std::wstring local = runtimeBaseDir();
    if (!local.empty()) {
        attempts.push_back({local + L"\\WebView2", forceNoSandbox,
                            L"local appdata"});
        attempts.push_back({local + L"\\WebView2", true,
                            L"local appdata + nosandbox"});
    }
    // %TEMP% fallback for machines whose %LOCALAPPDATA% is unwritable.
    // The exe folder is NEVER used: the single-exe build creates nothing
    // beside itself (no webview-profile folder anywhere in the program dir).
    wchar_t tempPath[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, tempPath) > 0) {
        std::wstring tempDir = tempPath;
        while (!tempDir.empty() && (tempDir.back() == L'\\' || tempDir.back() == L'/'))
            tempDir.pop_back();
        std::wstring tempProfile =
            tempDir + L"\\ManiaReplayAnalyzer\\WebView2";
        attempts.push_back({tempProfile, forceNoSandbox,
                            L"temp dir"});
        attempts.push_back({tempProfile, true,
                            L"temp dir + nosandbox"});
    }
    return attempts;
}

class AppWindow {
  public:
    explicit AppWindow(std::wstring url) : url_(std::move(url)) {}

    ~AppWindow() {
        if (hwnd_) DestroyWindow(hwnd_);
    }

    bool create(HINSTANCE instance) {
        instance_ = instance;
        startTick_ = GetTickCount64();
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &AppWindow::wndProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        // Match the app theme (#14151a) so the brief pre-content moment
        // is a seamless dark frame instead of a white flash.
        static HBRUSH background = CreateSolidBrush(RGB(0x14, 0x15, 0x1a));
        windowClass.hbrBackground = background;
        windowClass.lpszClassName = kWindowClass;
        if (!RegisterClassExW(&windowClass) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        RECT bounds{0, 0, 1440, 900};
        AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0);
        // Visible from creation: WebView2 does not reliably attach its
        // composition target to a window first shown after the controller
        // completes, and an up-front window means the UI pops immediately.
        hwnd_ = CreateWindowExW(
            0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
            bounds.bottom - bounds.top, nullptr, nullptr, instance, this);
        return hwnd_ != nullptr;
    }

    // Attempt state is stored as LABELS (stable across rebuilds and
    // reorderings): line 1 = last successful attempt, line 2 = last failed
    // attempt (demoted so a deterministically-broken attempt stops costing
    // its async-failure wait on every launch).
    void readState(std::wstring& preferred, std::wstring& failed) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, webViewStatePath().c_str(), L"r") != 0 || !f) return;
        wchar_t line[128] = L"";
        for (int i = 0; i < 2 && fgetws(line, 128, f); ++i) {
            size_t n = wcspbrk(line, L"\r\n")
                           ? static_cast<size_t>(wcspbrk(line, L"\r\n") -
                                                 line)
                           : wcslen(line);
            (i == 0 ? preferred : failed).assign(line, n);
        }
        fclose(f);
    }

    void writeState(const std::wstring& preferred,
                    const std::wstring& failed) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, webViewStatePath().c_str(), L"w") != 0 || !f) return;
        fwprintf(f, L"%s\n%s\n", preferred.c_str(), failed.c_str());
        fclose(f);
    }

    int loadPreferredAttempt() {
        std::wstring preferred, failed;
        readState(preferred, failed);
        int firstOk = -1;
        for (size_t i = 0; i < attempts_.size(); ++i) {
            if (failed == attempts_[i].label) continue;
            if (preferred == attempts_[i].label) return static_cast<int>(i);
            if (firstOk < 0) firstOk = static_cast<int>(i);
        }
        return firstOk;
    }

    void savePreferredAttempt(size_t index) {
        std::wstring preferred, failed;
        readState(preferred, failed);
        writeState(attempts_[index].label, failed);
    }

    void saveFailedAttempt(const wchar_t* label) {
        std::wstring preferred, failed;
        readState(preferred, failed);
        writeState(preferred, label);
    }

    void startAttempts(std::vector<WebViewAttempt> attempts) {
        attempts_ = std::move(attempts);
        const int preferred = loadPreferredAttempt();
        if (preferred > 0 && static_cast<size_t>(preferred) < attempts_.size()) {
            // Last successful attempt goes first: warm profile, no wasted
            // retries. The full fallback chain stays behind it.
            std::rotate(attempts_.begin(), attempts_.begin() + preferred,
                        attempts_.end());
        }
        attemptIndex_ = 0;
        tryCurrentAttempt();
    }

    int run() {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

    bool webViewSucceeded() const { return succeeded_; }

  private:
    void tryCurrentAttempt() {
        if (attemptIndex_ >= attempts_.size()) {
            webviewLog(L"all webview attempts failed\n", L"", 0);
            succeeded_ = false;
            if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            return;
        }
        const WebViewAttempt& attempt = attempts_[attemptIndex_];
        auto environmentHandler =
            Microsoft::WRL::Callback<
                ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT result,
                       ICoreWebView2Environment* environment) -> HRESULT {
                    if (FAILED(result) || !environment) {
                        webviewLog(
                            L"webview init failed (environment, %s): "
                            L"hr=0x%08lX\n",
                            attempts_[attemptIndex_].label,
                            static_cast<unsigned long>(result));
                        return advanceAttempt();
                    }
                    auto controllerHandler =
                        Microsoft::WRL::Callback<
                            ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this](HRESULT controllerResult,
                                   ICoreWebView2Controller*
                                       controller) -> HRESULT {
                                if (FAILED(controllerResult) || !controller) {
                                    webviewLog(
                                        L"webview init failed (controller, "
                                        L"%s): hr=0x%08lX\n",
                                        attempts_[attemptIndex_].label,
                                        static_cast<unsigned long>(
                                            controllerResult));
                                    return advanceAttempt();
                                }
                                succeeded_ = true;
                                savePreferredAttempt(attemptIndex_);
                                {
                                    wchar_t readyMsg[160];
                                    _snwprintf(readyMsg, 159,
                                               L"webview ready in %lu ms "
                                               L"(attempt %u: %s)",
                                               static_cast<unsigned long>(
                                                   GetTickCount64() -
                                                   startTick_),
                                               static_cast<unsigned>(
                                                   attemptIndex_),
                                               attempts_[attemptIndex_]
                                                   .label);
                                    readyMsg[159] = L'\0';
                                    webviewLog(L"%s\n", readyMsg, 0);
                                }
                                controller_ = controller;
                                if (SUCCEEDED(controller_->get_CoreWebView2(
                                        &webview_)) &&
                                    webview_) {
                                    webview_->Navigate(url_.c_str());
                                }
                                RECT client{};
                                GetClientRect(hwnd_, &client);
                                controller_->put_Bounds(client);
                                return S_OK;
                            });
                    return environment->CreateCoreWebView2Controller(
                        hwnd_, controllerHandler.Get());
                });

        CreateDirectoryW(attempt.userDataFolder.c_str(), nullptr);

        Microsoft::WRL::ComPtr<ICoreWebView2EnvironmentOptions> options;
        {
            // --no-sandbox for restricted environments, plus optional
            // extra chromium switches via MRA_WEBVIEW2_ARGS (diagnostics,
            // e.g. --remote-debugging-port=9222).
            std::wstring extra;
            if (attempt.noSandbox) extra += L"--no-sandbox ";
            wchar_t envArgs[1024] = L"";
            if (GetEnvironmentVariableW(L"MRA_WEBVIEW2_ARGS", envArgs, 1024) >
                0) {
                extra += envArgs;
            }
            // 独显偏好: 用 --use-adapter-luid 把 WebView2 的 GPU 进程钉在
            // 高性能适配器上 (进程级环境变量, 只影响本项目)。LUID 由启动时
            // 的 DXGI 枚举写入 (见 preferDiscreteGpuEnvironment)。
            wchar_t adapterArgs[128] = L"";
            if (GetEnvironmentVariableW(L"MRA_GPU_ADAPTER_LUID",
                                        adapterArgs, 128) > 0) {
                extra += L"--use-adapter-luid=";
                extra += adapterArgs;
                extra += L" ";
            }
            if (!extra.empty()) {
                auto opts =
                    Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
                opts->put_AdditionalBrowserArguments(extra.c_str());
                options = opts;
            }
        }
        HRESULT startResult = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, attempt.userDataFolder.c_str(), options.Get(),
            environmentHandler.Get());
        if (FAILED(startResult)) {
            webviewLog(L"webview init failed (start, %s): hr=0x%08lX\n",
                       attempt.label,
                       static_cast<unsigned long>(startResult));
            advanceAttempt();
        }
    }

    // Move to the next fallback attempt on the same window. Called from
    // completion callbacks (the S_OK return swallows the failure) and once
    // per synchronous start failure; the attempt list is short so the
    // recursion depth is trivially bounded.
    HRESULT advanceAttempt() {
        if (attemptIndex_ < attempts_.size())
            saveFailedAttempt(attempts_[attemptIndex_].label);
        ++attemptIndex_;
        tryCurrentAttempt();
        return S_OK;
    }

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam,
                                    LPARAM lParam) {
        AppWindow* self = reinterpret_cast<AppWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<AppWindow*>(create->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(self));
            return TRUE;
        }
        switch (message) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC dc = BeginPaint(hwnd, &ps);
                if (self && !self->controller_) {
                    RECT rc{};
                    GetClientRect(hwnd, &rc);
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, RGB(0x8a, 0x8f, 0xa3));
                    DrawTextW(dc, L"正在启动界面…", -1, &rc,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_SIZE:
                if (self && self->controller_) {
                    RECT client{};
                    GetClientRect(hwnd, &client);
                    self->controller_->put_Bounds(client);
                }
                return 0;
            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            default:
                break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    bool succeeded_ = false;
    std::wstring url_;
    std::vector<WebViewAttempt> attempts_;
    ULONGLONG startTick_ = 0;
    size_t attemptIndex_ = 0;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
};

}  // namespace

void openInBrowser(const std::wstring& url) {
    HINSTANCE result = ShellExecuteW(nullptr, L"open", url.c_str(),
                                     nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) > 32) return;
    // Fallback: cmd start handles machines where direct URL invocation is
    // not associated.
    std::wstring cmd = L"cmd.exe /c start \"\"" + url + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

bool launchWebView2(void* instance, const std::wstring& url) {
    const HINSTANCE hInstance = static_cast<HINSTANCE>(instance);
    // One visible window for the whole lifetime; only the WebView2
    // environment/controller is retried across profile fallbacks.
    AppWindow window(url);
    if (!window.create(hInstance)) {
        webviewLog(L"window create failed: gle=0x%08lX\n", L"",
                   static_cast<unsigned long>(GetLastError()));
        return false;
    }
    window.startAttempts(buildAttempts());
    window.run();
    return window.webViewSucceeded();
}

}  // namespace mra

#else

namespace mra {

void openInBrowser(const std::wstring& url) {
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
}

bool launchWebView2(void*, const std::wstring&) {
    return false;
}

}  // namespace mra

#endif
