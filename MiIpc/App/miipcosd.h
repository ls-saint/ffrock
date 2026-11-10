#ifndef MIIPC_OSD_H_
#define MIIPC_OSD_H_

#include "../JSON/cJSON.h"
#include "../Soc/IHard.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MIIPC_OSD_MAX_ENTRIES 4
#define MIIPC_OSD_TEXT_MAX 96

typedef struct miipc_osd_entry {
    int index;
    int configured;
    int show;
    int font_size;
    int align;
    int x;
    int y;
    int width;
    int height;
    int channel_width;
    int channel_height;
    char text[MIIPC_OSD_TEXT_MAX];
} miipc_osd_entry_t;

void miipc_osd_init(IHardSoc* hard_soc);
void miipc_osd_deinit(void);
void miipc_osd_set_hard_soc(IHardSoc* hard_soc);
int miipc_osd_apply_json(cJSON* root,
                         cJSON* payload,
                         miipc_osd_entry_t* out_entry,
                         char* err_msg,
                         int err_msg_len);
int miipc_osd_get_entry(int index, miipc_osd_entry_t* out_entry);
cJSON* miipc_osd_build_data(int selected_index);

#ifdef __cplusplus
}
#endif

#endif
