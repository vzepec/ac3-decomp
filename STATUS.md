# STATUS — estado actual del proyecto

**Última actualización:** 2026-10-15 (rama `etapa-3`; integración en `develop`)

> Arranque en frío: `make brief`. Contrato: `CLAUDE.md` / `AGENTS.md`.
> Flujo desde terminal: `docs/08-continuar-por-consola.md`.

## Etapa actual

**Etapa 3 — Análisis estático: EN CURSO** (rama `etapa-3`). Las etapas 1 y 2
están fusionadas en `develop`. Primer corte de los tres criterios de salida
cumplido (ver `docs/09-codigo.md`); queda nombrar el árbol de llamadas.

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
- ⚠️ El **modelo del avión del jugador no está localizado**. Descartado por
  análisis estático en todos los registros y en `ACE.BIN`; los huecos 0-7 de
  la tabla de modelos LOD se rellenan en ejecución. Los registros de tipo 1
  grandes resultaron ser las **demos**
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

**Decisión pendiente del usuario:** el siguiente paso útil es dinámico.

**A (recomendada). Análisis dinámico para el avión del jugador.** Instalar un
emulador con depurador (PCSX-Redux tiene volcado de RAM y depurador MIPS en
Linux), jugar hasta una misión con vista exterior y volcar la RAM:

1. buscar en el volcado cabeceras `LodModel` (`u16 0, u16 extent, u32 0x1C…`
   recolocadas: los offsets ya son punteros `0x80…`) y la tabla de modelos
   LOD (huecos 0-7)
2. comparar sus bytes con los registros del BPB para saber de dónde salen
3. regla: los volcados de RAM son datos del juego → solo en `data/`

**B. Seguir con análisis estático:** `h0` de las primitivas, el formato de las
demos (registros 111-126), el bit exacto de cada vecino en el mapa de
teselas, y nombrar en Ghidra las funciones ya identificadas con un script
reproducible desde un `.tsv` versionado.

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
