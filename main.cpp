#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <initguid.h>
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include "webview2_sdk/build/native/include/WebView2.h"

#pragma comment(lib, "dwmapi.lib")

typedef BOOL(WINAPI* SetProcessDpiAwarenessContextFn)(DPI_AWARENESS_CONTEXT);

template <typename T>
class SimpleComPtr {
private:
    T* ptr;
public:
    SimpleComPtr() : ptr(nullptr) {}
    SimpleComPtr(T* p) : ptr(p) { if (ptr) ptr->AddRef(); }
    SimpleComPtr(const SimpleComPtr& other) : ptr(other.ptr) { if (ptr) ptr->AddRef(); }
    ~SimpleComPtr() { if (ptr) { ptr->Release(); ptr = nullptr; } }

    T* Get() const { return ptr; }
    T** GetAddressOf() { return &ptr; }
    T** operator&() { return &ptr; }
    T* operator->() const { return ptr; }
    operator bool() const { return ptr != nullptr; }

    SimpleComPtr& operator=(const SimpleComPtr& other) {
        if (this != &other) {
            if (ptr) ptr->Release();
            ptr = other.ptr;
            if (ptr) ptr->AddRef();
        }
        return *this;
    }
    SimpleComPtr& operator=(T* p) {
        if (ptr != p) {
            if (ptr) ptr->Release();
            ptr = p;
            if (ptr) ptr->AddRef();
        }
        return *this;
    }
    void Reset() { if (ptr) { ptr->Release(); ptr = nullptr; } }
};

struct TabInfo {
    std::wstring title;
    std::wstring url;
    std::wstring favicon;
    SimpleComPtr<ICoreWebView2Controller> controller;
    SimpleComPtr<ICoreWebView2> webview;
};

// Global Handles
HINSTANCE g_hInstance = NULL;
HWND g_hWndMain = NULL;
HWND g_hWndTopBar = NULL;
HWND g_hWndContent = NULL;

SimpleComPtr<ICoreWebView2Environment> g_webViewEnvironment;
SimpleComPtr<ICoreWebView2Controller> g_topBarController;
SimpleComPtr<ICoreWebView2> g_topBarWebview;

std::vector<TabInfo> g_tabs;
int g_activeTabIndex = -1;
bool g_topBarLoaded = false;

#define TOPBAR_HEIGHT 108
#define BORDER_PADDING 0

// Forward declarations
void CreateNewTab(const std::wstring& initialUrl = L"https://www.google.com");
void SwitchToTab(int index);
void CloseTab(int index);
void NavigateTo(const std::wstring& input);
void TranslateCurrentPage();
void ShowHistoryDialog();
void ShowDownloadsDialog();
void LogHistory(const std::wstring& url);
void SyncUi();

bool IsValidUrl(const std::wstring& str) {
    if (str.rfind(L"http://", 0) == 0 || str.rfind(L"https://", 0) == 0 || str.rfind(L"file://", 0) == 0) return true;
    size_t dotPos = str.find(L'.');
    if (dotPos != std::wstring::npos && dotPos > 0 && dotPos < str.length() - 1 && str.find(L' ') == std::wstring::npos) return true;
    return false;
}

std::wstring PrepareUrl(const std::wstring& input) {
    if (input.empty()) return L"https://www.google.com";
    if (IsValidUrl(input)) {
        if (input.rfind(L"http://", 0) != 0 && input.rfind(L"https://", 0) != 0 && input.rfind(L"file://", 0) != 0) {
            return L"https://" + input;
        }
        return input;
    }
    std::wstring searchUrl = L"https://www.google.com/search?q=";
    for (wchar_t c : input) {
        if (c == L' ') searchUrl += L'+';
        else if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'-' || c == L'_' || c == L'.' || c == L'~') searchUrl += c;
        else {
            char utf8[8] = {0};
            WideCharToMultiByte(CP_UTF8, 0, &c, 1, utf8, sizeof(utf8), NULL, NULL);
            for (int i = 0; utf8[i] != '\0'; i++) {
                wchar_t hex[4];
                swprintf_s(hex, L"%%%02X", (unsigned char)utf8[i]);
                searchUrl += hex;
            }
        }
    }
    return searchUrl;
}

void InjectHistoryDataIfHistoryTab(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= (int)g_tabs.size()) return;
    auto& tab = g_tabs[tabIndex];
    if (tab.url.find(L"history.html") == std::wstring::npos || !tab.webview) return;

    wchar_t appData[MAX_PATH];
    std::wstringstream js;
    js << L"if (window.loadHistoryData) { window.loadHistoryData([";

    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appData))) {
        std::wstring historyPath = std::wstring(appData) + L"\\FloatBrowser_history.txt";
        FILE* f = NULL;
        _wfopen_s(&f, historyPath.c_str(), L"r, ccs=UTF-8");
        if (f) {
            wchar_t line[1024];
            std::vector<std::wstring> lines;
            while (fgetws(line, 1024, f)) lines.push_back(line);
            fclose(f);

            bool first = true;
            for (int i = (int)lines.size() - 1; i >= 0; --i) {
                std::wstring l = lines[i];
                if (l.empty() || l.find(L"history.html") != std::wstring::npos) continue;

                // Format: [2026-11-21 00:00:00] Title - Url
                std::wstring timeStr = L"";
                std::wstring urlStr = l;
                if (l.length() > 22 && l[0] == L'[') {
                    timeStr = l.substr(12, 8); // e.g. 00:00:00
                    urlStr = l.substr(22);
                }
                while (!urlStr.empty() && (urlStr.back() == L'\n' || urlStr.back() == L'\r')) urlStr.pop_back();

                std::wstring titleStr = urlStr;
                size_t p = urlStr.find(L" - ");
                if (p != std::wstring::npos) {
                    titleStr = urlStr.substr(0, p);
                    urlStr = urlStr.substr(p + 3);
                }

                if (!first) js << L",";
                first = false;

                // Escape quotes
                for (size_t k = 0; (k = titleStr.find(L'"', k)) != std::wstring::npos; k += 2) titleStr.replace(k, 1, L"\\\"");
                for (size_t k = 0; (k = urlStr.find(L'"', k)) != std::wstring::npos; k += 2) urlStr.replace(k, 1, L"\\\"");

                js << L"{\"time\":\"" << timeStr << L"\",\"title\":\"" << titleStr << L"\",\"url\":\"" << urlStr << L"\"}";
            }
        }
    }
    js << L"]); }";
    tab.webview->ExecuteScript(js.str().c_str(), nullptr);
}

void LogHistory(const std::wstring& url) {
    if (url.empty() || url == L"about:blank" || url.find(L"history.html") != std::wstring::npos) return;
    wchar_t appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appData))) {
        std::wstring historyPath = std::wstring(appData) + L"\\FloatBrowser_history.txt";
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        localtime_s(&tm, &now_time);
        std::wstringstream ss;
        ss << std::put_time(&tm, L"[%Y-%m-%d %H:%M:%S] ") << url << L"\n";
        FILE* f = NULL;
        _wfopen_s(&f, historyPath.c_str(), L"a, ccs=UTF-8");
        if (f) { fputws(ss.str().c_str(), f); fclose(f); }
    }
}

void ShowDownloadsDialog() {
    wchar_t downloadsPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, downloadsPath))) {
        std::wstring fullPath = std::wstring(downloadsPath) + L"\\Downloads";
        int res = MessageBoxW(g_hWndMain, (L"مجلد التنزيلات الافتراضي:\n" + fullPath + L"\n\nهل ترغب في فتح مجلد التنزيلات الآن؟").c_str(), L"إدارة التنزيلات (Downloads)", MB_YESNO | MB_ICONQUESTION);
        if (res == IDYES) {
            ShellExecuteW(NULL, L"open", fullPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
    }
}

void TranslateCurrentPage() {
    if (g_activeTabIndex >= 0 && g_activeTabIndex < (int)g_tabs.size()) {
        auto& tab = g_tabs[g_activeTabIndex];
        if (tab.webview) {
            std::wstring script = 
                L"(function() {"
                L"  if (document.getElementById('ub-translate-bar')) return;"
                L"  var bar = document.createElement('div');"
                L"  bar.id = 'ub-translate-bar';"
                L"  bar.style.position = 'fixed';"
                L"  bar.style.top = '12px';"
                L"  bar.style.right = '20px';"
                L"  bar.style.zIndex = '2147483647';"
                L"  bar.style.display = 'flex';"
                L"  bar.style.alignItems = 'center';"
                L"  bar.style.gap = '10px';"
                L"  bar.style.background = '#ffffff';"
                L"  bar.style.padding = '8px 14px';"
                L"  bar.style.borderRadius = '24px';"
                L"  bar.style.boxShadow = '0 4px 16px rgba(0,0,0,0.2)';"
                L"  bar.style.fontFamily = 'Segoe UI, Arial, sans-serif';"
                L"  bar.style.fontSize = '13px';"
                L"  bar.style.direction = 'rtl';"
                L"  bar.innerHTML = '<span style=\"display:flex;align-items:center;gap:6px;font-weight:600;color:#1a73e8;\">🌐 ترجمة فورية</span>' +"
                L"                  '<div id=\"google_translate_element\"></div>' +"
                L"                  '<button id=\"ub-close-tr\" style=\"border:none;background:none;cursor:pointer;font-weight:bold;color:#70757a;padding:2px 6px;\">✕</button>';"
                L"  document.body.appendChild(bar);"
                L"  document.getElementById('ub-close-tr').onclick = function() { bar.remove(); };"
                L"  var script = document.createElement('script');"
                L"  script.type = 'text/javascript';"
                L"  script.src = 'https://translate.google.com/translate_a/element.js?cb=googleTranslateElementInit';"
                L"  document.body.appendChild(script);"
                L"  window.googleTranslateElementInit = function() {"
                L"    new google.translate.TranslateElement({pageLanguage: 'auto', includedLanguages: 'ar,en,fr,es,de,tr', layout: google.translate.TranslateElement.InlineLayout.SIMPLE}, 'google_translate_element');"
                L"  };"
                L"})();";
            tab.webview->ExecuteScript(script.c_str(), nullptr);
        }
    }
}

void SyncUi() {
    if (!g_topBarWebview || !g_topBarLoaded) return;

    std::wstringstream js;
    js << L"if (window.renderTabs) { window.renderTabs([";
    for (size_t i = 0; i < g_tabs.size(); ++i) {
        if (i > 0) js << L",";
        std::wstring title = g_tabs[i].title.empty() ? L"Google" : g_tabs[i].title;
        // Escape quotes
        for (size_t p = 0; (p = title.find(L'"', p)) != std::wstring::npos; p += 2) {
            title.replace(p, 1, L"\\\"");
        }
        std::wstring fav = g_tabs[i].favicon;
        js << L"{\"title\":\"" << title << L"\",\"favicon\":\"" << fav << L"\"}";
    }
    js << L"]," << g_activeTabIndex << L"); }";

    if (g_activeTabIndex >= 0 && g_activeTabIndex < (int)g_tabs.size()) {
        std::wstring url = g_tabs[g_activeTabIndex].url;
        for (size_t p = 0; (p = url.find(L'"', p)) != std::wstring::npos; p += 2) {
            url.replace(p, 1, L"\\\"");
        }
        js << L"if (window.setUrl) { window.setUrl(\"" << url << L"\"); }";
    }

    g_topBarWebview->ExecuteScript(js.str().c_str(), nullptr);
}

void SwitchToTab(int index) {
    if (index < 0 || index >= (int)g_tabs.size()) return;
    if (g_activeTabIndex >= 0 && g_activeTabIndex < (int)g_tabs.size()) {
        if (g_tabs[g_activeTabIndex].controller) {
            g_tabs[g_activeTabIndex].controller->put_IsVisible(FALSE);
        }
    }
    g_activeTabIndex = index;
    auto& tab = g_tabs[index];
    if (tab.controller) {
        RECT rc;
        GetClientRect(g_hWndContent, &rc);
        tab.controller->put_Bounds(rc);
        tab.controller->put_IsVisible(TRUE);
    }
    if (!tab.title.empty()) {
        SetWindowTextW(g_hWndMain, (tab.title + L" - Float").c_str());
    } else {
        SetWindowTextW(g_hWndMain, L"Float");
    }
    SyncUi();
}

void CloseTab(int index) {
    if (g_tabs.size() <= 1) {
        NavigateTo(L"https://www.google.com");
        return;
    }
    if (index < 0 || index >= (int)g_tabs.size()) return;
    if (g_tabs[index].controller) {
        g_tabs[index].controller->Close();
    }
    g_tabs.erase(g_tabs.begin() + index);
    if (g_activeTabIndex >= (int)g_tabs.size()) {
        g_activeTabIndex = (int)g_tabs.size() - 1;
    }
    SwitchToTab(g_activeTabIndex);
}

void NavigateTo(const std::wstring& input) {
    if (g_activeTabIndex >= 0 && g_activeTabIndex < (int)g_tabs.size()) {
        std::wstring targetUrl = PrepareUrl(input);
        auto& tab = g_tabs[g_activeTabIndex];
        tab.url = targetUrl;
        if (tab.webview) {
            tab.webview->Navigate(targetUrl.c_str());
        }
        LogHistory(targetUrl);
        SyncUi();
    }
}

// Event Handlers for Content Tabs
class ContentSourceChangedHandler : public ICoreWebView2SourceChangedEventHandler {
public:
    int tabIndex;
    LONG m_ref;
    ContentSourceChangedHandler(int idx) : tabIndex(idx), m_ref(1) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = static_cast<ICoreWebView2SourceChangedEventHandler*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&m_ref);
        if (count == 0) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs*) override {
        LPWSTR uri = nullptr;
        if (SUCCEEDED(sender->get_Source(&uri)) && uri) {
            if (tabIndex < (int)g_tabs.size()) {
                g_tabs[tabIndex].url = uri;
                // Generate favicon url from host
                std::wstring u = uri;
                size_t p = u.find(L"://");
                if (p != std::wstring::npos) {
                    size_t slash = u.find(L'/', p + 3);
                    std::wstring host = (slash == std::wstring::npos) ? u : u.substr(0, slash);
                    g_tabs[tabIndex].favicon = host + L"/favicon.ico";
                }
                if (tabIndex == g_activeTabIndex) {
                    SyncUi();
                }
                InjectHistoryDataIfHistoryTab(tabIndex);
            }
            CoTaskMemFree(uri);
        }
        return S_OK;
    }
};

class ContentTitleChangedHandler : public ICoreWebView2DocumentTitleChangedEventHandler {
public:
    int tabIndex;
    LONG m_ref;
    ContentTitleChangedHandler(int idx) : tabIndex(idx), m_ref(1) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = static_cast<ICoreWebView2DocumentTitleChangedEventHandler*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&m_ref);
        if (count == 0) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, IUnknown*) override {
        LPWSTR title = nullptr;
        if (SUCCEEDED(sender->get_DocumentTitle(&title)) && title) {
            if (tabIndex < (int)g_tabs.size()) {
                g_tabs[tabIndex].title = title;
                if (tabIndex == g_activeTabIndex) {
                    SetWindowTextW(g_hWndMain, (std::wstring(title) + L" - Float").c_str());
                }
                SyncUi();
            }
            CoTaskMemFree(title);
        }
        return S_OK;
    }
};

class ContentHistoryChangedHandler : public ICoreWebView2HistoryChangedEventHandler {
public:
    int tabIndex;
    LONG m_ref;
    ContentHistoryChangedHandler(int idx) : tabIndex(idx), m_ref(1) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = static_cast<ICoreWebView2HistoryChangedEventHandler*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&m_ref);
        if (count == 0) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, IUnknown*) override {
        LPWSTR uri = nullptr;
        if (SUCCEEDED(sender->get_Source(&uri)) && uri) {
            if (tabIndex < (int)g_tabs.size()) {
                g_tabs[tabIndex].url = uri;
                if (tabIndex == g_activeTabIndex) {
                    SyncUi();
                }
                LogHistory(uri);
            }
            CoTaskMemFree(uri);
        }
        return S_OK;
    }
};

class ContentNewWindowRequestedHandler : public ICoreWebView2NewWindowRequestedEventHandler {
public:
    LONG m_ref;
    ContentNewWindowRequestedHandler() : m_ref(1) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = static_cast<ICoreWebView2NewWindowRequestedEventHandler*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&m_ref);
        if (count == 0) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) override {
        LPWSTR uri = nullptr;
        if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
            std::wstring targetUrl = uri;
            CoTaskMemFree(uri);
            // Mark event as handled so WebView2 doesn't open a separate OS window
            args->put_Handled(TRUE);
            // Open in a new tab inside Float!
            CreateNewTab(targetUrl);
        }
        return S_OK;
    }
};

class OpenInNewTabCustomItemSelectedHandler : public ICoreWebView2CustomItemSelectedEventHandler {
public:
    std::wstring m_targetUrl;
    LONG m_ref;
    OpenInNewTabCustomItemSelectedHandler(const std::wstring& url) : m_targetUrl(url), m_ref(1) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = static_cast<ICoreWebView2CustomItemSelectedEventHandler*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&m_ref);
        if (count == 0) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2ContextMenuItem*, IUnknown*) override {
        if (!m_targetUrl.empty()) {
            CreateNewTab(m_targetUrl);
        }
        return S_OK;
    }
};

class ContentContextMenuRequestedHandler : public ICoreWebView2ContextMenuRequestedEventHandler {
public:
    LONG m_ref;
    ContentContextMenuRequestedHandler() : m_ref(1) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = static_cast<ICoreWebView2ContextMenuRequestedEventHandler*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&m_ref);
        if (count == 0) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2ContextMenuRequestedEventArgs* args) override {
        SimpleComPtr<ICoreWebView2ContextMenuTarget> target;
        ICoreWebView2ContextMenuTarget* rawTarget = nullptr;
        if (SUCCEEDED(args->get_ContextMenuTarget(&rawTarget)) && rawTarget) {
            target = rawTarget;
            rawTarget->Release();

            BOOL hasLink = FALSE;
            target->get_HasLinkUri(&hasLink);
            if (hasLink) {
                LPWSTR linkUri = nullptr;
                if (SUCCEEDED(target->get_LinkUri(&linkUri)) && linkUri) {
                    std::wstring link(linkUri);
                    CoTaskMemFree(linkUri);

                    SimpleComPtr<ICoreWebView2Environment9> env9;
                    if (g_webViewEnvironment && SUCCEEDED(g_webViewEnvironment->QueryInterface(IID_ICoreWebView2Environment9, (void**)env9.GetAddressOf()))) {
                        SimpleComPtr<ICoreWebView2ContextMenuItem> menuItem;
                        ICoreWebView2ContextMenuItem* rawItem = nullptr;
                        if (SUCCEEDED(env9->CreateContextMenuItem(
                            L"Open link in new tab",
                            nullptr,
                            COREWEBVIEW2_CONTEXT_MENU_ITEM_KIND_COMMAND,
                            &rawItem)) && rawItem) {
                            menuItem = rawItem;
                            rawItem->Release();

                            EventRegistrationToken itemToken;
                            auto itemHandler = new OpenInNewTabCustomItemSelectedHandler(link);
                            menuItem->add_CustomItemSelected(itemHandler, &itemToken);
                            itemHandler->Release();

                            SimpleComPtr<ICoreWebView2ContextMenuItemCollection> menuList;
                            ICoreWebView2ContextMenuItemCollection* rawColl = nullptr;
                            if (SUCCEEDED(args->get_MenuItems(&rawColl)) && rawColl) {
                                menuList = rawColl;
                                rawColl->Release();
                                menuList->InsertValueAtIndex(0, menuItem.Get());
                            }
                        }
                    }
                }
            }
        }
        return S_OK;
    }
};

class CreateContentControllerHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
public:
    int tabIndex;
    std::wstring initialUrl;
    LONG m_ref;
    CreateContentControllerHandler(int idx, const std::wstring& url) : tabIndex(idx), initialUrl(url), m_ref(1) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&m_ref);
        if (count == 0) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller* controller) override {
        if (FAILED(result) || !controller) return result;
        if (tabIndex >= 0 && tabIndex < (int)g_tabs.size()) {
            g_tabs[tabIndex].controller = controller;
            ICoreWebView2* raw = nullptr;
            controller->get_CoreWebView2(&raw);
            g_tabs[tabIndex].webview = raw;
            if (raw) raw->Release();

            SimpleComPtr<ICoreWebView2Settings> settings;
            ICoreWebView2Settings* rawSettings = nullptr;
            if (SUCCEEDED(g_tabs[tabIndex].webview->get_Settings(&rawSettings)) && rawSettings) {
                settings = rawSettings;
                rawSettings->Release();
                settings->put_IsStatusBarEnabled(FALSE);
                settings->put_AreDevToolsEnabled(TRUE);
                settings->put_AreDefaultContextMenusEnabled(TRUE);
            }

            EventRegistrationToken token;
            auto srcHandler = new ContentSourceChangedHandler(tabIndex);
            g_tabs[tabIndex].webview->add_SourceChanged(srcHandler, &token);
            srcHandler->Release();

            auto titleHandler = new ContentTitleChangedHandler(tabIndex);
            g_tabs[tabIndex].webview->add_DocumentTitleChanged(titleHandler, &token);
            titleHandler->Release();

            auto historyHandler = new ContentHistoryChangedHandler(tabIndex);
            g_tabs[tabIndex].webview->add_HistoryChanged(historyHandler, &token);
            historyHandler->Release();

            auto newWinHandler = new ContentNewWindowRequestedHandler();
            g_tabs[tabIndex].webview->add_NewWindowRequested(newWinHandler, &token);
            newWinHandler->Release();

            SimpleComPtr<ICoreWebView2_11> webview11;
            if (SUCCEEDED(g_tabs[tabIndex].webview->QueryInterface(IID_ICoreWebView2_11, (void**)webview11.GetAddressOf()))) {
                auto menuHandler = new ContentContextMenuRequestedHandler();
                webview11->add_ContextMenuRequested(menuHandler, &token);
                menuHandler->Release();
            }

            RECT rc;
            GetClientRect(g_hWndContent, &rc);
            controller->put_Bounds(rc);

            if (tabIndex == g_activeTabIndex) {
                controller->put_IsVisible(TRUE);
            } else {
                controller->put_IsVisible(FALSE);
            }
            g_tabs[tabIndex].webview->Navigate(initialUrl.c_str());
            SyncUi();
        }
        return S_OK;
    }
};

void CreateNewTab(const std::wstring& initialUrl) {
    TabInfo info;
    info.title = L"Google";
    info.url = initialUrl;
    info.favicon = L"https://www.google.com/favicon.ico";
    g_tabs.push_back(info);
    int newIndex = (int)g_tabs.size() - 1;

    if (g_webViewEnvironment && g_hWndContent) {
        auto handler = new CreateContentControllerHandler(newIndex, initialUrl);
        g_webViewEnvironment->CreateCoreWebView2Controller(g_hWndContent, handler);
    }
    SwitchToTab(newIndex);
}

// TopBar WebMessage Handler
class TopBarWebMessageHandler : public ICoreWebView2WebMessageReceivedEventHandler {
public:
    LONG m_ref;
    TopBarWebMessageHandler() : m_ref(1) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = static_cast<ICoreWebView2WebMessageReceivedEventHandler*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&m_ref);
        if (count == 0) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) override {
        LPWSTR rawMsg = nullptr;
        if (SUCCEEDED(args->TryGetWebMessageAsString(&rawMsg)) && rawMsg) {
            std::wstring msg = rawMsg;
            CoTaskMemFree(rawMsg);

            if (msg.rfind(L"NAV:", 0) == 0) {
                std::wstring url = msg.substr(4);
                NavigateTo(url);
            } else if (msg == L"BACK") {
                if (g_activeTabIndex >= 0 && g_tabs[g_activeTabIndex].webview) g_tabs[g_activeTabIndex].webview->GoBack();
            } else if (msg == L"FORWARD") {
                if (g_activeTabIndex >= 0 && g_tabs[g_activeTabIndex].webview) g_tabs[g_activeTabIndex].webview->GoForward();
            } else if (msg == L"REFRESH") {
                if (g_activeTabIndex >= 0 && g_tabs[g_activeTabIndex].webview) g_tabs[g_activeTabIndex].webview->Reload();
            } else if (msg == L"HOME") {
                NavigateTo(L"https://www.google.com");
            } else if (msg == L"TAB_NEW") {
                CreateNewTab(L"https://www.google.com");
            } else if (msg == L"TRANSLATE") {
                TranslateCurrentPage();
            } else if (msg == L"DOWNLOADS") {
                ShowDownloadsDialog();
            } else if (msg == L"OPEN_HISTORY_TAB") {
                wchar_t exePath[MAX_PATH];
                GetModuleFileNameW(NULL, exePath, MAX_PATH);
                std::wstring dir = exePath;
                size_t lastSlash = dir.find_last_of(L"\\/");
                if (lastSlash != std::wstring::npos) dir = dir.substr(0, lastSlash);
                std::wstring histUrl = std::wstring(L"file:///") + dir + L"\\history.html";
                for (auto& ch : histUrl) { if (ch == L'\\') ch = L'/'; }
                CreateNewTab(histUrl);
            } else if (msg == L"CLEAR_HISTORY") {
                wchar_t appData[MAX_PATH];
                if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appData))) {
                    std::wstring historyPath = std::wstring(appData) + L"\\FloatBrowser_history.txt";
                    _wremove(historyPath.c_str());
                }
            } else if (msg == L"TRANSLATE") {
                TranslateCurrentPage();
            } else if (msg == L"REFRESH") {
                if (g_activeTabIndex >= 0 && g_activeTabIndex < (int)g_tabs.size() && g_tabs[g_activeTabIndex].webview) {
                    g_tabs[g_activeTabIndex].webview->Reload();
                }
            } else if (msg == L"WIN_MIN") {
                ShowWindow(g_hWndMain, SW_MINIMIZE);
            } else if (msg == L"WIN_MAX") {
                if (IsZoomed(g_hWndMain)) ShowWindow(g_hWndMain, SW_RESTORE);
                else ShowWindow(g_hWndMain, SW_MAXIMIZE);
            } else if (msg == L"WIN_CLOSE") {
                PostMessageW(g_hWndMain, WM_CLOSE, 0, 0);
            } else if (msg == L"WIN_DRAG") {
                ReleaseCapture();
                SendMessageW(g_hWndMain, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            } else if (msg.rfind(L"TAB_SWITCH:", 0) == 0) {
                int idx = _wtoi(msg.substr(11).c_str());
                SwitchToTab(idx);
            } else if (msg.rfind(L"TAB_CLOSE:", 0) == 0) {
                int idx = _wtoi(msg.substr(10).c_str());
                CloseTab(idx);
            } else if (msg.rfind(L"TAB_DUPLICATE:", 0) == 0) {
                int idx = _wtoi(msg.substr(14).c_str());
                if (idx >= 0 && idx < (int)g_tabs.size()) {
                    std::wstring targetUrl = g_tabs[idx].url.empty() ? L"https://www.google.com" : g_tabs[idx].url;
                    CreateNewTab(targetUrl);
                }
            }
        }
        return S_OK;
    }
};

class TopBarNavigationCompletedHandler : public ICoreWebView2NavigationCompletedEventHandler {
public:
    LONG m_ref;
    TopBarNavigationCompletedHandler() : m_ref(1) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = static_cast<ICoreWebView2NavigationCompletedEventHandler*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&m_ref);
        if (count == 0) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) override {
        g_topBarLoaded = true;
        SyncUi();
        return S_OK;
    }
};

class CreateTopBarControllerHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
public:
    LONG m_ref;
    CreateTopBarControllerHandler() : m_ref(1) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&m_ref);
        if (count == 0) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller* controller) override {
        if (FAILED(result) || !controller) return result;
        g_topBarController = controller;
        ICoreWebView2* raw = nullptr;
        controller->get_CoreWebView2(&raw);
        g_topBarWebview = raw;
        if (raw) raw->Release();

        SimpleComPtr<ICoreWebView2Settings> settings;
        ICoreWebView2Settings* rawSettings = nullptr;
        if (SUCCEEDED(g_topBarWebview->get_Settings(&rawSettings)) && rawSettings) {
            settings = rawSettings;
            rawSettings->Release();
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_AreDevToolsEnabled(FALSE);
            settings->put_AreDefaultContextMenusEnabled(FALSE);
        }

        EventRegistrationToken token;
        auto msgHandler = new TopBarWebMessageHandler();
        g_topBarWebview->add_WebMessageReceived(msgHandler, &token);
        msgHandler->Release();

        // Fit TopBar window
        RECT rc;
        GetClientRect(g_hWndTopBar, &rc);
        g_topBarController->put_Bounds(rc);
        g_topBarController->put_IsVisible(TRUE);

        // Navigation completed listener to sync tabs
        auto navCompleted = new TopBarNavigationCompletedHandler();
        g_topBarWebview->add_NavigationCompleted(navCompleted, &token);
        navCompleted->Release();

        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring dir = exePath;
        size_t lastSlash = dir.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) dir = dir.substr(0, lastSlash);
        
        std::wstring htmlPath = std::wstring(L"file:///") + dir + L"\\chrome_ui.html";
        for (auto& ch : htmlPath) { if (ch == L'\\') ch = L'/'; }
        
        g_topBarWebview->Navigate(htmlPath.c_str());

        // Now create initial Google tab!
        CreateNewTab(L"https://www.google.com");
        return S_OK;
    }
};

class CreateEnvCompletedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
public:
    LONG m_ref;
    CreateEnvCompletedHandler() : m_ref(1) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&m_ref);
        if (count == 0) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Environment* env) override {
        if (SUCCEEDED(result) && env) {
            g_webViewEnvironment = env;
            auto handler = new CreateTopBarControllerHandler();
            env->CreateCoreWebView2Controller(g_hWndTopBar, handler);
        }
        return S_OK;
    }
};

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_GETMINMAXINFO: {
        // Prevent covering the Windows Taskbar on Maximize
        LPMINMAXINFO mmi = (LPMINMAXINFO)lParam;
        HMONITOR hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfoW(hMon, &mi)) {
            mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
            mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
            mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
            mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
        }
        return 0;
    }

    case WM_NCCALCSIZE:
        // Removes standard native titlebar completely in both maximized and windowed mode
        return 0;

    case WM_NCACTIVATE:
        // Prevents Windows DWM from repainting the native caption on focus changes
        return 1;

    case WM_NCPAINT:
        // Suppress native titlebar painting
        return 0;

    case WM_NCHITTEST: {
        if (IsZoomed(hWnd)) return HTCLIENT;

        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        RECT rc;
        GetWindowRect(hWnd, &rc);

        const int b = 6;
        bool left   = (pt.x >= rc.left && pt.x < rc.left + b);
        bool right  = (pt.x < rc.right && pt.x >= rc.right - b);
        bool top    = (pt.y >= rc.top && pt.y < rc.top + b);
        bool bottom = (pt.y < rc.bottom && pt.y >= rc.bottom - b);

        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;

        return HTCLIENT;
    }

    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

        // Windows 11/10 pushes frameless maximized windows 8px off-screen.
        // We offset it so scrollbars and buttons are 100% visible!
        bool isMaximized = IsZoomed(hWnd);
        int borderOffset = isMaximized ? 8 : 0;

        int topBarX = borderOffset;
        int topBarY = borderOffset;
        int topBarW = width - (borderOffset * 2);
        int topBarH = TOPBAR_HEIGHT;

        int contentX = borderOffset;
        int contentY = borderOffset + TOPBAR_HEIGHT;
        int contentW = width - (borderOffset * 2);
        int contentH = height - (borderOffset * 2) - TOPBAR_HEIGHT;

        if (contentW < 10) contentW = 10;
        if (contentH < 10) contentH = 10;

        if (g_hWndTopBar && g_hWndContent) {
            SetWindowPos(g_hWndTopBar, HWND_TOP, topBarX, topBarY, topBarW, topBarH, SWP_NOACTIVATE);
            SetWindowPos(g_hWndContent, HWND_BOTTOM, contentX, contentY, contentW, contentH, SWP_NOACTIVATE);

            if (g_topBarController) {
                RECT rc = {0, 0, topBarW, topBarH};
                g_topBarController->put_Bounds(rc);
            }

            if (g_activeTabIndex >= 0 && g_activeTabIndex < (int)g_tabs.size()) {
                if (g_tabs[g_activeTabIndex].controller) {
                    RECT rc = {0, 0, contentW, contentH};
                    g_tabs[g_activeTabIndex].controller->put_Bounds(rc);
                }
            }
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI WinMainActual(HINSTANCE hInstance, int nCmdShow) {
    // 1. Full Per-Monitor DPI Awareness V2
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        SetProcessDpiAwarenessContextFn setDpi = (SetProcessDpiAwarenessContextFn)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (setDpi) {
            setDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    g_hInstance = hInstance;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"FloatBrowserMainWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hInstance, L"id", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    if (!wc.hIcon) wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(hInstance, L"id", IMAGE_ICON, 16, 16, LR_SHARED);
    wc.hbrBackground = CreateSolidBrush(RGB(222, 225, 230));
    RegisterClassExW(&wc);

    // Main Frameless Window
    g_hWndMain = CreateWindowExW(
        0, L"FloatBrowserMainWindow", L"Float",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1360, 860,
        NULL, NULL, hInstance, NULL
    );

    // Set Window Icons
    HICON hBigIcon = (HICON)LoadImageW(hInstance, L"id", IMAGE_ICON, 32, 32, LR_SHARED);
    HICON hSmallIcon = (HICON)LoadImageW(hInstance, L"id", IMAGE_ICON, 16, 16, LR_SHARED);
    if (!hBigIcon) hBigIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    if (hBigIcon) SendMessageW(g_hWndMain, WM_SETICON, ICON_BIG, (LPARAM)hBigIcon);
    if (hSmallIcon) SendMessageW(g_hWndMain, WM_SETICON, ICON_SMALL, (LPARAM)hSmallIcon);

    // Apply DWM Shadow and Windows 11 rounded corners (Edge/Chrome style)
    MARGINS margins = {1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(g_hWndMain, &margins);

    DWORD cornerPref = 2; // DWMWCP_ROUND (Windows 11 modern rounded corners)
    DwmSetWindowAttribute(g_hWndMain, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &cornerPref, sizeof(cornerPref));

    // Force Windows DWM to recalculate non-client area without standard titlebar
    SetWindowPos(g_hWndMain, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    // TopBar Child Window (WS_CLIPSIBLINGS so it paints cleanly)
    g_hWndTopBar = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 1360, TOPBAR_HEIGHT,
        g_hWndMain, NULL, hInstance, NULL
    );

    // Content Child Window
    g_hWndContent = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, TOPBAR_HEIGHT, 1360, 860 - TOPBAR_HEIGHT,
        g_hWndMain, NULL, hInstance, NULL
    );

    // Ensure TopBar is strictly above Content window in Z-order
    SetWindowPos(g_hWndTopBar, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    ShowWindow(g_hWndMain, nCmdShow ? nCmdShow : SW_SHOWNORMAL);
    UpdateWindow(g_hWndMain);

    wchar_t appData[MAX_PATH];
    std::wstring userDataFolder = L"";
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appData))) {
        userDataFolder = std::wstring(appData) + L"\\FloatBrowser_UserData";
        CreateDirectoryW(userDataFolder.c_str(), NULL);
    }

    typedef HRESULT (STDAPICALLTYPE *CreateEnvFn)(
        PCWSTR browserExecutableFolder,
        PCWSTR userDataFolder,
        ICoreWebView2EnvironmentOptions* environmentOptions,
        ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environment_created_handler);

    HMODULE hLoader = LoadLibraryW(L"WebView2Loader.dll");
    if (!hLoader) {
        hLoader = LoadLibraryW(L"webview2_sdk\\build\\native\\x64\\WebView2Loader.dll");
    }

    if (hLoader) {
        CreateEnvFn fn = (CreateEnvFn)GetProcAddress(hLoader, "CreateCoreWebView2EnvironmentWithOptions");
        if (fn) {
            auto handler = new CreateEnvCompletedHandler();
            fn(NULL, userDataFolder.empty() ? NULL : userDataFolder.c_str(), NULL, handler);
        }
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    return WinMainActual(hInst, nShow);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow) {
    return WinMainActual(hInst, nShow);
}
