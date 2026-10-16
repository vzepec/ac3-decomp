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

## Plan

| Paso | Qué | Estado |
|---|---|---|
| 1 | Completar la capa: lo que el juego llama y `psx/` no tiene (tabla abajo) | ⏳ siguiente |
| 2 | Salida de vídeo: el área visible de la VRAM a pantalla con GLES (PC con EGL y Android), imagen 1:1 | ⏳ |
| 3 | Recompilador MIPS → C (`tools/recomp/`): residente y 5 overlays, validado función a función contra `mipsemu` | ⏳ |
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

(vacío: la etapa acaba de abrirse)
