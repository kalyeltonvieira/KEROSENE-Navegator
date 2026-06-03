#ifndef KEROSENE_H
#define KEROSENE_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef COBJMACROS
#define COBJMACROS
#endif

#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define K_MAX_URL 2048
#define K_MAX_TITLE 256
#define K_MAX_TABS 16
#define K_MAX_SEARCH_RESULTS 12
#define K_TAB_MEMORY_LIMIT (50u * 1024u * 1024u)

typedef struct KColor {
    float r;
    float g;
    float b;
    float a;
} KColor;

typedef struct KRect {
    float x;
    float y;
    float w;
    float h;
} KRect;

typedef enum KNodeType {
    K_NODE_DOCUMENT,
    K_NODE_ELEMENT,
    K_NODE_TEXT
} KNodeType;

typedef enum KDisplay {
    K_DISPLAY_NONE,
    K_DISPLAY_BLOCK,
    K_DISPLAY_INLINE,
    K_DISPLAY_FLEX,
    K_DISPLAY_GRID
} KDisplay;

typedef struct KStyle {
    KDisplay display;
    KColor color;
    KColor background;
    KColor border_color;
    float font_size;
    float line_height;
    float margin[4];
    float padding[4];
    float border_width;
    float border_radius;
    float width;
    float height;
    int font_weight;
    int underline;
    int has_background;
    int grid_columns;
    int flex_row;
} KStyle;

typedef struct KDomNode {
    KNodeType type;
    char *tag;
    char *text;
    char *id;
    char *classes;
    char *href;
    char *src;
    char *style_attr;
    KStyle style;
    KRect box;
    struct KDomNode *parent;
    struct KDomNode *first_child;
    struct KDomNode *last_child;
    struct KDomNode *next;
} KDomNode;

typedef struct KCssRule KCssRule;

typedef enum KRenderCommandType {
    K_RENDER_RECT,
    K_RENDER_TEXT,
    K_RENDER_BORDER
} KRenderCommandType;

typedef struct KRenderCommand {
    KRenderCommandType type;
    KRect rect;
    KColor color;
    float radius;
    char *text;
    char *href;
    float font_size;
    int font_weight;
    int underline;
} KRenderCommand;

typedef struct KRenderList {
    KRenderCommand *items;
    size_t count;
    size_t capacity;
    float content_height;
} KRenderList;

typedef struct KSearchResult {
    char title[K_MAX_TITLE];
    char url[K_MAX_URL];
    char snippet[512];
    char domain[128];
    uint32_t accent;
} KSearchResult;

typedef struct KPage {
    KDomNode *document;
    KRenderList render;
    char url[K_MAX_URL];
    char title[K_MAX_TITLE];
    char status[256];
    char *html;
    char *reader_text;
    char search_query[256];
    char search_provider[64];
    char search_total_label[128];
    KSearchResult search_results[K_MAX_SEARCH_RESULTS];
    int search_count;
    int compat_mode;
    size_t bytes_used;
    int reader_mode;
    int frozen;
} KPage;

typedef struct KHttpResponse {
    int status;
    char *body;
    size_t body_len;
    char final_url[K_MAX_URL];
    char content_type[128];
} KHttpResponse;

typedef struct KeroseneUiTheme {
    int tab_h;
    int toolbar_h;
    uint32_t bg;
    uint32_t surface;
    uint32_t surface_hot;
    uint32_t text;
    uint32_t muted;
    uint32_t accent;
    uint32_t page_bg;
} KeroseneUiTheme;

void kerosene_ui_theme(KeroseneUiTheme *out);
int kerosene_ui_normalize_omnibox(const char *input, char *out, size_t out_cap);

void *kzalloc(size_t bytes);
void *krealloc_owned(void *ptr, size_t bytes);
char *kstrdup(const char *text);
char *kstrndup(const char *text, size_t len);
void kfree(void *ptr);
uint64_t ktime_ms(void);

KColor kcolor_rgba(float r, float g, float b, float a);
KColor kcolor_from_argb(uint32_t argb);
KColor kcolor_lerp(KColor a, KColor b, float t);

KDomNode *cyclone_parse_html(const char *html);
KDomNode *cyclone_parse_fragment(const char *html);
void cyclone_free_dom(KDomNode *node);
KDomNode *cyclone_find_first(KDomNode *node, const char *tag);
char *cyclone_collect_text(KDomNode *node);
void cyclone_extract_title(KDomNode *doc, char *out, size_t cap);
char *cyclone_extract_style_text(KDomNode *doc);
char *cyclone_extract_script_text(KDomNode *doc);

KCssRule *cyclone_parse_css(const char *css);
void cyclone_free_css(KCssRule *rules);
void cyclone_apply_styles(KDomNode *doc, KCssRule *rules);
void cyclone_apply_inline_style(KStyle *style, const char *inline_css);
float cyclone_edge_sum(const float edges[4], int horizontal);
KStyle cyclone_default_style_for(const KDomNode *node);

void cyclone_layout_page(KDomNode *doc, KRenderList *out, float viewport_w, float viewport_h, int reader_mode);
float cyclone_layout_node(KDomNode *node, KRenderList *out, float x, float y, float w, const char *inherited_href);
float cyclone_layout_flex_children(KDomNode *node, KRenderList *out, float x, float y, float w);
float cyclone_layout_grid_children(KDomNode *node, KRenderList *out, float x, float y, float w);

void render_list_init(KRenderList *list);
void render_list_reset(KRenderList *list);
void render_list_free(KRenderList *list);
void render_add_rect(KRenderList *list, KRect rect, KColor color, float radius);
void render_add_border(KRenderList *list, KRect rect, KColor color, float width, float radius);
void render_add_text(KRenderList *list, KRect rect, KColor color, const char *text, const char *href, float font_size, int font_weight, int underline);

void spark_run_document(KDomNode *doc, const char *base_url, char *status, size_t status_cap);
int spark_compile_count_statements(const char *source);
int spark_jit_loop_budget(const char *source);
void spark_gc_note_alloc(size_t bytes);
void spark_gc_note_free(size_t bytes);
size_t spark_gc_live_bytes(void);

void adblock_init(void);
bool adblock_should_block(const char *url);
bool http_get(const char *url, KHttpResponse *out, char *err, size_t err_cap);
void http_response_free(KHttpResponse *response);
int search_fetch_results(const char *query, KSearchResult *results, int max_results, char *provider, size_t provider_cap, char *total_label, size_t total_label_cap, char *err, size_t err_cap);
const char *quic_status_text(void);
bool cache_get(const char *url, KHttpResponse *out);
void cache_put(const char *url, const KHttpResponse *response);

typedef struct KRenderer KRenderer;
bool renderer_init(HWND hwnd, KRenderer **out, char *err, size_t err_cap);
void renderer_resize(KRenderer *renderer, int width, int height);
void renderer_begin(KRenderer *renderer, KColor clear);
void renderer_fill_rect(KRenderer *renderer, KRect rect, KColor color, float radius);
void renderer_draw_border(KRenderer *renderer, KRect rect, KColor color, float width, float radius);
void renderer_draw_line(KRenderer *renderer, float x0, float y0, float x1, float y1, KColor color, float width);
void renderer_draw_text(KRenderer *renderer, KRect rect, KColor color, const char *text, float font_size, int font_weight, int underline);
void renderer_end(KRenderer *renderer);
void renderer_free(KRenderer *renderer);

int compositor_visible(KRect rect, float y_offset, float viewport_h);
KRect compositor_offset_rect(KRect rect, float x_offset, float y_offset);
int kerosene_probe_d3d11(char *out, size_t cap);

typedef struct KTextAtlas KTextAtlas;
KTextAtlas *text_atlas_create(void *dwrite_factory);
void *text_atlas_format(KTextAtlas *atlas, float font_size, int font_weight);
void text_atlas_free(KTextAtlas *atlas);

wchar_t *platform_utf8_to_wide(const char *text, int *out_len);
char *platform_read_file(const wchar_t *path, size_t *out_len);
bool platform_write_file(const wchar_t *path, const void *data, size_t len);
void platform_get_data_dir(wchar_t *out, size_t cap);
void platform_open_external(const char *url);

bool win32_register_window_class(HINSTANCE instance, const wchar_t *class_name, WNDPROC proc);

bool webview_host_init(HWND hwnd, int x, int y, int w, int h, char *err, size_t err_cap);
void webview_host_resize(int x, int y, int w, int h);
void webview_host_show(int show);
void webview_host_navigate(const char *url);
void webview_host_reload(void);
void webview_host_go_back(void);
void webview_host_go_forward(void);
int webview_host_can_go_back(void);
int webview_host_can_go_forward(void);
void webview_host_destroy(void);

#ifdef __cplusplus
}
#endif

#endif
