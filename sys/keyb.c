#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "dat.h"
#include "fns.h"

/*
 * The keyboard, as lib386/lib_sys/keyboard.asm's interrupt handler kept
 * it: each byte the keyboard sends (XT set 1, extended keys after an
 * E0) updates Key (the scan code of the key down, low byte), Joy (the
 * arrows), Fire (space, return, ctrl, alt, del, shift) and FuncKey (F1
 * to F12). While AsciiMode is set the handler also passed the keys to
 * the BIOS, whose buffer GetAscii reads: here a queue of the BIOS's
 * words, scan code and character.
 */

enum
{
	Nbios = 15,	/* the keys the BIOS's buffer holds */
};

volatile uint16_t	Key;
volatile uint16_t	Joy;
volatile uint16_t	Fire;
volatile uint16_t	FuncKey;
uint16_t	AsciiMode;

/* keyboard.asm's TabSpecialKey and TabSpecialFunc */
static uint8_t special[] = {
	72, 80, 75, 77, 71, 73, 81, 79,		/* the arrows, keypad's corners */
	57, 28, 29, 56, 83, 42, 54,		/* space, return, ctrl, alt, del, shifts */
	59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 87, 88,	/* F1 to F12 */
	42, 0,
};
static uint8_t func[][2] = {
	{0, 1}, {0, 2}, {0, 4}, {0, 8}, {0, 1+4}, {0, 1+8}, {0, 2+8}, {0, 2+4},
	{1, 1}, {1, 2}, {1, 4}, {1, 8}, {1, 16}, {1, 32}, {1, 32},
	{2, 1}, {2, 2}, {2, 4}, {2, 8}, {2, 16}, {2, 32}, {2, 64}, {2, 128},
	{3, 1}, {3, 2}, {3, 4}, {3, 8},
	{255, 0}, {255, 0},
};

static int extended;
static uint16_t bios[Nbios];
static int nbios;

static void
setbyte(volatile uint16_t *w, int hi, int bits, int on)
{
	int shift;

	shift = hi ? 8 : 0;
	if (on)
		*w |= bits << shift;
	else
		*w &= ~(bits<<shift);
}

/* xtcode is keyboard.asm's NewIntPM09, for one byte from the keyboard. */
void
xtcode(int c)
{
	int s, i, up;

	if (c == 0xE0) {
		extended = 1;
		return;
	}
	s = c & 0x7F;
	up = c & 0x80;
	if (s == 0x2A && extended) {	/* the shift an extended key fakes */
		extended = 0;
		return;
	}
	for (i = 0; i < (int)nelem(special); i++)
		if (special[i] == s)
			break;
	if (i < (int)nelem(special))
		switch (func[i][0]) {
		case 0:
			setbyte(&Joy, 0, func[i][1], !up);
			extended = 0;
			return;
		case 1:
			/*
			 * FIXED: LBA.EXE, with Ctrl, Alt and Del down together,
			 * writes the BIOS's warm boot flag and returns far to
			 * FFFFh:0, the BIOS's reset in real mode. In protected
			 * mode that far return faults. The three keys set their
			 * bits in Fire alone. Taken from LbaWin 0.8.1.
			 */
			setbyte(&Fire, 0, func[i][1], !up);
			extended = 0;
			return;
		case 2:
			setbyte(&FuncKey, 0, func[i][1], !up);
			break;
		case 3:
			setbyte(&FuncKey, 1, func[i][1], !up);
			break;
		default:
			extended = 0;
			return;
		}
	if (up)
		Key &= 0xFF00;
	else
		Key = (Key & 0xFF00) | s;
	extended = 0;
}

/* A word for the BIOS's buffer, scan code and character, while AsciiMode asks for them, its 15 at most. */
void
biosword(int w)
{
	if (AsciiMode == 0 || nbios == Nbios)
		return;
	bios[nbios++] = (uint16_t)w;
}

/* GetAscii is keyboard.asm's GetAscii: the BIOS's next word, 0 for none. */
uint16_t
GetAscii(void)
{
	uint16_t w;

	pump();
	if (nbios == 0)
		return 0;
	w = bios[0];
	memmove(bios, bios + 1, (nbios - 1) * sizeof bios[0]);
	nbios--;
	return w;
}

/* ClearAsciiBuffer is keyboard.asm's ClearAsciiBuffer: the BIOS's buffer emptied. */
void
ClearAsciiBuffer(void)
{
	pump();
	nbios = 0;
}
