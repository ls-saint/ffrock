#include "mihttpserver.h"

#if defined(_WIN32) && !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mongoose.h"
#include "mibase.h"
#include "mithread.h"
#include "miwebrtcbridge.h"

#ifndef MI_HTTP_THREAD_STACK_SIZE
#define MI_HTTP_THREAD_STACK_SIZE (128u * 1024u)
#endif

#if defined(_WIN32)
#define MI_HTTP_DEFAULT_LISTEN_URL "http://0.0.0.0:10010"
#else
#define MI_HTTP_DEFAULT_LISTEN_URL "http://0.0.0.0:80"
#endif

typedef struct mi_http_server_s {
    struct mg_mgr mgr;
    mi_thread_t thread;
    volatile int running;
    volatile int started;
    int thread_started;
    char listen_url[128];
} mi_http_server_t;

static mi_http_server_t g_http_server;
static mi_http_server_onvif_handler_t g_onvif_handler;
static void* g_onvif_handler_user;
static char g_onvif_path[96] = "/onvif/device_service";
#if !defined(RTOS__) && !defined(MIIPC_TXW82X_MINIMAL)
static int g_http_stop_registered;
#endif

static void http_copy_mg_str_text(const struct mg_str* src, char* dst, int dst_len)
{
    size_t n;

    if (dst == NULL || dst_len <= 0) {
        return;
    }
    dst[0] = '\0';
    if (src == NULL || src->ptr == NULL || src->len == 0) {
        return;
    }
    n = src->len;
    if (n >= (size_t)dst_len) {
        n = (size_t)dst_len - 1u;
    }
    memcpy(dst, src->ptr, n);
    dst[n] = '\0';
}

void mi_http_server_set_onvif_handler(const char* path,
                                      mi_http_server_onvif_handler_t handler,
                                      void* user_data)
{
    if (path != NULL && path[0] != '\0') {
        strncpy(g_onvif_path, path, sizeof(g_onvif_path) - 1u);
        g_onvif_path[sizeof(g_onvif_path) - 1u] = '\0';
    } else {
        strncpy(g_onvif_path, "/onvif/device_service", sizeof(g_onvif_path) - 1u);
        g_onvif_path[sizeof(g_onvif_path) - 1u] = '\0';
    }
    g_onvif_handler_user = user_data;
    g_onvif_handler = handler;
}

static char* http_copy_body(const struct mg_str* body)
{
    char* copy;

    if (body == NULL || body->ptr == NULL || body->len == 0) {
        return NULL;
    }

    if (body->len > (size_t)0x7ffffffeu) {
        return NULL;
    }
    copy = (char*)mi_mem_malloc_large((int)body->len + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, body->ptr, body->len);
    copy[body->len] = '\0';
    return copy;
}

static int http_json_hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static const char* http_json_skip_ws(const char* p, const char* end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        ++p;
    }
    return p;
}

static int http_json_key_matches(const char* p, const char* end, const char* key, const char** after)
{
    const char* k;

    if (p == NULL || end == NULL || key == NULL || p >= end || *p != '"') {
        return 0;
    }
    ++p;
    k = key;
    while (p < end) {
        char ch = *p++;
        if (ch == '"') {
            if (*k == '\0') {
                if (after != NULL) {
                    *after = p;
                }
                return 1;
            }
            return 0;
        }
        if (ch == '\\') {
            if (p >= end) {
                return 0;
            }
            ch = *p++;
        }
        if (*k == '\0' || ch != *k) {
            return 0;
        }
        ++k;
    }
    return 0;
}

static char* http_json_decode_string(const char* p, const char* end)
{
    const char* src;
    char* out;
    char* dst;

    if (p == NULL || end == NULL || p >= end || *p != '"') {
        return NULL;
    }
    ++p;
    if ((end - p) > 0x7ffffffe) {
        return NULL;
    }
    out = (char*)mi_mem_malloc_large((int)(end - p) + 1);
    if (out == NULL) {
        return NULL;
    }
    dst = out;
    for (src = p; src < end; ++src) {
        char ch = *src;
        if (ch == '"') {
            *dst = '\0';
            return out;
        }
        if (ch == '\\') {
            if (++src >= end) {
                break;
            }
            ch = *src;
            switch (ch) {
            case '"':
            case '\\':
            case '/':
                *dst++ = ch;
                break;
            case 'b':
                *dst++ = '\b';
                break;
            case 'f':
                *dst++ = '\f';
                break;
            case 'n':
                *dst++ = '\n';
                break;
            case 'r':
                *dst++ = '\r';
                break;
            case 't':
                *dst++ = '\t';
                break;
            case 'u':
                if (src + 4 < end) {
                    int h0 = http_json_hex_value(src[1]);
                    int h1 = http_json_hex_value(src[2]);
                    int h2 = http_json_hex_value(src[3]);
                    int h3 = http_json_hex_value(src[4]);
                    if (h0 >= 0 && h1 >= 0 && h2 >= 0 && h3 >= 0) {
                        unsigned int code = ((unsigned int)h0 << 12) |
                            ((unsigned int)h1 << 8) |
                            ((unsigned int)h2 << 4) |
                            (unsigned int)h3;
                        *dst++ = code < 0x80u ? (char)code : '?';
                        src += 4;
                        break;
                    }
                }
                mi_mem_free_large(out);
                return NULL;
            default:
                mi_mem_free_large(out);
                return NULL;
            }
        } else {
            *dst++ = ch;
        }
    }

    mi_mem_free_large(out);
    return NULL;
}

static char* http_json_get_string_field(const struct mg_str* body, const char* key)
{
    const char* p;
    const char* end;

    if (body == NULL || body->ptr == NULL || body->len == 0 || key == NULL) {
        return NULL;
    }
    p = body->ptr;
    end = body->ptr + body->len;
    while (p < end) {
        const char* after_key;
        const char* value;

        if (*p++ != '"') {
            continue;
        }
        if (!http_json_key_matches(p - 1, end, key, &after_key)) {
            continue;
        }
        value = http_json_skip_ws(after_key, end);
        if (value >= end || *value != ':') {
            continue;
        }
        value = http_json_skip_ws(value + 1, end);
        if (value < end && *value == '"') {
            return http_json_decode_string(value, end);
        }
    }
    return NULL;
}

static int http_json_parse_u32_text(const char* text, uint32_t* out)
{
    char* end;
    unsigned long value;

    if (text == NULL || out == NULL || text[0] == '\0') {
        return 0;
    }
    value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > 0xffffffffUL) {
        return 0;
    }
    *out = (uint32_t)value;
    return 1;
}

static int http_json_get_u32_field(const struct mg_str* body, const char* key, uint32_t* out)
{
    const char* p;
    const char* end;

    if (body == NULL || body->ptr == NULL || body->len == 0 || key == NULL || out == NULL) {
        return 0;
    }
    p = body->ptr;
    end = body->ptr + body->len;
    while (p < end) {
        const char* after_key;
        const char* value;

        if (*p++ != '"') {
            continue;
        }
        if (!http_json_key_matches(p - 1, end, key, &after_key)) {
            continue;
        }
        value = http_json_skip_ws(after_key, end);
        if (value >= end || *value != ':') {
            continue;
        }
        value = http_json_skip_ws(value + 1, end);
        if (value < end && *value == '"') {
            char* text = http_json_decode_string(value, end);
            int ok = http_json_parse_u32_text(text, out);
            mi_mem_free_large(text);
            return ok;
        }
        if (value < end && *value >= '0' && *value <= '9') {
            char text[32];
            int n = 0;
            while (value < end && *value >= '0' && *value <= '9' && n < (int)sizeof(text) - 1) {
                text[n++] = *value++;
            }
            text[n] = '\0';
            return http_json_parse_u32_text(text, out);
        }
    }
    return 0;
}

static char* http_extract_sdp(struct mg_http_message* hm)
{
    struct mg_str* content_type;
    char* sdp;

    if (hm == NULL) {
        return NULL;
    }

    content_type = mg_http_get_header(hm, "Content-Type");
    if (content_type != NULL &&
        mg_strstr(*content_type, mg_str("application/json")) != NULL) {
        sdp = http_json_get_string_field(&hm->body, "sdp");
        if (sdp != NULL) {
            return sdp;
        }
    }

    return http_copy_body(&hm->body);
}

static uint32_t http_extract_session_id(struct mg_http_message* hm)
{
    char text[32];
    unsigned long value;
    uint32_t json_value;

    if (hm == NULL) {
        return 0;
    }

    memset(text, 0, sizeof(text));
    if (mg_http_get_var(&hm->query, "sessionId", text, sizeof(text)) > 0 ||
        mg_http_get_var(&hm->query, "sid", text, sizeof(text)) > 0) {
        value = strtoul(text, NULL, 10);
        return (uint32_t)value;
    }

    if (http_json_get_u32_field(&hm->body, "sessionId", &json_value)) {
        return json_value;
    }

    if (http_json_get_u32_field(&hm->body, "sid", &json_value)) {
        return json_value;
    }

    return 0;
}

static int http_parse_source_chn_text(const char* text, int* source_chn)
{
    char* end;
    unsigned long value;

    if (text == NULL || source_chn == NULL || text[0] == '\0') {
        return 0;
    }
    value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > 0x7fffffffUL) {
        return 0;
    }
    *source_chn = (int)value;
    return 1;
}

static int http_extract_source_chn(struct mg_http_message* hm, int* source_chn)
{
    static const char* const keys[] = { "chn", "channel", "route" };
    char text[32];
    uint32_t json_value;
    int i;

    if (source_chn == NULL) {
        return -1;
    }
    *source_chn = -1;
    if (hm == NULL) {
        return 0;
    }

    for (i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); ++i) {
        int n;

        memset(text, 0, sizeof(text));
        n = mg_http_get_var(&hm->query, keys[i], text, sizeof(text));
        if (n > 0) {
            return http_parse_source_chn_text(text, source_chn) ? 1 : -1;
        }
    }

    for (i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); ++i) {
        if (http_json_get_u32_field(&hm->body, keys[i], &json_value)) {
            if (json_value > 0x7fffffffu) {
                return -1;
            }
            *source_chn = (int)json_value;
            return 1;
        }
    }

    return 0;
}

static void http_handle_webrtc_offer(struct mg_connection* c, struct mg_http_message* hm)
{
    char* offer;
    char headers[160];
    mi_webrtc_answer_t answer;
    int offer_len;
    int source_chn = -1;
    int source_chn_status;
    int ret;

    printf("MiIpc WebRTC offer received body_len=%u\n",
           hm != NULL ? (unsigned int)hm->body.len : 0u);
    fflush(stdout);
    offer = http_extract_sdp(hm);
    if (offer == NULL) {
        mg_http_reply(c,
                      400,
                      "Content-Type: application/json\r\n",
                      "{\"error\":\"missing sdp\"}\n");
        return;
    }

    memset(&answer, 0, sizeof(answer));
    offer_len = (int)strlen(offer);
    source_chn_status = http_extract_source_chn(hm, &source_chn);
    if (source_chn_status < 0) {
        mi_mem_free_large(offer);
        mg_http_reply(c,
                      400,
                      "Content-Type: application/json\r\n",
                      "{\"error\":\"invalid source channel\"}\n");
        return;
    }

    printf("MiIpc WebRTC offer extracted sdp_len=%d source_chn=%d\n",
           offer_len,
           source_chn);
    fflush(stdout);
    ret = mi_webrtc_handle_offer_for_channel(offer, offer_len, &answer, source_chn);
    printf("MiIpc WebRTC offer handler ret=%d status=%d body_len=%d\n",
           ret,
           answer.status,
           answer.body_len);
    fflush(stdout);
    mi_mem_free_large(offer);

    if (answer.body == NULL) {
        printf("MiIpc WebRTC offer failed: empty answer\n");
        fflush(stdout);
        mg_http_reply(c,
                      500,
                      "Content-Type: application/json\r\n",
                      "{\"error\":\"webrtc offer failed\"}\n");
        return;
    }

    snprintf(headers,
             sizeof(headers),
             "Content-Type: %s\r\nCache-Control: no-store\r\n",
             answer.content_type);
    mg_http_reply(c,
                  answer.status,
                  headers,
                  "%.*s",
                  answer.body_len,
                  answer.body);
    printf("MiIpc WebRTC offer answered status=%d bytes=%d\n",
           answer.status,
           answer.body_len);
    fflush(stdout);
    mi_webrtc_answer_free(&answer);
}

static void http_handle_webrtc_candidates(struct mg_connection* c, struct mg_http_message* hm)
{
    uint32_t session_id = http_extract_session_id(hm);
    char* json;
    int ret;
    if (session_id == 0) {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n", "{\"error\":\"missing sessionId\"}\n");
        return;
    }
    json = (char*)mi_mem_malloc_large(32768);
    if (json == NULL) {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n", "{\"error\":\"memory\"}\n");
        return;
    }
    ret = mi_webrtc_candidates_json(session_id, json, 32768);
    if (ret > 0) mg_http_reply(c, 200, "Content-Type: application/json\r\nCache-Control: no-store\r\n", "%s", json);
    else mg_http_reply(c, ret == -2 ? 404 : 500, "Content-Type: application/json\r\n", "{\"error\":\"candidates unavailable\"}\n");
    mi_mem_free_large(json);
}

static void http_handle_webrtc_candidate(struct mg_connection* c, struct mg_http_message* hm)
{
    struct mg_str* content_type;
    char* candidate = NULL;
    uint32_t session_id;
    int candidate_len;
    int ret;

    if (hm == NULL) {
        mg_http_reply(c,
                      400,
                      "Content-Type: application/json\r\n",
                      "{\"error\":\"bad request\"}\n");
        return;
    }

    session_id = http_extract_session_id(hm);
    printf("MiIpc WebRTC candidate received session=%u body_len=%u\n",
           (unsigned int)session_id,
           (unsigned int)hm->body.len);
    fflush(stdout);

    content_type = mg_http_get_header(hm, "Content-Type");
    if (content_type != NULL &&
        mg_strstr(*content_type, mg_str("application/json")) != NULL) {
        candidate = http_json_get_string_field(&hm->body, "candidate");
    } else {
        candidate = http_copy_body(&hm->body);
    }

    if (candidate == NULL) {
        mi_mem_free_large(candidate);
        printf("MiIpc WebRTC candidate missing session=%u\n",
               (unsigned int)session_id);
        fflush(stdout);
        mg_http_reply(c,
                      400,
                      "Content-Type: application/json\r\n",
                      "{\"error\":\"missing candidate\"}\n");
        return;
    }

    candidate_len = (int)strlen(candidate);
    ret = mi_webrtc_add_candidate(session_id, candidate, candidate_len);
    printf("MiIpc WebRTC candidate handled session=%u len=%d ret=%d\n",
           (unsigned int)session_id,
           candidate_len,
           ret);
    fflush(stdout);
    mi_mem_free_large(candidate);
    if (ret == -2) {
        mg_http_reply(c,
                      200,
                      "Content-Type: application/json\r\nCache-Control: no-store\r\n",
                      "{\"ok\":true,\"ignored\":true,\"code\":%d}\n",
                      ret);
        return;
    }
    if (ret != 0) {
        mg_http_reply(c,
                      400,
                      "Content-Type: application/json\r\n",
                      "{\"error\":\"candidate failed\",\"code\":%d}\n",
                      ret);
        return;
    }

    mg_http_reply(c,
                  200,
                  "Content-Type: application/json\r\nCache-Control: no-store\r\n",
                  "{\"ok\":true}\n");
}

static void http_handle_webrtc_close(struct mg_connection* c, struct mg_http_message* hm)
{
    uint32_t session_id;
    int ret;

    session_id = http_extract_session_id(hm);
    printf("MiIpc WebRTC close received session=%u body_len=%u\n",
           (unsigned int)session_id,
           hm != NULL ? (unsigned int)hm->body.len : 0u);
    fflush(stdout);
    if (session_id == 0) {
        mg_http_reply(c,
                      400,
                      "Content-Type: application/json\r\nCache-Control: no-store\r\n",
                      "{\"error\":\"missing sessionId\"}\n");
        return;
    }

    ret = mi_webrtc_close_session(session_id);
    printf("MiIpc WebRTC close handled session=%u ret=%d\n",
           (unsigned int)session_id,
           ret);
    fflush(stdout);
    if (ret != 0) {
        mg_http_reply(c,
                      400,
                      "Content-Type: application/json\r\nCache-Control: no-store\r\n",
                      "{\"error\":\"close failed\",\"code\":%d}\n",
                      ret);
        return;
    }

    mg_http_reply(c,
                  200,
                  "Content-Type: application/json\r\nCache-Control: no-store\r\n",
                  "{\"ok\":true}\n");
}

static int http_path_matches_onvif(struct mg_http_message* hm)
{
    if (hm == NULL || g_onvif_handler == NULL || g_onvif_path[0] == '\0') {
        return 0;
    }
    return mg_http_match_uri(hm, g_onvif_path);
}

static void http_handle_onvif(struct mg_connection* c, struct mg_http_message* hm)
{
    char method[16];
    char uri[160];
    char headers[160];
    char* response;
    const char* content_type;
    int status;
    int ret;

    if (c == NULL || hm == NULL || g_onvif_handler == NULL) {
        return;
    }
    response = (char*)mi_mem_malloc_large(16384);
    if (response == NULL) {
        mg_http_reply(c,
                      500,
                      "Content-Type: text/plain\r\nCache-Control: no-store\r\n",
                      "no memory\n");
        return;
    }
    http_copy_mg_str_text(&hm->method, method, sizeof(method));
    http_copy_mg_str_text(&hm->uri, uri, sizeof(uri));
    status = 200;
    content_type = "application/soap+xml; charset=utf-8";
    ret = g_onvif_handler(method,
                          uri,
                          hm->body.ptr,
                          (int)hm->body.len,
                          response,
                          16384,
                          &status,
                          &content_type,
                          g_onvif_handler_user);
    if (ret < 0) {
        mg_http_reply(c,
                      500,
                      "Content-Type: text/plain\r\nCache-Control: no-store\r\n",
                      "onvif failed\n");
        mi_mem_free_large(response);
        return;
    }
    snprintf(headers,
             sizeof(headers),
             "Content-Type: %s\r\nCache-Control: no-store\r\n",
             content_type != NULL ? content_type : "application/soap+xml; charset=utf-8");
    mg_http_reply(c, status, headers, "%.*s", ret, response);
    mi_mem_free_large(response);
}

static void http_event_handler(struct mg_connection* c, int ev, void* ev_data)
{
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;

        if (http_path_matches_onvif(hm)) {
            http_handle_onvif(c, hm);
        } else if (mg_http_match_uri(hm, "/health")) {
            mg_http_reply(c,
                          200,
                          "Content-Type: application/json\r\n",
                          "{\"status\":\"ok\"}\n");
        } else if (mg_http_match_uri(hm, "/api/webrtc/stats")) {
            char* json = (char*)mi_mem_malloc_large(16384);
            uint32_t session_id = http_extract_session_id(hm);
            int n;
            if (json == NULL) {
                mg_http_reply(c,
                              500,
                              "Content-Type: application/json\r\nCache-Control: no-store\r\n",
                              "{\"error\":\"no memory\"}\n");
                return;
            }
            n = mi_webrtc_stats_json(session_id, json, 16384);
            if (n < 0 || n >= 16384) {
                mg_http_reply(c,
                              500,
                              "Content-Type: application/json\r\nCache-Control: no-store\r\n",
                              "{\"error\":\"stats failed\"}\n");
            } else {
                mg_http_reply(c,
                              200,
                              "Content-Type: application/json\r\nCache-Control: no-store\r\n",
                              "%s",
                              json);
            }
            mi_mem_free_large(json);
        } else if (mg_http_match_uri(hm, "/") || mg_http_match_uri(hm, "/webrtc")) {
            mg_http_reply(c,
                          200,
                          "Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\n",
                          "%s",
                          mi_webrtc_page_html());
        } else if (mg_http_match_uri(hm, "/api/webrtc/offer")) {
            if (mg_vcmp(&hm->method, "POST") != 0) {
                mg_http_reply(c,
                              405,
                              "Content-Type: application/json\r\nAllow: POST\r\n",
                              "{\"error\":\"method not allowed\"}\n");
            } else {
                http_handle_webrtc_offer(c, hm);
            }
        } else if (mg_http_match_uri(hm, "/api/webrtc/candidates")) {
            if (mg_vcmp(&hm->method, "GET") == 0) http_handle_webrtc_candidates(c, hm);
            else mg_http_reply(c, 405, "Content-Type: application/json\r\nAllow: GET\r\n", "{\"error\":\"method not allowed\"}\n");
        } else if (mg_http_match_uri(hm, "/api/webrtc/candidate")) {
            if (mg_vcmp(&hm->method, "POST") != 0) {
                mg_http_reply(c,
                              405,
                              "Content-Type: application/json\r\nAllow: POST\r\n",
                              "{\"error\":\"method not allowed\"}\n");
            } else {
                http_handle_webrtc_candidate(c, hm);
            }
        } else if (mg_http_match_uri(hm, "/api/webrtc/close")) {
            if (mg_vcmp(&hm->method, "POST") != 0) {
                mg_http_reply(c,
                              405,
                              "Content-Type: application/json\r\nAllow: POST\r\n",
                              "{\"error\":\"method not allowed\"}\n");
            } else {
                http_handle_webrtc_close(c, hm);
            }
        } else {
            mg_http_reply(c,
                          404,
                          "Content-Type: text/plain\r\n",
                          "not found\n");
        }
    }
}

static void* http_server_thread(void* arg)
{
    mi_http_server_t* server = (mi_http_server_t*)arg;

    mg_mgr_init(&server->mgr);
    if (mg_http_listen(&server->mgr, server->listen_url, http_event_handler, server) == NULL) {
        printf("MiIpc http listen failed: %s\n", server->listen_url);
        server->running = 0;
        server->started = 0;
        mg_mgr_free(&server->mgr);
        return NULL;
    }

    server->started = 1;
    printf("MiIpc http server listen on %s\n", server->listen_url);
    while (server->running) {
        mg_mgr_poll(&server->mgr, 100);
    }

    mg_mgr_free(&server->mgr);
    server->started = 0;
    printf("MiIpc http server stopped\n");
    fflush(stdout);
    return NULL;
}

int mi_http_server_start(const char* listen_url)
{
    thread_config_t thread_config;
    const char* url = listen_url != NULL ? listen_url : MI_HTTP_DEFAULT_LISTEN_URL;

    if (g_http_server.running) {
        return 0;
    }
    if (g_http_server.thread_started) {
        (void)mi_thread_join(&g_http_server.thread, NULL, 3000);
        memset(&g_http_server, 0, sizeof(g_http_server));
    }

    memset(&g_http_server, 0, sizeof(g_http_server));
    strncpy(g_http_server.listen_url, url, sizeof(g_http_server.listen_url) - 1);
    g_http_server.listen_url[sizeof(g_http_server.listen_url) - 1] = '\0';

    thread_config.name = "MiIpcHttp";
    thread_config.priority = 0;
    thread_config.stack_size = MI_HTTP_THREAD_STACK_SIZE;
    thread_config.detached = 0;

    g_http_server.running = 1;
    if (mi_thread_create(&g_http_server.thread, http_server_thread, &g_http_server, &thread_config) != MI_THREAD_OK) {
        g_http_server.running = 0;
        return -1;
    }
    g_http_server.thread_started = 1;
#if !defined(RTOS__) && !defined(MIIPC_TXW82X_MINIMAL)
    if (!g_http_stop_registered) {
        atexit(mi_http_server_stop);
        g_http_stop_registered = 1;
    }
#endif
    return 0;
}

void mi_http_server_stop(void)
{
    mi_thread_err_t join_ret;

    if (!g_http_server.running && !g_http_server.thread_started) {
        return;
    }
    mi_webrtc_shutdown();
    g_http_server.running = 0;
    join_ret = mi_thread_join(&g_http_server.thread, NULL, 3000);
    if (join_ret != MI_THREAD_OK) {
        printf("MiIpc http server thread join failed: %d\n", (int)join_ret);
        return;
    }
    g_http_server.thread_started = 0;
    memset(&g_http_server, 0, sizeof(g_http_server));
}

int mi_http_server_is_running(void)
{
    return g_http_server.started;
}
