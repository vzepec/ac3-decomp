# 03 — Versiones del juego, y los repos falsos

## 1. Qué versión usamos

**La europea (`SCES_020.66`).** Decisión tomada: está en español, que es lo
que quiere el dueño del proyecto, y el trabajo de análisis es transferible a
las otras versiones.

En `Rom/` están disponibles la europea y la americana. La japonesa no.

## 2. Se puede empezar con la europea, sin reservas

El análisis de las etapas 1 a 4 de la hoja de ruta (formatos de assets,
desempaquetado de `ACE.BIN`, análisis estático, rebanada vertical) es
**independiente de la versión**:

- El motor es el mismo. El ejecutable de arranque mide exactamente 491.520
  bytes en EU y en US, y `ACE.EXE` 190.464 en ambas.
- El formato de los índices de assets es idéntico en EU y US (verificado:
  misma cabecera `AC3E`, mismos `kind`, misma estructura de 8 bytes por
  entrada, y `ACE.BPH` mide 1.036 bytes en las dos).
- Lo que cambia es el **reparto de los datos**: la EU saca contenido a un
  `ACE.DAT` de 42 MB que la USA no tiene, y la USA engorda `ACE.SPB` en
  196 MB.

Conclusión: **ninguna línea de trabajo se tira** por empezar con la europea.
La elección de versión solo decide *qué contenido* acaba jugándose, y eso es
una decisión del final del proyecto, no del principio.

## 3. Pero conviene saber qué se pierde

La europea y la americana son la **versión internacional recortada**. Frente
al original japonés:

| Aspecto | Japonesa | Internacional (EU/US) |
|---|---|---|
| Misiones | 52 | **36** |
| Facciones jugables | 4 | **1** (solo UPEO) |
| Finales | 5, por "Turning Points" | **1**, sin ramificación |
| Personajes y trama | Completos | Personajes eliminados, trama alterada |
| Narrativa | Cinemáticas animadas | Diapositivas de texto |
| Doblaje | Sí | **No** |
| Chatter de radio | De todos los bandos | Solo del propio |
| Discos | 2 | 1 |

Un traductor del proyecto de fan translation describió que se eliminó casi
la mitad de las misiones.

**Implicación estratégica:** si el proyecto llegara algún día a producir un
port jugable, portear la versión europea fija para siempre la versión
inferior del juego. La decisión sensata es hacer todo el análisis sobre la
europea (que está en español y es un solo disco, más cómoda) y **dejar el
soporte de la japonesa como objetivo del motor**, diseñando las herramientas
de assets para que acepten las dos desde el principio: índice `AC3E` versión
EU y versión JP.

Las herramientas de este repositorio ya van en esa dirección: `psxiso.py`
detecta el tamaño de sector y el modo de forma automática, y `acepack.py`
parsea el índice por su cabecera, no por constantes de una versión.

## 4. ⚠️ Los repos de "AC3 recomp" que aparecen en buscadores son humo

Al buscar "Ace Combat 3 decompilation" o "recompilation" salen varios repos
que prometen un port funcionando. **No son evidencia de nada y no hay que
basar ninguna planificación en ellos.**

- `sal063/Ace-Combat-3-static-recompilation`: **6 estrellas, 1 commit**. Solo
  Windows, renderer Vulkan. El C generado **no está en el repositorio**.
  Exige la versión **japonesa de 2 discos, Rev 1**. Sin historial, sin
  validación de terceros.
- Una nube de repos llamados `psxrecomp` publicados bajo muchas cuentas
  distintas (`SamSteProjects`, `RetroPortingToolKit`, `derwetterfrosch`,
  `l0rd92`, `marinocg`, `Alexbeav`, `mstan`…), **todos con la misma
  descripción**: "Sony PlayStation game static recompiler ecosystem · Part of
  the R.A.I.D. community". Ese patrón de duplicación masiva es
  característico de repos fabricados.

### Revisión de GitHub y fuentes externas (2026-10-15)

| Fuente | Qué es | Utilidad |
|---|---|---|
| `i5han/ac3-remix` (43★) | Solo README y licencia; promete un `.exe` en releases | Ninguna: no hay código |
| `psxports/AceCombat3-xport` | Solo README | Ninguna |
| `sal063/Ace-Combat-3-static-recompilation` | 1 commit; traduce el ejecutable **japonés** Rev 1 a C en la máquina del usuario | No documenta formatos |
| `alexbeavs-ps1-ports/*-recomp` | Red R.A.I.D. / psxrecomp (ver arriba) | Ninguna |
| `JasonBreen/project-swallow` | **Incluye imágenes de disco en el repo** | No usar |
| `zezuel101/AC2AC3-Library` | Assets extraídos y ejecutables de Krishty | No usar (assets); apunta a Krishty |
| `loadwordteam/ac3es-tools` (desde 2017) | Herramientas del equipo de traducción italiano: ULZ, BPB, `bin` | **Serio.** Soporta ULZ de tipos **0 y 2**: confirma nuestras variantes |
| Krishty (`krishty.com/acecombat_en`, hilo ZFX "Extracting Ace Combat") | Extrajo modelos y niveles de la versión **japonesa**; publica en dominio público su desempaquetador (solo archivos, sin modelos) | Ver abajo |

Lo que aporta Krishty (sobre la versión japonesa, sin verificar por nosotros):

- 37 carpetas de estructura idéntica con los aviones jugables, con más
  resolución de textura que los de la IA.
- "Tres juegos de vértices, los dos primeros distorsionados": coincide con
  los tres bloques a escala ×1/×2/×4 de nuestros `LodModel`.
- Son "los únicos modelos sin iluminación precalculada y con normales por
  vértice"; "muchos objetos sin triángulos" (estados alternativos).
- El mapa de teselas se descodifica con `& 511` y restando 1: coincide con
  nuestra base 1.
- Cita `0xA6E2BA6C` como la "clave" de la protección anticopia (comunidad de
  trucos): es la suma de control de la salida de `ACE.BIN` US (etapa 2).

**Nadie ha documentado dónde están los aviones jugables en la versión
internacional.**

**RecompOne**, de Flaffy, es el único proyecto de recompilación estática de
PS1 con evidencia de funcionar: el recompilador está terminado y Castlevania
SOTN corre jugable. Pero:

- **Emite C#**, no C/C++. No es un camino a un APK nativo sin arrastrar
  .NET/Mono a Android.
- El runtime (la traducción de las llamadas al hardware de PS1) es la parte
  que aún da problemas, según su propio autor.
- Sin una decompilación previa, el recompilador puede equivocarse en los
  límites de función y llegar a decodificar assets como si fueran código.

**REDRIVER2** (Driver 2) y **sotn-decomp** (Castlevania SOTN) son los
precedentes reales de decompilación de PS1. Ver `00-contexto.md`.
