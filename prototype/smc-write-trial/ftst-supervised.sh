#!/bin/bash

# One reviewed Ftst probe, followed by an independent read-only process.
# Keep this wrapper separate from the read-only Ventilator application.
set -u

trial_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
trial_binary="$trial_dir/smc-write-trial"
trial_started=0

if [[ $# -ne 0 || ! -t 0 || "$(id -u)" -ne 0 || ! -x "$trial_binary" ]]; then
    echo "Use an interactive root Terminal and a built smc-write-trial binary; no arguments." >&2
    exit 2
fi

for source in Makefile smc-write-trial.c trial_logic.c trial_logic.h trial_actions.c trial_actions.h; do
    if [[ ! -f "$trial_dir/$source" || "$trial_dir/$source" -nt "$trial_binary" ]]; then
        echo "Trial binary is stale or source is missing: $source. Rebuild with make -C $trial_dir build; no SMC access attempted." >&2
        exit 2
    fi
done

recover_and_verify() {
    local recovery_status observer_status
    echo "CRITICAL: checking independent recovery; keep the Mac awake." >&2
    "$trial_binary" restore-unlock --apply --confirm RESTORE-UNLOCK-Mac15,7-27.0
    recovery_status=$?
    "$trial_binary" observe-baseline --read-only
    observer_status=$?
    if [[ $recovery_status -ne 0 || $observer_status -ne 0 ]]; then
        echo "CRITICAL: system control is not verified. Reboot now, then read the SMC baseline." >&2
    else
        echo "Recovery and a separate 61-sample baseline observation completed; review the log." >&2
    fi
    return 1
}

on_interrupt() {
    trap - INT TERM HUP QUIT
    if [[ $trial_started -eq 0 ]]; then
        echo "Preflight interrupted before the trial; no trial write was requested." >&2
        exit 130
    fi
    echo "CRITICAL: supervised probe interrupted." >&2
    recover_and_verify
    exit 130
}
trap on_interrupt INT TERM HUP QUIT

echo "Binary SHA-256: $(shasum -a 256 "$trial_binary")"
if ! "$trial_binary" restore-unlock --dry-run; then
    echo "Read-only recovery precheck failed; no trial write was requested." >&2
    exit 2
fi
if ! "$trial_binary" ftst-check --dry-run; then
    echo "Read-only baseline precheck failed; no trial write was requested." >&2
    exit 2
fi

trial_started=1
"$trial_binary" ftst-check --apply-reviewed --confirm FTST-REVIEWED-Mac15,7-27.0
trial_status=$?
if [[ $trial_status -eq 2 ]]; then
    echo "Trial stopped before any SMC write; independent observation is unnecessary."
    exit 2
fi

echo "Starting an independent read-only process after the trial exited."
"$trial_binary" observe-baseline --read-only
observer_status=$?
if [[ $observer_status -ne 0 ]]; then
    recover_and_verify
    exit 1
fi

if [[ $trial_status -ne 0 ]]; then
    echo "Trial was rejected or unverified; the independent baseline stayed stable for one minute." >&2
    exit 1
fi

echo "Reviewed Ftst trial and independent 61-sample baseline observation completed."
