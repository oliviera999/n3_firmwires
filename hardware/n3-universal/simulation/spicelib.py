"""Mini-harnais ngspice : écrit une netlist, lance ngspice -b, relit les wrdata."""
from __future__ import annotations
import subprocess, re, os, tempfile, numpy as np
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "out"
OUT.mkdir(exist_ok=True)

# Palette (dataviz reference instance, mode clair)
C = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6", "#e34948"]
STATUS = dict(good="#0ca30c", warning="#fab219", serious="#ec835a", critical="#d03b3b")
INK, INK2, GRID, SURF = "#0b0b0b", "#52514e", "#e6e5e1", "#fcfcfb"


def run(netlist: str, name: str, params: dict | None = None, timeout=120) -> tuple[str, dict[str, np.ndarray]]:
    """Exécute la netlist. `params` remplace la ligne .param par défaut.
    Retourne (stdout, {fichier wrdata -> tableau})."""
    params = params or {}
    txt = netlist
    for k, v in params.items():
        # remplace 'K=valeur' dans la ligne .param (première occurrence)
        txt, n = re.subn(rf"(\.param[^\n]*\b{k}=)[^\s]+", rf"\g<1>{v}", txt, count=1)
        if n == 0:
            raise KeyError(f"param {k} absent de la netlist")
    txt = txt.replace(".control\n", ".control\nset wr_vecnames\nset wr_singlescale\n", 1)
    txt = txt.replace("@@OUT@@", "out").replace("@@NAME@@", name)
    cir = OUT / f"{name}.cir"
    cir.write_text(txt, encoding="utf-8")
    r = subprocess.run(["ngspice", "-b", str(cir)], cwd=HERE, capture_output=True, text=True, timeout=timeout)
    out = r.stdout + r.stderr
    data = {}
    for f in OUT.glob(f"{name}__*.txt"):
        arr = np.loadtxt(f, skiprows=1)
        data[f.stem.split("__", 1)[1]] = arr
    return out, data


def mktemp_param_line(**kw):
    return ".param " + " ".join(f"{k}={v}" for k, v in kw.items())


def style(ax, title=None, xlabel=None, ylabel=None):
    ax.set_facecolor(SURF)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_color(GRID)
    ax.tick_params(colors=INK2, labelsize=8)
    ax.grid(True, color=GRID, linewidth=0.6)
    ax.set_axisbelow(True)
    if title:
        ax.set_title(title, color=INK, fontsize=10, loc="left", pad=8)
    if xlabel:
        ax.set_xlabel(xlabel, color=INK2, fontsize=8)
    if ylabel:
        ax.set_ylabel(ylabel, color=INK2, fontsize=8)


def meas(out: str, key: str) -> float | None:
    m = re.search(rf"^{re.escape(key)}\s*=\s*([-+0-9.eE]+)", out, re.M)
    return float(m.group(1)) if m else None
