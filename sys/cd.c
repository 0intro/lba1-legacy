#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "dat.h"
#include "fns.h"

/*
 * The CD, as lib386/lib_cd/cdrom.asm drove it through MSCDEX: a drive
 * letter for its files, a file's place in sectors (GetFileCDR) to read
 * it by sectors (ReadLongCDR), and its audio tracks. Here the CD is a
 * directory, drive D:, and a file's sectors are its own 2048 bytes from
 * a start of ours, or a CD's image (cdimage.c), its sectors the
 * image's. A track is a file beside them, trackNN.wav as made from a
 * CD's image by tools/cdtracks.py, or the image's track: 16-bit stereo
 * at 44100 Hz, its length its frames of 2352 bytes, 75 a second, as the
 * CD's table of contents gave it. One plays from its start to its end.
 * The port's clock takes its frames as it passes them, a few ahead
 * (cdtick), and hands them to their own stream.
 */

enum
{
	Sector = 2048,
	Start = 16,	/* the first file's sector: past ISO 9660's own */
	Frame = 2352,	/* of audio: 588 of 16-bit stereo */
};

typedef struct Track Track;

struct Track
{
	FILE	*f;	/* a host's file, */
	Image	*img;	/* or the image whose sound it is */
	int32_t	audio;	/* the image's audio track the sound lies in */
	int32_t	num;	/* the CD's track the game plays, 0 for none */
	uint32_t	n;	/* its frames */
	uint64_t	fed;	/* frames handed */
	uint64_t	start;	/* ns */
	uint64_t	base;	/* the byte of its file, or of its image's track, it starts at */
	uint64_t	at;	/* the byte of the track read next */
};

int16_t	DriveCDR = -2;	/* cdrom.asm's, -2 for no drive */
int32_t	FileCD_Start;
int32_t	FileCD_Sect;
int32_t	FileCD_Size;

static char cdfile[1024];
static Image *cdimg;	/* GetFileCDR's file's image, NULL for a host's file */
static uint32_t cdstart;	/* its first sector there */
static uint32_t cdsize;	/* its bytes */
static Track track;
static uint64_t now;
static uint64_t played;	/* frames, every track's */
static int32_t pausedtrack;	/* LBA2's PausedTrack: the track PauseCD stopped, 0 for none */
static uint64_t pausedframe;	/* LBA2's Cur: the frame it stopped at */

/*
 * InitCDR is cdrom.asm's: whether the CD's root holds the file name, the
 * CD's drive D: in DriveCDR, or -2 when the CD has no bind, as cdrom.asm
 * gives it without a drive. A CD without the file keeps D:, where
 * cdrom.asm gives -1: the port reads its other files where they lie, the
 * voices among them, which the game names from DriveCDR.
 */
int32_t
InitCDR(char *name)
{
	char file[300];

	setcdletter('D');
	if (!havecd()) {
		DriveCDR = -2;
		return 0;
	}
	DriveCDR = 'D' - 'A';
	snprintf(file, sizeof file, "D:\\%s", name);
	return havefile(file);
}

static uint32_t
le32(const uint8_t *p)
{
	return p[0] | p[1]<<8 | p[2]<<16 | (uint32_t)p[3]<<24;
}

/*
 * The file of name on the CD opened at its sound into t, or the image's
 * track it names, and its frames, those its file holds: 0, or -1 if the
 * CD lacks it or it holds no CD audio.
 */
static int
audioopen(char *name, Track *t)
{
	char path[1024];
	uint8_t h[24];
	uint32_t size, first;
	long at, end;
	int fmt;
	FILE *f;

	memset(t, 0, sizeof *t);
	if (readpath(name, path, sizeof path) < 0) {
		if (cdtrack(name, &t->img, &t->audio, &first, &t->n) < 0)
			return -1;
		t->base = (uint64_t)first * Frame;
		return 0;
	}
	f = fopen(path, "rb");
	if (f == NULL)
		return -1;
	if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) != 0 || memcmp(h+8, "WAVE", 4) != 0)
		goto bad;
	fmt = 0;
	while (fread(h, 1, 8, f) == 8) {
		size = le32(h+4);
		if (memcmp(h, "fmt ", 4) == 0) {
			if (size < 16 || fread(h, 1, 16, f) != 16)
				goto bad;
			/* PCM, 2 channels, 44100 Hz, 16 bits */
			fmt = h[0] == 1 && h[1] == 0 && h[2] == 2 && h[3] == 0 && le32(h+4) == 44100 && h[14] == 16;
			size -= 16;
		} else if (memcmp(h, "data", 4) == 0) {
			if (!fmt || (at = ftell(f)) < 0 || fseek(f, 0, SEEK_END) != 0 || (end = ftell(f)) < at
			|| fseek(f, at, SEEK_SET) != 0)
				goto bad;
			if (size > (unsigned long)(end - at))
				size = end - at;
			t->f = f;
			t->n = size / Frame;
			t->base = at;
			return 0;
		}
		if (size > 0x7ffffffe || fseek(f, size + (size&1), SEEK_CUR) != 0)
			goto bad;
	}
bad:
	fclose(f);
	return -1;
}

/* The CD's track num into t, as audioopen opens it. */
static int
trackopen(int32_t num, Track *t)
{
	char name[32];

	if (num < 1 || num > 99)
		return -1;
	snprintf(name, sizeof name, "D:\\TRACK%02d.WAV", (int)num);
	return audioopen(name, t);
}

/*
 * n bytes of t's sound from its byte t->at, as a drive plays them: an
 * image's track as imagetrackread gives it, or its file's bytes, zeros
 * past its end.
 */
static void
trackread(Track *t, uint8_t *buf, size_t n)
{
	size_t got;

	if (t->img != NULL)
		imagetrackread(t->img, t->audio, t->base + t->at, buf, n);
	else {
		got = fread(buf, 1, n, t->f);
		memset(buf + got, 0, n - got);
	}
	t->at += n;
}

/* t's sound read from its byte at on: 0, or -1. */
static int
trackseek(Track *t, uint64_t at)
{
	if (t->img == NULL && (t->base + at > LONG_MAX || fseek(t->f, (long)(t->base + at), SEEK_SET) != 0))
		return -1;
	t->at = at;
	return 0;
}

/* Whether t is open. */
static int
trackon(Track *t)
{
	return t->f != NULL || t->img != NULL;
}

/* t closed. */
static void
trackclose(Track *t)
{
	if (t->f != NULL)
		fclose(t->f);
	t->f = NULL;
	t->img = NULL;
}

/* StopCDR is cdrom.asm's: the track playing stopped. */
void
StopCDR(void)
{
	trackclose(&track);
	sdlcdstop();
}

/* ClearCDR is cdrom.asm's: the track playing stopped. */
void
ClearCDR(void)
{
	StopCDR();
}

/*
 * PlayTrackCDR is cdrom.asm's: the track played from its start, for its
 * length, in place of the one playing, which a track the CD lacks leaves
 * as it is. 0 if it plays.
 */
int32_t
PlayTrackCDR(int32_t t)
{
	Track nt;

	if (trackopen(t, &nt) < 0)
		return 1;
	StopCDR();
	track = nt;
	track.num = t;
	track.start = now;
	return 0;
}

/*
 * FEATURE: PauseCD is LBA2's CD.CPP PauseCD: the track playing stopped,
 * the frame the clock had reached kept, for ResumeCD. Taken from LBA2.
 *
 * FIXED: LBA2's PauseCD returns at once when no track plays, keeping an
 * older pause, which a ResumeCD then played again. That pause is
 * forgotten.
 */
void
PauseCD(void)
{
	pausedtrack = 0;
	if (!trackon(&track))
		return;
	pausedtrack = track.num;
	pausedframe = (now - track.start) * 75 / 1000000000;
	StopCDR();
}

/*
 * FEATURE: ResumeCD is LBA2's CD.CPP ResumeCD: the track PauseCD stopped
 * played on from its frame to its end, as from the time the clock had
 * then reached. Taken from LBA2.
 */
void
ResumeCD(void)
{
	Track nt;

	if (pausedtrack == 0)
		return;
	StopCDR();
	if (trackopen(pausedtrack, &nt) == 0) {
		if (pausedframe < nt.n && trackseek(&nt, pausedframe*Frame) == 0) {
			track = nt;
			track.num = pausedtrack;
			track.fed = pausedframe;
			track.start = now - (pausedframe*1000000000 + 74) / 75;
		} else
			trackclose(&nt);
	}
	pausedtrack = 0;
}

/*
 * GetLengthTrackCDR is cdrom.asm's: a track's length in frames, the
 * start of the next less its own, or 0 for one the CD lacks.
 */
int32_t
GetLengthTrackCDR(int32_t t)
{
	Track nt;

	/*
	 * FIXED: LBA.EXE's StartTrackCDR answers a track the CD lacks with
	 * what its buffer last held. Such a track has a length of 0.
	 */
	if (trackopen(t, &nt) < 0)
		return 0;
	trackclose(&nt);
	return nt.n;
}

/*
 * The frames of t that the clock has passed, and Ahead more, at most
 * its own.
 */
static uint64_t
due(Track *t, uint64_t ns)
{
	uint64_t n;

	n = (ns - t->start) * 75 / 1000000000 + Ahead;
	return n > t->n ? t->n : n;
}

/*
 * cdtick: the frames of the track playing that the clock has passed,
 * and Ahead more, read and handed on, each summed in a record. At its
 * end it stops.
 */
void
cdtick(uint64_t ns)
{
	uint8_t buf[Frame];
	uint64_t n;

	now = ns;
	if (trackon(&track)) {
		for (n = due(&track, ns); track.fed < n; track.fed++) {
			trackread(&track, buf, Frame);
			sdlcd(buf, Frame);
			summedcd(++played, buf, Frame);
		}
		if (track.fed == track.n)
			trackclose(&track);
	}
}

/*
 * GetFileCDR is cdrom.asm's: the file's start, size and number of
 * sectors, which it counts as (size+2048)/2048. The number, or 0.
 */
int32_t
GetFileCDR(char *name)
{
	char file[300], path[sizeof cdfile];
	Image *img;
	uint32_t start, size, n;

	snprintf(file, sizeof file, "D:%s", name);
	if (cdimagefile(file, &img, &start, &size)) {
		cdimg = img;
		cdstart = start;
		cdsize = size;
		FileCD_Start = start;
		FileCD_Size = size;
		FileCD_Sect = (size + 2048) / 2048;
		return FileCD_Sect;
	}
	/*
	 * FIXED: LBA.EXE's GetFileCDR takes MSCDEX's failure for a success,
	 * and answers a file the CD lacks with the last file found. Such a
	 * file gives 0, FileCD_* and the file ReadLongCDR reads as they
	 * were.
	 */
	if (readpath(file, path, sizeof path) < 0)
		return 0;
	cdimg = NULL;
	memmove(cdfile, path, strlen(path)+1);
	n = FileSize(file);
	FileCD_Start = Start;
	FileCD_Size = n;
	FileCD_Sect = (n + 2048) / 2048;
	return FileCD_Sect;
}

/*
 * ReadLongCDR reads sectors of the last file GetFileCDR found: 0 if it
 * did. Past the file's end the CD went on into what followed it, and
 * GetFileCDR counts a sector too many when the size is a multiple of
 * 2048: there, zeros.
 */
int32_t
ReadLongCDR(int32_t start, int32_t n, void *buf)
{
	FILE *f;
	size_t got;
	uint32_t pos, k;

	if (cdimg != NULL) {
		memset(buf, 0, (size_t)n*Sector);
		if (start < (int32_t)cdstart)
			return 0;
		pos = (uint32_t)(start - cdstart) * Sector;
		if (pos < cdsize) {
			k = (uint32_t)n*Sector;
			if (k > cdsize - pos)
				k = cdsize - pos;
			imageread(cdimg, cdstart, pos, buf, k);
		}
		return 0;
	}
	f = fopen(cdfile, "rb");
	if (f == NULL)
		return -1;
	memset(buf, 0, (size_t)n*Sector);
	got = 0;
	if (fseek(f, (long)(start-Start)*Sector, SEEK_SET) == 0)
		got = fread(buf, 1, (size_t)n*Sector, f);
	USED(got);
	fclose(f);
	return 0;
}
