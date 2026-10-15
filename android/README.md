# AC3 Slice — rebanada vertical (etapa 4)

App nativa de Android (C + OpenGL ES 3, sin Gradle; solo una actividad Java
mínima para el selector de archivos): un avión del
jugador volando sobre el terreno de una misión, con el mando físico. Es la
**prueba de la verdad** de `docs/05-hoja-de-ruta.md`, no un juego.

**El APK no contiene nada del juego.** En el arranque busca tu propia imagen
de disco y lee de ella, con un port a C de las herramientas de `tools/`
(validado contra ellas: mismos triángulos, TIM y alturas), el mapa y las
teselas de la misión, las texturas y el modelo del avión.

## Compilar

```bash
make apk            # o: android/build.sh  ->  android/build/ac3slice.apk
```

Requiere el SDK en `~/tools/android-sdk` (plataforma 36, build-tools 37.0.0,
NDK 30) y el JDK de `~/tools`. La librería se enlaza con páginas de 16 KB.
La clave de firma de depuración se genera en `data/android/` (fuera de git).

## Instalar en la Retroid

1. Copia el APK al dispositivo y ábrelo, o con depuración USB:
   `~/tools/android-sdk/platform-tools/adb install -r android/build/ac3slice.apk`
2. Al abrir la app aparece el **selector de archivos del sistema**: elige la
   imagen `.bin` del disco (europeo o americano). No hace falta conceder
   ningún permiso: el selector da acceso solo a ese archivo, esté en la
   memoria interna o en la tarjeta SD.
3. La elección se recuerda: las siguientes veces arranca directamente. Para
   elegir otro disco, pulsa **Select + Start**.

**La imagen debe ser el `.bin` en crudo** (2352 bytes por sector). **No
sirven `.chd` ni `.pbp`** (comprimidos). Si eliges un archivo que no es un
disco de Ace Combat 3, el selector vuelve a abrirse.

## Pantallas de estado

Todavía no hay texto en pantalla:

| Color | Significado |
|---|---|
| azul oscuro | esperando la imagen de disco, o cargando |
| rojo | fallo al cargar una misión o un avión (ver `adb logcat -s AC3Slice`) |

## Controles

| Mando | Acción |
|---|---|
| palanca izquierda | cabeceo invertido como el original (hacia delante = morro abajo) y alabeo |
| A / B | disparar misil (se guía hacia el blanco del recuadro verde) |
| R2 / L2 | acelerar / frenar |
| R1 / L1 | guiñada |
| palanca derecha | girar la cámara |
| Y / X | avión siguiente / anterior (los 22) |
| Select | misión siguiente (registros 6-46) |
| Start | volver a la posición inicial |
| Select + Start | elegir otra imagen de disco |

## HUD y combate

- Arriba a la derecha, discreto: **fps** (media de cada medio segundo).
  Arriba a la izquierda: **K** = derribos.
- **Enemigos**: hasta 8 aviones de la propia misión (sección 2 del registro,
  sus modelos y pieles originales) con una IA sencilla que vuela entre puntos
  de paso alrededor del jugador. Al ser alcanzados explotan y reaparecen a
  los 5 s.
- **Misiles**: modelo real del juego (sección 3, entrada 3). Se fija el
  enemigo más centrado en un cono de ~35° y a menos de 14.000 unidades
  (recuadro verde); el misil acelera y se guía hacia él.
- El texto del HUD usa una fuente propia de 5×7 (no es la del juego).

## Rendimiento

Sin tope de fps: vsync a la frecuencia nativa de la pantalla (objetivo 60) y
física por delta-time. Los fps se escriben en el registro cada 2 s:

```bash
adb logcat -s AC3Slice
```

## Probar en el PC

`render.c` no depende de Android. `test/test_render.c` dibuja un fotograma
con EGL + GLES3 (Mesa) y `test/test_desktop.c` valida la carga de datos:

```bash
cd android
cc -O2 -Isrc test/test_desktop.c src/ace.c src/scene.c -lm -o build/test_desktop
build/test_desktop <imagen.bin> 6 55
```

## Qué es del juego y qué no

| Elemento | Origen |
|---|---|
| Terreno, texturas, mapa de teselas | disco del usuario (registro de misión) |
| Avión del jugador y enemigos: modelos, pieles, colores | disco del usuario |
| Modelo del misil | disco del usuario (s3, entrada 3); mide ~13 unidades |
| Estela del misil (cuadrado claro detrás) | **propio**, provisional |
| Explosiones | **propias** (cuadrados de color); las del juego no están localizadas |
| Cielo, niebla, luz del avión del jugador | **propios** (aproximación); los parámetros de entorno del juego están en el preludio de la misión, sin descifrar campo a campo |
| Texto del HUD | fuente propia 5×7 |

## Limitaciones conocidas

- Vuelo arcade simple, sin física real del juego, sin IA, HUD ni misiones.
- Solo se dibujan las piezas del avión en su estado base (sin superficies
  móviles), el terreno, los enemigos y los misiles (no los objetos de s4).
- Combate simplificado: los enemigos no disparan ni hay colisiones entre aviones.
- Iluminación aproximada (luz fija); los colores precalculados del terreno sí
  son los del juego.
