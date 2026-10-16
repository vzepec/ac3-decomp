# 10 — Etapa 5: capa de compatibilidad PsyQ

> Estado: **en curso** (abierta el 2026-10-16 por petición del dueño del
> proyecto). Rama `etapa-5`. Código en `psx/`.

## Qué es y qué no es

Una biblioteca en C que reimplementa las funciones de las librerías de Sony
(PsyQ 4.x) que usa Ace Combat 3, con la misma interfaz, para que algún día el
código del juego (recompilado o reescrito, etapa 6) pueda ejecutarse en
Android. Es el equivalente de Psy-X (REDRIVER2).

**No es el juego**: aquí no se recompila nada de Namco. Cada función se
valida por separado contra el comportamiento conocido.

## Superficie que hay que cubrir

Medida con `python3 tools/psyqapi.py <firmas> data/ghidra/all_*.c`: las
funciones de librería a las que llama código propio del juego (no las
internas de las librerías).

| Librería | Funciones | Ejemplos (nº de llamadores) | Destino en Android |
|---|---|---|---|
| LIBCD | 27 | `CdReadyCallback` (14), `CdControlF` (11), `CdRead`, `CdSearchFile` | lectura de la imagen de disco (ya hecha en `android/src/ace.c`) |
| LIBGTE | 26 | `SetTransMatrix` (12), `rsin` (12), `RotTransPers` (10), `RotMatrixYXZ`, `SquareRoot0`, `ratan2` | C en punto fijo exacto |
| LIBSPU | 22 | `SpuSetCommonAttr`, `SpuSetKey`, `SpuSetVoiceAttr`, reverb | mezclador propio sobre el audio de Android |
| LIBPAD | 22 | `PadGetState`, `PadSetAct` (la mayoría son internas del SIO) | mando físico de Android |
| LIBAPI | 21 | eventos, contadores, `InitHeap` | equivalentes del sistema |
| LIBGPU | 18 | `DrawSync` (13), `ClearImage`, `LoadImage`, `ClearOTagR`, `DrawOTag`, `StoreImage` | VRAM simulada + rasterizado en GLES (como la rebanada) |
| LIBC | 12 | `rand` (84), `memmove`, `strcmp` | libc del sistema (salvo `rand`, que debe dar la misma secuencia) |
| LIBETC | 9 | `VSync` (23), `DMACallback`, `VSyncCallback` | bucle y temporizador |
| LIBCARD | 9 | tarjeta de memoria | archivo de guardado |
| otras | 9 | `DecDCTvlc*` (vídeo), división de 64 bits | según haga falta |

**Total: 175 funciones de API.** Además, el código del juego usa la **GTE
directamente** (instrucciones COP2 en línea, sin pasar por libgte): 2.351
usos en 167 funciones. Las órdenes que aparecen: `RTPS`, `RTPT`, `NCLIP`,
`AVSZ3`, `AVSZ4`, `MVMVA` (varias formas), `DPCS`, `DPCT`, `INTPL`, `NCS`,
`NCT`, `NCCS`, `GPF`, `GPL`, `SQR`, más la carga y lectura de casi todos los
registros. Un **emulador exacto de la GTE** es, por tanto, el núcleo de la
etapa.

## Cómo se valida (sin PS1)

1. **Contra el código original de Sony.** Las funciones de libgte, libc
   (`rand`), etc. están dentro de `ACE.BIN` como código MIPS. Un intérprete
   MIPS R3000 mínimo (`tools/mipsemu.py`) las ejecuta y sus resultados son el
   oráculo: la reimplementación en C debe dar **exactamente** lo mismo en
   miles de entradas. Para las funciones que usan la GTE, el intérprete usa la
   GTE de `psx/` y la prueba comprueba la coherencia de ambos lados.
2. **Contra la especificación** (psx-spx, de nocash) para las órdenes de la
   GTE: saturaciones, banderas, división UNR.
3. **Contra los datos del juego**: la rebanada de `android/` ya reproduce el
   render de terreno y modelos; se podrá reescribir sobre `psx/` y comparar.

## Plan

| Paso | Entregable | Validación |
|---|---|---|
| 1 | Inventario de la API (`tools/psyqapi.py`) | ✅ este documento |
| 2 | Intérprete MIPS de referencia (`tools/mipsemu.py`) | ✅ ejecuta las funciones de Sony de `ACE.BIN` |
| 3 | `psx/` con libgte sin GTE (`rsin`, `rcos`, `ratan2`, `SquareRoot0`, `rand`) | ✅ **84.000 casos, 0 diferencias** (`make psx-check`) |
| 4 | Emulador de la GTE (`psx/src/gte.c`) | ✅ especificación + 27 pruebas (`make psx-check`); sin verificar contra hardware |
| 5 | libgte con GTE (`RotMatrixYXZ`, `MulMatrix`, `RotTransPers`, …) | ✅ 21 funciones, 0 diferencias en memoria, retorno y los 64 registros (`make psx-check`) |
| 6 | libgpu: tabla de ordenación, primitivas, VRAM, `DrawOTag` | ✅ funciones de memoria (6, oráculo) + GPU por software (75 pruebas, 72 texturas reales idénticas) + terreno real dibujado (`make psx-scene`) |
| 7 | libcd, libpad, libetc | ✅ validadas ejecutando el código del juego: apertura de packs, 127/127 registros por streaming idénticos, mando (`tools/psxcheck_io.py`) |
| 8 | libspu | ✅ SPU por software + libspu (8 funciones idénticas al original); driver de sonido del juego tocando el banco común (`tools/psxcheck_snd.py`). Pendiente: música XA |

**Esfuerzo:** meses. **Riesgo:** medio (la GTE es delicada; el resto es
trabajo conocido gracias a Psy-X y a la documentación pública).

## Resultados

**Pasos 2-3 (2026-10-16).** `tools/mipsemu.py` es un intérprete R3000 de
enteros (con hueco de retardo, 2 MB de RAM, scratchpad, y de la GTE solo los
movimientos de registros y LZCS/LZCR). `tools/psyqcheck.py` ejecuta con él las
funciones originales y las compara con `psx/`:

| Función | Casos | Diferencias | Notas |
|---|---|---|---|
| `rsin`, `rcos` | 21.000 cada una | 0 | tabla generada: `round(4096·sen(2πi/4096))`, exacta en las 4.097 entradas |
| `SquareRoot0` | 17.093 | 0 | tabla generada: `floor(√v · 512)`, v = 0x40..0xFF, exacta; reproduce la aproximación del original (√1.000.000 = 999) |
| `ratan2` | 16.289 | 0 | la tabla **no** tiene fórmula exacta (el mejor ajuste acierta 996/1025): se lee del `ACE.BIN` del usuario, localizada por estructura (única en EU y US) |
| `rand` | 8.500 | 0 | congruencial `s·0x41C64E6D + 0x3039`, devuelve `(s >> 16) & 0x7FFF` |

**Validación del método** (regla 3): con errores introducidos a propósito
(`rcos` desplazado una unidad, raíz redondeada en vez de truncada, constante
de `rand` cambiada) el comparador detecta miles de diferencias.

Decisión de diseño: las tablas de Sony se **generan** cuando hay fórmula
exacta y, si no, se **leen del disco del usuario** en tiempo de ejecución;
nunca se copian al código ni al APK.

**Paso 4 (2026-10-16): emulador de la GTE.** `psx/src/gte.c` implementa, según
psx-spx, los 64 registros (con sus lecturas especiales: extensión de signo de
VZ/IR, FIFO de SXY/SZ/RGB, IRGB/ORGB, LZCS/LZCR, `H` leído con signo) y las 22
órdenes definidas (todas las que usa el juego y el resto), con las
saturaciones y banderas de 44 bits (MAC1-3), 32 bits (MAC0), IR, color, SZ/OTZ,
SX/SY e IR0, la división UNR con su tabla generada por fórmula, y las rarezas
documentadas: la bandera de IR3 en `RTPS` mira `MAC3 >> 12` sea cual sea `sf`;
el bit 31 de FLAG no incluye los bits 22-19; `MVMVA` con el vector FC solo usa
la primera columna para las banderas; la matriz "reservada" de `MVMVA`.

Pruebas (`psx/test/test_gte.c`, 27 comprobaciones, todas bien): proyección
con valores calculados a mano (incluidos OFX/OFY, traslación, saturación de
pantalla y z detrás de la cámara), `RTPT` = 3 × `RTPS` en 2.000 casos
aleatorios, `MVMVA` frente a coma flotante (error 0), `NCLIP`, `AVSZ3/4`,
`INTPL`, `SQR`, banderas y registros especiales. La división UNR coincide con
la exacta en el 94,5 % de 327.756 casos y se aparta como mucho 3: es la
imprecisión que el algoritmo documentado reproduce a propósito.

Al escribir las pruebas aparecieron un error real (SX/SY deben salir del valor
de 64 bits, no del MAC0 truncado) y dos expectativas mías equivocadas (el bit
31 de FLAG no incluye la saturación de IR3; el color empujado es `MAC >> 4`).

**Límite honesto:** sin hardware ni un emulador de referencia a mano, la GTE
queda validada contra la especificación, no contra una PS1. Un siguiente nivel
sería comparar con la GTE de un emulador maduro (DuckStation, Mednafen) sobre
entradas aleatorias.

**Paso 5 (2026-10-16): libgte con GTE.** El intérprete delega las
instrucciones COP2 (`MTC2/MFC2/CTC2/CFC2`, `LWC2/SWC2` y las órdenes) en la
GTE de `psx/` compilada como biblioteca compartida (`psx/build/libpsx.so`,
ctypes). `tools/psyqcheck_gte.py` ejecuta así el código original de Sony y
compara con `psx/src/libgte.c` sobre otra instancia de la GTE: mismas
estructuras de entrada aleatorias (bytes de relleno incluidos) y mismo estado
aleatorio de los 64 registros; se comparan la memoria de salida, el valor
devuelto y **el estado final completo de la GTE**.

Resultado: **21 funciones, 300 casos cada una, 0 diferencias**:
`SetRotMatrix`, `SetTransMatrix`, `SetColorMatrix`, `SetBackColor`,
`ReadGeomOffset`, `RotTransPers`, `RotTransPers3`, `RotTrans`, `MulMatrix0`,
`MulMatrix`, `MulMatrix2`, `SetMulMatrix`, `ApplyMatrixSV`, `ApplyMatrixLV`,
`ApplyRotMatrixLV`, `TransposeMatrix`, `ScaleMatrix`, `OuterProduct0`,
`RotMatrix`, `RotMatrixYXZ`, `VectorNormalSS`. Con las del paso 3, **25 de las
26 funciones de libgte que usa el juego quedan idénticas al original**; la
26.ª, `InitGeom`, activa el COP2 a través de la BIOS (fuera del intérprete) y
su parte de GTE (7 constantes) se verificó leyéndola.

Detalles que exigió la exactitud: las cargas de matrices y vectores son
palabras de 32 bits que arrastran los bytes de relleno de las estructuras
(cambian el estado interno de la GTE); `MulMatrix*` escribe IR3 como palabra
de 32 bits sobre `m[2][2]` y su relleno; `ScaleMatrix` escribe `m[2][2]` con
el producto completo; `RotMatrix` niega antes de desplazar. Las tablas de
seno/coseno empaquetada (`0x800B4F84`) y de inverso de raíz (`0x800B4DF8`) se
generan por fórmula exacta (`round(4096·sen/cos)`, `floor(32768/√v)`).

Dominio: `VectorNormalSS` con vectores cuya suma de cuadrados desborda 32
bits lee, en el original, fuera de su tabla (comportamiento no definido): el
comparador cuenta esos casos aparte (2 de 300 con entradas aleatorias de
rango completo; ninguno con vectores realistas).

Validación del método: con cinco errores introducidos a propósito
(transposición incompleta, otro redondeo en `RotMatrix`, IR3 de 16 bits,
`R33` cargado con signo —que solo cambia un registro interno— y un factor 4
en vez de 8 en `ApplyMatrixLV`) el comparador detecta todos.

**Paso 6, primera parte (2026-10-16): modelo de memoria y libgpu sin GPU.**
Decisión de arquitectura: los datos del juego viven en una **RAM de PS1
simulada** (2 MB + scratchpad, `psx/include/psx_mem.h`) y los punteros que el
juego guarda en sus estructuras son direcciones PS1 de 32 bits. Es lo que
encaja con una recompilación estática del código MIPS en la etapa 6 (la
memoria conserva su forma) y permite comparar con el oráculo sobre la misma
imagen. La alternativa de Psy-X (punteros nativos de 64 bits con arreglos por
estructura) obligaría a tocar cada estructura del juego.

Funciones de libgpu que solo escriben en memoria, idénticas al original
(`tools/psyqcheck_gpu.py`, 500 casos cada una, estructuras en la misma
dirección PS1 en los dos lados): `SetDefDrawEnv` (con `dfe` según el modo de
vídeo: `h < 289` en PAL, `< 257` en NTSC), `SetDefDispEnv`, `SetDrawTPage`,
`SetDrawArea` (E3/E4 limitados al tamaño de VRAM que fija `ResetGraph`, en
`0x800B97E8`), `CatPrim`, `GetTPage`. Para leer el ensamblador cuando Ghidra
corta funciones se añadió `tools/mipsdis.py`, que anota las direcciones
efectivas (un error al sumar a mano un desplazamiento negativo hizo fallar
`SetDrawArea` en el comparador hasta corregirlo).

**Paso 6, segunda parte: GPU por software.** `psx/src/gpu.c`: VRAM de
1024×512 y procesador GP0 según psx-spx (polígonos planos/Gouraud/con textura
de 4, 8 y 15 bits, CLUT, ventana de textura, los cuatro modos de
semitransparencia, tramado, bit de máscara, rectángulos y sprites, líneas y
polilíneas, relleno, transferencias de VRAM y E1-E6) y el recorrido de listas
de dibujo en la RAM simulada. Encima, en `libgpu.c`: `ResetGraph`,
`DrawSync`, `SetDispMask`, `LoadImage`, `StoreImage`, `ClearImage`,
`ClearOTagR`, `ClearOTag`, `DrawOTag`, `PutDrawEnv`, `PutDispEnv`.

Pruebas (`psx/test/test_gpu.c`, 75, todas bien): **72 texturas reales del
disco** subidas a la VRAM y dibujadas como sprites coinciden píxel a píxel con
la decodificación independiente del TIM; un cuadrado de 10×10 pinta
exactamente 100 píxeles (regla de relleno: sin bordes derecho e inferior);
dos triángulos con arista común en mezcla aditiva no dejan huecos ni
solapes; Gouraud, los cuatro modos de mezcla, transferencias por GP0 y una
lista de dibujo encadenada en la RAM. La primera versión tenía la regla de
relleno al revés (con y hacia abajo se invierte la orientación); la prueba lo
detectó.

**Integración con datos reales** (`make psx-scene`, `psx/test/test_scene.c`):
el terreno de la misión 6 se dibuja como lo haría el juego, enteramente con
la capa: texturas con `LoadImage`, matriz de vista con `RotMatrixYXZ` y
`TransposeMatrix`, traslación con `ApplyMatrixLV`, vértices con `RTPT`,
paquetes `POLY_FT4`/`POLY_GT4` construidos desde las plantillas de los
modelos en la RAM simulada, descarte con `NCLIP`, `ClearOTagR` y `DrawOTag`.
Sale la ciudad con el puente y la costa. No es el código del juego (es una
reconstrucción de su bucle de terreno); demuestra que la capa funciona de
punta a punta.

Límites: la interpolación de color y UV dentro de los polígonos sigue la idea
del hardware pero no está verificada contra una PS1; `ClearOTagR` termina la
lista en `0xFFFFFF` en vez de en el paquete interno de la librería.

**Paso 7 (2026-10-16): libcd, libetc, libpad, validadas con el código del juego.**

`psx/src/libcd.c`, `libetc.c` y `libpad.c`.

*Modelo de tiempo e interrupciones* (`psx_libetc.h`). No hay hilos ni
interrupciones reales: el tiempo avanza por fotogramas (`psx_vblank`). Cada
fotograma incrementa el contador de VSync, actualiza los mandos, hace avanzar
el CD en 8 pasos y llama al callback de VSync. El CD entrega en cada paso los
comandos pendientes y los sectores a la velocidad de lectura (150 por segundo
a doble velocidad: 3 por fotograma en PAL). Las funciones que esperan en la
PS1 (`VSync`, `CdSync(0)`, `CdControlB`) hacen avanzar ese tiempo ellas
mismas, así que los callbacks llegan "durante la espera", como harían las
interrupciones.

*Callbacks como direcciones.* El juego guarda sus punteros a función como
palabras de 32 bits dentro de sus estructuras (p. ej. `0x80029BB0` en el
estado del lector de CD). La capa los trata como direcciones PS1 y los
invoca a través de una función de despacho del anfitrión (`psx_set_call`).
El código recompilado de la etapa 6 traducirá la dirección a su función; las
pruebas la traducen al intérprete.

*Direcciones fijas.* Los búferes de resultado que reciben los callbacks y el
`CdlLOC` de `CdLastPos` están en la RAM simulada, en las mismas direcciones
que usa la libcd original del ACE.BIN europeo (0x800C5C98, 0x800C5CA0,
0x800B40F4), porque el juego recibe esos punteros.

*libcd.*
- Imagen `.bin` (2352 B por sector) o `.iso` (2048).
- Comandos Setloc, Setmode, Setfilter, ReadN, ReadS, Pause, Stop, Standby,
  GetlocL/P y GetTN, con terminación diferida que se notifica a
  `CdSyncCallback`.
- Un callback de `CdReadyCallback` por sector; `CdGetSector` lee 2048 o 2340
  bytes según el modo.
- ReadS con filtro XA: los sectores de audio no llegan al programa; van a un
  sumidero para libspu.
- `CdSearchFile` sobre el ISO9660.

*libpad.* Modo directo: cada fotograma escribe en los búferes de
`PadInitDirect` el estado, el id (0x41/0x73), los botones activos a nivel
bajo y los ejes. El mando se presenta ya identificado como estándar
(`PadGetState` = 2), sin negociación de DualShock ni vibración (SIN HACER).

**Validación (`tools/psxcheck_io.py`, en `make psx-check`).**
- `tools/mipsemu.py` admite ahora RAM externa (la misma `psx_ram` de la capa,
  por ctypes), desvíos de funciones de librería a Python/C (hooks) y llamadas
  reentrantes para los callbacks.
- El intérprete ejecuta el código original del juego, y sus llamadas a
  libcd/libetc/libpad van a la implementación en C.

Resultados:
1. `CdIntToPos`/`CdPosToInt`: 40.000 casos idénticos a las originales (oráculo).
2. `CdSearchFile`: LBA y tamaño de 5 archivos iguales a los de `tools/psxiso.py`.
3. `packs_init` del juego (0x8002AC0C):
   - busca los 4 packs, lee los índices con `CdRead` y los valida (marca
     AC3E, códigos 0x1019 y 0x19991019) sin mensajes de error;
   - la tabla BPH reubicada coincide en sus 127 entradas con `tools/acepack.py`.
4. El lector por streaming del juego (`spb_stream_start`, 0x8002A34C):
   - secuencia: Setloc, Setmode 0xA0, ReadN, un callback por sector que
     comprueba el número de sector de la cabecera y copia con `CdGetSector`,
     y al final Pause;
   - **127/127 registros del BPB idénticos** byte a byte a los extraídos,
     sin escribir más allá del final;
   - unos 3 sectores por fotograma.
5. El mando, con la inicialización (0x800260AC) y la lectura por fotograma
   (0x800269E4) del juego:
   - botones, pulsaciones nuevas, cambio a analógico con los cuatro ejes y
     desconexión.

Método validado con 6 mutaciones; todas se detectan:
- desplazamiento de los datos del sector;
- saltarse sectores;
- BCD mal calculado;
- velocidad de lectura a la mitad (no se detectaba hasta añadir la
  comprobación de tiempo);
- ejes intercambiados;
- byte de estado del mando.

`psx/test/test_etc.c` comprueba la semántica de `VSync` (-1, 0, 1, n) y el
callback de VSync.

Simplificaciones y límites:
- `CdRead` es síncrono;
- no hay tiempos de búsqueda;
- `PadChkVsync` sigue la descripción de la referencia de PsyQ, sin verificar
  contra el original;
- el streaming de XA (modos 1 y 2 de `spb_stream_start`, con ReadS) solo se
  ejercitará con libspu (paso 8).

**Paso 8 (2026-10-16): SPU por software y libspu.**

*SPU* (`psx/src/spu.c`, según psx-spx):
- registros, RAM de sonido de 512 KB, 24 voces ADPCM;
- ADSR con ENVX, KON/KOFF y ENDX;
- volúmenes fijos y entrada de audio de CD;
- transferencias por el puerto de datos y por DMA.

*Sin hacer:*
- interpolación gaussiana (se usa lineal, con una muestra de retardo);
- volumen en modo barrido, ruido, modulación de tono;
- reverberación (los registros se guardan, pero no se procesa);
- interrupción de la SPU.

*libspu* (`psx/src/libspu.c`). Comportamiento leído del ensamblador de la
librería original: Ghidra parte varias funciones en trozos `S_*_OBJ_*` y la
descompilación no sirve. Tres firmas del ACE.BIN están mal atribuidas:
- `SsUtReverbOff` es `SpuInit`;
- la "`SpuRead`" que usa el juego es `SpuWrite`;
- 0x8009E238 es `SpuQuit` y 0x8009EEC8, `SpuSetReverbVoice`.

Las tablas de `_spu_note2pitch` salen exactas por fórmula: ⌊0x8000·2^(i/12)⌋
y ⌊0x8000·2^(j/1536)⌋, las 140 entradas.

**Validación.**
1. **Oráculo** (`tools/psyqcheck_spu.py`). `mipsemu` admite ahora registros
   de E/S: los accesos de la libreria original a 0x1F801C00 van a una
   instancia de la SPU de psx/. Funciones comparadas, 2000 casos cada una,
   **todas idénticas** (registros, envolventes, retorno):
   - `SpuSetVoiceAttr`, `SpuSetCommonAttr`, `SpuSetKey`;
   - `SpuGetAllKeysStatus`, `_spu_note2pitch`;
   - `SpuSetReverb`, `SpuSetReverbDepth`, `SpuSetReverbVoice`.

   Diez mutaciones; las nueve no equivalentes se detectan.
2. **SPU frente a `tools/acesnd.py`.** Las 39 muestras del banco común,
   tocadas a tono 0x1000, salen idénticas a la decodificación independiente,
   con parada al final y ENDX (falta solo la última muestra, que el
   retardo deja fuera al apagarse la voz). Cuatro mutaciones detectadas
   (filtro, fin de muestra, tono, fases ADSR).
3. **Driver de sonido del juego en el intérprete** (inicialización, carga
   de banco, petición de sonido y fotograma del driver, con libspu desviada a
   psx/):
   - la RAM de sonido queda idéntica al banco;
   - 40 sonidos del banco común arrancan con la dirección, el ADSR y el tono
     esperados, suenan y terminan (o quedan en bucle, como el motor).

**Hallazgo.** La fórmula de frecuencia de `acesnd` era incorrecta para los
sonidos con afinación fina (u0 ≠ 0): la fracción se **suma** en 1/128 de
semitono, no se resta en 1/256. Corregidos `tools/acesnd.py`,
`docs/04-formatos.md` y el mezclador del APK (`android/src/sound.c`; en la
0.7 esos sonidos suenan desafinados hasta 1,3 semitonos).

Simplificaciones de libspu:
- transferencias inmediatas;
- `SpuSetReverbModeParam` y `SpuClearReverbWorkArea` guardan el modo sin
  reproducir las escrituras originales. Pendiente junto con la reverberación
  de la SPU.

**Pendiente del paso 8:** la música XA. El sumidero de libcd ya entrega los
sectores; falta el decodificador XA-ADPCM con remuestreo a 44,1 kHz y una
referencia independiente para validarlo (no hay ffmpeg en el sistema).
