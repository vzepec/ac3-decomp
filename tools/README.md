# tools — herramientas de análisis

Sin dependencias: **Python 3 estándar**. Todas funcionan como librería y como
CLI. Los atajos están en el `Makefile` de la raíz.

| Herramienta | Qué hace |
|---|---|
| `psxiso.py` | Lee imágenes de disco PSX. Detecta solo el tamaño de sector y el modo (`MODE1/2352`, `MODE2/2352`, 2048 crudo) y parsea el ISO9660 |
| `acepack.py` | Índices y contenedores `ACE.SPH`/`ACE.SPB` y `ACE.BPH`/`ACE.BPB`. Lista, inventaría y extrae |
| `psexe.py` | Cabeceras PS-EXE y la variante propietaria de Namco de `ACE.BIN`. Mide entropía y densidad de prólogos MIPS para detectar empaquetado |
| `ulz.py` | Descompresor ULZ de Namco (`Ulz\x1a`). `--check` valida la coherencia de flujos en todas las secciones |
| `acerec.py` | Registros de `ACE.BPB`: tabla de secciones, volcado descomprimido, resumen por tipo |
| `tim2png.py` | Escanea TIM estándar (validadas por `bnum`) y las exporta a PNG sin dependencias |
| `acemodel.py` | Modelos del tipo 17: lista vértices, exporta `.obj` (nube de puntos), validación global |
| `acedat.py` | Demuestra que `ACE.DAT` es relleno |

## Uso directo

```bash
python3 tools/psxiso.py  <imagen.bin> list
python3 tools/psxiso.py  <imagen.bin> extract <dir>
python3 tools/acepack.py <imagen.bin> index
python3 tools/acepack.py <imagen.bin> inventory
python3 tools/acepack.py <imagen.bin> extract BPB <dir>
python3 tools/psexe.py   <imagen.bin>
python3 tools/acepack.py <imagen.bin> tags
python3 tools/acerec.py  sections data/bpb/0006_*.bin
python3 tools/acerec.py  dump data/bpb/0006_*.bin data/sec
python3 tools/ulz.py     --check data/bpb
python3 tools/tim2png.py scan data/sec/0006_*_s14.bin
python3 tools/tim2png.py png  data/sec/0006_*_s14.bin data/png
python3 tools/acemodel.py list  data/sec/0006_*_s04.bin
python3 tools/acemodel.py verts data/sec/0006_*_s04.bin 0
python3 tools/acedat.py  <imagen EU>
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
