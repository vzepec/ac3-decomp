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
- ✅ **Bancos de sonido del SPU** (tipo 4 s2, tipo 17 s15, tipo 13 s8): 63/63
  por disco, ADPCM a WAV — `make sounds`. Corrige la hipótesis de "punteros a
  código": eran direcciones de la RAM del SPU
- ✅ **Aviones de misión con LOD** (tipo 17 s02/s03), vértices ×1/×2/×4 y
  POLY_GT3/GT4: 578/578 — `make models`
- ✅ `ACE.SPB` = audio XA + vídeo, por subcabeceras de sector — `acepack.py xa`
- ⚠️ El **modelo del avión del jugador no está localizado** (ver abajo)

## Siguiente tarea concreta

La etapa 1 tiene su criterio de salida cumplido y la rebanada vertical (etapa
4) ya tiene un avión que dibujar: el modelo 8 de la sección 2 del registro
0006. Dos opciones; **la recomendada es la A**:

**A. Empezar la etapa 2: descomprimir `ACE.BIN` de forma estática.** Pista
nueva (sin verificar): el campo `0x14` de la cabecera (`0x5C`) es el offset
de los datos comprimidos, y en ellos se lee `"Can't" FF " ope"`, es decir,
literales en claro con un byte de flags `0xFF` intercalado: patrón de un LZ con
flags de 8 bits. Un LZSS clásico (Okumura, ventana de 4 KB) **no** lo
descomprime bien (consume solo ~206 KB de 413 KB): es una variante.

```bash
python3 -c "import sys; sys.path.insert(0,'tools'); from psxiso import Disc; \
b=Disc('Rom/Ace Combat 3 - Electrosphere (Europe) (EnFrDeEsIt)/Ace Combat 3 - Electrosphere (Europe) (En,Fr,De,Es,It).bin').read_file('ACE.BIN'); \
open('data/ace_bin.raw','wb').write(b)"
xxd -s 0x5c data/ace_bin.raw | head -40
```

Criterio de validación: la salida debe medir exactamente 700.416 bytes,
consumir exactamente los 413.772 − 0x5C de entrada, y dar una densidad de
prólogos MIPS comparable a `SCES_020.66` (`tools/psexe.py` la mide). Si el
formato no sale por análisis, buscar el descompresor en `SCES_020.66` (que
está en claro) o volcar la RAM con un emulador.

**B. Seguir buscando el modelo del avión del jugador.** Descartados: tipo 4,
tipo 17 s02/s03, SPB. Candidatos: los 16 registros de tipo 1 (0111-0126,
formato desconocido) y las entradas ULZ de variante 0 del tipo 13. Si no
aparece en los datos, puede estar dentro de `ACE.BIN`, con lo que A lo
desbloquea también.

Pendientes menores de la etapa 1: ULZ variante 0; campos `h0/h1/e0/e1` de las
primitivas; bits altos del mapa de teselas; campos sin resolver de los bancos
de sonido; frecuencia de muestreo de las muestras; `.obj` con UV y atlas.

## Bloqueos conocidos

| Bloqueo | Impacto | Cómo se levanta |
|---|---|---|
| `ACE.BIN` comprimido | **No se puede analizar ni medir el código del juego** | Etapa 2: hay pista de formato (ver arriba); si no, volcado de RAM o descompresor de `SCES_020.66` |
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
