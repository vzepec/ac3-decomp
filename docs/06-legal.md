# 06 — Qué se puede publicar y qué no

No es asesoría legal; es la práctica establecida en la escena de
decompilación, que es la que sigue este proyecto.

## Se puede

- **Código escrito por nosotros**: herramientas de análisis, parsers,
  documentación de formatos, un runtime o un renderer propios. REDRIVER2 y
  sotn-decomp llevan años publicados bajo este modelo.
- **Documentar formatos**. Describir la estructura de `ACE.SPH` no reproduce
  obra protegida.
- **Un APK que no contenga nada de Namco**, que pida al usuario su propia
  copia del juego y lea o extraiga los datos en el primer arranque.

## No se puede

- **Distribuir el APK con código o assets de Namco dentro.** Eso es
  infracción: fuera de Google Play y expuesto a retirada incluso en GitHub.
- **Subir las imágenes de disco, los ejecutables extraídos o los assets** a
  ningún repositorio. Por eso `Rom/` y `data/` están en `.gitignore`, junto
  con `*.bin`, `*.SPB`, `*.BPB`, `SCES_*`, `SLUS_*`.
- **Incluir bytes del original** en el repositorio "como referencia", ni
  volcados de RAM, ni el módulo `ACE.BIN` descomprimido.

## Regla operativa

> Si un archivo salió del disco, no entra en git. Punto.

Antes de cualquier primer `git add`, comprobar:

```bash
git status --porcelain | grep -iE '\.(bin|cue|iso|spb|bpb|sph|bph|exe)$|SCES_|SLUS_|SLPS_'
```

Si eso imprime algo, el `.gitignore` no está haciendo su trabajo.

## El APK libre (etapa 7)

`REGION=libre android_game/build.sh` genera un APK **solo con código del
repositorio**:
- no lleva el código del juego traducido ni `ACE.BIN`;
- lee `ACE.BIN` del disco del usuario e interpreta el código del juego en el
  aparato, como un emulador (`docs/12-etapa7.md`).

Es el caso "un APK que no contenga nada de Namco" de arriba.

**Comprobación antes de publicarlo** (obligatoria en cada release):

```bash
python3 tools/apk_audit.py data/android/ac3game-<version>-libre.apk \
    data/exe/ace_eu.bin data/exe/ace_us.bin data/exe/ovl/overlay*.exe data/exe/ovl_us/overlay*.exe
```

Debe dar **0 coincidencias**. El APK personal de la etapa 6 da cientos de
miles, y es el caso positivo con el que se validó la herramienta.

Las tablas versionadas del modo libre (`psx/recomp/hle_*.c`,
`psx/recomp/ac3_versions.c`) contienen solo direcciones y nombres de funciones
de librería, que son resultados del análisis, igual que los archivos de
símbolos que publican los proyectos de decompilación. No contienen bytes del
juego.

