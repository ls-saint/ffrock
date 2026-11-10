#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include "WinAudioCapturer.h"

#include <Windows.h>
#include <audioclient.h>
#include <math.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mithread.h"
#include "../../Audio/miipc_g711.h"
#include "../CommInfo.h"

namespace MiniRtc
{
	static void win_audio_release_unknown(IUnknown** obj)
	{
		if (obj != NULL && *obj != NULL) {
			(*obj)->Release();
			*obj = NULL;
		}
	}

	static int win_audio_clamp16(int value)
	{
		if (value > 32767) {
			return 32767;
		}
		if (value < -32768) {
			return -32768;
		}
		return value;
	}

	static int win_audio_subformat_tag(const GUID* sub_format)
	{
		static const unsigned char guid_tail[8] = {
			0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
		};

		if (sub_format == NULL) {
			return 0;
		}
		if (sub_format->Data2 != 0x0000 ||
			sub_format->Data3 != 0x0010 ||
			memcmp(sub_format->Data4, guid_tail, sizeof(guid_tail)) != 0) {
			return 0;
		}
		return (int)sub_format->Data1;
	}

	static void win_audio_flush_log()
	{
		fflush(stdout);
	}

	CWinAudioCapturer::CWinAudioCapturer(FpOnAVStream cllAV)
		: m_cllAV(cllAV),
		  m_enumerator(NULL),
		  m_device(NULL),
		  m_audio_client(NULL),
		  m_capture_client(NULL),
		  m_mix_format(NULL),
		  m_buffer_frames(0),
		  m_initialized(0),
		  m_channels(0),
		  m_sample_rate(0),
		  m_bits_per_sample(0),
		  m_bytes_per_sample(0),
		  m_is_float(0),
		  m_source_pos(0.0),
		  m_packet_samples(0),
		  m_pts_ms(0),
		  m_audio_process(NULL)
	{
		memset(m_packet_pcm, 0, sizeof(m_packet_pcm));
		memset(m_processed_pcm, 0, sizeof(m_processed_pcm));
		memset(m_packet_g711, 0, sizeof(m_packet_g711));
	}

	CWinAudioCapturer::~CWinAudioCapturer()
	{
		Close();
	}

	void CWinAudioCapturer::Close()
	{
		if (m_audio_client != NULL) {
			m_audio_client->Stop();
		}

		win_audio_release_unknown((IUnknown**)&m_capture_client);
		win_audio_release_unknown((IUnknown**)&m_audio_client);
		win_audio_release_unknown((IUnknown**)&m_device);
		win_audio_release_unknown((IUnknown**)&m_enumerator);

		if (m_mix_format != NULL) {
			CoTaskMemFree(m_mix_format);
			m_mix_format = NULL;
		}

		m_initialized = 0;
		m_buffer_frames = 0;
		m_mono_queue.clear();
		m_source_pos = 0.0;
		m_packet_samples = 0;
		miipc_audio_process_destroy(m_audio_process);
		m_audio_process = NULL;
	}

	int CWinAudioCapturer::InitAudioProcess()
	{
		miipc_audio_process_config_t config;

		miipc_audio_process_load_config(&config);
		config.log_tag = "MiIpc Win32 audio";
		if (miipc_audio_process_create(&config, &m_audio_process) != 0) {
			m_audio_process = NULL;
			printf("MiIpc Win32 audio process init failed, use raw microphone audio\n");
			win_audio_flush_log();
			return -1;
		}
		return 0;
	}

	int CWinAudioCapturer::ParseFormat()
	{
		int format_tag;

		if (m_mix_format == NULL) {
			return -1;
		}

		m_channels = (int)m_mix_format->nChannels;
		m_sample_rate = (int)m_mix_format->nSamplesPerSec;
		m_bits_per_sample = (int)m_mix_format->wBitsPerSample;
		m_bytes_per_sample = m_bits_per_sample / 8;
		format_tag = (int)m_mix_format->wFormatTag;
		m_is_float = 0;

		if (format_tag == WAVE_FORMAT_EXTENSIBLE) {
			WAVEFORMATEXTENSIBLE* ext = (WAVEFORMATEXTENSIBLE*)m_mix_format;
			format_tag = win_audio_subformat_tag(&ext->SubFormat);
		}

		if (format_tag == WAVE_FORMAT_IEEE_FLOAT) {
			m_is_float = 1;
		} else if (format_tag != WAVE_FORMAT_PCM) {
			printf("MiIpc Win32 audio unsupported wave format tag=%d\n", format_tag);
			win_audio_flush_log();
			return -1;
		}

		if (m_channels <= 0 || m_channels > 16 ||
			m_sample_rate <= 0 ||
			m_bytes_per_sample <= 0 ||
			m_mix_format->nBlockAlign < m_channels * m_bytes_per_sample) {
			printf("MiIpc Win32 audio invalid format channels=%d rate=%d bits=%d align=%u\n",
				m_channels,
				m_sample_rate,
				m_bits_per_sample,
				(unsigned int)m_mix_format->nBlockAlign);
			win_audio_flush_log();
			return -1;
		}

		if (m_is_float && m_bits_per_sample != 32) {
			printf("MiIpc Win32 audio unsupported float bits=%d\n", m_bits_per_sample);
			win_audio_flush_log();
			return -1;
		}
		if (!m_is_float &&
			m_bits_per_sample != 8 &&
			m_bits_per_sample != 16 &&
			m_bits_per_sample != 24 &&
			m_bits_per_sample != 32) {
			printf("MiIpc Win32 audio unsupported pcm bits=%d\n", m_bits_per_sample);
			win_audio_flush_log();
			return -1;
		}

		return 0;
	}

	int CWinAudioCapturer::Init()
	{
		HRESULT hr;
		REFERENCE_TIME buffer_duration = 1000000;

		Close();

		hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
			NULL,
			CLSCTX_ALL,
			__uuidof(IMMDeviceEnumerator),
			(void**)&m_enumerator);
		if (FAILED(hr)) {
			printf("MiIpc Win32 audio CoCreateInstance failed hr=0x%08lx\n", (unsigned long)hr);
			win_audio_flush_log();
			return -1;
		}

		hr = m_enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &m_device);
		if (FAILED(hr)) {
			hr = m_enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &m_device);
		}
		if (FAILED(hr)) {
			printf("MiIpc Win32 audio no default capture device hr=0x%08lx\n", (unsigned long)hr);
			win_audio_flush_log();
			return -1;
		}

		hr = m_device->Activate(__uuidof(IAudioClient),
			CLSCTX_ALL,
			NULL,
			(void**)&m_audio_client);
		if (FAILED(hr)) {
			printf("MiIpc Win32 audio Activate IAudioClient failed hr=0x%08lx\n", (unsigned long)hr);
			win_audio_flush_log();
			return -1;
		}

		hr = m_audio_client->GetMixFormat(&m_mix_format);
		if (FAILED(hr) || m_mix_format == NULL) {
			printf("MiIpc Win32 audio GetMixFormat failed hr=0x%08lx\n", (unsigned long)hr);
			win_audio_flush_log();
			return -1;
		}

		if (ParseFormat() != 0) {
			return -1;
		}

		InitAudioProcess();

		hr = m_audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
			0,
			buffer_duration,
			0,
			m_mix_format,
			NULL);
		if (FAILED(hr)) {
			printf("MiIpc Win32 audio Initialize failed hr=0x%08lx\n", (unsigned long)hr);
			win_audio_flush_log();
			return -1;
		}

		hr = m_audio_client->GetBufferSize(&m_buffer_frames);
		if (FAILED(hr)) {
			printf("MiIpc Win32 audio GetBufferSize failed hr=0x%08lx\n", (unsigned long)hr);
			win_audio_flush_log();
			return -1;
		}

		hr = m_audio_client->GetService(__uuidof(IAudioCaptureClient),
			(void**)&m_capture_client);
		if (FAILED(hr)) {
			printf("MiIpc Win32 audio GetService failed hr=0x%08lx\n", (unsigned long)hr);
			win_audio_flush_log();
			return -1;
		}

		hr = m_audio_client->Start();
		if (FAILED(hr)) {
			printf("MiIpc Win32 audio Start failed hr=0x%08lx\n", (unsigned long)hr);
			win_audio_flush_log();
			return -1;
		}

		m_initialized = 1;
		if (m_audio_process != NULL) {
			miipc_audio_process_set_source_format(m_audio_process,
				1,
				m_sample_rate,
				m_channels,
				m_bits_per_sample);
		}
		printf("MiIpc Win32 audio capture started rate=%d channels=%d bits=%d float=%d buffer_frames=%u\n",
			m_sample_rate,
			m_channels,
			m_bits_per_sample,
			m_is_float,
			m_buffer_frames);
		win_audio_flush_log();
		return 0;
	}

	void CWinAudioCapturer::CaptureLoop()
	{
		while (m_initialized && m_capture_client != NULL) {
			UINT32 packet_frames = 0;
			HRESULT hr = m_capture_client->GetNextPacketSize(&packet_frames);

			if (FAILED(hr)) {
				printf("MiIpc Win32 audio GetNextPacketSize failed hr=0x%08lx\n", (unsigned long)hr);
				win_audio_flush_log();
				break;
			}

			while (packet_frames > 0) {
				BYTE* data = NULL;
				UINT32 frames = 0;
				DWORD flags = 0;

				hr = m_capture_client->GetBuffer(&data, &frames, &flags, NULL, NULL);
				if (FAILED(hr)) {
					printf("MiIpc Win32 audio GetBuffer failed hr=0x%08lx\n", (unsigned long)hr);
					win_audio_flush_log();
					return;
				}

				ProcessBuffer((const unsigned char*)data,
					(unsigned int)frames,
					(flags & AUDCLNT_BUFFERFLAGS_SILENT) ? 1 : 0);

				hr = m_capture_client->ReleaseBuffer(frames);
				if (FAILED(hr)) {
					printf("MiIpc Win32 audio ReleaseBuffer failed hr=0x%08lx\n", (unsigned long)hr);
					win_audio_flush_log();
					return;
				}

				hr = m_capture_client->GetNextPacketSize(&packet_frames);
				if (FAILED(hr)) {
					printf("MiIpc Win32 audio GetNextPacketSize failed hr=0x%08lx\n", (unsigned long)hr);
					win_audio_flush_log();
					return;
				}
			}

			mi_thread_sleep(5);
		}
	}

	int16_t CWinAudioCapturer::ReadMonoSample(const unsigned char* frame) const
	{
		int sum = 0;
		int ch;

		for (ch = 0; ch < m_channels; ++ch) {
			const unsigned char* sample = frame + ch * m_bytes_per_sample;
			int value = 0;

			if (m_is_float) {
				float f = 0.0f;
				memcpy(&f, sample, sizeof(f));
				if (f > 1.0f) {
					f = 1.0f;
				} else if (f < -1.0f) {
					f = -1.0f;
				}
				value = (int)(f * 32767.0f);
			} else if (m_bits_per_sample == 8) {
				value = ((int)sample[0] - 128) << 8;
			} else if (m_bits_per_sample == 16) {
				int16_t v = 0;
				memcpy(&v, sample, sizeof(v));
				value = (int)v;
			} else if (m_bits_per_sample == 24) {
				int v = ((int)sample[0]) |
					((int)sample[1] << 8) |
					((int)sample[2] << 16);
				if (v & 0x800000) {
					v |= ~0xFFFFFF;
				}
				value = v >> 8;
			} else if (m_bits_per_sample == 32) {
				int32_t v = 0;
				memcpy(&v, sample, sizeof(v));
				value = (int)(v >> 16);
			}

			sum += value;
		}

		return (int16_t)win_audio_clamp16(sum / m_channels);
	}

	void CWinAudioCapturer::ProcessBuffer(const unsigned char* data, unsigned int frames, int silent)
	{
		unsigned int i;
		unsigned int block_align;

		if (frames == 0) {
			return;
		}

		block_align = (unsigned int)m_mix_format->nBlockAlign;
		if (m_mono_queue.size() + frames > (size_t)m_sample_rate * 2u) {
			m_mono_queue.clear();
			m_source_pos = 0.0;
			m_packet_samples = 0;
		}

		m_mono_queue.reserve(m_mono_queue.size() + frames);
		for (i = 0; i < frames; ++i) {
			if (silent || data == NULL) {
				m_mono_queue.push_back(0);
			} else {
				m_mono_queue.push_back(ReadMonoSample(data + i * block_align));
			}
		}

		ResampleAndPacketize();
	}

	int CWinAudioCapturer::BufferFarendPcm(const int16_t* pcm, int samples, int sample_rate_hz)
	{
		if (m_audio_process == NULL || pcm == NULL || samples <= 0) {
			return -1;
		}
		return miipc_audio_process_buffer_farend(m_audio_process,
			pcm,
			(size_t)samples,
			sample_rate_hz);
	}

	int CWinAudioCapturer::GetStats(miipc_audio_capture_stats_t* stats)
	{
		if (stats == NULL) {
			return -1;
		}

		if (m_audio_process != NULL &&
			miipc_audio_process_get_stats(m_audio_process, stats) == 0) {
			stats->active = m_initialized;
			return 0;
		}
		memset(stats, 0, sizeof(*stats));
		stats->active = m_initialized;
		stats->sample_rate_hz = m_sample_rate;
		stats->channels = m_channels;
		stats->bits_per_sample = m_bits_per_sample;
		return -1;
	}

	void CWinAudioCapturer::ResampleAndPacketize()
	{
		double step;

		if (m_sample_rate <= 0) {
			return;
		}

		step = (double)m_sample_rate / 8000.0;
		while (m_source_pos + 1.0 < (double)m_mono_queue.size()) {
			int index = (int)m_source_pos;
			double frac = m_source_pos - (double)index;
			double s0 = (double)m_mono_queue[(size_t)index];
			double s1 = (double)m_mono_queue[(size_t)index + 1u];
			int sample = (int)(s0 + (s1 - s0) * frac);

			m_packet_pcm[m_packet_samples++] = (int16_t)win_audio_clamp16(sample);
			m_source_pos += step;

			if (m_packet_samples == 160) {
				EmitPacket();
			}
		}

		if (m_source_pos >= 1.0) {
			size_t drop = (size_t)m_source_pos;
			if (drop > m_mono_queue.size()) {
				drop = m_mono_queue.size();
			}
			if (drop > 0) {
				m_mono_queue.erase(m_mono_queue.begin(), m_mono_queue.begin() + drop);
				m_source_pos -= (double)drop;
			}
		}
	}

	void CWinAudioCapturer::EmitPacket()
	{
		struct Stream_Info info;
		const int16_t* pcm = m_packet_pcm;

		if (m_cllAV == NULL) {
			m_packet_samples = 0;
			return;
		}

		if (m_audio_process != NULL &&
			miipc_audio_process_capture(m_audio_process,
				m_packet_pcm,
				m_processed_pcm,
				160u,
				m_pts_ms) == 160) {
			pcm = m_processed_pcm;
		}
		miipc_g711a_encode(pcm, 160u, m_packet_g711);

		memset(&info, 0, sizeof(info));
		info.buffer = m_packet_g711;
		info.buflen = (int)sizeof(m_packet_g711);
		info.enType = CI_G711;
		info.nFrameRate = 8000;
		info.pts = m_pts_ms;
		m_pts_ms += 20;

		m_cllAV(100, &info, 1);
		m_packet_samples = 0;
	}
}
