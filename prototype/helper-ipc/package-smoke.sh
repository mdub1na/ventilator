#!/bin/sh
set -eu

scratch=$(mktemp -d "${TMPDIR:-/tmp}/ventilator-helper-package.XXXXXX")
chmod 700 "$scratch"
app="$scratch/HelperProbe.app"
service="com.ventilator.helper-ipc.package-test.$$"
bundle_id="com.ventilator.helper-ipc.package-test.$$"
domain="gui/$(id -u)"
registration_attempted=0

service_absent() {
    i=0
    while launchctl print "$domain/$service" >/dev/null 2>&1; do
        i=$((i + 1))
        if [ "$i" -ge 20 ]; then return 1; fi
        sleep 0.1
    done
}

cleanup() {
    result=$?
    trap - EXIT HUP INT TERM
    if [ "$registration_attempted" -eq 1 ]; then
        if ! "$app/Contents/MacOS/agent-registration" unregister >&2; then
            echo "CRITICAL: could not unregister $service" >&2
            result=1
        fi
        if ! service_absent; then
            echo "CRITICAL: package service still present: $domain/$service" >&2
            result=1
        fi
    fi
    if [ "$result" -eq 0 ]; then
        rm -rf "$scratch"
    else
        echo "package retained for inspection: $app" >&2
    fi
    exit "$result"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources" "$app/Contents/Library/LaunchAgents"
cp ./agent-registration "$app/Contents/MacOS/agent-registration"
cp ./helper-status "$app/Contents/Resources/helper-status"
cp ./helper-status "$scratch/client"
codesign --force --sign - --identifier com.ventilator.helper-ipc.package-test.client "$scratch/client"
codesign --force --sign - --identifier com.ventilator.helper-ipc.package-test.service "$app/Contents/Resources/helper-status"
client_hash=$(codesign -dv --verbose=4 "$scratch/client" 2>&1 | sed -n 's/^CDHash=//p')
server_hash=$(codesign -dv --verbose=4 "$app/Contents/Resources/helper-status" 2>&1 | sed -n 's/^CDHash=//p')
if [ "${#client_hash}" -ne 40 ] || [ "${#server_hash}" -ne 40 ]; then
    echo "could not read signed peer cdhash values" >&2
    exit 1
fi

cat >"$app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleIdentifier</key><string>$bundle_id</string>
  <key>CFBundleName</key><string>Ventilator Helper Probe</string>
  <key>CFBundleExecutable</key><string>agent-registration</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>LSUIElement</key><true/>
</dict></plist>
EOF

cat >"$app/Contents/Library/LaunchAgents/com.ventilator.helper-ipc.package-test.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$service</string>
  <key>AssociatedBundleIdentifiers</key><string>$bundle_id</string>
  <key>BundleProgram</key><string>Contents/Resources/helper-status</string>
  <key>ProgramArguments</key><array>
    <string>Contents/Resources/helper-status</string>
    <string>serve</string><string>$service</string><string>$client_hash</string>
  </array>
  <key>MachServices</key><dict><key>$service</key><true/></dict>
</dict></plist>
EOF

plutil -lint "$app/Contents/Info.plist" "$app/Contents/Library/LaunchAgents/com.ventilator.helper-ipc.package-test.plist"
codesign --force --sign - "$app"
codesign --verify --strict --verbose=2 "$app"
codesign --verify --strict --verbose=2 "$app/Contents/Resources/helper-status"

before=$("$app/Contents/MacOS/agent-registration" status)
echo "before=$before"
if [ "$before" != "notRegistered" ] && [ "$before" != "notFound" ]; then
    echo "unexpected pre-registration status" >&2
    exit 1
fi
registration_attempted=1
"$app/Contents/MacOS/agent-registration" register
after=$("$app/Contents/MacOS/agent-registration" status)
echo "after=$after"
if [ "$after" != "enabled" ]; then
    echo "agent is not enabled; cleaning up" >&2
    exit 1
fi
"$scratch/client" request "$service" "$server_hash"

"$app/Contents/MacOS/agent-registration" unregister
final=$("$app/Contents/MacOS/agent-registration" status)
if [ "$final" != "notRegistered" ]; then
    echo "CRITICAL: unexpected post-unregister status: $final" >&2
    exit 1
fi
if ! service_absent; then
    echo "CRITICAL: package service still present after unregister" >&2
    exit 1
fi
registration_attempted=0
echo "package agent removed: $final"
