#define _GNU_SOURCE
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>
#include "dat.h"
#include "fns.h"
#include "dll.h"
#include "abi.h"
#include "pc.h"

/*
 * The PC a DOS build of the game ran on, under DOS/4GW, for the oracles
 * that run its code in this 32-bit process. pcload maps the build's
 * image where it was linked (dll.c's dllmap), writes this process's
 * selectors into its selector fixups, which tools/le2elf.py leaves 0
 * (the interrupt handlers' loads of DGROUP), reads its symbols, and
 * maps DOS memory from 80000h to lowend.
 *
 * What the code does to the machine faults here, its gates and ports
 * being the kernel's, and is done in place on the ucontext's registers:
 *
 *	int n	DPMI's int 31h and DOS's int 21h functions 25h and 35h by
 *		pc.c, any other by the handler the oracle gives for n
 *		(pcint), which takes the registers in a Cpu
 *	in, out, ins, outs
 *		of a byte, a word or a doubleword, at DX or an immediate
 *		port, repeated or single, by the port's handlers (pcport)
 *	cli, sti
 *		skipped: the oracle delivers its interrupts between calls
 *	hlt	in a vector's stub, the previous handler a handler chains
 *		to through its far pointer: the oracle's model of it
 *		(pcbios), if any, then the stub's iretd, counted (chained)
 *
 * DPMI, the functions the builds and the CD's drivers call: 0100h and
 * 0101h (DOS memory, first fit), 0300h (a real-mode int, its register
 * block copied in and back, to the handlers pcint gives, CX counted),
 * 0500h, 0501h and 0502h (linear memory, mapped). The PIC counts its
 * ends of interrupt at 20h. Anything else ends the run, reported, as
 * does any other fault, and a
 * run's alarm. The faults are taken on a stack of their own.
 *
 * acall calls a procedure as the C does and checks its convention,
 * cxcall calls one as the C does with a known ECX, intcall an interrupt
 * handler as the CPU does, with a frame its iretd takes, every register
 * checked. inchild runs a part in a process of its own.
 */

enum
{
	Lowmax = 0x100000,
	Nblock = 64,
	Nport = 16,
	Nimage = 4,
	Sigstack = 1<<16,
	Pattern = 0x5555,	/* a CX left by the code */
};

typedef struct Block Block;
typedef struct Port Port;

/* DOS memory given out, in paragraphs, and the selector DPMI gives with it */
struct Block
{
	uint32_t	seg;
	uint32_t	n;
	uint32_t	sel;
};

/* ports lo to hi and their handlers */
struct Port
{
	uint32_t	lo;
	uint32_t	hi;
	uint32_t	(*in)(uint32_t, int);
	void	(*out)(uint32_t, uint32_t, int);
};

char	*oracle = "oracle";
char	where[Nlabel];
char	what[Nlabel];
int	nfail;
uint32_t	lowend = 0xa0000;
uint32_t	pccs;
int	ncx;
uint32_t	cxlast;
int	neoi;
int	(*pcfault)(void*);

static struct
{
	uintptr_t	va;
	char	*name;
}	*syms;
static int nsyms;
static void (*ints[256])(Cpu*);
static void (*bios[256])(void);
static Port ports[Nport];
static int nports;
static Block blocks[Nblock];
static int nblocks;
static uint32_t pmsel[256], pmoff[256];
static int chains[256];
static uint8_t *stubs;
static uint32_t image[Nimage][2];
static int nimage;
static uint32_t freebytes = 16<<20;	/* DPMI 0500h's largest block */
static uint32_t pcds;	/* its data selector */

/* intcall's */
uintptr_t	intfn;
uint32_t	intebp, intesp, intflags;
uint32_t	intregs[11];	/* EAX to EBP, ESP, EFLAGS, DS, ES after the handler */

void
fatal(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "%s: ", oracle);
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	exit(1);
}

void
fail(char *fmt, ...)
{
	va_list arg;

	if (++nfail > Nshown)
		return;
	va_start(arg, fmt);
	fprintf(stderr, "%s: %s: %s: ", oracle, where, what);
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
}

/* The end of a run in its child, its status the failures. */
void
runend(void)
{
	fflush(stdout);
	fflush(stderr);
	_exit(nfail > 254 ? 255 : nfail + 1);
}

void
setlo(uint32_t *r, uint32_t v)
{
	*r = (*r & 0xffff0000) | (v & 0xffff);
}

void
put16(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
}

void
put32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

/* n bytes at a segment and offset, in DOS memory, or the end of the run. */
uint8_t*
lowmem(uint32_t seg, uint32_t off, uint32_t n)
{
	uint32_t a;

	a = (seg & 0xffff)*16 + (off & 0xffff);
	if (a < Lowbase || a + n > lowend) {
		fail("DOS memory asked at %04x:%04x, outside it", seg & 0xffff, off & 0xffff);
		runend();
	}
	return (uint8_t*)(uintptr_t)a;
}

/* n bytes at a selector or segment and an offset, as an int's handler is given them. */
uint8_t*
cpumem(Cpu *c, uint32_t seg, uint32_t off, uint32_t n)
{
	if (c->real)
		return lowmem(seg, off, n);
	return (uint8_t*)(uintptr_t)off;
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

/* The address of name, or 0 if the map lacks it. */
static uintptr_t
symaddr(char *name)
{
	int i;

	for (i = 0; i < nsyms; i++)
		if (strcmp(syms[i].name, name) == 0)
			return syms[i].va;
	return 0;
}

uintptr_t
lookup(char *name)
{
	uintptr_t a;

	a = symaddr(name);
	if (a == 0)
		fatal("no %s in the map", name);
	return a;
}

/* The image's segments, the executable ones kept as its code, and its selector fixups written. */
static void
segments(char *elf)
{
	FILE *f;
	uint8_t *b, *ph;
	char path[1024], line[256], kind[16];
	unsigned long va, tgt, size, obj, off;
	uint32_t lo, hi, sel, phoff;
	long n;
	int i, k, nseg, phnum;
	uint32_t seg[8][3];

	b = readfile(elf, &n);
	if (n < 52 || memcmp(b, "\177ELF\001\001", 6) != 0 || get16(b+18) != 3)
		fatal("%s: not an ELF32 i386 file", elf);
	phoff = get32(b+28);
	phnum = get16(b+44);
	if (phoff > (uint32_t)n || (uint32_t)phnum > ((uint32_t)n - phoff) / 32)
		fatal("%s: bad program header", elf);
	nseg = 0;
	for (i = 0; i < phnum && nseg < 8; i++) {
		ph = b + phoff + 32*i;
		if (get32(ph) != 1)
			continue;
		lo = get32(ph+8);
		hi = lo + get32(ph+20);
		seg[nseg][0] = lo;
		seg[nseg][1] = hi;
		seg[nseg][2] = get32(ph+24) & 1;
		if ((get32(ph+24) & 1) && nimage < Nimage) {
			image[nimage][0] = lo;
			image[nimage][1] = hi;
			nimage++;
		}
		nseg++;
	}
	free(b);
	snprintf(path, sizeof path, "%s", elf);
	k = strlen(path);
	if (k < 4 || strcmp(path+k-4, ".elf") != 0)
		fatal("%s: not a .elf beside its .fix", elf);
	strcpy(path+k-4, ".fix");
	f = fopen(path, "r");
	if (f == NULL)
		fatal("%s: cannot open", path);
	while (fgets(line, sizeof line, f) != NULL) {
		if (sscanf(line, "%lx %15s %lu %lu %lx %lx", &va, kind, &size, &obj, &off, &tgt) != 6)
			continue;
		if (strcmp(kind, "sel16") != 0)
			continue;
		sel = pcds;
		for (i = 0; i < nseg; i++)
			if (tgt >= seg[i][0] && tgt < seg[i][1] && seg[i][2])
				sel = pccs;
		for (i = 0; i < nseg && (va < seg[i][0] || va >= seg[i][1] || seg[i][1] - va < 2); i++)
			;
		if (i == nseg)
			fatal("%s: a selector's place at %lx outside the image", path, va);
		put16((uint8_t*)(uintptr_t)va, sel);
	}
	fclose(f);
}

void
pcint(int n, void (*fn)(Cpu*))
{
	ints[n & 0xff] = fn;
}

void
pcport(uint32_t lo, uint32_t hi, uint32_t (*in)(uint32_t, int), void (*out)(uint32_t, uint32_t, int))
{
	if (nports == Nport)
		fatal("too many ports");
	ports[nports].lo = lo;
	ports[nports].hi = hi;
	ports[nports].in = in;
	ports[nports].out = out;
	nports++;
}

void
pcbios(int n, void (*fn)(void))
{
	bios[n & 0xff] = fn;
}

/* The protected-mode vector n, as the code set it. */
void
pcvector(int n, uint32_t *sel, uint32_t *off)
{
	*sel = pmsel[n & 0xff];
	*off = pmoff[n & 0xff];
}

/* Times the code chained to the handler vector n held at first. */
int
chained(int n)
{
	return chains[n & 0xff];
}

static Port*
portof(uint32_t p)
{
	int i;

	for (i = 0; i < nports; i++)
		if (p >= ports[i].lo && p <= ports[i].hi)
			return &ports[i];
	fail("port %xh, which the model lacks", p);
	runend();
	return NULL;
}

/* The PIC's command port: ends of interrupt counted. */
static uint32_t
picin(uint32_t p, int w)
{
	USED(p);
	USED(w);
	return 0;
}

static void
picout(uint32_t p, uint32_t v, int w)
{
	USED(w);
	if (p == 0x20 && v == 0x20)
		neoi++;
}

/*
 * A selector for a DOS block, as DOS/4GW gives one apart from the
 * segment: one of the LDT's at ring 3 from 1007h, below every segment
 * of DOS memory here, the first a block leaves free.
 */
static uint32_t
newsel(void)
{
	uint32_t s;
	int i;

	for (s = 0x1007;; s += 8) {
		for (i = 0; i < nblocks && blocks[i].sel != s; i++)
			;
		if (i == nblocks)
			return s;
	}
}

/* DOS memory: n paragraphs, first fit, their segment or 0, its selector into *sel. */
static uint32_t
dosalloc(uint32_t n, uint32_t *largest, uint32_t *sel)
{
	uint32_t s, end, big;
	int i, j;

	*sel = newsel();
	s = Lowbase/16;
	big = 0;
	for (i = 0; i <= nblocks; i++) {
		end = i < nblocks ? blocks[i].seg : lowend/16;
		if (end - s > big)
			big = end - s;
		if (end - s >= n && n > 0 && nblocks < Nblock) {
			for (j = nblocks; j > i; j--)
				blocks[j] = blocks[j-1];
			blocks[i].seg = s;
			blocks[i].n = n;
			blocks[i].sel = *sel;
			nblocks++;
			return s;
		}
		if (i < nblocks)
			s = blocks[i].seg + blocks[i].n;
	}
	*largest = big;
	return 0;
}

/* The DOS block of selector sel, or -1. */
static int
dosblock(uint32_t sel)
{
	int i;

	for (i = 0; i < nblocks; i++)
		if (blocks[i].sel == (sel & 0xffff))
			return i;
	return -1;
}

/* DOS memory for the oracle's own buffers: its linear address. */
uint32_t
lowalloc(uint32_t n)
{
	uint32_t s, big, sel;

	s = dosalloc((n + 15) / 16, &big, &sel);
	if (s == 0)
		fatal("out of DOS memory");
	return s*16;
}

static void
dpmierr(Cpu *c, uint32_t code)
{
	setlo(&c->eax, code);
	c->flags |= 1;
}

/* A real-mode int through the register block at ES:EDI, as DPMI's 0300h runs it. */
static void
realint(Cpu *c)
{
	uint8_t *rmi;
	Cpu r;
	int n;

	n = c->ebx & 0xff;
	if ((c->ecx & 0xffff) != 0) {
		ncx++;
		cxlast = c->ecx & 0xffff;
	}
	rmi = (uint8_t*)(uintptr_t)c->edi;
	r.edi = get32(rmi);
	r.esi = get32(rmi+4);
	r.ebp = get32(rmi+8);
	r.ebx = get32(rmi+16);
	r.edx = get32(rmi+20);
	r.ecx = get32(rmi+24);
	r.eax = get32(rmi+28);
	r.flags = get16(rmi+32);
	r.es = get16(rmi+34);
	r.ds = get16(rmi+36);
	r.real = 1;
	if (ints[n] == NULL) {
		fail("DPMI 0300h for int %02xh, function %04x, which the model lacks", n, r.eax & 0xffff);
		runend();
	}
	ints[n](&r);
	put32(rmi, r.edi);
	put32(rmi+4, r.esi);
	put32(rmi+8, r.ebp);
	put32(rmi+16, r.ebx);
	put32(rmi+20, r.edx);
	put32(rmi+24, r.ecx);
	put32(rmi+28, r.eax);
	put16(rmi+32, r.flags);
	put16(rmi+34, r.es);
	put16(rmi+36, r.ds);
}

/* DPMI, function AX. */
static void
dpmi(Cpu *c)
{
	uint32_t n, s, big, sel;
	uint8_t *b;
	void *m;
	int i;

	c->flags &= ~1;
	switch (c->eax & 0xffff) {
	case 0x0100:
		n = c->ebx & 0xffff;
		s = dosalloc(n, &big, &sel);
		if (s == 0) {
			dpmierr(c, 8);
			setlo(&c->ebx, big);
			return;
		}
		setlo(&c->eax, s);
		setlo(&c->edx, sel);
		return;
	case 0x0101:
		i = dosblock(c->edx);
		if (i < 0) {
			dpmierr(c, 9);
			return;
		}
		nblocks--;
		memmove(blocks+i, blocks+i+1, (nblocks-i) * sizeof blocks[0]);
		return;
	case 0x0300:
		realint(c);
		return;
	case 0x0500:
		b = cpumem(c, c->es, c->edi, 48);
		memset(b, 0xff, 48);
		put32(b, freebytes);
		put32(b+4, freebytes/4096);
		put32(b+8, freebytes/4096);
		return;
	case 0x0501:
		n = (c->ebx & 0xffff) << 16 | (c->ecx & 0xffff);
		m = mmap(NULL, n > 0 ? n : 1, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT, -1, 0);
		if (m == MAP_FAILED) {
			dpmierr(c, 0x8013);
			return;
		}
		s = (uint32_t)(uintptr_t)m;
		setlo(&c->ebx, s >> 16);
		setlo(&c->ecx, s);
		setlo(&c->esi, s >> 16);
		setlo(&c->edi, s);
		return;
	case 0x0502:
		return;
	}
	fail("DPMI function %04x, which the model lacks", c->eax & 0xffff);
	runend();
}

/* DOS's int 21h on the protected-mode vectors, as DOS/4GW extends it: 25h sets AL's, 35h gives it. */
static void
dosvec(Cpu *c)
{
	uint32_t n;

	n = c->eax & 0xff;
	if ((c->eax >> 8 & 0xff) == 0x25) {
		pmsel[n] = c->ds & 0xffff;
		pmoff[n] = c->edx;
		return;
	}
	c->es = pmsel[n];
	c->ebx = pmoff[n];
}

/* The int n the code executed, on the registers. */
static void
doint(int n, Cpu *c)
{
	uint32_t ah;

	ah = c->eax >> 8 & 0xff;
	if (n == 0x31) {
		dpmi(c);
		return;
	}
	if (n == 0x21 && (ah == 0x25 || ah == 0x35)) {
		dosvec(c);
		return;
	}
	if (ints[n] == NULL) {
		fail("int %02xh, function %04x, which the model lacks", n, c->eax & 0xffff);
		runend();
	}
	ints[n](c);
}

/* incode: whether the address a lies in the build's code or in DOS memory, where its code runs. */
int
incode(uint32_t a)
{
	int i;

	for (i = 0; i < nimage; i++)
		if (a >= image[i][0] && a < image[i][1])
			return 1;
	return 0;
}

/* The value an in of width w puts in EAX. */
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

/* ins and outs: count elements of w bytes between DX and ES:EDI or DS:ESI, as DF steps them. */
static void
string(greg_t *g, int out, int w, int rep)
{
	Port *p;
	uint32_t port, n, v;
	uint8_t *m;
	int step;

	port = g[REG_EDX] & 0xffff;
	p = portof(port);
	step = g[REG_EFL] & 0x400 ? -w : w;
	n = rep ? g[REG_ECX] : 1;
	for (; n > 0; n--) {
		if (out) {
			m = (uint8_t*)(uintptr_t)g[REG_ESI];
			v = w == 1 ? m[0] : w == 2 ? get16(m) : get32(m);
			p->out(port, v, w);
			g[REG_ESI] += step;
		} else {
			m = (uint8_t*)(uintptr_t)g[REG_EDI];
			v = p->in(port, w);
			if (w == 1)
				m[0] = v;
			else if (w == 2)
				put16(m, v);
			else
				put32(m, v);
			g[REG_EDI] += step;
		}
		if (rep)
			g[REG_ECX]--;
	}
}

static void
regsin(greg_t *g, Cpu *c)
{
	c->eax = g[REG_EAX];
	c->ebx = g[REG_EBX];
	c->ecx = g[REG_ECX];
	c->edx = g[REG_EDX];
	c->esi = g[REG_ESI];
	c->edi = g[REG_EDI];
	c->ebp = g[REG_EBP];
	c->flags = g[REG_EFL];
	c->ds = g[REG_DS] & 0xffff;
	c->es = g[REG_ES] & 0xffff;
	c->real = 0;
}

static void
regsout(greg_t *g, Cpu *c)
{
	g[REG_EAX] = c->eax;
	g[REG_EBX] = c->ebx;
	g[REG_ECX] = c->ecx;
	g[REG_EDX] = c->edx;
	g[REG_ESI] = c->esi;
	g[REG_EDI] = c->edi;
	g[REG_EBP] = c->ebp;
	g[REG_EFL] = (g[REG_EFL] & ~0x8d5u) | (c->flags & 0x8d5);
	g[REG_DS] = c->ds;
	g[REG_ES] = c->es;
}

/* The previous handler of vector n, chained to: the oracle's model, then its iretd. */
static void
chain(greg_t *g, int n)
{
	uint32_t *sp;

	chains[n]++;
	if (bios[n] != NULL)
		bios[n]();
	sp = (uint32_t*)(uintptr_t)g[REG_ESP];
	g[REG_EIP] = sp[0];
	g[REG_CS] = sp[1] & 0xffff;
	g[REG_EFL] = (g[REG_EFL] & ~0xcd5u) | (sp[2] & 0xcd5);
	g[REG_ESP] += 12;
}

/* A fault: what the code does to the machine done for it, anything else the end of the run. */
static void
onfault(int sig, siginfo_t *si, void *ctx)
{
	ucontext_t *uc;
	greg_t *g;
	uint8_t *ip, *op;
	uint32_t eip, port, mask;
	Cpu c;
	Port *p;
	int w, rep;

	uc = ctx;
	g = uc->uc_mcontext.gregs;
	eip = g[REG_EIP];
	ip = (uint8_t*)(uintptr_t)eip;
	if (sig == SIGSEGV && stubs != NULL && ip >= stubs && ip < stubs + 256 && ip[0] == 0xf4) {
		chain(g, ip - stubs);
		return;
	}
	if (sig == SIGSEGV && incode(eip)) {
		op = ip;
		w = 4;
		rep = 0;
		while (op < ip + 4 && (op[0] == 0x66 || op[0] == 0xf3 || op[0] == 0xf2)) {
			if (op[0] == 0x66)
				w = 2;
			else
				rep = 1;
			op++;
		}
		mask = w == 2 ? 0xffff : 0xffffffff;
		switch (op[0]) {
		case 0xcd:
			regsin(g, &c);
			doint(op[1], &c);
			regsout(g, &c);
			g[REG_EIP] = eip + (op - ip) + 2;
			return;
		case 0xfa:
		case 0xfb:
			g[REG_EIP] = eip + (op - ip) + 1;
			return;
		case 0xe4:
		case 0xe5:
		case 0xec:
		case 0xed:
			if ((op[0] & 1) == 0)
				w = 1;
			port = op[0] & 8 ? g[REG_EDX] & 0xffff : op[1];
			p = portof(port);
			setin(g, p->in(port, w), w);
			g[REG_EIP] = eip + (op - ip) + (op[0] & 8 ? 1 : 2);
			return;
		case 0xe6:
		case 0xe7:
		case 0xee:
		case 0xef:
			if ((op[0] & 1) == 0)
				w = 1;
			port = op[0] & 8 ? g[REG_EDX] & 0xffff : op[1];
			p = portof(port);
			p->out(port, g[REG_EAX] & (w == 1 ? 0xff : mask), w);
			g[REG_EIP] = eip + (op - ip) + (op[0] & 8 ? 1 : 2);
			return;
		case 0x6c:
		case 0x6d:
		case 0x6e:
		case 0x6f:
			string(g, op[0] & 2, op[0] & 1 ? w : 1, rep);
			g[REG_EIP] = eip + (op - ip) + 1;
			return;
		}
	}
	if (pcfault != NULL && pcfault(ctx))
		return;
	fprintf(stderr, "%s: %s: %s: signal %d at %08x, address %08x: %02x %02x %02x %02x\n",
		oracle, where, what, sig, eip, (uint32_t)(uintptr_t)si->si_addr,
		incode(eip) ? ip[0] : 0, incode(eip) ? ip[1] : 0,
		incode(eip) ? ip[2] : 0, incode(eip) ? ip[3] : 0);
	nfail++;
	runend();
}

static void
onalarm(int sig)
{
	USED(sig);
	fprintf(stderr, "%s: %s: %s: runs on\n", oracle, where, what);
	nfail++;
	runend();
}

/*
 * pcload(elf, sym): the build's image mapped, its selectors written,
 * its symbols read, DOS memory mapped, the vectors' stubs made and the
 * faults taken.
 */
void
pcload(char *elf, char *sym)
{
	struct sigaction sa;
	stack_t ss;
	uint16_t cs, ds;
	void *m;
	int i;

	__asm__ volatile("movw %%cs, %0\n\tmovw %%ds, %1" : "=r"(cs), "=r"(ds));
	pccs = cs;
	pcds = ds;
	if (lowend <= Lowbase || lowend > Lowmax)
		fatal("DOS memory to %x", lowend);
	dllmap(elf);
	segments(elf);
	loadsyms(sym);
	m = mmap((void*)Lowbase, lowend - Lowbase, PROT_READ|PROT_WRITE|PROT_EXEC,
		MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE, -1, 0);
	if (m == MAP_FAILED || m != (void*)Lowbase)
		fatal("cannot map DOS memory at %x", Lowbase);
	if (nimage == Nimage)
		fatal("%s: too many segments", elf);
	image[nimage][0] = Lowbase;
	image[nimage][1] = lowend;
	nimage++;
	stubs = mmap(NULL, 4096, PROT_READ|PROT_WRITE|PROT_EXEC,
		MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT, -1, 0);
	if (stubs == MAP_FAILED)
		fatal("cannot map the vectors' stubs");
	memset(stubs, 0xf4, 4096);
	for (i = 0; i < 256; i++) {
		pmsel[i] = pccs;
		pmoff[i] = (uint32_t)(uintptr_t)(stubs + i);
	}
	pcport(0x20, 0x21, picin, picout);
	ss.ss_sp = malloc(Sigstack);
	ss.ss_size = Sigstack;
	ss.ss_flags = 0;
	if (ss.ss_sp == NULL || sigaltstack(&ss, NULL) < 0)
		fatal("cannot give the faults a stack");
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = onfault;
	sa.sa_flags = SA_SIGINFO|SA_ONSTACK;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGILL, &sa, NULL);
	sigaction(SIGFPE, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
	sa.sa_handler = onalarm;
	sa.sa_flags = SA_ONSTACK;
	sigaction(SIGALRM, &sa, NULL);
}

/* The procedure fn of n stack arguments, its EAX, the convention checked. */
int32_t
acall(uintptr_t fn, int n, ...)
{
	va_list arg;
	int32_t a[8], r;
	int i;

	if (n > 8)
		fatal("acall of %d arguments", n);
	va_start(arg, n);
	for (i = 0; i < n; i++)
		a[i] = va_arg(arg, int32_t);
	va_end(arg);
	r = abicall(fn, n, a);
	abiok("the assembly");
	return r;
}

/*
 * cxcall(fn, n, args, cx) calls fn as the C does, with the n words at
 * args on the stack and ECX cx at its entry, where abicall leaves 0:
 * the game's code leaves any value there. It returns fn's EAX and puts
 * EBX, ESI and EDI back, ESP taken from EBP, which fn must keep.
 */
__asm__(
	".text\n"
	".globl cxcall\n"
	"cxcall:\n"
	"	pushl %ebp\n"
	"	movl %esp, %ebp\n"
	"	pushl %ebx\n"
	"	pushl %esi\n"
	"	pushl %edi\n"
	"	movl 12(%ebp), %ecx\n"
	"	movl 16(%ebp), %edx\n"
	"1:	testl %ecx, %ecx\n"
	"	jz 2f\n"
	"	pushl -4(%edx,%ecx,4)\n"
	"	decl %ecx\n"
	"	jmp 1b\n"
	"2:	movl 20(%ebp), %ecx\n"
	"	cld\n"
	"	call *8(%ebp)\n"
	"	leal -12(%ebp), %esp\n"
	"	popl %edi\n"
	"	popl %esi\n"
	"	popl %ebx\n"
	"	popl %ebp\n"
	"	ret\n"
);

/* Each DPMI 0300h since the last asked given CX 0, as the call that made them was given ECX 5555h. */
void
cxnone(void)
{
	if (ncx != 0)
		fail("entered with ECX %04xh, gives DPMI 0300h a CX of %u", Pattern, cxlast);
	ncx = 0;
}

/*
 * intcall(fn) calls the interrupt handler fn as the CPU does, the
 * flags, CS and the return on the stack for its iretd, every register a
 * pattern. intok says what it left changed.
 */
__asm__(
	".text\n"
	".globl intcall\n"
	"intcall:\n"
	"	pushl %ebp\n"
	"	movl %esp, %ebp\n"
	"	pushl %ebx\n"
	"	pushl %esi\n"
	"	pushl %edi\n"
	"	movl 8(%ebp), %eax\n"
	"	movl %eax, intfn\n"
	"	movl %ebp, intebp\n"
	"	movl %esp, intesp\n"
	"	movl $0x55555555, %eax\n"
	"	movl $0x11111111, %ebx\n"
	"	movl $0x66666666, %ecx\n"
	"	movl $0x77777777, %edx\n"
	"	movl $0x22222222, %esi\n"
	"	movl $0x33333333, %edi\n"
	"	movl $0x44444444, %ebp\n"
	"	cld\n"
	"	pushfl\n"
	"	popl intflags\n"
	"	pushfl\n"
	"	pushl %cs\n"
	"	call *intfn\n"
	"	movl %eax, %ss:intregs\n"
	"	movl %ebx, %ss:intregs+4\n"
	"	movl %ecx, %ss:intregs+8\n"
	"	movl %edx, %ss:intregs+12\n"
	"	movl %esi, %ss:intregs+16\n"
	"	movl %edi, %ss:intregs+20\n"
	"	movl %ebp, %ss:intregs+24\n"
	"	movl %esp, %ss:intregs+28\n"
	"	pushfl\n"
	"	popl %ss:intregs+32\n"
	"	movl $0, %ss:intregs+36\n"
	"	movl $0, %ss:intregs+40\n"
	"	movw %ds, %ss:intregs+36\n"
	"	movw %es, %ss:intregs+40\n"
	"	pushl %ss\n"
	"	popl %ds\n"
	"	pushl %ss\n"
	"	popl %es\n"
	"	cld\n"
	"	movl intebp, %ebp\n"
	"	leal -12(%ebp), %esp\n"
	"	popl %edi\n"
	"	popl %esi\n"
	"	popl %ebx\n"
	"	popl %ebp\n"
	"	ret\n"
);

/* Whether the last intcall's handler kept every register, the flags and the stack. */
int
intok(char *who)
{
	static uint32_t want[7] = {
		0x55555555, 0x11111111, 0x66666666, 0x77777777,
		0x22222222, 0x33333333, 0x44444444,
	};
	static char *name[11] = {
		"EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP", "ESP", "EFLAGS", "DS", "ES",
	};
	char buf[128];
	uint32_t broke;
	int i;

	broke = 0;
	for (i = 0; i < 7; i++)
		if (intregs[i] != want[i])
			broke |= 1 << i;
	if (intregs[7] != intesp)
		broke |= 1 << 7;
	if ((intregs[8] ^ intflags) & 0xcd5)
		broke |= 1 << 8;
	if (intregs[9] != pcds)
		broke |= 1 << 9;
	if (intregs[10] != pcds)
		broke |= 1 << 10;
	if (broke == 0)
		return 1;
	buf[0] = 0;
	for (i = 0; i < 11; i++)
		if (broke & 1<<i) {
			strcat(buf, " ");
			strcat(buf, name[i]);
		}
	fail("%s: the handler broke%s", who, buf);
	return 0;
}

/* A run in a process of its own, ended after secs seconds: 1 if it failed. */
int
inchild(char *label, int secs, void (*fn)(void*), void *arg)
{
	pid_t pid;
	int st;

	fflush(stdout);
	fflush(stderr);
	pid = fork();
	if (pid < 0)
		fatal("cannot fork");
	if (pid == 0) {
		snprintf(where, sizeof where, "%s", label);
		snprintf(what, sizeof what, "the start");
		nfail = 0;
		alarm(secs);
		fn(arg);
		fflush(stdout);
		if (nfail > Nshown)
			fprintf(stderr, "%s: %s: %d failed\n", oracle, where, nfail);
		exit(nfail > 0);
	}
	if (waitpid(pid, &st, 0) < 0)
		fatal("cannot wait");
	if (WIFEXITED(st) && WEXITSTATUS(st) == 0)
		return 0;
	if (WIFSIGNALED(st))
		fprintf(stderr, "%s: %s: signal %d\n", oracle, label, WTERMSIG(st));
	return 1;
}
