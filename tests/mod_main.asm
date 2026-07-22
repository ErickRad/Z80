; mod_main.asm
; Modulo principal - usa rotinas de outro modulo via EXTERN.
; Demonstra: EXTERN/GLOBAL, ligacao de dois modulos, mapa de ligacao e
; relocacao completa (o par pode ser carregado em qualquer endereco).
;
; Build & run:
;   asm  mod_main.asm mod_main.obj
;   asm  mod_lib.asm  mod_lib.obj
;   link -abs -o prog.exe -org 8000 -m prog.map mod_main.obj mod_lib.obj
;   exec prog.exe
; Expected output:
;   Z80 ligado!
;   Z80 ligado!
;
; Ligacao relocavel (relocacao finalizada na carga):
;   link -reloc -o prog.exe -org 0000 mod_main.obj mod_lib.obj
;   exec prog.exe --load-addr 4000

        EXTERN PRINT_STR
        EXTERN NOVA_LINHA
        GLOBAL INICIO

        ORG 0x0000
INICIO:
        LD B, 2             ; imprime a mensagem duas vezes
LOOP:                       ; rotulo local, tambem existe em mod_lib.asm
        PUSH BC
        LD HL, MSG
        CALL PRINT_STR
        CALL NOVA_LINHA
        POP BC
        DJNZ LOOP
        HALT

MSG:    DB 'Z80 ligado!', 0
