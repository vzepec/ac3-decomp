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
| 3 | Recompilador MIPS → C (`tools/recomp/`): residente y 5 overlays, validado función a función contra `mipsemu` | ⏳ siguiente |
| 4 | Arranque nativo en el PC: ejecutable de arranque → `ACE.BIN` → bucle principal; meta: **pantalla de título** | ⏳ |
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
