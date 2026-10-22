; tasm.inc: the frames TASM gave the procedures of cdrom.asm, read first
; when JWasm assembles it (-Fi): enter with the locals' size, then the
; registers the procedure uses, where JWasm, as MASM, pushes and copies
; EBP, then subtracts the locals' size.

tasmframe	macro	procname, flag, parmbytes, localbytes, reglist, userparms
	if parmbytes or localbytes
	enter	localbytes, 0
	endif
	for r, reglist
	push	r
	endm
	exitm	%0
endm

option	prologue:tasmframe
