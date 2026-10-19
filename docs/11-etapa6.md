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
| 4 | Arranque nativo en el PC: ejecutable de arranque → `ACE.BIN` → bucle principal; meta: **pantalla de título** | ✅ llega a la primera misión en vuelo, en las dos versiones |
| 5 | Recorrido 1:1 en el orden del juego: título, menú, briefing, misión 1… hasta los finales; comparación con DuckStation | 🔄 **funciona entero en las dos versiones:** las 41 misiones, las secuencias de final con los créditos y guardar/cargar, sin fallos (`docs/pruebas/`). Falta la comparación con DuckStation, que el dueño deja para el final |
| 6 | Android: el usuario genera su APK desde su disco; controles de la Retroid; rendimiento | ✅ superado por la etapa 7: APK libre publicado (v1.7-alfa), `VEL` 100 % en la Retroid |
| 7 | Mejoras sobre la base 1:1: render GL a resolución nativa con la precisión de nuestra GTE (como PGXP), panorámico, **60 fps por interpolación** | 🔄 60 fps por interpolación ✅; **pantalla ancha** ✅ publicada en la release `v1.8-alfa`; alta resolución, antitemblores y sprites de la CPU ⏳ (un primer intento con GLES, en la rama `etapa-6-paso-15`, dio fallos en la Retroid) |
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

### Paso 6: intermedias en hilos aparte y contador (APK 1.2-alfa)

**Problema observado en la Retroid (1.1-alfa):** con las intermedias activas,
la misión baja a ~5-10 imágenes por segundo a ojo. En la 1.1, las
intermedias se dibujaban dentro de `DrawOTag`, en el hilo del juego. Si ese
trabajo no cabe en los 40 ms del paso, se frena el juego entero, no solo la
imagen. La causa exacta en la Retroid está **SIN RESOLVER**: no hay
mediciones del aparato. Hipótesis: el hilo del juego cae en un núcleo lento
(Cortex-A55) o la GPU por software es allí bastante más lenta que en el PC.

**Cambios** (`psx/src/interp.c`, `psx_interp_threads`):
- En `DrawOTag`, el hilo del juego empareja vértices como antes. Después
  guarda una instantánea del paso: RAM (2 MB), scratchpad, estado de la GPU
  antes del dibujo real y la lista de coordenadas que se mueven.
- Dibuja la imagen real y sigue con la lógica.
- Cada hilo de dibujo (3 en el APK) copia la instantánea, aplica las
  posiciones mezcladas de su fase *k* en su RAM privada y dibuja con
  `psx_gpu_draw_list_mem` sobre su propio `PsxGpu`. Luego entrega la imagen
  con la hora del paso.
- **Pasos saltados:** si al llegar el paso siguiente algún hilo no ha
  terminado, ese paso va sin intermedias y se cuenta como saltado. El juego
  nunca espera a los hilos.
- **Seguridad entre hilos:** el estado del rasterizador por triángulo
  (`g_tri_cx`, `g_tri_by`) pasa a ser `_Thread_local`.
- **Presentación:** la conversión de VRAM a RGBA sale a
  `psx_display_convert`. El sumidero recibe la hora del paso (`t_step`), y la
  hora de salida de cada imagen es `t_step + k/den · paso`.
- El modo en el hilo del juego sigue disponible (`psx_interp_threads(0)`):
  es el que usa la prueba de verdad conocida.

**APK:**
- El hilo del juego y los de dibujo se fijan a los núcleos rápidos: los de
  frecuencia máxima ≥ 75 % de la mayor, leída de `/sys`.
- La pantalla copia la imagen bajo el cerrojo y sube la textura fuera de él.
- **Número de intermedias:**
  - baja si más del 10 % de los pasos saltan o si el hilo del juego duerme
    menos de 3 ms por VBlank;
  - sube si no salta ninguno y cada hilo tarda menos de 25 ms.
- **Contador** arriba a la izquierda:
  - `FPS`: imágenes distintas mostradas por segundo, según un resumen de la
    imagen;
  - `VEL`: VBlank del juego por segundo frente a 50, es decir, si el juego va
    a su velocidad;
  - `INT`: intermedias por paso;
  - `CPU`: uso del hilo del juego.
- Cada segundo va a logcat una línea con esas cifras y, además, el núcleo
  del juego, los pasos saltados y los ms por paso del juego y de cada hilo.

**Validación** (PC, `make native` con `AC3_INTERP=3 AC3_INTERP_FROM=3900` y
el guion de la misión, con y sin `AC3_INTERP_THREADS=3`):
- Las intermedias y las reales de los pasos 225-228 son **idénticas byte a
  byte** en los dos modos (13 de 13).
- El paso 226 saltó sus intermedias con hilos. Sin ritmo de 50 Hz, el PC
  dibuja más rápido que los hilos, y ese es el comportamiento previsto.
- `make psx-check`: sin fallos y el mismo resumen de VRAM del banco de
  pruebas.

**SIN RESOLVER:** las cifras reales de la Retroid. Las dirá el contador.

### Paso 7: titileo en pasos sin intermedias (APK 1.3-alfa)

**Informe de la Retroid con la 1.2-alfa:**
- En vuelo normal: 60 FPS, VEL 100 %, INT 3, CPU 15 %.
- En pruebas de estrés (giros rápidos, muchos objetos, mapa en pantalla):
  artefactos, titileo y pérdida de estabilidad.

**Defecto encontrado en el código:**
- Si un paso no reúne bastantes parejas (menos de ¼ de las coordenadas
  reconocidas), `psx_interp_draw` devolvía 0. También pasaba en el primer
  paso tras cada `psx_interp_config`, es decir, cada vez que el número de
  intermedias se adapta.
- En ese caso el juego dibujaba el paso por su cuenta, pero la imagen **no
  llegaba a la cola** de la pantalla.
- La pantalla seguía mostrando la imagen del paso anterior y, a los 0,15 s
  sin imágenes nuevas, saltaba a la otra vía, la del área visible.
- Resultado: imagen congelada y alternancia entre imágenes viejas y nuevas.
- **Medido:** con el guion de la misión, 5 de 480 pasos caen en este caso
  (`make native` con `AC3_INTERP=3`, línea "interpolacion: ... sin bastantes
  parejas"). Es previsible que en los giros bruscos y al abrir el mapa sean
  muchos más.

**Arreglo:**
- Esos pasos también entregan su imagen real al sumidero, con la misma hora
  de salida que una real normal.
- El número de intermedias solo sube tras 5 ventanas seguidas con holgura,
  porque cada cambio altera la cadencia.
- Logcat añade "sin parejas" por segundo.

**Validación:**
- Con hilos y sin ellos: 16 de 16 capturas idénticas.
- Prueba de verdad conocida sin cambios: 28,34 dB en 393 casos, igual que el
  binario del paso 5 (`5fac7bc`) compilado de nuevo.
  - **Corrección:** las cifras que se anotaron en el paso 5 (28,7 dB, 390
    casos) venían de un binario anterior. Con el guion de la misión y el
    binario de `5fac7bc`, la prueba da 28,34 dB frente a 27,27 y 27,57 dB de
    los vecinos.

**SIN RESOLVER:**
- **Artefactos por emparejamiento en giros muy rápidos:** saltos de más de 48
  px por paso no se interpolan, y los vértices repetidos pueden emparejarse
  mal. No se pudo reproducir en el PC sin el mando real.
- **Sprites colocados por la CPU:** las llamas de los motores y las
  explosiones saltan al ritmo de 25.

### Paso 8: terreno lejano en giros rápidos (APK 1.4-alfa)

**Informe de la Retroid (1.3-alfa):** con la interpolación activa, en giros
rápidos de avión o de cámara, aparecen parpadeos y "terreno negro" en el
terreno lejano.

**Reproducción en el PC:**
- La prueba de verdad conocida admite ahora `AC3_INTERP_TEST=S`: se
  interpola entre *k* y *k+S+1* y se compara con el paso real del medio.
  - S=3 y S=5 simulan movimientos de 2 y 3 veces la velocidad de la misión.
  - La prueba informa además de los píxeles casi negros de la intermedia que
    no lo son en las reales ("agujeros"), con la misma medida en el paso
    real del medio como ruido.
- `tools/interp_holes.py` hace la misma medida sobre las capturas de
  `AC3_INTERP_FROM`/`AC3_INTERP_LEN`.
  - Su prueba con un agujero sintético cuenta 80 de 80 píxeles.
  - Sobre las capturas del guion da 6-9 píxeles por imagen, lo mismo que
    tres reales seguidas (9,3). **Con los giros del guion no hay grietas:**
    esa medida es solo textura que se mueve.
- **Sonda** (`PSX_INTERP_PROBE=vb,x,y`): en el paso indicado, la intermedia se
  dibuja paquete a paquete y se anotan los paquetes que escriben el píxel.
  `PSX_INTERP_PRIMDUMP` vuelca las coordenadas de la lista con su estado y su
  desplazamiento de dibujo.

**Hallazgos:**
1. **El fondo (cielo, bruma) no sale de la GTE.**
   - Es un cuadrilátero que cubre la pantalla, con el horizonte calculado por
     la CPU según la orientación de la cámara.
   - El terreno 3D va con el desplazamiento de dibujo 160,384, centrado; el
     HUD, con 0,256.
   - En las intermedias el fondo no se movía, mientras el terreno sí.
2. **Puntos oscuros en la silueta del terreno**, encontrados con la sonda en
   un caso con S=5.
   - Son caras de las laderas: de espaldas en el paso anterior (el juego no
     las dibujaba) y de cara en este.
   - Al mover cada vértice en línea recta quedan de cara antes de tiempo y
     muestran su textura oscura sobre la bruma.
3. **Parejas equivocadas en vértices repetidos:**
   - Los vértices del terreno con coordenadas de modelo repetidas se
     emparejaban con la predicción del movimiento global, que en giros
     fuertes falla.
   - Resultado: vértices casi juntos en pantalla recibían desplazamientos
     muy distintos.
4. **Grietas posibles en vértices compartidos:** un vértice sin pareja se
   movía con la media de cada primitiva por separado, y en primitivas
   distintas recibía valores distintos.

**Cambios** (`psx/src/interp.c`):
- **Parejas en dos fases.** Primero las claves únicas. Después las repetidas,
  con la predicción de sus vecinos: la media de los vértices únicos de sus
  mismos polígonos (tolerancia de 8 px); el movimiento global queda como
  último recurso.
- **Desplazamientos grandes:** los saltos de hasta 128 px se admiten si
  cuadran con la predicción. El límite sin predicción sigue en 48 px.
- **Vértices sin pareja:** un único desplazamiento por punto de pantalla,
  la media de lo que proponen todas sus primitivas.
- **Fondo:** las primitivas grandes (≥ 128 px) sin GTE en el espacio 3D se
  reconocen en el paso anterior por comando, textura y orden de aparición, y
  se interpolan con sus propios vértices.
  - Si no se reconocen, se mueven con el movimiento global, deslizando sobre
    su borde los vértices que están en el borde de la pantalla.
- **Polígonos que se dan la vuelta:** si la superficie con signo cambia entre
  los dos pasos, el polígono no se dibuja en las intermedias antes de la
  fracción estimada del giro, |a₀|/(|a₀|+|aₖ|).
  - Tampoco se dibuja si en la intermedia queda de espaldas o crece más del
    doble de lo que mide en los dos pasos reales.
- **Base de las intermedias:** cada intermedia se dibuja sobre la imagen real
  del paso. El juego no borra la pantalla (`isbg` = 0 y sin rellenos en la
  lista), así que si queda un hueco se ve terreno real y no memoria antigua.
- La aplicación de cada fase (`apply_phase`, `PhaseList`) es una sola
  función para los dos modos, con hilos y sin ellos.

**Validación (PSNR de la intermedia frente al paso real del medio; entre
paréntesis, el mejor vecino real):**

| | 1.3 | 1.4 |
|---|---|---|
| S=1 (393 casos) | 28,34 dB (27,57) | 28,47 dB |
| S=3 (191-192 casos) | 26,96 dB (25,71) | 26,89 dB |
| S=5 (126-127 casos) | 26,14 dB (24,78) | 26,18 dB |

- Interpolar el fondo con sus propios vértices mejora 9 de 127 casos con S=5,
  hasta +1,4 dB, y no empeora ninguno.
- En el caso de la sonda, los píxeles oscuros sobre la bruma bajan de 145 a
  100.
- Con hilos y sin ellos: 44 de 44 capturas idénticas.
- `make psx-check` sin fallos.
- Emparejar cuesta 0,5 ms por paso (antes 0,35).
- **Peor caso con S=5:** un choque con la pantalla temblando. El propio juego
  deja una franja negra al desplazar la imagen, y la intermedia la interpola.
  No es un defecto.

**SIN RESOLVER:**
- No se pudo reproducir en el PC el giro rápido de la Retroid: el guion solo
  tiene la cruceta, sin la palanca de la cámara.
- Con movimientos muy grandes (S=5), interpolar en línea recta un alabeo de
  ~60° coloca mal la silueta del terreno. Es el límite de la interpolación
  2D: quedan caras de ladera oscuras donde la imagen real del medio ya
  muestra cielo.

### Paso 9: radar, mapa y temblor del terreno (APK 1.5-alfa)

**Informe de la Retroid (1.4-alfa):**
- 7 capturas y un vídeo de la primera misión, guardados en la carpeta
  `imagenes bugs/`, que no se versiona.
- Defectos visibles:
  - al radar le falta una cuña;
  - bloques del mapa grande (botón X de la Retroid = cuadrado) desplazados;
  - franjas y puntos oscuros en el horizonte y en la silueta de la costa;
  - temblor del terreno en giros del avión o de la cámara (palanca
    derecha).

**Herramientas:**
- **El guion de `native_main` acepta palancas** (`lizq`, `lder`, `larr`,
  `laba`, `rizq`, `rder`, `rarr`, `raba`), con `AC3_PAD_ID=73`. Así se
  reproduce el giro de cámara de la Retroid.
- **Nueva medida, `tools/interp_dark.py`:** píxeles de la intermedia más de
  60 niveles más oscuros que en las dos reales.
- **Depuración, `PSX_INTERP_DBG2`:** una línea por paso con las coordenadas
  reconocidas, emparejadas, arrastradas y quietas, y si hay movimiento
  global.
- **Vídeo de la Retroid:** se analizó con un buscador automático de
  parpadeos, es decir, imágenes que se apartan de su anterior y de su
  siguiente más de lo que esas dos se apartan entre sí. Se usó el ffmpeg de
  `imageio-ffmpeg`, instalado en un entorno aislado dentro de la carpeta
  temporal.

**Hallazgos:**
1. **El HUD, el radar y el mapa también usan la GTE**, porque giran, pero se
   dibujan con otro desplazamiento de dibujo (0,256) o en su propia área.
   - Volcado de un paso: 94 coordenadas de la GTE en el espacio del HUD y 9
     en la textura del radar (320,256).
   - Se interpolaban como si fueran escena 3D y sus vértices falseaban el
     movimiento global.
2. **El movimiento global fallaba en uno de cada dos pasos** (`PSX_INTERP_DBG2`
   en el guion de la misión: modelo 1/0 alternando).
   - En esos pasos, ~160 coordenadas del terreno (12 %) quedaban quietas
     mientras el resto se movía: un parpadeo del terreno a 12,5 Hz.
   - **Causa:** solo se ajustaba con claves únicas. Contando solo el espacio
     3D, únicas en los dos pasos quedan 15, menos del mínimo de 24, porque el
     terreno repite coordenadas.
3. **Giros de cámara con la palanca derecha:**
   - El terreno se desplaza más de 128 px por paso y lo único emparejado es
     el avión propio.
   - Se pasaba el control de calidad, pero el terreno quedaba mitad movido y
     mitad quieto: grietas por las que se ve el cielo (caso 3992 del guion de
     estrés).

**Cambios** (`psx/src/interp.c`):
- **Espacio 3D:** es el espacio de dibujo (desplazamiento E5 + origen del área
  E3) con más coordenadas de la GTE. Solo él se interpola. El HUD, el radar y
  el mapa quedan como en la imagen real.
- **Unicidad y referencias:** se cuentan solo con vértices del espacio 3D, y
  el paso anterior guarda solo esos.
- **Movimiento global en dos ajustes** (`fit_affine`):
  - antes de emparejar, con claves únicas (mínimo 12);
  - después, con todas las parejas 3D, descartando las que se apartan más de
    6 px (mínimo 24).
  - El segundo es el que mueve lo que queda sin pareja y el fondo.
- **Paso sin intermedias** si más del 3 % de las coordenadas 3D no se pueden
  mover.

**Validación:**

| Prueba | Medida | 1.3/1.4 | 1.5 |
|---|---|---|---|
| Verdad conocida, guion de la misión, S=1 | PSNR | 28,47 dB (393 casos) | 28,39 dB (398 casos) |
| Verdad conocida, guion de estrés con palancas | PSNR | 26,72 dB (289 casos) | 27,06 dB (308 casos) |
| Velocidad real, alabeo | px oscuros por intermedia | 16,0 (peor 181) | 0,2 (peor 11) |
| Mapa abierto en giro | diferencia media frente a la real | 1,87 | 1,23 |

- **Verdad conocida, S=1:** el mejor vecino real da 27,50 dB. Con el guion de
  estrés da 25,47 dB en la 1.5.
- **Alabeo:** el peor caso de la 1.4 era el indicador de velocidad del HUD
  deformado.
- **Mapa:** los bloques corridos desaparecen.
- **Caso 3992:** sin grietas.
- Con hilos y sin ellos: 47 de 47 capturas idénticas.
- Emparejar cuesta 0,3 ms por paso.
- `make psx-check` sin fallos.

**SIN RESOLVER:**
- Los puntos oscuros en la silueta de la costa del vídeo no se reprodujeron
  igual en el PC, donde la escena tiene bosque y no mar.
- Los giros de cámara muy bruscos siguen siendo un límite de la interpolación
  en línea recta:
  - la cámara cambia de velocidad dentro del paso;
  - los saltos de más de 128 px no se interpolan: ese paso va sin
    intermedias, con un tirón a 25 fps.

### ROM americana: la versión US de punta a punta (2026-10-17)

Rama `etapa-6-rom-us`. Objetivo: que la recompilación, el arranque nativo y
el APK acepten también el disco americano (`SLUS_009.72`), no solo el europeo.

**Por qué no basta con cambiar el disco.** Las herramientas tenían escritas a
mano direcciones del `ACE.BIN` europeo: límites del código propio y de la
librería, base de los overlays, `$gp`, BSS, `ac3_main`, las funciones de
librería sin firma de Ghidra… En la US todo eso está en otro sitio y el
código propio no va desplazado en bloque: el desplazamiento cambia por zonas
(de `+0x154` a `+0x3D8`) porque hay funciones distintas (no hay menú de
idioma). La librería de Sony sí va entera `+0x3D4`.

**`tools/recomp/region.py`** saca del binario cada dirección, sin escribir
ninguna a mano, y escribe `data/recomp_us/region.json` (Python) y
`data/recomp_us/ac3_region.h` (C):
- **del arranque** (`crt0` en la entrada del PS-X EXE): BSS, `$gp`, base de los
  overlays, `ac3_main` (el `jal` tras restaurar `$ra`), `__main`,
  `stup1`/`stup0` e inicio del código propio;
- **de la tabla de modos** (buscada: modos de los overlays 1 y 2 en la misma
  base): el final de cada overlay;
- **por firma** frente al `ACE.BIN` europeo (ventanas de instrucciones con los
  inmediatos de `lui`/`addiu`/cargas y los destinos de `jal` enmascarados):
  límites de la librería y de `ulz_decompress`, las 16 funciones de librería
  sin firma de Ghidra y los 6 callbacks de `FixFunctionStarts`. Dos de estos
  (discos de hélice de 3 y 5 palas) son casi iguales: la ambigüedad se
  resuelve con el desplazamiento de las vecinas;
- **por correspondencia de datos:** en las funciones emparejadas, las
  direcciones que usan las mismas instrucciones (absolutas, propagadas por
  `addiu`, o relativas a `$gp`) dan 876 parejas EU → US. De ahí salen el
  puntero al estado del juego, el estado y el búfer del mando;
- **el modo de vídeo:** el argumento de la llamada a `SetVideoMode` (EU 1 =
  PAL; US 0 = NTSC, 60 Hz);
- con `--disc`, el ejecutable de arranque de `SYSTEM.CNF` (`\SLUS_009.72;1`),
  que el APK busca para aceptar el disco.

**Validación del perfil:** aplicado al `ACE.BIN` europeo debe dar las 16
constantes que ya usaban las herramientas, más las 16 funciones sin firma y
los 6 callbacks; si no, el programa se detiene. Lo hace en cada ejecución.
Para la US se comprueba además que `ac3_main` por el arranque (`0x80025E5C`) y
por firma coinciden, y que la librería tiene un único desplazamiento.

Comprobación independiente: el enlace con la capa (`gen_hle.py`) de la US
resuelve las **mismas 155 funciones de librería con los mismos nombres** que
la EU, sacados de un análisis de Ghidra hecho aparte sobre la US (firmas
PsyQ): 154 a `+0x3D4` y `__main` a `+0x180`.

| | EU | US |
|---|---|---|
| `ac3_main` | `0x80025D08` | `0x80025E5C` |
| `$gp` | `0x800BA9F4` | `0x800BB454` |
| BSS | `0x800BACB0`-`0x800C9DC0` | `0x800BB808`-`0x800CA908` |
| base de los overlays | `0x800C9DC0` | `0x800CA908` |
| librería | `0x80099DE0`-`0x800AE09C` | `0x8009A1B4`-`0x800AE470` |
| tabla de modos | `0x800AE544` | `0x800AE918` |
| estado del juego (puntero → estado) | `0x800BACD0` → `0x800BD158` | `0x800BB828` → `0x800BDCB0` |
| vídeo | PAL, 50 Hz | NTSC, 60 Hz |

**Cambios en las herramientas** (la EU sigue igual: sin perfil, las mismas
constantes de antes):
- `recomp.py`, `check.py`, `gen_hle.py` y `hle.lib_targets`: opción
  `--region` (perfil) y, en `gen_hle`/`hle`, el volcado de Ghidra de la US;
- `check.py` con `--region` no toca `docs/progreso.json` (la infografía cuenta
  la EU);
- `aceovl.py --table auto`: busca la tabla de modos;
- `recomp_rt.c`: `rc_set_region()` (base de overlays, librería, `__main`,
  `$gp`), con la EU por defecto;
- `native_main.c` y `game_main.c`: las direcciones son macros con la EU por
  defecto; la US las recibe con `-include data/recomp_us/ac3_region.h`. El
  ritmo (50/60 Hz), las muestras de audio por VBlank (882/735) y la hora de las
  intermedias salen de `AC3_REFRESH`.
- Ghidra no admite un proyecto en una ruta con un elemento que empiece por
  `.` (el árbol de trabajo estaba en `.claude/worktrees/`): `make ghidra-us`
  acepta `GHIDRA_US_PROJ=<dir>` para ponerlo fuera.

**Comprobación de que la EU no cambia:** con las herramientas nuevas,
`ace_eu.c` y `overlay1-5.c` salen **idénticos byte a byte** a los de
`data/recomp/` y `hle_gen.c` también. El arranque EU (`make native`, 3.600
fotogramas) da **36 de 36 capturas idénticas** con `native_main.c` y
`recomp_rt.c` de la rama y con los de la base (`d22a33c`).

**Validación de la US:**

```bash
make us-prep ghidra-us   # ACE.BIN, BPB, overlays, perfil y Ghidra (~10 min)
make recomp-check-us     # comparador contra el interprete, como recomp-check
make native-us           # capturas en data/recomp_us/native/shots
```

*Comparador contra el intérprete* (`make recomp-check-us`, 24 estados por
entrada, el mismo arnés que la EU con las direcciones del perfil):

| Módulo | Entradas | Validadas | Distintas | Funciones de Ghidra validadas (US) | (EU, paso 3) |
|---|---|---|---|---|---|
| residente | 2.040 | 1.309 | 0 | 1.126 / 1.239 | 1.130 / 1.237 |
| overlay 1 | 308 | 266 | 0 | 140 / 143 | 142 / 144 |
| overlay 2 | 837 | 545 | 0 | 365 / 402 | 362 / 401 |
| overlay 3 | 128 | 89 | 0 | 22 / 22 | 20 / 21 |
| overlay 4 | 96 | 74 | 0 | 10 / 10 | 10 / 10 |
| overlay 5 | 10 | 8 | 0 | 8 / 8 | 8 / 8 |
| **total** | **3.419** | **2.291** | **0** | **1.671 / 1.824 (91,6 %)** | 1.672 / 1.821 |

El resto son «sin caso», como en la EU (con estados aleatorios fallan o
agotan el tiempo en los dos lados). El overlay 3 de la US es más grande
(92.584 B frente a 85.800) y da 128 entradas frente a 49.

*Arranque nativo* (`make native-us`, guion `GUION_US`): pantalla de carga,
título («PRESS START BUTTON», f300), menú de modo, dificultad, presentación
de la campaña, menú de misiones, briefing de la primera misión («Transport»),
selección de avión (EF2000E Typhoon II) y armas, película de la UPEO, texto
de prólogo (se salta con Start, f3100) y **la primera misión en vuelo con
«ENGAGE»** (f3300), HUD y radar. La US no tiene menú de idioma: llega al
título hacia el fotograma 250 (60 Hz); el guion de la EU pulsa Start en el
título en el 1100 (50 Hz). Entre 130 y 290 fotogramas por segundo de CPU en el
PC, según la ejecución.

**APK:** `REGION=us android_game/build.sh` → `data/android/ac3game-<versión>-us.apk`
(la EU sigue saliendo como `ac3game-<versión>.apk`, igual que antes). Otro
paquete, `com.vzepec.ac3game.us` («AC3 Recomp (US)»), para que conviva con la
EU en el aparato. Al elegir el disco, la US comprueba que contiene
`SLUS_009.72`; si no, lo descarta y vuelve a abrir el selector.

**SIN RESOLVER:**
- La US **no se ha probado en la Retroid** (ni el APK ni el ritmo de 60 Hz con
  intermedias: en la misión la US dibuja una imagen cada 2 VBlank, 30 por
  segundo).
- `BUDGET_PER_FRAME` (25.000) está calibrado para la EU; en NTSC el fotograma es
  más corto (~564.000 ciclos frente a ~677.000). No se ha medido si hace falta
  otro valor.
- `tools/recomp/record.py` y `boot.py` (comparador con estados reales en el
  intérprete) siguen siendo solo EU.
- Los nombres propios de `tools/ghidra/names.tsv` (EU) no se aplican a la US.

### APK universal (1.6-alfa)

Una sola app para los discos europeo y americano. Petición del dueño tras
probar la 1.5 de cada versión; según él, la americana va más estable y con
menos artefactos.

- **Tabla por versión** (`android_game/src/ac3_regions.h`): dirección de
  `ac3_main`, `gp`, BSS, overlays, librería, refresco, código traducido,
  tablas de HLE, firmas de overlays, `ACE.BIN` y ejecutable de arranque del
  disco.
  - `regions.c` se compila una vez por versión. La americana toma sus
    valores de `data/recomp_us/ac3_region.h` (`tools/recomp/region.py`).
- **Elección:** `game_main.c` (`disc_region`) busca en el disco abierto el
  ejecutable de arranque de cada versión que lleva el APK (`SCES_020.66`,
  `SLUS_009.72`). El refresco (50/60 Hz) y las muestras de audio por VBlank
  (882/735) pasan a ser variables.
- **Símbolos:** las funciones traducidas se llaman por su dirección
  (`f_<direccion>`) y las tablas tienen el mismo nombre en las dos versiones.
  `build.sh` renombra todos los símbolos definidos en los objetos americanos
  con el sufijo `_us` (`llvm-nm` + `llvm-objcopy --redefine-syms`), y el
  cambio alcanza también las referencias entre ellos.
- **`build.sh`:**
  - `REGION=all`, el valor por defecto, genera el universal,
    `ac3game-<version>.apk`, paquete `com.vzepec.ac3game`.
  - `REGION=eu` y `REGION=us` generan las versiones sueltas con paquete propio
    (`.eu`, `.us`).

**Comprobado:**
- Enlaza sin símbolos duplicados.
- La biblioteca define `ac3_region_eu` y `ac3_region_us`, con 4.077
  funciones `f_` entre las dos versiones.
- Lo único sin resolver son las bibliotecas del sistema.
- Las tres variantes se generan: universal de 7,0 MB y sueltas de 3,6 MB.

**SIN RESOLVER:** la prueba en la Retroid con los dos discos.

### Paso 11: las 41 misiones, una a una (versión americana)

El dueño ha jugado varias misiones en la Retroid sin errores. Para
comprobarlas todas sin completarlas en orden, se fuerza la misión que carga
el juego.

**Cómo elige el juego la misión** (decompilación de Ghidra):
- La función que arranca la misión (EU `FUN_800355f0`, US `FUN_800357b8`)
  toma el `short` de **`g_state + 0x390`**, la misión actual, y carga el
  registro `mission_records[n]` del BPB.
- `g_state` es el puntero en EU `0x800BACD0` y en US `0x800BB828`.
- `mission_records` es una tabla de **41** entradas (registros 6 a 46),
  idéntica en las dos versiones (EU `0x800AF748`, US `0x800AFB1C`).

**Herramienta:**
- `mipsi_set_hook`: el intérprete llama a una función del anfitrión al
  entrar en una dirección.
- `AC3_MISSION=N` en el binario libre escribe N en `g_state + 0x390` la
  primera vez que se entra en la función de arranque. Las siguientes las
  encadena el juego.
- `tools/misiones.py` y `make misiones` recorren las misiones: 4.000
  fotogramas cada una con un guion que llega a la misión y vuela a ciegas
  disparando misiles. Anotan la misión que arranca, los fallos `rc_fault`, la
  salida del proceso y las capturas, y generan hojas de contactos.

**Resultado** (US, binario libre = el código del APK):
- **41/41 misiones cargan, arrancan y terminan sin ningún fallo.** Cada una
  dibuja su escenario: ciudades, costa, cañones, noche, nieve, despegues,
  túneles del Geofront, el espacio, la Electrosfera…
- **Encadenamientos:**
  - La **16** es la secuencia del lanzamiento, sin juego. Al acabar, el juego
    encadena la **17** ("Leaving atmosphere"), que se vuela en el espacio.
  - La **1** (primera de la campaña) y la **29** muestran antes un texto de
    la historia ("PEACEMAKER", "REVELATION"). Con Start se salta y se llega
    a "ENGAGE" y al vuelo.
- La misión que el juego arranca por sí solo en la partida nueva es la 1.

**SIN RESOLVER:**
- completar cada misión y los finales;
- el árbol de decisiones de la historia;
- guardar y cargar en la tarjeta;
- la comparación con DuckStation.

**Reproducir:** `make misiones` (`MISIONES=0-40`).

### Paso 12: cobertura, estados reales e infografía nueva

**Pregunta del dueño:** la infografía de `develop` daba 91 % recompilado y el
recorrido sin avance, a pesar de las 41 misiones. ¿Se puede avanzar?

- **El 91 %** eran funciones validadas con estados aleatorios. El resto no
  salía distinto, sino *sin caso*: con datos aleatorios no terminaba ningún
  caso.
- **Validación con estados de partidas reales:**
  - el intérprete anota las funciones en que entra (`AC3_COVERAGE`,
    `tools/cobertura.py`);
  - guarda el estado al entrar en las pendientes (`AC3_STATES`,
    `tools/estados.py`);
  - `tools/recomp/check.py --states` compara desde ahí el intérprete del APK
    con el de referencia.
- **Resultado:**
  - americana: 1.671 → **1.750/1.824 (95,9 %)**;
  - europea: 1.672 → **1.754/1.821 (96,3 %)**;
  - **0 distintas** en todas las pruebas;
  - en las 41 misiones se ejecutan 1.065 (US) y 1.058 (EU) funciones.
  - Registro: `docs/pruebas/2026-10-18-validacion-estados-reales.md`.
- **Versión europea:** las 41 misiones, igual que la americana: 41/41 sin
  fallos (`make misiones-eu`).
- **Infografía nueva** (`tools/progress.py version us|eu` y `svg`): mide
  la versión americana y lo que lleva el APK, que es el intérprete.
  - código validado (aleatorio o real, sin ninguna diferencia);
  - funciones ejecutadas en las misiones;
  - capa;
  - pasos de la etapa (5/8);
  - recorrido en dos barras: funciona (45/46, falta el final) y comparado
    con DuckStation (0/46);
  - la europea, resumida al pie.

**Pendiente:**
- 63 funciones por versión que no se ejecutan en vuelo (menús, finales,
  overlays 3-5);
- los finales;
- la comparación con DuckStation.

### Paso 13: finales, créditos, guardar y cargar

- **Finales:**
  - `FUN_80037d58` escribe en `g_state + 0x3AC` la secuencia de final según
    el nodo de la historia: 1 para `0x2E`, 2 para `0x36`/`0x3B`.
  - El **modo 5** la reproduce; con 2 sigue en los **créditos** (modo 7,
    overlay 3).
  - `AC3_SEQ=v` la fuerza en la demostración del título.
  - Resultado (`tools/finales.py`, `make finales` / `finales-eu`): las dos
    secuencias se reproducen en las dos versiones y el juego vuelve solo a su
    ciclo, sin fallos. Los créditos completos duran unos 4 minutos.
- **Guardar y cargar** (`tools/guardado.py`, `make guardado` /
  `guardado-eu`):
  - guarda desde la interfaz de la UPEO: `open` con FCREAT, `write` de 4.860
    B (US) o 4.604 B (EU), partida válida con cabecera "SC";
  - carga desde el menú principal con una copia de la tarjeta;
  - sin fallos en las dos versiones.
  - El primer intento no guardaba: en "Save to file #1?" el cursor empieza
    en "no". El fallo era del guion, no de la capa.
- **`PSX_CARDDBG=1`:** la capa de tarjeta anota las llamadas del juego
  (open/read/write/firstfile).
- **Registro:** `docs/pruebas/2026-10-18-finales.md` y
  `2026-10-18-guardado.md`.
- **Recorrido:** funciona 46/46 (arranque, menús, 41 misiones y final).
  Falta compararlo con DuckStation.

### Paso 16: pantalla ancha (release v1.8-alfa)

**Nombres:** las versiones de prueba se llamaron 1.10-alfa y 1.11-alfa. Lo
publicado es la release **`v1.8-alfa`** (la 1.11, con `versionCode` 13).
No tiene que ver con los APK de prueba "1.8/1.9-alfa" del primer intento,
que nunca se publicaron.

**Primer intento descartado (rama `etapa-6-paso-15`, APK 1.8/1.9-alfa).**
Juntaba tres cosas: dibujo por GLES a alta resolución, vértices sin temblor
y pantalla ancha. En la Retroid, en las dos versiones, aparecieron:
- vértices que se estiran en líneas largas por la pantalla;
- huecos en el modelo del avión y en el terreno lejano;
- el mapa del radar fuera de su círculo.

El dueño decidió deshacerlo todo y volver a añadir las tres cosas **una por
una**, probando cada una en la Retroid. La rama queda guardada sin fusionar.

**Solo pantalla ancha, sobre el dibujo por software de siempre.**
- **GTE:** `psx_gte_wide = 0.75` comprime la x proyectada (RTPS/RTPT)
  alrededor de OFX. El juego dibuja en 320 lo que cabría en 427 (16:9), y la
  imagen se presenta ensanchada a 16:9 (`PSX_FIT_169`, `psx_present.c`).
- **Cono de visión del terreno:** el juego elige las casillas visibles con
  su propio cono, calculado con el ancho de pantalla de la cámara (short en
  `a0+4`, vale 320) en EU `0x8005E140` / US `0x8005E3E4`.
  - Sin tocarlo, faltaba terreno en las esquinas inferiores.
  - Un enganche del intérprete pone 427 al entrar y devuelve 320 al entrar
    en la función que lo sigue (EU `0x8005FD68` / US `0x8006000C`):
    `ac3_wide_install`, en `psx/recomp/ac3_versions.c`.
  - El intérprete admite ahora hasta 8 enganches (`mipsi_set_hook`).
  - Direcciones US: por coincidencia de bytes del código EU (instrucciones
    con dirección enmascaradas). Lectura comprobada con `AC3_WIDE_DBG=1`:
    "ancho 320, alto 240" (US) / "alto 256" (EU).
- **Solo en la misión:** la pantalla ancha se activa mientras el juego
  calcula ese cono (cada imagen con terreno) y se apaga 15 VBlank después.
  Menús, briefing y vídeos siguen a 4:3. Cada imagen de la cola lleva su
  marca de pantalla ancha, para presentarla con su encaje.
- **L3+R3:** 60 fps + 16:9 → 60 fps + 4:3 → original. El contador lo muestra
  (`16:9`, `4:3`, `ORIG`).

**Comprobado en el PC** (`AC3_WIDE=1` del binario libre):
- misión 21 en la US y la EU, sin huecos en los lados ni fallos;
- el radar con el dibujo por software queda dentro de su círculo, ensanchado
  como el resto del HUD;
- misión 2 (US) con imágenes intermedias: las mismas intermedias que a 4:3
  (21 de 36 en la ventana), sin picos;
- la capa: `make psx-check` sin cambios.

**Pistas para el siguiente punto (alta resolución), sin comprobar:** con el
dibujo por software el radar sale bien. Su desborde con GLES encaja con que
`psx_hw.c` no emulaba el bit de máscara (`SIN RESOLVER`).

**Conocido:** el HUD y los sprites de la CPU (explosiones, humo) salen
ensanchados 4/3. Es lo normal de esta técnica.

**Probado en la Retroid (1.10-alfa):** funciona y va mucho más estable que
el primer intento.

**1.11-alfa, menús también a 16:9** (petición del dueño):
- En el modo `16:9` se presenta todo ensanchado, no solo la misión.
- **Excepción, los vídeos:** van en color de 24 bits y siguen a 4:3, para
  no deformarlos. Comprobado en el PC con `AC3_GEOM_DBG=1`:
  - pantalla de título y menús: 15 bits (320×480);
  - vídeo de la introducción: 24 bits (320×240).
- **En los menús la GTE no comprime:** solo se estiran, así que sus modelos
  3D (hangar) salen 4/3 más anchos.
  - Comprimir allí podría descuadrar lo que el juego coloca a mano sobre lo
    que proyecta la GTE (`SIN RESOLVER`: no se ha probado).
  - En la misión la GTE sigue comprimiendo como en la 1.10.

**Publicada como `v1.8-alfa`** (2026-10-19), a petición del dueño:
- la rama `etapa-6-paso-16` se integra en `develop` y en `main`;
- la release adjunta el APK con la auditoría a 0 y su SHA-256.

Siguiente: la alta resolución por partes, con un comparador automático
software/GLES por misiones enteras antes de ir a la Retroid (`STATUS.md`).

### Paso 17: alta resolución por GLES, comprobada antes de ir a la Retroid (APK 1.9-alfa, sin publicar)

Punto 2 del plan por partes (pantalla ancha → alta resolución →
antitemblores). Rama `etapa-6-paso-17`. Del primer intento (rama
`etapa-6-paso-15`) se recupera solo el dibujo por GLES: `psx_hw.c`, el
registro de primitivas de `gpu.c` y el reparto por la interpolación. **Sin
antitemblores.**

#### 1. El comparador

- **Cómo funciona** (`psx/gl/psx_hwcmp.c`, `AC3_HWCMP=<dir>` del binario
  libre, `tools/hwcmp.py`, `make hwcmp`):
  - en partida real, cada lista de dibujo del juego la dibuja la GPU por
    software (la referencia) y GLES la vuelve a dibujar a escala 1, desde
    la misma VRAM de partida;
  - con `AC3_INTERP` + `AC3_INTERP_THREADS` compara también las imágenes
    intermedias de los 60 fps: los hilos de dibujo las rasterizan por
    software además de anotarlas (`psx_interp_hw(sumidero, 1)`).
- **Qué cuenta como "bloque malo"** (8×8):
  - 8 o más píxeles que difieren en más de 40 en algún canal;
  - y cuyo color no está en los 3×3 vecinos de la otra imagen.
  - Así un borde o una línea movidos un píxel no cuentan, y un hueco, un
    pico o un recorte de máscara sí.
- **Además:** listas por debajo del 99 % de píxeles casi iguales, para
  diferencias repartidas.

**Validado con el caso positivo conocido** (regla 3):
- con el bit de máscara desactivado a propósito (`PSX_HW_NOMASK=1`), la
  misión 0 da **570 listas con bloques malos** (el radar);
- con la máscara, **0**.
- En la primera pasada, el comparador detectó el radar desbordado de la
  Retroid en todas las imágenes de misión: 34 bloques en (8,8)-(79,79).

#### 2. Lo que encontró, y la corrección

| Fallo | Causa | Corrección |
|---|---|---|
| Radar desbordado de su círculo (el del vídeo del dueño) | `psx_hw.c` no emulaba el **bit de máscara**. En el radar, las primitivas que comprueban la máscara son semitransparentes con textura | **Stencil** de 1 bit = bit 15. Cada primitiva escribe lo que la GPU dejaría (texel / poner máscara; dos pasadas en las texturadas). Las que comprueban, pintan solo donde vale 0. El fondo lleva su bit 15 de partida. Solo si la lista tiene alguna primitiva que comprueba |
| Bordes de polígono un píxel fuera | GL mira el centro del píxel; la GPU, la esquina | Geometría desplazada casi medio píxel de salida (`u_shift`). Color, uv y tramado se llevan a la esquina exacta (`u_delta`). Si no, una uv decreciente lee el texel anterior: era la línea de más del borde del mapa del radar |
| Líneas: pendiente y redondeo | El cuadrilátero alargado un píxel cambiaba la pendiente | Paralelogramo con la pendiente exacta y desempate como la GPU (lejos de cero), compensando el desplazamiento de muestreo. `psx/test/test_hwlines.c`: **5043 de 5043 líneas idénticas** |
| Fondo leído un píxel al lado | La copia directa de la VRAM no tenía el margen de +0,001 | Margen añadido. Una lista de la misión 2 pasa de 37 a 49 dB |

**Fotograma de prueba** (`make psx-hw`): de 37,2 dB y 97,7 % casi iguales a
**50,5 dB y 99,88 %**.

#### 3. Resultado en las 41 misiones (US, escala 1)

- **127.072 listas, 1 con bloques malos:** una línea de la rejilla del túnel
  (misión 37), desplazada un píxel.
- **Imágenes intermedias** (misión 2, 3 por paso): 4.207 listas, de ellas
  1.077 intermedias, **0 bloques malos**.
- **Conclusión:** GLES dibuja las intermedias igual que el software. Los
  picos del primer intento no venían del dibujo GLES. Quedan como
  sospechosos (`SIN RESOLVER`):
  - los antitemblores de entonces;
  - el driver de la Retroid (Adreno), que Mesa no reproduce;
  - o la propia interpolación, que también se ve con el dibujo por
    software (el dueño ve parpadeos en edificios con la 1.8).
- **Diferencias que quedan:**
  - degradados de las líneas: un escalón de 5 bits, porque el software
    trunca hacia cero;
  - redondeo de la mezcla semitransparente (GL en 8 bits, la consola en 5).
  - Ninguna se ve como fallo.

#### 4. APK 1.9-alfa

- **L3+R3** recorre GLES ×N + 16:9 (`GLX4 16:9`) → software 16:9
  (`SW 16:9`, como la 1.8) → software 4:3 (`SW 4:3`) → original (`ORIG`).
- **Escala:** la entera que quepa en la pantalla, hasta ×4.
- Los menús a 60 imágenes por segundo siguen por software.

#### 5. Rendimiento (APK 1.9.1-alfa)

**En la Retroid, la 1.9-alfa se ve bien pero va lenta:** 10-20 imágenes por
segundo en `GLX4 16:9`, según los objetos y el mapa.

**Causa, medida en el PC** (`test_hw` imprime las llamadas por imagen a
escala 4): **1.141 a 1.433 llamadas de dibujo por imagen**, casi dos por
primitiva, cada una con su propia subida de vértices. En un móvil cada
llamada cuesta decenas de microsegundos, y la app dibuja hasta 60 imágenes por
segundo. Venía de:
- semitransparencia en dos pasadas con estados distintos;
- stencil en dos pasadas en todas las texturas, en cuanto la lista comprueba
  la máscara (el radar: todas las imágenes de misión);
- una subida de vértices por lote.

**Corrección** (`psx/gl/psx_hw.c`):
- **Mezcla única** `ONE, ONE_MINUS_SRC_ALPHA`: el shader saca color
  premultiplicado y alfa según el modo (opaco 1; abr 0: F/2 con alfa 0,5;
  abr 1: F con alfa 0; abr 3: F/4 con alfa 0). Solo la resta (abr 2) tiene
  su propio estado.
- **Stencil exacto (dos pasadas) solo en lo necesario:** primitivas
  anteriores a la última que comprueba la máscara y que tocan su zona.
- **Lotes:** las llamadas se encolan con su estado y los vértices de cada
  tramo (entre subidas de VRAM) suben de una vez.
- **Texturas:** la VRAM va en 3 texturas que se turnan, y solo se suben las
  franjas de 16 filas que cambian.
- **App:** el hilo de pantalla intercambia los búferes con la ranura en vez
  de copiarlos.

| Fotograma | Llamadas antes | Llamadas ahora |
|---|---|---|
| Prueba | 1.141 | 104 |
| Misión 2 | 1.433 | 106 |
| Túnel | 224 | 51 |

**Mismo resultado:**
- mismos dB a escala 1;
- comparador sin bloques malos en la misión 2 (con intermedias) y en la 21;
- `test_hwlines` 5043/5043.

**Contador:** en `GLX4` hay una tercera línea, `GL x.x  SWAP y.y`: ms por
imagen en el dibujo (CPU) y en `eglSwapBuffers`. También va a logcat, con
llamadas y subidas por imagen. Sirve para ver si sigue limitando la CPU del
hilo de pantalla o la GPU (`SIN RESOLVER` hasta probarlo).

### Paso 18: fidelidad a la PS1 — la GTE contra una de referencia (APK 1.9.2-alfa, sin publicar)

**Problema.** Con la 1.9.1 (rendimiento bien), el dueño sigue viendo
titileos, parpadeos y vértices que se estiran, sobre todo en ciudades
(vídeo e imágenes en `imagenes bugs/`).

**Análisis del vídeo** (detector de "cambia y vuelve" por bloques entre
fotogramas consecutivos):
- los peores eventos son tramos de puente y autopista que se estiran
  durante 1-3 fotogramas y vuelven (t = 24,97 s, 25,67 s, 27,30 s);
- también hay parpadeos de piezas cercanas.

**Estrategia de raíz: comparar cada capa con una referencia contrastada
con hardware, en partida real.** La GPU ya tiene su comparador (paso 17).
La **GTE** solo se había validado con la especificación y pruebas propias
(etapa 5, "sin verificar contra hardware").

**Oráculo de la GTE** (`psx/test/gteref.c`, `tools/gteref.sh`,
`make gteref`):
- cada orden COP2 del juego se repite en la GTE de Mednafen (Beetle PSX,
  commit fijo), con los 64 registros de entrada;
- se comparan los 32 de datos y FLAG.
- La de Mednafen es GPL: se descarga a `data/ref/` (fuera de git) y solo la
  enlaza el binario de pruebas `psx/build/ac3_gteref`. Nunca entra en el
  repositorio ni en el APK.

**Hallazgo: IR0 de RTPS/RTPT** (el factor de niebla de cada vértice):
- con el vértice muy cerca de la cámara, `DQA * (H/SZ)` desborda MAC0;
- el hardware satura IR0 desde el valor completo y da **0** (sin niebla);
- la nuestra lo hacía desde MAC0 truncado y daba **0x1000** (niebla total).
- El terreno y los objetos guardan IR0 por vértice (`terrain_draw`:
  `stIR0` tras cada RTPT): los polígonos más cercanos pasaban de golpe al
  color de la niebla.
- Antes de corregirlo, el comparador dio ~120.000 diferencias en IR0 en la
  misión 0 (caso positivo); después, 0.

**Resultado:** las 41 misiones US, **133.445.920 órdenes de la GTE, 0
diferencias** con Mednafen (RTPS, RTPT, NCLIP, MVMVA, DPCS, DPCT, INTPL,
SQR, AVSZ3/4, GPF, GPL, NCS, NCT).

**Siguiente sospechosa: la interpolación** (`SIN RESOLVER`):
- una clave de modelo "única" en los dos pasos se empareja sin contrastarla
  con el movimiento de la cámara (salvo saltos de más de 48 px);
- en ciudades de piezas repetidas, el tramo A de un paso y el B del
  siguiente (idénticos) pueden emparejarse;
- en las intermedias, esos vértices viajarían de una pieza a otra:
  estiramiento de 1-3 fotogramas, como en el vídeo.
- Hay que confirmar si los estiramientos salen también en `ORIG` (sin
  intermedias) y reproducir la partida en el PC.
