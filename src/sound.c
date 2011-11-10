/*******************************************************************************#
#           guvcview              http://guvcview.sourceforge.net               #
#                                                                               #
#           Paulo Assis <pj.assis@gmail.com>                                    #
#                                                                               #
# This program is free software; you can redistribute it and/or modify          #
# it under the terms of the GNU General Public License as published by          #
# the Free Software Foundation; either version 2 of the License, or             #
# (at your option) any later version.                                           #
#                                                                               #
# This program is distributed in the hope that it will be useful,               #
# but WITHOUT ANY WARRANTY; without even the implied warranty of                #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                 #
# GNU General Public License for more details.                                  #
#                                                                               #
# You should have received a copy of the GNU General Public License             #
# along with this program; if not, write to the Free Software                   #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA     #
#                                                                               #
********************************************************************************/

#include <glib/gprintf.h>
#include <string.h>
#include <math.h>
#include "vcodecs.h"
#include "avilib.h"
#include "acodecs.h"
#include "lavc_common.h"
#include "audio_effects.h"
#include "ms_time.h"

#if GLIB_MINOR_VERSION < 31
	#define __AMUTEX pdata->mutex
#else
	#define __AMUTEX &pdata->mutex
#endif

static int fill_audio_buffer(struct paRecordData *pdata, UINT64 ts)
{
	int ret =0;
	UINT64 buffer_length;

	if(pdata->sampleIndex >= pdata->aud_numSamples)
	{
		buffer_length = (G_NSEC_PER_SEC * pdata->aud_numSamples)/(pdata->samprate * pdata->channels);

		/*first frame time stamp*/
		if(pdata->a_ts <= 0)
		{
			/*if sound begin time > first video frame ts then sync audio to video
			* else set audio ts to aprox. the video ts */
			if((pdata->ts_ref > 0) && (pdata->ts_ref < pdata->snd_begintime)) 
				pdata->a_ts = pdata->snd_begintime - pdata->ts_ref;
			else pdata->a_ts = 1; /*make it > 0 otherwise we will keep getting the same ts*/
		}
		else /*increment time stamp for audio frame*/
			pdata->a_ts += buffer_length;

		/* check audio drift through timestamps */
		if (ts > pdata->snd_begintime)
			ts -= pdata->snd_begintime;
		else
			ts = 0;
		if (ts > buffer_length)
			ts -= buffer_length;
		else
			ts = 0;
		pdata->ts_drift = ts - pdata->a_ts;
		
		pdata->sampleIndex = 0; /*reset*/
		if(!pdata->audio_buff[pdata->w_ind].used)
		{
			/*copy data to audio buffer*/
			memcpy(pdata->audio_buff[pdata->w_ind].frame, pdata->recordedSamples, pdata->aud_numBytes);
			pdata->audio_buff[pdata->w_ind].time_stamp = pdata->a_ts + pdata->delay;
			pdata->audio_buff[pdata->w_ind].used = TRUE;
			NEXT_IND(pdata->w_ind, AUDBUFF_SIZE);
		}
		else
		{
			/*drop audio data*/
			ret = -1;
			g_printerr("AUDIO: droping audio data\n");
		}
	}
	
	return ret;
}

/*--------------------------- sound callback ------------------------------*/
int 
recordCallback (const void *inputBuffer, void *outputBuffer,
	unsigned long framesPerBuffer,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags,
	void *userData )
{
	struct paRecordData *pdata = (struct paRecordData*)userData;

	const SAMPLE *rptr = (const SAMPLE*)inputBuffer;
	int i;
	UINT64 ts, nsec_per_frame;

	/* buffer ends at timestamp "now", calculate beginning timestamp */
	nsec_per_frame = G_NSEC_PER_SEC / pdata->samprate;
	ts = ns_time_monotonic() - (UINT64) framesPerBuffer * nsec_per_frame;

	g_mutex_lock( __AMUTEX );
		gboolean capVid = pdata->capVid;
		int channels = pdata->channels;
		int skip_n = pdata->skip_n;
	g_mutex_unlock( __AMUTEX );
	
	if (skip_n > 0) /*skip audio while were skipping video frames*/
	{
		
		if(capVid) 
		{
			g_mutex_lock( __AMUTEX );
				pdata->snd_begintime = ns_time_monotonic(); /*reset first time stamp*/
			g_mutex_unlock( __AMUTEX );
			return (paContinue); /*still capturing*/
		}
		else
		{	g_mutex_lock( __AMUTEX );
				pdata->streaming=FALSE;
			g_mutex_lock( __AMUTEX );
			return (paComplete);
		}
	}
	
	int numSamples= framesPerBuffer * channels;

	g_mutex_lock( __AMUTEX );
		/*set to FALSE on paComplete*/
		pdata->streaming=TRUE;

		for( i=0; i<numSamples; i++ )
		{
			pdata->recordedSamples[pdata->sampleIndex] = inputBuffer ? *rptr++ : 0;
			pdata->sampleIndex++;
		
			fill_audio_buffer(pdata, ts);

			/* increment timestamp accordingly while copying */
			if (i % channels == 0)
				ts += nsec_per_frame;
		}
		
	g_mutex_unlock( __AMUTEX );

	if(capVid) return (paContinue); /*still capturing*/
	else 
	{
		g_mutex_lock( __AMUTEX );
			pdata->streaming=FALSE;
		g_mutex_unlock( __AMUTEX );
		return (paComplete);
	}
	
}

void
set_sound (struct GLOBAL *global, struct paRecordData* pdata, void *lav_aud_data) 
{
	struct lavcAData **lavc_data = (struct lavcAData **) lav_aud_data;
	
	if(global->Sound_SampRateInd==0)
		global->Sound_SampRate=global->Sound_IndexDev[global->Sound_UseDev].samprate;/*using default*/
	
	if(global->Sound_NumChanInd==0) 
	{
		/*using default if channels <3 or stereo(2) otherwise*/
		global->Sound_NumChan=(global->Sound_IndexDev[global->Sound_UseDev].chan < 3) ? 
			global->Sound_IndexDev[global->Sound_UseDev].chan : 2;
	}
	
	pdata->audio_buff = NULL;
	pdata->recordedSamples = NULL;
	
	pdata->samprate = global->Sound_SampRate;
	pdata->channels = global->Sound_NumChan;
	g_mutex_lock( __AMUTEX );
		pdata->skip_n = global->skip_n; /*initial video frames to skip*/
	g_mutex_unlock( __AMUTEX );
	if(global->debug) g_print("using audio codec: 0x%04x\n",global->Sound_Format );
	switch (global->Sound_Format)
	{
		case PA_FOURCC:
		{
			pdata->aud_numSamples = MPG_NUM_SAMP * pdata->channels;
			break;
		}
		default:
		{
			/*initialize lavc data*/
			if(!(*lavc_data)) 
			{
				*lavc_data = init_lavc_audio(pdata, get_ind_by4cc(global->Sound_Format));
			}
			/*use lavc audio codec frame size to determine samples*/
			pdata->aud_numSamples = (*lavc_data)->codec_context->frame_size * pdata->channels;
			break;
		}
	}
	
	pdata->aud_numBytes = pdata->aud_numSamples * sizeof(SAMPLE);
	pdata->input_type = PA_SAMPLE_TYPE;
	pdata->mp2Buff = NULL;
	
	pdata->sampleIndex = 0;
	
	pdata->flush = 0;
	pdata->a_ts= 0;
	pdata->ts_ref = 0;
	
	pdata->stream = NULL;
	/* some drivers, e.g. GSPCA, don't set fps( guvcview sets it to 1/1 ) 
	 * so we can't obtain the proper delay for H.264 (2 video frames)
	 * if set, use the codec properties fps value */
	int fps_num = 1;
	int fps_den = get_enc_fps(global->VidCodec); /*if set use encoder fps */
	if(!fps_den) /*if not set use video combobox fps*/
	{
		fps_num = global->fps_num;
		fps_den = global->fps;
	}
	if((get_vcodec_id(global->VidCodec) == CODEC_ID_H264) && (fps_den >= 5)) 
		pdata->delay = (UINT64) 2*(fps_num * G_NSEC_PER_SEC / fps_den); /*2 frame delay in nanosec*/
	pdata->delay += global->Sound_delay; /*add predefined delay - def = 0*/
	
	/*reset the indexes*/	
	pdata->r_ind = 0;
	pdata->w_ind = 0;
	/*buffer for video PCM 16 bits*/
	pdata->pcm_sndBuff=NULL;
	/*set audio device to use*/
	pdata->inputParameters.device = global->Sound_IndexDev[global->Sound_UseDev].id; /* input device */
}

int
init_sound(struct paRecordData* pdata)
{
	PaError err = paNoError;
	int i=0;
	
	/*alloc audio ring buffer*/
    	if(!(pdata->audio_buff))
    	{
		pdata->audio_buff = g_new0(AudBuff, AUDBUFF_SIZE);
		for(i=0; i<AUDBUFF_SIZE; i++)
			pdata->audio_buff[i].frame = g_new0(SAMPLE, pdata->aud_numSamples);
	}
	
	/*alloc the callback buffer*/
	pdata->recordedSamples = g_new0(SAMPLE, pdata->aud_numSamples);
	
	switch(pdata->api)
	{
#ifdef PULSEAUDIO
		case PULSE:
			if(err = pulse_init_audio(pdata))
				goto error;
			break;
#endif
		case PORT:
		default:
			if(pdata->stream)
			{
				if( !(Pa_IsStreamStopped( pdata->stream )))
				{
					Pa_AbortStream( pdata->stream );
					Pa_CloseStream( pdata->stream );
					pdata->stream = NULL;
				}
			}
				
			pdata->inputParameters.channelCount = pdata->channels;
			pdata->inputParameters.sampleFormat = PA_SAMPLE_TYPE;
			if (Pa_GetDeviceInfo( pdata->inputParameters.device ))
				pdata->inputParameters.suggestedLatency = Pa_GetDeviceInfo( pdata->inputParameters.device )->defaultHighInputLatency;
			else
				pdata->inputParameters.suggestedLatency = DEFAULT_LATENCY_DURATION/1000.0;
			pdata->inputParameters.hostApiSpecificStreamInfo = NULL; 
	
			/*---------------------------- start recording Audio. ----------------------------- */
	
			err = Pa_OpenStream(
				&pdata->stream,
				&pdata->inputParameters,
				NULL,                  /* &outputParameters, */
				pdata->samprate,        /* sample rate        */
				MPG_NUM_SAMP,          /* buffer in frames => Mpeg frame size (samples = 1152 samples * channels)*/
				paNoFlag,              /* PaNoFlag - clip and dhiter*/
				recordCallback,        /* sound callback     */
				pdata );                /* callback userData  */
	
			if( err != paNoError ) goto error;
	
			err = Pa_StartStream( pdata->stream );
			if( err != paNoError ) goto error; /*should close the stream if error ?*/
			break;
	}
	
	/*sound start time - used to sync with video*/
	pdata->snd_begintime = ns_time_monotonic();

	return (0);
error:
	g_printerr("An error occured while starting the audio API\n" );
	g_printerr("Error number: %d\n", err );
	if(pdata->api == PORT) g_printerr("Error message: %s\n", Pa_GetErrorText( err ) ); 
	pdata->streaming=FALSE;
	pdata->flush=0;
	pdata->delay=0;
	if(pdata->api == PORT)
	{
		if(pdata->stream) Pa_AbortStream( pdata->stream );
	}
	if(pdata->recordedSamples) g_free( pdata->recordedSamples );
	pdata->recordedSamples=NULL;
	if(pdata->audio_buff)
	{
		for(i=0; i<AUDBUFF_SIZE; i++)
			g_free(pdata->audio_buff[i].frame);
		g_free(pdata->audio_buff);
	}
	pdata->audio_buff = NULL;
	/*lavc is allways checked and cleaned when finishing worker thread*/
	return(-1);
} 

int
close_sound (struct paRecordData *pdata) 
{
	int err = 0;
	int ret = 0;
	int i   = 0;
    
	pdata->capVid = 0;
	
	/*stops and closes the audio stream*/
	if(pdata->stream)
	{
		if(Pa_IsStreamActive( pdata->stream ) > 0)
		{
			g_print("Aborting audio stream\n");
			err = Pa_AbortStream( pdata->stream );
		}
		else
		{
			g_print("Stoping audio stream\n");
			err = Pa_StopStream( pdata->stream );
		}
		if( err != paNoError )
		{
			g_printerr("An error occured while stoping the audio stream\n" );
			g_printerr("Error number: %d\n", err );
			g_printerr("Error message: %s\n", Pa_GetErrorText( err ) );
			ret = -1;
		}
	}
	
	if(pdata->api == PORT)
	{
		g_print("Closing audio stream...\n");
		err = Pa_CloseStream( pdata->stream );
		if( err != paNoError )
		{
			g_printerr("An error occured while closing the audio stream\n" );
			g_printerr("Error number: %d\n", err );
			g_printerr("Error message: %s\n", Pa_GetErrorText( err ) );
			ret = -1;
		}
	}
	pdata->stream = NULL;
	pdata->flush = 0;
	pdata->delay = 0; /*reset the audio delay*/
	
	/* ---------------------------------------------------------------------
	 * make sure no operations are performed on the buffers  */
	g_mutex_lock(__AMUTEX);
		/*free primary buffer*/
		g_free( pdata->recordedSamples );
		pdata->recordedSamples=NULL;
		if(pdata->audio_buff)
		{
			for(i=0; i<AUDBUFF_SIZE; i++)
				g_free(pdata->audio_buff[i].frame);
			g_free(pdata->audio_buff);
		}
		pdata->audio_buff = NULL;
		if(pdata->pcm_sndBuff) g_free(pdata->pcm_sndBuff);
		pdata->pcm_sndBuff = NULL;
	g_mutex_unlock(__AMUTEX);
	
	return (ret);
}

/* saturate float samples to int16 limits*/
static gint16 clip_int16 (float in)
{
	in = (in < -32768) ? -32768 : (in > 32767) ? 32767 : in;
	
	return ((gint16) in);
}

void Float2Int16 (struct paRecordData* pdata, AudBuff *proc_buff)
{
	if (!(pdata->pcm_sndBuff)) 
		pdata->pcm_sndBuff = g_new0(gint16, pdata->aud_numSamples);
		
	int samp = 0;
	
	for(samp=0; samp < pdata->aud_numSamples; samp++)
	{
		pdata->pcm_sndBuff[samp] = clip_int16(proc_buff->frame[samp] * 32767.0); //* 32768 + 385;
	}
}

