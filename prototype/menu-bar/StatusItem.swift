import AppKit
import Foundation

// Native macOS status-item spike. Read-only: it launches smc-read --status-json.
private struct Fan: Decodable {
    let index: Int
    let actualRpm: Double?
    let minRpm: Double?
    let maxRpm: Double?

    enum CodingKeys: String, CodingKey {
        case index
        case actualRpm = "actual_rpm"
        case minRpm = "min_rpm"
        case maxRpm = "max_rpm"
    }

    var level: Int? {
        guard let actualRpm, actualRpm.isFinite, actualRpm >= 0 else { return nil }
        if actualRpm == 0 { return 0 }
        guard let minRpm, let maxRpm, minRpm.isFinite, maxRpm.isFinite,
              minRpm >= 0, maxRpm > minRpm else { return nil }
        let fraction = min(1, max(0, (actualRpm - minRpm) / (maxRpm - minRpm)))
        return min(5, 1 + Int(floor(5 * fraction)))
    }
}

private struct Snapshot: Decodable {
    let schema: Int
    let fans: [Fan]
    let cpuKey: String
    let cpuTempC: Double?

    enum CodingKeys: String, CodingKey {
        case schema, fans
        case cpuKey = "cpu_key"
        case cpuTempC = "cpu_temp_c"
    }

    var combinedLevel: Int? {
        guard !fans.isEmpty else { return nil }
        let levels = fans.map(\.level)
        guard levels.allSatisfy({ $0 != nil }) else { return nil }
        return levels.compactMap { $0 }.max()
    }
}

private final class StatusItemController: NSObject, NSMenuDelegate {
    private let probe: URL
    private let statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private let menu = NSMenu()
    private let cpuItem = NSMenuItem(title: "CPU: — °C", action: nil, keyEquivalent: "")
    private let fanItems = (0..<2).map { NSMenuItem(title: "Вентилятор \($0): — RPM", action: nil, keyEquivalent: "") }
    private var reading = false

    init(probe: URL) {
        self.probe = probe
        super.init()
        cpuItem.isEnabled = false
        menu.addItem(cpuItem)
        for item in fanItems {
            item.isEnabled = false
            menu.addItem(item)
        }
        menu.addItem(.separator())
        let quitItem = NSMenuItem(title: "Выход", action: #selector(quit), keyEquivalent: "q")
        quitItem.target = self
        menu.addItem(quitItem)
        menu.delegate = self
        statusItem.menu = menu
        render(level: nil, temperature: nil)
        let timer = Timer(timeInterval: 2, repeats: true) { [weak self] _ in self?.refresh() }
        // The default run-loop mode pauses while an AppKit menu tracks pointer events.
        RunLoop.main.add(timer, forMode: .common)
        refresh()
    }

    func menuWillOpen(_ menu: NSMenu) {
        refresh()
    }

    @objc private func quit() {
        NSApplication.shared.terminate(nil)
    }

    private func refresh() {
        guard !reading else { return }
        reading = true
        let probe = self.probe
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let snapshot = Self.read(probe)
            DispatchQueue.main.async {
                guard let self else { return }
                self.reading = false
                self.show(snapshot)
            }
        }
    }

    private static func read(_ probe: URL) -> Snapshot? {
        let process = Process()
        process.executableURL = probe
        process.arguments = ["--status-json"]
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = Pipe()
        do {
            try process.run()
            process.waitUntilExit()
            guard process.terminationStatus == 0 else { return nil }
            let data = pipe.fileHandleForReading.readDataToEndOfFile()
            let snapshot = try JSONDecoder().decode(Snapshot.self, from: data)
            guard snapshot.schema == 1, snapshot.cpuKey == "TCMz" else { return nil }
            return snapshot
        } catch {
            return nil
        }
    }

    private func show(_ snapshot: Snapshot?) {
        guard let snapshot else {
            cpuItem.title = "Датчики недоступны"
            fanItems.forEach { $0.title = "Вентилятор: — RPM" }
            render(level: nil, temperature: nil)
            return
        }
        cpuItem.title = snapshot.cpuTempC.map { String(format: "CPU · максимум кристалла (TCMz): %.0f °C", $0) }
            ?? "CPU · максимум кристалла (TCMz): — °C"
        for (index, item) in fanItems.enumerated() {
            if let fan = snapshot.fans.first(where: { $0.index == index }) {
                item.title = fan.actualRpm.map { String(format: "Вентилятор %d: %.0f RPM", index, $0) }
                    ?? "Вентилятор \(index): — RPM"
            } else {
                item.title = "Вентилятор \(index): — RPM"
            }
        }
        render(level: snapshot.combinedLevel, temperature: snapshot.cpuTempC)
    }

    private func render(level: Int?, temperature: Double?) {
        let image = NSImage(size: NSSize(width: 63, height: 17))
        image.lockFocus()
        drawFanAndAirflow()
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
            // Outline every segment, including filled ones, for a consistent gauge on a dark menu bar.
            NSColor.white.setStroke()
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

    private func drawFanAndAirflow() {
        NSColor.white.setStroke()
        let housing = NSBezierPath(ovalIn: NSRect(x: 1, y: 2.5, width: 12, height: 12))
        housing.lineWidth = 0.8
        housing.stroke()

        let center = NSPoint(x: 7, y: 8.5)
        func point(radius: CGFloat, angle: CGFloat) -> NSPoint {
            NSPoint(x: center.x + radius * cos(angle), y: center.y + radius * sin(angle))
        }
        NSColor.white.setFill()
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

        NSColor.white.setStroke()
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

guard CommandLine.arguments.count == 2 else {
    fputs("Usage: status-item <path-to-smc-read>\n", stderr)
    exit(EXIT_FAILURE)
}
let probe = URL(fileURLWithPath: CommandLine.arguments[1], relativeTo: URL(fileURLWithPath: FileManager.default.currentDirectoryPath)).standardizedFileURL
let app = NSApplication.shared
app.setActivationPolicy(.accessory)
private let controller = StatusItemController(probe: probe)
withExtendedLifetime(controller) { app.run() }
