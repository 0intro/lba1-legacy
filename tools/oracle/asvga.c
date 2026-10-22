#include <dirent.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "asmoracle.h"

/*
 * The drawing modules, ours and the assembly's, each on its own
 * screens: lines, boxes, blocks, the screen cleared and copied, graphs,
 * masks, fonts, the movies' frames, CopyMask, the menus' fire,
 * SmallSort and ShadeBox, polygons, circles and their fills, the
 * holomap's textured triangles, and the bodies p_ob_iso draws. Each
 * side has its Log and Screen, the same random bytes with 4096 more
 * past each, its line table and its clip box. After each call ours are
 * compared with the assembly's.
 */

enum
{
	Width = 640,
	Height = 480,
	Slack = 4096,	/* before and past each screen, compared too */
	Span = Slack + Width*Height + Slack,
	Ngraph = 64,
	Ours = 0,
	Asm,
};

/* ours, which the game's global.c and its assembly define */
uint8_t	*Log, *Screen, *Phys;
uint32_t	TabOffLine[Height];
int32_t	Screen_X, Screen_Y;
int32_t	ClipXmin, ClipYmin, ClipXmax, ClipYmax;
uint8_t	*BufSpeak;

extern uint8_t	*Imagen;

void	Line(int32_t, int32_t, int32_t, int32_t, int32_t);
void	Box(int32_t, int32_t, int32_t, int32_t, int32_t);
void	CopyBlock(int32_t, int32_t, int32_t, int32_t, void*, int32_t, int32_t, void*);
void	SaveBlock(void*, void*, int32_t, int32_t, int32_t, int32_t);
void	RestoreBlock(void*, void*, int32_t, int32_t, int32_t, int32_t);
void	Cls(void);
void	CopyScreen(void*, void*);
void	AffGraph(int32_t, int32_t, int32_t, void*);
void	GetDxDyGraph(int32_t, int32_t*, int32_t*, void*);
int32_t	CalcGraphMsk(int32_t, void*, void*);
void	CoulMask(int32_t);
void	AffMask(int32_t, int32_t, int32_t, void*);
void	GetDxDyMask(int32_t, int32_t*, int32_t*, void*);
void	CoulFont(int32_t);
void	SetFont(void*, int32_t, int32_t);
int32_t	SizeFont(void*);
int32_t	CarFont(int32_t, int32_t, int32_t);
void	Font(int32_t, int32_t, void*);
void	Mcga_Cls(void);
void	Mcga_Flip(void);
void	DrawFrame(void*, int32_t, int32_t);
void	UpdateFrame(void*, int32_t);
void	BlackFrame(void);
void	CopyFrame(void*);
void	CopyMask(int32_t, int32_t, int32_t, void*, void*);
void	DoFire(int32_t, int32_t);
void	SmallSort(void*, int32_t, int32_t);
void	ShadeBox(int32_t, int32_t, int32_t, int32_t, int32_t);
void	CopyBlockMCGA(int32_t, int32_t, int32_t, int32_t, void*, int32_t, int32_t, void*);
void	AsmFillProp(int16_t*, int32_t, int32_t, int32_t, int32_t);
void	Expand(void*, void*, uint32_t);

typedef struct Svga Svga;

/* a side's screens, and where it keeps them, its line table, its screen's size and its clip box */
struct Svga
{
	uint8_t	*log;
	uint8_t	*screen;
	uint8_t	*logp;
	uint8_t	*screenp;
	uint8_t	*tab;
	uint8_t	*size[2];
	uint8_t	*clip[4];
};

static Svga svga[2];
static char *clipnames[] = {"ClipXmin", "ClipYmin", "ClipXmax", "ClipYmax"};
static int nsvga;	/* the sides set up */

static void
put32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static void
put16(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
}

static void
fill(uint8_t *p, uint32_t n)
{
	uint32_t i;

	for (i = 0; i < n; i++)
		p[i] = rnd();
}

/* A screen and its slack. */
static uint8_t*
screenbuf(void)
{
	uint8_t *p;

	p = malloc(Span);
	if (p == NULL)
		fatal("svga: out of memory");
	return p + Slack;
}

/* Where a screen's slack begins. */
static uint8_t*
span(uint8_t *screen)
{
	return screen - Slack;
}

/* The sides' screens and where they keep them. */
static void
setup(void)
{
	Svga *s;
	int i;

	if (nsvga)
		return;
	s = &svga[Ours];
	s->logp = (uint8_t*)&Log;
	s->screenp = (uint8_t*)&Screen;
	s->tab = (uint8_t*)TabOffLine;
	s->size[0] = (uint8_t*)&Screen_X;
	s->size[1] = (uint8_t*)&Screen_Y;
	s->clip[0] = (uint8_t*)&ClipXmin;
	s->clip[1] = (uint8_t*)&ClipYmin;
	s->clip[2] = (uint8_t*)&ClipXmax;
	s->clip[3] = (uint8_t*)&ClipYmax;
	s = &svga[Asm];
	s->logp = (uint8_t*)lookup("Log");
	s->screenp = (uint8_t*)lookup("Screen");
	s->tab = (uint8_t*)lookup("TabOffLine");
	s->size[0] = (uint8_t*)lookup("Screen_X");
	s->size[1] = (uint8_t*)lookup("Screen_Y");
	for (i = 0; i < 4; i++)
		s->clip[i] = (uint8_t*)lookup(clipnames[i]);
	for (i = 0; i < (int)nelem(svga); i++) {
		svga[i].log = screenbuf();
		svga[i].screen = screenbuf();
	}
	nsvga = nelem(svga);
}

/* Side i's pointers, line table and size as the game sets them. */
static void
setside(int i)
{
	Svga *s;
	int y;

	s = &svga[i];
	put32(s->logp, (uint32_t)(uintptr_t)s->log);
	put32(s->screenp, (uint32_t)(uintptr_t)s->screen);
	for (y = 0; y < Height; y++)
		put32(s->tab + 4*y, y*Width);
	put32(s->size[0], Width);
	put32(s->size[1], Height);
}

/* The sides' screens, the same random bytes. */
static void
screens(void)
{
	int i;

	setup();
	fill(span(svga[Ours].log), Span);
	fill(span(svga[Ours].screen), Span);
	for (i = 0; i < nsvga; i++) {
		if (i != Ours) {
			memcpy(span(svga[i].log), span(svga[Ours].log), Span);
			memcpy(span(svga[i].screen), span(svga[Ours].screen), Span);
		}
		setside(i);
	}
}

static void
setclip(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	int i;

	for (i = 0; i < nsvga; i++) {
		put32(svga[i].clip[0], x0);
		put32(svga[i].clip[1], y0);
		put32(svga[i].clip[2], x1);
		put32(svga[i].clip[3], y1);
	}
}

/* A clip box within the screen, the whole of it once in four. */
static void
randclip(void)
{
	int32_t x0, y0;

	if (rndn(4) == 0) {
		setclip(0, 0, Width-1, Height-1);
		return;
	}
	x0 = rndn(Width);
	y0 = rndn(Height);
	setclip(x0, y0, rndr(x0, Width-1), rndr(y0, Height-1));
}

/* Whether the assembly's screens differ from ours: which in *what, and where from the screen's start in *at. */
static int
differs(char **what, int32_t *at)
{
	uint8_t *a, *b;
	int32_t k;

	a = svga[Asm].log;
	b = svga[Ours].log;
	*what = "Log";
	if (memcmp(span(a), span(b), Span) == 0) {
		a = svga[Asm].screen;
		b = svga[Ours].screen;
		*what = "Screen";
		if (memcmp(span(a), span(b), Span) == 0)
			return 0;
	}
	for (k = -Slack; a[k] == b[k]; k++)
		;
	*at = k;
	return 1;
}

/* Whether the assembly's screens are ours, else fail with what was called. */
static int
samescreens(char *name, char *args)
{
	char *what;
	int32_t k;

	if (differs(&what, &k)) {
		fail("%s %s: %s at byte %d (%d,%d)", name, args, what, k, k%Width, k/Width);
		return 0;
	}
	return 1;
}

/* A coordinate: far out, around the screen, or on it. */
static int32_t
coord(int32_t size)
{
	switch (rndn(4)) {
	case 0:
		return rndr(-16000, 16000);
	case 1:
		return rndr(-2000, size+2000);
	}
	return rndn(size);
}

/* how a drawing in a child ended: drawn, or by a fault or the alarm */
enum
{
	Drawn,
	Fault,
	Hang,
	Killed,
};

static char *ends[] = {"drawn", "a fault", "no end", "another signal"};
static struct itimerval quarter = {{0, 0}, {0, 250000}};	/* a child's alarm */

/* How a child ended, from its status. */
static int
ended(int status)
{
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return Drawn;
	if (WIFSIGNALED(status) && WTERMSIG(status) == SIGFPE)
		return Fault;
	if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM)
		return Hang;
	return Killed;
}

/* Line from far on side i's screen, in a child: how it ended, the screen it drew in out. */
static int
farline(int i, uintptr_t fn, int32_t *a, uint8_t *out)
{
	int status;
	pid_t pid;

	pid = fork();
	if (pid < 0)
		fatal("line: cannot fork");
	if (pid == 0) {
		signal(SIGFPE, SIG_DFL);	/* a fault ends the child by its signal, AddressSanitizer's handler or not */
		setitimer(ITIMER_REAL, &quarter, NULL);
		if (i == Ours)
			Line(a[0], a[1], a[2], a[3], a[4]);
		else
			((void(*)(int32_t, int32_t, int32_t, int32_t, int32_t))fn)(a[0], a[1], a[2], a[3], a[4]);
		memcpy(out, span(svga[i].log), Span);
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid)
		fatal("line: cannot wait");
	return ended(status);
}

/* A line through a random point p of the clip box, its ends q and 2p-q, q within lim. */
static void
throughbox(int32_t *a, int32_t lim)
{
	int32_t px, py;

	px = rndr(ClipXmin, ClipXmax);
	py = rndr(ClipYmin, ClipYmax);
	a[0] = rndr(-lim, lim);
	a[1] = rndr(-lim, lim);
	a[2] = 2*px - a[0];
	a[3] = 2*py - a[1];
}

/*
 * Lines from up to 70000 out, half of them through the clip box, where
 * the 16-bit clipping of LBA.EXE's assembly faulted or never ended.
 * Each is drawn by ours and by the assembly's, each in a child ended by
 * a fault or a quarter second's alarm, and how they ended and what they
 * drew are compared. A line the assembly faults on is passed by and
 * counted: there the C may do otherwise, as a FIXED: comment says.
 */
static void
farlines(uintptr_t fn)
{
	uint8_t *c, *d;
	int32_t a[5];
	int n[4][4], r, i, j, oc, oa;

	c = mmap(NULL, Span, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	d = mmap(NULL, Span, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	if (c == MAP_FAILED || d == MAP_FAILED)
		fatal("line: cannot map");
	memset(n, 0, sizeof n);
	for (r = 0; r < Rounds/100; r++) {
		randclip();
		if (r%2 == 0)
			for (i = 0; i < 4; i++)
				a[i] = rndr(-70000, 70000);
		else
			throughbox(a, 70000);
		a[4] = rnd();
		oc = farline(Ours, 0, a, c);
		oa = farline(Asm, fn, a, d);
		n[oc][oa]++;
		if (oa == Fault)
			continue;
		if (oc != oa)
			fail("Line from far %d %d %d %d, clip %d %d %d %d: ours %s, the assembly's %s",
				a[0], a[1], a[2], a[3], ClipXmin, ClipYmin, ClipXmax, ClipYmax, ends[oc], ends[oa]);
		else if (oc == Drawn && memcmp(c, d, Span) != 0)
			fail("Line from far %d %d %d %d, clip %d %d %d %d: drawn otherwise",
				a[0], a[1], a[2], a[3], ClipXmin, ClipYmin, ClipXmax, ClipYmax);
	}
	printf("line: %d from far, ended (ours, the assembly's)", r);
	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
			if (n[i][j] > 0)
				printf(", %s and %s %d", ends[i], ends[j], n[i][j]);
	printf("\n");
	munmap(c, Span);
	munmap(d, Span);
}

/* A million lines in and out of random clip boxes, then a thousand from far (farlines). */
void
testline(void)
{
	uintptr_t s;
	int32_t a[5];
	char args[128];
	int r;

	s = lookup("Line");
	screens();
	for (r = 0; r < 10*Rounds; r++) {
		randclip();
		a[0] = coord(Width);
		a[1] = coord(Height);
		a[2] = rndn(8) == 0 ? a[0] : coord(Width);
		a[3] = rndn(8) == 0 ? a[1] : coord(Height);
		a[4] = rnd();
		acall(s, 5, a[0], a[1], a[2], a[3], a[4]);
		Line(a[0], a[1], a[2], a[3], a[4]);
		snprintf(args, sizeof args, "%d %d %d %d, clip %d %d %d %d", a[0], a[1], a[2], a[3],
			ClipXmin, ClipYmin, ClipXmax, ClipYmax);
		if (!samescreens("Line", args))
			break;
	}
	printf("line: %d rounds\n", r);
	if (r == 10*Rounds)
		farlines(s);
}

void
testbox(void)
{
	uintptr_t s;
	int32_t x0, y0, x1, y1, col;
	char args[128];
	int r;

	s = lookup("Box");
	screens();
	for (r = 0; r < Rounds; r++) {
		randclip();
		x0 = rndr(-100, Width+100);
		x1 = rndr(x0, Width+200);
		y0 = rndr(-100, Height+100);
		y1 = rndr(y0, Height+200);
		col = rnd();
		acall(s, 5, x0, y0, x1, y1, col);
		Box(x0, y0, x1, y1, col);
		snprintf(args, sizeof args, "%d %d %d %d, clip %d %d %d %d", x0, y0, x1, y1,
			ClipXmin, ClipYmin, ClipXmax, ClipYmax);
		if (!samescreens("Box", args))
			break;
	}
	printf("box: %d rounds\n", r);
}

/* Log or Screen of side i. */
static uint8_t*
buf(int i, int log)
{
	return log ? svga[i].log : svga[i].screen;
}

void
testblock(void)
{
	static uint8_t abuf[Width*Height+Slack], cbuf[Width*Height+Slack];
	uintptr_t cb, sb, rb;
	int32_t x0, y0, x1, y1, xd, yd;
	int r, src, dst;
	char args[128];

	cb = lookup("CopyBlock");
	sb = lookup("SaveBlock");
	rb = lookup("RestoreBlock");
	screens();
	fill(abuf, sizeof abuf);
	memcpy(cbuf, abuf, sizeof abuf);
	for (r = 0; r < Rounds; r++) {
		x0 = rndn(Width);
		x1 = rndr(x0, Width-1);
		y0 = rndn(Height);
		y1 = rndr(y0, Height-1);
		xd = rndn(Width - (x1-x0));
		yd = rndn(Height - (y1-y0));
		src = rnd() & 1;
		dst = rnd() & 1;
		acall(cb, 8, x0, y0, x1, y1, (int32_t)(uintptr_t)buf(Asm, src), xd, yd, (int32_t)(uintptr_t)buf(Asm, dst));
		CopyBlock(x0, y0, x1, y1, buf(Ours, src), xd, yd, buf(Ours, dst));
		snprintf(args, sizeof args, "%d %d %d %d of %s to %d %d of %s", x0, y0, x1, y1,
			src ? "Log" : "Screen", xd, yd, dst ? "Log" : "Screen");
		if (!samescreens("CopyBlock", args))
			break;
		acall(sb, 6, (int32_t)(uintptr_t)buf(Asm, src), (int32_t)(uintptr_t)abuf, x0, y0, x1, y1);
		SaveBlock(buf(Ours, src), cbuf, x0, y0, x1, y1);
		if (memcmp(abuf, cbuf, sizeof abuf) != 0) {
			fail("SaveBlock %s: ours and the assembly's differ", args);
			break;
		}
		x0 = rndn(Width);
		y0 = rndn(Height);
		x1 = rndr(x0, Width-1);
		y1 = rndr(y0, Height-1);
		acall(rb, 6, (int32_t)(uintptr_t)buf(Asm, dst), (int32_t)(uintptr_t)abuf, x0, y0, x1, y1);
		RestoreBlock(buf(Ours, dst), cbuf, x0, y0, x1, y1);
		if (!samescreens("RestoreBlock", args))
			break;
	}
	printf("block: %d rounds\n", r);
}

void
testscreen(void)
{
	uintptr_t cls, cs, mcls, mflip;
	uint8_t *aphys;
	int r;

	cls = lookup("Cls");
	cs = lookup("CopyScreen");
	mcls = lookup("Mcga_Cls");
	mflip = lookup("Mcga_Flip");
	aphys = (uint8_t*)lookup("Phys");
	screens();
	for (r = 0; r < 100; r++) {
		fill(span(svga[Ours].log), Span);
		fill(span(svga[Ours].screen), Span);
		memcpy(span(svga[Asm].log), span(svga[Ours].log), Span);
		memcpy(span(svga[Asm].screen), span(svga[Ours].screen), Span);
		acall(cs, 2, (int32_t)(uintptr_t)svga[Asm].log, (int32_t)(uintptr_t)svga[Asm].screen);
		CopyScreen(svga[Ours].log, svga[Ours].screen);
		if (!samescreens("CopyScreen", "Log to Screen"))
			break;
		fill(span(svga[Ours].log), Span);
		memcpy(span(svga[Asm].log), span(svga[Ours].log), Span);
		acall(cls, 0);
		Cls();
		if (!samescreens("Cls", "Log"))
			break;
		fill(span(svga[Ours].log), Span);
		memcpy(span(svga[Asm].log), span(svga[Ours].log), Span);
		acall(mcls, 0);
		Mcga_Cls();
		if (!samescreens("Mcga_Cls", "Log"))
			break;
		/* Phys is each side's Screen, which Mcga_Flip copies Log into */
		fill(span(svga[Ours].log), Span);
		fill(span(svga[Ours].screen), Span);
		memcpy(span(svga[Asm].log), span(svga[Ours].log), Span);
		memcpy(span(svga[Asm].screen), span(svga[Ours].screen), Span);
		put32(aphys, (uint32_t)(uintptr_t)svga[Asm].screen);
		Phys = svga[Ours].screen;
		acall(mflip, 0);
		Mcga_Flip();
		if (!samescreens("Mcga_Flip", "Log to Phys"))
			break;
	}
	printf("screen: %d rounds\n", r);
}

/*
 * A bank of n random graphs at bank, each line's blocks adding up to
 * its width, of kinds 00 (pixels left), 01 (copied), 10 (repeated)
 * and, if kinds is 4, 11 (copied, which CalcGraphMsk takes for
 * repeated). Colour 0 is among the pixels. The graphs start gap bytes
 * past the bank's table of offsets.
 */
static void
graphbank(uint8_t *bank, int n, int maxw, int kinds, int32_t gap)
{
	uint8_t *p, *nb;
	int i, j, w, h, y, x, k, op;

	p = bank + 4*n + gap;
	for (i = 0; i < n; i++) {
		put32(bank + 4*i, p - bank);
		w = rndr(1, maxw);
		h = rndr(1, 120);
		*p++ = w;
		*p++ = h;
		*p++ = rndn(64);
		*p++ = rndn(64);
		for (y = 0; y < h; y++) {
			nb = p++;
			*nb = 0;
			for (x = 0; x < w; x += k) {
				k = rndr(1, 64);
				if (k > w-x)
					k = w - x;
				op = rndn(kinds);
				*p++ = op<<6 | (k - 1);
				if (op == 2)
					*p++ = rndn(4) == 0 ? 0 : rnd();
				else if (op != 0)
					for (j = 0; j < k; j++)
						*p++ = rndn(4) == 0 ? 0 : rnd();
				(*nb)++;
			}
		}
	}
}

enum
{
	Maskbuf = 40 << 10,	/* a mask of a graph 255 by 120, and slack */
	Far = 1 << 24,	/* the gap of a far bank, its offsets past 24 bits */
};

/*
 * CalcGraphMsk of graph i of bank on both sides, into buffers of the
 * same random bytes: whether the assembly's mask is ours.
 */
static int
calcmask(uintptr_t fn, uint8_t *bank, int i)
{
	static uint8_t buf[2][Maskbuf];
	int32_t n[2];

	fill(buf[Ours], Maskbuf);
	memcpy(buf[Asm], buf[Ours], Maskbuf);
	n[Ours] = CalcGraphMsk(i, bank, buf[Ours]);
	n[Asm] = acall(fn, 3, i, (int32_t)(uintptr_t)bank, (int32_t)(uintptr_t)buf[Asm]);
	if (n[Asm] != n[Ours] || memcmp(buf[Asm], buf[Ours], Maskbuf) != 0) {
		fail("CalcGraphMsk of graph %d: ours and the assembly's differ", i);
		return 0;
	}
	return 1;
}

/*
 * A bank of masks made of n random graphs by our CalcGraphMsk, their
 * graphs at graphs, the masks gap bytes past the table.
 */
static void
maskbank(uint8_t *masks, uint8_t *graphs, int n, int maxw, int32_t gap)
{
	uint32_t off;
	int i;

	graphbank(graphs, n, maxw, 3, 0);
	off = 4*n + gap;
	for (i = 0; i < n; i++) {
		put32(masks + 4*i, off);
		off += CalcGraphMsk(i, graphs, masks + off);
	}
}

/*
 * Graphs with blocks of kind 11 or without, a bank of each kind in
 * turn. The game's graphs hold the three other kinds alone. A bank in
 * four lies past 16 MB, where an offset needs its fourth byte.
 */
void
testgraph(void)
{
	static uint8_t near[1<<21], far[Far + (1<<21)];
	uint8_t *bank;
	uintptr_t ag, dd, cg;
	int32_t num, x, y, adx, ady, cdx, cdy;
	char args[128];
	int r, i;

	ag = lookup("AffGraph");
	dd = lookup("GetDxDyGraph");
	cg = lookup("CalcGraphMsk");
	screens();
	bank = near;
	for (r = 0; r < Rounds; r++) {
		if (r % 500 == 0) {
			bank = r % 2000 == 1500 ? far : near;
			graphbank(bank, Ngraph, 255, r % 1000 == 0 ? 4 : 3, bank == far ? Far : 0);
			for (i = 0; i < Ngraph; i++)
				if (!calcmask(cg, bank, i))
					break;
			if (i < Ngraph)
				break;
		}
		randclip();
		num = rndn(Ngraph);
		x = rndr(-300, Width+50);
		y = rndr(-200, Height+50);
		acall(ag, 4, num, x, y, (int32_t)(uintptr_t)bank);
		AffGraph(num, x, y, bank);
		snprintf(args, sizeof args, "%d at %d %d, clip %d %d %d %d", num, x, y,
			ClipXmin, ClipYmin, ClipXmax, ClipYmax);
		if (!samescreens("AffGraph", args))
			break;
		acall(dd, 4, num, (int32_t)(uintptr_t)&adx, (int32_t)(uintptr_t)&ady, (int32_t)(uintptr_t)bank);
		GetDxDyGraph(num, &cdx, &cdy, bank);
		if (adx != cdx || ady != cdy) {
			fail("GetDxDyGraph %d: ours %d %d, the assembly's %d %d", num, cdx, cdy, adx, ady);
			break;
		}
	}
	printf("graph: %d rounds\n", r);
}

/* Masks of random graphs, a bank in four past 16 MB, as testgraph's. */
void
testmask(void)
{
	static uint8_t graphs[1<<21], near[1<<21], far[Far + (1<<21)];
	uint8_t *masks;
	uintptr_t cm, am, dd;
	int32_t num, x, y, col, adx, ady, cdx, cdy;
	char args[128];
	int r;

	cm = lookup("CoulMask");
	am = lookup("AffMask");
	dd = lookup("GetDxDyMask");
	screens();
	masks = near;
	for (r = 0; r < Rounds; r++) {
		if (r % 500 == 0) {
			masks = r % 2000 == 1500 ? far : near;
			maskbank(masks, graphs, Ngraph, 255, masks == far ? Far : 0);
		}
		if (r % 50 == 0) {
			col = rndn(8) == 0 ? 0 : (int32_t)rnd();
			acall(cm, 1, col);
			CoulMask(col);
		}
		randclip();
		num = rndn(Ngraph);
		x = rndr(-300, Width+50);
		y = rndr(-200, Height+50);
		acall(am, 4, num, x, y, (int32_t)(uintptr_t)masks);
		AffMask(num, x, y, masks);
		snprintf(args, sizeof args, "%d at %d %d, clip %d %d %d %d", num, x, y,
			ClipXmin, ClipYmin, ClipXmax, ClipYmax);
		if (!samescreens("AffMask", args))
			break;
		acall(dd, 4, num, (int32_t)(uintptr_t)&adx, (int32_t)(uintptr_t)&ady, (int32_t)(uintptr_t)masks);
		GetDxDyMask(num, &cdx, &cdy, masks);
		if (adx != cdx || ady != cdy) {
			fail("GetDxDyMask %d: ours %d %d, the assembly's %d %d", num, cdx, cdy, adx, ady);
			break;
		}
	}
	printf("mask: %d rounds\n", r);
}

void
testfont(void)
{
	static uint8_t graphs[1<<23], font[1<<22];
	uintptr_t cf, sf, szf, cr, f;
	uint8_t s[64];
	int32_t x, y, il, is, a, c, col;
	char args[128];
	int r, i, n;

	cf = lookup("CoulFont");
	sf = lookup("SetFont");
	szf = lookup("SizeFont");
	cr = lookup("CarFont");
	f = lookup("Font");
	screens();
	maskbank(font, graphs, 256, 24, 0);
	il = 1;
	is = 10;
	for (r = 0; r < Rounds; r++) {
		if (r % 100 == 0) {
			il = rndn(8) == 0 ? rndr(256, 259) : rndr(0, 3);
			is = rndn(20);
			acall(sf, 3, (int32_t)(uintptr_t)font, il, is);
			SetFont(font, il, is);
			col = rnd();
			acall(cf, 1, col);
			CoulFont(col);
		}
		randclip();
		n = rndn(sizeof s);
		for (i = 0; i < n; i++)
			s[i] = rndn(5) == 0 ? ' ' : rndr(1, 255);
		s[n] = 0;
		x = rndr(-200, Width);
		y = rndr(-100, Height);
		acall(f, 3, x, y, (int32_t)(uintptr_t)s);
		Font(x, y, s);
		snprintf(args, sizeof args, "a string of %d at %d %d, interleave %d, space %d", n, x, y, il, is);
		if (!samescreens("Font", args))
			break;
		a = acall(szf, 1, (int32_t)(uintptr_t)s);
		c = SizeFont(s);
		if (a != c) {
			fail("SizeFont %s: ours %d, the assembly's %d", args, c, a);
			break;
		}
		col = rndr(1, 255);
		a = acall(cr, 3, x, y, col);
		c = CarFont(x, y, col);
		snprintf(args, sizeof args, "%d at %d %d, interleave %d, space %d", col, x, y, il, is);
		if (a != c) {
			fail("CarFont %s: ours %d, the assembly's %d", args, c, a);
			break;
		}
		if (!samescreens("CarFont", args))
			break;
	}
	printf("font: %d rounds\n", r);
}

/* the movies' passes that draw (playfla.c) */
enum
{
	FlaLc = 6,
	FlaBlack = 7,
	FlaBrun = 8,
	FlaCopy = 9,
	Maxrun = 127,	/* the longest run of the movies */
};

/* A line of runs as DrawFrame reads them, w pixels and runs of max at most: a negative count copies, another fills. */
static uint8_t*
drawline(uint8_t *p, int w, int max)
{
	uint8_t *nb;
	int k;

	nb = p++;
	*nb = 0;
	while (w > 0 && *nb < 255 && rndn(16) != 0) {
		if (rnd() & 1) {
			k = rndr(1, max);
			if (k > w)
				k = w;
			*p++ = -k;
			fill(p, k);
			p += k;
		} else {
			k = rndn(max+1);
			if (k > w)
				k = w;
			*p++ = k;
			*p++ = rnd();
		}
		w -= k;
		(*nb)++;
	}
	return p;
}

/* A line of runs as UpdateFrame reads them, each a skip and a count: a positive count copies, another fills. */
static uint8_t*
updateline(uint8_t *p, int w, int max)
{
	uint8_t *nb;
	int k, skip;

	nb = p++;
	*nb = 0;
	while (w > 0 && *nb < 255 && rndn(16) != 0) {
		skip = rndn(w < 256 ? w : 256);
		*p++ = skip;
		w -= skip;
		if (rnd() & 1) {
			k = rndn(max+1);
			if (k > w)
				k = w;
			*p++ = k;
			fill(p, k);
			p += k;
		} else {
			k = rndr(1, max);
			if (k > w)
				k = w;
			*p++ = -k;
			*p++ = rnd();
		}
		w -= k;
		(*nb)++;
	}
	return p;
}

/* Whether both sides drew pass kind of a movie's frame at q alike. */
static int
flapass(uintptr_t *s, int kind, uint8_t *q, int32_t dy, char *args)
{
	switch (kind) {
	case FlaLc:
		acall(s[0], 2, (int32_t)(uintptr_t)q, 320);
		UpdateFrame(q, 320);
		return samescreens("UpdateFrame", args);
	case FlaBrun:
		acall(s[1], 3, (int32_t)(uintptr_t)q, 320, dy);
		DrawFrame(q, 320, dy);
		return samescreens("DrawFrame", args);
	case FlaBlack:
		acall(s[2], 0);
		BlackFrame();
		return samescreens("BlackFrame", args);
	case FlaCopy:
		acall(s[3], 1, (int32_t)(uintptr_t)q);
		CopyFrame(q);
		return samescreens("CopyFrame", args);
	}
	return 1;
}

/* Every frame of the movies in gamedir's FLA drawn by both sides: how many, or -1 if they differ. */
static int
movies(uintptr_t *s)
{
	DIR *dir;
	struct dirent *e;
	uint8_t *b, *p, *q;
	long n;
	int32_t dy;
	uint32_t f, nframe;
	int k, npass, frames, ok;
	char path[1024], args[1100];

	snprintf(path, sizeof path, "%s/FLA", gamedir);
	dir = opendir(path);
	if (dir == NULL)
		fatal("fla: cannot open %s", path);
	frames = 0;
	ok = 1;
	while (ok && (e = readdir(dir)) != NULL) {
		k = strlen(e->d_name);
		if (k < 4 || strcmp(e->d_name+k-4, ".FLA") != 0)
			continue;
		snprintf(path, sizeof path, "%s/FLA/%s", gamedir, e->d_name);
		b = readfile(path, &n);
		nframe = get32(b+6);
		dy = get16(b+14);
		p = b + 20 + 4*get16(b+16);
		for (f = 0; ok && f < nframe && p+6 <= b+n; f++) {
			npass = p[0];
			q = p + 6;
			p = q + get32(p+2);
			for (k = 0; ok && k < npass; k++) {
				snprintf(args, sizeof args, "%s frame %u pass %d", e->d_name, f, k);
				ok = flapass(s, q[0], q+4, dy, args);
				q += 4 + get16(q+2);
			}
			frames++;
		}
		free(b);
	}
	closedir(dir);
	return ok ? frames : -1;
}

/*
 * adfli_a's frames: with gamedir every frame of the game's movies, then
 * random frames of the FLA's kinds, one in eight past the movies' 200
 * lines, runs of 127 at most as the movies have them. Then a line of a
 * run of 128, which both
 * copy or fill whole.
 */
void
testfla(void)
{
	static uint8_t frame[1<<20];
	uintptr_t s[4];
	uint8_t *p;
	int32_t dy, y, n;
	char args[128];
	int r, i, frames;

	s[0] = lookup("UpdateFrame");
	s[1] = lookup("DrawFrame");
	s[2] = lookup("BlackFrame");
	s[3] = lookup("CopyFrame");
	screens();
	if (gamedir != NULL) {
		frames = movies(s);
		if (frames < 0) {
			printf("fla: 0 rounds\n");
			return;
		}
		if (frames == 0)
			fatal("fla: no movies in %s", gamedir);
		printf("fla: %d frames of the movies\n", frames);
	}
	for (r = 0; r < Rounds/10; r++) {
		dy = rndr(1, 200);
		for (p = frame, i = 0; i < dy; i++)
			p = drawline(p, 320, Maxrun);
		snprintf(args, sizeof args, "%d lines", dy);
		if (!flapass(s, FlaBrun, frame, dy, args))
			break;
		/*
		 * a frame in eight past the movies' 200 lines, from a line past
		 * 255 or of 256 lines and more, the log holding 960 of 320
		 */
		if (rndn(8) != 0) {
			y = rndn(200);
			n = rndr(1, 200-y);
		} else if (rnd() & 1) {
			y = rndr(256, 900);
			n = rndr(1, 960-y);
		} else {
			y = rndn(200);
			n = rndr(256, 960-y);
		}
		put16(frame, y);
		put16(frame+2, n);
		for (p = frame + 4, i = 0; i < n; i++)
			p = updateline(p, 320, Maxrun);
		snprintf(args, sizeof args, "%d lines from %d", n, y);
		if (!flapass(s, FlaLc, frame, 0, args))
			break;
		fill(frame, 320*200);
		if (!flapass(s, FlaCopy, frame, 0, "a picture"))
			break;
		if (rndn(8) == 0 && !flapass(s, FlaBlack, frame, 0, ""))
			break;
	}
	if (r == Rounds/10) {
		frame[0] = 1;
		frame[1] = 0x80;
		fill(frame+2, 128);
		if (flapass(s, FlaBrun, frame, 1, "a copy of 128")) {
			put16(frame, 0);
			put16(frame+2, 1);
			frame[4] = 1;
			frame[5] = 0;
			frame[6] = 0x80;
			frame[7] = rnd();
			flapass(s, FlaLc, frame, 0, "a fill of 128");
		}
	}
	printf("fla: %d rounds\n", r);
}

/*
 * The bricks of LBA_BRK.HQR in gamedir banked as grille.c banks them,
 * their offsets first, and their masks as CreateMaskGph makes them: how
 * many.
 */
static int
loadbricks(uint8_t **bankp, uint8_t **masksp)
{
	char path[1024];
	uint8_t *b, *bank, *masks;
	uint32_t n, i, o, size, total, off;
	long fsize;
	int nb;

	snprintf(path, sizeof path, "%s/LBA_BRK.HQR", gamedir);
	b = readfile(path, &fsize);
	n = get32(b) / 4;
	nb = 0;
	total = 0;
	for (i = 0; i < n; i++) {
		o = get32(b + 4*i);
		if (o == 0 || o + 10 > (uint32_t)fsize || o + 10 + get32(b + o + 4) > (uint32_t)fsize)
			continue;
		nb++;
		total += get32(b + o);
	}
	bank = malloc(4*(nb+1) + total + 64);
	masks = malloc(4*(nb+1) + total + 64);
	if (bank == NULL || masks == NULL)
		fatal("cpymask: out of memory");
	off = 4 * (nb + 1);
	put32(bank, off);
	nb = 0;
	for (i = 0; i < n; i++) {
		o = get32(b + 4*i);
		if (o == 0 || o + 10 > (uint32_t)fsize || o + 10 + get32(b + o + 4) > (uint32_t)fsize)
			continue;
		size = get32(b + o);
		if (get16(b + o + 8) == 0)
			memcpy(bank + off, b + o + 10, size);
		else
			Expand(b + o + 10, bank + off, size);
		off += size;
		put32(bank + 4*++nb, off);
	}
	free(b);
	off = 4 * (nb + 1);
	put32(masks, off);
	for (i = 0; i < (uint32_t)nb; i++) {
		off += CalcGraphMsk(i, bank, masks + off);
		put32(masks + 4*(i+1), off);
	}
	*bankp = bank;
	*masksp = masks;
	return nb;
}

/*
 * CopyMask of a one-line mask of runs at x 100, y 100, from Screen,
 * through every clip box whose edges fall on its run boundaries or a
 * pixel beside them (all, for a mask 8 wide at most), the rows about
 * the line compared. The count of the clip boxes, or -1 if a side
 * differed.
 */
static int
maskline(uintptr_t cm, uint8_t *runs, int nrun)
{
	static uint8_t bank[8 + 4 + 1 + 64];
	int32_t w, e[64], ne, a, b, i, k;
	uint8_t *la, *lc;
	char args[160];
	int n;

	for (w = 0, i = 0; i < nrun; i++)
		w += runs[i];
	if (w < 1 || w > 255)
		return 0;
	put32(bank, 4);
	bank[4] = w;
	bank[5] = 1;
	bank[6] = 0;
	bank[7] = 0;
	bank[8] = nrun;
	memcpy(bank + 9, runs, nrun);
	ne = 0;
	if (w <= 8)
		for (i = 0; i <= w; i++)
			e[ne++] = i;
	else
		for (i = 0, k = 0; i <= nrun; i++) {
			for (a = -1; a <= 1; a++)
				if (k+a >= 0 && k+a <= w && ne < (int)nelem(e))
					e[ne++] = k+a;
			if (i < nrun)
				k += runs[i];
		}
	n = 0;
	for (a = 0; a < ne; a++)
		for (b = 0; b < ne; b++) {
			if (e[b] <= e[a])
				continue;
			setclip(100 + e[a], 0, 100 + e[b] - 1, Height-1);
			/* the rows apart from Screen, so that a copy shows wherever it lands */
			for (i = 0; i < 3*Width; i++)
				svga[Asm].log[99*Width + i] = svga[Ours].log[99*Width + i] = svga[Ours].screen[99*Width + i] ^ 0xA5;
			acall(cm, 5, 0, 100, 100, (int32_t)(uintptr_t)bank, (int32_t)(uintptr_t)svga[Asm].screen);
			CopyMask(0, 100, 100, bank, svga[Ours].screen);
			la = svga[Asm].log + 99*Width;
			lc = svga[Ours].log + 99*Width;
			if (memcmp(la, lc, 3*Width) != 0) {
				snprintf(args, sizeof args, "a line of %d runs, %d wide, clip %d to %d", nrun, w, e[a], e[b]-1);
				fail("CopyMask %s: the logs differ", args);
				return -1;
			}
			n++;
		}
	return n;
}

/*
 * With -x, one-line masks of every sequence of up to 6 runs 8 pixels wide
 * at most, runs of 0 among them, and of every sequence of two to four runs
 * of 0, 1, 2, 126 to 129, 254 and 255 pixels, through their clip boxes
 * (maskline): the edges of the state machine for a clipped line, its
 * unsigned compares.
 */
static int
masklines(uintptr_t cm)
{
	static uint8_t lens[] = {0, 1, 2, 126, 127, 128, 129, 254, 255};
	uint8_t runs[8];
	int32_t seq, k, nrun, i, t, nlen;
	int n, boxes, r;

	screens();
	n = boxes = 0;
	/* runs of 0 to 8 pixels summing to 8 at most, up to 6 of them, by counting in base 9 */
	for (nrun = 1; nrun <= 6; nrun++)
		for (seq = 0; ; seq++) {
			for (t = seq, i = 0; i < nrun; i++, t /= 9)
				runs[i] = t % 9;
			if (t != 0)
				break;
			for (k = 0, i = 0; i < nrun; i++)
				k += runs[i];
			if (k < 1 || k > 8)
				continue;
			if ((r = maskline(cm, runs, nrun)) < 0)
				return 0;
			n++;
			boxes += r;
		}
	nlen = nelem(lens);
	for (nrun = 2; nrun <= 4; nrun++)
		for (seq = 0; ; seq++) {
			for (t = seq, i = 0; i < nrun; i++, t /= nlen)
				runs[i] = lens[t % nlen];
			if (t != 0)
				break;
			if ((r = maskline(cm, runs, nrun)) < 0)
				return 0;
			n += r > 0;
			boxes += r;
		}
	printf("cpymask: %d lines of runs through %d clip boxes\n", n, boxes);
	return 1;
}

/*
 * CopyMask as the game calls it, with gamedir through the masks of its
 * bricks at the columns 24 apart that DrawOverBrick goes through, the
 * bricks meeting a random clip box, and through random masks
 * (CalcGraphMsk's of random graphs), whole and clipped. From Screen as
 * the game copies, or from another screen. The random masks are 127
 * wide at most, or 255 past the game's range, a bank of each in turn.
 */
void
testcpymask(void)
{
	static uint8_t graphs[1<<21], masks[1<<21], *other[2], *bricks, *brickmasks;
	static int nbrick;
	uintptr_t cm, cg;
	uint8_t *src[2], *bank, *l, *sc;
	int32_t num, x, y, k;
	char args[128];
	int r, i, fromscreen, game;

	cm = lookup("CopyMask");
	cg = lookup("CalcGraphMsk");
	screens();
	if (other[Ours] == NULL) {
		other[Ours] = screenbuf();
		other[Asm] = screenbuf();
		if (gamedir != NULL)
			nbrick = loadbricks(&bricks, &brickmasks);
	}
	for (i = 0; i < nbrick; i++)
		if (!calcmask(cg, bricks, i)) {
			printf("cpymask: 0 rounds\n");
			return;
		}
	fill(span(other[Ours]), Span);
	memcpy(span(other[Asm]), span(other[Ours]), Span);
	for (r = 0; r < Rounds; r++) {
		if (r % 500 == 0)
			maskbank(masks, graphs, Ngraph, r/500 % 2 ? 255 : 127, 0);
		/* the copies leave the log as their source: every 16 rounds a log apart from both */
		if (r%16 == 0)
			for (i = 0; i < nsvga; i++) {
				l = span(svga[i].log);
				sc = span(svga[i].screen);
				for (k = 0; k < Span; k++)
					l[k] = sc[k] ^ 0xA5;
			}
		randclip();
		game = nbrick > 0 && r%2 == 0;
		if (game) {
			bank = brickmasks;
			num = rndn(nbrick);
			x = rndr((ClipXmin+24)/24 - 1, (ClipXmax+24)/24)*24 - 24;
			y = rndr(ClipYmin - 37, ClipYmax);
		} else {
			bank = masks;
			num = rndn(Ngraph);
			x = rndr(-300, Width+50);
			y = rndr(-200, Height+50);
		}
		fromscreen = rndn(8) != 0;
		src[Ours] = fromscreen ? svga[Ours].screen : other[Ours];
		src[Asm] = fromscreen ? svga[Asm].screen : other[Asm];
		acall(cm, 5, num, x, y, (int32_t)(uintptr_t)bank, (int32_t)(uintptr_t)src[Asm]);
		CopyMask(num, x, y, bank, src[Ours]);
		snprintf(args, sizeof args, "%s %d at %d %d from %s, clip %d %d %d %d", game ? "brick" : "mask",
			num, x, y, fromscreen ? "Screen" : "another screen", ClipXmin, ClipYmin, ClipXmax, ClipYmax);
		if (!samescreens("CopyMask", args))
			break;
	}
	if (gamedir != NULL)
		printf("cpymask: %d bricks\n", nbrick);
	printf("cpymask: %d rounds\n", r);
	if (exhaustive && r == Rounds)
		masklines(cm);
}

enum
{
	Fire = 320 * 50,	/* an image of the fire, 320 wide */
	Firebuf = 2*Fire + Slack,	/* the fire, its next step, slack */
};

/*
 * The menus' fire: steps at random lines and colours, from a fire
 * random, cold (zeros, as the game mostly has it, its bottom lines
 * cooling below 0 into fuel) or cold but for a few hot points.
 */
void
testfire(void)
{
	static uint8_t *fire[2];
	uintptr_t df;
	uint8_t *aimagen;
	int32_t lig, col, k;
	char args[128], *what;
	int r, i;

	df = lookup("DoFire");
	aimagen = (uint8_t*)lookup("Imagen");
	screens();
	if (fire[Ours] == NULL) {
		fire[Ours] = malloc(Firebuf);
		fire[Asm] = malloc(Firebuf);
		if (fire[Ours] == NULL || fire[Asm] == NULL)
			fatal("fire: out of memory");
	}
	BufSpeak = fire[Ours];
	put32((uint8_t*)lookup("BufSpeak"), (uint32_t)(uintptr_t)fire[Asm]);
	for (r = 0; r < Rounds/10; r++) {
		if (r % 100 == 0) {
			fill(fire[Ours], Firebuf);
			if (r/100 % 3 != 0)
				memset(fire[Ours], 0, 2*Fire);
			if (r/100 % 3 == 2)
				for (i = 0; i < 64; i++)
					fire[Ours][rndn(2*Fire)] = rnd();
			memcpy(fire[Asm], fire[Ours], Firebuf);
		}
		lig = rndn(Height-49);
		col = rnd();
		acall(df, 2, lig, col);
		DoFire(lig, col);
		snprintf(args, sizeof args, "line %d colour %d", lig, col);
		if (differs(&what, &k) || memcmp(fire[Asm], fire[Ours], Firebuf) != 0
		|| get32(aimagen) != (uint32_t)(uintptr_t)fire[Asm] || Imagen != fire[Ours]) {
			fail("DoFire %s: ours and the assembly's differ", args);
			break;
		}
	}
	printf("fire: %d rounds\n", r);
}

/*
 * func's SmallSort, ShadeBox and CopyBlockMCGA: lists of entries of
 * several sizes with repeated keys, among them counts of 1 or less down
 * to INT32_MIN, which func.asm returns on, boxes in and out of random
 * clip boxes, at their edges and one past them, on a log taken from the
 * screen every 16 rounds, and boxes of the screen copied to the start
 * of the log, 320 bytes a line, xd and yd at random. The game sorts
 * T_SORT's 12 bytes alone, and copies the movies' 320 by 200.
 */
void
testfunc(void)
{
	static uint8_t al[64*64], cl[64*64];
	static int32_t sizes[] = {4, 5, 6, 8, 10, 12, 14, 16, 20, 34, 64};
	static int32_t nones[] = {INT32_MIN, -1, 0, 1};
	uintptr_t ss, sb, mc;
	int32_t n, size, x0, y0, x1, y1, xd, yd, dec, i;
	char args[128];
	int r;

	ss = lookup("SmallSort");
	sb = lookup("ShadeBox");
	mc = lookup("CopyBlockMCGA");
	screens();
	for (r = 0; r < Rounds; r++) {
		if (r%100 == 0)
			n = nones[r/100 % nelem(nones)];
		else
			n = rndn(64);
		size = sizes[rndn(nelem(sizes))];
		fill(al, sizeof al);
		for (i = 0; i < n; i++) {
			al[i*size] = rndn(6) - 3;
			al[i*size+1] = al[i*size] & 0x80 ? 0xFF : 0;
		}
		memcpy(cl, al, sizeof al);
		acall(ss, 3, (int32_t)(uintptr_t)al, n, size);
		SmallSort(cl, n, size);
		if (memcmp(al, cl, sizeof al) != 0) {
			fail("SmallSort %d of %d bytes: ours and the assembly's differ", n, size);
			break;
		}
		randclip();
		x0 = rndr(-100, Width+100);
		x1 = rndr(x0, Width+200);
		y0 = rndr(-100, Height+100);
		y1 = rndr(y0, Height+200);
		/* a box in four starts or ends at a clip edge, or one past it */
		switch (rndn(16)) {
		case 0:
			x0 = ClipXmax + rndn(2);
			x1 = rndr(x0, Width+200);
			break;
		case 1:
			x1 = ClipXmin - rndn(2);
			x0 = rndr(x1-100, x1);
			break;
		case 2:
			y0 = ClipYmax + rndn(2);
			y1 = rndr(y0, Height+200);
			break;
		case 3:
			y1 = ClipYmin - rndn(2);
			y0 = rndr(y1-100, y1);
			break;
		}
		dec = rndn(20);
		/* the shades reach 0 within a few hundred rounds: a fresh log */
		if (r%16 == 0)
			for (i = 0; i < nsvga; i++)
				memcpy(span(svga[i].log), span(svga[i].screen), Span);
		acall(sb, 5, x0, y0, x1, y1, dec);
		ShadeBox(x0, y0, x1, y1, dec);
		snprintf(args, sizeof args, "%d %d %d %d by %d, clip %d %d %d %d", x0, y0, x1, y1, dec,
			ClipXmin, ClipYmin, ClipXmax, ClipYmax);
		if (!samescreens("ShadeBox", args))
			break;
		x0 = rndn(Width);
		x1 = rndr(x0, Width-1);
		y0 = rndn(Height);
		y1 = rndr(y0, Height-1);
		xd = rnd();
		yd = rnd();
		acall(mc, 8, x0, y0, x1, y1, (int32_t)(uintptr_t)svga[Asm].screen, xd, yd,
			(int32_t)(uintptr_t)svga[Asm].log);
		CopyBlockMCGA(x0, y0, x1, y1, svga[Ours].screen, xd, yd, svga[Ours].log);
		snprintf(args, sizeof args, "%d %d %d %d", x0, y0, x1, y1);
		if (!samescreens("CopyBlockMCGA", args))
			break;
	}
	printf("func: %d rounds\n", r);
}

enum
{
	Npoly = 97,	/* TabPoly's words in LBA.EXE, which the tests compare */
	Nline = 480,
	Ntab = 6,	/* the line tables */
};

int32_t	ComputePoly(void);
int32_t	ComputeSphere(int32_t, int32_t, int32_t);
void	FillVertic(int32_t, int32_t);
void	SetFillDetails(int32_t);

extern int16_t	TypePoly, NbPolyPoints, Xmin, Ymin, Xmax, Ymax;
extern int16_t	TabPoly[], TabVerticG[], TabVerticD[], TabCoulG[], TabCoulD[], TabX1[], TabY1[];

typedef struct Poly Poly;

/* a side's polygon: its type, points and frame, and its line tables */
struct Poly
{
	uint8_t	*type;
	uint8_t	*npoints;
	uint8_t	*frame[4];
	uint8_t	*tabpoly;
	uint8_t	*tab[Ntab];	/* TabVerticG, TabVerticD, TabCoulG, TabCoulD, TabX1, TabY1 */
};

static Poly poly[2];
static char *fillnames[] = {
	"Triste 0", "Triste 1", "Triste 2", "Tele 0", "Tele 1", "Tele 2",
	"Copper 0", "Copper 1", "Copper 2", "Bopper 0", "Bopper 1", "Bopper 2",
	"Marbre 0", "Marbre 1", "Marbre 2", "Trans 0", "Trans 1", "Trans 2",
	"Trame 0", "Trame 1", "Trame 2", "Gouraud 0", "Gouraud 1", "Gouraud 2",
	"Dith 0", "Dith 1", "Dith 2",
};
static char *framenames[] = {"Xmin", "Ymin", "Xmax", "Ymax"};
static char *tabnames[Ntab] = {"TabVerticG", "TabVerticD", "TabCoulG", "TabCoulD", "TabX1", "TabY1"};

static void
setuppoly(void)
{
	static int done;
	Poly *p;
	int i;

	if (done)
		return;
	done = 1;
	p = &poly[Ours];
	p->type = (uint8_t*)&TypePoly;
	p->npoints = (uint8_t*)&NbPolyPoints;
	p->frame[0] = (uint8_t*)&Xmin;
	p->frame[1] = (uint8_t*)&Ymin;
	p->frame[2] = (uint8_t*)&Xmax;
	p->frame[3] = (uint8_t*)&Ymax;
	p->tabpoly = (uint8_t*)TabPoly;
	p->tab[0] = (uint8_t*)TabVerticG;
	p->tab[1] = (uint8_t*)TabVerticD;
	p->tab[2] = (uint8_t*)TabCoulG;
	p->tab[3] = (uint8_t*)TabCoulD;
	p->tab[4] = (uint8_t*)TabX1;
	p->tab[5] = (uint8_t*)TabY1;
	p = &poly[Asm];
	p->type = (uint8_t*)lookup("TypePoly");
	p->npoints = (uint8_t*)lookup("NbPolyPoints");
	for (i = 0; i < 4; i++)
		p->frame[i] = (uint8_t*)lookup(framenames[i]);
	for (i = 0; i < Ntab; i++)
		p->tab[i] = (uint8_t*)lookup(tabnames[i]);
	p->tabpoly = (uint8_t*)lookup("TabPoly");
	/* TabPoly alike on both sides, as the tests compare it */
	memcpy(p->tabpoly, TabPoly, 2*Npoly);
}

/*
 * What of the assembly's frame, TabPoly and, if tabs, line tables
 * differs from ours, NULL if nothing. After a circle, Xmin and Xmax are
 * left out: ComputeSphere leaves them as they were, and the fill reads
 * Ymin and Ymax alone. TabPoly holds the polygon, clipped or closed by
 * a copy of its first point, and past it what earlier polygons left,
 * which a point the clipping adds keeps as its colour but for Gouraud.
 */
static char*
polydiffers(int circle, int tabs)
{
	int k;

	for (k = 0; k < 4; k++)
		if ((!circle || k%2 == 1) && memcmp(poly[Asm].frame[k], poly[Ours].frame[k], 2) != 0)
			return framenames[k];
	if (memcmp(poly[Asm].tabpoly, poly[Ours].tabpoly, 2*Npoly) != 0)
		return "TabPoly";
	for (k = 0; tabs && k < 4; k++)
		if (memcmp(poly[Asm].tab[k], poly[Ours].tab[k], 2*Nline) != 0)
			return tabnames[k];
	return NULL;
}

static int
byangle(const void *a, const void *b)
{
	double x, y;

	x = *(double*)a;
	y = *(double*)b;
	return x < y ? -1 : x > y;
}

/*
 * n points of an ellipse about cx,cy, at random angles in order, one
 * way round or the other: a convex polygon. Its colour words, for
 * Gouraud, are three times in four as the game makes them, a colour
 * of a band of 16 for each point, else anything.
 */
static void
convex(int16_t *p, int n)
{
	double t[20];
	int32_t cx, cy, rx, ry, b;
	int i, back, band;

	cx = rndn(4) == 0 ? rndr(-300, Width+300) : rndr(0, Width);
	cy = rndn(4) == 0 ? rndr(-300, Height+300) : rndr(0, Height);
	rx = rndn(4) == 0 ? rndn(600) : rndn(120);
	ry = rndn(4) == 0 ? rndn(600) : rndn(120);
	for (i = 0; i < n; i++)
		t[i] = rnd() / 4294967296.0 * 2*M_PI;
	qsort(t, n, sizeof t[0], byangle);
	back = rnd() & 1;
	band = rndn(4) != 0;
	b = 16 * rndn(16);
	for (i = 0; i < n; i++) {
		if (band)
			p[3*i] = b + rndn(16);
		else
			p[3*i] = rndn(4) == 0 ? (int16_t)rnd() : (int16_t)rndn(256);
		p[3*i+1] = cx + (int32_t)lround(rx*cos(t[back ? n-1-i : i]));
		p[3*i+2] = cy + (int32_t)lround(ry*sin(t[back ? n-1-i : i]));
	}
}

/*
 * Seven points about a random place, a W 210 wide and 150 high at a
 * scale of 1/2 to 2: a polygon that a side of the clip box may cut four
 * times, its clipped points still within the 32 of LBA.EXE's buffers.
 */
static void
wshape(int16_t *p)
{
	static int16_t w[7][2] = {
		{-10, 100}, {50, -50}, {100, 100}, {150, -50}, {200, 100}, {200, 200}, {-10, 200},
	};
	int32_t cx, cy, sc;
	int i;

	cx = rndr(-200, Width);
	cy = rndr(-200, Height);
	sc = rndr(32, 128);
	for (i = 0; i < 7; i++) {
		p[3*i] = rndn(256);
		p[3*i+1] = cx + w[i][0]*sc/64;
		p[3*i+2] = cy + w[i][1]*sc/64;
	}
}

typedef struct Farpoly Farpoly;

/* a polygon of farpolygons: its points, x and y, and its clip box */
struct Farpoly
{
	int	n;
	int16_t	pt[3][2];
	int32_t	clip[4];
};

/*
 * Polygons past the game's inputs, where LBA.EXE is defined: two with
 * an edge over 32767 lines or columns across a side of the box, whose
 * crossing the clipping reckons in 16 bits, two with an edge of 32768
 * columns and three with an edge ending left of x 0, in boxes reaching
 * so far, and six whose points all lie at an end of 16 bits, where the
 * frame starts from: a point alone, or three flat on it and cut by a
 * side. Every quotient of their clipping fits in 16 bits, and every
 * line they leave lies on the screen.
 */
static Farpoly farpolys[] = {
	{3, {{-10, -20000}, {10, 20000}, {10, 200}}, {0, 0, Width-1, Height-1}},
	{3, {{-2, 110}, {32767, 100}, {32767, 120}}, {0, 0, Width-1, Height-1}},
	{3, {{-1, 0}, {32767, 2}, {-1, 2}}, {-1, 0, 32767, Height-1}},
	{3, {{-1, 0}, {32767, 3}, {-1, 3}}, {-1, 0, 32767, Height-1}},
	{3, {{-1, 0}, {0, 2}, {-1, 2}}, {-1, 0, Width-1, Height-1}},
	{3, {{0, 383}, {-191, 0}, {-191, 383}}, {-191, 0, Width-1, Height-1}},
	{3, {{1, 28}, {-1, 0}, {-1, 28}}, {-1, 0, Width-1, Height-1}},
	{1, {{32767, 0}}, {0, 0, Width-1, Height-1}},
	{1, {{-32768, 0}}, {0, 0, Width-1, Height-1}},
	{1, {{0, 32767}}, {0, 0, Width-1, Height-1}},
	{1, {{0, -32768}}, {0, 0, Width-1, Height-1}},
	{3, {{-5, 32767}, {5, 32767}, {0, 32767}}, {0, 0, Width-1, 32767}},
	{3, {{-5, -32768}, {5, -32768}, {0, -32768}}, {0, -32768, Width-1, Height-1}},
};

/*
 * ComputePoly alone on each polygon of farpolys, flat and its colours
 * 0, on both sides: whether all were alike, their results, frames,
 * TabPoly and the lines of those that show.
 */
static int
farpolygons(uintptr_t cp)
{
	Farpoly *f;
	int16_t pts[3*3];
	int32_t ra, rc;
	char *what;
	int i, k;

	for (i = 0; i < (int)nelem(farpolys); i++) {
		f = &farpolys[i];
		setclip(f->clip[0], f->clip[1], f->clip[2], f->clip[3]);
		memset(pts, 0, sizeof pts);
		for (k = 0; k < f->n; k++) {
			pts[3*k+1] = f->pt[k][0];
			pts[3*k+2] = f->pt[k][1];
		}
		TypePoly = 0;
		memcpy(poly[Asm].type, &TypePoly, 2);
		memcpy(TabPoly, pts, 2*3*f->n);
		memcpy(poly[Asm].tabpoly, pts, 2*3*f->n);
		NbPolyPoints = f->n;
		memcpy(poly[Asm].npoints, &NbPolyPoints, 2);
		ra = acall(cp, 0);
		rc = ComputePoly();
		what = ra != rc ? "the result" : polydiffers(0, rc != 0);
		if (what != NULL) {
			fail("ComputePoly of far polygon %d, %d points from %d,%d, clip %d %d %d %d: ours %d, the assembly's %d, %s",
				i, f->n, f->pt[0][0], f->pt[0][1], f->clip[0], f->clip[1], f->clip[2], f->clip[3], rc, ra, what);
			return 0;
		}
	}
	setclip(0, 0, Width-1, Height-1);
	return 1;
}

/*
 * s_poly's ComputePoly and ComputeSphere and s_fillv's FillVertic and
 * SetFillDetails: random convex polygons of 1 to 20 points, as faces
 * of the game's bodies project, and circles, in and out of random clip
 * boxes, every type of fill at every level of detail, -1 among them,
 * which both take for 2. The frame, TabPoly and the line tables are
 * compared, then the screen. Past the game's inputs, where LBA.EXE is
 * defined: polygons in the shape of a W, boxes empty across x, as
 * SetClip keeps them, a side 65536 off for a polygon, which both read
 * in 16 bits, ComputePoly alone under a type of 8000h or more, and
 * circles of a radius past 32767 whose lines past 32767 read, in 16
 * bits, out of the box. A circle across x 0 in a box whose right side
 * is -1, which takes its lines from x 0 to 65535, is filled by Triste,
 * Copper and Trame alone, which LBA.EXE draws nothing of there. Then
 * the polygons at the ends of 16 bits of farpolygons.
 */
void
testpoly(void)
{
	uintptr_t cp, cs, fv, sd;
	int32_t ra, rc, type, col, level, a[3], v[4];
	int16_t pts[3*20];
	char *what, args[160];
	int r, n, k, circle, kind, fill;

	cp = lookup("ComputePoly");
	cs = lookup("ComputeSphere");
	fv = lookup("FillVertic");
	sd = lookup("SetFillDetails");
	screens();
	setuppoly();
	level = 0;
	for (r = 0; r < 10*Rounds; r++) {
		if (r % 100 == 0) {
			level = rndr(-1, 3);
			acall(sd, 1, level);
			SetFillDetails(level);
		}
		circle = rndn(5) == 0;
		kind = rndn(16);
		if (rnd() & 1)
			setclip(0, 0, Width-1, Height-1);
		else
			randclip();
		if (kind == 0) {
			v[0] = rndr(1, Width-1);
			v[1] = rndn(Height);
			setclip(v[0], v[1], rndr(0, v[0]-1), rndr(v[1], Height-1));
		}
		type = rndn(9);
		fill = 1;
		if (circle) {
			a[0] = rndn(4) == 0 ? rndr(-200, Width+200) : rndr(0, Width);
			a[1] = rndn(4) == 0 ? rndr(-200, Height+200) : rndr(0, Height);
			a[2] = rndn(4) == 0 ? rndn(400) : rndn(60);
			if (kind == 1) {
				setclip(0, 0, Width-1, Height-1);
				a[0] = rndr(59800, 60200);
				a[1] = rndr(200, 280);
				a[2] = rndr(64800, 65200);
			} else if (kind == 2) {
				v[1] = rndn(Height);
				setclip(0, v[1], -1, rndr(v[1], Height-1));
				a[2] = rndr(1, 60);
				a[0] = rndr(-a[2], a[2]-1);
				type = (int[]){0, 2, 6}[rndn(3)];
			}
			TypePoly = type;
			memcpy(poly[Asm].type, &TypePoly, 2);
			ra = acall(cs, 3, a[0], a[1], a[2]);
			rc = ComputeSphere(a[0], a[1], a[2]);
			snprintf(args, sizeof args, "circle %d %d %d, type %d, clip %d %d %d %d", a[0], a[1], a[2],
				type, ClipXmin, ClipYmin, ClipXmax, ClipYmax);
			what = "ComputeSphere";
		} else {
			if (kind == 1) {
				n = 7;
				wshape(pts);
			} else {
				n = rndr(1, 20);
				convex(pts, n);
			}
			if (kind == 2) {
				v[0] = ClipXmin;
				v[1] = ClipYmin;
				v[2] = ClipXmax;
				v[3] = ClipYmax;
				v[rndn(4)] += rnd() & 1 ? 65536 : -65536;
				setclip(v[0], v[1], v[2], v[3]);
			}
			TypePoly = type;
			if (kind == 3) {
				TypePoly = rndr(0x8000, 0xFFFF);
				fill = 0;
			}
			memcpy(poly[Asm].type, &TypePoly, 2);
			memcpy(TabPoly, pts, sizeof pts);
			memcpy(poly[Asm].tabpoly, pts, sizeof pts);
			NbPolyPoints = n;
			memcpy(poly[Asm].npoints, &NbPolyPoints, 2);
			ra = acall(cp, 0);
			rc = ComputePoly();
			snprintf(args, sizeof args, "polygon of %d points at %d,%d, type %d, clip %d %d %d %d", n,
				pts[1], pts[2], (uint16_t)TypePoly, ClipXmin, ClipYmin, ClipXmax, ClipYmax);
			what = "ComputePoly";
		}
		/* the frame and TabPoly whatever the result, the tables for what shows */
		if (ra != rc || polydiffers(circle, rc != 0) != NULL) {
			fail("%s %s: ours %d, the assembly's %d, %s", what, args, rc, ra,
				ra != rc ? "the result" : polydiffers(circle, rc != 0));
			break;
		}
		if (rc != 0 && fill) {
			/* the fill, from the same line tables on both sides */
			col = rnd();
			acall(fv, 2, type, col);
			FillVertic(type, col);
			snprintf(args + strlen(args), sizeof args - strlen(args), ", colour %#x, level %d", col, level);
			what = fillnames[3*type + ((uint32_t)level > 2 ? 2 : level)];
			if (!samescreens(what, args))
				break;
		}
		/*
		 * The lines from x 0 to 65535 a circle left would be filled by a
		 * later polygon of one point, which writes none: each made 0 to 0.
		 */
		if (circle && kind == 2)
			for (k = 0; k < 2; k++) {
				memset(poly[k].tab[0], 0, 2*Nline);
				memset(poly[k].tab[1], 0, 2*Nline);
			}
	}
	printf("poly: %d rounds\n", r);
	if (r == 10*Rounds && farpolygons(cp))
		printf("poly: %d polygons at the ends of 16 bits\n", (int)nelem(farpolys));
}

void	AsmTexturedTriangleNoClip(void);
void	AsmGouraudTriangleNoClip(void);
void	FillTextPolyNoClip(int32_t, int32_t, void*);
void	FillTextPoly(int32_t, int32_t, void*);
void	FillTextPolyShade(int32_t, int32_t, int32_t, void*);
void	AsmFillPropNoClip(int16_t*, int32_t, int32_t, int32_t, int32_t);

extern int16_t	TabText[];
extern int32_t	LYmin, LYmax;

typedef struct Tex Tex;

/* a side's triangle's texels and the lines it covers */
struct Tex
{
	uint8_t	*tabtext;
	uint8_t	*lymin;
	uint8_t	*lymax;
};

static Tex tex[2];

/* What of the assembly's lines, frame and tables differs from ours, NULL if nothing. */
static char*
texdiffers(void)
{
	int k;

	if (memcmp(tex[Asm].lymin, &LYmin, 4) != 0)
		return "LYmin";
	if (memcmp(tex[Asm].lymax, &LYmax, 4) != 0)
		return "LYmax";
	if (memcmp(poly[Asm].frame[1], &Ymin, 2) != 0)
		return "Ymin";
	if (memcmp(poly[Asm].frame[3], &Ymax, 2) != 0)
		return "Ymax";
	for (k = 0; k < Ntab; k++)
		if (memcmp(poly[Asm].tab[k], poly[Ours].tab[k], 2*Nline) != 0)
			return tabnames[k];
	return NULL;
}

/*
 * The six tables of ours random, the assembly's the same: each line's
 * left x within the screen, or with neg one in four left of it, its
 * right mostly past it.
 */
static void
textables(int neg)
{
	int32_t l, r;
	int k, y;

	for (y = 0; y < Nline; y++) {
		l = neg && rndn(4) == 0 ? rndr(-Width, -1) : (int32_t)rndn(Width);
		if (rndn(8) == 0)
			r = l - (int32_t)rndn(60);
		else
			r = l + rndr(1, Width-l);
		TabVerticG[y] = l;
		TabVerticD[y] = r;
		TabCoulG[y] = rnd();
		TabCoulD[y] = rnd();
		TabX1[y] = rnd();
		TabY1[y] = rnd();
	}
	for (k = 0; k < Ntab; k++)
		memcpy(poly[Asm].tab[k], poly[Ours].tab[k], 2*Nline);
}

static int32_t
clamp(int32_t v, int32_t lo, int32_t hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

/*
 * A triangle in both sides' TabPoly and its texels in TabText: about a
 * random point of the screen, 40 pixels across but once in eight. Its
 * texels are close together, as a quarter of the globe's map, or not.
 * TabPoly's points 3 and 4, which the procedures read none of, are set
 * to two points of the screen on two lines. Once in sixteen a point lies
 * left of x 0, which the procedures read as a word past 32767, and once
 * in sixteen the three lie on line -1, above the screen, which no edge
 * steps: whether the triangle is such, whose lines a filler would fill
 * past the screen.
 */
static int
triangle(void)
{
	int32_t cx, cy, d, tx, ty;
	int i, close, off;

	d = rndn(8) == 0 ? rndn(400) : rndn(40);
	cx = rndn(Width);
	cy = rndn(Height);
	tx = rnd();
	ty = rnd();
	close = rnd() & 1;
	for (i = 0; i < 3; i++) {
		TabPoly[3*i] = rnd();
		TabPoly[3*i+1] = clamp(cx + rndr(-d, d), 0, Width-1);
		TabPoly[3*i+2] = clamp(cy + rndr(-d, d), 0, Height-1);
		if (close) {
			TabText[3*i+1] = tx + rndr(-4096, 4096);
			TabText[3*i+2] = ty + rndr(-4096, 4096);
		} else {
			TabText[3*i+1] = rnd();
			TabText[3*i+2] = rnd();
		}
	}
	off = rndn(16);
	if (off == 0)
		TabPoly[3*rndn(3)+1] = -rndr(1, d+1);
	else if (off == 1)
		TabPoly[2] = TabPoly[5] = TabPoly[8] = -1;
	for (i = 3; i < 5; i++) {
		TabPoly[3*i] = rnd();
		TabPoly[3*i+1] = rndn(Width);
		TabPoly[3*i+2] = rndn(Height);
	}
	if (TabPoly[14] == TabPoly[11])
		TabPoly[14] = (TabPoly[11] + 1) % Height;
	memcpy(poly[Asm].tabpoly, TabPoly, 15*2);
	memcpy(tex[Asm].tabtext, TabText, 9*2);
	return off <= 1;
}

/*
 * texture's procedures: random triangles of either facing, small as the
 * holomap's globe's mostly, their texels random or close together, or
 * their intensities random, stepped into the tables by
 * AsmTexturedTriangleNoClip, the game's, or AsmGouraudTriangleNoClip,
 * then their lines, or those of random tables, filled from a random map
 * by FillTextPolyNoClip, the game's, by FillTextPoly in random clip
 * boxes or by FillTextPolyShade, and random edges stepped into random
 * words by AsmFillPropNoClip and, in random clip boxes, AsmFillProp,
 * their ends 100000 out once in eight, where the clip's product passes
 * 32 bits. The tables, LYmin, LYmax, Ymin, Ymax, the screen and the
 * words are compared, the tables alone of a triangle off the screen.
 * For the clipped fillers alone, which keep their lines in the box: now
 * and then tables whose lines start left of x 0, and a box whose sides
 * lie 65536 on, which they read in 16 bits.
 */
void
testtexture(void)
{
	static uint8_t map[256*256];
	static int16_t wa[Nline+16], wc[Nline+16];
	uintptr_t tt, gt, ft, fc, fs, fpn, fp;
	int32_t ymin, ymax, dark, x0, y0, x1, y1, w;
	char *diff, *what, args[160];
	int r, k, f, off;

	tt = lookup("AsmTexturedTriangleNoClip");
	gt = lookup("AsmGouraudTriangleNoClip");
	ft = lookup("FillTextPolyNoClip");
	fc = lookup("FillTextPoly");
	fs = lookup("FillTextPolyShade");
	fpn = lookup("AsmFillPropNoClip");
	fp = lookup("AsmFillProp");
	screens();
	setuppoly();
	tex[Ours].tabtext = (uint8_t*)TabText;
	tex[Ours].lymin = (uint8_t*)&LYmin;
	tex[Ours].lymax = (uint8_t*)&LYmax;
	tex[Asm].tabtext = (uint8_t*)lookup("TabText");
	tex[Asm].lymin = (uint8_t*)lookup("LYmin");
	tex[Asm].lymax = (uint8_t*)lookup("LYmax");
	fill(map, sizeof map);
	for (r = 0; r < 10*Rounds; r++) {
		setclip(0, 0, Width-1, Height-1);
		f = rndn(4);
		textables(f >= 2 && rndn(8) == 0);
		k = rndn(4);
		if (k < 3) {
			off = triangle();
			memcpy(tex[Asm].lymin, &LYmin, 4);
			memcpy(tex[Asm].lymax, &LYmax, 4);
			memcpy(poly[Asm].frame[1], &Ymin, 2);
			memcpy(poly[Asm].frame[3], &Ymax, 2);
			if (k < 2) {
				acall(tt, 0);
				AsmTexturedTriangleNoClip();
				what = "AsmTexturedTriangleNoClip";
			} else {
				acall(gt, 0);
				AsmGouraudTriangleNoClip();
				what = "AsmGouraudTriangleNoClip";
			}
			snprintf(args, sizeof args, "triangle %d,%d %d,%d %d,%d, texels %04x %04x, %04x %04x, %04x %04x",
				TabPoly[1], TabPoly[2], TabPoly[4], TabPoly[5], TabPoly[7], TabPoly[8],
				(uint16_t)TabText[1], (uint16_t)TabText[2], (uint16_t)TabText[4], (uint16_t)TabText[5],
				(uint16_t)TabText[7], (uint16_t)TabText[8]);
			if ((diff = texdiffers()) != NULL) {
				fail("%s %s: %s differs from the assembly's", what, args, diff);
				break;
			}
			ymin = LYmin;
			ymax = LYmax;
			if (ymin > ymax || off)
				continue;
		} else {
			ymin = rndn(Nline);
			ymax = rndr(ymin, Nline-1);
			snprintf(args, sizeof args, "random tables");
		}
		if (f >= 2) {
			randclip();
			if (rndn(8) == 0)
				setclip(ClipXmin + 65536, ClipYmin, ClipXmax + 65536, ClipYmax);
		}
		switch (f) {
		case 0:
		case 1:
			acall(ft, 3, ymin, ymax, (int32_t)(uintptr_t)map);
			FillTextPolyNoClip(ymin, ymax, map);
			what = "FillTextPolyNoClip";
			break;
		case 2:
			acall(fc, 3, ymin, ymax, (int32_t)(uintptr_t)map);
			FillTextPoly(ymin, ymax, map);
			what = "FillTextPoly";
			break;
		default:
			dark = rndn(20);
			acall(fs, 4, ymin, ymax, dark, (int32_t)(uintptr_t)map);
			FillTextPolyShade(ymin, ymax, dark, map);
			what = "FillTextPolyShade";
			break;
		}
		snprintf(args + strlen(args), sizeof args - strlen(args), ", lines %d to %d, clip %d %d %d %d",
			ymin, ymax, ClipXmin, ClipYmin, ClipXmax, ClipYmax);
		if (!samescreens(what, args))
			break;
		if ((diff = texdiffers()) != NULL) {
			fail("%s %s: %s differs from the assembly's", what, args, diff);
			break;
		}
		/* edges of distinct lines, which the steps divide by, their x past 16 bits now and then */
		x0 = rndr(-40000, 40000);
		x1 = rndr(-40000, 40000);
		do {
			y0 = rndn(Nline);
			y1 = rndn(Nline);
		} while (y0 == y1);
		fill((uint8_t*)wc, sizeof wc);
		memcpy(wa, wc, sizeof wa);
		acall(fpn, 5, (int32_t)(uintptr_t)wa, x0, y0, x1, y1);
		AsmFillPropNoClip(wc, x0, y0, x1, y1);
		if (memcmp(wa, wc, sizeof wa) != 0) {
			fail("AsmFillPropNoClip %d %d %d %d: the words differ", x0, y0, x1, y1);
			break;
		}
		w = rndn(8) == 0 ? 100000 : 200;
		y0 = rndr(-w, Nline+w);
		y1 = rndr(-w, Nline+w);
		memcpy(wa, wc, sizeof wa);
		acall(fp, 5, (int32_t)(uintptr_t)wa, x0, y0, x1, y1);
		AsmFillProp(wc, x0, y0, x1, y1);
		if (memcmp(wa, wc, sizeof wa) != 0) {
			fail("AsmFillProp %d %d %d %d, clip %d %d: the words differ", x0, y0, x1, y1,
				ClipYmin, ClipYmax);
			break;
		}
	}
	printf("texture: %d rounds\n", r);
}

enum
{
	Nbody = 512,
	Npoint = 500,	/* List_Point's, List_Anim_Point's and List_Normal's */
	Nentity = 5000,	/* words */
	Ntri = 1000,
	Bgroup = 38,
	INFO_ANIM = 2,
	Eline = 0,	/* the kinds of entity List_Tri names, E_LIGNE to E_SPHERE */
	Epoly,
	Esphere,
	Ekind = 0xFFFF,	/* a kind no entity has: List_Tri's entries past a drawing's count */
	Nrandom = 160,	/* random bodies, as many as the game has */
	Bodymax = 32 << 10,	/* a random body's bytes at most */
	Ngroup = 30,	/* TabMat's matrices */
	Maxring = 20,	/* a ring's points in a body with groups */
	Maxwild = 7,	/* in a body without them */
	Nring = 256,	/* a body's rings at most */
};

void	PatchObjet(void*);
int32_t	AffObjetIso(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, void*);
void	SetIsoProjection(int32_t, int32_t, int32_t);
void	SetProjection(int32_t, int32_t, int32_t, int32_t, int32_t);
void	SetFollowCamera(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
void	SetLightVector(int16_t, int16_t, int16_t);

extern int16_t	List_Point[], List_Anim_Point[], List_Entity[], List_Tri[];
extern int16_t	ScreenXmin, ScreenYmin, ScreenXmax, ScreenYmax, NbPoints;
extern int32_t	X0, Y0, Z0, lAlpha, lBeta, lGamma, NormalXLight, NormalYLight, NormalZLight, LMatriceWorld[];

typedef struct Obj Obj;

/* a side's lists and box */
struct Obj
{
	uint8_t	*point;
	uint8_t	*anim;
	uint8_t	*entity;
	uint8_t	*tri;
	uint8_t	*box[4];	/* ScreenXmin, ScreenYmin, ScreenXmax, ScreenYmax */
	uint8_t	*left[6];	/* X0, Y0, Z0, lAlpha, lBeta, lGamma, as the last group left them */
};

static Obj obj[2];
static uint8_t *body[2][Nbody];
static uint32_t bodylen[Nbody];
static int nbody;
static char *boxnames[] = {"ScreenXmin", "ScreenYmin", "ScreenXmax", "ScreenYmax"};
static char *leftnames[] = {"X0", "Y0", "Z0", "lAlpha", "lBeta", "lGamma"};

/* Room for a body of size bytes and 64 of zeros after it. */
static uint8_t*
bodybuf(uint32_t size)
{
	uint8_t *p;

	p = malloc(size + 64);
	if (p == NULL)
		fatal("obj: out of memory");
	memset(p, 0, size + 64);
	return p;
}

/* Body p of size bytes ours, and a copy of it the assembly's. */
static void
addbody(uint8_t *p, uint32_t size)
{
	if (nbody >= Nbody)
		fatal("obj: past %d bodies", Nbody);
	body[Ours][nbody] = p;
	body[Asm][nbody] = bodybuf(size);
	memcpy(body[Asm][nbody], p, size + 64);
	bodylen[nbody] = size + 64;
	nbody++;
}

/* The entries of an HQR file of the game, each into both sides' bodies, room kept for the random ones: how many. */
static int
loadbodies(char *name)
{
	char path[1024];
	uint8_t *b, *p;
	uint32_t n, i, o, size, packed;
	long fsize;
	int k;

	snprintf(path, sizeof path, "%s/%s", gamedir, name);
	b = readfile(path, &fsize);
	n = get32(b) / 4;
	k = 0;
	for (i = 0; i < n && nbody < Nbody - Nrandom; i++) {
		o = get32(b + 4*i);
		if (o == 0 || o + 10 > (uint32_t)fsize)
			continue;
		size = get32(b + o);
		packed = get32(b + o + 4);
		if (size == 0 || o + 10 + packed > (uint32_t)fsize)
			continue;
		p = bodybuf(size);
		if (get16(b + o + 8) == 0)
			memcpy(p, b + o + 10, size);
		else
			Expand(b + o + 10, p, size);
		addbody(p, size);
		k++;
	}
	free(b);
	return k;
}

/* The groups of a body and their count, NULL if it has none. */
static uint8_t*
bodygroups(uint8_t *b, uint16_t *n)
{
	uint8_t *p;

	*n = 0;
	if ((get16(b) & INFO_ANIM) == 0)
		return NULL;
	p = b + 16 + get16(b + 14);
	p += 2 + 6*get16(p);
	*n = get16(p);
	return p + 2;
}

/* Body i's groups but the first in a random pose, both sides alike: turned mostly, moved now and then. */
static void
pose(int i)
{
	uint8_t *g[2];
	uint16_t n, t;
	int k, j, s;

	for (s = 0; s < 2; s++)
		g[s] = bodygroups(body[s][i], &n);
	if (g[Ours] == NULL)
		return;
	for (k = 1; k < n; k++) {
		t = rndn(16) == 0 ? 1 : 0;
		put16(g[Ours] + Bgroup*k + 8, t);
		for (j = 10; j < 16; j += 2)
			put16(g[Ours] + Bgroup*k + j, t == 1 ? rndr(-300, 300) : rndr(-1024, 1024));
		memcpy(g[Asm] + Bgroup*k + 8, g[Ours] + Bgroup*k + 8, 8);
	}
}

typedef struct Ring Ring;

/* a ring of a random body: m points from first, on an ellipse in a plane of one group, in order round it */
struct Ring
{
	int	first;
	int	m;
};

/*
 * The m points of a ring at p, 3 words each: an ellipse about a centre
 * within 100 of the group's origin, of semi-axes of 150 to 400 and a
 * half to all of that, in a random plane, at angles a step apart and
 * moved by 0.15 of a step at most, in order round it. Its points lie
 * within 600 of the origin, and the ellipse bends between two of them
 * by more than their rounding moves them, so that it stays convex.
 */
static void
ring(uint8_t *p, int m)
{
	double c[3], u[3], v[3], a, b, t, d, s;
	int i, k;

	for (;;) {
		for (k = 0; k < 3; k++) {
			u[k] = rndr(-1000, 1000);
			v[k] = rndr(-1000, 1000);
		}
		d = sqrt(u[0]*u[0] + u[1]*u[1] + u[2]*u[2]);
		if (d < 100)
			continue;
		for (k = 0; k < 3; k++)
			u[k] /= d;
		s = u[0]*v[0] + u[1]*v[1] + u[2]*v[2];
		for (k = 0; k < 3; k++)
			v[k] -= s * u[k];
		d = sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
		if (d >= 100)
			break;
	}
	for (k = 0; k < 3; k++) {
		v[k] /= d;
		c[k] = rndr(-100, 100);
	}
	a = rndr(150, 400);
	b = a * rndr(50, 100) / 100;
	t = rnd() / 4294967296.0 * 2*M_PI;
	for (i = 0; i < m; i++) {
		s = t + 2*M_PI * (i + rndr(-15, 15)/100.0) / m;
		for (k = 0; k < 3; k++)
			put16(p + 6*i + 2*k, lround(c[k] + a*cos(s)*u[k] + b*sin(s)*v[k]));
	}
}

/*
 * A random body into b, laid out as AffObjetIso reads one: its length.
 * Three in four have INFO_ANIM and 1 to 30 groups. One in eight stands
 * at the lists' limits, 500 points, 30 groups, 500 normals and entities
 * filling List_Entity's 5000 words, and about as many hold 1 to 8
 * points, the game's having 8 to 257. A group's first point, its joint,
 * lies at its origin, its parent is any earlier group, and it hangs
 * from that one's joint, so that a pose keeps every point within 900 of
 * the body's origin, plus the moves of the groups above it, and in
 * front of testobj's camera. The group's other points are rings (ring)
 * and loose points within 500. The normals are mostly of the game's
 * ranges, their divisors 1 to 4096. One in sixteen is long, of 1200 a
 * side with groups and 30000 to 32767 without, where the light's sum
 * passes 16 bits, its divisor 256 to 2048, and one in sixteen of the
 * others has a divisor of 8000h to FFFEh, which the idiv takes signed:
 * every quotient fits. Off the limits, a body in eight has no normal,
 * and one in eight of the others has one. A polygon is a ring's points
 * in order, all or some, either way round, of any of the 11 materials.
 * One group carries it whole, so that its projection stays convex and
 * clipping adds a point at most at each side of the box. Its 20 points
 * then stay within the 32 TabPoly holds, even were rounding to bend a
 * ring so that one side crossed it at every point, where 30 would pass
 * them clipped at all four sides. A body without groups keeps its
 * points in the plane x = 0: RotateNuage hands Proj_ISO the high halves
 * Rot left and those of x times a word of the matrix, which elsewhere
 * throw the points past what the clipping reckons in 16 bits. Its rings
 * hold 7 points at most, which no clipping brings past 33 whatever
 * their projection. Lines and spheres join or stand on any point.
 */
static uint32_t
randbody(uint8_t *b)
{
	Ring rings[Nring], *rg;
	int count[Ngroup], joint[Ngroup], normals[Ngroup], sub[Maxring];
	uint8_t *p, *q;
	int anim, edge, maxring, np, ng, nn, nr, nl, ns, target, words, npoly;
	int i, k, g, at, left, m, n, mat, w, start, dir;

	anim = rndn(4) != 0;
	edge = rndn(8) == 0;
	maxring = anim ? Maxring : Maxwild;
	fill(b, 16);
	put16(b, anim ? rnd() | INFO_ANIM : rnd() & ~INFO_ANIM);
	k = rndn(8) == 0 ? 2*rndn(12) : 10;
	put16(b + 14, k);
	fill(b + 16, k);
	p = b + 16 + k;
	np = edge ? Npoint : rndn(8) == 0 ? rndr(1, 8) : rndn(4) == 0 ? rndr(8, Npoint) : rndr(8, 260);
	ng = !anim ? 1 : edge ? Ngroup : rndr(1, 25);
	if (ng > np)
		ng = np;
	nn = edge ? Npoint : rndn(8) == 0 ? 0 : rndn(8) == 0 ? 1 : rndr(1, 260);
	for (g = 0; g < ng; g++) {
		count[g] = 1;
		normals[g] = 0;
	}
	for (i = ng; i < np; i++)
		count[rndn(ng)]++;
	for (i = 0; i < nn; i++)
		normals[rndn(ng)]++;
	put16(p, np);
	p += 2;
	nr = 0;
	at = 0;
	for (g = 0; g < ng; g++) {
		joint[g] = at;
		memset(p + 6*at, 0, 6);
		at++;
		for (left = count[g] - 1; left > 0; left -= m) {
			if (left >= 3 && nr < Nring && rndn(4) != 0) {
				m = rndr(3, left < maxring ? left : maxring);
				ring(p + 6*at, m);
				rings[nr].first = at;
				rings[nr].m = m;
				nr++;
			} else {
				m = 1;
				for (k = 0; k < 3; k++)
					put16(p + 6*at + 2*k, rndr(-500, 500));
			}
			at += m;
		}
	}
	if (!anim)
		for (i = 0; i < np; i++)
			put16(p + 6*i, 0);
	p += 6*np;
	if (anim) {
		put16(p, ng);
		p += 2;
		for (g = 0; g < ng; g++) {
			q = p + Bgroup*g;
			fill(q, Bgroup);
			put16(q, 6*joint[g]);
			put16(q + 2, count[g]);
			if (g == 0) {
				put16(q + 4, 0);
				put16(q + 6, 0xFFFF);
			} else {
				k = rndn(g);
				put16(q + 4, 6*joint[k]);
				put16(q + 6, Bgroup*k);
			}
			put16(q + 8, rndn(2));
			for (k = 10; k < 16; k += 2)
				put16(q + k, rndr(-1024, 1024));
			put16(q + 18, normals[g]);
		}
		p += Bgroup*ng;
	}
	put16(p, nn);
	p += 2;
	for (i = 0; i < nn; i++) {
		if (rndn(16) == 0) {
			for (k = 0; k < 3; k++)
				put16(p + 2*k, anim ? rndr(-1200, 1200) : (rnd() & 1 ? 1 : -1) * rndr(30000, 32767));
			put16(p + 6, rndr(256, 2048));
		} else {
			w = rndn(8) == 0 ? 127 : 64;
			for (k = 0; k < 3; k++)
				put16(p + 2*k, rndr(-w, w));
			put16(p + 6, rndn(16) == 0 ? rndr(0x8000, 0xFFFE) : rndn(8) == 0 ? rndr(1, 4096) : rndr(256, 2048));
		}
		p += 8;
	}
	nl = edge ? rndr(40, 100) : rndn(4) == 0 ? 0 : rndr(1, 44);
	ns = edge ? rndr(10, 40) : rndn(2) == 0 ? 0 : rndr(1, 11);
	words = 6*(nl + ns);
	target = edge ? Nentity : rndr(4, 300);
	q = p;
	p += 2;
	for (npoly = 0; nr > 0 && npoly < target; npoly++) {
		rg = &rings[rndn(nr)];
		n = rndn(2) == 0 ? rg->m : rndr(3, rg->m);
		if (n > (Nentity - words - 2) / 3)
			n = (Nentity - words - 2) / 3;
		if (n < 3)
			break;
		for (i = 0, k = 0; i < rg->m; i++)
			if (rndn(rg->m - i) < (uint32_t)(n - k))
				sub[k++] = i;
		mat = rndn(nn > 0 ? 11 : 7);
		p[0] = mat;
		p[1] = n;
		put16(p + 2, rnd());
		p += 4;
		if (mat == 7 || mat == 8) {
			put16(p, rndn(nn));
			p += 2;
		}
		start = rndn(n);
		dir = rnd() & 1 ? 1 : n - 1;
		for (i = 0; i < n; i++) {
			if (mat >= 9) {
				put16(p, rndn(nn));
				p += 2;
			}
			put16(p, 6*(rg->first + sub[(start + i*dir) % n]));
			p += 2;
		}
		words += 2 + 3*n;
	}
	put16(q, npoly);
	put16(p, nl);
	p += 2;
	for (i = 0; i < nl; i++) {
		p[0] = rndn(4) == 0 ? rnd() : 0;
		p[1] = rnd();
		put16(p + 2, rnd());
		put16(p + 4, 6*rndn(np));
		put16(p + 6, 6*rndn(np));
		p += 8;
	}
	put16(p, ns);
	p += 2;
	for (i = 0; i < ns; i++) {
		p[0] = rndn(9);
		put16(p + 1, rnd());
		p[3] = rnd();
		put16(p + 4, rndr(1, 400));
		put16(p + 6, 6*rndn(np));
		p += 8;
	}
	put16(p, rnd());
	p += 2;
	if (p - b > Bodymax)
		fatal("obj: a random body of %d bytes, past %d", (int)(p - b), Bodymax);
	return p - b;
}

/* Our lists, box and what the groups leave, and the assembly's, which objasm compares. */
static void
objsides(void)
{
	int k;

	obj[Ours].point = (uint8_t*)List_Point;
	obj[Ours].anim = (uint8_t*)List_Anim_Point;
	obj[Ours].entity = (uint8_t*)List_Entity;
	obj[Ours].tri = (uint8_t*)List_Tri;
	obj[Ours].box[0] = (uint8_t*)&ScreenXmin;
	obj[Ours].box[1] = (uint8_t*)&ScreenYmin;
	obj[Ours].box[2] = (uint8_t*)&ScreenXmax;
	obj[Ours].box[3] = (uint8_t*)&ScreenYmax;
	obj[Asm].point = (uint8_t*)lookup("List_Point");
	obj[Asm].anim = (uint8_t*)lookup("List_Anim_Point");
	obj[Asm].entity = (uint8_t*)lookup("List_Entity");
	obj[Asm].tri = (uint8_t*)lookup("List_Tri");
	for (k = 0; k < 4; k++)
		obj[Asm].box[k] = (uint8_t*)lookup(boxnames[k]);
	obj[Ours].left[0] = (uint8_t*)&X0;
	obj[Ours].left[1] = (uint8_t*)&Y0;
	obj[Ours].left[2] = (uint8_t*)&Z0;
	obj[Ours].left[3] = (uint8_t*)&lAlpha;
	obj[Ours].left[4] = (uint8_t*)&lBeta;
	obj[Ours].left[5] = (uint8_t*)&lGamma;
	for (k = 0; k < 6; k++)
		obj[Asm].left[k] = (uint8_t*)lookup(leftnames[k]);
}

/*
 * Whether the assembly's box, lists, TabPoly and what its groups left
 * are ours, of n points and ntri entries of List_Tri, its List_Tri
 * holding addresses in its List_Entity where ours holds offsets: what
 * differs, NULL if nothing.
 */
static char*
objasm(int n, int ntri)
{
	uint32_t pa, pc;
	int k;

	for (k = 0; k < 4; k++)
		if (get16(obj[Asm].box[k]) != get16(obj[Ours].box[k]))
			return boxnames[k];
	for (k = 0; k < 6; k++)
		if (get32(obj[Asm].left[k]) != get32(obj[Ours].left[k]))
			return leftnames[k];
	if (memcmp(obj[Asm].anim, obj[Ours].anim, 6*n) != 0)
		return "List_Anim_Point";
	if (memcmp(obj[Asm].point, obj[Ours].point, 6*n) != 0)
		return "List_Point";
	if (memcmp(obj[Asm].entity, obj[Ours].entity, 2*Nentity) != 0)
		return "List_Entity";
	if (memcmp(poly[Asm].tabpoly, poly[Ours].tabpoly, 2*Npoly) != 0)
		return "TabPoly";
	for (k = 0; k < ntri; k++) {
		pa = get32(obj[Asm].tri + 8*k + 4);
		pc = get32(obj[Ours].tri + 8*k + 4);
		if (memcmp(obj[Asm].tri + 8*k, obj[Ours].tri + 8*k, 4) != 0
		|| (pa != pc && pa - (uint32_t)(uintptr_t)obj[Asm].entity != pc))
			return "List_Tri";
	}
	return NULL;
}

/*
 * AffObjetIso of body b at a (xwr, ywr, zwr and the angles) on both
 * sides, args saying what for a fault: whether their result, lists, box
 * and screens are alike, and *drawn one more if they drew.
 */
static int
objdraw(uintptr_t af, int b, int32_t *a, char *args, int *drawn)
{
	int32_t ra, rc, at;
	char *diff;
	int k, n, ntri;

	for (k = 0; k < Ntri; k++) {
		put16(obj[Ours].tri + 8*k + 2, Ekind);
		put16(obj[Asm].tri + 8*k + 2, Ekind);
	}
	ra = acall(af, 7, a[0], a[1], a[2], a[3], a[4], a[5], (int32_t)(uintptr_t)body[Asm][b]);
	rc = AffObjetIso(a[0], a[1], a[2], a[3], a[4], a[5], body[Ours][b]);
	n = NbPoints;
	for (ntri = 0; ntri < Ntri && get16(obj[Ours].tri + 8*ntri + 2) <= 2; ntri++)
		;
	diff = objasm(n, rc == 0 ? ntri : 0);
	if (ra != rc || diff != NULL) {
		fail("AffObjetIso %s: %s", args, ra != rc ? "the result" : diff);
		return 0;
	}
	if (differs(&diff, &at)) {
		fail("AffObjetIso %s: %s at byte %d (%d,%d)", args, diff, at, at%Width, at/Width);
		return 0;
	}
	*drawn += rc == 0;
	return 1;
}

/*
 * A body of two points, its joint at the origin and the other dz ahead
 * of it, in one group, with no polygons or spheres and a line from one
 * to the other, as AffObjetIso reads one: its length. The group claims
 * a normal, which the body's count of normals, 0, makes AffObjetIso
 * pass by. Of two points in one place the line is a pixel or nothing,
 * which Line's clipping gives without a division.
 */
static uint32_t
linebody(uint8_t *b, int16_t dz)
{
	uint8_t *p;

	memset(b, 0, 96);
	put16(b, INFO_ANIM);
	put16(b + 16, 2);
	put16(b + 28, dz);
	put16(b + 30, 1);
	put16(b + 32 + 2, 2);
	put16(b + 32 + 6, 0xFFFF);
	put16(b + 32 + 18, 1);
	p = b + 32 + Bgroup;
	put16(p + 4, 1);
	p[7] = rnd();
	put16(p + 12, 6);
	return p + 16 - b;
}

/*
 * A body of one point at the origin, in one group, with no normals,
 * polygons or lines and a sphere of radius r on the point, filled as
 * type 0, as AffObjetIso reads one: its length.
 */
static uint32_t
ballbody(uint8_t *b, uint16_t r)
{
	uint8_t *p;

	memset(b, 0, 80);
	put16(b, INFO_ANIM);
	put16(b + 16, 1);
	put16(b + 24, 1);
	put16(b + 26 + 2, 1);
	put16(b + 26 + 6, 0xFFFF);
	p = b + 26 + Bgroup;
	put16(p + 6, 1);
	put16(p + 9, rnd());
	put16(p + 12, r);
	return p + 16 - b;
}

/*
 * A body of three points, at the origin and side along x and along y,
 * in one group, with no normals, lines or spheres and two faces of
 * material 0 on the three, either way round, so that one faces the
 * camera, as AffObjetIso reads one: its length.
 */
static uint32_t
facebody(uint8_t *b, uint16_t side)
{
	uint8_t *p;
	int i;

	memset(b, 0, 104);
	put16(b, INFO_ANIM);
	put16(b + 16, 3);
	put16(b + 24, side);
	put16(b + 32, side);
	put16(b + 36, 1);
	put16(b + 38 + 2, 3);
	put16(b + 38 + 6, 0xFFFF);
	p = b + 38 + Bgroup;
	put16(p + 2, 2);
	p += 4;
	for (i = 0; i < 2; i++) {
		p[1] = 3;
		put16(p + 2, rnd());
		put16(p + 6, i == 0 ? 6 : 12);
		put16(p + 8, i == 0 ? 12 : 6);
		p += 10;
	}
	return p + 4 - b;
}

typedef struct Edge Edge;

/* a round of objedges, in perspective or in isometry */
struct Edge
{
	int	body;	/* Eline from point 0 to 1, Esphere on point 0, or Epoly, two faces */
	int	persp;
	int32_t	kf;	/* KFactor, LFactorX and LFactorY in perspective */
	int32_t	lfx;
	int32_t	lfy;
	int64_t	k;	/* point 0's depth plus KFactor, the projection's divisor */
	int32_t	x;	/* point 0's x and y in the camera's view */
	int32_t	y;
	int16_t	dz;	/* point 1 that far ahead of point 0 */
	uint16_t	size;	/* the sphere's radius, the faces' side */
};

/*
 * The projection's edges, under a camera at the origin, its angles and
 * the body's 0, so that the body's point x, y, z at xwr, ywr, zwr lies
 * at x + xwr, y + ywr and zoom - zwr - z in the camera's view, k being
 * that depth plus KFactor. On linebody's bodies: behind the camera, on
 * its plane and 1 before it, 65535 to 196607 and past 2^31 deep, ends
 * that project to 65535, 65536, 131072 and 2^30 and as far left and
 * up, a product past 32 bits, a quotient of 1 by the divisor of a
 * point behind, and a point behind after one before it, and in
 * isometry points that project to 32767 and to -32768, the box's first
 * bounds. Every quotient fits in 32 bits. On ballbody's, spheres whose
 * radius LBA.EXE divides by the depth word plus KFactor in 16 bits: by
 * 2, under an LFactorX of 10001h, of which it takes the low word, and
 * for a radius of FFFFh, which it takes signed, and by 1000 for a
 * radius of FC18h, a quotient of -1000 that it takes unsigned, its
 * circle over the box, every line of it that wraps in 16 bits landing
 * past the box. On facebody's, faces whose depth words read past 32767,
 * below the -32000 a face's depth starts from.
 */
static Edge edges[] = {
	{Eline, 1, 300, 1000, 1000, -5, 1000, 0, 0, 0},
	{Eline, 1, 300, 1000, 1000, 0, 1000, 0, 0, 0},
	{Eline, 1, 300, 1000, 1000, 1, 5, 0, 0, 0},
	{Eline, 1, 300, 1000, 1000, 65535, 1000, 0, 0, 0},
	{Eline, 1, 300, 1000, 1000, 65536, 1000, 0, 0, 0},
	{Eline, 1, 300, 1000, 1000, 131072, 1000, 0, 0, 0},
	{Eline, 1, 300, 1000, 1000, 196607, -1000, 0, 0, 0},
	{Eline, 1, 300, 1000, 1000, (1LL<<31) + 150, 1000, 0, 0, 0},
	{Eline, 1, 300, 1000, 1000, 1000, 65215, 0, 0, 0},
	{Eline, 1, 300, 1000, 1000, 1000, 65216, 0, 0, 0},
	{Eline, 1, 300, 1000, 1000, 1000, 130752, 0, 0, 0},
	{Eline, 1, 300, 1000, 1000, 1000, 1 << 30, 0, 0, 0},
	{Eline, 1, 300, 1000, 1000, 1000, -65857, 0, 0, 0},
	{Eline, 1, 300, 1000, 1000, 1000, -(1 << 30), 0, 0, 0},
	{Eline, 1, 300, 1000, 1000, 1000, 0, -65295, 0, 0},
	{Eline, 1, 300, 1000, 1000, 1000, 0, -65296, 0, 0},
	{Eline, 1, 300, 1000, 1000, 1000, 0, -130832, 0, 0},
	{Eline, 1, 300, 1000, 1000, 1000, 0, -(1 << 30), 0, 0},
	{Eline, 1, 300, 1000, 1000, 1000, 0, 65777, 0, 0},
	{Eline, 1, 300, 1000, 1000, 1000, 0, 1 << 30, 0, 0},
	{Eline, 1, 300, 2000, 2000, 1000, 1 << 21, -(1 << 21), 0, 0},
	{Eline, 1, 300, 2, 2, -5, (1 << 30) - 1, -((1 << 30) - 1), 0, 0},
	{Eline, 1, 300, 1000, 1000, 1000, 100, 50, 1010, 0},
	{Eline, 0, 0, 0, 0, 0, 692210, 0, 0, 0},
	{Eline, 0, 0, 0, 0, 0, -705870, 0, 0, 0},
	{Esphere, 1, 300, 0x10001, 1000, 65238, 0, 0, 0, 1},
	{Esphere, 1, 300, 1, 1000, 65238, 0, 0, 0, 0xFFFF},
	{Esphere, 1, 300, 1000, 1000, 700, 0, 0, 0, 0xFC18},
	{Epoly, 1, 300, 33000, 33000, 33000, 0, 0, 0, 100},
};

/* The rounds of edges, each drawn on both sides: whether all were alike. */
static int
objedges(uintptr_t af, uintptr_t sp, uintptr_t si, uintptr_t sf, int *drawn)
{
	enum {
		Zoom = 1000,
	};
	static uint8_t made[104];
	Edge *e;
	int32_t a[6];
	int64_t z;
	char args[160];
	uint32_t size;
	uint8_t *p;
	int i, b;

	setclip(0, 0, Width-1, Height-1);
	for (i = 0; i < (int)nelem(edges); i++) {
		e = &edges[i];
		if (e->body == Esphere)
			size = ballbody(made, e->size);
		else if (e->body == Epoly)
			size = facebody(made, e->size);
		else
			size = linebody(made, e->dz);
		p = bodybuf(size);
		memcpy(p, made, size);
		b = nbody;
		addbody(p, size);
		if (e->persp) {
			SetProjection(320, 240, e->kf, e->lfx, e->lfy);
			acall(sp, 5, 320, 240, e->kf, e->lfx, e->lfy);
			SetFollowCamera(0, 0, 0, 0, 0, 0, Zoom);
			acall(sf, 7, 0, 0, 0, 0, 0, 0, Zoom);
			z = e->k - e->kf;
			a[2] = (int32_t)(uint32_t)(Zoom - z);
		} else {
			SetIsoProjection(320, 240, 512);
			acall(si, 3, 320, 240, 512);
			a[2] = 0;
		}
		a[0] = e->x;
		a[1] = e->y;
		a[3] = a[4] = a[5] = 0;
		snprintf(args, sizeof args, "edge %d, body %d at %d %d %d, %s", i, b, a[0], a[1], a[2],
			e->persp ? "perspective" : "isometry");
		if (!objdraw(af, b, a, args, drawn))
			return 0;
	}
	return 1;
}

enum
{
	Npointbody = 16,	/* objpoints' bodies */
	Nlit = 2,	/* objlights' */
};

/*
 * A body without groups of 1 to 500 points within 1000 of its origin,
 * each coordinate at random, and nothing more, as AffObjetIso reads
 * one: its length.
 */
static uint32_t
pointbody(uint8_t *b)
{
	uint8_t *p;
	int i, n;

	n = rndr(1, Npoint);
	memset(b, 0, 18);
	put16(b + 16, n);
	p = b + 18;
	for (i = 0; i < 3*n; i++)
		put16(p + 2*i, rndr(-1000, 1000));
	p += 6*n;
	memset(p, 0, 8);
	return p + 8 - b;
}

/*
 * Bodies of points alone without groups (pointbody), drawn 1000 times
 * in isometry at random centres, places and angles: whether all were
 * alike. RotateNuage hands Proj_ISO, as the high half of a point's
 * depth, that of its x times a word of the matrix, which randbody's
 * bodies, their points at x 0, leave 0. These hold points alone, which
 * List_Point gives wherever Proj_ISO throws them.
 */
static int
objpoints(uintptr_t af, uintptr_t si, int *drawn)
{
	static uint8_t made[18 + 6*Npoint + 8];
	int32_t a[6];
	char args[160];
	uint32_t size;
	uint8_t *p;
	int r, i, b, first;

	first = nbody;
	for (i = 0; i < Npointbody; i++) {
		size = pointbody(made);
		p = bodybuf(size);
		memcpy(p, made, size);
		addbody(p, size);
	}
	for (r = 0; r < Rounds/100; r++) {
		b = first + (int)rndn(Npointbody);
		a[0] = rndr(200, 440);
		a[1] = rndr(150, 330);
		SetIsoProjection(a[0], a[1], 512);
		acall(si, 3, a[0], a[1], 512);
		for (i = 0; i < 3; i++)
			a[i] = rndr(-1500, 1500);
		for (; i < 6; i++)
			a[i] = rndr(-1024, 1024);
		snprintf(args, sizeof args, "body %d of points at %d %d %d turned %d %d %d", b, a[0], a[1], a[2],
			a[3], a[4], a[5]);
		if (!objdraw(af, b, a, args, drawn))
			return 0;
	}
	return 1;
}

/* Each side's LMatriceWorld, m, and light, x, y, z. */
static void
setlight(int32_t *m, int32_t x, int32_t y, int32_t z)
{
	uint8_t *w;
	int i;

	w = (uint8_t*)lookup("LMatriceWorld");
	for (i = 0; i < 9; i++) {
		LMatriceWorld[i] = m[i];
		put32(w + 4*i, m[i]);
	}
	NormalXLight = x;
	NormalYLight = y;
	NormalZLight = z;
	put32((uint8_t*)lookup("NormalXLight"), x);
	put32((uint8_t*)lookup("NormalYLight"), y);
	put32((uint8_t*)lookup("NormalZLight"), z);
}

/*
 * A body of three points, at the origin and 600 along y and along z, in
 * one group if anim, with the normal n (its x, y, z and divisor), no
 * lines or spheres and two faces of material 7 on the points, either
 * way round, lit by n, as AffObjetIso reads one: its length.
 */
static uint32_t
litbody(uint8_t *b, int anim, int16_t *n)
{
	uint8_t *p;
	int i;

	memset(b, 0, 116);
	put16(b, anim ? INFO_ANIM : 0);
	put16(b + 16, 3);
	put16(b + 26, 600);
	put16(b + 34, 600);
	p = b + 36;
	if (anim) {
		put16(p, 1);
		put16(p + 2 + 2, 3);
		put16(p + 2 + 6, 0xFFFF);
		put16(p + 2 + 18, 1);
		p += 2 + Bgroup;
	}
	put16(p, 1);
	for (i = 0; i < 4; i++)
		put16(p + 2 + 2*i, n[i]);
	put16(p + 10, 2);
	p += 12;
	for (i = 0; i < 2; i++) {
		p[0] = 7;
		p[1] = 3;
		put16(p + 2, rnd());
		put16(p + 8, i == 0 ? 6 : 12);
		put16(p + 10, i == 0 ? 12 : 6);
		p += 12;
	}
	return p + 4 - b;
}

/*
 * Faces lit past the game's lights, on both sides: whether both were
 * alike. Each body is drawn unturned in isometry at zwr -1000, so that
 * a group's matrix is LMatriceWorld, and the high halves RotateNuage
 * hands Proj_ISO for the points of a body without groups match the
 * signs of their low halves. A body with a group, under a world whose
 * z is 16383 for 16384 and a light of 1 on z, sums its normal of 1 on z
 * to 16384 less 1, over a divisor of 1. A body without groups, under
 * the world of 16384 and a light of 10000h on each axis, of which the
 * products take the low words, has a normal of 1 on each axis over a
 * divisor of 2.
 */
static int
objlights(uintptr_t af, uintptr_t si, int *drawn)
{
	static int32_t world[Nlit][9] = {
		{16384, 0, 0, 0, 16384, 0, 0, 0, 16383},
		{16384, 0, 0, 0, 16384, 0, 0, 0, 16384},
	};
	static int32_t light[Nlit][3] = {
		{0, 0, 1},
		{0x10000, 0x10000, 0x10000},
	};
	static int16_t normal[Nlit][4] = {
		{0, 0, 1, 1},
		{1, 1, 1, 2},
	};
	static uint8_t made[116];
	int32_t a[6];
	char args[160];
	uint32_t size;
	uint8_t *p;
	int i, b;

	SetIsoProjection(320, 240, 512);
	acall(si, 3, 320, 240, 512);
	a[0] = a[1] = 0;
	a[2] = -1000;
	a[3] = a[4] = a[5] = 0;
	for (i = 0; i < Nlit; i++) {
		size = litbody(made, i == 0, normal[i]);
		p = bodybuf(size);
		memcpy(p, made, size);
		b = nbody;
		addbody(p, size);
		setlight(world[i], light[i][0], light[i][1], light[i][2]);
		snprintf(args, sizeof args, "lit body %d, %s groups", b, i == 0 ? "with" : "without");
		if (!objdraw(af, b, a, args, drawn))
			return 0;
	}
	return 1;
}

/*
 * p_ob_iso's PatchObjet and AffObjetIso, over the drawing modules and
 * p_trigo of each side, on random bodies (randbody) and with gamedir on
 * the game's (BODY.HQR and INVOBJ.HQR) in every second round, each
 * patched by each side's PatchObjet: at random positions and angles,
 * their groups in random poses, in isometry or in perspective under a
 * camera following them, a random light, on the whole screen or in a
 * random clip box, the fills at a random level, then the rounds of
 * objedges, objpoints and objlights. Ours and the assembly's lists,
 * box, TabPoly, screen and what the groups leave of p_trigo's state (X0
 * to Z0, lAlpha to lGamma) are compared.
 */
void
testobj(void)
{
	static uint8_t made[Bodymax];
	uintptr_t po, af, sd, sl, si, sp, sf;
	int32_t a[7];
	char args[160];
	uint32_t size;
	uint8_t *p;
	int r, i, b, iso, level, drawn, ngame;

	po = lookup("PatchObjet");
	af = lookup("AffObjetIso");
	sd = lookup("SetFillDetails");
	sl = lookup("SetLightVector");
	si = lookup("SetIsoProjection");
	sp = lookup("SetProjection");
	sf = lookup("SetFollowCamera");
	nbody = 0;
	if (gamedir != NULL) {
		loadbodies("BODY.HQR");
		loadbodies("INVOBJ.HQR");
		if (nbody == 0)
			fatal("obj: no bodies in %s", gamedir);
	}
	ngame = nbody;
	for (i = 0; i < Nrandom; i++) {
		size = randbody(made);
		p = bodybuf(size);
		memcpy(p, made, size);
		addbody(p, size);
	}
	for (i = 0; i < nbody; i++) {
		acall(po, 1, (int32_t)(uintptr_t)body[Asm][i]);
		PatchObjet(body[Ours][i]);
		if (memcmp(body[Asm][i], body[Ours][i], bodylen[i]) != 0)
			fail("PatchObjet of body %d: ours and the assembly's differ", i);
	}
	screens();
	objsides();
	setuppoly();
	drawn = 0;
	for (r = 0; r < Rounds/10; r++) {
		if (r % 500 == 0)
			screens();
		b = ngame > 0 && r%2 == 0 ? (int)rndn(ngame) : ngame + (int)rndn(nbody - ngame);
		pose(b);
		if (rndn(4) == 0)
			randclip();
		else
			setclip(0, 0, Width-1, Height-1);
		level = rndn(3);
		acall(sd, 1, level);
		SetFillDetails(level);
		for (i = 0; i < 3; i++)
			a[i] = (int16_t)rndr(-1024, 1024);
		SetLightVector(a[0], a[1], a[2]);
		acall(sl, 3, a[0], a[1], a[2]);
		iso = rnd() & 1;
		if (iso) {
			a[0] = rndr(200, 440);
			a[1] = rndr(150, 330);
			a[2] = 512;
			SetIsoProjection(a[0], a[1], a[2]);
			acall(si, 3, a[0], a[1], a[2]);
			for (i = 0; i < 3; i++)
				a[i] = rndr(-1500, 1500);
		} else {
			a[0] = 320;
			a[1] = 240;
			a[2] = rndr(100, 400);
			a[3] = rndr(300, 2000);
			a[4] = rndr(300, 2000);
			SetProjection(a[0], a[1], a[2], a[3], a[4]);
			acall(sp, 5, a[0], a[1], a[2], a[3], a[4]);
			for (i = 0; i < 3; i++)
				a[i] = rndr(-5000, 5000);
			for (; i < 6; i++)
				a[i] = rndr(-1024, 1024);
			/* a camera far enough that every sphere lies before it: LBA.EXE's assembly took the radius of one behind it unsigned */
			a[6] = rndr(4000, 12000);
			SetFollowCamera(a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
			acall(sf, 7, a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
			for (i = 0; i < 3; i++)
				a[i] += rndr(-800, 800);
		}
		for (i = 3; i < 6; i++)
			a[i] = rndr(-1024, 1024);
		snprintf(args, sizeof args, "body %d at %d %d %d turned %d %d %d, %s, level %d", b, a[0], a[1], a[2],
			a[3], a[4], a[5], iso ? "isometry" : "perspective", level);
		if (!objdraw(af, b, a, args, &drawn))
			break;
	}
	printf("obj: %d rounds, %d bodies of the game's and %d random ones, %d drawn\n", r, ngame, nbody - ngame, drawn);
	if (r < Rounds/10 || !objedges(af, sp, si, sf, &drawn))
		return;
	printf("obj: the projection's %d edges\n", (int)nelem(edges));
	if (!objpoints(af, si, &drawn))
		return;
	printf("obj: %d drawings of %d bodies of points without groups\n", Rounds/100, Npointbody);
	if (objlights(af, si, &drawn))
		printf("obj: %d lit bodies\n", Nlit);
}
