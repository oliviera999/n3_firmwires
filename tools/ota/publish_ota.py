#!/usr/bin/env python3
"""Signe/publie un firmware OTA pour l'ecosysteme n3.

Deux schemas de metadata coexistent :

  - "n3ota" (n3pp / msp / cam) : verif sha256 + signature ECDSA P-256
    (`shared/n3_common/n3_ota`). metadata = objet {version,url,sha256,signature},
    multi-cles pour cam. HTTP.
        sha256    = hex des octets du .bin
        signature = base64(ECDSA-DER sur sha256)  (openssl dgst -sha256 -sign)

  - "ffp5" (ffp5cs WROOM) : verif md5 (`ffp5cs/ota_manager`, OtaArtifactSelect).
    metadata = { "channels": { <prod|test>: { <model>: {version,bin_url,size,md5} } } }
    (fusion non destructive). HTTPS. Pas de signature ECDSA.
    Perimetre : WROOM firmware seul (modele esp32-wroom). S3 + image filesystem
    LittleFS = suivi separe (cf. docs/OTA_GITHUB_DEPLOY.md).

Modes :
  --print-version   Affiche la version (lue dans firmwares.manifest.json) et sort.
  (defaut)          Calcule l'integrite et ecrit firmware.bin + metadata.json.

Garde-fou (--guard, actif par defaut) : recupere le metadata en ligne et ECHOUE
si la version a publier n'est pas strictement superieure a celle deja servie
(evite un deploiement no-op rejete par les appareils). --no-guard pour forcer
(ex. rollback vers une version anterieure).
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

# Mapping selection workflow -> details de publication.
#   scheme "n3ota" : subdir (+ subdir_test pour le canal test), key (cam).
#   scheme "ffp5"  : model + bindir par canal (le canal = cle channels prod/test).
TARGETS = {
    "n3pp":      {"id": "n3pp", "scheme": "n3ota", "subdir": "n3pp", "subdir_test": "n3pp-test", "key": None},
    "msp":       {"id": "msp",  "scheme": "n3ota", "subdir": "msp",  "subdir_test": "msp-test",  "key": None},
    "cam-msp1":  {"id": "uploadphotosserver", "scheme": "n3ota", "subdir": "cam", "key": "msp1"},
    "cam-n3pp":  {"id": "uploadphotosserver", "scheme": "n3ota", "subdir": "cam", "key": "n3pp"},
    "cam-ffp3":  {"id": "uploadphotosserver", "scheme": "n3ota", "subdir": "cam", "key": "ffp3"},
    "pgl":       {"id": "poissonglouton", "scheme": "n3ota", "subdir": "pgl", "key": None},
    "ffp5-wroom": {"id": "ffp5cs", "scheme": "ffp5", "model": "esp32-wroom",
                   "bindir": {"prod": "esp32-wroom", "test": "esp32-wroom-beta"}},
}

DEFAULT_BASE = {"n3ota": "http://iot.olution.info/ota", "ffp5": "https://iot.olution.info/ota"}


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
        nums = (re.findall(r"\d+", v or "") + ["0", "0", "0"])[:3]
        return [int(n) for n in nums]
    pa, pb = parts(a), parts(b)
    return (pa > pb) - (pa < pb)


def sha256_hex(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def md5_hex(path: Path) -> str:
    return hashlib.md5(path.read_bytes()).hexdigest()


def sign_b64(bin_path: Path, key_path: Path) -> str:
    """Signature ECDSA DER sur sha256(bin), encodee base64 (cf. n3_ota.cpp)."""
    der = subprocess.run(
        ["openssl", "dgst", "-sha256", "-sign", str(key_path), str(bin_path)],
        check=True, capture_output=True,
    ).stdout
    return base64.b64encode(der).decode("ascii")


def fetch_json(url: str):
    try:
        with urllib.request.urlopen(url, timeout=15) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except (urllib.error.URLError, json.JSONDecodeError, OSError) as exc:
        print(f"[OTA] metadata en ligne indisponible ({exc}); pas de comparaison.")
        return None


def guard_version(scheme, metadata_url, new_version, *, key=None, channel=None, model=None):
    """Echoue si new_version <= version en ligne (si une version est servie)."""
    doc = fetch_json(metadata_url)
    if doc is None:
        return
    if scheme == "ffp5":
        entry = (doc.get("channels", {}).get(channel, {}) or {}).get(model)
    else:
        entry = doc.get(key) if key else doc
    remote = entry.get("version") if isinstance(entry, dict) else None
    if remote is None:
        return
    if compare_versions(new_version, remote) <= 0:
        raise SystemExit(
            f"[OTA] Garde-fou: version a publier {new_version} <= version en ligne {remote}. "
            f"Bumper le firmware ou utiliser --no-guard (rollback).")
    print(f"[OTA] Garde-fou OK: {new_version} > {remote} (en ligne).")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--firmware", required=True, choices=sorted(TARGETS), help="Cible OTA")
    p.add_argument("--bin", type=Path, help="Chemin du firmware.bin compile")
    p.add_argument("--key", type=Path, help="Cle privee ECDSA (PEM) — schema n3ota uniquement")
    p.add_argument("--ota-root", type=Path, help="Racine OTA dans n3_serveur (ex: serveur/ota)")
    p.add_argument("--channel", choices=["prod", "test"], default="prod", help="Canal de deploiement")
    p.add_argument("--base-url", help="Prefixe URL public (defaut selon schema)")
    p.add_argument("--version", help="Force la version (sinon lue depuis le manifest)")
    p.add_argument("--print-version", action="store_true", help="Affiche la version et sort")
    p.add_argument("--guard", action=argparse.BooleanOptionalAction, default=True,
                   help="Echoue si version <= celle en ligne (defaut: oui)")
    p.add_argument("--dry-run", action="store_true", help="N'ecrit rien, affiche le metadata")
    args = p.parse_args()

    target = TARGETS[args.firmware]
    scheme = target["scheme"]
    version = args.version or read_version(target["id"])

    if args.print_version:
        print(version)
        return 0

    for name, val in (("--bin", args.bin), ("--ota-root", args.ota_root)):
        if val is None:
            raise SystemExit(f"{name} requis hors mode --print-version")
    if not args.bin.is_file():
        raise SystemExit(f"Binaire introuvable : {args.bin}")
    if scheme == "n3ota":
        if args.key is None or not args.key.is_file():
            raise SystemExit("--key (cle privee ECDSA) requis pour le schema n3ota")

    base_url = (args.base_url or DEFAULT_BASE[scheme]).rstrip("/")

    if scheme == "ffp5":
        rc = publish_ffp5(args, target, version, base_url)
    else:
        rc = publish_n3ota(args, target, version, base_url)
    return rc


def publish_n3ota(args, target, version, base_url) -> int:
    subdir = target["subdir"]
    if args.channel == "test":
        if "subdir_test" not in target:
            raise SystemExit(f"Pas de canal test pour {args.firmware}")
        subdir = target["subdir_test"]

    metadata_url = f"{base_url}/{subdir}/metadata.json"
    if args.guard:
        guard_version("n3ota", metadata_url, version, key=target["key"])

    sha = sha256_hex(args.bin)
    sig = sign_b64(args.bin, args.key)
    bin_rel = f"{subdir}/{target['key']}/firmware.bin" if target["key"] else f"{subdir}/firmware.bin"
    entry = {"version": version, "url": f"{base_url}/{bin_rel}", "sha256": sha, "signature": sig}

    meta_path = args.ota_root / subdir / "metadata.json"
    bin_dst = args.ota_root / bin_rel

    if target["key"]:
        existing = json.loads(meta_path.read_text(encoding="utf-8")) if meta_path.is_file() else {}
        existing[target["key"]] = entry
        meta_obj = existing
    else:
        meta_obj = entry

    return write_out(args, version, sha, bin_dst, meta_path, meta_obj)


def publish_ffp5(args, target, version, base_url) -> int:
    model = target["model"]
    channel = args.channel  # prod/test == cle channels
    bindir = target["bindir"][channel]
    metadata_url = f"{base_url}/metadata.json"
    if args.guard:
        guard_version("ffp5", metadata_url, version, channel=channel, model=model)

    md5 = md5_hex(args.bin)
    size = args.bin.stat().st_size
    bin_rel = f"{bindir}/firmware.bin"
    entry = {"version": version, "bin_url": f"{base_url}/{bin_rel}", "size": size, "md5": md5}

    meta_path = args.ota_root / "metadata.json"
    bin_dst = args.ota_root / bin_rel

    doc = json.loads(meta_path.read_text(encoding="utf-8")) if meta_path.is_file() else {}
    doc.setdefault("channels", {}).setdefault(channel, {})[model] = entry

    return write_out(args, version, md5, bin_dst, meta_path, doc, integrity_label="md5")


def write_out(args, version, integrity, bin_dst, meta_path, meta_obj, integrity_label="sha256") -> int:
    meta_json = json.dumps(meta_obj, separators=(",", ":"), ensure_ascii=False)
    print(f"[OTA] firmware={args.firmware} version={version} canal={args.channel}")
    print(f"[OTA] {integrity_label}={integrity}")
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
