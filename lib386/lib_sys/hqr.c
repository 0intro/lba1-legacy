#include <stddef.h>
#include <stdint.h>
#include "adeline.h"
#include "lib_sys.h"

/* HQR_GiveIndex is hqr.asm's HQR_GiveIndex: the last of the nbindex blocks at list whose index is index, or NULL. */
T_HQR_BLOC*
HQR_GiveIndex(uint16_t index, uint16_t nbindex, void *list)
{
	T_HQR_BLOC *b;

	for (b = (T_HQR_BLOC*)list + nbindex; b != list; )
		if ((--b)->Index == index)
			return b;
	return NULL;
}
