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

**CORRECCIÓN (2026-10-15): sí hay overlays de código** — verificado con
`python3 tools/aceovl.py list data/exe/ace_eu.bin data/bpb`.

La tabla de modos (`0x800AE544`, registros `{pasos, id, inicio, fin}` de
16 B) que recorre el despachador `FUN_80025EB0`:

| Modo | Código | Rango | Tamaño |
|---|---|---|---|
| 0, 1, 2, 5, 6 | residente (`ACE.BIN`); el 2 es la carga de misión | `0x80010000`- | |
| 3 | **overlay 2** = entrada 2 del BPB | `0x800C9DC0`-`0x800F9E18` | 196.696 |
| 4 | **overlay 1** = entrada 1 | `0x800C9DC0`-`0x800DB010` | 70.224 |
| 7 | **overlay 3** = entrada 3 | `0x800C9DC0`-`0x800DECE8` | 85.800 |
| 8 | **overlay 4** = entrada 4 | `0x800C9DC0`-`0x800D96C0` | 63.744 |
| 9 | **overlay 5** = entrada 5 | `0x800C9DC0`-`0x800CBA38` | 7.288 |

Pruebas: el tamaño del registro según su `tag` coincide exactamente con el
rango en 4 de 5 overlays (en el 4 difiere en 4 bytes); los punteros de pasos
caen tras un `jr ra` (inicios de función) con base `0x800C9DC0`; el código de
los overlays referencia globales del residente (`0x800BACD0`). Las entradas
1-5 del BPB no son "tablas de sistema": son estos overlays. El overlay 1
contiene las cadenas de la tarjeta de memoria. Ninguno contiene modelos,
TIM (salvo uno en el 5) ni ULZ. `make overlays` los extrae como PS-X EXE a
`data/exe/ovl/`.

Importados en Ghidra (base `0x800C9DC0`, con `CreateMissingFunctions`):

| Overlay | Modo | Funciones | Propias | Bytes propios | COP2 | Papel probable (por sus cadenas, `SIN VERIFICAR`) |
|---|---|---|---|---|---|---|
| 1 | 4 | 141 | 140 | 39 KB | 0 | menú principal / carga de partida (tarjeta, "LOG OUT", "Now Loading") |
| 2 | 3 | 401 | 401 | 152 KB | 66 | interfaz entre misiones ("DEBRIEFING", "CONNECTION LOST") |
| 3 | 7 | 21 | 21 | 14 KB | 0 | casi todo datos gráficos |
| 4 | 8 | 10 | 10 | 12 KB | 0 | nombres de pistas BGM (¿reproductor?) |
| 5 | 9 | 8 | 8 | 4 KB | 0 | títulos de los temas (¿modo de sonido / créditos?) |

Solo `CdMix` (libcd) aparece como librería; el resto es código de Namco.
**Total del juego: unas 1.840 funciones propias (~690 KB)**: ~1.262 en
`ACE.BIN` y ~580 en los overlays.

## 3. Cifras de funciones ✅

Medido sobre el bloque que contiene el punto de entrada (el segmento
`GTEMAC` del loader se excluye: no es código del juego).

| Módulo | Funciones | Reconocidas (PsyQ) | Propias | Bytes propios | COP2 |
|---|---|---|---|---|---|
| `ACE.BIN` (EU) | 2.011 | 749 (78 KB) | **1.262** | **473 KB** | 1.939 |
| `SCES_020.66` | 701 | 665 (95 %) | 36 | 4,5 KB | 0 en `.text` |
| `ACE.EXE` | 221 | 213 | 8 | 0,6 KB | 0 |

"Propias" = funciones sin nombre (`FUN_`) más `main`. Es una **cota
superior** en un sentido (cualquier función de librería cuya firma no casara
cuenta como propia) y **aproximada** en el otro: las funciones hoja sin
prólogo que solo se invocan por puntero no se detectan.

**Corrección importante:** el análisis automático de Ghidra dejaba fuera 158
prólogos `addiu sp,sp,-N`: funciones que solo se llaman por puntero
(callbacks del CD, el propio cargador de misión). `tools/ghidra/
CreateMissingFunctions.java` las crea (155; las otras 3 caían dentro de las
nuevas) y forma parte de `make ghidra-import`. Antes de esa corrección la
cifra era 1.853 funciones y 1.106 propias.

Desglose de las ~747 de `ACE.BIN` por librería (medido antes de la corrección; no cambia: las funciones nuevas son todas propias) (`tools/ghidra/libsplit.py`):
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
- Corrección: `"Can't open TIM"` lo usan `FUN_80026BE4`, `FUN_80026D00` y
  `FUN_80026D8C`; `FUN_800269E4` es la lectura del mando (`PadChkVsync`).
- Papeles del fotograma: `FUN_80025720` modo de vídeo, `FUN_80025BB0` limpia
  la tabla de orden, `FUN_800269E4` mando, `FUN_80028784` descodifica el
  mando por tipo (`0x41`, `0x23`…), `FUN_8002DDCC` sonido, **`FUN_80025EB0`
  despachador de modos** (`tabla[modo][paso]` en `0x800AE544`),
  `FUN_8002C0B4` CD, `FUN_80027408` sprites 2D, `FUN_80025C20` fin de
  fotograma. Nombres aplicables con `tools/ghidra/names.tsv`.

El papel de cada una de las funciones de actualización está `SIN RESOLVER`.

## 5. Sistema de packs y descompresión en `ACE.BIN` ✅

Localizado con `tools/mipsref.py` (referencias `lui`/`addiu` y tablas de
punteros) y decompilado con `tools/ghidra/decomp.sh`:

| Dirección (EU) | Función | Qué hace |
|---|---|---|
| `0x800AE7A0` | (datos) | tabla de punteros a `\ACE.BPH;1`, `\ACE.SPH;1`, `\ACE.BPB;1`, `\ACE.SPB;1` |
| `0x8002A8AC` | búsqueda | `CdSearchFile` de los 4 archivos; `"ERR : File Not Found.[%s]"` |
| `0x8002A9E4` | apertura de packs | lee BPH y SPH, comprueba `AC3E`, suma la LBA del pack a cada `sector`, y exige `SBCODE` = `0x1019` (BPH) y `SPCODE` = `0x19991019` (SPH): el ejecutable EU solo acepta packs EU |
| `0x8002AC0C` | inicialización | prepara el estado del CD y espera a la apertura |
| `0x80029E8C` | máquina de estados del CD | la llama `FUN_8002C0B4`, del bucle principal |
| `0x8002A34C` | arranque de streaming | descodifica el `tag` del SPB (modo, canal XA, longitud): ver `04-formatos.md` |
| `0x800AE118` | **descompresor ULZ** | variantes 0, 1 y 2 |
| `0x80026E50` | descomprimir + cargar TIM | ULZ y luego `FUN_80026D8C` (la de `"Can't open TIM"`) |
| `0x80035730` | **cargador de registro de misión** (tipo 17) | ver §6; `0x80035C90` es una variante |

Esto confirma desde el código el formato de índice de la etapa 1 (magic,
contador en `0x10`, entradas desde `0x14`, sector relativo al pack) y ha
resuelto el `tag` del SPB y la variante 0 de ULZ.

## 6. El cargador de misión confirma el formato de tipo 17 ✅

`FUN_80035730` recorre el registro de tipo 17 en memoria. Con el puntero al
registro desplazado 4 bytes (palabra `k+1` = offset de la sección `k`):

| Sección | Qué hace el cargador | Coincide con la etapa 1 |
|---|---|---|
| 14, 13, 12, 11, 10 | `FUN_80026E50(sección, siguiente)`: descomprime y sube las TIM a VRAM, con `DrawSync` entre cada una | ✅ las 5 secciones de texturas |
| 15 | `FUN_8002D544` sin descomprimir | ✅ el banco de sonido, la única sin ULZ |
| 0-10 | se copian los offsets a un área temporal bajo `0x801FB700`, se recolocan y se descomprimen las 11 con `FUN_800AE118` | ✅ |
| 0 | se guarda el puntero | mapa de teselas |
| 1 | cada entrada pasa por `FUN_80026EE0` (índices 1..n) | ✅ tabla de teselas |
| 4 | cada entrada pasa por `FUN_80026EE0` | ✅ tabla de objetos |
| 2, 3 | cada entrada pasa por `FUN_80026EE0(…, 6)` | ✅ la otra familia de modelos (con LOD) |
| 3 → 2 | las entradas de s3 se copian en los huecos 96+ de la tabla de s2 | nuevo: s2 y s3 son **una sola tabla** de modelos |

Que el cargador trate distinto a s1/s4 y a s2/s3 coincide con las dos
familias de cabecera de modelo (`Model` y `LodModel` en `tools/acemodel.py`)
que se dedujeron solo con los datos.

`FUN_80026EE0` no "inicializa": **recoloca**. Suma la dirección base del
modelo a `n` palabras desde `modelo + 4`, salvo las que ya tienen el bit 31
(ya son punteros). El número sale del registro `a2` en cada llamada:

| Llamada | `a2` | Campos recolocados |
|---|---|---|
| s1, s4 (`Model`) | 4 | `off_vertices, off_unk, off_prims, off_end` |
| s2, s3 (`LodModel`) | 6 | `off_vertices, off_x2, off_x4, (0), off_gt3, off_gt4` |

Confirma desde el código que esos campos son offsets relativos al modelo, y
que `off_unk` (siempre 0 en los datos) es el offset de un bloque opcional.

Datos útiles para seguir: `$gp = 0x800BA9F4` (se fija en el arranque), así que
la estructura de misión `0x800BACC0` se lee como `lw rX, 0x2CC($gp)`, en 32
sitios del código.

## 7. Intersección con el terreno: resuelve las primitivas ✅

`FUN_80097EDC` (y su gemela `FUN_80098EF4`) prueba un rayo contra las caras
de las teselas: 39 `lwc2`, 26 `NCLIP` y ningún `RTPT` (trabaja con vértices ya
transformados). Para cada primitiva:

- toma `e0` (y `e1`) como offset a un `SVECTOR` de una tabla de normales
  (la sección 5 del registro), carga la normal negada en la matriz de luz y
  hace `LL` para el producto escalar
- el bit `0x200` de la primera palabra (`h1 = 2`) parte el cuadrilátero en
  dos triángulos con una normal cada uno; el `0x100` (`h1 = 1`) es triángulo
- usa los índices de vértice de los bytes 16-19 para leer posiciones ya
  transformadas (8 bytes por vértice)
- busca la tesela con `índice & 0x1FF`, los 9 bits del mapa de teselas

Validado contra los datos con `tools/acemodel.py normals` (ver
`04-formatos.md`). Se encontró tras descartar candidatos por huella: la clave
fue buscar funciones con mucho `lwc2` en lugar de `RTPT`.

Sus llamadores (`FUN_80098A34`, `FUN_80098B80`, `FUN_80098DB0`) convierten
posiciones del mundo en celdas del mapa de teselas: `celda = (coord >> 23) +
128`, `mapa[z·256 + x] & 0x1FF`, con 0 = sin tesela (ver `04-formatos.md`).
Los usan funciones de lógica (`FUN_8006894C`, `FUN_80069D34`, `FUN_8006F670`…),
probablemente colisión de aviones y proyectiles con el suelo.

## 8. Render de terreno y objetos ✅ (confirma vértices y offsets GTE)

| Dirección (EU) | Qué hace |
|---|---|
| `0x8005FF9C` | recorre una **lista de colocación** (`SVECTOR` posición + palabra de control: modelo = bits 0-8, fin = bit 21, bit 22 y bits 16-20 `SIN RESOLVER`); por modelo hace la pasada de vértices y llama a los renders de primitivas. `0x80060F14` es otra entrada al mismo render |
| `0x80060214` | render del bloque POLY_FT4 (36 B): `NCLIP`, profundidad = máx Z >> 3, POLY_FT4 directo con `DPCS` si está lejos; si está cerca, subdivide |
| `0x80060470` | ídem para el bloque POLY_GT4 |
| `0x80060DC0` | subdivisión de caras cercanas (las convierte en GT4) |

Dos confirmaciones del formato de la etapa 1, ahora desde el código:

- **Pasada de vértices:** carga `VXY0`, `VXY1`, `VXY2` de tres palabras
  seguidas, `VZ0`/`VZ1` de la mitad baja y alta de la cuarta y `VZ2` de la
  quinta, hace `RTPT` y avanza 20 bytes: el grupo
  `x0,y0,x1,y1,x2,y2,z0,z1,z2,pad`.
- **Bytes 8-15 de la primitiva:** la subdivisión los usa como offsets en
  bytes dentro del bloque de vértices para leer `xy` y `z` de cada vértice.
  No eran redundantes: los usa el camino cercano.

`h0` no se usa en estas rutas: sigue `SIN RESOLVER`.

**De dónde sale la lista de colocación:** `FUN_8005CDA4` la construye a partir
del **mapa de teselas** (`+0x248` de la estructura de render en `*(0x2D0($gp))`;
la tabla de teselas está en `+0x24C`). Recorre una ventana visible de celdas y
por cada celda con tesela escribe su posición y una palabra de control:
bits 0-15 = la celda entera, 16-20 = columna, 21 = fin, 22 = primera de la
fila, 25-27 = reutilizar vértices de las vecinas ya transformadas. El render
hace `flags ^= flags & (flags << 16)`: los bits 9, 10 y 11 de la celda anulan
los bits 25, 26 y 27. Así se resolvieron (con contraste estadístico) los bits
altos del mapa.

## 9. Objetos de escena, aviones y recursos ✅

| Dirección (EU) | Qué hace |
|---|---|
| `0x80066B64` | render de objetos de escena: reparte (`0x80065CC4`) y llama a los renders |
| `0x80065CC4` | por objeto: distancia, y **LOD = id·4 + (0, 1 o 2 según distancia)** sobre la tabla de modelos con LOD |
| `0x80061408` | **render jerárquico de modelos LOD**: lista de piezas de 0x38 B, cada una con matriz propia, callback, bloque de vértices elegido y `LodModel`; usa sus `+0x14`/`+0x18` (`off_gt3`/`off_gt4`) |
| `0x8006159C`, `0x800616FC` | renders de los bloques GT3 y GT4 de los modelos LOD |
| `0x8002953C` | **identificador de recurso**: bit 11 = pack (0 BPB, 1 SPB), bits 0-10 = entrada |
| `0x800354B8` | secuenciador de carga: reconoce el registro de misión (tabla `0x800AF748`, índice `+0x390`) y el del avión del jugador (tabla `0x800AF79C`, índice `+0x392`) |
| `0x80035E78`, `0x80035EF8` | carga y reproducción de **demos** (tabla `0x800AF7C8`: registro de tipo 1, misión, avión) |

Tablas de datos en `ACE.BIN` con números de entrada del BPB:
`0x800AF748` (41 misiones: registros 6-46), `0x800AF79C` (22 aviones del
jugador: registros 47-68) y `0x800AF7C8` (16 demos: registros 111-126).

## 10. Lo que queda (etapa 3, siguientes pasos)

1. Recorrer el árbol de llamadas de las funciones de `main` y ponerles nombre
   (entrada, lógica, cámara, render, sonido…).
2. `h0` de las primitivas; qué vecino exacto corresponde a cada bit alto del
   mapa.
3. **El modelo del avión del jugador**: por análisis estático no aparece (ver
   `04-formatos.md`). Propuesta: instalar un emulador con depurador
   (PCSX-Redux o DuckStation), volcar la RAM en una misión con vista exterior
   y localizar el `LodModel` del jugador y su origen.
   Buscar por huella (`RTPT` + pasos de 36/48/32 bytes) dio falsos
   candidatos (`0x8008FE38` y `0x8008A874` son efectos e iluminación); mejor
   seguir el flujo desde los 32 accesos a `0x2CC($gp)`.
3. Repetir la medición en la versión US.
4. Valorar `rea` (`morluto/rea`) como capa sobre Ghidra.
