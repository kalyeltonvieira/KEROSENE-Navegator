#include "kerosene.h"

#include <ctype.h>
#include <string.h>

typedef struct CacheEntry {
    char url[K_MAX_URL];
    char content_type[128];
    char *body;
    size_t body_len;
    int status;
    uint64_t stamp;
} CacheEntry;

static CacheEntry g_cache[32];
static LONG g_clock = 0;

typedef struct AdTrieNode {
    int child[128];
    int terminal;
} AdTrieNode;

static AdTrieNode g_ad_nodes[2048];
static int g_ad_count = 1;
static int g_ad_ready = 0;

static const char *g_ad_patterns[] = {
    "doubleclick.net", "googlesyndication.com", "google-analytics.com",
    "/ads/", "/adserver/", "adservice.google.", "facebook.com/tr",
    "pixel.", "tracking", "utm_source=", "scorecardresearch.com",
    "taboola.com", "outbrain.com", NULL
};

static void response_copy(KHttpResponse *out, const CacheEntry *entry) {
    memset(out, 0, sizeof(*out));
    out->status = entry->status;
    out->body_len = entry->body_len;
    out->body = (char *)kzalloc(entry->body_len + 1);
    memcpy(out->body, entry->body, entry->body_len);
    strncpy(out->final_url, entry->url, sizeof(out->final_url) - 1);
    strncpy(out->content_type, entry->content_type, sizeof(out->content_type) - 1);
}

bool cache_get(const char *url, KHttpResponse *out) {
    if (!url || !out) return false;
    for (size_t i = 0; i < sizeof(g_cache) / sizeof(g_cache[0]); i++) {
        if (g_cache[i].body && !_stricmp(g_cache[i].url, url)) {
            g_cache[i].stamp = (uint64_t)InterlockedIncrement(&g_clock);
            response_copy(out, &g_cache[i]);
            return true;
        }
    }
    return false;
}

void cache_put(const char *url, const KHttpResponse *response) {
    if (!url || !response || !response->body || response->body_len == 0 || response->body_len > 4 * 1024 * 1024) {
        return;
    }
    size_t slot = 0;
    uint64_t oldest = UINT64_MAX;
    for (size_t i = 0; i < sizeof(g_cache) / sizeof(g_cache[0]); i++) {
        if (!g_cache[i].body) {
            slot = i;
            oldest = 0;
            break;
        }
        if (g_cache[i].stamp < oldest) {
            oldest = g_cache[i].stamp;
            slot = i;
        }
    }
    kfree(g_cache[slot].body);
    memset(&g_cache[slot], 0, sizeof(g_cache[slot]));
    strncpy(g_cache[slot].url, url, sizeof(g_cache[slot].url) - 1);
    strncpy(g_cache[slot].content_type, response->content_type, sizeof(g_cache[slot].content_type) - 1);
    g_cache[slot].body = (char *)kzalloc(response->body_len + 1);
    memcpy(g_cache[slot].body, response->body, response->body_len);
    g_cache[slot].body_len = response->body_len;
    g_cache[slot].status = response->status;
    g_cache[slot].stamp = (uint64_t)InterlockedIncrement(&g_clock);
}

void adblock_init(void) {
    memset(g_ad_nodes, 0, sizeof(g_ad_nodes));
    g_ad_count = 1;
    for (int i = 0; g_ad_patterns[i]; i++) {
        int node = 0;
        for (const char *p = g_ad_patterns[i]; *p; p++) {
            unsigned char c = (unsigned char)tolower((unsigned char)*p);
            if (c >= 128) {
                continue;
            }
            if (!g_ad_nodes[node].child[c]) {
                if (g_ad_count >= (int)(sizeof(g_ad_nodes) / sizeof(g_ad_nodes[0]))) {
                    break;
                }
                g_ad_nodes[node].child[c] = g_ad_count++;
            }
            node = g_ad_nodes[node].child[c];
        }
        g_ad_nodes[node].terminal = 1;
    }
    g_ad_ready = 1;
}

bool adblock_should_block(const char *url) {
    if (!url) return false;
    if (!g_ad_ready) {
        adblock_init();
    }
    for (const char *start = url; *start; start++) {
        int node = 0;
        for (const char *p = start; *p; p++) {
            unsigned char c = (unsigned char)tolower((unsigned char)*p);
            if (c >= 128 || !g_ad_nodes[node].child[c]) {
                break;
            }
            node = g_ad_nodes[node].child[c];
            if (g_ad_nodes[node].terminal) {
                return true;
            }
        }
    }
    return false;
}
