# tools — herramientas de análisis

Sin dependencias: **Python 3 estándar**. Todas funcionan como librería y como
CLI. Los atajos están en el `Makefile` de la raíz.

| Herramienta | Qué hace |
|---|---|
| `psxiso.py` | Lee imágenes de disco PSX. Detecta solo el tamaño de sector y el modo (`MODE1/2352`, `MODE2/2352`, 2048 crudo) y parsea el ISO9660 |
| `acepack.py` | Índices y contenedores `ACE.SPH`/`ACE.SPB` y `ACE.BPH`/`ACE.BPB`. Lista, inventaría y extrae |
| `psexe.py` | Cabeceras PS-EXE y la variante propietaria de Namco de `ACE.BIN`. Mide entropía y densidad de prólogos MIPS para detectar empaquetado |

## Uso directo

```bash
python3 tools/psxiso.py  <imagen.bin> list
python3 tools/psxiso.py  <imagen.bin> extract <dir>
python3 tools/acepack.py <imagen.bin> index
python3 tools/acepack.py <imagen.bin> inventory
python3 tools/acepack.py <imagen.bin> extract BPB <dir>
python3 tools/psexe.py   <imagen.bin>
```

## Como librería

```python
import sys; sys.path.insert(0, 'tools')
from psxiso import Disc
from acepack import Index, load

d = Disc("Rom/.../disco.bin")
print(d.volume_id(), d.sector_size)
idx, pack = load(d, "BPB")
for i, tag, sector, nbytes in idx.extents(pack.size):
    print(i, hex(tag), sector, nbytes)
```

## Convenciones

- Documentación y comentarios **en español**, sin tildes dentro de los `.py`
- Identificadores en inglés
- Todo hallazgo que una herramienta demuestre va también a `notes/bitacora.md`
- Marcar explícitamente como `SIN RESOLVER` lo que sea hipótesis
- **Validar cada heurístico contra un caso positivo conocido** antes de
  concluir nada con él (ver la entrada de la bitácora sobre el bug de
  endianness)
