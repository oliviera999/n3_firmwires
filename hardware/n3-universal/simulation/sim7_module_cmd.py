"""Sim 7 — rev 0.3 : commande directe d'un module relais ACTIF A L'ETAT HAUT (type KY-019)
par un GPIO 3,3 V a travers Rs, rappel 10k vers la masse, forcage JP direct (+3V3 / GND).

Hypotheses sur le module (NON mesurees, a controler sur un exemplaire) :
  S -> Rbm (1 k nominal, marquage CMS « 102 ») -> base d'un NPN type S8050 (J3Y), emetteur GND,
  collecteur -> bobine SRD-05 (70 ohm) -> +5 V ; LED + 1 k en parallele de la bobine ; diode de roue libre.
  hFE prudent 85 (classe la plus faible de la fiche S8050 a 100 mA) ; 300 = piece la plus sensible.
Le NPN est simule avec le modele BC337 de models.lib dont on regle Bf (etalonne : hFE a 70 mA = 0,875 x Bf).
Coin defavorable : hFE 65 (85 a 25 degC, -23 % a froid : le modele n'a pas de derive de gain en temperature).
"""
import json, itertools, numpy as np
from spicelib import run, meas, OUT

NET = r"""* Module relais actif haut commande par GPIO -> Rs
.param BF_BC337=119 VTO_4905=-3 VCC=5 RS=470 RPD=10k RBM=1k RBE=1e12 RCOIL=70 LCOIL=0.1 V33=3.3 VG=3.3 RGPIO=40 RPU=1e12 RFON=1e12 RFOFF=1e12 TEMPC=27 TPULSE=0
.option temp={TEMPC}
.include models.lib
.option savecurrents
Vcc vcc 0 {VCC}
V33 v33 0 {V33}
Vg gsrc 0 PULSE({VG} {V33} 1m 1u 1u {TPULSE} 1)
Rg gsrc gpio {RGPIO}
Rpu v33 gpio {RPU}
Rs gpio line {RS}
Rpd line 0 {RPD}
Rfon v33 line {RFON}
Rfoff line 0 {RFOFF}
Rbm line b {RBM}
Rbe b 0 {RBE}
Q1 sw b 0 BC337-40
D1 sw vcc 1N4007
Rc vcc n1 {RCOIL}
Lc n1 sw {LCOIL}
Rl vcc nl 1k
Dl nl sw LEDRED
.control
op
let ib = @rbm[i]
let icoil = @rc[i]
let vcoil = v(vcc)-v(sw)
let igpio = -i(Vg)
let i33 = -i(V33)
print v(line) v(gpio) v(sw) ib icoil vcoil igpio i33
@@TRAN@@
quit
.endc
.end
"""
KEYS = ("v(line)", "v(gpio)", "v(sw)", "ib", "icoil", "vcoil", "igpio", "i33")
def op(name="s7", tran="", **p):
    out, _ = run(NET.replace("@@TRAN@@", tran), name, {k: str(v) for k, v in p.items()})
    r = {k: meas(out, k) for k in KEYS}
    r["_out"] = out
    return r
def clean(r): return {k: (round(v, 5) if isinstance(v, float) else v) for k, v in r.items() if k != "_out"}
BF = lambda hfe: round(hfe / 0.875, 1)   # etalonne : hFE(70 mA, Vce 1 V) = 0,875 x Bf sur ce modele
res = {}

# 1. grille : Rs x Rbm, coin defavorable (hFE 85, GPIO 3,0 V, Vcc 5,25 V = bobine la plus gourmande, -10 degC)
grid = []
for rs, rbm, rbe in itertools.product((0.001, 220, 470, 1000), ("1k", "2.2k", "4.7k", "10k"), ("1e12", "10k")):
    for corner, p in (("nominal", dict(BF_BC337=BF(160), V33=3.3, VG=3.3, VCC=5.0)),
                      ("defavorable", dict(BF_BC337=BF(65), V33=3.0, VG=3.0, VCC=5.25, TEMPC=-10, RCOIL=63))):
        r = op(RS=rs, RBM=rbm, RBE=rbe, **p)
        vcc = p["VCC"]
        grid.append(dict(rs=round(rs), rbm=rbm, rbe=("aucune" if rbe == "1e12" else rbe), coin=corner,
                         ib_mA=round(r["ib"] * 1e3, 3), icoil_mA=round(r["icoil"] * 1e3, 1), vce=round(r["v(sw)"], 3),
                         vcoil=round(r["vcoil"], 3), ratio_vcoil=round(r["vcoil"] / 5.0, 3), colle=bool(r["vcoil"] >= 3.75),
                         igpio_mA=round(r["igpio"] * 1e3, 2)))
res["grille"] = grid

# 2. Monte-Carlo par valeur de Rs (module Rbm 1k +-5 %)
rng = np.random.default_rng(7)
for rs in (470, 1000):
    mc = []
    for i in range(150):
        p = dict(RS=f"{rs * rng.uniform(.95, 1.05):.0f}", RPD=f"{1e4 * rng.uniform(.95, 1.05):.0f}",
                 RBM=f"{1e3 * rng.uniform(.95, 1.05):.0f}", BF_BC337=BF(rng.uniform(65, 400)),
                 RCOIL=f"{70 * rng.uniform(.9, 1.1):.1f}", VCC=f"{rng.uniform(4.5, 5.25):.3f}",
                 TEMPC=f"{rng.uniform(-10, 60):.1f}")
        v = rng.uniform(3.0, 3.3); p["V33"] = p["VG"] = f"{v:.2f}"
        r = op(**p); mc.append((r["v(sw)"], r["vcoil"], r["ib"], r["igpio"], float(p["VCC"])))
    a = np.array(mc)
    res[f"mc_rs{rs}"] = dict(n=len(a), vce_max=float(a[:, 0].max()), vce_med=float(np.median(a[:, 0])),
                             vcoil_min=float(a[:, 1].min()), ratio_min=float((a[:, 1] / a[:, 4]).min()),
                             ib_min_mA=float(a[:, 2].min() * 1e3), igpio_max_mA=float(a[:, 3].max() * 1e3),
                             n_sous_3V75=int((a[:, 1] < 3.75).sum()))

# 3. forcages contraires au GPIO (Rbm 1k, hFE 160)
forc = {}
for rs in (220, 470, 1000):
    on = op(RS=rs, RFON="1m", VG=0, BF_BC337=BF(160))       # JP 1-2 ON force, GPIO bas : le GPIO absorbe
    off = op(RS=rs, RFOFF="1m", BF_BC337=BF(160))            # JP 2-3 OFF force, GPIO haut : le GPIO debite
    onhz = op(RS=rs, RFON="1m", RGPIO="1e12", BF_BC337=BF(160))  # ON force, GPIO flottant (reset)
    forc[rs] = dict(on_gpio_bas=dict(igpio_mA=round(on["igpio"] * 1e3, 2), vline=round(on["v(line)"], 3), vcoil=round(on["vcoil"], 3),
                                     i33_mA=round(on["i33"] * 1e3, 2), p_rs_mW=round(on["igpio"] ** 2 * rs * 1e3, 1)),
                    off_gpio_haut=dict(igpio_mA=round(off["igpio"] * 1e3, 2), vline=round(off["v(line)"], 4), icoil_mA=round(off["icoil"] * 1e3, 3)),
                    on_gpio_flottant=dict(vgpio=round(onhz["v(gpio)"], 3), vcoil=round(onhz["vcoil"], 3)))
res["forcage"] = forc

# 4. demarrage : GPIO flottant, puis tirage interne vers +3V3 (45k typ, 30k bas de fourchette), module le plus sensible (hFE 300)
boot = {}
for rs in (470, 1000):
    b = {}
    r = op(RS=rs, RGPIO="1e12", BF_BC337=BF(400), TEMPC=60); b["flottant"] = dict(vline=r["v(line)"], icoil_mA=r["icoil"] * 1e3)
    for rpu in ("45k", "30k", "10k"):
        r = op(RS=rs, RGPIO="1e12", RPU=rpu, BF_BC337=BF(400), TEMPC=60)
        b[f"pullup_{rpu}"] = dict(vline=round(r["v(line)"], 3), ib_uA=round(r["ib"] * 1e6, 2), icoil_mA=round(r["icoil"] * 1e3, 2), vcoil=round(r["vcoil"], 3))
    # sans rappel 10k (ce que fait ffp3 aujourd'hui) avec tirage interne 45k : pour memoire
    r = op(RS=rs, RGPIO="1e12", RPU="45k", RPD="1e12", BF_BC337=BF(400), TEMPC=60)
    b["pullup_45k_SANS_rappel"] = dict(vline=round(r["v(line)"], 3), icoil_mA=round(r["icoil"] * 1e3, 2), vcoil=round(r["vcoil"], 3))
    boot[rs] = b
res["demarrage"] = boot

# 5. seuil de la ligne : tension sur le bornier qui fait passer 7 mA (10 % du courant nominal = relachement garanti) puis 3,75 V bobine
thr = {}
for hfe in (65, 400):
    pts = []
    for vg in np.arange(0.3, 3.31, 0.02):
        r = op(RS=0.001, RGPIO=0.001, RPD="1e12", VG=f"{vg:.2f}", V33=f"{vg:.2f}", BF_BC337=BF(hfe), TEMPC=60)
        pts.append((vg, r["icoil"], r["vcoil"]))
    a = np.array(pts)
    thr[hfe] = dict(v_ligne_pour_7mA=float(a[a[:, 1] >= 0.007][0, 0]), v_ligne_pour_3V75=float(a[a[:, 2] >= 3.75][0, 0]) if (a[:, 2] >= 3.75).any() else None)
res["seuil_ligne_60C"] = thr

# 6. parasite de mise sous tension du S3 : GPIO18 haut pendant 60 us (fiche ESP32-S3, table des glitches), module le plus sensible
TR = "tran 1u 6m\nmeas tran icoil_pk MAX i(Lc)\nmeas tran vsw_min MIN v(sw)"
gl = {}
for rs, L in itertools.product((470, 1000), (0.05, 0.1)):
    out, _ = run(NET.replace("@@TRAN@@", TR), "s7_glitch", dict(RS=str(rs), VG="0", TPULSE="60u", BF_BC337=str(BF(400)), LCOIL=str(L)))
    gl[f"rs{rs}_L{int(L*1e3)}mH"] = dict(icoil_pk_mA=round(meas(out, "icoil_pk") * 1e3, 2))
res["glitch_60us"] = gl

# 7. GPIO45 (S3, strapping VDD_SPI, tirage interne ~45k vers la masse) : niveau vu par la broche au reset
g45 = {}
for rs in (470, 1000):
    auto = op(RS=rs, RGPIO="1e12", BF_BC337=BF(160))
    onf = op(RS=rs, RGPIO="45k", VG=0, RFON="1m", BF_BC337=BF(160))   # RGPIO=45k vers 0 V = tirage interne bas
    g45[rs] = dict(auto_vgpio=round(auto["v(gpio)"], 4), on_force_vgpio=round(onf["v(gpio)"], 3))
res["gpio45_reset"] = g45

(OUT / "s7_results.json").write_text(json.dumps(res, indent=1, ensure_ascii=False))
for k, v in res.items():
    if k != "grille": print(k, json.dumps(v, ensure_ascii=False))
print("\nGRILLE (coin defavorable, sans R base-emetteur) : Rs / Rbm -> Ib mA, Vce, Vbobine, colle")
for g in grid:
    if g["coin"] == "defavorable" and g["rbe"] == "aucune":
        print(f"  Rs {g['rs']:>4}  Rbm {g['rbm']:>4}  Ib {g['ib_mA']:.2f} mA  Vce {g['vce']:.2f} V  Vbob {g['vcoil']:.2f} V ({g['ratio_vcoil']*100:.0f} %)  {'OK' if g['colle'] else 'NE COLLE PAS'}")
print("avec R base-emetteur 10k :")
for g in grid:
    if g["coin"] == "defavorable" and g["rbe"] == "10k":
        print(f"  Rs {g['rs']:>4}  Rbm {g['rbm']:>4}  Ib {g['ib_mA']:.2f} mA  Vce {g['vce']:.2f} V  Vbob {g['vcoil']:.2f} V  {'OK' if g['colle'] else 'NE COLLE PAS'}")
