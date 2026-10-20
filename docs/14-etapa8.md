# Etapa 8: interpolación pulida (redibujar las intermedias con el código del juego)

Rama `etapa-8` (sale de `etapa-6-paso-20`, sin publicar). Decisión del dueño
tras la investigación de `docs/13-60fps.md`.

## Idea

- En cada imagen intermedia, el **mundo estático** (terreno, cielo, ciudades,
  objetos de escena) se vuelve a dibujar con **el propio código de dibujo del
  juego** y una cámara interpolada entre dos pasos.
- **Los objetos móviles** (aviones, misiles, efectos) siguen con la
  interpolación en pantalla (etapa 6).
- Sin emparejar vértices del mundo estático: sin estiramientos, cielo
  tembloroso ni huecos en el horizonte.

## Lo que se sabe (EU; las direcciones US se obtienen por firma, como en el paso 16)

**El dibujo de la misión** (`FUN_80033748`, desde el paso de vuelo
`FUN_80033c58`):
1. elige la tabla de orden del búfer actual (`g_render[4]`) y la limpia
   (`ClearOTagR`, 4096);
2. llama a la **lógica del paso** (parámetro: `0x80032ca4`; el avión, la IA,
   los objetos y los efectos se actualizan y en parte se dibujan ahí);
3. actualiza la cámara (`FUN_80079da4(g_render[10])`) y construye sus
   matrices (`FUN_8003304c(cámara, g_render+0x2C, g_render+0x4C)`);
4. **dibuja el mundo estático:** `FUN_8003308c`, `FUN_80033140`,
   `FUN_80033194` (objetos de escena), `FUN_800331d4` (terreno);
5. dibuja el HUD (`FUN_80039d84`…).

**Variables:**
- `g_render` = `*(0x800BACC4)` (`0x8013D648` en partida);
- `g_render[4]` es la tabla de orden y `g_render[5]` el puntero de
  primitivas (dos búferes de 0x1F800 bytes);
- la cámara es `g_render[10]` (`0x8013D700`).

**Prueba de concepto** (`AC3_RERENDER=vblank` del binario libre):
- en `DrawOTag` se desenganchan los paquetes del mundo estático (los
  primitivos creados entre la entrada de `FUN_8003308c` y la de
  `FUN_80039d84`: 536 en la misión 2);
- se gira la cámara (`AC3_RERENDER_YAW`) y se vuelven a llamar esas funciones
  (`guest_call`, como una interrupción);
- **resultado:** terreno y cielo girados, HUD y avión intactos. Esa imagen se
  ve 3 VBlank después (doble búfer).

**Entradas del dibujo estático** (`AC3_RERENDER_READS` +
`AC3_RERENDER_SURVEY=pasos`: el intérprete marca las palabras que se leen
antes de escribirlas — `mipsi_rtrace` / `mipsi_wtrace` — y se cruzan con la
RAM del paso anterior). 400 pasos de la misión 2 (EU):

| Entrada | Tipo | En la intermedia |
|---|---|---|
| `cámara+4/+8/+C` | posición (enteros) | interpolar |
| `cámara+14/+16/+18` | ángulos (16 bits, círculo = 16384, `& 0x3FFF`) | interpolar por el camino corto |
| `cámara+9C/+A0/+A4` | vector pequeño (cambia cada paso) | interpolar |
| `g_render+474/+4D0/+52C/+588` (+0..+8) | posiciones de 4 objetos | interpolar |
| `g_render+C`, `+10`, `+14`, `cámara+1E0`, `800bd15c` | contador, búfer actual, desplazamientos del doble búfer | del paso actual |
| `g_render+4C..5C` | matriz traspuesta (la escribe `TransposeMatrix`, de la librería) | resultado, no entrada |

**Corrección pendiente:** las primitivas nuevas no van al otro búfer (allí
viven primitivas 2D de la misma imagen: cambiaba un dígito del altímetro),
sino tras el final del búfer actual.

## Redibujo exacto (validado)

Con la misma cámara, el redibujo tiene que reproducir la imagen original
**bit a bit**. Es la prueba de que el método no introduce errores propios
(`AC3_RERENDER_SELFTEST=1`: compara las primitivas decodificadas y la VRAM).

Hicieron falta cuatro cosas:
1. **Partir del estado del juego al empezar el dibujo estático (S0).** En la
   entrada de `FUN_8003308c` se guardan RAM, scratchpad, GTE y la pila; el
   dibujo no es puro (cachés, listas que rotan en cada llamada). Antes de
   redibujar se devuelven a S0 las palabras que escribió el dibujo original
   (registro de escrituras del intérprete), salvo las tablas de orden, los
   búferes de primitivas y los paquetes de la cadena (HUD, sprites 2D).
2. **El puntero de primitivas no vuelve a S0:** si no, el redibujo escribía
   encima de lo viejo y pisaba el HUD (un dígito del altímetro cambiaba).
   Las primitivas nuevas van detrás de todas las de la imagen.
3. **Los enlaces de la tabla de orden maestra (`0x800BC154`) no se
   restauran:** quedaba un enlace a la dirección 0.
4. **El orden dentro de cada cajón de la tabla de orden.**
   - El juego inserta a la cabeza: primero lo de antes del dibujo estático,
     luego lo estático y luego lo de después (el desplazamiento de dibujo E5
     del frente). En la cadena queda: después > estático > antes.
   - Lo redibujado entra el último y quedaba delante del E5: el cielo salía
     sin su desplazamiento (160,128 píxeles de error, 22 primitivas).
   - Se recoloca.

**Resultado** (misión 2 EU, VBlank 4300, 4500 y 4700): 1.102, 755 y 1.116
primitivas, **0 distintas** y **0 píxeles** distintos del original.

## Módulo e integración

**`psx/recomp/ac3_rerender.c`** (direcciones EU y US):
- `ac3_rr_install` pone los enganches de inicio y fin del dibujo estático;
- `ac3_rr_build(ot, t)` construye en `psx_ram` la lista de la intermedia de
  fase t.
- **Entradas que interpola:**
  - la cámara (posición, ángulos por el camino corto, vector `+9C`);
  - las posiciones del **arreglo de objetos móviles del mundo**
    (`g_render+0x474 + k·0x5C`, 32 entradas, hasta la 26 usada en la
    misión 24). Lo leen la IA y el reparto de LOD de los objetos de escena:
    vehículos, barcos.
  - Un objeto cuya posición salta más de 2^20 (la ranura cambia de
    ocupante) se queda en su sitio.
- **Cortes de plano** (más de 1/8 de vuelta o 2^21 de posición): no hay
  redibujo.
- **Restauración completa:** toda palabra distinta de S0, no solo las
  escritas. La encuesta de la US mostró lecturas de la **pila** sin
  iniciar (`0x801FF940..`) que otras funciones reescriben antes de
  `DrawOTag`.

**Direcciones US:** por firma desde la EU. `FUN_80033140` (constantes
distintas) se saca de la secuencia de llamadas del dibujo de la misión US
(`0x80033910`): `0x80033308`. `g_render` = gp + 968 = `0x800BB81C`.

**Encuesta en las 41 misiones US** (`AC3_RR_SURVEY=1`): el resto de lo que
cambia entre pasos es el búfer actual, contadores y desplazamientos del doble
búfer (se quedan como en el paso actual).

**En la interpolación** (`psx/src/interp.c`, `psx_interp_rebuild`):
- en el hilo del juego, para cada fase, se guarda la RAM, se construye la
  intermedia, se copia para los hilos de dibujo y se restaura;
- los hilos parten de esa RAM y solo interpolan en pantalla lo emparejado
  (lo móvil);
- si el emparejamiento del paso falla, las intermedias salen igual, con el
  mundo exacto y lo móvil quieto.

**PC:** `AC3_RR=1` con `AC3_INTERP` y `AC3_INTERP_THREADS`.
- Con t = 1 (`AC3_RR_SELFTEST`), la americana sale exacta en 3 momentos de
  la misión 0.
- **Coste** (misión 0 US): 4,6 ms por paso redibujado, con las copias de
  RAM. La misión sin redibujo: 29,3 s de CPU; con él, 33,3 s.

**App** (`android_game/src/game_main.c`):
- L3+R3: `16:9 N` (nueva, por defecto) → `16:9 A` (la anterior) → `4:3 N`
  → `ORIG`;
- con la nueva, **una** intermedia por paso (30 → 60 fps; la pantalla no
  muestra más).

**Estabilidad:** las 41 misiones US con `AC3_RR=1 AC3_INTERP=1`
(`tools/misiones.py`): **41 sin fallos**. Entre 5 y 7 pasos por misión sin
redibujo (cortes de plano), unos 5 ms de media por paso.

**APK 1.9.5-alfa** (rama `etapa-8`, sin publicar): para probar en la Retroid.
- Comparar `16:9 N` con `16:9 A` en ciudades.
- Mirar `VEL` y `CPU`: el redibujo corre en el hilo del juego.

**Pendiente:**
- los objetos móviles (aviones, misiles, efectos) siguen con la
  interpolación en pantalla;
- la orientación de los objetos del arreglo (+0x10/+0x14) no se interpola;
- la europea, con su propia encuesta.

## Prueba de la 1.9.5 en la Retroid y lo que salió

**Informe del dueño:**
- "se ve más estable", pero baja a **20-30 fps** en zonas con muchos
  edificios u objetos;
- en la pausa, **el velocímetro queda encima del menú rojo**;
- de antes, solo en 16:9: **a veces, al girar, se deforma la imagen en los
  bordes laterales** (captura en `imagenes bugs/`).

### El HUD encima del menú de pausa

- **Causa:**
  - en la pausa el juego no ejecuta el dibujo estático: el rango guardado
    era de otro paso, y desengancharlo rompía la cadena;
  - además, el código de la pausa reserva primitivas más allá de
    `g_render[5]` sin moverlo, así que lo redibujado se escribía encima;
  - y la recolocación por clases (después / nuevo / antes) movía de sitio
    paquetes del HUD que el juego pone en los mismos cajones.
- **Corrección** (`psx/recomp/ac3_rerender.c`):
  - el fin del dibujo estático solo vale si su inicio corrió en el mismo
    paso;
  - un rango de más de 3 VBlank de antigüedad no se redibuja;
  - lo nuevo se escribe detrás del final real de lo que usa la cadena, no
    de `g_render[5]`;
  - en cada cajón, el orden de lo que ya estaba no se toca: lo nuevo va
    justo donde estaba lo estático (tras su "ancla", el paquete que lo
    precedía).
- **Comprobación:** con t = 1, la pausa (VBlank 3065, misión 0) sale exacta
  (0 píxeles distintos; antes, 614). Las demás pruebas siguen a 0.

### Autoprueba en las 41 misiones

`AC3_RR_SELFTEST=2600 AC3_RR_SELFTEST_EVERY=10`: compara con t = 1 cada 10
VBlank, con el guion de `tools/misiones.py` más dos pausas.
- **Resultado:** 5.592 imágenes, 150 distintas (2,7 %), en 7 misiones; 9
  sin construir (cortes de plano y pausas).
- **Causa de las distintas** (misión 22, VBlank 2600; registro de lecturas
  del dibujo original frente al redibujo):
  - el juego lee posiciones de la pila que **no inicializa**
    (`0x801FFE48` desde `0x8007B5F0`, y otras);
  - su contenido lo deja lo último que pasó por allí, y en el original una
    interrupción de VBlank cae a mitad del dibujo. Es un comportamiento del
    juego, como en la PS1, no del redibujo;
  - el efecto: unos colores de la bruma con 1 de diferencia (45 frente a
    44).
- **De paso:** el redibujo en el hilo del juego gastaba el presupuesto de
  instrucciones, y podía disparar un VBlank del juego a mitad del redibujo
  (con la RAM de S0, que luego se restaura). Lo resuelve el hilo de dibujo
  (abajo).

### La franja deformada en los bordes (16:9)

- **Causa:**
  - las franjas del cielo (paso 19) se cierran con dos líneas del
    horizonte;
  - cada línea son dos puntos a ±X que la GTE proyecta (US `FUN_80093324`,
    EU `FUN_80092f90`) y que se recortan a la pantalla (US `FUN_8005dd8c`,
    ±160 × ±120);
  - X lo calcula el juego (US `FUN_8009354c`: `0x3bff88 / H`; `FUN_8009497c`)
    para que, en 4:3, los extremos caigan fuera de la pantalla (a 240 px del
    centro, más allá de la esquina, a 200);
  - con la x comprimida a 3/4, cuando la línea apunta hacia una esquina
    (unos 29° de alabeo) un extremo queda dentro, y la franja se cierra
    hasta la esquina con una cuña falsa.
- **Corrección** (`ac3_wide_install`, nuevo campo `sky_lines` de
  `Ac3Version`): al entrar en esa función, en pantalla ancha, la x de los
  extremos se multiplica por 1 / `psx_gte_wide`. Quien la llama los
  recalcula en cada imagen.
- **Medida** (`AC3_SKYCHECK=1`): en las franjas del cielo, todo vértice debe
  estar en el borde de la pantalla.
  - Misiones 0, 2, 3, 5, 8, 12, 17 y 24, 4.574 imágenes con cielo.
  - **16:9 sin la corrección** (`AC3_WIDE_NOSKY=1`): 7 imágenes con cuña (caso
    positivo: la 2927 de la misión 3, extremo en (158,117)).
  - **16:9 con la corrección:** 0 (el mismo extremo, en (160,119)).
  - **4:3:** 0.

### El redibujo, en un hilo de dibujo

- **Medida** (misión 0, PC):
  - el redibujo son unos 150.000 instrucciones del juego por paso, el
    19,5 % de todas las de la partida (en vuelo, un 40 % más sobre el paso);
  - de 4,6 ms por paso: dibujo del juego 3,2; restaurar 0,4; cadena y
    cajones 0,6; S0 0,3.
  - En el hilo del juego, eso baja la velocidad donde el juego ya va justo.
- **Segunda copia del intérprete** (`psx/recomp/ac3_rr_async.c`,
  `tools/rr_clone.sh`):
  - `mipsi.c`, `recomp_rt.c`, `mem.c`, `gte.c`, `libgte.c`, `libgpu.c`,
    `hle_*.c`, `ac3_versions.c` y `ac3_rerender.c` se enlazan dos veces;
  - la segunda, con todos sus símbolos con el prefijo `rrw_`
    (`objcopy --redefine-syms`, como el código americano en
    `android_game/build.sh`);
  - tiene su RAM, su scratchpad, su GTE, sus enganches y su presupuesto
    (sin reloj). Solo comparte las tablas de `libgte_math.c` (de solo
    lectura) y funciones sin estado.
- **Reparto:**
  - **hilo del juego** (`DrawOTag`): solo la instantánea
    (`ac3_rr_snapshot`: rango, pila, entradas, S0, `psx_gte_wide`);
  - S0 va en **dos búferes**: el del paso que se está redibujando queda
    reservado hasta `ac3_rr_release`, y el juego guarda el siguiente en el
    otro (si los dos están ocupados, ese paso no se redibuja);
  - **hilo de dibujo 0** (`psx_interp_rebuild_async`): copia la RAM del paso
    en la segunda copia, construye y dibuja cada fase.
- **Comprobación:**
  - en la misión 2 (pantalla ancha, a ritmo real), **324 de 326**
    intermedias son idénticas por los dos caminos. Las otras 2 difieren
    poco en todo el cuadro: en el camino viejo, la GTE del redibujo añadía
    vértices al emparejamiento del paso siguiente, y en la copia no;
  - la pausa sale bien;
  - a ritmo real (`AC3_PACE=60`, misión 0): 707 de 721 pasos redibujados
    (en el hilo del juego eran 718). El hilo de dibujo 0 tarda unos 17 ms
    por paso en el PC (5,6 ms de media entre los 3 hilos, de los que solo
    trabaja uno).
- **Estabilidad:** las 41 misiones US en 16:9 con `AC3_RR=1` (hilo de
  dibujo), sin ritmo real: **41 sin fallos**, 10.022 pasos redibujados.
- **PC:** `AC3_RR=1` (hilo de dibujo) o `AC3_RR=2` (hilo del juego).
  `AC3_PACE=60` va a ritmo real.

**APK 1.9.6-alfa** (rama `etapa-8`, sin publicar): las tres correcciones.
- **Qué mirar en la Retroid:**
  - `VEL` debería seguir al 100 % en ciudades;
  - `FPS`: si el hilo de dibujo no llega a tiempo, algunos pasos salen sin
    intermedia (la imagen va a 30 en esos pasos, pero el juego no se
    frena);
  - la pausa;
  - los bordes en giros.

## Release v1.9-alfa

- **Prueba de la 1.9.6** por el dueño: "funciona mucho mejor". Aún hay
  bajones de fps mirando ciudades muy pobladas (el hilo de dibujo no llega
  a tiempo en algunos pasos), pero se siente más estable.
- **Modos:** a petición del dueño se quitan los de la interpolación anterior.
  L3+R3: `16:9` → `4:3` → `ORIG`, los dos primeros con la interpolación
  nueva.
- **Europea:** la corrección del cielo baja las imágenes con cuña de 31 a 2
  (misión 3; en la 2, 2 de 744). Las 2 que quedan salen igual sin la
  corrección, y en 4:3 no aparecen (allí la línea termina justo en la
  esquina). `SIN RESOLVER`. La americana, la de referencia, queda en 0.
- **Publicada como `v1.9-alfa`** (`versionCode` 21):
  - la rama `etapa-8` se integra en `develop` y en `main`;
  - la release adjunta el APK con la auditoría a 0 y su SHA-256.

**Siguiente:** el rendimiento (análisis de llevar el dibujo a la GPU, en
`STATUS.md`).

## Rendimiento: en qué se va el tiempo (después de la v1.9-alfa)

**Medida en el PC** (misión 0 US, ciudad, 16:9, `AC3_PACE=60 AC3_RR=1`,
desde el VBlank 2700; `rr:` al salir del binario libre):

| Hilo | Ocupado por paso (33,3 ms) | Reparto |
|---|---|---|
| Juego | 18,8 ms | intérprete y capa ~10,2; **dibujo de la imagen real por software 6,7**; emparejamiento 1,3; copias 0,6 |
| Dibujo 0 | ~16 ms | **redibujo (intérprete) 8,4**; **dibujo de la intermedia por software ~7,6** |
| Dibujo 1 y 2 | ~0 | parados: con la interpolación nueva solo trabaja el 0 |

- Cerca de la mitad del trabajo es rasterizar por software (la GPU de la
  PS1 emulada en la CPU). La otra mitad es interpretar MIPS: la lógica del
  juego y el redibujo.
- En el aparato faltan las cifras por hilo: el contador solo da `CPU` del
  hilo del juego. `SIN RESOLVER` hasta medir en la Retroid.

## Paso 1: dibujo por la GPU a resolución original (rama `etapa-8-paso-1`)

**Decisión del dueño:** ir a la GPU, no repartir el software entre hilos,
porque después viene la alta resolución.

**Qué se integra:**
- el dibujo por GLES de la etapa 6 (pasos 17-19, `psx/gl/psx_hw.c`, validado
  con el comparador), que estaba en la app hasta el paso 19 (commit
  `fb7fd72`; la rama `gles-alta-resolucion` se borró: su contenido está en
  `develop`);
- a escala 1 y con el tramado de la PS1: la misma imagen que por software;
- con la interpolación nueva, el hilo de dibujo solo **anota** la intermedia
  (`psx_gpu_record` con `only`): no la rasteriza. La GPU la dibuja en el hilo
  de presentación;
- la imagen real sigue dibujándose también por software en el hilo del
  juego, porque la memoria de vídeo del juego es la del software.

**Modos de la app (temporal, para comparar):** L3+R3 recorre `GPU 16:9` →
`SW 16:9` → `GPU 4:3` → `ORIG`.
- El registro (`adb logcat -s AC3Game`) da cada segundo el hilo de dibujo 0,
  con el redibujo aparte.
- También da los ms de GLES por imagen.

**Comparador** (`AC3_RR=1 AC3_WIDE=1 AC3_INTERP=1`, `tools/hwcmp.py`, 41
misiones): 140.538 listas (intermedias incluidas), 4 con bloques malos (7
bloques).
- **Misión 7, VBlank 2763** (se reproduce): una estela de triángulos muy
  finos que GLES rasteriza un píxel al lado. Es el mismo tipo de caso límite
  que la línea del túnel de la etapa 6.
- **3 en menús** (misiones 5, 6 y 8): puntos sueltos que no se reproducen al
  repetir la misión. `SIN RESOLVER`.
- **Misión 37:** 86 % de píxeles idénticos y 201 listas por debajo del 99 %
  de casi iguales. Sin interpolación ya da 89,8 % y 131: es el túnel. Las
  intermedias solo añaden listas del mismo tipo.
- Todas las misiones terminan sin fallos.

**Medida en el PC** (misión 0, ciudad, `AC3_PACE=60`; `AC3_HW_NULL=1` anota
las listas y las tira, sin GLES):

| | Software | GPU |
|---|---|---|
| Hilo de dibujo 0 | ~17 ms (redibujo 8,7 + rasterizado 7,6) | ~9,5 ms (redibujo 8,9 + anotar 0,6) |
| Hilo del juego | ~19 ms (rasterizado de la imagen real 6,5-6,8) | igual |

- El rasterizado se mide aparte (`rr: rasterizado por software`).
- **Cuidado:** `AC3_HW_NULL=` vacío también lo activa (`getenv`): una primera
  medida salió mal por eso.

**APK 1.10.1-alfa** (prueba, sin publicar).

**Siguiente:**
- según lo que dé la Retroid, la imagen real solo en la GPU (quitaría el
  rasterizado del hilo del juego, unos 7 ms en el PC). Exige seguir qué
  partes de la memoria de vídeo lee el juego;
- después, la escala (alta resolución).

## Paso 2: la imagen real solo en la GPU (rama `etapa-8-paso-2`)

**Objetivo:** quitar del hilo del juego el rasterizado por software de la
imagen real (~6,5 ms por paso en la ciudad de la misión 0, en el PC).

**Cómo** (el diseño de los renderizadores por hardware de los emuladores):
- **Software** (`psx_gpu_record` con `only`): en la misión, la lista solo se
  interpreta. Lo de fuera de la imagen (texturas, paletas) se sigue dibujando
  y sube a la GPU como hasta ahora.
- **GPU** (`psx/gl/psx_hw.c`, `psx_hw_frame_ex`):
  - conserva cada imagen de la VRAM (el doble búfer): color, un segundo
    búfer con el bit 15 de cada píxel y el stencil para la máscara;
  - la real se dibuja sobre la suya; la intermedia, sobre una copia.
  - Si el software no tiene la imagen al día (`main_stale`), parte de la
    suya y no de la VRAM del software.
- **Seguimiento** (`psx/src/gpu.c`):
  - las zonas que solo tiene la GPU, por hilo (el del juego);
  - cuenta lo que las lee o escribe por otro camino (`psx_gpu_stale_ev`);
  - **escrituras fuera de las listas** (LoadImage, ClearImage, MoveImage): se
    pasan a la GPU con la siguiente lista (subida y copia a la imagen);
  - **lecturas como textura:** la lista apunta las zonas (`read`), y la GPU
    copia antes su imagen a su textura de la VRAM, convertida a 15 bits con
    el bit 15 de su segundo búfer (`read_zones`).
- **App:** solo en la misión con la GPU (`psx_interp_hw_main`). La
  presentación dibuja también las imágenes reales que se salta, para que la
  imagen conservada no se atrase.

**Lo que salió en las 41 misiones** (`AC3_HW_NULL=1 AC3_HW_MAIN=1`, binario
libre):
- sin fallos;
- lecturas de la imagen como textura en 7 misiones (232 u 80):
  - es el efecto de "MISSION FAILED", que refleja la imagen anterior (misión
    10, VBlank 3455: cuadriláteros de 160×120 con la página del otro búfer, a
    15 bits);
  - sin el bit 15 en la copia salía casi negro, porque el reflejo es
    semitransparente por texel;
- una escritura fuera de las listas (misión 16).

**Comparador** (`AC3_HWCMP_ONLY=1`): una copia de la GPU por software dibuja
la lista entera como referencia, mientras la GPU parte de lo que conserva.
- Misión 10, reflejo incluido: **0 listas malas** de 3.131.
- **Las 41 misiones: 127.072 listas, 3 con bloques malos (6 bloques).**
  - Misión 7, VBlank 2763: la estela de triángulos finos (la misma que con
    el dibujo GPU normal).
  - Misiones 2 y 40, en el choque del guion: un matiz de sombreado de pocos
    píxeles encima del radar. Puede ser un hueco sin cubrir que hereda una
    diferencia de una imagen anterior. `SIN RESOLVER`, menor.
- `AC3_HWCMP_PERSIST=1`: solo prueba partir de la imagen conservada (con el
  software completo).

**Medida en el PC** (misión 0, `AC3_PACE=60`): el rasterizado de la imagen
real pasa de 6,45 ms a **0,45 ms**. El total del hilo del juego, a ritmo real,
lo enmascara el escalado de frecuencia de la CPU (18,8 → 16,9 ms).

## Paso 3: lo que se mueve, en la interpolación nueva (rama `etapa-8-paso-3`)

**Petición del dueño:** llevar a la interpolación nueva lo que falta (el avión,
los proyectiles…). También vio un fallo: en la repetición del final de misión,
el avión enfocado sale doble y parpadeando.

### 3a: la repetición y la orientación de los objetos

- **Reproducido** con la demostración del título (US, sin guion, VBlank
  ~5700-6950: la misma cámara de repetición).
  - Con `AC3_RR_NODRAW=1` (se quita lo estático sin redibujarlo) el avión
    sale bien: forma parte del dibujo estático.
  - **Encuesta** (`AC3_RR_SURVEY=1`) en la demostración: `g_render+0x3C4`,
    con la forma de una entrada del arreglo de objetos (posición +0/+4/+8,
    ángulos en shorts +0x10/+0x12/+0x14), cambia en casi cada paso. Es el
    avión que sigue la cámara, y no se interpolaba: la cámara iba a medio
    camino y el avión no.
  - También cambia la orientación de las entradas del arreglo (+0x10/+0x14).
- **Corrección** (`fields_init`): 33 objetos (el de `+0x3C4` y los 32 del
  arreglo), con posición y tres ángulos.
  - Los ángulos (`F_A16`) van por el camino corto: en 16 bits si la
    diferencia es pequeña; si no, en una vuelta de 4096.
  - Un objeto que salta de sitio se queda como en el paso actual.
- **Medida** (`tools/entre_medias.py`): cada intermedia debe quedar entre sus
  dos reales (d(I,A) y d(I,B) menores que d(A,B)).
  - Antes: 0 de 6 (d(I,·) ~16 con d(A,B) ~5).
  - Después: **504 de 504** en VBlank 5700-6950, y 2 pasos sin intermedia
    (cortes de cámara).
- **Sin romper nada:** autoprueba con t = 1 en las misiones 0, 10 y 24, 0
  imágenes distintas.

### 3b: aviones, misiles y efectos de la misión

**Lo que se averiguó:**
- En la misión, la lógica y el dibujo de los objetos móviles van mezclados.
  - Los subsistemas que llama la lógica del vuelo (US `0x80032e6c`; en la
    tabla de subpasos, `+0x9f230` de ACE.BIN) actualizan y dibujan en el
    mismo bucle. Por ejemplo, `FUN_8006b48c` mueve cada partícula y la
    dibuja.
  - Volver a dibujarlos con el código del juego exigiría separar el dibujo
    de la lógica de cada subsistema: **no es viable por ahora**.
  - Lo móvil sigue con la interpolación en pantalla (emparejamiento por
    vértices), dentro de la interpolación nueva.
- **Prueba de verdad conocida con hilos** (`AC3_INTERP_TEST`, arreglada):
  - con hilos de dibujo, la intermedia llega después de la real siguiente, y
    el comparador emparejaba cada intermedia con el caso anterior; ahora la
    real espera a su intermedia;
  - con la interpolación nueva, interpola entre los dos pasos de referencia
    (`psx_interp_next_is_ref`);
  - `AC3_INTERP_TEST_FROM` y `AC3_INTERP_TEST_DUMP=v1,v2`.
- **Misión 0, todo el vuelo, a ritmo real:**

| | Intermedia | Vecinos reales |
|---|---|---|
| Interpolación antigua | 26,8 dB | 25,3 dB |
| Nueva | 29,2 dB | 25,3 dB |
| Nueva, con la corrección de abajo | 29,3 dB (peor caso −4,1 en vez de −8,0) | 25,3 dB |

**Fallo del paso 3a, corregido:** interpolar `g_render+0x3C4` en el vuelo
estiraba la estela del misil (la intermedia la dibujaba doble).
- En el vuelo, el dibujo estático usa esa entrada como origen de las
  estelas. Con `AC3_RR_NODRAW` la estela desaparecía, y sin interpolar esa
  entrada también.
- **Ahora** se interpola solo en los subpasos sin juego. Los marcan dos
  enganches: US `0x80032fac` y `0x80032ffc` (demostración y repetición; la
  demostración usa el segundo, comprobado con `AC3_TRACE`); EU `0x80032de4`
  y `0x80032e34`.
- Demostración: **570 de 570** intermedias entre medias.
- Máximo de enganches del intérprete: de 8 a 12.

**Probado y descartado: piezas enteras** (`PSX_INTERP_RIGID=1`).
- Cada pieza (matriz de la GTE) emparejada se interpolaba entera: rotación
  y traslación a medio camino, y sus vértices otra vez por la GTE.
  - La reconstrucción es exacta: con la matriz del propio paso sale lo
    registrado. Hizo falta pasar el contexto de la GTE que proyecta
    (`psx_gte_hook_ctx`): el de la librería tenía otro centro.
  - 62 piezas por paso en la misión 0.
- **Medida:** el PSNR no cambia, y los polígonos estirados aumentan
  (demostración, de 3.188 a 3.471; misión 2, de 1.831 a 2.773), con más pasos
  tomados por cortes. Una pieza que se mueve entera junto a vecinas que se
  mueven vértice a vértice abre costuras.
- Queda en el código, desactivada.

### Las llamas de los motores sobre el HUD (paso 2)

- **Informe del dueño:** al acelerar, las llamas deforman la línea verde
  del HUD y salen rectángulos verdes.
- **Reproducido** con `tools/misiones.py` `GUION_FUEGO` (vista exterior con
  SELECT y postquemador con R1), y con el comparador en el modo del paso 2:
  166 listas malas en la misión 0.
- **Causa:** el efecto de calor de los motores copia (VRAM → VRAM, 16×1) un
  trozo de la imagen que se está dibujando y lo vuelve a dibujar. Con la
  imagen solo en la GPU, el software copiaba una imagen vieja: 2.760 copias
  contadas.
  - Medido antes de concluir: el bit 15 de la GPU coincide con el del
    software (1-20 píxeles distintos por imagen), así que no era la máscara.
  - Además, al partir de una imagen conservada, el stencil se rehace desde el
    bit 15 exacto de su segundo búfer (`stencil_from_mask`).
- **Corrección:** antes de dibujar, se mira la lista
  (`psx_gpu_list_copies_from`). Si copia desde su imagen, la dibuja entera
  el software, y la copia lee lo recién dibujado. Ninguna imagen mal; solo
  se pierde el ahorro mientras dura el efecto.
- **Resultado:** 0 listas malas (de 2.794) y 0 copias de imágenes viejas.
- **Las 41 misiones con `GUION_FUEGO`** (comparador, modo del paso 2): 127.072
  listas, 5 con un bloque malo cada una; sin fallos.

## Rendimiento en la Retroid: medido con el registro (1.10.4 a 1.10.12)

La Retroid conectada por USB (`adb logcat -s AC3Game`), con el dueño volando
bajo sobre una ciudad. Cada versión añadía una medida.

1. **La CPU va sobrada.**
   - Hilo del juego al 23-46 %, hilo de dibujo a 4-8 ms por paso, ningún paso
     saltado.
   - Los bajones solo ocurrían en modo GPU, en el hilo que presenta.
2. **Cada imagen real se dibujaba dos veces en la GPU** (1.10.5): la ya
   mostrada seguía en la cola y se tomaba por "saltada". Corregido.
3. **Subidas a mitad de imagen** (1.10.8 y 1.10.10, modos de medida en
   L3+R3):
   - adelantarlas todas llevó la GPU de 17,8 a 12,7 ms por imagen, y de 54,6 a
     59,6 fps;
   - sin la mejora del paso 2: 39 fps; sin la pasada de máscara: igual.
   - En la GPU por baldosas de la Retroid, cada subida a mitad corta el
     dibujo.
   - **Atlas de subidas** (`psx/gl/psx_hw.c`): todas las de la lista van al
     principio a un atlas. Cada primitiva lleva en el vértice cuántas había
     antes de ella, y el shader lee la más reciente que cubre el texel (o la
     VRAM de partida). Comparador: 0 listas malas, 0 subidas a mitad.
4. **Ritmo de presentación** (1.10.12): quedaban bajones sin vueltas lentas
   ni retrasos, con intermedias tiradas.
   - Cada vuelta enseñaba la imagen vencida más reciente y tiraba las demás.
     Como la GPU despierta la vuelta (no el refresco), una intermedia y su
     real vencían a veces juntas.
   - **Ahora** se enseña la pendiente más antigua; solo con 3 o más
     atrasadas, se salta a las dos más nuevas.
   - **Resultado: 259 s en `GPU 16:9` sobre la ciudad, 60,0 fps de media, ni
     un segundo por debajo de 55 (mínimo 59).**
5. **Doble pasada**, limitada a lo que tapa una primitiva posterior que
   comprueba la máscara (`plan_exact`). Las sustractivas en una sola pasada
   (medida `GPU 1P`) no cambiaban nada: no hace falta.

El registro sigue dando por segundo:
- vueltas de presentación y tiempo de GPU de una imagen (`glFinish`);
- imágenes que llegan tarde y tiradas;
- el peor tiempo del hilo de dibujo;
- llamadas, subidas y motivos de corte de lote.

## Paso 4: alta resolución y filtro de texturas (rama `etapa-8-paso-4`, release v1.12-alfa)

**Qué hace** (`psx/gl/psx_hw.c`, `android_game/`):
- **Resolución interna 1x-4x**, elegida en las Opciones del menú (por
  defecto 4x: 1280×960 en 4:3).
- **A más de 1x, color de 24 bits sin tramado** (`psx_hw_truecolor`). Los
  degradados (gouraud, modulación) quedan suaves en vez de en escalones de 15
  bits con la trama 4x4 ampliada.
- **La imagen se presenta con filtro lineal** a más de 1x.
- **Filtro de texturas bilineal** (opción; por defecto, el de la PS1),
  hecho en el shader:
  - solo en los triángulos con textura: textos, menús y la mayor parte del
    HUD siguen nítidos;
  - cada vecino pasa por su paleta, y los transparentes (texel 0) no
    cuentan;
  - los vecinos no salen del rango de uv del polígono (no se mezcla con la
    VRAM de al lado).

**Medido en la Retroid (60 fps):**

| Configuración | fps de media | GPU por imagen |
|---|---|---|
| 4x sin filtro | 60,0 | 19,1 ms |
| 4x bilineal | 59,4 | 21,0 ms |
| 3x bilineal | 59,0 | 19,7 ms |
| 2x bilineal | 60,0 | 17,4 ms |

Las configuraciones las cambiaba el PC cada 20 s mientras el dueño jugaba,
por `adb` (ver *Depuración por adb*, abajo).

**Fallos de la alta resolución, corregidos:**
1. **Líneas oscuras alrededor del radar.** Las primitivas del radar son
   triángulos con la uv al revés (decrece hacia la derecha y hacia abajo).
   - La GPU lee la textura en la esquina de cada píxel y no dibuja los bordes
     derecho e inferior: nunca llega al texel del vértice.
   - A 4x, los subpíxeles de la última columna sí llegaban, y ese texel está
     fuera del mapa.
   - **Ahora** el rango de uv de cada triángulo es el que lee la GPU
     (`set_uvr_tri`): se mira en las esquinas cubiertas de los 3x3 píxeles de
     cada vértice.
   - Reproducido en el PC con `AC3_HWCMP_PROBE` y las capturas a escala: la
     columna 75 del radar, de 15 píxeles distintos a 0.
2. **Líneas (primitivas LINE) medio píxel desplazadas** a más de 1x: la franja
   se miraba en la esquina del píxel. Ahora cubre el píxel de la recta.
3. **Bajones con el filtro bilineal** (30-450 ms de GPU por imagen en
   explosiones). Cada lectura de textura recorría todas las subidas del
   atlas.
   - **Ahora** cada primitiva lleva solo hasta la última subida que toca su
     textura, su paleta o el origen de su copia (`epoch_of`); casi siempre
     ninguna.
   - Comparador: 0 listas malas.

**Herramientas:**
- `AC3_HWSHOT=escala,vb1,vb2,dir` (comparador): imagen de GLES a escala 1 y a
  la pedida, de cada lista entre esos VBlank.
  - Con `AC3_HWCMP_ONLY` o `_PERSIST`, conservando la imagen como la app.
  - `AC3_HWSHOT_FILTRO=1`: con el bilineal.
- **Depuración por adb**: `<externalDataPath>/depurar.txt` (en la Retroid,
  `/sdcard/Android/data/com.vzepec.ac3game/files/depurar.txt`), releído cada
  segundo:
  - `escala=N color24=0|1 solo=0|1 filtro=0|1 modo=0-3`;
  - permite probar hipótesis con el juego en pausa en el sitio del fallo y
    capturar con `adb exec-out screencap`.

**La estela del misil que parpadea junto a la cámara (a 60 fps): no es de la
interpolación.**
- Misión 2, disparo hacia delante con la cámara detrás. El trozo de estela
  junto a la cámara aparece y desaparece paso a paso.
- Píxeles brillantes de esa zona, VBlank 3316-3345: **la misma secuencia en
  ORIG** (sin interpolación, capturas por VBlank) **que en las imágenes
  reales** con interpolación. Las intermedias siguen a su real.
- Con 60 fps se nota más porque lo demás va fluido.
- **SIN RESOLVER:** si una PlayStation real hace lo mismo (comparar con otro
  emulador).

**Validación final** (comparador a 1x, las 41 misiones, intermedias y
`GUION_FUEGO`):
- 140.276 listas, 8 con un bloque malo;
- en las mismas misiones que en la v1.10-alfa (3, 7, 20, 22 y 23);
- sin fallos.

**Pendiente:**
- precisión de vértices (tipo PGXP), si se ven temblores;
- otros filtros (xBR).

