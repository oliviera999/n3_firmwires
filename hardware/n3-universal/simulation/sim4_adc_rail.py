"""Sim 4 — mesures : pont VBAT (R38/JP12/R49/C13/D11), ponts écho HC-SR04 (1k/2k), entrées ADC (10k / 1k / 100n).
Sim 5 — dynamique du rail 5 V : 6 relais + rafales Wi-Fi ESP32 + appel servo, alimentation à impédance non nulle."""
import json, numpy as np, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from spicelib import run, style, meas, C, STATUS, OUT, INK, INK2

res = {}
# ----------------------------------------------------------------- 4a pont VBAT
NETV = r"""* Pont VBAT : J25 -> R38 100k -> VBAT_DIV -> (JP12) R39 22k | R58 100k -> GND ; R49 1k -> ADC_VBAT ; C13 100n ; D11 BAT85 -> +3V3
.param BF_BC337=560 VTO_4905=-3 RBAS=22k VBAT=12.8
.option savecurrents
.include models.lib
V33 v33 0 3.3
Vbat vbat_sense 0 {VBAT}
R38 vbat_sense vbat_div 100k
Rbas vbat_div 0 {RBAS}
R49 vbat_div adc_vbat 1k
C13 adc_vbat 0 100n
D11 adc_vbat v33 BAT85
* entrée ADC ESP32 : ~ 1 pF commuté, fuite négligeable -> 10 Mohm
Radc adc_vbat 0 10Meg
.control
dc Vbat 0 25 0.25
let i38 = @r38[i]
let id11 = @d11[id]
wrdata @@OUT@@/@@NAME@@__sweep.txt v(adc_vbat) i38 id11
quit
.endc
.end
"""
fig, ax = plt.subplots(figsize=(8, 3.6), constrained_layout=True)
fig.patch.set_facecolor("#fcfcfb")
for i, (rb, lab) in enumerate((("22k", "JP12 1-2 : 100k/22k (bus 12 V)"), ("100k", "JP12 2-3 : 100k/100k (1S Li-ion)"))):
    out, d = run(NETV, f"s4_vbat_{rb}", dict(RBAS=rb))
    sw = d["sweep"]
    ax.plot(sw[:, 0], sw[:, 1], color=C[i], lw=1.6, label=lab)
    # pleine échelle ADC (atténuation 11 dB : ~3,1 V utiles ; 2,45 V linéaire recommandé)
    fs31 = float(np.interp(3.1, sw[:, 1], sw[:, 0])) if sw[:, 1].max() >= 3.1 else None
    fs245 = float(np.interp(2.45, sw[:, 1], sw[:, 0]))
    k22 = np.argmin(abs(sw[:, 0] - 22))
    res[f"vbat_{rb}"] = dict(vbat_at_adc_3p1V=fs31, vbat_at_adc_2p45V=fs245,
                            adc_at_22V=float(sw[k22, 1]), i_r38_uA_at_22V=float(sw[k22, 2] * 1e6), i_d11_uA_at_22V=float(sw[k22, 3] * 1e6),
                            adc_at_14p7V=float(np.interp(14.7, sw[:, 0], sw[:, 1])), adc_at_4p2V=float(np.interp(4.2, sw[:, 0], sw[:, 1])))
ax.axhline(3.1, color=STATUS["warning"], ls="--", lw=1); ax.text(0.2, 3.14, "3,1 V : plafond ADC ESP32 (atténuation 11 dB)", fontsize=7.5, color=INK2)
ax.axhline(2.45, color=INK2, ls=":", lw=1); ax.text(0.2, 2.49, "2,45 V : fin de la zone linéaire recommandée (ADC WROOM)", fontsize=7.5, color=INK2)
ax.axvline(22, color=STATUS["critical"], ls="--", lw=1); ax.text(22.2, 0.3, "panneau à vide\nsur J25 (erreur)", fontsize=7.5, color=INK2)
style(ax, "ADC_VBAT en fonction de la tension sur J25 (clamp D11 BAT85 vers +3V3)", "V sur J25", "V sur ADC_VBAT")
ax.legend(frameon=False, fontsize=8, loc="center right")
fig.savefig(OUT / "s4_vbat_divider.png", dpi=150)
plt.close(fig)

# RC du pont : réponse à un échelon (temps d'établissement pour l'ADC)
NETVT = NETV.replace("Vbat vbat_sense 0 {VBAT}", "Vbat vbat_sense 0 PULSE(10 14 1m 1u 1u 50m 100m)").replace(
    "dc Vbat 0 25 0.25\nlet i38 = @r38[i]\nlet id11 = @d11[id]\nwrdata @@OUT@@/@@NAME@@__sweep.txt v(adc_vbat) i38 id11",
    "tran 20u 20m\nmeas tran v0 FIND v(adc_vbat) AT=0.99m\nmeas tran v1 FIND v(adc_vbat) AT=19.9m\nmeas tran t63 WHEN v(adc_vbat)=1.98 RISE=1")
out, _ = run(NETVT, "s4_vbat_step")
res["vbat_step"] = dict(v_10V=meas(out, "v0"), v_14V=meas(out, "v1"), tau_ms=(meas(out, "t63") - 1e-3) * 1e3 if meas(out, "t63") else None)

# ----------------------------------------------------------------- 4b pont écho HC-SR04
NETE = r"""* Écho HC-SR04 5 V -> R 1k série -> GPIO ; R 2k -> GND via JP (fermé). Câble 2 m ~ 200 pF, entrée GPIO ~ 5 pF
.param BF_BC337=560 VTO_4905=-3 VECHO=5.0 CCABLE=200p RJP=1m
.include models.lib
Vecho echo_src 0 PULSE(0 {VECHO} 1u 30n 30n 20u 50u)
Rout_sr04 echo_src echo 100
Ccable echo 0 {CCABLE}
R1 echo gpio 1k
R2 gpio div 2k
Rjp div 0 {RJP}
Cgpio gpio 0 5p
* protection interne GPIO ESP32 (diode vers 3V3 absente sur ESP32 ; on observe seulement le niveau)
.control
tran 10n 60u
wrdata @@OUT@@/@@NAME@@__tran.txt v(echo) v(gpio)
meas tran vhigh FIND v(gpio) AT=15u
meas tran tr10 WHEN v(gpio)=0.33 RISE=1
meas tran tr90 WHEN v(gpio)=2.97 RISE=1
quit
.endc
.end
"""
for tag, p in (("5V", dict()), ("5V25", dict(VECHO=5.25)), ("open", dict(RJP="1e12"))):
    out, _ = run(NETE, f"s4_echo_{tag}", p)
    r = dict(vhigh=meas(out, "vhigh"))
    if meas(out, "tr10") and meas(out, "tr90"):
        r["trise_ns"] = (meas(out, "tr90") - meas(out, "tr10")) * 1e9
    res[f"echo_{tag}"] = r

# ----------------------------------------------------------------- 5 dynamique du rail 5 V
NET5 = r"""* Rail 5 V : adaptateur 5 V (Rs 0,25 ohm, câble 1 uH) -> Q12 -> +5V (C1 1000u + C2 470u) ; charges : 6 relais, ESP32 (via D5/AMS1117), servo
.param BF_BC337=560 VTO_4905=-3 VADAPT=5.0 RS=0.25 LS=1u NREL=6 ISERVO=2.5
.option savecurrents method=gear gmin=1e-10
.include models.lib
Vad vad 0 {VADAPT}
Rs vad vad2 {RS}
Ls vad2 vin_raw {LS}
M12 vin_raw q12g v5 IRF4905
R57 q12g 0 100k
Dtvs 0 v5 TVS68
C1 v5 c1e 1000u
Resr1 c1e 0 0.08
C2 v5 c2e 470u
Resr2 c2e 0 0.12
* 6 relais enclenchés successivement toutes les 5 ms à partir de 5 ms (bobines 70 ohm + 100 mH, LED 1k)
.param RC=70 LC=0.1
Vg1 g1 0 PULSE(0 3.3 5m  20n 20n 1 2)
Vg2 g2 0 PULSE(0 3.3 10m 20n 20n 1 2)
Vg3 g3 0 PULSE(0 3.3 15m 20n 20n 1 2)
Vg4 g4 0 PULSE(0 3.3 20m 20n 20n 1 2)
Vg5 g5 0 PULSE(0 3.3 25m 20n 20n 1 2)
Vg6 g6 0 PULSE(0 3.3 30m 20n 20n 1 2)
.subckt relay gpio vcc gnd
Rb gpio b 1k
Rp b gnd 10k
Q1 sw b gnd BC337-40
D1 sw vcc 1N4007
Rc vcc n1 70
Lc n1 sw 0.1
Rl vcc nl 1k
Dl nl sw LEDRED
.ends
X1 g1 v5 0 relay
X2 g2 v5 0 relay
X3 g3 v5 0 relay
X4 g4 v5 0 relay
X5 g5 v5 0 relay
X6 g6 v5 0 relay
* ESP32 : 80 mA de fond + rafales Wi-Fi 350 mA (2 ms toutes les 10 ms), derrière D5 + AMS1117
D5 v5 vin_5v 1N5822
XU2 vin_5v v33 0 AMS1117
Cdk v33 cdke 22u
Resrdk cdke 0 0.3
Iesp0 v33 0 0.08
Iesp1 v33 0 PULSE(0 0.35 3m 10u 10u 2m 10m)
* servo : appel 2,5 A pendant 60 ms à partir de 40 ms
Iservo v5 0 PULSE(0 {ISERVO} 40m 100u 100u 60m 1)
* HC-SR04 x3 + LDO capteurs 100 mA
Imisc v5 0 0.145
.control
tran 10u 110m
wrdata @@OUT@@/@@NAME@@__tran.txt v(v5) v(vin_5v) v(v33) i(Vad)
meas tran v5_min_relays MIN v(v5) FROM=4m TO=39m
meas tran v5_min_servo  MIN v(v5) FROM=40m TO=100m
meas tran v33_min_servo MIN v(v33) FROM=40m TO=100m
meas tran v33_min_relays MIN v(v33) FROM=4m TO=39m
meas tran iad_max MAX abs(i(Vad)) FROM=40m TO=100m
quit
.endc
.end
"""
fig, axs = plt.subplots(2, 1, figsize=(8.5, 6), sharex=True, constrained_layout=True)
fig.patch.set_facecolor("#fcfcfb")
for i, (tag, p, lab) in enumerate((("5V_rs025", dict(), "adaptateur 5,0 V, 0,25 Ω"),
                                   ("4V8_rs05", dict(VADAPT=4.8, RS=0.5), "adaptateur 4,8 V, 0,5 Ω (bas de gamme + câble fin)"),
                                   ("5V_vto4", dict(VTO_4905=-4), "5,0 V, 0,25 Ω, Q12 Vgs(th) = −4 V"))):
    out, d = run(NET5, f"s5_{tag}", p, timeout=300)
    tr = d["tran"]; t = tr[:, 0] * 1e3
    axs[0].plot(t, tr[:, 1], color=C[i], lw=1.3, label=lab)
    axs[1].plot(t, tr[:, 3], color=C[i], lw=1.3, label=lab)
    res[f"rail5_{tag}"] = {k: meas(out, k) for k in ("v5_min_relays", "v5_min_servo", "v33_min_servo", "v33_min_relays", "iad_max")}
axs[0].axhline(4.5, color=STATUS["warning"], ls="--", lw=1); axs[0].text(0.5, 4.52, "4,5 V : minimum HC-SR04", fontsize=7.5, color=INK2)
style(axs[0], "Rail +5V : 6 relais enclenchés de 5 à 30 ms, rafales Wi-Fi toutes les 10 ms, servo 2,5 A de 40 à 100 ms", None, "V")
axs[0].legend(frameon=False, fontsize=7.5, loc="lower left", ncol=1)
axs[1].axhline(3.2, color=STATUS["warning"], ls="--", lw=1); axs[1].text(0.5, 3.21, "3,2 V", fontsize=7.5, color=INK2)
style(axs[1], "3V3 du DevKit (derrière D5 1N5822 + AMS1117)", "ms", "V")
axs[1].set_ylim(2.6, 3.4)
fig.savefig(OUT / "s5_rail5_dynamics.png", dpi=150)
plt.close(fig)

(OUT / "s4_results.json").write_text(json.dumps(res, indent=1, ensure_ascii=False))
print(json.dumps(res, indent=1, ensure_ascii=False))
