#ifndef x265Encoder_H_
#define x265Encoder_H_
extern "C"
{
	#include "x265_config.h"
	#include "x265.h"
}


typedef struct X265EnCtx
{
	x265_param* pH265Param;
	x265_encoder* pX265Handle;
	x265_picture* pPicIn;
	x265_picture* pPicOut;
	x265_nal* pNals;
	uint32_t iNal;
	/************没有任何作用，只是作为变量缓存*************/
	unsigned char* InY;
	unsigned char* InU;
	unsigned char* InV;

	/***********编码后的数据缓存****************/
	unsigned char* pH265Buf;
	int            H265BufLen;

}X265EnCtx;

class CX265Encoder
{
   public:
		   CX265Encoder();
		   ~CX265Encoder();
		   int IniParams(int Width, int Height, int FrameRate);
		   void GenForceIFrame();
		   X265EnCtx* GetCtx() { return   &m_X265EnCtx; }
		   int EncodeFrame(uint8_t* Y, uint8_t* U, uint8_t* V, int64_t pts, int64_t& Outpts, int64_t& Outdts);
private:
	       X265EnCtx     m_X265EnCtx;
		   int m_nForceIFrame = 0;

};


#endif