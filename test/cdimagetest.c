#include <dirent.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif
#include "../sys/dat.h"
#include "../sys/dos.h"
#include "../sys/fns.h"

/*
 * cdimagetest: a CD's image read as the CD (sys/cdimage.c), and its
 * files and tracks as the game reads them through the port
 * (sys/files.c).
 *
 *	cdimagetest dir [image tree tracks]...
 *
 * In dir it writes small images of its own, an ISO and cue sheets of a
 * data track and two audio tracks, one file for the disc and one for
 * each track, the data track's sectors of MODE1/2352, MODE2/2352 as the
 * PlayStation's or MODE1/2048, the audio tracks in BINARY or WAVE
 * files, their samples little-endian or big-endian, and reads each file
 * and track back, then the image bound as the CD through the port's
 * calls: its files, its listings and its tracks as sys/cd.c finds them.
 * It writes malformed ones too, each of which must be refused, or open
 * and fail CD_LBA's lookup, and a raw image given in its sheet's place
 * must be refused for its sheet. A WAVE file's samples end with their
 * chunk or with the file, and a chunk past the file's end refuses it.
 * Then for each image named, a cue sheet or an ISO, every file of its
 * directories must have the bytes of the file of its name in tree, at
 * its path or else at its base name, every file of tree must be in the
 * image, and every trackNN.wav of tracks ("-" for none) must have the
 * samples of the image's sound of its name (sys/cdimage.c's
 * imagesound).
 */

enum
{
	Sector = 2048,
	Raw = 2352,
	Npath = 1100,
	Nimage = 32,	/* the sectors of the images written here */
	Nframe = 40,	/* each audio track's frames */
	Nbig = 1<<20,
};

typedef struct Case Case;

/*
 * A malformed image: the change to the good ISO, or a cue sheet's text,
 * and whether the image is refused, for the reason its text gives, or
 * opens and fails CD_LBA's lookup, whose record it spoils.
 */
struct Case
{
	char	*name;
	int	at;	/* the byte changed, -1 for none */
	int	byte;
	char	*cue;	/* NULL for the ISO */
	int	lookup;	/* the lookup fails, the image opening */
	char	*why;	/* a part of the refusal's text */
};

/* CD_LBA's record in the root: past . and .. and the associated file before it */
enum
{
	Cdrec = 18*Sector + 34 + 34 + 42,
};

/* The records' date, 1994-10-14 12:00:30: the years past 1900, the month, the day, the hour, the minute, the second. */
static uint8_t made[] = {94, 10, 14, 12, 0, 30};
static char gamedir[] = "LBA";	/* the CD's directory of the game */

static char *top;
static int fails;
#ifdef _WIN32
static int windows = 1;
#else
static int windows = 0;
#endif
static uint8_t iso[Nimage*Sector];
static uint8_t readme[3000];
static uint8_t big[Nbig], bigto[Nbig];

void
fatal(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "cdimagetest: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	exit(1);
}

static void
fail(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "cdimagetest: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	if (++fails >= 20)
		fatal("too many failures");
}

static void
join(char *out, int n, char *dir, char *name)
{
	if (snprintf(out, n, "%s/%s", dir, name) >= n)
		fatal("%s/%s: too long a path", dir, name);
}

/* path from the root: as it is if it starts there, else from the current directory */
static void
fromroot(char *out, int n, char *path)
{
	char cwd[Npath];

	if (path[0] == '/' || path[0] == '\\' || (path[0] != '\0' && path[1] == ':')) {
		if (snprintf(out, n, "%s", path) >= n)
			fatal("%s: too long a path", path);
		return;
	}
	if (getcwd(cwd, sizeof cwd) == NULL)
		fatal("no current directory");
	join(out, n, cwd, path);
}

#ifdef _WIN32
static void
makedir(char *path)
{
	_mkdir(path);
}
#else
static void
makedir(char *path)
{
	mkdir(path, 0777);
}
#endif

static void
put(char *path, uint8_t *b, size_t n)
{
	FILE *f;

	f = fopen(path, "wb");
	if (f == NULL || fwrite(b, 1, n, f) != n || fclose(f) != 0)
		fatal("writing %s", path);
}

static void
le16(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v>>8;
}

static void
le32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v>>8;
	p[2] = v>>16;
	p[3] = v>>24;
}

static void
be32(uint8_t *p, uint32_t v)
{
	p[0] = v>>24;
	p[1] = v>>16;
	p[2] = v>>8;
	p[3] = v;
}

/* A directory record at p of the extent at s, size bytes, a directory with dir, named name: its length. */
static int
record(uint8_t *p, uint32_t s, uint32_t size, int dir, char *name, int namelen)
{
	int len;

	len = 33 + namelen;
	len += len & 1;
	memset(p, 0, len);
	p[0] = len;
	le32(p+2, s);
	be32(p+6, s);
	le32(p+10, size);
	be32(p+14, size);
	memcpy(p+18, made, sizeof made);
	p[25] = dir ? 2 : 0;
	p[32] = namelen;
	memcpy(p+33, name, namelen);
	return len;
}

/*
 * The ISO written here: the volume at sector 16, the root at 18 with
 * CD_LBA. and LBA, LBA at 19 with README.TXT, CD_LBA's 22 bytes at 20
 * and README's 3000 at 21.
 */
static void
makeiso(void)
{
	uint8_t *p;
	int i;

	memset(iso, 0, sizeof iso);
	p = iso + 16*Sector;
	p[0] = 1;
	memcpy(p+1, "CD001", 5);
	p[6] = 1;
	memset(p+40, ' ', 32);
	memcpy(p+40, "ADELINE", 7);
	le32(p+80, Nimage);
	record(p+156, 18, Sector, 1, "\0", 1);
	p = iso + 17*Sector;
	p[0] = 255;
	memcpy(p+1, "CD001", 5);
	p = iso + 18*Sector;
	p += record(p, 18, Sector, 1, "\0", 1);
	p += record(p, 18, Sector, 1, "\1", 1);
	/* an associated file of CD_LBA's name, which a reader passes by */
	i = record(p, 21, 7, 0, "CD_LBA.;1", 9);
	p[25] = 4;
	p += i;
	p += record(p, 20, 22, 0, "CD_LBA.;1", 9);
	record(p, 19, Sector, 1, "LBA", 3);
	p = iso + 19*Sector;
	p += record(p, 19, Sector, 1, "\0", 1);
	p += record(p, 18, Sector, 1, "\1", 1);
	record(p, 21, sizeof readme, 0, "README.TXT;1", 12);
	memcpy(iso + 20*Sector, "\"15/08/94 Lba Demo\" \r\n", 22);
	for (i = 0; i < (int)sizeof readme; i++)
		readme[i] = i*7 + i/2048;
	memcpy(iso + 21*Sector, readme, sizeof readme);
}

/* Audio track t's frame f's sample k, a word of its own. */
static uint8_t
sample(int t, int f, int k)
{
	return (t*31 + f*7 + k) & 0xff;
}

/*
 * Whether the image's audio track t has at its frame f, as
 * imagetrackread gives it, the frame wf of the track wt written here,
 * or silence for wt 0.
 */
static int
trackframe(Image *m, int t, uint32_t f, int wt, int wf)
{
	uint8_t b[Raw];
	int k;

	if (imagetrackread(m, t, (uint64_t)f*Raw, b, Raw) != Raw)
		return 0;
	for (k = 0; k < Raw; k++)
		if (b[k] != (wt != 0 ? sample(wt, wf, k) : 0))
			return 0;
	return 1;
}

/* A WAV file's header of n frames of 16-bit stereo at 44100 Hz into h, a chunk of 8 bytes before its samples with list: its bytes. */
static int
wavhead(uint8_t *h, uint32_t n, int list)
{
	int k;

	k = 0;
	memcpy(h, "RIFF", 4);
	le32(h+4, 36 + n*Raw + (list ? 16 : 0));
	memcpy(h+8, "WAVEfmt ", 8);
	le32(h+16, 16);
	le16(h+20, 1);
	le16(h+22, 2);
	le32(h+24, 44100);
	le32(h+28, 44100*4);
	le16(h+32, 4);
	le16(h+34, 16);
	k = 36;
	if (list) {
		memcpy(h+k, "LIST", 4);
		le32(h+k+4, 8);
		memcpy(h+k+8, "INFOabcd", 8);
		k += 16;
	}
	memcpy(h+k, "data", 4);
	le32(h+k+4, n*Raw);
	return k + 8;
}

/*
 * The data track's sectors as raw sectors of mode, 1, or 2 of Form 1
 * after a subheader of its own, or as the ISO's for mode 0, then two
 * audio tracks, into path, one file or with split one for each track,
 * the tracks as WAVE files with wave.
 */
static void
makebin(char *path, int split, int mode, int wave)
{
	char name[Npath];
	FILE *f;
	uint8_t raw[Raw], h[64];
	int s, t, k, n;

	f = fopen(path, "wb");
	if (f == NULL)
		fatal("writing %s", path);
	for (s = 0; s < Nimage; s++) {
		memset(raw, 0, sizeof raw);
		memset(raw+1, 0xff, 10);
		raw[15] = mode;
		if (mode == 0)
			memcpy(raw, iso + s*Sector, Sector);
		else if (mode == 2) {
			/* the subheader twice: file 1, channel 1, a data sector of Form 1 */
			raw[16] = raw[20] = 1;
			raw[17] = raw[21] = 1;
			raw[18] = raw[22] = 0x08;
			memcpy(raw+24, iso + s*Sector, Sector);
		} else
			memcpy(raw+16, iso + s*Sector, Sector);
		n = mode == 0 ? Sector : Raw;
		if (fwrite(raw, 1, n, f) != (size_t)n)
			fatal("writing %s", path);
	}
	for (t = 2; t <= 3; t++) {
		if (split) {
			if (fclose(f) != 0)
				fatal("writing %s", path);
			snprintf(name, sizeof name, "%s%d", path, t);
			f = fopen(name, "wb");
			if (f == NULL)
				fatal("writing %s", name);
			if (wave) {
				n = wavhead(h, Nframe, t == 3);
				if (fwrite(h, 1, n, f) != (size_t)n)
					fatal("writing %s", name);
			}
		}
		for (s = 0; s < Nframe; s++) {
			for (k = 0; k < Raw; k++)
				raw[k] = sample(t, s, k);
			if (fwrite(raw, 1, Raw, f) != Raw)
				fatal("writing %s", path);
		}
	}
	if (fclose(f) != 0)
		fatal("writing %s", path);
}

/* A sound's sample n of channel c, a triangle wave stepping by 100. */
static int
wave(uint32_t n, int c)
{
	uint32_t p;

	p = (n + 100*c) % 400;
	return p < 200 ? (int)p*100 - 10000 : (400 - (int)p)*100 - 10000;
}

/* The byte k of the wave's samples, 16-bit stereo, little-endian. */
static uint8_t
wavebyte(uint64_t k)
{
	uint16_t v;

	v = (uint16_t)wave((uint32_t)(k/4), (int)(k/2 % 2));
	return k%2 == 0 ? v & 0xff : v >> 8;
}

/*
 * The data track's sectors as raw sectors of mode 1, then an audio
 * track of Nframe frames of the wave, big-endian with big, into path.
 */
static void
makewave(char *path, int big)
{
	FILE *f;
	uint8_t raw[Raw];
	uint64_t k;
	int s, j;

	f = fopen(path, "wb");
	if (f == NULL)
		fatal("writing %s", path);
	for (s = 0; s < Nimage; s++) {
		memset(raw, 0, sizeof raw);
		memset(raw+1, 0xff, 10);
		raw[15] = 1;
		memcpy(raw+16, iso + s*Sector, Sector);
		if (fwrite(raw, 1, Raw, f) != Raw)
			fatal("writing %s", path);
	}
	for (s = 0; s < Nframe; s++) {
		for (j = 0; j < Raw; j++) {
			k = (uint64_t)s*Raw + j;
			raw[j] = wavebyte(big ? k^1 : k);
		}
		if (fwrite(raw, 1, Raw, f) != Raw)
			fatal("writing %s", path);
	}
	if (fclose(f) != 0)
		fatal("writing %s", path);
}

/* Whether the image's audio track 2 gives n bytes from its byte at as the wave's, little-endian. */
static int
waveread(Image *m, uint64_t at, uint32_t n)
{
	uint8_t b[2*Raw];
	uint32_t k;

	if (n > sizeof b || imagetrackread(m, 2, at, b, n) != n)
		return 0;
	for (k = 0; k < n; k++)
		if (b[k] != wavebyte(at + k))
			return 0;
	return 1;
}

/* The good image's files and tracks read back. */
static void
check(char *what, Image *m, int audio)
{
	uint8_t b[3100], frame[Raw];
	uint32_t s, size, n;
	int t;

	if (imagefile(m, "CD_LBA", &s, &size) != 0 || s != 20 || size != 22)
		fail("%s: CD_LBA: %u %u, want 20 22", what, s, size);
	else if (imageread(m, s, 0, b, size) != 22 || memcmp(b, "\"15/08/94 Lba Demo\" \r\n", 22) != 0)
		fail("%s: CD_LBA's bytes", what);
	if (imagefile(m, "lba/readme.txt", &s, &size) != 0 || s != 21 || size != sizeof readme)
		fail("%s: lba/readme.txt, any case: %u %u, want 21 %u", what, s, size, (unsigned)sizeof readme);
	else if (imageread(m, s, 100, b, size-100) != size-100 || memcmp(b, readme+100, size-100) != 0)
		fail("%s: README.TXT's bytes from 100", what);
	if (imagefile(m, "LBA", &s, &size) == 0 || !imagedir(m, "lba") || !imagedir(m, ""))
		fail("%s: LBA and the root, directories, taken for files", what);
	if (imagefile(m, "LBA/NONE.TXT", &s, &size) == 0 || imagedir(m, "LBA/NONE"))
		fail("%s: LBA/NONE.TXT or LBA/NONE found", what);
	if (imagefile(m, "CD_LBA/X", &s, &size) == 0)
		fail("%s: CD_LBA/X found below a file", what);
	if (strcmp(imagelabel(m), "ADELINE") != 0)
		fail("%s: the volume %s, want ADELINE", what, imagelabel(m));
	if (!audio) {
		if (imageaudio(m, 2) != 0)
			fail("%s: an ISO with an audio track", what);
		return;
	}
	for (t = 2; t <= 3; t++) {
		if ((n = imageaudio(m, t)) != Nframe) {
			fail("%s: track %d: %u frames, want %d", what, t, n, Nframe);
			continue;
		}
		for (s = 0; s < Nframe; s++)
			if (!trackframe(m, t, s, t, s)) {
				fail("%s: track %d: frame %u", what, t, s);
				break;
			}
		if (imagetrackread(m, t, (uint64_t)Nframe*Raw, frame, Raw) != 0)
			fail("%s: track %d: a frame past its end", what, t);
	}
	if (imageaudio(m, 1) != 0)
		fail("%s: the data track taken for an audio track", what);
}

static Image*
open1(char *path, char *what)
{
	char err[256];
	Image *m;

	m = imageopen(path, err, sizeof err);
	if (m == NULL)
		fail("%s: %s", what, err);
	return m;
}

static int nlisted;

/* The listing of the game's directory: README.TXT alone, in its case, its date the records' as DOS's words. */
static void
listed(void *arg, char *name, int dir, uint32_t size, uint16_t date, uint16_t time)
{
	(void)arg;
	nlisted++;
	if (strcmp(name, "README.TXT") != 0 || dir || size != sizeof readme
	|| date != ((made[0]-80)<<9 | made[1]<<5 | made[2]) || time != (made[3]<<11 | made[4]<<5 | made[5]/2))
		fail("listing %s: %s %d %u %04x %04x", gamedir, name, dir, size, date, time);
}

/* The names _dos_findfirst lists for pattern with attr, a line each with its size, in buf. */
static void
list(const char *pattern, unsigned attr, char *buf, size_t n)
{
	struct find_t ft;
	unsigned r;
	size_t k;

	buf[0] = '\0';
	k = 0;
	for (r = _dos_findfirst(pattern, attr, &ft); r == 0 && k < n; r = _dos_findnext(&ft))
		k += (size_t)snprintf(buf + k, n - k, "%s %lu%s\n", ft.name, (unsigned long)ft.size,
			(ft.attrib & _A_SUBDIR) ? " dir" : (ft.attrib & _A_VOLID) ? " vol" : "");
}

/* Whether _dos_findfirst lists want for pattern with attr. */
static void
lists(const char *pattern, unsigned attr, const char *want)
{
	char buf[1024];

	list(pattern, attr, buf, sizeof buf);
	if (strcmp(buf, want) != 0)
		fail("%s lists \"%s\", want \"%s\"", pattern, buf, want);
}

/* The game's file name read whole through the port into b, n bytes at most: its bytes, -1 where it opens none. */
static long
readall(char *name, uint8_t *b, uint32_t n)
{
	uint32_t k, r;
	int32_t h;

	if ((h = OpenRead(name)) == 0)
		return -1;
	for (k = 0; k < n && (r = Read(h, b + k, n - k)) > 0; k += r)
		;
	Close(h);
	return k;
}

/*
 * The cue sheet of one file bound as the CD, with a directory after it
 * holding a file of its own, read through the port's calls as the game
 * reads them, its tracks as sys/cd.c finds them.
 */
static void
port(char *cue)
{
	char pref[Npath], dir[Npath], path[Npath], *cds[2];
	uint32_t first, frames, f;
	int32_t h, track;
	Image *img;
	uint8_t *b;
	long k;

	join(pref, sizeof pref, top, "pref");
	join(dir, sizeof dir, top, "second");
	makedir(pref);
	makedir(dir);
	join(path, sizeof path, dir, "SECOND.TXT");
	put(path, (uint8_t*)"second", 6);
	join(path, sizeof path, dir, "CD_LBA");
	put(path, (uint8_t*)"the directory's", 15);
	cds[0] = cue;
	cds[1] = dir;
	setdirs(pref, NULL, 0, cds, 2);
	b = big;
	if ((k = readall("D:\\CD_LBA", b, Nbig)) != 22 || memcmp(b, "\"15/08/94 Lba Demo\" \r\n", 22) != 0)
		fail("the port: D:\\CD_LBA: %ld bytes, want the image's 22", k);
	if ((k = readall("d:\\lba\\readme.txt", b, Nbig)) != (long)sizeof readme || memcmp(b, readme, sizeof readme) != 0)
		fail("the port: d:\\lba\\readme.txt: %ld bytes, want README.TXT's %u", k, (unsigned)sizeof readme);
	if ((k = readall("D:\\SECOND.TXT", b, Nbig)) != 6 || memcmp(b, "second", 6) != 0)
		fail("the port: D:\\SECOND.TXT, the second CD's: %ld bytes", k);
	h = OpenRead("D:\\LBA\\README.TXT");
	if (h == 0 || readpath("D:\\LBA\\README.TXT", path, sizeof path) == 0)
		fail("the port: README.TXT, want a file of the image");
	else {
		if (Seek(h, 0, SEEK_END) != (int32_t)sizeof readme || Seek(h, -10, SEEK_END) != 2990
		|| Read(h, b, 100) != 10 || memcmp(b, readme+2990, 10) != 0)
			fail("the port: README.TXT's end, sought");
		if (Seek(h, -1, SEEK_SET) != -1 || Seek(h, -20, SEEK_CUR) != 2980 || Read(h, b, 4) != 4
		|| memcmp(b, readme+2980, 4) != 0)
			fail("the port: README.TXT sought before its start, then back from its end");
	}
	if (h != 0)
		Close(h);
	if ((h = OpenRead("D:\\LBA")) != 0) {
		fail("the port: D:\\LBA, a directory, opens");
		Close(h);
	}
	if (cdtrack("D:\\track02.wav", &img, &track, &first, &frames) != 0 || track != 2 || first != 0 || frames != Nframe)
		fail("the port: D:\\track02.wav, want track 2's %d frames", Nframe);
	else
		for (f = 0; f < frames; f++)
			if (!trackframe(img, track, f, 2, f)) {
				fail("the port: D:\\TRACK02.WAV's frame %u", f);
				break;
			}
	if (cdtrack("D:\\TRACK04.WAV", &img, &track, &first, &frames) == 0
	|| cdtrack("D:\\TRACK01.WAV", &img, &track, &first, &frames) == 0)
		fail("the port: TRACK04.WAV or TRACK01.WAV, the data track, plays");
	lists("D:\\*.*", _A_NORMAL, "CD_LBA 22\nSECOND.TXT 6\n");
	lists("D:\\*.*", _A_SUBDIR, "CD_LBA 22\nLBA 0 dir\nSECOND.TXT 6\n");
	lists("D:\\LBA\\*.TXT", _A_NORMAL, "README.TXT 3000\n");
	setdirs(pref, NULL, 0, cds+1, 1);
	if ((k = readall("D:\\CD_LBA", b, Nbig)) != 15)
		fail("the port: the directory alone, D:\\CD_LBA of %ld bytes, want its 15", k);
}

/*
 * A WAVE file of track 2's Nframe frames into path, its data chunk
 * saying dlen bytes, after a junk chunk's header alone saying jlen bytes
 * where jlen is set, and with cut the samples' last 100 bytes a LIST
 * chunk of its own.
 */
static void
oddwave(char *path, uint32_t dlen, uint32_t jlen, int cut)
{
	uint8_t h[64], raw[Raw];
	FILE *f;
	int s, k, n;

	n = wavhead(h, Nframe, 0) - 8;
	if (jlen != 0) {
		memcpy(h+n, "junk", 4);
		le32(h+n+4, jlen);
		n += 8;
	}
	memcpy(h+n, "data", 4);
	le32(h+n+4, dlen);
	n += 8;
	f = fopen(path, "wb");
	if (f == NULL || fwrite(h, 1, n, f) != (size_t)n)
		fatal("writing %s", path);
	for (s = 0; s < Nframe; s++) {
		for (k = 0; k < Raw; k++)
			raw[k] = sample(2, s, k);
		if (cut && s == Nframe-1) {
			memcpy(raw+Raw-100, "LIST", 4);
			le32(raw+Raw-96, 92);
			memset(raw+Raw-92, 0x55, 92);
		}
		if (fwrite(raw, 1, Raw, f) != Raw)
			fatal("writing %s", path);
	}
	if (fclose(f) != 0)
		fatal("writing %s", path);
}

/*
 * WAVE files whose chunks say more than they hold, after wave.bin's
 * data track: a data chunk past the file's end gives the file's frames,
 * one cut by a chunk after it gives zeros past its samples, and one
 * after a chunk past the file's end is refused.
 */
static void
wavechunks(void)
{
	char path[Npath], cue[Npath], text[Npath], err[256];
	uint8_t b[Raw];
	uint32_t n;
	Image *m;
	int k;

	join(cue, sizeof cue, top, "odd.cue");
	snprintf(text, sizeof text,
		"FILE \"wave.bin\" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n"
		"FILE \"odd.wav\" WAVE\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n");
	put(cue, (uint8_t*)text, strlen(text));
	join(path, sizeof path, top, "odd.wav");
	oddwave(path, 0xffffffff, 0, 0);
	m = open1(cue, "a WAVE file's data chunk past its end");
	if (m != NULL) {
		if ((n = imageaudio(m, 2)) != Nframe)
			fail("a WAVE file's data chunk past its end: %u frames, want %d", n, Nframe);
		else if (!trackframe(m, 2, Nframe-1, 2, Nframe-1))
			fail("a WAVE file's data chunk past its end: its last frame");
		imageclose(m);
	}
	oddwave(path, Nframe*Raw - 100, 0, 1);
	m = open1(cue, "a WAVE file of a chunk after its samples");
	if (m != NULL) {
		if ((n = imageaudio(m, 2)) != Nframe)
			fail("a WAVE file of a chunk after its samples: %u frames, want %d", n, Nframe);
		memset(b, 0xaa, sizeof b);
		imagetrackread(m, 2, (uint64_t)(Nframe-1)*Raw, b, Raw);
		for (k = 0; k < Raw; k++)
			if (b[k] != (k < Raw-100 ? sample(2, Nframe-1, k) : 0)) {
				fail("a WAVE file of a chunk after its samples: byte %d of its last frame %#x", k, b[k]);
				break;
			}
		imageclose(m);
	}
	oddwave(path, Nframe*Raw, 0xfffffff8, 0);
	m = imageopen(cue, err, sizeof err);
	if (m != NULL) {
		fail("a WAVE file of a chunk past its end: opens");
		imageclose(m);
	} else if (strstr(err, "a WAVE file other than") == NULL)
		fail("a WAVE file of a chunk past its end: refused for %s", err);
}

/*
 * The images this test makes: good ones read back, malformed ones
 * refused or failing CD_LBA's lookup.
 */
static void
own(void)
{
	static Case bad[] = {
		{"the volume's signature", 16*Sector+1, 'X', NULL, 0, "no track"},
		{"the volume's version", 16*Sector+6, 2, NULL, 0, "no ISO 9660 volume"},
		{"a root out of the image", 16*Sector+156+2, 200, NULL, 0, "a root directory out of"},
		{"a record too short", Cdrec, 20, NULL, 1, NULL},
		{"a name past its record", Cdrec+32, 120, NULL, 1, NULL},
		{"an extent past the image", Cdrec+2, 250, NULL, 1, NULL},
		{"a size past the image", Cdrec+13, 1, NULL, 1, NULL},
		{"a size past the image's end", Cdrec+11, 0x68, NULL, 1, NULL},
		{"a cue without its file", -1, 0, "TRACK 01 MODE1/2352\n  INDEX 01 00:00:00\n", 0, "a TRACK line out of place"},
		{"a cue naming a missing file", -1, 0, "FILE \"none.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n", 0, "no such file"},
		{"a cue of an AIFF file", -1, 0, "FILE \"disc.bin\" AIFF\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n", 0, "other than BINARY or WAVE"},
		{"a WAVE file of no RIFF", -1, 0, "FILE \"disc.bin\" WAVE\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n", 0, "a WAVE file other than"},
		{"a data track in a WAVE file", -1, 0, "FILE \"wave.bin\" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\nFILE \"wave.bin2\" WAVE\n  TRACK 02 MODE1/2352\n    INDEX 01 00:00:00\n", 0, "a data track in a file of sound"},
		{"a MODE2/2336 track", -1, 0, "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE2/2336\n    INDEX 01 00:00:00\n", 0, "a track of a kind other than"},
		{"a second out of range", -1, 0, "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:61:00\n", 0, "not a time"},
		{"a frame out of range", -1, 0, "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:75\n", 0, "not a time"},
		{"a negative INDEX", -1, 0, "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX -1 00:00:00\n", 0, "an INDEX's number out of range"},
		{"tracks out of order", -1, 0, "FILE \"disc.bin\" BINARY\n  TRACK 02 MODE1/2352\n    INDEX 01 00:00:00\n  TRACK 01 AUDIO\n    INDEX 01 00:00:32\n", 0, "a track's number out of order"},
		{"a track without its index", -1, 0, "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE1/2352\n  TRACK 02 AUDIO\n    INDEX 01 00:00:32\n", 0, "a track without its INDEX 01"},
		{"an index past its file", -1, 0, "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n  TRACK 02 AUDIO\n    INDEX 01 09:00:00\n", 0, "a track without its INDEX 01 within its file"},
		{"two sector sizes in a file", -1, 0, "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n  TRACK 02 AUDIO\n    INDEX 01 00:00:32\n", 0, "two sector sizes"},
		{"audio alone", -1, 0, "FILE \"disc.bin\" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n", 0, "no data track"},
		{"a PREGAP before its track", -1, 0, "FILE \"disc.bin\" BINARY\n  PREGAP 00:00:02\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n", 0, "a PREGAP line out of place"},
		{"a PREGAP after its INDEX 00", -1, 0, "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n  TRACK 02 AUDIO\n    INDEX 00 00:00:30\n    PREGAP 00:00:02\n    INDEX 01 00:00:32\n", 0, "a PREGAP line out of place"},
		{"a PREGAP after its INDEX 01", -1, 0, "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n  TRACK 02 AUDIO\n    INDEX 01 00:00:32\n    PREGAP 00:00:02\n", 0, "a PREGAP line out of place"},
		{"an INDEX 00 after its INDEX 01", -1, 0, "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n  TRACK 02 AUDIO\n    INDEX 01 00:00:32\n    INDEX 00 00:00:33\n", 0, "a track's INDEX 00 out of place"},
		{"an INDEX 01 given twice", -1, 0, "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n  TRACK 02 AUDIO\n    INDEX 01 00:00:32\n    INDEX 01 00:00:33\n", 0, "a track's INDEX 01 given twice"},
		{"an INDEX 00 given twice", -1, 0, "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n  TRACK 02 AUDIO\n    INDEX 00 00:00:30\n    INDEX 00 00:00:31\n    INDEX 01 00:00:32\n", 0, "a track's INDEX 00 given twice"},
		{"an INDEX 01 in the next file", -1, 0, "FILE \"split.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\nFILE \"split.bin2\" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\nFILE \"split.bin3\" BINARY\n    INDEX 01 00:00:00\n", 0, "a track's INDEX 01 given twice"},
		{"an INDEX 00 in a file of its own", -1, 0, "FILE \"split.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\nFILE \"split.bin2\" BINARY\n  TRACK 02 AUDIO\n    INDEX 00 00:00:00\nFILE \"split.bin3\" BINARY\n    INDEX 01 00:00:00\n", 0, "a track's INDEX 00 out of place"},
		{NULL, 0, 0, NULL, 0, NULL},
	};
	char path[Npath], bin[Npath], cue[Npath], text[2*Npath];
	uint32_t s, size;
	uint8_t keep;
	Image *m;
	Case *c;
	int k;

	makeiso();
	join(path, sizeof path, top, "disc.iso");
	put(path, iso, sizeof iso);
	m = open1(path, "the ISO");
	if (m != NULL) {
		check("the ISO", m, 0);
		nlisted = 0;
		if (imagelist(m, gamedir, listed, NULL) != 0 || nlisted != 1)
			fail("listing %s: %d files", gamedir, nlisted);
		imageclose(m);
	}

	join(bin, sizeof bin, top, "disc.bin");
	makebin(bin, 0, 1, 0);
	m = imageopen(bin, text, sizeof text);
	if (m != NULL) {
		fail("disc.bin, raw sectors: taken for an image");
		imageclose(m);
	} else if (strstr(text, "a raw image") == NULL)
		fail("disc.bin, raw sectors: refused for %s, where its reason is a raw image", text);
	join(cue, sizeof cue, top, "disc.cue");
	/* the data track's 32 sectors, then tracks 2 and 3, the second after a pregap of 2 frames, in lower case, with a BOM */
	snprintf(text, sizeof text,
		"\xEF\xBB\xBFREM a test\r\nfile \"DISC.BIN\" binary\r\n  track 01 mode1/2352\r\n    index 01 00:00:00\r\n"
		"  TRACK 02 AUDIO\r\n    PREGAP 00:02:00\r\n    INDEX 01 00:00:%02d\r\n"
		"  TRACK 03 AUDIO\r\n    INDEX 00 00:00:%02d\r\n    INDEX 01 00:00:%02d\r\n",
		Nimage, Nimage+Nframe-2, Nimage+Nframe);
	put(cue, (uint8_t*)text, strlen(text));
	m = open1(cue, "the cue sheet of one file");
	if (m != NULL) {
		check("the cue sheet of one file", m, 1);
		imageclose(m);
	}
	/* the sheet of one file, the file named from the root */
	fromroot(path, sizeof path, bin);
	snprintf(text, sizeof text,
		"FILE \"%s\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
		"  TRACK 02 AUDIO\n    INDEX 01 00:00:%02d\n"
		"  TRACK 03 AUDIO\n    INDEX 01 00:00:%02d\n",
		path, Nimage, Nimage+Nframe);
	put(cue, (uint8_t*)text, strlen(text));
	m = open1(cue, "the cue sheet of one file named from the root");
	if (m != NULL) {
		check("the cue sheet of one file named from the root", m, 1);
		imageclose(m);
	}
	port(cue);
	/* the data track in MODE2/2352 sectors of Form 1, as the PlayStation's disc has it */
	join(bin, sizeof bin, top, "mode2.bin");
	makebin(bin, 0, 2, 0);
	join(cue, sizeof cue, top, "mode2.cue");
	snprintf(text, sizeof text,
		"FILE \"mode2.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n"
		"  TRACK 02 AUDIO\n    INDEX 01 00:00:%02d\n"
		"  TRACK 03 AUDIO\n    INDEX 01 00:00:%02d\n",
		Nimage, Nimage+Nframe);
	put(cue, (uint8_t*)text, strlen(text));
	m = open1(cue, "the cue sheet of a MODE2 data track");
	if (m != NULL) {
		check("the cue sheet of a MODE2 data track", m, 1);
		imageclose(m);
	}
	/*
	 * a sound's samples in a BINARY file, little-endian as BINARY says,
	 * then big-endian as the Internet Archive's rip of LBA2's EU CD holds
	 * them: read little-endian both times, whole frames, and bytes from
	 * an odd byte across a frame's end
	 */
	join(bin, sizeof bin, top, "wave16.bin");
	join(cue, sizeof cue, top, "wave16.cue");
	snprintf(text, sizeof text,
		"FILE \"wave16.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
		"  TRACK 02 AUDIO\n    INDEX 01 00:00:%02d\n",
		Nimage);
	put(cue, (uint8_t*)text, strlen(text));
	for (k = 0; k < 2; k++) {
		makewave(bin, k);
		m = open1(cue, k ? "a sound big-endian" : "a sound little-endian");
		if (m == NULL)
			continue;
		if (!waveread(m, 0, Raw) || !waveread(m, (uint64_t)(Nframe-1)*Raw, Raw) || !waveread(m, 3*Raw - 5, 11))
			fail("a sound %s: its samples, want them little-endian", k ? "big-endian" : "little-endian");
		imageclose(m);
	}
	/* the data track in MODE1/2048 sectors, the audio tracks in WAVE files, the second with a chunk before its samples */
	join(bin, sizeof bin, top, "wave.bin");
	makebin(bin, 1, 0, 1);
	join(cue, sizeof cue, top, "wave.cue");
	snprintf(text, sizeof text,
		"FILE \"wave.bin\" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n"
		"FILE \"wave.bin2\" WAVE\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n"
		"FILE \"wave.bin3\" WAVE\n  TRACK 03 AUDIO\n    INDEX 01 00:00:00\n");
	put(cue, (uint8_t*)text, strlen(text));
	m = open1(cue, "the cue sheet of a MODE1/2048 data track and WAVE files");
	if (m != NULL) {
		check("the cue sheet of a MODE1/2048 data track and WAVE files", m, 1);
		imageclose(m);
	}
	wavechunks();

	join(bin, sizeof bin, top, "split.bin");
	makebin(bin, 1, 1, 0);
	join(cue, sizeof cue, top, "split.cue");
	snprintf(text, sizeof text,
		"FILE \"split.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
		"FILE \"split.bin2\" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n"
		"FILE \"split.bin3\" BINARY\n  TRACK 03 AUDIO\n    INDEX 01 00:00:00\n");
	put(cue, (uint8_t*)text, strlen(text));
	m = open1(cue, "the cue sheet of a file a track");
	if (m != NULL) {
		check("the cue sheet of a file a track", m, 1);
		imageclose(m);
	}
	/* track 3's file opening on a pregap of 2 frames, which track 2 runs on through */
	snprintf(text, sizeof text,
		"FILE \"split.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
		"FILE \"split.bin2\" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n"
		"FILE \"split.bin3\" BINARY\n  TRACK 03 AUDIO\n    INDEX 00 00:00:00\n    INDEX 01 00:00:02\n");
	put(cue, (uint8_t*)text, strlen(text));
	m = open1(cue, "the cue sheet of a file a track, a pregap stored");
	if (m != NULL) {
		if (imageaudio(m, 2) != Nframe+2 || imageaudio(m, 3) != Nframe-2)
			fail("a pregap stored: tracks of %u and %u frames, want %d and %d",
				imageaudio(m, 2), imageaudio(m, 3), Nframe+2, Nframe-2);
		else if (!trackframe(m, 2, Nframe-1, 2, Nframe-1) || !trackframe(m, 2, Nframe, 3, 0) || !trackframe(m, 2, Nframe+1, 3, 1))
			fail("a pregap stored: track 2's last 3 frames, want its own last, then track 3's file's first 2");
		imageclose(m);
	}
	/*
	 * the same with a PREGAP of 3 frames before track 3's INDEX 00, a
	 * frame into its file: track 2 runs on through that frame, the
	 * silence, then the frame after
	 */
	snprintf(text, sizeof text,
		"FILE \"split.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
		"FILE \"split.bin2\" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n"
		"FILE \"split.bin3\" BINARY\n  TRACK 03 AUDIO\n    PREGAP 00:00:03\n    INDEX 00 00:00:01\n    INDEX 01 00:00:02\n");
	put(cue, (uint8_t*)text, strlen(text));
	m = open1(cue, "the cue sheet of a file a track, a PREGAP in its pregap");
	if (m != NULL) {
		if (imageaudio(m, 2) != Nframe+5)
			fail("a PREGAP in a pregap: track 2 of %u frames, want %d", imageaudio(m, 2), Nframe+5);
		else if (!trackframe(m, 2, Nframe, 3, 0) || !trackframe(m, 2, Nframe+1, 0, 0) || !trackframe(m, 2, Nframe+3, 0, 0)
		|| !trackframe(m, 2, Nframe+4, 3, 1))
			fail("a PREGAP in a pregap: track 2's last 5 frames, want track 3's file's first, 3 of silence, its second");
		imageclose(m);
	}
	/*
	 * each pregap at the end of the file before, as Exact Audio Copy
	 * appends it: track 2's 2 sectors in the data track's file, track
	 * 3's 2 frames in track 2's, which runs on through them
	 */
	snprintf(text, sizeof text,
		"FILE \"split.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
		"  TRACK 02 AUDIO\n    INDEX 00 00:00:%02d\n"
		"FILE \"split.bin2\" BINARY\n    INDEX 01 00:00:00\n"
		"  TRACK 03 AUDIO\n    INDEX 00 00:00:%02d\n"
		"FILE \"split.bin3\" BINARY\n    INDEX 01 00:00:00\n",
		Nimage-2, Nframe-2);
	put(cue, (uint8_t*)text, strlen(text));
	m = open1(cue, "the cue sheet of a file a track, the pregaps appended");
	if (m != NULL) {
		check("the cue sheet of a file a track, the pregaps appended", m, 1);
		imageclose(m);
	}
	/*
	 * track 3 after a PREGAP of 5 frames of silence, in the sheet of
	 * one file, before its INDEX 00 and then before its INDEX 01 alone:
	 * track 2 runs on through it, the silence where the disc has it
	 */
	join(cue, sizeof cue, top, "disc.cue");
	snprintf(text, sizeof text,
		"FILE \"disc.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
		"  TRACK 02 AUDIO\n    INDEX 01 00:00:%02d\n"
		"  TRACK 03 AUDIO\n    PREGAP 00:00:05\n    INDEX 00 00:00:%02d\n    INDEX 01 00:00:%02d\n",
		Nimage, Nimage+Nframe-2, Nimage+Nframe);
	put(cue, (uint8_t*)text, strlen(text));
	m = open1(cue, "the cue sheet of one file, a PREGAP");
	if (m != NULL) {
		if (imageaudio(m, 2) != Nframe+5 || imageaudio(m, 3) != Nframe)
			fail("a PREGAP: tracks of %u and %u frames, want %d and %d", imageaudio(m, 2), imageaudio(m, 3), Nframe+5, Nframe);
		else if (!trackframe(m, 2, Nframe-3, 2, Nframe-3) || !trackframe(m, 2, Nframe-2, 0, 0) || !trackframe(m, 2, Nframe+2, 0, 0)
		|| !trackframe(m, 2, Nframe+3, 2, Nframe-2))
			fail("a PREGAP: track 2's frames about its silence");
		imageclose(m);
	}
	snprintf(text, sizeof text,
		"FILE \"disc.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
		"  TRACK 02 AUDIO\n    INDEX 01 00:00:%02d\n"
		"  TRACK 03 AUDIO\n    PREGAP 00:00:05\n    INDEX 01 00:00:%02d\n",
		Nimage, Nimage+Nframe);
	put(cue, (uint8_t*)text, strlen(text));
	m = open1(cue, "the cue sheet of one file, a PREGAP before INDEX 01");
	if (m != NULL) {
		if (imageaudio(m, 2) != Nframe+5)
			fail("a PREGAP before INDEX 01: track 2 of %u frames, want %d", imageaudio(m, 2), Nframe+5);
		else if (!trackframe(m, 2, Nframe-1, 2, Nframe-1) || !trackframe(m, 2, Nframe, 0, 0) || !trackframe(m, 2, Nframe+4, 0, 0))
			fail("a PREGAP before INDEX 01: track 2's frames about its silence");
		imageclose(m);
	}

	join(path, sizeof path, top, "bad.iso");
	join(cue, sizeof cue, top, "bad.cue");
	for (c = bad; c->name != NULL; c++) {
		if (c->cue != NULL) {
			put(cue, (uint8_t*)c->cue, strlen(c->cue));
			m = imageopen(cue, text, sizeof text);
		} else {
			keep = iso[c->at];
			iso[c->at] = c->byte;
			put(path, iso, sizeof iso);
			iso[c->at] = keep;
			m = imageopen(path, text, sizeof text);
		}
		if (c->lookup && m == NULL)
			fail("%s: refused: %s", c->name, text);
		else if (c->lookup && imagefile(m, "CD_LBA", &s, &size) == 0)
			fail("%s: CD_LBA found through it", c->name);
		else if (!c->lookup && m != NULL)
			fail("%s: taken for an image", c->name);
		else if (!c->lookup && strstr(text, c->why) == NULL)
			fail("%s: refused for %s, where its reason is %s", c->name, text, c->why);
		if (m != NULL)
			imageclose(m);
	}
}

/* Whether path names a directory. */
static int
isdir(char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* The file at path's bytes against the image's file at s of size bytes. */
static void
same(Image *m, char *rel, uint32_t s, uint32_t size, char *path)
{
	uint32_t pos, k;
	size_t got;
	FILE *f;

	f = fopen(path, "rb");
	if (f == NULL) {
		fail("%s: no %s", rel, path);
		return;
	}
	for (pos = 0;; pos += k) {
		got = fread(big, 1, Nbig, f);
		k = size - pos < Nbig ? size - pos : Nbig;
		if (got != k) {
			fail("%s: %u bytes in the image, %s holds another count", rel, size, path);
			break;
		}
		if (k == 0)
			break;
		if (imageread(m, s, pos, bigto, k) != k || memcmp(big, bigto, k) != 0) {
			fail("%s: the bytes from %u differ from %s's", rel, pos, path);
			break;
		}
	}
	fclose(f);
}

typedef struct Walk Walk;

/* A walk of the image's directories against the tree. */
struct Walk
{
	Image	*m;
	char	*tree;
	char	dir[Npath];	/* the image's directory being listed, "" for its root */
	int	files;
};

static void
walked(void *arg, char *name, int dir, uint32_t size, uint16_t date, uint16_t time)
{
	char rel[Npath], path[Npath], dot[Npath+1], *base;
	uint32_t s, n;
	Walk *w;

	(void)size;
	(void)date;
	(void)time;
	w = arg;
	if (w->dir[0] == '\0')
		snprintf(rel, sizeof rel, "%s", name);
	else
		join(rel, sizeof rel, w->dir, name);
	if (dir)
		return;
	if (imagefile(w->m, rel, &s, &n) != 0) {
		fail("%s: listed, then out of the image", rel);
		return;
	}
	w->files++;
	base = strrchr(rel, '/');
	base = base != NULL ? base+1 : rel;
	if (findhost(w->tree, rel, path, sizeof path) == 0 || findhost(w->tree, base, path, sizeof path) == 0) {
		same(w->m, rel, s, n, path);
		return;
	}
	/*
	 * A tree copied by a tool may keep ISO 9660's last dot of a name
	 * without extension, CD_LBA., which Windows takes off a path: there
	 * the file stays unread, its bytes checked on the other systems.
	 */
	snprintf(dot, sizeof dot, "%s.", base);
	if (findhost(w->tree, dot, path, sizeof path) != 0)
		fail("%s: in the image, out of %s", rel, w->tree);
	else if (!windows)
		same(w->m, rel, s, n, path);
}

/* The files of the CD's directories, its root, LBA, LBA/FLA and LBA/VOX, each checked. */
static void
walkdir(Walk *w)
{
	static char *subs[] = {"LBA", "LBA/FLA", "LBA/VOX", NULL};
	char **p;

	if (imagelist(w->m, "", walked, w) != 0)
		fail("the root: no listing");
	for (p = subs; *p != NULL; p++) {
		snprintf(w->dir, sizeof w->dir, "%s", *p);
		imagelist(w->m, *p, walked, w);
	}
	w->dir[0] = '\0';
}

/* The files of tree, a host's directory, and of its directories, counted. */
static int
count(char *tree)
{
	char path[Npath];
	struct dirent *e;
	DIR *d;
	int n;

	d = opendir(tree);
	if (d == NULL)
		return 0;
	n = 0;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.')
			continue;
		join(path, sizeof path, tree, e->d_name);
		n += isdir(path) ? count(path) : 1;
	}
	closedir(d);
	return n;
}

/* The WAV file at path, its samples after a header of 44 bytes, against the image's sound name. */
static void
sound(Image *m, char *name, char *path)
{
	uint64_t at;
	uint32_t first, n, want, k, left;
	int32_t num;
	FILE *g;

	g = fopen(path, "rb");
	if (g == NULL || fseek(g, 0, SEEK_END) != 0) {
		fail("%s: unread", path);
		if (g != NULL)
			fclose(g);
		return;
	}
	want = (ftell(g) - 44) / Raw;
	n = 0;
	if (imagesound(m, name, &num, &first, &n) != 0 || n != want || fseek(g, 44, SEEK_SET) != 0) {
		fail("%s: %u frames, %s %u", name, n, path, want);
		fclose(g);
		return;
	}
	at = (uint64_t)first * Raw;
	for (left = n*Raw; left > 0; left -= k, at += k) {
		k = left < Nbig ? left : Nbig;
		if (fread(big, 1, k, g) != k)
			break;
		if (imagetrackread(m, num, at, bigto, k) != k || memcmp(big, bigto, k) != 0) {
			fail("%s: the samples %u frames from its end differ from %s's", name, left/Raw, path);
			break;
		}
	}
	fclose(g);
}

/*
 * The sounds of dir, each trackNN.wav, against the image's at path of
 * their names, as the players name them: the count of them, an audio
 * track wanting one.
 */
static int
tracks(Image *m, char *path, char *dir)
{
	char file[Npath], name[Npath];
	int t, n;

	n = 0;
	for (t = 1; t <= 99; t++) {
		snprintf(name, sizeof name, "track%02d.wav", t);
		if (findhost(dir, name, file, sizeof file) == 0) {
			sound(m, name, file);
			n++;
		}
	}
	for (t = 1; t <= 99 && imageaudio(m, t) == 0; t++)
		;
	if (t <= 99 && n == 0)
		fail("%s: audio tracks, and no sound of them in %s", path, dir);
	return n;
}

/* An image named, against its tree and its tracks ("-" for none). */
static void
image(char *path, char *tree, char *dir)
{
	Walk w;
	Image *m;
	int n, k;

	m = open1(path, path);
	if (m == NULL)
		return;
	w.m = m;
	w.tree = tree;
	w.dir[0] = '\0';
	w.files = 0;
	walkdir(&w);
	n = count(tree);
	if (w.files != n)
		fail("%s: %d files, %s %d", path, w.files, tree, n);
	k = strcmp(dir, "-") != 0 ? tracks(m, path, dir) : 0;
	imageclose(m);
	printf("cdimagetest: %s: %d files, %d sounds\n", path, w.files, k);
}

int
main(int argc, char **argv)
{
	int i;

	if (argc < 2 || (argc-2) % 3 != 0) {
		fprintf(stderr, "usage: cdimagetest dir [image tree tracks]...\n");
		return 2;
	}
	top = argv[1];
	makedir(top);
	own();
	for (i = 2; i < argc; i += 3)
		image(argv[i], argv[i+1], argv[i+2]);
	if (fails > 0)
		return 1;
	printf("cdimagetest: ok\n");
	return 0;
}
