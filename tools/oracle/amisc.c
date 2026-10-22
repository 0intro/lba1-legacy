#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "asmoracle.h"

/*
 * The small modules that need no screen: balance's Balance and
 * BalanceWord, buffer_a's MovMem, RazMem and CompBuf, hqr's
 * HQR_GiveIndex, and p_sintab's table. The game balances a sample over
 * the screen's width, 0 to 255, at a volume of up to 724 (GiveBalance),
 * copies with MovMem between buffers apart, and keeps each index once in
 * an HQR's list (HQR_Get adds a block when HQR_GiveIndex gives NULL).
 */

enum
{
	N = 4096,	/* buffer_a's buffers */
	Nblock = 300,	/* HQR_GiveIndex's list */
	Block = 14,	/* T_HQR_BLOC */
	Maxvol = 724,	/* the game's loudest balanced sample */
};

void	Balance(uint32_t, uint32_t, uint32_t*, uint32_t*);
void	BalanceWord(uint32_t, uint32_t, uint16_t*, uint16_t*);
void	MovMem(void*, void*, uint32_t);
void	RazMem(void*, uint32_t);
int32_t	CompBuf(void*, void*, uint32_t);
void	*HQR_GiveIndex(uint16_t, uint16_t, void*);

extern int16_t	P_SinTab[1024];

static void
fill(uint8_t *p, uint32_t n)
{
	uint32_t i;

	for (i = 0; i < n; i++)
		p[i] = rnd();
}

void
testsmall(void)
{
	static uint8_t a[N], c[N], x[N], y[N], list[Nblock*Block];
	uintptr_t bal, balw, mov, raz, cmp, give;
	uint32_t b, vol, al, ar, cl, cr, s, t, n;
	uint16_t awl, awr, cwl, cwr, index, nb;
	int32_t ra, rc;
	uint8_t *pa, *pc;
	int r, i;

	bal = lookup("Balance");
	balw = lookup("BalanceWord");
	mov = lookup("MovMem");
	raz = lookup("RazMem");
	cmp = lookup("CompBuf");
	give = lookup("HQR_GiveIndex");
	for (r = 0; r < Rounds; r++) {
		b = r < 768 ? (uint32_t)r : rndn(768);
		vol = r%4 == 0 ? rnd() : rndn(Maxvol+1);
		acall(bal, 4, b, vol, (int32_t)(uintptr_t)&al, (int32_t)(uintptr_t)&ar);
		Balance(b, vol, &cl, &cr);
		acall(balw, 4, b, vol, (int32_t)(uintptr_t)&awl, (int32_t)(uintptr_t)&awr);
		BalanceWord(b, vol, &cwl, &cwr);
		if (al != cl || ar != cr || awl != cwl || awr != cwr) {
			fail("Balance %u %u: ours %u %u %u %u, the assembly's %u %u %u %u", b, vol,
				cl, cr, cwl, cwr, al, ar, awl, awr);
			break;
		}

		fill(a, N);
		memcpy(c, a, N);
		s = rndn(N/2);
		t = rndn(N/2);
		n = rndn(N/2);
		acall(mov, 3, (int32_t)(uintptr_t)(a+s), (int32_t)(uintptr_t)(a+t), n);
		MovMem(c+s, c+t, n);
		if (memcmp(a, c, N) != 0) {
			fail("MovMem %u bytes from %u to %u: ours and the assembly's differ", n, s, t);
			break;
		}

		t = rndn(N/2);
		n = rndn(N/2);
		acall(raz, 2, (int32_t)(uintptr_t)(a+t), n);
		RazMem(c+t, n);
		if (memcmp(a, c, N) != 0) {
			fail("RazMem %u bytes at %u: ours and the assembly's differ", n, t);
			break;
		}

		fill(x, N);
		memcpy(y, x, N);
		n = rndn(N);
		if (n > 0 && rnd()&1)
			y[rndn(n)] ^= 1 + rndn(255);
		ra = acall(cmp, 3, (int32_t)(uintptr_t)x, (int32_t)(uintptr_t)y, n);
		rc = CompBuf(x, y, n);
		if (ra != rc) {
			fail("CompBuf %u bytes: ours %d, the assembly's %d", n, rc, ra);
			break;
		}

		fill(list, sizeof list);
		nb = rndn(Nblock+1);
		for (i = 0; i < nb; i++) {
			list[i*Block] = r%2 == 0 ? (uint32_t)i : rndn(40);
			list[i*Block+1] = r%2 == 0 ? (uint32_t)i>>8 : rnd()&1;
		}
		index = r%2 == 0 ? rndn(Nblock+8) : rndn(48) | (rnd()&1)<<8;
		pa = (uint8_t*)(uintptr_t)acall(give, 3, index, nb, (int32_t)(uintptr_t)list);
		pc = HQR_GiveIndex(index, nb, list);
		if (pa != pc) {
			fail("HQR_GiveIndex %u of %u: ours and the assembly's differ", index, nb);
			break;
		}
	}
	if (memcmp((void*)lookup("P_SinTab"), P_SinTab, sizeof P_SinTab) != 0)
		fail("P_SinTab: ours and the assembly's differ");
	printf("small: %d rounds\n", r);
}
