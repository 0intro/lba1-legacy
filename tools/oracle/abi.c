#include <stddef.h>
#include <stdint.h>
#include "abi.h"

/*
 * Calls to the assembly that keep the convention whatever it does, as
 * asmoracle and waveoracle make them.
 * abicall(fn, n, args) calls fn with the n words at args on the stack,
 * as cdecl, and returns its EAX. Before the call EBX, ESI, EDI and EBP
 * hold known values. After it, abibroke has a bit set for each of them
 * that changed (1, 2, 4, 8), for a stack not as it was (16) and for the
 * direction flag left set (32), and all are put back, so that a routine
 * that breaks the convention does not break its caller here.
 */

uintptr_t	abifn;
uint32_t	abiebp, abiesp, abibroke;

__asm__(
	".text\n"
	".globl abicall\n"
	"abicall:\n"
	"	pushl %ebp\n"
	"	movl %esp, %ebp\n"
	"	pushl %ebx\n"
	"	pushl %esi\n"
	"	pushl %edi\n"
	"	movl 8(%ebp), %eax\n"
	"	movl %eax, abifn\n"
	"	movl 12(%ebp), %ecx\n"
	"	movl 16(%ebp), %edx\n"
	"1:	testl %ecx, %ecx\n"
	"	jz 2f\n"
	"	pushl -4(%edx,%ecx,4)\n"
	"	decl %ecx\n"
	"	jmp 1b\n"
	"2:	movl %ebp, abiebp\n"
	"	movl %esp, abiesp\n"
	"	movl $0x11111111, %ebx\n"
	"	movl $0x22222222, %esi\n"
	"	movl $0x33333333, %edi\n"
	"	movl $0x44444444, %ebp\n"
	"	cld\n"
	"	call *abifn\n"
	"	xorl %ecx, %ecx\n"
	"	cmpl $0x11111111, %ebx\n"
	"	je 3f\n"
	"	orl $1, %ecx\n"
	"3:	cmpl $0x22222222, %esi\n"
	"	je 4f\n"
	"	orl $2, %ecx\n"
	"4:	cmpl $0x33333333, %edi\n"
	"	je 5f\n"
	"	orl $4, %ecx\n"
	"5:	cmpl $0x44444444, %ebp\n"
	"	je 6f\n"
	"	orl $8, %ecx\n"
	"6:	cmpl abiesp, %esp\n"
	"	je 7f\n"
	"	orl $16, %ecx\n"
	"7:	pushfl\n"
	"	popl %edx\n"
	"	testl $0x400, %edx\n"
	"	jz 8f\n"
	"	orl $32, %ecx\n"
	"8:	movl %ecx, abibroke\n"
	"	cld\n"
	"	movl abiebp, %ebp\n"
	"	leal -12(%ebp), %esp\n"
	"	popl %edi\n"
	"	popl %esi\n"
	"	popl %ebx\n"
	"	popl %ebp\n"
	"	ret\n"
);

/*
 * regcall(fn, r) calls fn as the assembly calls its register entries:
 * with EAX to EBP as r holds them, and the direction flag clear. What
 * fn leaves in them goes back into r. ESP and the registers the
 * convention keeps are put back.
 */
uintptr_t	regfn;
Regs	*regp;
uint32_t	regebp;

__asm__(
	".text\n"
	".globl regcall\n"
	"regcall:\n"
	"	pushl %ebp\n"
	"	movl %esp, %ebp\n"
	"	pushl %ebx\n"
	"	pushl %esi\n"
	"	pushl %edi\n"
	"	movl 8(%ebp), %eax\n"
	"	movl %eax, regfn\n"
	"	movl 12(%ebp), %eax\n"
	"	movl %eax, regp\n"
	"	movl %ebp, regebp\n"
	"	movl 4(%eax), %ebx\n"
	"	movl 8(%eax), %ecx\n"
	"	movl 12(%eax), %edx\n"
	"	movl 16(%eax), %esi\n"
	"	movl 20(%eax), %edi\n"
	"	movl 24(%eax), %ebp\n"
	"	movl 0(%eax), %eax\n"
	"	cld\n"
	"	call *regfn\n"
	"	pushl %eax\n"
	"	movl regp, %eax\n"
	"	popl 0(%eax)\n"
	"	movl %ebx, 4(%eax)\n"
	"	movl %ecx, 8(%eax)\n"
	"	movl %edx, 12(%eax)\n"
	"	movl %esi, 16(%eax)\n"
	"	movl %edi, 20(%eax)\n"
	"	movl %ebp, 24(%eax)\n"
	"	cld\n"
	"	movl regebp, %ebp\n"
	"	leal -12(%ebp), %esp\n"
	"	popl %edi\n"
	"	popl %esi\n"
	"	popl %ebx\n"
	"	popl %ebp\n"
	"	ret\n"
);

/* Whether the last abicall kept the convention, and if it did not, what it broke. */
int
abiok(char *what)
{
	if (abibroke == 0)
		return 1;
	fail("%s: the assembly broke the convention:%s%s%s%s%s%s", what,
		abibroke & 1 ? " EBX" : "", abibroke & 2 ? " ESI" : "",
		abibroke & 4 ? " EDI" : "", abibroke & 8 ? " EBP" : "",
		abibroke & 16 ? " ESP" : "", abibroke & 32 ? " DF" : "");
	return 0;
}
