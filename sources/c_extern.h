#include	"defines.h"

/*══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

#include	"ambiance.def"
#include	"diskfunc.def"
#include	"extra.def"
#ifndef	DOS
#include	"exttext.def"
#endif
#include	"fiche.def"
#include	"flipbox.def"
#include	"gamemenu.def"
#include	"gerelife.def"
#include	"geretrak.def"
#include	"grille.def"
#include	"holomap.def"
#include	"incrust.def"
#include	"message.def"
#include	"object.def"
#include	"perso.def"
#include	"playfla.def"

/* prototype */

char    *GetMultiText( LONG text, char *dst ) ;

WORD	GiveObjSelect( WORD type, LONG xm, LONG ym ) ;

UBYTE	WorldColBrick( WORD xw, WORD yw, WORD zw ) ;
/* declared for C99: DOS calls them undeclared, as LBA.EXE was compiled */
#ifndef	DOS
LONG	WorldColBrickFull( LONG xw, LONG yw, LONG zw, LONG ymax ) ;
LONG	WorldCodeBrick( LONG xw, LONG yw, LONG zw ) ;
void	Map2Screen( LONG xm, LONG ym, LONG zm ) ;
void	DecompColonne( void *pts, void *ptd ) ;
void	MixteColonne( void *pts, void *ptd ) ;
#endif

#ifndef	DOS
UBYTE	*GetAdrBlock( LONG numblock ) ;

void	BlackFrame( void ) ;
void	CopyFrame( void *ptsrc ) ;
void	DrawFrame( void *ptframe, LONG deltax, LONG deltay ) ;
void	UpdateFrame( void *ptframe, LONG deltax ) ;
void	Mcga_Cls( void ) ;
void	Mcga_Flip( void ) ;

void	DoFire( LONG lig, LONG coul ) ;

LONG	ComputeTime( void ) ;
LONG	GetHDFreeSize( void ) ;
void	ShadeBox( LONG x0, LONG y0, LONG x1, LONG y1, LONG deccoul ) ;
void	SmallSort( void *objetlist, LONG nbobjets, LONG structsize ) ;
void	CopyBlockMCGA(	LONG x0, LONG y0, LONG x1, LONG y1, void *src,
			LONG xd, LONG yd, void *dst ) ;
#endif

WORD	ThrowExtra(	WORD owner,
			WORD x, WORD y, WORD z, WORD num,
			WORD alpha, WORD beta, WORD vitesse, WORD poids,
			WORD hitforce ) ;
WORD	GetValue( void ) ;

WORD	GetListObjet( WORD x0, WORD y0 ) ;

LONG	SceReadWord( void ) ;
WORD	SearchLastZoneSce( void ) ;

ULONG	HQ_MixSample( WORD numsample, WORD decalage, WORD repeat, WORD volleft, WORD volright ) ;
void	HQ_StopOneSample( WORD num ) ;

void	*LoadTestMalloc( char *filename ) ;

WORD	InitAnim( UBYTE gen_anim, WORD flag, UBYTE gen_nextanim, WORD numobj ) ;
WORD	SearchAnim( UBYTE numanim, WORD numobj ) ;
WORD	SearchBody( UBYTE numbody, WORD numobj ) ;
void	LoadFic( UBYTE indexfileobjet, WORD numobj ) ;

/*--------- divers ---------------------------------------------*/

extern	UWORD	*BufOrder	;
extern	UBYTE	*BufText 	;
extern	UBYTE	*BufSpeak 	;

extern	UBYTE	GamePathname[] ;

extern	WORD	ComportementDebug;

extern	UBYTE	*LbaFont ;
extern	UBYTE	*Screen	;
extern	UBYTE	PalettePcx[] ;
extern	UBYTE	*PtrPal ;

extern	WORD	MyJoy ;
extern	WORD	MyFire ;
extern	WORD	MyKey ;
#ifndef	DOS
extern	ULONG	MyActions ;
#endif

extern	WORD	LastFire ;

extern	LONG	FlagVsync ;
extern	WORD	FlagMCGA ;
extern	LONG	FlagCredits ;

extern	UBYTE	String[] ;
extern	UBYTE	EditString[] ;
extern	WORD	Value ;

extern	LONG	SampleVolume, MusicVolume, CDVolume, LineVolume, MasterVolume ;
extern	WORD	SamplesEnable ;
extern	UBYTE	*PtrSample[] ;

extern	WORD	QuickMessage ;
extern	LONG	FlecheForcee ;

extern	WORD	FlagPalettePcx ;
extern	WORD	FlagFade ;
extern	WORD	FlagMessageShade ;
extern	WORD	Bulle ;
extern	WORD	FlagBlackPal ;

extern	WORD	FlaFromCD ;
extern	UBYTE	PathFla[] ;

extern	LONG	Version_US ;

/*-------- RESSOURCES ---------------------------------------------*/

extern	T_HQR_HEADER	*HQR_Samples ;
extern	T_HQR_HEADER	*HQR_Midi ;
extern	T_HQR_HEADER	*InventoryObj ;

/*-------- DISKFUNC.C ---------------------------------------------*/

extern	char	PleaseWait[] ;

extern	UBYTE	ProgDrive[] ;
extern	UBYTE	ProgDir[] ;

extern	ULONG	OrgDrive ;
extern	UBYTE	OrgDir[] ;

extern	UBYTE	Drive[] ;
extern	UBYTE	Dir[] ;
extern	UBYTE	Name[] ;
extern	UBYTE	Ext[] ;

extern	UBYTE	OrgPathname[] ;
extern	UBYTE	ProgramPathname[] ;

extern	UBYTE	Pathname[] ;

extern	UBYTE	FliPathname[_MAX_PATH] ;

/*-------- SCRIPT.C ---------------------------------------------*/

extern	UBYTE	FicName[] ;
extern	UBYTE	*PtrFiche ;
extern	UBYTE	*PtrCommand ;
extern	UBYTE	*PtrFicName ;

/*-------- FLIP_BOX.C ---------------------------------------------*/

extern	WORD	NbPhysBox ;
extern	WORD	NbOptPhysBox ;

/*-------- LIB_SVGA ---------------------------------------------*/

extern	WORD	NbPolyPoints ;

/*-------- life tool ---------------------------------------------*/

extern	WORD	ErrorFlag ;

extern	WORD	FlagInfos ;

extern	WORD	Modif ;

extern	UBYTE	DefPathname[] ;
extern	UBYTE	ScePathname[] ;
extern	UBYTE	PcxPathname[] ;

extern	WORD	FlagFrame ;

extern	T_ZONE	*ListZone ;
extern	WORD	NbZones ;

extern	WORD	IndexZone ;
extern	WORD	EditNumZone ;
extern	WORD	EditTypeZone ;
extern	WORD	VisuTypeZone ;

extern	UBYTE	*ListTypeZone[] ;

extern	T_TRACK	*ListBrickTrack ;
extern	WORD	NbBrickTrack ;

extern	WORD	IndexBrickTrack ;
extern	WORD	EditNumTrack ;

extern	WORD	ActiveEdit ;

/*
extern	T_FLAG	ListFlagCube[] ;
extern	T_FLAG	ListFlagGame[] ;
*/
extern	UBYTE	ListFlagCube[] ;
extern	UBYTE	ListFlagGame[] ;

extern	UBYTE	ListFlagInventory[] ;


extern	UBYTE	*ListMove[] ;

/*--------- holomap ----------------------------*/

//extern	T_HOLO_OBJ	ListHoloObj[MAX_HOLO_OBJ] ;
//extern	WORD		NbHoloObj ;

extern	T_HOLO_POS	*ListHoloPos ; // [MAX_HOLO_POS] ;
extern	UBYTE	TabHoloPos[] ;

/*--------- menus -----------------------------*/

extern	WORD	GameChoice ;
extern	WORD	GameNbChoices ;
extern	WORD	GameListChoice[] ;

/*--------- perso ----------------------------*/

extern	WORD	FlagTheEnd ;

extern	WORD	ActionNormal ;
extern	WORD	InventoryAction ;
extern	WORD	Weapon ;

extern	WORD	Comportement ;
#ifndef	DOS
extern	WORD	MemoComportement ;
#endif
extern	WORD	AnimRien ;
extern	WORD	AnimRienNormal, AnimRienSportif, AnimRienAgressif, AnimRienDiscret, AnimRienProtopack ;

extern	UBYTE	*PtrFile3dNormal ;
extern	UBYTE	*PtrFile3dSportif ;
extern	UBYTE	*PtrFile3dAgressif ;
extern	UBYTE	*PtrFile3dDiscret ;
extern	UBYTE	*PtrFile3dProtopack ;

extern	WORD	SceZoom ;
extern	WORD	Shadow ;
extern	WORD	ShadowX, ShadowY, ShadowZ ;
extern	UBYTE	ShadowCol ;

extern	WORD	NumHolomapTraj ;
extern	WORD	FlagChgCube, NewCube, NumCube ;
extern	WORD	NewPosX, NewPosY, NewPosZ ;
extern	WORD	SceneStartX, SceneStartY, SceneStartZ ;
extern	WORD	CubeStartX, CubeStartY, CubeStartZ ;
extern	WORD	GameOverCube ;

extern	WORD	CombatAuto ;
extern	WORD	MagicBall ;
extern	WORD	MagicBallType ;
extern	WORD	MagicBallCount ;

extern	WORD	MagicLevel ;
extern	WORD	MagicPoint ;
extern	WORD	NbGoldPieces ;
extern	WORD	NbLittleKeys ;
extern	WORD	Chapitre ;
extern	UBYTE	Island 	;
extern	WORD	Fuel ;
extern	WORD	NumPingouin ;

extern	WORD	NbFourLeafClover ;
extern	WORD	NbCloverBox ;

extern	WORD		FlagClimbing ;
extern	WORD		StartYFalling ;
extern	T_REAL_VALUE	RealFalling ;
extern	WORD		StepFalling ;

extern	UBYTE	CodeJeu ;
extern	WORD	FlagWater ;

// perso sauvegarde entrée cube

extern	WORD	SaveCube ;
extern	WORD	SaveComportement ;

extern	char	SaveStringBody[SIZE_NAME+1] ;
extern	BYTE	SaveGenBody ;

extern	WORD	SaveBeta ;
extern	WORD	SaveNbGoldPieces ;
extern	WORD	SaveMagicLevel ;
extern	WORD	SaveMagicPoint ;
extern	WORD	SaveLifePoint ;
extern	WORD	SaveChapitre ;
extern	WORD	SaveNbCloverBox ;
extern	WORD	SaveNbFourLeafClover ;

/*--------- Extra objs ----------------------------*/

extern	T_EXTRA	ListExtra[] ;

//extern	UBYTE	*PtrSpriteExtra ;
extern	T_HQR_HEADER	*HQRPtrSpriteExtra ;
extern	WORD	*PtrZvExtra ;

/*---------------- incrust display ----------------*/

extern	T_INCRUST_DISP	ListIncrustDisp[] ;

/*-------- Moteur 3D ---------------------------------------------*/

extern	UBYTE	*BufferShadow ;

extern	UBYTE	*BufferAnim ;
extern	UBYTE	*PtrBufferAnim ;

extern	LONG	NbBodys ;
extern	UBYTE	*PtrBody[] ;
extern	T_HQR_HEADER	*HQR_Anims ;

extern	LONG	NbObjets ;
extern	T_OBJET	ListObjet[] ;

extern	T_SORT	ListTri[] ;

extern	WORD	NumObjFollow ;

/*--------- ambiance ----------------------------*/

extern	WORD	SampleAmbiance[] ;
extern	WORD	SampleRepeat[] ;
extern	WORD	SampleRnd[] ;
extern	WORD	SamplePlayed ;
extern	ULONG	TimerNextAmbiance ;
extern	WORD	SecondEcart ;
extern	WORD	SecondMin ;
extern	WORD	CubeJingle ;
extern	UBYTE	*PtrXmi ;
extern	WORD	NumXmi ;

/*-------- Brick ISO ---------------------------------------------*/

#ifdef	BRICK_HQR
extern	UBYTE	*BufferBrick	;
#endif
extern	UBYTE	*BufCube ;
#ifndef	DOS
extern	UBYTE	*TabBlock ;
extern	LONG	XScreen, YScreen ;
#endif

extern	WORD	FirstTime ;
extern	WORD	CameraZone ;

extern	LONG	StartXCube ;
extern	LONG	StartYCube ;
extern	LONG	StartZCube ;

extern	LONG	WorldXCube ;
extern	LONG	WorldYCube ;
extern	LONG	WorldZCube ;

extern	WORD	XpOrgw ;
extern	WORD	YpOrgw ;

extern	WORD	LastXmBrickSelected ;
extern	WORD	LastYmBrickSelected ;
extern	WORD	LastZmBrickSelected ;

extern	UBYTE	*BufCollis ;

extern	WORD	CubeClip ;
extern	WORD	CubeClipX, CubeClipY, CubeClipZ ;

extern	WORD	FlagAffGrille ;
/*--------- Message ---------------------------------------------*/

extern	WORD	NumObjSpeak ;
extern	LONG	FlagSpeak ;



