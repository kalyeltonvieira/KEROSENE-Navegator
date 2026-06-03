#include "kerosene.h"

wchar_t *platform_utf8_to_wide(const char *text, int *out_len) {
    if (!text) {
        text = "";
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (len <= 0) {
        return NULL;
    }
    wchar_t *wide = (wchar_t *)kzalloc((size_t)len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, len);
    if (out_len) {
        *out_len = len - 1;
    }
    return wide;
}
