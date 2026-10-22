#include <stddef.h>
#include <stdint.h>
#include "../sys/compat.h"
#include "c_extern.h"

/* Made by tools/psmusic.py, which proves them. Do not edit. */

/*
 * FEATURE: the PlayStation's recordings of the tunes, which it plays
 * from its disc. TabPsTrack is the track of tunes 0 to 30 as its
 * PlayMusic (SLES_006.98 0x80046674) asks for it, 0 for none: tunes 1
 * to 8 tracks 2 to 9, tune 9 track 27, the jingles tracks 10 to 26.
 * Taken from the PlayStation.
 *
 * Tune 29, a sequence of 39.1 s, takes track 26, J18MID.WAV, the
 * recording of tune 30, whose music lasts 27.0 s as tune 30's sequence
 * of 26.2 s, as the PlayStation plays it.
 */
static uint8_t TabPsTrack[31] = {
	17, 2, 3, 4, 5, 6, 7, 8, 9, 27, 22, 10, 11, 12, 13, 14, 15, 16,
	17, 18, 19, 20, 21, 23, 0, 0, 0, 24, 25, 26, 26,
};

/*
 * TabPsFile is the file of each track on the port's CD: the disc's
 * TRACK1.WAV to TRACK9.WAV, tunes 1 to 9, as the PC CD's tracks 2 to
 * 10, which hold the same recordings, and its jingles by their names.
 */
static char *TabPsFile[28] = {
	[2] = "TRACK02.WAV",
	[3] = "TRACK03.WAV",
	[4] = "TRACK04.WAV",
	[5] = "TRACK05.WAV",
	[6] = "TRACK06.WAV",
	[7] = "TRACK07.WAV",
	[8] = "TRACK08.WAV",
	[9] = "TRACK09.WAV",
	[10] = "JINGLES\\J01MID.WAV",
	[11] = "JINGLES\\J02MID.WAV",
	[12] = "JINGLES\\J03MID.WAV",
	[13] = "JINGLES\\J04MID.WAV",
	[14] = "JINGLES\\J05MID.WAV",
	[15] = "JINGLES\\J06MID.WAV",
	[16] = "JINGLES\\J07MID.WAV",
	[17] = "JINGLES\\J08MID.WAV",
	[18] = "JINGLES\\J09MID.WAV",
	[19] = "JINGLES\\J10MID.WAV",
	[20] = "JINGLES\\J11MID.WAV",
	[21] = "JINGLES\\J12MID.WAV",
	[22] = "JINGLES\\J14MID.WAV",
	[23] = "JINGLES\\J15MID.WAV",
	[24] = "JINGLES\\J16MID.WAV",
	[25] = "JINGLES\\J17MID.WAV",
	[26] = "JINGLES\\J18MID.WAV",
	[27] = "TRACK10.WAV",
};

/* GetPsTrackFile: the file on the CD of tune num's track, or NULL */
char*
GetPsTrackFile(int32_t num)
{
	if (num < 0 || num >= 31)
		return NULL;
	return TabPsFile[TabPsTrack[num]];
}
