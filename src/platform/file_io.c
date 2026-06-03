#include "kerosene.h"

#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *kzalloc(size_t bytes) {
    void *ptr = calloc(1, bytes ? bytes : 1);
    if (!ptr) {
        ExitProcess(12);
    }
    return ptr;
}

void *krealloc_owned(void *ptr, size_t bytes) {
    void *next = realloc(ptr, bytes ? bytes : 1);
    if (!next) {
        ExitProcess(12);
    }
    return next;
}

char *kstrdup(const char *text) {
    if (!text) {
        text = "";
    }
    size_t len = strlen(text);
    return kstrndup(text, len);
}

char *kstrndup(const char *text, size_t len) {
    char *out = (char *)kzalloc(len + 1);
    if (len) {
        memcpy(out, text, len);
    }
    out[len] = 0;
    return out;
}

void kfree(void *ptr) {
    free(ptr);
}

uint64_t ktime_ms(void) {
    return GetTickCount64();
}

char *platform_read_file(const wchar_t *path, size_t *out_len) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)kzalloc((size_t)len + 1);
    fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (out_len) *out_len = (size_t)len;
    return buf;
}

bool platform_write_file(const wchar_t *path, const void *data, size_t len) {
    FILE *f = _wfopen(path, L"wb");
    if (!f) return false;
    size_t wrote = fwrite(data, 1, len, f);
    fclose(f);
    return wrote == len;
}

void platform_get_data_dir(wchar_t *out, size_t cap) {
    if (!out || cap == 0) return;
    wchar_t base[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, base) != S_OK) {
        GetCurrentDirectoryW(MAX_PATH, base);
    }
    _snwprintf(out, cap, L"%ls\\Kerosene", base);
    CreateDirectoryW(out, NULL);
}

void platform_open_external(const char *url) {
    wchar_t *wurl = platform_utf8_to_wide(url, NULL);
    if (wurl) {
        ShellExecuteW(NULL, L"open", wurl, NULL, NULL, SW_SHOWNORMAL);
        kfree(wurl);
    }
}
