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
	USED(name);
	/*
	 * FEATURE: LbaWin's settings, Hit walls among them, read once the
	 * configuration file's places are set, as the Classic 3.2.3 reads
	 * them after its InitConfigFile.
	 */
	ReadLbaWinSettings();
}
