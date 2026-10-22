/* compat.c */
int	watcomstricmp(const char*, const char*);
int	watcomstrnicmp(const char*, const char*, size_t);
char*	watcomstrupr(char*);
char*	watcomitoa(int, char*, int);
void	watcomsplitpath(const char*, char*, char*, char*, char*);
char*	watcomgetcwd(char*, int);
int	watcomchdir(const char*);

/* qsort.c */
void	watcomqsort(void*, size_t, size_t, int (*)(const void*, const void*));

/* rand.c */
int	watcomrand(void);
void	watcomsrand(unsigned);

/* files.c */
void	setpref(char*);
void	setdirs(char*, char**, int, char**, int);
int	bindpath(int, char*, char*, char*, int);
int	bindfile(int, char*, char*, char*, int);
int	bindtext(int, char*, const char*, char*, int);
void	bindconfig(char*, const char*);
int	havefile(char*);
int	havecd(void);
void	setcdletter(int);
int	setprogdir(const char*);
int	readpath(char*, char*, int);
int32_t	OpenRead(char*);
int32_t	OpenWrite(char*);
uint32_t	Read(int32_t, void*, uint32_t);
uint32_t	Write(int32_t, void*, uint32_t);
void	Close(int32_t);
int32_t	Seek(int32_t, int32_t, int32_t);
uint32_t	FileSize(char*);
int32_t	renamefile(char*, char*);
void	setwriter(void(*)(int));
void	writer(void);
int	stepwrite(void);
void	endwrites(void);

/* mem.c */
void	meminit(void);
void*	Malloc(int32_t);
void*	HostMalloc(int32_t);
void	Free(void*);

/* timer.c */
void	clockinit(void);
void	pump(void);
void	idle(void);
uint64_t	pumps(void);
uint32_t	shown(void);
void	vsync(void);
void	framesync(void);

/* keyb.c */
void	xtcode(int);
void	biosword(int);

/* replay.c */
void	recordinput(char*);
void	playinput(char*);
int	playing(void);
void	portexit(int);
void	keyin(int);
void	wordin(int);
void	replay(uint64_t);
void	sumto(char*);
void	shotat(uint32_t, uint32_t, uint32_t);
void	summed(uint32_t, uint64_t);

/* video.c */
void	videoinit(void);
void	PalMulti(int16_t, int16_t, uint8_t*);

/* sound.c */
void	soundinit(void);

/* sdl.c */
void	fatal(char*, ...);
void	sdlclose(void);
uint64_t	sdlnanos(void);
void	sdlsleep(uint64_t);
void	sdlshow(void);
void	sdlpoll(void);

/* the game's */
int	lbamain(int, char**);
int32_t	Def_ReadValue2(unsigned char*, unsigned char*, int32_t*);
int32_t	Def_WriteValue(unsigned char*, unsigned char*, int32_t);
