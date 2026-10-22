#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

#include "adeline.h"
#define	HQ_R_M		/* lib_sys.h leaves HQRM_Load to this file */
#include "lib_sys.h"

#define	RECOVER_AREA	500

/*──────────────────────────────────────────────────────────────────────────*/

#pragma pack(push, 2)	/* every structure laid out as Watcom's /zp2 did */
typedef	struct	{	ULONG	SizeFile ;
			ULONG	CompressedSizeFile ;
			WORD	CompressMethod ;	/* 0 stored */
							/* 1 LZS */
		}	T_HEADER ;

void	Expand( void *ptrsourcecomp, void *ptrblocdest, ULONG sizefile ) ;

/*──────────────────────────────────────────────────────────────────────────*/

#ifdef	DOS
LONG	HQRM_Load( UBYTE *name, UWORD index, void **ptrptr )
#else
LONG	HQRM_Load( UBYTE *name, UWORD index, void *ptr )
#endif
{
#ifndef	DOS
	void		**ptrptr = ptr ;
#endif
	LONG		handle ;
	UWORD		nbbloc ;
	ULONG		buffer ;
	ULONG		seekindex ;
#ifndef	DOS
	UBYTE		*ptrbloc ;
#endif
	UBYTE		*ptrdecomp ;
	T_HEADER	header ;

	handle = OpenRead( name ) ;
	if( !handle )	return FALSE ;

	Read( handle, &buffer, 4L ) ;
	nbbloc = (UWORD)(buffer / 4L) ;

	if( index >= nbbloc )	return FALSE ;

	Seek( handle, index * 4L, SEEK_START ) ;
	Read( handle, &seekindex, 4L ) ;

	Seek( handle, seekindex, SEEK_START ) ;
	Read( handle, &header, sizeof( header ) ) ;

	if( !HQM_Alloc( header.SizeFile, ptrptr ) ) // il y a deja 500 de plus
	{
		return FALSE ;
	}
#ifndef	DOS
	memcpy( &ptrbloc, ptrptr, sizeof( ptrbloc ) ) ;	// may lie in a packed structure
#endif

	switch( header.CompressMethod )
	{
		case 0:		/* Stored */
#ifdef	DOS
			Read(	handle,	*ptrptr, header.SizeFile ) ;
#else
			Read(	handle,	ptrbloc, header.SizeFile ) ;
#endif
			break ;

		case 1:		/* LZS */
#ifdef	DOS
			ptrdecomp = (UBYTE*)(*ptrptr) + header.SizeFile - header.CompressedSizeFile + RECOVER_AREA ;
			Read(	handle, ptrdecomp, header.CompressedSizeFile ) ;
			Expand( ptrdecomp, *ptrptr, header.SizeFile ) ;
#else
			ptrdecomp = ptrbloc + header.SizeFile - header.CompressedSizeFile + RECOVER_AREA ;
			Read(	handle, ptrdecomp, header.CompressedSizeFile ) ;
			Expand( ptrdecomp, ptrbloc, header.SizeFile ) ;
#endif
			break ;

		default:
#ifdef	DOS
			HQM_Free( *ptrptr ) ;
#else
			HQM_Free( ptrbloc ) ;
#endif
			return FALSE ;	/* UnKnown version */
	}

	Close( handle ) ;

	return header.SizeFile ;
}

#pragma pack(pop)
