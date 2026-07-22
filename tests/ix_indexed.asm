; ix_indexed.asm
; Indexed addressing with the IX register over a DB byte array.
; Demonstrates: LD IX, label / LD A,(IX+d) / DB data section / labels.
; Build & run:
;   asm  ix_indexed.asm ix_indexed.obj
;   link -abs -o ix_indexed.exe -org 0000 ix_indexed.obj
;   exec ix_indexed.exe
; Expected output:  IX!

    ORG 0x0000
    LD IX, DATA
    LD A, (IX+0)
    OUT (0), A
    LD A, (IX+1)
    OUT (0), A
    LD A, (IX+2)
    OUT (0), A
    LD A, 10
    OUT (0), A
    HALT

DATA:
    DB 73, 88, 33   ; 'I', 'X', '!'
