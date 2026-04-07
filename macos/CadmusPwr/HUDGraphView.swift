// Created by Cadmus of Tyre (@ccot7) on 4/7/26.
// HUDGraphView.swift
// Replicates the Cairo HUD graph from the Linux GTK version using SwiftUI Canvas.
// Scanlines, glow, corner brackets, fill, sharp line, endpoint dot.

import SwiftUI

struct HUDGraphView: View {
    let history:   [Double]   // 60 values
    let count:     Int        // how many are valid
    let pos:       Int        // current write position
    let maxVal:    Double
    let lineR:     Double
    let lineG:     Double
    let lineB:     Double
    let label:     String
    let curVal:    Double
    let unit:      String
    let theme:     HUDTheme

    var body: some View {
        Canvas { ctx, size in
            let w = size.width, h = size.height
            let pad: CGFloat = 20   // bottom label bar height

            // Background
            ctx.fill(Path(CGRect(x: 0, y: 0, width: w, height: h)),
                     with: .color(theme.bg1))

            // Scanlines (dark theme feel)
            var y: CGFloat = 0
            while y < h {
                ctx.stroke(Path { p in p.move(to: CGPoint(x:0,y:y)); p.addLine(to: CGPoint(x:w,y:y)) },
                           with: .color(.black.opacity(0.08)), lineWidth: 1)
                y += 3
            }

            // Horizontal grid
            for i in 1..<4 {
                let gy = h * CGFloat(i) / 4.0
                ctx.stroke(Path { p in p.move(to: CGPoint(x:0,y:gy)); p.addLine(to: CGPoint(x:w,y:gy)) },
                           with: .color(theme.border.opacity(0.6)), lineWidth: 0.5)
            }
            // Vertical grid
            for i in 1..<6 {
                let gx = w * CGFloat(i) / 6.0
                ctx.stroke(Path { p in p.move(to: CGPoint(x:gx,y:0)); p.addLine(to: CGPoint(x:gx,y:h)) },
                           with: .color(theme.border.opacity(0.3)), lineWidth: 0.5)
            }

            guard count >= 2, maxVal > 0 else {
                drawLabelBar(ctx: ctx, size: size, pad: pad)
                drawCornerBrackets(ctx: ctx, size: size)
                return
            }

            let n = count
            let startPos = ((pos - n) % 60 + 60) % 60
            let plotH = h - pad
            let lineColor = Color(red: lineR, green: lineG, blue: lineB)

            func point(_ i: Int) -> CGPoint {
                let idx = (startPos + i) % 60
                let x = CGFloat(i) / CGFloat(59) * w
                let y = plotH - CGFloat(history[idx] / maxVal) * (plotH - 4)
                return CGPoint(x: x, y: y)
            }

            // Fill
            var fillPath = Path()
            fillPath.move(to: CGPoint(x: 0, y: plotH))
            for i in 0..<n { fillPath.addLine(to: point(i)) }
            fillPath.addLine(to: CGPoint(x: w, y: plotH))
            fillPath.closeSubpath()
            ctx.fill(fillPath, with: .color(lineColor.opacity(0.12)))

            // Glow (thick)
            var glowPath = Path()
            for i in 0..<n {
                if i == 0 { glowPath.move(to: point(i)) }
                else       { glowPath.addLine(to: point(i)) }
            }
            ctx.stroke(glowPath, with: .color(lineColor.opacity(0.22)), lineWidth: 5)

            // Sharp line
            var linePath = Path()
            for i in 0..<n {
                if i == 0 { linePath.move(to: point(i)) }
                else       { linePath.addLine(to: point(i)) }
            }
            ctx.stroke(linePath, with: .color(lineColor.opacity(0.95)), lineWidth: 1.5)

            // Endpoint dot
            let ep = point(n - 1)
            ctx.fill(Path(ellipseIn: CGRect(x: ep.x-3, y: ep.y-3, width: 6, height: 6)),
                     with: .color(lineColor))
            ctx.fill(Path(ellipseIn: CGRect(x: ep.x-7, y: ep.y-7, width: 14, height: 14)),
                     with: .color(lineColor.opacity(0.22)))

            drawLabelBar(ctx: ctx, size: size, pad: pad)
            drawCornerBrackets(ctx: ctx, size: size)
        }
    }

    // MARK: Label bar

    private func drawLabelBar(ctx: GraphicsContext, size: CGSize, pad: CGFloat) {
        let w = size.width, h = size.height
        // Strip background
        ctx.fill(Path(CGRect(x: 0, y: h - pad, width: w, height: pad)),
                 with: .color(theme.bg0.opacity(0.88)))
        // Top rule
        ctx.stroke(Path { p in
            p.move(to: CGPoint(x: 0, y: h - pad))
            p.addLine(to: CGPoint(x: w, y: h - pad))
        }, with: .color(theme.lineHi.opacity(0.25)), lineWidth: 0.5)

        // Label text — drawn via ImageRenderer workaround using resolved text
        let font = Font.system(size: 10, design: .monospaced).bold()
        let labelText = Text(label).font(font).foregroundColor(theme.textDim)
        let valStr    = String(format: "%.1f %@", curVal, unit)
        let valText   = Text(valStr).font(font).foregroundColor(Color(red: lineR, green: lineG, blue: lineB))

        var lCtx = ctx
        lCtx.draw(lCtx.resolve(labelText), at: CGPoint(x: 6, y: h - 5), anchor: .bottomLeading)
        lCtx.draw(lCtx.resolve(valText),   at: CGPoint(x: w - 6, y: h - 5), anchor: .bottomTrailing)
    }

    // MARK: Corner brackets

    private func drawCornerBrackets(ctx: GraphicsContext, size: CGSize) {
        let w = size.width, h = size.height
        let sz: CGFloat = 9
        let c = theme.cyan.opacity(0.7)
        let lw: CGFloat = 1.2
        // top-left
        ctx.stroke(Path { p in p.move(to: CGPoint(x:0,y:sz)); p.addLine(to:.init(x:0,y:0)); p.addLine(to:.init(x:sz,y:0)) }, with:.color(c), lineWidth:lw)
        // top-right
        ctx.stroke(Path { p in p.move(to: CGPoint(x:w-sz,y:0)); p.addLine(to:.init(x:w,y:0)); p.addLine(to:.init(x:w,y:sz)) }, with:.color(c), lineWidth:lw)
        // bottom-left
        ctx.stroke(Path { p in p.move(to: CGPoint(x:0,y:h-sz)); p.addLine(to:.init(x:0,y:h)); p.addLine(to:.init(x:sz,y:h)) }, with:.color(c), lineWidth:lw)
        // bottom-right
        ctx.stroke(Path { p in p.move(to: CGPoint(x:w-sz,y:h)); p.addLine(to:.init(x:w,y:h)); p.addLine(to:.init(x:w,y:h-sz)) }, with:.color(c), lineWidth:lw)
    }
}
