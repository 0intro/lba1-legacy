#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dat.h"
#include "tlba1c.h"

/*
 * The midi module's ClearMidi is this file's, declared before fns.h
 * declares the port's, midi.c's, of the same type, so that the later
 * declaration takes this one's linkage (C99 6.2.2).
 */
static void	ClearMidi(void);

#include "fns.h"

/*
 * FEATURE: the Classic's music, its layer over SoLoud (TLBA1C.exe
 * 0x403650 to 0x403df0, the map's cdrom and music modules and
 * IsMidiPlaying, the runtime's vector constructor iterator at 0x403940
 * aside), its midi module (0x409cb0 to 0x409d28) and the tunes' loop of
 * its ambiance module (DoLoopMidi), decompiled. Three streams play
 * files of music on its sound handler, SoLoud (here stream.c): the CD's
 * track, the tune and the voice, each named by a code, a track's number,
 * a tune's or 0x10000, or a voice's or 0x20000 (MusicFileName), its
 * length in ms counted from the file once and kept (StreamLength), and
 * the 1994 game's calls on the XMIDI are the midi module's over the
 * tune's stream. The functions whose names the port's or the 1994
 * game's take are static here and reached through tlba1c. Taken from
 * the Classic 3.2.3.
 */

enum
{
	Midi = 0x10000,	/* a tune's code */
	Voice = 0x20000,	/* a voice's */
	Namelen = 0x104,
};

/* SoLoud's WavStream as OpenStream fills it: the file, its rate and length. */
struct Wavstream
{
	char	path[1024];	/* the host's */
	float	rate;	/* mBaseSamplerate */
	uint32_t	samples;	/* mSampleCount */
};

Stream	StreamCD;	/* 0x8dc0a4 */
Stream	StreamMidi;	/* 0x8dc0b0 */
Stream	StreamVoice;	/* 0x8dc0bc */
int32_t	StreamLengthCD[11];	/* 0x8dc46c: tracks 2 to 10's, in ms */
int32_t	StreamLengthMidi[33];	/* 0x8dc498: tunes 0 to 32's */
float	MusicStreamVolume = 1;	/* 0x819b84 */
float	MixVoice = 1;	/* 0x819584: the voices' volume */

static int ready;	/* the sound handler's, as InitSoundHandler made it */
static uint8_t told[Ntune+11];	/* the tunes' and the tracks' missing files, told of */

/* GetVoiceVolume is 0x409f10, of the Classic's sound module: the voices' volume. */
static float
GetVoiceVolume(void)
{
	return MixVoice;
}

/* InitCDR is 0x403650: the music started, the CD found. */
static int32_t
InitCDR(char *name)
{
	USED(name);
	InitMusic();
	return 1;
}

/* MusicFileName is 0x403660: the file of code's music, into name. */
void
MusicFileName(uint32_t code, char *name)
{
	if (code & Midi) {
		snprintf(name, Namelen, "Common\\Midi\\LBA1-%02d.mp3", (int)(code & 0xfffeffff) + 1);
		return;
	}
	if (code & Voice) {
		VoiceOggName(name, code & 0xfffdffff);
		return;
	}
	snprintf(name, Namelen, "Common\\Music\\Track_%02d.mp3", (int)code);
}

/* InitStream is 0x4036c0: stream s made empty. */
void
InitStream(Stream *s)
{
	s->stream = NULL;
	s->handle = 0;
	s->code = 0;
}

/*
 * PlayStream is 0x4036e0: code's file played on stream s from its
 * start, a voice at twice the voices' volume and the rest at the
 * music's, and its length in ms, or 0 if it cannot be played.
 */
int32_t
PlayStream(Stream *s, uint32_t code)
{
	float volume;

	s->stream = OpenStream(code);
	if (s->stream == NULL)
		return 0;
	if (code & Voice)
		volume = GetVoiceVolume() + GetVoiceVolume();
	else
		volume = MusicStreamVolume;
	s->handle = soundplay(s->stream->path, volume);
	if (s->handle == 0) {
		fprintf(stderr, "lba: can't play music track 0x%X\n", (unsigned)code);
		StopStream(s);
		return 0;
	}
	s->code = code;
	return StreamLength(s->stream, code);
}

/* StopStream is 0x403770: stream s's voice stopped, its WavStream let go. */
void
StopStream(Stream *s)
{
	if (s->handle != 0) {
		soundstop(s->handle);
		s->handle = 0;
		s->code = 0;
	}
	if (s->stream != NULL) {
		free(s->stream);
		s->stream = NULL;
	}
}

/* PauseStream is 0x4037b0: stream s's voice paused where it plays. */
void
PauseStream(Stream *s)
{
	if (s->handle != 0)
		soundpause(s->handle, 1);
}

/* ResumeStream is 0x4037d0: stream s's voice played on. */
void
ResumeStream(Stream *s)
{
	if (s->handle != 0)
		soundpause(s->handle, 0);
}

/*
 * StreamPlaying is 0x4037f0: whether stream s plays, its voice's time
 * in ms, cut to 32 bits, short of its length. A stream whose voice has
 * ended is stopped.
 */
int
StreamPlaying(Stream *s)
{
	double t;
	int32_t length;

	if (s->code == 0)
		return 0;
	if (!soundvalid(s->handle)) {
		StopStream(s);
		return 0;
	}
	t = soundtime(s->handle);
	length = StreamLength(s->stream, s->code);
	return (uint32_t)(int64_t)(t * 1000.0) < (uint32_t)length;
}

/* StreamVolume is 0x403920: stream s's voice at volume. */
void
StreamVolume(Stream *s, float volume)
{
	if (s->handle != 0)
		soundvolume(s->handle, volume);
}

/*
 * InitMusic is 0x403970: the sound handler made, and 10 if it is
 * there, else 0, which the midi module keeps as Midi_Driver_Enable. The
 * port's, stream.c, runs on the port's clock whatever the sound device,
 * so that a replay goes alike everywhere.
 */
int32_t
InitMusic(void)
{
	ready = 1;
	return 10;
}

/* FUN_00403980 is 0x403980, a return alone. Dead code. */
void
FUN_00403980(void)
{
}

/* StreamOfCode is 0x403990: the stream of code. Dead code. */
Stream*
StreamOfCode(uint32_t code)
{
	if (code & Midi)
		return &StreamMidi;
	if (code & Voice)
		return &StreamVoice;
	return &StreamCD;
}

/*
 * The length in ms of w, as the Classic counts it: its samples as a
 * float over its rate, a float, then times 1000 as a double, cut to an
 * integer, each step rounded as the Classic's SSE rounds it.
 */
static int32_t
ms(Wavstream *w)
{
	float q;
	double t;

	if (w->rate == 0)
		return 0;
	q = (float)w->samples / w->rate;
	t = (double)q * 1000.0;
	return (int32_t)t;
}

/*
 * StreamLength is 0x4039c0: the length in ms of w, code's file, a
 * track's or a tune's counted the first time and kept, a voice's each
 * time, and 0 for a track or a tune past the tables.
 */
int32_t
StreamLength(Wavstream *w, uint32_t code)
{
	int32_t *p;

	if (code & Voice)
		return ms(w);
	if (code & Midi) {
		code &= 0xfffeffff;
		if (code > 32)
			return 0;
		p = &StreamLengthMidi[code];
	} else {
		if (code - 2 > 8)
			return 0;
		p = &StreamLengthCD[code];
	}
	if (*p != 0)
		return *p;
	*p = ms(w);
	return *p;
}

/*
 * OpenStream is 0x403ab0: code's file opened and parsed as a WavStream,
 * as SoLoud's WavStream::load does, or NULL while the sound handler is
 * missing or for a missing file, told of the first time.
 *
 * FEATURE: the file sought in the CD's directory, where the Classic seeks
 * Common\... in its own. The port's own.
 */
Wavstream*
OpenStream(uint32_t code)
{
	char name[Namelen], file[Namelen+4], *p;
	Wavstream *w;
	Decoder *d;
	uint32_t rate, channels;
	uint64_t n;
	uint8_t *t;

	if (!ready)
		return NULL;
	w = calloc(1, sizeof *w);
	if (w == NULL)
		return NULL;
	MusicFileName(code, name);
	snprintf(file, sizeof file, "D:\\%s", name);
	d = NULL;
	if (readpath(file, w->path, sizeof w->path) == 0)
		d = decopen(w->path, Padded);
	/*
	 * FEATURE: DotEmu's Ogg Vorbis file of the same name where the
	 * Classic's MP3 is missing. The port's own.
	 */
	if (d == NULL && (p = strrchr(file, '.')) != NULL) {
		strcpy(p, ".ogg");
		if (readpath(file, w->path, sizeof w->path) == 0)
			d = decopen(w->path, Padded);
	}
	if (d == NULL) {
		t = code & Midi ? &told[(code & 0xffff) % Ntune] : &told[Ntune + code%11];
		if (!*t && !(code & Voice))
			fprintf(stderr, "lba: can't open stream file '%s'\n", name);
		*t = 1;
		free(w);
		return NULL;
	}
	decinfo(d, &rate, &channels, &n);
	w->rate = (float)rate;
	w->samples = (uint32_t)n;
	decclose(d);
	return w;
}

/*
 * PlayTrackCDR is 0x403c90: code's file played on its stream, the
 * tune's for a tune, the voice's for a voice, the CD's for a track, and
 * its length in ms, or 0 if it cannot be played.
 */
static int32_t
PlayTrackCDR(uint32_t code)
{
	if (!ready)
		return 0;
	if (code & Midi)
		return PlayStream(&StreamMidi, code);
	return PlayStream(code & Voice ? &StreamVoice : &StreamCD, code);
}

/* StopCDR is 0x403cd0: code's stream stopped. */
static void
StopCDR(uint32_t code)
{
	if (code & Midi) {
		StopStream(&StreamMidi);
		return;
	}
	StopStream(code & Voice ? &StreamVoice : &StreamCD);
}

/* PauseCDR is 0x403d00: code's stream paused. */
static void
PauseCDR(uint32_t code)
{
	if (code & Midi) {
		PauseStream(&StreamMidi);
		return;
	}
	PauseStream(code & Voice ? &StreamVoice : &StreamCD);
}

/* ResumeCDR is 0x403d30: code's stream played on. */
static void
ResumeCDR(uint32_t code)
{
	if (code & Midi) {
		ResumeStream(&StreamMidi);
		return;
	}
	ResumeStream(code & Voice ? &StreamVoice : &StreamCD);
}

/* IsMidiPlaying is 0x403d60: whether code's stream plays. */
static int32_t
IsMidiPlaying(uint32_t code)
{
	if (code & Midi)
		return StreamPlaying(&StreamMidi);
	return StreamPlaying(code & Voice ? &StreamVoice : &StreamCD);
}

/* SetMusicVolume is 0x403da0: the music's volume, the CD's and the tune's streams'. */
void
SetMusicVolume(float volume)
{
	MusicStreamVolume = volume;
	StreamVolume(&StreamCD, volume);
	StreamVolume(&StreamMidi, volume);
}

/* SetVoiceVolume is 0x403de0: the voice's stream at volume, kept in MixVoice for the next. */
void
SetVoiceVolume(float volume)
{
	MixVoice = volume;
	StreamVolume(&StreamVoice, volume);
}

/* InitMidi is 0x409cb0: the music started, Midi_Driver_Enable its answer. */
static void
InitMidi(void)
{
	Midi_Driver_Enable = InitMusic();
}

/* ClearMidi is 0x409cc0. */
static void
ClearMidi(void)
{
	Midi_Driver_Enable = 0;
}

/* PlayMidi is 0x409cd0: tune num played on the tune's stream. */
static void
PlayMidi(int32_t num)
{
	NumXmi = num;
	PlayTrackCDR(num | Midi);
}

/* StopMidi is 0x409cf0: the tune stopped while it plays, and forgotten. */
static void
StopMidi(void)
{
	if (NumXmi >= 0 && IsMidiPlaying(Midi)) {
		StopCDR(Midi);
		NumXmi = -1;
	}
}

/* FadeMidiDown is 0x409d20: the tune stopped, as StopMidi. */
static void
FadeMidiDown(void)
{
	StopMidi();
}

/*
 * DoLoopMidi is 0x40bbe0, of the Classic's ambiance: the tune played
 * again once it has ended. Dead code.
 */
static void
DoLoopMidi(void)
{
	if (Midi_Driver_Enable && NumXmi != -1 && !IsMidiPlaying(Midi))
		PlayMidi(NumXmi);
}

/*
 * FEATURE: LBA2's pause of the music (PauseMusic, ResumeMusic) on the
 * Classic's CD stream (midi 0) or tune stream (midi 1): the stream
 * paused and set aside, so that the menu's music plays on the stream
 * made empty, as LBA2 keeps the stream it pauses, then the menu's
 * stopped and the stream set aside played on. The port's own.
 */
static Stream aside[2];

void
musicpause(int midi)
{
	Stream *s;

	s = midi ? &StreamMidi : &StreamCD;
	StopStream(&aside[midi]);
	PauseStream(s);
	aside[midi] = *s;
	InitStream(s);
}

void
musicresume(int midi)
{
	Stream *s;

	s = midi ? &StreamMidi : &StreamCD;
	StopStream(s);
	if (aside[midi].stream == NULL)
		return;
	*s = aside[midi];
	InitStream(&aside[midi]);
	ResumeStream(s);
	StreamVolume(s, MusicStreamVolume);
}

Tlba1c	tlba1c = {
	InitCDR,
	PlayTrackCDR,
	StopCDR,
	PauseCDR,
	ResumeCDR,
	IsMidiPlaying,
	InitMidi,
	ClearMidi,
	PlayMidi,
	StopMidi,
	FadeMidiDown,
	DoLoopMidi,
};
