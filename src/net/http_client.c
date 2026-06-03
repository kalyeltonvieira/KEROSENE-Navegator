#include "kerosene.h"

#include <winhttp.h>
#include <stdio.h>
#include <string.h>

static void set_err(char *err, size_t cap, const char *msg) {
    if (err && cap) {
        strncpy(err, msg, cap - 1);
        err[cap - 1] = 0;
    }
}

static char *wide_to_utf8(const wchar_t *w) {
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *out = (char *)kzalloc((size_t)len + 1);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, len, NULL, NULL);
    return out;
}

bool http_get(const char *url, KHttpResponse *out, char *err, size_t err_cap) {
    memset(out, 0, sizeof(*out));
    if (!url || !*url) {
        set_err(err, err_cap, "URL vazia");
        return false;
    }
    if (adblock_should_block(url)) {
        set_err(err, err_cap, "request bloqueado pelo adblock nativo");
        return false;
    }
    if (cache_get(url, out)) {
        return true;
    }

    wchar_t *wurl = platform_utf8_to_wide(url, NULL);
    if (!wurl) {
        set_err(err, err_cap, "falha convertendo URL");
        return false;
    }

    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = (DWORD)-1;
    uc.dwHostNameLength = (DWORD)-1;
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) {
        kfree(wurl);
        set_err(err, err_cap, "URL invalida");
        return false;
    }

    int secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    wchar_t host[512];
    wchar_t path[2048];
    size_t host_len = uc.dwHostNameLength < 511 ? uc.dwHostNameLength : 511;
    memcpy(host, uc.lpszHostName, host_len * sizeof(wchar_t));
    host[host_len] = 0;
    size_t path_len = 0;
    if (uc.dwUrlPathLength) {
        size_t n = uc.dwUrlPathLength < 1600 ? uc.dwUrlPathLength : 1600;
        memcpy(path + path_len, uc.lpszUrlPath, n * sizeof(wchar_t));
        path_len += n;
    } else {
        path[path_len++] = L'/';
    }
    if (uc.dwExtraInfoLength && path_len < 2040) {
        size_t n = uc.dwExtraInfoLength < 2040 - path_len ? uc.dwExtraInfoLength : 2040 - path_len;
        memcpy(path + path_len, uc.lpszExtraInfo, n * sizeof(wchar_t));
        path_len += n;
    }
    path[path_len] = 0;

    HINTERNET session = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) Kerosene/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        kfree(wurl);
        set_err(err, err_cap, "WinHttpOpen falhou");
        return false;
    }
    WinHttpSetTimeouts(session, 3000, 3000, 5000, 5000);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        kfree(wurl);
        set_err(err, err_cap, "WinHttpConnect falhou");
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        kfree(wurl);
        set_err(err, err_cap, "WinHttpOpenRequest falhou");
        return false;
    }

    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy));
    WinHttpAddRequestHeaders(request, L"Accept: text/html,application/xhtml+xml,text/plain,*/*\r\nAccept-Encoding: identity\r\n", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    BOOL ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(request, NULL);
    if (!ok) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        kfree(wurl);
        set_err(err, err_cap, "request HTTP falhou");
        return false;
    }

    DWORD status = 0, status_size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &status_size, NULL);
    out->status = (int)status;

    wchar_t content_type[128];
    DWORD ct_size = sizeof(content_type);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE, NULL, content_type, &ct_size, NULL)) {
        char *ct = wide_to_utf8(content_type);
        strncpy(out->content_type, ct, sizeof(out->content_type) - 1);
        kfree(ct);
    }

    size_t cap = 65536;
    out->body = (char *)kzalloc(cap + 1);
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) {
            break;
        }
        if (out->body_len + avail + 1 > 8 * 1024 * 1024) {
            set_err(err, err_cap, "resposta maior que 8MB");
            break;
        }
        if (out->body_len + avail + 1 > cap) {
            while (out->body_len + avail + 1 > cap) cap *= 2;
            out->body = (char *)krealloc_owned(out->body, cap + 1);
        }
        DWORD read = 0;
        if (!WinHttpReadData(request, out->body + out->body_len, avail, &read) || read == 0) {
            break;
        }
        out->body_len += read;
        out->body[out->body_len] = 0;
    }

    strncpy(out->final_url, url, sizeof(out->final_url) - 1);
    if (out->body && out->body_len) {
        cache_put(url, out);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    kfree(wurl);
    return out->body != NULL;
}

void http_response_free(KHttpResponse *response) {
    if (!response) return;
    kfree(response->body);
    memset(response, 0, sizeof(*response));
}
