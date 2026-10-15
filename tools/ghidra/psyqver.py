"""psyqver — que version de PsyQ encaja mejor con un binario.

Busca en el binario las firmas de OBJ de cada version de la extension
ghidra_psx_ldr (data/psyq/<version>/*.json: bytes con comodines ??) y cuenta
cuantas aparecen. La version con mas coincidencias es la candidata; sirve
para validar la que elige automaticamente el loader de Ghidra.

Uso:
    python3 tools/ghidra/psyqver.py <binario_plano> <dir_data_psyq>
"""
import json
import os
import re
import sys


def sig_regex(sig):
    parts = [b"." if t == "??" else re.escape(bytes([int(t, 16)])) for t in sig.split()]
    return re.compile(b"".join(parts), re.DOTALL)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    data = open(argv[1], "rb").read()
    results = []
    for ver in sorted((v for v in os.listdir(argv[2]) if v.isdigit()), key=int):
        vdir = os.path.join(argv[2], ver)
        found = total = 0
        for fn in os.listdir(vdir):
            if not fn.endswith(".json"):
                continue
            for obj in json.load(open(os.path.join(vdir, fn))):
                sig = obj.get("sig", "")
                if len(sig.split()) < 16:
                    continue  # firmas muy cortas: demasiados falsos positivos
                total += 1
                if any(m.start() % 4 == 0 for m in sig_regex(sig).finditer(data)):
                    found += 1
        results.append((found, ver, total))
        print(f"PsyQ {ver:>5}: {found:4d} OBJ encontrados de {total}")
    best = max(results)
    print(f"mejor candidata: {best[1]} ({best[0]} OBJ)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
