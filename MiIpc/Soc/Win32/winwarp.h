#ifndef winwarp_H_
#define winwarp_H_

#include "../../Audio/miipc_audio_capture_stats.h"
#include "../IHard.h"

#ifdef __cplusplus
extern "C"
{
#endif

void InitWin32(FpOnAVStream cllAV, FpOnGetAudio readAudio);
void RequestIdrWin32(int chn);
int SetEncoderBitrateWin32(int chn, int bitrate_kbps);
int Win32AudioBufferFarendPcm(const int16_t* pcm, int samples, int sample_rate_hz);
int Win32AudioGetCaptureStats(miipc_audio_capture_stats_t* stats);
#ifdef __cplusplus
}
#endif
#endif
