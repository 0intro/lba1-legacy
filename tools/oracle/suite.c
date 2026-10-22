#define _GNU_SOURCE
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "dat.h"
#include "fns.h"
#include "dll.h"
#include "suite.h"

/*
 * The scenarios of the driver oracle (fmoracle.c). A scenario runs
 * twice, in two processes forked from the oracle, once on the DLL
 * (indll) and once on its decompilation, each streaming what it does:
 * the I/O the oracle sees (emitio) and, at the end of each step (a
 * procedure called, an interval served), its result and the CRC of the
 * driver's writable data (the oracle's statecrc). The oracle compares the two streams item
 * by item and reports the first that differs. A run whose step takes
 * more than 30 seconds (a decompilation, or a mutant of it, that loops)
 * is ended. Blocks of the DLL can each carry an INT3, their runs
 * counted in memory both processes share.
 *
 * The XMIDI sequences the sequencer's scenario plays are made here: a
 * file of every event the sequencer reads, XMIDI's controllers among
 * them, and one of branch points.
 */

enum
{
	Iio = 1,
	Istep,
	Iend,

	Nstates = 10,
	Statemax = 4096,
	Nring = 24,
	Watchdog = 30,	/* seconds a step may take: past it, a run that loops is ended */
	Seqtable = 283,	/* sequence_state in the sequencer's variables */
	Seqstate = 536,	/* a state_table */
};

typedef struct Item Item;
typedef struct Stepitem Stepitem;
typedef struct Buf Buf;

/* the I/O: a, b, c as the oracle gives them */
struct Item
{
	uint32_t	kind;
	uint32_t	a;
	uint32_t	b;
	uint32_t	c;
};

/* a step's end: a its number, b its result, c the data's CRC */
struct Stepitem
{
	Item	h;
	char	label[40];
};

struct Buf
{
	uint8_t	*p;
	int	n;
	int	max;
};

int indll;
Synth *synthc;
uint32_t seed;

static uint32_t crctab[256];
static FILE *out;
static uint32_t nstep;
static uint32_t *blocks;
static char **blockfn;
static int *hits;
static int nblocks;
static uint8_t *files[Nfiles];
static uint8_t ctrl[128];
static uint8_t states[Nstates][Statemax];
static uint8_t junk[64] = "RIFF\0\0\0\x10WAVEfmt ";

void
crcinit(void)
{
	uint32_t c;
	int i, k;

	for (i = 0; i < 256; i++) {
		c = i;
		for (k = 0; k < 8; k++)
			c = c & 1 ? 0xedb88320 ^ c>>1 : c>>1;
		crctab[i] = c;
	}
}

uint32_t
crc(uint32_t c, const uint8_t *p, size_t n)
{
	c = ~c;
	while (n-- > 0)
		c = crctab[(c ^ *p++) & 0xff] ^ c>>8;
	return ~c;
}

uint32_t
rnd(uint32_t n)
{
	seed = seed*1103515245 + 12345;
	return (seed >> 8) % n;
}

static void
emit(void *p, size_t n)
{
	if (fwrite(p, 1, n, out) != n)
		_exit(3);
}

/* emitio: an I/O item, as the oracle makes it. */
void
emitio(uint32_t a, uint32_t b, uint32_t c)
{
	Item it;

	it.kind = Iio;
	it.a = a;
	it.b = b;
	it.c = c;
	emit(&it, sizeof it);
}

/* step: a step's end, its result first, then the label's format and arguments. */
void
step(char *fmt, ...)
{
	Stepitem s;
	va_list arg;

	memset(&s, 0, sizeof s);
	va_start(arg, fmt);
	s.h.b = va_arg(arg, uint32_t);
	vsnprintf(s.label, sizeof s.label, fmt, arg);
	va_end(arg);
	s.h.kind = Istep;
	s.h.a = nstep++;
	alarm(Watchdog);
	s.h.c = statecrc();
	emit(&s, sizeof s);
}

/* A run of the scenario on the DLL or on the C, streamed to fd. */
static void
run(int dll, int fd, Scenario *sc)
{
	Item it;

	indll = dll;
	out = fdopen(fd, "w");
	if (out == NULL)
		_exit(3);
	setvbuf(out, NULL, _IOFBF, 1<<20);
	runinit();
	if (dll && nblocks > 0 && sc->traps)
		dllcover(blocks, nblocks, hits);
	alarm(Watchdog);
	sc->fn();
	memset(&it, 0, sizeof it);
	it.kind = Iend;
	emit(&it, sizeof it);
	fflush(out);
	exit(0);
}

/* The next item of a stream, or 0 at its end. */
static int
next(FILE *f, Stepitem *s)
{
	memset(s, 0, sizeof *s);
	if (fread(&s->h, sizeof s->h, 1, f) != 1)
		return 0;
	if (s->h.kind == Istep && fread((uint8_t*)s + sizeof s->h, sizeof *s - sizeof s->h, 1, f) != 1)
		return 0;
	return 1;
}

static void
show(char *who, Stepitem *s)
{
	if (s->h.kind == Iio)
		printf("\t%s: %08x %08x %08x\n", who, s->h.a, s->h.b, s->h.c);
	else if (s->h.kind == Istep)
		printf("\t%s: step %u \"%s\": result %08x, data %08x\n", who,
			s->h.a, s->label, s->h.b, s->h.c);
	else
		printf("\t%s: the end\n", who);
}

/* scenario: the scenario run on both: 0 if they are the same throughout. */
int
scenario(char *who, Scenario *sc)
{
	static Stepitem ring[Nring];
	int pa[2], pb[2], st, bad, ra, rb, nring, k;
	pid_t da, db;
	FILE *fa, *fb;
	Stepitem a, b;
	char last[40];
	uint64_t nio, nsteps;

	if (pipe(pa) < 0 || pipe(pb) < 0)
		fatal("pipe");
	fflush(stdout);
	da = fork();
	if (da == 0) {
		close(pa[0]);
		close(pb[0]);
		close(pb[1]);
		run(1, pa[1], sc);
	}
	db = fork();
	if (db == 0) {
		close(pb[0]);
		close(pa[0]);
		close(pa[1]);
		run(0, pb[1], sc);
	}
	if (da < 0 || db < 0)
		fatal("fork");
	close(pa[1]);
	close(pb[1]);
	fa = fdopen(pa[0], "r");
	fb = fdopen(pb[0], "r");
	if (fa == NULL || fb == NULL)
		fatal("fdopen");
	setvbuf(fa, NULL, _IOFBF, 1<<20);
	setvbuf(fb, NULL, _IOFBF, 1<<20);
	snprintf(last, sizeof last, "the start");
	nio = nsteps = 0;
	bad = 0;
	nring = 0;
	for (;;) {
		ra = next(fa, &a);
		rb = next(fb, &b);
		ring[nring++ % Nring] = a;
		if (!ra || !rb) {
			printf("%s: %s: %s's run ended early, after \"%s\"\n", who, sc->name, !ra ? "the DLL" : "the C", last);
			bad = 1;
			break;
		}
		if (memcmp(&a, &b, sizeof a) != 0) {
			printf("%s: %s: after \"%s\", they differ:\n", who, sc->name, last);
			for (k = nring > Nring ? nring - Nring : 0; k < nring - 1; k++)
				show("both", &ring[k % Nring]);
			show("DLL", &a);
			show("C", &b);
			bad = 1;
			break;
		}
		if (a.h.kind == Iend)
			break;
		if (a.h.kind == Istep) {
			snprintf(last, sizeof last, "%s", a.label);
			nsteps++;
		} else
			nio++;
	}
	if (bad && ra && rb) {
		kill(da, SIGKILL);
		kill(db, SIGKILL);
	}
	fclose(fa);
	fclose(fb);
	waitpid(da, &st, 0);
	if (WIFSIGNALED(st) && WTERMSIG(st) != SIGKILL)
		printf("%s: %s: the DLL's run died of signal %d\n", who, sc->name, WTERMSIG(st));
	if (!bad && (!WIFEXITED(st) || WEXITSTATUS(st) != 0)) {
		printf("%s: %s: the DLL's run failed\n", who, sc->name);
		bad = 1;
	}
	waitpid(db, &st, 0);
	if (WIFSIGNALED(st) && WTERMSIG(st) != SIGKILL)
		printf("%s: %s: the C's run died of signal %d\n", who, sc->name, WTERMSIG(st));
	if (!bad && (!WIFEXITED(st) || WEXITSTATUS(st) != 0)) {
		printf("%s: %s: the C's run failed\n", who, sc->name);
		bad = 1;
	}
	if (!bad)
		printf("%s: %s: %llu steps, %llu accesses, the same\n", who, sc->name,
			(unsigned long long)nsteps, (unsigned long long)nio);
	return bad;
}

/*
 * AIL/32's procedure n with its arguments, the handle 0 first: on the
 * DLL through its index, on the C the function that decompiles it.
 */
uint32_t
api(int n, int na, ...)
{
	uint32_t a[8], x[9];
	va_list arg;
	int i;

	va_start(arg, na);
	for (i = 0; i < na; i++)
		a[i] = va_arg(arg, uint32_t);
	va_end(arg);
	if (indll) {
		x[0] = 0;
		memcpy(x + 1, a, na * sizeof a[0]);
		return dllcall(dllproc(n), na + 1, x);
	}
	switch (n) {
	case 100: return (uint32_t)(uintptr_t)synthc->desc;
	case 101: return synthc->detect(a[0], a[1], a[2], a[3]);
	case 102: initdriver(synthc, a[0], a[1], a[2], a[3]); return 0;
	case 103: servedriver(); return 0;
	case 104: shutdowndriver((char*)(uintptr_t)a[0]); return 0;
	case 150: return statesize();
	case 151: return registerseq((uint8_t*)(uintptr_t)a[0], a[1], (void*)(uintptr_t)a[2], (uint8_t*)(uintptr_t)a[3]);
	case 152: releaseseqhandle(a[0]); return 0;
	case 153: return synthc->cachesize();
	case 154: synthc->definecache((void*)(uintptr_t)a[0], a[1]); return 0;
	case 155: return synthc->request(a[0]);
	case 156: synthc->install(a[0], a[1], (uint8_t*)(uintptr_t)a[2]); return 0;
	case 157: synthc->protect(a[0], a[1]); return 0;
	case 158: synthc->unprotect(a[0], a[1]); return 0;
	case 159: return synthc->status(a[0], a[1]);
	case 170: startseq(a[0]); return 0;
	case 171: stopseq(a[0]); return 0;
	case 173: resumeseq(a[0]); return 0;
	case 174: return seqstatus(a[0]);
	case 175: return relvolume(a[0]);
	case 176: return reltempo(a[0]);
	case 177: setrelvolume(a[0], a[1], a[2]); return 0;
	case 178: setreltempo(a[0], a[1], a[2]); return 0;
	case 179: return beatcount(a[0]);
	case 180: return barcount(a[0]);
	case 181: branchindex(a[0], a[1]); return 0;
	case 182: return controlval(a[0], a[1], a[2]);
	case 183: setcontrolval(a[0], a[1], a[2], a[3]); return 0;
	case 185: return channotes(a[0], a[1]);
	case 186: return synthc->cvmsg(a[0], a[1], a[2]);
	case 187: synthc->sysexmsg(a[0], a[1], a[2], a[3], (uint8_t*)(uintptr_t)a[4], a[5]); return 0;
	case 188: synthc->display((char*)(uintptr_t)a[0]); return 0;
	case 189: installcallback((void (*)(int32_t, int32_t))(uintptr_t)a[0]); return 0;
	case 190: cancelcallback(); return 0;
	case 191: return lockchannel();
	case 192: mapseqchannel(a[0], a[1], a[2]); return 0;
	case 193: releasechannel(a[0]); return 0;
	case 194: return trueseqchannel(a[0], a[1]);
	}
	fatal("no procedure %d in the decompilation", n);
	return 0;
}

/*
 * seqcrc: c continued over the state of each sequence the sequencer's
 * variables (seq, xmidistate's bytes or the DLL's) list, the DLL's
 * state_table bytes or the C's laid out alike (xmidiseqstate).
 */
uint32_t
seqcrc(uint32_t c, uint8_t *seq)
{
	uint8_t b[Seqstate];
	uint32_t a;
	int i;

	for (i = 0; i < 8; i++) {
		a = get32(seq + Seqtable + 4*i);
		if (a == 0)
			continue;
		if (indll)
			c = crc(c, (uint8_t*)(uintptr_t)a, Seqstate);
		else {
			xmidiseqstate(4*i, b);
			c = crc(c, b, Seqstate);
		}
	}
	return c;
}

/* readblocks: the blocks of Ghidra's list (function, entry, start, end, flow, targets), their counts shared. */
/* Each of the n names asked is a scenario's among the ns at s, or the oracle ends. */
void
asked(char *oracle, char **names, int n, Scenario *s, int ns)
{
	int i, k;

	for (k = 0; k < n; k++) {
		for (i = 0; i < ns; i++)
			if (strcmp(names[k], s[i].name) == 0)
				break;
		if (i == ns) {
			fprintf(stderr, "%s: no scenario %s\n", oracle, names[k]);
			exit(2);
		}
	}
}

void
readblocks(char *path)
{
	FILE *f;
	char line[512], fn[128];
	uint32_t entry, start, *b;
	int i, n, max;

	f = fopen(path, "r");
	if (f == NULL)
		fatal("%s: cannot open", path);
	n = max = 0;
	b = NULL;
	blockfn = NULL;
	while (fgets(line, sizeof line, f) != NULL) {
		if (sscanf(line, "%127s %x %x", fn, &entry, &start) != 3)
			continue;
		if (n == max) {
			max = max ? 2*max : 1024;
			b = realloc(b, max * sizeof b[0]);
			blockfn = realloc(blockfn, max * sizeof blockfn[0]);
			if (b == NULL || blockfn == NULL)
				fatal("out of memory");
		}
		b[n] = start;
		blockfn[n] = strdup(fn);
		n++;
	}
	fclose(f);
	for (i = 1; i < n; i++)
		if (b[i] <= b[i-1])
			fatal("%s: blocks out of order at %08x", path, b[i]);
	blocks = b;
	nblocks = n;
	hits = mmap(NULL, n * sizeof hits[0], PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	if (hits == MAP_FAILED)
		fatal("cannot share the blocks' counts");
}

/* coverblocks: the blocks' traps set in this process. */
void
coverblocks(void)
{
	if (nblocks > 0)
		dllcover(blocks, nblocks, hits);
}

/* coverage: the blocks left at 0 runs, by function. */
void
coverage(char *who)
{
	char *fn;
	int i, n, k;

	n = 0;
	for (i = 0; i < nblocks; i++)
		if (hits[i])
			n++;
	printf("%s: %d of %d blocks reached\n", who, n, nblocks);
	fn = NULL;
	k = 0;
	for (i = 0; i < nblocks; i++) {
		if (hits[i])
			continue;
		if (fn == NULL || strcmp(fn, blockfn[i]) != 0) {
			if (fn != NULL)
				printf("\n");
			fn = blockfn[i];
			printf("\t%s:", fn);
			k = 0;
		}
		if (k > 0 && k % 8 == 0)
			printf("\n\t\t");
		printf(" %05x", blocks[i]);
		k++;
	}
	if (fn != NULL)
		printf("\n");
}

/* The XMIDI writer. */
static void
put(Buf *b, uint32_t c)
{
	if (b->n == b->max) {
		b->max = b->max ? 2*b->max : 4096;
		b->p = realloc(b->p, b->max);
		if (b->p == NULL)
			fatal("out of memory");
	}
	b->p[b->n++] = c;
}

static void
puts4(Buf *b, char *s)
{
	int i;

	for (i = 0; i < 4; i++)
		put(b, s[i]);
}

static void
putle(Buf *b, uint32_t v, int n)
{
	int i;

	for (i = 0; i < n; i++)
		put(b, v >> 8*i & 0xff);
}

/* An IFF chunk opened: its id, its length to come (big-endian). */
static int
chunk(Buf *b, char *id)
{
	int at;

	at = b->n;
	puts4(b, id);
	putle(b, 0, 4);
	return at;
}

static void
endchunk(Buf *b, int at)
{
	uint32_t n;

	n = b->n - at - 8;
	b->p[at+4] = n >> 24;
	b->p[at+5] = n >> 16;
	b->p[at+6] = n >> 8;
	b->p[at+7] = n;
}

/* A variable-length number, the highest 7 bits first. */
static void
putvln(Buf *b, uint32_t v)
{
	uint8_t t[5];
	int n;

	n = 0;
	t[n++] = v & 0x7f;
	while ((v >>= 7) != 0)
		t[n++] = 0x80 | (v & 0x7f);
	while (n > 0)
		put(b, t[--n]);
}

static void
ev3(Buf *b, uint32_t s, uint32_t d1, uint32_t d2)
{
	put(b, s);
	put(b, d1);
	put(b, d2);
}

static void
delay(Buf *b, uint32_t n)
{
	while (n > 0x7f) {
		put(b, 0x7f);
		n -= 0x7f;
	}
	if (n > 0)
		put(b, n);
}

static void
note(Buf *b, uint32_t ch, uint32_t key, uint32_t vel, uint32_t dur)
{
	ev3(b, 0x90|ch, key, vel);
	putvln(b, dur);
}

static void
ctl(Buf *b, uint32_t ch, uint32_t c, uint32_t v)
{
	ev3(b, 0xb0|ch, c, v);
}

static void
meta(Buf *b, uint32_t type, uint8_t *d, uint32_t n)
{
	uint32_t i;

	put(b, 0xff);
	put(b, type);
	putvln(b, n);
	for (i = 0; i < n; i++)
		put(b, d[i]);
}

/* Every event the sequencer reads, XMIDI's controllers in each of their ways. */
static void
features(Buf *b)
{
	static uint8_t sig34[] = {3, 2, 24, 8}, sig68[] = {6, 3, 24, 8}, sig21[] = {2, 1, 24, 8}, sig44[] = {4, 0, 24, 8};
	static uint8_t tempo1[] = {0x07, 0xa1, 0x20}, tempo2[] = {0x03, 0xd0, 0x90}, text[] = "a text";
	static uint8_t longtext[200];
	uint32_t i, ch;

	meta(b, 0x58, sig34, 4);
	meta(b, 0x51, tempo1, 3);
	/* held to the end: a pedal, a lock, a protection, a voice protection */
	ctl(b, 11, 64, 0x7f);
	ctl(b, 12, 110, 0x7f);
	ctl(b, 13, 111, 0x7f);
	ctl(b, 14, 112, 0x7f);
	note(b, 12, 0x3c, 0x50, 600);
	for (ch = 0; ch < 16; ch++) {
		ev3(b, 0xc0|ch, 0x10*ch & 0x7f, 0);
		b->n--;
		ctl(b, ch, 7, 100);
		ctl(b, ch, 10, 0x40 + ch);
		ctl(b, ch, 11, 0x7f);
	}
	for (ch = 1; ch < 10; ch++)
		note(b, ch, 0x30 + 3*ch, 0x50 + ch, 40 + ch);
	delay(b, 10);
	ev3(b, 0xe1, 0x00, 0x50);
	put(b, 0xd1);
	put(b, 0x40);
	ev3(b, 0xa1, 0x33, 0x20);
	ctl(b, 1, 1, 0x40);
	ctl(b, 1, 64, 0x7f);
	ctl(b, 1, 64, 0);
	put(b, 0xf0);
	putvln(b, 4);
	for (i = 0; i < 4; i++)
		put(b, 0x41 + i);
	put(b, 0xf0);
	put(b, 0x84);
	put(b, 0x80);
	put(b, 0x05);
	for (i = 0; i < 5; i++)
		put(b, 0x10 + i);
	put(b, 0xf7);
	putvln(b, 2);
	put(b, 0x7f);
	put(b, 0xf7);
	meta(b, 0x01, text, sizeof text - 1);
	meta(b, 0x7f, longtext, sizeof longtext);
	delay(b, 3);
	meta(b, 0x58, sig68, 4);
	delay(b, 200);
	meta(b, 0x58, sig21, 4);
	meta(b, 0x51, tempo2, 3);
	delay(b, 100);
	meta(b, 0x58, sig44, 4);
	ctl(b, 1, 118, 0);
	ctl(b, 1, 119, 5);
	ctl(b, 2, 119, 0x7f);
	delay(b, 2);
	/* FOR loops, nested, one too many, a NEXT of 0x3f falling through, stray NEXTs */
	ctl(b, 1, 116, 2);
	note(b, 1, 0x3c, 0x60, 2);
	ctl(b, 1, 116, 3);
	note(b, 2, 0x40, 0x60, 1);
	delay(b, 2);
	ctl(b, 1, 116, 2);
	ctl(b, 1, 116, 2);
	ctl(b, 1, 116, 9);
	note(b, 3, 0x43, 0x60, 1);
	delay(b, 1);
	ctl(b, 1, 117, 0x7f);
	ctl(b, 1, 117, 0x7f);
	ctl(b, 1, 117, 0x3f);
	ctl(b, 1, 117, 0x40);
	ctl(b, 1, 117, 0x7f);
	ctl(b, 1, 117, 0x7f);
	ctl(b, 1, 117, 0x7f);
	ctl(b, 1, 117, 0x7f);
	/* protection, locks, indirection, the rest */
	ctl(b, 3, 111, 0x7f);
	note(b, 3, 0x48, 0x70, 30);
	ctl(b, 3, 111, 0);
	ctl(b, 4, 110, 0x7f);
	note(b, 4, 0x4a, 0x70, 20);
	ctl(b, 4, 7, 90);
	delay(b, 4);
	ctl(b, 4, 110, 0);
	ctl(b, 5, 110, 0x7f);
	ctl(b, 6, 110, 0x7f);
	ctl(b, 7, 110, 0x7f);
	ctl(b, 8, 110, 0x7f);
	ctl(b, 9, 110, 0x7f);
	ctl(b, 2, 110, 0x7f);
	ctl(b, 1, 110, 0x7f);
	ctl(b, 10, 110, 0x7f);
	note(b, 5, 0x30, 0x60, 4);
	delay(b, 5);
	for (ch = 1; ch < 11; ch++)
		ctl(b, ch, 110, 0);
	ctl(b, 5, 115, 2);
	ctl(b, 5, 7, 0x11);
	ctl(b, 5, 115, 0x7f);
	ctl(b, 5, 10, 0x22);
	ctl(b, 6, 112, 0x7f);
	ctl(b, 6, 113, 0x7f);
	ctl(b, 6, 114, 3);
	ctl(b, 6, 120, 1);
	ctl(b, 6, 112, 0);
	ctl(b, 6, 113, 0);
	ctl(b, 6, 114, 0);
	ctl(b, 1, 121, 0);
	ctl(b, 2, 123, 0);
	/* lengths of 0, 1 and 2, and many notes queued, short of the last entry */
	for (i = 0; i < 16; i++)
		note(b, 1 + i % 9, 0x24 + i, 0x40 + i, i < 3 ? i : 30 + 4*i);
	delay(b, 20);
	for (i = 0; i < 8; i++)
		note(b, 1 + i % 9, 0x24 + i, 0x40, 1);
	delay(b, 300);
	put(b, 0xff);
	put(b, 0x2f);
	put(b, 0);
}

/*
 * More notes than the queue's 32: the first entry taken again, the
 * last's notes ended as rewind_seq's code is changed.
 */
static void
overflow(Buf *b)
{
	uint32_t i;

	for (i = 0; i < 40; i++)
		note(b, 1 + i % 9, 0x24 + i, 0x40 + i, 20 + 4*i);
	delay(b, 3);
	note(b, 2, 0x60, 0x40, 1);
	delay(b, 200);
	put(b, 0xff);
	put(b, 0x2f);
	put(b, 0);
}

/* Events between branch points, a FOR loop of count 0, for ever, last. */
static void
branches(Buf *b, int *marks)
{
	uint32_t i;

	marks[0] = b->n;
	for (i = 0; i < 4; i++) {
		note(b, 2, 0x3c + i, 0x60, 6);
		delay(b, 3);
	}
	marks[1] = b->n;
	ctl(b, 2, 116, 0);
	note(b, 3, 0x43, 0x60, 2);
	delay(b, 4);
	marks[2] = b->n;
	note(b, 4, 0x48, 0x60, 3);
	ctl(b, 3, 117, 0x7f);
	put(b, 0xff);
	put(b, 0x2f);
	put(b, 0);
}

/*
 * Held: on seven channels a program, a bend, a volume and the pedal
 * down, two locks and the protections, long notes, and a tempo whose
 * beat is sixteen intervals exactly, the beat cleared after some.
 */
static void
held(Buf *b)
{
	static uint8_t sig34[] = {3, 2, 24, 8}, tempo[] = {0x02, 0x08, 0xd5};
	uint32_t ch;

	meta(b, 0x58, sig34, 4);
	meta(b, 0x51, tempo, 3);
	for (ch = 1; ch < 8; ch++) {
		put(b, 0xc0|ch);
		put(b, 5*ch);
		ev3(b, 0xe0|ch, 0x10*ch, 0x30 + ch);
		ctl(b, ch, 7, 90 + ch);
		ctl(b, ch, 64, 0x7f);
	}
	ctl(b, 2, 110, 0x7f);
	ctl(b, 3, 111, 0x7f);
	ctl(b, 4, 112, 0x7f);
	ctl(b, 5, 110, 0x7f);
	for (ch = 1; ch < 8; ch++)
		note(b, ch, 0x30 + ch, 0x60, 200);
	delay(b, 30);
	ctl(b, 1, 118, 0);
	delay(b, 1000);
	put(b, 0xff);
	put(b, 0x2f);
	put(b, 0);
}

/* Signatures of a whole note, a half and past 32 bits, notes between, and a tempo of 255 us. */
static void
signatures(Buf *b)
{
	static uint8_t s1[] = {3, 0, 24, 8}, s2[] = {5, 1, 24, 8}, s3[] = {2, 40, 24, 8}, s4[] = {7, 3, 24, 8};
	static uint8_t fast[] = {0, 0, 0xff};

	meta(b, 0x58, s1, 4);
	note(b, 2, 0x40, 0x60, 10);
	delay(b, 40);
	meta(b, 0x58, s2, 4);
	note(b, 3, 0x42, 0x60, 10);
	delay(b, 40);
	meta(b, 0x58, s3, 4);
	delay(b, 20);
	meta(b, 0x58, s4, 4);
	note(b, 4, 0x44, 0x60, 10);
	delay(b, 40);
	meta(b, 0x51, fast, 3);
	delay(b, 10);
	put(b, 0xff);
	put(b, 0x2f);
	put(b, 0);
}

/*
 * The corners. On channel 1 a program, a bend, a modulation and a
 * volume. On 2 to 5 bends with a byte of 0xff or 0xfe, on 6 and 7
 * programs 0xff and 0xfe, on 8 and 9 the pedal at 0xff and 0xfe. The
 * pressures. A note of status 0x80, channel 1's as the sequencer takes
 * it. A system exclusive of 130 bytes. A long note and a longer one,
 * later a bend and a program on 12, and a long wait.
 */
static void
corner(Buf *b)
{
	uint32_t i;

	put(b, 0xc0);
	put(b, 9);
	ev3(b, 0xe0, 0x20, 0x41);
	ctl(b, 0, 1, 0x30);
	ctl(b, 0, 7, 0x50);
	ev3(b, 0xe1, 0xff, 0x40);
	ev3(b, 0xe2, 0x10, 0xff);
	ev3(b, 0xe3, 0xfe, 0x40);
	ev3(b, 0xe4, 0x10, 0xfe);
	put(b, 0xc5);
	put(b, 0xff);
	put(b, 0xc6);
	put(b, 0xfe);
	ctl(b, 7, 64, 0xff);
	ctl(b, 8, 64, 0xfe);
	ev3(b, 0xa3, 0x40, 0x20);
	put(b, 0xd3);
	put(b, 0x30);
	ev3(b, 0x80, 0x3c, 0x50);
	putvln(b, 6);
	put(b, 0xf0);
	putvln(b, 130);
	for (i = 0; i < 130; i++)
		put(b, i & 0x7f);
	note(b, 2, 0x40, 0x60, 40);
	note(b, 3, 0x43, 0x60, 300);
	delay(b, 30);
	ev3(b, 0xeb, 0x00, 0x50);
	put(b, 0xcb);
	put(b, 0x22);
	delay(b, 2000);
	put(b, 0xff);
	put(b, 0x2f);
	put(b, 0);
}

/* Two long notes, and the end before they are over. */
static void
ends(Buf *b)
{
	note(b, 2, 0x40, 0x60, 60);
	note(b, 3, 0x44, 0x60, 60);
	delay(b, 4);
	put(b, 0xff);
	put(b, 0x2f);
	put(b, 0);
}

/* Channel 10 locked, two notes on it, the second longer. */
static void
lockplay(Buf *b)
{
	ctl(b, 9, 110, 0x7f);
	note(b, 9, 0x40, 0x60, 10);
	note(b, 9, 0x43, 0x60, 40);
	delay(b, 80);
	put(b, 0xff);
	put(b, 0x2f);
	put(b, 0);
}

/* A callback first, then a note. */
static void
trigger(Buf *b)
{
	ctl(b, 1, 119, 0x5a);
	note(b, 2, 0x40, 0x60, 5);
	delay(b, 10);
	put(b, 0xff);
	put(b, 0x2f);
	put(b, 0);
}

/* A few notes from key, and the end. */
static void
few(Buf *b, uint32_t key)
{
	note(b, 2, key, 0x60, 4);
	delay(b, 2);
	note(b, 3, key + 4, 0x60, 2);
	delay(b, 6);
	put(b, 0xff);
	put(b, 0x2f);
	put(b, 0);
}

/* A FORM of type t (XMID or a name near it), its EVNT a few notes from key. */
static void
small(Buf *b, char *t, uint32_t key)
{
	int f, c;

	f = chunk(b, "FORM");
	puts4(b, t);
	c = chunk(b, "EVNT");
	few(b, key);
	endchunk(b, c);
	endchunk(b, f);
}

/*
 * A FORM XMID whose chunks have neighbours named near them: a TIMB then
 * a TIMX, an RBRN then an RBRX (each a timbre or a branch point of its
 * own), an EVNX of waits before the EVNT.
 */
static void
near(Buf *b)
{
	static char *names[] = {"TIMB", "TIMX", "RBRN", "RBRX"};
	int f, c, i;

	f = chunk(b, "FORM");
	puts4(b, "XMID");
	for (i = 0; i < 4; i++) {
		c = chunk(b, names[i]);
		putle(b, 1, 2);
		if (i < 2) {
			put(b, 0x30 + 0x10*i);
			put(b, 0);
		} else {
			putle(b, 3, 2);
			putle(b, 4 + 8*(i - 2), 4);
		}
		endchunk(b, c);
	}
	c = chunk(b, "EVNX");
	putle(b, 0x7f7f7f7f, 4);
	endchunk(b, c);
	c = chunk(b, "EVNT");
	few(b, 0x50);
	endchunk(b, c);
	endchunk(b, f);
}

/*
 * The big one, past 16 MB in each chunk and its padding of 0x55 bytes:
 * a CAT XMID of one FORM XMID, its TIMB of 258 timbres (one 256 times,
 * then two), its RBRN of 258 branch points (branch_index matches a
 * point's id by its low byte: the last's alone is Bigfar's, its offset
 * past 16 MB in the EVNT, and Bignear's is in the RBRN's padding, past
 * its count), its EVNT a few notes, a long wait, and notes again past
 * 16 MB.
 */
enum
{
	Bigtarget = 0x01012345,
	Bigpad = 0x01010100,
	Bignear = 0x7fd,
	Bigfar = 0x6fe,
};

static void
big(Buf *b)
{
	uint32_t i;
	int cat, f, c;

	cat = chunk(b, "CAT ");
	puts4(b, "XMID");
	f = chunk(b, "FORM");
	puts4(b, "XMID");
	c = chunk(b, "TIMB");
	putle(b, 258, 2);
	for (i = 0; i < 258; i++) {
		put(b, i < 256 ? 0x10 : 0x10*(i - 254));
		put(b, 0);
	}
	while (b->n - c - 8 < Bigpad)
		put(b, 0x55);
	endchunk(b, c);
	c = chunk(b, "RBRN");
	putle(b, 258, 2);
	for (i = 0; i < 258; i++) {
		putle(b, i == 257 ? Bigfar : 2 + i%250, 2);
		putle(b, i == 257 ? Bigtarget : 2*i, 4);
	}
	/* padding to 16 MB, but an entry past the count, the 300th */
	while (b->n - c - 8 < 2 + 300*6)
		put(b, 0x55);
	putle(b, Bignear, 2);
	putle(b, 4, 4);
	while (b->n - c - 8 < Bigpad)
		put(b, 0x55);
	endchunk(b, c);
	c = chunk(b, "EVNT");
	note(b, 2, 0x3c, 0x60, 6);
	delay(b, 3);
	note(b, 3, 0x40, 0x60, 2);
	while (b->n - c - 8 < Bigtarget)
		put(b, 0x7f);
	note(b, 4, 0x45, 0x60, 3);
	delay(b, 2);
	note(b, 5, 0x48, 0x60, 2);
	delay(b, 4);
	put(b, 0xff);
	put(b, 0x2f);
	put(b, 0);
	endchunk(b, c);
	endchunk(b, f);
	endchunk(b, cat);
}

/* A FORM XMID: a TIMB chunk if asked, RBRN if branch points, EVNT of features, branches or the overflow. */
static void
form(Buf *b, int timb, int rbrn)
{
	static uint8_t ids[] = {1, 2, 5};
	Buf e;
	int f, c, marks[3];
	uint32_t i;

	f = chunk(b, "FORM");
	puts4(b, "XMID");
	if (timb) {
		c = chunk(b, "TIMB");
		putle(b, 4, 2);
		for (i = 0; i < 4; i++) {
			put(b, 0x10*i);
			put(b, 0);
		}
		endchunk(b, c);
	}
	memset(&e, 0, sizeof e);
	if (rbrn == 1)
		branches(&e, marks);
	else if (rbrn == 2)
		overflow(&e);
	else
		features(&e);
	if (rbrn == 1) {
		c = chunk(b, "RBRN");
		putle(b, 3, 2);
		for (i = 0; i < 3; i++) {
			putle(b, ids[i], 2);
			putle(b, marks[i], 4);
		}
		endchunk(b, c);
	}
	c = chunk(b, "EVNT");
	for (i = 0; i < (uint32_t)e.n; i++)
		put(b, e.p[i]);
	endchunk(b, c);
	endchunk(b, f);
	free(e.p);
}

/*
 * xmidi: the XMIDI files, made once, before any run, so that both see
 * them at the same addresses:
 *
 *	0	a CAT XMID of two sequences, the features with their timbres
 *		then the branches, after an XDIR form
 *	1	a lone FORM XMID of the features
 *	2	of the overflow
 *	3	the held controllers
 *	4	the time signatures
 *	5, 6	a CATX and a FORX holding a sequence
 *	7	a FORM XMIX before a FORM XMID
 *	8	the same in a CAT
 *	9	chunks named near XMIDI's
 *	10	a CAT of two sequences and a third after it
 *	11	the big one
 *	12	file 10 with the CAT's length a byte longer, over the
 *		third's first
 *	13	the corners
 *	14	notes past the end
 *	15	notes on a locked channel
 *	16	a callback first
 */
uint8_t*
xmidi(int n)
{
	static void (*evnts[])(Buf*) = {[3] = held, [4] = signatures, [13] = corner, [14] = ends, [15] = lockplay, [16] = trigger};
	Buf b;
	int f, c;

	if (files[n] != NULL)
		return files[n];
	memset(&b, 0, sizeof b);
	if (n == 0) {
		f = chunk(&b, "FORM");
		puts4(&b, "XDIR");
		c = chunk(&b, "INFO");
		putle(&b, 2, 2);
		endchunk(&b, c);
		endchunk(&b, f);
		c = chunk(&b, "CAT ");
		puts4(&b, "XMID");
		form(&b, 1, 0);
		form(&b, 0, 1);
		endchunk(&b, c);
	} else if (n <= 2)
		form(&b, 0, n == 2 ? 2 : 0);
	else if (n <= 4 || n >= 13) {
		f = chunk(&b, "FORM");
		puts4(&b, "XMID");
		c = chunk(&b, "EVNT");
		evnts[n](&b);
		endchunk(&b, c);
		endchunk(&b, f);
	} else if (n <= 6) {
		c = chunk(&b, n == 5 ? "CATX" : "FORX");
		puts4(&b, "XMID");
		small(&b, "XMID", 0x30);
		endchunk(&b, c);
	} else if (n == 7) {
		small(&b, "XMIX", 0x30);
		small(&b, "XMID", 0x40);
	} else if (n == 8) {
		c = chunk(&b, "CAT ");
		puts4(&b, "XMID");
		small(&b, "XMIX", 0x30);
		small(&b, "XMID", 0x40);
		endchunk(&b, c);
	} else if (n == 9)
		near(&b);
	else if (n == 10 || n == 12) {
		c = chunk(&b, "CAT ");
		puts4(&b, "XMID");
		small(&b, "XMID", 0x30);
		small(&b, "XMID", 0x40);
		endchunk(&b, c);
		if (n == 12)
			b.p[c+7]++;
		small(&b, "XMID", 0x50);
	} else
		big(&b);
	files[n] = b.p;
	return b.p;
}

/* xmidictrl: the table the indirect controller prefix takes its values from. */
uint8_t*
xmidictrl(void)
{
	int i;

	for (i = 0; i < 128; i++)
		ctrl[i] = (i*37 + 11) & 0x7f;
	return ctrl;
}

/* The callback: a step, and the service called again from within it. */
static void
callback(int32_t h, int32_t v)
{
	step("callback %d %d", 0, h, v);
	api(103, 0);
	step("service within", 0);
}

/* lib_midi's PlayMidi's loop: the timbres the sequence asks for, installed while there are. */
static void
timbres(int32_t h)
{
	uint32_t t;
	uint8_t *p;

	for (;;) {
		t = api(155, 1, h);
		step("request", t);
		if ((t & 0xffff) == 0xffff)
			break;
		p = timbreof(t >> 8 & 0xff, t & 0xff);
		if (p == NULL)
			break;
		api(156, 3, t >> 8 & 0xff, t & 0xff, (uint32_t)(uintptr_t)p);
		step("install %x", 0, t & 0xffff);
	}
}

/* The calls of a sequence's state AIL/32's API gives, each a step. */
static void
query(int32_t h)
{
	uint32_t ch;

	step("status %d", api(174, 1, h), h);
	step("beat %d", api(179, 1, h), h);
	step("bar %d", api(180, 1, h), h);
	step("relvolume %d", api(175, 1, h), h);
	step("reltempo %d", api(176, 1, h), h);
	for (ch = 1; ch <= 16; ch += 3) {
		step("notes %d %u", api(185, 2, h, ch), h, ch);
		step("control %d %u 7", api(182, 3, h, ch, 7), h, ch);
		step("control %d %u 10", api(182, 3, h, ch, 10), h, ch);
		step("control %d %u 119", api(182, 3, h, ch, 119), h, ch);
		step("control %d %u 2", api(182, 3, h, ch, 2), h, ch);
		step("truechannel %d %u", api(194, 2, h, ch), h, ch);
	}
}

/* n intervals served, the sequence's state queried every k. */
static void
intervals(int n, int32_t h, int k)
{
	int i;

	for (i = 0; i < n; i++) {
		interval();
		step("interval", 0);
		if (i % k == 0 && h != -1)
			query(h);
	}
}

/*
 * sequencer: XMIDI sequences and every procedure of AIL/32's API the
 * driver has, on handles good, freed and bad.
 */
void
sequencer(void)
{
	int32_t h[Nstates], hb;
	uint32_t i, c, ctl;
	int k;

	initmidi();
	ctl = (uint32_t)(uintptr_t)xmidictrl();
	api(189, 1, (uint32_t)(uintptr_t)callback);
	step("callback installed", 0);
	for (k = 0; k < Nstates; k++)
		h[k] = -1;
	h[0] = api(151, 4, (uint32_t)(uintptr_t)xmidi(0), 0, (uint32_t)(uintptr_t)states[0], ctl);
	step("register 0", h[0]);
	timbres(h[0]);
	hb = api(151, 4, (uint32_t)(uintptr_t)xmidi(0), 1, (uint32_t)(uintptr_t)states[1], ctl);
	step("register 1", hb);
	step("register 2", api(151, 4, (uint32_t)(uintptr_t)xmidi(0), 2, (uint32_t)(uintptr_t)states[2], ctl));
	h[2] = api(151, 4, (uint32_t)(uintptr_t)xmidi(1), 0, (uint32_t)(uintptr_t)states[2], ctl);
	step("register lone", h[2]);
	step("register lone 1", api(151, 4, (uint32_t)(uintptr_t)xmidi(1), 1, (uint32_t)(uintptr_t)states[3], ctl));
	step("register no XMIDI", api(151, 4, (uint32_t)(uintptr_t)junk, 0, (uint32_t)(uintptr_t)states[3], ctl));
	api(173, 1, h[2]);
	step("resume, never started", 0);
	for (k = 3; k < Nstates; k++) {
		h[k] = api(151, 4, (uint32_t)(uintptr_t)xmidi(0), 0, (uint32_t)(uintptr_t)states[k], ctl);
		step("register %d", h[k], k);
	}
	for (k = 3; k < Nstates; k++)
		if (h[k] != -1) {
			api(152, 1, h[k]);
			step("release %d", 0, k);
		}
	query(h[0]);
	api(170, 1, h[0]);
	step("start", 0);
	api(173, 1, h[0]);
	step("resume, playing", 0);
	intervals(40, h[0], 16);
	api(178, 3, h[0], 150, 0);
	step("tempo 150", 0);
	intervals(30, h[0], 16);
	api(178, 3, h[0], 60, 1000);
	step("tempo 60 in 1 s", 0);
	intervals(150, h[0], 7);
	api(178, 3, h[0], 60, 1000);
	api(178, 3, h[0], 100, 300);
	step("tempo 100 in .3 s", 0);
	intervals(60, h[0], 5);
	api(178, 3, h[0], 140, 1);
	step("tempo 140 in 1 ms", 0);
	intervals(4, h[0], 1);
	api(178, 3, h[0], 60, 1);
	step("tempo 60 in 1 ms", 0);
	intervals(4, h[0], 1);
	api(178, 3, h[0], 100, 0);
	api(177, 3, h[0], 40, 0);
	step("volume 40", 0);
	intervals(20, h[0], 16);
	api(177, 3, h[0], 127, 500);
	api(177, 3, h[0], 127, 500);
	step("volume 127 in .5 s", 0);
	intervals(80, h[0], 16);
	api(177, 3, h[0], 20, 300);
	step("volume 20 in .3 s", 0);
	intervals(50, h[0], 16);
	api(177, 3, h[0], 20, 300);
	step("volume 20 in .3 s, at 20", 0);
	intervals(50, h[0], 16);
	api(177, 3, h[0], 120, 5);
	step("volume 120 in 5 ms", 0);
	intervals(4, h[0], 1);
	api(171, 1, h[0]);
	step("stop", 0);
	intervals(5, h[0], 16);
	api(173, 1, h[0]);
	step("resume", 0);
	api(171, 1, h[0]);
	for (i = 0; i < 9; i++)
		step("lock", api(191, 0));
	api(173, 1, h[0]);
	step("resume, every channel locked", 0);
	for (i = 1; i <= 16; i++)
		api(193, 1, i);
	step("channels released", 0);
	api(192, 3, h[0], 2, 6);
	step("map 2 6", 0);
	api(192, 3, h[0], 3, 3);
	intervals(40, h[0], 16);
	api(170, 1, h[0]);
	step("start again", 0);
	intervals(30, h[0], 16);
	for (i = 0; i < 10; i++) {
		c = api(191, 0);
		step("lock", c);
	}
	intervals(10, h[0], 16);
	for (i = 1; i <= 16; i++) {
		api(193, 1, i);
		step("release channel %u", 0, i);
	}
	for (c = 0; c < 128; c += 9) {
		api(183, 4, h[0], 2, c, 0x40);
		step("set control %u", 0, c);
	}
	api(183, 4, h[0], 3, 115, 9);
	api(183, 4, h[0], 3, 7, 0x40);
	step("set control indirect", 0);
	api(177, 3, h[0], 120, 0);
	api(183, 4, h[0], 3, 7, 0x7f);
	step("set volume 127 at 120 percent", 0);
	api(170, 1, h[2]);
	step("start lone", 0);
	intervals(260, h[2], 1);
	api(152, 1, h[2]);
	step("release lone, playing", 0);
	intervals(600, h[0], 16);
	api(170, 1, hb);
	step("start branches", 0);
	intervals(20, hb, 16);
	api(181, 2, hb, 2);
	step("branch 2", 0);
	intervals(20, hb, 16);
	api(181, 2, hb, 5);
	step("branch 5", 0);
	intervals(10, hb, 16);
	api(181, 2, hb, 1);
	api(181, 2, hb, 9);
	api(181, 2, h[0], 1);
	step("branches", 0);
	intervals(30, hb, 16);
	api(190, 0);
	step("callback cancelled", 0);
	api(170, 1, h[0]);
	intervals(60, h[0], 16);
	/* send_cv_msg's EAX is dropped: AIL_send_channel_voice_message is void */
	for (i = 0x80; i < 0xf0; i += 0x10) {
		api(186, 3, i|2, 0x40, 0x40);
		step("cv %x", 0, i);
		api(186, 3, i|2, 0x40, 0);
		step("cv %x", 0, i);
	}
	api(187, 6, 0, 0, 0, 3, (uint32_t)(uintptr_t)"\x41\x10\x42", 0);
	step("sysex", 0);
	api(188, 1, (uint32_t)(uintptr_t)"lba");
	step("display", 0);
	step("cache size", api(153, 0));
	api(154, 2, (uint32_t)(uintptr_t)states[Nstates-1], Statemax);
	step("define cache", 0);
	timbres(h[0]);
	api(156, 3, 0, 0x10, (uint32_t)(uintptr_t)timbreof(0, 0x10));
	step("install timbre", 0);
	for (i = 0; i < 4; i++) {
		api(157, 2, 0, 0x10*i);
		step("protect %u", 0, 0x10*i);
		step("timbre status %u", api(159, 2, 0, 0x10*i), 0x10*i);
		api(158, 2, 0, 0x10*i);
		step("unprotect %u", 0, 0x10*i);
		step("timbre status %u", api(159, 2, 0, 0x10*i), 0x10*i);
	}
	step("timbre status 1 1", api(159, 2, 1, 1));
	step("request, no TIMB", api(155, 1, hb));
	step("request, bad handle", api(155, 1, -1));
	api(156, 3, 0x7e, 0x7e, 0);
	step("install, no address", 0);
	query(-1);
	api(170, 1, -1);
	api(152, 1, -1);
	api(171, 1, -1);
	api(173, 1, -1);
	api(177, 3, -1, 10, 0);
	api(178, 3, -1, 10, 0);
	api(181, 2, -1, 1);
	api(183, 4, -1, 1, 7, 1);
	api(192, 3, -1, 1, 2);
	step("bad handle", 0);
	intervals(20, h[0], 16);
	api(152, 1, h[0]);
	step("release first", 0);
	c = api(191, 0);
	step("lock", c);
	api(193, 1, c);
	step("release channel", 0);
	api(104, 1, (uint32_t)(uintptr_t)"");
	step("shutdown", 0);
	intervals(4, -1, 1);
	api(104, 1, (uint32_t)(uintptr_t)"");
	step("shutdown again", 0);
}

/* A file registered as sequence num, its state queried, played some intervals, branched if b, let go. */
static void
trial(char *what, uint8_t *x, uint32_t num, uint32_t b)
{
	int32_t g;

	g = api(151, 4, (uint32_t)(uintptr_t)x, num, (uint32_t)(uintptr_t)states[8], 0);
	step("register %s", g, what);
	if (g == -1)
		return;
	timbres(g);
	query(g);
	api(170, 1, g);
	step("start %s", 0, what);
	intervals(6, g, 1);
	if (b != 0) {
		api(181, 2, g, b);
		step("branch %x", 0, b);
		intervals(6, g, 1);
	}
	api(171, 1, g);
	api(152, 1, g);
	step("release %s", 0, what);
}

/*
 * sequences: eight sequences at once and a ninth refused. States used
 * again. A sequence stopped and resumed while it holds pedals, locks,
 * protections, programs and bends, and with every channel locked. The
 * API's setters at the edges of what they take. Tempos, volumes and
 * signatures that land on their marks exactly. Files whose chunks are
 * named near XMIDI's, or longer than 16 MB with a branch point past
 * 16 MB. A second initialisation, and a shutdown with sequences
 * registered.
 */
void
sequences(void)
{
	static uint32_t edges[] = {0x3f, 0x40, 0x41, 0x7f, 0x80, 0xc0, 0x140};
	static uint32_t pct[] = {126, 127, 100, 101, 0x1000000, 0x10000000};
	int32_t h[Nstates], hd;
	uint32_t i, k, ctl, c;

	initmidi();
	ctl = (uint32_t)(uintptr_t)xmidictrl();
	api(189, 1, (uint32_t)(uintptr_t)callback);
	step("callback installed", 0);
	for (k = 0; k < 9; k++) {
		h[k] = api(151, 4, (uint32_t)(uintptr_t)xmidi(1), 0, (uint32_t)(uintptr_t)states[k], ctl);
		step("register %u", h[k], k);
	}
	for (k = 0; k < 8; k += 2) {
		api(170, 1, h[k]);
		step("start %u", 0, k);
	}
	intervals(40, h[6], 4);
	for (k = 0; k < 8; k++)
		query(h[k]);
	/* a handle let go must not be asked about: the driver reads its state through a null pointer */
	api(171, 1, h[2]);
	api(152, 1, h[2]);
	step("stop and release 2", 0);
	api(152, 1, h[3]);
	step("release 3", 0);
	api(152, 1, h[4]);
	step("release 4, playing", 0);
	h[2] = h[3] = -1;
	intervals(10, h[6], 4);
	for (k = 0; k < 8; k++)
		query(h[k]);
	/* states used again: the held controllers where 2 played, the features where 3 was */
	hd = api(151, 4, (uint32_t)(uintptr_t)xmidi(3), 0, (uint32_t)(uintptr_t)states[2], ctl);
	step("register held", hd);
	h[3] = api(151, 4, (uint32_t)(uintptr_t)xmidi(1), 0, (uint32_t)(uintptr_t)states[3], ctl);
	step("register again", h[3]);
	query(hd);
	query(h[3]);
	for (k = 0; k < 8; k++)
		if (h[k] != -1) {
			api(171, 1, h[k]);
			api(152, 1, h[k]);
			step("let %u go", 0, k);
		}
	api(170, 1, hd);
	step("start held", 0);
	intervals(40, hd, 1);
	/* stopped and resumed holding them, at a volume of its own */
	api(177, 3, hd, 90, 0);
	api(171, 1, hd);
	step("stop held", 0);
	query(hd);
	api(173, 1, hd);
	step("resume held", 0);
	query(hd);
	intervals(4, hd, 1);
	/* every channel locked: the resume's locks fail */
	for (i = 0; i < 10; i++)
		step("lock", api(191, 0));
	api(171, 1, hd);
	api(173, 1, hd);
	step("resume, every channel locked", 0);
	query(hd);
	for (i = 1; i <= 16; i++)
		api(193, 1, i);
	step("channels released", 0);
	/* started again, rewound: two intervals' programs, stopped and resumed */
	api(170, 1, hd);
	intervals(2, hd, 1);
	api(171, 1, hd);
	api(173, 1, hd);
	step("restart, stop, resume", 0);
	intervals(20, hd, 1);
	/* the setters at their edges: NEXT, protection and locks */
	for (i = 0; i < nelem(edges); i++) {
		api(183, 4, hd, 2, 116, 1);
		api(183, 4, hd, 2, 117, edges[i]);
		step("next %x", 0, edges[i]);
		intervals(3, hd, 1);
		api(183, 4, hd, 6, 111, edges[i]);
		step("protect %x", 0, edges[i]);
		c = api(191, 0);
		step("lock", c);
		api(193, 1, c);
		api(183, 4, hd, 7, 110, edges[i]);
		step("lock 7 %x", 0, edges[i]);
		query(hd);
		api(183, 4, hd, 7, 110, 0);
		api(183, 4, hd, 6, 111, 0);
	}
	api(183, 4, hd, 2, 0x107, 0x164);
	step("control 0x107 0x164", 0);
	for (i = 0; i < nelem(pct); i++) {
		api(177, 3, hd, pct[i], 0);
		api(183, 4, hd, 2, 7, 100);
		api(183, 4, hd, 3, 7, 0x7f);
		api(183, 4, hd, 4, 7, 0xff);
		step("volume %x", 0, pct[i]);
		query(hd);
	}
	api(177, 3, hd, 100, 0);
	/* tempos and volumes whose steps land exactly */
	api(178, 3, hd, 99, 0);
	step("tempo 99", 0);
	intervals(6, hd, 1);
	api(178, 3, hd, 100, 0);
	api(178, 3, hd, 110, 83);
	step("tempo 110 in 83 ms", 0);
	intervals(12, hd, 1);
	api(177, 3, hd, 110, 83);
	step("volume 110 in 83 ms", 0);
	intervals(12, hd, 1);
	api(171, 1, hd);
	api(152, 1, hd);
	step("release held", 0);
	/* time signatures of a whole note, a half, and past 32 bits */
	hd = api(151, 4, (uint32_t)(uintptr_t)xmidi(4), 0, (uint32_t)(uintptr_t)states[5], ctl);
	step("register signatures", hd);
	api(170, 1, hd);
	intervals(150, hd, 1);
	api(171, 1, hd);
	api(152, 1, hd);
	step("release signatures", 0);
	/* files of names near XMIDI's, and the big one */
	trial("catx", xmidi(5), 0, 0);
	trial("forx", xmidi(6), 0, 0);
	trial("xmix", xmidi(7), 0, 0);
	trial("cat xmix", xmidi(8), 0, 0);
	trial("near", xmidi(9), 0, 3);
	trial("third past the cat", xmidi(10), 2, 0);
	trial("third a byte into the cat", xmidi(12), 2, 0);
	trial("big", xmidi(11), 0, Bignear);
	trial("big", xmidi(11), 0, Bigfar);
	trial("big's second", xmidi(11), 1, 0);
	trial("big lone", xmidi(11) + 12, 0, Bigfar);
	/* a second initialisation, the callback and the sequences gone */
	initmidi();
	h[0] = api(151, 4, (uint32_t)(uintptr_t)xmidi(1), 0, (uint32_t)(uintptr_t)states[0], ctl);
	step("register after init", h[0]);
	api(170, 1, h[0]);
	intervals(300, h[0], 16);
	/* shut down with sequences registered, a free slot between: they are let go */
	h[1] = api(151, 4, (uint32_t)(uintptr_t)xmidi(1), 0, (uint32_t)(uintptr_t)states[1], ctl);
	step("register", h[1]);
	h[2] = api(151, 4, (uint32_t)(uintptr_t)xmidi(1), 0, (uint32_t)(uintptr_t)states[2], ctl);
	step("register", h[2]);
	api(152, 1, h[1]);
	step("release", 0);
	api(104, 1, (uint32_t)(uintptr_t)"");
	step("shutdown", 0);
	step("register after shutdown", api(151, 4, (uint32_t)(uintptr_t)xmidi(1), 0, (uint32_t)(uintptr_t)states[0], ctl));
}

/*
 * queue: the overflow, its notes queried as they end. The overflow
 * changes a byte of the driver's rewind_seq, so the scenario ends there,
 * before any sequence is registered or started again.
 */
void
queue(void)
{
	int32_t h;

	initmidi();
	h = api(151, 4, (uint32_t)(uintptr_t)xmidi(2), 0, (uint32_t)(uintptr_t)states[0], 0);
	step("register", h);
	api(170, 1, h);
	step("start", 0);
	intervals(300, h, 16);
	api(171, 1, h);
	step("stop", 0);
	query(h);
	api(152, 1, h);
	step("release", 0);
	api(104, 1, (uint32_t)(uintptr_t)"");
	step("shutdown", 0);
}

/* A callback that sets the driver up again, from within its service. */
static void
reinit(int32_t h, int32_t v)
{
	step("callback %d %d, init", 0, h, v);
	initmidi();
}

/*
 * corners: the sequencer where its tests turn. -1 is refused as a
 * handle, even with slot 0 stopped and holding branch points, and a
 * handle let go is resumed. The corners alone in slot 4, the slots before it free: the
 * API's procedures on it, a channel mapped from odd to even, a note of
 * status 0x80, a long system exclusive, a volume landing on 126,
 * percents whose products pass 32 bits or have the sign bit, ramps of
 * 2^29 ms, a channel's notes flushed with one queued, values past a byte
 * and a channel past 256. Its pedals, locks and protections at 64, 63,
 * 0x80 and 65 on channels 1, 3, 10 and 16, another sequence protecting,
 * every channel locked, stopped and resumed. The bytes 0xfe and 0xff
 * played on locked channels, then released. A state let go while
 * playing, notes queued at its end, registered again. Notes sounding
 * from sequences let go, and counts of notes wrapped below 0, on
 * channels locked. The driver set up again from within a callback, and
 * with sequences playing, channels locked and notes sounding. A
 * shutdown with a sequence alone in slot 4, and with every slot taken.
 */
void
corners(void)
{
	static uint32_t edge[][2] = {{1, 64}, {3, 63}, {10, 0x80}, {16, 65}};
	static uint32_t held[] = {64, 111, 112, 110};
	int32_t h[8], hs, g;
	uint32_t i, k, ctl;

	initmidi();
	ctl = (uint32_t)(uintptr_t)xmidictrl();
	h[0] = api(151, 4, (uint32_t)(uintptr_t)xmidi(0), 1, (uint32_t)(uintptr_t)states[0], ctl);
	step("register branches", h[0]);
	api(170, 1, h[0]);
	intervals(4, h[0], 1);
	api(171, 1, h[0]);
	step("stop branches", 0);
	api(181, 2, -1, 2);
	step("branch -1", 0);
	api(173, 1, -1);
	step("resume -1", 0);
	query(h[0]);
	g = api(151, 4, (uint32_t)(uintptr_t)xmidi(1), 0, (uint32_t)(uintptr_t)states[1], ctl);
	step("register", g);
	api(152, 1, g);
	api(173, 1, g);
	api(171, 1, g);
	api(152, 1, g);
	step("let go, resumed, stopped, let go", 0);
	api(152, 1, h[0]);
	step("release branches", 0);
	/* the corners alone in slot 4 */
	for (k = 0; k < 4; k++)
		h[k] = api(151, 4, (uint32_t)(uintptr_t)xmidi(1), 0, (uint32_t)(uintptr_t)states[k], ctl);
	hs = api(151, 4, (uint32_t)(uintptr_t)xmidi(13), 0, (uint32_t)(uintptr_t)states[4], ctl);
	step("register corners", hs);
	for (k = 0; k < 4; k++)
		api(152, 1, h[k]);
	step("release the slots before", 0);
	timbres(hs);
	api(170, 1, hs);
	step("start corners", 0);
	intervals(2, hs, 1);
	api(192, 3, hs, 12, 11);
	step("map 12 11", 0);
	api(178, 3, hs, 100, 0);
	step("tempo 100", 0);
	api(177, 3, hs, 100, 0);
	api(183, 4, hs, 5, 7, 126);
	api(177, 3, hs, 100, 0);
	step("volume 126 at 100", 0);
	api(177, 3, hs, 90, 0);
	step("volume 90", 0);
	query(hs);
	intervals(40, hs, 4);
	api(183, 4, hs, 4, 110, 0);
	step("unlock 4, one note queued", 0);
	query(hs);
	api(183, 4, hs, 2, 119, 0x177);
	step("callback 0x177", 0);
	api(183, 4, hs, 0x103, 10, 0x30);
	step("pan on channel 0x103", 0);
	api(177, 3, hs, 0x2000000, 0);
	api(183, 4, hs, 2, 7, 0x80);
	step("volume 0x80 at 0x2000000", 0);
	api(177, 3, hs, 0x80000000, 0);
	api(183, 4, hs, 2, 7, 0xc0);
	step("volume 0xc0 at 0x80000000", 0);
	api(177, 3, hs, 100, 0);
	api(178, 3, hs, 102, 0x20000000);
	api(177, 3, hs, 102, 0x20000000);
	step("ramps of 2^29 ms", 0);
	query(hs);
	api(178, 3, hs, 100, 0);
	api(177, 3, hs, 100, 0);
	api(181, 2, hs, 1);
	step("branch, no branch points", 0);
	/* pedals, locks and protections at their edges, another sequence protecting, every channel locked */
	for (i = 0; i < nelem(edge); i++)
		for (k = 0; k < nelem(held); k++)
			api(183, 4, hs, edge[i][0], held[k], edge[i][1]);
	step("held at the edges", 0);
	g = api(151, 4, (uint32_t)(uintptr_t)xmidi(1), 0, (uint32_t)(uintptr_t)states[1], ctl);
	api(183, 4, g, 3, 111, 0x7f);
	api(183, 4, g, 10, 111, 0x7f);
	step("protected by another", g);
	for (i = 0; i < 9; i++)
		step("lock", api(191, 0));
	query(hs);
	api(171, 1, hs);
	step("stop at the edges", 0);
	query(hs);
	/* the synthesizer told otherwise meanwhile */
	for (i = 0; i < 5; i++)
		api(186, 3, 0xe0|i, 0x11, 0x22);
	api(186, 3, 0xc0, 0x33, 0);
	api(173, 1, hs);
	step("resume at the edges", 0);
	query(hs);
	intervals(4, hs, 1);
	for (i = 0; i < nelem(edge); i++)
		for (k = 0; k < nelem(held); k++)
			api(183, 4, hs, edge[i][0], held[k], 0);
	api(183, 4, g, 3, 111, 0);
	api(183, 4, g, 10, 111, 0);
	api(152, 1, g);
	for (i = 1; i <= 16; i++)
		api(193, 1, i);
	step("let go", 0);
	/* the bytes 0xfe and 0xff played on locked channels, the synthesizer told otherwise, released */
	for (i = 0; i < 8; i++)
		step("lock", api(191, 0));
	api(170, 1, hs);
	intervals(2, hs, 1);
	for (i = 1; i < 9; i++) {
		api(186, 3, 0xe0|i, 0x11, 0x22);
		api(186, 3, 0xc0|i, 0x33, 0);
	}
	api(186, 3, 0xb7, 64, 0x7f);
	step("restart on locked channels", 0);
	for (i = 1; i <= 16; i++) {
		api(193, 1, i);
		step("release channel %u", 0, i);
	}
	api(171, 1, hs);
	api(152, 1, hs);
	step("release corners", 0);
	/* a state let go while playing, notes queued at its end, registered again */
	g = api(151, 4, (uint32_t)(uintptr_t)xmidi(14), 0, (uint32_t)(uintptr_t)states[9], ctl);
	api(170, 1, g);
	api(152, 1, g);
	step("start and let go", g);
	intervals(10, -1, 1);
	g = api(151, 4, (uint32_t)(uintptr_t)xmidi(14), 0, (uint32_t)(uintptr_t)states[9], ctl);
	step("register in the state let go", g);
	query(g);
	api(170, 1, g);
	intervals(10, g, 1);
	api(152, 1, g);
	step("release ended", 0);
	/* the notes left sounding, on channels locked */
	for (i = 0; i < 9; i++)
		step("lock", api(191, 0));
	for (i = 1; i <= 16; i++)
		api(193, 1, i);
	step("channels released", 0);
	/* channel 9's count of notes wrapped to 0xff, then 0xfe, the others locked */
	g = api(151, 4, (uint32_t)(uintptr_t)xmidi(15), 0, (uint32_t)(uintptr_t)states[9], ctl);
	api(170, 1, g);
	intervals(2, g, 1);
	api(193, 1, 9);
	step("channel 9 released, its notes queued", 0);
	intervals(15, g, 1);
	for (k = 0; k < 2; k++) {
		for (i = 0; i < 8; i++)
			step("lock", api(191, 0));
		for (i = 1; i <= 16; i++)
			api(193, 1, i);
		step("channels released", 0);
		intervals(30, g, 1);
	}
	intervals(30, g, 4);
	api(152, 1, g);
	step("release", 0);
	/* set up again from within a callback, the only sequence's */
	initmidi();
	api(189, 1, (uint32_t)(uintptr_t)reinit);
	g = api(151, 4, (uint32_t)(uintptr_t)xmidi(16), 0, (uint32_t)(uintptr_t)states[0], ctl);
	api(170, 1, g);
	step("start trigger", g);
	intervals(3, -1, 1);
	/* set up again with sequences playing, channels locked and protected, notes sounding */
	initmidi();
	h[0] = api(151, 4, (uint32_t)(uintptr_t)xmidi(13), 0, (uint32_t)(uintptr_t)states[0], ctl);
	h[1] = api(151, 4, (uint32_t)(uintptr_t)xmidi(1), 0, (uint32_t)(uintptr_t)states[1], ctl);
	api(170, 1, h[0]);
	api(170, 1, h[1]);
	step("start two", 0);
	intervals(20, h[1], 4);
	step("lock", api(191, 0));
	step("lock", api(191, 0));
	api(183, 4, h[1], 5, 111, 0x7f);
	step("protect 5", 0);
	initmidi();
	/* shut down with a sequence alone in slot 4, then with every slot taken */
	for (k = 0; k < 5; k++)
		h[k] = api(151, 4, (uint32_t)(uintptr_t)xmidi(1), 0, (uint32_t)(uintptr_t)states[k], ctl);
	for (k = 0; k < 4; k++)
		api(152, 1, h[k]);
	api(170, 1, h[4]);
	intervals(4, h[4], 1);
	api(104, 1, (uint32_t)(uintptr_t)"");
	step("shutdown, slot 4 alone", 0);
	initmidi();
	for (k = 0; k < 8; k++) {
		h[k] = api(151, 4, (uint32_t)(uintptr_t)xmidi(1), 0, (uint32_t)(uintptr_t)states[k], ctl);
		step("register %u", h[k], k);
		if (k % 3 == 0)
			api(170, 1, h[k]);
	}
	intervals(4, -1, 1);
	api(104, 1, (uint32_t)(uintptr_t)"");
	step("shutdown, every slot", 0);
}
