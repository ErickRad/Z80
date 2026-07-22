; stack.asm
; Stack operations with 16-bit register pairs.
; Demonstrates: LD rr,nn / PUSH / POP between register pairs / SP usage.
;
; Build & run:
;   asm  stack.asm stack.obj
;   link -abs -o stack.exe -org 0000 stack.obj
;   exec stack.exe
; Expected output:  HI       (BC pushed, popped into HL, H then L printed)

    ORG 0x0000
    LD BC, 0x4849   ; B = 0x48 ('H'), C = 0x49 ('I')
    PUSH BC
    POP HL          ; HL now holds 0x4849
    LD A, H
    OUT (0), A
    LD A, L
    OUT (0), A
    LD A, 10
    OUT (0), A
    HALT
