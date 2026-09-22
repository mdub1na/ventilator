import AppKit

enum StatusArtwork {
    static func render(on statusItem: NSStatusItem, level: Int?, temperature: Double?) {
        let image = NSImage(size: NSSize(width: 63, height: 17))
        image.lockFocus()
        let appearance = statusItem.button?.effectiveAppearance.bestMatch(from: [.aqua, .darkAqua])
        let outline: NSColor = appearance == .darkAqua ? .white : .black
        drawFanAndAirflow(outline: outline)
        let colors: [NSColor] = [
            NSColor(srgbRed: 0.18, green: 0.72, blue: 0.35, alpha: 1),
            NSColor(srgbRed: 0.53, green: 0.78, blue: 0.27, alpha: 1),
            NSColor(srgbRed: 0.95, green: 0.75, blue: 0.20, alpha: 1),
            NSColor(srgbRed: 0.96, green: 0.48, blue: 0.19, alpha: 1),
            NSColor(srgbRed: 0.88, green: 0.23, blue: 0.25, alpha: 1),
        ]
        for index in 0..<5 {
            let height = CGFloat(5 + index * 2)
            let rectangle = NSRect(x: 27 + CGFloat(index * 7), y: 2, width: 5, height: height)
            let path = NSBezierPath(roundedRect: rectangle, xRadius: 1, yRadius: 1)
            if let level, index < level {
                colors[index].setFill()
                path.fill()
            }
            outline.setStroke()
            path.lineWidth = 0.75
            path.stroke()
        }
        image.unlockFocus()
        image.isTemplate = false
        statusItem.button?.image = image
        statusItem.button?.imagePosition = .imageLeft
        statusItem.button?.title = temperature.map { String(format: "%.0f°C", $0) } ?? "—°C"
        statusItem.button?.toolTip = "Ventilator · CPU: максимум кристалла (TCMz); шкала вентиляторов"
    }

    private static func drawFanAndAirflow(outline: NSColor) {
        outline.setStroke()
        let housing = NSBezierPath(ovalIn: NSRect(x: 1, y: 2.5, width: 12, height: 12))
        housing.lineWidth = 0.8
        housing.stroke()

        let center = NSPoint(x: 7, y: 8.5)
        func point(radius: CGFloat, angle: CGFloat) -> NSPoint {
            NSPoint(x: center.x + radius * cos(angle), y: center.y + radius * sin(angle))
        }
        outline.setFill()
        for blade in 0..<3 {
            let angle = CGFloat(blade) * .pi * 2 / 3
            let shape = NSBezierPath()
            shape.move(to: point(radius: 1.1, angle: angle - 0.3))
            shape.curve(to: point(radius: 4.7, angle: angle - 0.2),
                        controlPoint1: point(radius: 2.7, angle: angle - 0.9),
                        controlPoint2: point(radius: 4.2, angle: angle - 0.8))
            shape.curve(to: point(radius: 1.4, angle: angle + 0.5),
                        controlPoint1: point(radius: 4.7, angle: angle + 0.5),
                        controlPoint2: point(radius: 2.1, angle: angle + 0.9))
            shape.close()
            shape.fill()
        }
        NSBezierPath(ovalIn: NSRect(x: 6.1, y: 7.6, width: 1.8, height: 1.8)).fill()

        outline.setStroke()
        let airflow = NSBezierPath()
        airflow.lineWidth = 0.8
        airflow.lineCapStyle = .round
        airflow.move(to: NSPoint(x: 15, y: 4.5))
        airflow.curve(to: NSPoint(x: 21, y: 4.5),
                      controlPoint1: NSPoint(x: 17, y: 5.5), controlPoint2: NSPoint(x: 19, y: 3.5))
        airflow.move(to: NSPoint(x: 15, y: 8.5))
        airflow.line(to: NSPoint(x: 24, y: 8.5))
        airflow.move(to: NSPoint(x: 21.5, y: 10))
        airflow.line(to: NSPoint(x: 24, y: 8.5))
        airflow.line(to: NSPoint(x: 21.5, y: 7))
        airflow.move(to: NSPoint(x: 15, y: 12.5))
        airflow.curve(to: NSPoint(x: 21, y: 12.5),
                      controlPoint1: NSPoint(x: 17, y: 13.5), controlPoint2: NSPoint(x: 19, y: 11.5))
        airflow.stroke()
    }
}
