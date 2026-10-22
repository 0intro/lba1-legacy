/*
 * What the DOS build's compilers need to build the 1994 tree as Watcom C
 * 9.01 did, read before every C file (wcc386 -fi), as gcc reads
 * sys/compat.h before the old files. The C files that name no DOS header
 * find dos.h and direct.h here, after the C library's that the renames
 * below must leave alone. __WATCOMC__ is 901 for Watcom C
 * 9.01, 950 for 9.5 and 1300 for OpenWatcom 2.
 */

/*
 * The exact-width types as the 1994 LONG and its kin were, long and short:
 * Watcom C 9 has no stdint.h, and OpenWatcom's makes the 32-bit ones int,
 * where a function called before its definition takes the type of its
 * arguments, as 1994's did, and its definition must agree. OpenWatcom's
 * stdint.h skips its own when _EXACT_WIDTH_INTS is defined.
 */
#define _EXACT_WIDTH_INTS
typedef signed char		int8_t;
typedef unsigned char		uint8_t;
typedef short			int16_t;
typedef unsigned short		uint16_t;
typedef long			int32_t;
typedef unsigned long		uint32_t;

#include <stddef.h>
#include <stdlib.h>
#include <dos.h>
#include <direct.h>

/*
 * diskfunc.c keeps the drive in a ULONG, which Watcom C 9 passed to
 * _dos_getdrive and _dos_setdrive with a warning and OpenWatcom rejects:
 * the same 32 bits, so a cast.
 */
#define _dos_getdrive(d)	(_dos_getdrive)((unsigned *)(d))
#define _dos_setdrive(d, n)	(_dos_setdrive)((d), (unsigned *)(n))

/*
 * The port's loops that wait on the keyboard or the timer call idle, and
 * its main loop framesync at the top of each frame, for what DOS's
 * interrupts and its uncapped loop do: here both are empty.
 */
#define idle()
#define framesync()

/*
 * The port's screen can be wider than 640x480: there the old code takes
 * the screen's size from LARGEUR_ECRAN and HAUTEUR_ECRAN and the scene's
 * centre from CENTRE_ECRAN_X and CENTRE_ECRAN_Y, draws the scene on the
 * whole screen (pushscreen) and the rest in a window of 640x480 in its
 * middle (pushwindow, poparea), at FENETRE_X, and puts a picture of
 * 640x480 in that window (windowpicture), its buffers LARGEUR_MAX wide,
 * the widest the run takes. Here the screen is 640x480, the window
 * fills it, and the four are empty.
 */
#define LARGEUR_ECRAN	640
#define LARGEUR_MAX	640
#define HAUTEUR_ECRAN	480
#define CENTRE_ECRAN_X	320
#define CENTRE_ECRAN_Y	240
#define FENETRE_X	0
#define pushscreen()
#define pushwindow()
#define poparea()
#define windowpicture(p)

#if __WATCOMC__ > 901
/*
 * perso.c installs its critical error handler through _harderr_, the name
 * Watcom C 9.01's run-time gives _harderr. The later ones call it
 * _harderr.
 */
#define _harderr_	_harderr

/*
 * Watcom C 9.01's qsort (sys/qsort.c), in the order the holomap's globe
 * shows, in place of a later run-time's: OpenWatcom's leaves equal
 * elements in another.
 */
#define qsort		watcomqsort

void	watcomqsort(void*, size_t, size_t, int (*)(const void*, const void*));
#endif
