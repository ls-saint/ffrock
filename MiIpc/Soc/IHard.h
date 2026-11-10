#ifndef  I_HARD_H_
#define  I_HARD_H_

#ifdef WIN32
   
#else
    #define IPCSO_API  __attribute__((visibility ("default"))) 
#endif

#include "CommInfo.h"


#ifdef __cplusplus
extern "C" {
#endif

    /************/
    typedef void (*FpOnAVStream)(int chn, struct Stream_Info* info, int count);

    /*******设备读取要播放的音频信息,即将音频数据给设备播放*******/
    typedef int  (*FpOnGetAudio)(uint8_t* buf,int buflen,int rz, int chn);
    /*******ai 报警消息回调********/
    typedef void (*FpAlarmNotify)(int index,enum EAlarmType type,int AreaId,const char* strValue);

    /********************************/
    /***只能做一些简单的*****/
    struct CommIspInfo
    {
        /***-1 不支持，或者未启用.***/
        //亮度.***/
        int nBrightness;
        /***对比度   0, 1, 2***/
        int nContrast;
        /***锐度.***/
        int nSharpness;
       /***饱和度,  0 ,1, 2***/
        int nSaturation;

        /***色度.***/
        int nHue;
       /***模拟增益.***/
        int nAGainMin;
        int nAGainMax;
    };

    /****畸形矫正*****/
    struct CommLDCH
    {
        /***是否启动和芯片有关.***/
        int nEn;
        /***级别，和芯片有关.***/
        int nLevel;
    };

    /***高光遮蔽/强光抑制***/
    struct CommHLC
    {
        /***是否启动和芯片有关.***/
        int nEn;
        /***级别，和芯片有关.***/
        int nLevel;
    };

    /***WDR/HDR 设置***/
    struct CommHDR
    {
        /***是否启动和芯片有关***/
        int nEn;
        /***级别，和芯片有关***/
        int nLevel;
        int nMaxLevel;
    };

    /***曝光时间***/
    struct ExposTime
    {
        /***是否启动和芯片有关***/
        int nTime1;
        /***级别，和芯片有关***/
        int nTime2;
    };
    /***背光补偿***/
    struct BacklightComp
    {
        /***是否启动和芯片有关***/
        int nEn;
        /***级别，和芯片有关***/
        int nLevel;
    };
   
    /***白平横***/
    struct AwbMode
    {
        /****0 自动 1 手动 ,***/ 
        int  model;
        int ragin;
        int gagin;
        int bagin;
    };
    /***抗闪****/
    struct AntiFlicker
    {
        int En;
        //0 50  1:60
        int Value;
    };

   
 typedef struct IHardSoc
 {
        //初始化硬件
        void   (*InitHard)(
            struct IHardSoc* warp,
            FpOnAVStream cllAV ,FpOnGetAudio readAudio,
            const struct SVideoCfg* vcfg, const  struct SCaptureCfg* ccfg);

        void   (*ReLoadHardCfg)(struct IHardSoc* warp);
        void   (*GetVPCfg)(struct IHardSoc* warp, struct SVideoCfg* vcfg, struct SCaptureCfg* ccfg);
        /***指示光线是否差(即接近黑夜)**/
        int    (*GetLowlight)(struct IHardSoc* warp) ;
        /***检测IR-CUT.**/
        void   (*OnCheckIrCut)(struct IHardSoc* warp);
        /***自带ptz.**/
        void   (*OnInMoveMotor)(struct IHardSoc* warp,int x, int y);

        void   (*SetAiAoVol)(struct IHardSoc* warp,int isAo, int vol);
        /***设置输出型，GPIO是否为高低.**/
        void   (*SetGpioValue)(struct IHardSoc* warp,enum DefGPIONum num, int value);
        void   (*SetGpioDir)(struct IHardSoc* warp,enum DefGPIONum num, int value);
        int    (*GetGpioValue)(struct IHardSoc* warp,enum DefGPIONum nGpioNo);
        int    (*GetResetKeyValue)(struct IHardSoc* warp);
        /***打开补光灯(白光灯)***/
        void   (*OpWRLed)(struct IHardSoc* warp,int op);
        /*****获取OSD对应通道大小，即vpss大小******/
        void   (*GetOsdChnSize)(struct IHardSoc* warp,int OsdId, int* width, int* height);
        int    (*GetStream)(struct IHardSoc* warp);
        int    (*CapturePic)(struct IHardSoc* warp,const char* strPath);
        //0 h264 1 h265  10 jpg 获取通道的编码信息
        int    (*GetEnTypeByChn)(struct IHardSoc* warp,int chn);
        int    (*SetEnTypeByChn)(struct IHardSoc* warp,int chn, int EnType);
        /***设置视频编码器信息***/
        int    (*SetVEncodeInfo)(struct IHardSoc* warp,int EncodeIndex, enum EVideoPicWh    PicWh, int MaxRate, int u32MaxBitRate);
        /***调整编码器码率  kbit/s****/
        int    (*AdjustEncoderRate)(struct IHardSoc* warp,int EncodeIndex, int u32MaxBitRate);
        //kbit/s.
        int    (*GetEncoderRate)(struct IHardSoc* warp,int EncodeIndex, int* nDefMaxBitRate);
        int    (*SetPicEnInfo)(struct IHardSoc* warp,enum EVideoPicWh    PicWh, int Qp);

        /***请求I帧***/
        void   (*ReqIDR)(struct IHardSoc* warp,int chn);

        int    (*SetMirror)(struct IHardSoc* warp,int VpssGrp, int VpssChn, int value);
        int    (*GetMirror)(struct IHardSoc* warp,int VpssGrp, int VpssChn) ;

        int    (*GetIspInfo)(struct IHardSoc* warp,struct CommIspInfo* ispInfo);
        int    (*SetIspInfo)(struct IHardSoc* warp,struct CommIspInfo* ispInfo);

        /***水平畸变***/
        int    (*SetCommLDCH)(struct IHardSoc* warp,const struct CommLDCH* ldch);
        int    (*GetCommLDCH)(struct IHardSoc* warp,struct CommLDCH* ldch) ;

        //HDR.
        int    (*SetCommHDR)(struct IHardSoc* warp,const struct CommHDR* hdr);
        int    (*GetCommHDR)(struct IHardSoc* warp,struct CommHDR* hdr) ;

        int    (*SetExposTime)(struct IHardSoc* warp,const struct ExposTime* exposTime) ;
        int    (*GetExposTime)(struct IHardSoc* warp,struct ExposTime* exposTime) ;

        int    (*SetBacklightComp)(struct IHardSoc* warp,const struct BacklightComp* bk) ;
        int    (*GetBacklightComp)(struct IHardSoc* warp,struct BacklightComp* bk);

        /***白平衡******/
        int    (*SetAwbMode)(struct IHardSoc* warp,const struct AwbMode* bk);
        int    (*GetAwbMode)(struct IHardSoc* warp,struct AwbMode* bk);

        /***抗闪****/
        int    (*SetAntiFlicker)(struct IHardSoc* warp,const struct AntiFlicker* bk);
        int    (*GetAntiFlicker)(struct IHardSoc* warp,struct AntiFlicker* bk);

        /***高光抑制***/
        int    (*SetHLC)(struct IHardSoc* warp,const struct CommHLC* bk);
        int    (*GetHLC)(struct IHardSoc* warp,struct CommHLC* bk);
      
        int    (*GetAf)(struct IHardSoc* warp,unsigned int* Fv1, unsigned int* Fv2) ;

        /*****Start OSD *******/
        void   (*UpdateOsd)(struct IHardSoc* warp,int index);
        int    (*SetOsdShow)(struct IHardSoc* warp,int Index, int show);
        int    (*GetOsdShow)(struct IHardSoc* warp,int Index);


        /***设置字体大小.**/
        int    (*SetFontSize)(struct IHardSoc* warp,int Index, int nFontSize);
        int    (*GetFontSize)(struct IHardSoc* warp,int Index);


        int    (*ResetOsdSize)(struct IHardSoc* warp,int index, int posx, int posy, int width, int height);
        int    (*SetTextAlign)(struct IHardSoc* warp,int index, int nAlign);
        int    (*SetOsdText)  (struct IHardSoc* warp,int index, const char* text);

        void   (*OnCheckStatus)(struct IHardSoc* warp);
        /*******ai检测处理*******/
        void   (*OnAiHandle)(struct SAlarmCfg* AlarmCfg,const struct SAlarmAreaInfo* AlarmAreas,int AreaCount,FpAlarmNotify notify);
        /******end OSD*****/
 }IHardSoc;


 /*****内部调用，如果实现这返回，没有实现返回0******/
 struct IHardSoc  *CreateHardImp();
#ifdef __cplusplus
}
#endif


#endif
