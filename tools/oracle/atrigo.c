#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "abi.h"
#include "asmoracle.h"

/*
 * p_trigo's procedures in random order on one state: cameras set and
 * turned, points turned and projected, matrices turned, lists of points
 * turned. Ours and the assembly's each keep their state, compared after
 * each call.
 */

enum
{
	Nmat = 3 + 30,	/* LMatriceRot, LMatriceWorld, LMatriceTempo, TabMat's */
	Npts = 64,
};

void	Rotate(int32_t, int32_t, int32_t);
void	RotatePoint(int32_t, int32_t, int32_t);
void	WorldRotatePoint(int32_t, int32_t, int32_t);
void	LongWorldRotatePoint(int32_t, int32_t, int32_t);
void	LongInverseRotatePoint(int32_t, int32_t, int32_t);
void	RotateMatriceWorld(int32_t, int32_t, int32_t);
void	CopyMatrice(int32_t*, int32_t*);
void	SetFollowCamera(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
void	SetPosCamera(int32_t, int32_t, int32_t);
void	SetAngleCamera(int32_t, int32_t, int32_t);
void	SetInverseAngleCamera(int32_t, int32_t, int32_t);
void	SetProjection(int32_t, int32_t, int32_t, int32_t, int32_t);
void	SetIsoProjection(int32_t, int32_t, int32_t);
int32_t	ProjettePoint(int32_t, int32_t, int32_t);
int32_t	LongProjettePoint(int32_t, int32_t, int32_t);
int32_t	TestVuePoly(void*);
void	SetLightVector(int16_t, int16_t, int16_t);
void	Rot(int32_t, int32_t, int32_t);
void	WorldRot(int32_t, int32_t, int32_t);
void	LongWorldRot(int32_t, int32_t, int32_t);
void	RotMatW(void);
void	RotMatIndex2(int32_t*, int32_t*);
void	RotList(int32_t*, void*, int16_t*, int32_t);
void	TransRotList(int32_t*, void*, int16_t*, int32_t);
void	Proj_3D(int32_t, int32_t, int32_t, int16_t*, int16_t*);
void	Proj_ISO(int32_t, int32_t, int32_t, int16_t*, int16_t*);

extern int16_t	compteur, TypeProj, Xp, Yp, IsoScale;
extern int32_t	XCentre, YCentre, KFactor, LFactorX, LFactorY;
extern int32_t	CameraX, CameraY, CameraZ, CameraXr, CameraYr, CameraZr;
extern int32_t	AlphaLight, BetaLight, GammaLight, Alpha, Beta, Gamma, lAlpha, lBeta, lGamma;
extern int32_t	NormalXLight, NormalYLight, NormalZLight, X0, Y0, Z0;
extern int32_t	LMatriceRot[9], LMatriceWorld[9], LMatriceTempo[9], TabMat[9*30];

typedef struct Var Var;

/* a global of p_trigo's: ours and its size */
struct Var
{
	char	*name;
	void	*c;
	uint32_t	size;
};

static Var vars[] = {
	{"compteur", &compteur, 2},
	{"TypeProj", &TypeProj, 2},
	{"Xp", &Xp, 2},
	{"Yp", &Yp, 2},
	{"IsoScale", &IsoScale, 2},
	{"XCentre", &XCentre, 4},
	{"YCentre", &YCentre, 4},
	{"KFactor", &KFactor, 4},
	{"LFactorX", &LFactorX, 4},
	{"LFactorY", &LFactorY, 4},
	{"CameraX", &CameraX, 4},
	{"CameraY", &CameraY, 4},
	{"CameraZ", &CameraZ, 4},
	{"CameraXr", &CameraXr, 4},
	{"CameraYr", &CameraYr, 4},
	{"CameraZr", &CameraZr, 4},
	{"AlphaLight", &AlphaLight, 4},
	{"BetaLight", &BetaLight, 4},
	{"GammaLight", &GammaLight, 4},
	{"Alpha", &Alpha, 4},
	{"Beta", &Beta, 4},
	{"Gamma", &Gamma, 4},
	{"lAlpha", &lAlpha, 4},
	{"lBeta", &lBeta, 4},
	{"lGamma", &lGamma, 4},
	{"NormalXLight", &NormalXLight, 4},
	{"NormalYLight", &NormalYLight, 4},
	{"NormalZLight", &NormalZLight, 4},
	{"X0", &X0, 4},
	{"Y0", &Y0, 4},
	{"Z0", &Z0, 4},
	{"LMatriceRot", LMatriceRot, 9*4},
	{"LMatriceWorld", LMatriceWorld, 9*4},
	{"LMatriceTempo", LMatriceTempo, 9*4},
	{"TabMat", TabMat, 9*30*4},
};

static char *names[] = {
	"Rotate", "RotatePoint", "WorldRotatePoint", "LongWorldRotatePoint",
	"LongInverseRotatePoint", "RotateMatriceWorld", "CopyMatrice", "SetFollowCamera",
	"SetPosCamera", "SetAngleCamera", "SetInverseAngleCamera", "SetProjection",
	"SetIsoProjection", "ProjettePoint", "LongProjettePoint", "TestVuePoly",
	"SetLightVector", "Rot", "WorldRot", "LongWorldRot",
	"RotMatW", "RotMatIndex2", "RotList", "TransRotList",
	"Proj_3D", "Proj_ISO",
};

/* each Var's assembly's */
static uint8_t *av[nelem(vars)];
static int16_t cpts[2][3*Npts], apts[2][3*Npts];

/* The index in vars of one of our globals. */
static int
var(void *c)
{
	int i;

	for (i = 0; i < (int)nelem(vars); i++)
		if (vars[i].c == c)
			return i;
	fatal("trigo: no such global");
	return 0;
}

/* One of our globals set, and the assembly's to the same. */
static void
set(int32_t *c, int32_t x)
{
	*c = x;
	memcpy(av[var(c)], c, 4);
}

/* matrix k of ours (0) or the assembly's (1) */
static int32_t*
mat(int side, int k)
{
	int i;

	if (k < 3)
		i = var(k == 0 ? LMatriceRot : k == 1 ? LMatriceWorld : LMatriceTempo);
	else
		i = var(TabMat);
	if (side == 0)
		return (int32_t*)vars[i].c + (k < 3 ? 0 : 9*(k-3));
	return (int32_t*)av[i] + (k < 3 ? 0 : 9*(k-3));
}

static int16_t*
pts(int side, int k)
{
	if (side == 0)
		return cpts[k];
	return apts[k];
}

/* An angle: any 32 bits now and then, or whole turns, else within a turn or two either way. */
static int32_t
angle(void)
{
	if (rndn(8) == 0)
		return rnd();
	if (rndn(4) == 0)
		return 0;
	if (rndn(8) == 0)
		return 1024 * rndr(-64, 64);
	return rndr(-2048, 2048);
}

/* A coordinate of 32 bits, of 16, or within 8000 either way. */
static int32_t
coord(void)
{
	switch (rndn(4)) {
	case 0:
		return rnd();
	case 1:
		return (int16_t)rnd();
	}
	return rndr(-8000, 8000);
}

static int
in16(int32_t v)
{
	return v >= -32768 && v <= 32767;
}

/* Every global of ours as the assembly has it. */
static int
sameasm(char *what)
{
	uint8_t *c;
	int i, k;

	for (k = 0; k < (int)nelem(vars); k++) {
		c = vars[k].c;
		if (memcmp(c, av[k], vars[k].size) != 0) {
			for (i = 0; i < (int)vars[k].size && c[i] == av[k][i]; i++)
				;
			fail("%s: %s differs at byte %d: ours %#x, assembly %#x", what, vars[k].name, i,
				c[i], av[k][i]);
			return 0;
		}
	}
	if (memcmp(cpts, apts, sizeof cpts) != 0) {
		fail("%s: the points differ", what);
		return 0;
	}
	return 1;
}

/*
 * The matrices and points of ours and the assembly's the same: turns
 * the game makes (our RotMatW turning the identity), or anything.
 */
static void
randstate(void)
{
	int32_t m[Nmat][9];
	int k, i;

	for (k = 0; k < Nmat; k++) {
		if (rndn(4) == 0) {
			for (i = 0; i < 9; i++)
				m[k][i] = rndn(4) == 0 ? (int32_t)rnd() : rndr(-16384, 16384);
			continue;
		}
		for (i = 0; i < 9; i++)
			LMatriceWorld[i] = i % 4 == 0 ? 16384 : 0;
		lAlpha = angle();
		lBeta = angle();
		lGamma = angle();
		RotMatW();
		memcpy(m[k], LMatriceRot, sizeof m[k]);
	}
	for (k = 0; k < Nmat; k++) {
		memcpy(mat(0, k), m[k], sizeof m[k]);
		memcpy(mat(1, k), m[k], sizeof m[k]);
	}
	set(&lAlpha, lAlpha);
	set(&lBeta, lBeta);
	set(&lGamma, lGamma);
	for (k = 0; k < 2; k++)
		for (i = 0; i < 3*Npts; i++)
			cpts[k][i] = rnd();
	memcpy(apts, cpts, sizeof apts);
}

/* a quotient as 16-bit idiv makes it, or whether it would fault */
static int
fits16(int32_t n, int32_t d)
{
	int32_t q;

	if (d == 0)
		return 0;
	q = n / d;
	return in16(q);
}

/* Whether Proj_3D, at these 16 bits, would make its quotients without faulting. */
static int
proj3dok(int32_t x, int32_t y, int32_t z)
{
	int16_t d;

	d = (uint16_t)z + (uint16_t)KFactor;
	if (d < 0)
		d = 32767;
	return fits16((int32_t)(int16_t)x * (int16_t)LFactorX, d)
		&& fits16((int32_t)(int16_t)-(uint16_t)y * (int16_t)LFactorY, d);
}

/*
 * A point that LongProjettePoint puts at a bound of its clamp of Xp or
 * Yp, or one past it, where the factor of that axis allows: at the depth
 * of a multiple of the factor, a coordinate the quotient takes exactly.
 */
static void
clamped(int32_t *a)
{
	static int32_t bounds[] = {32767, 32768, -32768, -32769};
	int64_t lf, n, q, c;
	int32_t k, axis;

	axis = rnd() & 1;
	lf = axis ? LFactorY : LFactorX;
	if (lf == 0 || lf < -65535 || lf > 65535)
		return;
	k = rndr(1, 4);
	if (KFactor > 0)
		k += (KFactor + llabs(lf) - 1) / llabs(lf);
	n = llabs(lf) * k;
	q = bounds[rndn(nelem(bounds))] - (int64_t)(axis ? YCentre : XCentre);
	c = q * k * (lf < 0 ? -1 : 1);
	if (axis)
		c = -c;
	if (n - KFactor < 0 || c < INT32_MIN || c > INT32_MAX)
		return;
	a[2] = (uint32_t)CameraZr - (uint32_t)(n - KFactor);
	a[axis] = (uint32_t)(axis ? CameraYr : CameraXr) + (uint32_t)c;
}

void
testtrigo(void)
{
	uintptr_t s[nelem(names)];
	int32_t a[7], ra, rc, z, n;
	int16_t poly[9], xp, yp;
	Regs g;
	int r, op, i, k, d, t;
	char *what, args[128];

	for (k = 0; k < (int)nelem(vars); k++)
		av[k] = (uint8_t*)lookup(vars[k].name);
	for (i = 0; i < (int)nelem(names); i++)
		s[i] = lookup(names[i]);
	if (!sameasm("the start"))
		return;
	randstate();
	for (r = 0; r < 10*Rounds; r++) {
		if (r % 1000 == 0)
			randstate();
		op = rndn(nelem(names));
		what = names[op];
		ra = rc = 0;
		for (i = 0; i < 7; i++)
			a[i] = coord();
		memset(&g, 0, sizeof g);
		args[0] = '\0';
		switch (op) {
		case 0:
			a[2] = angle();
			abicall(s[op], 3, a);
			Rotate(a[0], a[1], a[2]);
			break;
		case 1:
			abicall(s[op], 3, a);
			RotatePoint(a[0], a[1], a[2]);
			break;
		case 2:
			abicall(s[op], 3, a);
			WorldRotatePoint(a[0], a[1], a[2]);
			break;
		case 3:
			abicall(s[op], 3, a);
			LongWorldRotatePoint(a[0], a[1], a[2]);
			break;
		case 4:
			abicall(s[op], 3, a);
			LongInverseRotatePoint(a[0], a[1], a[2]);
			break;
		case 5:
			for (i = 0; i < 3; i++)
				a[i] = angle();
			abicall(s[op], 3, a);
			RotateMatriceWorld(a[0], a[1], a[2]);
			break;
		case 6:
			k = rndn(Nmat);
			d = rndn(Nmat);
			a[0] = (int32_t)(uintptr_t)mat(1, k);
			a[1] = (int32_t)(uintptr_t)mat(1, d);
			abicall(s[op], 2, a);
			CopyMatrice(mat(0, k), mat(0, d));
			snprintf(args, sizeof args, "matrix %d to %d", k, d);
			break;
		case 7:
			for (i = 3; i < 6; i++)
				a[i] = angle();
			a[6] = rndr(-20000, 20000);
			abicall(s[op], 7, a);
			SetFollowCamera(a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
			break;
		case 8:
			abicall(s[op], 3, a);
			SetPosCamera(a[0], a[1], a[2]);
			break;
		case 9:
		case 10:
			for (i = 0; i < 3; i++)
				a[i] = angle();
			abicall(s[op], 3, a);
			if (op == 9)
				SetAngleCamera(a[0], a[1], a[2]);
			else
				SetInverseAngleCamera(a[0], a[1], a[2]);
			break;
		case 11:
			a[0] = rndr(0, 640);
			a[1] = rndr(0, 480);
			a[2] = rndn(4) == 0 ? rndr(-200, 2000) : rndr(1, 400);
			/* a factor in eight past 16 bits, of which Proj_3D takes the low word */
			a[3] = rndn(8) == 0 ? (int32_t)rnd() : rndr(-2000, 2000);
			a[4] = rndn(8) == 0 ? (int32_t)rnd() : rndr(-2000, 2000);
			abicall(s[op], 5, a);
			SetProjection(a[0], a[1], a[2], a[3], a[4]);
			break;
		case 12:
			a[0] = rndr(-1000, 1000);
			a[1] = rndr(-1000, 1000);
			abicall(s[op], 3, a);
			SetIsoProjection(a[0], a[1], a[2]);
			break;
		case 13:
			/* a depth in eight whose low word is 0 */
			if (rndn(8) == 0)
				a[2] = (uint32_t)CameraZr - 65536u*rndr(-2, 2);
			z = (uint32_t)CameraZr - (uint32_t)a[2];
			if (TypeProj != 1 && (int16_t)z >= 0 && !proj3dok((uint32_t)a[0] - (uint32_t)CameraXr,
			   (uint32_t)a[1] - (uint32_t)CameraYr, z))
				continue;
			ra = abicall(s[op], 3, a);
			rc = ProjettePoint(a[0], a[1], a[2]);
			break;
		case 14:
			for (i = 0; i < 3; i++)
				a[i] = rndn(2) ? rndr(-(1<<20), 1<<20) : rndr(-20000, 20000);
			if (rndn(16) == 0)
				a[2] = CameraZr;
			else if (rndn(16) == 0)
				clamped(a);
			z = (uint32_t)CameraZr - (uint32_t)a[2];
			n = (uint32_t)z + (uint32_t)KFactor;
			if (z >= 0 && n >= 0) {
				if (n == 0)
					continue;
				if ((int64_t)(int32_t)((uint32_t)a[0] - (uint32_t)CameraXr) * LFactorX / n
				   != (int32_t)((int64_t)(int32_t)((uint32_t)a[0] - (uint32_t)CameraXr) * LFactorX / n))
					continue;
				if ((int64_t)(int32_t)((uint32_t)a[1] - (uint32_t)CameraYr) * LFactorY / n
				   != (int32_t)((int64_t)(int32_t)((uint32_t)a[1] - (uint32_t)CameraYr) * LFactorY / n))
					continue;
			}
			ra = abicall(s[op], 3, a);
			rc = LongProjettePoint(a[0], a[1], a[2]);
			break;
		case 15:
			for (i = 0; i < 9; i++)
				poly[i] = rndn(4) == 0 ? (int16_t)rnd() : rndr(-1000, 1000);
			if (rndn(4) == 0) {
				/* a point on the line of the other two: no face */
				k = rndr(-3, 3);
				poly[7] = poly[1] + k*(poly[4] - poly[1]);
				poly[8] = poly[2] + k*(poly[5] - poly[2]);
			}
			a[0] = (int32_t)(uintptr_t)poly;
			ra = abicall(s[op], 1, a);
			rc = TestVuePoly(poly);
			snprintf(args, sizeof args, "%d %d, %d %d, %d %d", poly[1], poly[2], poly[4], poly[5], poly[7], poly[8]);
			break;
		case 16:
			for (i = 0; i < 3; i++)
				a[i] = (int16_t)angle();
			abicall(s[op], 3, a);
			SetLightVector(a[0], a[1], a[2]);
			break;
		case 17:
		case 18:
		case 19:
			g.eax = a[0];
			g.ebx = a[1];
			g.ecx = a[2];
			regcall(s[op], &g);
			if (op == 17)
				Rot(a[0], a[1], a[2]);
			else if (op == 18)
				WorldRot(a[0], a[1], a[2]);
			else
				LongWorldRot(a[0], a[1], a[2]);
			break;
		case 20:
			set(&lAlpha, angle());
			set(&lBeta, angle());
			set(&lGamma, angle());
			regcall(s[op], &g);
			RotMatW();
			break;
		case 21:
			set(&lAlpha, angle());
			set(&lBeta, angle());
			set(&lGamma, angle());
			k = rndn(Nmat);
			t = rndn(4) == 0 ? k : (int)rndn(Nmat);
			g.eax = (uint32_t)(uintptr_t)mat(1, k);
			g.ebp = (uint32_t)(uintptr_t)mat(1, t);
			regcall(s[op], &g);
			RotMatIndex2(mat(0, k), mat(0, t));
			snprintf(args, sizeof args, "matrix %d of %d turned by %d %d %d", k, t, lAlpha, lBeta, lGamma);
			break;
		case 22:
		case 23:
			if (op == 23) {
				set(&lAlpha, coord());
				set(&lBeta, coord());
				set(&lGamma, coord());
			}
			set(&X0, coord());
			set(&Y0, coord());
			set(&Z0, coord());
			k = rndn(Nmat);
			n = rndr(1, Npts);
			t = rnd() & 1;
			g.eax = (uint32_t)(uintptr_t)mat(1, k);
			g.esi = (uint32_t)(uintptr_t)pts(1, t);
			g.edi = (uint32_t)(uintptr_t)pts(1, !t);
			g.ecx = n | rnd() << 16;
			regcall(s[op], &g);
			if (op == 22)
				RotList(mat(0, k), pts(0, t), pts(0, !t), n);
			else
				TransRotList(mat(0, k), pts(0, t), pts(0, !t), n);
			snprintf(args, sizeof args, "%d points by matrix %d", n, k);
			break;
		case 24:
			if (!proj3dok(a[0], a[1], a[2]))
				continue;
			g.eax = a[0];
			g.ebx = a[1];
			g.ebp = a[2];
			regcall(s[op], &g);
			Proj_3D(a[0], a[1], a[2], &xp, &yp);
			if ((int16_t)g.eax != xp || (int16_t)g.ebx != yp) {
				fail("Proj_3D %d %d %d: ours %d %d, the assembly's %d %d", a[0], a[1], a[2],
					xp, yp, (int16_t)g.eax, (int16_t)g.ebx);
				goto out;
			}
			break;
		case 25:
			g.eax = a[0];
			g.ebx = a[1];
			g.ebp = a[2];
			regcall(s[op], &g);
			Proj_ISO(a[0], a[1], a[2], &xp, &yp);
			if ((int16_t)g.eax != xp || (int16_t)g.ebx != yp) {
				fail("Proj_ISO %d %d %d: ours %d %d, the assembly's %d %d", a[0], a[1], a[2],
					xp, yp, (int16_t)g.eax, (int16_t)g.ebx);
				goto out;
			}
			break;
		}
		if (op < 17 && !abiok(what))
			break;
		if (ra != rc) {
			if (args[0] == '\0')
				snprintf(args, sizeof args, "%d %d %d", a[0], a[1], a[2]);
			fail("%s %s: ours %d, the assembly's %d", what, args, rc, ra);
			break;
		}
		if (!sameasm(what))
			break;
	}
out:
	printf("trigo: %d rounds\n", r);
}

