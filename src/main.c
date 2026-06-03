#include "kerosene.h"

#include <ole2.h>
#include <windowsx.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct KTab {
    KPage page;
    int webview_id;
    float scroll_y;
    char home_query[256];
    int home_cursor;
    int home_select_all;
    char history[32][K_MAX_URL];
    int history_count;
    int history_index;
} KTab;

typedef struct KApp {
    HWND hwnd;
    KRenderer *renderer;
    KeroseneUiTheme theme;
    KTab tabs[K_MAX_TABS];
    int tab_count;
    int active;
    int width;
    int height;
    int omnibox_focus;
    int omnibox_select_all;
    int omnibox_cursor;
    int home_search_focus;
    int menu_open;
    int prefer_webview;
    int dark_mode;
    int theme_animating;
    float theme_t;
    float theme_from;
    float theme_to;
    uint64_t theme_start_ms;
    char omnibox[K_MAX_URL];
    char renderer_status[160];
} KApp;

static const char *HOME_HTML =
"<!doctype html><html><head><title>Nova guia</title></head><body></body></html>";

typedef struct KShortcut {
    const char *label;
    const char *url;
    uint32_t color;
} KShortcut;

static const KShortcut HOME_SHORTCUTS[] = {
    { "Wiby", "https://wiby.me/", 0xFF1A73E8 },
    { "Wikipedia", "https://www.wikipedia.org/", 0xFF3C4043 },
    { "GitHub", "https://github.com/", 0xFF24292F },
    { "Example", "https://example.com/", 0xFF188038 },
    { "Apollo 11", "apollo 11", 0xFFD93025 },
    { "Docs", "https://developer.mozilla.org/", 0xFF5F6368 }
};

#define K_THEME_TIMER_ID 42
#define K_THEME_ANIM_MS 220.0f
#define K_WINDOW_BUTTON_W 46.0f
#define K_WINDOW_BUTTONS_W (K_WINDOW_BUTTON_W * 3.0f)

static KColor theme_color(KApp *app, uint32_t argb) {
    float t = app ? app->theme_t : 0.0f;
    KColor light = kcolor_from_argb(argb);
    KColor dark = light;
    if (app) {
        if (argb == app->theme.bg) dark = kcolor_from_argb(0xFF050505);
        else if (argb == app->theme.surface) dark = kcolor_from_argb(0xFF101114);
        else if (argb == app->theme.surface_hot) dark = kcolor_from_argb(0xFF1A1B20);
        else if (argb == app->theme.text) dark = kcolor_from_argb(0xFFF5F7FA);
        else if (argb == app->theme.muted) dark = kcolor_from_argb(0xFFA9AFB8);
        else if (argb == app->theme.accent) dark = kcolor_from_argb(0xFF8AB4F8);
        else if (argb == app->theme.page_bg) dark = kcolor_from_argb(0xFF000000);
    }
    return kcolor_lerp(light, dark, t);
}

static KColor theme_pair(KApp *app, uint32_t light_argb, uint32_t dark_argb) {
    float t = app ? app->theme_t : 0.0f;
    return kcolor_lerp(kcolor_from_argb(light_argb), kcolor_from_argb(dark_argb), t);
}

static float theme_ease(float p) {
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p * p * (3.0f - 2.0f * p);
}

static void theme_start_toggle(KApp *app) {
    app->dark_mode = !app->dark_mode;
    app->theme_from = app->theme_t;
    app->theme_to = app->dark_mode ? 1.0f : 0.0f;
    app->theme_start_ms = ktime_ms();
    app->theme_animating = 1;
    SetTimer(app->hwnd, K_THEME_TIMER_ID, 16, NULL);
    InvalidateRect(app->hwnd, NULL, FALSE);
}

static void theme_tick(KApp *app) {
    if (!app->theme_animating) return;
    float p = (float)(ktime_ms() - app->theme_start_ms) / K_THEME_ANIM_MS;
    float e = theme_ease(p);
    app->theme_t = app->theme_from + (app->theme_to - app->theme_from) * e;
    if (p >= 1.0f) {
        app->theme_t = app->theme_to;
        app->theme_animating = 0;
        KillTimer(app->hwnd, K_THEME_TIMER_ID);
    }
    InvalidateRect(app->hwnd, NULL, FALSE);
}

static float chrome_h(KApp *app) {
    return (float)(app->theme.tab_h + app->theme.toolbar_h);
}

static KTab *current_tab(KApp *app) {
    if (!app || app->active < 0 || app->active >= app->tab_count) return NULL;
    return &app->tabs[app->active];
}

static KPage *current_page(KApp *app) {
    KTab *tab = current_tab(app);
    return tab ? &tab->page : NULL;
}

static float current_scroll(KApp *app) {
    KTab *tab = current_tab(app);
    return tab ? tab->scroll_y : 0.0f;
}

static void set_current_scroll(KApp *app, float value) {
    KTab *tab = current_tab(app);
    if (tab) tab->scroll_y = value;
}

static char *current_home_query(KApp *app) {
    KTab *tab = current_tab(app);
    return tab ? tab->home_query : NULL;
}

static int *current_home_cursor(KApp *app) {
    KTab *tab = current_tab(app);
    return tab ? &tab->home_cursor : NULL;
}

static int *current_home_select_all(KApp *app) {
    KTab *tab = current_tab(app);
    return tab ? &tab->home_select_all : NULL;
}

static int current_webview_id(KApp *app) {
    KTab *tab = current_tab(app);
    return tab ? tab->webview_id : -1;
}

static KRect settings_panel_rect(KApp *app);

static void webview_content_bounds(KApp *app, int *x, int *y, int *w, int *h) {
    int top = (int)chrome_h(app);
    int width = app->width;
    int height = app->height - top;
    if (height < 1) height = 1;

    KPage *page = current_page(app);
    if (app->menu_open && page && page->compat_mode) {
        KRect panel = settings_panel_rect(app);
        int side_w = (int)(panel.x - 8.0f);
        if (side_w >= 280) {
            width = side_w;
        } else {
            width = side_w > 1 ? side_w : 1;
        }
    }

    *x = 0;
    *y = top;
    *w = width > 1 ? width : 1;
    *h = height;
}

static int is_home_page(const KPage *page) {
    return page && !_stricmp(page->url, "kerosene:home");
}

static int is_search_page(const KPage *page) {
    return page && !_strnicmp(page->url, "kerosene:search?q=", 18);
}

static int text_len_i(const char *text) {
    size_t len = strlen(text ? text : "");
    return len > 0x7fffffff ? 0x7fffffff : (int)len;
}

static int clamp_cursor(const char *text, int cursor) {
    int len = text_len_i(text);
    if (cursor < 0) return 0;
    if (cursor > len) return len;
    return cursor;
}

static void set_omnibox_text(KApp *app, const char *text) {
    strncpy(app->omnibox, text ? text : "", sizeof(app->omnibox) - 1);
    app->omnibox[sizeof(app->omnibox) - 1] = 0;
    app->omnibox_cursor = text_len_i(app->omnibox);
    app->omnibox_select_all = 0;
}

static void sync_omnibox_from_page(KApp *app) {
    KPage *page = current_page(app);
    if (!page || is_home_page(page)) {
        set_omnibox_text(app, "");
    } else if (is_search_page(page)) {
        set_omnibox_text(app, page->search_query);
    } else {
        set_omnibox_text(app, page->url);
    }
}

static char *focused_text(KApp *app, size_t *cap, int **cursor, int **select_all) {
    if (app->home_search_focus) {
        if (cap) *cap = sizeof(app->tabs[0].home_query);
        if (cursor) *cursor = current_home_cursor(app);
        if (select_all) *select_all = current_home_select_all(app);
        return current_home_query(app);
    }
    if (app->omnibox_focus) {
        if (cap) *cap = sizeof(app->omnibox);
        if (cursor) *cursor = &app->omnibox_cursor;
        if (select_all) *select_all = &app->omnibox_select_all;
        return app->omnibox;
    }
    return NULL;
}

static void text_delete_selection(char *text, int *cursor, int *select_all) {
    if (!text || !cursor || !select_all || !*select_all) return;
    text[0] = 0;
    *cursor = 0;
    *select_all = 0;
}

static void text_insert(KApp *app, const char *insert) {
    size_t cap = 0;
    int *cursor = NULL;
    int *select_all = NULL;
    char *text = focused_text(app, &cap, &cursor, &select_all);
    if (!text || !cursor || !select_all || !insert || !*insert || cap == 0) return;

    text_delete_selection(text, cursor, select_all);
    int len = text_len_i(text);
    int pos = clamp_cursor(text, *cursor);
    size_t ins_len = strlen(insert);
    if ((size_t)len + ins_len >= cap) {
        ins_len = cap > (size_t)len + 1 ? cap - (size_t)len - 1 : 0;
    }
    if (!ins_len) return;
    memmove(text + pos + ins_len, text + pos, (size_t)(len - pos) + 1);
    memcpy(text + pos, insert, ins_len);
    *cursor = pos + (int)ins_len;
}

static void text_backspace(KApp *app) {
    size_t cap = 0;
    int *cursor = NULL;
    int *select_all = NULL;
    char *text = focused_text(app, &cap, &cursor, &select_all);
    (void)cap;
    if (!text || !cursor || !select_all) return;
    if (*select_all) {
        text_delete_selection(text, cursor, select_all);
        return;
    }
    int pos = clamp_cursor(text, *cursor);
    if (pos <= 0) return;
    int len = text_len_i(text);
    memmove(text + pos - 1, text + pos, (size_t)(len - pos) + 1);
    *cursor = pos - 1;
}

static void text_delete_forward(KApp *app) {
    size_t cap = 0;
    int *cursor = NULL;
    int *select_all = NULL;
    char *text = focused_text(app, &cap, &cursor, &select_all);
    (void)cap;
    if (!text || !cursor || !select_all) return;
    if (*select_all) {
        text_delete_selection(text, cursor, select_all);
        return;
    }
    int pos = clamp_cursor(text, *cursor);
    int len = text_len_i(text);
    if (pos >= len) return;
    memmove(text + pos, text + pos + 1, (size_t)(len - pos));
}

static void text_move_cursor(KApp *app, int where) {
    size_t cap = 0;
    int *cursor = NULL;
    int *select_all = NULL;
    char *text = focused_text(app, &cap, &cursor, &select_all);
    (void)cap;
    if (!text || !cursor || !select_all) return;
    int len = text_len_i(text);
    if (where == VK_LEFT) {
        *cursor = *select_all ? 0 : clamp_cursor(text, *cursor - 1);
    } else if (where == VK_RIGHT) {
        *cursor = *select_all ? len : clamp_cursor(text, *cursor + 1);
    } else if (where == VK_HOME) {
        *cursor = 0;
    } else if (where == VK_END) {
        *cursor = len;
    }
    *select_all = 0;
}

static void text_select_all(KApp *app) {
    size_t cap = 0;
    int *cursor = NULL;
    int *select_all = NULL;
    char *text = focused_text(app, &cap, &cursor, &select_all);
    (void)cap;
    if (!text || !cursor || !select_all) return;
    *cursor = text_len_i(text);
    *select_all = text[0] != 0;
}

static int cursor_from_x(const char *text, float text_x, float char_w, float x) {
    int len = text_len_i(text);
    int pos = (int)floorf((x - text_x) / char_w + 0.5f);
    if (pos < 0) return 0;
    if (pos > len) return len;
    return pos;
}

static void clipboard_set_text(HWND hwnd, const char *text) {
    if (!text) return;
    int wide_len = 0;
    wchar_t *wide = platform_utf8_to_wide(text, &wide_len);
    if (!wide) return;
    size_t bytes = ((size_t)wide_len + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        kfree(wide);
        return;
    }
    void *dst = GlobalLock(mem);
    if (!dst) {
        GlobalFree(mem);
        kfree(wide);
        return;
    }
    memcpy(dst, wide, bytes);
    GlobalUnlock(mem);
    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        SetClipboardData(CF_UNICODETEXT, mem);
        CloseClipboard();
    } else {
        GlobalFree(mem);
    }
    kfree(wide);
}

static int clipboard_get_text(HWND hwnd, char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = 0;
    if (!OpenClipboard(hwnd)) return 0;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (!data) {
        CloseClipboard();
        return 0;
    }
    wchar_t *wide = (wchar_t *)GlobalLock(data);
    if (!wide) {
        CloseClipboard();
        return 0;
    }
    int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (needed > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int)cap, NULL, NULL);
        out[cap - 1] = 0;
    }
    GlobalUnlock(data);
    CloseClipboard();
    return out[0] != 0;
}

static void text_copy(KApp *app) {
    size_t cap = 0;
    int *cursor = NULL;
    int *select_all = NULL;
    char *text = focused_text(app, &cap, &cursor, &select_all);
    (void)cap;
    (void)cursor;
    if (!text || !text[0]) return;
    if (select_all && *select_all) {
        clipboard_set_text(app->hwnd, text);
    } else {
        clipboard_set_text(app->hwnd, text);
    }
}

static void text_cut(KApp *app) {
    size_t cap = 0;
    int *cursor = NULL;
    int *select_all = NULL;
    char *text = focused_text(app, &cap, &cursor, &select_all);
    (void)cap;
    if (!text || !select_all || !*select_all) return;
    clipboard_set_text(app->hwnd, text);
    text_delete_selection(text, cursor, select_all);
}

static void text_paste(KApp *app) {
    char text[2048];
    if (clipboard_get_text(app->hwnd, text, sizeof(text))) {
        text_insert(app, text);
    }
}

static void sync_webview_visibility(KApp *app) {
    KTab *tab = current_tab(app);
    KPage *page = tab ? &tab->page : NULL;
    int x, y, w, h;
    webview_content_bounds(app, &x, &y, &w, &h);
    webview_host_resize(x, y, w, h);
    webview_host_activate((tab && page && page->compat_mode) ? tab->webview_id : -1);
}

static void close_menu(KApp *app) {
    if (!app->menu_open) return;
    app->menu_open = 0;
    sync_webview_visibility(app);
}

static void clear_text_focus(KApp *app) {
    app->omnibox_focus = 0;
    app->omnibox_select_all = 0;
    app->home_search_focus = 0;
    int *home_select = current_home_select_all(app);
    if (home_select) *home_select = 0;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode_query(const char *src, char *out, size_t cap) {
    size_t j = 0;
    for (size_t i = 0; src && src[i] && j + 1 < cap; i++) {
        if (src[i] == '+') {
            out[j++] = ' ';
        } else if (src[i] == '%' && src[i + 1] && src[i + 2]) {
            int a = hex_digit(src[i + 1]);
            int b = hex_digit(src[i + 2]);
            if (a >= 0 && b >= 0) {
                out[j++] = (char)((a << 4) | b);
                i += 2;
            }
        } else {
            out[j++] = src[i];
        }
    }
    if (cap) out[j] = 0;
}

static void url_encode_query(const char *src, char *out, size_t cap) {
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;
    for (const unsigned char *p = (const unsigned char *)(src ? src : ""); *p && j + 4 < cap; p++) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
            out[j++] = (char)*p;
        } else if (*p == ' ') {
            out[j++] = '+';
        } else {
            out[j++] = '%';
            out[j++] = hex[*p >> 4];
            out[j++] = hex[*p & 15];
        }
    }
    if (cap) out[j < cap ? j : cap - 1] = 0;
}

static int parse_positive_int(const char *text, int fallback) {
    if (!text || !isdigit((unsigned char)*text)) return fallback;
    int value = 0;
    while (isdigit((unsigned char)*text)) {
        value = value * 10 + (*text - '0');
        if (value > 9999) return 9999;
        text++;
    }
    return value > 0 ? value : fallback;
}

static void parse_search_url(const char *url, char *query, size_t query_cap, int *page_out) {
    if (query && query_cap) query[0] = 0;
    if (page_out) *page_out = 1;
    const char *params = url ? strstr(url, "kerosene:search?") : NULL;
    if (!params) return;
    params += 16;
    const char *q = strstr(params, "q=");
    if (q && query && query_cap) {
        q += 2;
        const char *end = strchr(q, '&');
        char encoded[512];
        size_t len = end ? (size_t)(end - q) : strlen(q);
        if (len >= sizeof(encoded)) len = sizeof(encoded) - 1;
        memcpy(encoded, q, len);
        encoded[len] = 0;
        url_decode_query(encoded, query, query_cap);
    }
    const char *page = strstr(params, "page=");
    if (page_out && page) {
        *page_out = parse_positive_int(page + 5, 1);
    }
}

static void make_search_url(const char *query, int page, char *out, size_t cap) {
    char encoded[512];
    url_encode_query(query, encoded, sizeof(encoded));
    if (page < 1) page = 1;
    snprintf(out, cap, "kerosene:search?q=%s&page=%d", encoded, page);
}

static void page_clear(KPage *page) {
    cyclone_free_dom(page->document);
    page->document = NULL;
    render_list_free(&page->render);
    kfree(page->html);
    kfree(page->reader_text);
    memset(page, 0, sizeof(*page));
}

static void page_init(KPage *page) {
    memset(page, 0, sizeof(*page));
    render_list_init(&page->render);
}

static void clamp_scroll(KApp *app) {
    KTab *tab = current_tab(app);
    KPage *page = tab ? &tab->page : NULL;
    if (!page) {
        if (tab) tab->scroll_y = 0.0f;
        return;
    }
    float viewport = (float)app->height - chrome_h(app);
    float max_scroll = page->render.content_height - viewport;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    if (tab->scroll_y < 0.0f) tab->scroll_y = 0.0f;
    if (tab->scroll_y > max_scroll) tab->scroll_y = max_scroll;
}

static void layout_active(KApp *app) {
    KPage *page = current_page(app);
    if (!page || !page->document) return;
    float viewport_w = (float)app->width;
    float viewport_h = (float)app->height - chrome_h(app);
    cyclone_layout_page(page->document, &page->render, viewport_w, viewport_h, page->reader_mode);
    clamp_scroll(app);
}

static void set_status(KPage *page, const char *text) {
    if (!page) return;
    strncpy(page->status, text ? text : "", sizeof(page->status) - 1);
    page->status[sizeof(page->status) - 1] = 0;
}

static void apply_document_pipeline(KApp *app, KPage *page, const char *url, const char *html) {
    cyclone_free_dom(page->document);
    page->document = NULL;
    kfree(page->html);
    kfree(page->reader_text);
    render_list_reset(&page->render);
    page->html = kstrdup(html ? html : "");
    page->bytes_used = strlen(page->html);
    page->frozen = page->bytes_used > K_TAB_MEMORY_LIMIT;
    strncpy(page->url, url ? url : "kerosene:home", sizeof(page->url) - 1);

    if (page->frozen) {
        const char *frozen = "<html><head><title>Aba congelada</title></head><body><h1>Esta aba esta usando recursos demais</h1><p>Limite por aba: 50MB.</p></body></html>";
        kfree(page->html);
        page->html = kstrdup(frozen);
    }

    page->document = cyclone_parse_html(page->html);
    char *style_text = cyclone_extract_style_text(page->document);
    KCssRule *css = cyclone_parse_css(style_text);
    cyclone_apply_styles(page->document, css);
    spark_run_document(page->document, page->url, page->status, sizeof(page->status));
    cyclone_apply_styles(page->document, css);
    cyclone_extract_title(page->document, page->title, sizeof(page->title));
    if (!page->title[0]) {
        strncpy(page->title, page->url, sizeof(page->title) - 1);
    }
    page->reader_text = cyclone_collect_text(page->document);
    cyclone_free_css(css);
    kfree(style_text);
    layout_active(app);
}

static void make_error_page(char *out, size_t cap, const char *url, const char *err) {
    snprintf(out, cap,
        "<html><head><title>Erro</title><style>body{padding:28px;font-family:Segoe UI;}pre{background:#f3f5f8;padding:12px;border:1px solid #d8dee9;}</style></head>"
        "<body><h1>Nao foi possivel abrir</h1><p>%s</p><pre>%s</pre></body></html>",
        url ? url : "", err ? err : "erro desconhecido");
}

static void normalize_or_copy(const char *input, char *out, size_t cap) {
    if (!kerosene_ui_normalize_omnibox(input, out, cap)) {
        strncpy(out, "kerosene:home", cap - 1);
        out[cap - 1] = 0;
    }
}

static void app_load_url_with_history(KApp *app, const char *input, int add_history);

static void app_load_url(KApp *app, const char *input) {
    app_load_url_with_history(app, input, 1);
}

static void history_record(KTab *tab, const char *url) {
    if (!tab || !url || !*url) return;
    if (tab->history_index >= 0 && tab->history_index < tab->history_count &&
        !_stricmp(tab->history[tab->history_index], url)) {
        return;
    }
    if (tab->history_index < tab->history_count - 1) {
        tab->history_count = tab->history_index + 1;
    }
    if (tab->history_count >= (int)(sizeof(tab->history) / sizeof(tab->history[0]))) {
        memmove(tab->history[0], tab->history[1], (sizeof(tab->history) / sizeof(tab->history[0]) - 1) * K_MAX_URL);
        tab->history_count--;
        if (tab->history_index > 0) tab->history_index--;
    }
    strncpy(tab->history[tab->history_count], url, K_MAX_URL - 1);
    tab->history[tab->history_count][K_MAX_URL - 1] = 0;
    tab->history_index = tab->history_count;
    tab->history_count++;
}

static void app_load_url_with_history(KApp *app, const char *input, int add_history) {
    KPage *page = current_page(app);
    if (!page) return;
    KTab *tab = &app->tabs[app->active];
    close_menu(app);
    clear_text_focus(app);

    char url[K_MAX_URL];
    normalize_or_copy(input, url, sizeof(url));
    if (add_history) {
        history_record(tab, url);
    }
    set_omnibox_text(app, url);
    tab->scroll_y = 0.0f;
    set_status(page, "Carregando");

    if (!_stricmp(url, "kerosene:home")) {
        page->compat_mode = 0;
        tab->home_query[0] = 0;
        tab->home_cursor = 0;
        tab->home_select_all = 0;
        set_omnibox_text(app, "");
        sync_webview_visibility(app);
        apply_document_pipeline(app, page, url, HOME_HTML);
        set_status(page, app->renderer_status);
        InvalidateRect(app->hwnd, NULL, FALSE);
        return;
    }

    if (!_strnicmp(url, "kerosene:search?q=", 18)) {
        page->compat_mode = 0;
        sync_webview_visibility(app);
        cyclone_free_dom(page->document);
        page->document = cyclone_parse_html("<html><head><title>Busca</title></head><body></body></html>");
        kfree(page->html);
        kfree(page->reader_text);
        page->html = kstrdup("");
        page->reader_text = NULL;
        render_list_reset(&page->render);
        memset(page->search_results, 0, sizeof(page->search_results));
        page->search_count = 0;
        page->search_provider[0] = 0;
        page->search_total_label[0] = 0;
        parse_search_url(url, page->search_query, sizeof(page->search_query), &page->search_page);
        strncpy(page->url, url, sizeof(page->url) - 1);
        snprintf(page->title, sizeof(page->title), "%s", page->search_query[0] ? page->search_query : "Busca");
        set_omnibox_text(app, page->search_query);
        char err[256] = {0};
        uint64_t start = ktime_ms();
        page->search_count = search_fetch_results(page->search_query, page->search_page, page->search_results, K_MAX_SEARCH_RESULTS,
            page->search_provider, sizeof(page->search_provider), page->search_total_label, sizeof(page->search_total_label), err, sizeof(err));
        page->render.content_height = 250.0f + (float)(page->search_count > 0 ? page->search_count : 1) * 154.0f;
        if (page->search_count > 0) {
            snprintf(page->status, sizeof(page->status), "%d resultados via %s, %llu ms",
                page->search_count, page->search_provider, (unsigned long long)(ktime_ms() - start));
        } else {
            snprintf(page->status, sizeof(page->status), "%s", err[0] ? err : "Nenhum resultado");
        }
        clamp_scroll(app);
        InvalidateRect(app->hwnd, NULL, FALSE);
        return;
    }

    if (app->prefer_webview) {
        page->compat_mode = 1;
        cyclone_free_dom(page->document);
        page->document = NULL;
        kfree(page->html);
        page->html = kstrdup("");
        kfree(page->reader_text);
        page->reader_text = NULL;
        render_list_reset(&page->render);
        memset(page->search_results, 0, sizeof(page->search_results));
        page->search_count = 0;
        page->search_query[0] = 0;
        strncpy(page->url, url, sizeof(page->url) - 1);
        page->url[sizeof(page->url) - 1] = 0;
        strncpy(page->title, url, sizeof(page->title) - 1);
        page->title[sizeof(page->title) - 1] = 0;
        snprintf(page->status, sizeof(page->status), "Modo compatibilidade WebView2");
        sync_webview_visibility(app);
        webview_host_navigate(tab->webview_id, url);
        InvalidateRect(app->hwnd, NULL, FALSE);
        return;
    }

    page->compat_mode = 0;
    sync_webview_visibility(app);

    KHttpResponse response;
    char err[256] = {0};
    uint64_t start = ktime_ms();
    if (http_get(url, &response, err, sizeof(err))) {
        apply_document_pipeline(app, page, response.final_url[0] ? response.final_url : url, response.body ? response.body : "");
        char status[256];
        snprintf(status, sizeof(status), "HTTP %d, %llu ms, %zu bytes, %s",
            response.status, (unsigned long long)(ktime_ms() - start), response.body_len, quic_status_text());
        set_status(page, status);
        http_response_free(&response);
    } else {
        char html[2048];
        make_error_page(html, sizeof(html), url, err);
        apply_document_pipeline(app, page, url, html);
        set_status(page, err);
    }
    InvalidateRect(app->hwnd, NULL, FALSE);
}

static void app_go_history(KApp *app, int delta) {
    if (!app || app->active < 0 || app->active >= app->tab_count) return;
    KTab *tab = &app->tabs[app->active];
    int next = tab->history_index + delta;
    if (next < 0 || next >= tab->history_count) return;
    tab->history_index = next;
    app_load_url_with_history(app, tab->history[next], 0);
}

static void app_new_tab(KApp *app, const char *url) {
    if (app->tab_count >= K_MAX_TABS) return;
    int idx = app->tab_count++;
    memset(&app->tabs[idx], 0, sizeof(app->tabs[idx]));
    page_init(&app->tabs[idx].page);
    app->tabs[idx].webview_id = webview_host_create_tab();
    app->tabs[idx].history_count = 0;
    app->tabs[idx].history_index = -1;
    app->active = idx;
    app_load_url(app, url ? url : "kerosene:home");
}

static void app_close_tab(KApp *app, int idx) {
    if (app->tab_count <= 1 || idx < 0 || idx >= app->tab_count) return;
    webview_host_close_tab(app->tabs[idx].webview_id);
    page_clear(&app->tabs[idx].page);
    for (int i = idx; i < app->tab_count - 1; i++) {
        app->tabs[i] = app->tabs[i + 1];
    }
    memset(&app->tabs[app->tab_count - 1], 0, sizeof(app->tabs[app->tab_count - 1]));
    app->tab_count--;
    if (app->active > idx) {
        app->active--;
    } else if (app->active >= app->tab_count) {
        app->active = app->tab_count - 1;
    }
    KPage *page = current_page(app);
    (void)page;
    sync_omnibox_from_page(app);
    sync_webview_visibility(app);
    InvalidateRect(app->hwnd, NULL, FALSE);
}

static void app_reload_active_tab(KApp *app) {
    KPage *page = current_page(app);
    if (!page) return;
    if (page->compat_mode) {
        webview_host_reload(current_webview_id(app));
    } else {
        app_load_url_with_history(app, page->url, 0);
    }
}

static void app_toggle_site_engine(KApp *app) {
    KPage *page = current_page(app);
    char url[K_MAX_URL] = {0};
    int reload = page && page->url[0] && strstr(page->url, "://");
    if (reload) {
        strncpy(url, page->url, sizeof(url) - 1);
    }

    app->prefer_webview = !app->prefer_webview;
    if (reload) {
        app_load_url_with_history(app, url, 0);
    } else {
        sync_webview_visibility(app);
        InvalidateRect(app->hwnd, NULL, FALSE);
    }
}

static void resolve_url(const char *base, const char *href, char *out, size_t cap) {
    if (!href || !*href) {
        strncpy(out, base ? base : "kerosene:home", cap - 1);
        out[cap - 1] = 0;
        return;
    }
    if (strstr(href, "://") || !_strnicmp(href, "kerosene:", 9)) {
        strncpy(out, href, cap - 1);
        out[cap - 1] = 0;
        return;
    }
    if (!_strnicmp(href, "//", 2)) {
        snprintf(out, cap, "https:%s", href);
        return;
    }
    char scheme_host[K_MAX_URL] = {0};
    const char *scheme = strstr(base ? base : "", "://");
    if (!scheme) {
        normalize_or_copy(href, out, cap);
        return;
    }
    const char *host_start = scheme + 3;
    const char *path_start = strchr(host_start, '/');
    size_t root_len = path_start ? (size_t)(path_start - (base ? base : "")) : strlen(base);
    if (root_len >= sizeof(scheme_host)) root_len = sizeof(scheme_host) - 1;
    memcpy(scheme_host, base, root_len);
    scheme_host[root_len] = 0;

    if (href[0] == '/') {
        snprintf(out, cap, "%s%s", scheme_host, href);
    } else {
        char dir[K_MAX_URL];
        strncpy(dir, base, sizeof(dir) - 1);
        char *slash = strrchr(dir, '/');
        if (slash && slash > strstr(dir, "://") + 2) {
            slash[1] = 0;
        } else {
            snprintf(dir, sizeof(dir), "%s/", scheme_host);
        }
        snprintf(out, cap, "%s%s", dir, href);
    }
}

static int point_in(KRect r, float x, float y) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

enum {
    ICON_BACK,
    ICON_FORWARD,
    ICON_RELOAD,
    ICON_HOME,
    ICON_MENU,
    ICON_PLUS,
    ICON_CLOSE,
    ICON_MINIMIZE,
    ICON_MAXIMIZE
};

static void draw_icon(KApp *app, int icon, float x, float y, float w, float h, KColor color) {
    float cx = x + w * 0.5f;
    float cy = y + h * 0.5f;
    float s = fminf(w, h);
    float lw = 1.8f;
    if (icon == ICON_BACK || icon == ICON_FORWARD) {
        float dir = icon == ICON_BACK ? -1.0f : 1.0f;
        float ax = cx - dir * s * 0.18f;
        float bx = cx + dir * s * 0.18f;
        renderer_draw_line(app->renderer, bx, cy - s * 0.24f, ax, cy, color, lw);
        renderer_draw_line(app->renderer, bx, cy + s * 0.24f, ax, cy, color, lw);
        renderer_draw_line(app->renderer, ax, cy, cx + dir * s * 0.28f, cy, color, lw);
    } else if (icon == ICON_RELOAD) {
        float r = s * 0.24f;
        const float pts[7][2] = {
            {-0.60f, -0.10f}, {-0.42f, -0.42f}, {-0.05f, -0.58f},
            {0.34f, -0.44f}, {0.56f, -0.10f}, {0.45f, 0.28f}, {0.12f, 0.50f}
        };
        for (int i = 0; i < 6; i++) {
            renderer_draw_line(app->renderer, cx + pts[i][0] * r, cy + pts[i][1] * r,
                cx + pts[i + 1][0] * r, cy + pts[i + 1][1] * r, color, lw);
        }
        renderer_draw_line(app->renderer, cx - r * 0.60f, cy - r * 0.10f, cx - r * 0.58f, cy - r * 0.45f, color, lw);
        renderer_draw_line(app->renderer, cx - r * 0.60f, cy - r * 0.10f, cx - r * 0.28f, cy - r * 0.20f, color, lw);
    } else if (icon == ICON_HOME) {
        renderer_draw_line(app->renderer, cx - s * 0.26f, cy - s * 0.02f, cx, cy - s * 0.28f, color, lw);
        renderer_draw_line(app->renderer, cx, cy - s * 0.28f, cx + s * 0.26f, cy - s * 0.02f, color, lw);
        renderer_draw_line(app->renderer, cx - s * 0.20f, cy - s * 0.02f, cx - s * 0.20f, cy + s * 0.24f, color, lw);
        renderer_draw_line(app->renderer, cx + s * 0.20f, cy - s * 0.02f, cx + s * 0.20f, cy + s * 0.24f, color, lw);
        renderer_draw_line(app->renderer, cx - s * 0.20f, cy + s * 0.24f, cx + s * 0.20f, cy + s * 0.24f, color, lw);
    } else if (icon == ICON_MENU) {
        for (int i = -1; i <= 1; i++) {
            float yy = cy + (float)i * s * 0.18f;
            renderer_fill_rect(app->renderer, (KRect){cx - 2.0f, yy - 2.0f, 4.0f, 4.0f}, color, 2.0f);
        }
    } else if (icon == ICON_PLUS) {
        renderer_draw_line(app->renderer, cx - s * 0.18f, cy, cx + s * 0.18f, cy, color, lw);
        renderer_draw_line(app->renderer, cx, cy - s * 0.18f, cx, cy + s * 0.18f, color, lw);
    } else if (icon == ICON_CLOSE) {
        renderer_draw_line(app->renderer, cx - s * 0.14f, cy - s * 0.14f, cx + s * 0.14f, cy + s * 0.14f, color, lw);
        renderer_draw_line(app->renderer, cx + s * 0.14f, cy - s * 0.14f, cx - s * 0.14f, cy + s * 0.14f, color, lw);
    } else if (icon == ICON_MINIMIZE) {
        renderer_draw_line(app->renderer, cx - s * 0.18f, cy + s * 0.16f, cx + s * 0.18f, cy + s * 0.16f, color, lw);
    } else if (icon == ICON_MAXIMIZE) {
        renderer_draw_border(app->renderer, (KRect){cx - s * 0.16f, cy - s * 0.16f, s * 0.32f, s * 0.32f}, color, lw, 1.0f);
    }
}

static void draw_icon_button(KApp *app, float x, float y, float w, float h, int icon, int enabled) {
    KColor icon_color = enabled ? theme_color(app, app->theme.text) : theme_pair(app, 0x995F6368, 0x667A808A);
    renderer_fill_rect(app->renderer, (KRect){x, y, w, h}, kcolor_rgba(0.0f, 0.0f, 0.0f, 0.0f), h * 0.5f);
    draw_icon(app, icon, x, y, w, h, icon_color);
}

static void draw_lock_icon(KApp *app, float x, float y, KColor color) {
    renderer_draw_line(app->renderer, x + 5.0f, y + 12.0f, x + 15.0f, y + 12.0f, color, 1.4f);
    renderer_draw_line(app->renderer, x + 5.0f, y + 12.0f, x + 5.0f, y + 21.0f, color, 1.4f);
    renderer_draw_line(app->renderer, x + 15.0f, y + 12.0f, x + 15.0f, y + 21.0f, color, 1.4f);
    renderer_draw_line(app->renderer, x + 5.0f, y + 21.0f, x + 15.0f, y + 21.0f, color, 1.4f);
    renderer_draw_line(app->renderer, x + 7.0f, y + 12.0f, x + 7.0f, y + 8.0f, color, 1.4f);
    renderer_draw_line(app->renderer, x + 13.0f, y + 12.0f, x + 13.0f, y + 8.0f, color, 1.4f);
    renderer_draw_line(app->renderer, x + 7.0f, y + 8.0f, x + 13.0f, y + 8.0f, color, 1.4f);
}

static KRect theme_toggle_rect(KApp *app) {
    float y = (float)app->theme.tab_h + 13.0f;
    return (KRect){ (float)app->width - K_WINDOW_BUTTONS_W - 101.0f, y, 48.0f, 28.0f };
}

static void draw_theme_toggle(KApp *app) {
    KRect r = theme_toggle_rect(app);
    float t = app->theme_t;
    KColor track = theme_pair(app, 0xFFE7EAEE, 0xFF202124);
    KColor border = theme_pair(app, 0xFFD0D5DD, 0xFF3A3D45);
    KColor knob = theme_pair(app, 0xFFFFC947, 0xFFF1F3F4);
    renderer_fill_rect(app->renderer, r, track, 14.0f);
    renderer_draw_border(app->renderer, r, border, 1.0f, 14.0f);

    float knob_x = r.x + 3.0f + (r.w - 26.0f) * t;
    KRect k = { knob_x, r.y + 3.0f, 22.0f, 22.0f };
    renderer_fill_rect(app->renderer, (KRect){k.x, k.y + 1.0f, k.w, k.h}, kcolor_rgba(0, 0, 0, 0.16f), 11.0f);
    renderer_fill_rect(app->renderer, k, knob, 11.0f);

    if (t < 0.55f) {
        KColor sun = kcolor_from_argb(0xFF8A5A00);
        float cx = k.x + 11.0f;
        float cy = k.y + 11.0f;
        renderer_fill_rect(app->renderer, (KRect){cx - 4.0f, cy - 4.0f, 8.0f, 8.0f}, sun, 4.0f);
        renderer_draw_line(app->renderer, cx, cy - 8.0f, cx, cy - 6.0f, sun, 1.2f);
        renderer_draw_line(app->renderer, cx, cy + 6.0f, cx, cy + 8.0f, sun, 1.2f);
        renderer_draw_line(app->renderer, cx - 8.0f, cy, cx - 6.0f, cy, sun, 1.2f);
        renderer_draw_line(app->renderer, cx + 6.0f, cy, cx + 8.0f, cy, sun, 1.2f);
    } else {
        KColor moon = kcolor_from_argb(0xFF111318);
        renderer_fill_rect(app->renderer, (KRect){k.x + 7.0f, k.y + 5.0f, 10.0f, 12.0f}, moon, 6.0f);
        renderer_fill_rect(app->renderer, (KRect){k.x + 11.0f, k.y + 4.0f, 8.0f, 14.0f}, knob, 7.0f);
    }
}

static KRect menu_button_rect(KApp *app) {
    return (KRect){ (float)app->width - K_WINDOW_BUTTONS_W - 42.0f, (float)app->theme.tab_h + 10.0f, 32.0f, 31.0f };
}

static KRect window_minimize_rect(KApp *app) {
    return (KRect){ (float)app->width - K_WINDOW_BUTTONS_W, 0.0f, K_WINDOW_BUTTON_W, (float)app->theme.tab_h };
}

static KRect window_maximize_rect(KApp *app) {
    return (KRect){ (float)app->width - K_WINDOW_BUTTONS_W + K_WINDOW_BUTTON_W, 0.0f, K_WINDOW_BUTTON_W, (float)app->theme.tab_h };
}

static KRect window_close_rect(KApp *app) {
    return (KRect){ (float)app->width - K_WINDOW_BUTTON_W, 0.0f, K_WINDOW_BUTTON_W, (float)app->theme.tab_h };
}

static int point_in_window_buttons(KApp *app, float x, float y) {
    return point_in(window_minimize_rect(app), x, y) ||
        point_in(window_maximize_rect(app), x, y) ||
        point_in(window_close_rect(app), x, y);
}

static KRect settings_panel_rect(KApp *app) {
    float top = chrome_h(app);
    float w = 320.0f;
    if (app->width < 760) w = (float)app->width - 16.0f;
    if (w < 280.0f) w = fmaxf(220.0f, (float)app->width - 16.0f);
    float x = (float)app->width - w;
    if (x < 8.0f) x = 8.0f;
    return (KRect){ x, top, w, fmaxf(260.0f, (float)app->height - top) };
}

static KRect settings_row(KApp *app, int idx) {
    KRect p = settings_panel_rect(app);
    return (KRect){ p.x + 14.0f, p.y + 56.0f + (float)idx * 48.0f, p.w - 28.0f, 42.0f };
}

static KRect settings_theme_row(KApp *app) {
    return settings_row(app, 0);
}

static KRect settings_engine_row(KApp *app) {
    return settings_row(app, 1);
}

static KRect settings_new_tab_row(KApp *app) {
    return settings_row(app, 2);
}

static KRect settings_reload_row(KApp *app) {
    return settings_row(app, 3);
}

static KRect settings_reader_row(KApp *app) {
    return settings_row(app, 4);
}

static KRect settings_external_row(KApp *app) {
    return settings_row(app, 5);
}

static void draw_settings_switch(KApp *app, KRect row, int on, int enabled) {
    KRect r = { row.x + row.w - 58.0f, row.y + 7.0f, 48.0f, 28.0f };
    KColor track = enabled
        ? (on ? theme_color(app, app->theme.accent) : theme_pair(app, 0xFFE7EAEE, 0xFF202124))
        : theme_pair(app, 0xFFD8DDE5, 0xFF24262C);
    KColor border = theme_pair(app, 0xFFD0D5DD, 0xFF3A3D45);
    KColor knob = enabled ? theme_pair(app, 0xFFFFFFFF, 0xFFF1F3F4) : theme_pair(app, 0xFFF2F4F7, 0xFF707783);
    renderer_fill_rect(app->renderer, r, track, 14.0f);
    renderer_draw_border(app->renderer, r, border, 1.0f, 14.0f);
    float knob_x = r.x + 3.0f + (on ? r.w - 26.0f : 0.0f);
    renderer_fill_rect(app->renderer, (KRect){knob_x, r.y + 3.0f, 22.0f, 22.0f}, knob, 11.0f);
}

static void draw_settings_row(KApp *app, KRect row, const char *label, const char *value, int enabled) {
    KColor text = enabled ? theme_color(app, app->theme.text) : theme_pair(app, 0x995F6368, 0x667A808A);
    KColor muted = enabled ? theme_color(app, app->theme.muted) : theme_pair(app, 0x885F6368, 0x557A808A);
    renderer_fill_rect(app->renderer, row, theme_pair(app, 0xFFF6F8FB, 0xFF15171B), 8.0f);
    renderer_draw_text(app->renderer, (KRect){row.x + 12.0f, row.y + 7.0f, row.w - 88.0f, 18.0f}, text, label, 13.0f, 600, 0);
    if (value && value[0]) {
        renderer_draw_text(app->renderer, (KRect){row.x + 12.0f, row.y + 24.0f, row.w - 88.0f, 16.0f}, muted, value, 11.0f, 400, 0);
    }
}

static void draw_settings_panel(KApp *app) {
    if (!app->menu_open) return;
    KRect p = settings_panel_rect(app);
    KColor surface = theme_color(app, app->theme.surface);
    KColor text = theme_color(app, app->theme.text);
    KColor muted = theme_color(app, app->theme.muted);
    renderer_fill_rect(app->renderer, (KRect){p.x - 3.0f, p.y, 3.0f, p.h}, theme_pair(app, 0x18000000, 0xAA000000), 0);
    renderer_fill_rect(app->renderer, p, surface, 0);
    renderer_draw_border(app->renderer, (KRect){p.x, p.y, 1.0f, p.h}, theme_pair(app, 0xFFE1E5EA, 0xFF2C3038), 1.0f, 0);
    renderer_draw_text(app->renderer, (KRect){p.x + 18.0f, p.y + 17.0f, p.w - 36.0f, 24.0f}, text, "Configuracoes", 18.0f, 700, 0);

    KRect theme_row = settings_theme_row(app);
    draw_settings_row(app, theme_row, "Tema", app->dark_mode ? "Preto" : "Claro", 1);
    draw_settings_switch(app, theme_row, app->dark_mode, 1);

    KRect engine_row = settings_engine_row(app);
    draw_settings_row(app, engine_row, "Motor de sites", app->prefer_webview ? "WebView2 compatibilidade" : "Renderizador nativo", 1);
    draw_settings_switch(app, engine_row, app->prefer_webview, 1);

    draw_settings_row(app, settings_new_tab_row(app), "Nova guia", "Abrir uma aba limpa", 1);
    draw_settings_row(app, settings_reload_row(app), "Recarregar aba", "Atualizar a pagina atual", 1);

    KPage *page = current_page(app);
    int reader_enabled = page && page->document && !page->compat_mode && !is_home_page(page) && !is_search_page(page);
    draw_settings_row(app, settings_reader_row(app), "Modo leitura", reader_enabled ? (page->reader_mode ? "Ligado nesta aba" : "Desligado nesta aba") : "Indisponivel nesta pagina", reader_enabled);
    draw_settings_switch(app, settings_reader_row(app), page && page->reader_mode, reader_enabled);

    int external_enabled = page && page->url[0] && strstr(page->url, "://");
    draw_settings_row(app, settings_external_row(app), "Abrir no navegador", external_enabled ? page->url : "Indisponivel para paginas internas", external_enabled);

    renderer_draw_text(app->renderer, (KRect){p.x + 18.0f, p.y + p.h - 64.0f, p.w - 36.0f, 18.0f}, muted, "Busca: Bing com fallback Wiby", 12.0f, 400, 0);
    renderer_draw_text(app->renderer, (KRect){p.x + 18.0f, p.y + p.h - 42.0f, p.w - 36.0f, 18.0f}, muted, "Abas: estado preservado", 12.0f, 400, 0);
    renderer_draw_text(app->renderer, (KRect){p.x + 18.0f, p.y + p.h - 20.0f, p.w - 36.0f, 18.0f}, muted, "Kerosene 0.1", 12.0f, 400, 0);
}

static void draw_window_controls(KApp *app) {
    KColor text = theme_color(app, app->theme.text);
    KColor close_bg = theme_pair(app, 0x00FFFFFF, 0x00101114);
    KRect min_r = window_minimize_rect(app);
    KRect max_r = window_maximize_rect(app);
    KRect close_r = window_close_rect(app);
    renderer_fill_rect(app->renderer, min_r, theme_pair(app, 0x00FFFFFF, 0x00101114), 0);
    renderer_fill_rect(app->renderer, max_r, theme_pair(app, 0x00FFFFFF, 0x00101114), 0);
    renderer_fill_rect(app->renderer, close_r, close_bg, 0);
    draw_icon(app, ICON_MINIMIZE, min_r.x + 11.0f, min_r.y + 7.0f, 24.0f, 24.0f, text);
    draw_icon(app, ICON_MAXIMIZE, max_r.x + 11.0f, max_r.y + 7.0f, 24.0f, 24.0f, text);
    draw_icon(app, ICON_CLOSE, close_r.x + 11.0f, close_r.y + 7.0f, 24.0f, 24.0f, text);
}

static void draw_search_icon(KApp *app, float x, float y, KColor color) {
    renderer_draw_line(app->renderer, x + 6.0f, y + 8.0f, x + 11.0f, y + 5.0f, color, 1.5f);
    renderer_draw_line(app->renderer, x + 11.0f, y + 5.0f, x + 16.0f, y + 8.0f, color, 1.5f);
    renderer_draw_line(app->renderer, x + 16.0f, y + 8.0f, x + 14.0f, y + 14.0f, color, 1.5f);
    renderer_draw_line(app->renderer, x + 14.0f, y + 14.0f, x + 8.0f, y + 14.0f, color, 1.5f);
    renderer_draw_line(app->renderer, x + 8.0f, y + 14.0f, x + 6.0f, y + 8.0f, color, 1.5f);
    renderer_draw_line(app->renderer, x + 14.0f, y + 14.0f, x + 19.0f, y + 20.0f, color, 1.7f);
}

static void draw_centered_text(KApp *app, KRect rect, KColor color, const char *text, float font_size, int weight) {
    float approx = (float)strlen(text) * font_size * 0.54f;
    KRect r = rect;
    if (approx < rect.w) {
        r.x += (rect.w - approx) * 0.5f;
        r.w = approx + 4.0f;
    }
    renderer_draw_text(app->renderer, r, color, text, font_size, weight, 0);
}

static void domain_label(const char *url, const char *fallback, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = 0;
    if (url && !_stricmp(url, "kerosene:home")) {
        strncpy(out, "K", cap - 1);
    } else if (url && !_strnicmp(url, "kerosene:search", 15)) {
        strncpy(out, "S", cap - 1);
    } else if (url && strstr(url, "://")) {
        const char *p = strstr(url, "://") + 3;
        if (!_strnicmp(p, "www.", 4)) p += 4;
        size_t n = 0;
        while (*p && *p != '/' && *p != ':' && *p != '?' && *p != '#' && n + 1 < cap && n < 2) {
            if (isalnum((unsigned char)*p)) {
                out[n++] = (char)toupper((unsigned char)*p);
            }
            p++;
        }
        out[n] = 0;
    }
    if (!out[0] && fallback && fallback[0]) {
        size_t n = 0;
        for (const char *p = fallback; *p && n + 1 < cap && n < 2; p++) {
            if (isalnum((unsigned char)*p)) {
                out[n++] = (char)toupper((unsigned char)*p);
            }
        }
        out[n] = 0;
    }
    if (!out[0]) strncpy(out, "?", cap - 1);
    out[cap - 1] = 0;
}

static uint32_t site_color(const char *url, const char *fallback, uint32_t preferred) {
    if (preferred) return preferred;
    const char *text = url && url[0] ? url : (fallback ? fallback : "kerosene");
    uint32_t hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        hash ^= (uint32_t)tolower((unsigned char)*p);
        hash *= 16777619u;
    }
    uint8_t r = (uint8_t)(80 + (hash & 0x7F));
    uint8_t g = (uint8_t)(80 + ((hash >> 8) & 0x7F));
    uint8_t b = (uint8_t)(80 + ((hash >> 16) & 0x7F));
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void draw_site_badge(KApp *app, KRect r, const char *url, const char *fallback, uint32_t preferred_color) {
    char letter[4];
    domain_label(url, fallback, letter, sizeof(letter));
    if (r.w < 32.0f || r.h < 32.0f) {
        letter[1] = 0;
    }
    KColor bg = kcolor_from_argb(site_color(url, fallback, preferred_color));
    float radius = fminf(r.w, r.h) * 0.30f;
    renderer_fill_rect(app->renderer, (KRect){r.x, r.y + 1.5f, r.w, r.h}, kcolor_rgba(0, 0, 0, 0.18f), radius);
    renderer_fill_rect(app->renderer, r, bg, radius);
    renderer_fill_rect(app->renderer, (KRect){r.x + 2.0f, r.y + 2.0f, fmaxf(1.0f, r.w - 4.0f), fmaxf(2.0f, r.h * 0.22f)},
        kcolor_rgba(1, 1, 1, 0.16f), radius * 0.7f);
    renderer_draw_border(app->renderer, r, kcolor_rgba(1, 1, 1, 0.28f), 1.0f, radius);
    renderer_draw_border(app->renderer, (KRect){r.x + 0.5f, r.y + 0.5f, r.w - 1.0f, r.h - 1.0f},
        kcolor_rgba(0, 0, 0, 0.16f), 1.0f, fmaxf(0.0f, radius - 0.5f));

    float font_size = fminf(r.w, r.h) * (letter[1] ? 0.42f : 0.54f);
    if (font_size < 10.0f) font_size = 10.0f;
    if (font_size > 24.0f) font_size = 24.0f;
    KRect text_rect = { r.x, r.y + fmaxf(0.0f, (r.h - font_size * 1.35f) * 0.5f), r.w, font_size * 1.35f };
    draw_centered_text(app, text_rect, kcolor_rgba(1, 1, 1, 1), letter, font_size, 800);
}

static KRect home_search_rect(KApp *app) {
    float top = chrome_h(app);
    float w = fminf(620.0f, (float)app->width - 80.0f);
    if (w < 280.0f) w = (float)app->width - 32.0f;
    float x = ((float)app->width - w) * 0.5f;
    float y = top + fmaxf(72.0f, ((float)app->height - top) * 0.18f + 78.0f);
    return (KRect){ x, y, w, 48.0f };
}

static KRect home_shortcut_rect(KApp *app, int idx) {
    KRect search = home_search_rect(app);
    int cols = app->width < 720 ? 3 : 6;
    float tile_w = app->width < 720 ? 86.0f : 92.0f;
    float gap = app->width < 720 ? 10.0f : 14.0f;
    float total_w = (float)cols * tile_w + (float)(cols - 1) * gap;
    float x0 = ((float)app->width - total_w) * 0.5f;
    int row = idx / cols;
    int col = idx % cols;
    return (KRect){ x0 + (tile_w + gap) * (float)col, search.y + 86.0f + (float)row * 96.0f, tile_w, 82.0f };
}

static KRect search_box_rect(KApp *app) {
    float top = chrome_h(app);
    float w = fminf(820.0f, (float)app->width - 64.0f);
    if (w < 300.0f) w = (float)app->width - 32.0f;
    return (KRect){ ((float)app->width - w) * 0.5f, top + 28.0f - current_scroll(app), w, 44.0f };
}

static KRect search_result_rect(KApp *app, int idx) {
    float w = fminf(860.0f, (float)app->width - 64.0f);
    if (w < 300.0f) w = (float)app->width - 32.0f;
    float x = ((float)app->width - w) * 0.5f;
    return (KRect){ x, 102.0f + (float)idx * 148.0f, w, 132.0f };
}

static int search_pager_start(int current) {
    return current <= 3 ? 1 : current - 2;
}

static KRect search_pager_button_rect(KApp *app, const KPage *page, int idx) {
    float button_w = idx == 0 || idx == 6 ? 82.0f : 42.0f;
    float gap = 8.0f;
    float total = 82.0f + gap + 5.0f * 42.0f + 4.0f * gap + gap + 82.0f;
    float x = ((float)app->width - total) * 0.5f;
    if (x < 18.0f) x = 18.0f;
    for (int i = 0; i < idx; i++) {
        x += (i == 0 || i == 6 ? 82.0f : 42.0f) + gap;
    }
    int count = page && page->search_count > 0 ? page->search_count : 1;
    float y = 112.0f + (float)count * 148.0f + 18.0f;
    return (KRect){ x, y, button_w, 34.0f };
}

static int search_pager_target_page(const KPage *page, int idx) {
    int current = page && page->search_page > 0 ? page->search_page : 1;
    if (idx == 0) return current > 1 ? current - 1 : 0;
    if (idx == 6) return page && page->search_count > 0 ? current + 1 : 0;
    return search_pager_start(current) + idx - 1;
}

static void draw_search_pager(KApp *app, KPage *page, float top) {
    if (!page) return;
    KColor text = theme_color(app, app->theme.text);
    KColor muted = theme_color(app, app->theme.muted);
    KColor accent = theme_color(app, app->theme.accent);
    int current = page->search_page > 0 ? page->search_page : 1;
    int start = search_pager_start(current);
    for (int i = 0; i < 7; i++) {
        int target = search_pager_target_page(page, i);
        int enabled = target > 0 && target != current;
        KRect r = search_pager_button_rect(app, page, i);
        r.y += top - current_scroll(app);
        if (r.y + r.h < top || r.y > (float)app->height) continue;
        int is_current = (i > 0 && i < 6 && target == current);
        renderer_fill_rect(app->renderer, r, is_current ? theme_pair(app, 0xFFE8F0FE, 0xFF172A46) : theme_color(app, app->theme.surface), 8.0f);
        renderer_draw_border(app->renderer, r, is_current ? accent : theme_pair(app, 0xFFE3E7EE, 0xFF25272E), 1.0f, 8.0f);
        char label[16];
        if (i == 0) snprintf(label, sizeof(label), "Anterior");
        else if (i == 6) snprintf(label, sizeof(label), "Proxima");
        else snprintf(label, sizeof(label), "%d", start + i - 1);
        draw_centered_text(app, r, enabled || is_current ? text : muted, label, 12.0f, is_current ? 700 : 500);
    }
}

static void draw_native_home(KApp *app) {
    float top = chrome_h(app);
    float h = (float)app->height - top;
    KColor text = theme_color(app, app->theme.text);
    KColor muted = theme_color(app, app->theme.muted);
    renderer_fill_rect(app->renderer, (KRect){0, top, (float)app->width, h}, theme_color(app, app->theme.page_bg), 0);

    KRect logo = { 0.0f, top + fmaxf(58.0f, h * 0.16f), (float)app->width, 64.0f };
    draw_centered_text(app, logo, text, "Kerosene", 46.0f, 600);

    KRect search = home_search_rect(app);
    renderer_fill_rect(app->renderer, (KRect){search.x + 2.0f, search.y + 4.0f, search.w, search.h}, theme_pair(app, 0x16000000, 0xAA000000), 24.0f);
    renderer_fill_rect(app->renderer, search, theme_color(app, app->theme.surface), 24.0f);
    renderer_draw_border(app->renderer, search, theme_pair(app, 0xFFDADCE0, 0xFF2B2D33), 1.0f, 24.0f);
    draw_search_icon(app, search.x + 18.0f, search.y + 13.0f, muted);
    if (app->home_search_focus) {
        renderer_draw_border(app->renderer, search, theme_color(app, app->theme.accent), 1.6f, 24.0f);
    }
    const char *home_query = current_home_query(app);
    if (!home_query) home_query = "";
    const char *home_text = home_query[0] ? home_query : "Pesquise no Wiby ou digite uma URL";
    int *home_cursor_ptr = current_home_cursor(app);
    int *home_select_ptr = current_home_select_all(app);
    int home_cursor = home_cursor_ptr ? clamp_cursor(home_query, *home_cursor_ptr) : text_len_i(home_query);
    if (app->home_search_focus && home_select_ptr && *home_select_ptr && home_query[0]) {
        renderer_fill_rect(app->renderer, (KRect){search.x + 52.0f, search.y + 10.0f, fminf(search.w - 68.0f, (float)strlen(home_query) * 8.1f + 8.0f), 28.0f},
            kcolor_rgba(0.22f, 0.48f, 0.88f, 0.22f), 5.0f);
    }
    renderer_draw_text(app->renderer, (KRect){search.x + 54.0f, search.y + 14.0f, search.w - 74.0f, 22.0f},
        home_query[0] ? text : muted, home_text, 15.0f, 400, 0);
    if (app->home_search_focus && (!home_select_ptr || !*home_select_ptr)) {
        float cx = search.x + 56.0f + fminf((float)home_cursor * 8.0f, search.w - 74.0f);
        renderer_fill_rect(app->renderer, (KRect){cx, search.y + 14.0f, 1.0f, 21.0f}, theme_color(app, app->theme.accent), 0);
    }

    size_t count = sizeof(HOME_SHORTCUTS) / sizeof(HOME_SHORTCUTS[0]);
    for (size_t i = 0; i < count; i++) {
        KRect tile = home_shortcut_rect(app, (int)i);
        renderer_fill_rect(app->renderer, tile, theme_color(app, app->theme.surface), 8.0f);
        renderer_draw_border(app->renderer, tile, theme_pair(app, 0xFFE8EAED, 0xFF26282E), 1.0f, 8.0f);
        KRect badge = { tile.x + (tile.w - 42.0f) * 0.5f, tile.y + 10.0f, 42.0f, 42.0f };
        draw_site_badge(app, badge, HOME_SHORTCUTS[i].url, HOME_SHORTCUTS[i].label, HOME_SHORTCUTS[i].color);
        draw_centered_text(app, (KRect){tile.x + 4.0f, tile.y + 58.0f, tile.w - 8.0f, 18.0f}, text, HOME_SHORTCUTS[i].label, 12.0f, 400);
    }
}

static void draw_native_search(KApp *app, KPage *page) {
    float top = chrome_h(app);
    float viewport_h = (float)app->height - top;
    KColor text = theme_color(app, app->theme.text);
    KColor muted = theme_color(app, app->theme.muted);
    KColor accent = theme_color(app, app->theme.accent);
    float scroll = current_scroll(app);
    renderer_fill_rect(app->renderer, (KRect){0, top, (float)app->width, viewport_h}, theme_color(app, app->theme.page_bg), 0);

    KRect search = search_box_rect(app);
    if (search.y + search.h >= top && search.y <= (float)app->height) {
        renderer_fill_rect(app->renderer, search, theme_pair(app, 0xFFF5F7FA, 0xFF101114), 22.0f);
        renderer_draw_border(app->renderer, search, theme_pair(app, 0xFFDADCE0, 0xFF2B2D33), 1.0f, 22.0f);
        draw_search_icon(app, search.x + 18.0f, search.y + 11.0f, muted);
        renderer_draw_text(app->renderer, (KRect){search.x + 54.0f, search.y + 12.0f, search.w - 68.0f, 22.0f},
            text, page->search_query, 15.0f, 400, 0);
    }

    char meta[180];
    if (page->search_count > 0 && page->search_total_label[0]) {
        snprintf(meta, sizeof(meta), "Mostrando 1-%d de %s - %s",
            page->search_count, page->search_total_label, page->search_provider[0] ? page->search_provider : "busca");
    } else if (page->search_count > 0) {
        snprintf(meta, sizeof(meta), "Mostrando 1-%d - %s",
            page->search_count, page->search_provider[0] ? page->search_provider : "busca");
    } else {
        snprintf(meta, sizeof(meta), "Nenhum resultado - %s", page->search_provider[0] ? page->search_provider : "busca");
    }
    float meta_y = top + 82.0f - scroll;
    if (meta_y >= top - 24.0f && meta_y <= (float)app->height) {
        renderer_draw_text(app->renderer, (KRect){search.x + 4.0f, meta_y, search.w - 8.0f, 20.0f}, muted, meta, 12.0f, 400, 0);
    }

    if (page->search_count <= 0) {
        float y = top + 134.0f - scroll;
        renderer_draw_text(app->renderer, (KRect){search.x, y, search.w, 30.0f}, text, "Nenhum resultado encontrado", 22.0f, 600, 0);
        renderer_draw_text(app->renderer, (KRect){search.x, y + 42.0f, search.w, 40.0f}, muted, page->status, 14.0f, 400, 0);
        draw_search_pager(app, page, top);
        return;
    }

    for (int i = 0; i < page->search_count; i++) {
        KRect card = search_result_rect(app, i);
        KRect draw = card;
        draw.y = top + card.y - scroll;
        if (draw.y + draw.h < top || draw.y > (float)app->height) continue;
        KSearchResult *r = &page->search_results[i];
        renderer_fill_rect(app->renderer, (KRect){draw.x + 1.0f, draw.y + 2.0f, draw.w, draw.h}, theme_pair(app, 0x12000000, 0x99000000), 8.0f);
        renderer_fill_rect(app->renderer, draw, theme_color(app, app->theme.surface), 8.0f);
        renderer_draw_border(app->renderer, draw, theme_pair(app, 0xFFE3E7EE, 0xFF25272E), 1.0f, 8.0f);

        KRect thumb = { draw.x + 16.0f, draw.y + 18.0f, 62.0f, 62.0f };
        draw_site_badge(app, thumb, r->url, r->domain, r->accent);

        float tx = draw.x + 96.0f;
        float tw = draw.w - 114.0f;
        renderer_draw_text(app->renderer, (KRect){tx, draw.y + 16.0f, tw, 24.0f}, accent, r->title, 18.0f, 600, 0);
        renderer_draw_text(app->renderer, (KRect){tx, draw.y + 42.0f, tw, 18.0f}, theme_pair(app, 0xFF188038, 0xFF81C995), r->domain, 12.0f, 500, 0);
        renderer_draw_text(app->renderer, (KRect){tx, draw.y + 62.0f, tw, 44.0f}, text, r->snippet[0] ? r->snippet : r->url, 13.5f, 400, 0);
        renderer_draw_text(app->renderer, (KRect){tx, draw.y + 110.0f, tw, 16.0f}, muted, r->url, 10.5f, 400, 0);
    }
    draw_search_pager(app, page, top);
}

static void draw_ui(KApp *app) {
    KColor bg = theme_color(app, app->theme.bg);
    KColor surface = theme_color(app, app->theme.surface);
    KColor hot = theme_color(app, app->theme.surface_hot);
    KColor text = theme_color(app, app->theme.text);
    KColor muted = theme_color(app, app->theme.muted);
    float tab_h = (float)app->theme.tab_h;
    float toolbar_h = (float)app->theme.toolbar_h;

    renderer_fill_rect(app->renderer, (KRect){0, 0, (float)app->width, tab_h + toolbar_h}, bg, 0);
    renderer_fill_rect(app->renderer, (KRect){0, tab_h - 1.0f, (float)app->width, 1.0f}, theme_pair(app, 0xFFD8DCE2, 0xFF22242A), 0);

    float tab_w = 190.0f;
    for (int i = 0; i < app->tab_count; i++) {
        float x = 8.0f + (float)i * (tab_w + 3.0f);
        KColor c = i == app->active ? surface : hot;
        renderer_fill_rect(app->renderer, (KRect){x, 6.0f, tab_w, tab_h - 5.0f}, c, 9.0f);
        const char *title = app->tabs[i].page.title[0] ? app->tabs[i].page.title : "Nova aba";
        draw_site_badge(app, (KRect){x + 12.0f, 9.0f, 20.0f, 20.0f}, app->tabs[i].page.url, title, 0);
        renderer_draw_text(app->renderer, (KRect){x + 39.0f, 10.0f, tab_w - 71.0f, 20.0f}, text, title, 12.0f, i == app->active ? 600 : 400, 0);
        draw_icon(app, ICON_CLOSE, x + tab_w - 30.0f, 6.0f, 24.0f, 24.0f, muted);
    }
    float plus_x = 8.0f + (float)app->tab_count * (tab_w + 3.0f);
    draw_icon_button(app, plus_x + 3.0f, 5.0f, 30.0f, 27.0f, ICON_PLUS, 1);

    float y = tab_h + 10.0f;
    KTab *tab = app->active >= 0 ? &app->tabs[app->active] : NULL;
    KPage *active_page = current_page(app);
    int can_back = (active_page && active_page->compat_mode && tab) ? webview_host_can_go_back(tab->webview_id) : (tab && tab->history_index > 0);
    int can_forward = (active_page && active_page->compat_mode && tab) ? webview_host_can_go_forward(tab->webview_id) : (tab && tab->history_index + 1 < tab->history_count);
    draw_icon_button(app, 10.0f, y, 34.0f, 31.0f, ICON_BACK, can_back);
    draw_icon_button(app, 50.0f, y, 34.0f, 31.0f, ICON_FORWARD, can_forward);
    draw_icon_button(app, 90.0f, y, 34.0f, 31.0f, ICON_RELOAD, 1);
    draw_icon_button(app, 130.0f, y, 34.0f, 31.0f, ICON_HOME, 1);

    float box_x = 174.0f;
    float box_w = (float)app->width - K_WINDOW_BUTTONS_W - box_x - 111.0f;
    if (box_w < 220.0f) box_w = 220.0f;
    renderer_fill_rect(app->renderer, (KRect){box_x, y, box_w, 34.0f}, app->omnibox_focus ? surface : theme_pair(app, 0xFFF0F2F5, 0xFF15161A), 17.0f);
    renderer_draw_border(app->renderer, (KRect){box_x, y, box_w, 34.0f}, app->omnibox_focus ? theme_color(app, app->theme.accent) : theme_pair(app, 0xFFDADCE0, 0xFF2A2D34), 1.0f, 17.0f);
    KPage *bar_page = current_page(app);
    int home_bar = bar_page && is_home_page(bar_page) && !app->omnibox_focus;
    const char *bar_text = home_bar ? "" : app->omnibox;
    int bar_cursor = clamp_cursor(bar_text, app->omnibox_cursor);
    if (!_strnicmp(bar_text, "https://", 8)) {
        draw_lock_icon(app, box_x + 10.0f, y + 5.0f, muted);
    } else {
        draw_search_icon(app, box_x + 9.0f, y + 6.0f, muted);
    }
    if (app->omnibox_focus && app->omnibox_select_all && bar_text[0]) {
        renderer_fill_rect(app->renderer, (KRect){box_x + 33.0f, y + 6.0f, fminf(box_w - 44.0f, (float)strlen(bar_text) * 7.1f + 6.0f), 22.0f},
            kcolor_rgba(0.22f, 0.48f, 0.88f, 0.22f), 3.0f);
    }
    if (bar_text[0]) {
        renderer_draw_text(app->renderer, (KRect){box_x + 36.0f, y + 8.0f, box_w - 48.0f, 20.0f}, text, bar_text, 13.0f, 400, 0);
    } else {
        renderer_draw_text(app->renderer, (KRect){box_x + 36.0f, y + 8.0f, box_w - 48.0f, 20.0f}, muted, "Pesquise no Wiby ou digite uma URL", 13.0f, 400, 0);
    }
    if (app->omnibox_focus && !app->omnibox_select_all) {
        float cursor_x = box_x + 37.0f + fminf((float)bar_cursor * 7.0f, box_w - 54.0f);
        renderer_fill_rect(app->renderer, (KRect){cursor_x, y + 8.0f, 1.0f, 19.0f}, theme_color(app, app->theme.accent), 0);
    }
    draw_theme_toggle(app);
    KRect menu = menu_button_rect(app);
    draw_icon_button(app, menu.x, menu.y, menu.w, menu.h, ICON_MENU, 1);
    draw_window_controls(app);
    draw_settings_panel(app);
}

static void draw_page(KApp *app) {
    KPage *page = current_page(app);
    if (!page) return;
    float top = chrome_h(app);
    float viewport_h = (float)app->height - top;
    if (page->compat_mode) {
        renderer_fill_rect(app->renderer, (KRect){0, top, (float)app->width, viewport_h}, kcolor_rgba(1, 1, 1, 1), 0);
        return;
    }
    if (is_home_page(page) && !page->reader_mode) {
        draw_native_home(app);
        return;
    }
    if (is_search_page(page) && !page->reader_mode) {
        draw_native_search(app, page);
        return;
    }
    renderer_fill_rect(app->renderer, (KRect){0, top, (float)app->width, viewport_h}, theme_color(app, app->theme.page_bg), 0);
    float scroll = current_scroll(app);
    float yoff = top - scroll;
    for (size_t i = 0; i < page->render.count; i++) {
        KRenderCommand *cmd = &page->render.items[i];
        KRect r = compositor_offset_rect(cmd->rect, 0.0f, yoff);
        if (r.y + r.h < top || r.y > (float)app->height) {
            continue;
        }
        if (cmd->type == K_RENDER_RECT) {
            renderer_fill_rect(app->renderer, r, cmd->color, cmd->radius);
        } else if (cmd->type == K_RENDER_BORDER) {
            renderer_draw_border(app->renderer, r, cmd->color, cmd->font_size, cmd->radius);
        } else if (cmd->type == K_RENDER_TEXT) {
            renderer_draw_text(app->renderer, r, cmd->color, cmd->text, cmd->font_size, cmd->font_weight, cmd->underline);
        }
    }

    if (page->render.content_height > viewport_h + 1.0f) {
        float track_h = viewport_h - 12.0f;
        float thumb_h = fmaxf(34.0f, track_h * viewport_h / page->render.content_height);
        float max_scroll = page->render.content_height - viewport_h;
        float thumb_y = top + 6.0f + (track_h - thumb_h) * (max_scroll > 0.0f ? scroll / max_scroll : 0.0f);
        renderer_fill_rect(app->renderer, (KRect){(float)app->width - 8.0f, thumb_y, 4.0f, thumb_h}, kcolor_rgba(0.50f, 0.55f, 0.62f, 0.65f), 2.0f);
    }

    if (page->status[0]) {
        renderer_fill_rect(app->renderer, (KRect){8.0f, (float)app->height - 26.0f, fminf(620.0f, (float)app->width - 16.0f), 20.0f}, kcolor_rgba(1, 1, 1, 0.92f), 4.0f);
        renderer_draw_text(app->renderer, (KRect){14.0f, (float)app->height - 23.0f, fminf(608.0f, (float)app->width - 28.0f), 16.0f}, theme_color(app, app->theme.muted), page->status, 11.0f, 400, 0);
    }
}

static void paint(KApp *app) {
    if (!app->renderer) return;
    renderer_begin(app->renderer, theme_color(app, app->theme.page_bg));
    draw_page(app);
    draw_ui(app);
    renderer_end(app->renderer);
}

static void toggle_reader(KApp *app);

static void handle_click(KApp *app, int x, int y) {
    float tab_h = (float)app->theme.tab_h;
    float toolbar_top = tab_h;
    float tab_w = 190.0f;
    KRect menu_btn = menu_button_rect(app);
    if (point_in(window_minimize_rect(app), (float)x, (float)y)) {
        ShowWindow(app->hwnd, SW_MINIMIZE);
        return;
    }
    if (point_in(window_maximize_rect(app), (float)x, (float)y)) {
        ShowWindow(app->hwnd, IsZoomed(app->hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        return;
    }
    if (point_in(window_close_rect(app), (float)x, (float)y)) {
        DestroyWindow(app->hwnd);
        return;
    }
    if (app->menu_open) {
        if (point_in(menu_btn, (float)x, (float)y)) {
            close_menu(app);
            InvalidateRect(app->hwnd, NULL, FALSE);
            return;
        }
        if (point_in(settings_theme_row(app), (float)x, (float)y)) {
            theme_start_toggle(app);
            InvalidateRect(app->hwnd, NULL, FALSE);
            return;
        }
        if (point_in(settings_engine_row(app), (float)x, (float)y)) {
            app_toggle_site_engine(app);
            return;
        }
        if (point_in(settings_new_tab_row(app), (float)x, (float)y)) {
            close_menu(app);
            app_new_tab(app, "kerosene:home");
            return;
        }
        if (point_in(settings_reload_row(app), (float)x, (float)y)) {
            close_menu(app);
            app_reload_active_tab(app);
            return;
        }
        if (point_in(settings_reader_row(app), (float)x, (float)y)) {
            KPage *page = current_page(app);
            if (page && page->document && !page->compat_mode && !is_home_page(page) && !is_search_page(page)) {
                close_menu(app);
                toggle_reader(app);
            }
            return;
        }
        if (point_in(settings_external_row(app), (float)x, (float)y)) {
            KPage *page = current_page(app);
            if (page && page->url[0] && strstr(page->url, "://")) {
                char url[K_MAX_URL];
                strncpy(url, page->url, sizeof(url) - 1);
                url[sizeof(url) - 1] = 0;
                close_menu(app);
                platform_open_external(url);
            }
            return;
        }
        if (point_in(settings_panel_rect(app), (float)x, (float)y)) {
            return;
        }
        close_menu(app);
        InvalidateRect(app->hwnd, NULL, FALSE);
        return;
    }
    if ((float)y < tab_h) {
        for (int i = 0; i < app->tab_count; i++) {
            float tx = 8.0f + (float)i * (tab_w + 3.0f);
            if (x >= tx && x <= tx + tab_w) {
                if (x >= tx + tab_w - 32.0f) {
                    app_close_tab(app, i);
                } else {
                    app->active = i;
                    sync_omnibox_from_page(app);
                    sync_webview_visibility(app);
                    layout_active(app);
                    InvalidateRect(app->hwnd, NULL, FALSE);
                }
                return;
            }
        }
        float plus_x = 8.0f + (float)app->tab_count * (tab_w + 3.0f);
        if (x >= plus_x && x <= plus_x + 42.0f) {
            app_new_tab(app, "kerosene:home");
        } else if (!point_in_window_buttons(app, (float)x, (float)y)) {
            ReleaseCapture();
            SendMessageW(app->hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return;
    }

    float by = toolbar_top + 10.0f;
    if (y >= by && y <= by + 34.0f) {
        if (x >= 10 && x <= 44) {
            KPage *page = current_page(app);
            int webview_id = current_webview_id(app);
            if (page && page->compat_mode && webview_host_can_go_back(webview_id)) webview_host_go_back(webview_id);
            else app_go_history(app, -1);
            return;
        }
        if (x >= 50 && x <= 84) {
            KPage *page = current_page(app);
            int webview_id = current_webview_id(app);
            if (page && page->compat_mode && webview_host_can_go_forward(webview_id)) webview_host_go_forward(webview_id);
            else app_go_history(app, 1);
            return;
        }
        if (x >= 90 && x <= 124) {
            app_reload_active_tab(app);
            return;
        }
        if (x >= 130 && x <= 164) {
            app_load_url(app, "kerosene:home");
            return;
        }
        if (point_in(theme_toggle_rect(app), (float)x, (float)y)) {
            theme_start_toggle(app);
            return;
        }
        if (point_in(menu_button_rect(app), (float)x, (float)y)) {
            clear_text_focus(app);
            app->menu_open = 1;
            sync_webview_visibility(app);
            InvalidateRect(app->hwnd, NULL, FALSE);
            return;
        }
        float box_x = 174.0f;
        float box_w = (float)app->width - box_x - 111.0f;
        if (box_w < 220.0f) box_w = 220.0f;
        app->omnibox_focus = x >= box_x && x <= box_x + box_w;
        app->omnibox_select_all = 0;
        if (app->omnibox_focus) {
            app->omnibox_cursor = cursor_from_x(app->omnibox, box_x + 37.0f, 7.0f, (float)x);
            app->home_search_focus = 0;
        }
        InvalidateRect(app->hwnd, NULL, FALSE);
        if (app->omnibox_focus) return;
    } else {
        app->omnibox_focus = 0;
        app->omnibox_select_all = 0;
        int *home_select = current_home_select_all(app);
        if (home_select) *home_select = 0;
    }

    KPage *page = current_page(app);
    if (!page) return;
    float top = chrome_h(app);
    if ((float)y < top) return;
    if (is_home_page(page) && !page->reader_mode) {
        KRect search = home_search_rect(app);
        if (point_in(search, (float)x, (float)y)) {
            app->home_search_focus = 1;
            char *home_query = current_home_query(app);
            int *home_cursor = current_home_cursor(app);
            int *home_select = current_home_select_all(app);
            if (home_cursor) *home_cursor = cursor_from_x(home_query ? home_query : "", search.x + 56.0f, 8.0f, (float)x);
            if (home_select) *home_select = 0;
            app->omnibox_focus = 0;
            app->omnibox_select_all = 0;
            InvalidateRect(app->hwnd, NULL, FALSE);
            return;
        }
        size_t count = sizeof(HOME_SHORTCUTS) / sizeof(HOME_SHORTCUTS[0]);
        for (size_t i = 0; i < count; i++) {
            if (point_in(home_shortcut_rect(app, (int)i), (float)x, (float)y)) {
                app_load_url(app, HOME_SHORTCUTS[i].url);
                return;
            }
        }
        InvalidateRect(app->hwnd, NULL, FALSE);
        return;
    }
    if (is_search_page(page) && !page->reader_mode) {
        float content_y = (float)y - top + current_scroll(app);
        KRect search_content = search_box_rect(app);
        search_content.y = 28.0f;
        if (point_in(search_content, (float)x, content_y)) {
            set_omnibox_text(app, page->search_query);
            app->omnibox_focus = 1;
            app->omnibox_select_all = 1;
            app->omnibox_cursor = text_len_i(app->omnibox);
            InvalidateRect(app->hwnd, NULL, FALSE);
            return;
        }
        for (int i = 0; i < page->search_count; i++) {
            if (point_in(search_result_rect(app, i), (float)x, content_y)) {
                app_load_url(app, page->search_results[i].url);
                return;
            }
        }
        for (int i = 0; i < 7; i++) {
            if (point_in(search_pager_button_rect(app, page, i), (float)x, content_y)) {
                int target = search_pager_target_page(page, i);
                if (target > 0 && target != page->search_page) {
                    char next_url[K_MAX_URL];
                    make_search_url(page->search_query, target, next_url, sizeof(next_url));
                    app_load_url(app, next_url);
                }
                return;
            }
        }
        InvalidateRect(app->hwnd, NULL, FALSE);
        return;
    }
    float content_y = (float)y - top + current_scroll(app);
    for (size_t i = 0; i < page->render.count; i++) {
        KRenderCommand *cmd = &page->render.items[i];
        if (cmd->href && cmd->href[0] && point_in(cmd->rect, (float)x, content_y)) {
            char resolved[K_MAX_URL];
            resolve_url(page->url, cmd->href, resolved, sizeof(resolved));
            app_load_url(app, resolved);
            return;
        }
    }
    InvalidateRect(app->hwnd, NULL, FALSE);
}

static void toggle_reader(KApp *app) {
    KPage *page = current_page(app);
    if (!page) return;
    page->reader_mode = !page->reader_mode;
    set_status(page, page->reader_mode ? "Modo leitura" : "Modo normal");
    set_current_scroll(app, 0.0f);
    layout_active(app);
    InvalidateRect(app->hwnd, NULL, FALSE);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    KApp *app = (KApp *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (msg == WM_NCCALCSIZE) {
        return 0;
    }
    if (!app) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    switch (msg) {
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
        if (hit != HTCLIENT && hit != HTNOWHERE) return hit;
        if (IsZoomed(hwnd)) return HTCLIENT;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        if (point_in_window_buttons(app, (float)pt.x, (float)pt.y)) return HTCLIENT;
        int edge = 8;
        int left = pt.x < edge;
        int right = pt.x >= app->width - edge;
        int top = pt.y < edge;
        int bottom = pt.y >= app->height - edge;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        return HTCLIENT;
    }
    case WM_CREATE: {
        app->hwnd = hwnd;
        kerosene_ui_theme(&app->theme);
        RECT rc;
        GetClientRect(hwnd, &rc);
        app->width = rc.right - rc.left;
        app->height = rc.bottom - rc.top;
        if (!renderer_init(hwnd, &app->renderer, app->renderer_status, sizeof(app->renderer_status))) {
            MessageBoxA(hwnd, app->renderer_status, "Kerosene", MB_ICONERROR);
            return -1;
        }
        char webview_err[160] = {0};
        webview_host_init(hwnd, 0, app->theme.tab_h + app->theme.toolbar_h, app->width, app->height - app->theme.tab_h - app->theme.toolbar_h, webview_err, sizeof(webview_err));
        adblock_init();
        app->prefer_webview = 1;
        app->active = -1;
        app_new_tab(app, "kerosene:home");
        return 0;
    }
    case WM_SIZE:
        app->width = LOWORD(lp);
        app->height = HIWORD(lp);
        renderer_resize(app->renderer, app->width, app->height);
        sync_webview_visibility(app);
        layout_active(app);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        paint(app);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        if (wp == K_THEME_TIMER_ID) {
            theme_tick(app);
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        handle_click(app, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        set_current_scroll(app, current_scroll(app) - (float)delta * 0.55f);
        clamp_scroll(app);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_CHAR:
        if (app->home_search_focus || app->omnibox_focus) {
            if (wp >= 32 && wp < 127) {
                char insert[2] = { (char)wp, 0 };
                text_insert(app, insert);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        break;
    case WM_KEYDOWN: {
        int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        int alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (ctrl && shift && wp == 'D') {
            theme_start_toggle(app);
            return 0;
        }
        if ((app->home_search_focus || app->omnibox_focus) && ctrl && wp == 'A') {
            text_select_all(app);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if ((app->home_search_focus || app->omnibox_focus) && ctrl && wp == 'C') {
            text_copy(app);
            return 0;
        }
        if ((app->home_search_focus || app->omnibox_focus) && ctrl && wp == 'X') {
            text_cut(app);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if ((app->home_search_focus || app->omnibox_focus) && ctrl && wp == 'V') {
            text_paste(app);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if ((app->home_search_focus || app->omnibox_focus) &&
            (wp == VK_LEFT || wp == VK_RIGHT || wp == VK_HOME || wp == VK_END)) {
            text_move_cursor(app, (int)wp);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if ((app->home_search_focus || app->omnibox_focus) && wp == VK_DELETE) {
            text_delete_forward(app);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (app->home_search_focus) {
            if (wp == VK_RETURN) {
                char *home_query = current_home_query(app);
                if (home_query && home_query[0]) {
                    app_load_url(app, home_query);
                }
                return 0;
            }
            if (wp == VK_BACK) {
                text_backspace(app);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (wp == VK_ESCAPE) {
                app->home_search_focus = 0;
                int *home_select = current_home_select_all(app);
                if (home_select) *home_select = 0;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }
        if (ctrl && wp == 'L') {
            sync_omnibox_from_page(app);
            app->omnibox_focus = 1;
            app->omnibox_cursor = text_len_i(app->omnibox);
            app->omnibox_select_all = app->omnibox[0] != 0;
            app->home_search_focus = 0;
            int *home_select = current_home_select_all(app);
            if (home_select) *home_select = 0;
            close_menu(app);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (ctrl && wp == 'T') {
            app_new_tab(app, "kerosene:home");
            return 0;
        }
        if (ctrl && wp == 'W') {
            app_close_tab(app, app->active);
            return 0;
        }
        if (alt && wp == VK_LEFT) {
            KPage *page = current_page(app);
            int webview_id = current_webview_id(app);
            if (page && page->compat_mode && webview_host_can_go_back(webview_id)) webview_host_go_back(webview_id);
            else app_go_history(app, -1);
            return 0;
        }
        if (alt && wp == VK_RIGHT) {
            KPage *page = current_page(app);
            int webview_id = current_webview_id(app);
            if (page && page->compat_mode && webview_host_can_go_forward(webview_id)) webview_host_go_forward(webview_id);
            else app_go_history(app, 1);
            return 0;
        }
        if (wp == VK_F9) {
            toggle_reader(app);
            return 0;
        }
        if (wp == VK_F5) {
            app_reload_active_tab(app);
            return 0;
        }
        if (app->omnibox_focus) {
            if (wp == VK_BACK) {
                text_backspace(app);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (wp == VK_RETURN) {
                app->omnibox_focus = 0;
                app_load_url(app, app->omnibox);
                return 0;
            }
            if (wp == VK_ESCAPE) {
                sync_omnibox_from_page(app);
                app->omnibox_focus = 0;
                app->omnibox_select_all = 0;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }
        break;
    }
    case WM_DESTROY:
        for (int i = 0; i < app->tab_count; i++) {
            page_clear(&app->tabs[i].page);
        }
        renderer_free(app->renderer);
        app->renderer = NULL;
        webview_host_destroy();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev;
    (void)cmd;
    OleInitialize(NULL);
    const wchar_t *class_name = L"KeroseneWindow";
    if (!win32_register_window_class(instance, class_name, wnd_proc)) {
        MessageBoxA(NULL, "Falha registrando janela", "Kerosene", MB_ICONERROR);
        return 1;
    }

    KApp *app = (KApp *)kzalloc(sizeof(KApp));
    DWORD style = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
    HWND hwnd = CreateWindowExW(
        0,
        class_name,
        L"Kerosene",
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1180,
        760,
        NULL,
        NULL,
        instance,
        app);
    if (!hwnd) {
        kfree(app);
        MessageBoxA(NULL, "Falha criando janela", "Kerosene", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    kfree(app);
    OleUninitialize();
    return (int)msg.wParam;
}
