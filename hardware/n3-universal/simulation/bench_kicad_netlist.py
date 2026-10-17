"""Banc sur la netlist SPICE exportée par KiCad (champs Sim.* de la rev 0.2.1).

Vérifie de bout en bout — schéma KiCad -> netlist -> ngspice — que le rail
capteurs +3V3_SW se coupe bien (le défaut Q7 de la rev 0.2 ne peut plus revenir
sans faire échouer ce banc).

Usage :
  kicad-cli sch export netlist --format spice -o out/n3-universal-kicad.spice kicad/n3-universal.kicad_sch
  python3 simulation/bench_kicad_netlist.py [out/n3-universal-kicad.spice]       (code 1 si défaut)

Les connecteurs, relais et modules sont exclus de la simulation (exclude_from_sim) :
le banc ajoute lui-même 5 V sur VIN_RAW (entrée J1, via Q12) et pilote GATE (GPIO13).
JP1 est un connecteur -> absent de la netlist = cavalier ÔTÉ (profil msp/n3pp).
`rshunt` évite les matrices singulières des nets qui ne mènent plus qu'à un bornier.
"""
import re, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
net_path = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "out" / "n3-universal-kicad.spice"
net = net_path.read_text(encoding="utf-8")
net = re.sub(r"^\.end\s*$", "", net, flags=re.M)
# la bibliothèque est référencée en absolu par kicad-cli : on la repointe sur kicad/n3u_sim.lib
net = re.sub(r'^\.include ".*n3u_sim\.lib"', f'.include "{(HERE.parent / "kicad" / "n3u_sim.lib").as_posix()}"', net, flags=re.M)

results = {}
for gate in (0.0, 3.3):
    cir = net + f"""
* --- banc : 5 V sur VIN_RAW (J1), GPIO13 = {gate} V, JP1 ôté
Vin VIN_RAW 0 5
Vgate GATE 0 {gate}
.option gmin=1e-12 rshunt=1e9
.control
op
print v(+5V) v(LDO_IN) v(+3V3_SW) v(GATE_G)
quit
.endc
.end
"""
    tmp = HERE / "out" / f"bench_kicad_gate{gate:g}.cir"
    tmp.write_text(cir, encoding="utf-8")
    out = subprocess.run(["ngspice", "-b", str(tmp)], capture_output=True, text=True).stdout
    vals = {k: float(v) for k, v in re.findall(r"^(v\([^)]+\)) = ([-+0-9.e]+)", out, re.M)}
    if not vals:
        print(out[-1500:]); sys.exit("ngspice : pas de point de fonctionnement")
    results[gate] = vals
    print(f"GPIO13 = {gate:>3} V : +5V = {vals['v(+5v)']:.3f} V  LDO_IN = {vals['v(ldo_in)']:.4f} V  "
          f"+3V3_SW = {vals['v(+3v3_sw)']:.3f} V  GATE_G = {vals['v(gate_g)']:.3f} V")

ok = results[0.0]["v(+3v3_sw)"] < 0.1 and results[3.3]["v(+3v3_sw)"] > 3.2 and results[3.3]["v(ldo_in)"] > 4.9
print("OK : rail capteurs coupé GPIO13 bas, 3,3 V GPIO13 haut" if ok else
      "DEFAUT : le rail capteurs ne se coupe pas (orientation de Q7 ?)")
sys.exit(0 if ok else 1)
