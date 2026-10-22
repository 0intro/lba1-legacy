#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "dat.h"
#include "fns.h"

/*
 * The synthesizer of A32SP2FM.DLL, the CD's XMIDI driver for a Sound
 * Blaster Pro 2 or 16's OPL3: AIL/32's yamaha32.inc assembled for
 * SBPRO2 (YMF262, stereo), as xmidi32.asm includes it. Its procedures
 * keep their names, less the underscores, and mostly their order. Its
 * registers' widths, wraps and signs are kept where they decide a
 * result. A register write goes to oplwrite, array in the high byte of
 * the register's number, and the detection's port I/O, which reads the
 * chip's status, to oplout and oplin.
 *
 * It gives each note a virtual voice (a slot, 20) and each slot one of
 * the chip's 18 two-operator voices, or 6 four-operator pairs, by
 * priority when all are taken. A timbre is a GTL entry of 14 bytes (two
 * operators) or 25 (four), kept in a cache the game gives it.
 */

enum
{
	/* xmidi32.asm's */
	Defpitchl = 0x00,
	Defpitchh = 0x40,
	Nchans = 16,

	/* controllers */
	Modulation = 1,
	Partvolume = 7,
	Panpot = 10,
	Expression = 11,
	Sustain = 64,
	Voiceprotect = 112,
	Timbreprotect = 113,
	Patchbanksel = 114,
	Resetallctrls = 121,
	Allnotesoff = 123,

	/* yamaha32.inc */
	Maxrecchan = 10,
	Mintruechan = 2,
	Maxtimbs = 192,
	Deftcsize = 3584,
	Nvoices = 18,
	N4opvoices = 6,
	Nslots = 20,
	Defpitchrange = 12,
	Defavdepth = 0xc0,
	Rpanthresh = 27,
	Lpanthresh = 100,
	Leftmask = 0xdf,
	Rightmask = 0xef,

	Slotfree = 0,
	Slotkeyon = 1,
	Slotkeyoff = 2,

	Bnkinst = 0,
	Tvinst = 1,
	Tveffect = 2,
	Opl3inst = 3,

	Uallregs = 0xf9,
	Uavekm = 0x80,
	Uksltl = 0x40,
	Uadsr = 0x20,
	Uws = 0x10,
	Ufbc = 0x08,
	Ufreq = 0x01,

	/* a timbre: BNK, OPL3BNK */
	Blength = 0,
	Btranspose = 2,
	Bmodavekm = 3,
	Bmodksltl = 4,
	Bmodad = 5,
	Bmodsr = 6,
	Bmodws = 7,
	Bfbc = 8,
	Bcaravekm = 9,
	Bcarksltl = 10,
	Bcarad = 11,
	Bcarsr = 12,
	Bcarws = 13,
	Omodavekm = 14,
	Omodksltl = 15,
	Omodad = 16,
	Omodsr = 17,
	Omodws = 18,
	Ofbc = 19,
	Ocaravekm = 20,
	Ocarksltl = 21,
	Ocarad = 22,
	Ocarsr = 23,
	Ocarws = 24,
	Sizebnk = 14,
	Sizeopl3bnk = 25,
};

static char devnames[] = "Creative Labs Sound Blaster Pro(TM) FM Sound\0";

static Drvrdesc ddt = {
	200, 3, "OPL", devnames, 0x220, -1, -1, -1, 120, 0,
};

/* the ports: SBPRO's mixer, the chip's status and data */
static uint32_t mixaddr, mixdata, dataout, addrstat;

static uint16_t freqtable[192] = {
	0x02b2, 0x02b4, 0x02b7, 0x02b9, 0x02bc, 0x02be, 0x02c1, 0x02c3, 0x02c6, 0x02c9,
	0x02cb, 0x02ce, 0x02d0, 0x02d3, 0x02d6, 0x02d8, 0x02db, 0x02dd, 0x02e0, 0x02e3,
	0x02e5, 0x02e8, 0x02eb, 0x02ed, 0x02f0, 0x02f3, 0x02f6, 0x02f8, 0x02fb, 0x02fe,
	0x0301, 0x0303, 0x0306, 0x0309, 0x030c, 0x030f, 0x0311, 0x0314, 0x0317, 0x031a,
	0x031d, 0x0320, 0x0323, 0x0326, 0x0329, 0x032b, 0x032e, 0x0331, 0x0334, 0x0337,
	0x033a, 0x033d, 0x0340, 0x0343, 0x0346, 0x0349, 0x034c, 0x034f, 0x0352, 0x0356,
	0x0359, 0x035c, 0x035f, 0x0362, 0x0365, 0x0368, 0x036b, 0x036f, 0x0372, 0x0375,
	0x0378, 0x037b, 0x037f, 0x0382, 0x0385, 0x0388, 0x038c, 0x038f, 0x0392, 0x0395,
	0x0399, 0x039c, 0x039f, 0x03a3, 0x03a6, 0x03a9, 0x03ad, 0x03b0, 0x03b4, 0x03b7,
	0x03bb, 0x03be, 0x03c1, 0x03c5, 0x03c8, 0x03cc, 0x03cf, 0x03d3, 0x03d7, 0x03da,
	0x03de, 0x03e1, 0x03e5, 0x03e8, 0x03ec, 0x03f0, 0x03f3, 0x03f7, 0x03fb, 0x03fe,
	0xfe01, 0xfe03, 0xfe05, 0xfe07, 0xfe08, 0xfe0a, 0xfe0c, 0xfe0e, 0xfe10, 0xfe12,
	0xfe14, 0xfe16, 0xfe18, 0xfe1a, 0xfe1c, 0xfe1e, 0xfe20, 0xfe21, 0xfe23, 0xfe25,
	0xfe27, 0xfe29, 0xfe2b, 0xfe2d, 0xfe2f, 0xfe31, 0xfe34, 0xfe36, 0xfe38, 0xfe3a,
	0xfe3c, 0xfe3e, 0xfe40, 0xfe42, 0xfe44, 0xfe46, 0xfe48, 0xfe4a, 0xfe4c, 0xfe4f,
	0xfe51, 0xfe53, 0xfe55, 0xfe57, 0xfe59, 0xfe5c, 0xfe5e, 0xfe60, 0xfe62, 0xfe64,
	0xfe67, 0xfe69, 0xfe6b, 0xfe6d, 0xfe6f, 0xfe72, 0xfe74, 0xfe76, 0xfe79, 0xfe7b,
	0xfe7d, 0xfe7f, 0xfe82, 0xfe84, 0xfe86, 0xfe89, 0xfe8b, 0xfe8d, 0xfe90, 0xfe92,
	0xfe95, 0xfe97, 0xfe99, 0xfe9c, 0xfe9e, 0xfea1, 0xfea3, 0xfea5, 0xfea8, 0xfeaa,
	0xfead, 0xfeaf,
};

static uint8_t noteoctave[96] = {
	0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,
	1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,
	2,2,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,
	4,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,
	5,5,5,5,6,6,6,6,6,6,6,6,6,6,6,6,7,
	7,7,7,7,7,7,7,7,7,7,7,
};

static uint8_t notehalftone[96] = {
	0,1,2,3,4,5,6,7,8,9,10,11,0,1,2,3,4,
	5,6,7,8,9,10,11,0,1,2,3,4,5,6,7,8,9,
	10,11,0,1,2,3,4,5,6,7,8,9,10,11,0,1,2,
	3,4,5,6,7,8,9,10,11,0,1,2,3,4,5,6,7,
	8,9,10,11,0,1,2,3,4,5,6,7,8,9,10,11,0,
	1,2,3,4,5,6,7,8,9,10,11,
};

/* the registers' values at a reset, from 01 to f5 of each array */
static uint8_t array0init[0xf5] = {
	0x20,0,0,0x60,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
	63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,
	63,63,63,63,63,63,0,0,0,0,0,0,0,0,0,0,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,0,0,0,0,0,0,0,0,0,0,
	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,Defavdepth,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,
};

static uint8_t array1init[0xf5] = {
	0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
	63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,
	63,63,63,63,63,63,0,0,0,0,0,0,0,0,0,0,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,0,0,0,0,0,0,0,0,0,0,
	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,
};

/* and 16 more, op_0's, which a velocity past 127 reads */
static uint8_t velgraph[32] = {
	82,85,88,91,94,97,100,103,106,109,112,115,118,121,124,127,
	0,1,2,6,7,8,12,13,14,18,19,20,24,25,26,30,
};

static uint8_t op0[18] = {0,1,2,6,7,8,12,13,14,18,19,20,24,25,26,30,31,32};
static uint8_t op1[18] = {3,4,5,9,10,11,15,16,17,21,22,23,27,28,29,33,34,35};
static uint8_t opindex[36] = {
	0,1,2,3,4,5,8,9,10,11,12,13,16,17,18,19,20,21,
	0,1,2,3,4,5,8,9,10,11,12,13,16,17,18,19,20,21,
};
static uint8_t oparray[36] = {
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
};
static uint8_t voicenum[18] = {0,1,2,3,4,5,6,7,8,0,1,2,3,4,5,6,7,8};
static uint8_t voicearray[18] = {0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1};
static uint8_t op4base[18] = {1,1,1,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0};
/* alt_voice, and the two bytes after it, alt_op_0's first (updatepriority) */
static uint8_t altvoice[20] = {3,4,5,0,1,2,0xff,0xff,0xff,12,13,14,9,10,11,0xff,0xff,0xff, 6,7};
static uint8_t altop0[18] = {6,7,8,0,1,2,0xff,0xff,0xff,24,25,26,18,19,20,0xff,0xff,0xff};
static uint8_t altop1[18] = {9,10,11,3,4,5,0xff,0xff,0xff,27,28,29,21,22,23,0xff,0xff,0xff};
static uint8_t connsel[18] = {1,2,4,1,2,4,0,0,0,8,16,32,8,16,32,0,0,0};
static uint8_t op4voice[N4opvoices] = {0,1,2,9,10,11};
static uint8_t carrier01[4] = {0,1,2,1};
static uint8_t carrier23[4] = {2,2,2,3};

static uint32_t noteevent;
static uint32_t timbhist[Maxtimbs];
static uint32_t timboffsets[Maxtimbs];
static uint8_t timbbank[Maxtimbs];
static uint8_t timbnum[Maxtimbs];
static uint8_t timbattribs[Maxtimbs];	/* bit 7: in use, 6: protected */

static uint8_t *cachebase;
static uint32_t cacheaddr;	/* its address, as the slots keep their timbres' */
static uint32_t cachesize;
static uint32_t cacheend;

static uint16_t TVaccum;
static uint16_t priaccum;
static uint8_t volupdate;
static int32_t rover2op;
static int32_t rover4op;
static uint8_t connshadow;

/* the slots' timbres, their addresses in the cache as 4 bytes each, for deletelru */
static uint8_t Stimbreoff[4*Nslots];
static uint16_t Sduration[Nslots];
static uint8_t Sstatus[Nslots];
static uint8_t Stype[Nslots];
static uint8_t Svoice[Nslots];
static uint8_t Schannel[Nslots];
static uint8_t Snote[Nslots];
static uint8_t Skeynum[Nslots];
static uint8_t Stranspose[Nslots];
static uint8_t Svelocity[Nslots];
static uint8_t Ssustain[Nslots];
static uint8_t Supdate[Nslots];
static uint8_t SKBFshadow[Nslots];
static uint8_t SBLOCK[Nslots];
static uint8_t SFBC[Nslots];
static uint8_t SKSLTL0[Nslots], SKSLTL1[Nslots];
static uint8_t SAVEKM0[Nslots], SAVEKM1[Nslots];
static uint8_t SAD0[Nslots], SAD1[Nslots];
static uint8_t SSR0[Nslots], SSR1[Nslots];
static uint8_t Sscale01[Nslots];
static uint16_t Swsval[Nslots];
static uint16_t Sm1val[Nslots];
static uint16_t Sm0val[Nslots];
static uint16_t Sfbval[Nslots];
static uint16_t Spval[Nslots];
static uint16_t Sv1val[Nslots];
static uint16_t Sv0val[Nslots];
static uint16_t Sfval[Nslots];
static uint8_t SKSLTL2[Nslots], SKSLTL3[Nslots];
static uint8_t SAVEKM2[Nslots], SAVEKM3[Nslots];
static uint8_t SAD2[Nslots], SAD3[Nslots];
static uint8_t SSR2[Nslots], SSR3[Nslots];
static uint16_t Swsval2[Nslots];
static uint16_t Sm3val[Nslots];
static uint16_t Sm2val[Nslots];
static uint16_t Sv3val[Nslots];
static uint16_t Sv2val[Nslots];
static uint8_t Sscale23[Nslots];

static uint8_t MIDIvol[Nchans];
static uint8_t MIDIpan[Nchans];
static uint8_t MIDIpitchl[Nchans];
static uint8_t MIDIpitchh[Nchans];
static uint8_t MIDIexpress[Nchans];
static uint8_t MIDImod[Nchans];
static uint8_t MIDIsus[Nchans];
static uint8_t MIDIvprot[Nchans];
static uint8_t MIDItimbre[Nchans];
static uint8_t MIDIbank[Nchans];
static uint8_t MIDIprogram[Nchans];
static uint8_t RBStimbres[128];
static uint8_t MIDIvoices[Nchans];
static uint8_t Vchannel[Nvoices];
static uint16_t SVpriority[Nslots];
static uint32_t	sendmidimessage(uint32_t, uint32_t, uint32_t);
static void	updatevoice(int);
static void	releasevoice(int);

static uint32_t
get32(uint8_t *p)
{
	return p[0] | p[1]<<8 | p[2]<<16 | (uint32_t)p[3]<<24;
}

static void
put32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

/* setioparms is set_IO_parms: SBPRO2's mixer, then the chip's status and data ports. */
static void
setioparms(uint32_t io, uint32_t irq, uint32_t dma, uint32_t drq)
{
	USED(irq);
	USED(dma);
	USED(drq);
	mixaddr = io + 4;
	mixdata = io + 5;
	addrstat = io;
	dataout = io + 1;
}

/*
 * The I/O routines. updatereg is yamaha32.inc's update_reg: the value
 * written to the register, the second array's above 0xff.
 */
static void
updatereg(uint32_t reg, uint32_t val)
{
	oplwrite(reg & 0x1ff, val & 0xff);
}

/* writeregister is write_register: an operator cell's register. */
static void
writeregister(uint32_t op, uint32_t base, uint32_t val)
{
	op &= 0xff;
	updatereg((uint32_t)oparray[op]<<8 | (uint8_t)(opindex[op] + base), val);
}

/* sendbyte is send_byte: a voice's register. */
static void
sendbyte(uint32_t voice, uint32_t base, uint32_t val)
{
	voice &= 0xff;
	updatereg((uint32_t)voicearray[voice]<<8 | (uint8_t)(voicenum[voice] + base), val);
}

/* readstatus is read_status: the chip's status. */
static uint32_t
readstatus(void)
{
	return oplin(addrstat) & 0xff;
}

static int32_t	detectadlib(void);

/*
 * detectdevice is detect_device: the chip looked for at the base+8 a
 * Sound Blaster Pro answers at, then the mixer's register 0x0a found to
 * keep its bits 1 and 2, the ports as they were after: 1 if so.
 */
static int32_t
detectdevice(uint32_t io, uint32_t irq, uint32_t dma, uint32_t drq)
{
	uint32_t sdataout, saddrstat, smixdata, smixaddr, a, b, r;

	sdataout = dataout;
	saddrstat = addrstat;
	smixdata = mixdata;
	smixaddr = mixaddr;
	setioparms(io, irq, dma, drq);
	dataout += 8;
	addrstat += 8;
	r = detectadlib();
	if (r != 0) {
		/* out dx,ax: 0x0a to the address, AH's 0 to the data after it */
		oplout(mixaddr, 0x0a);
		oplout(mixaddr + 1, 0);
		a = oplin(mixdata) & 0xff;
		oplout(mixdata, a ^ 6);
		b = (oplin(mixdata) & 0xff) ^ 6;
		oplout(mixdata, a);
		r = b == a;
	}
	mixaddr = smixaddr;
	mixdata = smixdata;
	addrstat = saddrstat;
	dataout = sdataout;
	return r;
}

/* detectsend is detect_send: a register written as detect_Adlib wants it, waited for. */
static void
detectsend(uint32_t reg, uint32_t val)
{
	int i;

	oplout(addrstat, reg & 0xff);
	for (i = 0; i < 6; i++)
		oplin(addrstat);
	oplout(dataout, val & 0xff);
	for (i = 0; i < 42; i++)
		oplin(addrstat);
}

/*
 * detectadlib is detect_Adlib: the timer flags clear at first, then
 * timer 1 started at 0xff and the status read 200 times, its flag and
 * the interrupt's raised: 1 if so.
 */
static int32_t
detectadlib(void)
{
	uint32_t before, after;
	int i;

	detectsend(4, 0x60);
	detectsend(4, 0x80);
	before = readstatus();
	detectsend(2, 0xff);
	detectsend(4, 0x21);
	after = 0;
	for (i = 0; i < 200; i++)
		after = readstatus();
	detectsend(4, 0x60);
	detectsend(4, 0x80);
	return (before & 0xe0) == 0 && (after & 0xe0) == 0xc0;
}

/* resetsynth is reset_synth: OPL3 mode, 18 two-operator voices, every register at rest. */
static void
resetsynth(void)
{
	uint32_t r;

	updatereg(0x105, 1);
	updatereg(0x104, 0);
	connshadow = 0;
	for (r = 1; r <= 0xf5; r++)
		updatereg(r, array0init[r-1]);
	for (r = 0x101; r <= 0x1f5; r++)
		updatereg(r, array1init[r-0x101]);
}

/* shutdownsynth is shutdown_synth: the OPL3's mode off. */
static void
shutdownsynth(void)
{
	updatereg(0x105, 0);
}

/* indextimbre is index_timbre: the cache's index of timbre bank<<8|num, or -1. */
static int32_t
indextimbre(uint32_t gnum)
{
	int32_t i;

	gnum &= 0xffff;
	for (i = 0; i < Maxtimbs; i++)
		if ((timbattribs[i] & 0x80) != 0 && timbbank[i] == (gnum>>8 & 0xff) && timbnum[i] == (gnum & 0xff))
			return i;
	return -1;
}

/* protecttimbre is protect_timbre: the timbre, or with bank and number 0xff all of them, kept. */
static void
protecttimbre(uint32_t bank, uint32_t num)
{
	int32_t i;

	if (((bank & 0xff)<<8 | (num & 0xff)) == 0xffff) {
		for (i = 0; i < Maxtimbs; i++)
			timbattribs[i] |= 0x40;
		return;
	}
	i = indextimbre((bank & 0xff)<<8 | (num & 0xff));
	if (i != -1)
		timbattribs[i] |= 0x40;
}

/* unprotecttimbre is unprotect_timbre. */
static void
unprotecttimbre(uint32_t bank, uint32_t num)
{
	int32_t i;

	if (((bank & 0xff)<<8 | (num & 0xff)) == 0xffff) {
		for (i = 0; i < Maxtimbs; i++)
			timbattribs[i] &= 0xbf;
		return;
	}
	i = indextimbre((bank & 0xff)<<8 | (num & 0xff));
	if (i != -1)
		timbattribs[i] &= 0xbf;
}

/* timbrestatus is timbre_status: the timbre's offset in the cache plus 1, else 0. */
static int32_t
timbrestatus(uint32_t bank, uint32_t num)
{
	int32_t i;

	i = indextimbre((bank & 0xff)<<8 | (num & 0xff));
	if (i == -1)
		return 0;
	return timboffsets[i] + 1;
}

/* getcachesize is get_cache_size: the timbre cache it wants. */
static uint32_t
getcachesize(void)
{
	return Deftcsize;
}

/* definecache is define_cache: the timbre cache, empty. */
static void
definecache(void *addr, uint32_t size)
{
	cachebase = addr;
	cacheaddr = (uint32_t)(uintptr_t)addr;
	cachesize = size;
	cacheend = 0;
}

/*
 * getrequest is get_request: the first timbre of the sequence's TIMB
 * chunk outside the cache, bank<<8|num, else -1.
 */
static int32_t
getrequest(int32_t h)
{
	uint8_t *p, *t;
	uint32_t n;

	if (h == -1)
		return -1;
	t = seqtimb(h);
	if (t == NULL || memcmp(t, "TIMB", 4) != 0)
		return -1;
	p = t + 8;
	n = p[0] | p[1]<<8;
	do {
		p += 2;
		if (indextimbre(p[0] | p[1]<<8) == -1)
			return p[0] | p[1]<<8;
	} while (--n != 0);
	return -1;
}

/*
 * deletelru is delete_LRU: the timbre used least lately, unprotected,
 * removed from the cache and the cache packed, the channels and slots
 * using it let go. Its walk of the slots' timbres, their addresses,
 * steps 2 bytes for entries of 4. The game's timbres all fit the cache,
 * so it is dead in the game.
 */
static void
deletelru(void)
{
	int32_t index, i, j;
	uint32_t min, toff, tsize, off;
	uint8_t *t;

	index = -1;
	min = 0xffffffff;
	for (i = 0; i < Maxtimbs; i++) {
		if ((timbattribs[i] & 0x80) == 0 || (timbattribs[i] & 0x40) != 0)
			continue;
		if (timbhist[i] > min)
			continue;
		min = timbhist[i];
		index = i;
	}
	if (index == -1)
		return;
	toff = timboffsets[index];
	t = cachebase + toff;
	tsize = t[Blength] | t[Blength+1]<<8;
	memmove(t, t + tsize, cacheend - (toff + tsize));
	timbattribs[index] = 0;
	cacheend -= tsize;
	for (i = 0; i < Nchans; i++)
		if (MIDItimbre[i] != 0xff && MIDItimbre[i] == index)
			MIDItimbre[i] = 0xff;
	for (i = 0; i < 128; i++)
		if (RBStimbres[i] == index)
			RBStimbres[i] = 0xff;
	for (i = 0; i < Maxtimbs; i++)
		if ((timbattribs[i] & 0x80) != 0 && timboffsets[i] > toff)
			timboffsets[i] -= tsize;
	toff += cacheaddr;
	for (i = 0, j = 0; i < Nslots; i++, j += 2) {
		if (Sstatus[i] == Slotfree)
			continue;
		off = get32(Stimbreoff + j);
		if (off < toff)
			continue;
		if (off == toff) {
			releasevoice(i);
			Sstatus[i] = Slotfree;
			continue;
		}
		put32(Stimbreoff + j, off - tsize);
	}
}

/*
 * installtimbre is install_timbre: the timbre at addr copied into the
 * cache unless there, as many timbres deleted as it takes to fit, and
 * every channel whose program asked for it given it.
 */
static void
installtimbre(uint32_t bank, uint32_t num, uint8_t *addr)
{
	int32_t index, i;
	uint32_t end, len;

	bank &= 0xff;
	num &= 0xff;
	index = indextimbre(bank<<8 | num);
	if (index == -1) {
		if (addr == NULL)
			return;
		for (;;) {
			for (i = 0; i < Maxtimbs; i++)
				if ((timbattribs[i] & 0x80) == 0)
					break;
			if (i < Maxtimbs) {
				len = addr[Blength] | addr[Blength+1]<<8;
				end = len + cacheend;
				if (end <= cachesize)
					break;
			}
			deletelru();
		}
		index = i;
		timbnum[i] = num;
		timbbank[i] = bank;
		timbattribs[i] = 0x80;
		timbhist[i] = noteevent++;
		timboffsets[i] = cacheend;
		memmove(cachebase + cacheend, addr, len);
		cacheend = end;
	}
	for (i = 0; i < Nchans; i++)
		if (MIDIprogram[i] == num && MIDIbank[i] == bank)
			MIDItimbre[i] = index;
}

/* initsynth is init_synth: the timbres, channels, slots and voices reset. */
static void
initsynth(void)
{
	int i;

	noteevent = 0;
	for (i = 0; i < Maxtimbs; i++)
		timbattribs[i] = 0;
	for (i = 0; i < Nchans; i++) {
		MIDItimbre[i] = 0xff;
		MIDIvoices[i] = 0;
		MIDIprogram[i] = 0xff;
		MIDIbank[i] = 0;
	}
	for (i = 0; i < Nslots; i++)
		Sstatus[i] = Slotfree;
	for (i = 0; i < Nvoices; i++)
		Vchannel[i] = 0xff;
	memset(RBStimbres, 0xff, sizeof RBStimbres);
	TVaccum = 0;
	priaccum = 0;
	volupdate = 0;
	rover2op = -1;
	rover4op = -1;
}

/*
 * updatepriority is update_priority: while a waiting slot ranks above
 * the lowest voiced one, the latter's voice given to it. A
 * four-operator slot takes the lowest four-operator pair, and when that
 * is a two-operator slot's, the second half's slot is looked up by the
 * slot's number for its voice's. The ranks are worked out once.
 */
static void
updatepriority(void)
{
	int32_t slotcnt, lowp, highp, low4p, s, k;
	uint32_t p, hp, lp, l4p, v;

	slotcnt = 0;
	lowp = highp = low4p = 0;
	for (s = 0; s < Nslots; s++) {
		if (Sstatus[s] == Slotfree)
			continue;
		slotcnt++;
		k = Schannel[s] & 0xf;
		p = 0xffff;
		if ((int8_t)MIDIvprot[k] < 64)
			p = Spval[s];
		if (p < MIDIvoices[k])
			p = 0;
		else
			p -= MIDIvoices[k];
		SVpriority[s] = p;
	}
	for (;;) {
		hp = 0;
		lp = 0xffff;
		l4p = 0xffff;
		for (s = 0; s < Nslots; s++) {
			if (Sstatus[s] == Slotfree)
				continue;
			p = SVpriority[s];
			v = Svoice[s];
			if (v == 0xff) {
				if (p < hp)
					continue;
				hp = p;
				highp = s;
				continue;
			}
			if (op4base[v] != 0 && p <= l4p) {
				l4p = p;
				low4p = s;
			}
			if (p <= lp) {
				lp = p;
				lowp = s;
			}
		}
		if (hp < lp || hp == 0)
			return;
		s = lowp;
		if (Stype[highp] == Opl3inst) {
			s = low4p;
			if (Stype[s] != Opl3inst) {
				for (k = 0; k < Nslots; k++)
					if (Sstatus[k] != Slotfree && Svoice[k] == altvoice[s]) {
						releasevoice(k);
						break;
					}
			}
		}
		v = Svoice[s];
		/*
		 * FIXED: A32SP2FM.DLL takes a slot without a voice where no voice
		 * of four operators is in use, and goes on with voice 0xff, past
		 * its tables. No voice can be given.
		 */
		if (v == 0xff)
			return;
		releasevoice(s);
		Svoice[highp] = v;
		k = Schannel[highp];
		MIDIvoices[k]++;
		Vchannel[v] = k;
		if (Stype[highp] == Opl3inst)
			Vchannel[v+3] = k;
		Supdate[highp] = Uallregs;
		updatevoice(highp);
		if (--slotcnt == 0)
			return;
	}
}

/*
 * assignvoice is assign_voice: the next free voice after the last
 * given, or four-operator pair, or else one taken by priority.
 */
static void
assignvoice(int s)
{
	int32_t n, v;

	if (Stype[s] == Opl3inst) {
		v = rover4op;
		for (n = 0; n < N4opvoices; n++) {
			if (++v == N4opvoices)
				v = 0;
			rover4op = v;
			if (Vchannel[op4voice[v]] == 0xff && Vchannel[op4voice[v]+3] == 0xff)
				break;
		}
		if (n == N4opvoices) {
			updatepriority();
			return;
		}
		v = op4voice[v];
		Svoice[s] = v;
		MIDIvoices[Schannel[s]]++;
		Vchannel[v] = Schannel[s];
		Vchannel[v+3] = Schannel[s];
	} else {
		v = rover2op;
		for (n = 0; n < Nvoices; n++) {
			if (++v == Nvoices)
				v = 0;
			rover2op = v;
			if (Vchannel[v] == 0xff)
				break;
		}
		if (n == Nvoices) {
			updatepriority();
			return;
		}
		Svoice[s] = v;
		MIDIvoices[Schannel[s]]++;
		Vchannel[v] = Schannel[s];
	}
	Supdate[s] = Uallregs;
	updatevoice(s);
}

/*
 * releasevoice is release_voice: the slot's note keyed off and its voice
 * let go, a slot of a BNK or four-operator timbre freed with it.
 */
static void
releasevoice(int s)
{
	uint32_t v;

	if (Svoice[s] == 0xff)
		return;
	SBLOCK[s] &= 0xdf;
	Supdate[s] |= Ufreq;
	updatevoice(s);
	MIDIvoices[Schannel[s]]--;
	v = Svoice[s];
	if (Stype[s] == Opl3inst)
		Vchannel[v+3] = 0xff;
	Vchannel[v] = 0xff;
	Svoice[s] = 0xff;
	if (Stype[s] == Opl3inst || Stype[s] == Bnkinst)
		Sstatus[s] = Slotfree;
}

/* An 8-bit level scaled as update_voice does: (a*b*2)>>8, then 1 more unless 0. */
static uint32_t
scale127(uint32_t a, uint32_t b)
{
	uint32_t v;

	v = (a*b*2 >> 8) & 0xff;
	return v != 0 ? (v+1) & 0xff : 0;
}

/*
 * updatevoice is update_voice: the registers the slot's update flags
 * name, written to its voice, a four-operator voice's second pair first,
 * its frequency left to the first. The levels scale the operators that
 * sound by the channel's volume and expression and the note's velocity.
 * The frequency is the note, transposed, bent by the wheel over a range
 * of 12 halftones, in 16ths of a halftone through freq_table.
 */
static void
updatevoice(int s)
{
	uint32_t voice, voice0, voice1, vol, array, updatesave, scale;
	uint32_t m0val, m1val, v0val, v1val, wsval, fbval, lvl, vib, val, ch, d;
	uint8_t AVEKM0, AVEKM1, KSLTL0, KSLTL1, AD0, AD1, SR0, SR1, FBC, cl;
	int32_t note, oct, ht, idx;
	int16_t pb, x;
	uint16_t ax, f, fnum;
	int8_t blk;

	if (Svoice[s] == 0xff)
		return;
	vol = 0;
	if (Supdate[s] & Uksltl) {
		ch = Schannel[s] & 0xf;
		vol = scale127(MIDIvol[ch], MIDIexpress[ch]);
		vol = scale127(vol, Svelocity[s]);
	}
	array = Stype[s] == Opl3inst;
	voice = Svoice[s];
	cl = connshadow;
	d = connsel[voice];
	if (array) {
		cl |= d;
		if (cl != connshadow) {
			connshadow = cl;
			updatereg(0x104, cl);
		}
	} else {
		cl &= ~d;
		if (cl != connshadow) {
			connshadow = cl;
			updatereg(0x104, cl);
			writeregister(altop0[voice], 0x80, 0x0f);
			writeregister(altop1[voice], 0x80, 0x0f);
			sendbyte(altvoice[voice], 0xb0, 0);
		}
	}
	updatesave = 0;
	if (array) {
		voice = Svoice[s] + 3;
		voice0 = op0[voice];
		voice1 = op1[voice];
		m0val = Sm2val[s];
		m1val = Sm3val[s];
		AVEKM0 = SAVEKM2[s];
		AVEKM1 = SAVEKM3[s];
		v0val = Sv2val[s];
		v1val = Sv3val[s];
		KSLTL0 = SKSLTL2[s];
		KSLTL1 = SKSLTL3[s];
		AD0 = SAD2[s];
		AD1 = SAD3[s];
		SR0 = SSR2[s];
		SR1 = SSR3[s];
		fbval = 0;
		wsval = Swsval2[s];
		FBC = SFBC[s] >> 1;
		scale = Sscale23[s];
		updatesave = Supdate[s];
	} else
		goto array0;
	for (;;) {
		if (Supdate[s] & Uavekm) {
			ch = Schannel[s] & 0xf;
			vib = (int8_t)MIDImod[ch] >= 64 ? 0x40 : 0;
			writeregister(voice0, 0x20, (m0val >> 12 & 0xff) | vib | AVEKM0);
			writeregister(voice1, 0x20, (m1val >> 12 & 0xff) | vib | AVEKM1);
			Supdate[s] &= ~Uavekm;
		}
		if (Supdate[s] & Uksltl) {
			lvl = v0val >> 10 & 0xff;
			if (scale & 1)
				lvl = (lvl*vol & 0xffff) / 127;
			writeregister(voice0, 0x40, (~lvl & 0x3f) | KSLTL0);
			lvl = v1val >> 10 & 0xff;
			if (scale & 2)
				lvl = (lvl*vol & 0xffff) / 127;
			writeregister(voice1, 0x40, (~lvl & 0x3f) | KSLTL1);
			Supdate[s] &= ~Uksltl;
		}
		if (Supdate[s] & Uadsr) {
			writeregister(voice0, 0x60, AD0);
			writeregister(voice1, 0x60, AD1);
			writeregister(voice0, 0x80, SR0);
			writeregister(voice1, 0x80, SR1);
			Supdate[s] &= ~Uadsr;
		}
		if (Supdate[s] & Uws) {
			writeregister(voice1, 0xe0, wsval & 0xff);
			writeregister(voice0, 0xe0, wsval >> 8 & 0xff);
			Supdate[s] &= ~Uws;
		}
		if (Supdate[s] & Ufbc) {
			val = ((fbval >> 12) & 0x0e) | (FBC & 1) | 0x30;
			ch = Schannel[s] & 0xf;
			if (MIDIpan[ch] <= Rpanthresh)
				val &= Rightmask;
			else if (MIDIpan[ch] >= Lpanthresh)
				val &= Leftmask;
			sendbyte(voice, 0xc0, val);
			Supdate[s] &= ~Ufbc;
		}
		if (Supdate[s] & Ufreq) {
			if (array == 1)
				goto endfreq;
			if (Stype[s] == Tveffect) {
				fnum = Sfval[s] >> 6;
				goto setfreq;
			}
			if ((SBLOCK[s] & 0x20) == 0) {
				sendbyte(voice, 0xb0, SKBFshadow[s] & 0xdf);
				goto endfreq;
			}
			ch = Schannel[s];
			pb = (int16_t)(uint16_t)(((uint32_t)MIDIpitchh[ch] << 7 | MIDIpitchl[ch]) - 0x2000);
			pb >>= 5;
			ax = (uint16_t)(pb * Defpitchrange);
			note = Snote[s] + (int8_t)Stranspose[s];
			note -= 24;
			do
				note += 12;
			while (note < 0);
			note += 12;
			do
				note -= 12;
			while (note > 95);
			ax = (uint16_t)(ax + ((uint32_t)note << 8));
			ax = (uint16_t)(ax + 8);
			x = (int16_t)ax >> 4;
			x -= 12 * 16;
			do
				x += 12 * 16;
			while (x < 0);
			x += 12 * 16;
			do
				x -= 12 * 16;
			while (x > 96*16-1);
			idx = (uint16_t)x >> 4;
			ht = notehalftone[idx];
			f = freqtable[ht*16 + (x & 15)];
			oct = noteoctave[idx];
			blk = oct - 1;
			if ((int16_t)f < 0)
				blk++;
			if (blk < 0) {
				blk++;
				f = (uint16_t)((int16_t)f >> 1);
			}
			fnum = (uint16_t)((((f >> 8) & 3) | (uint8_t)(blk << 2)) << 8 | (f & 0xff));
		setfreq:
			sendbyte(voice, 0xa0, fnum & 0xff);
			val = (fnum >> 8 | SBLOCK[s]) & 0xff;
			SKBFshadow[s] = val;
			sendbyte(voice, 0xb0, val);
		endfreq:
			Supdate[s] &= ~Ufreq;
		}
		if (array == 0)
			return;
		array = 0;
		Supdate[s] = updatesave;
	array0:
		voice = Svoice[s];
		voice0 = op0[voice];
		voice1 = op1[voice];
		m0val = Sm0val[s];
		m1val = Sm1val[s];
		AVEKM0 = SAVEKM0[s];
		AVEKM1 = SAVEKM1[s];
		v0val = Sv0val[s];
		v1val = Sv1val[s];
		KSLTL0 = SKSLTL0[s];
		KSLTL1 = SKSLTL1[s];
		AD0 = SAD0[s];
		AD1 = SAD1[s];
		SR0 = SSR0[s];
		SR1 = SSR1[s];
		fbval = Sfbval[s];
		wsval = Swsval[s];
		FBC = SFBC[s];
		scale = Sscale01[s];
	}
}

/*
 * bnkphase is BNK_phase: the slot set up from its timbre's first two
 * operators, keyed on, of middling priority, every register to write.
 */
static void
bnkphase(int s)
{
	uint8_t *t;

	t = cachebase + (get32(Stimbreoff + 4*s) - cacheaddr);
	SBLOCK[s] = 0x20;
	Stype[s] = Bnkinst;
	Sduration[s] = 0xffff;
	Spval[s] = 32767;
	SFBC[s] = t[Bfbc] & 1;
	Sfbval[s] = (uint16_t)(t[Bfbc] << 12);
	SKSLTL0[s] = t[Bmodksltl] & 0xc0;
	Sv0val[s] = (uint16_t)((~t[Bmodksltl] & 0x3f) << 10);
	SKSLTL1[s] = t[Bcarksltl] & 0xc0;
	Sv1val[s] = (uint16_t)((~t[Bcarksltl] & 0x3f) << 10);
	SAVEKM0[s] = t[Bmodavekm] & 0xf0;
	Sm0val[s] = (uint16_t)(t[Bmodavekm] << 12);
	SAVEKM1[s] = t[Bcaravekm] & 0xf0;
	Sm1val[s] = (uint16_t)(t[Bcaravekm] << 12);
	SAD0[s] = t[Bmodad];
	SSR0[s] = t[Bmodsr];
	SAD1[s] = t[Bcarad];
	SSR1[s] = t[Bcarsr];
	Swsval[s] = t[Bmodws]<<8 | t[Bcarws];
	Sscale01[s] = SFBC[s] | 2;
	Supdate[s] = Uallregs;
}

/*
 * oplphase is OPL_phase: as bnkphase, then the second pair from the
 * timbre's next eleven bytes, its connection bit the second of the
 * four-operator algorithm, which says which operators sound.
 */
static void
oplphase(int s)
{
	uint8_t *t;

	bnkphase(s);
	t = cachebase + (get32(Stimbreoff + 4*s) - cacheaddr);
	Stype[s] = Opl3inst;
	SFBC[s] |= (t[Bfbc] & 0x80) >> 6;
	Sscale01[s] = carrier01[SFBC[s]];
	Sscale23[s] = carrier23[SFBC[s]];
	SKSLTL2[s] = t[Omodksltl] & 0xc0;
	Sv2val[s] = (uint16_t)((~t[Omodksltl] & 0x3f) << 10);
	SKSLTL3[s] = t[Ocarksltl] & 0xc0;
	Sv3val[s] = (uint16_t)((~t[Ocarksltl] & 0x3f) << 10);
	SAVEKM2[s] = t[Omodavekm] & 0xf0;
	Sm2val[s] = (uint16_t)(t[Omodavekm] << 12);
	SAVEKM3[s] = t[Ocaravekm] & 0xf0;
	Sm3val[s] = (uint16_t)(t[Ocaravekm] << 12);
	SAD2[s] = t[Omodad];
	SSR2[s] = t[Omodsr];
	SAD3[s] = t[Ocarad];
	SSR3[s] = t[Ocarsr];
	Swsval2[s] = t[Omodws]<<8 | t[Ocarws];
}

/*
 * noteoff is note_off: every slot keyed on with the key on the channel
 * released, or marked sustained while the channel's pedal is down.
 */
static void
noteoff(uint32_t ch, uint32_t note)
{
	int s;

	ch &= 0xff;
	note &= 0xff;
	for (s = 0; s < Nslots; s++) {
		if (Sstatus[s] != Slotkeyon || Skeynum[s] != note || Schannel[s] != ch)
			continue;
		if ((int8_t)MIDIsus[ch] >= 64) {
			Ssustain[s] = 1;
			continue;
		}
		if (Stype[s] == Opl3inst || Stype[s] == Bnkinst) {
			releasevoice(s);
			Sstatus[s] = Slotfree;
			continue;
		}
		Sduration[s] = 1;
	}
}

/*
 * noteon is note_on: a free slot given the channel's timbre, or on
 * channel 10 the note's own (bank 127), keyed on and voiced. Channel
 * 10's notes sound at their timbre's note. A timbre of other than 14
 * or 25 bytes leaves the slot as it was but keyed on.
 */
static void
noteon(uint32_t ch, uint32_t note, uint32_t velocity)
{
	uint32_t tim, off, len;
	uint8_t *t;
	int s;

	tim = MIDItimbre[ch];
	if (ch == 9) {
		tim = RBStimbres[note];
		if (tim == 0xff) {
			tim = indextimbre(127<<8 | note) & 0xff;
			RBStimbres[note] = tim;
		}
	}
	if (tim == 0xff)
		return;
	off = timboffsets[tim];
	t = cachebase + off;
	timbhist[tim] = ++noteevent;
	for (s = 0; s < Nslots; s++)
		if (Sstatus[s] == Slotfree)
			break;
	if (s == Nslots)
		return;
	Schannel[s] = ch;
	Skeynum[s] = note;
	if (ch == 9) {
		Snote[s] = t[Btranspose];
		Stranspose[s] = 0;
	} else {
		Snote[s] = note;
		Stranspose[s] = t[Btranspose];
	}
	Svelocity[s] = velgraph[(velocity & 0xff) >> 3];
	put32(Stimbreoff + 4*s, cacheaddr + off);
	Sstatus[s] = Slotkeyon;
	Ssustain[s] = 0;
	len = t[Blength] | t[Blength+1]<<8;
	if (len == Sizeopl3bnk)
		oplphase(s);
	else if (len == Sizebnk)
		bnkphase(s);
	else
		return;
	Svoice[s] = 0xff;
	assignvoice(s);
}

/*
 * releasesustain is release_sustain: the channel's sustained notes
 * turned off, by the note each sounds rather than its key.
 */
static void
releasesustain(uint32_t ch)
{
	int s;

	for (s = 0; s < Nslots; s++)
		if (Sstatus[s] != Slotfree && Schannel[s] == (ch & 0xff) && Ssustain[s] != 0)
			noteoff(ch, Snote[s]);
}

/* The slots of the channel, their flags raised, updated. */
static void
flagupdates(uint32_t ch, uint32_t flags)
{
	int s;

	for (s = 0; s < Nslots; s++)
		if (Sstatus[s] != Slotfree && Schannel[s] == ch) {
			Supdate[s] |= flags;
			updatevoice(s);
		}
}

/*
 * sendmidimessage is send_MIDI_message: a MIDI channel voice message
 * played on the synthesizer. Notes sound on channels 2 to 10 alone.
 * What it leaves in EAX, send_cv_msg gives back and its callers drop.
 */
static uint32_t
sendmidimessage(uint32_t stat, uint32_t d1, uint32_t d2)
{
	uint32_t ch, st, t;
	int s;

	d1 &= 0xff;
	ch = stat & 0xf;
	st = stat & 0xf0;
	d2 &= 0xff;
	switch (st) {
	case 0x90:
		if (ch < Mintruechan-1 || ch > Maxrecchan-1)
			return 0;
		if (d2 != 0)
			noteon(ch, d1, d2);
		else
			noteoff(ch, d1);
		return 0;
	case 0x80:
		noteoff(ch, d1);
		return 0;
	case 0xe0:
		MIDIpitchl[ch] = d1;
		MIDIpitchh[ch] = d2;
		flagupdates(ch, Ufreq);
		return 0;
	case 0xc0:
		MIDIprogram[ch] = d1;
		MIDItimbre[ch] = indextimbre((uint32_t)MIDIbank[ch]<<8 | d1);
		return 0;
	case 0xb0:
		break;
	default:
		return 0;
	}
	switch (d1) {
	case Patchbanksel:
		MIDIbank[ch] = d2;
		return 0;
	case Voiceprotect:
		MIDIvprot[ch] = d2;
		return 0;
	case Timbreprotect:
		t = MIDItimbre[ch];
		if (t == 0xff)
			return 0;
		timbattribs[t] &= 0xbf;
		if ((int8_t)d2 >= 64)
			timbattribs[t] |= 0x40;
		return 0;
	case Modulation:
		MIDImod[ch] = d2;
		flagupdates(ch, Uavekm);
		return 0;
	case Partvolume:
		MIDIvol[ch] = d2;
		flagupdates(ch, Uksltl);
		return 0;
	case Expression:
		MIDIexpress[ch] = d2;
		flagupdates(ch, Uksltl);
		return 0;
	case Panpot:
		MIDIpan[ch] = d2;
		flagupdates(ch, Ufbc);
		return 0;
	case Sustain:
		MIDIsus[ch] = d2;
		if ((int8_t)d2 < 64)
			releasesustain(ch);
		return 0;
	case Allnotesoff:
		for (s = 0; s < Nslots; s++)
			if (Sstatus[s] == Slotkeyon && Schannel[s] == ch)
				noteoff(ch, Snote[s]);
		return 0;
	case Resetallctrls:
		MIDIsus[ch] = 0;
		releasesustain(ch);
		MIDImod[ch] = 0;
		MIDIexpress[ch] = 127;
		MIDIpitchl[ch] = Defpitchl;
		MIDIpitchh[ch] = Defpitchh;
		flagupdates(ch, Uavekm|Uksltl|Ufreq);
		return 0;
	}
	return 0;
}

/* sendcvmsg is send_cv_msg. */
static uint32_t
sendcvmsg(uint32_t stat, uint32_t d1, uint32_t d2)
{
	return sendmidimessage(stat, d1, d2);
}

/* sendsysexmsg is send_sysex_msg: an empty frame. */
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

/* writedisplay is write_display: an empty frame. */
static void
writedisplay(char *s)
{
	USED(s);
}

/* n bytes of v, the lowest first. */
static uint8_t*
putle(uint8_t *p, uint32_t v, int n)
{
	while (n-- > 0) {
		*p++ = v;
		v >>= 8;
	}
	return p;
}

static uint8_t*
putbytes(uint8_t *p, uint8_t *b, int n)
{
	memmove(p, b, n);
	return p + n;
}

static uint8_t*
putwords(uint8_t *p, uint16_t *w, int n)
{
	int i;

	for (i = 0; i < n; i++)
		p = putle(p, w[i], 2);
	return p;
}

/*
 * yamahastate: the synthesizer's variables into p as A32SP2FM.DLL lays
 * them out from 0x106eb, 0xed4 bytes, the cache's address as 32 bits,
 * for the oracle, which compares them with the DLL's.
 */
void
yamahastate(uint8_t *p)
{
	static uint8_t *bytes1[] = {
		Sstatus, Stype, Svoice, Schannel, Snote, Skeynum, Stranspose,
		Svelocity, Ssustain, Supdate, SKBFshadow, SBLOCK, SFBC, SKSLTL0,
		SKSLTL1, SAVEKM0, SAVEKM1, SAD0, SAD1, SSR0, SSR1, Sscale01,
	};
	static uint16_t *words1[] = {Swsval, Sm1val, Sm0val, Sfbval, Spval, Sv1val, Sv0val, Sfval};
	static uint8_t *bytes2[] = {SKSLTL2, SKSLTL3, SAVEKM2, SAVEKM3, SAD2, SAD3, SSR2, SSR3};
	static uint16_t *words2[] = {Swsval2, Sm3val, Sm2val, Sv3val, Sv2val};
	static uint8_t *chans[] = {
		MIDIvol, MIDIpan, MIDIpitchl, MIDIpitchh, MIDIexpress, MIDImod,
		MIDIsus, MIDIvprot, MIDItimbre, MIDIbank, MIDIprogram,
	};
	int i;

	p = putle(p, mixaddr, 4);
	p = putle(p, mixdata, 4);
	p = putle(p, dataout, 4);
	p = putle(p, addrstat, 4);
	p = putle(p, noteevent, 4);
	for (i = 0; i < Maxtimbs; i++)
		p = putle(p, timbhist[i], 4);
	for (i = 0; i < Maxtimbs; i++)
		p = putle(p, timboffsets[i], 4);
	p = putbytes(p, timbbank, Maxtimbs);
	p = putbytes(p, timbnum, Maxtimbs);
	p = putbytes(p, timbattribs, Maxtimbs);
	p = putle(p, cacheaddr, 4);
	p = putle(p, cachesize, 4);
	p = putle(p, cacheend, 4);
	p = putle(p, TVaccum, 2);
	p = putle(p, priaccum, 2);
	p = putle(p, volupdate, 1);
	p = putle(p, rover2op, 4);
	p = putle(p, rover4op, 4);
	p = putle(p, connshadow, 1);
	p = putbytes(p, Stimbreoff, sizeof Stimbreoff);
	p = putwords(p, Sduration, Nslots);
	for (i = 0; i < (int)nelem(bytes1); i++)
		p = putbytes(p, bytes1[i], Nslots);
	for (i = 0; i < (int)nelem(words1); i++)
		p = putwords(p, words1[i], Nslots);
	for (i = 0; i < (int)nelem(bytes2); i++)
		p = putbytes(p, bytes2[i], Nslots);
	for (i = 0; i < (int)nelem(words2); i++)
		p = putwords(p, words2[i], Nslots);
	p = putbytes(p, Sscale23, Nslots);
	for (i = 0; i < (int)nelem(chans); i++)
		p = putbytes(p, chans[i], Nchans);
	p = putbytes(p, RBStimbres, sizeof RBStimbres);
	p = putbytes(p, MIDIvoices, Nchans);
	p = putbytes(p, Vchannel, Nvoices);
	putwords(p, SVpriority, Nslots);
}

Synth opl3 = {
	.desc = &ddt,
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
