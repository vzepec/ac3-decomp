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
