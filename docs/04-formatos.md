# 04 — Formato de los contenedores de assets  ✅ RESUELTO

Reproducible con `make index` y `make inventory`.
Implementado en `tools/acepack.py`.

El disco tiene dos pares índice+contenedor:

| Índice | Contenedor | Entradas (EU) | Tamaño del pack |
|---|---|---|---|
| `ACE.SPH` | `ACE.SPB` | 72 | 316,3 MiB |
| `ACE.BPH` | `ACE.BPB` | 127 | 33,0 MiB |

## Cabecera del índice

```c
struct AceIndexHeader {   // 0x14 = 20 bytes
    char magic[4];        // "AC3E"
    u32  kind;            // 1 = SPH (streaming) ; 3 = BPH (bancos)
    u32  date;            // BCD. SPH: 0x19991019 (EU) / 0x19991130 (US)
                          //      BPH: 0x1019 / 0x1130 (sin el ano)
    u32  unk;             // SPH: 0 ; BPH: 0x16C3 (EU) / 0x16CF (US)  SIN RESOLVER
    u32  count;           // numero de entradas
};

struct AceIndexEntry {    // 8 bytes
    u32 tag;              // byte alto = categoria
                          // BPB: 24 bits bajos = tamano del registro en u32 (verificado)
                          // SPB: SIN RESOLVER
    u32 sector;           // offset dentro del pack, en sectores de 2048 bytes
};
```

**Validación:** `0x14 + 8*count == tamaño del archivo de índice` en las
**cuatro** combinaciones (EU/US × SPH/BPH). Es prueba fuerte de que la
cabecera mide exactamente 20 bytes y la entrada 8.

```
ACE.SPH EU: count=72  -> 20 + 8*72  = 596   == 596   OK
ACE.BPH EU: count=127 -> 20 + 8*127 = 1036  == 1036  OK
ACE.SPH US: count=88  -> 20 + 8*88  = 724   == 724   OK
ACE.BPH US: count=127 -> 20 + 8*127 = 1036  == 1036  OK
```

## Trampa importante: el índice NO está ordenado

Las entradas **no vienen ordenadas por sector**. En `ACE.SPH` europeo la
entrada 71 apunta al sector 19400, mucho antes que la entrada 70 (157062).

Por tanto el tamaño de cada recurso **no** se puede deducir recorriendo el
índice en orden. Hay que **ordenar por sector** y tomar como límite el
siguiente offset; el último llega al final del pack. `Index.extents()` en
`tools/acepack.py` lo hace así.

Con ese criterio la suma de extensiones cubre **el 100 %** de los dos packs
(316,3 MiB y 33,0 MiB), lo que confirma que no hay huecos ni solapes y que la
interpretación es correcta.

## Semántica del `tag` ✅ RESUELTA EN EL BPB

Reproducible con `make tags`.

En `ACE.BPB`, `(tag & 0xFFFFFF) * 4` es el **tamaño exacto del registro en
bytes**; la extensión en el pack es ese valor redondeado a sector. Se cumple
en **127/127** entradas del disco europeo y **127/127** del americano.

En `ACE.SPB` la regla **no** se cumple (0/72, 0/88): allí el tag significa
otra cosa, y está **resuelto leyendo el código** que arranca el streaming
(`0x8002A34C` en `ACE.BIN` EU; ver `09-codigo.md`):

| Bits | Campo |
|---|---|
| 31 | siempre 1 (si no, el juego rechaza la entrada) |
| 27-28 | modo: 2 = audio XA, 3 = audio XA / vídeo (0 y 1: datos) |
| 24-26 | desplazamiento |
| 19-23 | **canal XA** |
| 0-18 | longitud; extensión en sectores = longitud << desplazamiento |

Validado con `python3 tools/acepack.py <imagen> spbtags`: en **72/72** (EU) y
**88/88** (US) el canal del tag está presente en las subcabeceras XA de la
entrada y la extensión cabe en el pack.

**Consecuencia:** en el SPB las entradas de audio **están entrelazadas**: cada
una es un canal de un flujo de 8 (canales 4, 8, …, 28) y varias comparten los
mismos sectores. El "tamaño" deducido ordenando por sector (`extents()`) solo
vale para el BPB.

## Categorías observadas (byte alto de `tag`)

### `ACE.SPB` — pack de streaming, 72 entradas

**Resuelto qué es** (`python3 tools/acepack.py <imagen> xa`): las
subcabeceras XA dicen que `0xF3`, `0xF4` y `0xFB` son audio XA-ADPCM
(`RT|FORM2|AUDIO`) entrelazado en hasta 10 canales, con ~9 % de sectores
Form 2 de datos intercalados; las dos `0xF8` son `RT|DATA` con audio
intercalado (probable vídeo STR). Son sectores **Form 2**: 2.324 bytes útiles,
así que la tabla de abajo, hecha leyendo 2.048, es solo orientativa. No hay
modelos en el SPB.

| Categoría | Entradas | Volumen | Hipótesis |
|---|---|---|---|
| `0xF8` | 2 | 37,9 MiB | Dos bloques de ~19 MB al inicio del pack: vídeo de apertura (MDEC) |
| `0xF3` | 39 | 205,5 MiB | El grueso: datos de misión / escenario por streaming |
| `0xF4` | 1 | 35,2 MiB | Un único bloque grande |
| `0xFB` | 30 | 37,7 MiB | Bloques de 130 KB a 3 MB: probable audio XA/ADPCM |

El contenido no empieza con un tag de tipo: son datos crudos de streaming
(patrones como `0C 0C 0C 0C…` repetidos, típicos de ADPCM o MDEC).

### `ACE.BPB` — pack de bancos, 127 entradas

Todas de categoría `0xE0`. A diferencia del SPB, el contenido **sí** tiene
estructura de registro y es legible.

#### Cabecera común de registro ✅ VERIFICADA

Reproducible con `make records`. Válida en **122 de los 127** registros:

```c
struct AceRecord {
    u32 type;                   // tipo de recurso
    u32 header_size;            // = 8 + 4*n_secciones
    u32 section_offset[n];      // relativos al inicio del registro,
                                // monotonos crecientes, todos < tamano
};
```

El tipo determina el número de secciones, de forma consistente:

| Tipo | Registros | `header_size` | Secciones | Tabla coherente |
|---|---|---|---|---|
| 1 | 18 | `0x08` | 0 | 18/18 |
| 4 | 22 | `0x14` | 3 | 22/22 |
| 5 | 40 | `0x18` | 4 | 40/40 |
| 13 | 1 | `0x38` | 12 | 1/1 |
| 17 | 41 | `0x48` | 16 | 41/41 |

Que los offsets sean monótonos y estén dentro del registro en **122/122** de
los casos aplicables no es casualidad: la interpretación es correcta.

Los tipos 17 (41 registros, ~600-765 KB cada uno) y 5 (40 registros) son el
volumen real del pack y el objetivo prioritario.

#### Las 5 entradas especiales — son overlays de código (corregido)

**Corrección:** las entradas 1-5 son los **overlays de código** de `ACE.BIN`
(ver `09-codigo.md` §2 y `tools/aceovl.py`): se cargan en `0x800C9DC0` y su
"cabecera" (`u32` de secuencia + punteros `0x800CBxxx`) es el comienzo de su
sección de datos ya enlazada. Lo que sigue es la descripción original de la
etapa 1, válida como observación de bytes pero no como interpretación.

Las entradas 1 a 5 **no** siguen esa convención: su `u32[1]` no es un tamaño
de cabecera sino un puntero o datos directos. Son tablas de sistema:

| # | `u32[0]` | `u32[1]` | Contenido identificado |
|---|---|---|---|
| 1 | 4 | `0x800CBE04` | **Memory card**: `CRD0`, `bu00:BESCESP02066AC3PAL` |
| 2 | 5 | `0x800CC004` | `CRD0`, mismas cadenas de memory card |
| 3 | 6 | `0xF39CFFFF` | Datos con patrón repetitivo |
| 4 | 7 | `0x4B4F4742` (`"BGOK"`) | **Nombres de BGM**: `BGOK01`, `BGOK02`, `BGTT01` |
| 5 | 8 | `0x7473614C` (`"Last"`) | **Textos**: `Last Flight`, `Clear`, `Morceaux` |

Hallazgos notables:

- `bu00:BESCESP02066AC3PAL` — nombre del archivo de memory card. `ESP` y
  `PAL` confirman que es la build española/europea.
- La entrada 5 contiene textos en varios idiomas (`Morceaux` es francés),
  confirmando la tabla multiidioma de la versión europea.
- En las entradas 1 y 2 el `u32[1]` es un **puntero a RAM de PS1 ya resuelto**
  (`0x800CBE04`, `0x800CC004`), y dentro aparecen más (`0x800CBE14`,
  `0x800CBE48`). Es decir, estos bancos se cargan a una dirección fija
  conocida y traen punteros precalculados. Eso **revela el mapa de memoria en
  tiempo de ejecución** y es muy valioso para la etapa 3.

#### Compresión: las secciones van en ULZ ✅ RESUELTO (variantes 0 y 2)

Reproducible con `make ulz-check`. Implementado en `tools/ulz.py`.

Las 16 secciones del tipo 17 (salvo la 15) y la sección 1 del tipo 4 vienen
comprimidas con **ULZ**, un formato LZ de Namco (magic `Ulz\x1a`):

```c
struct UlzHeader {          // 16 bytes
    char magic[4];          // "Ulz\x1a"
    u32  size_variant;      // bits 0-23: tamano descomprimido ; 24-31: variante (siempre 2)
    u32  lit_off_nbits;     // bits 0-23: offset del flujo de literales ; 24-31: nbits
    u32  ref_off;           // offset del flujo de referencias
};                          // flujo de flags: desde 0x10 hasta lit_off
```

Tres flujos independientes. Flags: palabras `u32` LE consumidas del bit 31 al
0. Bit `1` → copiar un byte de literales. Bit `0` → leer un `u16` `v` de
referencias: `distancia = (v & ((1<<nbits)-1)) + 1`, `longitud = (v >> nbits) + 3`.

**Validación:** tras producir exactamente el tamaño declarado, la holgura de
cada flujo es siempre 4 bytes (flags: una palabra terminadora), 0-3
(literales: alineación) y 0 o 2 (referencias: alineación a `u32`), en las
**659 de 659** secciones ULZ. Ninguna referencia apunta fuera de la ventana.

**Variantes** (byte alto de `0x04`), según el descompresor del juego
(`FUN_800AE118` de `ACE.BIN` EU):

- **2**: la descrita. Todas las secciones de primer nivel.
- **0**: mismos flujos, pero cada palabra de flags lleva un **bit centinela**
  (el 1 más bajo no es un token) y una palabra 0 termina el flujo. Las 604
  entradas anidadas del tipo 13; válidas 604/604 con tamaño exacto.
- **1**: como la 0 en unidades de 16 bits. Está en el código, pero no en los
  datos: sin validar.

#### Las texturas SÍ son TIM, dentro del ULZ ✅

Reproducible con `make textures`. Implementado en `tools/tim2png.py`.

La búsqueda anterior de cabeceras TIM dio cero porque se hizo sobre datos
**comprimidos**. Tras descomprimir aparecen TIM estándar de Sony (casi todas
4 bpp con CLUT), con su destino en VRAM. Validación del escaneo:
`bnum == 12 + 2*w*h` en todos los bloques y destino dentro de 1024×512.

Verificado a ojo en el registro 6: una **fuente** con acentos europeos, la
**piel de un avión** vista desde arriba y el **mapa de radar** de la zona.

#### Contenedor genérico: tabla de offsets ✅

Casi todas las secciones descomprimidas empiezan igual:

```c
u32 count;
u32 offset[count];     // offset[0] == 4 + 4*count, monotonos
```

#### Tipo 17 — misión / escenario (41 registros)

Reproducible con `make sections` y `make models`.

| Sec | ULZ | Contenido | EU = US | Estado |
|---|---|---|---|---|
| 0 | sí | **Mapa de teselas** 256×256 `u16` (131.072 B fijos) | sí | ✅ ver abajo |
| 1 | sí | **Teselas de terreno**: tabla de modelos (187-491) | sí | ✅ vértices |
| 2 | sí | **Aviones de la misión con LOD**: 128 huecos (32 × 4 LOD) | sí | ✅ ver abajo |
| 3 | sí | **Modelos pequeños con LOD** (¿armas?): 4 o 16 huecos | sí | ✅ geometría |
| 4 | sí | **Objetos 3D** pequeños: tabla de modelos | sí | ✅ vértices |
| 5 | sí | **Tabla de normales de cara** (`SVECTOR` unitarios en 4.12) | sí | ✅ ver primitivas |
| 6 | sí | **Textos de la misión** (EU: 5 idiomas) | **no** | identificado |
| 7-9 | sí | Pequeñas (8-1.256 B) | sí | `SIN RESOLVER` |
| 10 | sí | **Texturas** del escenario (hasta 279 TIM) | sí | ✅ |
| 11-14 | sí | **Texturas**: avión, radar, fuente, iconos | sí | ✅ |
| 15 | no | **Banco de sonido del SPU** (ver abajo) | **no** | ✅ |

**Sección 0 = mapa de teselas.** 65.536 celdas `u16`; los 9 bits bajos son un
índice de tesela y `max(v & 0x1FF)` coincide **exactamente** con el número de
teselas de la sección 1 en los 41 registros comprobados. Pintado, el mapa del
registro 6 muestra mar, costa y una ciudad en cuadrícula, con la misma silueta
que su textura de radar. **El índice es base 1** (`tesela = s01[(v & 0x1FF) - 1]`):
con base 1 los vértices de bordes compartidos entre teselas vecinas coinciden
en altura en el **99,4 %** de los casos (4.707.247/4.734.343, 41 registros),
frente al 74,6 % con base 0 (`python3 tools/acemodel.py tilemap data/bpb`).
**Los 3 bits altos (9-11)** los interpreta el render (`0x8005FF9C` y el
constructor de la lista de celdas `0x8005CDA4`, ver `09-codigo.md`): cada
uno **anula la reutilización de vértices ya transformados de una tesela
vecina** (`flags ^= flags & (flags << 16)` sobre la palabra de control).
Contraste con los datos (`python3 tools/acemodel.py tilemap data/bpb`): los
bordes izquierdos que no casan con la vecina son el 0,7 % con el bit a 0, y
el 60 % (bit 11), 47 % (bit 9) y 28 % (bit 10) con el bit a 1. Explica el
residuo de la continuidad de bordes. Qué vecino exacto corresponde a cada bit
sigue `SIN RESOLVER` (no basta con alturas iguales: también debe coincidir la
posición del vértice en el bloque).

**Confirmado desde el código** (`FUN_80098B80`, consulta de terreno de
`ACE.BIN`): la celda de una posición del mundo (punto fijo 20.12) es
`(coord >> 23) + 128`, es decir, celdas de **2048 unidades** con la celda
(128,128) en el origen; el mapa se indexa `mapa[z·256 + x]`; `celda & 0x1FF`
es el índice y **0 significa "sin tesela"**, lo que cuadra con la base 1
(la entrada 0 de la tabla de offsets es el contador). La consulta de
colisión no usa los 3 bits altos.

**Comparación EU/US:** las secciones 0-5 y 7-14 son **idénticas byte a byte**
en los 41 registros. Solo cambian la 6 (textos) y la 15. Terreno, modelos y
texturas son comunes a las dos versiones. La 15 difiere porque es el banco de
sonido, cuya dirección base en la RAM del SPU cambia entre versiones.

#### Formato de modelo (secciones 1 y 4 del tipo 17) ✅ vértices y caras

Implementado en `tools/acemodel.py`.

```c
struct ModelHeader {
    u16 zero;
    u16 extent;          // teselas: 4387 fijo ; objetos: >= max|coord|
    u32 off_vertices;    // 0x18 teselas, 0x24 objetos
    u32 off_unk;         // 0 en todos los vistos
    u32 off_prims;       // bloque 1: primitivas planas   (POLY_FT4)
    u32 off_end;         // bloque 2: primitivas Gouraud  (POLY_GT4)
    // objetos: u32 0 ; s16 bbox[6] = xmax,xmin, ymax,ymin, zmax,zmin
};
// en off_vertices:
u32 n;
struct { s16 x0,y0, x1,y1, x2,y2; s16 z0,z1,z2; s16 pad; } group[(n+2)/3];
```

Los vértices van en **grupos de tres**, con los `(x,y)` juntos y los `z`
aparte: es justo lo que el GTE necesita para `RTPT` (transformar 3 vértices
de una vez cargando `VXY0..2` y `VZ0..2` con `lwc2`).

**Validación:** `off_vertices + 4 + 20*ceil(n/3) == off_prims` en **14.095 de
14.095** modelos; en los 421 objetos con bbox, la bbox de la cabecera es
**exactamente** el mín/máx de los vértices decodificados (**421/421**). La
tesela de mar da las cuatro esquinas `(±1024, -1126, ±1024)`.

Las teselas miden 2048×2048 unidades.

**Primitivas** (`u32 n` y `n` registros en cada bloque):

| Off | Tipo | Campo |
|---|---|---|
| 0 | `u8` | `h0` (0-4) — `SIN RESOLVER` |
| 1 | `u8` | `h1`: 0 = cuadrilátero plano, 1 = triángulo, 2 = cuadrilátero no plano partido en `(i0,i1,i2)` + `(i1,i3,i2)` |
| 2 | `u16` | 0 |
| 4 | `u16`×2 | `e0`, `e1`: offsets en bytes a la **normal de la cara** en la sección 5; `e0 = e1` salvo con `h1 = 2` (una normal por triángulo) |
| 8 | `u8[8]` | por vértice, offset en bytes de su `xy` y su `z` en el bloque de vértices (listos para el GTE; los usa la subdivisión de caras cercanas, `0x80060DC0`) |
| 16 | `u8[4]` | índices `i0..i3`; orden de tira: cara `i0,i1,i3,i2`; triángulo si `i3 == i2` |
| 20 | | `uv0`, CLUT (`x=(c&63)*16`, `y=c>>6`), `uv1`, tpage, `uv2`, `uv3` |
| 32 | | FT4 (36 B): `rgb` + `0x2C` · GT4 (48 B): 4 × (`rgb` + `0x3C`) |

Los últimos bytes son literalmente la palabra color+código de un paquete del
GPU de PS1. En las teselas cada bloque va seguido de 12 bytes de ceros por
primitiva (huecos que el motor rellenará con coordenadas de pantalla); en los
objetos no hay relleno. Las CLUT y tpages apuntan exactamente a las zonas de
VRAM donde se cargan las TIM del mismo registro.

**`h1`, `e0`, `e1` y la sección 5** se resolvieron leyendo el código de
intersección con el terreno de `ACE.BIN` (`0x80097EDC`, ver `09-codigo.md`) y
se validaron con `python3 tools/acemodel.py normals data/bpb`:

- 119.003/119.003 `e` son múltiplos de 8 y caen dentro de s5, y en los 41
  registros `máx(e) + 8` es **exactamente** el tamaño de s5
- `h1 = 0` siempre es cuadrilátero (55.746), `h1 = 1` siempre triángulo
  (22.110), `h1 = 2` siempre cuadrilátero (41.147)
- en las 77.831 caras con una sola normal, la guardada es **antiparalela** a
  `(v1−v0)×(v2−v0)` (coseno < −0,99) y tiene módulo 4096
- con `h1 = 2`, `e0`/`e1` casan con los triángulos `(i0,i1,i2)` y
  `(i1,i3,i2)` en el 99,7 % (41.034/41.147)

**Validación:** en **14.095/14.095** modelos los dos bloques se consumen
exactamente, todos los códigos son `0x2C`/`0x3C`, todos los índices son
menores que el número de vértices y los offsets de los bytes 8-15 coinciden
con los índices. Renderizado en alámbrico, el terreno colocado según el mapa
de teselas forma costas continuas.

#### Modelos con LOD (secciones 2 y 3 del tipo 17) ✅

Implementado en `tools/acemodel.py` (`LodModel`). Verificado en **578/578**.

```c
struct LodModelHeader {
    u16 zero; u16 extent;
    u32 off_vertices;    // 0x1C siempre
    u32 off_x2, off_x4;  // 0x1C si no hay bloques extra
    u32 zero2;
    u32 off_gt3, off_gt4;
};
```

- Vértices con el mismo layout de grupos de 3 que los modelos de terreno.
- En los LOD altos hay **tres bloques con los mismos vértices a escala ×1,
  ×2 y ×4** (diferencias de redondeo ≤ 1 en 300/300); el de ×4 alcanza
  exactamente `extent`. Los LOD bajos guardan un solo bloque a escala 1/4
  (`4·máx|v| ≈ extent` en 278/278). Hipótesis: precisión según distancia.
- Primitivas: `u32 n` + POLY_GT3 de 32 B (código `0x34`, 3 colores) y
  `u32 n` + POLY_GT4 de 36 B (`0x3C`, 4 colores). Bloques consumidos exactos.
- En s02 los modelos van de 4 en 4 (mismo `extent`, 155/99/39… vértices):
  un avión con sus niveles de detalle. Renderizado, el modelo 8 del registro 6
  es un avión con alas en flecha y deriva.
- Las texturas que usan están en VRAM (768/800, 256): las pieles de la
  sección 11 de la misma misión. **No** usan la página de la piel del jugador
  (tipo 4, en (704,256)): son los aviones enemigos/aliados de la misión.

#### Bancos de sonido del SPU (tipo 17 s15, tipo 4 s2, tipo 13 s8) ✅

Implementado en `tools/acesnd.py`. Verificado en **63/63** (EU) y **63/63**
(US).

```c
u8  b0;  u8 n;  u16 b2;      // n = numero de entradas; b0, b2 SIN RESOLVER
u32 spu_base;                // destino en la RAM del SPU
u32 size;                    // bytes de ADPCM
struct { u32 id; u16 p0, p1; u16 vol; u8 u0; u8 note;
         u32 adsr; u32 addr; } entry[n];      // 20 B
u8  adpcm[size];             // + relleno de ceros
```

Validación: todos los bloques ADPCM válidos, el último con flag de fin, y cada
`addr - spu_base` alineado y justo detrás de un fin de muestra. Decodificadas a
PCM: 15 millones de muestras sin un solo recorte en EU.

**Corrige una hipótesis anterior:** los valores que cambian entre EU y US
(`0x043C90` / `0x0496D0`) son `spu_base`, no punteros a código de `ACE.BIN`.

#### Tipo 4 — aviones del jugador (22 registros)

| Sec | ULZ | Contenido |
|---|---|---|
| 0 | sí | 168-220 B: puntos de referencia del avión (vectores `x,y,z,0`; varios en la cola, probablemente toberas) — `SIN RESOLVER` en detalle |
| 1 | sí | 2 TIM: **piel 256×256** con 8 CLUT (esquemas de color) e **icono 40×40** |
| 2 | no | **Banco de sonido** (2 muestras: el motor del avión, probablemente) |

**El modelo 3D del avión del jugador no está en estos registros.** El juego
carga el tipo 4 del avión elegido junto con el registro de misión (tabla
`0x800AF79C` de `ACE.BIN`, índice en `+0x392` del estado), pero no trae
geometría. En la tabla de modelos con LOD (s2 del tipo 17) los huecos 0-7
(ids 0 y 1) están **vacíos en las 41 misiones**: se rellenan en tiempo de
ejecución, presumiblemente con el avión del jugador, desde una fuente aún sin
localizar.

#### Tipo 13 — interfaz (1 registro, 0072)

12 secciones sin comprimir; dentro, **cada entrada de tabla es un ULZ
independiente de variante 0** (604 en total, todas válidas con
`make ulz-check`). Casi todo son TIM de interfaz:

| Sec | Contenido |
|---|---|
| 0 | **21 vistas cenitales de los aviones seleccionables** (hangar) |
| 1 | **21 rótulos con sus nombres**: EF2000E Typhoon II, MiG-33 Fulcrum SS, F/A-18U Hornet ADV, F-16XFU Gyrfalcon, R-101/102/103 Delphinus, R-201 Asterozoa, F-16XA Sakerfalcon, F-15S/MT Eagle+, F-22C Raptor II, F/A-32C Erne, Su-37 Super Flanker, R-211 Orcinus, Su-43 Berkut, XFA-36A Game, RF-12A2 Blackbird, R-352 Sepia, XR-900 Geopelia, X-49 Night Raven, UI-4054 Aurora |
| 3, 4, 9 | iconos y piezas de interfaz (110, 98 y 350 TIM) |
| 5 | 4 TIM |
| 8 | banco de sonido |
| 2, 6 | `SIN RESOLVER` |

#### Tipo 1 grandes (16 registros, 0111-0126) — demos

`u32 1, u32 8`, luego una tabla de 2 entradas: la primera siempre de 8.276 B,
la segunda de 140-358 KB. No es código (0 prólogos MIPS), ni ULZ, ni ADPCM.

**Son las demos** (confirmado desde el código): `ACE.BIN` tiene en
`0x800AF7C8` una tabla de 16 entradas `(u16 registro, u8 misión, u8 avión)`
con los registros 111-126, recorrida por un contador de demo (`+0x3AA` del
estado, da la vuelta tras 15). `FUN_80035E78` carga el registro y
`FUN_80035EF8` se lo pasa al reproductor `FUN_8007C548`. El formato interno de
la grabación está `SIN RESOLVER`.

#### Tipo 5 — briefings de misión (40 registros)

Sin comprimir. Textos de briefing ("Neuwork air unit transporting military
supplies…") y listas de enemigos ("R-501 Rhincodon x 2"), con una tabla de 5
entradas por idioma. La sección 3 contiene TIM.

## Lo que queda sin resolver

1. **Dónde está el modelo 3D del avión del jugador.** Descartados con
   análisis estático: tipo 4, tipo 17 s02/s03 (huecos 0-7 siempre vacíos),
   SPB (incluidos sus 28.446 sectores de datos, sin ULZ ni TIM), tipo 13,
   tipo 1 (demos), registros 0 y 69, y `ACE.BIN`. Una búsqueda exhaustiva de
   bloques de vértices en todo el BPB descomprimido solo encuentra los de
   t17 s1-s4. En la versión japonesa los aviones jugables sí son entradas
   propias, con normales por vértice (Krishty, ver `03-versiones.md`).
   Siguiente paso: análisis dinámico (volcado de RAM en misión).
3. Campos `h0`, `h1`, `e0`, `e1` de las primitivas; bits altos del mapa de
   teselas; campos `b0`, `b2`, `id`, `p0/p1`, `u0` de los bancos de sonido.
4. Secciones 7-9 del tipo 17; los tipos 1 grandes; las tablas 1-5 del BPB.
5. El campo `unk` del BPH (no lo usa la rutina de apertura) y los sectores de
   datos intercalados en el audio XA.

## Cómo seguir

```bash
make extract-bpb       # las 127 entradas en data/bpb/
make ulz-check         # descompresor validado
make sections          # mapa de secciones por tipo
make models            # validacion de modelos (terreno, objetos, LOD)
make sounds            # validacion de bancos de sonido
make textures REC=0006 # TIM a PNG en data/png/
```
