# 08 — Continuar el proyecto desde una terminal

Guía para retomar el trabajo en una sesión nueva de consola, con o sin un
agente de IA.

## 1. Arranque en frío (30 segundos)

```bash
cd ~/Escritorio/Decomp
make brief
```

`make brief` imprime el estado del proyecto, comprueba que las herramientas
corren y que los discos están donde deben. Si eso sale bien, el entorno está
listo: **no hace falta instalar nada** para la etapa actual (solo Python 3).

Luego:

```bash
cat STATUS.md
```

## 2. Si trabajas con un agente de IA en la consola

Los archivos `CLAUDE.md` y `AGENTS.md` se cargan solos en la mayoría de
agentes. Si el tuyo no los carga, arranca con esto:

```
Lee CLAUDE.md, STATUS.md y docs/04-formatos.md antes de hacer nada.
Luego dime en que etapa esta el proyecto y cual es la siguiente tarea,
sin empezar a trabajar todavia.
```

Y para ponerlo a trabajar en la tarea actual:

```
Sigue la "Siguiente tarea concreta" de STATUS.md.
Reglas: no versiones nada que salga del disco; no afirmes nada que no
puedas reproducir con una herramienta de tools/; valida cualquier
heuristico contra un caso positivo conocido antes de concluir con el.
Cuando acabes, actualiza notes/bitacora.md, el documento de docs/ que
corresponda y STATUS.md.
```

## 3. Comprobación de salud del entorno

```bash
make check        # las tres herramientas importan bien
make files        # tabla de archivos de los dos discos
make info         # cabeceras PS-EXE y deteccion de empaquetado
make index        # cabeceras de los indices de assets
make records      # tipos de registro y tablas de seccion
make inventory    # inventario de los 199 recursos
```

Valores que deben salir, y que sirven de regresión. Si alguno cambia, algo
se rompió:

| Comprobación | Valor esperado |
|---|---|
| `ACE.SPH` europeo | `count=72`, `20+8*72 = 596` = tamaño del archivo |
| `ACE.BPH` europeo | `count=127`, `20+8*127 = 1036` |
| `ACE.SPH` americano | `count=88`, `20+8*88 = 724` |
| Cobertura de los packs | 100 % (316,3 MiB y 33,0 MiB) |
| `SCES_020.66` | PS-EXE, 0,47 prólogos MIPS/KiB, **en claro** |
| `ACE.BIN` | cabecera `PS-Y`, 0,01 prólogos/KiB, **empaquetado** |
| Registros de `ACE.BPB` | tipo 1 ×18, 4 ×22, 5 ×40, 13 ×1, 17 ×41 |

## 4. Poner el repositorio bajo git

Todavía no es un repositorio. **Ejecuta esto tú en tu terminal**, no desde un
agente remoto (git necesita borrar sus propios archivos de bloqueo y un
agente con los permisos restringidos puede dejar el `.git` en mal estado):

```bash
cd ~/Escritorio/Decomp
git init
git add .
git status            # COMPROBAR antes de commitear
```

Antes del primer commit, verifica que no se cuela nada del disco:

```bash
git status --porcelain | grep -iE '\.(bin|cue|iso|spb|bpb|sph|bph|exe)$|SCES_|SLUS_|SLPS_'
```

**Si eso imprime algo, para y arregla el `.gitignore`.** Si no imprime nada:

```bash
git commit -m "Base del proyecto: herramientas de analisis y documentacion

Formato de los contenedores de assets resuelto y verificado (100% de
cobertura de ACE.SPB y ACE.BPB). Cabecera de registro resuelta en 122 de
127 registros. ACE.BIN confirmado como modulo comprimido."
```

## 5. Dónde van las cosas nuevas

| Qué produces | Dónde va |
|---|---|
| Una herramienta o parser | `tools/`, con docstring en español explicando el formato |
| Un hallazgo | `notes/bitacora.md`, entrada nueva **arriba**, con cómo reproducirlo |
| Estructura de un formato | el documento de `docs/` que corresponda |
| Cambio de estado o de siguiente tarea | `STATUS.md` |
| Datos extraídos del disco | `data/` (ignorado por git) |
| Scratch, pruebas, basura | fuera del repositorio, o `_to_delete/` |

## 6. Trabajo pendiente de instalar

| Herramienta | Para qué | Cuándo hace falta |
|---|---|---|
| **Ghidra** + loader de PSX + soporte COP2/GTE | Análisis estático del código | Etapa 3. **No está instalado** |
| Emulador con depurador (PCSX-Redux, DuckStation, no$psx) | Volcar la RAM para desempaquetar `ACE.BIN` | Etapa 2 |
| Android NDK r27+ | Compilar el APK | Etapa 4 |

Nada de eso hace falta para la etapa 1, que es la actual.

## 7. Si te pierdes

El orden de lectura que da contexto completo en ~15 minutos:

1. `CLAUDE.md` — qué es esto y las reglas
2. `STATUS.md` — dónde estamos
3. `docs/00-contexto.md` — objetivo y por qué es duro
4. `docs/05-hoja-de-ruta.md` — el plan
5. `docs/04-formatos.md` — el trabajo técnico ya hecho
6. `notes/bitacora.md` — cómo se llegó hasta aquí, incluidos los errores
