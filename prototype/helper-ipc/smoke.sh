#!/bin/sh
set -eu

scratch=$(mktemp -d "${TMPDIR:-/tmp}/ventilator-helper-ipc.XXXXXX")
chmod 700 "$scratch"
service="com.ventilator.helper-ipc.smoke.$$"
domain="gui/$(id -u)"
plist="$scratch/service.plist"
binary="$(pwd -P)/helper-status"
registered=0

cleanup() {
    result=$?
    trap - EXIT HUP INT TERM
    if [ "$registered" -eq 1 ]; then
        if ! launchctl bootout "$domain" "$plist"; then
            echo "CRITICAL: temporary XPC service may remain: $domain/$service" >&2
            result=1
        fi
        if launchctl print "$domain/$service" >/dev/null 2>&1; then
            echo "CRITICAL: temporary XPC service still registered: $domain/$service" >&2
            result=1
        fi
    fi
    if [ "$result" -eq 0 ]; then
        rm -rf "$scratch"
    else
        echo "smoke artifacts retained for inspection: $scratch" >&2
    fi
    exit "$result"
}
trap cleanup EXIT HUP INT TERM

cat >"$plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$service</string>
  <key>ProgramArguments</key><array>
    <string>$binary</string><string>serve</string><string>$service</string>
  </array>
  <key>MachServices</key><dict><key>$service</key><true/></dict>
</dict></plist>
EOF

launchctl bootstrap "$domain" "$plist"
registered=1
./helper-status request "$service"
