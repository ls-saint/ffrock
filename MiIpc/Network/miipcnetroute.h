#ifndef MIIPC_NET_ROUTE_H_
#define MIIPC_NET_ROUTE_H_

#ifdef __cplusplus
extern "C" {
#endif

#define MIIPC_NET_ROUTE_IFACE_MAX 16
#define MIIPC_NET_ROUTE_TEXT_MAX 128
#define MIIPC_NET_ROUTE_GATEWAY_MAX 64

typedef struct miipc_net_route_config {
    int enable;
    int check_interval_ms;
    int ping_timeout_ms;
    int fail_threshold;
    int recover_threshold;
    int switch_cooldown_ms;
    char primary_iface[MIIPC_NET_ROUTE_IFACE_MAX];
    char backup_ifaces[MIIPC_NET_ROUTE_TEXT_MAX];
    char probe_host[MIIPC_NET_ROUTE_TEXT_MAX];
    /* Optional gateways used when an interface has no default route yet. */
    char primary_gateway[MIIPC_NET_ROUTE_GATEWAY_MAX];
    char backup_gateways[MIIPC_NET_ROUTE_TEXT_MAX];
} miipc_net_route_config_t;

void miipc_net_route_default_config(miipc_net_route_config_t* cfg);
void miipc_net_route_load_config(miipc_net_route_config_t* cfg);
int miipc_net_route_start(const miipc_net_route_config_t* cfg);
void miipc_net_route_stop(void);
int miipc_net_route_check_once(void);
const char* miipc_net_route_current_iface(void);

#ifdef __cplusplus
}
#endif

#endif
