"""acedat — que hay dentro de ACE.DAT (42 MB, solo en el disco europeo).

Resultado (verificado): ACE.DAT es un archivo de RELLENO. De sus 20.672
sectores, todos Mode 2 Form 1 de datos, solo 40 (0,19 %) contienen algun byte
distinto de cero: 108 bytes no nulos cada uno, con solo dos patrones distintos
entre los 40.

Esta al FINAL del disco (LBA 179.433, despues de ACE.SPB y ACE.EXE), asi que
no desplaza a ningun otro archivo. No contiene assets: se puede ignorar para
el analisis de formatos.

SIN RESOLVER: para que existe (rellenar el disco, o algo de mastering/anticopia)
y el significado del patron de los 40 sectores.

Uso:
    python3 tools/acedat.py <imagen.bin>
"""
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from psxiso import Disc  # noqa: E402

SECTOR = 2048


def scan(disc):
    f = disc.find("ACE.DAT")
    if f is None:
        return None
    nsec = (f.size + SECTOR - 1) // SECTOR
    hits = []
    for s in range(nsec):
        data = disc.read(f.lba + s, SECTOR)
        if any(data):
            hits.append((s, sum(1 for b in data if b), hashlib.sha1(data).hexdigest()[:12]))
    return f, nsec, hits


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    res = scan(Disc(argv[1]))
    if res is None:
        print("este disco no tiene ACE.DAT (esperado en la version USA)")
        return 0
    f, nsec, hits = res
    print(f"ACE.DAT: lba={f.lba} size={f.size} sectores={nsec}")
    print(f"sectores con algun byte no nulo: {len(hits)} ({100 * len(hits) / nsec:.2f} %)")
    print(f"patrones distintos entre ellos: {len({h for _, _, h in hits})}")
    print("posiciones (sector relativo, bytes no nulos):", [(s, n) for s, n, _ in hits])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
