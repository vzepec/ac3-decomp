# STATUS — estado actual del proyecto

**Última actualización:** 2026-10-27 (etapa 9, paso 1: la partida es determinista, base del cooperativo en red; publicada la `v1.13.2-alfa`; se trabaja con la versión americana)

> Arranque en frío: `make brief`. Contrato: `CLAUDE.md` / `AGENTS.md`.
> Flujo desde terminal: `docs/08-continuar-por-consola.md`.

## Etapa actual

**Etapa 6 — El juego, nativo: EN CURSO** (plan en `docs/11-etapa6.md`). El
juego corre nativo sobre la capa `psx/`, en las versiones europea y
americana, hasta la primera misión. Falta el recorrido completo (paso 5).
**Etapa 7 — APK libre: publicada** (`v1.13.2-alfa`, `docs/12-etapa7.md`; paso 7: menú y actualizador). Desde
ahí el único APK que se genera es `AC3Recomp-<versión>.apk`, que interpreta el
juego desde el disco del usuario. Etapas 1-5 cerradas.
**Etapa 8 — Interpolación pulida: publicada en la `v1.9-alfa`**
(`docs/14-etapa8.md`). El mundo se redibuja con el código del juego en cada
imagen intermedia, en un hilo de dibujo.
**Etapa 9 — Juego en red (experimental): EN CURSO** (`docs/15-etapa9.md`, rama `etapa-9`). La campaña en
cooperativo por lockstep, sin servidor propio. Paso 1 (determinismo) hecho.

**Ramas:** `main` (base y releases) y `develop`. Cada trabajo nuevo va en su
rama (`etapa-6-paso-N`), que se fusiona y se borra al subir.

## Hecho

- ✅ Discos EU y US documentados (`docs/01-disco.md`, `tools/psxiso.py`)
- ✅ Ejecutables analizados; `ACE.BIN` comprimido; PsyQ de stock
  (`docs/02-binarios.md`, `tools/psexe.py`)
- ✅ Índices `AC3E` resueltos, 100 % de cobertura (`tools/acepack.py`)
- ✅ **Tag del BPB** = tamaño del registro en `u32` (127/127 EU y US) — `make tags`
- ✅ Cabecera común de registro (`type`, `header_size`, offsets de sección)
- ✅ **Compresión ULZ de Namco** resuelta: 659/659 secciones — `make ulz-check`
- ✅ **Texturas TIM a PNG** (estaban dentro del ULZ) — `make textures REC=0006`
- ✅ **Modelos: vértices y caras** (POLY_FT4/GT4), 14.095/14.095 — `make models`
- ✅ **Mapa de teselas** del terreno (sección 0, índice base 1, 99,4 %)
- ✅ Mapa de secciones de los tipos 4 (aviones), 5 (briefings) y 17 (misiones)
- ✅ EU y US comparten terreno, modelos y texturas; solo cambian textos y la
  sección 15
- ✅ `ACE.DAT` es relleno (`tools/acedat.py`)
- ✅ **Bancos de sonido del SPU** (tipo 4 s2, tipo 17 s15, tipo 13 s8): 63/63
  por disco, ADPCM a WAV — `make sounds`. Corrige la hipótesis de "punteros a
  código": eran direcciones de la RAM del SPU
- ✅ **Aviones de misión con LOD** (tipo 17 s02/s03), vértices ×1/×2/×4 y
  POLY_GT3/GT4: 578/578 — `make models`
- ✅ `ACE.SPB` = audio XA + vídeo, por subcabeceras de sector — `acepack.py xa`
- ✅ **Registro → nombre de avión** resuelto por silueta (`acemodel.py
  hangar`): 0048-0068 = los 21 del hangar en orden; 0047 = Geopelia oculto
- ✅ **Avión del jugador exportable con texturas** (`acemodel.py
  export-player`): validado a ojo (F-15 del registro 0055)
- ✅ **Avión del jugador encontrado**: en el preludio del registro de tipo 4
  (sección inicial que el modelo de registro ignoraba). 22 aviones, 264/264
  modelos, 7.895/7.895 primitivas validadas — `acemodel.py player`. Los
  registros de tipo 1 grandes son las **demos**
- ✅ **Etapa 3, primer corte** (`docs/09-codigo.md`): Ghidra 12.1.4 +
  `ghidra_psx_ldr` en `~/tools`. `ACE.BIN`: 2.011 funciones, ~749 PsyQ,
  **~1.262 propias (473 KB)**, 1.939 COP2 (incluye 155 funciones que Ghidra
  no veía por invocarse solo por puntero). Mapa de módulos (no hay overlays
  de código; `ACE.EXE` = pantalla anticopia). Bucle principal en `main`
  `0x80025D08` — `make ghidra-import ghidra-stats`
- ✅ **Sistema de packs localizado en `ACE.BIN`**: apertura (`0x8002A9E4`),
  streaming (`0x8002A34C`), descompresor ULZ (`0x800AE118`). Con ellos:
  **tag del SPB resuelto** (72/72, 88/88) y **ULZ variante 0 resuelta**
  (604/604; tipo 13 = hangar: vistas y nombres de los 21 aviones)
- ✅ **Primitivas resueltas desde el código** (intersección con el terreno,
  `0x80097EDC`): `h1` = plano/triángulo/partido, `e0/e1` = offsets a normales
  de cara, **sección 5 = tabla de normales**. 77.831/77.831 normales
  coherentes — `make models`
- ✅ **Cargador de misión** (`0x80035730`) localizado: confirma desde el
  código el mapa de secciones del tipo 17, y revela que s2 y s3 forman una
  sola tabla de modelos
- ✅ Formato de `ACE.BIN` confirmado contra el descompresor real de
  `SCES_020.66`; resueltos los campos `0x04`, `0x14` y `0x5C`
- ✅ **Etapa 2: `ACE.BIN` descomprimido** (EU 700.416 B, US 704.512 B),
  validado con la suma de control de la cabecera — `make acebin`

## Etapa 7: APK libre — PUBLICADO (v1.7-alfa; v1.8-alfa con pantalla ancha)

`docs/12-etapa7.md`. **El único APK que se genera:** `sh android_game/build.sh`
→ `data/android/AC3Recomp-<versión>.apk`.
- Intérprete MIPS en C y capa `psx/`; lee `ACE.BIN` del disco del usuario.
- 0 bytes del juego según `tools/apk_audit.py`.
- Probado en la Retroid: `VEL` 100 %.
- Release alfa en GitHub con [`INSTALAR.md`](INSTALAR.md): requisitos,
  instalación y controles.

**Desde 2026-10-18 se trabaja con la versión americana** (`SLUS-00972`).
Según el dueño, en la Retroid es mucho más estable. La europea se mantiene
compilando, pero no se afina. Pruebas de referencia: `make libre
DISCO="$(US)" LIBRE_GUION="$(GUION_US)"` y `make native-us`.

## Siguiente tarea concreta

**Etapa 9, paso 1 (rama `etapa-9-paso-1`, `1.13.3-alfa` de prueba): determinismo, hecho** (`docs/15-etapa9.md`).
Mismos mandos → misma RAM en el PC, la Retroid y el S24, y **el S24 y la Retroid dan la misma partida** (aparato
contra aparato, el caso del juego en red); intermedias, 60/120, GPU y resolución no cambian la partida;
el 16:9 sí (el anfitrión lo fija para los dos). Arreglado de paso: el redibujo adelantaba la semilla de `rand` de la
partida (con intermedias e hilos, otra partida; y la autoprueba t = 1 de las 41 misiones pasa de 805 imágenes
distintas del original a 0).
- **Pendiente:** la europea en el aparato. El arreglo de la semilla toca el redibujo: probarlo jugando antes de
  publicarlo (misiones 8, 17, 19, 22 y 25; «AC3 Dev» 1.13.3 instalada en la Retroid y el S24).

**Etapa 9, paso 2 (rama `etapa-9-paso-2`): el avión del jugador, mapeado** (`docs/15-etapa9.md`,
`tools/entidades.py`). Entidad 0 en `g_render+0x3BC`; contexto en `DAT_800BBA74/78` y la entrada `g_state+0x120`;
**ranura 1 de avión libre en las 41 misiones**; el juego ya lee el puerto 2 (ranura 4); 34 funciones de vuelo usan
al jugador (cámara, HUD, puntería, dibujo, guion).
- **`SIN RESOLVER`:** a quién persigue la IA.

**Etapa 9, paso 3 (rama `etapa-9-paso-3`): el segundo avión, en el PC** (`psx/recomp/ac3_coop.c`, `AC3_COOP=1`,
`tools/coop.py`).
- **Funciona:**
  - vuela con el mando del puerto 2;
  - el juego lo dibuja con el modelo del jugador;
  - dispara misiles y metralleta desde su avión;
  - el jugador 1 queda idéntico en las 41 misiones;
  - 0 fallos;
  - determinista.
- **Siguiente:** paso 4, el combate del jugador 2:
  - sus efectos fuera de la cabina del 1 (ráfaga, estelas);
  - las órdenes de modo también para él;
  - su puntería, HUD y radar;
  - el daño, la caída y la reaparición;
  - a quién persigue la IA.

**Al publicar una release:** la nota lleva `## Novedades de la …` y
`## What's new in …` (el menú muestra la del idioma elegido). La etiqueta
`vX.Y-alfa` es la versión que compara el actualizador.

**APK de desarrollo** (etapa 8, paso 9; no se publica): `DESARROLLO=1 sh android_game/build.sh`, lista de
misiones y entrada directa. Pruebas en la Retroid: `tools/retroid_mision.sh`.

**Paso 10 (rama `etapa-8-paso-10`):** terreno en los bordes en 16:9 y bajones del efecto del virus
(`docs/14-etapa8.md`).
- **Hecho y confirmado por el dueño en la Retroid:**
  - el virus a 59-60 fps y sin fallos gráficos (la estela: el software dibuja la imagen real cuando la
    lista la lee como textura);
  - los puntos del radar y del mapa grande, bien.
  - los bordes en 16:9 sin temblor (decimales de la GTE: un entero ambiguo, sin decimales) y el centro
    suave.
- **Publicado en la `v1.13-alfa`** con las mejoras acumuladas (pasos 5-7 y 10, etapa 7 paso 9), tras
  probar el dueño la 1.12.20-alfa.
- **`SIN RESOLVER`:**
  - por qué el fallo de los bordes no sale en el PC;
  - las 6 listas malas y la misión 37 del comparador (anteriores a este paso).

**Pantalla negra al acabar la Maze: arreglada** (`v1.12.1-alfa`, `docs/14-etapa8.md` paso 8;
`make fin-mision`). La release solo lleva ese arreglo.

**Pantallas de 120 Hz (paso 12): publicado en la `v1.13.1-alfa`.** Galaxy S24 a 120 fps, opción 60/120 en el
menú (bloqueada sin 120 Hz), fases del redibujo repartidas entre hilos y el redibujo que no cabía en la misión 19
(el cielo temblaba) en el otro búfer de primitivas (`docs/14-etapa8.md` paso 12).
- **Pendiente:** jugarla en la Retroid (60 Hz). El arreglo del búfer también actúa ahí; las 41 misiones en el PC
  con 1 intermedia salen sin fallos.
- ~~`SIN RESOLVER`: las imágenes de la misión 19 que la autoprueba t = 1 da distintas en el HUD y el radar~~:
  resuelto en la etapa 9, paso 1. Era el ruido de `FUN_80082f68`: el redibujo usaba otra semilla de `rand`.

**El avión a 120 Hz (paso 13): publicado en la `v1.13.2-alfa`** (confirmado por el dueño en el S24). Temblaba
siempre (decimales de la GTE en las fases repartidas) y, al acelerar, salía pequeño y sin textura en la primera
intermedia (el avión que sigue la cámara no se movía con ella en el redibujo).
`docs/14-etapa8.md` paso 13.
- **Pendiente:** jugar la 1.13.1 y la 1.13.2 en la Retroid (60 Hz) y ver la opción de 120 bloqueada en una
  pantalla de 60 Hz.

**Aparcado, sin publicar:**
- el HUD sin ensanchar en 16:9 (rama `etapa-8-hud-ancho`);
- la cámara libre (paso 11, rama `etapa-8-paso-11`): en la Retroid el dueño no notó ningún cambio y pidió
  quitarla; no está en la `v1.13-alfa` (`SIN RESOLVER` por qué no se notaba).

**Alta resolución: hecha** (`v1.12-alfa`, `docs/14-etapa8.md` paso 4).

**Explosiones a 60 fps:** probadas tres formas, ninguna mejora la prueba de verdad conocida (`SIN RESOLVER`, paso 6 de la etapa 8).

**Mando táctil: primera versión** (`v1.12-alfa`, `docs/12-etapa7.md` paso 8). Ajustarlo con lo que diga el dueño.

**Siguiente, a elegir con el dueño:**
- otros filtros de textura (xBR);
- la estela del misil junto a la cámara: comprobar en otro emulador si la
  consola real también parpadea (`SIN RESOLVER`; la mejora queda para el
  futuro, decisión del dueño).

**Pendiente de etapa 8:**
- la europea (2 imágenes con cuña del cielo en 16:9: `SIN RESOLVER`);
- los puntos sueltos en menús del comparador (`SIN RESOLVER`).

## Pendiente

| Qué | Etapa / paso | Estado |
|---|---|---|
| Juego completo jugable de principio a fin, 1:1 | 6 / paso 5 | 🔄 funciona entero (misiones, finales, créditos, guardar/cargar; US y EU); falta DuckStation (al final) |
| Sprites que coloca la CPU (explosiones, humo, llamas de los motores) a 60 fps | 6 / paso 7 | ⏳ |
| GPU por GLES: alta resolución con la precisión de nuestra GTE y panorámico | 6 / paso 7 | ⏳ |
| Calibrar `BUDGET_PER_FRAME` (reloj del juego), sobre todo en NTSC | 6 | ⏳ |
| Documentar los botones "según el juego" de `INSTALAR.md` | 7 | ⏳ |
| APK reproducible en GitHub Actions y copia de seguridad de la clave de firma | 7 | ⏳ |
| `record.py` / `boot.py` (comparador con estados reales) y `names.tsv` para la americana | 6 | ⏳ menor |
| Interpolación de la europea | 6 / paso 7 | ⏸ aparcada por el dueño |
| Pasar a C legible las partes que se quieran modificar | 6 / paso 8 | opcional |

El contador de la app (arriba a la izquierda) da:
- `FPS`: imágenes distintas por segundo;
- `VEL`: velocidad del juego (100 % = la de la consola);
- `INT`: intermedias por paso;
- `CPU`: uso del hilo del juego.

L3+R3 activa o desactiva las intermedias, y `adb logcat -s AC3Game` da el
detalle por segundo.

Hecho en la etapa 6:
- paso 1, capa completa (152/152);
- paso 2, salida de vídeo;
- paso 3, recompilador (1.672/1.821 funciones validadas, 0 distintas);
- pasos 4-9 y APK universal: APK personal (1.0 arranque y misión; 1.1 intermedias; 1.2
  intermedias en hilos aparte y contador; 1.3 sin titileo en pasos sin
  parejas; 1.4 terreno lejano en giros rápidos; 1.5 radar, mapa y temblor
  del terreno; 1.6 universal EU + US);
- versión americana: perfil de direcciones (`tools/recomp/region.py`), 1.671/1.824
  funciones validadas y 0 distintas, nativa en el PC hasta la primera misión en
  vuelo (`make native-us`) y APK `-us` (sin probar en la Retroid).

Hecho en la etapa 5:
- inventario de API, oráculo MIPS con GTE y emulador de la GTE;
- **libgte completa** (25/26 idénticas al original);
- modelo de memoria PS1;
- **libgpu** (6 funciones idénticas + GPU por software, 75 pruebas) y
  terreno de la misión 6 dibujado con la capa (`make psx-scene`);
- **libcd/libetc/libpad**, validadas ejecutando el código del juego: packs
  abiertos, 127/127 registros por streaming idénticos y mando
  (`tools/psxcheck_io.py`);
- **SPU por software y libspu** (8 funciones idénticas al original) con el
  driver de sonido del juego tocando el banco común (`tools/psxcheck_snd.py`).
  De paso se corrigió la fórmula de frecuencia de los sonidos (afecta al APK
  0.7);
- **audio XA** idéntico a libav y música del juego sonando por la capa
  (`tools/psxcheck_xa.py`).

APK más reciente: **0.7** (efectos de sonido originales), pendiente de prueba
en la Retroid. Música: pendiente (qué pista XA suena en cada misión).

Ya localizado para el APK 0.4: explosiones, humo, restos y estelas de
proyectil (registro 0000); cielo por bandas y niebla (preludio); nubes,
sol/destello y bruma (s13 + s8).

1. ✅ **Tablas de aparición** (s6): aviones (LOD de s2) y objetos de tierra
   (s4) con bando, tipo, modelo y posición. Pendiente menor: unidad de la
   orientación; versión destruida de los objetos.
2. ✅ Objetos de tierra: salen de la tabla anterior.
3. ✅ **Superficies móviles**: estados discretos (alas, aerofreno, timón),
   `tools/acesurf.py`. Pendiente menor: qué control elige cada estado.
   ✅ Estelas de humo del misil y de puntas de ala (búferes circulares).
4. ✅ Ritmo de ticks: 25/s (EU, PAL) y 30/s (US, NTSC), `tools/acevideo.py`.
5. ✅ Superficies: interpolación según los mandos (docs/04). Para el APK:
   interpolar en vez de cambiar de estado.
6. Estelas: perfil de intensidad ✅; color y mezcla `SIN RESOLVER`
   (necesita emulador).
7. ✅ Lista de proyectil: el misil no usa las listas de disparos rectos
   (correcciones para el próximo APK: quitar la llama al misil; interpolar
   superficies; ritmo de 25/30 ticks; rumbos con 0x4000 = 360°).
8. ✅ Overlays 3-5 (créditos, reproductor de música, reproductor de prueba)
   y `h0` (descartado en render y colisión).
9. ✅ Demos: grabación de la cola de eventos (`tools/acedemo.py`, 16/16),
   con las instantáneas de entidad (posición y ángulos por fotograma).
10. ✅ Unidad de ángulos de entidad: 0x4000 = 360° (validada con las demos).
   Para el próximo APK: corregir rumbos (hoy usa 4096).

## Bloqueos conocidos

| Bloqueo | Impacto | Cómo se levanta |
|---|---|---|
| Semántica del `tag` en el SPB | Menor: la extracción ya funciona sin ella | Comparar con subcabeceras XA de cada entrada |

## Decisiones tomadas

- **Versión de trabajo: la europea** (`SCES_020.66`), por estar en español.
  El análisis es transferible; ver `docs/03-versiones.md` §2.
- **No construir todavía** renderer, runtime ni proyecto Android. Primero
  entender los datos.
- **No usar como referencia** los repos de "AC3 recomp" que aparecen en
  buscadores; ver `docs/03-versiones.md` §4.

## Sin empezar

Decompilación · capa de compatibilidad PsyQ · proyecto Android · renderer
