#!/bin/sh
# start virtual agents that send test metrics to a running server, so the dashboard
# has computers to show without real student pcs
#
#   ./test.sh                 12 agents (A-01..B-06) + LAB-X, with warning, critical,
#                             offline and download-jump cases mixed in
#   ./test.sh -n 30           30 agents
#   ./test.sh --all-normal    only normal agents
#   ./test.sh --host 192.168.1.20   send to a server on another computer
#
# start the server first (./run.sh in another terminal). ctrl+c stops the agents
# all options: ./test.sh --help
cd "$(dirname "$0")"
command -v python3 >/dev/null 2>&1 || { echo "python3 is not installed."; exit 1; }
exec python3 -u tests/virtual_agents.py "$@"
