# 01 — Estructura de los discos

Reproducible con `make files`.

Ambos discos son de **pista única `MODE2/2352`**: no hay pistas CDDA, así que
toda la música y la voz van dentro de los datos (XA/ADPCM o streams propios).

## Disco europeo — `SCES_020.66`

- Imagen: 470.999.760 bytes (449 MiB)
- Volumen: `ACE3ES` · Editor: `NAMCO LIMITED`
- `SYSTEM.CNF`: `BOOT = cdrom:\SCES_020.66;1`, `TCB = 4`, `EVENT = 10`,
  `STACK = 801FFF00`

| Archivo | LBA | Tamaño | Qué es |
|---|---|---|---|
| `SYSTEM.CNF` | 23 | 68 | Configuración de arranque |
| `SCES_020.66` | 24 | 491.520 | Ejecutable de arranque (módulo residente) |
| `ACE.BIN` | 264 | 413.772 | Módulo principal, **comprimido** |
| `ACE.BPH` | 467 | 1.036 | Índice de `ACE.BPB` (127 entradas) |
| `ACE.SPH` | 468 | 596 | Índice de `ACE.SPB` (72 entradas) |
| `ACE.BPB` | 469 | 34.650.112 | Pack de bancos |
| `ACE.EXE` | 179.340 | 190.464 | Ejecutable secundario |
| `ACE.DAT` | 179.433 | 42.336.192 | Datos (no existe en la versión USA) |
| `ACE.SPB` | 17.388 | 331.677.696 | Pack de streaming |

Total en archivos: 409.761.456 bytes (390,8 MiB).

## Disco americano — `SLUS_009.72`

- Imagen: 644.977.200 bytes
- Volumen: `ACE3ES` · Editor: `NAMCO HOMETEK INC.`

| Archivo | Tamaño | Comparación con EU |
|---|---|---|
| `SLUS_009.72` | 491.520 | mismo tamaño |
| `ACE.BIN` | 415.532 | +1.760 bytes |
| `ACE.EXE` | 190.464 | idéntico tamaño |
| `ACE.BPH` | 1.036 | idéntico tamaño |
| `ACE.SPH` | 724 | +128 bytes (88 entradas vs 72) |
| `ACE.BPB` | 31.803.392 | −2,8 MB |
| `ACE.SPB` | 528.351.232 | **+196 MB** |
| `ACE.DAT` | — | **no existe** |

Total: 561.253.968 bytes (535,3 MiB).

**Interpretación (corregida el 2026-10-15):** se pensó que la versión europea
había sacado contenido de `ACE.SPB` a `ACE.DAT`. **Es falso.** `ACE.DAT` es un
archivo de relleno: de sus 20.672 sectores solo 40 tienen algún byte distinto
de cero (`tools/acedat.py`). Está al final del disco y no contiene assets.

La diferencia de tamaño de `ACE.SPB` (+196 MB en la USA) sigue `SIN RESOLVER`;
el SPB americano tiene 88 entradas frente a 72 y más categorías (`0xF5`,
`0xF8`), probablemente vídeo.

## Observación clave para el proyecto

El volumen de las dos versiones se llama `ACE3ES` y el ejecutable de arranque
mide exactamente lo mismo (491.520 B) en ambas. El trabajo de análisis de
código hecho sobre una versión **se transfiere** a la otra con ajustes de
direcciones, no desde cero.
