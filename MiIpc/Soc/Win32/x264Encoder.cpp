#include "x264Encoder.h"
#include <stdlib.h>
#include <memory.h>
#include <assert.h>
#if defined(_M_X64) || defined(_WIN64)
#pragma comment(lib, "libx264-148.lib")
#else
#pragma comment(lib, "libx264.lib")
#endif

CX264Encoder::CX264Encoder():m_nForceIFrame(0)
{
	memset(&m_EnCtx,0,sizeof(m_EnCtx));
	IniParams(640, 360, 20);
}
CX264Encoder::~CX264Encoder()
{

}
	  

int CX264Encoder::IniParams(int Width, int Height,int FrameRate)
{
	memset(&m_EnCtx, 0, sizeof(m_EnCtx));
	m_EnCtx.pH264Param = (x264_param_t*)::malloc(sizeof(x264_param_t));
	memset(m_EnCtx.pH264Param, 0, sizeof(x264_param_t));
	//* 设置Profile.使用Baseline profile  


	//* 配置参数  
	//* 使用默认参数，在这里因为我的是实时网络传输，所以我使用了zerolatency的选项，
	//使用这个选项之后就不会有delayed_frames，如果你使用的不是这样的话，还需要在编码完成之后得到缓存的编码帧  
	//x264_param_default_preset(m_EnCtx.pH264Param, "veryfast", "zerolatency");
	x264_param_default_preset(m_EnCtx.pH264Param, x264_preset_names[0], "zerolatency");

	x264_param_apply_profile(m_EnCtx.pH264Param, x264_profile_names[0]);

	//* cpuFlags  
	m_EnCtx.pH264Param->i_threads = X264_SYNC_LOOKAHEAD_AUTO;//* 取空缓冲区继续使用不死锁的保证.  
															 //* 视频选项  
	m_EnCtx.pH264Param->i_width = Width;  //* 要编码的图像宽度.  
	m_EnCtx.pH264Param->i_height = Height; //* 要编码的图像高度  
	m_EnCtx.pH264Param->i_frame_total = 0;  //* 编码总帧数.不知道用0.  
	m_EnCtx.pH264Param->i_keyint_max = 20;
	m_EnCtx.pH264Param->rc.i_rc_method =  X264_RC_ABR;

	//* 流参数  
	m_EnCtx.pH264Param->i_bframe = 0;  //不适用b帧
	m_EnCtx.pH264Param->b_open_gop = 0;
	m_EnCtx.pH264Param->i_bframe_pyramid = 0;
	m_EnCtx.pH264Param->i_bframe_adaptive = X264_B_ADAPT_TRELLIS;
	m_EnCtx.pH264Param->b_repeat_headers = 1;  // 重复SPS/PPS 放到关键帧前面      
	m_EnCtx.pH264Param->b_sliced_threads = 1;
	m_EnCtx.pH264Param->rc.i_lookahead = 0;
	m_EnCtx.pH264Param->rc.b_mb_tree = 0;
	/* 根据公式看到减少帧延时的方法，也就是（zerolatency 设置）
	param->rc.i_lookahead = 0;
	param->i_sync_lookahead = 0;
	param->i_bframe = 0;
	param->b_sliced_threads = 1;
	param->b_vfr_input = 0;
	param->rc.b_mb_tree = 0;*/
	//x264 zerolatency
	//* Log参数，不需要打印编码信息时直接注释掉就行  
	// m_EnCtx.pH264Param->i_log_level  = X264_LOG_DEBUG;  
	//* 速率控制参数  
	m_EnCtx.pH264Param->rc.i_bitrate = 200;//* 码率(比特率,单位Kbps)  
											 //* muxing parameters  
	m_EnCtx.pH264Param->i_fps_den = 1; //* 帧率分母  
	m_EnCtx.pH264Param->i_fps_num = FrameRate;//* 帧率分子  
	m_EnCtx.pH264Param->i_timebase_den = FrameRate;
	m_EnCtx.pH264Param->i_timebase_num = 1;
	

	//* 编码需要的辅助变量  
	m_EnCtx.pPicIn = (x264_picture_t*)::malloc(sizeof(x264_picture_t));
	memset(m_EnCtx.pPicIn, 0, sizeof(x264_picture_t));

	m_EnCtx.pPicOut = (x264_picture_t*)::malloc(sizeof(x264_picture_t));
	memset(m_EnCtx.pPicOut, 0, sizeof(x264_picture_t));

	x264_picture_init(m_EnCtx.pPicOut);

	x264_picture_alloc(m_EnCtx.pPicIn, X264_CSP_I420, m_EnCtx.pH264Param->i_width, m_EnCtx.pH264Param->i_height);
	m_EnCtx.pPicIn->img.i_csp = X264_CSP_I420;
	m_EnCtx.pPicIn->img.i_plane = 3;
	m_EnCtx.InY = m_EnCtx.pPicIn->img.plane[0];
	m_EnCtx.InU = m_EnCtx.pPicIn->img.plane[1];
	m_EnCtx.InV = m_EnCtx.pPicIn->img.plane[2];
	/* m_EnCtx.pPicIn->img.i_stride[0]=m_EnCtx.pH264Param->i_width;
	m_EnCtx.pPicIn->img.i_stride[1]=m_EnCtx.pH264Param->i_width/2;
	m_EnCtx.pPicIn->img.i_stride[2]=m_EnCtx.pH264Param->i_width/2;*/

	//* 打开编码器句柄,通过x264_encoder_parameters得到设置给X264  
	//* 的参数.通过x264_encoder_reconfig更新X264的参数  

	//图像质量控制,越大越不清晰
	m_EnCtx.pH264Param->rc.f_rf_constant = 30;
	m_EnCtx.pH264Param->rc.f_rf_constant_max = 50;/**/

	m_EnCtx.pX264Handle = x264_encoder_open(m_EnCtx.pH264Param);
	assert(m_EnCtx.pX264Handle);
	m_EnCtx.H264BufLen = m_EnCtx.pH264Param->i_width* m_EnCtx.pH264Param->i_height * 4;
	m_EnCtx.pH264Buf = (unsigned char*)::malloc(m_EnCtx.H264BufLen);
	memset(m_EnCtx.pH264Buf, 0, m_EnCtx.H264BufLen);
  return 0;
}
void CX264Encoder::GenForceIFrame()
{
	m_nForceIFrame=1;
}
int CX264Encoder::SetBitrateKbps(int bitrate_kbps)
{
	if (bitrate_kbps < 100) bitrate_kbps = 100;
	if (bitrate_kbps > 2500) bitrate_kbps = 2500;
	if (m_EnCtx.pH264Param == NULL || m_EnCtx.pX264Handle == NULL) return -1;
	m_EnCtx.pH264Param->rc.i_rc_method = X264_RC_ABR;
	m_EnCtx.pH264Param->rc.i_bitrate = bitrate_kbps;
	m_EnCtx.pH264Param->rc.i_vbv_max_bitrate = bitrate_kbps;
	m_EnCtx.pH264Param->rc.i_vbv_buffer_size = bitrate_kbps;
	return x264_encoder_reconfig(m_EnCtx.pX264Handle, m_EnCtx.pH264Param);
}
//编码成h264
int CX264Encoder::EncodeFrame( uint8_t* Y,  uint8_t* U,  uint8_t* V,int64_t pts,int64_t& Outpts, int64_t& Outdts)
{
	//return 0;
	    //编码  
	    m_EnCtx.pNals=NULL;
	    m_EnCtx.iNal=0;
		
		//Y
		m_EnCtx.pPicIn->img.plane[0] = Y;
		//U
		m_EnCtx.pPicIn->img.plane[1] = U;
		//V
		m_EnCtx.pPicIn->img.plane[2] = V;
		if(m_nForceIFrame)
		{
			 m_EnCtx.pPicIn->i_type = X264_TYPE_IDR;
		     m_nForceIFrame=0;
		}
		else
			m_EnCtx.pPicIn->i_type = 0;
		m_EnCtx.pPicIn->i_pts=pts*90;
		
        int frame_size = x264_encoder_encode(m_EnCtx.pX264Handle,&m_EnCtx.pNals,&m_EnCtx.iNal,m_EnCtx.pPicIn,m_EnCtx.pPicOut);  
		if (frame_size>0)
		{

			Outpts = m_EnCtx.pPicOut->i_pts;
			Outdts = m_EnCtx.pPicOut->i_dts;
			//m_pIStoreData
			//printf("frame_size:%d \n", frame_size);
		}
        return frame_size;
}
int CX264Encoder::CloseEncoder()
{
	m_nPictureId = 1;
     //* 关闭编码器句柄  
	if(m_EnCtx.pX264Handle!=NULL){
     x264_encoder_close(m_EnCtx.pX264Handle);  
	 m_EnCtx.pX264Handle=NULL;
	}
	if(m_EnCtx.pPicOut!=NULL){
		free(m_EnCtx.pPicOut);
		m_EnCtx.pPicOut=NULL;
	}
	//* 清除图像区域  
	if(m_EnCtx.pPicIn!=NULL){
	 m_EnCtx.pPicIn->img.plane[0]=m_EnCtx.InY;
	 m_EnCtx.pPicIn->img.plane[1]=m_EnCtx.InU;
     m_EnCtx.pPicIn->img.plane[2]=m_EnCtx.InV;
     x264_picture_clean(m_EnCtx.pPicIn);  
	 free(m_EnCtx.pPicIn);
	 m_EnCtx.pPicIn=NULL;
	}
	if(m_EnCtx.pH264Param!=NULL){
	 free(m_EnCtx.pH264Param);
	 m_EnCtx.pH264Param=NULL;
	}
	
	 if(m_EnCtx.pH264Buf!=NULL)
	 {
	    free(m_EnCtx.pH264Buf);
	    m_EnCtx.pH264Buf=NULL;
	 }
     return 0;
}