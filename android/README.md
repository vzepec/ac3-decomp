# AC3 Slice — rebanada vertical (etapa 4)

App nativa de Android (C + OpenGL ES 3, sin Java ni Gradle): un avión del
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
2. **La imagen de disco** debe ser el `.bin` en crudo (2352 bytes por sector)
   del disco europeo o americano. **No sirven `.chd` ni `.pbp`** (comprimidos).
   La app busca archivos `.bin`/`.iso` de volumen `ACE3ES` en, por orden:
   - `Android/data/com.vzepec.ac3slice/files/` (no necesita permisos)
   - `ROMs/` (y subcarpetas hasta 3 niveles), `Download/`, `PSX/`, `AC3/`,
     la raíz del almacenamiento y las tarjetas SD
3. Para las carpetas fuera de la de la app, concede el permiso a mano:
   **Ajustes > Aplicaciones > AC3 Slice > Acceso a todos los archivos**.

## Pantallas de estado

Todavía no hay texto en pantalla:

| Color | Significado |
|---|---|
| azul oscuro | cargando |
| magenta | no se encuentra la imagen de disco (o falta el permiso) |
| rojo | la imagen no se pudo leer como Ace Combat 3 |

## Controles

| Mando | Acción |
|---|---|
| palanca izquierda | cabeceo (hacia delante = morro abajo) y alabeo |
| R2 / L2 | acelerar / frenar |
| R1 / L1 | guiñada |
| palanca derecha | girar la cámara |
| Y / X | avión siguiente / anterior (los 22) |
| Select | misión siguiente (registros 6-46) |
| Start | volver a la posición inicial |

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

## Limitaciones conocidas

- Vuelo arcade simple, sin física real del juego, sin IA, HUD ni misiones.
- Solo se dibujan las piezas del avión en su estado base (sin superficies
  móviles) y el terreno (sin objetos de s4 ni aviones de la misión).
- Iluminación aproximada (luz fija); los colores precalculados del terreno sí
  son los del juego.
