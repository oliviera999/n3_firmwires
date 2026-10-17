"""Sim 1 — étage de commande relais K1..K6 (GPIO -> 1k -> BC337-40, pull-down 10k,
bobine SRD-05 70 Ohm, roue libre 1N4007, LED témoin 1k, sélecteur de forçage JP)."""
import json, numpy as np, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from spicelib import run, style, meas, C, STATUS, OUT, INK, INK2

NET = r"""* Étage relais n3-universal rev 0.2
.param BF_BC337=560 VTO_4905=-3 VCC=5 RB=1k RP=10k RL=1k RCOIL=70 LCOIL=0.1 RJPOFF=1e12 RFON=1e12 TEMPC=27 VGPIO=3.3
.option temp={TEMPC}
.include models.lib
.option savecurrents
Vcc vcc 0 {VCC}
Vgpio gsrc 0 PULSE(0 {VGPIO} 1m 20n 20n 15m 40m)
Rgpio gsrc gpio 50
Rb gpio base {RB}
Rp base 0 {RP}
* forçage : JP 1-2 (ON) = +3V3 -> 1k -> base ; JP 2-3 (OFF) = base -> GND
V33 v33 0 3.3
Rfon v33 base {RFON}
Rjpoff base 0 {RJPOFF}
Q1 sw base 0 BC337-40
Dfw sw vcc 1N4007
Rcoil vcc ncoil {RCOIL}
Lcoil ncoil sw {LCOIL}
Rled vcc nled {RL}
Dled nled sw LEDRED
.control
set wr_vecnames
set wr_singlescale
tran 2u 40m
let iled = @rled[i]
let ib = @rb[i]
let pq = v(sw)*(i(Lcoil)+iled)
wrdata @@OUT@@/@@NAME@@__tran.txt v(sw) i(Lcoil) i(Vgpio) v(base) iled
meas tran vsw_on  FIND v(sw) AT=14m
meas tran icoil_on FIND i(Lcoil) AT=14m
meas tran ib_on   FIND ib AT=14m
meas tran iled_on FIND iled AT=14m
meas tran igpio_on FIND i(Vgpio) AT=14m
meas tran vsw_pk  MAX v(sw) FROM=16m TO=25m
meas tran t_release WHEN i(Lcoil)=0.0071 FALL=1
meas tran t_on90 WHEN i(Lcoil)=0.064 RISE=1
meas tran pq_on FIND pq AT=14m
quit
.endc
.end
"""

res = {}

# --- 1a. transitoire nominal ---------------------------------------------
out, d = run(NET, "s1_nominal")
res["nominal"] = {k: meas(out, k) for k in ("vsw_on", "icoil_on", "ib_on", "iled_on", "igpio_on", "vsw_pk", "t_release", "t_on90", "pq_on")}
tr = d["tran"]
t = tr[:, 0] * 1e3

fig, axs = plt.subplots(3, 1, figsize=(8, 7.5), sharex=True, constrained_layout=True)
fig.patch.set_facecolor("#fcfcfb")
axs[0].plot(t, tr[:, 1], color=C[0], lw=1.6)
style(axs[0], "Collecteur V(REL_SW) — ON à 1 ms, OFF à 16 ms", None, "V")
axs[0].annotate(f"pic roue libre {res['nominal']['vsw_pk']:.2f} V", xy=(16.1, res['nominal']['vsw_pk']), xytext=(22, 5.95),
                color=INK2, fontsize=8, arrowprops=dict(arrowstyle="-", color=INK2, lw=0.6))
axs[0].annotate(f"Vce(sat) = {res['nominal']['vsw_on']*1000:.0f} mV", xy=(8, 0.1), xytext=(6, 1.2), color=INK2, fontsize=8,
                arrowprops=dict(arrowstyle="-", color=INK2, lw=0.6))
axs[1].plot(t, tr[:, 2] * 1e3, color=C[1], lw=1.6)
axs[1].axhline(53.6, color=STATUS["warning"], lw=1, ls="--")
axs[1].text(0.3, 55.5, "75 % de 71 mA = tension d'enclenchement garantie (3,75 V)", fontsize=7.5, color=INK2)
style(axs[1], "Courant bobine", None, "mA")
axs[2].plot(t, -tr[:, 3] * 1e3, color=C[2], lw=1.6, label="courant GPIO (source)")
axs[2].plot(t, tr[:, 5] * 1e3, color=C[3], lw=1.6, label="courant LED témoin")
style(axs[2], "Courants GPIO et LED", "ms", "mA")
axs[2].legend(frameon=False, fontsize=8, loc="center right")
fig.savefig(OUT / "s1_relay_transient.png", dpi=150)
plt.close(fig)

# --- 1b. sensibilité à l'inductance de bobine (inconnue) -------------------
rows = []
for L in (0.05, 0.1, 0.2):
    out, _ = run(NET, f"s1_L{int(L*1000)}", dict(LCOIL=L))
    rows.append(dict(L_mH=L * 1000, vsw_pk=meas(out, "vsw_pk"), t_release_ms=(meas(out, "t_release") - 16e-3) * 1e3,
                     t_on90_ms=(meas(out, "t_on90") - 1e-3) * 1e3))
res["inductance"] = rows

# --- 1c. Modes forcés ------------------------------------------------------
# ON forcé, GPIO bas (le GPIO absorbe le courant du 1k de forçage)
out, _ = run(NET, "s1_forceON_gpio0", dict(RFON="1k", VGPIO=0))
res["force_on_gpio_low"] = dict(vsw=meas(out, "vsw_on"), icoil=meas(out, "icoil_on"), igpio=meas(out, "igpio_on"))
out, _ = run(NET, "s1_forceON_gpio1", dict(RFON="1k"))
res["force_on_gpio_high"] = dict(vsw=meas(out, "vsw_on"), icoil=meas(out, "icoil_on"), igpio=meas(out, "igpio_on"))
# OFF forcé, GPIO haut : le GPIO débite dans 1k vers GND
out, _ = run(NET, "s1_forceOFF_gpio1", dict(RJPOFF="1m"))
res["force_off_gpio_high"] = dict(vsw=meas(out, "vsw_on"), icoil=meas(out, "icoil_on"), igpio=meas(out, "igpio_on"))

# --- 1d. Monte-Carlo : hFE, tolérances, Vcc, température -------------------
rng = np.random.default_rng(42)
N = 120
mc = []
for i in range(N):
    p = dict(
        BF_BC337=round(rng.uniform(250, 630) * 560 / 400, 1),  # modèle Bf=560 <-> hFE~400 typ ; échelle sur la plage datasheet
        RB=f"{rng.uniform(0.95, 1.05) * 1000:.0f}",
        RP=f"{rng.uniform(0.95, 1.05) * 10000:.0f}",
        RCOIL=f"{rng.uniform(0.9, 1.1) * 70:.1f}",
        VCC=f"{rng.uniform(4.5, 5.25):.3f}",
        TEMPC=f"{rng.uniform(-10, 60):.1f}",
        VGPIO=f"{rng.uniform(3.0, 3.3):.2f}",
    )
    out, _ = run(NET, f"s1_mc{i:03d}", p)
    mc.append(dict(**{k: float(v) for k, v in p.items()}, vsw=meas(out, "vsw_on"), icoil=meas(out, "icoil_on"),
                   vcoil=float(p["VCC"]) - meas(out, "vsw_on"), ib=meas(out, "ib_on"), pq=meas(out, "pq_on")))
res["mc"] = dict(n=N, vsw_max=max(m["vsw"] for m in mc), vsw_min=min(m["vsw"] for m in mc),
                 vcoil_min=min(m["vcoil"] for m in mc), icoil_min=min(m["icoil"] for m in mc),
                 pq_max=max(m["pq"] for m in mc),
                 n_vsw_over_300mV=sum(m["vsw"] > 0.3 for m in mc),
                 n_vcoil_under_3p75=sum(m["vcoil"] < 3.75 for m in mc))

fig, axs = plt.subplots(1, 2, figsize=(9, 3.6), constrained_layout=True)
fig.patch.set_facecolor("#fcfcfb")
hfe = [m["BF_BC337"] * 400 / 560 for m in mc]
axs[0].scatter(hfe, [m["vsw"] * 1000 for m in mc], s=14, color=C[0], alpha=0.8)
axs[0].axhline(300, color=STATUS["warning"], ls="--", lw=1)
axs[0].text(255, 310, "seuil 300 mV", fontsize=7.5, color=INK2)
style(axs[0], f"Monte-Carlo n={N} : Vce(sat) vs hFE", "hFE (250–630)", "Vce(sat) mV")
axs[1].scatter([m["VCC"] for m in mc], [m["vcoil"] for m in mc], s=14, color=C[1], alpha=0.8)
axs[1].axhline(3.75, color=STATUS["critical"], ls="--", lw=1)
axs[1].text(4.52, 3.8, "3,75 V = must-operate SRD-05", fontsize=7.5, color=INK2)
style(axs[1], "Tension réellement appliquée à la bobine vs Vcc", "Vcc (V)", "V")
fig.savefig(OUT / "s1_relay_montecarlo.png", dpi=150)
plt.close(fig)

(OUT / "s1_results.json").write_text(json.dumps(res, indent=1, ensure_ascii=False))
print(json.dumps(res, indent=1, ensure_ascii=False))
