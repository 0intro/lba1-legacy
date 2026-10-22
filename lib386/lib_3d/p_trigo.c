#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../lib_sys/adeline.h"
#include "lib_3d.h"

/*
 * The rotations and projections of p_trigo.asm. A matrix is 9 LONGs,
 * row by row, 1.0 being 1<<14. Angles turn in 1024 steps, sines are
 * 14-bit (P_SinTab). LMatriceWorld turns the world into the camera's
 * view, LMatriceRot an object's group, TabMat holds a matrix for each
 * group of a body. X0, Y0 and Z0 receive what is turned. The assembly's
 * register entries are functions here: Rot,
 * WorldRot, LongWorldRot, RotMatW, RotMatIndex2, RotList, TransRotList,
 * Proj_3D and Proj_ISO.
 */

enum
{
	TypeIso = 1,	/* TYPE_ISO */
};

WORD	compteur;
WORD	TypeProj;
LONG	XCentre = 320, YCentre = 200;
WORD	Xp, Yp;
WORD	IsoScale = 500;
WORD	Z_Min, Z_Max;
LONG	KFactor = 128, LFactorX = 1024, LFactorY = 840;
LONG	CameraX, CameraY, CameraZ;
LONG	CameraXr, CameraYr, CameraZr;
LONG	AlphaLight, BetaLight, GammaLight;
LONG	Alpha, Beta, Gamma;
LONG	lAlpha, lBeta, lGamma;
LONG	NormalXLight = 61, NormalYLight, NormalZLight;
LONG	X0, Y0, Z0;
LONG	LMatriceRot[9], LMatriceWorld[9], LMatriceTempo[9];
LONG	TabMat[9*30];

static LONG LMatriceDummy[9];

static int32_t
sinus(int32_t a)
{
	return P_SinTab[a & 1023];
}

static int32_t
cosinus(int32_t a)
{
	return P_SinTab[((uint32_t)a + 256) & 1023];
}

/* a*b >> 14, as imul and sar made it */
static int32_t
mul14(int32_t a, int32_t b)
{
	return (int32_t)((uint32_t)a * (uint32_t)b) >> 14;
}

/* (a*b + c*d) >> 14 */
static int32_t
add14(int32_t a, int32_t b, int32_t c, int32_t d)
{
	return (int32_t)((uint32_t)a*(uint32_t)b + (uint32_t)c*(uint32_t)d) >> 14;
}

/* (a*b - c*d) >> 14 */
static int32_t
sub14(int32_t a, int32_t b, int32_t c, int32_t d)
{
	return (int32_t)((uint32_t)a*(uint32_t)b - (uint32_t)c*(uint32_t)d) >> 14;
}

/* a row of m times x, y, z, >> 14: in 32 bits */
static int32_t
dot(LONG *m, int32_t x, int32_t y, int32_t z)
{
	uint32_t s;

	s = (uint32_t)m[0]*x + (uint32_t)m[1]*y + (uint32_t)m[2]*z;
	return (int32_t)s >> 14;
}

/* the same of a row, or a column at stride 3, in 64 bits, of which shrd keeps the 32 above the 14th */
static int32_t
ldot(LONG *m, int stride, int32_t x, int32_t y, int32_t z)
{
	uint64_t s;

	s = (uint64_t)((int64_t)m[0]*x) + (uint64_t)((int64_t)m[stride]*y) + (uint64_t)((int64_t)m[2*stride]*z);
	return (uint32_t)(s >> 14);
}

/* Rot is p_trigo.asm's Rot: x, y, z, of 16 bits, turned by LMatriceRot into X0, Y0, Z0. */
void
Rot(int32_t x, int32_t y, int32_t z)
{
	x = (int16_t)x;
	y = (int16_t)y;
	z = (int16_t)z;
	X0 = dot(LMatriceRot, x, y, z);
	Y0 = dot(LMatriceRot + 3, x, y, z);
	Z0 = dot(LMatriceRot + 6, x, y, z);
}

/* RotatePoint is p_trigo.asm's RotatePoint: Rot. */
void
RotatePoint(LONG x, LONG y, LONG z)
{
	Rot(x, y, z);
}

/* WorldRot is p_trigo.asm's WorldRot: x, y, z, of 16 bits, turned by LMatriceWorld. */
void
WorldRot(int32_t x, int32_t y, int32_t z)
{
	x = (int16_t)x;
	y = (int16_t)y;
	z = (int16_t)z;
	X0 = dot(LMatriceWorld, x, y, z);
	Y0 = dot(LMatriceWorld + 3, x, y, z);
	Z0 = dot(LMatriceWorld + 6, x, y, z);
}

/* WorldRotatePoint is p_trigo.asm's WorldRotatePoint: WorldRot. */
void
WorldRotatePoint(LONG x, LONG y, LONG z)
{
	WorldRot(x, y, z);
}

/* LongWorldRot is p_trigo.asm's LongWorldRot: x, y, z turned by LMatriceWorld, in 64 bits. */
void
LongWorldRot(int32_t x, int32_t y, int32_t z)
{
	X0 = ldot(LMatriceWorld, 1, x, y, z);
	Y0 = ldot(LMatriceWorld + 3, 1, x, y, z);
	Z0 = ldot(LMatriceWorld + 6, 1, x, y, z);
}

/* LongWorldRotatePoint is p_trigo.asm's LongWorldRotatePoint: LongWorldRot. */
void
LongWorldRotatePoint(LONG x, LONG y, LONG z)
{
	LongWorldRot(x, y, z);
}

/* LongInverseRot: x, y, z turned back by LMatriceWorld, its transpose. */
static void
LongInverseRot(int32_t x, int32_t y, int32_t z)
{
	X0 = ldot(LMatriceWorld, 3, x, y, z);
	Y0 = ldot(LMatriceWorld + 1, 3, x, y, z);
	Z0 = ldot(LMatriceWorld + 2, 3, x, y, z);
}

/* LongInverseRotatePoint is p_trigo.asm's LongInverseRotatePoint: LongInverseRot. */
void
LongInverseRotatePoint(LONG x, LONG y, LONG z)
{
	LongInverseRot(x, y, z);
}

/*
 * RotMatIndex2 is p_trigo.asm's RotMatIndex2: the matrix src turned by
 * lAlpha about x, then lGamma about z, then lBeta about y, into dst,
 * through LMatriceDummy. An angle of 0 is not turned by. Each word of
 * dst is reckoned from src as the assembly read it, after the words of
 * dst written before it: where dst is src, the turn by lAlpha reads
 * what it wrote.
 */
void
RotMatIndex2(LONG *dst, LONG *src)
{
	LONG *s, *t;
	int32_t sa, ca;
	int i;

	s = src;
	if (lAlpha != 0) {
		sa = sinus(lAlpha);
		ca = cosinus(lAlpha);
		dst[0] = s[0];
		dst[3] = s[3];
		dst[6] = s[6];
		for (i = 0; i < 9; i += 3) {
			dst[i+1] = add14(s[i+1], ca, s[i+2], sa);
			dst[i+2] = sub14(s[i+2], ca, s[i+1], sa);
		}
		s = dst;
	}
	if (lGamma != 0) {
		sa = sinus(lGamma);
		ca = cosinus(lGamma);
		t = LMatriceDummy;
		t[2] = s[2];
		t[5] = s[5];
		t[8] = s[8];
		for (i = 0; i < 9; i += 3) {
			t[i] = add14(s[i], ca, s[i+1], sa);
			t[i+1] = sub14(s[i+1], ca, s[i], sa);
		}
		s = t;
	}
	if (lBeta == 0) {
		if (s != dst)
			for (i = 0; i < 9; i++)
				dst[i] = s[i];
		return;
	}
	if (s == dst) {
		for (i = 0; i < 9; i++)
			LMatriceDummy[i] = dst[i];
		s = LMatriceDummy;
	}
	sa = sinus(lBeta);
	ca = cosinus(lBeta);
	dst[1] = s[1];
	dst[4] = s[4];
	dst[7] = s[7];
	for (i = 0; i < 9; i += 3) {
		dst[i] = sub14(s[i], ca, s[i+2], sa);
		dst[i+2] = add14(s[i+2], ca, s[i], sa);
	}
}

/* RotMatW is p_trigo.asm's RotMatW: LMatriceWorld turned by lAlpha, lBeta, lGamma into LMatriceRot. */
void
RotMatW(void)
{
	RotMatIndex2(LMatriceRot, LMatriceWorld);
}

/* RotateMatriceWorld is p_trigo.asm's RotateMatriceWorld: the same by palpha, pbeta, pgamma. */
void
RotateMatriceWorld(LONG palpha, LONG pbeta, LONG pgamma)
{
	lAlpha = palpha;
	lBeta = pbeta;
	lGamma = pgamma;
	RotMatW();
}

/* SetLightVector is p_trigo.asm's SetLightVector: the light's angles, and its normal of 59 turned by them. */
void
SetLightVector(WORD alpha, WORD beta, WORD gamma)
{
	AlphaLight = lAlpha = alpha;
	BetaLight = lBeta = beta;
	GammaLight = lGamma = gamma;
	RotMatW();
	Rot(0, 0, NORMAL_UNIT - 5);
	NormalXLight = X0;
	NormalYLight = Y0;
	NormalZLight = Z0;
}

/* a word at any address, as a body holds its points */
static int16_t
get16(uint8_t *p)
{
	int16_t v;

	memcpy(&v, p, 2);
	return v;
}

/*
 * The n points of 3 words at src (a body's, at any address) turned by
 * mat into dst, X0, Y0 and Z0 added in 16 bits, each first moved by
 * lAlpha, lBeta and lGamma if trans.
 */
static void
rotlist(LONG *mat, uint8_t *src, WORD *dst, uint16_t n, int trans)
{
	int32_t x, y, z;

	compteur = n;
	/*
	 * FIXED: LBA.EXE turns 65536 points for a count of 0, past the 500
	 * of List_Anim_Point. A count of 0 returns at once. Taken from
	 * DotEmu's Android 1.06.
	 */
	if (n == 0)
		return;
	do {
		x = get16(src);
		y = get16(src + 2);
		z = get16(src + 4);
		if (trans) {
			x += (uint32_t)lAlpha;
			y += (uint32_t)lBeta;
			z += (uint32_t)lGamma;
		}
		dst[0] = (uint16_t)dot(mat, x, y, z) + (uint16_t)X0;
		dst[1] = (uint16_t)dot(mat + 3, x, y, z) + (uint16_t)Y0;
		dst[2] = (uint16_t)dot(mat + 6, x, y, z) + (uint16_t)Z0;
		src += 6;
		dst += 3;
	} while (--compteur != 0);
}

/* RotList is p_trigo.asm's RotList: n points turned by mat and moved to X0, Y0, Z0. */
void
RotList(LONG *mat, void *src, WORD *dst, int32_t n)
{
	rotlist(mat, src, dst, n, 0);
}

/* TransRotList is p_trigo.asm's TransRotList: the same, each first moved by lAlpha, lBeta, lGamma. */
void
TransRotList(LONG *mat, void *src, WORD *dst, int32_t n)
{
	rotlist(mat, src, dst, n, 1);
}

/* CopyMatrice is p_trigo.asm's CopyMatrice. */
void
CopyMatrice(LONG *MatSrc, LONG *MatDest)
{
	int i;

	for (i = 0; i < 9; i++)
		MatDest[i] = MatSrc[i];
}

/* FlipMatrice: src's transpose into dst, a row read for each column written. */
static void
FlipMatrice(LONG *src, LONG *dst)
{
	LONG a, b, c;
	int i;

	for (i = 0; i < 3; i++) {
		a = src[3*i];
		b = src[3*i+1];
		c = src[3*i+2];
		dst[i] = a;
		dst[i+3] = b;
		dst[i+6] = c;
	}
}

/* SetPosCamera is p_trigo.asm's SetPosCamera. */
void
SetPosCamera(LONG poswx, LONG poswy, LONG poswz)
{
	CameraX = poswx;
	CameraY = poswy;
	CameraZ = poswz;
}

/*
 * SetAngleCamera is p_trigo.asm's SetAngleCamera: LMatriceWorld made
 * of the camera's angles, turning by gamma, then alpha, then beta, and
 * the camera turned by it into CameraXr, CameraYr and CameraZr.
 */
void
SetAngleCamera(LONG palpha, LONG pbeta, LONG pgamma)
{
	LONG *w;
	int32_t sa, ca, sg, cg, sb, cb, t;

	w = LMatriceWorld;
	Alpha = palpha & 1023;
	sa = sinus(Alpha);
	ca = cosinus(Alpha);
	Gamma = pgamma & 1023;
	sg = sinus(Gamma);
	cg = cosinus(Gamma);
	w[0] = cg;
	w[1] = -sg;
	w[3] = mul14(sg, ca);
	w[4] = mul14(cg, ca);
	w[6] = mul14(sg, sa);
	w[7] = mul14(cg, sa);
	Beta = pbeta & 1023;
	sb = sinus(Beta);
	cb = cosinus(Beta);
	t = w[0];
	w[0] = mul14(cb, t);
	w[2] = mul14(sb, t);
	t = w[3];
	w[3] = add14(cb, t, sb, sa);
	w[5] = sub14(sb, t, cb, sa);
	t = w[6];
	w[6] = sub14(cb, t, ca, sb);
	w[8] = add14(sb, t, ca, cb);
	LongWorldRot(CameraX, CameraY, CameraZ);
	CameraXr = X0;
	CameraYr = Y0;
	CameraZr = Z0;
}

/* SetInverseAngleCamera is p_trigo.asm's SetInverseAngleCamera: the same, LMatriceWorld then turned about. */
void
SetInverseAngleCamera(LONG palpha, LONG pbeta, LONG pgamma)
{
	SetAngleCamera(palpha, pbeta, pgamma);
	FlipMatrice(LMatriceWorld, LMatriceDummy);
	CopyMatrice(LMatriceDummy, LMatriceWorld);
	LongWorldRot(CameraX, CameraY, CameraZ);
	CameraXr = X0;
	CameraYr = Y0;
	CameraZr = Z0;
}

/*
 * Rotate is p_trigo.asm's Rotate and RotXY: coorx, coory turned by angle
 * in 2D into X0, Y0, of 16 bits. An angle whose low 16 bits are 0 leaves
 * them whole.
 */
void
Rotate(LONG coorx, LONG coory, LONG angle)
{
	int32_t x, y, s, c;

	if ((uint16_t)angle == 0) {
		X0 = coorx;
		Y0 = coory;
		return;
	}
	x = (int16_t)coorx;
	y = (int16_t)coory;
	s = sinus(angle);
	c = cosinus(angle);
	X0 = add14(x, c, y, s);
	Y0 = sub14(c, y, x, s);
}

/*
 * SetFollowCamera is p_trigo.asm's SetFollowCamera: the camera at
 * camzoom from the target, looking at it at its angles.
 */
void
SetFollowCamera(LONG targetx, LONG targety, LONG targetz, LONG camalpha, LONG cambeta, LONG camgamma, LONG camzoom)
{
	CameraX = targetx;
	CameraY = targety;
	CameraZ = targetz;
	/*
	 * FIXED: LBA.EXE takes back this call's arguments with a 16-bit add to
	 * SP, which leaves ESP 64 KB short near a 64 KB boundary. C takes them
	 * back whole.
	 */
	SetAngleCamera(camalpha, cambeta, camgamma);
	CameraZr += (uint32_t)camzoom;
	LongInverseRot(CameraXr, CameraYr, CameraZr);
	CameraX = X0;
	CameraY = Y0;
	CameraZ = Z0;
}

/*
 * Proj_3D is p_trigo.asm's Proj_3D: x, y at depth z in perspective, all
 * in 16 bits, into xp, yp. A depth past 32767 is 32767.
 */
void
Proj_3D(int32_t x, int32_t y, int32_t z, WORD *xp, WORD *yp)
{
	int16_t d, ny;

	d = (uint16_t)z + (uint16_t)KFactor;
	if (d < 0)
		d = 32767;
	ny = -(uint16_t)y;
	/*
	 * FIXED: LBA.EXE's 16-bit idiv faults on a quotient past 16 bits.
	 * These divide in 32 bits and keep the low 16.
	 */
	*xp = (uint16_t)((int32_t)(int16_t)x * (int16_t)LFactorX / d) + (uint16_t)XCentre;
	*yp = (uint16_t)((int32_t)ny * (int16_t)LFactorY / d) + (uint16_t)YCentre;
}

/* Proj_ISO is p_trigo.asm's Proj_ISO: x, y, z in isometry into xp, yp, of 16 bits. */
void
Proj_ISO(int32_t x, int32_t y, int32_t z, WORD *xp, WORD *yp)
{
	uint32_t a, b;

	a = ((uint32_t)x - (uint32_t)z) * 24;
	b = (uint32_t)y*30 - ((uint32_t)z + (uint32_t)x)*12;
	*xp = (uint16_t)((int32_t)a >> 9) + (uint16_t)XCentre;
	*yp = (uint16_t)-(uint16_t)((int32_t)b >> 9) + (uint16_t)YCentre;
}

/*
 * ProjettePoint is p_trigo.asm's ProjettePoint: a point of the world
 * into Xp, Yp, seen from the camera, or in isometry. 0 (and Xp, Yp 0)
 * if it lies behind the camera, its depth's low 16 bits negative, -1
 * otherwise.
 */
LONG
ProjettePoint(LONG CoorX, LONG CoorY, LONG CoorZ)
{
	int32_t z;

	if (TypeProj == TypeIso) {
		Proj_ISO(CoorX, CoorY, CoorZ, &Xp, &Yp);
		return -1;
	}
	z = (uint32_t)CameraZr - (uint32_t)CoorZ;
	if ((int16_t)z < 0) {
		Xp = Yp = 0;
		return 0;
	}
	Proj_3D((uint32_t)CoorX - (uint32_t)CameraXr, (uint32_t)CoorY - (uint32_t)CameraYr, z, &Xp, &Yp);
	return -1;
}

/* a LONG held to a WORD, as the assembly's adc of the sign made it */
static int16_t
clamp16(int32_t v)
{
	if (v > 32767)
		return 32767;
	if (v < -32768)
		return -32768;
	return v;
}

/*
 * LongProjettePoint is p_trigo.asm's LongProjettePoint: ProjettePoint in
 * perspective in 32 bits, Xp and Yp held to 16, behind the camera when
 * its depth, or the depth plus KFactor, is negative.
 */
LONG
LongProjettePoint(LONG CoorX, LONG CoorY, LONG CoorZ)
{
	int32_t x, y, z;

	x = (uint32_t)CoorX - (uint32_t)CameraXr;
	y = (uint32_t)CoorY - (uint32_t)CameraYr;
	z = (uint32_t)CameraZr - (uint32_t)CoorZ;
	if (z < 0 || (z = (uint32_t)z + (uint32_t)KFactor) < 0) {
		Xp = Yp = 0;
		return 0;
	}
	/*
	 * FIXED: LBA.EXE's idiv faults on a quotient past 32 bits. These
	 * divide in 64 bits and keep the low 32.
	 */
	Xp = clamp16((uint32_t)((int64_t)x * LFactorX / z) + (uint32_t)XCentre);
	y = 0u - (uint32_t)y;
	Yp = clamp16((uint32_t)((int64_t)y * LFactorY / z) + (uint32_t)YCentre);
	return -1;
}

/* SetProjection is p_trigo.asm's SetProjection: in perspective, about xc, yc. */
void
SetProjection(LONG xc, LONG yc, LONG kfact, LONG lfactx, LONG lfacty)
{
	XCentre = xc;
	YCentre = yc;
	KFactor = kfact;
	LFactorX = lfactx;
	LFactorY = lfacty;
	TypeProj = 0;
}

/* SetIsoProjection is p_trigo.asm's SetIsoProjection: in isometry, about xc, yc. */
void
SetIsoProjection(LONG xc, LONG yc, LONG scale)
{
	XCentre = xc;
	YCentre = yc;
	IsoScale = scale;
	TypeProj = TypeIso;
}

/*
 * TestVuePoly is p_trigo.asm's TestVuePoly: whether the polygon's first
 * three points turn the way of a face seen, by the cross product of two
 * edges, in 16 bits.
 */
LONG
TestVuePoly(void *ptrpoly)
{
	WORD *p;
	int32_t a, b;

	p = ptrpoly;
	b = (int16_t)(p[4] - p[1]) * (int16_t)(p[2] - p[8]);
	a = (int16_t)(p[5] - p[2]) * (int16_t)(p[1] - p[7]);
	return a < b;
}
