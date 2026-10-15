#!/bin/sh
# Atajo: decompila funciones de un programa del proyecto Ghidra de data/.
#   tools/ghidra/decomp.sh <programa> <salida.c> <func|0xaddr> [...]
# La salida debe ir a data/ (codigo derivado del juego, no se versiona).
GHIDRA=${GHIDRA:-$HOME/tools/ghidra_12.1.4_PUBLIC}
JDK=${JDK:-$HOME/tools/jdk-21.0.12.1+1}
prog=$1; out=$2; shift 2
JAVA_HOME="$JDK" PATH="$JDK/bin:$PATH" "$GHIDRA/support/analyzeHeadless" data/ghidra ac3 \
    -process "$prog" -noanalysis -scriptPath tools/ghidra \
    -postScript DumpDecomp.java "$out" "$@" > data/ghidra/decomp.log 2>&1
grep -q "ERROR" data/ghidra/decomp.log && grep ERROR data/ghidra/decomp.log | head -5
exit 0
