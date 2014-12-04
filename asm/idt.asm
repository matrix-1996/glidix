;	Glidix kernel
;
;	Copyright (c) 2014, Madd Games.
;	All rights reserved.
;	
;	Redistribution and use in source and binary forms, with or without
;	modification, are permitted provided that the following conditions are met:
;	
;	* Redistributions of source code must retain the above copyright notice, this
;		list of conditions and the following disclaimer.
;	
;	* Redistributions in binary form must reproduce the above copyright notice,
;		this list of conditions and the following disclaimer in the documentation
;		and/or other materials provided with the distribution.
;	
;	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
;	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
;	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
;	DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
;	FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
;	DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
;	SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
;	CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
;	OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
;	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

section .text
bits 64

[global loadIDT]
[extern idtPtr]
loadIDT:
	lidt [idtPtr]
	ret

%macro pushAll 0
	push	 rax
	push	 rcx
	push	 rdx
	push	 rbx
	push	 rbp
	push	 rsi
	push	 rdi
%endmacro

%macro popAll 0
	pop	rdi
	pop	rsi
	pop	rbp
	pop	rbx
	pop	rdx
	pop	rcx
	pop	rax
%endmacro

[extern isrHandler]

isrCommon:
	pushAll

	mov			ax, ds
	push			rax

	mov			ax, 0x10
	mov			ds, ax
	mov			es, ax
	mov			fs, ax
	mov			gs, ax

	mov			rdi, rsp		; pass a pointer to registers as argument to isrHandler
	call	 		isrHandler

	pop			rbx
	mov			ds, bx
	mov			es, bx
	mov			fs, bx
	mov			gs, bx

	popAll
	add			rsp, 16
	;sti
	iretq

%macro ISR_NOERRCODE 1
	global isr%1
	isr%1:
		cli
		push qword 0 
		push qword %1
		jmp isrCommon
%endmacro

%macro ISR_ERRCODE 1
	global isr%1
	isr%1:
		cli
		push qword %1
		jmp isrCommon
%endmacro

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE	 8
ISR_NOERRCODE 9
ISR_ERRCODE	 10
ISR_ERRCODE	 11
ISR_ERRCODE	12
ISR_ERRCODE	13
ISR_ERRCODE	14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_NOERRCODE 17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31