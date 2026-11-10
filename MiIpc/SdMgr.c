#include "SdMgr.h"

#include "../MiBase/include/mibase.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#if MI_BASE_OS_POSIX && defined(__linux__) && !defined(MIIPC_TXW82X_MINIMAL)
#define MIIPC_SD_MGR_LINUX 1
#include <mntent.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#define MIIPC_SD_MGR_LINUX 0
#endif

static void miipc_sd_mgr_copy_text(char* out, int out_len, const char* text)
{
    if (out == NULL || out_len <= 0) {
        return;
    }
    snprintf(out, (size_t)out_len, "%s", text != NULL ? text : "");
    out[out_len - 1] = '\0';
}

const char* miipc_sd_mgr_fs_type_name(miipc_sd_mgr_fs_type_t fs_type)
{
    switch (fs_type) {
    case MIIPC_SD_MGR_FS_FAT32:
        return "vfat";
    case MIIPC_SD_MGR_FS_EXFAT:
        return "exfat";
    case MIIPC_SD_MGR_FS_AUTO:
        return "auto";
    default:
        return "unknown";
    }
}

const char* miipc_sd_mgr_result_text(int result)
{
    switch (result) {
    case MIIPC_SD_MGR_OK:
        return "ok";
    case MIIPC_SD_MGR_ERR_INVALID_ARGUMENT:
        return "invalid argument";
    case MIIPC_SD_MGR_ERR_UNSUPPORTED:
        return "unsupported platform";
    case MIIPC_SD_MGR_ERR_NOT_FOUND:
        return "device not found";
    case MIIPC_SD_MGR_ERR_INVALID_DEVICE:
        return "invalid block device";
    case MIIPC_SD_MGR_ERR_INVALID_MOUNT_PATH:
        return "invalid mount path";
    case MIIPC_SD_MGR_ERR_BUSY:
        return "device or mount path is busy";
    case MIIPC_SD_MGR_ERR_NOT_MOUNTED:
        return "mount path is not mounted";
    case MIIPC_SD_MGR_ERR_MOUNT:
        return "mount failed";
    case MIIPC_SD_MGR_ERR_UNMOUNT:
        return "unmount failed";
    case MIIPC_SD_MGR_ERR_FORMAT:
        return "format failed";
    case MIIPC_SD_MGR_ERR_FS_ERROR:
        return "filesystem check reported errors";
    case MIIPC_SD_MGR_ERR_TOOL_NOT_FOUND:
        return "filesystem tool not found";
    case MIIPC_SD_MGR_ERR_SYSTEM:
        return "system error";
    default:
        return "unknown error";
    }
}

void miipc_sd_mgr_default_mount_config(miipc_sd_mgr_mount_config_t* config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->fs_type = MIIPC_SD_MGR_FS_AUTO;
    config->create_mount_path = 1;
}

#if MIIPC_SD_MGR_LINUX

typedef struct miipc_sd_mgr_mount_entry {
    char source[MIIPC_SD_MGR_PATH_MAX];
    char path[MIIPC_SD_MGR_PATH_MAX];
    char fs_type[MIIPC_SD_MGR_FS_TYPE_MAX];
} miipc_sd_mgr_mount_entry_t;

static pthread_mutex_t g_miipc_sd_mgr_lock = PTHREAD_MUTEX_INITIALIZER;

static int miipc_sd_mgr_has_control_char(const char* text)
{
    const unsigned char* p = (const unsigned char*)text;

    if (text == NULL || text[0] == '\0') {
        return 1;
    }
    while (*p != '\0') {
        if (*p < 0x20u || *p == 0x7fu) {
            return 1;
        }
        ++p;
    }
    return 0;
}

static int miipc_sd_mgr_has_dot_component(const char* path)
{
    const char* part;

    if (path == NULL) {
        return 1;
    }
    part = path;
    while (*part != '\0') {
        const char* end;
        int len;

        while (*part == '/') {
            ++part;
        }
        if (*part == '\0') {
            break;
        }
        end = part;
        while (*end != '\0' && *end != '/') {
            ++end;
        }
        len = (int)(end - part);
        if ((len == 1 && part[0] == '.') ||
            (len == 2 && part[0] == '.' && part[1] == '.')) {
            return 1;
        }
        part = end;
    }
    return 0;
}

static int miipc_sd_mgr_is_valid_absolute_path(const char* path)
{
    size_t len;

    if (path == NULL || path[0] != '/' || miipc_sd_mgr_has_control_char(path) ||
        miipc_sd_mgr_has_dot_component(path)) {
        return 0;
    }
    len = strlen(path);
    return len > 1u && len < MIIPC_SD_MGR_PATH_MAX;
}

static int miipc_sd_mgr_is_valid_device_path(const char* device)
{
    return miipc_sd_mgr_is_valid_absolute_path(device) &&
           strncmp(device, "/dev/", 5) == 0;
}

static int miipc_sd_mgr_validate_block_device(const char* device)
{
    struct stat st;

    if (!miipc_sd_mgr_is_valid_device_path(device)) {
        errno = EINVAL;
        return MIIPC_SD_MGR_ERR_INVALID_DEVICE;
    }
    if (stat(device, &st) != 0) {
        return errno == ENOENT ? MIIPC_SD_MGR_ERR_NOT_FOUND : MIIPC_SD_MGR_ERR_SYSTEM;
    }
    if (!S_ISBLK(st.st_mode)) {
        errno = ENOTBLK;
        return MIIPC_SD_MGR_ERR_INVALID_DEVICE;
    }
    return MIIPC_SD_MGR_OK;
}

static int miipc_sd_mgr_is_mmc_partition_name(const char* name)
{
    const char* p;

    if (name == NULL || strncmp(name, "mmcblk", 6) != 0) {
        return 0;
    }
    p = name + 6;
    if (*p < '0' || *p > '9') {
        return 0;
    }
    while (*p >= '0' && *p <= '9') {
        ++p;
    }
    if (*p++ != 'p' || *p < '1' || *p > '9') {
        return 0;
    }
    while (*p >= '0' && *p <= '9') {
        ++p;
    }
    return *p == '\0';
}

static int miipc_sd_mgr_device_same(const char* left, const char* right)
{
    struct stat left_st;
    struct stat right_st;

    if (left == NULL || right == NULL || left[0] == '\0' || right[0] == '\0') {
        return 0;
    }
    if (strcmp(left, right) == 0) {
        return 1;
    }
    if (stat(left, &left_st) == 0 && stat(right, &right_st) == 0 &&
        S_ISBLK(left_st.st_mode) && S_ISBLK(right_st.st_mode) &&
        left_st.st_rdev == right_st.st_rdev) {
        return 1;
    }
    return 0;
}

static int miipc_sd_mgr_device_contains(const char* whole_device, const char* source)
{
    size_t len;
    const char* base;
    const char* suffix;

    if (whole_device == NULL || source == NULL) {
        return 0;
    }
    len = strlen(whole_device);
    if (len == 0 || strncmp(whole_device, source, len) != 0) {
        return 0;
    }
    suffix = source + len;
    if (*suffix == 'p') {
        ++suffix;
    } else {
        base = strrchr(whole_device, '/');
        base = base != NULL ? base + 1 : whole_device;
        if (base[0] != '\0' && base[strlen(base) - 1u] >= '0' &&
            base[strlen(base) - 1u] <= '9') {
            return 0;
        }
    }
    if (*suffix == '\0') {
        return 0;
    }
    while (*suffix != '\0') {
        if (*suffix < '0' || *suffix > '9') {
            return 0;
        }
        ++suffix;
    }
    return 1;
}

static int miipc_sd_mgr_device_matches_or_contains(const char* device, const char* source)
{
    char device_real[MIIPC_SD_MGR_PATH_MAX];
    char source_real[MIIPC_SD_MGR_PATH_MAX];
    const char* left = device;
    const char* right = source;

    if (miipc_sd_mgr_device_same(device, source)) {
        return 1;
    }
    if (device != NULL && source != NULL &&
        realpath(device, device_real) != NULL && realpath(source, source_real) != NULL) {
        left = device_real;
        right = source_real;
        if (miipc_sd_mgr_device_same(left, right)) {
            return 1;
        }
    }
    return miipc_sd_mgr_device_contains(left, right);
}

static int miipc_sd_mgr_find_mount(const char* device,
                                   const char* mount_path,
                                   miipc_sd_mgr_mount_entry_t* entry)
{
    FILE* fp;
    struct mntent* mnt;

    if ((device == NULL || device[0] == '\0') &&
        (mount_path == NULL || mount_path[0] == '\0')) {
        errno = EINVAL;
        return -1;
    }
    fp = setmntent("/proc/self/mounts", "r");
    if (fp == NULL) {
        return -1;
    }
    while ((mnt = getmntent(fp)) != NULL) {
        int matched = 0;

        if (device != NULL && device[0] != '\0' &&
            miipc_sd_mgr_device_matches_or_contains(device, mnt->mnt_fsname)) {
            matched = 1;
        }
        if (mount_path != NULL && mount_path[0] != '\0' &&
            strcmp(mount_path, mnt->mnt_dir) == 0) {
            matched = 1;
        }
        if (matched) {
            if (entry != NULL) {
                memset(entry, 0, sizeof(*entry));
                miipc_sd_mgr_copy_text(entry->source, sizeof(entry->source), mnt->mnt_fsname);
                miipc_sd_mgr_copy_text(entry->path, sizeof(entry->path), mnt->mnt_dir);
                miipc_sd_mgr_copy_text(entry->fs_type, sizeof(entry->fs_type), mnt->mnt_type);
            }
            endmntent(fp);
            return 1;
        }
    }
    endmntent(fp);
    return 0;
}

static int miipc_sd_mgr_ensure_mount_path(const char* mount_path, int create_path)
{
    char path[MIIPC_SD_MGR_PATH_MAX];
    char* p;
    struct stat st;

    if (!miipc_sd_mgr_is_valid_absolute_path(mount_path)) {
        errno = EINVAL;
        return MIIPC_SD_MGR_ERR_INVALID_MOUNT_PATH;
    }
    if (lstat(mount_path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return MIIPC_SD_MGR_OK;
        }
        errno = ENOTDIR;
        return MIIPC_SD_MGR_ERR_INVALID_MOUNT_PATH;
    }
    if (errno != ENOENT || !create_path) {
        return MIIPC_SD_MGR_ERR_INVALID_MOUNT_PATH;
    }

    miipc_sd_mgr_copy_text(path, sizeof(path), mount_path);
    for (p = path + 1; *p != '\0'; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(path, 0755) != 0 && errno != EEXIST) {
                return MIIPC_SD_MGR_ERR_SYSTEM;
            }
            *p = '/';
        }
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        return MIIPC_SD_MGR_ERR_SYSTEM;
    }
    if (lstat(mount_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return MIIPC_SD_MGR_ERR_INVALID_MOUNT_PATH;
    }
    return MIIPC_SD_MGR_OK;
}

static unsigned long miipc_sd_mgr_default_mount_flags(void)
{
    unsigned long flags = MS_NOSUID | MS_NODEV | MS_NOEXEC;

#ifdef MS_NOATIME
    flags |= MS_NOATIME;
#endif
    return flags;
}

/* Add each future custom filesystem here and to the format/check dispatchers. */
static int miipc_sd_mgr_is_supported_fs_type(miipc_sd_mgr_fs_type_t fs_type)
{
    switch (fs_type) {
    case MIIPC_SD_MGR_FS_FAT32:
    case MIIPC_SD_MGR_FS_EXFAT:
        return 1;
    default:
        return 0;
    }
}

static int miipc_sd_mgr_require_unmounted(const char* device)
{
    int ret = miipc_sd_mgr_find_mount(device, NULL, NULL);

    if (ret < 0) {
        return MIIPC_SD_MGR_ERR_SYSTEM;
    }
    if (ret > 0) {
        errno = EBUSY;
        return MIIPC_SD_MGR_ERR_BUSY;
    }
    return MIIPC_SD_MGR_OK;
}

static int miipc_sd_mgr_label_valid(const char* label, miipc_sd_mgr_fs_type_t fs_type)
{
    int max_len;
    size_t len;
    const unsigned char* p;

    if (label == NULL || label[0] == '\0') {
        return 1;
    }
    if (fs_type == MIIPC_SD_MGR_FS_FAT32) {
        max_len = 11;
    } else if (fs_type == MIIPC_SD_MGR_FS_EXFAT) {
        max_len = 15;
    } else {
        return 0;
    }
    len = strlen(label);
    if (len > (size_t)max_len) {
        return 0;
    }
    p = (const unsigned char*)label;
    while (*p != '\0') {
        if (*p < 0x20u || *p == 0x7fu || *p == '/' || *p == '\\') {
            return 0;
        }
        ++p;
    }
    return 1;
}

static int miipc_sd_mgr_run_tool(const char* const* tools,
                                 char* argv[],
                                 int* exit_code)
{
    int i;

    if (tools == NULL || argv == NULL || exit_code == NULL) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0; tools[i] != NULL; ++i) {
        pid_t pid;
        int status;
        pid_t waited;

        if (access(tools[i], X_OK) != 0) {
            continue;
        }
        pid = fork();
        if (pid < 0) {
            return -1;
        }
        if (pid == 0) {
            argv[0] = (char*)tools[i];
            execv(tools[i], argv);
            _exit(127);
        }
        do {
            waited = waitpid(pid, &status, 0);
        } while (waited < 0 && errno == EINTR);
        if (waited < 0) {
            return -1;
        }
        if (!WIFEXITED(status)) {
            *exit_code = 128;
        } else {
            *exit_code = WEXITSTATUS(status);
        }
        return 0;
    }
    errno = ENOENT;
    return -1;
}

static int miipc_sd_mgr_format_type(const char* device,
                                    miipc_sd_mgr_fs_type_t fs_type,
                                    const char* label,
                                    int* tool_exit_code)
{
    static const char* const vfat_tools[] = {
        "/sbin/mkfs.fat", "/usr/sbin/mkfs.fat", "/sbin/mkfs.vfat", "/usr/sbin/mkfs.vfat", NULL
    };
    static const char* const exfat_tools[] = {
        "/sbin/mkfs.exfat", "/usr/sbin/mkfs.exfat", NULL
    };
    const char* const* tools;
    char* argv[7];
    int argc = 0;

    if (device == NULL || tool_exit_code == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(argv, 0, sizeof(argv));
    argv[argc++] = NULL;
    if (fs_type == MIIPC_SD_MGR_FS_FAT32) {
        tools = vfat_tools;
        argv[argc++] = "-F";
        argv[argc++] = "32";
        if (label != NULL && label[0] != '\0') {
            argv[argc++] = "-n";
            argv[argc++] = (char*)label;
        }
    } else if (fs_type == MIIPC_SD_MGR_FS_EXFAT) {
        tools = exfat_tools;
        if (label != NULL && label[0] != '\0') {
            argv[argc++] = "-n";
            argv[argc++] = (char*)label;
        }
    } else {
        errno = EINVAL;
        return -1;
    }
    argv[argc++] = (char*)device;
    argv[argc] = NULL;
    return miipc_sd_mgr_run_tool(tools, argv, tool_exit_code);
}

static int miipc_sd_mgr_check_type(const char* device,
                                   miipc_sd_mgr_fs_type_t fs_type,
                                   int* tool_exit_code)
{
    static const char* const vfat_tools[] = {
        "/sbin/fsck.fat", "/usr/sbin/fsck.fat", "/sbin/fsck.vfat", "/usr/sbin/fsck.vfat",
        "/sbin/dosfsck", "/usr/sbin/dosfsck", NULL
    };
    static const char* const exfat_tools[] = {
        "/sbin/fsck.exfat", "/usr/sbin/fsck.exfat", NULL
    };
    const char* const* tools;
    char* argv[4];

    if (device == NULL || tool_exit_code == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(argv, 0, sizeof(argv));
    argv[0] = NULL;
    if (fs_type == MIIPC_SD_MGR_FS_FAT32) {
        tools = vfat_tools;
        argv[1] = "-n";
        argv[2] = (char*)device;
    } else if (fs_type == MIIPC_SD_MGR_FS_EXFAT) {
        tools = exfat_tools;
        argv[1] = "-n";
        argv[2] = (char*)device;
    } else {
        errno = EINVAL;
        return -1;
    }
    argv[3] = NULL;
    return miipc_sd_mgr_run_tool(tools, argv, tool_exit_code);
}

#endif

int miipc_sd_mgr_find_mmc_partition(char* device, int device_len)
{
    if (device == NULL || device_len <= 0) {
        return MIIPC_SD_MGR_ERR_INVALID_ARGUMENT;
    }
    device[0] = '\0';

#if MIIPC_SD_MGR_LINUX
    {
        FILE* fp;
        char line[512];
        char name[MIIPC_SD_MGR_PATH_MAX];
        char candidate[MIIPC_SD_MGR_PATH_MAX];
        int saved_errno;

        if (device_len < 6) {
            errno = ERANGE;
            return MIIPC_SD_MGR_ERR_INVALID_ARGUMENT;
        }
        fp = fopen("/proc/partitions", "r");
        if (fp == NULL) {
            return errno == ENOENT ? MIIPC_SD_MGR_ERR_NOT_FOUND : MIIPC_SD_MGR_ERR_SYSTEM;
        }
        saved_errno = 0;
        while (fgets(line, sizeof(line), fp) != NULL) {
            unsigned int major_number;
            unsigned int minor_number;
            unsigned long long block_count;
            struct stat st;
            int parsed;
            int length;

            memset(name, 0, sizeof(name));
            parsed = sscanf(line,
                            " %u %u %llu %255s",
                            &major_number,
                            &minor_number,
                            &block_count,
                            name);
            if (parsed != 4 || !miipc_sd_mgr_is_mmc_partition_name(name)) {
                continue;
            }
            (void)major_number;
            (void)minor_number;
            (void)block_count;
            length = snprintf(candidate, sizeof(candidate), "/dev/%s", name);
            if (length <= 0 || length >= (int)sizeof(candidate) ||
                length >= device_len) {
                continue;
            }
            if (stat(candidate, &st) != 0) {
                saved_errno = errno;
                continue;
            }
            if (!S_ISBLK(st.st_mode)) {
                continue;
            }
            memcpy(device, candidate, (size_t)length + 1u);
            fclose(fp);
            return MIIPC_SD_MGR_OK;
        }
        saved_errno = saved_errno != 0 ? saved_errno : ENOENT;
        fclose(fp);
        errno = saved_errno;
        return MIIPC_SD_MGR_ERR_NOT_FOUND;
    }
#else
    (void)device_len;
    return MIIPC_SD_MGR_ERR_UNSUPPORTED;
#endif
}

int miipc_sd_mgr_get_status(const char* device,
                            const char* mount_path,
                            miipc_sd_mgr_status_t* status)
{
    if (status == NULL) {
        return MIIPC_SD_MGR_ERR_INVALID_ARGUMENT;
    }
    memset(status, 0, sizeof(*status));
    miipc_sd_mgr_copy_text(status->device, sizeof(status->device), device);
    miipc_sd_mgr_copy_text(status->mount_path, sizeof(status->mount_path), mount_path);

#if MIIPC_SD_MGR_LINUX
    {
        struct stat st;
        miipc_sd_mgr_mount_entry_t entry;
        int mounted;

        if ((device == NULL || device[0] == '\0') &&
            (mount_path == NULL || mount_path[0] == '\0')) {
            return MIIPC_SD_MGR_ERR_INVALID_ARGUMENT;
        }
        if (device != NULL && device[0] != '\0' && !miipc_sd_mgr_is_valid_device_path(device)) {
            return MIIPC_SD_MGR_ERR_INVALID_DEVICE;
        }
        if (mount_path != NULL && mount_path[0] != '\0' &&
            !miipc_sd_mgr_is_valid_absolute_path(mount_path)) {
            return MIIPC_SD_MGR_ERR_INVALID_MOUNT_PATH;
        }
        if (device != NULL && device[0] != '\0') {
            if (stat(device, &st) == 0) {
                status->device_exists = 1;
                status->device_is_block = S_ISBLK(st.st_mode) ? 1 : 0;
            } else {
                status->system_errno = errno;
            }
        }
        if (mount_path != NULL && mount_path[0] != '\0') {
            if (stat(mount_path, &st) == 0) {
                status->mount_path_exists = S_ISDIR(st.st_mode) ? 1 : 0;
            } else if (status->system_errno == 0) {
                status->system_errno = errno;
            }
        }
        memset(&entry, 0, sizeof(entry));
        mounted = miipc_sd_mgr_find_mount(device, mount_path, &entry);
        if (mounted < 0) {
            status->system_errno = errno;
            return MIIPC_SD_MGR_ERR_SYSTEM;
        }
        if (mounted > 0) {
            struct statvfs fs;

            status->mounted = 1;
            miipc_sd_mgr_copy_text(status->mount_path, sizeof(status->mount_path), entry.path);
            miipc_sd_mgr_copy_text(status->fs_type, sizeof(status->fs_type), entry.fs_type);
            if (statvfs(entry.path, &fs) == 0) {
                status->total_bytes = (uint64_t)fs.f_frsize * (uint64_t)fs.f_blocks;
                status->free_bytes = (uint64_t)fs.f_frsize * (uint64_t)fs.f_bavail;
#ifdef ST_RDONLY
                status->read_only = (fs.f_flag & ST_RDONLY) != 0 ? 1 : 0;
#endif
                status->writable = status->read_only ? 0 : 1;
            } else {
                status->system_errno = errno;
            }
        }
        return MIIPC_SD_MGR_OK;
    }
#else
    (void)device;
    (void)mount_path;
    return MIIPC_SD_MGR_ERR_UNSUPPORTED;
#endif
}

int miipc_sd_mgr_mount(const miipc_sd_mgr_mount_config_t* config)
{
#if MIIPC_SD_MGR_LINUX
    miipc_sd_mgr_mount_entry_t mounted_entry;
    miipc_sd_mgr_fs_type_t candidates[2];
    int candidate_count = 0;
    int i;
    int ret;
    unsigned long flags;
    int saved_errno = EINVAL;

    if (config == NULL || config->device == NULL || config->mount_path == NULL) {
        return MIIPC_SD_MGR_ERR_INVALID_ARGUMENT;
    }
    ret = miipc_sd_mgr_validate_block_device(config->device);
    if (ret != MIIPC_SD_MGR_OK) {
        return ret;
    }
    ret = miipc_sd_mgr_ensure_mount_path(config->mount_path, config->create_mount_path);
    if (ret != MIIPC_SD_MGR_OK) {
        return ret;
    }
    if (config->fs_type != MIIPC_SD_MGR_FS_AUTO &&
        !miipc_sd_mgr_is_supported_fs_type(config->fs_type)) {
        errno = EINVAL;
        return MIIPC_SD_MGR_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&g_miipc_sd_mgr_lock);
    memset(&mounted_entry, 0, sizeof(mounted_entry));
    ret = miipc_sd_mgr_find_mount(config->device, config->mount_path, &mounted_entry);
    if (ret < 0) {
        pthread_mutex_unlock(&g_miipc_sd_mgr_lock);
        return MIIPC_SD_MGR_ERR_SYSTEM;
    }
    if (ret > 0) {
        if (strcmp(mounted_entry.path, config->mount_path) == 0 &&
            miipc_sd_mgr_device_same(config->device, mounted_entry.source)) {
            pthread_mutex_unlock(&g_miipc_sd_mgr_lock);
            return MIIPC_SD_MGR_OK;
        }
        errno = EBUSY;
        pthread_mutex_unlock(&g_miipc_sd_mgr_lock);
        return MIIPC_SD_MGR_ERR_BUSY;
    }

    if (config->fs_type == MIIPC_SD_MGR_FS_AUTO) {
        candidates[candidate_count++] = MIIPC_SD_MGR_FS_EXFAT;
        candidates[candidate_count++] = MIIPC_SD_MGR_FS_FAT32;
    } else {
        candidates[candidate_count++] = config->fs_type;
    }
    flags = config->mount_flags != 0 ? config->mount_flags : miipc_sd_mgr_default_mount_flags();
    for (i = 0; i < candidate_count; ++i) {
        if (mount(config->device,
                  config->mount_path,
                  miipc_sd_mgr_fs_type_name(candidates[i]),
                  flags,
                  NULL) == 0) {
            pthread_mutex_unlock(&g_miipc_sd_mgr_lock);
            return MIIPC_SD_MGR_OK;
        }
        saved_errno = errno;
    }
    errno = saved_errno;
    pthread_mutex_unlock(&g_miipc_sd_mgr_lock);
    return MIIPC_SD_MGR_ERR_MOUNT;
#else
    (void)config;
    return MIIPC_SD_MGR_ERR_UNSUPPORTED;
#endif
}

int miipc_sd_mgr_unmount(const char* mount_path)
{
#if MIIPC_SD_MGR_LINUX
    int ret;

    if (!miipc_sd_mgr_is_valid_absolute_path(mount_path)) {
        errno = EINVAL;
        return MIIPC_SD_MGR_ERR_INVALID_MOUNT_PATH;
    }
    pthread_mutex_lock(&g_miipc_sd_mgr_lock);
    ret = miipc_sd_mgr_find_mount(NULL, mount_path, NULL);
    if (ret < 0) {
        pthread_mutex_unlock(&g_miipc_sd_mgr_lock);
        return MIIPC_SD_MGR_ERR_SYSTEM;
    }
    if (ret == 0) {
        pthread_mutex_unlock(&g_miipc_sd_mgr_lock);
        return MIIPC_SD_MGR_ERR_NOT_MOUNTED;
    }
    sync();
    if (umount2(mount_path, 0) != 0) {
        ret = errno == EBUSY ? MIIPC_SD_MGR_ERR_BUSY : MIIPC_SD_MGR_ERR_UNMOUNT;
        pthread_mutex_unlock(&g_miipc_sd_mgr_lock);
        return ret;
    }
    pthread_mutex_unlock(&g_miipc_sd_mgr_lock);
    return MIIPC_SD_MGR_OK;
#else
    (void)mount_path;
    return MIIPC_SD_MGR_ERR_UNSUPPORTED;
#endif
}

int miipc_sd_mgr_format(const char* device,
                        miipc_sd_mgr_fs_type_t fs_type,
                        const char* label)
{
#if MIIPC_SD_MGR_LINUX
    int ret;
    int tool_exit_code = 0;

    ret = miipc_sd_mgr_validate_block_device(device);
    if (ret != MIIPC_SD_MGR_OK) {
        return ret;
    }
    if (fs_type == MIIPC_SD_MGR_FS_AUTO) {
        fs_type = MIIPC_SD_MGR_FS_EXFAT;
    }
    if (!miipc_sd_mgr_is_supported_fs_type(fs_type) ||
        !miipc_sd_mgr_label_valid(label, fs_type)) {
        errno = EINVAL;
        return MIIPC_SD_MGR_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&g_miipc_sd_mgr_lock);
    ret = miipc_sd_mgr_require_unmounted(device);
    if (ret == MIIPC_SD_MGR_OK) {
        sync();
        if (miipc_sd_mgr_format_type(device, fs_type, label, &tool_exit_code) != 0) {
            ret = errno == ENOENT ? MIIPC_SD_MGR_ERR_TOOL_NOT_FOUND : MIIPC_SD_MGR_ERR_SYSTEM;
        } else if (tool_exit_code != 0) {
            ret = MIIPC_SD_MGR_ERR_FORMAT;
        }
    }
    pthread_mutex_unlock(&g_miipc_sd_mgr_lock);
    return ret;
#else
    (void)device;
    (void)fs_type;
    (void)label;
    return MIIPC_SD_MGR_ERR_UNSUPPORTED;
#endif
}

int miipc_sd_mgr_check_filesystem(const char* device,
                                  miipc_sd_mgr_fs_type_t fs_type,
                                  int* fsck_exit_code)
{
#if MIIPC_SD_MGR_LINUX
    int ret;
    int tool_exit_code = 0;

    if (fsck_exit_code != NULL) {
        *fsck_exit_code = -1;
    }
    ret = miipc_sd_mgr_validate_block_device(device);
    if (ret != MIIPC_SD_MGR_OK) {
        return ret;
    }
    if (fs_type == MIIPC_SD_MGR_FS_AUTO) {
        errno = EINVAL;
        return MIIPC_SD_MGR_ERR_INVALID_ARGUMENT;
    }
    if (!miipc_sd_mgr_is_supported_fs_type(fs_type)) {
        errno = EINVAL;
        return MIIPC_SD_MGR_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&g_miipc_sd_mgr_lock);
    ret = miipc_sd_mgr_require_unmounted(device);
    if (ret == MIIPC_SD_MGR_OK) {
        if (miipc_sd_mgr_check_type(device, fs_type, &tool_exit_code) != 0) {
            ret = errno == ENOENT ? MIIPC_SD_MGR_ERR_TOOL_NOT_FOUND : MIIPC_SD_MGR_ERR_SYSTEM;
        } else if (tool_exit_code != 0) {
            ret = MIIPC_SD_MGR_ERR_FS_ERROR;
        }
    }
    pthread_mutex_unlock(&g_miipc_sd_mgr_lock);
    if (fsck_exit_code != NULL) {
        *fsck_exit_code = tool_exit_code;
    }
    return ret;
#else
    (void)device;
    (void)fs_type;
    if (fsck_exit_code != NULL) {
        *fsck_exit_code = -1;
    }
    return MIIPC_SD_MGR_ERR_UNSUPPORTED;
#endif
}
