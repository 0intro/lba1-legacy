#define USED(x)		((void)(x))
#define nelem(a)	(sizeof(a)/sizeof((a)[0]))

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
extern volatile uint32_t	TimerRef;	/* the game's time, in the PIT's ticks of 50 Hz (timer.c) */

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
