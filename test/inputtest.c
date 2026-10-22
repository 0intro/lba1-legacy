#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sys/compat.h"
#include "../lib386/lib_sys/adeline.h"
#include "../lib386/lib_sys/lib_sys.h"

#undef main	/* compat.h's, which names the game's lbamain */

/*
 * inputtest: the Classic's input as sys/input.c and sources/input.c
 * take it, behind GamePad.
 *
 *	inputtest prefdir
 *
 * The virtual key of XT codes on a US keyboard, with LbaWinSettings's 4
 * too, each key once. The keyboard's bytes held in KeyState and
 * KeyVk, a record's virtual key in the US keyboard's place. The inputs
 * ReadInputs reads through the default bindings, each of their 40 keys
 * alone and two bound to none, Joy and Fire in the 1994 game's bits, two keys of one input,
 * an input kept out until its release, and a code from 0x100 up. Then
 * the bindings InitInput reads from an lba.cfg written in prefdir, the
 * keys the pad presses and the pad's bindings made again from them. Then
 * the tests of the 1994 keys in the Classic's inputs' place, GamePad off
 * and on: TestKeyEsc, KeyInput, MyInput, ComportementInput and
 * ComportementHeld, the previous and the next behaviour of each, and the
 * zoom's and the pause's waits.
 * Then the pad: its inputs pressed and released, two inputs on one key,
 * a release in the other context, a stick's halves and a trigger,
 * ClearInputs keeping the keyboard's keys, and the bytes the pad gives
 * the 1994 keyboard, with LbaWinSettings's 4 too. Then the name grid's
 * keys, GridInput's: a held key's repeats, the BIOS's arrows, the
 * keyboard's Return and the pad's, Backspace and a letter, and LBA2's
 * name editor's, EditInput's: the pad's Return, Escape and Backspace
 * once a press, the keyboard's words first, F10 as Escape. Then the
 * controls pages' GetBindInput, the bindings SaveInput writes into
 * lba.cfg and InitInput reads back, and DefaultInput's and
 * DefaultGInput's.
 */

void	setdirs(char*, char**, int, char**, int);
int	usvkey(int, int);
void	vkcode(int, int);
void	KeyDown(void);
void	KeyUp(int32_t);
void	vkkey(int, int);
void	InitInput(void);
int32_t	TestKeyEsc(void);
int32_t	KeyInput(int32_t, uint32_t);
int32_t	MyInput(int32_t, uint32_t);
int32_t	ComportementInput(uint32_t);
int32_t	ComportementHeld(void);
int32_t	StepComportement(uint32_t);
int32_t	KeyHeld(void);
int32_t	PauseWait(void);
int32_t	GridInput(int32_t, int32_t*, uint32_t*);
int32_t	EditInput(int32_t, int32_t*);
int32_t	GetBindInput(int);
void	DefaultInput(void);
void	DefaultGInput(void);
void	SaveInput(void);
void	PadButton(int, int);
void	ClearInputs(void);
void	SetInputContext(int32_t);

extern uint8_t	KeyState[];
extern int32_t	KeyVk;
extern uint32_t	TabInput[];
extern uint32_t	TabGInput[];
extern uint32_t	PadButtons;
extern int32_t	LastPadButton;

char	PathConfigFile[260];	/* sys/sys.c's, which inputtest leaves out */
volatile UWORD	Key;		/* sys/keyb.c's */
volatile UWORD	Joy;
volatile UWORD	Fire;
volatile ULONG	InputActions;
WORD	MyKey;			/* the main loop's, sources/global.c's */
ULONG	MyActions;
WORD	Comportement;		/* sources/global.c's */
volatile ULONG	TimerRef;	/* sys/timer.c's */

enum
{
	Ninput = 27,
	Npad = 31,
	Pad = 0x200,
	Menus = 1<<10,
	Holomap = 1<<11,
	Normal = 1<<13,
	Discret = 1<<16,
	Pause = 1<<12,
	Previous = 1<<25,
	Next = 1<<26,
};

/* A behaviour, an input of the previous or the next, and the behaviour it steps to */
static struct {
	int	comportement;
	uint32_t	input;
	int	want;
} steps[] = {
	{0, Previous, 3}, {1, Previous, 0}, {3, Previous, 2}, {4, Previous, 0},
	{0, Next, 1}, {2, Next, 3}, {3, Next, 0}, {4, Next, 0},
};

/*
 * With GamePad off and on, the 1994 Key, MyKey and the inputs held, and
 * what TestKeyEsc, KeyInput(K_H, Holomap), MyInput(K_H, Holomap),
 * ComportementInput, ComportementHeld give
 */
static struct {
	int	pad;
	int	key, mykey;
	uint32_t	actions;
	int	esc, keyh, myh, comp, held;
} tests[] = {
	{0, 0, 0, 0, 0, 0, 0, -1, 0},
	{0, K_ESC, 0, 0, 1, 0, 0, -1, 0},
	{0, 0, 0, Menus, 0, 0, 0, -1, 0},		/* Menus, an input alone */
	{0, K_H, K_H, Holomap, 0, 1, 1, -1, 0},
	{0, K_F1, 0, 0, 0, 0, 0, -1, 1},
	{0, K_F4, 0, Normal, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 0, 0, -1, 0},
	{1, K_ESC, 0, 0, 1, 0, 0, -1, 0},		/* Escape stays the key */
	{1, 0, 0, Menus, 1, 0, 0, -1, 0},
	{1, K_H, K_H, 0, 0, 0, 0, -1, 0},		/* H unbound */
	{1, 0, 0, Holomap, 0, 1, 1, -1, 0},
	{1, K_F1, 0, 0, 0, 0, 0, -1, 0},
	{1, 0, 0, Normal|Discret, 0, 0, 0, 0, 1},	/* the first behaviour */
	{1, 0, 0, Discret, 0, 0, 0, 3, 1},
	{1, 0, 0, Previous, 0, 0, 0, -1, 1},
};

/*
 * With GamePad off and on, the 1994 Key, KeyVk, the inputs held, Joy and
 * Fire, and what KeyHeld and PauseWait give
 */
static struct {
	int	pad;
	int	key, vk;
	uint32_t	actions;
	int	joy, fire;
	int	held, wait;
} waits[] = {
	{0, 0, 0, 0, 0, 0, 0, 1},
	{0, K_P, 0, 0, 0, 0, 1, 0},		/* the 1994 P */
	{0, 0, 0x50, 0, 0, 0, 0, 1},		/* a virtual key alone */
	{0, 0, 0, 0, 1, 0, 0, 0},		/* an arrow */
	{0, 0, 0, 0, 0, 1, 0, 0},		/* a fire key */
	{1, 0, 0, 0, 0, 0, 0, 1},
	{1, K_P, 0, 0, 0, 0, 0, 0},		/* the 1994 Key ends it, KeyVk held none */
	{1, 0, 0x21, 0, 0, 0, 1, 1},		/* Page Up held, bound to none */
	{1, 0, 0x21, Pause, 0, 0, 1, 0},	/* Page Up bound to Pause ends it */
	{1, 0, 0, 0, 1, 0, 0, 0},		/* an arrow */
	{1, 0, 0, 0, 0, 1, 0, 0},		/* a fire key */
};

static char *pref;
static int fails;

/* An XT code, after E0 if ext, and its virtual key on a US keyboard, with LbaWinSettings's 4 if set4 */
static struct {
	int	c, ext, set4, vk;
} codes[] = {
	{0x11, 0, 0, 'W'},
	{0x2C, 0, 0, 'Z'},
	{0x11, 0, 1, 'Z'},	/* the key in W's place gives AZERTY's W code its key */
	{0x2C, 0, 1, 'W'},
	{0x10, 0, 1, 'Q'},
	{0x48, 0, 0, 0x68},	/* keypad 8 */
	{0x48, 1, 0, 0x26},	/* up */
	{0x48, 1, 1, 0x26},
	{0x1C, 0, 0, 0x0D},
	{0x1C, 1, 0, 0x6C},	/* the keypad's Enter */
	{0x1D, 1, 0, 0xA3},
	{0x53, 1, 0, 0x2E},	/* Delete */
	{0x53, 0, 0, 0x6E},	/* the keypad's point */
	{0x4F, 1, 0, 0x23},	/* End */
	{0x28, 0, 0, 0xC0},	/* apostrophe, SdlKeyToVk's scan code */
	{0x29, 0, 0, 0xDE},	/* grave */
	{0x0C, 0, 0, 0},	/* minus */
	{0x56, 0, 0, 0},	/* the key beside left Shift */
	{0xAC, 0, 0, 'Z'},	/* Z's release */
};

/* A key's virtual key, the bit of its input in InputActions, and its Joy and Fire */
static struct {
	int	vk;
	uint32_t	actions;
	int	joy, fire;
} keys[] = {
	{0x26, 1<<0, 1, 0}, {0x68, 1<<0, 1, 0},
	{0x28, 1<<1, 2, 0}, {0x62, 1<<1, 2, 0},
	{0x25, 1<<2, 4, 0}, {0x64, 1<<2, 4, 0},
	{0x27, 1<<3, 8, 0}, {0x66, 1<<3, 8, 0},
	{0x20, 1<<4, 0, 1}, {0x65, 1<<4, 0, 1},		/* Space, keypad 5 */
	{0x0D, 1<<5, 0, 2}, {0x6C, 1<<5, 0, 2},		/* Return */
	{0xA2, 1<<6, 0, 4}, {0xA3, 1<<6, 0, 4},		/* Ctrl */
	{0xA4, 1<<7, 0, 8}, {0xA5, 1<<7, 0, 8},		/* Alt */
	{0xA0, 1<<8, 0, 32}, {0xA1, 1<<8, 0, 32},	/* Shift */
	{'W', 1<<9, 0, 0}, {0x23, 1<<9, 0, 0},
	{0x79, 1<<10, 0, 0},
	{'H', 1<<11, 0, 0}, {0x09, 1<<11, 0, 0},
	{'P', 1<<12, 0, 0},
	{0x70, 1<<13, 0, 0}, {0x71, 1<<14, 0, 0}, {0x72, 1<<15, 0, 0},
	{0x73, 1<<16, 0, 0}, {0x74, 1<<17, 0, 0}, {0x75, 1<<18, 0, 0},
	{'1', 1<<19, 0, 0}, {'2', 1<<20, 0, 0},
	{'M', 1<<21, 0, 0}, {'J', 1<<22, 0, 0}, {'G', 1<<23, 0, 0},
	{'C', 1<<24, 0, 0},
	{'9', 1<<25, 0, 0}, {0x67, 1<<25, 0, 0},
	{'0', 1<<26, 0, 0}, {0x69, 1<<26, 0, 0},
	{0x2E, 0, 0, 0},	/* Delete, F_SUPPR in the 1994 game */
	{0x61, 0, 0, 0},	/* keypad 1, a corner */
};

/* The keys each of the pad's inputs presses with the default bindings */
static uint32_t padkeys[Npad] = {
	'W', 0x0D, 0x20, 0xA0, 'H', 0, 0x79, 0,
	0, 0xA2, 0xA4, 'J', 'M', 'G', 'C', 0,
	0, 0, 0, 0, 0, 0x25, 0x27, 0,
	0, 0x25, 0x27, '9', '0', 0x28, 0x26,
};

/* The pad's bindings, two an input, as DefTabGInput holds them */
static uint32_t padbinds[2*Ninput] = {
	Pad|30, 0, Pad|29, 0, Pad|21, Pad|25, Pad|22, Pad|26,
	Pad|2, 0, Pad|1, 0, Pad|9, 0, Pad|10, 0,
	Pad|3, 0, Pad|0, 0, Pad|6, 0, Pad|4, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, Pad|12, 0, Pad|11, 0, Pad|13, 0,
	Pad|14, 0, Pad|27, 0, Pad|28, 0,
};

/* The keyboard's bytes the pad gave, which xtcode, keyb.c's, takes */
static int xt[16], nxt;

void
xtcode(int c)
{
	if (nxt < (int)(sizeof xt / sizeof xt[0]))
		xt[nxt++] = c;
}

/* idle is sys/timer.c's, the clock left as it is here. */
void
idle(void)
{
}

/* padin is sys/replay.c's: the pad's input given to the Classic's input. */
void
padin(int n, int down)
{
	PadButton(n, down);
}

/* sdlpadclear is sys/sdl.c's, whose pad inputtest leaves out. */
void
sdlpadclear(void)
{
}

/* fatal is sys/fns.h's, as sys/sdl.c has it. */
void
fatal(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "inputtest: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	exit(1);
}

static void
put(char *name, char *text)
{
	char path[1024];
	FILE *f;

	snprintf(path, sizeof path, "%s/%s", pref, name);
	f = fopen(path, "wb");
	if (f == NULL || fwrite(text, 1, strlen(text), f) != strlen(text) || fclose(f) != 0)
		fatal("cannot write %s", path);
}

static void
code(int i)
{
	LbaWinSettings = codes[i].set4 ? 4 : 0;
	if (usvkey(codes[i].c, codes[i].ext) == codes[i].vk)
		return;
	fprintf(stderr, "inputtest: code %s%02x with 4 %s: %#x, want %#x\n", codes[i].ext ? "e0 " : "",
		codes[i].c, codes[i].set4 ? "set" : "clear", usvkey(codes[i].c, codes[i].ext), codes[i].vk);
	fails++;
}

/* Each virtual key given by one XT code alone. */
static void
unique(void)
{
	int seen[256], c, ext, vk;

	LbaWinSettings = 0;
	memset(seen, 0, sizeof seen);
	for (ext = 0; ext < 2; ext++)
		for (c = 0; c < 0x80; c++) {
			vk = usvkey(c, ext);
			if (vk == 0)
				continue;
			if (seen[vk]) {
				fprintf(stderr, "inputtest: virtual key %#x given twice\n", vk);
				fails++;
			}
			seen[vk] = 1;
		}
}

static void
held(char *what, int vk, int wantstate, int wantkey)
{
	if ((KeyState[vk] & 0x80) == wantstate && KeyVk == wantkey)
		return;
	fprintf(stderr, "inputtest: %s: KeyState[%#x] %#x, KeyVk %#x, want %#x and %#x\n", what, vk,
		KeyState[vk], KeyVk, wantstate, wantkey);
	fails++;
}

/*
 * The keyboard's bytes into KeyState: E0, a release, a record's virtual
 * key, and a key of no XT code by its virtual key alone.
 */
static void
bytes(void)
{
	LbaWinSettings = 0;
	vkcode(0xE0, -1);
	vkcode(0x48, -1);
	held("up", 0x26, 0x80, 0x26);
	vkcode(0x48, -1);
	held("keypad 8 after up", 0x68, 0x80, 0x68);
	vkcode(0xE0, -1);
	vkcode(0xC8, -1);
	held("up released", 0x26, 0, 0);
	vkcode(0xC8, -1);
	held("keypad 8 released", 0x68, 0, 0);
	vkcode(0x10, 'A');
	held("A in Q's place", 'A', 0x80, 'A');
	held("Q in Q's place", 'Q', 0, 'A');
	vkcode(0x90, 'A');
	held("A released", 'A', 0, 0);
	vkcode(0x0C, -1);
	held("minus", 0, 0, 0);
	vkkey(0x2C, 1);	/* Print Screen, a key of no XT code */
	held("Print Screen", 0x2C, 0x80, 0x2C);
	vkkey(0x2C, 0);
	held("Print Screen released", 0x2C, 0, 0);
	memset(KeyState, 0, 256);
}

static void
inputs(char *what, uint32_t wantactions, int wantjoy, int wantfire)
{
	if (InputActions == wantactions && Joy == wantjoy && Fire == wantfire)
		return;
	fprintf(stderr, "inputtest: %s: actions %#lx, Joy %#x, Fire %#x, want %#lx, %#x and %#x\n", what,
		(unsigned long)InputActions, Joy, Fire, (unsigned long)wantactions, wantjoy, wantfire);
	fails++;
}

static void
key(int i)
{
	char what[64];

	snprintf(what, sizeof what, "key %#x", keys[i].vk);
	KeyState[keys[i].vk] = 0x80;
	ReadInputs(0);
	inputs(what, keys[i].actions, keys[i].joy, keys[i].fire);
	KeyState[keys[i].vk] = 0;
	ReadInputs(0);
	inputs(what, 0, 0, 0);
}

/* Two keys of one input, an input kept out until its release, a code from 0x100 up. */
static void
reads(void)
{
	KeyState[0x26] = 0x80;
	KeyState[0x68] = 0x80;
	ReadInputs(0);
	inputs("up and keypad 8", 1, 1, 0);
	KeyState[0x26] = 0;
	ReadInputs(0);
	inputs("keypad 8 left", 1, 1, 0);
	KeyState[0x68] = 0;
	KeyState['H'] = 0x80;
	ReadInputs(1<<11);
	inputs("H kept out", 0, 0, 0);
	ReadInputs(0);
	inputs("H still kept out", 0, 0, 0);
	KeyState['H'] = 0;
	ReadInputs(0);
	KeyState['H'] = 0x80;
	ReadInputs(0);
	inputs("H again", 1<<11, 0, 0);
	KeyState['H'] = 0;
	TabInput[0] = 0x1FF;
	TabInput[1] = 0x1FF;
	memset(KeyState, 0x80, 256);
	ReadInputs(0);
	if (InputActions & 1) {
		fprintf(stderr, "inputtest: code 0x1ff held\n");
		fails++;
	}
	TabInput[0] = 0x26;
	TabInput[1] = 0x68;
	memset(KeyState, 0, 256);
	ReadInputs(0);
}

static void
tables(char *what, uint32_t *t, uint32_t *want, int n)
{
	int i;

	for (i = 0; i < n; i++)
		if (t[i] != want[i]) {
			fprintf(stderr, "inputtest: %s[%d] %#lx, want %#lx\n", what, i, (unsigned long)t[i],
				(unsigned long)want[i]);
			fails++;
			return;
		}
}

static void
padheld(char *what, int vk, int wantstate, int wantkey, uint32_t wantbuttons)
{
	if ((KeyState[vk] & 0x80) == wantstate && KeyVk == wantkey && PadButtons == wantbuttons)
		return;
	fprintf(stderr, "inputtest: %s: KeyState[%#x] %#x, KeyVk %#x, PadButtons %#lx, want %#x, %#x and %#lx\n",
		what, vk, KeyState[vk], KeyVk, (unsigned long)PadButtons, wantstate, wantkey,
		(unsigned long)wantbuttons);
	fails++;
}

static void
bytesgiven(char *what, int n, int b0, int b1)
{
	if (nxt == n && (n < 1 || xt[0] == b0) && (n < 2 || xt[1] == b1)) {
		nxt = 0;
		return;
	}
	fprintf(stderr, "inputtest: %s: %d bytes %#x %#x, want %d, %#x %#x\n", what, nxt, xt[0], xt[1], n, b0, b1);
	nxt = 0;
	fails++;
}

/*
 * The pad's inputs with the default bindings: a press and a release,
 * the 1994 keyboard's bytes, two inputs on one key, a release in the
 * other context, a stick's halves and a trigger, ClearInputs, a
 * held input pressed again in the other context, a key the keyboard and
 * the pad hold, and the codes KeyDown and KeyUp leave alone, which
 * UBSan's bounds hold.
 */
static void
pad(void)
{
	FlagGamePad = 1;
	LbaWinSettings = 0;
	SetInputContext(0);
	nxt = 0;
	PadButton(2, 1);	/* X: Space in play */
	padheld("X in play", 0x20, 0x80, 0x20, 1u<<2);
	bytesgiven("X's press", 1, 0x39, 0);
	if (LastPadButton != (0x200|2)) {
		fprintf(stderr, "inputtest: LastPadButton %#lx, want 0x202\n", (unsigned long)LastPadButton);
		fails++;
	}
	PadButton(2, 0);
	padheld("X released", 0x20, 0, 0, 0);
	bytesgiven("X's release", 1, 0x39|0x80, 0);
	PadButton(21, 1);	/* the left stick left and the right one: Left in play */
	PadButton(25, 1);
	PadButton(21, 0);
	padheld("one of two on Left", 0x25, 0x80, 0x25, 1u<<25);
	PadButton(25, 0);
	padheld("both off Left", 0x25, 0, 0, 0);
	bytesgiven("Left pressed twice, released once", 6, 0xE0, 0x4B);
	SetInputContext(1);
	PadButton(1, 1);	/* B: Escape in the menus */
	padheld("B in the menus", 0x1B, 0x80, 0x1B, 1u<<1);
	bytesgiven("B's press", 1, 0x01, 0);
	SetInputContext(0);
	padheld("play cleared the pad", 0x1B, 0, 0, 0);
	bytesgiven("B's release", 1, 0x81, 0);
	PadButton(1, 1);	/* B: Return in play */
	SetInputContext(1);
	PadButton(1, 0);	/* released in the menus: Return, which its press pressed */
	padheld("B pressed in play, released in the menus", 0x0D, 0, 0, 0);
	padheld("Escape left alone", 0x1B, 0, 0, 0);
	bytesgiven("Return in and out", 2, 0x1C, 0x9C);
	SetInputContext(0);
	PadButton(22, 1);	/* the left stick right, as sdl.c's padaxis gives it */
	padheld("the stick right", 0x27, 0x80, 0x27, 1u<<22);
	PadButton(22, 0);	/* then left */
	PadButton(21, 1);
	padheld("the stick left", 0x25, 0x80, 0x25, 1u<<21);
	padheld("Right released", 0x27, 0, 0x25, 1u<<21);
	PadButton(21, 0);	/* then in the middle */
	padheld("the stick in the middle", 0x25, 0, 0, 0);
	PadButton(30, 1);	/* the right trigger */
	padheld("the right trigger", 0x26, 0x80, 0x26, 1u<<30);
	PadButton(30, 0);
	padheld("the trigger let go", 0x26, 0, 0, 0);
	nxt = 0;
	PadButton(0, 1);	/* A: W in play, which 4 gives AZERTY's W code */
	LbaWinSettings = 4;
	bytesgiven("A's W", 1, 0x11, 0);
	PadButton(3, 1);	/* Y: Shift */
	ClearInputs();
	padheld("ClearInputs", 0xA0, 0, 0, 0);
	padheld("ClearInputs, W", 'W', 0, 0, 0);
	nxt = 0;
	PadButton(0, 1);
	bytesgiven("A's W with 4", 1, 0x2C, 0);
	PadButton(0, 0);
	LbaWinSettings = 0;
	nxt = 0;
	KeyState['K'] = 0x80;	/* the keyboard's keys stay held */
	PadButton(2, 1);
	ClearInputs();
	padheld("the keyboard's K kept", 'K', 0x80, 0, 0);
	KeyState['K'] = 0;
	SetInputContext(0);
	nxt = 0;
	PadButton(27, 1);	/* the right stick up: 9 in play */
	SetInputContext(1);
	PadButton(27, 1);	/* moved in a menu, still past 10000: Up there */
	padheld("the stick moved in the menus", '9', 0, 0x26, 1u<<27);
	padheld("its Up", 0x26, 0x80, 0x26, 1u<<27);
	bytesgiven("9 in and out", 4, 0x0A, 0x8A);
	PadButton(27, 0);
	SetInputContext(0);
	padheld("back in play", '9', 0, 0, 0);
	padheld("back in play, Up", 0x26, 0, 0, 0);
	SetInputContext(1);
	vkcode(0x1C, -1);	/* the keyboard's Return, the pad's A's in the menus */
	nxt = 0;
	PadButton(0, 1);
	PadButton(0, 0);
	padheld("A let go, the keyboard's Return held", 0x0D, 0x80, 0x0D, 0);
	PadButton(0, 1);
	vkcode(0x9C, -1);
	padheld("the keyboard's let go, A's held", 0x0D, 0x80, 0, 1u<<0);
	SetInputContext(0);
	padheld("both let go", 0x0D, 0, 0, 0);
	bytesgiven("Return pressed twice, released once", 3, 0x1C, 0x1C);
	KeyVk = 0x100;	/* codes past KeyState's 256 bytes and before them */
	KeyDown();
	KeyVk = -1;
	KeyDown();
	KeyUp(0x100);
	KeyUp(-1);
	nxt = 0;
	FlagGamePad = 0;
}

/* A tick of TimerRef, KeyVk, GetAscii's word and what GridInput gives */
static struct {
	uint32_t	timer;
	int32_t	vk, car, want;
} grids[] = {
	{100, 0x27, 0, A_RIGHT},		/* Right pressed: at once */
	{100, 0x27, 0, 0},
	{110, 0x27, 0, 0},			/* 10 ticks on, not past */
	{111, 0x27, 0, A_RIGHT},		/* 11 on */
	{113, 0x27, A_RIGHT, 0},		/* the BIOS's arrow, KeyVk's alone */
	{114, 0x27, 0, 0},
	{115, 0x27, 0, A_RIGHT},		/* every 4 */
	{116, 0, 0, 0},			/* released */
	{116, 0x26, 0, A_UP},
	{117, 0, A_LEFT, 0},
	{118, 0x0D, 0, A_SPACE},		/* the pad's Return: the letter */
	{119, 0, 0, 0},
	{120, 0x0D, A_RETURN, A_RETURN},	/* the keyboard's: the name */
	{121, 0, 0, 0},
	{122, 0x08, 0, A_BACKSPACE},
	{123, 0, 'a', 'a'},			/* a letter typed */
	{124, 0x20, A_SPACE, A_SPACE},
};

/* LastPadButton, KeyVk, the inputs held, GetAscii's word and what EditInput gives */
static struct {
	int32_t	pad, vk;
	uint32_t	actions;
	int32_t	car, want;
} edits[] = {
	{0, 0, 0, 0, 0},
	{0, 0, 0, 'a', 'a'},			/* the keyboard's letter */
	{0, 0x0D, 0, A_RETURN, A_RETURN},	/* the keyboard's Return, its word */
	{0, 0x0D, 0, 0, 0},			/* held, the keyboard's, no word */
	{0, 0, 0, 0, 0},
	{Pad|0, 0x0D, 0, 0, A_RETURN},		/* the pad's A: Return */
	{Pad|0, 0x0D, 0, 0, 0},			/* held: once a press */
	{0, 0, 0, 0, 0},
	{Pad|1, 0x1B, 0, 0, A_ESC},		/* B: Escape */
	{0, 0, 0, 0, 0},
	{Pad|2, 0x08, 0, 0, A_BACKSPACE},	/* X: Backspace */
	{Pad|2, 0x08, 0, 'b', 'b'},
	{0, 0, 0, 0, 0},
	{0, 0x0D, 0, 0, 0},			/* the keyboard's Return, its word taken */
	{0, 0, 0, 0, 0},
	{Pad|2, 0x08, 0, 'b', 'b'},		/* a word goes first, */
	{Pad|2, 0x08, 0, 0, A_BACKSPACE},	/* the pad's key at the next call */
	{0, 0, 0, 0, 0},
	{0, 0x79, Menus, 0, A_ESC},		/* F10, the Menus input: Escape */
	{0, 0x79, Menus, 0, 0},
};

static void
edit(void)
{
	int32_t last, got;
	int i;

	last = 0;
	for (i = 0; i < (int)(sizeof edits / sizeof edits[0]); i++) {
		LastPadButton = edits[i].pad;
		KeyVk = edits[i].vk;
		InputActions = edits[i].actions;
		got = EditInput(edits[i].car, &last);
		if (got != edits[i].want) {
			fprintf(stderr, "inputtest: edit %d, pad %#lx, key %#lx, inputs %#lx, word %#lx: %#lx, want %#lx\n",
				i, (unsigned long)edits[i].pad, (unsigned long)edits[i].vk,
				(unsigned long)edits[i].actions, (unsigned long)edits[i].car,
				(unsigned long)got, (unsigned long)edits[i].want);
			fails++;
		}
	}
	LastPadButton = 0;
	KeyVk = 0;
	InputActions = 0;
}

static void
grid(void)
{
	int32_t last;
	uint32_t timer;
	int i, got;

	last = 0;
	timer = 0;
	for (i = 0; i < (int)(sizeof grids / sizeof grids[0]); i++) {
		TimerRef = grids[i].timer;
		KeyVk = grids[i].vk;
		got = GridInput(grids[i].car, &last, &timer);
		if (got != grids[i].want) {
			fprintf(stderr, "inputtest: grid at %lu, key %#lx, word %#lx: %#x, want %#lx\n",
				(unsigned long)grids[i].timer, (unsigned long)grids[i].vk, (unsigned long)grids[i].car,
				got, (unsigned long)grids[i].want);
			fails++;
		}
	}
	KeyVk = 0;
	TimerRef = 0;
}

static void
bind(char *what, int pad, int32_t want)
{
	int32_t got;

	got = GetBindInput(pad);
	if (got == want)
		return;
	fprintf(stderr, "inputtest: GetBindInput %s: %#lx, want %#lx\n", what, (long)got, (long)want);
	fails++;
}

/*
 * GetBindInput: the pad's first input held, Escape, the highest key
 * pressed since the call before, KeyState keeping that call's presses
 * alone, and the keys it leaves out. For the pad, Escape is a key
 * pressed, as any other.
 */
static void
binds(void)
{
	memset(KeyState, 0, 256);
	GetBindInput(0);
	KeyVk = 0;
	PadButtons = 1u<<5 | 1u<<9;
	bind("the pad's 5 and 9", 1, 0x205);
	PadButtons = 1;
	bind("the pad's 0", 1, 0x200);
	PadButtons = 1u<<30;
	bind("the pad's 30", 1, 0x21E);
	PadButtons = 0;
	bind("no key", 0, 0);
	KeyVk = 0x1B;
	bind("Escape's KeyVk for the pad", 1, 0);
	KeyState[0x1B] = 0x80;
	bind("Escape for the pad", 1, 0x1B);
	KeyState['B'] = 0x80;
	bind("B for the pad", 1, 'B');
	KeyState[0x1B] = 0;
	KeyState['B'] = 0;
	GetBindInput(0);
	KeyVk = 0x1B;
	bind("Escape", 0, 0x1B);
	KeyVk = 0;
	KeyState['A'] = 0x80;
	KeyState['Z'] = 0x80;
	bind("A and Z", 0, 'Z');
	if (KeyState['A'] != 0x80) {
		fprintf(stderr, "inputtest: GetBindInput: KeyState['A'] %#x, want 0x80\n", KeyState['A']);
		fails++;
	}
	bind("A and Z held", 0, 0);
	if (KeyState['A'] != 0) {
		fprintf(stderr, "inputtest: GetBindInput held: KeyState['A'] %#x, want 0\n", KeyState['A']);
		fails++;
	}
	KeyState[0x04] = 0x80;
	KeyState[0x2B] = 0x80;
	KeyState[0x5B] = 0x80;
	KeyState[0x5C] = 0x80;
	bind("the mouse, VK_EXECUTE and the Windows keys", 0, 0);
	KeyState[0x05] = 0x80;
	bind("key 5", 0, 0x05);
	memset(KeyState, 0, 256);
	GetBindInput(0);
}

/* Whether tab holds want, else a failure printed for what. */
static void
same(char *what, uint32_t *tab, uint32_t *want)
{
	int i;

	for (i = 0; i < 2*Ninput; i++)
		if (tab[i] != want[i]) {
			fprintf(stderr, "inputtest: %s: binding %d %#lx, want %#lx\n", what, i, (unsigned long)tab[i],
				(unsigned long)want[i]);
			fails++;
			return;
		}
}

/*
 * The bindings written into lba.cfg by SaveInput, every one changed and
 * read back by InitInput, each of the pad's inputs bound once so that
 * SetInputBits binds it again where it was, then DefaultInput's and
 * DefaultGInput's, the bindings InitInput reads from an lba.cfg of the
 * language alone.
 */
static void
roundtrip(void)
{
	uint32_t def[2*Ninput], gdef[2*Ninput], keys[2*Ninput], pads[2*Ninput];
	int i;

	put("LBA.CFG", "Language: English\r\n");
	InitInput();
	memcpy(def, TabInput, sizeof def);
	memcpy(gdef, TabGInput, sizeof gdef);
	for (i = 0; i < Ninput; i++) {
		keys[2*i] = 0x30 + 2*i;
		keys[2*i + 1] = i == 1 ? 0 : 0x31 + 2*i;
		pads[2*i] = Pad | i;
		pads[2*i + 1] = Ninput + i < Npad ? Pad | (Ninput + i) : 0;
	}
	memcpy(TabInput, keys, sizeof keys);
	memcpy(TabGInput, pads, sizeof pads);
	SaveInput();
	memcpy(TabInput, def, sizeof def);
	memcpy(TabGInput, gdef, sizeof gdef);
	InitInput();
	same("the keys read back", TabInput, keys);
	same("the pad read back", TabGInput, pads);
	DefaultInput();
	DefaultGInput();
	same("the keys' defaults", TabInput, def);
	same("the pad's defaults", TabGInput, gdef);
	if (def[0] != 0x26 || def[1] != 0x68 || gdef[2*11] != (Pad|4)) {
		fprintf(stderr, "inputtest: the defaults: %#lx %#lx %#lx, want 0x26 0x68 0x204\n", (unsigned long)def[0],
			(unsigned long)def[1], (unsigned long)gdef[2*11]);
		fails++;
	}
}

/* The tests of the 1994 keys in the Classic's inputs' place. */
static void
test(int i)
{
	FlagGamePad = tests[i].pad;
	Key = tests[i].key;
	MyKey = tests[i].mykey;
	InputActions = tests[i].actions;
	MyActions = tests[i].actions;
	if (TestKeyEsc() == tests[i].esc && KeyInput(K_H, Holomap) == tests[i].keyh
	&& MyInput(K_H, Holomap) == tests[i].myh && ComportementInput(MyActions) == tests[i].comp
	&& ComportementHeld() == tests[i].held)
		return;
	fprintf(stderr, "inputtest: GamePad %d, Key %#x, MyKey %#x, inputs %#lx: %d %d %d %d %d, want %d %d %d %d %d\n",
		tests[i].pad, tests[i].key, tests[i].mykey, (unsigned long)tests[i].actions, TestKeyEsc(),
		KeyInput(K_H, Holomap), MyInput(K_H, Holomap), ComportementInput(MyActions), ComportementHeld(),
		tests[i].esc, tests[i].keyh, tests[i].myh, tests[i].comp, tests[i].held);
	fails++;
}

/* KeyHeld and PauseWait, the zoom's and the pause's waits. */
static void
pausewait(int i)
{
	FlagGamePad = waits[i].pad;
	Key = waits[i].key;
	KeyVk = waits[i].vk;
	InputActions = waits[i].actions;
	Joy = waits[i].joy;
	Fire = waits[i].fire;
	if (KeyHeld() == waits[i].held && PauseWait() == waits[i].wait)
		return;
	fprintf(stderr, "inputtest: GamePad %d, Key %#x, KeyVk %#x, inputs %#lx, Joy %d, Fire %d: %d %d, want %d %d\n",
		waits[i].pad, waits[i].key, waits[i].vk, (unsigned long)waits[i].actions, waits[i].joy,
		waits[i].fire, KeyHeld(), PauseWait(), waits[i].held, waits[i].wait);
	fails++;
}

int
main(int argc, char **argv)
{
	uint32_t want[Npad];
	int i;

	if (argc != 2) {
		fprintf(stderr, "usage: inputtest prefdir\n");
		return 2;
	}
	pref = argv[1];
	setdirs(pref, &pref, 1, &pref, 1);
	snprintf(PathConfigFile, sizeof PathConfigFile, "LBA.CFG");

	for (i = 0; i < (int)(sizeof codes / sizeof codes[0]); i++)
		code(i);
	unique();
	bytes();

	put("LBA.CFG", "Language: English\r\n");
	InitInput();
	tables("TabPadKey", TabPadKey, padkeys, Npad);
	tables("TabGInput", TabGInput, padbinds, 2*Ninput);
	for (i = 0; i < (int)(sizeof keys / sizeof keys[0]); i++)
		key(i);
	reads();

	/* H's input on K alone, the pad's B (1) also on W's input, Return's keeping it */
	put("LBA.CFG", "Input11_1: 75\r\nInput11_2: 0\r\nGInput9_2: 513\r\n");
	InitInput();
	KeyState['K'] = 0x80;
	ReadInputs(0);
	inputs("K for H", 1<<11, 0, 0);
	KeyState['K'] = 0;
	KeyState['H'] = 0x80;
	KeyState[0x09] = 0x80;
	ReadInputs(0);
	inputs("H and Tab unbound", 0, 0, 0);
	KeyState['H'] = 0;
	KeyState[0x09] = 0;
	ReadInputs(0);
	memcpy(want, padkeys, sizeof want);
	want[4] = 'K';	/* Back, bound to H's input, presses its K */
	tables("TabPadKey", TabPadKey, want, Npad);
	tables("TabGInput", TabGInput, padbinds, 2*Ninput);

	for (i = 0; i < (int)(sizeof tests / sizeof tests[0]); i++)
		test(i);
	for (i = 0; i < (int)(sizeof waits / sizeof waits[0]); i++)
		pausewait(i);
	FlagGamePad = 0;
	pad();
	grid();
	edit();
	binds();
	roundtrip();
	for (i = 0; i < (int)(sizeof steps / sizeof steps[0]); i++) {
		Comportement = steps[i].comportement;
		if (StepComportement(steps[i].input) != steps[i].want) {
			fprintf(stderr, "inputtest: behaviour %d stepped by %#lx: %d, want %d\n", steps[i].comportement,
				(unsigned long)steps[i].input, StepComportement(steps[i].input), steps[i].want);
			fails++;
		}
	}

	if (fails != 0)
		return 1;
	printf("inputtest: ok\n");
	return 0;
}
