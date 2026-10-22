#define USED(x)		((void)(x))
#define nelem(a)	(sizeof(a)/sizeof((a)[0]))

enum
{
	Ahead = 8,	/* the CD's and the music's files' frames handed on before the clock passes them */
};

enum
{
	Svga,		/* the game's, VESA mode 101h in LBA.EXE */
	Mcga,		/* 320x200, mode 13h, the movies' */
};

typedef struct Vga Vga;

/* The video card: its memory, Screen_X by Screen_Y bytes, its DAC (6-bit values), its mode. */
struct Vga
{
	uint8_t	*vram;
	uint8_t	dac[256][3];
	int	mode;
};

extern Vga vga;	/* the video card (video.c) */
extern uint8_t	PalEntries[768];	/* the palette Palette last set, which SkipLogo fades (video.c) */
extern int32_t	Screen_X;	/* the screen's width, 640 or Wide_X (video.c) */
extern int32_t	Screen_Y;	/* its height, 480 */
extern int32_t	Wide_X;	/* the screen's width with WideScreen ON, set at the start (sys.c) */
extern int32_t	FlagWideScreen;	/* WideScreen ON: the screen Wide_X wide */
extern int32_t	FlagIntegerScaling;	/* IntegerScaling ON: the screen scaled by whole multiples */
extern int32_t	DesiredDisplayNum;	/* lba.cfg's DesiredDisplayNum: the display the window opens on, from 0 */
extern int32_t	FlagGamePad;	/* lba.cfg's GamePad: 1 with ON (input.c) */
extern volatile uint16_t	Joy;	/* keyboard.asm's: the arrows down, a bit each (keyb.c) */
extern volatile uint16_t	Fire;	/* the fire keys down, a bit each */
extern volatile uint32_t	InputActions;	/* the Classic's inputs held, a bit each (keyb.c, input.c) */

extern uint16_t	AsciiMode;	/* the keys passed to the BIOS's buffer too, for GetAscii (keyb.c) */
extern char	PathConfigFile[];	/* lba.cfg's name, InitAdelineSystem's (sys.c) */
extern int32_t	LbaWinSettings;	/* lba.cfg's LbaWinSettings, LbaWin's switches (settings.c) */
extern int32_t	FlagFullScreen;	/* the screen on the whole display, LbaWinFullScreen's (video.c) */
extern volatile uint32_t	TimerRef;	/* the game's time, in the PIT's ticks of 50 Hz (timer.c) */
extern int32_t	Wave_Driver_Enable;	/* the samples' driver loaded (wave.c) */
extern int32_t	PlayRate;	/* the samples' rate, in Hz */
extern int16_t	Midi_Driver_Enable;	/* the music's driver loaded (midi.c) */

/*
 * The music's sources: MusicTracks for the tunes the game asks of the
 * CD, MusicJingles for those it asks of the XMIDI, each the index of its
 * value among tracknames or jinglenames (modes.c), Auto's taken among
 * the others at the start
 */
enum
{
	Tcd,		/* the CD's tracks */
	Tmidi,		/* the XMIDI, as the disk version */
	Tclassic,	/* the Classic's files, as the Classic */
	Tauto,		/* the first of them the CD's directory holds */
};

enum
{
	Jmidi,		/* the XMIDI */
	Jplaystation,	/* the PlayStation's recordings on the CD */
	Jcd,		/* cut from the CD's tracks, as DotEmu */
	Jclassic,	/* the Classic's files, as the Classic */
	Jauto,		/* the first of them the CD's directory holds */
};

extern int32_t	MusicTracks;	/* the CD's tunes' source, a T value (modes.c) */
extern int32_t	MusicJingles;	/* the XMIDI's tunes' source, a J value */
extern char	*tracknames[];	/* MusicTracks' values' names, NULL last */
extern char	*jinglenames[];	/* MusicJingles' */

enum
{
	Ngame = 8,	/* the game's directories at most, -g's (sdl.c) */
	Ncd = 8,	/* the CDs at most, -c's (sdl.c) */
	Nrow = 10,	/* an install's binds of its own folders at most (install.c) */
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

/* a decoder of an MP3 or Ogg Vorbis file (decode.c) */
typedef struct Decoder Decoder;

/* decopen's ways of reading an MP3 */
enum
{
	Gapless,	/* its encoder's delay and padding left out */
	Padded,	/* kept as silence, as the Classic decodes its music's files */
};

/* a CD's image, a cue sheet and its files or an ISO (cdimage.c) */
typedef struct Image Image;

enum
{
	Ntune = 33,	/* the tunes, 0 to 32 */
	Nknot = 16,	/* a cut's knots at most */
	Unit = 4096,	/* a cut's gain of 1 */
};

typedef struct Knot Knot;
typedef struct Cut Cut;

/* A knot of a cut's gain: its sample from the cut's first, its gain in Units. */
struct Knot
{
	uint32_t	at;
	uint16_t	gain;
};

/*
 * A tune cut from a track of the CD as DotEmu cut it (cuts.c): its
 * samples, each a 16-bit stereo pair, from the first, the gain Unit
 * before the first knot and drawn from knot to knot, the last's after
 * it.
 */
struct Cut
{
	uint8_t	track;	/* the CD's, 0 for none */
	uint32_t	first;	/* its first sample, on the PlayStation's tracks */
	uint32_t	n;	/* its samples, 0 for the track whole */
	uint32_t	mark;	/* the CRC-32 of its 32 samples a second in */
	uint8_t	nknot;
	Knot	knot[Nknot];
};

extern Cut	cuts[Ntune];	/* each tune's cut, DotEmu's (cuts.c) */

typedef struct Wavstream Wavstream;
typedef struct Stream Stream;

/* A stream of the Classic's music: its file, the voice playing it, its code (music.c). */
struct Stream
{
	Wavstream	*stream;
	uint32_t	handle;
	uint32_t	code;
};

extern Stream	StreamCD;	/* the CD's tunes' stream (music.c) */
extern Stream	StreamMidi;	/* the XMIDI's tunes' */
extern Stream	StreamVoice;	/* the voices' */
extern int32_t	StreamLengthCD[11];	/* tracks 2 to 10's lengths, in ms */
extern int32_t	StreamLengthMidi[33];	/* tunes 0 to 32's */
extern float	MusicStreamVolume;	/* the music's streams' volume */
extern float	MixVoice;	/* the voices' */
extern int16_t	NumXmi;	/* the XMIDI's tune playing, -1 for none, -2 at the start (global.c) */

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
