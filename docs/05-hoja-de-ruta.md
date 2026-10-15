# 05 — Hoja de ruta

## Premisa

El objetivo final (APK nativo jugable) **no es alcanzable por una persona en
un plazo normal**. Los precedentes de PS1 lo demuestran: REDRIVER2 lleva unos
6 años y partió con símbolos de depuración; sotn-decomp lleva unos 5 con
muchos colaboradores y sigue incompleto.

Esto no invalida el proyecto. Lo que invalida un proyecto así es organizarlo
como una carrera hacia "el juego funcionando", porque entonces a los tres
meses no hay nada que mostrar y se abandona.

**Regla de diseño de esta hoja de ruta:** cada etapa produce algo útil y
publicable por sí sola, y cada etapa tiene un criterio de salida explícito y
un punto de abandono honesto.

---

## Etapa 0 — Decidir la versión ✅ HECHO

Decisión: trabajar sobre la **europea** (español, un disco). Diseñar las
herramientas para que acepten también la americana y, en el futuro, la
japonesa. Ver `03-versiones.md`.

---

## Etapa 1 — Arqueología de assets ✅ CRITERIO DE SALIDA CUMPLIDO

**Objetivo:** entender y documentar los contenedores de datos del disco.

| Tarea | Estado |
|---|---|
| Lectura de imágenes `MODE2/2352` y tabla ISO9660 | ✅ `tools/psxiso.py` |
| Formato de los índices `ACE.SPH` / `ACE.BPH` | ✅ resuelto, 100 % de cobertura |
| Extracción de los 199 recursos | ✅ `make extract-bpb` |
| Semántica del campo `tag` | ✅ BPB (tamaño en `u32`) · ⬜ SPB |
| Compresión de las secciones (ULZ) | ✅ `tools/ulz.py`, 659/659 |
| Localizar y convertir TIM a PNG | ✅ `tools/tim2png.py` |
| Vértices y caras de modelos del tipo 17 | ✅ `tools/acemodel.py`, 14.095/14.095 |
| Mapa de teselas del terreno | ✅ índice de 9 bits, base 1 |
| Contenido de `ACE.DAT` (42 MB) | ✅ relleno, `tools/acedat.py` |
| Modelos de avión (tipo 4 s2 / tipo 17 s15) | ⬜ siguiente |
| `ACE.SPB` (streaming: vídeo/audio) | ⬜ |

**Criterio de salida:** poder extraer de `ACE.BPB` una textura y verla como
PNG, y un modelo y listar sus vértices.

**Valor si el proyecto se detiene aquí:** una especificación de formatos y un
conjunto de herramientas que sirven a cualquier otro proyecto de AC3
(traducciones, mods, museo). Legal y publicable. Esto ya es una contribución
real.

**Esfuerzo:** semanas. **Riesgo:** bajo.

---

## Etapa 2 — Desempaquetar `ACE.BIN`

**Objetivo:** obtener los 700.416 bytes del módulo principal en claro.

Dos vías, en este orden:

1. Volcado de RAM `0x80010000`..`0x800BB000` desde un emulador con depurador
   (PCSX-Redux, DuckStation, no$psx) tras la carga. Rápido, desbloquea el
   análisis.
2. Localizar el descompresor en `SCES_020.66` (que está en claro) y
   reimplementarlo como herramienta. Limpio y reproducible.

**Criterio de salida:** un blob con densidad de prólogos MIPS comparable a
`SCES_020.66` (~0,4 por KiB). `tools/psexe.py` ya mide eso.

**Por qué es crítico:** hasta aquí **no se puede ni estimar** el tamaño real
del trabajo de código. Esta etapa es la que convierte el proyecto de
especulación en planificable.

**Esfuerzo:** días para la vía 1, semanas para la vía 2. **Riesgo:** bajo.

---

## Etapa 3 — Análisis estático del código

**Prerrequisito:** instalar **Ghidra** (no está en la máquina) con el loader
de PSX y soporte COP2/GTE.

| Tarea | Por qué |
|---|---|
| Matching de firmas de PsyQ | Marca de golpe todo el código de librería de Sony y deja solo el de Namco |
| Mapa de memoria en ejecución | Los bancos traen punteros a `0x800C_xxxx` ya resueltos: regalan el layout |
| Identificar el bucle principal y el gestor de overlays | Es la columna vertebral del motor |
| Contar funciones propias reales | La primera estimación honesta del trabajo |

Sobre **`rea`** (`rea-agents`, npm v6.5.0, repo `morluto/rea`): existe de
verdad y es útil como capa de CLI/MCP **sobre** Ghidra/IDA/Hopper para el
ciclo "explícame esta función". **No genera un port compilable** y necesita
Ghidra instalado para binarios nativos. Es una ayuda al análisis, no un
atajo al objetivo.

**Criterio de salida:** un documento con el número de funciones propias, el
mapa de overlays y el bucle principal identificado.

**Esfuerzo:** meses. **Riesgo:** medio.

---

## Etapa 4 — Rebanada vertical en Android ⬅️ LA PRUEBA DE LA VERDAD

**Objetivo:** una cosa, pequeña y nativa, corriendo en el Flip 2:

- leer los archivos de assets **reales** del disco del usuario
- dibujar **un** modelo de avión con un renderer GLES propio
- mover la cámara con el **gamepad físico** del Flip 2
- empaquetado como APK, compilado desde Linux con el NDK, con segmentos
  alineados a 16 KB

Nada de juego: ni vuelo, ni IA, ni HUD, ni misiones.

**Para qué sirve:** es la **medición**. Si esta rebanada cuesta 3 meses, el
juego completo en solitario son más de 10 años, y conviene saberlo entonces y
no al cuarto año. Si cuesta 3 semanas, el proyecto merece seguir.

**Criterio de salida:** el APK instala y corre en el dispositivo.

**Esfuerzo:** meses. **Riesgo:** el que decide si hay etapa 5.

---

## Etapa 5 — Capa de compatibilidad PsyQ

Reimplementar `libgpu` sobre GLES/Vulkan, `libgte` en punto fijo exacto,
`libspu` sobre el audio de Android, `libcd` sobre lectura de archivos. Es el
equivalente de Psy-X. Es el verdadero motor del port.

**Solo se entra aquí si la etapa 4 salió bien.**

---

## Etapa 6 — El juego

Decompilación o recompilación del módulo principal, módulo a módulo. Es la
etapa de los años. Realista solo con colaboradores.

---

## Mientras tanto

**DuckStation en el Flip 2** ya ejecuta Ace Combat 3 a velocidad plena, sin
nada de ingeniería inversa. Ese es el listón: el port solo tiene sentido si
algún día lo supera (resolución nativa, 16:9, 60 fps, controles mejores). Es
sano tenerlo instalado y presente como referencia, no como competencia.
