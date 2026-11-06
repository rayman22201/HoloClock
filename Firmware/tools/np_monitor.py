#!/usr/bin/env python
"""Long-running now-playing ticker diagnostics logger.

Polls the HoloClock's /np-debug endpoint and appends one JSON line per sample
to np_monitor.jsonl. Prints an ALERT line whenever something notable changes
(ticker hidden, fetch failures, WiFi disconnect, device unreachable) so the
next "ticker stopped" incident is captured with evidence of WHICH layer failed:

  - wifi.disconnects grew / device unreachable  -> WiFi layer
  - np.last_http_code != 200 / fail_streak > 0  -> stream fetch layer (TLS/API)
  - np.last_err set                             -> API response shape
  - heap.max_alloc < ~45000                     -> heap too fragmented for TLS

Usage:  python np_monitor.py            (uses config.yaml holoclock.ip)
        python np_monitor.py 192.168.1.225
"""
import json
import sys
import time
import urllib.request
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).parent
LOG = HERE / "np_monitor.jsonl"
POLL_SECONDS = 30


def device_ip():
    if len(sys.argv) > 1:
        return sys.argv[1]
    try:
        import yaml
        cfg = yaml.safe_load((HERE / "config.yaml").read_text())
        return cfg["holoclock"]["ip"]
    except Exception:
        return "192.168.1.225"


def fetch(url):
    with urllib.request.urlopen(url, timeout=5) as r:
        return json.loads(r.read().decode())


def main():
    ip = device_ip()
    url = f"http://{ip}:81/np-debug"
    print(f"Polling {url} every {POLL_SECONDS}s -> {LOG}")
    prev = {}
    while True:
        stamp = datetime.now().isoformat(timespec="seconds")
        try:
            data = fetch(url)
            data["ts"] = stamp
            with LOG.open("a") as f:
                f.write(json.dumps(data) + "\n")

            np_, wifi, heap = data["np"], data["wifi"], data["heap"]
            alerts = []
            if prev:
                if prev["np"]["visible"] and not np_["visible"]:
                    alerts.append("TICKER HIDDEN")
                if wifi["disconnects"] > prev["wifi"]["disconnects"]:
                    alerts.append(
                        f"WIFI DISCONNECT #{wifi['disconnects']} reason={wifi['last_disc_reason']}")
                if np_["fail_streak"] > 0 and np_["fail_streak"] != prev["np"]["fail_streak"]:
                    alerts.append(
                        f"FETCH FAILING x{np_['fail_streak']} http={np_['last_http_code']} err='{np_['last_err']}'")
            if heap["max_alloc"] < 45000:
                alerts.append(f"HEAP FRAGMENTED max_alloc={heap['max_alloc']}")
            if data["uptime_ms"] < prev.get("uptime_ms", 0):
                alerts.append("DEVICE REBOOTED")

            status = (f"{stamp} visible={np_['visible']} ok={np_['ok_count']}/{np_['poll_count']} "
                      f"http={np_['last_http_code']} rssi={wifi['rssi']} "
                      f"disc={wifi['disconnects']} heap_max={heap['max_alloc']}")
            print(status)
            for a in alerts:
                print(f"{stamp} *** ALERT: {a}")
            prev = data
        except Exception as e:
            line = {"ts": stamp, "unreachable": str(e)}
            with LOG.open("a") as f:
                f.write(json.dumps(line) + "\n")
            print(f"{stamp} *** ALERT: DEVICE UNREACHABLE ({e})")
        time.sleep(POLL_SECONDS)


if __name__ == "__main__":
    main()
