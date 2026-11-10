
#ifndef kkX264Encoder_H_
#define kkX264Encoder_H_

#include <stdio.h>
#include <stdint.h>
extern "C" 
{
#include "x264_config.h"
#include "x264.h"
}

typedef struct X264EnCtx
 {
     x264_param_t*   pH264Param;
	 x264_t*         pX264Handle;
	 x264_picture_t* pPicIn;  
     x264_picture_t* pPicOut; 
	 x264_nal_t     *pNals;
	 int iNal;
	 /************没有任何作用，只是作为变量缓存*************/
	 unsigned char *InY;
	 unsigned char *InU;
     unsigned char *InV;

	 /***********编码后的数据缓存****************/
	 unsigned char       *pH264Buf;
	 int                  H264BufLen;

 }X264EnCtx;
class CX264Encoder
{
    public:
		CX264Encoder();
		~CX264Encoder();
	    int IniParams(int Width,int Height,int FrameRate);
		void GenForceIFrame();
		int SetBitrateKbps(int bitrate_kbps);
	    //编码成h264
	    int EncodeFrame(uint8_t* Y,  uint8_t* U,  uint8_t* V, int64_t pts, int64_t& Outpts, int64_t& Outdts);
	    int CloseEncoder();
		X264EnCtx* GetCtx() { return &m_EnCtx; }

private:
	    X264EnCtx   m_EnCtx;
		int         m_nForceIFrame;
		uint16_t    m_nPictureId=0;
		
};
#endif