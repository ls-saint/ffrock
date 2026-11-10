#include "mibase.h"
#include "socket/mimdns.h"

#include <stdio.h>
#include <string.h>

#if MI_BASE_OS_WINDOWS || MI_BASE_OS_POSIX || MI_BASE_OS_VXWORKS || defined(MI_BASE_HAS_BSD_SOCKET)
#define MI_MDNS_USE_BSD_SOCKET 1
#else
#define MI_MDNS_USE_BSD_SOCKET 0
#endif

#define MI_MDNS_IPV4_ADDR "224.0.0.251"
#define MI_MDNS_SERVICE_ENUM "_services._dns-sd._udp.local"
#define MI_MDNS_PACKET_MAX 1500u
#define MI_MDNS_MAX_SERVICES 8
#define MI_MDNS_MAX_RESULTS 16
#define MI_MDNS_MAX_ADDRS 8
#define MI_MDNS_DEFAULT_TTL 120u

#if MI_MDNS_USE_BSD_SOCKET

#if MI_BASE_OS_WINDOWS
#include <Winsock2.h>
#include <Windows.h>
#include <ws2tcpip.h>
#if defined(_MSC_VER)
#pragma comment(lib, "ws2_32.lib")
#endif
typedef int socklen_t;
typedef SOCKET mi_mdns_socket_t;
#define MI_MDNS_INVALID_SOCKET INVALID_SOCKET
#define MI_MDNS_SOCKET_ERROR SOCKET_ERROR
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int mi_mdns_socket_t;
#define MI_MDNS_INVALID_SOCKET (-1)
#define MI_MDNS_SOCKET_ERROR (-1)
#endif

#include "mimutex.h"
#include "mithread.h"
typedef struct MiMdnsService {
    char instance_name[64];
    char service_type[96];
    char protocol[8];
    char service_name[MI_MDNS_MAX_NAME];
    char full_name[MI_MDNS_MAX_NAME];
    char host_name[128];
    char ipv4[64];
    char txt[MI_MDNS_MAX_TXT];
    uint16_t port;
    uint32_t ttl;
} MiMdnsService;

typedef struct MiMdnsWriter {
    uint8_t* data;
    size_t len;
    size_t cap;
} MiMdnsWriter;

typedef struct MiMdnsHostAddr {
    char host_name[128];
    char addr[64];
    int family;
} MiMdnsHostAddr;

typedef struct MiMdnsImpl {
    mi_Mdns api;
    mi_mdns_socket_t sock;
    mi_thread_t thread;
    volatile int running;
    mi_Mutex* lock;
    mi_mdns_result_cb on_result;
    void* user_data;
    char host_name[128];
    char ipv4[64];
    uint32_t flags;
    MiMdnsService services[MI_MDNS_MAX_SERVICES];
    int service_count;
} MiMdnsImpl;

static int mdns_start_impl(mi_Mdns* mdns);
static void mdns_stop_impl(mi_Mdns* mdns);
static int mdns_add_service_impl(mi_Mdns* mdns, const mi_MdnsServiceConfig* service);
static void mdns_clear_services_impl(mi_Mdns* mdns);
static int mdns_query_impl(mi_Mdns* mdns, const char* service_type, const char* protocol);
static int mdns_announce_impl(mi_Mdns* mdns);

static int mdns_stricmp_ascii(const char* a, const char* b)
{
    unsigned char ca;
    unsigned char cb;

    if (a == NULL || b == NULL) {
        return a == b ? 0 : (a == NULL ? -1 : 1);
    }

    while (*a != '\0' && *b != '\0') {
        ca = (unsigned char)*a;
        cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (unsigned char)(ca + ('a' - 'A'));
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (unsigned char)(cb + ('a' - 'A'));
        }
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
        ++a;
        ++b;
    }

    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int mdns_name_equal(const char* a, const char* b)
{
    return mdns_stricmp_ascii(a, b) == 0;
}

static size_t mdns_strnlen_local(const char* text, size_t max_len)
{
    size_t i;

    if (text == NULL) {
        return 0;
    }
    for (i = 0; i < max_len && text[i] != '\0'; ++i) {
    }
    return i;
}

static void mdns_copy(char* dst, size_t dst_len, const char* src)
{
    size_t len;

    if (dst == NULL || dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    if (src == NULL) {
        return;
    }
    len = mdns_strnlen_local(src, dst_len - 1u);
    if (len > 0) {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
}

static int mdns_ends_with_name(const char* name, const char* suffix)
{
    size_t name_len;
    size_t suffix_len;

    if (name == NULL || suffix == NULL) {
        return 0;
    }
    name_len = strlen(name);
    suffix_len = strlen(suffix);
    if (name_len < suffix_len) {
        return 0;
    }
    return mdns_name_equal(name + name_len - suffix_len, suffix);
}

static int mdns_has_local_suffix(const char* name)
{
    return mdns_ends_with_name(name, ".local") || mdns_name_equal(name, "local");
}

static int mdns_format(char* out, size_t out_len, const char* fmt, const char* a, const char* b)
{
    int ret;

    if (out == NULL || out_len == 0 || fmt == NULL) {
        return 0;
    }
    ret = snprintf(out, out_len, fmt, a != NULL ? a : "", b != NULL ? b : "");
    if (ret < 0 || (size_t)ret >= out_len) {
        out[0] = '\0';
        return 0;
    }
    return 1;
}

static int mdns_make_host_name(const char* host, char* out, size_t out_len)
{
    if (host == NULL || host[0] == '\0') {
        host = "mibase";
    }
    if (mdns_has_local_suffix(host)) {
        mdns_copy(out, out_len, host);
        return out != NULL && out[0] != '\0';
    }
    return mdns_format(out, out_len, "%s.local", host, NULL);
}

static void mdns_normalize_protocol(const char* protocol, char* out, size_t out_len)
{
    if (protocol == NULL || protocol[0] == '\0') {
        protocol = "_tcp";
    }
    if (protocol[0] == '_') {
        mdns_copy(out, out_len, protocol);
    } else {
        mdns_format(out, out_len, "_%s", protocol, NULL);
    }
}

static int mdns_make_service_name(const char* service_type, const char* protocol, char* out, size_t out_len)
{
    char proto[8];

    if (service_type == NULL || service_type[0] == '\0') {
        return 0;
    }
    if (mdns_has_local_suffix(service_type)) {
        mdns_copy(out, out_len, service_type);
        return out != NULL && out[0] != '\0';
    }
    if (strstr(service_type, "._tcp") != NULL || strstr(service_type, "._udp") != NULL) {
        return mdns_format(out, out_len, "%s.local", service_type, NULL);
    }

    mdns_normalize_protocol(protocol, proto, sizeof(proto));
    if (service_type[0] == '_') {
        return mdns_format(out, out_len, "%s.%s.local", service_type, proto);
    }
    return mdns_format(out, out_len, "_%s.%s.local", service_type, proto);
}

static int mdns_make_full_name(const char* instance, const char* service_name, char* out, size_t out_len)
{
    if (instance == NULL || instance[0] == '\0' || service_name == NULL || service_name[0] == '\0') {
        return 0;
    }
    return mdns_format(out, out_len, "%s.%s", instance, service_name);
}

static void mdns_extract_instance(const char* full_name, const char* service_name, char* out, size_t out_len)
{
    size_t full_len;
    size_t service_len;
    size_t copy_len;

    if (out == NULL || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (full_name == NULL || service_name == NULL) {
        return;
    }
    full_len = strlen(full_name);
    service_len = strlen(service_name);
    if (full_len > service_len + 1u &&
        full_name[full_len - service_len - 1u] == '.' &&
        mdns_name_equal(full_name + full_len - service_len, service_name)) {
        copy_len = full_len - service_len - 1u;
        if (copy_len >= out_len) {
            copy_len = out_len - 1u;
        }
        memcpy(out, full_name, copy_len);
        out[copy_len] = '\0';
        return;
    }
    mdns_copy(out, out_len, full_name);
}

static uint16_t mdns_read_u16(const uint8_t* data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t mdns_read_u32(const uint8_t* data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
}

static int mdns_write_u8(MiMdnsWriter* writer, uint8_t value)
{
    if (writer == NULL || writer->len + 1u > writer->cap) {
        return 0;
    }
    writer->data[writer->len++] = value;
    return 1;
}

static int mdns_write_u16(MiMdnsWriter* writer, uint16_t value)
{
    if (writer == NULL || writer->len + 2u > writer->cap) {
        return 0;
    }
    writer->data[writer->len++] = (uint8_t)(value >> 8);
    writer->data[writer->len++] = (uint8_t)(value & 0xFFu);
    return 1;
}

static int mdns_write_u32(MiMdnsWriter* writer, uint32_t value)
{
    if (writer == NULL || writer->len + 4u > writer->cap) {
        return 0;
    }
    writer->data[writer->len++] = (uint8_t)(value >> 24);
    writer->data[writer->len++] = (uint8_t)((value >> 16) & 0xFFu);
    writer->data[writer->len++] = (uint8_t)((value >> 8) & 0xFFu);
    writer->data[writer->len++] = (uint8_t)(value & 0xFFu);
    return 1;
}

static int mdns_write_bytes(MiMdnsWriter* writer, const void* data, size_t len)
{
    if (writer == NULL || (len > 0 && data == NULL) || writer->len + len > writer->cap) {
        return 0;
    }
    if (len > 0) {
        memcpy(writer->data + writer->len, data, len);
        writer->len += len;
    }
    return 1;
}

static void mdns_patch_u16(MiMdnsWriter* writer, size_t offset, uint16_t value)
{
    if (writer == NULL || offset + 1u >= writer->cap) {
        return;
    }
    writer->data[offset] = (uint8_t)(value >> 8);
    writer->data[offset + 1u] = (uint8_t)(value & 0xFFu);
}

static int mdns_write_name(MiMdnsWriter* writer, const char* name)
{
    const char* label;
    const char* dot;
    size_t label_len;

    if (writer == NULL || name == NULL || name[0] == '\0') {
        return 0;
    }

    label = name;
    while (*label != '\0') {
        dot = strchr(label, '.');
        if (dot == NULL) {
            label_len = strlen(label);
        } else {
            label_len = (size_t)(dot - label);
        }
        if (label_len == 0 || label_len > 63u) {
            return 0;
        }
        if (!mdns_write_u8(writer, (uint8_t)label_len) || !mdns_write_bytes(writer, label, label_len)) {
            return 0;
        }
        if (dot == NULL) {
            break;
        }
        label = dot + 1;
    }

    return mdns_write_u8(writer, 0);
}

static int mdns_read_name(const uint8_t* packet, size_t packet_len, size_t* offset, char* out, size_t out_len)
{
    size_t pos;
    size_t out_pos = 0;
    size_t next_offset = 0;
    int jumped = 0;
    int jumps = 0;

    if (packet == NULL || offset == NULL || out == NULL || out_len == 0 || *offset >= packet_len) {
        return 0;
    }

    pos = *offset;
    out[0] = '\0';
    while (pos < packet_len) {
        uint8_t len = packet[pos++];
        if ((len & 0xC0u) == 0xC0u) {
            uint16_t ptr;
            if (pos >= packet_len) {
                return 0;
            }
            ptr = (uint16_t)(((uint16_t)(len & 0x3Fu) << 8) | packet[pos++]);
            if (ptr >= packet_len || ++jumps > 16) {
                return 0;
            }
            if (!jumped) {
                next_offset = pos;
                jumped = 1;
            }
            pos = ptr;
            continue;
        }
        if ((len & 0xC0u) != 0) {
            return 0;
        }
        if (len == 0) {
            if (!jumped) {
                next_offset = pos;
            }
            break;
        }
        if (pos + len > packet_len) {
            return 0;
        }
        if (out_pos != 0) {
            if (out_pos + 1u >= out_len) {
                return 0;
            }
            out[out_pos++] = '.';
        }
        if (out_pos + len >= out_len) {
            return 0;
        }
        memcpy(out + out_pos, packet + pos, len);
        out_pos += len;
        out[out_pos] = '\0';
        pos += len;
    }

    if (next_offset == 0) {
        return 0;
    }
    *offset = next_offset;
    return 1;
}

static int mdns_append_question(MiMdnsWriter* writer, const char* name, uint16_t type)
{
    size_t start;

    if (writer == NULL) {
        return 0;
    }
    start = writer->len;
    if (mdns_write_name(writer, name) &&
        mdns_write_u16(writer, type) &&
        mdns_write_u16(writer, 1u)) {
        return 1;
    }
    writer->len = start;
    return 0;
}

static int mdns_append_record_header(MiMdnsWriter* writer, const char* name, uint16_t type, uint16_t klass, uint32_t ttl, size_t* rdlen_offset)
{
    if (!mdns_write_name(writer, name) ||
        !mdns_write_u16(writer, type) ||
        !mdns_write_u16(writer, klass) ||
        !mdns_write_u32(writer, ttl)) {
        return 0;
    }
    *rdlen_offset = writer->len;
    return mdns_write_u16(writer, 0);
}

static int mdns_finish_record(MiMdnsWriter* writer, size_t rdlen_offset, size_t data_start)
{
    size_t rdlen;

    if (writer == NULL || writer->len < data_start || writer->len - data_start > 0xFFFFu) {
        return 0;
    }
    rdlen = writer->len - data_start;
    mdns_patch_u16(writer, rdlen_offset, (uint16_t)rdlen);
    return 1;
}

static int mdns_append_ptr(MiMdnsWriter* writer, const char* owner, const char* target, uint32_t ttl)
{
    size_t start;
    size_t rdlen_offset;
    size_t data_start;

    if (writer == NULL) {
        return 0;
    }
    start = writer->len;
    if (!mdns_append_record_header(writer, owner, MI_MDNS_RECORD_PTR, 1u, ttl, &rdlen_offset)) {
        writer->len = start;
        return 0;
    }
    data_start = writer->len;
    if (!mdns_write_name(writer, target)) {
        writer->len = start;
        return 0;
    }
    if (!mdns_finish_record(writer, rdlen_offset, data_start)) {
        writer->len = start;
        return 0;
    }
    return 1;
}

static int mdns_append_srv(MiMdnsWriter* writer, const char* owner, const char* host, uint16_t port, uint32_t ttl)
{
    size_t start;
    size_t rdlen_offset;
    size_t data_start;

    if (writer == NULL) {
        return 0;
    }
    start = writer->len;
    if (!mdns_append_record_header(writer, owner, MI_MDNS_RECORD_SRV, 0x8001u, ttl, &rdlen_offset)) {
        writer->len = start;
        return 0;
    }
    data_start = writer->len;
    if (!mdns_write_u16(writer, 0) ||
        !mdns_write_u16(writer, 0) ||
        !mdns_write_u16(writer, port) ||
        !mdns_write_name(writer, host)) {
        writer->len = start;
        return 0;
    }
    if (!mdns_finish_record(writer, rdlen_offset, data_start)) {
        writer->len = start;
        return 0;
    }
    return 1;
}

static int mdns_append_txt(MiMdnsWriter* writer, const char* owner, const char* txt, uint32_t ttl)
{
    size_t start;
    size_t rdlen_offset;
    size_t data_start;
    size_t txt_len;

    if (writer == NULL) {
        return 0;
    }
    start = writer->len;
    if (!mdns_append_record_header(writer, owner, MI_MDNS_RECORD_TXT, 0x8001u, ttl, &rdlen_offset)) {
        writer->len = start;
        return 0;
    }
    data_start = writer->len;
    if (txt == NULL || txt[0] == '\0') {
        if (!mdns_write_u8(writer, 0)) {
            writer->len = start;
            return 0;
        }
    } else {
        txt_len = strlen(txt);
        while (txt_len > 0) {
            size_t chunk = txt_len > 255u ? 255u : txt_len;
            if (!mdns_write_u8(writer, (uint8_t)chunk) || !mdns_write_bytes(writer, txt, chunk)) {
                writer->len = start;
                return 0;
            }
            txt += chunk;
            txt_len -= chunk;
        }
    }
    if (!mdns_finish_record(writer, rdlen_offset, data_start)) {
        writer->len = start;
        return 0;
    }
    return 1;
}

static int mdns_append_a(MiMdnsWriter* writer, const char* owner, const char* ipv4, uint32_t ttl)
{
    size_t start;
    size_t rdlen_offset;
    size_t data_start;
    struct in_addr addr;

    if (writer == NULL) {
        return 0;
    }
    start = writer->len;
    if (ipv4 == NULL || ipv4[0] == '\0') {
        return 0;
    }
    if (inet_pton(AF_INET, ipv4, &addr) != 1) {
        return 0;
    }
    if (!mdns_append_record_header(writer, owner, MI_MDNS_RECORD_A, 0x8001u, ttl, &rdlen_offset)) {
        writer->len = start;
        return 0;
    }
    data_start = writer->len;
    if (!mdns_write_bytes(writer, &addr, sizeof(addr))) {
        writer->len = start;
        return 0;
    }
    if (!mdns_finish_record(writer, rdlen_offset, data_start)) {
        writer->len = start;
        return 0;
    }
    return 1;
}

static void mdns_writer_begin(MiMdnsWriter* writer, uint8_t* buffer, size_t cap, uint16_t flags)
{
    writer->data = buffer;
    writer->len = 0;
    writer->cap = cap;
    mdns_write_u16(writer, 0);
    mdns_write_u16(writer, flags);
    mdns_write_u16(writer, 0);
    mdns_write_u16(writer, 0);
    mdns_write_u16(writer, 0);
    mdns_write_u16(writer, 0);
}

static int mdns_send_packet(MiMdnsImpl* self, const uint8_t* data, size_t len)
{
    struct sockaddr_in addr;
    int ret;

    if (self == NULL || self->sock == MI_MDNS_INVALID_SOCKET || data == NULL || len == 0) {
        return 0;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(MI_MDNS_PORT);
    addr.sin_addr.s_addr = inet_addr(MI_MDNS_IPV4_ADDR);
    ret = sendto(self->sock, (const char*)data, (int)len, 0, (const struct sockaddr*)&addr, (socklen_t)sizeof(addr));
    return ret == (int)len;
}

static int mdns_question_matches_service(const char* qname, uint16_t qtype, const MiMdnsService* service)
{
    if (qname == NULL || service == NULL) {
        return 0;
    }
    if (qtype == MI_MDNS_RECORD_ANY) {
        return mdns_name_equal(qname, service->service_name) ||
               mdns_name_equal(qname, service->full_name) ||
               mdns_name_equal(qname, service->host_name) ||
               mdns_name_equal(qname, MI_MDNS_SERVICE_ENUM);
    }
    if (qtype == MI_MDNS_RECORD_PTR) {
        return mdns_name_equal(qname, service->service_name) ||
               mdns_name_equal(qname, MI_MDNS_SERVICE_ENUM);
    }
    if (qtype == MI_MDNS_RECORD_SRV || qtype == MI_MDNS_RECORD_TXT) {
        return mdns_name_equal(qname, service->full_name);
    }
    if (qtype == MI_MDNS_RECORD_A) {
        return mdns_name_equal(qname, service->host_name);
    }
    return 0;
}

static int mdns_add_service_records(MiMdnsWriter* writer, const MiMdnsService* service, const char* qname, uint16_t qtype, uint16_t* answer_count)
{
    uint16_t before;

    if (writer == NULL || service == NULL || answer_count == NULL) {
        return 0;
    }

    before = *answer_count;
    if (qtype == MI_MDNS_RECORD_PTR || qtype == MI_MDNS_RECORD_ANY) {
        if (mdns_name_equal(qname, MI_MDNS_SERVICE_ENUM)) {
            if (mdns_append_ptr(writer, MI_MDNS_SERVICE_ENUM, service->service_name, service->ttl)) {
                ++(*answer_count);
            }
        }
        if (mdns_name_equal(qname, service->service_name) || qtype == MI_MDNS_RECORD_ANY) {
            if (mdns_append_ptr(writer, service->service_name, service->full_name, service->ttl)) {
                ++(*answer_count);
            }
        }
    }

    if ((qtype == MI_MDNS_RECORD_SRV || qtype == MI_MDNS_RECORD_ANY || mdns_name_equal(qname, service->service_name)) &&
        (mdns_name_equal(qname, service->full_name) || mdns_name_equal(qname, service->service_name) || qtype == MI_MDNS_RECORD_ANY)) {
        if (mdns_append_srv(writer, service->full_name, service->host_name, service->port, service->ttl)) {
            ++(*answer_count);
        }
    }
    if ((qtype == MI_MDNS_RECORD_TXT || qtype == MI_MDNS_RECORD_ANY || mdns_name_equal(qname, service->service_name)) &&
        (mdns_name_equal(qname, service->full_name) || mdns_name_equal(qname, service->service_name) || qtype == MI_MDNS_RECORD_ANY)) {
        if (mdns_append_txt(writer, service->full_name, service->txt, service->ttl)) {
            ++(*answer_count);
        }
    }
    if ((qtype == MI_MDNS_RECORD_A || qtype == MI_MDNS_RECORD_ANY || mdns_name_equal(qname, service->service_name) || mdns_name_equal(qname, service->full_name)) &&
        service->ipv4[0] != '\0') {
        if (mdns_append_a(writer, service->host_name, service->ipv4, service->ttl)) {
            ++(*answer_count);
        }
    }

    return *answer_count != before;
}

static int mdns_send_response_for_question(MiMdnsImpl* self, const char* qname, uint16_t qtype)
{
    uint8_t buffer[MI_MDNS_PACKET_MAX];
    MiMdnsWriter writer;
    uint16_t answer_count = 0;
    int i;

    if (self == NULL || qname == NULL) {
        return 0;
    }

    mdns_writer_begin(&writer, buffer, sizeof(buffer), 0x8400u);
    if (self->lock != NULL) {
        self->lock->Lock(self->lock);
    }
    for (i = 0; i < self->service_count; ++i) {
        if (mdns_question_matches_service(qname, qtype, &self->services[i])) {
            mdns_add_service_records(&writer, &self->services[i], qname, qtype, &answer_count);
        }
    }
    if (self->lock != NULL) {
        self->lock->UnLock(self->lock);
    }

    if (answer_count == 0) {
        return 0;
    }
    mdns_patch_u16(&writer, 6u, answer_count);
    return mdns_send_packet(self, writer.data, writer.len);
}

static int mdns_send_announce_locked(MiMdnsImpl* self)
{
    uint8_t buffer[MI_MDNS_PACKET_MAX];
    MiMdnsWriter writer;
    uint16_t answer_count = 0;
    int i;

    if (self == NULL) {
        return 0;
    }

    mdns_writer_begin(&writer, buffer, sizeof(buffer), 0x8400u);
    for (i = 0; i < self->service_count; ++i) {
        const MiMdnsService* service = &self->services[i];
        if (mdns_append_ptr(&writer, MI_MDNS_SERVICE_ENUM, service->service_name, service->ttl)) {
            ++answer_count;
        }
        if (mdns_append_ptr(&writer, service->service_name, service->full_name, service->ttl)) {
            ++answer_count;
        }
        if (mdns_append_srv(&writer, service->full_name, service->host_name, service->port, service->ttl)) {
            ++answer_count;
        }
        if (mdns_append_txt(&writer, service->full_name, service->txt, service->ttl)) {
            ++answer_count;
        }
        if (service->ipv4[0] != '\0' && mdns_append_a(&writer, service->host_name, service->ipv4, service->ttl)) {
            ++answer_count;
        }
    }
    if (answer_count == 0) {
        return 0;
    }
    mdns_patch_u16(&writer, 6u, answer_count);
    return mdns_send_packet(self, writer.data, writer.len);
}

static int mdns_send_query(MiMdnsImpl* self, const char* qname, uint16_t qtype)
{
    uint8_t buffer[MI_MDNS_PACKET_MAX];
    MiMdnsWriter writer;

    if (self == NULL || qname == NULL || qname[0] == '\0') {
        return 0;
    }

    mdns_writer_begin(&writer, buffer, sizeof(buffer), 0);
    if (!mdns_append_question(&writer, qname, qtype)) {
        return 0;
    }
    mdns_patch_u16(&writer, 4u, 1u);
    return mdns_send_packet(self, writer.data, writer.len);
}

static void mdns_parse_txt(const uint8_t* data, size_t len, char* out, size_t out_len)
{
    size_t pos = 0;
    size_t out_pos = 0;

    if (out == NULL || out_len == 0) {
        return;
    }
    out[0] = '\0';
    while (pos < len) {
        uint8_t item_len = data[pos++];
        if (pos + item_len > len) {
            return;
        }
        if (item_len > 0) {
            size_t copy_len = item_len;
            if (out_pos != 0 && out_pos + 1u < out_len) {
                out[out_pos++] = ';';
            }
            if (out_pos + copy_len >= out_len) {
                copy_len = out_len - out_pos - 1u;
            }
            if (copy_len > 0) {
                memcpy(out + out_pos, data + pos, copy_len);
                out_pos += copy_len;
                out[out_pos] = '\0';
            }
        }
        pos += item_len;
    }
}

static int mdns_find_result(mi_MdnsResult* results, int* result_count, const char* full_name)
{
    int i;

    if (results == NULL || result_count == NULL || full_name == NULL || full_name[0] == '\0') {
        return -1;
    }
    for (i = 0; i < *result_count; ++i) {
        if (mdns_name_equal(results[i].full_name, full_name)) {
            return i;
        }
    }
    if (*result_count >= MI_MDNS_MAX_RESULTS) {
        return -1;
    }
    i = *result_count;
    memset(&results[i], 0, sizeof(results[i]));
    mdns_copy(results[i].full_name, sizeof(results[i].full_name), full_name);
    ++(*result_count);
    return i;
}

static void mdns_apply_host_addresses(mi_MdnsResult* results, int result_count, const MiMdnsHostAddr* addrs, int addr_count)
{
    int i;
    int j;

    for (i = 0; i < result_count; ++i) {
        if (results[i].addr[0] != '\0' || results[i].host_name[0] == '\0') {
            continue;
        }
        for (j = 0; j < addr_count; ++j) {
            if (mdns_name_equal(results[i].host_name, addrs[j].host_name)) {
                mdns_copy(results[i].addr, sizeof(results[i].addr), addrs[j].addr);
                results[i].address_family = addrs[j].family;
                break;
            }
        }
    }
}

static void mdns_parse_response(MiMdnsImpl* self, const uint8_t* packet, size_t packet_len, uint16_t qd, uint16_t an, uint16_t ns, uint16_t ar)
{
    size_t offset = 12u;
    uint32_t total_rr;
    uint32_t rr;
    uint16_t i;
    mi_MdnsResult results[MI_MDNS_MAX_RESULTS];
    MiMdnsHostAddr addrs[MI_MDNS_MAX_ADDRS];
    int result_count = 0;
    int addr_count = 0;

    if (self == NULL || packet == NULL || packet_len < 12u || self->on_result == NULL) {
        return;
    }

    memset(results, 0, sizeof(results));
    memset(addrs, 0, sizeof(addrs));
    for (i = 0; i < qd; ++i) {
        char qname[MI_MDNS_MAX_NAME];
        if (!mdns_read_name(packet, packet_len, &offset, qname, sizeof(qname)) || offset + 4u > packet_len) {
            return;
        }
        offset += 4u;
    }

    total_rr = (uint32_t)an + (uint32_t)ns + (uint32_t)ar;
    for (rr = 0; rr < total_rr; ++rr) {
        char name[MI_MDNS_MAX_NAME];
        uint16_t type;
        uint16_t klass;
        uint32_t ttl;
        uint16_t rdlen;
        size_t rdata;
        int idx;

        if (!mdns_read_name(packet, packet_len, &offset, name, sizeof(name)) || offset + 10u > packet_len) {
            return;
        }
        type = mdns_read_u16(packet + offset);
        klass = mdns_read_u16(packet + offset + 2u);
        ttl = mdns_read_u32(packet + offset + 4u);
        rdlen = mdns_read_u16(packet + offset + 8u);
        offset += 10u;
        if (offset + rdlen > packet_len) {
            return;
        }
        rdata = offset;
        (void)klass;

        if (type == MI_MDNS_RECORD_PTR) {
            char target[MI_MDNS_MAX_NAME];
            size_t roff = rdata;
            if (mdns_read_name(packet, packet_len, &roff, target, sizeof(target))) {
                idx = mdns_find_result(results, &result_count, target);
                if (idx >= 0) {
                    mdns_copy(results[idx].service_type, sizeof(results[idx].service_type), name);
                    mdns_extract_instance(target, name, results[idx].instance_name, sizeof(results[idx].instance_name));
                    results[idx].ttl = ttl;
                }
            }
        } else if (type == MI_MDNS_RECORD_SRV && rdlen >= 6u) {
            char host[128];
            size_t roff = rdata + 6u;
            if (mdns_read_name(packet, packet_len, &roff, host, sizeof(host))) {
                idx = mdns_find_result(results, &result_count, name);
                if (idx >= 0) {
                    results[idx].port = mdns_read_u16(packet + rdata + 4u);
                    results[idx].ttl = ttl;
                    mdns_copy(results[idx].host_name, sizeof(results[idx].host_name), host);
                }
            }
        } else if (type == MI_MDNS_RECORD_TXT) {
            idx = mdns_find_result(results, &result_count, name);
            if (idx >= 0) {
                mdns_parse_txt(packet + rdata, rdlen, results[idx].txt, sizeof(results[idx].txt));
                results[idx].ttl = ttl;
            }
        } else if (type == MI_MDNS_RECORD_A && rdlen == 4u) {
            struct in_addr addr;
            char ip[64];
            memcpy(&addr, packet + rdata, sizeof(addr));
            if (inet_ntop(AF_INET, &addr, ip, sizeof(ip)) != NULL && addr_count < MI_MDNS_MAX_ADDRS) {
                mdns_copy(addrs[addr_count].host_name, sizeof(addrs[addr_count].host_name), name);
                mdns_copy(addrs[addr_count].addr, sizeof(addrs[addr_count].addr), ip);
                addrs[addr_count].family = AF_INET;
                ++addr_count;
            }
        }

        offset += rdlen;
    }

    mdns_apply_host_addresses(results, result_count, addrs, addr_count);
    for (i = 0; i < (uint16_t)result_count; ++i) {
        if (results[i].full_name[0] != '\0') {
            if (results[i].instance_name[0] == '\0' && results[i].service_type[0] != '\0') {
                mdns_extract_instance(results[i].full_name, results[i].service_type, results[i].instance_name, sizeof(results[i].instance_name));
            }
            self->on_result(self->user_data, &results[i]);
        }
    }
}

static void mdns_handle_packet(MiMdnsImpl* self, const uint8_t* packet, size_t packet_len)
{
    uint16_t flags;
    uint16_t qd;
    uint16_t an;
    uint16_t ns;
    uint16_t ar;
    size_t offset = 12u;
    uint16_t i;

    if (self == NULL || packet == NULL || packet_len < 12u) {
        return;
    }

    flags = mdns_read_u16(packet + 2u);
    qd = mdns_read_u16(packet + 4u);
    an = mdns_read_u16(packet + 6u);
    ns = mdns_read_u16(packet + 8u);
    ar = mdns_read_u16(packet + 10u);

    if ((flags & 0x8000u) != 0) {
        mdns_parse_response(self, packet, packet_len, qd, an, ns, ar);
        return;
    }

    for (i = 0; i < qd; ++i) {
        char qname[MI_MDNS_MAX_NAME];
        uint16_t qtype;
        if (!mdns_read_name(packet, packet_len, &offset, qname, sizeof(qname)) || offset + 4u > packet_len) {
            return;
        }
        qtype = mdns_read_u16(packet + offset);
        offset += 4u;
        mdns_send_response_for_question(self, qname, qtype);
    }
}

static thread_return_t mdns_thread_main(void* arg)
{
    MiMdnsImpl* self = (MiMdnsImpl*)arg;

    while (self != NULL && self->running) {
        fd_set read_set;
        struct timeval tv;
        int ret;

        FD_ZERO(&read_set);
        FD_SET(self->sock, &read_set);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        ret = select((int)(self->sock + 1), &read_set, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(self->sock, &read_set)) {
            uint8_t buffer[MI_MDNS_PACKET_MAX];
            struct sockaddr_in from_addr;
            socklen_t from_len = (socklen_t)sizeof(from_addr);
            int len = recvfrom(self->sock, (char*)buffer, (int)sizeof(buffer), 0, (struct sockaddr*)&from_addr, &from_len);
            if (len > 0) {
                mdns_handle_packet(self, buffer, (size_t)len);
            }
        }
    }
    return NULL;
}

#if MI_BASE_OS_WINDOWS
static int mdns_ensure_winsock(void)
{
    static int wsa_started = 0;
    if (!wsa_started) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return 0;
        }
        wsa_started = 1;
    }
    return 1;
}
#endif

static void mdns_close_socket(mi_mdns_socket_t sock)
{
#if MI_BASE_OS_WINDOWS
    closesocket(sock);
#else
    close(sock);
#endif
}

static int mdns_detect_ipv4(char* out, size_t out_len)
{
    mi_mdns_socket_t sock;
    struct sockaddr_in remote_addr;
    struct sockaddr_in local_addr;
    socklen_t local_len;

    if (out == NULL || out_len == 0) {
        return 0;
    }
    out[0] = '\0';

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == MI_MDNS_INVALID_SOCKET) {
        return 0;
    }
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(80);
    remote_addr.sin_addr.s_addr = inet_addr("8.8.8.8");
    if (connect(sock, (const struct sockaddr*)&remote_addr, (socklen_t)sizeof(remote_addr)) == 0) {
        memset(&local_addr, 0, sizeof(local_addr));
        local_len = (socklen_t)sizeof(local_addr);
        if (getsockname(sock, (struct sockaddr*)&local_addr, &local_len) == 0 &&
            local_addr.sin_addr.s_addr != htonl(INADDR_LOOPBACK) &&
            local_addr.sin_addr.s_addr != 0) {
            if (inet_ntop(AF_INET, &local_addr.sin_addr, out, out_len) != NULL) {
                mdns_close_socket(sock);
                return 1;
            }
        }
    }
    mdns_close_socket(sock);
    return 0;
}

static int mdns_detect_hostname(char* out, size_t out_len)
{
    char host[128];

    if (out == NULL || out_len == 0) {
        return 0;
    }
    out[0] = '\0';
    if (gethostname(host, sizeof(host)) != 0) {
        mdns_copy(out, out_len, "mibase");
        return 1;
    }
    host[sizeof(host) - 1u] = '\0';
    return mdns_make_host_name(host, out, out_len);
}

static mi_mdns_socket_t mdns_open_socket(void)
{
    mi_mdns_socket_t sock;
    struct sockaddr_in bind_addr;
    struct ip_mreq mreq;
    int opt = 1;
    unsigned char ttl = 255;
    unsigned char loop = 1;

#if MI_BASE_OS_WINDOWS
    if (!mdns_ensure_winsock()) {
        return MI_MDNS_INVALID_SOCKET;
    }
#endif

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == MI_MDNS_INVALID_SOCKET) {
        return MI_MDNS_INVALID_SOCKET;
    }

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, (socklen_t)sizeof(opt));
#if !MI_BASE_OS_WINDOWS && defined(SO_REUSEPORT)
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (const char*)&opt, (socklen_t)sizeof(opt));
#endif
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, (socklen_t)sizeof(ttl));
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop, (socklen_t)sizeof(loop));

    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(MI_MDNS_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (const struct sockaddr*)&bind_addr, (socklen_t)sizeof(bind_addr)) == MI_MDNS_SOCKET_ERROR) {
        mdns_close_socket(sock);
        return MI_MDNS_INVALID_SOCKET;
    }

    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = inet_addr(MI_MDNS_IPV4_ADDR);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, (socklen_t)sizeof(mreq)) == MI_MDNS_SOCKET_ERROR) {
        mdns_close_socket(sock);
        return MI_MDNS_INVALID_SOCKET;
    }

    return sock;
}

static int mdns_start_impl(mi_Mdns* mdns)
{
    MiMdnsImpl* self = (MiMdnsImpl*)mdns;
    thread_config_t config;

    if (self == NULL) {
        return 0;
    }
    if (self->running) {
        return 1;
    }

    self->sock = mdns_open_socket();
    if (self->sock == MI_MDNS_INVALID_SOCKET) {
        return 0;
    }

    memset(&config, 0, sizeof(config));
    config.name = "mi_mdns";
    config.stack_size = 0;
    config.detached = 0;
    self->running = 1;
    if (mi_thread_create(&self->thread, mdns_thread_main, self, &config) != MI_THREAD_OK) {
        self->running = 0;
        mdns_close_socket(self->sock);
        self->sock = MI_MDNS_INVALID_SOCKET;
        return 0;
    }

    mdns_announce_impl(mdns);
    return 1;
}

static void mdns_stop_impl(mi_Mdns* mdns)
{
    MiMdnsImpl* self = (MiMdnsImpl*)mdns;

    if (self == NULL || !self->running) {
        return;
    }
    self->running = 0;
    mi_thread_join(&self->thread, NULL, 2000);
    if (self->sock != MI_MDNS_INVALID_SOCKET) {
        mdns_close_socket(self->sock);
        self->sock = MI_MDNS_INVALID_SOCKET;
    }
}

static int mdns_add_service_impl(mi_Mdns* mdns, const mi_MdnsServiceConfig* service)
{
    MiMdnsImpl* self = (MiMdnsImpl*)mdns;
    MiMdnsService* item;
    char service_name[MI_MDNS_MAX_NAME];
    char host_name[128];

    if (self == NULL || service == NULL || service->instance_name == NULL || service->service_type == NULL || service->port == 0) {
        return 0;
    }
    if (!mdns_make_service_name(service->service_type, service->protocol, service_name, sizeof(service_name))) {
        return 0;
    }
    if (!mdns_make_host_name(service->host_name != NULL ? service->host_name : self->host_name, host_name, sizeof(host_name))) {
        return 0;
    }

    if (self->lock != NULL) {
        self->lock->Lock(self->lock);
    }
    if (self->service_count >= MI_MDNS_MAX_SERVICES) {
        if (self->lock != NULL) {
            self->lock->UnLock(self->lock);
        }
        return 0;
    }

    item = &self->services[self->service_count];
    memset(item, 0, sizeof(*item));
    mdns_copy(item->instance_name, sizeof(item->instance_name), service->instance_name);
    mdns_copy(item->service_type, sizeof(item->service_type), service->service_type);
    mdns_normalize_protocol(service->protocol, item->protocol, sizeof(item->protocol));
    mdns_copy(item->service_name, sizeof(item->service_name), service_name);
    if (!mdns_make_full_name(item->instance_name, item->service_name, item->full_name, sizeof(item->full_name))) {
        if (self->lock != NULL) {
            self->lock->UnLock(self->lock);
        }
        return 0;
    }
    mdns_copy(item->host_name, sizeof(item->host_name), host_name);
    mdns_copy(item->ipv4, sizeof(item->ipv4), service->ipv4 != NULL ? service->ipv4 : self->ipv4);
    mdns_copy(item->txt, sizeof(item->txt), service->txt);
    item->port = service->port;
    item->ttl = service->ttl != 0 ? service->ttl : MI_MDNS_DEFAULT_TTL;
    ++self->service_count;
    if (self->lock != NULL) {
        self->lock->UnLock(self->lock);
    }

    if (self->running) {
        mdns_announce_impl(mdns);
    }
    return 1;
}

static void mdns_clear_services_impl(mi_Mdns* mdns)
{
    MiMdnsImpl* self = (MiMdnsImpl*)mdns;

    if (self == NULL) {
        return;
    }
    if (self->lock != NULL) {
        self->lock->Lock(self->lock);
    }
    memset(self->services, 0, sizeof(self->services));
    self->service_count = 0;
    if (self->lock != NULL) {
        self->lock->UnLock(self->lock);
    }
}

static int mdns_query_impl(mi_Mdns* mdns, const char* service_type, const char* protocol)
{
    MiMdnsImpl* self = (MiMdnsImpl*)mdns;
    char service_name[MI_MDNS_MAX_NAME];

    if (self == NULL || service_type == NULL || service_type[0] == '\0') {
        return 0;
    }
    if (!self->running && !mdns_start_impl(mdns)) {
        return 0;
    }
    if (!mdns_make_service_name(service_type, protocol, service_name, sizeof(service_name))) {
        return 0;
    }
    return mdns_send_query(self, service_name, MI_MDNS_RECORD_PTR);
}

static int mdns_announce_impl(mi_Mdns* mdns)
{
    MiMdnsImpl* self = (MiMdnsImpl*)mdns;
    int ret;

    if (self == NULL) {
        return 0;
    }
    if (!self->running) {
        return 1;
    }
    if (self->lock != NULL) {
        self->lock->Lock(self->lock);
    }
    ret = mdns_send_announce_locked(self);
    if (self->lock != NULL) {
        self->lock->UnLock(self->lock);
    }
    return ret;
}

struct mi_Mdns* mi_CreateMdns(const mi_MdnsConfig* config)
{
    MiMdnsImpl* self;

#if MI_BASE_OS_WINDOWS
    if (!mdns_ensure_winsock()) {
        return NULL;
    }
#endif

    self = (MiMdnsImpl*)mi_mem_malloc((int)sizeof(MiMdnsImpl));
    if (self == NULL) {
        return NULL;
    }
    memset(self, 0, sizeof(*self));
    self->api.Start = mdns_start_impl;
    self->api.Stop = mdns_stop_impl;
    self->api.AddService = mdns_add_service_impl;
    self->api.ClearServices = mdns_clear_services_impl;
    self->api.Query = mdns_query_impl;
    self->api.Announce = mdns_announce_impl;
    self->sock = MI_MDNS_INVALID_SOCKET;
    self->flags = MI_MDNS_FLAG_IPV4;
    if (config != NULL) {
        self->flags = config->flags != 0 ? config->flags : MI_MDNS_FLAG_IPV4;
        self->on_result = config->on_result;
        self->user_data = config->user_data;
        if (config->ipv4 != NULL) {
            mdns_copy(self->ipv4, sizeof(self->ipv4), config->ipv4);
        }
    }
    if (self->ipv4[0] == '\0') {
        mdns_detect_ipv4(self->ipv4, sizeof(self->ipv4));
    }
    if (config != NULL && config->host_name != NULL) {
        mdns_make_host_name(config->host_name, self->host_name, sizeof(self->host_name));
    } else {
        mdns_detect_hostname(self->host_name, sizeof(self->host_name));
    }

    self->lock = mi_CreateMutex(1);
    if (self->lock == NULL) {
        mi_mem_free(self);
        return NULL;
    }
    return &self->api;
}

void mi_DestroyMdns(struct mi_Mdns* mdns)
{
    MiMdnsImpl* self = (MiMdnsImpl*)mdns;

    if (self == NULL) {
        return;
    }
    mdns_stop_impl(mdns);
    if (self->lock != NULL) {
        mi_DelMutex(self->lock);
        self->lock = NULL;
    }
    mi_mem_free(self);
}

#else

struct mi_Mdns* mi_CreateMdns(const mi_MdnsConfig* config)
{
    (void)config;
    return NULL;
}

void mi_DestroyMdns(struct mi_Mdns* mdns)
{
    (void)mdns;
}

#endif

int mi_mdns_start(struct mi_Mdns* mdns)
{
    return mdns != NULL && mdns->Start != NULL ? mdns->Start(mdns) : 0;
}

void mi_mdns_stop(struct mi_Mdns* mdns)
{
    if (mdns != NULL && mdns->Stop != NULL) {
        mdns->Stop(mdns);
    }
}

int mi_mdns_add_service(struct mi_Mdns* mdns, const mi_MdnsServiceConfig* service)
{
    return mdns != NULL && mdns->AddService != NULL ? mdns->AddService(mdns, service) : 0;
}

void mi_mdns_clear_services(struct mi_Mdns* mdns)
{
    if (mdns != NULL && mdns->ClearServices != NULL) {
        mdns->ClearServices(mdns);
    }
}

int mi_mdns_query(struct mi_Mdns* mdns, const char* service_type, const char* protocol)
{
    return mdns != NULL && mdns->Query != NULL ? mdns->Query(mdns, service_type, protocol) : 0;
}

int mi_mdns_announce(struct mi_Mdns* mdns)
{
    return mdns != NULL && mdns->Announce != NULL ? mdns->Announce(mdns) : 0;
}
