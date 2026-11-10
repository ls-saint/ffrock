#ifndef miudpskt_H_
#define miudpskt_H_
#include <stdint.h>
#include "miaddr.h"
#ifdef __cplusplus
extern "C" {
#endif
	/**************/
	typedef struct mi_UdpSkt
	{
		uint16_t  (*SendTo)(struct mi_UdpSkt* skt,const uint8_t* buf,uint16_t buflen, struct mi_SockAddr* pAddr);
		uint8_t   (*SetDscp)(struct mi_UdpSkt* skt,int value);
		uint16_t  (*Recv)(struct mi_UdpSkt* skt, uint8_t* buf, uint16_t buflen, struct mi_SockAddr* pAddr);
		uint8_t   (*Bind)(struct mi_UdpSkt* skt, const char* strIp,uint16_t port);
	}mi_UdpSkt;

	struct mi_UdpSkt* mi_CreateUdpSkt(int sktType);
	void mi_DestroyUdpSkt(struct mi_UdpSkt* skt);
	int mi_SetUdpReuseAddr(struct mi_UdpSkt* skt, int enable);
	int mi_SetUdpBroadcast(struct mi_UdpSkt* skt, int enable);
	int mi_SetUdpRecvBuf(struct mi_UdpSkt* skt,int buflen);
	int mi_SetUdpSendBuf(struct mi_UdpSkt* skt, int buflen);
#ifdef __cplusplus
}
#endif
#endif
