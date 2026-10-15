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

## `ACE.BIN` — módulo principal ✅ DESCOMPRIMIDO

Reproducible con `make acebin`. Implementado en `tools/acebin.py`.

Cabecera **propietaria de Namco** de `0x5C` bytes, no PS-EXE:

| Offset | EU | US | Significado | Estado |
|---|---|---|---|---|
| `0x00` | `PS-Y` | `PS-Y` | magic | ✅ |
| `0x04` | `0x77E36134` | `0x3E7035AA` | ajuste para que la suma `u32` del archivo entero sea 0 | ✅ |
| `0x08` | `0x800BB000` | `0x800BC000` | dirección final = carga + tamaño descomprimido | ✅ |
| `0x0C` | 413.772 | 415.530 | tamaño útil del archivo (US: 2 bytes de relleno extra) | ✅ |
| `0x10` | 700.416 | 704.512 | tamaño descomprimido | ✅ |
| `0x14` | `0x5C` | `0x5C` | offset del flujo comprimido | ✅ |
| `0x1C` | `0x8002521C` | `0x8002539C` | punto de entrada; desde aquí, estructura `EXEC` de PsyQ | ✅ |
| `0x24` | `0x80010000` | `0x80010000` | dirección de carga | ✅ |
| `0x28` | = `0x10` | = `0x10` | tamaño (repetido) | ✅ |
| `0x3C` | `0x801FFFF0` | | pila | ✅ |
| `0x58` | `0x4288FFB0` | `0xA6E2BA6C` | **suma de las palabras `u32` de la salida** | ✅ |
| `0x5C` | `0x0C` | `0x0C` | primer byte del flujo: bits de distancia (12) | ✅ |

### Formato de compresión

Un LZ de un solo flujo desde `0x5C`:

```
u8 nbits                         # 12
repetir:
    u8 flags                     # 0 = fin
    consumir bits del 0 hacia arriba mientras quede más de uno
    (el bit más alto es un centinela; con flags >= 0x80 salen 7 elementos):
        1 -> 1 byte literal
        0 -> v = b0<<8 | b1:
             distancia = v & 0xFFF  (0 significa 4096)
             longitud  = v >> 12    (0 significa 16)
```

Se descifró primero a mano, siguiendo cadenas conocidas (`"Can't open TIM"`,
`"Error: MemAlloc: Overflow %d bytes (REQ: %d bytes)\n"`), y después se
**confirmó contra el descompresor real** de `SCES_020.66` (`0x801641E4`),
decompilado con Ghidra, que además resolvió el byte `0x5C` y el centinela.

Arranque real (`SCES_020.66`, `0x801642D4`): magic, suma del archivo = 0,
descompresión, final = carga + tamaño, suma de la salida = `0x58`, `Exec()`.
Si algo falla, carga `ACE.EXE` (pantalla anticopia). Ver `09-codigo.md`.

**Validación:**

- La salida mide exactamente el tamaño declarado, en EU y en US.
- El byte 0 terminador cae justo al final del tamaño útil del archivo.
- La suma `u32` del archivo entero es 0, como exige el cargador.
- **La suma `u32` de la salida coincide exactamente con el campo `0x58`** en
  los dos discos. Con "distancia 0 = ceros" en vez de 4096, no coincide.
- El punto de entrada cae tras un `jr ra; nop` y empieza con el bucle de
  limpieza de BSS típico de PsyQ.
- El 51,7 % de los `jal` apuntan a un prólogo, frente al 53,7 % de
  `SCES_020.66` y el 54,3 % de `ACE.EXE`, que están en claro.
- Densidad de prólogos: 1,57 por KiB. Es más alta que la de `SCES_020.66`
  (0,47) porque `ACE.BIN` es casi todo código, y `SCES` arrastra tablas.

Ratio de compresión 1,69:1. El resultado **no se versiona**: va a `data/exe/`,
como binario plano o envuelto en una cabecera PS-X EXE estándar
(`--psexe`), lista para cargarse en Ghidra (etapa 3).

No contiene TIM ni modelos en los formatos conocidos de `ACE.BPB`.

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
Es decir, **toda la matemática de vuelo y cámara está en `ACE.BIN`**, que
ya está descomprimido (ver arriba) y se puede medir en la etapa 3. El GTE tendrá que reimplementarse con
precisión exacta (es aritmética de punto fijo con saturación), o el modelo
de vuelo derivará de forma sutil.
