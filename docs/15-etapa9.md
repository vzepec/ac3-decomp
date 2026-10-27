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
| 9 | 1 | **Determinismo:** mismos mandos → misma RAM, con opciones distintas, en el PC y en el aparato | ✅ |
| 9 | 2 | El avión del jugador: estructura, función de vuelo y todo lo que lo usa (cámara, HUD, radar, daño, fin de misión) | |
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
- **Pendiente:** dos aparatos distintos (S24 contra Retroid: otro procesador,
  mismo APK) con `tools/movil_determinismo.sh`; y la versión europea en el
  aparato.

**Reproducir:**
`make determinismo MISION=37` · `python3 tools/determinismo.py "<US>" <dir> --mision 19 --apk --detalle` ·
`tools/movil_determinismo.sh <dir> 0` (APK de desarrollo instalado).
