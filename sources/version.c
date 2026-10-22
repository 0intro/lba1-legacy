#include 	"c_extern.h"

#include	<process.h>

#ifdef	DEMO
char	*Version	= "LBA Demo ("
			  __DATE__
			  " / "
			  __TIME__
			  ")\n"		;
#else
// la date et l'heure de LBA.EXE (0x40004)
char	*Version	= "LBA/Relentless ("
			  "Oct 14 1994"
			  " / "
			  "11:43:15"
			  ")\n"		;
#endif