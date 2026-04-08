# CadmusPwr — Linux

A native GTK3 desktop app for monitoring Intel CPU power, temperature, frequency, and utilization on Linux. Inspired by Intel Power Gadget.

Runs natively on Wayland (Fedora/GNOME) and X11. Zero polling daemons, no DBus — reads directly from `sysfs` and `procfs`.

---

## What it does

- Real-time CPU package power (RAPL)
- Per-core frequency monitoring
- CPU utilization per logical core
- Temperature monitoring (all available thermal zones)
- 60-second rolling history graphs
- Power, temperature, and utilization visualization
- Lightweight GTK3 native desktop UI

---

## UI Look & Feel

- Dark/White themed desktop window, resizable
- Live-updating graphs for package power, temperature, and CPU utilization (60-second history)
- Per-core frequency and utilization bars for all logical CPUs
- Package watts, core watts, DRAM watts, TDP%, avg, and peak power
- All thermal zones that expose CPU temperature

---

## Requirements

| Requirement | Details |
|---|---|
| OS | Any Linux distro |
| Kernel | 3.13+ (RAPL), 4.x+ recommended |
| CPU | Intel with RAPL support (Sandy Bridge 2011+) |
| Desktop | GNOME / Wayland / X11 |
| GTK | GTK3 |
| Compiler | GCC or Clang |
| Permissions | See section below |

---

## Install dependencies

GTK3 is already installed on your Fedora system as part of GNOME. You just need the development headers:

```bash
# Fedora
sudo dnf install gtk3-devel

# Ubuntu / Debian
sudo apt install libgtk-3-dev

# Arch
sudo pacman -S gtk3
```

---

## Build & Install (Makefile — recommended)

### Build
make

### Install binary
make install

Installs to:
~/.local/bin/CadmusPwr

### Install desktop launcher
make desktop

Then search CadmusPwr in GNOME Activities.

### Fix RAPL permissions (run ONCE only, after that no need to do it again)
make udev

---

## Cleanup

make uninstall
make clean

---

## Makefile targets

- make → build .build/CadmusPwr
- make install → install binary to ~/.local/bin
- make desktop → install GNOME launcher + icon
- make udev → install permanent RAPL permissions
- make uninstall → remove app + launcher
- make clean → remove build folder

---

## Run

```bash
CadmusPwr

or

~/.local/bin/CadmusPwr
```

---

## Permissions (IMPORTANT)

RAPL energy counters require elevated read access ie access to:
/sys/class/powercap/intel-rapl:*/

## ✅ Recommended fix: udev rule (clean, permanent)

This is the correct system-wide solution and works with desktop launchers.

```
make udev
```

---
## Why this is needed

The app reads:

- `/sys/class/powercap/...` → restricted
- `/proc/stat` → OK
- `/sys/class/thermal/...` → usually OK

Without permissions:

- CPU usage works
- Temperature may work
- ❌ Power shows 0.00 W

---

## Feature & What it measures

### Package Power (RAPL)

Source: `/sys/class/powercap/intel-rapl:*/energy_uj`

RAPL (Running Average Power Limit) reads hardware energy counters. The app calculates watts by diffing readings over time.

| Domain | What it covers |
|---|---|
| Package | Entire CPU socket |
| Core | CPU cores + L1/L2 cache |
| DRAM | Memory controller + RAM |
| Uncore | iGPU and fabric (if present) |

TDP% is calculated from: `/sys/class/powercap/intel-rapl:0/constraint_0_power_limit_uw`

---

### Per-core Frequency

Source: `/sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq`

Current frequency reported by the CPU frequency governor for each logical core, in GHz.

---

### CPU Utilization

Source: `/proc/stat`

Per-CPU counters diffed between samples. 
Formula:

```
util% = 100 × (1 − Δidle / Δtotal)
```

---

### Temperature

Source: `/sys/class/thermal/thermal_zone*/temp`

All zones matching `x86_pkg_temp`, `acpitz`, or `cpu-thermal`. Reported in °C.

---

### Graphs

All three graphs show a 60-second rolling history. The Y-axis scales automatically:
- Power graph scales to TDP (or peak seen, whichever is higher)
- Temperature graph scales to 100°C
- Utilization graph scales to 100%

---

## Color coding

| Color | Meaning |
|---|---|
| Green | Low / healthy |
| Yellow | Moderate |
| Red | High / hot |
| Blue | Accent / power graph line |
| Cyan | Frequency labels |
| Muted gray | Secondary info, labels |

Thresholds: power < 30W = green, 30–60W = yellow, > 60W = red. Temperature < 60°C = green, 60–80°C = yellow, > 80°C = red.

---

## Troubleshooting

### Error dialog: "No RAPL power domains found"

The app shows a dialog and exits. Fix with one of the permission options above, then rerun.

To verify RAPL is available on your system:

```bash
ls /sys/class/powercap/
# Should show: intel-rapl:0  intel-rapl:0:0  etc.
```

If that directory is empty, load the kernel modules:

```bash
sudo modprobe intel_rapl_common
sudo modprobe intel_rapl_msr
```

---

### "pkg-config: command not found" at build time

```bash
# Fedora
sudo dnf install pkgconfig

# Ubuntu
sudo apt install pkg-config
```

---

### No temperature displayed

Some systems don't expose the expected thermal zone types. Check what's available:

```bash
for z in /sys/class/thermal/thermal_zone*/; do
  echo "$z: $(cat $z/type) = $(cat $z/temp)"
done
```

If you see zones with different type names, the source code can be adjusted — look for the `discover_thermal()` function and add your zone type to the `strstr` checks.

---

### Running in a VM - Issue

RAPL is generally not available inside virtual machines. The app will show the error dialog and exit. This is a hardware limitation — the hypervisor blocks direct energy counter access. Running on a real physical machine is required.

---

### Window appears but power shows 0.00 W

This usually means the energy counter files are readable but returning 0. Double-check permissions:

```bash
cat /sys/class/powercap/intel-rapl:0/energy_uj
# Should print a large number like 12345678901
```

If that returns 0 or permission denied, revisit the permissions section.

---

## Manual build (Legacy - optional fallback)

## Build

```bash
gcc -O2 -o CadmusPwr CadmusPwr.c $(pkg-config --cflags --libs gtk+-3.0)
```

The `$(pkg-config --cflags --libs gtk+-3.0)` part automatically finds and links the GTK3 headers and libraries on your system. You don't need to know where they are — `pkg-config` handles it.

### Optional: strip for smaller binary

```bash
gcc -O2 -s -o CadmusPwr CadmusPwr.c $(pkg-config --cflags --libs gtk+-3.0)
```

### With Clang

```bash
clang -O2 -o CadmusPwr CadmusPwr.c $(pkg-config --cflags --libs gtk+-3.0)
```

---

## Permission: ✅ Recommended fix: udev rule (clean, permanent)

This is the correct system-wide solution and works with desktop launchers.

### 1. Create the rule

```bash
sudo nano /etc/udev/rules.d/99-rapl.rules
```

Paste:

```bash
SUBSYSTEM=="powercap", ACTION=="add", RUN+="/bin/chmod o+r /sys/class/powercap/%k/energy_uj"
```

### 2. Reload rules

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

### 3. Reboot (recommended)

Some systems (especially Fedora) require a reboot for full effect.

### 4. Test

```bash
cat /sys/class/powercap/intel-rapl:0/energy_uj
```

If it prints a number → ✅ good

---

## Desktop icon & app launcher

You can make CadmusPwr appear in your GNOME app launcher and dock like any other installed app.

### Step 1 — Move the binary somewhere permanent

After building, move the compiled binary out of wherever you built it:

```bash
mkdir -p ~/.local/bin
mv CadmusPwr ~/.local/bin/
```

---

### Step 2 — Create the desktop entry

```bash
nano ~/.local/share/applications/CadmusPwr.desktop
```

Paste this in, replacing `YOUR_USERNAME` with your actual username (run `whoami` if unsure):

```ini
[Desktop Entry]
Name=CadmusPwr
Comment=CPU Power Monitor
Exec=/home/YOUR_USERNAME/.local/bin/CadmusPwr
Icon=utilities-system-monitor
Terminal=false
Type=Application
Categories=Utilities;System;Monitor;
```

Save and exit (`Ctrl+O`, `Enter`, `Ctrl+X` in nano).

---

### Step 3 — Register it with GNOME

```bash
update-desktop-database ~/.local/share/applications/
```

CadmusPwr will now appear when you search in GNOME Activities. Right-click it and select "Pin to Dash" to add it to your dock.

---

### Custom icon (optional)

Replace the `Icon=` line in the `.desktop` file with a path to any `.png` image:

```ini
Icon=/LOCATION_YOU_SEE_FIT/CadmusPwr.png
```

---

### Note on recompiling

If you ever rebuild the binary, just copy it back to `~/.local/bin/` again:

```bash
gcc -O2 -o CadmusPwr CadmusPwr.c $(pkg-config --cflags --libs gtk+-3.0)
cp CadmusPwr ~/.local/bin/CadmusPwr
```

The `.desktop` file stays the same and doesn't need to be touched.

---

## Changelog

See [CHANGELOG_macos.md](CHANGELOG.md).

---

## License

MIT — see [LICENSE](https://github.com/ccot7/cadmus-pwr/blob/main/LICENSE).
