#ifndef COMM_INFO_H_
#define COMM_INFO_H_
#include <stdint.h>


#ifdef __cplusplus
extern "C" 
{
#endif

    /***分辨率定义*****/
    enum EVideoPicWh
    {
        HV_UnDef = -1,
        H640_360 = 1,
        H1280_720 = 2,
        H1920_1080,
       
		
        HDef = 100
    };
	//编码器信息
	enum CodecInfo
	{
		CI_UnDef = -1,
		CI_H264 = 0,
		CI_H265,
		CI_JPG = 10,

		CI_OPUS = 51,
		CI_AAC = 52,
		CI_G711=53,
	    CI_PCM
	};



    /*******GPIO口定义与EAlarmType兼容*********/
    enum DefGPIONum
    {
        IO_LgGPIO1 = 501,
        IO_LgGPIO2 = 502,
        IO_LgGPIO3 = 503,
        IO_LgGPIO4 = 504,


        IO_LgGPIO5 = 505,
        IO_LgGPIO6 = 507,
        IO_LgGPIO7 = 508,
        IO_LgGPIO8 = 509,

        //默认低，拉高组
        IO_LgGPIO9 = 510,
        IO_LgGPIO10 = 511,
        IO_LgGPIO11 = 512,
        IO_LgGPIO12 = 513,


        IO_LgGPIOMax = 520,
        IO_LgWLED    = 600
    };


    //用于具体标记某一类
    enum EAlarmType
    {
        EAT_No = -1,
        //移动侦测
        EAT_MD = 0,
        //人脸检测
        EAT_IVP = 1,

        //人形检测
        EAT_Human = 4,
        //人脸识别
        EAT_HumanFace = 5,
        //车牌号码
        EAT_CarNo = 6,
        //车型
        EAT_CarSharp = 7,
        //录像，不作报警处理
        EAT_Record = 8,
        //拍照，不作报警处理
        EAT_Cap = 9,
        //不作报警处理
        EAT_CloudRec = 10,
        //不作报警处理
        EAT_AlarmRec = 11,
        EAT_TimerCap = 12,
        EAT_TimerRec = 13,
        /*****200到500是符合端口*****/
        EAT_ExDetect = 200,
        EAT_ExDetect2,
        EAT_ExDetect3,
        EAT_ExDetect4,
        EAT_ExComm = 500,

        //默认高，外部拉低
        EAT_IO_LgGPIO1 = 501,
        EAT_IO_LgGPIO2 = 502,
        EAT_IO_LgGPIO3 = 503,
        EAT_IO_LgGPIO4 = 504,


        EAT_IO_LgGPIO5 = 505,
        EAT_IO_LgGPIO6 = 507,
        EAT_IO_LgGPIO7 = 508,
        EAT_IO_LgGPIO8 = 509,

        //默认低，拉高组
        EAT_IO_LgGPIO9 = 510,
        EAT_IO_LgGPIO10 = 511,
        EAT_IO_LgGPIO11 = 512,
        EAT_IO_LgGPIO12 = 513,

        EAT_IO_LgGPIOMax = 520
    };
	
	struct Stream_Info
	{
		//CodecInfo
		int        enType;
		int        width;
		int        height;
		//视频是帧率，音频是采样率
		int        nFrameRate;
		uint8_t*   buffer;
        uint8_t*   srcbuffer;
        //音视频长度
		int        buflen;
		//单位毫秒
		uint64_t   pts;
		int        Ikey;
	};

    /********报警信息*********/
    typedef struct EAlarnInfo
    {
        /**报警类型****/
        enum EAlarmType aType;
        /***一些坐标信息存储这里***/
        char    strMsg[257];
        /****指示消息是否处理, 内部变量，阈值，用于控制通知****/
        uint8_t    nHandle;
        uint64_t   nOccTick;
    };
    struct SPoint
    {
        int x;
        int y;
    };
    struct SRect
    {
        int left;
        int top;
        int right;
        int bottom;
    };


   
   
    //AI算法阈值配置.
    struct SAlarmCfg
    {
        uint8_t  nMdEnable;          //移动侦测.

        uint8_t  nFigureEnable;      //人形检测.
        uint8_t  nFigureCon;         //人形阈值.

        uint8_t  nFaceEnable;        //人脸检测.
        uint8_t  nFaceCon;           //人脸阈值.

        uint8_t  nBCDEnable;         //婴儿哭声检测.

        /***基本AI检测间隔
           简单的阈值控制，单位毫秒最小值600ms，只对前4项有小.
        ***/
        int      nBseAiLimit;       

        //是否启用通知
        uint8_t  bNotify;

      
    };

    struct SAlarmAreaInfo
    {
        struct SPoint Ptpos[30];
        int           PtCount;
        char          name[128];

        //区域ID.
        int                 id;
        //指示是否显示.
        int                 isShow;

    
        int                 nAIType;
    
        int                 nAIExType;

        //报警标识，内部变量
        int                 isAlarm;
    };

    /*****
	  视频配置信息
	******/
    struct SVideoCfg
    { 
        //分辨率.
        enum EVideoPicWh chn1PicWh;     
        //最大帧率.
        int chn1MaxFrameRate;      
         //最大码率.
        int chn1MaxCodeRate;       
        enum CodecInfo chn1Encoder;
       //分辨率.
        enum  EVideoPicWh chn2PicWh; 
        //帧率.
        int   chn2FrameRate;
        //大码率.          
        int   chn2CodeRate;           
        enum  CodecInfo chn2Encoder;
    };

    /***拍照信息设置***/
    struct SCaptureCfg
    {
        enum EVideoPicWh PicWh;
        /***拍照质量***/
        int         nQuality;

    };

#ifdef __cplusplus
    }
#endif


#endif
