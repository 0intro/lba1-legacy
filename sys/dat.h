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
extern int32_t	LbaWinSettings;	/* lba.cfg's LbaWinSettings, LbaWin's switches (settings.c) */
extern int32_t	FlagFullScreen;	/* the screen on the whole display, LbaWinFullScreen's (video.c) */
extern volatile uint32_t	TimerRef;	/* the game's time, in the PIT's ticks of 50 Hz (timer.c) */
extern int32_t	Wave_Driver_Enable;	/* the samples' driver loaded (wave.c) */
extern int32_t	PlayRate;	/* the samples' rate, in Hz */
extern int16_t	Midi_Driver_Enable;	/* the music's driver loaded (midi.c) */

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
extern Synth	awe32;	/* the AWE32's (awe.c) */

/*
 * A32GFX.DLL's library, Creative's for the AWE32, its records laid out
 * as its Watcom /zp1 did, its data where the DLL holds it.
 */
enum
{
	/* a preset's tags, their values the layer's from Ttune on */
	Tstart, Tloopstart, Tloopend,
	Ttune, Tlfo1pitch, Tlfo2pitch, Tenvpitch, Tcutoff, Tq, Tlfo1cutoff,
	Tenvcutoff, T11, Tlfo1vol, T13, Tchorus, Treverb, Tpan, T17, T18,
	T19, Tlfo1delay, Tlfo1freq, Tlfo2delay, Tlfo2freq, Tmoddelay,
	Tmodattack, Tmodhold, Tmoddecay, Tmodsustain, Tmodrelease,
	Tvoldelay, Tvolattack, Tvolhold, Tvoldecay, Tvolsustain,
	Tvolrelease, Tkeyhold, Tkeydecay, T38, T39, Tkeys, Tvels, T42, Tkey,
	Tvel, Tatten, T46, T47, Tcoarse, Tfine, Tend, Tmodes, Troot, Tscale,
	T54, Tzone, Tinstr, Tback,

	Awevoices = 30,
};

typedef struct Awevoice Awevoice;
typedef struct Awechan Awechan;
typedef struct Awestate Awestate;
typedef struct Layer Layer;
typedef struct Aweparse Aweparse;
typedef struct Awemod Awemod;
typedef struct Awedir Awedir;

#pragma pack(push, 1)
struct Awevoice
{
	uint16_t	chan;		/* 0xffff: free since initvoices */
	uint16_t	key;		/* chan<<8 | key, 0xff free */
	uint16_t	unused0;
	uint16_t	vel;
	uint16_t	atten;
	uint16_t	unused1[4];
	uint16_t	release;	/* DCYSUSV's on a release, less its bit 15 */
	uint16_t	modrelease;	/* DCYSUS's */
	uint32_t	pitch;		/* IP before the bend */
};

struct Awechan
{
	uint16_t	preset;		/* 0x80: the drum kit */
	uint16_t	unused;
	uint16_t	reverb;
	uint16_t	chorus;
	uint16_t	pan;
	uint16_t	aux;
	uint16_t	sustain;
	uint16_t	volume;
	uint16_t	modulation;
	uint16_t	rpndata;	/* RPN 0's, the bend's range in semitones above */
	uint16_t	bank;
	uint16_t	banksel;
	int32_t	bend;		/* IP's units */
	uint16_t	dataentry;
};

/* the region at 0x11fc6 */
struct Awestate
{
	uint16_t	rover;
	uint16_t	voice;		/* the last given */
	uint16_t	key;
	uint8_t	pad[4];
	Awevoice	voices[Awevoices];
	Awechan	chans[16];
	uint16_t	unused[4];
};

/* a zone's parameters: the addresses, then a word a tag from Ttune */
struct Layer
{
	uint32_t	addr[3];
	int16_t	p[52];
};

/* the region at 0x13472 */
struct Aweparse
{
	uint16_t	cursor;
	uint16_t	finetune;
	uint16_t	coarsetune;
	uint16_t	rpn;
	uint16_t	nrpn;
	uint16_t	pad;
	Layer	layers[4];
	Layer	cur;
};

struct Awemod
{
	uint8_t	tag;
	uint32_t	val;
};

struct Awedir
{
	uint16_t	size;		/* its low 15 bits read */
	uint16_t	off;
	uint32_t	program;
};
#pragma pack(pop)

extern Drvrdesc	aweddt;	/* the AWE32 driver's descriptor (awedata.c) */
extern char	awenames[0x1e];	/* the devices it names */
extern uint32_t	awemixaddr;	/* yamaha32.inc's ports for SBPRO2, which set_IO_parms sets */
extern uint32_t	awemixdata;
extern uint32_t	awedataout;
extern uint32_t	aweaddrstat;
extern uint16_t	aweports[8];	/* the EMU8000's ports */
extern uint16_t	aweinits[4][4][32];	/* the INIT arrays' four sets */
extern Awestate	awestate;	/* the voices and the channels */
extern uint8_t	aweeqlevels[2];	/* the equalizer's levels, the treble's and the bass's */
extern uint16_t	aweeq[12][8];	/* its words by level */
extern uint8_t	aweunref[132];	/* a dead table */
extern uint16_t	awesearchtab[128];	/* the dead search's table */
extern uint16_t	awedecaytab[128];	/* the decay times by code, descending */
extern Aweparse	aweparse;	/* the parser's state */
extern Layer	awedeflayer;	/* the layer every zone starts from */
extern uint8_t	awedeflayerpad[6];	/* the six bytes after it */
extern uint8_t	awetagsize[58];	/* the bytes of each tag's value */
extern Awemod	awemods[48];	/* the zone's modifiers */
extern uint16_t	aweatten[384];	/* the attenuation a velocity and a volume give */
extern Awedir	awedir[136];	/* the presets, a program each and the drum kit last */
extern uint8_t	awepresets[0x49d7];	/* the presets' zones, tagged records */

/* emu8ktab.c: the EMU8000's tables (tools/emu8ktab.py) */
extern uint32_t	emu8kexp2[4097];
extern uint16_t	emu8klog2[4097];
extern int16_t	emu8kkernel[257][4];
extern int32_t	emu8kfilter[16][256][3];
