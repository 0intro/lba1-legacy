#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "abi.h"
#include "asmoracle.h"

/*
 * p_anim's procedures: random bodies and animations, and with gamedir
 * the game's (BODY.HQR and ANIM.HQR), laid out alike in an arena for
 * each side, put through the procedures in random order as time
 * passes. Ours and the assembly's each keep their arena and globals,
 * the bodies a call takes and the globals compared after each call,
 * the arenas whole every 1000, the frame a body comes from compared as
 * its offset in its arena.
 */

enum
{
	Info = 16,	/* a body's frame and time */
	Group = 38,
	Room = 64,	/* past a body or an animation */
	Arena = 8 << 20,
	Buffer = 64 << 10,	/* StockInterAnim's frames, at the arena's end */
	Nbody = 512,
	Nanim = 2048,
	INFO_ANIM = 2,
};

void	Expand(void*, void*, uint32_t);
void	SetAnimObjet(int32_t, void*, void*);
int32_t	SetInterAnimObjet(int32_t, void*, void*);
int32_t	SetInterAnimObjet2(int32_t, void*, void*);
int32_t	SetInterDepObjet(int32_t, void*, void*);
int16_t	StockInterAnim(uint8_t*, uint8_t*);
void	CopyInterAnim(void*, void*);
int32_t	GetNbFramesAnim(void*);
int32_t	GetBouclageAnim(void*);
void	SaveTimer(void);
void	RestoreTimer(void);
void	SetAngleGroupe(int32_t, int32_t, int32_t, int32_t, void*);
void	GetAngleGroupe(int32_t, void*);

extern uint32_t	TimerRef, MemoTimerRef;
extern int32_t	CmptMemoTimerRef;
extern int16_t	AnimStepX, AnimStepY, AnimStepZ;
extern int16_t	AnimMasterRot, AnimStepAlpha, AnimStepBeta, AnimStepGamma;

typedef struct Var Var;

/* a global of p_anim's: ours and its size */
struct Var
{
	char	*name;
	void	*c;
	uint32_t	size;
};

static Var vars[] = {
	{"AnimStepX", &AnimStepX, 2},
	{"AnimStepY", &AnimStepY, 2},
	{"AnimStepZ", &AnimStepZ, 2},
	{"AnimMasterRot", &AnimMasterRot, 2},
	{"AnimStepAlpha", &AnimStepAlpha, 2},
	{"AnimStepBeta", &AnimStepBeta, 2},
	{"AnimStepGamma", &AnimStepGamma, 2},
	{"TimerRef", &TimerRef, 4},
	{"MemoTimerRef", &MemoTimerRef, 4},
	{"CmptMemoTimerRef", &CmptMemoTimerRef, 4},
};

static char *names[] = {
	"SetAnimObjet", "SetInterAnimObjet", "SetInterAnimObjet2", "SetInterDepObjet",
	"StockInterAnim", "CopyInterAnim", "SaveTimer", "RestoreTimer",
	"GetNbFramesAnim", "SetAngleGroupe", "GetAngleGroupe", "GetBouclageAnim",
};

/* how often each, out of 32: mostly the moves towards a frame, as the game makes them */
static int weights[] = {2, 10, 5, 5, 2, 2, 1, 1, 1, 2, 1, 0};

/* each Var's assembly's */
static uint8_t *av[nelem(vars)];
static uint8_t *aarena, *carena;	/* the assembly's, ours */
static uint32_t top;
static uint32_t body[Nbody], bodylen[Nbody], anim[Nanim];
static int nbody, nanim, gamebodies, gameanims;

/* MemOffset and MemPointer over our arena: a frame a body comes from is kept as its offset */
uint32_t
MemOffset(void *p)
{
	if (p == NULL)
		return 0;
	return (uint8_t*)p - carena;
}

void*
MemPointer(uint32_t off)
{
	if (off == 0)
		return NULL;
	return carena + off;
}

static void
put32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static void
put16(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
}

static void
fill(uint8_t *p, uint32_t n)
{
	uint32_t i;

	for (i = 0; i < n; i++)
		p[i] = rnd();
}

/* The offset in its arena of the frame a body comes from, 0 for none. Side 0 is ours, 1 the assembly's. */
static uint32_t
from(int side, uint32_t b)
{
	uint32_t p;

	if (side == 0)
		return get32(carena + b + Info);
	p = get32(aarena + b + Info);
	if (p == 0)
		return 0;
	return p - (uint32_t)(uintptr_t)aarena;
}

/* Room for n bytes and some slack at the arena's top, or NULL. */
static uint8_t*
alloc(uint32_t n)
{
	uint8_t *p;

	if (top + n + Room > Arena - Buffer)
		return NULL;
	p = aarena + top;
	fill(p + n, Room);
	top += (n + Room + 1) & ~1u;
	return p;
}

/* The entries of an HQR file of the game into the arena, their offsets in off, and their count. */
static int
loadhqr(char *name, uint32_t *off, uint32_t *len, int max)
{
	char path[1024];
	uint8_t *b, *p;
	uint32_t n, i, o, size, packed;
	long fsize;
	int k;

	snprintf(path, sizeof path, "%s/%s", gamedir, name);
	b = readfile(path, &fsize);
	n = get32(b) / 4;
	k = 0;
	for (i = 0; i < n && k < max; i++) {
		o = get32(b + 4*i);
		if (o == 0 || o + 10 > (uint32_t)fsize)
			continue;
		size = get32(b + o);
		packed = get32(b + o + 4);
		if (size == 0 || o + 10 + packed > (uint32_t)fsize)
			continue;
		p = alloc(size);
		if (p == NULL)
			break;
		if (get16(b + o + 8) == 0)
			memcpy(p, b + o + 10, size);
		else
			Expand(b + o + 10, p, size);
		off[k] = p - aarena;
		if (len != NULL)
			len[k] = size + Room;
		k++;
	}
	free(b);
	return k;
}

/* A group's type, one of the three but now and then. */
static uint16_t
type(void)
{
	return rndn(64) == 0 ? (uint16_t)rnd() : rndn(3);
}

/* A random value of a group or a step: small, or anything. */
static uint16_t
value(void)
{
	return rndn(4) == 0 ? (uint16_t)rnd() : (uint16_t)rndr(-1100, 1100);
}

static void
randbody(void)
{
	uint8_t *b, *p;
	uint32_t info, np, ng, i, n;

	info = 8 + 2*rndn(6);
	np = rndn(20);
	ng = rndr(1, 24);
	n = 16 + info + 2 + 6*np + 2 + Group*ng;
	b = alloc(n);
	if (b == NULL)
		return;
	fill(b, n);
	put16(b, rndn(16) == 0 ? rnd() & ~INFO_ANIM : rnd() | INFO_ANIM);
	put16(b + 14, info);
	put32(b + Info, 0);
	p = b + Info + info;
	put16(p, np);
	p += 2 + 6*np;
	put16(p, ng);
	for (i = 0; i < ng; i++)
		put16(p + 2 + Group*i + 8, type());
	body[nbody] = b - aarena;
	bodylen[nbody] = n + Room;
	nbody++;
}

static void
randanim(void)
{
	uint8_t *a, *p;
	uint32_t nf, ng, f, g, k;

	nf = rndr(1, 10);
	ng = rndr(1, 24);
	a = alloc(8 + (8 + 8*ng)*nf);
	if (a == NULL)
		return;
	put16(a, nf);
	put16(a + 2, ng);
	put16(a + 4, rndn(nf));
	put16(a + 6, rnd());
	p = a + 8;
	for (f = 0; f < nf; f++) {
		put16(p, rndn(8) == 0 ? 0 : rndn(8) == 0 ? rndr(1, 3000) : rndr(1, 60));
		for (k = 1; k < 4; k++)
			put16(p + 2*k, value());
		p += 8;
		for (g = 0; g < ng; g++) {
			put16(p, type());
			for (k = 1; k < 4; k++)
				put16(p + 2*k, value());
			p += 8;
		}
	}
	anim[nanim++] = a - aarena;
}

/* The two arenas: with gamedir the game's bodies and animations, coming from no frame, and random ones. */
static void
arenas(void)
{
	int i;

	aarena = malloc(Arena);
	carena = malloc(Arena);
	if (aarena == NULL || carena == NULL)
		fatal("anim: out of memory");
	fill(aarena, Arena);
	top = 0;
	nbody = gamebodies = 0;
	nanim = gameanims = 0;
	if (gamedir != NULL) {
		nbody = gamebodies = loadhqr("BODY.HQR", body, bodylen, Nbody/2);
		for (i = 0; i < nbody; i++)
			if (get16(aarena + body[i]) & INFO_ANIM)
				put32(aarena + body[i] + Info, 0);
		nanim = gameanims = loadhqr("ANIM.HQR", anim, NULL, Nanim/2);
	}
	while (nbody < Nbody && top < Arena/2)
		randbody();
	while (nanim < Nanim && top < 3*(Arena/4))
		randanim();
	memcpy(carena, aarena, Arena);
}

/* Whether the assembly's body i is ours, its frame at the same place. */
static int
samebody(int i, int *at)
{
	uint8_t *o, *c;
	uint32_t k;

	o = aarena + body[i];
	c = carena + body[i];
	if (from(1, body[i]) != from(0, body[i])) {
		*at = Info;
		return 0;
	}
	for (k = 0; k < bodylen[i]; k++)
		if ((k < Info || k >= Info+4) && o[k] != c[k]) {
			*at = k;
			return 0;
		}
	return 1;
}

static int
sameasm(char *what, int b, int b2)
{
	int k, at;

	for (k = 0; k < (int)nelem(vars); k++)
		if (memcmp(vars[k].c, av[k], vars[k].size) != 0) {
			fail("%s: %s differs: ours %d, assembly %d", what, vars[k].name,
				vars[k].size == 2 ? (int16_t)get16(vars[k].c) : (int32_t)get32(vars[k].c),
				vars[k].size == 2 ? (int16_t)get16(av[k]) : (int32_t)get32(av[k]));
			return 0;
		}
	if (!samebody(b, &at) || !samebody(b2, &at)) {
		fail("%s: body %d or %d differs from the assembly's at byte %d", what, b, b2, at);
		return 0;
	}
	return 1;
}

/* One of our globals copied to the assembly's. */
static void
toasm(void *c)
{
	int k;

	for (k = 0; k < (int)nelem(vars); k++)
		if (vars[k].c == c) {
			memcpy(av[k], c, vars[k].size);
			return;
		}
	fatal("anim: no such global");
}

/*
 * Whether the assembly's arena is ours whole, the frame each body comes
 * from compared as its offset.
 */
static int
samearenas(void)
{
	uint32_t i, j;
	int k;

	for (k = 0; k < nbody; k++)
		if (from(1, body[k]) != from(0, body[k])) {
			fail("anim: body %d's frame differs from the assembly's", k);
			return 0;
		}
	for (i = 0; i < Arena; i = j + 1) {
		for (j = i; j < Arena && memcmp(aarena + j, carena + j, Arena - j < 4096 ? Arena - j : 4096) == 0; )
			j += Arena - j < 4096 ? Arena - j : 4096;
		while (j < Arena && aarena[j] == carena[j])
			j++;
		if (j == Arena)
			break;
		for (k = 0; k < nbody && !(j >= body[k] + Info && j < body[k] + Info + 4); k++)
			;
		if (k == nbody) {
			fail("anim: the arena differs from the assembly's at byte %#x", j);
			return 0;
		}
	}
	return 1;
}

static int32_t
arg(uint8_t *arena, uint32_t off)
{
	return (int32_t)(uintptr_t)(arena + off);
}

void
testanim(void)
{
	uintptr_t s[nelem(names)];
	int32_t a[5], ra, rc;
	uint32_t clock, slot, n;
	uint16_t nf;
	int r, op, b, b2, an, frame, k;
	char *what;

	for (k = 0; k < (int)nelem(vars); k++)
		av[k] = (uint8_t*)lookup(vars[k].name);
	for (k = 0; k < (int)nelem(names); k++)
		s[k] = lookup(names[k]);
	arenas();
	clock = rnd();
	CmptMemoTimerRef = 0;
	toasm(&CmptMemoTimerRef);
	b = b2 = an = frame = 0;
	for (r = 0; r < 10*Rounds; r++) {
		/*
		 * Episodes of 32 calls on a body and an animation (now and then
		 * another), a few ticks apart, the frame aimed at moving on as
		 * it is reached. At times a body is put back to coming from no
		 * frame, and the clock is near 0, so that it left it at its
		 * duration.
		 */
		if (r % 32 == 0) {
			b = rndn(nbody);
			b2 = rndn(nbody);
			an = rndn(nanim);
			frame = 0;
			if (rndn(4) == 0) {
				put32(aarena + body[b] + Info, 0);
				put32(carena + body[b] + Info, 0);
				if (rnd() & 1)
					clock = rndn(100);
			}
		}
		if (rndn(16) == 0)
			an = rndn(nanim);
		clock += rndn(50) == 0 ? rndn(5000) : rndn(8);
		TimerRef = clock;
		toasm(&TimerRef);
		nf = get16(carena + anim[an]);
		if (nf != 0)
			frame %= nf;
		n = rndn(32);
		for (op = 0; n >= (uint32_t)weights[op]; op++)
			n -= weights[op];
		/* the timer saved and restored in pairs, nested, as the game does, but once in 16 */
		if (op == 7 && CmptMemoTimerRef <= 0 && rndn(16) != 0)
			op = 6;
		else if (op == 6 && CmptMemoTimerRef >= 3)
			op = 7;
		what = names[op];
		ra = rc = 0;
		switch (op) {
		case 0:
			/* a frame in eight past the animation's, of which one in eight negative, refused unsigned */
			a[0] = rndn(8) != 0 ? frame : rndn(8) == 0 ? -(int32_t)rndr(1, 1000) : (int32_t)rndn(nf + 4);
			a[1] = arg(aarena, anim[an]);
			a[2] = arg(aarena, body[b]);
			abicall(s[op], 3, a);
			SetAnimObjet(a[0], carena + anim[an], carena + body[b]);
			break;
		case 1:
		case 2:
		case 3:
			if (nf == 0)
				continue;
			a[0] = frame;
			a[1] = arg(aarena, anim[an]);
			a[2] = arg(aarena, body[b]);
			ra = abicall(s[op], 3, a);
			if (op == 1)
				rc = SetInterAnimObjet(frame, carena + anim[an], carena + body[b]);
			else if (op == 2)
				rc = SetInterAnimObjet2(frame, carena + anim[an], carena + body[b]);
			else
				rc = SetInterDepObjet(frame, carena + anim[an], carena + body[b]);
			if (rc == 1)
				frame++;
			break;
		case 4:
			slot = Arena - Buffer + 2*rndn(Buffer/4);
			a[0] = arg(aarena, slot);
			a[1] = arg(aarena, body[b]);
			ra = (int16_t)abicall(s[op], 2, a);
			rc = StockInterAnim(carena + slot, carena + body[b]);
			n = (uint16_t)rc;
			if (n > Buffer/4)
				n = Buffer / 4;
			if (memcmp(aarena + slot, carena + slot, n) != 0) {
				fail("StockInterAnim of body %d: the frame differs from the assembly's", b);
				goto out;
			}
			break;
		case 5:
			a[0] = arg(aarena, body[b2]);
			a[1] = arg(aarena, body[b]);
			abicall(s[op], 2, a);
			CopyInterAnim(carena + body[b2], carena + body[b]);
			break;
		case 6:
			abicall(s[op], 0, a);
			SaveTimer();
			break;
		case 7:
			abicall(s[op], 0, a);
			RestoreTimer();
			break;
		case 8:
			a[0] = arg(aarena, anim[an]);
			ra = abicall(s[op], 1, a);
			rc = GetNbFramesAnim(carena + anim[an]);
			if (!abiok(what))
				goto out;
			if (ra != rc) {
				fail("%s: ours %d, the assembly's %d", what, rc, ra);
				goto out;
			}
			op = 11;
			what = names[op];
			ra = abicall(s[op], 1, a);
			rc = GetBouclageAnim(carena + anim[an]);
			break;
		case 9:
			a[0] = rndn(30);
			a[1] = rnd();
			a[2] = rnd();
			a[3] = rnd();
			a[4] = arg(aarena, body[b]);
			abicall(s[op], 5, a);
			SetAngleGroupe(a[0], a[1], a[2], a[3], carena + body[b]);
			break;
		case 10:
			a[0] = rndn(30);
			a[1] = arg(aarena, body[b]);
			abicall(s[op], 2, a);
			GetAngleGroupe(a[0], carena + body[b]);
			/* the assembly's returns with EDI changed, which the C keeps (p_anim.c) */
			abibroke &= ~4;
			break;
		}
		if (!abiok(what))
			break;
		if (ra != rc) {
			fail("%s: ours %d, the assembly's %d", what, rc, ra);
			break;
		}
		if (!sameasm(what, b, b2))
			break;
		if (r % 1000 == 999 && !samearenas())
			break;
	}
out:
	printf("anim: %d rounds, over %d bodies and %d animations, %d and %d the game's\n", r, nbody, nanim,
		gamebodies, gameanims);
	free(aarena);
	free(carena);
}

/* a body for CopyInterAnim: animated, 8 bytes of information, np points, ng groups */
static void
copybody(uint8_t *b, uint32_t len, uint32_t np, uint32_t ng)
{
	uint8_t *p;

	fill(b, len);
	put16(b, INFO_ANIM);
	put16(b + 14, 8);
	p = b + 24;
	put16(p, np);
	p += 2 + 6*np;
	put16(p, ng);
}


enum
{
	Many = 65536,	/* the groups LBA.EXE moved for a count of 0 */
	Countbody = 24 + 2 + 6*8 + 2 + Group*Many + 64,
	Countanim = 8 + 2*(8 + 8*Many) + 64,
};

/* a body of np points and ng groups, as copybody makes, its frame none */
static void
countbody(uint8_t *b, uint32_t np, uint32_t ng)
{
	copybody(b, Countbody, np, ng);
	put32(b + Info, 0);
}

/* an animation of 2 frames of ng groups, each lasting time */
static void
countanim(uint8_t *a, uint32_t ng, uint32_t time)
{
	uint32_t i;

	fill(a, Countanim);
	put16(a, 2);
	put16(a + 2, ng);
	put16(a + 4, 0);
	for (i = 0; i < 2; i++)
		put16(a + 8 + (8*ng + 8)*i, time);
}

/* p_anim's globals of the frame's steps and turns, the assembly's names and ours */
static char *countnames[] = {
	"AnimStepX", "AnimStepY", "AnimStepZ", "AnimMasterRot",
	"AnimStepAlpha", "AnimStepBeta", "AnimStepGamma",
};
static int16_t *countours[] = {
	&AnimStepX, &AnimStepY, &AnimStepZ, &AnimMasterRot,
	&AnimStepAlpha, &AnimStepBeta, &AnimStepGamma,
};

/* whether ours and the assembly's are the same */
static int
countvars(void)
{
	uint32_t i;

	for (i = 0; i < nelem(countnames); i++)
		if (memcmp((void*)lookup(countnames[i]), countours[i], 2) != 0)
			return 0;
	return 1;
}

/* the assembly's made ours, which the cases before may have left apart */
static void
countsync(void)
{
	uint32_t i;

	for (i = 0; i < nelem(countnames); i++)
		memcpy((void*)lookup(countnames[i]), countours[i], 2);
}

/*
 * SetAnimObjet, SetInterAnimObjet and SetInterAnimObjet2 (on the way to
 * the frame and there), StockInterAnim and CopyInterAnim, for bodies and
 * animations of which one has no group: LBA.EXE's assembly moved 65536
 * groups for a count of 0 (65535 after the first on the way to a frame),
 * over the body and past it. The build's must move none, as ours: the
 * bodies, the frames and the globals the same, the frame a body comes
 * from aside, its address in the one and its offset in the other.
 */
static int
fixcount(void)
{
	static uint8_t body[2][Countbody], other[2][Countbody], anim[2][Countanim];
	static char *names[] = {
		"SetAnimObjet", "SetInterAnimObjet", "SetInterAnimObjet2", "StockInterAnim", "CopyInterAnim",
	};
	uintptr_t f[nelem(names)];
	uint8_t *keep;
	uint32_t *atimer, time, np, nb, na;
	int32_t rc, ra, frame;
	int r, op, there;

	for (op = 0; op < (int)nelem(names); op++)
		f[op] = lookup(names[op]);
	atimer = (uint32_t*)lookup("TimerRef");
	keep = carena;
	carena = body[0];
	countsync();
	for (r = 0; r < 200; r++) {
		op = rndn(nelem(names));
		np = rndr(1, 8);
		nb = op >= 3 || rndn(2) == 0 ? 0 : rndr(1, 5);
		na = nb == 0 ? rndr(0, 5) : 0;
		time = rndr(2, 100);
		there = rndn(2);
		countbody(body[0], np, nb);
		memcpy(body[1], body[0], Countbody);
		countanim(anim[0], na, time);
		memcpy(anim[1], anim[0], Countanim);
		countbody(other[0], rndr(1, 8), op == 4 ? (uint32_t)rndr(1, 5) : 0);
		memcpy(other[1], other[0], Countbody);
		TimerRef = *atimer = there ? 2*time + rndn(100) : time + rndn(time);
		frame = rndn(2);
		rc = ra = 0;
		switch (op) {
		case 0:
			acall(f[op], 3, frame, (int32_t)(uintptr_t)anim[1], (int32_t)(uintptr_t)body[1]);
			SetAnimObjet(frame, anim[0], body[0]);
			break;
		case 1:
			ra = acall(f[op], 3, frame, (int32_t)(uintptr_t)anim[1], (int32_t)(uintptr_t)body[1]);
			rc = SetInterAnimObjet(frame, anim[0], body[0]);
			break;
		case 2:
			ra = acall(f[op], 3, frame, (int32_t)(uintptr_t)anim[1], (int32_t)(uintptr_t)body[1]);
			rc = SetInterAnimObjet2(frame, anim[0], body[0]);
			break;
		case 3:
			ra = (int16_t)acall(f[op], 2, (int32_t)(uintptr_t)other[1], (int32_t)(uintptr_t)body[1]);
			rc = StockInterAnim(other[0], body[0]);
			break;
		case 4:
			acall(f[op], 2, (int32_t)(uintptr_t)body[1], (int32_t)(uintptr_t)other[1]);
			CopyInterAnim(body[0], other[0]);
			break;
		}
		if (!abiok("fixes: p_anim's count of 0"))
			return 0;
		memset(body[0] + Info, 0, 4);
		memset(body[1] + Info, 0, 4);
		if (op == 4) {
			memset(other[0] + Info, 0, 4);
			memset(other[1] + Info, 0, 4);
		}
		if (rc != ra || memcmp(body[0], body[1], Countbody) != 0 || memcmp(other[0], other[1], Countbody) != 0
		|| memcmp(anim[0], anim[1], Countanim) != 0 || !countvars()) {
			fail("fixes: %s of a body of %u groups, an animation of %u, frame %d, %s it: ours %d, the"
				" assembly's %d, the bodies, frames or globals apart", names[op], nb, na, frame,
				there ? "at" : "on the way to", rc, ra);
			return 0;
		}
	}
	carena = keep;
	printf("fixes: count, %d calls on bodies or animations of no group, none moved\n", r);
	return 1;
}

/* p_anim's fixes (asmoracle.c's fixes). */
int
fixanim(void)
{
	return fixcount();
}
