#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dat.h"
#include "fns.h"
#include "drlibs/dr_mp3.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.c"

/*
 * An MP3 or Ogg Vorbis file decoded into 16-bit samples, interleaved: a
 * file that starts "OggS" by stb_vorbis, any other by dr_mp3, which
 * leaves out an MP3's encoder delay and padding where its LAME tag gives
 * them, so that its samples are those the encoder was given. A file
 * gives its length in frames, which decread holds to, the frames a
 * decoder fails to give read as silence.
 */

enum
{
	Mp3,
	Ogg,
	Most = 0x10000,	/* the frames a decoder is asked for at once, whose samples an int counts */
};

struct Decoder
{
	int	kind;
	drmp3	mp3;
	stb_vorbis	*ogg;
	uint32_t	rate;
	uint32_t	channels;
	uint64_t	frames;	/* the length */
	uint64_t	pos;	/* the next frame */
};

/* decopen: the decoder of the host's file path, or NULL. */
Decoder*
decopen(const char *path)
{
	Decoder *d;
	stb_vorbis_info info;
	unsigned char magic[4];
	FILE *f;
	size_t n;
	int err;

	f = fopen(path, "rb");
	if (f == NULL)
		return NULL;
	n = fread(magic, 1, sizeof magic, f);
	fclose(f);
	d = calloc(1, sizeof *d);
	if (d == NULL)
		return NULL;
	if (n == sizeof magic && memcmp(magic, "OggS", 4) == 0) {
		d->kind = Ogg;
		d->ogg = stb_vorbis_open_filename(path, &err, NULL);
		if (d->ogg == NULL) {
			free(d);
			return NULL;
		}
		info = stb_vorbis_get_info(d->ogg);
		d->rate = info.sample_rate;
		d->channels = (uint32_t)info.channels;
		d->frames = stb_vorbis_stream_length_in_samples(d->ogg);
	} else {
		d->kind = Mp3;
		if (!drmp3_init_file(&d->mp3, path, NULL)) {
			free(d);
			return NULL;
		}
		d->rate = d->mp3.sampleRate;
		d->channels = d->mp3.channels;
		d->frames = drmp3_get_pcm_frame_count(&d->mp3);
	}
	if (d->rate == 0 || d->channels == 0 || d->channels > 2 || d->frames == 0) {
		decclose(d);
		return NULL;
	}
	return d;
}

/* decinfo: the file's rate in Hz, its channels and its length in frames. */
void
decinfo(Decoder *d, uint32_t *rate, uint32_t *channels, uint64_t *frames)
{
	*rate = d->rate;
	*channels = d->channels;
	*frames = d->frames;
}

/*
 * decread: up to n frames from the next into buf, their samples
 * interleaved. The frames read, fewer only at the file's end.
 */
uint64_t
decread(Decoder *d, int16_t *buf, uint64_t n)
{
	uint64_t got, k;

	if (n > d->frames - d->pos)
		n = d->frames - d->pos;
	for (got = 0; got < n; got += k) {
		k = n - got;
		if (k > Most)
			k = Most;
		if (d->kind == Ogg)
			k = (uint64_t)stb_vorbis_get_samples_short_interleaved(d->ogg, (int)d->channels,
				buf + got*d->channels, (int)(k*d->channels));
		else
			k = drmp3_read_pcm_frames_s16(&d->mp3, k, buf + got*d->channels);
		/* what the decoder could not give */
		if (k == 0) {
			k = n - got;
			memset(buf + got*d->channels, 0, k*d->channels*sizeof buf[0]);
		}
		d->pos += k;
	}
	return n;
}

/* decseek: the next frame made frame: 0, or -1. The frame at the end sets pos alone, the decoder kept where it stands. */
int
decseek(Decoder *d, uint64_t frame)
{
	if (frame > d->frames)
		return -1;
	if (frame < d->frames && d->kind == Ogg && !stb_vorbis_seek(d->ogg, (unsigned int)frame))
		return -1;
	if (frame < d->frames && d->kind == Mp3 && !drmp3_seek_to_pcm_frame(&d->mp3, frame))
		return -1;
	d->pos = frame;
	return 0;
}

/* decclose: the decoder let go. */
void
decclose(Decoder *d)
{
	if (d->kind == Ogg)
		stb_vorbis_close(d->ogg);
	else
		drmp3_uninit(&d->mp3);
	free(d);
}
