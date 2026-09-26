#!/bin/sh
set -eu

service=com.ventilator.helper-ipc.signed-daemon-test
plist=com.ventilator.helper-ipc.signed-daemon-test.plist
bundle_id=com.ventilator.helper-ipc.signed-daemon-test

usage() {
    echo "Usage: $0 prepare | status APP | register APP | check APP | unregister APP | cleanup APP" >&2
    exit 2
}

require_app() {
    [ "$#" -eq 1 ] || usage
    app=$1
    [ "$(basename "$app")" = HelperDaemonProbe.app ] || { echo "unexpected probe app name" >&2; exit 2; }
    case "$(basename "$(dirname "$app")")" in
        ventilator-signed-daemon.*) ;;
        *) echo "unexpected probe directory name" >&2; exit 2 ;;
    esac
    [ -f "$app/Contents/Info.plist" ] || { echo "probe app not found" >&2; exit 2; }
    [ -f "$app/../.ventilator-daemon-probe" ] || { echo "probe marker missing" >&2; exit 2; }
}

registration() {
    "$app/Contents/MacOS/daemon-registration" "$1"
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
    cp ./helper-status "$scratch/signing-check"
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
    scratch=$(mktemp -d "${TMPDIR:-/tmp}/ventilator-signed-daemon.XXXXXX")
    chmod 700 "$scratch"
    app="$scratch/HelperDaemonProbe.app"
    trap 'echo "incomplete probe retained: $scratch" >&2' EXIT HUP INT TERM
    valid_identity
    mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources" \
        "$app/Contents/Library/LaunchDaemons"
    cp ./daemon-registration "$app/Contents/MacOS/daemon-registration"
    cp ./daemon-status "$app/Contents/Resources/daemon-status"
    cp ./helper-status "$scratch/signed-client"
    cp ./helper-status "$scratch/other-client"
    cp ./helper-status "$scratch/other-signed-client"
    codesign --force --sign "$identity" --timestamp=none \
        --identifier com.ventilator.helper-ipc.signed-client "$scratch/signed-client"
    codesign --force --sign "$identity" --timestamp=none \
        --identifier com.ventilator.helper-ipc.signed-daemon "$app/Contents/Resources/daemon-status"
    codesign --force --sign "$identity" --timestamp=none \
        --identifier com.ventilator.helper-ipc.daemon-registration "$app/Contents/MacOS/daemon-registration"
    codesign --force --sign - --identifier com.ventilator.helper-ipc.other-client "$scratch/other-client"
    codesign --force --sign "$identity" --timestamp=none \
        --identifier com.ventilator.helper-ipc.other-signed-client "$scratch/other-signed-client"
    team=$(codesign -dv --verbose=4 "$scratch/signed-client" 2>&1 | sed -n 's/^TeamIdentifier=//p')
    case "$team" in
        ??????????) case "$team" in *[!A-Za-z0-9]*) echo "invalid Team ID" >&2; exit 1;; esac ;;
        *) echo "missing Team ID" >&2; exit 1;;
    esac
    client_requirement="anchor apple generic and identifier \"com.ventilator.helper-ipc.signed-client\" and certificate leaf[subject.OU] = \"$team\""
    daemon_requirement="anchor apple generic and identifier \"com.ventilator.helper-ipc.signed-daemon\" and certificate leaf[subject.OU] = \"$team\""
    codesign -v -R="$client_requirement" "$scratch/signed-client"
    codesign -v -R="$daemon_requirement" "$app/Contents/Resources/daemon-status"
    if codesign -v -R="$client_requirement" "$scratch/other-client" >/dev/null 2>&1; then
        echo "ad hoc client unexpectedly meets signed requirement" >&2
        exit 1
    fi
    if codesign -v -R="$client_requirement" "$scratch/other-signed-client" >/dev/null 2>&1; then
        echo "foreign signed client unexpectedly meets identifier requirement" >&2
        exit 1
    fi
    cat >"$app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleIdentifier</key><string>$bundle_id</string>
  <key>CFBundleName</key><string>Ventilator Read-only Daemon Probe</string>
  <key>CFBundleExecutable</key><string>daemon-registration</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>LSUIElement</key><true/>
</dict></plist>
EOF
    cat >"$app/Contents/Library/LaunchDaemons/$plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$service</string>
  <key>AssociatedBundleIdentifiers</key><string>$bundle_id</string>
  <key>BundleProgram</key><string>Contents/Resources/daemon-status</string>
  <key>ProgramArguments</key><array>
    <string>Contents/Resources/daemon-status</string>
    <string>$service</string><string>$team</string>
  </array>
  <key>MachServices</key><dict><key>$service</key><true/></dict>
</dict></plist>
EOF
    plutil -lint "$app/Contents/Info.plist" "$app/Contents/Library/LaunchDaemons/$plist"
    codesign --force --sign "$identity" --timestamp=none "$app"
    codesign --verify --strict "$app"
    codesign --verify --strict "$app/Contents/Resources/daemon-status"
    codesign --verify --strict "$app/Contents/MacOS/daemon-registration"
    codesign --verify --strict "$scratch/signed-client"
    codesign --verify --strict "$scratch/other-signed-client"
    printf '%s\n' "$team" >"$scratch/.team"
    : >"$scratch/.ventilator-daemon-probe"
    trap - EXIT HUP INT TERM
    echo "prepared=$app"
    echo "status=$(registration status)"
    echo "cleanup command: $0 unregister '$app' && $0 cleanup '$app'"
}

[ "$#" -ge 1 ] || usage
action=$1
shift
case "$action" in
    prepare) prepare "$@" ;;
    status)
        require_app "$@"
        registration status
        if launchctl print "system/$service" >/dev/null 2>&1; then echo "system-service=present"; else echo "system-service=absent"; fi
        ;;
    register)
        require_app "$@"
        before=$(registration status)
        [ "$before" = notRegistered ] || [ "$before" = notFound ] || { echo "unexpected pre-registration status: $before" >&2; exit 1; }
        registration register
        registration status
        ;;
    check)
        require_app "$@"
        [ "$(registration status)" = enabled ] || { echo "daemon is not enabled" >&2; exit 1; }
        team=$(cat "$app/../.team")
        "$app/../signed-client" request-signed "$service" "$team"
        if "$app/../other-client" request-signed "$service" "$team" >/dev/null 2>&1; then
            echo "CRITICAL: ad hoc client reached signed daemon" >&2
            exit 1
        fi
        echo "ad-hoc-client=rejected"
        if "$app/../other-signed-client" request-signed "$service" "$team" >/dev/null 2>&1; then
            echo "CRITICAL: foreign signed client reached daemon" >&2
            exit 1
        fi
        echo "foreign-signed-client=rejected"
        "$app/../signed-client" request-signed "$service" "$team"
        launchctl print "system/$service" >/dev/null || { echo "system daemon missing" >&2; exit 1; }
        echo "system-service=present"
        ;;
    unregister)
        require_app "$@"
        before=$(registration status)
        if [ "$before" != notRegistered ] && [ "$before" != notFound ]; then registration unregister; fi
        after=$(registration status)
        [ "$after" = notRegistered ] || [ "$after" = notFound ] || { echo "CRITICAL: status after unregister=$after" >&2; exit 1; }
        service_absent || { echo "CRITICAL: system/$service still present" >&2; exit 1; }
        echo "daemon removed: $after; system-service=absent"
        ;;
    cleanup)
        require_app "$@"
        after=$(registration status)
        [ "$after" = notRegistered ] || [ "$after" = notFound ] || { echo "unregister daemon before cleanup: $after" >&2; exit 1; }
        service_absent || { echo "system service still present" >&2; exit 1; }
        scratch=$(dirname "$app")
        rm -rf "$scratch"
        echo "probe package removed"
        ;;
    *) usage ;;
esac
