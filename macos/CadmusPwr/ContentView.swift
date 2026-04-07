// Created by Cadmus of Tyre (@ccot7) on 4/7/26.
// ContentView.swift
// ─────────────────────────────────────────────────────────────────────────────
// Root SwiftUI view. Composes the entire window from smaller sub-views:
//   headerBar      — app name + CPU model
//   toolbarBar     — pause, refresh slider, heatmap toggle, theme toggle
//   errorBanner    — shown when powermetrics fails to launch
//   throttleBanner — shown when genuine CPU throttling is detected
//   topMetricCards — power / temperature / utilisation summary cards
//   graphsSection  — 3-up rolling graphs + click-to-zoom overlay
//   coresSection   — per-core rows or heatmap (toggled from toolbar)
//   statusBar      — sample count, core count, refresh rate
// ─────────────────────────────────────────────────────────────────────────────

import SwiftUI

// MARK: - Zoom state

/// Which graph (if any) is currently expanded full-width.
enum ZoomTarget { case none, power, temp, util }

// MARK: - ContentView

struct ContentView: View {

    // The data source — @StateObject keeps it alive for the view's lifetime.
    @StateObject private var reader = PowerMetricsReader()

    // ── UI state ──────────────────────────────────────────────────────────────
    @State private var paused      = false
    @State private var refreshIdx  = AppConstants.defaultRefreshIndex
    @State private var darkTheme   = true
    @State private var heatmapMode = false
    @State private var zoom: ZoomTarget = .none

    // Convenience shorthands recomputed on every render (cheap value types).
    private var theme: HUDTheme      { darkTheme ? .dark : .light }
    private var snap:  SystemSnapshot { reader.snapshot }

    // MARK: - Body

    var body: some View {
        VStack(spacing: 0) {
            headerBar
            toolbarBar

            // Error banner only when powermetrics can't launch
            if let err = reader.error { errorBanner(err) }

            // Throttle banner only on real throttle events
            if snap.throttling { throttleBanner }

            ScrollView {
                VStack(spacing: 0) {
                    topMetricCards
                    graphsSection
                    coresSection
                }
                .padding(8)
            }

            statusBar
        }
        .background(theme.bg0)
        .frame(minWidth: 860, minHeight: 680)
        .onAppear {
            reader.start(intervalMs: AppConstants.refreshPresets[refreshIdx])
        }
        // React to pause/resume toggle
        .onChange(of: paused) { _ in
            if paused { reader.stop() }
            else      { reader.start(intervalMs: AppConstants.refreshPresets[refreshIdx]) }
        }
        // React to refresh rate slider changes
        .onChange(of: refreshIdx) { _ in
            guard !paused else { return }
            reader.restart(intervalMs: AppConstants.refreshPresets[refreshIdx])
        }
    }

    // MARK: - Header bar

    /// Shows the app name (from AppConstants) and the detected CPU model.
    var headerBar: some View {
        HStack {
            HStack(spacing: 0) {
                // App name — comes from AppConstants.appName so renaming
                // the app requires only one edit in AppConstants.swift.
                Text(AppConstants.appName)
                    .font(.system(size: 16, weight: .bold, design: .monospaced))
                    .foregroundColor(theme.cyan)
                Text("  //  \(AppConstants.appSubtitle)")
                    .font(.system(size: 10, design: .monospaced))
                    .foregroundColor(theme.textDim)
            }
            Spacer()
            Text(snap.cpuModel)
                .font(.system(size: 11, design: .monospaced))
                .foregroundColor(theme.textDim)
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
        .background(theme.bg1)
        .overlay(alignment: .bottom) {
            Rectangle().fill(theme.border).frame(height: 1)
        }
    }

    // MARK: - Toolbar

    /// Pause/resume · refresh rate slider · heatmap toggle · theme toggle.
    var toolbarBar: some View {
        HStack(spacing: 10) {

            hudButton(paused ? "RESUME" : "PAUSE") { paused.toggle() }

            Divider().frame(height: 20).background(theme.border)

            // Refresh rate: 250ms / 500ms / 1 000ms / 2 000ms
            Text("REFRESH: \(AppConstants.refreshPresets[refreshIdx])ms")
                .font(.system(size: 10, design: .monospaced))
                .foregroundColor(theme.textDim)
                .frame(width: 140, alignment: .leading)

            Slider(
                value: Binding(
                    get: { Double(refreshIdx) },
                    set: { refreshIdx = Int($0.rounded()) }
                ),
                in: 0...Double(AppConstants.refreshPresets.count - 1),
                step: 1
            )
            .frame(width: 110)
            .accentColor(theme.cyan.opacity(0.8))
            .help("250ms / 500ms / 1 000ms / 2 000ms")

            Divider().frame(height: 20).background(theme.border)

            hudButton(heatmapMode ? "ROW VIEW" : "HEATMAP") {
                heatmapMode.toggle()
            }

            Spacer()

            Text("click graph to zoom  //")
                .font(.system(size: 10, design: .monospaced))
                .foregroundColor(theme.textDim.opacity(0.5))

            hudButton(darkTheme ? "LIGHT" : "DARK") {
                withAnimation(.easeInOut(duration: 0.25)) { darkTheme.toggle() }
            }
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 5)
        .background(theme.bg1)
        .overlay(alignment: .bottom) {
            Rectangle().fill(theme.border).frame(height: 1)
        }
    }

    // MARK: - Banners

    /// Red banner shown when `powermetrics` cannot be launched.
    func errorBanner(_ msg: String) -> some View {
        HStack {
            Image(systemName: "exclamationmark.triangle.fill")
                .foregroundColor(theme.red)
            Text(msg)
                .font(.system(size: 11, design: .monospaced))
                .foregroundColor(theme.text)
                .lineLimit(4)
            Spacer()
        }
        .padding(10)
        .background(theme.red.opacity(0.12))
        .overlay(alignment: .bottom) {
            Rectangle().fill(theme.red).frame(height: 1)
        }
    }

    /// Warning banner shown only when genuine CPU throttling is active.
    /// (Requires cpu_prochot=1, thermal_pressure≠Nominal, or cpu_die≥100 °C.)
    var throttleBanner: some View {
        HStack {
            Image(systemName: "thermometer.sun.fill")
                .foregroundColor(theme.red)
            Text("THERMAL THROTTLING DETECTED  —  CPU is reducing clock speed. Check cooling.")
                .font(.system(size: 11, weight: .bold, design: .monospaced))
                .foregroundColor(theme.red)
            Spacer()
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(theme.red.opacity(0.10))
        .overlay(alignment: .top)    { Rectangle().fill(theme.red).frame(height: 1) }
        .overlay(alignment: .bottom) { Rectangle().fill(theme.red).frame(height: 1) }
    }

    // MARK: - Top metric cards

    var topMetricCards: some View {
        HStack(alignment: .top, spacing: 0) {
            powerCard
            tempCard
            utilCard
        }
    }

    // ── Power card ────────────────────────────────────────────────────────────

    var powerCard: some View {
        hudCard {
            sectionTitle("POWER")
            sectionDesc("Measured via powermetrics / SMC hardware counters.")

            // Large package-watts number, colour-coded by load level
            let pColor = snap.pkgWatts < 30 ? theme.green
                       : snap.pkgWatts < 60 ? theme.amber
                       :                      theme.red
            HStack(alignment: .firstTextBaseline, spacing: 2) {
                Text(String(format: "%.2f", snap.pkgWatts))
                    .font(.system(size: 28, weight: .bold, design: .monospaced))
                    .foregroundColor(pColor)
                Text("W")
                    .font(.system(size: 14, design: .monospaced))
                    .foregroundColor(theme.textDim)
            }

            if snap.tdpWatts > 0 {
                labeledValue("TDP",
                             String(format: "%.0f%% of %.0fW", snap.tdpPct, snap.tdpWatts))
            }

            Divider().background(theme.border).padding(.vertical, 3)

            // Sub-domain breakdown.
            // On Intel laptops these are often estimates (marked with ~).
            // On Apple Silicon or when RAPL exposes them, they are real readings.
            let est = snap.subDomainsEstimated
            if snap.coreWatts   > 0 { domainRow(est ? "~CORES"  : "CORES",  snap.coreWatts,   theme.cyan)   }
            if snap.gpuWatts    > 0 { domainRow(est ? "~GPU"    : "GPU",    snap.gpuWatts,    theme.green)  }
            if snap.dramWatts   > 0 { domainRow(est ? "~DRAM"   : "DRAM",   snap.dramWatts,   theme.purple) }
            if snap.uncoreWatts > 0 { domainRow(est ? "~UNCORE" : "UNCORE", snap.uncoreWatts, theme.blue)   }

            Divider().background(theme.border).padding(.vertical, 3)

            HStack(spacing: 12) {
                labeledValue("AVG",  String(format: "%.2fW", snap.avgPkgWatts))
                labeledValue("PEAK", String(format: "%.2fW", snap.maxPkgWatts))
            }
        }
        .frame(minWidth: 240)
    }

    // ── Temperature card ──────────────────────────────────────────────────────

    var tempCard: some View {
        hudCard {
            sectionTitle("TEMPERATURE")
            sectionDesc("Die temp. 80 C+ = throttle risk. 100 C = throttling active.")

            if snap.zones.isEmpty {
                Text("no thermal zones found")
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundColor(theme.textDim)
            } else {
                ForEach(snap.zones.prefix(6)) { zone in
                    let tc = zone.tempC < 60 ? theme.green
                           : zone.tempC < 80 ? theme.amber
                           :                   theme.red
                    HStack {
                        Text(zone.name)
                            .font(.system(size: 11, design: .monospaced))
                            .foregroundColor(theme.textDim)
                        Spacer()
                        Text(String(format: "%.1f C", zone.tempC))
                            .font(.system(size: 11, weight: .bold, design: .monospaced))
                            .foregroundColor(tc)
                    }
                    .padding(.vertical, 1)
                }
            }

            // ── Fan speeds ────────────────────────────────────────────────────
            // MBP15,3 note: powermetrics only exposes one fan key ("fan") on
            // this model — it appears to be an average or the right fan only.
            // fan_0 / fan_1 are not in the plist. We show what we have.
            if snap.fan0RPM > 0 || snap.fan1RPM > 0 {
                Divider().background(theme.border).padding(.vertical, 3)

                if snap.fan0RPM > 0 {
                    fanRow(label: snap.fan1RPM > 0 ? "Fan L" : "Fan (avg)",
                           rpm: snap.fan0RPM)
                }
                if snap.fan1RPM > 0 {
                    fanRow(label: "Fan R", rpm: snap.fan1RPM)
                }
            }

            // Thermal load level (0–100). Not a throttle flag — 25 is normal.
            if snap.thermalLevel > 0 {
                let tlColor = snap.thermalLevel < 50 ? theme.green
                            : snap.thermalLevel < 80 ? theme.amber
                            :                          theme.red
                HStack {
                    Text("Thermal load")
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundColor(theme.textDim)
                    Spacer()
                    Text("\(snap.thermalLevel)")
                        .font(.system(size: 11, weight: .bold, design: .monospaced))
                        .foregroundColor(tlColor)
                }
            }
        }
        .frame(minWidth: 210)
    }

    // ── Utilisation card ──────────────────────────────────────────────────────

    var utilCard: some View {
        hudCard {
            sectionTitle("UTILIZATION")
            sectionDesc("% time cores spent doing work vs. idle.")

            let avg = snap.cores.isEmpty ? 0.0 :
                snap.cores.map(\.utilPct).reduce(0, +) / Double(snap.cores.count)
            let uc = avg < 50 ? theme.green
                   : avg < 80 ? theme.amber
                   :             theme.red

            HStack(alignment: .firstTextBaseline, spacing: 2) {
                Text(String(format: "%.1f", avg))
                    .font(.system(size: 28, weight: .bold, design: .monospaced))
                    .foregroundColor(uc)
                Text("%")
                    .font(.system(size: 14, design: .monospaced))
                    .foregroundColor(theme.textDim)
            }
            Text("avg across all cores")
                .font(.system(size: 11, design: .monospaced))
                .foregroundColor(theme.textDim)
        }
        .frame(minWidth: 210)
    }

    // MARK: - Graphs section

    /// Temperature colour helpers — computed once per render cycle and reused
    /// in both the normal card and the zoom overlay.
    private var tempColor: (r: Double, g: Double, b: Double) {
        let t = snap.zones.first?.tempC ?? 0
        if t < 60 { return (0.10, 0.90, 0.50) }
        if t < 80 { return (1.00, 0.65, 0.00) }
        return (1.00, 0.25, 0.28)
    }

    var graphsSection: some View {
        VStack(spacing: 0) {

            // Normal 3-up row (hidden when a graph is zoomed)
            if zoom == .none {
                HStack(spacing: 0) {
                    graphCard(
                        title:   "PACKAGE POWER  //  60s",
                        target:  .power,
                        history: snap.pkgHistory,
                        count:   snap.historyCount,
                        pos:     snap.historyPos,
                        maxVal:  max(snap.maxPkgWatts * 1.1, snap.tdpWatts * 1.1, 20),
                        r: 0.18, g: 0.60, b: 1.00,
                        cur:     snap.pkgWatts,
                        unit:    "W"
                    )
                    graphCard(
                        title:   "TEMPERATURE  //  60s",
                        target:  .temp,
                        history: snap.tempHistory,
                        count:   snap.historyCount,
                        pos:     snap.historyPos,
                        maxVal:  100,
                        r: tempColor.r, g: tempColor.g, b: tempColor.b,
                        cur:     snap.zones.first?.tempC ?? 0,
                        unit:    "C"
                    )
                    graphCard(
                        title:   "CPU UTILIZATION  //  60s",
                        target:  .util,
                        history: snap.utilHistory,
                        count:   snap.historyCount,
                        pos:     snap.historyPos,
                        maxVal:  100,
                        r: 0.10, g: 0.90, b: 0.50,
                        cur:     snap.cores.isEmpty ? 0 :
                                 snap.cores.map(\.utilPct).reduce(0, +) / Double(snap.cores.count),
                        unit:    "%"
                    )
                }
            }

            // Zoomed graph — shown instead of the 3-up row
            if zoom != .none {
                zoomedGraphCard
            }
        }
    }

    /// One graph card in the 3-up row. Clicking it sets `zoom`.
    func graphCard(title: String, target: ZoomTarget,
                   history: [Double], count: Int, pos: Int,
                   maxVal: Double, r: Double, g: Double, b: Double,
                   cur: Double, unit: String) -> some View {
        hudCard {
            sectionTitle(title)
            HUDGraphView(
                history: history, count: count, pos: pos,
                maxVal:  max(maxVal, 1),
                lineR: r, lineG: g, lineB: b,
                label: title, curVal: cur, unit: unit,
                theme: theme
            )
            .frame(height: 100)
            .onTapGesture {
                withAnimation(.easeInOut(duration: 0.18)) {
                    zoom = (zoom == target) ? .none : target
                }
            }
            .help("Click to zoom")
            .overlay(alignment: .topTrailing) {
                // Small zoom icon hint
                Image(systemName: "arrow.up.left.and.arrow.down.right")
                    .font(.system(size: 8))
                    .foregroundColor(theme.textDim.opacity(0.4))
                    .padding(4)
            }
        }
    }

    /// Full-width zoomed graph. Clicking it collapses back to the 3-up row.
    var zoomedGraphCard: some View {
        // Resolve the parameters for whichever graph is active.
        let params: (history: [Double], maxV: Double,
                     r: Double, g: Double, b: Double,
                     cur: Double, unit: String, title: String) = {
            switch zoom {
            case .power:
                return (snap.pkgHistory,
                        max(snap.maxPkgWatts * 1.1, snap.tdpWatts * 1.1, 20),
                        0.18, 0.60, 1.00, snap.pkgWatts, "W",
                        "PACKAGE POWER — ZOOMED")
            case .temp:
                return (snap.tempHistory, 100,
                        tempColor.r, tempColor.g, tempColor.b,
                        snap.zones.first?.tempC ?? 0, "C",
                        "TEMPERATURE — ZOOMED")
            case .util:
                let avg = snap.cores.isEmpty ? 0.0 :
                    snap.cores.map(\.utilPct).reduce(0, +) / Double(snap.cores.count)
                return (snap.utilHistory, 100,
                        0.10, 0.90, 0.50, avg, "%",
                        "CPU UTILIZATION — ZOOMED")
            case .none:
                return ([], 1, 0, 0, 0, 0, "", "")
            }
        }()

        return hudCard {
            HStack {
                sectionTitle(params.title)
                Spacer()
                Text("[tap to close]")
                    .font(.system(size: 10, design: .monospaced))
                    .foregroundColor(theme.textDim.opacity(0.5))
            }
            HUDGraphView(
                history: params.history,
                count:   snap.historyCount,
                pos:     snap.historyPos,
                maxVal:  max(params.maxV, 1),
                lineR:   params.r, lineG: params.g, lineB: params.b,
                label:   params.title, curVal: params.cur, unit: params.unit,
                theme:   theme
            )
            .frame(height: 200)
            .onTapGesture {
                withAnimation(.easeInOut(duration: 0.18)) { zoom = .none }
            }
        }
    }

    // MARK: - Cores section

    var coresSection: some View {
        hudCard {
            HStack {
                sectionTitle("CORES")
                Text("//  freq / load / util / est.W")
                    .font(.system(size: 10, design: .monospaced))
                    .foregroundColor(theme.textDim)
                Spacer()
            }
            sectionDesc("Est. W = RAPL budget weighted by util × freq². Approximation only.")

            if heatmapMode {
                CoreHeatmapView(cores: snap.cores, theme: theme)
            } else {
                CoreRowsView(cores: snap.cores, theme: theme)
            }
        }
    }

    // MARK: - Status bar
    private var statusText: String {
        var text = "\(AppConstants.appName) v\(AppConstants.appVersion)"
        text += "  ·  SAMPLE #\(snap.sampleCount)"
        text += "  ·  \(snap.cores.count) CORES"
        text += "  ·  \(AppConstants.refreshPresets[refreshIdx])ms"
        
        if paused {
            text += "  [PAUSED]"
        }
        
        return text
    }
    var statusBar: some View {
        HStack {
               Text(statusText)
                   .font(.system(size: 10, design: .monospaced))
                   .foregroundColor(theme.textDim)

               Spacer()
           }
           .padding(.horizontal, 12)
           .padding(.vertical, 5)
           .background(darkTheme ? Color(hex: "#080b10") : Color(hex: "#dde2ea"))
           .overlay(alignment: .top) {
               Rectangle().fill(theme.border).frame(height: 1)
           }
    }

    // MARK: - Reusable component builders

    /// Card container with consistent padding, background, and border.
    func hudCard<Content: View>(@ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 4) { content() }
            .padding(12)
            .background(theme.bg1)
            .overlay(RoundedRectangle(cornerRadius: 4).stroke(theme.border, lineWidth: 1))
            .cornerRadius(4)
            .padding(4)
    }

    /// All-caps cyan section header with letter spacing.
    func sectionTitle(_ text: String) -> some View {
        Text(text)
            .font(.system(size: 10, weight: .bold, design: .monospaced))
            .foregroundColor(theme.cyan)
            .tracking(2)
    }

    /// Muted italic description line below a section title.
    func sectionDesc(_ text: String) -> some View {
        Text(text)
            .font(.system(size: 10, design: .monospaced))
            .foregroundColor(theme.textDim.opacity(0.6))
            .italic()
    }

    /// Coloured label + bold watts value — used for CORES / GPU / DRAM / UNCORE.
    func domainRow(_ label: String, _ watts: Double, _ color: Color) -> some View {
        HStack(spacing: 4) {
            Text(label)
                .font(.system(size: 11, design: .monospaced))
                .foregroundColor(color)
            Text(String(format: "%.2f W", watts))
                .font(.system(size: 11, weight: .bold, design: .monospaced))
                .foregroundColor(theme.text)
            Spacer()
        }
    }

    /// Muted label + bold value — used for TDP, AVG, PEAK.
    func labeledValue(_ label: String, _ value: String) -> some View {
        HStack(spacing: 4) {
            Text(label)
                .font(.system(size: 10, design: .monospaced))
                .foregroundColor(theme.textDim)
            Text(value)
                .font(.system(size: 11, weight: .bold, design: .monospaced))
                .foregroundColor(theme.text)
        }
    }

    /// Fan RPM row with cyan value. Adapts label for single vs dual-fan Macs.
    func fanRow(label: String, rpm: Double) -> some View {
        HStack {
            Text(label)
                .font(.system(size: 11, design: .monospaced))
                .foregroundColor(theme.textDim)
            Spacer()
            Text(String(format: "%.0f RPM", rpm))
                .font(.system(size: 11, weight: .bold, design: .monospaced))
                .foregroundColor(theme.cyan)
        }
    }

    /// Toolbar button with HUD styling.
    func hudButton(_ label: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(label)
                .font(.system(size: 10, weight: .bold, design: .monospaced))
                .foregroundColor(theme.cyan)
                .padding(.horizontal, 10)
                .padding(.vertical, 4)
                .background(theme.bg2)
                .overlay(RoundedRectangle(cornerRadius: 2).stroke(theme.border, lineWidth: 1))
                .cornerRadius(2)
        }
        .buttonStyle(.plain)
        .focusable(false)
    }
}

// MARK: - Preview

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
            .frame(width: 900, height: 700)
    }
}

