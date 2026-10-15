# STATUS — estado actual del proyecto

**Última actualización:** 2026-10-15 (rama `etapa-2`; integración en `develop`)

> Arranque en frío: `make brief`. Contrato: `CLAUDE.md` / `AGENTS.md`.
> Flujo desde terminal: `docs/08-continuar-por-consola.md`.

## Etapa actual

**Etapa 2 — Desempaquetar `ACE.BIN`: HECHA** (rama `etapa-2`). La etapa 1
está fusionada en `develop`, que es la rama de integración de etapas. La
siguiente es la **etapa 3 — análisis estático**. Ver `docs/05-hoja-de-ruta.md`.

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
- ✅ **Etapa 2: `ACE.BIN` descomprimido** (EU 700.416 B, US 704.512 B),
  validado con la suma de control de la cabecera — `make acebin`

## Siguiente tarea concreta

**Etapa 3: preparar el análisis estático de `ACE.BIN`.**

1. Instalar **Ghidra** (necesita JDK 17+) y la extensión de PSX
   (`ghidra_psx_ldr`, con soporte de COP2/GTE). Dejar en `docs/` la versión
   exacta y cómo instalarla.
2. Cargar `data/exe/ace_eu.exe` (generado con `make acebin`; es un PS-X EXE
   estándar con carga en `0x80010000` y entrada en `0x8002521C`).
3. Primera medición, sin entrar todavía en las funciones:
   - número de funciones que detecta Ghidra
   - matching de firmas PsyQ: cuántas son de librería y cuántas de Namco
   - instrucciones COP2 (GTE) en `ACE.BIN`, con la misma métrica que
     `docs/02-binarios.md` usa para los otros dos ejecutables
4. Valorar `rea` (`morluto/rea`) como capa sobre Ghidra para el ciclo de
   "explícame esta función". No sustituye a Ghidra.

Criterio de salida de la etapa 3 (hoja de ruta): número de funciones propias,
mapa de overlays y bucle principal identificado.

Pendientes que siguen abiertos de etapas anteriores: el avión del jugador;
ULZ variante 0; campos `0x04` y `0x5C` de la cabecera de `ACE.BIN`; campos
sin resolver de primitivas, mapa de teselas y bancos de sonido.

## Bloqueos conocidos

| Bloqueo | Impacto | Cómo se levanta |
|---|---|---|
| Ghidra no instalado | Etapa 3 no puede empezar | Instalarlo con el loader de PSX y soporte COP2/GTE |
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
