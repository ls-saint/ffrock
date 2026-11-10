#include "winwarp.h"

#include "GdiCapturer.h"
#include "WinAudioCapturer.h"
#include "mimutex.h"
#include "mithread.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>

#define MI_WIN_AUDIO_PLAY_RATE 8000
#define MI_WIN_AUDIO_PLAY_CHANNELS 1
#define MI_WIN_AUDIO_PLAY_BITS 16
#define MI_WIN_AUDIO_PLAY_FRAME_BYTES 320

MiniRtc::CGdiCapturer* G_pCapturer=0;
static mi_thread_t G_CapThread;
static mi_thread_t G_AudioThread;
static mi_thread_t G_AudioPlayThread;
static FpOnAVStream G_AudioCallback = 0;
static FpOnGetAudio G_AudioReadCallback = 0;
static int G_AudioThreadStarted = 0;
static int G_AudioPlayThreadStarted = 0;
static mi_Mutex* G_AudioCapturerLock = 0;
static MiniRtc::CWinAudioCapturer* G_AudioCapturer = 0;

static void win_audio_capturer_lock()
{
	if (G_AudioCapturerLock != 0)
	{
		G_AudioCapturerLock->Lock(G_AudioCapturerLock);
	}
}

static void win_audio_capturer_unlock()
{
	if (G_AudioCapturerLock != 0)
	{
		G_AudioCapturerLock->UnLock(G_AudioCapturerLock);
	}
}

static void* OnWinCap(void* arg)
{
	(void)arg;
	G_pCapturer->Init(::GetDesktopWindow());
	while (true)
	{
		G_pCapturer->OnCapturePicture();
		mi_thread_sleep(50);
	}
	return NULL;
}

static void* OnWinAudioCap(void* arg)
{
	(void)arg;
	while (true)
	{
		HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
		int should_uninit = SUCCEEDED(hr) ? 1 : 0;

		if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE)
		{
			MiniRtc::CWinAudioCapturer audio(G_AudioCallback);
			if (audio.Init() == 0)
			{
				win_audio_capturer_lock();
				G_AudioCapturer = &audio;
				win_audio_capturer_unlock();
				audio.CaptureLoop();
				win_audio_capturer_lock();
				if (G_AudioCapturer == &audio)
				{
					G_AudioCapturer = 0;
				}
				win_audio_capturer_unlock();
			}
		}
		else
		{
			printf("MiIpc Win32 audio CoInitializeEx failed hr=0x%08lx\n", (unsigned long)hr);
			fflush(stdout);
		}

		if (should_uninit)
		{
			CoUninitialize();
		}
		mi_thread_sleep(1000);
	}
	return NULL;
}

static int win_audio_open_output(HWAVEOUT* wave_out)
{
	WAVEFORMATEX format;
	MMRESULT result;

	if (wave_out == NULL)
	{
		return -1;
	}
	memset(&format, 0, sizeof(format));
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = MI_WIN_AUDIO_PLAY_CHANNELS;
	format.nSamplesPerSec = MI_WIN_AUDIO_PLAY_RATE;
	format.wBitsPerSample = MI_WIN_AUDIO_PLAY_BITS;
	format.nBlockAlign = (WORD)((format.nChannels * format.wBitsPerSample) / 8);
	format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

	result = waveOutOpen(wave_out, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
	if (result != MMSYSERR_NOERROR)
	{
		printf("MiIpc Win32 audio output open failed ret=%u\n", (unsigned int)result);
		fflush(stdout);
		*wave_out = NULL;
		return -1;
	}
	printf("MiIpc Win32 audio output started rate=%d channels=%d bits=%d\n",
		MI_WIN_AUDIO_PLAY_RATE,
		MI_WIN_AUDIO_PLAY_CHANNELS,
		MI_WIN_AUDIO_PLAY_BITS);
	fflush(stdout);
	return 0;
}

static int win_audio_play_pcm(HWAVEOUT wave_out, const unsigned char* pcm, int bytes)
{
	WAVEHDR header;
	MMRESULT result;

	if (wave_out == NULL || pcm == NULL || bytes <= 0)
	{
		return -1;
	}
	memset(&header, 0, sizeof(header));
	header.lpData = (LPSTR)pcm;
	header.dwBufferLength = (DWORD)bytes;

	result = waveOutPrepareHeader(wave_out, &header, sizeof(header));
	if (result != MMSYSERR_NOERROR)
	{
		printf("MiIpc Win32 audio output prepare failed ret=%u\n", (unsigned int)result);
		fflush(stdout);
		return -1;
	}

	result = waveOutWrite(wave_out, &header, sizeof(header));
	if (result != MMSYSERR_NOERROR)
	{
		printf("MiIpc Win32 audio output write failed ret=%u\n", (unsigned int)result);
		fflush(stdout);
		waveOutUnprepareHeader(wave_out, &header, sizeof(header));
		return -1;
	}

	while ((header.dwFlags & WHDR_DONE) == 0)
	{
		mi_thread_sleep(2);
	}
	waveOutUnprepareHeader(wave_out, &header, sizeof(header));
	return 0;
}

static void* OnWinAudioPlay(void* arg)
{
	HWAVEOUT wave_out = NULL;
	unsigned char buffer[MI_WIN_AUDIO_PLAY_FRAME_BYTES];
	(void)arg;

	for (;;)
	{
		int bytes;
		FpOnGetAudio read_audio = G_AudioReadCallback;

		if (read_audio == NULL)
		{
			if (wave_out != NULL)
			{
				waveOutReset(wave_out);
				waveOutClose(wave_out);
				wave_out = NULL;
			}
			mi_thread_sleep(100);
			continue;
		}

		if (wave_out == NULL && win_audio_open_output(&wave_out) != 0)
		{
			mi_thread_sleep(1000);
			continue;
		}

		memset(buffer, 0, sizeof(buffer));
		bytes = read_audio(buffer,
			MI_WIN_AUDIO_PLAY_FRAME_BYTES,
			MI_WIN_AUDIO_PLAY_RATE,
			MI_WIN_AUDIO_PLAY_CHANNELS);
		if (bytes <= 0)
		{
			mi_thread_sleep(20);
			continue;
		}
		if (bytes > MI_WIN_AUDIO_PLAY_FRAME_BYTES)
		{
			bytes = MI_WIN_AUDIO_PLAY_FRAME_BYTES;
		}
		bytes &= ~1;
		if (bytes <= 0)
		{
			mi_thread_sleep(20);
			continue;
		}

		Win32AudioBufferFarendPcm((const int16_t*)buffer,
			bytes / (int)sizeof(int16_t),
			MI_WIN_AUDIO_PLAY_RATE);
		if (win_audio_play_pcm(wave_out, buffer, bytes) != 0)
		{
			waveOutReset(wave_out);
			waveOutClose(wave_out);
			wave_out = NULL;
			mi_thread_sleep(100);
		}
	}

	return NULL;
}

void InitWin32(FpOnAVStream cllAV, FpOnGetAudio readAudio)
{
	thread_config_t thread_config;

	memset(&thread_config, 0, sizeof(thread_config));
	G_pCapturer = new MiniRtc::CGdiCapturer(cllAV);
	G_AudioReadCallback = readAudio;
	if (G_AudioCapturerLock == 0)
	{
		G_AudioCapturerLock = mi_CreateMutex(1);
	}
	thread_config.name = "MiIpcWinCap";
	thread_config.priority = 0;
	thread_config.stack_size = 0;
	thread_config.detached = 1;
	mi_thread_create(&G_CapThread, OnWinCap, NULL, &thread_config);

	if (!G_AudioThreadStarted)
	{
		G_AudioCallback = cllAV;
		memset(&thread_config, 0, sizeof(thread_config));
		thread_config.name = "MiIpcWinAudio";
		thread_config.priority = 0;
		thread_config.stack_size = 0;
		thread_config.detached = 1;
		if (mi_thread_create(&G_AudioThread, OnWinAudioCap, NULL, &thread_config) == MI_THREAD_OK)
		{
			G_AudioThreadStarted = 1;
		}
		else
		{
			printf("MiIpc Win32 audio thread start failed\n");
			fflush(stdout);
		}
	}

	if (!G_AudioPlayThreadStarted)
	{
		memset(&thread_config, 0, sizeof(thread_config));
		thread_config.name = "MiIpcWinAudioOut";
		thread_config.priority = 0;
		thread_config.stack_size = 0;
		thread_config.detached = 1;
		if (mi_thread_create(&G_AudioPlayThread, OnWinAudioPlay, NULL, &thread_config) == MI_THREAD_OK)
		{
			G_AudioPlayThreadStarted = 1;
		}
		else
		{
			printf("MiIpc Win32 audio output thread start failed\n");
			fflush(stdout);
		}
	}
}

int Win32AudioBufferFarendPcm(const int16_t* pcm, int samples, int sample_rate_hz)
{
	int ret = -1;

	win_audio_capturer_lock();
	if (G_AudioCapturer != 0)
	{
		ret = G_AudioCapturer->BufferFarendPcm(pcm, samples, sample_rate_hz);
	}
	win_audio_capturer_unlock();
	return ret;
}

int Win32AudioGetCaptureStats(miipc_audio_capture_stats_t* stats)
{
	int ret = -1;

	if (stats == 0)
	{
		return -1;
	}
	memset(stats, 0, sizeof(*stats));
	win_audio_capturer_lock();
	if (G_AudioCapturer != 0)
	{
		ret = G_AudioCapturer->GetStats(stats);
	}
	win_audio_capturer_unlock();
	return ret;
}

void RequestIdrWin32(int chn)
{
	(void)chn;
	if (G_pCapturer != 0)
	{
		G_pCapturer->OnReqIDR();
	}
}

int SetEncoderBitrateWin32(int chn, int bitrate_kbps)
{
	(void)chn;
	if (G_pCapturer == 0)
	{
		return -1;
	}
	return G_pCapturer->SetBitrateKbps(bitrate_kbps);
}
