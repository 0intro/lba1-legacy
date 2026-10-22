#include <stddef.h>
#include <stdint.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "dat.h"
#include "fns.h"

/*
 * FEATURE: Alt+F4 and the Windows keys refused, LbaWinSettings's 0x40
 * and 0x80. On every system altf4 keeps F4 pressed while an Alt key is
 * down from the game, where the window manager leaves the keys to it.
 * On Windows a hook of the keyboard, which Windows calls before any
 * program sees a key, refuses F4 pressed with Alt and, while the
 * screen fills the display, the Windows keys, in every program while
 * the game's window has the focus, and sdl.c turns SDL's own close on
 * Alt+F4 off. Taken from the Classic 3.2.3, as LbaWin 0.8.1 made them.
 *
 * FIXED: the Classic installs its hook for the whole run, so that
 * Alt+F4 is refused in every program while the game runs, and the
 * Windows keys while it fills the display, and it decides each event
 * alone: F4 pressed, then Alt, then F4 released loses the release,
 * which leaves F4 down for good. The hook is in place while the window
 * has the focus, as SDL places its own, and a key's release goes as its
 * first press went, and so do its repeats once that press was refused.
 */

enum
{
	Vkf4 = 0x73,	/* Windows' virtual keys: F4 */
	Vklwin = 0x5B,	/* the left Windows key */
	Vkrwin = 0x5C,	/* the right one */
	Altdown = 0x20,	/* the hook's flags: LLKHF_ALTDOWN, Alt down */
	Keyup = 0x80,	/* LLKHF_UP, a release */
	Alt = 0x38,	/* the keyboard's codes: Alt, E0 first for the right one */
	F4 = 0x3E,
	Extended = 0xE0,
	Release = 0x80,	/* a release's bit */
};

static int	held;	/* the keys the hook met pressed, by bit */
static int	refused;	/* those of them whose first press it refused */
static int	altdown;	/* the Alt keys down, as altf4 reads them: 1 the left, 2 the right */
static int	altext;	/* altf4's last code an extended key's E0 */
static int	f4held;	/* F4 down: 1 given to the game, 2 kept from it */

/* refusealtf4: whether F4 pressed with Alt is refused, LbaWinSettings's 0x40. */
int
refusealtf4(void)
{
	return (LbaWinSettings & 0x40) != 0;
}

/* refusewinkeys: whether the Windows keys are refused on the whole display, LbaWinSettings's 0x80. */
int
refusewinkeys(void)
{
	return (LbaWinSettings & 0x80) != 0;
}

/* The bit of a key the hook may refuse, 0 for the others. */
static int
refusable(uint32_t vk)
{
	switch (vk) {
	case Vkf4:
		return 1;
	case Vklwin:
		return 2;
	case Vkrwin:
		return 4;
	}
	return 0;
}

/*
 * hookkey: the hook's answer to the event of Windows' virtual key vk
 * with the hook's flags, 1 to refuse it. A key's first press is refused
 * as the settings say, and its release goes as that press went, and so
 * do its repeats once the press was refused. A repeat of a passed press
 * takes the settings' test, so that a refused one costs that repeat
 * alone.
 */
int
hookkey(uint32_t vk, uint32_t flags)
{
	int bit, refuse;

	bit = refusable(vk);
	if (bit == 0)
		return 0;
	if (vk == Vkf4)
		refuse = refusealtf4() && (flags & Altdown) != 0;
	else
		refuse = refusewinkeys() && FlagFullScreen != 0;
	if (flags & Keyup) {
		refuse = (refused & bit) != 0;
		held &= ~bit;
		refused &= ~bit;
	} else if (held & bit) {
		if (refused & bit)
			refuse = 1;
	} else {
		held |= bit;
		if (refuse)
			refused |= bit;
	}
	return refuse;
}

/*
 * altf4: whether the keyboard's code c is F4's, kept from the game. F4
 * pressed while an Alt key is down is kept with refusealtf4, and its
 * repeats, its BIOS word and its release go as its first press went.
 * It reads the codes after the record, Alt's among them, so that a
 * replay keeps F4 out as the run did.
 */
int
altf4(int c)
{
	int s, bit, kept;

	if (c == Extended) {
		altext = 1;
		return 0;
	}
	s = c & ~Release;
	if (s == Alt) {
		bit = altext ? 2 : 1;
		if (c & Release)
			altdown &= ~bit;
		else
			altdown |= bit;
	}
	altext = 0;
	if (s != F4)
		return 0;
	if (c & Release) {
		kept = f4held == 2;
		f4held = 0;
		return kept;
	}
	if (f4held == 0)
		f4held = refusealtf4() && altdown != 0 ? 2 : 1;
	return f4held == 2;
}

/* altf4word: whether the BIOS's word w is F4's, kept from the game with its key. */
int
altf4word(int w)
{
	return f4held == 2 && (w>>8) == F4;
}

#ifdef _WIN32
/* HookKeyboard is TLBA1C.exe 0x8dd4f4 (LbaWin.exe 0x43dd44): the hook in place, NULL for none */
static HHOOK	HookKeyboard;

/* KeyboardHookProc is TLBA1C.exe 0x427610 (LbaWin.exe 0x4049c0): a key's event refused (1), or passed on. */
static LRESULT CALLBACK
KeyboardHookProc(int code, WPARAM w, LPARAM l)
{
	KBDLLHOOKSTRUCT *k;

	k = (KBDLLHOOKSTRUCT*)l;
	if (code == HC_ACTION && hookkey(k->vkCode, k->flags))
		return 1;
	return CallNextHookEx(HookKeyboard, code, w, l);
}

/*
 * hookkeys(on) puts the hook in place with a key on, outside a replay,
 * whose keys come from its record, or takes it away: sdl.c's, as the
 * window takes the focus and loses it, as SDL places its own.
 */
void
hookkeys(int on)
{
	if (on && HookKeyboard == NULL && (refusealtf4() || refusewinkeys()) && !playing()) {
		held = 0;
		refused = 0;
		HookKeyboard = SetWindowsHookExA(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandleA(NULL), 0);
	} else if (!on && HookKeyboard != NULL) {
		UnhookWindowsHookEx(HookKeyboard);
		HookKeyboard = NULL;
	}
}
#else
/* hookkeys(on): the window manager owns Alt+F4 and the Super keys here. */
void
hookkeys(int on)
{
	USED(on);
}
#endif
