#define USED(x)		((void)(x))
#define nelem(a)	(sizeof(a)/sizeof((a)[0]))

enum
{
	Ahead = 8,	/* the CD's frames handed on before the clock passes them */
};

enum
{
	Svga,		/* the game's, VESA mode 101h in LBA.EXE */
	Mcga,		/* 320x200, mode 13h, the movies' */
};

typedef struct Vga Vga;

/* The video card: its memory, its DAC (6-bit values), its mode. */
struct Vga
{
	uint8_t	vram[640*480];
	uint8_t	dac[256][3];
	int	mode;
};

extern Vga vga;	/* the video card (video.c) */

extern uint16_t	AsciiMode;	/* the keys passed to the BIOS's buffer too, for GetAscii (keyb.c) */
extern char	PathConfigFile[];	/* lba.cfg's name, InitAdelineSystem's (sys.c) */
extern volatile uint32_t	TimerRef;	/* the game's time, in the PIT's ticks of 50 Hz (timer.c) */
extern int32_t	Wave_Driver_Enable;	/* the samples' driver loaded (wave.c) */
extern int32_t	PlayRate;	/* the samples' rate, in Hz */
extern int16_t	Midi_Driver_Enable;	/* the music's driver loaded (midi.c) */
extern char	MidiFM;	/* MidiType FM: MIDI_SB.HQR's tunes, the OPL3's (sys.c) */

enum
{
	Ngame = 8,	/* the game's directories at most, -g's (sdl.c) */
	Ncd = 8,	/* the CDs at most, -c's (sdl.c) */
};

/* sys/lba.cfg's bytes and a 0, made by the Makefile: the configuration's text (sys.c) */
extern uint8_t	lbacfg[];

/* The calls files.c's writer makes of its lock, sdl.c's (setwriter) */
enum
{
	Wlock,
	Wunlock,
	Wwait,		/* the lock given up until a wake, then taken back */
	Wwake,		/* every waiter woken */
};

/* a CD's image, a cue sheet and its files or an ISO (cdimage.c) */
typedef struct Image Image;

typedef struct Drvrdesc Drvrdesc;
typedef struct Synth Synth;

/* AIL's drvr_desc: what an XMIDI driver's describe_driver gives. */
struct Drvrdesc
{
	uint32_t	minapi;
	uint32_t	type;
	char	suffix[4];
	char	*names;		/* the devices, each NUL-ended, an empty one last */
	int32_t	io;
	int32_t	irq;
	int32_t	dma;
	int32_t	drq;
	int32_t	rate;		/* the service's, in Hz */
	uint32_t	display;
};

/*
 * An XMIDI driver's synthesizer, what xmidi32.asm includes: the
 * procedures the sequencer calls, and those of the driver's index the
 * synthesizer gives.
 */
struct Synth
{
	Drvrdesc	*desc;
	void	(*setioparms)(uint32_t, uint32_t, uint32_t, uint32_t);
	void	(*reset)(void);
	void	(*init)(void);
	uint32_t	(*send)(uint32_t, uint32_t, uint32_t);
	void	(*shutdown)(void);
	int32_t	(*detect)(uint32_t, uint32_t, uint32_t, uint32_t);
	uint32_t	(*cvmsg)(uint32_t, uint32_t, uint32_t);
	void	(*sysexmsg)(uint32_t, uint32_t, uint32_t, uint32_t, uint8_t*, uint32_t);
	void	(*display)(char*);
	uint32_t	(*cachesize)(void);
	void	(*definecache)(void*, uint32_t);
	int32_t	(*request)(int32_t);
	void	(*install)(uint32_t, uint32_t, uint8_t*);
	void	(*protect)(uint32_t, uint32_t);
	void	(*unprotect)(uint32_t, uint32_t);
	int32_t	(*status)(uint32_t, uint32_t);
};

extern Synth	opl3;	/* the OPL3's synthesizer (yamaha.c) */

