#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <Windows.h>
#include "mad.h"
#include "madmp3.h"
#include "resample.h"
static E_PLAY_STATUS g_nPlayStatus = PLAY_STOPED_STATE;
int setPlayerStatus(E_PLAY_STATUS nPlayStatus)
{
	g_nPlayStatus = nPlayStatus;
	return 0;
}

int getPlayerStatus()
{
	return g_nPlayStatus;
}





#define IN_BUFFER_SIZE	(2880 + 1 + MAD_BUFFER_GUARD)	//According to Rob, this is the biggest-possible frame.
//Increase by 1 to avoid bugging the available data check within frame.c.

struct Mp3Stream
{
	FILE* file;
	FILE* pcmfd;
	rs_data* pRsdata;
	unsigned char buffer[IN_BUFFER_SIZE];
};

static int played = 0;		/* playing time in milliseconds */
static int hypnosisTime = 0;		/* playing time in milliseconds */
static struct mad_decoder decoder;

int getPlayerTime()
{
	return played / 1000;
}




int getHypnosisTime()
{
	return hypnosisTime / 1000;
}


static signed int madscale(mad_fixed_t sample)
{
	sample += (1l << (MAD_F_FRACBITS - 16));	/* round */
	if (sample >= MAD_F_ONE)			/* clip */
		sample = MAD_F_ONE - 1;
	if (sample < -MAD_F_ONE)
		sample = -MAD_F_ONE;
	return sample >> (MAD_F_FRACBITS + 1 - 16);	/* quantize */
}


static enum mad_flow input_handler(void* buffer, struct mad_stream* stream)
{
	struct Mp3Stream* istream = (struct Mp3Stream*)buffer;
	int LeftOverData_level, len;

	if (g_nPlayStatus == PLAY_STOPED_STATE)
    {

        return MAD_FLOW_STOP;
    }

	//When more data is required, but none is left.
	if (feof(istream->file))
		return MAD_FLOW_STOP;

	if (g_nPlayStatus == PLAY_PAUSE_STATE)
	{
		//printf("(%d) <%s> ---------------PLAY_PAUSE_STATE \n", __LINE__, __FUNCTION__);
		Sleep(500);
		return MAD_FLOW_CONTINUE;
	}

	if (stream->next_frame == NULL || stream->error == MAD_ERROR_BUFLEN)
	{	//When there's a partial frame at the end of the buffer.
		LeftOverData_level = stream->bufend - stream->next_frame;

		/*	Sanity check: when there is a BUFLEN error but this buffer is full,
			the request likely cannot be satisfied. Break! */
		if (stream->error == MAD_ERROR_BUFLEN && LeftOverData_level == IN_BUFFER_SIZE)
			return MAD_FLOW_BREAK;

		memmove(istream->buffer, stream->next_frame, LeftOverData_level);
	}
	else
		LeftOverData_level = 0;

	len = fread(&istream->buffer[LeftOverData_level], 1, IN_BUFFER_SIZE - LeftOverData_level, istream->file);
	//printf("(%d) <%s> ---------------len = %d \n", __LINE__, __FUNCTION__, len);
	if (len + LeftOverData_level < IN_BUFFER_SIZE)
		memset(&istream->buffer[len + LeftOverData_level], 0, IN_BUFFER_SIZE - (len + LeftOverData_level));	//Erase guard bytes.

	mad_stream_buffer(stream, istream->buffer, len + LeftOverData_level);
	return MAD_FLOW_CONTINUE;
}

/*
 * The following utility routine performs simple rounding, clipping, and
 * scaling of MAD's high-resolution samples down to 16 bits. It does not
 * perform any dithering or noise shaping, which would be recommended to
 * obtain any exceptional audio quality. It is therefore not recommended to
 * use this routine if high-quality output is desired.
 */

static inline signed int scale(mad_fixed_t sample)
{
	/* round */
	sample += (1L << (MAD_F_FRACBITS - 16));

	/* clip */
	if (sample >= MAD_F_ONE)
		sample = MAD_F_ONE - 1;
	else if (sample < -MAD_F_ONE)
		sample = -MAD_F_ONE;

	/* quantize */
	return sample >> (MAD_F_FRACBITS + 1 - 16);
}



static char mixed[4094];
static enum mad_flow output_handler(void* buffer,
	struct mad_header const* header,
	struct mad_pcm* pcm)
{
	unsigned int nchannels, nsamples;
	mad_fixed_t const* left_ch, * right_ch;
	int madTime = 0;

	if (g_nPlayStatus == PLAY_STOPED_STATE)
    {
      
        return MAD_FLOW_STOP;
    }
	if (pcm->length < 1152)
		return MAD_FLOW_CONTINUE;

	/* pcm->samplerate contains the sampling frequency */
	if (pcm->samplerate == 0)
	{
		return MAD_FLOW_CONTINUE;
	}


	nchannels = pcm->channels;
	nsamples = pcm->length;
	left_ch = pcm->samples[0];
	right_ch = pcm->samples[1];

	

	int i = 0;
	madTime = mad_timer_count(decoder.sync->frame.header.duration, MAD_UNITS_MILLISECONDS);
	played += madTime;
	hypnosisTime += madTime;
	//printf("(%d) <%s> ---------------played=%d,  samplerate=%d chn=%d length=%d \n", __LINE__, __FUNCTION__, played, pcm->samplerate, pcm->channels, pcm->length);

	while (nsamples--) {
		signed int sample;

		/* output sample(s) in 16-bit signed little-endian PCM */

		sample = scale(*left_ch++);
		//putchar((sample >> 0) & 0xff);
		//putchar((sample >> 8) & 0xff);
		mixed[i++] = (sample >> 0) & 0xff;
		mixed[i++] = (sample >> 8) & 0xff;

		/*if (nchannels == 2) {
			sample = scale(*right_ch++);
			mixed[i++] = (sample >> 0) & 0xff;
			mixed[i++] = (sample >> 8) & 0xff;
		}*/

	}

	//AudioMp3DataPlay(mixed, pcm->length * 2);
	Sleep(20);
	Mp3Stream* istream = (Mp3Stream*)buffer;
	
	int16_t bufmm[2048] = { 0x0 };
	if(istream->pRsdata == 0)// 
		istream->pRsdata = resample_init(pcm->samplerate, 8000, 2048);
	int16_t* bux = (int16_t*)::malloc(1024);

	
	int Pcmlen = resample(istream->pRsdata, (short*)mixed,
		pcm->length, (short*)bux, 1024, 0);

	/*static FILE* pcmfd = ::fopen("D://testx41k.pcm", "wb+");
	if (pcmfd)
	{
       ::fwrite((short*)mixed,1, pcm->length*2, pcmfd);
	}*/

	printf("%d %d \n", pcm->length, pcm->samplerate);
   /* int inlx =  pcm->length*1000/pcm->samplerate;
	int16_t* buf = (int16_t*)::malloc(pcm->length*2+64);
	memcpy(buf, mixed, pcm->length * 2);
	*/

     //av->PutAudioPcm((int16_t*)buf, pcm->length, pcm->samplerate)
   


//	fwrite(bux, 1, Pcmlen * 2, istream->pcmfd);
	return MAD_FLOW_CONTINUE;
}
static enum mad_flow error_handler(void* buffer,
	struct mad_stream* stream,
	struct mad_frame* frame)
{	//There may be a "lost sync" error at the start. There is no code to handle the ID3 header, so ignore such recoverable errors.
	return (MAD_RECOVERABLE(stream->error) ? MAD_FLOW_IGNORE : MAD_FLOW_BREAK);
}

int testMad(const char* filename)
{

    Mp3Stream istream;
	memset(&istream, 0, sizeof(istream));
	if ((istream.file = fopen(filename, "rb")) != NULL)
	{
		played = 0;
        hypnosisTime = 0;
		g_nPlayStatus = PLAY_PLAYING_STATE;
		mad_decoder_init(&decoder, &istream, &input_handler, NULL, NULL, &output_handler, &error_handler, NULL);
		mad_decoder_options(&decoder, MAD_OPTION_IGNORECRC);	//Disable CRC check
		mad_decoder_run(&decoder, MAD_DECODER_MODE_SYNC);

		mad_decoder_finish(&decoder);
		fclose(istream.file);
		//ERROR_LOG3("---------------------------------------------\n");
		g_nPlayStatus = PLAY_STOPED_STATE;
		return 0;
	}
	else
		return ENOENT;
	//ERROR_LOG3("---------------------------------------------\n");
}




/**
http://openaudio.cos.tx.xmcdn.com/group12/M03/3B/B8/wKgDW1WcwvHgpaztABo1oYeL3WM766.mp3
http://openaudio.cos.tx.xmcdn.com/group12/M00/3B/BA/wKgDW1Wcw4KRwaQoAA3X6jbFdrI099.mp3
http://openaudio.cos.tx.xmcdn.com/group10/M08/3B/BD/wKgDaVWcwmKDRr9VAAf1BcwyTP8640.mp3
http://openaudio.cos.tx.xmcdn.com/group12/M04/3B/B1/wKgDXFWcwvPQyIoDAAs7WDZORKo649.mp3
http://openaudio.cos.tx.xmcdn.com/group7/M0A/3C/83/wKgDWlWcw32ikTitABiSCXj-nlk484.mp3
http://openaudio.cos.tx.xmcdn.com/group8/M05/3B/C9/wKgDYFWcu7eDZXt9AAyEWEWOVu8151.mp3
http://openaudio.cos.tx.xmcdn.com/group62/M04/2F/6E/wKgMZ10-rieSvUjqACWbHbpMHDo631.mp3
http://openaudio.cos.tx.xmcdn.com/group3/M00/9A/7A/wKgDslKwCBDzEzuvAAptlvppx3M628.mp3
http://openaudio.cos.tx.xmcdn.com/group10/M08/3B/C9/wKgDZ1Wcwl_iUyNLAAgyM9DhuF0525.mp3
http://openaudio.cos.tx.xmcdn.com/group11/M06/3C/10/wKgDa1WcwxigaYOhAAeZ4ZOgOgg456.mp3
http://openaudio.cos.tx.xmcdn.com/group15/M0B/3C/33/wKgDaFWcwu7AyR7yAA1-HqD5nPY508.mp3
http://openaudio.cos.tx.xmcdn.com/group7/M0A/3C/82/wKgDWlWcw2rhoK_cAAwLgQNQqJ0996.mp3
http://openaudio.cos.tx.xmcdn.com/group14/M02/3C/1A/wKgDY1WcwYfjGY7DAAndIyENW1M215.mp3
http://openaudio.cos.tx.xmcdn.com/group11/M06/3C/14/wKgDbVWcwtOiustkAAi3MWinVBc601.mp3
http://openaudio.cos.tx.xmcdn.com/group11/M07/3C/12/wKgDa1WcxH_xR_1DAA32iaDP0ZI375.mp3
http://openaudio.cos.tx.xmcdn.com/group7/M01/3C/79/wKgDWlWcvGjRTn-JAAnZ35OCeiM925.mp3
http://szali-ibabycloud-media.oss-cn-shenzhen.aliyuncs.com/common/media/audio/ibaby.mp3
http://szali-ibabycloud-media.oss-cn-shenzhen.aliyuncs.com/common/media/audio/20150317163232112.mp3
http://szali-ibabycloud-media.oss-cn-shenzhen.aliyuncs.com/common/media/audio/20150317170251182.mp3
http://szali-ibabycloud-media.oss-cn-shenzhen.aliyuncs.com/common/media/audio/20150317170407156.mp3
http://szali-ibabycloud-media.oss-cn-shenzhen.aliyuncs.com/common/media/audio/20150317170510921.mp3
http://szali-ibabycloud-media.oss-cn-shenzhen.aliyuncs.com/common/media/audio/20150317170604524.mp3
http://szali-ibabycloud-media.oss-cn-shenzhen.aliyuncs.com/common/media/audio/20150317170641456.mp3
http://szali-ibabycloud-media.oss-cn-shenzhen.aliyuncs.com/common/media/audio/20150317170731988.mp3
http://szali-ibabycloud-media.oss-cn-shenzhen.aliyuncs.com/common/media/audio/20150317171119918.mp3

*/
