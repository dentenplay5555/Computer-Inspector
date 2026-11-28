"""Shared by the test tools: what a real agent does on the wire, in plain Python.

- TLS that trusts only the certificate matching the server's pin (like agent/pin.cpp)
- a small WebSocket client (masked text frames out, frames in, pong on ping)
- signing in like agent/main.cpp: the server says hello with a nonce; a new pc joins with
  an HMAC made from the join key, a known pc signs the nonce with its own key, and a
  "rekey" is saved first and then confirmed with an HMAC made from the new key

Only the Python standard library is used.
"""
import hashlib, hmac, http.client, json, os, select, socket, ssl, struct, base64

BUILD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build")

# ---------- tls with the pin ----------
TLS = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
TLS.check_hostname = False          # checked by the pin instead
TLS.verify_mode = ssl.CERT_NONE

class Refused(Exception):
    """The server said no for good (wrong pin, wrong join key, ...): retrying will not help.
    Deliberately not an OSError, which the tools treat as "server not up yet"."""

def pin_of_der(der):
    h = hashlib.sha256(der).hexdigest()
    return "-".join(h[i:i + 4] for i in range(0, 32, 4))

def pin_matches(pin, der):
    p = "".join(c for c in pin.lower() if c not in "-: ")
    return len(p) >= 32 and hashlib.sha256(der).hexdigest().startswith(p)

def pin_from_args(pin, pin_file):
    """--pin, else --pin-file, else the server's certificate in build/ (same pc as the server)."""
    if pin:
        return pin
    path = pin_file or os.path.join(BUILD, "server.crt")
    try:
        with open(path) as f:
            return pin_of_der(ssl.PEM_cert_to_DER_cert(f.read()))
    except OSError:
        raise SystemExit(f"No --pin given and {path} not found. Copy the pin from the server window or log.")

def join_key_from_args(join_key, join_key_file):
    """--join-key, else --join-key-file, else build/agents.json on the server pc; None if none."""
    if join_key:
        return join_key
    path = join_key_file or os.path.join(BUILD, "agents.json")
    try:
        with open(path) as f:
            return json.load(f).get("join_key")
    except (OSError, ValueError):
        return None

def norm_join(k):
    return "".join(c for c in k.upper() if c not in "- ")

def sign(key, msg):
    return hmac.new(key.encode(), msg.encode(), hashlib.sha256).hexdigest()

# ---------- websocket ----------
class WS:
    def __init__(self, host, port, path, pin, timeout=3):
        raw = socket.create_connection((host, port), timeout=timeout)
        s = TLS.wrap_socket(raw, server_hostname=host)
        if not pin_matches(pin, s.getpeercert(binary_form=True)):
            s.close()
            raise Refused("The server's certificate does not match the pin (wrong pin, or not our server).")
        key = base64.b64encode(os.urandom(16)).decode()
        s.sendall((f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\n"
                   f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        resp = b""
        while b"\r\n\r\n" not in resp:
            chunk = s.recv(1024)
            if not chunk:
                raise ConnectionError("closed during handshake")
            resp += chunk
        head, self.buf = resp.split(b"\r\n\r\n", 1)
        if b" 101 " not in head.split(b"\r\n")[0]:
            raise ConnectionError("handshake refused")
        self.s = s

    def _send(self, op, data):
        mask, n = os.urandom(4), len(data)
        if n < 126:
            head = bytes([0x80 | op, 0x80 | n])
        elif n < 65536:
            head = bytes([0x80 | op, 0x80 | 126]) + struct.pack(">H", n)
        else:
            head = bytes([0x80 | op, 0x80 | 127]) + struct.pack(">Q", n)
        self.s.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def send(self, text):
        self._send(0x1, text.encode())

    def _frame(self):
        """One complete frame from the buffer, or None."""
        b = self.buf
        if len(b) < 2:
            return None
        op, n, i = b[0] & 0x0F, b[1] & 0x7F, 2
        if n == 126:
            if len(b) < 4: return None
            n, i = struct.unpack(">H", b[2:4])[0], 4
        elif n == 127:
            if len(b) < 10: return None
            n, i = struct.unpack(">Q", b[2:10])[0], 10
        if len(b) < i + n:
            return None
        self.buf = b[i + n:]
        return op, b[i:i + n]

    def recv(self, timeout=None):
        """Next text frame as a string; None when nothing arrived within timeout."""
        while True:
            f = self._frame()
            if f:
                op, payload = f
                if op == 0x9:
                    self._send(0xA, payload)           # pong
                elif op == 0x8:
                    raise ConnectionError("server closed the connection")
                elif op == 0x1:
                    return payload.decode()
                continue
            if self.s.pending() == 0:
                r, _, _ = select.select([self.s], [], [], timeout)
                if not r:
                    return None
            chunk = self.s.recv(65536)
            if not chunk:
                raise ConnectionError("closed")
            self.buf += chunk

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass

# ---------- keys of the virtual pcs ----------
class KeyStore:
    """{"pin": ..., "keys": {"A-01": "..."}} in a file; keys only count for that pin."""
    def __init__(self, path, pin):
        self.path, self.pin = path, pin
        self.keys = {}
        try:
            with open(path) as f:
                d = json.load(f)
            if pin_norm(d.get("pin", "")) == pin_norm(pin):
                self.keys = d.get("keys", {})
        except (OSError, ValueError):
            pass

    def get(self, aid):
        return self.keys.get(aid)

    def set(self, aid, key):
        if key is None:
            self.keys.pop(aid, None)
        else:
            self.keys[aid] = key
        tmp = self.path + ".tmp"
        os.makedirs(os.path.dirname(os.path.abspath(self.path)), exist_ok=True)
        with open(tmp, "w") as f:
            json.dump({"pin": self.pin, "keys": self.keys}, f, indent=2)
        os.chmod(tmp, 0o600)
        os.replace(tmp, self.path)          # whole file or nothing, like the real agent

def pin_norm(p):
    return "".join(c for c in p.lower() if c in "0123456789abcdef")

# ---------- one signed-in agent ----------
class Agent:
    def __init__(self, host, port, pin, aid, store, join_key):
        self.host, self.port, self.pin, self.aid, self.store, self.join_key = host, port, pin, aid, store, join_key
        self.ws = None
        self.rotations = 0

    def _wait(self, types, timeout=5):
        while True:
            t = self.ws.recv(timeout)
            if t is None:
                raise ConnectionError("no answer from the server")
            m = json.loads(t)
            if m.get("type") in types:
                return m
            self._handle(m)

    def _join(self):
        if not self.join_key:
            raise Refused(f"{self.aid} has not joined and there is no join key (--join-key, or build/agents.json)")
        self.ws.send(json.dumps({"type": "join", "id": self.aid,
                                 "proof": sign(norm_join(self.join_key), f"ci-join|{self.nonce}|{self.aid}")}))

    def connect(self):
        self.ws = WS(self.host, self.port, "/agent", self.pin)
        self.nonce = self._wait({"hello"})["nonce"]
        key = self.store.get(self.aid)
        if key:
            self.ws.send(json.dumps({"type": "auth", "id": self.aid, "sig": sign(key, f"ci-auth|{self.nonce}|{self.aid}")}))
        else:
            self._join()
        while True:
            m = self._wait({"ok", "welcome", "denied"})
            if m["type"] == "welcome":
                self.store.set(self.aid, m["key"])
                return self
            if m["type"] == "ok":
                return self
            why = m.get("why")
            if why == "not_joined" and key and self.join_key:
                self.store.set(self.aid, None)      # revoked: join again on this connection
                key = None
                self._join()
                continue
            raise Refused(f"{self.aid}: the server refused it ({why})")

    def _handle(self, m):
        if m.get("type") == "rekey":
            # save first, then confirm; until the server sees the confirmation it keeps the old key
            self.store.set(self.aid, m["key"])
            self.ws.send(json.dumps({"type": "rekeyed", "sig": sign(m["key"], f"ci-rekey|{self.nonce}|{self.aid}")}))
            self.rotations += 1

    def poll(self):
        while True:
            t = self.ws.recv(0)
            if t is None:
                return
            self._handle(json.loads(t))

    def send(self, obj):
        self.ws.send(json.dumps(obj))
        self.poll()

    def close(self):
        if self.ws:
            self.ws.close()

# ---------- the dashboard's https api, logged in as the teacher ----------
class Dashboard:
    """Logs in with the teacher password (POST /login) and keeps the session cookie.
    Every connection checks the certificate pin first, like the agents."""
    def __init__(self, host, port, pin, password):
        self.host, self.port, self.pin, self.cookie = host, port, pin, None
        status, body = self._req("POST", "/login", {"password": password})
        if status != 200:
            raise Refused(f"Teacher login failed ({status}: {body.decode(errors='replace')})")

    def _req(self, method, path, body=None):
        c = http.client.HTTPSConnection(self.host, self.port, context=TLS, timeout=15)
        try:
            c.connect()
            if not pin_matches(self.pin, c.sock.getpeercert(binary_form=True)):
                raise Refused("The server's certificate does not match the pin (wrong pin, or not our server).")
            headers = {"Content-Type": "application/json"} if body is not None else {}
            if self.cookie:
                headers["Cookie"] = self.cookie
            c.request(method, path, json.dumps(body) if body is not None else None, headers)
            r = c.getresponse()
            data = r.read()
            set_cookie = r.getheader("Set-Cookie")
            if set_cookie:
                self.cookie = set_cookie.split(";")[0]
            return r.status, data
        finally:
            c.close()

    def get_json(self, path):
        status, data = self._req("GET", path)
        if status != 200:
            raise ConnectionError(f"GET {path}: {status}")
        return json.loads(data)
