#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../lib_sys/adeline.h"
#include "lib_svga.h"

/*
 * CalcGraphMsk is graphmsk.asm's CalcGraphMsk: graph num of bank (see
 * graph_a.c) as a mask at mask, its size returned. The mask has the
 * graph's header, then a line at a time a count and runs, alternately
 * of pixels to leave and of pixels to take, the first to leave, 0 if
 * the graph's line starts with pixels. A block is a run to take when
 * its bit 7 is set (10 and 11) or else its bit 6 (01). Counts are bytes.
 */
int32_t
CalcGraphMsk(int32_t num, void *bank, void *mask)
{
	uint8_t *s, *d, *count, op, n, nb, nl, nd, data;
	uint32_t off;

	memcpy(&off, (uint8_t*)bank + 4*num, 4);
	s = (uint8_t*)bank + off;
	d = mask;
	memcpy(d, s, 4);
	nl = s[1];
	s += 4;
	d += 4;
	do {
		nd = 0;
		data = 0;
		count = d++;
		nb = *s++;
		if (*s & 0xC0) {
			*d++ = 0;
			nd++;
		}
		do {
			op = *s++;
			n = (op & 0x3F) + 1;
			if (op & 0x80) {
				data += n;
				s++;
			} else if (op & 0x40) {
				data += n;
				s += n;
			} else {
				if (data != 0) {
					*d++ = data;
					nd++;
					data = 0;
				}
				*d++ = n;
				nd++;
			}
		} while (--nb != 0);
		if (data != 0) {
			*d++ = data;
			nd++;
		}
		*count = nd;
	} while (--nl != 0);
	return d - (uint8_t*)mask;
}
