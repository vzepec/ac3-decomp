# AC3 Recomp — el juego recompilado en Android (etapa 6, paso 4)

**Compilación personal. No se distribuye.** El APK enlaza el código del juego
traducido a C (`data/recomp/*.c`) e incrusta `ACE.BIN`; los dos se generan
desde el disco del usuario. Por eso el APK se escribe en `data/android/`,
fuera de git (ver `docs/06-legal.md`). Este directorio solo contiene código
nuestro: el programa principal y el puente Java del selector de disco.

**APK universal (1.6-alfa):** una sola app para los dos discos, el europeo
(`SCES_020.66`) y el americano (`SLUS_009.72`).
- La app elige la versión al abrir el disco, por su ejecutable de arranque. Si
  el disco no es de ninguna de las dos, vuelve a pedirlo.
- Lleva las dos traducciones y los dos `ACE.BIN` (unos 7 MB).
- Las direcciones de cada versión son una tabla
  (`src/ac3_regions.h`, `src/regions.c`).
- Los símbolos del código americano llevan el sufijo `_us`, porque las
  funciones se llaman por su dirección (`f_80025458`…) y chocarían con las
  europeas.

```bash
make native                          # europea: data/recomp/ y prueba en el PC
make us-prep ghidra-us native-us     # americana: data/recomp_us/ y prueba en el PC
android_game/build.sh                # universal -> data/android/ac3game-<version>.apk
REGION=eu android_game/build.sh      # solo la europea   -> ac3game-<version>-eu.apk
REGION=us android_game/build.sh      # solo la americana -> ac3game-<version>-us.apk
```

- **Paquetes:** el universal usa `com.vzepec.ac3game` y sustituye a la app
  europea de antes, con la misma tarjeta de memoria. Las versiones sueltas
  usan `com.vzepec.ac3game.eu` («AC3 Recomp (EU)») y `com.vzepec.ac3game.us`
  («AC3 Recomp (US)»), así que conviven con él.
- **Ritmo:** PAL va a 50 VBlank/s y 882 muestras de audio por VBlank; NTSC, a
  60 y 735. `VEL` 100 % es el ritmo de la versión del disco.

## Cómo funciona

- **Hilo del juego:** `ac3_main` recompilado sobre la capa `psx/`.
  - El reloj es el presupuesto de saltos del código traducido, como en
    `psx/recomp/native_main.c`.
  - En cada VBlank espera al tiempo real (50 Hz, PAL), lee el mando, genera
    882 muestras de la SPU y deja la imagen visible en un búfer compartido.
- **Hilo principal:** EGL/GLES 3 y presentación a 4:3 (`psx/gl/psx_present.c`),
  entrada y ciclo de vida.
- **AAudio:** lee las muestras de un anillo. La SPU no tiene cerrojos, así que
  solo la toca el hilo del juego.
- **Disco:** se elige con el selector del sistema; no hace falta ningún
  permiso.
- **Tarjeta de memoria:** `tarjeta1.mcd`, en los datos privados de la app.
- **Mando:** analógico (DualShock, 0x73).
  - Sticks de la Retroid → sticks.
  - A/B/X/Y → cruz/círculo/cuadrado/triángulo.
  - L1/R1/L2/R2, Start/Select y la cruceta, a sus botones.

## Limitaciones conocidas (1.0-alfa)

- `BUDGET_PER_FRAME` no está calibrado. Si un fotograma del juego exige más
  trabajo que el presupuesto, el reloj avanza a mitad de fotograma.
- Imágenes intermedias (1.1-alfa): en la misión, 25 pasos de lógica por
  segundo se muestran con hasta 3 imágenes intermedias por paso, interpoladas
  por vértices (`psx/src/interp.c`).
  - Un paso de retraso.
  - L3+R3 las activa o desactiva.
  - Su número se adapta: baja si los hilos de dibujo no llegan a tiempo o si
    el hilo del juego va justo.
  - Desde la 1.2-alfa se dibujan en 3 hilos aparte, fijados a los núcleos
    rápidos. En la 1.1 frenaban el juego en la Retroid.
- Contador (1.2-alfa), arriba a la izquierda:
  - `FPS`: imágenes distintas por segundo;
  - `VEL`: velocidad del juego frente a la consola;
  - `INT`: intermedias por paso;
  - `CPU`: uso del hilo del juego.
  - Detalle cada segundo en `adb logcat -s AC3Game`.
- Aún no hay pantalla ancha.
- Solo se ha probado hasta la primera misión en el PC (`make native`).
