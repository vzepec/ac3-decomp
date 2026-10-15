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
    u32 tag;              // byte alto = categoria ; 24 bits bajos = ID  SIN RESOLVER
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

## Categorías observadas (byte alto de `tag`)

### `ACE.SPB` — pack de streaming, 72 entradas

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

#### Las 5 entradas especiales

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

#### ⚠️ Resultado negativo importante: no hay TIM estándar

Se buscó la cabecera TIM (magic `10 00 00 00` seguido de un campo de flags
con modo de color plausible) en registros de tipo 17 y 4: **cero
coincidencias**. Las texturas **no** se almacenan como TIM suelto dentro de
estos registros.

Hipótesis a probar: van empaquetadas en formato propio dentro de una de las
secciones, o pre-formateadas como un bloque listo para subir a la VRAM del
GPU con `LoadImage` (en cuyo caso no llevarían cabecera TIM, solo un
rectángulo de destino y los píxeles).

No perder tiempo buscando TIM de nuevo.

## Lo que queda sin resolver

1. **Semántica de los 24 bits bajos de `tag`.** Comprobado que **no** son el
   tamaño en bytes. Candidatos: ID de recurso, hash de nombre, o un par
   `(tipo:16, índice:16)` — en el SPB el byte alto varía en pasos de `0x20`
   (`0xF340`, `0xF360`, `0xF380`…), lo que apunta a un campo compuesto.
2. **El campo `unk` en `0x0C` del BPH** (`0x16C3` / `0x16CF`).
3. **Los formatos internos** de cada tipo de registro (4, 5, 6, 7, 8, 17).
   El tipo 17 es el objetivo prioritario: contiene la geometría y las
   texturas.
4. **Qué contiene `ACE.DAT`** (42 MB, solo en la versión europea). No tiene
   índice propio en el disco; probablemente lo indexa el código.

## Cómo seguir

```bash
make records                  # tipos de registro y tablas de seccion
make extract-bpb              # extrae las 127 entradas a data/bpb/
```

Siguiente objetivo: **las 16 secciones de un registro de tipo 17**. Para cada
sección, mirar su tamaño y sus primeros bytes y clasificarla. Se buscan:

- Un bloque de píxeles para VRAM (sin cabecera TIM, ver arriba) junto con un
  rectángulo de destino
- Una CLUT (paleta): 16 o 256 entradas de 16 bits
- Listas de primitivas del GPU o geometría: tripletes de `s16` como
  coordenadas de vértice

Comparar la **misma** entrada entre el disco europeo y el americano ayuda:
lo que cambie es dato localizado, lo que no cambie es geometría o código.
