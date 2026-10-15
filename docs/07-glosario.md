# 07 — Glosario

Para que cualquier agente o colaborador nuevo pueda leer el resto de la
documentación sin contexto previo de PlayStation 1.

| Término | Qué es |
|---|---|
| **R3000A** | La CPU de la PS1. MIPS I de 32 bits, **little endian**, a 33,8 MHz. Sin FPU: todo el cálculo es entero y de punto fijo |
| **GTE** (COP2) | Geometry Transformation Engine. Coprocesador de matemática 3D: matrices, proyección, producto vectorial, interpolación de color. Aritmética de punto fijo **con saturación**. Reimplementarlo mal provoca derivas sutiles en vuelo y cámara |
| **GPU** | El rasterizador de la PS1. No tiene shaders ni Z-buffer: dibuja listas de primitivas ordenadas por el programa (ordering table). Sin corrección de perspectiva en texturas, de ahí la deformación característica |
| **MDEC** | Decodificador de macrobloques por hardware. Descomprime vídeo (los `.STR`) y fotos fijas |
| **SPU** | Sound Processing Unit. 24 voces de ADPCM, reverb por hardware |
| **XA / ADPCM** | Audio comprimido leído en streaming directamente del CD, sin pasar por la CPU |
| **PsyQ** | El SDK oficial de Sony para PS1. Librerías `libapi` (sistema), `libgpu`, `libgte`, `libspu`, `libcd`. **AC3 lo usa de stock**, lo que permite reimplementar esas funciones en lugar de decompilarlas |
| **PS-EXE** | Formato de ejecutable de PS1. Cabecera de 2048 bytes con magic `PS-X EXE`, dirección de carga, tamaño y punto de entrada |
| **Overlay** | Módulo de código que se carga sobre una dirección fija de RAM, reemplazando al anterior. La PS1 solo tiene 2 MB, así que los juegos grandes intercambian overlays. AC3 carga `ACE.EXE` y `ACE.BIN` ambos en `0x80010000` |
| **TIM** | Formato de textura de PS1. Magic `0x00000010`. Puede llevar paleta (CLUT) de 4 u 8 bits, o color directo de 16 bits |
| **VAG / VB / VH** | Formatos de sonido de PS1: muestra ADPCM individual, banco de muestras y su cabecera |
| **CLUT** | Color Look-Up Table. La paleta de una textura indexada |
| **LBA** | Logical Block Address. Número de sector dentro del disco. Un sector de datos útiles son 2048 bytes |
| **MODE2/2352** | Pista de CD cruda: 2352 bytes por sector, de los que 2048 son datos de usuario a partir del offset 24. `psxiso.py` lo detecta solo |
| **`.SYM`** | Archivo de símbolos de depuración del toolchain de Sony. Da nombres de función y variable. **El disco de AC3 no trae ninguno**, y esa es una de las razones por las que el proyecto es duro |
| **Decompilación** | Reescribir a mano el código a C hasta que compile a bytes idénticos al original ("matching"). Lento, pero produce código legible y modificable |
| **Recompilación estática** | Traducir automáticamente cada instrucción MIPS a C/C++ equivalente. Rápido de arrancar, pero el resultado es ilegible y depende de un runtime que emule el hardware |
| **Psy-X** | La capa de REDRIVER2 que reimplementa la API de PsyQ sobre hardware moderno. El modelo a seguir para la capa de compatibilidad de este proyecto |
| **NDK** | Android Native Development Kit. El toolchain de clang que compila C/C++ para ARM en Android |
| **ABI** | Application Binary Interface. En Android: `arm64-v8a` (lo que usa el Flip 2), `armeabi-v7a` (32 bits, legado) |
