#ifndef MIIPC_LOG_H_
#define MIIPC_LOG_H_

#ifdef __cplusplus
extern "C" {
#endif

#define MIIPC_LOG_DEFAULT_PATH "MiIpc.log"
#define MIIPC_LOG_MAX_BYTES (20u * 1024u)
#define MIIPC_LOG_KEEP_BYTES (10u * 1024u)

int  miipc_log_init(const char* path);
void miipc_log_close(void);
void miipc_log_set_console(int enable);
void miipc_log_write(const char* level, const char* fmt, ...);
void miipc_log_rotate_now(void);

#ifdef __cplusplus
}
#endif

#endif
