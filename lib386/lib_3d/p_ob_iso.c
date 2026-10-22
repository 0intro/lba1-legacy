#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../lib_sys/adeline.h"
#include "../lib_svga/lib_svga.h"
#include "lib_3d.h"

/*
 * The bodies of p_ob_iso.asm. AffObjetIso turns a body's points, by its
 * groups' matrices if it is animated, projects them into List_Point,
 * lights its normals into List_Normal, lists its polygons that face the
 * camera, its lines and its spheres in List_Entity, each with a depth
 * in List_Tri, sorts that deepest first and draws it. A body is a
 * header of 16 bytes and a block of info, then its points (a count and
 * 3 words each), its groups if animated (a count and 38 bytes each),
 * its normals (a count and 4 words each), its polygons, lines and
 * spheres (each a count, then their definitions). A point is named by
 * its offset in bytes in the lists of points, which keep 3 words each,
 * the projected ones x, y and a depth. The lists keep the assembly's
 * layouts, for what one drawing leaves in them is read by the next:
 * List_Entity holds a polygon as its type, count of points and colour,
 * then 3 words a point, an intensity (written for Gouraud only), x and
 * y. It holds a line as 4 bytes of the body's and two points' x and y,
 * and a sphere as 6 bytes of the body's, x, y and depth. List_Tri holds
 * 4 words an entity, its depth, its kind and its offset in List_Entity,
 * where the assembly held its address.
 */

enum
{
	Npoint = 500,
	Nentity = 5000,
	Ntri = 1000,
	Group = 38,
	Mat = 9,
	Eline = 0,	/* E_LIGNE */
	Epoly = 1,
	Esphere = 2,
	Persp = 0,	/* TYPE_3D (any other projection is isometric here) */
};

WORD	List_Point[Npoint*3];
WORD	List_Anim_Point[Npoint*3];
WORD	List_Entity[Nentity];
WORD	List_Tri[Ntri*4];
WORD	ScreenXmin, ScreenYmin, ScreenXmax, ScreenYmax;
WORD	NbPoints;
WORD	FlagLight = 1;

static WORD	List_Normal[Npoint];
static uint16_t	TotalEntite;
static uint16_t	NbGroupes;
static int32_t	PosXWr, PosYWr, PosZWr;
static uint8_t	*OffsetDefPoint;
static uint8_t	*ListGroupe;

static uint16_t
get16(void *p)
{
	uint16_t v;

	memcpy(&v, p, 2);
	return v;
}

static void
put16(void *p, uint16_t v)
{
	memcpy(p, &v, 2);
}

static uint32_t
get32(void *p)
{
	uint32_t v;

	memcpy(&v, p, 4);
	return v;
}

static void
put32(void *p, uint32_t v)
{
	memcpy(p, &v, 4);
}

/* the byte off of a list of points */
static uint8_t*
pt(WORD *list, uint32_t off)
{
	return (uint8_t*)list + off;
}

/* PatchObjet is p_ob_iso.asm's PatchObjet: each group's parent, but the first's, from an offset of groups to one of matrices. */
void
PatchObjet(void *ptrobj)
{
	uint8_t *p;
	uint16_t n;

	p = ptrobj;
	if ((get16(p) & INFO_ANIM) == 0)
		return;
	p += 16 + get16(p + 14);
	p += 2 + 6*get16(p);
	n = get16(p);
	p += 2;
	if (--n == 0)
		return;
	do {
		p += Group;
		put16(p + 6, (uint32_t)get16(p + 6) * 36 / 38);
	} while (--n != 0);
}

/* The screen's box widened to x, y. */
static void
boxx(uint16_t x)
{
	if ((int16_t)x < ScreenXmin)
		ScreenXmin = x;
	if ((int16_t)x > ScreenXmax)
		ScreenXmax = x;
}

static void
boxy(uint16_t y)
{
	if ((int16_t)y < ScreenYmin)
		ScreenYmin = y;
	if ((int16_t)y > ScreenYmax)
		ScreenYmax = y;
}

/* The origin of a group's points: its parent's matrix, and X0, Y0, Z0 its point org in List_Anim_Point. */
static LONG*
parent(uint8_t *g)
{
	uint16_t m, org;

	m = get16(g + 6);
	if (m == 0xFFFF) {
		X0 = Y0 = Z0 = 0;
		return LMatriceWorld;
	}
	org = get16(g + 4);
	X0 = (int16_t)get16(pt(List_Anim_Point, org));
	Y0 = (int16_t)get16(pt(List_Anim_Point, org + 2));
	Z0 = (int16_t)get16(pt(List_Anim_Point, org + 4));
	return TabMat + m/4;
}

/* RotateGroupe: the group's matrix, its parent's turned by a, b, c, and its points turned by it. */
static void
RotateGroupe(uint8_t *g, LONG *mat, int32_t a, int32_t b, int32_t c)
{
	LONG *src;

	lAlpha = a;
	lBeta = b;
	lGamma = c;
	src = parent(g);
	RotMatIndex2(mat, src);
	RotList(mat, OffsetDefPoint + get16(g), (WORD*)pt(List_Anim_Point, get16(g)), get16(g + 2));
}

/* TranslateGroupe: the group's matrix its parent's, and its points moved by a, b, c and turned by it. */
static void
TranslateGroupe(uint8_t *g, LONG *mat, int32_t a, int32_t b, int32_t c)
{
	lAlpha = a;
	lBeta = b;
	lGamma = c;
	CopyMatrice(parent(g), mat);
	TransRotList(mat, OffsetDefPoint + get16(g), (WORD*)pt(List_Anim_Point, get16(g)), get16(g + 2));
}

/*
 * The projection of AnimNuage: each point of List_Anim_Point moved by
 * the body's position into List_Point, in isometry or perspective, and
 * the screen's box widened to it. In isometry the depth is that of
 * the iso view, in 16 bits. In perspective a coordinate beyond 16 bits
 * unsigned becomes its high word or 7FFF, so that one below 0 becomes
 * -1, and the depth plus KFactor, if not above 0, 7FFFFFFF.
 */
static void
project(void)
{
	uint8_t *s, *d;
	uint32_t x, y, z, a;
	int32_t k;
	uint16_t n, v;

	s = (uint8_t*)List_Anim_Point;
	d = (uint8_t*)List_Point;
	n = NbPoints;
	do {
		x = (int16_t)get16(s) + (uint32_t)PosXWr;
		y = (int16_t)get16(s + 2) + (uint32_t)PosYWr;
		z = 0u - ((int16_t)get16(s + 4) + (uint32_t)PosZWr);
		s += 6;
		if (TypeProj != Persp) {
			v = (uint16_t)((int32_t)((z + x) * 24) >> 9) + (uint16_t)XCentre;
			put16(d, v);
			boxx(v);
			a = (x - z)*12 - 30*y;
			v = (uint16_t)((int32_t)a >> 9) + (uint16_t)YCentre;
			put16(d + 2, v);
			boxy(v);
			put16(d + 4, (uint16_t)(z - x) - (uint16_t)y);
		} else {
			if ((int64_t)(int32_t)z + KFactor <= 0)
				k = 0x7FFFFFFF;
			else
				k = z + (uint32_t)KFactor;
			/*
			 * FIXED: LBA.EXE's idiv faults on a quotient past 32
			 * bits. This and the Y below divide in 64 bits and
			 * keep the low 32.
			 */
			a = (uint32_t)((int64_t)(int32_t)x * LFactorX / k) + (uint32_t)XCentre;
			v = a > 0xFFFF ? a >> 16 | 0x7FFF : a;
			put16(d, v);
			boxx(v);
			a = (uint32_t)((int64_t)(int32_t)(0u - y) * LFactorY / k) + (uint32_t)YCentre;
			v = a > 0xFFFF ? a >> 16 | 0x7FFF : a;
			put16(d + 2, v);
			boxy(v);
			put16(d + 4, (uint32_t)k > 0xFFFF ? (uint32_t)k >> 16 | 0x7FFF : (uint32_t)k);
		}
		d += 6;
	} while (--n != 0);
}

/*
 * ComputeAnimNormal: each group's normals turned by its matrix, their
 * intensity under the light into List_Normal, 0 for those turned away.
 * LMatriceRot holds each group's matrix scaled by the light in turn.
 */
static uint8_t*
ComputeAnimNormal(uint8_t *p)
{
	uint8_t *g;
	LONG *m, *l, n[3];
	WORD *dst;
	uint32_t s;
	int32_t x, y, z;
	uint16_t k;
	int i;

	k = get16(p);
	p += 2;
	if (k == 0)
		return p;
	dst = List_Normal;
	m = TabMat;
	l = LMatriceRot;
	n[0] = NormalXLight;
	n[1] = NormalYLight;
	n[2] = NormalZLight;
	g = ListGroupe + 18;
	k = NbGroupes;
	do {
		if (get16(g) != 0) {
			compteur = get16(g);
			for (i = 0; i < 9; i++)
				l[i] = (uint32_t)m[i] * (uint32_t)n[i/3];
			do {
				x = (int16_t)get16(p);
				y = (int16_t)get16(p + 2);
				z = (int16_t)get16(p + 4);
				s = 0;
				for (i = 0; i < 9; i += 3)
					s += (uint32_t)l[i]*x + (uint32_t)l[i+1]*y + (uint32_t)l[i+2]*z;
				/*
				 * FIXED: LBA.EXE's 16-bit idiv faults on a
				 * quotient past 16 bits. This divides in 32
				 * bits and keeps the low 16.
				 */
				if ((int32_t)s < 0)
					*dst++ = 0;
				else
					*dst++ = (int16_t)((int32_t)s >> 14) / (int16_t)get16(p + 6);
				p += 8;
			} while (--compteur != 0);
		}
		g += Group;
		m += Mat;
	} while (--k != 0);
	return p;
}

/*
 * AnimNuage: the groups' matrices and points, the first group turned by
 * the body's angles, the others by their own (or moved, for a
 * translation, a group of another kind left), then projected, and the
 * normals lit.
 */
static uint8_t*
AnimNuage(uint8_t *p)
{
	uint8_t *g;
	LONG *m;
	uint16_t n, type;

	NbPoints = get16(p);
	p += 2;
	OffsetDefPoint = p;
	p += 6 * (uint16_t)NbPoints;
	NbGroupes = get16(p);
	p += 2;
	ListGroupe = p;
	RotateGroupe(p, TabMat, lAlpha, lBeta, lGamma);
	g = p + Group;
	n = NbGroupes - 1;
	if (n != 0) {
		m = TabMat + Mat;
		do {
			type = get16(g + 8);
			if (type == TYPE_ROTATE)
				RotateGroupe(g, m, (int16_t)get16(g + 10), (int16_t)get16(g + 12), (int16_t)get16(g + 14));
			else if (type == TYPE_TRANSLATE)
				TranslateGroupe(g, m, (int16_t)get16(g + 10), (int16_t)get16(g + 12), (int16_t)get16(g + 14));
			m += Mat;
			g += Group;
		} while (--n != 0);
	}
	project();
	return ComputeAnimNormal(g);
}

/*
 * RotateNuage: the points of a body without groups turned by its angles
 * and projected in isometry, whatever the projection, the depth that
 * of the turned point, then its normals lit. The assembly gave Proj_ISO
 * registers whose low halves it had set and whose high halves were as
 * Rot left them: Y0's, Z0's, and x times LMatriceRot's seventh word.
 */
static uint8_t*
RotateNuage(uint8_t *p)
{
	uint8_t *d;
	uint32_t ex, ey, ez;
	int32_t sum;
	uint16_t n, z, x;
	WORD *dst;

	RotMatW();
	NbPoints = get16(p);
	p += 2;
	d = (uint8_t*)List_Point;
	n = NbPoints;
	do {
		x = get16(p);
		Rot(x, get16(p + 2), get16(p + 4));
		p += 6;
		z = 0u - ((uint16_t)Z0 + (uint16_t)PosZWr);
		ex = ((uint32_t)Y0 & 0xFFFF0000) | (uint16_t)((uint16_t)X0 - (uint16_t)PosXWr);
		ey = ((uint32_t)Z0 & 0xFFFF0000) | (uint16_t)((uint16_t)Y0 - (uint16_t)PosYWr);
		ez = ((uint32_t)(int16_t)x * (uint32_t)LMatriceRot[6] & 0xFFFF0000) | z;
		Proj_ISO(ex, ey, ez, (WORD*)d, (WORD*)(d + 2));
		boxx(get16(d));
		boxy(get16(d + 2));
		put16(d + 4, z);
		d += 6;
	} while (--n != 0);
	n = get16(p);
	p += 2;
	if (n == 0)
		return p;
	dst = List_Normal;
	do {
		Rot(get16(p), get16(p + 2), get16(p + 4));
		sum = (uint32_t)((int16_t)X0 * (int16_t)NormalXLight) + (uint32_t)((int16_t)Y0 * (int16_t)NormalYLight)
			+ (uint32_t)((int16_t)Z0 * (int16_t)NormalZLight);
		/*
		 * FIXED: LBA.EXE's 16-bit idiv faults on a quotient past 16
		 * bits. This divides in 32 bits and keeps the low 16.
		 */
		if (sum < 0)
			*dst++ = 0;
		else
			*dst++ = sum / (int16_t)get16(p + 6);
		p += 8;
	} while (--n != 0);
	return p;
}

/* An entry of List_Tri: an entity's depth, kind and place in List_Entity. */
static void
tri(uint16_t i, uint16_t z, uint16_t kind, uint8_t *e)
{
	uint8_t *t;

	t = (uint8_t*)List_Tri + 8*i;
	put16(t, z);
	put16(t + 2, kind);
	put32(t + 4, e - (uint8_t*)List_Entity);
}

static int16_t
key(int i)
{
	return get16((uint8_t*)List_Tri + 8*i);
}

static void
swap(int i, int j)
{
	uint8_t t[8], *a, *b;

	a = (uint8_t*)List_Tri + 8*i;
	b = (uint8_t*)List_Tri + 8*j;
	memcpy(t, a, 8);
	memmove(a, b, 8);
	memcpy(b, t, 8);
}

/*
 * SergeSort, p_ob_iso.asm's: List_Tri's entries 0 to last, deepest
 * first, as the assembly moved them, for the order of equal depths is
 * the order of drawing. A range of 2 is put in order, one of 3 to 8 by
 * selection, the first of the deepest put first. A larger one is cut
 * about its first entry, scanned from both ends on one count, the range
 * after the cut kept for later and the one before sorted on.
 */
static void
SergeSort(int last)
{
	int stack[2*Ntri], sp, i, j, f, l, t, k, m, cx, bp;
	int16_t pivot, max;

	sp = 0;
	i = 0;
	l = last;
	cx = l - i;
	for (;;) {
		if (cx == 1) {
			if (key(i) < key(l))
				swap(i, l);
		} else if (cx <= 7) {
			bp = cx;
			do {
				max = key(i);
				m = -1;
				for (k = i + 1; cx > 0; k++, cx--)
					if (key(k) > max) {
						m = k;
						max = key(k);
					}
				if (m >= 0)
					swap(i, m);
				i++;
				cx = --bp;
			} while (bp != 0);
		} else {
			f = i;
			pivot = key(i);
			i++;
			j = l;
			for (;;) {
				if (key(i) >= pivot) {
					i++;
					if (--cx == 0)
						break;
					continue;
				}
				while (key(j) <= pivot) {
					j--;
					if (--cx == 0)
						goto cut;
				}
				swap(i, j);
			}
		cut:
			t = i;
			i = j;
			j = t;
			swap(f, i);
			if (j < l) {
				stack[sp++] = j;
				stack[sp++] = l;
			}
			i--;
			if (f < i) {
				l = i;
				i = f;
				cx = l - i;
				continue;
			}
		}
		if (sp == 0)
			return;
		l = stack[--sp];
		i = stack[--sp];
		cx = l - i;
	}
}

/* aff_E_POLY: a polygon of List_Entity framed and filled. */
static void
affpoly(uint8_t *e)
{
	uint32_t c;

	c = get32(e);
	TypePoly = c & 0xFF;
	NbPolyPoints = c >> 8 & 0xFF;
	memcpy(TabPoly, e + 4, 6*(size_t)NbPolyPoints);
	if (ComputePoly() != 0)
		FillVertic(c & 0xFF, c >> 16);
}

/* aff_E_LIGNE: a line, in the colour of the body's line's second byte. */
static void
affline(uint8_t *e)
{
	uint16_t w;

	w = get16(e);
	Line((int16_t)get16(e + 4), (int16_t)get16(e + 6), (int16_t)get16(e + 8), (int16_t)get16(e + 10),
		(uint16_t)(w << 8 | w >> 8));
}

/*
 * aff_E_SPHERE: a sphere, its radius projected in 16 bits, the screen's
 * box widened to it, its centre taken unsigned.
 */
static void
affsphere(uint8_t *e)
{
	uint32_t r;
	uint16_t x, y, rr;
	int16_t d;

	r = get16(e + 4);
	x = get16(e + 6);
	y = get16(e + 8);
	if (TypeProj == Persp) {
		d = get16(e + 10) + (uint16_t)KFactor;
		/*
		 * FIXED: LBA.EXE's 16-bit idiv faults on a quotient past 16
		 * bits. This divides in 32 bits and keeps the low 16.
		 */
		r = (uint16_t)((int32_t)(int16_t)r * (int16_t)LFactorX / d);
	} else
		r = r*34 >> 9;
	rr = r;
	if ((int16_t)(x + rr) > ScreenXmax)
		ScreenXmax = x + rr;
	if ((int16_t)(x - rr) < ScreenXmin)
		ScreenXmin = x - rr;
	if ((int16_t)(y + rr) > ScreenYmax)
		ScreenYmax = y + rr;
	if ((int16_t)(y - rr) < ScreenYmin)
		ScreenYmin = y - rr;
	if (ComputeSphere(x, y, r) != 0)
		FillVertic(e[0], get16(e + 1));
}

/*
 * A polygon of the body into List_Entity at e, its points' x and y from
 * List_Point. Its depth, the greatest of its points'. Past it, or NULL
 * if it is turned away. A material from 9 (MAT_GOURAUD) is a Gouraud
 * fill, its points' intensities their normals' plus the colour's low
 * byte. 7 and 8 (MAT_FLAT) one colour plus a normal's intensity.
 */
static uint8_t*
poly(uint8_t **pp, uint8_t *e, int16_t *zmax)
{
	uint8_t *p, *first;
	uint32_t c;
	uint16_t w, off, col;
	uint8_t mat, n;
	int32_t a, b;

	p = *pp;
	c = get32(p);
	p += 2;
	mat = c;
	n = c >> 8;
	*zmax = -32000;
	if (mat >= 9) {
		put32(e, (c & ~0xFFu) | (uint8_t)(mat - 2));
		p += 2;
		e += 4;
		first = e;
		col = c >> 16 & 0xFF;
		do {
			w = List_Normal[get16(p)];
			put16(e, (w & 0xFF00) | (uint8_t)(w + col));
			off = get16(p + 2);
			p += 4;
			put32(e + 2, get32(pt(List_Point, off)));
			e += 6;
			if ((int16_t)get16(pt(List_Point, off + 4)) > *zmax)
				*zmax = get16(pt(List_Point, off + 4));
		} while (--n != 0);
	} else {
		if (mat >= 7) {
			put16(e, (c & 0xFF00) | (uint8_t)(mat - 7));
			put16(e + 2, get16(p) + List_Normal[get16(p + 2)]);
			p += 4;
		} else {
			put32(e, c);
			p += 2;
		}
		e += 4;
		first = e;
		do {
			off = get16(p);
			p += 2;
			put32(e + 2, get32(pt(List_Point, off)));
			e += 6;
			if ((int16_t)get16(pt(List_Point, off + 4)) > *zmax)
				*zmax = get16(pt(List_Point, off + 4));
		} while (--n != 0);
	}
	*pp = p;
	b = (int16_t)(get16(first + 4) - get16(first + 16)) * (int16_t)(get16(first + 8) - get16(first + 2));
	a = (int16_t)(get16(first + 2) - get16(first + 14)) * (int16_t)(get16(first + 10) - get16(first + 4));
	if (a < b)
		return e;
	return NULL;
}

/*
 * FinNuage: the body's polygons that face the camera, its lines and its
 * spheres listed, sorted and drawn. 0, or 1 and the screen's box -1 if
 * there is nothing to draw.
 */
static LONG
FinNuage(uint8_t *p)
{
	uint8_t *e, *next, *t;
	uint16_t n, off, z;
	int16_t zmax;

	e = (uint8_t*)List_Entity;
	TotalEntite = 0;
	n = get16(p);
	p += 2;
	if (n != 0)
		do {
			next = poly(&p, e, &zmax);
			if (next != NULL) {
				tri(TotalEntite++, zmax, Epoly, e);
				e = next;
			}
		} while (--n != 0);
	n = get16(p);
	p += 2;
	if (n != 0)
		do {
			put32(e, get32(p));
			off = get16(p + 4);
			put32(e + 4, get32(pt(List_Point, off)));
			z = get16(pt(List_Point, off + 4));
			off = get16(p + 6);
			p += 8;
			put32(e + 8, get32(pt(List_Point, off)));
			if ((int16_t)get16(pt(List_Point, off + 4)) >= (int16_t)z)
				z = get16(pt(List_Point, off + 4));
			tri(TotalEntite++, z, Eline, e);
			e += 12;
		} while (--n != 0);
	n = get16(p);
	p += 2;
	if (n != 0)
		do {
			memcpy(e, p, 6);
			off = get16(p + 6);
			p += 8;
			memcpy(e + 6, pt(List_Point, off), 6);
			tri(TotalEntite++, get16(e + 10), Esphere, e);
			e += 12;
		} while (--n != 0);
	if (TotalEntite > 1)
		SergeSort(TotalEntite - 1);
	if (TotalEntite == 0) {
		ScreenXmax = ScreenYmax = ScreenXmin = ScreenYmin = -1;
		return 1;
	}
	t = (uint8_t*)List_Tri;
	n = TotalEntite;
	do {
		e = (uint8_t*)List_Entity + get32(t + 4);
		switch (get16(t + 2)) {
		case Eline:
			affline(e);
			break;
		case Epoly:
			affpoly(e);
			break;
		case Esphere:
			affsphere(e);
			break;
		}
		t += 8;
	} while (--n != 0);
	return 0;
}

/*
 * AffObjetIso is p_ob_iso.asm's AffObjetIso: the body at xwr, ywr, zwr
 * (in the world, turned into the camera's view in perspective), turned
 * by palpha, pbeta, pgamma, drawn into Log, and the box of the screen
 * it covers in ScreenXmin to ScreenYmax.
 */
LONG
AffObjetIso(LONG xwr, LONG ywr, LONG zwr, LONG palpha, LONG pbeta, LONG pgamma, void *ptrobj)
{
	uint8_t *p;

	lAlpha = palpha;
	lBeta = pbeta;
	lGamma = pgamma;
	ScreenXmin = ScreenYmin = 32767;
	ScreenXmax = ScreenYmax = -32767;
	if (TypeProj == Persp) {
		LongWorldRot(xwr, ywr, zwr);
		PosXWr = (uint32_t)X0 - (uint32_t)CameraXr;
		PosYWr = (uint32_t)Y0 - (uint32_t)CameraYr;
		PosZWr = (uint32_t)Z0 - (uint32_t)CameraZr;
	} else {
		PosXWr = xwr;
		PosYWr = ywr;
		PosZWr = zwr;
	}
	p = ptrobj;
	if (get16(p) & INFO_ANIM)
		p = AnimNuage(p + 16 + get16(p + 14));
	else
		p = RotateNuage(p + 16 + get16(p + 14));
	return FinNuage(p);
}
