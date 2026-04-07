// Created by Cadmus of Tyre (@ccot7) on 4/7/26.
// PowerMetricsReader.swift
// ─────────────────────────────────────────────────────────────────────────────
// Spawns `sudo powermetrics` as a background subprocess, streams its plist
// output, and publishes a parsed `SystemSnapshot` on the main thread.
//
// Confirmed plist structure for Intel MacBookPro15,3 (i9-9980HK):
//
//   plist
//   ├── processor
//   │   ├── package_watts           — total socket power  (always present)
//   │   ├── freq_hz                 — average package frequency
//   │   └── packages[]              — one entry per CPU socket
//   │       └── packages[0]
//   │           ├── cores_active_ratio  — fraction of physical cores busy (0–1)
//   │           ├── gpu_active_ratio    — GPU activity fraction (0–1)
//   │           └── cores[]             — physical cores (8 on i9-9980HK)
//   │               └── cores[i]
//   │                   └── cpus[]      — logical threads (2 per core = HT)
//   │                       └── cpus[j]
//   │                           ├── freq_hz             — current Hz
//   │                           └── duty_cycles[]       — array of time windows
//   │                               └── [0]  finest (16 µs)
//   │                                   ├── active_count
//   │                                   └── idle_count
//   ├── smc                         — System Management Controller sensors
//   │   ├── cpu_die                 — CPU die temp °C
//   │   ├── gpu_die                 — GPU die temp °C
//   │   ├── fan_0                   — left  fan RPM  (or "fan" on some builds)
//   │   ├── fan_1                   — right fan RPM
//   │   ├── cpu_die_power_target    — TDP ceiling in watts (98 W here)
//   │   ├── cpu_prochot             — 1 = hardware PROCHOT asserted (throttling)
//   │   └── cpu_thermal_level       — load index 0–100 (NOT a throttle flag)
//   └── thermal_pressure            — "Nominal" | "Moderate" | "Heavy" | "Trapping"
// ─────────────────────────────────────────────────────────────────────────────

import Foundation
import Combine

// MARK: - Data model

/// Represents one logical CPU (a HyperThreading thread on Intel).
struct CoreInfo: Identifiable {
    let id:             Int
    var freqMHz:        Double   // current clock in MHz
    var utilPct:        Double   // 0–100, from duty_cycles[0]
    var estimatedWatts: Double   // proportional RAPL budget share
}

/// One named temperature sensor.
struct ThermalZone: Identifiable {
    let id:    Int
    var name:  String
    var tempC: Double
}

/// Complete snapshot of the machine state for one sample interval.
struct SystemSnapshot {

    var cpuModel: String = ""

    // ── Power ─────────────────────────────────────────────────────────────────
    /// Total package power from RAPL (always a real measurement).
    var pkgWatts:   Double = 0
    /// CPU core sub-domain. Real if hardware exposes it; estimated otherwise.
    var coreWatts:  Double = 0
    /// iGPU sub-domain. Real if hardware exposes it; estimated otherwise.
    var gpuWatts:   Double = 0
    /// DRAM sub-domain. Usually estimated on Intel laptops.
    var dramWatts:  Double = 0
    /// Uncore fabric. Usually estimated on Intel laptops.
    var uncoreWatts: Double = 0
    /// `true` when sub-domain values are heuristic estimates, not RAPL reads.
    var subDomainsEstimated: Bool = false
    /// TDP ceiling in watts, from `smc.cpu_die_power_target`.
    var tdpWatts:   Double = 0
    /// `pkgWatts / tdpWatts * 100`, or 0 if TDP is unknown.
    var tdpPct:     Double = 0

    // ── Thermal ───────────────────────────────────────────────────────────────
    var fan0RPM:      Double = 0   // left  fan (or only fan if machine has one)
    var fan1RPM:      Double = 0   // right fan (0 if not present)
    /// cpu_thermal_level: a load index 0–100, *not* a throttle flag.
    var thermalLevel: Int    = 0
    var cores:        [CoreInfo]    = []
    var zones:        [ThermalZone] = []
    /// True only when a genuine throttle event is detected (see apply()).
    var throttling:   Bool   = false

    // ── Rolling statistics ────────────────────────────────────────────────────
    var sampleCount:  UInt64 = 0
    var avgPkgWatts:  Double = 0
    var maxPkgWatts:  Double = 0

    // ── Graph history (ring buffer, length = AppConstants.historyLength) ───────
    var pkgHistory:  [Double] = Array(repeating: 0, count: AppConstants.historyLength)
    var tempHistory: [Double] = Array(repeating: 0, count: AppConstants.historyLength)
    var utilHistory: [Double] = Array(repeating: 0, count: AppConstants.historyLength)
    var historyPos:  Int      = 0
    var historyCount: Int     = 0
}

// MARK: - Reader

/// Owns the `powermetrics` subprocess and publishes parsed snapshots.
/// Mark `final` so the compiler can devirtualise method calls.
final class PowerMetricsReader: ObservableObject {

    // Published properties — always mutated on the main thread.
    @Published var snapshot = SystemSnapshot()
    @Published var error: String? = nil

    // ── Private state ─────────────────────────────────────────────────────────
    private var process:    Process?
    private var pipe:       Pipe?
    /// Accumulation buffer for partial plist data between read() callbacks.
    private var buffer:     Data = Data()
    private var intervalMs: Int  = AppConstants.refreshPresets[AppConstants.defaultRefreshIndex]
    private var isRunning        = false
    private let cpuModelStr:     String

    /// Maximum bytes we allow the buffer to grow before discarding stale data.
    /// Guards against memory growth if plist parsing fails repeatedly.
    private static let maxBufferBytes = 512 * 1024   // 512 KB

    // MARK: - Init / deinit

    init() {
        cpuModelStr = Self.readCPUModel()
    }

    /// Ensure the subprocess is always cleaned up when this object is released.
    deinit {
        stop()
    }

    // MARK: - Lifecycle

    /// Start sampling at the given interval (milliseconds).
    func start(intervalMs: Int = 1000) {
        self.intervalMs = intervalMs
        guard !isRunning else { return }
        isRunning = true
        launchProcess()
    }

    /// Stop sampling and terminate the subprocess.
    func stop() {
        isRunning = false
        process?.terminate()
        process = nil
        // Closing the pipe's read end prevents the readabilityHandler
        // from firing again after the process exits.
        pipe?.fileHandleForReading.readabilityHandler = nil
        pipe = nil
        buffer.removeAll()
    }

    /// Stop and restart with a new interval — used by the refresh slider.
    func restart(intervalMs: Int) {
        stop()
        self.intervalMs = intervalMs
        // Brief delay avoids a race between terminate() and the next launch.
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) { [weak self] in
            self?.isRunning = true
            self?.launchProcess()
        }
    }

    // MARK: - Process management

    private func launchProcess() {
        let p  = Process()
        let pp = Pipe()
        process = p
        pipe    = pp
        buffer.removeAll()

        p.executableURL = URL(fileURLWithPath: "/usr/bin/sudo")
        p.arguments = [
            "/usr/bin/powermetrics",
            "--format",   "plist",
            // cpu_power: package/core/GPU watts + per-core duty cycles + freq
            // smc:       die temps, fan RPM, power targets, prochot flag
            // thermal:   thermal_pressure string
            "--samplers", "cpu_power,smc,thermal",
            "-i",         "\(intervalMs)",
        ]
        p.standardOutput = pp
        p.standardError  = FileHandle.nullDevice

        // Data arrives on a background thread; we accumulate and parse here.
        pp.fileHandleForReading.readabilityHandler = { [weak self] fh in
            guard let self else { return }
            let chunk = fh.availableData
            guard !chunk.isEmpty else { return }
            self.buffer.append(chunk)
            // Safety valve — discard if buffer grows unreasonably large.
            if self.buffer.count > Self.maxBufferBytes {
                self.buffer.removeAll()
            }
            self.tryParsePlist()
        }

        // Auto-restart on unexpected exit (sleep/wake, sudo expiry, etc.)
        p.terminationHandler = { [weak self] _ in
            guard let self, self.isRunning else { return }
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
                self.launchProcess()
            }
        }

        do {
            try p.run()
        } catch {
            DispatchQueue.main.async { [weak self] in
                self?.error =
                    "Failed to launch powermetrics: \(error.localizedDescription)\n\n" +
                    "Ensure /etc/sudoers.d/powermon exists and is chmod 400 — see README."
            }
        }
    }

    // MARK: - Plist streaming

    /// `powermetrics --format plist` emits one complete plist per sample,
    /// terminated by a NUL byte (0x00). We scan the accumulation buffer for
    /// NUL separators and dispatch each complete document for parsing.
    private func tryParsePlist() {
        while let sep = buffer.firstIndex(of: 0) {
            let chunk = Data(buffer[buffer.startIndex..<sep])
            buffer.removeSubrange(buffer.startIndex...sep)
            guard !chunk.isEmpty else { continue }
            parseSample(chunk)
        }
    }

    private func parseSample(_ data: Data) {
        guard let plist = try? PropertyListSerialization.propertyList(
            from: data, options: [], format: nil
        ) as? [String: Any] else { return }

        // Parsing is cheap; move straight to main thread for UI safety.
        DispatchQueue.main.async { [weak self] in
            self?.apply(plist: plist)
        }
    }

    // MARK: - Apply plist → snapshot

    private func apply(plist: [String: Any]) {
        // Work on a local copy — single atomic assignment at the end avoids
        // partial-update glitches in the SwiftUI diff engine.
        var snap = self.snapshot
        snap.cpuModel = cpuModelStr

        guard let proc     = plist["processor"] as? [String: Any],
              let packages = proc["packages"]   as? [[String: Any]],
              let pkg0     = packages.first
        else { return }

        // ── Package power ─────────────────────────────────────────────────────
        snap.pkgWatts = num(proc, "package_watts")

        // ── Sub-domain power ──────────────────────────────────────────────────
        // Try real RAPL keys inside packages[0]. On many Intel laptop configs
        // these are 0 or absent; fall back to activity-ratio estimation.
        let realCore   = num(pkg0, "cpu_watts")
        let realGpu    = num(pkg0, "gpu_watts")
        let realDram   = num(pkg0, "dram_watts")
        let realUncore = num(pkg0, "uncore_watts")

        if realCore > 0 || realGpu > 0 {
            // Hardware exposes the breakdown — use it directly.
            snap.coreWatts   = realCore
            snap.gpuWatts    = realGpu
            snap.dramWatts   = realDram
            snap.uncoreWatts = realUncore
            snap.subDomainsEstimated = false
        } else {
            // Estimate from activity ratios (both confirmed present on MBP15,3).
            // Budget fractions are typical for an Intel i9-9980HK under load.
            let coreActivity = num(pkg0, "cores_active_ratio")  // 0–1
            let gpuActivity  = num(pkg0, "gpu_active_ratio")    // 0–1
            let total        = snap.pkgWatts

            let coreEst   = total * 0.65 * coreActivity
            let gpuEst    = total * 0.15 * gpuActivity
            let dramEst   = total * 0.12 * max(coreActivity, 0.1)
            let uncoreEst = max(0, total - coreEst - gpuEst - dramEst)

            snap.coreWatts   = coreEst
            snap.gpuWatts    = gpuEst
            snap.dramWatts   = dramEst
            snap.uncoreWatts = uncoreEst
            snap.subDomainsEstimated = true
        }

        // ── SMC sensors ───────────────────────────────────────────────────────
        // Reset throttling here so it can only be set by the conditions below.
        snap.throttling = false

        if let smc = plist["smc"] as? [String: Any] {

            // TDP ceiling (98 W on this machine)
            let pTarget = num(smc, "cpu_die_power_target")
            if pTarget > 0 { snap.tdpWatts = pTarget }

            // ── Fans ──────────────────────────────────────────────────────────
            // Confirmed via SMC dump on MBP15,3: powermetrics only surfaces one
            // fan key ("fan"). fan_0 / fan_1 are NOT in the plist on this model.
            // The machine has two fans physically but only one is exposed here.
            // fan1RPM stays 0; ContentView shows a single "Fan (avg)" row.
            snap.fan0RPM = num(smc, "fan_0") > 0 ? num(smc, "fan_0") : num(smc, "fan")
            snap.fan1RPM = num(smc, "fan_1")

            // Thermal load index — a 0–100 percentage of thermal headroom used.
            // Do NOT use this as a throttle flag; 25 is perfectly normal.
            snap.thermalLevel = Int(num(smc, "cpu_thermal_level"))

            // ── Throttle detection ────────────────────────────────────────────
            // Only three conditions indicate genuine Intel throttling:
            //
            //   1. cpu_prochot == true  (hardware PROCHOT signal — definitive)
            //   2. thermal_pressure != "Nominal"  (OS thermal event)
            //   3. cpu_die >= 100 °C  (at thermal junction limit)
            //
            // cpu_prochot is a Bool in the plist (confirmed: "False" / "True").
            // Read it as Bool directly; NSNumber cast is unreliable for booleans.
            let prochot = smc["cpu_prochot"] as? Bool ?? false
            if prochot { snap.throttling = true }

            // Temperature zones
            var zones: [ThermalZone] = []
            let sensors: [(name: String, key: String)] = [
                ("CPU Die (hottest spot)", "cpu_die"),
                ("GPU Die", "gpu_die"),
            ]
            for (i, s) in sensors.enumerated() {
                let t = num(smc, s.key)
                guard t > 1, t < 125 else { continue }
                zones.append(ThermalZone(id: i, name: s.name, tempC: t))
                if t >= AppConstants.throttleTempC { snap.throttling = true }
            }
            if !zones.isEmpty { snap.zones = zones }
        }

        // thermal_pressure: OS-level indicator (separate from SMC block)
        if let pressure = plist["thermal_pressure"] as? String,
           pressure != "Nominal" {
            snap.throttling = true
        }

        if snap.tdpWatts > 0 {
            snap.tdpPct = (snap.pkgWatts / snap.tdpWatts) * 100.0
        }

        // ── Per-core utilisation + frequency ──────────────────────────────────
        // Physical cores → logical threads:
        //   packages[0].cores[i].cpus[j]
        //   logical id = i * 2 + j   (HyperThreading pairs each physical core)
        var cores: [CoreInfo] = []

        if let coreList = pkg0["cores"] as? [[String: Any]] {
            for (physIdx, physCore) in coreList.enumerated() {
                guard let cpuList = physCore["cpus"] as? [[String: Any]] else { continue }

                for (threadIdx, cpu) in cpuList.enumerated() {
                    let logicalId = physIdx * 2 + threadIdx
                    let freqMHz   = num(cpu, "freq_hz") / 1_000_000.0

                    // duty_cycles is an array of 12 measurement windows ranging
                    // from 16 µs (index 0) to ~32 ms (index 11).
                    //
                    // Using dcArr.first (16 µs) overestimates utilisation — a
                    // core that wakes briefly for an interrupt appears ~100% busy
                    // in that tiny window even if it was idle for the full second.
                    //
                    // Using dcArr.last (~32 ms) averages over a longer period and
                    // produces readings consistent with Activity Monitor and Intel
                    // Power Gadget (which use the full sample interval as their window).
                    let utilPct: Double = {
                        guard let dcArr = cpu["duty_cycles"] as? [[String: Any]],
                              let dc   = dcArr.last else { return 0 }
                        let active = num(dc, "active_count")
                        let idle   = num(dc, "idle_count")
                        let total  = active + idle
                        guard total > 0 else { return 0 }
                        return (active / total) * 100.0
                    }()

                    cores.append(CoreInfo(
                        id:             logicalId,
                        freqMHz:        freqMHz,
                        utilPct:        max(0, min(100, utilPct)),
                        estimatedWatts: 0
                    ))
                }
            }
        }

        cores.sort { $0.id < $1.id }
        if !cores.isEmpty { snap.cores = cores }
        distributeCorePower(snap: &snap)

        // ── Ring-buffer history ───────────────────────────────────────────────
        let h = AppConstants.historyLength
        snap.pkgHistory[snap.historyPos]  = snap.pkgWatts
        snap.tempHistory[snap.historyPos] = snap.zones.first?.tempC ?? 0

        let avgUtil = snap.cores.isEmpty ? 0.0 :
            snap.cores.map(\.utilPct).reduce(0, +) / Double(snap.cores.count)
        snap.utilHistory[snap.historyPos] = avgUtil

        snap.historyPos = (snap.historyPos + 1) % h
        if snap.historyCount < h { snap.historyCount += 1 }

        // ── Rolling stats ─────────────────────────────────────────────────────
        snap.sampleCount += 1
        snap.avgPkgWatts = (snap.avgPkgWatts * Double(snap.sampleCount - 1)
                            + snap.pkgWatts) / Double(snap.sampleCount)
        if snap.pkgWatts > snap.maxPkgWatts { snap.maxPkgWatts = snap.pkgWatts }

        // Single atomic publish — SwiftUI sees one consistent state change.
        self.snapshot = snap
        self.error    = nil
    }

    // MARK: - Per-core power distribution

    /// Distributes the RAPL core budget across logical CPUs weighted by
    /// `utilPct × (freq / maxFreq)²`, which approximates relative power draw.
    /// Falls back to equal shares when all cores are idle.
    private func distributeCorePower(snap: inout SystemSnapshot) {
        let budget = snap.coreWatts > 0 ? snap.coreWatts : snap.pkgWatts
        guard budget > 0, !snap.cores.isEmpty else { return }

        let maxFreq = snap.cores.map(\.freqMHz).max() ?? 1.0
        var weights = snap.cores.map { c -> Double in
            let fr = maxFreq > 0 ? c.freqMHz / maxFreq : 0
            return (c.utilPct / 100.0) * fr * fr
        }
        let total = weights.reduce(0, +)

        for i in snap.cores.indices {
            snap.cores[i].estimatedWatts = total > 0
                ? (weights[i] / total) * budget
                : budget / Double(snap.cores.count)
        }
        _ = weights  // suppress unused-variable warning
    }

    // MARK: - Helpers

    /// Safely extract a `Double` from any plist value type.
    private func num(_ dict: [String: Any], _ key: String) -> Double {
        guard let val = dict[key] else { return 0 }
        if let n = val as? NSNumber { return n.doubleValue }
        if let d = val as? Double   { return d }
        if let i = val as? Int      { return Double(i) }
        if let s = val as? String   { return Double(s) ?? 0 }
        return 0
    }

    // MARK: - CPU model (run once at init)

    /// Reads the CPU brand string via `sysctl` synchronously.
    /// Called once during `init()` — safe because it's fast.
    static func readCPUModel() -> String {
        let p  = Process()
        let pp = Pipe()
        p.executableURL  = URL(fileURLWithPath: "/usr/sbin/sysctl")
        p.arguments      = ["-n", "machdep.cpu.brand_string"]
        p.standardOutput = pp
        p.standardError  = FileHandle.nullDevice
        try? p.run()
        p.waitUntilExit()
        let data = pp.fileHandleForReading.readDataToEndOfFile()
        return String(data: data, encoding: .utf8)?
            .trimmingCharacters(in: .whitespacesAndNewlines) ?? "Unknown CPU"
    }
}
