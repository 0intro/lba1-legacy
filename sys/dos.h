/*
 * What dos.h gave the 1994 code: the drive, and the search for files
 * the game makes (sys/files.c, sys/compat.c). Included by compat.h and
 * by the port files that implement these.
 */
#define _A_NORMAL	0x00
#define _A_RDONLY	0x01
#define _A_HIDDEN	0x02
#define _A_SYSTEM	0x04
#define _A_VOLID	0x08
#define _A_SUBDIR	0x10
#define _A_ARCH		0x20

struct find_t
{
	char		reserved[21];
	char		attrib;
	unsigned short	wr_time;
	unsigned short	wr_date;
	unsigned int	size;
	char		name[256];	/* 8.3, or in the root a long name (sys/files.c) */
};

unsigned	_dos_findfirst(const char*, unsigned, struct find_t*);
unsigned	_dos_findnext(struct find_t*);
void		_dos_getdrive(unsigned*);
void		_dos_setdrive(unsigned, unsigned*);
