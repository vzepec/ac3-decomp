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
| 4 | APK libre (`REGION=libre android_game/build.sh`) y auditoría (`tools/apk_audit.py`) | ✅ generado, sin probar en la Retroid |
| 5 | Rendimiento en la Retroid (caché de decodificación, bloques…) si hace falta | pendiente |
| 6 | Release alfa en GitHub con el APK libre | pendiente (decisión del dueño) |

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

- **Rendimiento del intérprete en la Retroid.** En el PC sobra. La CPU de la
  Retroid debería ser parecida por núcleo, pero no está medido.
- **Distribución:** la decisión de publicar es del dueño. `docs/06-legal.md`
  ya considera publicable "un APK que no contenga nada de Namco, que pida al
  usuario su propia copia del juego". La auditoría es la comprobación objetiva
  y hay que repetirla en cada release.
