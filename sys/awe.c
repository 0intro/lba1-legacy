#include <stddef.h>
#include <stdint.h>
#include "dat.h"
#include "fns.h"

/*
 * The synthesizer of A32GFX.DLL, the CD's XMIDI driver for a Sound
 * Blaster AWE32, decompiled. Its sequencer is xmidi32.asm,
 * A32SP2FM.DLL's byte for byte (xmidi.c). Around it AIL's glue,
 * yamaha32.inc's skeleton assembled for SBPRO2, calls Creative's AWE32
 * library of 1994 (Watcom C), which drives the EMU8000 through its
 * ports, from the base set_IO_parms is given: Data0 at B+0x400, Data1
 * B+0x800, Data2 B+0x802, Data3 B+0xC00, the pointer B+0xC02. Every
 * function of the library is here, in its order, naming its address, the
 * dead ones too. The widths, signs and wraps of its arithmetic are kept
 * where they decide a result. Its data is in awedata.c, and its port
 * I/O goes to aweout and awein.
 */

enum
{
	/* the port table's slots */
	Data0 = 2,
	Data1 = 4,
	Data2 = 5,
	Data3 = 6,
	Pointer = 7,

	/* the EMU8000's registers, by number, their channel saying which */
	Cpf = 0, Ptrx = 1, Cvcf = 2, Vtft = 3, Z2 = 4, Z1 = 5, Psst = 6, Csl = 7,	/* Data0 */
	Ccca = 0, Hwcf = 1, Init1 = 2, Init3 = 3,	/* Data1 (Hwcf: HWCF4 to 6, HWCF1 to 3, by channel) */
	Envvol = 4, Dcysusv = 5, Envval = 6, Dcysus = 7,	/* Data1 */
	Init2 = 2, Init4 = 3, Atkhldv = 4, Lfo1val = 5, Atkhld = 6, Lfo2val = 7,	/* Data2 */
	Wc = 1,	/* Data2, channel 27 */
	Ip = 0, Ifatn = 1, Pefe = 2, Fmmod = 3, Tremfrq = 4, Fm2frq2 = 5, U7 = 7,	/* Data3 */

	/* the channel AIL's shutdown_synth gives the library: the low word of its return address (0x10265) */
	Shutdownchan = 0x0265,
};

static int32_t	detect(void);
static uint32_t	awereset(void);
static uint32_t	aweterminate(void);
static uint32_t	awemidi(uint32_t, uint32_t, uint32_t);
static uint32_t	note(uint32_t, uint32_t, uint32_t);
static uint32_t	program(uint32_t, uint32_t);
static uint32_t	ignore(uint32_t, uint32_t);
static uint32_t	pitchbend(uint32_t, uint32_t, uint32_t);
static uint32_t	noteon(uint32_t, uint32_t, uint32_t);
static void	release(uint32_t, uint32_t);
static uint32_t	modulation(uint32_t, uint32_t);
static uint32_t	dataentrymsb(uint32_t, uint32_t);
static uint32_t	volume(uint32_t, uint32_t);
static uint32_t	pan(uint32_t, uint32_t);
static uint32_t	dataentrylsb(uint32_t, uint32_t);
static uint32_t	sustain(uint32_t, uint32_t);
static uint32_t	reverb(uint32_t, uint32_t);
static uint32_t	chorus(uint32_t, uint32_t);
static uint32_t	nrpnlsb(uint32_t, uint32_t);
static uint32_t	nrpnmsb(uint32_t, uint32_t);
static uint32_t	rpnlsb(uint32_t, uint32_t);
static uint32_t	rpnmsb(uint32_t, uint32_t);
static uint32_t	resetctrls(uint32_t, uint32_t);
static uint32_t	allnotesoff(uint32_t, uint32_t);

/* the controllers' handlers, by number (0x12fee) */
static uint32_t (*ctrls[128])(uint32_t, uint32_t) = {
	ignore, modulation, ignore, ignore, ignore, ignore, dataentrymsb, volume,
	ignore, ignore, pan, ignore, ignore, ignore, ignore, ignore,
	ignore, ignore, ignore, ignore, ignore, ignore, ignore, ignore,
	ignore, ignore, ignore, ignore, ignore, ignore, ignore, ignore,
	ignore, ignore, ignore, ignore, ignore, ignore, dataentrylsb, ignore,
	ignore, ignore, ignore, ignore, ignore, ignore, ignore, ignore,
	ignore, ignore, ignore, ignore, ignore, ignore, ignore, ignore,
	ignore, ignore, ignore, ignore, ignore, ignore, ignore, ignore,
	sustain, ignore, ignore, ignore, ignore, ignore, ignore, ignore,
	ignore, ignore, ignore, ignore, ignore, ignore, ignore, ignore,
	ignore, ignore, ignore, ignore, ignore, ignore, ignore, ignore,
	ignore, ignore, ignore, reverb, ignore, chorus, ignore, ignore,
	ignore, ignore, nrpnlsb, nrpnmsb, rpnlsb, rpnmsb, ignore, ignore,
	ignore, ignore, ignore, ignore, ignore, ignore, ignore, ignore,
	ignore, ignore, ignore, ignore, ignore, ignore, ignore, ignore,
	ignore, resetctrls, ignore, allnotesoff, ignore, ignore, ignore, ignore,
};

/*
 * setioparms is A32GFX.DLL 0x101b9, set_IO_parms: yamaha32.inc's ports
 * for SBPRO2, then the EMU8000's table, a base and a base+2 every 0x400.
 */
static void
setioparms(uint32_t io, uint32_t irq, uint32_t dma, uint32_t drq)
{
	uint32_t i;

	USED(irq);
	USED(dma);
	USED(drq);
	awemixaddr = io + 4;
	awemixdata = io + 5;
	aweaddrstat = io;
	awedataout = io + 1;
	for (i = 0; i < 4; i++) {
		aweports[2*i] = io + 0x400*i;
		aweports[2*i+1] = io + 2 + 0x400*i;
	}
}

/* detectdevice is A32GFX.DLL 0x1020d, detect_device: the ports set, the chip looked for. */
static int32_t
detectdevice(uint32_t io, uint32_t irq, uint32_t dma, uint32_t drq)
{
	setioparms(io, irq, dma, drq);
	return detect();
}

/* resetsynth is A32GFX.DLL 0x10240, reset_synth: the chip and the library set up. */
static void
resetsynth(void)
{
	awereset();
}

/* shutdownsynth is A32GFX.DLL 0x1025b, shutdown_synth. */
static void
shutdownsynth(void)
{
	aweterminate();
}

/* sendmidimessage is A32GFX.DLL 0x10276, send_MIDI_message: the three bytes to the library. */
static uint32_t
sendmidimessage(uint32_t stat, uint32_t d1, uint32_t d2)
{
	return awemidi(stat & 0xff, d1 & 0xff, d2 & 0xff);
}

/* sendcvmsg is A32GFX.DLL 0x102aa, send_cv_msg. */
static uint32_t
sendcvmsg(uint32_t stat, uint32_t d1, uint32_t d2)
{
	return sendmidimessage(stat, d1, d2);
}

/* protecttimbre is A32GFX.DLL 0x102ee, protect_timbre: a bare ret. */
static void
protecttimbre(uint32_t bank, uint32_t num)
{
	USED(bank);
	USED(num);
}

/* unprotecttimbre is A32GFX.DLL 0x102ef, unprotect_timbre: a bare ret. */
static void
unprotecttimbre(uint32_t bank, uint32_t num)
{
	USED(bank);
	USED(num);
}

/*
 * timbrestatus is A32GFX.DLL 0x102f0, timbre_status: a bare ret, so what
 * EAX held, AIL's call_driver having jumped through it: its own address.
 */
static int32_t
timbrestatus(uint32_t bank, uint32_t num)
{
	USED(bank);
	USED(num);
	return 0x102f0;
}

/* getcachesize is A32GFX.DLL 0x102f1, get_cache_size: a cache of 0 bytes. */
static uint32_t
getcachesize(void)
{
	return 0;
}

/* definecache is A32GFX.DLL 0x102f7, define_cache: a bare ret. */
static void
definecache(void *addr, uint32_t size)
{
	USED(addr);
	USED(size);
}

/* getrequest is A32GFX.DLL 0x102f8, get_request: -1, the list's end. */
static int32_t
getrequest(int32_t h)
{
	USED(h);
	return -1;
}

/* installtimbre is A32GFX.DLL 0x102fe, install_timbre: a bare ret. */
static void
installtimbre(uint32_t bank, uint32_t num, uint8_t *addr)
{
	USED(bank);
	USED(num);
	USED(addr);
}

/* sendsysexmsg is A32GFX.DLL 0x102ff, send_sysex_msg: an empty frame. */
static void
sendsysexmsg(uint32_t a, uint32_t b, uint32_t c, uint32_t len, uint8_t *data, uint32_t delay)
{
	USED(a);
	USED(b);
	USED(c);
	USED(len);
	USED(data);
	USED(delay);
}

/* writedisplay is A32GFX.DLL 0x10304, write_display: an empty frame. */
static void
writedisplay(char *s)
{
	USED(s);
}

/* initsynth is A32GFX.DLL 0x10309, init_synth: a bare ret. */
static void
initsynth(void)
{
}

/* aweoutpw is A32GFX.DLL 0x1030a, dead: a word to a port, the value's low byte. */
void
aweoutpw(uint32_t port, uint32_t val)
{
	aweout(port & 0xffff, val & 0xff);
}

/* aweinpw is A32GFX.DLL 0x10335, dead: a word from a port. */
uint32_t
aweinpw(uint32_t port)
{
	return awein(port & 0xffff);
}

/*
 * The library. awe32detect is A32GFX.DLL 0x11a94, dead: the port table
 * set again (Pascal), then 0.
 */
uint32_t
awe32detect(uint32_t base)
{
	uint32_t i;

	for (i = 0; i < 4; i++) {
		aweports[2*i] = base + 0x400*i;
		aweports[2*i+1] = base + 2 + 0x400*i;
	}
	return 0;
}

static void	inithardware(void);
static void	initmidi(void);

/* awereset is A32GFX.DLL 0x11ac3: the chip set up, then the MIDI state. */
static uint32_t
awereset(void)
{
	inithardware();
	initmidi();
	return 0;
}

/*
 * aweterminate is A32GFX.DLL 0x11ad0: a channel's notes off, called
 * without its argument.
 */
static uint32_t
aweterminate(void)
{
	allnotesoff(Shutdownchan, 0);
	return 0;
}

/*
 * awemidi is A32GFX.DLL 0x11af6, Pascal: a MIDI message to its handler,
 * a controller's through the table, numbers of 0x80 and up dropped, and
 * 1 for a status outside 0x80 to 0xef.
 */
static uint32_t
awemidi(uint32_t stat, uint32_t d1, uint32_t d2)
{
	uint32_t k, ch;

	k = ((stat & 0xf0) >> 4) - 8;
	ch = stat & 0xf;
	if ((k & 0xffff) > 6)
		return 1;
	switch (k) {
	case 0:
		note(ch, d1 & 0xffff, 0);
		break;
	case 1:
		note(ch, d1 & 0xffff, d2 & 0xffff);
		break;
	case 2:
		ignore(ch, d1 & 0xffff);
		break;
	case 3:
		if (d1 >= 0x80)
			break;
		ctrls[d1](ch, d2 & 0xffff);
		break;
	case 4:
		if ((d1 & 0xffff) >= 0x80)
			break;
		program(ch, d1 & 0xffff);
		break;
	case 5:
		ignore(ch, d1 & 0xffff);
		break;
	case 6:
		pitchbend(ch, d1, d2);
		break;
	}
	return 0;
}

/*
 * initmidi is A32GFX.DLL 0x12586: every channel's controllers at their
 * start, channel 10 on the drum kit, every voice free.
 */
static void
initmidi(void)
{
	Awechan *c;
	int i;

	for (i = 0; i < 16; i++) {
		c = &awestate.chans[i];
		c->pan = 0x80;
		c->sustain = 0;
		c->volume = 100;
		c->modulation = 0;
		c->rpndata = 0x200;
		c->dataentry = 0;
		c->bend = 0;
		c->bank = 0;
		c->banksel = 0;
	}
	for (i = 0; i < 16; i++) {
		awestate.chans[i].bank = 0;
		awestate.chans[i].preset = i == 9 ? 0x80 : 0;
	}
	for (i = 0; i < Awevoices; i++)
		awestate.voices[i].key = 0xff;
}

/* writeword is A32GFX.DLL 0x12638: a register of a channel, a word at a port. */
static void
writeword(uint32_t reg, uint32_t port, uint32_t val, uint32_t ch)
{
	aweout(aweports[Pointer], (reg<<5 | ch) & 0xffff);
	aweout(aweports[port & 0xffff], val & 0xffff);
}

/* readword is A32GFX.DLL 0x12668. */
static uint32_t
readword(uint32_t reg, uint32_t port, uint32_t ch)
{
	aweout(aweports[Pointer], (reg<<5 | ch) & 0xffff);
	return awein(aweports[port & 0xffff]);
}

/* writedword is A32GFX.DLL 0x12691: the low word at the port, the high at the next. */
static void
writedword(uint32_t reg, uint32_t port, uint32_t val, uint32_t ch)
{
	port &= 0xffff;
	aweout(aweports[Pointer], (reg<<5 | ch) & 0xffff);
	aweout(aweports[port], val & 0xffff);
	aweout(aweports[port+1], val >> 16);
}

/* readdword is A32GFX.DLL 0x126d3. */
static uint32_t
readdword(uint32_t reg, uint32_t port, uint32_t ch)
{
	uint32_t lo, hi;

	port &= 0xffff;
	aweout(aweports[Pointer], (reg<<5 | ch) & 0xffff);
	lo = awein(aweports[port]);
	hi = awein(aweports[port+1]);
	return hi<<16 | lo;
}

/*
 * initaudio is A32GFX.DLL 0x12719: of the 30 voices, the envelopes off,
 * every word register zeroed a register at a time, DCYSUSV last.
 */
static void
initaudio(void)
{
	static uint8_t regs[][2] = {
		{Dcysusv, Data1}, {Ip, Data3}, {Ifatn, Data3}, {Pefe, Data3},
		{Fmmod, Data3}, {Tremfrq, Data3}, {Fm2frq2, Data3},
		{Lfo1val, Data2}, {Lfo2val, Data2}, {Envval, Data1},
		{Envvol, Data1}, {Atkhldv, Data2}, {Atkhld, Data2},
		{Dcysus, Data1}, {Dcysusv, Data1},
	};
	uint32_t r, ch;

	for (r = 0; r < nelem(regs); r++)
		for (ch = 0; ch < Awevoices; ch++)
			writeword(regs[r][0], regs[r][1], r == 0 ? 0x80 : 0, ch);
}

/*
 * initregs is A32GFX.DLL 0x128ba: of the 30 voices, every doubleword
 * register zeroed, the filter's state of Data0 5 and 4 the last.
 */
static void
initregs(void)
{
	static uint8_t regs[][2] = {
		{Psst, Data0}, {Csl, Data0}, {Vtft, Data0}, {Ptrx, Data0},
		{Cvcf, Data0}, {Cpf, Data0}, {Ccca, Data1}, {Z1, Data0},
		{Z2, Data0},
	};
	uint32_t r, ch;

	for (r = 0; r < nelem(regs); r++)
		for (ch = 0; ch < Awevoices; ch++)
			writedword(regs[r][0], regs[r][1], 0, ch);
}

/*
 * aweseteq is A32GFX.DLL 0x129b6, dead: the bass (aweeqlevels[1]) and
 * the treble (aweeqlevels[0]) set into the fourth set's INIT4, from a
 * copy of the table on the stack.
 */
void
aweseteq(void)
{
	uint16_t *b, *t;

	b = aweeq[aweeqlevels[1]];
	t = aweeq[aweeqlevels[0]];
	aweinits[3][3][7] = b[0];
	aweinits[3][3][13] = b[1];
	aweinits[3][3][19] = b[2] + t[7] + 0x260;
	aweinits[3][3][17] = t[3];
	aweinits[3][3][25] = t[4];
	aweinits[3][3][15] = t[5];
	aweinits[3][3][23] = t[6];
	aweinits[3][3][27] = b[2] + t[7] + 0x360;
}

/* Until the sample counter has counted 1024 more, exactly. */
static void
waitwc(void)
{
	uint32_t t;

	t = (readword(Wc, Data2, 27) + 0x400) & 0xffff;
	while (readword(Wc, Data2, 27) != t)
		;
}

/*
 * initarrays is A32GFX.DLL 0x12a5e: the four sets of INIT1 to INIT4,
 * 1024 samples after each of the first three, and HWCF4 to HWCF6
 * before the fourth.
 */
static void
initarrays(void)
{
	static uint8_t regs[4][2] = {{Init1, Data1}, {Init2, Data2}, {Init3, Data1}, {Init4, Data2}};
	uint32_t s, a, ch;

	for (s = 0; s < 4; s++) {
		if (s == 3) {
			writedword(Hwcf, Data1, 0, 9);
			writedword(Hwcf, Data1, 0x83, 10);
			writedword(Hwcf, Data1, 0x8000, 13);
		}
		for (a = 0; a < 4; a++)
			for (ch = 0; ch < 32; ch++)
				writeword(regs[a][0], regs[a][1], aweinits[s][a][ch], ch);
		if (s < 3)
			waitwc();
	}
}

/*
 * aweinitfm is A32GFX.DLL 0x12d5b, dead: voices 30 and 31 set to pass
 * the OPL3's left and right through the effects, the high word of
 * voice 30's PTRX written on a fall of the pointer's bit 12.
 */
void
aweinitfm(void)
{
	uint32_t d0, ptr, d1;

	writeword(Dcysusv, Data1, 0x80, 30);
	writeword(Dcysusv, Data1, 0x80, 31);
	writedword(Psst, Data0, 0xffffffe0, 30);
	writedword(Csl, Data0, 0xffffe8, 30);
	writedword(Ptrx, Data0, 0, 30);
	writedword(Cpf, Data0, 0, 30);
	writedword(Ccca, Data1, 0xffffe3, 30);
	writedword(Psst, Data0, 0xfffff0, 31);
	writedword(Csl, Data0, 0xfffff8, 31);
	writedword(Ptrx, Data0, 0xff, 31);
	writedword(Cpf, Data0, 0x8000, 31);
	d0 = aweports[Data0];
	writedword(Ccca, Data1, 0xfffff3, 31);
	ptr = aweports[Pointer];
	d1 = aweports[Data1];
	aweout(ptr, 0x3e);
	aweout(d0, 0);
	while ((awein(ptr) & 0x1000) == 0)
		;
	while ((awein(ptr) & 0x1000) != 0)
		;
	aweout((d0 + 2) & 0xffff, 0x4828);
	aweout(ptr, 0x3c);
	aweout(d1, 0);
	writedword(Vtft, Data0, 0xffffffff, 30);
	writedword(Vtft, Data0, 0xffffffff, 31);
	writeword(Hwcf, Data1, 4, 31);
}

static void	initvoices(void);

/*
 * inithardware is A32GFX.DLL 0x12edc: audio off, the voices, their
 * registers and the INIT arrays set, HWCF2 0x20, audio on.
 */
static void
inithardware(void)
{
	writeword(Hwcf, Data1, 0, 31);
	initaudio();
	initregs();
	initarrays();
	initvoices();
	writeword(Hwcf, Data1, 0x20, 30);
	writeword(Hwcf, Data1, 4, 31);
}

/* initvoices is A32GFX.DLL 0x12f21: every voice free, its channel 0xffff. */
static void
initvoices(void)
{
	int i;

	for (i = 0; i < Awevoices; i++)
		awestate.voices[i].chan = 0xffff;
}

/* awereleaseall is A32GFX.DLL 0x12f3a, dead: every voice released, at rate 15. */
void
awereleaseall(void)
{
	uint32_t ch;

	for (ch = 0; ch < Awevoices; ch++)
		writeword(Dcysusv, Data1, 0x800f, ch);
}

/*
 * detect is A32GFX.DLL 0x12f5b: the EMU8000 recognised by what it reads
 * back (7 of Data3, HWCF1 and HWCF2): 1 if it is there.
 */
static int32_t
detect(void)
{
	if ((readword(U7, Data3, 0) & 0x0c) == 0)
		return 0;
	writeword(Hwcf, Data1, 0x59, 29);
	if ((readword(Hwcf, Data1, 29) & 0x58) == 0)
		return 0;
	if ((readword(Hwcf, Data1, 30) & 1) == 0)
		return 0;
	writeword(Hwcf, Data1, 0x20, 30);
	if ((readword(Hwcf, Data1, 30) & 2) == 0)
		return 0;
	writeword(Hwcf, Data1, 0, 31);
	if ((readword(Hwcf, Data1, 30) & 0x40) != 0)
		return 0;
	return 1;
}

/* awenoteoff is A32GFX.DLL 0x13a66, dead: a key released. */
uint32_t
awenoteoff(uint32_t ch, uint32_t key)
{
	release(ch & 0xffff, key & 0xffff);
	return 0;
}

/* note is A32GFX.DLL 0x13a7d: a note on, or off at velocity 0. */
static uint32_t
note(uint32_t ch, uint32_t key, uint32_t vel)
{
	if ((vel & 0xffff) == 0)
		release(ch & 0xffff, key & 0xffff);
	else
		noteon(ch & 0xffff, key & 0xffff, vel & 0xffff);
	return 0;
}

/* program is A32GFX.DLL 0x13abe: the channel's preset, but channel 10's. */
static uint32_t
program(uint32_t ch, uint32_t p)
{
	Awechan *c;

	if ((ch & 0xffff) == 9)
		return 0;
	c = &awestate.chans[ch & 0xffff];
	c->preset = p;
	c->bank = c->banksel;
	return 0;
}

/* ignore is A32GFX.DLL 0x13ae7: the pressures and the controllers but 14. */
static uint32_t
ignore(uint32_t ch, uint32_t v)
{
	USED(ch);
	USED(v);
	return 0;
}

/*
 * pitchbend is A32GFX.DLL 0x13aea: the bend, from its two data bytes
 * (signed) and the channel's range (2 semitones for 0), in IP's units,
 * each of the channel's voices given its pitch with it.
 */
static uint32_t
pitchbend(uint32_t ch, uint32_t d1, uint32_t d2)
{
	int32_t b, r;
	uint32_t i;
	Awevoice *v;

	ch &= 0xffff;
	b = (int32_t)(int8_t)d2*128 + (int8_t)d1 - 0x2000;
	r = awestate.chans[ch].rpndata >> 8;
	if (r == 0)
		r = 2;
	b = b * 100 * r / 0x2008;
	b = b * 341 / 100;
	awestate.chans[ch].bend = b;
	for (i = 0; i < Awevoices; i++) {
		v = &awestate.voices[i];
		if (v->chan != ch)
			continue;
		writeword(Ip, Data3, (uint16_t)(v->pitch + b), i);
	}
	return 0;
}

/*
 * addparam is A32GFX.DLL 0x13bb0, stdcall: a tag's value into the
 * current layer, added to the addresses and the tune, set otherwise.
 */
static void
addparam(uint32_t val, uint32_t tag)
{
	tag &= 0xff;
	if (tag <= 2)
		aweparse.cur.addr[tag] += val;
	else if (tag == 3)
		aweparse.cur.p[0] += val;
	else
		aweparse.cur.p[tag-3] = val;
}

/*
 * nexttag is A32GFX.DLL 0x13bf7: the value of the tag at the cursor,
 * one byte, two (signed) or three, the cursor past it.
 */
static uint32_t
nexttag(void)
{
	uint8_t *p;

	p = awepresets + aweparse.cursor;
	switch (awetagsize[p[0]]) {
	case 1:
		aweparse.cursor += 2;
		return p[1];
	case 2:
		aweparse.cursor += 3;
		return (int16_t)(p[1] | p[2]<<8);
	default:
		aweparse.cursor += 4;
		return (p[0] | p[1]<<8 | p[2]<<16 | (uint32_t)p[3]<<24) >> 8;
	}
}

/* inrange is A32GFX.DLL 0x13c52, stdcall: x in the range lo | hi<<8. */
static uint32_t
inrange(uint32_t r, uint32_t x)
{
	return (r & 0xff) <= (x & 0xffff) && (x & 0xffff) <= ((r & 0xffff) >> 8);
}

/*
 * layers is A32GFX.DLL 0x13c7d: the channel's preset walked for the key
 * and velocity, a layer made of each zone that holds them: how many.
 * Tags 0x38 open an instrument, 0x37 a zone, 0x39 goes back to the
 * instrument. 0x28 and 0x29 are the key and velocity ranges, 0x32 ends
 * a zone with its three addresses, and the rest are kept, the
 * instrument's and the zone's, and laid over the default layer.
 */
static uint32_t
layers(uint32_t ch, uint32_t key, uint32_t vel)
{
	uint32_t n, end, a, b, c, d, m, v, x, i;
	uint8_t tag;
	Awedir *p;

	n = m = a = b = c = d = 0;
	p = &awedir[awestate.chans[ch].preset];
	aweparse.cursor = p->off;
	end = (p->size & 0x7fff) + aweparse.cursor;
	for (;;) {
		if (aweparse.cursor >= (end & 0xffff))
			return n;
		tag = awepresets[aweparse.cursor];
		switch (tag) {
		case 0x37:
			d = aweparse.cursor;
			d += nexttag();
			a = m;
			continue;
		case 0x38:
			c = aweparse.cursor;
			c += nexttag();
			d = c;
			a = b = m;
			continue;
		case 0x39:
			aweparse.cursor = c;
			continue;
		case 0x28:
		case 0x29:
			v = nexttag();
			x = tag == 0x29 ? vel : key;
			if (!inrange(v & 0xffff, x)) {
				aweparse.cursor = d;
				if (aweparse.cursor == (c & 0xffff))
					m = b;
				continue;
			}
			awemods[m & 0xffff].tag = tag;
			awemods[m & 0xffff].val = v & 0xffff;
			m++;
			continue;
		case 0x32:
			nexttag();
			aweparse.cur = awedeflayer;
			addparam(key, Tkey);
			addparam(vel, Tvel);
			for (i = 0; (i & 0xffff) < (m & 0xffff); i++)
				addparam(awemods[i & 0xffff].val, awemods[i & 0xffff].tag);
			addparam(nexttag(), Tstart);
			addparam(nexttag(), Tloopstart);
			addparam(nexttag(), Tloopend);
			aweparse.layers[n & 0xffff] = aweparse.cur;
			n++;
			m = (d & 0xffff) == (c & 0xffff) ? b : a;
			aweparse.cursor = d;
			continue;
		}
		i = m & 0xffff;
		m++;
		awemods[i].tag = tag;
		awemods[i].val = nexttag();
	}
}

/* aweclamp16 is A32GFX.DLL 0x13ef7, dead: x within lo and hi, as 16-bit signed numbers. */
uint32_t
aweclamp16(uint32_t x, uint32_t lo, uint32_t hi)
{
	if ((int16_t)x > (int16_t)hi)
		return hi;
	if ((int16_t)x < (int16_t)lo)
		return lo;
	return x;
}

static uint32_t	allocvoice(uint32_t, uint32_t, uint32_t);
static int32_t	pitch(int32_t);
static uint32_t	velatten(uint32_t);
static uint32_t	volatten(uint32_t);
static uint32_t	decaycode(uint32_t);

/*
 * The attenuation of a note: of its velocity, the channel's volume and
 * the layer's level, each 0x7f for silence (volume's and note on's).
 */
static uint32_t
attenuation(uint32_t vel, uint32_t vol, uint32_t level)
{
	uint32_t va, vo, t, a;

	va = velatten(vel);
	vo = volatten(vol);
	t = (3u * (uint16_t)(0x7f - level) & 0xffff) >> 3;
	a = (int32_t)(((va + vo + t) & 0xffff) * 8) / 3;
	if ((a & 0xffff) >= 0xff || va == 0x7f || vo == 0x7f || t == 0x7f)
		a = 0xff;
	return a;
}

/*
 * Whether drum kit kit (0x81 to 0x87) plays the key itself (note on's
 * switch, 0x13f69), rather than the standard kit's, 0x80.
 */
static int
kitkey(uint32_t kit, uint32_t k)
{
	switch (kit) {
	case 0x81:
		return k == 0x29 || k == 0x2b || k == 0x2d || k == 0x2f || k == 0x30 || k == 0x32;
	case 0x82:
		return k == 0x24 || k == 0x26 || k == 0x29 || k == 0x2b || k == 0x2d || k == 0x2f || k == 0x30 || k == 0x32;
	case 0x83:
		return k == 0x24 || k == 0x26 || k == 0x28 || k == 0x29 || k == 0x2b || k == 0x2d || k == 0x2f || k == 0x30 || k == 0x32 || k == 0x34;
	case 0x84:
		return (k >= 0x24 && k <= 0x26) || (k >= 0x29 && k <= 0x32);
	case 0x86:
		return k >= 0x26 && k <= 0x28;
	case 0x87:
		return k == 0x24 || (k >= 0x26 && k <= 0x35) || k == 0x39 || k == 0x3b;
	}
	return 1;
}

/* The word a register takes from two of a layer's values, the first high. */
static uint32_t
hilo(uint16_t hi, uint16_t lo)
{
	return ((uint32_t)hi<<8 | lo) & 0xffff;
}

/*
 * noteon is A32GFX.DLL 0x13f32: the key's layers, each given a voice and
 * the registers in the order the chip wants them, DCYSUSV last to start
 * its envelopes. On channel 10 a kit of 0x81 to 0x87 plays some keys
 * with the standard kit's. The velocity's argument doubles as a local:
 * from the second layer on, a voice is given the first layer's, at least
 * 70 when its attack is under 0x7d.
 */
static uint32_t
noteon(uint32_t ch, uint32_t key, uint32_t vel)
{
	uint32_t saved, n, i, v, ip, send, at, code, ls, le, start, pan;
	int32_t p, hold, decay, x;
	Awechan *c;
	Awevoice *vp;
	Layer *l;

	saved = 0;
	c = &awestate.chans[ch];
	if (ch == 9 && (uint16_t)(c->preset - 0x81) <= 6 && !kitkey(c->preset, key)) {
		saved = c->preset;
		c->preset = 0x80;
	}
	n = layers(ch, key, vel);
	if (saved >= 0x81)
		c->preset = saved;
	if ((n & 0xffff) == 0)
		return 1;
	for (i = 0; (int16_t)i < (int16_t)n; i++) {
		l = &aweparse.layers[(int16_t)i];
		v = allocvoice(ch, key, vel);
		if (v == 0xffff)
			return 1;
		if (ch == 9) {
			l->p[Tvolattack-Ttune] = 0x7f;
			l->p[Tvoldelay-Ttune] = (int16_t)0xbfff;
		}
		x = ((int32_t)l->p[Tkey-Ttune] + l->p[Tcoarse-Ttune]) * 100 + l->p[Tfine-Ttune] - (uint16_t)l->p[Troot-Ttune];
		l->p[Ttune-Ttune] = (int16_t)x;
		if (l->p[Tscale-Ttune] == 1)
			l->p[Ttune-Ttune] = l->p[Ttune-Ttune] / 2;
		if (ch != 9) {
			l->p[Tkeyhold-Ttune] = 0;
			hold = (int16_t)(l->p[Tvolhold-Ttune] - (int16_t)(l->p[Tkey-Ttune] - 0x3c) * l->p[Tkeyhold-Ttune]);
			if (hold < 0)
				hold = 0;
			hold = 0x7f - hold / 0x5c;
			l->p[Tkeydecay-Ttune] = 0;
			decay = (uint16_t)l->p[Tvoldecay-Ttune] - (l->p[Tkey-Ttune] - 0x3c) * l->p[Tkeydecay-Ttune];
			if (decay < 0)
				decay = 0;
		} else {
			hold = l->p[Tvolhold-Ttune];
			if (hold < 0)
				hold = 0;
			hold = 0x7f - hold / 0x5c;
			decay = (uint16_t)l->p[Tvoldecay-Ttune];
		}
		code = decaycode(decay) & 0xff;
		start = l->addr[Tstart] - 0x36;
		if (l->p[Tmodes-Ttune] & 1) {
			ls = l->addr[Tloopstart];
			le = l->addr[Tloopend] + 1;
		} else {
			ls = l->addr[Tloopend] + 4;
			le = l->addr[Tloopend] + 0x10;
		}
		p = pitch(l->p[Ttune-Ttune]);
		vp = &awestate.voices[v];
		vp->atten = l->p[Tatten-Ttune];
		vp->pitch = (uint16_t)p;
		ip = p;
		if (c->bend != 0)
			ip = (uint16_t)c->bend + p;
		ip &= 0xffff;
		at = attenuation(l->p[Tvel-Ttune], c->volume, l->p[Tatten-Ttune]);
		vp->release = l->p[Tvolrelease-Ttune];
		vp->modrelease = l->p[Tmodrelease-Ttune];
		if (ch != 9 && l->p[Tvolattack-Ttune] < 0x7d) {
			vel = (uint16_t)l->p[Tvel-Ttune];
			if (vel < 0x46)
				vel = 0x46;
			l->p[Tcutoff-Ttune] = ((int32_t)(int16_t)vel * l->p[Tcutoff-Ttune] + 0x40) / 0x7f;
		}
		writeword(Envvol, Data1, (uint16_t)l->p[Tvoldelay-Ttune], v);
		writeword(Atkhldv, Data2, ((uint32_t)hold<<8 | (uint16_t)l->p[Tvolattack-Ttune]) & 0x7f7f, v);
		writeword(Envval, Data1, (uint16_t)l->p[Tmoddelay-Ttune], v);
		writeword(Atkhld, Data2, hilo(l->p[Tmodhold-Ttune], l->p[Tmodattack-Ttune]), v);
		writeword(Dcysus, Data1, hilo(l->p[Tmodsustain-Ttune], l->p[Tmoddecay-Ttune]), v);
		writeword(Ip, Data3, ip, v);
		send = (uint16_t)(l->p[Treverb-Ttune] + c->reverb);
		if (send > 0xff)
			send = 0xff;
		writedword(Ptrx, Data0, ip<<16 | send<<8 | c->aux, v);
		writeword(Ifatn, Data3, ((uint32_t)(uint16_t)l->p[Tcutoff-Ttune]<<8 | at) & 0xffff, v);
		writeword(Lfo1val, Data2, (uint16_t)l->p[Tlfo1delay-Ttune], v);
		writeword(Lfo2val, Data2, (uint16_t)l->p[Tlfo2delay-Ttune], v);
		writeword(Pefe, Data3, hilo(l->p[Tenvpitch-Ttune], l->p[Tenvcutoff-Ttune]), v);
		writeword(Fmmod, Data3, hilo(l->p[Tlfo1pitch-Ttune] + c->modulation, l->p[Tlfo1cutoff-Ttune]), v);
		writeword(Tremfrq, Data3, hilo(l->p[Tlfo1vol-Ttune], l->p[Tlfo1freq-Ttune]), v);
		writeword(Fm2frq2, Data3, hilo(l->p[Tlfo2pitch-Ttune], l->p[Tlfo2freq-Ttune]), v);
		if (ch == 9)
			pan = (uint16_t)(0xff - 2*(uint16_t)l->p[Tpan-Ttune] - 0x81 + c->pan);
		else
			pan = c->pan;
		if ((int16_t)pan > 0xff)
			pan = 0xff;
		else if ((int16_t)pan < 0)
			pan = 0;
		writedword(Psst, Data0, ls | pan<<24, v);
		send = (uint16_t)(l->p[Tchorus-Ttune] + c->chorus);
		if (send > 0xff)
			send = 0xff;
		writedword(Csl, Data0, le | send<<24, v);
		writedword(Ccca, Data1, (uint32_t)(int32_t)l->p[Tq-Ttune]<<28 | start, v);
		writeword(Dcysusv, Data1, ((uint32_t)(uint16_t)l->p[Tvolsustain-Ttune]<<8 | code) & 0xffff, v);
	}
	return 1;
}

/*
 * release is A32GFX.DLL 0x1464d: the voices of the key released, their
 * envelopes at their release rates, but while the channel's pedal is
 * down. The voices are free either way.
 */
static void
release(uint32_t ch, uint32_t key)
{
	Awevoice *v;
	uint32_t i;

	for (i = 0; i < Awevoices; i++) {
		v = &awestate.voices[i];
		if (v->key != (ch<<8) + key)
			continue;
		if (awestate.chans[ch].sustain != 0) {
			v->key = 0xff;
			continue;
		}
		writeword(Dcysusv, Data1, (uint16_t)(0x8000 + v->release), i);
		v->key = 0xff;
		writeword(Dcysus, Data1, (uint16_t)(0x8000 + v->modrelease), i);
	}
}

/*
 * allocvoice is A32GFX.DLL 0x146df: a voice for the key, the next free
 * one whose volume target is at most 0x7ff, else the next whose target
 * is at most 0x1000, else the next, its envelopes turned off.
 */
static uint32_t
allocvoice(uint32_t ch, uint32_t key, uint32_t vel)
{
	Awevoice *v;
	uint32_t i;

	for (i = 0; i < Awevoices; i++) {
		if (++awestate.rover >= Awevoices)
			awestate.rover = 0;
		if (awestate.voices[awestate.rover].key == 0xff
		&& (readdword(Vtft, Data0, awestate.rover) >> 16) <= 0x7ff)
			goto found;
	}
	for (i = 0; i < Awevoices; i++) {
		if (++awestate.rover >= Awevoices)
			awestate.rover = 0;
		if ((readdword(Vtft, Data0, awestate.rover) >> 16) <= 0x1000)
			goto found;
	}
	if (++awestate.rover >= Awevoices)
		awestate.rover = 0;
found:
	awestate.voice = awestate.rover;
	awestate.key = (ch << 8) + key;
	v = &awestate.voices[awestate.voice];
	v->key = awestate.key;
	v->chan = ch;
	v->vel = vel;
	writeword(Dcysusv, Data1, 0x80, awestate.voice);
	return awestate.voice;
}

/*
 * pitch is A32GFX.DLL 0x14800: cents into IP's units, 0xe000 the
 * sample's own pitch, the octaves above the first lost.
 */
static int32_t
pitch(int32_t c)
{
	int32_t o;

	o = 0;
	if (c >= 0)
		o = c / 1200;
	else
		while (c < 0) {
			o--;
			c += 1200;
		}
	if (o > 1)
		o = 1;
	return (c % 1200 << 12) / 1200 + (o + 14) * 0x1000;
}

/* reverb is A32GFX.DLL 0x14868: controller 91, the reverb send added, 0 to 76. */
static uint32_t
reverb(uint32_t ch, uint32_t v)
{
	awestate.chans[ch].reverb = (uint16_t)(60*v) / 100;
	return 0;
}

/* chorus is A32GFX.DLL 0x1489d: controller 93, the chorus send added. */
static uint32_t
chorus(uint32_t ch, uint32_t v)
{
	awestate.chans[ch].chorus = (uint16_t)(60*v) / 100;
	return 0;
}

/* pan is A32GFX.DLL 0x148d2: controller 10, 0xfe at the left, 0 at the right. */
static uint32_t
pan(uint32_t ch, uint32_t v)
{
	awestate.chans[ch].pan = 0xfe - 2*v;
	return 0;
}

/*
 * volume is A32GFX.DLL 0x148f2: controller 7, the attenuation of each
 * of the channel's held voices worked out again and written under its
 * cutoff.
 */
static uint32_t
volume(uint32_t ch, uint32_t v)
{
	Awevoice *vp;
	uint32_t i, a;

	awestate.chans[ch].volume = v;
	for (i = 0; i < Awevoices; i++) {
		vp = &awestate.voices[i];
		if (vp->chan != ch || vp->key == 0xff)
			continue;
		a = attenuation(vp->vel, awestate.chans[ch].volume, vp->atten);
		a |= readword(Ifatn, Data3, i) & 0xff00;
		writeword(Ifatn, Data3, a & 0xffff, i);
	}
	return 0;
}

/*
 * sustain is A32GFX.DLL 0x149ec: controller 64, the pedal down at 64,
 * and up, the channel's voices that are free released.
 */
static uint32_t
sustain(uint32_t ch, uint32_t v)
{
	Awevoice *vp;
	uint32_t i;

	if ((v & 0xffff) >= 0x40) {
		awestate.chans[ch].sustain = 1;
		return 0;
	}
	awestate.chans[ch].sustain = 0;
	for (i = 0; i < Awevoices; i++) {
		vp = &awestate.voices[i];
		if (vp->chan == ch && vp->key == 0xff)
			writeword(Dcysusv, Data1, (uint16_t)(0x8000 + vp->release), i);
	}
	return 0;
}

/*
 * modulation is A32GFX.DLL 0x14a5f: controller 1, a 30th of it to the
 * channel's vibrato, written at once to its voices' FMMOD, the cutoff's
 * part zeroed.
 */
static uint32_t
modulation(uint32_t ch, uint32_t v)
{
	uint32_t i, m;

	m = (v & 0xffff) / 30;
	awestate.chans[ch].modulation = m;
	for (i = 0; i < Awevoices; i++)
		if (awestate.voices[i].chan == ch)
			writeword(Fmmod, Data3, m<<8 & 0xffff, i);
	return 0;
}

/* rpnlsb is A32GFX.DLL 0x14ac9: controller 100. */
static uint32_t
rpnlsb(uint32_t ch, uint32_t v)
{
	USED(ch);
	aweparse.rpn = (aweparse.rpn & 0xff00) | (v & 0xff);
	return 0;
}

/* rpnmsb is A32GFX.DLL 0x14ae0: controller 101. */
static uint32_t
rpnmsb(uint32_t ch, uint32_t v)
{
	USED(ch);
	aweparse.rpn = (aweparse.rpn & 0x00ff) | (v<<8 & 0xffff);
	return 0;
}

/* nrpnlsb is A32GFX.DLL 0x14af0: controller 98. */
static uint32_t
nrpnlsb(uint32_t ch, uint32_t v)
{
	USED(ch);
	aweparse.nrpn = (aweparse.nrpn & 0xff00) | (v & 0xff);
	return 0;
}

/* nrpnmsb is A32GFX.DLL 0x14b07: controller 99. */
static uint32_t
nrpnmsb(uint32_t ch, uint32_t v)
{
	USED(ch);
	aweparse.nrpn = (aweparse.nrpn & 0x00ff) | (v<<8 & 0xffff);
	return 0;
}

/*
 * dataentrymsb is A32GFX.DLL 0x14b17: controller 6, the high byte of RPN
 * 0 (the bend's range), 1 or 2 (tunings, a dead global). An NRPN
 * only marks the channel.
 */
static uint32_t
dataentrymsb(uint32_t ch, uint32_t v)
{
	Awechan *c;
	uint32_t hi;

	c = &awestate.chans[ch];
	if (aweparse.rpn != 0xffff) {
		if (c->dataentry == 0)
			c->dataentry = 1;
		hi = v<<8 & 0xffff;
		if (aweparse.rpn == 0)
			c->rpndata = (c->rpndata & 0xff) | hi;
		else if (aweparse.rpn == 0x100)
			aweparse.finetune = (aweparse.finetune & 0xff) | hi;
		else if (aweparse.rpn == 0x200)
			aweparse.coarsetune = (aweparse.coarsetune & 0xff) | hi;
	}
	if (aweparse.nrpn != 0xffff && c->dataentry == 0)
		c->dataentry = 1;
	return 0;
}

/* dataentrylsb is A32GFX.DLL 0x14bc0: controller 38, added to the RPN's word. */
static uint32_t
dataentrylsb(uint32_t ch, uint32_t v)
{
	if (aweparse.rpn == 0)
		awestate.chans[ch].rpndata += v;
	else if (aweparse.rpn == 0x100)
		aweparse.finetune += v;
	else if (aweparse.rpn == 0x200)
		aweparse.coarsetune += v;
	return 0;
}

/*
 * resetctrls is A32GFX.DLL 0x14c0e: controller 121, the channel's
 * controllers at their start, but the volume at 127, and the bend and
 * the RPN as they were.
 */
static uint32_t
resetctrls(uint32_t ch, uint32_t v)
{
	Awechan *c;

	USED(v);
	c = &awestate.chans[ch];
	c->reverb = 0;
	c->chorus = 0;
	c->pan = 0x80;
	c->aux = 0;
	c->volume = 0x7f;
	c->modulation = 0;
	c->sustain = 0;
	c->rpndata = 0x200;
	c->dataentry = 0;
	c->bank = 0;
	return 0;
}

/* allnotesoff is A32GFX.DLL 0x14c73: controller 123, the channel's voices released fast. */
static uint32_t
allnotesoff(uint32_t ch, uint32_t v)
{
	uint32_t i;

	USED(v);
	for (i = 0; i < Awevoices; i++)
		if (awestate.voices[i].chan == (ch & 0xffff))
			writeword(Dcysusv, Data1, 0x807f, i);
	return 0;
}

/* velatten is A32GFX.DLL 0x14ca8: a velocity's attenuation. */
static uint32_t
velatten(uint32_t vel)
{
	return aweatten[vel & 0xff] & 0xff;
}

/* volatten is A32GFX.DLL 0x14cb5: a volume's attenuation. */
static uint32_t
volatten(uint32_t vol)
{
	return aweatten[128 + (vol & 0xff)] & 0xff;
}

/* aweclamp127 is A32GFX.DLL 0x14cc2, dead: x within 0 and 127, as a 16-bit signed number. */
uint32_t
aweclamp127(uint32_t x)
{
	if ((int16_t)x > 0x7f)
		return 0x7f;
	if ((int16_t)x < 0)
		return 0;
	return x;
}

/*
 * awesearch is A32GFX.DLL 0x14ce0, dead: the first entry from 1 below x
 * of awesearchtab, its index in the low byte, its high byte above.
 */
uint32_t
awesearch(uint32_t x)
{
	uint32_t i;

	for (i = 1; i < 0x80; i++)
		if (x > awesearchtab[i])
			return (awesearchtab[i] & 0xff00) | i;
	return (awesearchtab[0x7f] & 0xff00) | 0x7f;
}

/* decaycode is A32GFX.DLL 0x14d05: the decay rate whose time is the first below x. */
static uint32_t
decaycode(uint32_t x)
{
	uint32_t i;

	for (i = 0; i < 0x80; i++)
		if (x > awedecaytab[i])
			return (awedecaytab[i] & 0xff00) | i;
	return (awedecaytab[0x7f] & 0xff00) | 0x7f;
}

Synth awe32 = {
	.desc = &aweddt,
	.setioparms = setioparms,
	.reset = resetsynth,
	.init = initsynth,
	.send = sendmidimessage,
	.shutdown = shutdownsynth,
	.detect = detectdevice,
	.cvmsg = sendcvmsg,
	.sysexmsg = sendsysexmsg,
	.display = writedisplay,
	.cachesize = getcachesize,
	.definecache = definecache,
	.request = getrequest,
	.install = installtimbre,
	.protect = protecttimbre,
	.unprotect = unprotecttimbre,
	.status = timbrestatus,
};
