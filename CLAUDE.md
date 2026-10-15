# CLAUDE.md — arranque en frío

> Claude Code y otros agentes cargan este archivo automáticamente al abrir
> una sesión en esta carpeta. Es un **bootstrap**: lo mínimo para no meter la
> pata en los primeros dos minutos. El contrato completo está en
> [`AGENTS.md`](AGENTS.md).

## Qué es esto

Ingeniería inversa de **Ace Combat 3: Electrosphere** (PlayStation 1, Namco,
1999) con el objetivo a largo plazo de un **port nativo a Android** (APK con
controles físicos, destino: Retroid Pocket Flip 2). Desarrollo desde Linux.

Idioma del proyecto: **español** en documentación y comentarios. Sin tildes
dentro de los `.py`. Identificadores de código en inglés.

## Lo primero que tienes que hacer

```bash
make brief          # estado actual + comprobacion de que todo corre
cat STATUS.md       # siguiente tarea concreta
```

**No empieces a trabajar sin leer `STATUS.md`.** Dice exactamente en qué
etapa está el proyecto y cuál es la siguiente tarea. Hay cosas ya probadas
que **no** hay que repetir.

## Las cinco reglas que no se rompen

1. **Nunca versiones ROMs ni datos del juego.** `Rom/` y `data/` están en
   `.gitignore`. Si un archivo salió del disco, no entra en git. Punto.
2. **No inventes hallazgos.** Todo lo que afirme un documento debe ser
   reproducible con una herramienta de `tools/`. Marca las hipótesis como
   `SIN RESOLVER`.
3. **Valida todo heurístico contra un caso positivo conocido** antes de
   concluir nada con él. Ya nos costó dos errores: ver en `notes/bitacora.md`
   las entradas del heurístico de prólogos MIPS y de los TIM "inexistentes".
4. **La etapa 4 (rebanada vertical en Android) está abierta** por petición
   del dueño del proyecto: `android/`. No la conviertas en un port del juego:
   es una medición. Ver `docs/05-hoja-de-ruta.md`.
   Ramas: una por etapa (`etapa-N`), integradas en `develop`; `main` es la base.
5. **No borres archivos del usuario.** Si algo sobra, muévelo a `_to_delete/`
   y avísale.

## El objetivo es duro, y el proyecto lo asume

Un port nativo jugable **no** es alcanzable a corto plazo por una persona.
Los precedentes de PS1 (REDRIVER2, sotn-decomp) llevan 5-6 años, en equipo, y
partieron con ventajas que este disco no tiene. No prometas un juego jugable.
La hoja de ruta está dividida en etapas que valen por sí solas.

## Dónde está cada cosa

| Necesitas | Lee |
|---|---|
| El contrato completo de trabajo | `AGENTS.md` |
| Estado y siguiente tarea | `STATUS.md` |
| Continuar desde una terminal | `docs/08-continuar-por-consola.md` |
| Qué hay en los discos | `docs/01-disco.md` |
| Los binarios y el blob comprimido | `docs/02-binarios.md` |
| Qué versión usamos y por qué | `docs/03-versiones.md` |
| **Formato de los assets (resuelto)** | `docs/04-formatos.md` |
| Plan por etapas | `docs/05-hoja-de-ruta.md` |
| Qué se puede publicar | `docs/06-legal.md` |
| Vocabulario de PS1 | `docs/07-glosario.md` |
| Análisis del código (Ghidra) | `docs/09-codigo.md` |
| Historial de hallazgos | `notes/bitacora.md` |

## Al terminar cualquier trabajo

1. La herramienta que lo demuestra, en `tools/`
2. El hallazgo en `notes/bitacora.md`, con cómo reproducirlo
3. El documento de `docs/` correspondiente, actualizado
4. `STATUS.md`, con la siguiente tarea
