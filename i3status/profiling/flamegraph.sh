#!/bin/sh

set -eu

usage() {
    printf 'Usage: %s TARGET [SECONDS] [OUTPUT]\n' "$0" >&2
    printf 'Environment: I3STATUS_BUILD_DIR (default: build-profile)\n' >&2
    printf 'Environment: FLAMEGRAPH_DIR (directory containing FlameGraph scripts)\n' >&2
    exit 2
}

[ "$#" -ge 1 ] && [ "$#" -le 3 ] || usage

target=$1
duration=${2:-30}
profile_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_dir=$(CDPATH= cd -- "$profile_dir/.." && pwd)
. "$profile_dir/perf-permissions.sh"
targets_dir=$profile_dir/targets
build_dir=${I3STATUS_BUILD_DIR:-"$project_dir/build-profile"}
binary=$build_dir/i3status
config=$targets_dir/$target/i3status.json
output=${3:-"$profile_dir/$target.svg"}

case "$duration" in
    ''|*[!0-9]*) printf 'Duration must be a positive integer\n' >&2; exit 2 ;;
esac
if [ "$duration" -eq 0 ]; then
    printf 'Duration must be greater than zero\n' >&2
    exit 2
fi

if ! command -v perf >/dev/null 2>&1; then
    printf 'perf is required to generate a flamegraph\n' >&2
    exit 1
fi

flamegraph_dir=${FLAMEGRAPH_DIR:-"$profile_dir/FlameGraph"}
if [ -n "$flamegraph_dir" ]; then
    stackcollapse=$flamegraph_dir/stackcollapse-perf.pl
    flamegraph=$flamegraph_dir/flamegraph.pl
    if [ ! -f "$stackcollapse" ] || [ ! -f "$flamegraph" ]; then
        stackcollapse=$(command -v stackcollapse-perf.pl || true)
        flamegraph=$(command -v flamegraph.pl || true)
    fi
else
    stackcollapse=$(command -v stackcollapse-perf.pl || true)
    flamegraph=$(command -v flamegraph.pl || true)
fi
if [ ! -f "$stackcollapse" ] || [ ! -f "$flamegraph" ]; then
    printf 'FlameGraph scripts are required: stackcollapse-perf.pl and flamegraph.pl\n' >&2
    printf 'Restore profiling/FlameGraph or set FLAMEGRAPH_DIR to a FlameGraph checkout\n' >&2
    exit 1
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

output_dir=$(dirname "$output")
mkdir -p "$output_dir"
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/i3status-flamegraph.XXXXXX")
cleanup() {
    restore_cpu_profiling_permissions
    rm -rf "$tmp_dir"
}
trap cleanup EXIT HUP INT TERM
perf_data=$tmp_dir/perf.data
perf_script=$tmp_dir/perf.script
folded=$tmp_dir/perf.folded

allow_cpu_profiling

printf 'Recording %s for %ss\n' "$target" "$duration" >&2
set +e
perf record -q -o "$perf_data" --call-graph dwarf -- \
    timeout --signal=INT --kill-after=5 "$duration" "$binary" -c "$config" >/dev/null
status=$?
set -e
if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
    printf 'perf record failed with status %s\n' "$status" >&2
    exit "$status"
fi

perf script -i "$perf_data" >"$perf_script"
perl "$stackcollapse" "$perf_script" >"$folded"
perl "$flamegraph" --title "i3status: $target" "$folded" >"$output"
printf 'Wrote %s\n' "$output" >&2
