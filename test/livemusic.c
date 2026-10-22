#define _GNU_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL3/SDL.h>

/*
 * livemusic: test/livemusic's preload, the device of the music's stream
 * in play.
 *
 *	LIVEMUSIC_RATE=hz LIVEMUSIC_STALLS=s:ms,... LIVEMUSIC_END=s
 *	LIVEMUSIC_LOG=file LIVEMUSIC_DATA=file LD_PRELOAD=livemusic.so lba ...
 *
 * The stream the port opens at hz, the music's, it opens unbound, and a
 * thread of its own pulls Pull frames from it a period at a time, on
 * SDL's clock, the port's, as a device on a clock of its own would (one
 * of SDL's drivers waits whole milliseconds and lags as it mixes). What
 * a pull lacks plays as silence. It answers SDL_GetAudioDeviceFormat for
 * that stream's device with its rate and its pull. It keeps in memory,
 * as they come, each put (its bytes, the bytes queued before it), each
 * query of the bytes queued and each pull (the bytes it lacked, the
 * bytes it asked for). It stalls the thread that opened the stream in
 * SDL_DelayNS, the port's one sleep, or in SDL_RenderPresent, where the
 * port waits for the display's vsync with DisplaySync, at the first of
 * them past each of the stalls' times (seconds after the open), for
 * their milliseconds, and raises SIGTERM at the first of them past the
 * end, which ends the port at its next pump. At exit, or at SDL_Quit if
 * it comes first, it writes the bytes put to one file and to the log a
 * line a record, times in ns from the open:
 *
 *	open 0 rate devicerate deviceframes
 *	put t bytes queued
 *	query t queued
 *	pull t lacked asked
 *	stall at length start end
 *	end t
 */

#define USED(x)	((void)(x))

enum
{
	Pull = 1024,	/* frames a pull, a common device's */
	Nlog = 1<<17,	/* records of each kind */
	Ndata = 64<<20,	/* bytes put */
	Nstall = 16,
};

typedef struct Rec Rec;
typedef struct Stall Stall;

struct Rec
{
	uint64_t	t;	/* ns since the open */
	int	a, b;
};

struct Stall
{
	uint64_t	at;	/* ns since the open */
	uint64_t	ns;
	uint64_t	start, end;	/* when it was made, 0 before */
};

static SDL_AudioStream *(SDLCALL *realopen)(SDL_AudioDeviceID, const SDL_AudioSpec*, SDL_AudioStreamCallback, void*);
static bool (SDLCALL *realput)(SDL_AudioStream*, const void*, int);
static int (SDLCALL *realqueued)(SDL_AudioStream*);
static void (SDLCALL *realdelay)(Uint64);
static bool (SDLCALL *realpresent)(SDL_Renderer*);
static bool (SDLCALL *realformat)(SDL_AudioDeviceID, SDL_AudioSpec*, int*);
static void (SDLCALL *realquit)(void);

static SDL_AudioStream *music;
static SDL_ThreadID opener;
static uint64_t t0;
static int rate;
static Rec pullrec[Nlog], putrec[Nlog], queryrec[Nlog];
static int npull, nput, nquery;
static uint8_t *data;
static size_t ndata;
static uint8_t pullbuf[4*Pull];
static Stall stalls[Nstall];
static int nstall;
static uint64_t endat;
static uint64_t ended;

/* name's next definition, SDL's own, into the function pointer at fn. */
static void
next(void *fn, size_t n, char *name)
{
	void *p;

	p = dlsym(RTLD_NEXT, name);
	if (p == NULL || n != sizeof p) {
		fprintf(stderr, "livemusic: no %s\n", name);
		_Exit(1);
	}
	memcpy(fn, &p, n);
}

static void
init(void)
{
	if (realopen != NULL)
		return;
	next(&realopen, sizeof realopen, "SDL_OpenAudioDeviceStream");
	next(&realput, sizeof realput, "SDL_PutAudioStreamData");
	next(&realqueued, sizeof realqueued, "SDL_GetAudioStreamQueued");
	next(&realdelay, sizeof realdelay, "SDL_DelayNS");
	next(&realpresent, sizeof realpresent, "SDL_RenderPresent");
	next(&realformat, sizeof realformat, "SDL_GetAudioDeviceFormat");
	next(&realquit, sizeof realquit, "SDL_Quit");
}

static uint64_t
since(void)
{
	return SDL_GetTicksNS() - t0;
}

static void
add(Rec *r, int *n, uint64_t t, int a, int b)
{
	if (*n >= Nlog)
		return;
	r[*n].t = t;
	r[*n].a = a;
	r[*n].b = b;
	(*n)++;
}

/* The device: Pull frames pulled at each period's end, from the open on. */
static int SDLCALL
device(void *arg)
{
	uint64_t k, at, now;
	int got;

	USED(arg);
	for (k = 1;; k++) {
		at = t0 + k*Pull*(uint64_t)1000000000/(uint64_t)rate;
		now = SDL_GetTicksNS();
		if (at > now)
			realdelay(at - now);
		SDL_LockAudioStream(music);
		got = SDL_GetAudioStreamData(music, pullbuf, sizeof pullbuf);
		if (got < 0)
			got = 0;
		add(pullrec, &npull, since(), (int)sizeof pullbuf - got, (int)sizeof pullbuf);
		SDL_UnlockAudioStream(music);
	}
	return 0;
}

/*
 * The log and the bytes put, written out once, while SDL still runs,
 * the port's alarm after its SIGTERM put off.
 */
static void
dump(void)
{
	static int done;
	char *p;
	FILE *f;
	int i;

	if (music == NULL || done)
		return;
	done = 1;
	alarm(0);
	SDL_LockAudioStream(music);
	p = getenv("LIVEMUSIC_LOG");
	if (p != NULL && (f = fopen(p, "w")) != NULL) {
		fprintf(f, "open 0 %d %d %d\n", rate, rate, Pull);
		for (i = 0; i < nput; i++)
			fprintf(f, "put %llu %d %d\n", (unsigned long long)putrec[i].t, putrec[i].a, putrec[i].b);
		for (i = 0; i < nquery; i++)
			fprintf(f, "query %llu %d\n", (unsigned long long)queryrec[i].t, queryrec[i].a);
		for (i = 0; i < npull; i++)
			fprintf(f, "pull %llu %d %d\n", (unsigned long long)pullrec[i].t, pullrec[i].a, pullrec[i].b);
		for (i = 0; i < nstall; i++)
			fprintf(f, "stall %llu %llu %llu %llu\n", (unsigned long long)stalls[i].at,
				(unsigned long long)stalls[i].ns, (unsigned long long)stalls[i].start,
				(unsigned long long)stalls[i].end);
		fprintf(f, "end %llu\n", (unsigned long long)ended);
		fclose(f);
	}
	p = getenv("LIVEMUSIC_DATA");
	if (p != NULL && (f = fopen(p, "wb")) != NULL) {
		fwrite(data, 1, ndata, f);
		fclose(f);
	}
	SDL_UnlockAudioStream(music);
}

/* The stalls of LIVEMUSIC_STALLS, "s:ms,...", and the end. */
static void
setup(void)
{
	char *p, *e;
	double s, ms;

	p = getenv("LIVEMUSIC_STALLS");
	while (p != NULL && *p != '\0' && nstall < Nstall) {
		s = strtod(p, &e);
		if (*e != ':')
			break;
		ms = strtod(e+1, &e);
		stalls[nstall].at = (uint64_t)(s*1e9);
		stalls[nstall].ns = (uint64_t)(ms*1e6);
		nstall++;
		p = *e == ',' ? e+1 : e;
	}
	p = getenv("LIVEMUSIC_END");
	endat = p != NULL ? (uint64_t)(strtod(p, NULL)*1e9) : 0;
}

SDL_AudioStream* SDLCALL
SDL_OpenAudioDeviceStream(SDL_AudioDeviceID dev, const SDL_AudioSpec *spec, SDL_AudioStreamCallback cb, void *arg)
{
	char *p;

	init();
	p = getenv("LIVEMUSIC_RATE");
	if (music != NULL || cb != NULL || p == NULL || spec == NULL || spec->freq != atoi(p))
		return realopen(dev, spec, cb, arg);
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO) || (music = SDL_CreateAudioStream(spec, spec)) == NULL
	|| (data = malloc(Ndata)) == NULL) {
		fprintf(stderr, "livemusic: no stream: %s\n", SDL_GetError());
		_Exit(1);
	}
	t0 = SDL_GetTicksNS();
	opener = SDL_GetCurrentThreadID();
	rate = spec->freq;
	setup();
	atexit(dump);
	if (SDL_CreateThread(device, "livemusic", NULL) == NULL) {
		fprintf(stderr, "livemusic: no thread: %s\n", SDL_GetError());
		_Exit(1);
	}
	return music;
}

bool SDLCALL
SDL_GetAudioDeviceFormat(SDL_AudioDeviceID dev, SDL_AudioSpec *spec, int *frames)
{
	init();
	if (music == NULL || dev != 0)
		return realformat(dev, spec, frames);
	spec->format = SDL_AUDIO_S16LE;
	spec->channels = 2;
	spec->freq = rate;
	if (frames != NULL)
		*frames = Pull;
	return true;
}

bool SDLCALL
SDL_PutAudioStreamData(SDL_AudioStream *s, const void *buf, int len)
{
	init();
	if (s == music && music != NULL) {
		add(putrec, &nput, since(), len, realqueued(s));
		if (len > 0 && ndata + (size_t)len <= Ndata) {
			memcpy(data + ndata, buf, (size_t)len);
			ndata += (size_t)len;
		}
	}
	return realput(s, buf, len);
}

int SDLCALL
SDL_GetAudioStreamQueued(SDL_AudioStream *s)
{
	int n;

	init();
	n = realqueued(s);
	if (s == music && music != NULL)
		add(queryrec, &nquery, since(), n, 0);
	return n;
}

/* The stalls due, and the end, at a sleep or a present of the thread that opened the stream. */
static void
stall(void)
{
	uint64_t t;
	int i;

	if (music == NULL || SDL_GetCurrentThreadID() != opener)
		return;
	t = since();
	for (i = 0; i < nstall; i++)
		if (stalls[i].start == 0 && t >= stalls[i].at) {
			stalls[i].start = t;
			realdelay(stalls[i].ns);
			stalls[i].end = since();
			t = stalls[i].end;
		}
	if (endat != 0 && ended == 0 && t >= endat) {
		ended = t;
		raise(SIGTERM);
	}
}

void SDLCALL
SDL_DelayNS(Uint64 ns)
{
	init();
	stall();
	realdelay(ns);
}

bool SDLCALL
SDL_RenderPresent(SDL_Renderer *r)
{
	init();
	stall();
	return realpresent(r);
}

void SDLCALL
SDL_Quit(void)
{
	init();
	dump();
	realquit();
}
