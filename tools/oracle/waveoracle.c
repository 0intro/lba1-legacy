#define _GNU_SOURCE
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>
#include "dat.h"
#include "fns.h"
#include "dll.h"
#include "abi.h"

/*
 * waveoracle: the samples' driver for the Sound Blaster 16, W_SB16.DLL,
 * against sys/wave.c, its translation, and the drivers of the other
 * cards on checks of their own.
 *
 *	waveoracle [-f] [-g gamedir] driver.elf driver.sym [scenario...]
 *
 * driver.elf is lib386/lib_samp/wave_a.asm built for a card, the CD's
 * driver or a DOS build's, as tools/le2elf.py makes it, mapped where it
 * was linked (dll.c), and driver.sym names its procedures
 * (tools/wmap2sym.py of its link map). A driver that mixes as
 * W_SB16.DLL does, 16-bit stereo without surround as its banner says
 * (W_SB16.DLL, W_PAS16.DLL, W_MWSS.DLL, W_GOLD.DLL), takes every
 * scenario, any other cards alone. The game's calls reach the driver
 * through the list of procedures its first doubleword points to, as
 * wave_i.asm's do. The card is left alone, the oracle doing what
 * InitCard would: StopSample clears the lists, CURRENT_BUFFER points at
 * a half of the oracle's, and UpdateBuffer, which the card's interrupt
 * runs, is called for each half the port's clock passes (wavetick). The
 * variables StopSample clears (Critical, CurrentList, SonList,
 * FlagPause, follow, weirdcount) are found in its code.
 * SaveStateSample and RestoreStateSample wait for that interrupt to
 * fade a half out or in: an INT3 on each of their three waits' jne runs
 * UpdateBuffer there, into a half kept apart, while the C's idle mixes
 * one, and a wait's halves must be as many and alike.
 *
 * Each side plays from an arena of its own, the same VOCs at the same
 * offsets. After each call the results must be the same, the driver
 * must have kept the calling convention and left its critical section,
 * and the lists and variables must be the C's as wavestate lays them
 * out, an address read as its offset in its arena. After each half,
 * its 1024 frames must be the same.
 *
 *	random	a hundred thousand rounds of the game's calls at random at
 *		22000, 17000 and 12000 Hz each, a half mixed between: VOCs of
 *		random lengths, rates and data, interpolated or not, played
 *		at random pitches, volumes and repeats, plugged after the
 *		last or not, stopped by their handle or their long handle,
 *		asked for, given an info, paused and continued, the state
 *		saved and restored, snapshots taken, and moved as the HQR
 *		cache packs itself, or up
 *	steps	a million rounds of plays at random on VOC headers alone,
 *		each looked at and stopped by its handle: every length to
 *		2^24, time constant, first byte and block, pitches small and
 *		large
 *	game	(with -g) every sample of gamedir's SAMPLES.HQR and every
 *		voice of its VOX/EN_*.VOX (the chapters' files numbered from
 *		000, then EN_GAM.VOX) at 22000 Hz, at the game's pitch,
 *		4096, and at 3800 and 4400, each mixed to its end, and with
 *		-f at 12000 Hz too
 *	faults	(with -f) the calls where W_SB16.DLL faults, runs on or
 *		breaks its convention: time constants of 0 and of 241 to
 *		255, steps past 16 bits and of 0, frames past 32 bits, a
 *		plug that finds no father, a list full, an info given to a
 *		long handle unlisted while a sample waits in SonList, a
 *		state restored with a son saved and after a play, a
 *		snapshot of two samples, while a son waits and of the
 *		lists full, the volumes of a long handle unlisted while a
 *		sample waits in SonList
 *	cards	the driver alone, of any card: for every pair of volumes of
 *		0 to 128 and a hundred thousand pairs at random below
 *		65536, ChangeVolume keeps the convention and leaves a
 *		voice's VOL_LEFT and VOL_RIGHT as MixSample sets them, and
 *		on an 8-bit stereo driver a voice mixed alone after each
 *		has its bytes in the card's byte of the pair for the
 *		speaker its volumes lean to (the left alone below 74,
 *		volright times 128 over volleft, the right alone from 222
 *		and for volleft 0, both between), 80h in the other: the
 *		SB16's left byte is the first, the SB Pro's and the PAS's
 *		the second
 *
 * Without -f the calls keep to what W_SB16.DLL survives: time
 * constants of 1 to 240, steps of 1 to 0FFFFh, a sample's frames
 * counted in 32 bits, a plug only after a sample still listed, lists
 * short of full, an info given to an unlisted long handle, or a stop of
 * one, only while SonList is empty, a state restored only when SonList
 * was empty at its save and weirdcount is as the save left it, and a
 * snapshot only of one sample at most, none waiting. With -f, for a
 * driver with the fixes, the random calls take any value too. A call
 * that faults, or that runs on for 30 seconds, is reported and ends the
 * run.
 */

enum
{
	Half = 1024,	/* frames of a half */
	Arena = 4<<20,	/* bytes of each side's arena */
	Pool = 3<<20,	/* the random VOCs' part of it */
	Nlist = 50,	/* a list's voices */
	Entry = 44,	/* a voice's bytes */
	Liststate = Entry*Nlist + 4,
	State = 2*Liststate + 9,
	Rounds = 100000,
	Nvoc = 400,
	Hdr = 0x1a,	/* a VOC's header, where its block starts */
	Fhandle = 20,	/* a voice's long handle, in its bytes */
	Nwait = 3,	/* the waits of SaveStateSample and RestoreStateSample */
	Nkept = 4,	/* halves kept from a call that waits */
};

/* the driver's procedures, in its list's order */
enum
{
	Pinit,
	Pclear,
	Paskvars,
	Pmix,
	Pinfo0,
	Pstop,
	Pstopone,
	Pinlist,
	Psnap,
	Ppause,
	Pcontinue,
	Psave,
	Prestore,
	Pdmaaddr,
	Pbufsize,
	Pvolume,
	Pshift,
	Pstoponelong,
	Nprocs,
};

/* in a voice's bytes, its addresses, read as offsets in the arenas, and its volumes */
enum
{
	Faddress = 0,
	Fstart = 24,
	Fvolume = 32,	/* VOL_LEFT, then VOL_RIGHT */
};

typedef struct Voc Voc;

/* a VOC of the pool: where it lies in each arena, its bytes */
struct Voc
{
	uint32_t	off;
	uint32_t	n;
};

/* sys/wave.c */
uint32_t	WavePlay(uint16_t, uint16_t, uint16_t, uint8_t, uint16_t, uint16_t, void*);
void	WaveGiveInfo0(uint32_t, uint32_t);
void	WaveStop(void);
void	WaveStopOne(uint16_t);
void	WaveStopOneLong(uint32_t);
int	WaveInList(uint16_t);
int	WavePause(void);
void	WaveContinue(void);
int	WaveGetSnap(void**);
void	WaveSaveState(void);
void	WaveRestoreState(void);
void	WaveChangeVolume(uint32_t, uint32_t, uint32_t);
void	WaveMove(void*, void*, uint32_t);

static uint32_t procs[Nprocs];
static uint32_t updatebuffer, currentbuffer, playrate;
static uint32_t critical, currentlist, sonlist, flagpause, follow, weirdcount;
static uint8_t *arena[2];	/* the driver's, the C's */
static uint32_t drvhalf[Half], cwave[Half];
static uint64_t nhalves;	/* mixed so far, the port's count */
static int nwaves;	/* halves the port handed on, in a call of wavetick */
static uint32_t waitjne[Nwait];	/* where each wait's jne holds an INT3 */
static uint32_t drvkept[Nkept][Half], ckept[Nkept][Half];	/* the halves a call's waits mixed */
static int ndrvkept, nckept;
static int waiting;	/* the C's idle mixes halves */
static int nidle;	/* its calls while waiting */
static int saved, savedsons;	/* a state saved, with a son waiting */
static uint32_t savedweird;	/* weirdcount as the save left it */
static int leftbyte;	/* the byte of an 8-bit stereo pair the card plays left, 0 for another format */
static Voc pool[Nvoc];
static int npool;
static uint32_t poolend;
static int faults;	/* -f */
static char *gamedir;
static uint32_t seed = 1;
static int nfail;
static char where[256];	/* the scenario and round */
static char what[256];	/* the call */

void
fatal(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "waveoracle: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	exit(1);
}

void
fail(char *fmt, ...)
{
	va_list arg;

	nfail++;
	va_start(arg, fmt);
	fprintf(stderr, "waveoracle: %s: %s: ", where, what);
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
}

/*
 * sys/timer.c's and sys/sdl.c's, for wave.c: while a call waits for the
 * card's interrupt, every second idle mixes the next half, kept apart, as
 * the port's clock passes a half only after many, and else idle returns
 * at once. sdlwave keeps the half.
 */
void
idle(void)
{
	uint64_t half;

	if (!waiting || ++nidle % 2 != 0)
		return;
	if (nckept == Nkept)
		fatal("%s: %s: the C waits past %d halves", where, what, Nkept);
	half = (uint64_t)Half * 1000000000 / (uint64_t)PlayRate;
	wavetick((nhalves+1) * half);
	nhalves++;
}

void
sdlwave(const uint32_t *buf, int n, int rate)
{
	USED(rate);
	if (waiting) {
		memmove(ckept[nckept++], buf, n * sizeof buf[0]);
		return;
	}
	memmove(cwave, buf, n * sizeof buf[0]);
	nwaves++;
}

void
summedwave(uint64_t n, const uint32_t *buf, int nf)
{
	USED(n);
	USED(buf);
	USED(nf);
}

/* A call that runs on: where, and the end. */
static void
onalarm(int sig)
{
	USED(sig);
	fprintf(stderr, "waveoracle: %s: %s: runs on\n", where, what);
	_exit(1);
}

/* A call that faults: where, at what instruction, and the end. */
static void
onfault(int sig, siginfo_t *si, void *ctx)
{
	ucontext_t *uc;

	USED(si);
	uc = ctx;
	fprintf(stderr, "waveoracle: %s: %s: signal %d at %08x\n", where, what, sig,
		(uint32_t)uc->uc_mcontext.gregs[REG_EIP]);
	_exit(1);
}

/* xorshift32 */
static uint32_t
rnd(void)
{
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return seed;
}

static uint32_t
rndn(uint32_t n)
{
	return rnd() % n;
}

static void
put32(uint32_t a, uint32_t v)
{
	uint8_t *p;

	p = (uint8_t*)(uintptr_t)a;
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static uint32_t
peek32(uint32_t a)
{
	return get32((uint8_t*)(uintptr_t)a);
}

/* the addresses w_sb16.sym gives the names */
static void
loadsyms(char *path)
{
	FILE *f;
	char line[256], type[8], name[128];
	unsigned va;

	f = fopen(path, "r");
	if (f == NULL)
		fatal("%s: cannot open", path);
	while (fgets(line, sizeof line, f) != NULL) {
		if (sscanf(line, "%x %7s %127s", &va, type, name) != 3)
			continue;
		if (strcmp(name, "UpdateBuffer") == 0)
			updatebuffer = va;
		else if (strcmp(name, "CURRENT_BUFFER") == 0)
			currentbuffer = va;
		else if (strcmp(name, "PlayRate") == 0)
			playrate = va;
	}
	fclose(f);
	if (updatebuffer == 0 || currentbuffer == 0 || playrate == 0)
		fatal("%s: no UpdateBuffer, CURRENT_BUFFER or PlayRate", path);
}

/*
 * The driver's procedures from its list, and from StopSample's first
 * instructions the variables it clears:
 *
 *	66 C7 05 Critical 01 00		mov word ptr[Critical], 1
 *	33 C0				xor eax, eax
 *	A3 CurrentList			mov dword ptr[CurrentList], eax
 *	A3 SonList			mov dword ptr[SonList], eax
 *	A2 FlagPause			mov byte ptr[FlagPause], al
 *	48				dec eax
 *	A3 follow			mov dword ptr[follow], eax
 *	C7 05 weirdcount 00 00 01 00	mov dword ptr[weirdcount], 10000h
 */
static void
findvars(void)
{
	static uint8_t code[] = {
		0x66, 0xc7, 0x05, 0, 0, 0, 0, 0x01, 0x00, 0x33, 0xc0, 0xa3, 0, 0, 0, 0,
		0xa3, 0, 0, 0, 0, 0xa2, 0, 0, 0, 0, 0x48, 0xa3, 0, 0, 0, 0,
		0xc7, 0x05, 0, 0, 0, 0, 0x00, 0x00, 0x01, 0x00,
	};
	static int at[] = {3, 12, 17, 22, 28, 34};
	uint8_t *p;
	uint32_t list;
	int i, j;

	list = peek32(0x10000);
	for (i = 0; i < Nprocs; i++)
		procs[i] = peek32(list + 4*i);
	p = (uint8_t*)(uintptr_t)procs[Pstop];
	for (i = 0; i < (int)sizeof code; i++) {
		for (j = 0; j < (int)nelem(at); j++)
			if (i >= at[j] && i < at[j]+4)
				break;
		if (j == (int)nelem(at) && p[i] != code[i])
			fatal("StopSample at %08x is not the driver's: byte %d is %02x", procs[Pstop], i, p[i]);
	}
	critical = get32(p+3);
	currentlist = get32(p+12);
	sonlist = get32(p+17);
	flagpause = get32(p+22);
	follow = get32(p+28);
	weirdcount = get32(p+34);
}

/*
 * The card's interrupt where SaveStateSample or RestoreStateSample waits
 * for it, at the INT3 on a wait's jne: with ZF set the wait is over and
 * the jne passed, else UpdateBuffer mixes the next half into one kept
 * apart, as NewIRQ would outside a critical section, and the cmp runs
 * again. regcall leaves abicall's state to the call it interrupts.
 */
static void
onwait(int sig, siginfo_t *si, void *ctx)
{
	ucontext_t *uc;
	uint32_t at;
	Regs r;
	int i;

	USED(sig);
	USED(si);
	uc = ctx;
	at = (uint32_t)uc->uc_mcontext.gregs[REG_EIP] - 1;
	for (i = 0; i < Nwait; i++)
		if (waitjne[i] == at)
			break;
	if (i == Nwait) {
		fprintf(stderr, "waveoracle: %s: %s: a trap at %08x\n", where, what, at);
		_exit(1);
	}
	if (uc->uc_mcontext.gregs[REG_EFL] & 0x40) {
		uc->uc_mcontext.gregs[REG_EIP] = at + 2;
		return;
	}
	if (ndrvkept == Nkept) {
		fprintf(stderr, "waveoracle: %s: %s: the driver waits past %d halves\n", where, what, Nkept);
		_exit(1);
	}
	put32(currentbuffer, (uint32_t)(uintptr_t)drvkept[ndrvkept++]);
	memset(&r, 0, sizeof r);
	regcall(updatebuffer, &r);
	uc->uc_mcontext.gregs[REG_EIP] = at - 7;
}

/*
 * The waits of SaveStateSample and RestoreStateSample for the card's
 * interrupt, found in the code from the one to past the other, an INT3
 * put on each jne for onwait:
 *
 *	80 3D FlagPause 01|00		cmp byte ptr[FlagPause], 1 or 0
 *	75 F7				jne the cmp
 */
static void
findwaits(void)
{
	struct sigaction sa;
	uint8_t *p, *e;
	int n, i;

	p = (uint8_t*)(uintptr_t)procs[Psave];
	e = (uint8_t*)(uintptr_t)procs[Prestore];
	if (e < p) {
		p = e;
		e = (uint8_t*)(uintptr_t)procs[Psave];
	}
	n = 0;
	for (e += 256; p < e; p++) {
		if (p[0] != 0x80 || p[1] != 0x3d || get32(p+2) != flagpause || p[6] > 1
		|| p[7] != 0x75 || p[8] != 0xf7)
			continue;
		if (n == Nwait)
			fatal("found more than the %d waits of SaveStateSample and RestoreStateSample", Nwait);
		waitjne[n++] = (uint32_t)(uintptr_t)(p + 7);
	}
	if (n != Nwait)
		fatal("found %d of the %d waits of SaveStateSample and RestoreStateSample", n, Nwait);
	for (i = 0; i < n; i++)
		*(uint8_t*)(uintptr_t)waitjne[i] = 0xcc;
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = onwait;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGTRAP, &sa, NULL);
}

/* A procedure of the driver called with n arguments, its convention and its critical section checked. */
static uint32_t
call(int proc, int n, ...)
{
	va_list arg;
	int32_t a[8];
	uint32_t r;
	int i;

	va_start(arg, n);
	for (i = 0; i < n; i++)
		a[i] = va_arg(arg, uint32_t);
	va_end(arg);
	alarm(30);
	r = abicall(procs[proc], n, a);
	alarm(0);
	abiok("the driver");
	if (get16((uint8_t*)(uintptr_t)critical) != 0)
		fail("the driver left its critical section entered");
	return r;
}

/* Both sides' sample rate. */
static void
setrate(uint32_t rate)
{
	put32(playrate, rate);
	PlayRate = rate;
}

/* The two lists and the variables alike, else what differs. */
static int
samestate(void)
{
	static uint8_t c[State], d[State];
	static char *names[] = {"CurrentList", "SonList"};
	uint8_t *dp, *cp;
	uint32_t x, y;
	int l, i, k;

	memset(c, 0, sizeof c);
	wavestate(c);
	memmove(d, (void*)(uintptr_t)currentlist, Liststate);
	memmove(d+Liststate, (void*)(uintptr_t)sonlist, Liststate);
	d[2*Liststate] = *(uint8_t*)(uintptr_t)flagpause;
	memmove(d+2*Liststate+1, (void*)(uintptr_t)follow, 4);
	memmove(d+2*Liststate+5, (void*)(uintptr_t)weirdcount, 4);
	for (l = 0; l < 2; l++)
		for (i = 0; i <= Nlist; i++) {
			dp = d + l*Liststate + i*Entry;
			cp = c + l*Liststate + i*Entry;
			if (get32(dp) == 0 || get32(cp) == 0) {
				if (get32(dp) != get32(cp)) {
					fail("%s ends at voice %d on the %s's side alone", names[l], i,
						get32(dp) == 0 ? "driver" : "C");
					return 0;
				}
				break;
			}
			for (k = 0; k < Entry; k++) {
				if (k == Faddress || k == Fstart) {
					x = get32(dp+k) - (uint32_t)(uintptr_t)arena[0];
					y = get32(cp+k) - (uint32_t)(uintptr_t)arena[1];
					if (x != y) {
						fail("%s voice %d byte %d: offset %u against %u", names[l], i, k, x, y);
						return 0;
					}
					k += 3;
				} else if (dp[k] != cp[k]) {
					fail("%s voice %d byte %d: %02x against %02x", names[l], i, k, dp[k], cp[k]);
					return 0;
				}
			}
		}
	if (memcmp(d+2*Liststate, c+2*Liststate, 9) != 0) {
		fail("FlagPause, follow, weirdcount: %02x %08x %08x against %02x %08x %08x",
			d[2*Liststate], get32(d+2*Liststate+1), get32(d+2*Liststate+5),
			c[2*Liststate], get32(c+2*Liststate+1), get32(c+2*Liststate+5));
		return 0;
	}
	return 1;
}

/* Whether a voice of long handle h is listed, and how many voices each list holds. */
static int
listed(uint32_t h, int *ncur, int *nson)
{
	uint32_t p;
	int found, l, i, *n;

	found = 0;
	for (l = 0; l < 2; l++) {
		p = l == 0 ? currentlist : sonlist;
		n = l == 0 ? ncur : nson;
		for (i = 0; peek32(p + i*Entry) != 0; i++)
			if (peek32(p + i*Entry + Fhandle) == h)
				found = 1;
		*n = i;
	}
	return found;
}

/* Whether both lists are empty. */
static int
silent(void)
{
	return peek32(currentlist) == 0 && peek32(sonlist) == 0;
}

/* A half mixed on both sides, alike, else where. */
static int
mix(void)
{
	uint64_t half;
	int i;

	snprintf(what, sizeof what, "half %llu", (unsigned long long)nhalves);
	put32(currentbuffer, (uint32_t)(uintptr_t)drvhalf);
	alarm(30);
	abicall(updatebuffer, 0, NULL);
	alarm(0);
	half = (uint64_t)Half * 1000000000 / (uint64_t)PlayRate;
	nwaves = 0;
	wavetick((nhalves+1) * half);
	nhalves++;
	if (nwaves != 1) {
		fail("the port mixed %d halves", nwaves);
		return 0;
	}
	for (i = 0; i < Half; i++)
		if (drvhalf[i] != cwave[i]) {
			fail("frame %d: %08x against %08x", i, drvhalf[i], cwave[i]);
			return 0;
		}
	return samestate();
}

/* Both sides stopped, as the game starts and as InitCard leaves them. */
static int
stop(void)
{
	snprintf(what, sizeof what, "stop");
	call(Pstop, 0);
	WaveStop();
	return samestate();
}

/* The samples of a handle stopped on both sides, and those plugged after them. */
static int
stopone(uint32_t h)
{
	snprintf(what, sizeof what, "stop one %u", h);
	call(Pstopone, 1, h);
	WaveStopOne(h);
	return samestate();
}

/* The sample of a long handle stopped on both sides, those plugged after it left waiting. */
static int
stoponelong(uint32_t h)
{
	snprintf(what, sizeof what, "stop one long %08x", h);
	call(Pstoponelong, 1, h);
	WaveStopOneLong(h);
	return samestate();
}

/* A sample's volumes changed on both sides, alike, else where. */
static int
volume(uint32_t h, uint32_t vl, uint32_t vr)
{
	snprintf(what, sizeof what, "volumes %u %u of %08x", vl, vr, h);
	call(Pvolume, 3, h, vl, vr);
	WaveChangeVolume(h, vl, vr);
	return samestate();
}

/*
 * A snapshot of the samples on both sides, as many and each entry alike
 * (long handle, info and bytes played), else where.
 */
static int
snap(void)
{
	uint32_t list, d, *c;
	void *buf;
	int n, i, k;

	snprintf(what, sizeof what, "snapshot");
	list = 0;
	d = call(Psnap, 1, (uint32_t)(uintptr_t)&list);
	n = WaveGetSnap(&buf);
	if (d != (uint32_t)n) {
		fail("%u samples against %d", d, n);
		return 0;
	}
	c = buf;
	for (i = 0; i < n; i++)
		for (k = 0; k < 3; k++)
			if (peek32(list + 12*i + 4*k) != c[3*i+k]) {
				fail("sample %d word %d: %08x against %08x", i, k,
					peek32(list + 12*i + 4*k), c[3*i+k]);
				return 0;
			}
	return samestate();
}

/*
 * The state saved (Psave) or restored (Prestore) on both sides, the
 * halves each mixed while it waited as many and alike, else where.
 */
static int
state(int proc)
{
	int i, k, sons;

	snprintf(what, sizeof what, "%s the state", proc == Psave ? "save" : "restore");
	sons = peek32(sonlist) != 0;
	ndrvkept = 0;
	call(proc, 0);
	nckept = 0;
	nidle = 0;
	waiting = 1;
	alarm(30);
	if (proc == Psave)
		WaveSaveState();
	else
		WaveRestoreState();
	alarm(0);
	waiting = 0;
	if (ndrvkept != nckept) {
		fail("%d halves mixed while it waited against %d", ndrvkept, nckept);
		return 0;
	}
	for (k = 0; k < ndrvkept; k++)
		for (i = 0; i < Half; i++)
			if (drvkept[k][i] != ckept[k][i]) {
				fail("half %d of its waits, frame %d: %08x against %08x", k, i,
					drvkept[k][i], ckept[k][i]);
				return 0;
			}
	if (proc == Psave) {
		saved = 1;
		savedsons = sons;
		savedweird = peek32(weirdcount);
	}
	return samestate();
}

/* A sample played on both sides from the VOC at off, alike, else where. */
static int
play(uint32_t off, uint32_t handle, uint32_t pitch, uint32_t repeat, uint32_t plug, uint32_t vl, uint32_t vr)
{
	uint32_t d, c;

	snprintf(what, sizeof what, "play the VOC at %u, handle %u pitch %u repeat %u plug %u volumes %u %u",
		off, handle, pitch, repeat, plug, vl, vr);
	d = call(Pmix, 7, handle, pitch, repeat, plug, vl, vr, (uint32_t)(uintptr_t)(arena[0] + off));
	c = WavePlay(handle, pitch, repeat, plug, vl, vr, arena[1] + off);
	if (d != c) {
		fail("the long handle %08x against %08x", d, c);
		return 0;
	}
	return samestate();
}

/* Halves mixed until both lists are empty, n at most. */
static int
mixout(int n)
{
	while (n-- > 0 && !silent())
		if (!mix())
			return 0;
	return 1;
}

/*
 * The bytes of a VOC made at off in both arenas: its first byte b0
 * (below 9 asks for the interpolation), its block at hdr past bytes at
 * random, of type type packed by pack, of time constant tc and n bytes
 * of data at random, its size given as size (n + 2 for a true one), then
 * an end.
 */
static uint32_t
mkvoc(uint32_t off, int b0, uint32_t hdr, int type, int pack, int tc, uint32_t n, uint32_t size)
{
	static char sig[] = "Creative Voice File\032";
	uint8_t *p;
	uint32_t i;

	if (hdr < Hdr || off + hdr + 7 + n > Arena)
		fatal("a VOC of %u bytes, its block at %u, past the arena", n, hdr);
	p = arena[0] + off;
	memmove(p, sig, 20);
	p[0] = b0;
	p[0x14] = hdr;
	p[0x15] = hdr >> 8;
	p[0x16] = 0x0a;
	p[0x17] = 0x01;
	p[0x18] = 0x29;
	p[0x19] = 0x11;
	for (i = Hdr; i < hdr; i++)
		p[i] = rnd() >> 8;
	p[hdr] = type;
	p[hdr+1] = size;
	p[hdr+2] = size >> 8;
	p[hdr+3] = size >> 16;
	p[hdr+4] = tc;
	p[hdr+5] = pack;
	for (i = 0; i < n; i++)
		p[hdr+6+i] = rnd() >> 8;
	p[hdr+6+n] = 0;
	memmove(arena[1] + off, p, hdr+7+n);
	return hdr + 7 + n;
}

/* A VOC as the game's are: a block of type 1, unpacked, at 1Ah. */
static uint32_t
voc(uint32_t off, int b0, int tc, uint32_t n)
{
	return mkvoc(off, b0, Hdr, 1, 0, tc, n, n + 2);
}

/* The block of the VOC at off. */
static uint8_t*
block(uint32_t off)
{
	return arena[0] + off + get16(arena[0] + off + 0x14);
}

/* A first byte: the voices' 0 or 1, the samples' C, or any. */
static int
firstbyte(void)
{
	switch (rndn(4)) {
	case 0:
		return rndn(2);
	case 1:
		return rndn(256);
	default:
		return 'C';
	}
}

/* A time constant of 1 to 240, and with -f at times of 0 or past 240. */
static int
timeconst(void)
{
	if (faults && rndn(8) == 0)
		return rndn(4) == 0 ? 0 : 241 + rndn(15);
	return 1 + rndn(240);
}

/*
 * A VOC at the pool's end: of random length, rate, first byte and data,
 * its block at times past more of the header, at times of another type
 * or packed.
 */
static void
addvoc(void)
{
	uint32_t n, hdr;
	int type, pack;

	switch (rndn(8)) {
	case 0:
		n = rndn(4);
		break;
	case 1:
		n = 65000 + rndn(66000);
		break;
	default:
		n = rndn(20000);
		break;
	}
	hdr = rndn(8) == 0 ? Hdr + rndn(600) : Hdr;
	type = 1;
	pack = 0;
	if (rndn(16) == 0) {
		type = rndn(10);
		pack = rndn(2);
	}
	if (npool == Nvoc || poolend + hdr + 7 + n > Pool)
		return;
	pool[npool].off = poolend;
	pool[npool].n = mkvoc(poolend, firstbyte(), hdr, type, pack, timeconst(), n, n + 2);
	poolend += pool[npool].n;
	npool++;
}

/* The pool's ith VOC dropped. */
static void
dropvoc(int i)
{
	memmove(&pool[i], &pool[i+1], (npool - i - 1) * sizeof pool[0]);
	npool--;
}

/*
 * Whether W_SB16.DLL's MixSample survives a play of the block at blk at
 * the pitch and rate. A block of another type, or packed, it turns down
 * first. Else each of its divisions must keep in range: 1000000 by 256
 * less the time constant, taken in a byte, gives the rate in 16 bits,
 * the bent rate by the rate played gives the step in 16 bits, the step
 * must be 1 or more, and the frames' count must fit 32 bits.
 */
static int
survives(uint8_t *blk, uint32_t pitch, uint32_t rate)
{
	uint32_t r, e, len;
	int tc;

	if (blk[0] != 1 || blk[5] != 0)
		return 1;
	tc = blk[4];
	if (tc == 0 || tc > 240)
		return 0;
	r = 1000000 / (256 - tc);
	e = r * (pitch << 4);
	e = (uint16_t)((e >> 16) + (e >> 15 & 1));
	if (e >= rate || e == 0)
		return 0;
	len = (blk[1] | blk[2]<<8 | (uint32_t)blk[3]<<16) - 2;
	return (len >> 16) < (e << 16) / rate;
}

/* A handle: small, as the game's samples', at times 1234h or 0FFFFh. */
static uint32_t
anyhandle(void)
{
	switch (rndn(16)) {
	case 0:
		return 0x1234;
	case 1:
		return 0xffff;
	default:
		return rndn(16);
	}
}

/* A pitch: about the game's, at times anything or small. */
static uint32_t
anypitch(void)
{
	switch (rndn(4)) {
	case 0:
		return rndn(65536);
	case 1:
		return rndn(64);
	default:
		return 2048 + rndn(4096);
	}
}

/* A repeat: 1 mostly, 2 or 3, 0 (for ever) at times. */
static uint32_t
anyrepeat(void)
{
	switch (rndn(10)) {
	case 0:
		return 0;
	case 1:
	case 2:
	case 3:
		return 2 + rndn(2);
	default:
		return 1;
	}
}

/* A play of a VOC of the pool, kept to what the driver survives without -f. */
static int
randplay(void)
{
	Voc *v;
	uint32_t pitch, plug;
	int ncur, nson, i;

	if (npool == 0)
		return 1;
	v = &pool[rndn(npool)];
	plug = rndn(5) == 0;
	pitch = anypitch();
	if (!faults) {
		if (!listed(peek32(follow), &ncur, &nson))
			plug = 0;
		if ((plug ? nson : ncur) >= Nlist)
			return 1;
		for (i = 0; i < 100 && !survives(block(v->off), pitch, PlayRate); i++)
			pitch = anypitch();
		if (i == 100)
			return 1;
	}
	return play(v->off, anyhandle(), pitch, anyrepeat(), plug, rndn(65536), rndn(65536));
}

/* A long handle listed, at times one that is not, which W_SB16.DLL survives only while no son waits. */
static uint32_t
somehandle(void)
{
	int ncur, nson, i;
	uint32_t p;

	listed(0, &ncur, &nson);
	if ((faults || nson == 0) && (ncur + nson == 0 || rndn(4) == 0))
		return rnd() | 0x10000;
	if (ncur + nson == 0)
		return 0;
	i = rndn(ncur + nson);
	p = i < ncur ? currentlist + i*Entry : sonlist + (i - ncur)*Entry;
	return peek32(p + Fhandle);
}

/*
 * The pool's bytes from a VOC's start, or from its data's, where its
 * samples start, to the pool's end, moved down into the VOC before, as
 * the HQR cache packs itself, or at times up by 1 to 8 bytes, and the
 * voices playing them with them. A VOC whose bytes are overwritten, or
 * whose data leaves its header, is dropped from the pool. Moved up over
 * themselves, the bytes are copied forward a doubleword at a time, the
 * copy reading what it wrote: the VOCs from there on are dropped.
 */
static int
move(void)
{
	uint32_t src, dst, n, d;
	int k, i, up, data;

	if (npool < 2)
		return 1;
	k = 1 + rndn(npool - 1);
	src = pool[k].off;
	data = rndn(4) == 0;
	if (data)
		src = block(src) + 6 - arena[0];
	up = rndn(4) == 0;
	if (up) {
		d = 1 + rndn(8);
		if (poolend + d > Pool)
			return 1;
		dst = src + d;
	} else {
		d = 1 + rndn(src - pool[k-1].off);
		dst = src - d;
	}
	n = poolend - src;
	snprintf(what, sizeof what, "move %u bytes from %u to %u", n, src, dst);
	call(Pshift, 3, (uint32_t)(uintptr_t)(arena[0] + dst), (uint32_t)(uintptr_t)(arena[0] + src), n);
	WaveMove(arena[1] + dst, arena[1] + src, n);
	if (memcmp(arena[0] + dst, arena[1] + dst, n) != 0) {
		fail("the bytes moved differ");
		return 0;
	}
	if (up) {
		poolend += d;
		npool = k;
		return samestate();
	}
	for (i = k+1; i < npool; i++)
		pool[i].off -= d;
	poolend -= d;
	if (data)
		dropvoc(k);
	else
		pool[k].off -= d;
	if (dst < pool[k-1].off + pool[k-1].n)
		dropvoc(k-1);
	return samestate();
}

/* One of the game's calls at random on both sides, alike, else where. */
static int
randcall(void)
{
	uint32_t h, d, c, r;
	int ncur, nson;

	r = rndn(100);
	if (r < 28)
		return randplay();
	if (r < 52)
		return mix();
	if (r < 60) {
		h = anyhandle();
		snprintf(what, sizeof what, "in list %u", h);
		d = call(Pinlist, 1, h);
		c = WaveInList(h);
		if (d != c) {
			fail("%u against %u", d, c);
			return 0;
		}
		return samestate();
	}
	if (r < 64)
		return stopone(anyhandle());
	if (r < 67) {
		h = somehandle();
		if (h == 0)
			return 1;
		return stoponelong(h);
	}
	if (r < 72) {
		h = somehandle();
		if (h == 0)
			return 1;
		c = rnd();
		snprintf(what, sizeof what, "info0 %08x of %08x", c, h);
		call(Pinfo0, 2, h, c);
		WaveGiveInfo0(h, c);
		return samestate();
	}
	if (r < 77) {
		h = somehandle();
		if (h == 0)
			return 1;
		return volume(h, rnd(), rnd());
	}
	if (r < 81) {
		snprintf(what, sizeof what, "pause");
		d = call(Ppause, 0);
		c = WavePause();
		if (d != c) {
			fail("%u against %u", d, c);
			return 0;
		}
		return samestate();
	}
	if (r < 85) {
		snprintf(what, sizeof what, "continue");
		call(Pcontinue, 0);
		WaveContinue();
		return samestate();
	}
	if (r < 87)
		return stop();
	if (r < 89)
		return state(Psave);
	if (r < 91) {
		if (!saved || (!faults && (savedsons || peek32(weirdcount) != savedweird)))
			return 1;
		return state(Prestore);
	}
	if (r < 95) {
		listed(0, &ncur, &nson);
		if (!faults && (nson > 0 || ncur > 1))
			return 1;
		return snap();
	}
	if (r < 99) {
		addvoc();
		return 1;
	}
	return move();
}

static void
randomcalls(void)
{
	static uint32_t rates[] = {22000, 17000, 12000};
	int i, r;

	for (i = 0; i < (int)nelem(rates); i++) {
		snprintf(where, sizeof where, "random at %u Hz", rates[i]);
		setrate(rates[i]);
		if (!stop())
			return;
		npool = 0;
		poolend = 0;
		while (npool < 200 && poolend < Pool/2)
			addvoc();
		for (r = 0; r < Rounds; r++) {
			snprintf(where, sizeof where, "random at %u Hz, round %d", rates[i], r);
			if (!randcall())
				return;
		}
	}
	printf("random: %d rounds at each of %d rates, %llu halves mixed\n",
		Rounds, (int)nelem(rates), (unsigned long long)nhalves);
}

/*
 * A million plays at random, on VOC headers alone, each looked at and
 * stopped by its handle, no half mixed: every length to 2^24, time
 * constant, first byte and block, pitches small and large.
 */
static void
steps(void)
{
	static uint32_t rates[] = {22000, 17000, 12000};
	uint32_t len, pitch, h, hdr, i;
	int r, type, pack;

	for (i = 0; i < nelem(rates); i++) {
		snprintf(where, sizeof where, "steps at %u Hz", rates[i]);
		setrate(rates[i]);
		if (!stop())
			return;
		for (r = 0; r < 10*Rounds/3; r++) {
			snprintf(where, sizeof where, "steps at %u Hz, round %d", rates[i], r);
			len = rndn(4) == 0 ? rnd() & 0xffffff : rndn(70000);
			hdr = rndn(8) == 0 ? Hdr + rndn(600) : Hdr;
			type = 1;
			pack = 0;
			if (rndn(16) == 0) {
				type = rndn(10);
				pack = rndn(2);
			}
			mkvoc(0, firstbyte(), hdr, type, pack, timeconst(), 0, len + 2);
			pitch = anypitch();
			if (!faults && !survives(block(0), pitch, PlayRate))
				continue;
			h = anyhandle();
			if (!play(0, h, pitch, anyrepeat(), 0, rndn(65536), rndn(65536)) || !stopone(h))
				return;
		}
	}
	printf("steps: %d rounds at each of %d rates\n", 10*Rounds/3, (int)nelem(rates));
}

/* Whether an HQR entry holds a VOC, which is then copied to the start of both arenas. */
static int
loadvoc(uint8_t *e)
{
	uint32_t hdr, size, n;

	if (e == NULL)
		return 0;
	hdr = get16(e + 0x14);
	if (hdr > 512 || e[hdr] != 1)
		return 0;
	size = e[hdr+1] | e[hdr+2]<<8 | (uint32_t)e[hdr+3]<<16;
	n = hdr + 4 + size;
	if (n + 1 > Arena)
		fatal("a VOC of %u bytes", n);
	memmove(arena[0], e, n);
	arena[0][n] = 0;
	memmove(arena[1], arena[0], n + 1);
	return 1;
}

/* Each VOC of an HQR file played at the game's pitch and either side of it, each to its end. */
static int
playall(char *path, uint32_t vol, int *nplayed)
{
	static uint32_t pitches[] = {4096, 3800, 4400};
	uint8_t **e;
	int ne, i, p;

	ne = hqr(path, &e);
	for (i = 0; i < ne; i++) {
		if (!loadvoc(e[i]))
			continue;
		for (p = 0; p < (int)nelem(pitches); p++) {
			if (!faults && !survives(block(0), pitches[p], PlayRate))
				continue;
			snprintf(where, sizeof where, "%s entry %d at %u Hz", path, i, PlayRate);
			if (!stop() || !play(0, i, pitches[p], 1, 0, vol, vol) || !mixout(100000))
				return 0;
			if (!silent()) {
				fail("it plays on");
				return 0;
			}
			(*nplayed)++;
		}
	}
	for (i = 0; i < ne; i++)
		free(e[i]);
	free(e);
	return 1;
}

static int
exists(char *path)
{
	FILE *f;

	f = fopen(path, "rb");
	if (f == NULL)
		return 0;
	fclose(f);
	return 1;
}

static void
gamesounds(void)
{
	static uint32_t rates[] = {22000, 12000};
	char path[1024];
	int i, r, n, nvox;

	n = 0;
	nvox = 0;
	for (r = 0; r < (faults ? 2 : 1); r++) {
		setrate(rates[r]);
		snprintf(path, sizeof path, "%s/SAMPLES.HQR", gamedir);
		if (!playall(path, 128, &n))
			return;
		for (i = 0; i <= 100; i++) {
			if (i < 100)
				snprintf(path, sizeof path, "%s/VOX/EN_%03d.VOX", gamedir, i);
			else
				snprintf(path, sizeof path, "%s/VOX/EN_GAM.VOX", gamedir);
			if (!exists(path))
				continue;
			if (!playall(path, 512, &n))
				return;
			nvox++;
		}
	}
	printf("game: %d plays of SAMPLES.HQR and %d VOX files, %llu halves mixed in all\n",
		n, nvox, (unsigned long long)nhalves);
}

/* The calls where W_SB16.DLL faults or breaks its convention, each mixed a while. */
static void
faultcases(void)
{
	static int tcs[] = {0, 241, 248, 255};
	int i, n;

	setrate(22000);
	for (i = 0; i < (int)nelem(tcs); i++) {
		snprintf(where, sizeof where, "faults: time constant %d", tcs[i]);
		voc(0, 'C', tcs[i], 4000);
		if (!stop() || !play(0, 1, 4096, 1, 0, 128, 128) || !mixout(200))
			return;
	}

	snprintf(where, sizeof where, "faults: steps past 16 bits");
	setrate(12000);
	voc(0, 'C', 200, 4000);
	voc(5000, 0, 200, 4000);
	if (!stop() || !play(0, 1, 4096, 1, 0, 128, 128) || !play(5000, 2, 65535, 2, 0, 512, 512) || !mixout(200))
		return;
	setrate(22000);

	snprintf(where, sizeof where, "faults: a step of 0");
	if (!stop() || !play(0, 1, 0, 1, 0, 128, 128))
		return;

	snprintf(where, sizeof where, "faults: frames past 32 bits");
	mkvoc(0, 'C', Hdr, 1, 0, 166, 64, 0);
	mkvoc(1000, 'C', Hdr, 1, 0, 166, 64, 1);
	mkvoc(2000, 'C', Hdr, 1, 0, 1, 64, 0xffffff);
	if (!stop() || !play(0, 1, 4096, 1, 0, 128, 128) || !play(1000, 2, 4096, 1, 0, 128, 128)
	|| !play(2000, 3, 50, 1, 0, 128, 128) || !mix() || !mix() || !stop())
		return;

	snprintf(where, sizeof where, "faults: a plug that finds no father");
	voc(0, 'C', 166, 4000);
	if (!stop() || !play(0, 1, 4096, 1, 1, 128, 128) || !play(0, 2, 4096, 1, 0, 128, 128)
	|| !mix() || !stopone(2) || !play(0, 3, 4096, 1, 1, 128, 128))
		return;

	snprintf(where, sizeof where, "faults: an info given to a long handle unlisted while a son waits");
	snprintf(what, sizeof what, "info0 of 12340000");
	if (!stop() || !play(0, 1, 4096, 1, 0, 128, 128) || !play(0, 2, 4096, 1, 1, 128, 128))
		return;
	call(Pinfo0, 2, 0x12340000, 7);
	WaveGiveInfo0(0x12340000, 7);
	if (!samestate())
		return;
	snprintf(where, sizeof where, "faults: the volumes of a long handle unlisted while a son waits");
	if (!volume(0x12340000, 64, 32))
		return;

	snprintf(where, sizeof where, "faults: a state restored with a son saved");
	voc(0, 'C', 166, 4000);
	if (!stop() || !play(0, 1, 4096, 1, 0, 128, 128) || !play(0, 2, 4096, 1, 1, 128, 128)
	|| !state(Psave) || !mix() || !state(Prestore) || !mixout(200))
		return;

	snprintf(where, sizeof where, "faults: a state restored after a play");
	if (!stop() || !play(0, 1, 4096, 1, 0, 128, 128) || !state(Psave)
	|| !play(0, 2, 4096, 1, 0, 128, 128) || !mix() || !state(Prestore) || !mixout(200))
		return;

	snprintf(where, sizeof where, "faults: a snapshot of two samples");
	if (!stop() || !play(0, 1, 4096, 1, 0, 128, 128) || !play(0, 2, 4096, 1, 0, 128, 128)
	|| !mix() || !snap())
		return;

	snprintf(where, sizeof where, "faults: a snapshot while a son waits");
	if (!stop() || !play(0, 1, 4096, 1, 0, 128, 128) || !play(0, 2, 4096, 1, 1, 128, 128)
	|| !mix() || !snap() || !mixout(200))
		return;

	snprintf(where, sizeof where, "faults: the lists full");
	if (!stop())
		return;
	for (n = 0; n <= Nlist; n++)
		if (!play(0, n & 15, 4096, 0, 0, 128, 128))
			return;
	for (n = 0; n <= Nlist; n++)
		if (!play(0, n & 15, 4096, 1, 1, 128, 128))
			return;
	if (!snap() || !mixout(10) || !stop())
		return;
	printf("faults: ok\n");
}

/*
 * The byte of a pair an 8-bit stereo driver gives a voice of these
 * volumes, as W_SB16.DLL leans: the card's byte for the right speaker
 * from 222 (volright times 128 over volleft) and for volleft 0, for the
 * left below 74, and 0 for both between.
 */
static int
position(uint32_t vl, uint32_t vr)
{
	if (vl == 0 || vr*128/vl >= 222)
		return 3 - leftbyte;
	if (vr*128/vl < 74)
		return leftbyte;
	return 0;
}

/*
 * Whether the voice alone in CurrentList, mixed into a half as the first
 * voice is, is where its volumes ask: its bytes in the pair's byte that
 * position gives and 80h in the other, or in both alike.
 */
static int
onside(uint32_t vl, uint32_t vr)
{
	uint8_t *b;
	int first, second, same, got, want, i;

	put32(currentbuffer, (uint32_t)(uintptr_t)drvhalf);
	alarm(30);
	abicall(updatebuffer, 0, NULL);
	alarm(0);
	b = (uint8_t*)drvhalf;
	first = 0;
	second = 0;
	same = 0;
	for (i = 0; i < Half; i++) {
		first += b[2*i] != 0x80;
		second += b[2*i+1] != 0x80;
		same += b[2*i] == b[2*i+1];
	}
	if (same == Half)
		got = 0;
	else if (second == 0)
		got = 1;
	else if (first == 0)
		got = 2;
	else
		got = -1;
	want = position(vl, vr);
	if (got != want) {
		fail("the sample in byte %d of the pair, its volumes asking %d (0 for both)", got, want);
		return 0;
	}
	return 1;
}

/*
 * The checks for a driver of any card, on the driver alone: for every
 * pair of volumes of 0 to 128, and pairs at random below 65536,
 * ChangeVolume keeps the convention and leaves a voice's VOL_LEFT and
 * VOL_RIGHT as MixSample sets them for the same volumes, and on an 8-bit
 * stereo driver each voice mixed alone is where its volumes ask.
 */
static void
cards(void)
{
	uint32_t vl, vr, h, set, changed;
	int i, n;

	voc(0, 'C', 166, 4000);
	n = 129*129 + Rounds;
	for (i = 0; i < n; i++) {
		if (i < 129*129) {
			vl = i / 129;
			vr = i % 129;
		} else {
			vl = rndn(65536);
			vr = rndn(65536);
		}
		snprintf(where, sizeof where, "cards, volumes %u %u", vl, vr);
		snprintf(what, sizeof what, "a play");
		call(Pstop, 0);
		call(Pmix, 7, 1, 4096, 1, 0, vl, vr, (uint32_t)(uintptr_t)arena[0]);
		set = peek32(currentlist + Fvolume);
		if (leftbyte != 0 && !onside(vl, vr))
			return;
		snprintf(what, sizeof what, "a play, its volumes changed");
		call(Pstop, 0);
		h = call(Pmix, 7, 2, 4096, 1, 0, vr, vl, (uint32_t)(uintptr_t)arena[0]);
		call(Pvolume, 3, h, vl, vr);
		changed = peek32(currentlist + Fvolume);
		if (nfail > 0)
			return;
		if (changed != set) {
			fail("MixSample sets VOL_LEFT and VOL_RIGHT %08x, ChangeVolume %08x", set, changed);
			return;
		}
		if (leftbyte != 0 && !onside(vl, vr))
			return;
	}
	printf("cards: %d pairs of volumes\n", n);
}

/*
 * The byte of an 8-bit stereo pair the driver's card plays on its left
 * speaker, by its banner: the SB16 the first, the SB Pro and the PAS the
 * second, as wave.inc's LEFTPOS says. 0 for a driver of another format.
 */
static int
leftbyteof(void)
{
	char *b;

	b = (char*)(uintptr_t)0x10004;
	if (strstr(b, "8 bit Stereo") == NULL)
		return 0;
	if (strstr(b, "Sound Blaster 16") != NULL)
		return 1;
	return 2;
}

/* Whether the driver mixes as sys/wave.c does, 16-bit stereo, by its banner after its list's address. */
static int
sb16like(void)
{
	char *b;

	b = (char*)(uintptr_t)0x10004;
	return strstr(b, "16 bit Stereo") != NULL && strstr(b, "Surround") == NULL;
}

static void
usage(void)
{
	fprintf(stderr, "usage: waveoracle [-f] [-g gamedir] driver.elf driver.sym [scenario...]\n");
	exit(2);
}

/* Whether a scenario is among those asked, all when none is. */
static int
asked(char *name, char **argv, int argc)
{
	int i;

	if (argc == 0)
		return 1;
	for (i = 0; i < argc; i++)
		if (strcmp(argv[i], name) == 0)
			return 1;
	return 0;
}

int
main(int argc, char *argv[])
{
	struct sigaction sa;
	char **scen;
	int i, n, like;

	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		if (strcmp(argv[i], "-f") == 0)
			faults = 1;
		else if (strcmp(argv[i], "-g") == 0 && i+1 < argc)
			gamedir = argv[++i];
		else
			usage();
	}
	if (argc - i < 2)
		usage();
	scen = argv + i + 2;
	n = argc - i - 2;
	for (i = 0; i < n; i++)
		if (strcmp(scen[i], "random") != 0 && strcmp(scen[i], "steps") != 0
		&& strcmp(scen[i], "game") != 0 && strcmp(scen[i], "faults") != 0
		&& strcmp(scen[i], "cards") != 0)
			usage();
	for (i = 0; i < n; i++)
		if ((strcmp(scen[i], "game") == 0 && gamedir == NULL) || (strcmp(scen[i], "faults") == 0 && !faults))
			usage();
	dllmap(scen[-2]);
	loadsyms(scen[-1]);
	findvars();
	findwaits();
	like = sb16like();
	leftbyte = leftbyteof();
	for (i = 0; i < n; i++)
		if (!like && strcmp(scen[i], "cards") != 0)
			fatal("%s mixes otherwise than sys/wave.c: it plays cards alone", scen[-2]);
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = onfault;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGFPE, &sa, NULL);
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGILL, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
	signal(SIGALRM, onalarm);
	arena[0] = calloc(1, Arena);
	arena[1] = calloc(1, Arena);
	if (arena[0] == NULL || arena[1] == NULL)
		fatal("out of memory");
	Wave_Driver_Enable = 1;
	if (like && asked("random", scen, n) && nfail == 0)
		randomcalls();
	if (like && asked("steps", scen, n) && nfail == 0)
		steps();
	if (like && gamedir != NULL && asked("game", scen, n) && nfail == 0)
		gamesounds();
	if (like && faults && asked("faults", scen, n) && nfail == 0)
		faultcases();
	if (asked("cards", scen, n) && nfail == 0)
		cards();
	if (nfail > 0) {
		fprintf(stderr, "waveoracle: %d failed\n", nfail);
		return 1;
	}
	printf("waveoracle: ok\n");
	return 0;
}
