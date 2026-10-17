"""Sim 2 — rail capteurs +3V3_SW : GPIO13 -> R34 1k -> Q8 BC337 (pull-down R48 10k)
-> grille Q7 IRF4905 (pull-up R35 100k vers +5V) -> U1 LD1117V33 (C11 10u, C12 100u).
Compare le câblage rev 0.2 (D=+5V, S=LDO_IN) à la variante corrigée (S=+5V, D=LDO_IN)."""
import json, numpy as np, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from spicelib import run, style, meas, C, STATUS, OUT, INK, INK2

NET = r"""* Rail capteurs n3-universal rev 0.2 — JP1 ÔTÉ (profil msp/n3pp, rail commuté)
.param BF_BC337=560 VTO_4905=-3 VCC=5 ILOAD=0.03 IBURST=0.15 WIRING=0 TEMPC=27
.option temp={TEMPC}
.option savecurrents method=gear gmin=1e-10 reltol=2e-3
.include models.lib
Vcc vcc 0 {VCC}
* GPIO13 : haut de 2 ms à 12 ms (rail ON), bas ailleurs (rail OFF)
Vgpio gsrc 0 PULSE(0 3.3 2m 20n 20n 10m 30m)
Rgpio gsrc gate 50
R34 gate gate_b 1k
R48 gate_b 0 10k
Q8 gate_g gate_b 0 BC337-40
R35 vcc gate_g 100k
* Q7 : WIRING=0 -> tel que câblé (drain=+5V, source=LDO_IN) ; WIRING=1 -> corrigé
.if (WIRING == 0)
M7 vcc gate_g ldo_in IRF4905
.else
M7 ldo_in gate_g vcc IRF4905
.endif
C11 ldo_in c11e 10u
Resr11 c11e 0 0.5
XU1 ldo_in v33sw 0 LD1117V33
C12 v33sw c12e 100u
Resr12 c12e 0 0.2
* charge capteurs permanente + rafale SD (150 mA, 2 ms) pendant le ON
Rload v33sw 0 {3.3/ILOAD}
Iburst v33sw 0 PULSE(0 {IBURST} 7m 10u 10u 2m 100m)
.control
tran 5u 25m uic
wrdata @@OUT@@/@@NAME@@__tran.txt v(gate) v(gate_g) v(ldo_in) v(v33sw) i(Vcc)
meas tran ldo_in_off FIND v(ldo_in) AT=1.9m
meas tran v33_off    FIND v(v33sw) AT=1.9m
meas tran icc_off    FIND i(Vcc) AT=1.9m
meas tran ldo_in_on  FIND v(ldo_in) AT=6.9m
meas tran v33_on     FIND v(v33sw) AT=6.9m
meas tran vgs_on     FIND v(gate_g) AT=6.9m
meas tran drop_on    PARAM='{VCC}-ldo_in_on'
meas tran v33_burst  MIN v(v33sw) FROM=7m TO=9m
meas tran ldo_in_off2 FIND v(ldo_in) AT=24.9m
meas tran v33_off2   FIND v(v33sw) AT=24.9m
meas tran t_on   WHEN v(v33sw)=3.0 RISE=1
meas tran t_off  WHEN v(v33sw)=0.5 FALL=1
quit
.endc
.end
"""

res = {}
for wiring, tag in ((0, "cablage_rev02"), (1, "corrige")):
    for vto in (-2.0, -3.0, -4.0):
        out, d = run(NET, f"s2_{tag}_vto{abs(vto):.0f}", dict(WIRING=wiring, VTO_4905=vto))
        r = {k: meas(out, k) for k in ("ldo_in_off", "v33_off", "icc_off", "ldo_in_on", "v33_on", "vgs_on", "drop_on",
                                          "v33_burst", "ldo_in_off2", "v33_off2", "t_on", "t_off")}
        res[f"{tag}_vto{abs(vto):.0f}"] = r
        if vto == -3.0:
            res[f"{tag}_tran"] = d["tran"]

fig, axs = plt.subplots(2, 1, figsize=(8, 6), sharex=True, constrained_layout=True)
fig.patch.set_facecolor("#fcfcfb")
for ax, tag, title in ((axs[0], "cablage_rev02", "Q7 tel que câblé en rev 0.2 : D = +5V, S = LDO_IN (JP1 ôté)"),
                       (axs[1], "corrige", "Variante corrigée : S = +5V, D = LDO_IN")):
    tr = res[f"{tag}_tran"]; t = tr[:, 0] * 1e3
    ax.plot(t, tr[:, 1], color=C[3], lw=1.2, label="GPIO13 (GATE)")
    ax.plot(t, tr[:, 3], color=C[0], lw=1.6, label="LDO_IN")
    ax.plot(t, tr[:, 4], color=C[1], lw=1.6, label="+3V3_SW")
    style(ax, title, "ms" if ax is axs[1] else None, "V")
    ax.set_ylim(-0.2, 5.6)
    ax.legend(frameon=False, fontsize=8, loc="center right", ncol=3)
r = res["cablage_rev02_vto3"]
axs[0].annotate(f"rail « coupé » : LDO_IN = {r['ldo_in_off']:.2f} V via la diode interne de Q7\n+3V3_SW reste à {r['v33_off']:.2f} V",
                xy=(16, r["ldo_in_off"]), xytext=(13.5, 1.6), fontsize=8, color=STATUS["critical"],
                arrowprops=dict(arrowstyle="-", color=STATUS["critical"], lw=0.7))
r2 = res["corrige_vto3"]
axs[1].annotate(f"rail coupé : {r2['v33_off']*1000:.0f} mV", xy=(21, 0.02), xytext=(13.5, 1.2), fontsize=8, color=INK2,
                arrowprops=dict(arrowstyle="-", color=INK2, lw=0.7))
fig.savefig(OUT / "s2_gate_wiring.png", dpi=150)
plt.close(fig)

for k in list(res):
    if k.endswith("_tran"):
        del res[k]
(OUT / "s2_results.json").write_text(json.dumps(res, indent=1, ensure_ascii=False))
print(json.dumps(res, indent=1, ensure_ascii=False))
