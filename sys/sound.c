#include <stddef.h>
#include <stdint.h>
#include "dat.h"
#include "fns.h"

/*
 * Sound, as lib386's lib_samp (samples and voices), lib_midi (the music
 * through AIL/32) and lib_mix (the card's mixer) offered it. Every
 * driver is taken for none, whatever the configuration names, and the
 * calls answer as lib_samp's, lib_midi's and lib_mix's did without one
 * loaded: 0, and nothing done. The game is silent and offers the
 * music's volume alone.
 */

int32_t	Wave_Driver_Enable;
int16_t	Midi_Driver_Enable;
int32_t	MaxVolume = 100;

void
soundinit(void)
{
	Wave_Driver_Enable = 0;
	Midi_Driver_Enable = 0;
}

uint32_t
WavePlay(uint16_t handle, uint16_t pitch, uint16_t repeat, uint8_t plug,
	uint16_t volleft, uint16_t volright, void *buf)
{
	USED(handle);
	USED(pitch);
	USED(repeat);
	USED(plug);
	USED(volleft);
	USED(volright);
	USED(buf);
	return 0;
}

void
WaveGiveInfo0(uint32_t longhandle, uint32_t info)
{
	USED(longhandle);
	USED(info);
}

void
WaveStop(void)
{
}

void
WaveStopOne(uint16_t handle)
{
	USED(handle);
}

void
WaveStopOneLong(uint32_t longhandle)
{
	USED(longhandle);
}

int
WaveInList(uint16_t handle)
{
	USED(handle);
	return 0;
}

int
WaveGetSnap(void **buf)
{
	USED(buf);
	return 0;
}

int
WavePause(void)
{
	return 0;
}

void
WaveContinue(void)
{
}

void
WaveSaveState(void)
{
}

void
WaveRestoreState(void)
{
}

void
WaveChangeVolume(uint32_t longhandle, uint32_t volleft, uint32_t volright)
{
	USED(longhandle);
	USED(volleft);
	USED(volright);
}

void
WaveMove(void *dst, void *src, uint32_t n)
{
	USED(dst);
	USED(src);
	USED(n);
}

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

void
MixerChangeVolume(int32_t wave, int32_t midi, int32_t cd, int32_t line, int32_t master)
{
	USED(wave);
	USED(midi);
	USED(cd);
	USED(line);
	USED(master);
}

void
MixerGetVolume(int32_t *wave, int32_t *midi, int32_t *cd, int32_t *line, int32_t *master)
{
	USED(wave);
	USED(midi);
	USED(cd);
	USED(line);
	USED(master);
}

void
MixerGetInfo(int32_t *wave, int32_t *midi, int32_t *cd, int32_t *line, int32_t *master)
{
	USED(wave);
	USED(midi);
	USED(cd);
	USED(line);
	USED(master);
}
