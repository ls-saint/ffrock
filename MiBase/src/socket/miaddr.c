#include "mibase.h"
#include "socket/miaddr.h"

#include <string.h>

static int mi_addr_parse_ipv4_text(const char* ip, unsigned char bytes[4])
{
    const char* p;
    unsigned int value = 0;
    int part = 0;
    int digits = 0;

    if (ip == NULL || ip[0] == '\0') {
        return 0;
    }

    for (p = ip; ; ++p) {
        if (*p >= '0' && *p <= '9') {
            value = (value * 10u) + (unsigned int)(*p - '0');
            if (value > 255u) {
                return 0;
            }
            ++digits;
            if (digits > 3) {
                return 0;
            }
            continue;
        }
        if (*p == '.' || *p == '\0') {
            if (digits == 0 || part >= 4) {
                return 0;
            }
            if (bytes != NULL) {
                bytes[part] = (unsigned char)value;
            }
            ++part;
            if (*p == '\0') {
                break;
            }
            value = 0;
            digits = 0;
            continue;
        }
        return 0;
    }

    return part == 4;
}

static int mi_addr_is_loopback_text(const char* ip)
{
    unsigned char bytes[4];

    if (ip == NULL || ip[0] == '\0') {
        return 0;
    }
    if (mi_addr_parse_ipv4_text(ip, bytes)) {
        return bytes[0] == 127u;
    }
    return strcmp(ip, "::1") == 0;
}

static int mi_addr_is_unspecified_text(const char* ip)
{
    unsigned char bytes[4];

    if (ip == NULL || ip[0] == '\0') {
        return 1;
    }
    if (mi_addr_parse_ipv4_text(ip, bytes)) {
        return bytes[0] == 0u && bytes[1] == 0u && bytes[2] == 0u && bytes[3] == 0u;
    }
    return strcmp(ip, "::") == 0 || strcmp(ip, "0:0:0:0:0:0:0:0") == 0;
}

static int mi_addr_is_link_local_text(const char* ip)
{
    unsigned char bytes[4];

    if (ip == NULL || ip[0] == '\0') {
        return 0;
    }
    if (mi_addr_parse_ipv4_text(ip, bytes)) {
        return bytes[0] == 169u && bytes[1] == 254u;
    }
    return (ip[0] == 'f' || ip[0] == 'F') &&
        (ip[1] == 'e' || ip[1] == 'E') &&
        ip[2] == '8' &&
        ip[3] == '0' &&
        ip[4] == ':';
}

static int mi_addr_is_usable_local_text(const char* ip)
{
    return ip != NULL && ip[0] != '\0' &&
        !mi_addr_is_unspecified_text(ip) &&
        !mi_addr_is_loopback_text(ip) &&
        !mi_addr_is_link_local_text(ip);
}

static int mi_addr_add_local_ip_text(char ips[][MI_ADDR_TEXT_MAX], int maxCount, int* count, const char* ip)
{
    int i;

    if (ips == NULL || count == NULL || maxCount <= 0 ||
        *count >= maxCount || !mi_addr_is_usable_local_text(ip)) {
        return 0;
    }
    for (i = 0; i < *count; ++i) {
        if (strcmp(ips[i], ip) == 0) {
            return 0;
        }
    }
    strncpy(ips[*count], ip, MI_ADDR_TEXT_MAX - 1u);
    ips[*count][MI_ADDR_TEXT_MAX - 1u] = '\0';
    ++(*count);
    return 1;
}

#if MI_BASE_OS_WINDOWS || MI_BASE_OS_POSIX || MI_BASE_OS_VXWORKS || defined(MI_BASE_HAS_BSD_SOCKET)
#define MI_ADDR_USE_BSD_SOCKET 1
#else
#define MI_ADDR_USE_BSD_SOCKET 0
#endif

#if MI_ADDR_USE_BSD_SOCKET

#if MI_BASE_OS_WINDOWS
#include <Winsock2.h>
#include <Windows.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#if defined(_MSC_VER)
#pragma comment(lib, "iphlpapi.lib")
#endif
typedef int socklen_t;
typedef SOCKET mi_addr_socket_t;
#define MI_ADDR_INVALID_SOCKET INVALID_SOCKET
#elif MI_BASE_OS_TXW82X
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
typedef int mi_addr_socket_t;
#define MI_ADDR_INVALID_SOCKET (-1)
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#if MI_BASE_OS_POSIX
#include <ifaddrs.h>
#include <net/if.h>
#endif
typedef int mi_addr_socket_t;
#define MI_ADDR_INVALID_SOCKET (-1)
#endif

#ifndef AF_INET6
#define AF_INET6 10
#endif

#if MI_BASE_OS_TXW82X
#define MI_ADDR_SUPPORT_IPV6 0
#else
#define MI_ADDR_SUPPORT_IPV6 1
#endif

#if MI_BASE_OS_WINDOWS
static int mi_addr_ensure_winsock(void)
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

static struct sockaddr* addr_ptr(mi_SockAddr* addr)
{
    return (struct sockaddr*)addr->ss_addr;
}

static void set_addr_len(mi_SockAddr* addr, socklen_t len)
{
    if (addr != NULL) {
        addr->ss_len = (uint16_t)len;
    }
}

static int mi_addr_inet_pton(int family, const char* src, void* dst)
{
#if MI_BASE_OS_WINDOWS
    char buffer[sizeof(struct sockaddr_in6)];
    struct sockaddr* sa = (struct sockaddr*)buffer;
    int len = family == AF_INET6 ? (int)sizeof(struct sockaddr_in6) : (int)sizeof(struct sockaddr_in);

    if (!mi_addr_ensure_winsock()) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    sa->sa_family = (ADDRESS_FAMILY)family;
    if (WSAStringToAddressA((LPSTR)src, family, NULL, sa, &len) != 0) {
        return 0;
    }

    if (family == AF_INET6) {
        memcpy(dst, &((struct sockaddr_in6*)sa)->sin6_addr, sizeof(struct in6_addr));
    } else {
        memcpy(dst, &((struct sockaddr_in*)sa)->sin_addr, sizeof(struct in_addr));
    }
    return 1;
#else
    return inet_pton(family, src, dst);
#endif
}

static void sync_family(mi_SockAddr* addr)
{
    if (addr != NULL) {
        addr->ss_family = addr_ptr(addr)->sa_family;
    }
}

static socklen_t addr_len_from_family(int family)
{
#if MI_ADDR_SUPPORT_IPV6
    return family == AF_INET6 ? (socklen_t)sizeof(struct sockaddr_in6) : (socklen_t)sizeof(struct sockaddr_in);
#else
    (void)family;
    return (socklen_t)sizeof(struct sockaddr_in);
#endif
}

static void mi_addr_close_socket(mi_addr_socket_t sock)
{
#if MI_BASE_OS_WINDOWS
    closesocket(sock);
#elif MI_BASE_OS_TXW82X
    closesocket(sock);
#else
    close(sock);
#endif
}

static int sockaddr_to_text(const mi_SockAddr* addr, char* out, int out_len)
{
    if (addr == NULL || out == NULL || out_len <= 0 || addr->ss_family == 0) {
        return 0;
    }

    out[0] = '\0';
#if MI_BASE_OS_WINDOWS
    {
        struct sockaddr* sa = (struct sockaddr*)addr->ss_addr;
        socklen_t sa_len = addr->ss_len ?
            (socklen_t)addr->ss_len : addr_len_from_family(addr->ss_family);
        if (getnameinfo(sa, sa_len, out, (socklen_t)out_len, NULL, 0, NI_NUMERICHOST) != 0) {
            return 0;
        }
        return 1;
    }
#else
#if MI_ADDR_SUPPORT_IPV6
    if (addr->ss_family == AF_INET6) {
        const struct sockaddr_in6* in6 = (const struct sockaddr_in6*)addr->ss_addr;
        return inet_ntop(AF_INET6, &in6->sin6_addr, out, (socklen_t)out_len) != NULL;
    }
#endif
    {
        const struct sockaddr_in* in = (const struct sockaddr_in*)addr->ss_addr;
        return inet_ntop(AF_INET, &in->sin_addr, out, (socklen_t)out_len) != NULL;
    }
#endif
}

int mi_SockAddrToText(const struct mi_SockAddr* Addr, char* ip, int ipLen, uint16_t* port)
{
#if MI_BASE_OS_TXW82X
    if (ip != NULL && ipLen > 0) {
        ip[0] = '\0';
    }
    if (port != NULL) {
        *port = 0;
    }
    if (Addr == NULL || Addr->ss_family == 0 || ip == NULL || ipLen <= 0) {
        return 0;
    }
    if (Addr->ss_family != AF_INET) {
        return 0;
    }
    {
        const struct sockaddr_in* in = (const struct sockaddr_in*)Addr->ss_addr;
        if (inet_ntop(AF_INET, &in->sin_addr, ip, (socklen_t)ipLen) == NULL) {
            return 0;
        }
        if (port != NULL) {
            *port = ntohs(in->sin_port);
        }
        return 1;
    }
#else
    char service[16];
    const struct sockaddr* sa;
    socklen_t sa_len;

    if (ip != NULL && ipLen > 0) {
        ip[0] = '\0';
    }
    if (port != NULL) {
        *port = 0;
    }
    if (Addr == NULL || Addr->ss_family == 0 || ip == NULL || ipLen <= 0) {
        return 0;
    }

    sa = (const struct sockaddr*)Addr->ss_addr;
    sa_len = Addr->ss_len ? (socklen_t)Addr->ss_len : addr_len_from_family(Addr->ss_family);
    service[0] = '\0';
    if (getnameinfo(sa, sa_len, ip, (socklen_t)ipLen, service, sizeof(service),
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        ip[0] = '\0';
        return 0;
    }
    if (port != NULL && service[0] != '\0') {
        *port = (uint16_t)atoi(service);
    }
    return 1;
#endif
}

int IsAddrIpv6(struct mi_SockAddr* Addr)
{
#if MI_ADDR_SUPPORT_IPV6
    return Addr != NULL && Addr->ss_family == AF_INET6;
#else
    (void)Addr;
    return 0;
#endif
}

int IsIpTextIpv6(const char* Ip)
{
#if MI_ADDR_SUPPORT_IPV6
    return Ip != NULL && strchr(Ip, ':') != NULL;
#else
    (void)Ip;
    return 0;
#endif
}

int GetIpTextFamily(const char* Ip)
{
    return IsIpTextIpv6(Ip) ? AF_INET6 : AF_INET;
}

void LoadIpPort(const char* Ip, uint16_t port, int ipVersion, struct mi_SockAddr* Addr)
{
    if (Addr == NULL) {
        return;
    }

    memset(Addr, 0, sizeof(*Addr));
    if (ipVersion == AF_INET6) {
#if MI_ADDR_SUPPORT_IPV6
        struct sockaddr_in6* in6 = (struct sockaddr_in6*)Addr->ss_addr;
        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons(port);
        set_addr_len(Addr, (socklen_t)sizeof(*in6));
        if (Ip == NULL || Ip[0] == '\0') {
            in6->sin6_addr = in6addr_any;
        } else if (mi_addr_inet_pton(AF_INET6, Ip, &in6->sin6_addr) != 1) {
            memset(Addr, 0, sizeof(*Addr));
            return;
        }
#else
        return;
#endif
    } else {
        struct sockaddr_in* in = (struct sockaddr_in*)Addr->ss_addr;
        in->sin_family = AF_INET;
        in->sin_port = htons(port);
        set_addr_len(Addr, (socklen_t)sizeof(*in));
        if (Ip == NULL || Ip[0] == '\0') {
            in->sin_addr.s_addr = htonl(INADDR_ANY);
        } else if (mi_addr_inet_pton(AF_INET, Ip, &in->sin_addr) != 1) {
            memset(Addr, 0, sizeof(*Addr));
            return;
        }
    }

    sync_family(Addr);
}

int ResolveIpPort(const char* Host, uint16_t port, int preferredFamily, struct mi_SockAddr* Addr, char* resolvedIp, int resolvedIpLen)
{
    struct addrinfo hints;
    struct addrinfo* result = NULL;
    struct addrinfo* it;
    char service[16];
    int family;

    if (Addr == NULL || Host == NULL || Host[0] == '\0' || port == 0) {
        return 0;
    }

#if MI_BASE_OS_WINDOWS
    if (!mi_addr_ensure_winsock()) {
        return 0;
    }
#endif

    family = preferredFamily;
    if (family != AF_INET && family != AF_INET6) {
        family = GetIpTextFamily(Host);
    }
#if !MI_ADDR_SUPPORT_IPV6
    if (family != AF_INET) {
        family = AF_INET;
    }
#endif

    LoadIpPort(Host, port, family, Addr);
    if (Addr->ss_family != 0) {
        if (resolvedIp != NULL && resolvedIpLen > 0) {
            sockaddr_to_text(Addr, resolvedIp, resolvedIpLen);
        }
        return 1;
    }

    memset(Addr, 0, sizeof(*Addr));
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
#if MI_ADDR_SUPPORT_IPV6
    hints.ai_family = (preferredFamily == AF_INET || preferredFamily == AF_INET6) ? preferredFamily : AF_UNSPEC;
#else
    hints.ai_family = AF_INET;
#endif
    snprintf(service, sizeof(service), "%u", (unsigned int)port);

    if (getaddrinfo(Host, service, &hints, &result) != 0) {
        return 0;
    }

    for (it = result; it != NULL; it = it->ai_next) {
        if ((it->ai_family == AF_INET
#if MI_ADDR_SUPPORT_IPV6
                || it->ai_family == AF_INET6
#endif
            ) &&
            it->ai_addr != NULL &&
            it->ai_addrlen <= sizeof(Addr->ss_addr)) {
            memcpy(Addr->ss_addr, it->ai_addr, (size_t)it->ai_addrlen);
            Addr->ss_len = (uint16_t)it->ai_addrlen;
            Addr->ss_family = (uint16_t)it->ai_family;
            if (resolvedIp != NULL && resolvedIpLen > 0) {
                sockaddr_to_text(Addr, resolvedIp, resolvedIpLen);
            }
            freeaddrinfo(result);
            return 1;
        }
    }

    freeaddrinfo(result);
    return 0;
}

#if MI_BASE_OS_WINDOWS
static int mi_get_local_ip_list_from_adapters(int preferredFamily, char ips[][MI_ADDR_TEXT_MAX], int maxCount, int* count)
{
    IP_ADAPTER_ADDRESSES* adapters;
    IP_ADAPTER_ADDRESSES* adapter;
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG size = 15000u;
    DWORD ret;

    if (ips == NULL || maxCount <= 0 || count == NULL || *count >= maxCount) {
        return 0;
    }
    if (!mi_addr_ensure_winsock()) {
        return 0;
    }

    adapters = (IP_ADAPTER_ADDRESSES*)mi_mem_malloc((int)size);
    if (adapters == NULL) {
        return 0;
    }
    ret = GetAdaptersAddresses((ULONG)AF_UNSPEC, flags, NULL, adapters, &size);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        mi_mem_free(adapters);
        adapters = (IP_ADAPTER_ADDRESSES*)mi_mem_malloc((int)size);
        if (adapters == NULL) {
            return 0;
        }
        ret = GetAdaptersAddresses((ULONG)AF_UNSPEC, flags, NULL, adapters, &size);
    }
    if (ret != NO_ERROR) {
        mi_mem_free(adapters);
        return 0;
    }

    for (adapter = adapters; adapter != NULL; adapter = adapter->Next) {
        IP_ADAPTER_UNICAST_ADDRESS* unicast;

        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }
        for (unicast = adapter->FirstUnicastAddress; unicast != NULL; unicast = unicast->Next) {
            struct sockaddr* sa = unicast->Address.lpSockaddr;
            char text[MI_ADDR_TEXT_MAX];

            if (sa == NULL) {
                continue;
            }
            if (preferredFamily == AF_INET || preferredFamily == 0) {
                if (sa->sa_family == AF_INET &&
                    getnameinfo(sa, (socklen_t)sizeof(struct sockaddr_in),
                                text, sizeof(text), NULL, 0, NI_NUMERICHOST) == 0 &&
                    mi_addr_is_usable_local_text(text)) {
                    mi_addr_add_local_ip_text(ips, maxCount, count, text);
                }
            }
            if (preferredFamily == AF_INET6) {
                if (sa->sa_family == AF_INET6 &&
                    getnameinfo(sa, (socklen_t)sizeof(struct sockaddr_in6),
                                text, sizeof(text), NULL, 0, NI_NUMERICHOST) == 0 &&
                    mi_addr_is_usable_local_text(text)) {
                    mi_addr_add_local_ip_text(ips, maxCount, count, text);
                }
            }
            if (*count >= maxCount) {
                mi_mem_free(adapters);
                return *count;
            }
        }
    }

    mi_mem_free(adapters);
    return *count;
}

static int mi_get_local_ip_from_adapters(int preferredFamily, char* ip, int ipLen)
{
    char ips[1][MI_ADDR_TEXT_MAX];
    int count = 0;

    if (ip == NULL || ipLen <= 0) {
        return 0;
    }
    ip[0] = '\0';
    if (mi_get_local_ip_list_from_adapters(preferredFamily, ips, 1, &count) <= 0) {
        return 0;
    }
    strncpy(ip, ips[0], (size_t)ipLen - 1u);
    ip[ipLen - 1] = '\0';
    return 1;
}
#endif

#if MI_BASE_OS_POSIX
static int mi_get_local_ip_list_from_interfaces(int preferredFamily, char ips[][MI_ADDR_TEXT_MAX], int maxCount, int* count)
{
    struct ifaddrs* addrs = NULL;
    struct ifaddrs* it;

    if (ips == NULL || maxCount <= 0 || count == NULL || *count >= maxCount) {
        return 0;
    }
    if (getifaddrs(&addrs) != 0) {
        return 0;
    }

    for (it = addrs; it != NULL; it = it->ifa_next) {
        char text[MI_ADDR_TEXT_MAX];
        int family;
        socklen_t len;

        if (it->ifa_addr == NULL) {
            continue;
        }
        if ((it->ifa_flags & IFF_UP) == 0 || (it->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }
        family = it->ifa_addr->sa_family;
        if (preferredFamily == AF_INET6) {
            if (family != AF_INET6) {
                continue;
            }
        } else if (family != AF_INET) {
            continue;
        }
        len = family == AF_INET6 ? (socklen_t)sizeof(struct sockaddr_in6) : (socklen_t)sizeof(struct sockaddr_in);
        if (getnameinfo(it->ifa_addr, len, text, sizeof(text), NULL, 0, NI_NUMERICHOST) != 0) {
            continue;
        }
        if (!mi_addr_is_usable_local_text(text)) {
            continue;
        }
        mi_addr_add_local_ip_text(ips, maxCount, count, text);
        if (*count >= maxCount) {
            freeifaddrs(addrs);
            return *count;
        }
    }

    freeifaddrs(addrs);
    return *count;
}

static int mi_get_local_ip_from_interfaces(int preferredFamily, char* ip, int ipLen)
{
    char ips[1][MI_ADDR_TEXT_MAX];
    int count = 0;

    if (ip == NULL || ipLen <= 0) {
        return 0;
    }
    ip[0] = '\0';
    if (mi_get_local_ip_list_from_interfaces(preferredFamily, ips, 1, &count) <= 0) {
        return 0;
    }
    strncpy(ip, ips[0], (size_t)ipLen - 1u);
    ip[ipLen - 1] = '\0';
    return 1;
}
#endif

static int mi_get_local_ip_from_route(int preferredFamily, char* ip, int ipLen)
{
    int family;
    mi_addr_socket_t fd;
    int ret = 0;

    if (ip == NULL || ipLen <= 0) {
        return 0;
    }
    ip[0] = '\0';
#if MI_BASE_OS_WINDOWS
    if (!mi_addr_ensure_winsock()) {
        return 0;
    }
#endif

    family = (MI_ADDR_SUPPORT_IPV6 && preferredFamily == AF_INET6) ? AF_INET6 : AF_INET;
    fd = socket(family, SOCK_DGRAM, 0);
    if (fd == MI_ADDR_INVALID_SOCKET) {
        return 0;
    }

    if (MI_ADDR_SUPPORT_IPV6 && family == AF_INET6) {
#if MI_ADDR_SUPPORT_IPV6
        struct sockaddr_in6 dst;
        memset(&dst, 0, sizeof(dst));
        dst.sin6_family = AF_INET6;
        dst.sin6_port = htons(80);
        if (mi_addr_inet_pton(AF_INET6, "2001:4860:4860::8888", &dst.sin6_addr) == 1 &&
            connect(fd, (struct sockaddr*)&dst, (socklen_t)sizeof(dst)) == 0) {
            struct sockaddr_in6 local;
            socklen_t len = (socklen_t)sizeof(local);
            if (getsockname(fd, (struct sockaddr*)&local, &len) == 0 &&
                getnameinfo((struct sockaddr*)&local, len, ip, (socklen_t)ipLen, NULL, 0, NI_NUMERICHOST) == 0 &&
                mi_addr_is_usable_local_text(ip)) {
                ret = 1;
            }
        }
#endif
    } else {
        struct sockaddr_in dst;
        memset(&dst, 0, sizeof(dst));
        dst.sin_family = AF_INET;
        dst.sin_port = htons(80);
        if (mi_addr_inet_pton(AF_INET, "8.8.8.8", &dst.sin_addr) == 1 &&
            connect(fd, (struct sockaddr*)&dst, (socklen_t)sizeof(dst)) == 0) {
            struct sockaddr_in local;
            socklen_t len = (socklen_t)sizeof(local);
            if (getsockname(fd, (struct sockaddr*)&local, &len) == 0) {
                mi_SockAddr local_addr;
                memset(&local_addr, 0, sizeof(local_addr));
                memcpy(local_addr.ss_addr, &local, sizeof(local));
                local_addr.ss_len = (uint16_t)len;
                local_addr.ss_family = AF_INET;
                if (sockaddr_to_text(&local_addr, ip, ipLen) &&
                    mi_addr_is_usable_local_text(ip)) {
                    ret = 1;
                }
            }
        }
    }

    if (!ret) {
        ip[0] = '\0';
    }
    mi_addr_close_socket(fd);
    return ret;
}

int mi_GetLocalIpListText(int preferredFamily, char ips[][MI_ADDR_TEXT_MAX], int maxCount)
{
    int family = preferredFamily;
    int count = 0;
    char route_ip[MI_ADDR_TEXT_MAX];

    if (ips == NULL || maxCount <= 0) {
        return 0;
    }
    if (family != AF_INET && family != AF_INET6) {
        family = AF_INET;
    }

    route_ip[0] = '\0';
    if (mi_get_local_ip_from_route(family, route_ip, sizeof(route_ip))) {
        mi_addr_add_local_ip_text(ips, maxCount, &count, route_ip);
    }

#if MI_BASE_OS_WINDOWS
    mi_get_local_ip_list_from_adapters(family, ips, maxCount, &count);
#elif MI_BASE_OS_POSIX
    mi_get_local_ip_list_from_interfaces(family, ips, maxCount, &count);
#endif
    return count;
}

int mi_GetLocalIpText(int preferredFamily, char* ip, int ipLen)
{
    char ips[1][MI_ADDR_TEXT_MAX];

    if (ip == NULL || ipLen <= 0) {
        return 0;
    }
    ip[0] = '\0';
    if (mi_GetLocalIpListText(preferredFamily, ips, 1) <= 0) {
        return 0;
    }
    strncpy(ip, ips[0], (size_t)ipLen - 1u);
    ip[ipLen - 1] = '\0';
    return 1;
}

#else

#ifndef AF_INET
#define AF_INET 2
#endif

#ifndef AF_INET6
#define AF_INET6 10
#endif

int IsAddrIpv6(struct mi_SockAddr* Addr)
{
    return Addr != NULL && Addr->ss_family == AF_INET6;
}

int mi_SockAddrToText(const struct mi_SockAddr* Addr, char* ip, int ipLen, uint16_t* port)
{
    if (ip != NULL && ipLen > 0) {
        ip[0] = '\0';
    }
    if (port != NULL) {
        *port = 0;
    }
    if (Addr == NULL || Addr->ss_family == 0 || ip == NULL || ipLen <= 0) {
        return 0;
    }
    snprintf(ip, (size_t)ipLen, "family:%u", (unsigned int)Addr->ss_family);
    return 1;
}

int IsIpTextIpv6(const char* Ip)
{
    return Ip != NULL && strchr(Ip, ':') != NULL;
}

int GetIpTextFamily(const char* Ip)
{
    return IsIpTextIpv6(Ip) ? AF_INET6 : AF_INET;
}

static int is_decimal_byte(const char* start, const char* end)
{
    unsigned int value = 0;
    const char* p;

    if (start == end) {
        return 0;
    }
    for (p = start; p < end; ++p) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
        value = (value * 10u) + (unsigned int)(*p - '0');
        if (value > 255u) {
            return 0;
        }
    }
    return 1;
}

static int is_ipv4_text(const char* ip)
{
    const char* start;
    const char* p;
    int parts = 0;

    if (ip == NULL || ip[0] == '\0') {
        return 0;
    }

    start = ip;
    for (p = ip; ; ++p) {
        if (*p == '.' || *p == '\0') {
            if (!is_decimal_byte(start, p)) {
                return 0;
            }
            ++parts;
            if (*p == '\0') {
                break;
            }
            start = p + 1;
        }
    }

    return parts == 4;
}

static int is_ipv6_text(const char* ip)
{
    const char* p;
    int has_colon = 0;

    if (ip == NULL || ip[0] == '\0') {
        return 0;
    }

    for (p = ip; *p != '\0'; ++p) {
        if (*p == ':') {
            has_colon = 1;
            continue;
        }
        if ((*p >= '0' && *p <= '9') ||
            (*p >= 'a' && *p <= 'f') ||
            (*p >= 'A' && *p <= 'F') ||
            *p == '.') {
            continue;
        }
        return 0;
    }

    return has_colon;
}

void LoadIpPort(const char* Ip, uint16_t port, int ipVersion, struct mi_SockAddr* Addr)
{
    (void)port;
    if (Addr != NULL) {
        memset(Addr, 0, sizeof(*Addr));
        if (Ip != NULL && Ip[0] != '\0') {
            if (ipVersion == AF_INET && !is_ipv4_text(Ip)) {
                return;
            }
            if (ipVersion == AF_INET6 && !is_ipv6_text(Ip)) {
                return;
            }
        }
        Addr->ss_family = (uint16_t)ipVersion;
    }
}

int mi_GetLocalIpText(int preferredFamily, char* ip, int ipLen)
{
    (void)preferredFamily;
    if (ip != NULL && ipLen > 0) {
        ip[0] = '\0';
    }
    return 0;
}

int mi_GetLocalIpListText(int preferredFamily, char ips[][MI_ADDR_TEXT_MAX], int maxCount)
{
    (void)preferredFamily;
    (void)ips;
    (void)maxCount;
    return 0;
}

#endif

int IsIpTextLoopback(const char* Ip)
{
    return mi_addr_is_loopback_text(Ip);
}

int IsIpTextUnspecified(const char* Ip)
{
    return mi_addr_is_unspecified_text(Ip);
}

void LoadIpPortAuto(const char* Ip, uint16_t port, struct mi_SockAddr* Addr)
{
    LoadIpPort(Ip, port, GetIpTextFamily(Ip), Addr);
}

#if !MI_ADDR_USE_BSD_SOCKET
int ResolveIpPort(const char* Host, uint16_t port, int preferredFamily, struct mi_SockAddr* Addr, char* resolvedIp, int resolvedIpLen)
{
    int family = preferredFamily;
    if (family != AF_INET && family != AF_INET6) {
        family = GetIpTextFamily(Host);
    }
    LoadIpPort(Host, port, family, Addr);
    if (resolvedIp != NULL && resolvedIpLen > 0) {
        if (Host != NULL) {
            strncpy(resolvedIp, Host, (size_t)resolvedIpLen - 1u);
            resolvedIp[resolvedIpLen - 1] = '\0';
        } else {
            resolvedIp[0] = '\0';
        }
    }
    return Addr != NULL && Addr->ss_family != 0;
}
#endif
