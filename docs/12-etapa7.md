# 12 — Etapa 7: APK distribuible sin nada de Namco

Rama `etapa-7`, separada de `etapa-6` para no tocar lo que funciona. Decisión del
dueño del proyecto (2026-10-17), tras preguntar por qué otros proyectos de
recompilación publican sus APK.

## Por qué

El APK de la etapa 6 lleva **el código del juego traducido a ARM** y **`ACE.BIN`
byte a byte** (`.incbin`). Las dos cosas son de Namco y no se pueden distribuir
(`docs/06-legal.md`).

Otros proyectos sí publican sus binarios, pero es una zona gris tolerada:
- Zelda64Recomp publica el código traducido.
- Ship of Harkinian publica código decompilado.
- Ha habido retiradas: binarios del port de Super Mario 64 a PC (2020), re3 y
  reVC (2021).

El camino limpio es el de los emuladores: **el APK solo lleva código nuestro**.
El código del juego se lee del disco del usuario y se **interpreta** en el
aparato.

## Diseño

```
disco del usuario ──► ACE.BIN (psx/src/acebin.c: LZ de Namco + sumas, etapa 2)
                       │
                       ▼
RAM de la PS1 ──► intérprete MIPS (psx/recomp/mipsi.c) ──► capa psx/ (etapas 5-6)
                       │      llamadas a la librería de Sony por dirección:
                       └────► psx/recomp/hle_eu.c, hle_us.c (direcciones + envoltorios)
versión (EU/US) ◄── ejecutable de arranque del disco (psx/recomp/ac3_versions.c)
```

- **El intérprete** sigue la misma semántica que el código traducido
  (`tools/recomp/recomp.py`) y que el intérprete de referencia
  (`tools/mipsemu.py`):
  - huecos de retardo, accesos a memoria `rc_*` y fallos;
  - presupuesto del reloj al entrar en funciones y en los saltos hacia atrás.
- **Bucle plano:** interpreta hasta que el pc vuelve a la dirección de
  retorno que espera quien llamó. Las retrollamadas de la capa (VSync, CD)
  entran de nuevo.
- **Modo mixto:** `rc_call` usa el intérprete cuando no hay función traducida
  para el destino. Con el código traducido cargado, el intérprete solo
  atiende lo que falte.
- **Enlace con la librería:** `gen_hle.py --libre` genera la misma tabla que
  `hle_gen.c`, pero **sin las firmas de los overlays**, que eran los únicos
  bytes del juego. Quedan direcciones, nombres de funciones de PsyQ y
  envoltorios generados de nuestras cabeceras. Es dato de análisis y se
  versiona.
- **Versiones:** `ac3_versions.c` guarda las direcciones de cada versión
  (`ac3_main`, `$gp`, BSS, librería, overlays, búfer del mando, refresco). La
  versión se reconoce por `SCES_020.66` o `SLUS_009.72` en el disco.

## Pasos

| Paso | Qué | Estado |
|---|---|---|
| 1 | Intérprete MIPS en C (`psx/recomp/mipsi.c`) | ✅ hecho |
| 2 | Enlace con la librería sin bytes del juego (`hle_eu.c`, `hle_us.c`) | ✅ hecho |
| 3 | `ACE.BIN` leído y descomprimido del disco (`psx/src/acebin.c`) | ✅ hecho |
| 4 | APK libre (`android_game/build.sh`, ahora por defecto: `AC3Recomp-<versión>.apk`) y auditoría (`tools/apk_audit.py`) | ✅ probado en la Retroid: `VEL` 100 %, todo correcto (dueño) |
| 5 | Rendimiento en la Retroid | ✅ no hace falta: el intérprete llega a tiempo real |
| 6 | Release alfa en GitHub con el APK libre (`INSTALAR.md`) | ✅ v1.7-alfa; v1.8-alfa (pantalla ancha); v1.9-alfa (interpolación nueva); v1.10-alfa (dibujo por la GPU); v1.11-alfa (menú); v1.12-alfa (alta resolución); v1.12.1-alfa (arreglo del CD en la americana) |
| 7 | Menú de inicio, opciones, idioma y actualizador (`LauncherActivity`, `Updater`) | ✅ probado en la Retroid (dueño); v1.11-alfa |
| 8 | Mando táctil en pantalla (`android_game/src/touchpad.c`) | ✅ v1.12-alfa |
| 9 | Vibración (DualShock), pausa al volver a la app y salir al menú (SELECT + START) | ✅ probado en la Retroid por adb |

## Resultados

**Intérprete frente al de referencia** (`make mipsi-check`): comparador de
la etapa 6, con las mismas entradas que el código traducido.
- Primera pasada, 8 casos por función, residente europeo: 2.037 entradas,
  1.252 validadas, **0 distintas**.
- **Pasada completa** (24 casos, residente y 5 overlays): 3.365 entradas,
  2.260 validadas y **0 distintas**. Funciones de Ghidra validadas: **1.672 de
  1.821**, la misma cifra que el código traducido.

  | Módulo | Validadas de Ghidra |
  |---|---|
  | residente | 1130/1237 |
  | overlay 1 | 142/144 |
  | overlay 2 | 362/401 |
  | overlay 3 | 20/21 |
  | overlay 4 | 10/10 |
  | overlay 5 | 8/8 |

**Arranque completo en el PC** (`native_main`; capturas cada 100
fotogramas):

| | Capturas idénticas al código traducido |
|---|---|
| Europea, `AC3_MIPSI=1` (intérprete con la tabla `hle_gen.c`), 4.000 fotogramas hasta la misión en vuelo | **40/40** |
| Europea, `make libre` (`ac3_libre`, compilado solo con el repositorio) | **40/40** |
| Americana, `make libre` con el guion `GUION_US`, 3.600 fotogramas | **36/36** |

- **Coste:** la europea necesita 372 millones de instrucciones en 80 s de
  juego (4,7 millones por segundo); la americana, 469 millones en 60 s (7,8
  millones por segundo).
- En el PC, el binario libre corre a ~2,2× el tiempo real, frente a ~2,9×
  del traducido: 36,8 s y 27,8 s de CPU para la misma partida, con
  rasterizador y SPU incluidos.

**`ACE.BIN` del disco** (`psx/test/test_acebin.c`): idéntico byte a byte al
de `tools/acebin.py`: 700.416 B la europea y 704.512 B la americana.

**APK libre** (`ac3game-<versión>-libre.apk`, 140 KB):
- Paquete `com.vzepec.ac3game.libre`, que convive con el APK personal.
- **Auditoría** (`tools/apk_audit.py`): busca cualquier ventana no trivial de
  32 bytes de los `ACE.BIN` y overlays europeos y americanos (328.449
  ventanas) en todos los archivos del APK.
  - APK libre: **0 coincidencias**.
  - Caso positivo conocido, el APK universal 1.6: 334.355.

## SIN RESOLVER

- **Distribución:** la decisión de publicar es del dueño. `docs/06-legal.md`
  ya considera publicable "un APK que no contenga nada de Namco, que pida al
  usuario su propia copia del juego". La auditoría es la comprobación objetiva
  y hay que repetirla en cada release.

## Primera release pública (1.7-alfa)

Probado por el dueño en la Retroid: `VEL` 100 %, todo correcto. Desde ahora
**solo se genera el APK publicable**:
- `android_game/build.sh` usa por defecto `REGION=libre`;
- sale `AC3Recomp-<versión>.apk`, con el paquete principal
  `com.vzepec.ac3game`, así que sustituye a la app personal y conserva su
  tarjeta de memoria.

`etapa-7` se integra en `develop` y en `main`. La release `v1.7-alfa` adjunta
el APK, con la auditoría a 0 y su SHA-256, y usa `INSTALAR.md` como texto.

## Segunda release (1.8-alfa)

**Novedad: pantalla ancha (16:9)**, del paso 16 de la etapa 6
(`docs/11-etapa6.md`):
- la misión se ve en 16:9 de verdad, con el cono de visión del terreno
  ensanchado;
- menús y briefing se estiran a 16:9;
- los vídeos siguen a 4:3;
- L3+R3 recorre 60 fps + 16:9 → 60 fps + 4:3 → original.

Probada por el dueño en la Retroid. Mismo procedimiento que la 1.7:
- APK con la auditoría a 0;
- SHA-256 en la descripción;
- `INSTALAR.md` al día como texto.

## Tercera release (1.9-alfa)

**Novedad: la interpolación nueva** (etapa 8, `docs/14-etapa8.md`):
- en cada imagen intermedia, el mundo estático se redibuja con el código del
  juego y la cámara a medio camino;
- el redibujo va en un hilo de dibujo, con una segunda copia del intérprete;
- el HUD ya no queda encima del menú de pausa;
- en 16:9 desaparece la cuña del cielo en los bordes al ladear.

**Modos:** L3+R3 recorre 60 fps + 16:9 → 60 fps + 4:3 → original. La
interpolación anterior se quita de la app, a petición del dueño.

**Nombres:** las versiones de prueba se llamaron 1.9.4 a 1.9.6. Lo publicado
es la `v1.9-alfa`, con `versionCode` 21, para instalarse encima de la 1.9.6.

Probada por el dueño en la Retroid: "funciona mucho mejor", aunque con bajones
de fps mirando ciudades muy pobladas. Mismo procedimiento que la 1.7 y la 1.8.

## Cuarta release (1.10-alfa)

**Novedad: la imagen la dibuja la GPU** (etapa 8, pasos 1 a 3,
`docs/14-etapa8.md`):
- GLES a escala 1, con el tramado de la PS1;
- en la misión, la imagen solo en la GPU;
- atlas de subidas y ritmo de presentación: 60 fps estables en ciudad,
  medido en la Retroid con `adb logcat`.

**Correcciones:**
- la línea del HUD bajo las llamas de los motores;
- el avión doble de la repetición del final de misión;
- la estela doble de los misiles.

**Modos:** L3+R3 recorre `16:9` → `4:3` → `ORIG`. El modo de comparación por
software se queda fuera del ciclo.

**Validación final** (comparador, las 41 misiones, intermedias con la
interpolación nueva y `GUION_FUEGO`): 140.139 listas, 7 con un bloque malo
cada una; sin fallos.

**Nombres:** las versiones de prueba se llamaron 1.10.1 a 1.10.13. Lo
publicado es la `v1.10-alfa`, con `versionCode` 35.

## Paso 7: menú de inicio y actualizador (1.11-alfa)

**El menú** (`android_game/java/.../LauncherActivity.java`):
- Vistas de Android, sin librerías ni imágenes del juego; se maneja con la
  cruceta y A.
- Botones: Jugar, Actualizar (solo con versión nueva), Cambiar disco,
  Opciones, Acerca de y Salir, en dos columnas. Arriba a la derecha, el
  idioma (inglés por defecto, o español).
- Lo elegido se guarda en las preferencias `ac3game`: disco, idioma y
  «mostrar contadores y estadísticas» (el juego lo lee al arrancar con
  `nativeSetOptions`; el registro de `adb logcat` sigue igual).
- **El juego va en su propio proceso** (`android:process=":game"`): al
  salir, `android_main` hace `_exit` y se vuelve al menú; cada partida
  arranca de cero.
- **Icono propio**: un caza vectorial (`android_game/res/`), sin arte del
  juego.

**El actualizador** (`Updater.java`):
1. Al abrir el menú pide `/repos/vzepec/ac3-decomp/releases`. Como todas son
   pre-release, `/releases/latest` da 404 (comprobado).
2. Toma la release con la versión más alta: la de la etiqueta (`v1.11-alfa`
   → 1.11), comparada número a número con la instalada.
3. Descarga el APK y comprueba el tamaño y el `digest` SHA-256 que publica
   GitHub para cada archivo.
4. Lo pasa al instalador de Android (`ACTION_INSTALL_PACKAGE` con
   `ApkProvider`, un proveedor mínimo sin librerías). Android pide
   confirmación y solo acepta la misma firma.
5. Las novedades salen de `## What's new` (menú en inglés) o `## Novedades`
   de la release: **las releases llevan las dos secciones**.

**Probado y descartado** (con `adb logcat` en la Retroid, Android 13):
- **Reabrir el menú solo tras actualizar.** Android cierra la app para
  sustituirla. Ni el aviso de la sesión de `PackageInstaller` ni
  `MY_PACKAGE_REPLACED` pueden abrirla: «Background activity start»
  bloqueado.
- **Abrir el instalador dentro de nuestra tarea.** Android la cerraba con la
  app y el botón **Abrir** del instalador no abría nada. Ahora va en su
  propia tarea (`FLAG_ACTIVITY_NEW_TASK`) y **Abrir** vuelve al menú, que
  dice «Actualizada a la …» (marca de `UpdatedReceiver`).

**Prueba sin publicar nada:**
- `UPDATE_URL=http://127.0.0.1:8765/releases.json sh android_game/build.sh`
  genera un APK que consulta un servidor del PC (`adb reverse tcp:8765
  tcp:8765`, `python3 -m http.server`), con un `releases.json` del mismo
  formato que el de GitHub.
- **Un APK así no se publica.**

**Permisos nuevos:**
- `INTERNET`: solo la consulta y la descarga;
- `REQUEST_INSTALL_PACKAGES`: abrir el instalador.

## Quinta release (1.11-alfa)

- Menú de inicio, opciones guardadas, inglés/español, icono y actualizador
  (paso 7).
- El juego no cambia respecto a la 1.10-alfa.
- **Las pruebas se llamaron 1.11.1 a 1.11.13.** El instalado en la Retroid
  tras ellas lleva `versionCode` 48, así que la release lleva el 49.

## Paso 8: mando táctil (v1.12-alfa)

Petición del dueño: poder jugar sin botones físicos.

**Diseño:** tomado de otros ports a Android, solo como idea (sin su código,
que es GPL):
- el stick analógico flotante de HarvestMoon64Recomp, con el centro donde se
  apoya el pulgar;
- un botón para ocultar el mando;
- un ajuste para desactivarlo.

**Cómo está hecho:**
- **Nativo** (`android_game/src/touchpad.c`):
  - capa GLES semitransparente, dibujada antes de cada `eglSwapBuffers`;
  - multitoque: cada dedo se queda con el control que tocó; en los botones de
    cara y la cruceta se puede deslizar a un vecino del mismo grupo;
  - tamaños en dp (densidad de `AConfiguration`).
- **Gráficos:** los dibuja `MainActivity.touchAtlas()` con `Canvas` en un
  atlas de celdas de 128 px. Son dibujo propio: la auditoría del APK sigue
  a 0.
- **Entrada:** los botones se suman a los del mando físico, y el stick táctil
  manda mientras se toca.
- **Modo** (Opciones del menú):
  - automático: se oculta con cualquier evento de un mando físico y vuelve al
    tocar la pantalla;
  - siempre;
  - nunca.

**Probado en la Retroid por adb:**
- se dibuja encima;
- un toque en START (`adb shell input swipe`) avanza los menús;
- con START físico se oculta.

**Pendiente:** lo que diga el dueño de tamaños, posiciones, transparencia,
segundo stick para la cámara y vibración.

## Paso 9: vibración, pausa al volver y salir al menú

**Vibración.**
- `psx/src/libpad.c` presentaba el mando como uno estándar
  (`PadStateFindCTP1`) y `PadSetAct` estaba vacía. El juego registraba igual
  su búfer, pero sin configurar los motores.
- **Ahora** el analógico (0x73) se presenta como un DualShock con los motores
  listos (`PadStateStable`, ids 4/7, dos motores). Con `PSX_PAD_LOG=1` se ve
  lo que hace el juego:
  1. `PadSetMainMode(0, 1, 2)`: analógico y bloqueado;
  2. pregunta por los dos motores;
  3. `PadSetActAlign(01 ff ff ff ff ff)`: el byte 0 del búfer va al motor
     grande;
  4. `PadSetAct(…, 1)`.
- **En la misión** pide fuerzas de 0x54-0x73 (vibración suave continua) y
  golpes de 0xFF que bajan poco a poco. Visto en el PC con `GUION_FUEGO`
  (VBlank 3912-4000).
- **Sin cambios en la imagen:** comparador de la misión 0 con intermedias, 0
  listas malas.
- `psx_pad_motor` da la fuerza.
- **La app** (`app_extras`, hilo que presenta) se la pasa a
  `MainActivity.rumble`:
  - usa el motor del mando si lo tiene, si no el del aparato;
  - órdenes de 120 ms renovadas cada 80 ms, con la amplitud de la fuerza.
- **Comprobado en la Retroid** con `vibrar=200` en el archivo de depuración:
  `dumpsys vibrator_manager` registra las órdenes de la app con amplitud 0,78.
- Opción **Vibración** en el menú; permiso `VIBRATE`.

**Vibración extra (no es del original).**
- Medido en el PC con `PSX_PAD_LOG`: el juego **no** pide vibración al
  disparar misiles (○), con la ametralladora (✕) ni al acelerar (R1). Solo la
  pide con daños, roces con el suelo y choques.
- Opción del menú, desactivada por defecto, durante la misión (terreno
  dibujado):
  - golpe de 150 ms (fuerza 190) al pulsar ○;
  - traqueteo 95/45 a unos 8 Hz con ✕ mantenida;
  - temblor de 40 con R1.
- Se suma a la del juego: vale la mayor de las dos.
- **Comprobado en la Retroid:** amplitudes 0,75, 0,37 y 0,18 en
  `dumpsys vibrator_manager`.
- **Limitación:** en el menú de pausa del juego, ○ y ✕ también vibran.

**Pausa al volver.**
- Al salir de la app (`APP_CMD_PAUSE`) con el juego en marcha, queda en pausa
  (`G.hold`) hasta que se pulse un botón o la pantalla. El primer toque no
  llega al juego.
- Rótulo «Pausa» dibujado por `MainActivity`, en el idioma del menú.
- No se usa la pausa del juego: si ya estuviera en pausa, START la quitaría.
- Probado en la Retroid: inicio → volver → rótulo y VEL 0 % → ✕ sigue.

**Salir al menú.**
- SELECT + START mantenidos 1,5 s (mando físico o táctil) →
  `ANativeActivity_finish`.
- Probado con `adb shell input keycombination -t 2200`.

**Tarjeta de memoria atómica.**
- `card_flush` escribe a `.tmp`, hace `fsync` y renombra: salir en mitad de una
  escritura no deja la tarjeta a medias.
- `tools/psxcheck_card.py`: TODO BIEN.

