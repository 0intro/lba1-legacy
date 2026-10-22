#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sys/compat.h"
#include "c_extern.h"

/*
 * FEATURE: the Classic 3.2.3's controls pages (TLBA1C.exe 0x40bdf0 to
 * 0x40cf72), behind GamePad: the keys' and the pad's bindings of the 27
 * inputs shown in a list and changed, each new one written into
 * lba.cfg. Taken from the Classic 3.2.3.
 */

enum
{
	Ninput = 27,	/* the inputs, the list's lines before its three of the page's own */
	Nline = 30,	/* the lines, Default, Restore and Accept the last three */
	Pad = 0x200,	/* a pad's input in the pad's bindings */
	Grey = 8,	/* ShadeBox's darkening of the boxes */
};

/* sources/input.c's bindings, the keys' and the pad's, and those a page started from */
extern uint32_t	TabInput[];
extern uint32_t	TabGInput[];
extern uint32_t	MemoTabInput[];

/* the Classic's virtual keys the pages read */
enum
{
	VkBack = 0x08,
	VkTab = 0x09,
	VkReturn = 0x0D,
	VkEscape = 0x1B,
	VkSpace = 0x20,
	VkPrior = 0x21,
	VkNext = 0x22,
	VkEnd = 0x23,
	VkHome = 0x24,
	VkLeft = 0x25,
	VkUp = 0x26,
	VkRight = 0x27,
	VkDown = 0x28,
	VkDelete = 0x2E,
};

/*
 * TabKeyText is TLBA1C.exe 0x7f1f50: the keys the texts 0x1038 on name,
 * in their order, 0 ending them: the keypad, the arrows and the grey
 * keys, the Shift, Ctrl and Alt keys, Tab, Num Lock, Caps Lock, F1 to
 * F12, the digits, Space, Return, the Windows and menu keys, Backspace,
 * Pause, Scroll Lock, Print Screen, Shift, Ctrl and Alt
 */
static uint8_t	TabKeyText[] = {
	0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x60,
	0x6E, 0x6C, 0x6F, 0x6A, 0x6B, 0x6D, 0x26, 0x28, 0x25, 0x27,
	0x24, 0x23, 0x21, 0x22, 0x2D, 0x2E, 0xA0, 0xA1, 0xA2, 0xA3,
	0xA4, 0xA5, 0x09, 0x90, 0x14, 0x70, 0x71, 0x72, 0x73, 0x74,
	0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, '1', '2', '3',
	'4', '5', '6', '7', '8', '9', '0', 0x20, 0x0D, 0x5B,
	0x5C, 0x5D, 0x08, 0x13, 0x91, 0x2C, 0x10, 0x11, 0x12, 0,
};

/* TLBA1C.exe 0x7f1f98: the names of the pad's 31 inputs, in English in every language */
static char	*TabPadText[31] = {
	"Action Down", "Action Right", "Action Left", "Action Up", "Back",
	"Select", "Start", "Left Stick", "Right Stick", "Left Shoulder",
	"Right Shoulder", "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right",
	"", "", "", "", "", "",
	"L-Axis Left", "L-Axis Right", "L-Axis Up", "L-Axis Down",
	"R-Axis Left", "R-Axis Right", "R-Axis Up", "R-Axis Down",
	"Left Trigger", "Right Trigger",
};

/*
 * The names SDL 2.0.22's SDL_GetKeyName gives the keycodes TLBA1C.exe's
 * VkToSdlKey (0x4062f0) gives the keys TabKeyText and the F keys leave
 * out, which GetKeyName (0x406880) asks SDL for and turns into code page
 * 850 (WideCharToMultiByte at 0x4068d4), whose two bytes here past ASCII
 * CP437 holds alike: SDL's names held here, so that a replay draws the
 * same pixels with any SDL
 */
static struct {
	uint8_t	vk;
	char	*name;
} keynames[] = {
	{0x1B, "Escape"}, {0x3B, "$"}, {0x3C, "<"}, {0x3D, "!"}, {0x3E, ":"},
	{0x3F, "*"}, {0x40, "^"},
	{'A', "A"}, {'B', "B"}, {'C', "C"}, {'D', "D"}, {'E', "E"}, {'F', "F"},
	{'G', "G"}, {'H', "H"}, {'I', "I"}, {'J', "J"}, {'K', "K"}, {'L', "L"},
	{'M', "M"}, {'N', "N"}, {'O', "O"}, {'P', "P"}, {'Q', "Q"}, {'R', "R"},
	{'S', "S"}, {'T', "T"}, {'U', "U"}, {'V', "V"}, {'W', "W"}, {'X', "X"},
	{'Y', "Y"}, {'Z', "Z"},
	{0xBA, ";"}, {0xBB, "="}, {0xBC, ","}, {0xBD, ")"},
	{0xC0, "\227"}, {0xDE, "\375"},
};

/* the axes' and the hat's names before the direction a joystick's code past 0x120 names */
static char	*axisnames[] = {"", "Z ", "R ", "U ", "V ", "H "};

/* the first of the five pairs of a joystick's directions each axis takes, past the code's 0x20 */
static uint8_t	axisfirst[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4};

/* ListControls is TLBA1C.exe 0x833c80: the input each of the list's lines shows */
static int32_t	ListControls[Ninput];

/* NbControls is TLBA1C.exe 0x834054: the inputs the list shows */
static int32_t	NbControls;

/* ControlsSelect is TLBA1C.exe 0x834058: the list's line selected, kept from a page to the next */
static int32_t	ControlsSelect;

/* ControlsColumn is TLBA1C.exe 0x833c78: the binding selected, the first or the second */
static int32_t	ControlsColumn;

/* ControlsMode is TLBA1C.exe 0x834050: 0 the keys' page, 1 the pad's */
static int32_t	ControlsMode;

/* TLBA1C.exe 0x834060 and 0x833cf0: the names of the keys' and the pad's bindings, two of 16 bytes an input */
static char	KeyNames[Ninput*32];
static char	PadNames[Ninput*32];

/*
 * CopyExtText is TLBA1C.exe 0x40f160: text num into dst, size bytes at
 * most, GetExtText taking 0x38 as the text 0x1038, as the Classic's
 * pages give their texts.
 */
static void
CopyExtText(int32_t num, char *dst, int size)
{
	strncpy(dst, GetExtText(num), size - 1);
	dst[size - 1] = '\0';
}

/* ShadeBox8 is TLBA1C.exe 0x40bdf0: the box darkened. */
static void
ShadeBox8(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	ShadeBox(x0, y0, x1, y1, Grey);
}

/* CursorBox is TLBA1C.exe 0x40be00: the cursor's box, colour 0x44. */
static void
CursorBox(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	Box(x0, y0, x1, y1, 0x44);
}

/* dst, of size bytes, with s added, as much as it holds. */
static void
catn(char *dst, int size, char *s)
{
	int n;

	n = strlen(dst);
	while (n < size - 1 && *s != '\0')
		dst[n++] = *s++;
	dst[n] = '\0';
}

/*
 * GetKeyName is TLBA1C.exe 0x406880: the name SDL gives the key vk,
 * keynames', into dst, and whether it has one.
 */
static int
GetKeyName(uint32_t vk, char *dst, int size)
{
	int i;

	for (i = 0; i < (int)(sizeof keynames / sizeof keynames[0]); i++)
		if (keynames[i].vk == vk) {
			snprintf(dst, size, "%s", keynames[i].name);
			return 1;
		}
	return 0;
}

/*
 * GetInputName is TLBA1C.exe 0x40be30: the name of a binding's code
 * into dst, 15 bytes and the NUL at most, its first 14 kept: dots for
 * 0, the pad's input's, F and its number, a joystick's, the text of a
 * key TabKeyText names, SDL's name, or Key and its number in hex.
 *
 * FIXED: TLBA1C.exe reads a pad's name past TabPadText for an input from
 * 31 up, which a hand-written Input key holding 0x200 and up gives, and
 * stops the program where a joystick's name outgrows its 16 bytes
 * (strcat_s). Such an input takes Key and its number, and a long name
 * is cut.
 */
void
GetInputName(char *dst, uint32_t code)
{
	char tmp[30];
	uint32_t n, axis;
	int i;

	if (code == 0) {
		snprintf(dst, 16, "..............");
	} else if ((code & Pad) && (code & ~Pad) < 31) {
		snprintf(dst, 16, "%s", TabPadText[code & ~Pad]);
	} else if (code - 0x70 <= 0x17) {
		snprintf(dst, 16, "F%d", (int)(code - 0x6F));
	} else if (code >= 0x100 && !(code & Pad)) {
		code -= 0x100;
		axis = code & 0x3F;
		snprintf(dst, 16, "Joy %d ", (int)(code>>6) + 1);
		if (axis < 0x20) {
			snprintf(tmp, sizeof tmp, "Btn %d", (int)axis + 1);
			catn(dst, 16, tmp);
		} else {
			if (axis - 0x20 < sizeof axisfirst) {
				n = axisfirst[axis - 0x20];
				axis -= n == 0 ? 0x20 : 0x20 + 2*n;
				catn(dst, 16, axisnames[n]);
			} else {
				axis -= 0x2C;
				catn(dst, 16, axisnames[5]);
			}
			CopyExtText(0x4B - axis, tmp, sizeof tmp);
			catn(dst, 16, tmp);
		}
	} else {
		for (i = 0; TabKeyText[i] != 0; i++)
			if (TabKeyText[i] == code)
				break;
		/*
		 * FEATURE: a key outside the Classic's names takes Key and its
		 * number, where the Classic asks Windows for the character the
		 * keyboard's layout gives it (VkToAscii, TLBA1C.exe 0x422eb0),
		 * so that a replay draws the same pixels on every layout. The
		 * port's own.
		 */
		if (TabKeyText[i] != 0)
			CopyExtText(0x38 + i, dst, 16);
		else if (!GetKeyName(code, dst, 16))
			snprintf(dst, 16, "Key %02X", (unsigned)code);
	}
	dst[14] = '\0';
}

/* GetInputNames is TLBA1C.exe 0x40c010: the names of the bindings of tab into names. */
static void
GetInputNames(uint32_t *tab, char *names)
{
	int i;

	for (i = 0; i < NbControls; i++) {
		GetInputName(names + 32*i, tab[2*ListControls[i]]);
		GetInputName(names + 32*i + 16, tab[2*ListControls[i] + 1]);
	}
}

/* InitControlsList is TLBA1C.exe 0x40c060: every input in the list, in its order. */
static void
InitControlsList(void)
{
	int i;

	for (i = 0; i < Ninput; i++)
		ListControls[i] = i;
	NbControls = i;
}

/*
 * DrawControlAction is TLBA1C.exe 0x40c080: line n of the inputs, its
 * input's name and its two bindings' names, the line selected in white,
 * the binding selected in the cursor's box and the others moved 5
 * pixels left.
 */
static void
DrawControlAction(int32_t n, char *names)
{
	char tmp[100];
	int32_t x, y;

	y = (int16_t)(9*n + 40);
	x = 0x1E;
	CopyBlock(15, y - 1, 0x16E, y + 8, Screen, 15, y - 1, Log);
	ShadeBox8(15, y - 1, 0x16E, y + 8);
	if (n == ControlsSelect) {
		CoulText(0xFF, -1);
		x = 0x19;
	} else {
		CoulText(0x4B, -1);
	}
	CopyExtText(ListControls[n] + 0x11, tmp, sizeof tmp);
	Text(x, y, tmp);
	CopyBlockPhys(15, y - 1, 0x16E, y + 8);

	CopyBlock(0x178, y - 1, 0x1F0, y + 8, Screen, 0x178, y - 1, Log);
	ShadeBox8(0x178, y - 1, 0x1F0, y + 8);
	x = 0x17D;
	if (n == ControlsSelect) {
		if (ControlsColumn == 0) {
			CoulText(0xFF, -1);
			CursorBox(0x178, y - 1, 0x1F0, y + 8);
			DrawCadre(0x178, y - 1, 0x1F0, y + 8);
		} else {
			x = 0x178;
		}
	}
	Text(x, y, names + 32*n);
	CopyBlockPhys(0x178, y - 1, 0x1F0, y + 8);

	CopyBlock(0x1FA, y - 1, 0x271, y + 8, Screen, 0x1FA, y - 1, Log);
	ShadeBox8(0x1FA, y - 1, 0x271, y + 8);
	x = 0x1FF;
	if (n == ControlsSelect) {
		CoulText(0xFF, -1);
		if (ControlsColumn == 1) {
			CursorBox(0x1FA, y - 1, 0x271, y + 8);
			DrawCadre(0x1FA, y - 1, 0x271, y + 8);
		} else {
			x = 0x1FA;
		}
	}
	Text(x, y, names + 32*n + 16);
	CopyBlockPhys(0x1FA, y - 1, 0x271, y + 8);
}

/*
 * DrawControlsLine is TLBA1C.exe 0x40c2e0: line n, an input's, or one of
 * the three of the page, centred in its box, the one selected in white
 * in the cursor's box.
 */
static void
DrawControlsLine(int32_t n, char *names)
{
	char tmp[100];
	int32_t x0, x1, y, half;

	if (n < Ninput) {
		DrawControlAction(n, names);
		return;
	}
	n -= Ninput;
	y = (n + 30) * 10;
	CopyExtText(n + 0x35, tmp, sizeof tmp);
	half = strlen(tmp) * 8 / 2;
	x0 = 0x136 - half;
	x1 = 0x14A + half;
	CopyBlock(x0, y - 2, x1, y + 8, Screen, x0, y - 2, Log);
	ShadeBox8(x0, y - 2, x1, y + 8);
	if (ControlsSelect - Ninput == n) {
		CoulText(0xFF, -1);
		CursorBox(x0, y - 2, x1, y + 8);
		DrawCadre(x0, y - 2, x1, y + 8);
	} else {
		CoulText(0xAE, -1);
	}
	Text(0x140 - (int32_t)(strlen(tmp) * 8 / 2), y, tmp);
	CopyBlockPhys(x0, y - 2, x1, y + 8);
}

/* DrawControlsBox is TLBA1C.exe 0x40c430: a box darkened and framed, shown at once if copy. */
static void
DrawControlsBox(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int copy)
{
	if (copy)
		CopyBlock(x0, y0, x1, y1, Screen, x0, y0, Log);
	ShadeBox8(x0, y0, x1, y1);
	DrawCadre(x0, y0, x1, y1);
	if (copy)
		CopyBlockPhys(x0, y0, x1, y1);
}

/*
 * DrawControlsHelp is TLBA1C.exe 0x40c4a0: the help's line, text 0x102C
 * and m, in red with m's 0x80, the pad's page's own for the first, or
 * from 0x2000 the port's own text m.
 */
static void
DrawControlsHelp(int32_t m)
{
	char tmp[100];
	int32_t n;

	n = (m & ~0x80) + 0x2C;
	if (ControlsMode == 1 && n == 0x2C)
		n = 0x31;
	if (m & 0x2000)
		snprintf(tmp, sizeof tmp, "%s", GetPortText(m & ~0x80));
	else
		CopyExtText(n, tmp, sizeof tmp);
	DrawControlsBox(10, 0x152, 0x276, 0x166, 1);
	CoulText(m & 0x80 ? 0x5A : 0x3D, -1);
	Text(0x140 - (int32_t)(strlen(tmp) * 8 / 2), 0x159, tmp);
}

/* DrawControls is TLBA1C.exe 0x40c560: the page whole, its boxes, titles, help and lines. */
static void
DrawControls(char *names)
{
	char tmp[100];
	int32_t i;

	CopyScreen(Screen, Log);
	DrawControlsBox(10, 10, 0x172, 0x1E, 0);
	DrawControlsBox(0x174, 10, 0x1F4, 0x1E, 0);
	DrawControlsBox(0x1F6, 10, 0x276, 0x1E, 0);
	DrawControlsBox(10, 0x20, 0x172, 0x120, 0);
	DrawControlsBox(0x174, 0x20, 0x1F4, 0x120, 0);
	DrawControlsBox(0x1F6, 0x20, 0x276, 0x120, 0);
	CoulText(0x3D, -1);
	CopyExtText(0x32, tmp, sizeof tmp);
	Text(0x1E, 0x11, tmp);
	CopyExtText(0x33, tmp, sizeof tmp);
	Text(0x17D, 0x11, tmp);
	CopyExtText(0x34, tmp, sizeof tmp);
	Text(0x1FF, 0x11, tmp);
	DrawControlsHelp(0);
	for (i = 0; i < NbControls; i++)
		DrawControlAction(i, names);
	DrawControlsBox(10, 0x125, 0x276, 0x14D, 0);
	for (i = Ninput; i < Nline; i++)
		DrawControlsLine(i, names);
	CopyBlockPhys(0, 0, 640, 480);
}

/* FindInput is TLBA1C.exe 0x40c720: the input code is a binding of in tab, or -1. */
static int32_t
FindInput(uint32_t *tab, uint32_t code)
{
	int32_t i;

	for (i = 0; i < NbControls; i++)
		if (tab[2*i] == code || tab[2*i + 1] == code)
			return i;
	return -1;
}

/*
 * The page's key of KeyVk, as EditControls' switch (TLBA1C.exe 0x40c7ef)
 * gives it: the grey keys and Return, Escape and Delete themselves,
 * Backspace and the keypad's point Delete, Tab Right, Space and the
 * keypad's Enter Return, the keypad's digits their keys, and for the
 * others the arrows of the inputs held.
 */
static int32_t
controlskey(int32_t vk)
{
	switch (vk) {
	case VkBack:
	case 0x6E:
		return VkDelete;
	case VkTab:
	case 0x66:
		return VkRight;
	case VkReturn:
	case VkEscape:
	case VkPrior:
	case VkNext:
	case VkEnd:
	case VkHome:
	case VkLeft:
	case VkUp:
	case VkRight:
	case VkDown:
	case VkDelete:
		return vk;
	case VkSpace:
	case 0x6C:
		return VkReturn;
	case 0x61:
		return VkEnd;
	case 0x62:
		return VkDown;
	case 0x63:
		return VkNext;
	case 0x64:
		return VkLeft;
	case 0x67:
		return VkHome;
	case 0x68:
		return VkUp;
	case 0x69:
		return VkPrior;
	}
	if (InputActions & I_UP)
		return VkUp;
	if (InputActions & I_DOWN)
		return VkDown;
	if (InputActions & I_LEFT)
		return VkLeft;
	if (InputActions & I_RIGHT)
		return VkRight;
	return 0;
}

/*
 * The refusal of EditControls (TLBA1C.exe 0x40c9c8 and 0x40cd94): sample
 * 150 at a pitch drawn from rand, and the help's line help.
 */
static void
refuse(int32_t help)
{
	HQ_MixSample(150, 0xC18 + rand() % 2000, 1, 128, 128);
	DrawControlsHelp(help);
}

/*
 * The binding of EditControls (TLBA1C.exe 0x40c96f to 0x40cbf0): of tab,
 * the first or the second of line sel as ControlsColumn says, changed
 * for the next key or pad's input pressed, Escape keeping it, an input
 * bound already refused, MyKey cleared.
 *
 * FIXED: TLBA1C.exe keeps the binding in MyKey, of 16 bits, so that a
 * code from 0x8000 up, which a hand-written lba.cfg gives, comes back
 * cut, its sign widened, where Escape or a refusal keeps it. The binding
 * keeps its 32 bits.
 */
static void
bindinput(uint32_t *tab, char *names, int32_t sel, int pad)
{
	uint32_t *p, before, code;
	char *name;
	int32_t help;

	p = &tab[2*sel + ControlsColumn];
	name = names + 16*(ControlsColumn + 2*sel);
	before = *p;
	while (AnyInput())
		idle();
	*p = 0;
	GetBindInput(pad);
	MyKey = 0;
	PadButtons = 0;
	LastPadButton = 0;
	do {
		MyKey = GetBindInput(pad);
		if (MyKey != VkEscape) {
			if (tab == TabGInput)
				MyKey = LastPadButton;
			else if (LastPadButton != 0 || PadButtons != 0)
				MyKey = 0;
		}
		if ((TimerRef & 0x1F) < 0xF) {
			DrawControlsHelp(3);
			snprintf(name, 16, "              ");
		} else {
			DrawControlsHelp(1);
			snprintf(name, 16, "..............");
		}
		DrawControlAction(sel, names);
		Flip();
	} while (MyKey == 0);
	help = 0;
	code = (uint32_t)(int32_t)MyKey;
	if (MyKey == VkEscape) {
		code = before;
	} else if (FindInput(tab, code) != -1) {
		HQ_MixSample(150, 0xC18 + rand() % 2000, 1, 128, 128);
		code = before;
		help = 0x82;
	}
	/*
	 * FIXED: TLBA1C.exe leaves in MyKey the key bound, or the old one
	 * where Escape or a refusal kept it, which the page then runs as a
	 * command: Delete clears the binding at once, Page Up, Page Down,
	 * Home and End move the selection, Left and Right take the other
	 * column. MyKey is cleared.
	 */
	MyKey = 0;
	DrawControlsHelp(help);
	*p = code;
	GetInputName(name, code);
	while (AnyInput())
		idle();
}

/*
 * EditControls is TLBA1C.exe 0x40c750, LBA2's DoMenuConfig where the two
 * agree (SOURCES/CONFIG.CPP): the page of the bindings tab, their names
 * in names. Up, Down, Page Up, Page Down, Home and End move, a key held
 * again 20 ticks after its press and every 2 after, Left and Right take
 * the other binding, Return changes it or picks a line of the page,
 * Delete clears it, Escape goes to Accept Configuration and from there
 * leaves with the bindings MemoTabInput keeps, the page's first ones or
 * those Default Configuration replaced. Leaving gives the inputs their
 * bindings, writes them into lba.cfg and keeps the keys held out.
 */
static void
EditControls(uint32_t *tab, char *names)
{
	int32_t old, prev, done, pad;
	uint32_t pressed, repeat;

	repeat = 0;
	done = 0;
	pad = tab == TabGInput;
	InitControlsList();
	GetInputNames(tab, names);
	Palette(PtrPal);
	memcpy(MemoTabInput, tab, 2*Ninput*sizeof tab[0]);
	DrawControls(names);
	while (AnyInput())
		idle();
	prev = ControlsSelect;
	MyKey = 0;
	idle();
	pressed = TimerRef;
	do {
		DrawControlsLine(ControlsSelect, names);
		Flip();
		old = MyKey;
		MyKey = controlskey(KeyVk);
		idle();
		if (old != MyKey) {
			pressed = TimerRef;
		} else if (MyKey == VkEscape) {
			continue;
		} else if (TimerRef - pressed < 20) {
			repeat = 0;
			continue;
		} else if (repeat == 0) {
			repeat = TimerRef;
		} else if (TimerRef - repeat < 2) {
			continue;
		} else {
			repeat += 2;
		}
		if (MyKey == VkUp) {
			ControlsSelect--;
			if (ControlsSelect < 0)
				ControlsSelect = Nline - 1;
			else if (ControlsSelect == Ninput - 1)
				ControlsSelect = NbControls - 1;
		} else if (MyKey == VkDown) {
			ControlsSelect++;
			if (ControlsSelect >= Nline)
				ControlsSelect = 0;
			else if (ControlsSelect == NbControls)
				ControlsSelect = Ninput;
		} else if (MyKey == VkReturn) {
			if (ControlsSelect >= NbControls) {
				switch (ControlsSelect - Ninput) {
				case 0:
					memcpy(MemoTabInput, tab, 2*Ninput*sizeof tab[0]);
					if (pad)
						DefaultGInput();
					else
						DefaultInput();
					GetInputNames(tab, names);
					DrawControls(names);
					break;
				case 1:
					memcpy(tab, MemoTabInput, 2*Ninput*sizeof tab[0]);
					GetInputNames(tab, names);
					DrawControls(names);
					break;
				case 2:
					done = 1;
					break;
				}
			} else if (pad && (1u<<ControlsSelect) == I_MENUS && ControlsColumn == 0) {
				refuse(0x84);
			} else {
				bindinput(tab, names, ControlsSelect, pad);
			}
		}
		switch (MyKey) {
		case VkEscape:
			if (ControlsSelect == Nline - 1) {
				if (!done) {
					memcpy(tab, MemoTabInput, 2*Ninput*sizeof tab[0]);
					GetInputNames(tab, names);
				}
				done = 2;
				continue;
			}
			ControlsSelect = Nline - 1;
			break;
		case VkPrior:
			if (ControlsSelect >= Ninput)
				ControlsSelect += NbControls - 0x25;
			else
				ControlsSelect -= 10;
			if (ControlsSelect < 0)
				ControlsSelect = 0;
			break;
		case VkNext:
			/*
			 * FIXED: TLBA1C.exe moves a line of the inputs' last ten
			 * by its distance from the first of them, so that Page
			 * Down from line 17 stays there. The selection moves ten
			 * lines on, into the page's entries past the last input,
			 * as LBA2 Legacy's page moves it.
			 */
			if (ControlsSelect < NbControls && ControlsSelect + 10 >= NbControls)
				ControlsSelect += 10 - NbControls + Ninput;
			else
				ControlsSelect += 10;
			if (ControlsSelect >= Nline)
				ControlsSelect = Nline - 1;
			break;
		case VkEnd:
			ControlsSelect = Nline - 1;
			break;
		case VkHome:
			ControlsSelect = 0;
			break;
		}
		if (prev != ControlsSelect) {
			DrawControlsLine(prev, names);
			prev = ControlsSelect;
		}
		if (ControlsSelect >= NbControls)
			continue;
		if (MyKey == VkLeft || MyKey == VkRight)
			ControlsColumn ^= 1;
		if (MyKey != VkDelete)
			continue;
		if (pad && (1u<<ControlsSelect) == I_MENUS && ControlsColumn == 0) {
			refuse(0x84);
			continue;
		}
		/*
		 * FIXED: TLBA1C.exe clears an input's last key though one of the
		 * pad's inputs is bound to it, whose key that is: the pad's input
		 * then presses nothing, and PadKeyToGInput drops its binding as
		 * the page closes. The clear is refused, the help saying why.
		 */
		if (!pad && tab[2*ControlsSelect + ControlsColumn] != 0 && tab[2*ControlsSelect + (ControlsColumn ^ 1)] == 0
		&& (TabGInput[2*ControlsSelect] != 0 || TabGInput[2*ControlsSelect + 1] != 0)) {
			refuse(0x2002 | 0x80);
			continue;
		}
		tab[2*ControlsSelect + ControlsColumn] = 0;
		snprintf(names + 16*(ControlsColumn + 2*ControlsSelect), 16, "..............");
	} while (!done);
	CopyScreen(Screen, Log);
	CopyBlockPhys(0, 0, 640, 480);
	GInputToPadKey();
	SetInputBits();
	SaveInput();
	ReadInputs(InputActions | I_ACTION_M|I_RETURN|I_COMPORTEMENT|I_THROW|I_INVENTORY|I_MENUS);
	MyKey = 0;
}

/* ControlsMenu is TLBA1C.exe 0x40cf40: the keys' page (mode 0) or the pad's (1). */
void
ControlsMenu(int32_t mode)
{
	ControlsMode = mode;
	if (mode == 0)
		EditControls(TabInput, KeyNames);
	else if (mode == 1)
		EditControls(TabGInput, PadNames);
}
