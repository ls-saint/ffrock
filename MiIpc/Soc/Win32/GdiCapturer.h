#ifndef CGdiCapturer_H_
#define CGdiCapturer_H_
#include <Windows.h>
#include <stddef.h>
#include <string>
#include "x264Encoder.h"
#include "comdef.h"
#include "../IHard.h"
namespace MiniRtc
{
	class CGdiCapturer
	{
	public:
		CGdiCapturer(
			FpOnAVStream    cllAV
		);
		~CGdiCapturer();
		int Init(HWND h);

		int   OnCapturePicture(int save=0,std::string strPath="");
		void* GetARGBBuf(int &Outwidth, int &OutHeight);
		void  OnReqIDR();
		int   SetBitrateKbps(int bitrate_kbps);
		int   GetYuvLen();
		int   CopyYuvData(uint8_t* yuvData);
		//void  SetStream(IVideoStream* pStream);
	protected:
		int   EncodeH264(uint8_t* Ybuf,uint8_t* Ubuf,uint8_t* Vbuf);
	private:
		FpOnAVStream       m_cllAV;
		CX264Encoder       m_X264Encoder;
		CRITICAL_SECTION   m_EncoderLock;
		int                m_nWidth;
		int                m_nHeight;
		HDC                m_hSourceDC;
		HDC                m_hDestDC;
		void*          m_pBuf;
		int            m_nBufLen;
		HBITMAP        m_hBitmap;
		BITMAPINFO     m_bmi;

		uint8_t*       m_nBufX = 0;
		int            m_nBufLenX = 0;
		uint8_t*       m_pYuv=0;
		int            m_nYuvlen = 0;
		
	};
}
#endif
