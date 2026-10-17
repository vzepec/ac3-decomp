# AGENTS.md — contrato de trabajo para agentes de IA

> **Lee este archivo completo antes de tocar nada.** Es el contrato completo
> del proyecto. Luego lee `STATUS.md` (estado actual) y `docs/` (contexto).
>
> Para un arranque rápido hay un bootstrap más corto en `CLAUDE.md`, que la
> mayoría de agentes cargan automáticamente. Para retomar el trabajo desde
> una terminal, `docs/08-continuar-por-consola.md`.

## 1. Qué es este proyecto

Ingeniería inversa de **Ace Combat 3: Electrosphere** (PlayStation 1, Namco,
1999) con el objetivo a largo plazo de un **port nativo a Android** (APK
instalable, con controles físicos, destino de referencia: Retroid Pocket
Flip 2). Se desarrolla desde un **PC Linux**.

**El objetivo declarado no es alcanzable a corto plazo y el proyecto lo
asume explícitamente.** Lee `docs/05-hoja-de-ruta.md` para entender por qué
y cuál es el plan real por etapas. No prometas un juego jugable.

## 2. Reglas duras

1. **Nunca metas ROMs, imágenes de disco ni assets extraídos en git.**
   `Rom/` y `data/` están en `.gitignore`. El repositorio solo contiene
   código y documentación escritos por nosotros. Esto es lo que hace el
   proyecto publicable (ver `docs/06-legal.md`).
2. **No inventes hallazgos.** Todo lo que se afirme en `docs/` debe estar
   verificado contra los bytes del disco y ser reproducible con una
   herramienta de `tools/`. Marca las hipótesis como `SIN RESOLVER`.
3. **Las herramientas de `tools/` son la fuente de verdad**, no los
   documentos. Si un documento y una herramienta discrepan, ejecuta la
   herramienta y corrige el documento.
4. **No confíes en los repos de "AC3 recomp" que aparecen en buscadores.**
   Ver `docs/03-versiones.md` §4. Son humo.
5. **Trabaja sobre la versión europea** (`SCES_020.66`) salvo instrucción
   contraria. Está en español, que es lo que el dueño del proyecto quiere.
6. **No borres archivos del disco del usuario.** Si algo sobra, muévelo a
   `_to_delete/` y avisa.

## 3. Cómo orientarse en 2 minutos

```bash
make brief             # estado del proyecto + comprobacion de entorno
make help              # comandos disponibles
make index             # cabeceras de los indices de assets (rapido)
make records           # tipos de registro y tablas de seccion
make inventory         # inventario completo de los 199 recursos
make info              # cabeceras PS-EXE de los binarios del juego
make extract-bpb       # extrae ACE.BPB a data/bpb/ (necesario para lo siguiente)
make ulz-check         # valida el descompresor ULZ
make models            # valida el formato de vertices
make textures REC=0006 # TIM de un registro a PNG
```

Las herramientas no necesitan dependencias: **Python 3 estándar, nada más**.

## 4. Mapa del repositorio

```
CLAUDE.md            bootstrap de arranque en frio (se carga solo)
AGENTS.md            este archivo: el contrato completo
README.md            presentacion corta
STATUS.md            estado actual y siguiente tarea concreta  <- LEE ESTO
Makefile             atajos
docs/
  00-contexto.md     objetivo, hardware destino, restricciones
  01-disco.md        estructura de los discos (EU y US), medidas
  02-binarios.md     PS-EXE, PsyQ, el blob comprimido ACE.BIN
  03-versiones.md    EU vs US vs JP, y los repos falsos
  04-formatos.md     FORMATO DE LOS PACKS DE ASSETS (resuelto)
  05-hoja-de-ruta.md etapas, criterios de salida, estimaciones
  06-legal.md        que se puede publicar y que no
  07-glosario.md     GTE, MDEC, SPU, overlay, PsyQ, TIM, VAG...
  08-continuar-por-consola.md  retomar el trabajo desde terminal
  09-codigo.md       analisis estatico: Ghidra, modulos, funciones, bucle
tools/
  psxiso.py          lectura de imagenes de disco PSX (libreria + CLI)
  acepack.py         indices y contenedores ACE.SPH/SPB y ACE.BPH/BPB
  psexe.py           cabeceras PS-EXE y la variante no estandar de ACE.BIN
  ulz.py             descompresor ULZ de Namco (secciones de los registros)
  acerec.py          secciones de los registros de ACE.BPB
  tim2png.py         TIM a PNG
  acemodel.py        modelos 3D del tipo 17 (terreno, objetos, aviones LOD)
  acesnd.py          bancos de sonido del SPU (ADPCM a WAV)
  acedat.py          demuestra que ACE.DAT es relleno
  acebin.py          descompresor de ACE.BIN (modulo principal)
  mipsref.py         referencias lui/addiu en codigo MIPS
  ghidra/            scripts de Ghidra y analisis de firmas (etapa 3)
  README.md          que hace cada herramienta y como usarla
notes/
  bitacora.md        registro append-only de hallazgos con fecha
Rom/                 imagenes de disco del usuario (IGNORADO por git)
data/                salida de extraccion (IGNORADO por git)
```

## 5. Cómo registrar trabajo

Cuando descubras algo:

1. Escribe/ajusta la herramienta que lo demuestra en `tools/`.
2. Añade el hallazgo a `notes/bitacora.md` con fecha y cómo reproducirlo.
3. Si es estructural, actualiza el documento correspondiente de `docs/`.
4. Actualiza `STATUS.md`: qué queda hecho y cuál es la siguiente tarea.

Mantén el estilo: documentación y comentarios **en español**, identificadores
de código en inglés, sin tildes dentro de los archivos `.py` (compatibilidad).

## 6. Qué NO hacer

- No construyas más de lo que pide la etapa en curso (ver `STATUS.md`). La
  etapa 6 recompila el juego siguiendo `docs/11-etapa6.md`, paso a paso.
  **Nunca subas a git el C generado por el recompilador** (deriva del código
  de Namco): va a `data/`. Construir el armazón del port
  antes de entender los datos es la forma más rápida de tirar meses.
- `ACE.BIN` está comprimido en el disco: trabaja siempre sobre la salida de
  `make acebin` (`data/exe/`), y nunca la subas a git.
- No añadas dependencias a las herramientas sin una razón fuerte. Deben
  ejecutarse en cualquier Python 3 recién instalado.
