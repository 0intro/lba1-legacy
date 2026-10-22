#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/*
 * oracle: run LBA.EXE's own functions against ours.
 *
 *	oracle lba.elf lba.exe.sym [test...]
 *
 * lba.elf is LBA.EXE as tools/le2elf.py makes it, its objects at their
 * addresses and its fixups applied. They are mapped where they were
 * linked, and its functions called by the names lba.exe.sym gives them.
 * LBA.EXE was built for the /4s convention, the arguments on the stack,
 * popped by the caller, the result in EAX, EBX, ESI, EDI and EBP kept:
 * cdecl calls it, and it calls cdecl comparators, with nothing between.
 * Watcom keeps the stack aligned on 4 bytes only, so this is built with
 * -mstackrealign.
 *
 * The tests (all of them without arguments):
 *
 *	rand	a million values from the first seed, and a thousand after
 *		each of a few srand
 *	qsort	arrays of every length below 40 and some longer, of elements of
 *		1 to 16 bytes with few distinct keys, sorted by both: the
 *		same elements must be compared in the same order, and equal
 *		elements left in the same order (the image's watcomqsort, the
 *		DOS build's, where it has one)
 */

typedef struct Sym Sym;

struct Sym
{
	uint32_t	va;
	char	*name;
};

static Sym *syms;
static int nsyms;
static int fails;

/* ours: sys/rand.c's and sys/qsort.c's */
int	watcomrand(void);
void	watcomsrand(unsigned);
void	watcomqsort(void*, size_t, size_t, int (*)(const void*, const void*));

static void
fatal(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "oracle: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	exit(1);
}

static uint32_t
get16(unsigned char *p)
{
	return p[0] | p[1]<<8;
}

static uint32_t
get32(unsigned char *p)
{
	return p[0] | p[1]<<8 | p[2]<<16 | (uint32_t)p[3]<<24;
}

static unsigned char*
readfile(char *path, long *np)
{
	FILE *f;
	unsigned char *b;
	long n;

	f = fopen(path, "rb");
	if (f == NULL)
		fatal("%s: cannot open", path);
	if (fseek(f, 0, SEEK_END) < 0)
		fatal("%s: cannot seek", path);
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

/* Map the PT_LOAD segments of an ELF32 at their addresses. */
static void
load(char *path)
{
	unsigned char *b, *ph;
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

/* Read a symbol map: lines of address, type and name. */
static void
loadsyms(char *path)
{
	FILE *f;
	char line[512], type[8], name[256];
	unsigned long va;

	f = fopen(path, "r");
	if (f == NULL)
		fatal("%s: cannot open", path);
	while (fgets(line, sizeof line, f) != NULL) {
		if (line[0] == '#' || sscanf(line, "%lx %7s %255s", &va, type, name) != 3)
			continue;
		syms = realloc(syms, (nsyms+1) * sizeof syms[0]);
		if (syms == NULL)
			fatal("%s: out of memory", path);
		syms[nsyms].va = va;
		syms[nsyms].name = strdup(name);
		nsyms++;
	}
	fclose(f);
}

/* The address of a name, or 0: an integer, which C lets become a function pointer. */
static uintptr_t
find(char *name)
{
	int i;

	for (i = 0; i < nsyms; i++)
		if (strcmp(syms[i].name, name) == 0)
			return syms[i].va;
	return 0;
}

static uintptr_t
lookup(char *name)
{
	uintptr_t va;

	va = find(name);
	if (va == 0)
		fatal("no %s in the symbol map", name);
	return va;
}

static void
check(int ok, char *what)
{
	if (!ok) {
		fprintf(stderr, "oracle: %s failed\n", what);
		fails++;
	}
}

static void
testrand(void)
{
	int (*orand)(void);
	void (*osrand)(unsigned);
	static unsigned seeds[] = {0, 1, 2, 12345, 0x7fffffff, 0x80000000, 0xffffffff};
	int i, j, bad;

	orand = (int(*)(void))lookup("rand");
	osrand = (void(*)(unsigned))lookup("srand");
	bad = 0;
	for (i = 0; i < 1000000; i++)
		if (orand() != watcomrand())
			bad++;
	check(bad == 0, "rand: a million values from the first seed");
	for (j = 0; j < (int)(sizeof seeds/sizeof seeds[0]); j++) {
		osrand(seeds[j]);
		watcomsrand(seeds[j]);
		bad = 0;
		for (i = 0; i < 1000; i++)
			if (orand() != watcomrand())
				bad++;
		check(bad == 0, "rand: a thousand values after srand");
	}
}

enum
{
	Maxlen = 4097,
	Maxsize = 16,
	Maxlog = 1 << 20,
};

static unsigned char *sortbase;
static size_t keysize;
static uint64_t cmplog[Maxlog];
static int ncmp;

/* The key of an element: its first byte, or its first WORD as z_sort has it. */
static int
key(const unsigned char *p)
{
	if (keysize == 1)
		return p[0];
	return (int16_t)(p[0] | p[1]<<8);
}

/* Compare by key, logging which two elements were compared. */
static int
cmpkey(const void *a, const void *b)
{
	uint32_t i, j;

	i = (const unsigned char*)a - sortbase;
	j = (const unsigned char*)b - sortbase;
	if (ncmp < Maxlog)
		cmplog[ncmp] = (uint64_t)i<<32 | j;
	ncmp++;
	return key(a) - key(b);
}

/*
 * Sort len elements of size bytes with nkeys distinct keys by both
 * qsorts. 0 if they compared the same elements in the same order and left
 * the same bytes.
 */
static int
sortboth(size_t len, size_t size, int nkeys, uint32_t *r)
{
	static unsigned char x[Maxlen*Maxsize], y[Maxlen*Maxsize];
	static uint64_t olog[Maxlog];
	void (*oqsort)(void*, size_t, size_t, int (*)(const void*, const void*));
	uintptr_t va;
	size_t i;
	int on;

	/* the DOS build links sys/qsort.c's, under its name */
	va = find("watcomqsort");
	if (va == 0)
		va = lookup("qsort");
	oqsort = (void(*)(void*, size_t, size_t, int (*)(const void*, const void*)))va;
	keysize = size == 1 ? 1 : 2;
	for (i = 0; i < len*size; i++) {
		*r = *r*1103515245 + 12345;
		x[i] = *r >> 16;
	}
	for (i = 0; i < len; i++) {
		x[i*size] %= nkeys;
		if (keysize == 2)
			x[i*size+1] = 0;
	}
	memcpy(y, x, len*size);
	sortbase = x;
	ncmp = 0;
	oqsort(x, len, size, cmpkey);
	on = ncmp;
	memcpy(olog, cmplog, (on < Maxlog ? on : Maxlog) * sizeof olog[0]);
	sortbase = y;
	ncmp = 0;
	watcomqsort(y, len, size, cmpkey);
	if (on != ncmp || memcmp(olog, cmplog, (on < Maxlog ? on : Maxlog) * sizeof olog[0]) != 0) {
		fprintf(stderr, "oracle: qsort: %zu elements of %zu bytes, %d keys: not the same comparisons\n",
			len, size, nkeys);
		return -1;
	}
	if (memcmp(x, y, len*size) != 0) {
		fprintf(stderr, "oracle: qsort: %zu elements of %zu bytes, %d keys: not the same order\n",
			len, size, nkeys);
		return -1;
	}
	return 0;
}

static void
testqsort(void)
{
	static size_t sizes[] = {1, 2, 3, 4, 6, 8, 12, 16};
	static size_t longer[] = {64, 100, 255, 256, 512, 1000, Maxlen};
	size_t s, n;
	int nkeys, bad, tried;
	uint32_t r;

	r = 1;
	bad = 0;
	tried = 0;
	for (s = 0; s < sizeof sizes/sizeof sizes[0]; s++) {
		for (nkeys = 1; nkeys <= 256; nkeys *= 4) {
			for (n = 0; n < 40; n++, tried++)
				bad += sortboth(n, sizes[s], nkeys, &r) != 0;
			for (n = 0; n < sizeof longer/sizeof longer[0]; n++, tried++)
				bad += sortboth(longer[n], sizes[s], nkeys, &r) != 0;
		}
	}
	check(bad == 0, "qsort: the same comparisons and order");
	printf("qsort: %d sorts\n", tried);
}

static struct {
	char	*name;
	void	(*fn)(void);
} tests[] = {
	{"rand", testrand},
	{"qsort", testqsort},
};

int
main(int argc, char **argv)
{
	int i, j, ntests;

	if (argc < 3) {
		fprintf(stderr, "usage: oracle lba.elf lba.exe.sym [test...]\n");
		return 2;
	}
	load(argv[1]);
	loadsyms(argv[2]);
	ntests = sizeof tests / sizeof tests[0];
	if (argc == 3)
		for (j = 0; j < ntests; j++)
			tests[j].fn();
	for (i = 3; i < argc; i++) {
		for (j = 0; j < ntests; j++)
			if (strcmp(argv[i], tests[j].name) == 0)
				break;
		if (j == ntests)
			fatal("no test %s", argv[i]);
		tests[j].fn();
	}
	if (fails > 0) {
		fprintf(stderr, "oracle: %d failed\n", fails);
		return 1;
	}
	printf("oracle: ok\n");
	return 0;
}
