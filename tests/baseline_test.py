#!/usr/bin/env python3
"""Baseline / anomaly detection test for the Computer Inspector server.

Sends fake metrics for three machines to a running server and listens on the
dashboard WebSocket to see when the server flags each machine as abnormal.

  NORM-A  steady Gaussian noise, never changes            -> must never alert
  NORM-B  same noise plus short 1-3 s bursts every ~60 s  -> must never alert
  JUMP-C  same noise, then rx jumps to ~2000 KB/s at --jump-at
                                                          -> must alert within seconds

Start the server first, with a learning period shorter than --jump-at, e.g.:
  ./build/server --learn-min 10 --history-dir /tmp/ci-test-history
  python3 tests/baseline_test.py --jump-at 900 --after 120 --out results.json

Only the Python standard library is used (no internet needed).
"""
import argparse, json, os, random, sys, threading, time

# tls with the pin, the websocket client and signing in are shared with virtual_agents.py
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ciproto import Dashboard, Agent, KeyStore, Refused, pin_from_args, join_key_from_args, BUILD

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--pin", help="the server's agent pin (default: read build/server.crt)")
    ap.add_argument("--pin-file", help="server.crt to take the pin from")
    ap.add_argument("--join-key", help="the server's join key (default: read build/agents.json)")
    ap.add_argument("--join-key-file", help="agents.json to take the join key from")
    ap.add_argument("--http-port", type=int, default=8081)
    ap.add_argument("--password", default=os.environ.get("CI_TEACHER_PASSWORD"),
                    help="teacher password, to read the snapshots (or set CI_TEACHER_PASSWORD)")
    ap.add_argument("--key-file", default=os.path.join(BUILD, "baseline_test.keys.json"),
                    help="where the test pcs keep their own keys")
    ap.add_argument("--jump-at", type=int, default=900, help="seconds of normal traffic before JUMP-C jumps")
    ap.add_argument("--after", type=int, default=120, help="seconds to keep sending after the jump")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--out", default="baseline_results.json")
    a = ap.parse_args()
    pin = pin_from_args(a.pin, a.pin_file)
    if not a.password:
        sys.exit("The test reads the dashboard's snapshots: give the teacher password with --password or CI_TEACHER_PASSWORD.")
    try:
        dash = Dashboard(a.host, a.http_port, pin, a.password)
    except Refused as e:
        sys.exit(str(e))
    store = KeyStore(a.key_file, pin)
    join_key = join_key_from_args(a.join_key, a.join_key_file)
    rnd = random.Random(a.seed)

    events = {"NORM-A": [], "NORM-B": [], "JUMP-C": []}   # (seconds since start, abn list)
    learning_end = {}
    start = time.time()
    stop = threading.Event()

    def watch():
        # the dashboard's own way: GET /snapshot (new about once a second), logged in
        last, seen = {}, None
        while not stop.is_set():
            time.sleep(0.25)
            try:
                snap = dash.get_json("/snapshot")
            except (OSError, ValueError):
                continue
            if snap.get("ts") == seen:
                continue
            seen = snap.get("ts")
            t = round(time.time() - start, 1)
            for m in snap.get("machines", []):
                mid = m.get("id")
                if mid not in events:
                    continue
                if not m.get("learn") and mid not in learning_end:
                    learning_end[mid] = t
                abn = sorted(m.get("abn", []))
                if abn != last.get(mid, []):
                    events[mid].append((t, abn))
                    last[mid] = abn
    threading.Thread(target=watch, daemon=True).start()

    def agent(k, wait_s=10):
        # retry while the server is still starting up
        deadline = time.time() + wait_s
        while True:
            try:
                return Agent(a.host, a.port, pin, k, store, join_key).connect()
            except ConnectionRefusedError:
                if time.time() > deadline:
                    raise
                time.sleep(0.5)
    try:
        conns = {k: agent(k) for k in events}
    except Refused as e:
        sys.exit(str(e))
    burst_left = 0
    total = a.jump_at + a.after
    sent = 0
    for sec in range(total):
        tick = start + sec
        for mid, c in conns.items():
            cpu = min(100, max(0, rnd.gauss(25, 6)))
            rx = max(0, rnd.gauss(80, 25))
            tx = max(0, rnd.gauss(20, 8))
            if mid == "NORM-B":
                if burst_left == 0 and rnd.random() < 1 / 60:
                    burst_left = rnd.randint(1, 3)
                if burst_left > 0:
                    cpu, rx, tx = rnd.uniform(60, 90), rnd.uniform(500, 1500), rnd.uniform(100, 400)
                    burst_left -= 1
            if mid == "JUMP-C" and sec >= a.jump_at:
                rx = max(0, rnd.gauss(2000, 150))
            c.send({"type": "metrics", "id": mid, "os": "test", "cpu": round(cpu, 1),
                    "ram": {"per": 40}, "disk": {"per": 50},
                    "net": {"rx_kbps": round(rx, 1), "tx_kbps": round(tx, 1)}})
            sent += 1
        time.sleep(max(0, tick + 1 - time.time()))
    time.sleep(2)
    stop.set()

    first_jump_alert = next((t for t, abn in events["JUMP-C"] if "rx" in abn), None)
    res = {
        "settings": vars(a),
        "samples_sent": sent,
        "learning_ended_s": learning_end,
        "false_alerts": {
            "NORM-A": sum(1 for _, abn in events["NORM-A"] if abn),
            "NORM-B": sum(1 for _, abn in events["NORM-B"] if abn),
            "JUMP-C_before_jump": sum(1 for t, abn in events["JUMP-C"] if abn and t < a.jump_at),
        },
        "jump_at_s": a.jump_at,
        "jump_detected_at_s": first_jump_alert,
        "detection_delay_s": None if first_jump_alert is None else round(first_jump_alert - a.jump_at, 1),
        "events": events,
    }
    with open(a.out, "w") as f:
        json.dump(res, f, indent=2)
    print(json.dumps({k: v for k, v in res.items() if k != "events"}, indent=2))

if __name__ == "__main__":
    main()
