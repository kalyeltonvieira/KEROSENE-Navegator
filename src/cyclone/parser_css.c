#include "kerosene.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CSS_DISPLAY = 1 << 0,
    CSS_COLOR = 1 << 1,
    CSS_BACKGROUND = 1 << 2,
    CSS_FONT_SIZE = 1 << 3,
    CSS_LINE_HEIGHT = 1 << 4,
    CSS_MARGIN = 1 << 5,
    CSS_PADDING = 1 << 6,
    CSS_BORDER = 1 << 7,
    CSS_WIDTH = 1 << 8,
    CSS_HEIGHT = 1 << 9,
    CSS_WEIGHT = 1 << 10,
    CSS_UNDERLINE = 1 << 11,
    CSS_GRID = 1 << 12,
    CSS_FLEX = 1 << 13,
    CSS_RADIUS = 1 << 14
};

struct KCssRule {
    char selector[96];
    KStyle style;
    unsigned mask;
    struct KCssRule *next;
};

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
    return s;
}

static float parse_px(const char *s, float fallback) {
    if (!s || !*s) {
        return fallback;
    }
    return (float)strtod(s, NULL);
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static KColor parse_color(const char *s, KColor fallback) {
    s = trim((char *)s);
    if (*s == '#') {
        size_t n = strlen(s + 1);
        int r = 0, g = 0, b = 0;
        if (n == 3) {
            r = hex_value(s[1]) * 17;
            g = hex_value(s[2]) * 17;
            b = hex_value(s[3]) * 17;
        } else if (n >= 6) {
            r = hex_value(s[1]) * 16 + hex_value(s[2]);
            g = hex_value(s[3]) * 16 + hex_value(s[4]);
            b = hex_value(s[5]) * 16 + hex_value(s[6]);
        }
        return kcolor_rgba(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
    }
    if (!_strnicmp(s, "rgb(", 4)) {
        int r = 0, g = 0, b = 0;
        sscanf(s + 4, "%d,%d,%d", &r, &g, &b);
        return kcolor_rgba(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
    }
    if (!_stricmp(s, "transparent")) return kcolor_rgba(0, 0, 0, 0);
    if (!_stricmp(s, "white")) return kcolor_rgba(1, 1, 1, 1);
    if (!_stricmp(s, "black")) return kcolor_rgba(0, 0, 0, 1);
    if (!_stricmp(s, "red")) return kcolor_rgba(0.86f, 0.08f, 0.08f, 1);
    if (!_stricmp(s, "green")) return kcolor_rgba(0.05f, 0.55f, 0.22f, 1);
    if (!_stricmp(s, "blue")) return kcolor_rgba(0.05f, 0.25f, 0.85f, 1);
    if (!_stricmp(s, "gray") || !_stricmp(s, "grey")) return kcolor_rgba(0.45f, 0.48f, 0.53f, 1);
    return fallback;
}

static void parse_edges(float edges[4], const char *value) {
    float v[4] = {0, 0, 0, 0};
    int n = sscanf(value, "%f%*[^0-9.-]%f%*[^0-9.-]%f%*[^0-9.-]%f", &v[0], &v[1], &v[2], &v[3]);
    if (n <= 1) {
        edges[0] = edges[1] = edges[2] = edges[3] = parse_px(value, 0.0f);
    } else if (n == 2) {
        edges[0] = edges[2] = v[0];
        edges[1] = edges[3] = v[1];
    } else if (n == 3) {
        edges[0] = v[0];
        edges[1] = edges[3] = v[1];
        edges[2] = v[2];
    } else {
        edges[0] = v[0];
        edges[1] = v[1];
        edges[2] = v[2];
        edges[3] = v[3];
    }
}

static int parse_grid_columns(const char *value) {
    const char *repeat = strstr(value, "repeat(");
    if (repeat) {
        int n = atoi(repeat + 7);
        return n > 0 && n < 12 ? n : 1;
    }
    int cols = 1;
    for (const char *p = value; *p; p++) {
        if (isspace((unsigned char)*p)) {
            while (isspace((unsigned char)*p)) p++;
            if (*p) cols++;
        }
    }
    return cols > 0 && cols < 12 ? cols : 1;
}

static void parse_decl(KStyle *style, unsigned *mask, char *name, char *value) {
    name = trim(name);
    value = trim(value);
    if (!*name || !*value) return;

    if (!_stricmp(name, "display")) {
        if (!_stricmp(value, "none")) style->display = K_DISPLAY_NONE;
        else if (!_stricmp(value, "flex")) style->display = K_DISPLAY_FLEX;
        else if (!_stricmp(value, "grid")) style->display = K_DISPLAY_GRID;
        else if (!_stricmp(value, "inline")) style->display = K_DISPLAY_INLINE;
        else style->display = K_DISPLAY_BLOCK;
        *mask |= CSS_DISPLAY;
    } else if (!_stricmp(name, "color")) {
        style->color = parse_color(value, style->color);
        *mask |= CSS_COLOR;
    } else if (!_stricmp(name, "background") || !_stricmp(name, "background-color")) {
        style->background = parse_color(value, style->background);
        style->has_background = style->background.a > 0.0f;
        *mask |= CSS_BACKGROUND;
    } else if (!_stricmp(name, "font-size")) {
        style->font_size = parse_px(value, style->font_size);
        style->line_height = style->font_size * 1.35f;
        *mask |= CSS_FONT_SIZE;
    } else if (!_stricmp(name, "line-height")) {
        style->line_height = parse_px(value, style->line_height);
        *mask |= CSS_LINE_HEIGHT;
    } else if (!_stricmp(name, "margin")) {
        parse_edges(style->margin, value);
        *mask |= CSS_MARGIN;
    } else if (!_stricmp(name, "padding")) {
        parse_edges(style->padding, value);
        *mask |= CSS_PADDING;
    } else if (!_stricmp(name, "border-width")) {
        style->border_width = parse_px(value, style->border_width);
        *mask |= CSS_BORDER;
    } else if (!_stricmp(name, "border-color")) {
        style->border_color = parse_color(value, style->border_color);
        *mask |= CSS_BORDER;
    } else if (!_stricmp(name, "border")) {
        style->border_width = parse_px(value, 1.0f);
        const char *hash = strchr(value, '#');
        if (hash) style->border_color = parse_color(hash, style->border_color);
        *mask |= CSS_BORDER;
    } else if (!_stricmp(name, "border-radius")) {
        style->border_radius = parse_px(value, style->border_radius);
        *mask |= CSS_RADIUS;
    } else if (!_stricmp(name, "width")) {
        style->width = parse_px(value, style->width);
        *mask |= CSS_WIDTH;
    } else if (!_stricmp(name, "height")) {
        style->height = parse_px(value, style->height);
        *mask |= CSS_HEIGHT;
    } else if (!_stricmp(name, "font-weight")) {
        style->font_weight = strstr(value, "bold") ? 700 : atoi(value);
        if (style->font_weight <= 0) style->font_weight = 400;
        *mask |= CSS_WEIGHT;
    } else if (!_stricmp(name, "text-decoration")) {
        style->underline = strstr(value, "underline") != NULL;
        *mask |= CSS_UNDERLINE;
    } else if (!_stricmp(name, "grid-template-columns")) {
        style->grid_columns = parse_grid_columns(value);
        *mask |= CSS_GRID;
    } else if (!_stricmp(name, "flex-direction")) {
        style->flex_row = _stricmp(value, "column") != 0;
        *mask |= CSS_FLEX;
    }
}

static void parse_declarations(KStyle *style, unsigned *mask, const char *src) {
    char *copy = kstrdup(src ? src : "");
    char *ctx = NULL;
    for (char *decl = strtok_s(copy, ";", &ctx); decl; decl = strtok_s(NULL, ";", &ctx)) {
        char *colon = strchr(decl, ':');
        if (!colon) continue;
        *colon = 0;
        parse_decl(style, mask, decl, colon + 1);
    }
    kfree(copy);
}

KCssRule *cyclone_parse_css(const char *css) {
    KCssRule *head = NULL;
    KCssRule **tail = &head;
    const char *p = css ? css : "";
    while (*p) {
        const char *open = strchr(p, '{');
        if (!open) break;
        const char *close = strchr(open + 1, '}');
        if (!close) break;
        char selector_buf[256];
        size_t selector_len = (size_t)(open - p);
        if (selector_len >= sizeof(selector_buf)) selector_len = sizeof(selector_buf) - 1;
        memcpy(selector_buf, p, selector_len);
        selector_buf[selector_len] = 0;

        char decl_buf[2048];
        size_t decl_len = (size_t)(close - open - 1);
        if (decl_len >= sizeof(decl_buf)) decl_len = sizeof(decl_buf) - 1;
        memcpy(decl_buf, open + 1, decl_len);
        decl_buf[decl_len] = 0;

        char *ctx = NULL;
        for (char *sel = strtok_s(selector_buf, ",", &ctx); sel; sel = strtok_s(NULL, ",", &ctx)) {
            sel = trim(sel);
            if (!*sel) continue;
            KCssRule *rule = (KCssRule *)kzalloc(sizeof(KCssRule));
            strncpy(rule->selector, sel, sizeof(rule->selector) - 1);
            rule->style = cyclone_default_style_for(NULL);
            parse_declarations(&rule->style, &rule->mask, decl_buf);
            *tail = rule;
            tail = &rule->next;
        }
        p = close + 1;
    }
    return head;
}

void cyclone_free_css(KCssRule *rules) {
    while (rules) {
        KCssRule *next = rules->next;
        kfree(rules);
        rules = next;
    }
}

static int has_class(const char *classes, const char *needle) {
    if (!classes || !needle || !*needle) return 0;
    size_t n = strlen(needle);
    const char *p = classes;
    while (*p) {
        while (isspace((unsigned char)*p)) p++;
        const char *start = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if ((size_t)(p - start) == n && !_strnicmp(start, needle, n)) return 1;
    }
    return 0;
}

static int selector_matches(const KCssRule *rule, const KDomNode *node) {
    if (!node || node->type != K_NODE_ELEMENT || !rule->selector[0]) return 0;
    const char *sel = rule->selector;
    const char *last_space = strrchr(sel, ' ');
    if (last_space) sel = trim((char *)last_space);
    if (*sel == '#') return node->id && !_stricmp(node->id, sel + 1);
    if (*sel == '.') return has_class(node->classes, sel + 1);
    const char *dot = strchr(sel, '.');
    if (dot) {
        char tag[64];
        size_t n = (size_t)(dot - sel);
        if (n >= sizeof(tag)) n = sizeof(tag) - 1;
        memcpy(tag, sel, n);
        tag[n] = 0;
        return node->tag && !_stricmp(node->tag, tag) && has_class(node->classes, dot + 1);
    }
    return node->tag && !_stricmp(node->tag, sel);
}

static void merge_style(KStyle *dst, const KStyle *src, unsigned mask) {
    if (mask & CSS_DISPLAY) dst->display = src->display;
    if (mask & CSS_COLOR) dst->color = src->color;
    if (mask & CSS_BACKGROUND) {
        dst->background = src->background;
        dst->has_background = src->has_background;
    }
    if (mask & CSS_FONT_SIZE) dst->font_size = src->font_size;
    if (mask & CSS_LINE_HEIGHT) dst->line_height = src->line_height;
    if (mask & CSS_MARGIN) memcpy(dst->margin, src->margin, sizeof(dst->margin));
    if (mask & CSS_PADDING) memcpy(dst->padding, src->padding, sizeof(dst->padding));
    if (mask & CSS_BORDER) {
        dst->border_width = src->border_width;
        dst->border_color = src->border_color;
    }
    if (mask & CSS_RADIUS) dst->border_radius = src->border_radius;
    if (mask & CSS_WIDTH) dst->width = src->width;
    if (mask & CSS_HEIGHT) dst->height = src->height;
    if (mask & CSS_WEIGHT) dst->font_weight = src->font_weight;
    if (mask & CSS_UNDERLINE) dst->underline = src->underline;
    if (mask & CSS_GRID) dst->grid_columns = src->grid_columns;
    if (mask & CSS_FLEX) dst->flex_row = src->flex_row;
}

void cyclone_apply_inline_style(KStyle *style, const char *inline_css) {
    unsigned mask = 0;
    KStyle parsed = *style;
    parse_declarations(&parsed, &mask, inline_css);
    merge_style(style, &parsed, mask);
}

static void apply_rec(KDomNode *node, KCssRule *rules, const KStyle *parent_style) {
    for (KDomNode *n = node; n; n = n->next) {
        if (n->type == K_NODE_TEXT && parent_style) {
            n->style = *parent_style;
        } else {
            n->style = cyclone_default_style_for(n);
            if (parent_style) {
                n->style.color = parent_style->color;
                n->style.font_size = parent_style->font_size;
                n->style.line_height = parent_style->line_height;
                n->style.font_weight = parent_style->font_weight;
            }
            for (KCssRule *r = rules; r; r = r->next) {
                if (selector_matches(r, n)) {
                    merge_style(&n->style, &r->style, r->mask);
                }
            }
            if (n->style_attr) {
                cyclone_apply_inline_style(&n->style, n->style_attr);
            }
        }
        apply_rec(n->first_child, rules, &n->style);
    }
}

void cyclone_apply_styles(KDomNode *doc, KCssRule *rules) {
    KStyle root = cyclone_default_style_for(NULL);
    apply_rec(doc, rules, &root);
}
