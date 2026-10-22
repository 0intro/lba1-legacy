/* asmoracle: the game's assembly, as a DOS build links it, against its translations into C */

#define nelem(a)	(sizeof(a)/sizeof((a)[0]))

enum
{
	Rounds = 100000,	/* of a module's test */
};

/* asmoracle.c */
void	fatal(char*, ...);
void	fail(char*, ...);
int	failures(void);
unsigned char*	readfile(char*, long*);
uint32_t	get16(uint8_t*);
uint32_t	get32(uint8_t*);
uint32_t	rnd(void);
uint32_t	rndn(uint32_t);
int32_t	rndr(int32_t, int32_t);
uintptr_t	lookup(char*);
int	segments(uint32_t*, uint32_t*, int);
int32_t	acall(uintptr_t, int, ...);
extern int	exhaustive;
extern char	*gamedir;	/* the game's files, or NULL: the tests then take random data alone */

/* the modules' tests */
void	testpfunc(void);	/* afunc.c */
void	testtrigo(void);	/* atrigo.c */
void	testanim(void);	/* aanim.c */
void	testline(void);	/* asvga.c */
void	testbox(void);
void	testblock(void);
void	testscreen(void);
void	testgraph(void);
void	testmask(void);
void	testfont(void);
void	testfla(void);
void	testcpymask(void);
void	testfire(void);
void	testfunc(void);
void	testpoly(void);
void	testtexture(void);
void	testobj(void);
void	testgrille(void);	/* agrille.c */
void	testsmall(void);	/* amisc.c */

/* the inputs where LBA.EXE's assembly faults, runs on or gives the wrong result, which the fixes mend in both */
int	fixfunc(void);	/* afunc.c */
int	fixanim(void);	/* aanim.c */
int	fixsvga(void);	/* asvga.c */
int	fixtrigo(void);	/* atrigo.c */
int	fixgrille(void);	/* agrille.c */
