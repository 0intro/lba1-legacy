#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "dat.h"
#include "fns.h"

/*
 * SDL3, which no other file knows: the window and what it shows, the
 * keyboard's events, the clock, and the start of the program.
 */

enum
{
	Ext = 0x100,	/* an extended key: E0 before its code */
	Kw = 0x2C,	/* W on AZERTY, the game's K_W */
	Winh = 2*480,	/* the window's first height at most: the game's 480 lines twice */
	Quiet = 16,	/* a sample of the music within this of 0 is silence: -66 dB */
	Drift = 5000,	/* the most steer nudges a stream's rate, in millionths */
	Deadzone = 10000,	/* a stick's or a trigger's, the Classic's */
	Firstaxis = 21,	/* the code of the left stick's left, the axes' first */
};

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static uint32_t *pixels;
static uint8_t *lastvram;	/* the video memory, the DAC and the mode last shown */
static uint8_t lastdac[256][3];
static int lastmode;
static int stale = 1;
static int dummy;	/* SDL's dummy video, where nothing shows */
static int winw, winh;	/* the window's first size */
static int recentre;	/* the window to be centred again as it leaves the whole display */
static int dispw, disph, dispset;	/* the display setdisplay gave, -D's or a record's */
static uint64_t dispframe;	/* ns, the display's frame setrefresh gave, 0 unknown */
static int frameset;
static int framegiven;	/* -R gave dispframe, which the presents keep to too */
static int vsyncwant;	/* the refreshes a present waits for, as sdlvsync was last asked */
static int vsynclast;	/* the renderer's vsync, as made or as last asked */
static int vsyncsaid;	/* the renderer's refusal said */
static int textinput;
static int lastscan;
static uint32_t padcodes;	/* the gamepad's codes down, a bit each */
static SDL_AtomicInt polled;
static SDL_AtomicInt ending;	/* the program's end begun */
static SDL_TimerID watchid;	/* the watchdog's timer */
static SDL_Mutex *wlock;	/* files.c's writer's lock, and its condition */
static SDL_Condition *wcond;
static SDL_Thread *wthread;

/*
 * The organisation and the application that name the preferences'
 * folder SDL makes: Adeline\Little Big Adventure in the user's AppData
 * on Windows, lba1 in XDG_DATA_HOME or ~/.local/share elsewhere, SDL
 * leaving out an empty organisation.
 */
#ifdef _WIN32
static const char preforg[] = "Adeline";
static const char prefapp[] = "Little Big Adventure";
#else
static const char preforg[] = "";
static const char prefapp[] = "lba1";
#endif

/*
 * The screen's scale modes: SDL's linear filter, as the Classic 3.2.3
 * gives its texture (TLBA1C.exe 0x406b70), where lba.cfg's
 * SmoothScaling is ON, the default (sdlfilter), and with OFF SDL 3.4's
 * pixel art mode, which draws each pixel the same size at any scale and
 * blends the line between two, where the headers name it, the nearest
 * pixel otherwise. scale takes the nearest pixel too from a library
 * older than 3.4, which a build on 3.4's headers may run on.
 */
#if SDL_VERSION_ATLEAST(3, 4, 0)
static const SDL_ScaleMode pixelart = SDL_SCALEMODE_PIXELART;
#else
static const SDL_ScaleMode pixelart = SDL_SCALEMODE_NEAREST;
#endif
static int smooth = 1;	/* the screen scaled through the linear filter */

/* SDL's scan codes, USB's, to the PC keyboard's (XT set 1) */
static const uint16_t xt[SDL_SCANCODE_COUNT] = {
	[SDL_SCANCODE_A] = 0x1E, [SDL_SCANCODE_B] = 0x30, [SDL_SCANCODE_C] = 0x2E,
	[SDL_SCANCODE_D] = 0x20, [SDL_SCANCODE_E] = 0x12, [SDL_SCANCODE_F] = 0x21,
	[SDL_SCANCODE_G] = 0x22, [SDL_SCANCODE_H] = 0x23, [SDL_SCANCODE_I] = 0x17,
	[SDL_SCANCODE_J] = 0x24, [SDL_SCANCODE_K] = 0x25, [SDL_SCANCODE_L] = 0x26,
	[SDL_SCANCODE_M] = 0x32, [SDL_SCANCODE_N] = 0x31, [SDL_SCANCODE_O] = 0x18,
	[SDL_SCANCODE_P] = 0x19, [SDL_SCANCODE_Q] = 0x10, [SDL_SCANCODE_R] = 0x13,
	[SDL_SCANCODE_S] = 0x1F, [SDL_SCANCODE_T] = 0x14, [SDL_SCANCODE_U] = 0x16,
	[SDL_SCANCODE_V] = 0x2F, [SDL_SCANCODE_W] = 0x11, [SDL_SCANCODE_X] = 0x2D,
	[SDL_SCANCODE_Y] = 0x15, [SDL_SCANCODE_Z] = 0x2C,
	[SDL_SCANCODE_1] = 0x02, [SDL_SCANCODE_2] = 0x03, [SDL_SCANCODE_3] = 0x04,
	[SDL_SCANCODE_4] = 0x05, [SDL_SCANCODE_5] = 0x06, [SDL_SCANCODE_6] = 0x07,
	[SDL_SCANCODE_7] = 0x08, [SDL_SCANCODE_8] = 0x09, [SDL_SCANCODE_9] = 0x0A,
	[SDL_SCANCODE_0] = 0x0B,
	[SDL_SCANCODE_RETURN] = 0x1C, [SDL_SCANCODE_ESCAPE] = 0x01,
	[SDL_SCANCODE_BACKSPACE] = 0x0E, [SDL_SCANCODE_TAB] = 0x0F,
	[SDL_SCANCODE_SPACE] = 0x39, [SDL_SCANCODE_MINUS] = 0x0C,
	[SDL_SCANCODE_EQUALS] = 0x0D, [SDL_SCANCODE_LEFTBRACKET] = 0x1A,
	[SDL_SCANCODE_RIGHTBRACKET] = 0x1B, [SDL_SCANCODE_BACKSLASH] = 0x2B,
	[SDL_SCANCODE_NONUSHASH] = 0x2B, [SDL_SCANCODE_SEMICOLON] = 0x27,
	[SDL_SCANCODE_APOSTROPHE] = 0x28, [SDL_SCANCODE_GRAVE] = 0x29,
	[SDL_SCANCODE_COMMA] = 0x33, [SDL_SCANCODE_PERIOD] = 0x34,
	[SDL_SCANCODE_SLASH] = 0x35, [SDL_SCANCODE_CAPSLOCK] = 0x3A,
	[SDL_SCANCODE_F1] = 0x3B, [SDL_SCANCODE_F2] = 0x3C, [SDL_SCANCODE_F3] = 0x3D,
	[SDL_SCANCODE_F4] = 0x3E, [SDL_SCANCODE_F5] = 0x3F, [SDL_SCANCODE_F6] = 0x40,
	[SDL_SCANCODE_F7] = 0x41, [SDL_SCANCODE_F8] = 0x42, [SDL_SCANCODE_F9] = 0x43,
	[SDL_SCANCODE_F10] = 0x44, [SDL_SCANCODE_F11] = 0x57, [SDL_SCANCODE_F12] = 0x58,
	[SDL_SCANCODE_SCROLLLOCK] = 0x46,
	[SDL_SCANCODE_INSERT] = Ext|0x52, [SDL_SCANCODE_HOME] = Ext|0x47,
	[SDL_SCANCODE_PAGEUP] = Ext|0x49, [SDL_SCANCODE_DELETE] = Ext|0x53,
	[SDL_SCANCODE_END] = Ext|0x4F, [SDL_SCANCODE_PAGEDOWN] = Ext|0x51,
	[SDL_SCANCODE_RIGHT] = Ext|0x4D, [SDL_SCANCODE_LEFT] = Ext|0x4B,
	[SDL_SCANCODE_DOWN] = Ext|0x50, [SDL_SCANCODE_UP] = Ext|0x48,
	[SDL_SCANCODE_NUMLOCKCLEAR] = 0x45, [SDL_SCANCODE_KP_DIVIDE] = Ext|0x35,
	[SDL_SCANCODE_KP_MULTIPLY] = 0x37, [SDL_SCANCODE_KP_MINUS] = 0x4A,
	[SDL_SCANCODE_KP_PLUS] = 0x4E, [SDL_SCANCODE_KP_ENTER] = Ext|0x1C,
	[SDL_SCANCODE_KP_1] = 0x4F, [SDL_SCANCODE_KP_2] = 0x50, [SDL_SCANCODE_KP_3] = 0x51,
	[SDL_SCANCODE_KP_4] = 0x4B, [SDL_SCANCODE_KP_5] = 0x4C, [SDL_SCANCODE_KP_6] = 0x4D,
	[SDL_SCANCODE_KP_7] = 0x47, [SDL_SCANCODE_KP_8] = 0x48, [SDL_SCANCODE_KP_9] = 0x49,
	[SDL_SCANCODE_KP_0] = 0x52, [SDL_SCANCODE_KP_PERIOD] = 0x53,
	[SDL_SCANCODE_NONUSBACKSLASH] = 0x56,
	[SDL_SCANCODE_LCTRL] = 0x1D, [SDL_SCANCODE_LSHIFT] = 0x2A,
	[SDL_SCANCODE_LALT] = 0x38, [SDL_SCANCODE_RCTRL] = Ext|0x1D,
	[SDL_SCANCODE_RSHIFT] = 0x36, [SDL_SCANCODE_RALT] = Ext|0x38,
};

/*
 * SDL's keys and the virtual keys the Classic gives them, SdlKeyToVk's
 * cases (TLBA1C.exe 0x405ce0), the symbols among them a French
 * keyboard's keys
 */
static struct {
	SDL_Keycode	key;
	uint8_t	vk;
} keyvk[] = {
	{SDLK_BACKSPACE, 0x08}, {SDLK_TAB, 0x09}, {SDLK_RETURN, 0x0D},
	{SDLK_ESCAPE, 0x1B}, {SDLK_SPACE, 0x20}, {SDLK_EXCLAIM, 0x3D},
	{SDLK_DOLLAR, 0x3B}, {SDLK_RIGHTPAREN, 0xBD}, {SDLK_ASTERISK, 0x3F},
	{SDLK_COMMA, 0xBC}, {SDLK_0, 0x30}, {SDLK_1, 0x31}, {SDLK_2, 0x32},
	{SDLK_3, 0x33}, {SDLK_4, 0x34}, {SDLK_5, 0x35}, {SDLK_6, 0x36},
	{SDLK_7, 0x37}, {SDLK_8, 0x38}, {SDLK_9, 0x39}, {SDLK_COLON, 0x3E},
	{SDLK_SEMICOLON, 0xBA}, {SDLK_LESS, 0x3C}, {SDLK_EQUALS, 0xBB},
	{SDLK_CARET, 0x40}, {SDLK_A, 0x41}, {SDLK_B, 0x42}, {SDLK_C, 0x43},
	{SDLK_D, 0x44}, {SDLK_E, 0x45}, {SDLK_F, 0x46}, {SDLK_G, 0x47},
	{SDLK_H, 0x48}, {SDLK_I, 0x49}, {SDLK_J, 0x4A}, {SDLK_K, 0x4B},
	{SDLK_L, 0x4C}, {SDLK_M, 0x4D}, {SDLK_N, 0x4E}, {SDLK_O, 0x4F},
	{SDLK_P, 0x50}, {SDLK_Q, 0x51}, {SDLK_R, 0x52}, {SDLK_S, 0x53},
	{SDLK_T, 0x54}, {SDLK_U, 0x55}, {SDLK_V, 0x56}, {SDLK_W, 0x57},
	{SDLK_X, 0x58}, {SDLK_Y, 0x59}, {SDLK_Z, 0x5A}, {SDLK_DELETE, 0x2E},
	{SDLK_CAPSLOCK, 0x14}, {SDLK_F1, 0x70}, {SDLK_F2, 0x71},
	{SDLK_F3, 0x72}, {SDLK_F4, 0x73}, {SDLK_F5, 0x74}, {SDLK_F6, 0x75},
	{SDLK_F7, 0x76}, {SDLK_F8, 0x77}, {SDLK_F9, 0x78}, {SDLK_F10, 0x79},
	{SDLK_F11, 0x7A}, {SDLK_F12, 0x7B}, {SDLK_PRINTSCREEN, 0x2C},
	{SDLK_SCROLLLOCK, 0x91}, {SDLK_PAUSE, 0x13}, {SDLK_INSERT, 0x2D},
	{SDLK_HOME, 0x24}, {SDLK_PAGEUP, 0x21}, {SDLK_END, 0x23},
	{SDLK_PAGEDOWN, 0x22}, {SDLK_RIGHT, 0x27}, {SDLK_LEFT, 0x25},
	{SDLK_DOWN, 0x28}, {SDLK_UP, 0x26}, {SDLK_NUMLOCKCLEAR, 0x90},
	{SDLK_KP_DIVIDE, 0x6F}, {SDLK_KP_MULTIPLY, 0x6A},
	{SDLK_KP_MINUS, 0x6D}, {SDLK_KP_PLUS, 0x6B}, {SDLK_KP_ENTER, 0x6C},
	{SDLK_KP_1, 0x61}, {SDLK_KP_2, 0x62}, {SDLK_KP_3, 0x63},
	{SDLK_KP_4, 0x64}, {SDLK_KP_5, 0x65}, {SDLK_KP_6, 0x66},
	{SDLK_KP_7, 0x67}, {SDLK_KP_8, 0x68}, {SDLK_KP_9, 0x69},
	{SDLK_KP_0, 0x60}, {SDLK_KP_PERIOD, 0x6E}, {SDLK_APPLICATION, 0x5D},
	{SDLK_F13, 0x7C}, {SDLK_F14, 0x7D}, {SDLK_F15, 0x7E},
	{SDLK_F16, 0x7F}, {SDLK_F17, 0x80}, {SDLK_F18, 0x81},
	{SDLK_F19, 0x82}, {SDLK_F20, 0x83}, {SDLK_F21, 0x84},
	{SDLK_F22, 0x85}, {SDLK_F23, 0x86}, {SDLK_F24, 0x87},
	{SDLK_LCTRL, 0xA2}, {SDLK_LSHIFT, 0xA0}, {SDLK_LALT, 0xA4},
	{SDLK_LGUI, 0x5B}, {SDLK_RCTRL, 0xA3}, {SDLK_RSHIFT, 0xA1},
	{SDLK_RALT, 0xA5}, {SDLK_RGUI, 0x5C},
};

/*
 * The BIOS's words for the keys that type no character (its function
 * 0, which GetAscii called): scan code and character, 0 for most.
 */
static const struct {
	uint8_t		scan;
	uint16_t	word;
} biosspecial[] = {
	{0x01, 0x011B}, {0x0E, 0x0E08}, {0x0F, 0x0F09}, {0x1C, 0x1C0D},
	{0x48, 0x4800}, {0x50, 0x5000}, {0x4B, 0x4B00}, {0x4D, 0x4D00},
	{0x47, 0x4700}, {0x4F, 0x4F00}, {0x49, 0x4900}, {0x51, 0x5100},
	{0x52, 0x5200}, {0x53, 0x5300},
	{0x3B, 0x3B00}, {0x3C, 0x3C00}, {0x3D, 0x3D00}, {0x3E, 0x3E00},
	{0x3F, 0x3F00}, {0x40, 0x4000}, {0x41, 0x4100}, {0x42, 0x4200},
	{0x43, 0x4300}, {0x44, 0x4400}, {0x57, 0x8500}, {0x58, 0x8600},
};

/* CP437's upper half, in Unicode */
static const uint16_t cp437[128] = {
	0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
	0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
	0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
	0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
	0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
	0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
	0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
	0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
	0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
	0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
	0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
	0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
	0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
	0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
	0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
};

void
fatal(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "lba: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	exit(1);
}

/*
 * A loop that waits without idle hangs the game, since only pumping moves
 * the clocks and the keys: say so when no events were taken for 2 s. A
 * replay waits on nothing outside it, so that such a loop never ends
 * there: after 10 s it is ended, a failure. The program's end, where the
 * writer puts the last files on the disk without a pump, has 60 s.
 */
static Uint32 SDLCALL
watchdog(void *arg, SDL_TimerID id, Uint32 interval)
{
	static int warned;
	unsigned t, p;

	USED(arg);
	USED(id);
	/* the last poll read before the clock, which cannot then be the earlier */
	p = (unsigned)SDL_GetAtomicInt(&polled);
	t = (unsigned)SDL_GetTicks() - p;
	if (SDL_GetAtomicInt(&ending)) {
		if (t >= 60000 && playing()) {
			fprintf(stderr, "lba: the replay's end hangs: ended\n");
			_Exit(1);
		}
		return interval;
	}
	if (t < 2000)
		warned = 0;
	else if (!warned) {
		fprintf(stderr, "lba: no events taken for %u ms: a loop waits without idle\n", t);
		warned = 1;
	}
	if (t >= 10000 && playing()) {
		fprintf(stderr, "lba: the replay hangs: ended\n");
		_Exit(1);
	}
	return interval;
}

/*
 * The watchdog started once the start has bound the game's files and
 * opened the record and the replay, which it reads from its thread, its
 * clock from now.
 */
static void
startwatch(void)
{
	SDL_SetAtomicInt(&polled, (int)SDL_GetTicks());
	watchid = SDL_AddTimer(500, watchdog, NULL);
}

/* portexit is the C library's exit for the port: the program's end, which the watchdog lets run. */
void
portexit(int status)
{
	SDL_SetAtomicInt(&ending, 1);
	exit(status);
}

/* The writer's lock, for files.c: its calls on SDL's mutex and condition. */
static void
writehook(int op)
{
	switch (op) {
	case Wlock:
		SDL_LockMutex(wlock);
		break;
	case Wunlock:
		SDL_UnlockMutex(wlock);
		break;
	case Wwait:
		SDL_WaitCondition(wcond, wlock);
		break;
	case Wwake:
		SDL_BroadcastCondition(wcond);
		break;
	default:
		fprintf(stderr, "lba: the writer's lock called with %d\n", op);
		break;
	}
}

static int SDLCALL
writing(void *arg)
{
	USED(arg);
	writer();
	return 0;
}

/*
 * The writer's last jobs done and its thread ended, before SDL_Quit and
 * at exit, under the watchdog's watch of the program's end, which then
 * stops.
 */
static void
endwriter(void)
{
	SDL_SetAtomicInt(&ending, 1);
	if (wthread != NULL) {
		endwrites();
		SDL_WaitThread(wthread, NULL);
		wthread = NULL;
		setwriter(NULL);
		SDL_DestroyCondition(wcond);
		SDL_DestroyMutex(wlock);
		wcond = NULL;
		wlock = NULL;
	}
	if (watchid != 0) {
		SDL_RemoveTimer(watchid);
		watchid = 0;
	}
}

/*
 * The writer of the preferences' files (files.c), on a thread of its
 * own, so that the disk's waits fall outside the game's, or on SDL's
 * failure to make it the files written at once. Its end at exit comes
 * after the record's (recordinput), registered later.
 */
static void
startwriter(void)
{
	wlock = SDL_CreateMutex();
	wcond = SDL_CreateCondition();
	if (wlock != NULL && wcond != NULL) {
		setwriter(writehook);
		wthread = SDL_CreateThread(writing, "writer", NULL);
		if (wthread != NULL) {
			atexit(endwriter);
			return;
		}
		setwriter(NULL);
	}
	fprintf(stderr, "lba: no writer thread, the files written at once: %s\n", SDL_GetError());
	SDL_DestroyCondition(wcond);
	SDL_DestroyMutex(wlock);
	wcond = NULL;
	wlock = NULL;
}

/* The signals stay timer.c's: SDL would make them a quit event, which a busy loop never reads. */
static void
sdlinit(void)
{
	/* An identifier would become the window's class on X11, in place of the program's file name. */
	SDL_SetAppMetadata("Little Big Adventure", NULL, NULL);
	SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
	if (!SDL_Init(SDL_INIT_VIDEO))
		fatal("SDL_Init: %s", SDL_GetError());
	dummy = SDL_GetCurrentVideoDriver() != NULL && strcmp(SDL_GetCurrentVideoDriver(), "dummy") == 0;
	atexit(sdlclose);
	/*
	 * FEATURE: the window made hidden, before lba.cfg is read, for
	 * sdlfullscreen to show it, on the whole display from the first frame
	 * when FlagFullScreen says so, as the Classic 3.2.3 makes its
	 * window hidden for the whole display.
	 */
	if (!SDL_CreateWindowAndRenderer("Little Big Adventure", 2*Screen_X, 2*Screen_Y,
	    SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN, &window, &renderer))
		fatal("SDL_CreateWindowAndRenderer: %s", SDL_GetError());
	SDL_GetRenderVSync(renderer, &vsynclast);
	SDL_StopTextInput(window);
	startwriter();
}

/* The display DesiredDisplayNum chose, or 0 */
static SDL_DisplayID	wantdisplay;

/*
 * The window's display: the one DesiredDisplayNum chose while the
 * window is hidden, else the one SDL gives it, or the primary.
 */
static SDL_DisplayID
windisplay(void)
{
	SDL_DisplayID d;

	if (wantdisplay != 0 && (SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN) != 0)
		return wantdisplay;
	d = SDL_GetDisplayForWindow(window);
	if (d == 0)
		d = SDL_GetPrimaryDisplay();
	return d;
}

/*
 * FEATURE: the display's size, which the wide screen takes its shape
 * from: the one -D or a record gave (setdisplay), else the desktop's,
 * or 0 by 0 on SDL's error. The port's own.
 */
void
sdldesktop(int *w, int *h)
{
	const SDL_DisplayMode *m;

	if (dispset) {
		*w = dispw;
		*h = disph;
		return;
	}
	*w = *h = 0;
	m = SDL_GetDesktopDisplayMode(windisplay());
	if (m == NULL)
		return;
	*w = m->w;
	*h = m->h;
}

/*
 * FEATURE: the window, hidden, put in the middle of DesiredDisplayNum's
 * display: the nth of SDL's from 0, and the first, SDL's primary, when
 * n lies below 0 or past the last, as the Classic 3.2.3's
 * SDLViewer::ChooseDisplayMode takes display 0 (TLBA1C.exe 0x4072e0).
 * SDL's X11 and Wayland move a hidden window as it shows, and give it
 * the display it was made on until then: wantdisplay keeps the choice
 * for the window's size and place. Taken from the Classic 3.2.3.
 */
void
sdldisplay(void)
{
	SDL_DisplayID *ids, d;
	int i, n;

	ids = SDL_GetDisplays(&n);
	if (ids == NULL)
		return;
	i = DesiredDisplayNum;
	if (i < 0 || i >= n)
		i = 0;
	d = n > 0 ? ids[i] : 0;
	SDL_free(ids);
	if (d == 0)
		return;
	wantdisplay = d;
	SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(d), SDL_WINDOWPOS_CENTERED_DISPLAY(d));
	SDL_SyncWindow(window);
}

/* setdisplay: the first call's w by h taken as the display. */
void
setdisplay(int w, int h)
{
	if (dispset)
		return;
	dispw = w;
	disph = h;
	dispset = 1;
}

/*
 * The frame of the window's display now, in ns, from its mode's rate,
 * 0 for a rate SDL leaves unknown or on its error.
 */
static uint64_t
sdldisplayframe(void)
{
	const SDL_DisplayMode *m;
	uint64_t num, den;

	m = SDL_GetCurrentDisplayMode(windisplay());
	if (m == NULL)
		return 0;
	if (m->refresh_rate_numerator > 0 && m->refresh_rate_denominator > 0) {
		num = (uint64_t)m->refresh_rate_numerator;
		den = (uint64_t)m->refresh_rate_denominator;
		return (1000000000*den + num/2) / num;
	}
	return 0;
}

/*
 * FEATURE: the display's frame the run paces its frames by: the one -R
 * or a record gave (setrefresh), else the window's display's now. The
 * port's own.
 */
uint64_t
sdlrefresh(void)
{
	if (frameset)
		return dispframe;
	return sdldisplayframe();
}

/* setrefresh: the first call's frame, in ns, 0 for unknown, taken as the display's. */
void
setrefresh(uint64_t ns)
{
	if (frameset)
		return;
	dispframe = ns;
	frameset = 1;
}

/*
 * FEATURE: the frame of the display the window shows on, which
 * DisplaySync's vsync follows: -R's, given where the system gives the
 * rate wrong, else the window's display's now. The port's own.
 */
uint64_t
sdlwindowframe(void)
{
	if (framegiven)
		return dispframe;
	return sdldisplayframe();
}

/* The renderer's vsync as vsyncwant says, 0 while the window is hidden, minimized or occluded. */
static void
applyvsync(void)
{
	int n, ok;

	n = vsyncwant;
	if (SDL_GetWindowFlags(window) & (SDL_WINDOW_HIDDEN|SDL_WINDOW_MINIMIZED|SDL_WINDOW_OCCLUDED))
		n = 0;
	if (n == vsynclast)
		return;
	vsynclast = n;
	ok = SDL_SetRenderVSync(renderer, n);
	if (!ok && n > 1)
		ok = SDL_SetRenderVSync(renderer, 1);
	if (!ok && !vsyncsaid) {
		fprintf(stderr, "lba: SDL_SetRenderVSync: %s\n", SDL_GetError());
		vsyncsaid = 1;
	}
}

/*
 * FEATURE: sdlvsync: the presents synchronized with every nth refresh
 * of the display, 0 for none, and 0 while the window is hidden,
 * minimized or occluded, where a present waiting for the display would
 * hold the game's clock back. A renderer refusing n above 1 takes 1.
 * Taken from the Classic 3.2.3, whose renderer presents with vsync.
 */
void
sdlvsync(int n)
{
	vsyncwant = n;
	applyvsync();
}

/*
 * The window's first size for the screen's width: the screen's times
 * the largest whole number the display's usable area takes, the whole
 * display where the window manager gives that area empty, but at most
 * the number that keeps it within Winh lines, or for a screen larger
 * than that area the largest of the screen's shape it takes. The
 * renderer draws the screen at that width. The window's display is
 * given back.
 */
static SDL_DisplayID
place(void)
{
	SDL_DisplayID d;
	SDL_Rect r;
	int k;

	d = windisplay();
	winw = Screen_X;
	winh = Screen_Y;
	if (!SDL_GetDisplayUsableBounds(d, &r) || r.w <= 0 || r.h <= 0)
		if (!SDL_GetDisplayBounds(d, &r))
			r.w = r.h = 0;
	if (r.w > 0 && r.h > 0) {
		k = r.w/Screen_X < r.h/Screen_Y ? r.w/Screen_X : r.h/Screen_Y;
		if (k > Winh/Screen_Y)
			k = Winh/Screen_Y;
		if (k >= 1) {
			winw = k*Screen_X;
			winh = k*Screen_Y;
		} else if (r.w*Screen_Y < r.h*Screen_X) {
			winw = r.w;
			winh = r.w*Screen_Y/Screen_X;
		} else {
			winw = r.h*Screen_X/Screen_Y;
			winh = r.h;
		}
	}
	/*
	 * FEATURE: IntegerScaling ON, the screen scaled by the largest whole
	 * multiple the window takes, black around it, and the window kept as
	 * large as the screen, which SDL crops in a smaller one. The port's
	 * own.
	 */
	if (FlagIntegerScaling) {
		SDL_SetRenderLogicalPresentation(renderer, Screen_X, Screen_Y, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
		SDL_SetWindowMinimumSize(window, Screen_X, Screen_Y);
	} else {
		SDL_SetRenderLogicalPresentation(renderer, Screen_X, Screen_Y, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	}
	return d;
}

/* The texture's scale mode, as smooth says. */
static void
scale(void)
{
	if (smooth)
		SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
	else if (SDL_GetVersion() < SDL_VERSIONNUM(3, 4, 0) || !SDL_SetTextureScaleMode(texture, pixelart))
		SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
}

/* sdlfilter: the screen scaled through the linear filter with on set, sharp otherwise (scale). */
void
sdlfilter(int on)
{
	smooth = on;
	scale();
	stale = 1;
}

/*
 * The screen's texture and what sdlshow keeps, made once for the screen
 * Wide_X wide, the widest the run takes, and the window at its first
 * size, centred on the display as SDL centred the window it made.
 */
void
sdlscreen(void)
{
	SDL_DisplayID d;

	d = place();
	SDL_SetWindowSize(window, winw, winh);
	SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(d), SDL_WINDOWPOS_CENTERED_DISPLAY(d));
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888,
		SDL_TEXTUREACCESS_STREAMING, Wide_X, Screen_Y);
	if (texture == NULL)
		fatal("SDL_CreateTexture: %s", SDL_GetError());
	scale();
	pixels = malloc((size_t)Wide_X*Screen_Y*sizeof pixels[0]);
	lastvram = calloc((size_t)Wide_X*Screen_Y, 1);
	if (pixels == NULL || lastvram == NULL)
		fatal("no memory for a screen of %dx%d", (int)Wide_X, (int)Screen_Y);
}

/*
 * FEATURE: the screen shown at its new width after Widescreen display's
 * choice, and the window at its first size for that width in the
 * display's middle, at once in a window, as it leaves the whole display
 * otherwise. The port's own.
 */
void
sdlsize(void)
{
	SDL_DisplayID d;

	d = place();
	if (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) {
		recentre = 1;
	} else {
		SDL_SetWindowSize(window, winw, winh);
		SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(d), SDL_WINDOWPOS_CENTERED_DISPLAY(d));
	}
	stale = 1;
}

void
sdlclose(void)
{
	hookkeys(0);
	endwriter();
	SDL_Quit();
}

/*
 * FEATURE: the screen on the whole display or in a window, as the
 * option menu's Fullscreen display switches it. Taken from the Classic
 * 3.2.3.
 */

/*
 * FEATURE: sdlfullscreen is TLBA1C.exe 0x407420, SDLViewer::SetFullScreen,
 * in SDL3's calls, as FlagFullScreen says: the window on the whole
 * display at the desktop's mode, or back in a window at its first size,
 * then shown, as the end of CreateSdlWindow (0x405020) shows it, and the
 * screen shown again whole at the next pump. A call that fails is said,
 * and the game goes on, as with the Classic's SdlError. Taken from the
 * Classic 3.2.3.
 *
 * FIXED: the Classic hides the pointer as it makes its window and shows
 * it after a switch back to a window, so that a window lacks it at the
 * start and has it after a switch. The pointer is hidden on the whole
 * display alone, from the start.
 */
void
sdlfullscreen(int on)
{
	if (!SDL_SetWindowFullscreen(window, on))
		fprintf(stderr, "lba: SDL_SetWindowFullscreen: %s\n", SDL_GetError());
	if (on) {
		if (!SDL_HideCursor())
			fprintf(stderr, "lba: SDL_HideCursor: %s\n", SDL_GetError());
	} else {
		if (!SDL_SetWindowSize(window, winw, winh))
			fprintf(stderr, "lba: SDL_SetWindowSize: %s\n", SDL_GetError());
		/*
		 * FEATURE: the window centred again on its display after the
		 * wide screen's switch changed the screen's width on the whole
		 * display. The port's own.
		 */
		if (recentre) {
			SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(SDL_GetDisplayForWindow(window)),
				SDL_WINDOWPOS_CENTERED_DISPLAY(SDL_GetDisplayForWindow(window)));
			recentre = 0;
		}
		if (!SDL_ShowCursor())
			fprintf(stderr, "lba: SDL_ShowCursor: %s\n", SDL_GetError());
	}
	/*
	 * FEATURE: with refusealtf4, SDL's own close on Alt+F4 (Windows')
	 * is off before the window shows, so that where the hook lets
	 * Alt+F4 through, before the window's focus puts it in place, or
	 * under Wine, which gives it F4 without its Alt flag, syskeys.c's
	 * altf4 keeps F4 from the game. The port's own.
	 */
	if (refusealtf4())
		SDL_SetHint(SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4, "0");
	SDL_ShowWindow(window);
	stale = 1;
}

/*
 * The sound's streams, 16-bit, each opened at its first frames and kept
 * a lead ahead of the device, which a pause of the game spends while
 * the device plays on. The clock (timer.c) catches up after a lag of up
 * to its Late, a tenth of a second, handing on at once what the pause
 * held back, and each lead holds that tenth besides what the stream
 * needs to play on time. A longer lag it gives up, and sdllate puts the
 * leads back as silence. The device's clock drifts from ours, and a
 * tune held long without a rest would drain its stream or fill it past
 * its limit: steer nudges the stream's rate, by Drift millionths at
 * most, to hold what waits at the lead.
 */
static uint32_t silence[1024];

/* Silence to stream s until n bytes wait there. */
static void
fill(SDL_AudioStream *s, int n)
{
	int q, m;

	for (q = SDL_GetAudioStreamQueued(s); q < n; q += m) {
		m = n - q < (int)sizeof silence ? n - q : (int)sizeof silence;
		SDL_PutAudioStreamData(s, silence, m);
	}
}

/*
 * The lead of stream s at rate, in bytes, for frames that come only as
 * the clock passes them: the clock's Late, two of the device's pulls, a
 * frame of the screen (what the clock hands on at a retrace) and 5 ms.
 */
static int
leadof(SDL_AudioStream *s, int rate)
{
	SDL_AudioSpec dev;
	int frames;

	if (!SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(s), &dev, &frames)
	|| dev.freq <= 0 || frames <= 0) {
		dev.freq = rate;
		frames = 2048;
	}
	return 4*(rate/10 + (int)((int64_t)2*frames*rate/dev.freq) + rate/60 + rate/200);
}

/* Stream s, which holds n bytes, its rate set to bring them back to lead. */
static void
steer(SDL_AudioStream *s, int n, int lead)
{
	float e;

	e = (float)(n - lead) / lead;
	if (e > 1)
		e = 1;
	else if (e < -1)
		e = -1;
	SDL_SetAudioStreamFrequencyRatio(s, 1 + e*Drift/1000000);
}

/*
 * The samples' halves to the sound card, at rate: a half of silence
 * ahead at the start, as the card's other half was, and the clock's
 * Late, so that the lead is two halves, the one playing and the one
 * being mixed, and that tenth. After a tenth of a second of silent
 * halves (mixhalf's zeros), the lead is held in place of each. A half
 * is dropped when more than two past the lead wait, the clock having
 * run ahead (a replay).
 */
static SDL_AudioStream *wavestream;
static int nowave;
static float wavegain = 1;
static int wavelead;	/* bytes */
static int wavequiet;	/* frames of silence in a row, a tenth of a second at most */

/* Whether n words are all 0. */
static int
zero(const uint32_t *w, int n)
{
	int i;

	for (i = 0; i < n; i++)
		if (w[i] != 0)
			return 0;
	return 1;
}

void
sdlwave(const uint32_t *buf, int frames, int rate)
{
	SDL_AudioSpec spec;
	int q;

	if (nowave)
		return;
	if (wavestream == NULL) {
		spec.format = SDL_AUDIO_S16LE;
		spec.channels = 2;
		spec.freq = rate;
		if (!SDL_InitSubSystem(SDL_INIT_AUDIO)
		|| (wavestream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL)) == NULL) {
			fprintf(stderr, "lba: no sound: %s\n", SDL_GetError());
			nowave = 1;
			return;
		}
		SDL_SetAudioStreamGain(wavestream, wavegain);
		SDL_ResumeAudioStreamDevice(wavestream);
		wavelead = 4*(2*frames + rate/10);
		fill(wavestream, wavelead - frames*4);
	}
	if (!zero(buf, frames))
		wavequiet = 0;
	else if (wavequiet < rate/10)
		wavequiet += frames;
	if (wavequiet >= rate/10) {
		fill(wavestream, wavelead);
		return;
	}
	q = SDL_GetAudioStreamQueued(wavestream);
	if (q > wavelead + 2*frames*4)
		return;
	SDL_PutAudioStreamData(wavestream, buf, frames*4);
	steer(wavestream, q + frames*4, wavelead);
}

/* The mixer chip's gain on the samples. */
void
sdlwavegain(float g)
{
	wavegain = g;
	if (wavestream != NULL)
		SDL_SetAudioStreamGain(wavestream, g);
}

/*
 * The CD's audio, at 44100 Hz, to a stream of its own, opened at the
 * first frame. cd.c hands its frames on Ahead ahead of the clock, and
 * silence before the first frame of each play makes up the rest of the
 * lead. A frame is dropped when more than half a second waits, the clock
 * having run ahead (a replay), and what waits is dropped when the CD
 * stops.
 */
static SDL_AudioStream *cdstream;
/* FEATURE: the PlayStation's track in the XMIDI's place goes to one more such stream, at the CD's gain. Taken from the PlayStation. */
static SDL_AudioStream *psstream;
static int nocd;
static float cdgain = 1;
static int cdlead;	/* bytes */
static int cdstart = 1;	/* the CD's next frame a play's first */
static int psstart = 1;	/* the PlayStation's track's */

/* n bytes of the CD's audio, a frame, to stream *s, opened if need be, *start set where it begins a play. */
static void
cdput(SDL_AudioStream **s, int *start, const void *buf, int n)
{
	SDL_AudioSpec spec;
	int q;

	if (nocd)
		return;
	if (*s == NULL) {
		spec.format = SDL_AUDIO_S16LE;
		spec.channels = 2;
		spec.freq = 44100;
		if (!SDL_InitSubSystem(SDL_INIT_AUDIO)
		|| (*s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL)) == NULL) {
			fprintf(stderr, "lba: no CD audio: %s\n", SDL_GetError());
			nocd = 1;
			return;
		}
		SDL_SetAudioStreamGain(*s, cdgain);
		SDL_ResumeAudioStreamDevice(*s);
		cdlead = leadof(*s, 44100);
	}
	if (*start) {
		fill(*s, cdlead - Ahead*n);
		*start = 0;
	}
	q = SDL_GetAudioStreamQueued(*s);
	if (q > 44100/2*4)
		return;
	SDL_PutAudioStreamData(*s, buf, n);
	steer(*s, q + n, cdlead);
}

void
sdlcd(const void *buf, int n)
{
	cdput(&cdstream, &cdstart, buf, n);
}

void
sdlcdstop(void)
{
	if (cdstream != NULL)
		SDL_ClearAudioStream(cdstream);
	cdstart = 1;
}

void
sdlpstrack(const void *buf, int n)
{
	cdput(&psstream, &psstart, buf, n);
}

void
sdlpstrackstop(void)
{
	if (psstream != NULL)
		SDL_ClearAudioStream(psstream);
	psstart = 1;
}

/* The mixer chip's gain on the CD, the PlayStation's tracks' too. */
void
sdlcdgain(float g)
{
	cdgain = g;
	if (cdstream != NULL)
		SDL_SetAudioStreamGain(cdstream, g);
	if (psstream != NULL)
		SDL_SetAudioStreamGain(psstream, g);
}

/*
 * The music's chip, at its rate (the OPL3's 49716 Hz, the EMU8000's
 * 44100), to a stream of its own. Its frames come only as the clock
 * passes them, so that its lead (leadof) is silence put at the start. A
 * chunk all within Quiet of 0 is silence (the EMU8000 gives 0 there,
 * Nuked OPL3 a dozen below). After a tenth of a second of it the lead
 * is held in place of the chunk, so that a rest within a tune keeps its
 * time. A chunk is dropped when more than half a second waits, the
 * clock having run ahead (a replay).
 */
static SDL_AudioStream *midistream;
static int nomidi;
static float midigain = 1;
static int midilead;	/* bytes */
static int midiquiet;	/* frames of silence in a row, a tenth of a second at most */

/* Whether n samples all lie within Quiet of 0. */
static int
quiet(int16_t *s, int n)
{
	int i;

	for (i = 0; i < n; i++)
		if (s[i] > Quiet || s[i] < -Quiet)
			return 0;
	return 1;
}

void
sdlmidi(int16_t *buf, int n, int rate)
{
	SDL_AudioSpec spec;
	int q;

	if (nomidi)
		return;
	if (midistream == NULL) {
		spec.format = SDL_AUDIO_S16LE;
		spec.channels = 2;
		spec.freq = rate;
		if (!SDL_InitSubSystem(SDL_INIT_AUDIO)
		|| (midistream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL)) == NULL) {
			fprintf(stderr, "lba: no music: %s\n", SDL_GetError());
			nomidi = 1;
			return;
		}
		SDL_SetAudioStreamGain(midistream, midigain);
		SDL_ResumeAudioStreamDevice(midistream);
		midilead = leadof(midistream, rate);
		fill(midistream, midilead);
	}
	if (!quiet(buf, 2*n))
		midiquiet = 0;
	else if (midiquiet < rate/10)
		midiquiet += n;
	if (midiquiet >= rate/10) {
		fill(midistream, midilead);
		return;
	}
	q = SDL_GetAudioStreamQueued(midistream);
	if (q > rate/2*4)
		return;
	SDL_PutAudioStreamData(midistream, buf, n*4);
	steer(midistream, q + n*4, midilead);
}

/* The mixer chip's gain on the music. */
void
sdlmidigain(float g)
{
	midigain = g;
	if (midistream != NULL)
		SDL_SetAudioStreamGain(midistream, g);
}

/*
 * sdllate: the lag the clock gave up, which spent what the streams held
 * ahead, made silence: each stream filled to its lead.
 */
void
sdllate(void)
{
	if (wavestream != NULL)
		fill(wavestream, wavelead);
	if (cdstream != NULL)
		fill(cdstream, cdlead);
	if (psstream != NULL)
		fill(psstream, cdlead);
	if (midistream != NULL)
		fill(midistream, midilead);
}

uint64_t
sdlnanos(void)
{
	return SDL_GetTicksNS();
}

void
sdlsleep(uint64_t ns)
{
	SDL_DelayNS(ns);
}

/* a DAC's 6 bits as 8, as DOSBox shows them */
static uint32_t
dac8(uint8_t v)
{
	return (uint32_t)(v<<2 | v>>4);
}

/*
 * Show the screen: its mode's part of the video memory through the DAC,
 * the 6-bit values widened to 8, unless it is as last shown and the
 * window still has it. The retrace is timer.c's, and with DisplaySync
 * each present waits for the display's refreshes too (sdlvsync).
 * Where the texture is wider or taller than the frame, the frame's last
 * column and row go once more beside it, which the linear filter reads
 * at the frame's edge. Mode 13h fills the largest 4:3 rectangle in the
 * middle of the screen, as a monitor showed it. Under SDL's dummy video
 * sdlshow returns at once.
 */
void
sdlshow(void)
{
	uint32_t pal[256];
	SDL_FRect src, dst;
	int i, w, h, dw, dh, x, y, tw, th;

	if (dummy)
		return;
	w = vga.mode == Mcga ? 320 : Screen_X;
	h = vga.mode == Mcga ? 200 : Screen_Y;
	if (!stale && vga.mode == lastmode && memcmp(vga.dac, lastdac, sizeof lastdac) == 0
	&& memcmp(vga.vram, lastvram, (size_t)w*h) == 0)
		return;
	lastmode = vga.mode;
	memcpy(lastdac, vga.dac, sizeof lastdac);
	memcpy(lastvram, vga.vram, (size_t)w*h);
	stale = 0;
	for (i = 0; i < 256; i++)
		pal[i] = 0xFF000000u | dac8(vga.dac[i][0])<<16 | dac8(vga.dac[i][1])<<8 | dac8(vga.dac[i][2]);
	tw = w < Wide_X ? w + 1 : w;
	th = h < Screen_Y ? h + 1 : h;
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++)
			pixels[y*tw + x] = pal[vga.vram[y*w + x]];
		if (tw > w)
			pixels[y*tw + w] = pixels[y*tw + w - 1];
	}
	if (th > h)
		memcpy(pixels + (size_t)h*tw, pixels + (size_t)(h - 1)*tw, (size_t)tw*sizeof pixels[0]);
	SDL_UpdateTexture(texture, &(SDL_Rect){0, 0, tw, th}, pixels, tw*4);
	src = (SDL_FRect){0, 0, w, h};
	dw = Screen_X;
	dh = Screen_Y;
	if (vga.mode == Mcga)
		dw = Screen_Y*4/3;
	dst = (SDL_FRect){(Screen_X - dw) / 2, (Screen_Y - dh) / 2, dw, dh};
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	SDL_RenderTexture(renderer, texture, &src, &dst);
	SDL_RenderPresent(renderer);
}

/*
 * FEATURE: the action key, W, a virtual key as LbaWin's and the
 * Classic's are. With LbaWinSettings's 4 set, the key that types W in
 * the keyboard's layout gives the game K_W, W's code on AZERTY, and the
 * key in that place, where it types another letter, the code of W's
 * place: the two trade codes. Taken from LbaWin 0.7.4 and the Classic
 * 3.2.3.
 */
static int
actionkey(int c, SDL_Keycode k)
{
	if (k == SDLK_W)
		return Kw;
	if (c == Kw)
		return xt[SDL_GetScancodeFromKey(SDLK_W, NULL)];
	return c;
}

/*
 * SdlKeyToVk is TLBA1C.exe 0x405ce0: the virtual key of SDL's key k,
 * Windows', or for a key keyvk leaves out, of the scan codes of
 * APOSTROPHE and GRAVE, and 0 for the others.
 */
static int
sdlkeytovk(SDL_Keycode k, SDL_Scancode sc)
{
	int i;

	for (i = 0; i < (int)nelem(keyvk); i++)
		if (keyvk[i].key == k)
			return keyvk[i].vk;
	if (sc == SDL_SCANCODE_APOSTROPHE)
		return 0xC0;
	if (sc == SDL_SCANCODE_GRAVE)
		return 0xDE;
	return 0;
}

/*
 * A key down or up: its code to the keyboard, and its virtual key for
 * the Classic's input where it is other than a US keyboard's.
 */
static void
key(SDL_Scancode sc, SDL_Keycode k, int down)
{
	int c, i, vk;

	c = xt[sc];
	if (LbaWinSettings & 4)
		c = actionkey(c, k);
	/*
	 * FEATURE: with GamePad a key that has no XT code, Print Screen,
	 * Pause, the menu's key or F13 to F24, reaches the Classic's input
	 * by its virtual key alone, as the Classic's ManageEvents gives it
	 * KeyState. Taken from the Classic 3.2.3.
	 */
	if (c == 0) {
		vk = sdlkeytovk(k, sc);
		if (FlagGamePad && vk != 0)
			vkin(vk, down);
		return;
	}
	vk = sdlkeytovk(k, sc);
	if (vk == usvkey(c, c & Ext))
		vk = -1;
	if (c & Ext)
		keyin(0xE0, -1);
	keyin((c & 0x7F) | (down ? 0 : 0x80), vk);
	if (!down)
		return;
	lastscan = c & 0x7F;
	/* With Num Lock on the keypad types its digit (text), the BIOS's word alone. */
	if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_PERIOD && (SDL_GetModState() & SDL_KMOD_NUM))
		return;
	for (i = 0; i < (int)nelem(biosspecial); i++)
		if (biosspecial[i].scan == lastscan) {
			wordin(biosspecial[i].word);
			break;
		}
}

/* Typed text, in CP437, as the BIOS would have had it, with its key's scan code. */
static void
text(const char *s)
{
	const uint8_t *p;
	uint32_t r;
	int c, i, n;

	for (p = (const uint8_t*)s; *p != 0; p += n) {
		if (*p < 0x80) {
			r = *p;
			n = 1;
		} else if ((*p & 0xE0) == 0xC0 && p[1] != 0) {
			r = (uint32_t)(*p & 0x1F)<<6 | (p[1] & 0x3F);
			n = 2;
		} else if ((*p & 0xF0) == 0xE0 && p[1] != 0 && p[2] != 0) {
			r = (uint32_t)(*p & 0x0F)<<12 | (uint32_t)(p[1] & 0x3F)<<6 | (p[2] & 0x3F);
			n = 3;
		} else {
			n = 1;
			continue;
		}
		c = -1;
		if (r < 0x80)
			c = (int)r;
		else
			for (i = 0; i < 128; i++)
				if (cp437[i] == r)
					c = 0x80 + i;
		if (c >= 0)
			wordin(lastscan<<8 | c);
	}
}

/*
 * FEATURE: the pads, with GamePad, as the Classic's SDL driver takes
 * them from SDL 2's GameController: every pad opened as it comes
 * (sdlgamepad), its buttons 0 to 20 and, past a dead zone of 10000, the
 * sticks' directions and the triggers, codes 21 to 30, each handed on
 * (padin) as it goes down or up. SDL 3 numbers the buttons as SDL 2 but
 * the face buttons, by their places where SDL 2 numbers a Nintendo
 * pad's by their labels (padbutton), and adds five past the touchpad,
 * which the port drops. Taken from the Classic 3.2.3.
 */

/* sdlgamepad: SDL's gamepads, whose events then come, an added one first for each there. */
void
sdlgamepad(void)
{
	if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
		fprintf(stderr, "lba: no gamepad: %s\n", SDL_GetError());
}

/* sdlpadclear: the codes down forgotten, as input.c's ClearInputs lets the pad's inputs go. */
void
sdlpadclear(void)
{
	padcodes = 0;
}

/* code down or up where it changes, as the Classic's PadButton's state says it */
static void
padpress(int code, int down)
{
	uint32_t bit;

	bit = (uint32_t)1 << code;
	if (down == ((padcodes & bit) != 0))
		return;
	if (down)
		padcodes |= bit;
	else
		padcodes &= ~bit;
	padin(code, down);
}

/*
 * The pad's input of SDL's button b on the pad which: SDL 3 numbers the
 * face buttons by their places, where the Classic's SDL 2 numbers a
 * Nintendo pad's by their labels, A, B, X and Y its inputs 0 to 3,
 * which their labels give back here. A face button of another label,
 * and every other button, keeps its place's number.
 */
static int
padbutton(SDL_JoystickID which, int b)
{
	SDL_Gamepad *g;

	if (b > SDL_GAMEPAD_BUTTON_NORTH)
		return b;
	g = SDL_GetGamepadFromID(which);
	if (g == NULL)
		return b;
	switch (SDL_GetGamepadButtonLabel(g, (SDL_GamepadButton)b)) {
	case SDL_GAMEPAD_BUTTON_LABEL_A:
		return SDL_GAMEPAD_BUTTON_SOUTH;
	case SDL_GAMEPAD_BUTTON_LABEL_B:
		return SDL_GAMEPAD_BUTTON_EAST;
	case SDL_GAMEPAD_BUTTON_LABEL_X:
		return SDL_GAMEPAD_BUTTON_WEST;
	case SDL_GAMEPAD_BUTTON_LABEL_Y:
		return SDL_GAMEPAD_BUTTON_NORTH;
	default:
		return b;
	}
}

/*
 * padaxis is TLBA1C.exe 0x405250 PadAxis and 0x4052e0 PadTrigger: a
 * stick's axis past the dead zone either way presses its direction and
 * lets the other go, within it lets both go, and a trigger past it
 * presses its code.
 *
 * FIXED: the Classic presses the direction again at each motion of the
 * stick past the dead zone, which presses its key again at each, a
 * burst of presses. A direction is pressed as the stick crosses into
 * it.
 */
static void
padaxis(int axis, int value)
{
	int neg, pos;

	if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
		padpress(Firstaxis + 8 + (axis - SDL_GAMEPAD_AXIS_LEFT_TRIGGER), value > Deadzone);
		return;
	}
	if (axis < SDL_GAMEPAD_AXIS_LEFTX || axis > SDL_GAMEPAD_AXIS_RIGHTY)
		return;
	neg = Firstaxis + 2*axis;
	pos = neg + 1;
	if (value > Deadzone) {
		padpress(neg, 0);
		padpress(pos, 1);
	} else if (value < -Deadzone) {
		padpress(pos, 0);
		padpress(neg, 1);
	} else {
		padpress(neg, 0);
		padpress(pos, 0);
	}
}

/*
 * A gamepad come or gone, as TLBA1C.exe 0x405320, ManageEvents, opens
 * the one come. SDL lets the buttons and the axes of one gone go before
 * it says so.
 *
 * FIXED: the Classic leaves the handle of a gamepad gone open. It is
 * closed.
 */
static void
padplug(SDL_JoystickID id, int added)
{
	SDL_Gamepad *g;

	if (added) {
		if (SDL_OpenGamepad(id) == NULL)
			fprintf(stderr, "lba: gamepad %u: %s\n", (unsigned)id, SDL_GetError());
		return;
	}
	g = SDL_GetGamepadFromID(id);
	if (g != NULL)
		SDL_CloseGamepad(g);
}

void
sdlpoll(void)
{
	SDL_Event e;

	SDL_SetAtomicInt(&polled, (int)SDL_GetTicks());
	if ((AsciiMode != 0) != textinput) {
		textinput = AsciiMode != 0;
		if (textinput)
			SDL_StartTextInput(window);
		else
			SDL_StopTextInput(window);
	}
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_EVENT_QUIT:
			portexit(0);
			break;
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
			key(e.key.scancode, e.key.key, e.type == SDL_EVENT_KEY_DOWN);
			break;
		case SDL_EVENT_TEXT_INPUT:
			text(e.text.text);
			break;
		/*
		 * FEATURE: the pad's buttons, SDL 2's 21 that the Classic counts
		 * (its pad's inputs 0 to 20), and its axes. Taken from the
		 * Classic 3.2.3.
		 */
		case SDL_EVENT_GAMEPAD_ADDED:
		case SDL_EVENT_GAMEPAD_REMOVED:
			padplug(e.gdevice.which, e.type == SDL_EVENT_GAMEPAD_ADDED);
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
			if (e.gbutton.button <= SDL_GAMEPAD_BUTTON_TOUCHPAD)
				padpress(padbutton(e.gbutton.which, e.gbutton.button), e.gbutton.down);
			break;
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
			padaxis(e.gaxis.axis, e.gaxis.value);
			break;
		case SDL_EVENT_WINDOW_EXPOSED:
			applyvsync();
			stale = 1;
			break;
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			stale = 1;
			break;
		case SDL_EVENT_WINDOW_SHOWN:
		case SDL_EVENT_WINDOW_HIDDEN:
		case SDL_EVENT_WINDOW_MINIMIZED:
		case SDL_EVENT_WINDOW_RESTORED:
		case SDL_EVENT_WINDOW_OCCLUDED:
			applyvsync();
			break;
		case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
		case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
			displayvsync();
			break;
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			hookkeys(1);
			break;
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			hookkeys(0);
			break;
		}
	}
}

static void
usage(void)
{
	fprintf(stderr, "usage: lba [-g gamedir]... [-c cd]... [-p prefdir] [-D WxH] [-R hz] [-r record | -i record] [-s sums] [-S n[-m[/k]]] [-w wave] [-m music]\n");
	exit(2);
}

/*
 * The directories: the game's (LBA.CFG and the HQR files), one or more,
 * read in the order given, the CD's (CD_LBA and LBA/, for the movies
 * and voices), one or more, each its root or a CD's image, read in the
 * order given, and the preferences, where everything written goes. By
 * default the game's is the current one, the CD's the first game
 * directory's parent, and the preferences SDL's. The configuration file
 * is read after them from the program's directory (files.c). The wide
 * screen takes its shape from the display, or from the size -D gives,
 * which a record keeps for its replays, and DisplaySync its pace and its
 * presents' vsync from the display's refresh in hertz, -R's, 1 to 1000
 * or 0 for unknown, the pace else a record's, else the window's
 * display's, and vsync else the window's display's. The input can be
 * recorded (-r) or played from a record (-i), the screens shown summed
 * (-s), the nth, or the nth to the mth (every kth), written as lban.bmp
 * (-S), and the samples mixed (-w) and the music made (-m) written raw:
 * replay.c.
 */
int
main(int argc, char *argv[])
{
	char *game[Ngame], *cd[Ncd], *pref, *rec, *in, *sums, *e, cdbuf[1024];
	const char *base;
	uint32_t n, m, k;
	long dw, dh;
	double hz;
	int i, ngame, ncd;

	meminit();
	ngame = 0;
	ncd = 0;
	pref = NULL;
	rec = NULL;
	in = NULL;
	sums = NULL;
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-' || argv[i][1] == 0 || argv[i][2] != 0 || i+1 >= argc)
			usage();
		switch (argv[i][1]) {
		case 'g':
			if (ngame == Ngame)
				fatal("-g given more than %d times", Ngame);
			game[ngame++] = argv[++i];
			break;
		case 'c':
			if (ncd == Ncd)
				fatal("-c given more than %d times", Ncd);
			cd[ncd++] = argv[++i];
			break;
		case 'p':
			pref = argv[++i];
			break;
		case 'D':
			dw = strtol(argv[++i], &e, 10);
			dh = *e == 'x' ? strtol(e+1, &e, 10) : 0;
			if (dw <= 0 || dw > 65535 || dh <= 0 || dh > 65535 || *e != '\0')
				usage();
			setdisplay((int)dw, (int)dh);
			break;
		case 'R':
			hz = strtod(argv[++i], &e);
			if (e == argv[i] || *e != '\0')
				usage();
			if (hz == 0)
				setrefresh(0);
			else if (hz >= 1 && hz <= 1000)
				setrefresh((uint64_t)(1e9/hz + 0.5));
			else
				usage();
			framegiven = 1;
			break;
		case 'r':
			rec = argv[++i];
			break;
		case 'i':
			in = argv[++i];
			break;
		case 's':
			sums = argv[++i];
			break;
		case 'S':
			n = strtoul(argv[++i], &e, 10);
			m = n;
			k = 1;
			if (*e == '-')
				m = strtoul(e+1, &e, 10);
			if (*e == '/')
				k = strtoul(e+1, NULL, 10);
			shotat(n, m, k);
			break;
		case 'w':
			waveto(argv[++i]);
			break;
		case 'm':
			musicto(argv[++i]);
			break;
		default:
			usage();
		}
	}
	if (rec != NULL && in != NULL)
		usage();
	sdlinit();
	if (pref == NULL) {
		pref = SDL_GetPrefPath(preforg, prefapp);
		if (pref == NULL)
			fatal("SDL_GetPrefPath: %s", SDL_GetError());
	}
	base = SDL_GetBasePath();
	if (ngame == 0)
		game[ngame++] = ".";
	if (ncd == 0) {
		if (strlen(game[0]) + 3 >= sizeof cdbuf)
			fatal("%s: too long a path", game[0]);
		snprintf(cdbuf, sizeof cdbuf, "%s/..", game[0]);
		cd[ncd++] = cdbuf;
	}
	setdirs(pref, game, ngame, cd, ncd);
	if (base != NULL && setprogdir(base) < 0)
		fatal("%s: too long a path", base);
	if (rec != NULL)
		recordinput(rec);
	if (in != NULL)
		playinput(in);
	if (sums != NULL)
		sumto(sums);
	startwatch();
	return lbamain(argc, argv);
}
