# STATUS — estado actual del proyecto

**Última actualización:** 2026-10-16 (etapa 5 cerrada e integrada en `develop`)

> Arranque en frío: `make brief`. Contrato: `CLAUDE.md` / `AGENTS.md`.
> Flujo desde terminal: `docs/08-continuar-por-consola.md`.

## Etapa actual

**Etapas 1-5 cerradas.** La etapa 5 dejó la capa de compatibilidad PsyQ en
`psx/` (resultados en `docs/10-etapa5.md`). La etapa 6 (recompilar el juego)
**no está abierta**: la decide el dueño del proyecto. Mientras, la rebanada de
Android sigue como medición: **APK 0.8** (música XA y afinación corregida).

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

## Siguiente tarea concreta

1. **Probar el APK 0.8 en la Retroid** (`~/Escritorio/ac3slice-0.8.apk`):
   música XA de cada misión y afinación corregida de los efectos.
2. Pendiente de la rebanada: la pista que elige el guion en partida
   (`FUN_800550F0`, SIN RESOLVER); ahora suena la de las demostraciones.
3. Decisión del dueño sobre la etapa 6. Antes de recompilar habría que
   llevar la VRAM de la GPU por software a GLES en Android y sustituir las
   aproximaciones marcadas SIN HACER en `psx/`.

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
