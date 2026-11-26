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
import argparse, base64, json, os, random, socket, struct, threading, time

def ws_connect(host, port, path, wait_s=10):
    # retry while the server is still starting up
    deadline = time.time() + wait_s
    while True:
        try:
            s = socket.create_connection((host, port))
            break
        except ConnectionRefusedError:
            if time.time() > deadline:
                raise
            time.sleep(0.5)
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall((f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\n"
               f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
    resp = b""
    while b"\r\n\r\n" not in resp:
        chunk = s.recv(1)
        if not chunk:
            raise ConnectionError("server closed during handshake")
        resp += chunk
    if b" 101 " not in resp.split(b"\r\n")[0]:
        raise ConnectionError(resp.split(b"\r\n")[0].decode())
    return s

def ws_send(s, text):
    data, mask = text.encode(), os.urandom(4)
    n = len(data)
    head = bytes([0x81]) + (bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack(">H", n))
    s.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("closed")
        buf += chunk
    return buf

def ws_recv(s):
    b1, b2 = recv_exact(s, 2)
    n = b2 & 0x7F
    if n == 126:
        n = struct.unpack(">H", recv_exact(s, 2))[0]
    elif n == 127:
        n = struct.unpack(">Q", recv_exact(s, 8))[0]
    payload = recv_exact(s, n)
    return b1 & 0x0F, payload

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--jump-at", type=int, default=900, help="seconds of normal traffic before JUMP-C jumps")
    ap.add_argument("--after", type=int, default=120, help="seconds to keep sending after the jump")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--out", default="baseline_results.json")
    a = ap.parse_args()
    rnd = random.Random(a.seed)

    events = {"NORM-A": [], "NORM-B": [], "JUMP-C": []}   # (seconds since start, abn list)
    learning_end = {}
    start = time.time()
    stop = threading.Event()

    def watch():
        d = ws_connect(a.host, a.port, "/dashboard")
        last = {}
        while not stop.is_set():
            op, payload = ws_recv(d)
            if op != 1:
                continue
            snap = json.loads(payload)
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

    conns = {k: ws_connect(a.host, a.port, "/agent") for k in events}
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
            ws_send(c, json.dumps({"type": "metrics", "id": mid, "os": "test", "cpu": round(cpu, 1),
                                   "ram": {"per": 40}, "disk": {"per": 50},
                                   "net": {"rx_kbps": round(rx, 1), "tx_kbps": round(tx, 1)}}))
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
