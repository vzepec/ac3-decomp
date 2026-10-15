# Bitácora de hallazgos

Registro append-only. Entrada nueva **arriba**. Cada hallazgo debe decir
**cómo reproducirlo**.

---

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
