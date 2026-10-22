#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../sys/dat.h"
#include "../sys/fns.h"

/*
 * clocktest: sys/timer.c's clock driven as the game drives it, the
 * screen and the rest of the port stubs of the test's own, counting the
 * retraces each loop of the game's calls waits for.
 *
 *	clocktest
 *
 * A turn is a string of calls: f framesync, the top of the main loop's
 * frame, p pump, a read of the keys or the clock inside a frame, i
 * idle, a loop's wait, v Vsync, a palette's wait for the retrace. Each
 * case runs one turn to settle, then Turns turns, in each of the
 * screen's modes, 640x480 and mode 13h, and counts the retraces they
 * passed. Then the game's frame and the display's refreshes a present
 * waits for (framepace, vsyncframes) in each mode on displays of each
 * rate, on a display the window moved to, and as SetDisplaySync sets
 * them, the pace from the run's display frame (sdlrefresh) and vsync
 * from its window's (sdlwindowframe). Then the timer's ticks from frame
 * to frame with DisplaySync off and on: TimerRef and TimerSystem a tick
 * for each 20 ms of the clock, as timer_a.asm's 50 Hz moved them. The
 * clock runs as outside a replay, its sleeps stubs that return at once.
 * Each failure is a line, and clocktest exits 1 after any.
 */

enum
{
	Turns = 100,	/* the turns a case counts */
	Vgaframe = 16683217,	/* ns, VGA's frame at 640x480, timer.c's Frame */
	Vgaframe13 = 14268123,	/* ns, in mode 13h, timer.c's Frame13 */
	Tickms = 20,	/* ms, the timer's tick */
	Steps = 30,	/* the frames whose steps a case checks */
};

static const struct {
	char	*turn;	/* the calls of a turn */
	uint32_t	want;	/* the retraces of Turns turns */
} cases[] = {
	{ "f", Turns },	/* a loop of waits, or a frame of the main loop */
	{ "fp", Turns },	/* a frame reading the keys inside it */
	{ "fpp", Turns },	/* reading them twice */
	{ "ifpi", Turns },	/* a frame between two waits */
	{ "fv", Turns },	/* a palette's fade */
};

/* displays of each rate: the game's frame in a mode and the refreshes a present waits for */
static const struct {
	int	mode;	/* the screen's */
	uint64_t	d;	/* ns, the display's frame */
	uint64_t	pace;	/* ns, the game's */
	int	vsync;
} displays[] = {
	{ Svga, 0, Vgaframe, 0 },	/* unknown */
	{ Svga, 33333333, Vgaframe, 0 },	/* 30 Hz */
	{ Svga, 20000000, Vgaframe, 0 },	/* 50 Hz */
	{ Svga, 16683333, 16683333, 1 },	/* 59.94 Hz */
	{ Svga, 16666667, 16666667, 1 },	/* 60 Hz */
	{ Svga, 13333333, Vgaframe, 1 },	/* 75 Hz, a frame for 1 or 2 refreshes */
	{ Svga, 10000000, Vgaframe, 1 },	/* 100 Hz */
	{ Svga, 8341675, 16683350, 2 },	/* 119.88 Hz */
	{ Svga, 8333333, 16666666, 2 },	/* 120 Hz */
	{ Svga, 6944444, Vgaframe, 1 },	/* 144 Hz, 2 or 3 */
	{ Svga, 6060606, Vgaframe, 1 },	/* 165 Hz */
	{ Svga, 5555556, 16666668, 3 },	/* 180 Hz */
	{ Svga, 4166667, 16666668, 4 },	/* 240 Hz */
	{ Svga, 3333333, 16666665, 4 },	/* 300 Hz, a frame for 5 refreshes, its present for DXGI's 4 at most */
	{ Svga, 2777778, 16666668, 4 },	/* 360 Hz, 6 and 4 */
	{ Mcga, 0, Vgaframe13, 0 },	/* unknown */
	{ Mcga, 16666667, Vgaframe13, 0 },	/* 60 Hz, slower than mode 13h's 70.09 Hz */
	{ Mcga, 14285714, 14285714, 1 },	/* 70 Hz */
	{ Mcga, 14268123, 14268123, 1 },	/* 70.09 Hz, VGA's own */
	{ Mcga, 13333333, Vgaframe13, 1 },	/* 75 Hz */
	{ Mcga, 8333333, Vgaframe13, 1 },	/* 120 Hz, a frame for 1 or 2 refreshes */
	{ Mcga, 7142857, 14285714, 2 },	/* 140 Hz */
	{ Mcga, 6944444, Vgaframe13, 1 },	/* 144 Hz */
	{ Mcga, 4761905, 14285715, 3 },	/* 210 Hz */
	{ Mcga, 3571429, 14285716, 4 },	/* 280 Hz */
	{ Mcga, 2857143, 14285715, 4 },	/* 350 Hz, 5 and 4 */
};

/* the window moved to another display at a run's pace: the refreshes a present waits for */
static const struct {
	uint64_t	pace;	/* ns */
	uint64_t	d;	/* ns, the display's frame */
	int	vsync;
} moves[] = {
	{ 16666667, 16683333, 0 },	/* 60 Hz's pace on 59.94 Hz, slower */
	{ 16666666, 16666667, 1 },	/* 120 Hz's on 60 Hz, a nanosecond slower */
	{ Vgaframe, 16666667, 1 },	/* VGA's on 60 Hz */
	{ 16666667, 8333333, 2 },	/* 60 Hz's on 120 Hz */
	{ 16666667, 0, 0 },	/* on a display of an unknown rate */
};

extern volatile uint32_t	TimerSystem;	/* sys/timer.c's, the system's time in its ticks */

Vga	vga;	/* sys/video.c's */
int32_t	FlagGamePad;	/* sys/input.c's */
static uint64_t	display;	/* ns, the frame of the window's display, sdlwindowframe's */
static uint64_t	refresh;	/* ns, the run's display frame, sdlrefresh's */
static int	vsyncs = -1;	/* the refreshes the last sdlvsync asked */
static uint64_t	shownat[2];	/* ns, the last two retraces summed */
static int	failed;	/* a check failed */

uint64_t
sdlnanos(void)
{
	return 0;
}

void
sdlsleep(uint64_t ns)
{
	USED(ns);
}

void
sdllate(void)
{
}

void
sdlshow(void)
{
}

void
summed(uint32_t n, uint64_t t)
{
	USED(n);
	shownat[0] = shownat[1];
	shownat[1] = t;
}

uint64_t
sdlrefresh(void)
{
	return refresh;
}

uint64_t
sdlwindowframe(void)
{
	return display;
}

void
sdlvsync(int n)
{
	vsyncs = n;
}

void
sdlpoll(void)
{
}

int
playing(void)
{
	return 0;
}

void
portexit(int status)
{
	exit(status);
}

void
replay(uint64_t n)
{
	USED(n);
}

void
ReadInputs(uint32_t context)
{
	USED(context);
}

void
wavetick(uint64_t t)
{
	USED(t);
}

void
cdtick(uint64_t t)
{
	USED(t);
}

void
miditick(uint64_t t)
{
	USED(t);
}

void
soundtick(uint64_t t)
{
	USED(t);
}

/* One turn's calls. */
static void
turn(const char *s)
{
	for (; *s != '\0'; s++) {
		switch (*s) {
		case 'f':
			framesync();
			break;
		case 'p':
			pump();
			break;
		case 'i':
			idle();
			break;
		case 'v':
			vsync();
			break;
		default:
			fprintf(stderr, "clocktest: bad call %c\n", *s);
			exit(2);
		}
	}
}

/*
 * SetDisplaySync(flag) in the screen's mode for a run of display frames
 * r ns, its window on a display of frames d ns: the retraces then come
 * pace ns apart and the presents wait for vsync refreshes, a line where
 * they differ.
 */
static void
synced(int mode, int32_t flag, uint64_t r, uint64_t d, uint64_t pace, int vsync)
{
	int k;

	vga.mode = mode;
	refresh = r;
	display = d;
	SetDisplaySync(flag);
	for (k = 0; k < 3; k++)
		turn("f");
	if (shownat[1] - shownat[0] != pace || vsyncs != vsync) {
		fprintf(stderr, "clocktest: SetDisplaySync(%d) in mode %d for a run of frames %" PRIu64 " ns on a display of frames %" PRIu64 " ns paces retraces %" PRIu64 " ns apart and vsync %d, want %" PRIu64 " and %d\n",
			(int)flag, mode, r, d, shownat[1] - shownat[0], vsyncs, pace, vsync);
		failed = 1;
	}
}

/*
 * SetDisplaySync(flag) in the screen's mode on a display of frames d
 * ns: TimerRef and TimerSystem step together, 0 or 1 from frame to
 * frame, the frames lasting less than a tick, and over Steps frames by
 * the clock's milliseconds over 20, a line where they do otherwise.
 */
static void
ticks(int mode, int32_t flag, uint64_t d)
{
	uint32_t ref, sys, r, t, ms;
	int k;

	vga.mode = mode;
	refresh = display = d;
	SetDisplaySync(flag);
	turn("f");
	turn("f");
	ref = TimerRef;
	sys = TimerSystem;
	ms = timegettime();
	for (k = 0; k < Steps; k++) {
		r = TimerRef;
		t = TimerSystem;
		turn("f");
		if (TimerRef - r > 1 || TimerSystem - t != TimerRef - r) {
			fprintf(stderr, "clocktest: with DisplaySync %d in mode %d on a display of frames %" PRIu64 " ns, a frame steps TimerRef %u and TimerSystem %u ticks, want 0 or 1 and the same\n",
				(int)flag, mode, d, (unsigned)(TimerRef - r), (unsigned)(TimerSystem - t));
			failed = 1;
			return;
		}
	}
	ms = timegettime() - ms;
	r = TimerRef - ref;
	if (r + 1 < ms/Tickms || r > ms/Tickms + 1 || TimerSystem - sys != r) {
		fprintf(stderr, "clocktest: with DisplaySync %d in mode %d on a display of frames %" PRIu64 " ns, TimerRef steps %u ticks and TimerSystem %u in %u ms, want %u\n",
			(int)flag, mode, d, (unsigned)r, (unsigned)(TimerSystem - sys), (unsigned)ms, (unsigned)(ms/Tickms));
		failed = 1;
	}
}

int
main(void)
{
	size_t i;
	uint32_t before, got;
	uint64_t pace;
	int k, n, m;

	clockinit();
	for (m = Svga; m <= Mcga; m++) {
		vga.mode = m;
		for (i = 0; i < nelem(cases); i++) {
			turn(cases[i].turn);
			before = shown();
			for (k = 0; k < Turns; k++)
				turn(cases[i].turn);
			got = shown() - before;
			if (got != cases[i].want) {
				fprintf(stderr, "clocktest: in mode %d, %d turns of %s wait %u retraces, want %u\n",
					m, Turns, cases[i].turn, (unsigned)got, (unsigned)cases[i].want);
				failed = 1;
			}
		}
	}
	for (i = 0; i < nelem(displays); i++) {
		pace = framepace(displays[i].mode == Mcga ? Vgaframe13 : Vgaframe, displays[i].d);
		n = vsyncframes(pace, displays[i].d);
		if (pace != displays[i].pace || n != displays[i].vsync) {
			fprintf(stderr, "clocktest: in mode %d a display of frames %" PRIu64 " ns gives a pace of %" PRIu64 " ns and vsync %d, want %" PRIu64 " and %d\n",
				displays[i].mode, displays[i].d, pace, n, displays[i].pace, displays[i].vsync);
			failed = 1;
		}
	}
	for (i = 0; i < nelem(moves); i++) {
		n = vsyncframes(moves[i].pace, moves[i].d);
		if (n != moves[i].vsync) {
			fprintf(stderr, "clocktest: the pace %" PRIu64 " ns on a display of frames %" PRIu64 " ns gives vsync %d, want %d\n",
				moves[i].pace, moves[i].d, n, moves[i].vsync);
			failed = 1;
		}
	}
	synced(Svga, 1, 8333333, 8333333, 16666666, 2);
	synced(Svga, 0, 8333333, 8333333, Vgaframe, 0);
	synced(Svga, 1, 8333333, 16666667, 16666666, 1);
	synced(Svga, 1, 16683333, 16683333, 16683333, 1);
	synced(Mcga, 1, 7142857, 7142857, 14285714, 2);
	synced(Mcga, 1, 16666667, 16666667, Vgaframe13, 0);
	synced(Mcga, 0, 7142857, 7142857, Vgaframe13, 0);
	ticks(Svga, 0, 0);
	ticks(Svga, 1, 8333333);
	ticks(Mcga, 0, 0);
	ticks(Mcga, 1, 7142857);
	return failed;
}
