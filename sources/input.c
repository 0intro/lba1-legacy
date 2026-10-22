#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../sys/compat.h"
#include "c_extern.h"

/*
 * FEATURE: the Classic 3.2.3's inputs, behind GamePad, LBA2's
 * SOURCES/INPUT.CPP: 27 inputs, each
 * bound to two virtual keys and to two of the pad's 31 inputs, read
 * from lba.cfg's Input%d_1 and Input%d_2 for the keys and GInput%d_1
 * and GInput%d_2 for the pad. Taken from the Classic 3.2.3.
 */

enum
{
	Ninput = 27,	/* the inputs, two bindings each */
	Npad = 31,	/* the pad's inputs, TabPadKey's */
	Pad = 0x200,	/* a pad's input, as TabGInput holds it */
};

/*
 * DefTabInput is TLBA1C.exe 0x8198f0: each input's two virtual keys,
 * the arrows and the keypad's, Space and keypad 5, Return and the
 * keypad's, the Ctrl, Alt and Shift keys, W and End, F10, H and Tab, P,
 * F1 to F6, 1, 2, M, J, G, C, 9 and keypad 7, 0 and keypad 9
 */
static uint32_t	DefTabInput[2*Ninput] = {
	0x26, 0x68, 0x28, 0x62, 0x25, 0x64, 0x27, 0x66,
	0x20, 0x65, 0x0D, 0x6C, 0xA2, 0xA3, 0xA4, 0xA5,
	0xA0, 0xA1, 'W', 0x23, 0x79, 0, 'H', 0x09,
	'P', 0, 0x70, 0, 0x71, 0, 0x72, 0,
	0x73, 0, 0x74, 0, 0x75, 0, '1', 0,
	'2', 0, 'M', 0, 'J', 0, 'G', 0,
	'C', 0, '9', 0x67, '0', 0x69,
};

/*
 * DefTabGInput is TLBA1C.exe 0x819810: each input's two of the pad's
 * inputs, Pad and the input's number: the triggers walk, the sticks
 * turn, the buttons act, the cross takes M, J, G and C, and the right
 * stick's two halves up and down the last two
 */
static uint32_t	DefTabGInput[2*Ninput] = {
	Pad|30, 0, Pad|29, 0, Pad|21, Pad|25, Pad|22, Pad|26,
	Pad|2, 0, Pad|1, 0, Pad|9, 0, Pad|10, 0,
	Pad|3, 0, Pad|0, 0, Pad|6, 0, Pad|4, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, Pad|12, 0, Pad|11, 0, Pad|13, 0,
	Pad|14, 0, Pad|27, 0, Pad|28, 0,
};

/* TabInput is TLBA1C.exe 0x8db5d0: the keys bound, two an input */
uint32_t	TabInput[2*Ninput];

/* TabGInput is TLBA1C.exe 0x8db6b0: the pad's inputs bound, two an input */
uint32_t	TabGInput[2*Ninput];

/* TabInputBit is TLBA1C.exe 0x8db410: the bit of each binding's input */
uint32_t	TabInputBit[2*Ninput];

/* NbInput is TLBA1C.exe 0x819804: the inputs, and the bindings from SetInputBits on */
int32_t	NbInput = Ninput;

/* MemoTabInput is TLBA1C.exe 0x8db4f0: a controls page's first bindings, or those its Default replaced, which Escape and Restore give back */
uint32_t	MemoTabInput[2*Ninput];

/*
 * PadKeyToGInput is TLBA1C.exe 0x41a050: the pad's inputs bound to each
 * input again, those whose key in TabPadKey is one of the input's keys,
 * the first found and the last.
 */
void
PadKeyToGInput(void)
{
	uint32_t k, u;
	int i;

	for (i = 0; i < Ninput; i++) {
		TabGInput[2*i] = 0;
		TabGInput[2*i + 1] = 0;
		for (u = 0; u < Npad; u++) {
			k = TabPadKey[u];
			if (k == 0 || (k != TabInput[2*i] && k != TabInput[2*i + 1]))
				continue;
			if (TabGInput[2*i] == 0)
				TabGInput[2*i] = u | Pad;
			else
				TabGInput[2*i + 1] = u | Pad;
		}
	}
}

/*
 * GInputToPadKey is TLBA1C.exe 0x41a0c0: the key each of the pad's
 * inputs presses in play, the first key of the first input it is bound
 * to, or that input's second where the first is 0.
 */
void
GInputToPadKey(void)
{
	uint32_t u;
	int i;

	for (u = 0; u < Npad; u++) {
		TabPadKey[u] = 0;
		for (i = 0; i < Ninput; i++)
			if (TabGInput[2*i] == (u|Pad) || TabGInput[2*i + 1] == (u|Pad)) {
				TabPadKey[u] = TabInput[2*i] != 0 ? TabInput[2*i] : TabInput[2*i + 1];
				break;
			}
	}
}

/* DefaultGInput is TLBA1C.exe 0x41a120: the pad's bindings their defaults. */
void
DefaultGInput(void)
{
	memcpy(TabGInput, DefTabGInput, sizeof TabGInput);
}

/*
 * SetInputBits is TLBA1C.exe 0x41a140, LBA2's InitInput: each input's
 * bit for its two bindings, the pad's inputs bound again from
 * TabPadKey, and the bindings given to ReadInputs.
 */
void
SetInputBits(void)
{
	uint32_t mask;

	for (NbInput = 0; NbInput < 2*Ninput; ) {
		mask = 1u << (NbInput/2);
		TabInputBit[NbInput++] = mask;
		TabInputBit[NbInput++] = mask;
	}
	if (NbInput > 64)
		NbInput = 64;
	PadKeyToGInput();
	InitBindings(NbInput, TabInput, TabInputBit);
}

/* DefaultInput is TLBA1C.exe 0x41a190, LBA2's RestoreInput: the keys' bindings their defaults. */
void
DefaultInput(void)
{
	memcpy(TabInput, DefTabInput, sizeof TabInput);
}

/*
 * ReadInputCfg is TLBA1C.exe 0x41a1d0, LBA2's ReadInputConfig: the
 * bindings of lba.cfg's keys name%d_1 and name%d_2 into dst, each
 * missing key's from def.
 */
static void
ReadInputCfg(char *name, uint32_t *dst, uint32_t *def)
{
	char id[64];
	int i;

	for (i = 0; i < Ninput; i++) {
		snprintf(id, sizeof id, "%s%d_1", name, i);
		dst[2*i] = ReadConfigValue(id, def[2*i]);
		snprintf(id, sizeof id, "%s%d_2", name, i);
		dst[2*i + 1] = ReadConfigValue(id, def[2*i + 1]);
	}
}

/*
 * InitInput is TLBA1C.exe 0x41a260: the bindings of the keys and of the
 * pad from lba.cfg, and the keys the pad presses.
 */
void
InitInput(void)
{
	ReadInputCfg("Input", TabInput, DefTabInput);
	ReadInputCfg("GInput", TabGInput, DefTabGInput);
	GInputToPadKey();
	SetInputBits();
}

/*
 * WriteInputCfg is TLBA1C.exe 0x41a2a0, LBA2's WriteInputConfig: the
 * bindings of src into lba.cfg's keys name%d_1 and name%d_2.
 */
static void
WriteInputCfg(uint32_t *src, char *name)
{
	char id[64];
	int i;

	for (i = 0; i < Ninput; i++) {
		snprintf(id, sizeof id, "%s%d_1", name, i);
		Def_WriteValue((UBYTE*)PathConfigFile, (UBYTE*)id, src[2*i]);
		snprintf(id, sizeof id, "%s%d_2", name, i);
		Def_WriteValue((UBYTE*)PathConfigFile, (UBYTE*)id, src[2*i + 1]);
	}
}

/*
 * SaveInput is TLBA1C.exe 0x41a320: the bindings of the keys and of the
 * pad into lba.cfg, where lba.cfg has bytes, which the port's built-in
 * text always gives it.
 */
void
SaveInput(void)
{
	if (FileSize(PathConfigFile) != 0) {
		WriteInputCfg(TabInput, "Input");
		WriteInputCfg(TabGInput, "GInput");
	}
}

/*
 * KeyHeld: whether a key is held, with GamePad the Classic's Key, its
 * virtual key, as its MainLoop waits for its release after the zoom and
 * as the pause starts (TLBA1C.exe 0x421460 and 0x42134e), and the 1994
 * Key otherwise.
 */
int32_t
KeyHeld(void)
{
	if (FlagGamePad)
		return KeyVk != 0;
	return Key != 0;
}

/*
 * PauseWait: whether the pause goes on, until a key, an arrow or a fire
 * key, as the 1994 game ends it, and with GamePad the Pause input too,
 * which the Classic's MainLoop ends it on (TLBA1C.exe 0x421365) and a
 * key bound to it gives where the 1994 Key keeps none of it.
 */
int32_t
PauseWait(void)
{
	if (FlagGamePad && (InputActions & I_PAUSE) != 0)
		return 0;
	return Key == 0 && Joy == 0 && Fire == 0;
}

/*
 * TestKeyEsc is TLBA1C.exe 0x41a1b0: whether Escape is down, the 1994
 * game's key, or with GamePad the Menus input.
 */
int32_t
TestKeyEsc(void)
{
	return Key == K_ESC || MenusHeld();
}

/*
 * Whether Escape is up and with GamePad the Menus input too, as the
 * negation of TestKeyEsc, whose stand-in in the DOS builds keeps the
 * 1994 game's test of the key, as Watcom compiles !(Key == K_ESC) other
 * than Key != K_ESC.
 */
int32_t
NotKeyEsc(void)
{
	return !TestKeyEsc();
}

/* Whether the Menus input is held, with GamePad. */
int32_t
MenusHeld(void)
{
	return FlagGamePad && (InputActions & I_MENUS);
}

/* Whether the main loop's frame took the Menus input, with GamePad. */
int32_t
MyMenus(void)
{
	return FlagGamePad && (MyActions & I_MENUS);
}

/*
 * Whether the 1994 game's key is down, or with GamePad its input in
 * the Classic's place, as the Classic tests its inputs.
 */
int32_t
KeyInput(int32_t key, uint32_t input)
{
	if (FlagGamePad)
		return (InputActions & input) != 0;
	return Key == key;
}

/* The negation of KeyInput, for the DOS builds' stand-in, as NotKeyEsc. */
int32_t
NotKeyInput(int32_t key, uint32_t input)
{
	return !KeyInput(key, input);
}

/* KeyInput for the main loop's frame, its MyKey and MyActions. */
int32_t
MyInput(int32_t key, uint32_t input)
{
	if (FlagGamePad)
		return (MyActions & input) != 0;
	return MyKey == key;
}

/*
 * The behaviour of the first of the four behaviours' inputs held in
 * actions, as the Classic's MainLoop (TLBA1C.exe 0x420b70) takes them,
 * or -1.
 */
int32_t
ComportementInput(uint32_t actions)
{
	static uint32_t inputs[] = {I_NORMAL, I_SPORTIF, I_AGRESSIF, I_DISCRET};
	int i;

	for (i = 0; i < (int)(sizeof inputs / sizeof inputs[0]); i++)
		if (actions & inputs[i])
			return C_NORMAL + i;
	return -1;
}

/*
 * Whether a key of the behaviour menu is held, F1 to F4, or with
 * GamePad the Classic's inputs of the menu, as its MenuComportement
 * (TLBA1C.exe 0x410b40) tests them: the menu's, the four behaviours'
 * and the previous and the next behaviour's.
 */
int32_t
ComportementHeld(void)
{
	if (FlagGamePad)
		return (InputActions & (I_COMPORTEMENT|I_COMPORTEMENTS|I_PREVIOUS|I_NEXT)) != 0;
	return Key >= K_F1 && Key <= K_F4;
}

/*
 * The behaviour after Comportement that the previous or the next
 * behaviour's input in actions gives, as the Classic's MainLoop
 * (TLBA1C.exe 0x421170) steps it: the previous of the normal one the
 * discreet one, and of the protopack the normal one, the next of the
 * discreet one and of the protopack the normal one.
 */
int32_t
StepComportement(uint32_t actions)
{
	if (actions & I_PREVIOUS) {
		if (Comportement > C_DISCRET)
			return C_NORMAL;
		if (Comportement == C_NORMAL)
			return C_DISCRET;
		return Comportement - 1;
	}
	if (Comportement + 1 > C_DISCRET)
		return C_NORMAL;
	return Comportement + 1;
}

/*
 * The name grid's key with GamePad, a word of the BIOS's as GetAscii
 * gives it in car, as the Classic's InputPlayerName (TLBA1C.exe
 * 0x4123f0) reads its keys: KeyVk's arrows, a key the pad presses too,
 * moving the cursor, again 11 ticks after the press and every 4 while
 * held (*last the key, *timer the repeat's time), Space and the pad's
 * Return taking the letter, the keyboard's Return, which the BIOS's
 * word gives, the name, Backspace the last, and the keyboard's arrows'
 * words left to KeyVk's.
 */
int32_t
GridInput(int32_t car, int32_t *last, uint32_t *timer)
{
	int32_t vk;

	vk = KeyVk;
	if (vk == *last) {
		if (*timer < TimerRef)
			*timer = TimerRef + 3;
		else
			vk = 0;
	} else {
		*timer = TimerRef + 10;
		*last = vk;
	}
	if (car == A_LEFT || car == A_RIGHT || car == A_UP || car == A_DOWN)
		car = 0;
	switch (vk) {
	case 0x25:
		return A_LEFT;
	case 0x27:
		return A_RIGHT;
	case 0x26:
		return A_UP;
	case 0x28:
		return A_DOWN;
	case 0x20:
		return A_SPACE;
	case 0x0D:
		return car == A_RETURN ? car : A_SPACE;
	case 0x08:
		return A_BACKSPACE;
	}
	return car;
}

/*
 * LBA2's name editor's key with GamePad, a word of the BIOS's as
 * GetAscii gives it in car, or at a call that gives none, Return, Escape
 * or Backspace as the pad presses them, which make no word of the
 * BIOS's, and Escape for the Menus input, once a press (*last the one
 * taken).
 */
int32_t
EditInput(int32_t car, int32_t *last)
{
	int32_t k;

	k = 0;
	if (InputActions & I_MENUS)
		k = A_ESC;
	else if (LastPadButton != 0 && KeyVk == 0x0D)
		k = A_RETURN;
	else if (LastPadButton != 0 && KeyVk == 0x1B)
		k = A_ESC;
	else if (LastPadButton != 0 && KeyVk == 0x08)
		k = A_BACKSPACE;
	if (k == *last || (k != 0 && car != 0))
		return car;
	*last = k;
	return car != 0 ? car : k;
}
