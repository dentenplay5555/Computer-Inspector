# Baseline / anomaly detection test (2026-11-26)

Script: `tests/baseline_test.py` (Python standard library only). It sends fake metrics
for three machines once a second and listens on the dashboard WebSocket to record when
the server marks a machine abnormal (`abn` field of the snapshot).

| Machine | Traffic | Expected |
|---|---|---|
| NORM-A | Gaussian noise: CPU 25 ± 6 %, rx 80 ± 25 KB/s, tx 20 ± 8 KB/s | never alert |
| NORM-B | same noise + random 1-3 s bursts (CPU 60-90 %, rx 500-1500 KB/s) about once a minute | never alert |
| JUMP-C | same noise, then rx jumps to about 2000 KB/s | alert within seconds of the jump |

Detection settings (server defaults): z-score > 3, value above 20 % CPU / 100 KB/s network,
5 samples in a row; baseline learned with an exact running mean/variance during the
learning period, then EWMA with a 10-minute half-life.

## Test 1: 15 minutes normal, then a jump

`./build/server --learn-min 10`, then
`python3 tests/baseline_test.py --jump-at 900 --after 120`

| Result | Value |
|---|---|
| Samples sent | 3060 (3 machines x 1020 s) |
| Learning ended | 600.6 s (all machines) |
| False alerts, NORM-A (whole run) | 0 |
| False alerts, NORM-B (whole run, including bursts) | 0 |
| False alerts, JUMP-C before the jump | 0 |
| Jump at | 900 s |
| Jump detected at | 905.0 s |
| **Detection delay** | **5.0 s** |

Raw data: `baseline-test-2026-11-26.json`.

## Test 2: restart keeps the baseline

Learn 1 minute and run, restart the server on the same history folder, then run again
(normal 120 s, then a jump).

| Result | Before restart | After restart |
|---|---|---|
| Learning ended | 60.2 s | **0.9 s** (baseline restored, no relearning) |
| False alerts NORM-A / NORM-B / JUMP-C before jump | 0 / 0 / 0 | 0 / 0 / 0 |
| Jump at → detected at | 170 s → 174.7 s | 120 s → 124.4 s |
| Detection delay | 4.7 s | 4.4 s |

Baseline of NORM-A read from `/history` right after the restart: CPU mean 24.5 / sd 5.4,
rx 82.2 / 24.6, tx 20.9 / 8.4. These match the generated traffic (25 ± 6, 80 ± 25, 20 ± 8).

Raw data: `baseline-restart-test-2026-11-26.json`.

## Problems found while testing (and fixed)

1. **Baseline rebuilt from 10 s averages after a restart.** Averaging hides most of the
   per-second spread (CPU sd came back as 1.6 instead of about 6), which makes normal
   noise look abnormal. Fix: each saved line stores the baseline itself (mean, variance,
   sample count) and when learning began; a restart restores it exactly.
2. **EWMA started from zero spread.** With a 10-minute half-life it only reaches about
   27 % of the real standard deviation after 1 minute and about 70 % after 10 minutes,
   so a short learning period produced false alerts (NORM-A 1-3, NORM-B 2 in a 1-minute
   learning run). Fix: learn with an exact running mean/variance (Welford), then switch
   to EWMA.

## Limits

- A machine whose normal use changes for good (for example a new program running all day)
  stays flagged, because abnormal samples do not update the baseline. Planned extension:
  a separate baseline per hour of the day.
- Only increases are flagged (a machine using more than normal), not drops.
- Test traffic is synthetic Gaussian noise; real classroom traffic is burstier.
