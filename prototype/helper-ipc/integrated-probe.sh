#!/bin/sh
set -eu

service=com.ventilator.helper-ipc.read-only
plist=com.ventilator.helper-ipc.read-only.plist

usage() {
    echo "Usage: $0 prepare | prepare-reboot | status APP | register APP | check APP | startup-audit-crash-run APP | reboot-before APP | reboot-after APP | watch APP | watch-crash-run APP | watch-crash-before APP | watch-crash-after APP | ui-crash APP | restart-before APP | restart-after APP | sleep-before APP | sleep-after APP | unregister APP | cleanup APP" >&2
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

sleep_wakes() {
    pmset -g log | sed -n 's/^Total Sleep\/Wakes since boot .* :\([0-9][0-9]*\)$/\1/p' | tail -n 1
}

boot_epoch() {
    sysctl -n kern.boottime | sed -n 's/^{ sec = \([0-9][0-9]*\), usec = .*/\1/p'
}

is_uint() {
    case "$1" in ''|*[!0-9]*) return 1 ;; *) return 0 ;; esac
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
        temporary) probe_root=${TMPDIR:-/tmp} ;;
        persistent)
            probe_root=$PWD/.reboot-probes
            mkdir -p "$probe_root"
            chmod 700 "$probe_root"
            ;;
        *) usage ;;
    esac
    (cd ../desktop-app && gradle createDistributable -Pcompose.desktop.packaging.checkJdkVendor=false)
    make daemon-status helper-status libhelper-probe.dylib
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
        [ "$(ventilator --helper-registration-status)" = enabled ] || { echo "daemon is no longer enabled" >&2; exit 1; }
        audit=$(ventilator --helper-startup-audit)
        after=$(running_root_pid) || { echo "root daemon did not start after reboot" >&2; exit 1; }
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
        ventilator --helper-unregister
        after=$(ventilator --helper-registration-status)
        [ "$after" = notRegistered ] || [ "$after" = notFound ] || { echo "CRITICAL: registration=$after" >&2; exit 1; }
        service_absent || { echo "CRITICAL: system/$service still present" >&2; exit 1; }
        echo "daemon removed: $after; system-service=absent"
        ;;
    cleanup)
        require_app "$@"
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
