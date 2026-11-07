# Profiling configurations

Each subdirectory contains an `i3status.json` configuration focused on one
major status module. `all-features` enables every module. The
configurations use live system sources and set a 50-millisecond update interval.

Build a profiling binary, then run one configuration for a bounded period:

```sh
meson setup build-profile --buildtype=debugoptimized
meson compile -C build-profile
timeout 30 perf stat -d \
  build-profile/i3status -c profiling/targets/wireless/i3status.json >/dev/null
```

The helper script selects a target and runs it under `perf stat`:

```sh
profiling/profile.sh wireless 30
profiling/profile.sh all-features 60
```

If `perf` is restricted by `kernel.perf_event_paranoid`, the scripts
temporarily adjust it automatically. They request `sudo` when necessary and
restore the original value automatically:

```sh
profiling/profile.sh wireless 30
profiling/flamegraph.sh all-features 60
```

Set `I3STATUS_BUILD_DIR` when the profiling build is elsewhere. Replace the
target to profile another module. The wireless,
ethernet, and volume configurations depend on the corresponding hardware or
audio service being available; they still emit their normal down/error output
when those services are unavailable.

Generate a flamegraph with the vendored FlameGraph scripts:

```sh
profiling/flamegraph.sh all-features 60
```

The SVG is written to `profiling/all-features.svg` by default. Pass a third
argument to choose another output path. The required scripts are copied under
`profiling/FlameGraph/`; set `FLAMEGRAPH_DIR` to another FlameGraph checkout
if you need to override them.
