#!/bin/sh
set -eu

service=com.ventilator.helper-ipc.read-only
plist=com.ventilator.helper-ipc.read-only.plist

usage() {
    echo "Usage: $0 prepare | status APP | register APP | check APP | unregister APP | cleanup APP" >&2
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
        job=$(launchctl print "system/$service") || { echo "system service missing" >&2; exit 1; }
        pid=$(printf '%s\n' "$job" | sed -n 's/^[[:space:]]*pid = \([0-9][0-9]*\)$/\1/p' | head -n 1)
        [ -n "$pid" ] || { echo "system daemon has no PID" >&2; exit 1; }
        uid=$(ps -p "$pid" -o uid= | tr -d ' ')
        [ "$uid" = 0 ] || { echo "daemon is not running as root" >&2; exit 1; }
        echo "system-service=present uid=0"
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
