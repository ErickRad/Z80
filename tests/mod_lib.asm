; mod_lib.asm
; Modulo biblioteca - exporta duas rotinas com GLOBAL.
; Par com mod_main.asm para demonstrar a ligacao de varios modulos.
;
; Observe que o rotulo LOOP tambem existe em mod_main.asm: rotulos locais
; pertencem ao modulo, so os declarados GLOBAL entram na tabela do ligador.
;
; Build & run: ver o cabecalho de mod_main.asm

        GLOBAL PRINT_STR
        GLOBAL NOVA_LINHA

; Imprime a cadeia apontada por HL, terminada em zero.
PRINT_STR:
LOOP:
        LD A, (HL)
        CP 0
        RET Z
        OUT (0), A
        INC HL
        JR LOOP

NOVA_LINHA:
        LD A, 10
        OUT (0), A
        RET
