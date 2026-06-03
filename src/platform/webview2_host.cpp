#include "kerosene.h"

#include <WebView2.h>
#include <atomic>
#include <cstdio>
#include <cwchar>

struct WebViewTab {
    ICoreWebView2Controller *controller = nullptr;
    ICoreWebView2 *webview = nullptr;
    bool allocated = false;
    bool creating = false;
    bool visible = false;
    unsigned generation = 1;
    wchar_t pending_url[K_MAX_URL] = L"";
};

struct WebViewHost {
    HWND hwnd = nullptr;
    ICoreWebView2Environment *environment = nullptr;
    RECT bounds = {0, 0, 1, 1};
    bool creating_environment = false;
    int active_id = -1;
    WebViewTab tabs[K_MAX_TABS];
};

static WebViewHost g_host;

static WebViewTab *tab_for(int tab_id) {
    if (tab_id < 0 || tab_id >= K_MAX_TABS) return nullptr;
    return &g_host.tabs[tab_id];
}

static wchar_t *utf8_to_wide_local(const char *text) {
    int len = MultiByteToWideChar(CP_UTF8, 0, text ? text : "", -1, nullptr, 0);
    if (len <= 0) return nullptr;
    wchar_t *out = (wchar_t *)kzalloc((size_t)len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, text ? text : "", -1, out, len);
    return out;
}

static void release_tab_view(WebViewTab *tab) {
    if (!tab) return;
    if (tab->controller) {
        tab->controller->Close();
        tab->controller->Release();
        tab->controller = nullptr;
    }
    if (tab->webview) {
        tab->webview->Release();
        tab->webview = nullptr;
    }
    tab->creating = false;
    tab->visible = false;
    tab->pending_url[0] = 0;
}

static void apply_tab_bounds(int tab_id) {
    WebViewTab *tab = tab_for(tab_id);
    if (!tab || !tab->controller) return;
    tab->controller->put_Bounds(g_host.bounds);
    tab->controller->put_IsVisible((tab->allocated && tab->visible && g_host.active_id == tab_id) ? TRUE : FALSE);
}

static void update_visibility() {
    for (int i = 0; i < K_MAX_TABS; i++) {
        WebViewTab *tab = &g_host.tabs[i];
        if (!tab->allocated) continue;
        tab->visible = i == g_host.active_id;
        apply_tab_bounds(i);
    }
}

static void ensure_controller(int tab_id);

class ControllerCompletedHandler final : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    std::atomic<ULONG> refs{1};
    int tab_id;
    unsigned generation;
public:
    ControllerCompletedHandler(int tab_id, unsigned generation) : tab_id(tab_id), generation(generation) {}

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
        WebViewTab *tab = tab_for(tab_id);
        if (!tab || !tab->allocated || tab->generation != generation) {
            if (result) result->Close();
            return S_OK;
        }

        tab->creating = false;
        if (FAILED(errorCode) || !result) return errorCode;

        tab->controller = result;
        tab->controller->AddRef();
        tab->controller->get_CoreWebView2(&tab->webview);

        ICoreWebView2Settings *settings = nullptr;
        if (tab->webview && SUCCEEDED(tab->webview->get_Settings(&settings)) && settings) {
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_AreDefaultContextMenusEnabled(TRUE);
            settings->put_IsScriptEnabled(TRUE);
            settings->Release();
        }

        apply_tab_bounds(tab_id);
        if (tab->webview && tab->pending_url[0]) {
            tab->webview->Navigate(tab->pending_url);
        }
        return S_OK;
    }
};

static void ensure_controller(int tab_id) {
    WebViewTab *tab = tab_for(tab_id);
    if (!tab || !tab->allocated || tab->controller || tab->creating || !g_host.environment) return;

    tab->creating = true;
    HRESULT hr = g_host.environment->CreateCoreWebView2Controller(
        g_host.hwnd,
        new ControllerCompletedHandler(tab_id, tab->generation));
    if (FAILED(hr)) {
        tab->creating = false;
    }
}

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
        g_host.creating_environment = false;
        if (FAILED(errorCode) || !result) return errorCode;

        g_host.environment = result;
        g_host.environment->AddRef();

        for (int i = 0; i < K_MAX_TABS; i++) {
            WebViewTab *tab = &g_host.tabs[i];
            if (tab->allocated && tab->pending_url[0]) {
                ensure_controller(i);
            }
        }
        update_visibility();
        return S_OK;
    }
};

extern "C" bool webview_host_init(HWND hwnd, int x, int y, int w, int h, char *err, size_t err_cap) {
    g_host.hwnd = hwnd;
    g_host.bounds = {x, y, x + (w > 1 ? w : 1), y + (h > 1 ? h : 1)};
    if (g_host.environment || g_host.creating_environment) return true;

    wchar_t data_dir[MAX_PATH];
    platform_get_data_dir(data_dir, MAX_PATH);
    wcscat_s(data_dir, L"\\WebView2");
    CreateDirectoryW(data_dir, nullptr);

    g_host.creating_environment = true;
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, data_dir, nullptr, new EnvironmentCompletedHandler());
    if (FAILED(hr)) {
        g_host.creating_environment = false;
        if (err && err_cap) {
            snprintf(err, err_cap, "WebView2 init falhou: 0x%08lx", (unsigned long)hr);
        }
        return false;
    }
    return true;
}

extern "C" int webview_host_create_tab(void) {
    for (int i = 0; i < K_MAX_TABS; i++) {
        WebViewTab *tab = &g_host.tabs[i];
        if (tab->allocated) continue;
        release_tab_view(tab);
        tab->allocated = true;
        tab->generation++;
        if (!tab->generation) tab->generation = 1;
        return i;
    }
    return -1;
}

extern "C" void webview_host_close_tab(int tab_id) {
    WebViewTab *tab = tab_for(tab_id);
    if (!tab || !tab->allocated) return;
    release_tab_view(tab);
    tab->allocated = false;
    tab->generation++;
    if (!tab->generation) tab->generation = 1;
    if (g_host.active_id == tab_id) {
        g_host.active_id = -1;
    }
    update_visibility();
}

extern "C" void webview_host_resize(int x, int y, int w, int h) {
    g_host.bounds = {x, y, x + (w > 1 ? w : 1), y + (h > 1 ? h : 1)};
    for (int i = 0; i < K_MAX_TABS; i++) {
        apply_tab_bounds(i);
    }
}

extern "C" void webview_host_activate(int tab_id) {
    WebViewTab *tab = tab_for(tab_id);
    g_host.active_id = (tab && tab->allocated) ? tab_id : -1;
    update_visibility();
    if (g_host.active_id >= 0) {
        ensure_controller(g_host.active_id);
    }
}

extern "C" void webview_host_navigate(int tab_id, const char *url) {
    WebViewTab *tab = tab_for(tab_id);
    if (!tab || !tab->allocated) return;

    wchar_t *wide = utf8_to_wide_local(url);
    if (!wide) return;

    wcsncpy_s(tab->pending_url, wide, _TRUNCATE);
    ensure_controller(tab_id);
    if (tab->webview) {
        tab->webview->Navigate(wide);
    }
    kfree(wide);
}

extern "C" void webview_host_reload(int tab_id) {
    WebViewTab *tab = tab_for(tab_id);
    if (tab && tab->webview) tab->webview->Reload();
}

extern "C" void webview_host_go_back(int tab_id) {
    WebViewTab *tab = tab_for(tab_id);
    if (tab && tab->webview) tab->webview->GoBack();
}

extern "C" void webview_host_go_forward(int tab_id) {
    WebViewTab *tab = tab_for(tab_id);
    if (tab && tab->webview) tab->webview->GoForward();
}

extern "C" int webview_host_can_go_back(int tab_id) {
    WebViewTab *tab = tab_for(tab_id);
    BOOL value = FALSE;
    if (tab && tab->webview) tab->webview->get_CanGoBack(&value);
    return value ? 1 : 0;
}

extern "C" int webview_host_can_go_forward(int tab_id) {
    WebViewTab *tab = tab_for(tab_id);
    BOOL value = FALSE;
    if (tab && tab->webview) tab->webview->get_CanGoForward(&value);
    return value ? 1 : 0;
}

extern "C" void webview_host_destroy(void) {
    for (int i = 0; i < K_MAX_TABS; i++) {
        release_tab_view(&g_host.tabs[i]);
    }
    if (g_host.environment) {
        g_host.environment->Release();
    }
    g_host = WebViewHost{};
}
