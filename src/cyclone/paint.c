#include "kerosene.h"

#include <string.h>

static void render_reserve(KRenderList *list, size_t needed) {
    if (needed <= list->capacity) {
        return;
    }
    size_t next = list->capacity ? list->capacity * 2 : 256;
    while (next < needed) {
        next *= 2;
    }
    list->items = (KRenderCommand *)krealloc_owned(list->items, next * sizeof(KRenderCommand));
    list->capacity = next;
}

void render_list_init(KRenderList *list) {
    memset(list, 0, sizeof(*list));
}

void render_list_reset(KRenderList *list) {
    for (size_t i = 0; i < list->count; i++) {
        kfree(list->items[i].text);
        kfree(list->items[i].href);
    }
    list->count = 0;
    list->content_height = 0.0f;
}

void render_list_free(KRenderList *list) {
    render_list_reset(list);
    kfree(list->items);
    memset(list, 0, sizeof(*list));
}

void render_add_rect(KRenderList *list, KRect rect, KColor color, float radius) {
    if (rect.w <= 0.0f || rect.h <= 0.0f || color.a <= 0.0f) {
        return;
    }
    render_reserve(list, list->count + 1);
    KRenderCommand *cmd = &list->items[list->count++];
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = K_RENDER_RECT;
    cmd->rect = rect;
    cmd->color = color;
    cmd->radius = radius;
}

void render_add_border(KRenderList *list, KRect rect, KColor color, float width, float radius) {
    if (rect.w <= 0.0f || rect.h <= 0.0f || width <= 0.0f || color.a <= 0.0f) {
        return;
    }
    render_reserve(list, list->count + 1);
    KRenderCommand *cmd = &list->items[list->count++];
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = K_RENDER_BORDER;
    cmd->rect = rect;
    cmd->color = color;
    cmd->radius = radius;
    cmd->font_size = width;
}

void render_add_text(KRenderList *list, KRect rect, KColor color, const char *text, const char *href, float font_size, int font_weight, int underline) {
    if (!text || !*text || rect.w <= 0.0f || rect.h <= 0.0f || color.a <= 0.0f) {
        return;
    }
    render_reserve(list, list->count + 1);
    KRenderCommand *cmd = &list->items[list->count++];
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = K_RENDER_TEXT;
    cmd->rect = rect;
    cmd->color = color;
    cmd->text = kstrdup(text);
    cmd->href = href && *href ? kstrdup(href) : NULL;
    cmd->font_size = font_size;
    cmd->font_weight = font_weight;
    cmd->underline = underline;
}
