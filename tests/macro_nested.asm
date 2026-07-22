; macro_nested.asm
; Processador de macro (uma passagem) - definicao e chamada aninhadas.
; Demonstra:
;   - chamada de macro dentro de macro   (IMPRIME_PAR chama IMPRIME_CHAR)
;   - definicao de macro dentro de macro (BANNER define FIM_LINHA)
;   - propagacao de parametros entre niveis de aninhamento
;
; Build & run:
;   macro macro_nested.asm macro_nested_exp.asm   ; opcional: ver a expansao
;   asm   macro_nested.asm macro_nested.obj       ; o asm ja chama o macro
;   link  -abs -o macro_nested.exe -org 0000 macro_nested.obj
;   exec  macro_nested.exe
; Expected output:  OK!!

MACRO IMPRIME_CHAR car
    LD A, car
    OUT (0), A
ENDM

; chamada aninhada: o corpo desta macro chama outra macro
MACRO IMPRIME_PAR prim, seg
    IMPRIME_CHAR prim
    IMPRIME_CHAR seg
ENDM

; definicao aninhada: FIM_LINHA so passa a existir quando BANNER e expandida
MACRO BANNER simbolo
    MACRO FIM_LINHA
        IMPRIME_CHAR 10
    ENDM

    IMPRIME_PAR simbolo, simbolo
    FIM_LINHA
ENDM

    ORG 0x0000
    IMPRIME_PAR 79, 75      ; 'O', 'K'
    BANNER 33               ; '!', '!' e nova linha
    HALT
