/*
 * What the 1994 code had from Watcom's headers, given otherwise. make
 * defs includes this first in each old file (-include), as the DOS
 * builds read dos/compat.h first, so it stands alone and includes the
 * headers it needs: the C library's that declare the names renamed
 * below, first, so that the names rename only the game's calls, and
 * dos.h.
 */

/*
 * A compile outside DOS takes the fixes of the old code's faults, which
 * a build of the CD's program takes with FIXES=1. A pass that reads this
 * file as the DOS build sees the code, DOS defined, takes them from its
 * flags.
 */
#ifndef	DOS
#define	FIXES
#endif

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* stdlib.h: sizes of path parts */
#define _MAX_PATH	260
#define _MAX_DRIVE	3
#define _MAX_DIR	256
#define _MAX_FNAME	256
#define _MAX_EXT	256

#define max(a,b)	(((a) > (b)) ? (a) : (b))
#define min(a,b)	(((a) < (b)) ? (a) : (b))

/*
 * Functions of Watcom's library, under names of ours: rand and qsort
 * must be Watcom's, whose sequence the menus' fire draws and whose order
 * the holomap shows.
 */
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

int	watcomstricmp(const char*, const char*);
int	watcomstrnicmp(const char*, const char*, size_t);
char*	watcomitoa(int, char*, int);
void	watcomsplitpath(const char*, char*, char*, char*, char*);
char*	watcomgetcwd(char*, int);
int	watcomchdir(const char*);
int	watcomrand(void);
void	watcomsrand(unsigned);
void	watcomqsort(void*, size_t, size_t, int (*)(const void*, const void*));

#include "dos.h"
