#include <Windows.h>
#include "GdiCapturer.h"
#include "libyuv.h"
#include "../CommInfo.h"

namespace MiniRtc
{
	CGdiCapturer::CGdiCapturer(FpOnAVStream       cllAV)
	{
		m_hSourceDC = 0;
		m_hDestDC = 0;
		m_nWidth = 0;
		m_nHeight = 0;
		m_pBuf = 0;
		m_nBufLen = 0;
		m_hBitmap = 0;
		m_cllAV = cllAV;
		InitializeCriticalSection(&m_EncoderLock);
	}
	CGdiCapturer::~CGdiCapturer()
	{
		::DeleteObject(m_hBitmap);
		m_hBitmap = 0;

		::DeleteDC(m_hDestDC);
		m_hDestDC = 0;

		::DeleteDC(m_hSourceDC);
		m_hSourceDC = 0;

		m_pBuf = NULL;
		m_nBufLen = 0;
		DeleteCriticalSection(&m_EncoderLock);
	}


	static void paint_mouse_pointer(HWND hwnd, HDC source_hdc, HDC dest_hdc, RECT clip_rect)
	{
		CURSORINFO ci = { 0 };

#define CURSOR_ERROR(str)                 \
    if (!gdigrab->cursor_error_printed) {       \
        WIN32_API_ERROR(str);             \
        gdigrab->cursor_error_printed = 1;      \
    }

		ci.cbSize = sizeof(ci);

		if (GetCursorInfo(&ci)) {
			HCURSOR icon = CopyCursor(ci.hCursor);
			ICONINFO info;
			POINT pos;


			int vertres = GetDeviceCaps(source_hdc, VERTRES);
			int desktopvertres = GetDeviceCaps(source_hdc, DESKTOPVERTRES);
			info.hbmMask = NULL;
			info.hbmColor = NULL;

			if (ci.flags != CURSOR_SHOWING)
				return;

			if (!icon) {
				icon = CopyCursor(LoadCursor(NULL, IDC_ARROW));
			}

			if (!GetIconInfo(icon, &info)) {
				goto icon_error;
			}

			pos.x = ci.ptScreenPos.x - clip_rect.left - info.xHotspot;
			pos.y = ci.ptScreenPos.y - clip_rect.top - info.yHotspot;

			if (hwnd) {
				RECT rect;

				if (GetWindowRect(hwnd, &rect)) {
					pos.x -= rect.left;
					pos.y -= rect.top;
				}
				else {
					goto icon_error;
				}
			}
			if (vertres == 0)
				vertres = 1;
			//that would keep the correct location of mouse with hidpi screens
			pos.x = pos.x * desktopvertres / vertres;
			pos.y = pos.y * desktopvertres / vertres;


			if (pos.x >= 0 && pos.x <= clip_rect.right - clip_rect.left &&
				pos.y >= 0 && pos.y <= clip_rect.bottom - clip_rect.top) {
				if (!DrawIcon(dest_hdc, pos.x, pos.y, icon))
				{
					int i = 0;
					i++;
				}
			}

		icon_error:
			if (info.hbmMask)
				DeleteObject(info.hbmMask);
			if (info.hbmColor)
				DeleteObject(info.hbmColor);
			if (icon)
				DestroyCursor(icon);
		}
		else {

		}
	}

	int CGdiCapturer::Init(HWND h)
	{
		m_hSourceDC = GetDC(NULL);
		int  vertres = GetDeviceCaps(m_hSourceDC, VERTRES);
		int  desktopvertres = GetDeviceCaps(m_hSourceDC, DESKTOPVERTRES);
		int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
		int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
		int right = (left + GetSystemMetrics(SM_CXVIRTUALSCREEN)) * desktopvertres / vertres;
		int bottom = (top + GetSystemMetrics(SM_CYVIRTUALSCREEN)) * desktopvertres / vertres;
		m_nWidth = 640;// right - left;
		m_nHeight = 360;// bottom - top;
		m_nBufLen = m_nWidth*m_nHeight * 4;

		int bpp = GetDeviceCaps(m_hSourceDC, BITSPIXEL);
		m_hDestDC = CreateCompatibleDC(m_hSourceDC);


		m_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		m_bmi.bmiHeader.biWidth = m_nWidth;
		m_bmi.bmiHeader.biHeight =  -m_nHeight;
		m_bmi.bmiHeader.biPlanes = 1;
		m_bmi.bmiHeader.biBitCount = 32;
		m_bmi.bmiHeader.biCompression = BI_RGB;
		m_bmi.bmiHeader.biSizeImage = 0;
		m_bmi.bmiHeader.biXPelsPerMeter = 0;
		m_bmi.bmiHeader.biYPelsPerMeter = 0;
		m_bmi.bmiHeader.biClrUsed = 0;
		m_bmi.bmiHeader.biClrImportant = 0;

		m_hBitmap = CreateDIBSection(m_hDestDC, &m_bmi, DIB_RGB_COLORS,
			&m_pBuf, NULL, 0);

		//    m_hBitmap =CreateCompatibleBitmap(m_hDDC,m_nWidth,m_nHeight);
		SelectObject(m_hDestDC, m_hBitmap);

		GetObject(m_hBitmap, sizeof(BITMAP), &m_bmi);
		return 1;
	}

	BOOL SaveBitmapToFile(HBITMAP   hBitmap, std::string szfilename)
	{
		HDC     hDC;
		//当前分辨率下每象素所占字节数          
		int     iBits;
		//位图中每象素所占字节数          
		WORD     wBitCount;
		//定义调色板大小，     位图中像素字节大小     ，位图文件大小     ，     写入文件字节数              
		DWORD     dwPaletteSize = 0, dwBmBitsSize = 0, dwDIBSize = 0, dwWritten = 0;
		//位图属性结构              
		BITMAP     Bitmap;
		//位图文件头结构          
		BITMAPFILEHEADER     bmfHdr;
		//位图信息头结构              
		BITMAPINFOHEADER     bi;
		//指向位图信息头结构                  
		LPBITMAPINFOHEADER     lpbi;
		//定义文件，分配内存句柄，调色板句柄              
		HANDLE     fh, hDib, hPal, hOldPal = NULL;

		//计算位图文件每个像素所占字节数              
		hDC = CreateDCA("DISPLAY", NULL, NULL, NULL);
		iBits = GetDeviceCaps(hDC, BITSPIXEL)     *     GetDeviceCaps(hDC, PLANES);
		DeleteDC(hDC);
		if (iBits <= 1)
			wBitCount = 1;
		else  if (iBits <= 4)
			wBitCount = 4;
		else if (iBits <= 8)
			wBitCount = 8;
		else
			wBitCount = 24;

		GetObject(hBitmap, sizeof(Bitmap), (LPSTR)&Bitmap);
		bi.biSize = sizeof(BITMAPINFOHEADER);
		bi.biWidth = Bitmap.bmWidth;
		bi.biHeight = Bitmap.bmHeight;
		bi.biPlanes = 1;
		bi.biBitCount = wBitCount;
		bi.biCompression = BI_RGB;
		bi.biSizeImage = 0;
		bi.biXPelsPerMeter = 0;
		bi.biYPelsPerMeter = 0;
		bi.biClrImportant = 0;
		bi.biClrUsed = 0;

		dwBmBitsSize = ((Bitmap.bmWidth *wBitCount + 31) / 32) * 4 * Bitmap.bmHeight;

		//为位图内容分配内存              
		hDib = GlobalAlloc(GHND, dwBmBitsSize + dwPaletteSize + sizeof(BITMAPINFOHEADER));
		lpbi = (LPBITMAPINFOHEADER)GlobalLock(hDib);
		*lpbi = bi;

		//     处理调色板                  
		hPal = GetStockObject(DEFAULT_PALETTE);
		if (hPal)
		{
			hDC = ::GetDC(NULL);
			hOldPal = ::SelectPalette(hDC, (HPALETTE)hPal, FALSE);
			RealizePalette(hDC);
		}

		//     获取该调色板下新的像素值              
		GetDIBits(hDC, hBitmap, 0, (UINT)Bitmap.bmHeight,
			(LPSTR)lpbi + sizeof(BITMAPINFOHEADER) + dwPaletteSize,
			(BITMAPINFO *)lpbi, DIB_RGB_COLORS);

		//恢复调色板                  
		if (hOldPal)
		{
			::SelectPalette(hDC, (HPALETTE)hOldPal, TRUE);
			RealizePalette(hDC);
			::ReleaseDC(NULL, hDC);
		}

		//创建位图文件                  
		fh = CreateFileA(szfilename.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);

		if (fh == INVALID_HANDLE_VALUE)         return     FALSE;

		//     设置位图文件头              
		bmfHdr.bfType = 0x4D42;     //     "BM"              
		dwDIBSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dwPaletteSize + dwBmBitsSize;
		bmfHdr.bfSize = dwDIBSize;
		bmfHdr.bfReserved1 = 0;
		bmfHdr.bfReserved2 = 0;
		bmfHdr.bfOffBits = (DWORD)sizeof(BITMAPFILEHEADER) + (DWORD)sizeof(BITMAPINFOHEADER) + dwPaletteSize;
		//     写入位图文件头              
		WriteFile(fh, (LPSTR)&bmfHdr, sizeof(BITMAPFILEHEADER), &dwWritten, NULL);
		//     写入位图文件其余内容              
		WriteFile(fh, (LPSTR)lpbi, dwDIBSize, &dwWritten, NULL);
		//清除                  
		GlobalUnlock(hDib);
		GlobalFree(hDib);
		CloseHandle(fh);

		return     TRUE;
	}
	int CGdiCapturer::OnCapturePicture(int save, std::string strPath)
	{
	//	return 0;
		int w =  m_nWidth;
		int h =  m_nHeight;
		BOOL xx = BitBlt(m_hDestDC, 0, 0, w, h, m_hSourceDC, 0, 0, SRCCOPY);//| CAPTUREBLT
		
		RECT clip_rect = { 0,0,w, h};
		if(!save)
		paint_mouse_pointer(0, m_hSourceDC, m_hDestDC, clip_rect);
	
		int  stride_y = w;
		int  stride_u = w / 2;
		int  stride_v = w / 2;

		int yuvlen  = stride_y * h + stride_u*h/2 + stride_v *h/2+10000;
		uint8_t* buf = (uint8_t*)::malloc(yuvlen);
		if(!save){

			uint8_t* Y = buf;
			uint8_t* U = buf+ stride_y*h;
			uint8_t* V = buf + stride_y * h+ stride_u * h/2;
			int ff=libyuv::ARGBToI420(
				(const uint8_t*)m_pBuf, w * 4,
				Y, stride_y,
				U, stride_u,
				V, stride_v,
				w, h);

			EncodeH264(Y, U, V);
		}else if (save==1) {
			SaveBitmapToFile(m_hBitmap, strPath);
		}else {
		
		}
		free(buf);
		return m_nBufLen;
	}
	int   CGdiCapturer::GetYuvLen()
	{
		
		return	0;
	}
	int   CGdiCapturer::CopyYuvData(uint8_t* yuvData)
	{
		
		return  0;
	}
	//windows 是BGRA
	void* CGdiCapturer::GetARGBBuf(int &w, int &h)
	{
		w = m_nWidth;
		h = m_nHeight;
		return m_pBuf;
	}
	FILE* H26xFile2 = 0;
	int CGdiCapturer::EncodeH264(uint8_t* Ybuf, uint8_t* Ubuf, uint8_t* Vbuf)
	{

		if (Ybuf == 0 || Ubuf == 0 || Vbuf == 0)
			return 0;

		int64_t   pts = GetTickCount64();
		int ret = 0;

		static int64_t   pts2 = 0;
		if (pts2 == 0)
			pts2 = pts;
		//	pts = pts - pts2 * 90;
		int64_t Outpts = 0; int64_t Outdts = 0;



		EnterCriticalSection(&m_EncoderLock);
		ret = m_X264Encoder.EncodeFrame(Ybuf, Ubuf, Vbuf, pts, Outpts, Outdts);
		if (ret <= 0)
		{
			LeaveCriticalSection(&m_EncoderLock);
			return 0;
		}
		X264EnCtx* EnCtx = m_X264Encoder.GetCtx();
		int fragments_count = EnCtx->iNal;
		int frame_len = 0;
		int is_key = 0;
		uint8_t* frame_buf = NULL;
		uint8_t* write_ptr = NULL;
		Stream_Info info;

		for (int i = 0; i < fragments_count; ++i)
		{
			int len = EnCtx->pNals[i].i_payload;
			uint8_t* data = EnCtx->pNals[i].p_payload;
			int i_type = EnCtx->pNals[i].i_type;

			if (len <= 0 || data == 0)
				continue;
			if (i_type == NAL_SLICE_IDR || i_type == NAL_SPS || i_type == NAL_PPS)
				is_key = 1;
			frame_len += len;
			if (!(len >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1))
			{
				if (len >= 3 && data[0] == 0 && data[1] == 0 && data[2] == 1)
					frame_len += 1;
				else
					frame_len += 4;
			}
		}

		if (frame_len <= 0)
		{
			LeaveCriticalSection(&m_EncoderLock);
			return 0;
		}

		frame_buf = (uint8_t*)::malloc(frame_len);
		if (frame_buf == 0)
			return 0;
		write_ptr = frame_buf;

		for (int i = 0; i < fragments_count; ++i)
		{
			int len = EnCtx->pNals[i].i_payload;
			uint8_t* data = EnCtx->pNals[i].p_payload;

			if (len <= 0 || data == 0)
				continue;
			if (len >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1)
			{
				memcpy(write_ptr, data, len);
				write_ptr += len;
			}
			else if (len >= 3 && data[0] == 0 && data[1] == 0 && data[2] == 1)
			{
				*write_ptr++ = 0;
				memcpy(write_ptr, data, len);
				write_ptr += len;
			}
			else
			{
				write_ptr[0] = 0;
				write_ptr[1] = 0;
				write_ptr[2] = 0;
				write_ptr[3] = 1;
				write_ptr += 4;
				memcpy(write_ptr, data, len);
				write_ptr += len;
			}
		}

		LeaveCriticalSection(&m_EncoderLock);

		memset(&info, 0, sizeof(info));
		info.nFrameRate = 30;
		info.Ikey = is_key;
		info.buffer = frame_buf;
		info.buflen = (int)(write_ptr - frame_buf);
		info.enType = CI_H264;
		info.pts = Outpts;
		info.width = m_nWidth;
		info.height = m_nHeight;

		if (H26xFile2 != 0) {
			fwrite(info.buffer, 1, info.buflen, H26xFile2);
		}
		if (m_cllAV)
		{
			m_cllAV(0, &info, 1);
		}
		free(frame_buf);		return 0;
		
	}

	int CGdiCapturer::SetBitrateKbps(int bitrate_kbps)
	{
		int ret;
		EnterCriticalSection(&m_EncoderLock);
		ret = m_X264Encoder.SetBitrateKbps(bitrate_kbps);
		LeaveCriticalSection(&m_EncoderLock);
		return ret;
	}
	void  CGdiCapturer::OnReqIDR()
	{
		m_X264Encoder.GenForceIFrame();
	}
}