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
- Lo que seguía tratando al 2 como un atacante cualquiera se iguala en la
  sección siguiente.

| Prueba | Resultado |
|---|---|
| `tools/red.py derribo`: derriba el 1 y, aparte, el 2 (enemigo con 50 de vida, `AC3_COOP_VIDA`) | el rótulo (≈450 px) solo en la vista de quien derriba; su voz se oye en su aparato y no en el otro; la misma partida desde las dos vistas |
| El resto de `tools/red.py` y `tools/coop_vista.py huella autoprueba` | igual |

## El 2 como el 1 ante los enemigos (misma rama)

El dueño pidió que los dos jugadores se comporten igual, y vio que los
enemigos no parecían ir a por el 2.

### Su daño y sus derribos

- **El daño** (`FUN_80045118`) sale de la tabla del jugador solo con el
  atacante en la ranura 0; con otra, de la tabla de armas por ranura
  (`DAT_800BB8C0 + ranura*0x40`), sin doblar en fácil. Medido: el misil
  del 2 quitaba 110 y el del 1, 160.
  - **Arreglo:** al entrar en `FUN_80045118`, los enemigos tocados por el 2
    tienen el atacante en la 0. Al entrar en lo siguiente de
    `FUN_80044f70` (`FUN_8004ae94`) vuelve a ser la 1, para que la IA y los
    derribos sigan sabiendo quién fue.
  - Medido con `tools/coop_vista.py misil`: el misil del 2 deja al enemigo
    2 en -20, como el del 1.
- **El informe** (`FUN_80047c2c`) cuenta como del jugador solo el atacante
  0. El derribo del 2 se anota ahora con el atacante 0: un registro de tipo
  0 y el contador de derribos del jugador (`DAT_800BB87C+0x2e`) en 1, como
  el del 1.

### Los enemigos y el 2

- **Le persiguen:** en las 41 misiones (`AC3_COOP_DBGIA`, 173 muestras),
  105 veces al 2 y 80 al 1. La elección de objetivo (`FUN_8004570c`) ya lo
  incluía desde el paso 6.
- **Le disparan:** el disparo de la IA es `FUN_80047344(k)`, con el blanco
  en `*DAT_800BB888 + k*0xB8 + 0x60`. Con el blanco en la 0 espera antes de
  disparar y avisa al jugador (`DAT_800C57EC | 1`). Con otro blanco
  disparaba enseguida: medido, misión 25, 8 misiles al 2 y ninguno al 1.
  - **Arreglo:** con el blanco en la 1, la llamada se hace con el contexto
    del 2 (`p2_begin`: su entidad en la 0, su copia del aviso de amenaza) y
    el blanco en la 0, y la original no se ejecuta. Para eso, un gancho de
    entrada puede saltarse la llamada (`mipsi_skip_call`, nuevo en el
    intérprete).
  - Igual con la IA de tierra (`FUN_80052184`, blanco en `+0x5B`), que
    ponía el aviso de fijado siempre en el del 1.
  - El bit de fijado del 2 se quita en el mismo sitio que el del 1
    (`FUN_80037fc8`). Los sonidos de esa IA siguen siendo comunes.
  - **Medido** (`AC3_COOP_MISIL`): misiles al 1 en las misiones 0, 18, 22,
    27, 30, 35 y 37; al 2, con su contexto, en la 26 y la 31. En la 25 ya
    no le disparan, como al 1. La partida, igual desde las dos vistas.
  - El 1 casi no tiene aviso de fijado (una vez, de tierra, en la misión
    22): los aviones disparan sin él.
- **`SIN RESOLVER`:** las órdenes del guion de «atacar al jugador»
  (`FUN_8004f77c`, blanco del guion `+0x1F` = 0, comportamientos 9 y 10)
  son siempre contra el 1.
  - Ejemplo: el enemigo 2 de la misión 37 ataca al 1 aunque el 2 esté
    detrás de él.
  - Se probó elegir al jugador más cercano y hacer la orden con el contexto
    del 2. No basta: el resto del paso del enemigo (`FUN_80045fc8`: giros y
    acercamiento) también usa la entidad 0, y el enemigo iba hacia el 1
    apuntando al 2. No disparaba a ninguno, y al 1 le llegó 1 misil en vez
    de 6. Se quitó. Hace falta ejecutar todo el paso de esos enemigos con
    el contexto del 2.

| Prueba | Resultado |
|---|---|
| `tools/coop_vista.py misil` | el misil del 2 con su vista: 140 → -20, como el del 1 (antes 30) |
| Informe tras el derribo de cada uno | igual: registro de tipo 0, 1 derribo del jugador |
| 41 misiones con `AC3_COOP_DBGIA` | perseguidores: 105 al 2, 80 al 1 |
| Misiones 22, 26, 31 y 37 (`AC3_COOP_MISIL`) | misiles al 1 (22, 37) y al 2 con su contexto (26, 31) |
| `tools/red.py`, `tools/coop_vista.py`, `tools/coop.py --vista2` | igual |

## Lo que vio el dueño en la misión 2 (misma rama)

La misión 2 (objetivos de tierra, algunos con torretas), en red Retroid↔S24.
Hubo cuatro cosas, más una separación de las partidas.

### Los derribos de tierra

- El mensaje de los de tierra lo saca otra función: `FUN_80050c80(j)` (la
  entidad `j + 0x20`), con el mensaje 7, o el `0x2E` en la misión `0x16`.
  El arreglo anterior solo cubría los aviones (`FUN_80045be0`).
- **Efectos:** el derribo de tierra del 1 salía en las dos pantallas, y el
  del 2 en ninguna.
- **Ahora:** la detección del derribo del 2 y la ocultación del mensaje del
  1 cubren las dos funciones.
- Medido con una opción de prueba, `AC3_COOP_MATA=k,v,j` (en el VBlank v la
  entidad k derribada por el jugador j), sobre la torreta 38: el rótulo
  (678 px) sale solo en la vista de quien la derriba, y la partida es la
  misma desde las dos vistas.

### Las unidades de tierra (torretas, tanques, barcos, lanzamisiles)

Todas van por la lista de tierra (`*DAT_800BB814`, entidades `0x20`-`0x3F`).

- **Solo tenían IA las cercanas al 1:**
  - el bucle de tierra (`FUN_8004ffd8` → `FUN_80050610` → `FUN_80051b5c`)
    exige la bandera `0x8000` de la entidad;
  - esa bandera la pone en cada paso `FUN_80050058`, y para los aviones
    `FUN_80038024`, si su registro de puntería del 1
    (`DAT_800C57F0[k]`) tiene `0x80`, cerca de él (`FUN_80044950`);
  - medido con `AC3_TRACE`: con el 1 lejos y el 2 junto a las torretas,
    `FUN_80050610` no se llamaba para ellas.
  - **Arreglo:** durante esas dos activaciones, los registros del 1 tienen
    también la cercanía de los del 2 (su copia de su paso) y después
    quedan como estaban.
- **Y apuntaban siempre al 1:**
  - sin blanco, `FUN_8004570c` atiende a una entidad por paso; si no
    hay, ponen la ranura 0;
  - el blanco se suelta tras cada disparo o fuera de alcance, en la misma
    llamada.
  - **Arreglo:** al entrar en `FUN_80051b5c`, sin blanco o con el 1, el
    jugador vivo más cercano. Su disparo de misiles (`FUN_80052184`) ya va
    con el contexto del 2.
- **Medido** con otra opción de prueba, `AC3_COOP_AQUI1/2=x,y,z` (cada
  jugador en un punto), y `AC3_TRACE` de los disparos
  (`FUN_80051dc4`, `FUN_80052184`), con uno a 1.500 de altura junto a las
  torretas y el otro lejos:
  - disparos con el 1 cerca: 135, 158 y 197 en tres sitios;
  - con el 2 cerca: antes 0; ahora 34, 119 y 151 (al 2 la distancia se
    mide de otra forma, por los tres ejes);
  - vida tras pasar junto a ellas: 88 el 1 y 88 el 2.

### La alerta de misil del 1 en la pantalla del 2

- El aviso de misil en camino (`FUN_8003a81c`, desde el paso de la cámara)
  mira el misil que va a la ranura 0 de la tabla de misiles:
  `*DAT_800BBA60`, con el bit k de la palabra 0, el número del misil en
  `+8+k` y la cuenta en `+0x88+k` (lo escribe `FUN_80067094`, desde cada
  lanzamiento).
  - Como el 2, en su vista, medía la distancia del misil que iba al 1 a su
    avión: volando cerca del 1 veía su alerta.
  - El 2 nunca tenía la suya.
- **Arreglo:**
  - como el 2, y en su redibujo, esa tabla tiene las ranuras 0 y 1
    intercambiadas;
  - los sonidos que pide el aviso de cada uno (la alarma) van con su
    dueño.
- **Medido:**
  - misión 37, misil hacia el 1 con el 2 al lado: el HUD rojo de la alerta
    (unos 1.300 px) solo en la vista del 1;
  - misión 26, misil hacia el 2: solo en la del 2 (unos 970 px);
  - la partida, igual.

### La separación de las partidas (`SIN RESOLVER`)

- **Qué pasó:**
  - en el VBlank 28380, a los 8 minutos, con combate de tierra y 4
    reapariciones del 2;
  - desde ahí, las 37 huellas siguientes fueron distintas (los dos
    aparatos lo vieron igual).
- **Por qué no se sabe todavía:** sin los mandos no se puede repetir.
- **Para la próxima:** el APK graba cada partida del menú Cooperativo en la
  carpeta de la app (`red_partida_*`):
  - la misión y las opciones;
  - la tarjeta;
  - los mandos de los dos puertos de cada VBlank;
  - la huella de cada 60.
- **`tools/red_repro.py`** la repite en el PC (`AC3_REPRO`) desde el 1 y
  desde el 2. Compara las huellas de cada aparato con la repetición, y las
  dos repeticiones entre sí, y dice el primer VBlank distinto y en qué
  bloques de la RAM.
- **Validado:** la Retroid crea la misión 2 y el PC se une, con los menús a
  mano y 6.001 VBlank. Las 100 huellas de la Retroid coinciden con la
  repetición, y las dos repeticiones dan la misma partida.

| Prueba | Resultado |
|---|---|
| Derribo de tierra (`AC3_COOP_MATA=38,3200,j`) del 1 y del 2 | el rótulo solo en la vista de quien derriba; la misma partida |
| Torretas de la misión 2 con cada jugador cerca | disparan a los dos (135-197 al 1; 34-151 al 2, antes 0); el mismo daño |
| Alerta de misil (misiones 37 y 26) | solo en la vista del jugador al que va el misil |
| `tools/red_repro.py` con una grabación de la Retroid | 100 huellas iguales; la misma partida desde el 1 y desde el 2 |
| `tools/red.py`, `tools/coop_vista.py` | igual |

## La misión 1 del dueño (Retroid↔S24, misma rama)

### La separación de las partidas, encontrada

- **Cómo se encontró:** con las dos grabaciones (`red_partida_*` de cada
  aparato) y `tools/red_repro.py`:
  - la Retroid coincide con la repetición desde el 1 en sus 728 huellas;
  - el S24, con la del 2;
  - las dos repeticiones se separan en el VBlank 10825, igual que los
    aparatos (3 huellas distintas, desde la 10860).
- **Dónde:**
  - con `AC3_DUMPMEM`, en el VBlank 10825 la RAM es igual, y en el 10826
    solo difiere una palabra de la pila (`0x801FFE10`);
  - con `AC3_WATCHWRITE` (y `AC3_WATCHWRITE_N`, nuevo: cuántas escrituras
    se imprimen), la escribe el bucle de `FUN_8007d870`, desde
    `FUN_8007e064` (el estado `0x80`, al acabar la misión). Desde el 1 da
    más vueltas antes del VBlank que desde el 2.
- **Por qué:** el reloj del modo nativo avanza un VBlank por cada 25.000
  entradas a función y saltos hacia atrás (`rc_budget`) cuando el juego no
  espera a VSync.
  - El redibujo desde el 2 ejecuta código del juego y gastaba de ese
    presupuesto, pero solo en el aparato que dibuja al 2.
  - En los bucles largos sin espera, su VBlank llegaba antes.
- **Arreglo:** el redibujo no gasta presupuesto (lo guarda y lo repone).
  Con él, las dos repeticiones dan la misma partida en los 43.713 VBlank.
- Probablemente es también la separación de la misión 2 (VBlank 28380), que
  no tenía grabación.

### Los sonidos que se oían en los dos aparatos

Medido repitiendo la grabación como el 2 con `AC3_COOP_DBGVOCES=1`, y
`AC3_TRACE` de `FUN_8002d8e4`, `FUN_8002d978`, `FUN_8002da14` y
`FUN_8002da48` para ver quién pide cada uno.

- **El pitido de «fijando» del 1** (0x2d) lo pide `FUN_8004038c`, la misma
  función que el tono de fijado (0x32), desde el HUD del dibujo, fuera de su
  paso de vuelo. Ahora es del 1, como el 0x32.
- **El aviso de que fijan al 1** (`FUN_80041c88` → `FUN_800417f8`: los
  sonidos de la tabla `DAT_800B0068`, 0x42, 0x40, 0x41...). Lo calcula la
  lógica del HUD de cada imagen (`FUN_80040c44`), también fuera de su paso:
  - **antes:** comunes, sonaban en los dos aparatos;
  - **ahora:** esa función se llama como el juego (los mismos registros y
    la misma pila, sin cambiar la huella) con sus sonidos del 1, o del 2
    en su contexto.
- **Las explosiones, los impactos y los aviones que caen** son sonidos con
  posición:
  - `FUN_8002f16c` (desde el daño, los derribos y los misiles) y el grupo
    de `FUN_8002f758`;
  - su volumen lo da `FUN_8002e5f8` con la distancia al oyente,
    `(0x3fff - (d << desp))²`;
  - el oyente de la partida es el jugador más cercano, y la partida es la
    misma en los dos aparatos.
  - **Ahora:** al pedir la posición (`FUN_8002e488`) se anotan la distancia
    al oyente y la distancia al avión de este aparato. La diferencia de
    volumen se quita solo en la salida de este aparato (`out_quiet` de la
    SPU, como `mute_out`).
  - **Medido:** un impacto a 994 del oyente y a 9.781 del 2 queda en el 18 %
    en el aparato del 2.
  - **Lo que no cambia:** el panorama sigue siendo el del oyente de la
    partida.
- **`SIN RESOLVER`:** el aviso de que fijan al 2 solo se calcula en su
  redibujo, que no deja sonidos, así que el 2 no lo oye. Su alerta de misil
  en camino sí suena.

### Los enemigos y el 2

- **Medido** en la partida del dueño (`AC3_COOP_DBGIA`, 47 medidas): 56
  perseguidores del 1 y 39 del 2. Le persiguen, algo menos que al 1.
- En la misión 1 no se llama `FUN_8004570c`, la elección de blanco que ya
  miraba al 2.
- **Las órdenes del guion de atacar al jugador** (9 y 10, `FUN_8004f77c`)
  son contra la entidad 0: ponen ese blanco y copian su posición.
  - Se probó hacerlas con el contexto del 2 cuando está más cerca. En la
    misión 37 el enemigo 2 cambia de blanco, pero no se demostró que llegue
    a perseguir al 2, y no se ha dejado.
  - Sigue `SIN RESOLVER`.

| Prueba | Resultado |
|---|---|
| `tools/red_repro.py` con las dos grabaciones de la misión 1 | la Retroid igual a la repetición; las dos repeticiones, la misma partida (antes, separadas en el VBlank 10825) |
| Sonidos de la partida repetida como el 2 | 0x2d del 1, avisos del HUD del 1; los sonidos con posición, desde el avión de cada uno |

## Las misiones 1 y 2 del dueño, otra vez: los ganchos que no cabían

- **Lo que vio el dueño** (Retroid el 1, S24 el 2; 59.057 VBlank y 984
  huellas, todas iguales):
  - en el aparato del 2 se oían el autopiloto del 1, su voz de BINGO y los
    sonidos con posición del 1 aunque estuviera lejos;
  - las torretas no le disparaban, y no sabía si el misil del lanzamisiles
    iba a por él.
- **La causa de los sonidos: la tabla de ganchos del intérprete** (`NHOOK`,
  `psx/recomp/mipsi.c`) era de 40.
  - El aparato con la vista del 2 registra 43 (medido: `coop: ... (N
    ganchos)`, nuevo en el registro):
    - quien crea, 39;
    - quien se une, más los del redibujo.
  - Los últimos no se registraban. Son los de los sonidos de cada aparato
    (el aviso del HUD, el volumen por distancia), que no cambian la
    partida, y por eso no se separaba.
  - El aviso de «sin sitio» iba a `stderr` y en el APK no se veía.
- **Arreglo:** la tabla es de 64, y el aviso va al registro.
- **Comprobado con la repetición de su partida como el 2**, con los mismos
  ganchos que el aparato (`AC3_RR=1`, `AC3_COOP_DBGVOCES=1`):
  - **del 1, no suenan en el aparato del 2:** el autopiloto (0x37, 4 de 4),
    su voz de BINGO (0x4c, 4 de 4), la metralleta (0x11, 54), el misil
    (0x0e, 11) y los tonos de fijado (0x2d, 0x32);
  - **del 2, sí:** su metralleta, sus misiles, sus tonos y su voz de BINGO;
  - **los sonidos con posición:** un evento a 2.985 del oyente y a 10.291
    del 2 no suena en el aparato del 2.
- **Las torretas sí le disparaban** (la misma repetición, con
  `AC3_COOP_DBGIA` y `AC3_TRACE=80051dc4`):
  - entre los VBlank 26.000 y 27.600 el 1 estaba a más de 70.000 y el
    antiaéreo (tipo 0x40) disparó unas 400 veces;
  - al 2 le dio una vez (vida 30 → 28).
- **El lanzamisiles** (unidad 6, tipo 0x41) tenía al 2 de blanco a 1.400.
  Lanzó en el VBlank 24.349 (`FUN_80067094`). Que saliera su alerta en el
  aparato del 2 dice que el misil iba a él: la alerta mira la ranura del
  misil.
- **Nuevo en `depurar.txt`:** `voces=1`, el dueño de cada sonido al
  registro (`AC3_COOP_DBGVOCES`).
- **Ojo con las pruebas:**
  - `AC3_TRACE` sobre una función con gancho lo sustituye (en
    `FUN_8002d8e4`, el de la cola de sonidos);
  - `tools/movil_red.sh` sin `MENU=1` ya no da la misma partida (VBlank 120,
    también con el commit anterior): con `depurar.txt` no se pasa la tarjeta
    de quien crea.
- **`SIN RESOLVER`:** el autopiloto del 2 (0x37) solo se pide en su
  redibujo y no suena en ningún aparato.

