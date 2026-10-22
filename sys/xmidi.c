#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "dat.h"
#include "fns.h"

/*
 * The sequencer of the CD's XMIDI driver, AIL/32's xmidi32.asm, in
 * A32SP2FM.DLL (the OPL3's, yamaha.c), assembled with yamaha32.inc's
 * equates for SBPRO2. The driver's procedures keep their names, without
 * the underscores, and mostly their order. Its registers' widths, wraps
 * and signs are kept where they decide a result. The synthesizer the
 * driver includes is a Synth, given to initdriver.
 *
 * The sequencer reads a sequence's events at 120 intervals a second
 * (servedriver): a byte below 128 waits that many intervals, a note on
 * carries its length and goes to a queue that turns it off, controllers
 * 110 to 120 are XMIDI's own (locks, loops, callbacks). A sequence's
 * state is the C's: its pointers are the host's, 64 bits on amd64.
 */

enum
{
	/* xmidi32.asm */
	Quantrate = 120,
	Quanttime = 8333,
	Quanttime16 = 0x208d5,
	Maxnotes = 32,
	Fornest = 4,
	Nseqs = 8,
	Defpitchl = 0x00,
	Defpitchh = 0x40,
	Nchans = 16,

	Seqstopped = 0,
	Seqplaying = 1,
	Seqdone = 2,

	/* controllers */
	Modulation = 1,
	Partvolume = 7,
	Panpot = 10,
	Expression = 11,
	Sustain = 64,
	Chanlock = 110,
	Chanprotect = 111,
	Voiceprotect = 112,
	Timbreprotect = 113,
	Patchbanksel = 114,
	Indirectcpfx = 115,
	Forloop = 116,
	Nextloop = 117,
	Clearbeatbar = 118,
	Callbacktrig = 119,
	Resetallctrls = 121,
	Allnotesoff = 123,

	/* the controller log, a row of Nchans each */
	PV = 0 * Nchans,
	MODUL = 1 * Nchans,
	PAN = 2 * Nchans,
	EXP = 3 * Nchans,
	SUS = 4 * Nchans,
	PBS = 5 * Nchans,
	CLOCK = 6 * Nchans,
	CPROT = 7 * Nchans,
	VPROT = 8 * Nchans,
	Ncontrols = 9,
	Ctrllog = Ncontrols * Nchans,

	/* yamaha32.inc's, assembled into the sequencer */
	Maxrecchan = 10,
	Maxtruechan = 9,
	Mintruechan = 2,
	Defsynthvol = 100,
};

typedef struct Seq Seq;

/* state_table */
struct Seq
{
	uint8_t	*timb;
	uint8_t	*rbrn;
	uint8_t	*evnt;
	uint8_t	*evntptr;
	int32_t	curcallback;
	uint8_t	*ctrlptr;
	int32_t	handle;
	uint16_t	started;
	uint16_t	status;
	uint16_t	postrelease;
	int16_t	intervalcnt;
	uint16_t	notecount;
	int32_t	volerror;	/* dead in xmidi32.asm too */
	int32_t	volpercent;
	int32_t	voltarget;
	int32_t	volaccum;
	int32_t	volperiod;
	int32_t	tempoerror;
	int32_t	tempopercent;
	int32_t	tempotarget;
	int32_t	tempoaccum;
	int32_t	tempoperiod;
	uint16_t	beatcount;
	uint16_t	measurecount;
	uint16_t	timenumerator;
	int32_t	timefraction;
	int32_t	beatfraction;
	int32_t	timeperbeat;
	uint8_t	*forptrs[Fornest];
	int16_t	forloopcnt[Fornest];
	uint8_t	chanmap[Nchans];
	uint8_t	chanprogram[Nchans];
	uint8_t	chanpitchl[Nchans];
	uint8_t	chanpitchh[Nchans];
	uint8_t	chanindirect[Nchans];
	uint8_t	chancontrols[Ctrllog];
	uint8_t	notechan[Maxnotes];
	uint8_t	notenum[Maxnotes];
	int32_t	notetime[Maxnotes];
};

/* xmidi32.asm's data */

static uint8_t loggedctrls[Ncontrols] = {
	Partvolume, Modulation, Panpot, Expression, Sustain,
	Patchbanksel,
	Chanlock, Chanprotect, Voiceprotect,
};
static uint8_t ctrldefault[Ncontrols] = {127, 0, 64, 127, 0, 0, 0, 0, 0};
static uint8_t prgdefault[] = {68, 48, 95, 78, 41, 3, 110, 122, 0xff};
/* each controller's row in the log, 0xff for none: the image's, before init_driver makes it */
static uint8_t ctrlhash[256] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static Seq *seqstate[Nseqs];
static uint32_t seqcount;
static int32_t curhandle;
static uint16_t serviceactive;

/* global_shadow: the controllers, programs and pitches as last sent */
static uint8_t globalcontrols[Ctrllog];
static uint8_t globalprogram[Nchans];
static uint8_t globalpitchl[Nchans];
static uint8_t globalpitchh[Nchans];

static uint8_t activenotes[Nchans];
static uint8_t lockstatus[Nchans];	/* bit 7: locked, 6: lock-protected */
static uint16_t initok;
static void (*triggerfn)(int32_t, int32_t);
static Synth *synth;

static void	xmidicontrol(Seq*, uint32_t, uint32_t, uint32_t);
static void	releaseseq(int32_t);
static void	flushnotequeue(Seq*);

static uint32_t
get32(uint8_t *p)
{
	return p[0] | p[1]<<8 | p[2]<<16 | (uint32_t)p[3]<<24;
}

/* An IFF chunk's length, big-endian. */
static uint32_t
iff32(uint8_t *p)
{
	return (uint32_t)p[0]<<24 | p[1]<<16 | p[2]<<8 | p[3];
}

/*
 * The sequencer. findseq is xmidi32.asm's find_seq: the num-th FORM
 * XMID of a CAT XMID file, or its one FORM XMID, else NULL.
 */
static uint8_t*
findseq(uint8_t *xmid, uint32_t num)
{
	uint32_t n, len;
	int32_t end;
	uint8_t *p;

	n = num + 1;
	p = xmid;
	for (;;) {
		if (memcmp(p, "CAT ", 4) != 0 && memcmp(p, "FORM", 4) != 0)
			return NULL;
		if (memcmp(p+8, "XMID", 4) == 0)
			break;
		p += iff32(p+4) + 8;
	}
	end = iff32(p+4) - 5;
	if (memcmp(p, "FORM", 4) == 0)
		return n == 1 ? p : NULL;
	p += 12;
	for (;;) {
		if (memcmp(p+8, "XMID", 4) == 0 && --n == 0)
			return p;
		len = iff32(p+4) + 8;
		end -= len;
		if (end < 0)
			return NULL;
		p += len;
	}
}

/* rewindseq is rewind_seq: the sequence at its start, its state as new. */
static void
rewindseq(int32_t h)
{
	Seq *q;
	int i;

	q = seqstate[h/4];
	for (i = 0; i < Fornest; i++)
		q->forloopcnt[i] = -1;
	for (i = 0; i < Nchans; i++) {
		q->chanmap[i] = i;
		q->chanprogram[i] = 0xff;
		q->chanpitchl[i] = 0xff;
		q->chanpitchh[i] = 0xff;
		q->chanindirect[i] = 0xff;
	}
	memset(q->chancontrols, 0xff, sizeof q->chancontrols);
	memset(q->notechan, 0xff, sizeof q->notechan);
	q->curcallback = -1;
	q->intervalcnt = 0;
	q->notecount = 0;
	q->volpercent = Defsynthvol;
	q->voltarget = Defsynthvol;
	q->tempopercent = 100;
	q->tempotarget = 100;
	q->tempoerror = 0;
	q->beatcount = 0;
	q->measurecount = 0xffff;
	q->beatfraction = 0;
	q->timefraction = 0;
	q->timenumerator = 4;
	q->timeperbeat = 0x7a1200;
}

/* A note of the queue at slot i off: its sequence's, through the channel's map. */
static void
queuenoteoff(Seq *q, int i)
{
	uint32_t ch, note;

	ch = q->notechan[i];
	note = q->notenum[i];
	q->notechan[i] = 0xff;
	ch = q->chanmap[ch];
	activenotes[ch]--;
	synth->send(0x80|ch, note, 0);
}

/* flushchannelnotes is flush_channel_notes: every sequence's queued notes on the channel off. */
static void
flushchannelnotes(uint32_t ch)
{
	uint32_t n;
	int32_t h;
	Seq *q;
	int i;

	h = 0;
	for (n = seqcount; n != 0; n--) {
		while (seqstate[h/4] == NULL)
			h += 4;
		q = seqstate[h/4];
		h += 4;
		if (q->notecount == 0)
			continue;
		for (i = 0; i < Maxnotes; i++)
			if (q->notechan[i] == (ch & 0xff)) {
				queuenoteoff(q, i);
				q->notecount--;
			}
	}
}

/* flushnotequeue is flush_note_queue: the sequence's queued notes off. */
static void
flushnotequeue(Seq *q)
{
	int i;

	for (i = 0; i < Maxnotes; i++)
		if (q->notechan[i] != 0xff)
			queuenoteoff(q, i);
	q->notecount = 0;
}

/* resetsequence is reset_sequence: the pedals, locks and protections the sequence holds let go. */
static void
resetsequence(Seq *q)
{
	uint32_t ch;

	for (ch = 0; ch < Nchans; ch++) {
		if ((int8_t)q->chancontrols[SUS+ch] >= 64) {
			globalcontrols[SUS+ch] = 0;
			synth->send(0xb0|ch, Sustain, 0);
		}
		if ((int8_t)q->chancontrols[CLOCK+ch] >= 64) {
			flushchannelnotes(ch);
			releasechannel(q->chanmap[ch] + 1);
			q->chanmap[ch] = ch;
		}
		if ((int8_t)q->chancontrols[CPROT+ch] >= 64)
			lockstatus[ch] &= 0xbf;
		if ((int8_t)q->chancontrols[VPROT+ch] >= 64)
			synth->send(0xb0|ch, Voiceprotect, 0);
	}
}

/*
 * restoresequence is restore_sequence: the channels the sequence locked
 * locked again, its logged controllers sent again (but the locks), its
 * pitches and programs.
 */
static void
restoresequence(Seq *q)
{
	uint32_t c, ch, h;
	int32_t l;

	for (ch = 0; ch < Nchans; ch++) {
		if (q->chancontrols[CLOCK+ch] == 0xff || (int8_t)q->chancontrols[CLOCK+ch] < 64)
			continue;
		l = lockchannel() - 1;
		if (l == -1)
			l = ch;
		q->chanmap[ch] = l;
	}
	for (c = 0; c < Ncontrols; c++) {
		if (loggedctrls[c] == Chanlock)
			continue;
		h = ctrlhash[loggedctrls[c]];
		for (ch = 0; ch < Nchans; ch++)
			if (q->chancontrols[(h + ch) & 0xff] != 0xff)
				xmidicontrol(q, ch, loggedctrls[c], q->chancontrols[(h + ch) & 0xff]);
	}
	for (ch = 0; ch < Nchans; ch++) {
		if (q->chanpitchl[ch] != 0xff && q->chanpitchh[ch] != 0xff)
			synth->send(0xe0 | q->chanmap[ch], q->chanpitchl[ch], q->chanpitchh[ch]);
		if (q->chanprogram[ch] != 0xff)
			synth->send(0xc0 | q->chanmap[ch], q->chanprogram[ch], 0);
	}
}

/* xmidivolume is XMIDI_volume: every channel's volume sent again, scaled by the sequence's. */
static void
xmidivolume(Seq *q)
{
	uint32_t ch, v;

	for (ch = 0; ch < Nchans; ch++) {
		if (q->chancontrols[PV+ch] == 0xff)
			continue;
		v = q->chancontrols[PV+ch] * (uint32_t)(uint16_t)q->volpercent / 100;
		if (v >= 127)
			v = 127;
		globalcontrols[PV+ch] = v;
		if (lockstatus[ch] & 0x80)
			continue;
		synth->send(0xb0|q->chanmap[ch], Partvolume, v);
	}
}

/*
 * xmidicontrol is XMIDI_control: a controller logged, then XMIDI's own
 * done (volume scaled by the sequence's, beat counts cleared, callbacks,
 * FOR and NEXT loops, channel protection, locks, indirection), others
 * sent to the synthesizer through the channel's map.
 */
static void
xmidicontrol(Seq *q, uint32_t ch, uint32_t con, uint32_t val)
{
	uint32_t i, h;
	int32_t l;

	ch &= 0xff;
	con &= 0xff;
	val &= 0xff;
	if (q->chanindirect[ch] != 0xff) {
		i = q->chanindirect[ch];
		q->chanindirect[ch] = 0xff;
		/* FIXED: midi.c gives no table, and the driver reads low memory. Here 0. */
		val = q->ctrlptr != NULL ? q->ctrlptr[i] : 0;
	}
	h = ctrlhash[con];
	if (h != 0xff) {
		h = (h + ch) & 0xff;
		globalcontrols[h] = val;
		q->chancontrols[h] = val;
	}
	switch (con) {
	case Partvolume:
		if (q->volpercent != 100) {
			/* FIXED: the driver's div faults past 32 bits. The low 32 are kept here. */
			val = (uint32_t)((uint64_t)val * (uint32_t)q->volpercent / 100);
			if (val >= 127)
				val = 127;
			globalcontrols[PV+ch] = val;
		}
		break;
	case Clearbeatbar:
		q->beatcount = 0;
		q->measurecount = 0;
		q->beatfraction = 0 - q->timefraction;
		return;
	case Callbacktrig:
		q->curcallback = val;
		if (triggerfn != NULL)
			triggerfn(q->handle, val);
		return;
	case Forloop:
		for (i = 0; i < Fornest; i++)
			if (q->forloopcnt[i] == -1) {
				q->forloopcnt[i] = val;
				q->forptrs[i] = q->evntptr;
				break;
			}
		return;
	case Nextloop:
		if ((int8_t)val < 64)
			return;
		for (l = Fornest - 1; l >= 0; l--)
			if (q->forloopcnt[l] != -1)
				break;
		if (l < 0)
			return;
		if (q->forloopcnt[l] != 0 && --q->forloopcnt[l] == 0) {
			q->forloopcnt[l] = -1;
			return;
		}
		q->evntptr = q->forptrs[l];
		return;
	case Chanprotect:
		lockstatus[ch] |= 0x40;
		if ((int8_t)val < 64)
			lockstatus[ch] &= 0xbf;
		return;
	case Chanlock:
		if ((int8_t)val >= 64) {
			l = lockchannel() - 1;
			if (l == -1)
				l = ch;
			q->chanmap[ch] = l;
			return;
		}
		flushchannelnotes(ch);
		releasechannel(q->chanmap[ch] + 1);
		q->chanmap[ch] = ch;
		return;
	case Indirectcpfx:
		q->chanindirect[ch] = val;
		return;
	}
	if (lockstatus[ch] & 0x80)
		return;
	synth->send(0xb0|q->chanmap[ch], con, val);
}

/*
 * A variable-length number, 7 bits a byte, the first the highest, and
 * p past it.
 */
static uint32_t
vln(uint8_t **pp)
{
	uint8_t *p, c;
	uint32_t v;

	p = *pp;
	v = 0;
	for (;;) {
		c = *p++;
		v |= c & 0x7f;
		if ((c & 0x80) == 0)
			break;
		v <<= 7;
	}
	*pp = p;
	return v;
}

/*
 * xmidinoteon is XMIDI_note_on: the note on, and into the queue with
 * its length less one, the first free entry or else the first (whose
 * note is then never turned off). It returns the event's length.
 */
static uint32_t
xmidinoteon(Seq *q)
{
	uint8_t *p, *e;
	uint32_t ch, note, vel, len, ph;
	int32_t dur;
	int i;

	e = q->evntptr;
	ch = e[0] & 0xf;
	note = e[1];
	vel = e[2];
	p = e + 3;
	dur = vln(&p);
	len = p - e;
	if (lockstatus[ch] & 0x80)
		return len;
	for (i = 0; i < Maxnotes; i++)
		if (q->notechan[i] == 0xff)
			break;
	if (i < Maxnotes)
		q->notecount++;
	else
		i = 0;
	q->notechan[i] = ch;
	q->notenum[i] = note;
	q->notetime[i] = dur - 1;
	ph = q->chanmap[ch];
	activenotes[ph]++;
	synth->send(0x90|ph, note, vel);
	return len;
}

/*
 * xmidimeta is XMIDI_meta: the end of the sequence, a time signature or
 * a tempo. It returns the event's length.
 */
static uint32_t
xmidimeta(Seq *q)
{
	uint8_t *e, *p;
	uint32_t len, n, f;
	int32_t d;

	e = q->evntptr;
	p = e + 2;
	len = vln(&p);
	len += p - e;
	switch (e[1]) {
	case 0x2f:
		resetsequence(q);
		q->status = Seqdone;
		if (q->postrelease != 0)
			releaseseq(curhandle);
		break;
	case 0x58:
		q->timenumerator = p[0];
		d = (int32_t)p[1] - 2;
		if (d < 0) {
			f = Quanttime16;
			for (n = -d; n != 0; n--)
				f >>= 1;
		} else {
			n = (uint32_t)1 << (d & 31);
			f = (uint32_t)((uint64_t)Quanttime16 * n);
		}
		q->timefraction = f;
		q->beatfraction = 0 - f;
		q->beatcount = 0;
		q->measurecount++;
		break;
	case 0x51:
		q->timeperbeat = (p[0]<<16 | p[1]<<8 | p[2]) << 4;
		break;
	}
	return len;
}

/*
 * xmidisysex is XMIDI_sysex: a system exclusive event's length, its
 * variable-length number taken in 16 bits, the event itself skipped.
 */
static uint32_t
xmidisysex(Seq *q)
{
	uint8_t *e, *p, c;
	uint32_t v;

	e = q->evntptr;
	p = e + 1;
	v = 0;
	for (;;) {
		c = *p++;
		v |= c & 0x7f;
		if ((c & 0x80) == 0)
			break;
		v = v << 7 & 0xffff;
	}
	return (p - e) + v;
}

/* uldivide is ul_divide, dead: num over den, EDX as the caller left it, here 0. */
uint32_t
uldivide(uint32_t num, uint32_t den)
{
	return num / den;
}

/*
 * advancecount is advance_count: the sequence's beat and bar counts, an
 * interval ahead (QUANT_ADVANCE), its state left as it was.
 */
static void
advancecount(int32_t h, uint32_t *beat, uint32_t *bar)
{
	Seq *q;
	int32_t f;

	q = seqstate[h/4];
	*beat = q->beatcount;
	*bar = q->measurecount;
	f = q->beatfraction + q->timefraction;
	if (f >= q->timeperbeat) {
		(*beat)++;
		if ((*beat & 0xffff) >= q->timenumerator) {
			*beat = 0;
			(*bar)++;
		}
	}
}

/*
 * servedriver is serve_driver, AIL's timer calling it 120 times a
 * second: each playing sequence gets as many intervals as its tempo
 * makes, each turning off the queued notes whose time is out, then
 * playing the events up to the next wait, then counting the beat. Its
 * tempo and volume move toward their targets.
 */
void
servedriver(void)
{
	uint32_t n, ch, st, d1, d2, len;
	int32_t e, c;
	uint8_t *p;
	Seq *q;
	int i;

	if (serviceactive != 0)
		return;
	serviceactive++;
	curhandle = -4;
	for (n = seqcount; n != 0; n--) {
		do
			curhandle += 4;
		while (seqstate[curhandle/4] == NULL);
		q = seqstate[curhandle/4];
		if (q->status != Seqplaying)
			continue;
		q->tempoerror += q->tempopercent;
		e = q->tempoerror - 100;
		while (e >= 0) {
			q->tempoerror = e;
			if (q->notecount != 0)
				for (i = 0; i < Maxnotes; i++) {
					if (q->notechan[i] == 0xff)
						continue;
					if (--q->notetime[i] >= 0) {
						/* repe scasb, ending on the last entry, has it counted down to 0 */
						if (i == Maxnotes-1)
							q->notetime[i] = 0;
						continue;
					}
					queuenoteoff(q, i);
					if (--q->notecount == 0)
						break;
					/*
					 * FIXED: the last entry's: repe scasb, ECX spent, keeps
					 * the count's flags, so the driver takes the entry
					 * again, free, till the count is 0: each time a message
					 * of status 0xff, and active_notes[0xff], a byte of
					 * rewind_seq's code, decremented. The code stays here.
					 */
					if (i == Maxnotes-1)
						while (q->notecount != 0) {
							q->notetime[i]--;
							synth->send(0xff, q->notenum[i], 0);
							q->notecount--;
						}
				}
			if (--q->intervalcnt <= 0) {
				for (;;) {
					p = q->evntptr;
					st = p[0];
					if (st < 128) {
						q->evntptr++;
						q->intervalcnt = st;
						break;
					}
					ch = st & 0xf;
					st &= 0xf0;
					d1 = p[1];
					d2 = p[2];
					len = 0;
					if (st >= 0xf0) {
						if (ch == 0xf)
							len = xmidimeta(q);
						else
							len = xmidisysex(q);
					} else if (st >= 0xe0) {
						q->chanpitchl[ch] = d1;
						q->chanpitchh[ch] = d2;
						globalpitchl[ch] = d1;
						globalpitchh[ch] = d2;
						len = 3;
						if ((lockstatus[ch] & 0x80) == 0)
							synth->send(st | q->chanmap[ch], d1, d2);
					} else if (st >= 0xd0) {
						len = 2;
						if ((lockstatus[ch] & 0x80) == 0)
							synth->send(st | q->chanmap[ch], d1, d2);
					} else if (st >= 0xc0) {
						q->chanprogram[ch] = d1;
						globalprogram[ch] = d1;
						len = 2;
						if ((lockstatus[ch] & 0x80) == 0)
							synth->send(st | q->chanmap[ch], d1, d2);
					} else if (st >= 0xb0) {
						xmidicontrol(q, ch, d1, d2);
						len = 3;
					} else if (st >= 0xa0) {
						len = 3;
						if ((lockstatus[ch] & 0x80) == 0)
							synth->send(st | q->chanmap[ch], d1, d2);
					} else
						len = xmidinoteon(q);
					q->evntptr += len;
					if (q->status != Seqplaying)
						goto nextseq;
				}
			}
			q->beatfraction += q->timefraction;
			if (q->beatfraction >= q->timeperbeat) {
				q->beatfraction -= q->timeperbeat;
				q->beatcount++;
				if (q->beatcount >= q->timenumerator) {
					q->beatcount = 0;
					q->measurecount++;
				}
			}
			e = q->tempoerror - 100;
		}
		if (q->tempopercent != q->tempotarget) {
			q->tempoaccum += Quanttime / 100;
			for (c = 0; q->tempoaccum - q->tempoperiod >= 0; c++)
				q->tempoaccum -= q->tempoperiod;
			if (c != 0) {
				if (q->tempopercent < q->tempotarget) {
					q->tempopercent += c;
					if (q->tempopercent > q->tempotarget)
						q->tempopercent = q->tempotarget;
				} else {
					q->tempopercent -= c;
					if (q->tempopercent < q->tempotarget)
						q->tempopercent = q->tempotarget;
				}
			}
		}
		if (q->volpercent != q->voltarget) {
			q->volaccum += Quanttime / 100;
			for (c = 0; q->volaccum - q->volperiod >= 0; c++)
				q->volaccum -= q->volperiod;
			if ((c & 0xffff) != 0) {
				if (q->volpercent < q->voltarget) {
					q->volpercent += c;
					if (q->volpercent > q->voltarget)
						q->volpercent = q->voltarget;
				} else {
					q->volpercent -= c;
					if (q->volpercent < q->voltarget)
						q->volpercent = q->voltarget;
				}
				xmidivolume(q);
			}
		}
	nextseq:;
	}
	serviceactive--;
}

/*
 * initdriver is init_driver: no sequence, the synthesizer given its
 * ports and reset, no callback, every channel from 2 to 10 at the
 * controllers' defaults, centred, and on its Roland program.
 */
void
initdriver(Synth *s, uint32_t io, uint32_t irq, uint32_t dma, uint32_t drq)
{
	uint32_t c, ch, h;

	serviceactive = 0;
	seqcount = 0;
	memset(globalcontrols, 0xff, sizeof globalcontrols);
	memset(globalprogram, 0xff, sizeof globalprogram);
	memset(globalpitchl, 0xff, sizeof globalpitchl);
	memset(globalpitchh, 0xff, sizeof globalpitchh);
	memset(ctrlhash, 0xff, sizeof ctrlhash);
	memset(seqstate, 0, sizeof seqstate);
	memset(lockstatus, 0, sizeof lockstatus);
	memset(activenotes, 0, sizeof activenotes);
	for (c = 0; c < Ncontrols; c++)
		ctrlhash[loggedctrls[c]] = c * Nchans;
	synth = s;
	synth->setioparms(io, irq, dma, drq);
	synth->reset();
	synth->init();
	cancelcallback();
	for (c = 0; c < Ncontrols; c++) {
		if (ctrldefault[c] == 0xff)
			continue;
		for (ch = Mintruechan - 1; ch <= Maxrecchan-1; ch++) {
			h = ctrlhash[loggedctrls[c]];
			globalcontrols[h+ch] = ctrldefault[c];
			synth->send(0xb0|ch, loggedctrls[c], ctrldefault[c]);
		}
	}
	for (ch = Mintruechan - 1; ch <= Maxrecchan-1; ch++) {
		globalpitchl[ch] = Defpitchl;
		globalpitchh[ch] = Defpitchh;
		synth->send(0xe0|ch, Defpitchl, Defpitchh);
		/* prg_default[di-...]: MASM indexed by EDI */
		c = prgdefault[ch-(Mintruechan-1)];
		if (c == 0xff)
			continue;
		globalprogram[ch] = c;
		synth->send(0xc0|ch, c, 0);
	}
	initok = 1;
}

/*
 * shutdowndriver is shutdown_driver: every sequence stopped and let go,
 * the synthesizer reset, the sign-off shown, the synthesizer shut down.
 */
void
shutdowndriver(char *signoff)
{
	uint32_t n;
	int32_t h;

	if (initok == 0)
		return;
	h = 0;
	for (n = seqcount; n != 0; n--) {
		while (seqstate[h/4] == NULL)
			h += 4;
		stopseq(h);
		releaseseq(h);
		h += 4;
	}
	synth->reset();
	synth->display(signoff);
	synth->shutdown();
	initok = 0;
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

/*
 * xmidistate: the sequencer's variables into p as the drivers lay them
 * out (A32SP2FM.DLL from 0x12c7b), 0x22b bytes,
 * its pointers as 32 bits, for the oracles, which compare them with the
 * DLL's.
 */
void
xmidistate(uint8_t *p)
{
	int i;

	memmove(p, loggedctrls, Ncontrols);
	p += Ncontrols;
	memmove(p, ctrldefault, Ncontrols);
	p += Ncontrols;
	memmove(p, prgdefault, sizeof prgdefault);
	p += sizeof prgdefault;
	memmove(p, ctrlhash, sizeof ctrlhash);
	p += sizeof ctrlhash;
	for (i = 0; i < Nseqs; i++)
		p = putle(p, (uint32_t)(uintptr_t)seqstate[i], 4);
	p = putle(p, seqcount, 4);
	p = putle(p, curhandle, 4);
	p = putle(p, serviceactive, 2);
	p = putle(p, (uint32_t)(uintptr_t)triggerfn, 4);
	memmove(p, globalcontrols, sizeof globalcontrols);
	p += sizeof globalcontrols;
	memmove(p, globalprogram, Nchans);
	p += Nchans;
	memmove(p, globalpitchl, Nchans);
	p += Nchans;
	memmove(p, globalpitchh, Nchans);
	p += Nchans;
	memmove(p, activenotes, Nchans);
	p += Nchans;
	memmove(p, lockstatus, Nchans);
	p += Nchans;
	putle(p, initok, 2);
}

/*
 * xmidiseqstate: sequence h's state into p as the drivers' state_table
 * lays it out, 536 bytes, its pointers as 32 bits, for the oracles.
 */
void
xmidiseqstate(int32_t h, uint8_t *p)
{
	static int n[] = {Nchans, Nchans, Nchans, Nchans, Nchans, Ctrllog, Maxnotes, Maxnotes};
	uint8_t *b[nelem(n)];
	Seq *q;
	int i;

	q = seqstate[h/4];
	b[0] = q->chanmap;
	b[1] = q->chanprogram;
	b[2] = q->chanpitchl;
	b[3] = q->chanpitchh;
	b[4] = q->chanindirect;
	b[5] = q->chancontrols;
	b[6] = q->notechan;
	b[7] = q->notenum;
	p = putle(p, (uint32_t)(uintptr_t)q->timb, 4);
	p = putle(p, (uint32_t)(uintptr_t)q->rbrn, 4);
	p = putle(p, (uint32_t)(uintptr_t)q->evnt, 4);
	p = putle(p, (uint32_t)(uintptr_t)q->evntptr, 4);
	p = putle(p, q->curcallback, 4);
	p = putle(p, (uint32_t)(uintptr_t)q->ctrlptr, 4);
	p = putle(p, q->handle, 4);
	p = putle(p, q->started, 2);
	p = putle(p, q->status, 2);
	p = putle(p, q->postrelease, 2);
	p = putle(p, q->intervalcnt, 2);
	p = putle(p, q->notecount, 2);
	p = putle(p, q->volerror, 4);
	p = putle(p, q->volpercent, 4);
	p = putle(p, q->voltarget, 4);
	p = putle(p, q->volaccum, 4);
	p = putle(p, q->volperiod, 4);
	p = putle(p, q->tempoerror, 4);
	p = putle(p, q->tempopercent, 4);
	p = putle(p, q->tempotarget, 4);
	p = putle(p, q->tempoaccum, 4);
	p = putle(p, q->tempoperiod, 4);
	p = putle(p, q->beatcount, 2);
	p = putle(p, q->measurecount, 2);
	p = putle(p, q->timenumerator, 2);
	p = putle(p, q->timefraction, 4);
	p = putle(p, q->beatfraction, 4);
	p = putle(p, q->timeperbeat, 4);
	for (i = 0; i < Fornest; i++)
		p = putle(p, (uint32_t)(uintptr_t)q->forptrs[i], 4);
	for (i = 0; i < Fornest; i++)
		p = putle(p, q->forloopcnt[i], 2);
	for (i = 0; i < (int)nelem(b); i++) {
		memmove(p, b[i], n[i]);
		p += n[i];
	}
	for (i = 0; i < Maxnotes; i++)
		p = putle(p, q->notetime[i], 4);
}

/* statesize is get_state_size: what a sequence's state takes. */
uint32_t
statesize(void)
{
	return sizeof(Seq);
}

/* installcallback is install_callback: fn called on controller 119 with the handle and value. */
void
installcallback(void (*fn)(int32_t, int32_t))
{
	triggerfn = fn;
}

/* cancelcallback is cancel_callback. */
void
cancelcallback(void)
{
	triggerfn = NULL;
}

/*
 * registerseq is register_seq: the num-th sequence of the XMIDI file
 * given a handle, its state in st (statesize bytes), its chunks found
 * (TIMB and RBRN optional, EVNT last), stopped, else -1.
 */
int32_t
registerseq(uint8_t *xmid, uint32_t num, void *st, uint8_t *ctrl)
{
	uint32_t len;
	uint8_t *p;
	int32_t h;
	Seq *q;

	for (h = 0; h < 4*Nseqs; h += 4)
		if (seqstate[h/4] == NULL)
			break;
	if (h == 4*Nseqs)
		return -1;
	p = findseq(xmid, num);
	if (p == NULL)
		return -1;
	q = st;
	seqstate[h/4] = q;
	q->timb = NULL;
	q->rbrn = NULL;
	q->evnt = NULL;
	len = 12;
	for (;;) {
		p += len;
		len = iff32(p+4) + 8;
		if (memcmp(p, "TIMB", 4) == 0)
			q->timb = p;
		else if (memcmp(p, "RBRN", 4) == 0)
			q->rbrn = p;
		else if (memcmp(p, "EVNT", 4) == 0)
			break;
	}
	q->handle = h;
	q->evnt = p;
	q->ctrlptr = ctrl;
	q->postrelease = 0;
	q->started = 0;
	q->status = Seqstopped;
	seqcount++;
	rewindseq(h);
	return h;
}

/* releaseseq is release_seq: the handle freed, or when the sequence plays, once it ends. */
static void
releaseseq(int32_t h)
{
	Seq *q;

	if (h == -1 || seqstate[h/4] == NULL)
		return;
	q = seqstate[h/4];
	if (q->status == Seqplaying) {
		q->postrelease = 1;
		return;
	}
	seqstate[h/4] = NULL;
	seqcount--;
}

void
releaseseqhandle(int32_t h)
{
	releaseseq(h);
}

/* stopseq is stop_seq: the sequence's queued notes off, what it holds let go. */
void
stopseq(int32_t h)
{
	Seq *q;

	if (h == -1 || seqstate[h/4] == NULL)
		return;
	q = seqstate[h/4];
	if (q->status != Seqplaying)
		return;
	flushnotequeue(q);
	resetsequence(q);
	q->status = Seqstopped;
}

/* startseq is start_seq: the sequence from its start. */
void
startseq(int32_t h)
{
	Seq *q;

	if (h == -1)
		return;
	q = seqstate[h/4];
	if (q->status == Seqplaying)
		stopseq(h);
	rewindseq(h);
	q->evntptr = q->evnt + 8;
	q->status = Seqplaying;
	q->started = 1;
}

/* resumeseq is resume_seq: a sequence stopped once started, its channels set again, playing on. */
void
resumeseq(int32_t h)
{
	Seq *q;

	if (h == -1 || seqstate[h/4] == NULL)
		return;
	q = seqstate[h/4];
	if (q->status != Seqstopped || q->started == 0)
		return;
	restoresequence(q);
	q->status = Seqplaying;
}

/* seqstatus is get_seq_status. */
int32_t
seqstatus(int32_t h)
{
	if (h == -1)
		return -1;
	return seqstate[h/4]->status;
}

/* beatcount is get_beat_count. */
int32_t
beatcount(int32_t h)
{
	uint32_t beat, bar;

	if (h == -1)
		return -1;
	advancecount(h, &beat, &bar);
	return beat;
}

/* barcount is get_bar_count. */
int32_t
barcount(int32_t h)
{
	uint32_t beat, bar;

	if (h == -1)
		return -1;
	advancecount(h, &beat, &bar);
	return bar;
}

/* mapseqchannel is map_seq_channel: the sequence's channel (from 1) played on another. */
void
mapseqchannel(int32_t h, uint32_t seqchan, uint32_t physchan)
{
	if (h == -1)
		return;
	seqstate[h/4]->chanmap[seqchan - 1] = physchan - 1;
}

/* trueseqchannel is true_seq_channel: the channel (from 1) the sequence's plays on. */
int32_t
trueseqchannel(int32_t h, uint32_t seqchan)
{
	if (h == -1)
		return -1;
	return seqstate[h/4]->chanmap[seqchan - 1] + 1;
}

/*
 * branchindex is branch_index: the sequence's events from the RBRN
 * chunk's marker on, its queued notes off, its FOR loops cancelled.
 */
void
branchindex(int32_t h, uint32_t marker)
{
	uint8_t *p;
	uint32_t n;
	Seq *q;
	int i;

	if (h == -1)
		return;
	q = seqstate[h/4];
	if (q->rbrn == NULL || memcmp(q->rbrn, "RBRN", 4) != 0)
		return;
	n = q->rbrn[8] | q->rbrn[9]<<8;
	p = q->rbrn + 10;
	while (p[0] != (marker & 0xff)) {
		p += 6;
		if (--n == 0)
			return;
	}
	q->evntptr = q->evnt + (get32(p+2) + 8);
	q->intervalcnt = 0;
	flushnotequeue(q);
	for (i = 0; i < Fornest; i++)
		q->forloopcnt[i] = -1;
}

/* reltempo is get_rel_tempo. */
int32_t
reltempo(int32_t h)
{
	if (h == -1)
		return -1;
	return seqstate[h/4]->tempopercent;
}

/* relvolume is get_rel_volume. */
int32_t
relvolume(int32_t h)
{
	if (h == -1)
		return -1;
	return seqstate[h/4]->volpercent;
}

/*
 * setreltempo is set_rel_tempo: the sequence's tempo, in percent, at
 * once, or over ms milliseconds, as set_rel_volume moves the volume.
 */
void
setreltempo(int32_t h, int32_t tempo, uint32_t ms)
{
	uint32_t d, p;
	Seq *q;

	if (h == -1)
		return;
	q = seqstate[h/4];
	q->tempotarget = tempo;
	if (ms == 0) {
		q->tempopercent = tempo;
		return;
	}
	d = q->tempotarget - q->tempopercent;
	if (d == 0)
		return;
	if ((int32_t)d < 0)
		d = -d;
	/* FIXED: the driver's div faults past 32 bits. The low 32 are kept here. */
	p = (uint32_t)((uint64_t)10*ms / d);
	if (p == 0)
		p = 1;
	q->tempoperiod = p;
	q->tempoaccum = 0;
}

/*
 * setrelvolume is set_rel_volume: the sequence's volume, in percent, at
 * once, or over ms milliseconds, a step every 10*ms/difference of 100 us.
 */
void
setrelvolume(int32_t h, int32_t vol, uint32_t ms)
{
	uint32_t d, p;
	Seq *q;

	if (h == -1)
		return;
	q = seqstate[h/4];
	q->voltarget = vol;
	if (ms == 0) {
		q->volpercent = vol;
		xmidivolume(q);
		return;
	}
	d = q->voltarget - q->volpercent;
	if (d == 0)
		return;
	if ((int32_t)d < 0)
		d = -d;
	/* FIXED: the driver's div faults past 32 bits. The low 32 are kept here. */
	p = (uint32_t)((uint64_t)10*ms / d);
	if (p == 0)
		p = 1;
	q->volperiod = p;
	q->volaccum = 0;
}

/*
 * controlval is get_control_val: the value the sequence last gave the
 * controller on the channel (from 1), its callback's for 119, and -1
 * for a controller outside the log.
 */
int32_t
controlval(int32_t h, uint32_t chan, uint32_t con)
{
	uint32_t i;
	Seq *q;

	if (h == -1)
		return -1;
	q = seqstate[h/4];
	if (con == Callbacktrig)
		return q->curcallback;
	i = ctrlhash[con & 0xff];
	if (i == 0xff)
		return -1;
	return q->chancontrols[(int32_t)(i + chan - 1)];
}

/* setcontrolval is set_control_val: a controller played as the sequence would. */
void
setcontrolval(int32_t h, uint32_t chan, uint32_t con, uint32_t val)
{
	if (h == -1)
		return;
	xmidicontrol(seqstate[h/4], chan - 1, con, val);
}

/* channotes is get_chan_notes: the sequence's queued notes on the channel (from 1). */
int32_t
channotes(int32_t h, uint32_t chan)
{
	int32_t n;
	Seq *q;
	int i;

	if (h == -1)
		return -1;
	q = seqstate[h/4];
	n = 0;
	for (i = 0; i < Maxnotes; i++)
		if (q->notechan[i] == ((chan - 1) & 0xff))
			n++;
	return n;
}

/* seqtimb: the sequence's TIMB chunk, for the synthesizer's get_request. */
uint8_t*
seqtimb(int32_t h)
{
	return seqstate[h/4]->timb;
}

/*
 * lockchannel is lock_channel: the highest channel from 2 to 9 with the
 * fewest notes, unlocked and unprotected (or, failing one, unlocked),
 * silenced and locked. It returns its number from 1, or 0.
 */
int32_t
lockchannel(void)
{
	uint32_t mask, min;
	int32_t c, e;

	e = -1;
	min = 0xff;
	mask = 0xc0;
	for (;;) {
		for (c = Maxtruechan - 1; c >= Mintruechan-1; c--) {
			if (lockstatus[c] & mask)
				continue;
			if (activenotes[c] >= min)
				continue;
			min = activenotes[c];
			e = c;
		}
		if (e != -1)
			break;
		if (mask == 0x80)
			return e + 1;
		mask = 0x80;
	}
	synth->send(0xb0|e, Sustain, 0);
	flushchannelnotes(e);
	activenotes[e] = 0;
	lockstatus[e] |= 0x80;
	return e + 1;
}

/*
 * releasechannel is release_channel: a locked channel (from 1) unlocked,
 * silenced, and given back its controllers, program and pitch as last
 * sent.
 */
void
releasechannel(uint32_t chan)
{
	uint32_t ch, c, v;

	ch = chan - 1;
	if ((lockstatus[ch] & 0x80) == 0)
		return;
	lockstatus[ch] &= 0x7f;
	activenotes[ch] = 0;
	synth->send(0xb0|ch, Sustain, 0);
	synth->send(0xb0|ch, Allnotesoff, 0);
	for (c = 0; c < Ncontrols; c++) {
		v = globalcontrols[c*Nchans + ch];
		if (v != 0xff)
			synth->send(0xb0|ch, loggedctrls[c], v);
	}
	if (globalprogram[ch] != 0xff)
		synth->send(0xc0|ch, globalprogram[ch], 0);
	if (globalpitchl[ch] != 0xff && globalpitchh[ch] != 0xff)
		synth->send(0xe0|ch, globalpitchl[ch], globalpitchh[ch]);
}
