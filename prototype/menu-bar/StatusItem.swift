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
        StatusArtwork.render(on: statusItem, level: nil, temperature: nil)
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
            StatusArtwork.render(on: statusItem, level: nil, temperature: nil)
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
        StatusArtwork.render(on: statusItem, level: snapshot.combinedLevel, temperature: snapshot.cpuTempC)
    }
}

@main
private enum StatusItemMain {
    static func main() {
        guard CommandLine.arguments.count == 2 else {
            fputs("Usage: status-item <path-to-smc-read>\n", stderr)
            exit(EXIT_FAILURE)
        }
        let probe = URL(fileURLWithPath: CommandLine.arguments[1], relativeTo: URL(fileURLWithPath: FileManager.default.currentDirectoryPath)).standardizedFileURL
        let app = NSApplication.shared
        app.setActivationPolicy(.accessory)
        let controller = StatusItemController(probe: probe)
        withExtendedLifetime(controller) { app.run() }
    }
}
