#include "kerosene.h"

#include <dwrite.h>
#include <string.h>

typedef struct TextFormatEntry {
    float size;
    int weight;
    IDWriteTextFormat *format;
} TextFormatEntry;

struct KTextAtlas {
    IDWriteFactory *factory;
    TextFormatEntry entries[32];
    size_t count;
};

KTextAtlas *text_atlas_create(void *dwrite_factory) {
    if (!dwrite_factory) {
        return NULL;
    }
    KTextAtlas *atlas = (KTextAtlas *)kzalloc(sizeof(KTextAtlas));
    atlas->factory = (IDWriteFactory *)dwrite_factory;
    IDWriteFactory_AddRef(atlas->factory);
    return atlas;
}

void *text_atlas_format(KTextAtlas *atlas, float font_size, int font_weight) {
    if (!atlas) {
        return NULL;
    }
    if (font_size < 8.0f) font_size = 8.0f;
    if (font_size > 96.0f) font_size = 96.0f;
    int weight = font_weight >= 600 ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    for (size_t i = 0; i < atlas->count; i++) {
        if ((int)(atlas->entries[i].size * 10.0f) == (int)(font_size * 10.0f) && atlas->entries[i].weight == weight) {
            return atlas->entries[i].format;
        }
    }
    if (atlas->count >= sizeof(atlas->entries) / sizeof(atlas->entries[0])) {
        return atlas->entries[0].format;
    }
    IDWriteTextFormat *format = NULL;
    HRESULT hr = IDWriteFactory_CreateTextFormat(
        atlas->factory,
        L"Segoe UI",
        NULL,
        (DWRITE_FONT_WEIGHT)weight,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        font_size,
        L"",
        &format);
    if (FAILED(hr)) {
        return NULL;
    }
    IDWriteTextFormat_SetWordWrapping(format, DWRITE_WORD_WRAPPING_WRAP);
    IDWriteTextFormat_SetTrimming(format, NULL, NULL);
    atlas->entries[atlas->count].size = font_size;
    atlas->entries[atlas->count].weight = weight;
    atlas->entries[atlas->count].format = format;
    atlas->count++;
    return format;
}

void text_atlas_free(KTextAtlas *atlas) {
    if (!atlas) return;
    for (size_t i = 0; i < atlas->count; i++) {
        if (atlas->entries[i].format) {
            IDWriteTextFormat_Release(atlas->entries[i].format);
        }
    }
    if (atlas->factory) {
        IDWriteFactory_Release(atlas->factory);
    }
    kfree(atlas);
}
