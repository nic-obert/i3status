#!/bin/sh

set -eu

usage() {
    printf 'Usage: %s TARGET [SECONDS]\n' "$0" >&2
    printf 'Environment: I3STATUS_BUILD_DIR (default: build-profile)\n' >&2
    exit 2
}

[ "$#" -ge 1 ] && [ "$#" -le 2 ] || usage

target=$1
duration=${2:-30}
profile_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_dir=$(CDPATH= cd -- "$profile_dir/.." && pwd)
. "$profile_dir/perf-permissions.sh"
trap restore_cpu_profiling_permissions EXIT HUP INT TERM
targets_dir=$profile_dir/targets
build_dir=${I3STATUS_BUILD_DIR:-"$project_dir/build-profile"}
binary=$build_dir/i3status
config=$targets_dir/$target/i3status.json

case "$duration" in
    ''|*[!0-9]*) printf 'Duration must be a positive integer\n' >&2; exit 2 ;;
esac
if [ "$duration" -eq 0 ]; then
    printf 'Duration must be greater than zero\n' >&2
    exit 2
fi

if ! meson compile -C "$build_dir"; then
    printf 'Failed to compile profiling binary in: %s\n' "$build_dir" >&2
    exit 1
fi

if [ ! -x "$binary" ]; then
    printf 'Profiling binary not found: %s\n' "$binary" >&2
    printf 'Build it with: meson setup %s --buildtype=debugoptimized && meson compile -C %s\n' "$build_dir" "$build_dir" >&2
    exit 1
fi

if [ ! -f "$config" ]; then
    printf 'Unknown profiling target: %s\n' "$target" >&2
    printf 'Available targets:\n' >&2
    find "$targets_dir" -mindepth 2 -maxdepth 2 -name i3status.json -printf '%h\n' 2>/dev/null |
        sed "s#^$targets_dir/##; s#^#  #" >&2
    exit 2
fi

allow_cpu_profiling

printf 'Profiling %s for %ss\n' "$target" "$duration" >&2
set +e
perf stat -d timeout "$duration" "$binary" -c "$config" >/dev/null
status=$?
set -e

# timeout returns 124 when the requested profiling window completes normally.
if [ "$status" -eq 124 ]; then
    exit 0
fi
exit "$status"
