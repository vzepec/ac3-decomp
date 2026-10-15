# STATUS — estado actual del proyecto

**Última actualización:** 2026-10-15 (rama `etapa-1`)

> Arranque en frío: `make brief`. Contrato: `CLAUDE.md` / `AGENTS.md`.
> Flujo desde terminal: `docs/08-continuar-por-consola.md`.

## Etapa actual

**Etapa 1 — Arqueología de assets: criterio de salida CUMPLIDO** (rama
`etapa-1`). Quedan tareas de cierre antes de pasar a la etapa 2. Ver
`docs/05-hoja-de-ruta.md`.

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

## Siguiente tarea concreta

**Descifrar el formato de la sección 2 del tipo 4 (aviones del jugador)**,
que comparte formato con la sección 15 del tipo 17. Es el candidato a modelo
jerárquico de avión, que es lo que necesita la rebanada vertical de la etapa 4.

```bash
make extract-bpb
python3 tools/acerec.py dump data/bpb/0047_*_t04.bin data/sec
xxd data/sec/0047_tage000206c_t04_s02.bin | head -40
```

Pistas (sin verificar):

1. Empieza con `u32` cuyo byte alto es 0 y valores tipo `0x043C90` que caen
   en el rango de `ACE.BIN` (`0x10000`-`0xBB000`) y **cambian entre EU y US**:
   probablemente punteros a funciones del ejecutable.
2. Aparece repetida la palabra `ff 80 c9 5f`; buscar el periodo de registro.
3. Comprobar si reutiliza el formato de modelo de `tools/acemodel.py`
   (cabecera `u16 0, u16 extent, u32 0x24…`) en algún nivel.

Tareas de cierre de la etapa 1, en paralelo o después:

- Campos `h0`/`h1`/`e0`/`e1` de las primitivas y bits altos del mapa de teselas
- Exportar `.obj` con UV + atlas de textura (las CLUT/tpage ya están decodificadas)
- Secciones 2, 3, 5, 7-9 del tipo 17 y el registro de tipo 13
- `ACE.SPB`: identificar vídeo (STR/MDEC) y audio (XA) por subcabecera de sector

## Bloqueos conocidos

| Bloqueo | Impacto | Cómo se levanta |
|---|---|---|
| `ACE.BIN` comprimido | **No se puede analizar ni medir el código del juego** | Etapa 2: volcado de RAM desde emulador, o reimplementar el descompresor |
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
