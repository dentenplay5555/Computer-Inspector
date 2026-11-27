#!/bin/sh
# build and start computer inspector on mac or linux
#
#   ./run.sh              build, start the server, open the dashboard in the browser
#   ./run.sh --agent      also start an agent on this computer (to test without other pcs)
#   ./run.sh --no-build   skip building (use what is already in build/)
#   ./run.sh --no-browser do not open the browser
#
# ctrl+c stops everything this script started
# if ./layout.json exists (seat map) and is newer, it is copied next to the server first
set -e
cd "$(dirname "$0")"

AGENT=0; BROWSER=1; BUILD=1
for a in "$@"; do
  case "$a" in
    --agent) AGENT=1 ;;
    --no-build) BUILD=0 ;;
    --no-browser) BROWSER=0 ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "Unknown option: $a (see ./run.sh --help)"; exit 1 ;;
  esac
done

if [ "$BUILD" = 1 ]; then
  command -v cmake >/dev/null 2>&1 || { echo "CMake is not installed. Install it (macOS: brew install cmake) and run again."; exit 1; }
  [ -d build ] || cmake -S . -B build
  cmake --build build --target server agent
fi
[ -x build/server ] || { echo "build/server not found. Run without --no-build first."; exit 1; }
# copy the seat map only when it is newer, so a map saved from the dashboard editor is kept
if [ -f layout.json ] && [ layout.json -nt build/layout.json ]; then cp layout.json build/layout.json; fi

PIDS=""
stop() {
  trap - INT TERM EXIT
  [ -n "$PIDS" ] && kill $PIDS 2>/dev/null
  wait 2>/dev/null
  echo "Stopped."
}
trap stop INT TERM EXIT

./build/server &
PIDS="$!"
sleep 1
if ! kill -0 "$PIDS" 2>/dev/null; then
  echo "The server stopped right away. Is another server already using port 8080 or 8081?"
  PIDS=""
  exit 1
fi

if [ "$AGENT" = 1 ]; then
  # same pc as the server: the pin and the join key come from the server's own files
  ./build/agent --host 127.0.0.1 --pin-file build/server.crt --join-key-file build/agents.json >/dev/null 2>&1 &
  PIDS="$PIDS $!"
fi

URL="https://127.0.0.1:8081"
if [ "$BROWSER" = 1 ]; then
  if command -v open >/dev/null 2>&1; then open "$URL"
  elif command -v xdg-open >/dev/null 2>&1; then xdg-open "$URL" >/dev/null 2>&1
  fi
fi
echo "Running. Dashboard on this computer: $URL (other PCs: see the address the server printed)."
echo "The browser warns about the certificate the first time: it is this server's own, accept it once."
echo "Ctrl+C to stop."
wait
