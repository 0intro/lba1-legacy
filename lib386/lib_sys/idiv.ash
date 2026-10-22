;*--------------------------------------------------------------------------*
; IDIV16 divisor: idiv of DX:AX by a word, but in 32 bits. AX and DX take
; the quotient's and the remainder's low words, and the high words of EAX
; and EDX stay as idiv leaves them, where idiv faults on a quotient past
; 16 bits. The divisor is a word register or a word in memory, AX, CX and
; DX read as they were before the macro.
; LBA.EXE's code (make CC=watcom) takes the idiv itself.

IDIV16		macro	divisor
	ifdef	FIXES
		push	eax
		push	ecx
		push	edx
		movzx	eax, ax
		shl	edx, 16
		or	eax, edx
	ifidni	<divisor>, <ax>
		movsx	ecx, word ptr[esp+8]
	elseifidni <divisor>, <cx>
		movsx	ecx, word ptr[esp+4]
	elseifidni <divisor>, <dx>
		movsx	ecx, word ptr[esp]
	else
		movsx	ecx, divisor
	endif
		cdq
		idiv	ecx
		mov	[esp+8], ax
		mov	[esp], dx
		pop	edx
		pop	ecx
		pop	eax
	else
		idiv	divisor
	endif
		endm
