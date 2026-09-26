#!/bin/sh
set -eu

[ "$#" -eq 1 ] || { echo "Usage: $0 PREPARED_INTEGRATED_APP" >&2; exit 2; }
app=$1
scratch=$(dirname "$app")
[ "$(basename "$app")" = Ventilator.app ] &&
    [ -f "$scratch/.ventilator-integrated-probe" ] || {
    echo "expected an integrated-probe.sh prepare package" >&2
    exit 2
}

service=com.ventilator.helper-ipc.read-only
domain="gui/$(id -u)"
plist="$scratch/$service.user-shadow.plist"
client="$scratch/signed-lifecycle-client"
team=$(codesign -dv --verbose=4 "$app" 2>&1 | sed -n 's/^TeamIdentifier=//p')
case "$team" in
    ??????????) case "$team" in *[!A-Za-z0-9]*) exit 2 ;; esac ;;
    *) echo "invalid app Team ID" >&2; exit 2 ;;
esac
[ "$("$app/Contents/MacOS/Ventilator" --helper-registration-status)" = notRegistered ] || {
    echo "root daemon must be unregistered for domain-shadow probe" >&2
    exit 1
}
if launchctl print "system/$service" >/dev/null 2>&1; then
    echo "root daemon is present" >&2
    exit 1
fi
if launchctl print "$domain/$service" >/dev/null 2>&1; then
    echo "user-domain name is already present" >&2
    exit 1
fi

cp ./helper-status "$client"
signed=0
for identity in $(security find-identity -v -p codesigning | awk '/Apple Development:/ {print $2}'); do
    if codesign --force --sign "$identity" --timestamp=none \
        --identifier ventilator.desktop "$client" >/dev/null 2>&1 &&
       codesign --verify --strict "$client" >/dev/null 2>&1 &&
       codesign -dv --verbose=4 "$client" 2>&1 | grep -q "^TeamIdentifier=$team\$"; then
        signed=1
        break
    fi
done
[ "$signed" -eq 1 ] || { echo "no matching Apple Development identity" >&2; exit 1; }

registered=0
stopped=0
request_pid=
cleanup() {
    result=$?
    trap - EXIT HUP INT TERM
    if [ -n "$request_pid" ] && kill -0 "$request_pid" 2>/dev/null; then
        kill "$request_pid" 2>/dev/null || true
        wait "$request_pid" 2>/dev/null || true
    fi
    if [ "$stopped" -eq 1 ]; then
        launchctl kill SIGCONT "$domain/$service" 2>/dev/null || true
    fi
    if [ "$registered" -eq 1 ] && launchctl print "$domain/$service" >/dev/null 2>&1; then
        if ! launchctl bootout "$domain" "$plist"; then
            echo "CRITICAL: could not remove $domain/$service; plist retained: $plist" >&2
            exit 1
        fi
        count=0
        while launchctl print "$domain/$service" >/dev/null 2>&1; do
            count=$((count + 1))
            if [ "$count" -ge 30 ]; then
                echo "CRITICAL: user-domain service remains; plist retained: $plist" >&2
                exit 1
            fi
            sleep 0.1
        done
    fi
    rm -f "$plist" "$client" "$scratch/.lifecycle-inflight-output"
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
    <string>$service</string><string>$team</string><string>ventilator.desktop</string>
  </array>
  <key>MachServices</key><dict><key>$service</key><true/></dict>
</dict></plist>
EOF
plutil -lint "$plist"
registered=1
launchctl bootstrap "$domain" "$plist"

"$client" request-signed "$service" "$team"
echo "signed user-domain control=accepted"
if "$app/Contents/MacOS/Ventilator" --helper-request; then
    echo "CRITICAL: app reached same-named user-domain service" >&2
    exit 1
fi
echo "privileged app connection=user-domain service rejected"

launchctl kill SIGSTOP "$domain/$service"
stopped=1
"$client" request-signed "$service" "$team" >"$scratch/.lifecycle-inflight-output" 2>&1 &
request_pid=$!
sleep 0.5
if ! kill -0 "$request_pid" 2>/dev/null; then
    echo "request exited before daemon interruption" >&2
    cat "$scratch/.lifecycle-inflight-output" >&2
    exit 1
fi
launchctl kill SIGKILL "$domain/$service"
stopped=0
if wait "$request_pid"; then
    echo "CRITICAL: interrupted request accepted a status" >&2
    exit 1
fi
request_pid=
cat "$scratch/.lifecycle-inflight-output"
echo "interrupted user-domain request=failed closed"
launchctl kickstart -k "$domain/$service" >/dev/null
"$client" request-signed "$service" "$team"
echo "new user-domain request=accepted"
