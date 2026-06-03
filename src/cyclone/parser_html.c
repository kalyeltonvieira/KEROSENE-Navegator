#include "kerosene.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static KDomNode *node_new(KNodeType type, const char *name, size_t name_len) {
    KDomNode *node = (KDomNode *)kzalloc(sizeof(KDomNode));
    node->type = type;
    if (name && name_len) {
        node->tag = kstrndup(name, name_len);
        for (char *p = node->tag; *p; p++) {
            *p = (char)tolower((unsigned char)*p);
        }
    }
    node->style = cyclone_default_style_for(node);
    return node;
}

static void node_append(KDomNode *parent, KDomNode *child) {
    if (!parent || !child) {
        return;
    }
    child->parent = parent;
    if (!parent->first_child) {
        parent->first_child = child;
    } else {
        parent->last_child->next = child;
    }
    parent->last_child = child;
}

static int is_name_char(char c) {
    return isalnum((unsigned char)c) || c == '-' || c == '_' || c == ':' || c == '.';
}

static const char *find_ci(const char *haystack, const char *needle) {
    size_t n = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        if (_strnicmp(p, needle, n) == 0) {
            return p;
        }
    }
    return NULL;
}

static int is_void_tag(const char *tag) {
    static const char *tags[] = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr", NULL
    };
    for (int i = 0; tags[i]; i++) {
        if (!strcmp(tag, tags[i])) {
            return 1;
        }
    }
    return 0;
}

static char *decode_entities(const char *src, size_t len, int collapse_ws) {
    char *out = (char *)kzalloc(len + 1);
    size_t j = 0;
    int last_space = 1;
    for (size_t i = 0; i < len; i++) {
        char c = src[i];
        if (c == '&') {
            if (i + 4 <= len && !strncmp(src + i, "&lt;", 4)) {
                c = '<'; i += 3;
            } else if (i + 4 <= len && !strncmp(src + i, "&gt;", 4)) {
                c = '>'; i += 3;
            } else if (i + 5 <= len && !strncmp(src + i, "&amp;", 5)) {
                c = '&'; i += 4;
            } else if (i + 6 <= len && !strncmp(src + i, "&quot;", 6)) {
                c = '"'; i += 5;
            } else if (i + 5 <= len && !strncmp(src + i, "&#39;", 5)) {
                c = '\''; i += 4;
            } else if (i + 6 <= len && !strncmp(src + i, "&nbsp;", 6)) {
                c = ' '; i += 5;
            }
        }
        if (collapse_ws && isspace((unsigned char)c)) {
            if (!last_space) {
                out[j++] = ' ';
                last_space = 1;
            }
            continue;
        }
        out[j++] = c;
        last_space = 0;
    }
    while (j && out[j - 1] == ' ') {
        j--;
    }
    out[j] = 0;
    return out;
}

static void set_attr(KDomNode *node, const char *name, size_t name_len, const char *value, size_t value_len) {
    char lowered[32];
    size_t n = name_len < sizeof(lowered) - 1 ? name_len : sizeof(lowered) - 1;
    for (size_t i = 0; i < n; i++) {
        lowered[i] = (char)tolower((unsigned char)name[i]);
    }
    lowered[n] = 0;

    char *decoded = decode_entities(value, value_len, 0);
    if (!strcmp(lowered, "id")) {
        kfree(node->id);
        node->id = decoded;
    } else if (!strcmp(lowered, "class")) {
        kfree(node->classes);
        node->classes = decoded;
    } else if (!strcmp(lowered, "href")) {
        kfree(node->href);
        node->href = decoded;
    } else if (!strcmp(lowered, "src")) {
        kfree(node->src);
        node->src = decoded;
    } else if (!strcmp(lowered, "style")) {
        kfree(node->style_attr);
        node->style_attr = decoded;
    } else {
        kfree(decoded);
    }
}

static const char *parse_attrs(const char *p, KDomNode *node, int *self_closing) {
    *self_closing = 0;
    while (*p && *p != '>') {
        while (isspace((unsigned char)*p)) p++;
        if (*p == '/') {
            *self_closing = 1;
            p++;
            continue;
        }
        const char *name = p;
        while (is_name_char(*p)) p++;
        size_t name_len = (size_t)(p - name);
        while (isspace((unsigned char)*p)) p++;
        const char *value = "";
        size_t value_len = 0;
        if (*p == '=') {
            p++;
            while (isspace((unsigned char)*p)) p++;
            if (*p == '\'' || *p == '"') {
                char quote = *p++;
                value = p;
                while (*p && *p != quote) p++;
                value_len = (size_t)(p - value);
                if (*p == quote) p++;
            } else {
                value = p;
                while (*p && !isspace((unsigned char)*p) && *p != '>') p++;
                value_len = (size_t)(p - value);
            }
        }
        if (name_len) {
            set_attr(node, name, name_len, value, value_len);
        }
    }
    if (*p == '>') p++;
    return p;
}

KDomNode *cyclone_parse_html(const char *html) {
    if (!html) {
        html = "";
    }
    KDomNode *doc = node_new(K_NODE_DOCUMENT, NULL, 0);
    KDomNode *stack[256];
    int top = 0;
    stack[top] = doc;
    const char *p = html;

    while (*p) {
        if (*p != '<') {
            const char *start = p;
            while (*p && *p != '<') p++;
            char *text = decode_entities(start, (size_t)(p - start), 1);
            if (*text) {
                KDomNode *node = node_new(K_NODE_TEXT, NULL, 0);
                node->text = text;
                node_append(stack[top], node);
            } else {
                kfree(text);
            }
            continue;
        }

        if (!strncmp(p, "<!--", 4)) {
            const char *end = strstr(p + 4, "-->");
            p = end ? end + 3 : p + strlen(p);
            continue;
        }
        if (p[1] == '!' || p[1] == '?') {
            const char *end = strchr(p, '>');
            p = end ? end + 1 : p + strlen(p);
            continue;
        }
        if (p[1] == '/') {
            p += 2;
            const char *name = p;
            while (is_name_char(*p)) p++;
            size_t name_len = (size_t)(p - name);
            while (*p && *p != '>') p++;
            if (*p == '>') p++;
            while (top > 0) {
                if (stack[top]->tag && strlen(stack[top]->tag) == name_len && _strnicmp(stack[top]->tag, name, name_len) == 0) {
                    top--;
                    break;
                }
                top--;
            }
            continue;
        }

        p++;
        const char *name = p;
        while (is_name_char(*p)) p++;
        size_t name_len = (size_t)(p - name);
        if (!name_len) {
            continue;
        }
        KDomNode *node = node_new(K_NODE_ELEMENT, name, name_len);
        int self_closing = 0;
        p = parse_attrs(p, node, &self_closing);
        node_append(stack[top], node);

        if (node->tag && (!strcmp(node->tag, "script") || !strcmp(node->tag, "style"))) {
            char close_tag[32];
            snprintf(close_tag, sizeof(close_tag), "</%s", node->tag);
            const char *end = find_ci(p, close_tag);
            size_t text_len = end ? (size_t)(end - p) : strlen(p);
            if (text_len) {
                KDomNode *text = node_new(K_NODE_TEXT, NULL, 0);
                text->text = kstrndup(p, text_len);
                node_append(node, text);
            }
            if (end) {
                const char *gt = strchr(end, '>');
                p = gt ? gt + 1 : end + strlen(end);
            } else {
                p += text_len;
            }
            continue;
        }

        if (!self_closing && node->tag && !is_void_tag(node->tag) && top < 255) {
            stack[++top] = node;
        }
    }

    return doc;
}

KDomNode *cyclone_parse_fragment(const char *html) {
    return cyclone_parse_html(html);
}

void cyclone_free_dom(KDomNode *node) {
    while (node) {
        KDomNode *next = node->next;
        cyclone_free_dom(node->first_child);
        kfree(node->tag);
        kfree(node->text);
        kfree(node->id);
        kfree(node->classes);
        kfree(node->href);
        kfree(node->src);
        kfree(node->style_attr);
        kfree(node);
        node = next;
    }
}

KDomNode *cyclone_find_first(KDomNode *node, const char *tag) {
    for (KDomNode *n = node; n; n = n->next) {
        if (n->type == K_NODE_ELEMENT && n->tag && !strcmp(n->tag, tag)) {
            return n;
        }
        KDomNode *child = cyclone_find_first(n->first_child, tag);
        if (child) {
            return child;
        }
    }
    return NULL;
}

static void append_text(char **buf, size_t *len, size_t *cap, const char *text) {
    if (!text || !*text) {
        return;
    }
    size_t n = strlen(text);
    if (*len + n + 2 > *cap) {
        size_t next = *cap ? *cap * 2 : 4096;
        while (*len + n + 2 > next) next *= 2;
        *buf = (char *)krealloc_owned(*buf, next);
        *cap = next;
    }
    memcpy(*buf + *len, text, n);
    *len += n;
    (*buf)[(*len)++] = ' ';
    (*buf)[*len] = 0;
}

static void collect_text_rec(KDomNode *node, char **buf, size_t *len, size_t *cap) {
    for (KDomNode *n = node; n; n = n->next) {
        if (n->type == K_NODE_ELEMENT && n->tag &&
            (!strcmp(n->tag, "script") || !strcmp(n->tag, "style") || !strcmp(n->tag, "title"))) {
            continue;
        }
        if (n->type == K_NODE_TEXT) {
            append_text(buf, len, cap, n->text);
        }
        collect_text_rec(n->first_child, buf, len, cap);
    }
}

char *cyclone_collect_text(KDomNode *node) {
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    collect_text_rec(node, &buf, &len, &cap);
    if (!buf) {
        return kstrdup("");
    }
    return buf;
}

void cyclone_extract_title(KDomNode *doc, char *out, size_t cap) {
    if (!out || cap == 0) {
        return;
    }
    out[0] = 0;
    KDomNode *title = cyclone_find_first(doc, "title");
    if (title && title->first_child && title->first_child->text) {
        strncpy(out, title->first_child->text, cap - 1);
        out[cap - 1] = 0;
    }
}

static void collect_tag_text(KDomNode *node, const char *tag, char **buf, size_t *len, size_t *cap) {
    for (KDomNode *n = node; n; n = n->next) {
        if (n->type == K_NODE_ELEMENT && n->tag && !strcmp(n->tag, tag) && n->first_child && n->first_child->text) {
            append_text(buf, len, cap, n->first_child->text);
        }
        collect_tag_text(n->first_child, tag, buf, len, cap);
    }
}

char *cyclone_extract_style_text(KDomNode *doc) {
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    collect_tag_text(doc, "style", &buf, &len, &cap);
    return buf ? buf : kstrdup("");
}

char *cyclone_extract_script_text(KDomNode *doc) {
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    collect_tag_text(doc, "script", &buf, &len, &cap);
    return buf ? buf : kstrdup("");
}
