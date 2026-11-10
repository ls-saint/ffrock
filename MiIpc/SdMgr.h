#ifndef MIIPC_SD_MGR_H_
#define MIIPC_SD_MGR_H_

#include <stdint.h>

#if defined(_WIN32)
#define MIIPC_SD_MGR_API
#elif defined(__GNUC__)
#define MIIPC_SD_MGR_API __attribute__((visibility("default")))
#else
#define MIIPC_SD_MGR_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MIIPC_SD_MGR_PATH_MAX 256
#define MIIPC_SD_MGR_FS_TYPE_MAX 16

typedef enum miipc_sd_mgr_fs_type {
    MIIPC_SD_MGR_FS_AUTO = 0,
    MIIPC_SD_MGR_FS_FAT32,
    MIIPC_SD_MGR_FS_EXFAT
} miipc_sd_mgr_fs_type_t;

/* Future filesystem types are added as explicit enum values and dispatched by
 * SdMgr.c. Arbitrary filesystem/tool names are intentionally not accepted. */

/* Linux exposes FAT32 through the vfat driver. Keep this alias for callers
 * that use the kernel driver name. */
#define MIIPC_SD_MGR_FS_VFAT MIIPC_SD_MGR_FS_FAT32

typedef enum miipc_sd_mgr_result {
    MIIPC_SD_MGR_OK = 0,
    MIIPC_SD_MGR_ERR_INVALID_ARGUMENT = -1,
    MIIPC_SD_MGR_ERR_UNSUPPORTED = -2,
    MIIPC_SD_MGR_ERR_NOT_FOUND = -3,
    MIIPC_SD_MGR_ERR_INVALID_DEVICE = -4,
    MIIPC_SD_MGR_ERR_INVALID_MOUNT_PATH = -5,
    MIIPC_SD_MGR_ERR_BUSY = -6,
    MIIPC_SD_MGR_ERR_NOT_MOUNTED = -7,
    MIIPC_SD_MGR_ERR_MOUNT = -8,
    MIIPC_SD_MGR_ERR_UNMOUNT = -9,
    MIIPC_SD_MGR_ERR_FORMAT = -10,
    MIIPC_SD_MGR_ERR_FS_ERROR = -11,
    MIIPC_SD_MGR_ERR_TOOL_NOT_FOUND = -12,
    MIIPC_SD_MGR_ERR_SYSTEM = -13
} miipc_sd_mgr_result_t;

typedef struct miipc_sd_mgr_mount_config {
    const char* device;
    const char* mount_path;
    miipc_sd_mgr_fs_type_t fs_type;
    unsigned long mount_flags;
    int create_mount_path;
} miipc_sd_mgr_mount_config_t;

typedef struct miipc_sd_mgr_status {
    int device_exists;
    int device_is_block;
    int mount_path_exists;
    int mounted;
    int writable;
    int read_only;
    int system_errno;
    uint64_t total_bytes;
    uint64_t free_bytes;
    char device[MIIPC_SD_MGR_PATH_MAX];
    char mount_path[MIIPC_SD_MGR_PATH_MAX];
    char fs_type[MIIPC_SD_MGR_FS_TYPE_MAX];
} miipc_sd_mgr_status_t;

/* Finds the first usable MMC partition reported by /proc/partitions and
 * writes its device node (for example, /dev/mmcblk0p1) to device. */
MIIPC_SD_MGR_API int miipc_sd_mgr_find_mmc_partition(char* device, int device_len);

/* Zero mount_flags selects the module's safe default mount flags. */
MIIPC_SD_MGR_API void miipc_sd_mgr_default_mount_config(miipc_sd_mgr_mount_config_t* config);

/* Mounts a block-device filesystem. AUTO tries exFAT, then FAT32. */
MIIPC_SD_MGR_API int miipc_sd_mgr_mount(const miipc_sd_mgr_mount_config_t* config);

/* Performs a normal unmount only. It deliberately never uses lazy or force unmount. */
MIIPC_SD_MGR_API int miipc_sd_mgr_unmount(const char* mount_path);

/* Formats an unmounted block device. AUTO means exFAT. This operation is destructive. */
MIIPC_SD_MGR_API int miipc_sd_mgr_format(const char* device,
                                         miipc_sd_mgr_fs_type_t fs_type,
                                         const char* label);

/* Runs a non-destructive filesystem check on an unmounted block device. */
MIIPC_SD_MGR_API int miipc_sd_mgr_check_filesystem(const char* device,
                                                   miipc_sd_mgr_fs_type_t fs_type,
                                                   int* fsck_exit_code);

/* Reports device, mount, read-only, and capacity state without changing it. */
MIIPC_SD_MGR_API int miipc_sd_mgr_get_status(const char* device,
                                             const char* mount_path,
                                             miipc_sd_mgr_status_t* status);

MIIPC_SD_MGR_API const char* miipc_sd_mgr_fs_type_name(miipc_sd_mgr_fs_type_t fs_type);
MIIPC_SD_MGR_API const char* miipc_sd_mgr_result_text(int result);

#ifdef __cplusplus
}
#endif

#endif
