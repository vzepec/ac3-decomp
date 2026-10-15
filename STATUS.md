# STATUS — estado actual del proyecto

**Última actualización:** 2026-10-15

> Arranque en frío: `make brief`. Contrato: `CLAUDE.md` / `AGENTS.md`.
> Flujo desde terminal: `docs/08-continuar-por-consola.md`.

## Etapa actual

**Etapa 1 — Arqueología de assets** (ver `docs/05-hoja-de-ruta.md`)

## Hecho

- ✅ Estructura de los discos europeo y americano documentada
  (`docs/01-disco.md`). Herramienta: `tools/psxiso.py`
- ✅ Cabeceras de los tres ejecutables analizadas (`docs/02-binarios.md`).
  Herramienta: `tools/psexe.py`
- ✅ Confirmado que el juego usa **librerías PsyQ de stock** (IDs RCS de
  `sys.c`, `bios.c`, `intr.c`) → esas funciones se reimplementan, no se
  decompilan
- ✅ Confirmado que `ACE.BIN` (el módulo principal) está **comprimido**:
  entropía 7,42 y 0,01 prólogos MIPS por KiB frente a 0,47 en el módulo en
  claro. Cabecera propietaria de Namco descifrada en sus campos principales
- ✅ **Formato de los índices de assets resuelto** (`docs/04-formatos.md`),
  validado en las 4 combinaciones EU/US × SPH/BPH, con **100 % de cobertura**
  de los dos packs. Herramienta: `tools/acepack.py`
- ✅ Inventario de los 199 recursos, con tipos identificados: tablas de
  memory card, nombres de BGM, tabla de textos multiidioma, y los bloques de
  recursos de misión
- ✅ **Cabecera común de registro resuelta** (`type`, `header_size`, tabla de
  offsets de sección), verificada en **122 de 127** registros de `ACE.BPB`.
  Distribución: tipo 1 ×18, tipo 4 ×22, tipo 5 ×40, tipo 13 ×1, tipo 17 ×41
- ✅ Herramientas **validadas también contra el disco americano**: mismo
  formato de índice y de registro, lo que confirma que son del motor y no de
  una versión concreta

## Siguiente tarea concreta

**Clasificar las 16 secciones de un registro de tipo 17** (41 registros de
~600-765 KB: el volumen real del pack).

```bash
make records            # tipos y tablas de seccion
make extract-bpb        # deja las 127 entradas en data/bpb/
```

Luego, sobre un `*_t17.bin`:

1. Para cada una de las 16 secciones, imprimir tamaño y primeros 64 bytes, y
   clasificarla. Se buscan: bloques de píxeles para VRAM, CLUTs (16 o 256
   entradas de 16 bits), y geometría (tripletes de `s16`).
2. Comparar la **misma entrada** entre el disco europeo y el americano: lo que
   cambie es dato localizado; lo que no cambie es geometría o código.
3. Escribir una herramienta nueva `tools/tim2png.py` (nombre provisional: aún
   no existe, hay que crearla; ajústalo al formato real que encuentres) y
   volcar la primera textura a PNG. **Ese es el criterio de salida de la etapa 1.**

> ⚠️ **No buscar cabeceras TIM otra vez.** Ya se hizo: cero coincidencias en
> registros de tipo 17 y 4. Las texturas no están como TIM suelto. Ver
> `docs/04-formatos.md`.

## Bloqueos conocidos

| Bloqueo | Impacto | Cómo se levanta |
|---|---|---|
| `ACE.BIN` comprimido | **No se puede analizar ni medir el código del juego** | Etapa 2: volcado de RAM desde emulador, o reimplementar el descompresor |
| Ghidra no instalado | Etapa 3 no puede empezar | Instalarlo con el loader de PSX y soporte COP2/GTE |
| Semántica del campo `tag` del índice | Menor: la extracción ya funciona sin ella | Comparar tags entre EU y US para la misma entrada |

## Decisiones tomadas

- **Versión de trabajo: la europea** (`SCES_020.66`), por estar en español.
  El análisis es transferible; ver `docs/03-versiones.md` §2.
- **No construir todavía** renderer, runtime ni proyecto Android. Primero
  entender los datos.
- **No usar como referencia** los repos de "AC3 recomp" que aparecen en
  buscadores; ver `docs/03-versiones.md` §4.

## Sin empezar

Decompilación · capa de compatibilidad PsyQ · proyecto Android · renderer
