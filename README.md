# Ace Combat 3: Electrosphere — ingeniería inversa y port a Android

Proyecto de ingeniería inversa de **Ace Combat 3: Electrosphere** (PS1,
Namco, 1999), con el objetivo a largo plazo de un port nativo a Android con
controles físicos. Desarrollo desde Linux.

## Progreso

![Progreso de la recompilación](docs/img/progreso.svg)

La barra verde mide el juego **recompilado a C y validado** contra el código
original: las 1.821 funciones propias del juego, del residente y los 5 overlays.
Se regenera con `make progress`. Plan en
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
| Estructura de los discos EU y US | ✅ documentada |
| Cabeceras PS-EXE de los 3 binarios | ✅ documentada |
| Confirmación de librerías PsyQ de stock | ✅ |
| Formato de índices `ACE.SPH` / `ACE.BPH` | ✅ resuelto, 100 % de cobertura |
| Extracción de los 199 recursos | ✅ funciona |
| Semántica del campo `tag` de cada entrada | ⚠️ sin resolver |
| Formatos internos de cada recurso | ⚠️ sin empezar |
| Desempaquetado de `ACE.BIN` | ⚠️ sin empezar — bloquea el análisis del código |
| Decompilación | ⬜ no empezada |
| Port Android | ⬜ no empezado |
