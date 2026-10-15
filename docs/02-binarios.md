# 02 — Los binarios del juego

Reproducible con `make info`.

El juego son **tres** ejecutables, no uno. Entre los tres hay ~1,1 MB de
código y datos MIPS.

## `SCES_020.66` — módulo residente

| Campo | Valor |
|---|---|
| Formato | PS-EXE estándar de Sony |
| Entrada `pc0` | `0x80176210` |
| Carga `t_addr` | `0x80160000` |
| `t_size` | 489.472 (`0x77800`) → termina en `0x801D7800` |
| Pila | `0x801FFFF0` |
| Licencia | *Sony Computer Entertainment Inc. for Europe area* |
| Entropía | 6,47 bits/byte |
| Prólogos MIPS | 224 (0,47 por KiB) → **código en claro** |

`2048 + t_size` es exactamente el tamaño del archivo: **no está comprimido**.
Se puede desensamblar hoy mismo.

Carga en la parte **alta** de la RAM (la PS1 tiene 2 MB, de `0x80000000` a
`0x80200000`), dejando libre toda la zona baja. Esto, junto con que
`ACE.EXE` y `ACE.BIN` cargan los dos en `0x80010000`, indica que
`SCES_020.66` es un **loader/capa de sistema residente** que permanece en
memoria mientras intercambia módulos de juego en la zona baja.

## `ACE.EXE` — ejecutable secundario

| Campo | Valor |
|---|---|
| Formato | PS-EXE estándar de Sony |
| Entrada `pc0` | `0x80016BE8` |
| Carga `t_addr` | `0x80010000` |
| `t_size` | 188.416 (`0x2E000`) |
| Licencia | *for **Japan** area* ← resto de la build japonesa |
| Entropía | 2,07 bits/byte (muy baja: mayoría de ceros) |
| Prólogos MIPS | 77 (0,42 por KiB) → **código en claro** |

74 % de sus palabras son cero. Es un módulo pequeño con mucho hueco
reservado. La cadena de licencia japonesa no se regeneró al hacer la
conversión regional.

## `ACE.BIN` — módulo principal, **comprimido**

Cabecera **propietaria de Namco**, no PS-EXE. Magic `PS-Y4a\xe3w`.

| Offset | Valor | Significado | Estado |
|---|---|---|---|
| `0x00` | `PS-Y4a\xe3w` | magic (8 bytes) | — |
| `0x08` | `0x800BB000` | dirección final = carga + tamaño descomprimido | ✅ verificado |
| `0x0C` | `0x6504C` = 413.772 | **tamaño del archivo, exacto** | ✅ verificado |
| `0x10` | `0xAB000` = 700.416 | tamaño descomprimido | ✅ verificado |
| `0x14` | `0x5C` = 92 | offset del inicio de los datos comprimidos (la cabecera mide 0x5C; ahí empiezan literales legibles como `Can't ope`) | ⚠️ probable, sin verificar con un descompresor |
| `0x1C` | `0x8002521C` | punto de entrada | ✅ verificado |
| `0x24` | `0x80010000` | dirección de carga | ✅ verificado |
| `0x28` | `0xAB000` | tamaño | ✅ verificado |
| `0x3C` | `0x801FFFF0` | pila | ✅ verificado |

Coherencia que valida la lectura: `0x80010000 + 0xAB000 = 0x800BB000`, que es
exactamente el campo `0x08`.

**Está empaquetado.** Dos evidencias independientes:

- Entropía del cuerpo: **7,42 bits/byte** (un binario MIPS normal ronda 6,5)
- Solo **6** prólogos `addiu sp, sp, -N` en 400 KB (0,01 por KiB), frente a
  0,47 y 0,42 por KiB en los otros dos módulos

413.772 bytes en disco se expanden a 700.416 en RAM: ratio 1,69:1.

### Consecuencia para el proyecto

**Este es el bloqueo principal del análisis de código.** El juego de verdad
—vuelo, IA, renderizado, misiones— está aquí dentro, y no se puede
desensamblar, ni recompilar estáticamente, ni medir, hasta desempaquetarlo.

Dos vías, ambas válidas:

1. **Localizar el descompresor** dentro de `SCES_020.66` (que sí está en
   claro) y reimplementarlo. Es la vía limpia y la que deja herramienta.
2. **Volcar la RAM desde un emulador** (DuckStation, PCSX-Redux o no$psx
   tienen volcado de memoria y depurador) justo después de que el loader
   descomprima, leyendo `0x80010000`..`0x800BB000`. Es la vía rápida para
   empezar a analizar.

Lo pragmático es hacer (2) para desbloquear el análisis y (1) después, para
tener una herramienta reproducible que no dependa de un emulador.

## Buena noticia: librerías PsyQ de stock

Los binarios conservan los identificadores RCS de las librerías de Sony:

```
$Id: sys.c,v  1.140 1998/01/12 07:52:27 noda  Exp yos $
$Id: bios.c,v 1.86  1997/03/28 07:42:42 makoto Exp yos $
$Id: intr.c,v 1.75  1997/02/07 09:00:36 makoto Exp $
```

Más cadenas como `ResetGraph:jtb=%08x,env=%08x`, `Can't open TIM [%08x]`,
`SPU:T/O [%s]`.

Esto importa mucho: una fracción grande del código es **librería conocida**
(`libapi`, `libgpu`, `libgte`, `libspu`, `libcd`). Esas funciones **no se
decompilan: se reimplementan** contra una capa que traduzca a OpenGL/Vulkan y
audio del host. Es exactamente la táctica de la capa Psy-X de REDRIVER2.
Reduce el trabajo real de forma significativa.

Un paso temprano de alto valor es el **matching de firmas de PsyQ** en Ghidra
para marcar de golpe todas las funciones de librería y quedarse solo con el
código propio de Namco.

## Sobre el uso del GTE

El coprocesador GTE (COP2) hace la matemática 3D. Medido sobre el código en
claro: `SCES_020.66` usa **64** instrucciones COP2 y `ACE.EXE` solo **1**.
Es decir, **toda la matemática de vuelo y cámara está dentro del blob
comprimido** y aún no se puede medir. El GTE tendrá que reimplementarse con
precisión exacta (es aritmética de punto fijo con saturación), o el modelo
de vuelo derivará de forma sutil.
