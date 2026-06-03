#include "kerosene.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void url_encode(const char *in, char *out, size_t cap) {
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;
    for (const unsigned char *p = (const unsigned char *)(in ? in : ""); *p && j + 4 < cap; p++) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
            out[j++] = (char)*p;
        } else if (*p == ' ') {
            out[j++] = '+';
        } else {
            out[j++] = '%';
            out[j++] = hex[*p >> 4];
            out[j++] = hex[*p & 15];
        }
    }
    if (cap) out[j < cap ? j : cap - 1] = 0;
}

static int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

static int hex_digit_local(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int base64_decode_url(const char *in, char *out, size_t cap) {
    unsigned int buf = 0;
    int bits = 0;
    size_t j = 0;
    for (const char *p = in; *p; p++) {
        if (*p == '=' || *p == '&' || *p == '"') break;
        int v = b64_value(*p);
        if (v < 0) continue;
        buf = (buf << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (j + 1 < cap) out[j++] = (char)((buf >> bits) & 0xFF);
        }
    }
    if (cap) out[j < cap ? j : cap - 1] = 0;
    return j > 0;
}

static void html_decode_to(const char *src, size_t len, char *out, size_t cap) {
    size_t j = 0;
    int last_space = 1;
    for (size_t i = 0; i < len && j + 1 < cap; i++) {
        char c = src[i];
        if (c == '&') {
            if (i + 6 <= len && !strncmp(src + i, "&nbsp;", 6)) { c = ' '; i += 5; }
            else if (i + 6 <= len && !strncmp(src + i, "&apos;", 6)) { c = '\''; i += 5; }
            else if (i + 6 <= len && !strncmp(src + i, "&quot;", 6)) { c = '"'; i += 5; }
            else if (i + 5 <= len && !strncmp(src + i, "&amp;", 5)) { c = '&'; i += 4; }
            else if (i + 4 <= len && !strncmp(src + i, "&lt;", 4)) { c = '<'; i += 3; }
            else if (i + 4 <= len && !strncmp(src + i, "&gt;", 4)) { c = '>'; i += 3; }
            else if (i + 3 < len && src[i + 1] == '#') {
                size_t k = i + 2;
                int hex = 0;
                unsigned code = 0;
                if (k < len && (src[k] == 'x' || src[k] == 'X')) {
                    hex = 1;
                    k++;
                }
                while (k < len && src[k] != ';') {
                    int v = hex ? hex_digit_local(src[k]) : (isdigit((unsigned char)src[k]) ? src[k] - '0' : -1);
                    if (v < 0) break;
                    code = code * (hex ? 16u : 10u) + (unsigned)v;
                    k++;
                }
                if (k < len && src[k] == ';' && code) {
                    if (code == 160) {
                        c = ' ';
                    } else if (code < 0x80 && j + 1 < cap) {
                        c = (char)code;
                    } else if (code < 0x800 && j + 2 < cap) {
                        out[j++] = (char)(0xC0 | (code >> 6));
                        out[j++] = (char)(0x80 | (code & 0x3F));
                        i = k;
                        last_space = 0;
                        continue;
                    } else if (code < 0x10000 && j + 3 < cap) {
                        out[j++] = (char)(0xE0 | (code >> 12));
                        out[j++] = (char)(0x80 | ((code >> 6) & 0x3F));
                        out[j++] = (char)(0x80 | (code & 0x3F));
                        i = k;
                        last_space = 0;
                        continue;
                    } else {
                        c = ' ';
                    }
                    i = k;
                }
            }
        }
        if (isspace((unsigned char)c)) {
            if (!last_space) {
                out[j++] = ' ';
                last_space = 1;
            }
            continue;
        }
        out[j++] = c;
        last_space = 0;
    }
    while (j && out[j - 1] == ' ') j--;
    out[j] = 0;
}

static void strip_tags_decode(const char *src, size_t len, char *out, size_t cap) {
    char *tmp = (char *)kzalloc(len + 1);
    size_t j = 0;
    int tag = 0;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '<') {
            tag = 1;
            if (j && tmp[j - 1] != ' ') tmp[j++] = ' ';
            continue;
        }
        if (src[i] == '>') {
            tag = 0;
            continue;
        }
        if (!tag && j < len) {
            tmp[j++] = src[i];
        }
    }
    html_decode_to(tmp, j, out, cap);
    kfree(tmp);
}

static const char *find_ci_local(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    for (const char *p = hay; *p; p++) {
        if (_strnicmp(p, needle, n) == 0) return p;
    }
    return NULL;
}

static void extract_domain(const char *url, char *out, size_t cap) {
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    const char *end = p;
    while (*end && *end != '/' && *end != '?' && *end != '#') end++;
    size_t len = (size_t)(end - p);
    if (len > 4 && !_strnicmp(p, "www.", 4)) {
        p += 4;
        len -= 4;
    }
    if (len >= cap) len = cap - 1;
    memcpy(out, p, len);
    out[len] = 0;
}

static void parse_bing_total_label(const char *html, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = 0;
    const char *p = strstr(html, "<span class=\"sb_count\">");
    if (!p) return;
    p = strchr(p, '>');
    if (!p) return;
    p++;
    const char *end = strstr(p, "</span>");
    if (!end) return;
    char raw[128];
    html_decode_to(p, (size_t)(end - p), raw, sizeof(raw));
    if (!_strnicmp(raw, "Sobre ", 6)) {
        snprintf(out, cap, "aproximadamente %s", raw + 6);
    } else if (!_strnicmp(raw, "About ", 6)) {
        snprintf(out, cap, "aproximadamente %s", raw + 6);
    } else if (raw[0]) {
        snprintf(out, cap, "%s", raw);
    }
}

static uint32_t domain_color(const char *domain) {
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)domain; *p; p++) {
        h ^= *p;
        h *= 16777619u;
    }
    static const uint32_t colors[] = {
        0xFF1A73E8, 0xFF188038, 0xFFD93025, 0xFF9334E6,
        0xFFF29900, 0xFF00796B, 0xFF5F6368, 0xFF0B57D0
    };
    return colors[h % (sizeof(colors) / sizeof(colors[0]))];
}

static void decode_bing_href(const char *href, char *out, size_t cap) {
    const char *u = strstr(href, "u=a1");
    if (u) {
        u += 4;
        if (base64_decode_url(u, out, cap) && strstr(out, "://")) return;
    }
    html_decode_to(href, strlen(href), out, cap);
}

static int parse_bing(const char *html, KSearchResult *results, int max_results) {
    int count = 0;
    const char *p = html;
    while (count < max_results && (p = strstr(p, "<li class=\"b_algo\"")) != NULL) {
        const char *next = strstr(p + 10, "<li class=\"b_algo\"");
        const char *h2 = strstr(p, "<h2");
        if (!h2 || (next && h2 > next)) { p += 10; continue; }
        const char *a = strstr(h2, "<a ");
        const char *href = a ? strstr(a, "href=\"") : NULL;
        const char *a_end = a ? strstr(a, "</a>") : NULL;
        if (!href || !a_end || (next && a_end > next)) { p = next ? next : p + 10; continue; }
        href += 6;
        const char *href_end = strchr(href, '"');
        if (!href_end) { p = next ? next : p + 10; continue; }

        const char *gt = strchr(a, '>');
        if (!gt || gt > a_end) { p = next ? next : p + 10; continue; }

        KSearchResult *r = &results[count];
        memset(r, 0, sizeof(*r));
        char href_buf[K_MAX_URL];
        size_t href_len = (size_t)(href_end - href);
        if (href_len >= sizeof(href_buf)) href_len = sizeof(href_buf) - 1;
        memcpy(href_buf, href, href_len);
        href_buf[href_len] = 0;
        decode_bing_href(href_buf, r->url, sizeof(r->url));
        if (!strstr(r->url, "://") || strstr(r->url, "bing.com/ck/")) {
            p = next ? next : p + 10;
            continue;
        }

        strip_tags_decode(gt + 1, (size_t)(a_end - gt - 1), r->title, sizeof(r->title));
        const char *caption = strstr(h2, "<p");
        if (caption && (!next || caption < next)) {
            const char *pgt = strchr(caption, '>');
            const char *pend = pgt ? strstr(pgt, "</p>") : NULL;
            if (pgt && pend && (!next || pend < next)) {
                strip_tags_decode(pgt + 1, (size_t)(pend - pgt - 1), r->snippet, sizeof(r->snippet));
            }
        }
        extract_domain(r->url, r->domain, sizeof(r->domain));
        r->accent = domain_color(r->domain);
        if (r->title[0] && r->url[0]) {
            count++;
        }
        p = next ? next : a_end;
    }
    return count;
}

static int parse_wiby(const char *html, KSearchResult *results, int max_results) {
    int count = 0;
    const char *p = html;
    while (count < max_results && (p = strstr(p, "<a href=\"")) != NULL) {
        const char *href = p + 9;
        const char *href_end = strchr(href, '"');
        const char *gt = href_end ? strchr(href_end, '>') : NULL;
        const char *end = gt ? strstr(gt, "</a>") : NULL;
        if (!href_end || !gt || !end) break;
        KSearchResult *r = &results[count];
        memset(r, 0, sizeof(*r));
        html_decode_to(href, (size_t)(href_end - href), r->url, sizeof(r->url));
        strip_tags_decode(gt + 1, (size_t)(end - gt - 1), r->title, sizeof(r->title));
        const char *after = end + 4;
        const char *next = strstr(after, "<a href=\"");
        const char *abs = find_ci_local(after, "ABSTRACT:");
        if (abs && (!next || abs < next)) {
            const char *snip_end = next ? next : abs + strlen(abs);
            if (snip_end - abs > 700) snip_end = abs + 700;
            strip_tags_decode(abs, (size_t)(snip_end - abs), r->snippet, sizeof(r->snippet));
        }
        extract_domain(r->url, r->domain, sizeof(r->domain));
        r->accent = domain_color(r->domain);
        if (strstr(r->url, "://") && r->title[0]) count++;
        p = end + 4;
    }
    return count;
}

int search_fetch_results(const char *query, int page, KSearchResult *results, int max_results, char *provider, size_t provider_cap, char *total_label, size_t total_label_cap, char *err, size_t err_cap) {
    if (!query || !*query || !results || max_results <= 0) return 0;
    if (page < 1) page = 1;
    memset(results, 0, (size_t)max_results * sizeof(results[0]));
    if (provider && provider_cap) provider[0] = 0;
    if (total_label && total_label_cap) total_label[0] = 0;
    char encoded[512];
    url_encode(query, encoded, sizeof(encoded));

    char url[K_MAX_URL];
    KHttpResponse response;
    char local_err[256] = {0};
    int first = (page - 1) * max_results + 1;
    snprintf(url, sizeof(url), "https://www.bing.com/search?q=%s&first=%d", encoded, first);
    if (http_get(url, &response, local_err, sizeof(local_err))) {
        int n = parse_bing(response.body, results, max_results);
        parse_bing_total_label(response.body, total_label, total_label_cap);
        http_response_free(&response);
        if (n > 0) {
            snprintf(provider, provider_cap, "Bing web index - pagina %d", page);
            return n;
        }
    }

    snprintf(url, sizeof(url), "https://wiby.me/?q=%s&p=%d", encoded, page);
    if (http_get(url, &response, local_err, sizeof(local_err))) {
        int n = parse_wiby(response.body, results, max_results);
        http_response_free(&response);
        if (n > 0) {
            snprintf(provider, provider_cap, "Wiby fallback - pagina %d", page);
            if (total_label && total_label_cap) snprintf(total_label, total_label_cap, "resultados encontrados");
            return n;
        }
    }

    if (err && err_cap) {
        snprintf(err, err_cap, "Nenhum backend de busca retornou resultados (%s)", local_err);
    }
    return 0;
}
