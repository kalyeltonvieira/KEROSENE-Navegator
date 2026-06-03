#include "kerosene.h"

#include <string.h>

static void set_edges(float edges[4], float value) {
    edges[0] = value;
    edges[1] = value;
    edges[2] = value;
    edges[3] = value;
}

KColor kcolor_rgba(float r, float g, float b, float a) {
    KColor c = { r, g, b, a };
    return c;
}

KColor kcolor_from_argb(uint32_t argb) {
    float a = ((argb >> 24) & 0xFF) / 255.0f;
    float r = ((argb >> 16) & 0xFF) / 255.0f;
    float g = ((argb >> 8) & 0xFF) / 255.0f;
    float b = (argb & 0xFF) / 255.0f;
    return kcolor_rgba(r, g, b, a);
}

KColor kcolor_lerp(KColor a, KColor b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return kcolor_rgba(
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t
    );
}

float cyclone_edge_sum(const float edges[4], int horizontal) {
    return horizontal ? edges[1] + edges[3] : edges[0] + edges[2];
}

KStyle cyclone_default_style_for(const KDomNode *node) {
    KStyle style;
    memset(&style, 0, sizeof(style));
    style.display = K_DISPLAY_INLINE;
    style.color = kcolor_rgba(0.09f, 0.10f, 0.12f, 1.0f);
    style.background = kcolor_rgba(0.0f, 0.0f, 0.0f, 0.0f);
    style.border_color = kcolor_rgba(0.72f, 0.75f, 0.80f, 1.0f);
    style.font_size = 16.0f;
    style.line_height = 22.0f;
    style.font_weight = 400;
    style.width = -1.0f;
    style.height = -1.0f;
    style.grid_columns = 1;
    set_edges(style.margin, 0.0f);
    set_edges(style.padding, 0.0f);

    if (!node || node->type == K_NODE_DOCUMENT) {
        style.display = K_DISPLAY_BLOCK;
        return style;
    }
    if (node->type == K_NODE_TEXT) {
        return style;
    }

    const char *tag = node->tag ? node->tag : "";
    if (!strcmp(tag, "html") || !strcmp(tag, "body") || !strcmp(tag, "main") ||
        !strcmp(tag, "section") || !strcmp(tag, "article") || !strcmp(tag, "header") ||
        !strcmp(tag, "footer") || !strcmp(tag, "nav") || !strcmp(tag, "div") ||
        !strcmp(tag, "p") || !strcmp(tag, "ul") || !strcmp(tag, "ol") ||
        !strcmp(tag, "li") || !strcmp(tag, "form") || !strcmp(tag, "pre") ||
        !strcmp(tag, "blockquote")) {
        style.display = K_DISPLAY_BLOCK;
    }
    if (!strcmp(tag, "body")) {
        style.background = kcolor_rgba(1.0f, 1.0f, 1.0f, 1.0f);
        style.has_background = 1;
        set_edges(style.margin, 0.0f);
        set_edges(style.padding, 18.0f);
    } else if (!strcmp(tag, "p")) {
        style.margin[0] = 8.0f;
        style.margin[2] = 12.0f;
    } else if (!strcmp(tag, "li")) {
        style.margin[0] = 4.0f;
        style.margin[2] = 4.0f;
        style.padding[3] = 18.0f;
    } else if (!strcmp(tag, "a")) {
        style.color = kcolor_rgba(0.05f, 0.32f, 0.76f, 1.0f);
        style.underline = 1;
    } else if (!strcmp(tag, "h1")) {
        style.display = K_DISPLAY_BLOCK;
        style.font_size = 34.0f;
        style.line_height = 42.0f;
        style.font_weight = 700;
        style.margin[0] = 12.0f;
        style.margin[2] = 12.0f;
    } else if (!strcmp(tag, "h2")) {
        style.display = K_DISPLAY_BLOCK;
        style.font_size = 26.0f;
        style.line_height = 34.0f;
        style.font_weight = 700;
        style.margin[0] = 12.0f;
        style.margin[2] = 10.0f;
    } else if (!strcmp(tag, "h3")) {
        style.display = K_DISPLAY_BLOCK;
        style.font_size = 21.0f;
        style.line_height = 28.0f;
        style.font_weight = 700;
        style.margin[0] = 10.0f;
        style.margin[2] = 8.0f;
    } else if (!strcmp(tag, "button") || !strcmp(tag, "input")) {
        style.display = K_DISPLAY_INLINE;
        style.background = kcolor_rgba(0.94f, 0.95f, 0.97f, 1.0f);
        style.has_background = 1;
        style.border_width = 1.0f;
        style.border_radius = 4.0f;
        set_edges(style.padding, 6.0f);
    } else if (!strcmp(tag, "img") || !strcmp(tag, "canvas") || !strcmp(tag, "video")) {
        style.display = K_DISPLAY_BLOCK;
        style.width = 360.0f;
        style.height = 180.0f;
        style.background = kcolor_rgba(0.91f, 0.93f, 0.96f, 1.0f);
        style.has_background = 1;
        style.border_width = 1.0f;
        style.border_radius = 3.0f;
    } else if (!strcmp(tag, "script") || !strcmp(tag, "style") || !strcmp(tag, "meta") ||
               !strcmp(tag, "link") || !strcmp(tag, "title")) {
        style.display = K_DISPLAY_NONE;
    }
    return style;
}
