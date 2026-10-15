# 00 — Contexto del proyecto

## Objetivo

Port **nativo** de *Ace Combat 3: Electrosphere* (PlayStation 1, Namco, 1999)
a Android, entregado como APK instalable y ejecutable, con soporte de
controles físicos. Nativo significa **sin emulador debajo**: el código del
juego traducido o reescrito a C/C++ que se compila para ARM.

## Entorno de desarrollo

- PC Linux. Toolchain de referencia: Android NDK (clang), Gradle/AGP.
- Herramientas de análisis de este repositorio: Python 3 estándar.
- Pendiente de instalar: **Ghidra** (no está en la máquina) con el loader de
  PSX y soporte de COP2/GTE.

## Hardware destino

**Retroid Pocket Flip 2**:

- SoC: Snapdragon 865 o Dimensity 1100 (dos variantes del producto)
- 8 GB de RAM
- Pantalla AMOLED 5,5" 1080p
- Sticks analógicos y gatillos con sensores Hall effect
- Android

La PS1 original tenía un R3000A a 33 MHz y 2 MB de RAM. El destino sobra por
órdenes de magnitud; el rendimiento **no** es un riesgo del proyecto.

## Restricciones del destino Android

| Tema | Detalle |
|---|---|
| ABI | `arm64-v8a` como principal. `armeabi-v7a` opcional |
| Gráficos | GLES3 o Vulkan |
| Páginas de 16 KB | Android reciente exige segmentos ELF alineados a 16 KB. Compilar con los flags del NDK r27+ o la app no carga |
| Mando | Android `InputDevice` o SDL gamepad. Los sticks del Flip 2 cubren los ejes analógicos que el juego espera |
| Datos del juego | **No pueden ir dentro del APK.** El usuario aporta su propio disco y la app lo lee o extrae en el primer arranque |

## Por qué el objetivo es duro

La parte de Android es la fácil y está resuelta en el papel. Lo difícil es
llegar desde ~1,1 MB de código MIPS **sin símbolos de depuración** hasta algo
portable. Los dos precedentes comparables de PS1:

- **REDRIVER2** (Driver 2 reescrito a C nativo): desde noviembre de 2020,
  con commits aún en septiembre de 2026. Unos 6 años. Y partió **con los
  archivos `.SYM` de símbolos del original**.
- **sotn-decomp** (Castlevania SOTN): desde diciembre de 2021, ~5 años, con
  una base amplia de colaboradores, y aún incompleto.

Ace Combat 3 parte **peor** que ambos: no hay símbolos en el disco, el módulo
principal está comprimido, y los 390 MB de assets usan contenedores
propietarios.

Ver `05-hoja-de-ruta.md` para el plan que sí es realizable.
