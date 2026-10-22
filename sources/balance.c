#include <stdint.h>
#include "../lib386/lib_sys/adeline.h"
#include "../lib386/lib_3d/lib_3d.h"
#include "balance.h"

/*
 * Balance is balance.asm's Balance: vol shared between the two sides by
 * the sine and cosine of bal, a quarter turn being 256, read as unsigned
 * words. The right gets the sine.
 */
void
Balance(uint32_t bal, uint32_t vol, uint32_t *left, uint32_t *right)
{
	*right = (uint16_t)P_SinTab[bal] * vol >> 14;
	*left = (uint16_t)P_SinTab[bal+256] * vol >> 14;
}

/* BalanceWord is balance.asm's BalanceWord: Balance into words. */
void
BalanceWord(uint32_t bal, uint32_t vol, uint16_t *left, uint16_t *right)
{
	*right = (uint16_t)P_SinTab[bal] * vol >> 14;
	*left = (uint16_t)P_SinTab[bal+256] * vol >> 14;
}
