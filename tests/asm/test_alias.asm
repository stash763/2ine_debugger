; NASM test: alias trampoline pattern — two CODE segments, far call between them
;
; This simulates what llvm-i286 codegen produces for weak aliases:
;   - _TEXT_main contains the real function (like default_malloc)
;   - _TEXT_alias contains a trampoline (like malloc -> call far default_malloc -> retf)
;   - main calls the trampoline, which calls the real function
;
; Expected behavior on real OS/2 1.x: exits with code 42
; If this crashes, the pattern itself is broken (not lx_loader)
; If this works on OS/2 but crashes on lx_loader, issue is in lx_loader
; If this works on both, issue is in our codegen output (not the pattern)

BITS 16
CPU 286

cr		equ 0x0d
lf		equ 0x0a

group DGROUP _DATA

	extern DOSEXIT

segment _DATA CLASS=DATA

rlen		dw 0
wlen		dw 0

segment	STACK stack
	resw 2048			; 4096 bytes stack

; Segment 1: main code
segment _TEXT_main CLASS=CODE

global ..start
..start:
	; Program entry: call main
	call far main
	; Exit with AX as return code
	push ax
	push 0
	call far DOSEXIT

global main
main:
	push bp
	mov bp, sp
	push bx
	push si
	push di
	sub sp, 4
	; Call the alias trampoline in _TEXT_alias segment
	; This is a far call to a different CODE segment
	call far alias_func
	; AX = 42 (return value from trampoline)
	mov bx, 0
	; Epilogue
	lea sp, [bp-6]
	pop di
	pop si
	pop bx
	pop bp
	retf

; The real function (also in _TEXT_main)
; Simulates an internal function like default_malloc
global real_func
real_func:
	push bp
	mov bp, sp
	push bx
	push si
	push di
	mov ax, 42
	mov bx, 0
	lea sp, [bp-6]
	pop di
	pop si
	pop bx
	pop bp
	retf

; Segment 2: alias trampoline (separate CODE segment)
; Simulates what codegen emits for weak_alias(default_malloc, malloc)
segment _TEXT_alias CLASS=CODE

; Trampoline: call the real function, then return
global alias_func
alias_func:
	call far real_func
	retf

segment end