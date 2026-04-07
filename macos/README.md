# CadmusPwr — macOS

A native SwiftUI macOS app for monitoring Intel CPU power, temperature, frequency, and utilisation in real time. 

Inspired by Intel Power Gadget. Reads hardware data via Apple's `powermetrics` tool, which accesses the SMC (System Management Controller) directly — no kernel extensions, no third-party drivers.

> Developed and tested on **MacBookPro15,3 (Intel Core i9-9980HK, 2019)**.
>
> Works on other Intel Macs and "Should (untested)" work on Apple Silicon (Apple Silicon exposes richer sub-domain data).

---

## Features

- **Package power** via Intel RAPL (always a real hardware measurement)
- **Sub-domain breakdown** — CORES / GPU / DRAM / UNCORE (real RAPL values when available; activity-ratio estimates on Intel laptops, clearly marked with `~`)
- **TDP %** from `smc.cpu_die_power_target`
- **CPU and GPU die temperatures** from SMC sensors
- **Dual fan speeds** — left and right fans displayed separately (Fan L / Fan R)
- **Thermal load level** — 0–100 index
- **Accurate throttle detection** — only triggers on `cpu_prochot=1`, `thermal_pressure≠Nominal`, or die temp ≥ 100 °C. No false positives.
- **Per-core frequency and utilisation** — 16 logical CPUs (8 physical × HyperThreading) from `duty_cycles[0]` finest window
- **Per-core estimated power** — RAPL budget distributed by `util × freq²`
- **60-second rolling graphs** with glow, scanlines, corner brackets
- **Click-to-zoom** any graph to full width
- **Heatmap view** — colour grid showing all cores at a glance
- **Dark / light theme** toggle
- **Pause / resume** data collection
- **Adjustable refresh rate** — 250 ms / 500 ms / 1 s / 2 s

---

## Requirements

| Requirement | Details |
|---|---|
| macOS | 13.0 Ventura or later |
| Xcode | 14.0 or later |
| CPU | Intel Mac (also works on Apple Silicon) |
| Swift | 5.7+ (bundled with Xcode 14+) |

---

## Project structure

```
macos/
├── Makefile
├── CadmusPwr.xcodeproj
└── CadmusPwr/
    ├── AppConstants.swift        ← App name, version, all configurable constants
    ├── CadmusPwrApp.swift        ← @main entry point
    ├── ContentView.swift         ← Full window layout and all sub-views
    ├── PowerMetricsReader.swift  ← Subprocess management + plist parsing
    ├── HUDGraphView.swift        ← Canvas-based rolling graph
    ├── CoreViews.swift           ← Per-core row view and heatmap
    ├── Theme.swift               ← Dark / light colour palettes
    ├── Assets.xcassets/          ← Place AppIcon.appiconset here
    └── CadmusPwr.entitlements    ← Hardened runtime entitlements
```

---

## Build

### Option A — Makefile (recommended)

The Makefile wraps `xcodebuild` so you can build and install without opening Xcode. It expects `CadmusPwr.xcodeproj` in the same directory.

```bash
# One-time setup — write the sudoers entry (see Permissions section)
make sudoers

# Build a Release .app into .build/Release/
make

# Build and copy to /Applications
make install

# Build a Debug .app (faster, keeps debug symbols)
make debug

# Remove local .build/ and system DerivedData
make clean

# Remove the app from /Applications
make uninstall

# Print all available targets
make help
```

### Makefile target reference

| Target | What it does |
|---|---|
| `make` | Build Release `.app` → `.build/Build/Products/Release/CadmusPwr.app` |
| `make debug` | Build Debug `.app` → `.build/Build/Products/Debug/CadmusPwr.app` |
| `make install` | Build Release then copy to `/Applications` |
| `make sudoers` | Write `/etc/sudoers.d/CadmusPwr` at chmod 400 |
| `make clean` | Delete `.build/` and system DerivedData |
| `make uninstall ` | Remove `CadmusPwr.app` from  `/Applications` |
| `make help` | List targets |

### Option B — Xcode GUI

1. Open `CadmusPwr.xcodeproj`
2. Select the **CadmusPwr** scheme and your Mac as the destination
3. Press `Cmd+R` to build and run

---

## Permissions — sudoers setup

`powermetrics` requires root to read SMC power counters. The app spawns `sudo powermetrics` internally. You need a sudoers entry so it runs without a password prompt.

### Using make (easiest)

```bash
make sudoers
```

### Manual setup

```bash
sudo visudo -f /etc/sudoers.d/CadmusPwr
```

Add this line, replacing `YOUR_USERNAME` with your macOS username (`whoami` to check):

```
YOUR_USERNAME ALL=(ALL) NOPASSWD: /usr/bin/powermetrics
```

Save and set permissions:

```bash
sudo chmod 400 /etc/sudoers.d/CadmusPwr
```

Verify (should run without asking for a password):

```bash
sudo powermetrics --samplers cpu_power,smc,thermal -i 1000 -n 1 --format plist
```

**Why chmod 400?** 

sudo requires sudoers files to be non-writable by group and others. `400` (owner read-only) is the strictest safe value. `600` causes sudo to reject the file with a "bad permissions" error.

---

## Adding your app icon

1. In Xcode open `Assets.xcassets` → `AppIcon`
2. Drag your 1024×1024 PNG into the macOS icon slot
3. Xcode generates all required sizes at build time

### Pinning to Dock

After `make install`:

1. Open Finder → Applications → CadmusPwr and double-click to launch
2. Right-click the Dock icon → Options → Keep in Dock

---

## What it measures

### Power

| Field | Source | Notes |
|---|---|---|
| Package (W) | `processor.package_watts` | Always a real RAPL reading |
| ~CORES (W) | `packages[0].cpu_watts` or estimated | Estimated on most Intel laptops |
| ~GPU (W) | `packages[0].gpu_watts` or estimated | Estimated on most Intel laptops |
| ~DRAM (W) | Estimated | Not exposed by powermetrics on this config |
| ~UNCORE (W) | Estimated | Not exposed by powermetrics on this config |
| TDP (W) | `smc.cpu_die_power_target` | 98 W on i9-9980HK |

Values with `~` are estimates using `cores_active_ratio` and `gpu_active_ratio` from the plist. Package power is always a direct hardware measurement.

### Temperature and fans

| Field | SMC key | Notes |
|---|---|---|
| CPU Die | `smc.cpu_die` | Main CPU thermal sensor |
| GPU Die | `smc.gpu_die` | Integrated GPU sensor |
| Fan L | `smc.fan_0` (fallback: `smc.fan`) | Left fan on MBP15,3 |
| Fan R | `smc.fan_1` | Right fan (hidden if absent) |

### Per-core data

Each of the 16 logical CPUs reports frequency from `freq_hz` and utilisation from `duty_cycles[0]` (the finest 16 µs measurement window). Logical CPU id = physical core × 2 + thread index.

### Throttle detection

The banner only appears when one of these is true:

| Condition | Meaning |
|---|---|
| `smc.cpu_prochot == 1` | Hardware PROCHOT asserted — definitive throttle |
| `thermal_pressure != "Nominal"` | OS thermal event escalated |
| `smc.cpu_die >= 100 °C` | At thermal junction limit |

`cpu_thermal_level = 25` is a normal load level and never triggers the banner.

---

## Troubleshooting

**Error banner on launch** — sudoers entry is missing or has wrong permissions. Run `make sudoers`.

**Sub-domains show `~`** — expected on Intel laptops. Package total is always real; sub-domains are estimates because this macOS version doesn't expose RAPL sub-domains via powermetrics.

**Only one fan shown** — your Mac exposes fans under the single `smc.fan` key rather than `fan_0`/`fan_1`. The app falls back to showing one fan labelled "Fan".

**Per-core util always 0** — `duty_cycles` structure may differ on your macOS version. Open an issue with the output of `sudo powermetrics --samplers cpu_power --format plist -i 1000 -n 1 | head -300`.

**Running in a VM** — `powermetrics` is unavailable in VMs. A physical Mac is required.

---

## What else could be added

- **Energy since launch** — integrate watts over time to show Wh consumed
- **CSV / JSON export** — log timestamped readings to a file
- **Menu bar mode** — live package-watts or temp in the menu bar without opening the full window
- **Configurable alert thresholds** — notify when power exceeds X watts for Y seconds
- **macOS Notification Center** integration for throttle events
- **Historical session comparison** — overlay today's run against a saved baseline
- **Apple Silicon richer data** — cluster-level power (P-cores vs E-cores), Neural Engine watts

---

## Changelog

See [CHANGELOG_macos.md](CHANGELOG_macos.md).

---

## License

MIT — see [LICENSE](https://github.com/ccot7/cadmus-pwr/blob/main/LICENSE).
