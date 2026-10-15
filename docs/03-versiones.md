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

### Lo que sí existe

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
