#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "dat.h"
#include "fns.h"
#include "dll.h"

/*
 * The oracles' way to a build's image or a driver, as tools/le2elf.py
 * makes it an ELF32, its objects mapped where they were linked in this
 * 32-bit process (dllmap).
 */

void	Expand(void*, void*, uint32_t);

uint32_t
get16(uint8_t *p)
{
	return p[0] | p[1]<<8;
}

uint32_t
get32(uint8_t *p)
{
	return p[0] | p[1]<<8 | p[2]<<16 | (uint32_t)p[3]<<24;
}

uint8_t*
readfile(char *path, long *np)
{
	FILE *f;
	uint8_t *b;
	long n;

	f = fopen(path, "rb");
	if (f == NULL)
		fatal("%s: cannot open", path);
	n = -1;
	if (fseek(f, 0, SEEK_END) == 0)
		n = ftell(f);
	if (n < 0 || fseek(f, 0, SEEK_SET) < 0)
		fatal("%s: cannot seek", path);
	b = malloc(n > 0 ? n : 1);
	if (b == NULL || fread(b, 1, n, f) != (size_t)n)
		fatal("%s: cannot read", path);
	fclose(f);
	*np = n;
	return b;
}

/*
 * An HQR file's entries, unpacked, NULL for an empty one (an offset of 0,
 * or the table's last, the file's size), each offset and size held to
 * the file's size before a pointer is made of it.
 */
int
hqr(char *path, uint8_t ***ents)
{
	uint8_t *b, *e, **v;
	uint32_t o, size, packed;
	long n;
	int i, ne;

	b = readfile(path, &n);
	if (n < 4 || get32(b) / 4 == 0 || get32(b) > (uint32_t)n)
		fatal("%s: bad HQR table", path);
	ne = get32(b) / 4;
	v = calloc(ne, sizeof v[0]);
	if (v == NULL)
		fatal("out of memory");
	for (i = 0; i < ne; i++) {
		o = get32(b + 4*i);
		if (o == 0 || o == (uint32_t)n)
			continue;
		if (o > (uint32_t)n || (uint32_t)n - o < 10)
			fatal("%s: bad HQR entry %d", path, i);
		size = get32(b + o);
		packed = get16(b + o + 8) == 0 ? size : get32(b + o + 4);
		if (size > 1u<<28 || packed > (uint32_t)n - o - 10)
			fatal("%s: bad HQR entry %d", path, i);
		e = calloc(1, size + 4096);
		if (e == NULL)
			fatal("out of memory");
		if (get16(b + o + 8) == 0)
			memcpy(e, b + o + 10, size);
		else
			Expand(b + o + 10, e, size);
		v[i] = e;
	}
	free(b);
	*ents = v;
	return ne;
}

/* dllmap: the PT_LOAD segments of an ELF32 mapped at their addresses. */
void
dllmap(char *path)
{
	uint8_t *b, *ph;
	uint32_t phoff, off, va, filesz, memsz, lo, hi;
	long n;
	int i, phnum;
	void *m;

	b = readfile(path, &n);
	if (n < 52 || memcmp(b, "\177ELF\001\001", 6) != 0 || get16(b+18) != 3)
		fatal("%s: not an ELF32 i386 file", path);
	phoff = get32(b+28);
	phnum = get16(b+44);
	if (phoff > (uint32_t)n || (uint32_t)phnum > ((uint32_t)n - phoff) / 32)
		fatal("%s: bad program header", path);
	for (i = 0; i < phnum; i++) {
		ph = b + phoff + 32*i;
		if (get32(ph) != 1)
			continue;
		off = get32(ph+4);
		va = get32(ph+8);
		filesz = get32(ph+16);
		memsz = get32(ph+20);
		if (filesz > memsz || off > (uint32_t)n || filesz > (uint32_t)n - off)
			fatal("%s: bad segment", path);
		lo = va & ~0xfffu;
		hi = (va + memsz + 0xfff) & ~0xfffu;
		m = mmap((void*)(uintptr_t)lo, hi - lo, PROT_READ|PROT_WRITE|PROT_EXEC,
			MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE, -1, 0);
		if (m == MAP_FAILED || m != (void*)(uintptr_t)lo)
			fatal("cannot map %s's segments where they belong", path);
		memcpy((void*)(uintptr_t)va, b + off, filesz);
	}
	free(b);
}
