# Etapa 9: juego en red (experimental) — la campaña en cooperativo

Rama `etapa-9` (sale de `develop` tras la `v1.13.2-alfa`). Decisión del dueño
(2026-10-27), tras evaluar un versus (1vs1, 2vs2) y un cooperativo.

## Decisión y por qué

AC3 es solo para un jugador: no tiene versus, pantalla partida ni cable link.
Se evaluaron dos maneras de jugar en red:

| | Avión fantasma (cada uno su partida) | **Lockstep** (los dos simulan la misma) |
|---|---|---|
| Qué viaja | posiciones y sucesos (misil lanzado, impacto) | solo los mandos |
| Enemigos, guion, radio, vídeos | habría que sincronizarlo todo | iguales en los dos sin hacer nada |
| Requisito | ninguno | **la partida debe ser determinista** |
| Encaja con | versus (los enemigos son el rival) | **cooperativo** (los dos contra la IA) |

**Elegido: cooperativo de la campaña por lockstep.** En el cooperativo el
segundo avión es «otro jugador más» contra la IA: sus misiles dan a los
enemigos y los misiles enemigos le dan con la lógica que ya existe. En el
versus habría que inventar misiles de jugador contra jugador.

**Sin servidor propio:** el lockstep va de un aparato a otro (uno hace de
anfitrión). Misma Wi-Fi, zona Wi-Fi de un móvil o IP directa no necesitan
servidor; para Internet, un código para compartir con STUN público. Un
repetidor (datos móviles con CGNAT) sería lo último y opcional.

### Precedentes (GitHub, octubre de 2026)

- **Mega Man X4 Recompiled** (`mstan/MegaManX4Recomp`, MIT): añadió un segundo
  jugador (X + Zero) a un juego de PS1 de un jugador, en local y en red, por
  lockstep con retraso de 2 pasos (`recomp-net`) y comparación de sumas de la
  RAM. Es el modelo más cercano. Sigue en alfa tras unos 4 meses.
- **recomp-net** (`RetroPortingToolKit/recomp-net`, MIT, C11): lockstep con
  retraso fijo, UDP en LAN, ICE (libjuice, MPL-2.0), repetidor del anfitrión;
  **recomp-net-server** (MIT, Rust): salas y señalización. Reutilizables.
- **Ace Combat 3 Japan Recomp** (`omegakatana92/...`, GPL-3.0): andamiaje de
  psxrecomp para la japonesa, 3 commits, `players = 1`. No sirve de base.
- OnlineCTR (CTR ya traía multijugador) y sm64coopdx (SM64, sincroniza
  estado sobre una decompilación completa): referencias de diseño.

## Plan

| Etapa | Paso | Qué | Estado |
|---|---|---|---|
| 9 | 1 | **Determinismo:** mismos mandos → misma RAM, con opciones distintas, en el PC y en el aparato; S24 = Retroid | ✅ |
| 9 | 2 | El avión del jugador: estructura, función de vuelo y todo lo que lo usa (cámara, HUD, radar, daño, fin de misión) | ✅ |
| 9 | 3 | Un segundo avión con un segundo mando, en el PC, en la misma pantalla | |
| 9 | 4 | La IA elige entre los dos; daño, caída y reaparición del segundo | |
| 9 | 5 | Cada jugador con su vista sin romper el lockstep | |
| 10 | 6-7 | Lockstep en red local (recomp-net); menú Crear / Unirse | |
| 11 | 8-9 | Internet (código para compartir); las 36 misiones en cooperativo | |

Los pasos 1 y 2 deciden la viabilidad.

## Paso 1: determinismo (rama `etapa-9-paso-1`)

### Cómo se mide

- **Huella de la RAM** (`psx_mem_hash`, `psx/src/mem.c`): en cada VBlank, 32
  bloques de 64 KB, el scratchpad y la **semilla de `rand`** (estado de la
  partida fuera de la RAM; ver abajo).
- **PC:** `AC3_RAMHASH=archivo` en el binario libre. `AC3_SPU=1` hace avanzar
  la SPU un VBlank de audio por VBlank, como el APK: el juego lee el estado de
  las voces (`SpuGetAllKeysStatus`) y, sin ella, el PC no se parece al APK
  (desde el VBlank 304, `0x800C41A2`).
- **`tools/determinismo.py`** (`make determinismo MISION=n`): la misma misión
  con el mismo guion de mando (`tools/misiones.py`) y una configuración por
  ejecución; compara cada una con la base. `--detalle` vuelca la RAM en el
  primer VBlank distinto y lista los tramos. **Control positivo:**
  `control_mando` (un misil más en el VBlank 3000) debe salir distinta.
- **Aparato:** claves de `depurar.txt` del APK de desarrollo: `huella=1` (la
  huella en `huella.bin`), `guion=1` (el mando lo pone solo el guion del PC;
  `guion=2`, el control) y `mision=N` (sin la lista).
  `tools/movil_determinismo.sh <dir> [mision]` lo hace todo por adb y deja el
  aparato como estaba.

### Hallazgo: el redibujo adelantaba la semilla de `rand` del juego

- Primera medida, misión 37 (US): con intermedias, desde el VBlank 2492 la
  partida era otra (una tabla de entradas de 0x14 bytes desde `0x801316CC`).
  En la 19, a 120, una diferencia en el VBlank 3611 que **no se repetía** al
  volver a ejecutar.
- `AC3_WATCHWRITE=801316CC`: la escribe `FUN_80082f68` (sprites de ruido con
  `rand() & 0x7c7e`): 0x1001 sin intermedias, 0x7E40 con ellas.
- Causa: el redibujo ejecuta código del juego que llama a `rand`. La copia
  del intérprete (`rrw_`) no incluía `libc_psx.c`: compartía la semilla con
  la partida. El redibujo síncrono (`AC3_RR=2`) la usaba en el hilo del juego
  y restauraba RAM, scratchpad y GTE, pero no la semilla. Solo pasa con
  redibujo **y** hilos de dibujo (sin hilos no se redibuja).
- **Arreglo:** `libc_psx.c` entra en la copia (Makefile y
  `android_game/build.sh`). El redibujo parte de la semilla de S0 (la del
  inicio del dibujo estático, `mark0`) y, en el hilo del juego, la de la
  partida vuelve a ser la que era (`ac3_rr_build`, `ac3_rr_build_from`,
  `RrSnap.rand_seed`).
- **Efecto en la imagen:** la autoprueba t = 1 (redibujo frente al dibujo
  real, cada paso desde el VBlank 2500, las 41 misiones US) pasa de **805
  imágenes distintas de 28.310 a 0** (antes: misiones 8, 17, 19, 22 y 25, con
  9, 95, 13, 444 y 244). En la 19 era el `SIN RESOLVER` del HUD y el radar
  (paso 12), que se atribuía a la pila sin inicializar: era el ruido, dibujado
  con otros números. Las intermedias de esas misiones deberían verse más
  limpias (por probar jugando).

### Resultados

**PC** (US salvo donde se dice; `--apk`; 4.000 VBlank, 5.100 en la EU):

| Configuración | Misiones 0, 19, 37 (US) y 0 (EU) | Las 41 (US) |
|---|---|---|
| la misma otra vez | igual | |
| control (un misil más) | **distinta** (desde el 3001-3002) | **distinta** |
| intermedias a 60 (redibujo, 1 hilo) | igual | |
| intermedias a 120 (3 hilos) | igual | igual |
| imagen solo en la GPU (`AC3_HW_NULL` + `AC3_HW_MAIN`) | igual | |
| ritmo real con hilos (`AC3_PACE=60`) | igual | |
| 16:9 (`AC3_WIDE=1`) | distinta en 0 y 19 (EU 0); igual en 37 | |

**Retroid Pocket Flip 2** (APK de desarrollo 1.13.3-alfa, misión 0, 4.110
VBlank):

| Configuración | Resultado |
|---|---|
| la misma otra vez (3 ejecuciones) | igual |
| control (`guion=2`) | **distinta** desde el VBlank 3001, también en la semilla |
| sin intermedias | igual |
| escala 1x | igual |
| 4:3 (`modo=2`) y original (`modo=3`) | distinta desde el 2569, en los mismos bloques que el PC |

(`modo=1` también es 16:9: `MODE_WIDE` incluye los modos 0 y 1.)

**Galaxy S24** (SM-S921B, Exynos, pantalla de 120 Hz; mismo APK, misión 0):
la misma otra vez, 60 Hz (`hz=60`), sin intermedias y escala 1x, iguales; el
control, distinto desde el 3001; 4:3 y original, distintos desde el 2569.

**Aparato contra aparato** (el caso del juego en red): la base del S24 y la de
la Retroid (Snapdragon 865, 60 Hz), **iguales en los 4.095 VBlank**, con dos
ejecuciones de la Retroid en días distintos; y el 4:3 del S24 igual al 4:3 de
la Retroid. El control del S24 frente a la base, distinto.

### Conclusiones

- **La partida es determinista** en el PC y en la Retroid, y las opciones de
  imagen (intermedias, 60/120, resolución, GPU, ritmo) no la cambian, una vez
  arreglada la semilla.
- **El 16:9 sí cambia la RAM:** sus ganchos (cono de visión del terreno,
  líneas del cielo) alteran lo que el juego calcula de la imagen. En la misión
  0, la cámara y el avión acaban igual (`AC3_DUMPMEM` en el VBlank 3990: misma
  posición) pero 74 KB de datos difieren. **En red, el anfitrión fija 16:9 o
  4:3 para los dos.**
- **La SPU es parte de la partida:** todos los aparatos deben hacerla avanzar
  igual (el APK ya lo hace en cada VBlank, en el hilo del juego).
- PC y APK no se comparan directamente: aplican el mando y anotan la huella en
  puntos distintos del VBlank (coinciden 1.920 de 4.000 VBlank con un
  desplazamiento de uno). Para jugar en red solo cuenta aparato contra aparato.
- **Dos aparatos distintos dan la misma partida** (S24 y Retroid: otro
  procesador, otra pantalla, mismo APK). Con dos aparatos conectados,
  `ADB="adb -s <serie>" tools/movil_determinismo.sh ...` en cada uno.
- **Pendiente:** la versión europea en el aparato.

**Reproducir:**
`make determinismo MISION=37` · `python3 tools/determinismo.py "<US>" <dir> --mision 19 --apk --detalle` ·
`tools/movil_determinismo.sh <dir> 0` (APK de desarrollo instalado).

## Paso 2: el avión del jugador (rama `etapa-9-paso-2`)

Todo en la versión americana; `tools/entidades.py` lo reproduce (tabla de
entidades, contexto del jugador y mandos en un VBlank de una misión).
Herramientas nuevas del binario libre: `AC3_WATCHWRITE_FROM=v` (vigilar
escrituras desde un VBlank) y `AC3_PAD2_ID` / `AC3_PAD2_BOTONES` (un mando en
el puerto 2).

### Las entidades

- `*0x800BB824` apunta a una **tabla de 64 punteros a entidad**
  (`g_render+0x2BC`): 0-31 aviones (la 0, el jugador), 32-63 objetos de
  tierra (ya visto en `docs/04-formatos.md`, tablas de aparición).
- **La entidad del jugador** está en `g_render+0x3BC` (`0x80134F68` en la
  misión 0; mide 0x58 bytes, las demás de avión 0x5C):

| Campo | Qué es |
|---|---|
| `+0` | banderas: bits 0-5 la ranura, `0x40/0x80/0x100` el bando, `0x4000`/`0x8000` activa |
| `+4` | modelo |
| `+8 / +0xC / +0x10` | posición (coma fija << 12) |
| `+0x18 / +0x1A / +0x1C` | ángulos (0x4000 = 360°) |
| `+0x38 / +0x3C / +0x40` | velocidad (se suma a la posición) |
| `+0x48` | tipo (1 el jugador; 0x18, 0x1F, 0x20 cazas; 0x40, 0x41 tierra) |
| `+0x4E` | **vida** (jugador 100, cazas 140, tierra 60) |
| `+0x54` | impactos recibidos (bits 0-5 y 6-14: dos clases de daño) e integración |

- **La ranura 1 de avión está libre en las 41 misiones** (VBlank 3200,
  `tools/entidades.py --todas`); la misión con más aviones usa hasta la 28.
  Sitio natural para el segundo avión. `SIN RESOLVER`: si alguna misión la
  ocupa más tarde, y por qué la tabla de aparición empieza en la 2.

### El contexto del jugador

Es pequeño y está concentrado:

| Qué | Dónde (US) |
|---|---|
| entidad 0 | `*(*0x800BB824)` = `g_render+0x3BC` |
| control y vuelo | `DAT_800BBA74` → 0x70 bytes (`0x8016670C`; los reserva `FUN_8006b7b4` con el de abajo) |
| estado (HUD, avisos, datos de la cámara) | `DAT_800BBA78` → 0x3C bytes (`0x8016677C`) |
| entrada del juego | `g_state+0x120` (`0x800BDDD0`): clase de mando (`+4`), acciones (`+8`), máscara de ejes (`+0x18`), 6 ejes desde `+0x1C` (cabeceo, alabeo… ±4096) |
| mando crudo | `0x800BED38` (`*0x800BB464`) |

**Los mandos:** el módulo de entrada del juego (`FUN_80026b38`,
`FUN_80026858`) lee **los dos puertos, con multitap: 8 ranuras**, y deja cada
una en `g_state+0x50 + k·0x1A` (tipo `+0xC`, botones `+0xE`, recién
pulsados `+0x10`, palancas `+0x12..+0x18`). El bucle principal solo traduce
la ranura 0 a la entrada del juego: `FUN_800288d8(g_state+0x50,
g_state+0x120, g_state+0x15C)` (la configuración de botones en `+0x15C`).
**Un mando en el puerto 2 llega a la ranura 4** (`AC3_PAD2_ID=73
AC3_PAD2_BOTONES=0x2008`: tipo 0x73 y botones 0x2008 en `0x800BDD68`): la
entrada del segundo jugador puede ir por donde iría en una PlayStation.

### El paso de vuelo

`FUN_80032e70` (y sus variantes `FUN_80033034`, `FUN_80033114`; Ghidra empieza
mal estas funciones), cuando no hay pausa (`0x20000000`):

1. `FUN_800715fc` — **el jugador** (429 líneas): mandos (`FUN_8006bb4c`,
   llamada desde ahí), vuelo y `FUN_80070d98` (daño a la vida `+0x4E` según
   `+0x54`; choque con el suelo, `FUN_8006fe1c`, que quita 3, 5 o 100 según
   `g_state+0x398`; avisos de altitud en `DAT_800BBA78[9]`).
2. `FUN_80044f70` — incluye **la puntería del jugador** (`FUN_80044950`):
   distancia y ángulo de cada entidad al morro del jugador, en tablas
   paralelas por entidad (`0x800C57F0`, `0x800C58F8`): radar (< 34.000),
   fijable (< 0x8000), en el cono (< 0x600).
3. `FUN_80038950` — los aviones: `FUN_80038024` (banderas por avión),
   `FUN_80037fc8` (derribos —`FUN_80045be0`: vida < 1, cuenta de derribos en
   `DAT_800BB87C+0x20`— y la IA), `FUN_800382c4` (mueve los 32 aviones con su
   velocidad; ajustes por misión según `DAT_800BBA0C`).
4. `FUN_800503b0`, `FUN_80045b70` (graba la ruta del jugador cada 120 pasos
   en `g_state+0x3BC`, `FUN_80047b2c`), `FUN_8006b088`.
5. `FUN_8007c6e4` — la cola de eventos (la de las demos) y `FUN_80083cfc`
   (efectos; los motores del jugador, `FUN_8008eb78`).
6. `FUN_80039f10` — la cámara, con datos de `DAT_800BBA78`.

### Lo que da por hecho un solo jugador

En vuelo se ejecutan 34 funciones que usan la entidad 0 o las dos estructuras
del jugador (cobertura de las misiones 0 y 6 entre los VBlank 2380 y 4000,
cruzada con el código de Ghidra):

| Grupo | Funciones |
|---|---|
| el jugador (vuelo, daño, armas) | `800715fc`, `80070d98`, `80070d34`, `80071558`, `800713a0`, `80070500`, `80074e9c`, `80074ef8`, `80074ff4`, `8006b604`, `8006b944` |
| puntería y radar | `80044950`, `80041c88` |
| cámara | `80078100`, `8007818c`, `8007a03c`, `80070390` |
| HUD | `80033744` (lee la vida), `80081ad0` |
| dibujo del avión y efectos | `80063144`, `80064d8c`, `80083cfc`, `80083bac`, `8008eb78` |
| guion de misión (altitud, distancia al jugador) | `80050e6c`, `80052c88`, `8003a81c`, `8003703c` |
| registro (ruta, eventos) | `800479e8`, `80047b2c`, `80038980` |
| `SIN RESOLVER` | `8003d46c`, `8004a9a4`, `8006b7b4` (reserva) |

### Viabilidad

**A favor:**
- hay una ranura de avión libre en todas las misiones;
- el juego ya lee el puerto 2;
- el contexto del jugador cabe en cuatro sitios (entidad, 0x70, 0x3C y la
  entrada);
- todo el jugador cuelga de una llamada por paso (`FUN_800715fc`).

**Propuesta para el paso 3 (segundo avión por cambio de contexto):**
1. Al empezar la misión, activar la ranura 1 como copia de la entidad 0 y
   reservar sus propios 0x70 + 0x3C bytes.
2. En cada paso, tras el jugador 1, cambiar el contexto: la tabla `[0]` a la
   entidad 1, `DAT_800BBA74/78` a sus copias y la entrada a la ranura 4
   (`FUN_800288d8` con su salida).
3. Volver a llamar a `FUN_800715fc` y restaurar.

**Riesgos (`SIN RESOLVER`):**
- a quién eligen como blanco los enemigos (si solo al jugador 1, el
  cooperativo funciona igual al principio);
- la puntería, el radar, el HUD y la cámara son del jugador 1, y el segundo
  necesitaría sus copias;
- el guion de misión mira solo al jugador 1;
- cómo se dibuja la entidad 1 (el modelo del jugador lo dibuja un código
  propio);
- cada jugador con su vista sin romper el lockstep (paso 5).

**Reproducir:**
- `python3 tools/entidades.py "<US>" --mision 0 --vblank 2950`
- `python3 tools/entidades.py "<US>" --todas`
- `AC3_WATCHWRITE=80134F70 AC3_WATCHWRITE_FROM=3000` (quién mueve la entidad 0)
- `AC3_WATCHREAD=800bddec AC3_WATCHREAD_FROM=3050` (quién lee el cabeceo)
