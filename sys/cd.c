#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "dat.h"
#include "fns.h"

/*
 * The CD, as lib386/lib_cd/cdrom.asm drove it through MSCDEX: a drive
 * letter for its files, a file's place in sectors (GetFileCDR) to read
 * it by sectors (ReadLongCDR), and its audio tracks. Here the CD is a
 * directory, drive D:, and a file's sectors are its own 2048 bytes from
 * a start of ours.
 */

enum
{
	Sector = 2048,
	Start = 16,	/* the first file's sector: past ISO 9660's own */
};

int16_t	DriveCDR = -2;	/* cdrom.asm's, -2 for no drive */
int32_t	FileCD_Start;
int32_t	FileCD_Sect;
int32_t	FileCD_Size;

static char cdfile[1024];

/*
 * InitCDR is cdrom.asm's: whether the CD's root holds the file name, the
 * CD's drive D: in DriveCDR, or -2 when the CD has no bind, as cdrom.asm
 * gives it without a drive. A CD without the file keeps D:, where
 * cdrom.asm gives -1: the port reads its other files where they lie, the
 * voices among them, which the game names from DriveCDR.
 */
int32_t
InitCDR(char *name)
{
	char file[300];

	setcdletter('D');
	if (!havecd()) {
		DriveCDR = -2;
		return 0;
	}
	DriveCDR = 'D' - 'A';
	snprintf(file, sizeof file, "D:\\%s", name);
	return havefile(file);
}

/*
 * The audio tracks, taken for none: PlayTrackCDR answers 1 and
 * GetLengthTrackCDR 0, as for a track the CD lacks.
 */
void
StopCDR(void)
{
}

void
ClearCDR(void)
{
}

int32_t
PlayTrackCDR(int32_t t)
{
	USED(t);
	return 1;
}

int32_t
GetLengthTrackCDR(int32_t t)
{
	USED(t);
	/*
	 * FIXED: LBA.EXE's StartTrackCDR answers a track the CD lacks with
	 * what its buffer last held. Such a track has a length of 0.
	 */
	return 0;
}

/*
 * GetFileCDR is cdrom.asm's: the file's start, size and number of
 * sectors, which it counts as (size+2048)/2048. The number, or 0.
 */
int32_t
GetFileCDR(char *name)
{
	char file[300], path[sizeof cdfile];
	uint32_t n;

	snprintf(file, sizeof file, "D:%s", name);
	/*
	 * FIXED: LBA.EXE's GetFileCDR takes MSCDEX's failure for a success,
	 * and answers a file the CD lacks with the last file found. Such a
	 * file gives 0, FileCD_* and the file ReadLongCDR reads as they
	 * were.
	 */
	if (readpath(file, path, sizeof path) < 0)
		return 0;
	memmove(cdfile, path, strlen(path)+1);
	n = FileSize(file);
	FileCD_Start = Start;
	FileCD_Size = n;
	FileCD_Sect = (n + 2048) / 2048;
	return FileCD_Sect;
}

/*
 * ReadLongCDR reads sectors of the last file GetFileCDR found: 0 if it
 * did. Past the file's end the CD went on into what followed it, and
 * GetFileCDR counts a sector too many when the size is a multiple of
 * 2048: there, zeros.
 */
int32_t
ReadLongCDR(int32_t start, int32_t n, void *buf)
{
	FILE *f;
	size_t got;

	f = fopen(cdfile, "rb");
	if (f == NULL)
		return -1;
	memset(buf, 0, (size_t)n*Sector);
	got = 0;
	if (fseek(f, (long)(start-Start)*Sector, SEEK_SET) == 0)
		got = fread(buf, 1, (size_t)n*Sector, f);
	USED(got);
	fclose(f);
	return 0;
}
