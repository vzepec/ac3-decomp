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
| 9 | 3 | Un segundo avión con un segundo mando, en el PC, en la misma pantalla | ✅ vuela, se dibuja y dispara |
| 9 | 4 | La IA elige entre los dos; daño, caída y reaparición del segundo | ✅ órdenes, caída y reaparición, disparo, daño y derribos; `SIN RESOLVER`: misiles guiados, estelas, IA |
| 9 | 5 | Cada jugador con su vista sin romper el lockstep | ✅ en el PC: su cámara, su blanco y su fijado (misiles guiados) y su imagen, con la misma partida |
| 9 | 6 | El cooperativo en el APK: dos aparatos, cada uno con la imagen de su jugador, la misma partida | ✅ Retroid y S24 |
| 10 | 7-8 | Lockstep en red local; menú Crear / Unirse (`docs/16-etapa10.md`) | ✅ paso 7: PC↔PC y PC↔Retroid, jugado |
| 11 | 9-10 | Internet (código para compartir); las 36 misiones en cooperativo | |

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

## Paso 3: el segundo avión, en el PC (rama `etapa-9-paso-3`)

### Cómo

`psx/recomp/ac3_coop.c` (solo en el binario del PC por ahora;
`AC3_COOP=1`). Un gancho a la entrada de `FUN_80044f70` (la función del paso
que va tras el jugador) hace, en cada paso de vuelo:

1. **la primera vez en la misión** (entidad 1 inactiva):
   - reserva, con el asignador del juego (`g_state+0x1C`), los seis bloques
     del jugador 2 y su entrada, como copia de los del 1;
   - activa la entidad 1 como copia de la 0 (ranura 1 en las banderas),
     desplazada `AC3_COOP_POS=dx,dz` unidades;
2. traduce el mando del puerto 2 (ranura 4) a la entrada del jugador 2 con
   la función del juego (`FUN_800288d8`);
3. **cambio de contexto:** la tabla `[0]` ↔ `[1]`, los seis punteros
   `DAT_800BBA68..80` y la entrada `g_state+0x120`;
4. llama a `FUN_800715fc` y lo devuelve todo al jugador 1.

Mandos de prueba: `AC3_PAD2_ID=73` y `AC3_GUION2="vblank:botón:duración,..."`
(mismo formato que el guion del jugador 1). `AC3_COOP_LOG=n`: posición,
ángulos, velocidad y vida de los dos cada n pasos.

**El modo del jugador** (`A+0`, `A+4`: piloto automático del inicio,
control…) lo cambian órdenes de la misión que solo llegan al bloque del
jugador 1 (`FUN_80074e9c`, `FUN_80074ef8`…). Sin más, el jugador 2 se quedaba
en el piloto automático y no obedecía. Por ahora copia esas dos palabras del
1 mientras vive. `SIN RESOLVER`: ejecutar esas órdenes también para él.

### Resultados

- **Vuela con su mando:** alabeo, rumbo y cabeceo siguen a su guion
  (`AC3_COOP_LOG`).
- **Se dibuja con el modelo y las texturas del jugador.** No hace falta nada
  nuestro: el recolector de objetos (`FUN_80065c18`) recorre las ranuras 0-31
  y solo salta la entidad en la que va la cámara (`cámara+0xC4`).
- **Dispara misiles desde su avión:** el misil (tipo 0x1801, ranuras
  `DAT_800BBA60+200+k·0xA4`, creadas por `FUN_8006b18c`) nace a 65 unidades
  del jugador 2 y a 295 del 1, y avanza en su rumbo. No daña al jugador 1.
- **Dispara la metralleta (✕) desde su avión:** las balas
  (`0x80164758..`) forman una hilera que arranca a 9 unidades del jugador 2,
  en su rumbo.
- **El jugador 1 no cambia:** en las 41 misiones su entidad es igual byte a
  byte en el VBlank 3600 con y sin jugador 2 (`tools/coop.py`).
- **Las 41 misiones, sin fallos del intérprete:**
  - 36 activan al jugador 2 antes del VBlank 3600;
  - la 1, 12, 16, 29 y 32 tienen una introducción más larga y lo activan
    entre el 3802 y el 7686.
- **Determinista:**
  - dos ejecuciones iguales dan la misma partida en los 4.000 VBlank, también
    con intermedias a 120;
  - un misil más del jugador 2 da otra (desde el VBlank 3202).

### Lo que falta (pasos 4 y 5)

- **Efectos atados a la vista del jugador 1:** cuando dispara el 2, en la
  cabina del 1 se ve la ráfaga de la metralleta (el mismo efecto que cuando
  dispara el 1), y las estelas de sus misiles salen de la posición del 1.
- El modo del jugador 2, copiado del 1 (ver arriba).
- Su puntería, radar y HUD: `FUN_80044950` y el HUD calculan solo para el 1.
- La IA, el daño recibido, la caída y la reaparición del jugador 2, y el fin
  de misión (en muchas misiones el guion de prueba lo estrella: vida −2).
- La vista propia del jugador 2.

**Reproducir:**
- `python3 tools/coop.py "<US>" <dir> 0-40 4`
- `AC3_COOP=1 AC3_COOP_POS=-90,90 AC3_PAD2_ID=73 AC3_GUION2="2760:cruz:40,2800:circulo:4"`
  con el guion de `tools/misiones.py` y la misión 0 (capturas cada 4 VBlank:
  el avión 2 delante del 1, disparando).

## Paso 4: el combate del jugador 2 (rama `etapa-9-paso-4`)

Todo en `psx/recomp/ac3_coop.c`, en el PC. Herramientas de medida nuevas:
- `AC3_COOP_DIFF=v0,v1`: lo que el paso del jugador 2 escribe fuera de lo
  suyo;
- `AC3_COOP_SONDA=función,vblank`: lo que escribe una función llamada como
  el jugador 2, deshaciéndolo después;
- `AC3_COOP_PUNTERIA_LOG=k`: su puntería sobre la entidad k.

Para colocarlo en las pruebas:
- `AC3_COOP_DESDE=v`: aparece en ese VBlank;
- `AC3_COOP_TRAS=k`: detrás de la entidad k;
- `AC3_COOP_GIRO=grados`: además, girado.

### Las órdenes de la misión, también para él

Las cinco órdenes que cambian el modo del jugador (`FUN_80074e9c`, `eb8`,
`ed8`, `ef8` y `f18`: piloto automático del inicio, control, fin…) las llama
la misión (`FUN_8003703c`, `FUN_80059e1c`, `FUN_8005a438`, `FUN_80075a14`).
Un gancho a su entrada las ejecuta antes también como el jugador 2, con los
mismos argumentos. Sustituye a la copia del modo del paso 3: su modo pasa de
0x800/4 (inicio) a 1/1 (control) por la orden del juego.

### Al caer, la partida no termina

Medido con `AC3_COOP_DIFF` en el choque (VBlank 3201, misión 0), el paso del
jugador 2 cambiaba:
- la cámara (`g_render+0x28` → modo 0x100 → 0x501, objetivo `+0x34` a la
  entidad 1, `+0xC4/+0xC8`): se iba a sus restos;
- `g_render+0` (2 → 3) y `0x800BE60C` (0 → 1): jugador derribado;
- `0x800BB868`: apagaba los canales de sonido del motor del 1
  (`FUN_8003012c`).

Y en cada paso, sus copias del control (`0x800C6508..14`, `FUN_80070d34`) y
`g_render+0x28C`.

Ahora todo eso se guarda antes del paso del 2 y se devuelve después
(`AC3_COOP_VISTA1=0` lo desactiva). Lo que sigue cambiando es legítimo:
sonidos, explosiones y la cola de eventos.

**Reaparición:** con vida < 1 durante `AC3_COOP_REVIVE` pasos (150, 5 s), y
mientras el 1 viva, reaparece junto al jugador 1 como al empezar.

- **Medido:** se estrella en el VBlank 3201, sus restos humean en el suelo y
  reaparece en el 3503 con vida 100 y en modo control.
- La vista del jugador 1 sigue en su cabina y la misión continúa.

### Disparo, daño y derribos

- **Metralleta:**
  - la ráfaga que se veía en la cabina del 1 era el fogonazo «del jugador»
    (`FUN_8008eb78`), con su estado en `DAT_800BB958+0x3948`, que activa
    `FUN_8008eb40` al disparar;
  - la etapa de efectos (`FUN_80083cfc`) lo dibujaba en la entidad 0;
  - ahora ese estado es uno por jugador y un gancho a la entrada de la etapa
    de efectos hace para el 2 sus partes del jugador (`FUN_80081ad0`,
    `FUN_80083bac`, `FUN_80083aac` en la misión 0x11, y `FUN_8008eb78`);
  - la cabina del 1 queda limpia cuando dispara el 2, y su ráfaga sigue
    saliendo cuando dispara él.
- **Daño:** detrás del enemigo de la ranura 2, la metralleta del jugador 2
  le baja la vida de 140 a 25 (sin disparar, 140).
- **Derribo:** con 160 VBlank de fuego lo derriba (vida 0, banderas
  0x4082) y **el contador de derribos del juego sube**
  (`DAT_800BB87C+0x20`).
- **Misil:** de frente, le quita 70.
- **Daño recibido:** en las 41 misiones, el jugador 2 acaba algunas con
  daño parcial sin estrellarse (vida 97, 52, 40, 10). Los disparos enemigos
  le alcanzan.

### Puntería

`FUN_80044950` calcula, para el jugador, la distancia y el ángulo de cada
entidad en registros de 0x34 bytes (`DAT_800C57F0[k]`, contiguos) y unas
globales (`DAT_800C57C0`, 0x14 bytes).
- Ahora el jugador 2 tiene los suyos (se intercambian con su contexto) y los
  calcula antes de su paso.
- **Medido** (`AC3_COOP_PUNTERIA_LOG=2`): marca al enemigo 2 a distancia 150,
  con las banderas de fijado (0x010001FF). Cada entidad se actualiza cada 64
  pasos, por turnos.

**`SIN RESOLVER`: sus misiles no persiguen.** Desviado 6° o más, el misil
falla con y sin su puntería propia. Tampoco persigue forzando en su registro
la marca de «en pantalla» que pone el HUD (`FUN_8003adb4`, `+0x20 |=
0x60000`). Falta saber cómo elige blanco el disparo del jugador. Puede
depender de la cámara y del HUD del jugador, que el 2 tendrá en el paso 5.
**Resuelto en el paso 5:** esta prueba no tenía caso positivo (a 150 unidades
nadie puede fijar). El fijado lo pone la lógica del HUD; con ella, sus
misiles persiguen.

### Determinismo y estado

- Las copias propias del jugador 2 (bloques, entrada, fogonazo y puntería)
  viven en la RAM del juego, reservadas con su asignador (`0x801F3700..` en
  la misión 0). La huella de la RAM las incluye, y una instantánea también.
- **Medido:**
  - con disparos, choque y reaparición, dos ejecuciones dan la misma partida
    en los 4.000 VBlank, también con intermedias a 120;
  - un misil más del jugador 2 da otra.
- **Las 41 misiones** (`tools/coop.py`):
  - el jugador 1 queda idéntico al de sin cooperativo en todas;
  - 0 fallos;
  - las 5 de introducción larga se anotan aparte.

### Otros `SIN RESOLVER`

- **Las estelas de sus misiles nacen junto a la cámara del jugador 1.** El
  misil nace bien (en su avión, banderas de la entidad 1 en `+0x6C`).
  Revisarlo en el paso 5, con la vista de cada uno. **Resuelto en el paso
  6:** salen de «el avión del jugador», que el dibujo lee de la entidad 0
  por su dirección.
- **A quién persiguen los enemigos.**

## Paso 5: la vista de cada jugador (rama `etapa-9-paso-5`)

En el lockstep cada aparato simula la misma partida y dibuja a su jugador.
Hacen falta dos cosas:
- que lo que la vista decide y cambia la partida (blanco, fijado) exista
  para los dos jugadores en los dos aparatos;
- dibujar la imagen desde el jugador 2 sin tocar la partida.

### Hallazgo: el fijado lo decide el HUD, durante el dibujo

- **El paso de la cámara** (`FUN_80039f10`, al final del paso de vuelo)
  elige el blanco:
  - `DAT_800C568E`, con el cambio manual (`FUN_8003a658`) y el automático
    (`FUN_8003a6f8`, `FUN_8003aa28`: la puntuación de cada entidad según su
    distancia);
  - devuelve el fijado (`DAT_800C5692`), que se guarda en
    `DAT_800BBA78+0x18`.
- **La lógica del HUD**, dentro del dibujo (`FUN_80039f8c`):
  - `FUN_8003adb4` proyecta cada entidad con la matriz de la cámara (marca
    «en pantalla» en `+0x20`: 0x10000 al alcance, 0x40000 delante, 0x60000
    en el recuadro);
  - `FUN_8003e9a0` y `FUN_8003ed34` llevan el recuadro del fijado
    (`DAT_800BB890..97`: estado, entidad, tamaño, x, y) y, en el estado 2,
    ponen el fijado.
- **El misil del jugador** sale con `DAT_800BBA78+0x18` como blanco
  (`FUN_800672d4`, cuarto argumento: -1 = sin blanco, va recto).

Por eso los misiles del jugador 2 no perseguían: nadie hacía su paso de
cámara ni su HUD. **Además, la prueba del paso 4 no tenía caso positivo:**
- a 150 unidades **no se puede fijar**: la proyección desborda la división
  de la GTE (H/SZ, bandera 17 → bit 31) y el blanco nunca cuenta como
  delante, en la consola igual;
- y en esa fase de la misión 0 el blanco lo impone el guion (entidad 0x20,
  rama 0x800 de `FUN_8003a4cc`).

Ni el jugador 1 fijaba ahí. Con la misión 5 a 600 unidades sí (abajo).

### Cómo

`psx/recomp/ac3_coop.c`:

1. **Lo suyo de la vista**, en la RAM del juego y que se intercambia con su
   contexto (`p2_begin` / `p2_end`):
   - su cámara (0x200 bytes; antes se guardaba y se devolvía la del 1);
     empieza como copia de la del 1 con los punteros a la entidad 0
     cambiados a la 1;
   - su estado del blanco y del fijado: `DAT_800C5680` (0x20 bytes),
     `DAT_800BB890` (8), `DAT_800BB960` (4) y `DAT_800C57E0` (0x10);
   - sus registros de pantalla (punteros en `DAT_800C50C0`, 0xC bytes).
2. **Su vista en la simulación** (gancho a la entrada de `FUN_80039f10`),
   en los dos aparatos y como el jugador 2:
   - su paso de cámara, con sus argumentos;
   - el ajuste de la cámara (`FUN_8007a03c`) y sus matrices (`FUN_80033214`);
   - la lógica de su HUD (`FUN_8003adb4`, `FUN_8003e9a0`), sin la parte que
     dibuja ni el contador de mensajes.

   Lo que su HUD añade a la tabla de orden y a las primitivas se descarta.
   Las matrices del 1 y la GTE vuelven a estar como antes.
3. **La imagen desde su avión** (`ac3_coop_draw_p2`; en el PC,
   `AC3_COOP_VER=2`):
   - al empezar el dibujo del paso (entrada de `FUN_8007a03c` desde
     `FUN_80033910`, justo tras la lógica: S1) se guardan la RAM, el
     scratchpad y la GTE;
   - en DrawOTag, se guarda el final del paso y se vuelve a S1;
   - se quita de la cadena maestra la tabla de la misión;
   - se vuelve a llamar a `FUN_80033910` como el jugador 2 con una lógica
     vacía (`FUN_8003a2f8`, que repone las bandas de cine de
     `g_render+0x28C`). Así se dibujan el mundo, los objetos y el HUD desde
     él;
   - se dibuja la lista y la RAM vuelve al final del paso.

   **Desde S1 y no desde el final:** el HUD tiene sublistas fijas
   (`0x8013B204`, doble búfer) que `FUN_80033910` no limpia. Dibujadas dos
   veces, la cadena se cerraba en un ciclo (900 ms por imagen). Desde S1:
   4.600 paquetes, unos 7 ms en total por imagen en el PC.

### Resultados

**Misiles guiados** (`tools/coop_vista.py misil`: misión 5, a 600 unidades
detrás del enemigo 2, misil en el VBlank 3220):

| | girado 0° | 4° | 6° |
|---|---|---|---|
| jugador 1 (control) | blanco 2, 140 → -20 | blanco 2, 140 → -20 | blanco 2, 140 → -20 |
| jugador 2 con su vista | blanco 2, 140 → 30 | blanco 2, 140 → 30 | blanco 2, 140 → 120 |
| jugador 2 sin ella (`AC3_COOP_VISTA=0`) | sin blanco, 140 → 30 | sin blanco, 140 → 120 | sin blanco, 140 → 120 |

(Vida del enemigo 2 en el VBlank 3600; 140 → 120 es un misil que pasa cerca
sin acertar. Desde el paso 6, con la IA que también persigue al 2, el
control sin su vista a 0° da 140 → 120: el enemigo 2 maniobra contra él.)

- El jugador 2 ya fija (recuadro en estado 2, `+0x18` = 2) y su misil sale
  con blanco y gira hacia el enemigo (`AC3_COOP_DBGMISIL`): girado 4° acierta
  y sin su vista no.
- A 6° su misil gira por el mismo camino que el del 1, unas 25 unidades al
  lado (sale del otro ala), y no acierta. `SIN RESOLVER` si es solo eso.

**La imagen desde el jugador 2** (capturas de la misión 0):
- su altitud, su velocidad, su radar y su HUD;
- al estrellarse, su cámara sigue sus restos con «BODY DAMAGE» en rojo
  mientras el 1 sigue volando.
- **Corregido en el paso 6:** en vuelo normal su cámara seguía al avión del
  1 (el puntero de su objetivo apunta dentro de la entidad, no a su inicio),
  y en tercera persona se dibujaba el avión del 1. El HUD sí era el suyo, y
  por eso no se vio en estas capturas.

**Lockstep:**
- con la SPU, la huella de la RAM de los 4.001 VBlank es la misma dibujando
  desde el jugador 1 o desde el 2 (`tools/coop_vista.py huella`);
- en las 41 misiones (`tools/coop.py … --vista2`): 18.033 imágenes desde el
  2, el jugador 1 igual que sin cooperativo, 0 fallos.

**Autoprueba** (`AC3_COOP_AUTOPRUEBA`: el mismo redibujo como el jugador 1):
- igual al original píxel a píxel salvo las 16 primeras imágenes de cada
  vuelo;
- es la transición de ruido con la que aparece la imagen, que se dibuja
  fuera de la lista de la misión y en la vista del 2 no sale.

### Opciones nuevas del binario del PC

- `AC3_COOP_VER=2`: la imagen desde el jugador 2.
- `AC3_COOP_VISTA=0`: sin su vista en la simulación.
- `AC3_COOP_AUTOPRUEBA=1` (y `_VOLCAR=v`): el redibujo como el 1, comparado.
- `AC3_COOP_TRAS1=k`, `AC3_COOP_GIRO1`: el jugador 1 detrás de la entidad k;
  `AC3_COOP_DIST=u`: a u unidades (150 si no).
- Medición:
  - `AC3_COOP_BLANCO=n`: blanco y fijado de cada jugador;
  - `AC3_COOP_MISIL=1`: blanco de cada misil lanzado;
  - `AC3_COOP_DBGMISIL=1`: misiles en vuelo;
  - `AC3_COOP_DBGHUD=k`: proyección del HUD del 1 sobre la entidad k;
  - `AC3_COOP_SONDA_VISTA=v`: qué escribe su vista;
  - `AC3_COOP_TIEMPOS=1`: tiempos y paquetes del redibujo.

### Lo que falta

- **En el aparato:**
  - el redibujo está en el binario del PC; en el APK va en su DrawOTag;
  - con las intermedias de 120 Hz, el redibujo de la etapa 8 guarda su S0
    en el dibujo del 1 y habría que darle el del 2;
  - en el aparato hay que medir el coste: 2 copias de 2 MB y un segundo
    dibujo de la misión.
- **Sonido:** los avisos del fijado del 2 suenan en los dos aparatos (son
  de la partida: la SPU cuenta para el determinismo).
- **`SIN RESOLVER`:**
  - las estelas de sus misiles: en las dos vistas se ve una que nace junto
    al radar, y no se ha podido decidir de dónde nace (resuelto en el paso
    6);
  - a quién persiguen los enemigos;
  - la transición de ruido en la vista del 2.

**Reproducir:**
- `python3 tools/coop_vista.py "<US>" <dir>`
- `python3 tools/coop.py "<US>" <dir> 0-40 6 --vista2`

## Paso 6: el cooperativo en el APK (rama `etapa-9-paso-6`)

### Cómo

- **La capa:**
  - `DrawOTag` tiene un desvío (`psx_draw_redirect`): recibe la lista y lo
    que hay que hacer con ella (con intermedias, sin ellas). El cooperativo
    reconstruye la lista del 2 y se la pasa;
  - con `ac3_rr_mute`, el redibujo de las intermedias (etapa 8) no anota el
    dibujo original del paso, que no se va a ver, y sí el del 2. Sus
    intermedias salen desde él.
- **El APK de desarrollo** (`depurar.txt`, se leen al arrancar):
  - `coop=1`: el segundo avión y el mando del puerto 2;
  - `vista=2`: la imagen desde el jugador 2;
  - `guion2=1`: su mando lo pone el guion de `tools/coop_vista.py`;
  - `mando2=1`: su mando es un segundo mando físico (el primero que se usa
    es el del 1);
  - `jugador=2`: desde que el 2 vuela, los controles del aparato son suyos y
    el 1 queda con el mando suelto (para probarlo con un solo aparato).
- Los mensajes del cooperativo van al registro (`logcat`, `AC3Game`). Cada
  300 imágenes, lo que cuesta el redibujo.

### Arreglos de la vista (lo vio el dueño jugando)

«Al alabear, en primera persona no se aprecia el giro; en tercera persona sí,
y aparece el avión transparente.» Reproducido en el PC con un control: el
jugador 1 y el 2 con las mismas entradas deben verse igual.

1. **La cámara del 2 seguía al avión del 1.**
   - Sus ángulos los pone el ajuste de la cámara (`FUN_8007a03c`) a partir
     de su objetivo, un puntero en `+0x34` a la posición de la entidad
     (`e+8`), no a su inicio.
   - Al crear su cámara solo se cambiaban los punteros iguales a la
     entidad 0. Ahora cambia cualquiera que apunte dentro de ella.
   - Medido (`AC3_COOP_DBGCAM`): su avión alabeado 4096 y su cámara a 0,
     como la del 1.
2. **«El avión del jugador» se dibuja desde una dirección fija.**
   - El dibujo estático lo dibuja, y saca el origen de las estelas, de la
     entidad 0 por su dirección (`g_render+0x3BC`), no por la tabla.
   - En su vista en la simulación y en el redibujo, el contenido de las dos
     entidades se intercambia (cada una conserva su ranura). La tabla
     vuelve a su orden y los punteros de la cámara se cambian
     (`ent_content_swap`).
   - Sin esto, en tercera persona se veía el avión del 1, nivelado.
   - **Las estelas de sus misiles ya nacen en su avión** (era `SIN RESOLVER`
     desde el paso 4).
3. **Sus copias del control** (`0x800C6508`, `FUN_80070d34`) pasan de
   protegidas a suyas, como su estado del blanco.

Comprobado: con las mismas entradas, sus imágenes en cabina y en tercera
persona coinciden con las del 1 (horizonte, alabeo, altitud). La lectura del
redibujo de los bloques del 1 por su dirección (`AC3_COOP_LECTURAS`) da
cero.

### Sonido y radar (segunda prueba del dueño)

«Los misiles salen del avión, pero al impactar no suena nada; el jugador 1
no aparece en el minimapa ni en el mapa grande, y al revés.»

- **Sonido:**
  - los sonidos con posición se calculan respecto a la cámara del jugador
    1 (`FUN_8002e488(salida, oyente, fuente)`, con el oyente en la cámara
    + 4);
  - los sueltos (`FUN_8002f16c` / `f20c`: explosiones, impactos) siempre
    suenan, pero la distancia les fija volumen y panorama: el impacto del 2
    lejos del 1 sonaba casi a cero;
  - el gestor de emisores (`FUN_8002f758`) además da voces según la
    posición respecto al oyente, y el driver (`FUN_8002df8c`) lee el estado
    de las voces (`SpuGetAllKeysStatus`): **un oyente distinto en cada
    aparato separaría las partidas**;
  - **arreglo, igual en los dos aparatos:** cada sonido se oye desde la
    cámara del jugador más cercano a él (gancho a la entrada de
    `FUN_8002e488`; `AC3_COOP_OYENTE=0` lo quita);
  - medido (`AC3_COOP_DBGOYENTE`): el impacto del misil del 2 (sonido 0x1B,
    VBlank 3276) se calcula desde su cámara, a 496 unidades (de la del 1,
    a 27.800); y los motores de los enemigos que tiene cerca;
  - consecuencia: el 1 también oye lo que pasa junto al 2, como si
    estuviera allí. Un oyente por aparato pediría separar el sonido de la
    partida.
- **Radar:**
  - su lógica (`FUN_8003d46c`) y su dibujo (`FUN_8003d8c0`) van de la
    ranura 2 a la 63: ninguno de los dos jugadores sale;
  - el color de cada punto lo da su registro (`[2]`: 0 gris, 1 rojo, 2
    azul, 3 amarillo); con el alcance del mapa grande (`DAT_800C569C` >
    0x7FFF), el punto pequeño;
  - **arreglo:** al entrar en su dibujo, el otro jugador (la ranura 1 de la
    tabla: el 2 en la imagen del 1; el 1 en el redibujo del 2) como un punto
    azul más. Su registro se calcula como el juego (centro y giro del radar
    en `*DAT_800BB978`, rumbo, por encima o por debajo) y lo dibuja
    `FUN_8003da58` (`AC3_COOP_RADAR=0` lo quita);
  - comprobado separándolos 8.000 unidades: cada uno ve al otro en el lado
    opuesto, a la escala del juego (el radar abarca unas 60.000 unidades).
- **Mapa grande** (tercera prueba: se abre manteniendo cuadrado, bit 0x8000
  de `g_state+0x128`, la X de la Retroid):
  - lo hacen `FUN_8003e0fc` y `FUN_8003c5f4`, desde `FUN_8003c014`;
  - `FUN_8003e0fc` pone una baldosa de 2×2 por entidad, otra vez desde la
    ranura 2: una sombra y luego el color, azul para el bando del jugador
    (0x40). Van en `*DAT_800BB974 + 0x518`, con su número en
    `*DAT_800BB970 + 0xC`, y `FUN_8003c5f4` las enlaza;
  - **arreglo:** al entrar en `FUN_8003c5f4`, una baldosa más para el otro
    jugador con la misma cuenta (`FUN_8003e66c`: casillas de x >> 23 y -z >>
    23, ventana de 128 alrededor del jugador);
  - primero copiaba el estilo del juego: la sombra roja de su primera
    pasada (0xFF, 0x40, 0x40) y el azul semitransparente encima. En la
    Retroid se veía rojo, como un enemigo (lo vio el dueño). Ahora es un
    cuadrado azul opaco de 3×3;
  - medido (`AC3_COOP_DBGMAPA`, separados 30.000 y 10.000 unidades): el 1 ve
    al 2 a (+15, −5) casillas y el 2 al 1 a (−15, +5).

### Temblor, IA y recuadro FRND (cuarta y quinta prueba)

- **El avión del 1 temblaba al volar a su lado.** Las intermedias (etapa 8)
  interpolaban la entidad 0 (`g_render+0x3C4`) y el arreglo de aviones de
  la ranura 2 en adelante (`g_render+0x474 + k*0x5C`), pero no la ranura 1
  (`g_render+0x41C`). La encuesta del dibujo estático (`AC3_RR_SURVEY`)
  confirma que la lee y que cambia en 228 de 229 pasos. Ahora es un objeto
  más (`AC3_RR_NFIELDS`, 34 objetos). Autoprueba t = 1: 0 de 80 distintas,
  con y sin cooperativo. El dueño lo confirmó en la Retroid.
- **Los enemigos no atacaban al 2** (`SIN RESOLVER` desde el paso 2):
  - cada enemigo elige objetivo en `FUN_8004570c`: al jugador (ranura 0) si
    le queda un perseguidor libre (`DAT_800C56A2/A3`) y, si no, el más
    cercano de las ranuras 2..63. La 1, nunca;
  - y cada paso suelta un objetivo distinto de 0 sin el bit 1 de sus
    banderas (`DAT_800C58F0[k]`), que en la 1 está a 0 (`FUN_80045fc8`);
  - además el jugador anota a quién persigue con el 0 fijo
    (`FUN_8004599c(0, k)`, `FUN_80045a94(0)`): como el 2, en la fila del 1
    de la tabla de persecuciones (`DAT_800C56A0`, 4 bytes por entidad);
  - **arreglo, igual en los dos aparatos:** la fila de la 1 empieza libre y
    su bit 1 va con su vida; lo que el 2 anota va a su fila; y cuando la
    elección da el jugador (o nadie), es el 2 si está más cerca de ese
    enemigo (o si es el único con perseguidor libre). `AC3_COOP_IA=0` lo
    quita;
  - medido (`AC3_COOP_DBGIA`, la tabla del juego): en las misiones 5, 9 y
    13 los enemigos persiguen al 2 (sin el arreglo, nunca). Que además le
    disparen está sin medir: la cuenta de misiles enemigos en vuelo da 0
    también contra el 1 en el juego sin tocar (sin caso positivo).
- **El recuadro azul FRND sobre el otro jugador** (idea del dueño, como el
  aliado de la misión 3):
  - el dibujo de los recuadros (`FUN_8003ef84`) recorre las 64 ranuras,
    pero la 1 no tiene registros de pantalla ni de puntería (punteros nulos
    en `DAT_800C50C0` / `DAT_800C57F0`) ni la bandera 0x1000, y la
    proyección (`FUN_8003adb4`) va de la 2 a la 63;
  - el aliado de la misión 3 (ranura 2): registro `00 00 01 19 ff 03 18 00
    9c 00 78 00` (forma 1, etiqueta 0x19, **tipo 3 = FRND**, tamaño, x, y);
    `FUN_80040154(3)` pinta la etiqueta (sprite de 18×6 en 136, 114);
  - **arreglo:** al entrar en `FUN_8003ea48`, el otro jugador se proyecta
    como lo hace el juego (matriz de la cámara, tamaño de `FUN_8003b1bc`) y,
    si se ve, la ranura 1 recibe registros propios con el tipo FRND hasta lo
    siguiente del HUD (`FUN_80062924`), cuando todo vuelve a como estaba.
    `AC3_COOP_FRND=0` lo quita;
  - comprobado en el PC: corchetes azules con «FRND» sobre el 2 en la
    imagen del 1, y sobre el 1 en la del 2.
- `NHOOK` pasa de 24 a 32: con la vista del 2, el APK usa 25 ganchos. Si se
  llenara, ahora lo dice.

### Resultados

- **Lockstep entre aparatos** (`tools/movil_coop.sh`, mismos mandos por
  guion para los dos jugadores):
  - Retroid dibujando desde el 1 y S24 desde el 2: la misma huella de la
    RAM en los 4.081 VBlank (misión 0);
  - al revés también;
  - tras los arreglos de la vista, en la misión 5: la misma en los 4.004;
  - con el oyente y el radar nuevos, también;
  - con el mapa, la Retroid dibujando desde el 1 y desde el 2 y el S24 de la
    ronda anterior: la misma. En el PC, la misma huella con los dos
    jugadores abriendo el mapa, y las 41 misiones bien.
- **Coste del redibujo del 2**, de media por imagen y como máximo:

  | Aparato | Media | Máximo | CPU |
  |---|---|---|---|
  | S24 | 8,5 ms (5,2 redibujo + 2,6 dibujo + 0,4 restaurar) | 22 ms | — |
  | Retroid | 7 ms | 15 ms | 24-42 % |

  El paso dura 33 ms. A 120 Hz (S24) y a 60 (Retroid) sin pasos saltados a
  la vista (contador de imágenes 120 / 60).
- **En el PC, de nuevo:**
  - misiles, huella con SPU y autoprueba iguales que en el paso 5;
  - con las intermedias de 120 Hz, la misma huella dibujando desde el 2;
  - las 41 misiones con `--vista2`: el 1 igual, 0 fallos.
- **«MISSION FAILED» en las pruebas con guion:** lo provoca el jugador 1
  (el guion del PC acaba en un picado y cae hacia el VBlank 3850). La caída
  del 2 no termina la misión: reaparece. Con el mando suelto, el 1 aguanta
  al menos 9.000 VBlank en las misiones 0 y 5.

### Lo que falta

- La red (etapa 10). Hasta entonces, el mando del 2 en un aparato es un
  guion, un segundo mando o los controles del propio aparato.
- La caída del 1 termina la misión, como en el juego. En cooperativo
  habría que decidir si reaparece.
- Los avisos de fijado del 2 suenan en los dos aparatos, y cada uno oye lo
  que pasa junto al otro (el oyente es el más cercano).
- `SIN RESOLVER`: si los enemigos le disparan (le persiguen, medido); la
  transición de ruido en la vista del 2.

**Reproducir:**
- `tools/movil_coop.sh <serie_A> <serie_B> <dir> [mision]`
- Radar: `AC3_COOP_POS=8000,0` con `AC3_COOP_VER=1` y `2`; sonido:
  `AC3_COOP_DBGOYENTE=1` en la prueba del misil de `tools/coop_vista.py`
- `python3 tools/coop_vista.py "<US>" <dir>`
- En el PC, la prueba del alabeo: `AC3_GUION2='2700:lder:80,2850:select:6,2900:lder:80'`
  con `AC3_COOP_VER=2`, contra el mismo guion en el jugador 1.
