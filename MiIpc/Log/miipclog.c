#include "miipclog.h"
#include "../../MiBase/include/mibase.h"
#include "../../MiBase/include/mimutex.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if MI_BASE_OS_WINDOWS
#include <Windows.h>
#endif

#define MIIPC_LOG_PATH_MAX 260
#define MIIPC_LOG_LINE_MAX 1024

typedef struct miipc_log_state {
    mi_Mutex* lock;
    FILE* file;
    int initialized;
    int console_enabled;
    int atexit_registered;
    char path[MIIPC_LOG_PATH_MAX];
} miipc_log_state_t;

static miipc_log_state_t g_miipc_log;

static void miipc_log_copy_path(char* dst, int dst_len, const char* src)
{
    if (dst == NULL || dst_len <= 0) {
        return;
    }
    if (src == NULL || src[0] == '\0') {
        src = MIIPC_LOG_DEFAULT_PATH;
    }
    strncpy(dst, src, (size_t)dst_len - 1u);
    dst[dst_len - 1] = '\0';
}

static int miipc_log_ensure_lock(void)
{
    if (g_miipc_log.lock != NULL) {
        return 1;
    }
    g_miipc_log.lock = mi_CreateMutex(1);
    return g_miipc_log.lock != NULL;
}

static void miipc_log_lock(void)
{
    if (g_miipc_log.lock != NULL) {
        g_miipc_log.lock->Lock(g_miipc_log.lock);
    }
}

static void miipc_log_unlock(void)
{
    if (g_miipc_log.lock != NULL) {
        g_miipc_log.lock->UnLock(g_miipc_log.lock);
    }
}

static void miipc_log_make_time(char* out, int out_len)
{
    if (out == NULL || out_len <= 0) {
        return;
    }
    out[0] = '\0';

#if MI_BASE_OS_WINDOWS
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        snprintf(out,
                 (size_t)out_len,
                 "%04u-%02u-%02u %02u:%02u:%02u.%03u",
                 (unsigned int)st.wYear,
                 (unsigned int)st.wMonth,
                 (unsigned int)st.wDay,
                 (unsigned int)st.wHour,
                 (unsigned int)st.wMinute,
                 (unsigned int)st.wSecond,
                 (unsigned int)st.wMilliseconds);
    }
#elif MI_BASE_OS_POSIX
    {
        time_t now;
        struct tm tmv;
        now = time(NULL);
        if (localtime_r(&now, &tmv) != NULL) {
            snprintf(out,
                     (size_t)out_len,
                     "%04d-%02d-%02d %02d:%02d:%02d.%03u",
                     tmv.tm_year + 1900,
                     tmv.tm_mon + 1,
                     tmv.tm_mday,
                     tmv.tm_hour,
                     tmv.tm_min,
                     tmv.tm_sec,
                     (unsigned int)(mi_getTick32() % 1000u));
        }
    }
#else
    snprintf(out, (size_t)out_len, "tick:%u", (unsigned int)mi_getTick32());
#endif

    out[out_len - 1] = '\0';
}

static long miipc_log_file_size(FILE* file)
{
    long cur;
    long size;

    if (file == NULL) {
        return 0;
    }
    cur = ftell(file);
    if (cur < 0) {
        cur = 0;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        return 0;
    }
    size = ftell(file);
    if (size < 0) {
        size = 0;
    }
    (void)fseek(file, cur, SEEK_SET);
    return size;
}

static int miipc_log_open_locked(void)
{
    if (g_miipc_log.file != NULL) {
        return 1;
    }
    if (g_miipc_log.path[0] == '\0') {
        miipc_log_copy_path(g_miipc_log.path, sizeof(g_miipc_log.path), MIIPC_LOG_DEFAULT_PATH);
    }
    g_miipc_log.file = fopen(g_miipc_log.path, "ab+");
    return g_miipc_log.file != NULL;
}

static void miipc_log_rotate_locked(void)
{
    FILE* in;
    FILE* out;
    uint8_t* keep_buf;
    long size;
    long keep_size;
    long read_size;

    if (g_miipc_log.file != NULL) {
        fflush(g_miipc_log.file);
        size = miipc_log_file_size(g_miipc_log.file);
        if (size <= (long)MIIPC_LOG_MAX_BYTES) {
            return;
        }
        fclose(g_miipc_log.file);
        g_miipc_log.file = NULL;
    } else {
        in = fopen(g_miipc_log.path, "rb");
        if (in == NULL) {
            return;
        }
        if (fseek(in, 0, SEEK_END) != 0) {
            fclose(in);
            return;
        }
        size = ftell(in);
        fclose(in);
        if (size <= (long)MIIPC_LOG_MAX_BYTES) {
            return;
        }
    }

    keep_size = size > (long)MIIPC_LOG_KEEP_BYTES ? (long)MIIPC_LOG_KEEP_BYTES : size;
    if (keep_size <= 0) {
        return;
    }
    keep_buf = (uint8_t*)mi_mem_malloc((int)keep_size);
    if (keep_buf == NULL) {
        return;
    }

    in = fopen(g_miipc_log.path, "rb");
    if (in == NULL) {
        mi_mem_free(keep_buf);
        return;
    }
    if (fseek(in, size - keep_size, SEEK_SET) != 0) {
        fclose(in);
        mi_mem_free(keep_buf);
        return;
    }
    read_size = (long)fread(keep_buf, 1u, (size_t)keep_size, in);
    fclose(in);
    if (read_size <= 0) {
        mi_mem_free(keep_buf);
        return;
    }

    out = fopen(g_miipc_log.path, "wb");
    if (out != NULL) {
        (void)fwrite(keep_buf, 1u, (size_t)read_size, out);
        fclose(out);
    }
    mi_mem_free(keep_buf);
    g_miipc_log.file = fopen(g_miipc_log.path, "ab+");
}

int miipc_log_init(const char* path)
{
    if (!miipc_log_ensure_lock()) {
        return -1;
    }

    miipc_log_lock();
    if (g_miipc_log.initialized) {
        miipc_log_unlock();
        return 0;
    }
    miipc_log_copy_path(g_miipc_log.path, sizeof(g_miipc_log.path), path);
    g_miipc_log.console_enabled = 0;
    g_miipc_log.initialized = 1;
    if (!g_miipc_log.atexit_registered) {
        atexit(miipc_log_close);
        g_miipc_log.atexit_registered = 1;
    }
    (void)miipc_log_open_locked();
    miipc_log_rotate_locked();
    miipc_log_unlock();
    return 0;
}

void miipc_log_close(void)
{
    if (!miipc_log_ensure_lock()) {
        return;
    }

    miipc_log_lock();
    if (g_miipc_log.file != NULL) {
        fflush(g_miipc_log.file);
        fclose(g_miipc_log.file);
        g_miipc_log.file = NULL;
    }
    g_miipc_log.initialized = 0;
    miipc_log_unlock();
}

void miipc_log_set_console(int enable)
{
    if (!miipc_log_ensure_lock()) {
        return;
    }
    miipc_log_lock();
    g_miipc_log.console_enabled = enable ? 1 : 0;
    miipc_log_unlock();
}

void miipc_log_write(const char* level, const char* fmt, ...)
{
    char time_text[32];
    char msg[MIIPC_LOG_LINE_MAX];
    va_list ap;
    int console_enabled;

    if (fmt == NULL) {
        return;
    }
    if (!miipc_log_ensure_lock()) {
        return;
    }
    if (level == NULL || level[0] == '\0') {
        level = "INFO";
    }

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    msg[sizeof(msg) - 1u] = '\0';
    miipc_log_make_time(time_text, sizeof(time_text));

    miipc_log_lock();
    if (!g_miipc_log.initialized) {
        miipc_log_copy_path(g_miipc_log.path, sizeof(g_miipc_log.path), MIIPC_LOG_DEFAULT_PATH);
        g_miipc_log.initialized = 1;
    }
    if (miipc_log_open_locked()) {
        fprintf(g_miipc_log.file, "%s [%s] %s\n", time_text, level, msg);
        fflush(g_miipc_log.file);
        miipc_log_rotate_locked();
    }
    console_enabled = g_miipc_log.console_enabled;
    miipc_log_unlock();

    if (console_enabled) {
        printf("%s [%s] %s\n", time_text, level, msg);
        fflush(stdout);
    }
}

void miipc_log_rotate_now(void)
{
    if (!miipc_log_ensure_lock()) {
        return;
    }
    miipc_log_lock();
    if (!g_miipc_log.initialized) {
        miipc_log_copy_path(g_miipc_log.path, sizeof(g_miipc_log.path), MIIPC_LOG_DEFAULT_PATH);
        g_miipc_log.initialized = 1;
    }
    miipc_log_rotate_locked();
    miipc_log_unlock();
}
