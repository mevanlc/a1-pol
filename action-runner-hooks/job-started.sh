#!/usr/bin/env sh
set -u

process_name="a1-pol-mem"

warn() {
    printf 'warning: %s\n' "$*" >&2
}

find_pids() {
    if command -v pgrep >/dev/null 2>&1; then
        pgrep -x "$process_name" 2>/dev/null || true
        return
    fi

    ps -eo pid=,comm= 2>/dev/null | awk -v name="$process_name" '$2 == name { print $1 }'
}

is_running() {
    pid="$1"

    if ! kill -0 "$pid" 2>/dev/null; then
        return 1
    fi

    if command -v ps >/dev/null 2>&1; then
        comm="$(ps -p "$pid" -o comm= 2>/dev/null | awk 'NR == 1 { print $1 }')"
        [ "$(basename "$comm" 2>/dev/null || printf '%s' "$comm")" = "$process_name" ]
        return
    fi

    return 0
}

pids="$(find_pids)"
[ -n "$pids" ] || exit 0

for pid in $pids; do
    if ! kill -TERM "$pid" 2>/dev/null; then
        warn "could not send SIGTERM to $process_name process $pid"
    fi
done

sleep 2

for pid in $pids; do
    if is_running "$pid"; then
        warn "$process_name process $pid is still running after SIGTERM"
    fi
done

exit 0
