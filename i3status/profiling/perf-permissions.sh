#!/bin/sh

original_perf_event_paranoid=
perf_event_paranoid_changed=false

set_perf_event_paranoid() {
    value=$1
    if [ "$(id -u)" -eq 0 ]; then
        sysctl -q -w "kernel.perf_event_paranoid=$value"
    else
        sudo sysctl -q -w "kernel.perf_event_paranoid=$value"
    fi
}

allow_cpu_profiling() {
    original_perf_event_paranoid=$(cat /proc/sys/kernel/perf_event_paranoid) || {
        printf 'Unable to read kernel.perf_event_paranoid\n' >&2
        return 1
    }

    case "$original_perf_event_paranoid" in
        ''|*[!0-9-]*)
            printf 'Invalid kernel.perf_event_paranoid value: %s\n' "$original_perf_event_paranoid" >&2
            return 1
            ;;
    esac

    if [ "$original_perf_event_paranoid" -le 0 ]; then
        return 0
    fi

    printf 'Temporarily setting kernel.perf_event_paranoid=0\n' >&2
    set_perf_event_paranoid 0 || {
        printf 'Unable to enable CPU profiling\n' >&2
        return 1
    }
    perf_event_paranoid_changed=true
}

restore_cpu_profiling_permissions() {
    if [ "$perf_event_paranoid_changed" = true ]; then
        printf 'Restoring kernel.perf_event_paranoid=%s\n' "$original_perf_event_paranoid" >&2
        if ! set_perf_event_paranoid "$original_perf_event_paranoid"; then
            printf 'Warning: unable to restore kernel.perf_event_paranoid=%s\n' \
                "$original_perf_event_paranoid" >&2
        fi
    fi
}
