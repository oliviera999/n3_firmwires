#!/usr/bin/env python3
"""Simulation 6 — fermeture de Q7 sur C2 déchargé (rev 0.2.2+, rail commuté msp/n3pp).

Depuis la rev 0.2.2, C2 (réservoir des servos) est sur LDO_IN, derrière Q7. Sur une unité à
rail commuté (JP1 ôté), Q7 se ferme à chaque réveil sur un C2 vide : C1 (1000 µF, +5V) se
partage avec C2 en moins d'une milliseconde et le +5V s'effondre le temps que la source
recharge l'ensemble. Ce script chiffre le creux sur le +5V et sur le 3V3 du DevKit selon la
source, la dispersion de Vgs(th) de Q7/Q12 et la valeur de C2.

Usage (depuis hardware/n3-universal/simulation) : python3 sim6_q7_inrush.py
Écrit out/s6_q7_inrush.cir (cas nominal msp1), out/s6_sweep.cir (dernier cas du balayage,
ignoré par git) et out/s6_results.json. Nécessite ngspice (testé : ngspice 42).

Limites : mêmes modèles que les autres simulations (models.lib) — AMS1117 et LD1117
comportementaux, sources idéalisées (résistance série ou limite de courant), résistances de
pistes estimées depuis le PCB routé en 1 oz (57 mΩ entre C1 et Q7, 66 mΩ entre Q7 et C2).
À confirmer à l'oscilloscope sur la première carte montée.
"""
from __future__ import annotations
import itertools, json, re, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "out"

NETLIST = """* Sim 6 : fermeture de Q7 sur C2 decharge (rev 0.2.2+) - {label}
.param BF_BC337=560 VTO_4905={vto}
.option method=gear gmin=1e-10 reltol=2e-3
.include {models}
{src}
M12 vin_raw q12g v5 IRF4905
R57 q12g 0 100k
C1 v5 c1e 1000u
Resr1 c1e 0 0.08
* DevKit : D5 = pont de fil, AMS1117, 22 uF, ESP32 eveille sans radio (80 mA)
Rd5 v5 vin5 0.02
XU2 vin5 v33 0 AMS1117
Cdk v33 cdke 22u
Resrdk cdke 0 0.3
Iesp v33 0 0.08
* GPIO13 passe a l'etat haut a 60 ms : Q8 tire la grille de Q7 a la masse
Vgpio gsrc 0 PULSE(0 3.3 60m 20n 20n 1 2)
Rgpio gsrc gate 50
R34 gate gate_b 1k
R48 gate_b 0 10k
Q8 gate_g gate_b 0 BC337-40
R35 v5 gate_g 100k
Rtr1 v5 v5q 0.057
M7 ldoq gate_g v5q IRF4905
Rtr2 ldoq ldo_in 0.066
C2 ldo_in c2e {c2}
Resr2 c2e 0 0.12
C11 ldoq c11e 10u
Resr11 c11e 0 0.5
XU1 ldoq v33sw 0 LD1117V33
C12 v33sw c12e 100u
Resr12 c12e 0 0.2
Rload v33sw 0 110
.control
tran 2u 80m
meas tran v5_min MIN v(v5) FROM=59m TO=80m
meas tran v33_min MIN v(v33) FROM=59m TO=80m
meas tran t_below3 TRIG v(v33) VAL=3.0 FALL=1 TD=59m TARG v(v33) VAL=3.0 RISE=1 TD=59m
quit
.endc
.end
"""

SOURCES = {
    "msp1 : boost 5 V limite a 1 A": "Vad vad 0 PWL(0 0 1m 5)\nBsrc vad vin_raw I = min(1.0, max(0,(V(vad)-V(vin_raw))/0.1))\nCbo vin_raw 0 22u",
    "bloc 5 V / 0,25 ohm": "Vad vad 0 PWL(0 0 1m 5)\nRs vad vad2 0.25\nLs vad2 vin_raw 1u",
    "bloc faible 4,8 V / 0,5 ohm": "Vad vad 0 PWL(0 0 1m 4.8)\nRs vad vad2 0.5\nLs vad2 vin_raw 1u",
    "Hi-Link 5,1 V / 0,05 ohm": "Vad vad 0 PWL(0 0 1m 5.1)\nRs vad vad2 0.05\nLs vad2 vin_raw 0.5u",
}
C2S = ["470u", "220u", "100u", "1n"]          # 1n = C2 non pose
VTOS = [-2, -3, -4]


def run(label, src, vto, c2, path):
    path.write_text(NETLIST.format(label=label, src=src, vto=vto, c2=c2, models="models.lib"), encoding="ascii")
    out = subprocess.run(["ngspice", "-b", str(path)], capture_output=True, text=True, cwd=HERE).stdout
    def g(k):
        m = re.search(rf"^{k}\s*=\s*([-\d.e+]+)", out, re.M)
        return float(m.group(1)) if m else None
    return {"v5_min": g("v5_min"), "v33_min": g("v33_min"), "t_below_3V0_ms": (g("t_below3") or 0) * 1e3}


def main():
    OUT.mkdir(exist_ok=True)
    res = []
    print(f"{'source':32}{'Vth':>5}{'C2':>7}{'+5V min':>9}{'3V3 DevKit min':>16}{'< 3,0 V':>10}")
    for (name, src), vto, c2 in itertools.product(SOURCES.items(), VTOS, C2S):
        nominal = name.startswith("msp1") and vto == -3 and c2 == "470u"
        r = run(name, src, vto, c2, OUT / ("s6_q7_inrush.cir" if nominal else "s6_sweep.cir"))
        if r["v33_min"] is None:
            sys.exit(f"ngspice n'a pas rendu de mesure pour {name} {vto} {c2}")
        res.append({"source": name, "vto": vto, "c2": "non pose" if c2 == "1n" else c2, **{k: round(v, 2) for k, v in r.items()}})
        d = f"{r['t_below_3V0_ms']:.2f} ms" if r["t_below_3V0_ms"] else "-"
        print(f"{name:32}{vto:5}{c2:>7}{r['v5_min']:9.2f}{r['v33_min']:16.2f}{d:>10}")
    (OUT / "s6_results.json").write_text(json.dumps(res, indent=1, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    main()
