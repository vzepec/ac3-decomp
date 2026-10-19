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
