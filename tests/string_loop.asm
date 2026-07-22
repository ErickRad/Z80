; string_loop.asm
; Laco contado com DJNZ percorrendo uma cadeia em memoria.
; Demonstra: LD A,(HL) / INC HL / DJNZ (decrementa B e salta se B != 0),
;            EQU com diferenca de rotulos (FIM - MSG) para calcular o tamanho.
;
; Build & run:
;   asm  string_loop.asm string_loop.obj
;   link -abs -o string_loop.exe -org 0000 string_loop.obj
;   exec string_loop.exe
; Expected output:  Z80!

        ORG 0x0000
        LD HL, MSG
        LD B, TAM
LOOP:
        LD A, (HL)
        OUT (0), A
        INC HL
        DJNZ LOOP
        HALT

MSG:    DB 'Z80!', 10
FIM:

TAM     EQU FIM - MSG       ; tamanho calculado pelo montador
