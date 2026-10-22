#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../sys/compat.h"
#include "c_extern.h"

/*
 * The brick grid's routines of grille_a.asm. A scene (BufCube) is 64
 * columns along x by 64 along z, each 25 cells high, a cell two bytes: a
 * block of TabBlock (1 up, 0 for none) and a brick of the block. A world
 * coordinate is 512 a cell along x and z, from half a cell before, and
 * 256 along y. What the assembly returned in AL it returned in EAX: where
 * EAX still held a coordinate of the map, its upper bytes came back with
 * AL, and the callers that take a LONG see them, as they do here from
 * WorldCodeBrick.
 */

enum
{
	Header = 3,	/* HEADER_BLOCK */
};

/*
 * grille.c's, declared here and not in c_extern.h: extra.c declares YMap
 * a WORD, and reads its low half.
 */
extern LONG	XMap, YMap, ZMap;

uint8_t	CodeJeu;

/* The block n's start in TabBlock, as the table of offsets at its head gives it. */
static uint8_t*
block(uint32_t n)
{
	uint32_t off;

	memcpy(&off, TabBlock + 4*n, 4);
	return TabBlock + off;
}

/* GetAdrBlock is grille_a.asm's GetAdrBlock. */
UBYTE*
GetAdrBlock(int32_t n)
{
	return block(n);
}

/* The byte of a cell's brick: its block's, or the cell's second if no block. */
static uint8_t
brick(uint32_t off)
{
	if (BufCube[off] == 0)
		return BufCube[off+1];
	return block(BufCube[off]-1)[Header + 4*BufCube[off+1]];
}

/*
 * WorldColBrickFull is grille_a.asm's WorldColBrickFull: the brick at
 * xw,yw,zw, or 1 if a cell above, up to ymax over it and to the top, is
 * not empty. Outside the map along x or z 0, below it 1. Above the top,
 * no cell is tested past the cell's own.
 */
int32_t
WorldColBrickFull(int32_t xw, int32_t yw, int32_t zw, int32_t ymax)
{
	uint32_t off, n, y;
	uint8_t v;

	XMap = ((uint32_t)xw + 256) >> 9;
	YMap = yw >> 8;
	ZMap = ((uint32_t)zw + 256) >> 9;
	/*
	 * FIXED: LBA.EXE sets AL alone on these three ways out, so that a
	 * caller taking a LONG reads a coordinate's upper bytes with it,
	 * 0x7FFF00 past the map's left edge. These give the byte alone.
	 * Taken from DotEmu's Android 1.06, which tests below the map first.
	 */
	if (XMap >= 64)
		return 0;
	off = 50 * XMap;
	if (YMap <= -1)
		return 1;
	off += 2 * YMap;
	if (ZMap >= 64)
		return 0;
	off += 3200 * ZMap;
	v = brick(off);
	/*
	 * FIXED: LBA.EXE counts the cells above unsigned, so that a height
	 * of 0 or less tests every cell up to the top. Such a height
	 * returns at once. Taken from DotEmu's Android 1.06.
	 */
	if (ymax <= 0)
		return v;
	n = ((uint32_t)ymax + 255) >> 8;
	for (y = YMap; y < 24; ) {
		off += 2;
		y++;
		/*
		 * FIXED: LBA.EXE returns here without the pop ebx it owes, so
		 * that EBX comes back as the loop's counter. C keeps the
		 * caller's registers.
		 */
		if (BufCube[off] != 0 || BufCube[off+1] != 0)
			return 1;
		if (--n == 0)
			break;
	}
	return v;
}

/* WorldColBrick is grille_a.asm's WorldColBrick: the brick at xw,yw,zw, or 0 outside the map and 1 below it. */
UBYTE
WorldColBrick(int16_t xw, int16_t yw, int16_t zw)
{
	uint32_t off;

	XMap = ((uint32_t)xw + 256) >> 9;
	YMap = yw >> 8;
	ZMap = ((uint32_t)zw + 256) >> 9;
	if (XMap >= 64)
		return 0;
	off = 50 * XMap;
	if (YMap <= -1)
		return 1;
	if (YMap > 24)
		return 0;
	off += 2 * YMap;
	if (ZMap >= 64)
		return 0;
	off += 3200 * ZMap;
	return brick(off);
}

/*
 * WorldCodeBrick is grille_a.asm's WorldCodeBrick: the code of the block
 * at xw,yw,zw, its fifth byte, or F0 if none. Below the map yw comes
 * back with a low byte of F0. The map's bounds are not tested.
 */
int32_t
WorldCodeBrick(int32_t xw, int32_t yw, int32_t zw)
{
	uint32_t off;

	XMap = ((uint32_t)xw + 256) >> 9;
	off = 50 * XMap;
	if (yw <= -1)
		return (yw & ~0xFF) | 0xF0;
	YMap = (uint32_t)yw >> 8;
	off += 2 * YMap;
	ZMap = ((uint32_t)zw + 256) >> 9;
	off += 3200 * ZMap;
	if (BufCube[off] == 0)
		return 0xF0;
	return block(BufCube[off]-1)[Header+1];
}

/*
 * Map2Screen is grille_a.asm's Map2Screen: the screen's XScreen,YScreen of
 * cell xm,ym,zm.
 *
 * FEATURE: the cell's place about the middle of a screen of any size, as
 * the projection's centre is, on columns of 24 pixels from the screen's
 * left edge. The port's own.
 */
void
Map2Screen(int32_t xm, int32_t ym, int32_t zm)
{
	int32_t d, s;

	d = xm - zm;
	XScreen = 16*d + 8*d + CENTRE_ECRAN_X-8-23-1;
	s = xm + zm;
	YScreen = 8*s + 4*s - (16*ym - ym) + CENTRE_ECRAN_Y-25;
}

/*
 * A column of a scene's grid, packed: a count of runs, each a byte. 00
 * and n give n+1 empty cells, 01 and n the n+1 cells that follow, and 10
 * and n a cell, n+1 times. The counts are bytes, 0 meaning 256.
 */

/* DecompColonne is grille_a.asm's DecompColonne: the column at src unpacked to dst. */
void
DecompColonne(void *src, void *dst)
{
	uint8_t *s, *d, op, n, nb;
	int i;

	s = src;
	d = dst;
	nb = *s++;
	do {
		op = *s++;
		n = (op & 0x3F) + 1;
		if ((op & 0xC0) == 0)
			memset(d, 0, 2*n);
		else if (op & 0x40) {
			memcpy(d, s, 2*n);
			s += 2 * n;
		} else {
			for (i = 0; i < n; i++)
				memcpy(d + 2*i, s, 2);
			s += 2;
		}
		d += 2 * n;
	} while (--nb != 0);
}

/* MixteColonne is grille_a.asm's MixteColonne: DecompColonne leaving the empty cells as they are. */
void
MixteColonne(void *src, void *dst)
{
	uint8_t *s, *d, op, n, nb;
	int i;

	s = src;
	d = dst;
	nb = *s++;
	do {
		op = *s++;
		n = (op & 0x3F) + 1;
		if (op & 0xC0) {
			if (op & 0x40) {
				memcpy(d, s, 2*n);
				s += 2 * n;
			} else {
				for (i = 0; i < n; i++)
					memcpy(d + 2*i, s, 2);
				s += 2;
			}
		}
		d += 2 * n;
	} while (--nb != 0);
}
