#include "kerosene.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

static float maxf(float a, float b) {
    return a > b ? a : b;
}

static char *trim_copy(const char *text, size_t len) {
    while (len && isspace((unsigned char)*text)) {
        text++;
        len--;
    }
    while (len && isspace((unsigned char)text[len - 1])) {
        len--;
    }
    return kstrndup(text, len);
}

static float layout_text(KDomNode *node, KRenderList *out, float x, float y, float w, const char *href) {
    const char *text = node->text ? node->text : "";
    float font = node->style.font_size > 0.0f ? node->style.font_size : 16.0f;
    float line = node->style.line_height > 0.0f ? node->style.line_height : font * 1.35f;
    float char_w = maxf(font * 0.52f, 4.0f);
    int max_chars = (int)(w / char_w);
    if (max_chars < 8) {
        max_chars = 8;
    }

    const char *p = text;
    float cursor_y = y;
    while (*p) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;
        const char *line_start = p;
        const char *line_end = p;
        int count = 0;
        const char *last_space = NULL;
        while (*p && count < max_chars) {
            if (isspace((unsigned char)*p)) {
                last_space = p;
            }
            p++;
            count++;
        }
        if (*p && last_space && last_space > line_start) {
            line_end = last_space;
            p = last_space + 1;
        } else {
            line_end = p;
        }
        char *line_text = trim_copy(line_start, (size_t)(line_end - line_start));
        if (*line_text) {
            render_add_text(out, (KRect){ x, cursor_y, w, line }, node->style.color, line_text, href, font, node->style.font_weight, node->style.underline);
            cursor_y += line;
        }
        kfree(line_text);
    }
    return cursor_y - y;
}

static const char *node_href(KDomNode *node, const char *inherited) {
    if (node && node->href && *node->href) {
        return node->href;
    }
    return inherited;
}

float cyclone_layout_node(KDomNode *node, KRenderList *out, float x, float y, float w, const char *inherited_href) {
    if (!node || node->style.display == K_DISPLAY_NONE) {
        return 0.0f;
    }

    if (node->type == K_NODE_TEXT) {
        return layout_text(node, out, x, y, w, inherited_href);
    }

    if (node->type == K_NODE_DOCUMENT) {
        float cy = y;
        for (KDomNode *child = node->first_child; child; child = child->next) {
            cy += cyclone_layout_node(child, out, x, cy, w, inherited_href);
        }
        return cy - y;
    }

    const KStyle *s = &node->style;
    float ml = s->margin[3], mr = s->margin[1], mt = s->margin[0], mb = s->margin[2];
    float pl = s->padding[3], pr = s->padding[1], pt = s->padding[0], pb = s->padding[2];
    float outer_x = x + ml;
    float outer_y = y + mt;
    float outer_w = s->width > 0.0f ? s->width : maxf(20.0f, w - ml - mr);
    float content_x = outer_x + pl + s->border_width;
    float content_y = outer_y + pt + s->border_width;
    float content_w = maxf(20.0f, outer_w - pl - pr - s->border_width * 2.0f);
    const char *href = node_href(node, inherited_href);

    size_t bg_index = (size_t)-1;
    if (s->has_background) {
        bg_index = out->count;
        render_add_rect(out, (KRect){ outer_x, outer_y, outer_w, 1.0f }, s->background, s->border_radius);
    }
    size_t border_index = (size_t)-1;
    if (s->border_width > 0.0f) {
        border_index = out->count;
        render_add_border(out, (KRect){ outer_x, outer_y, outer_w, 1.0f }, s->border_color, s->border_width, s->border_radius);
    }

    float used = 0.0f;
    if (node->tag && (!strcmp(node->tag, "img") || !strcmp(node->tag, "canvas") || !strcmp(node->tag, "video"))) {
        float h = s->height > 0.0f ? s->height : 180.0f;
        KColor ph = s->has_background ? s->background : kcolor_rgba(0.91f, 0.93f, 0.96f, 1.0f);
        if (!s->has_background) {
            render_add_rect(out, (KRect){ outer_x, outer_y, outer_w, h }, ph, s->border_radius);
        }
        const char *label = node->src && *node->src ? node->src : node->tag;
        render_add_text(out, (KRect){ content_x + 8.0f, content_y + 8.0f, content_w - 16.0f, 24.0f },
            kcolor_rgba(0.35f, 0.40f, 0.48f, 1.0f), label, href, 13.0f, 400, 0);
        used = h;
    } else if (s->display == K_DISPLAY_FLEX) {
        used = cyclone_layout_flex_children(node, out, content_x, content_y, content_w);
    } else if (s->display == K_DISPLAY_GRID) {
        used = cyclone_layout_grid_children(node, out, content_x, content_y, content_w);
    } else {
        float cy = content_y;
        for (KDomNode *child = node->first_child; child; child = child->next) {
            cy += cyclone_layout_node(child, out, content_x, cy, content_w, href);
        }
        used = cy - content_y;
    }

    float total_h = s->height > 0.0f ? s->height : used + pt + pb + s->border_width * 2.0f;
    if (total_h < s->line_height && node->first_child) {
        total_h = s->line_height + pt + pb;
    }
    if (bg_index != (size_t)-1 && bg_index < out->count) {
        out->items[bg_index].rect.h = total_h;
    }
    if (border_index != (size_t)-1 && border_index < out->count) {
        out->items[border_index].rect.h = total_h;
    }
    node->box = (KRect){ outer_x, outer_y, outer_w, total_h };
    return mt + total_h + mb;
}

static KDomNode *best_content_root(KDomNode *doc) {
    KDomNode *main = cyclone_find_first(doc, "main");
    if (main) return main;
    KDomNode *article = cyclone_find_first(doc, "article");
    if (article) return article;
    KDomNode *body = cyclone_find_first(doc, "body");
    return body ? body : doc;
}

void cyclone_layout_page(KDomNode *doc, KRenderList *out, float viewport_w, float viewport_h, int reader_mode) {
    render_list_reset(out);
    if (!doc) {
        return;
    }
    float max_w = reader_mode ? 760.0f : viewport_w;
    if (max_w > viewport_w - 36.0f) {
        max_w = viewport_w - 36.0f;
    }
    if (max_w < 260.0f) {
        max_w = viewport_w;
    }
    float x = reader_mode ? (viewport_w - max_w) * 0.5f : 0.0f;
    KDomNode *root = reader_mode ? best_content_root(doc) : doc;
    float h = cyclone_layout_node(root, out, x, 0.0f, max_w, NULL);
    out->content_height = h + 24.0f;
}
