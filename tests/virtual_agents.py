#!/usr/bin/env python3
"""Virtual agents for testing Computer Inspector without real student PCs.

Each virtual agent opens its own WebSocket to the server (like a real agent) and sends
a metrics message every second, in the same JSON format as agent/main.cpp.

Ids follow the seat-map example: A-01..A-06, B-01..B-06, ... (6 per row), so
layout.example.json works out of the box.

With --mix (the default) a few agents behave differently so every state shows up:
  A-02  warning:  CPU around 85 %
  A-03  critical: RAM around 93 %
  A-04  offline:  stops sending after --offline-after seconds
  A-05  jump:     download jumps to ~2000 KB/s after --jump-after seconds
                  (the history branch flags it once learning is over)
  LAB-X not in the seat map (only with --mix)
Everyone else sends normal noise. Use --all-normal to turn the special cases off.

Only the Python standard library is used. Ctrl+C stops all agents.
"""
import argparse, base64, hashlib, json, os, random, socket, ssl, struct, sys, time

# tls: the server only speaks wss://. like the real agent, trust exactly the certificate
# whose sha-256 matches the pin (shown in the server window / log as "Agent pin: ...")
TLS = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
TLS.check_hostname = False          # checked by the pin instead
TLS.verify_mode = ssl.CERT_NONE

def pin_of_der(der):
    h = hashlib.sha256(der).hexdigest()
    return "-".join(h[i:i + 4] for i in range(0, 32, 4))

def pin_matches(pin, der):
    p = "".join(c for c in pin.lower() if c not in "-: ")
    return len(p) >= 32 and hashlib.sha256(der).hexdigest().startswith(p)

def pin_from_args(pin, pin_file):
    # --pin, else --pin-file, else the server's certificate in build/ (same pc as the server)
    if pin:
        return pin
    path = pin_file or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "server.crt")
    try:
        with open(path) as f:
            return pin_of_der(ssl.PEM_cert_to_DER_cert(f.read()))
    except OSError:
        sys.exit(f"No --pin given and {path} not found. Copy the pin from the server window or log.")

def tls_wrap(raw, host, pin):
    s = TLS.wrap_socket(raw, server_hostname=host)
    if not pin_matches(pin, s.getpeercert(binary_form=True)):
        s.close()
        # not an OSError on purpose: retrying will not help, stop and say why
        raise SystemExit("The server's certificate does not match the pin (wrong pin, or not our server).")
    return s

def connect(host, port, pin):
    s = tls_wrap(socket.create_connection((host, port), timeout=2), host, pin)
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall((f"GET /agent HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\n"
               f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
    resp = b""
    while b"\r\n\r\n" not in resp:
        chunk = s.recv(1024)
        if not chunk:
            raise ConnectionError("closed during handshake")
        resp += chunk
    if b" 101 " not in resp.split(b"\r\n")[0]:
        raise ConnectionError("handshake refused")
    s.settimeout(None)
    return s

def send(s, text):
    data, mask = text.encode(), os.urandom(4)
    n = len(data)
    head = bytes([0x81]) + (bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack(">H", n))
    s.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

def seat_ids(n):
    out = []
    for i in range(n):
        row, col = divmod(i, 6)
        out.append(f"{chr(ord('A') + row % 26)}-{col + 1:02d}")
    return out

def main():
    ap = argparse.ArgumentParser(description="Start virtual agents that send test metrics.")
    ap.add_argument("-n", "--count", type=int, default=12, help="number of agents (default 12)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--pin", help="the server's agent pin (default: read build/server.crt)")
    ap.add_argument("--pin-file", help="server.crt to take the pin from")
    ap.add_argument("--all-normal", action="store_true", help="no warning/critical/offline/jump agents")
    ap.add_argument("--offline-after", type=int, default=20, help="seconds before A-04 goes quiet")
    ap.add_argument("--jump-after", type=int, default=120, help="seconds before A-05 download jumps")
    ap.add_argument("--seconds", type=int, default=0, help="stop after this many seconds (0 = until Ctrl+C)")
    ap.add_argument("--seed", type=int, default=None)
    a = ap.parse_args()
    pin = pin_from_args(a.pin, a.pin_file)
    if not 1 <= a.count <= 200:
        sys.exit("--count must be between 1 and 200")
    rnd = random.Random(a.seed)

    ids = seat_ids(a.count)
    role = {i: "normal" for i in ids}
    if not a.all_normal:
        for mid, r in (("A-02", "warn"), ("A-03", "crit"), ("A-04", "offline"), ("A-05", "jump")):
            if mid in role:
                role[mid] = r
        ids.append("LAB-X")
        role["LAB-X"] = "normal"
    os_of = {mid: ("windows" if k % 3 else "linux") for k, mid in enumerate(ids)}

    print(f"Virtual agents -> ws://{a.host}:{a.port}/agent")
    specials = {mid: r for mid, r in role.items() if r != "normal"}
    print(f"  {len(ids)} agents: {', '.join(ids)}")
    if specials:
        desc = {"warn": "CPU ~85 % (warning)", "crit": "RAM ~93 % (critical)",
                "offline": f"stops after {a.offline_after} s (offline 30 s later)",
                "jump": f"download jumps to ~2000 KB/s after {a.jump_after} s"}
        for mid, r in specials.items():
            print(f"  {mid}: {desc[r]}")
    print("Ctrl+C to stop.")

    conns = {}
    warned = False
    start = time.time()
    sec = 0
    try:
        while not a.seconds or sec < a.seconds:
            for mid in ids:
                r = role[mid]
                if r == "offline" and sec >= a.offline_after:
                    if mid in conns:
                        conns.pop(mid).close()
                    continue
                cpu = min(100, max(0, rnd.gauss(25, 6)))
                ram = min(100, max(0, rnd.gauss(45, 4)))
                rx = max(0, rnd.gauss(80, 25))
                tx = max(0, rnd.gauss(20, 8))
                if r == "warn":
                    cpu = min(100, max(0, rnd.gauss(85, 2)))
                if r == "crit":
                    ram = min(100, max(0, rnd.gauss(93, 1)))
                if r == "jump" and sec >= a.jump_after:
                    rx = max(0, rnd.gauss(2000, 150))
                total_mb = 8192
                msg = {"type": "metrics", "id": mid, "os": os_of[mid], "ts": int(time.time() * 1000),
                       "cpu": round(cpu, 1),
                       "ram": {"used_mb": int(total_mb * ram / 100), "total_mb": total_mb, "per": round(ram, 1)},
                       "net": {"rx_kbps": round(rx, 1), "tx_kbps": round(tx, 1)},
                       "disk": {"use_gb": 120.0, "tot_gb": 256.0, "per": 46.9}}
                try:
                    if mid not in conns:
                        conns[mid] = connect(a.host, a.port, pin)
                    send(conns[mid], json.dumps(msg))
                    warned = False
                except OSError:
                    # server not up yet or restarted: retry next second
                    old = conns.pop(mid, None)
                    if old:
                        old.close()
                    if not warned:
                        print(f"Cannot reach the server at {a.host}:{a.port}, retrying every second...")
                        warned = True
            sec += 1
            time.sleep(max(0, start + sec - time.time()))
    except KeyboardInterrupt:
        pass
    for s in conns.values():
        s.close()
    print(f"\nStopped after {sec} s.")

if __name__ == "__main__":
    main()
