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
- ⚠️ El **modelo del avión del jugador no está localizado**: tampoco está en
  `ACE.BIN`. Candidatos: registros de tipo 1 grandes y ULZ variante 0
- ✅ **Etapa 3, primer corte** (`docs/09-codigo.md`): Ghidra 12.1.4 +
  `ghidra_psx_ldr` en `~/tools`. `ACE.BIN`: 1.853 funciones, 747 PsyQ,
  **≤ 1.106 propias (420 KB)**, 1.939 COP2. Mapa de módulos (no hay overlays
  de código; `ACE.EXE` = pantalla anticopia). Bucle principal en `main`
  `0x80025D08` — `make ghidra-import ghidra-stats`
- ✅ **Sistema de packs localizado en `ACE.BIN`**: apertura (`0x8002A9E4`),
  streaming (`0x8002A34C`), descompresor ULZ (`0x800AE118`). Con ellos:
  **tag del SPB resuelto** (72/72, 88/88) y **ULZ variante 0 resuelta**
  (604/604; tipo 13 = hangar: vistas y nombres de los 21 aviones)
- ✅ Formato de `ACE.BIN` confirmado contra el descompresor real de
  `SCES_020.66`; resueltos los campos `0x04`, `0x14` y `0x5C`
- ✅ **Etapa 2: `ACE.BIN` descomprimido** (EU 700.416 B, US 704.512 B),
  validado con la suma de control de la cabecera — `make acebin`

## Siguiente tarea concreta

**Etapa 3: seguir el consumo de las secciones del tipo 17 desde el código.**

```bash
make acebin ghidra-import      # si data/ghidra no existe
tools/ghidra/decomp.sh ace_eu.exe data/ghidra/x.c 0x80026e50
```

1. Partir de `FUN_80026E50`, que llama al descompresor ULZ (`0x800AE118`), y
   subir por quién la llama hasta encontrar el código que recorre la tabla de
   secciones de un registro de tipo 17.
2. Identificar qué función consume cada sección (modelos s01/s04, LOD
   s02/s03, mapa de teselas s00, textos s06) y nombrarlas.
3. Con eso, resolver campos `SIN RESOLVER`: `h0/h1/e0/e1` de primitivas, bits
   altos del mapa de teselas, campos de los bancos de sonido.
4. Dejar el nombrado reproducible: un script en `tools/ghidra/` que aplique
   nombres a direcciones desde un `.tsv` versionado (solo direcciones y
   nombres nuestros: no es código del juego).

Pendientes abiertos: el avión del jugador (último candidato: registros de
tipo 1 grandes); medición de la versión US; valorar `rea`.

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
