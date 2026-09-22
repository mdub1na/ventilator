import AppKit
import Foundation

// Receives read-only snapshots from the Kotlin app. It never opens AppleSMC itself.
private final class StatusItemBridge: NSObject, NSMenuDelegate {
    private let statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private let menu = NSMenu()
    private let cpuItem = NSMenuItem(title: "CPU · максимум кристалла (TCMz): — °C", action: nil, keyEquivalent: "")
    private let fanItems = (0..<2).map { NSMenuItem(title: "Вентилятор \($0): — RPM", action: nil, keyEquivalent: "") }
    private let windowItem = NSMenuItem(title: "Скрыть окно", action: #selector(toggleWindow), keyEquivalent: "")
    private var visible = true
    private var level: Int?
    private var temperature: Double?
    private var contextMonitor: Any?

    override init() {
        super.init()
        cpuItem.isEnabled = false
        menu.addItem(cpuItem)
        fanItems.forEach { item in
            item.isEnabled = false
            menu.addItem(item)
        }
        menu.addItem(.separator())
        windowItem.target = self
        menu.addItem(windowItem)
        let quitItem = NSMenuItem(title: "Выход", action: #selector(quit), keyEquivalent: "q")
        quitItem.target = self
        menu.addItem(quitItem)
        menu.delegate = self
        statusItem.button?.target = self
        statusItem.button?.action = #selector(statusClicked)
        statusItem.button?.sendAction(on: [.leftMouseUp])
        contextMonitor = NSEvent.addLocalMonitorForEvents(matching: .rightMouseDown) { [weak self] event in
            guard let self, let button = self.statusItem.button, let window = button.window,
                  event.windowNumber == window.windowNumber,
                  button.bounds.contains(button.convert(event.locationInWindow, from: nil)) else { return event }
            NSMenu.popUpContextMenu(self.menu, with: event, for: button)
            return nil
        }
        StatusArtwork.render(on: statusItem, level: nil, temperature: nil)
    }

    deinit {
        if let contextMonitor { NSEvent.removeMonitor(contextMonitor) }
    }

    func menuWillOpen(_ menu: NSMenu) {
        StatusArtwork.render(on: statusItem, level: level, temperature: temperature)
    }

    @objc private func statusClicked() {
        print("toggle")
        fflush(stdout)
    }

    func accept(_ line: String) {
        let fields = line.split(separator: "\t", omittingEmptySubsequences: false).map(String.init)
        guard fields.count == 6, fields[0] == "snapshot",
              fields[5] == "0" || fields[5] == "1" else { return }
        let parsedLevel = Int(fields[1])
        level = parsedLevel.map { (0...5).contains($0) ? $0 : nil } ?? nil
        temperature = Double(fields[2]).flatMap { $0.isFinite && (10...115).contains($0) ? $0 : nil }
        let rpms = fields[3...4].map { field -> Double? in
            Double(field).flatMap { $0.isFinite && $0 >= 0 ? $0 : nil }
        }
        visible = fields[5] == "1"
        windowItem.title = visible ? "Скрыть окно" : "Открыть окно"
        cpuItem.title = temperature.map { String(format: "CPU · максимум кристалла (TCMz): %.0f °C", $0) }
            ?? "CPU · максимум кристалла (TCMz): — °C"
        for index in fanItems.indices {
            fanItems[index].title = rpms[index].map { String(format: "Вентилятор %d: %.0f RPM", index, $0) }
                ?? "Вентилятор \(index): — RPM"
        }
        StatusArtwork.render(on: statusItem, level: level, temperature: temperature)
    }

    @objc private func toggleWindow() {
        print(visible ? "hide" : "show")
        fflush(stdout)
    }

    @objc private func quit() {
        print("quit")
        fflush(stdout)
    }
}

@main
private enum StatusItemBridgeMain {
    static func main() {
        guard CommandLine.arguments.count == 2, CommandLine.arguments[1] == "--stdio" else {
            fputs("Usage: status-item-bridge --stdio\n", stderr)
            exit(EXIT_FAILURE)
        }

        let app = NSApplication.shared
        app.setActivationPolicy(.accessory)
        let controller = StatusItemBridge()
        DispatchQueue.global(qos: .utility).async {
            while let line = readLine() {
                DispatchQueue.main.async { controller.accept(line) }
            }
            DispatchQueue.main.async { app.terminate(nil) }
        }
        withExtendedLifetime(controller) { app.run() }
    }
}
