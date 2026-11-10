/*
  Copyright (c), 2001-2025, Shenshu Tech. Co., Ltd.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include "sample_comm.h"
#include "securec.h"

#define BIG_STREAM_SIZE PIC_2560X1440
#define SMALL_STREAM_SIZE PIC_D1_NTSC

#define VI_VB_YUV_CNT 3
#define VPSS_VB_YUV_CNT 4

#define ENTER_ASCII 10


#ifdef Dev_MemLow64MB
#define CHN_NUM_MAX 2
#else
#define CHN_NUM_MAX 3
#endif

#ifndef MI_CV610_VPSS_CHN_NUM
#define MI_CV610_VPSS_CHN_NUM 1
#endif
#define SVAC3_CHN_NUM_MAX 1
#define MAX_WATERMARK_LEN 128
#define DEFAULT_WAIT_TIME   20000

typedef struct {
    hi_size max_size;
    hi_pixel_format pixel_format;
    hi_size in_size;
    hi_size output_size[HI_VPSS_MAX_PHYS_CHN_NUM];
    hi_compress_mode compress_mode[HI_VPSS_MAX_PHYS_CHN_NUM];
    hi_bool enable[HI_VPSS_MAX_PHYS_CHN_NUM];
    hi_fmu_mode fmu_mode[HI_VPSS_MAX_PHYS_CHN_NUM];
} sample_venc_vpss_chn_attr;

typedef struct {
    hi_u32 valid_num;
    hi_u64 blk_size[HI_VB_MAX_COMMON_POOLS];
    hi_u32 blk_cnt[HI_VB_MAX_COMMON_POOLS];
    hi_u32 supplement_config;
} sample_venc_vb_attr;

typedef struct {
    hi_vpss_chn vpss_chn[CHN_NUM_MAX];
    hi_venc_chn venc_chn[CHN_NUM_MAX];
} sample_venc_vpss_chn;

typedef struct {
    hi_s32 venc_chn_num;
    hi_size enc_size[CHN_NUM_MAX];
    hi_s32 vpss_chn_depth;
} sample_venc_param;


static hi_bool g_sample_venc_exit = HI_FALSE;
static hi_bool g_send_multi_frame_signal = HI_FALSE;
static pthread_t g_send_multi_frame_thread = 0;
static hi_bool g_mpp_started = HI_FALSE;


static hi_s32 sample_venc_getchar(void)
{
    hi_s32 c;
    if (g_sample_venc_exit == HI_TRUE) {
        printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
        return 'e';
    }

    c = getchar();

    if (g_sample_venc_exit == HI_TRUE) {
        printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
        return 'e';
    }

    return c;
}

static td_void print_gop_mode(void)
{
    printf("please input choose gop mode!\n");
    printf("\t 0) normal p.\n");
    printf("\t 1) dual p.\n");
    printf("\t 2) smart p.\n");
}

static td_void svac3_print_gop_mode(void)
{
    printf("please input choose gop mode!\n");
    printf("\t 0) normal p.\n");
    printf("\t 1) dual p.\n");
    printf("\t 2) smart p.\n");
    printf("\t 3) smart crr.\n");
}


static hi_s32 sample_clear_invalid_ch(void)
{
    hi_s32 c;

    while ((c = sample_venc_getchar()) != ENTER_ASCII) {
    }
    return HI_SUCCESS;
}

static hi_s32 set_gop_mode(hi_s32 c, hi_venc_gop_mode *gop_mode)
{
    switch (c) {
        case '0':
            *gop_mode = HI_VENC_GOP_MODE_NORMAL_P;
            break;

        case '1':
            *gop_mode = HI_VENC_GOP_MODE_DUAL_P;
            break;

        case '2':
            *gop_mode = HI_VENC_GOP_MODE_SMART_P;
            break;

        default:
            return HI_FAILURE;
    }
    return HI_SUCCESS;
}

static hi_s32 svac3_set_gop_mode(hi_s32 c, hi_venc_gop_mode *gop_mode)
{
    switch (c) {
        case '0':
            *gop_mode = HI_VENC_GOP_MODE_NORMAL_P;
            break;

        case '1':
            *gop_mode = HI_VENC_GOP_MODE_DUAL_P;
            break;

        case '2':
            *gop_mode = HI_VENC_GOP_MODE_SMART_P;
            break;

        case '3':
            *gop_mode = HI_VENC_GOP_MODE_SMART_CRR;
            break;

        default:
            return HI_FAILURE;
    }
    return HI_SUCCESS;
}

static hi_s32 get_gop_mode(hi_venc_gop_mode *gop_mode)
{
    hi_s32 c;

    while (HI_TRUE) {
        print_gop_mode();
        c = sample_venc_getchar();
        if (g_sample_venc_exit) {
            return HI_FAILURE;
        }

        if (c == ENTER_ASCII) {
            sample_print("invalid input! please try again.\n");
            continue;
        } else if (set_gop_mode(c, gop_mode) == HI_SUCCESS && sample_venc_getchar() == ENTER_ASCII) {
            return HI_SUCCESS;
        }
        sample_clear_invalid_ch();
        sample_print("invalid input! please try again.\n");
    }

    return HI_SUCCESS;
}

static hi_s32 svac3_get_gop_mode(hi_venc_gop_mode *gop_mode)
{
    hi_s32 c;

    while (HI_TRUE) {
        svac3_print_gop_mode();
        c = sample_venc_getchar();
        if (g_sample_venc_exit) {
            return HI_FAILURE;
        }

        if (c == ENTER_ASCII) {
            sample_print("invalid input! please try again.\n");
            continue;
        } else if (svac3_set_gop_mode(c, gop_mode) == HI_SUCCESS && sample_venc_getchar() == ENTER_ASCII) {
            return HI_SUCCESS;
        }
        sample_clear_invalid_ch();
        sample_print("invalid input! please try again.\n");
    }

    return HI_SUCCESS;
}

static hi_void print_rc_mode(hi_payload_type type)
{
    printf("please input choose rc mode!\n");
    printf("\t c) cbr.\n");
    printf("\t v) vbr.\n");
    if (type != HI_PT_MJPEG) {
        printf("\t b) abr.\n");
        printf("\t a) avbr.\n");
        printf("\t x) cvbr.\n");
        printf("\t q) qvbr.\n");
    }
    printf("\t f) fix_qp\n");
}

static hi_s32 set_rc_mode(hi_s32 c, sample_rc *rc_mode)
{
    switch (c) {
        case 'b':
            *rc_mode = SAMPLE_RC_ABR;
            break;

        case 'c':
            *rc_mode = SAMPLE_RC_CBR;
            break;

        case 'v':
            *rc_mode = SAMPLE_RC_VBR;
            break;

        case 'a':
            *rc_mode = SAMPLE_RC_AVBR;
            break;

        case 'x':
            *rc_mode = SAMPLE_RC_CVBR;
            break;

        case 'q':
            *rc_mode = SAMPLE_RC_QVBR;
            break;

        case 'f':
            *rc_mode = SAMPLE_RC_FIXQP;
            break;

        default:
            return HI_FAILURE;
    }
    return HI_SUCCESS;
}

hi_s32 get_rc_mode(hi_payload_type type, sample_rc *rc_mode)
{
    hi_s32 c;

    if (type == HI_PT_JPEG) {
        return HI_SUCCESS;
    }

    while (HI_TRUE) {
        print_rc_mode(type);
        c = sample_venc_getchar();
        if (g_sample_venc_exit) 
        {
            return HI_FAILURE;
        }

        if (c == ENTER_ASCII) 
        {
            sample_print("invalid input! please try again.\n");
            continue;
        } else if (set_rc_mode(c, rc_mode) == HI_SUCCESS && sample_venc_getchar() == ENTER_ASCII) {
            return HI_SUCCESS;
        }
        sample_clear_invalid_ch();
        sample_print("invalid input! please try again.\n");
    }

    return HI_SUCCESS;
}

static hi_s32 set_intra_refresh_mode(hi_s32 c, hi_venc_intra_refresh_mode *intra_refresh_mode)
{
    switch (c) {
        case 'r':
            *intra_refresh_mode = HI_VENC_INTRA_REFRESH_ROW;
            break;

        case 'c':
            *intra_refresh_mode = HI_VENC_INTRA_REFRESH_COLUMN;
            break;

        default:
            return HI_FAILURE;
    }
    return HI_SUCCESS;
}

static td_void print_refresh_mode(void)
{
    printf("please input choose intra refresh mode!\n");
    printf("\t r) row.\n");
    printf("\t c) column.\n");
}

hi_s32 get_intra_refresh_mode(hi_venc_intra_refresh_mode *intra_refresh_mode)
{
    hi_s32 c;

    while (HI_TRUE) {
        print_refresh_mode();
        c = sample_venc_getchar();
        if (g_sample_venc_exit) {
            return HI_FAILURE;
        }

        if (c == ENTER_ASCII) {
            sample_print("invalid input! please try again.\n");
            continue;
        } else if (set_intra_refresh_mode(c, intra_refresh_mode) == HI_SUCCESS &&
            sample_venc_getchar() == ENTER_ASCII) {
            return HI_SUCCESS;
        }
        sample_clear_invalid_ch();
        sample_print("invalid input! please try again.\n");
    }

    return HI_SUCCESS;
}

static hi_s32 set_payload_type(hi_s32 c, hi_payload_type *payload)
{
    switch (c) {
        case '0':
            *payload = HI_PT_H264;
            break;

        case '1':
            *payload = HI_PT_H265;
            break;

        case '2':
            *payload = HI_PT_SVAC3;
            break;

        default:
            return HI_FAILURE;
    }
    return HI_SUCCESS;
}

static td_void print_payload_type(void)
{
    printf("please input choose payload type!\n");
    printf("\t 0) h264\n");
    printf("\t 1) h265\n");
    printf("\t 2) svac3\n");
}

hi_s32 get_payload_type(hi_payload_type *payload)
{
    hi_s32 c;

    while (HI_TRUE) {
        print_payload_type();
        c = sample_venc_getchar();
        if (g_sample_venc_exit) {
            return HI_FAILURE;
        }

        if (c == ENTER_ASCII) {
            sample_print("invalid input! please try again.\n");
            continue;
        } else if (set_payload_type(c, payload) == HI_SUCCESS &&
            sample_venc_getchar() == ENTER_ASCII) {
            return HI_SUCCESS;
        }
        sample_clear_invalid_ch();
        sample_print("invalid input! please try again.\n");
    }

    return HI_SUCCESS;
}

static hi_s32 sample_composite_set_gop_mode(hi_s32 c, hi_venc_gop_mode *gop_mode)
{
    switch (c) {
        case '0':
            *gop_mode = HI_VENC_GOP_MODE_NORMAL_P;
            break;

        case '1':
            *gop_mode = HI_VENC_GOP_MODE_SMART_P;
            break;

        default:
            return HI_FAILURE;
    }
    return HI_SUCCESS;
}

static td_void composte_print_gop_mode(void)
{
    printf("please input choose gop mode!\n");
    printf("\t 0) normal p.\n");
    printf("\t 1) smart p.\n");
}

static hi_s32 composite_get_gop_mode(hi_venc_gop_mode *gop_mode)
{
    hi_s32 c;

    while (HI_TRUE) {
        composte_print_gop_mode();
        c = sample_venc_getchar();
        if (g_sample_venc_exit) {
            return HI_FAILURE;
        }

        if (c == ENTER_ASCII) {
            sample_print("invalid input! please try again.\n");
            continue;
        } else if (sample_composite_set_gop_mode(c, gop_mode) == HI_SUCCESS &&
            sample_venc_getchar() == ENTER_ASCII) {
            return HI_SUCCESS;
        }

        sample_clear_invalid_ch();
        sample_print("invalid input! please try again.\n");
    }

    return HI_SUCCESS;
}

static hi_void update_vb_attr(sample_venc_vb_attr *vb_attr, const hi_size *size, hi_pixel_format format,
    hi_compress_mode compress_mode, td_u32 blk_cnt)
{
    hi_pic_buf_attr pic_buf_attr = { 0 };

    if (vb_attr->valid_num >= HI_VB_MAX_COMMON_POOLS) {
        printf("vb valid_num = %d should smaller than HI_VB_MAX_COMMON_POOLS(%d)\n",
            vb_attr->valid_num, HI_VB_MAX_COMMON_POOLS);
        return;
    }

    pic_buf_attr.width = size->width;
    pic_buf_attr.height = size->height;
    pic_buf_attr.align = HI_DEFAULT_ALIGN;
    pic_buf_attr.bit_width = HI_DATA_BIT_WIDTH_8;
    pic_buf_attr.pixel_format = format;
    pic_buf_attr.compress_mode = compress_mode;
    vb_attr->blk_size[vb_attr->valid_num] = hi_common_get_pic_buf_size(&pic_buf_attr);
    vb_attr->blk_cnt[vb_attr->valid_num] = blk_cnt;

    vb_attr->valid_num++;
}

static hi_void get_vb_attr(const hi_size *vi_size, const sample_venc_vpss_chn_attr *vpss_chn_attr,
    sample_venc_vb_attr *vb_attr)
{
    hi_s32 i;

    /* vb for vi-vpss */
    update_vb_attr(vb_attr, vi_size, HI_PIXEL_FORMAT_YUV_SEMIPLANAR_422, HI_COMPRESS_MODE_NONE, VI_VB_YUV_CNT);

    // vb for vpss-venc(big stream)
    if (vb_attr->valid_num >= HI_VB_MAX_COMMON_POOLS) {
        return;
    }

    for (i = 0; i < HI_VPSS_MAX_PHYS_CHN_NUM && vb_attr->valid_num < HI_VB_MAX_COMMON_POOLS; i++) {
        if (vpss_chn_attr->enable[i] == HI_TRUE && vpss_chn_attr->fmu_mode[i] == HI_FMU_MODE_OFF) {
            update_vb_attr(vb_attr, &vpss_chn_attr->output_size[i], vpss_chn_attr->pixel_format,
                vpss_chn_attr->compress_mode[i], VPSS_VB_YUV_CNT);
        }
    }

    vb_attr->supplement_config = HI_VB_SUPPLEMENT_JPEG_MASK | HI_VB_SUPPLEMENT_BNR_MOT_MASK;
}

static hi_void get_default_vpss_chn_attr(hi_size *vi_size, hi_size enc_size[], hi_s32 len,
    sample_venc_vpss_chn_attr *vpss_chan_attr)
{
    hi_s32 i;
    hi_u32 max_width;
    hi_u32 max_height;

    if (memset_s(vpss_chan_attr, sizeof(sample_venc_vpss_chn_attr), 0, sizeof(sample_venc_vpss_chn_attr)) != EOK) {
        printf("vpss chn attr call memset_s error\n");
        return;
    }

    max_width = vi_size->width;
    max_height = vi_size->height;

    for (i = 0; (i < len) && (i < HI_VPSS_MAX_PHYS_CHN_NUM); i++) 
    {
        vpss_chan_attr->output_size[i].width = enc_size[i].width;
        vpss_chan_attr->output_size[i].height = enc_size[i].height;
        vpss_chan_attr->compress_mode[i] = (i == 0) ? HI_COMPRESS_MODE_SEG_COMPACT : HI_COMPRESS_MODE_NONE;
        vpss_chan_attr->enable[i] = HI_TRUE;
        vpss_chan_attr->fmu_mode[i] = HI_FMU_MODE_OFF;

        max_width = MAX2(max_width, enc_size[i].width);
        max_height = MAX2(max_height, enc_size[i].height);
        printf("w:%d h:%d",vpss_chan_attr->output_size[i].width , vpss_chan_attr->output_size[i].height );
    }

    vpss_chan_attr->max_size.width = max_width;
    vpss_chan_attr->max_size.height = max_height;
    vpss_chan_attr->pixel_format = HI_PIXEL_FORMAT_YVU_SEMIPLANAR_420;

    return;
}

static hi_s32 sample_venc_sys_init(sample_venc_vb_attr *vb_attr)
{
    hi_u32 i;
    hi_s32 ret;
    hi_vb_cfg vb_cfg = { 0 };

    if (vb_attr->valid_num > HI_VB_MAX_COMMON_POOLS) {
        sample_print("sample_venc_sys_init vb valid num(%d) too large than HI_VB_MAX_COMMON_POOLS(%d)!\n",
            vb_attr->valid_num, HI_VB_MAX_COMMON_POOLS);
        return HI_FAILURE;
    }

    for (i = 0; i < vb_attr->valid_num; i++) {
        vb_cfg.common_pool[i].blk_size = vb_attr->blk_size[i];
        vb_cfg.common_pool[i].blk_cnt = vb_attr->blk_cnt[i];
    }

    vb_cfg.max_pool_cnt = vb_attr->valid_num;

    if (vb_attr->supplement_config == 0) {
        ret = sample_comm_sys_vb_init(&vb_cfg);
    } else {
        ret = sample_comm_sys_init_with_vb_supplement(&vb_cfg, vb_attr->supplement_config);
    }

    if (ret != HI_SUCCESS) {
        sample_print("sample_venc_sys_init failed!\n");
    }

    return ret;
}

static hi_s32 sample_venc_vi_init(sample_vi_cfg *vi_cfg)
{
    hi_s32 ret;

    ret = sample_comm_vi_start_vi(vi_cfg);
    if (ret != HI_SUCCESS) {
        sample_print("sample_comm_vi_start_vi failed: 0x%x\n", ret);
        return ret;
    }

    return HI_SUCCESS;
}

static hi_void sample_venc_vi_deinit(sample_vi_cfg *vi_cfg)
{
    sample_comm_vi_stop_vi(vi_cfg);
}


static hi_void sample_venc_vpss_deinit(hi_vpss_grp vpss_grp, sample_venc_vpss_chn_attr *vpss_chan_cfg)
{
    hi_s32 ret;

    ret = sample_common_vpss_stop(vpss_grp, vpss_chan_cfg->enable, HI_VPSS_MAX_PHYS_CHN_NUM);
    if (ret != HI_SUCCESS) {
        sample_print("failed with %#x!\n", ret);
    }
}





static hi_void sample_set_venc_vpss_chn(sample_venc_vpss_chn *venc_vpss_chn, hi_s32 chn_num_max)
{
    hi_s32 i;
    hi_unused(chn_num_max);

    for (i = 0; i < CHN_NUM_MAX; i++) {
        venc_vpss_chn->vpss_chn[i] = i;
        venc_vpss_chn->venc_chn[i] = i;
    }
}

static hi_void sample_venc_unbind_vpss_stop(hi_vpss_grp vpss_grp, const sample_venc_vpss_chn *venc_vpss_chn)
{
    hi_s32 i;

    for (i = 0; i < CHN_NUM_MAX; i++) {
        sample_comm_vpss_un_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[i], venc_vpss_chn->venc_chn[i]);
        sample_comm_venc_stop(venc_vpss_chn->venc_chn[i]);
    }
}


static hi_void sample_venc_stop(const sample_venc_vpss_chn *venc_vpss_chn, hi_s32 chn_num_max)
{
    hi_s32 i;
    hi_unused(chn_num_max);

    for (i = 0; i < CHN_NUM_MAX; i++) {
        sample_comm_venc_stop(venc_vpss_chn->venc_chn[i]);
    }
}

static hi_void sample_venc_exit_process()
{
    printf("please press twice ENTER to exit this sample\n");
    (hi_void)getchar();

    if (g_sample_venc_exit != HI_TRUE) {
        (hi_void)getchar();
    }
    sample_comm_venc_stop_get_stream(CHN_NUM_MAX);
}

static hi_void sample_venc_mosaic_map_exit_process(hi_s32 chn_num_max)
{
    printf("please press twice ENTER to exit this sample\n");
    (hi_void)getchar();

    if (g_sample_venc_exit != HI_TRUE) {
        (hi_void)getchar();
    }

    sample_comm_venc_stop_send_frame();
    sample_comm_venc_stop_get_stream(chn_num_max);
}

//vb 
static hi_void sample_venc_online_wrap_get_default_vb_cfg(hi_vb_cfg *vb_cfg, sample_venc_param *enc_param,
    hi_u32 wrap_size)
{
    hi_s32 i;
    hi_vb_calc_cfg calc_cfg;
    hi_pic_buf_attr buf_attr;

    (hi_void)memset_s(vb_cfg, sizeof(hi_vb_cfg), 0, sizeof(hi_vb_cfg));
    vb_cfg->max_pool_cnt = 128; /* 128 blks */

    for (i = 1; i < enc_param->venc_chn_num && i < CHN_NUM_MAX; i++) 
    {
        buf_attr.width = enc_param->enc_size[i].width;
        buf_attr.height = enc_param->enc_size[i].height;
        buf_attr.align = HI_DEFAULT_ALIGN;
        buf_attr.bit_width = HI_DATA_BIT_WIDTH_8;
        buf_attr.pixel_format = HI_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        buf_attr.compress_mode = HI_COMPRESS_MODE_NONE;
        buf_attr.video_format = HI_VIDEO_FORMAT_LINEAR;
        hi_common_get_pic_buf_cfg(&buf_attr, &calc_cfg);

        vb_cfg->common_pool[i].blk_size = calc_cfg.vb_size;
        vb_cfg->common_pool[i].blk_cnt = 3; /* 3 blk_cnt */
    }

    vb_cfg->common_pool[i].blk_cnt = 1;
    vb_cfg->common_pool[i].blk_size = wrap_size;
}

static hi_void sample_venc_online_wrap_get_default_sys_cfg(sampe_sys_cfg *sys_cfg)
{
    sys_cfg->route_num = 1;
    sys_cfg->mode_type = HI_VI_ONLINE_VPSS_ONLINE;
    sys_cfg->vpss_wrap_en = HI_TRUE;
}

hi_void sample_venc_online_wrap_get_default_vpss_cfg(sample_sns_type sns_type, sample_vpss_cfg *vpss_cfg,
    sample_venc_param *enc_param)
{
    hi_vpss_chn chn;
    hi_size in_size;
    hi_u32 max_width;
    hi_u32 max_height;

    sample_comm_vi_get_size_by_sns_type(sns_type, &in_size);
    sample_comm_vpss_get_default_grp_attr(&vpss_cfg->grp_attr);
    sample_comm_vpss_get_default_3dnr_attr(&vpss_cfg->nr_attr);
    vpss_cfg->vpss_grp = 0;
    max_width = in_size.width;
    max_height = in_size.height;

    for (chn = 0; chn < enc_param->venc_chn_num && chn < HI_VPSS_MAX_PHYS_CHN_NUM; chn++) 
    {
        vpss_cfg->chn_en[chn] = HI_TRUE; 
        vpss_cfg->chn_attr[chn].depth =  enc_param->vpss_chn_depth;
        sample_comm_vpss_get_default_chn_attr(&vpss_cfg->chn_attr[chn]);
        if (chn > HI_VPSS_CHN1) 
        {
           // vpss_cfg->chn_attr[chn].compress_mode = HI_COMPRESS_MODE_NONE;
        }
        if(chn==0)
        {
            vpss_cfg->chn_attr[chn].depth=0;
            vpss_cfg->chn_attr[chn].frame_rate.src_frame_rate=30;
            vpss_cfg->chn_attr[chn].frame_rate.dst_frame_rate=30;
        //else
          // vpss_cfg->chn_attr[chn].frame_rate.dst_frame_rate=8;
        }else{
            vpss_cfg->chn_attr[chn].frame_rate.src_frame_rate=5;
            vpss_cfg->chn_attr[chn].frame_rate.dst_frame_rate=5;
            vpss_cfg->chn_attr[chn].depth = 2;
        }
        // vpss_cfg->chn_attr[chn].frame_rate.dst_frame_rate=30;
        //else
          // vpss_cfg->chn_attr[chn].frame_rate.dst_frame_rate=8;
        vpss_cfg->chn_attr[chn].width  = enc_param->enc_size[chn].width;
        vpss_cfg->chn_attr[chn].height = enc_param->enc_size[chn].height;
       
//vpss_cfg->chn_attr[chn].

        max_width = MAX2(max_width, enc_param->enc_size[chn].width);
        max_height = MAX2(max_height, enc_param->enc_size[chn].height);
    }
    vpss_cfg->chn_attr[HI_VPSS_CHN0].compress_mode = HI_COMPRESS_MODE_SEG_COMPACT;
    vpss_cfg->grp_attr.max_width  = max_width;
    vpss_cfg->grp_attr.max_height = max_height;
    vpss_cfg->wrap_attr[0].enable = HI_TRUE;
}

static hi_void sample_venc_vpss_get_wrap_cfg(sample_sns_type sns_type, sampe_sys_cfg *sys_cfg,
    sample_vpss_cfg *vpss_cfg)
{
    hi_u32 full_lines_std;
    if (sys_cfg->vpss_wrap_en) {
        if (sns_type == SC4336P_MIPI_4M_30FPS_10BIT || sns_type == OS04D10_MIPI_4M_30FPS_10BIT ||
            sns_type == GC4023_MIPI_4M_30FPS_10BIT || sns_type == SC431HAI_MIPI_4M_30FPS_10BIT ||
            sns_type == SC431HAI_MIPI_4M_30FPS_10BIT_WDR2TO1 || sns_type == SC4336P_MIPI_3M_30FPS_10BIT) {
            full_lines_std = 1500; /* full_lines_std: 1500 */
        } else if (sns_type == SC450AI_MIPI_4M_30FPS_10BIT || sns_type == SC450AI_MIPI_4M_30FPS_10BIT_WDR2TO1) {
            full_lines_std = 1585; /* full_lines_std: 1585 */
        } else if (sns_type == SC500AI_MIPI_5M_30FPS_10BIT || sns_type == SC500AI_MIPI_5M_30FPS_10BIT_WDR2TO1) {
            full_lines_std = 1700; /* full_lines_std: 1700 */
        } else {
            sys_cfg->vpss_wrap_en = HI_FALSE;
            vpss_cfg->wrap_attr[0].enable = HI_FALSE;
            return;
        }
        (hi_void)sample_comm_vpss_get_wrap_cfg(vpss_cfg->chn_attr, sys_cfg->mode_type, full_lines_std,
            &vpss_cfg->wrap_attr[0]);

        sys_cfg->vpss_wrap_size = vpss_cfg->wrap_attr[0].buf_size;
    }
}

static hi_s32 sample_venc_online_wrap_sys_init(sample_venc_param *enc_param, sampe_sys_cfg *sys_cfg)
{
    hi_s32 ret;
    hi_vb_cfg vb_cfg = {0};
    hi_u32 supplement_config = HI_VB_SUPPLEMENT_JPEG_MASK | HI_VB_SUPPLEMENT_BNR_MOT_MASK;

    sample_venc_online_wrap_get_default_vb_cfg(&vb_cfg, enc_param, sys_cfg->vpss_wrap_size);

    if (supplement_config == 0) {
        ret = sample_comm_sys_vb_init(&vb_cfg);
    } else {
        ret = sample_comm_sys_init_with_vb_supplement(&vb_cfg, supplement_config);
    }

    if (ret != HI_SUCCESS) {
        sample_print("sample_venc_sys_init failed!\n");
    }

    if (sample_comm_vi_set_vi_vpss_mode(sys_cfg->mode_type, HI_VI_AIISP_MODE_DEFAULT) != HI_SUCCESS) {
        goto sys_exit;
    }

    return HI_SUCCESS;
sys_exit:
    sample_comm_sys_exit();
    return HI_FAILURE;
}

static hi_s32 sample_venc_get_enc_size(hi_pic_size *pic_size, sample_venc_param *enc_param)
{
    hi_s32 i;
    hi_s32 ret;

    for (i = 0; i < enc_param->venc_chn_num && i < CHN_NUM_MAX; i++) 
    {
        ret = sample_comm_sys_get_pic_size(pic_size[i], &(enc_param->enc_size[i]));
        if (ret != HI_SUCCESS) 
        {
            sample_print("sample_comm_sys_get_pic_size failed!\n");
            return ret;
        }
    }

    return HI_SUCCESS;
}

static hi_s32 sample_venc_online_wrap_start_vpss(hi_vpss_grp grp, sample_vpss_cfg *vpss_cfg)
{
    hi_s32 ret;
    hi_frame_interrupt_attr frame_interrupt_attr = {0};
    sample_vpss_chn_attr vpss_chn_attr = {0};

    frame_interrupt_attr.interrupt_type = HI_FRAME_INTERRUPT_EARLY_END;
    frame_interrupt_attr.early_line = vpss_cfg->grp_attr.max_height / 2; /* 2 half */
    ret = hi_mpi_vpss_set_grp_frame_interrupt_attr(0, &frame_interrupt_attr);
    if (ret != HI_SUCCESS) {
        printf("hi_mpi_vpss_set_grp_frame_interrupt_attr failed!\n");
        return ret;
    }

    (hi_void)memcpy_s(&vpss_chn_attr.chn_attr[0], sizeof(hi_vpss_chn_attr) * HI_VPSS_MAX_PHYS_CHN_NUM,
        vpss_cfg->chn_attr, sizeof(hi_vpss_chn_attr) * HI_VPSS_MAX_PHYS_CHN_NUM);
    (hi_void)memcpy_s(vpss_chn_attr.chn_enable, sizeof(vpss_chn_attr.chn_enable),
        vpss_cfg->chn_en, sizeof(vpss_chn_attr.chn_enable));
    (hi_void)memcpy_s(&vpss_chn_attr.wrap_attr[0], sizeof(hi_vpss_chn_buf_wrap_attr) * HI_VPSS_MAX_PHYS_CHN_NUM,
        vpss_cfg->wrap_attr, sizeof(hi_vpss_chn_buf_wrap_attr) * HI_VPSS_MAX_PHYS_CHN_NUM);
    vpss_chn_attr.chn_array_size = HI_VPSS_MAX_PHYS_CHN_NUM;
    ret = sample_common_vpss_start(grp, &vpss_cfg->grp_attr, &vpss_chn_attr);
    if (ret != HI_SUCCESS) {
        return ret;
    }

    if (vpss_cfg->chn_en[1]) {
        const hi_low_delay_info low_delay_info = { HI_TRUE, 200, HI_FALSE }; /* 200: lowdelay line */
        if (hi_mpi_vpss_set_chn_low_delay(grp, 1, &low_delay_info) != HI_SUCCESS) {
            goto stop_vpss;
        }
    }

    return HI_SUCCESS;

stop_vpss:
    sample_common_vpss_stop(grp, vpss_cfg->chn_en, HI_VPSS_MAX_PHYS_CHN_NUM);
    return HI_FAILURE;
}

static hi_void sample_venc_online_wrap_stop_vpss(hi_vpss_grp grp, sample_vpss_cfg *vpss_cfg)
{
    sample_common_vpss_stop(grp, vpss_cfg->chn_en, HI_VPSS_MAX_PHYS_CHN_NUM);
}

static hi_s32 sample_venc_online_wrap_start_sys_vi_vpss(sample_sns_type sns_type, sample_vi_cfg *vi_cfg,
    sample_vpss_cfg *vpss_cfg, sample_venc_param *enc_param)
{
    hi_s32 ret;
    sampe_sys_cfg sys_cfg = {0};
    hi_vi_pipe vi_pipe = 0;
    hi_vpss_grp vpss_grp = 0;

    /* *****************************************
      step 0: related parameter ready
    ***************************************** */
    sample_venc_online_wrap_get_default_sys_cfg(&sys_cfg);
    sample_comm_vi_get_default_vi_cfg(sns_type, vi_cfg);
    sample_venc_online_wrap_get_default_vpss_cfg(sns_type, vpss_cfg, enc_param);
    sample_venc_vpss_get_wrap_cfg(sns_type, &sys_cfg, vpss_cfg);

    /* *****************************************
      step 1: init sys alloc common vb
    ***************************************** */
   sample_print("sample_venc_online_wrap_sys_init 1 \n");
    if ((ret = sample_venc_online_wrap_sys_init(enc_param, &sys_cfg)) != HI_SUCCESS) 
    {
        sample_print("Init SYS err for %#x!\n", ret);
        return ret;
    }

    sample_print("sample_venc_vi_init 1 \n");
    if ((ret = sample_venc_vi_init(vi_cfg)) != HI_SUCCESS) {
        sample_print("Init VI err for %#x!\n", ret);
        goto EXIT_SYS_STOP;
    }

    sample_print("sample_venc_online_wrap_start_vpss 1 \n");
    if ((ret = sample_venc_online_wrap_start_vpss(vpss_grp, vpss_cfg)) != HI_SUCCESS) {
        sample_print("Init VPSS err for %#x!\n", ret);
        goto EXIT_VI_STOP;
    }

    if ((ret = sample_comm_vi_bind_vpss(vi_pipe, 0, vpss_grp, 0)) != HI_SUCCESS) {
        sample_print("VI Bind VPSS err for %#x!\n", ret);
        goto EXIT_VPSS_STOP;
    }

    return HI_SUCCESS;

EXIT_VPSS_STOP:
    sample_venc_online_wrap_stop_vpss(vpss_grp, vpss_cfg);
EXIT_VI_STOP:
    sample_venc_vi_deinit(vi_cfg);
EXIT_SYS_STOP:
    sample_comm_sys_exit();

    return ret;
}

static hi_void sample_venc_online_wrap_stop_sys_vi_vpss(sample_vi_cfg *vi_cfg, sample_vpss_cfg *vpss_cfg)
{
    hi_vi_pipe vi_pipe = 0;
    hi_vpss_grp vpss_grp = 0;

    sample_comm_vi_un_bind_vpss(vi_pipe, 0, vpss_grp, 0);
    sample_venc_online_wrap_stop_vpss(vpss_grp, vpss_cfg);
    sample_venc_vi_deinit(vi_cfg);
    sample_comm_sys_exit();
}

static hi_void sample_venc_set_pic_size_0(sample_sns_type sns_type, hi_pic_size *pic_size)
{
    pic_size[0] = BIG_STREAM_SIZE;
    if (sns_type == SC4336P_MIPI_3M_30FPS_10BIT) {
        pic_size[0] = PIC_2304X1296;
    }
}


static hi_vpss_grp vpss_grp = 0;
 static sample_venc_vpss_chn venc_vpss_chn = {0};
 sample_vi_cfg vi_cfg = {0};
 sample_vpss_cfg vpss_cfg = {0};
/* *****************************************************************************
 * function :  H.265e@2560x1440@30fps + h264e@1280x720@30fps
 * **************************************************************************** */
hi_s32 sample_venc_normal(hi_void)
{
    hi_s32 ret;
    sample_sns_type sns_type = 0;
    
    
   
    sample_venc_param enc_param = {0};
    hi_pic_size pic_size[CHN_NUM_MAX] = {0};

    /* *****************************************
      step 0: related parameter ready
    ***************************************** */
    sns_type = SENSOR0_TYPE;
    enc_param.venc_chn_num = MI_CV610_VPSS_CHN_NUM;
    if (enc_param.venc_chn_num <= 0 || enc_param.venc_chn_num > CHN_NUM_MAX) {
        enc_param.venc_chn_num = CHN_NUM_MAX;
    }
    sample_venc_set_pic_size_0(sns_type, pic_size);
    pic_size[0] = PIC_1080P;
    if (CHN_NUM_MAX > 1) {
        pic_size[1] = PIC_480P;
    }
    if (CHN_NUM_MAX > 2) {
        pic_size[2] = PIC_480P;
    }
    sample_venc_get_enc_size(pic_size, &enc_param);
    sample_set_venc_vpss_chn(&venc_vpss_chn, enc_param.venc_chn_num);

   
    printf("sns_type:%d \n",sns_type);



    
    /* *****************************************
      step 1: init sys alloc common vb
    ***************************************** */
    if ((ret = sample_venc_online_wrap_start_sys_vi_vpss(sns_type, &vi_cfg, &vpss_cfg, &enc_param)) != HI_SUCCESS) {
        return ret;
    }    /**/

    g_mpp_started = HI_TRUE;
    return HI_SUCCESS;
}


 void AppMppExit()
 {
     if (g_mpp_started != HI_TRUE) {
         return;
     }
      sample_venc_online_wrap_stop_sys_vi_vpss(&vi_cfg, &vpss_cfg);
     g_mpp_started = HI_FALSE;
     printf("sample_comm_sys_exit\n");
     printf("exit mpp ok\n");
 }
