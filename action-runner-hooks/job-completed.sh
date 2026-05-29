#!/usr/bin/env sh
set -u

process_name="a1-pol-mem"
percent="${A1_POL_MEM_PERCENT:-50}"
script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_root="$script_dir/.."

error() {
    printf 'error: %s\n' "$*" >&2
}

is_executable_file() {
    [ -f "$1" ] && [ -x "$1" ]
}

find_in_repo() {
    find "$repo_root" -type f \( -name "$process_name" -o -name "$process_name.exe" \) \
        -perm -111 -print 2>/dev/null | sort | head -n 1
}

resolve_bin() {
    if [ -n "${A1_POL_MEM_BIN:-}" ]; then
        if is_executable_file "$A1_POL_MEM_BIN"; then
            printf '%s\n' "$A1_POL_MEM_BIN"
            return 0
        fi
        return 1
    fi

    if [ -d "$repo_root/.git" ]; then
        found="$(find_in_repo)"
        if [ -n "$found" ]; then
            printf '%s\n' "$found"
            return 0
        fi
    fi

    if command -v "$process_name" >/dev/null 2>&1; then
        command -v "$process_name"
        return 0
    fi

    return 1
}

bin="$(resolve_bin)" || {
    error "could not locate $process_name; set A1_POL_MEM_BIN, build the repo, or add it to PATH"
    exit 1
}

"$bin" start "$percent"
exit $?
