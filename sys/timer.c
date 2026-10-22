#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include "dat.h"
#include "fns.h"

/*
 * The clocks of lib386/lib_sys/timer_a.asm, whose handler moved them 50
 * times a second: TimerRef (the game's), TimerSystem (the system's) and
 * the frame rate. SaveTimer and RestoreTimer (p_anim.asm) stop TimerRef
 * around menus by putting it back. The display's retrace is a clock too:
 * the screen is shown at most once a frame of its mode, and Vsync waits
 * for the next.
 *
 * Here they all follow a clock of our own, which moves where the game
 * waits (idle, Vsync, framesync) and a little at each pump, for the
 * work between two: a run depends on its input alone, and a replay
 * repeats it (replay.c). Unless replaying, the clock is held to the real
 * one, never ahead of it. Behind it by more than Late, as after a load,
 * it gives the lag up, which the sound's streams take as silence
 * (sdllate). The retrace comes at VGA's frame of the screen's mode, or
 * with DisplaySync at the display's frames n at a time where n of them
 * last that frame within one part in Parts (framepace), the display's
 * presents waiting for its refreshes.
 */

enum
{
	Tick = 20000000,	/* ns, the timer's 50 Hz */
	Work = 10000,		/* ns, a pump */
	Idle = 1000000,		/* ns, an idle */
	Late = 100000000,	/* ns behind the real clock, given up */
	Frame = 16683217,	/* ns, VGA's frame at 640x480: 800 dots by 525 lines at 25.175 MHz, 59.94 Hz */
	Frame13 = 14268123,	/* ns, in mode 13h: 449 lines, 70.09 Hz */
	Parts = 100,		/* frames within one part in Parts of each other match */
	Million = 1000000,	/* a display slower by a millionth or less than the pace still keeps to it */
	Maxsync = 4,		/* the most refreshes a present waits for, DXGI's */
};

volatile uint32_t	TimerRef;
volatile uint32_t	TimerSystem;
uint16_t	NbFramePerSecond;
uint16_t	WaitNbTicks = 1;
uint16_t	CmptFrame;
uint16_t	Cmpt_18;

static uint64_t now;
static int64_t skew;	/* the real clock when ours was 0 */
static int fast;
static uint64_t ticks;
static uint64_t retrace;	/* the last one the screen was shown at */
static uint64_t paces[2] = {Frame, Frame13};	/* ns, the retrace's period at 640x480 and in mode 13h: VGA's, or DisplaySync's pace */
static int synced;	/* DisplaySync ON */
static uint32_t nshown;
static uint64_t npump;
static volatile sig_atomic_t quitting;

#ifdef _WIN32
static void
deadline(unsigned secs)
{
	USED(secs);
}
#else
/* SIGALRM in secs seconds, none for 0 */
static void
deadline(unsigned secs)
{
	alarm(secs);
}
#endif

/* One tick, as timer_a.asm's NewIntPM08. */
static void
tick(void)
{
	TimerSystem++;
	TimerRef++;
	if (--WaitNbTicks == 0) {
		WaitNbTicks = 50;
		NbFramePerSecond = CmptFrame;
		CmptFrame = 0;
	}
	Cmpt_18 += 23864;
}

/* A frame of the screen's mode, in ns: VGA's, or DisplaySync's pace (paces). */
static uint64_t
frame(void)
{
	return paces[vga.mode == Mcga];
}

/* A request to end, taken at the next pump. Off Windows, a loop that never pumps dies a second later. */
static void
onterm(int sig)
{
	signal(sig, onterm);
	quitting = 1;
	deadline(1);
}

void
clockinit(void)
{
	now = 0;
	skew = (int64_t)sdlnanos();
	fast = playing();
	signal(SIGINT, onterm);
	signal(SIGTERM, onterm);
}

/* Our clock to t, and unless replaying the real one waited for. */
static void
advance(uint64_t t)
{
	int64_t ahead;

	now = t;
	if (fast)
		return;
	ahead = skew + (int64_t)now - (int64_t)sdlnanos();
	if (ahead > 0)
		sdlsleep((uint64_t)ahead);
	else if (ahead < -Late) {
		skew -= ahead;
		sdllate();
	}
}

/* The system's events or the record's, the clocks, and the screen if a retrace came since it was last shown. */
void
pump(void)
{
	uint64_t t;

	if (quitting) {
		deadline(0);
		portexit(0);
	}
	npump++;
	now += Work;
	while (ticks < now/Tick) {
		tick();
		ticks++;
	}
	sdlpoll();
	replay(npump);
	wavetick(now);
	cdtick(now);
	miditick(now);
	t = now - now%frame();
	if (t > retrace) {
		retrace = t;
		nshown++;
		sdlshow();
		summed(nshown, t);
	}
}

/* What a loop that waits calls: pump, and give the time back. */
void
idle(void)
{
	pump();
	advance(now + Idle);
}

/* How many pumps so far: the record's measure of when. */
uint64_t
pumps(void)
{
	return npump;
}

/* How many times the screen was shown. */
uint32_t
shown(void)
{
	return nshown;
}

/* The next retrace waited for, the screen shown then. */
void
vsync(void)
{
	advance(now - now%frame() + frame());
	pump();
}

/*
 * The top of a frame of the main loop, which drew as fast as the machine
 * let it: pump, and wait for the retrace if the screen was not shown
 * since the frame before, whose drawing would be lost.
 */
void
framesync(void)
{
	static uint32_t last;

	pump();
	if (nshown == last)
		vsync();
	last = nshown;
}

/* Whether a and b differ by one part in Parts of b at most. */
static int
within(uint64_t a, uint64_t b)
{
	return (a > b ? a - b : b - a) <= b/Parts;
}

/*
 * framepace: the game's frame of VGA's frame v on a display of frames d
 * ns long, 0 for unknown: n of the display's frames where n of them
 * last v within one part in Parts, so that each of the game's frames
 * shows for n of the display's, and v otherwise.
 */
uint64_t
framepace(uint64_t v, uint64_t d)
{
	uint64_t n;

	if (d == 0)
		return v;
	n = (v + d/2) / d;
	if (within(n*d, v))
		return n*d;
	return v;
}

/*
 * vsyncframes: the display's refreshes a present waits for at the pace
 * p on a display of frames d ns long: n where n of them last the pace
 * within one part in Parts, Maxsync at most, else 1 where the display
 * is faster than the pace, and 0 where it is slower, by more than a
 * millionth, or unknown, so that a present keeps to the pace. DXGI's
 * Present takes a SyncInterval of 4 at most, and SDL's direct3d11 and
 * direct3d12 renderers take a larger one and fail every present.
 */
int
vsyncframes(uint64_t p, uint64_t d)
{
	uint64_t n;

	if (d == 0 || d > p + p/Million)
		return 0;
	n = (p + p/Million) / d;
	if (within(n*d, p))
		return n > Maxsync ? Maxsync : (int)n;
	return 1;
}

/*
 * FEATURE: SetDisplaySync, lba.cfg's DisplaySync, ON where the key is
 * missing (modes.c's readplatform): where flag is set, each mode's
 * frames paced by the display's refresh (framepace of sdlrefresh's
 * frame) and the presents synchronized with it (displayvsync), and
 * VGA's frames with presents as they come otherwise, the flag given
 * back. vsync as the Classic 3.2.3 presents, the pace the port's own,
 * as LBA2's port paces its own.
 */
int32_t
SetDisplaySync(int32_t flag)
{
	uint64_t d;

	synced = flag != 0;
	d = synced ? sdlrefresh() : 0;
	paces[0] = framepace(Frame, d);
	paces[1] = framepace(Frame13, d);
	displayvsync();
	return flag;
}

/*
 * displayvsync: the presents synchronized with the window's display at
 * the pace of the screen's mode, as DisplaySync says, and as they come
 * in a replay, which runs as fast as the machine goes.
 */
void
displayvsync(void)
{
	if (fast)
		return;
	sdlvsync(synced ? vsyncframes(frame(), sdlwindowframe()) : 0);
}
