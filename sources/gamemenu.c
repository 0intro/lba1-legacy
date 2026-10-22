#include 	"c_extern.h"
#ifndef	DOS
#include 	"balance.h"	// DOS calls Balance undeclared, as LBA.EXE was compiled
#endif

/*══════════════════════════════════════════════════════════════════════════*
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

	LONG	MixMusic = 1 ;		// Flag Mixer for Music
extern	LONG	MaxVolume ;		// Max Music Volume if no Mixer
#ifndef	DOS
extern	LONG	Language ;		// message.c's, the texts' language
extern	LONG	LanguageCD ;		// message.c's, the voices' language
extern	char	*TabLanguage[] ;	// message.c's, the languages' names
extern	LONG	FlagDisplayText ;	// message.c's, the texts shown under the voices
extern	LONG	FlagVoiceTest ;		// message.c's, the page's test voice loading and speaking
extern	LONG	FlagVoiceCD ;		// message.c's, the voices' file on the CD
void	CloseFdNar( void ) ;		// message.c's
#endif

#define	HAUTEUR_STANDARD	50
#define	MENU_SPACE		6

#define	COUL_TEXT_MENU		15	// trouver blanc palette jeu
#define	COUL_SELECT_MENU	68
#define	COUL_INPUT_NAME		112
#define	COUL_LETTER_INPUT	15

WORD	LargeurMenu = 550 ;

#ifdef	DOS
UWORD	GameMainMenu[] = {
#else
UWORD	GameMainMenu[4+2*6] = {	// FEATURE: room for LBA2's main menu (BuildGameMainMenu). Taken from LBA2.
#endif
			0,	// selected
			4,	// nb entries
			200,	// y center
			0,	// .dia num
			0, 20,	// start a new game game
			0, 21,	// continue game
			0, 23,	// options
			0, 22	// ret dos
				}	;

#ifndef	DOS
/*
 * FEATURE: LBA2's main menu, of which BuildGameMainMenu keeps the
 * entries that apply: Resume Game with a game in play or in current.lba,
 * New Game, Load Game while a save is listed, Save Game with a game in
 * play that may be saved, Options Menu, Quit, LBA2's own texts. Taken
 * from LBA2.
 */
UWORD	RealGameMainMenu[] = {
			0,	// selected
			6,	// nb entries
			335,	// y center
			0,	// .dia num
			0, 70|0x4000,	// reprendre partie
			0, 71|0x4000,	// nouvelle partie
			0, 72|0x4000,	// charger partie
			0, 73|0x4000,	// sauver partie
			0, 74|0x4000,	// Options
			0, 75|0x4000	// Quitter
				}	;
#endif

UWORD	GameQuitMenu[] = {
			0,	// selected
			2,	// nb entries
			240,	// y center
			0,	// .dia num
			0, 28,	// continuer jeu
			0, 27	// abandonner partie
				} ;

UWORD	GameOptionMenu[] = {
			0,	// selected
#ifdef	DOS
			4,	// nb entries
#else
			8,	// nb entries
#endif
			0,	// y from top.
			0,	// .dia num

			0, 24,  // annuler
			0, 30,	// reglages volumes
			0, 46,	// gestion saved game
#ifdef	DOS
			0, 47	// advanced options
#else
			0, 47,	// advanced options
			0, 0x1000,	// FEATURE: Choose language, before the switches. Taken from the Classic 3.2.3.
			0, 0x1008,	// FEATURE: Fullscreen display, before Hit walls. Taken from the Classic 3.2.3.
			0, 0x2001,	// FEATURE: Widescreen display, the wide screen's switch after the full screen's. The port's own.
			0, 0x100A	// FEATURE: Hit walls, the option menu's last entry. Taken from the Classic 3.2.3.
#endif
				} ;

UWORD	GameAdvancedMenu[] = {
			0,	// selected
			5,	// nb entries
			0,	// y from top.
			0,	// .dia num

			0, 26,	// retour
			0, 4,	// agressif auto/manuel
			6, 31,	// niveau details
			7, 32,	// ombres on/off
			8, 33	// zoom on/off
				} ;

#ifndef	DOS
/*
 * FEATURE: the language page, its entries of types 9 and 10 the texts'
 * language and the voices', which the arrows step. Taken from the
 * Classic 3.2.3, whose types 7 and 8 mark the advanced options' shadows
 * and zoom here.
 *
 * FEATURE: LBA2's Display text after the voices, counted in while they
 * speak, its text 17 while the texts show under the voices and 16 while
 * the voices speak alone. Taken from LBA2.
 */
#define	MENU_DISPLAY_TEXT	3

/* LanguageMenu is TLBA1C.exe 0x819780, with LBA2's Display text. */
UWORD	LanguageMenu[] = {
			0,	// selected
			3,	// nb entries
			0,	// y from top.
			0,	// .dia num

			0, 26,	// retour menu prec
			9, 0x1001,	// langue des textes
			10, 0x1002,	// langue des voix
			0, 17|0x4000	// affichage des textes
				} ;
#endif

UWORD	GameVolumeMenu[] = {
			0,	// selected
			7,	// nb entries
			0,	// y from top.
			0,	// .dia num

			0, 26,  // retour prec
			1, 10,	// type musvol / num text
			2, 11,	// type sampvol
			3, 12,	// type cdvol
			4, 13,	// type linevol
			5, 14,	// type mastervol
			0, 16	// save settings

				}	;

UWORD	SavedGameMenu[] = {
			0,	// selected
			3,	// nb entries
			0,	// y from top.
			0,	// .dia num

			0, 26,  // retour menu prec
			0, 41,	// copier une sauvegarde
			0, 45	// détruire une sauvegarde

				} ;

UWORD	SavedConfirmMenu[] = {
			0,	// selected
			2,	// nb entries
			240,	// y center.
			0,	// .dia num

			0, 24,  // annuler
			0, 48	// détruire une sauvegarde

				} ;

#ifndef	DOS
/*
 * FEATURE: LBA2's SavedConfirmMenu, with LBA2's own texts, under the
 * picture of the save to delete (DeleteSavedGame). Taken from LBA2.
 */
UWORD	SavedDeleteMenu[] = {
			0,	// selected
			2,	// nb entries
			350,	// y center.
			0,	// .dia num

			0, 24|0x4000,  // annuler
			0, 48|0x4000	// détruire une sauvegarde

				} ;
#endif

UWORD	GameChoiceMenu[] = {
			0,	// selected
			7,	// nb entries
			0,	// y from top. byte 8(
			0,	// .dia num

			0, 0,
			0, 1,
			0, 2,
			0, 3,
			0, 4,
			0, 5,
#ifdef	DOS
			0, 6	}	;
#else
			0, 6,
			0xFFFF	}	;	// FEATURE: the answer Escape picks, which GameAskChoice sets after the answers. Taken from the Classic 3.2.3.
#endif

ULONG	TimerSample ;

// save players games

#define	MAX_PLAYER	50

#ifndef	DOS
/*
 * FEATURE: LBA2's MAX_PLAYER, as many names and their 4-byte pointers as
 * a 640x480 buffer holds. Taken from LBA2.
 */
#define	MAX_PLAYER_LIST	((640*480)/(MAX_SIZE_PLAYER_NAME+1+4))

#define	SCREEN_SAVE_WIDTH	160	// l'image d'une sauvegarde
#define	SCREEN_SAVE_HEIGHT	120

#define	Y_START_CHOICE		205	// la première ligne de la liste
#define	TEMPO_REPEAT_KEY	8	// les 150 ms de LBA2, en 50èmes de seconde

WORD	ZoomSaveX = -1 ;	// d'où ZoomSavedGame agrandit la partie chargée, -1 sans zoom
WORD	ZoomSaveY = -1 ;

WORD	CursorActif = FALSE ;	// le curseur de saisie, affiché par DrawOneString
WORD	CursorX, CursorY ;
#endif

UBYTE	NumVersion = 0 ;

#define	NB_CAR_H	14
#define	NB_CAR_V	5

#define	MAX_SIZE_PLAYER_NAME	29
#ifdef	FIXES
#define	SIZE_PLAYER_ENTRY	(MAX_SIZE_PLAYER_NAME+1+_MAX_PATH)	// le nom, puis le fichier
#endif

UBYTE	GamePathname[_MAX_PATH] ;

//UBYTE	PlayerGameFile[_MAX_PATH] ;

UBYTE	PlayerName[MAX_SIZE_PLAYER_NAME+1] ;

/*──────────────────────────────────────────────────────────────────────────*/
// UBYTE	TabInvOrder[] = {

WORD	ListBetaGame[MAX_FLAGS_GAME] ;

#define	SIZE_INV_OBJ_X	75
#define	SIZE_INV_OBJ_Y	65


WORD	InvSelect = 0 ;
WORD	InvCoul = COUL_SELECT_MENU ;

#define	INV_START_X	17
#define	INV_START_Y	10

/*──────────────────────────────────────────────────────────────────────────*/

#define	SIZE_FOUND_OBJ	130

/*──────────────────────────────────────────────────────────────────────────*/

T_REAL_VALUE	RealRot ;
UBYTE		*Ptr3do ;
WORD		TabAnim[4] ;
WORD		FrameAnim[4] ;

extern	WORD	ZoneGrm ;
extern	WORD	IndexGrm ;

#define	CTRL_X0	100
#define	CTRL_Y0	100
#define	CTRL_X1	550
#define	CTRL_Y1	290

/*══════════════════════════════════════════════════════════════════════════*
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

void	Init3DGame()
{
	/*
	 * FEATURE: the projection's centre about the middle of a screen of
	 * any size, as the bricks are. The port's own.
	 */
	SetIsoProjection( CENTRE_ECRAN_X-8-1, CENTRE_ECRAN_Y, SIZE_BRICK_XZ ) ;
	SetPosCamera( 0,0,0 ) ;
	SetAngleCamera( 0,0,0 ) ;
	SetLightVector( AlphaLight, BetaLight, 0 ) ;
}

extern	UBYTE	*Imagen ;

/*──────────────────────────────────────────────────────────────────────────*/

void	DrawFire( WORD x0, WORD y0, WORD x1, WORD y1, WORD coul )
{
	DoFire( y0, coul ) ;

	if( !Rnd(5) )
	{
		BufSpeak[ Rnd(320*10)+20*320 ] = 255 ;
	}
}

void	WaitReleaseSpace()
{
	SaveTimer() ;
	/*
	 * FIXED: LBA.EXE waits for Space and Escape alone, so that a
	 * dialogue or a question closed by another key goes back to the
	 * game with it down, which the next frame takes for its own: Shift
	 * for the inventory, Return for the camera, H for the holomap. This
	 * waits for every key's release but the arrows', as the quit menu's
	 * Continue does.
	 */
#ifdef	FIXES
	while( Key OR Fire
#ifndef	DOS
	/*
	 * FEATURE: the action key. W, held with LbaWinSettings's 4 set
	 * (InputActions' 0x200, the Classic's Talk/Search), is waited for as
	 * Space is, so that W held acts once. Taken from the Classic 3.2.3.
	 */
	OR (InputActions & 0x200)
#endif
	) idle() ;
#else
	while( (Fire & F_SPACE)
	OR (Key == K_ESC) ) ;
#endif
	RestoreTimer() ;
}

/*══════════════════════════════════════════════════════════════════════════*
	     █▀▀▀▀ █▀▀▀█ █▄ ▄█ █▀▀▀▀       █▄ ▄█ █▀▀▀▀ ██▄ █ █   █
	     ██ ▀█ ██▀▀█ ██▀ █ ██▀▀        ██▀ █ ██▀▀  ██▀██ ██  █
	     ▀▀▀▀▀ ▀▀  ▀ ▀▀  ▀ ▀▀▀▀▀ ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀▀
 *══════════════════════════════════════════════════════════════════════════*/

/*══════════════════════════════════════════════════════════════════════════*/

void	DrawObj3D( WORD x0, WORD y0, WORD x1, WORD y1,
	WORD y3d , WORD betaflag ,
	UBYTE *ptr3do )
{
	WORD	beta ;

	SetIsoProjection( (x1+x0)/2, (y1+y0)/2, 0 ) ;

	SetClip( x0,y0, x1,y1 ) ;

	if( betaflag == -1 )
	{
		beta = GetRealAngle( &RealRot ) ;

		if( RealRot.TimeValue == 0 )
		{
			InitRealAngle( beta, beta-256, 50, &RealRot ) ;
		}
	}
	else
	{
		beta = betaflag ;
	}

	AffObjetIso( 0,y3d,0, 0,beta,0, ptr3do ) ;
}

/*══════════════════════════════════════════════════════════════════════════*/

void	DrawCadre( WORD x0, WORD y0, WORD x1, WORD y1 )
{
	Line( x0  , y0  , x1, y0, 79 ) ;
	Line( x0  , y0  , x0, y1, 79 ) ;
	Line( x1  , y0+1, x1, y1, 73 ) ;
	Line( x0+1, y1  , x1, y1, 73 ) ;
}

/*══════════════════════════════════════════════════════════════════════════*
    █▀▀▀▀ █▀▀▀█ █▄ ▄█ █▀▀▀█ █▀▀▀█ █▀▀▀█ ▀▀█▀▀ █▀▀▀▀ █▄ ▄█ █▀▀▀▀ ██▄ █ ▀▀█▀▀
    ██    ██  █ ██▀ █ ██▀▀▀ ██  █ ██▀█▀   ██  ██▀▀  ██▀ █ ██▀▀  ██▀██   ██
    ▀▀▀▀▀ ▀▀▀▀▀ ▀▀  ▀ ▀▀    ▀▀▀▀▀ ▀▀  ▀   ▀▀  ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀▀ ▀▀  ▀   ▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

void	DrawComportement( WORD comportement, WORD beta, WORD copyblock )
{
	WORD	x0, y0, x1, y1 ;
	UBYTE	*ptranim ;
	WORD	frameanim ;


	x0 = CTRL_X0+10 + comportement * 110 ;
	y0 = CTRL_Y0+10 ;
	x1 = x0+99 ;
	y1 = y0+119 ;

	ptranim = HQR_Get( HQR_Anims, TabAnim[comportement] ) ;
	frameanim = FrameAnim[comportement] ;

	if( SetInterAnimObjet( frameanim, ptranim, Ptr3do ) )
	{
		frameanim++ ;
		if( frameanim == GetNbFramesAnim( ptranim ) )
		{
			frameanim = GetBouclageAnim( ptranim ) ;
		}
		FrameAnim[comportement] = frameanim ;
	}

	if( !copyblock )	/* init */
	{
		DrawCadre( x0-1, y0-1, x1+1, y1+1 ) ;
	}

	MemoClip() ;
	UnSetClip() ;

	if( comportement != Comportement )
	{

		Box( x0,y0, x1,y1, 0 ) ;

/*		CopyBlock( x0,y0, x1,y1, Screen, x0, y0, Log ) ;
		ShadeBox( x0,y0, x1,y1, 3 ) ;
*/
	}
	else
	{

		Box( x0,y0, x1,y1, 68 ) ;

/*		CopyBlock( CTRL_X0+10, CTRL_Y0+139, CTRL_X1-10, CTRL_Y0+139+40, Screen,
			   CTRL_X0+10, CTRL_Y0+139, Log ) ;
		ShadeBox( CTRL_X0+10, CTRL_Y0+139, CTRL_X1-10, CTRL_Y0+139+40, 4 ) ;
*/
		Box( CTRL_X0+10, CTRL_Y0+139, CTRL_X1-10, CTRL_Y0+139+40, 0 ) ;
		DrawCadre( CTRL_X0+10, CTRL_Y0+139, CTRL_X1-10, CTRL_Y0+139+40 ) ;

		CoulFont( 15 ) ;
		if( Comportement == C_AGRESSIF
		AND CombatAuto == TRUE )
			GetMultiText( 4, String ) ;
		else
			GetMultiText( Comportement, String ) ;

		Font( (CTRL_X1+CTRL_X0-SizeFont( String ))/2, CTRL_Y0+140, String )		;
	}

	DrawObj3D(	x0, y0, x1, y1,
			-600, beta,
			Ptr3do ) ;

/*	if( comportement != Comportement )
	{
//		ShadeBox( x0,y0, x1,y1, 5 ) ;
	}
	else
	{
//		Rect( x0+1,y0+1, x1-1,y1-1, 12 ) ;
		Rect( x0+2,y0+2, x1-2,y1-2, 12 ) ;
	}
*/

	if( copyblock )
	{
		CopyBlockPhys( x0,y0,x1,y1 ) ;
		CopyBlockPhys( CTRL_X0+10, CTRL_Y0+139, CTRL_X1-10, CTRL_Y0+139+40 ) ;
	}

	RestoreClip() ;
}

/*══════════════════════════════════════════════════════════════════════════*/
/*══════════════════════════════════════════════════════════════════════════*/

void	DrawInfoMenu( WORD x0, WORD y0 )
{
	WORD	x,x1 ;
	WORD	l ;
	WORD	lifepoint ;

	UnSetClip() ;

	DrawCadre( x0,y0, x0+450, y0+80 ) ;
	/*Shade*/Box( x0+1,y0+1, x0+450-1, y0+80-1, 0/*3*/ ) ;

	// Life Points	(max 50)

//	AffGraph( 4, x0+9, y0+13, PtrSpriteExtra ) ;
	AffGraph( 0, x0+9, y0+13, HQR_Get(HQRPtrSpriteExtra, 4) ) ;

	lifepoint = ListObjet[NUM_PERSO].LifePoint ;
	x = RegleTrois32( x0+25, x0+325, 50, lifepoint ) ;

	Box( x0+25, y0+10, x, y0+10+15, 91 ) ;
	Box( x, y0+10, x0+325, y0+10+15, 0 ) ;

	DrawCadre( x0+25, y0+10, x0+325, y0+10+15 ) ;

	// Magic level	(max 4)	MagicPoint (max 80)

	if( !ListFlagGame[FLAG_CONSIGNE]
	AND  ListFlagGame[FLAG_TUNIQUE] )
	{

//	AffGraph( 5, x0+9, y0+36, PtrSpriteExtra ) ;
	AffGraph( 0, x0+9, y0+36, HQR_Get(HQRPtrSpriteExtra, 5) ) ;

	if( MagicLevel )
	{
		x = RegleTrois32( x0+25, x0+325, 80, MagicPoint ) ;
		Box( x0+25, y0+35, x, y0+35+15, 75 ) ;

		for( l=0; l<MagicLevel; l++ )
		{
			x1 = RegleTrois32( x0+25, x0+325, 40, MagicLevel*10 ) ;
			Line( x1, y0+35+1, x1, y0+35+15-1, 0 ) ;
		}

		x1 = RegleTrois32( x0+25, x0+325, 40, MagicLevel*10 ) ;
		DrawCadre( x0+25, y0+35, x1, y0+35+15 ) ;
	}

	}

	// Nb Gold Pieces

//	AffGraph( 3, x0+340, y0+15, PtrSpriteExtra ) ;
	AffGraph( 0, x0+340, y0+15, HQR_Get(HQRPtrSpriteExtra, 3) ) ;
	CoulFont( 155 ) ;
	Font( x0+370, y0+5, Itoa(NbGoldPieces) ) ;

	// Nb Little Keys

//	AffGraph( 6, x0+340, y0+55, PtrSpriteExtra ) ;
	AffGraph( 0, x0+340, y0+55, HQR_Get(HQRPtrSpriteExtra, 6) ) ;
	CoulFont( 155 ) ;
	Font( x0+370, y0+40, Itoa(NbLittleKeys) ) ;

	// Nb CloverBox

	for( l=0; l<NbCloverBox; l++ )
	{
		x1 = RegleTrois32( x0+25, x0+325, MAX_CLOVER_BOX, l ) ;
//		AffGraph( 41, x1, y0+58, PtrSpriteExtra ) ;
		AffGraph( 0, x1, y0+58, HQR_Get(HQRPtrSpriteExtra, 41) ) ;
	}

	// Nb Four-leaf Clover

	for( l=0; l<NbFourLeafClover; l++ )
	{
		x1 = RegleTrois32( x0+25, x0+325, MAX_CLOVER_BOX, l ) ;
//		AffGraph( 7, x1+2, y0+60, PtrSpriteExtra ) ;
		AffGraph( 0, x1+2, y0+60, HQR_Get(HQRPtrSpriteExtra, 7) ) ;
	}

	CopyBlockPhys( x0,y0, x0+450, y0+80 ) ;
}

/*══════════════════════════════════════════════════════════════════════════*/

void	DrawMenuComportement( WORD beta )
{
	DrawCadre( CTRL_X0  , CTRL_Y0  , CTRL_X1, CTRL_Y1 ) ;

	ShadeBox( CTRL_X0+1,CTRL_Y0+1, CTRL_X1-1, CTRL_Y1-1, 2 ) ;

	SetAnimObjet(	FrameAnim[C_NORMAL],
			HQR_Get( HQR_Anims, TabAnim[C_NORMAL] ),
			Ptr3do ) ;
	DrawComportement( C_NORMAL, beta, FALSE ) ;

	SetAnimObjet(	FrameAnim[C_SPORTIF],
			HQR_Get( HQR_Anims, TabAnim[C_SPORTIF] ),
			Ptr3do ) ;
	DrawComportement( C_SPORTIF, beta, FALSE ) ;

	SetAnimObjet(	FrameAnim[C_AGRESSIF],
			HQR_Get( HQR_Anims, TabAnim[C_AGRESSIF] ),
			Ptr3do ) ;
	DrawComportement( C_AGRESSIF,beta, FALSE ) ;

	SetAnimObjet(	FrameAnim[C_DISCRET],
			HQR_Get( HQR_Anims, TabAnim[C_DISCRET] ),
			Ptr3do ) ;
	DrawComportement( C_DISCRET, beta, FALSE ) ;

	DrawInfoMenu( CTRL_X0, CTRL_Y1+10 ) ;

	CopyBlockPhys( CTRL_X0,CTRL_Y0, CTRL_X1, CTRL_Y1 ) ;
}

/*══════════════════════════════════════════════════════════════════════════*/

#ifdef	FIXES
void	MenuComportement( WORD arrows )
#else
void	MenuComportement()
#endif
{
	T_OBJET	*ptrobj ;
	UBYTE	*memoptrfile3d ;
	WORD	oldcomp ;
	WORD	memoisland ;
	LONG	memoflagspeak ;

	SaveTimer() ;

	if( Comportement == C_PROTOPACK )
	{
#ifdef	FIXES
		SetComportement( C_NORMAL ) ;	// le bourdonnement arrêté
		HQ_SaveSamples() ;		// et les boucles mises de côté
#else
		HQ_StopSample() ;
		SetComportement( C_NORMAL ) ;
#endif
	}

	ptrobj = &ListObjet[NUM_PERSO] ;

	memoptrfile3d = ptrobj->PtrFile3D ;

	Ptr3do = PtrBody[ptrobj->Body] ;

	TabAnim[0] = AnimRienNormal ;
	TabAnim[1] = AnimRienSportif ;
	TabAnim[2] = AnimRienAgressif ;
	TabAnim[3] = AnimRienDiscret ;

	InitRealAngle( ptrobj->Beta, ptrobj->Beta-256, 50, &RealRot ) ;

	CopyScreen( Log, Screen ) ;

	memoisland = Island ;
	Island = 255 ;

	memoflagspeak = FlagSpeak ;
	FlagSpeak = FALSE ;
	InitDial( 0 ) ;

	DrawMenuComportement( ptrobj->Beta ) ;

	oldcomp = Comportement ;
	SetAnimObjet(	FrameAnim[Comportement],
			HQR_Get( HQR_Anims, TabAnim[Comportement] ),
			Ptr3do ) ;

	while( (Fire & F_CTRL) OR ((Key>=K_F1) AND (Key<=K_F4)) )
	{
#ifdef	DEBUG_TOOLS
		CheckSavePcx() ;
#endif
		MyJoy = Joy ;

/*		if( Comportement == C_AGRESSIF )
		{
			if( MyJoy & J_UP )	CombatAuto = TRUE ;
			if( MyJoy & J_DOWN )	CombatAuto = FALSE ;
		}
*/
		/*
		 * FIXED: LBA.EXE reads the arrows in the menu that F1 to F4
		 * open too, so that one held there, as Twinsen walks, turns
		 * the key's behaviour into another. They are read for Ctrl's
		 * menu alone. Taken from LbaWin 0.8.1.
		 */
#ifdef	FIXES
		if( arrows )
		{
			if( MyJoy & J_RIGHT ) 	Comportement++ ;
			if( MyJoy & J_LEFT )	Comportement-- ;
			Comportement &= 3 ;
		}
#else
		if( MyJoy & J_RIGHT ) 	Comportement++ ;
		if( MyJoy & J_LEFT )	Comportement-- ;
		Comportement &= 3 ;
#endif

		if( Comportement != oldcomp )
		{
			DrawComportement( oldcomp, ptrobj->Beta, TRUE ) ;

			oldcomp = Comportement ;
			InitRealAngle(	ptrobj->Beta,
					ptrobj->Beta-256,
					50, &RealRot ) ;

			SetAnimObjet(	FrameAnim[Comportement],
					HQR_Get( HQR_Anims, TabAnim[Comportement] ),
					Ptr3do ) ;

			/*
			 * FIXED: LBA.EXE waits for every arrow's release after a
			 * change, so that Up held, as Twinsen walks, keeps the
			 * menu up and the game stopped. It waits for Left and
			 * Right alone. Taken from LbaWin 0.8.0.
			 */
#ifdef	FIXES
			while( Joy & (J_LEFT|J_RIGHT) )
#else
			while( Joy )
#endif
				DrawComportement( Comportement, -1, TRUE ) ;
		}
		DrawComportement( Comportement, -1, TRUE ) ;
	}
	SetComportement( Comportement ) ;

	Init3DGame() ;

	Island = memoisland ;

	FlagSpeak = memoflagspeak ;
	InitDial( START_FILE_ISLAND+Island ) ;

#ifdef	FIXES
	HQ_RestoreSamples() ;	// les boucles reprises
#endif

	RestoreTimer() ;
}

/*══════════════════════════════════════════════════════════════════════════*
	      █    ██▄ █ █   █ █▀▀▀▀ ██▄ █ ▀▀█▀▀ █▀▀▀█ █▀▀▀█ █  ▄▀
	      ██   ██▀██ ██ ▄▀ ██▀▀  ██▀██   ██  ██  █ ██▀█▀ ██▀
	      ▀▀   ▀▀  ▀ ▀▀▀   ▀▀▀▀▀ ▀▀  ▀   ▀▀  ▀▀▀▀▀ ▀▀  ▀ ▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/
/*──────────────────────────────────────────────────────────────────────────*/

void	Draw3dObject( WORD x, WORD y, WORD *ptrobj, WORD beta, WORD zoom )
{
	SetProjection( x,y,128,200,200 ) ;

	SetFollowCamera( 0,0,0, 60,0,0, zoom ) ;

	if( !ptrobj )
	{
		Message( "ptrobj = 0", TRUE ) ;
	}
	else
	{
		AffObjetIso(	0,0,0, 0,beta,0, ptrobj ) ;
	}
}

/*──────────────────────────────────────────────────────────────────────────*/

void	DrawOneInventory( WORD flagnum )
{
	WORD	x, y, x0, y0, x1, y1 ;
	WORD	*ptrobj ;

	x =	INV_START_X + 10 + SIZE_INV_OBJ_X/2
		+ (SIZE_INV_OBJ_X+10) * (flagnum/4) ;
	y = 	INV_START_Y + 10 + SIZE_INV_OBJ_Y/2
		+ (SIZE_INV_OBJ_Y+10) * (flagnum&3) ;

	x0 = x - SIZE_INV_OBJ_X/2 ;
	y0 = y - SIZE_INV_OBJ_Y/2 ;
	x1 = x + SIZE_INV_OBJ_X/2 ;
	y1 = y + SIZE_INV_OBJ_Y/2 ;

	if( flagnum == InvSelect )
	{
		Box( x0, y0, x1, y1, InvCoul ) ;
	}
	else
	{
		Box( x0, y0, x1, y1, 0 ) ;
//		CopyBlock( x0, y0, x1, y1, Screen, x0, y0, Log ) ;
//		ShadeBox( x0, y0, x1, y1, 4 ) ;
	}

	if( (ListFlagGame[flagnum])
	AND ((ListFlagGame[FLAG_CONSIGNE]==0) OR (flagnum==FLAG_CLOVER)) )
	{
		ptrobj = HQR_Get( InventoryObj, flagnum ) ;
		if( HQR_Flag )
		{
			PatchObjet( ptrobj ) ;
		}

		Draw3dObject(	x, y, ptrobj,
				ListBetaGame[flagnum]+=8, 15000 ) ;

		if( flagnum == 15 )	// carburant
		{
			CoulFont( 15 ) ;
			Font( x0+3,y0+32, Itoa( Fuel ) ) ;
		}
	}

	DrawCadre( x0, y0, x1, y1 ) ;

	CopyBlockPhys( x0,y0, x1,y1 ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/


void	DrawListInventory()
{
	WORD	x, y ;
	WORD	n = 0 ;
	WORD	x0, y0, x1, y1 ;

	x0 = INV_START_X ;
	y0 = INV_START_Y ;

	x1 = x0 + (SIZE_INV_OBJ_X+10)*7 + 10 ;
	y1 = y0 + (SIZE_INV_OBJ_Y+10)*4 + 10 ;

	ShadeBox( x0,y0, x1,y1, 4 ) ;
	DrawCadre( x0,y0, x1,y1 ) ;

/*	DrawCadre(	INV_START_X+ 10 - 2 + (SIZE_INV_OBJ_X+10) * 0,
			INV_START_Y+ 10 - 2 + (SIZE_INV_OBJ_Y+10) * 1,

			INV_START_X+ 1 + (SIZE_INV_OBJ_X+10) * 1,
			INV_START_Y+ 1 + (SIZE_INV_OBJ_Y+10) * 3  ) ;
*/
	Rect(	INV_START_X+ 10 - 2 + (SIZE_INV_OBJ_X+10) * 1,
		INV_START_Y+ 10 - 2 + (SIZE_INV_OBJ_Y+10) * 0,
		INV_START_X+ 1 + (SIZE_INV_OBJ_X+10) * 2,
		INV_START_Y+ 1 + (SIZE_INV_OBJ_Y+10) * 4,
		75  ) ;

	CopyBlockPhys( x0,y0, x1,y1 ) ;

	for( n=0; n<MAX_INVENTORY; n++ )
	{
		DrawOneInventory( n ) ;
	}
}

/*──────────────────────────────────────────────────────────────────────────*/

void	Inventory()
{
	WORD	flag = 1 ;
	WORD	dialstat = 0 ;
	WORD	oldselect ;
	WORD	memoalphalight, memobetalight ;
	LONG	memoflagspeak ;

	CopyScreen( Log, Screen ) ;

	memoalphalight = AlphaLight ;
	memobetalight = BetaLight ;
	SetLightVector( 896, 950, 0 ) ;

	// magic NRJ
	if( NbFourLeafClover > 0 )	ListFlagGame[FLAG_CLOVER] = TRUE ;
	else				ListFlagGame[FLAG_CLOVER] = FALSE ;

	InvCoul = COUL_SELECT_MENU ;
	DrawListInventory() ;	// draw all

	memoflagspeak = FlagSpeak ;
	FlagSpeak = FALSE ;
	InitDial( 2 ) ;	// game divers txt

	dialstat = 3 ;
	TestCoulDial( 4 ) ;
	InitDialWindow() ;

	while( Key != K_ESC )
	{
#ifdef	DEBUG_TOOLS
		CheckSavePcx() ;
#endif
		oldselect = InvSelect ;

		if( !flag )
		{
			MyJoy = Joy ;
			MyFire = Fire ;
			MyKey = Key ;
			if( MyJoy OR Fire )	flag = 1 ;
		}
		else
		{
			MyFire = MyJoy = MyKey = 0 ;
			if( !Joy AND !Fire )	flag = 0 ;
		}

		if( (MyKey == K_ESC) OR (MyFire&F_SHIFT) )
		{
			break ;
		}

		if( MyJoy & J_DOWN )
		{
			InvSelect++ ;
			if( InvSelect >= MAX_INVENTORY )
				InvSelect = 0 ;
			DrawOneInventory( oldselect ) ;
			dialstat = 3 ;
		}

		if( MyJoy & J_UP )
		{
			InvSelect-- ;
			if( InvSelect < 0 )
				InvSelect = MAX_INVENTORY - 1 ;
			DrawOneInventory( oldselect ) ;
			dialstat = 3 ;
		}

		if( MyJoy & J_LEFT )
		{
			InvSelect -= 4 ;
			if( InvSelect < 0 )
				InvSelect = MAX_INVENTORY + InvSelect ;
			DrawOneInventory( oldselect ) ;
			dialstat = 3 ;
		}

		if( MyJoy & J_RIGHT )
		{
			InvSelect += 4 ;
			if( InvSelect >= MAX_INVENTORY )
				InvSelect = InvSelect - MAX_INVENTORY ;
			DrawOneInventory( oldselect ) ;
			dialstat = 3 ;
		}


		if( dialstat == 3 )
		{
			SecondInitDialWindow() ;

			if( (ListFlagGame[InvSelect]==1)
			AND ((ListFlagGame[FLAG_CONSIGNE]==0) OR (InvSelect==FLAG_CLOVER)) )
			{
				OpenDialNoWindow( InvSelect+100 ) ;
				dialstat = 0 ;
			}
			else
			{
				OpenDialNoWindow( 128 ) ;
				dialstat = 0 ;
			}
		}

		if( dialstat != 2 )
		{
			dialstat = NextDialCar() ;
		}
		if( dialstat != 1 )		// en cours lecture
		{
			Vsync() ;
		}

		if( MyFire & F_SPACE )
		{
			if( dialstat == 2 )
			{
				SecondInitDialWindow() ;
				dialstat = 0 ;
			}
			else
			{
				if( (ListFlagGame[InvSelect]==1)
				AND ((ListFlagGame[FLAG_CONSIGNE]==0) OR (InvSelect==FLAG_CLOVER)) )
				{
					SecondInitDialWindow() ;
					OpenDialNoWindow( InvSelect+100 ) ;
				}
			}
		}

		DrawOneInventory( InvSelect ) ;

		if( MyFire & F_RETURN )
		{
			if( (ListFlagGame[InvSelect]==1)
			AND ((ListFlagGame[FLAG_CONSIGNE]==0) OR (InvSelect==FLAG_CLOVER)) )
			{
				InventoryAction = InvSelect ;

				InvCoul = 91 ; // rouge
				DrawOneInventory( InvSelect ) ;

				break ;
			}
		}
	}
	CloseDial() ;

	AlphaLight = memoalphalight ;
	BetaLight  = memobetalight ;
	Init3DGame() ;

	FlagSpeak = memoflagspeak ;
	InitDial( START_FILE_ISLAND+Island ) ;

	while( Key OR Fire ) idle() ;
}
/*══════════════════════════════════════════════════════════════════════════*
    █▀▀▀▀ █▀▀▀█ █   █ ██▄ █ █▀▀▀▄       █▀▀▀█ █▀▀█      █ █▀▀▀▀ █▀▀▀▀ ▀▀█▀▀
    ██▀▀  ██  █ ██  █ ██▀██ ██  █       ██  █ ██▀▀█ ▄▄  █ ██▀▀  ██      ██
    ▀▀    ▀▀▀▀▀ ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀  ▀▀▀▀▀ ▀▀▀▀▀ ▀▀▀▀▀ ▀▀▀▀▀ ▀▀▀▀▀ ▀▀▀▀▀   ▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

void	DoFoundObj( WORD numobj )
{
	char	string[256] ;
	WORD	*ptr3do ;
	T_OBJET	*ptrobj ;
	WORD	xm, ym, zm, xw, yw, zw ;
	WORD	x, y, x0, y0, x1, y1 ;
	ULONG	timerend ;
	WORD	dialstat ;
	WORD	flag ;
	UBYTE	*ptranim ;
	WORD	numanim ;
	WORD	frameanim ;

	LONG	memoflagspeak ;
	LONG	speakfromcd  ;
	LONG	memotimersystem ;

	ptrobj = &ListObjet[NUM_PERSO] ;

	// reaff scene recentree sans twinkel

	StartXCube = ((ListObjet[NUM_PERSO].PosObjX+DEMI_BRICK_XZ)/SIZE_BRICK_XZ) ;
	StartYCube = ((ListObjet[NUM_PERSO].PosObjY+SIZE_BRICK_Y)/SIZE_BRICK_Y) ;
	StartZCube = ((ListObjet[NUM_PERSO].PosObjZ+DEMI_BRICK_XZ)/SIZE_BRICK_XZ) ;

	ptrobj->Flags |= INVISIBLE ;
	AffScene( TRUE ) ;
	ptrobj->Flags &= ~INVISIBLE ;

	CopyScreen( Log, Screen ) ;

	// reaff twinksen anim trouve

	/*
	 * FEATURE: Twinsen, the object and their boxes on the whole screen,
	 * in its coordinates, the dialogue in the window of 640x480 in its
	 * middle. The port's own.
	 */
	pushscreen() ;

	xw = StartXCube*SIZE_BRICK_XZ ;
	yw = StartYCube*SIZE_BRICK_Y ;
	zw = StartZCube*SIZE_BRICK_XZ ;

	AffObjetIso(	ptrobj->PosObjX-xw, ptrobj->PosObjY-yw, ptrobj->PosObjZ-zw,
			0, 128/*ptrobj->Beta*/, 0,
			PtrBody[ptrobj->Body] ) ;

	SetClip( ScreenXmin, ScreenYmin, ScreenXmax, ScreenYmax ) ;

	xm = ((ptrobj->PosObjX+DEMI_BRICK_XZ)/SIZE_BRICK_XZ) ;
	ym = ((ptrobj->PosObjY)/SIZE_BRICK_Y) ;
	if( (ptrobj->Col&127) != 0 )	ym++ ;
	zm = ((ptrobj->PosObjZ+DEMI_BRICK_XZ)/SIZE_BRICK_XZ) ;

	DrawOverBrick( xm, ym, zm ) ;

	Flip() ;

	ProjettePoint(	ptrobj->PosObjX-xw,
			ptrobj->PosObjY-yw,
			ptrobj->PosObjZ-zw ) ;

	Yp -= 150 ;

	x0 = Xp - SIZE_FOUND_OBJ/2 ;
	y0 = Yp - SIZE_FOUND_OBJ/2 ;
	x1 = Xp + SIZE_FOUND_OBJ/2 ;
	y1 = Yp + SIZE_FOUND_OBJ/2 ;

	HQ_MixSample(	41, 0x1000, 1, 128, 128 ) ;

	// aff text et objet

#ifdef	CDROM

	if( IsVoiceFileOnHD( 2 ) )
	{
		// voix sur hd touche à rien

		InitDial( 2 ) ;	// game divers txt
		speakfromcd = FALSE ;

	}
	else	// coupe music cd si on
	{
		StopMusicCD() ;
		memoflagspeak = FlagSpeak ;
		FlagSpeak = FALSE ;

		InitDial( 2 ) ;

		FlagSpeak = memoflagspeak ;
		speakfromcd = TRUE ;
	}

#else

	InitDial( 2 ) ;	// game divers txt

#endif

	poparea() ;
	UnSetClip() ;
	OpenDial( numobj ) ;
//	timerend = TimerRef + 50 * 30 ;
	dialstat = 1 ;
	flag = FALSE ;
	TestCoulDial( 4 ) ;	// coul twinsen
#ifdef	CDROM

	if( speakfromcd )
	{
		SpeakFromCD( 2, numobj ) ;
	}
	else
	{
		Speak( numobj ) ;
	}
#endif

	numanim = SearchAnim( GEN_ANIM_TROUVE, NUM_PERSO ) ;
	ptranim = HQR_Get( HQR_Anims, numanim ) ;
#ifdef	DEBUG_TOOLS
	if( numanim == -1 )
	{
		Message( "num anim 'trouve' = -1 in DoFoundObj", TRUE ) ;
		goto argexit ;
	}
	if( !ptranim )
	{
		Message( "ptranim = 0 in DoFoundObj", TRUE ) ;
		goto argexit ;
	}
#endif

	frameanim = 0 ;

	PtrBufferAnim += StockInterAnim( PtrBufferAnim, PtrBody[ptrobj->Body] ) ;
	if( PtrBufferAnim > BufferAnim + 5000 - 512 )	PtrBufferAnim = BufferAnim ;

	ptr3do = HQR_Get( InventoryObj, numobj ) ;
	if( HQR_Flag )
	{
		PatchObjet( ptr3do ) ;
	}

	NbOptPhysBox = 0 ;		// clear list cls boxes

	pushscreen() ;
	while( flag == FALSE )
	{
		UnSetClip() ;

		NbPhysBox = 0 ;
		ClsBoxes() ;

		ShadeBox( x0, y0, x1, y1, 4 ) ;
		SetClip(  x0, y0, x1, y1 ) ;

		Draw3dObject(	Xp, Yp, ptr3do,
				ListBetaGame[numobj]+=8, 10000 ) ;

		DrawCadre( x0, y0, x1, y1 ) ;
		AddPhysBox( x0,y0, x1,y1 ) ;

		UnSetClip() ;
		Init3DGame() ;

		// reaff twinkel anim trouve

		if( SetInterAnimObjet( frameanim, ptranim, PtrBody[ptrobj->Body] ) )
		{
			frameanim++ ;
			if( frameanim == GetNbFramesAnim( ptranim ) )
			{
				frameanim = GetBouclageAnim( ptranim ) ;
			}
		}

		AffObjetIso(	ptrobj->PosObjX-xw, ptrobj->PosObjY-yw, ptrobj->PosObjZ-zw,
				0, 128/*ptrobj->Beta*/, 0,
				PtrBody[ptrobj->Body] ) ;

		SetClip( ScreenXmin, ScreenYmin, ScreenXmax, ScreenYmax ) ;
		DrawOverBrick( xm, ym, zm ) ;
		AddPhysBox( ScreenXmin, ScreenYmin, ScreenXmax, ScreenYmax ) ;

		// message

		if( dialstat == 1 )
		{
			pushwindow() ;
			UnSetClip() ;
			dialstat = NextDialCar() ;
			poparea() ;
		}
		if( dialstat != 1 )		// pas en cours lecture
		{
			Vsync() ;
		}

		FlipBoxes() ;

		if( Fire )
		{
			if( dialstat == 0 )
			{
				flag = TRUE ;
			}
			if( dialstat == 2 )
			{
				dialstat = 1 ;	// continue
			}
		}
	}
	poparea() ;

#ifdef	DEBUG_TOOLS
argexit:
#endif

#ifdef	CDROM

	if( speakfromcd )
	{
		while( TestSpeakFromCD() AND (Key != K_ESC) ) idle() ;
	}
	else
	{
		while( TestSpeak() AND (Key != K_ESC) ) idle() ;
	}
	StopSpeak() ;
#endif
	Init3DGame() ;

	InitDial( START_FILE_ISLAND+Island ) ;

#ifdef	FIXES
	while( Key == K_ESC ) idle() ;

	/*
	 * FIXED: LBA.EXE goes back to the game with the key that closed
	 * the found object's box still down, Escape alone waited for, so
	 * that the next frame takes Space for Twinsen's action or a jump
	 * and Shift for the inventory. This waits for every key's release
	 * but the arrows', as the quit menu's Continue does.
	 */
	while( Key OR Fire ) idle() ;
#else
	while( Key == K_ESC ) ;
#endif
}

/*══════════════════════════════════════════════════════════════════════════*/
/*══════════════════════════════════════════════════════════════════════════*/

void	ExtInitMcga()
{
	int	i ;
	char	num[10] ;

	if (Wave_Driver_Enable)
		while( !WavePause() ) ;

	InitMcgaMode() ;
	if( FlagPalettePcx )	Palette( PalettePcx ) ;
	else			Palette( PtrPal ) ;
	WaveContinue() ;
	FlagMCGA = TRUE ;
}

void	ExtInitSvga()
{
	if (Wave_Driver_Enable)
		while( !WavePause() ) ;

	SimpleInitSvga() ;
	if( FlagPalettePcx )	Palette( PalettePcx ) ;
	else			Palette( PtrPal ) ;
	FlagMCGA = FALSE ;
	WaveContinue() ;
}

void	TestRestoreModeSVGA( LONG flag )
{
	if( FlagMCGA )
	{
		ExtInitSvga() ;
		if( flag )	AffScene(TRUE)	;
	}
}

/*══════════════════════════════════════════════════════════════════════════*/

void	CopyBlockPhysMCGA( LONG x0, LONG y0, LONG x1, LONG y1 )
{
	LONG	x, y ;

/*	x = (x0+x1)/2 - 160 ;
	y = (y0+y1)/2 - 100 ;
*/
	x = x0 - 160 ;
	y = y0 - 140 ;

	if( x > LARGEUR_ECRAN-320 )	x = LARGEUR_ECRAN-320 ;
	if( x < 0 )		x = 0 ;
	if( y > HAUTEUR_ECRAN-200 )	y = HAUTEUR_ECRAN-200 ;
	if( y < 0 )		y = 0 ;

#ifdef	DOS
	CopyBlockMCGA( x,y, x+319, y+199, Log, 0,0,0xA0000 ) ;
#else
	CopyBlockMCGA( x,y, x+319, y+199, Log, 0,0,Phys ) ;
#endif
}

/*══════════════════════════════════════════════════════════════════════════*
	     ██▀▀▀ █▀▀▀█ █   █ █▀▀▀▀       █     █▀▀▀█ █▀▀▀█ █▀▀▀▄
	     ▀▀▀▀█ ██▀▀█ ██ ▄▀ ██▀▀        ██    ██  █ ██▀▀█ ██  █
	     ▀▀▀▀▀ ▀▀  ▀ ▀▀▀   ▀▀▀▀▀ ▀▀▀▀▀ ▀▀▀▀▀ ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

#ifndef	DOS
/*
 * FEATURE: the saves listed in the order of their names, as LBA2 lists
 * them. Taken from LBA2.
 */
int	my_sort_function( const void *a, const void *b )
{
	return( stricmp( *(char**)a, *(char**)b ) ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/
#endif

#ifdef	DOS
WORD	PlayerGameList( UBYTE **ptrlistname, UBYTE *listname )
#else
WORD	PlayerGameList( UBYTE **ptrlistname, UBYTE *listname, WORD flagnew )
#endif
{
	struct	find_t	fileinfo ;
	ULONG	rc ;
	UBYTE	wbyte ;
	UBYTE	pathname[_MAX_PATH] ;
#ifdef	FIXES
	UBYTE	*entry ;
#endif
	WORD	nb = 0 ;
	ULONG	handle ;
#ifndef	DOS
	UBYTE	**startptrlist = ptrlistname ;
	LONG	maxplayer = MAX_PLAYER ;
	LONG	n ;

	/*
	 * FEATURE: with SaveAnywhere, the list of LBA2's GAMEMENU.CPP: its
	 * bound, current.lba and autosave.lba left out, the names sorted,
	 * autosave.lba's last to be loaded. Taken from LBA2.
	 */
	if( FlagSaveAnywhere )	maxplayer = MAX_PLAYER_LIST ;
#endif

	strcpy( pathname, PATH_RESSOURCE"*.LBA" ) ;

	rc = _dos_findfirst( pathname, _A_NORMAL, &fileinfo ) ;
	while( !rc )
	{
		strcpy( pathname, PATH_RESSOURCE ) ;
		strcat( pathname,  fileinfo.name ) ;
#ifndef	DOS
		// On ne liste pas l'autosave ni la currentsave
		if( FlagSaveAnywhere
		AND (!stricmp( pathname, AUTOSAVE_FILENAME )
		OR   !stricmp( pathname, CURRENTSAVE_FILENAME )) )
		{
			rc = _dos_findnext( &fileinfo ) ;
			continue ;
		}
#endif
		handle = OpenRead( pathname ) ;
		if( handle )
		{
			Read( handle, &wbyte, 1 ) ;	// num version

#ifdef	FIXES
			entry = listname ;
#endif
			*ptrlistname++ = listname ;
#ifndef	DOS
			if( FlagSaveAnywhere )
			{
				/*
				 * FIXED: LBA2 reads the name up to its 0, past the
				 * room it has for one and past the file's end, as
				 * LBA.EXE does. It reads MAX_SIZE_PLAYER_NAME bytes
				 * at most, and stops at the file's end.
				 */
				n = 0 ;
				do
				{
					if( (n == MAX_SIZE_PLAYER_NAME)
					OR  (Read( handle, &wbyte, 1 ) != 1) )
					{
						wbyte = 0 ;
					}
					*listname++ = wbyte ;
					n++ ;

				} while( wbyte != 0 ) ;
			}
			else
#endif
			do
			{
				Read( handle, &wbyte, 1 ) ;
				*listname++ = wbyte ;

			} while( wbyte != 0 ) ;

			Close( handle ) ;

			/*
			 * FIXED: LBA.EXE lists the names alone, and the name
			 * chosen is looked up again (FindPlayerFile), so that of
			 * two saves of one name the first is loaded. Each entry
			 * keeps its file after its name. Taken from LbaWin 0.7.8.
			 */
#ifdef	FIXES
			strcpy( entry+MAX_SIZE_PLAYER_NAME+1, pathname ) ;
			listname = entry+SIZE_PLAYER_ENTRY ;
#endif

			nb++ ;
#ifdef	DOS
			if( nb == MAX_PLAYER )	return nb ;
#else
			if( nb == maxplayer )	return nb ;
#endif
		}
		rc = _dos_findnext( &fileinfo ) ;
	}
#ifndef	DOS
	if( FlagSaveAnywhere )
	{
		qsort(	(void *)startptrlist,
			nb,
			sizeof( UBYTE * ),
			my_sort_function ) ;

		/*
		 * FIXED: LBA2 reads autosave.lba's name through PlayerName and
		 * GamePathname, which each caller then puts back,
		 * ChoosePlayerName the name alone. It is read straight into
		 * the list.
		 */
		if( !flagnew AND nb<maxplayer AND FileSize( AUTOSAVE_FILENAME ) )
		{
			handle = OpenRead( AUTOSAVE_FILENAME ) ;
			if( handle )
			{
				Read( handle, &wbyte, 1 ) ;	// num version

				entry = listname ;
				*ptrlistname = listname ;
				n = 0 ;
				do
				{
					if( (n == MAX_SIZE_PLAYER_NAME)
					OR  (Read( handle, &wbyte, 1 ) != 1) )
					{
						wbyte = 0 ;
					}
					*listname++ = wbyte ;
					n++ ;

				} while( wbyte != 0 ) ;

				Close( handle ) ;
				strcpy( entry+MAX_SIZE_PLAYER_NAME+1, AUTOSAVE_FILENAME ) ;
				nb++ ;
			}
		}
	}
#endif
	return nb ;
}

/*──────────────────────────────────────────────────────────────────────────*/

WORD	FindPlayerFile()
{
	struct	find_t	fileinfo ;
	ULONG	rc ;
	UBYTE	pathname[_MAX_PATH] ;
	UBYTE	playername[MAX_SIZE_PLAYER_NAME+1] ;
	UBYTE	*ptr ;
	ULONG	handle ;
	UBYTE	wbyte ;
#ifndef	DOS
	LONG	n ;
#endif

	strcpy( pathname, PATH_RESSOURCE"*.LBA" ) ;

	rc = _dos_findfirst( pathname, _A_NORMAL, &fileinfo ) ;
	while( !rc )
	{
		strcpy( pathname, PATH_RESSOURCE ) ;
		strcat( pathname,  fileinfo.name ) ;

		handle = OpenRead( pathname ) ;

		Read( handle, &wbyte, 1 ) ;	// num version

		ptr = playername ;
#ifndef	DOS
		if( FlagSaveAnywhere )
		{
			/*
			 * FIXED: as LBA.EXE, LBA2 reads the name up to its 0, past
			 * playername's room and past the file's end, and from
			 * handle 0 when the file cannot be opened. It reads
			 * MAX_SIZE_PLAYER_NAME bytes at most, from a file open.
			 */
			n = 0 ;
			do
			{
				if( (handle == 0)
				OR  (n == MAX_SIZE_PLAYER_NAME)
				OR  (Read( handle, &wbyte, 1 ) != 1) )
				{
					wbyte = 0 ;
				}
				*ptr++ = wbyte ;
				n++ ;

			} while( wbyte != 0 ) ;
		}
		else
#endif
		do
		{
			Read( handle, &wbyte, 1 ) ;
			*ptr++ = wbyte ;

		} while( wbyte != 0 ) ;

		Close( handle ) ;

		if( !stricmp( PlayerName, playername ) )
		{
			strcpy( GamePathname, PATH_RESSOURCE ) ;
			strcat( GamePathname, fileinfo.name ) ;
			return TRUE ;
		}

		rc = _dos_findnext( &fileinfo ) ;
	}
#ifndef	DOS
	/*
	 * FIXED: LBA2 lets a player name a save AUTOSAVE while autosave.lba
	 * is missing, and the list then shows two AUTOSAVE once the first
	 * scene's autosave is made. AUTOSAVE names autosave.lba, whether it
	 * exists or not.
	 */
	if( FlagSaveAnywhere AND !stricmp( PlayerName, AUTOSAVE_NAME ) )
	{
		strcpy( GamePathname, AUTOSAVE_FILENAME ) ;
		return TRUE ;
	}
#endif
	return FALSE ;
}

/*──────────────────────────────────────────────────────────────────────────*/

void	SaveGame()
{
	WORD	handle ;
	WORD	wword ;
	UBYTE	wbyte ;

	handle = OpenWrite( GamePathname ) ;
	if( !handle )
	{
		Message( "Error Writing Saved Game", TRUE ) ;
		return ;
	}

	NumVersion = 3 ;

	Write( handle, &NumVersion, 1 ) ;

	Write( handle, PlayerName, strlen(PlayerName)+1 ) ;

// list flag game
	wbyte = MAX_FLAGS_GAME ;
	Write( handle, &wbyte, 1 ) ;	// nb octets
	Write( handle, ListFlagGame, MAX_FLAGS_GAME ) ;

// infos jeu
	Write( handle, &NumCube, 1 ) ;
	Write( handle, &Chapitre, 1 ) ;
	Write( handle, &Comportement, 1 ) ;
	Write( handle, &ListObjet[NUM_PERSO].LifePoint, 1 ) ;

	Write( handle, &NbGoldPieces, 2 ) ;

	Write( handle, &MagicLevel, 1 ) ;
	Write( handle, &MagicPoint, 1 ) ;
	Write( handle, &NbCloverBox, 1 ) ;

	Write( handle, &SceneStartX, 2 ) ;
	Write( handle, &SceneStartY, 2 ) ;
	Write( handle, &SceneStartZ, 2 ) ;
	Write( handle, &ListObjet[NUM_PERSO].Beta, 2 ) ;

	Write( handle, &ListObjet[NUM_PERSO].GenBody, 1 ) ;

// tabholopos

	wbyte = MAX_HOLO_POS ;
	Write( handle, &wbyte, 1 ) ;
	Write( handle, TabHoloPos, wbyte ) ;

// others
	Write( handle, &Fuel, 1 ) ;

// used inventory
	wbyte = MAX_INVENTORY ;
	Write( handle, &wbyte, 1 ) ;	// nb octets
	Write( handle, ListFlagInventory, MAX_INVENTORY ) ;

// others
	Write( handle, &NbFourLeafClover, 1 ) ;
	Write( handle, &Weapon, 2 ) ;

	Close( handle ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/

void	LoadGame()
{
	WORD	handle ;
	WORD	wword ;
	UBYTE	wbyte ;
	UBYTE	*ptr ;

	handle = OpenRead( GamePathname ) ;
	/*
	 * FIXED: LBA.EXE reads the save from handle 0, the keyboard under
	 * DOS, when its open fails, a file gone since the list was made.
	 * The load returns at once, the game as InitGame set it, a new one.
	 * Taken from DotEmu's Android 1.06.
	 */
#ifdef	FIXES
	if( !handle )	return ;
#endif

#ifdef	DOS
	Read( handle, &wbyte, 1 ) ;	// num version
#else
	Read( handle, &NumVersion, 1 ) ;	// num version
#endif

	ptr = PlayerName ;
	do
	{
		Read( handle, &wbyte, 1 ) ;
		*ptr++ = wbyte ;

	} while( wbyte != 0 ) ;

// list flag game
	Read( handle, &wbyte, 1 ) ;	// nb octets
	Read( handle, ListFlagGame, wbyte ) ;

	NewCube = 0 ;
	Read( handle, &NewCube, 1 ) ;

	Read( handle, &Chapitre, 1 ) ;

	Read( handle, &Comportement, 1 ) ;
	SaveComportement = Comportement ;
	Read( handle, &ListObjet[NUM_PERSO].LifePoint, 1 ) ;

	Read( handle, &NbGoldPieces, 2 ) ;

	Read( handle, &MagicLevel, 1 ) ;
	Read( handle, &MagicPoint, 1 ) ;
	Read( handle, &NbCloverBox, 1 ) ;

	Read( handle, &SceneStartX, 2 ) ;
	Read( handle, &SceneStartY, 2 ) ;
	Read( handle, &SceneStartZ, 2 ) ;
	Read( handle, &ListObjet[NUM_PERSO].Beta, 2 ) ;
	SaveBeta = ListObjet[NUM_PERSO].Beta ;

	Read( handle, &ListObjet[NUM_PERSO].GenBody, 1 ) ;

// tabholopos
	Read( handle, &wbyte, 1 ) ;
	Read( handle, TabHoloPos, wbyte ) ;

// others
	Read( handle, &Fuel, 1 ) ;

// used inventory
	Read( handle, &wbyte, 1 ) ;	// nb octets
	Read( handle, ListFlagInventory, wbyte ) ;

// others
	Read( handle, &NbFourLeafClover, 1 ) ;
	Read( handle, &Weapon, 2 ) ;

#ifndef	DOS
// scène en cours (SaveAnywhere)
	LoadGameContexte( handle, NumVersion ) ;
#endif

	Close( handle ) ;

	NumCube = -1 ;
	FlagChgCube = 3 ;
}

/*══════════════════════════════════════════════════════════════════════════*
	   █    ██▄ █ █▀▀▀█ █   █ ▀▀█▀▀       ██▄ █ █▀▀▀█ █▄ ▄█ █▀▀▀▀
	   ██   ██▀██ ██▀▀▀ ██  █   ██        ██▀██ ██▀▀█ ██▀ █ ██▀▀
	   ▀▀   ▀▀  ▀ ▀▀    ▀▀▀▀▀   ▀▀  ▀▀▀▀▀ ▀▀  ▀ ▀▀  ▀ ▀▀  ▀ ▀▀▀▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

/*──────────────────────────────────────────────────────────────────────────*/

void	DrawOneString( WORD x, WORD y, UBYTE *playername, WORD draw )
{
	WORD	x2, x0,y0,x1,y1 ;
//	UBYTE	string[256] ;

	x0 = x - LargeurMenu/2 ;
	x1 = x + LargeurMenu/2 ;

	y0 = y - HAUTEUR_STANDARD/2 ;
	y1 = y + HAUTEUR_STANDARD/2 ;

#ifndef	DOS
	/*
	 * FEATURE: draw's low bits alone, the list passing its arrows in the
	 * high ones. Taken from LBA2.
	 */
	draw &= 0x0F ;
#endif

	// fond

	if( draw == 0 )
	{
		CopyBlock( x0, y0, x1, y1, Screen, x0, y0, Log ) ;
		ShadeBox( x0, y0, x1, y1, 4 ) ;
	}
	if( draw == 1 )
	{
		DrawFire( x0, y0, x1, y1, COUL_INPUT_NAME&0xF0 ) ;
	}
	if( draw == 2 )
	{
		Box( x0, y0, x1, y1, 91 ) ;
	}

	// cadre
	DrawCadre( x0, y0, x1, y1 ) ;

	// text
	CoulFont( COUL_TEXT_MENU ) ;

//	if( playername[0] != 0 )
		Font( x - SizeFont( playername )/2, y-18, playername ) ;

#ifndef	DOS
	/*
	 * FEATURE: the cursor of the name typed, which LBA2 draws as a sprite
	 * of its own sprite file, a bar here. Taken from LBA2.
	 */
	if( CursorActif )	Box( CursorX+4, CursorY-16, CursorX+6, CursorY+14, COUL_TEXT_MENU ) ;
#endif

	// flip

	CopyBlockPhys( x0,y0, x1,y1 ) ;
}

#ifndef	DOS
/*──────────────────────────────────────────────────────────────────────────*/
/*
 * FEATURE: a line of the list put back as the background has it. Taken
 * from LBA2.
 */
void	ClearOneString( WORD x, WORD y )
{
	WORD	x0,y0,x1,y1 ;

	x0 = x - LargeurMenu/2 ;
	x1 = x + LargeurMenu/2 ;

	y0 = y - HAUTEUR_STANDARD/2 ;
	y1 = y + HAUTEUR_STANDARD/2 ;

	// fond
	CopyBlock( x0, y0, x1, y1, Screen, x0, y0, Log ) ;
	CopyBlockPhys( x0,y0, x1,y1 ) ;
}
#endif

/*──────────────────────────────────────────────────────────────────────────*/

void	DrawSingleString( WORD x, WORD y, UBYTE *string )
{
	WORD	x2, x0,y0,x1,y1 ;
	WORD	sf ;

	sf = SizeFont( string ) ;

	x0 = x - sf/2 ;
	x1 = x + sf/2 ;

	y0 = y - HAUTEUR_STANDARD/2 ;
	y1 = y + HAUTEUR_STANDARD/2 ;

	// cadre
//	DrawCadre( x0, y0, x1, y1 ) ;

	// text
	CoulFont( 0 ) ;
	Font( x0 + 4, y-18 + 4, string ) ;
	CoulFont( COUL_TEXT_MENU ) ;
	Font( x0, y-18, string ) ;

	// flip
	CopyBlockPhys( x0,y0, x1,y1 ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/

UBYTE	TabLetter[NB_CAR_V][NB_CAR_H] =
{
	{ ' ','A','B','C','D','E','F','G','H','I','J','K','L','M' } ,
	{ '.','N','O','P','Q','R','S','T','U','V','W','X','Y','Z' } ,
	{ '-','a','b','c','d','e','f','g','h','i','j','k','l','m' } ,
	{ '?','n','o','p','q','r','s','t','u','v','w','x','y','z' } ,
	{ '!','0','1','2','3','4','5','6','7','8','9',' ', 8 , 13 }   } ;

//	{ ' ','é','è','ç','à','ô','ê','â','î','8','9',' ',' ',' ' } ,

/*──────────────────────────────────────────────────────────────────────────*/

/*
WORD	SearchLetter( UBYTE car )
{
	WORD	x, y ;

	for( y=0; y<NB_CAR_H; y++ )
	{
		for( x=0; x<NB_CAR_H; x++ )
		{
			if( car == TabLetter[y][x] )
			{
				return TRUE ;
			}
		}
	}
	return FALSE ;
}
*/
/*──────────────────────────────────────────────────────────────────────────*/

void	DrawOneLetter( WORD tabx, WORD taby, WORD draw )
{
	WORD	x2, x0,y0,x1,y1 ;
	UBYTE	letter[256] ;
	WORD	x, y ;

	letter[0] = TabLetter[taby][tabx] ;
	letter[1] = 0 ;

	x = 25 + (640/NB_CAR_H) * tabx ;
	y = 200 + (280/NB_CAR_V) * taby ;

	x0 = x - 40/2 ;
	x1 = x + 40/2 ;

	y0 = y - 50/2 ;
	y1 = y + 50/2 ;

	// fond

	if( draw )
	{
		Box( x0, y0, x1, y1, 91 ) ;
	}
	else
	{
		CopyBlock( x0, y0, x1, y1, Screen, x0, y0, Log ) ;
		ShadeBox( x0, y0, x1, y1, 4 ) ;
	}

	// cadre
	DrawCadre( x0, y0, x1, y1 ) ;

	// text

	CoulFont( COUL_LETTER_INPUT ) ;

	Font( x - SizeFont( letter )/2, y-18, letter ) ;

	// flip

	CopyBlockPhys( x0,y0, x1,y1 ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/

void	DrawAllLetters()
{
	WORD	x, y ;

	for( y=0; y<NB_CAR_V; y++ )
	{
		for( x=0; x<NB_CAR_H; x++ )
		{
			DrawOneLetter( x,y, FALSE ) ;
		}
	}
}

/*──────────────────────────────────────────────────────────────────────────*/
/*──────────────────────────────────────────────────────────────────────────*/

#ifndef	DOS
/*
 * FEATURE: LBA2's characters of a name, any but those of a file's path.
 * Taken from LBA2.
 */
/*
 * FIXED: LBA2's form gives strchr the whole word, so that a key's word
 * whose low byte is 0, Left among them, finds the string's end and comes
 * out invalid, and its case is never reached. Such a word holds no letter.
 */
#define	IsValidChar(c)	(!((c) & 0xFF) OR !strchr( "<>:\"|\\/", (c) ))
#endif

#ifdef	DOS
WORD	InputPlayerName( WORD nummess )
#else
WORD	InputPlayerName( WORD nummess, UBYTE *name )
#endif
{
	WORD	flag = 1 ;
	UBYTE	string[256] ;
	WORD	poscarX = 0 , poscarY = 0 ;
	WORD	oldposcarX = -1 , oldposcarY ;
	WORD	len ;
	WORD	memoasciimode ;
	WORD	car = 0 ;
	WORD	retval = FALSE ;
	UBYTE	incar ;

#ifndef	DOS
	/*
	 * FEATURE: with SaveAnywhere, LBA2's InputPlayerName: the name typed
	 * on the list's line nummess, from name, its cursor blinking, Escape
	 * giving the name back, Return refused on an empty name or one
	 * another save has. Taken from LBA2.
	 */
	if( FlagSaveAnywhere )
	{
		UBYTE	memoplayername[MAX_SIZE_PLAYER_NAME+1] ;
		WORD	x, y ;
		LONG	akey ;
		ULONG	timeraff, timernoaff ;

		while( Key OR Fire ) idle() ;

		memoasciimode = AsciiMode ;
		AsciiMode = TRUE ;
		while( GetAscii() ) ;

		strcpy( memoplayername, name ) ;
		/*
		 * FIXED: LBA2's ChoosePlayerName gives PlayerName as name, which
		 * strcpy then copies onto itself.
		 */
		if( name != PlayerName )	strcpy( PlayerName, name ) ;

		len = strlen( PlayerName ) ;

		y = Y_START_CHOICE+nummess*60 ;
		/*
		 * FIXED: LBA2 places the cursor from x before it sets it, in the
		 * loop's first turn.
		 */
		x = 320+SizeFont(PlayerName)/2 ;

		timeraff = TimerRef+20 ;
		timernoaff = 0 ;
		CursorActif = TRUE ;

		while( flag!=-1 )
		{
			if( TimerRef<timeraff )
			{
				CursorX = x-4 ;
				CursorY = y+2 ;

				if( !len )	CursorX -= 16 ;

				CursorActif = TRUE ;
				timernoaff = TimerRef+10 ;
			}
			else
			{
				if( TimerRef>=timernoaff )
				{
					timeraff = TimerRef+20 ;
					CursorActif = TRUE ;
					CursorX = x-4 ;
					CursorY = y+2 ;

					if( !len )	CursorX -= 16 ;
				}
				else	CursorActif = FALSE ;
			}

			DrawOneString( 320, y, PlayerName, 1 ) ;

			if( flag==1 )
			{
				x = 320+SizeFont(PlayerName)/2 ;
				flag = 0 ;
			}

			do
			{
			akey = GetAscii() ;

			if( !IsValidChar(akey) )	akey = 0 ;

			switch( akey )
			{
				case A_ESC:
					strcpy( PlayerName, memoplayername ) ;
					retval = FALSE ;
					flag = -1 ;
					break ;

				case A_RETURN:
					if( strcmp(PlayerName,"")
					AND ((FindPlayerFile()==FALSE)
					OR (!strcmp(PlayerName,memoplayername))) )
					{
						retval = TRUE ;
						flag = -1 ;
					}
					else
					{
						strcpy( PlayerName, memoplayername ) ;
						len = strlen( PlayerName ) ;
						flag = 1 ;
					}
					break ;

				case A_LEFT:
				case A_BACKSPACE:
					if( len>0 )
					{
						PlayerName[--len] = '\0' ;
						flag = 1 ;
					}
					break ;

				default:
					akey &= 0xFF ;

					/*
					 * FIXED: LBA2 bounds the name by the line's width
					 * alone, past the room PlayerName has, and LBA.EXE
					 * reads 30 bytes of it. It takes
					 * MAX_SIZE_PLAYER_NAME characters at most.
					 */
					if( x<550
					AND len<MAX_SIZE_PLAYER_NAME
					AND akey>=32 AND akey<=122 )
					{
						PlayerName[len++] = (char)akey ;
						PlayerName[len] = '\0' ;
						x = 320+SizeFont(PlayerName)/2 ;
					}
					break ;
			}// end_switch
			}
			while( akey ) ;	// Tant que Buffer Clavier non vide
		}

		AsciiMode = memoasciimode ;

		while( Key OR Fire ) idle() ;

		CursorActif = FALSE ;

		return retval ;
	}
#endif

try_again:

	CopyScreen( Screen, Log ) ;
	Flip() ;

	InitDial( 0 ) ;

	GetMultiText( nummess, string ) ;
	CoulFont( COUL_LETTER_INPUT ) ;
	Font( 320 - SizeFont(string)/2, 20, string ) ;
	CopyBlockPhys( 0,0,639,99 ) ;

	strcpy( PlayerName, "" ) ;
	DrawOneString( 320, 100, PlayerName, 1 ) ;
	DrawAllLetters() ;

	while( Key OR Fire ) idle() ;

	memoasciimode = AsciiMode ;
	AsciiMode = TRUE ;

	while( GetAscii() != 0 ) ;

	while( Key != K_ESC )
	{
#ifdef	DEBUG_TOOLS
		CheckSavePcx() ;
#endif
		////////////////

		car = GetAscii() ;
		MyKey = Key ;
		MyJoy = Joy ;
		MyFire = Fire ;

		///////////////

		DrawOneString( 320, 100, PlayerName, 1 ) ;

		if( flag & 1 )
		{
			if( oldposcarX != -1 )
				DrawOneLetter( oldposcarX, oldposcarY, FALSE ) ;
			DrawOneLetter( poscarX, poscarY, TRUE ) ;
			oldposcarX = poscarX ;
			oldposcarY = poscarY ;
			flag &= ~1 ;
		}

		if( flag & 2 )
		{
			while( Fire ) idle() ;
			flag &= ~2 ;
		}

		///////////////

		if( car == A_SPACE )
		{
			incar = TabLetter[poscarY][poscarX] ;
			if( (incar == 8) OR (incar == 13) )
			{
				if( incar == 8 )	car = A_BACKSPACE ;
				if( incar == 13 )	car = A_RETURN ;
			}
			else
			{
				len = strlen(PlayerName) ;
				if( len < MAX_SIZE_PLAYER_NAME )
				{
					PlayerName[len] = incar ;
					PlayerName[len+1] = 0 ;
					flag |= 2 ;
				}
			}
		}

		if( car == A_RIGHT )
		{
			poscarX++ ;
			if( poscarX == NB_CAR_H )	poscarX = 0 ;
			flag |= 1 ;
		}
		if( car == A_LEFT )
		{
			poscarX-- ;
			if( poscarX == -1 )	poscarX = NB_CAR_H - 1 ;
			flag |= 1 ;
		}
		if( car == A_DOWN )
		{
			poscarY++ ;
			if( poscarY == NB_CAR_V )	poscarY = 0 ;
			flag |= 1 ;
		}
		if( car == A_UP )
		{
			poscarY-- ;
			if( poscarY == -1 )	poscarY = NB_CAR_V - 1 ;
			flag |= 1 ;
		}

		///////////////

		len = strlen(PlayerName) ;

		if( car == A_BACKSPACE )
		{
			if( len > 0 )	PlayerName[len-1] = 0 ;
			else		car = 0 ;
		}

		if( car == A_RETURN )
		{
			if( len != 0 )	break ;
			else		car = 0 ;
		}

		if( (car != A_SPACE) AND (car != 0) )
		{
			car &= 255 ;

//			if( SearchLetter( car ) )
			if( (car>32) AND (car<255) )
			{
				if( len < MAX_SIZE_PLAYER_NAME )
				{
					PlayerName[len] = car ;
					PlayerName[len+1] = 0 ;
				}
			}
		}

		////////////////
	}

	if( car == A_RETURN )
	{
		if( FindPlayerFile() == TRUE )
		{
			nummess = 43 ;
			goto try_again ;
		}
		else	retval = TRUE ;
	}

#ifdef	TRACE
	AsciiMode = memoasciimode ;
#else
	AsciiMode = FALSE ;
#endif

	CopyScreen( Screen, Log ) ;
	Flip() ;

	return retval ;
}

/*──────────────────────────────────────────────────────────────────────────*/
/*──────────────────────────────────────────────────────────────────────────*/

#ifndef	DOS
#define	NB_SAVE_CHOICE	5	// le NB_GAME_CHOICE de LBA2: lignes de la liste

#define	ARROW_SG	(1<<4)	// Flèche Supérieur Gauche des cadres
#define	ARROW_ID	(1<<6)	// Flèche Inférieur Droit des cadres

#define	TIME_ZOOM_SAVE	30	// les 600 ms de LBA2, en 50èmes de seconde

/*──────────────────────────────────────────────────────────────────────────*/
/*
 * FEATURE: a save's picture, ptr, framed at x, y. Taken from LBA2.
 */
void	DrawScreenSave( UBYTE *ptr, WORD x, WORD y )
{
	WORD	x1, y1 ;

	x1 = x + SCREEN_SAVE_WIDTH - 1 ;
	y1 = y + SCREEN_SAVE_HEIGHT - 1 ;

	Box( x, y, x1, y1, 0 ) ;
	RestoreBlock( Log, ptr, x, y, x1, y1 ) ;
	DrawCadre( x-1, y-1, x1+1, y1+1 ) ;
	CopyBlockPhys( x-1, y-1, x1+1, y1+1 ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/
/*
 * FEATURE: the frame of a save's picture, empty, at x, y: a version 3's
 * or the New Game line's. Taken from LBA2.
 */
void	DrawCadreSave( WORD x, WORD y )
{
	WORD	x1, y1 ;

	x1 = x + SCREEN_SAVE_WIDTH - 1 ;
	y1 = y + SCREEN_SAVE_HEIGHT - 1 ;

	Box( x, y, x1, y1, 0 ) ;
	DrawCadre( x-1, y-1, x1+1, y1+1 ) ;
	CopyBlockPhys( x-1, y-1, x1+1, y1+1 ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/
/*
 * FEATURE: PlayerName's save shown with its picture, then deleted if
 * Destroy is chosen and kept on Cancel or Escape, as LBA2 deletes one:
 * whether it was. Taken from LBA2.
 */
WORD	DeleteSavedGame()
{
	UBYTE	*ptr ;
	WORD	ret = -1 ;
	WORD	select ;

	DrawOneString( 320, 80, PlayerName, 2 ) ;
	ptr = LoadGameScreen() ;
	if( ptr )	DrawScreenSave( ptr, 240, 130 ) ;
	else		DrawCadreSave( 240, 130 ) ;

	SavedDeleteMenu[0] = 0 ;

	while( ret==-1 )
	{
		select = DoGameMenu( SavedDeleteMenu ) ;

		switch( select )	// num mess
		{
			case 24|0x4000:	// annuler
			case 1000:	// <ESC>
				ret = 0 ;
				break ;

			case 48|0x4000: // detruire
				// retourne 0 si Pb -> pas effacé
				//          1 si Ok
				ret = Delete( GamePathname ) ;
				break ;
		}
	}

	while( Key OR Fire OR Joy )	idle() ;

	return ret ;
}

/*──────────────────────────────────────────────────────────────────────────*/
/*
 * FEATURE: the scene of a game loaded, drawn in Log, grown over black
 * out of its save's picture at x, y to the whole screen, in
 * TIME_ZOOM_SAVE ticks of game time held, as LBA2 shows a game loaded.
 * Taken from LBA2.
 *
 * FEATURE: x, y are the picture's place in the window of 640x480, and
 * the scene grows to the whole screen, however wide, in its
 * coordinates. The port's own.
 */
void	ZoomSavedGame( WORD x, WORD y )
{
	LONG	x0, y0, x1, y1 ;
	ULONG	time, savetimer ;
	UBYTE	*ptr ;

	/*
	 * FIXED: ScaleBox reads a pixel and a line past its source where the
	 * box's size divides it exactly, 120 lines of 480 on the port's
	 * screen, which LBA2 leaves past its block. That line here repeats
	 * the last.
	 */
	ptr = Malloc( LARGEUR_ECRAN*(HAUTEUR_ECRAN+1) + 1 ) ;
	if( !ptr )	TheEnd( NOT_ENOUGH_MEM, "ZoomSavedGame" ) ;

	SaveTimer() ;

	x += FENETRE_X ;
	pushscreen() ;

	CopyScreen( Log, ptr ) ;
	memcpy( ptr+LARGEUR_ECRAN*HAUTEUR_ECRAN, ptr+LARGEUR_ECRAN*(HAUTEUR_ECRAN-1), LARGEUR_ECRAN ) ;
	ptr[LARGEUR_ECRAN*(HAUTEUR_ECRAN+1)] = ptr[LARGEUR_ECRAN*(HAUTEUR_ECRAN+1)-1] ;
	Cls() ;
	Flip() ;
	if( FlagPalettePcx )	Palette( PalettePcx ) ;
	else			Palette( PtrPal ) ;
	/*
	 * FIXED: the palette is set here over the black that InitGame's
	 * fade left, and FlagBlackPal kept saying black, so that the next
	 * fade to black, at a change of scene, a picture or a load, cut to
	 * black at once. It is cleared, as FadeToPal clears it.
	 */
	FlagBlackPal = FALSE ;

	savetimer = TimerRef ;

	while( TRUE )
	{
		time = TimerRef-savetimer ;

		x0 = RegleTrois32( x, 0, TIME_ZOOM_SAVE, time ) ;
		x1 = RegleTrois32( x+SCREEN_SAVE_WIDTH-1, LARGEUR_ECRAN-1, TIME_ZOOM_SAVE, time ) ;
		y0 = RegleTrois32( y, 0, TIME_ZOOM_SAVE, time ) ;
		y1 = RegleTrois32( y+SCREEN_SAVE_HEIGHT-1, HAUTEUR_ECRAN-1, TIME_ZOOM_SAVE, time ) ;

		if( x0<=0 OR y0<=0 OR x1>=LARGEUR_ECRAN-1 OR y1>=HAUTEUR_ECRAN-1 )	break ;

		ScaleBox( 0, 0, LARGEUR_ECRAN-1, HAUTEUR_ECRAN-1, ptr, x0, y0, x1, y1, Log ) ;
		Vsync() ;
		CopyBlockPhys( x0, y0, x1, y1 ) ;
	}

	CopyScreen( ptr, Log ) ;
	Flip() ;

	Free( ptr ) ;

	poparea() ;
	RestoreTimer() ;
}

/*──────────────────────────────────────────────────────────────────────────*/
/*
 * FEATURE: whether a save is listed, for the main menu. Taken from
 * LBA2.
 */
WORD	IsExistSavedGame()
{
	UBYTE	*listplayername ;
	UBYTE	**ptrlist ;
	WORD	nb ;

	listplayername = Malloc( MAX_PLAYER_LIST * SIZE_PLAYER_ENTRY ) ;
	ptrlist = (UBYTE**)HostMalloc( MAX_PLAYER_LIST * sizeof(UBYTE*) ) ;

	if( !listplayername OR !ptrlist )
	{
		TheEnd( NOT_ENOUGH_MEM, "IsExistSavedGame" ) ;
	}

	nb = PlayerGameList( ptrlist, listplayername, FALSE ) ;

	Free( ptrlist ) ;
	Free( listplayername ) ;

	return nb ;
}

/*──────────────────────────────────────────────────────────────────────────*/
#endif

#define	NB_GAME_CHOICE	6

#ifdef	DOS
WORD	ChoosePlayerName( WORD mess )
#else
WORD	ChoosePlayerName( WORD mess, WORD flagflip, WORD flagnew )
#endif
{
	WORD	flag = 1 ;
	UBYTE	*listplayername ;
	UBYTE	**ptrlist ;
	WORD	n, nb ;
	WORD	start = 0 ;
	WORD	select = 0 ;
	WORD	ys = 50 ;
	WORD	retval = 0 ;
	UBYTE	string[256] ;

#ifndef	DOS
	/*
	 * FEATURE: with SaveAnywhere, LBA2's list of saves, five lines at a
	 * time under the selected save's picture: Up and Down, Page Up and
	 * Page Down, Home and End move, D, Backspace or Del delete the save
	 * selected, Return or any fire key but Del chooses. To save
	 * (flagnew), its last line is New Game, a save chosen is named
	 * again on its line (InputPlayerName), and the list gives 2, the
	 * game then saved to GamePathname under PlayerName. To load, it
	 * gives 1, PlayerName and GamePathname the save's. Escape gives 0,
	 * the names as they were. Taken from LBA2.
	 */
	if( FlagSaveAnywhere )
	{
		UBYTE	memoplayername[MAX_SIZE_PLAYER_NAME+1] ;
		UBYTE	memogamepathname[_MAX_PATH] ;
		UBYTE	memoname[_MAX_PATH] ;
		UBYTE	newgametxt[256] ;
		UBYTE	*ptr ;
		ULONG	timeranotherkey = 0 ;
		WORD	timerdraw = 1 ;
		WORD	draw = 1 ;
		WORD	oldselect ;
		WORD	nbsave ;	// les sauvegardes de la liste, sans la ligne Nouvelle partie
		WORD	mykey = 0, myjoy = 0, myfire = 0 ;
		LONG	flagpcx = FlagPalettePcx ;

		flag = 3 ;
		ys = Y_START_CHOICE ;

		strcpy( memoplayername, PlayerName ) ;
		strcpy( memogamepathname, GamePathname ) ;

		while( Key OR Fire OR Joy )	idle() ;

		// le plasma du feu, à la place de la voix en jeu
		Load_HQR( PATH_RESSOURCE"ress.hqr", BufSpeak, RESS_INIT_PLASMA ) ;

		CopyScreen( Screen, Log ) ;
		Flip() ;
		if( flagpcx )	Palette( PtrPal ) ;

		listplayername = Malloc( MAX_PLAYER_LIST * SIZE_PLAYER_ENTRY ) ;
		ptrlist = (UBYTE**)HostMalloc( MAX_PLAYER_LIST * sizeof(UBYTE*) ) ;

		if( !listplayername OR !ptrlist )
		{
			TheEnd( NOT_ENOUGH_MEM, "Choose Player Name" ) ;
		}

		nb = PlayerGameList( ptrlist, listplayername, flagnew ) ;

		if( !nb AND !flagnew )
		{
			Free( ptrlist ) ;
			Free( listplayername ) ;
			if( flagpcx )	Palette( PalettePcx ) ;
			return FALSE ;
		}

		InitDial( 0 ) ;
		GetMultiText( 71|0x4000, newgametxt ) ;	// Nouvelle partie

		if( flagnew AND nb<MAX_PLAYER_LIST-1 )
		{
			ptrlist[nb] = newgametxt ;
			nb++ ;
		}
		else	flagnew = 0 ;

		if( strcmp(PlayerName,"") )
		{
			/*
			 * FIXED: LBA2 selects the first save of the player's
			 * name, which of two saves of one name may be another
			 * file than the game's, and Return then saves over that
			 * file. The save of the game's own file, GamePathname,
			 * is selected first, each line keeping its file.
			 */
			nbsave = flagnew ? nb-1 : nb ;
			for( select=0; select<nbsave; select++ )
			{
				if( !stricmp(GamePathname,ptrlist[select]+MAX_SIZE_PLAYER_NAME+1) )
					break ;
			}
			if( select == nbsave )
			{
				for( select=0; select<nb-1; select++ )
				{
					if( !stricmp(PlayerName,ptrlist[select]) )
						break ;
				}
			}

			while( select >= (start + NB_SAVE_CHOICE) )
			{
				if( start < (nb-1) )
				{
					start++ ;
				}
			}
		}

		while( mykey != K_ESC )
		{
			if( flag == 3 )
			{
				DrawSingleString( 320, 25, (UBYTE*)GetMultiText( mess,string ) ) ;
				flag = 1 ;
			}

			if( flag == 1 )
			{
				for( n=0; n<NB_SAVE_CHOICE; n++ )
				{
					if( n+start >= nb )	break ;

					draw = ((n+start) == select) ;

					if( (n+start) == select )
					{
						ys = Y_START_CHOICE+60*n ;
					}

					if( start>0 AND n==0 )	draw |= ARROW_SG ;
					else if( (start+NB_SAVE_CHOICE<nb) AND (n==NB_SAVE_CHOICE-1) )
							draw |= ARROW_ID ;

					DrawOneString( 320, Y_START_CHOICE+60*n, ptrlist[n+start], draw ) ;
				}

				draw = 1 ;

				if( start>0 AND select==start )	draw |= ARROW_SG ;
				else if( (start+NB_SAVE_CHOICE<nb) AND (select==start+NB_SAVE_CHOICE-1) )
						draw |= ARROW_ID ;

				timeranotherkey = TimerRef + TEMPO_REPEAT_KEY ;
				mykey = myjoy = myfire = 0 ;
				flag = 0 ;
			}

			if( timerdraw )
			{
				strcpy( PlayerName, ptrlist[select] ) ;

				if( !flagnew OR select<nb-1 )
				{
					strcpy( GamePathname, ptrlist[select]+MAX_SIZE_PLAYER_NAME+1 ) ;
					ptr = LoadGameScreen() ;
					if( ptr )	DrawScreenSave( ptr, 240, 50 ) ;
					else		DrawCadreSave( 240, 50 ) ;
				}
				else	// Nouvelle partie
				{
					DrawCadreSave( 240, 50 ) ;
				}

				timerdraw = 0 ;
			}

			if( TimerRef>timeranotherkey )
			{
				mykey = Key ;
				myjoy = Joy ;
				myfire = Fire ;
			}

			DrawOneString( 320, ys, ptrlist[select], draw ) ;

			oldselect = select ;

			switch( myjoy & (J_UP|J_DOWN|J_LEFT|J_RIGHT) )
			{
				case J_DOWN:
					if( select < (nb-1) )
					{
						select++ ;
						if( select >= (start + NB_SAVE_CHOICE) )
						{
							if( start < (nb-1) )
							{
								start++ ;
							}
						}
						else
						{
							DrawOneString( 320, ys, ptrlist[select-1], (draw&0xFFFE) ) ;
						}
						flag = 1 ;
						timerdraw = 1 ;
					}
					break ;

				case J_UP:
					if( select > 0 )
					{
						select-- ;

						if( select < start )
						{
							start-- ;
						}
						else
						{
							DrawOneString( 320, ys, ptrlist[select+1], (draw&0xFFFE) ) ;
						}
						flag = 1 ;
						timerdraw = 1 ;
					}
					break ;

				case J_DOWN|J_RIGHT:	// page suivante
					if( select < (nb-1) )
					{
						select+=NB_SAVE_CHOICE ;
						if( select>=nb )	select = nb-1 ;

						if( select >= (start + NB_SAVE_CHOICE) )
						{
							start += NB_SAVE_CHOICE ;
							if( start >= (nb-(NB_SAVE_CHOICE-1)) )	start = nb-NB_SAVE_CHOICE ;
						}
						else
						{
							DrawOneString( 320, ys, ptrlist[oldselect], (draw&0xFFFE) ) ;
						}
						flag = 1 ;
						timerdraw = 1 ;
					}
					break ;

				case J_UP|J_RIGHT:	// page précédente
					if( select > 0 )
					{
						select-=NB_SAVE_CHOICE ;
						if( select<0 )	select=0 ;

						if( select < start )
						{
							start=select ;
						}
						else
						{
							DrawOneString( 320, ys, ptrlist[oldselect], (draw&0xFFFE) ) ;
						}
						flag = 1 ;
						timerdraw = 1 ;
					}
					break ;

				case J_UP|J_LEFT:	// début
					if( select > 0 )
					{
						select = 0 ;

						if( start==0 )
						{
							DrawOneString( 320, ys, ptrlist[oldselect], (draw&0xFFFE) ) ;
						}
						else	start = 0 ;

						flag = 1 ;
						timerdraw = 1 ;
					}
					break ;

				case J_DOWN|J_LEFT:	// fin
					if( select < (nb-1) )
					{
						select = nb-1 ;

						if( start==(nb-NB_SAVE_CHOICE) )
						{
							DrawOneString( 320, ys, ptrlist[oldselect], (draw&0xFFFE) ) ;
						}
						else
						{
							start = nb-NB_SAVE_CHOICE ;
							if( start<0 )	start = 0 ;
						}

						flag = 1 ;
						timerdraw = 1 ;
					}
					break ;
			}

			// Effacement d'une sauvegarde
			/*
			 * FIXED: LBA2 deletes the save whose picture was shown
			 * last, on the New Game line too, and before the picture
			 * of a save moved to in the same frame. The save selected
			 * is looked up again, and D on the New Game line ignored.
			 */
			if( (mykey==K_D
			OR   mykey==K_BACKSPACE
			OR   (myfire & F_SUPPR))
			AND (!flagnew OR select<nb-1) )
			{
				CopyScreen( Screen, Log ) ;
				Flip() ;

				strcpy( PlayerName, ptrlist[select] ) ;
				strcpy( GamePathname, ptrlist[select]+MAX_SIZE_PLAYER_NAME+1 ) ;

				if( DeleteSavedGame() )
				{
					if( nb>1 AND select<nb-1 )
					{
						/*
						 * FIXED: LBA2 moves the names that follow by
						 * memcpy, onto the place they overlap.
						 */
						memmove( &ptrlist[select], &ptrlist[select+1], ((nb-1)-select)*sizeof(UBYTE*) ) ;
					}

					// recharge list player
					nb-- ;

					if( select>=nb )
					{
						select = nb-1 ;
						if( select<0 )		select = 0 ;

						if( select<start )
						{
							start = select ;
							if( start<0 )	start = 0 ;
						}
					}

					if( !nb )	break ;	// plus de sauvegarde
				}

				CopyScreen( Screen, Log ) ;
				Flip() ;

				flag = 1 ;	// redraw menu
				timerdraw = 1 ;
			}

			if( myfire & ~F_SUPPR )
			{
				if( timerdraw )
				{
					strcpy( PlayerName, ptrlist[select] ) ;

					if( !flagnew OR select<nb-1 )
					{
						strcpy( GamePathname, ptrlist[select]+MAX_SIZE_PLAYER_NAME+1 ) ;
						ptr = LoadGameScreen() ;
						if( ptr )	DrawScreenSave( ptr, 240, 50 ) ;
						else		DrawCadreSave( 240, 50 ) ;
					}
					else	// Nouvelle partie
					{
						DrawCadreSave( 240, 50 ) ;
					}

					timerdraw = 0 ;
				}

				if( flagnew )	// Nouvelle partie
				{
					if( select==nb-1 )	strcpy( PlayerName, "" ) ;
					else
					{
						strcpy( GamePathname, ptrlist[select]+MAX_SIZE_PLAYER_NAME+1 ) ;
						strcpy( memoname, GamePathname ) ;
					}

					while( Fire )	idle() ;

					if( InputPlayerName( select-start, PlayerName ) )
					{
						if( select==nb-1 )
						{
							do
							{
								strcpy( GamePathname, PATH_RESSOURCE"S" ) ;
								strcat( GamePathname, Itoa( Rnd(10000) ) ) ;
								strcat( GamePathname, ".LBA" ) ;
							}
							while( FileSize( GamePathname ) != 0 ) ;
						}
						else	// renommer une sauvegarde
						{
							strcpy( GamePathname, memoname ) ;
						}

						retval = 2 ;
						break ;
					}
					else
					{
						while( Fire )	idle() ;
						if( select==nb-1 )	strcpy( PlayerName, newgametxt ) ;
						timerdraw = 1 ;
						flag = 3 ;	// Rafraichissement ecran
						mykey = 0 ;
					}
				}
				else
				{
					retval = 1 ;
					break ;
				}
			}
		}

		if( retval==0 )	// <ESC>, ou plus de sauvegarde
		{
			/*
			 * FIXED: LBA2 puts PlayerName back after Escape alone, and
			 * leaves GamePathname naming the last save the list looked
			 * up, the player's file then another player's. Both are
			 * put back, after the last save deleted too.
			 */
			strcpy( PlayerName, memoplayername ) ;
			strcpy( GamePathname, memogamepathname ) ;
		}
		else if( retval==1 )
		{
			strcpy( PlayerName, ptrlist[select] ) ;
			strcpy( GamePathname, ptrlist[select]+MAX_SIZE_PLAYER_NAME+1 ) ;

			for( n=0; n<NB_SAVE_CHOICE; n++ )
			{
				if( (n+start) != select )
				{
					ClearOneString( 320, Y_START_CHOICE+60*n ) ;
				}
			}
		}

		Free( ptrlist ) ;
		Free( listplayername ) ;

		if( flagflip )
		{
			CopyScreen( Screen, Log ) ;
			Flip() ;
		}

		while( Key OR Fire OR Joy )	idle() ;

		if( flagpcx )	Palette( PalettePcx ) ;

		return retval ;
	}
#endif

	CopyScreen( Screen, Log ) ;
	Flip() ;

#ifdef	FIXES
	listplayername = Malloc( MAX_PLAYER * SIZE_PLAYER_ENTRY ) ;
#else
	listplayername = Malloc( MAX_PLAYER * (MAX_SIZE_PLAYER_NAME+1) ) ;
#endif
	ptrlist = (UBYTE**)HostMalloc( MAX_PLAYER * sizeof(UBYTE*) ) ;

	if( !listplayername OR !ptrlist )
	{
		TheEnd( NOT_ENOUGH_MEM, "Choose Player Name" ) ;
	}

#ifdef	DOS
	nb = PlayerGameList( ptrlist, listplayername ) ;
#else
	nb = PlayerGameList( ptrlist, listplayername, FALSE ) ;
#endif

	if( !nb )	return FALSE ;

	InitDial( 0 ) ;

	DrawSingleString( 320, 40, (UBYTE*)GetMultiText( mess,string ) ) ;

	while( Key != K_ESC )
	{
		if( flag == 1 )
		{
			for( n=0; n<6; n++ )
			{
				if( n+start >= nb )	break ;
				if( (n+start) == select )
				{
					ys = 50+50+60*n ;
				}
				DrawOneString( 320, 50+50+60*n, ptrlist[n+start], (n+start) == select ) ;
			}
			while( Joy OR Fire )
				DrawOneString( 320, ys, ptrlist[select], 1 ) ;
			flag = 0 ;
		}

		DrawOneString( 320, ys, ptrlist[select], 1 ) ;

		if( Joy & J_DOWN )
		{
			if( select < (nb-1) )
			{
				DrawOneString( 320, ys, ptrlist[select], 0 ) ;
				select++ ;
				flag = 1 ;
			}

			if( select >= (start + 6) )
			{
				if( start < (nb-1) )
				{
					start++ ;
				}
			}
		}

		if( Joy & J_UP )
		{
			if( select > 0 )
			{
				DrawOneString( 320, ys, ptrlist[select], 0 ) ;
				select-- ;
				flag = 1 ;
			}
			if( select < start )
			{
				start-- ;
			}
		}

		if( Fire )
		{
			retval = 1 ;
			break ;
		}
	}

	strcpy( PlayerName, ptrlist[select] ) ;
#ifdef	FIXES
	strcpy( GamePathname, ptrlist[select]+MAX_SIZE_PLAYER_NAME+1 ) ;
#else
	FindPlayerFile() ;
#endif

	Free( ptrlist ) ;
	Free( listplayername ) ;

	CopyScreen( Screen, Log ) ;
	Flip() ;

	return retval ;
}

/*══════════════════════════════════════════════════════════════════════════*
			    █▄ ▄█ █▀▀▀▀ ██▄ █ █   █
			    ██▀ █ ██▀▀  ██▀██ ██  █
			    ▀▀  ▀ ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

void	DrawOneChoice( WORD x, WORD y, WORD type, WORD num, WORD select )
{
	WORD	x2, x0,y0,x1,y1 ;
	UBYTE	string[256] ;
	ULONG	volleft, volright ;

	x0 = x - LargeurMenu/2 ;
	x1 = x + LargeurMenu/2 ;

	y0 = y - HAUTEUR_STANDARD/2 ;
	y1 = y + HAUTEUR_STANDARD/2 ;

	// fond

	if( select )
	{
		switch( type )
		{
			case 1:	// music volume
				x2 = RegleTrois32( x0, x1, 255, MusicVolume ) ;
				DrawFire( x0, y0, x2, y1, 91&0xF0 ) ;
				Box( x2, y0, x1, y1, COUL_SELECT_MENU ) ;
				break ;

			case 2:	// sample volume
				x2 = RegleTrois32( x0, x1, 255, SampleVolume ) ;
				DrawFire( x0, y0, x2, y1, 91&0xF0 ) ;
				Box( x2, y0, x1, y1, COUL_SELECT_MENU ) ;
				break ;

			case 3:	// CD volume
				x2 = RegleTrois32( x0, x1, 255, CDVolume ) ;
				DrawFire( x0, y0, x2, y1, 91&0xF0 ) ;
				Box( x2, y0, x1, y1, COUL_SELECT_MENU ) ;
				break ;

			case 4:	// Line volume
				x2 = RegleTrois32( x0, x1, 255, LineVolume ) ;
				DrawFire( x0, y0, x2, y1, 91&0xF0 ) ;
				Box( x2, y0, x1, y1, COUL_SELECT_MENU ) ;
				break ;

			case 5:	// Master volume
				x2 = RegleTrois32( x0, x1, 255, MasterVolume ) ;
				DrawFire( x0, y0, x2, y1, 91&0xF0 ) ;
				Box( x2, y0, x1, y1, COUL_SELECT_MENU ) ;
				break ;
			default:
//				Box( x0, y0, x1, y1, COUL_SELECT_MENU ) ;
				DrawFire( x0, y0, x1, y1, COUL_SELECT_MENU&0xF0 ) ;
		}

		if( (type >= 1) AND (type<=5) )
		{
			// bruitons un peu la situation (violement)

			DoLoopMidi() ;

			if( (TimerRef-TimerSample) > 30 )
			{
				TimerSample = TimerRef ;
				Balance( rand()&127, 128, &volleft, &volright );
				HQ_MixSample( 34+((rand()&7)>0)*92, 0x1000, 1, volleft, volright ) ;
//				HQ_MixSample( Rnd( 50 ), 0x1000, 1, volleft, volright ) ;
			}
		}

	}
	else
	{
		CopyBlock( x0, y0, x1, y1, Screen, x0, y0, Log ) ;
		ShadeBox( x0, y0, x1, y1, 4 ) ;
	}

	// cadre
	DrawCadre( x0, y0, x1, y1 ) ;

	// text

	CoulFont( COUL_TEXT_MENU ) ;
#ifndef	DOS
	/*
	 * FEATURE: a text numbered from 0x1000 is one of the Classic's own,
	 * the language page's, Fullscreen display's and Hit walls' among
	 * them. The language page's entries of types 9 and 10 name the
	 * texts' language and the voices', or None while the voices are
	 * off. Taken from the Classic 3.2.3.
	 *
	 * FIXED: the Classic names the languages by a table of its own,
	 * Spanish in English beside the others' own names. The page names
	 * them by TabLanguage, Español among them.
	 */
	if( num & 0x1000 )
	{
		if( type == 9 )
			snprintf( string, sizeof string, GetExtText( num ),
				TabLanguage[Language] ) ;
		else if( type == 10 )
			snprintf( string, sizeof string, GetExtText( num ),
				FlagSpeak ? TabLanguage[LanguageCD] : GetExtText( 0x1003 ) ) ;
		else	strcpy( string, GetExtText( num ) ) ;
	}
	else
#endif
	GetMultiText( num, string ) ;

	Font( x - SizeFont( string )/2, y-18, string ) ;

	// flip

	CopyBlockPhys( x0,y0, x1,y1 ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/

void	DrawGameMenu( UWORD *ptrmenu, WORD justone )
{
	WORD	n, y ;
	WORD	nb ;
	WORD	selected, type, num ;

	selected = *ptrmenu++ ;

	nb = *ptrmenu++ ;

	y = *ptrmenu++ ;

	ptrmenu++ ;	// dial num

	if( y == 0 )	// depuis le haut
	{
		y = HAUTEUR_STANDARD/2 + 10 ;
	}
	else		// centre sur y
	{
		y -= ( HAUTEUR_STANDARD*nb + (nb-1)*MENU_SPACE ) / 2 ;
	}

	for( n=0; n<nb; n++ )
	{
		type = *ptrmenu++ ;
		num = *ptrmenu++ ;

		if( justone )
		{
			if( n == selected )
			{
				DrawOneChoice( 320, y, type, num, TRUE ) ;
			}
		}
		else
		{
			DrawOneChoice( 320, y, type, num, (n==selected) ) ;
		}

		y += HAUTEUR_STANDARD + MENU_SPACE ;
	}

	if( nb == 0 )	idle() ;
}

/*──────────────────────────────────────────────────────────────────────────*/

void	SetVolumes(LONG Music, LONG Sample, LONG Line, LONG CD, LONG Master )
{
	/*
	 * FIXED: LBA.EXE compares where it means to assign, so that its wait
	 * for the next tick waits only while TimerRef is 0. The wait is
	 * dropped. Taken from LbaWin 0.8.1.
	 */
#ifndef	FIXES
	static	ULONG	lasttimer ;


	while (lasttimer == TimerRef ) ;
	lasttimer == TimerRef ;
#endif
	if (!MixMusic)
	{
		MaxVolume = Music ;
		VolumeMidi( 100 ) ;
	}

	MixerChangeVolume(	Sample,
				Music,
				CD,
				Line,
				Master ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/
#ifndef	DOS
/*
 * FEATURE: the entry Escape picks in a menu, the word after its entries,
 * 0xFFFF for none. Taken from the Classic 3.2.3.
 */
UWORD	GetEscMenuEntry( UWORD *ptrmenu )
{
	return ptrmenu[4+ptrmenu[1]*2] ;
}

/*──────────────────────────────────────────────────────────────────────────*/
#endif

WORD	DoGameMenu( UWORD *ptrmenu )
{
	WORD	flag = 1 ;
	WORD	nb , selected ;
	LONG	flagVol = 0 ;
	UWORD	*ptr ;
	ULONG	chrono ;
#ifndef	DOS
	UWORD	escentry = 0xFFFF ;
	WORD	flagesc = FALSE ;
#endif

	Load_HQR( PATH_RESSOURCE"ress.hqr", BufSpeak, RESS_INIT_PLASMA ) ;

	selected = ptrmenu[0] ;
	nb = ptrmenu[1] ;

#ifndef	DOS
	/*
	 * FEATURE: with SaveAnywhere, a question's menu takes the answer
	 * Escape picks from after its answers, and waits for the keys held
	 * at its start to be released. Taken from the Classic 3.2.3.
	 */
	if( FlagSaveAnywhere AND (ptrmenu == GameChoiceMenu) )
	{
		escentry = GetEscMenuEntry( ptrmenu ) ;
		while( Key OR Joy ) idle() ;
	}
#endif

	chrono = TimerRef ;

	while( TRUE )
	{

#ifdef	DEBUG_TOOLS
		CheckSavePcx() ;
#endif
/*	okazou
		if( Key == K_S )
		{
			Save( "f:\\projet\\lba\\datas\\initplas.ma", BufSpeak, 16*1024 ) ;
		}
*/

#ifndef	DOS
		/*
		 * FEATURE: Escape makes that answer the selection, drawn as
		 * chosen once the keys are released, then gives it. Taken from
		 * the Classic 3.2.3.
		 */
		if( (Key == K_ESC) AND (escentry != 0xFFFF) )
		{
			selected = escentry ;
			flag = 1 ;
			flagesc = TRUE ;
		}
#endif

		// rustine credits ...
		if( ptrmenu == GameMainMenu )
		{
#ifdef	DEMO
			if( (TimerRef - chrono > 50 * 60)
			OR ((Key == K_C) AND (Fire & F_SHIFT)) )
#else
			if( (TimerRef - chrono > 50 * (60 * 3 + 53) )
			OR ((Key == K_C) AND (Fire & F_SHIFT)) )
#endif
			{
				chrono = TimerRef ;
				return 9999 ;
			}
		}

		MyJoy = Joy ;

		if( MyJoy & J_DOWN )
		{
			selected++ ;
			if( selected == nb )	selected = 0 ;
			flag = 1 ;
		}

		if( MyJoy & J_UP )
		{
			selected-- ;
			if( selected < 0 )	selected = nb - 1 ;
			flag = 1 ;
		}

		switch( ptrmenu[4 + 0 + selected*2] ) // type
		{
			case 0:	// text normal
				break ;

			case 1: // volume music

				if ( flag ) flagVol = 3 ;

				if( (MyJoy & J_LEFT) )
				{
					MusicVolume -= 4 ;
					flagVol = 3 ;
				}
				if( (MyJoy & J_RIGHT) )
				{
					MusicVolume += 4 ;
					flagVol = 3 ;
				}
				break ;

			case 2: // volume sample

				if ( flag ) flagVol = 3 ;

				if( (MyJoy & J_LEFT) )
				{
					SampleVolume -= 4 ;
					flagVol = 3 ;
				}
				if( (MyJoy & J_RIGHT) )
				{
					SampleVolume += 4 ;
					flagVol = 3 ;
				}
				break ;

			case 3: // volume cd

				if ( flag ) flagVol = 4 ;

				if( (MyJoy & J_LEFT) )
				{
					CDVolume -= 4 ;
					flagVol = 4 ;
				}
				if( (MyJoy & J_RIGHT) )
				{
					CDVolume += 4 ;
					flagVol = 4 ;
				}
				break ;

			case 4: // volume Line

				if ( flag ) flagVol = 5 ;

				if( (MyJoy & J_LEFT) )
				{
					LineVolume -= 4 ;
					flagVol = 5 ;
				}
				if( (MyJoy & J_RIGHT) )
				{
					LineVolume += 4 ;
					flagVol = 5 ;
				}
				break ;

			case 5: // volume master

				if ( flag ) flagVol = 2 ;

				if( (MyJoy & J_LEFT) )
				{
					MasterVolume -= 4 ;
					flagVol = 2 ;
				}
				if( (MyJoy & J_RIGHT) )
				{
					MasterVolume += 4 ;
					flagVol = 2 ;
				}
				break ;
#ifndef	DOS

			/*
			 * FEATURE: the arrows step the language page's
			 * languages back or forward, the texts' or the voices',
			 * the fire given its start again in BufSpeak after the
			 * voices' step, where the test voice was read, then
			 * await the keys' release. Taken from the Classic 3.2.3.
			 */
			case 9: // langue des textes

				if( (MyJoy & J_LEFT) )
				{
					ChangeTextLanguage( Language-1 ) ;
					while( Joy OR Fire OR Key )
						DrawGameMenu( ptrmenu, TRUE ) ;
				}
				else if( (MyJoy & J_RIGHT) )
				{
					ChangeTextLanguage( Language+1 ) ;
					while( Joy OR Fire OR Key )
						DrawGameMenu( ptrmenu, TRUE ) ;
				}
				break ;

			case 10: // langue des voix

				if( (MyJoy & J_LEFT) OR (MyJoy & J_RIGHT) )
				{
					if( (MyJoy & J_LEFT) )
						ChangeVoiceLanguage( LanguageCD-1 ) ;
					else	ChangeVoiceLanguage( LanguageCD+1 ) ;
					Load_HQR( PATH_RESSOURCE"ress.hqr", BufSpeak, RESS_INIT_PLASMA ) ;
					/*
					 * FEATURE: the entries counted again, the
					 * voices giving or taking Display text. Taken
					 * from LBA2.
					 */
					nb = ptrmenu[1] ;
					while( Joy OR Fire OR Key )
						DrawGameMenu( ptrmenu, TRUE ) ;
				}
				break ;
#endif
		}

		if( flag == 1 )
		{
			ptrmenu[0] = selected ;
			DrawGameMenu( ptrmenu, FALSE ) ;
#ifndef	DOS
			/*
			 * FEATURE: with SaveAnywhere, the main menu shows the
			 * picture of current.lba, which Resume Game loads, above its
			 * entries. Taken from LBA2.
			 */
			if( FlagSaveAnywhere AND (ptrmenu == GameMainMenu) )
			{
				UBYTE	memogamepathname[_MAX_PATH] ;
				UBYTE	*ptr ;

				strcpy( memogamepathname, GamePathname ) ;
				strcpy( GamePathname, CURRENTSAVE_FILENAME ) ;

				ptr = LoadGameScreen() ;
				if( ptr )	DrawScreenSave( ptr, 240, 10 ) ;

				strcpy( GamePathname, memogamepathname ) ;
			}
#endif
			while( Joy OR Fire OR Key )
				DrawGameMenu( ptrmenu, TRUE ) ;
			flag = 0 ;
		}
		else
		{
//			Vsync() ;
			if( flagVol )
			{
				if( MusicVolume > 255 )	MusicVolume = 255 ;
				if( SampleVolume > 255 )SampleVolume = 255 ;
				if( LineVolume > 255 )	LineVolume = 255 ;
				if( CDVolume > 255 )	CDVolume = 255 ;
				if( MasterVolume > 255 )MasterVolume = 255 ;

				if( MusicVolume < 0 )	MusicVolume = 0 ;
				if( SampleVolume < 0 )	SampleVolume = 0 ;
				if( LineVolume < 0 )	LineVolume = 0 ;
				if( CDVolume < 0 )	CDVolume = 0 ;
				if( MasterVolume < 0 )	MasterVolume = 0 ;

				switch ( flagVol )
				{
					case 3:
					SetVolumes(MusicVolume, SampleVolume, 0, 0, MasterVolume ) ;
					break;
					case 4:
					SetVolumes(0, SampleVolume, 0, CDVolume, MasterVolume ) ;
					break;
					case 5:
					SetVolumes(0, SampleVolume, LineVolume, 0, MasterVolume ) ;
					break;
					default:
					SetVolumes(MusicVolume, SampleVolume, LineVolume, CDVolume, MasterVolume ) ;
				}
			}
			DrawGameMenu( ptrmenu, TRUE ) ;
			flagVol = 0 ;
			flag = 0 ;
		}

		if( Fire & F_RETURN
		OR  Fire & F_SPACE )
		{
			return ptrmenu[4 + 1 + selected*2] ; // num mess
		}

#ifndef	DOS
		if( flagesc )	return ptrmenu[4 + 1 + selected*2] ; // <ESC>

		/*
		 * FEATURE: with SaveAnywhere, Escape gives 1000 in every menu
		 * but a question's, which each menu takes as its way back, the
		 * main menu going on with the game in play, or selecting Quit
		 * with none. Taken from LBA2.
		 */
		if( FlagSaveAnywhere
		AND (ptrmenu != GameChoiceMenu)
		AND (Key == K_ESC) )
		{
			return 1000 ;
		}
#endif

//		if( Key == K_ESC )	break ;
	}
	return -1 ;
}

#ifndef	DOS
/*══════════════════════════════════════════════════════════════════════════*/
/*
 * FEATURE: the language page, the texts' language and the voices' chosen
 * apart and written to lba.cfg. Taken from the Classic 3.2.3.
 */
/*──────────────────────────────────────────────────────────────────────────*/

/*
 * ChangeTextLanguage is TLBA1C.exe 0x4136c0: the texts' language set to
 * n, wrapped over the five, bank 0 loaded in it, and the language page
 * drawn again.
 */
void	ChangeTextLanguage( LONG n )
{
	LONG	memoflagspeak ;

	if( n >= 5 )		Language = 0 ;
	else if( n < 0 )	Language = 4 ;
	else			Language = n ;

	/*
	 * FIXED: InitDial with the voices on runs InitSpeak, which stops the
	 * CD's music before it looks for the menus' voices, where the
	 * Classic's leaves it playing. The voices are held off around the
	 * bank, as the quit menu holds them.
	 */
	memoflagspeak = FlagSpeak ;
	FlagSpeak = FALSE ;
	InitDial( 0 ) ;
	FlagSpeak = memoflagspeak ;

	/*
	 * FEATURE: PleaseWait, the voices' transfer's title, read again in
	 * the language, where the Classic reads again its behaviours' names,
	 * which it shows in play. The port's own.
	 */
	GetMultiText( 49, PleaseWait ) ;

	DrawGameMenu( LanguageMenu, FALSE ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/

/*
 * ChangeVoiceLanguage is TLBA1C.exe 0x413710: the voices' language set to
 * n among the CD's three, None past either end, and from None the first
 * forward or the last back, then text 9 of bank 3 spoken as their test,
 * the timer held.
 */
void	ChangeVoiceLanguage( LONG n )
{
	LONG	step ;

	/*
	 * FIXED: the Classic turns the voices on whatever the sound, where
	 * InitLanguage keeps them off with WaveDriver: NoWave, so that a
	 * dialogue whose text is hidden under the voices would pass unseen.
	 * There the voices stay off.
	 */
	if( !Wave_Driver_Enable )	return ;

	SaveTimer() ;

	step = (n < LanguageCD) ? -1 : 1 ;
	LanguageCD = n ;

	if( !FlagSpeak )
	{
		FlagSpeak = TRUE ;
		LanguageCD = (n < 0) ? 2 : 0 ;
		step = (n < 0) ? -1 : 1 ;
	}

	/*
	 * FEATURE: the voices step through the languages whose voices the
	 * install holds, None past the last, so that the page offers the
	 * voices it can speak. The port's own.
	 */
	while( (LanguageCD >= 0) AND (LanguageCD <= 2)
	AND    !IsVoiceLanguage( LanguageCD ) )
		LanguageCD += step ;

	if( (LanguageCD < 0) OR (LanguageCD > 2) )
	{
		LanguageCD = 0 ;
		FlagSpeak = FALSE ;
		/*
		 * FIXED: the Classic's None leaves the voices' file open, and
		 * FlagVoiceCD says the CD still reads it, which keeps the tunes
		 * from the CD's tracks. The file is closed.
		 */
		CloseFdNar() ;
		FlagVoiceCD = FALSE ;
		/*
		 * FEATURE: Display text goes with the voices, the page drawn
		 * again on its three entries. Taken from LBA2.
		 */
		LanguageMenu[1] = 3 ;
		CopyScreen( Screen, Log ) ;
		Flip() ;
		DrawGameMenu( LanguageMenu, FALSE ) ;
		RestoreTimer() ;
		return ;
	}

	/*
	 * FEATURE: the test voice's bank loaded with the CD's music playing
	 * on, as the Classic's InitSpeak leaves the music, and the voice
	 * spoken from a copy of its block, the page going on under it, as
	 * the Classic's mixer plays it. Taken from the Classic 3.2.3.
	 */
	FlagVoiceTest = TRUE ;
	InitDial( 3 ) ;
	Speak( 9 ) ;
	FlagVoiceTest = FALSE ;

	/*
	 * FIXED: InitDial with the voices on runs InitSpeak, which stops the
	 * CD's music before it looks for the menus' voices, where the
	 * Classic's leaves it playing. The voices are held off around the
	 * bank, as the quit menu holds them.
	 */
	FlagSpeak = FALSE ;
	InitDial( 0 ) ;
	FlagSpeak = TRUE ;

	/*
	 * FIXED: a bank of voices copied from the CD draws LBA.EXE's
	 * transfer bar, which turns colour 15, the menus' white, to cyan and
	 * stays on the screen under the page, where the Classic reads its
	 * voices from the disk. The palette the menu shows is set again, the
	 * scene's own (FlagPalettePcx) under F6's options, the normal one
	 * under the main menu, which shows it over any scene, the background
	 * shown again and the page drawn over it, the fire given its start
	 * first in BufSpeak, where the voice was read.
	 */
	if( FlagPalettePcx AND (GameOptionMenu[5] == 15) )
		Palette( PalettePcx ) ;
	else	Palette( PtrPal ) ;
	Load_HQR( PATH_RESSOURCE"ress.hqr", BufSpeak, RESS_INIT_PLASMA ) ;
	CopyScreen( Screen, Log ) ;
	Flip() ;
	/*
	 * FEATURE: Display text on the page with the voices. Taken from
	 * LBA2.
	 */
	LanguageMenu[1] = 4 ;
	DrawGameMenu( LanguageMenu, FALSE ) ;

	RestoreTimer() ;
}

/*──────────────────────────────────────────────────────────────────────────*/

/*
 * LanguageOptions is TLBA1C.exe 0x413790: the language page, Return on an
 * entry stepping its language forward, the arrows either way in
 * DoGameMenu, and on leaving a changed choice written to lba.cfg.
 */
LONG	LanguageOptions()
{
	WORD	select ;
	WORD	flag = 0 ;
	LONG	memolanguage ;
	LONG	memolanguagecd ;
	LONG	memoflagspeak ;
	LONG	memoflagdisplaytext ;

	memolanguagecd = LanguageCD ;
	memolanguage = Language ;
	memoflagdisplaytext = FlagDisplayText ;

	CopyScreen( Log, Screen ) ;

	ReadLanguage() ;
	memoflagspeak = FlagSpeak ;

	/*
	 * FEATURE: Display text on the page while the voices speak, as
	 * LBA2's option menu shows it, the selection kept on the page.
	 * Taken from LBA2.
	 */
	LanguageMenu[1] = FlagSpeak ? 4 : 3 ;
	if( LanguageMenu[0] >= LanguageMenu[1] )
		LanguageMenu[0] = LanguageMenu[1]-1 ;

	while( !flag )
	{
		LanguageMenu[4+0+MENU_DISPLAY_TEXT*2+1] = (16+FlagDisplayText)|0x4000 ;

		select = DoGameMenu( LanguageMenu ) ;

		switch( select )	// num mess
		{
			case 26: // menu prec.
			/*
			 * FEATURE: with SaveAnywhere, Escape leaves as the first
			 * entry does. Taken from the LBA2 Classic 3.2.4.
			 */
			case 1000: // <ESC>
				flag = 1 ;
				break ;

			case 0x1001: // langue des textes
				ChangeTextLanguage( Language+1 ) ;
				break ;

			case 0x1002: // langue des voix
				ChangeVoiceLanguage( LanguageCD+1 ) ;
				break ;

			case 16|0x4000: // textes cachés
			case 17|0x4000: // textes affichés
				FlagDisplayText ^= 1 ;
				break ;
		}
	}

	/*
	 * FIXED: the voices turned off from English, or on to English, leave
	 * LanguageCD at 0, so that the Classic writes nothing and the next
	 * start gives the voices back as they were. Whether the voices speak
	 * is compared too.
	 */
	if( (Language != memolanguage)
	OR  (LanguageCD != memolanguagecd)
	OR  (FlagSpeak != memoflagspeak) )
	{
		WriteLanguage() ;
	}

	/*
	 * FEATURE: Display text written to lba.cfg when it changed, as LBA2's
	 * WriteConfigFile writes it. Taken from LBA2.
	 */
	if( FlagDisplayText != memoflagdisplaytext )
	{
		if( FlagDisplayText )	Def_WriteString( PathConfigFile, "FlagDisplayText", "ON" ) ;
		else			Def_WriteString( PathConfigFile, "FlagDisplayText", "OFF" ) ;
	}

	CopyScreen( Screen, Log ) ;
	Flip() ;

	return FALSE ;
}

/*──────────────────────────────────────────────────────────────────────────*/
#endif
/*══════════════════════════════════════════════════════════════════════════*/
/*══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

LONG	OptionsMenu()
{
	WORD select ;
	WORD flag = 0 ;
#ifndef	DOS
	UWORD *ptrfullscreen ;
	UWORD *ptrwide ;
	UWORD *ptrhitwalls ;
	WORD n ;
	LONG dx ;

	/*
	 * FEATURE: Fullscreen display, the option menu's switch of the
	 * screen between the whole display and a window. Its entry says ON
	 * while FlagFullScreen is set and OFF without, and its choice
	 * switches the screen and writes the flag to lba.cfg at once. Taken
	 * from the Classic 3.2.3. The entry is found by its text, as Hit
	 * walls' is below.
	 */
	ptrfullscreen = GameOptionMenu+4 ;
	while( (ptrfullscreen[1] != 0x1008) AND (ptrfullscreen[1] != 0x1009) )
	{
		ptrfullscreen += 2 ;
	}
	ptrfullscreen[1] = FlagFullScreen ? 0x1008 : 0x1009 ;

	/*
	 * FEATURE: Widescreen display, the option menu's switch of the wide
	 * screen. Its entry says ON while FlagWideScreen is set and OFF
	 * while it is clear, where InitProgram left it in the menu, and its
	 * choice moves the screen's width at once and writes the key to
	 * lba.cfg. The port's own.
	 */
	ptrwide = NULL ;
	for( n=0; n<GameOptionMenu[1]; n++ )
	{
		if( (GameOptionMenu[4+2*n+1] == 0x2000) OR (GameOptionMenu[4+2*n+1] == 0x2001) )
		{
			ptrwide = GameOptionMenu+4+2*n ;
			ptrwide[1] = FlagWideScreen ? 0x2000 : 0x2001 ;
		}
	}

	/*
	 * FEATURE: Hit walls, the option menu's switch of the bump into
	 * walls. Its entry says ON while LbaWinSettings has 2 and OFF
	 * without, and its choice turns 2 over and writes the settings to
	 * lba.cfg at once. Taken from the Classic 3.2.3. The entry is found
	 * by its text, the menu's entries before it being as lba.cfg sets
	 * them.
	 */
	ptrhitwalls = GameOptionMenu+4 ;
	while( (ptrhitwalls[1] != 0x100A) AND (ptrhitwalls[1] != 0x100B) )
	{
		ptrhitwalls += 2 ;
	}
	ptrhitwalls[1] = (LbaWinSettings & 2) ? 0x100A : 0x100B ;

#endif
	CopyScreen( Log, Screen ) ;

#ifdef	FIXES
	HQ_SaveSamples() ;	// les boucles du jeu gardées de côté
#else
	HQ_StopSample() ;
#endif
#ifdef	CDROM
	PlayCdTrack( 9 ) ;
#else
	PlayMidiFile( 9 ) ;
#endif

	while( !flag )
	{
		select = DoGameMenu( GameOptionMenu ) ;

		switch( select )	// num mess
		{
/*			case -1: // esc
				flag = 1 ;
				break ;
*/
			case 15: // retour au jeu
			case 26: // menu prec.
#ifndef	DOS
			/*
			 * FEATURE: with SaveAnywhere, Escape leaves as the first
			 * entry does. Taken from LBA2.
			 */
			case 1000: // <ESC>
#endif
				flag = 1 ;
				break ;

			case 47:
				CopyScreen( Screen, Log ) ;
				Flip() ;
				AdvancedOptions() ;
				break ;

			case 30:
				CopyScreen( Screen, Log ) ;
				Flip() ;
				VolumeOptions() ;
				break ;

			case 46:
				CopyScreen( Screen, Log ) ;
				Flip() ;
				SavedGameManagement() ;
				break ;
#ifndef	DOS

			case 0x1000:	// Choose language
				CopyScreen( Screen, Log ) ;
				Flip() ;
				LanguageOptions() ;
				break ;

			case 0x1008:	// Fullscreen display: ON
			case 0x1009:	// Fullscreen display: OFF
				WriteFullScreen() ;
				ptrfullscreen[1] = FlagFullScreen ? 0x1008 : 0x1009 ;
				break ;

			/*
			 * FEATURE: the screen's width moved and the scene's centre
			 * with the screen's middle, which keeps a script's
			 * projection, iso or 3D. Over a scene in play, F6's options
			 * (Return to game) or LBA2's menu over a game Escape left
			 * (EscTimer), the scene is drawn again at the new width under
			 * the menu, its origin and the numbers over a find moving
			 * with it, and over the main menu its picture is laid out
			 * again, black around it. The port's own.
			 */
			case 0x2000:	// Widescreen display: ON
			case 0x2001:	// Widescreen display: OFF
				dx = CENTRE_ECRAN_X ;
				writewide( Screen ) ;
				XCentre += CENTRE_ECRAN_X - dx ;
				ptrwide[1] = FlagWideScreen ? 0x2000 : 0x2001 ;
				if( (GameOptionMenu[5] == 15) OR EscTimer )
				{
					RedrawScene() ;
					CopyScreen( Log, Screen ) ;
				}
				else
				{
					CopyScreen( Screen, Log ) ;
					Flip() ;
				}
				break ;

			case 0x100A:	// Hit walls: ON
			case 0x100B:	// Hit walls: OFF
				LbaWinSettings ^= 2 ;
				/*
				 * FIXED: with keep original game play (1) set, as
				 * LbaWin's lba.cfg has it, ReadLbaWinSettings turns
				 * Hit walls on again at each start, so that the
				 * Classic's switch holds until the program ends. It
				 * clears 1 too.
				 */
				LbaWinSettings &= ~1 ;
				ptrhitwalls[1] = (LbaWinSettings & 2) ? 0x100A : 0x100B ;
				WriteLbaWinSettings() ;
				break ;
#endif
		}
	}
	CopyScreen( Screen, Log ) ;
	Flip() ;
	/*
	 * FIXED: LBA.EXE goes back to the game with the key that chose
	 * Return to game still down, which the next frame takes for
	 * Twinsen's action, or Return for the camera's recentring. This
	 * waits for its release, as the quit menu's Continue does.
	 */
#ifdef	FIXES
	if( select == 15 )	while( Key OR Fire ) idle() ;
#endif
#ifndef	DOS
	/*
	 * FEATURE: with SaveAnywhere, the menu left by Escape waits for the
	 * keys' release before the game reads them. Taken from LBA2.
	 */
	if( select == 1000 )	while( Key OR Fire OR Joy ) idle() ;
#endif

	return FALSE ;
}

/*══════════════════════════════════════════════════════════════════════════*/
/*══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

void	VolumeOptions()
{
	WORD select ;
	WORD flag = 0 ;

	CopyScreen( Log, Screen ) ;

#ifdef	CDROM
	PlayAllMusic( 9 ) ;	// he oui on lance les 2
#else
	PlayMidiFile( 9 ) ;
#endif

	TimerSample = TimerRef ;

	while( !flag )
	{
		select = DoGameMenu( GameVolumeMenu ) ;

		switch( select )	// num mess
		{
/*			case -1: // esc
				flag = 1 ;
				break ;
*/
			case 26: // quitter
#ifndef	DOS
			/*
			 * FEATURE: with SaveAnywhere, Escape leaves as the first
			 * entry does, the music and the volumes set as it sets
			 * them. Taken from LBA2.
			 */
			case 1000: // <ESC>
#endif
#ifdef	CDROM
				FadeMusicMidi( 1 ) ;
#endif
				SetVolumes(MusicVolume, SampleVolume, LineVolume, CDVolume, MasterVolume ) ;
				flag = 1 ;
				break ;

			case 16:
				SetVolumes(MusicVolume, SampleVolume, LineVolume, CDVolume, MasterVolume ) ;
				WriteVolumeSettings() ;
				GameVolumeMenu[0] = 0 ; // selected
				break ;
		}
	}
	CopyScreen( Screen, Log ) ;
	Flip() ;
}
/*──────────────────────────────────────────────────────────────────────────*/

void	SavedGameManagement()
{
	WORD	select ;
	WORD	flag = 0 ;
	UBYTE	memoplayername[MAX_SIZE_PLAYER_NAME] ;
	UBYTE	memogamepath[_MAX_PATH] ;

	CopyScreen( Log, Screen ) ;

	strcpy( memoplayername, PlayerName ) ;
	strcpy( memogamepath, GamePathname ) ;

	while( !flag )
	{
		select = DoGameMenu( SavedGameMenu ) ;

		switch( select )	// num mess
		{
			case 26: // quitter
				flag = 1 ;
				break ;

			case 41: // copier
#ifdef	DOS
				if( ChoosePlayerName( 41 ) )
#else
				if( ChoosePlayerName( 41, FALSE, FALSE ) )
#endif
				{
					UBYTE	*ptr,*ptrs ;
					LONG	size ;
					UBYTE	num ;
					UBYTE	n,c ;
					LONG	handle ;

					ptrs = ptr = LoadMalloc( GamePathname ) ;
					size = FileSize( GamePathname ) ;
					num = *ptr++ ;		// num version
					n = strlen( ptr ) + 1 ; // size player name

#ifdef	DOS
					if( InputPlayerName( 44 ) )
#else
					if( InputPlayerName( 44, PlayerName ) )
#endif
					{
						do
						{
							strcpy( GamePathname, PATH_RESSOURCE"S" ) ;
							strcat( GamePathname, Itoa( Rnd(10000) ) ) ;
							strcat( GamePathname, ".LBA" ) ;
						}
						while( FileSize( GamePathname ) != 0 ) ;

						handle = OpenWrite( GamePathname ) ;
						if( handle )
						{
							Write( handle, &num, 1 ) ;
							Write( handle, PlayerName, strlen(PlayerName)+1 ) ;
							Write( handle, ptrs+n+1, size-n-1 ) ;
							Close( handle ) ;
						}
					}
					Free( ptrs ) ;
				}
				CopyScreen( Screen, Log ) ;
				break ;

			case 45: // detruire
#ifdef	DOS
				if( ChoosePlayerName( 45 ) )
#else
				if( ChoosePlayerName( 45, FALSE, FALSE ) )
#endif
				{
					CopyScreen( Screen, Log ) ;

					DrawOneString( 320, 80, PlayerName, 2 ) ;

					SavedConfirmMenu[0] = 0 ;
					select = DoGameMenu( SavedConfirmMenu ) ;

					switch( select )	// num mess
					{
						case 48: // detruire
							Delete( GamePathname ) ;
							break ;
					}
				}
				SavedGameMenu[0] = 0 ;
				CopyScreen( Screen, Log ) ;
				Flip() ;
//				flag = 1 ;
				break ;
		}
	}
	CopyScreen( Screen, Log ) ;
	Flip() ;

	strcpy( GamePathname, memogamepath ) ;
	strcpy( PlayerName, memoplayername ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/

void	AdvancedOptions()
{
	WORD select ;
	WORD flag = 0 ;

	CopyScreen( Log, Screen ) ;

	while( !flag )
	{
		select = DoGameMenu( GameAdvancedMenu ) ;

		switch( select )	// num mess
		{
			case 26:
#ifndef	DOS
			/*
			 * FEATURE: with SaveAnywhere, Escape leaves as the first
			 * entry does. Taken from LBA2.
			 */
			case 1000: // <ESC>
#endif
				flag = 1 ;
				break ;

			case 4: // agressif auto
				GameAdvancedMenu[7] = 2 ;
				CombatAuto = FALSE ;
				break ;

			case 2: // agressif manuel
				GameAdvancedMenu[7] = 4 ;
				CombatAuto = TRUE ;
				break ;
			case 31: // poly high
				GameAdvancedMenu[9] = 131 ;
				SetFillDetails( 1 ) ;
				break ;
			case 131: // poly medium
				GameAdvancedMenu[9] = 231 ;
				SetFillDetails( 0 ) ;
				break ;
			case 231: // poly low
				GameAdvancedMenu[9] = 31 ;
				SetFillDetails( 2 ) ;
				break ;

			case 32: // ombres full
				GameAdvancedMenu[11] = 132 ;
				Shadow = 1 ;
				break ;
			case 132: // ombres obj
				GameAdvancedMenu[11] = 232 ;
				Shadow = 0 ;
				break ;
			case 232: // pas ombres
				GameAdvancedMenu[11] = 32 ;
				Shadow = 2 ;
				break ;

			case 33: // zoom hard
				GameAdvancedMenu[13] = 233 ;
				SceZoom = 0 ;
				break ;

			case 233: // pas zoom
				GameAdvancedMenu[13] = 33 ;
				SceZoom = 1 ;
				break ;

/*			case 233: // zoom soft
				GameAdvancedMenu[13] = 33 ;
				SceZoom = 2 ;
				break ;
*/
		}
	}
	CopyScreen( Screen, Log ) ;
	Flip() ;
}

#ifndef	DOS
/*══════════════════════════════════════════════════════════════════════════*/
/*
 * FEATURE: the main menu as LBA2 builds it from RealGameMainMenu,
 * firstloop TRUE with no game in play, 2 to select Quit: Resume Game
 * kept while current.lba holds a game, its entries then centred lower
 * under the game's picture, Load Game while a save is listed, Save Game
 * with a game in play that may be saved (SavingEnable). Taken from LBA2.
 *
 * FIXED: LBA2 keeps Resume Game while current.lba exists alone, which
 * Escape writes only while the game may be saved, and a game left by
 * Escape before any such save could not be gone on with. With a game in
 * play Resume Game is kept, the entries lowered under current.lba's
 * picture while it exists.
 */
void	BuildGameMainMenu( LONG firstloop )
{
	UWORD	*ptrreal = RealGameMainMenu ;
	UWORD	*ptrmain = GameMainMenu ;
	UWORD	nb = 0 ;
	UWORD	ycenter = 275 ;

	*ptrmain++ = *ptrreal++ ;	// selected
	*ptrmain++ = *ptrreal++ ;	// nb entries
	*ptrmain++ = *ptrreal++ ;	// y center
	*ptrmain++ = *ptrreal++ ;	// .dia num

	// reprendre partie

	if( !firstloop
	OR  FileSize( CURRENTSAVE_FILENAME ) )
	{
		*ptrmain++ = *ptrreal ;
		*ptrmain++ = *(ptrreal+1) ;
		nb++ ;
		if( FileSize( CURRENTSAVE_FILENAME ) )	ycenter = 335 ;
	}

	ptrreal += 2 ;	// nouvelle partie
	*ptrmain++ = *ptrreal++ ;
	*ptrmain++ = *ptrreal++ ;
	nb++ ;

	// charger partie

	if( IsExistSavedGame() )
	{
		*ptrmain++ = *ptrreal ;
		*ptrmain++ = *(ptrreal+1) ;
		nb++ ;
	}

	ptrreal += 2 ;	// sauver partie

	if( !firstloop
	AND SavingEnable )
	{
		*ptrmain++ = *ptrreal ;
		*ptrmain++ = *(ptrreal+1) ;
		nb++ ;
	}

	ptrreal += 2 ;	// Options

	*ptrmain++ = *ptrreal++ ;
	*ptrmain++ = *ptrreal++ ;
	nb++ ;

	// Quitter
	*ptrmain++ = *ptrreal++ ;
	*ptrmain++ = *ptrreal++ ;
	nb++ ;

	if( firstloop==2 ) GameMainMenu[0] = (UWORD)(nb-1) ;	// selected
	GameMainMenu[1] = nb ;		// nb entries
	GameMainMenu[2] = ycenter ;	// y center
}
#endif

/*══════════════════════════════════════════════════════════════════════════*/
/*══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

LONG	MainGameMenu()
{
	WORD select ;
	WORD flag = 0 ;

	HQ_StopSample() ;


/*	for( flag=0; flag<50*320; flag++ )
	{
		BufSpeak[flag] = 0 ;
	}
	flag = 0;
*/

	CopyScreen( Log, Screen ) ;

#ifndef	DOS
	/*
	 * FEATURE: with SaveAnywhere, LBA2's MainGameMenu, which runs the
	 * game (gogame) and comes back to the menu over it when the main
	 * loop ends: there Escape saves the game into current.lba while it
	 * may be saved (SavingEnable), the end plays the credits and its
	 * movie, and after a game over, the end or the demo no game is left
	 * in play (firstloop). Resume Game goes on with the game in play, or
	 * loads current.lba, New Game starts one at once, Load Game and
	 * Save Game open the list of saves, Quit ends the program,
	 * current.lba keeping the game. Escape on the menu goes on with the
	 * game, or selects Quit with none. Over a scene in a palette of its
	 * own (FlagPalettePcx) the menu takes the normal one, and the
	 * scene's comes back with the game. The actors' loops are set aside
	 * under the menu, as under the screens over the game, where LBA2
	 * fades every sample out and pauses it. Taken from LBA2.
	 */
	if( FlagSaveAnywhere )
	{
		LONG	firstloop = TRUE ;
		LONG	gogame = FALSE ;
		LONG	ret ;

		while( !flag )
		{
			if( gogame )
			{
				ret = MainLoop() ;

				switch( ret )
				{
					case 0: // <ESC>
						if( SavingEnable )	CurrentSaveGame() ;	// sauve dans current.lba
						break ;

					case 1: // Gagné !!!!
						Credits() ;
						PlayAnimFla( "The_End" ) ;
						Cls() ;
						Flip() ;
						Palette( PtrPal ) ;
						firstloop = TRUE ;
						EscTimer = 0 ;
						break ;

					case 2: // GameOver :(
						// Pour ne pouvoir ni reprendre la partie
						// ni la sauver
						firstloop = TRUE ;
						EscTimer = 0 ;
						break ;
				}

				CopyScreen( Log, Screen ) ;
				gogame = FALSE ;
			}

			BuildGameMainMenu( firstloop ) ;

			InitDial( 0 )		;//	SYS

			PlayCdTrack( 9 ) ;
			HQ_SaveSamples() ;	// les boucles du jeu mises de côté

			GetMultiText( 49, PleaseWait ) ;

			// la palette normale sous le menu, comme LBA2
			if( FlagPalettePcx )	Palette( PtrPal ) ;

			select = DoGameMenu( GameMainMenu ) ;
			switch( select )	// num mess
			{
				case 9999:	// menu demo
					/*
					 * FIXED: LBA2's demo enters the main loop with the
					 * EscTimer of the game Escape left, which puts that
					 * game's clock back and resumes its music in the
					 * demo. It is cleared, and the demo, which LBA1
					 * plays over the game in play, leaves none, as a
					 * game over does.
					 */
					EscTimer = 0 ;
					firstloop = TRUE ;

					MenuDemo() ;
					Load_HQR( PATH_RESSOURCE"ress.hqr", Screen, RESS_MENU_PCR ) ;
					windowpicture( Screen ) ;
					CopyScreen( Screen, Log ) ;
					SetBlackPal() ;
					Flip() ;
					FadeToPal( PtrPal ) ;
					break ;

				case 1000: // <ESC>, on reprend la partie en cours
					if( firstloop )
					{
						firstloop = 2 ;
						break ;
					}

				case 70|0x4000: // reprendre partie
					if( !firstloop )
					{
						InitDial( START_FILE_ISLAND+Island ) ;
						StopMusicCD() ;		// la musique du menu
						while( Key OR Fire OR Joy )	idle() ;
						if( FlagPalettePcx )	Palette( PalettePcx ) ;	// celle du jeu
						gogame = TRUE ;
					}
					else if( FileSize( CURRENTSAVE_FILENAME ) )
					{
						UBYTE	memoplayername[MAX_SIZE_PLAYER_NAME+1] ;
						UBYTE	memogamepathname[_MAX_PATH] ;

						strcpy( memoplayername, PlayerName ) ;
						strcpy( memogamepathname, GamePathname ) ;

						firstloop = FALSE ;
						strcpy( PlayerName, CURRENTSAVE_NAME ) ;
						strcpy( GamePathname, CURRENTSAVE_FILENAME ) ;

						ZoomSaveX = 240 ;
						ZoomSaveY = 10 ;
						InitGame( -1, 0 ) ;
						Introduction() ;

						strcpy( PlayerName, memoplayername ) ;
						strcpy( GamePathname, memogamepathname ) ;

						gogame = TRUE ;
						EscTimer = 0 ;
					}
					break ;

				case 71|0x4000: // newgame
					// AUTOSAVE, dans autosave.lba, jusqu'a ce qu'une
					// sauvegarde la nomme
					firstloop = FALSE ;
					strcpy( PlayerName, AUTOSAVE_NAME ) ;
					strcpy( GamePathname, AUTOSAVE_FILENAME ) ;
					InitGame( 1, 0 ) ;
					Introduction() ;
					gogame = TRUE ;
					EscTimer = 0 ;
					break ;

				case 72|0x4000: // load
					if( ChoosePlayerName( 41|0x4000, FALSE, FALSE ) )
					{
						firstloop = FALSE ;
						ZoomSaveX = 240 ;
						ZoomSaveY = 50 ;
						InitGame( -1, 0 ) ;
						Introduction() ;
						gogame = TRUE ;
						EscTimer = 0 ;
					}
					else
					{
						CopyScreen( Screen, Log ) ;
						Flip() ;
					}
					break ;

				case 73|0x4000: // Save Game
					if( !firstloop )
					{
						if( ChoosePlayerName( 42|0x4000, FALSE, TRUE ) == 2 )
						{
							if( !WriteSaveGame( TRUE ) )	SaveGame() ;
						}
						CopyScreen( Screen, Log ) ;
						Flip() ;
					}
					break ;

				case 74|0x4000: // Options
					CopyScreen( Screen, Log ) ;
					Flip() ;
					GameOptionMenu[5] = 26 ; // retour prec
					OptionsMenu() ;
					break ;

				case 75|0x4000: // quitter
					flag = 1 ;
					break ;
			}
		}
		return FALSE ;
	}
#endif

	while( !flag )
	{
		InitDial( 0 )		;//	SYS

#ifdef	CDROM
		PlayCdTrack( 9 ) ;
#else
		PlayMidiFile( 9 ) ;
#endif
		HQ_StopSample() ;

		GetMultiText( 49, PleaseWait ) ;

		select = DoGameMenu( GameMainMenu ) ;
		switch( select )	// num mess
		{
			case 9999:	// menu demo

				MenuDemo() ;
				Load_HQR( PATH_RESSOURCE"ress.hqr", Screen, RESS_MENU_PCR ) ;
				windowpicture( Screen ) ;
				CopyScreen( Screen, Log ) ;
				SetBlackPal() ;
				Flip() ;
				FadeToPal( PtrPal ) ;
				break ;

//			case -1: // esc
			case 22: // quitter
				flag = 1 ;
				break ;

			case 20: // newgame

#ifdef	DOS
				if( !InputPlayerName( 42 ) )	break ;
#else
				if( !InputPlayerName( 42, PlayerName ) )	break ;
#endif

				do
				{
					strcpy( GamePathname, PATH_RESSOURCE"S" ) ;
					strcat( GamePathname, Itoa( Rnd(10000) ) ) ;
					strcat( GamePathname, ".LBA" ) ;
				}
				while( FileSize( GamePathname ) != 0 ) ;

				InitGame( 1, 0 ) ;
				Introduction() ;
				if( MainLoop() )
				{
#ifdef	DEMO
					PlayMidiFile( 6 ) ;
					Credits() ;
					TheEnd( PROGRAM_OK, "* End of Demo version." ) ;
#else
					Credits() ;
					PlayAnimFla( "The_End" ) ;
					Cls() ;
					Flip() ;
					Palette( PtrPal ) ;
#endif
				}

				CopyScreen( Log, Screen ) ;
				while( Key OR Fire ) idle() ; // provisoire
				break ;

			case 21: // load

#ifdef	DOS
				if( !ChoosePlayerName( 21 ) ) break ;
#else
				if( !ChoosePlayerName( 21, FALSE, FALSE ) ) break ;
#endif

				InitGame( -1, 0 ) ;
				Introduction() ;
				if( MainLoop() )
				{
#ifdef	DEMO
					PlayMidiFile( 6 ) ;
					Credits() ;
					TheEnd( PROGRAM_OK, "* End of Demo version." ) ;
#else
					Credits() ;
					PlayAnimFla( "The_End" ) ;
					Cls() ;
					Flip() ;
					Palette( PtrPal ) ;
#endif
				}
				CopyScreen( Log, Screen ) ;
				while( Key OR Fire ) idle() ; // provisoire
				break ;

			case 23:
				CopyScreen( Screen, Log ) ;
				Flip() ;
				GameOptionMenu[5] = 26 ; // retour prec
				OptionsMenu() ;
				break ;
		}
	}
	return FALSE ;
}

/*══════════════════════════════════════════════════════════════════════════*/
/*══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

LONG	QuitMenu()
{
	WORD select ;
	LONG memoflagspeak ;

	CopyScreen( Log, Screen ) ;
#ifdef	FIXES
	HQ_SaveSamples() ;	// les boucles mises de côté
#else
	HQ_StopSample() ;
#endif

	while( TRUE )
	{
		memoflagspeak = FlagSpeak ;
		FlagSpeak = FALSE ;
		InitDial( 0 )		;//	SYS
		FlagSpeak = memoflagspeak ;

		select = DoGameMenu( GameQuitMenu ) ;

		InitDial( START_FILE_ISLAND+Island )	;//	SYS

		switch( select )	// num mess
		{
			case 28: // continue
				/*
				 * FIXED: LBA.EXE goes back to the game with the key
				 * that chose Continue still down, which the next
				 * frame takes for Twinsen's action. This waits for
				 * its release, as the inventory does. LbaWin waits
				 * for Key alone, which holds no fire key here.
				 */
#ifdef	FIXES
				while( Key OR Fire ) idle() ;
				HQ_RestoreSamples() ;	// et reprises
#endif
				return FALSE ;

			case 27: // abandonner
				return TRUE ;
		}
	}
}

/*══════════════════════════════════════════════════════════════════════════*
       █▀▀▀▀ █▀▀▀█ █▄ ▄█ █▀▀▀▀       █▀▀▀▀ █   █ █▀▀▀█  █    █▀▀▀▀ █▀▀▀▀
       ██ ▀█ ██▀▀█ ██▀ █ ██▀▀        ██    ██▀▀█ ██  █  ██   ██    ██▀▀
       ▀▀▀▀▀ ▀▀  ▀ ▀▀  ▀ ▀▀▀▀▀ ▀▀▀▀▀ ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀▀  ▀▀   ▀▀▀▀▀ ▀▀▀▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/
/*
UWORD	GameChoiceMenu[] = {
			0,	// selected
			7,	// nb entries
			0,	// y from top.
			0,	// .dia num

			0, 0,
			0, 1,
			0, 2,
			0, 3,
			0, 4,
			0, 5,
			0, 6	}	;
*/

/*──────────────────────────────────────────────────────────────────────────*/

void	GameAskChoice( WORD nummess )
{
	WORD n ;

	CopyScreen( Log, Screen ) ;

	GameChoiceMenu[0] = 0 ;
	GameChoiceMenu[1] = GameNbChoices ;
#ifndef	DOS
	/*
	 * FEATURE: with SaveAnywhere, Escape picks the last answer, the one
	 * that declines, and none in the four questions whose last answer
	 * says something: the prison's and the library's two on Principal
	 * Island, and Bab Lapi's in Funky Town. Taken from the Classic 3.2.3.
	 */
	if( FlagSaveAnywhere )
	{
		GameChoiceMenu[4+GameNbChoices*2] = GameNbChoices-1 ;

		switch( nummess )
		{
			case 7:		// Bab Lapi, à Funky Town
			case 40:
				/*
				 * FIXED: the Classic tests Bab Lapi's question on
				 * island 1, Principal Island, where scene 74 lies on
				 * island 7, so that there Escape takes the last
				 * answer, the Cloning center. It is tested on
				 * island 7.
				 */
				if( (Island == 7) AND (NumCube == 74) )
				{
					GameChoiceMenu[4+GameNbChoices*2] = 0xFFFF ;
				}
				break ;

			case 93:	// la prison
				if( (Island == 1) AND (NumCube == 11) )
				{
					GameChoiceMenu[4+GameNbChoices*2] = 0xFFFF ;
				}
				break ;

			case 132:	// la bibliothèque
			case 141:
				if( (Island == 1) AND (NumCube == 10) )
				{
					GameChoiceMenu[4+GameNbChoices*2] = 0xFFFF ;
				}
				break ;
		}
	}
#endif
	/*
	 * FIXED: LBA.EXE puts a menu's first box 35 from the top whatever
	 * its answers, so that the sixth box, from 290 to 340, covers the
	 * dialogue's window, from 334. A menu of six answers or more is
	 * centred on y 191, its sixth box ending at 331. Taken from LbaWin
	 * 0.7.6.
	 */
#ifdef	FIXES
	if( GameNbChoices >= 6 )	GameChoiceMenu[2] = 191 ;
	else				GameChoiceMenu[2] = 0 ;
#else
	GameChoiceMenu[2] = 0 ;
#endif
	GameChoiceMenu[3] = START_FILE_ISLAND+Island ;
	for(n=0; n<GameNbChoices; n++ )
	{
		GameChoiceMenu[4+n*2+0] = 0 ;			// type
		GameChoiceMenu[4+n*2+1] = GameListChoice[n] ;	// mess
	}

	MyDial( nummess ) ;
	DoGameMenu( GameChoiceMenu ) ;
	GameChoice = GameListChoice[GameChoiceMenu[0]] ; // ret num mess

#ifdef	CDROM
	NumObjSpeak = NUM_PERSO ;
	Speak( GameChoice ) ;
	while( TestSpeak() AND (Key!=K_ESC) )	idle() ;// Wait until silence
	StopSpeak() ;//	Security
#endif
}

/*══════════════════════════════════════════════════════════════════════════*
	     █▀▀▀▀ █▀▀▀█ █▄ ▄█ █▀▀▀▀       █▀▀▀█ █   █ █▀▀▀▀ █▀▀▀█
	     ██ ▀█ ██▀▀█ ██▀ █ ██▀▀        ██  █ ██ ▄▀ ██▀▀  ██▀█▀
	     ▀▀▀▀▀ ▀▀  ▀ ▀▀  ▀ ▀▀▀▀▀ ▀▀▀▀▀ ▀▀▀▀▀ ▀▀▀   ▀▀▀▀▀ ▀▀  ▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

void	GameOver()
{
	WORD	*ptr3do ;
	LONG	x0,y0,x1,y1 ;
	LONG	zoom ;
	LONG	alpha, beta ;
	ULONG	chrono ;

	TestRestoreModeSVGA( FALSE ) ;
	Palette( PtrPal ) ;

	CopyScreen( Log, Screen ) ;

	ptr3do = LoadMalloc_HQR( PATH_RESSOURCE"ress.hqr", RESS_GAME_OVER ) ;
	if( !ptr3do )	return ;
	PatchObjet( ptr3do ) ;

	HQ_StopSample() ;
	FadeMusicMidi( 1 ) ;

	// lance un sample ou music de game over ??

	SetProjection( 320,240, 128,200,200 ) ;

	alpha = 60 ;
	beta = 0 ;
	zoom = 50000 ;

	x0 = 320-200 ;
	y0 = 240-120 ;
	x1 = 320+200-1 ;
	y1 = 240+120-1 ;

	chrono= TimerRef ;

	SetClip(  x0, y0, x1, y1 ) ;
	while( (Key != K_ESC) AND (TimerRef-chrono<=10*50) )
	{
		zoom = BoundRegleTrois( 40000, 3200, 10*50, TimerRef-chrono ) ;
		beta = RegleTrois32( 1, 1024, 2*50, (TimerRef-chrono)%(2*50) ) ;

		CopyBlock( x0, y0, x1, y1, Screen, x0, y0, Log ) ;

		SetFollowCamera( 0,0,0, 0, -beta,0, zoom ) ;
		AffObjetIso(	0,0,0, 0,0,0, ptr3do ) ;

		CopyBlockPhys( x0,y0, x1,y1 ) ;
	}
	HQ_MixSample( 37, 0x1000+Rnd(2000)-(2000/2), 1, 128, 128 ) ;
	CopyBlock( x0, y0, x1, y1, Screen, x0, y0, Log ) ;
	SetFollowCamera( 0,0,0, 0,0,0, zoom ) ;
	AffObjetIso(	0,0,0, 0,0,0, ptr3do ) ;
	CopyBlockPhys( x0,y0, x1,y1 ) ;

	chrono = TimerRef + 50 * 3 ;
	while( (TimerRef < chrono) AND (Key!=K_ESC) ) idle() ;

	UnSetClip() ;
	Free( ptr3do ) ;

	CopyScreen( Screen, Log ) ;
	Flip() ;

	Init3DGame() ;
}

/*══════════════════════════════════════════════════════════════════════════*
		   █▀▀▀▀ █▀▀▀█ █▀▀▀▀ █▀▀▀▄  █    ▀▀█▀▀ ██▀▀▀
		   ██    ██▀█▀ ██▀▀  ██  █  ██     ██  ▀▀▀▀█
		   ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀▀ ▀▀▀▀   ▀▀     ▀▀  ▀▀▀▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

void	Credits()
{
	LONG	memoflagspeak ;
	LONG	memoshadow ;

	memoflagspeak = FlagSpeak ;
	memoshadow = Shadow ;

	FlagSpeak = FALSE ;
	FlagCredits = TRUE ;
	Shadow = 0 ;

	InitGame( 1, 0 ) ;

	NewCube = NumCube = 119 ;

	MainLoop() ;

	FlagCredits = FALSE ;
	FlagSpeak = memoflagspeak ;
	Shadow = memoshadow ;

	Cls() ;
	Flip() ;
}

/*──────────────────────────────────────────────────────────────────────────*/

void	RessPict( WORD num )
{
	SetBlackPal() ;
	Load_HQR( PATH_RESSOURCE"ress.hqr", Screen, num ) ;		// pcr
	windowpicture( Screen ) ;
	CopyScreen( Screen, Log ) ;
	Load_HQR( PATH_RESSOURCE"ress.hqr", PalettePcx, num+1 ) ;	// pcp
	Flip() ;
	FadeToPal( PalettePcx ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/

void	TimerPause( WORD nbsec )
{
	ULONG	memotimer ;

	memotimer = TimerRef ;
	while( TimerRef < (memotimer+50*nbsec) )
	{
		if( Key OR Fire OR Joy )	return ;
		idle() ;
	}
}

/*──────────────────────────────────────────────────────────────────────────*/

#ifdef	DOS
void	AdelineLogo()
#else
LONG	AdelineLogo()
#endif
{
#ifndef	DOS
	LONG	cut ;

#endif
	PlayMidiFile( 31 ) ;

	Load_HQR( PATH_RESSOURCE"ress.hqr", Screen, RESS_LOGO_PCR ) ;
	windowpicture( Screen ) ;
	CopyScreen( Screen, Log ) ;
	Load_HQR( PATH_RESSOURCE"ress.hqr", PalettePcx, RESS_LOGO_PAL ) ;
#ifdef	DOS
	WhiteFade() ;
	Flip() ;
	FadeWhiteToPal( PalettePcx ) ;
#else
	/*
	 * FEATURE: with SkipLogo a key that cuts the white fade passes the
	 * fade from white too, and the answer says that a key cut the logo,
	 * as AdelineLogo (TLBA1C.exe 0x414db0) answers. Taken from the
	 * Classic 3.2.3.
	 */
	cut = WhiteFade() ;
	Flip() ;
	if( cut )	return TRUE ;
	return FadeWhiteToPal( PalettePcx ) ;
#endif
}

/*──────────────────────────────────────────────────────────────────────────*/

void	MenuDemo()	// lancée toutes les 4 minutes
{
	Credits() ;
	if( Key OR Fire OR Joy )	return ;

	PlayAnimFla( "DRAGON3" ) ;
	if( Key OR Fire OR Joy )	return ;

	RessPict( RESS_TWINSUN_PCR ) ;
	TimerPause( 3 ) ;
	if( Key OR Fire OR Joy )	return ;
	FadeToBlack( PalettePcx ) ;

	RessPict( RESS_INTRO_2_PCR ) ;
	TimerPause( 3 ) ;
	if( Key OR Fire OR Joy )	return ;
	FadeToBlack( PalettePcx ) ;

	RessPict( RESS_INTRO_3_PCR ) ;
	TimerPause( 3 ) ;
	if( Key OR Fire OR Joy )	return ;
	FadeToBlack( PalettePcx ) ;

	PlayAnimFla( "BATEAU" ) ;
	if( Key OR Fire OR Joy )	return ;

	if( Version_US )
		RessPict( RESS_BUMPER_PCR ) ;
	else
		RessPict( RESS_BUMPER2_PCR ) ;

	TimerPause( 3 ) ;
	if( Key OR Fire OR Joy )	return ;
	FadeToBlack( PalettePcx ) ;

	AdelineLogo() ;
	TimerPause( 3 ) ;
	if( Key OR Fire OR Joy )	return ;
	FadeToBlack( PalettePcx ) ;
}

