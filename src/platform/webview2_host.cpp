#include "kerosene.h"

#include <WebView2.h>
#include <shlwapi.h>
#include <atomic>
#include <cstdio>
#include <cstring>

struct WebViewHost {
    HWND hwnd = nullptr;
    ICoreWebView2Environment *environment = nullptr;
    ICoreWebView2Controller *controller = nullptr;
    ICoreWebView2 *webview = nullptr;
    RECT bounds = {0, 0, 1, 1};
    bool visible = false;
    bool creating = false;
    wchar_t pending_url[K_MAX_URL] = L"";
};

static WebViewHost g_host;

static wchar_t *utf8_to_wide_local(const char *text) {
    int len = MultiByteToWideChar(CP_UTF8, 0, text ? text : "", -1, nullptr, 0);
    if (len <= 0) return nullptr;
    wchar_t *out = (wchar_t *)kzalloc((size_t)len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, text ? text : "", -1, out, len);
    return out;
}

static void set_webview_bounds() {
    if (g_host.controller) {
        g_host.controller->put_Bounds(g_host.bounds);
        g_host.controller->put_IsVisible(g_host.visible ? TRUE : FALSE);
    }
}

class ControllerCompletedHandler final : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    std::atomic<ULONG> refs{1};
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler) {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG value = --refs;
        if (!value) delete this;
        return value;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Controller *result) override {
        g_host.creating = false;
        if (FAILED(errorCode) || !result) return errorCode;
        g_host.controller = result;
        g_host.controller->AddRef();
        g_host.controller->get_CoreWebView2(&g_host.webview);

        ICoreWebView2Settings *settings = nullptr;
        if (g_host.webview && SUCCEEDED(g_host.webview->get_Settings(&settings)) && settings) {
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_AreDefaultContextMenusEnabled(TRUE);
            settings->put_IsScriptEnabled(TRUE);
            settings->Release();
        }
        set_webview_bounds();
        if (g_host.webview && g_host.pending_url[0]) {
            g_host.webview->Navigate(g_host.pending_url);
        }
        return S_OK;
    }
};

class EnvironmentCompletedHandler final : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    std::atomic<ULONG> refs{1};
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler) {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG value = --refs;
        if (!value) delete this;
        return value;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Environment *result) override {
        if (FAILED(errorCode) || !result) {
            g_host.creating = false;
            return errorCode;
        }
        g_host.environment = result;
        g_host.environment->AddRef();
        return g_host.environment->CreateCoreWebView2Controller(g_host.hwnd, new ControllerCompletedHandler());
    }
};

extern "C" bool webview_host_init(HWND hwnd, int x, int y, int w, int h, char *err, size_t err_cap) {
    g_host.hwnd = hwnd;
    g_host.bounds = {x, y, x + (w > 1 ? w : 1), y + (h > 1 ? h : 1)};
    if (g_host.controller || g_host.creating) return true;

    wchar_t data_dir[MAX_PATH];
    platform_get_data_dir(data_dir, MAX_PATH);
    wcscat_s(data_dir, L"\\WebView2");
    CreateDirectoryW(data_dir, nullptr);

    g_host.creating = true;
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, data_dir, nullptr, new EnvironmentCompletedHandler());
    if (FAILED(hr)) {
        g_host.creating = false;
        if (err && err_cap) {
            snprintf(err, err_cap, "WebView2 init falhou: 0x%08lx", (unsigned long)hr);
        }
        return false;
    }
    return true;
}

extern "C" void webview_host_resize(int x, int y, int w, int h) {
    g_host.bounds = {x, y, x + (w > 1 ? w : 1), y + (h > 1 ? h : 1)};
    set_webview_bounds();
}

extern "C" void webview_host_show(int show) {
    g_host.visible = show != 0;
    set_webview_bounds();
}

extern "C" void webview_host_navigate(const char *url) {
    wchar_t *wide = utf8_to_wide_local(url);
    if (!wide) return;
    wcsncpy_s(g_host.pending_url, wide, _TRUNCATE);
    if (g_host.webview) {
        g_host.webview->Navigate(wide);
    }
    kfree(wide);
}

extern "C" void webview_host_reload(void) {
    if (g_host.webview) g_host.webview->Reload();
}

extern "C" void webview_host_go_back(void) {
    if (g_host.webview) g_host.webview->GoBack();
}

extern "C" void webview_host_go_forward(void) {
    if (g_host.webview) g_host.webview->GoForward();
}

extern "C" int webview_host_can_go_back(void) {
    BOOL value = FALSE;
    if (g_host.webview) g_host.webview->get_CanGoBack(&value);
    return value ? 1 : 0;
}

extern "C" int webview_host_can_go_forward(void) {
    BOOL value = FALSE;
    if (g_host.webview) g_host.webview->get_CanGoForward(&value);
    return value ? 1 : 0;
}

extern "C" void webview_host_destroy(void) {
    if (g_host.controller) {
        g_host.controller->Close();
        g_host.controller->Release();
    }
    if (g_host.webview) g_host.webview->Release();
    if (g_host.environment) g_host.environment->Release();
    g_host = WebViewHost{};
}
