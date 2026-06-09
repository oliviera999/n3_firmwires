"""
pio_check_flash_budget.py — Garde-fou taille binaire (audit optimisation v13.93).

La flash ffp5cs est quasi pleine (WROOM ~96 %, S3 ~99 %) : une petite régression
peut faire déborder la partition applicative au flash, hors CI. Ce post-script
compare la taille de l'image (.bin) à la taille de la partition app et ÉCHOUE le
build si le seuil `custom_flash_budget_pct` (défini par env dans platformio.ini)
est dépassé — pour attraper le débordement à la compilation, pas sur la cible.

Seuil par défaut très haut (99.9 %) si non défini : le script informe sans bloquer.
"""
Import("env")  # noqa: F821  (fourni par PlatformIO/SCons)
import os


def _app_partition_size(env):
    # PlatformIO calcule upload.maximum_size depuis la table de partitions custom.
    try:
        return int(env.BoardConfig().get("upload.maximum_size", 0))
    except Exception:
        return 0


def check_flash_budget(source, target, env):
    bin_path = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    if not os.path.isfile(bin_path):
        return
    used = os.path.getsize(bin_path)
    max_size = _app_partition_size(env)
    if max_size <= 0:
        print("[flash-budget] taille partition inconnue, check ignoré")
        return

    pct = 100.0 * used / max_size
    budget = float(env.GetProjectOption("custom_flash_budget_pct", 99.9))
    print("[flash-budget] %s : %d / %d o = %.1f%% (budget %.1f%%)"
          % (env["PIOENV"], used, max_size, pct, budget))

    if pct > budget:
        print("=" * 70)
        print("ERREUR budget flash : %.1f%% > %.1f%% — la partition app est"
              " trop pleine." % (pct, budget))
        print("Réduire le binaire (assets, code mort) ou relever"
              " custom_flash_budget_pct (au risque d'un débordement réel).")
        print("=" * 70)
        env.Exit(1)


# Le .bin est produit par elf2bin (après checkprogsize) lors d'un `pio run`.
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_flash_budget)
