# Changelog — CadmusPwr macOS

---

## [1.0.0] — 2026

### Added
- Package power via Intel RAPL (`processor.package_watts`) — always a real measurement
- Sub-domain power (CORES / GPU / DRAM / UNCORE) — real RAPL values when available; activity-ratio estimates on Intel laptops, marked with `~`
- TDP percentage from `smc.cpu_die_power_target`
- CPU die and GPU die temperature from SMC sensors
- **Dual fan support** — left (Fan L) and right (Fan R) fans displayed separately using `smc.fan_0` / `smc.fan_1`, with single-fan fallback
- Per-core frequency and utilisation for all 16 logical CPUs (8 physical × HyperThreading) via `duty_cycles[0]`
- Per-core estimated power distributed by `util × freq²` from RAPL budget
- 60-second rolling graphs for package power, temperature, and utilisation
- Click-to-zoom: any graph expands full-width at 200 px
- Per-core heatmap view (colour grid) toggled from toolbar
- Dark / light theme toggle with full colour palette swap
- Pause / resume data collection
- Adjustable refresh rate: 250 ms / 500 ms / 1 s / 2 s
- `AppConstants.swift` — single file for app name, version, all magic numbers
- `Makefile` with `make`, `make install`, `make sudoers`, `make debug`, `make clean`
- MIT licence

### Fixed
- **False throttle positive** — `cpu_thermal_level = 25` was incorrectly triggering the warning. Throttle now only fires on `cpu_prochot=1`, `thermal_pressure≠Nominal`, or `cpu_die≥100°C`
- **Sub-domains not showing** — `cpu_watts`/`gpu_watts` are absent on this Intel config; fallback estimation from `cores_active_ratio` and `gpu_active_ratio` now fills the breakdown
- **`deinit` added** to `PowerMetricsReader` — subprocess is always terminated when the object is released, preventing zombie processes
- **Buffer size cap** added to `PowerMetricsReader` — prevents memory growth if plist parsing fails repeatedly
- **`pipe?.fileHandleForReading.readabilityHandler = nil`** set on `stop()` — prevents the handler from firing after process termination
- `tempR/G/B` computed once per render cycle instead of three times
- `_ = weights` suppressed spurious unused-variable warning in release builds
