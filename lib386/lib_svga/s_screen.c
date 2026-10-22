#include <stdint.h>
#include <string.h>
#include "../lib_sys/adeline.h"
#include "lib_svga.h"

/* Cls is s_screen.asm's Cls: Log cleared. */
void
Cls(void)
{
	memset(Log, 0, 640*480);
}

/* CopyScreen is s_screen.asm's CopyScreen: a screen from src to dst, in memory. */
void
CopyScreen(void *src, void *dst)
{
	memcpy(dst, src, 640*480);
}
