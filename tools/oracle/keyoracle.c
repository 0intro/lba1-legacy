#define _GNU_SOURCE
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include "dat.h"
#include "fns.h"
#include "abi.h"
#include "pc.h"

/*
 * keyoracle: the keyboard as lib386/lib_sys/keyboard.asm's handler of
 * IRQ 1 keeps it, against sys/keyb.c, the port's, at the interface the
 * game reads: Key, Joy, Fire and FuncKey, and GetAscii's words while
 * AsciiMode is set.
 *
 *	keyoracle lba0.elf lba0.sym
 *
 * lba0.elf is a DOS build, as tools/le2elf.py makes it, and lba0.sym
 * names its procedures and data (tools/wmap2sym.py of its link map).
 * Its code runs on pc.c's PC. InitKeyboard puts NewIntPM09 in vector 9
 * through int 21h, as the game's start does. Each byte the keyboard
 * sends waits in the 8042's output buffer, port 60h, port 64h's bit 0
 * set while it does, and the handler is called as IRQ 1 calls it: it
 * must read the byte, see one end of interrupt sent to the PIC, its own
 * or the BIOS's, keep every register, and reset nothing: a write of the
 * BIOS's warm boot flag at 472h and a far return to FFFFh:0, LBA.EXE's
 * for Ctrl, Alt and Del together, end the interrupt as a failure.
 * While AsciiMode is set it chains to the BIOS's handler, which the
 * model plays: it reads port 60h again and puts each key's make in its
 * buffer of 15 words, the key's scan code and its character on a US
 * layout, which int 16h gives GetAscii. sys/keyb.c's xtcode is given
 * the same bytes, one at a time, and biosword the words of every make,
 * as sys/sdl.c gives them.
 *
 *	bytes	from 1024 states at random, each of the 256 bytes, alone
 *		and after an E0, then a left Shift's make, which an E0
 *		before it makes a fake that both drop
 *	random	250000 bytes at random, most of them the special keys'
 *		makes and breaks and E0
 *	bursts	two to four bytes waiting together, which the handler's
 *		loop reads in one interrupt (an 8042 holds one byte, and the
 *		keyboard sends the next a millisecond after it is read)
 *	ascii	the random bytes with AsciiMode set, one an interrupt, the
 *		BIOS given each, GetAscii and ClearAsciiBuffer between
 *	vectors	InitKeyboard puts the handler in vector 9, ClearKeyboard
 *		the one it found there
 *
 * After each byte or burst Key, Joy, Fire and FuncKey must be the same,
 * and after each GetAscii its word. What the port does otherwise on
 * purpose is let pass:
 *
 *	_Key, the handler's last byte: the game reads Key
 *
 * Each part runs in a process of its own, ended after 600 seconds, its
 * differences reported, the first 20 in full, and the end is
 * "keyoracle: ok", or "keyoracle: N failed" and status 1.
 */

enum
{
	Nstate = 1024,
	Rounds = 250000,
	Nbios = 16,	/* the BIOS's buffer at 40:1Eh, one slot kept empty */
	Nburst = 4,
};

/* the assembly's procedures and data, by lba0.sym */
static struct
{
	uintptr_t	initkeyboard;
	uintptr_t	clearkeyboard;
	uintptr_t	newintpm09;
	uintptr_t	getascii;
	uintptr_t	clearasciibuffer;
	uint16_t	*key;
	uint16_t	*joy;
	uint16_t	*fire;
	uint16_t	*funckey;
	uint16_t	*asciimode;
} as;

/* sys/keyb.c's, which fns.h leaves to the game's headers */
extern volatile uint16_t	Key;
extern volatile uint16_t	Joy;
extern volatile uint16_t	Fire;
extern volatile uint16_t	FuncKey;
uint16_t	GetAscii(void);
void	ClearAsciiBuffer(void);

/* the 8042 */
static uint8_t kbwait[Nburst];	/* bytes the keyboard sent */
static int nkbwait, ikbwait;
static uint8_t kbdata;	/* its output buffer, port 60h */
static int kbfull;	/* port 64h's bit 0 */

/* a keyboard's BIOS: its shift and E0 states, and its buffer */
typedef struct Bios Bios;

struct Bios
{
	int	shift;
	int	ext;
	uint16_t	q[Nbios];
	int	head;
	int	tail;
};

static Bios bios;	/* the assembly's, chained to */
static Bios pbios;	/* the words sys/sdl.c would give the port */
static int nreset;
static int warmboot;
static sigjmp_buf resetjmp;

/* sys/timer.c's, for GetAscii */
void
pump(void)
{
}

static uint32_t
rnd(void)
{
	static uint32_t x = 0x2545f491;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

/* The 8042's ports: 60h gives the byte and the next waiting, 64h whether one is there. */
static uint32_t
kbin(uint32_t port, int w)
{
	uint32_t v;

	USED(w);
	if (port == 0x64)
		return 0x1c | kbfull;
	v = kbdata;
	if (kbfull) {
		kbfull = 0;
		if (ikbwait < nkbwait) {
			kbdata = kbwait[ikbwait++];
			kbfull = 1;
		}
	}
	return v;
}

static void
kbout(uint32_t port, uint32_t v, int w)
{
	USED(w);
	fail("the 8042's port %xh written, %02x", port, v);
}

/* US layout's characters, unshifted then shifted, by scan code from 1 to 39h */
static char *plain = "\0331234567890-=\b\tqwertyuiop[]\r\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
static char *shifted = "\033!@#$%^&*()_+\b\tQWERTYUIOP{}\r\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";

/* The word the BIOS b makes of byte c, or 0. */
static uint16_t
bioskey(Bios *b, int c)
{
	int s, ext;

	if (c == 0xe0) {
		b->ext = 1;
		return 0;
	}
	ext = b->ext;
	b->ext = 0;
	s = c & 0x7f;
	if (s == 0x2a || s == 0x36) {
		if (!ext)
			b->shift = !(c & 0x80);
		return 0;
	}
	if ((c & 0x80) || s == 0 || s == 0x1d || s == 0x38 || s == 0x3a || s == 0x45 || s == 0x46)
		return 0;
	if (ext || s > 0x39)
		return s << 8 | (ext && s == 0x1c ? '\r' : ext && s == 0x35 ? '/' : 0);
	return s << 8 | (uint8_t)(b->shift ? shifted : plain)[s-1];
}

static int
biosn(Bios *b)
{
	return (b->tail - b->head + Nbios) % Nbios;
}

static void
biosput(Bios *b, uint16_t w)
{
	int n;

	n = (b->tail + 1) % Nbios;
	if (n == b->head)
		return;
	b->q[b->tail] = w;
	b->tail = n;
}

/* The BIOS's handler of IRQ 1, chained to: the byte read again, its end of interrupt. */
static void
biosirq(void)
{
	uint16_t w;

	w = bioskey(&bios, kbin(0x60, 1));
	if (w != 0)
		biosput(&bios, w);
	neoi++;
}

/* The BIOS's int 16h: 00h the next word, 01h whether there is one. */
static void
int16(Cpu *c)
{
	switch (c->eax >> 8 & 0xff) {
	case 0x00:
		if (bios.head == bios.tail) {
			fail("int 16h 00h on an empty buffer, which waits for a key");
			runend();
		}
		setlo(&c->eax, bios.q[bios.head]);
		bios.head = (bios.head + 1) % Nbios;
		return;
	case 0x01:
		if (bios.head == bios.tail) {
			c->flags |= 0x40;
			return;
		}
		c->flags &= ~0x40u;
		setlo(&c->eax, bios.q[bios.head]);
		return;
	}
	fail("int 16h, function %04x, which the model lacks", c->eax & 0xffff);
	runend();
}

/*
 * Ctrl, Alt and Del: the handler writes the BIOS's warm boot flag at
 * 472h, below what a process maps, then returns far to FFFFh:0, where
 * the BIOS resets the PC in real mode. Both are taken here, the second
 * the end of the interrupt, both in the build's code: a fault elsewhere
 * is left to pc.c to report.
 */
static int
onreset(void *ctx)
{
	ucontext_t *uc;
	greg_t *g;
	uint8_t *ip;
	uint32_t *sp;

	uc = ctx;
	g = uc->uc_mcontext.gregs;
	if (!incode(g[REG_EIP]) || !incode(g[REG_EIP] + 4))
		return 0;
	ip = (uint8_t*)(uintptr_t)g[REG_EIP];
	if (memcmp(ip, "\x66\xc7\x00", 3) == 0 && g[REG_EAX] == 0x472) {
		warmboot = ip[3] | ip[4]<<8;
		g[REG_EIP] += 5;
		return 1;
	}
	sp = (uint32_t*)(uintptr_t)g[REG_ESP];
	if (ip[0] == 0xcb && (sp[1] & 0xffff) == 0xffff)
		siglongjmp(resetjmp, 1);
	return 0;
}

/*
 * IRQ 1 for the n bytes at b, all waiting in the 8042 together: 1 if
 * the handler reset the PC.
 */
static int
irq(uint8_t *b, int n)
{
	uint32_t sel, off;
	int e;

	memmove(kbwait, b, n);
	nkbwait = n;
	ikbwait = 1;
	kbdata = b[0];
	kbfull = 1;
	e = neoi;
	warmboot = 0;
	if (sigsetjmp(resetjmp, 1) != 0) {
		nreset++;
		if (nreset <= Nshown)
			fail("the handler resets the PC: %04xh at 472h, a far return to FFFFh:0", warmboot);
		else
			nfail++;
		return 1;
	}
	pcvector(9, &sel, &off);
	intcall(off);
	intok("NewIntPM09");
	if (neoi != e + 1)
		fail("%d ends of interrupt", neoi - e);
	if (kbfull || ikbwait < nkbwait)
		fail("the handler left %d bytes in the 8042", nkbwait - ikbwait + kbfull);
	return 0;
}

/* The same bytes to the port, and the words of their makes to its BIOS's queue. */
static void
port(uint8_t *b, int n)
{
	uint16_t w;
	int i;

	for (i = 0; i < n; i++) {
		xtcode(b[i]);
		w = bioskey(&pbios, b[i]);
		if (w != 0)
			biosword(w);
	}
}

static void
putstate(uint16_t key, uint16_t joy, uint16_t fire, uint16_t funckey)
{
	*as.key = Key = key;
	*as.joy = Joy = joy;
	*as.fire = Fire = fire;
	*as.funckey = FuncKey = funckey;
}

/* Key, Joy, Fire and FuncKey the same on both sides: 1 if they are. */
static int
same(void)
{
	if (*as.key == Key && *as.joy == Joy && *as.fire == Fire && *as.funckey == FuncKey)
		return 1;
	fail("the assembly's Key %04x Joy %04x Fire %04x FuncKey %04x, ours %04x %04x %04x %04x",
		*as.key, *as.joy, *as.fire, *as.funckey, Key, Joy, Fire, FuncKey);
	return 0;
}

/* Both sides given the n bytes at b: 1 if the handler reset the PC. */
static int
send(uint8_t *b, int n)
{
	int r;

	r = irq(b, n);
	port(b, n);
	return r;
}

/* Both sides as they were at the start, the E0 states cleared by a byte both ignore. */
static void
fresh(void)
{
	static uint8_t zero;

	putstate(0, 0, 0, 0);
	bios.ext = pbios.ext = 0;
	send(&zero, 1);
}

/* Both BIOSes empty and alike again, after a key one of them lost. */
static void
resync(void)
{
	bios.head = bios.tail = 0;
	pbios.head = pbios.tail = 0;
	bios.shift = pbios.shift;
	bios.ext = pbios.ext;
	ClearAsciiBuffer();
}

static void
initboth(void)
{
	acall(as.initkeyboard, 0);
	*as.asciimode = AsciiMode = 0;
	bios.head = bios.tail = 0;
	pbios.head = pbios.tail = 0;
	bios.shift = pbios.shift = 0;
	ClearAsciiBuffer();
	fresh();
}

/* A byte at random, most of them the special keys' and E0. */
static uint8_t
anybyte(void)
{
	static uint8_t keys[] = {
		72, 80, 75, 77, 71, 73, 81, 79, 57, 28, 29, 56, 83, 42, 54,
		59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 87, 88, 0x2c, 0x10, 0x1e,
	};
	uint32_t r;

	r = rnd() % 100;
	if (r < 15)
		return 0xe0;
	if (r < 80)
		return keys[rnd() % sizeof keys] | (rnd() & 0x80);
	return rnd();
}

static void
bytes(void *v)
{
	uint8_t b[3];
	int i, c, e, n;

	USED(v);
	initboth();
	n = 0;
	for (i = 0; i < Nstate; i++)
		for (e = 0; e < 2; e++)
			for (c = 0; c < 256; c++) {
				fresh();
				putstate((rnd() & 0x7f) | (i & 1 ? rnd() & 0xff00 : 0), rnd() & 0x0f,
					i & 2 ? rnd() & 0x3f : rnd() & 0x23, rnd() & 0x0fff);
				snprintf(what, sizeof what, "%s%02xh from Key %04x Joy %04x Fire %04x FuncKey %04x",
					e ? "E0 then " : "", c, Key, Joy, Fire, FuncKey);
				b[0] = 0xe0;
				b[1] = c;
				b[2] = 0x2a;
				if (e && send(b, 1)) {
					fresh();
					continue;
				}
				if (send(b+1, 1)) {
					fresh();
					continue;
				}
				n++;
				if (!same())
					continue;
				snprintf(what, sizeof what, "%s%02xh then 2Ah", e ? "E0 then " : "", c);
				if (send(b+2, 1))
					continue;
				same();
			}
	printf("bytes: %d bytes, %d resets, %s\n", n, nreset, nfail == 0 ? "the same" : "differ");
}

static void
atrandom(void *v)
{
	uint8_t b;
	int i;

	USED(v);
	initboth();
	for (i = 0; i < Rounds; i++) {
		b = anybyte();
		snprintf(what, sizeof what, "byte %d, %02xh", i, b);
		if (send(&b, 1)) {
			fresh();
			continue;
		}
		same();
	}
	printf("random: %d bytes, %d resets, %s\n", Rounds, nreset, nfail == 0 ? "the same" : "differ");
}

static void
bursts(void *v)
{
	uint8_t b[Nburst];
	int i, j, n;

	USED(v);
	initboth();
	for (i = 0; i < Rounds/4; i++) {
		n = 2 + rnd() % (Nburst - 1);
		for (j = 0; j < n; j++)
			b[j] = anybyte();
		snprintf(what, sizeof what, "burst %d of %d bytes from %02xh", i, n, b[0]);
		if (send(b, n)) {
			fresh();
			continue;
		}
		same();
	}
	printf("bursts: %d bursts, %d resets, %s\n", Rounds/4, nreset, nfail == 0 ? "the same" : "differ");
}

static void
ascii(void *v)
{
	uint8_t b;
	uint16_t a, p;
	int i, c, nword;

	USED(v);
	initboth();
	*as.asciimode = AsciiMode = 1;
	nword = 0;
	for (i = 0; i < Rounds; i++) {
		b = anybyte();
		snprintf(what, sizeof what, "byte %d, %02xh", i, b);
		c = chained(9);
		if (send(&b, 1)) {
			resync();
			fresh();
			continue;
		}
		same();
		if (chained(9) != c + 1) {
			fail("the BIOS given the byte %d times", chained(9) - c);
			resync();
			continue;
		}
		while (rnd() % 2 == 0 || biosn(&bios) >= Nbios - 2) {
			snprintf(what, sizeof what, "GetAscii after byte %d", i);
			a = acall(as.getascii, 0);
			p = GetAscii();
			if (a != p) {
				fail("the assembly's %04x, ours %04x", a, p);
				break;
			}
			if (a == 0)
				break;
			nword++;
		}
		if (rnd() % 1000 == 0) {
			snprintf(what, sizeof what, "ClearAsciiBuffer after byte %d", i);
			acall(as.clearasciibuffer, 0);
			ClearAsciiBuffer();
			if (bios.head != bios.tail)
				fail("the BIOS keeps %d words", biosn(&bios));
		}
	}
	printf("ascii: %d bytes, %d words, %d resets, %s\n", Rounds, nword, nreset,
		nfail == 0 ? "the same" : "differ");
}

static void
vectors(void *v)
{
	uint32_t sel0, off0, sel, off;

	USED(v);
	pcvector(9, &sel0, &off0);
	snprintf(what, sizeof what, "InitKeyboard");
	acall(as.initkeyboard, 0);
	pcvector(9, &sel, &off);
	if (sel != pccs || off != as.newintpm09)
		fail("vector 9 %04x:%08x, NewIntPM09 is %04x:%08x", sel, off, pccs, (uint32_t)as.newintpm09);
	snprintf(what, sizeof what, "ClearKeyboard");
	acall(as.clearkeyboard, 0);
	pcvector(9, &sel, &off);
	if (sel != sel0 || off != off0)
		fail("vector 9 %04x:%08x, it was %04x:%08x", sel, off, sel0, off0);
	printf("vectors: %s\n", nfail == 0 ? "as they should" : "differ");
}

int
main(int argc, char **argv)
{
	static struct
	{
		char	*name;
		void	(*fn)(void*);
	} parts[] = {
		{"bytes", bytes},
		{"random", atrandom},
		{"bursts", bursts},
		{"ascii", ascii},
		{"vectors", vectors},
	};
	int i, nbad;

	oracle = "keyoracle";
	if (argc != 3) {
		fprintf(stderr, "usage: keyoracle lba0.elf lba0.sym\n");
		return 2;
	}
	pcload(argv[1], argv[2]);
	as.initkeyboard = lookup("InitKeyboard");
	as.clearkeyboard = lookup("ClearKeyboard");
	as.newintpm09 = lookup("NewIntPM09");
	as.getascii = lookup("GetAscii");
	as.clearasciibuffer = lookup("ClearAsciiBuffer");
	as.key = (uint16_t*)lookup("Key");
	as.joy = (uint16_t*)lookup("Joy");
	as.fire = (uint16_t*)lookup("Fire");
	as.funckey = (uint16_t*)lookup("FuncKey");
	as.asciimode = (uint16_t*)lookup("AsciiMode");
	pcport(0x60, 0x60, kbin, kbout);
	pcport(0x64, 0x64, kbin, kbout);
	pcbios(9, biosirq);
	pcint(0x16, int16);
	pcfault = onreset;
	nbad = 0;
	for (i = 0; i < (int)nelem(parts); i++)
		nbad += inchild(parts[i].name, 600, parts[i].fn, NULL);
	if (nbad > 0) {
		fprintf(stderr, "keyoracle: %d failed\n", nbad);
		return 1;
	}
	printf("keyoracle: ok\n");
	return 0;
}
