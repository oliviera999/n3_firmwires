#!/usr/bin/env python3
"""
Analyse des échanges avec le serveur distant (iot.olution.info) dans un log série ESP32.

Extrait et résume:
- Connexions TCP/TLS vers le serveur
- Requêtes GET (fetch config / outputs state)
- Requêtes POST (push status, heartbeat) et leur succès/échec
- Blocages (heap, WiFi, timeout)

Usage:
  python tools/monitor/analyze_remote_server_exchanges.py monitor_5min_2026-01-28_17-38-52.log
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from collections import defaultdict


def read_lines(path: Path) -> list[str]:
    if not path.exists():
        return []
    try:
        return path.read_text(encoding="utf-8", errors="ignore").splitlines()
    except Exception:
        return path.read_bytes().decode("utf-8", errors="ignore").splitlines()


def analyze(lines: list[str]) -> dict:
    out: dict = {
        "connections": [],
        "get_requests": [],
        "get_success": 0,
        "post_blocked_heap": 0,
        "post_blocked_wifi": 0,
        "tls_recovered": 0,
        "sync_config_applied": 0,
        "sync_post_attempted": 0,
        "boot_fetch_ok": False,
        "boot_fetch_fail": False,
    }

    for i, line in enumerate(lines):
        # Connexion établie vers iot.olution.info
        if "connected to iot.olution.info" in line:
            out["connections"].append(line.strip())
        # GET outputs/state (URL dans le log)
        if "url: https://iot.olution.info" in line and "/outputs" in line:
            out["get_requests"].append(line.strip())
        if "sendRequest(): sendRequest code=200" in line and "Mail" not in line:
            out["get_success"] += 1
        # Boot tryFetchConfigFromServer
        if "Boot tryFetchConfigFromServer: OK" in line:
            out["boot_fetch_ok"] = True
        if "Boot tryFetchConfigFromServer: ECHEC" in line:
            out["boot_fetch_fail"] = True
        # POST bloqué (heap insuffisant)
        if "TLS nécessite" in line and "report de la requête" in line:
            out["post_blocked_heap"] += 1
        if "Envoi POST bloqué" in line and "WiFi=NO" in line:
            out["post_blocked_wifi"] += 1
        # TLS stop / récupération mémoire
        if "[HTTP] 🔄 TLS stop: récupéré" in line:
            out["tls_recovered"] += 1
        # Sync: configuration appliquée depuis JSON
        if "[Sync] Configuration appliquée depuis JSON" in line:
            out["sync_config_applied"] += 1
        # Sync: envoi POST tenté
        if "[Sync] ✅ Conditions remplies, envoi POST" in line:
            out["sync_post_attempted"] += 1
    return out


def print_report(summary: dict, log_name: str) -> None:
    print("=== ANALYSE DES ÉCHANGES AVEC LE SERVEUR DISTANT ===")
    print(f"Log: {log_name}")
    print()

    print("--- Boot ---")
    print(f"  tryFetchConfigFromServer au boot: {'OK' if summary['boot_fetch_ok'] else 'ECHEC' if summary['boot_fetch_fail'] else 'non observé'}")
    print()

    print("--- Connexions TCP/TLS (iot.olution.info:443) ---")
    n_conn = len(summary["connections"])
    print(f"  Nombre de connexions établies: {n_conn}")
    if summary["connections"]:
        print(f"  Exemple: {summary['connections'][0][:80]}...")
    print()

    print("--- Requêtes GET (fetch config / outputs state) ---")
    n_get = len(summary["get_requests"])
    print(f"  Requêtes GET initiées (URL visibles): {n_get}")
    print(f"  Réponses HTTP 200 observées: {summary['get_success']}")
    print()

    print("--- Requêtes POST (push status / heartbeat) ---")
    print(f"  Tentatives d'envoi POST (Sync): {summary['sync_post_attempted']}")
    print(f"  POST bloqués (heap insuffisant / fragmentation): {summary['post_blocked_heap']}")
    print(f"  POST bloqués (WiFi déconnecté): {summary['post_blocked_wifi']}")
    print()

    print("--- Application config distante ---")
    print(f"  « Configuration appliquée depuis JSON »: {summary['sync_config_applied']} fois")
    print()

    print("--- Mémoire TLS ---")
    print(f"  Récupérations mémoire après TLS (TLS stop): {summary['tls_recovered']}")
    print()

    if summary["post_blocked_heap"] > 0:
        print("--- Synthèse ---")
        print("  Le serveur distant répond correctement en GET (config).")
        print("  Les POST (mesures/heartbeat) sont souvent bloqués par manque de bloc")
        print("  contigu pour TLS (~45 KB). Réduire la fragmentation ou libérer du heap")
        print("  avant les envois POST peut améliorer le push vers le serveur.")


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: python tools/monitor/analyze_remote_server_exchanges.py <fichier_log>")
        return 2
    log_path = Path(sys.argv[1])
    lines = read_lines(log_path)
    if not lines:
        print("Log vide ou introuvable.")
        return 1
    summary = analyze(lines)
    print_report(summary, log_path.name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
