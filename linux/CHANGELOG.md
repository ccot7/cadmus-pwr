# Changelog — CadmusPwr Linux

---

## [1.0.0] — 2026-04-08

Initial release.

### Added

- Package, core, DRAM, and uncore power via Intel RAPL (`/sys/class/powercap/intel-rapl:*`) — all sub-domains are direct hardware readings, no estimation
- TDP watts and TDP% from `constraint_0_power_limit_uw`
- Per-core frequency from `cpufreq/scaling_cur_freq` for all logical CPUs
- Per-core utilisation from `/proc/stat` diffed between samples
- Per-core estimated power distributed by `util × (freq / max_freq)²` from RAPL core budget
- CPU temperature from all matching thermal zones (`x86_pkg_temp`, `acpitz`, `cpu-thermal`)
- Thermal throttle detection and warning banner — triggers at 100 °C
- 60-second rolling graphs for package power, temperature, and utilisation with HUD aesthetic (glow, scanlines, corner brackets)
- Click-to-zoom: any graph expands to full width
- Per-core row view with segmented VU-meter bars
- Per-core heatmap view with green → amber → red interpolation
- Dark / light theme toggle with full CSS hot-swap via GString
- Pause / resume data collection
- Adjustable refresh rate: 250 ms / 500 ms / 1 s / 2 s via toolbar slider
- `APP_NAME` / `APP_VERSION` / `APP_SUBTITLE` constants — rename the app in one place
- `PATH_BUF` constant — all sysfs path buffers use one consistent size (512 bytes)
- `<inttypes.h>` / `SCNu64` format macros for correct portable `uint64_t` scanning from `/proc/stat`
- `Makefile` with `make`, `make install`, `make desktop`, `make update-icon-cache`, `make udev`, `make uninstall`, `make clean`, `make help`
- `make udev` writes `/etc/udev/rules.d/99-rapl.rules` for permanent RAPL permissions
- `.build/` hidden build directory — prevents Spotlight/IDE indexing of intermediate files
- MIT licence

### Fixed (from development iterations)

- `CoreDrawData` heap allocations tracked in a `GPtrArray` and freed in `on_window_destroy` — no memory leak on exit
- `GtkCssProvider` properly `g_object_unref`'d on window destroy
- Timer correctly stopped via `g_source_remove` before `gtk_main_quit`
- `apply_theme` uses `GString` instead of a fixed-size buffer — safe against long CSS rule additions
- `closedir` always called in all `discover_*` functions regardless of loop exit path
- All `strncpy` calls correctly NUL-terminate the destination buffer
- `gtk_widget_destroy` called on error dialog after `gtk_dialog_run`
- Inline Makefile comment removed from `Icon=` echo line — was being written literally into the `.desktop` file, breaking icon resolution
- `StartupWMClass` added to generated `.desktop` — fixes duplicate icon issue in GNOME
