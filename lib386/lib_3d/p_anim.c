#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../lib_sys/adeline.h"
#include "../lib_sys/lib_sys.h"
#include "lib_3d.h"

/*
 * The animations of p_anim.asm. A body with INFO_ANIM keeps, in the
 * block of info that follows its 14 bytes of header and the word of the
 * block's size, the frame it comes from (0 for none) and the time it
 * left it. After the block come its points, a word and 6 bytes
 * each, then its groups, a word and 38 bytes each, of which the 8 from
 * byte 8 move: a type, then three angles or steps. An animation is its
 * frames' count, its groups' count, the frame it loops to and a word,
 * then the frames, each a duration, three steps and those 8 bytes for
 * each group, the first group's turning the whole body. Between frames
 * a group moves from the frame its body came from to the next by the
 * time spent over the next's duration, in 16 bits.
 */

enum
{
	Info = 16,	/* the body's frame and time */
	Group = 38,
	Gwords = 8,	/* the bytes of a group that move, from its byte 8 */
};

ULONG	MemoTimerRef;
LONG	CmptMemoTimerRef;
WORD	AnimStepX, AnimStepY, AnimStepZ;
WORD	AnimMasterRot, AnimStepAlpha, AnimStepBeta, AnimStepGamma;

static uint16_t
get16(uint8_t *p)
{
	uint16_t v;

	memcpy(&v, p, 2);
	return v;
}

static void
put16(uint8_t *p, uint16_t v)
{
	memcpy(p, &v, 2);
}

static uint32_t
get32(uint8_t *p)
{
	uint32_t v;

	memcpy(&v, p, 4);
	return v;
}

static void
put32(uint8_t *p, uint32_t v)
{
	memcpy(p, &v, 4);
}

/*
 * The frame a body comes from, its address in 4 bytes, as the assembly
 * keeps it: the game's memory lies below 2 GB (sys/mem.c), so that a
 * 64-bit host keeps it too.
 */
static uint8_t*
getframe(uint8_t *body)
{
	return (uint8_t*)(intptr_t)(int32_t)get32(body + Info);
}

static void
setframe(uint8_t *body, uint8_t *frame)
{
	put32(body + Info, (uint32_t)(uintptr_t)frame);
}

/* The body's first group's animated words, and *n the count of its groups. */
static uint8_t*
groups(uint8_t *body, uint16_t *n)
{
	uint8_t *p;

	p = body + Info + get16(body + 14);
	p += 2 + 6*get16(p);
	*n = get16(p);
	return p + 2 + 8;
}

/* Frame n of an animation. */
static uint8_t*
frame(uint8_t *anim, uint32_t n)
{
	return anim + 8 + (8*get16(anim + 2) + 8) * n;
}

/* n groups' moving bytes from s to d, sstep and dstep apart (8 in a frame, 38 in a body). */
static void
copygroups(uint8_t *d, uint32_t dstep, uint8_t *s, uint32_t sstep, uint16_t n)
{
	/*
	 * FIXED: LBA.EXE moves 65536 groups for a count of 0, over the body
	 * and past it. A count of 0 returns at once. Taken from DotEmu's
	 * Android 1.06.
	 */
	if (n == 0)
		return;
	do {
		memcpy(d, s, Gwords);
		d += dstep;
		s += sstep;
	} while (--n != 0);
}

/* The frame's steps, and its first group's words as they are. */
static void
steps(uint8_t *f)
{
	AnimStepX = get16(f + 2);
	AnimStepY = get16(f + 4);
	AnimStepZ = get16(f + 6);
	AnimMasterRot = get16(f + 8);
	AnimStepAlpha = get16(f + 10);
	AnimStepBeta = get16(f + 12);
	AnimStepGamma = get16(f + 14);
}

/* v*t/time as the assembly's 16-bit imul and idiv made it */
static uint16_t
inter(uint16_t v, uint32_t t, uint32_t time)
{
	/*
	 * FIXED: LBA.EXE's 16-bit idiv faults on a quotient past 16 bits. This
	 * divides in 32 bits and keeps the low 16.
	 */
	return (int32_t)(int16_t)v * (int16_t)t / (int16_t)time;
}

/* The frame's steps, and its first group's turns, by t over time. Its first group's type. */
static void
intersteps(uint8_t *f, uint32_t t, uint32_t time)
{
	AnimMasterRot = get16(f + 8);
	AnimStepAlpha = inter(get16(f + 10), t, time);
	AnimStepBeta = inter(get16(f + 12), t, time);
	AnimStepGamma = inter(get16(f + 14), t, time);
	AnimStepX = inter(get16(f + 2), t, time);
	AnimStepY = inter(get16(f + 4), t, time);
	AnimStepZ = inter(get16(f + 6), t, time);
}

/*
 * PatchInterAngle and PatchInterStep: a group's word from s's to d's by
 * t over time. An angle (rot) modulo 1024, the shorter way round.
 */
static uint16_t
patchinter(uint16_t s, uint16_t d, uint32_t t, uint32_t time, int rot)
{
	int16_t delta;

	if (!rot)
		return s + inter(d - s, t, time);
	s &= 1023;
	delta = (d & 1023) - s;
	if (delta < -512)
		delta += 1024;
	else if (delta > 512)
		delta -= 1024;
	return (s + inter(delta, t, time)) & 1023;
}

/*
 * The n groups at g (a body's) from those at s to those at d, by t over
 * time, each taking d's type. A group of no type known takes its type
 * alone, and the groups after it are read and written out of step by 6
 * bytes, as the assembly did.
 */
static void
patch(uint8_t *g, uint8_t *s, uint8_t *d, uint16_t n, uint32_t t, uint32_t time)
{
	uint16_t type;
	int i;

	do {
		type = get16(d);
		put16(g, type);
		g += 2;
		s += 2;
		d += 2;
		if (type == TYPE_ROTATE || type == TYPE_TRANSLATE || type == TYPE_ZOOM)
			for (i = 0; i < 3; i++) {
				put16(g, patchinter(get16(s), get16(d), t, time, type == TYPE_ROTATE));
				g += 2;
				s += 2;
				d += 2;
			}
		g += Group - 8;
	} while (--n != 0);
}

/* SaveTimer is p_anim.asm's SaveTimer: TimerRef kept by the first of nested calls. */
void
SaveTimer(void)
{
	if (CmptMemoTimerRef == 0)
		MemoTimerRef = TimerRef;
	CmptMemoTimerRef++;
}

/* RestoreTimer is p_anim.asm's RestoreTimer: TimerRef put back by the last. */
void
RestoreTimer(void)
{
	if (--CmptMemoTimerRef == 0)
		TimerRef = MemoTimerRef;
}

/*
 * The word of the body's groups' count, as SetAngleGroupe and
 * GetAngleGroupe find it. NULL if the body has no animation or fewer
 * groups than numgroupe.
 */
static uint8_t*
groupcount(uint8_t *body, uint32_t numgroupe)
{
	uint8_t *p;

	if ((get16(body) & INFO_ANIM) == 0)
		return NULL;
	p = body + Info + get16(body + 14);
	p += 2 + 6*get16(p);
	if (get16(p) < numgroupe)
		return NULL;
	return p;
}

/*
 * SetAngleGroupe is p_anim.asm's SetAngleGroupe: the three angles of a
 * group set, but the group is the one past the last, its count taken
 * for numgroupe.
 */
void
SetAngleGroupe(int32_t numgroupe, int32_t palpha, int32_t pbeta, int32_t pgamma, void *ptrobj)
{
	uint8_t *p;

	p = groupcount(ptrobj, numgroupe);
	if (p == NULL)
		return;
	p += 2 + Group*get16(p) + 10;
	put16(p, palpha);
	put16(p + 2, pbeta);
	put16(p + 4, pgamma);
}

/*
 * GetAngleGroupe is p_anim.asm's GetAngleGroupe: meant to give a
 * group's angles in AnimStepAlpha, Beta and Gamma, it gives the groups'
 * count and the first group's first two words.
 */
void
GetAngleGroupe(int32_t numgroupe, void *ptrobj)
{
	uint8_t *p;

	/*
	 * FIXED: LBA.EXE computes into EDI an address it never uses, and
	 * returns with EDI changed. C keeps the caller's registers.
	 */
	p = groupcount(ptrobj, numgroupe);
	if (p == NULL)
		return;
	AnimStepAlpha = get16(p);
	AnimStepBeta = get16(p + 2);
	AnimStepGamma = get16(p + 4);
}

/* GetNbFramesAnim is p_anim.asm's GetNbFramesAnim. */
LONG
GetNbFramesAnim(void *ptranim)
{
	return get16(ptranim);
}

/* GetBouclageAnim is p_anim.asm's GetBouclageAnim: the frame the animation loops to. */
LONG
GetBouclageAnim(void *ptranim)
{
	return get16((uint8_t*)ptranim + 4);
}

/* SetAnimObjet is p_anim.asm's SetAnimObjet: the body put in frame numframe, from now. */
void
SetAnimObjet(LONG numframe, void *ptranim, void *ptrobj)
{
	uint8_t *a, *o, *f, *g;
	uint16_t n, m;

	a = ptranim;
	o = ptrobj;
	if ((uint32_t)numframe >= get16(a))
		return;
	f = frame(a, numframe);
	if ((get16(o) & INFO_ANIM) == 0)
		return;
	setframe(o, f);
	put32(o + Info + 4, TimerRef);
	n = get16(a + 2);
	g = groups(o, &m);
	if (n > m)
		n = m;
	copygroups(g, Group, f + 8, 8, n);
	steps(f);
}

/*
 * The frame framedest of an animation, the body's groups and the time
 * since it left its frame, as the three SetInter procedures begin: a
 * body that comes from no frame comes from this one, and left it at a
 * time equal to its duration. 0 if the body has no animation.
 */
static uint8_t*
interframe(uint8_t *a, int32_t framedest, uint8_t *o, uint8_t **src, uint32_t *t, uint32_t *time)
{
	uint8_t *f;
	uint32_t start;

	f = frame(a, framedest);
	*time = get16(f);
	if ((get16(o) & INFO_ANIM) == 0)
		return NULL;
	*src = getframe(o);
	start = get32(o + Info + 4);
	if (*src == NULL) {
		*src = f;
		start = *time;
	}
	*t = TimerRef - start;
	return f;
}

/*
 * SetInterAnimObjet is p_anim.asm's SetInterAnimObjet: the body on its
 * way to frame framedest, 0, or there once the frame's duration has
 * passed, the frame then the one it comes from, 1. On the way, the
 * groups after the first are moved, the first's turns and the frame's
 * steps given by the time spent.
 */
LONG
SetInterAnimObjet(LONG framedest, void *ptranimdest, void *ptrobj)
{
	uint8_t *a, *o, *f, *s, *g;
	uint32_t t, time;
	uint16_t n, m;

	a = ptranimdest;
	o = ptrobj;
	f = interframe(a, framedest, o, &s, &t, &time);
	if (f == NULL)
		return 0;
	n = get16(a + 2);
	g = groups(o, &m);
	if (n > m)
		n = m;
	if (t >= time) {
		copygroups(g, Group, f + 8, 8, n);
		setframe(o, f);
		put32(o + Info + 4, TimerRef);
		steps(f);
		return 1;
	}
	intersteps(f, t, time);
	/*
	 * FIXED: LBA.EXE moves the 65535 groups after the first for a count
	 * of 0, over the body and past it. A count of 0 moves the first
	 * group alone. Taken from DotEmu's Android 1.06.
	 */
	if (n > 1)
		patch(g + Group, s + 16, f + 16, n - 1, t, time);
	return 0;
}

/*
 * SetInterAnimObjet2 is p_anim.asm's SetInterAnimObjet2: the same for
 * the groups alone, steps and turns left as they are. At the frame, the
 * time the body left it is not changed.
 */
LONG
SetInterAnimObjet2(LONG framedest, void *ptranimdest, void *ptrobj)
{
	uint8_t *a, *o, *f, *s, *g;
	uint32_t t, time;
	uint16_t n, m;

	a = ptranimdest;
	o = ptrobj;
	f = interframe(a, framedest, o, &s, &t, &time);
	if (f == NULL)
		return 0;
	n = get16(a + 2);
	g = groups(o, &m);
	if (n > m)
		n = m;
	if (t >= time) {
		copygroups(g, Group, f + 8, 8, n);
		setframe(o, f);
		return 1;
	}
	/*
	 * FIXED: LBA.EXE moves the 65535 groups after the first for a count
	 * of 0, over the body and past it. A count of 0 moves the first
	 * group alone. Taken from DotEmu's Android 1.06.
	 */
	if (n > 1)
		patch(g + Group, s + 16, f + 16, n - 1, t, time);
	return 0;
}

/* SetInterDepObjet is p_anim.asm's SetInterDepObjet: the same for the steps and turns alone, the groups left. */
LONG
SetInterDepObjet(LONG framedest, void *ptranimdest, void *ptrobj)
{
	uint8_t *o, *f, *s;
	uint32_t t, time;

	o = ptrobj;
	f = interframe(ptranimdest, framedest, o, &s, &t, &time);
	if (f == NULL)
		return 0;
	if (t >= time) {
		setframe(o, f);
		put32(o + Info + 4, TimerRef);
		steps(f);
		return 1;
	}
	intersteps(f, t, time);
	return 0;
}

/*
 * StockInterAnim is p_anim.asm's StockInterAnim: the body's groups as
 * they are into a frame at BufferAnim, its first 8 bytes left, which the
 * body then comes from, from now. The frame's size.
 */
WORD
StockInterAnim(UBYTE *BufferAnim, UBYTE *PtrObj)
{
	uint8_t *g;
	uint16_t n;

	if ((get16(PtrObj) & INFO_ANIM) == 0)
		return 0;
	put32(PtrObj + Info + 4, TimerRef);
	setframe(PtrObj, BufferAnim);
	g = groups(PtrObj, &n);
	copygroups(BufferAnim + 8, 8, g, Group, n);
	return 8*n + 8;
}

/* CopyInterAnim is p_anim.asm's CopyInterAnim: a body's frame, time and groups to another's. */
void
CopyInterAnim(void *ptrobjs, void *ptrobjd)
{
	uint8_t *s, *d, *gs, *gd;
	uint16_t n, m;

	s = ptrobjs;
	d = ptrobjd;
	if ((get16(s) & INFO_ANIM) == 0 || (get16(d) & INFO_ANIM) == 0)
		return;
	memcpy(d + Info, s + Info, 8);
	/*
	 * FIXED: LBA.EXE walks the second body with EAX's high half left
	 * from three times the first's points, which from 21846 points on
	 * puts the walk 64 KB or more past the body. C walks each body by
	 * its own counts.
	 */
	gs = groups(s, &n);
	gd = groups(d, &m);
	if (n > m)
		n = m;
	copygroups(gd, Group, gs, Group, n);
}

/*
 * FEATURE: for the saves of the running scene (sources/savegame.c), the
 * frame a body comes from (NULL for none) and the time it left it, NULL
 * and 0 for a body without animation, and both set again. The port's
 * own.
 */
void*
bodyframe(void *ptrobj, ULONG *time)
{
	uint8_t *o;

	o = ptrobj;
	*time = 0;
	if ((get16(o) & INFO_ANIM) == 0)
		return NULL;
	*time = get32(o + Info + 4);
	return getframe(o);
}

void
setbodyframe(void *ptrobj, void *frame, ULONG time)
{
	uint8_t *o;

	o = ptrobj;
	if ((get16(o) & INFO_ANIM) == 0)
		return;
	setframe(o, frame);
	put32(o + Info + 4, time);
}
