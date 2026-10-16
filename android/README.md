# AC3 Slice — rebanada vertical (etapa 4)

App nativa de Android (C + OpenGL ES 3, sin Gradle; solo una actividad Java
mínima para el selector de archivos): un avión del
jugador volando sobre el terreno de una misión, con el mando físico. Desde la
0.4, con el cielo, los enemigos, los objetos de tierra y los efectos del
juego. Es la
**prueba de la verdad** de `docs/05-hoja-de-ruta.md`, no un juego.

**El APK no contiene nada del juego.** En el arranque busca tu propia imagen
de disco y lee de ella, con un port a C de las herramientas de `tools/`
(validado contra ellas: mismos triángulos, TIM y alturas), el mapa y las
teselas de la misión, las texturas y el modelo del avión.

## Compilar

```bash
make apk            # o: android/build.sh  ->  android/build/ac3slice-<version>.apk
```

Requiere el SDK en `~/tools/android-sdk` (plataforma 36, build-tools 37.0.0,
NDK 30) y el JDK de `~/tools`. La librería se enlaza con páginas de 16 KB.
La clave de firma de depuración se genera en `data/android/` (fuera de git).

## Instalar en la Retroid

1. Copia el APK al dispositivo y ábrelo, o con depuración USB:
   `~/tools/android-sdk/platform-tools/adb install -r android/build/ac3slice-<version>.apk`
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
| rojo | fallo al cargar una misión o un avión (ver `adb logcat -s AC3Slice`); se puede seguir cambiando de misión (Select) o de avión (Y/X) |

## Controles

| Mando | Acción |
|---|---|
| palanca izquierda | cabeceo invertido como el original (hacia delante = morro abajo) y alabeo |
| A / B | disparar misil (se guía hacia el blanco del recuadro verde) |
| R2 / L2 | acelerar / frenar (L2 abre el aerofreno) |
| R1 / L1 | guiñada (mueve el timón) |
| palanca derecha | girar la cámara |
| Y / X | avión siguiente / anterior (los 22) |
| Select | misión siguiente (registros 6-46) |
| Start | volver a la posición inicial |
| Select + Start | elegir otra imagen de disco |

## HUD y combate

- Arriba a la derecha, discreto: **fps** (media de cada medio segundo).
  Arriba a la izquierda: **K** = derribos.
- **Inicio** (0.6): el del guion, con su rumbo (unidad 0x4000 = 360°,
  corregida). Ajustes propios de la rebanada: si el guion deja el inicio en
  (0, 0, 0) (un marcador que rellena en partida) o el enemigo más cercano
  queda a más de 30.000 u, se empieza a 15.000 u de él, mirándolo; los
  enemigos con posición marcador aparecen en un anillo cercano.
- **Inicio, enemigos y objetos de tierra salen del guion de la misión**
  (sección 6): empiezas donde empieza el jugador; los aviones enemigos
  aparecen en sus posiciones y formaciones, con sus modelos; los edificios y
  unidades de tierra, en su sitio y a ras de suelo. La IA es propia y
  sencilla (vuelan entre puntos alrededor de su posición inicial, o de ti si
  estás cerca). Si los derribas a todos, reaparecen a los 6 s.
- **Misiles**: se fija el blanco (avión u objeto) más centrado en un cono de
  ~35° y a menos de 14.000 unidades (recuadro verde).
- **Choques** (0.5): te estrellas contra el terreno (sus triángulos reales:
  suelo, puentes, edificios), los objetos de tierra y los aviones enemigos.
  Se puede pasar por debajo de puentes y entre edificios si cabes. Tras
  estrellarte, explosión y reaparición en el inicio a los 3 s. El radio del
  avión (8 unidades) y la detección son propios: la colisión del juego está
  sin resolver.
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
cc -O2 -Isrc test/test_collide.c src/ace.c src/scene.c -lm -o build/test_collide
build/test_collide <imagen.bin> 20     # huecos bajo puentes y estructuras
```

## Qué es del juego y qué no

| Elemento | Origen |
|---|---|
| Terreno, texturas, mapa de teselas | disco del usuario (registro de misión) |
| Cielo por bandas y color de niebla de cada misión | disco (preludio de la misión); la mezcla "hacia el sol" usa una unidad de ángulo supuesta |
| Posición inicial, enemigos y objetos de tierra | disco (guion de la misión, s6): posición, bando, modelo, rumbo |
| Modelos de aviones enemigos, objetos, misil y tu avión | disco (s2/s3/s4 y registro de tipo 4) |
| Alerones, cabeceo, timón y aerofreno de tu avión | disco (posiciones extremas del modelo) e **interpolación y suavizado del código** (0.6) |
| Explosiones | disco (registro común 0000): animación de 10 fotogramas, vida (14 ticks) y tamaño (256) del código |
| Humo oscuro de los restos | disco (texturas y paleta de humo); los restos y su trayectoria son aproximados |
| Llama del misil | quitada en la 0.6: ese sprite es de los disparos rectos, no del misil |
| Sol y su halo | disco y halo del juego (s13), dirección y tamaño del código; el resplandor a pantalla completa es aproximado |
| Estrías de velocidad (misiones con clima 1) | textura y número de trazos del juego; longitud y anchura aproximadas |
| Estela de humo del misil y de puntas de ala | forma, frecuencia de puntos y vida del código; **color y transparencia propios** (sin resolver) |
| Objeto destruido | **hipótesis**: el modelo anterior de s4, más bajo (sin confirmar en el código) |
| Física de vuelo, IA, combate | **propios** y simplificados |
| Luz del avión del jugador | **propia** (dirección fija) |
| Efectos de sonido (0.7) | bancos del juego (común 0000 s3, avión, misión), decodificados del disco; ids del código: lanzamiento 0x0E, impacto 0x1D, fallo 0x1C, avión destruido 0x1B, motor 84/85; frecuencia por notas como la SPU |
| Volumen por distancia, panorama, curva del motor | **propios** (aproximación de `FUN_8002EFA4` / `FUN_8002FFAC`) |
| Música | **todavía no**: está en XA en `ACE.SPB` y falta saber qué pista suena en cada misión |
| Texto del HUD | fuente propia 5×7 |

Ritmo del juego (0.6): 25 ticks por segundo con el disco europeo (PAL) y 30
con el americano (NTSC), como el original; se detecta por el ejecutable de
arranque del disco (`SCES_`/`SLES_` = PAL).

## Limitaciones conocidas

- Vuelo arcade simple, sin física real del juego, sin HUD ni objetivos de misión.
- Combate simplificado: los enemigos no disparan ni hay colisiones entre aviones.
- Sin campo de nubes (solo lo usa la misión 16) ni capas de bruma todavía.
- Corregido en la 0.6: a veces se empezaba mirando en dirección contraria
  (unidad de ángulo equivocada) o muy lejos de la acción.
- Iluminación del avión aproximada (luz fija); los colores precalculados del
  terreno, los objetos y los enemigos sí son los del juego.
- Corregido en la 0.5: el avión chocaba con "el aire" encima de puentes y
  edificios (el suelo era la altura máxima de cada celda de 2048 unidades) y
  no podía estrellarse.
- Corregido en la 0.4: al cambiar de misión, las misiones 23 y 43 (de gran
  altitud, sin terreno) daban pantalla roja y la app quedaba atascada.
