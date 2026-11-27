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
  Student PC (agent, C++)  -> WebSocket ->  Server (C++)  ->  Dashboard (web + Desktop app)
<measures CPU/RAM/network>          <keeps latest state + history>
```
 
## Project layout
 
```
agent/       measures statistics on each computer
server/      receives data from agents and serves the dashboard
server/gui/  native window for the server (Dear ImGui + GLFW)
dashboard/   web page for the teacher
docs/        API reference and test notes
```
 
## Quick start

One script builds everything, starts the server and opens the dashboard:

```
./run.sh            # Mac / Linux
run.bat             # Windows
```

Add `--agent` to also start an agent on the same computer (handy for testing without
other PCs). `./run.sh --help` lists the other options. Ctrl+C stops everything on
Mac / Linux; on Windows, close the server window.

To fill the dashboard without real PCs, start virtual agents in a second terminal:

```
./test.sh             # 12 agents A-01..B-06 + LAB-X, with warning, critical,
                      # offline and download-jump cases mixed in
./test.sh -n 30 --all-normal
```

They send the same JSON as the real agent and reconnect if the server restarts.
`./test.sh --help` lists all options (needs Python 3, no extra packages).

## Build
 
Requirements: a C++17 compiler and CMake.
 
```
cmake -S . -B build #this section is optional but required to for first time you do this
cmake --build build #<--target <server/agent> (This section is optional: default is agent) >
./build/agent #or you can use ./build/server if you compiled server source code
```

Targets: `agent`, `server` (console) and `server_gui` (window). Both servers use the same
core, so they behave the same. Run either one from the repository root so it can find
`dashboard/index.html`.

`server_gui` downloads GLFW, Dear ImGui and the Noto Sans / Noto Sans Thai fonts the first
time CMake configures (the fonts are compiled into the binary). On Linux it needs the
X11 or Wayland development packages; to skip it on a machine without a desktop:

```
cmake -S . -B build -DSERVER_GUI=OFF
```
 
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

These changes only work in a browser on the server computer itself (`localhost` or its own
LAN address): the dashboard has no login, and a student must not be able to hide or rename
their own PC. Other PCs see the names and descriptions but no edit buttons. The dashboard
uses `GET /me` and `POST /meta`, `/forget`, `/restore` with `{"id": "..."}`.

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

**Save** writes `layout.json` next to the server. To stop other PCs on the LAN from
changing the room, saving only works in a browser on the server computer itself. On any
other PC, use **Download file** and copy the file there. `run.sh` / `run.bat` copy a
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
