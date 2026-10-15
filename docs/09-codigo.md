# 09 — Análisis estático del código (etapa 3)

Reproducible con `make ghidra-import` y `make ghidra-stats` (ver §1).
Scripts en `tools/ghidra/`.

> **Nada de lo que produce Ghidra se versiona.** El proyecto (`data/ghidra/`),
> los volcados de pseudo-C y los TSV de funciones son derivados del código
> del juego. Este documento describe estructura, direcciones y cifras; no
> reproduce código.

## 1. Entorno

Instalado en espacio de usuario (`~/tools`), sin `sudo`:

| Componente | Versión | Verificación |
|---|---|---|
| JDK | Temurin 21.0.12.1+1 | SHA-256 de Adoptium `ce79869e…aee94` |
| Ghidra | 12.1.4 (`ghidra_12.1.4_PUBLIC_20260921.zip`) | SHA-256 publicado `ddac49f9…d4db` |
| Extensión PSX | `ghidra_psx_ldr` release `2026.10.03`, build para 12.1.4 | release oficial de GitHub (sin hash publicado) |

La extensión aporta el loader de PS-X EXE, el lenguaje `PSX:LE:32` con
COP2/GTE, un segmento artificial `GTEMAC` con los manejadores de macros GTE y
firmas de las librerías PsyQ 2.6-4.7.

```bash
make acebin            # genera data/exe/ace_eu.exe (PS-X EXE en claro)
make ghidra-import     # importa y analiza (~4 min)
make ghidra-stats      # cifras -> data/ghidra/stats_eu.txt y _funcs.tsv
```

## 2. Mapa de módulos ✅

```
BIOS
 └─ SCES_020.66   residente en 0x80160000
     · lee ACE.BIN entero del CD mientras dibuja una pantalla de carga con sonido
     · lo valida y lo descomprime (función 0x801642D4, ver 02-binarios.md)
     ├─ si todo cuadra → Exec() de ACE.BIN en 0x80010000
     └─ si falla, o si Exec vuelve → LoadExec("cdrom:\ACE.EXE;1")
 ACE.BIN          código 0x80010000-0x800BB000
                  BSS 0x800BACB0-0x800C9DC0, InitHeap desde 0x800C9DC4
                  abre solo ACE.BPB/BPH/SPB/SPH: no carga más código
 ACE.EXE          pantalla fija "WARNING! An error has occurred while
                  loading…" (aviso anticopia), con una TIM de 640×480
```

Pruebas: referencias a las cadenas de nombre de archivo de cada ejecutable,
decompilación de `main` de `SCES_020.66` y de su función de arranque, y la
TIM de `ACE.EXE` exportada con `tools/tim2png.py`.

**No hay overlays de código** dentro de `ACE.BIN`: es un único módulo de
700 KB. Los registros de tipo 1 de `ACE.BPB` no contienen código (0
prólogos MIPS).

## 3. Cifras de funciones ✅

Medido sobre el bloque que contiene el punto de entrada (el segmento
`GTEMAC` del loader se excluye: no es código del juego).

| Módulo | Funciones | Reconocidas (PsyQ) | Propias | Bytes propios | COP2 |
|---|---|---|---|---|---|
| `ACE.BIN` (EU) | 1.853 | 747 (77 KB) | **1.106** | **420 KB** | 1.939 |
| `SCES_020.66` | 701 | 665 (95 %) | 36 | 4,5 KB | 0 en `.text` |
| `ACE.EXE` | 221 | 213 | 8 | 0,6 KB | 0 |

"Propias" = funciones sin nombre (`FUN_`) más `main`. Es una **cota
superior**: cualquier función de librería cuya firma no casara cuenta como
propia.

Desglose de las 747 de `ACE.BIN` por librería (`tools/ghidra/libsplit.py`):
LIBCD 164 · LIBPAD 125 · LIBSPU 124 · LIBC2 75 · LIBGTE 66 · LIBAPI 52 ·
LIBGPU 42 · LIBETC 37 · LIBCARD 22 · LIBC 19 · LIBSN 11 · LIBPRESS 4 ·
LIBSND 1 · otros 5.

### Validación del reconocimiento

- **Control positivo:** la cadena `ResetGraph:jtb=%08x` solo la usa
  `ResetGraph` de libgpu; en `ACE.BIN` la única función que la referencia es
  justo la que las firmas llamaron `ResetGraph`.
- En `SCES_020.66` y `ACE.EXE`, que son casi todo librería, se reconoce el
  95 % y el 96 % de las funciones.
- **Versión de PsyQ:** el loader elige 4.5.0. Una búsqueda directa de firmas
  (`tools/ghidra/psyqver.py`) da algo más de coincidencias a 4.6 (187 OBJ
  frente a 173), pero forzando 4.6.0 en Ghidra se reconocen **menos**
  funciones (456 frente a 748). Se mantiene 4.5.0. Probablemente se
  enlazaron librerías de versiones mezcladas.

## 4. Bucle principal ✅

- `start` (`0x8002521C`, `2MBYTE.OBJ` de PsyQ): pone a cero la BSS, llama a
  `InitHeap` y salta a `main`.
- `main` (`0x80025D08`): inicialización (`FUN_80025AC0`, `VSync`,
  `SetDispMask`) y un **bucle infinito por fotograma** que:
  - llama en orden a 7 funciones propias de actualización (`FUN_80025720`,
    `FUN_80025BB0`, `FUN_800269E4`, `FUN_80028784`, `FUN_8002DDCC`,
    `FUN_80025EB0`, `FUN_8002C0B4`) y a `FUN_80027408`;
  - mide tiempos con el contador raíz (`GetRCnt(0xF2000001)` antes y
    después de `DrawSync`, y `ResetRCnt`);
  - sincroniza con `VSync(n)`, con `n` leído de una estructura global: el
    divisor de fotogramas;
  - gestiona banderas de estado en una estructura global apuntada desde
    `0x800BACD0`.
- `FUN_800269E4` es la que usa `"Can't open TIM"`: carga de recursos.

El papel de cada una de las funciones de actualización está `SIN RESOLVER`.

## 5. Lo que queda (etapa 3, siguientes pasos)

1. Recorrer el árbol de llamadas de las funciones de `main` y ponerles nombre
   (entrada, lógica, cámara, render, sonido…).
2. Localizar el código que lee `ACE.BPB` y relacionarlo con los formatos ya
   resueltos (`ULZ`, tablas de offsets, modelos): debería confirmar la
   semántica de campos `SIN RESOLVER` de `04-formatos.md`.
3. Repetir la medición en la versión US.
4. Valorar `rea` (`morluto/rea`) como capa sobre Ghidra.
