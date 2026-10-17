"""libsplit — desglosa por libreria PsyQ las funciones reconocidas por Ghidra.

Lee el TSV de tools/ghidra/AceStats.java y los JSON de firmas de la extension
ghidra_psx_ldr (data/psyq/<version>/*.json), que dicen que OBJ y que simbolos
forman cada LIB.

Uso:
    python3 tools/ghidra/libsplit.py <stats_funcs.tsv> <dir_firmas_version>
"""
import collections
import csv
import json
import os
import sys


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    sym2lib, obj2lib = {}, {}
    for fn in os.listdir(argv[2]):
        if not fn.endswith(".json"):
            continue
        lib = fn[:-5].replace(".LIB", "").replace(".OBJ", "")
        for obj in json.load(open(os.path.join(argv[2], fn))):
            objname = obj["name"].replace(".OBJ", "").replace(".", "_")
            obj2lib[objname.upper()] = lib
            for lab in obj.get("labels", []):
                sym2lib.setdefault(lab["name"], lib)
    rows = list(csv.DictReader(open(argv[1]), delimiter="\t"))
    by_lib = collections.defaultdict(lambda: [0, 0])
    unknown = []
    own = [0, 0]
    for r in rows:
        name, size = r["name"], int(r["size"])
        if name.startswith("FUN_") or name == "main":
            own[0] += 1
            own[1] += size
            continue
        lib = sym2lib.get(name)
        if lib is None and "_OBJ_" in name:
            lib = obj2lib.get(name.split("_OBJ_")[0].upper())
        if lib is None:
            unknown.append(name)
            lib = "?"
        by_lib[lib][0] += 1
        by_lib[lib][1] += size
    total_lib = sum(v[0] for v in by_lib.values())
    print(f"{'libreria':<12} {'funciones':>9} {'bytes':>9}")
    for lib, (n, sz) in sorted(by_lib.items(), key=lambda kv: -kv[1][1]):
        print(f"{lib:<12} {n:>9} {sz:>9}")
    print(f"{'TOTAL PsyQ':<12} {total_lib:>9} {sum(v[1] for v in by_lib.values()):>9}")
    print(f"{'propias':<12} {own[0]:>9} {own[1]:>9}   (FUN_ sin reconocer + main)")
    if unknown:
        print("nombres sin libreria asignada:", unknown[:20])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
