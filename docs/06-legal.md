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
