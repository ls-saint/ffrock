#include "mibase.h"
#include "socket/miaddr.h"
#include "socket/miudpskt.h"

#include <string.h>

#if MI_BASE_OS_WINDOWS || MI_BASE_OS_POSIX || MI_BASE_OS_VXWORKS || defined(MI_BASE_HAS_BSD_SOCKET)
#define MI_BASE_USE_BSD_SOCKET 1
#else
#define MI_BASE_USE_BSD_SOCKET 0
#endif

#if MI_BASE_USE_BSD_SOCKET

#if MI_BASE_OS_WINDOWS
#include <Winsock2.h>
#include <Windows.h>
#include <ws2tcpip.h>
#if defined(_MSC_VER)
#pragma comment(lib, "ws2_32.lib")
#endif
typedef int socklen_t;
typedef SOCKET mi_socket_t;
#define MI_INVALID_SOCKET INVALID_SOCKET
#define MI_SOCKET_ERROR SOCKET_ERROR
#elif MI_BASE_OS_TXW82X
#include "lwip/sockets.h"
#include "lwip/inet.h"
typedef int mi_socket_t;
#define MI_INVALID_SOCKET (-1)
#define MI_SOCKET_ERROR (-1)
#else
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
typedef int mi_socket_t;
#define MI_INVALID_SOCKET (-1)
#define MI_SOCKET_ERROR (-1)
#endif

#ifndef AF_INET6
#define AF_INET6 10
#endif

#if MI_BASE_OS_TXW82X
#define MI_UDP_SUPPORT_IPV6 0
#else
#define MI_UDP_SUPPORT_IPV6 1
#endif

typedef struct InterUdpSkt {
    mi_UdpSkt skt;
    mi_socket_t fd;
    int sktType;
} InterUdpSkt;

static struct sockaddr* addr_ptr(mi_SockAddr* addr)
{
    return (struct sockaddr*)addr->ss_addr;
}

static const struct sockaddr* const_addr_ptr(const mi_SockAddr* addr)
{
    return (const struct sockaddr*)addr->ss_addr;
}

static socklen_t addr_len_from_family(int family)
{
#if MI_UDP_SUPPORT_IPV6
    return family == AF_INET6 ? (socklen_t)sizeof(struct sockaddr_in6) : (socklen_t)sizeof(struct sockaddr_in);
#else
    (void)family;
    return (socklen_t)sizeof(struct sockaddr_in);
#endif
}

static void set_addr_len(mi_SockAddr* addr, socklen_t len)
{
    if (addr != NULL) {
        addr->ss_len = (uint16_t)len;
    }
}

static void sync_family(mi_SockAddr* addr)
{
    if (addr != NULL) {
        addr->ss_family = addr_ptr(addr)->sa_family;
    }
}

static int make_socket_nonblocking(mi_socket_t sock)
{
#if MI_BASE_OS_WINDOWS
    u_long arg = 1;
    return ioctlsocket(sock, FIONBIO, &arg) == 0;
#elif MI_BASE_OS_TXW82X
    unsigned long arg = 1;
    return ioctlsocket(sock, FIONBIO, &arg) == 0;
#elif MI_BASE_OS_VXWORKS
    int arg = 1;
    return ioctl(sock, FIONBIO, (int)&arg) == 0;
#else
    int cur_flags = fcntl(sock, F_GETFL, 0);
    if (cur_flags < 0) {
        return 0;
    }
    return fcntl(sock, F_SETFL, cur_flags | O_NONBLOCK) == 0;
#endif
}

static void close_socket(mi_socket_t sock)
{
#if MI_BASE_OS_WINDOWS
    closesocket(sock);
#elif MI_BASE_OS_TXW82X
    closesocket(sock);
#else
    close(sock);
#endif
}

static uint16_t onSendTo(mi_UdpSkt* skt, const uint8_t* buf, uint16_t buflen, mi_SockAddr* adr)
{
    InterUdpSkt* iskt = (InterUdpSkt*)skt;
    int ret;

    if (iskt == NULL || buf == NULL || adr == NULL || adr->ss_family == 0) {
        return 0;
    }

    ret = sendto(iskt->fd, (const char*)buf, buflen, 0, const_addr_ptr(adr), addr_len_from_family(adr->ss_family));
    return ret > 0 ? (uint16_t)ret : 0;
}

static uint8_t onSetDscp(mi_UdpSkt* skt, int value)
{
    InterUdpSkt* iskt = (InterUdpSkt*)skt;
    int tos = value << 2;

    if (iskt == NULL) {
        return 0;
    }

    return setsockopt(iskt->fd, IPPROTO_IP, IP_TOS, (const char*)&tos, (socklen_t)sizeof(tos)) == 0;
}

static uint16_t onRecv(mi_UdpSkt* skt, uint8_t* buf, uint16_t buflen, mi_SockAddr* pAddr)
{
    InterUdpSkt* iskt = (InterUdpSkt*)skt;
    socklen_t sockaddrlen;
    int len;

    if (iskt == NULL || buf == NULL || pAddr == NULL || iskt->fd == MI_INVALID_SOCKET) {
        return 0;
    }

    memset(pAddr, 0, sizeof(*pAddr));
    sockaddrlen = (socklen_t)sizeof(pAddr->ss_addr);
    len = recvfrom(iskt->fd, (char*)buf, buflen, 0, addr_ptr(pAddr), &sockaddrlen);
    if (len <= 0) {
        return 0;
    }

    sync_family(pAddr);
    set_addr_len(pAddr, sockaddrlen);
    return (uint16_t)len;
}

static uint8_t onBind(mi_UdpSkt* skt, const char* strIp, uint16_t port)
{
    InterUdpSkt* iskt = (InterUdpSkt*)skt;
    mi_SockAddr addr;

    if (iskt == NULL) {
        return 0;
    }

    LoadIpPort(strIp, port, (MI_UDP_SUPPORT_IPV6 && iskt->sktType == 2) ? AF_INET6 : AF_INET, &addr);
    if (addr.ss_family == 0) {
        return 0;
    }

    return bind(iskt->fd, const_addr_ptr(&addr), addr_len_from_family(addr.ss_family)) != MI_SOCKET_ERROR;
}

struct mi_UdpSkt* mi_CreateUdpSkt(int sktType)
{
    InterUdpSkt* iskt;
    int family = (MI_UDP_SUPPORT_IPV6 && sktType == 2) ? AF_INET6 : AF_INET;

#if MI_BASE_OS_WINDOWS
    static int wsa_started = 0;
    if (!wsa_started) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return NULL;
        }
        wsa_started = 1;
    }
#endif

    iskt = (InterUdpSkt*)mi_mem_malloc((int)sizeof(InterUdpSkt));
    if (iskt == NULL) {
        return NULL;
    }
    memset(iskt, 0, sizeof(*iskt));

    iskt->skt.Bind = onBind;
    iskt->skt.Recv = onRecv;
    iskt->skt.SendTo = onSendTo;
    iskt->skt.SetDscp = onSetDscp;
    iskt->sktType = (MI_UDP_SUPPORT_IPV6 && sktType == 2) ? 2 : 1;
    iskt->fd = socket(family, SOCK_DGRAM, 0);
    if (iskt->fd == MI_INVALID_SOCKET) {
        mi_mem_free(iskt);
        return NULL;
    }

    if (!make_socket_nonblocking(iskt->fd)) {
        close_socket(iskt->fd);
        mi_mem_free(iskt);
        return NULL;
    }

#if defined(IPV6_V6ONLY)
    if (family == AF_INET6) {
        int disabled = 0;
        setsockopt(iskt->fd, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&disabled, (socklen_t)sizeof(disabled));
    }
#endif

    return (mi_UdpSkt*)iskt;
}

void mi_DestroyUdpSkt(struct mi_UdpSkt* skt)
{
    InterUdpSkt* iskt = (InterUdpSkt*)skt;
    if (iskt == NULL) {
        return;
    }
    if (iskt->fd != MI_INVALID_SOCKET) {
        close_socket(iskt->fd);
        iskt->fd = MI_INVALID_SOCKET;
    }
    mi_mem_free(iskt);
}

int mi_SetUdpReuseAddr(struct mi_UdpSkt* skt, int enable)
{
    InterUdpSkt* iskt = (InterUdpSkt*)skt;
    int value = enable ? 1 : 0;
    if (iskt == NULL) {
        return 0;
    }
    return setsockopt(iskt->fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&value, (socklen_t)sizeof(value)) == 0;
}

int mi_SetUdpBroadcast(struct mi_UdpSkt* skt, int enable)
{
    InterUdpSkt* iskt = (InterUdpSkt*)skt;
    int value = enable ? 1 : 0;
    if (iskt == NULL) {
        return 0;
    }
    return setsockopt(iskt->fd, SOL_SOCKET, SO_BROADCAST, (const char*)&value, (socklen_t)sizeof(value)) == 0;
}

int mi_SetUdpRecvBuf(struct mi_UdpSkt* skt, int buflen)
{
    InterUdpSkt* iskt = (InterUdpSkt*)skt;
    if (iskt == NULL || buflen <= 0) {
        return 0;
    }
    return setsockopt(iskt->fd, SOL_SOCKET, SO_RCVBUF, (const char*)&buflen, (socklen_t)sizeof(buflen)) == 0;
}

int mi_SetUdpSendBuf(struct mi_UdpSkt* skt, int buflen)
{
    InterUdpSkt* iskt = (InterUdpSkt*)skt;
    if (iskt == NULL || buflen <= 0) {
        return 0;
    }
    return setsockopt(iskt->fd, SOL_SOCKET, SO_SNDBUF, (const char*)&buflen, (socklen_t)sizeof(buflen)) == 0;
}

#else

struct mi_UdpSkt* mi_CreateUdpSkt(int sktType)
{
    (void)sktType;
    return NULL;
}

void mi_DestroyUdpSkt(struct mi_UdpSkt* skt)
{
    (void)skt;
}

int mi_SetUdpReuseAddr(struct mi_UdpSkt* skt, int enable)
{
    (void)skt;
    (void)enable;
    return 0;
}

int mi_SetUdpBroadcast(struct mi_UdpSkt* skt, int enable)
{
    (void)skt;
    (void)enable;
    return 0;
}

int mi_SetUdpRecvBuf(struct mi_UdpSkt* skt, int buflen)
{
    (void)skt;
    (void)buflen;
    return 0;
}

int mi_SetUdpSendBuf(struct mi_UdpSkt* skt, int buflen)
{
    (void)skt;
    (void)buflen;
    return 0;
}

#endif
