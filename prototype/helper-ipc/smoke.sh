#!/bin/sh
set -eu

scratch=$(mktemp -d "${TMPDIR:-/tmp}/ventilator-helper-ipc.XXXXXX")
chmod 700 "$scratch"
domain="gui/$(id -u)"
trusted_service="com.ventilator.helper-ipc.smoke.trusted.$$"
foreign_service="com.ventilator.helper-ipc.smoke.foreign.$$"
trusted_plist="$scratch/trusted.plist"
foreign_plist="$scratch/foreign.plist"
trusted_registered=0
foreign_registered=0

stop_service() {
    plist=$1
    service=$2
    if ! launchctl bootout "$domain" "$plist"; then
        echo "CRITICAL: temporary XPC service may remain: $domain/$service" >&2
        return 1
    fi
    if launchctl print "$domain/$service" >/dev/null 2>&1; then
        echo "CRITICAL: temporary XPC service still registered: $domain/$service" >&2
        return 1
    fi
}

cleanup() {
    result=$?
    trap - EXIT HUP INT TERM
    if [ "$trusted_registered" -eq 1 ]; then
        stop_service "$trusted_plist" "$trusted_service" || result=1
    fi
    if [ "$foreign_registered" -eq 1 ]; then
        stop_service "$foreign_plist" "$foreign_service" || result=1
    fi
    if [ "$result" -eq 0 ]; then
        rm -rf "$scratch"
    else
        echo "smoke artifacts retained for inspection: $scratch" >&2
    fi
    exit "$result"
}
trap cleanup EXIT HUP INT TERM

cp ./helper-status "$scratch/trusted"
cp ./helper-status "$scratch/foreign"
codesign --force --sign - --identifier com.ventilator.helper-ipc.smoke.trusted "$scratch/trusted"
codesign --force --sign - --identifier com.ventilator.helper-ipc.smoke.foreign "$scratch/foreign"
trusted_hash=$(codesign -dv --verbose=4 "$scratch/trusted" 2>&1 | sed -n 's/^CDHash=//p')
foreign_hash=$(codesign -dv --verbose=4 "$scratch/foreign" 2>&1 | sed -n 's/^CDHash=//p')
if [ "${#trusted_hash}" -ne 40 ] || [ "${#foreign_hash}" -ne 40 ] || [ "$trusted_hash" = "$foreign_hash" ]; then
    echo "could not establish distinct ad hoc cdhash values" >&2
    exit 1
fi

write_plist() {
    plist=$1
    service=$2
    binary=$3
    client_hash=$4
    server_log=$5
    cat >"$plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$service</string>
  <key>ProgramArguments</key><array>
    <string>$binary</string><string>serve</string><string>$service</string><string>$client_hash</string>
  </array>
  <key>MachServices</key><dict><key>$service</key><true/></dict>
  <key>StandardErrorPath</key><string>$server_log</string>
</dict></plist>
EOF
}

write_plist "$trusted_plist" "$trusted_service" "$scratch/trusted" "$trusted_hash" "$scratch/trusted-server.log"
write_plist "$foreign_plist" "$foreign_service" "$scratch/foreign" "$trusted_hash" "$scratch/foreign-daemon.log"

launchctl bootstrap "$domain" "$trusted_plist"
trusted_registered=1
"$scratch/trusted" request "$trusted_service" "$trusted_hash"
if [ "$(grep -c 'accepted XPC connection' "$scratch/trusted-server.log")" -ne 1 ]; then
    echo "trusted server did not accept exactly one initial connection" >&2
    exit 1
fi
if "$scratch/foreign" request "$trusted_service" "$trusted_hash" >"$scratch/foreign-client.log" 2>&1; then
    echo "foreign client unexpectedly reached the trusted service" >&2
    exit 1
fi
if ! grep -q 'code=4097' "$scratch/foreign-client.log"; then
    echo "foreign client did not fail with the expected XPC connection error" >&2
    cat "$scratch/foreign-client.log" >&2
    exit 1
fi
if [ "$(grep -c 'accepted XPC connection' "$scratch/trusted-server.log")" -ne 1 ]; then
    echo "foreign client reached the trusted listener" >&2
    exit 1
fi
"$scratch/trusted" request "$trusted_service" "$trusted_hash"
if [ "$(grep -c 'accepted XPC connection' "$scratch/trusted-server.log")" -ne 2 ]; then
    echo "trusted server did not remain usable after rejecting the foreign client" >&2
    exit 1
fi
echo "foreign client rejected"
stop_service "$trusted_plist" "$trusted_service"
trusted_registered=0

launchctl bootstrap "$domain" "$foreign_plist"
foreign_registered=1
if "$scratch/trusted" request "$foreign_service" "$trusted_hash" >"$scratch/foreign-server.log" 2>&1; then
    echo "trusted client unexpectedly accepted the foreign service" >&2
    exit 1
fi
if ! grep -q 'code=4102' "$scratch/foreign-server.log"; then
    echo "foreign service did not fail with the XPC code-signing error" >&2
    cat "$scratch/foreign-server.log" >&2
    exit 1
fi
echo "foreign service rejected"
stop_service "$foreign_plist" "$foreign_service"
foreign_registered=0
