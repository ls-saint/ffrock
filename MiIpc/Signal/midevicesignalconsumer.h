#ifndef MIIPC_DEVICE_SIGNAL_CONSUMER_H_
#define MIIPC_DEVICE_SIGNAL_CONSUMER_H_

#ifdef __cplusplus
extern "C" {
#endif

#define MIIPC_SIGNAL_CONSUMER_DEFAULT_QUEUE_LIMIT 64
#define MIIPC_SIGNAL_CONSUMER_MAX_MESSAGE_SIZE 4096

int mi_device_signal_consumer_start(int queue_limit);
void mi_device_signal_consumer_stop(void);
int mi_device_signal_consumer_post(const char* data, int len);
int mi_device_signal_consumer_get_info(char* out, int out_len);

#ifdef __cplusplus
}
#endif

#endif
