#!/bin/bash

set -u

case_dir="$(mktemp -d "${TMPDIR:-/tmp}/ftst-supervised-test.XXXXXX")"
trap 'rm -rf "$case_dir"' EXIT
cp "$(dirname "$0")/ftst-supervised.sh" "$case_dir/ftst-supervised.sh"
for source in Makefile smc-write-trial.c trial_logic.c trial_logic.h trial_actions.c trial_actions.h; do
    cp "$(dirname "$0")/$source" "$case_dir/$source"
    touch -t 202001010000 "$case_dir/$source"
done

cat > "$case_dir/id" <<'FAKE_ID'
#!/bin/sh
if [ "$1" = -u ]; then echo 0; else /usr/bin/id "$@"; fi
FAKE_ID

cat > "$case_dir/smc-write-trial" <<'FAKE_SMC'
#!/bin/sh
printf '%s\n' "$*" >> "$FTST_FAKE_LOG"
case "$1 $2" in
    'restore-unlock --dry-run') exit 0 ;;
    'ftst-check --dry-run') exit 0 ;;
    'ftst-check --apply-reviewed') exit "$FTST_FAKE_STATUS" ;;
    'restore-unlock --apply') exit 0 ;;
    'observe-baseline --read-only')
        if [ "${FTST_FAKE_OBSERVER_FAIL:-0}" = 1 ] &&
           [ ! -f "$FTST_FAKE_SEEN" ]; then
            : > "$FTST_FAKE_SEEN"
            exit 1
        fi
        exit 0 ;;
esac
exit 9
FAKE_SMC
chmod +x "$case_dir/id" "$case_dir/smc-write-trial"
touch -t 202101010000 "$case_dir/smc-write-trial"

run_case() {
    : > "$case_dir/log"
    rm -f "$case_dir/seen"
    if case_output=$(script -q -e /dev/null env \
        PATH="$case_dir:$PATH" \
        FTST_FAKE_LOG="$case_dir/log" \
        FTST_FAKE_SEEN="$case_dir/seen" \
        FTST_FAKE_STATUS="$1" \
        FTST_FAKE_OBSERVER_FAIL="$2" \
        "$case_dir/ftst-supervised.sh" </dev/null 2>&1); then
        return 0
    else
        return $?
    fi
}

if run_case 2 0; then exit 1; else status=$?; fi
[[ $status -eq 2 ]] || exit 1
[[ $(wc -l < "$case_dir/log") -eq 3 ]] || exit 1
! grep -Eq 'observe-baseline|restore-unlock --apply' "$case_dir/log" || exit 1

if run_case 1 0; then exit 1; else status=$?; fi
[[ $status -eq 1 ]] || exit 1
[[ $(grep -c 'observe-baseline --read-only' "$case_dir/log") -eq 1 ]] || exit 1
! grep -q 'restore-unlock --apply' "$case_dir/log" || exit 1

if run_case 0 1; then exit 1; else status=$?; fi
[[ $status -eq 1 ]] || exit 1
[[ $(grep -c 'observe-baseline --read-only' "$case_dir/log") -eq 2 ]] || exit 1
grep -q 'restore-unlock --apply' "$case_dir/log" || exit 1

touch -t 202201010000 "$case_dir/trial_actions.c"
if run_case 0 0; then exit 1; else status=$?; fi
[[ $status -eq 2 ]] || exit 1
[[ ! -s "$case_dir/log" ]] || exit 1
[[ "$case_output" == *'Trial binary is stale or source is missing: trial_actions.c'* ]] || exit 1

echo 'supervised Ftst wrapper tests passed'
