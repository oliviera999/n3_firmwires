#!/usr/bin/env python3
"""Signe et publie un firmware OTA pour l'ecosysteme n3 (cibles n3_ota).

Reproduit, de facon portable (Linux/CI), la logique du `publish_ota.ps1`
Windows : calcul du sha256, signature ECDSA P-256 sur le digest, ecriture du
`metadata.json` au format attendu par `shared/n3_common/n3_ota`.

Le firmware verifie embarque (`n3_ota.cpp`) :
  - sha256  = hex minuscule des octets du .bin
  - signature = base64 d'une signature ECDSA **DER** sur le digest sha256
    (compatible `openssl dgst -sha256 -sign cle.pem firmware.bin`)

Cibles supportees (meme format {version,url,sha256,signature}) :
  - n3pp / msp            -> metadata.json objet unique
  - cam (uploadphotosserver) -> metadata.json multi-cles (msp1/n3pp/ffp3)

ffp5cs n'est PAS gere ici (schema distinct : channels + md5, HTTPS) ; il
conserve son propre `ffp5cs/scripts/publish_ota.ps1`.

Modes :
  --print-version   Affiche seulement la version (lue dans le manifest) et sort.
  (defaut)          Signe le .bin et ecrit firmware.bin + metadata.json.

Garde-fou (--guard, actif par defaut) : avant publication, recupere le
metadata.json en ligne et ECHOUE si la version a publier n'est pas strictement
superieure a celle deja servie (evite un deploiement no-op silencieusement
rejete par les appareils, cf. compareVersions de n3_ota.cpp).

Usage :
  python tools/ota/publish_ota.py --firmware n3pp --print-version
  python tools/ota/publish_ota.py \
      --firmware n3pp --channel prod \
      --bin n3pp/.pio/build/esp32dev/firmware.bin \
      --key /tmp/ota_signing_key.pem \
      --ota-root <repo_n3_serveur>/serveur/ota
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
MANIFEST = REPO_ROOT / "firmwares.manifest.json"

# Mapping selection -> (firmware_id manifest, sous-dossier OTA, cle metadata).
# Pour cam, la cle metadata distingue les galeries dans un metadata.json unique.
TARGETS = {
    "n3pp":      {"id": "n3pp", "subdir": "n3pp",      "subdir_test": "n3pp-test", "key": None},
    "msp":       {"id": "msp",  "subdir": "msp",       "subdir_test": "msp-test",  "key": None},
    "cam-msp1":  {"id": "uploadphotosserver", "subdir": "cam", "key": "msp1"},
    "cam-n3pp":  {"id": "uploadphotosserver", "subdir": "cam", "key": "n3pp"},
    "cam-ffp3":  {"id": "uploadphotosserver", "subdir": "cam", "key": "ffp3"},
}


def read_version(firmware_id: str) -> str:
    """Lit la version depuis la source declaree dans firmwares.manifest.json."""
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    entry = next((f for f in manifest["firmwares"] if f["id"] == firmware_id), None)
    if entry is None:
        raise SystemExit(f"Firmware '{firmware_id}' absent de {MANIFEST.name}")
    src = entry["versionSource"]
    text = (REPO_ROOT / entry["path"] / src["file"]).read_text(encoding="utf-8")
    if "define" in src:
        m = re.search(rf'#define\s+{re.escape(src["define"])}\s+"([^"]+)"', text)
    else:
        m = re.search(src["pattern"], text)
    if not m:
        raise SystemExit(f"Version introuvable dans {src['file']} pour {firmware_id}")
    return m.group(1)


def compare_versions(a: str, b: str) -> int:
    """Compare maj.min.patch comme n3_ota.cpp (composants manquants = 0)."""
    def parts(v):
        nums = re.findall(r"\d+", v or "")
        nums = (nums + ["0", "0", "0"])[:3]
        return [int(n) for n in nums]
    pa, pb = parts(a), parts(b)
    return (pa > pb) - (pa < pb)


def sha256_hex(path: Path) -> str:
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


def sign_b64(bin_path: Path, key_path: Path) -> str:
    """Signature ECDSA DER sur sha256(bin), encodee base64 (cf. n3_ota.cpp)."""
    der = subprocess.run(
        ["openssl", "dgst", "-sha256", "-sign", str(key_path), str(bin_path)],
        check=True, capture_output=True,
    ).stdout
    return base64.b64encode(der).decode("ascii")


def fetch_remote_version(metadata_url: str, key: str | None) -> str | None:
    """Version actuellement servie en ligne, ou None si absente/injoignable."""
    try:
        with urllib.request.urlopen(metadata_url, timeout=15) as resp:
            doc = json.loads(resp.read().decode("utf-8"))
    except (urllib.error.URLError, json.JSONDecodeError, OSError) as exc:
        print(f"[OTA] Garde-fou: metadata en ligne indisponible ({exc}); pas de comparaison.")
        return None
    entry = doc.get(key) if key else doc
    if not isinstance(entry, dict):
        return None
    return entry.get("version")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--firmware", required=True, choices=sorted(TARGETS), help="Cible OTA")
    p.add_argument("--bin", type=Path, help="Chemin du firmware.bin compile")
    p.add_argument("--key", type=Path, help="Cle privee ECDSA P-256 (PEM)")
    p.add_argument("--ota-root", type=Path, help="Racine OTA dans n3_serveur (ex: serveur/ota)")
    p.add_argument("--channel", choices=["prod", "test"], default="prod", help="Canal (n3pp/msp uniquement)")
    p.add_argument("--base-url", default="http://iot.olution.info/ota", help="Prefixe URL public")
    p.add_argument("--version", help="Force la version (sinon lue depuis le manifest)")
    p.add_argument("--print-version", action="store_true", help="Affiche la version et sort")
    p.add_argument("--guard", action=argparse.BooleanOptionalAction, default=True,
                   help="Echoue si la version n'est pas > a celle deja en ligne (defaut: oui)")
    p.add_argument("--dry-run", action="store_true", help="N'ecrit rien, affiche le metadata")
    args = p.parse_args()

    target = TARGETS[args.firmware]
    version = args.version or read_version(target["id"])

    if args.print_version:
        print(version)
        return 0

    for name, val in (("--bin", args.bin), ("--key", args.key), ("--ota-root", args.ota_root)):
        if val is None:
            raise SystemExit(f"{name} requis hors mode --print-version")
    if not args.bin.is_file():
        raise SystemExit(f"Binaire introuvable : {args.bin}")
    if not args.key.is_file():
        raise SystemExit(f"Cle privee introuvable : {args.key}")

    # Sous-dossier OTA (canal test reserve a n3pp/msp).
    subdir = target["subdir"]
    if args.channel == "test":
        if "subdir_test" not in target:
            raise SystemExit(f"Pas de canal test pour {args.firmware}")
        subdir = target["subdir_test"]

    base_url = args.base_url.rstrip("/")
    metadata_url = f"{base_url}/{subdir}/metadata.json"

    # Garde-fou version : refuse une version <= a celle deja servie.
    if args.guard:
        remote = fetch_remote_version(metadata_url, target["key"])
        if remote is not None:
            if compare_versions(version, remote) <= 0:
                raise SystemExit(
                    f"[OTA] Garde-fou: version a publier {version} <= version en ligne {remote}. "
                    f"Bumper le firmware (skill bump-firmware-version) ou utiliser --no-guard.")
            print(f"[OTA] Garde-fou OK: {version} > {remote} (en ligne).")

    sha = sha256_hex(args.bin)
    sig = sign_b64(args.bin, args.key)

    if target["key"]:  # cam : metadata multi-cles, bin sous cam/<key>/firmware.bin
        bin_rel = f"{subdir}/{target['key']}/firmware.bin"
    else:
        bin_rel = f"{subdir}/firmware.bin"

    entry = {
        "version": version,
        "url": f"{base_url}/{bin_rel}",
        "sha256": sha,
        "signature": sig,
    }

    meta_path = args.ota_root / subdir / "metadata.json"
    bin_dst = args.ota_root / bin_rel

    if target["key"]:
        # Fusion : preserve les autres galeries du metadata.json existant.
        existing = {}
        if meta_path.is_file():
            existing = json.loads(meta_path.read_text(encoding="utf-8"))
        existing[target["key"]] = entry
        meta_obj = existing
    else:
        meta_obj = entry

    meta_json = json.dumps(meta_obj, separators=(",", ":"), ensure_ascii=False)

    print(f"[OTA] firmware={args.firmware} version={version} canal={args.channel}")
    print(f"[OTA] sha256={sha}")
    print(f"[OTA] bin  -> {bin_dst}")
    print(f"[OTA] meta -> {meta_path}")
    print(f"[OTA] metadata.json:\n{meta_json}")

    if args.dry_run:
        print("[OTA] --dry-run : aucune ecriture.")
        return 0

    bin_dst.parent.mkdir(parents=True, exist_ok=True)
    meta_path.parent.mkdir(parents=True, exist_ok=True)
    bin_dst.write_bytes(args.bin.read_bytes())
    meta_path.write_text(meta_json + "\n", encoding="utf-8")
    print("[OTA] Publication ecrite dans l'arbre n3_serveur (a committer/pousser).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
