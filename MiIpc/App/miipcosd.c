#include "miipcosd.h"
#include "../../MiBase/include/mimutex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct miipc_osd_update {
    int index;
    int has_show;
    int show;
    int has_text;
    char text[MIIPC_OSD_TEXT_MAX];
    int has_font_size;
    int font_size;
    int has_align;
    int align;
    int has_rect;
    int x;
    int y;
    int width;
    int height;
} miipc_osd_update_t;

typedef struct miipc_osd_manager {
    int inited;
    IHardSoc* hard_soc;
    struct mi_Mutex* lock;
    miipc_osd_entry_t entries[MIIPC_OSD_MAX_ENTRIES];
} miipc_osd_manager_t;

static miipc_osd_manager_t g_osd;

static int osd_streq_ci(const char* a, const char* b)
{
    unsigned char ca;
    unsigned char cb;

    if (a == NULL || b == NULL) {
        return 0;
    }
    while (*a != '\0' && *b != '\0') {
        ca = (unsigned char)*a;
        cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (unsigned char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (unsigned char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static int osd_clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void osd_copy_text(char* dst, int dst_len, const char* src)
{
    if (dst == NULL || dst_len <= 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, (size_t)dst_len - 1u);
    dst[dst_len - 1] = '\0';
}

static void osd_set_error(char* err_msg, int err_msg_len, const char* text)
{
    osd_copy_text(err_msg, err_msg_len, text);
}

static void osd_lock(void)
{
    if (g_osd.lock != NULL && g_osd.lock->Lock != NULL) {
        g_osd.lock->Lock(g_osd.lock);
    }
}

static void osd_unlock(void)
{
    if (g_osd.lock != NULL && g_osd.lock->UnLock != NULL) {
        g_osd.lock->UnLock(g_osd.lock);
    }
}

static void osd_default_entry(miipc_osd_entry_t* entry, int index)
{
    if (entry == NULL) {
        return;
    }
    memset(entry, 0, sizeof(*entry));
    entry->index = index;
    entry->configured = index == 0 ? 1 : 0;
    entry->show = index == 0 ? 1 : 0;
    entry->font_size = 0;
    entry->align = 0;
    entry->x = 16;
    entry->y = index == 0 ? 16 : (16 + index * 32);
    entry->width = 0;
    entry->height = 0;
}

static void osd_ensure_init(void)
{
    int i;

    if (g_osd.inited) {
        return;
    }

    memset(&g_osd, 0, sizeof(g_osd));
    g_osd.lock = mi_CreateMutex(1);
    for (i = 0; i < MIIPC_OSD_MAX_ENTRIES; ++i) {
        osd_default_entry(&g_osd.entries[i], i);
    }
    g_osd.inited = 1;
}

static cJSON* osd_get_item2(cJSON* root, cJSON* payload, const char* key)
{
    cJSON* item;

    if (key == NULL) {
        return NULL;
    }
    item = payload != NULL ? cJSON_GetObjectItem(payload, key) : NULL;
    if (item != NULL) {
        return item;
    }
    return root != NULL ? cJSON_GetObjectItem(root, key) : NULL;
}

static cJSON* osd_get_first_item(cJSON* root, cJSON* payload, const char* const* keys, int key_count)
{
    int i;
    cJSON* item;

    if (keys == NULL || key_count <= 0) {
        return NULL;
    }
    for (i = 0; i < key_count; ++i) {
        item = osd_get_item2(root, payload, keys[i]);
        if (item != NULL) {
            return item;
        }
    }
    return NULL;
}

static int osd_parse_int_text(const char* text, int* out_value)
{
    char* end;
    long value;

    if (text == NULL || text[0] == '\0' || out_value == NULL) {
        return 0;
    }
    value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < -2147483647L || value > 2147483647L) {
        return 0;
    }
    *out_value = (int)value;
    return 1;
}

static int osd_item_to_int(cJSON* item, int* out_value)
{
    if (item == NULL || out_value == NULL) {
        return 0;
    }
    if (cJSON_IsNumber(item)) {
        *out_value = item->valueint;
        return 1;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return osd_parse_int_text(item->valuestring, out_value);
    }
    if (cJSON_IsTrue(item)) {
        *out_value = 1;
        return 1;
    }
    if (cJSON_IsFalse(item)) {
        *out_value = 0;
        return 1;
    }
    return 0;
}

static int osd_item_to_bool(cJSON* item, int* out_value)
{
    const char* text;

    if (item == NULL || out_value == NULL) {
        return 0;
    }
    if (cJSON_IsTrue(item)) {
        *out_value = 1;
        return 1;
    }
    if (cJSON_IsFalse(item)) {
        *out_value = 0;
        return 1;
    }
    if (cJSON_IsNumber(item)) {
        *out_value = item->valueint != 0 ? 1 : 0;
        return 1;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return 0;
    }

    text = item->valuestring;
    if (osd_streq_ci(text, "true") || osd_streq_ci(text, "on") ||
        osd_streq_ci(text, "yes") || osd_streq_ci(text, "show") ||
        osd_streq_ci(text, "enable")) {
        *out_value = 1;
        return 1;
    }
    if (osd_streq_ci(text, "false") || osd_streq_ci(text, "off") ||
        osd_streq_ci(text, "no") || osd_streq_ci(text, "hide") ||
        osd_streq_ci(text, "disable")) {
        *out_value = 0;
        return 1;
    }
    return osd_parse_int_text(text, out_value);
}

static int osd_get_int_by_keys(cJSON* root,
                               cJSON* payload,
                               const char* const* keys,
                               int key_count,
                               int* out_value)
{
    return osd_item_to_int(osd_get_first_item(root, payload, keys, key_count), out_value);
}

static int osd_get_bool_by_keys(cJSON* root,
                                cJSON* payload,
                                const char* const* keys,
                                int key_count,
                                int* out_value)
{
    return osd_item_to_bool(osd_get_first_item(root, payload, keys, key_count), out_value);
}

static const char* osd_get_string_by_keys(cJSON* root,
                                          cJSON* payload,
                                          const char* const* keys,
                                          int key_count,
                                          int* found)
{
    cJSON* item = osd_get_first_item(root, payload, keys, key_count);

    if (found != NULL) {
        *found = 0;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        if (found != NULL) {
            *found = 1;
        }
        return item->valuestring;
    }
    return "";
}

static int osd_parse_update(cJSON* root,
                            cJSON* payload,
                            miipc_osd_update_t* update,
                            char* err_msg,
                            int err_msg_len)
{
    static const char* const index_keys[] = { "Index", "OsdId", "OSDId", "Id", "id", "OsdIndex", "OSDIndex" };
    static const char* const show_keys[] = { "Show", "show", "Enable", "enable", "Enabled", "enabled", "Visible", "visible" };
    static const char* const text_keys[] = { "Text", "text", "OsdText", "OSDText", "Title", "title", "Name", "name" };
    static const char* const font_keys[] = { "FontSize", "font_size", "Font", "font", "Size", "size" };
    static const char* const align_keys[] = { "Align", "align", "TextAlign", "text_align" };
    static const char* const x_keys[] = { "X", "x", "PosX", "posx", "Left", "left" };
    static const char* const y_keys[] = { "Y", "y", "PosY", "posy", "Top", "top" };
    static const char* const w_keys[] = { "W", "w", "Width", "width" };
    static const char* const h_keys[] = { "H", "h", "Height", "height" };
    int found;
    int value;
    const char* text;

    if (update == NULL) {
        return -1;
    }
    memset(update, 0, sizeof(*update));
    update->index = 0;

    if (osd_get_int_by_keys(root, payload, index_keys, (int)(sizeof(index_keys) / sizeof(index_keys[0])), &value)) {
        update->index = value;
    }
    if (update->index < 0 || update->index >= MIIPC_OSD_MAX_ENTRIES) {
        osd_set_error(err_msg, err_msg_len, "invalid OSD index");
        return -2;
    }

    if (osd_get_bool_by_keys(root, payload, show_keys, (int)(sizeof(show_keys) / sizeof(show_keys[0])), &value)) {
        update->has_show = 1;
        update->show = value ? 1 : 0;
    }

    text = osd_get_string_by_keys(root, payload, text_keys, (int)(sizeof(text_keys) / sizeof(text_keys[0])), &found);
    if (found) {
        update->has_text = 1;
        osd_copy_text(update->text, sizeof(update->text), text);
    }

    if (osd_get_int_by_keys(root, payload, font_keys, (int)(sizeof(font_keys) / sizeof(font_keys[0])), &value)) {
        update->has_font_size = 1;
        update->font_size = osd_clamp_int(value, 0, 36);
    }

    if (osd_get_int_by_keys(root, payload, align_keys, (int)(sizeof(align_keys) / sizeof(align_keys[0])), &value)) {
        update->has_align = 1;
        update->align = osd_clamp_int(value, 0, 2);
    }

    if (osd_get_int_by_keys(root, payload, x_keys, (int)(sizeof(x_keys) / sizeof(x_keys[0])), &value)) {
        update->has_rect = 1;
        update->x = osd_clamp_int(value, 0, 8192);
    } else {
        update->x = g_osd.entries[update->index].x;
    }
    if (osd_get_int_by_keys(root, payload, y_keys, (int)(sizeof(y_keys) / sizeof(y_keys[0])), &value)) {
        update->has_rect = 1;
        update->y = osd_clamp_int(value, 0, 8192);
    } else {
        update->y = g_osd.entries[update->index].y;
    }
    if (osd_get_int_by_keys(root, payload, w_keys, (int)(sizeof(w_keys) / sizeof(w_keys[0])), &value)) {
        update->has_rect = 1;
        update->width = osd_clamp_int(value, 0, 8192);
    } else {
        update->width = g_osd.entries[update->index].width;
    }
    if (osd_get_int_by_keys(root, payload, h_keys, (int)(sizeof(h_keys) / sizeof(h_keys[0])), &value)) {
        update->has_rect = 1;
        update->height = osd_clamp_int(value, 0, 8192);
    } else {
        update->height = g_osd.entries[update->index].height;
    }

    return 0;
}

static void osd_refresh_channel_size_locked(miipc_osd_entry_t* entry)
{
    int width = 0;
    int height = 0;

    if (entry == NULL) {
        return;
    }
    if (g_osd.hard_soc != NULL && g_osd.hard_soc->GetOsdChnSize != NULL) {
        g_osd.hard_soc->GetOsdChnSize(g_osd.hard_soc, entry->index, &width, &height);
    }
    entry->channel_width = width;
    entry->channel_height = height;
}

static int osd_apply_entry_to_hardware_locked(const miipc_osd_entry_t* entry,
                                              char* err_msg,
                                              int err_msg_len)
{
    IHardSoc* hard = g_osd.hard_soc;
    int ret;

    if (entry == NULL) {
        return -1;
    }
    if (hard == NULL) {
        return 0;
    }

    if (!entry->show) {
        if (hard->SetOsdShow != NULL) {
            ret = hard->SetOsdShow(hard, entry->index, 0);
            if (ret != 0) {
                osd_set_error(err_msg, err_msg_len, "SetOsdShow failed");
                return ret;
            }
        }
        return 0;
    }

    if (hard->ResetOsdSize != NULL) {
        ret = hard->ResetOsdSize(hard, entry->index, entry->x, entry->y, entry->width, entry->height);
        if (ret != 0) {
            osd_set_error(err_msg, err_msg_len, "ResetOsdSize failed");
            return ret;
        }
    }
    if (hard->SetFontSize != NULL) {
        ret = hard->SetFontSize(hard, entry->index, entry->font_size);
        if (ret != 0) {
            osd_set_error(err_msg, err_msg_len, "SetFontSize failed");
            return ret;
        }
    }
    if (hard->SetTextAlign != NULL) {
        ret = hard->SetTextAlign(hard, entry->index, entry->align);
        if (ret != 0) {
            osd_set_error(err_msg, err_msg_len, "SetTextAlign failed");
            return ret;
        }
    }
    if (hard->SetOsdText != NULL) {
        ret = hard->SetOsdText(hard, entry->index, entry->text);
        if (ret != 0) {
            osd_set_error(err_msg, err_msg_len, "SetOsdText failed");
            return ret;
        }
    }
    if (hard->SetOsdShow != NULL) {
        ret = hard->SetOsdShow(hard, entry->index, entry->show);
        if (ret != 0) {
            osd_set_error(err_msg, err_msg_len, "SetOsdShow failed");
            return ret;
        }
    }
    if (hard->UpdateOsd != NULL) {
        hard->UpdateOsd(hard, entry->index);
    }

    return 0;
}

static void osd_apply_configured_locked(void)
{
    int i;
    int ret;

    if (g_osd.hard_soc == NULL) {
        return;
    }
    for (i = 0; i < MIIPC_OSD_MAX_ENTRIES; ++i) {
        if (g_osd.entries[i].configured) {
            ret = osd_apply_entry_to_hardware_locked(&g_osd.entries[i], NULL, 0);
            osd_refresh_channel_size_locked(&g_osd.entries[i]);
            printf("MiIpc OSD auto apply index=%d show=%d time=%d text_len=%d chn=%dx%d ret=%d\n",
                g_osd.entries[i].index,
                g_osd.entries[i].show,
                (g_osd.entries[i].index == 0 && g_osd.entries[i].text[0] == '\0') ? 1 : 0,
                (int)strlen(g_osd.entries[i].text),
                g_osd.entries[i].channel_width,
                g_osd.entries[i].channel_height,
                ret);
            fflush(stdout);
        }
    }
}

static cJSON* osd_entry_to_json(const miipc_osd_entry_t* entry)
{
    cJSON* item;
    int time_mode;

    if (entry == NULL) {
        return NULL;
    }
    item = cJSON_CreateObject();
    if (item == NULL) {
        return NULL;
    }
    time_mode = (entry->index == 0 && entry->text[0] == '\0') ? 1 : 0;
    cJSON_AddNumberToObject(item, "Index", entry->index);
    cJSON_AddNumberToObject(item, "Configured", entry->configured);
    cJSON_AddNumberToObject(item, "Show", entry->show);
    cJSON_AddNumberToObject(item, "Enable", entry->show);
    cJSON_AddStringToObject(item, "Text", entry->text);
    cJSON_AddNumberToObject(item, "TimeMode", time_mode);
    cJSON_AddNumberToObject(item, "FontSize", entry->font_size);
    cJSON_AddNumberToObject(item, "Align", entry->align);
    cJSON_AddNumberToObject(item, "X", entry->x);
    cJSON_AddNumberToObject(item, "Y", entry->y);
    cJSON_AddNumberToObject(item, "W", entry->width);
    cJSON_AddNumberToObject(item, "H", entry->height);
    cJSON_AddNumberToObject(item, "ChnWidth", entry->channel_width);
    cJSON_AddNumberToObject(item, "ChnHeight", entry->channel_height);
    return item;
}

void miipc_osd_init(IHardSoc* hard_soc)
{
    osd_ensure_init();
    osd_lock();
    g_osd.hard_soc = hard_soc;
    osd_unlock();
}

void miipc_osd_deinit(void)
{
    struct mi_Mutex* lock;

    if (!g_osd.inited) {
        return;
    }
    osd_lock();
    g_osd.hard_soc = NULL;
    osd_unlock();

    lock = g_osd.lock;
    g_osd.lock = NULL;
    if (lock != NULL) {
        mi_DelMutex(lock);
    }
    memset(&g_osd, 0, sizeof(g_osd));
}

void miipc_osd_set_hard_soc(IHardSoc* hard_soc)
{
    osd_ensure_init();
    osd_lock();
    g_osd.hard_soc = hard_soc;
    osd_apply_configured_locked();
    osd_unlock();
}

int miipc_osd_apply_json(cJSON* root,
                         cJSON* payload,
                         miipc_osd_entry_t* out_entry,
                         char* err_msg,
                         int err_msg_len)
{
    miipc_osd_update_t update;
    miipc_osd_entry_t* entry;
    int ret;

    osd_ensure_init();
    osd_set_error(err_msg, err_msg_len, "");

    osd_lock();
    ret = osd_parse_update(root, payload, &update, err_msg, err_msg_len);
    if (ret != 0) {
        osd_unlock();
        return ret;
    }

    entry = &g_osd.entries[update.index];
    entry->configured = 1;
    if (update.has_rect) {
        entry->x = update.x;
        entry->y = update.y;
        entry->width = update.width;
        entry->height = update.height;
    }
    if (update.has_font_size) {
        entry->font_size = update.font_size;
    }
    if (update.has_align) {
        entry->align = update.align;
    }
    if (update.has_text) {
        osd_copy_text(entry->text, sizeof(entry->text), update.text);
        if (entry->text[0] != '\0' && !update.has_show) {
            entry->show = 1;
        }
    }
    if (update.has_show) {
        entry->show = update.show ? 1 : 0;
    }

    ret = osd_apply_entry_to_hardware_locked(entry, err_msg, err_msg_len);
    osd_refresh_channel_size_locked(entry);
    if (out_entry != NULL) {
        *out_entry = *entry;
    }
    osd_unlock();
    return ret;
}

int miipc_osd_get_entry(int index, miipc_osd_entry_t* out_entry)
{
    if (index < 0 || index >= MIIPC_OSD_MAX_ENTRIES || out_entry == NULL) {
        return -1;
    }
    osd_ensure_init();
    osd_lock();
    osd_refresh_channel_size_locked(&g_osd.entries[index]);
    *out_entry = g_osd.entries[index];
    osd_unlock();
    return 0;
}

cJSON* miipc_osd_build_data(int selected_index)
{
    cJSON* data;
    cJSON* array;
    cJSON* selected;
    cJSON* item;
    int i;

    osd_ensure_init();
    data = cJSON_CreateObject();
    array = cJSON_CreateArray();
    if (data == NULL || array == NULL) {
        cJSON_Delete(data);
        cJSON_Delete(array);
        return NULL;
    }

    osd_lock();
    cJSON_AddNumberToObject(data, "MaxCount", MIIPC_OSD_MAX_ENTRIES);
    cJSON_AddNumberToObject(data, "HardwareReady", g_osd.hard_soc != NULL ? 1 : 0);
    for (i = 0; i < MIIPC_OSD_MAX_ENTRIES; ++i) {
        osd_refresh_channel_size_locked(&g_osd.entries[i]);
        item = osd_entry_to_json(&g_osd.entries[i]);
        if (item != NULL) {
            cJSON_AddItemToArray(array, item);
        }
    }
    if (selected_index >= 0 && selected_index < MIIPC_OSD_MAX_ENTRIES) {
        selected = osd_entry_to_json(&g_osd.entries[selected_index]);
        if (selected != NULL) {
            cJSON_AddItemToObject(data, "Item", selected);
            cJSON_AddNumberToObject(data, "Index", g_osd.entries[selected_index].index);
            cJSON_AddNumberToObject(data, "Configured", g_osd.entries[selected_index].configured);
            cJSON_AddNumberToObject(data, "Show", g_osd.entries[selected_index].show);
            cJSON_AddNumberToObject(data, "Enable", g_osd.entries[selected_index].show);
            cJSON_AddStringToObject(data, "Text", g_osd.entries[selected_index].text);
            cJSON_AddNumberToObject(data, "TimeMode",
                (g_osd.entries[selected_index].index == 0 && g_osd.entries[selected_index].text[0] == '\0') ? 1 : 0);
            cJSON_AddNumberToObject(data, "FontSize", g_osd.entries[selected_index].font_size);
            cJSON_AddNumberToObject(data, "Align", g_osd.entries[selected_index].align);
            cJSON_AddNumberToObject(data, "X", g_osd.entries[selected_index].x);
            cJSON_AddNumberToObject(data, "Y", g_osd.entries[selected_index].y);
            cJSON_AddNumberToObject(data, "W", g_osd.entries[selected_index].width);
            cJSON_AddNumberToObject(data, "H", g_osd.entries[selected_index].height);
            cJSON_AddNumberToObject(data, "ChnWidth", g_osd.entries[selected_index].channel_width);
            cJSON_AddNumberToObject(data, "ChnHeight", g_osd.entries[selected_index].channel_height);
        }
    }
    osd_unlock();

    cJSON_AddItemToObject(data, "OsdInfos", array);
    return data;
}
