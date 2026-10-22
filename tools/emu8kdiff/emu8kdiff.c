#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "86box/86box.h"
#include "86box/io.h"
#include "86box/rom.h"
#include "86box/sound.h"
#include "86box/snd_emu8k.h"

/*
 * emu8kdiff: our EMU8000 (sys/emu8k.c) against 86Box's (snd_emu8k.c,
 * built from ../lba1-tmp), on the same accesses.
 *
 *	emu8kdiff [-w file.wav] awe32.raw log
 *
 * log is our chip's port accesses as tools/oracle's aweoracle -l writes
 * them, a line each: the samples made by then, the port, i or o, the
 * value. 86Box's chip, at 0x620 as the AWE32's at 220h, is made to run
 * to each time and given each access. Its reads are compared with ours,
 * by register: the sample counter, the volume targets the driver's
 * allocator reads, the attenuation it reads back, the detection's, and
 * the rest, each kind's first difference said. With -w its sound is
 * written as a WAV file, 16-bit stereo at 44100 Hz, to be heard beside
 * ours (aweoracle -w).
 */

enum
{
	Nkind = 5,
};

/* snd_emu8k.c's, which its header leaves out */
uint16_t	emu8k_inw(uint16_t, void*);
void	emu8k_outw(uint16_t, uint16_t, void*);

static char *kinds[Nkind] = {"WC", "VTFT", "IFATN", "detection", "other"};

int wavetable_pos_global;

static emu8k_t emu;
static char *romname;
static FILE *wav;
static uint32_t frames;
static uint64_t done;

void
fatal(const char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "emu8kdiff: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	exit(1);
}

void
pclog(const char *fmt, ...)
{
	(void)fmt;
}

void
io_sethandler(uint16_t base, int size, uint8_t (*inb)(uint16_t, void*), uint16_t (*inw)(uint16_t, void*),
	uint32_t (*inl)(uint16_t, void*), void (*outb)(uint16_t, uint8_t, void*), void (*outw)(uint16_t, uint16_t, void*),
	void (*outl)(uint16_t, uint32_t, void*), void *priv)
{
	(void)base, (void)size, (void)inb, (void)inw, (void)inl, (void)outb, (void)outw, (void)outl, (void)priv;
}

void
io_removehandler(uint16_t base, int size, uint8_t (*inb)(uint16_t, void*), uint16_t (*inw)(uint16_t, void*),
	uint32_t (*inl)(uint16_t, void*), void (*outb)(uint16_t, uint8_t, void*), void (*outw)(uint16_t, uint16_t, void*),
	void (*outl)(uint16_t, uint32_t, void*), void *priv)
{
	(void)base, (void)size, (void)inb, (void)inw, (void)inl, (void)outb, (void)outw, (void)outl, (void)priv;
}

FILE*
rom_fopen(const char *fn, char *mode)
{
	(void)fn;
	return fopen(romname, mode);
}

static void
put32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static void
header(uint32_t n)
{
	uint8_t h[44];

	memcpy(h, "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\0\0\0\0\0\0\0\0\x04\0\x10\0data\0\0\0\0", 44);
	put32(h+4, 36 + 4*n);
	put32(h+24, 44100);
	put32(h+28, 4*44100);
	put32(h+40, 4*n);
	if (fseek(wav, 0, SEEK_SET) < 0 || fwrite(h, 1, 44, wav) != 44)
		fatal("cannot write the WAV file");
}

static int16_t
clip(int32_t v)
{
	return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

/* 86Box's chip run to sample t, its buffer handed on a period at a time, as its sound card does. */
static void
run(uint64_t t)
{
	int16_t o[2*WTBUFLEN];
	uint32_t n;
	int i;

	while (done < t) {
		n = t - done;
		if (n > (uint32_t)(WTBUFLEN - wavetable_pos_global))
			n = WTBUFLEN - wavetable_pos_global;
		wavetable_pos_global += n;
		emu8k_update(&emu);
		done += n;
		if (wavetable_pos_global < WTBUFLEN)
			continue;
		if (wav != NULL) {
			for (i = 0; i < 2*WTBUFLEN; i++)
				o[i] = clip(emu.buffer[i]);
			if (fwrite(o, 4, WTBUFLEN, wav) != WTBUFLEN)
				fatal("cannot write the WAV file");
			frames += WTBUFLEN;
		}
		emu8k_reset_buffer(&emu);
		wavetable_pos_global = 0;
	}
}

/* What a read at port with the pointer at ptr reads. */
static int
kind(uint32_t port, uint32_t ptr)
{
	int reg, ch;

	reg = ptr >> 5 & 7;
	ch = ptr & 0x1f;
	if (port == 0xa22 && reg == 1 && ch == 27)
		return 0;
	if ((port == 0x620 || port == 0x622) && reg == 3)
		return 1;
	if (port == 0xe20 && reg == 1)
		return 2;
	if ((port == 0xe20 && reg == 7) || (port == 0xa20 && reg == 1 && ch >= 29))
		return 3;
	return 4;
}

int
main(int argc, char *argv[])
{
	char line[128], dir;
	unsigned long long t;
	unsigned int port, val;
	uint32_t ptr, theirs, n[Nkind], same[Nkind], vtmax;
	FILE *f;
	int k;

	if (argc == 5 && strcmp(argv[1], "-w") == 0) {
		wav = fopen(argv[2], "wb");
		if (wav == NULL)
			fatal("cannot create %s", argv[2]);
		header(0);
		argc -= 2;
		argv += 2;
	}
	if (argc != 3) {
		fprintf(stderr, "usage: emu8kdiff [-w file.wav] awe32.raw log\n");
		return 2;
	}
	romname = argv[1];
	f = fopen(argv[2], "r");
	if (f == NULL)
		fatal("cannot open %s", argv[2]);
	emu8k_init(&emu, 0x620, 512);
	memset(n, 0, sizeof n);
	memset(same, 0, sizeof same);
	vtmax = 0;
	ptr = 0;
	while (fgets(line, sizeof line, f) != NULL) {
		if (sscanf(line, "%llu %x %c %x", &t, &port, &dir, &val) != 4)
			fatal("bad line: %s", line);
		run(t);
		if (dir == 'o') {
			if (port == 0xe22)
				ptr = val;
			emu8k_outw(port, val, &emu);
			continue;
		}
		theirs = emu8k_inw(port, &emu);
		k = kind(port, ptr);
		n[k]++;
		if (theirs == val)
			same[k]++;
		else if (n[k] - same[k] == 1)
			printf("emu8kdiff: %s: first differs at %llu: port %x, register %u of channel %u: ours %04x, 86Box's %04x\n",
				kinds[k], t, port, ptr >> 5 & 7, ptr & 0x1f, val, theirs);
		if (k == 1 && port == 0x622 && (theirs > val ? theirs - val : val - theirs) > vtmax)
			vtmax = theirs > val ? theirs - val : val - theirs;
	}
	fclose(f);
	if (wav != NULL) {
		for (i = 0; i < 2*wavetable_pos_global; i++)
			o[i] = clip(emu.buffer[i]);
		if (fwrite(o, 4, wavetable_pos_global, wav) != (size_t)wavetable_pos_global)
			fatal("cannot write the WAV file");
		frames += wavetable_pos_global;
		header(frames);
		fclose(wav);
	}
	for (k = 0; k < Nkind; k++)
		printf("emu8kdiff: %s: %u reads, %u the same\n", kinds[k], n[k], same[k]);
	printf("emu8kdiff: the volume targets differ by %u at most; %llu samples\n", vtmax, (unsigned long long)done);
	return 0;
}
