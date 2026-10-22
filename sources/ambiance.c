
#include	"c_extern.h"
#include	"balance.h"

#ifdef	CDROM
extern	LONG	FlagVoiceCD ;
LONG	CurrentMusicCD = -1 ;
ULONG	EndMusicCD = 0 ;
#endif

/*══════════════════════════════════════════════════════════════════════════*
		█▀▀▀█ █▄ ▄█ █▀▀█   █    █▀▀▀█ ██▄ █ █▀▀▀▀ █▀▀▀▀
		██▀▀█ ██▀ █ ██▀▀█  ██   ██▀▀█ ██▀██ ██    ██▀▀
		▀▀  ▀ ▀▀  ▀ ▀▀▀▀▀  ▀▀   ▀▀  ▀ ▀▀  ▀ ▀▀▀▀▀ ▀▀▀▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

/*
 * FIXED: LBA.EXE leaves the loops an actor plays, a tank's engine, playing
 * after its death until the scene changes. The loops an actor's
 * animation or track starts are kept with the driver's long handle
 * (HQ_3D_MixSampleObj), and its death stops them (HQ_StopSamplesObj),
 * where LbaWin 0.7.6, DotEmu and the Classic stop every sample of an actor
 * whose life points run out: its death's one-shots play on.
 */
#ifdef	FIXES
#define	MAX_LOOP_OBJ	50	// LIST_SIZE, ce que le driver joue à la fois

typedef	struct	{	WORD	NumObj ;
			WORD	NumSample ;
			ULONG	LongHandle ;	}	T_LOOP_OBJ ;

T_LOOP_OBJ	ListLoopObj[MAX_LOOP_OBJ] ;
WORD		NbLoopObj = 0 ;
LONG		FlagLoopsKept ;		// les boucles mises de côté

/*──────────────────────────────────────────────────────────────────────────*/

void	DropLoopObj( WORD n )
{
	NbLoopObj-- ;
	ListLoopObj[n] = ListLoopObj[NbLoopObj] ;
}

/*──────────────────────────────────────────────────────────────────────────*/
#endif

ULONG	HQ_MixSample( WORD numsample, WORD decalage, WORD repeat, WORD volleft, WORD volright )
{
	UBYTE	string[256] ;
	ULONG	retvalue ;
	UBYTE	*ptr ;
#ifdef	FIXES
	WORD	n ;
#endif

	retvalue = -1 ;

	if( !SamplesEnable )	return -1	;

	if ( numsample == -1 )	return -1	;/* Loran ( Come from GereSceneMenu)*/

	ptr = HQR_GetSample( HQR_Samples, numsample ) ;
	if( ptr )
	{
		retvalue =
		WavePlay( numsample, decalage, repeat, 0, volleft, volright, ptr ) ;

#ifdef	FIXES
		// un handle rendu de nouveau: la boucle gardée sous lui est finie
		for( n=NbLoopObj-1; n>=0; n-- )
		{
			if( ListLoopObj[n].LongHandle == retvalue )	DropLoopObj( n ) ;
		}
#endif
	}

	return retvalue ;
}

#ifdef	FIXES
/*──────────────────────────────────────────────────────────────────────────*/
// une boucle gardée avec le long handle du driver

void	KeepLoopObj( WORD numobj, WORD numsample, ULONG longhandle )
{
	if( (longhandle != 0) AND (longhandle != (ULONG)-1)
	AND (NbLoopObj < MAX_LOOP_OBJ) )
	{
		ListLoopObj[NbLoopObj].NumObj = numobj ;
		ListLoopObj[NbLoopObj].NumSample = numsample ;
		ListLoopObj[NbLoopObj].LongHandle = longhandle ;
		NbLoopObj++ ;
	}
}
#endif

/*══════════════════════════════════════════════════════════════════════════*/
void	HQ_StopSample()
{
	WaveStop()	;
#ifdef	FIXES
	NbLoopObj = 0 ;
	FlagLoopsKept = FALSE ;
#endif
}

/*══════════════════════════════════════════════════════════════════════════*/
/*
 * FIXED: LBA.EXE stops every sample at the screens that open over the
 * game, the holomap, the quit menu, the options and Ctrl's menu in
 * flight, so that the loops an actor's script or animation started once,
 * the hum of scene 54's protopack in flight among them, stay silent
 * after them. The actors' loops are set aside in the driver's state and
 * played on after the screen, the other samples stopped, in the form of
 * LbaWin 0.8.1's holomap, which pauses its loops and plays them on.
 */
#ifdef	FIXES
// les boucles des objets mises de côté, les autres samples arrêtés

void	HQ_SaveSamples()
{
	T_WAVE	*ptrlist ;
	void	*snap ;
	LONG	n, k, nbsamp, nbloop ;

	if( FlagLoopsKept )	// dans un autre écran
	{
		WaveStop() ;
		return ;
	}

	nbsamp = WaveGetSnap( &snap ) ;
	ptrlist = snap ;

	nbloop = 0 ;
	for( n=0; n<nbsamp; n++ )
	{
		for( k=0; k<NbLoopObj; k++ )
		{
			if( ptrlist[n].LongHandle == ListLoopObj[k].LongHandle )
			{
				nbloop++ ;
				break ;
			}
		}
	}

	if( nbloop == 0 )
	{
		HQ_StopSample() ;
		return ;
	}

	for( n=0; n<nbsamp; n++ )
	{
		for( k=0; k<NbLoopObj; k++ )
		{
			if( ptrlist[n].LongHandle == ListLoopObj[k].LongHandle )
				break ;
		}
		if( k == NbLoopObj )	WaveStopOneLong( ptrlist[n].LongHandle ) ;
	}

	WaveSaveState() ;
	FlagLoopsKept = TRUE ;
}

/*══════════════════════════════════════════════════════════════════════════*/
// les boucles mises de côté reprises

void	HQ_RestoreSamples()
{
	if( FlagLoopsKept )
	{
		WaveRestoreState() ;
		FlagLoopsKept = FALSE ;
	}
}
#endif
/*══════════════════════════════════════════════════════════════════════════*/
void	HQ_StopOneSample(WORD num)
{
#ifdef	FIXES
	WORD	n ;

	WaveStopOne( num )	;

	for( n=NbLoopObj-1; n>=0; n-- )
	{
		if( ListLoopObj[n].NumSample == num )	DropLoopObj( n ) ;
	}
#else
	WaveStopOne( num )	;
#endif
}
#ifdef	FIXES
/*══════════════════════════════════════════════════════════════════════════*/
// les boucles de numobj, de numsample, ou toutes pour -1, arrêtées

void	HQ_StopSamplesObj( WORD numobj, WORD numsample )
{
	WORD	n ;

	for( n=NbLoopObj-1; n>=0; n-- )
	{
		if( (ListLoopObj[n].NumObj == numobj)
		AND ((numsample == -1) OR (ListLoopObj[n].NumSample == numsample)) )
		{
			WaveStopOneLong( ListLoopObj[n].LongHandle ) ;
			DropLoopObj( n ) ;
		}
	}
}
#endif
/*══════════════════════════════════════════════════════════════════════════*/
/*══════════════════════════════════════════════════════════════════════════*/

WORD	GiveBalance( WORD xp, WORD yp, LONG volume, ULONG *volleft, ULONG *volright )
{
	LONG	balance ;
	int	flag = 0 ;

	/*
	 * FEATURE: the balance over the whole screen, however wide and tall,
	 * faded out 240 above and below it and 320 beside it. The port's own.
	 */
	if( (yp > 240+HAUTEUR_ECRAN)
	OR  (yp < -240) )	return FALSE ;

	// baisse volume vers le haut à partir de Y<0 jusqu'a -480
	if( yp < 0 )
	{
		volume = RegleTrois32( 0, volume, 240, 240+yp ) ;
		flag = 1 ;
	}
	// baisse volume vers le bas à partir de Y>479  jusqu'a 480+480
	if( yp > HAUTEUR_ECRAN-1 )
	{
		volume = RegleTrois32( 0, volume, 240, 240+HAUTEUR_ECRAN-yp ) ;
		flag = 1 ;
	}

	// gere attenuation du volume sur le cote gauche
	// utilise le volume eventuellement deja ajuste sur Y
	if( (xp >= -320) AND (xp < 0) )
	{
		*volleft  = RegleTrois32( 0, (volume*100)/128, 320, xp+320 ) ;
//		*volleft  = RegleTrois32( 0, volume, 320, xp+320 ) ;
		*volright = 0 ;
		return TRUE ;
	}

	// gere attenuation du volume sur le cote droit
	if( (xp >= LARGEUR_ECRAN) AND (xp < LARGEUR_ECRAN+320) )
	{
		*volleft  = 0 ;
		*volright = RegleTrois32( 0, (volume*100)/128, 320, 320+LARGEUR_ECRAN-xp ) ;
//		*volright = RegleTrois32( 0, volume, 320, 320+640-xp ) ;
		return TRUE ;
	}

	// sinon gere la balance gauche/droite sur l'ecran
	// l'eventuelle attenuation du volume sur Y est toujour la
	if( (xp >= 0) AND (xp < LARGEUR_ECRAN) )
	{
		if (flag) volume = (volume*100)/128;

		balance = RegleTrois32( 0,256, LARGEUR_ECRAN, xp ) ;
		Balance(	balance, volume,
				volleft, volright  ) ;
		return TRUE ;
	}

	return FALSE ;
}

/*──────────────────────────────────────────────────────────────────────────*/

#ifdef	FIXES
ULONG	HQ_3D_MixSample(	WORD numsample, WORD decalage, WORD repeat,
#else
void	HQ_3D_MixSample(	WORD numsample, WORD decalage, WORD repeat,
#endif
				WORD x, WORD y, WORD z )
{
	ULONG	volleft, volright, longhandle ;
	LONG	balance ;

#ifdef	FIXES
	if( !SamplesEnable )	return -1 ;
#else
	if( !SamplesEnable )	return ;
#endif

	ProjettePoint(	x-WorldXCube,
			y-WorldYCube,
			z-WorldZCube ) ;

#ifdef	FIXES
	longhandle = -1 ;
#endif

	if( GiveBalance( Xp, Yp, 128, &volleft, &volright ) )
	{
		longhandle = HQ_MixSample(	numsample, decalage, repeat,
						volleft, volright ) ;

#ifdef	DOS
		WaveGiveInfo0( longhandle, (LONG)Xp<<16 + Yp ) ;
#else
		WaveGiveInfo0( longhandle, (ULONG)Xp<<((16 + Yp)&31) ) ;
#endif
	}

#ifdef	FIXES
	return longhandle ;
#endif
}

#ifdef	FIXES
/*──────────────────────────────────────────────────────────────────────────*/
// un sample d'un objet, gardé s'il boucle

void	HQ_3D_MixSampleObj(	WORD numobj, WORD numsample, WORD decalage,
				WORD repeat, WORD x, WORD y, WORD z )
{
	ULONG	longhandle ;

	longhandle = HQ_3D_MixSample( numsample, decalage, repeat, x, y, z ) ;

	if( repeat == 0 )	KeepLoopObj( numobj, numsample, longhandle ) ;
}
#endif

/*══════════════════════════════════════════════════════════════════════════*/
/*══════════════════════════════════════════════════════════════════════════*/

void	HQ_ChangeBalanceSamples( WORD oldxporg, WORD oldyporg )
{
	LONG	n, nbsamp ;
	T_WAVE	*ptrlist ;
	LONG	deltaxp, newxp ;
	LONG	deltayp, newyp ;
	ULONG	volleft, volright ;

  return ;
#ifdef	TANTPIS
	if( !SamplesEnable )	return ;

	nbsamp = WaveGetSnap( &ptrlist ) ;

	deltaxp = XpOrgw - oldxporg ;
	deltaxp = YpOrgw - oldyporg ;

	for( n=0; n<nbsamp; n++ )
	{
		if( ptrlist->LongHandle != 123456 )	// son ambiance
		{
			newxp = ptrlist->Info0>>16  - deltaxp ;
			newyp = ptrlist->Info0&0xFFFF - deltayp ;

			if( GiveBalance( newxp, newyp, 128, &volleft, &volright ) )
			{
				WaveChangeVolume( ptrlist->LongHandle, volleft, volright ) ;
				WaveGiveInfo0( ptrlist->LongHandle, newxp<<16 + newyp ) ;
			}
			else
			{
				// coupe ce sample longhandle plus tard
				WaveStopOne( ptrlist->LongHandle & 0xFFFF ) ;
			}
		}
		ptrlist++ ;
	}
#endif
}

/*══════════════════════════════════════════════════════════════════════════*/
/*══════════════════════════════════════════════════════════════════════════*/

void	GereAmbiance()
{
	LONG	sample ;
	LONG	numsample ;
	LONG	n ;
	LONG	decal ;
	LONG	repeat ;
	ULONG	longhandle ;

	if( !SamplesEnable )	return ;

	if( TimerRef >= TimerNextAmbiance )
	{
		sample = Rnd( 4 ) ;	/* 0 1 2 3 */

		for( n=0; n<4; n++ )
		{
			if( !(SamplePlayed & (1<<sample)) ) /* si pas joue */
			{
				SamplePlayed |= (1<<sample) ;	/* marque le joué */
				if( SamplePlayed == 15 ) /* tous joue */
					SamplePlayed = 0 ;

				numsample = SampleAmbiance[sample] ;
				if( numsample != -1 ) /* si defini */
				{
					decal = SampleRnd[sample] ;
					repeat = SampleRepeat[sample] ;

/*					if( repeat == 0 ) // infini
					{
						if( WaveInList( numsample ) )
						{

						}
					}
*/
					longhandle =HQ_MixSample(	numsample,
							0x1000+Rnd(decal)-(decal/2),
							repeat, 110, 110 ) ;

					// info son ambiance
//					WaveGiveInfo0( longhandle, 123456 ) ;
					break ;
				}
			}
			sample++ ;
			sample &= 3 ;

		}

		TimerNextAmbiance = TimerRef
					+
				( Rnd( SecondEcart ) + SecondMin ) * 50 ;
	}
}

/*══════════════════════════════════════════════════════════════════════════*
		   █▀▀▀█ █▀▀▀█ █     █▀▀▀▀ ▀▀█▀▀ ▀▀█▀▀ █▀▀▀▀
		   ██▀▀▀ ██▀▀█ ██    ██▀▀    ██    ██  ██▀▀
		   ▀▀    ▀▀  ▀ ▀▀▀▀▀ ▀▀▀▀▀   ▀▀    ▀▀  ▀▀▀▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

void	FadePal( UBYTE r, UBYTE v, UBYTE b, UBYTE *ptrpal, WORD percent )
{
	UBYTE	workpal[768] ;
	LONG	n ;

	for( n=0; n<256; n++ )
	{
		workpal[n*3+0] = RegleTrois32( r, ptrpal[n*3+0], 100, percent ) ;
		workpal[n*3+1] = RegleTrois32( v, ptrpal[n*3+1], 100, percent ) ;
		workpal[n*3+2] = RegleTrois32( b, ptrpal[n*3+2], 100, percent ) ;
	}
	Palette( workpal ) ;
}

void	FadeToBlack( UBYTE *ptrpal )
{
	LONG	n ;

	if( !FlagBlackPal )
	{
		/*
		 * FIXED: LBA.EXE lets the clock run through the fade, so that
		 * the world moves on at once by the fade's time after it. This
		 * holds the clock. Taken from LbaWin 0.8.0.
		 */
#ifdef	FIXES
		SaveTimer() ;
#endif
		for( n=100; n>=0; n-=2 )
		{
			Vsync() ;
			FadePal( 0,0,0, ptrpal, n ) ;
		}
#ifdef	FIXES
		RestoreTimer() ;
#endif
	}
	FlagBlackPal = TRUE ;
}

#ifdef	DOS
void	WhiteFade()
#else
LONG	WhiteFade()
#endif
{
	UBYTE	workpal[768] ;
	LONG	n,m ;

	/*
	 * FIXED: LBA.EXE keeps FlagBlackPal through this fade, which ends
	 * lit, so that after a fade to black the flag keeps saying black
	 * and the next FadeToBlack passes by, the screen cut to black. It
	 * is cleared. Taken from the Classic 3.2.3.
	 */
#ifdef	FIXES
	FlagBlackPal = FALSE ;
#endif
	for( n=0; n<=255; n++ )
	{
		memset( workpal, n, 768 ) ;
		Vsync() ;
		Palette( workpal ) ;
#ifndef	DOS
		/*
		 * FEATURE: with SkipLogo a key, Fire or Joy ends the white
		 * fade at its step, the palette as it stands, and the answer
		 * says that a key cut it, as WhiteFade (TLBA1C.exe 0x40b7e0)
		 * answers. Taken from the Classic 3.2.3.
		 */
		if( FlagSkipLogo AND (Key OR Fire OR Joy) )	return TRUE ;
#endif
	}
#ifndef	DOS
	return FALSE ;
#endif
}

#ifdef	DOS
void	FadeWhiteToPal( UBYTE *ptrpal )
#else
LONG	FadeWhiteToPal( UBYTE *ptrpal )
#endif
{
	LONG	n ;

	/*
	 * FIXED: LBA.EXE keeps FlagBlackPal through this fade, which ends
	 * lit, so that after a fade to black the flag keeps saying black
	 * and the next FadeToBlack passes by, the screen cut to black. It
	 * is cleared. Taken from the Classic 3.2.3.
	 */
#ifdef	FIXES
	FlagBlackPal = FALSE ;
	/*
	 * FIXED: LBA.EXE lets the clock run through the fade, so that the
	 * world moves on at once by the fade's time after it. This holds
	 * the clock. Taken from LbaWin 0.8.0.
	 */
	SaveTimer() ;
#endif
	for( n=0; n<=100; n+=1 )
	{
		Vsync() ;
		FadePal( 255,255,255, ptrpal, n ) ;
#ifndef	DOS
		/*
		 * FEATURE: with SkipLogo a key, Fire or Joy ends the fade at
		 * its step on the palette it fades to, and the answer says
		 * that a key cut it, as FadeWhiteToPal (TLBA1C.exe 0x40b8e0)
		 * gives FadePalTime its key. Taken from the Classic 3.2.3.
		 */
		if( FlagSkipLogo AND (Key OR Fire OR Joy) )
		{
			Palette( ptrpal ) ;
			RestoreTimer() ;
			return TRUE ;
		}
#endif
	}
#ifdef	FIXES
	RestoreTimer() ;
#endif
#ifndef	DOS
	return FALSE ;
#endif
}

void	FadeToPal( UBYTE *ptrpal )
{
	LONG	n ;

	/*
	 * FIXED: LBA.EXE lets the clock run through the fade, so that the
	 * world moves on at once by the fade's time after it. This holds
	 * the clock. Taken from LbaWin 0.8.0.
	 */
#ifdef	FIXES
	SaveTimer() ;
#endif
	for( n=0; n<=100; n+=2 )
	{
		Vsync() ;
		FadePal( 0,0,0, ptrpal, n ) ;
	}
#ifdef	FIXES
	RestoreTimer() ;
#endif
	FlagBlackPal = FALSE ;
}

void	SetBlackPal()
{
	LONG	n ;

	for( n=0; n<256; n++ )
	{
		PalOne( n, 0,0,0 ) ;
	}
	FlagBlackPal = TRUE ;
}

void	FadePalToPal( UBYTE *ptrpal, UBYTE *ptrpal1 )
{
	UBYTE	workpal[768] ;
	LONG	n,m ;

	for( m=0; m<=100; m++ )
	{
		for( n=0; n<256; n++ )
		{
			workpal[n*3+0] = RegleTrois32( ptrpal[n*3+0], ptrpal1[n*3+0], 100, m ) ;
			workpal[n*3+1] = RegleTrois32( ptrpal[n*3+1], ptrpal1[n*3+1], 100, m ) ;
			workpal[n*3+2] = RegleTrois32( ptrpal[n*3+2], ptrpal1[n*3+2], 100, m ) ;
		}
		Vsync() ;
		Palette( workpal ) ;
	}
	/*
	 * FIXED: LBA.EXE keeps FlagBlackPal through this fade, which ends
	 * lit, so that after a fade to black the flag keeps saying black
	 * and the next FadeToBlack passes by, the screen cut to black. It
	 * is cleared. Taken from the Classic 3.2.3.
	 */
#ifdef	FIXES
	FlagBlackPal = FALSE ;
#endif
}

void	FadeToRed( UBYTE *ptrpal )
{
	LONG	n ;

	/*
	 * FIXED: LBA.EXE lets the clock run through the fade, so that the
	 * world moves on at once by the fade's time after it. This holds
	 * the clock. Taken from LbaWin 0.8.0.
	 */
#ifdef	FIXES
	SaveTimer() ;
#endif
	for( n=100; n>=0; n-=2 )
	{
		Vsync() ;
		FadePal( 255,0,0, ptrpal, n ) ;
	}
#ifdef	FIXES
	RestoreTimer() ;
	/*
	 * FIXED: LBA.EXE keeps FlagBlackPal through this fade, which ends
	 * lit, so that after a fade to black the flag keeps saying black
	 * and the next FadeToBlack passes by, the screen cut to black. It
	 * is cleared. Taken from the Classic 3.2.3.
	 */
	FlagBlackPal = FALSE ;
#endif
}

void	FadeRedToPal( UBYTE *ptrpal )
{
	LONG	n ;

	/*
	 * FIXED: LBA.EXE lets the clock run through the fade, so that the
	 * world moves on at once by the fade's time after it. This holds
	 * the clock. Taken from LbaWin 0.8.0.
	 */
#ifdef	FIXES
	SaveTimer() ;
#endif
	for( n=0; n<=100; n+=2 )
	{
		Vsync() ;
		FadePal( 255,0,0, ptrpal, n ) ;
	}
#ifdef	FIXES
	RestoreTimer() ;
	/*
	 * FIXED: LBA.EXE keeps FlagBlackPal through this fade, which ends
	 * lit, so that after a fade to black the flag keeps saying black
	 * and the next FadeToBlack passes by, the screen cut to black. It
	 * is cleared. Taken from the Classic 3.2.3.
	 */
	FlagBlackPal = FALSE ;
#endif
}

/*══════════════════════════════════════════════════════════════════════════*
			    █▄ ▄█  █    █▀▀▀▄  █
			    ██▀ █  ██   ██  █  ██
			    ▀▀  ▀  ▀▀   ▀▀▀▀   ▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

#ifndef	DOS
/*
 * FEATURE: with SaveAnywhere, the tune a new scene holds for the save
 * of its first frame (RestartMusic) starts here, after the save, as
 * LBA2's main loop starts the scene's tune after that frame. Taken from
 * LBA2.
 */
void	RestartCubeMusic( void )
{
	if( !RestartMusic )	return ;

	RestartMusic = FALSE ;
	PlayMusic( CubeJingle ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/
#endif

#ifdef	CDROM

void	StopMusicCD( void )
{
#ifndef	DOS
	/*
	 * FEATURE: with SaveAnywhere, a tune the scene holds for its save
	 * starts first, as LBA.EXE had it playing by then. The port's own.
	 */
	RestartCubeMusic() ;
#endif
	StopCDR() ;
	CurrentMusicCD = -1 ;
}

#endif

/*──────────────────────────────────────────────────────────────────────────*/

void	FadeMusicMidi( ULONG t )
{
#ifndef	DOS
	/*
	 * FEATURE: with SaveAnywhere, a tune the scene holds for its save
	 * starts first, as LBA.EXE had it playing by then. The port's own.
	 */
	RestartCubeMusic() ;
#endif
	FadeMidiDown( t ) ;
	NumXmi = -1 ;
}

/*──────────────────────────────────────────────────────────────────────────*/

#ifndef	DOS
static	WORD	PausedXmi = -1 ;	// le XMIDI en pause
static	LONG	StoppedXmi ;		// et arrêté depuis, par l'air d'un menu
#endif

void	StopMusicMidi()
{
#ifndef	DOS
	/*
	 * FEATURE: with SaveAnywhere, a tune the scene holds for its save
	 * starts first, as LBA.EXE had it playing by then. The port's own.
	 */
	RestartCubeMusic() ;
	if( PausedXmi != -1 )	StoppedXmi = TRUE ;
#endif
	StopMidi() ;
	NumXmi = -1 ;
}

/*──────────────────────────────────────────────────────────────────────────*/

void	PlayMusic( WORD num )
{
	if( num == -1 )
	{
#ifdef	CDROM
		StopMusicCD() ;
#endif
		StopMusicMidi() ;
		return ;
	}

#ifdef	CDROM

	if( FlagVoiceCD		// voix sur CD music fm
	OR (num < 1)
	OR (num > 9) )		// ou jingle que FM
	{
		PlayMidiFile( num ) ;
	}
	else			// voix sur HD ponheur
	{
		PlayCdTrack( num ) ; // 1ere track = 2
	}

#else
	PlayMidiFile( num ) ;
#endif
}

/*──────────────────────────────────────────────────────────────────────────*/

void	PlayMidiFile( WORD num )
{
#ifdef	DOS
	if( !Midi_Driver_Enable )	return ;// hum si on peut
#else
	char	*track ;
	LONG	cut ;

	/*
	 * FEATURE: with SaveAnywhere, a tune the scene holds for its save
	 * starts first, as LBA.EXE had it playing by then. The port's own.
	 */
	RestartCubeMusic() ;
	/*
	 * FEATURE: with MusicJingles: PlayStation, the PlayStation's
	 * recording of the tune, its track on the CD (GetPsTrackFile), plays
	 * in place of the XMIDI when the CD holds it, at the CD's volume,
	 * with or without a MIDI driver, as the CD's tracks play. Taken from
	 * the PlayStation.
	 */
	track = NULL ;
	if( MusicJingles == JINGLES_PLAYSTATION )
	{
		track = GetPsTrackFile( num ) ;
		if( (track != NULL) AND (!pstracklength( track )) )	track = NULL ;
	}
	/*
	 * FEATURE: with MusicJingles: CD, the tune's cut from its track of
	 * the CD, as DotEmu cut it for its recording (cutlength), plays so
	 * when the CD holds the track. Taken from DotEmu 1.06.
	 */
	cut = FALSE ;
	if( MusicJingles == JINGLES_CD )	cut = cutlength( num ) != 0 ;
	if( !Midi_Driver_Enable AND (track == NULL) AND !cut )	return ;
#endif

#ifdef	CDROM
	StopMusicCD() ;
#endif

	if( (num != NumXmi)
	OR  (!IsMidiPlaying()) )
	{
		StopMusicMidi() ;
//		HQR_Reset_Ressource( HQR_Midi ) ;
#ifndef	DOS
		if( track != NULL )
		{
			NumXmi = num ;
			pstrackplay( track ) ;
			VolumeMidi( 100 ) ;
			return ;
		}
		if( cut )
		{
			NumXmi = num ;
			cutplay( num ) ;
			VolumeMidi( 100 ) ;
			return ;
		}
#endif
		PtrXmi = HQR_Get( HQR_Midi, num ) ;
		NumXmi = num ;
		PlayMidi( PtrXmi ) ;
		VolumeMidi( 100 ) ;
	}
}

/*──────────────────────────────────────────────────────────────────────────*/

#ifdef	CDROM
LONG	GetMusicCD()
{
	if ( TimerSystem > EndMusicCD)	CurrentMusicCD = -1 ;
	return CurrentMusicCD ;
}
/*──────────────────────────────────────────────────────────────────────────*/

void	PlayCdTrack( WORD num )
{
	LONG	t ;

#ifndef	DOS
	/*
	 * FEATURE: with SaveAnywhere, a tune the scene holds for its save
	 * starts first, as LBA.EXE had it playing by then. The port's own.
	 */
	RestartCubeMusic() ;
	/*
	 * FEATURE: with MusicTracks: Midi, a tune asked of the CD plays from
	 * the XMIDI, as the disk version, LBA.EXE's source built without
	 * CDROM, plays the scenes' and the menus' tunes. Taken from the disk
	 * version.
	 */
	if( MusicTracks == TRACKS_MIDI )
	{
		PlayMidiFile( num ) ;
		return ;
	}
#endif
	SaveTimer() ;

	FadeMusicMidi( 1 ) ;
	NumXmi = -1 ;

	if (num != GetMusicCD())
	{
		StopMusicCD() ;
		EndMusicCD = ( GetLengthTrackCDR( num+1 ) * 50 ) / 75 + 50 ;
		PlayTrackCDR( num + 1 ) ;
		EndMusicCD += TimerSystem ;
		CurrentMusicCD = num ;
	}

	RestoreTimer() ;
}

/*──────────────────────────────────────────────────────────────────────────*/

void	PlayAllMusic( WORD num )
{
#ifndef	DOS
	/*
	 * FEATURE: with MusicTracks: Midi, the volumes' tune plays from the
	 * XMIDI alone, as in the disk version. Taken from the disk version.
	 */
	if( MusicTracks == TRACKS_MIDI )
	{
		PlayMidiFile( num ) ;
		return ;
	}
	/*
	 * FEATURE: the PlayStation's track playing in the XMIDI's place
	 * stops for the tune of the volumes, as the XMIDI does, with a MIDI
	 * driver or without. Taken from the PlayStation.
	 */
	if( pstrackplaying() )	StopMusicMidi() ;
#endif
	if( Midi_Driver_Enable )
	{
		if( (num != NumXmi)
		OR  (!IsMidiPlaying()) )
		{
			StopMusicMidi() ;
			PtrXmi = HQR_Get( HQR_Midi, num ) ;
			NumXmi = num ;
			PlayMidi( PtrXmi ) ;
			VolumeMidi( 100 ) ;
		}
	}

//	if( (n=num+1) != GetMusicCDR() )
	if( num != GetMusicCD() )
	{
		StopMusicCD() ;
		EndMusicCD = ( GetLengthTrackCDR( num+1 ) * 50 ) / 75 + 50 ;
		PlayTrackCDR( num+1 ) ;
		EndMusicCD += TimerSystem ;
		CurrentMusicCD = num ;
	}
}

/*──────────────────────────────────────────────────────────────────────────*/

#endif

#ifndef	DOS
/*──────────────────────────────────────────────────────────────────────────*/
/*
 * FEATURE: the game's music paused under the main menu in play and
 * resumed where it stood when the game goes on (MainLoop, EscTimer): the
 * CD's track at its frame (PauseCD) and the XMIDI (PauseMidi), as LBA2's
 * MUSIC.CPP pauses its CD and its stream. LBA1 times the CD's track by
 * TimerSystem (CurrentMusicCD, EndMusicCD), carried across the pause,
 * and the menu's tune fades the XMIDI, whose number and volume come
 * back. Taken from LBA2.
 */

static	LONG	PausedMusicCD = -1 ;	// LBA1: la track en pause
static	ULONG	PausedTimeCD ;		// et ce qu'il lui restait

void	PauseMusic( void )
{
	SaveTimer() ;

#ifdef	CDROM
	PausedMusicCD = GetMusicCD() ;
	PausedTimeCD = EndMusicCD - TimerSystem ;
	PauseCD() ;
	CurrentMusicCD = -1 ;
#endif
	PausedXmi = NumXmi ;
	StoppedXmi = FALSE ;
	PauseMidi() ;

	RestoreTimer() ;
}

/*──────────────────────────────────────────────────────────────────────────*/

void	ResumeMusic( void )
{
	SaveTimer() ;

#ifdef	CDROM
	StopMusicCD() ;		// la musique du menu

	ResumeCD() ;
	if( PausedMusicCD != -1 )
	{
		CurrentMusicCD = PausedMusicCD ;
		EndMusicCD = TimerSystem + PausedTimeCD ;
	}
	PausedMusicCD = -1 ;
#endif
	ResumeMidi() ;
	if( PausedXmi != -1 )
	{
		/*
		 * FEATURE: where a menu's tune took the XMIDI from the game's
		 * tune (MusicTracks: Midi, or the volumes' page), that tune plays
		 * again from its start, as the PlayStation's pause menu asks for
		 * the scene's tune again on its exit. Taken from the PlayStation.
		 */
		if( StoppedXmi )
		{
			PlayMidiFile( PausedXmi ) ;
		}
		else
		{
			NumXmi = PausedXmi ;
			VolumeMidi( 100 ) ;
		}
	}
	PausedXmi = -1 ;

	RestoreTimer() ;
}
#endif
