"""Sim 7 — rev 0.3, decision D7 : D5 = 1N5822 posee partout + cavalier 2 points en parallele.
Chaine source -> cable -> Q12 -> +5V -> D5 (ou cavalier) -> VIN_5V -> AMS1117 du DevKit -> 3V3.

 a) marge statique par profil, au courant de pointe du DevKit (Wi-Fi, 0,45 A) ;
 b) cavalier pose, USB seul ; cavalier ote, USB + alimentation ensemble ;
 c) fermeture de Q7 sur C2 decharge (profils a rail commute) avec la diode ;
 d) ffp5cs sur bloc 5 V : 6 modules relais + Wi-Fi + servo SG90 bloque.

Diode : « max » = 1N5822 de models.lib (0,42 V a 0,45 A, 0,52 V a 3 A = maximum de la fiche) ;
        « typ » = 1N5822T (0,33 V a 0,45 A, courbe typique 25 degC). Regulateur du DevKit : AMS1117
        comportemental (chute 1,1 V + 0,2 ohm x I, soit 1,19 V a 0,45 A : il regule tant que
        VIN_5V >= 4,49 V), dont le courant de sortie est preleve sur l'entree (models.lib rev 0.3).
"""
from __future__ import annotations
import json, re, subprocess, itertools
from pathlib import Path
HERE = Path(__file__).resolve().parent
OUT = HERE / "out"; OUT.mkdir(exist_ok=True)

HEAD = """* Sim 7 : {label}
.param BF_BC337=560 VTO_4905={vto}
.option method=gear gmin=1e-10 reltol=2e-3
.include models.lib
.model SWM SW(Ron=0.15 Roff=1e9 Vt=1.5 Vh=0.1)
"""

def ng(txt: str, name: str) -> str:
    p = OUT / f"{name}.cir"; p.write_text(txt, encoding="ascii")
    r = subprocess.run(["ngspice", "-b", str(p)], capture_output=True, text=True, cwd=HERE)
    return r.stdout + r.stderr
def g(out, k):
    m = re.search(rf"^{re.escape(k)}\s*=\s*([-+\d.eE]+)", out, re.M)
    return float(m.group(1)) if m else None

# ------------------------------------------------------------------ a) statique par profil
STATIC = HEAD + """Vad vad 0 {vsrc}
Bsrc vad vin_raw I = min({ilim}, (V(vad)-V(vin_raw))/{rsrc})
Rleak vin_raw 0 1e7
M12 vin_raw q12g v5 IRF4905
R57 q12g 0 100k
Irail v5 0 {irail}
{d5}
XU2 vin5 v33 0 AMS1117
Iesp v33 0 {iesp}
.control
op
print v(vin_raw) v(v5) v(vin5) v(v33)
quit
.endc
.end
"""
D5 = {"fil (0.2.3)": "Rd5 v5 vin5 0.02", "1N5822 max": "D5 v5 vin5 1N5822", "1N5822 typ": "D5 v5 vin5 1N5822T"}
MOD = 0.074   # un module colle : bobine 71 mA + LED 3 mA
PROFILS = [  # nom, Vsource, R serie (source + cable), limite de courant, charge du rail hors DevKit
    ("msp1 : boost regle 5,0 V", 5.0, 0.10, 1.0, 0.035),
    ("msp1 : boost regle 5,1 V", 5.1, 0.10, 1.0, 0.035),
    ("msp1 : boost regle 5,2 V", 5.2, 0.10, 1.0, 0.035),
    ("n3pp : chargeur 5,0 V, cable 0,25 ohm, 1 module", 5.0, 0.25, 9, 0.040 + MOD),
    ("n3pp : chargeur 5,2 V, cable 0,25 ohm, 1 module", 5.2, 0.25, 9, 0.040 + MOD),
    ("n3pp : chargeur faible 4,8 V, 0,5 ohm, 1 module", 4.8, 0.50, 9, 0.040 + MOD),
    ("ffp5cs : bloc 5,0 V, 0,15 ohm, 6 modules sur le rail", 5.0, 0.15, 9, 0.043 + 6 * MOD),
    ("ffp5cs : bloc 5,0 V, 0,15 ohm, 2 modules sur le rail", 5.0, 0.15, 9, 0.043 + 2 * MOD),
    ("ffp5cs : bloc 5,0 V, 0,15 ohm, modules en direct sur le bloc", 5.0, 0.15, 9, 0.043),
    ("ffp5cs : bloc 5,1 V, 0,15 ohm, 6 modules sur le rail", 5.1, 0.15, 9, 0.043 + 6 * MOD),
    ("ffp5cs : bloc 5,2 V, 0,15 ohm, 6 modules sur le rail", 5.2, 0.15, 9, 0.043 + 6 * MOD),
    ("ffp5cs : bloc 5,0 V, cordon fin 0,30 ohm, 6 modules sur le rail", 5.0, 0.30, 9, 0.043 + 6 * MOD),
    ("ffp5cs 12 V : buck regle 5,1 V, 0,10 ohm, 6 modules sur le rail", 5.1, 0.10, 9, 0.043 + 6 * MOD),
    ("ffp5cs 12 V : buck regle 5,0 V, 0,10 ohm, 6 modules sur le rail", 5.0, 0.10, 9, 0.043 + 6 * MOD),
]
res = {"statique": [], "vsource_min": []}
print(f"{'profil':66}{'D5':13}{'Vth':>4}{'+5V':>6}{'VIN_5V':>8}{'3V3':>6}{'marge':>7}")
for (nom, vs, rs, il, ir), (dn, d5), vto in itertools.product(PROFILS, D5.items(), (-3, -4)):
    out = ng(STATIC.format(label=nom, vto=vto, vsrc=vs, rsrc=rs, ilim=il, irail=ir, d5=d5, iesp=0.45), "s7_static")
    v5, vin5, v33 = g(out, "v(v5)"), g(out, "v(vin5)"), g(out, "v(v33)")
    res["statique"].append(dict(profil=nom, d5=dn, vto=vto, v5=round(v5, 3), vin5=round(vin5, 3), v33=round(v33, 3), marge_regul=round(vin5 - 4.49, 3)))
    print(f"{nom:66}{dn:13}{vto:4}{v5:6.2f}{vin5:8.2f}{v33:6.2f}{vin5 - 4.49:+7.2f}")

# tension de source minimale pour garder 3V3 >= 3,20 V (diode max, Q12 a -4 V = pire cas ; et typ/-3)
for (nom, _, rs, il, ir) in (PROFILS[1], PROFILS[3], PROFILS[6], PROFILS[8], PROFILS[12]):
    row = dict(profil=nom.split(" : ")[0] + f" (R {rs} ohm, rail {ir*1000:.0f} mA)")
    for tag, d5, vto in (("pire", D5["1N5822 max"], -4), ("typique", D5["1N5822 typ"], -3), ("fil_typique", D5["fil (0.2.3)"], -3)):
        lo, hi = 4.0, 6.0
        for _ in range(18):
            mid = (lo + hi) / 2
            v33 = g(ng(STATIC.format(label=nom, vto=vto, vsrc=mid, rsrc=rs, ilim=il, irail=ir, d5=d5, iesp=0.45), "s7_static"), "v(v33)")
            lo, hi = (lo, mid) if v33 >= 3.2 else (mid, hi)
        row[tag] = round(hi, 2)
    res["vsource_min"].append(row); print("Vsource min pour 3V3 >= 3,20 V :", row)

# ------------------------------------------------------------------ b) USB
USB = HEAD + """Vusb vusb 0 {vusb}
Rusb vusb vbus {rusb}
Ddk vbus vin5 1N5819
{alim}
M12 vin_raw q12g v5 IRF4905
R57 q12g 0 100k
Rleak vin_raw 0 1e7
Brail v5 0 I = {irail}*max(0, min(1, V(v5)/3))
Rrail v5 0 10k
D5 v5 vin5 1N5822
Rjp v5 vin5 {rjp}
XU2 vin5 v33 0 AMS1117
Iesp v33 0 {iesp}
.control
op
let iusb = -i(Vusb)
print v(v5) v(vin5) v(v33) iusb {extra}
quit
.endc
.end
"""
res["usb_seul_cavalier_pose"] = []
print("\nUSB seul, cavalier pose (rail = VIN_5V) :")
for vusb, rusb, nmod, iesp in itertools.product((5.0, 4.75), (0.3, 0.6), (0, 1, 2), (0.125, 0.45)):
    out = ng(USB.format(label="usb seul", vto=-3, vusb=vusb, rusb=rusb, alim="", irail=0.040 + nmod * MOD, rjp=0.03, iesp=iesp, extra=""), "s7_usb")
    v5, v33, iusb = g(out, "v(v5)"), g(out, "v(v33)"), g(out, "iusb")
    r = dict(vusb=vusb, r_cable=rusb, modules=nmod, i_devkit=iesp, rail=round(v5, 2), v33_devkit=round(v33, 2), i_usb_mA=round(iusb * 1e3), u1_regule=bool(v5 >= 4.31))
    res["usb_seul_cavalier_pose"].append(r); print(" ", r)
res["usb_et_alim"] = []
print("\nUSB + alimentation ensemble :")
for cav, rjp in (("ote", 1e12), ("pose (INTERDIT)", 0.03)):
    for valim in (5.1, 4.8, 0.0):
        alim = f"Vad vad 0 {valim}\nRs vad vin_raw 0.15" if valim else "Rs vin_raw 0 1e7"
        out = ng(USB.format(label="usb+alim", vto=-3, vusb=5.0, rusb=0.3, alim=alim, irail=0.043 + 6 * MOD, rjp=rjp, iesp=0.2,
                            extra="i(Vad)" if valim else ""), "s7_usb2")
        r = dict(cavalier=cav, alim=valim or "absente", rail=round(g(out, "v(v5)"), 2), vin5=round(g(out, "v(vin5)"), 2),
                 i_usb_mA=round(g(out, "iusb") * 1e3), i_alim_mA=round(-(g(out, "i(vad)") or 0) * 1e3))
        res["usb_et_alim"].append(r); print(" ", r)

# ------------------------------------------------------------------ c) Q7 se ferme sur C2 vide, avec D5
INRUSH = HEAD + """{src}
M12 vin_raw q12g v5 IRF4905
R57 q12g 0 100k
C1 v5 c1e 1000u
Resr1 c1e 0 0.08
{d5}
Chold vin5 che {chold}
Resrh che 0 0.15
XU2 vin5 v33 0 AMS1117
Cdk v33 cdke 22u
Resrdk cdke 0 0.3
Iesp v33 0 0.08
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
meas tran v33_avant FIND v(v33) AT=59m
meas tran t_below3 TRIG v(v33) VAL=3.0 FALL=1 TD=59m TARG v(v33) VAL=3.0 RISE=1 TD=59m
quit
.endc
.end
"""
SRC = {
    "msp1 : boost 5,1 V limite a 1 A": "Vad vad 0 PWL(0 0 1m 5.1)\nBsrc vad vin_raw I = min(1.0, max(0,(V(vad)-V(vin_raw))/0.1))\nCbo vin_raw 0 22u",
    "msp1 : boost 5,0 V limite a 1 A": "Vad vad 0 PWL(0 0 1m 5.0)\nBsrc vad vin_raw I = min(1.0, max(0,(V(vad)-V(vin_raw))/0.1))\nCbo vin_raw 0 22u",
    "n3pp : bloc 5,0 V / 0,25 ohm": "Vad vad 0 PWL(0 0 1m 5)\nRs vad vad2 0.25\nLs vad2 vin_raw 1u",
    "ffp5cs 12 V (option 9.9) : buck 5,1 V / 0,10 ohm limite a 3 A": "Vad vad 0 PWL(0 0 1m 5.1)\nBsrc vad vin_raw I = min(3.0, max(0,(V(vad)-V(vin_raw))/0.1))\nCbo vin_raw 0 100u",
}
res["q7_sur_c2_vide"] = []
print(f"\n{'source':62}{'D5':12}{'C2':>6}{'Chold':>7}{'+5V min':>9}{'3V3 avant':>10}{'3V3 min':>9}{'<3,0 V':>9}")
for (name, src), (dn, d5), c2, ch in itertools.product(SRC.items(), (("fil", "Rd5 v5 vin5 0.02"), ("1N5822", "D5 v5 vin5 1N5822")), ("470u", "100u", "1n"), ("1n", "100u", "220u", "470u")):
    if dn == "fil" and ch != "1n": continue
    worst = None
    for vto in (-2, -3):
        out = ng(INRUSH.format(label=name, vto=vto, src=src, d5=d5, c2=c2, chold=ch), "s7_inrush")
        r = dict(v5=g(out, "v5_min"), v33=g(out, "v33_min"), av=g(out, "v33_avant"), t=(g(out, "t_below3") or 0) * 1e3)
        if worst is None or r["v33"] < worst["v33"]: worst = r
    row = dict(source=name, d5=dn, c2="non pose" if c2 == "1n" else c2, c_vin5="aucun" if ch == "1n" else ch,
               v5_min=round(worst["v5"], 2), v33_avant=round(worst["av"], 2), v33_min=round(worst["v33"], 2), ms_sous_3V0=round(worst["t"], 2))
    res["q7_sur_c2_vide"].append(row)
    print(f"{name:62}{dn:12}{row['c2']:>6}{row['c_vin5']:>7}{row['v5_min']:9.2f}{row['v33_avant']:10.2f}{row['v33_min']:9.2f}{row['ms_sous_3V0']:9.2f}")

# ------------------------------------------------------------------ d) ffp5cs sur bloc 5 V : modules + Wi-Fi + servo
DYN = HEAD + """Vad vad 0 PWL(0 0 0.2m {vsrc})
Rs vad vad2 {rsrc}
Ls vad2 vin_raw 1u
M12 vin_raw q12g v5 IRF4905
R57 q12g 0 100k
Dtvs 0 v5 TVS68
C1 v5 c1e 1000u
Resr1 c1e 0 0.08
* JP1 ferme : LDO_IN = +5V par le cavalier ; C2 et servos derriere
Rjp1 v5 ldo_in 0.03
C2 ldo_in c2e {c2}
Resr2 c2e 0 0.12
.subckt module ctl vcc gnd
S1 sw gnd ctl gnd SWM
D1 sw vcc 1N4007
Rc vcc n1 70
Lc n1 sw 0.1
Rl vcc sw 1.5k
.ends
{modules}
{d5}
Chold vin5 che {chold}
Resrh che 0 0.15
XU2 vin5 v33 0 AMS1117
Cdk v33 cdke 22u
Resrdk cdke 0 0.3
Iesp0 v33 0 0.08
Iesp1 v33 0 PULSE(0 0.35 23m 10u 10u 2m 10m)
Iservo ldo_in 0 PULSE(0 {iservo} 60m 100u 100u 60m 1)
Imisc v5 0 0.043
.control
tran 10u 130m
meas tran v5_min_mod MIN v(v5) FROM=20m TO=59m
meas tran v33_min_mod MIN v(v33) FROM=20m TO=59m
meas tran v5_min_servo MIN v(v5) FROM=60m TO=125m
meas tran v33_min_servo MIN v(v33) FROM=60m TO=125m
quit
.endc
.end
"""
def modules(n, on_rail=True):
    if not on_rail: return ""
    return "\n".join(f"Vg{i} g{i} 0 PULSE(0 3.3 {20 + 5 * i}m 20n 20n 1 2)\nX{i} g{i} v5 0 module" for i in range(1, n + 1))
res["ffp5cs_bloc_dyn"] = []
print(f"\n{'source':34}{'modules':>22}{'servo':>7}{'Chold':>7}{'+5V min':>9}{'3V3 min (modules+WiFi)':>24}{'3V3 min (servo)':>17}")
for (sn, vs, rs), (mn, on_rail), isv, ch in itertools.product(
        (("bloc 5,0 V / 0,15 ohm", 5.0, 0.15), ("bloc 5,1 V / 0,15 ohm", 5.1, 0.15), ("bloc 5,2 V / 0,15 ohm", 5.2, 0.15), ("bloc 5,0 V / 0,30 ohm", 5.0, 0.30), ("buck 5,1 V / 0,10 ohm", 5.1, 0.10)),
        (("6 sur le rail", True), ("en direct sur le bloc", False)), (0.65, 1.3), ("1n", "470u", "cavalier")):
    worst = None
    d5 = "Rd5 v5 vin5 0.03" if ch == "cavalier" else "D5 v5 vin5 1N5822"
    chv = "1n" if ch == "cavalier" else ch
    for vto in (-3, -4):
        out = ng(DYN.format(label=sn, vto=vto, vsrc=vs, rsrc=rs, c2="470u", modules=modules(6, on_rail), d5=d5, chold=chv, iservo=isv), "s7_dyn")
        r = {k: g(out, k) for k in ("v5_min_mod", "v33_min_mod", "v5_min_servo", "v33_min_servo")}
        if worst is None or r["v33_min_servo"] < worst["v33_min_servo"]: worst = r
    row = dict(source=sn, modules=mn, servo_A=isv, c_vin5={"1n": "aucun", "cavalier": "cavalier pose"}.get(ch, ch), **{k: round(v, 2) for k, v in worst.items()})
    res["ffp5cs_bloc_dyn"].append(row)
    print(f"{sn:34}{mn:>22}{isv:7.2f}{row['c_vin5']:>7}{row['v5_min_servo']:9.2f}{row['v33_min_mod']:24.2f}{row['v33_min_servo']:17.2f}")

(OUT / "s7_results.json").write_text(json.dumps(res, indent=1, ensure_ascii=False), encoding="utf-8")
