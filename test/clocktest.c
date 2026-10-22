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
 * passed. Then the timer's ticks from frame to frame: TimerRef and
 * TimerSystem step together, 0 or 1 a frame. The clock runs as outside
 * a replay, its sleeps stubs that return at once.
 * Each failure is a line, and clocktest exits 1 after any.
 */

enum
{
	Turns = 100,	/* the turns a case counts */
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

extern volatile uint32_t	TimerSystem;	/* sys/timer.c's, the system's time in its ticks */

Vga	vga;	/* sys/video.c's */
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
	USED(t);
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
wavetick(uint64_t t)
{
	USED(t);
}

void
cdtick(uint64_t t)
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
 * The screen's mode: TimerRef and TimerSystem step together, 0 or 1 from
 * frame to frame, the frames lasting less than a tick, a line where they
 * do otherwise.
 */
static void
ticks(int mode)
{
	uint32_t r, t;
	int k;

	vga.mode = mode;
	turn("f");
	turn("f");
	for (k = 0; k < Steps; k++) {
		r = TimerRef;
		t = TimerSystem;
		turn("f");
		if (TimerRef - r > 1 || TimerSystem - t != TimerRef - r) {
			fprintf(stderr, "clocktest: in mode %d a frame steps TimerRef %u and TimerSystem %u ticks, want 0 or 1 and the same\n",
				mode, (unsigned)(TimerRef - r), (unsigned)(TimerSystem - t));
			failed = 1;
			return;
		}
	}
}

int
main(void)
{
	size_t i;
	uint32_t before, got;
	int k, m;

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
	ticks(Svga);
	ticks(Mcga);
	return failed;
}
