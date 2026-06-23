#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Analyse rapide d'un firmware.map (GNU ld) pour la DRAM interne ESP32.

Extrait les plus gros contributeurs de .dram0.bss et .dram0.data afin de
cibler les optimisations memoire statique.

Usage:
  python tools/analyze_dram_map.py <chemin firmware.map>
"""
from __future__ import annotations

import re
import sys
from collections import defaultdict


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: analyze_dram_map.py <firmware.map>")
        return 1
    path = sys.argv[1]

    # Lignes de symbole GNU ld :
    #  .bss.<sym>     0x3ffbxxxx   0xSIZE   chemin/objet.o
    # ou symbole + chemin sur deux lignes.
    sym_re = re.compile(r"^\s+(\.(?:bss|data|dram0\.\w+)\S*)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(.*)$")
    cont_re = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(.*)$")

    by_symbol = []
    by_object = defaultdict(int)
    by_section = defaultdict(int)

    in_dram = False
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            # Reperer l'entree dans les sections DRAM
            stripped = line.strip()
            if stripped.startswith(".dram0.bss") or stripped.startswith(".dram0.data"):
                in_dram = True
            elif stripped.startswith(".flash") or stripped.startswith(".iram") or stripped.startswith(".rtc"):
                in_dram = False

            m = sym_re.match(line)
            if m:
                sect, addr, size, obj = m.groups()
                size_i = int(size, 16)
                addr_i = int(addr, 16)
                # DRAM data interne : 0x3ffb0000 - 0x40000000
                if 0x3FF80000 <= addr_i < 0x40000000 and size_i > 0:
                    objname = obj.strip().split("(")[-1].rstrip(")")
                    objshort = objname.split("\\")[-1].split("/")[-1]
                    by_symbol.append((size_i, sect, objshort))
                    by_object[objshort] += size_i
                    if ".bss" in sect:
                        by_section["bss"] += size_i
                    elif ".data" in sect:
                        by_section["data"] += size_i

    print("=== TOP 40 SYMBOLES DRAM (bss/data) ===")
    for size_i, sect, obj in sorted(by_symbol, reverse=True)[:40]:
        print(f"{size_i:7d}  {sect:45s}  {obj}")

    print("\n=== TOP 30 OBJETS (cumul DRAM) ===")
    for obj, total in sorted(by_object.items(), key=lambda x: -x[1])[:30]:
        print(f"{total:7d}  {obj}")

    print("\n=== TOTAUX ===")
    for k, v in by_section.items():
        print(f"{k}: {v} octets")
    print(f"total symboles detectes: {sum(s for s, _, _ in by_symbol)} octets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
