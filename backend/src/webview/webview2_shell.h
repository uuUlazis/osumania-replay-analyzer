#pragma once
// WebView2 desktop window shell. When WebView2 support is compiled out (or
// the runtime is missing at launch) the caller falls back to the default
// browser and keeps the HTTP server running.
#include <string>

namespace mra {

// Opens the URL in the user's default browser (ShellExecute "open").
void openInBrowser(const std::wstring& url);

// Creates the WebView2 desktop window and runs its message loop until closed.
// Returns false when WebView2 support is unavailable; the caller should fall
// back to the system browser.
bool launchWebView2(void* instance, const std::wstring& url);

}  // namespace mra
