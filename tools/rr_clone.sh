#!/bin/sh
# rr_clone.sh — segunda copia del interprete para el redibujo en un hilo de dibujo (etapa 8,
# psx/recomp/ac3_rr_async.c): compila las fuentes y pone el prefijo rrw_ a todos los simbolos
# globales que definen (y a sus referencias entre ellas), como el codigo americano en
# android_game/build.sh. Las referencias a lo que no esta en la lista (libc, la GPU, VSync, las
# tablas de libgte_math.c) siguen apuntando a la copia unica.
#   tools/rr_clone.sh "<compilador y opciones>" <nm> <objcopy> <salida> fuente.c...
set -e
CC=$1; NM=$2; OC=$3; OUT=$4; shift 4
mkdir -p "$OUT"
rm -f "$OUT"/*.o
for f in "$@"; do
    $CC -c "$f" -o "$OUT/$(basename "$f" .c).o"
done
"$NM" --defined-only -g "$OUT"/*.o | awk 'NF == 3 {print $3, "rrw_" $3}' | sort -u > "$OUT/rrw.syms"
for o in "$OUT"/*.o; do "$OC" --redefine-syms="$OUT/rrw.syms" "$o"; done
