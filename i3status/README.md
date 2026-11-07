# i3status

## Description

i3status is a small program for generating a status bar for i3bar, dzen2, xmobar
or similar programs. It is designed to be very efficient by issuing a very small
number of system calls, as one generally wants to update such a status line
every second. This ensures that even under high load, your status bar is updated
correctly. Also, it saves a bit of energy by not hogging your CPU as much as
spawning the corresponding amount of shell commands would.

Configuration files are JSON and are parsed with YAJL. See the man page for the
JSON schema and default configuration search paths.

## Development

i3status has the following dependencies:
  * libyajl-dev
  * libasound2-dev
  * libnl-genl-3-dev
  * meson (compile-time only dependency)
  * asciidoc (only for the documentation)
  * libpulse-dev (for getting the current volume using PulseAudio)

On debian-based systems, the following line will install all requirements:
```bash
apt-get install autoconf libyajl-dev libasound2-dev libiw-dev asciidoc libpulse-dev libnl-genl-3-dev meson
```

## Upstream

i3status is developed at https://github.com/i3/i3status

## Compilation

Prefer installing i3status via your Linux distribution’s package manager.

If you absolutely have to build from source, use:

```bash
  mkdir build
  cd build
  meson setup
  ninja
  sudo ninja install
```
