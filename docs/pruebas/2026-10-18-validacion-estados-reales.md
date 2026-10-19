# Prueba: funciones del juego validadas con estados reales — 2026-10-18

**Pregunta del dueño:** la infografía daba "91 % recompilado". ¿Se puede
avanzar con el resto?

## De dónde venía el 91 %

El comparador (`tools/recomp/check.py`) ejecuta cada función del juego desde
24 estados aleatorios en dos sitios:
- el intérprete de referencia (`tools/mipsemu.py`);
- el código en C, el traducido o el intérprete `mipsi.c`.

Una función queda **validada** si algún caso termina normalmente igual en
los dos y ninguno sale distinto. El ~9 % restante no salía **distinto**:
salía **sin caso**, porque con datos aleatorios nunca terminaba un caso
válido (punteros a estructuras que no existen, bucles que esperan al
hardware…).

## Qué se hizo

1. **Cobertura** (`AC3_COVERAGE`, `tools/cobertura.py`): durante las 41
   misiones el intérprete anota las funciones en que entra. En los overlays
   se identifica cuál estaba cargado por la firma de sus 64 primeros bytes.
2. **Estados reales** (`AC3_STATES`, `tools/estados.py`): la primera vez que
   el juego entra en una función pendiente, se guarda el estado completo:
   registros, hi/lo, GTE, scratchpad y RAM.
3. **Comparación desde esos estados** (`check.py --states DIR --ovl-dir …
   --interp psx/build/libmipsi.so`): el intérprete de referencia frente al
   intérprete en C del APK, con 2 millones de pasos por caso.

## Resultados

| | Americana (`SLUS-00972`) | Europea (`SCES-02066`) |
|---|---|---|
| Funciones de Ghidra | 1.824 | 1.821 |
| Se ejecutan en las 41 misiones | 1.065 (58 %) | 1.058 (58 %) |
| Validadas con estados aleatorios (intérprete en C) | 1.671 — 0 distintas | 1.672 — 0 distintas |
| Pendientes que se ejecutan en las misiones | 90 | 86 |
| … con estado real capturado | 90 / 90 | 86 / 86 |
| … **validadas con estados reales** | **79** (residente 67, overlay 1: 2, overlay 2: 10) — **0 distintas** | **82** (residente 70, overlay 1: 1, overlay 2: 11) — **0 distintas** |
| … sin caso también con estado real | 11 (no vuelven en 2 millones de pasos, o fallan igual en los dos) | 4 |
| **Total validadas** | **1.750 / 1.824 (95,9 %)** | **1.754 / 1.821 (96,3 %)** |
| Pendientes que no se ejecutan en las misiones | 63 | 63 |

- Los overlays 3, 4 y 5 no se ejecutan en vuelo: deben de ser menús,
  finales y otras partes del juego, **SIN RESOLVER**. Ya estaban validados
  al 100 % con estados aleatorios.
- **Ninguna prueba ha dado una sola diferencia** entre el intérprete del APK
  y el de referencia.

**Reproducir:**

```bash
make misiones                 # y make misiones-eu: cobertura.txt por mision
python3 tools/cobertura.py us psx/build/misiones cov_us.json
python3 tools/estados.py "<disco US>" cov_us.json estados_us            # (--eu para la europea)
python3 tools/recomp/check.py data/exe/ace_us.bin data/recomp_us --states estados_us \
    --ovl-dir data/exe/ovl_us --interp psx/build/libmipsi.so --region data/recomp_us/region.json
make mipsi-check-us           # estados aleatorios (la europea: make mipsi-check)
python3 tools/progress.py version us cov_us.json   # infografia
```

Los estados son RAM del juego: se generan fuera de git y no se versionan.
