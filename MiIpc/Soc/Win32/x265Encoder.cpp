#include "x265Encoder.h"
#include <stdlib.h>
#include <string.h>
#pragma comment(lib, "libx265.lib")


CX265Encoder::CX265Encoder()
{
    memset(&m_X265EnCtx, 0, sizeof(m_X265EnCtx));
    IniParams(640, 360, 20);
}

CX265Encoder::~CX265Encoder()
{


}
void CX265Encoder::GenForceIFrame()
{
    m_nForceIFrame = 1;

}
int CX265Encoder::IniParams(int Width, int Height, int FrameRate)
{
    m_X265EnCtx.pH265Param = x265_param_alloc();
    //1.编码器参数初始化
    x265_param_default_preset(m_X265EnCtx.pH265Param, "ultafast", "zerolatecy");
   
  // x265_param_default(m_X265EnCtx.pH265Param);
    m_X265EnCtx.pH265Param->sourceWidth = Width;
    m_X265EnCtx.pH265Param->sourceHeight = Height;
  //  m_X265EnCtx.pH265Param->bBPyramid = 0;
    //关闭b帧
    m_X265EnCtx.pH265Param->bframes = 0;
   
    //编码帧率
    m_X265EnCtx.pH265Param->fpsNum = FrameRate;
   m_X265EnCtx.pH265Param->fpsDenom = 1;
    //write sps,pps 写在 keyframe之前
    m_X265EnCtx.pH265Param->bRepeatHeaders = 1;
    m_X265EnCtx.pH265Param->internalCsp = X265_CSP_I420;

    m_X265EnCtx.pH265Param->keyframeMax = 2 * FrameRate;

   // m_X265EnCtx.pH265Param->rc.bitrate = 1024 * 512;  //设置编码码率512kbps

   x265_param_apply_profile(m_X265EnCtx.pH265Param, x265_profile_names[0]);
   

   
    m_X265EnCtx.pX265Handle = x265_encoder_open(m_X265EnCtx.pH265Param);
    if (m_X265EnCtx.pX265Handle == NULL) {
    //    printf("x265_encoder_open fail\n");
        return 0;
    }
 
    m_X265EnCtx.pPicIn = x265_picture_alloc();
    x265_picture_init(m_X265EnCtx.pH265Param, m_X265EnCtx.pPicIn);



    int frame_pix = m_X265EnCtx.pH265Param->sourceWidth * m_X265EnCtx.pH265Param->sourceHeight;
    char* buff = (char*)malloc(frame_pix * 3 / 2);
    m_X265EnCtx.pPicIn->planes[0] = buff;
    m_X265EnCtx.pPicIn->planes[1] = buff + frame_pix;
    m_X265EnCtx.pPicIn->planes[2] = buff + frame_pix * 5 / 4;/**/

    m_X265EnCtx.pPicIn->stride[0] = m_X265EnCtx.pH265Param->sourceWidth;
    m_X265EnCtx.pPicIn->stride[1] = m_X265EnCtx.pH265Param->sourceWidth / 2;
    m_X265EnCtx.pPicIn->stride[2] = m_X265EnCtx.pH265Param->sourceWidth / 2;


    m_X265EnCtx.pPicOut = x265_picture_alloc();
    memset(m_X265EnCtx.pPicOut, 0, sizeof(x265_picture));

    x265_picture_init(m_X265EnCtx.pH265Param, m_X265EnCtx.pPicOut);

    return 0;
}
int CX265Encoder::EncodeFrame(uint8_t* Y, uint8_t* U, uint8_t* V, int64_t pts, int64_t& Outpts, int64_t& Outdts)
{
    //return 0;
    m_X265EnCtx.iNal = 0;

    //Y
    m_X265EnCtx.pPicIn->planes[0] = Y;
    //U
    m_X265EnCtx.pPicIn->planes[1] = U;
    //V
    m_X265EnCtx.pPicIn->planes[2] = V;


    if (m_nForceIFrame){
        m_X265EnCtx.pPicIn->sliceType = X265_TYPE_IDR;
        m_nForceIFrame = 0;
    }
    


    m_X265EnCtx.pPicIn->pts = pts * 90;

    int frame_size = x265_encoder_encode(m_X265EnCtx.pX265Handle, &m_X265EnCtx.pNals, &m_X265EnCtx.iNal, m_X265EnCtx.pPicIn, m_X265EnCtx.pPicOut);
    if (frame_size > 0 || m_X265EnCtx.iNal)
    {

        Outpts = m_X265EnCtx.pPicOut->pts;
        Outdts = m_X265EnCtx.pPicOut->dts;
        //m_pIStoreData
        //printf("frame_size:%d \n", frame_size);
    }
    return frame_size;

}