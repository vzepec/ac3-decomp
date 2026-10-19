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
