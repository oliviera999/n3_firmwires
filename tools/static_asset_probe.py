#!/usr/bin/env python3
import argparse
import json
import time
import urllib.error
import urllib.request
from pathlib import Path

LOG_PATH = Path("c:/Users/olivi/Mon Drive/travail/olution/Projets/prototypage/platformIO/Projects/ffp5cs/.cursor/debug.log")
SESSION_ID = "debug-session"


def build_url(base_url: str, path: str) -> str:
    if base_url.endswith("/"):
        base_url = base_url[:-1]
    return f"{base_url}{path}"


# region agent log
def log_event(run_id: str, hypothesis_id: str, location: str, message: str, data: dict) -> None:
    entry = {
        "sessionId": SESSION_ID,
        "runId": run_id,
        "hypothesisId": hypothesis_id,
        "location": location,
        "message": message,
        "data": data,
        "timestamp": int(time.time() * 1000),
    }
    LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
    with LOG_PATH.open("a", encoding="utf-8") as fh:
        fh.write(json.dumps(entry))
        fh.write("\n")
# endregion


def fetch_bytes(url: str, timeout: float = 5.0):
    req = urllib.request.Request(url, headers={"Cache-Control": "no-cache"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            payload = resp.read()
            return resp.status, resp.headers, payload, None
    except urllib.error.HTTPError as err:
        return err.code, err.headers, b"", str(err)
    except Exception as exc:  # pylint: disable=broad-except
        return None, {}, b"", str(exc)


def probe_assets(base_url: str, run_id: str) -> None:
    asset_checks = [
        ("H1", "/shared/common.css", "GET shared/common.css"),
        ("H1", "/shared/common.js", "GET shared/common.js"),
        ("H1", "/shared/websocket.js", "GET shared/websocket.js"),
        ("H2", "/assets/css/uplot.min.css", "GET assets css"),
        ("H2", "/assets/js/uplot.iife.min.js", "GET assets js"),
    ]
    for hypothesis_id, path, description in asset_checks:
        target_url = build_url(base_url, path)
        status, headers, payload, error = fetch_bytes(target_url)
        data = {
            "url": target_url,
            "status": status,
            "contentLength": len(payload),
            "error": error,
            "cacheControl": headers.get("Cache-Control") if headers else None,
        }
        # region agent log
        log_event(run_id, hypothesis_id, "tools/static_asset_probe.py:probe_assets", description, data)
        # endregion


def probe_debug_endpoint(base_url: str, run_id: str) -> None:
    target_url = build_url(base_url, "/debug/static-assets")
    status, _, payload, error = fetch_bytes(target_url)
    parsed = None
    if payload:
        try:
            parsed = json.loads(payload.decode("utf-8"))
        except json.JSONDecodeError as exc:
            error = f"json_error: {exc}"
    data = {
        "url": target_url,
        "status": status,
        "payload": parsed,
        "error": error,
    }
    # region agent log
    log_event(run_id, "H3", "tools/static_asset_probe.py:probe_debug_endpoint", "GET debug/static-assets", data)
    # endregion


def main():
    parser = argparse.ArgumentParser(description="Collect static asset status for FFP5CS web server")
    parser.add_argument("--base-url", default="http://192.168.0.220", help="Base URL of the ESP32 web server")
    parser.add_argument("--run-id", default="pre-fix", help="Identifier for this debugging run")
    args = parser.parse_args()

    probe_assets(args.base_url, args.run_id)
    probe_debug_endpoint(args.base_url, args.run_id)


if __name__ == "__main__":
    main()
