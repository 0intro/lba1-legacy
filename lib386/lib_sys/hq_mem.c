#include <stdio.h>
#include <stdlib.h>
#ifndef	DOS
#include <string.h>
#endif
#include <fcntl.h>

#include "adeline.h"
#include "lib_sys.h"

/*══════════════════════════════════════════════════════════════════════════*
	     █   █ █▀▀▀█       █▄ ▄█ █▀▀▀▀ █▄ ▄█ █▀▀▀█ █▀▀▀█ █  ▄▀
	     ██▀▀█ ██ ▄█       ██▀ █ ██▀▀  ██▀ █ ██  █ ██▀█▀ ██▀
	     ▀▀  ▀ ▀▀▀▀  ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀▀ ▀▀  ▀ ▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

ULONG	Size_HQM_Memory = 0 ;
ULONG	Size_HQM_Free = 0 ;
UBYTE	*Ptr_HQM_Memory = 0 ;
UBYTE	*Ptr_HQM_Next = 0 ;

#pragma pack(push, 2)	/* every structure laid out as Watcom's /zp2 did */
typedef	struct	{	ULONG	Id ;
			ULONG	Size ;
			void	**Ptr ;		}	HQM_HEADER ;


/*══════════════════════════════════════════════════════════════════════════*/
// Init le buffer global

LONG	HQM_Init_Memory( ULONG size )
{
	if( !Ptr_HQM_Memory )
	{
		Ptr_HQM_Memory = Malloc( size + 500 ) ;	// recover area
		if( Ptr_HQM_Memory )
		{
			Size_HQM_Memory = size ;
			Size_HQM_Free = size ;
			Ptr_HQM_Next = Ptr_HQM_Memory ;
			return TRUE ;
		}
	}
	return FALSE ;
}

/*══════════════════════════════════════════════════════════════════════════*/
// free le buffer global

void	HQM_Clear_Memory()
{
	if( Ptr_HQM_Memory )
	{
		Free( Ptr_HQM_Memory ) ;
		Size_HQM_Free = 0 ;
	}
}

/*══════════════════════════════════════════════════════════════════════════*/
// alloue un bloc de memoire

#ifdef	DOS
LONG	HQM_Alloc( ULONG size, void *ptrptr )
{
	void	**ptr = ptrptr ;

	if( !Ptr_HQM_Memory )
	{
		*ptr = 0 ;
		return FALSE ;
	}

	if( size <= (Size_HQM_Free + sizeof( HQM_HEADER )) )
	{
		*ptr = Ptr_HQM_Next + sizeof( HQM_HEADER ) ;

		((HQM_HEADER*)Ptr_HQM_Next)->Id   = 0x12345678 ;
		((HQM_HEADER*)Ptr_HQM_Next)->Size = size ;
		((HQM_HEADER*)Ptr_HQM_Next)->Ptr  = ptr ;

		Ptr_HQM_Next  += size + sizeof( HQM_HEADER ) ;
		Size_HQM_Free -= size + sizeof( HQM_HEADER ) ;

		return TRUE ;
	}
	*ptr = 0 ;
	return FALSE ;	// pas assez de place
}
#else
// les blocs sont à n'importe quelle adresse et le pointeur peut être
// dans une structure compactée: on les copie

LONG	HQM_Alloc( ULONG size, void *ptrptr )
{
	void		**ptr = ptrptr ;
	UBYTE		*ptrbloc = 0 ;
	HQM_HEADER	header ;

	if( !Ptr_HQM_Memory )
	{
		memcpy( ptr, &ptrbloc, sizeof( ptrbloc ) ) ;
		return FALSE ;
	}

	if( size <= (Size_HQM_Free + sizeof( HQM_HEADER )) )
	{
		ptrbloc = Ptr_HQM_Next + sizeof( HQM_HEADER ) ;
		memcpy( ptr, &ptrbloc, sizeof( ptrbloc ) ) ;

		header.Id   = 0x12345678 ;
		header.Size = size ;
		header.Ptr  = ptr ;
		memcpy( Ptr_HQM_Next, &header, sizeof( HQM_HEADER ) ) ;

		Ptr_HQM_Next  += size + sizeof( HQM_HEADER ) ;
		Size_HQM_Free -= size + sizeof( HQM_HEADER ) ;

		return TRUE ;
	}
	memcpy( ptr, &ptrbloc, sizeof( ptrbloc ) ) ;
	return FALSE ;	// pas assez de place
}
#endif

/*══════════════════════════════════════════════════════════════════════════*/
// free tous les blocs dans le buffer global

void	HQM_Free_All()
{
	if( Ptr_HQM_Memory )
	{
		Ptr_HQM_Next = Ptr_HQM_Memory ;
		Size_HQM_Free = Size_HQM_Memory ;
	}
}

/*══════════════════════════════════════════════════════════════════════════*/
// resize le dernier bloc de memoire

#ifdef	DOS
void	HQM_Shrink_Last( void *ptr, ULONG newsize )
{
	HQM_HEADER	*ptrh ;
	ULONG		deltasize ;

	if( !Ptr_HQM_Memory )	return ;

	ptrh = (HQM_HEADER*)((UBYTE*)ptr - sizeof( HQM_HEADER )) ;

	if( ptrh->Id != 0x12345678 )	return ;	// erreur grave

	deltasize = ptrh->Size - newsize ;

	ptrh->Size -= deltasize ;

	Ptr_HQM_Next  -= deltasize ;
	Size_HQM_Free += deltasize ;
}
#else
void	HQM_Shrink_Last( void *ptr, ULONG newsize )
{
	HQM_HEADER	header ;
	ULONG		deltasize ;

	if( !Ptr_HQM_Memory )	return ;

	memcpy( &header, (UBYTE*)ptr - sizeof( HQM_HEADER ), sizeof( HQM_HEADER ) ) ;

	if( header.Id != 0x12345678 )	return ;	// erreur grave

	deltasize = header.Size - newsize ;

	header.Size -= deltasize ;
	memcpy( (UBYTE*)ptr - sizeof( HQM_HEADER ), &header, sizeof( HQM_HEADER ) ) ;

	Ptr_HQM_Next  -= deltasize ;
	Size_HQM_Free += deltasize ;
}
#endif

/*══════════════════════════════════════════════════════════════════════════*/
// libere un bloc de memoire et bouche le trou (remap les ptrs)

#ifdef	DOS
void	HQM_Free( void *ptr )
{
	HQM_HEADER	*ptrh ;
	UBYTE		*ptrs, *ptrd, *ptrm ;
	ULONG		delsize, movesize ;

	if( !Ptr_HQM_Memory )	return ;

	ptrs = ptrd = (UBYTE*)ptr - sizeof( HQM_HEADER ) ;

	ptrh = (HQM_HEADER*)ptrd ;
	if( ptrh->Id != 0x12345678 )	return ;	// erreur grave

	delsize = sizeof( HQM_HEADER ) + ptrh->Size ;
	ptrs = ptrd + delsize ;
	movesize = (ULONG)(Ptr_HQM_Next - ptrs) ;

	ptrm = ptrs ;
	while( ptrm < Ptr_HQM_Next )
	{
		ptrh = (HQM_HEADER*)ptrm ;
		*(UBYTE*)(ptrh->Ptr) = *(UBYTE*)(ptrh->Ptr) - delsize ;
		ptrm += ptrh->Size + sizeof( HQM_HEADER ) ;
	}

	memmove( ptrd, ptrs, movesize ) ;

	Ptr_HQM_Next  -= delsize ;
	Size_HQM_Free += delsize ;
}
#else
void	HQM_Free( void *ptr )
{
	HQM_HEADER	header ;
	UBYTE		*ptrs, *ptrd, *ptrm ;
	ULONG		delsize, movesize ;

	if( !Ptr_HQM_Memory )	return ;

	ptrs = ptrd = (UBYTE*)ptr - sizeof( HQM_HEADER ) ;

	memcpy( &header, ptrd, sizeof( HQM_HEADER ) ) ;
	if( header.Id != 0x12345678 )	return ;	// erreur grave

	delsize = sizeof( HQM_HEADER ) + header.Size ;
	ptrs = ptrd + delsize ;
	movesize = (ULONG)(Ptr_HQM_Next - ptrs) ;

	ptrm = ptrs ;
	while( ptrm < Ptr_HQM_Next )
	{
		memcpy( &header, ptrm, sizeof( HQM_HEADER ) ) ;
		*(UBYTE*)(header.Ptr) = *(UBYTE*)(header.Ptr) - delsize ;
		ptrm += header.Size + sizeof( HQM_HEADER ) ;
	}

	memmove( ptrd, ptrs, movesize ) ;

	Ptr_HQM_Next  -= delsize ;
	Size_HQM_Free += delsize ;
}
#endif

/*══════════════════════════════════════════════════════════════════════════*/
// test la cohérence du buffer global

#ifdef	DOS
LONG	HQM_Check()
{
	HQM_HEADER	*ptrh ;
	UBYTE		*ptr ;

	if( !Ptr_HQM_Memory )	return FALSE ;

	ptr = Ptr_HQM_Memory ;

	while( ptr < Ptr_HQM_Next )
	{
		ptrh = (HQM_HEADER*)ptr ;

		if( ptrh->Id != 0x12345678 )	return FALSE ;

		ptr += ptrh->Size + sizeof( HQM_HEADER ) ;
	}
	return TRUE ;
}
#else
LONG	HQM_Check()
{
	HQM_HEADER	header ;
	UBYTE		*ptr ;

	if( !Ptr_HQM_Memory )	return FALSE ;

	ptr = Ptr_HQM_Memory ;

	while( ptr < Ptr_HQM_Next )
	{
		memcpy( &header, ptr, sizeof( HQM_HEADER ) ) ;

		if( header.Id != 0x12345678 )	return FALSE ;

		ptr += header.Size + sizeof( HQM_HEADER ) ;
	}
	return TRUE ;
}
#endif
#pragma pack(pop)
