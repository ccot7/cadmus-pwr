// Created by Cadmus of Tyre (@ccot7) on 4/7/26.
// Theme.swift
// Color palette for dark and light HUD themes.
// Compatible with Intel and Apple Silicon Macs.

import SwiftUI

// MARK: - Theme struct

struct HUDTheme {
    // SwiftUI colors (for views, labels, backgrounds)
    let bg0:     Color
    let bg1:     Color
    let bg2:     Color
    let border:  Color
    let lineHi:  Color
    let cyan:    Color
    let green:   Color
    let amber:   Color
    let red:     Color
    let blue:    Color
    let purple:  Color
    let text:    Color
    let textDim: Color

    // CGColor equivalents (for Canvas drawing)
    let cg_bg0:     CGColor
    let cg_bg1:     CGColor
    let cg_bg2:     CGColor
    let cg_line:    CGColor
    let cg_lineHi:  CGColor
    let cg_cyan:    CGColor
    let cg_green:   CGColor
    let cg_amber:   CGColor
    let cg_red:     CGColor
    let cg_text:    CGColor
    let cg_textDim: CGColor
}

// MARK: - Prebuilt themes

extension HUDTheme {

    static let dark = HUDTheme(
        bg0:     Color(hex: "#0a0d14"),
        bg1:     Color(hex: "#12171f"),
        bg2:     Color(hex: "#1a2130"),
        border:  Color(hex: "#1a2e48"),
        lineHi:  Color(hex: "#2d8ce6"),
        cyan:    Color(hex: "#00d9ff"),
        green:   Color(hex: "#18e67f"),
        amber:   Color(hex: "#ffa600"),
        red:     Color(hex: "#ff4047"),
        blue:    Color(hex: "#2d99ff"),
        purple:  Color(hex: "#a659ff"),
        text:    Color(hex: "#e0ebff"),
        textDim: Color(hex: "#6b85ad"),
        cg_bg0:     CGColor.hex("#0a0d14"),
        cg_bg1:     CGColor.hex("#12171f"),
        cg_bg2:     CGColor.hex("#1a2130"),
        cg_line:    CGColor.hex("#213859"),
        cg_lineHi:  CGColor.hex("#2d8ce6"),
        cg_cyan:    CGColor.hex("#00d9ff"),
        cg_green:   CGColor.hex("#18e67f"),
        cg_amber:   CGColor.hex("#ffa600"),
        cg_red:     CGColor.hex("#ff4047"),
        cg_text:    CGColor.hex("#e0ebff"),
        cg_textDim: CGColor.hex("#6b85ad")
    )

    static let light = HUDTheme(
        bg0:     Color(hex: "#f2f4f8"),
        bg1:     Color(hex: "#e6eaf0"),
        bg2:     Color(hex: "#fafbff"),
        border:  Color(hex: "#c0cad8"),
        lineHi:  Color(hex: "#2d7acc"),
        cyan:    Color(hex: "#0073cc"),
        green:   Color(hex: "#0d9945"),
        amber:   Color(hex: "#cc7200"),
        red:     Color(hex: "#d91820"),
        blue:    Color(hex: "#1a5cb0"),
        purple:  Color(hex: "#6622cc"),
        text:    Color(hex: "#1a1e2e"),
        textDim: Color(hex: "#607090"),
        cg_bg0:     CGColor.hex("#f2f4f8"),
        cg_bg1:     CGColor.hex("#e6eaf0"),
        cg_bg2:     CGColor.hex("#fafbff"),
        cg_line:    CGColor.hex("#c0cad8"),
        cg_lineHi:  CGColor.hex("#2d7acc"),
        cg_cyan:    CGColor.hex("#0073cc"),
        cg_green:   CGColor.hex("#0d9945"),
        cg_amber:   CGColor.hex("#cc7200"),
        cg_red:     CGColor.hex("#d91820"),
        cg_text:    CGColor.hex("#1a1e2e"),
        cg_textDim: CGColor.hex("#607090")
    )

    // Returns (CGColor, SwiftUI Color) for a utilisation/power percentage.
    func dataColor(pct: Double) -> (CGColor, Color) {
        if pct < 50 { return (cg_green, green) }
        if pct < 80 { return (cg_amber, amber) }
        return (cg_red, red)
    }
}

// MARK: - Color extensions

extension Color {
    /// Initialise a SwiftUI Color from a CSS hex string e.g. "#0a0d14".
    init(hex: String) {
        let h = hex.trimmingCharacters(in: CharacterSet(charactersIn: "#"))
        var rgb: UInt64 = 0
        Scanner(string: h).scanHexInt64(&rgb)
        self.init(
            red:   Double((rgb >> 16) & 0xFF) / 255,
            green: Double((rgb >>  8) & 0xFF) / 255,
            blue:  Double( rgb        & 0xFF) / 255
        )
    }
}

extension CGColor {
    /// Create a CGColor from a CSS hex string e.g. "#0a0d14".
    static func hex(_ hex: String) -> CGColor {
        let h = hex.trimmingCharacters(in: CharacterSet(charactersIn: "#"))
        var rgb: UInt64 = 0
        Scanner(string: h).scanHexInt64(&rgb)
        return CGColor(
            red:   CGFloat((rgb >> 16) & 0xFF) / 255,
            green: CGFloat((rgb >>  8) & 0xFF) / 255,
            blue:  CGFloat( rgb        & 0xFF) / 255,
            alpha: 1
        )
    }
}
