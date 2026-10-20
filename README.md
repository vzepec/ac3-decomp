# Ace Combat 3: Electrosphere — ingeniería inversa y port a Android

Proyecto de ingeniería inversa de **Ace Combat 3: Electrosphere** (PS1,
Namco, 1999), con el objetivo a largo plazo de un port nativo a Android con
controles físicos. Desarrollo desde Linux.

## Progreso

![Progreso de la recompilación](docs/img/progreso.svg)

La barra verde mide el **código del juego validado** en la versión americana
(la de referencia): las 1.824 funciones propias del residente y los 5 overlays.
Una función cuenta si el intérprete que lleva el APK da exactamente lo mismo
que el intérprete de referencia, con estados aleatorios o con estados de
partidas reales, y nunca algo distinto. Las demás barras:
- funciones que se ejecutan en las 41 misiones;
- capa PsyQ;
- pasos de la etapa 6;
- recorrido: lo que funciona y lo comparado con DuckStation.

Las pruebas están registradas en [`docs/pruebas/`](docs/pruebas/README.md). Se
regenera con `tools/progress.py version us <cobertura.json>` y
`tools/progress.py svg`. Plan en
[`docs/11-etapa6.md`](docs/11-etapa6.md).

**Estado:** etapas 1-5 cerradas (formatos, código, rebanada de Android y capa
de compatibilidad PsyQ en `psx/`). **Etapa 6 en curso:** el juego nativo por
recompilación estática. **Compatible con los discos europeo (SCES-02066) y
americano (SLUS-00972):** las dos versiones se recompilan, arrancan en el PC
hasta la primera misión y van en un APK personal universal para Android, con imágenes
intermedias a 60 fps en la misión. El código recompilado y los APK se generan
desde el disco del usuario y **no se distribuyen** (llevan código de Namco).

## Probar la alfa en Android

**[INSTALAR.md](INSTALAR.md)** explica todo lo necesario: requisitos, instalación
y la tabla de controles. Basta con descargar `AC3Recomp-<versión>.apk` de las
releases y tener tu propio disco (europeo o americano).

El APK **solo lleva código de este repositorio**: lee el juego del disco del
usuario y lo interpreta, como un emulador (etapa 7, `docs/12-etapa7.md`). La
herramienta `tools/apk_audit.py` comprueba que no contiene bytes del juego.

### Compilar el APK

```bash
sh android_game/build.sh   # APK publicable -> data/android/AC3Recomp-<version>.apk (sin necesitar el juego)
make libre                 # el mismo juego en el PC, desde la imagen del disco (DISCO=...)
```

Las variantes personales de la etapa 6 llevan código del juego traducido y no se
distribuyen. Se generan con `REGION=all|eu|us android_game/build.sh`, después de
`make native`.bash
make native                          # europea: recompila y prueba en el PC
make us-prep ghidra-us native-us     # americana
sh android_game/build.sh             # APK universal (los dos discos) -> data/android/ac3game-<version>.apk
```

- 👉 **Agentes de IA: empezad por [`CLAUDE.md`](CLAUDE.md)** (bootstrap) y
  luego [`AGENTS.md`](AGENTS.md) (contrato completo).
- 👉 Estado y siguiente tarea: [`STATUS.md`](STATUS.md)
- 👉 Retomar desde una terminal:
  [`docs/08-continuar-por-consola.md`](docs/08-continuar-por-consola.md)
- 👉 Por qué el objetivo no es "el juego jugable el mes que viene":
  [`docs/05-hoja-de-ruta.md`](docs/05-hoja-de-ruta.md)

## Requisitos

Python 3 estándar. Nada más para las herramientas actuales.

## Uso rápido

```bash
make brief        # estado del proyecto + comprobacion de entorno
make index        # cabeceras de los indices de assets
make inventory    # inventario de los 199 recursos del disco europeo
make records      # tipos de registro y tablas de seccion
make info         # cabeceras PS-EXE de los ejecutables del juego
make files        # tabla de archivos de ambos discos
```

Las imágenes de disco van en `Rom/` y **no se distribuyen**: hay que usar
una copia propia del juego. Ver [`docs/06-legal.md`](docs/06-legal.md).

## Qué hay resuelto

| Área | Estado |
|---|---|
| **Discos y formatos** (etapas 1-2) | |
| Estructura de los discos europeo y americano | ✅ documentada (`docs/01-disco.md`) |
| Índices `ACE.SPH` / `ACE.BPH` y los 199 recursos | ✅ 100 % de cobertura, extracción completa |
| Compresión ULZ de Namco | ✅ 659/659 secciones |
| Texturas, modelos (con LOD), terreno, sonidos del SPU, audio XA y vídeo | ✅ convertibles (`docs/04-formatos.md`) |
| `ACE.BIN` (el módulo principal, comprimido) | ✅ descomprimido y validado con sus sumas, en las dos versiones |
| **Código** (etapas 3 y 5) | |
| Análisis estático con Ghidra: residente y 5 overlays, librerías PsyQ marcadas | ✅ (`docs/09-codigo.md`) |
| Capa de compatibilidad PsyQ (`psx/`): GTE, GPU, CD, SPU, mando, tarjeta… | ✅ 152/152 funciones de Sony que usa el juego |
| **El juego nativo** (etapa 6) | |
| Recompilación estática MIPS → C, validada función a función | ✅ 0 funciones distintas del original |
| Intérprete MIPS propio, validado frente al de referencia | ✅ 95,9 % (US) / 96,3 % (EU) del código validado, 0 distintas |
| Versiones europea (`SCES-02066`) y americana (`SLUS-00972`) | ✅ las dos; la americana es la de referencia |
| Las 41 misiones | ✅ cargan y se juegan sin fallos (`docs/pruebas/`) |
| Secuencias de final y créditos | ✅ se reproducen enteras |
| Guardar y cargar en la tarjeta de memoria | ✅ partida válida, se carga |
| 60 fps en vuelo (imágenes intermedias) | ✅ el mundo se redibuja con el código del juego; dibujo por la GPU, 60 fps estables en ciudad (release `v1.10-alfa`) |
| Comparación 1:1 con DuckStation | ⏳ pendiente (al final) |
| Pantalla ancha (16:9) | ✅ release `v1.8-alfa` |
| Alta resolución (GPU por GLES) | ⏳ en curso (paso 7) |
| **Android** (etapa 7) | |
| APK publicable sin nada de Namco (`AC3Recomp-<versión>.apk`) | ✅ release `v1.12-alfa` (alta resolución y filtro bilineal); lee el juego del disco del usuario |
| Probado en una Retroid Pocket Flip 2 | ✅ velocidad completa (`VEL` 100 %) |
