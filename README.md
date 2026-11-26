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
- [x] Send statistics to a central server over WebSocket
- [x] Teacher dashboard: one card per computer (ok / warning / critical / offline), Thai / English, light / dark
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

Server options:

```
./build/server --learn-min 15 --history-dir history   # defaults
./build/server --no-history-files                     # keep history in memory only
```

Test: `tests/baseline_test.py` (see the docstring); results are in `docs/`.
## Seat map

The dashboard can show the room as a seat map, each seat colored by status. Write a
`layout.json` and put it **in the same folder as the server executable** (for example
`build/layout.json`). Start from `layout.example.json`:

```
{"cols": 6, "seats": {"A-01": [0, 0], "A-02": [1, 0], "B-01": [0, 1]}}
```

- Each key is a computer's id (the agent's `--id`, or its hostname by default).
- Each value is `[column, row]`, counted from 0. Row 0 is the front of the room.
- Seats with no computer reporting show grey and dashed; computers that are not in the
  file are listed under "ไม่อยู่ในผัง" (not in the layout).
- Without the file, or if it is invalid (bad numbers, two computers on one seat), the
  dashboard stays in card view and says why.

The server only serves this one path (`/layout.json`); the file is read again on every
page load, so edits show after a refresh.

## Privacy
 
Intended for classrooms where users know the system is running. Screen viewing (when
implemented) happens only on request, shows an indicator on the student's computer, and
nothing is recorded.
 
## License
 
[MIT](LICENSE). Third-party libraries and their licenses are listed in
`THIRD_PARTY_NOTICES` (to be added in future (planned for websocket lib)). 
