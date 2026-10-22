/*
			MALLOC (c) Adeline 1993

		Functions:

			- Malloc
			- Free
			- Mshrink
*/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

#include "adeline.h"
#include "lib_sys.h"

ULONG	LoadMallocFileSize ;

/*──────────────────────────────────────────────────────────────────────────*/
void	*LoadMalloc( char *name )
{
	LONG	handle	;
	ULONG	sizer	;
	void	*pt	;

	LoadMallocFileSize = FileSize( name )		;
	if ( !LoadMallocFileSize )	return( 0 )	;

	pt = Malloc( LoadMallocFileSize )		;
	if ( pt == 0 )	return( 0 )	;

	handle = OpenRead( name )	;
	/*
	 * FIXED: LBA.EXE keeps the block when the open or the read fails,
	 * so that each failure loses the file's size of memory. Free it.
	 * Taken from DotEmu's Android 1.06.
	 */
#ifdef	FIXES
	if ( !handle)
	{
		Free( pt )		;
		return( 0 )	;
	}
#else
	if ( !handle)	return( 0 )	;
#endif

	sizer = Read( handle, pt, LoadMallocFileSize );
	if ( sizer != LoadMallocFileSize )
	{
		Close( handle ) ;
#ifdef	FIXES
		Free( pt ) ;
#endif
		return( 0 );
	}

	Close( handle )			;
	return( pt )			;
}
/*──────────────────────────────────────────────────────────────────────────*/
