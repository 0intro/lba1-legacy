/* cdimage.c */
Image*	imageopen(char*, char*, int);
void	imageclose(Image*);
int	imagefile(Image*, char*, uint32_t*, uint32_t*);
uint32_t	imageread(Image*, uint32_t, uint32_t, uint8_t*, uint32_t);
int	imagedir(Image*, char*);
int	imagelist(Image*, char*, void(*)(void*, char*, int, uint32_t, uint16_t, uint16_t), void*);
char*	imagelabel(Image*);
uint32_t	imageaudio(Image*, int32_t);
int32_t	tracknum(const char*);
int	imagesound(Image*, char*, int32_t*, uint32_t*, uint32_t*);
uint32_t	imagetrackread(Image*, int32_t, uint64_t, uint8_t*, uint32_t);

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
unsigned	watcomseed(void);

/* files.c */
void	setpref(char*);
void	setdirs(char*, char**, int, char**, int);
int	bindpath(int, char*, char*, char*, int);
int	bindfile(int, char*, char*, char*, int);
int	bindtext(int, char*, const char*, char*, int);
void	bindconfig(char*, const char*);
int	findhost(const char*, const char*, char*, int);
int	havefile(char*);
int	havecd(void);
int	cdimagefile(char*, Image**, uint32_t*, uint32_t*);
int	cdtrack(char*, Image**, int32_t*, uint32_t*, uint32_t*);
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
uint64_t	framepace(uint64_t, uint64_t);
int	vsyncframes(uint64_t, uint64_t);
int32_t	SetDisplaySync(int32_t);
void	displayvsync(void);

/* keyb.c */
void	xtcode(int);
void	biosword(int);

/* syskeys.c */
int	refusealtf4(void);
int	refusewinkeys(void);
int	hookkey(uint32_t, uint32_t);
int	altf4(int);
int	altf4word(int);
void	hookkeys(int);

/* input.c */
int	usvkey(int, int);
void	vkcode(int, int);
void	vkkey(int, int);
void	KeyDown(void);
void	KeyUp(int32_t);
void	InitBindings(int32_t, uint32_t*, uint32_t*);
void	ReadInputs(uint32_t);
void	SetPadButton(int, int);
int	TestPadButton(int);
uint32_t	GetBinding(int);
void	PadButton(int, int);
void	ClearInputs(void);
void	SetInputContext(int32_t);
int	AnyInput(void);
int32_t	GetBindInput(int);

/* replay.c */
void	recordinput(char*);
void	recorddisplay(int, int);
void	playinput(char*);
int	playing(void);
void	portexit(int);
void	keyin(int, int);
void	padin(int, int);
void	vkin(int, int);
void	wordin(int);
void	replay(uint64_t);
void	sumto(char*);
void	shotat(uint32_t, uint32_t, uint32_t);
void	summed(uint32_t, uint64_t);
void	summedwave(uint64_t, const uint32_t*, int);
void	summedcd(uint64_t, const uint8_t*, int);
void	summedmidi(uint64_t, int16_t*, int);
void	waveto(char*);
void	musicto(char*);

/* wide.c */
int32_t	screenwidth(int, int);

/* modes.c */
int	readmode(char*, char*, char**);
int	readswitch(char*, char*, int);
void	readplatform(char*, int*);
int32_t	gamekey(char*, char*);
char*	gamekeyname(int);

/* video.c */
int32_t	SetSmoothScaling(int32_t);
void	videoinit(void);
void	pushscreen(void);
void	pushwindow(void);
void	poparea(void);
void	windowpicture(void*);
void	writewide(void*);
void	PalMulti(int16_t, int16_t, uint8_t*);

/* sound.c */
void	soundinit(void);

/* wave.c */
void	wavetick(uint64_t);
void	wavestate(uint8_t*);

/* cd.c */
void	cdtick(uint64_t);

/* xmidi.c */
uint32_t	uldivide(uint32_t, uint32_t);
void	servedriver(void);
void	initdriver(Synth*, uint32_t, uint32_t, uint32_t, uint32_t);
void	shutdowndriver(char*);
uint32_t	statesize(void);
void	installcallback(void (*)(int32_t, int32_t));
void	cancelcallback(void);
int32_t	registerseq(uint8_t*, uint32_t, void*, uint8_t*);
void	releaseseqhandle(int32_t);
void	stopseq(int32_t);
void	startseq(int32_t);
void	resumeseq(int32_t);
int32_t	seqstatus(int32_t);
int32_t	beatcount(int32_t);
int32_t	barcount(int32_t);
void	mapseqchannel(int32_t, uint32_t, uint32_t);
int32_t	trueseqchannel(int32_t, uint32_t);
void	branchindex(int32_t, uint32_t);
int32_t	reltempo(int32_t);
int32_t	relvolume(int32_t);
void	setreltempo(int32_t, int32_t, uint32_t);
void	setrelvolume(int32_t, int32_t, uint32_t);
int32_t	controlval(int32_t, uint32_t, uint32_t);
void	setcontrolval(int32_t, uint32_t, uint32_t, uint32_t);
int32_t	channotes(int32_t, uint32_t);
uint8_t*	seqtimb(int32_t);
int32_t	lockchannel(void);
void	releasechannel(uint32_t);
void	xmidistate(uint8_t*);
void	xmidiseqstate(int32_t, uint8_t*);

/* yamaha.c */
void	yamahastate(uint8_t*);

/* awe.c: A32GFX.DLL's dead functions */
void	aweoutpw(uint32_t, uint32_t);
uint32_t	aweinpw(uint32_t);
uint32_t	awe32detect(uint32_t);
void	aweseteq(void);
void	aweinitfm(void);
void	awereleaseall(void);
uint32_t	awenoteoff(uint32_t, uint32_t);
uint32_t	aweclamp16(uint32_t, uint32_t, uint32_t);
uint32_t	aweclamp127(uint32_t);
uint32_t	awesearch(uint32_t);

/* emu8k.c */
void	emu8kreset(uint16_t*, void (*)(int32_t*, int));
void	emu8kout(uint32_t, uint32_t);
uint32_t	emu8kin(uint32_t);
void	emu8krun(uint64_t);
uint64_t	emu8kmade(void);

/* emu8kfx.c */
void	emu8kfxreset(void);
void	emu8kfxinit(uint16_t (*)[32], uint32_t);
void	emu8kfx(int32_t, int32_t, int32_t*, int32_t*);

/* oplio.c */
void	oplioreset(uint32_t);
void	oplportout(uint32_t, uint32_t);
uint32_t	oplportin(uint32_t);

/* midi.c */
void	oplwrite(uint32_t, uint32_t);
void	oplout(uint32_t, uint32_t);
uint32_t	oplin(uint32_t);
void	aweout(uint32_t, uint32_t);
uint32_t	awein(uint32_t);
int	midiinit(void);
void	midimix(int32_t, int32_t, float);
void	ClearMidi(void);
void	miditick(uint64_t);

/* sdl.c */
void	sdlfilter(int);
void	sdlgamepad(void);
void	sdlpadclear(void);
void	fatal(char*, ...);
void	sdldesktop(int*, int*);
void	sdldisplay(void);
void	setdisplay(int, int);
void	sdlscreen(void);
void	sdlsize(void);
void	sdlclose(void);
uint64_t	sdlnanos(void);
void	sdlsleep(uint64_t);
void	sdlshow(void);
uint64_t	sdlrefresh(void);
void	setrefresh(uint64_t);
uint64_t	sdlwindowframe(void);
void	sdlvsync(int);
void	sdlpoll(void);
void	sdlwave(const uint32_t*, int, int);
void	sdlwavegain(float);
void	sdlcd(const void*, int);
void	sdlcdstop(void);
void	sdlcdgain(float);
void	sdlmidi(int16_t*, int, int);
void	sdlmidigain(float);
void	sdllate(void);
void	sdlfullscreen(int);
int32_t	SetFullScreen(int32_t);

/* the game's */
int	lbamain(int, char**);
unsigned char*	Def_ReadString(unsigned char*, unsigned char*);
int32_t	Def_ReadValue2(unsigned char*, unsigned char*, int32_t*);
int32_t	Def_WriteValue(unsigned char*, unsigned char*, int32_t);
int32_t	Def_WriteString(unsigned char*, unsigned char*, unsigned char*);
void	ReadLbaWinSettings(void);
uint32_t	Load(char*, void*);
