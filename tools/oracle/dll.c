#define _GNU_SOURCE
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include "dat.h"
#include "fns.h"
#include "dll.h"

/*
 * The oracles' way to a build's image or a driver, as tools/le2elf.py
 * makes it an ELF32, its objects mapped where they were linked in this
 * 32-bit process (dllmap). An AIL/32 driver's procedures are found in
 * its index, as AIL/32 found them (dllload), and called through
 * dllcall, which keeps EBX, ESI, EDI and EBP whatever the callee does
 * with them. Its port I/O and its CLI
 * and STI fault here: a SIGSEGV handler decodes each (IN and OUT of a
 * byte, a word or a doubleword, at DX or at an immediate port) and does
 * it through the functions the oracle gives, and anything else is
 * fatal. dllcover puts an INT3 on the first byte of each block given,
 * which a SIGTRAP handler takes away as the block first runs, counting
 * it.
 */

void	Expand(void*, void*, uint32_t);

static uint32_t procs[256];
static void (*iout)(uint32_t, uint32_t, int);
static uint32_t (*iin)(uint32_t, int);
static uint32_t *blocks;
static uint8_t *saved;
static int *hits;
static int nblocks;

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

/* The driver's index: its first doubleword's address, pairs of a number and a procedure to -1. */
static void
procindex(void)
{
	uint8_t *p;

	p = (uint8_t*)(uintptr_t)get32((uint8_t*)(uintptr_t)0x10000);
	for (; get32(p) != 0xffffffff; p += 8)
		if (get32(p) >= 100 && get32(p) < 356)
			procs[get32(p)-100] = get32(p+4);
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

/* dllload: an AIL/32 driver mapped, its index read. */
void
dllload(char *path)
{
	dllmap(path);
	procindex();
}

/* dllproc: the driver's procedure number n. */
uint32_t
dllproc(int n)
{
	if (n < 100 || n >= 356 || procs[n-100] == 0)
		fatal("driver has no procedure %d", n);
	return procs[n-100];
}

/*
 * dllcall(fn, n, args): fn called with the n arguments, pushed from the
 * last, its address in EAX as AIL's call_driver jumped to it, its result EAX. The stack and the registers
 * C keeps are put back whatever fn did.
 */
__asm__(
	".text\n"
	".globl dllcall\n"
	".type dllcall, @function\n"
	"dllcall:\n"
	"	pushl %ebp\n"
	"	movl %esp, %ebp\n"
	"	pushl %ebx\n"
	"	pushl %esi\n"
	"	pushl %edi\n"
	"	movl 12(%ebp), %ecx\n"
	"	movl 16(%ebp), %edx\n"
	"1:	decl %ecx\n"
	"	js 3f\n"
	"	pushl (%edx,%ecx,4)\n"
	"	jmp 1b\n"
	"3:	movl 8(%ebp), %eax\n"
	"	call *%eax\n"
	"	leal -12(%ebp), %esp\n"
	"	popl %edi\n"
	"	popl %esi\n"
	"	popl %ebx\n"
	"	popl %ebp\n"
	"	ret\n"
	".size dllcall, .-dllcall\n"
);

/* The value an IN of width w puts in EAX. */
static void
setin(greg_t *g, uint32_t v, int w)
{
	switch (w) {
	case 1:
		g[REG_EAX] = (g[REG_EAX] & ~0xffu) | (v & 0xff);
		break;
	case 2:
		g[REG_EAX] = (g[REG_EAX] & ~0xffffu) | (v & 0xffff);
		break;
	default:
		g[REG_EAX] = v;
		break;
	}
}

/* The driver's I/O and interrupt flag, done for it. */
static void
onfault(int sig, siginfo_t *si, void *ctx)
{
	ucontext_t *uc;
	greg_t *g;
	uint8_t *ip, *op;
	uint32_t mask;
	int w;

	USED(si);
	uc = ctx;
	g = uc->uc_mcontext.gregs;
	ip = (uint8_t*)(uintptr_t)g[REG_EIP];
	op = ip;
	w = 4;
	if (op[0] == 0x66) {
		w = 2;
		op++;
	}
	mask = w == 2 ? 0xffff : 0xffffffff;
	switch (op[0]) {
	case 0xfa:	/* cli */
	case 0xfb:	/* sti */
		if (op != ip)
			break;
		g[REG_EIP] += 1;
		return;
	case 0xee:	/* out dx, al */
		if (op != ip)
			break;
		iout(g[REG_EDX] & 0xffff, g[REG_EAX] & 0xff, 1);
		g[REG_EIP] += 1;
		return;
	case 0xef:	/* out dx, ax or eax */
		iout(g[REG_EDX] & 0xffff, g[REG_EAX] & mask, w);
		g[REG_EIP] += op - ip + 1;
		return;
	case 0xec:	/* in al, dx */
		if (op != ip)
			break;
		setin(g, iin(g[REG_EDX] & 0xffff, 1), 1);
		g[REG_EIP] += 1;
		return;
	case 0xed:	/* in ax or eax, dx */
		setin(g, iin(g[REG_EDX] & 0xffff, w), w);
		g[REG_EIP] += op - ip + 1;
		return;
	case 0xe6:	/* out imm8, al */
		if (op != ip)
			break;
		iout(op[1], g[REG_EAX] & 0xff, 1);
		g[REG_EIP] += 2;
		return;
	case 0xe7:	/* out imm8, ax or eax */
		iout(op[1], g[REG_EAX] & mask, w);
		g[REG_EIP] += op - ip + 2;
		return;
	case 0xe4:	/* in al, imm8 */
		if (op != ip)
			break;
		setin(g, iin(op[1], 1), 1);
		g[REG_EIP] += 2;
		return;
	case 0xe5:	/* in ax or eax, imm8 */
		setin(g, iin(op[1], w), w);
		g[REG_EIP] += op - ip + 2;
		return;
	}
	fprintf(stderr, "oracle: the driver faulted at %08x: %02x %02x %02x %02x\n",
		(uint32_t)g[REG_EIP], ip[0], ip[1], ip[2], ip[3]);
	signal(sig, SIG_DFL);
	abort();
}

/* A block's INT3 met: counted, its byte put back, run again. */
static void
ontrap(int sig, siginfo_t *si, void *ctx)
{
	ucontext_t *uc;
	greg_t *g;
	uint32_t a;
	int lo, hi, m;

	USED(si);
	uc = ctx;
	g = uc->uc_mcontext.gregs;
	a = g[REG_EIP] - 1;
	lo = 0;
	hi = nblocks;
	while (lo < hi) {
		m = (lo + hi) / 2;
		if (blocks[m] < a)
			lo = m + 1;
		else
			hi = m;
	}
	if (lo == nblocks || blocks[lo] != a) {
		fprintf(stderr, "oracle: a trap at %08x, no block's\n", a);
		signal(sig, SIG_DFL);
		abort();
	}
	*(uint8_t*)(uintptr_t)a = saved[lo];
	hits[lo]++;
	g[REG_EIP] = a;
}

/* Any other fault: where. */
static void
ondie(int sig, siginfo_t *si, void *ctx)
{
	ucontext_t *uc;
	greg_t *g;

	USED(si);
	uc = ctx;
	g = uc->uc_mcontext.gregs;
	fprintf(stderr, "oracle: signal %d at %08x\n", sig, (uint32_t)g[REG_EIP]);
	signal(sig, SIG_DFL);
	abort();
}

/* dllio: the driver's port I/O given to out and in (port, value, width in bytes). */
void
dllio(void (*out)(uint32_t, uint32_t, int), uint32_t (*in)(uint32_t, int))
{
	struct sigaction sa;

	iout = out;
	iin = in;
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = onfault;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &sa, NULL);
	sa.sa_sigaction = ondie;
	sigaction(SIGILL, &sa, NULL);
	sigaction(SIGFPE, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
}

/*
 * dllcover: an INT3 on each of the n blocks, in ascending order, their
 * runs counted in h (which may be shared with other processes).
 */
void
dllcover(uint32_t *b, int n, int *h)
{
	struct sigaction sa;
	int i;

	blocks = b;
	nblocks = n;
	hits = h;
	saved = malloc(n);
	if (saved == NULL)
		fatal("out of memory");
	for (i = 0; i < n; i++) {
		if (i > 0 && b[i] <= b[i-1])
			fatal("blocks out of order at %08x", b[i]);
		saved[i] = *(uint8_t*)(uintptr_t)b[i];
		*(uint8_t*)(uintptr_t)b[i] = 0xcc;
	}
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = ontrap;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGTRAP, &sa, NULL);
}
