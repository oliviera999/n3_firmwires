"""Sim 5 — appel de courant servo sur le rail 5 V, par type de servo et par source.
Reprend la netlist du rail 5 V de sim4 (6 relais collés, Wi-Fi actif, capteurs) et balaie
le courant de blocage : SG90 ~0,65 A, MG90S ~1 A (ou 2 SG90 ensemble), 1,5 A, MG996R ~2,5 A.
Sortie : out/s5_servo_results.json + tableau console (minimum de +5V et du 3V3 du DevKit)."""
import re, json
from spicelib import run, meas, OUT

SRC = (OUT.parent / "sim4_adc_rail.py").read_text(encoding="utf-8")
NET5 = re.search(r'NET5 = r"""(.*?)"""', SRC, re.S).group(1)
SOURCES = (("bloc 5 V correct (5,0 V, 0,25 Ω)", dict()),
           ("bloc faible (4,8 V, 0,5 Ω)", dict(VADAPT=4.8, RS=0.5)),
           ("Hi-Link / buck 5 A (5,1 V, 0,1 Ω)", dict(VADAPT=5.1, RS=0.1)))
SERVOS = ((0.65, "SG90 bloqué"), (1.0, "MG90S bloqué / 2 x SG90"), (1.5, "1,5 A"), (2.5, "MG996R bloqué"))
res = {}
print(f"{'source':<36} {'servo':<26} {'+5V min':>8} {'3V3 min':>8}")
for lab, p in SOURCES:
    for i, name in SERVOS:
        out, _ = run(NET5, "s5_servo", dict(ISERVO=i, **p), timeout=300)
        v5, v33 = meas(out, "v5_min_servo"), meas(out, "v33_min_servo")
        res[f"{lab} | {i} A"] = dict(source=lab, i_servo=i, servo=name, v5_min=v5, v33_min=v33, ok=v33 >= 3.1)
        print(f"{lab:<36} {name:<26} {v5:>8.2f} {v33:>8.2f} {'OK' if v33 >= 3.1 else 'brownout'}")
(OUT / "s5_servo_results.json").write_text(json.dumps(res, indent=1, ensure_ascii=False))
