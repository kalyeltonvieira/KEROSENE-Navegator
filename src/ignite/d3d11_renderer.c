#include "kerosene.h"

#include <initguid.h>
#include <d2d1.h>
#include <dwrite.h>
#include <d3d11.h>
#include <stdio.h>
#include <string.h>

struct KRenderer {
    HWND hwnd;
    ID2D1Factory *d2d_factory;
    ID2D1HwndRenderTarget *target;
    IDWriteFactory *dwrite_factory;
    ID2D1SolidColorBrush *brush;
    KTextAtlas *text_atlas;
    int width;
    int height;
};

static D2D1_COLOR_F d2d_color(KColor c) {
    D2D1_COLOR_F out = { c.r, c.g, c.b, c.a };
    return out;
}

static D2D1_RECT_F d2d_rect(KRect r) {
    D2D1_RECT_F out = { r.x, r.y, r.x + r.w, r.y + r.h };
    return out;
}

static void set_brush(KRenderer *r, KColor color) {
    D2D1_COLOR_F c = d2d_color(color);
    ID2D1SolidColorBrush_SetColor(r->brush, &c);
}

bool renderer_init(HWND hwnd, KRenderer **out, char *err, size_t err_cap) {
    *out = NULL;
    KRenderer *r = (KRenderer *)kzalloc(sizeof(KRenderer));
    r->hwnd = hwnd;
    RECT rc;
    GetClientRect(hwnd, &rc);
    r->width = rc.right - rc.left;
    r->height = rc.bottom - rc.top;

    char d3d_msg[128];
    kerosene_probe_d3d11(d3d_msg, sizeof(d3d_msg));

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **)&r->d2d_factory);
    if (FAILED(hr)) {
        snprintf(err, err_cap, "D2D1CreateFactory falhou: 0x%08lx", (unsigned long)hr);
        renderer_free(r);
        return false;
    }

    D2D1_RENDER_TARGET_PROPERTIES props;
    memset(&props, 0, sizeof(props));
    props.type = D2D1_RENDER_TARGET_TYPE_HARDWARE;
    props.pixelFormat.format = DXGI_FORMAT_UNKNOWN;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_UNKNOWN;
    props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

    D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props;
    memset(&hwnd_props, 0, sizeof(hwnd_props));
    hwnd_props.hwnd = hwnd;
    hwnd_props.pixelSize.width = (UINT32)r->width;
    hwnd_props.pixelSize.height = (UINT32)r->height;
    hwnd_props.presentOptions = D2D1_PRESENT_OPTIONS_NONE;

    hr = ID2D1Factory_CreateHwndRenderTarget(r->d2d_factory, &props, &hwnd_props, &r->target);
    if (FAILED(hr)) {
        snprintf(err, err_cap, "CreateHwndRenderTarget falhou: 0x%08lx", (unsigned long)hr);
        renderer_free(r);
        return false;
    }

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, (IUnknown **)&r->dwrite_factory);
    if (FAILED(hr)) {
        snprintf(err, err_cap, "DWriteCreateFactory falhou: 0x%08lx", (unsigned long)hr);
        renderer_free(r);
        return false;
    }

    D2D1_COLOR_F white = {1, 1, 1, 1};
    hr = ID2D1HwndRenderTarget_CreateSolidColorBrush(r->target, &white, NULL, &r->brush);
    if (FAILED(hr)) {
        snprintf(err, err_cap, "CreateSolidColorBrush falhou: 0x%08lx", (unsigned long)hr);
        renderer_free(r);
        return false;
    }

    r->text_atlas = text_atlas_create(r->dwrite_factory);
    *out = r;
    if (err && err_cap) {
        snprintf(err, err_cap, "%s", d3d_msg);
    }
    return true;
}

void renderer_resize(KRenderer *r, int width, int height) {
    if (!r) return;
    r->width = width;
    r->height = height;
    if (r->target) {
        D2D1_SIZE_U size = { (UINT32)(width > 0 ? width : 1), (UINT32)(height > 0 ? height : 1) };
        ID2D1HwndRenderTarget_Resize(r->target, &size);
    }
}

void renderer_begin(KRenderer *r, KColor clear) {
    if (!r || !r->target) return;
    ID2D1HwndRenderTarget_BeginDraw(r->target);
    D2D1_COLOR_F c = d2d_color(clear);
    ID2D1HwndRenderTarget_Clear(r->target, &c);
}

void renderer_fill_rect(KRenderer *r, KRect rect, KColor color, float radius) {
    if (!r || !r->target || !r->brush || rect.w <= 0.0f || rect.h <= 0.0f) return;
    set_brush(r, color);
    if (radius > 0.5f) {
        D2D1_ROUNDED_RECT rr;
        rr.rect = d2d_rect(rect);
        rr.radiusX = radius;
        rr.radiusY = radius;
        ID2D1HwndRenderTarget_FillRoundedRectangle(r->target, &rr, (ID2D1Brush *)r->brush);
    } else {
        D2D1_RECT_F dr = d2d_rect(rect);
        ID2D1HwndRenderTarget_FillRectangle(r->target, &dr, (ID2D1Brush *)r->brush);
    }
}

void renderer_draw_border(KRenderer *r, KRect rect, KColor color, float width, float radius) {
    if (!r || !r->target || !r->brush || width <= 0.0f) return;
    set_brush(r, color);
    if (radius > 0.5f) {
        D2D1_ROUNDED_RECT rr;
        rr.rect = d2d_rect(rect);
        rr.radiusX = radius;
        rr.radiusY = radius;
        ID2D1HwndRenderTarget_DrawRoundedRectangle(r->target, &rr, (ID2D1Brush *)r->brush, width, NULL);
    } else {
        D2D1_RECT_F dr = d2d_rect(rect);
        ID2D1HwndRenderTarget_DrawRectangle(r->target, &dr, (ID2D1Brush *)r->brush, width, NULL);
    }
}

void renderer_draw_line(KRenderer *r, float x0, float y0, float x1, float y1, KColor color, float width) {
    if (!r || !r->target || !r->brush || width <= 0.0f) return;
    set_brush(r, color);
    D2D1_POINT_2F p0 = { x0, y0 };
    D2D1_POINT_2F p1 = { x1, y1 };
    ID2D1HwndRenderTarget_DrawLine(r->target, p0, p1, (ID2D1Brush *)r->brush, width, NULL);
}

void renderer_draw_text(KRenderer *r, KRect rect, KColor color, const char *text, float font_size, int font_weight, int underline) {
    if (!r || !r->target || !r->brush || !text || !*text || rect.w <= 0.0f || rect.h <= 0.0f) return;
    int wlen = 0;
    wchar_t *wide = platform_utf8_to_wide(text, &wlen);
    if (!wide) return;
    IDWriteTextFormat *format = (IDWriteTextFormat *)text_atlas_format(r->text_atlas, font_size, font_weight);
    if (!format) {
        kfree(wide);
        return;
    }
    set_brush(r, color);
    D2D1_RECT_F dr = d2d_rect(rect);
    ID2D1RenderTarget_DrawText((ID2D1RenderTarget *)r->target, wide, (UINT32)wlen, format, &dr, (ID2D1Brush *)r->brush,
        D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
    if (underline) {
        float y = rect.y + rect.h - 3.0f;
        D2D1_POINT_2F p0 = { rect.x, y };
        D2D1_POINT_2F p1 = { rect.x + rect.w, y };
        ID2D1HwndRenderTarget_DrawLine(r->target, p0, p1, (ID2D1Brush *)r->brush, 1.0f, NULL);
    }
    kfree(wide);
}

void renderer_end(KRenderer *r) {
    if (!r || !r->target) return;
    HRESULT hr = ID2D1HwndRenderTarget_EndDraw(r->target, NULL, NULL);
    if (hr == D2DERR_RECREATE_TARGET) {
        if (r->brush) {
            ID2D1SolidColorBrush_Release(r->brush);
            r->brush = NULL;
        }
        ID2D1HwndRenderTarget_Release(r->target);
        r->target = NULL;
    }
}

void renderer_free(KRenderer *r) {
    if (!r) return;
    text_atlas_free(r->text_atlas);
    if (r->brush) ID2D1SolidColorBrush_Release(r->brush);
    if (r->target) ID2D1HwndRenderTarget_Release(r->target);
    if (r->dwrite_factory) IDWriteFactory_Release(r->dwrite_factory);
    if (r->d2d_factory) ID2D1Factory_Release(r->d2d_factory);
    kfree(r);
}
