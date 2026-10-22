#include <stddef.h>

/*
 * Watcom C 9.01's qsort, written from what LBA.EXE's does and proved
 * against it by tools/oracle: the order it leaves equal elements in is
 * what the holomap's globe shows. OpenWatcom's own leaves another, so
 * the DOS build links this one.
 */

static void
swap(char *a, char *b, size_t n)
{
	char t;

	while (n-- > 0) {
		t = *a;
		*a++ = *b;
		*b++ = t;
	}
}

/*
 * watcomqsort is Watcom's qsort (LBA.EXE 0x2e522), its comparisons and
 * exchanges in the same order: the middle element is the pivot, swapped
 * to the front. One pass gathers the elements not above it after it. A
 * partition whose elements all equal the pivot is done. The smaller part
 * is sorted first, the other stacked.
 */
void
watcomqsort(void *a, size_t n, size_t size, int (*cmp)(const void*, const void*))
{
	char *base, *p, *q, *stkbase[32];
	size_t i, nle, lastne, stkn[32];
	int c, sp;

	base = a;
	sp = 0;
	for (;;) {
		while (n < 2) {
			if (sp == 0)
				return;
			sp--;
			base = stkbase[sp];
			n = stkn[sp];
		}
		q = base + size;
		if (n == 2) {
			if (cmp(base, q) > 0)
				swap(base, q, size);
			n = 0;
			continue;
		}
		swap(base, base + (n>>1)*size, size);
		p = base;
		nle = 0;
		lastne = 0;
		for (i = 1; i < n; i++) {
			c = cmp(q, base);
			if (c <= 0) {
				p += size;
				if (++nle != i)
					swap(q, p, size);
			}
			if (c != 0)
				lastne = nle;
			q += size;
		}
		if (nle == n-1 && lastne == 0) {
			n = 0;
			continue;
		}
		if (nle != 0)
			swap(base, p, size);
		n -= nle + 1;
		if (nle != lastne)
			nle = lastne + 1;
		if (nle < n) {
			stkbase[sp] = p + size;
			stkn[sp] = n;
			n = nle;
		} else {
			stkbase[sp] = base;
			stkn[sp] = nle;
			base = p + size;
		}
		sp++;
	}
}
