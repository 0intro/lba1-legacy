#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "dat.h"
#include "fns.h"
#include "nuked/opl3.h"
#include "dll.h"
#include "abi.h"
#include "pc.h"
#include "dosfs.h"

/*
 * ailoracle: the music as lib386/lib_midi/midi.c plays it through
 * AIL/32 and the CD's XMIDI driver for the OPL3, A32SP2FM.DLL, served
 * by AIL's timer, against sys/midi.c, the port's, over sys/xmidi.c and
 * sys/yamaha.c, at the interface the game calls: InitMidiDLL,
 * AskMidiVars and InitMidi as InitAdelineSystem calls them, NewProc08
 * given to AIL's timer at 50 Hz, then PlayMidi, StopMidi,
 * IsMidiPlaying, FadeMidiDown, VolumeMidi, DoLoopMidi and ClearMidi.
 *
 *	ailoracle lba0.elf lba0.sym dir
 *
 * lba0.elf is a DOS build, as tools/le2elf.py makes it, and lba0.sym
 * names its procedures and data (tools/wmap2sym.py of its link map). dir
 * is the game's directory, which holds A32SP2FM.DLL, SAMPLE.OPL and
 * MIDI_SB.HQR. The build's code runs on pc.c's PC, DOS memory up to
 * 1 MB, _Extender 1 as DOS/4GW sets it, its files read from dir through
 * dosfs.c, the standard streams linked for InitMidiDLL's printf as
 * OpenWatcom's run-time links them at its start (__InitFiles), and a
 * Sound Blaster 16 at the base MidiBase names, 220h for -1: the OPL3's
 * addresses at the base, base+8 and base+2 (the second array), the data
 * at the next port, its timers counting on the ports' own time, a
 * microsecond an access, and the mixer's registers at base+4 and
 * base+5, read back as written. The CD's lba.cfg gives MidiBase 220h,
 * and MidiIRQ and MidiDMA blank, which Def_ReadValue2 reads as 0, and
 * the oracle writes them so through AskMidiVars's pointer but in its
 * keys part. AIL sets the PIT's channel 0 for the driver's 120 Hz, and
 * IRQ 0 calls AIL's handler at each of its counts, the driver served at
 * each, the BIOS's handler chained to as AIL's counter for it carries.
 *
 * sys/midi.c is given the same keys,
 * and its clock passes each of those periods in turn (miditick). Each
 * side runs in a process of its own, and the driver's descriptor after
 * the start (its base, IRQ, DMA and DRQ), the OPL3's register writes,
 * each with the periods served before it, and IsMidiPlaying's answers
 * must be the same, in the same order, a difference reported with the
 * call before it. The calls are the game's: PlayMusic's IsMidiPlaying
 * while the tune is the one asked for, then StopMidi, PlayMidi and
 * VolumeMidi(100), StopMidi, FadeMidiDown(1), VolumeMidi(100) after
 * MaxVolume set as SetVolumes sets it when the mixer leaves the music's
 * volume to it, DoLoopMidi, IsMidiPlaying, and ClearMidi at the end.
 *
 *	game	every tune of MIDI_SB.HQR as PlayMusic starts it, polled every
 *		third period as the frames poll it, then faded, stopped, played
 *		on while DoLoopMidi starts it again as the menus' fire does, or
 *		asked for again while it plays and its volume changed
 *	random	2000 of those calls at random periods, of any tune
 *	keys	the start with MidiBase, MidiIRQ and MidiDMA as a user may
 *		write them for a Sound Blaster 16 (220h 5 1, 240h 7 3, 260h 10
 *		5, 280h 2 0) and as -1, which keeps the driver's own, then the
 *		logo's tune as PlayMusic starts it, polled for 2 seconds, and
 *		ClearMidi
 *
 * The DOS side must answer 1 to InitMidiDLL and InitMidi, keep every
 * register in each call and each interrupt, send one end of interrupt
 * at each and another when it chains to the BIOS, count the game's
 * ticks as AIL's counter gives them, and give the PIT's channel and
 * vector 8 back to the BIOS at ClearMidi. What the port does otherwise
 * on purpose is let pass, its count said: the game's timer, which AIL
 * calls when its counter, 8333 us added at each period, reaches
 * 20000, where the port's clock ticks every 20 ms.
 *
 * Each part runs its sides in processes of their own, ended after 600
 * seconds, its differences reported, the first 20 in full, and the end
 * is "ailoracle: ok", or "ailoracle: N failed" and status 1.
 */

enum
{
	Base = 0x220,	/* MidiBase, the CD's */
	Lastbase = 0x280,	/* the last a Sound Blaster 16 is set to */
	Jingle = 31,	/* the logo's tune */
	Divisor = 9943,	/* the PIT's count for 120 Hz, sys/midi.c's */
	Pithz = 1193182,
	Period = 8333,	/* what AIL's counters add at each, in us */
	Gameus = 20000,	/* the game's timer, 50 Hz */
	Biosus = 54925,	/* the BIOS's */
	Hz = 120,	/* periods a second */
	Rounds = 2000,
	Nrec = 1<<23,	/* a side's records */
	Nevent = 1<<16,
	Dosend = 0x100000,
	Extender = 1,	/* the run-time's _Extender under DOS/4GW */
	Playing = 0x200,	/* a record of IsMidiPlaying's answer */
	Desc,	/* 201h to 204h: the descriptor's base, IRQ, DMA and DRQ after the start */
	Call = Desc + 4,	/* a call about to be made, its index */
};

/* the calls of the game the parts make */
enum
{
	Eplay,	/* PlayMusic's: IsMidiPlaying if the tune is the last asked for, then StopMidi, PlayMidi, VolumeMidi(100) */
	Estop,	/* StopMusicMidi's StopMidi */
	Efade,	/* FadeMusicMidi's FadeMidiDown(1) */
	Evolume,	/* VolumeMidi(100) */
	Emax,	/* SetVolumes's: MaxVolume, then VolumeMidi(100) */
	Eloop,	/* DoLoopMidi */
	Eplaying,	/* IsMidiPlaying */
	Eclear,	/* ClearMidi */
};

typedef struct Event Event;
typedef struct Rec Rec;
typedef struct Side Side;

struct Event
{
	uint32_t	tick;	/* the periods served before it */
	int	op;
	int32_t	arg;
};

/* an OPL3 register written, its second array above 0xff, or an answer of IsMidiPlaying */
struct Rec
{
	uint32_t	tick;
	uint16_t	reg;
	uint16_t	val;
};

/* what a side did, shared with the process it runs in */
struct Side
{
	uint32_t	n;
	uint32_t	ticks;	/* periods served */
	uint32_t	timerref;	/* the game's ticks */
	uint32_t	bios;	/* the BIOS's handler chained to */
	uint32_t	divisor;	/* the PIT's last count */
	Rec	*r;
};

/* the assembly's procedures and data, by lba0.sym */
static struct
{
	uintptr_t	initmididll;
	uintptr_t	askmidivars;
	uintptr_t	initmidi;
	uintptr_t	clearmidi;
	uintptr_t	playmidi;
	uintptr_t	stopmidi;
	uintptr_t	ismidiplaying;
	uintptr_t	fademididown;
	uintptr_t	volumemidi;
	uintptr_t	doloopmidi;
	uintptr_t	registertimer;
	uintptr_t	settimerfrequency;
	uintptr_t	starttimer;
	uintptr_t	newproc08;
	uintptr_t	initfiles;	/* OpenWatcom's run-time's, 0 in Watcom C 9's */
	uint32_t	*timerref;
	uint32_t	*desc;
	int32_t	*maxvolume;
	uint8_t	*extender;
} as;

/* sys/midi.c's interface, which lib386/lib_midi/lib_midi.h declares for the game */
extern int32_t	MaxVolume;
void	ClearMidi(void);
void	PlayMidi(unsigned char*);
void	StopMidi(void);
int32_t	IsMidiPlaying(void);
void	FadeMidiDown(int16_t);
void	VolumeMidi(int16_t);
void	DoLoopMidi(void);

static char *dir;
static int32_t keys[3] = {Base, 0, 0};	/* MidiBase, MidiIRQ and MidiDMA, as lba.cfg gives them */
static uint32_t cardbase = Base;	/* where the card sits */
static uint8_t **xmi;	/* MIDI_SB.HQR's tunes */
static int ntunes;
static Event *events;
static int nevents;
static Side *sides;	/* the assembly's, then ours */
static Side *cur;
static uint32_t tick;

/* the card's ports, as the DOS side meets them */
static uint64_t isa;	/* their time: a microsecond an access */
static uint32_t addr;	/* the register addressed, its array in bit 8 */
static uint32_t t1, t2, ctl, flags;
static uint64_t t1start, t2start;
static uint8_t mixidx, mixer[256];
static uint32_t pitcount, pitlow;

/* the rest of the program's, for sys/midi.c: the configuration, the files, SDL, the records and the OPL3 */
char	PathConfigFile[] = "LBA.CFG";

static void
record(uint32_t reg, uint32_t val)
{
	Rec *r;

	if (cur->n == Nrec)
		fatal("more than %d records", Nrec);
	r = &cur->r[cur->n++];
	r->tick = tick;
	r->reg = reg;
	r->val = val;
}

/* The configuration: the part's keys. */
int32_t
Def_ReadValue2(unsigned char *path, unsigned char *key, int32_t *v)
{
	USED(path);
	if (strcmp((char*)key, "MidiBase") == 0)
		*v = keys[0];
	else if (strcmp((char*)key, "MidiIRQ") == 0)
		*v = keys[1];
	else
		*v = keys[2];
	return 1;
}

/* The game's files, from dir. */
uint32_t
FileSize(char *name)
{
	char path[1024];
	struct stat st;

	snprintf(path, sizeof path, "%s/%s", dir, name);
	if (stat(path, &st) < 0)
		return 0;
	return st.st_size;
}

uint32_t
Load(char *name, void *buf)
{
	char path[1024];
	uint8_t *b;
	long n;

	snprintf(path, sizeof path, "%s/%s", dir, name);
	b = readfile(path, &n);
	memmove(buf, b, n);
	free(b);
	return n;
}

void
sdlmidi(int16_t *buf, int n, int hz)
{
	USED(buf);
	USED(n);
	USED(hz);
}

void
summedmidi(uint64_t t, int16_t *buf, int n)
{
	USED(t);
	USED(buf);
	USED(n);
}

void
OPL3_Reset(opl3_chip *chip, uint32_t hz)
{
	USED(chip);
	USED(hz);
}

/* The chip's registers, as sys/midi.c gives them, recorded. */
void
OPL3_WriteReg(opl3_chip *chip, uint16_t reg, uint8_t v)
{
	USED(chip);
	record(reg, v);
}

void
OPL3_Generate(opl3_chip *chip, int16_t *buf)
{
	USED(chip);
	buf[0] = buf[1] = 0;
}

static uint32_t
rnd(void)
{
	static uint32_t x = 0x510e527f;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

/* The OPL3's status: a timer's flag once it has counted from its register to 256, and the interrupt's with it. */
static uint32_t
status(void)
{
	if ((ctl & 0x41) == 0x01 && isa - t1start >= (256 - t1) * 80u)
		flags |= 0x40;
	if ((ctl & 0x22) == 0x02 && isa - t2start >= (256 - t2) * 320u)
		flags |= 0x20;
	return flags != 0 ? 0x80 | flags : 0;
}

/* A register of the OPL3 written: the timers' kept, register 4's bit 7 clearing their flags, each recorded. */
static void
oplreg(uint32_t r, uint32_t v)
{
	if (r == 2)
		t1 = v;
	else if (r == 3)
		t2 = v;
	else if (r == 4 && (v & 0x80) != 0)
		flags = 0;
	else if (r == 4) {
		if ((v & 1) != 0 && (ctl & 1) == 0)
			t1start = isa;
		if ((v & 2) != 0 && (ctl & 2) == 0)
			t2start = isa;
		ctl = v;
	}
	record(r, v);
}

static uint32_t
cardin(uint32_t port, int w)
{
	USED(w);
	isa++;
	switch (port - cardbase) {
	case 0:
	case 8:
		return status();
	case 5:
		return mixer[mixidx];
	}
	return 0xff;
}

static void
cardbyte(uint32_t port, uint32_t v)
{
	isa++;
	switch (port - cardbase) {
	case 0:
	case 8:
		addr = v;
		break;
	case 2:
		addr = 0x100 | v;
		break;
	case 1:
	case 3:
	case 9:
		oplreg(addr, v);
		break;
	case 4:
		mixidx = v;
		break;
	case 5:
		mixer[mixidx] = v;
		break;
	}
}

/* An out to the card, the bus giving it a byte at a time from the lowest. */
static void
cardout(uint32_t port, uint32_t v, int w)
{
	int i;

	for (i = 0; i < w; i++)
		cardbyte(port + i, v >> 8*i & 0xff);
}

static uint32_t
pitin(uint32_t port, int w)
{
	USED(w);
	fail("the PIT's port %xh read", port);
	return 0;
}

/* The PIT's ports: 43h a control word for channel 0, 40h its count, low byte then high. */
static void
pitout(uint32_t port, uint32_t v, int w)
{
	USED(w);
	if (port == 0x43) {
		if ((v & 0xf0) != 0x30)
			fail("a control word %02xh", v);
		pitlow = 0;
		return;
	}
	if (port != 0x40) {
		fail("the PIT's port %xh written, %02xh", port, v);
		return;
	}
	if (!pitlow) {
		pitcount = v & 0xff;
		pitlow = 1;
		return;
	}
	pitcount |= (v & 0xff) << 8;
	pitlow = 0;
	cur->divisor = pitcount;
}

/* The BIOS's handler of IRQ 0, chained to: its end of interrupt. */
static void
biosirq(void)
{
	neoi++;
}

static void
add(uint32_t t, int op, int32_t arg)
{
	if (nevents == Nevent)
		fatal("more than %d calls", Nevent);
	events[nevents].tick = t;
	events[nevents].op = op;
	events[nevents].arg = arg;
	nevents++;
}

/* Calls op every third period from t for n periods: the end. */
static uint32_t
polls(uint32_t t, uint32_t n, int op)
{
	uint32_t end;

	for (end = t + n; t < end; t += 3)
		add(t, op, 0);
	return end;
}

static int
anytune(void)
{
	int n;

	do
		n = rnd() % ntunes;
	while (xmi[n] == NULL);
	return n;
}

/*
 * Every tune as PlayMusic starts it, then, by turns, faded, stopped,
 * played on under DoLoopMidi, and asked for again while it plays, its
 * volume changed.
 */
static void
gamecalls(void)
{
	uint32_t t;
	int n, k;

	nevents = 0;
	t = 0;
	k = 0;
	for (n = 0; n < ntunes; n++) {
		if (xmi[n] == NULL)
			continue;
		add(t, Eplay, n);
		switch (k++ % 4) {
		case 0:
			t = polls(t, 10*Hz, Eplaying);
			add(t, Efade, 1);
			t = polls(t, 2*Hz, Eplaying);
			break;
		case 1:
			t = polls(t, 8*Hz, Eplaying);
			add(t, Estop, 0);
			t = polls(t, Hz, Eplaying);
			break;
		case 2:
			t = polls(t, 20*Hz, Eloop);
			break;
		case 3:
			t = polls(t, 5*Hz, Eplaying);
			add(t, Eplay, n);
			add(t, Emax, rnd() % 256);
			t = polls(t, 5*Hz, Eplaying);
			add(t, Emax, 100);
			add(t, Evolume, 0);
			break;
		}
	}
	add(t, Eclear, 0);
}

/* The game's calls at random periods. */
static void
randomcalls(void)
{
	uint32_t t;
	int i;

	nevents = 0;
	t = 0;
	for (i = 0; i < Rounds; i++) {
		t += rnd() % Hz;
		switch (rnd() % 10) {
		case 0:
		case 1:
		case 2:
			add(t, Eplay, anytune());
			break;
		case 3:
			add(t, Estop, 0);
			break;
		case 4:
			add(t, Efade, 1);
			break;
		case 5:
			add(t, Evolume, 0);
			break;
		case 6:
			add(t, Emax, rnd() % 256);
			break;
		case 7:
			add(t, Eloop, 0);
			break;
		default:
			add(t, Eplaying, 0);
			break;
		}
	}
	add(t, Eclear, 0);
}

/* The logo's tune as PlayMusic starts it, polled for 2 seconds, then ClearMidi. */
static void
keycalls(void)
{
	nevents = 0;
	add(0, Eplay, Jingle);
	add(polls(0, 2*Hz, Eplaying), Eclear, 0);
}

static char*
callname(Event *e)
{
	static char buf[64];

	switch (e->op) {
	case Eplay:
		snprintf(buf, sizeof buf, "PlayMusic(%d)", e->arg);
		break;
	case Estop:
		return "StopMidi";
	case Efade:
		return "FadeMidiDown(1)";
	case Evolume:
		return "VolumeMidi(100)";
	case Emax:
		snprintf(buf, sizeof buf, "MaxVolume %d, VolumeMidi(100)", e->arg);
		break;
	case Eloop:
		return "DoLoopMidi";
	case Eplaying:
		return "IsMidiPlaying";
	default:
		return "ClearMidi";
	}
	return buf;
}

/* Call i and its period, or the start before the first. */
static char*
callof(int i)
{
	static char buf[128];

	if (i < 0)
		return "the start";
	snprintf(buf, sizeof buf, "%s at period %u", callname(&events[i]), events[i].tick);
	return buf;
}

/* The call last made by period t, or the start. */
static char*
callat(uint32_t t)
{
	int lo, hi, mid;

	lo = 0;
	hi = nevents;
	while (lo < hi) {
		mid = (lo + hi) / 2;
		if (events[mid].tick <= t)
			lo = mid + 1;
		else
			hi = mid;
	}
	return callof(lo - 1);
}

static int32_t
asplaying(void)
{
	int32_t r;

	r = acall(as.ismidiplaying, 0);
	record(Playing, r);
	return r;
}

static void
ascall(Event *e, int *numxmi)
{
	switch (e->op) {
	case Eplay:
		if (e->arg != *numxmi || !asplaying()) {
			acall(as.stopmidi, 0);
			acall(as.playmidi, 1, (int32_t)(uintptr_t)xmi[e->arg]);
			acall(as.volumemidi, 1, 100);
			*numxmi = e->arg;
		}
		break;
	case Estop:
		acall(as.stopmidi, 0);
		*numxmi = -1;
		break;
	case Efade:
		acall(as.fademididown, 1, e->arg);
		*numxmi = -1;
		break;
	case Emax:
		*as.maxvolume = e->arg;
		acall(as.volumemidi, 1, 100);
		break;
	case Evolume:
		acall(as.volumemidi, 1, 100);
		break;
	case Eloop:
		acall(as.doloopmidi, 0);
		break;
	case Eplaying:
		asplaying();
		break;
	case Eclear:
		acall(as.clearmidi, 0);
		break;
	}
}

static int32_t
ourplaying(void)
{
	int32_t r;

	r = IsMidiPlaying();
	record(Playing, r);
	return r;
}

static void
ourcall(Event *e, int *numxmi)
{
	switch (e->op) {
	case Eplay:
		if (e->arg != *numxmi || !ourplaying()) {
			StopMidi();
			PlayMidi(xmi[e->arg]);
			VolumeMidi(100);
			*numxmi = e->arg;
		}
		break;
	case Estop:
		StopMidi();
		*numxmi = -1;
		break;
	case Efade:
		FadeMidiDown(e->arg);
		*numxmi = -1;
		break;
	case Emax:
		MaxVolume = e->arg;
		VolumeMidi(100);
		break;
	case Evolume:
		VolumeMidi(100);
		break;
	case Eloop:
		DoLoopMidi();
		break;
	case Eplaying:
		ourplaying();
		break;
	case Eclear:
		ClearMidi();
		break;
	}
}

/* AskMidiVars's names, the keys the port reads, each in its turn: 1 if they are. */
static int
names(uint32_t ids)
{
	static char *want[] = {"MidiBase", "MidiIRQ", "MidiDMA", ""};
	char *s;
	int i;

	for (i = 0; i < 4; i++) {
		s = (char*)(uintptr_t)get32((uint8_t*)(uintptr_t)ids + 4*i);
		if (strcmp(s, want[i]) != 0) {
			fail("its name %d \"%s\", the port's \"%s\"", i, s, want[i]);
			return 0;
		}
	}
	return 1;
}

/* IRQ 0, as the CPU calls AIL's handler at the end of one of the PIT's counts. */
static void
irq(uintptr_t fn)
{
	int e, c;

	e = neoi;
	c = chained(8);
	intcall(fn);
	intok("IRQ 0");
	if (neoi - e != 1 + chained(8) - c)
		fail("IRQ 0 sends %d ends of interrupt, the BIOS's handler chained to %d times", neoi - e, chained(8) - c);
	cur->bios += chained(8) - c;
}

/* The assembly's side: the game's start, then the calls, the periods served between. */
static void
asside(void *v)
{
	uint32_t ids, vars, sel0, off0, sel, off;
	uint8_t *d;
	int32_t r, h;
	Event *e;
	int numxmi, i;

	USED(v);
	cur = &sides[0];
	tick = 0;
	*as.extender = Extender;
	dosfsinit(dir);
	if (as.initfiles != 0) {
		snprintf(what, sizeof what, "__InitFiles");
		acall(as.initfiles, 0);
	}
	pcvector(8, &sel0, &off0);
	snprintf(what, sizeof what, "InitMidiDLL(\"A32SP2FM.DLL\")");
	r = acall(as.initmididll, 1, (int32_t)(uintptr_t)"A32SP2FM.DLL");
	if (r != 1) {
		fail("gives %d", r);
		return;
	}
	snprintf(what, sizeof what, "AskMidiVars");
	acall(as.askmidivars, 2, (int32_t)(uintptr_t)&ids, (int32_t)(uintptr_t)&vars);
	if (!names(ids))
		return;
	for (i = 0; i < 3; i++)
		put32((uint8_t*)(uintptr_t)vars + 4*i, keys[i]);
	snprintf(what, sizeof what, "InitMidi");
	r = acall(as.initmidi, 0);
	if (r != 1) {
		fail("gives %d", r);
		return;
	}
	d = (uint8_t*)(uintptr_t)*as.desc;
	for (i = 0; i < 4; i++)
		record(Desc + i, get32(d + 0x10 + 4*i));
	snprintf(what, sizeof what, "the game's timer given to AIL");
	h = acall(as.registertimer, 1, (int32_t)(uintptr_t)as.newproc08);
	acall(as.settimerfrequency, 2, h, 50);
	acall(as.starttimer, 1, h);
	if (cur->divisor != Divisor) {
		fail("the PIT counts %u, the port's period %u", cur->divisor, Divisor);
		return;
	}
	pcvector(8, &sel, &off);
	numxmi = -1;
	for (e = events; e < events + nevents && nfail == 0; e++) {
		for (; tick < e->tick; cur->ticks++) {
			tick++;
			snprintf(what, sizeof what, "period %u, after %s", tick, callat(tick - 1));
			irq(off);
		}
		snprintf(what, sizeof what, "period %u, %s", tick, callname(e));
		record(Call, e - events);
		ascall(e, &numxmi);
	}
	cur->timerref = *as.timerref;
	pcvector(8, &sel, &off);
	if (sel != sel0 || off != off0)
		fail("ClearMidi leaves vector 8 at %x:%x, the BIOS's %x:%x", sel, off, sel0, off0);
	if (cur->divisor != 0)
		fail("ClearMidi leaves the PIT's count %u, the BIOS's 0", cur->divisor);
}

/* Our side, the same calls, the periods served by the clock. */
static void
ourside(void *v)
{
	Event *e;
	int numxmi;

	USED(v);
	cur = &sides[1];
	tick = 0;
	Midi_Driver_Enable = midiinit();
	if (!Midi_Driver_Enable) {
		fail("midiinit gives 0");
		return;
	}
	record(Desc, opl3.desc->io);
	record(Desc + 1, opl3.desc->irq);
	record(Desc + 2, opl3.desc->dma);
	record(Desc + 3, opl3.desc->drq);
	numxmi = -1;
	for (e = events; e < events + nevents; e++) {
		for (; tick < e->tick; cur->ticks++) {
			tick++;
			miditick((uint64_t)tick * Divisor * 1000000000ull / Pithz);
		}
		record(Call, e - events);
		ourcall(e, &numxmi);
	}
}

static char*
recname(Rec *r)
{
	static char *field[] = {"base", "IRQ", "DMA", "DRQ"};
	static char buf[2][64];
	static int k;

	k ^= 1;
	if (r->reg == Playing)
		snprintf(buf[k], sizeof buf[k], "IsMidiPlaying %d", (int16_t)r->val);
	else if (r->reg >= Desc && r->reg < Desc + 4)
		snprintf(buf[k], sizeof buf[k], "the descriptor's %s %d", field[r->reg - Desc], (int16_t)r->val);
	else if (r->reg == Call)
		snprintf(buf[k], sizeof buf[k], "the call %s", callname(&events[r->val]));
	else
		snprintf(buf[k], sizeof buf[k], "register %03x=%02x", r->reg, r->val);
	return buf[k];
}

/* The two sides' records, the same. */
static void
compare(char *part)
{
	Side *a, *p;
	Rec *x, *y;
	uint32_t i, n, nw, na, port;
	int last;

	snprintf(where, sizeof where, "%s", part);
	snprintf(what, sizeof what, "the records");
	nfail = 0;
	a = &sides[0];
	p = &sides[1];
	n = a->n < p->n ? a->n : p->n;
	nw = na = 0;
	last = -1;
	for (i = 0; i < n; i++) {
		x = &a->r[i];
		y = &p->r[i];
		if (x->tick != y->tick || x->reg != y->reg || x->val != y->val) {
			fail("record %u, after %s: the assembly's %s at period %u, ours %s at period %u",
				i, callof(last), recname(x), x->tick, recname(y), y->tick);
			break;
		}
		if (x->reg == Call)
			last = x->val;
		nw += x->reg < Playing;
		na += x->reg == Playing;
	}
	if (nfail == 0 && a->n != p->n)
		fail("the assembly's %u records, ours %u", a->n, p->n);
	if (a->timerref != (uint64_t)a->ticks * Period / Gameus)
		fail("the game's timer ticks %u times, AIL's counter %u", a->timerref,
			(uint32_t)((uint64_t)a->ticks * Period / Gameus));
	if (a->bios != (uint64_t)a->ticks * Period / Biosus)
		fail("the BIOS's handler chained to %u times, AIL's counter %u", a->bios,
			(uint32_t)((uint64_t)a->ticks * Period / Biosus));
	port = (uint64_t)a->ticks * Divisor * 50 / Pithz;
	printf("%s: %u periods, %u calls, %u writes, %u answers, %s\n", part, a->ticks, nevents, nw, na,
		nfail == 0 ? "the same" : "differ");
	printf("%s: the game's timer: %u ticks by AIL's counter, %u by the port's clock\n", part, a->timerref, port);
}

/* A part: its calls made on both sides, then compared: 1 if it failed. */
static int
part(char *name, void (*calls)(void))
{
	int bad;

	calls();
	memset(sides, 0, 2*sizeof sides[0]);
	sides[0].r = (Rec*)(sides + 2);
	sides[1].r = sides[0].r + Nrec;
	bad = inchild(name, 600, asside, NULL);
	bad += inchild(name, 600, ourside, NULL);
	if (bad > 0)
		return 1;
	compare(name);
	return nfail > 0;
}

int
main(int argc, char **argv)
{
	/* MidiBase, MidiIRQ and MidiDMA as a user may write them for a Sound Blaster 16, or -1 for the driver's own */
	static int32_t keysets[][3] = {
		{0x220, 5, 1},
		{0x240, 7, 3},
		{0x260, 10, 5},
		{0x280, 2, 0},
		{-1, -1, -1},
	};
	char path[1024], name[64];
	size_t n;
	int nbad, i;

	oracle = "ailoracle";
	if (argc != 4) {
		fprintf(stderr, "usage: ailoracle lba0.elf lba0.sym dir\n");
		return 2;
	}
	dir = argv[3];
	lowend = Dosend;
	pcload(argv[1], argv[2]);
	as.initmididll = lookup("InitMidiDLL");
	as.askmidivars = lookup("AskMidiVars");
	as.initmidi = lookup("InitMidi");
	as.clearmidi = lookup("ClearMidi");
	as.playmidi = lookup("PlayMidi");
	as.stopmidi = lookup("StopMidi");
	as.ismidiplaying = lookup("IsMidiPlaying");
	as.fademididown = lookup("FadeMidiDown");
	as.volumemidi = lookup("VolumeMidi");
	as.doloopmidi = lookup("DoLoopMidi");
	as.registertimer = lookup("_AIL_register_timer");
	as.settimerfrequency = lookup("_AIL_set_timer_frequency");
	as.starttimer = lookup("_AIL_start_timer");
	as.newproc08 = lookup("NewProc08");
	as.initfiles = symaddr("__InitFiles");
	as.timerref = (uint32_t*)lookup("TimerRef");
	as.desc = (uint32_t*)lookup("desc");
	as.maxvolume = (int32_t*)lookup("MaxVolume");
	as.extender = (uint8_t*)lookup("_Extender");
	snprintf(path, sizeof path, "%s/MIDI_SB.HQR", dir);
	ntunes = hqr(path, &xmi);
	events = malloc(Nevent * sizeof events[0]);
	n = 2*sizeof sides[0] + 2*(size_t)Nrec*sizeof(Rec);
	sides = mmap(NULL, n, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
	if (events == NULL || sides == MAP_FAILED)
		fatal("out of memory");
	pcint(0x21, dosfs);
	pcport(Base, Lastbase+15, cardin, cardout);
	pcport(0x40, 0x43, pitin, pitout);
	pcbios(8, biosirq);
	nbad = part("game", gamecalls);
	nbad += part("random", randomcalls);
	for (i = 0; i < (int)nelem(keysets); i++) {
		memmove(keys, keysets[i], sizeof keys);
		cardbase = keys[0] == -1 ? Base : (uint32_t)keys[0];
		if (keys[0] == -1)
			snprintf(name, sizeof name, "keys -1 %d %d", keys[1], keys[2]);
		else
			snprintf(name, sizeof name, "keys %xh %d %d", keys[0], keys[1], keys[2]);
		nbad += part(name, keycalls);
	}
	if (nbad > 0) {
		fprintf(stderr, "ailoracle: %d failed\n", nbad);
		return 1;
	}
	printf("ailoracle: ok\n");
	return 0;
}
