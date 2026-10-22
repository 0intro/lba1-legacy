#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif
#include "dat.h"
#include "dos.h"
#include "fns.h"

/*
 * watcomatoi is Watcom's atoi: after blanks and a sign, the digits'
 * value modulo 2^32, as its int sums them.
 */
int
watcomatoi(const char *s)
{
	unsigned u;
	int neg;

	while (isspace((unsigned char)*s))
		s++;
	neg = *s == '-';
	if (*s == '+' || *s == '-')
		s++;
	u = 0;
	while (isdigit((unsigned char)*s))
		u = u*10 + (unsigned)(*s++ - '0');
	if (neg)
		u = -u;
	return (int)u;
}

/*
 * The functions of Watcom's library that the 1994 code calls, but for
 * rand, srand and qsort (rand.c, qsort.c): functions msvcrt also exports
 * under these names, or that only DOS had (sys/compat.h maps the
 * names).
 */

int
watcomstricmp(const char *a, const char *b)
{
	return watcomstrnicmp(a, b, (size_t)-1);
}

int
watcomstrnicmp(const char *a, const char *b, size_t n)
{
	int ca, cb;

	for (; n > 0; n--, a++, b++) {
		ca = tolower(*(const unsigned char*)a);
		cb = tolower(*(const unsigned char*)b);
		if (ca != cb || ca == 0)
			return ca - cb;
	}
	return 0;
}

char*
watcomstrupr(char *s)
{
	char *p;

	for (p = s; *p != '\0'; p++)
		*p = (char)toupper(*(unsigned char*)p);
	return s;
}

/* watcomitoa is Watcom's itoa: a minus sign in base 10 only. */
char*
watcomitoa(int v, char *s, int radix)
{
	char buf[34], *p, *q;
	unsigned u;

	q = s;
	u = (unsigned)v;
	if (radix == 10 && v < 0) {
		*q++ = '-';
		u = 0 - u;
	}
	p = buf;
	do {
		*p++ = "0123456789abcdefghijklmnopqrstuvwxyz"[u % radix];
		u /= radix;
	} while (u != 0);
	while (p > buf)
		*q++ = *--p;
	*q = '\0';
	return s;
}

/* watcomltoa is Watcom's ltoa, whose long is 32 bits: itoa's. */
char*
watcomltoa(long v, char *s, int radix)
{
	return watcomitoa((int)v, s, radix);
}

/* watcomultoa is Watcom's ultoa: the value as a uint32_t. */
char*
watcomultoa(unsigned long v, char *s, int radix)
{
	char buf[34], *p, *q;
	uint32_t u;

	u = v;
	p = buf;
	do {
		*p++ = "0123456789abcdefghijklmnopqrstuvwxyz"[u % radix];
		u /= radix;
	} while (u != 0);
	for (q = s; p > buf; )
		*q++ = *--p;
	*q = 0;
	return s;
}

static void
part(char *dst, size_t max, const char *a, const char *b)
{
	size_t n;

	if (dst == NULL)
		return;
	n = b - a;
	if (n > max - 1)
		n = max - 1;
	memmove(dst, a, n);
	dst[n] = '\0';
}

/*
 * watcomsplitpath is Watcom's _splitpath: the drive with its colon, the
 * directory to its last separator, \ or /, the name, the extension with
 * its dot. Parts longer than their buffers are cut.
 */
void
watcomsplitpath(const char *path, char *drive, char *dir, char *name, char *ext)
{
	const char *d, *n, *e, *p;

	d = path;
	if (path[0] != '\0' && path[1] == ':')
		d = path + 2;
	part(drive, 3, path, d);
	n = d;
	for (p = d; *p != '\0'; p++)
		if (*p == '/' || *p == '\\')
			n = p + 1;
	part(dir, 256, d, n);
	e = strrchr(n, '.');
	if (e == NULL)
		e = n + strlen(n);
	part(name, 256, n, e);
	part(ext, 256, e, e + strlen(e));
}

char*
watcomgetcwd(char *buf, int size)
{
#ifdef _WIN32
	return _getcwd(buf, size);
#else
	return getcwd(buf, size);
#endif
}

int
watcomchdir(const char *dir)
{
#ifdef _WIN32
	return _chdir(dir);
#else
	return chdir(dir);
#endif
}

/* One drive, C:, which nothing changes. */
void
_dos_getdrive(unsigned *drive)
{
	*drive = 3;
}

void
_dos_setdrive(unsigned drive, unsigned *ndrives)
{
	USED(drive);
	*ndrives = 26;
}
