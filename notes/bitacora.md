# Bitácora de hallazgos

Registro append-only. Entrada nueva **arriba**. Cada hallazgo debe decir
**cómo reproducirlo**.

---

## 2026-10-15 — Las callbacks "de estado de pieza" son luces y rotores

Las callbacks de pieza de `0x800B09B8` no son superficies móviles: dibujan
luces, un resplandor, discos de hélice/rotor (3 o 5 palas, y un par
contrarrotatorio) y las toberas. Ghidra había partido mal estas funciones
(empezaban 4-8 bytes tarde; dos no existían); `FixFunctionStarts.java` las
crea en la dirección exacta que dan las tablas. Las superficies móviles
siguen pendientes: candidatas, los modelos "solo vértices" del avión del
jugador.

Reproducir: volcar `0x800B09B8` del ejecutable, aplicar
`FixFunctionStarts.java` y descompilar con `tools/ghidra/decomp.sh`.

## 2026-10-15 — Tablas de aparición de la misión: aviones y objetos de tierra

Confirmado en el código (`FUN_8004C258`, `FUN_8004CDB0`): el guion (s6) trae
dos tablas de aparición, aviones (44 B, ranuras 0-31) y objetos (36 B,
ranuras 32-63), con bando, tipo, modelo y posición. Casos positivos: los
modelos de avión de la misión 6 caen justo en los grupos LOD ocupados de s2;
los objetos usan la tabla de s4, con la misma forma para el mismo tipo en
misiones distintas, y están a ras del suelo de su tesela.

Reproducir: `python3 tools/acescript.py data/bpb/00{06,12,20}_*.bin`.

## 2026-10-15 — La sección 6 de la misión es el guion, no textos

`mission_start` (`FUN_80038818`) toma s6 como guion (`DAT_800BADC0`), con
variantes elegidas al azar si hay 3. La subtabla `DAT_800BAD70` de la
subentrada 0 tiene registros de 44 B con posiciones en coma fija: en la
misión 6, un registro de bando 0 a 7.500 de altura y 10 de bando 1 por
parejas en formación. Pista fuerte de la tabla de aparición de entidades;
sin confirmar con el código que la consume. También aclarado el mapa de
punteros de `mission_load`.

Reproducir: `python3 tools/acescript.py data/bpb/0006_*.bin`.

## 2026-10-15 — Preludio de misión: cielo por bandas y color de niebla

El preludio de 312 B del tipo 17 está comprimido con ULZ (no se había visto
porque el tamaño citado era ya el descomprimido). Campos localizados en el
código: niebla `+0x08` (color lejano de la GTE), cenit, cielo alto/bajo en
dos versiones (mirando al sol / de espaldas), color bajo el horizonte,
alturas de 4 bandas, rumbo del sol, sol/bruma/clima. El cielo se dibuja
como bandas de polígonos Gouraud a pantalla completa, no con textura.
Validación: alturas de banda decrecientes en todas las misiones probadas;
la 10 sale nocturna y la 40 sin sol, coherentes con su aspecto.

Reproducir: `python3 tools/acesky.py data/bpb/00{06,10,40}_*.bin`.

## 2026-10-15 — Explosiones, humo y estelas: texturas y sistema de partículas

El registro común 0000 contiene las texturas de efectos de combate (s0:
explosiones/fuego/humo en rejilla de 64 px; s1: llamas, restos, estelas).
Validado con un caso positivo: los fotogramas 6-15 que recorre
`FUN_80084324` forman una animación de explosión coherente
(`tools/acefx.py frames`). Documentados el registro de partícula (28 B), los
4 tipos, la cola de eventos de efecto, la composición del impacto de misil y
cómo se dibujan los proyectiles en vuelo (segmentos cabeza-cola, no humo).
Sin resolver: qué lista de proyectil es el misil estándar; ritmo de ticks
(¿25 Hz en PAL, 30 en NTSC?).

Reproducir: `python3 tools/acefx.py sheet data/bpb/0000_*.bin` y búsqueda de
CLUT `0x40x6`-`0x45x6` en `data/ghidra/all_ace_eu.c`.

## 2026-10-15 — Efectos del entorno: nubes, sol y bruma (las explosiones, no)

Buscando las explosiones por sus CLUT constantes en el código: s13 TIM 1 es
el campo de nubes (`FUN_8008DD10`), s13 TIM 2 el sol y su destello
(`FUN_8008A310`, no una bola de fuego como sugería su aspecto) y s13 TIM 0 las
capas de nubes/bruma (`FUN_8008C1C8`, con parámetros en la sección 8). La
sección 8 y el preludio son, pues, parámetros de entorno. Explosiones sin
localizar.

Reproducir: búsqueda de `0x4F16`, `0x4F56`, `0x4E56`, `0x4ED6` en
`data/ghidra/all_ace_eu.c` (DumpAll) y exportar s12-s14 con `tools/tim2png.py`.

## 2026-10-15 — Etapa 4: criterio de salida cumplido; qué es del juego y qué no

El usuario confirma que la 0.3 funciona en la Retroid. Aclarado lo que no es
del juego: la estela del misil (el "cuadrado blanco"), las explosiones, el
cielo, la niebla y la luz del avión son aproximaciones propias; terreno,
modelos, pieles y el modelo del misil sí vienen del disco. Se cierra la etapa
4 como medición y se retoma la etapa 3.

## 2026-10-15 — APK 0.3: misiles, enemigos, HUD de fps y controles invertidos

Confirmado por el usuario que la 0.2 funciona en la Retroid. La 0.3 añade:
enemigos con los aviones de la misión (s2, LOD alto) y una IA sencilla;
misiles con el modelo real del juego (s3 entrada 3: 12 vértices, 8×8×52,
igual en todas las misiones) guiados al blanco fijado; explosiones; contador
de fps y de derribos. El cabeceo estaba al revés de lo que decía el código:
ahora palanca hacia delante = morro abajo, como el original. Cámara a la
mitad de distancia. Verificado en el PC con `android/test/test_render.c`.

## 2026-10-15 — APK 0.2: la imagen de disco se elige con el selector del sistema

En la Retroid la versión 0.1 se quedaba en magenta: no encontraba la imagen
(falta de permiso o carpeta no prevista). Ahora una actividad Java mínima
abre el selector de archivos (Storage Access Framework), guarda el URI con
permiso persistente y pasa el descriptor al código nativo por JNI. Sin
permisos de almacenamiento. Si el archivo no es un disco de AC3, se vuelve a
pedir; Select + Start permite cambiarlo.

## 2026-10-15 — Etapa 4: primer APK de la rebanada vertical

`android/`: app nativa (NativeActivity, C, GLES3) que lee la imagen de disco
del usuario y vuela un avión del jugador sobre el terreno de una misión. El
port a C de las herramientas coincide con Python (misión 6: 432 teselas, 5042
triángulos, 291 TIM; F-15: 454 triángulos). El render (VRAM simulada en
textura R16UI y CLUT en el shader) se verificó en el PC con Mesa: ciudad,
bahía y el avión con su piel. Sin tope de fps, vsync nativo.

Reproducir: `make apk`; `android/test/test_render.c` (ver `android/README.md`).

## 2026-10-15 — Callbacks por pieza; t04 s0 = parámetros del avión (motores)

Las piezas de `lod_parts_draw` llevan un callback tomado de dos tablas
(`0x800B09B8`, `0x800B09D0`) según el tipo de avión. El callback 1,
`0x8008A874`, dibuja el efecto de los motores con datos de t04 s0: su
`+0xC` byte 0 es el número de motores, verificado contra la realidad (F-16 =
1, bimotores reales = 2). Corrige la descripción anterior de esa función como
"iluminación".

Reproducir: tabla de `+0xC` de t04 s0 con `tools/acerec.py` (ver
`docs/04-formatos.md`).

## 2026-10-15 — Papel de los overlays 1 y 2 desde el código

Su primer paso de modo inicializa una máquina de subpantallas de menú según
banderas del estado y restaura la última misión y avión elegidos. Con sus
cadenas: overlay 1 = menú principal; overlay 2 = interfaz de campaña entre
misiones. Estados de pieza del avión: el repartidor prepara los bloques de
vértices de todas las entradas; la elección/interpolación debe estar en el
callback por pieza (sin localizar).

## 2026-10-15 — Correspondencia registro → avión, por silueta

Silueta cenital de cada avión del jugador frente a las 21 vistas del hangar:
los registros 0048-0068 son los 21 aviones en el orden del hangar (IoU
0,77-0,95, siempre máximo en el que toca); el 0047 es otro XR-900 Geopelia
(prototipo oculto). Primer intento fallido por orientación (el hangar tiene
el morro a la derecha): ahora se prueban rotaciones y espejo.

Reproducir: `python3 tools/acemodel.py hangar data/bpb`

## 2026-10-15 — Avión del jugador exportado con texturas (UV validadas)

`acemodel.py export-player` saca `.obj` + `.mtl` + texturas. Rasterizados en
vista cenital, el 0047 (ala volante blanca y roja) y el 0055 (F-15 azul)
salen perfectos. Corrección: las 8 filas de CLUT de la piel son paletas por
zona, no esquemas de color.

Reproducir: `python3 tools/acemodel.py export-player data/bpb/0055_*_t04.bin data/obj/p55`

## 2026-10-15 — Preludios de los tipos 17 y 13

Tipo 17: 312 B de parámetros de entorno (colores; `FUN_8005D490` genera con
ellos 16 paletas animadas). Tipo 13: 36 TIM de 56×84 = las imágenes de las 36
misiones.

Reproducir: `Record.prelude_bounds()` de `tools/acerec.py` + `tools/ulz.py` +
`tools/tim2png.py` (ver `docs/04-formatos.md`).

## 2026-10-15 — Escalas ×1/×2/×4 confirmadas en el código; ensamblado del jugador

`scene_objects_sort` elige el bloque de vértices por distancia (×1 lejos, ×2
medio, ×4 cerca) y ajusta la traslación: confirma la hipótesis de precisión.
El avión del jugador se cuelga como tabla de 12 piezas en el hueco id 0 /
nivel 3 de la tabla LOD, y el render toma entradas concretas (e1, e6, e8):
algunas son estados alternativos de piezas móviles.

Reproducir: `make ghidra-import`, DumpAll y leer `scene_objects_sort` y
`FUN_80032514` (ver `docs/09-codigo.md`).

## 2026-10-15 — ENCONTRADO el avión del jugador: el preludio de los registros

Siguiendo en el código quién rellena el modelo 0 de la tabla LOD (el del
objeto del jugador) se llegó a `0x80035C90`, que no era una variante del
cargador de misión sino la **carga del registro de tipo 4**: descomprime un
bloque ULZ situado entre la cabecera y la sección 0 y recoloca sus modelos.

Error de fondo de la etapa 1: `header_size` es el offset de una sección
inicial (el preludio). Todas las búsquedas anteriores la saltaban.

Formato: 12 `LodModel` por avión (1 clásico, 4 iluminados con normales y
primitivas de 24 B, 7 de solo vértices = estados de piezas móviles).
Validado 264/264 entradas y 7.895/7.895 primitivas con la textura y la CLUT
de la piel del jugador.

Reproducir: `python3 tools/acemodel.py player data/bpb`

## 2026-10-15 — Overlays en Ghidra: ~580 funciones propias más

Overlays 1-5: 141, 401, 21, 10 y 8 funciones; solo `CdMix` es de librería.
Total de código propio del juego: unas 1.840 funciones (~690 KB). Papeles
probables por cadenas: menú/carga (1), interfaz entre misiones (2), datos
gráficos (3), BGM (4), títulos de temas (5).

Reproducir: `make overlays` e importar `data/exe/ovl/overlayN.exe` con
`CreateMissingFunctions.java` y `AceStats.java` (ver `docs/09-codigo.md`).

## 2026-10-15 — Overlays confirmados: las entradas 1-5 del BPB son código

La tabla de modos de `ACE.BIN` (`0x800AE544`) lista 10 modos; los 3, 4, 7, 8 y
9 cargan los overlays 2, 1, 3, 4 y 5 en `0x800C9DC0`. Overlay k = entrada k
del BPB: tamaño según el tag == rango de memoria en 4/5 (el otro, 4 bytes de
diferencia); punteros de pasos en inicios de función. Corrige la etapa 1 (que
las llamó "tablas de sistema") y la etapa 3 (que dijo que no había overlays).

Ningún overlay contiene modelos. El avión elegido (`+0x392` del estado) pasa
al objeto del jugador (`+0x48`) por una tabla identidad en `0x800234E4`.

Reproducir: `make overlays`

## 2026-10-15 — Hay overlays de código (corrige la etapa 3) y nombrado reproducible

La tabla de modos (`0x800AE544`) revela modos con código cargado en
`0x800C9DC0`: las entradas 1 y 2 del BPB son overlays (casan en tamaño y
densidad de prólogos). Pendiente confirmar el offset de carga. Corregido
también qué función usa "Can't open TIM". Nuevo `tools/ghidra/names.tsv` +
`ApplyNames.java` (40 nombres).

## 2026-10-15 — Revisión de proyectos externos; el SPB no tiene modelos

No hay decompilación pública de AC3. `loadwordteam/ac3es-tools` confirma los
ULZ de tipos 0 y 2. Krishty extrajo los aviones jugables de la versión
**japonesa** (37 carpetas; tres juegos de vértices; normales por vértice),
pero nadie ha documentado la internacional. Ver `docs/03-versiones.md`.

Descartado el SPB: sus 28.446 sectores de datos (los Form 2 intercalados con
el audio, leídos con 2324 bytes) no contienen ULZ, TIM ni modelos; parecen
marcas de sincronía por canal. Una búsqueda exhaustiva de bloques de vértices
en todo el BPB descomprimido solo encuentra los de t17 s1-s4.

## 2026-10-15 — Render de aviones jerárquico, demos, y el avión del jugador sigue sin aparecer

- `0x80061408`: render de modelos LOD por piezas con matriz propia (aviones
  articulados); `0x80065CC4` elige LOD = id·4 + (0-2 según distancia).
- `0x8002953C`: id de recurso = bit 11 pack + 11 bits de entrada.
- Tablas en `ACE.BIN`: misiones (`0x800AF748`), aviones del jugador
  (`0x800AF79C`, registros 47-68) y demos (`0x800AF7C8`, registros 111-126 con
  misión y avión). **Los registros de tipo 1 grandes son las demos.**
- Huecos 0-7 de la tabla de modelos LOD vacíos en las 41 misiones: se rellenan
  en ejecución. El modelo del jugador no está en ningún registro analizado.
  Propuesta: análisis dinámico con emulador.

Reproducir: `tools/ghidra/decomp.sh ace_eu.exe data/ghidra/x.c 0x80061408 0x80065cc4 0x80035e28 0x800354b8`
y la tabla con `python3 -c` sobre `data/exe/ace_eu.bin` en `0x800AF79C`.

## 2026-10-15 — Bits altos del mapa de teselas: no reutilizar vértices vecinos

`FUN_8005CDA4` construye la lista de celdas visibles desde el mapa y copia la
celda entera en la palabra de control; el render anula con los bits 9-11 de la
celda los bits 25-27 ("reutilizar vértices de la vecina"). Contraste: bordes
izquierdos que no casan, 0,7 % con el bit a 0 frente a 60 % / 47 % / 28 % con
los bits 11 / 9 / 10 a 1. Explica el 0,6 % residual de la etapa 1.

Reproducir: `python3 tools/acemodel.py tilemap data/bpb`

## 2026-10-15 — Render de terreno y objetos localizado

`0x8005FF9C` (pasada de vértices con `RTPT`, que confirma el grupo de 20 bytes
desde el código), `0x80060214`/`0x80060470` (FT4/GT4 con `NCLIP`, `DPCS` y
tabla de orden) y `0x80060DC0` (subdivisión, que usa los bytes 8-15 de la
primitiva como offsets GTE). Se encontró buscando funciones que copian las
palabras `0x14`-`0x2C` desde el mismo registro base (las palabras del paquete
GPU que ya trae cada primitiva). `h0` no aparece en estas rutas.

Reproducir: `tools/ghidra/decomp.sh ace_eu.exe data/ghidra/r.c 0x8005ff9c 0x80060214 0x80060dc0`

## 2026-10-15 — Geometría del mapa de teselas confirmada desde el código

`FUN_80098B80`: celda = `(coord >> 23) + 128` (celdas de 2048 unidades, origen
en la celda 128,128), mapa por filas de Z, `& 0x1FF` = índice, 0 = sin tesela.
Coincide con lo deducido de los datos (tamaño de tesela, base 1). Los 3 bits
altos no se usan en la colisión.

Reproducir: `tools/ghidra/decomp.sh ace_eu.exe data/ghidra/t.c 0x80098b80`

## 2026-10-15 — Primitivas resueltas: h1, e0/e1 y la sección 5 (normales)

Leyendo el código de intersección con el terreno de `ACE.BIN` (`0x80097EDC`):
`e0`/`e1` son offsets a normales de cara en la sección 5 del registro de
tipo 17; `h1` = 0 cuadrilátero plano, 1 triángulo, 2 cuadrilátero partido en
dos triángulos con una normal cada uno.

Validado en los 41 registros: 119.003 `e` dentro de s5 y `máx(e)+8 = len(s5)`
en 41/41; clases de `h1` sin excepciones; 77.831/77.831 normales
antiparalelas al producto vectorial de la cara; partición de `h1 = 2` en el
99,7 %.

Dos falsos candidatos antes de dar con ella (efectos con `RTPT`). La pista
buena: funciones con mucho `lwc2` y `NCLIP` y sin `RTPT`.

Reproducir: `python3 tools/acemodel.py normals data/bpb`

## 2026-10-15 — La "inicialización" de modelos es una recolocación

`FUN_80026EE0` suma la base del modelo a `n` offsets de su cabecera; `n` = 4
para s1/s4 y 6 para s2/s3 (leído del registro `a2` en cada llamada del
cargador de misión). Confirma las dos cabeceras de modelo desde el código.

Intento fallido: buscar el render de modelos por huella (`RTPT` y pasos de
36/48 bytes) dio dos falsos candidatos, que resultaron ser efectos e
iluminación. Queda seguir el flujo de datos desde `0x2CC($gp)`.

Reproducir: `tools/ghidra/decomp.sh ace_eu.exe data/ghidra/m.c 0x80026ee0`
y el desensamblado de las llamadas en `0x80035A60`, `0x80035AB0`,
`0x80035B00`, `0x80035B50`

## 2026-10-15 — Cargador de misión localizado; Ghidra se dejaba 155 funciones

El análisis automático de Ghidra no creaba las funciones que solo se invocan
por puntero: 158 prólogos huérfanos. `tools/ghidra/CreateMissingFunctions.java`
crea 155 (las otras 3 caen dentro de las nuevas). Nueva cifra de `ACE.BIN`:
2.011 funciones, ~1.262 propias (473 KB).

Entre ellas, el **cargador de registros de tipo 17** (`0x80035730`): sube las
TIM de s10-s14, manda s15 (sonido) sin descomprimir, descomprime s0-s10 e
inicializa las tablas de modelos (s1, s4 por un lado; s2, s3 con un
parámetro distinto) y fusiona s3 en los huecos 96+ de s2. Confirma desde el
código el mapa de secciones de la etapa 1.

Se encontró al ver que la cifra de llamadas `jal` al descompresor ULZ no
cuadraba con los llamadores que listaba Ghidra.

Reproducir: `make ghidra-import ghidra-stats`;
`tools/ghidra/decomp.sh ace_eu.exe data/ghidra/m.c 0x80035730`

## 2026-10-15 — El código resuelve el tag del SPB y la variante 0 de ULZ

Localizados en `ACE.BIN` el sistema de packs (`0x8002A9E4`: valida `AC3E`, suma
la LBA del pack y exige los códigos de build `0x1019`/`0x19991019`), el
arranque de streaming (`0x8002A34C`) y el descompresor ULZ (`0x800AE118`).

- **Tag del SPB:** bit 31, modo (bits 27-28), desplazamiento (24-26), canal XA
  (19-23), longitud (0-18). Validado 72/72 EU y 88/88 US contra las
  subcabeceras XA. Las entradas de audio están entrelazadas (un canal cada
  una) y se solapan.
- **ULZ variante 0:** bit centinela en cada palabra de flags y palabra 0 como
  terminador. 604/604 entradas del tipo 13 válidas.
- **Tipo 13:** vistas cenitales y nombres de los 21 aviones seleccionables.

Reproducir: `make ulz-check`; `python3 tools/acepack.py <imagen> spbtags`;
`python3 tools/mipsref.py data/exe/ace_eu.bin 0x80010000 --addr 0x800ae7a0 --tsv data/ghidra/stats_eu_funcs.tsv`

## 2026-10-15 — Etapa 3: Ghidra, cifras de funciones, módulos y bucle principal

Ghidra 12.1.4 + `ghidra_psx_ldr` (en `~/tools`). `ACE.BIN` EU: 1.853
funciones; 747 reconocidas como PsyQ 4.5 (77 KB) y ≤ 1.106 propias (420 KB);
1.939 instrucciones COP2. Control positivo: la única referencia a
`ResetGraph:jtb` está en la función nombrada `ResetGraph`; en `SCES_020.66` se
reconoce el 95 %. Forzar PsyQ 4.6 reconoce menos (456).

Mapa de módulos: `SCES_020.66` carga y valida `ACE.BIN` y lo ejecuta; si falla,
carga `ACE.EXE`, que es la **pantalla de aviso anticopia** ("WARNING! An error
has occurred while loading…"). `ACE.BIN` no carga más código.

Bucle principal: `main` en `0x80025D08`, bucle infinito por fotograma con
`GetRCnt`/`DrawSync`/`VSync(n)`.

El descompresor real de `SCES_020.66` (`0x801641E4`) confirma el formato de
`ACE.BIN` y resuelve los campos pendientes: el byte `0x5C` es el número de
bits de distancia (12); los flags usan bit centinela y un byte 0 termina el
flujo; el campo `0x04` hace que la suma `u32` del archivo sea 0 (verificado
en EU y US); desde `0x1C` hay una estructura `EXEC` de PsyQ.

Reproducir: `make acebin ghidra-import ghidra-stats`; ver `docs/09-codigo.md`

## 2026-10-15 — ACE.BIN descomprimido (etapa 2 cumplida)

LZ de un solo flujo desde `0x5D`: byte de flags con 7 elementos (bits 0-6, 1 =
literal), referencias de 2 bytes `longitud = b0>>4` (0 → 16) y `distancia =
b1 | (b0&0xF)<<8` (0 → 4096). Se descifró siguiendo cadenas conocidas
("Can't open TIM", "Error: MemAlloc: Overflow…").

Validación definitiva: el campo `0x58` de la cabecera es la suma de las
palabras `u32` de la salida, y coincide exactamente en EU (`0x4288FFB0`) y US
(`0xA6E2BA6C`). La salida mide lo declarado (700.416 / 704.512) y la entrada
se consume entera salvo el relleno. El campo `0x14` (antes "¿número de
secciones?") es el tamaño de la cabecera.

Pista que no funcionó: un LZSS clásico (Okumura) consumía solo la mitad de la
entrada. Diferencias: 7 flags por byte, no 8, y longitudes sin desplazamiento.

`ACE.BIN` no contiene TIM ni modelos en formato conocido: el avión del jugador
sigue sin aparecer.

Reproducir: `make acebin`

## 2026-10-15 — Bancos de sonido, aviones con LOD y el SPB (corrige una hipótesis)

La "siguiente tarea" suponía que la sección 2 del tipo 4 era el modelo del
avión con punteros a código. **Falso**: es un banco de sonido del SPU, igual
que la sección 15 del tipo 17 y la 8 del tipo 13. Los valores que cambiaban
entre EU y US son la dirección base en la RAM del SPU. Validado en 63/63
bancos por disco; 15 M muestras PCM decodificadas sin recortes.

Los aviones están en la sección 2 del tipo 17: modelos de 4 en 4 (LOD), con
tres bloques de vértices a escala ×1/×2/×4 en el LOD alto, y primitivas
POLY_GT3/GT4. 578/578 válidos; el render del modelo 8 del registro 6 es un
avión. Pero usan las pieles de la misión, no la del jugador: el modelo del
avión del jugador sigue sin aparecer.

`ACE.SPB` es audio XA y vídeo (subcabeceras de sector Form 2). El tipo 13 son
imágenes de interfaz en ULZ de variante 0 (no resuelta del todo).

Reproducir: `make sounds`; `make models`;
`python3 tools/acepack.py <imagen EU> xa`;
`python3 tools/acemodel.py obj data/sec/0006_tage0028567_t17_s02.bin 8 data/obj/avion.obj`

## 2026-10-15 — Primitivas de los modelos y base del mapa de teselas

Bloque 1: `u32 n` + POLY_FT4 de 36 B (código `0x2C`); bloque 2: `u32 n` +
POLY_GT4 de 48 B (`0x3C`, 4 colores). Índices `u8` en orden de tira, UV, CLUT
y tpage en el orden del paquete GPU. Validado en 14.095/14.095 modelos (bloques
exactos, códigos, índices, offsets GTE). Lo descifró un sub-agente; se
re-verificó integrándolo en `tools/acemodel.py`.

El índice del mapa de teselas es base 1: continuidad de bordes 99,4 % frente a
74,6 % con base 0.

Reproducir: `make models`; `python3 tools/acemodel.py tilemap data/bpb`;
`python3 tools/acemodel.py obj data/sec/0006_tage0028567_t17_s04.bin 0 data/obj/o.obj`

## 2026-10-15 — ACE.DAT es relleno (corrige una interpretación anterior)

De sus 20.672 sectores solo 40 (0,19 %) tienen algún byte distinto de cero,
con dos patrones de 108 bytes. Está al final del disco. No contiene assets.
La hipótesis de `docs/01-disco.md` ("la EU sacó contenido del SPB a ACE.DAT")
era **falsa** y se ha corregido.

Reproducir: `python3 tools/acedat.py <imagen EU>`

## 2026-10-15 — EU y US comparten terreno, modelos y texturas

Comparando sección a sección (descomprimida) los 127 registros: en los 41 de
tipo 17 las secciones 0-5 y 7-14 son idénticas byte a byte. Solo cambian la 6
(textos de misión: la EU lleva 5 idiomas) y la 15. En el tipo 4 cambia solo la
sección 2, en valores tipo `0x043C90`/`0x0496D0` dentro del rango de
`ACE.BIN`: hipótesis de punteros a código (`SIN RESOLVER`).

Reproducir: extraer el BPB americano a `data/bpb_us/` y comparar con
`tools/acerec.py` (ver `docs/04-formatos.md`, tipo 17).

## 2026-10-15 — Vértices de modelos resueltos (criterio de salida de la etapa 1)

Las secciones 1 (teselas de terreno) y 4 (objetos) del tipo 17 son tablas de
modelos. Vértices en grupos de 3: `x0,y0,x1,y1,x2,y2,z0,z1,z2,pad` (`s16`), el
layout que necesita el GTE para `RTPT`. Validación: tamaño del bloque
coherente en 14.095/14.095 modelos y bbox de cabecera == bbox real en 421/421
objetos.

La sección 0 es un mapa de teselas 256×256: `max(v & 0x1FF)` == número de
teselas de la sección 1 en los 41 registros, y pintado muestra mar, costa y
ciudad, igual que la textura de radar.

Reproducir: `make models`; `python3 tools/acemodel.py verts data/sec/0006_tage0028567_t17_s04.bin 0`

## 2026-10-15 — Primera textura en PNG (criterio de salida de la etapa 1)

Las texturas **sí** son TIM estándar, pero dentro de secciones ULZ. El
"resultado negativo" anterior (cero TIM) se debió a buscar sobre datos
comprimidos: lección para la próxima vez, medir la entropía antes de buscar
magics. 9.288 TIM solo en la sección 10 de los 41 registros de tipo 17.
Verificado a ojo: fuente con acentos, piel de avión, mapa de radar.

Reproducir: `make textures REC=0006` → `data/png/`

## 2026-10-15 — Compresión ULZ de Namco resuelta

Las secciones de los registros empiezan por `Ulz\x1a`. Cabecera de 16 bytes
y tres flujos (flags `u32` MSB primero, literales `u8`, referencias `u16` con
`nbits` de distancia). Validación: holgura de flujos 4 / 0-3 / 0-2 bytes en
659/659 secciones.

Reproducir: `make ulz-check`

## 2026-10-15 — El tag del BPB es el tamaño del registro

`(tag & 0xFFFFFF) * 4` == tamaño del registro en 127/127 (EU) y 127/127 (US).
En el SPB no (0/72, 0/88).

Reproducir: `make tags`

## 2026-10-15 — Cabecera común de registro resuelta, y las texturas NO son TIM

Los registros de `ACE.BPB` comparten layout:
`u32 type; u32 header_size; u32 section_offset[(header_size-8)/4];` con los
offsets monótonos crecientes y dentro del registro. Verificado en **122 de
127** registros; las 5 restantes (entradas 1-5) son tablas de sistema con otra
convención.

El tipo determina el número de secciones de forma consistente: tipo 1 → 0
secciones (18 registros), tipo 4 → 3 (22), tipo 5 → 4 (40), tipo 13 → 12 (1),
tipo 17 → 16 (41).

**Resultado negativo:** se buscó la cabecera TIM (`10 00 00 00` + flags de
modo plausibles) en registros de tipo 17 y 4 → **cero coincidencias**. Las
texturas no se guardan como TIM suelto. Probablemente van como bloque listo
para `LoadImage` a VRAM, sin cabecera. No repetir esta búsqueda.

En las entradas 1 y 2 el segundo `u32` es un puntero a RAM ya resuelto
(`0x800CBE04`, `0x800CC004`): revela el mapa de memoria en ejecución.

Reproducir: `make records`

## 2026-10-15 — Herramientas validadas contra el disco americano

Mismo formato de índice (`AC3E`, `kind` 1 y 3, 8 bytes por entrada) y misma
cabecera de registro. El SPB americano tiene más categorías (`0xF5` ×8,
`0xF8` ×10 frente a ×2 en el europeo), coherente con que la europea movió
contenido a `ACE.DAT`.

Confirma que el formato es **del motor**, no de una versión. El trabajo hecho
sobre la europea se transfiere.

Reproducir: `python3 tools/acepack.py "Rom/.../Ace Combat 3 ... (USA).bin" index`

## 2026-10-15 — Formato de los índices de assets resuelto

Cabecera de 20 bytes (`AC3E`, kind, fecha BCD, desconocido, count) y entradas
de 8 bytes (`tag`, `sector`). Validado porque `20 + 8*count` da exactamente
el tamaño del archivo en las **cuatro** combinaciones EU/US × SPH/BPH.

Los offsets están **en sectores de 2048 bytes** dentro del pack.

**Trampa:** el índice **no está ordenado por sector** (en `ACE.SPH` europeo la
entrada 71 apunta al sector 19400, antes que la 70). Hay que ordenar por
sector para deducir los tamaños. Con ese criterio la suma cubre el **100 %**
de los dos packs, lo que confirma la interpretación.

Reproducir: `make index` y `make inventory`

## 2026-10-15 — Contenido identificado en ACE.BPB

- Entrada 1: `CRD0`, `bu00:BESCESP02066AC3PAL` → tablas de memory card.
  `ESP`/`PAL` confirman build española/europea
- Entrada 4: `BGOK01`, `BGOK02`, `BGTT01` → nombres de pistas de BGM
- Entrada 5: `Last Flight`, `Clear`, `Morceaux` → tabla de textos multiidioma
- Entradas 6-126: 121 registros de tipo `0x11`, ~600-765 KB cada uno → el
  volumen real del pack; geometría y texturas de misión
- Varios registros contienen **punteros a RAM ya resueltos** (`0x800CBE04`,
  `0x800CBE14`, `0x800CBE48`) → revelan el mapa de memoria en ejecución

Reproducir: `make inventory`

## 2026-10-15 — ACE.BIN está comprimido, con cabecera propietaria

Magic `PS-Y4a\xe3w`, no `PS-X EXE`. Campos descifrados por coherencia cruzada:
`0x0C` = tamaño exacto del archivo (413.772), `0x10` = tamaño descomprimido
(700.416), `0x24` = carga (`0x80010000`), `0x08` = fin (`0x800BB000`, que es
carga + descomprimido). Entrada en `0x8002521C`.

Comprimido: entropía 7,42 bits/byte y solo 0,01 prólogos `addiu sp,sp,-N` por
KiB, frente a 0,47 en `SCES_020.66` y 0,42 en `ACE.EXE`.

Reproducir: `make info`

## 2026-10-15 — El juego usa PsyQ de stock

IDs RCS presentes en los binarios: `sys.c,v 1.140 1998/01/12 … noda Exp yos`,
`bios.c,v 1.86`, `intr.c,v 1.75`. Más cadenas `ResetGraph:jtb=%08x,env=%08x`,
`Can't open TIM [%08x]`, `SPU:T/O [%s]`.

Consecuencia: las funciones de `libapi`/`libgpu`/`libgte`/`libspu`/`libcd` se
**reimplementan**, no se decompilan.

Reproducir: `make info`

## 2026-10-15 — No hay símbolos de depuración en el disco

El disco tiene **9 archivos** y ninguno es `.SYM` ni `.MAP`. Es la diferencia
principal frente a REDRIVER2, que partió con los símbolos del original.

Reproducir: `make files`

## 2026-10-15 — Error corregido: heurístico de prólogos MIPS

La primera versión de `mips_prologues()` buscaba los bytes `27 bd ff`, que es
el orden **equivocado**: MIPS en PS1 es little endian, así que la instrucción
`addiu sp, sp, -N` (`0x27BDFFxx`) aparece en el archivo como `xx FF BD 27`.
El bug daba cero en todos los binarios y llevaba a concluir falsamente que
`SCES_020.66` estaba empaquetado. Corregido a buscar `FF BD 27`.

Lección: todo heurístico debe validarse contra un caso **positivo conocido**
antes de usarse para concluir nada.
