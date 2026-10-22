#include <stdint.h>
#include <string.h>
#include "../lib386/lib_sys/adeline.h"
#include "../lib386/lib_svga/lib_svga.h"

/* Mcga_Cls is mcga.asm's Mcga_Cls: Log's 320x200 cleared. */
void
Mcga_Cls(void)
{
	memset(Log, 0, 320*200);
}

/* Mcga_Flip is mcga.asm's Mcga_Flip: Log's 320x200 to the screen. */
void
Mcga_Flip(void)
{
	memcpy(Phys, Log, 320*200);
}
