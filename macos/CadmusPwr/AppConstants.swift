// Created by Cadmus of Tyre (@ccot7) on 4/7/26.
// AppConstants.swift
// ─────────────────────────────────────────────────────────────────────────────
// Single source of truth for every string and number that might need changing.
// To rename the app, change `appName` here — it propagates everywhere.
// ─────────────────────────────────────────────────────────────────────────────

import Foundation

enum AppConstants {

    // ── Identity ──────────────────────────────────────────────────────────────
    /// Display name shown in the window header and title bar.
    static let appName = "CadmusPwr — Power Monitor"
    /// Short subtitle shown next to the name in the header.
    static let appSubtitle = "macOS CPU MONITOR"
    /// Semantic version string shown in the status bar and About panel.
    static let appVersion = "1.0.0"

    // ── Sampling ──────────────────────────────────────────────────────────────
    /// Available refresh intervals in milliseconds (matches toolbar slider).
    static let refreshPresets = [250, 500, 1000, 2000]
    /// Index into `refreshPresets` used on first launch.
    static let defaultRefreshIndex = 2   // 1 000 ms

    // ── History ───────────────────────────────────────────────────────────────
    /// Number of samples retained for the rolling graphs.
    static let historyLength = 60

    // ── Throttle thresholds ───────────────────────────────────────────────────
    /// CPU die temperature (°C) at which we consider the chip throttling.
    static let throttleTempC: Double = 100.0
}
