#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include "dat.h"
#include "fns.h"

/*
 * Sound, as lib386's lib_samp (samples and voices), lib_midi (the music
 * through AIL/32) and lib_mix (the card's mixer) offered it, the samples
 * and the mixer a Sound Blaster 16's whatever driver the configuration
 * names. As in the
 * original's InitAdelineSystem, a driver missing, empty or No... is
 * none. WaveDriver gives the samples (wave.c) at WaveRate, which
 * W_SB16.DLL kept between 12000 and 22000. A rate the original had to
 * be given is its own 22000 here, and the base it had to be given is
 * nothing. MidiDriver is taken for none, the music silent. MixerDriver
 * gives the mixer, as M_SB16.DLL drove it: every volume adjustable, 0
 * to 255 being 2 dB a step of 8 below the top, read back to its step,
 * the music's read from the samples' (a slip of M_SB16.DLL's), its
 * registers at first the chip's own after a reset, giving their gains
 * to the samples and the CD's audio (cd.c). Without one the game offers
 * the music's volume alone and the card plays as it was.
 */

enum
{
	Vwave,
	Vmidi,
	Vcd,
	Vline,
	Vmaster,
};

int16_t	Midi_Driver_Enable;
int32_t	MaxVolume = 100;

static int mixer;
static int32_t volumes[5] = {0xC0, 0xC0, 0, 0, 0xC0};

/* The driver the configuration names for key, if any. */
static int
driver(char *key, char *none)
{
	unsigned char *p;

	p = Def_ReadString((unsigned char*)PathConfigFile, (unsigned char*)key);
	return p != NULL && *p != 0 && watcomstricmp((char*)p, none) != 0;
}

/* An SB16 mixer register's gain: its 5 bits, 2 dB a step from 0 dB. */
static float
gain(int32_t v)
{
	return powf(10, (float)(((v & 0xFF) >> 3) - 31) * 2 / 20);
}

/* The registers' gains on the samples and the CD. */
static void
gains(void)
{
	sdlwavegain(gain(volumes[Vwave]) * gain(volumes[Vmaster]));
	sdlcdgain(gain(volumes[Vcd]) * gain(volumes[Vmaster]));
}

void
soundinit(void)
{
	int32_t rate;

	Wave_Driver_Enable = driver("WaveDriver", "NoWave");
	if (Wave_Driver_Enable) {
		if (Def_ReadValue2((unsigned char*)PathConfigFile, (unsigned char*)"WaveRate", &rate))
			PlayRate = rate;
		/* W_SB16.DLL's ResetCard, unsigned */
		if ((uint32_t)PlayRate < 12000)
			PlayRate = 12000;
		if ((uint32_t)PlayRate > 22000)
			PlayRate = 22000;
	}
	Midi_Driver_Enable = 0;
	mixer = driver("MixerDriver", "NoMixer");
	if (mixer)
		gains();
}

/* lib_midi's calls, on a driver taken for none. */
void
PlayMidi(unsigned char *xmi)
{
	USED(xmi);
}

void
StopMidi(void)
{
}

int32_t
IsMidiPlaying(void)
{
	return 0;
}

void
FadeMidiDown(int16_t nbsec)
{
	USED(nbsec);
}

void
VolumeMidi(int16_t volume)
{
	USED(volume);
}

void
DoLoopMidi(void)
{
}

/* SetMixerVolume is M_SB16.DLL's: a byte to each register, -1 none. */
void
MixerChangeVolume(int32_t wave, int32_t midi, int32_t cd, int32_t line, int32_t master)
{
	int32_t v[5];
	int i;

	if (!mixer)
		return;
	v[Vwave] = wave;
	v[Vmidi] = midi;
	v[Vcd] = cd;
	v[Vline] = line;
	v[Vmaster] = master;
	for (i = 0; i < 5; i++)
		if (v[i] != -1)
			volumes[i] = v[i] & 0xFF;
	gains();
}

/* GetMixerVolume is M_SB16.DLL's: the registers read back, the music's being the samples'. */
void
MixerGetVolume(int32_t *wave, int32_t *midi, int32_t *cd, int32_t *line, int32_t *master)
{
	if (!mixer)
		return;
	if (master != NULL)
		*master = volumes[Vmaster] & 0xF8;
	if (line != NULL)
		*line = volumes[Vline] & 0xF8;
	if (cd != NULL)
		*cd = volumes[Vcd] & 0xF8;
	if (midi != NULL)
		*midi = volumes[Vwave] & 0xF8;
	if (wave != NULL)
		*wave = volumes[Vwave] & 0xF8;
}

/* GetMixerInfo is M_SB16.DLL's: every volume adjustable. */
void
MixerGetInfo(int32_t *wave, int32_t *midi, int32_t *cd, int32_t *line, int32_t *master)
{
	if (!mixer)
		return;
	if (wave != NULL)
		*wave = 1;
	if (line != NULL)
		*line = 1;
	if (midi != NULL)
		*midi = 1;
	if (cd != NULL)
		*cd = 1;
	if (master != NULL)
		*master = 1;
}
