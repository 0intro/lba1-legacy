#include <stddef.h>
#include <stdint.h>
#include "dat.h"
#include "fns.h"

/*
 * sys/decode.c's calls for the oracles that link sys/cdimage.c, whose
 * discs hold BINARY and WAVE files: decopen gives NULL, so that a cue
 * sheet's MP3 or OGG file is refused as one of no sound, and the other
 * calls stand idle.
 */

Decoder*
decopen(const char *path, int mode)
{
	USED(path);
	USED(mode);
	return NULL;
}

void
decinfo(Decoder *d, uint32_t *rate, uint32_t *channels, uint64_t *frames)
{
	USED(d);
	*rate = 0;
	*channels = 0;
	*frames = 0;
}

uint64_t
decread(Decoder *d, int16_t *buf, uint64_t n)
{
	USED(d);
	USED(buf);
	USED(n);
	return 0;
}

int
decseek(Decoder *d, uint64_t frame)
{
	USED(d);
	USED(frame);
	return -1;
}

void
decclose(Decoder *d)
{
	USED(d);
}
