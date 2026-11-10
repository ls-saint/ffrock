#include "miipcbroadcast.h"
#include "../App/miipcappinfo.h"
#include "../JSON/cJSON.h"
#include "../../MiBase/include/mibase.h"
#include "../../MiBase/include/mimutex.h"
#include "../../MiBase/include/mithread.h"
#include "../../MiBase/include/socket/miudpskt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if MI_BASE_OS_POSIX && defined(__linux__)
#define MIIPC_BROADCAST_RAW_LINUX 1
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#else
#define MIIPC_BROADCAST_RAW_LINUX 0
#endif

#define MIIPC_BROADCAST_DEFAULT_BIND_IP "0.0.0.0"
#define MIIPC_BROADCAST_DEFAULT_IDLE_MS 20u
#define MIIPC_BROADCAST_DEFAULT_BIND_RETRY_MS 3000u
#define MIIPC_BROADCAST_DEFAULT_RECV_BUF 65536
#define MIIPC_BROADCAST_PACKET_MAX 2048
#define MIIPC_BROADCAST_RAW_FRAME_MAX 2300
#define MIIPC_BROADCAST_PREVIEW_MAX 160
#define MIIPC_BROADCAST_DISCOVERY_MAGIC "AFAFAFBBBBBBBBBB"
#define MIIPC_BROADCAST_DEST_IP "255.255.255.255"
#define MIIPC_BROADCAST_THREAD_STACK_SIZE (16u * 1024u)

typedef struct miipc_broadcast_state {
    mi_UdpSkt* udp;
    mi_Mutex* lock;
    mi_thread_t thread;
    volatile int running;
    int started;
    int stop_registered;
    uint16_t port;
    uint32_t idle_sleep_ms;
    uint32_t bind_retry_ms;
    uint32_t last_bind_attempt_ms;
    uint32_t recv_count;
    uint32_t recv_bytes;
    uint32_t recv_errors;
    uint32_t bind_errors;
    uint32_t response_count;
    uint32_t response_errors;
    int recv_buffer_size;
    char bind_ip[64];
#if MIIPC_BROADCAST_RAW_LINUX
    int raw_fd;
    uint32_t last_raw_attempt_ms;
    uint32_t raw_errors;
    uint32_t raw_recv_count;
    uint32_t raw_send_count;
    uint32_t raw_last_ip_check_ms;
    int raw_no_ip_mode;
    int raw_open_logged;
#endif
    miipc_broadcast_recv_cb callback;
    void* callback_user;
} miipc_broadcast_state_t;

static miipc_broadcast_state_t g_broadcast;

static void broadcast_lock(void)
{
    if (g_broadcast.lock != NULL) {
        g_broadcast.lock->Lock(g_broadcast.lock);
    }
}

static void broadcast_unlock(void)
{
    if (g_broadcast.lock != NULL) {
        g_broadcast.lock->UnLock(g_broadcast.lock);
    }
}

static void broadcast_copy_text(char* dst, int dst_len, const char* src)
{
    if (dst == NULL || dst_len <= 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, (size_t)dst_len - 1u);
    dst[dst_len - 1] = '\0';
}

static int broadcast_ensure_lock(void)
{
    if (g_broadcast.lock != NULL) {
        return 1;
    }
    g_broadcast.lock = mi_CreateMutex(1);
    return g_broadcast.lock != NULL;
}

static uint32_t broadcast_norm_idle(uint32_t value)
{
    if (value == 0) {
        return MIIPC_BROADCAST_DEFAULT_IDLE_MS;
    }
    return value > 1000u ? 1000u : value;
}

static uint32_t broadcast_norm_retry(uint32_t value)
{
    if (value == 0) {
        return MIIPC_BROADCAST_DEFAULT_BIND_RETRY_MS;
    }
    return value < 1000u ? 1000u : value;
}

static int broadcast_try_open_socket(void)
{
    mi_UdpSkt* udp;
    uint16_t port;
    int recv_buffer_size;
    char bind_ip[64];

    broadcast_lock();
    if (g_broadcast.udp != NULL || !g_broadcast.running) {
        broadcast_unlock();
        return g_broadcast.udp != NULL ? 0 : -1;
    }
    g_broadcast.last_bind_attempt_ms = mi_getTick32();
    port = g_broadcast.port;
    recv_buffer_size = g_broadcast.recv_buffer_size;
    broadcast_copy_text(bind_ip, sizeof(bind_ip), g_broadcast.bind_ip);
    broadcast_unlock();

    udp = mi_CreateUdpSkt(1);
    if (udp == NULL) {
        broadcast_lock();
        g_broadcast.bind_errors++;
        broadcast_unlock();
        printf("MiIpc broadcast create udp failed\n");
        fflush(stdout);
        return -1;
    }
    (void)mi_SetUdpReuseAddr(udp, 1);
    (void)mi_SetUdpBroadcast(udp, 1);
    (void)mi_SetUdpRecvBuf(udp, recv_buffer_size);
    if (!udp->Bind(udp, bind_ip, port)) {
        mi_DestroyUdpSkt(udp);
        broadcast_lock();
        g_broadcast.bind_errors++;
        broadcast_unlock();
        printf("MiIpc broadcast bind %s:%u failed, will retry\n", bind_ip, (unsigned int)port);
        fflush(stdout);
        return -1;
    }

    broadcast_lock();
    if (g_broadcast.running && g_broadcast.udp == NULL) {
        g_broadcast.udp = udp;
        udp = NULL;
    }
    broadcast_unlock();

    if (udp != NULL) {
        mi_DestroyUdpSkt(udp);
        return -1;
    }
    printf("MiIpc broadcast listen on %s:%u\n", bind_ip, (unsigned int)port);
    fflush(stdout);
    return 0;
}

#if MIIPC_BROADCAST_RAW_LINUX
static uint16_t broadcast_raw_read_be16(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static void broadcast_raw_write_be16(uint8_t* p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)(value & 0xffu);
}

static uint16_t broadcast_raw_checksum(const uint8_t* data, int len)
{
    uint32_t sum = 0;

    while (len > 1) {
        sum += (uint32_t)broadcast_raw_read_be16(data);
        data += 2;
        len -= 2;
    }
    if (len > 0) {
        sum += (uint32_t)((uint16_t)data[0] << 8);
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

static void broadcast_raw_note_error(void)
{
    broadcast_lock();
    g_broadcast.raw_errors++;
    broadcast_unlock();
}

static void broadcast_raw_close_fd(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

static int broadcast_raw_set_nonblock(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return 0;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static int broadcast_raw_try_open_socket(void)
{
    int fd;
    int err;
    int ok;

    broadcast_lock();
    if (!g_broadcast.running || g_broadcast.raw_fd >= 0) {
        ok = g_broadcast.raw_fd >= 0;
        broadcast_unlock();
        return ok ? 0 : -1;
    }
    g_broadcast.last_raw_attempt_ms = mi_getTick32();
    broadcast_unlock();

    fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
    if (fd < 0) {
        err = errno;
        broadcast_lock();
        g_broadcast.raw_errors++;
        if (!g_broadcast.raw_open_logged || (err != EPERM && err != EACCES)) {
            printf("MiIpc broadcast raw AF_PACKET open failed errno=%d, UDP path still active\n", err);
            fflush(stdout);
            g_broadcast.raw_open_logged = 1;
        }
        broadcast_unlock();
        return -1;
    }

    if (!broadcast_raw_set_nonblock(fd)) {
        err = errno;
        broadcast_raw_close_fd(fd);
        broadcast_lock();
        g_broadcast.raw_errors++;
        printf("MiIpc broadcast raw AF_PACKET nonblock failed errno=%d\n", err);
        fflush(stdout);
        broadcast_unlock();
        return -1;
    }

    broadcast_lock();
    if (g_broadcast.running && g_broadcast.raw_fd < 0) {
        g_broadcast.raw_fd = fd;
        fd = -1;
    }
    broadcast_unlock();

    if (fd >= 0) {
        broadcast_raw_close_fd(fd);
        return -1;
    }

    printf("MiIpc broadcast raw AF_PACKET fallback enabled for no-IP interfaces\n");
    fflush(stdout);
    return 0;
}

static int broadcast_raw_should_recv(mi_UdpSkt* udp)
{
    char ip[64];
    uint32_t now;
    uint32_t last_check;
    int no_ip_mode;

    if (udp == NULL) {
        return 1;
    }

    now = mi_getTick32();
    broadcast_lock();
    last_check = g_broadcast.raw_last_ip_check_ms;
    no_ip_mode = g_broadcast.raw_no_ip_mode;
    broadcast_unlock();
    if (last_check != 0 && (uint32_t)(now - last_check) < 1000u) {
        return no_ip_mode;
    }

    no_ip_mode = !mi_GetLocalIpText(AF_INET, ip, sizeof(ip));
    broadcast_lock();
    g_broadcast.raw_last_ip_check_ms = now;
    g_broadcast.raw_no_ip_mode = no_ip_mode;
    broadcast_unlock();
    return no_ip_mode;
}

static int broadcast_raw_get_iface_mac(int ifindex, uint8_t mac[6])
{
    int fd;
    struct ifreq ifr;
    char ifname[IF_NAMESIZE];

    if (ifindex <= 0 || mac == NULL) {
        return 0;
    }
    if (if_indextoname((unsigned int)ifindex, ifname) == NULL) {
        return 0;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return 0;
    }
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1u);
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) != 0) {
        close(fd);
        return 0;
    }
    close(fd);
    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6u);
    return 1;
}

static int broadcast_raw_make_sockaddr(uint32_t ip_be, uint16_t port, mi_SockAddr* from)
{
    struct sockaddr_in* sin;

    if (from == NULL) {
        return 0;
    }
    memset(from, 0, sizeof(*from));
    sin = (struct sockaddr_in*)from->ss_addr;
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);
    sin->sin_addr.s_addr = ip_be;
    from->ss_family = AF_INET;
    from->ss_len = (uint16_t)sizeof(*sin);
    return 1;
}

static int broadcast_raw_recv(int fd, uint8_t* out, int out_cap, mi_SockAddr* from, int* raw_ifindex)
{
    uint8_t frame[MIIPC_BROADCAST_RAW_FRAME_MAX];
    struct sockaddr_ll ll;
    socklen_t ll_len;
    int frame_len;
    int ip_offset;
    int udp_offset;
    int payload_offset;
    int payload_len;
    int ip_header_len;
    int ip_total_len;
    int udp_len;
    uint16_t eth_type;
    uint16_t fragment;
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t listen_port;
    uint32_t src_ip_be;

    if (fd < 0 || out == NULL || out_cap <= 0 || from == NULL) {
        return -1;
    }

    memset(&ll, 0, sizeof(ll));
    ll_len = (socklen_t)sizeof(ll);
    frame_len = (int)recvfrom(fd, frame, sizeof(frame), 0, (struct sockaddr*)&ll, &ll_len);
    if (frame_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        broadcast_raw_note_error();
        return -1;
    }
    if (frame_len <= 0 || ll.sll_pkttype == PACKET_OUTGOING) {
        return 0;
    }
    if (frame_len < 14) {
        return 0;
    }

    eth_type = broadcast_raw_read_be16(frame + 12);
    ip_offset = 14;
    if ((eth_type == 0x8100u || eth_type == 0x88a8u) && frame_len >= 18) {
        eth_type = broadcast_raw_read_be16(frame + 16);
        ip_offset = 18;
    }
    if (eth_type != (uint16_t)ETH_P_IP) {
        return 0;
    }
    if (frame_len < ip_offset + 20) {
        return 0;
    }
    if ((frame[ip_offset] >> 4) != 4) {
        return 0;
    }

    ip_header_len = (frame[ip_offset] & 0x0fu) * 4;
    if (ip_header_len < 20 || frame_len < ip_offset + ip_header_len + 8) {
        return 0;
    }
    ip_total_len = (int)broadcast_raw_read_be16(frame + ip_offset + 2);
    if (ip_total_len < ip_header_len + 8 || frame_len < ip_offset + ip_total_len) {
        return 0;
    }
    fragment = broadcast_raw_read_be16(frame + ip_offset + 6);
    if ((fragment & 0x3fffu) != 0) {
        return 0;
    }
    if (frame[ip_offset + 9] != IPPROTO_UDP) {
        return 0;
    }

    udp_offset = ip_offset + ip_header_len;
    src_port = broadcast_raw_read_be16(frame + udp_offset);
    dst_port = broadcast_raw_read_be16(frame + udp_offset + 2);
    udp_len = (int)broadcast_raw_read_be16(frame + udp_offset + 4);
    if (udp_len < 8 || udp_len > ip_total_len - ip_header_len) {
        return 0;
    }

    broadcast_lock();
    listen_port = g_broadcast.port;
    broadcast_unlock();
    if (dst_port != listen_port) {
        return 0;
    }

    payload_offset = udp_offset + 8;
    payload_len = udp_len - 8;
    if (payload_len > out_cap) {
        broadcast_raw_note_error();
        return -1;
    }

    memcpy(out, frame + payload_offset, (size_t)payload_len);
    memcpy(&src_ip_be, frame + ip_offset + 12, sizeof(src_ip_be));
    (void)broadcast_raw_make_sockaddr(src_ip_be, src_port, from);
    if (raw_ifindex != NULL) {
        *raw_ifindex = ll.sll_ifindex;
    }

    broadcast_lock();
    g_broadcast.raw_recv_count++;
    broadcast_unlock();
    return payload_len;
}

static int broadcast_raw_send_json(int ifindex, uint16_t reply_port, const char* json, int len)
{
    uint8_t frame[MIIPC_BROADCAST_RAW_FRAME_MAX];
    uint8_t mac[6];
    struct sockaddr_ll dst;
    int fd;
    int frame_len;
    int ip_offset = 14;
    int udp_offset = 34;
    int payload_offset = 42;
    uint16_t listen_port;
    uint16_t ip_len;
    uint16_t udp_len;
    uint16_t checksum;
    static uint16_t packet_id = 1;

    if (ifindex <= 0 || json == NULL || len <= 0) {
        return -1;
    }
    if (reply_port == 0) {
        reply_port = MIIPC_BROADCAST_DEFAULT_PORT;
    }
    if (len > MIIPC_BROADCAST_RAW_FRAME_MAX - payload_offset) {
        broadcast_raw_note_error();
        return -1;
    }

    broadcast_lock();
    fd = g_broadcast.raw_fd;
    listen_port = g_broadcast.port ? g_broadcast.port : (uint16_t)MIIPC_BROADCAST_DEFAULT_PORT;
    broadcast_unlock();
    if (fd < 0) {
        return -1;
    }
    if (!broadcast_raw_get_iface_mac(ifindex, mac)) {
        broadcast_raw_note_error();
        return -1;
    }

    memset(frame, 0, sizeof(frame));
    memset(frame, 0xff, 6u);
    memcpy(frame + 6, mac, 6u);
    broadcast_raw_write_be16(frame + 12, (uint16_t)ETH_P_IP);

    ip_len = (uint16_t)(20 + 8 + len);
    udp_len = (uint16_t)(8 + len);
    frame[ip_offset] = 0x45;
    frame[ip_offset + 1] = 0;
    broadcast_raw_write_be16(frame + ip_offset + 2, ip_len);
    broadcast_raw_write_be16(frame + ip_offset + 4, packet_id++);
    broadcast_raw_write_be16(frame + ip_offset + 6, 0);
    frame[ip_offset + 8] = 64;
    frame[ip_offset + 9] = IPPROTO_UDP;
    memset(frame + ip_offset + 12, 0, 4u);
    memset(frame + ip_offset + 16, 0xff, 4u);
    checksum = broadcast_raw_checksum(frame + ip_offset, 20);
    broadcast_raw_write_be16(frame + ip_offset + 10, checksum);

    broadcast_raw_write_be16(frame + udp_offset, listen_port);
    broadcast_raw_write_be16(frame + udp_offset + 2, reply_port);
    broadcast_raw_write_be16(frame + udp_offset + 4, udp_len);
    broadcast_raw_write_be16(frame + udp_offset + 6, 0);
    memcpy(frame + payload_offset, json, (size_t)len);
    frame_len = payload_offset + len;

    memset(&dst, 0, sizeof(dst));
    dst.sll_family = AF_PACKET;
    dst.sll_protocol = htons(ETH_P_IP);
    dst.sll_ifindex = ifindex;
    dst.sll_halen = 6;
    memset(dst.sll_addr, 0xff, 6u);
    if (sendto(fd, frame, (size_t)frame_len, 0, (struct sockaddr*)&dst, (socklen_t)sizeof(dst)) == frame_len) {
        broadcast_lock();
        g_broadcast.raw_send_count++;
        broadcast_unlock();
        return 0;
    }

    broadcast_raw_note_error();
    return -1;
}
#else
static int broadcast_raw_try_open_socket(void)
{
    return -1;
}

static int broadcast_raw_recv(int fd, uint8_t* out, int out_cap, mi_SockAddr* from, int* raw_ifindex)
{
    (void)fd;
    (void)out;
    (void)out_cap;
    (void)from;
    (void)raw_ifindex;
    return 0;
}

static int broadcast_raw_send_json(int ifindex, uint16_t reply_port, const char* json, int len)
{
    (void)ifindex;
    (void)reply_port;
    (void)json;
    (void)len;
    return -1;
}
#endif

static void broadcast_make_preview(const uint8_t* data, int len, char* out, int out_len)
{
    int i;
    int max_len;

    if (out == NULL || out_len <= 0) {
        return;
    }
    out[0] = '\0';
    if (data == NULL || len <= 0) {
        return;
    }

    max_len = len;
    if (max_len > out_len - 1) {
        max_len = out_len - 1;
    }
    for (i = 0; i < max_len; ++i) {
        unsigned char c = data[i];
        out[i] = (c >= 32u && c < 127u) ? (char)c : '.';
    }
    out[max_len] = '\0';
}

static int broadcast_payload_contains(const uint8_t* data, int len, const char* token)
{
    int i;
    int token_len;

    if (data == NULL || len <= 0 || token == NULL || token[0] == '\0') {
        return 0;
    }
    token_len = (int)strlen(token);
    if (token_len <= 0 || len < token_len) {
        return 0;
    }
    for (i = 0; i <= len - token_len; ++i) {
        if (memcmp(data + i, token, (size_t)token_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static uint16_t broadcast_get_addr_port(const mi_SockAddr* from)
{
    char ip[96];
    uint16_t port = 0;

    if (!mi_SockAddrToText(from, ip, sizeof(ip), &port)) {
        return 0;
    }
    return port;
}

static uint16_t broadcast_get_listen_port(void)
{
    uint16_t port;

    broadcast_lock();
    port = g_broadcast.port;
    broadcast_unlock();
    return port ? port : (uint16_t)MIIPC_BROADCAST_DEFAULT_PORT;
}

//¹ã²¥ÏûÏ¢
static char* broadcast_build_device_info_json(void)
{
    miipc_app_info_t app_info;
    cJSON* root;
    char* json;
    uint16_t listen_port;

    miipc_app_info_get(&app_info);
    listen_port = broadcast_get_listen_port();
    root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

 
    cJSON_AddStringToObject(root, "Url", "DevBroadcast");
    cJSON_AddStringToObject(root, "DevId", app_info.dev_id);
    cJSON_AddNumberToObject(root, "DevRandId", (double)app_info.dev_rand_id);
    cJSON_AddNumberToObject(root, "UId", (double)app_info.uid);
    cJSON_AddStringToObject(root, "Series", app_info.series);
    cJSON_AddStringToObject(root, "BindCode", app_info.bindcode);
    cJSON_AddNumberToObject(root, "ListenPort", (double)listen_port);

    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static int broadcast_send_device_info(mi_UdpSkt* udp, uint16_t reply_port, int raw_ifindex)
{
    mi_SockAddr dst;
    char* json;
    int len;
    uint16_t sent;
    int sent_ok = 0;
    int udp_tried = 0;
    int raw_tried = 0;
    const char* sent_path = "none";

    if (reply_port == 0) {
        reply_port = MIIPC_BROADCAST_DEFAULT_PORT;
    }

    json = broadcast_build_device_info_json();
    if (json == NULL) {
        broadcast_lock();
        g_broadcast.response_errors++;
        broadcast_unlock();
        return -1;
    }
    len = (int)strlen(json);
    if (len <= 0 || len > 0xffff) {
        cJSON_free(json);
        broadcast_lock();
        g_broadcast.response_errors++;
        broadcast_unlock();
        return -1;
    }

    if (raw_ifindex > 0) {
        raw_tried = 1;
        sent_ok = broadcast_raw_send_json(raw_ifindex, reply_port, json, len) == 0;
        if (sent_ok) {
            sent_path = "raw";
        }
    }

    if (!sent_ok && udp != NULL) {
        udp_tried = 1;
        LoadIpPortAuto(MIIPC_BROADCAST_DEST_IP, reply_port, &dst);
        if (dst.ss_family != 0) {
            sent = udp->SendTo(udp, (const uint8_t*)json, (uint16_t)len, &dst);
            sent_ok = sent == (uint16_t)len;
            if (sent_ok) {
                sent_path = "udp";
            }
        } else {
            sent = 0;
        }
    } else {
        sent = 0;
    }

    if (sent_ok) {
        broadcast_lock();
        g_broadcast.response_count++;
        broadcast_unlock();
        printf("MiIpc broadcast device info sent to %s:%u len=%d path=%s\n",
               MIIPC_BROADCAST_DEST_IP,
               (unsigned int)reply_port,
               len,
               sent_path);
        fflush(stdout);
        cJSON_free(json);
        return 0;
    }

    broadcast_lock();
    g_broadcast.response_errors++;
    broadcast_unlock();
    printf("MiIpc broadcast device info send failed to %s:%u len=%d sent=%u udp=%d raw=%d\n",
           MIIPC_BROADCAST_DEST_IP,
           (unsigned int)reply_port,
           len,
           (unsigned int)sent,
           udp_tried,
           raw_tried);
    fflush(stdout);
    cJSON_free(json);
    return -1;
}

static void broadcast_handle_discovery(mi_UdpSkt* udp, const uint8_t* data, int len, const mi_SockAddr* from, int raw_ifindex)
{
    uint16_t reply_port;

    if (!broadcast_payload_contains(data, len, MIIPC_BROADCAST_DISCOVERY_MAGIC)) {
        return;
    }

    reply_port = broadcast_get_addr_port(from);
    (void)broadcast_send_device_info(udp, reply_port, raw_ifindex);
}

static void broadcast_log_packet(const uint8_t* data, int len, const mi_SockAddr* from)
{
    char ip[96];
    char preview[MIIPC_BROADCAST_PREVIEW_MAX + 1];
    uint16_t port = 0;

    if (!mi_SockAddrToText(from, ip, sizeof(ip), &port)) {
        broadcast_copy_text(ip, sizeof(ip), "unknown");
    }
    broadcast_make_preview(data, len, preview, sizeof(preview));
    printf("MiIpc broadcast recv from %s:%u len=%d data=%s\n",
           ip,
           (unsigned int)port,
           len,
           preview);
    fflush(stdout);
}

static void broadcast_process_packet(mi_UdpSkt* udp,
                                     const uint8_t* data,
                                     int len,
                                     const mi_SockAddr* from,
                                     int raw_ifindex,
                                     miipc_broadcast_recv_cb callback,
                                     void* callback_user)
{
    if (data == NULL || len <= 0 || from == NULL) {
        return;
    }

    broadcast_lock();
    g_broadcast.recv_count++;
    g_broadcast.recv_bytes += (uint32_t)len;
    broadcast_unlock();

    broadcast_log_packet(data, len, from);
    broadcast_handle_discovery(udp, data, len, from, raw_ifindex);
    if (callback != NULL) {
        callback(data, len, from, callback_user);
    }
}

static void* broadcast_thread_main(void* user)
{
    uint8_t buffer[MIIPC_BROADCAST_PACKET_MAX];
    (void)user;

    while (g_broadcast.running) {
        mi_UdpSkt* udp;
        uint32_t idle_sleep_ms;
        uint32_t bind_retry_ms;
        uint32_t last_bind_attempt_ms;
        miipc_broadcast_recv_cb callback;
        void* callback_user;
        mi_SockAddr from;
        uint16_t len;
        int processed = 0;
        uint32_t now;
#if MIIPC_BROADCAST_RAW_LINUX
        int raw_fd;
        int raw_ifindex;
        int raw_len;
        uint32_t last_raw_attempt_ms;
#endif

        broadcast_lock();
        udp = g_broadcast.udp;
        idle_sleep_ms = g_broadcast.idle_sleep_ms;
        bind_retry_ms = g_broadcast.bind_retry_ms;
        last_bind_attempt_ms = g_broadcast.last_bind_attempt_ms;
        callback = g_broadcast.callback;
        callback_user = g_broadcast.callback_user;
#if MIIPC_BROADCAST_RAW_LINUX
        raw_fd = g_broadcast.raw_fd;
        last_raw_attempt_ms = g_broadcast.last_raw_attempt_ms;
#endif
        broadcast_unlock();

        if (udp == NULL) {
            now = mi_getTick32();
            if (last_bind_attempt_ms == 0 || (uint32_t)(now - last_bind_attempt_ms) >= bind_retry_ms) {
                (void)broadcast_try_open_socket();
            }
        }

#if MIIPC_BROADCAST_RAW_LINUX
        if (raw_fd < 0) {
            now = mi_getTick32();
            if (last_raw_attempt_ms == 0 || (uint32_t)(now - last_raw_attempt_ms) >= bind_retry_ms) {
                (void)broadcast_raw_try_open_socket();
                broadcast_lock();
                raw_fd = g_broadcast.raw_fd;
                broadcast_unlock();
            }
        }
#endif

        if (udp != NULL) {
            len = udp->Recv(udp, buffer, sizeof(buffer), &from);
            if (len > 0) {
                broadcast_process_packet(udp, buffer, (int)len, &from, 0, callback, callback_user);
                processed = 1;
            }
        }

#if MIIPC_BROADCAST_RAW_LINUX
        if (raw_fd >= 0) {
            raw_ifindex = 0;
            raw_len = broadcast_raw_recv(raw_fd, buffer, sizeof(buffer), &from, &raw_ifindex);
            if (raw_len > 0) {
                broadcast_process_packet(udp, buffer, raw_len, &from, raw_ifindex, callback, callback_user);
                processed = 1;
            }
        }
#endif

        if (!processed) {
            mi_thread_sleep(idle_sleep_ms);
        }
    }
    return NULL;
}

void miipc_broadcast_default_config(miipc_broadcast_config_t* cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->bind_ip = MIIPC_BROADCAST_DEFAULT_BIND_IP;
    cfg->port = (uint16_t)MIIPC_BROADCAST_DEFAULT_PORT;
    cfg->idle_sleep_ms = MIIPC_BROADCAST_DEFAULT_IDLE_MS;
    cfg->bind_retry_ms = MIIPC_BROADCAST_DEFAULT_BIND_RETRY_MS;
    cfg->recv_buffer_size = MIIPC_BROADCAST_DEFAULT_RECV_BUF;
}

void miipc_broadcast_set_callback(miipc_broadcast_recv_cb callback, void* user)
{
    if (!broadcast_ensure_lock()) {
        return;
    }
    broadcast_lock();
    g_broadcast.callback = callback;
    g_broadcast.callback_user = user;
    broadcast_unlock();
}

int miipc_broadcast_start(const miipc_broadcast_config_t* cfg)
{
    miipc_broadcast_config_t default_cfg;
    thread_config_t thread_config;
    uint16_t port;
    const char* bind_ip;
    int recv_buffer_size;

    if (cfg == NULL) {
        miipc_broadcast_default_config(&default_cfg);
        cfg = &default_cfg;
    }
    if (!broadcast_ensure_lock()) {
        return -1;
    }

    broadcast_lock();
    if (g_broadcast.started) {
        broadcast_unlock();
        return 0;
    }
    broadcast_unlock();

    port = cfg->port ? cfg->port : (uint16_t)MIIPC_BROADCAST_DEFAULT_PORT;
    bind_ip = (cfg->bind_ip != NULL && cfg->bind_ip[0] != '\0') ? cfg->bind_ip : MIIPC_BROADCAST_DEFAULT_BIND_IP;
    recv_buffer_size = cfg->recv_buffer_size > 0 ? cfg->recv_buffer_size : MIIPC_BROADCAST_DEFAULT_RECV_BUF;

    broadcast_lock();
    g_broadcast.udp = NULL;
    g_broadcast.running = 1;
    g_broadcast.started = 0;
    g_broadcast.port = port;
    g_broadcast.idle_sleep_ms = broadcast_norm_idle(cfg->idle_sleep_ms);
    g_broadcast.bind_retry_ms = broadcast_norm_retry(cfg->bind_retry_ms);
    g_broadcast.last_bind_attempt_ms = 0;
    g_broadcast.recv_count = 0;
    g_broadcast.recv_bytes = 0;
    g_broadcast.recv_errors = 0;
    g_broadcast.bind_errors = 0;
    g_broadcast.response_count = 0;
    g_broadcast.response_errors = 0;
    g_broadcast.recv_buffer_size = recv_buffer_size;
    broadcast_copy_text(g_broadcast.bind_ip, sizeof(g_broadcast.bind_ip), bind_ip);
#if MIIPC_BROADCAST_RAW_LINUX
    g_broadcast.raw_fd = -1;
    g_broadcast.last_raw_attempt_ms = 0;
    g_broadcast.raw_errors = 0;
    g_broadcast.raw_recv_count = 0;
    g_broadcast.raw_send_count = 0;
    g_broadcast.raw_open_logged = 0;
#endif
    broadcast_unlock();

    memset(&thread_config, 0, sizeof(thread_config));
    thread_config.name = "MiIpcBroadcast";
    thread_config.stack_size = MIIPC_BROADCAST_THREAD_STACK_SIZE;
    thread_config.detached = 0;
    if (mi_thread_create(&g_broadcast.thread, broadcast_thread_main, NULL, &thread_config) != MI_THREAD_OK) {
        broadcast_lock();
        g_broadcast.running = 0;
        g_broadcast.recv_errors++;
        broadcast_unlock();
        return -1;
    }

    broadcast_lock();
    g_broadcast.started = 1;
    broadcast_unlock();
    if (!g_broadcast.stop_registered) {
        atexit(miipc_broadcast_stop);
        g_broadcast.stop_registered = 1;
    }
    (void)broadcast_try_open_socket();
#if MIIPC_BROADCAST_RAW_LINUX
    (void)broadcast_raw_try_open_socket();
#endif
    printf("MiIpc broadcast receiver started for %s:%u\n", bind_ip, (unsigned int)port);
    fflush(stdout);
    return 0;
}

void miipc_broadcast_stop(void)
{
    mi_UdpSkt* udp = NULL;
    int started;
#if MIIPC_BROADCAST_RAW_LINUX
    int raw_fd = -1;
#endif

    if (!broadcast_ensure_lock()) {
        return;
    }

    broadcast_lock();
    started = g_broadcast.started;
    g_broadcast.running = 0;
    broadcast_unlock();

    if (started) {
        (void)mi_thread_join(&g_broadcast.thread, NULL, 3000);
    }

    broadcast_lock();
    udp = g_broadcast.udp;
    g_broadcast.udp = NULL;
#if MIIPC_BROADCAST_RAW_LINUX
    raw_fd = g_broadcast.raw_fd;
    g_broadcast.raw_fd = -1;
#endif
    g_broadcast.started = 0;
    broadcast_unlock();

    if (udp != NULL) {
        mi_DestroyUdpSkt(udp);
    }
#if MIIPC_BROADCAST_RAW_LINUX
    broadcast_raw_close_fd(raw_fd);
#endif
}

void miipc_broadcast_get_stats(miipc_broadcast_stats_t* stats)
{
    if (stats == NULL) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
    if (!broadcast_ensure_lock()) {
        return;
    }
    broadcast_lock();
    stats->started = g_broadcast.started;
    stats->port = g_broadcast.port;
    stats->recv_count = g_broadcast.recv_count;
    stats->recv_bytes = g_broadcast.recv_bytes;
    stats->recv_errors = g_broadcast.recv_errors;
    stats->bind_errors = g_broadcast.bind_errors;
    stats->response_count = g_broadcast.response_count;
    stats->response_errors = g_broadcast.response_errors;
#if MIIPC_BROADCAST_RAW_LINUX
    stats->raw_enabled = g_broadcast.raw_fd >= 0;
    stats->raw_recv_count = g_broadcast.raw_recv_count;
    stats->raw_send_count = g_broadcast.raw_send_count;
    stats->raw_errors = g_broadcast.raw_errors;
#endif
    broadcast_unlock();
}
