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
 * it gives the lag up.
 */

enum
{
	Tick = 20000000,	/* ns, the timer's 50 Hz */
	Work = 10000,		/* ns, a pump */
	Idle = 1000000,		/* ns, an idle */
	Late = 100000000,	/* ns behind the real clock, given up */
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

/*
 * A frame of the display, in ns: VGA's 25.175 MHz dot clock, 800 dots a
 * line, 525 lines at 640x480 (59.94 Hz) and 449 in mode 13h (70.09 Hz).
 */
static uint64_t
frame(void)
{
	return (uint64_t)800 * (vga.mode == Mcga ? 449 : 525) * 1000000000 / 25175000;
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
	else if (ahead < -Late)
		skew -= ahead;
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
