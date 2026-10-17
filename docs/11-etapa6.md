# 11 — Etapa 6: el juego, nativo (recompilación estática)

Abierta el 2026-10-16 por decisión del dueño del proyecto. Rama `etapa-6`.

## Objetivo

Ace Combat 3 completo, de la pantalla inicial al final, **nativo** en la
Retroid Pocket Flip 2 y **1:1** con el original: misma lógica, mismos tiempos
y misma imagen que en una PS1 (la referencia práctica es DuckStation).

Sobre esa base, después: resolución nativa, panorámico, 60 fps por
interpolación y controles propios.

## Método: recompilar, no reescribir

El menú, los briefings y las 52 misiones son el mismo motor: residente
`ACE.BIN` más 5 overlays que se cargan en `0x800C9DC0` (ver
`docs/09-codigo.md`). Reescribir cada pantalla a mano sería reescribir el
motor leyendo ensamblador; eso es la decompilación, y cuesta años.

En su lugar, el código MIPS del juego se **traduce automáticamente a C**
(recompilación estática, la técnica de N64Recomp) y se compila nativo,
enlazado con la capa `psx/` de la etapa 5 en lugar de las librerías de Sony.
La etapa 5 se diseñó para esto:
- la RAM simulada usa direcciones PS1;
- los callbacks son direcciones;
- `tools/mipsemu.py` sirve de referencia instrucción a instrucción;
- el código del juego ya corre sobre la capa en las pruebas (packs,
  streaming, mando, efectos, música).

Así sale **todo el juego a la vez**. El trabajo pasa a ser corregir
diferencias, no construir pantallas.

El orden "pantalla inicial → menú → briefing → misión 1 → … → final" es el
**orden de pruebas**: cada pantalla que se comporte igual que en DuckStation
queda validada.

## Condición legal

El C recompilado **deriva del código de Namco**: no entra en git ni en un APK
distribuido (`docs/06-legal.md`).
- El recompilador es nuestro y genera el C a partir del disco del usuario,
  en `data/` (fuera de git).
- Primer modelo: **el usuario compila su propio APK en el PC** con una
  herramienta del proyecto. Generarlo en el dispositivo queda para más
  adelante.
- El APK de la rebanada (`android/`) sigue sin llevar nada del juego.

## Ramas y seguimiento

- Cada paso en su rama: `etapa-6-paso-N`, creada desde `etapa-6` y fusionada
  en ella al terminar el paso. `etapa-6` se integra en `develop`.
- `make progress` (`tools/progress.py`) recalcula `docs/progreso.json` (solo
  números) y la infografía `docs/img/progreso.svg` del README:
  - funciones propias recompiladas y validadas, sobre 1.821: residente 1.237,
    overlays 144 / 401 / 21 / 10 / 8;
  - funciones de Sony cubiertas por la capa;
  - pasos de la etapa;
  - recorrido 1:1.

## Plan

| Paso | Qué | Estado |
|---|---|---|
| 1 | Completar la capa: lo que el juego llama y `psx/` no tiene (tabla abajo) | ✅ 152/152 funciones de Sony que llama el juego (residente y overlays) |
| 2 | Salida de vídeo: el área visible de la VRAM a pantalla con GLES (PC con EGL y Android), imagen 1:1 | ✅ conversión exacta en 15/24 bits, GLES idéntico píxel a píxel, películas reales; compila para arm64 |
| 3 | Recompilador MIPS → C (`tools/recomp/`): residente y 5 overlays, validado función a función contra `mipsemu` | ✅ 1.672/1.821 funciones validadas con estados aleatorios, 0 distintas; el resto, con estados reales en el paso 4 |
| 4 | Arranque nativo en el PC: ejecutable de arranque → `ACE.BIN` → bucle principal; meta: **pantalla de título** | ⏳ siguiente |
| 5 | Recorrido 1:1 en el orden del juego: título, menú, briefing, misión 1… hasta los finales; comparación con DuckStation | ⏳ |
| 6 | Android: el usuario genera su APK desde su disco; controles de la Retroid; rendimiento | ⏳ |
| 7 | Mejoras sobre la base 1:1: render GL a resolución nativa con la precisión de nuestra GTE (como PGXP), panorámico, **60 fps por interpolación** | ⏳ |
| 8 | Opcional: pasar a C legible las partes que se quieran modificar | — |

**Criterio de salida:** el juego completo, jugable de principio a fin en la
Retroid, nativo y 1:1 (pasos 1-6). Las mejoras del paso 7 son un hito
aparte.

### Paso 1: lo que le falta a la capa

Inventario (2026-10-16): API que llama el código del juego, según `make
psyq-api`, frente a las cabeceras de `psx/include/`.

| Librería | Hechas | Faltan | Notas |
|---|---|---|---|
| libgte | 26/26 | — | |
| libcd | 24/27 | `CD_flush`, `CD_init`, `CD_initvol` | internas de libcd; comprobar si las llama código propio |
| libgpu | 15/18 | `OpenTIM`, `ReadTIM`, `GetGraphDebug` | sencillas, con oráculo |
| libetc | 7/9 | `startIntrDMA`, `startIntrVSync` | internas de la inicialización |
| libspu | 15/22 | `_spu_*`, `_SpuSetAnyVoice` | internas: los llamadores son de la propia libspu (comprobar) |
| libpad | 11/22 | `_pad*`, `setRC2wait`… | internas del protocolo serie: no hacen falta con el modo directo |
| libapi (BIOS) | 0/21 | eventos (`DeliverEvent`, `WaitEvent`, `CloseEvent`…), contadores (`SetRCnt`, `GetRCnt`…), interrupciones (`HookEntryInt`, `SysEnqIntRP`…), `InitHeap`, `FlushCache`, `write` | modelo de alto nivel sobre `psx_vblank` |
| libc | 2/12 | `printf`, `puts`, `memcpy`, `memset`, `memmove`, `memchr`, `strcmp`, `strncmp`, `bzero`, `setjmp` | `rand`/`srand` ya idénticas; el resto, sobre la libc del anfitrión |
| libcard | 0/9 | tarjeta de memoria | guardar partida: archivo de tarjeta compatible con los emuladores |
| libpress | 0/3 | `DecDCTvlc*` | **vídeos** (intro y cinemáticas): descompresión y modelo del MDEC |
| libsn | 0/3 | `__divdi3`, `__udivdi3`, `__main` | aritmética de 64 bits del compilador |

Además: **el MDEC** (decodificador de vídeo, por DMA 0/1) si el juego lo
programa directamente, y el **ejecutable de arranque** (`SCES_002.47` /
`SLES`), que descomprime y carga `ACE.BIN`.

### Riesgos conocidos

- **Saltos indirectos** (tablas de saltos, punteros a función) que Ghidra no
  resolvió. Se cubren con un despachador por dirección y con el registro de
  saltos no resueltos durante las pruebas.
- **Bucles de espera** contra el hardware o contra variables que cambia una
  interrupción: en la recompilación hay que dar paso al tiempo simulado.
- **Casos de la GPU o la SPU** que solo aparezcan al jugar.
- **60 fps:** la lógica corre a 25 ticks/s (PAL) o 30 (NTSC). Acelerarla
  rompería el 1:1. Se interpolará el dibujo (cámara y objetos entre ticks),
  y para eso hay que localizar en el código las matrices de cada fotograma.

## Resultados

**Paso 1 (2026-10-16): capa completa, 152/152.** Rama `etapa-6-paso-1`.

*El inventario inicial se quedaba corto.* Contaba solo nombres con firma PsyQ
y se le escapaban dos cosas:
- **funciones sin firma** que el juego llama, identificadas por su código:
  `SetGeomOffset`, `SetGeomScreen`, `ReadGeomScreen`, `NormalClip`,
  `TransMatrix`, las de libpress que controlan el MDEC (`DecDCTReset/in/out/
  outSync/outCallback`), `strlen` y las secciones críticas;
- **las llamadas de los overlays**, que en su volcado aparecen como
  `FUN_<dirección del residente>`: eventos de la BIOS, archivos de la tarjeta,
  `StoreImage2`, `ClearImage2`, `MoveImage`, `SetDrawMode`, `GetODE`,
  `CompMatrix`, `RotMatrix_gte`, `sprintf`, `SpuReadDecodedData`…

`tools/progress.py count` resuelve ahora las direcciones de los overlays y
cuenta cualquier función de la zona de librerías a la que llame código propio.

| Bloque | Implementación | Validación |
|---|---|---|
| libgte (8 más) | `libgte.c` | oráculo: idénticas en registros GTE y memoria |
| libgpu (`OpenTIM`/`ReadTIM`, `SetDrawMode`, `MoveImage`, `ClearImage2`, `StoreImage2`, `GetODE`) | `libgpu.c` | oráculo (`ReadTIM`, `SetDrawMode`); prueba de GP0 0x80 |
| contadores raíz, `InitHeap`, secciones críticas, eventos | `libapi.c` | `psx/test/test_etc.c`; los eventos, con el código del juego (tarjeta) |
| `printf` y `sprintf` | `libapi.c` | oráculo: **dos formateadores distintos en PsyQ**, ambos idénticos en 5.075 formatos |
| `__divdi3`, `__udivdi3` | `libapi.c` | oráculo |
| libpress `DecDCTvlc2` | `libpress.c` | oráculo: 120 fotogramas reales idénticos |
| MDEC | `libpress.c` | libav `mdec`: ≥ 49,5 dB |
| tarjeta de memoria | `libcard.c` | código de guardado del juego (overlay 1) |

Hallazgos:
- **`printf` y `sprintf` de PsyQ no comparten formateador.**
  - En `printf`: la precisión cuenta el signo y el prefijo; el `0` rellena
    también `%c` y `%s`; `h` extiende el signo desde 16 bits en todo salvo
    `%p`; `%%` no se rellena.
  - En `sprintf`: casi C estándar, pero `%p` sale con 8 cifras en mayúsculas,
    `%#p` lleva el prefijo `0p` y `%#s` es una **cadena Pascal** (longitud en
    el primer byte).
  - El juego solo usa `%d`/`%s` (con ancho y relleno), que ya coincidían.
- **Vídeo.** Las películas (solo las entradas 0 y 1 del SPB, de 304×224 y
  320×176) usan sectores con una cabecera propia de Namco y el flujo de bits
  de **versión 3**. El fin de fotograma es un código de 10 unos en la
  posición del DC; después, `DecDCTvlc2` rellena con `0xFE00`.
- **Tablas del MDEC por fórmula,** comprobadas contra el ACE.BIN:
  cuantización = matriz intra de MPEG-1 en zigzag con q[0] = 2; IDCT =
  ⌊32768·C(u)·cos((2x+1)uπ/16)⌋.
- **Referencia de color.** Para comparar con libav hay que usar su salida YUV:
  su conversión a RGB es de rango limitado (×1,164 − 18), y el MDEC trabaja
  en rango completo.
- **Tarjeta.** El juego guarda `BESCESP02066AC3PAL` (1 bloque, 0x11FC bytes)
  con escrituras asíncronas y espera el evento SwCARD EvSpIOE.

Validación de la tarjeta (`tools/psxcheck_card.py`), con el código del
overlay 1 en el intérprete: formatear, listar (15 libres), crear, guardar,
cargar y listar otra vez (1 archivo, 14 libres). La carga pasa la
comprobación del propio juego y los datos vuelven idénticos. La imagen `.mcd`
es válida: firma, sumas XOR y directorio.

Sin hacer (anotado en las cabeceras):
- `DecDCTvlcSize2` con límite (el juego usa 0, sin límite) y la versión 2 del
  flujo sin probar (el disco no la usa);
- aritmética exacta de la conversión YUV→RGB del hardware;
- `ClearImage2` en modo entrelazado;
- si la BIOS redondea las lecturas y escrituras de tarjeta a 128 bytes.

**Paso 2 (2026-10-16): salida de vídeo.** Rama `etapa-6-paso-2`.

- `psx/src/display.c` (`psx_display.h`) convierte el área visible de la VRAM
  (la del último `PutDispEnv`, que ahora guarda también el modo de 24 bits y
  el entrelazado) a RGBA:
  - 15 bits: expansión (c << 3) | (c >> 2);
  - 24 bits: R, G, B empaquetados en medias palabras, como las películas;
  - con `SetDispMask(0)`, negro.
- `psx/gl/psx_present.c` (aparte de `libpsx`, para que la capa no dependa de
  OpenGL) sube la imagen como textura de GLES 3 y la dibuja en tres modos:
  4:3 con barras (el televisor), escala entera o 1:1. Filtrado al vecino más
  cercano o lineal.

Validación (`make psx-display`):
1. `psx/test/test_display.c`, 15 comprobaciones:
   - la conversión, calculada desde los datos de origen, sin leer la VRAM; con
     el área en distintas posiciones, dando la vuelta por los bordes y con x
     impar en 24 bits;
   - la pantalla apagada;
   - con OpenGL (EGL pbuffer, Mesa): `glReadPixels` idéntico píxel a píxel a
     1:1 y a escala entera, y barras laterales en 4:3.
2. `tools/psxcheck_display.py`: fotogramas de las dos películas montados en la
   VRAM como lo hace el reproductor del juego (MDEC en 24 bits, `DecDCTout`
   por columnas de 16 píxeles y `LoadImage` de cada una). La salida de 24 bits
   es **idéntica al RGB del MDEC**.
3. `make psx-scene` saca ahora su imagen por este camino.
4. `make psx-android`: la capa y la presentación compilan para arm64 con el
   NDK, sin avisos.

Mutaciones detectadas: orden de bytes en 24 bits y expansión de 5 bits.

Pendiente:
- la prueba en la Retroid, que necesita un APK con la capa (llegará con el
  arranque, paso 4, o antes si se pide);
- el recorte y la posición en el televisor (`DISPENV.screen`) y el
  entrelazado.

**Paso 3 (2026-10-17): recompilador MIPS → C.** Rama `etapa-6-paso-3`.

`tools/recomp/recomp.py` traduce cada función propia del juego a una función
de C sobre `psx/recomp/recomp_rt.h` (código nuestro: CPU, memoria con
direcciones PS1, GTE de la capa, despachador por dirección, enganche de
librería). El C generado deriva del código de Namco: va a `data/recomp/` y no
entra en git (`make recomp`).

Reglas, las mismas que `tools/mipsemu.py`:
- **Huecos de retardo:** la condición y el registro de `jr`/`jalr` se leen
  antes del hueco.
- **Saltos internos y externos:** los internos son `goto`; los externos, salto
  de cola.
- **`jr $ra`:** retorno, comprobando que `$ra` es la dirección esperada (la
  fija quien llama y la heredan los saltos de cola).
- **Tablas de `switch`:** se leen enteras con el número de casos del `sltiu`.
- **Saltos a un hueco de retardo:** soportados (el descompresor ULZ los usa).
- **Overlays:** cada uno con su prefijo y su tabla; `rc_set_overlay` indica
  el cargado.

Entradas descubiertas, además de las funciones de Ghidra:
- destinos de `jal`;
- direcciones cargadas con `lui`/`addiu`;
- etiquetas `LAB_`;
- **finales de función compartidos** (saltos a mitad de otra función);
- **punteros a código en los datos** (tablas de manejadores);
- destinos que llegan desde los overlays.

| Módulo | Entradas | Funciones de Ghidra validadas |
|---|---|---|
| residente | 2.065 | 1.130 / 1.237 |
| overlay 1 | 320 | 142 / 144 |
| overlay 2 | 843 | 362 / 401 |
| overlay 3 | 49 | 20 / 21 |
| overlay 4 | 109 | 10 / 10 |
| overlay 5 | 10 | 8 / 8 |
| **total** | | **1.672 / 1.821 (91,8 %)** |

**Validación** (`tools/recomp/check.py`, `make recomp-check`):
- Cada entrada se ejecuta con 24 estados iniciales en el intérprete (código
  original) y traducida. Los estados son aleatorios pero plausibles: punteros
  a una zona de datos que a su vez contiene punteros válidos, y estructuras
  del juego.
- Se comparan los 32 registros, HI/LO, los 2 MB de RAM, el scratchpad, la GTE
  y la secuencia de llamadas a librerías. Las librerías van, en los dos lados,
  a un stub determinista: se prueba el código del juego, no las librerías.
- Resultado: **0 funciones distintas en los seis módulos**.
- Las 149 restantes son "sin caso": con estados aleatorios siempre fallan o
  agotan el tiempo en los dos lados. Necesitan estados reales (paso 4).

Fallos encontrados en el camino, todos corregidos:
- funciones a las que se llegaba a mitad (finales compartidos);
- punteros a código en datos;
- tablas de saltos con `add` y tablas cortadas por los límites de la
  función;
- el saltar a un hueco de retardo;
- un rango de "código" que en realidad eran datos;
- en el arnés, punteros de ctypes truncados a 32 bits.

**Sensibilidad del comparador (medida, no supuesta).** Sobre 60 funciones
validadas, una mutación de una constante en cada una:
- con 24 casos se detectan 32 (53 %);
- con 120 casos, 39 (65 %).

"Validada" significa que no se encontró diferencia, no que esté demostrada.
Un fallo de una regla de traducción afectaría a muchas funciones y saldría a
la luz; los que se escapan son locales. La comprobación fuerte será la del
paso 4: fotogramas reales del juego en el intérprete y en el código traducido
desde el mismo estado, con la RAM comparada fotograma a fotograma.

**Comparador con estados reales y primer arranque nativo (2026-10-17).**
Rama `etapa-6-comparador`.

*El juego original arranca en el intérprete sobre la capa*
(`tools/recomp/boot.py`, `hle.py`):
- Las 155 funciones de Sony a las que salta el juego se enlazan con `psx/`
  con firmas sacadas automáticamente de las cabeceras.
- Recorre la pantalla de carga, el menú de idioma, el título, la película de
  presentación, los menús y el briefing.

Hizo falta:
- **Eventos de la tarjeta:** `_card_clear` avisa con eventos de hardware
  (`HwCARD`); sin eso, el juego se quedaba en "Now Loading.".
- **Reloj por instrucciones:** unas 450.000 instrucciones por fotograma PAL
  (`INSNS_PER_FRAME`, aproximado). El reproductor de vídeo espera al CD en un
  bucle sin llamar a librerías (`GMV : timeout VLC`); en la consola llegan
  interrupciones durante ese bucle.

*Validación con estados reales* (`tools/recomp/record.py`,
`make recomp-record`):
- Durante ese arranque, al entrar en una función (solo si se llegó llamando:
  la instrucción anterior es el hueco de retardo de un `jal`/`jalr`) se toma
  una instantánea y se graban sus llamadas a librería con sus efectos en RAM,
  scratchpad y GTE.
- Al volver se ejecuta la función recompilada desde la instantánea, con esas
  llamadas reproducidas, y se compara el estado.
- Resultado en 3.000 fotogramas: **239 funciones en 641 casos reales, todos
  idénticos**; 3 descartados porque saltó una interrupción a mitad de la
  función.

*El juego recompilado, nativo en el PC* (`psx/recomp/native_main.c`,
`tools/recomp/gen_hle.py`, `make native`):
- Enlace nativo con la capa generado desde las cabeceras.
- Callbacks como interrupciones, con pila propia.
- El presupuesto de saltos hace de reloj (`BUDGET_PER_FRAME`, aproximado).
- El overlay cargado se detecta por firma.

Resultado: **el juego llega a la primera misión en vuelo** (HUD, radar,
terreno, "ENGAGE"), pasando por menú, dificultad, briefing y selección de
avión. Va a unos 120-330 fotogramas por segundo de CPU: varias veces el
tiempo real.

Fallos destapados al ejecutarlo:
- una tabla de `switch` cuya base está en un registro `s` cargado al
  principio de la función, que ya se detecta desde el inicio de la función de
  Ghidra;
- un límite de seguridad para listas de dibujo corruptas.

**Paso 4: APK personal 1.0-alfa (2026-10-17).** Rama `etapa-6-paso-4`.

- `android_game/` es una app aparte de la rebanada (`com.vzepec.ac3game`) con
  el juego recompilado. Hilos, sonido y mando: ver `android_game/README.md`.
- `android_game/build.sh` la compila con el NDK. Enlaza `data/recomp/*.c` y
  ACE.BIN incrustado; deja el APK en `data/android/` porque **no se
  distribuye**.
- Probado en el PC con el mando analógico (`AC3_PAD_ID=73 make native`): llega
  al vuelo de la primera misión.

Rendimiento en el PC: los fotogramas de vuelo cuestan unos 2-4 ms de CPU
(250-600 fotogramas/s); el coste está en las cargas y las películas. -O2 en
el código generado no mejora sobre -O1.

**SIN RESOLVER:** el rendimiento real en la Retroid, y si el presupuesto sin
calibrar provoca tirones.

**Paso 5: imágenes intermedias (60 fps) y GPU por software 2× más rápida
(2026-10-17).** Rama `etapa-6-paso-5`.

*Punto de partida (medido):*
- La misión dibuja una imagen cada 2 VBlank (25 por segundo en PAL).
- La lógica avanza un paso por imagen: con `PSX_VSYNC_MAX=1` el juego entero
  va al doble.
- Para ver más imágenes sin acelerarlo hay que interpolar.

*Interpolación por vértices* (`psx/src/interp.c`, `psx/include/psx_interp.h`).
Es genérica, no depende del juego y no toca su código:
- La GTE anota cada vértice proyectado (RTPS/RTPT): coordenadas del modelo →
  posición de pantalla.
- En `DrawOTag` se reconocen, por su valor exacto, las coordenadas de la lista
  de dibujo que salieron de la GTE en ese paso (~90 % de las de la misión).
- **Emparejamiento con el paso anterior:**
  - Más de la mitad de los vértices repiten coordenadas de modelo, porque el
    terreno va por casillas con coordenadas locales.
  - Con los vértices de clave única se ajusta un movimiento global afín de la
    imagen.
  - Cada vértice repetido se empareja con el candidato más cercano a la
    predicción de ese modelo.
  - Los vértices de la GTE sin pareja siguen la media de su primitiva o el
    modelo global.
  - Lo que no viene de la GTE (HUD, textos) no se mueve.
- **Dibujo:** la lista se dibuja con las posiciones mezcladas sobre una copia
  del estado de la GPU, y se entregan al anfitrión las imágenes t = ¼, ½, ¾ y
  la real.
- Cobertura en la misión: 55-90 % de las coordenadas emparejadas.
- **Defecto corregido:** contando apariciones, las parejas se desplazaban y
  salían astillas en el horizonte. Con el modelo afín desaparecen (comprobado
  con capturas).

*Validación con verdad conocida* (`AC3_INTERP_TEST=1` en `native_main`,
juego a un paso por VBlank):
- Se interpola entre los pasos *k* y *k+2* y se compara con el *k+1* real.
- Resultado en 390 casos: PSNR medio de 28,7 dB, frente a 27,5 y 27,8 dB de
  los vecinos reales (lo que se vería sin interpolar).
- Los peores casos son cambios del HUD en 2D (el texto "ENGAGE"), que no se
  pueden interpolar.

*Rasterizador de la GPU* (`psx/src/gpu.c`). Antes iban 58 ns por píxel: cada
píxel del rectángulo envolvente recalculaba las aristas con hasta cinco
divisiones de 64 bits. Ahora:
- tramo exacto por fila;
- atributos incrementales (cociente + resto, el mismo valor que la división);
- tramos especializados.

Resultados:
- Imagen de misión de 4,9 a 3,3 ms (`psx/test/bench_gpu.c` sobre un fotograma
  real guardado con `PSX_GPU_DUMP`).
- Arranque completo de 4.200 fotogramas de 33 a 17 s de CPU.
- **Resultado idéntico byte a byte:** 84 capturas del arranque iguales al
  rasterizador anterior, el mismo resumen de VRAM y `test_gpu` sin fallos.

*APK 1.1-alfa:*
- El hilo del juego entrega 3 intermedias + la real por paso, cada una con su
  hora de salida.
- La pantalla muestra en cada refresco la última que ya tocaba.
- Un paso de retraso (40 ms).
- El número de intermedias se adapta (de 1 a 3) según le sobre tiempo al hilo
  del juego.
- L3+R3 activa o desactiva la interpolación.

**SIN RESOLVER:**
- el rendimiento en la Retroid;
- objetos que el juego coloca con la CPU tras la GTE (sprites de explosiones);
  quedan en la posición del paso real.
