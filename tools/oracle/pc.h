/* pc.c: the PC a DOS build ran on, under the oracles that run its code */

enum
{
	Lowbase = 0x80000,	/* DOS memory, which DPMI's 0100h gives: the image's data end below */
	Nshown = 20,	/* failures of a run reported */
	Nlabel = 1024,
};

typedef struct Cpu Cpu;

/* the registers of an int, as its handler takes them and gives them back */
struct Cpu
{
	uint32_t	eax, ebx, ecx, edx, esi, edi, ebp;
	uint32_t	flags;	/* carry 1, zero 40h */
	uint32_t	ds, es;
	int	real;	/* passed down by DPMI's 0300h: ds and es are segments */
};

extern char	*oracle;	/* the program, for its reports */
extern char	where[Nlabel], what[Nlabel];	/* the run and the call */
extern int	nfail;	/* failures in this run */
extern uint32_t	lowend;	/* DOS memory's end, A0000h unless the oracle moves it before pcload */
extern uint32_t	pccs;	/* this process's code selector */
extern int	ncx;	/* DPMI 0300h calls given a CX other than 0 */
extern uint32_t	cxlast;
extern int	neoi;	/* ends of interrupt sent to the PIC */
extern uint32_t	dpmifail;	/* the DPMI function made to fail when nonzero */
extern int	ndpmifail;	/* its calls to fail before it answers, -1 for every one */
extern int	(*pcfault)(void*);	/* a fault past pc.c's decoding, given its ucontext: 1 if taken */

void	pcload(char*, char*);
uintptr_t	lookup(char*);
uintptr_t	symaddr(char*);
void	pcint(int, void (*)(Cpu*));
void	pcport(uint32_t, uint32_t, uint32_t (*)(uint32_t, int), void (*)(uint32_t, uint32_t, int));
void	pcbios(int, void (*)(void));
void	pcvector(int, uint32_t*, uint32_t*);
int	chained(int);
void	setlo(uint32_t*, uint32_t);
void	put16(uint8_t*, uint32_t);
void	put32(uint8_t*, uint32_t);
uint8_t*	lowmem(uint32_t, uint32_t, uint32_t);
uint8_t*	cpumem(Cpu*, uint32_t, uint32_t, uint32_t);
uint32_t	lowalloc(uint32_t);
int32_t	acall(uintptr_t, int, ...);
int32_t	cxcall(uintptr_t, int, int32_t*, uint32_t);
void	intcall(uintptr_t);
int	intok(char*);
void	cxnone(void);
int	inchild(char*, int, void (*)(void*), void*);
void	runend(void);
int	incode(uint32_t);
