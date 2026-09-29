#!/bin/bash
set -eu

# A consumed trial must stay closed even when its old wrapper sets this marker.
binary=${1:-./smc-write-trial}
expected='reviewed minimum-target trial completed; further hardware trials suspended before SMC access'
failed=0
for marker in unset 0 1; do
    status=0
    if [[ "$marker" == unset ]]; then
        output=$(env -u VENTILATOR_MINIMUM_SUPERVISED "$binary" ftst-minimum \
            --apply-reviewed --confirm FTST-MINIMUM-Mac15,7-27.0 2>&1) || status=$?
    else
        output=$(env VENTILATOR_MINIMUM_SUPERVISED="$marker" "$binary" ftst-minimum \
            --apply-reviewed --confirm FTST-MINIMUM-Mac15,7-27.0 2>&1) || status=$?
    fi
    if [[ $status -ne 2 || "$output" != "$expected" ]]; then
        printf 'minimum trial suspension failed: marker=%s status=%s output=%s\n' \
            "$marker" "$status" "$output" >&2
        failed=1
    fi
done
[[ $failed -eq 0 ]] || exit 1
echo 'minimum trial suspension tests passed'
