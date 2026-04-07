// Created by Cadmus of Tyre (@ccot7) on 4/7/26.
// CoreViews.swift
// Per-core row view (segmented VU bars) and heatmap view.

import SwiftUI

// MARK: - Single core row

struct CoreRowView: View {
    let core:  CoreInfo
    let theme: HUDTheme

    private var dataColor: Color {
        theme.dataColor(pct: core.utilPct).1
    }

    var body: some View {
        Canvas { ctx, size in
            let w = size.width, h = size.height
            let t = theme

            // Background
            ctx.fill(Path(CGRect(x:0,y:0,width:w,height:h)),
                     with:.color(t.bg2))

            // Left accent bar
            ctx.fill(Path(CGRect(x:0,y:0,width:3,height:h)),
                     with:.color(dataColor.opacity(0.9)))

            let font = Font.system(size: 10, design: .monospaced).bold()

            // Core ID
            let idStr  = String(format: "C%02d", core.id)
            var gCtx   = ctx
            gCtx.draw(gCtx.resolve(Text(idStr).font(font).foregroundColor(t.textDim)),
                      at: CGPoint(x: 8, y: h/2), anchor: .leading)

            // Frequency
            let freqStr = String(format: "%.2fG", core.freqMHz / 1000.0)
            gCtx.draw(gCtx.resolve(Text(freqStr).font(font).foregroundColor(t.cyan)),
                      at: CGPoint(x: 52, y: h/2), anchor: .leading)

            // Segmented bar
            let barX: CGFloat = 106
            let barW = w - barX - 100
            if barW > 20 {
                drawSegBar(ctx: &gCtx, x: barX, y: (h - 6)/2,
                           w: barW, h: 6, pct: core.utilPct, theme: t)
            }

            // Util %
            let utilStr = String(format: "%3.0f%%", core.utilPct)
            gCtx.draw(gCtx.resolve(Text(utilStr).font(font).foregroundColor(dataColor)),
                      at: CGPoint(x: w - 68, y: h/2), anchor: .leading)

            // Est. W
            let pwrStr = String(format: "~%.2fW", core.estimatedWatts)
            gCtx.draw(gCtx.resolve(Text(pwrStr).font(font).foregroundColor(t.amber)),
                      at: CGPoint(x: w - 4, y: h/2), anchor: .trailing)

            // Bottom border
            ctx.stroke(Path { p in p.move(to:.init(x:0,y:h-0.5)); p.addLine(to:.init(x:w,y:h-0.5)) },
                       with:.color(t.border.opacity(0.4)), lineWidth:0.5)
        }
        .frame(height: 22)
    }
}

// MARK: - Segmented VU bar helper

func drawSegBar(ctx: inout GraphicsContext, x: CGFloat, y: CGFloat,
                w: CGFloat, h: CGFloat, pct: Double, theme: HUDTheme) {
    let segs   = 20
    let sw     = (w - CGFloat(segs - 1) * 1.5) / CGFloat(segs)
    let filled = Int((pct / 100.0 * Double(segs)).rounded())

    for s in 0..<segs {
        let sx = x + CGFloat(s) * (sw + 1.5)
        let segPct = Double(s) / Double(segs) * 100.0
        let color: Color = s < filled
            ? theme.dataColor(pct: segPct).1
            : theme.border.opacity(0.5)
        ctx.fill(Path(CGRect(x: sx, y: y, width: sw, height: h)),
                 with: .color(color))
    }
}

// MARK: - All core rows

struct CoreRowsView: View {
    let cores: [CoreInfo]
    let theme: HUDTheme

    var body: some View {
        VStack(spacing: 0) {
            ForEach(cores) { core in
                CoreRowView(core: core, theme: theme)
            }
        }
    }
}

// MARK: - Heatmap

struct CoreHeatmapView: View {
    let cores: [CoreInfo]
    let theme: HUDTheme

    private var cols: Int {
        // Will be recalculated by GeometryReader but default sensible
        cores.count > 32 ? 16 : cores.count > 16 ? 8 : 4
    }

    var body: some View {
        GeometryReader { geo in
            let w     = geo.size.width
            let cols  = w > 600 ? 16 : w > 400 ? 8 : 4
            let rows  = (cores.count + cols - 1) / cols
            let cellW = (w - 2) / CGFloat(cols)
            let cellH: CGFloat = max(32, cellW * 0.6)
            let totalH = CGFloat(rows) * cellH + 2

            Canvas { ctx, size in
                ctx.fill(Path(CGRect(x:0,y:0,width:size.width,height:size.height)),
                         with:.color(theme.bg2))

                for (i, core) in cores.enumerated() {
                    let col = i % cols
                    let row = i / cols
                    let cx  = 1 + CGFloat(col) * cellW
                    let cy  = 1 + CGFloat(row) * cellH
                    let cw  = cellW - 1
                    let ch  = cellH - 1

                    let pct = core.utilPct
                    // Interpolate green → amber → red
                    let fillColor: Color
                    if pct < 50 {
                        let f = pct / 50.0
                        fillColor = blend(theme.green, theme.amber, t: f)
                    } else {
                        let f = (pct - 50) / 50.0
                        fillColor = blend(theme.amber, theme.red, t: f)
                    }
                    let alpha = pct < 1 ? 0.15 : 0.80
                    ctx.fill(Path(CGRect(x:cx,y:cy,width:cw,height:ch)),
                             with:.color(fillColor.opacity(alpha)))

                    // Labels
                    let font = Font.system(size: 9, design: .monospaced).bold()
                    let labelColor = theme.bg0.opacity(0.9)
                    if cellW >= 28 {
                        let idStr = "C\(core.id)"
                        var gc = ctx
                        gc.draw(gc.resolve(Text(idStr).font(font).foregroundColor(labelColor)),
                                at: CGPoint(x: cx + 3, y: cy + ch - 4), anchor: .bottomLeading)
                    }
                    if cellW >= 38 && cellH >= 22 {
                        let pStr = String(format: "%.0f%%", pct)
                        var gc = ctx
                        gc.draw(gc.resolve(Text(pStr).font(font).foregroundColor(labelColor)),
                                at: CGPoint(x: cx + 3, y: cy + 4), anchor: .topLeading)
                    }
                }

                // Grid lines
                for c in 0...cols {
                    let gx = 1 + CGFloat(c) * cellW
                    ctx.stroke(Path { p in p.move(to:.init(x:gx,y:0)); p.addLine(to:.init(x:gx,y:size.height)) },
                               with:.color(theme.bg0.opacity(0.5)), lineWidth:0.5)
                }
                for r in 0...rows {
                    let gy = 1 + CGFloat(r) * cellH
                    ctx.stroke(Path { p in p.move(to:.init(x:0,y:gy)); p.addLine(to:.init(x:size.width,y:gy)) },
                               with:.color(theme.bg0.opacity(0.5)), lineWidth:0.5)
                }
            }
            .frame(height: totalH)
        }
        .frame(height: CGFloat((cores.count + cols - 1) / cols) * 36 + 2)
    }

    private func blend(_ a: Color, _ b: Color, t: Double) -> Color {
        // Simple linear blend in sRGB
        let t = max(0, min(1, t))
        return Color(
            red:   lerp(component(a, .red),   component(b, .red),   t: t),
            green: lerp(component(a, .green), component(b, .green), t: t),
            blue:  lerp(component(a, .blue),  component(b, .blue),  t: t)
        )
    }

    private func lerp(_ a: Double, _ b: Double, t: Double) -> Double { a + (b-a)*t }

    private enum Channel { case red, green, blue }
    private func component(_ c: Color, _ ch: Channel) -> Double {
        // Approximate — works for our named hex colors
        let r = c.resolve(in: .init())
        switch ch {
        case .red:   return Double(r.red)
        case .green: return Double(r.green)
        case .blue:  return Double(r.blue)
        }
    }
}
