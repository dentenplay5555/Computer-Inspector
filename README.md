# Computer Inspector for Classroom
 
*(Classroom Network Intelligence)*
 
A lightweight tool that lets a teacher see the health of every computer in a classroom
and the network between them, in one place, in real time.
 
## Why
 
In a school computer lab, a slow machine, a congested network, or a machine that is
silently downloading a lot of data usually goes unnoticed until a student raises a hand.
Classroom-control tools (screen view, remote control) show *what is on the screen*, but not
*how the machine and the network are doing*. General monitoring tools show the numbers, but
are heavy to set up and not designed for a classroom.
 
## Main idea
 
> How can a teacher tell which computer is using the network more than normal, or is
> behaving suspiciously (for example because of malware)?
 
**Answer:** measure CPU, memory and network usage on every computer, keep a history of
these statistics, and compare each machine against its own normal behaviour.
 
This does **not** identify a specific virus. It points the teacher to the machine that
behaves abnormally, so it can be checked.
 
## Features
 
Status: `[x]` done, `[ ]` planned.
 
- [x] Read CPU / RAM / network statistics (Linux)
- [x] Read CPU / RAM / network statistics (Windows)
- [x] Read CPU / RAM / network statistics (macOS)
- [x] Send statistics to a central server over WebSocket
- [x] Teacher dashboard: one card per computer (ok / warning / critical / offline), Thai / English, light / dark
- [x] Server window (`server_gui`): server status, connect addresses, live machine table, log (Thai / English, light / dark)
- [x] Classroom seat map
- [x] Stat history and per-machine baseline, to flag abnormal usage
- [ ] Grouped alerts (e.g. a whole row goes offline → likely a switch problem)
- [ ] On-demand screen streaming with adaptive quality
- [ ] Limited host control (send message, lock screen) with authentication
## How it works
 
```
  Student PC (agent, C++)  -> wss:// (TLS) ->  Server (C++)  -> https:// ->  Dashboard (web + Desktop app)
<measures CPU/RAM/network>          <keeps latest state + history>
```
 
## Project layout
 
```
agent/       measures statistics on each computer
server/      receives data from agents and serves the dashboard
server/gui/  native window for the server (Dear ImGui + GLFW)
common/      code shared by agent and server (TLS certificate pin)
dashboard/   web page for the teacher
docs/        API reference and test notes
```
 
## Quick start

One script builds everything, starts the server and opens the dashboard:

```
./run.sh            # Mac / Linux
run.bat             # Windows
```

The first time, it asks for a teacher password (it protects the dashboard; typing is
hidden). `--set-password` sets a new one before starting. Add `--agent` to also start an
agent on the same computer (handy for testing without
other PCs). `./run.sh --help` lists the other options. Ctrl+C stops everything on
Mac / Linux; on Windows, close the server window.

On Windows, `run.bat` builds with [Ninja](https://ninja-build.org/) (`winget install
Ninja-build.Ninja`, or `pacman -S mingw-w64-ucrt-x86_64-ninja` in MSYS2; Visual Studio's
Developer Command Prompt already has it). Run it where a compiler is on PATH: MinGW `g++`,
or the "x64 Native Tools Command Prompt for VS". A `build\` folder made earlier with NMake
or Visual Studio is switched to Ninja automatically; saved `layout.json` and
`machines.json` in it are kept.

To fill the dashboard without real PCs, start virtual agents in a second terminal:

```
./test.sh             # 12 agents A-01..B-06 + LAB-X, with warning, critical,
                      # offline and download-jump cases mixed in
./test.sh -n 30 --all-normal
```

They send the same JSON as the real agent, over TLS with the same pin check, join and
sign in like it (keys in `build/virtual_agents.keys.json`), follow key rotation, and
reconnect if the server restarts. On the server PC they read the pin from
`build/server.crt` and the join key from `build/agents.json`; for a server elsewhere add
`--host <ip> --pin <pin> --join-key <key>`.
`./test.sh --help` lists all options (needs Python 3, no extra packages).

## Build
 
Requirements: a C++17 compiler and CMake.
 
```
cmake -S . -B build #this section is optional but required to for first time you do this
cmake --build build #<--target <server/agent> (This section is optional: default is agent) >
./build/agent #or you can use ./build/server if you compiled server source code
```

The first configure also downloads mbedTLS 3.6 (TLS for `wss://` and `https://`; checked
against its published SHA-256), so no system OpenSSL is needed.

Targets: `agent`, `server` (console) and `server_gui` (window). Both servers use the same
core, so they behave the same. Run either one from the repository root so it can find
`dashboard/index.html`.

`server_gui` downloads GLFW, Dear ImGui and the Noto Sans / Noto Sans Thai fonts the first
time CMake configures (the fonts are compiled into the binary). On Linux it needs the
X11 or Wayland development packages; to skip it on a machine without a desktop:

```
cmake -S . -B build -DSERVER_GUI=OFF
```
 
## Encryption and the agent pin

Everything on the network is encrypted with TLS: agents connect with `wss://` (port 8080)
and the dashboard is `https://` (port 8081). On its first start the server makes its own
certificate and key, `server.crt` and `server.key`, next to the server executable (keep
`server.key` private; deleting both makes a new pair, and every agent then needs the new
pin).

Agents trust only that certificate. The server window and the server log show its
**agent pin** (the first 128 bits of the certificate's SHA-256), and every agent needs it:

```
./agent --host 192.168.1.10 --pin 3fa2-91bc-0d4e-77a1-5c20-e9f3-1b86-44d0
```

An agent without `--pin` does not start, and one whose server presents a different
certificate (a wrong pin, a reinstalled server, or someone pretending to be the server)
refuses to connect and says so. On the server PC itself, `--pin-file build/server.crt`
reads the pin from the certificate (`run.sh --agent` and `run.bat --agent` do this).

The browser warns the first time, because the certificate is the server's own and not from
a public authority: accept it once per browser, or install `server.crt` as trusted on the
teacher PCs. The dashboard only talks to port 8081, so it is one exception. (While a
browser only has the exception and not the trust, it opens and drops an extra connection
each second; the console server prints a "tls accept failed" line for it. Harmless, and
it stops once `server.crt` is trusted.)

## Joining and keys

Only PCs that joined may send data. The server keeps who may join and each PC's key in
`agents.json` next to the server executable (secret: owner-only on Mac / Linux).

1. **Join key.** The server window and log show it (e.g. `5Y7X-J9FB-AZBX-THN9-W6JB-53TU-AZ`).
   Run each new PC once with it:

   ```
   ./agent --host 192.168.1.10 --pin 40d9-… --join-key 5Y7X-J9FB-…
   ```

   The PC then gets **its own key**, saved in `agent.key` next to the agent (owner-only on
   Mac / Linux; `--key-file` to put it elsewhere). From then on `--join-key` is not needed.
   **New join key** in the server window replaces it; PCs that already joined keep working.
2. **Signing in.** On every connection the server sends a random number, and the agent
   answers with a signature made from its key (HMAC-SHA256). Keys are never sent to prove
   anything, and metrics only count after signing in, under the ID the PC signed in as
   (whatever the message says), so one PC cannot send data as another.
3. **Rotation.** When a PC's key is older than 24 hours and it is online, the server sends
   it a new one. The agent saves it (whole file or nothing), then proves it has it; only
   then does the server drop the old key. If anything breaks in between (crash, power
   cut), either key still works and the rotation finishes on the next connection. A PC that
   was off for a week rotates as soon as it is back. `server --rotate-hours N` changes the
   interval (`0` = never).
4. **Revoking.** **Revoke key** in a computer's edit pop-up on the dashboard (teacher
   login) disconnects it and forgets its key; it must join again with the join key. Use
   it when a PC is reinstalled or its key may have leaked.

The join key alone cannot take over a PC that already joined: a second join with the same
ID is refused until that ID's key is revoked. The agent stops with a clear message (exit
code 3) when the server refuses it for good: wrong join key, ID already joined, key revoked.
On the server PC, `--join-key-file build/agents.json` reads the join key from the server's
file (`run.sh --agent` and `run.bat --agent` do this, like `--pin-file`).

## Teacher login and audit log

The dashboard only shows student PCs after logging in with the teacher password.

- **First time:** `run.sh` / `run.bat` ask for it before starting the server (typing is
  hidden; `--set-password` changes it later). It can also be set in the server window,
  with `server --set-password`, or in the dashboard opened on the server PC itself. Until then the
  dashboard on other PCs says to do that. At least 8 characters.
- The password is stored only as a salted PBKDF2-SHA256 hash (600,000 rounds) in
  `auth.json` next to the server (owner-only on Mac / Linux). Checking it takes a moment on
  purpose. `server --set-password` replaces it (restart a running server afterwards).
- **Sessions:** a random cookie (`HttpOnly`, `Secure`, `SameSite=Strict`), ended by
  **Log out**, 30 minutes without use, 12 hours in all, or a server restart. The person
  icon in the header changes the password, which logs out every other browser.
- **Wrong passwords:** 5 from one address lock it out for 1 minute, then 2, 4, ... up to 15.
- **What needs the login:** everything except the page itself and `GET /me`: the data
  (`/snapshot`, `/history`, `/layout.json`), renaming, removing, revoking, saving the seat
  map. Requests that change something must be JSON, which other websites cannot send in
  the teacher's name.
- **Audit log:** `audit.log` next to the server gets one line per event, with the time and
  address: logins (and failed ones), log outs, password changes, renames, removals, revokes,
  seat-map saves, new join keys, and agents that joined, rotated their key or were refused.

## History and abnormal usage

The server keeps, for every computer, the last hour of CPU, download (rx) and upload (tx)
as 10-second averages, and learns what is normal for that computer:

- **Baseline:** an exponentially weighted moving average (EWMA) of the value and of its
  variance, with a 10-minute half-life. Samples that look abnormal are not added to it,
  so a spike cannot slowly become "normal".
- **Abnormal** when all three hold: z-score `(value - mean) / sd` above 3, the value is
  above a minimum (CPU 20 %, network 100 KB/s), and it lasts 5 samples in a row (about 5 s).
- **Learning period:** no alerts during the first 15 minutes of each computer.
- Saved to `history/<id>.jsonl` (one JSON line per 10 s with the averages and the baseline
  at that moment; the last day is kept). On restart the graphs and the exact baseline are
  restored, so learning does not start again. IDs that are not safe file names (letters, digits, `-`, `_`, `.`) stay in memory only.

The snapshot sent to dashboards gains two fields per machine (existing fields unchanged):
`abn` (e.g. `["rx"]`) and `learn` (true while still learning). `GET /history?id=<id>`
returns the graph data and baseline for a computer the server has seen.

Server options (`server` only; `server_gui` uses the defaults):

```
./build/server --learn-min 15 --history-dir history   # defaults
./build/server --no-history-files                     # keep history in memory only
```

Test: `tests/baseline_test.py` (see the docstring); results are in `docs/`.
## Managing computers

A computer is offline when it has sent nothing for 30 seconds. Its card says how long:
"No data for 45 s", "12 min", "3 h 5 min", "2 days 4 h" (the server window's Last
update column counts the same way).

On the server computer, every card has an edit button (pencil) that opens:

- **Display name:** shown instead of the computer ID on the cards, the seat map and the
  server window (hover the name there to see the ID). Leave it empty to show the ID again.
  The ID itself belongs to the agent (`--id`, or the hostname) and is what the seat map
  and the agent command use; it cannot be changed from the dashboard.
- **Description:** a note under the name, such as "screen flickers".
- **Remove from dashboard:** for an online or offline computer. It disappears from the
  dashboards and the server window, and the server ignores its data, until you press
  **Add back** (in the notice shown after removing, or under **Show removed computers** at the
  bottom of the page). Its history and baseline are kept.

Names, descriptions and removed computers are saved in `machines.json` next to the server
executable, so they survive a restart.

All of this needs the teacher login (see below). The dashboard uses `GET /me` and
`POST /meta`, `/forget`, `/restore`, `/revoke` with `{"id": "..."}`.

## Seat map

The dashboard can show the room as a seat map, each seat colored by status. The map is
the file `layout.json` **in the same folder as the server executable** (for example
`build/layout.json`).

### Editing in the dashboard

Open **Room map** and press **Create seat map** (or **Edit map** when one exists). The map
is made of blocks on a grid:

- **Seat:** one computer, one cell. Computers that are connected but not placed yet are
  listed beside the map; click one to add it, or type an ID to add a seat for a computer
  that is not on yet.
- **Label:** text on the map, any size, such as "aisle" or "teacher's desk". Gaps need no
  block, since any cell without a seat is already empty; a label only says what is there.
  Drag its bottom-right corner, or use Width / Height, to resize it.

Drag a block to move it, or select it and click the cell it should go to. With the
keyboard: arrows move the selected block, Shift+arrows resize a label, Delete
removes it. Columns and Rows change the size of the room. Blocks never overlap.

**Save** writes `layout.json` next to the server (teacher login). **Download file** gives
the same file to copy by hand. `run.sh` / `run.bat` copy a
`./layout.json` from the repository root only when it is newer, so a saved map is kept.

### File format

Written by the editor, or by hand (start from `layout.example.json`):

```
{
  "cols": 6,
  "rows": 4,
  "seats": {"A-01": [0, 0], "A-02": [1, 0], "B-01": [0, 1]},
  "labels": [{"c": 2, "r": 0, "w": 1, "h": 4, "text": "aisle"}]
}
```

- `seats`: each key is a computer's id (the agent's `--id`, or its hostname by default);
  each value is `[column, row]`, counted from 0. Row 0 is the front of the room.
- `rows` (optional): room depth, so empty back rows are kept. Up to 30 columns and 50 rows.
- `labels` (optional): text blocks at column `c`, row `r`, `w` cells wide and `h` deep;
  `text` is required (up to 40 characters).
- Seats with no computer reporting show grey and dashed; computers that are not in the
  file are listed under "ไม่อยู่ในผัง" (not in the layout).
- If the file is invalid (out of range, two blocks overlapping), the dashboard says so and
  the editor can make a new one. The server checks the same rules before saving.

The server only serves this one path (`/layout.json`, GET to read, POST to save); the file
is read again on every page load.

## Privacy
 
Intended for classrooms where users know the system is running. Screen viewing (when
implemented) happens only on request, shows an indicator on the student's computer, and
nothing is recorded.
 
## License
 
[MIT](LICENSE). Third-party libraries and their licenses are listed in
`THIRD_PARTY_NOTICES`.
