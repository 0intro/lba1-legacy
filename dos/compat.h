/*
 * What the DOS build's compilers need to build the 1994 tree as Watcom C
 * 9.01 did, read before every C file (wcc386 -fi), after the C library's
 * headers that its renames must leave alone. __WATCOMC__ is 901 for
 * Watcom C 9.01, 950 for 9.5 and 1300 for OpenWatcom 2.
 */

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
