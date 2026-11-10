#ifndef MIIPC_CFG_DB_H_
#define MIIPC_CFG_DB_H_

#ifdef __cplusplus
extern "C" {
#endif

#define MIIPC_CFG_DB_FILE "Cfg.ini"
#define MIIPC_CFG_DB_TEXT_MAX 256

int miipc_cfg_db_init(void);
int miipc_cfg_db_read_string(const char* section, const char* key, const char* default_value, char* out, int out_len);
int miipc_cfg_db_read_int(const char* section, const char* key, int default_value, int* out_value);
int miipc_cfg_db_write_string(const char* section, const char* key, const char* value);
int miipc_cfg_db_write_int(const char* section, const char* key, int value);

#ifdef __cplusplus
}
#endif

#endif
