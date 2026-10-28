# Etapa 10 — El cooperativo en red local

Abierta el 2026-10-28. Sigue a la etapa 9 (`docs/15-etapa9.md`), donde el
cooperativo ya funciona en un aparato y la partida es la misma en dos. Aquí
los dos aparatos se conectan y juegan juntos por **lockstep**: cada uno
simula la partida entera, y lo único que viaja es el mando de cada jugador.

| Paso | Qué | Estado |
|---|---|---|
| 7 | Lockstep por TCP (`psx/recomp/ac3_net.c`): PC↔PC y PC↔Retroid por el cable; saltar la introducción, pausa compartida | ✅ |
| 8 | Menú Crear / Unirse en el APK, búsqueda en la red local, Wi-Fi entre dos aparatos | |

Solo código nuestro: sockets POSIX, iguales en el PC y en Android. No hay
servidor: uno escucha (el anfitrión) y el otro conecta (el invitado).

## Paso 7: el lockstep (rama `etapa-10-paso-7`)

### Cómo funciona

- **Papeles:** el anfitrión es el jugador 1 (puerto 1 del mando) y escucha;
  el invitado es el 2 y conecta.
- **Saludo:** al conectar, cada uno envía 20 bytes:
  - `AC3N`, la versión del protocolo (1) y el papel;
  - el retraso (manda el del anfitrión);
  - las opciones que cambian la partida: hoy, el bit de 16:9.

  Si las opciones difieren, avisa de que las partidas se separarán.
- **Mensajes de 16 bytes:**
  - tipo 1: el mando de un VBlank (botones y las cuatro palancas);
  - tipo 2: la huella de la RAM de un VBlank.
- **En cada VBlank v**, en la espera del VBlank (antes de que el juego lea
  el mando):
  - se envía el mando local para v + retraso;
  - se aplican los de los dos para v, esperando el del otro si no ha
    llegado;
  - los primeros `retraso` VBlank van sin mando.
- **Retraso:** 2 VBlank (33 ms); `retraso` en `depurar.txt` o
  `AC3_RED_RETRASO`.
- **Comprobación:** cada 60 VBlank se envía y se compara la huella de la
  RAM (los 32 bloques, el scratchpad y la semilla de `rand`). Si difiere, se
  dice en el registro.
- **Red perdida:** si el otro calla 20 s, la red se da por perdida y su
  mando queda suelto.
- **El mismo ritmo de imagen:** el PC y el APK deben avanzar el VBlank en el
  mismo punto. `AC3_COMO_APK` pone en el PC la huella, la SPU y el mando en
  la espera del VBlank, como el APK; el modo red lo implica.
- **Con adb reverse** (`tcp:7070`), el `127.0.0.1` del aparato llega al PC
  por el cable.
  - El cable acepta la conexión aunque el anfitrión aún no escuche, y la
    cierra sin saludo.
  - Por eso el invitado reintenta hasta recibir un saludo. Solo un saludo
    que llega y no cuadra es un fallo.

### Los menús y la pausa son de los dos

El juego solo lee el puerto 1 fuera del avión. `ac3_coop_p1_extra`
decide qué botones del jugador 2 también cuentan como del 1:

- **fuera del vuelo** (menús, introducción, cinemáticas, informe, pausa):
  todos;
- **en vuelo:** solo START, así que la pausa es de los dos.

Se aplica a los mandos ya acordados por la red, así que es igual en los dos
aparatos. «En vuelo» es que el paso del jugador 1 corrió en los últimos 4
VBlank (el vuelo da un paso cada 2).

**Medido en la misión 1:**
- con el START del 2, el vuelo empieza en el VBlank ~2700; sin él, en el
  6817 (la introducción dura más de un minuto);
- si el 2 pausa y el 1 reanuda, hay 150 pasos de vuelo menos (los 300
  VBlank de pausa), y la partida es la misma en los dos.

### Lo que el dueño encontró jugando, y su arreglo

**1. Las partidas se separaban con SELECT.**
- **Qué pasaba:** a los 9 s de aparecer el 2 (VBlank 7380), con el mando
  real. Con los guiones de prueba nunca pasó.
- **Reproducido en el PC:** el guion del 2 pulsa todos los botones, y se
  compara dibujando desde el 1 y desde el 2. Se separa en el VBlank 7111,
  tras SELECT (vista exterior), y solo en la semilla de `rand`.
- **Causa:** el dibujo de la vista exterior llama a `rand`, y su semilla
  está en nuestra libc, fuera de la RAM. El redibujo del 2 restaura la RAM,
  pero no la semilla.
- **Arreglo:** el redibujo vuelve a la semilla de S1 al empezar y a la del
  final al acabar, como ya hacían las intermedias (`g_s0_seed`).
- **Comprobado:** 9.201 VBlank iguales con todos los botones.

**2. Los menús, sin sonido y acelerados.** El APK de desarrollo va hacia la
misión elegida acelerado y sin sonido. En red, el ritmo lo marca el
anfitrión: ahora a velocidad normal y con sonido. Además el invitado, que
llegaba antes, esperaba al PC en casi cada VBlank; ahora ninguno espera.

**3. Retraso de los botones.** Bajado de 4 a 2 VBlank (de 67 a 33 ms), sin
esperas en el cable.

**4. Saltar la introducción.** Ver «Los menús y la pausa son de los dos».

**5. La pausa en el aparato del 2 mostraba el avión del 1.**
- Sin paso de vuelo no había redibujo.
- **Cómo se detecta la pausa ahora,** al empezar el dibujo
  (`FUN_8007a03c`):
  - `g_state+0` bit `0x20000000`: la pausa. Con él, `FUN_80033e20` va al
    menú de pausa;
  - o `g_render+0` bit `0x40`: menú sobre la misión. Lo ponen
    `FUN_800345f0` y `FUN_80034c38` tras el dibujo, y lo quita
    `FUN_8007a03c`. Medido: `0x02` en vuelo, `0x42` en pausa.
  - Con solo el segundo, la primera imagen de la pausa salía como el 1 y
    parpadeaba (lo vio el dueño).
- **El menú:**
  - El juego lo dibuja después de `FUN_80033910`: la pausa con
    `FUN_80034808`, el otro menú con `FUN_80034d78`. Los dos reciben
    `(*(g_state+0xC), puntero de primitivas)` y devuelven el nuevo puntero.
  - El redibujo anota cuál dibujó el juego en ese paso y lo repite encima,
    con el puntero de primitivas del redibujo y el estado del 1.
  - Errores corregidos por el camino:
    - con el puntero del original, el menú pisaba las primitivas del mundo;
    - con el estado del 2, el cursor no se movía.
- **Comprobado:**
  - el menú aparece y desaparece en las mismas imágenes que en el
    original;
  - ninguna imagen de la pausa sale desde el 1;
  - la misma partida desde las dos vistas.

### Resultados

| Prueba | Resultado |
|---|---|
| PC↔PC (`tools/red.py`, `red`): START del 2, SELECT, pausa del 2 que quita el 1 | huellas de cada VBlank iguales; 74 comparadas, 0 distintas; vuelo desde el 2711; 150 pasos menos |
| Control: el invitado a 4:3, el anfitrión a 16:9 | aviso de opciones; 29 de 74 distintas |
| Vistas (sin red): SELECT del 2, pausa y menú del 1, desde el 1 y desde el 2 | la misma partida en los 4.001 VBlank |
| PC↔Retroid por el cable (`tools/movil_red.sh`), 4:3 y 16:9 | 0 distintas (4.101 VBlank, 68 huellas) |
| El dueño jugando en la Retroid contra el PC, misión 1 | 0 distintas en 150-269 huellas por partida, 0 esperas, 60 fps |
| Vistas de la etapa 9 (`tools/coop_vista.py huella autoprueba`) | igual que en el paso 6 |

### Lo que falta

- **El paso 8:**
  - menú Crear / Unirse en el APK (hoy, `depurar.txt`: `red=1` / `red=2`,
    `red_ip1..4`, `red_puerto`, `retraso`);
  - búsqueda del anfitrión en la red local;
  - Wi-Fi entre dos aparatos (Retroid y S24).
- **El PC:** no tiene ventana ni mando. Como anfitrión vuela recto con su
  guion; para jugar persona contra persona hacen falta dos aparatos, o SDL2
  en el PC.
- **Volver a sincronizar:** si las partidas se separan, hoy solo se dice.
  Habría que enviar la RAM del anfitrión.
- **Pendientes de la etapa 9:** que el 1 reaparezca en cooperativo; los
  avisos de fijado del 2 se oyen en los dos aparatos.
- **`SIN RESOLVER`:** si los enemigos disparan al 2.

**Reproducir:**
- `python3 tools/red.py "<US>" <dir>` (red, control, vistas);
- `VB=4500 tools/movil_red.sh <serie> <dir> 1` (PC↔aparato por el cable;
  `VB=0` y `GUION=0` para jugar).
