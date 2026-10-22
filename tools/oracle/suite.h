enum
{
	Nfiles = 17,	/* the XMIDI files xmidi makes */
};

typedef struct Scenario Scenario;

struct Scenario
{
	char	*name;
	void	(*fn)(void);
	int	traps;		/* the INT3 of -c set */
};

/* suite.c */
extern int	indll;
extern Synth	*synthc;
extern uint32_t	seed;
void	crcinit(void);
uint32_t	crc(uint32_t, const uint8_t*, size_t);
uint32_t	seqcrc(uint32_t, uint8_t*);
uint32_t	rnd(uint32_t);
void	emitio(uint32_t, uint32_t, uint32_t);
void	step(char*, ...);
int	scenario(char*, Scenario*);
void	asked(char*, char**, int, Scenario*, int);
void	readblocks(char*);
void	coverblocks(void);
void	coverage(char*);
uint8_t*	xmidi(int);
uint8_t*	xmidictrl(void);
void	sequencer(void);
void	sequences(void);
void	queue(void);
void	corners(void);

uint32_t	api(int, int, ...);

/* the oracle's */
uint8_t*	timbreof(uint32_t, uint32_t);
uint32_t	statecrc(void);
void	runinit(void);
void	initmidi(void);
void	interval(void);
