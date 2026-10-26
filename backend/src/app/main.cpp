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

#include <winreg.h>
#include <tlhelp32.h>
#include <dxgi.h>
#include <wrl/client.h>

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

// GPU 策略 (全部作用域限本项目, 不写全局注册表):
// 1. 宿主 exe 写 UserGpuPreferences=高性能 (Windows 调度按 exe 路径)
// 2. 经 DXGI 枚举适配器取独显 (专用显存最大且非软件渲染) 的 LUID, 以进程级
//    环境变量 MRA_GPU_ADAPTER_LUID 传给加载器, 拼成 WebView2 浏览器参数
//    --use-adapter-luid=<High>-<Low>, 把渲染钉在独显上
// 3. 结束残留的 msedgewebview2 共享浏览器进程 (偏好只对新进程生效)
void setGpuPreferenceForKey(const std::wstring& exePath) {
  std::wstring subKey =
      L"Software\\Microsoft\\DirectX\\UserGpuPreferences\\" + exePath;
  const wchar_t* value = L"GpuPreference=2;";  // 2 = high performance
  HKEY key = nullptr;
  if (::RegCreateKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS)
    return;
  wchar_t current[64]{};
  DWORD size = sizeof(current);
  DWORD type = 0;
  bool same =
      ::RegQueryValueExW(key, nullptr, nullptr, &type,
                         reinterpret_cast<LPBYTE>(current), &size) == ERROR_SUCCESS &&
      type == REG_SZ && std::wcscmp(current, value) == 0;
  if (!same)
    ::RegSetValueExW(key, nullptr, 0, REG_SZ,
                     reinterpret_cast<const BYTE*>(value),
                     static_cast<DWORD>((std::wcslen(value) + 1) * sizeof(wchar_t)));
  ::RegCloseKey(key);
}

void preferDiscreteGpu() {
  wchar_t exePath[MAX_PATH]{};
  DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) return;
  setGpuPreferenceForKey(exePath);

  // DXGI: 取专用显存最大的非软件适配器 (即独显) 的 LUID
  wchar_t luidText[64] = L"";
  HMODULE dxgiModule = ::LoadLibraryW(L"dxgi.dll");
  if (dxgiModule) {
    using CreateDXGIFactory1Fn = HRESULT(WINAPI*)(const IID&, void**);
    auto createFactory = reinterpret_cast<CreateDXGIFactory1Fn>(
        ::GetProcAddress(dxgiModule, "CreateDXGIFactory1"));
    if (createFactory) {
      Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
      if (SUCCEEDED(createFactory(__uuidof(IDXGIFactory1), &factory))) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> best;
        SIZE_T bestMemory = 0;
        for (UINT i = 0;; ++i) {
          Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
          if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND)
            break;
          DXGI_ADAPTER_DESC1 desc{};
          if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            const bool software =
                (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            if (!software && desc.DedicatedVideoMemory > bestMemory) {
              best = adapter;
              bestMemory = desc.DedicatedVideoMemory;
            }
          }
        }
        if (best) {
          DXGI_ADAPTER_DESC1 desc{};
          best->GetDesc1(&desc);
          std::swprintf(luidText, 64, L"%u-%u",
                        desc.AdapterLuid.HighPart,
                        static_cast<unsigned>(desc.AdapterLuid.LowPart));
        }
      }
    }
    ::FreeLibrary(dxgiModule);
  }
  // A/B 开关: 设 MRA_NO_ADAPTER_LUID=1 启动可关闭 --use-adapter-luid 钉定
  // (定位「独显渲染→核显屏呈现」跨卡拷贝是否为卡顿来源)
  if (luidText[0] != L'\0' &&
      ::GetEnvironmentVariableW(L"MRA_NO_ADAPTER_LUID", nullptr, 0) == 0)
    ::SetEnvironmentVariableW(L"MRA_GPU_ADAPTER_LUID", luidText);
  else
    ::SetEnvironmentVariableW(L"MRA_GPU_ADAPTER_LUID", nullptr);

  // GPU 偏好只对新启动的进程生效: 结束残留的 WebView2 共享浏览器进程,
  // 让下次加载时新进程按独显偏好启动 (会短暂重启其他 WebView2 应用的会话)
  HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap != INVALID_HANDLE_VALUE) {
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (::Process32FirstW(snap, &pe)) {
      do {
        if (::lstrcmpiW(pe.szExeFile, L"msedgewebview2.exe") == 0) {
          HANDLE proc =
              ::OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
          if (proc) { ::TerminateProcess(proc, 0); ::CloseHandle(proc); }
        }
      } while (::Process32NextW(snap, &pe));
    }
    ::CloseHandle(snap);
  }
}

}  // namespace

int wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    preferDiscreteGpu();
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
