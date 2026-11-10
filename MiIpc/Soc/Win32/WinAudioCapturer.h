#ifndef WIN_AUDIO_CAPTURER_H_
#define WIN_AUDIO_CAPTURER_H_

#include <stdint.h>
#include <vector>

#include "../IHard.h"
#include "../../Audio/miipc_audio_capture_stats.h"
#include "../../Audio/miipc_audio_process.h"

typedef struct IAudioClient IAudioClient;
typedef struct IAudioCaptureClient IAudioCaptureClient;
typedef struct IMMDevice IMMDevice;
typedef struct IMMDeviceEnumerator IMMDeviceEnumerator;
typedef struct tWAVEFORMATEX WAVEFORMATEX;
struct mi_Mutex;
namespace MiniRtc
{
	class CWinAudioCapturer
	{
	public:
		explicit CWinAudioCapturer(FpOnAVStream cllAV);
		~CWinAudioCapturer();

		int Init();
		void CaptureLoop();
		int BufferFarendPcm(const int16_t* pcm, int samples, int sample_rate_hz);
		int GetStats(miipc_audio_capture_stats_t* stats);

	private:
		void Close();
		int ParseFormat();
		void ProcessBuffer(const unsigned char* data, unsigned int frames, int silent);
		int16_t ReadMonoSample(const unsigned char* frame) const;
		void ResampleAndPacketize();
		void EmitPacket();
		int InitAudioProcess();

	private:
		FpOnAVStream m_cllAV;
		IMMDeviceEnumerator* m_enumerator;
		IMMDevice* m_device;
		IAudioClient* m_audio_client;
		IAudioCaptureClient* m_capture_client;
		WAVEFORMATEX* m_mix_format;
		unsigned int m_buffer_frames;
		int m_initialized;
		int m_channels;
		int m_sample_rate;
		int m_bits_per_sample;
		int m_bytes_per_sample;
		int m_is_float;
		double m_source_pos;
		std::vector<int16_t> m_mono_queue;
		int16_t m_packet_pcm[160];
		int16_t m_processed_pcm[160];
		unsigned char m_packet_g711[160];
		int m_packet_samples;
		uint64_t m_pts_ms;
		miipc_audio_process_t* m_audio_process;
	};
}

#endif
