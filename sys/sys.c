#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "dat.h"
#include "fns.h"

/*
 * What lib386/lib_sys/adeline.c set up: the configuration file, whose
 * keys chose DOS's drivers, the video, the keyboard and the timer. Here
 * the sound's drivers are the port's (sound.c), MidiType chooses the
 * music banks, and the configuration file is read where the game's
 * files are read, as LBA.EXE reads LBA.CFG in its directory, its changes
 * written into the preferences (files.c).
 */

char	PathConfigFile[260];
char	MidiFM = 1;

void
InitAdelineSystem(char *name, int32_t inits)
{
	unsigned char *p;

	USED(inits);
	printf("\nCopyright (c) Adeline Software International 1994, All Rights Reserved.\n\n");
	snprintf(PathConfigFile, sizeof PathConfigFile, "%s", name);
	/*
	 * FEATURE: the configuration file read, after the preferences and
	 * the game's directories, from the program's directory, as LbaWin's
	 * InitAdelineSystem (LbaWin.exe 0x403550) takes the LBA.CFG of its
	 * program's folder, and last from sys/lba.cfg's text, which the
	 * program holds. Taken from LbaWin 0.8.1, the text the port's own.
	 */
	bindconfig(name, (char*)lbacfg);
	readplatform(name);
	p = Def_ReadString((unsigned char*)name, (unsigned char*)"MidiType");
	if (p != NULL && *p != 0)
		MidiFM = watcomstricmp((char*)p, "FM") == 0;
	soundinit();
	videoinit();
	clockinit();
}

void
ClearAdelineSystem(void)
{
	ClearMidi();
	sdlclose();
}

/*
 * ComputeTime is func.asm's: DOS's time word added to its date word, as
 * the game's voice cache compares them.
 */
int32_t
ComputeTime(void)
{
	time_t t;
	struct tm *tm;

	t = time(NULL);
	tm = localtime(&t);
	return (tm->tm_hour<<11 | tm->tm_min<<5 | tm->tm_sec/2)
		+ ((tm->tm_year-80)<<9 | (tm->tm_mon+1)<<5 | tm->tm_mday);
}
