#include "ini/ini.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MI_INI_LINE_MAX 8192
#define MI_INI_SECTION_MAX 256
#define MI_INI_VALUE_MAX 6144

typedef struct mi_ini_line {
    char* text;
    struct mi_ini_line* next;
} mi_ini_line_t;

static char* mi_ini_strdup(const char* text)
{
    size_t len;
    char* copy;
    if (text == NULL) {
        text = "";
    }
    len = strlen(text);
    copy = (char*)malloc(len + 1u);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, len + 1u);
    return copy;
}

static char* trim_left(char* text)
{
    while (text != NULL && *text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }
    return text;
}

static void trim_right(char* text)
{
    size_t len;
    if (text == NULL) {
        return;
    }
    len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1u])) {
        text[len - 1u] = '\0';
        len--;
    }
}

static char* trim_inplace(char* text)
{
    char* out = trim_left(text);
    trim_right(out);
    return out;
}

static int is_blank_or_comment(const char* line)
{
    const char* p = line;
    while (p != NULL && *p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    return p == NULL || *p == '\0' || *p == ';' || *p == '#';
}

static int parse_section(const char* line, char* out, size_t out_len)
{
    char temp[MI_INI_LINE_MAX + 1];
    char* p;
    char* end;
    size_t len;

    if (line == NULL || out == NULL || out_len == 0) {
        return 0;
    }
    out[0] = '\0';
    strncpy(temp, line, sizeof(temp) - 1u);
    temp[sizeof(temp) - 1u] = '\0';
    p = trim_left(temp);
    if (*p != '[') {
        return 0;
    }
    p++;
    end = strchr(p, ']');
    if (end == NULL) {
        return 0;
    }
    *end = '\0';
    p = trim_inplace(p);
    len = strlen(p);
    if (len == 0 || len >= out_len) {
        return 0;
    }
    memcpy(out, p, len + 1u);
    return 1;
}

static int parse_key_value(const char* line, char* key, size_t key_len, char* value, size_t value_len)
{
    char temp[MI_INI_LINE_MAX + 1];
    char* p;
    char* eq;
    char* k;
    char* v;
    size_t len;

    if (line == NULL || key == NULL || value == NULL || key_len == 0 || value_len == 0) {
        return 0;
    }
    key[0] = '\0';
    value[0] = '\0';
    if (is_blank_or_comment(line)) {
        return 0;
    }
    strncpy(temp, line, sizeof(temp) - 1u);
    temp[sizeof(temp) - 1u] = '\0';
    p = trim_left(temp);
    if (*p == '[') {
        return 0;
    }
    eq = strchr(p, '=');
    if (eq == NULL) {
        return 0;
    }
    *eq = '\0';
    k = trim_inplace(p);
    v = trim_inplace(eq + 1);
    len = strlen(k);
    if (len == 0 || len >= key_len) {
        return 0;
    }
    memcpy(key, k, len + 1u);
    len = strlen(v);
    if (len >= value_len) {
        len = value_len - 1u;
    }
    memcpy(value, v, len);
    value[len] = '\0';
    return 1;
}

static int section_matches(const char* line, const char* section)
{
    char parsed[MI_INI_SECTION_MAX + 1];
    return parse_section(line, parsed, sizeof(parsed)) && strcmp(parsed, section) == 0;
}

static int key_matches(const char* line, const char* key)
{
    char parsed_key[MI_INI_SECTION_MAX + 1];
    char parsed_value[MI_INI_LINE_MAX + 1];
    return parse_key_value(line, parsed_key, sizeof(parsed_key), parsed_value, sizeof(parsed_value)) &&
           strcmp(parsed_key, key) == 0;
}

static void make_line(char* out, size_t out_len, const char* key, const char* value)
{
    if (out == NULL || out_len == 0) {
        return;
    }
    snprintf(out, out_len, "%s=%s\n", key != NULL ? key : "", value != NULL ? value : "");
}

static int append_line(mi_ini_line_t** head, mi_ini_line_t** tail, const char* text)
{
    mi_ini_line_t* line = (mi_ini_line_t*)malloc(sizeof(mi_ini_line_t));
    if (line == NULL) {
        return 0;
    }
    line->text = mi_ini_strdup(text);
    if (line->text == NULL) {
        free(line);
        return 0;
    }
    line->next = NULL;
    if (*tail != NULL) {
        (*tail)->next = line;
    } else {
        *head = line;
    }
    *tail = line;
    return 1;
}

static void free_lines(mi_ini_line_t* head)
{
    while (head != NULL) {
        mi_ini_line_t* next = head->next;
        free(head->text);
        free(head);
        head = next;
    }
}

static int load_lines(const char* file, mi_ini_line_t** head, mi_ini_line_t** tail)
{
    FILE* fp;
    char line[MI_INI_LINE_MAX + 1];

    *head = NULL;
    *tail = NULL;
    fp = fopen(file, "r");
    if (fp == NULL) {
        return 1;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (!append_line(head, tail, line)) {
            fclose(fp);
            free_lines(*head);
            *head = NULL;
            *tail = NULL;
            return 0;
        }
    }
    fclose(fp);
    return 1;
}

static int save_lines(const char* file, mi_ini_line_t* head)
{
    FILE* fp = fopen(file, "w");
    if (fp == NULL) {
        return 0;
    }
    while (head != NULL) {
        fputs(head->text != NULL ? head->text : "", fp);
        head = head->next;
    }
    fclose(fp);
    return 1;
}

int ini_read_string(const char* section, const char* key, char* val, int val_len, const char* file)
{
    FILE* fp;
    char line[MI_INI_LINE_MAX + 1];
    int in_section = 0;

    if (val != NULL && val_len > 0) {
        val[0] = '\0';
    }
    if (section == NULL || key == NULL || val == NULL || val_len <= 0 || file == NULL) {
        return 0;
    }

    fp = fopen(file, "r");
    if (fp == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char parsed_section[MI_INI_SECTION_MAX + 1];
        char parsed_key[MI_INI_SECTION_MAX + 1];
        char parsed_value[MI_INI_LINE_MAX + 1];

        if (parse_section(line, parsed_section, sizeof(parsed_section))) {
            in_section = strcmp(parsed_section, section) == 0;
            continue;
        }
        if (!in_section) {
            continue;
        }
        if (parse_key_value(line, parsed_key, sizeof(parsed_key), parsed_value, sizeof(parsed_value)) &&
            strcmp(parsed_key, key) == 0) {
            strncpy(val, parsed_value, (size_t)val_len - 1u);
            val[val_len - 1] = '\0';
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

int ini_read_int(const char* section, const char* key, const char* file, int* outvalue)
{
    char value[MI_INI_VALUE_MAX + 1];
    char* endptr;
    long parsed;

    if (outvalue == NULL) {
        return 0;
    }
    if (!ini_read_string(section, key, value, sizeof(value), file)) {
        return 0;
    }
    parsed = strtol(value, &endptr, 10);
    if (endptr == value) {
        return 0;
    }
    *outvalue = (int)parsed;
    return 1;
}

int ini_write_string(const char* section, const char* key, const char* val, const char* file)
{
    mi_ini_line_t* head = NULL;
    mi_ini_line_t* tail = NULL;
    mi_ini_line_t* cur;
    char new_line[MI_INI_LINE_MAX + 1];
    char new_section[MI_INI_SECTION_MAX + 4];
    int found_section = 0;
    int in_section = 0;
    int wrote_key = 0;

    if (section == NULL || key == NULL || val == NULL || file == NULL) {
        return 0;
    }
    if (strlen(section) > MI_INI_SECTION_MAX || strlen(key) > MI_INI_SECTION_MAX ||
        strlen(val) > MI_INI_VALUE_MAX) {
        return 0;
    }
    if (!load_lines(file, &head, &tail)) {
        return 0;
    }

    make_line(new_line, sizeof(new_line), key, val);
    snprintf(new_section, sizeof(new_section), "[%s]\n", section);

    for (cur = head; cur != NULL; cur = cur->next) {
        char parsed_section[MI_INI_SECTION_MAX + 1];
        if (parse_section(cur->text, parsed_section, sizeof(parsed_section))) {
            if (in_section && !wrote_key) {
                mi_ini_line_t* inserted = (mi_ini_line_t*)malloc(sizeof(mi_ini_line_t));
                if (inserted == NULL) {
                    free_lines(head);
                    return 0;
                }
                inserted->text = mi_ini_strdup(new_line);
                if (inserted->text == NULL) {
                    free(inserted);
                    free_lines(head);
                    return 0;
                }
                inserted->next = cur;
                if (head == cur) {
                    head = inserted;
                } else {
                    mi_ini_line_t* prev = head;
                    while (prev != NULL && prev->next != cur) {
                        prev = prev->next;
                    }
                    if (prev != NULL) {
                        prev->next = inserted;
                    }
                }
                wrote_key = 1;
            }
            in_section = strcmp(parsed_section, section) == 0;
            if (in_section) {
                found_section = 1;
            }
            continue;
        }
        if (in_section && key_matches(cur->text, key)) {
            char* replacement = mi_ini_strdup(new_line);
            if (replacement == NULL) {
                free_lines(head);
                return 0;
            }
            free(cur->text);
            cur->text = replacement;
            wrote_key = 1;
        }
    }

    if (found_section && !wrote_key) {
        if (!append_line(&head, &tail, new_line)) {
            free_lines(head);
            return 0;
        }
        wrote_key = 1;
    }
    if (!found_section) {
        if (tail != NULL && tail->text != NULL) {
            size_t len = strlen(tail->text);
            if (len > 0 && tail->text[len - 1u] != '\n') {
                if (!append_line(&head, &tail, "\n")) {
                    free_lines(head);
                    return 0;
                }
            }
        }
        if (!append_line(&head, &tail, new_section) || !append_line(&head, &tail, new_line)) {
            free_lines(head);
            return 0;
        }
    }

    if (!save_lines(file, head)) {
        free_lines(head);
        return 0;
    }
    free_lines(head);
    (void)wrote_key;
    return 1;
}

int ini_write_int(const char* section, const char* key, int val, const char* file)
{
    char value[32];
    snprintf(value, sizeof(value), "%d", val);
    return ini_write_string(section, key, value, file);
}

int IniReadValue(char* section, const char* key, char* val, const char* file)
{
    char plain_section[MI_INI_SECTION_MAX + 1];
    char temp[MI_INI_VALUE_MAX + 1];
    int ret;
    size_t len;

    if (section == NULL || val == NULL) {
        return 0;
    }
    len = strlen(section);
    if (len >= 2u && section[0] == '[' && section[len - 1u] == ']') {
        len -= 2u;
        if (len >= sizeof(plain_section)) {
            return 0;
        }
        memcpy(plain_section, section + 1, len);
        plain_section[len] = '\0';
        ret = ini_read_string(plain_section, key, temp, sizeof(temp), file);
    } else {
        ret = ini_read_string(section, key, temp, sizeof(temp), file);
    }
    if (ret) {
        strcpy(val, temp);
    } else {
        val[0] = '\0';
    }
    return ret;
}

int readStringValue(const char* section, const char* key, char* val, const char* file)
{
    char temp[MI_INI_VALUE_MAX + 1];
    int ret;
    if (val == NULL) {
        return 0;
    }
    ret = ini_read_string(section, key, temp, sizeof(temp), file);
    if (ret) {
        strcpy(val, temp);
    } else {
        val[0] = '\0';
    }
    return ret;
}

int readIntValue(const char* section, const char* key, const char* file, int* outvalue)
{
    return ini_read_int(section, key, file, outvalue);
}

void IniWriteValue(const char* section, const char* key, const char* val, const char* file)
{
    char plain_section[MI_INI_SECTION_MAX + 1];
    size_t len;

    if (section == NULL) {
        return;
    }
    len = strlen(section);
    if (len >= 2u && section[0] == '[' && section[len - 1u] == ']') {
        len -= 2u;
        if (len >= sizeof(plain_section)) {
            return;
        }
        memcpy(plain_section, section + 1, len);
        plain_section[len] = '\0';
        ini_write_string(plain_section, key, val, file);
        return;
    }
    ini_write_string(section, key, val, file);
}

int writeStringValue(const char* section, const char* key, const char* val, const char* file)
{
    return ini_write_string(section, key, val, file);
}

int writeStringVlaue(const char* section, const char* key, const char* val, const char* file)
{
    return writeStringValue(section, key, val, file);
}

int writeIntValue(const char* section, const char* key, int val, const char* file)
{
    return ini_write_int(section, key, val, file);
}
