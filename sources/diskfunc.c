#include	"c_extern.h"

UBYTE	*PtrSce ;
UBYTE	*PtrScene ;

#define	GET_BYTE	(*PtrSce++)
#define	GET_WORD	PeekWord( PtrSce ) ; PtrSce+=2

/*══════════════════════════════════════════════════════════════════════════*
	     █▀▀▀▄  █    ██▀▀▀ █  ▄▀       █▀▀▀▀ █   █ ██▄ █ █▀▀▀▀
	     ██  █  ██   ▀▀▀▀█ ██▀▄        ██▀▀  ██  █ ██▀██ ██
	     ▀▀▀▀   ▀▀   ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀▀ ▀▀    ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

void	GetDiskEnv( UBYTE *progpath )
{
	_splitpath( (char*)progpath, ProgDrive, ProgDir, Name, Ext ) ;
	getcwd( OrgDir, _MAX_DIR ) ;
	_dos_getdrive( &OrgDrive ) ;
}

void	RestoreDiskEnv()
{
	ULONG	total ;

	_dos_setdrive( OrgDrive, &total ) ;
	chdir( OrgDir ) ;
}

/*══════════════════════════════════════════════════════════════════════════*
	  ██▀▀▀ █▀▀▀▀ █▀▀▀▀ ██▄ █ █▀▀▀▀       █▀▀▀▀  █    █     █▀▀▀▀
	  ▀▀▀▀█ ██    ██▀▀  ██▀██ ██▀▀        ██▀▀   ██   ██    ██▀▀
	  ▀▀▀▀▀ ▀▀▀▀▀ ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀▀ ▀▀▀▀▀ ▀▀     ▀▀   ▀▀▀▀▀ ▀▀▀▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

WORD	LoadScene( WORD numscene )
{
	T_OBJET	*ptrobj ;
	WORD	n, info3 ;
	UBYTE	string[256] ;
	UBYTE	mess[256] ;
	WORD	sizetoload ;
	WORD	indexfile3d ;

//	PtrScene = PtrSce = LoadMalloc_HQR( PATH_RESSOURCE"scene.hqr", numscene ) ;

	HQRM_Load( PATH_RESSOURCE"scene.hqr", numscene, &PtrScene ) ;
	CHECK_MEMORY

	PtrSce = PtrScene ;

	if( !PtrSce )
	{
		Message( "Scene.Hqr introuvable ou pas assez mem", TRUE ) ;
	}

// info world: INFO_WORLD

		NewCube = numscene ;

		Island = GET_BYTE ;

		GameOverCube = GET_BYTE ;

		n = GET_WORD ;
		n = GET_WORD ;

//		ListHoloObj[NUM_PERSO].Alpha = GET_WORD ;
//		ListHoloObj[NUM_PERSO].Beta = GET_WORD ;
//		ListHoloObj[NUM_PERSO].Size = GET_WORD ;

// ambiance: AMBIANCE

		AlphaLight = GET_WORD ;
		BetaLight = GET_WORD ;

		SampleAmbiance[0] = GET_WORD ;
		SampleRepeat[0] = GET_WORD ;
		SampleRnd[0] = GET_WORD ;
		SampleAmbiance[1] = GET_WORD ;
		SampleRepeat[1] = GET_WORD ;
		SampleRnd[1] = GET_WORD ;
		SampleAmbiance[2] = GET_WORD ;
		SampleRepeat[2] = GET_WORD ;
		SampleRnd[2] = GET_WORD ;
		SampleAmbiance[3] = GET_WORD ;
		SampleRepeat[3] = GET_WORD ;
		SampleRnd[3] = GET_WORD ;
		SecondMin = GET_WORD ;
		SecondEcart = GET_WORD ;

		CubeJingle = GET_BYTE ;
//		PlayMidiFile( CubeJingle ) ;

// hero inits: HERO_START

		ptrobj = ListObjet ;

		CubeStartX = GET_WORD ;
		CubeStartY = GET_WORD ;
		CubeStartZ = GET_WORD ;

		sizetoload = GET_WORD ;
		ptrobj->PtrTrack = PtrSce ;
		PtrSce += sizetoload ;

		sizetoload = GET_WORD ;
		ptrobj->PtrLife = PtrSce ;
		PtrSce += sizetoload ;

		ptrobj++ ;

// objets: OBJECT

		NbObjets = GET_WORD ;
		for( n=1; n<NbObjets; n++, ptrobj++ )
		{
			InitObject( n ) ;

			ptrobj->Flags = GET_WORD ;

			indexfile3d = GET_WORD ;

			if( !(ptrobj->Flags & SPRITE_3D) )
			{

	HQRM_Load( PATH_RESSOURCE"File3D.hqr", indexfile3d, &ptrobj->PtrFile3D ) ;
	CHECK_MEMORY

/*				ptrobj->PtrFile3D =
					LoadMalloc_HQR(
						PATH_RESSOURCE"File3D.hqr",
						indexfile3d ) ;
*/

			}

			ptrobj->GenBody = GET_BYTE ;
			ptrobj->GenAnim = GET_BYTE ;
/*
if( n==5 )
{
	CoulText( 15,0 ) ;
	Text( 10, 300, "obj 5 org anim: %d", ptrobj->GenAnim ) ;
}
*/

			ptrobj->Sprite = GET_WORD ;

			ptrobj->OldPosX = ptrobj->PosObjX = GET_WORD ;
			ptrobj->OldPosY = ptrobj->PosObjY = GET_WORD ;
			ptrobj->OldPosZ = ptrobj->PosObjZ = GET_WORD ;
			ptrobj->HitForce = GET_BYTE ;
			ptrobj->OptionFlags = GET_WORD ;
			ptrobj->OptionFlags &= ~EXTRA_GIVE_NOTHING ;
			ptrobj->Beta = GET_WORD ;
			ptrobj->SRot = GET_WORD ;
			ptrobj->Move = GET_WORD ;

			ptrobj->Info  = GET_WORD ;
			ptrobj->Info1 = GET_WORD ;
			ptrobj->Info2 = GET_WORD ;
			ptrobj->Info3 = GET_WORD ;

			ptrobj->NbBonus = GET_BYTE ;
			ptrobj->CoulObj = GET_BYTE ;
			ptrobj->Armure = GET_BYTE ;
			ptrobj->LifePoint = GET_BYTE ;

			sizetoload = GET_WORD ;
			ptrobj->PtrTrack = PtrSce ;
			PtrSce = PtrSce + sizetoload ;

			sizetoload = GET_WORD ;
			ptrobj->PtrLife = PtrSce ;
			PtrSce = PtrSce + sizetoload ;
		}

// zone declechement: ZONE

		NbZones = GET_WORD ;
		ListZone = (T_ZONE*)PtrSce ;
		PtrSce += NbZones * 12 * 2 ;

		/*
		 * FIXED: four faults of the scenes' data. Scene 9's hidden
		 * mine, actor 21, lies by the way in from scene 62, so that
		 * Twinsen may not get in there. Scene 12's door halves,
		 * actors 28 and 29, overlap by 33, so that the fort's door
		 * may not open. Scene 77's ladder, zone 6, ends 28 short of
		 * Tippet's secret passage. Scene 105's zone 11 takes zone 7's
		 * number 3, so that the action key by the safe runs the
		 * hero's sequence of zone 3, and Twinsen is stuck. The mine
		 * is moved, actor 29 put at Z 1795, the ladder ended at
		 * Z 3616 and zone 11 numbered 50. Taken from LbaWin 0.7.9
		 * (the doors, the zone) and 0.8.0 (the mine, the ladder).
		 */
#ifdef	FIXES
		switch( numscene )
		{
			case 9:
				ListObjet[21].OldPosX = ListObjet[21].PosObjX = 6912 ;
				ListObjet[21].OldPosZ = ListObjet[21].PosObjZ = 768 ;
				/*
				 * FIXED: scene 9's actor 11, the snow bunker's
				 * gun, stands 176 too high, its barrel showing
				 * above the bunker's roof as it turns. It is put
				 * at Y 1024. Taken from the PlayStation version,
				 * SLES_006.98.
				 */
				ListObjet[11].OldPosY = ListObjet[11].PosObjY = 1024 ;
				break ;

			case 12:
				ListObjet[29].OldPosZ = ListObjet[29].PosObjZ = 1795 ;
				/*
				 * FIXED: scene 12's actor 18, the turret's gun,
				 * stands 128 above its slot's floor, its barrel
				 * against the dome over the slot. It is put on
				 * the floor, at Y 2048. Taken from the
				 * PlayStation version, SLES_006.98.
				 */
				ListObjet[18].OldPosY = ListObjet[18].PosObjY = 2048 ;
				break ;

			case 20:
				/*
				 * FIXED: scene 20's zone 4, a bonus, lies wholly
				 * over a cupboard that a jump does not top, so
				 * that Twinsen may not get into it. Its X1 is put
				 * at 24305, the floor beside the cupboard. Taken
				 * from DotEmu's Android 1.06.
				 */
				PokeUWord( (UBYTE*)ListZone + 4*sizeof(T_ZONE)
					   + offsetof(T_ZONE, X1), 24305 ) ;
				break ;

			case 42:
				/*
				 * FIXED: scene 42's actor 22, the rabbibunny who
				 * greets Twinsen coming from the museum, stands
				 * on the harbour building's roof, and his first
				 * walk takes him off its edge, a fall of 1792.
				 * He is put on the walkway before the arch.
				 * Taken from the PlayStation version,
				 * SLES_006.98.
				 */
				ListObjet[22].OldPosY = ListObjet[22].PosObjY = 3584 ;
				ListObjet[22].OldPosZ = ListObjet[22].PosObjZ = 3508 ;
				break ;

			case 58:
				/*
				 * FIXED: scene 58's actor 1, Julia, has an
				 * armour of 1, so that Twinsen's blows wear her
				 * life down and kill her, gone until the scene
				 * loads again. Her armour is put at 51, above
				 * his strongest blow. Taken from the PlayStation
				 * version, SLES_006.98.
				 */
				ListObjet[1].Armure = 51 ;
				break ;

			case 77:
				PokeUWord( (UBYTE*)ListZone + 6*sizeof(T_ZONE)
					   + offsetof(T_ZONE, Z1), 3616 ) ;
				break ;

			case 105:
				PokeUWord( (UBYTE*)ListZone + 11*sizeof(T_ZONE)
					   + offsetof(T_ZONE, Num), 50 ) ;
				break ;
		}
#endif

// point track: TRACK

		NbBrickTrack = GET_WORD ;
		ListBrickTrack = (T_TRACK*)PtrSce ;

		return TRUE ;
}

/*══════════════════════════════════════════════════════════════════════════*
			 █▀▀▀▄ █▀▀▀▀ █▀▀█  █   █ █▀▀▀▀
			 ██  █ ██▀▀  ██▀▀█ ██  █ ██ ▀█
			 ▀▀▀▀  ▀▀▀▀▀ ▀▀▀▀▀ ▀▀▀▀▀ ▀▀▀▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

/*
void	*LoadTestMalloc( char *filename )
{
	void	*ptr ;
	UBYTE	string[256] ;

	ptr = LoadMalloc( filename ) ;

	if( !ptr )
	{
		strcpy( string, "ARG ! Mem:" ) ;
		strcat( string, Itoa( (ULONG)Malloc(-1) ) ) ;
		strcat( string, " fichier:" ) ;
		strcat( string, filename ) ;

		Message( string, TRUE ) ;
	}
	return ptr ;
}
*/

/*══════════════════════════════════════════════════════════════════════════*/
/*══════════════════════════════════════════════════════════════════════════*/

