/* the Music Player Daemon (MPD)
 * (c)2003-2004 by Warren Dukes (shank@mercury.chem.pitt.edu)
 * This project's homepage is: http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "mp3_decode.h"

#ifdef HAVE_MAD

#include "pcm_utils.h"
#ifdef USE_MPD_MAD
#include "libmad/mad.h"
#else
#include <mad.h>
#endif
#ifdef HAVE_ID3TAG
#ifdef USE_MPD_ID3TAG
#include "libid3tag/id3tag.h"
#else
#include <id3tag.h>
#endif
#endif
#include "playerData.h"
#include "log.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#define FRAMES_CUSHION		2000

#define READ_BUFFER_SIZE	40960

#define DECODE_SKIP		-3
#define DECODE_BREAK		-2
#define DECODE_CONT		-1
#define DECODE_OK		0

/* this is stolen from mpg321! */
struct audio_dither {
	mad_fixed_t error[3];
	mad_fixed_t random;
};

unsigned long prng(unsigned long state) {
	return (state * 0x0019660dL + 0x3c6ef35fL) & 0xffffffffL;
}

signed long audio_linear_dither(unsigned int bits, mad_fixed_t sample, struct audio_dither *dither) {
	unsigned int scalebits;
	mad_fixed_t output, mask, random;

	enum {
		MIN = -MAD_F_ONE,
		MAX =  MAD_F_ONE - 1
	};

	sample += dither->error[0] - dither->error[1] + dither->error[2];

	dither->error[2] = dither->error[1];
	dither->error[1] = dither->error[0] / 2;

	output = sample + (1L << (MAD_F_FRACBITS + 1 - bits - 1));

	scalebits = MAD_F_FRACBITS + 1 - bits;
	mask = (1L << scalebits) - 1;

	random  = prng(dither->random);
	output += (random & mask) - (dither->random & mask);

	dither->random = random;

	if (output > MAX) {
		output = MAX;

		if (sample > MAX)
			sample = MAX;
	}
	else if (output < MIN) {
	        output = MIN;

		if (sample < MIN)
			sample = MIN;
	}

	output &= ~mask;

	dither->error[0] = sample - output;

	return output >> scalebits;
}
/* end of stolen stuff from mpg321 */

/* decoder stuff is based on madlld */

typedef struct _mp3DecodeData {
	FILE * fp;
	struct mad_stream stream;
	struct mad_frame frame;
	struct mad_synth synth;
	mad_timer_t timer;
	unsigned char readBuffer[READ_BUFFER_SIZE];
	char outputBuffer[CHUNK_SIZE];
	char * outputPtr;
	char * outputBufferEnd;
	float totalTime;
	float elapsedTime;
	int muteFrame;
	long * frameOffset;
	mad_timer_t * times;
	long highestFrame;
	long currentOffset;
	long maxFrames;
	long currentFrame;
	int flush;
	unsigned long bitRate;
} mp3DecodeData;

void initMp3DecodeData(mp3DecodeData * data) {
	data->outputPtr = data->outputBuffer;
	data->outputBufferEnd = data->outputBuffer+CHUNK_SIZE;
	data->muteFrame = 0;
	data->currentOffset = 0;
	data->highestFrame = 0;
	data->maxFrames = 0;
	data->frameOffset = NULL;
	data->times = NULL;
	data->currentFrame = 0;
	data->flush = 1;
	mad_stream_init(&data->stream);
	mad_frame_init(&data->frame);
	mad_synth_init(&data->synth);
	mad_timer_reset(&data->timer);
}

int fillMp3InputBuffer(mp3DecodeData * data, long offset) {
	size_t readSize;
	size_t remaining;
	unsigned char * readStart;

	if(offset>=0) {
		if(fseek(data->fp,offset,SEEK_SET)==0) {
			data->currentOffset = offset;
		}
	}

	if(offset==-1 && (data->stream).next_frame!=NULL) {
		remaining = (data->stream).bufend-(data->stream).next_frame;
		memmove(data->readBuffer,(data->stream).next_frame,remaining);
		readStart = (data->readBuffer)+remaining;
		readSize = READ_BUFFER_SIZE-remaining;
	}
	else {
		readSize = READ_BUFFER_SIZE;
		readStart = data->readBuffer,
		remaining = 0;
	}
			
	readSize = fread(readStart,1,readSize,data->fp);
	if(readSize<=0) return -1;

	data->currentOffset+=readSize;

	mad_stream_buffer(&data->stream,data->readBuffer,readSize+remaining);
	(data->stream).error = 0;

	return 0;
}

int decodeNextFrameHeader(mp3DecodeData * data) {
	if((data->stream).buffer==NULL || (data->stream).error==MAD_ERROR_BUFLEN) {
		if(fillMp3InputBuffer(data,/*data->currentOffset*/-1) < 0) {
			return DECODE_BREAK;
		}
	}
	if(mad_header_decode(&data->frame.header,&data->stream)) {
#ifdef HAVE_ID3TAG
		if((data->stream).error==MAD_ERROR_LOSTSYNC && 
				(data->stream).this_frame) 
		{
			signed long tagsize = id3_tag_query(
					(data->stream).this_frame,
					(data->stream).bufend-
					(data->stream).this_frame);
			if(tagsize>0) {
				mad_stream_skip(&(data->stream),tagsize);
				return DECODE_CONT;
			}
		}
#endif
		if(MAD_RECOVERABLE((data->stream).error)) return DECODE_SKIP;
		else {
			if((data->stream).error==MAD_ERROR_BUFLEN) return DECODE_CONT;
			else
			{
				ERROR("unrecoverable frame level error "
					"(%s).\n",
					mad_stream_errorstr(&data->stream));
				data->flush = 0;
				return DECODE_BREAK;
			}
		}
	}

	return DECODE_OK;
}

int decodeNextFrame(mp3DecodeData * data) {
	if((data->stream).buffer==NULL || (data->stream).error==MAD_ERROR_BUFLEN) {
		if(fillMp3InputBuffer(data,/*data->currentOffset*/-1) < 0) {
			return DECODE_BREAK;
		}
	}
#ifdef HAVE_ID3TAG
	if(mad_frame_decode(&data->frame,&data->stream)) {
		if((data->stream).error==MAD_ERROR_LOSTSYNC) {
			signed long tagsize = id3_tag_query(
					(data->stream).this_frame,
					(data->stream).bufend-
					(data->stream).this_frame);
			if(tagsize>0) {
				mad_stream_skip(&(data->stream),tagsize);
				return DECODE_CONT;
			}
		}
#endif
		if(MAD_RECOVERABLE((data->stream).error)) return DECODE_SKIP;
		else {
			if((data->stream).error==MAD_ERROR_BUFLEN) return DECODE_CONT;
			else
			{
				ERROR("unrecoverable frame level error "
					"(%s).\n",
					mad_stream_errorstr(&data->stream));
				data->flush = 0;
				return DECODE_BREAK;
			}
		}
	}

	return DECODE_OK;
}

/* xing stuff stolen from alsaplayer */
# define XING_MAGIC	(('X' << 24) | ('i' << 16) | ('n' << 8) | 'g')

struct xing {
  	long flags;			/* valid fields (see below) */
  	unsigned long frames;		/* total number of frames */
  	unsigned long bytes;		/* total number of bytes */
  	unsigned char toc[100];	/* 100-point seek table */
  	long scale;			/* ?? */
};

enum {
  	XING_FRAMES = 0x00000001L,
  	XING_BYTES  = 0x00000002L,
  	XING_TOC    = 0x00000004L,
  	XING_SCALE  = 0x00000008L
};

int parse_xing(struct xing *xing, struct mad_bitptr ptr, unsigned int bitlen)
{
  	if (bitlen < 64 || mad_bit_read(&ptr, 32) != XING_MAGIC) goto fail;

  	xing->flags = mad_bit_read(&ptr, 32);
  	bitlen -= 64;

  	if (xing->flags & XING_FRAMES) {
    		if (bitlen < 32) goto fail;
    		xing->frames = mad_bit_read(&ptr, 32);
    		bitlen -= 32;
  	}

  	if (xing->flags & XING_BYTES) {
    		if (bitlen < 32) goto fail;
    		xing->bytes = mad_bit_read(&ptr, 32);
    		bitlen -= 32;
  	}

  	if (xing->flags & XING_TOC) {
    		int i;
    		if (bitlen < 800) goto fail;
      		for (i = 0; i < 100; ++i) xing->toc[i] = mad_bit_read(&ptr, 8);
    		bitlen -= 800;
  	}

  	if (xing->flags & XING_SCALE) {
    		if (bitlen < 32) goto fail;
    		xing->scale = mad_bit_read(&ptr, 32);
    		bitlen -= 32;
  	}

 	 return 1;

fail:
  	xing->flags = 0;
  	return 0;
}

int decodeFirstFrame(mp3DecodeData * data) {
	struct stat filestat;
	struct xing xing;
	int ret;
	int skip;

	memset(&xing,0,sizeof(struct xing));
	xing.flags = 0;

	while(1) {
		skip = 0;
		while((ret = decodeNextFrameHeader(data))==DECODE_CONT);
		if(ret==DECODE_SKIP) skip = 1;
		else if(ret==DECODE_BREAK) return -1;
		while((ret = decodeNextFrame(data))==DECODE_CONT);
		if(ret==DECODE_BREAK) return -1;
		if(!skip && ret==DECODE_OK) break;
	}

	if(parse_xing(&xing,data->stream.anc_ptr,data->stream.anc_bitlen)) {
		if(xing.flags & XING_FRAMES) {
			mad_timer_t duration = data->frame.header.duration;
			mad_timer_multiply(&duration,xing.frames);
			data->muteFrame = 1;
			data->totalTime = ((float)mad_timer_count(duration,
						MAD_UNITS_MILLISECONDS))/1000;
			data->maxFrames = xing.frames;
		}
	}
	else {
		size_t offset = data->currentOffset;
		mad_timer_t duration = data->frame.header.duration;
		float frameTime = ((float)mad_timer_count(duration,
					MAD_UNITS_MILLISECONDS))/1000;
		fstat(fileno(data->fp),&filestat);
		if(data->stream.this_frame!=NULL) {
			offset-= data->stream.bufend-data->stream.this_frame;
		}
		else {
			offset-= data->stream.bufend-data->stream.buffer;
		}
		data->totalTime = ((filestat.st_size-offset)*8.0)/
					(data->frame).header.bitrate;
		data->maxFrames = data->totalTime/frameTime+FRAMES_CUSHION;
	}

	data->frameOffset = malloc(sizeof(long)*data->maxFrames);
	data->times = malloc(sizeof(mad_timer_t)*data->maxFrames);

	return 0;
}

void mp3DecodeDataFinalize(mp3DecodeData * data) {
	mad_synth_finish(&data->synth);
	mad_frame_finish(&data->frame);
	mad_stream_finish(&data->stream);

	if(data->fp) fclose(data->fp);
	if(data->frameOffset) free(data->frameOffset);
	if(data->times) free(data->times);
}

/* this is primarily used for getting total time for tags */
int getMp3TotalTime(char * file) {
	mp3DecodeData data;
	int ret;

	while(!(data.fp = fopen(file,"r")) && errno==EINTR);
	if(!data.fp) return -1;

	initMp3DecodeData(&data);
	if(decodeFirstFrame(&data)<0) ret = -1;
	else ret = data.totalTime+0.5;
	mp3DecodeDataFinalize(&data);

	return ret;
}

int openMp3(char * file, mp3DecodeData * data) {
	if((data->fp = fopen(file,"r"))<=0) {
		ERROR("problems opening \"%s\"\n",file);
		return -1;
	}
	initMp3DecodeData(data);
	if(decodeFirstFrame(data)<0) {
		mp3DecodeDataFinalize(data);
		return -1;
	}

	return 0;
}

int mp3ChildSendData(mp3DecodeData * data, Buffer * cb, DecoderControl * dc) {
	while(cb->begin==cb->end && cb->wrap && !dc->stop && !dc->seek) 
		my_usleep(10000);
	if(dc->stop) return -1;
	/* just for now, so it doesn't hang */
	if(dc->seek) return 0;
	/* be sure to remove this! */

	memcpy(cb->chunks+cb->end*CHUNK_SIZE,data->outputBuffer,CHUNK_SIZE);
	cb->chunkSize[cb->end] = data->outputPtr-data->outputBuffer;
	cb->bitRate[cb->end] = data->bitRate/1000;
	cb->times[cb->end] = data->elapsedTime;

	cb->end++;
	if(cb->end>=buffered_chunks) {
		cb->end = 0;
		cb->wrap = 1;
	}

	return 0;
}

int mp3Read(mp3DecodeData * data, Buffer * cb, DecoderControl * dc) {
	static int i;
	static int ret;
	static struct audio_dither dither;
	static int skip;

	if(data->currentFrame>=data->highestFrame && 
			data->highestFrame<data->maxFrames) 
	{
		mad_timer_add(&data->timer,(data->frame).header.duration);
		data->bitRate = (data->frame).header.bitrate;
		data->frameOffset[data->currentFrame] = 
				data->currentOffset;
		if(data->stream.this_frame!=NULL) {
			data->frameOffset[data->currentFrame]-= 
					data->stream.bufend-
					data->stream.this_frame;
		}
		else {
			data->frameOffset[data->currentFrame]-= 
					data->stream.bufend-data->stream.buffer;
		}
		data->times[data->currentFrame] = data->timer;
		data->highestFrame++;
	}
	else data->timer = data->times[data->currentFrame];
	data->currentFrame++;
	data->elapsedTime = ((float)mad_timer_count(data->timer,MAD_UNITS_MILLISECONDS))/1000;

	if(data->muteFrame) {
		if(!dc->seek) data->muteFrame = 0;
		else if(dc->seekWhere<=data->elapsedTime) {
			data->muteFrame = 0;
			dc->seek = 0;
		}
	}
	else {
		mad_synth_frame(&data->synth,&data->frame);

		for(i=0;i<(data->synth).pcm.length;i++) {
			mpd_sint16 * sample;

			sample = (mpd_sint16 *)data->outputPtr;	
			*sample = (mpd_sint16) audio_linear_dither(16,
					(data->synth).pcm.samples[0][i],
					&dither);
			data->outputPtr+=2;

			if(MAD_NCHANNELS(&(data->frame).header)==2) {
				sample = (mpd_sint16 *)data->outputPtr;	
				*sample = (mpd_sint16) audio_linear_dither(16,
						(data->synth).pcm.samples[1][i],
						&dither);
				data->outputPtr+=2;
			}

			if(data->outputPtr==data->outputBufferEnd) {
				if(mp3ChildSendData(data,cb,dc)<0) {
					data->flush = 0;
					return DECODE_BREAK;
				}
				data->outputPtr = data->outputBuffer;
				if(dc->seek) break;
			}
		}

		if(dc->seek) {
			long i = 0;
			cb->wrap = 0;
			cb->end = cb->begin;
			data->muteFrame = 1;
			while(i<data->highestFrame && dc->seekWhere >
					((float)mad_timer_count(data->times[i],
					MAD_UNITS_MILLISECONDS))/1000) 
			{
				i++;
			}
			if(i<data->highestFrame) {
				data->currentFrame = i;
				fillMp3InputBuffer(data,data->frameOffset[i]);
				data->muteFrame = 0;
				dc->seek = 0;
			}
		}
	}

	while(1) {
		skip = 0;
		while((ret = decodeNextFrameHeader(data))==DECODE_CONT);
		if(ret==DECODE_SKIP) skip = 1;
		else if(ret==DECODE_BREAK) break;
		if(!data->muteFrame) {
			while((ret = decodeNextFrame(data))==DECODE_CONT);
			if(ret==DECODE_BREAK) break;
		}
		if(!skip && ret==DECODE_OK) break;
	}

	return ret;
}

void initAudioFormatFromMp3DecodeData(mp3DecodeData * data, AudioFormat * af) {
	af->bits = 16;
	af->sampleRate = (data->frame).header.samplerate;
	af->channels = MAD_NCHANNELS(&(data->frame).header);
}

int mp3_decode(Buffer * cb, AudioFormat * af, DecoderControl * dc) {
	mp3DecodeData data;

	if(openMp3(dc->file,&data) < 0) {
		ERROR("Input does not appear to be a mp3 bit stream.\n");
		return -1;
	}

	initAudioFormatFromMp3DecodeData(&data,af);
	cb->totalTime = data.totalTime;
	dc->start = 0;
	dc->state = DECODE_STATE_DECODE;

	while(mp3Read(&data,cb,dc)!=DECODE_BREAK);
	/* send last little bit if not dc->stop */
	if(data.outputPtr!=data.outputBuffer && data.flush)  {
		mp3ChildSendData(&data,cb,dc);
	}

	mp3DecodeDataFinalize(&data);

	if(dc->seek) dc->seek = 0;

	if(dc->stop) {
		dc->state = DECODE_STATE_STOP;
		dc->stop = 0;
	}
	else dc->state = DECODE_STATE_STOP;
		
	return 0;
}

#endif
