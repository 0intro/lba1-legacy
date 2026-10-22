#include <stdint.h>
#include "adeline.h"
#include "lib_sys.h"

/*
 * FEATURE: LbaWin's settings, one number of lba.cfg: 1 keep original
 * game play, 2 Hit walls, 4 the action key in every behaviour, 8 no
 * screen saver, 0x10 a fade at each change of scene, 0x20 moves
 * relative to the screen, 0x40 no Alt+F4, 0x80 no Windows keys. The
 * port reads 1, the original game play at each start, 2, the bump into
 * walls, which the option menu switches,
 * 4, W as the action, 0x10, the fades, 0x20, the moves relative to the
 * screen, and 0x40 and 0x80, Alt+F4 and the Windows keys refused. Taken
 * from the Classic 3.2.3, as LbaWin 0.7.4 made it.
 */

/*
 * LbaWinSettings is TLBA1C.exe 0x819a74 (LbaWin.exe 0x42f098): the
 * settings, the Classic's 0xd4, sys/lba.cfg's 212.
 */
int32_t	LbaWinSettings = 0xd4;

/*
 * ForceOriginalGamePlay is TLBA1C.exe 0x4268e0 (LbaWin.exe 0x402df0):
 * keep original game play sets Hit walls and the fades, and clears the
 * action key.
 */
void
ForceOriginalGamePlay(void)
{
	if (LbaWinSettings & 1)
		LbaWinSettings = (LbaWinSettings & ~4) | 0x12;
}

/*
 * ReadLbaWinSettings is TLBA1C.exe 0x426900 (LbaWin.exe 0x402e10): the
 * settings of lba.cfg, the default kept when the key is missing.
 */
void
ReadLbaWinSettings(void)
{
	Def_ReadValue2((UBYTE*)PathConfigFile, (UBYTE*)"LbaWinSettings", &LbaWinSettings);
	ForceOriginalGamePlay();
}

/*
 * WriteLbaWinSettings is TLBA1C.exe 0x426920 (LbaWin.exe 0x402e30): the
 * settings into lba.cfg.
 */
void
WriteLbaWinSettings(void)
{
	Def_WriteValue((UBYTE*)PathConfigFile, (UBYTE*)"LbaWinSettings", LbaWinSettings);
}

/* ReadConfigValue is TLBA1C.exe 0x4234c0: the value of key in lba.cfg, or dflt where the key is missing. */
int32_t
ReadConfigValue(char *key, int32_t dflt)
{
	int32_t v;

	if (Def_ReadValue2((UBYTE*)PathConfigFile, (UBYTE*)key, &v))
		return v;
	return dflt;
}
