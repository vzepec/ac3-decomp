"""mipsref — referencias a direcciones y constantes en codigo MIPS de PS1.

En MIPS una direccion o constante de 32 bits se construye con dos
instrucciones: `lui rt, hi` seguida (no siempre justo detras) de
`addiu/ori/lw/sw/... rx, rt, lo`. Esta herramienta busca esos pares.

  - para direcciones usa la parte alta "ajustada" (hi + 1 si lo >= 0x8000),
    porque addiu/lw extienden el signo de lo
  - para constantes con `ori` usa la parte alta tal cual

Si se le pasa el TSV de tools/ghidra/AceStats.java, dice en que funcion cae
cada referencia.

Uso:
    python3 tools/mipsref.py <binario> <base> --str "\\\\ACE.BPB;1" [--tsv funcs.tsv]
    python3 tools/mipsref.py <binario> <base> --addr 0x80024b8c [--tsv ...]
    python3 tools/mipsref.py <binario> <base> --const 0x1a7a6c55 [--tsv ...]

<base> es la direccion de carga del primer byte del binario (0x80010000 para
data/exe/ace_eu.bin).
"""
import bisect
import csv
import struct
import sys

LOAD_STORE_ALU = {0x08, 0x09, 0x0C, 0x0D, 0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2B, 0x32, 0x3A}
WINDOW = 12  # instrucciones a mirar tras el lui


def find_refs(words, base, value, use_ori=False):
    lo = value & 0xFFFF
    hi = value >> 16 if use_ori else ((value + 0x8000) >> 16) & 0xFFFF
    hits = []
    for i, w in enumerate(words):
        if (w & 0xFFE00000) != 0x3C000000 or (w & 0xFFFF) != hi:
            continue
        rt = (w >> 16) & 31
        for j in range(i + 1, min(i + 1 + WINDOW, len(words))):
            v = words[j]
            op = v >> 26
            if op in LOAD_STORE_ALU and ((v >> 21) & 31) == rt and (v & 0xFFFF) == lo:
                if use_ori == (op == 0x0D):
                    hits.append((base + 4 * i, base + 4 * j))
                    break
            # si rt se sobrescribe, se abandona
            if op in (0x09, 0x0F, 0x23) and ((v >> 16) & 31) == rt:
                break
    return hits


def load_funcs(path):
    rows = list(csv.DictReader(open(path), delimiter="\t"))
    funcs = sorted((int(r["entry"], 16), r["name"]) for r in rows)
    return [f[0] for f in funcs], funcs


def main(argv):
    if len(argv) < 5:
        print(__doc__)
        return 2
    data = open(argv[1], "rb").read()
    base = int(argv[2], 0)
    words = struct.unpack(f"<{len(data) // 4}I", data[:len(data) // 4 * 4])
    mode, arg = argv[3], argv[4]
    use_ori = False
    if mode == "--str":
        s = arg.encode("latin1").decode("unicode_escape").encode("latin1")
        off = data.find(s)
        if off < 0:
            print("cadena no encontrada")
            return 1
        value = base + off
        print(f"cadena en {value:#010x}")
    elif mode == "--addr":
        value = int(arg, 0)
    elif mode == "--const":
        value = int(arg, 0)
        use_ori = True
    else:
        print(__doc__)
        return 2
    starts = funcs = None
    if "--tsv" in argv:
        starts, funcs = load_funcs(argv[argv.index("--tsv") + 1])
    hits = find_refs(words, base, value, use_ori)
    if use_ori:
        # una constante tambien puede montarse con lui + addiu (lo con signo)
        hits += [h for h in find_refs(words, base, value, False) if h not in hits]
    print(f"{len(hits)} referencias a {value:#010x}")
    for lui, use in hits:
        where = ""
        if funcs:
            k = bisect.bisect_right(starts, use) - 1
            if k >= 0:
                where = f"  en {funcs[k][1]} ({funcs[k][0]:#010x})"
        print(f"  lui {lui:#010x}  uso {use:#010x}{where}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
