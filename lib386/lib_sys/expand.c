#include <stdint.h>
#include "adeline.h"
#include "lib_sys.h"

/*
 * Expand is expand.asm's Expand: count bytes of LZS from src into dest.
 * A byte of flags, low bit first, tells each of the next eight items: 1,
 * a byte to copy, or 0, a word, the length less 2 in its low 4 bits and
 * the distance back less 1 in its high 12, of bytes already written,
 * copied one at a time so that a copy may repeat what it writes. The
 * count is only compared with zero: a stream that overshoots it runs on,
 * as the assembly's did.
 */
void
Expand(void *src, void *dest, uint32_t count)
{
	uint8_t *s, *d, *from;
	unsigned flags, w, n;
	int i;

	/*
	 * FIXED: LBA.EXE compares the count with zero after each item
	 * alone, so that a count of 0 expands some 2^32 bytes, past the
	 * stream and the destination. A count of 0 returns at once. Taken
	 * from DotEmu's Android 1.06.
	 */
	if (count == 0)
		return;
	s = src;
	d = dest;
	for (;;) {
		flags = *s++;
		for (i = 0; i < 8; i++, flags >>= 1) {
			if (flags & 1) {
				*d++ = *s++;
				if (--count == 0)
					return;
				continue;
			}
			w = s[0] | s[1]<<8;
			s += 2;
			n = (w & 0xF) + 2;
			count -= n;
			from = d - (w >> 4) - 1;
			while (n-- > 0)
				*d++ = *from++;
			if (count == 0)
				return;
		}
	}
}
