#!/usr/bin/env python3
"""Verifie un firmware OTA *deja publie* (boucle bout-en-bout).

Apres push dans n3_serveur, ce script rejoue exactement ce que fait un appareil
via `shared/n3_common/n3_ota` :
  1. recupere le metadata.json sur l'URL PUBLIQUE (iot.olution.info),
  2. telecharge le firmware.bin pointe,
  3. verifie que sha256(bin) == metadata.sha256,
  4. verifie la signature ECDSA avec la **cle publique embarquee** dans le
     firmware (`shared/n3_common/src/n3_ota_pubkey.h`).

Echoue si le serveur ne sert pas le binaire attendu (mauvais chemin OTA, push
non deploye, fichier corrompu). Plusieurs tentatives pour absorber un eventuel
delai de deploiement serveur.

Usage :
  python tools/ota/verify_published.py \
      --metadata-url http://iot.olution.info/ota/n3pp/metadata.json
  python tools/ota/verify_published.py \
      --metadata-url http://iot.olution.info/ota/cam/metadata.json --key msp1
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
PUBKEY_HEADER = REPO_ROOT / "shared" / "n3_common" / "src" / "n3_ota_pubkey.h"


def extract_pubkey_pem(header: Path) -> str:
    """Reconstitue le PEM concatene dans OTA_SIGNING_PUBLIC_KEY_PEM[]."""
    text = header.read_text(encoding="utf-8")
    m = re.search(r"OTA_SIGNING_PUBLIC_KEY_PEM\s*\[\s*\]\s*=(.*?);", text, re.DOTALL)
    if not m:
        raise SystemExit(f"OTA_SIGNING_PUBLIC_KEY_PEM introuvable dans {header}")
    chunks = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    pem = "".join(chunks).encode("utf-8").decode("unicode_escape")
    if "BEGIN PUBLIC KEY" not in pem:
        raise SystemExit("PEM de cle publique invalide apres extraction.")
    return pem


def http_get(url: str, timeout: int = 30) -> bytes:
    with urllib.request.urlopen(url, timeout=timeout) as resp:
        return resp.read()


def verify_ffp5(doc: dict, channel: str, model: str) -> tuple[bool, str]:
    """Schema ffp5cs : channels[channel][model], integrite md5 (pas de signature)."""
    entry = (doc.get("channels", {}).get(channel, {}) or {}).get(model)
    if not isinstance(entry, dict):
        return False, f"channels[{channel}][{model}] absent"
    url, expected_md5 = entry.get("bin_url"), entry.get("md5")
    if not url or not expected_md5:
        return False, "champs bin_url/md5 manquants"
    try:
        blob = http_get(url)
    except Exception as exc:  # noqa: BLE001
        return False, f"firmware injoignable ({url}): {exc}"
    actual_md5 = hashlib.md5(blob).hexdigest()
    if actual_md5.lower() != expected_md5.lower():
        return False, f"md5 mismatch: metadata={expected_md5} reel={actual_md5}"
    return True, f"OK version={entry.get('version')} md5 verifie"


def verify_once(metadata_url: str, key: str | None, pub_pem_path: Path,
                channel: str | None = None, model: str | None = None) -> tuple[bool, str]:
    try:
        doc = json.loads(http_get(metadata_url).decode("utf-8"))
    except Exception as exc:  # noqa: BLE001 - on retente
        return False, f"metadata injoignable: {exc}"

    # Schema ffp5cs : presence d'un nœud "channels" + --channel/--model fournis.
    if "channels" in doc and channel and model:
        return verify_ffp5(doc, channel, model)

    entry = doc.get(key) if key else doc
    if not isinstance(entry, dict):
        return False, f"cle metadata '{key}' absente"
    url, expected_sha, sig_b64 = entry.get("url"), entry.get("sha256"), entry.get("signature")
    if not url or not expected_sha:
        return False, "champs url/sha256 manquants"

    try:
        blob = http_get(url)
    except Exception as exc:  # noqa: BLE001
        return False, f"firmware injoignable ({url}): {exc}"

    actual_sha = hashlib.sha256(blob).hexdigest()
    if actual_sha.lower() != expected_sha.lower():
        return False, f"sha256 mismatch: metadata={expected_sha} reel={actual_sha}"

    if sig_b64:
        with tempfile.TemporaryDirectory() as td:
            bin_p = Path(td) / "fw.bin"
            sig_p = Path(td) / "sig.der"
            bin_p.write_bytes(blob)
            import base64
            sig_p.write_bytes(base64.b64decode(sig_b64))
            res = subprocess.run(
                ["openssl", "dgst", "-sha256", "-verify", str(pub_pem_path),
                 "-signature", str(sig_p), str(bin_p)],
                capture_output=True, text=True,
            )
            if res.returncode != 0:
                return False, f"signature ECDSA invalide ({res.stdout.strip()} {res.stderr.strip()})"
        return True, f"OK version={entry.get('version')} sha256+signature verifies"
    return True, f"OK version={entry.get('version')} sha256 verifie (pas de signature)"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--metadata-url", required=True, help="URL publique du metadata.json")
    p.add_argument("--key", help="Cle metadata (cam: msp1/n3pp/ffp3)")
    p.add_argument("--channel", help="Canal channels[...] (schema ffp5cs)")
    p.add_argument("--model", help="Modele channels[chan][...] (ffp5cs: esp32-wroom)")
    p.add_argument("--pubkey-header", type=Path, default=PUBKEY_HEADER,
                   help="Header contenant la cle publique embarquee")
    p.add_argument("--retries", type=int, default=6, help="Tentatives (delai serveur)")
    p.add_argument("--delay", type=int, default=20, help="Secondes entre tentatives")
    args = p.parse_args()

    pem = extract_pubkey_pem(args.pubkey_header)
    with tempfile.NamedTemporaryFile("w", suffix=".pem", delete=False) as f:
        f.write(pem)
        pub_path = Path(f.name)

    last = ""
    for attempt in range(1, args.retries + 1):
        ok, msg = verify_once(args.metadata_url, args.key, pub_path, args.channel, args.model)
        if ok:
            print(f"[VERIFY] {msg}")
            return 0
        last = msg
        print(f"[VERIFY] tentative {attempt}/{args.retries} echouee: {msg}")
        if attempt < args.retries:
            time.sleep(args.delay)

    print(f"::error::Verification post-deploiement echouee apres {args.retries} tentatives: {last}")
    print("Si le serveur a un delai/etape de deploiement, augmenter --retries/--delay "
          "ou desactiver l'etape (verify_published=false).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
