#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include "../sys/dat.h"
#include "../sys/fns.h"

/*
 * pcm: an MP3 or Ogg Vorbis file decoded as the port decodes it
 * (sys/decode.c), its 16-bit samples on standard output, little-endian
 * and interleaved.
 *
 *	pcm [-i] file
 *
 * -i prints instead the file's rate in Hz, its channels and its length
 * in frames, on a line.
 */

enum
{
	Nbuf = 4096,	/* frames read at once */
};

static void
usage(void)
{
	fprintf(stderr, "usage: pcm [-i] file\n");
}

int
main(int argc, char **argv)
{
	static int16_t buf[Nbuf*2];
	static unsigned char out[Nbuf*2*2];
	Decoder *d;
	uint32_t rate, channels;
	uint64_t frames, n, i;
	int info, a;

#ifdef _WIN32
	_setmode(_fileno(stdout), _O_BINARY);
#endif
	info = 0;
	for (a = 1; a < argc && argv[a][0] == '-'; a++) {
		if (strcmp(argv[a], "-i") == 0)
			info = 1;
		else {
			usage();
			return 2;
		}
	}
	if (a != argc - 1) {
		usage();
		return 2;
	}
	d = decopen(argv[a]);
	if (d == NULL) {
		fprintf(stderr, "pcm: %s: cannot decode\n", argv[a]);
		return 1;
	}
	decinfo(d, &rate, &channels, &frames);
	if (info) {
		printf("%lu %lu %llu\n", (unsigned long)rate, (unsigned long)channels, (unsigned long long)frames);
		decclose(d);
		return 0;
	}
	while ((n = decread(d, buf, Nbuf)) > 0) {
		for (i = 0; i < n*channels; i++) {
			out[2*i] = (uint16_t)buf[i] & 0xff;
			out[2*i+1] = (uint16_t)buf[i] >> 8;
		}
		if (fwrite(out, 2*channels, n, stdout) != n) {
			fprintf(stderr, "pcm: write error\n");
			decclose(d);
			return 1;
		}
	}
	decclose(d);
	if (fflush(stdout) != 0) {
		fprintf(stderr, "pcm: write error\n");
		return 1;
	}
	return 0;
}
