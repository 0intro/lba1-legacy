/*
 * What the 1994 code had from Watcom's headers, given otherwise. The
 * Makefile includes this first in each old file (-include), as the DOS
 * builds read dos/compat.h first, so it stands alone and includes the
 * headers it needs: the C library's that declare the names renamed
 * below, first, so that the names rename only the game's calls (mingw
 * declares _splitpath as imported from its DLL), and dos.h, which
 * sys/files.c shares.
 */

/*
 * The port takes the fixes of the old code's faults, which a build of
 * the CD's program takes with FIXES=1. A pass that reads this file as
 * the DOS build sees the code, DOS defined, takes them from its flags.
 */
#ifndef	DOS
#define	FIXES
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* stdlib.h: sizes of path parts, as mingw's own */
#define _MAX_PATH	260
#define _MAX_DRIVE	3
#define _MAX_DIR	256
#define _MAX_FNAME	256
#define _MAX_EXT	256

#define max(a,b)	(((a) > (b)) ? (a) : (b))
#define min(a,b)	(((a) < (b)) ? (a) : (b))

/*
 * Functions of Watcom's library, under names of ours: msvcrt exports
 * several of these names, rand and qsort must be Watcom's, whose
 * sequence the menus' fire draws and whose order the holomap shows, and
 * atoi, whose value past 32 bits each C library gives otherwise.
 */
#define atoi		watcomatoi
#define stricmp		watcomstricmp
#define strcmpi		watcomstricmp
#define strnicmp	watcomstrnicmp
#define itoa		watcomitoa
#define _splitpath	watcomsplitpath
#define getcwd		watcomgetcwd
#define chdir		watcomchdir
#define rand		watcomrand
#define srand		watcomsrand
#define qsort		watcomqsort

int	watcomatoi(const char*);
int	watcomstricmp(const char*, const char*);
int	watcomstrnicmp(const char*, const char*, size_t);
char*	watcomitoa(int, char*, int);
void	watcomsplitpath(const char*, char*, char*, char*, char*);
char*	watcomgetcwd(char*, int);
int	watcomchdir(const char*);
int	watcomrand(void);
void	watcomsrand(unsigned);
void	watcomqsort(void*, size_t, size_t, int (*)(const void*, const void*));

/* The program starts in sys/sdl.c, which calls the game's main. */
#define main		lbamain

/*
 * DOS's interrupts changed the keyboard's and the timer's variables
 * while the game waited on them. In the port the loops that wait call
 * idle (sys/timer.c), which does the same. The main loop drew as fast
 * as it could. In the port it calls framesync at the top of each frame,
 * which does the same and draws no more frames than the screen shows.
 */
void	idle(void);
void	framesync(void);

/* The game's own switches of lba.cfg, each its default where the file leaves it out (sys/modes.c). */
int32_t	gamekey(char*, char*);

#include "dos.h"
