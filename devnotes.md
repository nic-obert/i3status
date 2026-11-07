

## Fix I3STATUS_VERSION
`config.h` contains the definition. There should only be one authority for the package version

## Remove libconfuse dependency
Use JSON and yajl for config file parsing. 

New config file format:
```JSON
{
    // General options
    "general": {
      "interval": 5
    },
    // Ordered map of sections to be shown in the status bar
    "order": {
        "wireless": {
            "interface": "_first_",
            "format_up": "W: (%quality at %essid) %ip",
            "format_down": "W: down"
        },
        "ethernet": {
            "interface": "_first_",
            "format_up": "E: %ip (%speed)",
            "format_down": "E: down"
        },
        "volume": {
            "title": "master",
            "format": "🔊: %volume",
            "format_muted": "🔇: %volume",
            "device": "pulse"
        },
        "battery": {
            "batno": "all", // Expected a positive integer, or "all" for -1
            "format": "%status %percentage %remaining %consumption",
            "format_percentage": "%.00f%s",
            "status_chr": "CHR",
            "status_bat": "BAT",
            "status_unk": "UNK",
            "status_full": "FULL",
            "status_idle": "IDLE",
            "last_full_capacity": true,
            "low_threshold": 20,
            "threshold_type": "percentage"
        },
        "load": {
            "format": "%1min"
        },
        "memory": {
            "format": "%used / %total"
        },
        "tztime": {
            "title": "local",
            "format": "%Y-%m-%d %H:%M "
        }
    }
}
```