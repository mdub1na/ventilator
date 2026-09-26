#!/bin/sh
set -eu

service=com.ventilator.helper-ipc.read-only
plist=com.ventilator.helper-ipc.read-only.plist

usage() {
    echo "Usage: $0 prepare | status APP | register APP | check APP | ui-crash APP | restart-before APP | restart-after APP | sleep-before APP | sleep-after APP | unregister APP | cleanup APP" >&2
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
    [ "$#" -eq 0 ] || usage
    (cd ../desktop-app && gradle createDistributable -Pcompose.desktop.packaging.checkJdkVendor=false)
    make daemon-status helper-status libhelper-probe.dylib
    scratch=$(mktemp -d "${TMPDIR:-/tmp}/ventilator-integrated-probe.XXXXXX")
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
    prepare) prepare "$@" ;;
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
        echo "probe package removed"
        ;;
    *) usage ;;
esac
