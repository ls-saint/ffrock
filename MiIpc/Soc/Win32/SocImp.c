#include <stdio.h>
#include <malloc.h>
#include "SocImp.h"
#include "winwarp.h"


typedef struct __ComHardSoc
{
	struct IHardSoc ISoc;
    FpOnAVStream    cllAV; 
    FpOnGetAudio    readAudio;
}ComHardSoc;
static ComHardSoc* G_pHardSoc = 0;
int InitSoc()
{
    return 1;
}
/*****获取视频配置*****/
void   GetVPCfg(struct IHardSoc* warp, struct SVideoCfg* vcfg, struct SCaptureCfg* ccfg)
{

}
/***指示光线是否差(即接近黑夜)**/
int    GetLowlight(struct IHardSoc* warp)
{
	return 0;
}
/***检测IR-CUT.**/
void   OnCheckIrCut(struct IHardSoc* warp) {

}
/***自带ptz.**/
void   OnInMoveMotor(struct IHardSoc* warp, int dir, int step) {
	return ;
}

/*****isAo=1 只是AO模块  0 是AI模块 ********/
void   SetAiAoVol(struct IHardSoc* warp, int isAo, int vol) {
	return ;
}
/***设置输出型，GPIO是否为高低.**/
void   SetGpioValue(struct IHardSoc* warp, enum DefGPIONum num, int value) {
	return ;
}
void   SetGpioDir(struct IHardSoc* warp, enum DefGPIONum num, int value) {
	return ;
}
int    GetGpioValue(struct IHardSoc* warp, enum DefGPIONum nGpioNo) 
{
	return 0;
}
int    ResetKeyValue(struct IHardSoc* warp)
{
   return 1;
}
/***打开补光灯(白光灯)***/
void   OpWRLed(struct IHardSoc* warp, int op) {
	return ;
}
/*****获取OSD对应通道大小，即vpss大小******/
void   GetOsdChnSize(struct IHardSoc* warp, int OsdId, int* width, int* height) {
	return ;
}
/****外部注册不需要实现*****/
int    GetStream(struct IHardSoc* warp) 
{
#ifdef WIN32
    Sleep(100);
#else
    usleep(100000);
#endif
	return 0;
}
int    CapturePic(struct IHardSoc* warp, const char* strPath) 
{
	return 0;
}
//0 h264 1 h265  10 jpg 获取通道的编码信息
int    GetEnTypeByChn(struct IHardSoc* warp, int chn) 
{
	return 0;
}
int    SetEnTypeByChn(struct IHardSoc* warp, int chn, int EnType) {
	return 0;
}
/***设置视频编码器信息***/
int    SetVEncodeInfo(struct IHardSoc* warp, int EncodeIndex, enum EVideoPicWh    PicWh, int MaxRate, int u32MaxBitRate) 
{
	return 0;
}
/***调整编码器码率  kbit/s****/
int    AdjustEncoderRate(struct IHardSoc* warp, int EncodeIndex, int u32MaxBitRate) 
{
	(void)warp;
	return SetEncoderBitrateWin32(EncodeIndex, u32MaxBitRate);
}
//kbit/s.
int    GetEncoderRate(struct IHardSoc* warp, int EncodeIndex, int* nDefMaxBitRate) {
	(void)warp;
	(void)EncodeIndex;
	if (nDefMaxBitRate != 0) {
		*nDefMaxBitRate = 0;
	}
	return 0;
}
int    SetPicEnInfo(struct IHardSoc* warp, enum EVideoPicWh    PicWh, int Qp) 
{
	return 0;
}

/***请求I帧***/
void   ReqIDR(struct IHardSoc* warp, int chn) {
	RequestIdrWin32(chn);
	return ;
}

int    SetMirror(struct IHardSoc* warp, int VpssGrp, int VpssChn, int value) {
	return 0;
}
int    GetMirror(struct IHardSoc* warp, int VpssGrp, int VpssChn) {
	return 0;
}

int    GetIspInfo(struct IHardSoc* warp, struct CommIspInfo* ispInfo) {
	return 0;
}
int    SetIspInfo(struct IHardSoc* warp, struct CommIspInfo* ispInfo) {
	return 0;
}

/***水平畸变***/
int    SetCommLDCH(struct IHardSoc* warp, const struct CommLDCH* ldch) {
	return 0;
}
int    GetCommLDCH(struct IHardSoc* warp, struct CommLDCH* ldch) {
	return 0;
}

//HDR.
int    SetCommHDR(struct IHardSoc* warp, const struct CommHDR* hdr) {
	return 0;
}
int    GetCommHDR(struct IHardSoc* warp, struct CommHDR* hdr)
{
	return 0;
}

int    SetExposTime(struct IHardSoc* warp, const struct ExposTime* exposTime)
{
	return 0;
}
int    GetExposTime(struct IHardSoc* warp, struct ExposTime* exposTime)
{
	return 0;
}

int    SetBacklightComp(struct IHardSoc* warp, const struct BacklightComp* bk)
{
	return 0;
}
int    GetBacklightComp(struct IHardSoc* warp, struct BacklightComp* bk)
{
	return 0;
}

/***白平衡******/
int    SetAwbMode(struct IHardSoc* warp, const struct AwbMode* bk)
{
	return 0;
}
int    GetAwbMode(struct IHardSoc* warp, struct AwbMode* bk)
{
	return 0;
}

/***抗闪****/
int    SetAntiFlicker(struct IHardSoc* warp, const struct AntiFlicker* bk)
{
	return 0;

}
int    GetAntiFlicker(struct IHardSoc* warp, struct AntiFlicker* bk)
{

	return 0;

}

/***高光抑制***/
int    SetHLC(struct IHardSoc* warp, const struct CommHLC* bk)
{
	return 0;
}
int    GetHLC(struct IHardSoc* warp, struct CommHLC* bk)
{

	return 0;
}

int    GetAf(struct IHardSoc* warp, unsigned int* Fv1, unsigned int* Fv2)
{

	return 0;
}

/*****Start OSD *******/
void   UpdateOsd(struct IHardSoc* warp, int index)
{
	return ;
}
int    SetOsdShow(struct IHardSoc* warp, int Index, int show)
{
	return 0;
}

int    GetOsdShow(struct IHardSoc* warp, int Index)
{
	return 0;
}


/***设置字体大小.**/
int    SetFontSize(struct IHardSoc* warp, int Index, int nFontSize)
{
	return 0;
}
int    GetFontSize(struct IHardSoc* warp, int Index)
{
	return 0;
}


int    ResetOsdSize(struct IHardSoc* warp, int index, int posx, int posy, int width, int height)
{
	return 0;
}
int    setTextAlign(struct IHardSoc* warp, int index, int nAlign)
{
	return 0;
}
int    SetOsdText(struct IHardSoc* warp, int index, const char* text)
{

	return 0;
}

void   InitHard(struct IHardSoc* warp, FpOnAVStream cllAV, FpOnGetAudio readAudio, const struct SVideoCfg* vcfg, const  struct SCaptureCfg* ccfg)
{
    printf("InitHard \n");
    G_pHardSoc->cllAV = cllAV;
    G_pHardSoc->readAudio = readAudio;
    InitWin32(cllAV, readAudio);
}


void  OnCheckStatus(struct IHardSoc* warp)
{
}
struct IHardSoc* CreateHardImp()
{
	if (G_pHardSoc)
		return &G_pHardSoc->ISoc;
    G_pHardSoc = (ComHardSoc*)malloc(sizeof(ComHardSoc));
	struct IHardSoc* ISoc = &G_pHardSoc->ISoc;
    ISoc->OnCheckStatus = OnCheckStatus;
    ISoc->GetVPCfg = GetVPCfg;
    ISoc->GetLowlight = GetLowlight;
    ISoc->OnCheckIrCut = OnCheckIrCut;
    ISoc->OnInMoveMotor = OnInMoveMotor;
    ISoc->SetAiAoVol = SetAiAoVol;
    ISoc->SetGpioValue = SetGpioValue;
    ISoc->SetGpioDir = SetGpioDir;
    ISoc->GetGpioValue = GetGpioValue;
    ISoc->GetResetKeyValue = ResetKeyValue;
    ISoc->OpWRLed = OpWRLed;
    ISoc->GetOsdChnSize = GetOsdChnSize;
    ISoc->GetStream = GetStream;
    ISoc->CapturePic = CapturePic;
    ISoc->GetEnTypeByChn = GetEnTypeByChn;
    ISoc->SetEnTypeByChn = SetEnTypeByChn;
    ISoc->SetVEncodeInfo = SetVEncodeInfo;
    ISoc->AdjustEncoderRate = AdjustEncoderRate;
    ISoc->GetEncoderRate = GetEncoderRate;
    ISoc->SetPicEnInfo = SetPicEnInfo;
    ISoc->ReqIDR = ReqIDR;
    ISoc->SetMirror = SetMirror;
    ISoc->GetMirror = GetMirror;
    ISoc->GetIspInfo = GetIspInfo;
    ISoc->SetIspInfo = SetIspInfo;
    ISoc->SetCommLDCH = SetCommLDCH;
    ISoc->GetCommLDCH = GetCommLDCH;
    ISoc->SetCommHDR = SetCommHDR;
    ISoc->GetCommHDR = GetCommHDR;
    ISoc->SetExposTime = SetExposTime;
    ISoc->GetExposTime = GetExposTime;
    ISoc->SetBacklightComp = SetBacklightComp;
    ISoc->GetBacklightComp = GetBacklightComp;
    ISoc->SetAwbMode = SetAwbMode;
    ISoc->GetAwbMode = GetAwbMode;
    ISoc->SetAntiFlicker = SetAntiFlicker;
    ISoc->GetAntiFlicker = GetAntiFlicker;
    ISoc->SetHLC = SetHLC;
    ISoc->GetHLC = GetHLC;
    ISoc->GetAf = GetAf;
    ISoc->UpdateOsd = UpdateOsd;
    ISoc->SetOsdShow = SetOsdShow;
    ISoc->GetOsdShow = GetOsdShow;
    ISoc->SetFontSize = SetFontSize;
    ISoc->GetFontSize = GetFontSize;
    ISoc->ResetOsdSize = ResetOsdSize;
    ISoc->SetTextAlign = setTextAlign;
    ISoc->SetOsdText = SetOsdText;
    ISoc->InitHard = InitHard;

    return ISoc;
}
