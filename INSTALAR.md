# AC3 Recomp — instalar y probar la alfa

**AC3 Recomp** ejecuta *Ace Combat 3: Electrosphere* (PlayStation, Namco, 1999)
de forma nativa en Android, a 60 imágenes por segundo en vuelo y en pantalla
ancha (16:9).

**El APK no contiene nada del juego.** Necesitas tu propia copia del disco:
- La app lee el juego de esa copia al arrancar y ejecuta su código con un
  intérprete propio, como un emulador.
- Ni este repositorio ni la release incluyen código, gráficos ni sonido de
  Namco (`docs/06-legal.md`).

> **Estado: alfa.** Probado en una Retroid Pocket Flip 2 con los discos
> europeo y americano, hasta la primera misión jugable. Puede haber fallos,
> sobre todo más allá de lo probado.

## Requisitos

| | |
|---|---|
| Android | 8.0 o superior (API 26), procesador de 64 bits (arm64-v8a) |
| Gráficos | OpenGL ES 3.0 |
| Mando | Recomendado: botones físicos (consolas Android tipo Retroid, AYN, Anbernic… o un mando Bluetooth/USB). No hay controles táctiles |
| Juego | Tu propia imagen del disco de **Ace Combat 3: Electrosphere**:<br>• europea, `SCES-02066` (inglés, francés, alemán, español, italiano)<br>• americana, `SLUS-00972` |
| Formato del disco | **`.bin` en bruto (2.352 bytes por sector)**, el que generan los volcados habituales (`.bin` + `.cue`; se elige el `.bin`). También sirve `.iso` de 2.048 bytes por sector, pero sin música ni vídeos (no lleva el audio XA) |
| Espacio | 230 KB la app, más tu imagen del disco (~600 MB) |

La versión japonesa (*Ace Combat 3: Electrosphere*, `SLPS-02020/02021`) **no**
está soportada.

## Instalación

1. Descarga `AC3Recomp-<versión>.apk` de la página de
   [releases](https://github.com/vzepec/ac3-decomp/releases).
2. Copia la imagen del disco (`.bin`) a tu dispositivo: almacenamiento
   interno, tarjeta SD, etc.
3. Instala el APK. Android pedirá permitir la instalación desde esa fuente
   ("orígenes desconocidos").
4. Abre **AC3 Recomp**. La primera vez se abre el selector de archivos:
   elige el `.bin` del juego.
   - La app reconoce si es el disco europeo o el americano y arranca.
   - Si eliges otro archivo, vuelve a pedirlo.
   - La siguiente vez abre directamente la misma imagen.
5. La partida se guarda en una tarjeta de memoria virtual dentro de la app.
   Se conserva al actualizar a una versión nueva.

## Controles

La app convierte los botones de Android en los del mando analógico de
PlayStation (DualShock, modo analógico):

| Botón del dispositivo | Botón de PlayStation | En el juego |
|---|---|---|
| A | ✕ (cruz) | Aceptar en los menús |
| B | ○ (círculo) | Disparar misil |
| X | □ (cuadrado) | Mantener pulsado: mapa grande |
| Y | △ (triángulo) | Según el juego |
| L1 / R1 | L1 / R1 | Según el juego |
| L2 / R2 (también gatillos analógicos) | L2 / R2 | Según el juego |
| Cruceta | Cruceta | Menús; pilotar |
| Palanca izquierda | Palanca izquierda | Pilotar el avión |
| Palanca derecha | Palanca derecha | Girar la cámara |
| Start | START | Pausa |
| Select | SELECT | Cambiar la vista |
| **L3 + R3** (pulsar las dos palancas a la vez) | — | **De la app:** cambia el modo de imagen: 60 fps + 16:9 → 60 fps + 4:3 → original (4:3, sin imágenes intermedias) |

"Según el juego" son botones que funcionan exactamente como en el original
pero que no hemos documentado aún. Las funciones de la columna "En el juego"
están comprobadas en pruebas.

## El contador

Arriba a la izquierda se muestra:

| | Qué significa |
|---|---|
| `FPS` | Imágenes distintas mostradas por segundo |
| `VEL` | Velocidad del juego frente a la consola original; **100 %** es la correcta (50 Hz la europea, 60 Hz la americana) |
| `INT` | Imágenes intermedias por paso del juego (1 con 60 fps; `-` en el original) |
| `CPU` | Uso del hilo del juego |
| `16:9` / `4:3` / `ORIG` | Modo de imagen (se cambia con L3+R3) |

## Qué esperar de la alfa

- **Vuelo a 60 fps.** La misión es fluida gracias a las imágenes intermedias.
  El juego calcula 25 pasos por segundo (30 en la americana), como el
  original, y la app añade una imagen entre paso y paso.
  - En esa imagen, el mundo (terreno, cielo, ciudades) **lo vuelve a dibujar
    el propio código del juego**, con la cámara a medio camino. Por eso no
    hay vértices estirados ni cielo que tiembla.
  - Lo que se mueve por su cuenta (aviones, misiles, efectos) todavía se
    interpola en pantalla, como antes.
  - **La imagen la dibuja la GPU** del dispositivo, a la resolución original
    y con el mismo aspecto que la PlayStation. Así el vuelo se mantiene a 60
    fps también sobre las ciudades más pobladas (medido en una Retroid
    Pocket Flip 2).
  - En los cortes de cámara, ese paso va sin imagen intermedia.
  - L3+R3 permite volver a las imágenes originales.
- La versión **americana** es la de referencia y la más probada; la europea
  también funciona.
- Explosiones, humo y llamas de los motores van al ritmo original.
- **Pantalla ancha (16:9)**, activada por defecto:
  - **En la misión** se ve más escenario por los lados, sin deformar la
    escena 3D. El HUD, las explosiones y el humo salen algo ensanchados.
  - **Menús y briefing** se estiran a 16:9.
  - **Los vídeos** siguen a 4:3, para no deformarlos.
  - L3+R3 pasa a 4:3.
- Imagen a la resolución original (320×240/256), escalada a la pantalla.
  Todavía no hay alta resolución.

## Si algo falla

Abre una incidencia en el repositorio indicando:
- el dispositivo;
- el disco (europeo o americano);
- qué hacías;
- el contador (`VEL`, `FPS`).

Si puedes, adjunta un vídeo o el registro:

```bash
adb logcat -s AC3Game
```

## Compilarlo tú mismo

```bash
sh android_game/build.sh   # -> data/android/AC3Recomp-<versión>.apk
```

Necesita el SDK y el NDK de Android (`android_game/README.md`), pero no el
juego: el APK se construye solo con este repositorio. Antes de publicar uno,
comprueba que no lleva bytes del juego con `tools/apk_audit.py`
(`docs/06-legal.md`).
