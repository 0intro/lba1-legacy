#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "../sys/dat.h"
#include "../sys/fns.h"

/*
 * padtest: sys/sdl.c's gamepad held to what each event must give.
 *
 *	padtest -p dir
 *
 * sdl.c's main starts SDL as the port does, then runs the test as the
 * game. The test attaches SDL's virtual gamepad, which sdl.c opens as it
 * comes, and moves its buttons, sticks and triggers: each step must hand
 * on (padin) the codes the Classic gives, its buttons 0 to 20 as they go
 * down and up, a Switch Pro pad's face buttons by their labels, which
 * trade A with B and X with Y against their places, past a dead zone of
 * 10000 the sticks' directions 21 to 28
 * and the triggers 29 and 30, each as it goes down or up, a stick held
 * pressing its direction again as it moves once the codes are forgotten
 * (sdlpadclear), and when the gamepad goes, every code down let go,
 * which SDL does. A trigger's
 * axis on SDL's virtual gamepad runs from -32768, let go, to 32767,
 * which SDL gives as 0 to 32767. Each failure is a line, and
 * padtest exits 1 after any. Run it with SDL's dummy drivers, whose
 * window takes the events of a gamepad in the background.
 */

enum
{
	Nev = 32,
};

Vga	vga;
uint16_t	AsciiMode;
int32_t	Wide_X = 640;
int32_t	Screen_X = 640;
int32_t	Screen_Y = 480;
int32_t	FlagIntegerScaling;
int32_t	DesiredDisplayNum;
int32_t	FlagGamePad;
int32_t	LbaWinSettings;

static int	got[Nev];	/* the codes handed on, 0x80 for up */
static int	ngot;
static int	failed;

void
padin(int n, int down)
{
	if (ngot < Nev)
		got[ngot++] = n | (down ? 0 : 0x80);
}

/* What sdl.c's main and its events call of the port, which the test leaves as it is. */
void
meminit(void)
{
}

void
setdirs(char *pref, char **game, int ngame, char **cd, int ncd)
{
	(void)pref;
	(void)game;
	(void)ngame;
	(void)cd;
	(void)ncd;
}

char*
startin(char *pref, const char *base, char **cd, int ncd, const char *saved)
{
	(void)pref;
	(void)base;
	(void)cd;
	(void)ncd;
	(void)saved;
	return NULL;
}

void
setdrives(int on)
{
	(void)on;
}

void
setwriter(void (*fn)(int))
{
	(void)fn;
}

int
setprogdir(const char *dir)
{
	(void)dir;
	return 0;
}

void
writer(void)
{
}

void
endwrites(void)
{
}

void
recordinput(char *f)
{
	(void)f;
}

void
playinput(char *f)
{
	(void)f;
}

int
playing(void)
{
	return 0;
}

void
sumto(char *f)
{
	(void)f;
}

void
shotat(uint32_t n, uint32_t m, uint32_t k)
{
	(void)n;
	(void)m;
	(void)k;
}

void
waveto(char *file)
{
	(void)file;
}

void
musicto(char *file)
{
	(void)file;
}

void
displayvsync(void)
{
}

void
keyin(int c, int vk)
{
	(void)c;
	(void)vk;
}

void
vkin(int vk, int down)
{
	(void)vk;
	(void)down;
}

void
wordin(int w)
{
	(void)w;
}

int
usvkey(int c, int ext)
{
	(void)c;
	(void)ext;
	return 0;
}

int
refusealtf4(void)
{
	return 0;
}

void
hookkeys(int on)
{
	(void)on;
}

/* The events polled, and the codes they handed on held to want, -1 its end. */
static void
expect(char *what, const int *want)
{
	int i, n;

	ngot = 0;
	sdlpoll();
	for (n = 0; want[n] >= 0; n++)
		;
	if (ngot == n && memcmp(got, want, n * sizeof want[0]) == 0)
		return;
	failed = 1;
	printf("padtest: %s: got", what);
	for (i = 0; i < ngot; i++)
		printf(" %s%d", got[i] & 0x80 ? "-" : "+", got[i] & 0x7F);
	printf(", want");
	for (i = 0; i < n; i++)
		printf(" %s%d", want[i] & 0x80 ? "-" : "+", want[i] & 0x7F);
	printf("\n");
}

static void
button(SDL_Joystick *j, int b, int down, char *what, const int *want)
{
	SDL_SetJoystickVirtualButton(j, b, down);
	expect(what, want);
}

static void
axis(SDL_Joystick *j, int a, int v, char *what, const int *want)
{
	SDL_SetJoystickVirtualAxis(j, a, (Sint16)v);
	expect(what, want);
}

int
lbamain(int argc, char **argv)
{
	static const int none[] = {-1};
	SDL_VirtualJoystickDesc desc;
	SDL_JoystickID id;
	SDL_Joystick *j;

	(void)argc;
	(void)argv;
	/* the gamepad's presses taken in the background, as the dummy driver's window runs */
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	sdlgamepad();
	SDL_INIT_INTERFACE(&desc);
	desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
	desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
	desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
	desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
	desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
	desc.name = "padtest";
	id = SDL_AttachVirtualJoystick(&desc);
	if (id == 0) {
		printf("padtest: SDL_AttachVirtualJoystick: %s\n", SDL_GetError());
		return 1;
	}
	expect("the gamepad come", none);
	j = SDL_GetJoystickFromID(id);
	if (j == NULL || !SDL_IsGamepad(id)) {
		printf("padtest: the gamepad come opens as none: %s\n", SDL_GetError());
		return 1;
	}
	axis(j, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768, "the left trigger at rest", none);
	axis(j, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768, "the right trigger at rest", none);

	button(j, SDL_GAMEPAD_BUTTON_SOUTH, 1, "A down", (const int[]){0, -1});
	button(j, SDL_GAMEPAD_BUTTON_SOUTH, 0, "A up", (const int[]){0x80, -1});
	button(j, SDL_GAMEPAD_BUTTON_START, 1, "Start down", (const int[]){6, -1});
	button(j, SDL_GAMEPAD_BUTTON_START, 0, "Start up", (const int[]){0x86, -1});
	button(j, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 1, "the pad's right down", (const int[]){14, -1});
	button(j, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 0, "the pad's right up", (const int[]){0x8E, -1});
	button(j, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, 1, "a paddle down", (const int[]){17, -1});
	button(j, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, 0, "a paddle up", (const int[]){0x91, -1});
	button(j, SDL_GAMEPAD_BUTTON_TOUCHPAD, 1, "the touchpad down", (const int[]){20, -1});
	button(j, SDL_GAMEPAD_BUTTON_TOUCHPAD, 0, "the touchpad up", (const int[]){0x94, -1});
	button(j, SDL_GAMEPAD_BUTTON_MISC2, 1, "a button the Classic lacks down", none);
	button(j, SDL_GAMEPAD_BUTTON_MISC2, 0, "a button the Classic lacks up", none);

	axis(j, SDL_GAMEPAD_AXIS_LEFTX, -10000, "the left stick at the dead zone's edge", none);
	axis(j, SDL_GAMEPAD_AXIS_LEFTX, -10001, "the left stick to the left", (const int[]){21, -1});
	axis(j, SDL_GAMEPAD_AXIS_LEFTX, -30000, "the left stick further left", none);
	sdlpadclear();
	axis(j, SDL_GAMEPAD_AXIS_LEFTX, -25000, "the left stick held as the codes are forgotten", (const int[]){21, -1});
	axis(j, SDL_GAMEPAD_AXIS_LEFTX, 20000, "the left stick across to the right", (const int[]){0x95, 22, -1});
	axis(j, SDL_GAMEPAD_AXIS_LEFTX, 0, "the left stick back", (const int[]){0x96, -1});
	axis(j, SDL_GAMEPAD_AXIS_LEFTY, -20000, "the left stick up", (const int[]){23, -1});
	axis(j, SDL_GAMEPAD_AXIS_LEFTY, 0, "the left stick back from up", (const int[]){0x97, -1});
	axis(j, SDL_GAMEPAD_AXIS_RIGHTX, 20000, "the right stick to the right", (const int[]){26, -1});
	axis(j, SDL_GAMEPAD_AXIS_RIGHTX, 0, "the right stick back", (const int[]){0x9A, -1});
	axis(j, SDL_GAMEPAD_AXIS_RIGHTY, 32767, "the right stick down", (const int[]){28, -1});
	axis(j, SDL_GAMEPAD_AXIS_RIGHTY, 0, "the right stick back from down", (const int[]){0x9C, -1});
	axis(j, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 2*9000 - 32768, "the left trigger short of the dead zone", none);
	axis(j, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 2*11000 - 32768, "the left trigger pressed", (const int[]){29, -1});
	axis(j, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768, "the left trigger let go", (const int[]){0x9D, -1});
	axis(j, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767, "the right trigger pressed", (const int[]){30, -1});

	button(j, SDL_GAMEPAD_BUTTON_WEST, 1, "X down", (const int[]){2, -1});
	axis(j, SDL_GAMEPAD_AXIS_LEFTX, -20000, "the left stick to the left again", (const int[]){21, -1});
	SDL_DetachVirtualJoystick(id);
	expect("the gamepad gone", (const int[]){0x95, 0x9E, 0x82, -1});

	desc.vendor_id = 0x057E;	/* a Switch Pro pad's */
	desc.product_id = 0x2009;
	desc.name = "padtest switch";
	id = SDL_AttachVirtualJoystick(&desc);
	if (id == 0) {
		printf("padtest: SDL_AttachVirtualJoystick: %s\n", SDL_GetError());
		return 1;
	}
	expect("the Switch Pro pad come", none);
	j = SDL_GetJoystickFromID(id);
	if (j == NULL || !SDL_IsGamepad(id)) {
		printf("padtest: the Switch Pro pad come opens as none: %s\n", SDL_GetError());
		return 1;
	}
	axis(j, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768, "the Switch Pro pad's left trigger at rest", none);
	axis(j, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768, "the Switch Pro pad's right trigger at rest", none);
	button(j, SDL_GAMEPAD_BUTTON_SOUTH, 1, "the Switch Pro pad's B down", (const int[]){1, -1});
	button(j, SDL_GAMEPAD_BUTTON_SOUTH, 0, "the Switch Pro pad's B up", (const int[]){0x81, -1});
	button(j, SDL_GAMEPAD_BUTTON_EAST, 1, "the Switch Pro pad's A down", (const int[]){0, -1});
	button(j, SDL_GAMEPAD_BUTTON_EAST, 0, "the Switch Pro pad's A up", (const int[]){0x80, -1});
	button(j, SDL_GAMEPAD_BUTTON_WEST, 1, "the Switch Pro pad's Y down", (const int[]){3, -1});
	button(j, SDL_GAMEPAD_BUTTON_WEST, 0, "the Switch Pro pad's Y up", (const int[]){0x83, -1});
	button(j, SDL_GAMEPAD_BUTTON_NORTH, 1, "the Switch Pro pad's X down", (const int[]){2, -1});
	button(j, SDL_GAMEPAD_BUTTON_NORTH, 0, "the Switch Pro pad's X up", (const int[]){0x82, -1});
	SDL_DetachVirtualJoystick(id);
	expect("the Switch Pro pad gone", none);

	if (!failed)
		printf("padtest: ok\n");
	return failed;
}
