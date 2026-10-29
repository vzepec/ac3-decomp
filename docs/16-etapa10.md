# Etapa 10 — El cooperativo en red local

Abierta el 2026-10-28. Sigue a la etapa 9 (`docs/15-etapa9.md`), donde el
cooperativo ya funciona en un aparato y la partida es la misma en dos. Aquí
los dos aparatos se conectan y juegan juntos por **lockstep**: cada uno
simula la partida entera, y lo único que viaja es el mando de cada jugador.

| Paso | Qué | Estado |
|---|---|---|
| 7 | Lockstep por TCP (`psx/recomp/ac3_net.c`): PC↔PC y PC↔Retroid por el cable; saltar la introducción, pausa compartida | ✅ |
| 8 | Menú Crear / Unirse en el APK, búsqueda en la red local, Wi-Fi entre dos aparatos | ✅ menú, anuncio, saludo y ping; PC↔Retroid y Retroid↔S24 por Wi-Fi con la misma partida, jugado por el dueño |

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

## Paso 8: el menú Cooperativo y el Wi-Fi (rama `etapa-10-paso-8`)

### El menú

En el lanzador, **Cooperativo (experimental)**. De momento solo sale en el
APK de desarrollo (`.dev`), porque el cooperativo vive allí. Tiene dos
opciones:

- **Crear partida:**
  - se elige la misión en la lista;
  - un diálogo muestra «Esperando a otro jugador», con la IP del aparato
    y la misión, y un botón Cancelar.
- **Unirse:**
  - la lista de partidas de la red, que se llena con los anuncios
    («nombre · misión (IP)»);
  - al final de la lista, «Escribir la IP…», por si la red no deja pasar
    los anuncios.
  - Quien se une no elige misión: le llega en el saludo.

**Los menús sin guion** (desde la prueba del dueño): tras conectar, los
jugadores recorren los menús del juego (título, partida nueva, dificultad,
menú de misión, informe, avión, armas). Los botones del 2 cuentan fuera del
vuelo.

- **La misión elegida, en la partida nueva:**
  - `FUN_800d2418` (overlay 1) guarda la dificultad (`g_state+0x398`) y pone
    la misión 1 en `+0x390`, con el `sh` de `0x800D2448`;
  - se vigila esa escritura (`mipsi_watch_write`, con un aviso nuevo,
    `mipsi_watch_write_fn`), y en la siguiente espera del VBlank, la misma
    en los dos aparatos, se pone la elegida;
  - así el menú de misión, el informe y el avión ya son los suyos: con la
    5, el menú muestra «SCRAMBLE» y el propio juego arranca la 5;
  - un gancho de entrada no servía: a `FUN_800d2418` se llega por una tabla
    de saltos, no con `jal`;
  - la escritura de la demo del título (VBlank 1319, desde `800cb8f4`) no
    cuenta.
- **La tarjeta de memoria de quien crea, en los dos.** Cada aparato tiene la
  suya, y el juego la lee al arrancar: con tarjeta en un lado y sin ella en
  el otro, las partidas se separaron en el VBlank 120.
  - Al conectar, quien crea envía sus 128 KB (`ac3_net_send_blob`) y quien
    se une los usa en memoria (`psx_card_attach_image`), sin tocar su
    archivo.
  - Así se puede **cargar una partida guardada**: la de quien crea. Para
    cargar la del otro aparato, crea él.
  - Lo que se guarde va a la tarjeta de quien crea; en el otro se queda en
    la memoria.
  - La misión elegida ya no se fuerza al arrancar, para que una partida
    cargada siga la suya: solo se pone en la partida nueva.
  - (Primero se probó sin tarjeta en los dos; cargar quedaba fuera.)
- Es la opción `AC3_NET_OPT_MENUS` del saludo. En el PC, `AC3_RED_MENUS=1`.

Al conectar sale un aviso con el nombre del otro. Si el otro se va a mitad
de partida, otro aviso: «sigues solo». Si no se conecta, se explica el
motivo (otro disco, otra versión, sin conexión) y se vuelve al menú.
`depurar.txt` (`red=`) sigue mandando si está.

### El protocolo, versión 2

- **Saludo:** lleva un `NetInfo`:
  - las opciones: 16:9 y, desde el paso 8, el cooperativo;
  - la huella de ACE.BIN (FNV-1a): con otro disco, se rechaza;
  - la misión de quien crea (-1: los menús del juego);
  - el nombre del aparato (`Build.MODEL`; en el PC, `AC3_RED_NOMBRE`);
  - el texto de la misión.
- **Lo que adopta quien se une:** la misión y el modo de imagen de quien
  crea (16:9 → modo 0; 4:3 → 2, o 3 si ya estaba en el original). En el
  PC, si no tiene `AC3_MISSION`, también la misión.
- **Anuncio:** mientras espera, quien crea envía cada segundo un datagrama
  UDP (`AC3L`, la versión, el puerto y el `NetInfo`) al puerto 7070:
  - a la difusión de cada red con difusión, y a 255.255.255.255;
  - en el PC llega una copia por red: con Docker, cuatro.
  - Quien busca escucha con el cerrojo de multidifusión de Android, sin el
    cual algunos Wi-Fi tiran la difusión. Permisos normales, que no se le
    preguntan al usuario: `ACCESS_WIFI_STATE` y
    `CHANGE_WIFI_MULTICAST_STATE`.
- **Ping:**
  - cada 30 VBlank, un mensaje de tipo 3 con la hora; el otro lo devuelve
    como tipo 4;
  - se muestra la media de los 8 últimos: en el contador («PING n MS»),
    en el registro cada 30 s y en el resumen del PC;
  - incluye hasta un VBlank del otro (0-17 ms), que lo devuelve cuando
    lee la red.

### Lo que se encontró

- **La primera vez desde el menú, las partidas se separaron en el VBlank
  60:**
  - `dbg_read` ponía el cooperativo del menú después de copiar `co`/`vi`;
    el invitado jugaba sin el segundo avión;
  - arreglado, y desde entonces el cooperativo va en las opciones del
    saludo: si un lado lo tiene y el otro no, se avisa.

- **El autopiloto del 1 salía también en la pantalla del 2** (lo vio el
  dueño jugando Retroid↔S24; el del 2 salía bien, solo en la suya):
  - el aviso del HUD lo elige `FUN_80041c88`, desde el HUD de cada imagen
    (`FUN_80040c44`), con el estado y la entrada del jugador. El 9 es el
    autopiloto; hay otros, como fuera de zona o la altura;
  - lo guarda en un bloque global que dura entre imágenes:
    `DAT_800C5AA0..AF` (aviso y tiempos) y `DAT_800BB988` (su cuenta);
    medido con `AC3_WATCHWRITE`;
  - el redibujo del 2 partía del bloque que dejó el dibujo del 1 y heredaba
    su aviso; el del 2 se perdía con el redibujo.
  - **Arreglo:** el 2 tiene su copia del bloque, que se pone al empezar su
    redibujo y se guarda al acabar (vacía al empezar la misión).
  - `AC3_COOP_AVISO=0` lo quita, y es el control positivo.

### Resultados

| Prueba | Resultado |
|---|---|
| `tools/red.py menus`: el anfitrión elige la 5, los dos con `AC3_RED_MENUS` | partida nueva en el VBlank 704 con la 5 en los dos; el juego arranca la 5; huellas iguales |
| Retroid crea Scramble desde el menú, el PC se une (`AC3_RED_MENUS`), menús recorridos a mano | el menú de misión muestra «SCRAMBLE»; partida nueva en el VBlank 1131 en los dos; el juego arranca la 5 en el 4218; 0 distintas |
| El dueño, Retroid↔S24 con los menús a mano (Scramble) | «funciona perfecto»: partida nueva en el VBlank 1052 y la misión 5 en el 2161 en los dos; 60 huellas, 0 distintas; ping 13-17 ms. La Retroid esperó al S24 en casi cada imagen (13 ms de media): la fase del lockstep, sin efecto en la partida |
| `tools/red.py tarjeta`: se guarda una partida (menú de misión > SAVE) y el anfitrión la carga en red; el invitado tiene una tarjeta vacía | 49 huellas, 0 distintas, y la tarjeta del invitado intacta; control (`AC3_RED_TARJETA=0`, el invitado con la suya): 48 de 49 distintas |
| `tools/red.py aviso`: autopiloto del 1 y del 2, desde las dos vistas | la barra «AUTOPILOT» (911 px) solo en la vista de quien lo activa, la misma partida; control (`AC3_COOP_AVISO=0`): sale en las dos |
| El dueño jugando Retroid↔S24 por Wi-Fi desde el menú, misión 1, 11 minutos | 690 huellas, 0 distintas; Retroid 60 fps, S24 120; ping 17-21 ms |
| `tools/red.py` (red con la misión del anfitrión, control, vistas, anuncio, disco) | todas bien; el invitado adopta la misión; con la europea, «el otro tiene otro disco» |
| PC crea, Retroid se une por Wi-Fi desde el menú (`MENU=1 tools/movil_red.sh`) | ve «PC · 01 (192.168.1.34)»; 66-74 huellas, 0 distintas; ping 14-25 ms |
| Retroid crea (misión 1 de la lista), PC se une por Wi-Fi | el PC oye su anuncio («Retroid Pocket Flip2», «01 Transport»), adopta la misión; 74 huellas, 0 distintas |
| Retroid crea (misión 1), S24 se une por Wi-Fi desde el menú, sin el PC | el S24 ve «Retroid Pocket Flip2 · 01 Transport (192.168.1.20)»; 89-90 huellas, 0 distintas; ping 17-19 ms; las dos pantallas en el mismo punto (S24 a 120 Hz con 3 intermedias, Retroid a 60) |

**Reproducir:**
- `python3 tools/red.py "<US>" <dir> [--eu "<EU>"]` (red, control, vistas,
  anuncio, disco);
- `MENU=1 VB=4000 tools/movil_red.sh <serie> <dir> 1` (PC↔aparato por Wi-Fi
  desde el menú);
- `VB=4500 tools/movil_red.sh <serie> <dir> 1` (PC↔aparato por el cable;
  `VB=0` y `GUION=0` para jugar).

## Tras el paso 8: el sonido de cada jugador y quien se va (rama `etapa-10-paso-8-arreglos`)

Dos cosas que vio el dueño jugando Retroid↔S24.

### Los disparos del otro se oían en los dos

El sonido es parte de la partida, porque el juego lee el estado de la SPU:
no se puede dejar de reproducir en un aparato. Lo que se hace es no
**mezclarlo en la salida**.

- **Las voces:** `FUN_8002d8e4(sonido)` (o `FUN_8002d978`) pide un sonido.
  `FUN_8002e2c0(sonido, voz)` lo deja en el registro de esa voz
  (`DAT_800c4150 + voz*0x14`), y el gestor la enciende un paso después
  con `SpuSetKey`. Medido con `AC3_COOP_DBGSONIDO`.
- **De quién es:** al pedirlo se anota el contexto:
  - el 2: su paso y su vista (`g_busy`);
  - el 1: su paso de vuelo, de la entrada de `FUN_800715fc` a la de
    `FUN_80044f70`; o el tono de fijado (`0x32`), que el 1 pide desde el
    HUD del dibujo;
  - si no, común.
- **Al encenderse la voz** (`psx_spu_keyon_hook`), queda con su dueño; sin
  petición anotada, común. En cada aparato, las voces del otro van a
  `mute_out` de la SPU: no se suman a la salida, pero la envolvente, el fin
  y la captura siguen igual, así que la partida no cambia (las huellas son
  iguales).
- **Medido** (`AC3_COOP_DBGVOCES`): metralleta `0x11` en la voz 4, misil
  `0xe` en la 12, tono de fijado `0x32`.
  - Las voces 0-2 son el motor (sonidos 0, `0x54` y `0x55`, pedidos al
    empezar el vuelo en el paso del 1). Es uno, el del avión del 1, y se oye
    en los dos.
- `ac3_coop_set_local(1|2)` lo activa en red; en el PC, sin red,
  `AC3_COOP_LOCAL`.

### El avión de quien se iba seguía en la partida

- **Si se va el que se une,** quien creó la partida quita su avión
  (`ac3_coop_remove_p2`):
  - entidad inactiva, con solo los bits de su ranura, como antes de
    crearla;
  - vida a 0 y fuera de los blancos de la IA, como derribado;
  - su paso ya no se da ni reaparece. Todos los ganchos del cooperativo
    miran `p2_active`, así que dejan de actuar.
  - Sigue solo, con el aviso de siempre.
- **Si se va quien creó la partida:** su avión es el del juego y sin él no
  hay partida. El que se unió ve «El que creó la partida se ha
  desconectado» y vuelve al menú.
- En el PC, lo mismo para el anfitrión. Por eso `tools/red.py` compara las
  huellas hasta que uno cierra: si el invitado acaba antes, el anfitrión
  quita el avión en los últimos VBlank.

### Resultados

| Prueba | Resultado |
|---|---|
| `tools/red.py sonido`, en red: dispara el 1 y después el 2 | cada aparato deja sin oír las voces 4 y 12 del otro y oye las suyas; el motor, en los dos; huellas iguales |
| `tools/red.py sonido`: tono de fijado del 1 (misión 5), en el aparato del 2 | 8 peticiones, del jugador 1, sin oír |
| `tools/red.py se_va`: el invitado se cierra en el VBlank 3100 | el anfitrión lo quita en el 3104; el punto azul del radar, 4 px antes y 0 después |
| El resto de `tools/red.py` | igual |

## El mensaje de derribo de cada uno (rama `etapa-10-paso-8-derribos`)

El dueño vio que al derribar con el 2 no salía nada en su pantalla
(BULL'S-EYE...), y con el 1 sí.

- **Por qué:** `FUN_80045be0` procesa los enemigos derribados y saca el
  mensaje con `FUN_8004b980(6 o 7)` solo si la ranura del atacante
  (`+0x50 & 0x3F` del enemigo) es la 0. Medido con `AC3_WATCHWRITE`:
  `FUN_80045118` escribe la vida igual para los dos, y en ella el atacante
  es la 1 para los misiles del 2.
- **El mensaje** está en la estructura de `*DAT_800BB9BC` (el actual en
  `+0x11C`, 0xFF sin mensaje). Se avanza y se dibuja en el dibujo de la
  misión: `FUN_80033910` → `FUN_800336ec` → `FUN_8004b468`.
- **Arreglo, sin tocar la partida:**
  - en la simulación, al entrar en `FUN_80047c2c` (el registro del derribo,
    justo después) se comprueban las mismas condiciones con el atacante en
    la 1; el mensaje solo se anota;
  - el 2 tiene su copia de la estructura: en su redibujo, el mensaje nuevo
    se arranca allí con `FUN_8004b980` y se avanza con el código del juego;
  - el mensaje de un derribo del 1 (`FUN_8004b980` llamado desde
    `FUN_80045be0`) no sale en el redibujo del 2.
- **La voz del mensaje** (bandera 1 de su tabla, `DAT_800B0534 + n*0x1C`:
  los sonidos 75 y 76) va a la cola de sonidos con espera (`DAT_800C4E70`,
  30 pasos):
  - la del 2 se encola en la simulación de los dos aparatos, como hace el
    juego con la del 1;
  - cada hueco de la cola recuerda su dueño, y al pedirse (`FUN_8002ef8c` →
    `FUN_8002d8e4`) la voz queda con él, así que solo se oye en el aparato
    de quien derribó.
- **`SIN RESOLVER` / pendiente:** el juego sigue tratando al 2 como un
  atacante cualquiera.
  - Su misil quita 110 (de la tabla de armas por ranura,
    `DAT_800BB8C0 + ranura*0x40`), y el del 1, 160: en fácil, el del
    jugador se dobla.
  - El informe cuenta sus derribos como de un aliado (`FUN_80047c2c`).
  - Decidir con el dueño si se igualan.

| Prueba | Resultado |
|---|---|
| `tools/red.py derribo`: derriba el 1 y, aparte, el 2 (enemigo con 50 de vida, `AC3_COOP_VIDA`) | el rótulo (≈450 px) solo en la vista de quien derriba; su voz se oye en su aparato y no en el otro; la misma partida desde las dos vistas |
| El resto de `tools/red.py` y `tools/coop_vista.py huella autoprueba` | igual |

