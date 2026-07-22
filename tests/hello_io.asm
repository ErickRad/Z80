; hello_io.asm
; Basic console output through I/O port 0 (OUT (0), A prints the char in A).
; Demonstrates: immediate loads, port output, program termination with HALT.
;
; Build & run:
;   asm  hello_io.asm hello_io.obj
;   link -abs -o hello_io.exe -org 0000 hello_io.obj
;   exec hello_io.exe
; Expected output:  Hi!

    ORG 0x0000
    LD A, 72        ; 'H'
    OUT (0), A
    LD A, 105       ; 'i'
    OUT (0), A
    LD A, 33        ; '!'
    OUT (0), A
    LD A, 10        ; newline
    OUT (0), A
    HALT
