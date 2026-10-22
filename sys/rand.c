#include <stdint.h>

/*
 * Watcom C 9.01's rand and srand, written from what LBA.EXE's do and
 * proved against them by tools/oracle: the sequence the menus' fire
 * draws, from the seed 1 at every start, as LBA.EXE never calls srand.
 */

static uint32_t randseed = 1;

/* watcomrand is Watcom's rand (LBA.EXE 0x28eaf). */
int
watcomrand(void)
{
	randseed = randseed*0x41c64e6d + 0x3039;
	return randseed>>16 & 0x7fff;
}

/* watcomsrand is Watcom's srand (LBA.EXE 0x28ed4). */
void
watcomsrand(unsigned seed)
{
	randseed = seed;
}
