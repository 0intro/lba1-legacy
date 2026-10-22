#include <stdint.h>
#include <string.h>
#include "adeline.h"
#include "lib_sys.h"

/*
 * MovMem is buffer_a.asm's MovMem: n bytes from src to dst, forward, four
 * at a time and then one at a time, as rep movsd and rep movsb copied
 * them where the two overlap.
 */
void
MovMem(void *src, void *dst, uint32_t n)
{
	uint8_t *s, *d, b[4];
	uint32_t i;

	s = src;
	d = dst;
	for (i = 0; i < n/4; i++) {
		memcpy(b, s, 4);
		memcpy(d, b, 4);
		s += 4;
		d += 4;
	}
	for (i = 0; i < n%4; i++)
		*d++ = *s++;
}

/* RazMem is buffer_a.asm's RazMem: n bytes cleared. */
void
RazMem(void *p, uint32_t n)
{
	memset(p, 0, n);
}

/* CompBuf is buffer_a.asm's CompBuf: 1 if the n bytes are the same, else 0. */
int32_t
CompBuf(void *a, void *b, uint32_t n)
{
	return memcmp(a, b, n) == 0;
}
