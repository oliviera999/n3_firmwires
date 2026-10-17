"""Sim 3 — entrées d'alimentation : Q12 (anti-inversion 5 V), chaîne 5 V -> DevKit (D5 + AMS1117),
Q11/D9/D8 (bus 12 V)."""
import json, numpy as np, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from spicelib import run, style, meas, C, STATUS, OUT, INK, INK2

# ---------------------------------------------------------------- 3a/3b : entrée 5 V
NET5 = r"""* Entrée 5 V : VIN_RAW -> Q12 -> +5V -> D5 1N5822 -> VIN_5V -> AMS1117 (DevKit) -> 3V3 ESP
.param BF_BC337=560 VTO_4905=-3 VIN=5.0 ILOAD5=0.5 IESP=0.25 TEMPC=27
.option temp={TEMPC}
.option savecurrents method=gear gmin=1e-10
.include models.lib
Vin vin_raw 0 {VIN}
M12 vin_raw q12g v5 IRF4905
R57 q12g 0 100k
Dtvs 0 v5 TVS68
C1 v5 c1e 1000u
Resr1 c1e 0 0.08
Iload5 v5 0 {ILOAD5}
D5 v5 vin_5v 1N5822
XU2 vin_5v v33 0 AMS1117
C_dk v33 c_dke 22u
Resr_dk c_dke 0 0.3
Iesp v33 0 {IESP}
.control
op
let drop12 = v(vin_raw)-v(v5)
print drop12 v(v5) v(vin_5v) v(v33)
dc Iload5 0 4 0.05
let drop = v(vin_raw)-v(v5)
wrdata @@OUT@@/@@NAME@@__sweep.txt drop v(v5) v(v33)
quit
.endc
.end
"""

res = {}
# 3a : chute Q12 vs courant, selon Vgs(th)
fig, axs = plt.subplots(1, 2, figsize=(9.5, 3.8), constrained_layout=True)
fig.patch.set_facecolor("#fcfcfb")
for i, vto in enumerate((-2.0, -3.0, -4.0)):
    out, d = run(NET5, f"s3_q12_vto{abs(vto):.0f}", dict(VTO_4905=vto))
    sw = d["sweep"]
    axs[0].plot(sw[:, 0], sw[:, 1] * 1000, color=C[i], lw=1.6, label=f"Vgs(th) = {vto:.0f} V")
    axs[1].plot(sw[:, 0], sw[:, 1] * sw[:, 0], color=C[i], lw=1.6, label=f"Vgs(th) = {vto:.0f} V")
    i1 = np.argmin(abs(sw[:, 0] - 1.0)); i25 = np.argmin(abs(sw[:, 0] - 2.5))
    res[f"q12_vto{abs(vto):.0f}"] = dict(drop_1A_mV=float(sw[i1, 1] * 1000), drop_2p5A_mV=float(sw[i25, 1] * 1000),
                                       p_2p5A_W=float(sw[i25, 1] * 2.5))
axs[0].axhline(50, color=INK2, ls=":", lw=1); axs[0].text(0.05, 55, "« ≈ 50 mV sous 1 A » (README)", fontsize=7.5, color=INK2)
style(axs[0], "Chute sur Q12 (IRF4905, Vgs = −5 V) vs courant du rail 5 V", "A", "mV")
axs[0].legend(frameon=False, fontsize=8)
style(axs[1], "Dissipation de Q12 (TO-220 sans dissipateur ≈ 62 °C/W)", "A", "W")
axs[1].legend(frameon=False, fontsize=8)
fig.savefig(OUT / "s3_q12_drop.png", dpi=150)
plt.close(fig)

# 3b : marge de dropout de la chaîne Q12 -> D5 -> AMS1117 selon la tension d'adaptateur
fig, ax = plt.subplots(figsize=(8, 3.8), constrained_layout=True)
fig.patch.set_facecolor("#fcfcfb")
vins = np.arange(4.4, 5.56, 0.05)
chain = {}
for j, (vto, iesp, iload) in enumerate(((-3.0, 0.25, 0.3), (-3.0, 0.45, 0.6), (-4.0, 0.45, 0.6))):
    v33 = []
    for vin in vins:
        out, _ = run(NET5, "s3_chain", dict(VTO_4905=vto, VIN=f"{vin:.2f}", IESP=iesp, ILOAD5=iload))
        v33.append(meas(out, "v(v33)"))
    chain[(vto, iesp, iload)] = v33
    ax.plot(vins, v33, color=C[j], lw=1.6, label=f"Vgs(th) {vto:.0f} V, ESP {iesp*1000:.0f} mA, autres charges 5 V {iload*1000:.0f} mA")
    res[f"chain_vto{abs(vto):.0f}_esp{int(iesp*1000)}"] = dict(
        vin_min_for_3p2V=float(next((v for v, x in zip(vins, v33) if x >= 3.2), np.nan)),
        v33_at_4p75=float(v33[np.argmin(abs(vins - 4.75))]), v33_at_5p0=float(v33[np.argmin(abs(vins - 5.0))]))
ax.axhline(3.2, color=STATUS["warning"], ls="--", lw=1); ax.text(4.41, 3.21, "3,2 V : seuil brownout ESP32 (≈ 2,8–3,0 V) + marge", fontsize=7.5, color=INK2)
ax.axvspan(4.75, 5.25, color="#e6e5e1", alpha=0.6, lw=0); ax.text(4.76, 2.45, "USB / adaptateurs « 5 V ±5 % »", fontsize=7.5, color=INK2)
style(ax, "3V3 du DevKit (AMS1117) en fonction de la tension d'entrée sur J1/J2 — chaîne Q12 → D5 → AMS1117", "V sur J1/J2", "V")
ax.set_ylim(2.4, 3.4); ax.legend(frameon=False, fontsize=7.5, loc="lower right")
fig.savefig(OUT / "s3_chain_dropout.png", dpi=150)
plt.close(fig)

# 3c : inversion de polarité sur J1/J2
NETREV = r"""* Inversion de polarité −5 V sur VIN_RAW
.param BF_BC337=560 VTO_4905=-3
.option savecurrents method=gear gmin=1e-10
.include models.lib
Vin vin_raw 0 PWL(0 0 1m -5 20m -5 21m 5 40m 5)
M12 vin_raw q12g v5 IRF4905
R57 q12g 0 100k
Dtvs 0 v5 TVS68
C1 v5 c1e 1000u
Resr1 c1e 0 0.08
Rload v5 0 10
.control
tran 10u 40m
wrdata @@OUT@@/@@NAME@@__tran.txt v(v5) i(Vin)
meas tran v5_min MIN v(v5) FROM=1m TO=20m
meas tran v5_ok  FIND v(v5) AT=39m
quit
.endc
.end
"""
out, d = run(NETREV, "s3_reverse")
trr = d["tran"]; m = (trr[:, 0] > 2e-3) & (trr[:, 0] < 20e-3)
res["reverse_polarity"] = dict(v5_min=meas(out, "v5_min"), i_reverse_A=float(abs(trr[m, 2]).max()), v5_after=meas(out, "v5_ok"))

# ---------------------------------------------------------------- 3d : bus 12 V, Q11 + D9 + D8
NET12 = r"""* Bus 12 V : J26 -> Q11 IRF4905 (G via R40 100k, zener D9 15 V G-S) -> VBAT12_PROT (D8 TVS 18 V, C5 470u) -> buck
.param BF_BC337=560 VTO_4905=-3 VBAT=12.8 ILOAD=3
.option savecurrents method=gear gmin=1e-10
.include models.lib
Vbat vbat12_in 0 {VBAT}
M11 vbat12_in qp_g vbat12_prot IRF4905
R40 qp_g 0 100k
D9 qp_g vbat12_prot Z15
D8 0 vbat12_prot TVS18
C5 vbat12_prot c5e 470u
Resr5 c5e 0 0.1
Iload vbat12_prot 0 {ILOAD}
.control
op
let vgs = v(qp_g)-v(vbat12_prot)
let drop = v(vbat12_in)-v(vbat12_prot)
let iz = -@d9[id]
print vgs drop iz
dc Vbat 8 16 0.25
let vgs = v(qp_g)-v(vbat12_prot)
let drop = v(vbat12_in)-v(vbat12_prot)
let iz = -@d9[id]
wrdata @@OUT@@/@@NAME@@__sweep.txt vgs drop iz
quit
.endc
.end
"""
fig, axs = plt.subplots(1, 2, figsize=(9.5, 3.6), constrained_layout=True)
fig.patch.set_facecolor("#fcfcfb")
for i, vto in enumerate((-2.0, -3.0, -4.0)):
    out, d = run(NET12, f"s3_q11_vto{abs(vto):.0f}", dict(VTO_4905=vto))
    sw = d["sweep"]
    axs[0].plot(sw[:, 0], sw[:, 1], color=C[i], lw=1.6, label=f"Vgs(th) = {vto:.0f} V")
    axs[1].plot(sw[:, 0], sw[:, 2] * 1000, color=C[i], lw=1.6, label=f"Vgs(th) = {vto:.0f} V")
    k = np.argmin(abs(sw[:, 0] - 15.5)); k2 = np.argmin(abs(sw[:, 0] - 10.5))
    res[f"q11_vto{abs(vto):.0f}"] = dict(vgs_at_15p5=float(sw[k, 1]), drop_mV_at_10p5_3A=float(sw[k2, 2] * 1000),
                                       drop_mV_at_15p5_3A=float(sw[k, 2] * 1000), iz_uA_at_15p5=float(sw[k, 3] * 1e6))
axs[0].axhline(-15, color=STATUS["warning"], ls="--", lw=1); axs[0].text(8.1, -14.7, "clamp D9 (1N4744A 15 V)", fontsize=7.5, color=INK2)
axs[0].axhline(-20, color=STATUS["critical"], ls="--", lw=1); axs[0].text(8.1, -19.7, "Vgs max IRF4905 ±20 V", fontsize=7.5, color=INK2)
style(axs[0], "Vgs de Q11 selon la tension du bus 12 V", "VBAT (V)", "V"); axs[0].set_ylim(-21, 0); axs[0].legend(frameon=False, fontsize=8)
style(axs[1], "Chute sur Q11 à 3 A (buck 5 V/3 A en entrée)", "VBAT (V)", "mV"); axs[1].legend(frameon=False, fontsize=8)
fig.savefig(OUT / "s3_q11_bus12.png", dpi=150)
plt.close(fig)

# surtension 30 V / 2 ms (load dump) et inversion −12 V
NET12T = r"""* Bus 12 V : transitoire 30 V 2 ms puis inversion −12 V
.param BF_BC337=560 VTO_4905=-3
.option savecurrents method=gear gmin=1e-10
.include models.lib
Vbat vsrc 0 PWL(0 12.8 5m 12.8 5.05m 30 7m 30 7.05m 12.8 12m 12.8 12.1m -12.8 20m -12.8)
Rsrc vsrc vbat12_in 0.3
M11 vbat12_in qp_g vbat12_prot IRF4905
R40 qp_g 0 100k
D9 qp_g vbat12_prot Z15
D8 0 vbat12_prot TVS18
C5 vbat12_prot c5e 470u
Resr5 c5e 0 0.1
Rload vbat12_prot 0 4.3
.control
tran 5u 20m
let itvs = @d8[id]
wrdata @@OUT@@/@@NAME@@__tran.txt v(vbat12_in) v(vbat12_prot) v(qp_g) i(Vbat) itvs
meas tran vprot_max MAX v(vbat12_prot) FROM=5m TO=7m
meas tran vprot_rev FIND v(vbat12_prot) AT=19.9m
quit
.endc
.end
"""
out, d = run(NET12T, "s3_bus12_transient")
tr = d["tran"]; t = tr[:, 0] * 1e3
ms = (tr[:, 0] >= 5e-3) & (tr[:, 0] <= 7e-3); mr = tr[:, 0] >= 13e-3
res["bus12_transient"] = dict(vprot_max=meas(out, "vprot_max"), vgs_min=float((tr[ms, 3] - tr[ms, 2]).min()),
    i_tvs_max_A=float(abs(tr[ms, 5]).max()), p_tvs_avg_W=float(np.mean(abs(tr[ms, 5]) * tr[ms, 2])),
    i_rev_max_A=float(abs(tr[mr, 4]).max()), vprot_rev=meas(out, "vprot_rev"))
fig, ax = plt.subplots(figsize=(8, 3.6), constrained_layout=True)
fig.patch.set_facecolor("#fcfcfb")
ax.plot(t, tr[:, 1], color=C[3], lw=1.3, label="VBAT12_IN (source, 0,3 Ω)")
ax.plot(t, tr[:, 2], color=C[0], lw=1.6, label="VBAT12_PROT")
ax.plot(t, tr[:, 3], color=C[1], lw=1.3, label="grille Q11")
style(ax, "Bus 12 V : surtension 30 V pendant 2 ms, puis inversion de polarité −12,8 V", "ms", "V")
ax.legend(frameon=False, fontsize=8, loc="lower left", ncol=3)
fig.savefig(OUT / "s3_bus12_transient.png", dpi=150)
plt.close(fig)

(OUT / "s3_results.json").write_text(json.dumps(res, indent=1, ensure_ascii=False))
print(json.dumps(res, indent=1, ensure_ascii=False))
