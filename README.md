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
- [x] Teacher dashboard: one card per computer (ok / warning / critical / offline)
- [x] Classroom seat map
- [ ] Stat history and per-machine baseline, to flag abnormal usage
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
 
## Build
 
Requirements: a C++17 compiler and CMake.
 
```
cmake -S . -B build #this section is optional but required to for first time you do this
cmake --build build #<--target <server/agent> (This section is optional: default is agent) >
./build/agent #or you can use ./build/server if you compiled server source code
```
 
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
