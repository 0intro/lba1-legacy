#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "dat.h"
#include "fns.h"

/*
 * FEATURE: what the Classic's music asks of its sound handler, SoLoud:
 * a WavStream, a file of music opened and parsed (OpenStream), played on
 * a voice from its start (play), the voice's handle valid until the
 * voice has played its last sample, and the voice's pause, volume and
 * stream time, the seconds it has played. Here a voice's samples come
 * from the file's decoder (decode.c) as the port's clock passes them, a
 * frame of 1/75 s at a time and Ahead frames ahead, as the CD's do
 * (cd.c), to a stream of sdl.c's at the file's rate, and its time is
 * the clock's in 1024ths of a second, so that the Classic's arithmetic
 * on it gives one result on every build. A record sums each frame by
 * its place, the file's name, the frame's number and the volume, as the
 * builds decode a file a few bits apart. The port's own.
 */

enum
{
	Nvoice = 3,	/* the Classic's streams: the CD's, the tune's, the voice's */
	Maxframe = 48000/75,	/* a frame's samples at most */
};

typedef struct Voice Voice;

struct Voice
{
	uint32_t	handle;	/* 0 for none */
	Decoder	*d;
	char	name[64];	/* the file's, for the records */
	uint32_t	rate;
	uint32_t	channels;
	uint64_t	n;	/* its samples */
	uint64_t	fed;	/* frames handed on */
	uint64_t	start;	/* the clock's ns at its first sample */
	int	paused;
	uint64_t	pausedat;	/* ns played, when paused */
	float	volume;
};

static Voice voices[Nvoice];
static uint32_t handles;
static uint64_t now;
static uint64_t played;	/* frames, every voice's */

/* The voice of handle h, or NULL. */
static Voice*
voiceof(uint32_t h)
{
	int i;

	if (h == 0)
		return NULL;
	for (i = 0; i < Nvoice; i++)
		if (voices[i].handle == h)
			return &voices[i];
	return NULL;
}

/* The voice let go, what waits of it dropped. */
static void
voicefree(Voice *v)
{
	if (v->d != NULL)
		decclose(v->d);
	v->d = NULL;
	v->handle = 0;
	sdlvoicestop(v - voices);
}

/* A frame's samples at the voice's rate. */
static uint64_t
framesize(Voice *v)
{
	return v->rate / 75;
}

/* The ns the voice has played, its time. */
static uint64_t
playedns(Voice *v)
{
	return v->paused ? v->pausedat : now - v->start;
}

/*
 * soundplay: file, a host's path, played on a voice from its start at
 * volume, as SoLoud's play: its handle, or 0 if it cannot be decoded or
 * every voice plays.
 */
uint32_t
soundplay(char *file, float volume)
{
	Voice *v;
	char *p;
	int i;

	for (i = 0; i < Nvoice; i++)
		if (voices[i].handle == 0)
			break;
	if (i == Nvoice)
		return 0;
	v = &voices[i];
	v->d = decopen(file, Padded);
	if (v->d == NULL)
		return 0;
	decinfo(v->d, &v->rate, &v->channels, &v->n);
	if (v->rate % 75 != 0 || v->rate/75 > Maxframe) {
		decclose(v->d);
		v->d = NULL;
		return 0;
	}
	p = strrchr(file, '/');
	snprintf(v->name, sizeof v->name, "%s", p != NULL ? p+1 : file);
	v->fed = 0;
	v->start = now;
	v->paused = 0;
	v->volume = volume;
	if (++handles == 0)
		handles = 1;
	v->handle = handles;
	sdlvoicegain(i, volume);
	return v->handle;
}

/* soundstop: the voice of handle h stopped, as SoLoud's stop. */
void
soundstop(uint32_t h)
{
	Voice *v;

	v = voiceof(h);
	if (v != NULL)
		voicefree(v);
}

/* soundvalid: whether handle h's voice plays still, as isValidVoiceHandle. */
int
soundvalid(uint32_t h)
{
	return voiceof(h) != NULL;
}

/*
 * soundtime: the seconds handle h's voice has played, in 1024ths, as
 * getStreamTime, or 0 for none.
 */
double
soundtime(uint32_t h)
{
	Voice *v;

	v = voiceof(h);
	if (v == NULL)
		return 0;
	return (double)(playedns(v) * 1024 / 1000000000) / 1024;
}

/* soundpause: handle h's voice paused, or played on, as setPause. */
void
soundpause(uint32_t h, int pause)
{
	Voice *v;

	v = voiceof(h);
	if (v == NULL || v->paused == pause)
		return;
	if (pause) {
		v->pausedat = now - v->start;
		v->paused = 1;
		sdlvoicestop(v - voices);
		return;
	}
	v->paused = 0;
	v->start = now - v->pausedat;
	v->fed = v->pausedat * 75 / 1000000000;
	if (decseek(v->d, v->fed * framesize(v)) < 0)
		voicefree(v);
}

/* soundvolume: handle h's voice's volume, as setVolume. */
void
soundvolume(uint32_t h, float volume)
{
	Voice *v;

	v = voiceof(h);
	if (v == NULL)
		return;
	v->volume = volume;
	sdlvoicegain(v - voices, volume);
}

/*
 * soundtick: each voice's frames the clock has passed, and Ahead more,
 * decoded and handed on, each summed in a record by its place. A voice
 * whose samples the clock has passed is let go.
 */
void
soundtick(uint64_t ns)
{
	static int16_t buf[Maxframe*2];
	uint8_t place[72];
	uint64_t due, m, k;
	Voice *v;
	int i, len;

	now = ns;
	for (i = 0; i < Nvoice; i++) {
		v = &voices[i];
		if (v->handle == 0 || v->paused)
			continue;
		m = framesize(v);
		if ((now - v->start) * v->rate / 1000000000 >= v->n) {
			voicefree(v);
			continue;
		}
		due = (now - v->start) * 75 / 1000000000 + Ahead;
		for (; v->fed < due && v->fed*m < v->n; v->fed++) {
			k = v->n - v->fed*m < m ? v->n - v->fed*m : m;
			k = decread(v->d, buf, k);
			memset(buf + k*v->channels, 0, (m-k)*v->channels*sizeof buf[0]);
			sdlvoice(i, buf, (int)(m*v->channels*sizeof buf[0]), v->rate, v->channels);
			len = snprintf((char*)place, sizeof place, "%s %llu %ld", v->name,
				(unsigned long long)v->fed, (long)(v->volume*65536 + 0.5f));
			summedstream(++played, place, len);
		}
	}
}
