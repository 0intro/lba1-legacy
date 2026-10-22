/* abi.c: calls to the assembly by its conventions */

typedef struct Regs Regs;

/* the registers of a call to an assembly's register entry (regcall) */
struct Regs
{
	uint32_t	eax;
	uint32_t	ebx;
	uint32_t	ecx;
	uint32_t	edx;
	uint32_t	esi;
	uint32_t	edi;
	uint32_t	ebp;
};

int32_t	abicall(uintptr_t, int, int32_t*);
int	abiok(char*);
void	regcall(uintptr_t, Regs*);
extern uint32_t	abibroke;	/* what the last abicall broke: EBX 1, ESI 2, EDI 4, EBP 8, ESP 16, DF 32 */

/* the oracle's: a failure reported, the run going on */
void	fail(char*, ...);
