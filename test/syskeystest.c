#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "../sys/dat.h"
#include "../sys/fns.h"

/*
 * syskeystest: sys/syskeys.c's answers to Alt+F4 and the Windows keys.
 *
 *	syskeystest
 *
 * hookkey, the hook's decision, meets the events Windows gives a
 * low-level hook, in order from the hook's install, each with the
 * settings and the screen of its case: Alt+F4 and F4 alone, each
 * Windows key on the whole display and in a window, each bit clear,
 * the Apps key and Z, and the repeats and releases that go as their
 * key's first press went. altf4 and altf4word, the game's filter, meet
 * sequences of the keyboard's codes and the BIOS's words, each from
 * every key up to every key up again.
 */

enum
{
	Vkf4 = 0x73,	/* Windows' virtual keys */
	Vklwin = 0x5B,
	Vkrwin = 0x5C,
	Vkapps = 0x5D,
	Vkz = 0x5A,
	Ext = 0x01,	/* the hook's flags: LLKHF_EXTENDED */
	Altdown = 0x20,	/* LLKHF_ALTDOWN */
	Up = 0x80,	/* LLKHF_UP */
};

typedef struct Hookcase Hookcase;
typedef struct Code Code;
typedef struct Seq Seq;

/* an event for the hook, the settings and the screen it meets, its answer */
struct Hookcase
{
	uint32_t	vk;
	uint32_t	flags;
	int	full;	/* the screen on the whole display */
	int	altf4;	/* LbaWinSettings's 0x40 */
	int	winkeys;	/* its 0x80 */
	int	want;	/* 1 refused */
	char	*what;
};

static Hookcase hookcases[] = {
	{Vkf4, Altdown, 1, 1, 1, 1, "Alt+F4 pressed"},
	{Vkf4, Altdown|Up, 1, 1, 1, 1, "its release"},
	{Vkf4, 0, 1, 1, 1, 0, "F4 pressed alone"},
	{Vkf4, Altdown|Up, 1, 1, 1, 0, "its release with Alt down"},
	{Vkf4, Altdown, 1, 0, 1, 0, "Alt+F4 pressed, 0x40 clear"},
	{Vkf4, Altdown|Up, 1, 0, 1, 0, "its release"},
	{Vklwin, Ext, 1, 1, 1, 1, "the left Windows key on the whole display"},
	{Vklwin, Ext|Up, 1, 1, 1, 1, "its release"},
	{Vkrwin, Ext, 1, 1, 1, 1, "the right Windows key on the whole display"},
	{Vkrwin, Ext|Up, 0, 1, 1, 1, "its release in a window"},
	{Vklwin, Ext, 0, 1, 1, 0, "the left Windows key in a window"},
	{Vklwin, Ext|Up, 1, 1, 1, 0, "its release on the whole display"},
	{Vklwin, Ext, 1, 1, 0, 0, "the left Windows key on the whole display, 0x80 clear"},
	{Vklwin, Ext|Up, 1, 1, 0, 0, "its release"},
	{Vkapps, Ext, 1, 1, 1, 0, "the Apps key on the whole display"},
	{Vkz, 0, 1, 1, 1, 0, "Z"},
	{Vkf4, 0, 1, 1, 1, 0, "F4 pressed alone"},
	{Vkf4, Altdown, 1, 1, 1, 1, "its repeat with Alt down"},
	{Vkf4, Altdown|Up, 1, 1, 1, 0, "its release with Alt down"},
	{Vkf4, Altdown, 1, 1, 1, 1, "Alt+F4 pressed"},
	{Vkf4, 0, 1, 1, 1, 1, "its repeat with Alt released"},
	{Vkf4, Up, 1, 1, 1, 1, "its release with Alt released"},
	{Vklwin, Ext, 0, 1, 1, 0, "the left Windows key in a window"},
	{Vklwin, Ext, 1, 1, 1, 1, "its repeat on the whole display"},
	{Vklwin, Ext|Up, 1, 1, 1, 0, "its release on the whole display"},
	{Vkf4, Altdown|Up, 1, 1, 1, 0, "a release whose press came before the hook"},
};

/* a code for altf4 ('k') or a word for altf4word ('b'), and whether it is kept from the game */
struct Code
{
	int	kind;
	int	val;
	int	kept;
};

enum
{
	Ncode = 16,
};

/* a sequence of codes and words with 0x40 as altf4 says, ended by {0} */
struct Seq
{
	int	altf4;
	char	*what;
	Code	codes[Ncode];
};

static Seq seqs[] = {
	{0, "Alt+F4, 0x40 clear", {
		{'k', 0x38, 0}, {'k', 0x3E, 0}, {'b', 0x3E00, 0}, {'k', 0xBE, 0}, {'k', 0xB8, 0}, {0}}},
	{1, "the left Alt and F4, a repeat", {
		{'k', 0x38, 0}, {'k', 0x3E, 1}, {'b', 0x3E00, 1}, {'k', 0x3E, 1}, {'b', 0x3E00, 1},
		{'k', 0xBE, 1}, {'k', 0xB8, 0}, {0}}},
	{1, "the right Alt and F4, a repeat once Alt is up", {
		{'k', 0xE0, 0}, {'k', 0x38, 0}, {'k', 0x3E, 1}, {'b', 0x3E00, 1}, {'k', 0xE0, 0},
		{'k', 0xB8, 0}, {'k', 0x3E, 1}, {'b', 0x3E00, 1}, {'k', 0xBE, 1}, {0}}},
	{1, "F4, then Alt and a repeat", {
		{'k', 0x3E, 0}, {'b', 0x3E00, 0}, {'k', 0x38, 0}, {'k', 0x3E, 0}, {'b', 0x3E00, 0},
		{'k', 0xBE, 0}, {'k', 0xB8, 0}, {0}}},
	{1, "Alt let go before F4", {
		{'k', 0x38, 0}, {'k', 0xB8, 0}, {'k', 0x3E, 0}, {'b', 0x3E00, 0}, {'k', 0xBE, 0}, {0}}},
	{1, "Alt and F2", {
		{'k', 0x38, 0}, {'k', 0x3C, 0}, {'b', 0x3C00, 0}, {'k', 0xBC, 0}, {'k', 0xB8, 0}, {0}}},
	{1, "both Alts, the left let go, then F4", {
		{'k', 0x38, 0}, {'k', 0xE0, 0}, {'k', 0x38, 0}, {'k', 0xB8, 0}, {'k', 0x3E, 1},
		{'b', 0x3E00, 1}, {'k', 0xBE, 1}, {'k', 0xE0, 0}, {'k', 0xB8, 0}, {0}}},
	{1, "an extended key, then the left Alt and F4", {
		{'k', 0xE0, 0}, {'k', 0x48, 0}, {'k', 0xE0, 0}, {'k', 0xC8, 0}, {'k', 0x38, 0},
		{'k', 0x3E, 1}, {'k', 0xBE, 1}, {'k', 0xB8, 0}, {0}}},
	{1, "F4's word alone", {
		{'b', 0x3E00, 0}, {0}}},
};

static int fails;

int
playing(void)
{
	return 0;
}

int32_t	FlagFullScreen;
int32_t	LbaWinSettings;

int
main(void)
{
	Hookcase *h;
	Seq *s;
	Code *c;
	size_t i;
	int got;

	for (i = 0; i < nelem(hookcases); i++) {
		h = &hookcases[i];
		FlagFullScreen = h->full;
		LbaWinSettings = (h->altf4 ? 0x40 : 0) | (h->winkeys ? 0x80 : 0);
		got = hookkey(h->vk, h->flags);
		if (got != h->want) {
			fprintf(stderr, "syskeystest: hook case %d, %s: %s, want %s\n", (int)i + 1, h->what,
				got ? "refused" : "passed", h->want ? "refused" : "passed");
			fails++;
		}
	}
	for (s = seqs; s < seqs + nelem(seqs); s++) {
		LbaWinSettings = s->altf4 ? 0x40 : 0;
		for (c = s->codes; c->kind != 0; c++) {
			got = c->kind == 'k' ? altf4(c->val) : altf4word(c->val);
			if (got != c->kept) {
				fprintf(stderr, "syskeystest: %s: %c %x %s, want %s\n", s->what, c->kind, (unsigned)c->val,
					got ? "kept" : "given", c->kept ? "kept" : "given");
				fails++;
			}
		}
	}
	if (fails > 0)
		return 1;
	printf("syskeystest: ok\n");
	return 0;
}
