#!/bin/sh
set -eu

[ "$#" -eq 1 ] || { echo "Usage: $0 PREPARED_DAEMON_PROBE_APP" >&2; exit 2; }
app=$1
scratch=$app/..
[ -f "$scratch/.ventilator-daemon-probe" ] || { echo "probe marker missing" >&2; exit 2; }
service="com.ventilator.helper-ipc.signed-user-test.$$"
domain="gui/$(id -u)"
plist="$scratch/$service.plist"
team=$(cat "$scratch/.team")
registered=0

cleanup() {
    result=$?
    trap - EXIT HUP INT TERM
    if [ "$registered" -eq 1 ]; then
        if ! launchctl bootout "$domain" "$plist" >&2; then
            echo "CRITICAL: could not remove $domain/$service; plist retained: $plist" >&2
            exit 1
        fi
        i=0
        while launchctl print "$domain/$service" >/dev/null 2>&1; do
            i=$((i + 1))
            if [ "$i" -ge 30 ]; then
                echo "CRITICAL: $domain/$service still present; plist retained: $plist" >&2
                exit 1
            fi
            sleep 0.1
        done
    fi
    rm -f "$plist"
    exit "$result"
}
trap cleanup EXIT HUP INT TERM

cat >"$plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$service</string>
  <key>ProgramArguments</key><array>
    <string>$app/Contents/Resources/daemon-status</string>
    <string>$service</string><string>$team</string>
  </array>
  <key>MachServices</key><dict><key>$service</key><true/></dict>
</dict></plist>
EOF
plutil -lint "$plist"
registered=1
launchctl bootstrap "$domain" "$plist"
"$scratch/signed-client" request-signed "$service" "$team"
if "$scratch/other-client" request-signed "$service" "$team" >/dev/null 2>&1; then
    echo "CRITICAL: ad hoc client reached signed listener" >&2
    exit 1
fi
echo "ad-hoc-client=rejected"
if "$scratch/other-signed-client" request-signed "$service" "$team" >/dev/null 2>&1; then
    echo "CRITICAL: foreign signed client reached listener" >&2
    exit 1
fi
echo "foreign-signed-client=rejected"
"$scratch/signed-client" request-signed "$service" "$team"
echo "signed user IPC passed; removing service"
