#!/bin/sh
set -eu

[ "$#" -eq 1 ] || { echo "Usage: $0 PREPARED_INTEGRATED_APP" >&2; exit 2; }
[ "$(id -u)" -ne 0 ] || { echo "run as the signed app's user, not root" >&2; exit 2; }
[ "$(sysctl -n hw.model)" = Mac15,7 ] && [ "$(sw_vers -productVersion)" = 27.0 ] || {
    echo "this probe is limited to Mac15,7 on macOS 27.0" >&2
    exit 2
}
app=$1
scratch=$(dirname "$app")
[ "$(basename "$app")" = Ventilator.app ] &&
    [ -f "$scratch/.ventilator-integrated-probe" ] || {
    echo "expected an integrated-probe.sh prepare package" >&2
    exit 2
}

service=com.ventilator.helper-ipc.read-only
launcher="$app/Contents/MacOS/Ventilator"
output="$scratch/.root-inflight-output"
[ -x "$launcher" ] || { echo "Ventilator launcher missing" >&2; exit 2; }
[ "$("$launcher" --helper-registration-status)" = enabled ] || {
    echo "read-only root daemon is not enabled" >&2
    exit 1
}

root_pid() {
    job=$(launchctl print "system/$service") || return 1
    pid=$(printf '%s\n' "$job" | sed -n 's/^[[:space:]]*pid = \([0-9][0-9]*\)$/\1/p' | head -n 1)
    [ -n "$pid" ] || return 1
    [ "$(ps -p "$pid" -o uid= | tr -d ' ')" = 0 ] || return 1
    [ "$(basename "$(ps -p "$pid" -o comm=)")" = daemon-status ] || return 1
    printf '%s\n' "$pid"
}

"$launcher" --helper-request >/dev/null
before=$(root_pid) || { echo "expected running UID 0 daemon-status" >&2; exit 1; }
echo "operation=root-inflight-read-only daemon=$before"
echo "Emergency resume in a second Terminal: sudo launchctl kill SIGCONT system/$service"
echo "sudo may ask for your macOS administrator password in this Terminal."
sudo -v

stopped=0
request_pid=
cleanup() {
    result=$?
    trap - EXIT HUP INT TERM
    if [ "$stopped" -eq 1 ] &&
       [ "$(ps -p "$before" -o stat= 2>/dev/null | cut -c 1)" = T ] &&
       [ "$(basename "$(ps -p "$before" -o comm=)")" = daemon-status ]; then
        if ! sudo -n /bin/kill -CONT "$before"; then
            echo "CRITICAL: could not resume test daemon PID $before; resume or reboot before cleanup" >&2
            exit 1
        fi
        sleep 0.1
        if [ "$(ps -p "$before" -o stat= 2>/dev/null | cut -c 1)" = T ]; then
            echo "CRITICAL: test daemon PID $before is still stopped; resume or reboot before cleanup" >&2
            exit 1
        fi
    fi
    if [ -n "$request_pid" ] && kill -0 "$request_pid" 2>/dev/null; then
        kill "$request_pid" 2>/dev/null || true
        wait "$request_pid" 2>/dev/null || true
    fi
    rm -f "$output"
    exit "$result"
}
trap cleanup EXIT HUP INT TERM

sudo -n launchctl kill SIGSTOP "system/$service"
stopped=1
attempt=0
while [ "$(ps -p "$before" -o stat= 2>/dev/null | cut -c 1)" != T ]; do
    attempt=$((attempt + 1))
    [ "$attempt" -lt 20 ] || { echo "daemon did not enter stopped state" >&2; exit 1; }
    sleep 0.1
done

"$launcher" --helper-request >"$output" 2>&1 &
request_pid=$!
sleep 0.5
request_state=$(ps -p "$request_pid" -o stat= 2>/dev/null | cut -c 1)
case "$request_state" in
    ''|Z)
        echo "request exited before daemon interruption" >&2
        cat "$output" >&2
        exit 1
        ;;
esac
echo "request=pending daemon=stopped"
sudo -n launchctl kill SIGKILL "system/$service"
stopped=0
if wait "$request_pid"; then
    echo "TEST FAILED: interrupted request accepted a status" >&2
    exit 1
fi
request_pid=
if grep -q '"protocol_version"' "$output"; then
    echo "TEST FAILED: failed request printed a status" >&2
    exit 1
fi
cat "$output"
echo "interrupted-root-request=failed-closed"

recovery=automatic
answered=0
for attempt in 1 2 3; do
    if "$launcher" --helper-request; then
        answered=1
        break
    fi
    sleep 1
done
if [ "$answered" -eq 0 ]; then
    recovery=kickstart
    sudo -n launchctl kickstart -k "system/$service" >/dev/null
    "$launcher" --helper-request
fi
after=$(root_pid) || { echo "root daemon did not recover" >&2; exit 1; }
[ "$after" != "$before" ] || { echo "daemon PID did not change" >&2; exit 1; }
[ "$("$launcher" --helper-registration-status)" = enabled ] || {
    echo "daemon registration is no longer enabled" >&2
    exit 1
}
echo "new-root-request=accepted recovery=$recovery old-pid=$before new-pid=$after uid=0"
