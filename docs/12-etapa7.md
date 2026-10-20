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
| 6 | Release alfa en GitHub con el APK libre (`INSTALAR.md`) | ✅ v1.7-alfa; v1.8-alfa (pantalla ancha); v1.9-alfa (interpolación nueva); v1.10-alfa (dibujo por la GPU); v1.11-alfa (menú); v1.12-alfa (alta resolución) |
| 7 | Menú de inicio, opciones, idioma y actualizador (`LauncherActivity`, `Updater`) | ✅ probado en la Retroid (dueño); v1.11-alfa |

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

