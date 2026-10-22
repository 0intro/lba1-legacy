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
 * CD's image by tools/cdtracks.py, or the image's sound, its track or a
 * file its directory places in one (cdimage.c's imagesound): 16-bit stereo
 * at 44100 Hz, its length its frames of 2352 bytes, 75 a second, as the
 * CD's table of contents gave it. One plays from its start to its end.
 * The port's clock takes its frames as it passes them, a few ahead
 * (cdtick), and hands them to their own stream.
 *
 * FEATURE: the PlayStation's track of a tune, a file on the CD, plays on
 * the music's channel in place of its XMIDI (PlayMidiFile, midi.c), as
 * the PlayStation plays its tunes from its disc: once, from its start to
 * its end, its frames taken as the clock passes them, Ahead more,
 * scaled by its volume, a percent that the music's calls set at once or
 * ramp down along the clock, and handed to a stream of their own at the
 * CD's gain. Taken from the PlayStation.
 *
 * FEATURE: a tune's cut from a track of the CD, as DotEmu cut it for its
 * recording (cuts.c), plays there in the same way: the track's frames
 * from the cut's first, the place its mark finds on this CD, to its end,
 * each sample scaled by the cut's gain at its frame. Taken from DotEmu
 * 1.06.
 */

enum
{
	Sector = 2048,
	Start = 16,	/* the first file's sector: past ISO 9660's own */
	Frame = 2352,	/* of audio: 588 of 16-bit stereo */
	Samples = Frame/4,	/* a frame's, each a 16-bit stereo pair */
	Mark = 32,	/* the samples a cut's mark sums */
	Markat = 44100,	/* their place, from its first */
	Markseek = 2*44100 + 4096,	/* the samples about its place on the PlayStation's tracks it is sought in: GOG's and Steam's image starts its tracks 2 s late */
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

typedef struct Pstrack Pstrack;

/* the PlayStation's track in the XMIDI's place, or a cut */
struct Pstrack
{
	Track	t;
	char	file[300];	/* on the CD */
	uint32_t	first;	/* the file's sample it starts at */
	uint32_t	skip;	/* a cut's samples before the track's start */
	uint32_t	samples;	/* its samples, 0 for the file's whole */
	Cut	*cut;	/* the gain's knots, or NULL */
	int	on;	/* asked for, not stopped since */
	int	paused;
	uint64_t	pausedframe;	/* the clock's, when paused */
	int32_t	from;	/* the volume at fadestart, a percent */
	int32_t	to;	/* the volume from fadeend */
	uint64_t	fadestart;	/* ns */
	uint64_t	fadeend;	/* ns */
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
static Pstrack ps;
static uint64_t psplayed;	/* frames, every PlayStation track's */
static int32_t placed[Ntune];	/* each cut's first on this CD */
static uint8_t isplaced[Ntune];

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
 * past its end. The bytes it holds.
 */
static size_t
trackread(Track *t, uint8_t *buf, size_t n)
{
	size_t got;

	if (t->img != NULL)
		got = imagetrackread(t->img, t->audio, t->base + t->at, buf, n);
	else {
		got = fread(buf, 1, n, t->f);
		memset(buf + got, 0, n - got);
	}
	t->at += n;
	return got;
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

/* The PlayStation's track's volume at time t, a percent. */
static int32_t
psvolume(uint64_t t)
{
	if (t >= ps.fadeend)
		return ps.to;
	if (t <= ps.fadestart)
		return ps.from;
	return ps.from + (int64_t)(ps.to - ps.from) * (int64_t)(t - ps.fadestart)
		/ (int64_t)(ps.fadeend - ps.fadestart);
}

/* A frame's samples, 16-bit little-endian, scaled to v percent. */
static void
scale(uint8_t *buf, int32_t v)
{
	int32_t s;
	uint16_t u;
	int i;

	if (v == 100)
		return;
	for (i = 0; i < Frame; i += 2) {
		s = buf[i] | buf[i+1]<<8;
		if (s >= 0x8000)
			s -= 0x10000;
		u = (uint16_t)(s * v / 100);
		buf[i] = u & 0xff;
		buf[i+1] = u >> 8;
	}
}

/* The gain of cut c at its sample i, in Units. */
static int32_t
cutgain(Cut *c, uint64_t i)
{
	Knot *k;
	int j;

	if (c->nknot == 0 || i < c->knot[0].at)
		return Unit;
	for (j = 1; j < c->nknot; j++) {
		k = &c->knot[j];
		if (i < k->at)
			return k[-1].gain + (int64_t)(k->gain - k[-1].gain) * (int64_t)(i - k[-1].at)
				/ (int64_t)(k->at - k[-1].at);
	}
	return c->knot[c->nknot-1].gain;
}

/* The recording's frame f, each sample scaled by its cut's gain. */
static void
cutframe(uint8_t *buf, uint64_t f)
{
	int32_t s, g;
	uint16_t u;
	int k, b;

	if (ps.cut == NULL)
		return;
	for (k = 0; k < Samples; k++) {
		g = cutgain(ps.cut, ps.skip + f*Samples + k);
		if (g == Unit)
			continue;
		for (b = 4*k; b < 4*k+4; b += 2) {
			s = buf[b] | buf[b+1]<<8;
			if (s >= 0x8000)
				s -= 0x10000;
			u = (uint16_t)(s * g / Unit);
			buf[b] = u & 0xff;
			buf[b+1] = u >> 8;
		}
	}
}

/*
 * cdtick: the frames of the track playing that the clock has passed,
 * and Ahead more, read and handed on, each summed in a record. At its
 * end it stops. So too the PlayStation's track or the cut, its frames
 * scaled by its volume at their time.
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
	if (trackon(&ps.t)) {
		for (n = due(&ps.t, ns); ps.t.fed < n; ps.t.fed++) {
			trackread(&ps.t, buf, Frame);
			cutframe(buf, ps.t.fed);
			scale(buf, psvolume(ps.t.start + ps.t.fed*1000000000/75));
			sdlpstrack(buf, Frame);
			summedpstrack(++psplayed, buf, Frame);
		}
		if (ps.t.fed == ps.t.n)
			trackclose(&ps.t);
	}
}

/*
 * pstracklength: the length in frames of name, the file of a
 * PlayStation track on the CD, or 0 if the CD lacks it.
 */
int32_t
pstracklength(char *name)
{
	char file[300];
	Track t;

	snprintf(file, sizeof file, "D:\\%s", name);
	if (audioopen(file, &t) < 0)
		return 0;
	trackclose(&t);
	return t.n;
}

/* pstrackstop: the PlayStation's track stopped, what waits of it dropped. */
void
pstrackstop(void)
{
	trackclose(&ps.t);
	ps.on = 0;
	ps.paused = 0;
	sdlpstrackstop();
}

/*
 * The file of the PlayStation's track opened at frame, its clock set so
 * that the frame plays now. 0 if it cannot be.
 */
static int
psopen(uint64_t frame)
{
	Track t;
	uint64_t all, len;
	uint32_t n;

	if (audioopen(ps.file, &t) < 0)
		return 0;
	all = (uint64_t)t.n * Samples;
	len = ps.samples != 0 ? ps.samples : all;
	if (ps.first + len > all)
		len = ps.first < all ? all - ps.first : 0;
	n = (len + Samples - 1) / Samples;
	if (frame >= n || trackseek(&t, (ps.first + frame*Samples)*4) != 0) {
		trackclose(&t);
		return 0;
	}
	ps.t = t;
	ps.t.n = n;
	ps.t.fed = frame;
	ps.t.start = now - (frame*1000000000 + 74) / 75;
	return 1;
}

/*
 * pstrackplay: name, the file of a PlayStation track on the CD, played
 * from its start at full volume.
 */
void
pstrackplay(char *name)
{
	pstrackstop();
	snprintf(ps.file, sizeof ps.file, "D:\\%s", name);
	ps.first = 0;
	ps.skip = 0;
	ps.samples = 0;
	ps.cut = NULL;
	ps.from = ps.to = 100;
	ps.fadestart = ps.fadeend = now;
	ps.on = psopen(0);
}

/*
 * The first sample of tune num's cut on this CD's track: where its mark
 * falls, sought about its place on the PlayStation's tracks, the
 * nearest first, or that place, for a CD whose samples differ there.
 * Below 0 the cut starts before the track does.
 */
static int32_t
cutplace(int32_t num)
{
	static uint8_t buf[(2*Markseek+Mark)*4];
	Cut *c;
	Track t;
	int32_t at, d, p;
	size_t got;

	c = &cuts[num];
	if (isplaced[num])
		return placed[num];
	isplaced[num] = 1;
	placed[num] = c->first;
	at = (int32_t)c->first + Markat - Markseek;
	if (at < 0)
		at = 0;
	if (trackopen(c->track, &t) < 0)
		return placed[num];
	got = 0;
	if (trackseek(&t, (uint64_t)at*4) == 0)
		got = trackread(&t, buf, sizeof buf);
	trackclose(&t);
	for (d = 0; d <= Markseek; d++) {
		p = (int32_t)c->first + Markat + d - at;
		if (p >= 0 && (size_t)(p+Mark)*4 <= got && crc(0, buf + p*4, Mark*4) == c->mark) {
			placed[num] = (int32_t)c->first + d;
			break;
		}
		p = (int32_t)c->first + Markat - d - at;
		if (d > 0 && p >= 0 && (size_t)(p+Mark)*4 <= got && crc(0, buf + p*4, Mark*4) == c->mark) {
			placed[num] = (int32_t)c->first - d;
			break;
		}
	}
	return placed[num];
}

/*
 * cutlength: the length in frames of tune num's cut, or 0 if it has none
 * or the CD lacks its track.
 */
int32_t
cutlength(int32_t num)
{
	Track t;

	if (num < 0 || num >= Ntune || cuts[num].track == 0)
		return 0;
	if (trackopen(cuts[num].track, &t) < 0)
		return 0;
	trackclose(&t);
	if (cuts[num].n == 0)
		return t.n;
	return (cuts[num].n + Samples - 1) / Samples;
}

/* cutplay: tune num's cut played from its start at full volume. */
void
cutplay(int32_t num)
{
	Cut *c;
	int32_t at;

	pstrackstop();
	c = &cuts[num];
	snprintf(ps.file, sizeof ps.file, "D:\\TRACK%02d.WAV", (int)c->track);
	at = c->n != 0 ? cutplace(num) : 0;
	ps.first = at > 0 ? at : 0;
	ps.skip = at < 0 ? -at : 0;
	ps.samples = c->n - ps.skip;
	ps.cut = c->nknot != 0 ? c : NULL;
	ps.from = ps.to = 100;
	ps.fadestart = ps.fadeend = now;
	ps.on = psopen(0);
}

/* The frame of the PlayStation's track the clock has reached. */
static uint64_t
psframe(void)
{
	return (now - ps.t.start) * 75 / 1000000000;
}

/*
 * pstrackplaying: whether the PlayStation's track plays, the clock short
 * of its end.
 */
int
pstrackplaying(void)
{
	return ps.on && !ps.paused && psframe() < ps.t.n;
}

/* pstrackvolume: its volume, a percent of 0 to 100, at once. */
void
pstrackvolume(int32_t v)
{
	if (v < 0)
		v = 0;
	if (v > 100)
		v = 100;
	ps.from = ps.to = v;
	ps.fadestart = ps.fadeend = now;
}

/* pstrackfade: its volume ramped from what it is to 0 over ms. */
void
pstrackfade(uint32_t ms)
{
	ps.from = psvolume(now);
	ps.to = 0;
	ps.fadestart = now;
	ps.fadeend = now + (uint64_t)ms*1000000;
}

/*
 * pstrackpause: the PlayStation's track stopped, the frame the clock
 * had reached kept.
 */
void
pstrackpause(void)
{
	if (!pstrackplaying())
		return;
	ps.pausedframe = psframe();
	ps.paused = 1;
	trackclose(&ps.t);
	sdlpstrackstop();
}

/*
 * pstrackresume: the PlayStation's track played on from the frame it
 * was paused at.
 */
void
pstrackresume(void)
{
	if (!ps.on || !ps.paused)
		return;
	ps.paused = 0;
	ps.on = psopen(ps.pausedframe);
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
