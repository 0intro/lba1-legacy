#include <stddef.h>
#include <stdint.h>
#include "dat.h"
#include "fns.h"

/*
 * readplatform: the port's keys of the configuration file name, read
 * once its places are set, each the value sys/lba.cfg holds where the
 * file leaves it out (cfgtest holds the two to each other).
 */
void
readplatform(char *name)
{
	/*
	 * FEATURE: LbaWin's settings, Hit walls among them, read once the
	 * configuration file's places are set, as the Classic 3.2.3 reads
	 * them after its InitConfigFile.
	 */
	ReadLbaWinSettings();
	/*
	 * FEATURE: the screen on the whole display or in a window, as
	 * LbaWinFullScreen last left it, the default kept when the key is
	 * missing, read after LbaWin's settings as the Classic 3.2.3's
	 * InitWindows (TLBA1C.exe 0x427a90) reads it.
	 */
	Def_ReadValue2((unsigned char*)name, (unsigned char*)"LbaWinFullScreen", &FlagFullScreen);
}
