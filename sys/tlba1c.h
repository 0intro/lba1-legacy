/*
 * The Classic's music functions whose names the port's or the 1994
 * game's take, static in sys/music.c and reached through tlba1c, each
 * under its name in TLBA1C.exe. Included by compat.h and by the port
 * files that call them.
 */
typedef struct Tlba1c Tlba1c;

struct Tlba1c
{
	int32_t	(*InitCDR)(char*);
	int32_t	(*PlayTrackCDR)(uint32_t);
	void	(*StopCDR)(uint32_t);
	void	(*PauseCDR)(uint32_t);
	void	(*ResumeCDR)(uint32_t);
	int32_t	(*IsMidiPlaying)(uint32_t);
	void	(*InitMidi)(void);
	void	(*ClearMidi)(void);
	void	(*PlayMidi)(int32_t);
	void	(*StopMidi)(void);
	void	(*FadeMidiDown)(void);
	void	(*DoLoopMidi)(void);
};

extern Tlba1c	tlba1c;
