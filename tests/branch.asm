; branch.asm
; Conditional control flow: compare and jump on the Z (zero) flag.
; Demonstrates: CP (compare), flag update, JP Z / fall-through, forward label.
;
; Build & run:
;   asm  branch.asm branch.obj
;   link -abs -o branch.exe -org 0000 branch.obj
;   exec branch.exe
; Expected output:  Y        (A == 5, so the branch is taken)

    ORG 0x0000
    LD A, 5
    CP 5            ; sets Z if A == 5
    JP Z, EQUAL
    LD A, 78        ; 'N' (not taken in this program)
    OUT (0), A
    HALT

EQUAL:
    LD A, 89        ; 'Y'
    OUT (0), A
    HALT
