#!/bin/sh
set -eu

service=com.ventilator.helper-ipc.read-only
plist=com.ventilator.helper-ipc.read-only.plist

usage() {
    echo "Usage: $0 prepare | prepare-reboot | status APP | register APP | eager-check APP | check APP | supervisor-start APP | supervisor-crash-start APP | supervisor-resume APP | supervisor-crash-run APP | supervisor-client-loss-run APP | supervisor-client-pending-loss-run APP | supervisor-status APP | supervisor-cleanup APP | startup-audit-crash-run APP | reboot-before APP | reboot-after APP | watch APP | watch-crash-run APP | watch-crash-before APP | watch-crash-after APP | ui-crash APP | restart-before APP | restart-after APP | sleep-before APP | sleep-after APP | unregister APP | cleanup APP" >&2
    exit 2
}

require_app() {
    [ "$#" -eq 1 ] || usage
    app=$1
    [ "$(basename "$app")" = Ventilator.app ] || { echo "unexpected app name" >&2; exit 2; }
    case "$(basename "$(dirname "$app")")" in
        ventilator-integrated-probe.*) ;;
        *) echo "unexpected probe directory name" >&2; exit 2 ;;
    esac
    [ -f "$app/../.ventilator-integrated-probe" ] || { echo "probe marker missing" >&2; exit 2; }
    [ -x "$app/Contents/MacOS/Ventilator" ] || { echo "Ventilator launcher missing" >&2; exit 2; }
}

ventilator() {
    "$app/Contents/MacOS/Ventilator" "$1"
}

service_absent() {
    i=0
    while launchctl print "system/$service" >/dev/null 2>&1; do
        i=$((i + 1))
        [ "$i" -lt 30 ] || return 1
        sleep 0.1
    done
}

running_root_pid() {
    job=$(launchctl print "system/$service") || return 1
    pid=$(printf '%s\n' "$job" | sed -n 's/^[[:space:]]*pid = \([0-9][0-9]*\)$/\1/p' | head -n 1)
    [ -n "$pid" ] || return 1
    uid=$(ps -p "$pid" -o uid= | tr -d ' ')
    [ "$uid" = 0 ] || return 1
    command=$(ps -p "$pid" -o comm=)
    [ "$(basename "$command")" = daemon-status ] || return 1
    printf '%s\n' "$pid"
}

require_live_readonly_service() {
    registration=$(ventilator --helper-registration-status)
    [ "$registration" = enabled ] && return 0
    [ "$registration" = notFound ] || {
        echo "daemon is not enabled: $registration" >&2
        return 1
    }
    # After a Login Items approval, a new app process can report notFound
    # while the approved system job already answers signed XPC.
    reply=$(ventilator --helper-request) || return 1
    case "$reply" in
        *'"smc_access":true'*'"state":"read_only_prototype"'*'"write_available":false'*) ;;
        *) echo "live daemon did not confirm read-only status: $reply" >&2; return 1 ;;
    esac
    running_root_pid >/dev/null || { echo "live daemon is not UID 0" >&2; return 1; }
    echo "registration=$registration; live-read-only-daemon=verified" >&2
}

sleep_wakes() {
    pmset -g log | sed -n 's/^Total Sleep\/Wakes since boot .* :\([0-9][0-9]*\)$/\1/p' | tail -n 1
}

boot_epoch() {
    sysctl -n kern.boottime | sed -n 's/^{ sec = \([0-9][0-9]*\), usec = .*/\1/p'
}

is_uint() {
    case "$1" in ''|*[!0-9]*) return 1 ;; *) return 0 ;; esac
}

supervisor_terminal() {
    attempt=0
    while [ "$attempt" -lt 45 ]; do
        status=$(ventilator --helper-supervisor-status)
        case "$status" in *'"state":"running"'*) ;; *) return 0 ;; esac
        attempt=$((attempt + 1))
        sleep 2
    done
    echo "supervisor did not finish; retain the registered package and journal" >&2
    return 1
}

supervisor_identity() {
    pid=$(printf '%s\n' "$1" | sed -n 's/.*"runner_pid":\([0-9][0-9]*\).*/\1/p')
    is_uint "$pid" && [ "$pid" -gt 0 ] || { echo "runner PID missing" >&2; return 1; }
    [ "$(ps -p "$pid" -o uid= | tr -d ' ')" = 0 ] || { echo "runner is not UID 0" >&2; return 1; }
    [ "$(./helper-status process-path "$pid")" = /private/var/db/com.ventilator.supervisor-read-only/supervisor-executable-v1 ] || {
        echo "runner path differs or is unavailable" >&2; return 1;
    }
    echo "supervisor-process=verified uid=0 pid=$pid"
}

owned_client_identity() {
    [ "$(ps -p "$owned_client_pid" -o ppid= | tr -d ' ')" = "$$" ] &&
        [ "$(./helper-status process-path "$owned_client_pid")" = "$client_executable" ]
}

stop_owned_client() {
    if owned_client_identity; then
        kill -KILL "$owned_client_pid" 2>/dev/null || true
        wait "$owned_client_pid" 2>/dev/null || true
    fi
}

valid_identity() {
    cp ./daemon-status "$scratch/signing-check"
    for candidate in $(security find-identity -v -p codesigning | awk '/Apple Development:/ {print $2}'); do
        if codesign --force --sign "$candidate" --timestamp=none \
            --identifier com.ventilator.helper-ipc.signing-check "$scratch/signing-check" >/dev/null 2>&1 &&
           codesign --verify --strict "$scratch/signing-check" >/dev/null 2>&1; then
            identity=$candidate
            return 0
        fi
    done
    echo "no locally verifiable Apple Development identity" >&2
    return 1
}

prepare() {
    [ "$#" -eq 1 ] || usage
    case "$1" in
        temporary) probe_root=${TMPDIR:-/tmp}; run_at_load_plist= ;;
        persistent)
            probe_root=$PWD/.reboot-probes
            run_at_load_plist='  <key>RunAtLoad</key><true/>'
            mkdir -p "$probe_root"
            chmod 700 "$probe_root"
            ;;
        *) usage ;;
    esac
    (cd ../desktop-app && gradle createDistributable -Pcompose.desktop.packaging.checkJdkVendor=false)
    make daemon-status helper-status libhelper-probe.dylib supervisor-probe
    scratch=$(mktemp -d "$probe_root/ventilator-integrated-probe.XXXXXX")
    chmod 700 "$scratch"
    app="$scratch/Ventilator.app"
    trap 'echo "incomplete probe retained: $scratch" >&2' EXIT HUP INT TERM
    valid_identity
    ditto ../desktop-app/build/compose/binaries/main/app/Ventilator.app "$app"
    [ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app/Contents/Info.plist")" = ventilator.desktop ] || {
        echo "unexpected Ventilator bundle identifier" >&2
        exit 1
    }
    [ -f "$app/Contents/app/resources/libhelper-probe.dylib" ] || { echo "JNI bridge missing" >&2; exit 1; }
    mkdir -p "$app/Contents/Library/LaunchDaemons" "$app/Contents/Resources"
    cp ./daemon-status "$app/Contents/Resources/daemon-status"
    cp ./supervisor-probe "$app/Contents/Resources/supervisor-probe"
    codesign --force --sign "$identity" --timestamp=none \
        --identifier com.ventilator.helper-ipc.signed-supervisor "$app/Contents/Resources/supervisor-probe"
    codesign --force --sign "$identity" --timestamp=none \
        --identifier com.ventilator.helper-ipc.signed-daemon "$app/Contents/Resources/daemon-status"
    team=$(codesign -dv --verbose=4 "$app/Contents/Resources/daemon-status" 2>&1 | sed -n 's/^TeamIdentifier=//p')
    case "$team" in
        ??????????) case "$team" in *[!A-Za-z0-9]*) echo "invalid Team ID" >&2; exit 1;; esac ;;
        *) echo "missing Team ID" >&2; exit 1;;
    esac
    cat >"$app/Contents/Library/LaunchDaemons/$plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$service</string>
$run_at_load_plist
  <key>AssociatedBundleIdentifiers</key><string>ventilator.desktop</string>
  <key>BundleProgram</key><string>Contents/Resources/daemon-status</string>
  <key>ProgramArguments</key><array>
    <string>Contents/Resources/daemon-status</string>
    <string>$service</string><string>$team</string><string>ventilator.desktop</string>
  </array>
  <key>MachServices</key><dict><key>$service</key><true/></dict>
</dict></plist>
EOF
    plutil -lint "$app/Contents/Library/LaunchDaemons/$plist"
    codesign --force --deep --sign "$identity" --timestamp=none "$app"
    codesign --verify --strict --deep "$app"
    client_requirement="anchor apple generic and identifier \"ventilator.desktop\" and certificate leaf[subject.OU] = \"$team\""
    daemon_requirement="anchor apple generic and identifier \"com.ventilator.helper-ipc.signed-daemon\" and certificate leaf[subject.OU] = \"$team\""
    codesign -v -R="$client_requirement" "$app"
    codesign -v -R="$daemon_requirement" "$app/Contents/Resources/daemon-status"
    supervisor_requirement="anchor apple generic and identifier \"com.ventilator.helper-ipc.signed-supervisor\" and certificate leaf[subject.OU] = \"$team\""
    codesign -v -R="$supervisor_requirement" "$app/Contents/Resources/supervisor-probe"
    : >"$scratch/.ventilator-integrated-probe"
    trap - EXIT HUP INT TERM
    echo "prepared=$app"
    echo "registration=$(ventilator --helper-registration-status)"
    echo "cleanup command: $0 unregister '$app' && $0 cleanup '$app'"
}

[ "$#" -ge 1 ] || usage
action=$1
shift
case "$action" in
    prepare) [ "$#" -eq 0 ] || usage; prepare temporary ;;
    prepare-reboot) [ "$#" -eq 0 ] || usage; prepare persistent ;;
    status)
        require_app "$@"
        echo "registration=$(ventilator --helper-registration-status)"
        if launchctl print "system/$service" >/dev/null 2>&1; then echo "system-service=present"; else echo "system-service=absent"; fi
        ;;
    register)
        require_app "$@"
        before=$(ventilator --helper-registration-status)
        [ "$before" = notRegistered ] || [ "$before" = notFound ] || { echo "unexpected pre-registration status: $before" >&2; exit 1; }
        service_absent || { echo "system/$service is already present" >&2; exit 1; }
        ventilator --helper-register
        ;;
    eager-check)
        require_app "$@"
        before=$(running_root_pid) || { echo "root daemon is not running before XPC request" >&2; exit 1; }
        [ "$(ventilator --helper-registration-status)" = enabled ] || { echo "daemon is not enabled" >&2; exit 1; }
        audit=$(ventilator --helper-startup-audit)
        case "$audit" in *"\"daemon_pid\":$before"* ) ;; *) echo "startup audit PID differs: $audit" >&2; exit 1 ;; esac
        case "$audit" in *'"state":"system_at_start"'* ) ;; *) echo "startup audit is not system: $audit" >&2; exit 1 ;; esac
        case "$audit" in *'"control_allowed":false'* ) ;; *) echo "startup audit authorizes control: $audit" >&2; exit 1 ;; esac
        echo "eager-startup=verified uid=0 pid=$before"
        echo "eager-startup-audit=$audit"
        ;;
    check)
        require_app "$@"
        [ "$(ventilator --helper-registration-status)" = enabled ] || { echo "daemon is not enabled" >&2; exit 1; }
        ventilator --helper-request
        baseline=$(ventilator --helper-baseline)
        case "$baseline" in
            *'"available":true'*'"baseline":true'* ) ;;
            *) echo "root daemon did not confirm read-only system baseline: $baseline" >&2; exit 1 ;;
        esac
        echo "root-baseline=$baseline"
        audit=$(ventilator --helper-startup-audit)
        case "$audit" in *'"state":"system_at_start"'* ) ;; *) echo "root startup audit is not system: $audit" >&2; exit 1 ;; esac
        case "$audit" in *'"control_allowed":false'* ) ;; *) echo "startup audit authorizes control: $audit" >&2; exit 1 ;; esac
        echo "root-startup-audit=$audit"
        scratch=$(dirname "$app")
        make helper-status
        valid_identity
        cp ./helper-status "$scratch/foreign-signed-client"
        cp ./helper-status "$scratch/foreign-adhoc-client"
        codesign --force --sign "$identity" --timestamp=none \
            --identifier com.ventilator.helper-ipc.signed-client "$scratch/foreign-signed-client"
        codesign --force --sign - --identifier ventilator.desktop "$scratch/foreign-adhoc-client"
        team=$(codesign -dv --verbose=4 "$app" 2>&1 | sed -n 's/^TeamIdentifier=//p')
        if "$scratch/foreign-signed-client" request-signed "$service" "$team" >/dev/null 2>&1; then
            echo "CRITICAL: other signed identifier reached daemon" >&2
            exit 1
        fi
        echo "foreign-signed-client=rejected"
        if "$scratch/foreign-adhoc-client" request-signed "$service" "$team" >/dev/null 2>&1; then
            echo "CRITICAL: ad hoc client reached daemon" >&2
            exit 1
        fi
        echo "ad-hoc-client=rejected"
        ventilator --helper-request
        running_root_pid >/dev/null || { echo "system daemon is not running as root" >&2; exit 1; }
        echo "system-service=present uid=0"
        ;;
    supervisor-start|supervisor-crash-start|supervisor-resume)
        require_app "$@"
        make helper-status
        require_live_readonly_service
        case "$action" in
            supervisor-start) command=--helper-supervisor-start ;;
            supervisor-crash-start) command=--helper-supervisor-crash-start ;;
            supervisor-resume) command=--helper-supervisor-resume ;;
        esac
        owned_log=$(mktemp "${TMPDIR:-/tmp}/ventilator-supervisor-client.XXXXXX")
        ventilator "$command" >"$owned_log" 2>&1 &
        owned_client_pid=$!
        trap 'kill -TERM "$owned_client_pid" 2>/dev/null || true' EXIT
        attempt=0
        initial=
        while [ "$attempt" -lt 50 ]; do
            initial=$(head -n 1 "$owned_log")
            [ -z "$initial" ] || break
            kill -0 "$owned_client_pid" 2>/dev/null || break
            attempt=$((attempt + 1))
            sleep 0.2
        done
        case "$initial" in *'"state":"running"'*) ;; *) echo "supervisor did not start: $owned_log" >&2; exit 1 ;; esac
        if kill -0 "$owned_client_pid" 2>/dev/null; then
            supervisor_identity "$initial"
        elif [ "$action" != supervisor-resume ]; then
            echo "supervisor client ended before identity check: $owned_log" >&2; exit 1
        fi
        if ! wait "$owned_client_pid"; then
            echo "supervisor client failed; retain state and output: $owned_log" >&2; exit 1
        fi
        trap - EXIT
        status=$(tail -n 1 "$owned_log")
        rm "$owned_log"
        echo "$action=$status"
        case "$status" in *'"state":"running"'*) echo "supervisor client returned before completion" >&2; exit 1 ;; esac
        ;;
    supervisor-crash-run)
        require_app "$@"
        [ "$(ventilator --helper-registration-status)" = enabled ] || { echo "daemon is not enabled" >&2; exit 1; }
        [ ! -e /private/var/db/com.ventilator.supervisor-read-only ] || {
            echo "crash diagnostic requires absent prior probe state" >&2; exit 1;
        }
        make helper-status
        # Verify the new explicit resume cannot start an operation on clean state.
        raw=$(ventilator --helper-supervisor-resume)
        status=$(printf '%s\n' "$raw" | tail -n 1)
        supervisor_terminal
        echo "clean-resume=$status"
        python3 -I - "$status" <<'PY'
import json, sys
s = json.loads(sys.argv[1]); r = s.get('report', {})
assert s['state'] == 'finished' and r['state'] == 'blocked'
assert r['journal_clear'] is True and r['resumed'] is False
assert r['operation_pid'] == r['admission_pid'] == r['recovery_pid'] == r['observer_pid'] == r['reaped'] == 0
PY
        "$0" supervisor-cleanup "$app"
        "$0" supervisor-crash-start "$app"
        supervisor_terminal
        echo "supervisor-interrupted=$status"
        ids=$(python3 -I - "$status" <<'PY'
import json, sys
s = json.loads(sys.argv[1])
assert s['state'] == 'interrupted' and s['crash']['journal_pending'] is True
assert s['write_available'] is False and s['crash']['loss_duration_ns'] <= 2_000_000_000
print(s['runner_pid'], s['crash']['observer_pid'])
PY
        )
        set -- $ids
        old_runner=$1; old_observer=$2
        ./helper-status process-absent "$old_runner" && ./helper-status process-absent "$old_observer" || {
            echo "old runner or observer remains; recovery blocked" >&2; exit 1;
        }
        echo "old-probe-processes=absent runner=$old_runner observer=$old_observer"
        # A pending marker must not be deleted even after the old processes exit.
        status=$(ventilator --helper-supervisor-cleanup)
        supervisor_terminal
        echo "pending-cleanup=$status"
        case "$status" in *'"reason":"exit"'*'"state":"failed"'*) ;; *) echo "pending cleanup did not refuse" >&2; exit 1 ;; esac
        [ -d /private/var/db/com.ventilator.supervisor-read-only ] || { echo "pending state disappeared" >&2; exit 1; }
        "$0" supervisor-resume "$app"
        supervisor_terminal
        echo "supervisor-recovery=$status"
        python3 -I - "$status" "$old_runner" "$old_observer" <<'PY'
import json, sys
s = json.loads(sys.argv[1]); r = s.get('report', {})
assert s['state'] == 'finished' and r['state'] == 'verified' and r['resumed'] is True
assert r['journal_clear'] is True and r['reaped'] == 2 and r['runner_uid'] == 0
assert r['operation_pid'] == r['admission_pid'] == r['admission_samples'] == r['admission_duration_ns'] == 0
assert r['recovery_samples'] == 61 and r['recovery_duration_ns'] >= 60_000_000_000
old = {int(sys.argv[2]), int(sys.argv[3])}
assert all(r[key] not in old for key in ('runner_pid', 'recovery_pid', 'observer_pid'))
PY
        echo "supervisor-crash=recovered operation-repeated=false journal-clear=true"
        ;;
    supervisor-client-loss-run)
        require_app "$@"
        require_live_readonly_service
        [ ! -e /private/var/db/com.ventilator.supervisor-read-only ] || {
            echo "client loss requires absent prior probe state" >&2; exit 1;
        }
        make helper-status
        owner_log=$(mktemp "${TMPDIR:-/tmp}/ventilator-owned-client.XXXXXX")
        "$app/Contents/MacOS/Ventilator" --helper-supervisor-start >"$owner_log" 2>&1 &
        owned_client_pid=$!
        trap 'kill -KILL "$owned_client_pid" 2>/dev/null || true' EXIT
        attempt=0
        initial=
        while [ "$attempt" -lt 50 ]; do
            initial=$(head -n 1 "$owner_log")
            [ -z "$initial" ] || break
            kill -0 "$owned_client_pid" 2>/dev/null || break
            attempt=$((attempt + 1))
            sleep 0.2
        done
        case "$initial" in *'"state":"running"'*) ;; *) echo "owned client did not start: $owner_log" >&2; exit 1 ;; esac
        supervisor_identity "$initial"
        status=$(ventilator --helper-supervisor-status)
        python3 -I - "$initial" "$status" <<'PY'
import json, sys
a, b = map(json.loads, sys.argv[1:])
assert b['state'] == 'running' and b['daemon_pid'] == a['daemon_pid']
assert b['runner_pid'] == a['runner_pid'] and b['write_available'] is False
PY
        echo "other-connection=read-only owner-still-running"
        busy=$(ventilator --helper-supervisor-start)
        python3 -I - "$initial" "$busy" <<'PY'
import json, sys
a, b = map(json.loads, sys.argv[1:])
assert b['state'] == 'failed' and b['reason'] == 'busy'
assert b['daemon_pid'] == a['daemon_pid'] and b['runner_pid'] == a['runner_pid']
PY
        echo "other-connection=busy ownership-not-transferred"
        owned_runner=$(printf '%s\n' "$initial" | sed -n 's/.*"runner_pid":\([0-9][0-9]*\).*/\1/p')
        kill -KILL "$owned_client_pid"
        wait "$owned_client_pid" 2>/dev/null || true
        trap - EXIT
        attempt=0
        while [ "$attempt" -lt 100 ]; do
            status=$(ventilator --helper-supervisor-status)
            case "$status" in *'"reason":"owner_lost"'*'"state":"failed"'*) break ;; esac
            attempt=$((attempt + 1))
            sleep 0.1
        done
        echo "client-loss-final=$status"
        python3 -I - "$initial" "$status" <<'PY'
import json, sys
a, b = map(json.loads, sys.argv[1:])
assert b['state'] == 'failed' and b['reason'] == 'owner_lost'
assert b['daemon_pid'] == a['daemon_pid'] and b['runner_pid'] == a['runner_pid']
assert b['write_available'] is False
PY
        ./helper-status process-absent "$owned_runner"
        echo "client-loss=runner-exited owner-lost=true"
        rm "$owner_log"
        ;;
    supervisor-client-pending-loss-run)
        require_app "$@"
        require_live_readonly_service
        [ ! -e /private/var/db/com.ventilator.supervisor-read-only ] || {
            echo "pending client loss requires absent prior probe state" >&2; exit 1;
        }
        baseline=$(ventilator --helper-baseline)
        python3 -I - "$baseline" <<'PY'
import json, sys
b = json.loads(sys.argv[1])
assert b['available'] is True and b['baseline'] is True
assert b['Ftst'] == 0 and b['mode'] == [3, 3] and b['target_rpm'] == [0, 0]
assert b['write_available'] is False
PY
        echo "pending-client-baseline-before=$baseline"
        make helper-status
        client_executable=$(realpath "$app/Contents/MacOS/Ventilator")
        owner_log=$(mktemp "${TMPDIR:-/tmp}/ventilator-pending-client.XXXXXX")
        "$app/Contents/MacOS/Ventilator" --helper-supervisor-start >"$owner_log" 2>&1 &
        owned_client_pid=$!
        trap 'stop_owned_client' EXIT
        attempt=0
        initial=
        while [ "$attempt" -lt 50 ]; do
            initial=$(head -n 1 "$owner_log")
            [ -z "$initial" ] || break
            owned_client_identity || break
            attempt=$((attempt + 1))
            sleep 0.2
        done
        case "$initial" in *'"state":"running"'*) ;; *) echo "owned client did not start: $owner_log" >&2; exit 1 ;; esac
        owned_client_identity || { echo "owned client identity changed: $owner_log" >&2; exit 1; }
        supervisor_identity "$initial"
        old_runner=$(printf '%s\n' "$initial" | sed -n 's/.*"runner_pid":\([0-9][0-9]*\).*/\1/p')
        # The first baseline takes at least 60 seconds; an ordinary complete
        # run takes another minute. Terminal evidence below, not this delay,
        # proves that the pending journal existed before client termination.
        attempt=0
        while [ "$attempt" -lt 16 ]; do
            sleep 5
            owned_client_identity || { echo "owned client ended before the pending check: $owner_log" >&2; exit 1; }
            status=$(ventilator --helper-supervisor-status)
            python3 -I - "$initial" "$status" <<'PY'
import json, sys
a, b = map(json.loads, sys.argv[1:])
assert b['state'] == 'running' and b['daemon_pid'] == a['daemon_pid']
assert b['runner_pid'] == a['runner_pid'] and b['write_available'] is False
PY
            attempt=$((attempt + 1))
        done
        echo "pending-client=running-after-80-seconds daemon-and-runner-unchanged"
        owned_client_identity || { echo "owned client identity changed before termination" >&2; exit 1; }
        kill -KILL "$owned_client_pid"
        wait "$owned_client_pid" 2>/dev/null || true
        trap - EXIT
        attempt=0
        while [ "$attempt" -lt 100 ]; do
            status=$(ventilator --helper-supervisor-status)
            case "$status" in *'"reason":"owner_lost"'*'"state":"failed"'*) break ;; esac
            attempt=$((attempt + 1))
            sleep 0.1
        done
        echo "pending-client-loss=$status"
        python3 -I - "$initial" "$status" <<'PY'
import json, sys
a, b = map(json.loads, sys.argv[1:])
assert b['state'] == 'failed' and b['reason'] == 'owner_lost'
assert b['daemon_pid'] == a['daemon_pid'] and b['runner_pid'] == a['runner_pid']
assert b['write_available'] is False
PY
        ./helper-status process-absent "$old_runner"
        echo "old-runner=absent pid=$old_runner"
        status=$(ventilator --helper-supervisor-cleanup)
        supervisor_terminal
        echo "pending-cleanup=$status"
        case "$status" in *'"reason":"exit"'*'"state":"failed"'*) ;; *) echo "cleanup did not refuse; pending was not proved" >&2; exit 1 ;; esac
        [ -d /private/var/db/com.ventilator.supervisor-read-only ] || { echo "pending state disappeared" >&2; exit 1; }
        "$0" supervisor-resume "$app"
        supervisor_terminal
        echo "pending-client-recovery=$status"
        python3 -I - "$status" "$old_runner" <<'PY'
import json, sys
s = json.loads(sys.argv[1]); r = s.get('report', {})
assert s['state'] == 'finished' and r['state'] == 'verified' and r['resumed'] is True
assert s['write_available'] is False and r['journal_clear'] is True
assert r['runner_uid'] == 0 and r['reaped'] == 2
assert r['admission_pid'] == r['operation_pid'] == 0
assert r['admission_samples'] == r['admission_duration_ns'] == 0
assert r['recovery_samples'] == 61 and r['recovery_duration_ns'] >= 60_000_000_000
assert r['runner_pid'] != int(sys.argv[2])
assert len({r['runner_pid'], r['recovery_pid'], r['observer_pid']}) == 3
PY
        echo "pending-client-loss=recovered operation-repeated=false journal-clear=true"
        baseline=$(ventilator --helper-baseline)
        python3 -I - "$baseline" <<'PY'
import json, sys
b = json.loads(sys.argv[1])
assert b['available'] is True and b['baseline'] is True
assert b['Ftst'] == 0 and b['mode'] == [3, 3] and b['target_rpm'] == [0, 0]
assert b['write_available'] is False
PY
        echo "pending-client-baseline-after=$baseline"
        rm "$owner_log"
        ;;
    supervisor-status)
        require_app "$@"
        ventilator --helper-supervisor-status
        ;;
    supervisor-cleanup)
        require_app "$@"
        status=$(ventilator --helper-supervisor-cleanup)
        attempt=0
        while [ "$attempt" -lt 10 ]; do
            case "$status" in
                *'"state":"cleaned"'*) echo "supervisor-cleanup=$status"; exit 0 ;;
                *'"state":"running"'*) ;;
                *) echo "supervisor state was not cleaned: $status" >&2; exit 1 ;;
            esac
            sleep 0.2
            status=$(ventilator --helper-supervisor-status)
            attempt=$((attempt + 1))
        done
        echo "supervisor still running; wait for completion before unregister" >&2
        exit 1
        ;;
    startup-audit-crash-run)
        require_app "$@"
        [ "$(ventilator --helper-registration-status)" = enabled ] || { echo "daemon is not enabled" >&2; exit 1; }
        sudo -v
        before_audit=$(ventilator --helper-startup-audit)
        before=$(running_root_pid) || { echo "root daemon did not start" >&2; exit 1; }
        case "$before_audit" in *"\"daemon_pid\":$before"* ) ;; *) echo "startup audit PID mismatch: $before_audit" >&2; exit 1 ;; esac
        case "$before_audit" in *'"state":"system_at_start"'* ) ;; *) echo "startup state is not system: $before_audit" >&2; exit 1 ;; esac
        before_time=$(printf '%s\n' "$before_audit" | sed -n 's/.*"sample_monotonic_ns":\([0-9][0-9]*\).*/\1/p')
        is_uint "$before_time" || { echo "startup sample time missing" >&2; exit 1; }
        echo "startup-audit-before=$before_audit"
        sudo -n launchctl kill SIGKILL "system/$service"
        attempt=0
        while [ "$attempt" -lt 8 ]; do
            if after_audit=$(ventilator --helper-startup-audit 2>/dev/null); then
                after=$(running_root_pid) || { echo "audit answered without root daemon" >&2; exit 1; }
                if [ "$after" != "$before" ]; then
                    case "$after_audit" in *"\"daemon_pid\":$after"* ) ;; *) echo "new startup audit PID mismatch: $after_audit" >&2; exit 1 ;; esac
                    case "$after_audit" in *'"state":"system_at_start"'* ) ;; *) echo "new startup state is not system: $after_audit" >&2; exit 1 ;; esac
                    case "$after_audit" in *'"control_allowed":false'* ) ;; *) echo "new startup audit authorizes control: $after_audit" >&2; exit 1 ;; esac
                    after_time=$(printf '%s\n' "$after_audit" | sed -n 's/.*"sample_monotonic_ns":\([0-9][0-9]*\).*/\1/p')
                    is_uint "$after_time" && [ "$after_time" -gt "$before_time" ] || { echo "new daemon did not take a later startup sample" >&2; exit 1; }
                    echo "startup-audit-after=$after_audit"
                    echo "startup-audit-crash=recovered old-pid=$before new-pid=$after"
                    exit 0
                fi
            fi
            attempt=$((attempt + 1))
            sleep 1
        done
        echo "new daemon did not answer with a later startup audit" >&2
        exit 1
        ;;
    reboot-before)
        require_app "$@"
        case "$app" in "$PWD/.reboot-probes/"*) ;; *) echo "reboot probe must use prepare-reboot" >&2; exit 1 ;; esac
        [ "$(ventilator --helper-registration-status)" = enabled ] || { echo "daemon is not enabled" >&2; exit 1; }
        marker="$app/../.reboot-baseline"
        [ ! -e "$marker" ] || { echo "reboot baseline already exists" >&2; exit 1; }
        audit=$(ventilator --helper-startup-audit)
        before=$(running_root_pid) || { echo "root daemon did not start" >&2; exit 1; }
        case "$audit" in *"\"daemon_pid\":$before"* ) ;; *) echo "startup audit PID differs: $audit" >&2; exit 1 ;; esac
        case "$audit" in *'"state":"system_at_start"'* ) ;; *) echo "startup audit is not system: $audit" >&2; exit 1 ;; esac
        case "$audit" in *'"control_allowed":false'* ) ;; *) echo "startup audit authorizes control: $audit" >&2; exit 1 ;; esac
        boot=$(boot_epoch)
        sampled_at=$(printf '%s\n' "$audit" | sed -n 's/.*"sample_monotonic_ns":\([0-9][0-9]*\).*/\1/p')
        is_uint "$boot" && is_uint "$sampled_at" || { echo "boot or sample time missing" >&2; exit 1; }
        printf '%s %s %s\n' "$boot" "$before" "$sampled_at" >"$marker"
        echo "reboot-audit-before=$audit"
        echo "reboot-baseline-recorded boot=$boot daemon-pid=$before; reboot Mac, then run reboot-after"
        ;;
    reboot-after)
        require_app "$@"
        marker="$app/../.reboot-baseline"
        [ -f "$marker" ] || { echo "run reboot-before first" >&2; exit 1; }
        read -r before_boot before_pid before_sample <"$marker"
        boot=$(boot_epoch)
        is_uint "$before_boot" && is_uint "$before_pid" && is_uint "$before_sample" && is_uint "$boot" || {
            echo "invalid reboot baseline or boot time" >&2
            exit 1
        }
        [ "$boot" -gt "$before_boot" ] || { echo "Mac has not rebooted since reboot-before" >&2; exit 1; }
        after=$(running_root_pid) || { echo "root daemon did not start before the first XPC request after reboot" >&2; exit 1; }
        [ "$(ventilator --helper-registration-status)" = enabled ] || { echo "daemon is no longer enabled" >&2; exit 1; }
        audit=$(ventilator --helper-startup-audit)
        case "$audit" in *"\"daemon_pid\":$after"* ) ;; *) echo "new startup audit PID differs: $audit" >&2; exit 1 ;; esac
        case "$audit" in *'"state":"system_at_start"'* ) ;; *) echo "new startup audit is not system: $audit" >&2; exit 1 ;; esac
        case "$audit" in *'"control_allowed":false'* ) ;; *) echo "new startup audit authorizes control: $audit" >&2; exit 1 ;; esac
        sampled_at=$(printf '%s\n' "$audit" | sed -n 's/.*"sample_monotonic_ns":\([0-9][0-9]*\).*/\1/p')
        is_uint "$sampled_at" && [ "$sampled_at" -gt 0 ] || { echo "new startup sample time missing" >&2; exit 1; }
        baseline=$(ventilator --helper-baseline)
        case "$baseline" in *'"available":true'*'"baseline":true'* ) ;; *) echo "fresh root baseline is not system: $baseline" >&2; exit 1 ;; esac
        rm "$marker"
        echo "reboot-audit-after=$audit"
        echo "reboot-baseline-after=$baseline"
        echo "reboot-audit=verified old-boot=$before_boot new-boot=$boot old-pid=$before_pid new-pid=$after uid=0"
        ;;
    watch)
        require_app "$@"
        [ "$(ventilator --helper-registration-status)" = enabled ] || { echo "daemon is not enabled" >&2; exit 1; }
        start=$(ventilator --helper-watch-start)
        before=$(running_root_pid) || { echo "root daemon did not start" >&2; exit 1; }
        case "$start" in *'"state":"running"'* ) ;; *) echo "watch did not start: $start" >&2; exit 1 ;; esac
        case "$start" in *'"samples":1'* ) ;; *) echo "watch did not take first sample: $start" >&2; exit 1 ;; esac
        case "$start" in *"\"daemon_pid\":$before"* ) ;; *) echo "watch started in another daemon: $start" >&2; exit 1 ;; esac
        echo "watch-start=$start"
        sleep 60
        attempt=0
        while :; do
            final=$(ventilator --helper-watch-status)
            case "$final" in
                *'"state":"stable"'* ) break ;;
                *'"state":"running"'* )
                    [ "$attempt" -lt 15 ] || { echo "watch did not finish in bounded time: $final" >&2; exit 1; }
                    attempt=$((attempt + 1))
                    sleep 2
                    ;;
                *) echo "watch stopped before stable completion: $final" >&2; exit 1 ;;
            esac
        done
        case "$final" in *'"samples":61'* ) ;; *) echo "watch missed samples: $final" >&2; exit 1 ;; esac
        case "$final" in *'"last_second":60'* ) ;; *) echo "watch missed final second: $final" >&2; exit 1 ;; esac
        case "$final" in *"\"daemon_pid\":$before"* ) ;; *) echo "daemon restarted during watch: $final" >&2; exit 1 ;; esac
        after=$(running_root_pid) || { echo "root daemon disappeared" >&2; exit 1; }
        [ "$after" = "$before" ] || { echo "root daemon PID changed during watch" >&2; exit 1; }
        echo "watch-final=$final"
        echo "watch=stable samples=61 daemon-persisted=true"
        ;;
    watch-crash-before)
        require_app "$@"
        [ "$(ventilator --helper-registration-status)" = enabled ] || { echo "daemon is not enabled" >&2; exit 1; }
        marker="$app/../.watch-crash-baseline"
        [ ! -e "$marker" ] || { echo "watch crash marker already exists" >&2; exit 1; }
        started=$(ventilator --helper-watch-start)
        before=$(running_root_pid) || { echo "root daemon did not start" >&2; exit 1; }
        case "$started" in *'"state":"running"'* ) ;; *) echo "watch did not start: $started" >&2; exit 1 ;; esac
        case "$started" in *'"samples":1'* ) ;; *) echo "watch missed first sample: $started" >&2; exit 1 ;; esac
        case "$started" in *"\"daemon_pid\":$before"* ) ;; *) echo "watch started in another daemon: $started" >&2; exit 1 ;; esac
        start_time=$(printf '%s\n' "$started" | sed -n 's/.*"started_monotonic_ns":\([0-9][0-9]*\).*/\1/p')
        is_uint "$start_time" || { echo "watch start time missing" >&2; exit 1; }
        printf '%s %s\n' "$before" "$start_time" >"$marker"
        echo "watch-crash-baseline=$started"
        echo "run: sudo launchctl kill SIGKILL system/$service"
        ;;
    watch-crash-run)
        require_app "$@"
        [ "$(ventilator --helper-registration-status)" = enabled ] || { echo "daemon is not enabled" >&2; exit 1; }
        sudo -v
        "$0" watch-crash-before "$app"
        pre_kill=$(ventilator --helper-watch-status)
        case "$pre_kill" in *'"state":"running"'* ) ;; *) echo "watch finished before crash: $pre_kill" >&2; exit 1 ;; esac
        read -r before start_time <"$app/../.watch-crash-baseline"
        case "$pre_kill" in *"\"daemon_pid\":$before"* ) ;; *) echo "watch moved to another daemon: $pre_kill" >&2; exit 1 ;; esac
        echo "watch-pre-kill=$pre_kill"
        sudo -n launchctl kill SIGKILL "system/$service"
        "$0" watch-crash-after "$app"
        ;;
    watch-crash-after)
        require_app "$@"
        marker="$app/../.watch-crash-baseline"
        [ -f "$marker" ] || { echo "run watch-crash-before first" >&2; exit 1; }
        read -r before start_time <"$marker"
        is_uint "$before" && is_uint "$start_time" || { echo "invalid watch crash marker" >&2; exit 1; }
        attempt=0
        while [ "$attempt" -lt 8 ]; do
            if status=$(ventilator --helper-watch-status 2>/dev/null); then
                after=$(running_root_pid) || { echo "watch status answered without root daemon" >&2; exit 1; }
                if [ "$after" != "$before" ]; then
                    case "$status" in *'"state":"idle"'* ) ;; *) echo "restarted daemon retained or misreported watch: $status" >&2; exit 1 ;; esac
                    case "$status" in *'"samples":0'* ) ;; *) echo "restarted daemon retained samples: $status" >&2; exit 1 ;; esac
                    case "$status" in *"\"daemon_pid\":$after"* ) ;; *) echo "watch status PID mismatch: $status" >&2; exit 1 ;; esac
                    [ "$(ventilator --helper-registration-status)" = enabled ] || { echo "registration lost after daemon restart" >&2; exit 1; }
                    echo "old-watch-lost=$status"
                    "$0" watch "$app"
                    rm "$marker"
                    echo "watch-crash=recovered old-pid=$before new-pid=$after old-start=$start_time"
                    exit 0
                fi
            fi
            attempt=$((attempt + 1))
            sleep 1
        done
        echo "new daemon did not answer with a different PID; run the sudo command or keep package for unregister" >&2
        exit 1
        ;;
    ui-crash)
        require_app "$@"
        [ "$(ventilator --helper-registration-status)" = enabled ] || { echo "daemon is not enabled" >&2; exit 1; }
        ventilator --helper-request >/dev/null
        before=$(running_root_pid) || { echo "root daemon is not running" >&2; exit 1; }
        scratch=$(dirname "$app")
        ui_pid=
        tray_pid=
        cleanup_ui() {
            result=$?
            trap - EXIT HUP INT TERM
            if [ -n "$ui_pid" ] && kill -0 "$ui_pid" 2>/dev/null; then
                kill -KILL "$ui_pid" 2>/dev/null || true
                wait "$ui_pid" 2>/dev/null || true
            fi
            if [ -n "$tray_pid" ] && kill -0 "$tray_pid" 2>/dev/null; then
                kill "$tray_pid" 2>/dev/null || true
            fi
            exit "$result"
        }
        trap cleanup_ui EXIT HUP INT TERM
        "$app/Contents/MacOS/Ventilator" >"$scratch/.ui-crash-output" 2>&1 &
        ui_pid=$!
        attempt=0
        while [ "$attempt" -lt 100 ]; do
            kill -0 "$ui_pid" 2>/dev/null || { echo "temporary UI exited before tray started" >&2; exit 1; }
            tray_pid=$(pgrep -P "$ui_pid" -f 'status-item-bridge' | head -n 1 || true)
            [ -z "$tray_pid" ] || break
            attempt=$((attempt + 1))
            sleep 0.1
        done
        [ -n "$tray_pid" ] || { echo "temporary UI did not start tray bridge" >&2; exit 1; }
        echo "temporary-ui=$ui_pid tray=$tray_pid root-daemon=$before"
        kill -KILL "$ui_pid"
        wait "$ui_pid" 2>/dev/null || true
        ui_pid=
        attempt=0
        while kill -0 "$tray_pid" 2>/dev/null; do
            attempt=$((attempt + 1))
            [ "$attempt" -lt 50 ] || { echo "tray bridge survived UI crash" >&2; exit 1; }
            sleep 0.1
        done
        tray_pid=
        after=$(running_root_pid) || { echo "root daemon disappeared after UI crash" >&2; exit 1; }
        [ "$after" = "$before" ] || { echo "root daemon restarted after UI crash" >&2; exit 1; }
        ventilator --helper-request
        echo "ui-crash=isolated tray=exited root-daemon=unchanged trusted-request=accepted"
        rm -f "$scratch/.ui-crash-output"
        ;;
    restart-before)
        require_app "$@"
        [ "$(ventilator --helper-registration-status)" = enabled ] || { echo "daemon is not enabled" >&2; exit 1; }
        ventilator --helper-request >/dev/null
        before=$(running_root_pid) || { echo "root daemon is not running" >&2; exit 1; }
        printf '%s\n' "$before" >"$app/../.restart-baseline"
        echo "restart-baseline-recorded; run: sudo launchctl kill SIGKILL system/$service"
        ;;
    restart-after)
        require_app "$@"
        marker="$app/../.restart-baseline"
        [ -f "$marker" ] || { echo "run restart-before first" >&2; exit 1; }
        read -r before <"$marker"
        is_uint "$before" || { echo "invalid daemon PID baseline" >&2; exit 1; }
        attempt=0
        while [ "$attempt" -lt 8 ]; do
            if ventilator --helper-request >/dev/null 2>&1; then
                after=$(running_root_pid) || { echo "request returned but root daemon is absent" >&2; exit 1; }
                [ "$after" != "$before" ] || { echo "daemon PID did not change; run the sudo launchctl kill command" >&2; exit 1; }
                [ "$(ventilator --helper-registration-status)" = enabled ] || {
                    echo "daemon restarted but registration is no longer enabled" >&2
                    exit 1
                }
                rm "$marker"
                echo "daemon-restarted=true uid=0; trusted-request=accepted"
                exit 0
            fi
            attempt=$((attempt + 1))
            sleep 1
        done
        echo "read-only daemon did not answer after SIGKILL; run unregister before cleanup" >&2
        exit 1
        ;;
    sleep-before)
        require_app "$@"
        [ "$(ventilator --helper-registration-status)" = enabled ] || { echo "daemon is not enabled" >&2; exit 1; }
        ventilator --helper-request >/dev/null
        running_root_pid >/dev/null || { echo "root daemon is not running" >&2; exit 1; }
        boot=$(boot_epoch)
        count=$(sleep_wakes)
        is_uint "$boot" && is_uint "$count" || { echo "could not read boot time or sleep/wake count" >&2; exit 1; }
        printf '%s %s\n' "$boot" "$count" >"$app/../.sleep-baseline"
        echo "sleep-baseline-recorded; now sleep and wake the Mac, then run sleep-after"
        ;;
    sleep-after)
        require_app "$@"
        marker="$app/../.sleep-baseline"
        [ -f "$marker" ] || { echo "run sleep-before first" >&2; exit 1; }
        read -r before_boot before_count <"$marker"
        boot=$(boot_epoch)
        count=$(sleep_wakes)
        is_uint "$boot" && is_uint "$count" && is_uint "$before_boot" && is_uint "$before_count" || {
            echo "invalid boot time or sleep/wake count" >&2
            exit 1
        }
        [ "$boot" = "$before_boot" ] || { echo "Mac rebooted instead of remaining in the same boot session" >&2; exit 1; }
        [ "$count" -gt "$before_count" ] || { echo "no new sleep/wake cycle observed" >&2; exit 1; }
        [ "$(ventilator --helper-registration-status)" = enabled ] || { echo "daemon is no longer enabled" >&2; exit 1; }
        ventilator --helper-request
        running_root_pid >/dev/null || { echo "root daemon is not running after wake" >&2; exit 1; }
        rm "$marker"
        echo "sleep-wake-observed=true; trusted-request=accepted; uid=0"
        ;;
    unregister)
        require_app "$@"
        if [ "$(ventilator --helper-registration-status)" = enabled ]; then
            supervisor=$(ventilator --helper-supervisor-status)
            case "$supervisor" in
                *'"state":"idle"'*|*'"state":"cleaned"'*) ;;
                *) echo "run supervisor-cleanup and confirm cleaned before unregister: $supervisor" >&2; exit 1 ;;
            esac
        fi
        [ ! -d /private/var/db/com.ventilator.supervisor-read-only ] || {
            echo "root probe state remains; enable the daemon and run supervisor-cleanup before unregister" >&2
            exit 1
        }
        ventilator --helper-unregister
        after=$(ventilator --helper-registration-status)
        [ "$after" = notRegistered ] || [ "$after" = notFound ] || { echo "CRITICAL: registration=$after" >&2; exit 1; }
        service_absent || { echo "CRITICAL: system/$service still present" >&2; exit 1; }
        echo "daemon removed: $after; system-service=absent"
        ;;
    cleanup)
        require_app "$@"
        [ ! -d /private/var/db/com.ventilator.supervisor-read-only ] || {
            echo "root probe state remains; use supervisor-cleanup before deleting the package" >&2
            exit 1
        }
        after=$(ventilator --helper-registration-status)
        [ "$after" = notRegistered ] || [ "$after" = notFound ] || { echo "unregister daemon before cleanup: $after" >&2; exit 1; }
        service_absent || { echo "system service still present" >&2; exit 1; }
        scratch=$(dirname "$app")
        rm -rf "$scratch"
        case "$scratch" in "$PWD/.reboot-probes/"*) rmdir "$PWD/.reboot-probes" 2>/dev/null || true ;; esac
        echo "probe package removed"
        ;;
    *) usage ;;
esac
