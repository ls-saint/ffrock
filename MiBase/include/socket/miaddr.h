#ifndef miaddr_H_
#define miaddr_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mi_SockAddr {
    uint16_t ss_family;
    uint16_t ss_len;
    uintptr_t ss_align;
    uint8_t ss_addr[256];
} mi_SockAddr;

#define MI_ADDR_TEXT_MAX 46

int   IsIpTextIpv6(const char* Ip);
int   GetIpTextFamily(const char* Ip);
int   IsIpTextLoopback(const char* Ip);
int   IsIpTextUnspecified(const char* Ip);
int   mi_GetLocalIpText(int preferredFamily, char* ip, int ipLen);
int   mi_GetLocalIpListText(int preferredFamily, char ips[][MI_ADDR_TEXT_MAX], int maxCount);
void  LoadIpPort(const char* Ip, uint16_t port, int ipVersion, struct mi_SockAddr* Addr);
void  LoadIpPortAuto(const char* Ip, uint16_t port, struct mi_SockAddr* Addr);
int   ResolveIpPort(const char* Host, uint16_t port, int preferredFamily, struct mi_SockAddr* Addr, char* resolvedIp, int resolvedIpLen);
int   mi_SockAddrToText(const struct mi_SockAddr* Addr, char* ip, int ipLen, uint16_t* port);
int   IsAddrIpv6(struct mi_SockAddr* Addr);

#ifdef __cplusplus
}
#endif

#endif
