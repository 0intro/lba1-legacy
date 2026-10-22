#include <stdint.h>
#include <stdio.h>
#include "../sys/dat.h"
#include "../sys/fns.h"

/*
 * widetest: the wide screen's width, sys/wide.c's screenwidth, held to
 * the width each display must give: 480 lines times the display's
 * shape, rounded down to a multiple of 4, within 640 and 1704.
 *
 *	widetest
 *
 * Each failure is a line, and widetest exits 1 after any.
 */

static const struct {
	int	w, h;	/* the display */
	int32_t	want;	/* the screen's width */
} cases[] = {
	{ 640, 480, 640 },	/* 4:3 */
	{ 1024, 768, 640 },
	{ 2048, 1536, 640 },
	{ 1280, 1024, 640 },	/* 5:4, narrower than 4:3 */
	{ 1920, 1080, 852 },	/* 16:9, 853.3 */
	{ 2560, 1440, 852 },
	{ 3840, 2160, 852 },
	{ 7680, 4320, 852 },	/* 16:9 at 8K */
	{ 1366, 768, 852 },	/* 853.75 */
	{ 1920, 1200, 768 },	/* 16:10 */
	{ 2560, 1600, 768 },
	{ 1680, 1050, 768 },
	{ 3440, 1440, 1144 },	/* 21:9, 1146.7 */
	{ 2560, 1080, 1136 },	/* 1137.8 */
	{ 2736, 1824, 720 },	/* 3:2 */
	{ 800, 400, 960 },	/* 2:1 */
	{ 5120, 1440, 1704 },	/* 32:9, 1706.7 */
	{ 5760, 1080, 1704 },	/* three displays of 16:9, held to 32:9 */
	{ 7680, 1080, 1704 },	/* 3413.3, held to 1704 */
	{ 1080, 1920, 640 },	/* on its side */
	{ 0, 0, 640 },	/* none */
	{ 1920, 0, 640 },
};

int
main(void)
{
	size_t i;
	int32_t got;
	int failed;

	failed = 0;
	for (i = 0; i < nelem(cases); i++) {
		got = screenwidth(cases[i].w, cases[i].h);
		if (got != cases[i].want) {
			fprintf(stderr, "widetest: screenwidth(%d, %d) = %d, want %d\n",
				cases[i].w, cases[i].h, (int)got, (int)cases[i].want);
			failed = 1;
		}
	}
	return failed;
}
