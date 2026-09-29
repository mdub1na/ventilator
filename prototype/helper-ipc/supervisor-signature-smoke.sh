#!/bin/sh
set -eu
make daemon-status supervisor-probe
scratch=$(mktemp -d "${TMPDIR:-/tmp}/ventilator-supervisor-signature.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
cp daemon-status "$scratch/daemon-status"
identity=
for candidate in $(security find-identity -v -p codesigning | awk '/Apple Development:/ {print $2}'); do
    if codesign --force --sign "$candidate" --timestamp=none \
        --identifier com.ventilator.helper-ipc.signed-daemon "$scratch/daemon-status" >/dev/null 2>&1 &&
        codesign --verify --strict "$scratch/daemon-status" >/dev/null 2>&1; then identity=$candidate; break; fi
done
[ -n "$identity" ] || { echo "no verifiable Apple Development signature" >&2; exit 1; }
cp supervisor-probe "$scratch/supervisor-probe"
codesign --force --sign "$identity" --timestamp=none \
    --identifier com.ventilator.helper-ipc.signed-supervisor "$scratch/supervisor-probe"
"$scratch/daemon-status" --probe-signature-check
codesign --force --sign "$identity" --timestamp=none \
    --identifier com.ventilator.helper-ipc.other-supervisor "$scratch/supervisor-probe"
if "$scratch/daemon-status" --probe-signature-check; then echo "wrong identifier accepted" >&2; exit 1; fi
codesign --force --sign - --identifier com.ventilator.helper-ipc.signed-supervisor "$scratch/supervisor-probe"
if "$scratch/daemon-status" --probe-signature-check; then echo "ad hoc signature accepted" >&2; exit 1; fi
codesign --force --sign "$identity" --timestamp=none \
    --identifier com.ventilator.helper-ipc.signed-supervisor "$scratch/supervisor-probe"
printf 'tampered' >>"$scratch/supervisor-probe"
if "$scratch/daemon-status" --probe-signature-check; then echo "tampered executable accepted" >&2; exit 1; fi
rm "$scratch/supervisor-probe"
if "$scratch/daemon-status" --probe-signature-check; then echo "missing executable accepted" >&2; exit 1; fi
echo "supervisor-signature=verified accepted=trusted rejected=wrong-identifier,ad-hoc,tampered,missing; no task launched"
