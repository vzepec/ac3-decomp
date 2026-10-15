# Bitácora de hallazgos

Registro append-only. Entrada nueva **arriba**. Cada hallazgo debe decir
**cómo reproducirlo**.

---

## 2026-10-15 — Primitivas de los modelos y base del mapa de teselas

Bloque 1: `u32 n` + POLY_FT4 de 36 B (código `0x2C`); bloque 2: `u32 n` +
POLY_GT4 de 48 B (`0x3C`, 4 colores). Índices `u8` en orden de tira, UV, CLUT
y tpage en el orden del paquete GPU. Validado en 14.095/14.095 modelos (bloques
exactos, códigos, índices, offsets GTE). Lo descifró un sub-agente; se
re-verificó integrándolo en `tools/acemodel.py`.

El índice del mapa de teselas es base 1: continuidad de bordes 99,4 % frente a
74,6 % con base 0.

Reproducir: `make models`; `python3 tools/acemodel.py tilemap data/bpb`;
`python3 tools/acemodel.py obj data/sec/0006_tage0028567_t17_s04.bin 0 data/obj/o.obj`

## 2026-10-15 — ACE.DAT es relleno (corrige una interpretación anterior)

De sus 20.672 sectores solo 40 (0,19 %) tienen algún byte distinto de cero,
con dos patrones de 108 bytes. Está al final del disco. No contiene assets.
La hipótesis de `docs/01-disco.md` ("la EU sacó contenido del SPB a ACE.DAT")
era **falsa** y se ha corregido.

Reproducir: `python3 tools/acedat.py <imagen EU>`

## 2026-10-15 — EU y US comparten terreno, modelos y texturas

Comparando sección a sección (descomprimida) los 127 registros: en los 41 de
tipo 17 las secciones 0-5 y 7-14 son idénticas byte a byte. Solo cambian la 6
(textos de misión: la EU lleva 5 idiomas) y la 15. En el tipo 4 cambia solo la
sección 2, en valores tipo `0x043C90`/`0x0496D0` dentro del rango de
`ACE.BIN`: hipótesis de punteros a código (`SIN RESOLVER`).

Reproducir: extraer el BPB americano a `data/bpb_us/` y comparar con
`tools/acerec.py` (ver `docs/04-formatos.md`, tipo 17).

## 2026-10-15 — Vértices de modelos resueltos (criterio de salida de la etapa 1)

Las secciones 1 (teselas de terreno) y 4 (objetos) del tipo 17 son tablas de
modelos. Vértices en grupos de 3: `x0,y0,x1,y1,x2,y2,z0,z1,z2,pad` (`s16`), el
layout que necesita el GTE para `RTPT`. Validación: tamaño del bloque
coherente en 14.095/14.095 modelos y bbox de cabecera == bbox real en 421/421
objetos.

La sección 0 es un mapa de teselas 256×256: `max(v & 0x1FF)` == número de
teselas de la sección 1 en los 41 registros, y pintado muestra mar, costa y
ciudad, igual que la textura de radar.

Reproducir: `make models`; `python3 tools/acemodel.py verts data/sec/0006_tage0028567_t17_s04.bin 0`

## 2026-10-15 — Primera textura en PNG (criterio de salida de la etapa 1)

Las texturas **sí** son TIM estándar, pero dentro de secciones ULZ. El
"resultado negativo" anterior (cero TIM) se debió a buscar sobre datos
comprimidos: lección para la próxima vez, medir la entropía antes de buscar
magics. 9.288 TIM solo en la sección 10 de los 41 registros de tipo 17.
Verificado a ojo: fuente con acentos, piel de avión, mapa de radar.

Reproducir: `make textures REC=0006` → `data/png/`

## 2026-10-15 — Compresión ULZ de Namco resuelta

Las secciones de los registros empiezan por `Ulz\x1a`. Cabecera de 16 bytes
y tres flujos (flags `u32` MSB primero, literales `u8`, referencias `u16` con
`nbits` de distancia). Validación: holgura de flujos 4 / 0-3 / 0-2 bytes en
659/659 secciones.

Reproducir: `make ulz-check`

## 2026-10-15 — El tag del BPB es el tamaño del registro

`(tag & 0xFFFFFF) * 4` == tamaño del registro en 127/127 (EU) y 127/127 (US).
En el SPB no (0/72, 0/88).

Reproducir: `make tags`

## 2026-10-15 — Cabecera común de registro resuelta, y las texturas NO son TIM

Los registros de `ACE.BPB` comparten layout:
`u32 type; u32 header_size; u32 section_offset[(header_size-8)/4];` con los
offsets monótonos crecientes y dentro del registro. Verificado en **122 de
127** registros; las 5 restantes (entradas 1-5) son tablas de sistema con otra
convención.

El tipo determina el número de secciones de forma consistente: tipo 1 → 0
secciones (18 registros), tipo 4 → 3 (22), tipo 5 → 4 (40), tipo 13 → 12 (1),
tipo 17 → 16 (41).

**Resultado negativo:** se buscó la cabecera TIM (`10 00 00 00` + flags de
modo plausibles) en registros de tipo 17 y 4 → **cero coincidencias**. Las
texturas no se guardan como TIM suelto. Probablemente van como bloque listo
para `LoadImage` a VRAM, sin cabecera. No repetir esta búsqueda.

En las entradas 1 y 2 el segundo `u32` es un puntero a RAM ya resuelto
(`0x800CBE04`, `0x800CC004`): revela el mapa de memoria en ejecución.

Reproducir: `make records`

## 2026-10-15 — Herramientas validadas contra el disco americano

Mismo formato de índice (`AC3E`, `kind` 1 y 3, 8 bytes por entrada) y misma
cabecera de registro. El SPB americano tiene más categorías (`0xF5` ×8,
`0xF8` ×10 frente a ×2 en el europeo), coherente con que la europea movió
contenido a `ACE.DAT`.

Confirma que el formato es **del motor**, no de una versión. El trabajo hecho
sobre la europea se transfiere.

Reproducir: `python3 tools/acepack.py "Rom/.../Ace Combat 3 ... (USA).bin" index`

## 2026-10-15 — Formato de los índices de assets resuelto

Cabecera de 20 bytes (`AC3E`, kind, fecha BCD, desconocido, count) y entradas
de 8 bytes (`tag`, `sector`). Validado porque `20 + 8*count` da exactamente
el tamaño del archivo en las **cuatro** combinaciones EU/US × SPH/BPH.

Los offsets están **en sectores de 2048 bytes** dentro del pack.

**Trampa:** el índice **no está ordenado por sector** (en `ACE.SPH` europeo la
entrada 71 apunta al sector 19400, antes que la 70). Hay que ordenar por
sector para deducir los tamaños. Con ese criterio la suma cubre el **100 %**
de los dos packs, lo que confirma la interpretación.

Reproducir: `make index` y `make inventory`

## 2026-10-15 — Contenido identificado en ACE.BPB

- Entrada 1: `CRD0`, `bu00:BESCESP02066AC3PAL` → tablas de memory card.
  `ESP`/`PAL` confirman build española/europea
- Entrada 4: `BGOK01`, `BGOK02`, `BGTT01` → nombres de pistas de BGM
- Entrada 5: `Last Flight`, `Clear`, `Morceaux` → tabla de textos multiidioma
- Entradas 6-126: 121 registros de tipo `0x11`, ~600-765 KB cada uno → el
  volumen real del pack; geometría y texturas de misión
- Varios registros contienen **punteros a RAM ya resueltos** (`0x800CBE04`,
  `0x800CBE14`, `0x800CBE48`) → revelan el mapa de memoria en ejecución

Reproducir: `make inventory`

## 2026-10-15 — ACE.BIN está comprimido, con cabecera propietaria

Magic `PS-Y4a\xe3w`, no `PS-X EXE`. Campos descifrados por coherencia cruzada:
`0x0C` = tamaño exacto del archivo (413.772), `0x10` = tamaño descomprimido
(700.416), `0x24` = carga (`0x80010000`), `0x08` = fin (`0x800BB000`, que es
carga + descomprimido). Entrada en `0x8002521C`.

Comprimido: entropía 7,42 bits/byte y solo 0,01 prólogos `addiu sp,sp,-N` por
KiB, frente a 0,47 en `SCES_020.66` y 0,42 en `ACE.EXE`.

Reproducir: `make info`

## 2026-10-15 — El juego usa PsyQ de stock

IDs RCS presentes en los binarios: `sys.c,v 1.140 1998/01/12 … noda Exp yos`,
`bios.c,v 1.86`, `intr.c,v 1.75`. Más cadenas `ResetGraph:jtb=%08x,env=%08x`,
`Can't open TIM [%08x]`, `SPU:T/O [%s]`.

Consecuencia: las funciones de `libapi`/`libgpu`/`libgte`/`libspu`/`libcd` se
**reimplementan**, no se decompilan.

Reproducir: `make info`

## 2026-10-15 — No hay símbolos de depuración en el disco

El disco tiene **9 archivos** y ninguno es `.SYM` ni `.MAP`. Es la diferencia
principal frente a REDRIVER2, que partió con los símbolos del original.

Reproducir: `make files`

## 2026-10-15 — Error corregido: heurístico de prólogos MIPS

La primera versión de `mips_prologues()` buscaba los bytes `27 bd ff`, que es
el orden **equivocado**: MIPS en PS1 es little endian, así que la instrucción
`addiu sp, sp, -N` (`0x27BDFFxx`) aparece en el archivo como `xx FF BD 27`.
El bug daba cero en todos los binarios y llevaba a concluir falsamente que
`SCES_020.66` estaba empaquetado. Corregido a buscar `FF BD 27`.

Lección: todo heurístico debe validarse contra un caso **positivo conocido**
antes de usarse para concluir nada.
