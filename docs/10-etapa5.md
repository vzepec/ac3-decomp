# 10 — Etapa 5: capa de compatibilidad PsyQ

> Estado: **en curso** (abierta el 2026-10-16 por petición del dueño del
> proyecto). Rama `etapa-5`. Código en `psx/`.

## Qué es y qué no es

Una biblioteca en C que reimplementa las funciones de las librerías de Sony
(PsyQ 4.x) que usa Ace Combat 3, con la misma interfaz, para que algún día el
código del juego (recompilado o reescrito, etapa 6) pueda ejecutarse en
Android. Es el equivalente de Psy-X (REDRIVER2).

**No es el juego**: aquí no se recompila nada de Namco. Cada función se
valida por separado contra el comportamiento conocido.

## Superficie que hay que cubrir

Medida con `python3 tools/psyqapi.py <firmas> data/ghidra/all_*.c`: las
funciones de librería a las que llama código propio del juego (no las
internas de las librerías).

| Librería | Funciones | Ejemplos (nº de llamadores) | Destino en Android |
|---|---|---|---|
| LIBCD | 27 | `CdReadyCallback` (14), `CdControlF` (11), `CdRead`, `CdSearchFile` | lectura de la imagen de disco (ya hecha en `android/src/ace.c`) |
| LIBGTE | 26 | `SetTransMatrix` (12), `rsin` (12), `RotTransPers` (10), `RotMatrixYXZ`, `SquareRoot0`, `ratan2` | C en punto fijo exacto |
| LIBSPU | 22 | `SpuSetCommonAttr`, `SpuSetKey`, `SpuSetVoiceAttr`, reverb | mezclador propio sobre el audio de Android |
| LIBPAD | 22 | `PadGetState`, `PadSetAct` (la mayoría son internas del SIO) | mando físico de Android |
| LIBAPI | 21 | eventos, contadores, `InitHeap` | equivalentes del sistema |
| LIBGPU | 18 | `DrawSync` (13), `ClearImage`, `LoadImage`, `ClearOTagR`, `DrawOTag`, `StoreImage` | VRAM simulada + rasterizado en GLES (como la rebanada) |
| LIBC | 12 | `rand` (84), `memmove`, `strcmp` | libc del sistema (salvo `rand`, que debe dar la misma secuencia) |
| LIBETC | 9 | `VSync` (23), `DMACallback`, `VSyncCallback` | bucle y temporizador |
| LIBCARD | 9 | tarjeta de memoria | archivo de guardado |
| otras | 9 | `DecDCTvlc*` (vídeo), división de 64 bits | según haga falta |

**Total: 175 funciones de API.** Además, el código del juego usa la **GTE
directamente** (instrucciones COP2 en línea, sin pasar por libgte): 2.351
usos en 167 funciones. Las órdenes que aparecen: `RTPS`, `RTPT`, `NCLIP`,
`AVSZ3`, `AVSZ4`, `MVMVA` (varias formas), `DPCS`, `DPCT`, `INTPL`, `NCS`,
`NCT`, `NCCS`, `GPF`, `GPL`, `SQR`, más la carga y lectura de casi todos los
registros. Un **emulador exacto de la GTE** es, por tanto, el núcleo de la
etapa.

## Cómo se valida (sin PS1)

1. **Contra el código original de Sony.** Las funciones de libgte, libc
   (`rand`), etc. están dentro de `ACE.BIN` como código MIPS. Un intérprete
   MIPS R3000 mínimo (`tools/mipsemu.py`) las ejecuta y sus resultados son el
   oráculo: la reimplementación en C debe dar **exactamente** lo mismo en
   miles de entradas. Para las funciones que usan la GTE, el intérprete usa la
   GTE de `psx/` y la prueba comprueba la coherencia de ambos lados.
2. **Contra la especificación** (psx-spx, de nocash) para las órdenes de la
   GTE: saturaciones, banderas, división UNR.
3. **Contra los datos del juego**: la rebanada de `android/` ya reproduce el
   render de terreno y modelos; se podrá reescribir sobre `psx/` y comparar.

## Plan

| Paso | Entregable | Validación |
|---|---|---|
| 1 | Inventario de la API (`tools/psyqapi.py`) | ✅ este documento |
| 2 | Intérprete MIPS de referencia (`tools/mipsemu.py`) | ✅ ejecuta las funciones de Sony de `ACE.BIN` |
| 3 | `psx/` con libgte sin GTE (`rsin`, `rcos`, `ratan2`, `SquareRoot0`, `rand`) | ✅ **84.000 casos, 0 diferencias** (`make psx-check`) |
| 4 | Emulador de la GTE (`psx/src/gte.c`) | especificación + pruebas de coherencia |
| 5 | libgte con GTE (`RotMatrixYXZ`, `MulMatrix`, `RotTransPers`, …) | oráculo MIPS con la GTE de `psx/` |
| 6 | libgpu: tabla de ordenación, primitivas, VRAM, `DrawOTag` | listas de dibujo reales del juego |
| 7 | libcd, libpad, libetc | sobre las piezas de `android/` |
| 8 | libspu | bancos de sonido de las misiones (`tools/acesnd.py`) |

**Esfuerzo:** meses. **Riesgo:** medio (la GTE es delicada; el resto es
trabajo conocido gracias a Psy-X y a la documentación pública).

## Resultados

**Pasos 2-3 (2026-10-16).** `tools/mipsemu.py` es un intérprete R3000 de
enteros (con hueco de retardo, 2 MB de RAM, scratchpad, y de la GTE solo los
movimientos de registros y LZCS/LZCR). `tools/psyqcheck.py` ejecuta con él las
funciones originales y las compara con `psx/`:

| Función | Casos | Diferencias | Notas |
|---|---|---|---|
| `rsin`, `rcos` | 21.000 cada una | 0 | tabla generada: `round(4096·sen(2πi/4096))`, exacta en las 4.097 entradas |
| `SquareRoot0` | 17.093 | 0 | tabla generada: `floor(√v · 512)`, v = 0x40..0xFF, exacta; reproduce la aproximación del original (√1.000.000 = 999) |
| `ratan2` | 16.289 | 0 | la tabla **no** tiene fórmula exacta (el mejor ajuste acierta 996/1025): se lee del `ACE.BIN` del usuario, localizada por estructura (única en EU y US) |
| `rand` | 8.500 | 0 | congruencial `s·0x41C64E6D + 0x3039`, devuelve `(s >> 16) & 0x7FFF` |

**Validación del método** (regla 3): con errores introducidos a propósito
(`rcos` desplazado una unidad, raíz redondeada en vez de truncada, constante
de `rand` cambiada) el comparador detecta miles de diferencias.

Decisión de diseño: las tablas de Sony se **generan** cuando hay fórmula
exacta y, si no, se **leen del disco del usuario** en tiempo de ejecución;
nunca se copian al código ni al APK.
