#!/usr/bin/env bash
# Bateria de regressao da toolchain Z80.
#
# Uso:  ./tests/run_tests.sh [caminho/para/bin]
# Padrao do diretorio dos executaveis: build/bin
#
# Cada teste monta, liga e executa um programa e compara a saida com o
# resultado esperado. Retorna 0 se tudo passar.

set -u

BIN="${1:-build/bin}"
DIR="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

ASM="$BIN/asm"
LINK="$BIN/link"
EXEC="$BIN/exec"
[ -x "$ASM.exe" ] && ASM="$ASM.exe" && LINK="$LINK.exe" && EXEC="$EXEC.exe"

pass=0
fail=0

check ()
{
    local nome="$1" esperado="$2" obtido="$3"
    if [ "$esperado" = "$obtido" ]; then
        printf '  ok   %-16s %s\n' "$nome" "$(printf '%s' "$obtido" | tr '\n' '|')"
        pass=$((pass + 1))
    else
        printf '  FALHA %-15s esperado [%s] obtido [%s]\n' "$nome" \
            "$(printf '%s' "$esperado" | tr '\n' '|')" \
            "$(printf '%s' "$obtido" | tr '\n' '|')"
        fail=$((fail + 1))
    fi
}

# saida do programa = tudo antes da linha "--- HALT ---"
saida ()
{
    "$EXEC" "$@" --max-cycles 200000 | sed -n '1,/--- HALT ---/p' | sed '$d' |
        sed -e 's/[[:space:]]*$//' | sed '/^$/d'
}

# --- programa unico ------------------------------------------------------
echo "Programas de um modulo:"
for t in hello_io:Hi! ix_indexed:IX! branch:Y stack:HI string_loop:Z80! \
         macro_nested:OK!!; do
    nome="${t%%:*}"
    esperado="${t#*:}"

    "$ASM" "$DIR/$nome.asm" "$TMP/$nome.obj" > /dev/null || { echo "  FALHA $nome (montagem)"; fail=$((fail+1)); continue; }
    "$LINK" -abs -o "$TMP/$nome.exe" -org 0000 "$TMP/$nome.obj" > /dev/null || { echo "  FALHA $nome (ligacao)"; fail=$((fail+1)); continue; }
    check "$nome" "$esperado" "$(saida "$TMP/$nome.exe")"
done

# --- dois modulos, varios enderecos de carga ------------------------------
echo "Ligacao de dois modulos (EXTERN/GLOBAL):"
ESPERADO="$(printf 'Z80 ligado!\nZ80 ligado!')"

"$ASM" "$DIR/mod_main.asm" "$TMP/mod_main.obj" > /dev/null
"$ASM" "$DIR/mod_lib.asm" "$TMP/mod_lib.obj" > /dev/null

for org in 0000 8000 C000; do
    "$LINK" -abs -o "$TMP/prog.exe" -org "$org" "$TMP/mod_main.obj" \
        "$TMP/mod_lib.obj" > /dev/null
    check "abs@$org" "$ESPERADO" "$(saida "$TMP/prog.exe")"
done

# --- ligador relocavel + carregador relocador -----------------------------
echo "Ligador relocavel + Carregador Relocador:"
"$LINK" -reloc -o "$TMP/progr.exe" -org 0000 "$TMP/mod_main.obj" \
    "$TMP/mod_lib.obj" > /dev/null

for addr in 0100 4000 C000; do
    check "carga@$addr" "$ESPERADO" \
        "$(saida "$TMP/progr.exe" --load-addr "$addr" | grep -v '^Carregador')"
done

echo
echo "$pass passaram, $fail falharam"
[ "$fail" -eq 0 ]
