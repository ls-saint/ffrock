#ifndef INI_FILE_H_
#define INI_FILE_H_

#ifdef __cplusplus
extern "C" {
#endif

int ini_read_string(const char* section, const char* key, char* val, int val_len, const char* file);
int ini_read_int(const char* section, const char* key, const char* file, int* outvalue);
int ini_write_string(const char* section, const char* key, const char* val, const char* file);
int ini_write_int(const char* section, const char* key, int val, const char* file);

int IniReadValue(char* section, const char* key, char* val, const char* file);
void IniWriteValue(const char* section, const char* key, const char* val, const char* file);
int readStringValue(const char* section, const char* key, char* val, const char* file);
int readIntValue(const char* section, const char* key, const char* file, int* outvalue);
int writeIntValue(const char* section, const char* key, int val, const char* file);
int writeStringValue(const char* section, const char* key, const char* val, const char* file);
int writeStringVlaue(const char* section, const char* key, const char* val, const char* file);

#ifdef __cplusplus
}
#endif

#endif
