#ifndef mimdns_H_
#define mimdns_H_

#include "mibase.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MI_MDNS_PORT 5353u
#define MI_MDNS_MAX_TXT 256u
#define MI_MDNS_MAX_NAME 160u

#define MI_MDNS_FLAG_IPV4 0x01u
#define MI_MDNS_FLAG_IPV6 0x02u

typedef enum mi_MdnsRecordType {
    MI_MDNS_RECORD_A = 1,
    MI_MDNS_RECORD_PTR = 12,
    MI_MDNS_RECORD_TXT = 16,
    MI_MDNS_RECORD_AAAA = 28,
    MI_MDNS_RECORD_SRV = 33,
    MI_MDNS_RECORD_ANY = 255
} mi_MdnsRecordType;

typedef struct mi_MdnsServiceConfig {
    const char* instance_name;
    const char* service_type;
    const char* protocol;
    const char* host_name;
    const char* ipv4;
    const char* txt;
    uint16_t port;
    uint32_t ttl;
} mi_MdnsServiceConfig;

typedef struct mi_MdnsResult {
    char instance_name[64];
    char service_type[96];
    char full_name[MI_MDNS_MAX_NAME];
    char host_name[128];
    char addr[64];
    char txt[MI_MDNS_MAX_TXT];
    uint16_t port;
    uint32_t ttl;
    int address_family;
} mi_MdnsResult;

typedef void (*mi_mdns_result_cb)(void* user_data, const mi_MdnsResult* result);

typedef struct mi_MdnsConfig {
    const char* host_name;
    const char* ipv4;
    uint32_t flags;
    mi_mdns_result_cb on_result;
    void* user_data;
} mi_MdnsConfig;

typedef struct mi_Mdns {
    int  (*Start)(struct mi_Mdns* mdns);
    void (*Stop)(struct mi_Mdns* mdns);
    int  (*AddService)(struct mi_Mdns* mdns, const mi_MdnsServiceConfig* service);
    void (*ClearServices)(struct mi_Mdns* mdns);
    int  (*Query)(struct mi_Mdns* mdns, const char* service_type, const char* protocol);
    int  (*Announce)(struct mi_Mdns* mdns);
} mi_Mdns;

struct mi_Mdns* mi_CreateMdns(const mi_MdnsConfig* config);
void mi_DestroyMdns(struct mi_Mdns* mdns);

int mi_mdns_start(struct mi_Mdns* mdns);
void mi_mdns_stop(struct mi_Mdns* mdns);
int mi_mdns_add_service(struct mi_Mdns* mdns, const mi_MdnsServiceConfig* service);
void mi_mdns_clear_services(struct mi_Mdns* mdns);
int mi_mdns_query(struct mi_Mdns* mdns, const char* service_type, const char* protocol);
int mi_mdns_announce(struct mi_Mdns* mdns);

#ifdef __cplusplus
}
#endif

#endif
