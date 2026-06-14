; NASM example to create a 16-bit OS/2 segmented NE executable


BITS 16

cr		equ 0x0d
lf		equ 0x0a
stdin		equ 0
stdout		equ 1

group DGROUP _DATA

        extern   DOSEXIT
	        extern   DOSREAD
	        extern   DOSWRITE

segment _DATA CLASS=DATA

msg1		db 'Hello World',cr,lf
msg1_len	equ $-msg1

rlen		dw 0
wlen		dw 0

segment	STACK stack
		; this is defined in .def file
		;resw 1024


segment _TEXT CLASS=CODE

..start:
                ;  Add parameters to stack and then call OS
                
		push 	stdout			; specifying stdout
                push	ds                      ; Address of string ds:address
		push	msg1
                mov	cx,msg1_len             ; length of string
                push	cx
                push	ds                      ; Address to store bytes written ds:address
                push	wlen
                call    far DOSWRITE

exit:
		push    1               	; terminate process
		push    0               	; return code
       call	far DOSEXIT

segment _DATA

segment end



