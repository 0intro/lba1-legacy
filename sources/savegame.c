/*══════════════════════════════════════════════════════════════════════════*
	     ██▀▀▀ █▀▀▀█ █   █ █▀▀▀▀       █▀▀▀▀ █▀▀▀█ █▄ ▄█ █▀▀▀▀
	     ▀▀▀▀█ ██▀▀█ ██ ▄▀ ██▀▀        ██ ▀█ ██▀▀█ ██▀ █ ██▀▀
	     ▀▀▀▀▀ ▀▀  ▀ ▀▀▀   ▀▀▀▀▀       ▀▀▀▀▀ ▀▀  ▀ ▀▀  ▀ ▀▀▀▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

#include	<stddef.h>
#include	<stdint.h>
#include	<stdio.h>
#include	<string.h>
#include	"../sys/compat.h"
#include 	"c_extern.h"

/*
 * FEATURE: the save of the running scene, LBA2's SAVEGAME.CPP. The 1994
 * record, SaveGame's, comes first, its version byte NUM_VERSION, so that
 * LBA.EXE, and the port with SaveAnywhere off, load any save at its
 * scene's entry. The tail follows: a picture of the scene, SAVE_X by
 * SAVE_Y, then SaveContexte's context, the scene's state the record
 * lacks, in LBA2's order and at LBA2's widths, LBA1's variable in the
 * place of LBA2's, then the CRC-32 of both. A named save packs the tail
 * with Compress_LZSS, SAVE_COMPRESS set in the version byte and the
 * tail's size first. A load reads the record, then the tail
 * (LoadGameContexte), its CRC-32 checked and the context read through
 * before any of it is used. ChangeCube loads the scene afresh without
 * what an entry does (FlagLoadGame), LoadContexte lays the context over
 * it, and InitLoadedGame sets each actor up again. Taken from LBA2.
 */

#define	NUM_VERSION		16	// (max 127)
#define	SAVE_COMPRESS		0x80

#define	SAVE_X		160	// l'image
#define	SAVE_Y		120
#define	SIZE_SAVE	(1L<<20)	// la place d'une sauvegarde
#define	SIZE_CUBE	(SIZE_CUBE_X*SIZE_CUBE_Y*SIZE_CUBE_Z*2)	// la grille
#define	RECOVER_AREA	500	// la place que Load_HQR prend après un fichier
#define	KEY_NONE	-1	// le corps ne vient d'aucune frame
#define	KEY_BLEND	-2	// il vient d'un mélange, et prend la frame où il va

extern	UBYTE	PlayerName[] ;

extern	WORD	LastMyJoy ;
extern	WORD	LastMyFire ;
extern	WORD	LastJoyFlag ;

extern	WORD	InvSelect 	    ;	// GAMEMENU.C

// LBA1
extern	UBYTE	*PtrScene ;
extern	WORD	ZoneGrm, IndexGrm, BetaUsedObj, TextLig, BigSampleRepeat ;

UBYTE	*PtrSave ;

WORD	LoadKeyFrame[MAX_OBJETS] ;	// la frame de chaque corps, pour InitLoadedGame
ULONG	LoadKeyTime[MAX_OBJETS] ;

static	UBYTE	*EndSave ;		// la fin du buffer de PtrSave
static	LONG	ErrSave ;		// PtrSave passé EndSave, ou une valeur hors de sa place
static	LONG	ApplySave ;		// le contexte posé sur la scène, ou lu à blanc

static	UBYTE	BufTaken[SIZE_SAVE] ;	// la fin prise par TakeSaveGame: l'image, le contexte
static	ULONG	SizeTaken ;
static	UBYTE	BufRead[SIZE_SAVE] ;	// la fin lue par LoadGameContexte
static	ULONG	SizeRead ;
static	UBYTE	BufPacked[SIZE_SAVE+4] ;	// une fin compactée
static	UBYTE	BufAgain[SIZE_SAVE] ;	// le contexte repris après un chargement

static	char	SceneHqr[] = PATH_RESSOURCE"scene.hqr" ;

/*══════════════════════════════════════════════════════════════════════════*
    █▀▀█  █   █ █▀▀▀▀ █▀▀▀▀ █▀▀▀▀ █▀▀▀█       █▀▀▀▀ █   █ ██▄ █ █▀▀▀▀ ██▀▀▀
    ██▀▀█ ██  █ ██▀▀  ██▀▀  ██▀▀  ██▀█▀       ██▀▀  ██  █ ██▀██ ██    ▀▀▀▀█
    ▀▀▀▀▀ ▀▀▀▀▀ ▀▀    ▀▀    ▀▀▀▀▀ ▀▀  ▀       ▀▀    ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀▀ ▀▀▀▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*
 * FIXED: LBA2's macros read and write their words and longs through
 * casts at any address, odd ones among them, which C leaves undefined,
 * and run past their buffer on a damaged save. These go a byte at a time,
 * stop at the buffer's end (ErrSave), refuse to write a value its width
 * would cut, and their readers set their variable only while the context
 * is laid over its scene (ApplySave), so that a save is read through
 * before any of it is used. LbaGetByte, LbaGetWord and LbaGetLong read
 * what the reading through needs.
 */

//-------- Lecture
#define	LbaRead(ptr,size)	{ UBYTE *ptrlba = LbaTake( size ) ;\
				if( ptrlba AND ApplySave )	memcpy( ptr, ptrlba, size ) ; }

#define	LbaReadByte(ptr)	{ UBYTE *ptrlba = LbaTake( 1 ) ;\
				if( ptrlba AND ApplySave )	ptr = *ptrlba ; }

#define	LbaReadWord(ptr)	{ UBYTE *ptrlba = LbaTake( 2 ) ;\
				if( ptrlba AND ApplySave )	ptr = PeekWord( ptrlba ) ; }

#define	LbaReadLong(ptr)	{ UBYTE *ptrlba = LbaTake( 4 ) ;\
				if( ptrlba AND ApplySave )	ptr = (LONG)PeekULong( ptrlba ) ; }

#define	LbaGetByte(val)		{ UBYTE *ptrlba = LbaTake( 1 ) ;\
				val = ptrlba ? *ptrlba : 0 ; }

#define	LbaGetWord(val)		{ UBYTE *ptrlba = LbaTake( 2 ) ;\
				val = ptrlba ? PeekWord( ptrlba ) : 0 ; }

#define	LbaGetLong(val)		{ UBYTE *ptrlba = LbaTake( 4 ) ;\
				val = ptrlba ? (LONG)PeekULong( ptrlba ) : 0 ; }

//-------- Ecriture
#define	LbaWrite(ptr,size)	LbaPut( ptr, size )

#define	LbaWriteByte(val)	LbaPutByte( val )

#define	LbaWriteWord(val)	LbaPutWord( val )

#define	LbaWriteLong(val)	LbaPutLong( val )

/*──────────────────────────────────────────────────────────────────────────*/
// size octets de PtrSave, NULL au-delà de EndSave
static	UBYTE	*LbaTake( ULONG size )
{
	UBYTE	*ptr ;

	if( ErrSave OR (size > (ULONG)(EndSave-PtrSave)) )
	{
		ErrSave = TRUE ;
		return NULL ;
	}
	ptr = PtrSave ;
	PtrSave += size ;
	return ptr ;
}

/*──────────────────────────────────────────────────────────────────────────*/
static	void	LbaPut( void *ptr, ULONG size )
{
	UBYTE	*ptrsave ;

	ptrsave = LbaTake( size ) ;
	if( ptrsave AND size )	memcpy( ptrsave, ptr, size ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/
// une valeur que sa place couperait: la sauvegarde est refusée
static	void	LbaCut( void )
{
	if( !ErrSave )
	{
		fprintf( stderr, "lba: a value does not fit its place in the save\n" ) ;
	}
	ErrSave = TRUE ;
}

/*──────────────────────────────────────────────────────────────────────────*/
// LbaReadByte la relit sans signe
static	void	LbaPutByte( LONG val )
{
	UBYTE	*ptrsave ;

	if( (val < 0) OR (val > 255) )	LbaCut() ;
	ptrsave = LbaTake( 1 ) ;
	if( ptrsave )	*ptrsave = (UBYTE)val ;
}

/*──────────────────────────────────────────────────────────────────────────*/
static	void	LbaPutWord( LONG val )
{
	UBYTE	*ptrsave ;

	if( (val < -32768) OR (val > 65535) )	LbaCut() ;
	ptrsave = LbaTake( 2 ) ;
	if( ptrsave )	PokeUWord( ptrsave, (UWORD)val ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/
static	void	LbaPutLong( ULONG val )
{
	UBYTE	*ptrsave ;

	ptrsave = LbaTake( 4 ) ;
	if( ptrsave )	PokeULong( ptrsave, val ) ;
}

/*══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/
// LBA1: le CRC-32 de size octets
static	ULONG	Crc32( UBYTE *ptr, ULONG size )
{
	ULONG	crc ;
	LONG	n ;

	crc = 0xFFFFFFFF ;
	while( size-- )
	{
		crc ^= *ptr++ ;
		for( n=0; n<8; n++ )
		{
			crc = (crc>>1) ^ (0xEDB88320 & -(crc&1)) ;
		}
	}
	return ~crc ;
}

/*──────────────────────────────────────────────────────────────────────────*/
/*
 * The Checksum LBA2 keeps in each scene's file: here the CRC-32 of the
 * scene num as SCENE.HQR holds it. FALSE if it cannot be read.
 */
static	LONG	SceneChecksum( LONG num, ULONG *checksum )
{
	UBYTE	*ptr ;
	ULONG	size ;

	*checksum = 0 ;
	size = Size_HQR( SceneHqr, num ) ;
	if( !size )	return FALSE ;
	ptr = Malloc( size+RECOVER_AREA ) ;
	if( !ptr )	return FALSE ;
	Load_HQR( (UBYTE*)SceneHqr, ptr, num ) ;
	*checksum = Crc32( ptr, size ) ;
	Free( ptr ) ;
	return TRUE ;
}

/*──────────────────────────────────────────────────────────────────────────*/
// LBA1: l'anim num dans le cache, sans la dater, et sa taille, ou NULL
static	UBYTE	*CachedAnim( WORD num, ULONG *size )
{
	T_HQR_BLOC	*ptrbloc ;

	if( num == -1 )	return NULL ;
	ptrbloc = HQR_GiveIndex( num, HQR_Anims->NbIndex, (UBYTE*)HQR_Anims+sizeof(T_HQR_HEADER) ) ;
	if( !ptrbloc )	return NULL ;
	*size = ptrbloc->Size ;
	return HQR_Anims->Buffer + ptrbloc->Offset ;
}

/*──────────────────────────────────────────────────────────────────────────*/
/*
 * The keyframe of its animation a 3D actor's body comes from, LBA2's
 * LastFrame, and the time it left it, its LastTimer: KEY_NONE for a
 * sprite or an actor without a body, KEY_BLEND for a frame of no
 * keyframe, a blend's.
 */
static	LONG	KeyFrame( T_OBJET *ptrobj, ULONG *time )
{
	UBYTE	*ptrframe, *ptranim ;
	ULONG	size, offset, step ;

	*time = 0 ;
	if( (ptrobj->Body == -1) OR (ptrobj->Flags & SPRITE_3D) )	return KEY_NONE ;
	ptrframe = bodyframe( PtrBody[ptrobj->Body], time ) ;
	if( !ptrframe )	return KEY_NONE ;
	ptranim = CachedAnim( ptrobj->Anim, &size ) ;
	if( !ptranim )	return KEY_BLEND ;
	if( ((uintptr_t)ptrframe < (uintptr_t)(ptranim+8))
	OR  ((uintptr_t)ptrframe-(uintptr_t)(ptranim+8) >= size-8) )	return KEY_BLEND ;
	offset = (ULONG)((uintptr_t)ptrframe-(uintptr_t)(ptranim+8)) ;
	step = 8*PeekUWord( ptranim+2 ) + 8 ;
	if( (offset%step) OR (offset/step >= PeekUWord( ptranim )) )	return KEY_BLEND ;
	return offset/step ;
}

/*──────────────────────────────────────────────────────────────────────────*/
/*
 * The cells of the grid the scene changed (the brick overlays), as runs
 * of an offset, a length and the cells, ended by SIZE_CUBE: the grid is
 * made again from the map, aside, and compared.
 */
static	void	SaveGrid( void )
{
	UBYTE	*ptrmap, *ptrcube ;
	ULONG	n, m ;

	ptrmap = Malloc( SIZE_CUBE ) ;
	if( !ptrmap )
	{
		ErrSave = TRUE ;
		return ;
	}
	ptrcube = BufCube ;
	BufCube = ptrmap ;
	CopyMapToCube() ;
	BufCube = ptrcube ;
	for( n=0; n<SIZE_CUBE; n=m )
	{
		while( (n < SIZE_CUBE) AND (ptrcube[n] == ptrmap[n]) )	n++ ;
		for( m=n; (m < SIZE_CUBE) AND (ptrcube[m] != ptrmap[m]); m++ )
			;
		if( m > n )
		{
			LbaWriteLong( n ) ;
			LbaWriteLong( m-n ) ;
			LbaWrite( ptrcube+n, m-n ) ;
		}
	}
	LbaWriteLong( SIZE_CUBE ) ;
	Free( ptrmap ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/
static	void	LoadGrid( void )
{
	LONG	offset, size ;

	while( TRUE )
	{
		LbaGetLong( offset ) ;
		if( ErrSave OR (offset == SIZE_CUBE) )	return ;
		LbaGetLong( size ) ;
		if( (offset < 0) OR (offset > SIZE_CUBE) OR (size < 0) OR (size > SIZE_CUBE-offset) )
		{
			ErrSave = TRUE ;
			return ;
		}
		LbaRead( BufCube+offset, size ) ;
	}
}

/*──────────────────────────────────────────────────────────────────────────*/
// Resultat pas terrible
/*
 * FIXED: LBA2's RemapPicture shifts both palettes in place to 6 bits and
 * back, which clears their low 2 bits, and maps 255 colours of 256, a
 * pixel of the last left to a byte of newpal never set. It shifts copies,
 * and maps the 256.
 */
void	RemapPicture( UBYTE *buf, ULONG size )
{
	LONG	n ;
	UBYTE	newpal[256] ;
	UBYTE	pal[768], palnormal[768] ;
	UBYTE	*ptr ;
	UBYTE	r,v,b ;

	// LBA1: la palette d'alarme
	if( !FlagPalettePcx )	return ;

	for( n=0; n<768; n++ )	pal[n] = PalettePcx[n] >> 2 ;
	for( n=0; n<768; n++ )	palnormal[n] = PtrPal[n] >> 2 ;

	ptr = pal ;
	for( n=0; n<256; n++ )
	{
		// ATTENTION: ne pas passer les (*ptr++) directement en
		// parametre sinon, l'empilage du C passerait b,v,r
		r = *ptr++ ;
		v = *ptr++ ;
		b = *ptr++ ;

		newpal[n] = SearchBoundColRGB( r, v, b, palnormal, 0, 255 ) ;
	}

	for( n=0; n<(LONG)size; n++, buf++ )
	{
		*buf = newpal[*buf] ;
	}
}

/*══════════════════════════════════════════════════════════════════════════*
	  █▀▀▀▄  █    ██▀▀▀ █  ▄▀       █▀▀▀▀ █   █ ██▄ █ █▀▀▀▀ ██▀▀▀
	  ██  █  ██   ▀▀▀▀█ ██▀▄        ██▀▀  ██  █ ██▀██ ██    ▀▀▀▀█
	  ▀▀▀▀   ▀▀   ▀▀▀▀▀ ▀▀  ▀       ▀▀    ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀▀ ▀▀▀▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/
/*
 * AutoSaveGame is LBA2's: the game as it runs saved into
 * AUTOSAVE_FILENAME as AUTOSAVE_NAME, unpacked, on the first frame of
 * each new scene, its record alone if the scene could not be taken. It
 * returns at once in the credits, LBA2's DemoSlide, and the end.
 */
void	AutoSaveGame( void )
{
	char	memoplayername[256] ;
	char	memogamepathname[_MAX_PATH] ;

	if( FlagCredits OR PLAY_THE_END )	return ;

	SaveTimer() ;

	strcpy( memoplayername, (char *)PlayerName ) ;
	strcpy( memogamepathname, (char *)GamePathname ) ;

	strcpy( (char *)GamePathname, AUTOSAVE_FILENAME ) ;
	strcpy( (char *)PlayerName, AUTOSAVE_NAME ) ;

	// Sauvegarde non compactee
	if( !TakeSaveGame() OR !WriteSaveGame( FALSE ) )
	{
		SaveGame() ;
	}

	strcpy( (char *)PlayerName, memoplayername ) ;
	strcpy( (char *)GamePathname, memogamepathname ) ;

	RestoreTimer() ;
}

/*──────────────────────────────────────────────────────────────────────────*/
/*
 * AutoSaveRecord: the end's record, as SaveGame writes it, saved into
 * AUTOSAVE_FILENAME as AUTOSAVE_NAME, the game's own save.
 */
void	AutoSaveRecord( void )
{
	char	memoplayername[256] ;
	char	memogamepathname[_MAX_PATH] ;

	SaveTimer() ;

	strcpy( memoplayername, (char *)PlayerName ) ;
	strcpy( memogamepathname, (char *)GamePathname ) ;

	strcpy( (char *)GamePathname, AUTOSAVE_FILENAME ) ;
	strcpy( (char *)PlayerName, AUTOSAVE_NAME ) ;

	SaveGame() ;

	strcpy( (char *)PlayerName, memoplayername ) ;
	strcpy( (char *)GamePathname, memogamepathname ) ;

	RestoreTimer() ;
}

/*──────────────────────────────────────────────────────────────────────────*/
/*
 * CurrentSaveGame is LBA2's: the game as it runs saved into
 * CURRENTSAVE_FILENAME as CURRENTSAVE_NAME, unpacked, which the main
 * menu's Resume Game loads, at Escape while the game may be saved and
 * after each load, its record alone if the scene could not be taken. The
 * tail taken is kept for the main menu's Save Game (WriteSaveGame). It
 * returns at once in the credits and the end.
 */
void	CurrentSaveGame( void )
{
	char	memoplayername[256] ;
	char	memogamepathname[_MAX_PATH] ;

	if( FlagCredits OR PLAY_THE_END )	return ;

	SaveTimer() ;

	strcpy( memoplayername, (char *)PlayerName ) ;
	strcpy( memogamepathname, (char *)GamePathname ) ;

	strcpy( (char *)GamePathname, CURRENTSAVE_FILENAME ) ;
	strcpy( (char *)PlayerName, CURRENTSAVE_NAME ) ;

	// Sauvegarde non compactee
	if( !TakeSaveGame() OR !WriteSaveGame( FALSE ) )
	{
		SaveGame() ;
	}

	strcpy( (char *)PlayerName, memoplayername ) ;
	strcpy( (char *)GamePathname, memogamepathname ) ;

	RestoreTimer() ;
}

/*──────────────────────────────────────────────────────────────────────────*/
/*
 * TakeSaveGame is the first half of LBA2's SaveGame: the tail of a save
 * taken now, the picture of the scene in Log scaled to SAVE_X by SAVE_Y,
 * then the context, kept for WriteSaveGame. Its size, 0 if it could not
 * be taken.
 */
ULONG	TakeSaveGame( void )
{
	UBYTE	*ptr ;

	SizeTaken = 0 ;

	// Image 160x120
	/*
	 * FEATURE: the picture is the window of 640x480 in the middle of a
	 * screen of any size, scaled into a buffer of the screen's size, as
	 * the window's lines start where the window lies. The port's own.
	 */
	ptr = Malloc( Screen_X*Screen_Y ) ;
	if( !ptr )	return 0 ;
	ScaleBox( 0, 0, 639, 479, Log, 0, 0, SAVE_X-1, SAVE_Y-1, ptr ) ;
	SaveBlock( ptr, BufTaken, 0, 0, SAVE_X-1, SAVE_Y-1 ) ;
	Free( ptr ) ;
	RemapPicture( BufTaken, SAVE_X*SAVE_Y ) ;

	PtrSave = BufTaken+SAVE_X*SAVE_Y ;
	EndSave = BufTaken+SIZE_SAVE ;
	ErrSave = FALSE ;

	// le TimerRef tel que SaveTimer l'a figé
	SaveContexte( CmptMemoTimerRef ? MemoTimerRef : TimerRef ) ;

	// LBA1: le CRC-32 de l'image et du contexte
	LbaWriteLong( Crc32( BufTaken, (ULONG)(PtrSave-BufTaken) ) ) ;

	if( ErrSave )	return 0 ;

	SizeTaken = PtrSave-BufTaken ;
	return SizeTaken ;
}

/*──────────────────────────────────────────────────────────────────────────*/
/*
 * WriteSaveGame is the second half of LBA2's SaveGame: the save into
 * GamePathname, SaveGame's record, its version byte made NUM_VERSION,
 * then the tail TakeSaveGame took, packed when flagcompress (LBA2's named
 * saves) and the packing shrinks it, written beside the file then
 * renamed over it. FALSE if it could not be, the file left as it was.
 *
 * FIXED: LBA2's SaveGame copies Compress_LZSS's output over the tail
 * whatever it returns, and a tail it cannot shrink, whose output it
 * leaves unfinished, came out garbled. Such a tail is written as it is,
 * its version byte without SAVE_COMPRESS. LBA2 writes the file over
 * itself, and a failed write lost the save before.
 */
LONG	WriteSaveGame( LONG flagcompress )
{
	char	pathname[_MAX_PATH] ;
	char	tmpname[_MAX_PATH] ;
	UBYTE	numversion ;
	ULONG	size ;
	LONG	handle ;
	LONG	ok ;

	if( !SizeTaken )	return FALSE ;

	numversion = NUM_VERSION ;
	size = 0 ;

	if( flagcompress )	// Version compactee
	{
		size = Compress_LZSS( (char *)BufTaken, (char *)BufPacked+4, SizeTaken ) ;
		if( size < SizeTaken )
		{
			numversion |= SAVE_COMPRESS ;
			PokeULong( BufPacked, SizeTaken ) ;
		}
	}

	strcpy( pathname, (char *)GamePathname ) ;
	strcpy( tmpname, pathname ) ;
	AddExt( tmpname, "TMP" ) ;
	strcpy( (char *)GamePathname, tmpname ) ;
	SaveGame() ;
	strcpy( (char *)GamePathname, pathname ) ;

	handle = OpenReadWrite( tmpname ) ;
	if( !handle )	return FALSE ;

	ok = (Seek( handle, 0, SEEK_START ) == 0)
	AND  (Write( handle, &numversion, 1 ) == 1)
	AND  (Seek( handle, 0, SEEK_END ) > 0) ;

	if( numversion & SAVE_COMPRESS )
	{
		ok = ok AND (Write( handle, BufPacked, size+4 ) == size+4) ;
	}
	else
	{
		ok = ok AND (Write( handle, BufTaken, SizeTaken ) == SizeTaken) ;
	}

	Close( handle ) ;

	if( ok AND renamefile( tmpname, pathname ) )	return TRUE ;

	Delete( tmpname ) ;
	return FALSE ;
}

/*──────────────────────────────────────────────────────────────────────────*/
/*
 * The packed tail at src, n bytes, expanded into dst, size bytes, as
 * Expand reads LZS. FALSE for a stream that runs out, copies from before
 * dst, or holds bytes past its last.
 *
 * FIXED: LBA2's LoadGame expands a packed save with ExpandLZ, which
 * trusts the stream: a damaged one runs past the buffers. This checks
 * each step.
 */
static	LONG	ExpandSave( UBYTE *src, ULONG n, UBYTE *dst, ULONG size )
{
	UBYTE	*ptrs, *ends, *ptrd, *endd ;
	ULONG	word, len, back, flags ;
	LONG	i ;

	ptrs = src ;
	ends = src+n ;
	ptrd = dst ;
	endd = dst+size ;

	while( ptrd < endd )
	{
		if( ptrs >= ends )	return FALSE ;
		flags = *ptrs++ ;
		for( i=0; (i < 8) AND (ptrd < endd); i++, flags >>= 1 )
		{
			if( flags & 1 )
			{
				if( ptrs >= ends )	return FALSE ;
				*ptrd++ = *ptrs++ ;
				continue ;
			}
			if( ends-ptrs < 2 )	return FALSE ;
			word = PeekUWord( ptrs ) ;
			ptrs += 2 ;
			len = (word & 0xF) + 2 ;
			back = (word >> 4) + 1 ;
			if( (back > (ULONG)(ptrd-dst)) OR (len > (ULONG)(endd-ptrd)) )	return FALSE ;
			for( ; len; len--, ptrd++ )	*ptrd = *(ptrd-back) ;
		}
	}
	return ptrs == ends ;
}

/*──────────────────────────────────────────────────────────────────────────*/
/*
 * The size of the record that opens the n bytes of a save at ptr
 * (SaveGame's: the version, the name, the game's flags, the scene and
 * the hero, the holomap's positions, the fuel, the inventory, the
 * clovers and the weapon), 0 if they hold none.
 */
static	ULONG	RecordSize( UBYTE *ptr, ULONG n )
{
	ULONG	i ;

	for( i=1; (i < n) AND ptr[i]; i++ )
		;
	i++ ;
	if( i >= n )	return 0 ;
	i += 1+ptr[i]+18 ;
	if( i >= n )	return 0 ;
	i += 1+ptr[i]+1 ;
	if( i >= n )	return 0 ;
	i += 1+ptr[i]+3 ;
	return (i <= n) ? i : 0 ;
}

/*──────────────────────────────────────────────────────────────────────────*/
/*
 * Whether the size bytes of a tail at ptr, the picture and the context,
 * end with their CRC-32, as TakeSaveGame writes it.
 *
 * FIXED: LBA2's LoadGame lays whatever a save holds, and a value damaged
 * in a save, which its counts let through, can give the actors places,
 * scripts' offsets and numbers out of their tables. A tail whose CRC-32
 * differs loads as its record.
 */
static	LONG	CheckTail( UBYTE *ptr, ULONG size )
{
	if( size <= SAVE_X*SAVE_Y+4 )	return FALSE ;
	return Crc32( ptr, size-4 ) == PeekULong( ptr+size-4 ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/
/*
 * LoadGameScreen is LBA2's: the picture of the save GamePathname names,
 * SAVE_X by SAVE_Y, or NULL for a save that holds none (version 3) or
 * whose tail is damaged, which loads as its record.
 */
UBYTE	*LoadGameScreen( void )
{
	ULONG	n, size, sizerecord ;
	LONG	handle ;

	handle = OpenRead( (char *)GamePathname ) ;
	if( !handle )	return( NULL ) ;
	n = Read( handle, BufPacked, sizeof(BufPacked) ) ;
	Close( handle ) ;

	sizerecord = RecordSize( BufPacked, n ) ;
	if( !sizerecord OR ((BufPacked[0] & ~SAVE_COMPRESS) != NUM_VERSION) )	return( NULL ) ;

	if( !(BufPacked[0] & SAVE_COMPRESS) )
	{
		return( CheckTail( BufPacked+sizerecord, n-sizerecord ) ? BufPacked+sizerecord : NULL ) ;
	}

	// Version compactee
	if( n-sizerecord < 4 )	return( NULL ) ;
	size = PeekULong( BufPacked+sizerecord ) ;
	if( (size <= SAVE_X*SAVE_Y) OR (size > SIZE_SAVE)
	OR  !ExpandSave( BufPacked+sizerecord+4, n-sizerecord-4, BufRead, size ) )
	{
		return( NULL ) ;
	}
	SizeRead = 0 ;
	return( CheckTail( BufRead, size ) ? BufRead : NULL ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/
/*
 * LoadGameContexte is the end of LBA2's LoadGame: LoadGame's handle past
 * the record of a save of the given version. With SaveAnywhere on and a
 * save of NUM_VERSION, its tail read, unpacked, its CRC-32 checked and
 * its context read through against the scene to load, then kept for
 * LoadGameScene. Any other save loads as its record, at the scene's
 * entry.
 */
LONG	LoadGameContexte( LONG handle, UBYTE numversion )
{
	UBYTE	buf[4] ;
	ULONG	n, size ;
	LONG	savetimerref ;

	SizeRead = 0 ;

	if( !FlagSaveAnywhere OR ((numversion & ~SAVE_COMPRESS) != NUM_VERSION) )	return FALSE ;

	if( numversion & SAVE_COMPRESS )	// Version compactee
	{
		if( Read( handle, buf, 4 ) != 4 )	return FALSE ;
		size = PeekULong( buf ) ;
		if( (size <= SAVE_X*SAVE_Y) OR (size > SIZE_SAVE) )	return FALSE ;
		n = Read( handle, BufPacked, sizeof(BufPacked) ) ;
		if( !n OR (n >= size) OR !ExpandSave( BufPacked, n, BufRead, size ) )	return FALSE ;
	}
	else
	{
		size = Read( handle, BufRead, sizeof(BufRead) ) ;
		if( size >= sizeof(BufRead) )	return FALSE ;
	}

	if( !CheckTail( BufRead, size ) )	return FALSE ;
	SizeRead = size-4 ;	// l'image et le contexte

	PtrSave = BufRead+SAVE_X*SAVE_Y ;
	EndSave = BufRead+SizeRead ;
	ErrSave = FALSE ;
	ApplySave = FALSE ;

	if( LoadContexte( &savetimerref ) )
	{
		SizeRead = 0 ;
		return FALSE ;
	}
	return TRUE ;
}

/*──────────────────────────────────────────────────────────────────────────*/
/*
 * LoadGameScene: the context LoadGameContexte kept laid over the scene
 * ChangeCube has loaded under FlagLoadGame, the timer put back as LBA2's
 * LoadGame puts it, then InitLoadedGame. FALSE for a save that has none
 * or one that could not be laid, the scene's actors then to be set up as
 * at its entry. The scene is then taken again, which must give the same
 * context: what differs was not put back, and is told.
 */
LONG	LoadGameScene( void )
{
	ULONG	n, m, i ;
	LONG	savetimerref ;

	if( !SizeRead )	return FALSE ;

	PtrSave = BufRead+SAVE_X*SAVE_Y ;
	EndSave = BufRead+SizeRead ;
	ErrSave = FALSE ;
	ApplySave = TRUE ;

	if( LoadContexte( &savetimerref ) )
	{
		ApplySave = FALSE ;
		SizeRead = 0 ;
		fprintf( stderr, "lba: the save could not be laid over its scene\n" ) ;
		return FALSE ;
	}

	ApplySave = FALSE ;

	TimerRef = savetimerref ;
	if( CmptMemoTimerRef )	MemoTimerRef = savetimerref ;

	InitLoadedGame() ;

	n = SizeRead-SAVE_X*SAVE_Y ;
	PtrSave = BufAgain ;
	EndSave = BufAgain+SIZE_SAVE ;
	ErrSave = FALSE ;
	SaveContexte( savetimerref ) ;
	m = PtrSave-BufAgain ;
	SizeRead = 0 ;

	if( !ErrSave AND (m == n) AND !memcmp( BufAgain, BufRead+SAVE_X*SAVE_Y, n ) )	return TRUE ;

	for( i=0; (i < n) AND (i < m) AND (BufAgain[i] == BufRead[SAVE_X*SAVE_Y+i]); i++ )
		;
	fprintf( stderr, "lba: the scene loaded differs from its save at byte %u of %u\n", (unsigned)i, (unsigned)n ) ;
	return TRUE ;
}

/*──────────────────────────────────────────────────────────────────────────*/
// Sauvegarde de l'etat du jeu dans PtrSave
void	SaveContexte( LONG savetimerref )
{
	T_OBJET		*ptrobj ;
	T_ZONE		*ptrz ;
	LONG	n ;
	UBYTE	wbyte ;
	UBYTE	*saveptr ;
	T_PATCH		*ptrpatch ;
	T_INCRUST_DISP	*ptrdisp ;
	T_INCRUST_DISP	disp ;
	ULONG	checksum ;
	LONG	keyframe ;
	ULONG	keytime ;
	UWORD	handle, pitch, volleft, volright ;
	ULONG	info0 ;

	// list vars game et cube
//	LbaWrite( ListVarGame, MAX_VARS_GAME*sizeof(ListVarGame[0]) ) ;	// LBA1: dans l'enregistrement
	LbaWrite( ListFlagCube, MAX_FLAGS_CUBE*sizeof(ListFlagCube[0]) ) ;

	// Globales
//	LbaWriteByte( Comportement ) ;	// LBA1: dans l'enregistrement

//	LbaWriteLong( ((NbZlitosPieces<<16)+(NbGoldPieces&0xFFFF)) ) ;
//	LbaWriteByte( MagicLevel ) ;
//	LbaWriteByte( MagicPoint ) ;
	LbaWriteByte( NbLittleKeys ) ;
//	LbaWriteWord( NbCloverBox ) ;

//	LbaWriteLong( SceneStartX ) ;
//	LbaWriteLong( SceneStartY ) ;
//	LbaWriteLong( SceneStartZ ) ;

	LbaWriteLong( StartXCube ) ;
	LbaWriteLong( StartYCube ) ;
	LbaWriteLong( StartZCube ) ;

//	LbaWriteByte( Weapon ) ;

	LbaWriteLong( savetimerref ) ;
	LbaWriteByte( NumObjFollow ) ;

	LbaWriteByte( SaveComportement ) ;	// SaveComportementHero
	LbaWriteWord( SaveBeta ) ;		// LBA1: a la place de SaveBodyHero

//	ptra = TabArrow ;			// LBA1: TabHoloPos, dans l'enregistrement

	// used inventory
//	ptri = TabInv ;				// LBA1: dans l'enregistrement

	// LBA1: le CRC-32 de la scene
	if( !SceneChecksum( NumCube, &checksum ) )	ErrSave = TRUE ;
	LbaWriteLong( checksum ) ;

	// Debut d'incompatibilité des sauvegardes

	LbaWriteLong( LastMyFire ) ;
	LbaWriteLong( LastMyJoy ) ;
//	LbaWriteLong( LastInput ) ;
	LbaWriteLong( LastJoyFlag ) ;
	LbaWriteByte( Bulle ) ;
	LbaWriteByte( ActionNormal ) ;
	LbaWriteLong( InventoryAction ) ;
	LbaWriteLong( MagicBall ) ;
	LbaWriteByte( MagicBallType ) ;
	LbaWriteWord( MagicBallCount ) ;	// LBA1: -1 au retour de la balle
//	LbaWriteLong( MagicBallFlags ) ;
	LbaWriteByte( FlagClimbing ) ;
	LbaWriteLong( StartYFalling ) ;
	LbaWrite( &RealFalling, sizeof( RealFalling ) ) ;	// comme avant la version 34
	LbaWriteLong( StepFalling ) ;
	LbaWriteByte( CameraZone ) ;
	LbaWriteLong( InvSelect ) ;
//	LbaWriteLong( ExtraConque ) ;
	LbaWriteWord( NumPingouin ) ;		// PingouinActif, LBA1: -1 sans pingouin

//	LbaWriteLong( (S32)PtrZoneClimb ) ;

	// Objets
	LbaWriteLong( NbObjets ) ; // nb d'objets pour EditLba2

	ptrobj = ListObjet ;

	for( n=0; n<NbObjets; n++, ptrobj++ )
	{
		LbaWriteByte( ptrobj->GenBody ) ;
		LbaWriteByte( ptrobj->Col ) ;
		LbaWriteWord( ptrobj->GenAnim ) ;
		LbaWriteWord( ptrobj->NextGenAnim ) ;
		LbaWriteLong( ptrobj->OldPosX ) ;
		LbaWriteLong( ptrobj->OldPosY ) ;
		LbaWriteLong( ptrobj->OldPosZ ) ;
		LbaWriteLong( ptrobj->Info ) ;
		LbaWriteLong( ptrobj->Info1 ) ;
		LbaWriteLong( ptrobj->Info2 ) ;
		LbaWriteLong( ptrobj->Info3 ) ;

//		LbaWrite( &(ptrobj->Coord), sizeof(ptrobj->Coord) ) ;

//		LbaWriteWord( ptrobj->SizeSHit ) ;
		LbaWriteWord( ptrobj->HitBy ) ;		// LBA1: -1 pour personne
		LbaWriteByte( ptrobj->HitForce ) ;
		LbaWriteWord( ptrobj->LifePoint ) ;
		LbaWriteWord( ptrobj->OptionFlags ) ;

//	U8	*PtrAnimAction ;

		LbaWriteWord( ptrobj->Sprite ) ;
		LbaWriteWord( ptrobj->OffsetLabelTrack ) ;

//	T_OBJ_3D Obj	;
//	U8	*PtrFile3D ;

//		LbaWriteLong( ptrobj->IndexFile3D ) ;

//	S16	NbBonus ;
		LbaWriteByte( ptrobj->Armure ) ;
//	U8	CoulObj ;

		LbaWriteWord( ptrobj->Xmin ) ;
		LbaWriteWord( ptrobj->Xmax ) ;
		LbaWriteWord( ptrobj->Ymin ) ;
		LbaWriteWord( ptrobj->Ymax ) ;
		LbaWriteWord( ptrobj->Zmin ) ;
		LbaWriteWord( ptrobj->Zmax ) ;

//		LbaWriteLong( ptrobj->OldBeta ) ;
		LbaWrite( &(ptrobj->RealAngle),sizeof(ptrobj->RealAngle) ) ;	// BoundAngle
//	U8	*PtrTrack ;
		LbaWriteWord( ptrobj->OffsetTrack ) ;
		LbaWriteWord( ptrobj->SRot ) ;
		LbaWriteWord( ptrobj->OffsetLife ) ;
//		LbaWriteWord( ptrobj->AnimDial ) ;
		LbaWriteWord( ptrobj->CarryBy ) ;
		LbaWriteByte( ptrobj->Move ) ;
		LbaWriteWord( ptrobj->ObjCol ) ;	// LBA1: -1 pour aucun
		LbaWriteWord( ptrobj->ZoneSce ) ;
		LbaWriteWord( ptrobj->LabelTrack ) ;
		LbaWriteWord( ptrobj->MemoLabelTrack ) ;
//		LbaWriteWord( ptrobj->MemoComportement ) ;
		LbaWriteLong( ptrobj->Flags ) ;
		LbaWriteLong( ptrobj->WorkFlags ) ;
		LbaWriteWord( ptrobj->DoorWidth ) ;
		LbaWriteWord( ptrobj->FlagAnim ) ;	// LBA1: l'alpha d'un sprite vers un point 3D aussi
		LbaWriteByte( ptrobj->CodeJeu ) ;
//		LbaWrite( &(ptrobj->ExeSwitch),sizeof(ptrobj->ExeSwitch) ) ;

//	S16	MessageChapter[MAX_CHAPTER] ;

//		LbaWriteLong( ptrobj->SampleAlways ) ;
//		LbaWriteByte( ptrobj->SampleVolume ) ;

//	T_ZONE	*PtrZoneRail ;

		// on ne veut pas sauver le champ CurrentFrame
		// LBA1: les champs de T_OBJ_3D dans son ordre, X, Y, Z,
		// Beta, Body, Anim, LastFrame et LastTimer (la frame d'ou
		// vient le corps), NextFrame et les LastAnimStep
		LbaWriteLong( ptrobj->PosObjX ) ;
		LbaWriteLong( ptrobj->PosObjY ) ;
		LbaWriteLong( ptrobj->PosObjZ ) ;
		LbaWriteLong( ptrobj->Beta ) ;
		LbaWriteByte( ptrobj->Body != -1 ) ;	// un corps, retrouve au chargement
		LbaWriteLong( ptrobj->Anim ) ;
		keyframe = KeyFrame( ptrobj, &keytime ) ;
		LbaWriteLong( keyframe ) ;
		LbaWriteLong( keytime ) ;
		LbaWriteLong( ptrobj->Frame ) ;
		LbaWriteLong( ptrobj->AnimStepX ) ;
		LbaWriteLong( ptrobj->AnimStepY ) ;
		LbaWriteLong( ptrobj->AnimStepZ ) ;
		LbaWriteLong( ptrobj->AnimStepBeta ) ;
	}

	//------------- PATCHES -------------
	LbaWriteLong( NbPatches ) ;

	saveptr = PtrSave ;
	LbaWriteLong( 0 ) ;	// la taille, ecrite ensuite

	ptrpatch = ListPatches ;

	for( n=0; n<NbPatches; n++, ptrpatch++ )
	{
		switch( ptrpatch->Size )
		{
			case 1:
				LbaWriteByte( *(PtrScene+ptrpatch->Offset) ) ;
				break ;

			case 2:
				LbaWriteWord( PeekWord( PtrScene+ptrpatch->Offset ) ) ;
				break ;

			case 4:
				LbaWriteLong( PeekULong( PtrScene+ptrpatch->Offset ) ) ;
				break ;

			default:
				LbaWrite( PtrScene+ptrpatch->Offset, ptrpatch->Size ) ;
		}
	}

	if( !ErrSave )	PokeULong( saveptr, (ULONG)(PtrSave-saveptr-4) ) ;
	//-----------------------------------

	// Extras
	/*
	 * FIXED: LBA2 writes the extras in use alone, and a load packs them to
	 * the list's start, moving each from its place, by which MagicBall
	 * names one. Every extra is written, in its place.
	 */
	LbaWriteByte( MAX_EXTRAS ) ;
	LbaWrite( ListExtra, sizeof(T_EXTRA)*MAX_EXTRAS ) ;

	// Zones
	LbaWriteLong( NbZones ) ;	// pour EditLba2

	ptrz = ListZone ;

	for( n=0; n<NbZones; n++, ptrz++ )
	{
		LbaWriteLong( ptrz->Info1 ) ;
		LbaWriteLong( ptrz->Info2 ) ;
		LbaWriteLong( ptrz->Info3 ) ;
//		LbaWriteLong( ptrz->Info7 ) ;
	}

	// Incrust
	saveptr = PtrSave ;
	LbaWriteByte( 0 ) ;	// le nombre, ecrit ensuite
	wbyte = 0 ;
	ptrdisp = ListIncrustDisp ;

	for( n=0; n<MAX_INCRUST_DISP; n++, ptrdisp++ )
	{
		if( ptrdisp->Num!=-1 )
		{
			/*
			 * FEATURE: a number's place, projected, counted as on a
			 * screen of 640x480, so that the save loads at any width.
			 * The port's own.
			 */
			disp = *ptrdisp ;
			if( disp.Type == INCRUST_NUM )
			{
				disp.X -= CENTRE_ECRAN_X-320 ;
			}
			LbaWrite( &disp, sizeof(T_INCRUST_DISP) ) ;
			wbyte++ ;
		}
	}

	if( !ErrSave )	*saveptr = wbyte ;

	// Infos camera
//	LbaWriteLong( VueDistance ) ;
//	LbaWriteLong( AlphaCam ) ;
//	LbaWriteLong( BetaCam ) ;
//	LbaWriteLong( GammaCam ) ;
//	LbaWriteLong( AddBetaCam ) ;

	// LBA1: la camera dans le monde, telle que la grille l'a dessinee
	LbaWriteLong( WorldXCube ) ;	// VueOffsetX
	LbaWriteLong( WorldYCube ) ;	// VueOffsetY
	LbaWriteLong( WorldZCube ) ;	// VueOffsetZ

	/*
	 * What LBA1 holds that LBA2 has not, or keeps elsewhere: the menu's
	 * choice, the water, the dialogue's line, the object used, the brick
	 * overlay, the alarm's palette, the ambience's timers, the behaviour
	 * the action key keeps, the grid's cells the overlays changed, the
	 * samples that play for ever and rand's seed.
	 */
	LbaWriteWord( GameChoice ) ;
	LbaWriteWord( FlagWater ) ;
	LbaWriteWord( TextLig ) ;
	LbaWriteWord( BetaUsedObj ) ;
	LbaWriteWord( ZoneGrm ) ;
	LbaWriteWord( IndexGrm ) ;
	LbaWriteWord( FlagPalettePcx ) ;
	LbaWrite( PalettePcx, 768 ) ;
	LbaWriteWord( SamplePlayed ) ;
	LbaWriteLong( TimerNextAmbiance ) ;
	LbaWriteWord( BigSampleRepeat ) ;
	LbaWriteWord( MemoComportement ) ;

	SaveGrid() ;

	for( n=0; waveloop( n, &handle, &pitch, &volleft, &volright, &info0 ); n++ )
		;
	if( n > 255 )	ErrSave = TRUE ;
	LbaWriteByte( n & 0xFF ) ;
	for( n=0; waveloop( n, &handle, &pitch, &volleft, &volright, &info0 ) AND (n < 256); n++ )
	{
		LbaWriteWord( handle ) ;
		LbaWriteWord( pitch ) ;
		LbaWriteWord( volleft ) ;
		LbaWriteWord( volright ) ;
		LbaWriteLong( info0 ) ;
	}

	LbaWriteLong( watcomseed() ) ;
}

/*──────────────────────────────────────────────────────────────────────────*/
// Chargement d'une partie se trouvant dans PtrSave
/*
 * LoadContexte is LBA2's: the context at PtrSave read through or, when
 * ApplySave, laid over the scene loaded afresh, the scene NewCube while
 * it is read through and NumCube when it is laid. TRUE, LBA2's flaginit,
 * if it is another scene's or does not read through, the scene then to
 * start as at its entry. Reading it through leaves the game as it is.
 *
 * FIXED: LBA2's LoadContexte takes as many objects, patches, zones,
 * extras and incrusts as the save says, into the game's lists. Each count
 * is read into a local and checked against the scene and the lists, and
 * the patches' size against the scene's places.
 */
LONG	LoadContexte( LONG *savetimerref )
{
	T_OBJET		*ptrobj ;
	T_PATCH		*ptrpatch ;
	T_INCRUST_DISP	*ptrdisp ;
	LONG	n ;
	LONG	wlong ;
	WORD	wword ;
	LONG	num, nbpatches, sizepatches ;
	ULONG	checksum ;
	UBYTE	wbyte ;
	UBYTE	*ptrkey ;
	LONG	keyframe, keytime ;
	WORD	handle, pitch, volleft, volright ;
	LONG	info0, seed ;
	ULONG	longhandle ;

	num = ApplySave ? NumCube : NewCube ;

	// list vars game and cube
	LbaRead( ListFlagCube, MAX_FLAGS_CUBE*sizeof(ListFlagCube[0]) ) ;

	// Globales
	LbaReadByte( NbLittleKeys ) ;

	LbaReadLong( StartXCube ) ;
	LbaReadLong( StartYCube ) ;
	LbaReadLong( StartZCube ) ;

	LbaGetLong( *savetimerref ) ;

	LbaReadByte( NumObjFollow ) ;

	LbaReadByte( SaveComportement ) ;
	LbaReadWord( SaveBeta ) ;

	LbaGetLong( wlong )	;	// Checksum

	if( !ApplySave
	AND (!SceneChecksum( num, &checksum ) OR ((ULONG)wlong != checksum)) )
	{
		ErrSave = TRUE ;
	}

	// Debut incompatibilité des sauvegardes

	LbaReadLong( LastMyFire ) ;
	LbaReadLong( LastMyJoy ) ;
	LbaReadLong( LastJoyFlag ) ;
	LbaReadByte( Bulle ) ;
	LbaReadByte( ActionNormal ) ;
	LbaReadLong( InventoryAction ) ;
	LbaReadLong( MagicBall ) ;
	LbaReadByte( MagicBallType ) ;
	LbaReadWord( MagicBallCount ) ;
	LbaReadByte( FlagClimbing ) ;
	LbaReadLong( StartYFalling ) ;
	LbaRead( &RealFalling, sizeof( RealFalling ) ) ;
	LbaReadLong( StepFalling ) ;
	LbaReadByte( CameraZone ) ;
	LbaReadLong( InvSelect ) ;
	LbaReadWord( NumPingouin ) ;

	// Objets
	LbaGetLong( wlong ) ; // nb d'objets pour EditLba2

	if( (wlong < 1) OR (wlong > MAX_OBJETS) OR (ApplySave AND (wlong != NbObjets)) )
	{
		ErrSave = TRUE ;
	}

	ptrobj = ListObjet ;

	for( n=0; (n < wlong) AND !ErrSave; n++, ptrobj++ )
	{
		LbaReadByte( ptrobj->GenBody ) ;
		LbaReadByte( ptrobj->Col ) ;
		LbaReadWord( ptrobj->GenAnim ) ;
		LbaReadWord( ptrobj->NextGenAnim ) ;
		LbaReadLong( ptrobj->OldPosX ) ;
		LbaReadLong( ptrobj->OldPosY ) ;
		LbaReadLong( ptrobj->OldPosZ ) ;
		LbaReadLong( ptrobj->Info  ) ;
		LbaReadLong( ptrobj->Info1 ) ;
		LbaReadLong( ptrobj->Info2 ) ;
		LbaReadLong( ptrobj->Info3 ) ;

		LbaReadWord( ptrobj->HitBy ) ;
		LbaReadByte( ptrobj->HitForce ) ;
		LbaReadWord( ptrobj->LifePoint ) ;
		LbaReadWord( ptrobj->OptionFlags ) ;

		LbaReadWord( ptrobj->Sprite ) ;
		LbaReadWord( ptrobj->OffsetLabelTrack ) ;

		LbaReadByte( ptrobj->Armure ) ;

		LbaReadWord( ptrobj->Xmin ) ;
		LbaReadWord( ptrobj->Xmax ) ;
		LbaReadWord( ptrobj->Ymin ) ;
		LbaReadWord( ptrobj->Ymax ) ;
		LbaReadWord( ptrobj->Zmin ) ;
		LbaReadWord( ptrobj->Zmax ) ;

		LbaRead( &(ptrobj->RealAngle), sizeof(ptrobj->RealAngle) ) ;

		LbaReadWord( ptrobj->OffsetTrack ) ;
		LbaReadWord( ptrobj->SRot ) ;
		LbaReadWord( ptrobj->OffsetLife ) ;
		LbaReadWord( ptrobj->CarryBy ) ;
		LbaReadByte( ptrobj->Move ) ;
		LbaReadWord( ptrobj->ObjCol ) ;
		LbaReadWord( ptrobj->ZoneSce ) ;
		LbaReadWord( ptrobj->LabelTrack ) ;
		LbaReadWord( ptrobj->MemoLabelTrack ) ;
		LbaReadLong( ptrobj->Flags ) ;
		LbaReadLong( ptrobj->WorkFlags ) ;
		LbaReadWord( ptrobj->DoorWidth ) ;
		LbaReadWord( ptrobj->FlagAnim ) ;
		LbaReadByte( ptrobj->CodeJeu ) ;

		// LBA1: T_OBJ_3D
		LbaReadLong( ptrobj->PosObjX ) ;
		LbaReadLong( ptrobj->PosObjY ) ;
		LbaReadLong( ptrobj->PosObjZ ) ;
		LbaReadLong( ptrobj->Beta ) ;
		LbaGetByte( wbyte ) ;		// un corps
		if( wbyte > 1 )	ErrSave = TRUE ;
		LbaReadLong( ptrobj->Anim ) ;
		ptrkey = PtrSave ;
		LbaGetLong( keyframe ) ;	// LastFrame
		LbaGetLong( keytime ) ;		// LastTimer
		if( keyframe < KEY_BLEND )	ErrSave = TRUE ;
		LbaReadLong( ptrobj->Frame ) ;	// NextFrame
		LbaReadLong( ptrobj->AnimStepX ) ;
		LbaReadLong( ptrobj->AnimStepY ) ;
		LbaReadLong( ptrobj->AnimStepZ ) ;
		LbaReadLong( ptrobj->AnimStepBeta ) ;

		if( ApplySave AND !ErrSave )
		{
			// Body 0: un corps qu'InitLoadedGame retrouve
			ptrobj->Body = wbyte ? 0 : -1 ;

			// un mélange: le corps part de la frame où il va, que
			// le contexte repris donnera
			if( keyframe == KEY_BLEND )
			{
				keyframe = ptrobj->Frame ;
				PokeULong( ptrkey, keyframe ) ;
			}
			LoadKeyFrame[n] = keyframe ;
			LoadKeyTime[n] = keytime ;
		}
	}

	//------------- PATCHES -------------
	ptrpatch = TabPatches ;
	nbpatches = 0 ;
	if( (num >= 0) AND (num < NbIndexPatches) )
	{
		ptrpatch = &TabPatches[IndexPatches[num]] ;
		nbpatches = IndexPatches[num+1]-IndexPatches[num] ;
	}

	LbaGetLong( wlong ) ;	// NbPatches
	if( wlong != nbpatches )	ErrSave = TRUE ;

	LbaGetLong( wlong ) ;	// leur taille
	for( n=0, sizepatches=0; n<nbpatches; n++ )
	{
		sizepatches += ptrpatch[n].Size ;
	}
	if( wlong != sizepatches )	ErrSave = TRUE ;

	for( n=0; (n < nbpatches) AND !ErrSave; n++, ptrpatch++ )
	{
		switch( ptrpatch->Size )
		{
			case 1:
				LbaReadByte( *(PtrScene+ptrpatch->Offset) ) ;
				break ;

			case 2:
				LbaGetWord( wword ) ;
				if( ApplySave AND !ErrSave )	PokeUWord( PtrScene+ptrpatch->Offset, wword ) ;
				break ;

			case 4:
				LbaGetLong( wlong ) ;
				if( ApplySave AND !ErrSave )	PokeULong( PtrScene+ptrpatch->Offset, wlong ) ;
				break ;

			default:
				LbaRead( PtrScene+ptrpatch->Offset, ptrpatch->Size ) ;
		}
	}
	//-----------------------------------

	// Extras
	LbaGetByte( wbyte ) ;
	if( wbyte != MAX_EXTRAS )	ErrSave = TRUE ;
	LbaRead( ListExtra, sizeof(T_EXTRA)*MAX_EXTRAS ) ;

	// Zones
	LbaGetLong( wlong ) ;	// pour EditLba2
	if( (wlong < 0) OR (wlong > MAX_ZONES) OR (ApplySave AND (wlong != NbZones)) )
	{
		ErrSave = TRUE ;
	}

	for( n=0; (n < wlong) AND !ErrSave; n++ )
	{
		LbaReadLong( ListZone[n].Info1 ) ;
		LbaReadLong( ListZone[n].Info2 ) ;
		LbaReadLong( ListZone[n].Info3 ) ;
	}

	// Incrust
	LbaGetByte( wbyte ) ;
	if( wbyte > MAX_INCRUST_DISP )	ErrSave = TRUE ;
	LbaRead( ListIncrustDisp, sizeof(T_INCRUST_DISP)*wbyte ) ;
	/*
	 * FEATURE: a number's place, counted as on a screen of 640x480, put
	 * back on this one. The port's own.
	 */
	if( ApplySave AND !ErrSave )
	{
		ptrdisp = ListIncrustDisp ;
		for( n=0; n<wbyte; n++, ptrdisp++ )
		{
			if( ptrdisp->Type == INCRUST_NUM )
			{
				ptrdisp->X += CENTRE_ECRAN_X-320 ;
			}
		}
	}

	// Infos camera
	LbaReadLong( WorldXCube ) ;
	LbaReadLong( WorldYCube ) ;
	LbaReadLong( WorldZCube ) ;

	// LBA1
	LbaReadWord( GameChoice ) ;
	LbaReadWord( FlagWater ) ;
	LbaReadWord( TextLig ) ;
	LbaReadWord( BetaUsedObj ) ;
	LbaReadWord( ZoneGrm ) ;
	LbaReadWord( IndexGrm ) ;
	LbaReadWord( FlagPalettePcx ) ;
	LbaRead( PalettePcx, 768 ) ;
	LbaReadWord( SamplePlayed ) ;
	LbaReadLong( TimerNextAmbiance ) ;
	LbaReadWord( BigSampleRepeat ) ;
	LbaReadWord( MemoComportement ) ;

	LoadGrid() ;

	LbaGetByte( wbyte ) ;
	for( n=0; (n < wbyte) AND !ErrSave; n++ )
	{
		LbaGetWord( handle ) ;
		LbaGetWord( pitch ) ;
		LbaGetWord( volleft ) ;
		LbaGetWord( volright ) ;
		LbaGetLong( info0 ) ;
		if( ApplySave AND !ErrSave )
		{
			longhandle = HQ_MixSample( handle, pitch, 0, volleft, volright ) ;
			if( info0 != -1 )	WaveGiveInfo0( longhandle, info0 ) ;
			/*
			 * FIXED: the save holds no sample's actor, and a loop
			 * kept for none is stopped by the scene's end alone: the
			 * protopack's hum, Twinsen's in flight, is his, so that
			 * leaving the protopack stops it.
			 */
			KeepLoopObj( ((handle == 156) AND (Comportement == C_PROTOPACK)) ? NUM_PERSO : -1, handle, longhandle ) ;
		}
	}

	LbaGetLong( seed ) ;

	if( ErrSave OR (PtrSave != EndSave) )	return TRUE ;

	if( ApplySave )	srand( seed ) ;

	return FALSE ;
}
