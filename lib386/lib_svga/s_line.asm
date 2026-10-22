;----------------------------------------------------------------------------
;				S_LINE.ASM 386
;			      (c) Adeline 1993
;----------------------------------------------------------------------------
		.386p
		jumps
		.model FLAT, SYSCALL

		.data

		include	svga.ash

EVEN

couleur		dw	00

		.code

		public	NoLanguage	Line
		public	NoLanguage	Line_A

;----------------------------------------------------------------------------
;		Line( x0, y0, x1, y1, coul )
;
Line		proc	uses esi edi ebp ebx,\
		x0:DWORD, y0:DWORD,\
		x1:DWORD, y1:DWORD,\
		coul:DWORD

; 		X0:ax  Y0:bx
;		X1:cx  Y1:DX
;		couleur: bp

		mov	eax, x0
		mov	ebx, y0
		mov	ecx, x1
		mov	edx, y1

		mov	ebp, coul

		call	Line_A

		ret

Line		endp

Line_A		proc

; 		X0:eax  Y0:ebx
;		X1:ecx  Y1:edx

		mov	[couleur], bp

		cmp	eax, ecx		; x0 < x1
		jle	sensok
		xchg	eax, ecx
		xchg	ebx, edx
sensok:
		; XY 0

onceagain:	xor	bp, bp

		cmp	eax, [ClipXmin]			; x0
		jge	ncxi
		or	bp, 1
		jmp	short ncxg

ncxi:		cmp	eax, [ClipXmax]
		jg      notseen

ncxg:		cmp	ebx, [ClipYmin]
		jge	ncyi
		or	bp, 8
		jmp	short ncya

ncyi:		cmp	ebx, [ClipYmax]
		jle	ncya
		or	bp, 4
ncya:
		; XY 1

		shl	bp, 8

		cmp	ecx, [ClipXmin]
		jl	notseen

		cmp	ecx, [ClipXmax]
		jle	ncx2a
		or	bp, 2
ncx2a:
		cmp	edx, [ClipYmin]
		jge	ncy2i
		or	bp, 8
		jmp	short ncy2a

ncy2i:		cmp	edx, [ClipYmax]
		jle	ncy2a
		or	bp, 4
ncy2a:

		; test

		mov	si, bp
		shr	si, 8
		test	si, bp
		jnz	notseen
		or	si, bp
		and	si, 0FFh
		jz	draw

		mov	edi, ecx
		sub	edi, eax		; edi delta x

		mov	esi, edx
		sub	esi, ebx		; esi delta y


		test	bp, 100h
		jz	c0
IFDEF	FIXES
		test	bp, 0C00h
		jz	clipx0
		; FIXED: LBA.EXE clips an end outside on both axes in x, then
		; in y from the point the x clip rounded, so that the rounding
		; compounds: a shallow line through a box one line high can be
		; rejected though it crosses the box, and one entering at a
		; corner starts up to dx/dy pixels late. Clip such an end once,
		; at the edge the line crosses there, the first end's later
		; entry or, below, the other's earlier exit, x on a tie.
		push	ecx
		push	edx
		mov	ecx, [ClipXmin]
		mov	edx, [ClipYmin]
		test	bp, 800h
		jnz	ey0
		mov	edx, [ClipYmax]
ey0:		call	cmpcross
		pop	edx
		pop	ecx
		ja	c0			; the line enters through y last
clipx0:
ENDIF
		mov	ebp, edx

		sub	eax, [ClipXmin]		; xmin - x0
		neg	eax
		; FIXED: LBA.EXE multiplies and divides the low words and keeps
		; 16 bits of the quotient: past 16 bits a clipped end lands
		; anywhere, so that the clipping may never end, and a dx or dy
		; that is a multiple of 65536 divides by 0. Multiply in 64 bits
		; and divide in 32, here and below, the quotient at most the
		; other difference while the coordinates lie within 2^30.
IFDEF	FIXES
		imul	esi			; * dy
		idiv	edi			; / dx
ELSE
		imul	si			; * dy
		idiv	di			; / dx
		movsx	eax, ax
ENDIF
		add     ebx, eax		; y0 +=
		mov	eax, [ClipXmin]		; x0 = xmin

		mov	edx, ebp

		jmp     onceagain
c0:
		test	bp, 800h
		jz	c1

		mov	ebp, edx

		xchg	eax, ebx
		sub	eax, [ClipYmin]		; ymin - y0
		neg	eax
IFDEF	FIXES
		imul	edi			; * dx
		idiv	esi			; / dy
ELSE
		imul	di			; * dx
		idiv	si			; / dy
		movsx	eax, ax
ENDIF
		add     eax, ebx		; x0 +=
		mov	ebx, [ClipYmin]		; y0 = ymin

		mov	edx, ebp

		jmp     onceagain
c1:
		test	bp, 400h
		jz	c2

		mov	ebp, edx

		xchg	eax, ebx
		sub	eax, [ClipYmax]		; ymax - y0
		neg	eax
IFDEF	FIXES
		imul	edi			; * dx
		idiv	esi			; / dy
ELSE
		imul	di			; * dx
		idiv	si			; / dy
		movsx	eax, ax
ENDIF
		add     eax, ebx		; x0 +=
		mov	ebx, [ClipYmax]		; y0 = ymax

		mov	edx, ebp

		jmp     onceagain



c2:
		test	bp, 2
		jz	c3
IFDEF	FIXES
		test	bp, 0Ch
		jz	clipx1
		push	ecx
		push	edx
		mov	ecx, [ClipXmax]
		mov	edx, [ClipYmin]
		test	bp, 8
		jnz	ey1
		mov	edx, [ClipYmax]
ey1:		call	cmpcross
		pop	edx
		pop	ecx
		jb	c3			; the line leaves through y first
clipx1:
ENDIF
		mov	ecx, eax			; save x0
		mov	eax, [ClipXmax]
		sub	eax, ecx			; xmax - x0
IFDEF	FIXES
		imul	esi				; * dy
		idiv	edi				; / dx
		mov	edx, eax
ELSE
		imul	si				; * dy
		idiv	di				; / dx
		movsx	edx, ax
ENDIF
		add     edx, ebx			; + y0
		mov	eax, ecx			; restore x0
		mov	ecx, [ClipXmax]

		jmp     onceagain

c3:
		test	bp, 8
		jz	c4

		mov	ecx, eax
		mov	eax, ebx
		sub	eax, [ClipYmin]			; ymin - y0
		neg	eax
IFDEF	FIXES
		imul	edi				; * dx
		idiv	esi				; / dy
		mov	edx, eax
ELSE
		imul	di				; * dx
		idiv	si				; / dy
		movsx	edx, ax
ENDIF
		mov	eax, ecx
		add     ecx, edx			; x0 +=
		mov	edx, [ClipYmin]

		jmp     onceagain

c4:
		test	bp, 4
		jz	onceagain

		mov	ecx, eax
		mov	eax, ebx
		sub	eax, [ClipYmax]			; ymax - y0
		neg	eax
IFDEF	FIXES
		imul	edi				; * dx
		idiv	esi				; / dy
		mov	edx, eax
ELSE
		imul	di				; * dx
		idiv	si				; / dy
		movsx	edx, ax
ENDIF
		mov	eax, ecx
		add	ecx, edx
		mov	edx, [ClipYmax]

		jmp     onceagain

notseen:	ret

;----------------------------------------------------------------------------

draw:           mov	esi, 640
		sub	ecx, eax
		sub	edx, ebx
		jns	descend

		neg	esi
		neg	edx

descend:	mov	edi, Log
		add	edi, TabOffLine[ ebx * 4 ]
		add	edi, eax

		mov	ax, [couleur]

		cmp	ecx, edx
		jl	vertical

horizontal:	mov	ebp, ecx
		shl	ebp, 1
		mov	ebx, ecx
		shl	edx, 1
		inc	ecx
		jmp	loopX

noovfy:		dec	ecx
		jz	exit
loopX:		mov	byte ptr[edi], al
		inc	edi
		sub	ebx, edx
		jnc	noovfy
		add	ebx, ebp
		add	edi, esi
		dec	ecx
		jnz	loopX

		ret

vertical:	xchg	ecx, edx
		mov	ebp, ecx
		shl	ebp, 1
		mov	ebx, ecx
		shl	edx, 1
		inc	ecx
		jmp	loopY

noovfx:		add	edi, esi
		dec	ecx
		jz	exit
loopY:		mov	byte ptr[edi], al
		sub	ebx, edx
		jnc	noovfx
		add	ebx, ebp
		adc	edi, esi
		dec	ecx
		jnz	loopY

exit:
		ret

Line_A		endp

IFDEF	FIXES
;----------------------------------------------------------------------------
; cmpcross: |ey-y|*dx against (ex-x)*|dy|, products of 64 bits, the flags
; as an unsigned cmp of the two leaves them (ja, jb, je): x and y in EAX
; and EBX, the edges ex and ey in ECX and EDX, ex-x 0 or above, dx above
; 0 in EDI, dy in ESI. EAX, EBX, ECX and EDX are kept.

cmpcross	proc	near
		push	eax
		push	ebx
		push	ecx
		push	edx
		sub	edx, ebx		; ey - y
		jns	ayabs
		neg	edx
ayabs:		sub	ecx, eax		; ex - x
		mov	eax, edx
		mul	edi			; |ey-y| * dx
		mov	ebx, eax
		push	edx
		mov	eax, esi
		cdq
		xor	eax, edx
		sub	eax, edx		; |dy|
		mul	ecx			; (ex-x) * |dy|
		pop	ecx
		cmp	ecx, edx		; the high dwords
		jne	cmpdone
		cmp	ebx, eax		; then the low
cmpdone:	pop	edx
		pop	ecx
		pop	ebx
		pop	eax
		ret
cmpcross	endp
ENDIF


		end
