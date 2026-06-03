#include "kerosene.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

static char *extract_quoted(const char *p) {
    p = skip_ws(p);
    if (*p != '\'' && *p != '"') {
        return NULL;
    }
    char quote = *p++;
    const char *start = p;
    size_t cap = strlen(p) + 1;
    char *out = (char *)kzalloc(cap);
    size_t j = 0;
    while (*p && *p != quote) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'n') out[j++] = '\n';
            else if (*p == 't') out[j++] = '\t';
            else out[j++] = *p;
            p++;
            continue;
        }
        out[j++] = *p++;
    }
    out[j] = 0;
    (void)start;
    return out;
}

static KDomNode *body_node(KDomNode *doc) {
    KDomNode *body = cyclone_find_first(doc, "body");
    return body ? body : doc;
}

static void append_fragment(KDomNode *parent, const char *html) {
    KDomNode *frag = cyclone_parse_fragment(html);
    KDomNode *child = frag->first_child;
    while (child) {
        KDomNode *next = child->next;
        child->next = NULL;
        child->parent = parent;
        if (!parent->first_child) {
            parent->first_child = child;
        } else {
            parent->last_child->next = child;
        }
        parent->last_child = child;
        child = next;
    }
    frag->first_child = NULL;
    cyclone_free_dom(frag);
}

static void replace_body(KDomNode *doc, const char *html) {
    KDomNode *body = body_node(doc);
    cyclone_free_dom(body->first_child);
    body->first_child = NULL;
    body->last_child = NULL;
    append_fragment(body, html);
}

static void run_statement(KDomNode *doc, const char *stmt, const char *base_url, char *status, size_t status_cap) {
    const char *p = skip_ws(stmt);
    if (!*p) {
        return;
    }

    if (!_strnicmp(p, "document.write", 14)) {
        const char *open = strchr(p, '(');
        if (open) {
            char *html = extract_quoted(open + 1);
            if (html) {
                append_fragment(body_node(doc), html);
                kfree(html);
            }
        }
        return;
    }

    if (!_strnicmp(p, "document.body.innerHTML", 23)) {
        const char *eq = strchr(p, '=');
        if (eq) {
            char *html = extract_quoted(eq + 1);
            if (html) {
                replace_body(doc, html);
                kfree(html);
            }
        }
        return;
    }

    if (!_strnicmp(p, "document.title", 14)) {
        const char *eq = strchr(p, '=');
        if (eq) {
            char *title = extract_quoted(eq + 1);
            if (title) {
                KDomNode *t = cyclone_find_first(doc, "title");
                if (t && t->first_child && t->first_child->type == K_NODE_TEXT) {
                    kfree(t->first_child->text);
                    t->first_child->text = title;
                } else {
                    kfree(title);
                }
            }
        }
        return;
    }

    if (!_strnicmp(p, "console.log", 11)) {
        const char *open = strchr(p, '(');
        if (open) {
            char *msg = extract_quoted(open + 1);
            if (msg) {
                snprintf(status, status_cap, "console: %.180s", msg);
                kfree(msg);
            }
        }
        return;
    }

    if (!_strnicmp(p, "fetch", 5)) {
        const char *open = strchr(p, '(');
        char *url = open ? extract_quoted(open + 1) : NULL;
        if (url) {
            if (adblock_should_block(url)) {
                snprintf(status, status_cap, "Spark fetch bloqueado por adblock: %.150s", url);
            } else {
                KHttpResponse response;
                char err[160] = {0};
                if (http_get(url, &response, err, sizeof(err))) {
                    snprintf(status, status_cap, "Spark fetch %d: %.120s", response.status, response.final_url);
                    http_response_free(&response);
                } else {
                    snprintf(status, status_cap, "Spark fetch falhou: %.150s", err);
                }
            }
            kfree(url);
        }
        return;
    }

    if (strstr(p, "querySelector") && strstr(p, "textContent")) {
        const char *eq = strchr(p, '=');
        char *text = eq ? extract_quoted(eq + 1) : NULL;
        if (text) {
            append_fragment(body_node(doc), text);
            kfree(text);
        }
    }

    (void)base_url;
}

void spark_run_document(KDomNode *doc, const char *base_url, char *status, size_t status_cap) {
    char *script = cyclone_extract_script_text(doc);
    if (!script || !*script) {
        kfree(script);
        return;
    }
    int statements = spark_compile_count_statements(script);
    int budget = spark_jit_loop_budget(script);
    if (statements > budget) {
        snprintf(status, status_cap, "Spark recusou script grande (%d statements, budget %d)", statements, budget);
        kfree(script);
        return;
    }

    char *cursor = script;
    while (*cursor) {
        char *end = strchr(cursor, ';');
        if (!end) {
            run_statement(doc, cursor, base_url, status, status_cap);
            break;
        }
        *end = 0;
        run_statement(doc, cursor, base_url, status, status_cap);
        cursor = end + 1;
    }
    kfree(script);
}
