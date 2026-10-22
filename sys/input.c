#include <stddef.h>
#include <stdint.h>
#include "dat.h"
#include "fns.h"

enum
{
	Npad = 31,	/* the pad's inputs */
	Pad = 0x200,	/* a pad's input in LastPadButton and the bindings */
};

/*
 * FEATURE: the Classic 3.2.3's input (TLBA1C.exe 0x4262c0 to 0x426670,
 * the pad's PadButton 0x4051c0, GetBinding
 * 0x405c70 and GetBindInput 0x422f50), behind GamePad: each key held by
 * its virtual key, Windows', in KeyState, the pad's inputs pressing
 * keys, and the game's inputs read from the keys through bindings,
 * a code to an input's bits, as LBA2 reads them
 * (LIB386/SYSTEM/INPUT.CPP). The record keeps the keyboard's XT codes
 * (replay.c): a code's virtual key is the one SDL's key gives it on a
 * US keyboard, or the record's own. keyb.c's keyboard runs beside it,
 * Key and FuncKey its own. Taken from the Classic 3.2.3, the switch the
 * port's own.
 */

int32_t	FlagGamePad;	/* lba.cfg's GamePad: 1 with ON */

/* KeyState is TLBA1C.exe 0x8dbd70: each virtual key, 0x80 while it is down */
uint8_t	KeyState[256];

/* KeyVk is TLBA1C.exe 0x8dbe70, the Classic's Key: the virtual key last pressed, 0 from a release on */
int32_t	KeyVk;

/* InputMasked is TLBA1C.exe 0x8dd4e8: the inputs ReadInputs keeps out until their release */
static uint32_t	InputMasked;

/*
 * TabPadKey is TLBA1C.exe 0x818bf0: the key each of the pad's 31
 * inputs presses in play, which GInputToPadKey gives from lba.cfg's
 * bindings at the start
 */
uint32_t	TabPadKey[31] = {
	'W', 0x0D, 0x20, 0xA0, 'H', 'H', 0x1B, 0,
	0, 0xA2, 0xA4, 'J', 'M', 'G', 'C', 0,
	0, 0, 0, 0, 0, 0x25, 0x27, 0,
	0, 0x25, 0x27, '9', '0', 0x28, 0x26,
};

/*
 * TabPadMenu is TLBA1C.exe 0x7f1e48: the key each of the pad's inputs
 * presses in the menus: A Return, B Escape, X Backspace, Y Shift, Back
 * and Guide H, Start Escape, the sticks' presses and the left shoulder
 * Ctrl, the right one Alt, the cross and the sticks the arrows, the
 * triggers down and up
 */
static uint32_t	TabPadMenu[31] = {
	0x0D, 0x1B, 0x08, 0xA0, 'H', 'H', 0x1B, 0xA2,
	0xA2, 0xA2, 0xA4, 0x26, 0x28, 0x25, 0x27, 0,
	0, 0, 0, 0, 0, 0x25, 0x27, 0x26,
	0x28, 0x25, 0x27, 0x26, 0x28, 0x28, 0x26,
};

/* PadButtons is TLBA1C.exe 0x8dbd64: the pad's 31 inputs held, a bit each */
uint32_t	PadButtons;

/* LastPadButton is TLBA1C.exe 0x8dbd68: the pad's input last pressed, 0x200 and its number, 0 from a release on */
int32_t	LastPadButton;

/* InputContext is TLBA1C.exe 0x819a70: 1 in the menus, the pad's keys TabPadMenu's, 0 in play, TabPadKey's */
int32_t	InputContext = 1;

/* The key each of PadButtons' bits pressed, which its release releases */
static uint32_t	padkey[32];

/* The keys the keyboard holds, 1 each */
static uint8_t	kbheld[256];

/* BindKeyState is TLBA1C.exe 0x8dd380: KeyState as GetBindInput last read it */
static uint8_t	BindKeyState[256];

/* NbBindings, PtrBindCodes and PtrBindActions are TLBA1C.exe 0x8dd488, 0x8dd480 and 0x8dd378 */
static int32_t	NbBindings;
static uint32_t	*PtrBindCodes;
static uint32_t	*PtrBindActions;

/*
 * The virtual key SdlKeyToVk (sdl.c) gives the key of each XT code on a
 * US keyboard, SDL's own keymap, the codes after E0 in the second row
 */
static uint8_t usvk[2][0x80] = {
	{
		[0x01] = 0x1B, [0x02] = '1', [0x03] = '2',
		[0x04] = '3', [0x05] = '4', [0x06] = '5', [0x07] = '6',
		[0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
		[0x0D] = 0xBB, [0x0E] = 0x08, [0x0F] = 0x09,
		[0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
		[0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
		[0x18] = 'O', [0x19] = 'P', [0x1C] = 0x0D,
		[0x1D] = 0xA2, [0x1E] = 'A', [0x1F] = 'S',
		[0x20] = 'D', [0x21] = 'F', [0x22] = 'G', [0x23] = 'H',
		[0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
		[0x27] = 0xBA, [0x28] = 0xC0, [0x29] = 0xDE,
		[0x2A] = 0xA0, [0x2C] = 'Z', [0x2D] = 'X',
		[0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N',
		[0x32] = 'M', [0x33] = 0xBC, [0x36] = 0xA1,
		[0x37] = 0x6A, [0x38] = 0xA4, [0x39] = 0x20,
		[0x3A] = 0x14, [0x3B] = 0x70, [0x3C] = 0x71,
		[0x3D] = 0x72, [0x3E] = 0x73, [0x3F] = 0x74,
		[0x40] = 0x75, [0x41] = 0x76, [0x42] = 0x77,
		[0x43] = 0x78, [0x44] = 0x79, [0x45] = 0x90,
		[0x46] = 0x91, [0x47] = 0x67, [0x48] = 0x68,
		[0x49] = 0x69, [0x4A] = 0x6D, [0x4B] = 0x64,
		[0x4C] = 0x65, [0x4D] = 0x66, [0x4E] = 0x6B,
		[0x4F] = 0x61, [0x50] = 0x62, [0x51] = 0x63,
		[0x52] = 0x60, [0x53] = 0x6E, [0x57] = 0x7A,
		[0x58] = 0x7B,
	},
	{
		[0x1C] = 0x6C, [0x1D] = 0xA3, [0x35] = 0x6F,
		[0x38] = 0xA5, [0x47] = 0x24, [0x48] = 0x26,
		[0x49] = 0x21, [0x4B] = 0x25, [0x4D] = 0x27,
		[0x4F] = 0x23, [0x50] = 0x28, [0x51] = 0x22,
		[0x52] = 0x2D, [0x53] = 0x2E,
	},
};

/* The 1994 game's Fire bits for the Classic's, 0x10 to 0x100: space, return, ctrl, alt and shift */
static uint16_t firebits[] = {1, 2, 4, 8, 32};

static int extended;

/*
 * The virtual key of the keyboard's code c, after E0 if ext, on a US
 * keyboard. With LbaWinSettings's 4 the codes of W and of AZERTY's W
 * trade their keys, as sdl.c trades the codes.
 */
int
usvkey(int c, int ext)
{
	c &= 0x7F;
	if (!ext && (LbaWinSettings & 4)) {
		if (c == 0x11)
			c = 0x2C;
		else if (c == 0x2C)
			c = 0x11;
	}
	return usvk[ext != 0][c];
}

static int	padholds(uint32_t, int);

/*
 * The keyboard's byte c, its key's virtual key vk or -1 for a US
 * keyboard's, into KeyState, as the Classic's ManageEvents (TLBA1C.exe
 * 0x405320) takes SDL's keys: KeyVk the key pressed, 0 at a release.
 */
void
vkcode(int c, int vk)
{
	int ext;

	if (c == 0xE0) {
		extended = 1;
		return;
	}
	ext = extended;
	extended = 0;
	if (vk < 0)
		vk = usvkey(c, ext);
	vkkey(vk, (c & 0x80) == 0);
}

/*
 * The keyboard's key of virtual key vk down or up, as vkcode gives the
 * keys of XT codes and sdl.c those that have none.
 *
 * FIXED: TLBA1C.exe releases at the keyboard's release a key one of the
 * pad's inputs holds, which the pad's buttons never press again while
 * they stay down. A key the pad holds stays held.
 */
void
vkkey(int vk, int down)
{
	if (vk > 0 && vk < 0x100)
		kbheld[vk] = down != 0;
	if (!down) {
		if (padholds(vk, -1))
			KeyVk = 0;
		else
			KeyUp(vk);
		return;
	}
	KeyVk = vk;
	if (vk != 0)
		KeyDown();
}

/*
 * KeyDown is TLBA1C.exe 0x4262c0: KeyVk held.
 *
 * FIXED: TLBA1C.exe writes a code below 0 into the bytes before
 * KeyState. The codes 1 to 0xFF alone hold a byte of KeyState.
 */
void
KeyDown(void)
{
	if (KeyVk > 0 && KeyVk < 0x100)
		KeyState[KeyVk] |= 0x80;
}

/*
 * KeyUp is TLBA1C.exe 0x426320: the key vk released, KeyVk 0.
 *
 * FIXED: TLBA1C.exe clears a code below 0 in the bytes before
 * KeyState. The codes 1 to 0xFF alone clear a byte of KeyState.
 */
void
KeyUp(int32_t vk)
{
	if (vk > 0 && vk < 0x100)
		KeyState[vk] &= 0x7F;
	KeyVk = 0;
}

/*
 * InitBindings is TLBA1C.exe 0x4264c0, LBA2's DefineInputKeys: n
 * bindings, each a key's code in codes and the bits of its inputs in
 * actions, which the Classic gives TabInput and TabInputBit.
 */
void
InitBindings(int32_t n, uint32_t *codes, uint32_t *actions)
{
	NbBindings = n;
	PtrBindCodes = codes;
	PtrBindActions = actions;
}

/*
 * ReadInputs is TLBA1C.exe 0x4264e0 up to its call of MoveInputCursor,
 * which moves the pointer on 2.21's page of its site, LBA2's GetInput:
 * the inputs held, those of norepeat and those it masked before kept
 * out until their release, Joy the first four and Fire the next five,
 * which take the 1994 game's bits.
 *
 * FIXED: TLBA1C.exe reads a code from 0x100 up in the bytes after
 * KeyState, a bit a code from KeyState[0xE0] on. The codes below 0x100
 * alone hold their inputs.
 */
void
ReadInputs(uint32_t norepeat)
{
	uint32_t m, held, c;
	int i;

	m = InputMasked | norepeat;
	held = 0;
	for (i = 0; i < NbBindings; i++) {
		c = PtrBindCodes[i];
		if (c < 0x100 && (KeyState[c] & 0x80))
			held |= PtrBindActions[i];
	}
	Joy = held & 0xF;
	Fire = 0;
	for (i = 0; i < (int)nelem(firebits); i++)
		if (held & 0x10<<i)
			Fire |= firebits[i];
	InputMasked = held & m;
	InputActions = held & ~m;
}

/*
 * The keyboard's code of virtual key vk on a US keyboard, as usvkey
 * gives the code's key, 0x100 added after E0, or 0.
 */
static int
vkxt(uint32_t vk)
{
	int c, ext;

	for (ext = 0; ext < 2; ext++)
		for (c = 1; c < 0x80; c++)
			if (vk != 0 && (uint32_t)usvkey(c, ext) == vk)
				return c | ext<<8;
	return 0;
}

/*
 * The key vk pressed or released by the pad: KeyState's, and the
 * keyboard's code for keyb.c, so that the 1994 game sees the key its
 * tests read.
 */
static void
padkeyin(uint32_t vk, int down)
{
	int c;

	if (down) {
		KeyVk = vk;
		KeyDown();
	} else {
		KeyUp(vk);
	}
	c = vkxt(vk);
	if (c == 0)
		return;
	if (c & 0x100)
		xtcode(0xE0);
	xtcode((c & 0x7F) | (down ? 0 : 0x80));
}

/* SetPadButton is TLBA1C.exe 0x426380: the pad's input n held or not. */
void
SetPadButton(int n, int down)
{
	if (down)
		PadButtons |= 1u<<n;
	else
		PadButtons &= ~(1u<<n);
}

/* TestPadButton is TLBA1C.exe 0x4263a0: whether the pad's input n is held. */
int
TestPadButton(int n)
{
	return (PadButtons & 1u<<n) != 0;
}

/*
 * GetBinding is TLBA1C.exe 0x405c70: the key of the pad's input n in the
 * context.
 *
 * FIXED: TLBA1C.exe tests -1 alone below 0, reading before TabPadMenu
 * and TabPadKey for an input below it. Every input below 0 takes key 0.
 */
uint32_t
GetBinding(int n)
{
	if (n < 0 || n >= Npad)
		return 0;
	if (InputContext != 0)
		return TabPadMenu[n];
	return TabPadKey[n];
}

/* Whether one of the pad's inputs held but n presses the key vk. */
static int
padholds(uint32_t vk, int n)
{
	int i;

	if (vk == 0)
		return 0;
	for (i = 0; i < Npad; i++)
		if (i != n && TestPadButton(i) && padkey[i] == vk)
			return 1;
	return 0;
}

/*
 * The key the pad's input n pressed released, KeyVk 0, or KeyVk that key
 * where another of the pad's inputs presses it.
 *
 * FIXED: TLBA1C.exe releases a key the keyboard holds too, so that a key
 * held on the keyboard and on the pad is seen pressed again at the
 * keyboard's next repeat once the pad lets it go, as play starts or the
 * button rises. A key the keyboard holds stays held.
 */
static void
padrelease(int n)
{
	uint32_t vk;

	vk = padkey[n];
	padkey[n] = 0;
	if (padholds(vk, n) || (vk != 0 && vk < 0x100 && kbheld[vk])) {
		KeyVk = vk;
		return;
	}
	if (vk != 0)
		padkeyin(vk, 0);
	else
		KeyVk = 0;
}

/*
 * PadButton is TLBA1C.exe 0x4051c0: the pad's input n down or up, its
 * key pressed, or released where no other input held presses it.
 *
 * FIXED: TLBA1C.exe releases at an input's release the key its binding
 * gives in the context then, so that a button pressed in one context
 * and released in another leaves its key held and releases another, and
 * a press of an input held, which a stick or a trigger makes at each of
 * its moves, takes the context's key and leaves the one before held. The
 * release takes the key the press pressed, and a press that changes the
 * key releases the one before. TLBA1C.exe sets the bit of an input from
 * 31 up too, which a record can give. PadButton returns at once for
 * such an input.
 */
void
PadButton(int n, int down)
{
	uint32_t vk;
	int held;

	if (n < 0 || n >= Npad)
		return;
	held = TestPadButton(n);
	SetPadButton(n, down);
	if (down) {
		vk = GetBinding(n);
		if (held && padkey[n] != vk)
			padrelease(n);
		padkey[n] = vk;
		LastPadButton = n | Pad;
		if (vk != 0)
			padkeyin(vk, 1);
		else
			KeyVk = 0;
		return;
	}
	LastPadButton = 0;
	padrelease(n);
}

/*
 * ClearInputs is TLBA1C.exe 0x426590, which SetInputContext calls as
 * play starts. sdl.c forgets the pad's codes with them, so that a stick
 * held presses its direction again as it moves, as the Classic's does.
 *
 * FIXED: TLBA1C.exe releases every key, the keyboard's too, so that a
 * key held as a menu or a message closes is seen pressed again at its
 * next repeat, W or Space opening again the message it closed. The
 * pad's inputs alone are released.
 */
void
ClearInputs(void)
{
	int n;

	for (n = 0; n < Npad; n++)
		if (TestPadButton(n))
			PadButton(n, 0);
	sdlpadclear();
}

/*
 * SetInputContext is TLBA1C.exe 0x426770: the pad's keys the menus' or
 * play's, play starting with the pad's inputs released.
 */
void
SetInputContext(int32_t context)
{
	if (context != InputContext && context == 0)
		ClearInputs();
	InputContext = context;
}

/*
 * AnyInput is TLBA1C.exe 0x426650: whether an input, Fire, Joy or a key
 * is held.
 */
int
AnyInput(void)
{
	return InputActions != 0 || Fire != 0 || Joy != 0 || KeyVk != 0;
}

/*
 * GetBindInput is TLBA1C.exe 0x422f50: the input a controls page binds,
 * after a pump: with pad the first of the pad's inputs held, 0x200 and
 * its number, and otherwise Escape, then the highest key pressed since
 * the call before, the mouse's buttons, VK_EXECUTE and the Windows keys
 * left out, or 0, KeyState keeping then that call's presses alone.
 */
int32_t
GetBindInput(int pad)
{
	uint8_t fresh;
	int32_t r;
	int i;

	r = 0;
	idle();
	if (pad) {
		for (i = 0; i < Npad; i++)
			if (TestPadButton(i))
				return i | Pad;
	} else if (KeyVk == 0x1B) {
		return 0x1B;
	}
	for (i = 0xFF; i >= 0; i--) {
		fresh = ~BindKeyState[i] & KeyState[i];
		BindKeyState[i] = KeyState[i];
		KeyState[i] = fresh & 0x80;
		if (i > 4 && i != 0x2B && i != 0x5B && i != 0x5C && r == 0 && (fresh & 0x80))
			r = i;
	}
	return r;
}
