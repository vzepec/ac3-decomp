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

#### Cabecera común de registro ✅ VERIFICADA (corregida: hay un preludio)

> **Corrección (2026-10-15):** el campo que aquí se llama `header_size` es en
> realidad el **offset de una sección inicial, el preludio**, que va de
> `header_size` a `section_offset[0]`. El formato real es
> `u32 tipo; u32 offset[n+1]` con `offset[0] = 4 + 4·(n+1)`. Se mantiene la
> numeración histórica (sección 0 = la primera tras el preludio);
> `tools/acerec.py` expone el preludio con `prelude_bounds()`. Preludios:
> - **tipo 17**: ULZ que descomprime a **312 B fijos de parámetros de entorno**
>   (muchos tripletes RGB y valores 4.12). El cargador los guarda en
>   `g_mission + 0x18` y `FUN_8005D490` usa `+8` para generar 16 paletas
>   animadas (interpolación con `rsin`, subidas a VRAM desde (320,384)):
>   animación de paletas por misión. Campo a campo, `SIN RESOLVER`.
> - **tipo 4**: los **modelos del avión del jugador** (ver abajo).
> - **tipo 13**: tabla de **36 TIM** en ULZ (56×84, 8 bpp): las **imágenes de
>   las 36 misiones** (ciudades, cañones, la Tierra desde el espacio…).
> - tipo 5: de 4 a 33.156 B, `SIN RESOLVER`.

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
| 6 | sí | **Guion de misión** (6 subentradas; ver abajo). Antes descrito como "textos" | **no** | parcial |
| 7, 9 | sí | Pequeñas (8-1.256 B) | sí | `SIN RESOLVER` |
| 8 | sí | 40 B: **capas de nubes/bruma** (2 niveles), ver efectos | sí | parcial |
| 10 | sí | **Texturas** del escenario (hasta 279 TIM) | sí | ✅ |
| 11-14 | sí | **Texturas**: avión, radar, fuente, iconos, efectos (ver abajo) | sí | ✅ |
| 15 | no | **Banco de sonido del SPU** (ver abajo) | **no** | ✅ |

**Efectos del entorno (texturas de s13 y parámetros), desde el código:**

| Textura | Uso | Función | Datos |
|---|---|---|---|
| s13 TIM 1 (CLUT 315) | **campo de nubes**: rejilla de sprites alrededor de la cámara (vuelta cada `0x4000`), altura fija `0x2400`, POLY_FT4 semitransparentes (`0x2E`, tpage `0x39` aditivo), cuadrante de 64 px al azar | `FUN_8008DD10` | — |
| s13 TIM 2 (CLUT 316 naranja / 317 gris) | **sol y destello** (lens flare): disco y halo; intensidad según lo centrado que esté el sol en pantalla | `FUN_8008A310` | preludio `+0x6C`… |
| s13 TIM 0 (CLUT 313 + i) | **capas de nubes/bruma** (2 niveles, color con semitransparencia) | `FUN_8008C1C8` | sección 8 (`g_mission + 0x3C`) |
| s12 | tiras de 8×8; una parece un misil | `SIN RESOLVER` | |

Corrección: la TIM 2 se tomó primero, por su aspecto, como "bola de fuego"
de explosión; el código muestra que es el sol. **Las explosiones no están
localizadas.** Nota: `g_render + 0x244` = `g_mission + 0x18` (preludio) y
`g_render + 0x268` = `g_mission + 0x3C` (sección 8).

### Sección 6 del tipo 17: guion de misión (parcial)

Al arrancar la misión, `mission_start` (`FUN_80038818`) toma s6
(`g_render + 0x260`) como `DAT_800BADC0`; si su tabla tiene 3 entradas elige
una al azar (variantes de la misión). Las subentradas las consumen:
`+4` → `FUN_8004BBB8`, `+8` → `FUN_8004B760`, `+0xC` → `DAT_800BAD28`
(`FUN_800527C8`), `+0x10` → `DAT_800BAD20` (`FUN_80052574`), `+0x14` →
`FUN_80054B54`.

La subentrada `+4` empieza con 4 contadores (bytes 0, 4, 8, 0xC) y 14
offsets en palabras a subtablas (`DAT_800BAD5C`, `…AD7C`, `…AD70`, `…AD9C`,
`…ADB8`, `…ADB4`, `…ADA8`, `…ADB0`, `…AD74`, `…AD98`, `…ADA0`, `…AD90` y dos
tablas de punteros). Volcado: `python3 tools/acescript.py data/bpb/0006_*.bin`.

**Tablas de aparición** (confirmadas en el código):

| Tabla | Registros | Ranuras de entidad | Inicializa |
|---|---|---|---|
| `DAT_800BAD70` | contador del byte 8, 44 B | 0-31 (aviones; la 0 es el jugador) | `FUN_8004C258` |
| `DAT_800BAD74` | contador del byte 0xC, 36 B | 32-63 (objetos de tierra y otros) | `FUN_8004CDB0` |

| Byte | Campo |
|---|---|
| 0 | bando: 0/1/2 → banderas de entidad `0x40`/`0x80`/`0x100` |
| 1 | tipo (entidad `+0x48`; p. ej. `0x40`/`0x41` objetos, `0x18`/`0x1F`/`0x20` aviones) |
| 2 | **modelo** (entidad `+4`); `0xFF` = sin modelo |
| 3 | índice en los registros de 40 B de la subentrada `+8` (parámetros del tipo) |
| 4-0x0F | posición x, y, z (s32, << 12) |
| 0x14-0x19 | orientación rx, ry, rz (s16, a `+0x18` de la entidad). **Unidad: 0x4000 = 360°**, y "adelante" en PS1 es (sen ry, cos ry): la matriz de la entidad (`FUN_80030534`) indexa una tabla de senos de periodo 0x4000 (`0x80010498`, coseno en `+0x1000`), y en las demos el rumbo grabado coincide con la dirección del movimiento con un error mediano de 3-9° (`tools/acedemo.py --heading`; con 4096 o 0x10000, 90-130°). En la misión 6, el jugador (`0x3800` = 315°) y las parejas enemigas (`0x1800` = 135°) se encaran. (Corrige dos versiones anteriores: "0x10000 = 360°" y "4096 = 360°".) |
| 0x20-0x23 | índices en otras subtablas (`AD9C` ×16 B, `ADB4`/`AD98` ×24 B, …) |

**El modelo indexa tablas distintas** según la tabla de aparición:

- aviones: grupo LOD `id·4 … id·4+3` de s2/s3. Validado en la misión 6: los
  modelos 2, 3 y 4 caen exactamente en los grupos ocupados de s2 (ranuras
  8-11, 12-15, 16-17), tres aviones distintos (extent 229, 216, 164).
- objetos: índice directo en la tabla de s4 (`Model`). Validado: el mismo
  tipo usa la misma forma en misiones distintas aunque cambie el índice
  (tipo `0x40` → modelo de 103×43, tipo `0x41` → 90×43, en las misiones 6 y
  12), y los objetos están a ras de suelo (y = −1.150 con la tesela a −1.126
  y la base del modelo 29 unidades por debajo del origen). Cada modelo de
  objeto va precedido de una versión baja (altura 15 frente a 43): con toda
  probabilidad la versión destruida (`FUN_80050C84` dibuja `modelo +
  desfase`), `SIN RESOLVER`.

La subentrada `+4` contiene también el **guion de eventos**, que interpreta
`FUN_80053704` (bytecode: 0x15 activa una entidad, 0x16 la desactiva,
0x0B-0x14 llaman a subórdenes…), `SIN RESOLVER` en detalle.

Mapa de punteros del cargador (`mission_load`): `ulz_decompress` devuelve el
final de lo escrito, así que `g_mission + 0x18` = preludio, `+0x1C` = s0 …
`+0x3C` = s8, `+0x40` = s9; y `g_mission + k` se copia a `g_render + 0x22C + k`.

### Preludio del tipo 17: cielo y niebla (desde el código)

El preludio está **comprimido con ULZ** (empieza por `Ulz\x1a`) y
descomprime a 312 B. El juego lo apunta desde `g_mission + 0x18`
(`g_render + 0x244`). Volcado: `python3 tools/acesky.py data/bpb/0006_*.bin`.

| Offset | Tipo | Uso | Función |
|---|---|---|---|
| `+0x02` | u16 | banderas: `0x100` → `FUN_80094300` (usa la sección 8), `0x200` → `FUN_80093FB8` (textura de nubes, CLUT 315), `0x400` `SIN RESOLVER` | `FUN_800936FC`, `FUN_80093E2C` |
| `+0x04` | RGB | color bajo el horizonte (mar/suelo lejano) | `FUN_800933CC` |
| `+0x08` | RGB | **niebla**: color lejano de la GTE (`ldfc` ×16) y color del horizonte | `FUN_8005D5FC`, `FUN_800933CC` |
| `+0x10` | s16 | altura de las bandas 2 y 3 del cielo (−2·v y −v/2) | `FUN_800931B8` |
| `+0x14` / `+0x18` | RGB | cielo alto, mirando al sol / de espaldas | `FUN_800933CC` |
| `+0x1C` | RGB | **cenit** (OR `0x030303`) | `FUN_800933CC` |
| `+0x20` / `+0x22` | s16 | altura de las bandas 1 y 0 (−2·v) | `FUN_800931B8` |
| `+0x24` / `+0x28` | RGB | cielo bajo, mirando al sol / de espaldas | `FUN_800933CC` |
| `+0x3E` | s16 | rumbo del sol (mezcla de los colores de sol/espaldas según el rumbo de la cámara) | `FUN_800933CC` |
| `+0x6C` | s16 | ≠ 0: hay sol (`FUN_8008A310`) | `FUN_80083B78` |
| `+0x6E` | u8 | ≠ 0: capas de bruma (`FUN_8008C1C8`) | `FUN_80083B78` |
| `+0x6F` | u8 | clima: 1-5 = entrada de la tabla `0x800B2208` (5 = campo de nubes) | `FUN_80083B78` |

**Cómo se dibuja el cielo** (`FUN_80093E2C`): no es una textura sino
polígonos planos y Gouraud a pantalla completa, en bandas separadas por
anillos alrededor de la cámara. Anillo k = puntos a altura `h_k` sobre el mar
y a distancia horizontal `D_k`: banda 0 (`+0x22`, D = 65534), banda 1
(`+0x20`, 65534), banda 2 (`+0x10`·2, 61440), banda 3 (`+0x10`/2, 30720) y el
horizonte (altura 0, en el borde del mundo: `sqrt(0x6800² − altura²)`, radio
0x6800 = 26.624). Colores de arriba abajo: cenit → cielo alto → cielo bajo
→ niebla (banda plana hasta el horizonte); bajo el horizonte, `+0x04`.
Los colores alto/bajo se mezclan una vez por fotograma según el rumbo de la
cámara respecto a `+0x3E`. `SIN RESOLVER`: la unidad exacta de esa mezcla
(el código hace `(2·sol − rumbo/2) & 0x1FFF`, plegado a 0..0x1000).

### Efectos de combate (registro común 0000), desde el código

El registro **0000** (tipo 5) trae dos TIM fijos en VRAM con todos los efectos
de combate. Exportar: `python3 tools/acefx.py sheet|frames data/bpb/0000_*.bin`.

| TIM | VRAM | CLUT | Contenido |
|---|---|---|---|
| s0 | (448,256), tpage `0x17` | 16 filas en (352,256) | rejilla 4×4 de celdas de 64 px: explosión (celdas 6-15), fuego, humo; las filas 0-1 a media escala (celdas de 32 px) |
| s1 | (512,256), tpage `0x18` | 12 filas en (352,272) | celdas de 32 px: salpicaduras/restos (v=0/32/64), llamas (v=96-159), estelas alargadas de proyectil (u=0xB0-0xEF), anillos |

**Partículas** (depósito `DAT_800BAE00 + 0x600`: 3 grupos × 32, máscara de
bits por grupo, `FUN_8007E76C` reserva). Registro de 28 bytes:

| Offset | Campo |
|---|---|
| `+0x00` s16 | edad: empieza en -1 (o más), +1 por tick (`FUN_800836AC`; 25 ticks/s en la EU, 30 en la US, ver `09-codigo.md` §9b); se dibuja si ≥ 0 |
| `+0x02` s16 | vida en ticks; al alcanzarla la partícula se libera |
| `+0x04/08/0C` s32 | posición (mundo << 12) |
| `+0x10/12/14` s16 | velocidad (se suma ×0x100 por tick) |
| `+0x16` u16 | movimiento: bits 0-2 rozamiento (v -= v >> n), bits 3-4 gravedad (+0x8AA/0x1A00/0x3400 a vy) |
| `+0x18` u16 | bits 0-3 parámetro, bits 4-7 **tipo** (índice en la tabla `0x800B221C`), bits 8-9 **mezcla** |
| `+0x1A` s16 | tamaño (unidades de mundo; se proyecta como tamaño × H / z) |

| Tipo | Dibujante | Aspecto |
|---|---|---|
| 0 | `FUN_80084324` | **explosión**: fotograma = edad×10/vida + 6 (celdas 6→15 de s0); mezcla 0 → CLUT 1 y semitransparencia media; si no, CLUT 0 y modo GPU = mezcla (1 = aditivo) |
| 1 | `FUN_8008451C` | **humo**: mismas celdas (6 + 10·t), CLUT 3 (gris), crece de tamaño s a 2s (3s si parámetro ≠ 0) y se funde hacia el color de niebla; mezcla 2 = sustractiva (humo oscuro), 1 = aditiva (claro) |
| 2 | `FUN_8008473C` | salpicaduras/restos en celdas de 32 px de s1 (6 u 8 fotogramas) |
| 3 | `FUN_80084934` | sprite que gira (tabla de senos `0x800B4F84`), `SIN RESOLVER` |

Valores de los generadores (`+0x18`): `0x100` = explosión aditiva,
`0x210` = humo oscuro, `0x111` = humo claro, `0x130` = tipo 3.

**Cola de eventos de efecto** (`FUN_8007C8AC`): los objetos no crean
partículas directamente; emiten un evento que `FUN_8007D9CC` reparte:
clase 3 = efecto puntual (7 variantes, `FUN_8007E3A0`), clase 4 = efecto con
velocidad (10 variantes, `FUN_8007E4DC`), clase 5 = estela/restos
(`FUN_8007E658`), clase 1 = efecto global (`FUN_8007E384`). Cada variante
tiene un envoltorio público (p. ej. 3.0 = `FUN_8007EA98`, 4.8 humo =
`FUN_800816F0`).

**Impacto de misil** (`FUN_800684BC` → `FUN_8007F3A8` → `FUN_8007F15C`,
sonido `0x1D`; sin impacto: efecto 4.0 y sonido `0x1C`):
1 explosión (vida 14 ticks, tamaño 256, aditiva) + 1 destello (depósito
`+0x33C0`, vida 9-12, tamaño 400-463) + 8 restos (depósito `+0x1098`): 2
fragmentos ardiendo con gravedad (tamaño 240-271) y 6 chispas.
Explosión pequeña genérica (`FUN_8007E7E0`): vida 8, tamaño 50-81
(100-131 en la variante 2) + 2 a 5 restos.

**Disparos en línea recta** (`FUN_8006AE1C`; objetos de 0x3C bytes en
`DAT_800BAF08 + 0x15D0`, 4 grupos × 32, con dos puntos cabeza/cola): los
crea `FUN_80069864` (cola en `+0x1550`) y los inicializa `FUN_80069940`
según su clase. Salen del punto de disparo del avión (s0 `+0x90`) en
`FUN_8006FD2C`, llamada con la clase 0-5 desde `FUN_8006FE60` …
`FUN_80070088`. Se dibujan como segmentos, no como partículas:

| Clase | Banderas | Lista | Dibujo |
|---|---|---|---|
| 0 | `0x20410` | `0x10` | `FUN_8008F204`: línea Gouraud semitransparente (trazadora del cañón) |
| 1 | `0x20820` | `0x20` | `FUN_8008F388`: sprite estirado de 0000 s1 (u = 0xB0, CLUT `0x4496`) |
| 2 | `0x41040` | `0x40` | `FUN_8008F660` (u = 0xC0) |
| 3 | `0x82080` | `0x80` | `FUN_8008F940` (u = 0xE0) |
| 4 | `0x94103` | `0x100` | no se dibuja en esta pasada |
| 5 | `0x98202` | ninguna | `FUN_8008FB00` (u = 0xD0) |

**El misil estándar no está aquí**: es un objeto con modelo (depósito
`DAT_800BAF08 + 200`, 0xA4 B, modelo = id + 0x17) y deja la estela de humo
A (abajo). Corrige una versión anterior que suponía "probablemente `F388`".
Qué arma especial es cada clase 1-5: `SIN RESOLVER`.

Tabla de armas `0x800B0A04`: 5 actualizaciones + 5 finales.

**Estelas de humo y de vapor** (búferes circulares en `DAT_800BAF38`;
actualiza `FUN_8007C44C` cada tick). Corrige la idea anterior de que el misil
no deja humo:

| Sistema | Fuente | Puntos | Vida | Dibujo |
|---|---|---|---|---|
| A (32 estelas × 32 puntos de 4 posiciones) | proyectiles con bandera `&8` (`FUN_8007B36C`): un punto nuevo cada 4 fotogramas (el resto actualiza el último), 4 posiciones a lo largo del eje del proyectil (tabla `0x800B1E94`) | `FUN_8007A60C` / `FUN_8007A7C0` | 90 ticks (`FUN_8007A8B4`) | `FUN_8007A990` |
| B (32 × 32 puntos de 2 posiciones) | aviones con `+5 & 8` (`FUN_8007C164`): un punto cada 2 fotogramas, dos posiciones según el tipo de avión (tabla `0x800B1EB4`, 16 B por tipo): **estelas de las puntas de ala** | `FUN_8007B5F0` / `FUN_8007B77C` | 63 ticks (`FUN_8007B834`) | `FUN_8007B928` |

Ambas se dibujan como bandas de POLY_G4 semitransparentes sin textura entre
puntos consecutivos (LINE_G2 semitransparente cuando están lejos).

Color de la A (`FUN_8007A990`): `color = FC · I / 4096` (`DPCS` con RGB = 0),
con FC = bytes `+0x20..+0x22` de s8 (×16) e intensidad por punto `I =
edad·128` mientras `edad·16 < restante`, y después `I = restante·32`: sube
en ~6 ticks hasta ~0,66 y cae linealmente hasta 0 al final de la vida.
`SIN RESOLVER`: qué color resulta de verdad, porque ese campo de s8 no
parece un RGB (en 22 de 41 misiones vale `80 80 00`; el primer byte varía
entre 0x10 y 0xB0 y hay valores como `80 FF D0`), y el modo de mezcla, que
fija un paquete de modo de dibujo fuera de esta función. Hace falta un
emulador para cerrarlo.

**Otros**: `FUN_8008EBDC` (postquemador, llamas de s1), `FUN_80090478` /
`FUN_80090FE0` (cintas Gouraud entre dos puntos, depósito `+0x3964`; las
emiten el rayo láser `FUN_80081B88` y el efecto 4.9).

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
| 0 | `u8` | `h0` (0-6; en las 41 misiones: 0×540, 1×10.049, 2×30.331, 3×69.322, 4×1.635, 5×7.120, 6×6) — `SIN RESOLVER`. No lo leen los renders de primitivas (`prims_ft4_draw`, `prims_gt4_draw`, `prim_subdivide`) ni los rayos contra el terreno (`terrain_raycast`, `terrain_raycast2`, que solo miran los bits `0x100`/`0x200` de `h1`); no depende del tamaño de la cara. Quizá sea un dato del editor sin uso en el juego |
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
  (`4·máx|v| ≈ extent` en 278/278). **Confirmado en el código**
  (`scene_objects_sort`, `0x80065CC4`): son niveles de **precisión según la
  distancia**; lejos se usa ×1, a media distancia ×2 y cerca ×4, desplazando la
  traslación `>> (2 − nivel)`.
- Primitivas: `u32 n` + POLY_GT3 de 32 B (código `0x34`, 3 colores) y
  `u32 n` + POLY_GT4 de 36 B (`0x3C`, 4 colores). Bloques consumidos exactos.
- En s02 los modelos van de 4 en 4 (mismo `extent`, 155/99/39… vértices):
  un avión con sus niveles de detalle. Renderizado, el modelo 8 del registro 6
  es un avión con alas en flecha y deriva.
- Las texturas que usan están en VRAM (768/800, 256): las pieles de la
  sección 11 de la misma misión. **No** usan la página de la piel del jugador
  (tipo 4, en (704,256)): son los aviones enemigos/aliados de la misión.

#### Bancos de sonido del SPU (tipo 17 s15, tipo 4 s2, tipo 13 s8, 0000 s3) ✅

**Cómo los usa el juego** (resuelto en 2026-10-16): `FUN_8002D544` copia las
entradas de cada banco a una tabla de sonidos de registros de 20 B
(`DAT_800C37D8`) **a partir de la ranura `b0`** de su cabecera: banco común
del registro 0000 (s3, 76 sonidos, ranuras 0-75), avión (ranuras 84-85: el
motor), misión (88-107) y el de la interfaz (tipo 13, 84-106, solo en
menús). El juego reproduce por id = ranura (`FUN_8002D724`, `FUN_8002EFA4`
con posición). La voz usa `note` = campo "vol" (0x3C00 = nota 60) y
`sample_note` = `note << 8 | u0`, así que la frecuencia es
`44.100 · 2^((nota − nota_base)/12)` Hz: en 436 de los 963 sonidos sale
exactamente 11.025 o 22.050 Hz. Los sonidos con posición llevan una
desafinación aleatoria de ±2 semitonos (`FUN_8002ECB8`). Ids identificados:
0x0E lanzamiento del misil, 0x1A otra arma, 0x11-0x15 disparos rectos,
0x1B avión destruido, 0x1C explosión sin impacto, 0x1D impacto de misil,
0x3B-0x3D menús; el motor (84/85) lo gestiona `FUN_8002FFAC`.


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
| 0 | sí | 168-220 B: **parámetros del avión**. `+0xC` byte 0 = **número de motores** (verificado contra la realidad: los dos F-16 valen 1; todos los bimotores reales, 2); byte 1 = 1 en F-15S/MT, F-22, Su-37 y otros: probablemente toberas vectoriales (`SIN VERIFICAR`). Siguen vectores `x,y,z,0`, los puntos de tobera. Lo consume el efecto de motores `engine_exhaust_draw` (`0x8008A874`) |
| 1 | sí | 2 TIM: **piel 256×256** con 8 filas de CLUT e **icono 40×40**. Corrección: las 8 filas **no** son esquemas de color; cada primitiva usa la fila de la zona de la piel que pinta |
| 2 | no | **Banco de sonido** (2 muestras: el motor del avión, probablemente) |

**Modelos del avión del jugador ✅** — en el **preludio** del registro
(`0x14` hasta la sección 0), comprimido en ULZ: una tabla de 12 `LodModel`.
Los carga `player_aircraft_load` (`0x80035C90`) y el objeto del jugador usa el
modelo 0 de la tabla LOD (huecos 0-3, vacíos en los datos de misión).

| Entradas por avión | Tipo |
|---|---|
| 1 | `LodModel` clásico (colores precalculados) |
| 4 | **iluminado**: el cuarto campo de cabecera es `off_normals` (bloque de `SVECTOR` sin contador); primitivas de 24 B en los dos bloques: `u32 0`, `u8 vértices[4]`, `u8 normales[4]`, `uv0`+CLUT, `uv1`+tpage, `uv2`, `uv3` |
| 7 | **solo vértices** (`0xFFFFFFFF` en los tres últimos campos): posiciones alternativas de las piezas móviles |

**Ensamblado:** la tabla de 12 piezas se cuelga del hueco id 0 / nivel 3 de
la tabla LOD (`*(tabla + 0xC)`, en `FUN_80032514`), y el render compone el
avión tomando entradas concretas (vistas: e1, e6, e8), con el mismo nivel de
precisión para todas. Varias entradas son estados alternativos de una pieza
(e3-e6 de e2, e8 de e7, e10-e11 de e9). El repartidor recoge, para la
precisión elegida, los punteros a los bloques de vértices de las 10 entradas
siguientes; la elección o interpolación del estado de cada pieza
(aerofrenos, timones, tren…) la hará el callback por pieza de
`lod_parts_draw`. Detalle `SIN RESOLVER`.

Validación (`python3 tools/acemodel.py player data/bpb`): **264/264**
entradas en los 22 aviones; **7.895/7.895** primitivas texturizan desde la
página de la piel del jugador (704,256) con su CLUT (336, 312-319).
Renderizado con textura (`python3 tools/acemodel.py export-player
<registro> <dir>` produce `.obj` + `.mtl` + una PNG por fila de CLUT), el
registro 0047 es un ala volante blanca y roja (igual que una de las vistas del
hangar) y el 0055 un F-15 azul con todos sus detalles en su sitio: geometría,
UV, CLUT y piel validados de extremo a extremo.

**Registro → avión** (`python3 tools/acemodel.py hangar data/bpb`: silueta
cenital frente a las 21 vistas del hangar, IoU 0,77-0,95, siempre en orden):
el índice `i` (registro `47 + i`) es el avión `i − 1` del hangar para
`i = 1..21`. El índice 0 (registro 0047) es otro **XR-900 Geopelia** (IoU
0,93): probablemente el prototipo oculto que menciona Krishty.

| Registro | Avión | Registro | Avión |
|---|---|---|---|
| 0047 | XR-900 Geopelia (¿prototipo?) | 0058 | Su-37 Super Flanker |
| 0048 | EF2000E Typhoon II | 0059 | R-102 Delphinus#2 |
| 0049 | MiG-33 Fulcrum SS | 0060 | R-211 Orcinus |
| 0050 | F/A-18U Hornet ADV | 0061 | Su-43 Berkut |
| 0051 | F-16XFU Gyrfalcon | 0062 | R-103 Delphinus#3 |
| 0052 | R-101 Delphinus#1 | 0063 | XFA-36A Game |
| 0053 | R-201 Asterozoa | 0064 | RF-12A2 Blackbird |
| 0054 | F-16XA Sakerfalcon | 0065 | R-352 Sepia |
| 0055 | F-15S/MT Eagle+ | 0066 | XR-900 Geopelia |
| 0056 | F-22C Raptor II | 0067 | X-49 Night Raven |
| 0057 | F/A-32C Erne | 0068 | UI-4054 Aurora |

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
`FUN_80035EF8` se lo pasa al reproductor `FUN_8007C548`.

**Formato de la grabación** ✅ (`tools/acedemo.py`): una demo es la
grabación de la **cola de eventos** del juego (`FUN_8007C8AC`, la misma que
alimenta efectos y entidades durante la partida). El registro tiene un solo
bloque con una tabla de 2 offsets:

| Bloque | Contenido |
|---|---|
| 0 (0x2054 B) | estructura del grabador (`DAT_800BAF40`): `+0` número de marcas de fotograma, `+0xC` posición de escritura, `+0x10` evento más antiguo (relativas al bloque 1; −1 = sin usar), `+0x2050` longitud |
| 1 | búfer circular de palabras; la vuelta está en `longitud − 0x40` |

Evento: tamaño = bits 0-3 de la primera palabra (en palabras), clase = bits
4-6 (`FUN_8007C778`). El grabador escribe el evento entero aunque pase del
final (de ahí los 0x40 B de margen) y vuelve al principio; el lector hace lo
mismo (`FUN_8007C7E8`). Una palabra igual a 1 es una **marca de fotograma**.

| Clase | Tamaños | Emisor / uso |
|---|---|---|
| 0 | 1 | marca de fotograma (valor 1) y estado del jugador (`FUN_8007C948`, subtipo en bits 7-9) |
| 1 | 4 | ~3,4 por fotograma: **instantánea de entidad** (`FUN_8007C6DC`): entidad = bits 9-14; palabras 1-3 = x, y, z (20 bits altos, coma fija << 12) y rx, ry, rz en los 12 bits bajos (ángulo >> 2, 0x4000 = 360°). Validado: la trayectoria del jugador es continua (mediana 25 u por fotograma, máximo 82) |
| 2 | 1, 4, 6 | entidades (`FUN_8007CC34`: 6 palabras = posición y ángulos) |
| 3 | 1, 3, 4, 5 | efectos: global, puntual, con velocidad, estela (`FUN_8007D940`, `D66C`, `D720`, `D830`) |
| 4 | 2 | `FUN_8007CEB4`, `SIN RESOLVER` |

Validación: en las 16 demos, las marcas contadas recorriendo el búfer
coinciden exactamente con `+0` (de 842 a 5.477 fotogramas: de 34 s a 3,6 min
a 25 ticks/s).

#### Superficies móviles del avión del jugador

Los 12 modelos del preludio del tipo 4 tienen un orden fijo. Los modelos
2-11 son **posiciones extremas** de tres piezas, y el juego **interpola**
entre la neutra y la extrema (corrige una versión anterior de este documento
que hablaba de "estados discretos"):

| Modelo | Pieza | Medido con `tools/acesurf.py` |
|---|---|---|
| 0 | tobera | — |
| 1 | fuselaje | — |
| 2 | alas, neutra | — |
| 3 / 4 | alas: alerones a un lado / al otro (antisimétrico) | 19 de 22 aviones |
| 5 / 6 | alas: cabeceo (simétrico; el signo cambia según el avión) | 18 de 22 |
| 7 / 8 | pieza dorsal: aerofreno recogido / abierto (sube 30-42 u.) | 10 de 22 |
| 9 / 10 / 11 | deriva: timón neutro / a +x / a −x | 17 de 22 |

Los registros 47, 65 y 66 (diseños ficticios) tienen todas las posiciones
idénticas: sin superficies móviles.

**De los mandos a los vértices** (desde el código):

1. `FUN_8006B90C` (y sus variantes `FUN_8006BEA8`, `FUN_8006C58C`,
   `FUN_80072494`) lee 4 ejes de mando de `g_state + 0x120` (bits digitales
   en `+8`, o analógicos en `+0x1C`..`+0x28` si `+0x18` lo indica) y escribe 5
   valores s16 en `DAT_800BAF18`, cada uno acercándose a su objetivo
   `|dif|/4 + 2` por tick:

   | Valor | Objetivo (±0x1000) | Pieza |
   |---|---|---|
   | `v[0]` | eje C (bits `0x2000000` / `0x4000000`) | alerones (3 si > 0, 4 si < 0) |
   | `v[1]` | eje A (bits `0x800000` / `0x1000000`) | cabeceo (5 si > 0, 6 si < 0) |
   | `v[2]` | eje B (bits `0x20000000` / `0x40000000`) | timón (10 si > 0, 11 si < 0) |
   | `v[3]` | `(acelerador + 0x1000) / 2` | intensidad de las toberas (`FUN_8008A874`) |
   | `v[4]` | `−2·(acelerador + 0x800)` si el acelerador < −0x800 (±0x80 por tick) | aerofreno (8) |

2. `FUN_800658B8`, antes de dibujar, genera los vértices de cada pieza con
   `FUN_800652CC`: `v = base + (extremo − base) · t / 4096` (operación INTPL
   de la GTE). Timón: `t = |v[2]|`; aerofreno: `t = v[4]`. Alas
   (`FUN_80065728` + `FUN_80065448`): primero `mezcla = lerp(alabeo,
   cabeceo, r)` con `r` según la proporción entre `|v[0]|` y `|v[1]|`
   (0x800 si son iguales), y después `lerp(neutra, mezcla, m)` con `m =
   (|v[0]| + |v[1]|) · max(r, 1 − r)`, limitado a 0x1000.

Qué eje físico del mando es cada letra (A, B, C) depende de la
configuración de controles: `SIN RESOLVER`; por la pieza que mueven, A es el
cabeceo, B la guiñada y C el alabeo.

#### Tipo 5 — briefings de misión (40 registros)

Sin comprimir. Textos de briefing ("Neuwork air unit transporting military
supplies…") y listas de enemigos ("R-501 Rhincodon x 2"), con una tabla de 5
entradas por idioma. La sección 3 contiene TIM.

## Lo que queda sin resolver

1. ✅ Resuelto: el avión del jugador está en el preludio del tipo 4 (no lo
   veíamos porque el modelo de registro ignoraba esa sección). Pendiente:
   cómo se ensamblan las piezas y se interpolan los estados alternativos.
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
