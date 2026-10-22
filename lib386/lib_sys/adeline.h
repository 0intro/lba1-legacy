/*══════════════════════════════════════════════════════════════════════════*
		   █▀▀▀█ █▀▀▀▄ █▀▀▀▀ █      █    ██▄ █ █▀▀▀▀
		   ██▀▀█ ██  █ ██▀▀  ██     ██   ██▀██ ██▀▀
		   ▀▀  ▀ ▀▀▀▀  ▀▀▀▀▀ ▀▀▀▀▀  ▀▀   ▀▀  ▀ ▀▀▀▀▀ 386
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

#include	<stdint.h>

#define	ULONG	uint32_t
#define	LONG	int32_t
#define	UWORD	uint16_t
#define	WORD	int16_t
#define	UBYTE	unsigned char
#define	BYTE	signed char

/*──────────────────────────────────────────────────────────────────────────*/
/* a WORD or a LONG of a buffer of bytes at any address, as the 386 read and
   wrote them: low byte first, where it lies for DOS, as the 1994 code did,
   a byte at a time elsewhere */

#ifdef	DOS
#define	PeekUWord( ptr )	(*(UWORD *)(ptr))
#define	PeekWord( ptr )		(*(WORD *)(ptr))
#define	PeekULong( ptr )	(*(ULONG *)(ptr))
#define	PokeUWord( ptr, val )	(*(UWORD *)(ptr) = (val))
#define	PokeULong( ptr, val )	(*(ULONG *)(ptr) = (val))
#else
static	inline	UWORD	PeekUWord( const void *ptr )
{
	const UBYTE	*p = ptr ;

	return (UWORD)(p[0] | p[1]<<8) ;
}

static	inline	WORD	PeekWord( const void *ptr )
{
	return (WORD)PeekUWord( ptr ) ;
}

static	inline	ULONG	PeekULong( const void *ptr )
{
	const UBYTE	*p = ptr ;

	return (ULONG)p[0] | (ULONG)p[1]<<8 | (ULONG)p[2]<<16 | (ULONG)p[3]<<24 ;
}

static	inline	void	PokeUWord( void *ptr, UWORD val )
{
	UBYTE	*p = ptr ;

	p[0] = (UBYTE)val ;
	p[1] = (UBYTE)(val>>8) ;
}

static	inline	void	PokeULong( void *ptr, ULONG val )
{
	UBYTE	*p = ptr ;

	p[0] = (UBYTE)val ;
	p[1] = (UBYTE)(val>>8) ;
	p[2] = (UBYTE)(val>>16) ;
	p[3] = (UBYTE)(val>>24) ;
}
#endif

/*──────────────────────────────────────────────────────────────────────────*/

#define	AND		&&
#define	OR		||

/*──────────────────────────────────────────────────────────────────────────*/

#define	TRUE	1
#define	FALSE	0

/*──────────────────────────────────────────────────────────────────────────*/
/*──────────────────────────────────────────────────────────────────────────*/

























